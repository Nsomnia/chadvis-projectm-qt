// test_PresetPersistence.cpp -- favourites/blacklist state is written
// atomically (T0059).
//
// The file is rewritten on every favourite toggle (PresetManager.cpp), not
// once per session, so the old truncate-and-write left a window on a hot path
// in which a crash erased both sets. The writer now goes through the shared
// temp+rename helper, and these two cases pin the observable half of that:
// the content round-trips, and a save leaves neither a stale `.tmp` nor a
// second file behind.

#include <QTemporaryDir>
#include <QtTest>

#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

#include "visualizer/PresetPersistence.hpp"

namespace fs = std::filesystem;
using namespace vc;

class TestPresetPersistence : public QObject {
    Q_OBJECT

private slots:
    void saveThenLoadRoundTrips();
    void aSaveLeavesNoTempFileBehind();
};

void TestPresetPersistence::saveThenLoadRoundTrips() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const fs::path path = fs::path(dir.path().toStdString()) / "preset_state.txt";

    // A stale file the save must replace rather than append to.
    std::ofstream(path) << "[favorites]\nstale\n";

    const std::set<std::string> favorites{"alpha", "gamma"};
    const std::set<std::string> blacklist{"beta"};
    const auto saved = PresetPersistence::saveState(path, favorites, blacklist);
    QVERIFY2(saved.isOk(), saved.error().message.c_str());

    std::set<std::string> loadedFavorites;
    std::set<std::string> loadedBlacklist;
    std::vector<PresetInfo> presets(2);
    presets[0].name = "alpha";
    presets[1].name = "beta";
    const auto loaded =
            PresetPersistence::loadState(path, loadedFavorites, loadedBlacklist, presets);
    QVERIFY2(loaded.isOk(), loaded.error().message.c_str());

    QVERIFY(loadedFavorites == favorites);
    QVERIFY(loadedBlacklist == blacklist);
    QVERIFY(presets[0].favorite);
    QVERIFY(!presets[0].blacklisted);
    QVERIFY(!presets[1].favorite);
    QVERIFY(presets[1].blacklisted);
}

void TestPresetPersistence::aSaveLeavesNoTempFileBehind() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const fs::path path = fs::path(dir.path().toStdString()) / "preset_state.txt";

    QVERIFY(PresetPersistence::saveState(path, {"a"}, {}).isOk());
    QVERIFY(PresetPersistence::saveState(path, {"a", "b"}, {"c"}).isOk());

    QVERIFY2(!fs::exists(path.string() + ".tmp"),
             "the atomic writer's temp file survived the rename");
    QVERIFY(fs::is_regular_file(path));
}

#include "test_PresetPersistence.moc"

int runTestPresetPersistence(int argc, char** argv) {
    TestPresetPersistence tc;
    return QTest::qExec(&tc, argc, argv);
}
