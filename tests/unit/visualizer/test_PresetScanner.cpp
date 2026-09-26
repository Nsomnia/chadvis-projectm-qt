#include <QtTest>
#include <QEventLoop>
#include <QMetaObject>
#include <QObject>
#include <QTimer>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#include "util/FileUtils.hpp"
#include "visualizer/PresetData.hpp"
#include "visualizer/PresetManager.hpp"
#include "visualizer/PresetScanner.hpp"

using namespace vc;
namespace fs = std::filesystem;

namespace {

/// Filename shapes the author regex still has to handle now that it is built
/// once per scan instead of once per file, paired with the author each must
/// yield. A hoist that dropped a flag, changed the pattern, or mishandled a
/// capture group would show up here as a wrong author.
struct NameShape {
    const char* stem;
    const char* author;
};

constexpr NameShape kNameShapes[] = {
    {"plain", ""},                 // no '-' at all: the hoist's early-out
    {"2026", ""},                  // digits only
    {"_leading_underscore", ""},   // underscores only
    {"dots.in.name", ""},          // internal dots only
    {"ünïcödé", ""},                   // non-ASCII, still no '-'
    {"dash-", ""},                 // '-' present but nothing can follow it
    {"Artist - Title", "Artist"},  // the canonical "author - title"
    {"spaced   -   name", "spaced"},// runs of spaces on both sides
    {"a - b - c", "a"},            // non-greedy: the FIRST separator wins
    {"weird_chars_01.v2-final", "weird_chars_01.v2"},
    {"MixedCase_99-ok", "MixedCase_99"},
    {"ünïcödé-prësèt", "ünïcödé"},
};

/// Waits for the scan worker to finish every scan it was handed, then pumps this
/// thread's event loop until the queued hand-off has been applied. Bounded, so a
/// broken hand-off fails the test instead of hanging it. Returns false on
/// timeout — the test never asserts on how long a scan takes.
bool drainScan(PresetManager& manager, QObject& publishContext,
               int timeoutMs = 10000) {
    manager.waitForScan();

    QEventLoop loop;
    QTimer::singleShot(timeoutMs, &loop, [&loop] { loop.exit(0); });
    // The worker posts its completion before waitForScan() returns, so this
    // queued call sits behind it in the FIFO and the completion lands first.
    QMetaObject::invokeMethod(&publishContext, [&loop] { loop.exit(1); },
                              Qt::QueuedConnection);
    return loop.exec() == 1;
}

} // namespace

class TestPresetScanner : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        testDir = fs::current_path() / "test_presets";
        fs::create_directories(testDir);
        fs::create_directories(testDir / "CategoryA");

        // Separate root, so the filename-shape coverage below cannot change the
        // number of presets testScan() counts in testDir.
        namesDir = fs::current_path() / "test_preset_names";
        fs::remove_all(namesDir);
        fs::create_directories(namesDir);
        for (const auto& shape : kNameShapes)
            createFile(namesDir / (std::string(shape.stem) + ".milk"));
        createFile(namesDir / "not_a_preset.txt");

        // Create dummy milk files
        createFile(testDir / "preset1.milk");
        createFile(testDir / "CategoryA/preset2.milk");
        createFile(testDir / "not_a_preset.txt");
    }

    void cleanupTestCase() {
        fs::remove_all(testDir);
        fs::remove_all(namesDir);
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

    void testNameShapesSurviveHoistedAuthorRegex() {
        std::vector<PresetInfo> presets;
        std::set<std::string> favorites;
        std::set<std::string> blacklist;

        auto result = PresetScanner::scan(
                namesDir, true, presets, favorites, blacklist);

        QVERIFY(result.isOk());
        // Every .milk, and not the .txt.
        QCOMPARE(presets.size(), std::size(kNameShapes));

        for (const auto& shape : kNameShapes) {
            const auto found = std::find_if(
                    presets.begin(), presets.end(), [&](const PresetInfo& p) {
                        return p.name == shape.stem;
                    });
            QVERIFY2(found != presets.end(),
                     qPrintable(QStringLiteral("missing preset '%1'")
                                        .arg(QString::fromUtf8(shape.stem))));
            QCOMPARE(QString::fromStdString(found->author),
                     QString::fromUtf8(shape.author));
            // Author parsing must not disturb the rest of the parse.
            QCOMPARE(QString::fromStdString(found->category),
                     QStringLiteral("Uncategorized"));
            QCOMPARE(QString::fromStdString(found->path.stem().string()),
                     QString::fromUtf8(shape.stem));
        }
    }

    void testAuthorRegexIsSharedAcrossRepeatedScans() {
        // Same shapes, scanned repeatedly through one manager: the hoisted
        // regex is a single shared object, so a scan that ran after another one
        // must still parse identically (no state carried between calls).
        PresetManager manager;

        for (int pass = 0; pass < 3; ++pass) {
            QVERIFY(manager.scan(namesDir, true).isOk());
            const PresetManager::Snapshot generation = manager.allPresets();
            QCOMPARE(generation->size(), std::size(kNameShapes));
            for (const auto& shape : kNameShapes) {
                const auto found = std::find_if(
                        generation->begin(), generation->end(),
                        [&](const PresetInfo& p) {
                            return p.name == shape.stem;
                        });
                QVERIFY(found != generation->end());
                QCOMPARE(QString::fromStdString(found->author),
                         QString::fromUtf8(shape.author));
            }
        }
    }

    void testAsyncRescanKeepsPinnedGenerationsValid() {
        QObject publishContext;
        PresetManager manager;
        manager.setPublishContext(&publishContext);

        // Baseline generation, published synchronously.
        QVERIFY(manager.scan(namesDir, true).isOk());

        const PresetManager::Snapshot pinnedList = manager.allPresets();
        const PresetManager::PresetView pinnedView = manager.activePresets();
        QVERIFY(!pinnedList->empty());
        QCOMPARE(pinnedView.generation, pinnedList);
        QCOMPARE(pinnedView.items.size(), pinnedList->size());

        // Borrowed pointers taken before any rescan happens.
        const PresetInfo* const pinnedEntry = &pinnedList->front();
        const PresetInfo* const pinnedFromView = pinnedView.items.front();
        QCOMPARE(pinnedEntry, pinnedFromView);
        const std::string firstName = pinnedEntry->name;
        const std::size_t countBefore = pinnedList->size();

        // A new file makes the rescan's result differ from what is pinned.
        const std::string addedName = "zz_added_after_pinning";
        createFile(namesDir / (addedName + ".milk"));

        int listChangedCount = 0;
        manager.listChanged.connect([&listChangedCount] { ++listChangedCount; });

        QVERIFY(manager.rescanAsync());
        // Every further request while that one is in flight is coalesced into a
        // single follow-up instead of queueing another full directory walk.
        // Deterministic: no event loop has run, so the in-flight flags the
        // publishing hand-off clears are still set.
        for (int i = 0; i < 7; ++i)
            QVERIFY(!manager.rescanAsync());
        QVERIFY(manager.scanInFlight());

        // Mid-rescan the published generation is still whole and still first
        // in sort order. The old code cleared the vector up front, which is what
        // left QML reading freed memory.
        QCOMPARE(manager.allPresets()->size(), countBefore);
        QCOMPARE(manager.allPresets()->front().name, firstName);
        QCOMPARE(manager.count(), countBefore);
        QVERIFY(!manager.empty());

        QVERIFY(drainScan(manager, publishContext));
        QVERIFY(!manager.scanInFlight());

        // A brand-new generation was published rather than the old one mutated:
        // the old storage is still pinned, so the allocator cannot have handed
        // its memory back out.
        const PresetManager::Snapshot republished = manager.allPresets();
        QVERIFY(republished != pinnedList);
        QCOMPARE(republished->size(), countBefore + 1);
        QVERIFY(&republished->front() != pinnedEntry);

        // The invariant: everything handed out before the rescan is still valid
        // and still reads back correctly.
        QCOMPARE(pinnedEntry, &pinnedList->front());
        QCOMPARE(pinnedEntry, pinnedFromView);
        QCOMPARE(pinnedEntry->name, firstName);
        QCOMPARE(pinnedList->size(), countBefore);
        QCOMPARE(pinnedView.items.size(), countBefore);
        QCOMPARE(pinnedView.items.front()->name, firstName);
        QCOMPARE(manager.count(), countBefore + 1);

        // The new preset exists only in the new generation.
        const auto added = std::find_if(
                republished->begin(), republished->end(),
                [&](const PresetInfo& p) { return p.name == addedName; });
        QVERIFY(added != republished->end());
        QVERIFY(std::none_of(pinnedList->begin(), pinnedList->end(),
                             [&](const PresetInfo& p) {
                                 return p.name == addedName;
                             }));

        // Publications are bounded by the number of requests, and at least one
        // landed. The stronger claim this test used to make -- "eight requests
        // produce at most two notifications" -- was never a guarantee, it was a
        // bet that the 13-file walk would take longer than seven sub-microsecond
        // calls. Coalescing bounds *concurrency*: one walk runs at a time and at
        // most one request is queued behind it. If the worker finishes a walk
        // before the caller issues the next request, that next request
        // legitimately starts a fresh walk, so the total number of walks over N
        // requests can be as high as N. What actually stops a held-down rescan
        // button from multiplying the QVariantList rebuilds is the run of
        // rescanAsync() calls above all returning false, which is asserted there
        // and is deterministic because no event loop has run.
        QVERIFY(listChangedCount >= 1);
        QVERIFY2(listChangedCount <= 8,
                 qPrintable(QStringLiteral("more publications (%1) than requests (8); "
                                           "a single request must not publish twice")
                                    .arg(listChangedCount)));

        // current() stays a borrowed pointer, and it points into the generation
        // that is published now.
        QVERIFY(manager.selectByName(firstName));
        const PresetInfo* const borrowed = manager.current();
        QVERIFY(borrowed != nullptr);
        QCOMPARE(borrowed->name, firstName);
        QVERIFY(borrowed >= republished->data());
        QVERIFY(borrowed < republished->data() + republished->size());
    }

    void testAsyncRescanWithoutPublishContextFallsBackToSync() {
        PresetManager manager;
        // No publish context: scanAsync must still populate the manager rather
        // than silently doing nothing. Asserted by content, not by an absolute
        // count, so it does not depend on what earlier slots added to namesDir.
        QVERIFY(manager.scanAsync(namesDir, true));
        QVERIFY(!manager.empty());
        QVERIFY(!manager.scanInFlight());

        const PresetManager::Snapshot generation = manager.allPresets();
        QCOMPARE(manager.count(), generation->size());
        for (const auto& shape : kNameShapes) {
            QVERIFY(std::any_of(generation->begin(), generation->end(),
                                [&](const PresetInfo& p) {
                                    return p.name == shape.stem;
                                }));
        }
    }

    void testFailedAsyncScanKeepsPublishedGeneration() {
        QObject publishContext;
        PresetManager manager;
        manager.setPublishContext(&publishContext);

        QVERIFY(manager.scan(namesDir, true).isOk());
        const PresetManager::Snapshot pinned = manager.allPresets();
        const std::size_t countBefore = pinned->size();

        std::string reportedError;
        manager.scanFailed.connect(
                [&reportedError](const std::string& message) {
                    reportedError = message;
                });

        // A directory that does not exist fails on the worker.
        QVERIFY(manager.scanAsync(testDir / "no_such_preset_dir", true));
        QVERIFY(drainScan(manager, publishContext));

        QVERIFY2(!reportedError.empty(), "scanFailed was not reported");
        // The working library survives, and its storage was not touched.
        QCOMPARE(manager.count(), countBefore);
        QCOMPARE(manager.allPresets(), pinned);
        QCOMPARE(pinned->size(), countBefore);
    }

private:
    fs::path testDir;
    fs::path namesDir;

    void createFile(const fs::path& path) {
        std::ofstream f(path);
        f << "dummy content";
    }
};

#include "test_PresetScanner.moc"

int runTestPresetScanner(int argc, char** argv) {
    TestPresetScanner tc;
    return QTest::qExec(&tc, argc, argv);
}
