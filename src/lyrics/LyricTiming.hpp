#pragma once
/**
 * @file LyricTiming.hpp
 * @file Purpose: an editable, anchored word-timing document -- the correction
 *                half of the karaoke feature.
 *
 * @section Why this exists
 * The tree can already *render* word timings and cannot *fix* one.
 * `LyricsData` carries per-word `startTime`/`endTime` from Suno's captured
 * aligned-lyrics payload, `LyricsExport::toAssDocument` writes them as per-word
 * `\kf` karaoke tags, and `VideoRecorderFFmpeg` muxes that document as a soft
 * subtitle track and can burn it in. There are **zero `waveform` hits in
 * `src/qml/`**, no editor, and no way to alter a single time. The timings are
 * write-once from a capture.
 *
 * That is the whole complaint this file addresses: *captured timings always
 * drift against a re-encoded local file, and that is the #1 karaoke
 * complaint.* Drift is not a bug in the capture -- a re-encode changes the
 * container, the encoder delay and the trim, so a document that was perfect for
 * the stream is a few hundred milliseconds out for the file on disk, and the
 * error is not constant across the track.
 *
 * Everything here is pure, headless, device-free and Qt-free. There is no
 * widget, no audio device and no QML type in this header, deliberately: the
 * logic has to be testable without a display, and a correction model that can
 * only be exercised by clicking things is a correction model nobody can verify.
 *
 * @section Precision, and why this is `double` where `LyricsData` is `float`
 * `LyricsData`'s word times are `f32`, and that is defensible for *display*:
 * an f32 has ~7 significant digits, so 0.1 s + 200 words of accumulated
 * arithmetic holds a single word to about 1e-8 s. It is not defensible for the
 * layer whose entire subject is drift. Every time here is `f64`, and the
 * arithmetic is arranged so that a reflow is a **single affine map evaluated
 * once per word**, never a running accumulation — so the error after a
 * three-minute document is the error of one multiply-add, not of two hundred.
 *
 * The conversion boundary is real and is not hidden: `LyricsData` is `f32`, so
 * a corrected document that is handed back to the ASS writer is narrowed once,
 * at the boundary. That narrowing is the reason the correction layer must be
 * the authority on timing rather than a view over the existing `LyricsData`
 * fields — see @section What this deliberately does not do.
 *
 * @section The reflow model, in one sentence
 * **Between two anchors the old timeline is mapped onto the new one linearly;
 * outside the outermost anchors nothing moves.** That is the whole algorithm,
 * and it is deliberately the simplest model that honours every anchor exactly,
 * needs no tuning constant, and is checkable by hand with a pencil.
 *
 * The alternatives were measured against that bar rather than assumed:
 *
 *  - *Least-squares with a smoothness penalty.* Minimising
 *    `sum((t - t0)^2) + lambda * sum(second difference)^2` subject to the
 *    anchors is solvable and is what a signal-processing person would reach
 *    for. It is rejected because `lambda` is a tuning constant with no physical
 *    meaning, its solution cannot be derived by hand, and the plain
 *    `sum((t - t0)^2)` term drives it toward "do not move anything", so the
 *    answer is almost entirely an artefact of `lambda`. A user asking "why did
 *    my word move 90 ms?" would get no answer.
 *  - *Syllable-weighted distribution.* Distributing a span's discrepancy in
 *    proportion to syllable count is the linguistically correct shape, and the
 *    repo cannot compute it: English syllable counting is a heuristic, not a
 *    fact, and a wrong syllable count silently distorts the entire span rather
 *    than failing. It would also need a syllable counter the tree does not have.
 *  - *Pure affine rescaling of the whole span.* This is what the implementation
 *    does. A ratio is the wrong shape for speech *inside* a span — a held
 *    vowel and a consonant cluster get the same proportional treatment — and
 *    that is the model's real cost, stated plainly rather than defended. It is
 *    chosen because it needs no input the repository cannot compute, it is
 *    exactly invertible, it preserves relative proportions inside the span
 *    perfectly, and a reader can verify any word of it with one multiplication.
 *
 * A word whose distance from the span's left anchor is `d` moves by
 * `d * (ratio - 1)`, so the weighting is *automatic*: a word far from the anchor
 * moves more than one near it, with no syllable counter involved. That is the
 * property that makes the affine model the right default here.
 *
 * @section What "outside the outermost anchors nothing moves" costs
 * This is the sharpest trade in the file and it is worth being exact about it.
 * With one anchor in the document, **a pin moves exactly one word.** Correcting
 * a phrase-wide offset therefore takes one pin per word. That sounds bad until
 * you say what the alternative asserts: to move the tail on one observation, the
 * model has to claim that every word the user never looked at is also wrong by
 * the same amount. This layer refuses to make that claim. The operation that
 * *does* assert it is `nudgeRipple`, and it says so in its name.
 *
 * Two consequences that are consequences, not bugs:
 *
 *  - `removeAnchor` on the document's **outermost** anchor changes no time at
 *    all. Releasing the last node leaves the map's unbounded tail with nothing
 *    to interpolate against, so the released word keeps its position and simply
 *    becomes free to move on the next pin. It restores freedom; it does not move
 *    anything.
 *  - A pin can leave the pinned word **overlapping an unproven neighbour**. The
 *    map is monotone so it cannot reorder words or shorten an anchored word, but
 *    it deliberately does not drag an unproven neighbour out of the way. The
 *    honest response is to report it: `checkPlausibility` flags the overlap, and
 *    the fix is to pin the neighbour too — which is the correct workflow
 *    anyway, because the second pin is a second piece of evidence.
 *
 * @section The pinned word's own extent translates rigidly
 * A pin at `index` fixes that word's **onset**. Its **offset** is set to
 * `oldOffset + (newOnset - oldOnset)`: the word keeps its duration. This is the
 * one place the model goes beyond "interpolate between anchors", and it is
 * forced by arithmetic rather than chosen. If the offset were mapped by the same
 * span function while the span's right end is identity, moving the last anchor
 * later would leave the word ending before it starts. Translating the word rigidly
 * is the only reading consistent with the pin: *the user said when this word
 * begins, and said nothing about how long it lasts, so its length is preserved.*
 *
 * @section Anchors are the only constraint class
 * There is one kind of constraint, not two, and that is load-bearing. A nudge
 * **promotes the word to `Pin::Full`**. Keeping a separate "user nudged this"
 * flag would mean a document carrying two competing notions of what is fixed,
 * and a later pin would rescale straight through a nudge and silently undo it.
 * With one class, "a time the user has vouched for" has exactly one
 * representation, and it survives.
 *
 * @section The price of a snapshot undo stack
 * Undo stores whole word-vector snapshots rather than inverse commands. A reflow
 * is not cheaply invertible once later pins exist — undoing a middle pin requires
 * re-deriving which words the span held open at the time — and an inverse-command
 * log would have to be correct about that or it corrupts the document. Measured
 * cost: `sizeof(TimedWord)` is 56 bytes, so a 200-word document costs ~11 KB per
 * undo step and the default `kMaxUndoDepth` of 128 caps the history at about
 * 1.4 MB. That is memory a desktop editor can spend, and it buys an undo that
 * cannot be subtly wrong.
 *
 * @section What this deliberately does not do
 * **It does not convert to or from `LyricsData`.** Three reasons, in order of
 * weight: `LyricsData`'s times are `f32`, so any round trip through it narrows
 * a value this file exists to keep exact; the flat word list here has no line
 * structure, so a conversion needs an index alignment between the two
 * representations that nothing in the tree currently guarantees; and the caller
 * that needs it is a QML bridge, which is a different file with a different
 * owner. `words()` is a `std::span` precisely so that bridge can be written
 * without this layer guessing.
 *
 * **It does not measure drift against real audio.** See `measureDrift`.
 */

#include "util/Types.hpp"

#include <cstddef>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace vc::lyricstiming {

// ─────────────────────────────────────────────────────────────────────────────
// Pins
// ─────────────────────────────────────────────────────────────────────────────

/// What the user has vouched for about one word.
///
/// The vocabulary is closed and small on purpose: an unbounded set of "how
/// pinned is this" values is a set of interpolation modes nobody would test.
enum class Pin : u8 {
    /// Free. The word is mapped by whatever span reflow currently governs it.
    None,
    /// The onset is fixed. The word is a node of the reflow map and moves only
    /// through an explicit re-pin. Produced by `pinAnchor`.
    Onset,
    /// Onset *and* offset are fixed. Produced by every `nudge*` entry point,
    /// and the only way a translation survives a later reflow.
    Full,
};

/// One word's timing, in seconds from the start of the track.
struct TimedWord {
    std::string text;
    f64 startSeconds{0.0};
    f64 endSeconds{0.0};
    Pin pin{Pin::None};

    /// The onset this word had when the document was adopted or decoded --
    /// the position the CURRENT pin targets are expressed against.
    ///
    /// Load-bearing, and the fix for a bug this field's absence caused. `reflow`
    /// rebuilds its span map from scratch on every pin, deriving the "before" side
    /// from the document as it currently stands. But a previous reflow already
    /// MOVED that document, so the left anchor's "before" onset was itself a
    /// reflowed value rather than an original one, and the slope of the span came
    /// out wrong for every free word.
    ///
    /// Measured, pinning word 1 to 0.5625 and then word 8 to 4.0625 on a 9-word
    /// fixture whose words sit on a 0.25 s grid. With original onsets the span is
    /// 0.50 -> 2.25 old and 0.5625 -> 4.0625 new, so the slope is exactly 2.0 and
    /// word 2 maps to 0.5625 + (0.75 - 0.5) * 2 = 1.0625. Reading the left endpoint
    /// from the already-shifted document instead gave 0.50 -> 0.5625, a slope of
    /// 56/27, and word 2 landed at 0.951388889 -- wrong, and wrong in a way that
    /// looks like a plausible number rather than a failure.
    ///
    /// So the model is a function of (original positions, current anchor targets)
    /// and never of its own previous output. That is what makes progressive pinning
    /// converge on the same answer as pinning the outer anchors first.
    f64 baseStartSeconds{0.0};

    /// The offset this word had when the document was adopted or decoded, i.e. the
    /// pristine DURATION.
    ///
    /// Both halves are needed and neither can be derived from the other after the
    /// fact. `baseStartSeconds` alone is not enough for a rigid translation: the
    /// translation is `end = start + duration`, and once a previous reflow has
    /// moved `endSeconds` the current duration is the reflowed one, so re-pinning
    /// an already-anchored word compounds its old correction into its length.
    /// Measured: pinning word 1 to 0.5625 and then re-pinning it gave a duration of
    /// 0.3125 instead of 0.1875 -- a word that grew by two thirds without being
    /// asked to.
    ///
    /// NEVER written by an edit. `startSeconds`/`endSeconds` are the live values;
    /// this pair is the datum they are measured from.
    f64 baseEndSeconds{0.0};
};

// ─────────────────────────────────────────────────────────────────────────────
// Errors
// ─────────────────────────────────────────────────────────────────────────────

/// Why an edit or a file operation was refused.
///
/// Named rather than a bare string so a caller can branch on the class and a
/// test can assert the *kind*. The list is deliberately short: every entry here
/// is a refusal the caller can do something different about.
enum class TimingError : u8 {
    /// The word index is past the end of the document. Never clamped — a clamped
    /// index silently corrects a different word than the caller named.
    IndexOutOfRange,
    /// A NaN or an infinity arrived as a time. Refused rather than sanitised: a
    /// NaN time names no instant, and folding it to zero would fabricate one.
    NonFiniteTime,
    /// A time before 0.0.
    NegativeTime,
    /// The pin would place the word at or before its previous anchor, or at or
    /// after its next one. Anchors are ordered and this is what keeps them so.
    WouldCrossAnchor,
    /// `removeAnchor` on a word that is not anchored.
    NotAnchored,
    /// `nudge*` with a delta of exactly zero. Reported so the caller can skip a
    /// history entry rather than pushing an empty one — a QML animated property
    /// reports its *starting* value as a change.
    ZeroNudge,
    /// Nothing to undo / nothing to redo.
    NothingToUndo,
    NothingToRedo,
    /// The file declares a schema major this build does not implement. Refused in
    /// **both** directions, because reading a v2 file with v1 defaults does not
    /// warn, it fabricates.
    UnsupportedSchema,
    /// The file is not a timing document at all, or a required key is missing.
    MalformedBody,
    /// The document cannot be written, or the file cannot be opened.
    WriteFailed,
    MissingFile,
};

[[nodiscard]] std::string_view timingErrorName(TimingError error) noexcept;

/// An error plus the reason a human should see. The code is for branching; the
/// detail is for a status line.
struct TimingFault {
    TimingError code{TimingError::IndexOutOfRange};
    std::string detail;

    [[nodiscard]] std::string describe() const {
        return std::string(timingErrorName(code)) + ": " + detail;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Edit outcomes
// ─────────────────────────────────────────────────────────────────────────────

/// What a reflow actually did.
///
/// `movedWords` counts **free** words — those carrying `Pin::None` — whose onset
/// *or* offset changed. The offset is included because a word whose end slid while
/// its start did not is visibly different to a user, and a report that said
/// "nothing moved" would be a lie. Anchored words are excluded because a span
/// cannot move an anchor; if an anchored word's *offset* shifted, that is the
/// rigid translation of its own pin and it is reported as `deltaSeconds`.
struct ReflowOutcome {
    /// Signed change in the pinned word's own onset. Zero is legitimate: pinning
    /// a word where it already is is how a user establishes an anchor without
    /// moving anything.
    f64 deltaSeconds{0.0};
    usize movedWords{0};
};

/// What a nudge actually did, after neighbour clamping.
///
/// `requestedSeconds` and `appliedSeconds` are separately named because the
/// whole point of the struct is that they can differ, and a UI that shows only
/// the requested value would claim a correction it did not apply.
struct NudgeOutcome {
    f64 requestedSeconds{0.0};
    f64 appliedSeconds{0.0};
    usize movedWords{0};
    /// The request was pulled back because it would have crossed a neighbour.
    bool clamped{false};
};

// ─────────────────────────────────────────────────────────────────────────────
// The document
// ─────────────────────────────────────────────────────────────────────────────

/// The kinds of edit a history entry can record.
///
/// `Interaction` is a sentinel **no single edit produces**. It labels the one
/// entry a bracketed gesture collapses into, so a later merge recognises an open
/// bracket rather than a lone edit — and its presence in `undo_` is the test for
/// "the last entry is still open".
enum class EditKind : u8 {
    Pin,
    Unpin,
    Nudge,
    Interaction,
};

[[nodiscard]] std::string_view editKindName(EditKind kind) noexcept;

/// Undo steps retained.
///
/// Memory is the binding constraint, not usefulness: see the file header,
/// @section The price of a snapshot undo stack. 128 steps of a 200-word document
/// is about 1.4 MB.
inline constexpr usize kMaxUndoDepth = 128;

/// An ordered set of word timings with user anchors and a real undo history.
///
/// Not copy-cheap: the history is part of the object. Move it; do not copy it.
class LyricTimingDocument {
public:
    LyricTimingDocument() = default;

    /// Adopt a word list **verbatim**.
    ///
    /// No validation, no reflow, no history. This is the import path: a document
    /// built from a capture is exactly what the capture said, including whatever
    /// is wrong with it. `checkPlausibility` is how you find out, and refusing to
    /// import would mean the one document most in need of correction is the one
    /// that cannot be opened.
    [[nodiscard]] static LyricTimingDocument adopt(std::vector<TimedWord> words);

    // ── identity ──────────────────────────────────────────────────────────
    //
    // Not part of the undo history: the stack snapshots word timings only. Set
    // them at construction and treat them as metadata afterwards.

    [[nodiscard]] const std::string& songId() const noexcept { return songId_; }
    void setSongId(std::string value) noexcept { songId_ = std::move(value); }
    [[nodiscard]] const std::string& title() const noexcept { return title_; }
    void setTitle(std::string value) noexcept { title_ = std::move(value); }
    [[nodiscard]] const std::string& artist() const noexcept { return artist_; }
    void setArtist(std::string value) noexcept { artist_ = std::move(value); }
    /// Length of the audio this document is timed against, when it is known.
    /// `std::nullopt` disables the `BeyondDocumentEnd` rule rather than
    /// guessing a length — a guessed length would flag every word after it.
    [[nodiscard]] const std::optional<f64>& mediaDurationSeconds() const noexcept {
        return mediaDurationSeconds_;
    }
    void setMediaDurationSeconds(std::optional<f64> value) noexcept {
        mediaDurationSeconds_ = value;
    }

    // ── words ─────────────────────────────────────────────────────────────

    [[nodiscard]] std::span<const TimedWord> words() const noexcept { return words_; }
    [[nodiscard]] usize wordCount() const noexcept { return words_.size(); }
    /// Null when out of range. The one place a caller can get a null back, and
    /// it is `const`-only: nothing in this layer mutates a word in place except
    /// through an undoable edit.
    [[nodiscard]] const TimedWord* word(usize index) const noexcept {
        return index < words_.size() ? &words_[index] : nullptr;
    }
    /// Number of pinned words. A correction tool with 200 anchors in a 200-word
    /// document has stopped being a correction tool, and a UI wants to say so.
    [[nodiscard]] usize anchorCount() const noexcept;

    // ── edits ─────────────────────────────────────────────────────────────

    /// Pin `index`'s onset to `seconds` and reflow the span it now shares with
    /// its neighbouring anchors.
    ///
    /// The pinned word's *offset* is set to `oldOffset + delta` — see the file
    /// header, @section The pinned word's own extent translates rigidly.
    ///
    /// Refused when `seconds` would land at or before the previous anchor or at
    /// or after the next one, which is what keeps the anchor list ordered and the
    /// reflow map monotone. `clampPin` is the companion a slider wants.
    ///
    /// A zero delta is **not** refused: pinning a word where it already is is a
    /// legitimate way to add evidence without changing anything.
    [[nodiscard]] std::expected<ReflowOutcome, TimingFault> pinAnchor(usize index, f64 seconds);

    /// Release the pin on `index` and reflow the merged span.
    ///
    /// Releasing the document's outermost anchor moves no time at all — see the
    /// file header, @section What "outside the outermost anchors nothing moves"
    /// costs. The operation restores freedom; it does not move anything.
    [[nodiscard]] std::expected<ReflowOutcome, TimingFault> removeAnchor(usize index);

    /// Move one word alone, rigidly, preserving its duration.
    ///
    /// The default authority, and the one this layer recommends: it is the only
    /// scope whose effect a user can verify by hearing *the word they corrected*
    /// in a single repetition. The two broader scopes each assert something about
    /// words the user has not looked at.
    ///
    /// The applied delta is clamped so the word cannot cross its neighbours — one
    /// side is `previous.endSeconds - startSeconds`, the other
    /// `next.startSeconds - endSeconds` — and `NudgeOutcome::clamped` reports it.
    /// The word becomes `Pin::Full`, so a later reflow cannot rescale through it.
    [[nodiscard]] std::expected<NudgeOutcome, TimingFault> nudgeWord(usize index, f64 deltaSeconds);

    /// Move `[first, last]` inclusive, rigidly, as one undo step.
    ///
    /// This is the "whole line" authority. The layer holds a flat word list and
    /// has no line structure, so the bounds come from the caller that does —
    /// which is the right owner: `LyricsData` already has the line breaks, and
    /// duplicating them here would be a second thing to keep in step.
    [[nodiscard]] std::expected<NudgeOutcome, TimingFault> nudgeRange(usize first, usize last,
                                                                      f64 deltaSeconds);

    /// Move `index` and **every word after it**, rigidly.
    ///
    /// The global authority, and the one that answers "this whole section is
    /// late". Every moved word becomes `Pin::Full`, which is the real cost: a
    /// ripple converts the tail into fixed points, so later pins in that region
    /// re-pin rather than reflow. That is the conservative direction — it will
    /// not silently move something the user asserted — and `removeAnchor` on the
    /// tail restores the freedom.
    [[nodiscard]] std::expected<NudgeOutcome, TimingFault> nudgeRipple(usize index,
                                                                       f64 deltaSeconds);

    /// The nearest admissible pin time for `index`, for a UI that wants to clamp
    /// rather than refuse.
    ///
    /// Clamps into the **open** interval between the neighbouring anchors, using
    /// `std::nextafter` so the returned value is one representable step inside the
    /// bound rather than equal to it — equal would be refused by `pinAnchor`.
    /// Returns 0.0 for an out-of-range index or a non-finite proposal.
    [[nodiscard]] f64 clampPin(usize index, f64 proposedSeconds) const noexcept;

    // ── history ───────────────────────────────────────────────────────────
    //
    // Two mechanisms, because a drag needs both and neither alone is enough.

    /// Open an interaction. Everything edited until `endInteraction()` is **one**
    /// undo step, whatever its size or duration.
    ///
    /// This is the honest answer for a slider drag, and it is explicit rather
    /// than inferred: this layer has no opinion about what a drag is, and a
    /// time-window heuristic mis-splits a slow deliberate drag and merges two
    /// fast deliberate ones. QML calls this on press/grab and
    /// `endInteraction()` on release.
    ///
    /// Reentrant — a counter, not a flag, so a nested bracket cannot end the
    /// outer one.
    void beginInteraction(std::string_view label);
    void endInteraction() noexcept;

    /// Break the implicit coalescing chain without ending an interaction.
    void commit() noexcept;

    [[nodiscard]] bool canUndo() const noexcept { return !undo_.empty(); }
    [[nodiscard]] bool canRedo() const noexcept { return !redo_.empty(); }
    [[nodiscard]] usize undoDepth() const noexcept { return undo_.size(); }
    [[nodiscard]] usize redoDepth() const noexcept { return redo_.size(); }
    [[nodiscard]] const std::string& interactionLabel() const noexcept { return interactionLabel_; }

    /// Restore the document bit-for-bit. "Bit-for-bit" is not decoration: the
    /// word times are `f64` and a reflow that round-tripped through an
    /// accumulator would not reproduce, so the snapshot is stored verbatim and the
    /// test asserts it with `std::bit_cast`.
    [[nodiscard]] std::expected<void, TimingFault> undo();
    [[nodiscard]] std::expected<void, TimingFault> redo();
    void clearHistory() noexcept;

private:
    /// What a history entry coalesces on.
    struct EditStamp {
        EditKind kind{EditKind::Pin};
        usize target{0};

        [[nodiscard]] bool operator==(const EditStamp&) const noexcept = default;
    };

    struct Entry {
        EditStamp stamp;
        /// The state to restore. For an `undo` entry that is the state *before*
        /// the edit; for a `redo` entry it is the state *after*.
        std::vector<TimedWord> before;
    };

    [[nodiscard]] std::optional<usize> prevAnchorIndex(usize index) const noexcept;
    [[nodiscard]] std::optional<usize> nextAnchorIndex(usize index) const noexcept;

    /// Rewrite `candidate` with the reflow map applied, and report what moved.
    ///
    /// `old` is the pre-edit word list and `candidate` the post-edit one; the map is
    /// derived entirely from the two, because the node values come from `candidate`
    /// and the domain from `old`. Passing a single list would make the freshly
    /// pinned word's new onset look like its *original* one, and the span it opens
    /// would collapse to zero width.
    ///
    /// Must be called exactly once per edit, before any other mutation.
    [[nodiscard]] ReflowOutcome applyReflow(const std::vector<TimedWord>& old,
                                            std::vector<TimedWord>& candidate) const;

    void pushEdit(EditKind kind, usize target, std::vector<TimedWord> before);

    std::vector<TimedWord> words_;
    std::string songId_;
    std::string title_;
    std::string artist_;
    std::optional<f64> mediaDurationSeconds_;

    std::vector<Entry> undo_;
    std::vector<Entry> redo_;
    /// The stamp an implicit merge would extend. Cleared by `undo`, `redo` and
    /// `commit` — merging into the entry a restore just popped would land the
    /// next edit's pre-image two steps back.
    std::optional<EditStamp> chain_;
    usize interactionDepth_{0};
    std::string interactionLabel_;
};

// ─────────────────────────────────────────────────────────────────────────────
// Physiological plausibility
// ─────────────────────────────────────────────────────────────────────────────

/// The ASS format's own quantum. `LyricsExport::assEventText` already floors a
/// `\kf` tag at one centisecond "because `\kf0` is a degenerate tag that makes
/// renderers flash the syllable rather than show it", so a word shorter than this
/// **cannot be represented at all** — the document is describing something the
/// output cannot carry.
inline constexpr f64 kAssCentisecondSeconds = 0.01;

/// One rendered frame at the project's default 60 fps. `LyricsData::getProgress`
/// is sampled once per frame, so a word shorter than this never *sweeps*: it
/// snaps from unlit to lit. This is a property of the display path, not of
/// speech, and it is a different floor from the format quantum — a 12 ms word
/// survives `toAssDocument` exactly and is invisible in the app.
inline constexpr f64 kSubFrameSeconds = 1.0 / 60.0;

/// How much two words may overlap before it is worth reporting.
///
/// 40 ms = four times the format quantum, so sub-quantum overlap is arithmetic
/// noise rather than a fact about the audio. Above that it is reported as a
/// **warning, not a violation**, because two voices genuinely do overlap in this
/// genre and a checker that called that an error would be wrong about real
/// material. The precise threshold is a judgement; the quantum multiple is the
/// part with a number behind it.
inline constexpr f64 kOverlapToleranceSeconds = 0.04;

/// A word this many times longer than the document's median word is an outlier.
///
/// Self-calibrating on purpose. An absolute speech-rate ceiling would need a
/// measured figure for sung English that this repository does not have, and it
/// would be wrong for both ends of the range — a 4-second melisma in a ballad is
/// correct, and so is a 90-millisecond word in a fast passage. Real sung word
/// durations are heavily right-skewed with a thin extreme tail, so a ratio to the
/// document's own median separates "held on purpose" from "an instrumental gap got
/// assigned to a word" without any external constant.
inline constexpr f64 kDurationOutlierFactor = 8.0;

/// Words needed before the median is worth computing. A three-word line has no
/// meaningful median, and a rule that fired on it would be noise.
inline constexpr usize kMinWordsForDurationMedian = 8;

/// What is wrong with a timing, independent of *where*.
enum class ViolationKind : u8 {
    /// A time is NaN or infinite. Every numeric rule below is meaningless after
    /// this, so it is reported alone and the word is skipped.
    NonFiniteTime,
    /// A time is before 0.0.
    NegativeTime,
    /// `endSeconds < startSeconds`.
    NegativeDuration,
    /// Shorter than `kAssCentisecondSeconds`: not representable in the output.
    SubQuantumDuration,
    /// Shorter than `kSubFrameSeconds`: representable, but never visible sweeping.
    SubFrameDuration,
    /// Word `i + 1` starts before word `i` starts.
    OutOfOrderStart,
    /// Word `i + 1` starts before word `i` ends by more than the tolerance.
    Overlap,
    /// Duration exceeds `kDurationOutlierFactor` times the document median.
    DurationOutlier,
    /// The word ends after the declared media duration.
    BeyondDocumentEnd,
};

[[nodiscard]] std::string_view violationKindName(ViolationKind kind) noexcept;

/// `Violation` means the data cannot be what it claims. `Warning` means it can,
/// but a human should look.
enum class Severity : u8 {
    Violation,
    Warning,
};

/// Sentinel for `PlausibilityFinding::otherIndex` on a rule that concerns one
/// word only.
inline constexpr usize kNoSecondWord = static_cast<usize>(-1);

/// One thing worth telling the user about one word.
struct PlausibilityFinding {
    ViolationKind kind{ViolationKind::NonFiniteTime};
    Severity severity{Severity::Violation};
    /// The word the finding is about. For a pairwise rule this is the *earlier*
    /// word.
    usize wordIndex{0};
    /// The second word of a pairwise rule; `kNoSecondWord` for a per-word rule.
    usize otherIndex{kNoSecondWord};
    /// The offending quantity — a duration in seconds, an overlap in seconds, or
    /// the time itself for a per-word rule.
    f64 measured{0.0};
    /// The threshold it was compared against, so a UI can say "0.030 s, limit
    /// 0.010 s" without knowing the constants.
    f64 limit{0.0};
};

/// Everything `checkPlausibility` found, ordered by word index then by the order
/// the rules run in.
struct PlausibilityReport {
    std::vector<PlausibilityFinding> findings;

    [[nodiscard]] usize violationCount() const noexcept;
    [[nodiscard]] usize warningCount() const noexcept;
    /// True when nothing of `Severity::Violation` was found. Warnings do not make
    /// a document unusable — a real vocal double is a warning, not a fault.
    [[nodiscard]] bool ok() const noexcept { return violationCount() == 0; }
    [[nodiscard]] bool empty() const noexcept { return findings.empty(); }
    /// Whether any finding of this kind is present. For a UI that wants a summary.
    [[nodiscard]] bool has(ViolationKind kind) const noexcept;
};

/// Check a word list against the physiological and representational limits above.
///
/// Pure: a free function over a span rather than a document method, so it works
/// on a candidate list before it is committed and on any list at all. Note that
/// the rules are deliberately *not* a drift measurement — see `measureDrift`.
///
/// `mediaDurationSeconds` is `std::nullopt` for "unknown", which disables
/// `BeyondDocumentEnd` rather than guessing.
[[nodiscard]] PlausibilityReport
checkPlausibility(std::span<const TimedWord> words,
                  std::optional<f64> mediaDurationSeconds = std::nullopt);

// ─────────────────────────────────────────────────────────────────────────────
// Drift
// ─────────────────────────────────────────────────────────────────────────────

/// Observations below this confidence are discarded rather than averaged in.
///
/// The exact number is a judgement: an aligner that reports 0.2 for a word it is
/// guessing at is right often enough to be useless. What makes the threshold
/// defensible is that it is reported in the result — `rejectedObservations` — so
/// a caller can see how much evidence was discarded rather than having to trust
/// the cut.
inline constexpr f64 kMinimumAlignmentConfidence = 0.5;

/// One word, as an external analyser believes it was sung.
///
/// **The producer of these does not exist yet**, and that is the honest state of
/// drift measurement. See `measureDrift`.
struct AlignedWord {
    /// Index into the document's word list. An index out of range is a *refusal*,
    /// not a discard: it means the producer and the document disagree about what
    /// the words are, and averaging over a subset would produce a number about the
    /// wrong document.
    usize wordIndex{0};
    f64 seconds{0.0};
    /// [0, 1], as the producer reports it.
    f64 confidence{0.0};
};

enum class DriftStatus : u8 {
    /// A usable number was produced.
    Ok,
    /// The document is empty.
    NoWords,
    /// Fewer than `kMinimumObservationsForDrift` observations survived the
    /// confidence cut.
    TooFewObservations,
    /// An observation named a word index the document does not have.
    IndexOutOfRange,
};

[[nodiscard]] std::string_view driftStatusName(DriftStatus status) noexcept;

/// Offsets below this are indistinguishable from "already aligned". 20 ms is a
/// judgement about what a viewer can perceive in a karaoke highlight; it is not a
/// measurement.
inline constexpr f64 kAlignedWithinSeconds = 0.020;

/// Observations required before a drift figure means anything.
inline constexpr usize kMinimumObservationsForDrift = 8;

struct WordDrift {
    usize wordIndex{0};
    /// `observed - document`. Signed, because "300 ms late" and "300 ms early"
    /// are different problems with different fixes.
    f64 offsetSeconds{0.0};
    f64 confidence{0.0};
};

struct DriftReport {
    DriftStatus status{DriftStatus::NoWords};

    usize usableObservations{0};
    /// Discarded for being below `kMinimumAlignmentConfidence`. Reported so the
    /// confidence cut is visible.
    usize rejectedObservations{0};

    /// Signed median offset. The median and not the mean because a handful of
    /// catastrophically wrong words should move the *report* of a problem, not the
    /// *summary* of one.
    f64 medianOffsetSeconds{0.0};
    /// Median absolute deviation from the median. The spread, so "everything is
    /// 200 ms late" and "half is fine and half is a second out" are distinguishable.
    f64 medianAbsoluteDeviationSeconds{0.0};
    /// Largest `|offset|`, signed.
    f64 worstOffsetSeconds{0.0};
    usize worstWordIndex{0};

    /// Populated in document order.
    std::vector<WordDrift> perWord;

    [[nodiscard]] bool ok() const noexcept { return status == DriftStatus::Ok; }
};

/// Compare a timing document against an external analyser's per-word alignment.
///
/// @section Can this honestly be computed? Not without an aligner, and this file
/// does not fake it
/// "How far has this drifted?" against a re-encoded file needs three things:
/// decoding (available — `AudioFileDecoder`), onset detection (available in
/// principle, and the pffft setup in `AudioAnalyzer` is the start of it), and
/// **assignment of onsets to words**. The third is the wall. It is either forced
/// alignment, which needs an acoustic model — Whisper CTC or similar, a
/// multi-hundred-megabyte dependency and a model fetch — or a heuristic that
/// assigns the Nth detected onset to the Nth word, which is precisely wrong in
/// the cases that matter: a melisma produces one onset and five words, and a rap
/// passage produces fewer onsets than words. Any number produced without one of
/// those is a **fabricated correlation score**, and a fabricated drift number in
/// a tool whose entire purpose is honest timing is worse than no number at all.
///
/// So this function defines the interface and refuses to guess. It is a pure
/// function of the document and the analyser's output, which means the moment a
/// real aligner exists it is a one-line call and nothing here changes. The
/// refusal thresholds are part of that: `kMinimumObservationsForDrift` exists
/// because a median over three observations is not a measurement, and returning a
/// number anyway is the failure mode being avoided.
///
/// What it *does* report honestly is the shape of the disagreement: a signed
/// median, a spread, and the worst word. Those three answer "is the whole track
/// late, or is one word wrong" — which is the question a correction tool is
/// actually for.
[[nodiscard]] DriftReport measureDrift(std::span<const TimedWord> document,
                                       std::span<const AlignedWord> observed);

// ─────────────────────────────────────────────────────────────────────────────
// Serialisation
// ─────────────────────────────────────────────────────────────────────────────

/// Bumped for a breaking change to the field set or its meaning.
inline constexpr u32 kTimingSchemaMajor = 1;
/// Bumped for an **additive** change only. A minor bump may add a key with a
/// documented default; it may never remove one, rename one, or change one's
/// meaning. That is what makes tolerating an unknown minor sound.
inline constexpr u32 kTimingSchemaMinor = 0;
/// Sidecar extension. Distinct from `DownloadQueue`'s `.part` ("being written
/// now") and `RenderJob`'s `.chadrjob`, for the same reason: `.chadtiming` is
/// what is left behind afterwards.
inline constexpr const char* kTimingSuffix = ".chadtiming";

/// The document as TOML bytes.
///
/// Refuses a document holding a non-finite time. TOML *can* express `nan` and
/// `inf`, so this would round-trip, but a timing document containing one is a
/// defect and the honest thing is to refuse to write it rather than persist a
/// value no renderer can use.
///
/// The round trip is exact and the reason is checkable rather than hoped for:
/// toml++ prints a `double` through `std::ostringstream` with
/// `precision(std::numeric_limits<double>::max_digits10)` — 17 significant
/// digits, which is by definition enough to name any `double` uniquely — and
/// imbues `std::locale::classic()`, so no locale can turn the separator into a
/// comma. (`impl/print_to_stream.inl`, read in toml++ 3.4.0. Its `std::to_chars`
/// path is compiled out: `preprocessor.hpp` sets `TOML_FLOAT_CHARCONV 0` for
/// every GCC and Clang.) `tests/unit/lyrics/test_LyricTiming.cpp` pins the
/// result with `std::bit_cast`.
[[nodiscard]] std::expected<std::string, TimingFault>
encodeDocument(const LyricTimingDocument& document);

struct DocumentDecode {
    enum class Status : u8 {
        Ok,
        /// Not a timing document, or a key required by schema v1 is missing.
        BadBody,
        /// A schema major this build does not implement. Refused in **both**
        /// directions — v1 is the first version, so there is nothing to migrate
        /// from, and "0" is not a legacy value to paper over.
        UnsupportedVersion,
    };

    Status status{Status::BadBody};
    u32 major{0};
    u32 minor{0};
    std::string detail;
    LyricTimingDocument document;

    [[nodiscard]] bool ok() const noexcept { return status == Status::Ok; }
};

/// Parse TOML bytes into a document.
///
/// A pure function of its input, so a test can hand it a truncated body or a
/// bumped version key directly.
///
/// @section Why the version key is *required* rather than defaulted
/// `RenderJob` puts an 8-byte binary header in front of its TOML body because
/// `toml::table` is a sorted map and no key reliably lands on the first byte —
/// so a body whose version key was lost parses as a valid current-version file
/// with every field defaulted. A timing document cannot pay for a binary header,
/// and does not need one, because **every key of schema v1 is written in every
/// file and required on read.** The `[[words]]` array and both version keys are
/// refused when absent rather than defaulted, which closes the same hole
/// structurally. Consequence, and it is the right one: a minor bump may only add
/// a key that carries a documented default.
[[nodiscard]] DocumentDecode decodeDocument(std::string_view bytes);

/// Write `document` to `dest` atomically.
///
/// Write temp, flush, check the stream, close, fsync, then rename over the
/// destination — the sequence `ConfigLoader::save` established and `writeReceipt`
/// mirrors. A half-written timing document is worse than none: the user would
/// come back to a file that parses as a real document with half the words gone.
[[nodiscard]] std::expected<void, TimingFault> saveDocument(const LyricTimingDocument& document,
                                                            const fs::path& dest);

/// Read a document back, refusing rather than defaulting anything.
[[nodiscard]] std::expected<LyricTimingDocument, TimingFault> loadDocument(const fs::path& src);

} // namespace vc::lyricstiming
