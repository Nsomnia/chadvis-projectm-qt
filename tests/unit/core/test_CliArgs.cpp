#include <QtTest>

#include "core/Application.hpp"

#include <initializer_list>
#include <string>
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

#include "test_CliArgs.moc"

int runTestCliArgs(int argc, char** argv) {
    TestCliArgs tc;
    return QTest::qExec(&tc, argc, argv);
}
