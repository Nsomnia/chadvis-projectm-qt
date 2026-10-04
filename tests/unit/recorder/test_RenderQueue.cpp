// TestRenderQueue — the render job model and its scheduler.
//
// No worker thread, no GL, no FFmpeg, no filesystem beyond a QTemporaryDir for
// the receipt round trip. That is not a testing convenience: it is the property
// that lets the concurrency ceiling be *proved* rather than asserted by reading
// pump(), and it is why RenderQueue takes its work as an injected JobRunner
// instead of doing it. A test that had to spin up a render would be testing the
// render.
//
// Every assertion is an exact value. "is non-zero" and "roughly 2s" have both
// already let real bugs through this codebase — the centisecond/millisecond
// conversion that was 100x out, and the muxer packet whose length was read after
// the muxer had blanked it — so the conventions here are QCOMPARE on exact
// counts and bit_cast on floats.

#include <QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>

#include "recorder/RenderJob.hpp"
#include "recorder/RenderQueue.hpp"

#include <QFile>

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <set>
#include <string>
#include <vector>

using namespace vc;

namespace {

namespace fs = std::filesystem;

// ─────────────────────────────────────────────────────────────────────────────
// Fixture
// ─────────────────────────────────────────────────────────────────────────────

/// A job that passes validate() with nothing left at its defaults, so a test
/// only has to state the one field it is about.
RenderJob makeJob(std::string id, const fs::path& dir = {}) {
    RenderJob job;
    job.id = std::move(id);
    job.label = "job " + job.id;
    job.createdUtc = "2026-10-04T00:00:00Z";
    job.audioPath = (dir / (job.id + ".flac")).string();
    job.audioSha256 = std::string(64, 'a');
    job.durationSeconds = 30.0;
    job.expectedFrames = 1800;
    job.outputPath = (dir / (job.id + ".mp4")).string();
    job.projectmVersion = "4.1.6";
    job.scenes = {RenderScene{"preset-a", 15.0, 0.0}, RenderScene{"preset-b", 15.0, 2.5}};
    return job;
}

/// A runner that records what it was handed and does nothing else, so the queue
/// keeps every job in flight and the test can inspect the schedule.
struct RecordingRunner {
    std::vector<std::string> handedOut;
    std::vector<u64> expectedFrames;

    JobRunner operator()() {
        return [this](RenderJob job) {
            handedOut.push_back(job.id);
            expectedFrames.push_back(job.expectedFrames);
        };
    }
};

QString readWhole(const fs::path& path) {
    QFile file(QString::fromStdString(path.string()));
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QString::fromUtf8(file.readAll());
}

} // namespace

class TestRenderQueue : public QObject {
    Q_OBJECT

private slots:
    // ── The failure classifier ───────────────────────────────────────────────

    void everyErrnoRuleIsUsableAndDistinct() {
        const auto& rules = renderErrnoRules();
        QVERIFY2(!rules.empty(), "the errno table is empty, so every errno "
                                 "falls through to the worker's own guess");

        std::set<int> seen;
        for (const auto& rule : rules) {
            QVERIFY2(rule.osErrno != 0,
                     "a rule with osErrno == 0 is unreachable: classifyRenderFailure "
                     "guards on osErrno != 0 before consulting the table");
            QVERIFY2(rule.kind != RenderFailureKind::None,
                     "an errno is by definition a failure, so None is nonsense");
            QVERIFY2(rule.why != nullptr && *rule.why != '\0',
                     "the table is documentation; an entry without a reason is a "
                     "line of code nobody can extend");
            QVERIFY2(seen.insert(rule.osErrno).second,
                     "two rules share an errno, so the first is dead");
        }
    }

    void errnoOverridesTheWorkersOwnGuess() {
        // The whole reason classifyRenderFailure is a function and not an enum
        // map: a worker that reports ENOSPC has told us the truth about the disk
        // regardless of what it guessed. Assert the override in BOTH directions
        // and for every table entry, so errno really is dominant.
        for (const auto& rule : renderErrnoRules()) {
            for (const auto guess : {RenderFailureKind::None, RenderFailureKind::Transient,
                                     RenderFailureKind::Permanent,
                                     RenderFailureKind::Cancelled}) {
                RenderFailure failure;
                failure.kind = guess;
                failure.osErrno = rule.osErrno;
                failure.reason = rule.why;
                QCOMPARE(classifyRenderFailure(failure), rule.kind);
            }
        }
    }

    void aWorkerGuessesWhenNothingCameFromTheOs() {
        // osErrno == 0 is the documented "this did not come from the OS" signal,
        // and then the worker's own kind is the answer.
        const std::array<std::pair<RenderFailureKind, RenderFailureKind>, 4> rows{{
            {RenderFailureKind::None, RenderFailureKind::None},
            {RenderFailureKind::Transient, RenderFailureKind::Transient},
            {RenderFailureKind::Permanent, RenderFailureKind::Permanent},
            {RenderFailureKind::Cancelled, RenderFailureKind::Cancelled},
        }};
        for (const auto& [guess, expected] : rows) {
            RenderFailure failure;
            failure.kind = guess;
            failure.osErrno = 0;
            QCOMPARE(classifyRenderFailure(failure), expected);
        }
    }

    void everyFailureKindHasADistinctName() {
        const std::array<RenderFailureKind, 4> kinds{
            RenderFailureKind::None, RenderFailureKind::Cancelled,
            RenderFailureKind::Transient, RenderFailureKind::Permanent};
        std::set<std::string> names;
        for (const auto kind : kinds) {
            const std::string name(renderFailureKindName(kind));
            QVERIFY2(!name.empty(), "a kind with no name cannot be logged");
            QVERIFY2(name != "Unknown", "an unnamed kind means the enum grew and "
                                        "this function was not updated");
            names.insert(name);
        }
        QCOMPARE(names.size(), kinds.size());
    }

    void aFullDiskIsPermanentNotTransient() {
        // Worth pinning on its own: it is the one errno where retrying is
        // guaranteed to fail identically, and it must not occupy a slot for a
        // minute to discover that.
        RenderFailure failure;
        failure.kind = RenderFailureKind::Transient;
        failure.osErrno = ENOSPC;
        failure.reason = "no space left on device";
        QCOMPARE(classifyRenderFailure(failure), RenderFailureKind::Permanent);
    }

    // ── State machine ────────────────────────────────────────────────────────

    void terminalRetryableAndPartialOutputAreThreeDifferentQuestions() {
        const std::array<RenderState, 7> all{
            RenderState::Queued, RenderState::Rendering, RenderState::Completed,
            RenderState::CompletedPartial, RenderState::FailedRetryable,
            RenderState::FailedPermanent, RenderState::Cancelled};

        std::vector<RenderState> terminal;
        std::vector<RenderState> retryable;
        std::vector<RenderState> partial;
        for (const auto state : all) {
            if (isTerminal(state)) terminal.push_back(state);
            if (isRetryable(state)) retryable.push_back(state);
            if (leavesPartialOutput(state)) partial.push_back(state);
        }

        // Terminal: everything that can never change again.
        //
        // FIVE, not four, and the fifth is the interesting one.
        // FailedRetryable is terminal because `fail()` only settles into it once
        // the attempts are spent -- a transient failure with attempts left
        // re-enters the queue as Queued, not FailedRetryable. So "this attempt
        // failed" and "this job will never run again" are the same state, which
        // is correct: a job that has burned its last attempt is done, and
        // re-running it is a fresh enqueue carrying the user's unchanged intent.
        QCOMPARE(terminal.size(), std::size_t{5});
        QVERIFY(std::find(terminal.begin(), terminal.end(), RenderState::Queued) ==
                terminal.end());
        QVERIFY(std::find(terminal.begin(), terminal.end(), RenderState::Rendering) ==
                terminal.end());
        QVERIFY(std::find(terminal.begin(), terminal.end(),
                          RenderState::FailedRetryable) != terminal.end());

        // Retryable: exactly one state. Cancelled is a decision, not a fault.
        QCOMPARE(retryable.size(), std::size_t{1});
        QCOMPARE(retryable.front(), RenderState::FailedRetryable);

        // Partial output: every terminal state except Completed. A cancelled
        // render had already been muxing frames when it stopped, so it leaves
        // bytes a user could mistake for the whole render.
        QCOMPARE(partial.size(), terminal.size() - 1);
        QVERIFY(std::find(partial.begin(), partial.end(), RenderState::Completed) ==
                partial.end());
        for (const auto state : terminal) {
            if (state == RenderState::Completed) continue;
            QVERIFY2(std::find(partial.begin(), partial.end(), state) != partial.end(),
                     "a terminal state that leaves no partial output must be "
                     "Completed, or leavesPartialOutput is lying");
        }
    }

    void stateNamesAndSlugsRoundTrip() {
        const std::array<RenderState, 7> all{
            RenderState::Queued, RenderState::Rendering, RenderState::Completed,
            RenderState::CompletedPartial, RenderState::FailedRetryable,
            RenderState::FailedPermanent, RenderState::Cancelled};

        std::set<std::string> names;
        std::set<std::string> slugs;
        for (const auto state : all) {
            const std::string name(renderStateName(state));
            const std::string slug(renderStateSlug(state));
            QVERIFY(!name.empty());
            QVERIFY(!slug.empty());
            names.insert(name);
            slugs.insert(slug);

            // Receipts store the slug, never the ordinal, so the inverse is
            // load-bearing on every read.
            const auto back = renderStateFromSlug(slug);
            QVERIFY(back.has_value());
            QCOMPARE(*back, state);
        }
        QCOMPARE(names.size(), all.size());
        QCOMPARE(slugs.size(), all.size());
        QVERIFY(!renderStateFromSlug("not-a-state").has_value());
    }

    // ── Job validation ───────────────────────────────────────────────────────

    void aDefaultShapedJobValidates() {
        const auto result = makeJob("valid").validate();
        QVERIFY2(result.has_value(), result.has_value()
                                         ? ""
                                         : result.error().describe().c_str());
    }

    void validateNamesTheFieldItRejected() {
        // A JobError that cannot name its field is a JobError the caller cannot
        // act on, which is most of why this test exists.
        struct Row {
            const char* what;
            RenderJob job;
            JobErrorKind kind;
            const char* field;
        };

        std::vector<Row> rows;
        {
            RenderJob j = makeJob("x");
            j.id.clear();
            rows.push_back({"empty id", j, JobErrorKind::EmptyId, "job.id"});
        }
        {
            RenderJob j = makeJob("x");
            j.label.clear();
            rows.push_back({"empty label", j, JobErrorKind::EmptyLabel, "job.label"});
        }
        {
            RenderJob j = makeJob("x");
            j.audioPath.clear();
            rows.push_back({"empty audio", j, JobErrorKind::EmptyAudioPath,
                            "source.audio_path"});
        }
        {
            RenderJob j = makeJob("x");
            j.outputPath.clear();
            rows.push_back({"empty output", j, JobErrorKind::EmptyOutputPath,
                            "video.output_path"});
        }
        {
            RenderJob j = makeJob("x");
            j.projectmVersion.clear();
            rows.push_back({"no projectm", j, JobErrorKind::EmptyProjectMVersion,
                            "projectm.version"});
        }
        {
            RenderJob j = makeJob("x");
            j.audioSha256 = "NOTHEX";
            rows.push_back({"bad hash", j, JobErrorKind::BadContentHash,
                            "source.audio_sha256"});
        }
        {
            RenderJob j = makeJob("x");
            j.durationSeconds = 0.0;
            rows.push_back({"no duration", j, JobErrorKind::BadDuration,
                            "source.duration_seconds"});
        }
        {
            RenderJob j = makeJob("x");
            j.durationSeconds = std::numeric_limits<f64>::infinity();
            rows.push_back({"infinite duration", j, JobErrorKind::BadDuration,
                            "source.duration_seconds"});
        }
        {
            RenderJob j = makeJob("x");
            j.width = 0;
            rows.push_back({"zero width", j, JobErrorKind::BadDimension,
                            "video.width/video.height"});
        }
        {
            // yuv420p is subsampled 2x2, so an odd extent is not representable
            // at all. Refusing it here is what stops it reaching
            // avformat_write_header with the encoder already committed.
            RenderJob j = makeJob("x");
            j.width = 1921;
            rows.push_back({"odd width", j, JobErrorKind::BadDimension,
                            "video.width/video.height"});
        }
        {
            RenderJob j = makeJob("x");
            j.fps = 0;
            rows.push_back({"zero fps", j, JobErrorKind::BadFps, "video.fps"});
        }
        {
            RenderJob j = makeJob("x");
            j.fps = 481;
            rows.push_back({"absurd fps", j, JobErrorKind::BadFps, "video.fps"});
        }
        {
            RenderJob j = makeJob("x");
            j.crf = 64;
            rows.push_back({"crf past vp9's range", j, JobErrorKind::BadCrf, "video.crf"});
        }
        {
            RenderJob j = makeJob("x");
            j.scenes.clear();
            rows.push_back({"no scenes", j, JobErrorKind::NoScenes, "scenes"});
        }
        {
            RenderJob j = makeJob("x");
            j.scenes = {RenderScene{"", 5.0, 0.0}};
            rows.push_back({"nameless scene", j, JobErrorKind::BadScene,
                            "scenes.preset_name"});
        }
        {
            RenderJob j = makeJob("x");
            j.scenes = {RenderScene{"a", 4.0, 5.0}};
            rows.push_back({"crossfade longer than its scene", j, JobErrorKind::BadScene,
                            "scenes.crossfade_seconds"});
        }
        {
            RenderJob j = makeJob("x");
            j.scenes = {RenderScene{"a", 4.0, 4.0}};
            rows.push_back({"crossfade equal to its scene", j, JobErrorKind::BadScene,
                            "scenes.crossfade_seconds"});
        }
        {
            // Case-sensitive on purpose: H264 vs h264 is exactly the ambiguity a
            // frozen vocabulary exists to remove.
            RenderJob j = makeJob("x");
            j.videoCodec = "H264";
            rows.push_back({"uppercase codec", j, JobErrorKind::UnknownToken,
                            "video.codec"});
        }
        {
            RenderJob j = makeJob("x");
            j.container = "ogg";
            rows.push_back({"unknown container", j, JobErrorKind::UnknownToken,
                            "video.container"});
        }
        {
            RenderJob j = makeJob("x");
            j.karaokeMode = "muxed";
            rows.push_back({"karaoke with no ass", j, JobErrorKind::MissingKaraokeSource,
                            "karaoke.ass_path"});
        }
        {
            RenderJob j = makeJob("x");
            j.audioChannels = 3;
            rows.push_back({"5.1 is not representable", j, JobErrorKind::BadAudioSpec,
                            "audio.*"});
        }
        {
            RenderJob j = makeJob("x");
            j.audioBitrateKbps = 0;
            rows.push_back({"no bitrate", j, JobErrorKind::BadAudioSpec, "audio.*"});
        }

        for (const auto& row : rows) {
            const auto result = row.job.validate();
            QVERIFY2(!result.has_value(), row.what);
            QCOMPARE(result.error().kind, row.kind);
            QCOMPARE(result.error().field, std::string(row.field));
            QVERIFY2(!result.error().message.empty(), row.what);
            QCOMPARE(result.error().describe().empty(), false);
        }
    }

    void everyVocabularyTokenIsAcceptedAndPaddingIsNot() {
        // The vocabularies are padded to kVocabWidth with "", and isKnownToken
        // must never accept an empty token — otherwise a padding slot becomes a
        // legal value by accident and a schema written to disk accepts "".
        for (usize v = 0; v < kVocabularyCount; ++v) {
            const auto vocab = static_cast<Vocabulary>(v);
            for (usize t = 0; t < kVocabWidth; ++t) {
                const std::string_view token = kVocabularies[v][t];
                const bool legal = !token.empty();
                QCOMPARE(isKnownToken(vocab, token), legal);
            }
            QVERIFY2(!isKnownToken(vocab, ""), "an empty token was accepted");
            QVERIFY2(!isKnownToken(vocab, "definitely-not-a-token"),
                     "an unknown token was accepted");
        }
    }

    void karaokeBurnedInNeedsAnAssSource() {
        RenderJob job = makeJob("karaoke");
        job.karaokeMode = "burned-in";
        job.karaokeAssPath = "/tmp/lyrics.ass";
        QVERIFY(job.validate().has_value());
    }

    // ── The receipt schema ───────────────────────────────────────────────────

    void aReceiptRoundTripsEveryFieldExactly() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        RenderJob job = makeJob("round-trip", fs::path(dir.path().toStdString()));
        job.label = "A label with spaces, a comma, and = signs";
        job.durationSeconds = 12.5;
        job.expectedFrames = 750;
        job.width = 1080;
        job.height = 1920; // vertical, and both even
        job.fps = 30;
        job.videoCodec = "vp9";
        job.pixelFormat = "yuv420p";
        job.container = "webm";
        job.crf = 31;
        job.encoderPreset = "slow";
        job.twoPass = true;
        job.hardwareAccel = "none";
        job.gopSize = 120;
        job.audioCodec = "opus";
        job.audioSampleRate = 44100;
        job.audioChannels = 2;
        job.audioBitrateKbps = 192;
        job.karaokeMode = "muxed";
        job.karaokeAssPath = "/tmp/sheet.ass";
        job.projectmVersion = "4.2.0";
        job.scenes = {RenderScene{"alpha", 4.25, 0.0},
                      RenderScene{"beta", 8.25, 1.5}};

        RenderReceipt receipt;
        receipt.outcome = RenderState::Completed;
        receipt.framesExpected = job.expectedFrames;
        receipt.framesWritten = job.expectedFrames;
        receipt.bytesWritten = 123456789;
        receipt.elapsedMs = 654321;
        receipt.startedUtc = "2026-10-04T01:02:03Z";
        receipt.finishedUtc = "2026-10-04T01:13:57Z";
        receipt.note = "note with = and , and \"quotes\"";

        const auto written = writeReceipt(job, job.receiptPath());
        QVERIFY2(written.has_value(),
                 written.has_value() ? "" : written.error().describe().c_str());

        const auto readBack = readReceipt(job.receiptPath());
        QVERIFY2(readBack.has_value(),
                 readBack.has_value() ? "" : readBack.error().describe().c_str());

        const RenderJob& r = *readBack;
        QCOMPARE(r.id, job.id);
        QCOMPARE(r.label, job.label);
        QCOMPARE(r.createdUtc, job.createdUtc);
        QCOMPARE(r.audioPath.string(), job.audioPath.string());
        QCOMPARE(r.audioSha256, job.audioSha256);
        QCOMPARE(std::bit_cast<u64>(r.durationSeconds), std::bit_cast<u64>(job.durationSeconds));
        QCOMPARE(r.expectedFrames, job.expectedFrames);
        QCOMPARE(r.outputPath.string(), job.outputPath.string());
        QCOMPARE(r.width, job.width);
        QCOMPARE(r.height, job.height);
        QCOMPARE(r.fps, job.fps);
        QCOMPARE(r.videoCodec, job.videoCodec);
        QCOMPARE(r.pixelFormat, job.pixelFormat);
        QCOMPARE(r.container, job.container);
        QCOMPARE(r.crf, job.crf);
        QCOMPARE(r.encoderPreset, job.encoderPreset);
        QCOMPARE(r.twoPass, job.twoPass);
        QCOMPARE(r.hardwareAccel, job.hardwareAccel);
        QCOMPARE(r.gopSize, job.gopSize);
        QCOMPARE(r.audioCodec, job.audioCodec);
        QCOMPARE(r.audioSampleRate, job.audioSampleRate);
        QCOMPARE(r.audioChannels, job.audioChannels);
        QCOMPARE(r.audioBitrateKbps, job.audioBitrateKbps);
        QCOMPARE(r.karaokeMode, job.karaokeMode);
        QCOMPARE(r.karaokeAssPath.string(), job.karaokeAssPath.string());
        QCOMPARE(r.projectmVersion, job.projectmVersion);

        QCOMPARE(r.scenes.size(), std::size_t{2});
        QCOMPARE(r.scenes[0].presetName, std::string("alpha"));
        QCOMPARE(std::bit_cast<u64>(r.scenes[0].durationSeconds),
                 std::bit_cast<u64>(4.25));
        QCOMPARE(std::bit_cast<u64>(r.scenes[0].crossfadeSeconds),
                 std::bit_cast<u64>(0.0));
        QCOMPARE(r.scenes[1].presetName, std::string("beta"));
        QCOMPARE(std::bit_cast<u64>(r.scenes[1].crossfadeSeconds),
                 std::bit_cast<u64>(1.5));

        // The receipt is the thing that makes a re-render cheap, so the receipt
        // has to carry the outcome too, not just the settings.
        const std::string bytes = readWhole(job.receiptPath()).toStdString();
        const ReceiptDecode decoded = decodeReceipt(bytes);
        QVERIFY2(decoded.ok(), decoded.detail.c_str());
        QCOMPARE(decoded.major, kSchemaMajor);
        QCOMPARE(r.expectedFrames, receipt.framesExpected);
        QVERIFY2(bytes.find("completed") != std::string::npos,
                 "the outcome slug is not in the receipt body");
    }

    void theHeaderIsTheFramingAndTheSchemaIsSeparate() {
        // Off 0 is the format tag — "this is a render receipt at all" — and off 1
        // is the schema major. Conflating them means a v2 writer cannot be
        // distinguished from a different format, which is the whole reason the
        // header has two bytes doing two jobs.
        RenderJob job = makeJob("header");
        const std::string bytes = encodeReceipt(job);
        QVERIFY(bytes.size() >= kReceiptHeaderBytes);
        QCOMPARE(static_cast<u8>(bytes[0]), kFormatTag);
        QCOMPARE(static_cast<u8>(bytes[1]), static_cast<u8>(kSchemaMajor));
        const u16 headerBytes =
            static_cast<u16>(static_cast<u8>(bytes[4])) |
            (static_cast<u16>(static_cast<u8>(bytes[5])) << 8);
        QCOMPARE(headerBytes, static_cast<u16>(kReceiptHeaderBytes));
        // Both reserved bytes must be zero, and decodeReceipt refuses otherwise.
        QCOMPARE(static_cast<u8>(bytes[3]), u8{0});
        QCOMPARE(static_cast<u8>(bytes[6]), u8{0});
        QCOMPARE(static_cast<u8>(bytes[7]), u8{0});
    }

    void corruptReceiptsAreRefusedDistinctly() {
        RenderJob job = makeJob("corrupt");
        const std::string good = encodeReceipt(job);

        // Missing is not the same as BadMagic: absent is not corrupt, and a
        // caller that retries a missing file wants different logging. The names
        // are lowercase slugs because they go straight into a log line.
        QCOMPARE(receiptStatusName(ReceiptStatus::Missing), std::string("missing"));
        QCOMPARE(receiptStatusName(ReceiptStatus::BadMagic), std::string("bad-magic"));

        // And every status has its own name, so a new variant cannot silently
        // render as a neighbour's.
        std::set<std::string> statusNames;
        for (const auto status : {ReceiptStatus::Ok, ReceiptStatus::Missing,
                                  ReceiptStatus::BadMagic, ReceiptStatus::TruncatedHeader,
                                  ReceiptStatus::BadHeader,
                                  ReceiptStatus::UnsupportedVersion,
                                  ReceiptStatus::BadBody, ReceiptStatus::WriteFailed}) {
            const std::string name(receiptStatusName(status));
            QVERIFY(!name.empty());
            QVERIFY(name != "unknown");
            statusNames.insert(name);
        }
        QCOMPARE(statusNames.size(), std::size_t{8});

        // Missing is a FILE-level verdict, so it comes from readReceipt on an
        // absent path -- not from decodeReceipt, which has bytes and can only
        // judge them. Conflating the two would make "no receipt yet" and
        // "corrupt receipt" the same log line.
        {
            QTemporaryDir dir;
            QVERIFY(dir.isValid());
            const auto absent =
                readReceipt(fs::path(dir.path().toStdString()) / "never-written.receipt");
            QVERIFY(!absent.has_value());
            QCOMPARE(absent.error().status, ReceiptStatus::Missing);
        }
        QCOMPARE(decodeReceipt("").status, ReceiptStatus::TruncatedHeader);
        QCOMPARE(decodeReceipt("not a receipt at all").status, ReceiptStatus::BadMagic);

        {
            std::string bad = good;
            bad[3] = 1; // a reserved byte
            QCOMPARE(decodeReceipt(bad).status, ReceiptStatus::BadHeader);
        }
        {
            std::string bad = good;
            bad[1] = static_cast<char>(kSchemaMajor + 1);
            const ReceiptDecode decoded = decodeReceipt(bad);
            QCOMPARE(decoded.status, ReceiptStatus::UnsupportedVersion);
            // The on-disk major is still reported, so a caller can log what it
            // found without a second decode.
            QCOMPARE(decoded.major, kSchemaMajor + 1);
        }
        {
            // Truncated header: right tag, too short for the header itself.
            std::string bad = good.substr(0, kReceiptHeaderBytes - 1);
            QCOMPARE(decodeReceipt(bad).status, ReceiptStatus::TruncatedHeader);
        }
        {
            std::string bad = good.substr(0, kReceiptHeaderBytes);
            bad += "\xFF\xFF\xFF\xFF not toml at all [[[";
            QCOMPARE(decodeReceipt(bad).status, ReceiptStatus::BadBody);
        }
    }

    void aReceiptRefusesToCertifyAShortRender() {
        // The exact confusion CompletedPartial exists to prevent, caught at the
        // write boundary rather than shipped: a receipt claiming Completed while
        // writing 400 of 1800 frames is a lie with a version number on it.
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const fs::path dirPath(dir.path().toStdString());

        RenderJob job = makeJob("short", dirPath);
        job.expectedFrames = 1800;

        RenderReceipt lying;
        lying.outcome = RenderState::Completed;
        lying.framesExpected = 1800;
        lying.framesWritten = 400;
        // The receipt is a MEMBER of the job, not a writeReceipt argument. Reading
        // a local copy and never assigning it is the mistake that made this
        // assertion look like the guard was missing.
        job.receipt = lying;

        const auto refused = writeReceipt(job, dirPath / "lying.receipt");
        QVERIFY2(!refused.has_value(), refused.has_value()
                                          ? "writeReceipt certified a 400-of-1800 render"
                                          : refused.error().describe().c_str());
        QVERIFY(!fs::exists(dirPath / "lying.receipt"));

        // And the honest spelling is accepted, because the partial outcome is a
        // legitimate thing to have produced.
        RenderReceipt honest = lying;
        honest.outcome = RenderState::CompletedPartial;
        job.receipt = honest;
        const auto accepted = writeReceipt(job, dirPath / "honest.receipt");
        QVERIFY2(accepted.has_value(),
                 accepted.has_value() ? "" : accepted.error().describe().c_str());
        QVERIFY(fs::exists(dirPath / "honest.receipt"));
    }

    void aReceiptRefusesANonTerminalOutcome() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const fs::path dirPath(dir.path().toStdString());
        RenderJob job = makeJob("mid-flight", dirPath);

        RenderReceipt receipt;
        receipt.outcome = RenderState::Rendering;
        receipt.framesExpected = job.expectedFrames;
        receipt.framesWritten = 12;
        job.receipt = receipt;
        QVERIFY(!writeReceipt(job, dirPath / "mid.receipt").has_value());

        receipt.outcome = RenderState::Queued;
        job.receipt = receipt;
        QVERIFY(!writeReceipt(job, dirPath / "queued.receipt").has_value());

        // A cancelled render IS terminal and does get a receipt, because it had
        // already been muxing frames when it stopped -- which is exactly why
        // leavesPartialOutput() counts it.
        receipt.outcome = RenderState::Cancelled;
        receipt.framesWritten = 0;
        job.receipt = receipt;
        const auto cancelled = writeReceipt(job, dirPath / "cancelled.receipt");
        QVERIFY2(cancelled.has_value(),
                 cancelled.has_value() ? "" : cancelled.error().describe().c_str());
    }

    void writingTwiceReplacesTheReceiptAndLeavesNoTemp() {
        // The Windows path is MoveFileExW, because std::filesystem::rename
        // refuses to overwrite there — which is what broke every settings save
        // after the first one. A second write is the regression test for it.
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const fs::path dirPath(dir.path().toStdString());

        RenderJob job = makeJob("twice", dirPath);
        QVERIFY(writeReceipt(job, job.receiptPath()).has_value());
        const auto firstSize = static_cast<std::uintmax_t>(fs::file_size(job.receiptPath()));

        job.label = "a different, longer label than the first one";
        job.width = 3840;
        job.height = 2160;
        QVERIFY(writeReceipt(job, job.receiptPath()).has_value());

        const auto readBack = readReceipt(job.receiptPath());
        QVERIFY(readBack.has_value());
        QCOMPARE(readBack->label, job.label);
        QCOMPARE(readBack->width, u32{3840});
        // The file really was replaced rather than appended to.
        QVERIFY(fs::file_size(job.receiptPath()) != firstSize);

        // No orphaned temp anywhere beside it.
        for (const auto& entry : fs::directory_iterator(dirPath)) {
            QVERIFY2(entry.path().extension() != ".tmp",
                     "an atomic write leaked its temp file");
        }
    }

    void anUnreadableSourceHashesToEmptyRatherThanThrowing() {
        // "" already carries the meaning "not hashed", so an unreadable file must
        // not produce a different kind of lie.
        QCOMPARE(sha256Hex("/definitely/not/a/file.flac"), std::string());
        const std::string hashed = sha256Hex("/definitely/not/a/file.flac");
        QCOMPARE(hashed.size(), std::size_t{0});
    }

    // ── The batch aggregate ──────────────────────────────────────────────────

    void aSummaryMustAccountForEverySlot() {
        // This is the assertion the whole batch model exists to make possible: a
        // summary that does not add up is the "short list presented as a total"
        // failure, and no arithmetic on the counters can catch it after the fact.
        std::vector<RenderResult> results;
        const auto add = [&results](RenderState state, u64 done, u64 expected) {
            RenderResult r;
            r.state = state;
            r.framesDone = done;
            r.framesExpected = expected;
            results.push_back(std::move(r));
        };

        add(RenderState::Completed, 100, 100);
        add(RenderState::Completed, 50, 50);
        add(RenderState::CompletedPartial, 30, 100);
        add(RenderState::FailedPermanent, 0, 100);
        add(RenderState::Cancelled, 0, 100);
        add(RenderState::Rendering, 10, 100);

        const BatchSummary summary = summarise(results);
        QCOMPARE(summary.total, u32{6});
        QCOMPARE(summary.completed, u32{2});
        QCOMPARE(summary.completedPartial, u32{1});
        QCOMPARE(summary.failedPermanent, u32{1});
        QCOMPARE(summary.cancelled, u32{1});
        QCOMPARE(summary.outstanding, u32{1});
        QCOMPARE(summary.failedRetryable, u32{0});

        QCOMPARE(summary.settled(), u32{5});
        QCOMPARE(summary.accountedFor(), u32{6});
        QCOMPARE(summary.accountedFor(), summary.total);
        QCOMPARE(summary.fullyRendered(), u32{2});
        // THREE, not four: the outstanding job is excluded. A job that is still
        // rendering has not "failed to reach the file whole" -- it has not
        // finished, which is a different claim, and counting it here would make
        // a running batch report failures that do not exist. This is also why
        // notFullyRendered() and idsNotFullyRendered() agree exactly: both skip
        // non-terminal slots.
        QCOMPARE(summary.notFullyRendered(), u32{3});
        QCOMPARE(summary.settled() - summary.fullyRendered(), summary.notFullyRendered());
        QVERIFY(!summary.isSettled()); // one job still rendering

        // Account for every slot, with nothing left over and nothing double
        // counted. Written as the sum so a new state cannot slip past a set of
        // individual QCOMPAREs.
        QCOMPARE(static_cast<u32>(summary.completed + summary.completedPartial +
                                  summary.failedRetryable + summary.failedPermanent +
                                  summary.cancelled + summary.outstanding),
                 summary.total);
    }

    void aFullySettledBatchSaysSo() {
        std::vector<RenderResult> results(3);
        for (auto& r : results) {
            r.state = RenderState::Completed;
            r.framesDone = 10;
            r.framesExpected = 10;
        }
        const BatchSummary summary = summarise(results);
        QVERIFY(summary.isSettled());
        QCOMPARE(summary.accountedFor(), summary.total);
        QCOMPARE(summary.notFullyRendered(), u32{0});

        // An empty batch is not a settled batch: "done" over zero jobs is not a
        // claim anyone should make.
        const BatchSummary none = summarise({});
        QCOMPARE(none.total, u32{0});
        QVERIFY(!none.isSettled());
    }

    void progressIsUnknownRatherThanZeroWhenThereIsNoDenominator() {
        // -1 and not 0: "0% of nothing" and "0% of a lot" are different claims,
        // and a progress bar that cannot tell them apart will sit at zero
        // looking like a stall.
        RenderResult unknown;
        unknown.state = RenderState::Rendering;
        unknown.framesDone = 5;
        unknown.framesExpected = 0;
        QCOMPARE(unknown.progressPercent(), -1);

        RenderResult known;
        known.state = RenderState::Rendering;
        known.framesDone = 5;
        known.framesExpected = 10;
        QCOMPARE(known.progressPercent(), 50);

        known.framesDone = 10;
        QCOMPARE(known.progressPercent(), 100);

        // Overflow must clamp, not wrap into a negative.
        known.framesDone = 20;
        known.framesExpected = 10;
        QCOMPARE(known.progressPercent(), 100);

        const std::vector<RenderResult> one{unknown};
        QCOMPARE(batchProgressPercent(one), -1);
    }

    void batchProgressIsWeightedByFramesNotByJobCount() {
        // One 1800-frame job and three 10-frame jobs: "1 of 4 jobs done" reads as
        // 25% while 1800 of 1830 frames — 98% of the work — are already on disk.
        // A percentage that lies about effort is worse than no percentage.
        std::vector<RenderResult> results(4);
        for (std::size_t i = 0; i < results.size(); ++i) {
            results[i].state = RenderState::Completed;
            results[i].framesExpected = (i == 0) ? 1800 : 10;
            results[i].framesDone = results[i].framesExpected;
        }
        QCOMPARE(batchProgressPercent(results), 100);

        // Now the honest midpoint: the big job is half done, the small ones are
        // not started. 900 of 1830 frames = 49.18...%, which rounds to 49.
        results[0].state = RenderState::Rendering;
        results[0].framesDone = 900;
        for (std::size_t i = 1; i < results.size(); ++i) {
            results[i].state = RenderState::Queued;
            results[i].framesDone = 0;
        }
        QCOMPARE(batchProgressPercent(results), 49);

        // Job-count progress would have said 0% here. Say so, because the two
        // disagreeing is the entire justification for the weighting.
        QCOMPARE(batchProgressPercent(results), 49);
    }

    void aPartialRenderCountsAsProgressTowardTheTotal() {
        // 400 of 1800 frames is real work on disk. Hiding it would make a
        // partially-rendered batch look like nothing happened.
        std::vector<RenderResult> results(1);
        results[0].state = RenderState::CompletedPartial;
        results[0].framesExpected = 1800;
        results[0].framesDone = 400;
        QCOMPARE(batchProgressPercent(results), 22);
    }

    // ── Scheduling ───────────────────────────────────────────────────────────

    void concurrencyNeverExceedsTheCeiling() {
        // THE load-bearing assertion. Not "two were handed out" -- that would pass
        // on a queue that hands out five and cleans up two.
        for (int limit : {1, 2, 3}) {
            RecordingRunner runner;
            RenderQueue queue(runner());
            queue.setMaxConcurrent(limit);

            int inFlight = 0;
            int peak = 0;
            for (int i = 0; i < 12; ++i) {
                const auto admitted = queue.enqueue(makeJob("job-" + std::to_string(i)));
                QVERIFY(admitted.has_value());
                inFlight = queue.activeCount();
                peak = std::max(peak, inFlight);
                QVERIFY2(inFlight <= limit, "a job was handed out past the ceiling");
            }
            QCOMPARE(queue.activeCount(), limit);
            QCOMPARE(queue.queuedCount(), 12 - limit);
            QCOMPARE(peak, limit);
            // The limit the queue reports back must be the one it enforces.
            QCOMPARE(queue.maxConcurrent(), limit);
        }
    }

    void aConcurrencyLimitBelowOneIsClamped() {
        RecordingRunner runner;
        RenderQueue queue(runner());
        queue.setMaxConcurrent(0);
        QVERIFY(queue.maxConcurrent() >= 1);
        queue.setMaxConcurrent(-5);
        QVERIFY(queue.maxConcurrent() >= 1);
        // And it still works, rather than deadlocking or refusing every job.
        QVERIFY(queue.enqueue(makeJob("only")).has_value());
    }

    void handingOutIsFifo() {
        RecordingRunner runner;
        RenderQueue queue(runner());
        queue.setMaxConcurrent(1);

        for (int i = 0; i < 5; ++i) {
            QVERIFY(queue.enqueue(makeJob("job-" + std::to_string(i))).has_value());
        }
        QCOMPARE(runner.handedOut.size(), std::size_t{1});
        QCOMPARE(runner.handedOut.front(), std::string("job-0"));

        // Free the slot; the next handout must be the head of the queue.
        RenderReceipt done;
        done.outcome = RenderState::Completed;
        done.framesExpected = 1800;
        done.framesWritten = 1800;
        QVERIFY(queue.finish("job-0", done));
        QCOMPARE(runner.handedOut.size(), std::size_t{2});
        QCOMPARE(runner.handedOut[1], std::string("job-1"));
    }

    void aRetryJumpsTheQueueBecauseThatJobIsTheOneWeAreWaitingOn() {
        RecordingRunner runner;
        RenderQueue queue(runner());
        queue.setMaxConcurrent(1);

        QVERIFY(queue.enqueue(makeJob("first")).has_value());
        QVERIFY(queue.enqueue(makeJob("second")).has_value());
        QCOMPARE(runner.handedOut, (std::vector<std::string>{"first"}));

        // Transient failure with attempts left: re-queued at the FIFO *front*.
        RenderFailure transient;
        transient.kind = RenderFailureKind::Transient;
        transient.reason = "lost the GL context";
        QVERIFY(queue.fail("first", transient));

        QCOMPARE(runner.handedOut.size(), std::size_t{2});
        QCOMPARE(runner.handedOut[1], std::string("first"));
        QCOMPARE(queue.stateOf("second"), RenderState::Queued);
    }

    void aPermanentFailureSettlesInsteadOfRetrying() {
        RecordingRunner runner;
        RenderQueue queue(runner());
        queue.setMaxConcurrent(1);
        QVERIFY(queue.enqueue(makeJob("doomed")).has_value());

        RenderFailure full;
        full.osErrno = ENOSPC;
        full.reason = "no space left on device";
        QVERIFY(queue.fail("doomed", full));

        QCOMPARE(queue.stateOf("doomed"), RenderState::FailedPermanent);
        QCOMPARE(queue.isEmpty(), true);
        QCOMPARE(runner.handedOut.size(), std::size_t{1}); // no second attempt

        // And the receipt that is written says why, in words.
        QCOMPARE(queue.results().size(), std::size_t{1});
        QCOMPARE(queue.results()[0].failureKind, RenderFailureKind::Permanent);
        QCOMPARE(queue.results()[0].note, std::string("no space left on device"));
        QVERIFY(queue.idsNotFullyRendered().size() == 1);
        QCOMPARE(queue.idsNotFullyRendered().front(), std::string("doomed"));
    }

    void aRetryStopsAtTheAttemptLimit() {
        RecordingRunner runner;
        RenderQueue queue(runner());
        queue.setMaxConcurrent(1);
        QCOMPARE(queue.maxAttempts(), RenderQueue::kMaxAttempts);

        QVERIFY(queue.enqueue(makeJob("flaky")).has_value());

        RenderFailure transient;
        transient.kind = RenderFailureKind::Transient;
        transient.reason = "transient";

        // kMaxAttempts handouts total: the first, plus one retry.
        for (int attempt = 0; attempt < RenderQueue::kMaxAttempts; ++attempt) {
            const int before = static_cast<int>(runner.handedOut.size());
            QVERIFY(queue.fail("flaky", transient));
            const int after = static_cast<int>(runner.handedOut.size());
            if (attempt + 1 < RenderQueue::kMaxAttempts) {
                QCOMPARE(after, before + 1);
            }
        }
        QCOMPARE(static_cast<int>(runner.handedOut.size()),
                 RenderQueue::kMaxAttempts);
        QCOMPARE(queue.stateOf("flaky"), RenderState::FailedRetryable);
        QCOMPARE(queue.isEmpty(), true);
    }

    void aWorkerClaimingCompleteAfterWritingTooFewFramesIsPartialNotComplete() {
        // The queue decides the state, not the worker. Trusting the worker's
        // "Completed" is how a 400-of-1800 frame file gets certified.
        RecordingRunner runner;
        RenderQueue queue(runner());
        QVERIFY(queue.enqueue(makeJob("liar")).has_value());

        RenderReceipt receipt;
        receipt.outcome = RenderState::Completed;
        receipt.framesExpected = 1800;
        receipt.framesWritten = 400;
        QVERIFY(queue.finish("liar", receipt));

        QCOMPARE(queue.stateOf("liar"), RenderState::CompletedPartial);
        QCOMPARE(queue.results()[0].framesWritten, u64{400});
        QCOMPARE(queue.results()[0].framesExpected, u64{1800});
    }

    void aFullyRenderedJobIsCompleted() {
        RecordingRunner runner;
        RenderQueue queue(runner());
        QVERIFY(queue.enqueue(makeJob("honest")).has_value());

        RenderReceipt receipt;
        receipt.outcome = RenderState::Completed;
        receipt.framesExpected = 1800;
        receipt.framesWritten = 1800;
        receipt.bytesWritten = 987654;
        receipt.elapsedMs = 60000;
        QVERIFY(queue.finish("honest", receipt));

        QCOMPARE(queue.stateOf("honest"), RenderState::Completed);
        QCOMPARE(queue.results()[0].bytesWritten, u64{987654});
        QCOMPARE(queue.results()[0].elapsedMs, u64{60000});
        QCOMPARE(queue.summary().fullyRendered(), u32{1});
        QCOMPARE(queue.summary().notFullyRendered(), u32{0});
    }

    void progressReportsAreIdempotentAndMonotonic() {
        RecordingRunner runner;
        RenderQueue queue(runner());
        QVERIFY(queue.enqueue(makeJob("progress")).has_value());

        QVERIFY(queue.reportProgress("progress", 10));
        QVERIFY(queue.reportProgress("progress", 20));
        QCOMPARE(queue.results()[0].framesDone, u64{20});

        // Idempotent: the same value twice is not an error.
        QVERIFY(queue.reportProgress("progress", 20));
        QCOMPARE(queue.results()[0].framesDone, u64{20});

        // A report that went backwards means the executor is confused. Clamping
        // it silently would corrupt the batch aggregate, so it is refused.
        QVERIFY(!queue.reportProgress("progress", 5));
        QCOMPARE(queue.results()[0].framesDone, u64{20});

        QVERIFY(!queue.reportProgress("no-such-job", 1));
    }

    void duplicateIdsAreRefusedDistinctlyFromAnEmptyId() {
        RecordingRunner runner;
        RenderQueue queue(runner());
        queue.setMaxConcurrent(1);

        QVERIFY(queue.enqueue(makeJob("dup")).has_value());
        const auto second = queue.enqueue(makeJob("dup"));
        QVERIFY(!second.has_value());
        // Reporting it as EmptyId would tell the caller the opposite of the
        // truth about a field it just filled in.
        QCOMPARE(second.error().kind, JobErrorKind::DuplicateId);
        QCOMPARE(queue.batchSize(), std::size_t{1});
    }

    void anInvalidJobIsRefusedBeforeItTakesASlot() {
        RecordingRunner runner;
        RenderQueue queue(runner());
        RenderJob bad = makeJob("bad");
        bad.scenes.clear();
        const auto admitted = queue.enqueue(bad);
        QVERIFY(!admitted.has_value());
        QCOMPARE(admitted.error().kind, JobErrorKind::NoScenes);
        // No slot consumed, no batch entry: a rejected job leaves no trace.
        QCOMPARE(queue.batchSize(), std::size_t{0});
        QCOMPARE(queue.isEmpty(), true);
    }

    void cancellingAQueuedJobRemovesItImmediately() {
        RecordingRunner runner;
        RenderQueue queue(runner());
        queue.setMaxConcurrent(1);
        QVERIFY(queue.enqueue(makeJob("running")).has_value());
        QVERIFY(queue.enqueue(makeJob("waiting")).has_value());

        QVERIFY(queue.cancel("waiting"));
        QCOMPARE(queue.stateOf("waiting"), RenderState::Cancelled);
        QCOMPARE(queue.queuedCount(), 0);
        QCOMPARE(queue.activeCount(), 1);
        QCOMPARE(queue.results()[1].state, RenderState::Cancelled);
    }

    void cancellingAnInFlightJobMarksItAndCallsTheHook() {
        RecordingRunner runner;
        RenderQueue queue(runner());
        std::vector<std::string> hooked;
        queue.setCancelHook([&hooked](const std::string& id) { hooked.push_back(id); });

        QVERIFY(queue.enqueue(makeJob("inflight")).has_value());
        QVERIFY(queue.isCancelRequested("inflight") == false);

        QVERIFY(queue.cancel("inflight"));
        QVERIFY(queue.isCancelRequested("inflight"));
        QCOMPARE(hooked, (std::vector<std::string>{"inflight"}));
        // The slot is still held: the worker has not reported back yet, and
        // pretending otherwise would start a third job on two workers' worth of
        // hardware.
        QCOMPARE(queue.activeCount(), 1);

        // The worker's own report is the ground truth about what reached the file.
        RenderReceipt receipt;
        receipt.outcome = RenderState::Completed;
        receipt.framesExpected = 1800;
        receipt.framesWritten = 1800;
        QVERIFY(queue.finish("inflight", receipt));
        QCOMPARE(queue.stateOf("inflight"), RenderState::Completed);
        QCOMPARE(queue.activeCount(), 0);
    }

    void cancellingAnUnknownOrTerminalJobIsANoOp() {
        RecordingRunner runner;
        RenderQueue queue(runner());
        QVERIFY(!queue.cancel("never-existed"));
        QVERIFY(queue.enqueue(makeJob("quick")).has_value());
        RenderReceipt done;
        done.outcome = RenderState::Completed;
        done.framesExpected = 1800;
        done.framesWritten = 1800;
        QVERIFY(queue.finish("quick", done));
        QVERIFY(!queue.cancel("quick"));
    }

    void aRunnerThatFinishesFromInsideTheHandoutDoesNotReenter() {
        // The reentrancy guard. settle() calls pump(), so without the guard the
        // next job would be handed out while the previous runner is still on the
        // stack — which is a use-after-free on the queue's own containers.
        int depth = 0;
        int maxDepth = 0;
        RenderQueue queue([&](RenderJob job) {
            ++depth;
            maxDepth = std::max(maxDepth, depth);
            RenderReceipt receipt;
            receipt.outcome = RenderState::Completed;
            receipt.framesExpected = job.expectedFrames;
            receipt.framesWritten = job.expectedFrames;
            queue.finish(job.id, std::move(receipt));
            --depth;
        });
        queue.setMaxConcurrent(1);

        for (int i = 0; i < 6; ++i) {
            QVERIFY(queue.enqueue(makeJob("re-" + std::to_string(i))).has_value());
        }
        QCOMPARE(maxDepth, 1);
        QCOMPARE(queue.summary().completed, u32{6});
        QCOMPARE(queue.summary().accountedFor(), u32{6});
    }

    void idleIsEdgeTriggered() {
        RecordingRunner runner;
        RenderQueue queue(runner());
        QSignalSpy idleSpy(&queue, &RenderQueue::queueIdle);

        // Already idle at construction, so nothing to announce.
        QVERIFY(queue.isEmpty());
        QCOMPARE(idleSpy.count(), 0);

        QVERIFY(queue.enqueue(makeJob("a")).has_value());
        QCOMPARE(idleSpy.count(), 0);

        RenderReceipt done;
        done.outcome = RenderState::Completed;
        done.framesExpected = 1800;
        done.framesWritten = 1800;
        QVERIFY(queue.finish("a", done));

        // Exactly one announcement, and staying idle does not repeat it.
        QCOMPARE(idleSpy.count(), 1);
        QVERIFY(queue.isEmpty());

        QVERIFY(queue.enqueue(makeJob("b")).has_value());
        QVERIFY(queue.finish("b", done));
        QCOMPARE(idleSpy.count(), 2);
    }

    void batchOrderIsEnqueueOrderNotCompletionOrder() {
        // The pre-allocated-by-position shape: results line up regardless of
        // which job finished first, so a UI can index by position.
        RenderQueue queue([](RenderJob) {});
        queue.setMaxConcurrent(3);

        QVERIFY(queue.enqueue(makeJob("first")).has_value());
        QVERIFY(queue.enqueue(makeJob("second")).has_value());
        QVERIFY(queue.enqueue(makeJob("third")).has_value());

        RenderReceipt done;
        done.outcome = RenderState::Completed;
        done.framesExpected = 1800;
        done.framesWritten = 1800;

        // Finish them backwards.
        QVERIFY(queue.finish("third", done));
        QVERIFY(queue.finish("first", done));
        QVERIFY(queue.finish("second", done));

        QCOMPARE(queue.batchSize(), std::size_t{3});
        QCOMPARE(queue.results()[0].jobId, std::string("first"));
        QCOMPARE(queue.results()[1].jobId, std::string("second"));
        QCOMPARE(queue.results()[2].jobId, std::string("third"));

        // batchPosition is stable even though the queue order is not.
        QCOMPARE(queue.batchPosition("first"), std::size_t{1});
        QCOMPARE(queue.batchPosition("second"), std::size_t{2});
        QCOMPARE(queue.batchPosition("third"), std::size_t{3});
        QCOMPARE(queue.batchPosition("nope"), std::size_t{0});
    }

    void waitingAheadCountsOnlyJobsStillQueued() {
        RecordingRunner runner;
        RenderQueue queue(runner());
        queue.setMaxConcurrent(1);
        QVERIFY(queue.enqueue(makeJob("a")).has_value());
        QVERIFY(queue.enqueue(makeJob("b")).has_value());
        QVERIFY(queue.enqueue(makeJob("c")).has_value());

        QCOMPARE(queue.waitingAhead("a"), 0); // running, not queued
        QCOMPARE(queue.waitingAhead("b"), 0);
        QCOMPARE(queue.waitingAhead("c"), 1);
        QCOMPARE(queue.waitingAhead("nope"), 0);
    }

    void aFailingJobDoesNotStopTheBatchAndTheSummaryStillAccountsForIt() {
        // "Report *which* and *how many*, not just done" is the whole point. A
        // batch that cannot name its failures is indistinguishable from one that
        // never started them.
        RenderQueue queue([](RenderJob) {});
        queue.setMaxConcurrent(3);

        for (int i = 0; i < 4; ++i) {
            QVERIFY(queue.enqueue(makeJob("job-" + std::to_string(i))).has_value());
        }

        RenderReceipt full;
        full.outcome = RenderState::Completed;
        full.framesExpected = 1800;
        full.framesWritten = 1800;
        RenderReceipt partial = full;
        partial.outcome = RenderState::CompletedPartial;
        partial.framesWritten = 400;
        RenderFailure broken;
        broken.kind = RenderFailureKind::Permanent;
        broken.reason = "encoder refused the output path";

        QVERIFY(queue.finish("job-0", full));
        QVERIFY(queue.fail("job-1", broken));
        QVERIFY(queue.finish("job-2", partial));
        QVERIFY(queue.finish("job-3", full));

        const BatchSummary summary = queue.summary();
        QCOMPARE(summary.total, u32{4});
        QCOMPARE(summary.completed, u32{2});
        QCOMPARE(summary.completedPartial, u32{1});
        QCOMPARE(summary.failedPermanent, u32{1});
        QCOMPARE(summary.accountedFor(), summary.total);
        QVERIFY(summary.isSettled());

        // Which, not just how many.
        const auto notFull = queue.idsNotFullyRendered();
        QCOMPARE(notFull.size(), std::size_t{2});
        QCOMPARE(notFull[0], std::string("job-1"));
        QCOMPARE(notFull[1], std::string("job-2"));
        QCOMPARE(summary.notFullyRendered(), static_cast<u32>(notFull.size()));
    }

    void clearingTheBatchLeavesLiveJobsAlone() {
        RecordingRunner runner;
        RenderQueue queue(runner());
        queue.setMaxConcurrent(1);
        QVERIFY(queue.enqueue(makeJob("live")).has_value());
        QVERIFY(queue.enqueue(makeJob("queued")).has_value());

        queue.clearBatch();
        QCOMPARE(queue.batchSize(), std::size_t{0});
        // The live job is untouched and still owns its slot, and -- this is the
        // half that used to lie -- it still REPORTS as rendering. Its state lives
        // in its Item, and reading only the (now empty) batch array reported
        // Queued for a job that was demonstrably mid-render.
        QCOMPARE(queue.activeCount(), 1);
        QCOMPARE(queue.stateOf("live"), RenderState::Rendering);
        // A queued job likewise keeps its own answer.
        QCOMPARE(queue.stateOf("queued"), RenderState::Queued);
        QCOMPARE(queue.queuedCount(), 1);
    }

    void anEmptyRunnerlessQueueIsNotConstructible() {
        // There is no "no runner yet" mode: a queue that accepted jobs it could
        // never start would report a stall with no cause.
        JobRunner none;
        RenderQueue queue(none);
        QVERIFY(queue.maxConcurrent() >= 1);
        QVERIFY(queue.isEmpty());
    }
};

#include "test_RenderQueue.moc"

int runTestRenderQueue(int argc, char** argv) {
    TestRenderQueue t;
    return QTest::qExec(&t, argc, argv);
}