/**
 * @file test_Version.cpp
 * @brief Regression test for the `--version` banner *and* the QML-visible
 *        version property.
 *
 * The banner used to carry a hardcoded "1.0.0" literal while version.txt had
 * moved on, so `--version` reported a version the binary never was. The same
 * defect then survived on the QML side: main.qml and NavRail.qml each spelled
 * their own version out in a string literal ("v2.0.0", "v2.0") while the real
 * version moved independently. These assertions pin every surface to
 * version.txt, read at test time, so a reintroduced literal fails here instead
 * of shipping.
 *
 * Three layers, deliberately:
 *   1. vc::Cli::versionBanner() — the exact bytes printVersion() streams.
 *   2. The real binary with `--version` — immune to any refactor of
 *      Application::printVersion() that stops using the accessor.
 *   3. SettingsBridge::version() — the property QML binds to, plus a scan of
 *      the QML tree proving no file spells a version out as a string literal.
 */

#include <QtTest>
#include <QProcess>
#include <QString>
#include "core/CliUtils.hpp"
#include "qml_bridge/SettingsBridge.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

namespace {

// The stale literal this test exists to keep out of the banner.
constexpr std::string_view kStaleVersion = "1.0.0";

std::string_view trim(std::string_view text) {
    const auto isSpace = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
    while (!text.empty() && isSpace(text.front())) {
        text.remove_prefix(1);
    }
    while (!text.empty() && isSpace(text.back())) {
        text.remove_suffix(1);
    }
    return text;
}

/// Drop ANSI SGR sequences so assertions compare plain text.
std::string stripAnsi(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size();) {
        if (text[i] == '\033' && i + 1 < text.size() && text[i + 1] == '[') {
            i += 2;
            while (i < text.size() && text[i] != 'm') {
                ++i;
            }
            if (i < text.size()) {
                ++i; // consume the terminating 'm'
            }
            continue;
        }
        out += text[i++];
    }
    return out;
}

/// Value printed on the "Version: " line of a banner, if present.
std::optional<std::string> versionLine(std::string_view banner) {
    const std::string text = stripAnsi(banner);
    for (std::size_t start = 0; start <= text.size();) {
        const std::size_t end = text.find('\n', start);
        const std::string_view line{text.data() + start,
                                    (end == std::string::npos ? text.size() : end) - start};
        if (const auto colon = line.find(':'); colon != std::string_view::npos &&
            trim(line.substr(0, colon)) == "Version") {
            return std::string(trim(line.substr(colon + 1)));
        }
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
    return std::nullopt;
}

/// Locate the repository root: the path handed down by CMake, else walk up
/// from the current directory (ctest runs the binary in build/tests).
std::optional<fs::path> repoRoot() {
#ifdef CHADVIS_SOURCE_DIR
    const fs::path configured{CHADVIS_SOURCE_DIR};
    if (fs::exists(configured / "version.txt")) {
        return configured;
    }
#endif
    fs::path dir = fs::current_path();
    for (int depth = 0; depth < 6; ++depth) {
        if (fs::exists(dir / "version.txt")) {
            return dir;
        }
        const fs::path parent = dir.parent_path();
        if (parent == dir) {
            break;
        }
        dir = parent;
    }
    return std::nullopt;
}

std::optional<std::string> versionTxtContents() {
    const auto root = repoRoot();
    if (!root) {
        return std::nullopt;
    }
    std::ifstream file(*root / "version.txt");
    if (!file) {
        return std::nullopt;
    }
    std::string contents;
    std::getline(file, contents);
    return std::string(trim(contents));
}

std::optional<std::string> readFile(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return std::nullopt;
    }
    return std::string{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

/// True when a QML string literal *begins* with a version token: an optional
/// "v", then digits, a dot, then more digits ("v2.0", "2.0.0").
bool startsWithVersion(std::string_view text) {
    const auto digitsFrom = [text](std::size_t from) {
        std::size_t i = from;
        while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i]))) {
            ++i;
        }
        return i;
    };
    const std::size_t majorFrom = text.starts_with('v') || text.starts_with('V') ? 1 : 0;
    const std::size_t majorEnd = digitsFrom(majorFrom);
    if (majorEnd == majorFrom || majorEnd >= text.size() || text[majorEnd] != '.') {
        return false;
    }
    return digitsFrom(majorEnd + 1) > majorEnd + 1;
}

/// Every double-quoted QML string literal under `qmlRoot` that reads as a
/// version, reported as "src/qml/<rel>:<line>: <literal>".
///
/// Only quoted spans are examined, so the `@version 1.0.0` doc tag that nearly
/// every QML file carries is correctly ignored: that is a per-component
/// revision marker, not the application version, and it is allowed to drift.
std::vector<std::string> qmlVersionLiterals(const fs::path& qmlRoot) {
    std::vector<fs::path> files;
    for (const auto& entry : fs::recursive_directory_iterator(qmlRoot)) {
        if (entry.is_regular_file() && entry.path().extension() == ".qml") {
            files.push_back(entry.path());
        }
    }
    // Sorted so a failure lists offenders deterministically.
    std::sort(files.begin(), files.end());

    std::vector<std::string> found;
    for (const auto& file : files) {
        const auto text = readFile(file);
        if (!text) {
            continue;
        }
        const std::string rel = fs::relative(file, qmlRoot).generic_string();
        std::size_t line = 1;
        // Quoted spans are skipped wholesale, so newlines are counted from the
        // last scanned offset rather than only at the scan head; otherwise a
        // multi-line literal silently desynchronises every later line number.
        std::size_t scanned = 0;
        const auto advanceLines = [&](std::size_t to) {
            for (std::size_t k = scanned; k < to; ++k) {
                if ((*text)[k] == '\n') {
                    ++line;
                }
            }
            scanned = to;
        };

        for (std::size_t i = 0; i < text->size();) {
            advanceLines(i);
            if ((*text)[i] != '"') {
                ++i;
                continue;
            }
            const std::size_t close = text->find('"', i + 1);
            if (close == std::string::npos) {
                break; // unterminated literal; nothing further to parse
            }
            const std::string_view literal{text->data() + i + 1, close - i - 1};
            if (startsWithVersion(literal)) {
                found.push_back(rel + ":" + std::to_string(line) + ": \"" +
                                std::string(literal) + "\"");
            }
            i = close + 1;
        }
    }
    return found;
}

std::string joinLines(const std::vector<std::string>& lines) {
    std::string joined;
    for (const auto& line : lines) {
        joined += "  " + line + "\n";
    }
    return joined;
}

#ifdef CHADVIS_BINARY
/// The real executable, when the test lane knows where to find it.
std::optional<fs::path> binaryPath() {
    const fs::path path{CHADVIS_BINARY};
    if (fs::exists(path)) {
        return path;
    }
    return std::nullopt;
}
#endif

} // namespace

class TestVersion : public QObject {
    Q_OBJECT

private slots:
    void versionIsInjectedByTheBuild() {
        // "unknown" is the no-compile-definition fallback: if it shows up here,
        // the build stopped forwarding PROJECT_VERSION to project_lib.
        QVERIFY2(vc::Cli::version() != "unknown",
                 "CHADVIS_VERSION was not defined for project_lib; the banner "
                 "would report \"unknown\"");
        QVERIFY2(!vc::Cli::version().empty(), "version() must never be empty");
    }

    void bannerReportsVersionTxt() {
        const auto expected = versionTxtContents();
        QVERIFY2(expected.has_value(),
                 "could not locate version.txt (set CHADVIS_SOURCE_DIR for the "
                 "unit_tests target)");

        const auto reported = versionLine(vc::Cli::versionBanner());
        QVERIFY2(reported.has_value(),
                 "the --version banner no longer emits a \"Version:\" line");
        QCOMPARE(*reported, *expected);
        QCOMPARE(std::string(vc::Cli::version()), *expected);
    }

    void bannerHasNoStaleLiteral() {
        const auto expected = versionTxtContents();
        QVERIFY(expected.has_value());
        if (*expected == kStaleVersion) {
            QSKIP("version.txt is 1.0.0; there is no stale literal left to catch");
        }
        const std::string banner = stripAnsi(vc::Cli::versionBanner());
        QVERIFY2(banner.find(kStaleVersion) == std::string::npos,
                 "the --version banner contains a hardcoded 1.0.0; it must read "
                 "CHADVIS_VERSION from version.txt");
    }

    void binaryReportsVersionTxt() {
#ifdef CHADVIS_BINARY
        const auto binary = binaryPath();
        if (!binary) {
            QSKIP("chadvis-projectm-qt binary not built at the configured path");
        }

        QProcess process;
        process.start(QString::fromStdString(binary->string()),
                      {QStringLiteral("--version")});
        QVERIFY2(process.waitForStarted(5000), "failed to launch the executable");
        QVERIFY2(process.waitForFinished(30000), "--version did not terminate");
        QCOMPARE(process.exitStatus(), QProcess::NormalExit);
        QCOMPARE(process.exitCode(), 0);

        const std::string output =
            stripAnsi(QString::fromUtf8(process.readAllStandardOutput()).toStdString());
        const auto reported = versionLine(output);
        QVERIFY2(reported.has_value(),
                 "the executable's --version output has no \"Version:\" line");

        const auto expected = versionTxtContents();
        QVERIFY(expected.has_value());
        QCOMPARE(*reported, *expected);
        QVERIFY2(output.find(kStaleVersion) == std::string::npos,
                 "chadvis-projectm-qt --version reports a hardcoded 1.0.0");
#else
        QSKIP("CHADVIS_BINARY not defined for the unit_tests target");
#endif
    }

    /// The property QML actually binds to must report version.txt, and it must
    /// be reached the way the engine reaches it.
    void qmlBridgeReportsVersionTxt() {
        const auto expected = versionTxtContents();
        QVERIFY2(expected.has_value(),
                 "could not locate version.txt (set CHADVIS_SOURCE_DIR for the "
                 "unit_tests target)");

        // Through the registered-singleton factory rather than the private
        // constructor: this is the exact object QML is handed, so a future
        // change that makes the property context-dependent fails here too.
        // Held as QObject because ~SettingsBridge is private; the destructor is
        // virtual, so releasing through the base is well-defined.
        std::unique_ptr<QObject> singleton{
            qml_bridge::SettingsBridge::create(nullptr, nullptr)};
        QVERIFY(singleton != nullptr);

        // Read through the meta-object, by the name QML binds to: that asserts
        // the Q_PROPERTY itself exists, not merely the C++ getter.
        const QMetaObject* meta = singleton->metaObject();
        const int index = meta->indexOfProperty("version");
        QVERIFY2(index >= 0,
                 "SettingsBridge has no \"version\" Q_PROPERTY; QML cannot read it");
        QCOMPARE(QString::fromLatin1(meta->property(index).name()),
                 QStringLiteral("version"));
        QVERIFY2(meta->property(index).isConstant(),
                 "SettingsBridge.version must be CONSTANT; it is a build-time fact "
                 "and cannot change at runtime");

        const QString reported = singleton->property("version").toString();
        QVERIFY2(!reported.isEmpty(),
                 "SettingsBridge.version is empty; QML would render a bare \"v\"");
        QCOMPARE(reported, QString::fromStdString(*expected));
    }

    /// No QML file may spell the application version out as a string literal.
    /// version.txt is the single source of truth; a literal here is how the
    /// banner regressed in the first place.
    void qmlTreeHasNoHardcodedVersionLiteral() {
        const auto root = repoRoot();
        QVERIFY2(root.has_value(),
                 "could not locate the repository root (set CHADVIS_SOURCE_DIR "
                 "for the unit_tests target)");
        const fs::path qmlRoot = *root / "src" / "qml";
        QVERIFY2(fs::is_directory(qmlRoot), "src/qml is missing from the source tree");

        const auto offenders = qmlVersionLiterals(qmlRoot);
        const std::string report =
            "QML string literals below begin with a version and must bind to "
            "SettingsBridge.version instead:\n" + joinLines(offenders);
        QVERIFY2(offenders.empty(), report.c_str());
    }

    /// Counterweight to the scan above: the two known surfaces must still
    /// *display* a version, read from the bridge. Otherwise "no literal" is
    /// satisfiable by deleting the string and leaving an empty status bar.
    void versionSurfacesBindToTheBridge() {
        const auto root = repoRoot();
        QVERIFY(root.has_value());

        for (const char* relative : {"src/qml/main.qml", "src/qml/components/NavRail.qml"}) {
            const auto text = readFile(*root / relative);
            QVERIFY2(text.has_value(),
                     qPrintable(QStringLiteral("could not read %1").arg(QLatin1String(relative))));
            QVERIFY2(text->find("SettingsBridge.version") != std::string::npos,
                     qPrintable(QStringLiteral("%1 does not read SettingsBridge.version; the "
                                               "version display must bind to the bridge")
                                    .arg(QLatin1String(relative))));
        }
    }
};

int runTestVersion(int argc, char** argv) {
    TestVersion tv;
    return QTest::qExec(&tv, argc, argv);
}

#include "test_Version.moc"
