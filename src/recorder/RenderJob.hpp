#pragma once
/**
 * @file RenderJob.hpp
 * @file Purpose: the durable, versioned record of one render.
 *
 * @section Why a schema and not a serialization of the encoder structs
 * The reason this file exists is one sentence long: nothing in the tree records
 * that a finished output was 1800 frames at 60 fps from *these* presets, so
 * "render it at 4K instead" means re-rendering three minutes in real time. A
 * receipt only pays for that if it holds every input that determines the
 * output -- and a receipt built by dumping `EncoderSettings` field by field
 * would rot the moment that struct is reordered, renamed, or gains a knob
 * nobody wants in a sidecar.
 *
 * So this is written as a **schema**: a closed vocabulary of slugs, explicit
 * required fields, and a version byte in the file header. The project's own
 * rule is the one that matters -- *the first schema wins forever* -- so the two
 * rules that make a first schema survivable are stated here and enforced by
 * `validate()` rather than left to a future reader's memory:
 *
 *  1. **The vocabulary is frozen at schema v1.** `kVocabularies` below is the
 *     complete, closed set of legal tokens. An unknown token is a validation
 *     failure, refused at `enqueue()` rather than discovered at frame 10800.
 *     This is a deliberate duplication of knowledge that also lives in
 *     `EncoderSettings`: the enum may move, the schema may not, and the
 *     slug -> enum mapping is the render worker's one-time adapter job.
 *  2. **Minor bumps are additive only.** A minor bump may add a key or a
 *     vocabulary entry. It may never remove one, rename one, or change one's
 *     meaning. That is what makes `kSchemaMinor` tolerable to ignore.
 *
 * @section The version rule, and why an unknown *newer* major is refused
 * The header's byte 1 is the schema major. A reader **refuses any major it does
 * not implement**, in both directions:
 *
 *  - *Newer major* -> `ReceiptLoadStatus::UnsupportedVersion`. Reading a v2 file
 *    with v1 defaults does not produce a warning, it produces a **fabricated
 *    render**: a v2 file that added a field to mean "burn the subtitles in" would
 *    silently render without them, and the receipt would then certify an output
 *    the user never asked for. Refusing is the only honest answer.
 *  - *Older major* -> also refused. v1 is the first version, so there is nothing
 *    to migrate from, and "0" is not a legacy value to be papered over.
 *  - *Newer minor* -> **accepted**, and the excess is reported in
 *    `ReceiptDecode::minor` so the caller can log it. This is the forward-
 *    compatible half, and it is only sound because of rule 2 above.
 *
 * Silently reading an unknown version as v1 is the specific corruption this
 * design exists to make impossible, so it is worth naming the two shapes it
 * takes in practice: a `value_or(kSchemaMajor)` on the version (every unknown
 * becomes "current"), and a body-only format with no version at all (a file
 * whose header was truncated parses as valid TOML with every field defaulted).
 * The 8-byte header defeats both, and `decodeReceipt()` is a pure function so a
 * test can hand it a truncated header and a bumped version byte directly.
 *
 * @section Why an 8-byte binary header in front of a TOML body
 * Measured against the alternative. A pure-TOML file cannot put a version
 * "on the first byte": `toml::table` is a sorted map, so key order is
 * alphabetical and no key reliably lands first; and putting `schema_version` in
 * the body gives two failure modes the header removes outright -- a body whose
 * version key was lost parses as a valid v1 job with every field defaulted, and
 * a v1 reader that used `value_or(1)` swallows a v2 file. An 8-byte header whose
 * byte 0 is a format tag makes both *structurally* impossible: a TOML file
 * starts with `[` or whitespace and can never be mistaken for a valid header,
 * so a header-stripped or TOML-only file is rejected as `BadMagic` rather than
 * half-believed.
 *
 * The body stays TOML on purpose. A receipt is small, is read by humans when a
 * render looks wrong, and must not need a second parser in the tree -- and
 * `ConfigLoader.cpp` already established write-temp / flush / fsync / atomic-
 * replace for exactly this kind of file, which `writeReceipt` mirrors.
 *
 * Header layout, little-endian, exactly `kReceiptHeaderBytes` long:
 *
 *   off 0 : u8  format tag        (kFormatTag)  -- the framing, not the schema
 *   off 1 : u8  schema major      (kSchemaMajor)
 *   off 2 : u8  schema minor      (kSchemaMinor)
 *   off 3 : u8  reserved, must be 0
 *   off 4 : u16 header bytes      (kReceiptHeaderBytes) -- a growth seam
 *   off 6 : u16 reserved, must be 0
 *   off 8 : TOML body, UTF-8
 *
 * @section Why the content hash exists but is not filled in automatically
 * `audioSha256` is what makes a receipt falsifiable: without it, a receipt read
 * back next to a *different* `audioPath` claims to describe a render it never
 * performed. It is empty-able, and empty means "not hashed", because hashing is
 * O(file bytes) -- a ten-job batch over ten 100 MB FLACs is a gigabyte of reads
 * before frame 0 -- so **the queue never hashes**. `sha256Hex()` is offered for a
 * caller who wants the guarantee and can afford it, and an empty field is an
 * honest absence rather than a fabricated value.
 *
 * @section Why `CompletedPartial` is its own state
 * A render that ends early can still leave a playable file, and that file must
 * never read as a success. `RenderState::CompletedPartial` is the whole reason
 * the batch summary can be honest: `VideoRecorderCore` distinguishes a stopped
 * recorder from a failed one, and a 40-second output from a 3-minute one is
 * exactly the "plausible-looking short file" failure this repo has already been
 * bitten by in the recorder's byte accounting. The receipt keeps both
 * `framesExpected` and `framesWritten` so the verdict is auditable after the
 * fact rather than trusted.
 */

#include "util/Types.hpp"

#include <array>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace vc {

// ─────────────────────────────────────────────────────────────────────────────
// Schema identity
// ─────────────────────────────────────────────────────────────────────────────

/// Bumped only for a framing change to the header itself.
inline constexpr u8 kFormatTag = 0x01;
/// Bumped for a breaking change to the field set or its meaning.
inline constexpr u32 kSchemaMajor = 1;
/// Bumped for an additive change only. See rule 2 in the file header.
inline constexpr u32 kSchemaMinor = 0;
/// Fixed by `kFormatTag`; `header bytes` in the header exists so a future
/// framing can extend the header and still be self-describing.
inline constexpr usize kReceiptHeaderBytes = 8;
/// Sidecar extension. Distinct from DownloadQueue's `.part`, because the two
/// mean opposite things: `.part` is being written now, `.chadrjob` is what is
/// left behind afterwards.
inline constexpr const char* kReceiptSuffix = ".chadrjob";

// ─────────────────────────────────────────────────────────────────────────────
// State machine
// ─────────────────────────────────────────────────────────────────────────────

/// Lifecycle of one render job.
///
/// Carried through signals as a plain `int`, exactly as `DownloadState` is, so
/// a QML consumer needs no metatype registration.
enum class RenderState : u8 {
    /// Accepted, waiting for a slot.
    Queued = 0,
    /// Handed to a runner and not yet reported terminal.
    Rendering = 1,
    /// Every expected frame reached the file.
    Completed = 2,
    /// A real, playable file, but fewer frames than the job asked for. Distinct
    /// from `Completed` on purpose; see the file header.
    CompletedPartial = 3,
    /// Transient failure with attempts left. The only retryable state.
    FailedRetryable = 4,
    /// Retrying cannot help: bad settings, a full disk, a refused path.
    FailedPermanent = 5,
    /// Stopped at the user's request. **Not** retryable -- see `isRetryable`.
    Cancelled = 6,
};

[[nodiscard]] std::string_view renderStateName(RenderState state) noexcept;

/// No worker is running on this job any more. The queue's slot is free and the
/// job can never change state on its own again.
[[nodiscard]] bool isTerminal(RenderState state) noexcept;

/// Whether a re-attempt of the *same* job could plausibly succeed.
///
/// `Cancelled` is deliberately excluded: cancelling is a decision, not a fault,
/// and resuming it would have to be a fresh enqueue carrying the user's
/// unchanged intent. `FailedPermanent` is excluded because "the disk is full"
/// does not become true again by itself.
[[nodiscard]] bool isRetryable(RenderState state) noexcept;

/// Whether a job in this state can leave bytes on disk that a user could
/// mistake for the whole render.
///
/// True for every terminal state except `Completed` -- including `Cancelled`,
/// because a cancelled render had already been muxing frames when it stopped.
/// The honesty predicate behind `CompletedPartial`.
[[nodiscard]] bool leavesPartialOutput(RenderState state) noexcept;

// ─────────────────────────────────────────────────────────────────────────────
// Closed vocabularies
// ─────────────────────────────────────────────────────────────────────────────

/// Which frozen token list a slug belongs to. An enum rather than a bare list so
/// `validate()` can say *which* field was wrong and `isKnownToken` stays one
/// lookup over one table.
enum class Vocabulary : u8 {
    VideoCodec = 0,
    PixelFormat = 1,
    Container = 2,
    EncoderPreset = 3,
    AudioCodec = 4,
    KaraokeMode = 5,
    HardwareAccel = 6,
};

/// The complete legal token set per vocabulary, frozen at schema v1. Every inner
/// array is padded to `kVocabWidth` with `""`, and `isKnownToken` never accepts
/// an empty token, so a padding slot cannot become a legal value by accident.
inline constexpr usize kVocabWidth = 10;
inline constexpr std::size_t kVocabularyCount = 7;
inline constexpr std::array<std::array<std::string_view, kVocabWidth>,
                            kVocabularyCount>
    kVocabularies{{
        {"h264", "h265", "vp9", "av1", "prores", "ffv1", "h264_nvenc",
         "h265_nvenc", "h264_vaapi", "h265_vaapi"},
        {"yuv420p", "yuv422p", "yuv444p", "rgb24", "nv12", "p010le", "", "", "", ""},
        {"mp4", "mkv", "webm", "mov", "avi", "", "", "", "", ""},
        {"ultrafast", "superfast", "veryfast", "faster", "fast", "medium", "slow",
         "slower", "veryslow", "placebo"},
        {"aac", "opus", "flac", "mp3", "pcm", "", "", "", "", ""},
        {"none", "muxed", "burned-in", "", "", "", "", "", "", ""},
        {"none", "nvenc", "vaapi", "amf", "quicksync", "", "", "", "", ""},
    }};

/// Exact membership in the named vocabulary. Case-sensitive: a slug is a wire
/// value, and `H264` vs `h264` is exactly the ambiguity a frozen vocabulary
/// exists to remove.
[[nodiscard]] bool isKnownToken(Vocabulary vocab, std::string_view token) noexcept;

/// Schema slug for a state. Receipts store the *name*, never an ordinal, for the
/// reason `renderStateName` exists: an enum ordinal is not a wire value.
[[nodiscard]] std::string_view renderStateSlug(RenderState state) noexcept;

/// Inverse of `renderStateSlug`. Returns nullopt for an unknown slug rather than
/// defaulting -- a receipt whose outcome we cannot read is not a receipt we may
/// reinterpret.
[[nodiscard]] std::optional<RenderState> renderStateFromSlug(std::string_view slug) noexcept;

// ─────────────────────────────────────────────────────────────────────────────
// Job fields
// ─────────────────────────────────────────────────────────────────────────────

/// One segment of the scene list: a preset, how long it holds, and how long it
/// crossfades into the next.
///
/// "Presets as scenes, shipped before real keyframes" is the stated plan, and
/// this is its data shape. `crossfadeSeconds` rides on the outgoing segment; a
/// projectM soft-cut is already a working crossfade primitive, so this field
/// records intent rather than describing an implementation that does not exist.
struct RenderScene {
    std::string presetName;
    f64 durationSeconds{0.0};
    f64 crossfadeSeconds{0.0};
};

/// What a finished job produced. Written only for a terminal job, so `outcome`
/// has exactly one meaning and there is no second axis to keep in step with
/// `RenderState`.
struct RenderReceipt {
    RenderState outcome{RenderState::Completed};
    /// The job's `expectedFrames`. Repeated here on purpose: the receipt is read
    /// without the job, so the denominator has to travel with the numerator or
    /// "wrote 400 frames" is not a claim about anything.
    u64 framesExpected{0};
    u64 framesWritten{0};
    u64 bytesWritten{0};
    u64 elapsedMs{0};
    /// ISO-8601 UTC, second precision, empty when unknown.
    std::string startedUtc;
    std::string finishedUtc;
    /// Free text, never parsed, never interpreted. A diagnosis a human can read.
    std::string note;
};

/// Which check rejected a job, and why. Named rather than a bare bool so a
/// caller can branch on the class and a test can assert the class.
enum class JobErrorKind : u8 {
    EmptyId,
    /// The id is already present in this batch, live or settled. Its own kind
    /// because reporting it as `EmptyId` tells the caller the opposite of the
    /// truth about a field it just filled in.
    DuplicateId,
    EmptyAudioPath,
    EmptyOutputPath,
    EmptyProjectMVersion,
    EmptyLabel,
    BadContentHash,
    BadDimension,
    BadFps,
    BadDuration,
    BadCrf,
    NoScenes,
    BadScene,
    UnknownToken,
    MissingKaraokeSource,
    BadAudioSpec,
};

[[nodiscard]] std::string_view jobErrorKindName(JobErrorKind kind) noexcept;

struct JobError {
    JobErrorKind kind{JobErrorKind::EmptyId};
    /// The offending field, as the schema spells it, so the message can name it.
    std::string field;
    /// Never empty.
    std::string message;

    [[nodiscard]] std::string describe() const;
};

/// Everything needed to re-render this job differently, plus what it produced.
struct RenderJob {
    // ── identity ──
    std::string id;
    /// Display only. Deliberately **not** a filename: `sanitizeFilename()` has
    /// measured gaps (no length cap, no Unicode normalisation), so a label must
    /// never reach a path.
    std::string label;
    std::string createdUtc;

    // ── input ──
    fs::path audioPath;
    /// 64 lowercase hex characters, or empty for "not hashed". See the file header.
    std::string audioSha256;
    f64 durationSeconds{0.0};
    /// The honest progress denominator: `round(durationSeconds * fps)`. Zero is
    /// legal and means "unknown", which is reported as -1 percent rather than as
    /// a fabricated 0.
    u64 expectedFrames{0};

    // ── output geometry and encoding ──
    fs::path outputPath;
    u32 width{1920};
    u32 height{1080};
    u32 fps{60};
    std::string videoCodec{"h264"};
    std::string pixelFormat{"yuv420p"};
    std::string container{"mp4"};
    u32 crf{18};
    std::string encoderPreset{"medium"};
    bool twoPass{false};
    std::string hardwareAccel{"none"};
    u32 gopSize{0};

    std::string audioCodec{"aac"};
    u32 audioSampleRate{48000};
    u32 audioChannels{2};
    u32 audioBitrateKbps{320};

    // ── creative inputs ──
    std::vector<RenderScene> scenes;
    /// One of `Vocabulary::KaraokeMode`. Recorded rather than inferred, because
    /// `burned-in` needs `libavfilter` which is an *optional* dependency: a
    /// receipt claiming a burn-in on a build without it is a lie the schema can
    /// prevent.
    std::string karaokeMode{"none"};
    fs::path karaokeAssPath;

    /// The projectM version that produced this output. Required, not optional:
    /// a render receipt that cannot name its visualizer version cannot answer
    /// "why does this look different", which is the only question a re-render
    /// is ever asked.
    std::string projectmVersion;

    RenderReceipt receipt;

    /// Sidecar path for this job: `outputPath` with `kReceiptSuffix` appended.
    [[nodiscard]] fs::path receiptPath() const;

    /// Whether bytes reached the file without the whole render landing. The one
    /// question a user needs answered before opening a short output.
    [[nodiscard]] bool receiptCarriesPartialOutput() const noexcept;

    [[nodiscard]] std::expected<void, JobError> validate() const;
};

// ─────────────────────────────────────────────────────────────────────────────
// Encoding
// ─────────────────────────────────────────────────────────────────────────────

/// Why a receipt could not be read, or written. Every variant is a refusal;
/// there is no "read it anyway" path.
enum class ReceiptStatus : u8 {
    Ok = 0,
    /// File absent. Distinct from `BadMagic`: absent is not corrupt.
    Missing = 1,
    /// Byte 0 is not `kFormatTag`, so this is not a render receipt at all.
    BadMagic = 2,
    /// Fewer than `kReceiptHeaderBytes` bytes.
    TruncatedHeader = 3,
    /// A reserved header byte was non-zero.
    BadHeader = 4,
    /// Major is not `kSchemaMajor`. See the version rule in the file header.
    UnsupportedVersion = 5,
    /// The TOML body did not parse, or a required section was absent.
    BadBody = 6,
    /// The bytes could not be written, flushed, or atomically replaced.
    WriteFailed = 7,
};

[[nodiscard]] std::string_view receiptStatusName(ReceiptStatus status) noexcept;

struct ReceiptError {
    ReceiptStatus status{ReceiptStatus::BadBody};
    /// Never empty.
    std::string detail;

    [[nodiscard]] std::string describe() const;
};

/// Result of decoding bytes. A struct rather than `std::expected` so the
/// rejected cases stay inspectable without a second API: a caller that wants to
/// know the on-disk major for a log has it here even on the refusal paths.
struct ReceiptDecode {
    ReceiptStatus status{ReceiptStatus::BadBody};
    /// Always populated from the header, even when `status != Ok`.
    u32 major{0};
    u32 minor{0};
    RenderJob job;
    std::string detail;

    [[nodiscard]] bool ok() const noexcept { return status == ReceiptStatus::Ok; }
};

/// Pure: header + TOML body. No filesystem, no logging, no clock.
[[nodiscard]] std::string encodeReceipt(const RenderJob& job);

/// Pure: the inverse, including every refusal. This is what the corruption tests
/// drive, so it takes bytes rather than a path.
[[nodiscard]] ReceiptDecode decodeReceipt(std::string_view bytes);

/// Atomic write: temp file, flush, close, fsync, replace.
///
/// Mirrors `ConfigLoader::save`'s sequence rather than inventing one, and for
/// the same reason that code exists: a receipt renamed into place half-written is
/// worse than no receipt, because a truncated receipt still *looks* like data.
/// The Windows branch is `MoveFileExW` with `MOVEFILE_REPLACE_EXISTING` because
/// `std::filesystem::rename` refuses to overwrite there, which is what broke
/// every settings save after the first one.
///
/// Refuses, before touching the filesystem, when `job.validate()` rejects the
/// job, when the outcome is not terminal, or when the outcome is `Completed`
/// with fewer frames written than expected -- the last of which is the exact
/// confusion `CompletedPartial` exists to prevent, caught at the write boundary
/// rather than shipped.
[[nodiscard]] std::expected<void, ReceiptError> writeReceipt(const RenderJob& job,
                                                            const fs::path& dest);

[[nodiscard]] std::expected<RenderJob, ReceiptError> readReceipt(const fs::path& src);

/// Lowercase hex SHA-256 of a file's contents, streamed in chunks so a 100 MB
/// FLAC does not have to be resident.
///
/// Never called by the queue; see the file header for why. Returns an empty
/// string for an unreadable or absent file, which is the same "not hashed"
/// meaning an empty `audioSha256` already carries.
[[nodiscard]] std::string sha256Hex(const fs::path& file);

} // namespace vc
