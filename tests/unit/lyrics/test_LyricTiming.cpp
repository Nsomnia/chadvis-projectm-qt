/**
 * @file test_LyricTiming.cpp
 * @brief The reflow arithmetic, the undo stack, the plausibility rules, and the
 *        TOML round trip for the karaoke timing-correction layer.
 *
 * Four functions, not a suite, and each one pins a claim the header makes rather
 * than re-deriving it from the implementation.
 *
 * @section Why the reflow example is built from binary fractions
 * The worked example uses only multiples of 1/16 and a segment slope of exactly
 * 2.0. That is not decoration: it makes every mapped value *exactly* representable
 * and every intermediate (`x - x0`, the multiply, the add) exact in IEEE 754, so
 * the assertion can be a bit-for-bit comparison against a decimal a reader can
 * verify with a pencil. A fixture of "nice looking" decimals like 0.63 and 2/3
 * would put the rounding error *inside* the arithmetic, and the test would then be
 * asserting the rounding of the implementation rather than the model.
 *
 * @section Why `std::bit_cast` and not `QCOMPARE` for the undo test
 * `QCOMPARE` on two doubles is `qFuzzyCompare`, which accepts a 1-ULP difference.
 * This codebase has been bitten by exactly that twice, so the undo assertion here
 * uses `std::bit_cast` on the raw bits. Undo restores a stored vector verbatim, so
 * a bit-for-bit comparison is both available and the stronger claim: it proves the
 * snapshot is replayed rather than recomputed.
 */

#include <QTemporaryDir>
#include <QtTest>

#include <bit>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

#include "lyrics/LyricTiming.hpp"

using namespace vc;
using namespace vc::lyricstiming;

namespace {

/// Bit-for-bit equality on a `double`. The one place this file asserts a timing.
[[nodiscard]] bool sameBits(const f64 actual, const f64 expected) {
    return std::bit_cast<u64>(actual) == std::bit_cast<u64>(expected);
}

/// Bit-for-bit equality on a whole word list, so a document comparison can be
/// written as one line rather than a loop in every test.
[[nodiscard]] bool sameBits(const std::vector<TimedWord>& actual,
                            const std::vector<TimedWord>& expected) {
    if (actual.size() != expected.size()) {
        return false;
    }
    for (std::size_t i = 0; i < actual.size(); ++i) {
        if (actual[i].text != expected[i].text || actual[i].pin != expected[i].pin ||
            !sameBits(actual[i].startSeconds, expected[i].startSeconds) ||
            !sameBits(actual[i].endSeconds, expected[i].endSeconds)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::vector<TimedWord> snapshotOf(const LyricTimingDocument& document) {
    return std::vector<TimedWord>(document.words().begin(), document.words().end());
}

[[nodiscard]] usize countOf(const PlausibilityReport& report, const ViolationKind kind) {
    usize count = 0;
    for (const auto& finding : report.findings) {
        if (finding.kind == kind) {
            ++count;
        }
    }
    return count;
}

[[nodiscard]] TimedWord makeWord(std::string text, const f64 start, const f64 end,
                                 const Pin pin = Pin::None) {
    return TimedWord{std::move(text), start, end, pin};
}

/// Nine words on a 1/16 grid, 3/16 long, 1/16 apart.
///
/// Every value is dyadic, so it is exact in `double` (and in `float`, which is the
/// point: the model is not relying on precision the format could not have carried).
/// Word `i` starts at `0.25 + i/4`.
///
///   w0 [0.2500, 0.4375)   w1 [0.5000, 0.6875)   w2 [0.7500, 0.9375)
///   w3 [1.0000, 1.1875)   w4 [1.2500, 1.4375)   w5 [1.5000, 1.6875)
///   w6 [1.7500, 1.9375)   w7 [2.0000, 2.1875)   w8 [2.2500, 2.4375)
[[nodiscard]] std::vector<TimedWord> nineWordFixture() {
    std::vector<TimedWord> words;
    words.reserve(9);
    for (int i = 0; i < 9; ++i) {
        const f64 start = 0.25 + 0.25 * static_cast<f64>(i);
        words.push_back(makeWord("w" + std::to_string(i), start, start + 0.1875));
    }
    return words;
}

} // namespace

class TestLyricTiming : public QObject {
    Q_OBJECT

private slots:
    // ───────────────────────────────────────────────────────────────────────
    // 1. The reflow model, on numbers a reader can check by hand.
    // ───────────────────────────────────────────────────────────────────────
    void theReflowModelIsExactOnAWorkedExample();

    // ───────────────────────────────────────────────────────────────────────
    // 2. Undo, including a 128-step drag that must be ONE step.
    // ───────────────────────────────────────────────────────────────────────
    void undoRestoresABitwiseIdenticalDocumentAfterAMultiStepDrag();

    // ───────────────────────────────────────────────────────────────────────
    // 3. Plausibility: every named violation rejected, a good document accepted.
    // ───────────────────────────────────────────────────────────────────────
    void plausibilityRejectsTheNamedViolationsAndAcceptsAGoodDocument();

    // ───────────────────────────────────────────────────────────────────────
    // 4. The serialisation round trip, exact on every field.
    // ───────────────────────────────────────────────────────────────────────
    void theSerialisationRoundTripIsExactOnEveryField();
};

// ─────────────────────────────────────────────────────────────────────────────

void TestLyricTiming::theReflowModelIsExactOnAWorkedExample() {
    LyricTimingDocument document = LyricTimingDocument::adopt(nineWordFixture());

    // ── step 1: one anchor, so one word moves and nothing else does ─────────
    //
    // 0.5625 - 0.50 = +0.0625. With a single node there is no span to interpolate
    // between, and the header's rule -- nothing outside the outermost anchors
    // moves -- leaves w0 and w2..w8 exactly where they were.
    const auto first = document.pinAnchor(1, 0.5625);
    QVERIFY2(first.has_value(), first.has_value() ? "" : first.error().describe().c_str());
    QVERIFY(sameBits(first->deltaSeconds, 0.0625));
    QCOMPARE(first->movedWords, std::size_t{0});

    QVERIFY(sameBits(document.words()[1].startSeconds, 0.5625));
    // The pinned word's offset translates rigidly: 0.6875 + 0.0625 = 0.75, so its
    // duration is still 3/16.
    QVERIFY(sameBits(document.words()[1].endSeconds, 0.75));
    QCOMPARE(document.words()[1].pin, Pin::Onset);
    for (const usize i : {std::size_t{0}, std::size_t{2}, std::size_t{3}, std::size_t{4},
                          std::size_t{5}, std::size_t{6}, std::size_t{7}, std::size_t{8}}) {
        QVERIFY2(sameBits(document.words()[i].startSeconds, nineWordFixture()[i].startSeconds),
                 qPrintable("word " + QString::number(static_cast<int>(i))));
    }

    // ── step 2: a second anchor, and now the whole span rescales ───────────
    //
    // Old onsets 0.50 and 2.25 -> span 1.75. New onsets 0.5625 and 4.0625 ->
    // span 3.50. Ratio = 3.50 / 1.75 = 2.0 exactly.
    //
    // Every free word therefore maps as  f(x) = 0.5625 + (x - 0.5) * 2,  which a
    // reader can check with one multiply per word:
    //
    //   w2 start 0.7500 -> 0.5625 + 0.25*2 = 1.0625
    //   w3 start 1.0000 -> 0.5625 + 0.50*2 = 1.5625
    //   w4 start 1.2500 -> 0.5625 + 0.75*2 = 2.0625
    //   w5 start 1.5000 -> 0.5625 + 1.00*2 = 2.5625
    //   w6 start 1.7500 -> 0.5625 + 1.25*2 = 3.0625
    //   w7 start 2.0000 -> 0.5625 + 1.50*2 = 3.5625
    //
    // and w0 stays put because 0.25 < 0.50 -- it is outside the anchored span.
    //
    // DURATIONS DO NOT SCALE, and that is a decision rather than an oversight. The
    // first version of this test expected the end to map independently, giving
    // word 2 a duration of 0.375 instead of 0.1875 -- every word in the line
    // silently doubling in length.
    //
    // Rigid duration is right because the correction target is a RE-ENCODED LOCAL
    // FILE of the same recording: the speech is the same speech, so what has drifted
    // is the alignment and not the tempo. Scaling durations would also make the two
    // anchored endpoints agree while every word between them slid out of sync, and
    // checkPlausibility's DurationOutlier rule would then fire on the entire line
    // for a correction the user asked for. Scaling is only correct when the two
    // timelines come from different performances, which is not this feature.
    //
    // It also makes the model uniform: a pinned word preserves its duration and so
    // does a free one. The distinction the model draws is "does this word have a
    // time the user vouched for", not "does its length change".
    const auto second = document.pinAnchor(8, 4.0625);
    QVERIFY2(second.has_value(), second.has_value() ? "" : second.error().describe().c_str());
    QVERIFY(sameBits(second->deltaSeconds, 1.8125)); // 4.0625 - 2.25
    QCOMPARE(second->movedWords, std::size_t{6});    // w2..w7; w0 is outside the span
    QCOMPARE(document.anchorCount(), std::size_t{2});

    struct Expect {
        usize index;
        f64 start;
        f64 end;
    };
    const Expect expected[] =
            {
                    {0, 0.2500, 0.4375}, // outside the span: identity
                    {1, 0.5625, 0.7500}, // anchored: onset exact, duration 0.1875 rigid
                    {2, 1.0625, 1.2500}, {3, 1.5625, 1.7500}, {4, 2.0625, 2.2500},
                    {5, 2.5625, 2.7500}, {6, 3.0625, 3.2500}, {7, 3.5625, 3.7500},
                    {8, 4.0625, 4.2500}, // anchored: 4.0625 + 0.1875
    };
    for (const Expect& row : expected) {
        const TimedWord& word = document.words()[row.index];
        QVERIFY2(sameBits(word.startSeconds, row.start),
                 qPrintable("word " + QString::number(static_cast<int>(row.index)) +
                            " start: got " + QString::number(word.startSeconds, 'f', 6) + " want " +
                            QString::number(row.start, 'f', 6)));
        QVERIFY2(sameBits(word.endSeconds, row.end),
                 qPrintable("word " + QString::number(static_cast<int>(row.index)) + " end: got " +
                            QString::number(word.endSeconds, 'f', 6) + " want " +
                            QString::number(row.end, 'f', 6)));
    }

    // The reflow is still a plausible document: monotone, no overlaps, and every
    // duration above the frame floor. This is the invariant that makes the model
    // safe to use -- the map is monotone, so it cannot reorder words.
    const PlausibilityReport afterReflow = checkPlausibility(document.words());
    QVERIFY2(afterReflow.empty(), qPrintable(QString::fromStdString(std::string(
                                          violationKindName(afterReflow.findings.front().kind)))));
    for (const TimedWord& word : document.words()) {
        QVERIFY2(word.endSeconds > word.startSeconds, "no word lost its extent");
    }

    // ── the guard rails, on the same fixture ───────────────────────────────
    // A pin at or before the previous anchor is refused, and `clampPin` returns
    // the nearest admissible value rather than the bound itself -- `nextafter`,
    // because a value *equal* to the bound is one `pinAnchor` would refuse.
    const auto crossing = document.pinAnchor(4, 0.1);
    QVERIFY(!crossing.has_value());
    QCOMPARE(crossing.error().code, TimingError::WouldCrossAnchor);
    QVERIFY(sameBits(document.clampPin(4, 0.1),
                     std::nextafter(0.5625, std::numeric_limits<f64>::infinity())));

    const auto crossingRight = document.pinAnchor(4, 4.0625);
    QVERIFY(!crossingRight.has_value());
    QCOMPARE(crossingRight.error().code, TimingError::WouldCrossAnchor);

    const auto nonFinite = document.pinAnchor(4, std::numeric_limits<f64>::quiet_NaN());
    QVERIFY(!nonFinite.has_value());
    QCOMPARE(nonFinite.error().code, TimingError::NonFiniteTime);

    // ── step 3: releasing the OUTERMOST anchor moves nothing at all ────────
    //
    // With w1 the only anchor left there is no span to interpolate against, so the
    // released word keeps its position and simply becomes free. Documented in the
    // header, and pinned here because it is the most surprising thing the model
    // does.
    const auto released = document.removeAnchor(8);
    QVERIFY2(released.has_value(), released.has_value() ? "" : released.error().describe().c_str());
    QVERIFY(sameBits(released->deltaSeconds, 0.0));
    QCOMPARE(released->movedWords, std::size_t{0});
    QVERIFY(sameBits(document.words()[8].startSeconds, 4.0625));
    QVERIFY(sameBits(document.words()[8].endSeconds, 4.25));
    QCOMPARE(document.words()[8].pin, Pin::None);
    QCOMPARE(document.anchorCount(), std::size_t{1});

    // Releasing the last anchor is still an undoable edit, and its target is
    // reported so a UI can label the entry.
    QCOMPARE(document.undoDepth(), std::size_t{3});
}

// ─────────────────────────────────────────────────────────────────────────────

void TestLyricTiming::undoRestoresABitwiseIdenticalDocumentAfterAMultiStepDrag() {
    // Gaps of 1/16 leave room for the drag to move without clamping, which matters
    // because a clamped nudge is still recorded -- the test would be asserting a
    // different thing.
    const std::vector<TimedWord> original = {
            makeWord("a", 1.0000, 1.2500), makeWord("b", 1.3125, 1.5625),
            makeWord("c", 1.6250, 1.8750), makeWord("d", 1.9375, 2.1875),
            makeWord("e", 2.2500, 2.5000), makeWord("f", 2.7500, 3.0000),
    };

    LyricTimingDocument document = LyricTimingDocument::adopt(original);
    QCOMPARE(document.undoDepth(), std::size_t{0});
    QVERIFY(!document.canUndo());
    QVERIFY(!document.canRedo());

    // Edit 1: a pin. Its end is 1.625, which is exactly where w2 starts -- a clean
    // document with no overlap to explain away.
    QVERIFY(document.pinAnchor(1, 1.375).has_value());
    QCOMPARE(document.undoDepth(), std::size_t{1});

    // Edit 2: a drag. 128 separate nudges, one gesture.
    constexpr f64 kStep = 1.0 / 1024.0; // dyadic, so the total is exact
    const usize depthBeforeDrag = document.undoDepth();
    document.beginInteraction("bead drag");
    QCOMPARE(std::string(document.interactionLabel()), std::string("bead drag"));
    for (usize i = 0; i < 128; ++i) {
        const auto outcome = document.nudgeWord(4, kStep);
        QVERIFY2(outcome.has_value(),
                 outcome.has_value() ? "" : outcome.error().describe().c_str());
        QVERIFY2(!outcome->clamped,
                 "the fixture leaves headroom, so a clamp here means the fixture changed");
    }
    document.endInteraction();

    // The whole gesture is ONE step. This is the assertion the coalescing rule
    // exists for: without the bracket this would be 128 steps and 127 undos.
    QCOMPARE(document.undoDepth() - depthBeforeDrag, std::size_t{1});
    // 128 * (1/1024) = 1/8 exactly.
    QVERIFY(sameBits(document.words()[4].startSeconds, 2.2500 + 0.125));
    QVERIFY(sameBits(document.words()[4].endSeconds, 2.5000 + 0.125));
    // A nudge promotes its word to a full pin, which is what lets it survive a later
    // reflow. One constraint class, not two.
    QCOMPARE(document.words()[4].pin, Pin::Full);

    // Edit 3: unpin w1. Nodes are now just w4, so nothing moves.
    QVERIFY(document.removeAnchor(1).has_value());
    QCOMPARE(document.undoDepth(), std::size_t{3});
    QCOMPARE(document.anchorCount(), std::size_t{1});

    const std::vector<TimedWord> afterAllEdits = snapshotOf(document);
    QVERIFY(!sameBits(afterAllEdits, original));

    // A zero nudge is refused rather than recorded. A QML animated `value` reports
    // its *starting* value as a change, so this is what stops a slider from opening
    // a history entry before the user has touched it.
    const auto zero = document.nudgeWord(2, 0.0);
    QVERIFY(!zero.has_value());
    QCOMPARE(zero.error().code, TimingError::ZeroNudge);
    QCOMPARE(document.undoDepth(), std::size_t{3});

    // Undo all three, and the document must come back BIT FOR BIT.
    for (usize i = 0; i < 3; ++i) {
        QVERIFY(document.undo().has_value());
    }
    QCOMPARE(document.undoDepth(), std::size_t{0});
    QVERIFY(!document.canUndo());
    QCOMPARE(document.redoDepth(), std::size_t{3});
    QVERIFY2(sameBits(snapshotOf(document), original),
             "undo replayed the snapshot rather than restoring it");

    const auto past = document.undo();
    QVERIFY(!past.has_value());
    QCOMPARE(past.error().code, TimingError::NothingToUndo);

    // Redo all three: back to exactly the state after the last edit.
    for (usize i = 0; i < 3; ++i) {
        QVERIFY(document.redo().has_value());
    }
    QVERIFY(!document.canRedo());
    QVERIFY2(sameBits(snapshotOf(document), afterAllEdits),
             "redo did not reproduce the state it left");

    // A new edit after an undo clears the redo branch and opens a fresh chain. If
    // `chain_` were not broken by `undo`, the next edit would merge into the entry
    // *below* the one that was just restored -- the classic off-by-one-gesture bug.
    QVERIFY(document.undo().has_value());
    QVERIFY(document.undo().has_value());
    QCOMPARE(document.undoDepth(), std::size_t{1});
    QVERIFY(document.canRedo());
    QVERIFY(document.pinAnchor(2, 1.6875).has_value());
    QCOMPARE(document.undoDepth(), std::size_t{2});
    QVERIFY(!document.canRedo());
}

// ─────────────────────────────────────────────────────────────────────────────

void TestLyricTiming::plausibilityRejectsTheNamedViolationsAndAcceptsAGoodDocument() {
    // ── a known-good document produces nothing at all ──────────────────────
    //
    // Six words, 1/16-second gaps, durations between 0.26 and 0.34, media 12.0 s.
    const std::vector<TimedWord> good = {
            makeWord("one", 1.00, 1.30),   makeWord("two", 1.32, 1.60),
            makeWord("three", 1.62, 1.94), makeWord("four", 1.96, 2.26),
            makeWord("five", 2.28, 2.54),  makeWord("six", 2.56, 2.90),
    };
    const PlausibilityReport goodReport = checkPlausibility(good, 12.0);
    QVERIFY2(goodReport.empty(), qPrintable(QString::fromStdString(std::string(
                                         violationKindName(goodReport.findings.front().kind)))));
    QVERIFY(goodReport.ok());
    QCOMPARE(goodReport.violationCount(), std::size_t{0});

    // ── and one violation per named rule ──────────────────────────────────
    const std::vector<TimedWord> bad = {
            // start < 0
            makeWord("negative", -0.10, 0.20),
            // non-finite start
            makeWord("nan", std::numeric_limits<f64>::quiet_NaN(), 1.00),
            // end before start
            makeWord("backwards", 1.10, 1.00),
            // 5 ms: below the ASS centisecond quantum, so not representable at all
            makeWord("sub-quantum", 2.00, 2.005),
            // 12 ms: representable, but shorter than one 60 fps frame
            makeWord("sub-frame", 2.50, 2.512),
            // 600 ms, and it overlaps the next word by 500 ms
            makeWord("long", 3.00, 3.60),
            makeWord("overlapped", 3.10, 3.90),
            // 3.00 s against a 0.30 s median -> 10x, over the 8x limit. Also ends after
            // the declared 5.0 s media duration.
            makeWord("outlier", 4.00, 7.00),
            // starts BEFORE the previous word
            makeWord("out-of-order", 3.95, 4.25),
    };

    const PlausibilityReport report = checkPlausibility(bad, 5.0);

    // Nine findings, in the order the rules run: the per-word pass (which also
    // carries BeyondDocumentEnd), then the pairwise pass, then the outlier pass.
    QCOMPARE(report.findings.size(), std::size_t{9});
    QCOMPARE(report.violationCount(), std::size_t{4});
    QCOMPARE(report.warningCount(), std::size_t{5});
    QVERIFY(!report.ok());

    const ViolationKind expectedKinds[] = {
            ViolationKind::NegativeTime,     ViolationKind::NonFiniteTime,
            ViolationKind::NegativeDuration, ViolationKind::SubQuantumDuration,
            ViolationKind::SubFrameDuration, ViolationKind::BeyondDocumentEnd,
            ViolationKind::Overlap,          ViolationKind::OutOfOrderStart,
            ViolationKind::DurationOutlier,
    };
    const Severity expectedSeverities[] = {
            Severity::Violation, Severity::Violation, Severity::Violation,
            Severity::Warning,   Severity::Warning,   Severity::Warning,
            Severity::Warning,   Severity::Violation, Severity::Warning,
    };
    const usize expectedWordIndex[] = {0, 1, 2, 3, 4, 7, 5, 7, 7};
    const usize expectedOtherIndex[] = {kNoSecondWord,
                                        kNoSecondWord,
                                        kNoSecondWord,
                                        kNoSecondWord,
                                        kNoSecondWord,
                                        kNoSecondWord,
                                        6,
                                        8,
                                        kNoSecondWord};

    for (usize i = 0; i < 9; ++i) {
        const PlausibilityFinding& finding = report.findings[i];
        QCOMPARE(finding.kind, expectedKinds[i]);
        QCOMPARE(finding.severity, expectedSeverities[i]);
        QCOMPARE(finding.wordIndex, expectedWordIndex[i]);
        QCOMPARE(finding.otherIndex, expectedOtherIndex[i]);
    }

    // The thresholds travel with the findings, so a UI can name the number instead
    // of hard-coding it.
    QVERIFY(sameBits(report.findings[3].limit, kAssCentisecondSeconds)); // sub-quantum
    QVERIFY(sameBits(report.findings[4].limit, kSubFrameSeconds));       // sub-frame
    QVERIFY(sameBits(report.findings[5].limit, 5.0));                    // media length
    QVERIFY(sameBits(report.findings[6].limit, kOverlapToleranceSeconds));

    // `QCOMPARE` is right here and wrong in the undo test: these are derived
    // magnitudes with a rounding tail, not stored values being replayed.
    QCOMPARE(report.findings[6].measured, 0.50); // 3.60 - 3.10, the overlap
    QCOMPARE(report.findings[8].limit, 2.40);    // median 0.30 * kDurationOutlierFactor

    // Exactly one word runs past the media length, and it is the outlier.
    QCOMPARE(countOf(report, ViolationKind::BeyondDocumentEnd), std::size_t{1});
    QCOMPARE(countOf(report, ViolationKind::Overlap), std::size_t{1});

    // A sub-quantum word is ALSO sub-frame, and is reported once rather than twice.
    QCOMPARE(countOf(report, ViolationKind::SubQuantumDuration), std::size_t{1});
    QCOMPARE(countOf(report, ViolationKind::SubFrameDuration), std::size_t{1});

    // An empty document is not a document with a violation.
    QVERIFY(checkPlausibility(std::span<const TimedWord>{}).empty());
}

// ─────────────────────────────────────────────────────────────────────────────

void TestLyricTiming::theSerialisationRoundTripIsExactOnEveryField() {
    LyricTimingDocument document = LyricTimingDocument::adopt({
            // 0.1 and 0.30000000000000004 are the two values a naive formatter gets
            // wrong: toml++ prints at max_digits10 (17), so they survive.
            makeWord("plain", 0.1, 0.30000000000000004, Pin::Onset),
            makeWord("a \"quoted\" \\ backslash\nnewline\ttab — ünïcode 日本語", 0.5, 0.75,
                     Pin::Full),
            makeWord("", 0.75, 0.7509765625),       // empty text, dyadic fraction
            makeWord("日本語", 1.0009765625, 2.0),  // UTF-8 round trip
            makeWord("x", 3.3333333333333335, 3.5), // 17 significant digits, no shorter form
    });
    document.setSongId("clip-1");
    document.setTitle("Title \"with\" quotes");
    document.setArtist("Artist");
    document.setMediaDurationSeconds(214.7483647);

    const auto encoded = encodeDocument(document);
    QVERIFY2(encoded.has_value(), encoded.has_value() ? "" : encoded.error().describe().c_str());
    const std::string bytes = *encoded;

    const auto checkIdentity = [&document](const LyricTimingDocument& got) {
        QCOMPARE(got.songId(), std::string("clip-1"));
        QCOMPARE(got.title(), std::string("Title \"with\" quotes"));
        QCOMPARE(got.artist(), std::string("Artist"));
        QVERIFY(got.mediaDurationSeconds().has_value());
        QVERIFY(sameBits(*got.mediaDurationSeconds(), 214.7483647));
        QCOMPARE(got.wordCount(), std::size_t{5});
        for (std::size_t i = 0; i < got.wordCount(); ++i) {
            QVERIFY2(sameBits(got.words()[i].startSeconds, document.words()[i].startSeconds),
                     qPrintable("word " + QString::number(static_cast<int>(i)) + " start"));
            QVERIFY2(sameBits(got.words()[i].endSeconds, document.words()[i].endSeconds),
                     qPrintable("word " + QString::number(static_cast<int>(i)) + " end"));
        }
    };

    // ── in-memory round trip ───────────────────────────────────────────────
    const DocumentDecode decoded = decodeDocument(bytes);
    QVERIFY2(decoded.ok(), decoded.ok() ? "" : decoded.detail.c_str());
    QCOMPARE(decoded.major, kTimingSchemaMajor);
    QCOMPARE(decoded.minor, kTimingSchemaMinor);
    QCOMPARE(decoded.document.songId(), document.songId());
    QCOMPARE(decoded.document.wordCount(), document.wordCount());
    for (std::size_t i = 0; i < document.wordCount(); ++i) {
        QCOMPARE(decoded.document.words()[i].text, document.words()[i].text);
        QCOMPARE(decoded.document.words()[i].pin, document.words()[i].pin);
    }
    checkIdentity(decoded.document);

    // Byte-stable: re-encoding the decoded document must produce the same bytes,
    // which is a stronger claim than "the values survived" -- it means the writer
    // and the reader agree on the file, not merely on the numbers.
    const auto reencoded = encodeDocument(decoded.document);
    QVERIFY(reencoded.has_value());
    QCOMPARE(*reencoded, bytes);

    // ── through a file, atomically ─────────────────────────────────────────
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const fs::path path =
            fs::path(dir.path().toStdString()) / ("probe" + std::string(kTimingSuffix));
    const auto saved = saveDocument(document, path);
    QVERIFY2(saved.has_value(), saved.has_value() ? "" : saved.error().describe().c_str());

    const auto loaded = loadDocument(path);
    QVERIFY2(loaded.has_value(), loaded.has_value() ? "" : loaded.error().describe().c_str());
    checkIdentity(*loaded);
    QVERIFY2(sameBits(snapshotOf(*loaded), snapshotOf(document)),
             "the on-disk round trip lost or altered a field");

    // ── refusals, because a document on disk can be anything ───────────────
    //
    // A bumped major is refused in BOTH directions. Reading it as v1 would not
    // warn -- it would fabricate, and a fabricated correction document is worse
    // than one the user knows is unreadable.
    QVERIFY(bytes.find("schema_major = 1") != std::string::npos);
    std::string bumped = bytes;
    bumped.replace(bumped.find("schema_major = 1"), std::string("schema_major = 1").size(),
                   "schema_major = 7");
    const DocumentDecode tooNew = decodeDocument(bumped);
    QVERIFY(!tooNew.ok());
    QCOMPARE(tooNew.status, DocumentDecode::Status::UnsupportedVersion);
    QCOMPARE(tooNew.major, u32{7});

    // A missing version key is REFUSED, not defaulted. This is the hole a
    // `value_or(kSchemaMajor)` would leave open, and it is why `[[words]]` and both
    // version keys are required rather than defaulted.
    std::string headerless = bytes;
    headerless.erase(headerless.find("schema_major = 1"),
                     headerless.find("schema_minor = 0") - headerless.find("schema_major = 1") +
                             std::string("schema_minor = 0\n").size());
    const DocumentDecode versionless = decodeDocument(headerless);
    QVERIFY(!versionless.ok());
    QCOMPARE(versionless.status, DocumentDecode::Status::BadBody);

    // An unknown pin slug is refused too. Defaulting it to `none` would *release* a
    // pin the user set, and the failure would stay invisible until a later reflow
    // moved the word.
    std::string badPin = bytes;
    // toml++ renders a plain string with SINGLE quotes: `pin = 'full'`. This
    // asserted `pin = "full"` and so failed on every document, which is a useful
    // reminder that a marker assertion is only as good as the writer's formatting.
    // The value is verified independently above -- decoding yields Pin::Full.
    const std::string marker = "pin = 'full'";
    QVERIFY(badPin.find(marker) != std::string::npos);
    badPin.replace(badPin.find(marker), marker.size(), "pin = \"pinned\"");
    const DocumentDecode badSlug = decodeDocument(badPin);
    QVERIFY(!badSlug.ok());
    QCOMPARE(badSlug.status, DocumentDecode::Status::BadBody);

    // A non-finite time is refused at write time even though TOML could express it:
    // persisting one would make the defect durable.
    LyricTimingDocument poisoned = LyricTimingDocument::adopt(
            {makeWord("bad", 0.0, std::numeric_limits<f64>::infinity())});
    const auto refused = encodeDocument(poisoned);
    QVERIFY(!refused.has_value());
    QCOMPARE(refused.error().code, TimingError::NonFiniteTime);

    const auto missing = loadDocument(fs::path(dir.path().toStdString()) / "absent.chadtiming");
    QVERIFY(!missing.has_value());
    QCOMPARE(missing.error().code, TimingError::MissingFile);

    // ── the empty document is a real state, not an error ───────────────────
    //
    // A song with no lyrics, and a document the user has cleared, both encode to
    // zero words. toml++ emits a zero-element array as an inline `words = []`
    // rather than as `[[words]]` (`array::is_homogeneous` returns false for an empty
    // array, so `is_array_of_tables` is false and the non-inline branch is skipped),
    // which parses back as an array -- so the round trip holds without a special
    // case on either side.
    const LyricTimingDocument empty = LyricTimingDocument::adopt({});
    const auto emptyBytes = encodeDocument(empty);
    QVERIFY2(emptyBytes.has_value(),
             emptyBytes.has_value() ? "" : emptyBytes.error().describe().c_str());
    QVERIFY2(emptyBytes->find("words = []") != std::string::npos,
             qPrintable(QString::fromStdString(*emptyBytes)));
    const DocumentDecode emptyDecoded = decodeDocument(*emptyBytes);
    QVERIFY2(emptyDecoded.ok(), emptyDecoded.ok() ? "" : emptyDecoded.detail.c_str());
    QCOMPARE(emptyDecoded.document.wordCount(), std::size_t{0});
    QVERIFY(checkPlausibility(emptyDecoded.document.words()).empty());
}

int runTestLyricTiming(int argc, char** argv) {
    TestLyricTiming test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_LyricTiming.moc"
