#include <toml++/toml.h>
#include <QtTest>
#include "core/ConfigParsers.hpp"

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

    void testSerialize() {
        AudioConfig audio;
        audio.device = "test_device";
        VisualizerConfig visualizer;
        RecordingConfig recording;
        UIConfig ui;
        KeyboardConfig keyboard;
        SunoConfig suno;
        std::vector<OverlayElementConfig> overlays;

        auto tbl = ConfigParsers::serialize(audio,
                                            visualizer,
                                            recording,
                                            ui,
                                            keyboard,
                                            suno,
                                            overlays,
                                            false);

        auto audioTbl = tbl["audio"].as_table();
        QVERIFY(audioTbl != nullptr);
        QCOMPARE((*audioTbl)["device"].as_string()->get(),
                 std::string("test_device"));
    }
};

int runTestConfigParsers(int argc, char** argv) {
    TestConfigParsers tc;
    return QTest::qExec(&tc, argc, argv);
}

#include "test_ConfigParsers.moc"
