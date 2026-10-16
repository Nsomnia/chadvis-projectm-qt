/**
 * @file test_Config.cpp
 * @brief The Config access contract — reads never mark dirty, writes do, and
 *        mutation is main-thread affine (T0022).
 *
 * Before T0022 the const and mutable section accessors shared one name. Since
 * Config::instance() hands out a non-const reference, every plain read in the
 * tree — `CONFIG.visualizer().fps`, `const auto& v = CONFIG.visualizer()`,
 * every QML property getter — silently selected the MUTABLE overload and
 * marked the config dirty. isDirty() at quit was therefore almost always true,
 * which defeated the T0023 fix ("only rewrite the file when something
 * changed"). The fix gives the write accessors distinct names (*Mut), so:
 *   - a read compiles to the const overload by construction,
 *   - a write through the old spelling is a compile error, not a silent dirty.
 * These tests pin that contract. They also pin the affinity rule: config
 * mutation is main-thread affine and asserted in debug builds.
 */

#include <QDir>
#include <QTemporaryDir>
#include <QtTest>

#include "core/Config.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>

using namespace vc;

namespace {

namespace fs = std::filesystem;

/// Read a saved config file back as text.
std::string slurp(const fs::path& p) {
    std::ifstream in(p);
    std::ostringstream os;
    os << in.rdbuf();
    return os.str();
}

} // namespace

class TestConfig : public QObject {
    Q_OBJECT

private slots:
    void init() {
        // Config is a singleton; every suite that runs after this one must
        // find it exactly as it was left. Snapshot per test, restore per test.
        savedAudio_ = CONFIG.audio();
        savedVisualizer_ = CONFIG.visualizer();
        savedRecording_ = CONFIG.recording();
        savedUi_ = CONFIG.ui();
        savedKeyboard_ = CONFIG.keyboard();
        savedSuno_ = CONFIG.suno();
        savedKaraoke_ = CONFIG.karaoke();
        savedOverlay_ = CONFIG.overlay();
        savedDebug_ = CONFIG.debug();
        CONFIG.markClean();
        QVERIFY(!CONFIG.isDirty());
    }

    void cleanup() {
        // Restore through the write accessors (marks dirty), then leave the
        // singleton clean for whoever runs next.
        CONFIG.audioMut() = savedAudio_;
        CONFIG.visualizerMut() = savedVisualizer_;
        CONFIG.recordingMut() = savedRecording_;
        CONFIG.uiMut() = savedUi_;
        CONFIG.keyboardMut() = savedKeyboard_;
        CONFIG.sunoMut() = savedSuno_;
        CONFIG.karaokeMut() = savedKaraoke_;
        CONFIG.overlayMut() = savedOverlay_;
        CONFIG.setDebug(savedDebug_);
        CONFIG.markClean();
    }

    /// The T0022 heart: reading every section, debug flag and path must NOT
    /// set the dirty flag. With the old overload pair this test could not even
    /// be written — the reads themselves dirtied the config.
    void constReadsNeverMarkDirty() {
        // Touch every section through the const accessors, exactly the way the
        // production read sites spell it.
        const auto& audio = CONFIG.audio();
        const auto& viz = CONFIG.visualizer();
        const auto& rec = CONFIG.recording();
        const auto& ui = CONFIG.ui();
        const auto& keys = CONFIG.keyboard();
        const auto& suno = CONFIG.suno();
        const auto& karaoke = CONFIG.karaoke();
        const auto& overlay = CONFIG.overlay();
        (void)audio.sampleRate;
        (void)viz.fps;
        (void)rec.video.crf;
        (void)ui.theme;
        (void)keys.playPause;
        (void)suno.downloadPath;
        (void)karaoke.fontFamily;
        (void)overlay.enabled;
        (void)CONFIG.debug();
        (void)CONFIG.configPath();
        (void)CONFIG.isDirty();

        QVERIFY2(!CONFIG.isDirty(),
                 "a pure read marked the config dirty — the mutable "
                 "accessor leaked back into the read path");
    }

    /// Writes go through *Mut() and mark dirty; save() clears it only on
    /// success (T0023's mechanism, now actually reachable because reads no
    /// longer dirty the flag).
    void writeMarksDirtyAndSaveClearsIt() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const auto path = dir.filePath("config.toml").toStdString();

        CONFIG.visualizerMut().fps = 91;
        QVERIFY(CONFIG.isDirty());

        const auto result = CONFIG.save(path);
        QVERIFY2(result.isOk(), result.error().message.c_str());
        QVERIFY2(!CONFIG.isDirty(),
                 "a successful save must clear the dirty flag (T0023)");
        // And the write actually landed.
        QVERIFY2(slurp(path).find("fps = 91") != std::string::npos,
                 "saved file does not carry the written value");
    }

    /// End-to-end persistence: write, save, reload, observe.
    void writeSaveReloadRoundTrip() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const auto path = dir.filePath("config.toml").toStdString();

        CONFIG.karaokeMut().yPosition = 0.123f;
        QVERIFY(CONFIG.save(path).isOk());

        CONFIG.karaokeMut().yPosition = 0.987f; // clobber
        QVERIFY(CONFIG.load(path).isOk());
        QCOMPARE(CONFIG.karaoke().yPosition, 0.123f);
        QVERIFY(!CONFIG.isDirty()); // load() ends clean
    }

    /// A failed save must leave the dirty flag SET, so a later save retries.
    void failedSaveKeepsDirtyFlag() {
        // A path under a nonexistent directory cannot be created.
        const auto path = (QDir::tempPath() + "/no-such-dir-42/config.toml")
                                  .toStdString();

        CONFIG.uiMut().theme = "chadvis-dark";
        QVERIFY(CONFIG.isDirty());
        const auto result = CONFIG.save(path);
        QVERIFY(!result.isOk());
        QVERIFY2(CONFIG.isDirty(),
                 "a failed save must not clear the dirty flag");
        CONFIG.markClean();
    }

    /// The affinity contract: the owning thread sees true, any other thread
    /// sees false. (The mutating paths assert this in debug builds; the
    /// predicate is what the assert calls, so pinning it here pins them.)
    void affinityPredicateTracksOwningThread() {
        QVERIFY(CONFIG.onConfigThread());

        bool offThread = true;
        std::thread t([&offThread, &cfg = CONFIG] {
            offThread = cfg.onConfigThread();
        });
        t.join();
        QVERIFY2(!offThread,
                 "a worker thread reported itself as the config's owner");
    }

    /// setDebug is a write: it must mark dirty (and the restore path in
    /// cleanup() relies on that spelling compiling).
    void setDebugMarksDirty() {
        const bool original = CONFIG.debug();
        CONFIG.setDebug(!original);
        QVERIFY(CONFIG.isDirty());
        QCOMPARE(CONFIG.debug(), !original);
    }

private:
    AudioConfig savedAudio_;
    VisualizerConfig savedVisualizer_;
    RecordingConfig savedRecording_;
    UIConfig savedUi_;
    KeyboardConfig savedKeyboard_;
    SunoConfig savedSuno_;
    KaraokeConfig savedKaraoke_;
    OverlayConfig savedOverlay_;
    bool savedDebug_{false};
};

int runTestConfig(int argc, char** argv) {
    TestConfig tc;
    return QTest::qExec(&tc, argc, argv);
}

#include "test_Config.moc"
