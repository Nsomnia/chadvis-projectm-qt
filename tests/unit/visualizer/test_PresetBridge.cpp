#include <QTemporaryDir>
#include <QtTest>

#include <filesystem>
#include <fstream>
#include <memory>

#include "core/Config.hpp"
#include "qml_bridge/PresetBridge.hpp"
#include "visualizer/PresetData.hpp"
#include "visualizer/PresetManager.hpp"
#include "visualizer/RatingManager.hpp"

using namespace vc;

namespace fs = std::filesystem;

namespace {

/// One preset file per name. The scanner accepts a bare .milk as a preset
/// (the scanner suite's fixture does the same); projectM only parses at load.
void createPreset(const fs::path& dir, const char* stem) {
    std::ofstream out(dir / (std::string(stem) + ".milk"));
    out << "dummy content";
}

} // namespace

// Row identity for the QML preset panel. Every index-based slot on
// PresetBridge (selectByIndex, toggleFavorite, toggleBlacklisted, setRating)
// addresses the FULL manager generation, while the panel renders
// filteredPresets() — a filtered subset whose visible positions diverge from
// full-list positions whenever a search or category filter is active. The
// variant maps must therefore carry full-list indices, or a click on row N
// acts on row 0. This suite pins that contract from the bridge side; the QML
// side has no test harness.
class TestPresetBridge : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void unfilteredRowsCarryFullListIndices();
    void filteredRowsCarryFullListIndicesNotVisiblePositions();
    void selectingThroughAFilteredRowSelectsThatRow();
    void favoritingThroughAFilteredRowTogglesThatRow();
    void ratingThroughAFilteredRowWritesThatRow();
    void refusingANonDirectoryKeepsTheCurrentLibrary();
    void choosingAFolderRecordsItAsThePresetPath();

private:
    QTemporaryDir dir_;
    fs::path presetPathBefore_;
    std::unique_ptr<PresetManager> manager_;
    std::unique_ptr<qml_bridge::PresetBridge> bridge_;
};

void TestPresetBridge::init() {
    presetPathBefore_ = Config::instance().visualizer().presetPath;
    const fs::path root = dir_.path().toStdString();
    createPreset(root, "alpha");
    createPreset(root, "bravo");
    createPreset(root, "charlie");
    createPreset(root, "delta");

    manager_ = std::make_unique<PresetManager>();
    QVERIFY2(manager_->scan(root, true).isOk(), "preset scan failed");
    QCOMPARE(static_cast<int>(manager_->count()), 4);

    // PresetBridge's manager pointer is process-global and one binary runs
    // every suite, so each test installs its own and cleanup() removes it.
    qml_bridge::PresetBridge::setPresetManager(manager_.get());
    bridge_ = std::make_unique<qml_bridge::PresetBridge>();
}

void TestPresetBridge::cleanup() {
    // The bridge subscribes to the manager's vc::Signals with lambdas
    // capturing `this`, and vc::Signal has no disconnect — so the bridge must
    // die first, with nothing emitting in between, and the static manager
    // pointer must be cleared for whichever suite runs next.
    bridge_.reset();
    qml_bridge::PresetBridge::setPresetManager(nullptr);
    manager_.reset();
    Config::instance().visualizerMut().presetPath = presetPathBefore_;
}

void TestPresetBridge::unfilteredRowsCarryFullListIndices() {
    // No query, no category: filteredPresets() is the active view, which is
    // the full list in order, so row position and full-list index coincide.
    const QVariantList rows = bridge_->filteredPresets();
    QCOMPARE(rows.size(), qsizetype(4));
    for (qsizetype row = 0; row < rows.size(); ++row) {
        const QVariantMap preset = rows.at(row).toMap();
        QVERIFY2(preset.contains(QStringLiteral("index")), "preset variant map has no index key");
        QCOMPARE(preset.value(QStringLiteral("index")).toInt(), static_cast<int>(row));
    }
}

void TestPresetBridge::filteredRowsCarryFullListIndicesNotVisiblePositions() {
    // Search narrows the list to one row. Its index key must be the preset's
    // position in the generation — compared against ground truth read from
    // the manager, so the assertion holds under any directory iteration
    // order — and not its visible position (0).
    bridge_->setSearchQuery(QStringLiteral("delta"));
    const QVariantList rows = bridge_->filteredPresets();
    QCOMPARE(rows.size(), qsizetype(1));
    const QVariantMap row = rows.first().toMap();
    QVERIFY(row.contains(QStringLiteral("index")));

    const PresetManager::Snapshot generation = manager_->allPresets();
    int truth = -1;
    for (int i = 0; i < static_cast<int>(generation->size()); ++i) {
        if ((*generation)[i].name == "delta") {
            truth = i;
            break;
        }
    }
    QVERIFY(truth >= 0);
    QCOMPARE(row.value(QStringLiteral("index")).toInt(), truth);
}

void TestPresetBridge::selectingThroughAFilteredRowSelectsThatRow() {
    // The regression this suite exists for: with the index key missing,
    // QML's modelData.index was undefined, coerced to 0, and every visible
    // row selected preset #0. The victim is taken from index 1 — never
    // slot 0 — so the old coercion cannot pass by directory-ordering luck.
    const PresetManager::Snapshot generation = manager_->allPresets();
    const std::string victim = (*generation)[1].name;
    bridge_->setSearchQuery(QString::fromStdString(victim));

    const QVariantList rows = bridge_->filteredPresets();
    QCOMPARE(rows.size(), qsizetype(1));
    const int index = rows.first().toMap().value(QStringLiteral("index")).toInt();

    QVERIFY(bridge_->selectByIndex(index));
    const QVariantMap current = bridge_->currentPreset();
    QCOMPARE(current.value(QStringLiteral("name")).toString(), QString::fromStdString(victim));
    QCOMPARE(current.value(QStringLiteral("index")).toInt(), 1);
}

void TestPresetBridge::favoritingThroughAFilteredRowTogglesThatRow() {
    const PresetManager::Snapshot generation = manager_->allPresets();
    const std::string victim = (*generation)[2].name;
    bridge_->setSearchQuery(QString::fromStdString(victim));

    const QVariantList rows = bridge_->filteredPresets();
    QCOMPARE(rows.size(), qsizetype(1));
    bridge_->toggleFavorite(rows.first().toMap().value(QStringLiteral("index")).toInt());

    // The toggle publishes a copy-on-write generation, so the pinned snapshot
    // keeps the OLD storage and the OLD values — through it the victim is
    // still unstarred…
    QVERIFY(!(*generation)[2].favorite);
    QVERIFY(!(*generation)[0].favorite);
    // …while the live generation the panel re-reads carries the star on the
    // victim — and only the victim, so the row the click meant is the row
    // that changed.
    const PresetManager::Snapshot live = manager_->allPresets();
    QVERIFY((*live)[2].favorite);
    QVERIFY(!(*live)[0].favorite);
    QCOMPARE(QString::fromStdString((*live)[2].name), QString::fromStdString(victim));
}

void TestPresetBridge::ratingThroughAFilteredRowWritesThatRow() {
    // The QML half of the rating path is the star Repeater, where modelData
    // is the star number 0-4 and the click must reach the bridge carrying the
    // ROW's index — which is what PresetDelegate.presetIndex exists to carry.
    // This is the bridge half: setRating(index, stars) must write onto the
    // preset that index addresses, never slot 0. RatingManager is a
    // process-wide in-memory singleton, so slot 0 is compared against its own
    // before-value rather than an assumed zero.
    const PresetManager::Snapshot generation = manager_->allPresets();
    const std::string victim = (*generation)[3].name;
    const std::string slotZero = (*generation)[0].name;
    const int slotZeroBefore = RatingManager::instance().getRating(slotZero);
    bridge_->setSearchQuery(QString::fromStdString(victim));

    const QVariantList rows = bridge_->filteredPresets();
    QCOMPARE(rows.size(), qsizetype(1));
    bridge_->setRating(rows.first().toMap().value(QStringLiteral("index")).toInt(), 4);

    QCOMPARE(RatingManager::instance().getRating(victim), 4);
    QCOMPARE(RatingManager::instance().getRating(slotZero), slotZeroBefore);
}

void TestPresetBridge::refusingANonDirectoryKeepsTheCurrentLibrary() {
    // The only production caller is a FolderDialog, which cannot produce a
    // non-directory — but the guard is what stops a programmatic or stale
    // value from persisting a path that scans to nothing, i.e. from emptying
    // a 9,795-preset library with one typo.
    const QString before = bridge_->folder();
    const std::size_t countBefore = manager_->count();

    bridge_->setFolder(QStringLiteral("/nonexistent/not-a-preset-folder"));

    QCOMPARE(bridge_->folder(), before);
    QCOMPARE(manager_->count(), countBefore);
}

void TestPresetBridge::choosingAFolderRecordsItAsThePresetPath() {
    // The folder the Settings row shows is the persisted config field, not a
    // bridge-side copy: one owner is what makes a relaunch reopen on the same
    // library.
    const fs::path chosen = dir_.path().toStdString() + "/chosen";
    fs::create_directories(chosen);
    createPreset(chosen, "echo");

    bridge_->setFolder(QString::fromStdString(chosen.string()));

    QCOMPARE(bridge_->folder(), QString::fromStdString(chosen.string()));
    QCOMPARE(QString::fromStdString(Config::instance().visualizer().presetPath.string()),
             QString::fromStdString(chosen.string()));

    // The rescan is asynchronous: the walk runs on the manager's worker and the
    // finished list is published back through a queued event, so the count only
    // changes once this thread has processed events. QTRY spins that loop (and
    // still fails on a hang), which is also what keeps cleanup() from destroying
    // the manager while its worker is mid-walk.
    QTRY_COMPARE_WITH_TIMEOUT(static_cast<int>(manager_->count()), 1, 5000);
}

#include "test_PresetBridge.moc"

int runTestPresetBridge(int argc, char** argv) {
    TestPresetBridge tc;
    return QTest::qExec(&tc, argc, argv);
}
