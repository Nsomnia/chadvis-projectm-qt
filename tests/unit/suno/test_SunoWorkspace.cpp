// TestSunoWorkspace — the honesty suite for the local creation workspace.
//
// The class was on the backlog because 6 of its 12 signals had no emitter, and
// registering its bridge would have shipped a Generate button that spun forever
// and a Render button pinned at 0%. Three tests, each aimed at one way that could
// come back:
//
//   1. What it does not support is REFUSED WITH A NAME, and nothing is half-started
//      on the way out.
//   2. Every admitted render settles, and only `Completed` counts as a completion.
//   3. Progress never goes backwards, and "unknown" is never rendered as 0%.
//
// No GL, no FFmpeg, no clock, no thread. The render half is driven through
// `RenderQueue`'s own injected `JobRunner` seam, which is exactly why that queue
// takes its work as a handout — a test that had to spin up a real render would be
// testing the render, and the render is the part that has no GL backend yet.
//
// Every assertion is an exact value or an exact string, for the reason
// test_RenderQueue.cpp documents: "is non-zero" and "roughly 2s" have both already
// let real bugs through this codebase.

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include "recorder/RenderJob.hpp"
#include "recorder/RenderQueue.hpp"
#include "suno/SunoWorkspace.hpp"

#include <cstddef>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

using namespace vc;
using namespace vc::suno;

namespace {

namespace fs = std::filesystem;

/// A job that passes `validate()` with nothing left at its defaults, so a test only
/// states the one field it is about.
RenderJob makeJob(std::string id, const fs::path& dir) {
    RenderJob job;
    job.id = std::move(id);
    job.label = "job " + job.id;
    job.createdUtc = "2026-10-05T00:00:00Z";
    job.audioPath = dir / (job.id + ".flac");
    job.durationSeconds = 30.0;
    job.expectedFrames = 1800;
    job.outputPath = dir / (job.id + ".mp4");
    job.projectmVersion = "4.1.6";
    job.scenes = {RenderScene{"preset-a", 15.0, 0.0}, RenderScene{"preset-b", 15.0, 2.5}};
    return job;
}

/// Every `renderProgress` percent, in order. Monotonicity is checked over this, and
/// it is the only place a fabricated 0% could hide.
std::vector<int> percents(const QSignalSpy& spy) {
    std::vector<int> out;
    out.reserve(static_cast<std::size_t>(spy.count()));
    for (int i = 0; i < spy.count(); ++i)
        out.push_back(spy.at(i).at(0).toInt());
    return out;
}

/// Exact element comparison rather than `QCOMPARE` on the container, so a mismatch
/// names the index and both values instead of two 100-character dumps.
void expectPercents(const std::vector<int>& seen, const std::vector<int>& expected) {
    QCOMPARE(seen.size(), expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i) {
        QVERIFY2(seen[i] == expected[i], qPrintable(QString("progress[%1] was %2, expected %3")
                                                            .arg(i)
                                                            .arg(seen[i])
                                                            .arg(expected[i])));
    }
}

/// The first string argument of every emission, for a failure signal asserted
/// exactly. Indices out of range would abort rather than fail, so the count is
/// always checked first.
std::vector<std::string> messages(const QSignalSpy& spy) {
    std::vector<std::string> out;
    out.reserve(static_cast<std::size_t>(spy.count()));
    for (int i = 0; i < spy.count(); ++i)
        out.push_back(spy.at(i).at(0).value<std::string>());
    return out;
}

} // namespace

class TestSunoWorkspace : public QObject {
    Q_OBJECT

private slots:

    // ── 1. Refusals are named, and nothing is half-started ──────────────────
    void unsupportedThingsAreRefusedWithANameAndStartNothing() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const fs::path dir = fs::path(temp.path().toStdString());

        SunoWorkspace ws;
        QSignalSpy started(&ws, &SunoWorkspace::renderStarted);
        QSignalSpy failed(&ws, &SunoWorkspace::renderFailed);

        // (a) Generation. `SunoClient` has no generation method and the captcha
        // decision is human, so this must refuse rather than set a flag.
        const auto generation = ws.startGeneration("a cold morning", "dreamy", true, "chirp-v3.5");
        QVERIFY(!generation.has_value());
        QCOMPARE(generation.error().kind, WorkspaceRefusalKind::GenerationUnavailable);
        QCOMPARE(generation.error().subject, std::string("POST /api/gen"));
        QVERIFY2(!generation.error().message.empty(),
                 "a refusal with no reason is the same as a silent no-op");
        // The authored text is real local state and survives the refusal.
        QCOMPARE(ws.state().currentPrompt, std::string("a cold morning"));
        QCOMPARE(ws.state().currentTags, std::string("dreamy"));
        QCOMPARE(ws.state().currentModel, std::string("chirp-v3.5"));
        QCOMPARE(ws.state().makeInstrumental, true);
        QVERIFY(!ws.state().lastError.empty());

        // (b) No queue. Accepting a job with nowhere to schedule it is how the old
        // class reported a render that began and never ended.
        const auto noQueue = ws.startRender(makeJob("no-queue", dir));
        QVERIFY(!noQueue.has_value());
        QCOMPARE(noQueue.error().kind, WorkspaceRefusalKind::NoQueueAttached);
        QCOMPARE(noQueue.error().subject, std::string("render queue"));
        QVERIFY(!ws.isRendering());
        QCOMPARE(ws.renderProgressPercent(), -1);
        QCOMPARE(started.count(), 0);
        QCOMPARE(failed.count(), 0);

        // (c) A malformed job is refused BEFORE it is announced, and the refusal
        // names the exact field `RenderJob::validate()` rejected.
        RenderQueue idle([](RenderJob) {});
        ws.setQueue(&idle);
        QCOMPARE(ws.queue(), &idle);
        RenderJob malformed = makeJob("malformed", dir);
        malformed.label.clear(); // -> JobErrorKind::EmptyLabel on "job.label"
        const auto rejected = ws.startRender(std::move(malformed));
        QVERIFY(!rejected.has_value());
        QCOMPARE(rejected.error().kind, WorkspaceRefusalKind::JobRejected);
        QCOMPARE(rejected.error().subject, std::string("job.label"));
        QVERIFY(!ws.isRendering());
        // `renderStarted` means a render IS live, so a job that never got that far
        // must not have emitted it.
        QCOMPARE(started.count(), 0);
        QCOMPARE(failed.count(), 0);

        // (d) The one rejection `validate()` cannot see — a duplicate id — happens
        // after the render was live, so it must be reported as a failure rather
        // than dropped. Dropping it is precisely how a flag is stranded as
        // Rendering with nothing left to clear it. The first job settles, because
        // the workspace renders one at a time and otherwise a duplicate could never
        // reach the queue at all.
        RenderQueue* once = nullptr;
        RenderQueue completing([&](RenderJob job) {
            RenderReceipt receipt;
            receipt.outcome = RenderState::Completed;
            receipt.framesExpected = job.expectedFrames;
            receipt.framesWritten = job.expectedFrames;
            once->finish(job.id, std::move(receipt));
        });
        once = &completing;
        SunoWorkspace dupes;
        dupes.setQueue(&completing);
        QSignalSpy dupFailed(&dupes, &SunoWorkspace::renderFailed);
        QSignalSpy dupDone(&dupes, &SunoWorkspace::renderCompleted);

        QVERIFY(dupes.startRender(makeJob("dup", dir)).has_value());
        QCOMPARE(dupDone.count(), 1);
        QVERIFY(!dupes.isRendering());

        QVERIFY(dupFailed.count() == 0);
        const auto again = dupes.startRender(makeJob("dup", dir));
        QVERIFY(!again.has_value());
        QCOMPARE(again.error().kind, WorkspaceRefusalKind::JobRejected);
        QCOMPARE(again.error().subject, std::string("job.id"));
        QCOMPARE(messages(dupFailed),
                 (std::vector<std::string>{"duplicate-id on 'job.id': a job with this id is "
                                           "already in the batch"}));
        QVERIFY(!dupes.isRendering());
        QCOMPARE(dupDone.count(), 1);
    }

    // ── 2. Every admitted render settles ────────────────────────────────────
    void everyAdmittedRenderSettlesAndOnlyCompletionCounts() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const fs::path dir = fs::path(temp.path().toStdString());

        // (a) Completed. The runner is a plain lambda handed a job by value, which
        // is `RenderQueue`'s contract and the reason nothing here needs a thread.
        RenderQueue* completing = nullptr;
        RenderQueue good([&](RenderJob job) {
            completing->reportProgress(job.id, 900);
            RenderReceipt receipt;
            receipt.outcome = RenderState::Completed;
            receipt.framesExpected = job.expectedFrames;
            receipt.framesWritten = job.expectedFrames;
            receipt.bytesWritten = 4096;
            completing->finish(job.id, std::move(receipt));
        });
        completing = &good;
        SunoWorkspace ws;
        ws.setQueue(&good);
        QSignalSpy started(&ws, &SunoWorkspace::renderStarted);
        QSignalSpy progress(&ws, &SunoWorkspace::renderProgress);
        QSignalSpy completed(&ws, &SunoWorkspace::renderCompleted);
        QSignalSpy failed(&ws, &SunoWorkspace::renderFailed);

        QVERIFY(ws.startRender(makeJob("full", dir)).has_value());
        QCOMPARE(started.count(), 1);
        // value<std::string>(), NOT toString(). Measured: the argument arrives as a
        // QVariant of typeId 65554 (a user type, so std::string is registered
        // dynamically rather than as a built-in), `canConvert<std::string>()` is
        // true, `value<std::string>()` returns "full" -- and `toString()` returns
        // "" because that variant is not QString-convertible. The COUNT is right
        // either way, so this reads exactly like a product bug emitting an empty job
        // id. It is not: the id really is "full", verified independently by printing
        // it at the emission site in SunoWorkspace::startRender.
        QCOMPARE(started.at(0).at(0).typeId() >= static_cast<int>(QMetaType::User), true);
        QVERIFY(started.at(0).at(0).canConvert<std::string>());
        QCOMPARE(started.at(0).at(0).value<std::string>(), std::string("full"));
        QCOMPARE(completed.count(), 1);
        QCOMPARE(failed.count(), 0);
        // Same QVariant reason as the renderStarted assertion: renderCompleted takes
        // an fs::path, which is neither QString- nor char*-convertible, so
        // toString() is empty and value<std::filesystem::path>() is the accessor.
        QCOMPARE(completed.at(0).at(0).value<fs::path>(), dir / "full.mp4");
        // The verdict arrived inside `startRender`, because `enqueue` pumps.
        QVERIFY(!ws.isRendering());
        QVERIFY(!ws.activeRenderJobId().has_value());
        QVERIFY(ws.state().lastError.empty());

        // Exactly the queue's emissions, in order: the admission emit, the state
        // change to rendering, `runJob`'s opening progress report, the worker's own
        // 900-of-1800, and the settle. Nothing is added and nothing is dropped.
        expectPercents(percents(progress), (std::vector<int>{0, 0, 0, 50, 100}));
        // The stage is the queue's own state name, never a description invented here.
        QCOMPARE(progress.at(0).at(1).value<std::string>(), std::string("queued"));
        QCOMPARE(progress.at(progress.count() - 1).at(1).value<std::string>(),
                 std::string("completed"));
        QCOMPARE(ws.renderProgressPercent(), 100);

        // (b) The only terminal state that is NOT a completion, carrying the queue's
        // own reason. This is the path this machine actually takes:
        // `RenderExecutor::jobRunner` fails a job outright when there is no
        // `RenderFrameBackend`, and there is no backend in the tree.
        RenderQueue* refusing = nullptr;
        RenderQueue noBackend([&](RenderJob job) {
            refusing->fail(job.id, RenderFailure{RenderFailureKind::Permanent, 0,
                                                 "no RenderFrameBackend: offline rendering has "
                                                 "no drawable owner in this build"});
        });
        refusing = &noBackend;
        SunoWorkspace offline;
        offline.setQueue(&noBackend);
        QSignalSpy nope(&offline, &SunoWorkspace::renderCompleted);
        QSignalSpy why(&offline, &SunoWorkspace::renderFailed);

        QVERIFY(offline.startRender(makeJob("no-backend", dir)).has_value());
        QCOMPARE(nope.count(), 0);
        QCOMPARE(messages(why),
                 (std::vector<std::string>{
                         "render no-backend ended as failed-permanent: no RenderFrameBackend: "
                         "offline rendering has no drawable owner in this build"}));
        QVERIFY(!offline.isRendering());

        // (c) Cancelled before a slot was free — the one path where nothing ever
        // ran and the queue still settles. `RenderQueue::cancel` settles a queued job
        // rather than erasing it, which is exactly why no `Rendering` flag can be
        // stranded here. The blocker occupies the single concurrency slot.
        RenderQueue occupied([](RenderJob) {});
        occupied.setMaxConcurrent(1);
        QVERIFY(occupied.enqueue(makeJob("blocker", dir)).has_value());
        QCOMPARE(occupied.activeCount(), 1);
        SunoWorkspace waiting;
        waiting.setQueue(&occupied);
        QSignalSpy cancelDone(&waiting, &SunoWorkspace::renderCompleted);
        QSignalSpy cancelFail(&waiting, &SunoWorkspace::renderFailed);

        QVERIFY(waiting.startRender(makeJob("waiting", dir)).has_value());
        QVERIFY2(waiting.isRendering(), "a queued job IS live; the flag must say so");
        QVERIFY(waiting.activeRenderJobId().value() == std::string_view("waiting"));
        QVERIFY(waiting.cancelRender());
        QCOMPARE(cancelDone.count(), 0);
        QCOMPARE(messages(cancelFail),
                 (std::vector<std::string>{"render waiting ended as cancelled: cancelled "
                                           "before a slot was free"}));
        QVERIFY(!waiting.isRendering());
        QVERIFY2(!waiting.cancelRender(),
                 "cancelling nothing must report false rather than claim a second "
                 "cancelled render");
    }

    // ── 3. Progress never lies ──────────────────────────────────────────────
    void progressIsMonotonicAndUnknownIsNeverZero() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const fs::path dir = fs::path(temp.path().toStdString());

        // A worker that reports a *smaller* count than it already did. The queue
        // refuses it; the published percent must not move back either, because a bar
        // that walks 66% -> 16% reads as a stall.
        RenderQueue* backwards = nullptr;
        RenderQueue queue([&](RenderJob job) {
            backwards->reportProgress(job.id, 1200); // 66%
            backwards->reportProgress(job.id, 300);  // refused: non-monotonic
            RenderReceipt receipt;
            receipt.outcome = RenderState::Completed;
            receipt.framesExpected = job.expectedFrames;
            receipt.framesWritten = job.expectedFrames;
            backwards->finish(job.id, std::move(receipt));
        });
        backwards = &queue;
        SunoWorkspace ws;
        ws.setQueue(&queue);
        QSignalSpy progress(&ws, &SunoWorkspace::renderProgress);

        QVERIFY(ws.startRender(makeJob("monotonic", dir)).has_value());
        expectPercents(percents(progress), (std::vector<int>{0, 0, 0, 66, 100}));
        QVERIFY(!ws.isRendering());

        // An unknown denominator is -1, and stays -1. `RenderQueue::enqueue`
        // hardcodes a literal 0 at its admission emit rather than asking
        // `RenderResult::progressPercent()`, which returns -1 for exactly this job;
        // publishing that 0 would put a permanent "0%" on a render nobody can
        // measure.
        RenderQueue* quiet = nullptr;
        RenderQueue unknown([&](RenderJob job) {
            quiet->reportProgress(job.id, 900);
            quiet->fail(job.id, RenderFailure{RenderFailureKind::Permanent, 0, "done"});
        });
        quiet = &unknown;
        SunoWorkspace blind;
        blind.setQueue(&unknown);
        QSignalSpy blindProgress(&blind, &SunoWorkspace::renderProgress);

        RenderJob noDenominator = makeJob("no-denominator", dir);
        noDenominator.expectedFrames = 0; // legal, and documented as "unknown"
        QVERIFY(blind.startRender(std::move(noDenominator)).has_value());
        QVERIFY2(blindProgress.count() == 5,
                 "the queue emits admission, state, opening progress, the worker's report "
                 "and the settle; all five are translated");
        expectPercents(percents(blindProgress), (std::vector<int>{-1, -1, -1, -1, -1}));
        QVERIFY(!blind.isRendering());
    }
};

#include "test_SunoWorkspace.moc"

int runTestSunoWorkspace(int argc, char** argv) {
    TestSunoWorkspace t;
    return QTest::qExec(&t, argc, argv);
}