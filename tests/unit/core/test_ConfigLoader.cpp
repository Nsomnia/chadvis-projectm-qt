/**
 * @file test_ConfigLoader.cpp
 * @brief Config round-trip, credential-safety and durability tests.
 *
 * ConfigLoader had zero tests, which is why a serializer that could write the
 * Clerk cookie to disk, a write-only `[overlay] enabled` key, an absent
 * `aspect_correction` template key and a Windows-broken save all shipped
 * unnoticed.
 *
 * @section UnknownKeys
 * The tested decision: serialize REBUILDS the document, so unknown keys and
 * unknown sections are DROPPED on save. That is deliberate -- preserving them
 * would mean reading and merging the previous file on every save -- and it is
 * pinned by unknownKeysAreDropped() rather than left untested. The corollary is
 * that the shipped template must declare every key the parser knows, which is
 * what shippedTemplateDeclaresEveryKey() enforces.
 */

#include <QTemporaryDir>
#include <QtTest>
#include <toml++/toml.h>

#include "core/Config.hpp"
#include "core/ConfigLoader.hpp"
#include "core/ConfigParsers.hpp"
#include "util/Color.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using namespace vc;

namespace {

namespace fs = std::filesystem;

/// Serialize the live config exactly as ConfigLoader::save does, so a test can
/// inspect the document without touching the filesystem.
toml::table serializeConfig(const Config& c) {
    return ConfigParsers::serialize(c.audio(), c.visualizer(), c.recording(), c.ui(), c.keyboard(),
                                    c.suno(), c.karaoke(), c.overlay(), c.debug());
}

/// Serialize a RecordingConfig on its own, with every other section default-
/// constructed. Keeps the [recording] assertions independent of the Config
/// singleton, so no test in this file has to mutate global state to reach them.
toml::table serializeRecording(const RecordingConfig& rec) {
    return ConfigParsers::serialize(AudioConfig{}, VisualizerConfig{}, rec, UIConfig{},
                                    KeyboardConfig{}, SunoConfig{}, KaraokeConfig{},
                                    OverlayConfig{}, false);
}

/// Render a document the way the save path does (`os << tbl`).
std::string render(const toml::table& tbl) {
    std::ostringstream os;
    os << tbl;
    return os.str();
}

void writeText(const fs::path& path, const std::string& text) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << text;
}

std::string readText(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    std::ostringstream os;
    os << file.rdbuf();
    return os.str();
}

/// The distinctive encoder settings the recording tests use. Every field
/// differs from BOTH its struct default (ConfigData.hpp: codec libx264, crf 18,
/// medium, yuv420p, 1920x1080@60; audio aac/320) and from what populate() below
/// writes (libx265, 30, slow, yuv444p, 640x360@24; libopus/128), so neither a
/// dropped key nor a load that quietly kept the previous in-memory value can
/// pass. Every value also sits inside the clamps parseRecording applies
/// (crf 0-51, width 160-7680, height 120-4320, fps 10-120, bitrate 64-640) and
/// the dimensions are already even, so the post-clamp value is the value written.
RecordingConfig distinctiveEncoders() {
    RecordingConfig rec;
    rec.video.codec = "libx265probe";
    rec.video.crf = 41;
    rec.video.preset = "veryslow";
    rec.video.pixelFormat = "yuv444p10le";
    rec.video.width = 2560;
    rec.video.height = 1440;
    rec.video.fps = 24;
    rec.audio.codec = "libopusprobe";
    rec.audio.bitrate = 256;
    return rec;
}

/// Every assertion for the state distinctiveEncoders() wrote, against a config
/// that was re-read from a serialized document.
void verifyDistinctiveEncoders(const RecordingConfig& rec) {
    QCOMPARE(rec.video.codec, std::string("libx265probe"));
    QCOMPARE(rec.video.crf, 41u);
    QCOMPARE(rec.video.preset, std::string("veryslow"));
    QCOMPARE(rec.video.pixelFormat, std::string("yuv444p10le"));
    QCOMPARE(rec.video.width, 2560u);
    QCOMPARE(rec.video.height, 1440u);
    QCOMPARE(rec.video.fps, 24u);
    QCOMPARE(rec.audio.codec, std::string("libopusprobe"));
    QCOMPARE(rec.audio.bitrate, 256u);
}

/// Populate EVERY field with a value that differs from its struct default, so a
/// dropped key can never masquerade as a passing round trip. `seed` gives two
/// distinguishable states: save with 1, clobber with 2, reload, expect 1.
///
/// Values stay inside every clamp the parsers apply (width/height/fps/crf/
/// bitrate/sensitivity/mesh/sidebar) and every float is exactly representable,
/// because a round-trip assertion comparing floats must not fail on a
/// representation artefact rather than on a lost key.
void populate(Config& c, int seed) {
    const std::string tag = std::to_string(seed);

    c.setDebug(seed % 2 == 1);

    auto& audio = c.audio();
    audio.device = "device-" + tag;
    audio.bufferSize = 1024u;
    audio.sampleRate = 48000u;

    auto& vis = c.visualizer();
    vis.presetPath = fs::path("/tmp/presets-" + tag);
    vis.width = 1280u;
    vis.height = 720u;
    vis.fps = 30u;
    vis.beatSensitivity = 2.5f;
    vis.presetDuration = 15u;
    vis.smoothPresetDuration = 3u;
    vis.hardCutSensitivity = 4.5f;
    vis.aspectCorrection = false;
    vis.shufflePresets = false;
    vis.forcePreset = "forced-" + tag;
    vis.useDefaultPreset = true;
    vis.meshX = 16u;
    vis.meshY = 12u;
    vis.texturePaths = {fs::path("/tmp/tex-a.png"), fs::path("/tmp/tex-b.png")};

    auto& rec = c.recording();
    rec.enabled = false;
    rec.autoRecord = true;
    rec.recordEntireSong = true;
    rec.restartTrackOnRecord = true;
    rec.stopAtTrackEnd = true;
    rec.outputDirectory = fs::path("/tmp/rec-" + tag);
    rec.defaultFilename = "clip-" + tag;
    rec.container = "mkv";
    rec.video.codec = "libx265";
    rec.video.crf = 30u;
    rec.video.preset = "slow";
    rec.video.pixelFormat = "yuv444p";
    rec.video.width = 640u;
    rec.video.height = 360u;
    rec.video.fps = 24u;
    rec.audio.codec = "libopus";
    rec.audio.bitrate = 128u;

    auto& ui = c.ui();
    ui.theme = "light-" + tag;
    ui.showPlaylist = false;
    ui.showPresets = false;
    ui.showDebugPanel = true;
    ui.backgroundColor = Color::fromHex("#112233FF");
    ui.accentColor = Color::fromHex("#445566FF");
    ui.expandedPanel = "panel-" + tag;
    ui.sidebarWidth = 333;
    ui.drawerOpen = true;

    auto& keys = c.keyboard();
    keys.playPause = "p" + tag;
    keys.nextTrack = "n" + tag;
    keys.prevTrack = "b" + tag;
    keys.toggleRecord = "r" + tag;
    keys.toggleFullscreen = "f" + tag;
    keys.nextPreset = "right" + tag;
    keys.prevPreset = "left" + tag;

    auto& suno = c.suno();
    suno.deviceId = "device-" + tag;
    // Legacy credential, exactly as SunoClient's failed migration leaves it.
    // Neither may ever reach the serializer.
    suno.token = "aaaa.bbbb.cccc";
    suno.cookie = "__client=SECRET-" + tag;
    suno.downloadPath = fs::path("/tmp/dl-" + tag);
    suno.autoDownload = true;
    suno.saveLyrics = false;
    suno.embedMetadata = false;
    suno.downloadFormat = SunoDownloadFormat::WAV;
    suno.debugLyrics = true;
    suno.debugLyricsFile = fs::path("/tmp/dbg-" + tag + ".json");

    auto& karaoke = c.karaoke();
    karaoke.enabled = false;
    karaoke.fontFamily = "Serif-" + tag;
    karaoke.fontSize = 48u;
    karaoke.bold = false;
    karaoke.yPosition = 0.25f;
    karaoke.activeColor = Color::fromHex("#FF00FFFF");
    karaoke.inactiveColor = Color::fromHex("#00FFFFFF");
    karaoke.shadowColor = Color::fromHex("#00000080");

    auto& overlay = c.overlay();
    overlay.enabled = seed % 2 == 0;
    overlay.elements.clear();
    OverlayElementConfig element;
    element.id = "el-" + tag;
    element.text = "text-" + tag;
    element.position = {0.25f, 0.75f};
    element.fontSize = 44u;
    element.color = Color::fromHex("#ABCDEFFF");
    element.opacity = 0.5f;
    element.animation = "pulse-" + tag;
    element.animationSpeed = 2.5f;
    element.anchor = "right";
    element.visible = false;
    overlay.elements.push_back(std::move(element));
}

/// Every assertion for the state populate(c, tag) wrote. Exact values, never
/// "non-zero" or "not the default": the whole class of bug here is a field
/// silently reverting to a default that happens to look plausible.
void verifyPopulated(const Config& c, const std::string& tag) {
    QCOMPARE(c.debug(), tag == "1");

    QCOMPARE(c.audio().device, "device-" + tag);
    QCOMPARE(c.audio().bufferSize, 1024u);
    QCOMPARE(c.audio().sampleRate, 48000u);

    QCOMPARE(c.visualizer().presetPath.string(), "/tmp/presets-" + tag);
    QCOMPARE(c.visualizer().width, 1280u);
    QCOMPARE(c.visualizer().height, 720u);
    QCOMPARE(c.visualizer().fps, 30u);
    QCOMPARE(c.visualizer().beatSensitivity, 2.5f);
    QCOMPARE(c.visualizer().presetDuration, 15u);
    QCOMPARE(c.visualizer().smoothPresetDuration, 3u);
    QCOMPARE(c.visualizer().hardCutSensitivity, 4.5f);
    QCOMPARE(c.visualizer().aspectCorrection, false);
    QCOMPARE(c.visualizer().shufflePresets, false);
    QCOMPARE(c.visualizer().forcePreset, "forced-" + tag);
    QCOMPARE(c.visualizer().useDefaultPreset, true);
    QCOMPARE(c.visualizer().meshX, 16u);
    QCOMPARE(c.visualizer().meshY, 12u);
    QCOMPARE(c.visualizer().texturePaths.size(), std::size_t{2});
    QCOMPARE(c.visualizer().texturePaths[0].string(), std::string("/tmp/tex-a.png"));
    QCOMPARE(c.visualizer().texturePaths[1].string(), std::string("/tmp/tex-b.png"));

    QCOMPARE(c.recording().enabled, false);
    QCOMPARE(c.recording().autoRecord, true);
    QCOMPARE(c.recording().recordEntireSong, true);
    QCOMPARE(c.recording().restartTrackOnRecord, true);
    QCOMPARE(c.recording().stopAtTrackEnd, true);
    QCOMPARE(c.recording().outputDirectory.string(), "/tmp/rec-" + tag);
    QCOMPARE(c.recording().defaultFilename, "clip-" + tag);
    QCOMPARE(c.recording().container, std::string("mkv"));
    QCOMPARE(c.recording().video.codec, std::string("libx265"));
    QCOMPARE(c.recording().video.crf, 30u);
    QCOMPARE(c.recording().video.preset, std::string("slow"));
    QCOMPARE(c.recording().video.pixelFormat, std::string("yuv444p"));
    QCOMPARE(c.recording().video.width, 640u);
    QCOMPARE(c.recording().video.height, 360u);
    QCOMPARE(c.recording().video.fps, 24u);
    QCOMPARE(c.recording().audio.codec, std::string("libopus"));
    QCOMPARE(c.recording().audio.bitrate, 128u);

    QCOMPARE(c.ui().theme, "light-" + tag);
    QCOMPARE(c.ui().showPlaylist, false);
    QCOMPARE(c.ui().showPresets, false);
    QCOMPARE(c.ui().showDebugPanel, true);
    QCOMPARE(c.ui().backgroundColor.toHex(), std::string("#112233FF"));
    QCOMPARE(c.ui().accentColor.toHex(), std::string("#445566FF"));
    QCOMPARE(c.ui().expandedPanel, "panel-" + tag);
    QCOMPARE(c.ui().sidebarWidth, 333);
    QCOMPARE(c.ui().drawerOpen, true);

    QCOMPARE(c.keyboard().playPause, "p" + tag);
    QCOMPARE(c.keyboard().nextTrack, "n" + tag);
    QCOMPARE(c.keyboard().prevTrack, "b" + tag);
    QCOMPARE(c.keyboard().toggleRecord, "r" + tag);
    QCOMPARE(c.keyboard().toggleFullscreen, "f" + tag);
    QCOMPARE(c.keyboard().nextPreset, "right" + tag);
    QCOMPARE(c.keyboard().prevPreset, "left" + tag);

    QCOMPARE(c.suno().deviceId, "device-" + tag);
    QCOMPARE(c.suno().downloadPath.string(), "/tmp/dl-" + tag);
    QCOMPARE(c.suno().autoDownload, true);
    QCOMPARE(c.suno().saveLyrics, false);
    QCOMPARE(c.suno().embedMetadata, false);
    QCOMPARE(c.suno().debugLyrics, true);
    QCOMPARE(c.suno().debugLyricsFile.string(), "/tmp/dbg-" + tag + ".json");
    QVERIFY(c.suno().downloadFormat == SunoDownloadFormat::WAV);

    QCOMPARE(c.karaoke().enabled, false);
    QCOMPARE(c.karaoke().fontFamily, "Serif-" + tag);
    QCOMPARE(c.karaoke().fontSize, 48u);
    QCOMPARE(c.karaoke().bold, false);
    QCOMPARE(c.karaoke().yPosition, 0.25f);
    QCOMPARE(c.karaoke().activeColor.toHex(), std::string("#FF00FFFF"));
    QCOMPARE(c.karaoke().inactiveColor.toHex(), std::string("#00FFFFFF"));
    QCOMPARE(c.karaoke().shadowColor.toHex(), std::string("#00000080"));

    QCOMPARE(c.overlay().enabled, tag == "2");
    QCOMPARE(c.overlay().elements.size(), std::size_t{1});
    if (!c.overlay().elements.empty()) {
        const auto& element = c.overlay().elements.front();
        QCOMPARE(element.id, "el-" + tag);
        QCOMPARE(element.text, "text-" + tag);
        QCOMPARE(element.position.x, 0.25f);
        QCOMPARE(element.position.y, 0.75f);
        QCOMPARE(element.fontSize, 44u);
        QCOMPARE(element.color.toHex(), std::string("#ABCDEFFF"));
        QCOMPARE(element.opacity, 0.5f);
        QCOMPARE(element.animation, "pulse-" + tag);
        QCOMPARE(element.animationSpeed, 2.5f);
        QCOMPARE(element.anchor, std::string("right"));
        QCOMPARE(element.visible, false);
    }
}

} // namespace

class TestConfigLoader : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        // Config is a singleton, so snapshot every section and put it back:
        // other suites (test_SunoDownloader, test_RecordingPipeline) read it.
        savedAudio_ = config().audio();
        savedVisualizer_ = config().visualizer();
        savedRecording_ = config().recording();
        savedUi_ = config().ui();
        savedKeyboard_ = config().keyboard();
        savedSuno_ = config().suno();
        savedKaraoke_ = config().karaoke();
        savedOverlay_ = config().overlay();
        savedDebug_ = config().debug();
    }

    void cleanupTestCase() {
        config().audio() = savedAudio_;
        config().visualizer() = savedVisualizer_;
        config().recording() = savedRecording_;
        config().ui() = savedUi_;
        config().keyboard() = savedKeyboard_;
        config().suno() = savedSuno_;
        config().karaoke() = savedKaraoke_;
        config().overlay() = savedOverlay_;
        config().setDebug(savedDebug_);
    }

    void roundTripPreservesEveryField() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const auto path = fs::path(dir.path().toStdString()) / "config.toml";

        populate(config(), 1);
        const auto saved = ConfigLoader::save(config(), path);
        QVERIFY2(saved.isOk(), saved.isErr() ? saved.error().message.c_str() : "");

        // Clobber every field, then reload: a value that survives can only have
        // come off disk.
        populate(config(), 2);
        const auto loaded = ConfigLoader::load(config(), path);
        QVERIFY2(loaded.isOk(), loaded.isErr() ? loaded.error().message.c_str() : "");

        verifyPopulated(config(), "1");

        // A durable save must not leave the temp file behind.
        QVERIFY(!fs::exists(fs::path(path.string() + ".tmp")));
    }

    // The two encoder sub-tables are built by expanding CHADVIS_VIDEO_FIELDS and
    // CHADVIS_REC_AUDIO_FIELDS, and those expand to `out.insert(...)` -- the
    // destination table has to be named `out`. They used to be built in locals
    // named `videoOut`/`audioOut`, which the macro never saw, so BOTH sub-tables
    // were written empty and every encoder key landed flat in [recording],
    // where video.codec and audio.codec collided on one key. Nothing detected it
    // because roundTripPreservesEveryField() writes the SAME encoder values for
    // both of its populate() seeds, so a load that read nothing back left
    // populate(2)'s values in place and every assertion still matched.
    void recordingEncoderSubTablesAreWrittenAndNotCollapsed() {
        const auto tbl = serializeRecording(distinctiveEncoders());

        auto* recording = tbl["recording"].as_table();
        QVERIFY(recording != nullptr);

        auto* video = (*recording)["video"].as_table();
        QVERIFY2(video != nullptr, "[recording.video] was not written at all");
        auto* audio = (*recording)["audio"].as_table();
        QVERIFY2(audio != nullptr, "[recording.audio] was not written at all");

        // Non-empty, with the exact key count the field tables declare: 7 video
        // (codec, crf, preset, pixel_format, width, height, fps) and 2 audio
        // (codec, bitrate). "Present" alone would be satisfied by an empty
        // table, which is precisely what the bug produced.
        QCOMPARE(video->size(), std::size_t{7});
        QCOMPARE(audio->size(), std::size_t{2});

        // The collision that lost the data: one flat `codec` key carried both
        // values and toml++ insert() keeps the first, so the AUDIO codec was the
        // one silently discarded. Both must be present, distinct, and correct.
        const auto videoCodec = (*video)["codec"].as_string();
        const auto audioCodec = (*audio)["codec"].as_string();
        QVERIFY(videoCodec != nullptr);
        QVERIFY(audioCodec != nullptr);
        QCOMPARE(videoCodec->get(), std::string("libx265probe"));
        QCOMPARE(audioCodec->get(), std::string("libopusprobe"));
        QVERIFY(videoCodec->get() != audioCodec->get());

        // The rest of each group landed in its own sub-table.
        QCOMPARE((*video)["crf"].as_integer()->get(), (i64)41);
        QCOMPARE((*video)["preset"].as_string()->get(), std::string("veryslow"));
        QCOMPARE((*video)["pixel_format"].as_string()->get(), std::string("yuv444p10le"));
        QCOMPARE((*video)["width"].as_integer()->get(), (i64)2560);
        QCOMPARE((*video)["height"].as_integer()->get(), (i64)1440);
        QCOMPARE((*video)["fps"].as_integer()->get(), (i64)24);
        QCOMPARE((*audio)["bitrate"].as_integer()->get(), (i64)256);

        // And the flat pollution is gone: no encoder key may sit at the top
        // level of [recording]. Pre-fix all eight were there.
        for (const char* key : {"codec", "crf", "preset", "pixel_format", "width",
                                "height", "fps", "bitrate"}) {
            QVERIFY2(!recording->contains(key), key);
        }

        // The eight keys that DO belong at the top level are untouched, with
        // exact counts, so the check above cannot pass by emptying the table.
        QCOMPARE(recording->size(), std::size_t{10});  // 8 plain + video + audio
        QVERIFY(recording->contains("enabled"));
        QVERIFY(recording->contains("auto_record"));
        QVERIFY(recording->contains("record_entire_song"));
        QVERIFY(recording->contains("restart_track_on_record"));
        QVERIFY(recording->contains("stop_at_track_end"));
        QVERIFY(recording->contains("default_filename"));
        QVERIFY(recording->contains("container"));
        QVERIFY(recording->contains("output_directory"));
    }

    void recordingEncoderSettingsRoundTripExactly() {
        const auto tbl = serializeRecording(distinctiveEncoders());

        // Parsing into a DEFAULT-CONSTRUCTED config is the honest shape of the
        // test: the values must come off the document. Parsing into the struct
        // they were written from would pass even if the document carried
        // nothing, which is what happened.
        RecordingConfig reloaded;
        ConfigParsers::parseRecording(tbl, reloaded);
        verifyDistinctiveEncoders(reloaded);

        // Same again through the RENDERED text, because that is what save()
        // writes and what parse_file() reads back -- a sub-table rendered in the
        // wrong place, or a scalar after a sub-table, would pass the in-memory
        // check and fail here.
        const auto text = render(tbl);
        RecordingConfig fromText;
        ConfigParsers::parseRecording(toml::parse(text), fromText);
        verifyDistinctiveEncoders(fromText);

        // The clamps parseRecording applies did not move any of them: an
        // assertion on the pre-clamp value would miss a clamp that had crept in.
        QCOMPARE(fromText.video.crf, 41u);
        QCOMPARE(fromText.video.width, 2560u);
        QCOMPARE(fromText.video.height, 1440u);
        QCOMPARE(fromText.video.fps, 24u);
        QCOMPARE(fromText.audio.bitrate, 256u);
    }

    void recordingEncoderSettingsSurviveTheFileOnDisk() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const auto path = fs::path(dir.path().toStdString()) / "config.toml";

        // populate() first, so every OTHER section is exercised too and the
        // encoder values start from something different from the ones asserted.
        populate(config(), 1);
        config().recording().video.codec = "libx265probe";
        config().recording().video.crf = 41;
        config().recording().video.preset = "veryslow";
        config().recording().video.pixelFormat = "yuv444p10le";
        config().recording().video.width = 2560;
        config().recording().video.height = 1440;
        config().recording().video.fps = 24;
        config().recording().audio.codec = "libopusprobe";
        config().recording().audio.bitrate = 256;

        const auto saved = ConfigLoader::save(config(), path);
        QVERIFY2(saved.isOk(), saved.isErr() ? saved.error().message.c_str() : "");

        // Clobber to pure struct defaults, so nothing can survive from memory.
        config().recording() = RecordingConfig{};
        QCOMPARE(config().recording().video.codec, std::string("libx264"));

        const auto loaded = ConfigLoader::load(config(), path);
        QVERIFY2(loaded.isOk(), loaded.isErr() ? loaded.error().message.c_str() : "");
        verifyDistinctiveEncoders(config().recording());

        // The same two keys, read back out of the FILE rather than through the
        // struct, so a serializer that nested them correctly but rendered them
        // unrecoverably would still be caught.
        const auto reparsed = toml::parse_file(path.string());
        auto* recording = reparsed["recording"].as_table();
        QVERIFY(recording != nullptr);
        auto* video = (*recording)["video"].as_table();
        QVERIFY(video != nullptr);
        auto* audio = (*recording)["audio"].as_table();
        QVERIFY(audio != nullptr);
        QCOMPARE((*video)["codec"].as_string()->get(), std::string("libx265probe"));
        QCOMPARE((*audio)["codec"].as_string()->get(), std::string("libopusprobe"));
        QVERIFY((*video)["codec"].as_string()->get() !=
                (*audio)["codec"].as_string()->get());
        for (const char* key : {"codec", "crf", "preset", "pixel_format", "width",
                                "height", "fps", "bitrate"}) {
            QVERIFY2(!recording->contains(key), key);
        }
        QVERIFY(!fs::exists(fs::path(path.string() + ".tmp")));
    }

    void serializeNeverEmitsCredentials() {
        populate(config(), 3);
        const auto tbl = serializeConfig(config());
        auto* suno = tbl["suno"].as_table();
        QVERIFY(suno != nullptr);

        // The whole point: no `token` and no `cookie` key, even though the
        // struct holds both (the state SunoClient leaves behind when the
        // keychain migration fails).
        QVERIFY(!suno->contains("token"));
        QVERIFY(!suno->contains("cookie"));

        // Exact-value companions. Without these two, a serializer that emitted
        // an EMPTY [suno] table would satisfy the assertions above.
        const auto deviceId = (*suno)["device_id"].as_string();
        QVERIFY(deviceId != nullptr);
        QCOMPARE(deviceId->get(), std::string("device-3"));

        const auto downloadPath = (*suno)["download_path"].as_string();
        QVERIFY(downloadPath != nullptr);
        QCOMPARE(downloadPath->get(), std::string("/tmp/dl-3"));

        // And the rendered document must not leak the values themselves, under
        // any key.
        const auto text = render(tbl);
        QVERIFY(text.find("aaaa.bbbb.cccc") == std::string::npos);
        QVERIFY(text.find("__client=SECRET") == std::string::npos);
        QVERIFY(text.find("token") == std::string::npos);
        QVERIFY(text.find("cookie") == std::string::npos);
    }

    // serializeNeverEmitsCredentials() inspects the in-memory table and the
    // string `os << tbl` would produce. That is now one step short of the thing
    // that matters: save() writes those bytes to `path + ".tmp"`, fsyncs them,
    // restricts the mode and then replaces `path` with it. A future change to
    // that pipeline -- writing a header line, merging into the previous
    // document, appending instead of truncating -- could put a credential on
    // disk while the serializer itself stayed clean. So the same three
    // assertions are repeated against the bytes that actually reached the file,
    // and against the parsed result of that file.
    void savedFileNeverContainsACredential() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const auto path = fs::path(dir.path().toStdString()) / "config.toml";

        populate(config(), 7);
        // Exact pre-state: the secret IS in the struct, so a pass can only mean
        // the write path dropped it.
        QCOMPARE(config().suno().token, std::string("aaaa.bbbb.cccc"));
        QCOMPARE(config().suno().cookie, std::string("__client=SECRET-7"));

        const auto saved = ConfigLoader::save(config(), path);
        QVERIFY2(saved.isOk(), saved.isErr() ? saved.error().message.c_str() : "");

        const auto onDisk = readText(path);
        QVERIFY2(!onDisk.empty(), "the saved file is empty, so this proves nothing");
        QCOMPARE(onDisk.find("aaaa.bbbb.cccc"), std::string::npos);
        QCOMPARE(onDisk.find("__client=SECRET"), std::string::npos);
        QCOMPARE(onDisk.find("token"), std::string::npos);
        QCOMPARE(onDisk.find("cookie"), std::string::npos);

        // Parsed, not just grepped: the [suno] table must still be there and
        // still carry its real keys.
        const auto reparsed = toml::parse_file(path.string());
        auto* suno = reparsed["suno"].as_table();
        QVERIFY(suno != nullptr);
        QVERIFY(!suno->contains("token"));
        QVERIFY(!suno->contains("cookie"));
        QCOMPARE((*suno)["device_id"].as_string()->get(), std::string("device-7"));

        // And no temp file was left holding the secret either.
        QVERIFY(!fs::exists(fs::path(path.string() + ".tmp")));
    }

    // The Windows-replace path: the second save is the one that used to fail
    // (fs::rename over an existing file) and the one that goes through
    // MoveFileEx / remove-then-rename today. Both writes are checked, because
    // "the second save cleaned it up" is exactly the wrong thing to rely on
    // when the first one is the one that leaked.
    void repeatedSavesNeverLeaveACredentialOnDisk() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const auto path = fs::path(dir.path().toStdString()) / "config.toml";

        populate(config(), 8);
        QCOMPARE(config().suno().cookie, std::string("__client=SECRET-8"));
        auto saved = ConfigLoader::save(config(), path);
        QVERIFY2(saved.isOk(), saved.isErr() ? saved.error().message.c_str() : "");
        {
            const auto first = readText(path);
            QCOMPARE(first.find("aaaa.bbbb.cccc"), std::string::npos);
            QCOMPARE(first.find("__client=SECRET"), std::string::npos);
            QCOMPARE(first.find("token"), std::string::npos);
            QCOMPARE(first.find("cookie"), std::string::npos);
            // Named, not `toml::parse_file(...)[..].as_table()`: as_table()
            // hands back a VIEW into the parse result, so taking it from a
            // temporary leaves a pointer into an object already destroyed.
            const auto reparsed = toml::parse_file(path.string());
            auto* suno = reparsed["suno"].as_table();
            QVERIFY(suno != nullptr);
            QVERIFY(!suno->contains("token"));
            QVERIFY(!suno->contains("cookie"));
        }

        populate(config(), 9);
        QCOMPARE(config().suno().cookie, std::string("__client=SECRET-9"));
        saved = ConfigLoader::save(config(), path);
        QVERIFY2(saved.isOk(), saved.isErr() ? saved.error().message.c_str() : "");
        {
            const auto second = readText(path);
            QCOMPARE(second.find("aaaa.bbbb.cccc"), std::string::npos);
            QCOMPARE(second.find("__client=SECRET"), std::string::npos);
            QCOMPARE(second.find("token"), std::string::npos);
            QCOMPARE(second.find("cookie"), std::string::npos);
            const auto reparsed = toml::parse_file(path.string());
            auto* suno = reparsed["suno"].as_table();
            QVERIFY(suno != nullptr);
            QVERIFY(!suno->contains("token"));
            QVERIFY(!suno->contains("cookie"));
            // Exact-value companion on both writes: an empty [suno] table would
            // satisfy every assertion above.
            QCOMPARE((*suno)["device_id"].as_string()->get(), std::string("device-9"));
        }

        // The replacement really happened: the second write is the live file and
        // the temp file did not survive it.
        QVERIFY(!fs::exists(fs::path(path.string() + ".tmp")));
    }

    void credentialsAreStillReadableForMigration() {
        // The security fix must not break the one-time migration that depends
        // on finding the legacy secret, so the parse path has to keep working.
        auto tbl = toml::parse(R"(
            [suno]
            token = "legacy.jwt.value"
            cookie = "__client=legacy-cookie"
            device_id = 'device-x'
        )");
        SunoConfig cfg;
        ConfigParsers::parseSuno(tbl, cfg);
        QCOMPARE(cfg.token, std::string("legacy.jwt.value"));
        QCOMPARE(cfg.cookie, std::string("__client=legacy-cookie"));
        QCOMPARE(cfg.deviceId, std::string("device-x"));
    }

    void overlayEnabledRoundTripsInBothStates() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const auto path = fs::path(dir.path().toStdString()) / "config.toml";

        populate(config(), 2);
        config().overlay().enabled = false;
        auto saved = ConfigLoader::save(config(), path);
        QVERIFY2(saved.isOk(), saved.isErr() ? saved.error().message.c_str() : "");
        auto loaded = ConfigLoader::load(config(), path);
        QVERIFY2(loaded.isOk(), loaded.isErr() ? loaded.error().message.c_str() : "");

        // Exact `false`, NOT `!enabled`: a parser that silently ignored the key
        // would leave the struct default `true`, and `!true` would pass.
        QCOMPARE(config().overlay().enabled, false);

        config().overlay().enabled = true;
        saved = ConfigLoader::save(config(), path);
        QVERIFY2(saved.isOk(), saved.isErr() ? saved.error().message.c_str() : "");
        loaded = ConfigLoader::load(config(), path);
        QVERIFY2(loaded.isOk(), loaded.isErr() ? loaded.error().message.c_str() : "");
        QCOMPARE(config().overlay().enabled, true);
    }

    void unknownKeysAreDropped() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const auto path = fs::path(dir.path().toStdString()) / "config.toml";

        writeText(path, "[general]\n"
                        "debug = true\n"
                        "\n"
                        "[visualizer]\n"
                        "width = 1600\n"
                        "future_key = 7\n"
                        "\n"
                        "[my_own_table]\n"
                        "keep = 'please'\n");

        auto loaded = ConfigLoader::load(config(), path);
        QVERIFY2(loaded.isOk(), loaded.isErr() ? loaded.error().message.c_str() : "");
        QCOMPARE(config().visualizer().width, 1600u);

        const auto saved = ConfigLoader::save(config(), path);
        QVERIFY2(saved.isOk(), saved.isErr() ? saved.error().message.c_str() : "");

        const auto reparsed = toml::parse_file(path.string());
        const auto* visualizer = reparsed["visualizer"].as_table();
        QVERIFY(visualizer != nullptr);
        QCOMPARE((*visualizer)["width"].as_integer()->get(), (i64)1600);
        // Documented decision, asserted: unknown key and unknown section both
        // go. Known keys are untouched.
        QVERIFY(!visualizer->contains("future_key"));
        QVERIFY(!reparsed.contains("my_own_table"));
        QVERIFY(reparsed.contains("audio"));
    }

    void corruptFileIsDiagnosedWithoutThrowing() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const auto path = fs::path(dir.path().toStdString()) / "config.toml";

        populate(config(), 4);
        const auto widthBefore = config().visualizer().width;

        writeText(path, "[general\ndebug = tru");
        const auto loaded = ConfigLoader::load(config(), path);
        QVERIFY(loaded.isErr());
        QVERIFY(!loaded.error().message.empty());
        // toml::parse_file throws before any field is touched, so a rejected
        // file leaves the previous state alone.
        QCOMPARE(config().visualizer().width, widthBefore);
    }

    void malformedValueFallsBackToDefaults() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const auto path = fs::path(dir.path().toStdString()) / "config.toml";

        // Valid TOML, wrong value types: tolerated, not fatal.
        writeText(path, "[audio]\n"
                        "buffer_size = 'not-a-number'\n"
                        "\n"
                        "[general]\n"
                        "debug = 'yes'\n");
        const auto loaded = ConfigLoader::load(config(), path);
        QVERIFY2(loaded.isOk(), loaded.isErr() ? loaded.error().message.c_str() : "");
        QCOMPARE(config().audio().bufferSize, AudioConfig{}.bufferSize);
        QCOMPARE(config().debug(), false);
    }

    void aMalformedColorIsDiagnosedRatherThanFatal() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const auto path = fs::path(dir.path().toStdString()) / "config.toml";

        // Two generations of this bug, both fixed, and the test now pins the
        // fixed state rather than the old failure mode.
        //
        // Originally Color::fromHex used std::stoi, which THREW
        // std::invalid_argument on a 6- or 8-character non-hex value and escaped
        // load()'s catch(toml::parse_error) to terminate the app. The
        // std::exception backstop turned that into a diagnostic.
        //
        // Then parseHexColor() replaced the stoi entirely: allocation-free,
        // non-throwing, and returning a Result with four distinct verdicts.
        // Color::fromHex is now a documented lossy shim over it that LOGS the
        // defect and still returns the historical value, so the config loads.
        //
        // The load therefore SUCCEEDS now. Asserting isErr() would be asserting
        // the old bug, so the assertions are on what a caller can actually
        // observe: it loads, the other keys still arrive, and the bad color did
        // not become a silently wrong one.
        writeText(path, "[ui]\naccent_color = 'zzzzzz'\ntheme = 'dark'\n");
        const auto loaded = ConfigLoader::load(config(), path);
        QVERIFY2(loaded.isOk(), loaded.isErr() ? loaded.error().message.c_str() : "");

        // The rest of the document parsed: a bad value must not cost the user
        // every other setting in the file.
        QCOMPARE(config().ui().theme, std::string("dark"));

        // And the colour is the documented lossy fallback, not a parsed value
        // that happens to be wrong. Opaque white is what Color{} is.
        QCOMPARE(config().ui().accentColor.r, u8{255});
        QCOMPARE(config().ui().accentColor.g, u8{255});
        QCOMPARE(config().ui().accentColor.b, u8{255});

        // The strict entry point is the one that refuses, and it names which of
        // the four defects it was. Assert the enum, not a message.
        const auto parsed = vc::parseHexColor("#zzzzzz");
        QVERIFY(!parsed.has_value());
        QCOMPARE(parsed.error().code, vc::ColorParseError::NotHex);
        // An empty string is its own defect, not NotHex -- "you gave me nothing"
        // and "you gave me letters" are different mistakes.
        QVERIFY(!vc::parseHexColor("").has_value());
        QCOMPARE(vc::parseHexColor("").error().code, vc::ColorParseError::Empty);
        // Order matters and is load-bearing: a string with no '#' at all is
        // MissingHash even when its length is also wrong, because "you forgot
        // the hash" is the first thing wrong with it. BadLength is only
        // reachable once the '#' is there.
        QCOMPARE(vc::parseHexColor("12345").error().code, vc::ColorParseError::MissingHash);
        QCOMPARE(vc::parseHexColor("#12345").error().code, vc::ColorParseError::BadLength);
        QCOMPARE(vc::parseHexColor("#1234567890").error().code,
                 vc::ColorParseError::BadLength);

        // And a well-formed colour round-trips exactly, which is the positive
        // half of the same contract.
        // toHexString always emits the 8-digit RGBA form in UPPER case, so the
        // round trip is parse(toHex(c)) == c rather than string equality --
        // which is what the Color.hpp contract actually claims.
        const auto good = vc::parseHexColor("#1a2b3c");
        QVERIFY(good.has_value());
        QCOMPARE(good->r, u8{0x1a});
        QCOMPARE(good->g, u8{0x2b});
        QCOMPARE(good->b, u8{0x3c});
        QCOMPARE(good->a, u8{0xff});
        QCOMPARE(vc::toHexString(*good), std::string("#1A2B3CFF"));
        const auto again = vc::parseHexColor(vc::toHexString(*good));
        QVERIFY(again.has_value());
        QCOMPARE(again->r, good->r);
        QCOMPARE(again->g, good->g);
        QCOMPARE(again->b, good->b);
        QCOMPARE(again->a, good->a);
    }

    void missingFileIsReportedNotThrown() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const auto loaded =
                ConfigLoader::load(config(), fs::path(dir.path().toStdString()) / "absent.toml");
        QVERIFY(loaded.isErr());
        QVERIFY(!loaded.error().message.empty());
    }

    void savingTwiceReplacesTheExistingFile() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const auto path = fs::path(dir.path().toStdString()) / "config.toml";

        // The Windows regression: fs::rename over an existing file fails there,
        // so the second save used to report an error and orphan the .tmp.
        populate(config(), 5);
        auto saved = ConfigLoader::save(config(), path);
        QVERIFY2(saved.isOk(), saved.isErr() ? saved.error().message.c_str() : "");

        populate(config(), 6);
        saved = ConfigLoader::save(config(), path);
        QVERIFY2(saved.isOk(), saved.isErr() ? saved.error().message.c_str() : "");

        // The second write is the one on disk, not the first.
        const auto reparsed = toml::parse_file(path.string());
        const auto* visualizer = reparsed["visualizer"].as_table();
        QVERIFY(visualizer != nullptr);
        const auto forcedPreset = (*visualizer)["force_preset"].as_string();
        QVERIFY(forcedPreset != nullptr);
        QCOMPARE(forcedPreset->get(), std::string("forced-6"));
        QVERIFY(!fs::exists(fs::path(path.string() + ".tmp")));
    }

    void shippedTemplateDeclaresEveryKey() {
#ifdef CHADVIS_SOURCE_DIR
        const auto path = fs::path(CHADVIS_SOURCE_DIR) / "config" / "default.toml";
        if (!fs::exists(path)) QSKIP("source tree not available");

        const auto templateTbl = toml::parse_file(path.string());

        // The key that motivated this suite: aspect_correction is parsed and
        // applied by Engine::init, and its absence decided letterbox-vs-squash
        // for a vertical render with nothing on screen saying so.
        const auto* visualizer = templateTbl["visualizer"].as_table();
        QVERIFY(visualizer != nullptr);
        QVERIFY(visualizer->contains("aspect_correction"));
        const auto aspect = (*visualizer)["aspect_correction"].as_boolean();
        QVERIFY(aspect != nullptr);
        QCOMPARE(aspect->get(), VisualizerConfig{}.aspectCorrection);

        // Every key the serializer emits must be declared by the template,
        // otherwise the shipped file silently diverges from the parser.
        const auto serialized = serializeConfig(config());
        for (auto it = serialized.begin(); it != serialized.end(); ++it) {
            const auto sectionEntry = *it;
            const std::string section{sectionEntry.first};
            auto* expected = sectionEntry.second.as_table();
            if (!expected) continue;
            auto* declared = templateTbl[section].as_table();
            QVERIFY2(declared != nullptr, section.c_str());
            for (auto key = expected->begin(); key != expected->end(); ++key) {
                const auto keyEntry = *key;
                QVERIFY2(declared->contains(std::string(keyEntry.first)),
                         (section + "." + std::string(keyEntry.first)).c_str());
            }
        }

        // And the template must never carry a credential key.
        const auto* suno = templateTbl["suno"].as_table();
        QVERIFY(suno != nullptr);
        QVERIFY(!suno->contains("token"));
        QVERIFY(!suno->contains("cookie"));
#else
        QSKIP("CHADVIS_SOURCE_DIR not defined");
#endif
    }

private:
    static Config& config() { return Config::instance(); }

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

int runTestConfigLoader(int argc, char** argv) {
    TestConfigLoader tc;
    return QTest::qExec(&tc, argc, argv);
}

#include "test_ConfigLoader.moc"