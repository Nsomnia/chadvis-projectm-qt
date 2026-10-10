#include <QTemporaryDir>
#include <QtTest>

#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>

#include "visualizer/PresetData.hpp"
#include "visualizer/PresetManager.hpp"

using namespace vc;

namespace fs = std::filesystem;

namespace {

/// One preset file per name. The scanner accepts a bare .milk as a preset
/// (the scanner and bridge suites' fixtures do the same); projectM only
/// parses at load.
void createPreset(const fs::path& dir, const char* stem) {
    std::ofstream out(dir / (std::string(stem) + ".milk"));
    out << "dummy content";
}

/// Sentinel for "no row of this generation carries that name" — a broken
/// fixture, which every caller asserts is not the case before using the index.
constexpr usize kNotFound = std::numeric_limits<usize>::max();

usize indexOf(const PresetManager::Snapshot& generation, const char* name) {
    for (usize i = 0; i < generation->size(); ++i) {
        if ((*generation)[i].name == name) return i;
    }
    return kNotFound;
}

} // namespace

// publishGeneration documents "Swap, never clear-and-refill: a Snapshot
// pinned by any reader keeps the old storage alive and unmodified"
// (PresetManager.cpp). setFavorite/setBlacklisted used to write the toggle
// straight into that shared storage, so the guarantee held for rescans and
// NOT for toggles — every pinned Snapshot observed the edit. This suite pins
// the copy-on-write repair: a toggle must publish a NEW generation and leave
// pinned storage at the values it was handed, while the favourite/blacklist
// semantics callers actually rely on (name-set queries, active count,
// selection identity, one listChanged per call, rescan persistence) stay
// exactly what they were before the repair.
class TestPresetManager : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void favoriteTogglePublishesANewGenerationAndLeavesPinnedReadsAlone();
    void blacklistTogglePublishesANewGenerationAndLeavesPinnedReadsAlone();
    void favoriteSemanticsAreUnchangedThroughTheCopy();
    void outOfRangeTogglesAreNoOps();
    void toggledFavoriteSurvivesARescan();

private:
    QTemporaryDir dir_;
    fs::path root_;
    std::unique_ptr<PresetManager> manager_;
    Signal<>::SlotId listChangedSlot_{0};
    int listChangedCount_{0};
};

void TestPresetManager::init() {
    root_ = dir_.path().toStdString();
    createPreset(root_, "alpha");
    createPreset(root_, "bravo");
    createPreset(root_, "charlie");

    manager_ = std::make_unique<PresetManager>();
    QVERIFY2(manager_->scan(root_, true).isOk(), "preset scan failed");
    QCOMPARE(static_cast<int>(manager_->count()), 3);

    listChangedCount_ = 0;
    // The SlotId is retained and disconnected in cleanup(): vc::Signal has no
    // lifetime link to its subscriber, and Signal.hpp requires a captured
    // `this` to be disconnected before the subscriber goes away.
    listChangedSlot_ = manager_->listChanged.connect([this] { ++listChangedCount_; });
}

void TestPresetManager::cleanup() {
    if (manager_) {
        manager_->listChanged.disconnect(listChangedSlot_);
    }
    manager_.reset();
}

void TestPresetManager::favoriteTogglePublishesANewGenerationAndLeavesPinnedReadsAlone() {
    const PresetManager::Snapshot pinned = manager_->allPresets();
    const usize victim = indexOf(pinned, "bravo");
    QVERIFY2(victim != kNotFound, "fixture preset missing from the generation");
    QVERIFY(!(*pinned)[victim].favorite);
    // The QML read shape: PresetView is what PresetBridge::filteredPresets
    // hands the panel, and its borrowed pointers must keep the old values too.
    const PresetManager::PresetView pinnedView = manager_->activePresets();

    manager_->toggleFavorite(victim);

    // Pinned storage keeps the values it was handed.
    QVERIFY(!(*pinned)[victim].favorite);
    bool sawVictim = false;
    for (const auto* p : pinnedView.items) {
        if (p->name == std::string("bravo")) {
            sawVictim = true;
            QVERIFY(!p->favorite);
        }
    }
    QVERIFY2(sawVictim, "victim missing from the pinned active view");

    // The toggle is visible only through a fresh generation: a different
    // publication carrying the star on the victim and nowhere else.
    const PresetManager::Snapshot live = manager_->allPresets();
    QVERIFY(live.get() != pinned.get());
    const usize liveVictim = indexOf(live, "bravo");
    QVERIFY2(liveVictim != kNotFound, "victim missing from the live generation");
    QVERIFY((*live)[liveVictim].favorite);
    const usize liveBystander = indexOf(live, "alpha");
    QVERIFY2(liveBystander != kNotFound, "bystander missing from the live generation");
    QVERIFY(!(*live)[liveBystander].favorite);

    // One toggle, one listChanged — same notification contract as before.
    QCOMPARE(listChangedCount_, 1);
}

void TestPresetManager::blacklistTogglePublishesANewGenerationAndLeavesPinnedReadsAlone() {
    const PresetManager::Snapshot pinned = manager_->allPresets();
    const usize victim = indexOf(pinned, "charlie");
    QVERIFY2(victim != kNotFound, "fixture preset missing from the generation");
    const int activeBefore = static_cast<int>(manager_->activeCount());

    manager_->toggleBlacklisted(victim);

    QVERIFY(!(*pinned)[victim].blacklisted);
    const PresetManager::Snapshot live = manager_->allPresets();
    QVERIFY(live.get() != pinned.get());
    const usize liveVictim = indexOf(live, "charlie");
    QVERIFY2(liveVictim != kNotFound, "victim missing from the live generation");
    QVERIFY((*live)[liveVictim].blacklisted);
    // Caller-visible semantics: the row leaves the active set immediately.
    QCOMPARE(static_cast<int>(manager_->activeCount()), activeBefore - 1);
    QCOMPARE(listChangedCount_, 1);
}

void TestPresetManager::favoriteSemanticsAreUnchangedThroughTheCopy() {
    const PresetManager::Snapshot generation = manager_->allPresets();
    const usize victim = indexOf(generation, "alpha");
    QVERIFY2(victim != kNotFound, "fixture preset missing from the generation");
    const usize selected = indexOf(generation, "bravo");
    QVERIFY2(selected != kNotFound, "fixture preset missing from the generation");
    QVERIFY(manager_->selectByIndex(selected));

    manager_->setFavorite(victim, true);

    // Exactly one listChanged per setter call, as before the copy-on-write.
    QCOMPARE(listChangedCount_, 1);
    // The name sets still drive the query views: the victim is a favourite…
    const PresetManager::PresetView favorites = manager_->favoritePresets();
    QCOMPARE(static_cast<int>(favorites.size()), 1);
    QCOMPARE(QString::fromStdString(favorites.items.front()->name), QStringLiteral("alpha"));
    // …and selection identity survives the generation swap.
    QCOMPARE(static_cast<int>(manager_->currentIndex()), static_cast<int>(selected));
    QCOMPARE(QString::fromStdString(manager_->current()->name), QStringLiteral("bravo"));

    manager_->setFavorite(victim, false);

    QCOMPARE(listChangedCount_, 2);
    QCOMPARE(static_cast<int>(manager_->favoritePresets().size()), 0);
}

void TestPresetManager::outOfRangeTogglesAreNoOps() {
    const PresetManager::Snapshot pinned = manager_->allPresets();

    manager_->setFavorite(999, true);
    manager_->setBlacklisted(999, true);
    manager_->toggleFavorite(999);
    manager_->toggleBlacklisted(999);

    // No publication, no notification: an out-of-range toggle must not publish
    // a lookalike generation or fire listChanged.
    QCOMPARE(listChangedCount_, 0);
    QVERIFY(manager_->allPresets().get() == pinned.get());
}

void TestPresetManager::toggledFavoriteSurvivesARescan() {
    const PresetManager::Snapshot generation = manager_->allPresets();
    const usize victim = indexOf(generation, "alpha");
    QVERIFY2(victim != kNotFound, "fixture preset missing from the generation");
    manager_->setFavorite(victim, true);

    // The favourite name sets are what carries a star across a re-walk; the
    // republished generation must still carry it.
    QVERIFY2(manager_->scan(root_, true).isOk(), "preset rescan failed");

    const PresetManager::Snapshot live = manager_->allPresets();
    const usize liveVictim = indexOf(live, "alpha");
    QVERIFY2(liveVictim != kNotFound, "victim missing from the live generation");
    QVERIFY((*live)[liveVictim].favorite);
}

#include "test_PresetManager.moc"

int runTestPresetManager(int argc, char** argv) {
    TestPresetManager tc;
    return QTest::qExec(&tc, argc, argv);
}
