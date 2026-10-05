#include "recorder/RenderExecutor.hpp"

#include <QTest>

#include <algorithm>
#include <functional>
#include <string>
#include <utility>
#include <vector>

using namespace vc;

namespace {

/// A three-job bench shaped like a real batch: a long job first and a *short* job
/// last in enqueue order, so a strictly serial scheduler would finish the short one
/// long after it. `framesDone` is advanced from the step just received, exactly as
/// `RenderExecutor::renderSlot` does, so the table the interleaver sees each turn is
/// the one the executor would really hand it.
struct Bench {
    FrameInterleaver inter;
    std::vector<ScheduleSlot> table;
    std::vector<u64> done;
    /// (jobId, frames) for every `Render` step, in order. Terminal verdicts are
    /// deliberately absent: a `Finish` produces no frames and recording one as a
    /// 0-frame render would blur the two.
    std::vector<std::pair<std::string, u32>> order;
    int finishes{0};
    int retires{0};
    /// Job ids in the order they first reached `Finish`. Deterministic, and it is
    /// the rotation's terminal order rather than an artefact of a step budget.
    std::vector<std::string> finishOrder;

    explicit Bench(const u32 quantum) : inter(quantum) {
        table = {
                {.jobId = "long",
                 .framesDone = 0,
                 .framesExpected = 6,
                 .cancelRequested = false,
                 .audioBehind = false},
                {.jobId = "mid",
                 .framesDone = 0,
                 .framesExpected = 4,
                 .cancelRequested = false,
                 .audioBehind = false},
                {.jobId = "short",
                 .framesDone = 0,
                 .framesExpected = 2,
                 .cancelRequested = false,
                 .audioBehind = false},
        };
        done = {0, 0, 0};
    }

    /// One turn: re-sync from the table, take one decision, act on it.
    ScheduleStep turn() {
        for (std::size_t i = 0; i < table.size(); ++i)
            table[i].framesDone = done[i];
        inter.sync(table);
        const ScheduleStep step = inter.next();
        switch (step.verdict) {
            case SlotVerdict::Render:
                order.emplace_back(step.jobId, step.frames);
                done[step.slotIndex] += step.frames;
                break;
            case SlotVerdict::Finish:
                ++finishes;
                if (std::find(finishOrder.begin(), finishOrder.end(), step.jobId) ==
                    finishOrder.end()) {
                    finishOrder.push_back(step.jobId);
                }
                break;
            case SlotVerdict::Retire:
                ++retires;
                break;
            case SlotVerdict::Skip:
                break;
        }
        return step;
    }

    /// Run to quiescence, bounded. Once every job is exhausted the interleaver keeps
    /// answering `Finish` forever, because dropping a finished slot is the
    /// *executor's* job (it erases from its own vector) and the pure policy has no
    /// ownership to erase into. So the budget is part of the expectation: 5 render
    /// steps plus one full terminal cycle over the three exhausted jobs.
    void drain(const int maxSteps = 8) {
        for (int i = 0; i < maxSteps; ++i)
            turn();
    }
};

} // namespace

class TestRenderExecutor : public QObject {
    Q_OBJECT

private slots:

    /// The load-bearing one: the interleaving order *is* the policy, exactly.
    ///
    /// Quantum 3 over jobs of 6 / 4 / 2 frames produces a sequence that can only
    /// come from "deficit-free round-robin, at most `quantum` frames per turn, one
    /// cursor step per turn, clipped to the frames a job still owes". Every entry is
    /// asserted, so a change to per-frame round-robin or to priority-by-frames-
    /// remaining fails here rather than being argued about in review.
    ///
    /// The ceiling half belongs to the same function because it is the same claim:
    /// the concurrency must follow the *verdict*. A test that asserted
    /// `maxConcurrent == 1` would pass on a build where somebody hardcoded 1, which
    /// is precisely the defect this file exists to prevent.
    void interleavingOrderIsExactAndTheCeilingFollowsTheVerdict() {
        Bench bench(3);
        bench.drain();

        const std::vector<std::pair<std::string, u32>> expected = {
                {"long", 3}, {"mid", 3}, {"short", 2}, // clipped to what `short` still owes
                {"long", 3}, {"mid", 1},               // likewise
        };
        QCOMPARE(std::size_t(bench.order.size()), std::size_t(expected.size()));
        for (std::size_t i = 0; i < expected.size(); ++i) {
            QCOMPARE(bench.order[i].first, expected[i].first);
            QCOMPARE(bench.order[i].second, expected[i].second);
        }
        QCOMPARE(bench.done[0], u64(6));
        QCOMPARE(bench.done[1], u64(4));
        QCOMPARE(bench.done[2], u64(2));

        // Every job ended in `Finish` and none in `Retire`, and the interleaver
        // dropped nothing: the executor owns slot lifetime. `short` finishes first
        // because it is the only job with frames left when its turn comes round, which
        // is the ordering claim: with three jobs sharing a context the short one does
        // not wait behind the long one.
        QCOMPARE(bench.finishes, 3);
        QCOMPARE(bench.retires, 0);
        const std::vector<std::string> expectedFinish = {"short", "long", "mid"};
        QCOMPARE(std::size_t(bench.finishOrder.size()), std::size_t(expectedFinish.size()));
        for (std::size_t i = 0; i < expectedFinish.size(); ++i) {
            QCOMPARE(bench.finishOrder[i], expectedFinish[i]);
        }
        QCOMPARE(int(bench.inter.size()), 3);

        // --- verdict -> ceiling ---
        const FrameCalibration none;
        const auto mac = decideConcurrency(RenderThreadVerdict::NeedsMainThread, none, 250.0);
        QCOMPARE(int(mac.topology), int(RenderTopology::GuiThreadShared));
        // Deliberately NOT 1: with one shared context the ceiling now bounds how many
        // jobs are *admitted*, not how many contexts exist. Pinning 1 here would pin
        // the pre-interleaving architecture.
        QCOMPARE(mac.maxConcurrent, kDefaultInterleaveDepth);
        QVERIFY(!mac.reason.empty());

        const auto dead = decideConcurrency(RenderThreadVerdict::Unsupported, none, 250.0);
        QCOMPARE(int(dead.maxConcurrent), 0);
        QCOMPARE(int(dead.topology), int(RenderTopology::Unsupported));

        // `Supported` with no measurement invents no number.
        const auto uncal = decideConcurrency(RenderThreadVerdict::Supported, none, 250.0);
        QCOMPARE(int(uncal.topology), int(RenderTopology::WorkerPerJob));
        QVERIFY(uncal.requiresCalibration);

        // `Supported` *with* a measurement is the only path that derives a number, and
        // it is clamped in both directions.
        const FrameCalibration fast{.frames = 30, .meanFrameMs = 8.0, .measured = true};
        QCOMPARE(decideConcurrency(RenderThreadVerdict::Supported, fast, 250.0).maxConcurrent,
                 kCalibratedCeilingCap);
        const FrameCalibration slow{.frames = 30, .meanFrameMs = 400.0, .measured = true};
        QCOMPARE(decideConcurrency(RenderThreadVerdict::Supported, slow, 250.0).maxConcurrent, 1);
    }

    /// A cancel lands within one quantum, not at the end of a 3-minute render.
    ///
    /// The request lands mid-rotation on purpose: cancelling when the cursor already
    /// points at the job would measure a lucky alignment. The claim is bounded by
    /// the quantum, and this is where that is measured -- in *turns* (the user-visible
    /// half) and in *frames not rendered* (the half that would have cost three
    /// minutes). It also pins that a cancel is `Retire` and not `Finish`: a
    /// cancelled render must never be filed as a completed one.
    void aCancelRetiresWithinOneQuantum() {
        constexpr u32 kQuantum = 4;
        FrameInterleaver inter(kQuantum);
        std::vector<ScheduleSlot> table = {
                {.jobId = "a",
                 .framesDone = 0,
                 .framesExpected = 600,
                 .cancelRequested = false,
                 .audioBehind = false},
                {.jobId = "b",
                 .framesDone = 0,
                 .framesExpected = 600,
                 .cancelRequested = false,
                 .audioBehind = false},
        };
        u64 doneA = 0;
        int cancelTurn = -1;
        u64 framesAtCancel = 0;
        u64 framesAtRetire = 0;
        int turnsAfterCancel = -1;

        for (int i = 0; i < 64; ++i) {
            table[0].framesDone = doneA;
            // Turn 3 is mid-rotation: the cursor points at `b`, so the worst case is
            // measured rather than dodged.
            if (cancelTurn < 0 && i == 3) {
                table[0].cancelRequested = true;
                cancelTurn = i;
                framesAtCancel = doneA;
            }
            inter.sync(table);
            const ScheduleStep step = inter.next();
            if (step.verdict == SlotVerdict::Render) {
                if (step.slotIndex == 0) doneA += step.frames;
                continue;
            }
            if (step.verdict == SlotVerdict::Retire) {
                QCOMPARE(step.jobId, std::string("a"));
                QCOMPARE(step.frames, 0u);
                framesAtRetire = doneA;
                turnsAfterCancel = i - cancelTurn;
                break;
            }
        }

        QVERIFY2(cancelTurn >= 0, "the cancel request was never issued");
        QVERIFY2(turnsAfterCancel >= 0, "the cancel was never honoured");
        // At most one turn of the rotation, i.e. at most one quantum of latency.
        const std::string why = "cancel took " + std::to_string(turnsAfterCancel) +
                                " turns, more than the single-quantum bound";
        QVERIFY2(turnsAfterCancel <= 1, why.c_str());
        // And at most one quantum of frames the job did not get to render.
        QVERIFY(framesAtRetire - framesAtCancel <= u64(kQuantum));
        // Nowhere near finished: this is what distinguishes "within one quantum" from
        // "at the end of the job".
        QVERIFY(framesAtRetire < table[0].framesExpected);
    }

    /// The audio-full policy: spend no GL on the blocked job, and do not let it
    /// starve the others.
    ///
    /// "Pause the render" is the honest answer for a job whose audio has fallen
    /// behind, because a dropped audio frame is a permanent A/V desync that cannot
    /// be repaired after the fact. What makes pausing affordable is that it is
    /// scoped to *one* job: the interleaver steps over the blocked slot and serves the
    /// next, and a blocked slot is never retired -- clearing `audioBehind` makes it
    /// eligible again with its frame counter intact. `skippedBehind` is counted rather
    /// than hidden so a permanently blocked job is a visible number.
    void aJobWithAudioBehindIsSkippedWithoutStarvingTheOthers() {
        FrameInterleaver inter(4);
        std::vector<ScheduleSlot> table = {
                {.jobId = "starved",
                 .framesDone = 0,
                 .framesExpected = 100,
                 .cancelRequested = false,
                 .audioBehind = true},
                {.jobId = "healthy",
                 .framesDone = 0,
                 .framesExpected = 100,
                 .cancelRequested = false,
                 .audioBehind = false},
        };
        inter.sync(table);

        const ScheduleStep first = inter.next();
        QCOMPARE(first.verdict, SlotVerdict::Render);
        QCOMPARE(first.jobId, std::string("healthy"));
        QCOMPARE(first.frames, 4u);
        QCOMPARE(first.skippedBehind, 1u);
        QCOMPARE(int(table[0].framesDone), 0);
        // Nothing was retired: a block is a pause, not a failure.
        QCOMPARE(int(inter.size()), 2);

        // Both blocked -> `Skip`, and still nothing dropped. This is the branch that
        // would otherwise spin the turn doing no work at all.
        table[1].audioBehind = true;
        inter.sync(table);
        const ScheduleStep blocked = inter.next();
        QCOMPARE(blocked.verdict, SlotVerdict::Skip);
        QCOMPARE(blocked.frames, 0u);
        QCOMPARE(int(inter.size()), 2);

        // Unblocked, it comes back with its counter untouched.
        table[0].audioBehind = false;
        inter.sync(table);
        const ScheduleStep recovered = inter.next();
        QCOMPARE(recovered.verdict, SlotVerdict::Render);
        QCOMPARE(recovered.jobId, std::string("starved"));
        QCOMPARE(recovered.frames, 4u);
        QCOMPARE(recovered.skippedBehind, 0u);
    }
};

QTEST_MAIN(TestRenderExecutor)
#include "test_RenderExecutor.moc"