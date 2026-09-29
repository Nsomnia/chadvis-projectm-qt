#include <QtTest>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "util/Signal.hpp"
#include "util/Types.hpp"

using vc::i64;
using vc::Signal;
using vc::u32;
using vc::u8;

namespace {

/// The corrected cost model, which this suite depends on.
///
/// `emitSignal` used to take its payload by value and pass it on as an lvalue,
/// so every subscriber copy-constructed it. On `Signal<std::vector<u8>, u32,
/// u32, i64>` that is ~8 MB of memcpy per frame per subscriber at 1080p60, on
/// the GUI thread, right after a GPU readback that already cost the same
/// again. A copy is measured in bytes and a `std::vector<u8>` move is a 24-byte
/// buffer steal, so the two are not comparable costs: these tests therefore
/// assert **copies** and **observed values**, never exact move counts, which
/// are a `std::function` implementation detail. Measured, old to new:
///
///     1 sub, rvalue    copies 1 -> 0    moves 2 -> 2
///     1 sub, lvalue    copies 2 -> 1    moves 1 -> 1
///     2 sub, lvalue    copies 3 -> 2    moves 2 -> 2
///     3 sub, rvalue    copies 3 -> 2    moves 4 -> 4
///
/// The hot path has exactly one production subscriber, so it takes the
/// forwarded branch and pays zero copies. `aSingleSubscriberStealsTheFrameBuffer`
/// is the test that measures it; the rest pin the fan-out and the pre-existing
/// semantics around it.

/// Counts its own copy-construction, which is the quantity the defect was
/// measured in. Move-construction is counted too, but only ever asserted as a
/// lower bound: an exact move count is a `std::function` implementation detail
/// and asserting one is what encoded the wrong cost model in the first place.
struct Probe {
    static inline int copies = 0;
    static inline int moves = 0;

    /// The moved-from value, so a subscriber that is handed a payload another
    /// subscriber already moved out of is visible as -1 rather than merely
    /// "not 42".
    static constexpr int kMovedFrom = -1;

    int value{0};

    Probe() = default;
    /// The origin of every payload in this suite. Deliberately neither a copy
    /// nor a move, and spelled as a prvalue so `Probe p = Probe(7)` is
    /// guaranteed-elided and the counters start at zero.
    explicit Probe(int initial) : value(initial) {}
    Probe(const Probe& other) : value(other.value) { ++copies; }
    Probe(Probe&& other) noexcept : value(other.value) {
        other.value = kMovedFrom;
        ++moves;
    }
    // Declared explicitly: a user-declared move constructor would otherwise
    // delete the copy assignment operator. Neither assignment is counted, and
    // neither is reached here.
    Probe& operator=(const Probe&) = default;
    Probe& operator=(Probe&&) = default;

    static void resetCounts() {
        copies = 0;
        moves = 0;
    }
};

} // namespace

class TestSignal : public QObject {
    Q_OBJECT

private slots:
    /// The centrepiece: the production payload shape, one production-style
    /// subscriber, and an aliasing assertion instead of a construction count.
    ///
    /// `data()` identity answers the only question the defect ever was --
    /// *was the frame memcpy'd or not?* -- and it is robust across standard
    /// libraries, unlike counting constructions. Reintroduce a by-value hop
    /// anywhere in the chain (the old `emitSignal(Args... args)`) and the
    /// subscriber's buffer is a fresh allocation at a different address, so this
    /// fails; the `copies` counts in the other tests would not notice a copy
    /// hidden behind a `std::function` that elides.
    void aSingleSubscriberStealsTheFrameBuffer() {
        Signal<std::vector<u8>, u32, u32, i64> signal;

        std::vector<u8> seen;
        u32 seenWidth = 0;
        u32 seenHeight = 0;
        i64 seenTimestamp = 0;
        signal.connect([&](std::vector<u8> frame, u32 width, u32 height, i64 ts) {
            seen = std::move(frame);
            seenWidth = width;
            seenHeight = height;
            seenTimestamp = ts;
        });

        constexpr u32 kWidth = 1920;
        constexpr u32 kHeight = 1080;
        std::vector<u8> frame(kWidth * kHeight * 3u, 0x7Fu);
        const unsigned char* const originalData = frame.data();
        const std::size_t originalBytes = frame.size();

        signal.emitSignal(std::move(frame), kWidth, kHeight, i64{1234567});

        QCOMPARE(seen.size(), originalBytes);
        QCOMPARE(seenWidth, kWidth);
        QCOMPARE(seenHeight, kHeight);
        QCOMPARE(seenTimestamp, i64{1234567});
        QVERIFY2(seen.data() == originalData,
                 "the frame buffer was copied instead of stolen: a by-value hop "
                 "is back in the emit chain");
        QVERIFY2(seen[0] == 0x7Fu, "the stolen buffer lost its contents");
        // The emitter handed its allocation away. All three major standard
        // libraries null the source on a vector move; pointer identity above is
        // the normative proof, this is the corroboration.
        QVERIFY(frame.empty());
        QCOMPARE(frame.capacity(), std::size_t{0});
    }

    /// The hot-path invariant in isolation, and the one the TODO item was about:
    /// an rvalue payload reaching a single by-value subscriber costs zero
    /// copies. `moves` is asserted only as a lower bound, because an exact move
    /// count is a `std::function` implementation detail; `copies == 0` plus the
    /// observed value is what actually pins the behaviour.
    void rvalueEmitToOneSubscriberMovesAndNeverCopies() {
        Probe::resetCounts();

        Signal<Probe> signal;
        int observed = 0;
        signal.connect([&observed](Probe probe) { observed = probe.value; });

        signal.emitSignal(Probe(7));

        QCOMPARE(Probe::copies, 0);
        QVERIFY2(Probe::moves > 0, "the payload never reached the subscriber");
        QCOMPARE(observed, 7);
    }

    /// Exactly once, not zero (that would be an aliasing bug) and not twice
    /// (the old code copied into its own by-value parameter and then again into
    /// the subscriber's). Both lvalue flavours are covered because the deduced
    /// forwarding reference has to accept each of them, and the tree emits
    /// members and const-ref parameters interchangeably.
    void lvalueEmitToOneSubscriberCopiesExactlyOnce() {
        Probe::resetCounts();

        Signal<Probe> signal;
        int observed = 0;
        signal.connect([&observed](Probe probe) { observed = probe.value; });

        Probe payload = Probe(11);
        signal.emitSignal(payload);

        QCOMPARE(Probe::copies, 1);
        QCOMPARE(observed, 11);
        QCOMPARE(payload.value, 11);

        Probe::resetCounts();
        const Probe constPayload = Probe(12);
        signal.emitSignal(constPayload);

        QCOMPARE(Probe::copies, 1);
        QCOMPARE(observed, 12);
        QCOMPARE(constPayload.value, 12);
    }

    /// The fan-out contract: every subscriber observes a full payload and none
    /// is handed a value another one already moved out of -- which is what
    /// forwarding to all of them would do. The last subscriber takes the
    /// forwarded value, so the emitter's own object ends up moved-from; the
    /// earlier ones take copies of it while it is still intact.
    void everySubscriberSeesTheFullValue() {
        Probe::resetCounts();

        Signal<Probe> signal;
        std::vector<int> seen;
        signal.connect([&seen](Probe probe) { seen.push_back(probe.value); });
        signal.connect([&seen](Probe probe) { seen.push_back(probe.value); });
        signal.connect([&seen](Probe probe) { seen.push_back(probe.value); });

        Probe payload = Probe(42);
        signal.emitSignal(std::move(payload));

        QCOMPARE(seen.size(), std::size_t{3});
        QCOMPARE(seen[0], 42);
        QCOMPARE(seen[1], 42);
        QVERIFY2(seen[2] != Probe::kMovedFrom,
                 "the last subscriber was handed an already-moved-from payload");
        QCOMPARE(seen[2], 42);
        QCOMPARE(Probe::copies, 2);
        QCOMPARE(payload.value, Probe::kMovedFrom);
    }

    /// Copies, not moves, when the emitter owns a named object: a second
    /// subscriber must not be able to steal the first one's payload, and the
    /// caller's object must survive the emit intact.
    void lvalueEmitFansOutAsOneCopyPerSubscriber() {
        Probe::resetCounts();

        Signal<Probe> signal;
        std::vector<int> seen;
        signal.connect([&seen](Probe probe) { seen.push_back(probe.value); });
        signal.connect([&seen](Probe probe) { seen.push_back(probe.value); });

        Probe payload = Probe(5);
        signal.emitSignal(payload);

        QCOMPARE(seen.size(), std::size_t{2});
        QCOMPARE(seen[0], 5);
        QCOMPARE(seen[1], 5);
        QCOMPARE(Probe::copies, 2);
        QCOMPARE(payload.value, 5);
    }

    /// A single-argument suite cannot prove that *every* argument position is
    /// forwarded; this one emits a temporary string alongside a moved Probe.
    void everyArgumentPositionIsForwarded() {
        Probe::resetCounts();

        Signal<std::string, Probe> signal;
        std::string seenLabel;
        int seenValue = 0;
        signal.connect([&](std::string label, Probe probe) {
            seenLabel = std::move(label);
            seenValue = probe.value;
        });

        signal.emitSignal(std::string("frame"), Probe(3));

        QCOMPARE(Probe::copies, 0);
        QVERIFY(Probe::moves > 0);
        QCOMPARE(seenLabel, std::string("frame"));
        QCOMPARE(seenValue, 3);
    }

    /// The forwarding change must not alter what a reference payload does: a
    /// `const T&` argument still aliases the caller's object and is never
    /// copied, and a subscriber taking `const T&` still sees a live object for
    /// the duration of the call.
    ///
    /// The zero counts here are not a construction-count model: a reference
    /// payload is never constructed at all, so both are structurally zero
    /// rather than an implementation detail. `Signal<const std::vector<SunoClip>&>`
    /// and `Signal<const RecordingStats&>` are live instances of this shape.
    void constReferencePayloadIsNotCopied() {
        Probe::resetCounts();

        Signal<const Probe&> signal;
        int observed = 0;
        const Probe* aliased = nullptr;
        signal.connect([&](const Probe& probe) {
            observed = probe.value;
            aliased = &probe;
        });

        const Probe payload = Probe(8);
        signal.emitSignal(payload);

        QCOMPARE(observed, 8);
        QCOMPARE(Probe::copies, 0);
        QCOMPARE(Probe::moves, 0);
        QCOMPARE(aliased, &payload);
    }

    /// operator() is a second forwarding site, so it gets the same proof: it
    /// used to take the payload by value and forward it as a perfect-forwarding
    /// no-op, which was a copy dressed up as a forward.
    void callOperatorForwardsLikeEmitSignal() {
        Probe::resetCounts();

        Signal<Probe> signal;
        int observed = 0;
        signal.connect([&observed](Probe probe) { observed = probe.value; });

        signal(Probe(21));

        QCOMPARE(Probe::copies, 0);
        QVERIFY(Probe::moves > 0);
        QCOMPARE(observed, 21);
    }

    void emittingToNobodyIsANoOp() {
        Probe::resetCounts();

        Signal<Probe> signal;
        QVERIFY(!signal.hasConnections());
        QCOMPARE(signal.connectionCount(), std::size_t{0});

        // Neither a copy nor a move: with no subscriber in the snapshot the
        // payload is never touched.
        signal.emitSignal(Probe(4));

        QCOMPARE(Probe::copies, 0);
        QCOMPARE(Probe::moves, 0);
        QVERIFY(!signal.hasConnections());
    }

    void disconnectStopsDelivery() {
        Signal<Probe> signal;
        int calls = 0;
        const auto id = signal.connect([&calls](Probe) { ++calls; });

        signal.emitSignal(Probe(1));
        QCOMPARE(calls, 1);

        signal.disconnect(id);
        QCOMPARE(signal.connectionCount(), std::size_t{0});
        signal.emitSignal(Probe(2));
        QCOMPARE(calls, 1);

        // A never-connected id is harmless.
        signal.disconnect(id);
        signal.emitSignal(Probe(3));
        QCOMPARE(calls, 1);
    }

    void disconnectAllStopsEveryDelivery() {
        Signal<Probe> signal;
        int calls = 0;
        signal.connect([&calls](Probe) { ++calls; });
        signal.connect([&calls](Probe) { ++calls; });

        signal.emitSignal(Probe(1));
        QCOMPARE(calls, 2);

        signal.disconnectAll();
        QCOMPARE(signal.connectionCount(), std::size_t{0});
        signal.emitSignal(Probe(2));
        QCOMPARE(calls, 2);
    }

    /// Pre-existing deferred-cleanup semantics: the connection list is
    /// snapshotted before any slot runs, so a slot that disconnects a later one
    /// still lets that later one run for the current emit, and the mark is
    /// honoured from the next emit onwards.
    void disconnectingFromInsideASlotTakesEffectOnTheNextEmit() {
        Signal<Probe> signal;
        int firstCalls = 0;
        int secondCalls = 0;
        Signal<Probe>::SlotId secondId{0};

        secondId = signal.connect([&](Probe) { ++secondCalls; });
        signal.connect([&](Probe) {
            ++firstCalls;
            signal.disconnect(secondId);
        });

        signal.emitSignal(Probe(1));
        QCOMPARE(firstCalls, 1);
        QCOMPARE(secondCalls, 1);

        signal.emitSignal(Probe(2));
        QCOMPARE(firstCalls, 2);
        QCOMPARE(secondCalls, 1);
        QCOMPARE(signal.connectionCount(), std::size_t{1});
    }

    /// A re-entrant emit must not take ownership of the chain cleanup away from
    /// the emit that is still running. A bool cannot express that, and it fails
    /// twice over: the inner emit's `false` sweeps the connection list early,
    /// and it makes the *outer* emit's later `disconnect` calls erase outright.
    ///
    /// Connect order is A, B, C, D and it is load-bearing: A must be first so
    /// the outer emit starts there, and A is the only slot that re-enters.
    /// Trace, with the outer emit iterating [A, B, C, D]:
    ///   A  disconnects B (a deferred mark), emits again, then reads the
    ///      connection list twice.
    ///      - after the nested emit returns, before its own later disconnect:
    ///        correct 4 (B and D are marked, nothing erased), a bool gives 2 --
    ///        the inner emit had already swept both marks.
    ///      - after disconnecting C, still inside the outer emit: correct 4 (a
    ///        mark is not an erasure), a bool gives 1 (erased outright, because
    ///        its flag was already false).
    ///   The nested snapshot is [A, C, D] under both, since B is marked by
    ///   then, so the two observations are about *when* the list is swept and
    ///   nothing else -- which is why the call counts below come out identical
    ///   in both models and cannot be what makes this test pass.
    void nestedEmitDoesNotTakeTheChainCleanupFromTheOuterEmit() {
        Signal<int> signal;
        int aCalls = 0;
        int bCalls = 0;
        int cCalls = 0;
        int dCalls = 0;
        std::size_t countAfterNestedEmit = 0;
        std::size_t countAfterOuterDisconnect = 0;
        bool inNestedEmit = false;
        Signal<int>::SlotId bId{0};
        Signal<int>::SlotId cId{0};
        Signal<int>::SlotId dId{0};

        signal.connect([&](int) {
            ++aCalls;
            if (!inNestedEmit) {
                signal.disconnect(bId);
                inNestedEmit = true;
                signal.emitSignal(-1);
                inNestedEmit = false;
                countAfterNestedEmit = signal.connectionCount();
                signal.disconnect(cId);
                countAfterOuterDisconnect = signal.connectionCount();
            }
        });
        bId = signal.connect([&bCalls](int) { ++bCalls; });
        cId = signal.connect([&](int) {
            ++cCalls;
            if (inNestedEmit) {
                // A disconnect issued from inside the nested emit must still be
                // a mark, not an erase.
                signal.disconnect(dId);
            }
        });
        dId = signal.connect([&dCalls](int) { ++dCalls; });

        signal.emitSignal(1);

        // The point of the test: nothing was erased while the outer emit was
        // still iterating, neither by the inner emit nor by its own later
        // disconnect.
        QCOMPARE(countAfterNestedEmit, std::size_t{4});
        QCOMPARE(countAfterOuterDisconnect, std::size_t{4});

        // The sweep happened exactly once, at the outermost leave: B, C and D
        // were all marked, and all three are gone now.
        QCOMPARE(signal.connectionCount(), std::size_t{1});

        // Snapshot semantics, unchanged: B is excluded from the nested emit
        // (already marked when that snapshot was taken) and B, C and D all ran
        // for the outer emit.
        QCOMPARE(aCalls, 2);
        QCOMPARE(bCalls, 1);
        QCOMPARE(cCalls, 2);
        QCOMPARE(dCalls, 2);
    }

    /// A throwing subscriber must not strand the chain. With the depth counter
    /// reached from a plain `emitting_ = false` at the end of `emitSignal`, the
    /// throw skips it, and every later `disconnect` takes the mark-only path for
    /// the rest of the Signal's life: the dead connection is still counted, and
    /// is still called on every subsequent emit.
    ///
    /// `good` is connected first so that it runs *before* the throw; the
    /// subject under test is the stranded counter, not the abandoned tail of
    /// the snapshot.
    void aThrowingSubscriberDoesNotStrandTheEmitChain() {
        Signal<int> signal;
        int goodCalls = 0;
        signal.connect([&goodCalls](int) { ++goodCalls; });
        const Signal<int>::SlotId throwerId =
                signal.connect([](int) { throw std::runtime_error("boom"); });

        try {
            signal.emitSignal(1);
            QFAIL("the subscriber's exception did not propagate out of emitSignal");
        } catch (const std::runtime_error&) {
            // Expected: emitSignal is not noexcept, so it propagates.
        }
        QCOMPARE(goodCalls, 1);

        // The chain unwound, so this erases outright instead of only marking.
        signal.disconnect(throwerId);
        QCOMPARE(signal.connectionCount(), std::size_t{1});

        // And the dead subscriber really is gone: this emit does not throw.
        signal.emitSignal(2);
        QCOMPARE(goodCalls, 2);
    }

    /// The fan-out loop picks its branch on `i + 1 == n`, so it is exercised
    /// above the handful of subscribers the other tests use.
    void everySubscriberAmongManyStillSeesTheFullValue() {
        constexpr int kSubscribers = 12;
        Probe::resetCounts();

        Signal<Probe> signal;
        std::vector<int> seen;
        for (int i = 0; i < kSubscribers; ++i) {
            signal.connect([&seen](Probe probe) { seen.push_back(probe.value); });
        }

        Probe payload = Probe(99);
        signal.emitSignal(std::move(payload));

        QCOMPARE(seen.size(), std::size_t{kSubscribers});
        for (const int value : seen) {
            QCOMPARE(value, 99);
        }
        QCOMPARE(Probe::copies, kSubscribers - 1);
        QCOMPARE(payload.value, Probe::kMovedFrom);
    }

    void zeroArgumentSignalStillWorks() {
        Signal<> signal;
        int calls = 0;
        signal.connect([&calls] { ++calls; });

        signal.emitSignal();
        QCOMPARE(calls, 1);
        signal();
        QCOMPARE(calls, 2);

        Signal<> empty;
        QVERIFY(!empty.hasConnections());
        empty.emitSignal();
        empty();
        QCOMPARE(calls, 2);
    }

    /// Mixed argument kinds in one payload, which is what the real signals look
    /// like (`Signal<std::vector<u8>, u32, u32, i64>`): a heavy first argument
    /// and trivial scalars behind it must all survive the emit unchanged.
    void heavyAndTrivialArgumentsTogether() {
        using Bytes = std::vector<unsigned char>;

        Signal<Bytes, int, std::string> signal;
        Bytes seenBytes;
        int seenNumber = 0;
        std::string seenText;

        signal.connect([&](Bytes bytes, int number, std::string text) {
            seenBytes = std::move(bytes);
            seenNumber = number;
            seenText = std::move(text);
        });

        Bytes payload{1, 2, 3, 4};
        signal.emitSignal(std::move(payload), 1920, std::string("1080"));

        QCOMPARE(seenBytes.size(), std::size_t{4});
        QCOMPARE(seenBytes[0], static_cast<unsigned char>(1));
        QCOMPARE(seenBytes[3], static_cast<unsigned char>(4));
        QCOMPARE(seenNumber, 1920);
        QCOMPARE(seenText, std::string("1080"));
    }
};

#include "test_Signal.moc"

int runTestSignal(int argc, char** argv) {
    TestSignal tc;
    return QTest::qExec(&tc, argc, argv);
}
