#include "ThemeCatalog.hpp"

#include "core/Logger.hpp"
#include "util/Color.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <system_error>

namespace vc {

namespace {

/// WCAG 2.x relative luminance on linearised channels. This deliberately
/// mirrors Theme.qml's relativeLuminance()/contrastRatio() and
/// scripts/check-theme-contrast.py: there is no C++ contrast helper in the
/// tree to reuse, and BT.601 luma() is a different function (Theme.qml §CONTRAST).
double linearize(double value)
{
    return value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
}

double relativeLuminance(const Color& color)
{
    const double r = linearize(static_cast<double>(color.r) / 255.0);
    const double g = linearize(static_cast<double>(color.g) / 255.0);
    const double b = linearize(static_cast<double>(color.b) / 255.0);
    return 0.2126 * r + 0.7152 * g + 0.0722 * b;
}

double contrastRatio(const Color& first, const Color& second)
{
    const double a = relativeLuminance(first);
    const double b = relativeLuminance(second);
    const double hi = std::max(a, b);
    const double lo = std::min(a, b);
    return (hi + 0.05) / (lo + 0.05);
}

QString fileNameOf(const fs::path& path)
{
    return QString::fromStdString(path.filename().string());
}

/// WCAG check for one foreground/background pair that the definition
/// provides explicitly. An absent token is derived by Theme.qml and is
/// not measured here.
std::expected<void, QString> checkContrast(const QString& foregroundName, const Color& foreground,
                                           const QString& backgroundName, const Color& background)
{
    const double ratio = contrastRatio(foreground, background);
    if (ratio < ThemeCatalog::kMinContrastRatio) {
        return std::unexpected(
            QStringLiteral("contrast %1:1 below %2:1 for %3 on %4")
                .arg(ratio, 0, 'f', 1)
                .arg(ThemeCatalog::kMinContrastRatio, 0, 'f', 1)
                .arg(foregroundName)
                .arg(backgroundName));
    }
    return {};
}

} // namespace

bool ThemeCatalog::isValidId(const QString& id)
{
    if (id.isEmpty() || id == QStringLiteral("custom")) {
        return false;
    }
    const auto isAlnum = [](QChar c) {
        return (c >= QLatin1Char('a') && c <= QLatin1Char('z')) ||
               (c >= QLatin1Char('0') && c <= QLatin1Char('9'));
    };
    if (!isAlnum(id.front())) {
        return false;
    }
    for (const QChar c : id) {
        if (!isAlnum(c) && c != QLatin1Char('-')) {
            return false;
        }
    }
    return true;
}

std::expected<ThemeDefinition, QString> ThemeCatalog::parseFile(const fs::path& path)
{
    std::error_code ec;
    const std::uintmax_t size = fs::file_size(path, ec);
    if (ec) {
        return std::unexpected(QStringLiteral("cannot stat file"));
    }
    if (size > kMaxFileBytes) {
        return std::unexpected(QStringLiteral("file exceeds 64 KiB"));
    }

    auto text = file::readText(path);
    if (text.isErr()) {
        return std::unexpected(
            QStringLiteral("cannot read file: %1").arg(QString::fromStdString(text.error().message)));
    }

    QJsonParseError parseError;
    const QJsonDocument document =
        QJsonDocument::fromJson(QByteArray::fromStdString(text.value()), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        return std::unexpected(QStringLiteral("invalid JSON: %1").arg(parseError.errorString()));
    }
    if (!document.isObject()) {
        return std::unexpected(QStringLiteral("root must be a JSON object"));
    }
    const QJsonObject root = document.object();

    const QJsonValue schemaValue = root.value(QStringLiteral("schemaVersion"));
    if (schemaValue.isUndefined()) {
        return std::unexpected(QStringLiteral("missing key 'schemaVersion'"));
    }
    if (!schemaValue.isDouble() || schemaValue.toDouble() != 1.0) {
        return std::unexpected(QStringLiteral("unsupported schemaVersion (only 1 is accepted)"));
    }

    const QJsonValue idValue = root.value(QStringLiteral("id"));
    if (idValue.isUndefined()) {
        return std::unexpected(QStringLiteral("missing key 'id'"));
    }
    if (!idValue.isString()) {
        return std::unexpected(QStringLiteral("key 'id' must be a string"));
    }
    const QString id = idValue.toString();
    if (id == QStringLiteral("custom")) {
        return std::unexpected(QStringLiteral("reserved id 'custom'"));
    }
    if (!isValidId(id)) {
        return std::unexpected(QStringLiteral("invalid id '%1'").arg(id));
    }

    const QJsonValue nameValue = root.value(QStringLiteral("displayName"));
    if (nameValue.isUndefined()) {
        return std::unexpected(QStringLiteral("missing key 'displayName'"));
    }
    if (!nameValue.isString()) {
        return std::unexpected(QStringLiteral("key 'displayName' must be a string"));
    }
    const QString displayName = nameValue.toString();
    if (displayName.trimmed().isEmpty()) {
        return std::unexpected(QStringLiteral("empty displayName"));
    }
    if (displayName.size() > 64) {
        return std::unexpected(QStringLiteral("displayName exceeds 64 characters"));
    }

    const QJsonValue descriptionValue = root.value(QStringLiteral("description"));
    if (descriptionValue.isUndefined()) {
        return std::unexpected(QStringLiteral("missing key 'description'"));
    }
    if (!descriptionValue.isString()) {
        return std::unexpected(QStringLiteral("key 'description' must be a string"));
    }

    const QJsonValue isDarkValue = root.value(QStringLiteral("isDark"));
    if (isDarkValue.isUndefined()) {
        return std::unexpected(QStringLiteral("missing key 'isDark'"));
    }
    if (!isDarkValue.isBool()) {
        return std::unexpected(QStringLiteral("key 'isDark' must be a bool"));
    }

    ThemeDefinition definition;
    definition.id = id;
    definition.displayName = displayName;
    definition.description = descriptionValue.toString();
    definition.isDark = isDarkValue.toBool();
    definition.author = QString();

    const QJsonValue authorValue = root.value(QStringLiteral("author"));
    if (!authorValue.isUndefined()) {
        if (!authorValue.isString()) {
            return std::unexpected(QStringLiteral("key 'author' must be a string"));
        }
        definition.author = authorValue.toString();
    }

    const QJsonValue colorsValue = root.value(QStringLiteral("colors"));
    if (colorsValue.isUndefined()) {
        return std::unexpected(QStringLiteral("missing key 'colors'"));
    }
    if (!colorsValue.isObject()) {
        return std::unexpected(QStringLiteral("key 'colors' must be an object"));
    }
    const QJsonObject colors = colorsValue.toObject();
    for (const QString& required : {QStringLiteral("accent"), QStringLiteral("background")}) {
        if (!colors.contains(required)) {
            return std::unexpected(QStringLiteral("missing key 'colors.%1'").arg(required));
        }
    }
    // Unknown tokens are preserved: Theme.qml ignores a token no binding
    // reads, and future tokens arrive this way (§11, forward-compat).
    for (auto it = colors.constBegin(); it != colors.constEnd(); ++it) {
        if (!it.value().isString()) {
            return std::unexpected(QStringLiteral("color '%1' must be a string").arg(it.key()));
        }
        auto parsed = parseHexColor(it.value().toString().toStdString());
        if (!parsed) {
            return std::unexpected(
                QStringLiteral("invalid color '%1': %2").arg(it.key(), parsed.error().message()));
        }
        definition.colors.insert(it.key(), QString::fromStdString(toHexString(*parsed)));
    }

    // Contrast is checked only for pairs the file states explicitly. A
    // minimal accent+background theme derives higher-contrast text in
    // Theme.qml and must still load.
    const auto accent = parseHexColor(definition.colors.value(QStringLiteral("accent")).toStdString());
    const auto background =
        parseHexColor(definition.colors.value(QStringLiteral("background")).toStdString());
    // Both were validated above; an unexpected failure is reported, not ignored.
    if (!accent || !background) {
        return std::unexpected(QStringLiteral("invalid accent or background color"));
    }
    if (definition.colors.contains(QStringLiteral("textPrimary"))) {
        const auto textPrimary = parseHexColor(definition.colors.value(QStringLiteral("textPrimary")).toStdString());
        if (auto check = checkContrast(QStringLiteral("textPrimary"), *textPrimary,
                                       QStringLiteral("background"), *background);
            !check) {
            return std::unexpected(check.error());
        }
    }
    if (definition.colors.contains(QStringLiteral("textOnAccent"))) {
        const auto textOnAccent =
            parseHexColor(definition.colors.value(QStringLiteral("textOnAccent")).toStdString());
        if (auto check = checkContrast(QStringLiteral("textOnAccent"), *textOnAccent,
                                       QStringLiteral("accent"), *accent);
            !check) {
            return std::unexpected(check.error());
        }
    }

    return definition;
}

ThemeCatalog::ThemeCatalog(const fs::path& themesDir) : dir_(themesDir)
{
    if (dir_.empty()) {
        dir_ = file::configDir() / "themes";
    }
    reload();
}

std::optional<ThemeDefinition> ThemeCatalog::find(const QString& id) const
{
    for (const auto& theme : themes_) {
        if (theme.id == id) {
            return theme;
        }
    }
    return std::nullopt;
}

void ThemeCatalog::reload()
{
    themes_.clear();
    errors_.clear();

    std::error_code ec;
    if (!fs::is_directory(dir_, ec)) {
        // A fresh install has no themes directory. Empty catalog, no error.
        return;
    }

    const auto files = file::listFiles(dir_, {".json"}, false);
    QSet<QString> seenIds;
    for (const auto& path : files) {
        auto parsed = parseFile(path);
        if (!parsed) {
            const QString reason = parsed.error();
            LOG_WARN("ThemeCatalog: rejecting {}: {}", fileNameOf(path).toStdString(), reason.toStdString());
            errors_ << QStringLiteral("%1: %2").arg(fileNameOf(path), reason);
            continue;
        }
        if (seenIds.contains(parsed->id)) {
            const QString reason = QStringLiteral("duplicate id '%1'").arg(parsed->id);
            LOG_WARN("ThemeCatalog: rejecting {}: {}", fileNameOf(path).toStdString(), reason.toStdString());
            errors_ << QStringLiteral("%1: %2").arg(fileNameOf(path), reason);
            continue;
        }
        seenIds.insert(parsed->id);
        themes_ << *parsed;
    }
}

} // namespace vc
