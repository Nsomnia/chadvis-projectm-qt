#include <QtTest>
#include <QDir>
#include <toml++/toml.h>
#include "core/ConfigParsers.hpp"
#include "util/FileUtils.hpp"

using namespace vc;

class TestConfigParsers : public QObject {
    Q_OBJECT

private slots:
    void testParseAudio() {
        auto tbl = toml::parse(R"(
            [audio]
            device = "default"
            sample_rate = 44100
            buffer_size = 1024
        )");

        AudioConfig cfg;
        ConfigParsers::parseAudio(tbl, cfg);

        QCOMPARE(cfg.device, std::string("default"));
        QCOMPARE(cfg.sampleRate, 44100u);
        QCOMPARE(cfg.bufferSize, 1024u);
    }

    void testParseVisualizer() {
        auto tbl = toml::parse(R"(
            [visualizer]
            preset_path = "/path/to/presets"
            preset_duration = 15
            shuffle = true
        )");

        VisualizerConfig cfg;
        ConfigParsers::parseVisualizer(tbl, cfg);

        QCOMPARE(cfg.presetPath.string(), std::string("/path/to/presets"));
        QCOMPARE(cfg.presetDuration, 15u);
        QCOMPARE(cfg.shufflePresets, true);
    }

    void testClampVisualizerBoundsRawOverrides() {
        // The CLI override lane (Application::init) writes raw flag values
        // into CONFIG past this parser, then re-runs these helpers. Pin the
        // exact bounds so the two lanes can never drift: --visualizer-fps 0
        // used to skip VisualizerRenderer's `fps > 0` guard entirely.
        VisualizerConfig cfg;
        cfg.fps = 0;
        cfg.width = 100;
        cfg.height = 9999;
        ConfigParsers::clampVisualizer(cfg);
        QCOMPARE(cfg.fps, 10u);
        QCOMPARE(cfg.width, 160u);
        QCOMPARE(cfg.height, 4320u);

        // Idempotent: the parse path already clamped these values once, and
        // Application::init runs the helper over them a second time.
        ConfigParsers::clampVisualizer(cfg);
        QCOMPARE(cfg.fps, 10u);
        QCOMPARE(cfg.width, 160u);
        QCOMPARE(cfg.height, 4320u);

        // The other direction a raw flag can overshoot.
        cfg.fps = 1000;
        cfg.width = 8000;
        cfg.height = 5000;
        ConfigParsers::clampVisualizer(cfg);
        QCOMPARE(cfg.fps, 240u);
        QCOMPARE(cfg.width, 7680u);
        QCOMPARE(cfg.height, 4320u);
    }

    void testClampVideoEncoderBoundsRawOverrides() {
        VideoEncoderConfig cfg;
        cfg.crf = 99; // --recording-crf past clamp(crf, 0, 51)
        ConfigParsers::clampVideoEncoder(cfg);
        QCOMPARE(cfg.crf, 51u);

        // Width rounds UP to even after clamping, exactly as parseRecording
        // always has.
        cfg.width = 161;
        ConfigParsers::clampVideoEncoder(cfg);
        QCOMPARE(cfg.width, 162u);

        // Idempotent on the values the parse path already produced.
        ConfigParsers::clampVideoEncoder(cfg);
        QCOMPARE(cfg.crf, 51u);
        QCOMPARE(cfg.width, 162u);

        // gop_size 0 means "auto" and must survive; an absurd value is
        // pulled back to auto. b_frames caps at 16.
        cfg.gopSize = 0;
        cfg.bFrames = 32;
        ConfigParsers::clampVideoEncoder(cfg);
        QCOMPARE(cfg.gopSize, 0u);
        QCOMPARE(cfg.bFrames, 16u);
        cfg.gopSize = 601;
        ConfigParsers::clampVideoEncoder(cfg);
        QCOMPARE(cfg.gopSize, 0u);
    }

    void testSerialize() {
        AudioConfig audio;
        audio.device = "test_device";
        VisualizerConfig visualizer;
        RecordingConfig recording;
        UIConfig ui;
        KeyboardConfig keyboard;
        SunoConfig suno;
        KaraokeConfig karaoke;
        OverlayConfig overlays;

        auto tbl = ConfigParsers::serialize(audio, visualizer, recording, ui, keyboard, suno,
                                            karaoke, overlays, false);

        auto audioTbl = tbl["audio"].as_table();
        QVERIFY(audioTbl != nullptr);
        QCOMPARE((*audioTbl)["device"].as_string()->get(), std::string("test_device"));
    }

    // T0070: the shipped template is portable, and a first save used to pin it
    // to one machine. Every path under $HOME is written `~/...`, which
    // expandPath reverses on the next parse.
    void serializeWritesPathsUnderHomeRelativeToHome() {
        const fs::path home(QDir::homePath().toStdString());
        AudioConfig audio;
        VisualizerConfig visualizer;
        RecordingConfig recording;
        UIConfig ui;
        KeyboardConfig keyboard;
        SunoConfig suno;
        KaraokeConfig karaoke;
        OverlayConfig overlays;

        recording.outputDirectory = home / "Music" / "renders";
        suno.downloadPath = home / "Music" / "suno";
        suno.debugLyricsFile = fs::path("/tmp/lyrics.srt");
        visualizer.texturePaths = {home / "textures", fs::path("/opt/textures")};

        const auto tbl = ConfigParsers::serialize(audio, visualizer, recording, ui, keyboard, suno,
                                                  karaoke, overlays, false);

        const auto* rec = tbl["recording"].as_table();
        QVERIFY(rec != nullptr);
        QCOMPARE((*rec)["output_directory"].as_string()->get(), std::string("~/Music/renders"));

        const auto* sunoTbl = tbl["suno"].as_table();
        QVERIFY(sunoTbl != nullptr);
        QCOMPARE((*sunoTbl)["download_path"].as_string()->get(), std::string("~/Music/suno"));
        // A path outside $HOME has no portable spelling and must not be mangled.
        QCOMPARE((*sunoTbl)["debug_lyrics_file"].as_string()->get(),
                 std::string("/tmp/lyrics.srt"));

        const auto* arr = (*tbl["visualizer"].as_table())["texture_paths"].as_array();
        QVERIFY(arr != nullptr);
        QCOMPARE(arr->size(), std::size_t(2));
        QCOMPARE((*arr)[0].value<std::string>(), std::string("~/textures"));
        QCOMPARE((*arr)[1].value<std::string>(), std::string("/opt/textures"));
    }

    // The auto-detected presets directory is a per-machine answer, so writing
    // it back would convert '' ("detect") into a pinned absolute path.
    void serializeLeavesAnAutoDetectedPresetPathEmpty() {
        AudioConfig audio;
        VisualizerConfig visualizer;
        RecordingConfig recording;
        UIConfig ui;
        KeyboardConfig keyboard;
        SunoConfig suno;
        KaraokeConfig karaoke;
        OverlayConfig overlays;
        const fs::path home(QDir::homePath().toStdString());

        visualizer.presetPath = file::presetsDir();
        auto tbl = ConfigParsers::serialize(audio, visualizer, recording, ui, keyboard, suno,
                                            karaoke, overlays, false);
        QCOMPARE((*tbl["visualizer"].as_table())["preset_path"].value<std::string>(),
                 std::string());

        // A user-chosen path is kept, and comes back expanded.
        visualizer.presetPath = home / "my-presets";
        tbl = ConfigParsers::serialize(audio, visualizer, recording, ui, keyboard, suno, karaoke,
                                       overlays, false);
        QCOMPARE((*tbl["visualizer"].as_table())["preset_path"].value<std::string>(),
                 std::string("~/my-presets"));

        VisualizerConfig reparsed;
        ConfigParsers::parseVisualizer(tbl, reparsed);
        QCOMPARE(reparsed.presetPath.string(), (home / "my-presets").string());
    }
};

int runTestConfigParsers(int argc, char** argv) {
    TestConfigParsers tc;
    return QTest::qExec(&tc, argc, argv);
}

#include "test_ConfigParsers.moc"
