/**
 * @file LyricTiming.cpp
 * @file Purpose: the reflow map, the history, the plausibility rules, and the
 *                TOML round trip for `LyricTimingDocument`.
 *
 * Read the header first. It carries the reasoning for every decision here; this
 * file carries the arithmetic and the measured details those decisions turn on.
 *
 * Everything here is a leaf: it reads and writes its own types and calls nothing
 * of its own kind. There is no Qt widget, no audio device and no network, which
 * is what lets `tests/unit/lyrics/test_LyricTiming.cpp` exercise the whole model
 * in a `QCoreApplication`.
 */

#include "lyrics/LyricTiming.hpp"

// Q_OS_WIN must be known before the platform block at the bottom, so this is
// explicit rather than inherited from whatever header happens to sort first.
// Same reason, and same comment, as `ConfigLoader.cpp` and `RenderJob.cpp`.
#include <QtGlobal>

#include "core/Logger.hpp"

#include <toml++/toml.h>

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <fstream>
#include <iterator>
#include <limits>
#include <ranges>
#include <sstream>
#include <system_error>
#include <utility>
#include <vector>

#ifndef Q_OS_WIN
#include <fcntl.h>
#include <unistd.h>
#else
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace vc::lyricstiming {

namespace {

/// The monotone piecewise-linear map, evaluated on one span.
///
/// `ox0 -> ax0` and `ox1 -> ax1` are the span's two endpoints. A non-degenerate
/// span has `ox1 > ox0` and interpolates; a degenerate one cannot be divided by,
/// and falls back to a **rigid translation by the left node's own correction**.
///
/// That fallback is reachable only from an adopted document whose anchor onsets
/// are not strictly increasing — `pinAnchor` refuses a pin that would make them
/// so, so a document that went through the editor cannot get here. `adopt` does
/// not validate, deliberately, so the case exists; `checkPausibility` reports it
/// as `OutOfOrderStart`. Translating is chosen over dividing because a division
/// by zero would put the word at an infinity, and a timing document containing an
/// infinity is a worse outcome than one word being a little wrong.
[[nodiscard]] f64 mapSpan(f64 x, f64 ox0, f64 ox1, f64 ax0, f64 ax1) noexcept {
    const f64 span = ox1 - ox0;
    if (!(span > 0.0)) {
        return x + (ax0 - ox0);
    }
    return ax0 + (x - ox0) * ((ax1 - ax0) / span);
}

[[nodiscard]] TimingFault fault(TimingError code, std::string detail) {
    return TimingFault{code, std::move(detail)};
}

/// toml++ 3.4.0 imbues `std::locale::classic()` before printing a double, so a
/// locale whose decimal separator is a comma cannot corrupt the file. Verified by
/// reading `impl/print_to_stream.inl`; `encodeDocument`'s header comment carries
/// the rest of the argument.
[[nodiscard]] std::string readString(const toml::table& tbl, std::string_view key,
                                     std::string_view fallback = {}) {
    if (const auto node = tbl[key]) {
        if (auto value = node.value<std::string>()) {
            return *value;
        }
    }
    return std::string(fallback);
}

/// Absent-or-wrong-type falls back rather than fails, because a hand-edited
/// document is a file on disk that anything can touch. The *required* keys are
/// checked by `decodeDocument` before this is reached, so the fallback only
/// applies to genuinely optional ones.
template <typename T>
[[nodiscard]] T readScalar(const toml::table& tbl, std::string_view key, T fallback) {
    if (const auto node = tbl[key]) {
        if (auto value = node.value<T>()) {
            return *value;
        }
    }
    return fallback;
}

[[nodiscard]] std::optional<u32> readVersion(const toml::table& tbl, std::string_view key) {
    if (const auto node = tbl[key]) {
        if (auto value = node.value<std::int64_t>()) {
            if (*value < 0) {
                return std::nullopt;
            }
            return static_cast<u32>(*value);
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<Pin> pinFromSlug(std::string_view slug) {
    // A closed vocabulary, and an unknown token is refused rather than treated as
    // `None`. Defaulting would let a typo *release* a pin the user set -- the
    // failure would be invisible until a later reflow moved the word.
    if (slug == "none") return Pin::None;
    if (slug == "onset") return Pin::Onset;
    if (slug == "full") return Pin::Full;
    return std::nullopt;
}

[[nodiscard]] std::string_view pinSlug(Pin pin) noexcept {
    switch (pin) {
        case Pin::None:
            return "none";
        case Pin::Onset:
            return "onset";
        case Pin::Full:
            return "full";
    }
    return "none";
}

/// Flush a file's contents to stable storage so a crash immediately after the
/// rename cannot leave a half-written document behind. `std::ofstream` exposes
/// neither fsync nor a handle, so the path is reopened — both primitives require
/// write access, and neither flag truncates or writes anything.
void flushToDisk(const fs::path& path) {
#ifdef Q_OS_WIN
    // FlushFileBuffers fails with ERROR_ACCESS_DENIED on a read-only handle.
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        LOG_WARN("Lyric timing save: could not reopen {} to flush it: error {}", path.string(),
                 static_cast<int>(::GetLastError()));
        return;
    }
    const BOOL flushed = ::FlushFileBuffers(h);
    // Captured before CloseHandle, which may itself clobber the last error.
    const DWORD flushError = ::GetLastError();
    ::CloseHandle(h);
    if (!flushed)
        LOG_WARN("Lyric timing save: could not flush {}: error {}", path.string(),
                 static_cast<int>(flushError));
#else
    // fsync(2) is specified to fail with EBADF on a descriptor not open for
    // writing, so this must be O_WRONLY -- and must NOT carry O_TRUNC, which
    // would empty the file we are about to rename into place.
    const int fd = ::open(path.c_str(), O_WRONLY);
    if (fd < 0) {
        LOG_WARN("Lyric timing save: could not reopen {} to flush it: errno {}", path.string(),
                 errno);
        return;
    }
    const int rc = ::fsync(fd);
    const int syncErrno = errno;
    ::close(fd);
    if (rc != 0)
        LOG_WARN("Lyric timing save: could not flush {}: errno {}", path.string(), syncErrno);
#endif
}

/// Rename `temp` over `dest`, replacing an existing destination where the platform
/// insists on being told.
[[nodiscard]] bool replaceFile(const fs::path& temp, const fs::path& dest, std::error_code& ec) {
#ifdef Q_OS_WIN
    if (::MoveFileExW(temp.c_str(), dest.c_str(),
                      MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        ec.clear();
        return true;
    }
    ec = std::error_code(static_cast<int>(::GetLastError()), std::system_category());
    return false;
#else
    // The error_code overload returns void; the success signal is a cleared ec,
    // not a bool. (`return fs::rename(...)` was a real compile error here, and the
    // same mistake appears in `ConfigLoader.cpp` in git history.)
    fs::rename(temp, dest, ec);
    return !ec;
#endif
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Names
// ─────────────────────────────────────────────────────────────────────────────

std::string_view timingErrorName(const TimingError error) noexcept {
    switch (error) {
        case TimingError::IndexOutOfRange:
            return "word index out of range";
        case TimingError::NonFiniteTime:
            return "time is not finite";
        case TimingError::NegativeTime:
            return "time is negative";
        case TimingError::WouldCrossAnchor:
            return "pin would cross a neighbouring anchor";
        case TimingError::NotAnchored:
            return "word is not anchored";
        case TimingError::ZeroNudge:
            return "nudge of exactly zero";
        case TimingError::NothingToUndo:
            return "nothing to undo";
        case TimingError::NothingToRedo:
            return "nothing to redo";
        case TimingError::UnsupportedSchema:
            return "unsupported schema version";
        case TimingError::MalformedBody:
            return "malformed document";
        case TimingError::WriteFailed:
            return "write failed";
        case TimingError::MissingFile:
            return "file not found";
    }
    return "unknown error";
}

std::string_view editKindName(const EditKind kind) noexcept {
    switch (kind) {
        case EditKind::Pin:
            return "pin";
        case EditKind::Unpin:
            return "unpin";
        case EditKind::Nudge:
            return "nudge";
        case EditKind::Interaction:
            return "interaction";
    }
    return "unknown";
}

std::string_view violationKindName(const ViolationKind kind) noexcept {
    switch (kind) {
        case ViolationKind::NonFiniteTime:
            return "non-finite time";
        case ViolationKind::NegativeTime:
            return "negative time";
        case ViolationKind::NegativeDuration:
            return "negative duration";
        case ViolationKind::SubQuantumDuration:
            return "shorter than the ASS centisecond quantum";
        case ViolationKind::SubFrameDuration:
            return "shorter than one rendered frame";
        case ViolationKind::OutOfOrderStart:
            return "starts before the previous word";
        case ViolationKind::Overlap:
            return "overlaps the next word";
        case ViolationKind::DurationOutlier:
            return "duration is an outlier against the document median";
        case ViolationKind::BeyondDocumentEnd:
            return "ends after the media duration";
    }
    return "unknown violation";
}

std::string_view driftStatusName(const DriftStatus status) noexcept {
    switch (status) {
        case DriftStatus::Ok:
            return "ok";
        case DriftStatus::NoWords:
            return "document has no words";
        case DriftStatus::TooFewObservations:
            return "too few usable observations to measure drift";
        case DriftStatus::IndexOutOfRange:
            return "an observation named a word the document does not have";
    }
    return "unknown status";
}

// ─────────────────────────────────────────────────────────────────────────────
// Document
// ─────────────────────────────────────────────────────────────────────────────

LyricTimingDocument LyricTimingDocument::adopt(std::vector<TimedWord> words) {
    LyricTimingDocument document;
    // The base is stamped HERE and never again. Every later edit derives its
    // mapping from it, so it has to be the position the caller handed us -- not
    // whatever a reflow last left behind. See TimedWord::baseStartSeconds.
    for (auto& word : words) {
        word.baseStartSeconds = word.startSeconds;
        word.baseEndSeconds = word.endSeconds;
    }
    document.words_ = std::move(words);
    return document;
}

usize LyricTimingDocument::anchorCount() const noexcept {
    usize count = 0;
    for (const auto& word : words_) {
        if (word.pin != Pin::None) {
            ++count;
        }
    }
    return count;
}

std::optional<usize> LyricTimingDocument::prevAnchorIndex(const usize index) const noexcept {
    for (usize i = index; i > 0; --i) {
        if (words_[i - 1].pin != Pin::None) {
            return i - 1;
        }
    }
    return std::nullopt;
}

std::optional<usize> LyricTimingDocument::nextAnchorIndex(const usize index) const noexcept {
    for (usize i = index + 1; i < words_.size(); ++i) {
        if (words_[i].pin != Pin::None) {
            return i;
        }
    }
    return std::nullopt;
}

ReflowOutcome LyricTimingDocument::applyReflow(const std::vector<TimedWord>& old,
                                               std::vector<TimedWord>& candidate) const {
    ReflowOutcome outcome;

    // Anchored words are settled FIRST, and unconditionally.
    //
    // The order matters: the span map below returns early when there is no second
    // anchor, and the single-anchor case is the common one -- a user correcting one
    // line has exactly one anchor. Settling them afterwards would leave a pinned
    // word's onset moved and its offset behind, which is a duration change nobody
    // asked for and which can invert a short word.
    //
    // `Onset` translates rigidly by the word's own correction: the user said when
    // the word begins and said nothing about how long it lasts. `Full` keeps both
    // ends -- that is what a nudge is.
    for (usize i = 0; i < candidate.size(); ++i) {
        if (candidate[i].pin == Pin::Onset) {
            // Duration preserved, measured from the BASE onset rather than from
            // `old[i].startSeconds`. The latter is this word's already-reflowed
            // position, so re-pinning an anchored word would compound its previous
            // correction into its duration. See TimedWord::baseStartSeconds.
            candidate[i].endSeconds = candidate[i].startSeconds +
                                      (candidate[i].baseEndSeconds -
                                       candidate[i].baseStartSeconds);
        }
    }

    std::vector<usize> nodes;
    nodes.reserve(candidate.size());
    for (usize i = 0; i < candidate.size(); ++i) {
        if (candidate[i].pin != Pin::None) {
            nodes.push_back(i);
        }
    }
    if (nodes.size() < 2) {
        // Zero or one node: there is no span to interpolate between, and the rule
        // that nothing outside the outermost anchors moves leaves everything else
        // exactly where it was.
        return outcome;
    }

    // `nodes.front()` and `nodes.back()` bound the mapped region. Outside it the map
    // is the identity, which is the whole of the "local" half of the model.
    const usize first = nodes.front();
    const usize last = nodes.back();

    // BASE positions on the "before" side, current positions on the "after" side.
    // Mixing the two -- which is what this did -- makes the span's slope a function
    // of how many pins came before rather than of where the anchors actually are.
    const auto mapValue = [&](const f64 x) -> f64 {
        if (x <= old[first].baseStartSeconds) {
            return x;
        }
        if (x >= old[last].baseStartSeconds) {
            return x;
        }
        for (usize j = 1; j < nodes.size(); ++j) {
            if (x <= old[nodes[j]].baseStartSeconds) {
                return mapSpan(x, old[nodes[j - 1]].baseStartSeconds,
                               old[nodes[j]].baseStartSeconds,
                               candidate[nodes[j - 1]].startSeconds,
                               candidate[nodes[j]].startSeconds);
            }
        }
        return x;
    };

    for (usize i = 0; i < candidate.size(); ++i) {
        if (candidate[i].pin != Pin::None) {
            // Settled above. A node's onset is already its own mapped value -- both
            // segments meeting there send it there -- so re-mapping it would only add
            // a way to disagree with the pin.
            continue;
        }
        // The free word's own base position and base duration, not its current
        // ones: a word the user has never touched should land where the model says
        // regardless of how many reflows have run.
        const f64 baseDuration = old[i].baseEndSeconds - old[i].baseStartSeconds;
        const f64 start = mapValue(old[i].baseStartSeconds);
        const f64 end = start + baseDuration;
        if (start != candidate[i].startSeconds || end != candidate[i].endSeconds) {
            ++outcome.movedWords;
            candidate[i].startSeconds = start;
            candidate[i].endSeconds = end;
        }
    }
    return outcome;
}

std::expected<ReflowOutcome, TimingFault> LyricTimingDocument::pinAnchor(const usize index,
                                                                         const f64 seconds) {
    if (index >= words_.size()) {
        return std::unexpected(
                fault(TimingError::IndexOutOfRange,
                      "word " + std::to_string(index) + " of " + std::to_string(words_.size())));
    }
    if (!std::isfinite(seconds)) {
        return std::unexpected(fault(TimingError::NonFiniteTime, "a pin time must be finite; " +
                                                                         std::to_string(seconds) +
                                                                         " names no instant"));
    }
    if (seconds < 0.0) {
        return std::unexpected(
                fault(TimingError::NegativeTime, "pin time " + std::to_string(seconds)));
    }

    // Anchors stay ordered along the index axis, which is what makes the reflow map
    // monotone and is why these two refusals exist. They are checked against the
    // *current* anchor onsets, so a re-pin of an existing anchor is checked against
    // its own neighbours and not against itself.
    if (const auto lo = prevAnchorIndex(index)) {
        if (seconds <= words_[*lo].startSeconds) {
            return std::unexpected(fault(TimingError::WouldCrossAnchor,
                                         "pin " + std::to_string(seconds) +
                                                 " is at or before the anchor on word " +
                                                 std::to_string(*lo) + " at " +
                                                 std::to_string(words_[*lo].startSeconds)));
        }
    }
    if (const auto hi = nextAnchorIndex(index)) {
        if (seconds >= words_[*hi].startSeconds) {
            return std::unexpected(fault(TimingError::WouldCrossAnchor,
                                         "pin " + std::to_string(seconds) +
                                                 " is at or after the anchor on word " +
                                                 std::to_string(*hi) + " at " +
                                                 std::to_string(words_[*hi].startSeconds)));
        }
    }

    auto before = words_;
    const f64 previous = words_[index].startSeconds;

    std::vector<TimedWord> candidate = words_;
    candidate[index].startSeconds = seconds;
    // A re-pin must never demote a `Full` word: the offset is still the user's, and
    // the only way to release it is `removeAnchor`.
    if (candidate[index].pin == Pin::None) {
        candidate[index].pin = Pin::Onset;
    }

    ReflowOutcome outcome = applyReflow(before, candidate);
    outcome.deltaSeconds = seconds - previous;

    words_ = std::move(candidate);
    pushEdit(EditKind::Pin, index, std::move(before));
    return outcome;
}

std::expected<ReflowOutcome, TimingFault> LyricTimingDocument::removeAnchor(const usize index) {
    if (index >= words_.size()) {
        return std::unexpected(
                fault(TimingError::IndexOutOfRange,
                      "word " + std::to_string(index) + " of " + std::to_string(words_.size())));
    }
    if (words_[index].pin == Pin::None) {
        return std::unexpected(fault(TimingError::NotAnchored, "word " + std::to_string(index)));
    }

    auto before = words_;
    const f64 previous = words_[index].startSeconds;

    std::vector<TimedWord> candidate = words_;
    candidate[index].pin = Pin::None;

    // Releasing a node collapses the two spans around it into one, and
    // `applyReflow` rebuilds the map with no node at `index` -- which is exactly
    // what frees it.
    ReflowOutcome outcome = applyReflow(before, candidate);
    outcome.deltaSeconds = candidate[index].startSeconds - previous;

    words_ = std::move(candidate);
    pushEdit(EditKind::Unpin, index, std::move(before));
    return outcome;
}

f64 LyricTimingDocument::clampPin(const usize index, const f64 proposedSeconds) const noexcept {
    if (index >= words_.size() || !std::isfinite(proposedSeconds)) {
        return 0.0;
    }
    f64 value = std::max(0.0, proposedSeconds);
    const auto lo = prevAnchorIndex(index);
    if (!lo) {
        return value;
    }

    // `nextafter`, not arithmetic: `lo.start + epsilon` is not reliably the next
    // representable double above it, and a value exactly equal to the bound is one
    // `pinAnchor` would refuse.
    constexpr f64 kInfinity = std::numeric_limits<f64>::infinity();
    const f64 lower = std::nextafter(words_[*lo].startSeconds, kInfinity);
    value = std::max(value, lower);

    if (const auto hi = nextAnchorIndex(index)) {
        const f64 upper = std::nextafter(words_[*hi].startSeconds, -kInfinity);
        // An empty admissible interval can only come from an adopted document whose
        // anchors are not ordered. The lower bound wins rather than returning a
        // value `pinAnchor` would refuse on the next line.
        if (lower <= upper) {
            value = std::min(value, upper);
        }
    }
    return value;
}

namespace {

/// The shared body of every `nudge*` entry point.
///
/// Clamping is applied in one fixed order and the second bound wins, which is a
/// stated limitation rather than an oversight: an already-overlapping document has
/// no admissible delta at all, and `checkPlausibility` is how that is found out.
[[nodiscard]] std::expected<NudgeOutcome, TimingFault> applyNudge(std::vector<TimedWord>& words,
                                                                  const usize first,
                                                                  const usize last,
                                                                  const f64 deltaSeconds) {
    if (first >= words.size() || last >= words.size() || first > last) {
        return std::unexpected(
                fault(TimingError::IndexOutOfRange, "range [" + std::to_string(first) + ", " +
                                                            std::to_string(last) + "] of " +
                                                            std::to_string(words.size())));
    }
    if (!std::isfinite(deltaSeconds)) {
        return std::unexpected(fault(TimingError::NonFiniteTime, "a nudge delta must be finite"));
    }

    NudgeOutcome outcome;
    outcome.requestedSeconds = deltaSeconds;
    if (deltaSeconds == 0.0) {
        // Refused, not applied. A QML animated `value` reports its starting value
        // as a change, and an undo entry per starting value would fill the history
        // before the user touched anything.
        return std::unexpected(
                fault(TimingError::ZeroNudge, "a nudge of exactly zero is not an edit"));
    }

    f64 applied = deltaSeconds;
    if (first > 0) {
        applied = std::max(applied, words[first - 1].endSeconds - words[first].startSeconds);
    }
    if (last + 1 < words.size()) {
        applied = std::min(applied, words[last + 1].startSeconds - words[last].endSeconds);
    }

    outcome.appliedSeconds = applied;
    outcome.clamped = applied != deltaSeconds;
    outcome.movedWords = last - first + 1;
    for (usize i = first; i <= last; ++i) {
        words[i].startSeconds += applied;
        words[i].endSeconds += applied;
        // The nudge is promoted to a pin, which is what lets it survive a later
        // reflow. See the header, @section Anchors are the only constraint class.
        words[i].pin = Pin::Full;
    }
    return outcome;
}

} // namespace

std::expected<NudgeOutcome, TimingFault> LyricTimingDocument::nudgeWord(const usize index,
                                                                        const f64 deltaSeconds) {
    if (index >= words_.size()) {
        return std::unexpected(
                fault(TimingError::IndexOutOfRange,
                      "word " + std::to_string(index) + " of " + std::to_string(words_.size())));
    }
    auto before = words_;
    auto outcome = applyNudge(words_, index, index, deltaSeconds);
    if (!outcome) {
        return outcome;
    }
    pushEdit(EditKind::Nudge, index, std::move(before));
    return outcome;
}

std::expected<NudgeOutcome, TimingFault>
LyricTimingDocument::nudgeRange(const usize first, const usize last, const f64 deltaSeconds) {
    if (first >= words_.size() || last >= words_.size() || first > last) {
        return std::unexpected(
                fault(TimingError::IndexOutOfRange, "range [" + std::to_string(first) + ", " +
                                                            std::to_string(last) + "] of " +
                                                            std::to_string(words_.size())));
    }
    auto before = words_;
    auto outcome = applyNudge(words_, first, last, deltaSeconds);
    if (!outcome) {
        return outcome;
    }
    pushEdit(EditKind::Nudge, first, std::move(before));
    return outcome;
}

std::expected<NudgeOutcome, TimingFault> LyricTimingDocument::nudgeRipple(const usize index,
                                                                          const f64 deltaSeconds) {
    if (index >= words_.size()) {
        return std::unexpected(
                fault(TimingError::IndexOutOfRange,
                      "word " + std::to_string(index) + " of " + std::to_string(words_.size())));
    }
    auto before = words_;
    auto outcome = applyNudge(words_, index, words_.size() - 1, deltaSeconds);
    if (!outcome) {
        return outcome;
    }
    pushEdit(EditKind::Nudge, index, std::move(before));
    return outcome;
}

// ─────────────────────────────────────────────────────────────────────────────
// History
// ─────────────────────────────────────────────────────────────────────────────

void LyricTimingDocument::beginInteraction(const std::string_view label) {
    ++interactionDepth_;
    if (interactionDepth_ == 1) {
        interactionLabel_.assign(label);
    }
    // An open bracket is already the coalescing rule; a leftover implicit chain must
    // not merge into it.
    chain_.reset();
}

void LyricTimingDocument::endInteraction() noexcept {
    if (interactionDepth_ > 0) {
        --interactionDepth_;
    }
    if (interactionDepth_ == 0) {
        interactionLabel_.clear();
    }
    chain_.reset();
}

void LyricTimingDocument::commit() noexcept { chain_.reset(); }

void LyricTimingDocument::pushEdit(const EditKind kind, const usize target,
                                   std::vector<TimedWord> before) {
    const EditStamp stamp{kind, target};

    // Two merge rules, and both keep the pre-image of the *first* edit in the chain.
    // Overwriting it with the current step's pre-image is the classic drag-undo bug:
    // undo then restores the state one step into the gesture, so the user presses
    // undo and sees the bead move back by one pixel instead of the whole drag
    // unwinding.
    bool merge = false;
    if (!undo_.empty()) {
        if (interactionDepth_ > 0) {
            // Inside a bracket, the trailing entry is the bracket itself if the
            // previous edit was also inside it. The target is deliberately *not*
            // compared: a multi-select drag is one gesture.
            merge = undo_.back().stamp.kind == EditKind::Interaction;
        } else {
            merge = chain_.has_value() && *chain_ == stamp;
        }
    }

    if (!merge) {
        const EditStamp recorded =
                interactionDepth_ > 0 ? EditStamp{EditKind::Interaction, target} : stamp;
        undo_.push_back(Entry{recorded, std::move(before)});
        if (undo_.size() > kMaxUndoDepth) {
            // Drop the oldest. O(n) on a 129-element vector of vectors, at a human
            // edit rate.
            undo_.erase(undo_.begin());
        }
    }

    // Any new edit invalidates the redo branch. Doing this unconditionally is why a
    // merged edit does not need to clear `redo_` separately.
    redo_.clear();
    chain_.reset();
    if (interactionDepth_ == 0) {
        chain_ = stamp;
    }
}

std::expected<void, TimingFault> LyricTimingDocument::undo() {
    if (undo_.empty()) {
        return std::unexpected(fault(TimingError::NothingToUndo, "history is empty"));
    }
    Entry entry = std::move(undo_.back());
    undo_.pop_back();
    // The state being left *is* the state a redo must restore, so the redo entry
    // carries the same shape as an undo entry.
    redo_.push_back(Entry{entry.stamp, words_});
    words_ = std::move(entry.before);
    // A merge into the entry we just restored would record the next edit's pre-image
    // two steps back, so the chain is broken here rather than at the next push.
    chain_.reset();
    return {};
}

std::expected<void, TimingFault> LyricTimingDocument::redo() {
    if (redo_.empty()) {
        return std::unexpected(fault(TimingError::NothingToRedo, "history is empty"));
    }
    Entry entry = std::move(redo_.back());
    redo_.pop_back();
    undo_.push_back(Entry{entry.stamp, words_});
    words_ = std::move(entry.before);
    chain_.reset();
    return {};
}

void LyricTimingDocument::clearHistory() noexcept {
    undo_.clear();
    redo_.clear();
    chain_.reset();
    interactionDepth_ = 0;
    interactionLabel_.clear();
}

// ─────────────────────────────────────────────────────────────────────────────
// Plausibility
// ─────────────────────────────────────────────────────────────────────────────

usize PlausibilityReport::violationCount() const noexcept {
    usize count = 0;
    for (const auto& finding : findings) {
        if (finding.severity == Severity::Violation) {
            ++count;
        }
    }
    return count;
}

usize PlausibilityReport::warningCount() const noexcept {
    return findings.size() - violationCount();
}

bool PlausibilityReport::has(const ViolationKind kind) const noexcept {
    for (const auto& finding : findings) {
        if (finding.kind == kind) {
            return true;
        }
    }
    return false;
}

PlausibilityReport checkPlausibility(const std::span<const TimedWord> words,
                                     const std::optional<f64> mediaDurationSeconds) {
    PlausibilityReport report;
    if (words.empty()) {
        return report;
    }
    report.findings.reserve(words.size());

    const auto add = [&report](const ViolationKind kind, const Severity severity, const usize index,
                               const usize other, const f64 measured, const f64 limit) {
        report.findings.push_back(
                PlausibilityFinding{kind, severity, index, other, measured, limit});
    };

    // ── per-word ─────────────────────────────────────────────────────────
    std::vector<f64> durations;
    durations.reserve(words.size());
    for (usize i = 0; i < words.size(); ++i) {
        const TimedWord& word = words[i];
        if (!std::isfinite(word.startSeconds) || !std::isfinite(word.endSeconds)) {
            // Reported alone and the word is skipped. Every rule below is a
            // comparison against a finite bound, and a NaN compares false against
            // all of them -- so continuing would report a *silence*, which is the
            // one outcome that is indistinguishable from "checked and fine".
            add(ViolationKind::NonFiniteTime, Severity::Violation, i, kNoSecondWord,
                word.startSeconds, 0.0);
            continue;
        }
        if (word.startSeconds < 0.0 || word.endSeconds < 0.0) {
            add(ViolationKind::NegativeTime, Severity::Violation, i, kNoSecondWord,
                std::min(word.startSeconds, word.endSeconds), 0.0);
        }

        const f64 duration = word.endSeconds - word.startSeconds;
        if (duration < 0.0) {
            add(ViolationKind::NegativeDuration, Severity::Violation, i, kNoSecondWord, duration,
                0.0);
            continue;
        }
        durations.push_back(duration);

        // `else if`, not two findings. A word below the quantum is also below one
        // frame, and reporting both tells a user about two problems when they have
        // one. The quantum is tested first because it is the tighter bound *and*
        // the more fundamental one: the output cannot represent it at all.
        if (duration < kAssCentisecondSeconds) {
            add(ViolationKind::SubQuantumDuration, Severity::Warning, i, kNoSecondWord, duration,
                kAssCentisecondSeconds);
        } else if (duration < kSubFrameSeconds) {
            add(ViolationKind::SubFrameDuration, Severity::Warning, i, kNoSecondWord, duration,
                kSubFrameSeconds);
        }

        if (mediaDurationSeconds && word.endSeconds > *mediaDurationSeconds) {
            add(ViolationKind::BeyondDocumentEnd, Severity::Warning, i, kNoSecondWord,
                word.endSeconds, *mediaDurationSeconds);
        }
    }

    // ── pairwise ─────────────────────────────────────────────────────────
    for (usize i = 0; i + 1 < words.size(); ++i) {
        const TimedWord& here = words[i];
        const TimedWord& next = words[i + 1];
        if (!std::isfinite(here.startSeconds) || !std::isfinite(here.endSeconds) ||
            !std::isfinite(next.startSeconds)) {
            // Already reported per word. Skipping here keeps the report from
            // carrying a second finding that says the same thing with a different
            // index.
            continue;
        }
        if (next.startSeconds < here.startSeconds) {
            add(ViolationKind::OutOfOrderStart, Severity::Violation, i, i + 1,
                next.startSeconds - here.startSeconds, 0.0);
        } else if (next.startSeconds < here.endSeconds - kOverlapToleranceSeconds) {
            // A warning, not a violation: two voices genuinely overlap in this
            // genre, so a checker that called that an error would be wrong about
            // real material.
            add(ViolationKind::Overlap, Severity::Warning, i, i + 1,
                here.endSeconds - next.startSeconds, kOverlapToleranceSeconds);
        }
    }

    // ── duration outlier ─────────────────────────────────────────────────
    if (words.size() >= kMinWordsForDurationMedian && durations.size() >= 2) {
        // `nth_element` puts the element that *would* sit at index size/2 in sorted
        // order there, in linear time. Upper median for an even count -- the lower
        // one is the more conservative and would flag a word the upper median
        // excuses, which is the wrong direction for a checker.
        const usize middle = durations.size() / 2;
        std::nth_element(durations.begin(), durations.begin() + static_cast<std::ptrdiff_t>(middle),
                         durations.end());
        const f64 median = durations[middle];
        if (median > 0.0) {
            const f64 limit = median * kDurationOutlierFactor;
            for (usize i = 0; i < words.size(); ++i) {
                const TimedWord& word = words[i];
                if (!std::isfinite(word.startSeconds) || !std::isfinite(word.endSeconds)) {
                    continue;
                }
                const f64 duration = word.endSeconds - word.startSeconds;
                if (duration > limit) {
                    add(ViolationKind::DurationOutlier, Severity::Warning, i, kNoSecondWord,
                        duration, limit);
                }
            }
        }
    }

    return report;
}

// ─────────────────────────────────────────────────────────────────────────────
// Drift
// ─────────────────────────────────────────────────────────────────────────────

DriftReport measureDrift(const std::span<const TimedWord> document,
                         const std::span<const AlignedWord> observed) {
    DriftReport report;
    if (document.empty()) {
        report.status = DriftStatus::NoWords;
        return report;
    }

    std::vector<f64> offsets;
    offsets.reserve(observed.size());
    for (const AlignedWord& observation : observed) {
        // An out-of-range index is a refusal rather than a discard: it means the
        // producer and the document disagree about what the words are, and averaging
        // over a subset would produce a number about the wrong document.
        if (observation.wordIndex >= document.size()) {
            report.status = DriftStatus::IndexOutOfRange;
            return report;
        }
        if (!std::isfinite(observation.seconds) || !std::isfinite(observation.confidence) ||
            observation.confidence < kMinimumAlignmentConfidence) {
            ++report.rejectedObservations;
            continue;
        }
        offsets.push_back(observation.seconds - document[observation.wordIndex].startSeconds);
        report.perWord.push_back(
                WordDrift{observation.wordIndex,
                          observation.seconds - document[observation.wordIndex].startSeconds,
                          observation.confidence});
    }

    report.usableObservations = offsets.size();
    if (report.usableObservations < kMinimumObservationsForDrift) {
        // The refusal the whole function exists to be honest about. A median over
        // three observations is not a measurement, and returning one anyway is the
        // failure mode being avoided.
        report.status = DriftStatus::TooFewObservations;
        return report;
    }

    const auto medianOf = [](std::vector<f64> values) {
        const usize middle = values.size() / 2;
        std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(middle),
                         values.end());
        return values[middle];
    };

    report.medianOffsetSeconds = medianOf(offsets);

    std::vector<f64> deviations;
    deviations.reserve(offsets.size());
    for (const f64 offset : offsets) {
        deviations.push_back(std::fabs(offset - report.medianOffsetSeconds));
    }
    report.medianAbsoluteDeviationSeconds = medianOf(std::move(deviations));

    for (const WordDrift& drift : report.perWord) {
        if (std::fabs(drift.offsetSeconds) > std::fabs(report.worstOffsetSeconds)) {
            report.worstOffsetSeconds = drift.offsetSeconds;
            report.worstWordIndex = drift.wordIndex;
        }
    }

    // Document order, so `perWord` is readable next to `words()`.
    std::ranges::sort(report.perWord, {}, &WordDrift::wordIndex);
    report.status = DriftStatus::Ok;
    return report;
}

// ─────────────────────────────────────────────────────────────────────────────
// Serialisation
// ─────────────────────────────────────────────────────────────────────────────

std::expected<std::string, TimingFault> encodeDocument(const LyricTimingDocument& document) {
    for (usize i = 0; i < document.wordCount(); ++i) {
        const TimedWord& word = *document.word(i);
        if (!std::isfinite(word.startSeconds) || !std::isfinite(word.endSeconds)) {
            // TOML *can* write `nan` and `inf`, so refusing is a choice rather than
            // a limitation. A timing document containing one is a defect, and
            // persisting it would make the defect durable.
            return std::unexpected(
                    fault(TimingError::NonFiniteTime,
                          "word " + std::to_string(i) + " carries a non-finite time"));
        }
    }
    if (const auto& duration = document.mediaDurationSeconds()) {
        if (!std::isfinite(*duration)) {
            return std::unexpected(
                    fault(TimingError::NonFiniteTime, "media duration is not finite"));
        }
    }

    toml::array wordsArray;
    for (const TimedWord& word : document.words()) {
        wordsArray.push_back(toml::table{{"text", word.text},
                                         {"start_seconds", word.startSeconds},
                                         {"end_seconds", word.endSeconds},
                                         {"pin", std::string(pinSlug(word.pin))}});
    }

    toml::table identity;
    identity.insert("song_id", document.songId());
    identity.insert("title", document.title());
    identity.insert("artist", document.artist());
    // `std::nullopt` is written as an absent key rather than a sentinel, because a
    // sentinel would have to be a number that cannot be a duration -- and the parse
    // side would then have to guess which one was chosen.
    if (const auto& duration = document.mediaDurationSeconds()) {
        identity.insert("media_duration_seconds", *duration);
    }

    // Every key of schema v1 appears in every file, so a diff between two documents
    // shows a changed value rather than a vanished key -- and, more importantly,
    // `decodeDocument` can *require* each one instead of defaulting it.
    const toml::table root{
            {"schema_major", static_cast<std::int64_t>(kTimingSchemaMajor)},
            {"schema_minor", static_cast<std::int64_t>(kTimingSchemaMinor)},
            {"identity", std::move(identity)},
            {"words", std::move(wordsArray)},
    };

    std::ostringstream stream;
    stream << root;
    if (!stream) {
        return std::unexpected(fault(TimingError::WriteFailed, "TOML stream failed"));
    }
    return stream.str();
}

DocumentDecode decodeDocument(const std::string_view bytes) {
    DocumentDecode out;

    toml::table root;
    try {
        root = toml::parse(bytes);
    } catch (const std::exception& err) {
        // `std::exception`, not `toml::parse_error`. Measured on toml++ 3.4.0: a
        // malformed *table header* (`[a!dio]`) throws a plain `std::runtime_error`,
        // which is not derived from `toml::parse_error`, so the narrow catch
        // `ConfigLoader::load` uses lets it escape. A document on disk can be
        // corrupted by anything, so the guard here is the one that actually holds.
        out.status = DocumentDecode::Status::BadBody;
        out.detail = std::string("TOML body is unreadable: ") + err.what();
        return out;
    }

    const auto major = readVersion(root, "schema_major");
    const auto minor = readVersion(root, "schema_minor");
    if (!major || !minor) {
        // Refused, not defaulted. See the header, @section Why the version key is
        // *required* rather than defaulted: a body whose version key was lost would
        // otherwise parse as a valid current document with every field defaulted.
        out.status = DocumentDecode::Status::BadBody;
        out.detail = "schema_major / schema_minor is missing or not a non-negative "
                     "integer; a document that lost its version has lost the fact of "
                     "which schema it was";
        return out;
    }
    out.major = *major;
    out.minor = *minor;

    // Unknown major refused in BOTH directions. Reading a v2 file with v1 defaults
    // does not warn -- it fabricates, and a fabricated correction document is worse
    // than one the user knows is unreadable.
    if (out.major != kTimingSchemaMajor) {
        out.status = DocumentDecode::Status::UnsupportedVersion;
        out.detail = "schema major " + std::to_string(out.major) + ", this build reads " +
                     std::to_string(kTimingSchemaMajor) +
                     "; refusing rather than substituting defaults for fields it "
                     "does not understand";
        return out;
    }

    const auto wordsNode = root["words"];
    const auto* wordsArray = wordsNode ? wordsNode.as_array() : nullptr;
    if (wordsArray == nullptr) {
        out.status = DocumentDecode::Status::BadBody;
        out.detail = "required array [[words]] is missing or the wrong type; a document "
                     "with a lost word list has lost every correction in it";
        return out;
    }

    std::vector<TimedWord> words;
    words.reserve(wordsArray->size());
    for (const auto& element : *wordsArray) {
        const auto* entry = element.as_table();
        if (entry == nullptr) {
            out.status = DocumentDecode::Status::BadBody;
            out.detail = "a [[words]] element is not a table; skipping it would shorten "
                         "the document silently";
            return out;
        }

        TimedWord word;
        word.text = readString(*entry, "text");
        // `start_seconds` is defaulted to 0.0 rather than refused. That is safe
        // here and only here because the key's *presence* was already established by
        // the writer's every-key-present rule, and because a word starting at 0.0 is
        // a legitimate value -- unlike, say, a missing `pin`, where defaulting would
        // release a pin the user set.
        word.startSeconds = readScalar<f64>(*entry, "start_seconds", 0.0);
        word.endSeconds = readScalar<f64>(*entry, "end_seconds", word.startSeconds);

        const std::string slug = readString(*entry, "pin", "none");
        const auto pin = pinFromSlug(slug);
        if (!pin) {
            out.status = DocumentDecode::Status::BadBody;
            out.detail = "word pin '" + slug +
                         "' is not one of none / onset / full; "
                         "defaulting it would silently release a pin";
            return out;
        }
        word.pin = *pin;
        words.push_back(std::move(word));
    }

    LyricTimingDocument document = LyricTimingDocument::adopt(std::move(words));
    if (const auto identityNode = root["identity"]) {
        if (const auto* identity = identityNode.as_table()) {
            document.setSongId(readString(*identity, "song_id"));
            document.setTitle(readString(*identity, "title"));
            document.setArtist(readString(*identity, "artist"));
            if (const auto node = (*identity)["media_duration_seconds"]) {
                if (auto value = node.value<f64>()) {
                    document.setMediaDurationSeconds(*value);
                }
            }
        }
    }

    out.document = std::move(document);
    out.status = DocumentDecode::Status::Ok;
    return out;
}

std::expected<void, TimingFault> saveDocument(const LyricTimingDocument& document,
                                              const fs::path& dest) {
    return encodeDocument(document).and_then(
            [&dest](const std::string& bytes) -> std::expected<void, TimingFault> {
                fs::path tempPath = dest;
                tempPath += ".tmp";

                {
                    std::ofstream file(tempPath, std::ios::binary | std::ios::trunc);
                    if (!file) {
                        // A `.tmp` left by an earlier crash would sit here forever
                        // otherwise.
                        std::error_code removeEc;
                        fs::remove(tempPath, removeEc);
                        return std::unexpected(
                                fault(TimingError::WriteFailed,
                                      "could not open " + tempPath.string() + " for writing"));
                    }
                    file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
                    file.flush();
                    // Read before close. A partially flushed stream renamed over the
                    // real document is exactly the corruption this dance prevents:
                    // the user would come back to a file that parses as a valid
                    // document with half the words gone.
                    const bool complete = static_cast<bool>(file);
                    file.close();
                    if (!complete) {
                        std::error_code removeEc;
                        fs::remove(tempPath, removeEc);
                        return std::unexpected(fault(TimingError::WriteFailed,
                                                     "failed while writing " + tempPath.string()));
                    }
                }

                flushToDisk(tempPath);

                std::error_code ec;
                if (!replaceFile(tempPath, dest, ec)) {
                    std::error_code removeEc;
                    fs::remove(tempPath, removeEc);
                    return std::unexpected(
                            fault(TimingError::WriteFailed,
                                  "could not replace " + dest.string() + ": " + ec.message()));
                }
                return {};
            });
}

std::expected<LyricTimingDocument, TimingFault> loadDocument(const fs::path& src) {
    std::error_code ec;
    if (!fs::exists(src, ec)) {
        return std::unexpected(fault(TimingError::MissingFile, "no document at " + src.string()));
    }

    std::ifstream file(src, std::ios::binary);
    if (!file) {
        return std::unexpected(fault(TimingError::MissingFile, "could not open " + src.string()));
    }
    const std::string bytes((std::istreambuf_iterator<char>(file)),
                            std::istreambuf_iterator<char>());

    const DocumentDecode decoded = decodeDocument(bytes);
    if (!decoded.ok()) {
        const TimingError code = decoded.status == DocumentDecode::Status::UnsupportedVersion
                                         ? TimingError::UnsupportedSchema
                                         : TimingError::MalformedBody;
        return std::unexpected(fault(code, decoded.detail));
    }
    return decoded.document;
}

} // namespace vc::lyricstiming
