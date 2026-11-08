#pragma once
/**
 * @file ThemeCatalog.hpp
 * @brief Discovers, validates and exposes user theme files.
 *
 * A theme is a definition: the named token set in THEME-ENGINE-PLAN.md §4.2
 * and src/qml/styles/ThemeDefinitions.qml. Theme.qml stays the sole QML
 * token owner; QML cannot enumerate a directory or read a file, so this
 * catalog owns discovery and validation and Theme.qml never parses files
 * itself (ThemeDefinitions.qml header, T0190).
 *
 * Source: <configDir>/themes/*.json, where configDir is vc::file::configDir()
 * (docs/dev/THEME-ENGINE-PLAN.md §6.1). File names are arbitrary; the id
 * inside the file is the identity. One rejected file never aborts discovery.
 */

#include "util/FileUtils.hpp"

#include <QMap>
#include <QList>
#include <QString>
#include <QStringList>

#include <expected>
#include <optional>

namespace vc {

/// One validated user theme. Mirrors the QML builtin shape one-to-one:
/// ThemeDefinitions.qml stores id as the map key and the definition as
/// {displayName, description, isDark, author, colors}. A JSON file carries
/// id inside the object (THEME-ENGINE-PLAN.md §4.2); colors are #RRGGBBAA
/// strings, never QColor, so every value passes through Theme.qml's
/// parseRgbaHex() on the QML side (T0047 byte-order rule, §3.3).
struct ThemeDefinition {
    QString id;
    QString displayName;
    QString description;
    bool isDark{true};
    QString author;
    /// Normalized uppercase "#RRGGBBAA" tokens. Always holds accent and
    /// background; every other token is optional because Theme.qml derives
    /// omitted tokens (ramp()/darker() rules).
    QMap<QString, QString> colors;
};

/// Discovery + validation for <configDir>/themes/*.json.
///
/// Degradation: a missing themes directory yields an empty catalog with no
/// errors; each file is validated independently and a failure appends
/// "<filename>: <reason>" to errors() without removing the valid themes.
class ThemeCatalog {
public:
    /// File-size ceiling: a theme is a palette, not a document (§11).
    static constexpr std::size_t kMaxFileBytes = 64 * 1024;
    /// WCAG AA minimum for text on its background (§11, check-theme-contrast.py).
    static constexpr double kMinContrastRatio = 4.5;

    /// @param themesDir Directory to scan. Empty means the default
    /// <configDir>/themes. Tests pass a temporary directory. The catalog
    /// scans immediately; call reload() to rescan.
    explicit ThemeCatalog(const fs::path& themesDir = {});

    /// Strict id pattern `^[a-z0-9][a-z0-9-]*$` plus the reserved id
    /// "custom" (§6.1, §7.3): custom is synthetic, never a file.
    [[nodiscard]] static bool isValidId(const QString& id);

    /// Validate one file without adding it to the catalog. Shared by
    /// reload() so a single-file check and discovery cannot drift.
    [[nodiscard]] static std::expected<ThemeDefinition, QString>
    parseFile(const fs::path& path);

    [[nodiscard]] QList<ThemeDefinition> themes() const { return themes_; }
    [[nodiscard]] QStringList errors() const { return errors_; }
    [[nodiscard]] std::optional<ThemeDefinition> find(const QString& id) const;
    [[nodiscard]] fs::path dir() const { return dir_; }

    /// Rescan dir_ from disk, replacing themes_ and errors_.
    void reload();

private:
    fs::path dir_;
    QList<ThemeDefinition> themes_;
    QStringList errors_;
};

} // namespace vc
