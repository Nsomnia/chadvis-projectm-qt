#include <QtTest>

#include "core/Application.hpp"
#include "core/CliUtils.hpp"

#include <algorithm>
#include <initializer_list>
#include <set>
#include <string>
#include <string_view>
#include <vector>

using namespace vc;

// parseArgsFrom is the pure parsing seam: real argv in, Result out, no
// Application instance. This is the tree's first coverage of the CLI parser
// (T0100 listed CliArgs as untested). The combination pinned here is the one
// recording cannot honour: frame capture is wired to the visualizer window's
// GL context at init time, so --headless + --record must be refused at parse
// time rather than launching a batch session that silently never records.
class TestCliArgs : public QObject {
    Q_OBJECT

private slots:
    void headlessAloneParses();
    void recordAloneParses();
    void headlessPlusRecordIsRefusedInBothOrders();
    void shortRecordFormIsAlsoRefused();
    void absentDefaultPresetOverrideStaysUnset();
    // T0207: the CliArgs.inc table is the single source of truth for
    // --help, topics, and completion — pin its shape so a malformed
    // table edit fails here instead of silently emitting bad help.
    void cliArgTableCoversAllFlags();
    void cliArgTableHasNoDuplicateLongNames();
    void cliArgTableSectionsAreNonEmpty();
    // T0211: strict number parsing and empty-arg safety.
    void intPrefixIsRejected();
    void negativeIntIsRejected();
    void floatPrefixIsRejected();
    void emptyArgDoesNotCrash();
};

namespace {

Result<AppOptions> parse(const std::initializer_list<const char*>& args) {
    std::vector<const char*> argv{"chadvis-projectm-qt"};
    for (const char* arg : args)
        argv.push_back(arg);
    return Application::parseArgsFrom(static_cast<int>(argv.size()), argv.data());
}

} // namespace

void TestCliArgs::headlessAloneParses() {
    auto result = parse({"--headless"});
    QVERIFY(result.isOk());
    const AppOptions opts = std::move(*result);
    QVERIFY(opts.headless);
    QVERIFY(!opts.startRecording);
}

void TestCliArgs::recordAloneParses() {
    auto result = parse({"--record"});
    QVERIFY(result.isOk());
    const AppOptions opts = std::move(*result);
    QVERIFY(opts.startRecording);
    QVERIFY(!opts.headless);
}

void TestCliArgs::headlessPlusRecordIsRefusedInBothOrders() {
    QVERIFY(!parse({"--headless", "--record"}).isOk());
    QVERIFY(!parse({"--record", "--headless"}).isOk());
}

void TestCliArgs::shortRecordFormIsAlsoRefused() {
    QVERIFY(!parse({"-r", "--headless"}).isOk());
    QVERIFY(!parse({"--headless", "-r"}).isOk());
}

/// T0205: "flag absent" must be distinguishable from "flag false". A plain bool
/// could not express that, so every launch without --default-preset wrote
/// `false` into the live config, erasing a `true` the user had saved.
void TestCliArgs::absentDefaultPresetOverrideStaysUnset() {
    const auto absent = parse({"--headless"});
    QVERIFY(absent.isOk());
    QVERIFY(!absent.value().useDefaultPreset.has_value());

    const auto present = parse({"--default-preset"});
    QVERIFY(present.isOk());
    QVERIFY(present.value().useDefaultPreset.has_value());
    QVERIFY(*present.value().useDefaultPreset);
}

void TestCliArgs::cliArgTableCoversAllFlags() {
    const auto& table = Cli::cliArgTable();
    // 27 entries as of T0207 (--dump-config added by T0208); bump when adding a flag to CliArgs.inc.
    QCOMPARE(table.size(), std::size_t{27});

    auto has = [&](std::string_view name) {
        return std::any_of(table.begin(), table.end(),
                           [&](const CliArg& a) { return a.longName == name; });
    };
    // Flags the T0207 audit found missing from hand-written completion.
    QVERIFY(has("--visualizer-fps"));
    QVERIFY(has("--audio-rate"));
    QVERIFY(has("--recording-preset"));
    QVERIFY(has("--suno-download-path"));
    QVERIFY(has("--suno-auto-download"));
    QVERIFY(has("--karaoke-enabled"));
    QVERIFY(has("--karaoke-font"));
    QVERIFY(has("--theme"));
    // Negations live on the entry, not as separate entries.
    const auto shuffle =
        std::find_if(table.begin(), table.end(), [&](const CliArg& a) {
            return a.longName == "--visualizer-shuffle";
        });
    QVERIFY(shuffle != table.end());
    QCOMPARE(shuffle->negationName, std::string_view("--no-visualizer-shuffle"));
    QVERIFY(!has("--no-visualizer-shuffle"));
}

void TestCliArgs::cliArgTableHasNoDuplicateLongNames() {
    const auto& table = Cli::cliArgTable();
    std::set<std::string_view> seen;
    for (const auto& arg : table) {
        QVERIFY2(!seen.contains(arg.longName),
                 qPrintable(QStringLiteral("duplicate flag: %1").arg(arg.longName)));
        seen.insert(arg.longName);
    }
}

void TestCliArgs::intPrefixIsRejected() {
    // T0211: std::stoi parsed "12x" as 12; the strict parser rejects it.
    QVERIFY(!parse({"--visualizer-fps", "12x"}).isOk());
    QVERIFY(!parse({"--audio-rate", "44100hz"}).isOk());
    // ...but a clean integer still parses.
    auto ok = parse({"--visualizer-fps", "120"});
    QVERIFY(ok.isOk());
    QCOMPARE(ok.value().visualizerFps.value_or(0), 120);
}

void TestCliArgs::negativeIntIsRejected() {
    // T0211: -1 wrapped to 4294967295 in the u32 field before the clamp.
    // Every CLI_INT flag feeds an unsigned quantity; reject at parse.
    QVERIFY(!parse({"--visualizer-fps", "-1"}).isOk());
    QVERIFY(!parse({"--recording-crf", "-5"}).isOk());
}

void TestCliArgs::floatPrefixIsRejected() {
    // T0211: std::stof parsed "0.5x" as 0.5; the strict parser rejects it.
    QVERIFY(!parse({"--karaoke-y-position", "0.5x"}).isOk());
    auto ok = parse({"--karaoke-y-position", "0.75"});
    QVERIFY(ok.isOk());
    QVERIFY(qFuzzyCompare(ok.value().karaokeYPosition.value_or(0.0f), 0.75f));
}

void TestCliArgs::emptyArgDoesNotCrash() {
    // T0211: arg[0] on an empty string_view is UB. An empty arg is not a
    // flag and not a file; it must not crash (reported as unknown).
    QVERIFY(!parse({""}).isOk());
}

void TestCliArgs::cliArgTableSectionsAreNonEmpty() {
    const auto& table = Cli::cliArgTable();
    QVERIFY(!table.empty());
    for (const auto& arg : table) {
        QVERIFY2(!arg.section.empty(),
                 qPrintable(QStringLiteral("flag missing section: %1").arg(arg.longName)));
        QVERIFY(!arg.helpText.empty());
    }
}

#include "test_CliArgs.moc"

int runTestCliArgs(int argc, char** argv) {
    TestCliArgs tc;
    return QTest::qExec(&tc, argc, argv);
}
