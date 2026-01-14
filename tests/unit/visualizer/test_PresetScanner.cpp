#include <QtTest>
#include <filesystem>
#include <fstream>
#include "util/FileUtils.hpp"
#include "visualizer/PresetData.hpp"
#include "visualizer/PresetScanner.hpp"

using namespace vc;
namespace fs = std::filesystem;

class TestPresetScanner : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        testDir = fs::current_path() / "test_presets";
        fs::create_directories(testDir);
        fs::create_directories(testDir / "CategoryA");

        // Create dummy milk files
        createFile(testDir / "preset1.milk");
        createFile(testDir / "CategoryA/preset2.milk");
        createFile(testDir / "not_a_preset.txt");
    }

    void cleanupTestCase() {
        fs::remove_all(testDir);
    }

    void testScan() {
        std::vector<PresetInfo> presets;
        std::set<std::string> favorites = {"preset1"};
        std::set<std::string> blacklist = {"preset2"};

        auto result = PresetScanner::scan(
                testDir, true, presets, favorites, blacklist);

        QVERIFY(result.isOk());
        QCOMPARE(presets.size(), 2u);

        bool found1 = false;
        bool found2 = false;

        for (const auto& p : presets) {
            if (p.name == "preset1") {
                found1 = true;
                QVERIFY(p.favorite);
                QVERIFY(!p.blacklisted);
            } else if (p.name == "preset2") {
                found2 = true;
                QVERIFY(!p.favorite);
                QVERIFY(p.blacklisted);
                QCOMPARE(p.category, std::string("CategoryA"));
            }
        }

        QVERIFY(found1);
        QVERIFY(found2);
    }

private:
    fs::path testDir;

    void createFile(const fs::path& path) {
        std::ofstream f(path);
        f << "dummy content";
    }
};
