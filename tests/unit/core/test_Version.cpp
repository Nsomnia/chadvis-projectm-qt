/**
 * @file test_Version.cpp
 * @brief Regression test for the `--version` banner.
 *
 * The banner used to carry a hardcoded "1.0.0" literal while version.txt had
 * moved on, so `--version` reported a version the binary never was. These
 * assertions pin the emitted text to version.txt, read at test time, so a
 * reintroduced literal fails here instead of shipping.
 *
 * Two layers, deliberately:
 *   1. vc::Cli::versionBanner() — the exact bytes printVersion() streams.
 *   2. The real binary with `--version` — immune to any refactor of
 *      Application::printVersion() that stops using the accessor.
 */

#include <QtTest>
#include <QProcess>
#include "core/CliUtils.hpp"

#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>

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
};

int runTestVersion(int argc, char** argv) {
    TestVersion tv;
    return QTest::qExec(&tv, argc, argv);
}

#include "test_Version.moc"
