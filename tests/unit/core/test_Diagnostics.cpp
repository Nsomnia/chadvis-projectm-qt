#include <QtGlobal>
#include <QtTest>
#include <QTemporaryDir>
#include <QDir>
#include <QStandardPaths>

#include "core/Diagnostics.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

using namespace vc;

namespace {

/// The allowlist, transcribed here deliberately: a second copy is the point.
/// If someone adds a key to collectDiagnosticsReport() without a review, this
/// test fails instead of silently widening what leaves the machine (T0130).
constexpr std::array<std::string_view, 7> kAllowlist{
    "application_version", "qt_version",   "os",
    "cpu_architecture",    "compiler",     "feature_avfilter",
    "feature_avaes"};

std::string slurp(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in),
                       std::istreambuf_iterator<char>());
}

} // namespace

class TestDiagnostics : public QObject {
    Q_OBJECT

private slots:
    void reportContainsExactlyTheAllowlistedKeys() {
        const auto report = collectDiagnosticsReport();
        QVERIFY(report.size() == kAllowlist.size());

        for (std::size_t i = 0; i < kAllowlist.size(); ++i) {
            QVERIFY2(std::string_view(report[i].first) == kAllowlist[i],
                     ("unexpected or reordered key: " + report[i].first).c_str());
            QVERIFY2(!report[i].second.empty(),
                     ("empty value for " + report[i].first).c_str());
        }
        // No duplicate keys.
        for (std::size_t i = 0; i < report.size(); ++i) {
            for (std::size_t j = i + 1; j < report.size(); ++j) {
                QVERIFY(report[i].first != report[j].first);
            }
        }
    }

    void reportCarriesNoSecretOrPathMaterial() {
        // The contract is allowlist-only, but pin the *shape* of the values too.
        // An env canary proves the environment is not read; the home/config
        // paths prove no user path is swept in. Compiler strings legitimately
        // carry punctuation, so we assert absence of secrets, not of slashes.
        qputenv("CHADVIS_DIAGNOSTICS_CANARY", "CANARY_S3CR3T_9f2b");
        const std::string home = QDir::homePath().toStdString();
        const std::string configDir =
            QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)
                .toStdString();
        const auto report = collectDiagnosticsReport();
        for (const auto& [key, value] : report) {
            QVERIFY2(value.find("CANARY_S3CR3T_9f2b") == std::string::npos,
                     ("environment leaked into " + key).c_str());
            QVERIFY2(value.find("Bearer ") == std::string::npos, key.c_str());
            QVERIFY2(value.find("cookie") == std::string::npos, key.c_str());
            QVERIFY2(value.find("session") == std::string::npos, key.c_str());
            QVERIFY2(value.find("://") == std::string::npos, key.c_str());
            QVERIFY2(value.find(home) == std::string::npos,
                     ("home path leaked into " + key).c_str());
            if (!configDir.empty()) {
                QVERIFY2(value.find(configDir) == std::string::npos,
                         ("config path leaked into " + key).c_str());
            }
        }
        qunsetenv("CHADVIS_DIAGNOSTICS_CANARY");
    }

    void renderingIsDeterministicAndComplete() {
        const auto report = collectDiagnosticsReport();
        const std::string first = renderDiagnosticsReport(report);
        const std::string second = renderDiagnosticsReport(report);
        QCOMPARE(first, second);
        QVERIFY(first.starts_with("ChadVis diagnostics\n"));
        for (const auto& key : kAllowlist) {
            QVERIFY2(first.find(std::string(key) + ": ") != std::string::npos,
                     std::string(key).c_str());
        }
        // One line per entry, plus the header.
        QCOMPARE(std::count(first.begin(), first.end(), '\n'),
                 static_cast<std::ptrdiff_t>(kAllowlist.size() + 1));
    }

    void writeCreatesTheRenderedReportAndOmitsTheDestination() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const fs::path dest = fs::path(dir.path().toStdString()) /
                              "chadvis-diagnostics.txt";
        const auto report = collectDiagnosticsReport();

        QVERIFY(writeDiagnosticsReport(dest, report).isOk());
        const std::string onDisk = slurp(dest);
        QCOMPARE(onDisk, renderDiagnosticsReport(report));
        // The user-chosen path itself must never be part of the report.
        QVERIFY2(onDisk.find(dest.string()) == std::string::npos,
                 "the destination path leaked into the report");
    }

    void writeRefusesBadDestinationsInsteadOfSwallowingThem() {
        const auto report = collectDiagnosticsReport();

        // Empty path.
        QVERIFY(!writeDiagnosticsReport({}, report).isOk());

        // A parent directory that does not exist: refuse, leave no file.
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const fs::path missing = fs::path(dir.path().toStdString()) /
                                 "no-such-dir" / "report.txt";
        const auto refused = writeDiagnosticsReport(missing, report);
        QVERIFY(!refused.isOk());
        QVERIFY2(!refused.error().message.empty(), "the failure had no message");
        QVERIFY(!fs::exists(missing));
    }
};

#include "test_Diagnostics.moc"

int runTestDiagnostics(int argc, char** argv) {
    TestDiagnostics tc;
    return QTest::qExec(&tc, argc, argv);
}
