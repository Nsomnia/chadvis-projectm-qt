#include "recorder/RenderJob.hpp"

#include <QByteArrayView>
#include <QCryptographicHash>
#include <QFile>
#include <QString>

#include <toml++/toml.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cmath>
#include <fstream>
#include <sstream>
#include <system_error>

// Q_OS_WIN must be known before the platform block below, so this is explicit
// rather than inherited from whatever Qt header happens to be included first.
#include <QtGlobal>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace vc {

namespace {

/// Every required body section, and whether it must be a table or an array of
/// tables. A missing one is a refusal rather than a default: a receipt that lost
/// a section has lost facts, and a job reconstructed from defaults would
/// re-render something the user never asked for. Which sections are required is
/// part of schema v1.
///
/// `scenes` is the one array: it is a root-level `[[scenes]]`, so it *is* its own
/// section rather than a key inside one. That is not a stylistic choice -- a
/// reader that looks for `scenes` inside `[scenes]` finds nothing, because toml++
/// emits an array-of-tables last (measured: `[audio] [job] [karaoke] [projectm]
/// [receipt] [source] [video] [[scenes]] [[scenes]]`), and the resulting "valid
/// receipt with zero scenes" is exactly the kind of quiet corruption a required
/// section list exists to prevent.
enum class SectionKind : u8 { Table, ArrayOfTables };

struct RequiredSection {
    SectionKind kind;
    std::string_view name;
};

constexpr std::array<RequiredSection, 8> kRequiredSections{{
    {SectionKind::Table, "job"},
    {SectionKind::Table, "source"},
    {SectionKind::Table, "video"},
    {SectionKind::Table, "audio"},
    {SectionKind::ArrayOfTables, "scenes"},
    {SectionKind::Table, "karaoke"},
    {SectionKind::Table, "projectm"},
    {SectionKind::Table, "receipt"},
}};

/// UTF-8 bytes of a path.
///
/// `fs::path::string()` narrows through the platform's *native* encoding, so on
/// Windows a receipt written from a path with a non-ASCII component would be
/// written in the ACP and read back as mojibake -- and a receipt that names the
/// wrong file is worse than one that names none. `u8string()` is the only
/// spelling that is UTF-8 on every platform.
[[nodiscard]] std::string utf8(const fs::path& path) {
    const std::u8string wide = path.u8string();
    return std::string(reinterpret_cast<const char*>(wide.data()), wide.size());
}

/// Inverse of `utf8`. The `char8_t` view of a UTF-8 `std::string` is exactly
/// what `fs::path`'s `char8_t` constructor expects; going through
/// `fs::path(std::string)` instead would narrow through the ACP again.
[[nodiscard]] fs::path pathFromUtf8(std::string_view text) {
    return fs::path(
        std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
}

[[nodiscard]] std::string toStd(std::string_view text) { return std::string(text); }

[[nodiscard]] bool isLowerHex(const std::string& text) {
    if (text.size() != 64) return false;
    return std::all_of(text.begin(), text.end(), [](const char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    });
}

[[nodiscard]] bool isFiniteNonNegative(const f64 value) {
    return std::isfinite(value) && value >= 0.0;
}

// ── TOML body, write ─────────────────────────────────────────────────────────

/// Serialize the job as TOML. Pure; no header, no filesystem.
///
/// `note` and the two timestamps are written even when empty rather than
/// omitted, so every key of schema v1 is present in every file and a diff
/// between two receipts shows a changed value rather than a vanished key.
[[nodiscard]] toml::table serialiseBody(const RenderJob& job) {
    toml::array scenesArr;
    for (const auto& scene : job.scenes) {
        scenesArr.push_back(toml::table{{"preset_name", scene.presetName},
                                        {"duration_seconds", scene.durationSeconds},
                                        {"crossfade_seconds", scene.crossfadeSeconds}});
    }

    const auto& receipt = job.receipt;

    // TOML integers are int64; every count here is a frame count or a byte
    // count whose realistic maximum is far below 2^63, and narrowing on the way
    // out keeps the file portable across the two TOML implementations.
    toml::table body{
        {"job",
         toml::table{{"id", job.id},
                     {"label", job.label},
                     {"created_utc", job.createdUtc}}},
        {"source",
         toml::table{{"audio_path", utf8(job.audioPath)},
                     {"audio_sha256", job.audioSha256},
                     {"duration_seconds", job.durationSeconds},
                     {"expected_frames", static_cast<i64>(job.expectedFrames)}}},
        {"video",
         toml::table{{"output_path", utf8(job.outputPath)},
                     {"width", static_cast<i64>(job.width)},
                     {"height", static_cast<i64>(job.height)},
                     {"fps", static_cast<i64>(job.fps)},
                     {"codec", job.videoCodec},
                     {"pixel_format", job.pixelFormat},
                     {"container", job.container},
                     {"crf", static_cast<i64>(job.crf)},
                     {"preset", job.encoderPreset},
                     {"two_pass", job.twoPass},
                     {"hardware_accel", job.hardwareAccel},
                     {"gop_size", static_cast<i64>(job.gopSize)}}},
        {"audio",
         toml::table{{"codec", job.audioCodec},
                     {"sample_rate", static_cast<i64>(job.audioSampleRate)},
                     {"channels", static_cast<i64>(job.audioChannels)},
                     {"bitrate_kbps", static_cast<i64>(job.audioBitrateKbps)}}},
        {"scenes", std::move(scenesArr)},
        {"karaoke",
         toml::table{{"mode", job.karaokeMode},
                     {"ass_path", utf8(job.karaokeAssPath)}}},
        {"projectm", toml::table{{"version", job.projectmVersion}}},
        {"receipt",
         toml::table{{"outcome", toStd(renderStateSlug(receipt.outcome))},
                     {"frames_expected", static_cast<i64>(receipt.framesExpected)},
                     {"frames_written", static_cast<i64>(receipt.framesWritten)},
                     {"bytes_written", static_cast<i64>(receipt.bytesWritten)},
                     {"elapsed_ms", static_cast<i64>(receipt.elapsedMs)},
                     {"started_utc", receipt.startedUtc},
                     {"finished_utc", receipt.finishedUtc},
                     {"note", receipt.note}}},
    };
    return body;
}

// ── TOML body, read ──────────────────────────────────────────────────────────

// Missing *keys* fall back to the caller's default; missing *sections* are a
// refusal. That split is deliberate: rule 2 of the schema (minor bumps are
// additive) requires a reader to tolerate a key it has never heard of, so
// tolerance is the correct default and the required-section list is where the
// fail-closed behaviour lives instead. The read shape -- `tbl[key]` then
// `node.value<T>()` -- is the one `ConfigParsers.cpp` already established.

[[nodiscard]] std::string readString(const toml::table& tbl, std::string_view key,
                                     std::string fallback = {}) {
    if (const auto node = tbl[key]) {
        if (auto value = node.value<std::string>()) return *value;
    }
    return fallback;
}

[[nodiscard]] i64 readInt(const toml::table& tbl, std::string_view key,
                          i64 fallback) {
    if (const auto node = tbl[key]) {
        if (auto value = node.value<i64>()) return *value;
    }
    return fallback;
}

[[nodiscard]] f64 readFloat(const toml::table& tbl, std::string_view key,
                            f64 fallback) {
    if (const auto node = tbl[key]) {
        if (auto value = node.value<f64>()) return *value;
    }
    return fallback;
}

[[nodiscard]] bool readBool(const toml::table& tbl, std::string_view key,
                            bool fallback) {
    if (const auto node = tbl[key]) {
        if (auto value = node.value<bool>()) return *value;
    }
    return fallback;
}

[[nodiscard]] u32 readU32(const toml::table& tbl, std::string_view key,
                          u32 fallback) {
    const i64 raw = readInt(tbl, key, static_cast<i64>(fallback));
    return raw < 0 ? fallback : static_cast<u32>(raw);
}

[[nodiscard]] u64 readU64(const toml::table& tbl, std::string_view key,
                          u64 fallback) {
    const i64 raw = readInt(tbl, key, static_cast<i64>(fallback));
    return raw < 0 ? fallback : static_cast<u64>(raw);
}

/// The single shape every validation failure returns, so no branch can forget to
/// wrap a `JobError` in an `unexpected`.
[[nodiscard]] std::unexpected<JobError> makeError(JobErrorKind kind, std::string field,
                                                  std::string message) {
    return std::unexpected(JobError{std::move(kind), std::move(field), std::move(message)});
}

// ── Atomic write helpers ─────────────────────────────────────────────────────
//
// Deliberately duplicated from `ConfigLoader.cpp`'s file-local
// flushToDisk/replaceFile/discardTemp rather than promoted to `FileUtils.hpp`:
// that file is not mine to edit, and those helpers log through `Logger` with a
// hardcoded "Config save:" prefix, so all three callers would want their own
// message anyway. The sequence and its two platform-specific details are
// load-bearing and are reproduced exactly; see the writeReceipt comment.

void flushToDisk(const fs::path& path) {
#ifdef Q_OS_WIN
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    const BOOL flushed = ::FlushFileBuffers(h);
    ::CloseHandle(h);
    (void)flushed;
#else
    // fsync(2) fails with EBADF on a descriptor not open for writing, so this
    // must be O_WRONLY -- and must NOT carry O_TRUNC, which would empty the very
    // file being renamed into place.
    const int fd = ::open(path.c_str(), O_WRONLY);
    if (fd < 0) return;
    const int rc = ::fsync(fd);
    ::close(fd);
    (void)rc;
#endif
}

[[nodiscard]] bool replaceFile(const fs::path& temp, const fs::path& dest,
                               std::error_code& ec) {
#ifdef Q_OS_WIN
    if (::MoveFileExW(temp.c_str(), dest.c_str(),
                      MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        ec.clear();
        return true;
    }
    ec = std::error_code(static_cast<int>(::GetLastError()), std::system_category());
    return false;
#else
    // The error_code overload returns void; success is a cleared ec.
    fs::rename(temp, dest, ec);
    return !ec;
#endif
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// State
// ─────────────────────────────────────────────────────────────────────────────

std::string_view renderStateName(const RenderState state) noexcept {
    switch (state) {
        case RenderState::Queued: return "queued";
        case RenderState::Rendering: return "rendering";
        case RenderState::Completed: return "completed";
        case RenderState::CompletedPartial: return "completed-partial";
        case RenderState::FailedRetryable: return "failed-retryable";
        case RenderState::FailedPermanent: return "failed-permanent";
        case RenderState::Cancelled: return "cancelled";
    }
    return "unknown";
}

std::string_view renderStateSlug(const RenderState state) noexcept {
    // Slug and display name are separate on purpose: the receipt stores the slug,
    // and a display name that a future UI rewords must not rewrite history.
    return renderStateName(state);
}

bool isTerminal(const RenderState state) noexcept {
    return state == RenderState::Completed || state == RenderState::CompletedPartial ||
           state == RenderState::FailedRetryable ||
           state == RenderState::FailedPermanent || state == RenderState::Cancelled;
}

bool isRetryable(const RenderState state) noexcept {
    return state == RenderState::FailedRetryable;
}

bool leavesPartialOutput(const RenderState state) noexcept {
    return isTerminal(state) && state != RenderState::Completed;
}

std::optional<RenderState> renderStateFromSlug(const std::string_view slug) noexcept {
    static constexpr std::array<RenderState, 7> kAll{
        RenderState::Queued,        RenderState::Rendering,
        RenderState::Completed,     RenderState::CompletedPartial,
        RenderState::FailedRetryable, RenderState::FailedPermanent,
        RenderState::Cancelled};
    for (const RenderState state : kAll) {
        if (renderStateSlug(state) == slug) return state;
    }
    return std::nullopt;
}

// ─────────────────────────────────────────────────────────────────────────────
// Vocabularies
// ─────────────────────────────────────────────────────────────────────────────

bool isKnownToken(const Vocabulary vocab, const std::string_view token) noexcept {
    if (token.empty()) return false;
    const auto index = static_cast<usize>(vocab);
    if (index >= kVocabularies.size()) return false;
    const auto& row = kVocabularies[index];
    return std::find(row.begin(), row.end(), token) != row.end();
}

std::string_view jobErrorKindName(const JobErrorKind kind) noexcept {
    switch (kind) {
        case JobErrorKind::EmptyId: return "empty-id";
        case JobErrorKind::DuplicateId: return "duplicate-id";
        case JobErrorKind::EmptyAudioPath: return "empty-audio-path";
        case JobErrorKind::EmptyOutputPath: return "empty-output-path";
        case JobErrorKind::EmptyProjectMVersion: return "empty-projectm-version";
        case JobErrorKind::EmptyLabel: return "empty-label";
        case JobErrorKind::BadContentHash: return "bad-content-hash";
        case JobErrorKind::BadDimension: return "bad-dimension";
        case JobErrorKind::BadFps: return "bad-fps";
        case JobErrorKind::BadDuration: return "bad-duration";
        case JobErrorKind::BadCrf: return "bad-crf";
        case JobErrorKind::NoScenes: return "no-scenes";
        case JobErrorKind::BadScene: return "bad-scene";
        case JobErrorKind::UnknownToken: return "unknown-token";
        case JobErrorKind::MissingKaraokeSource: return "missing-karaoke-source";
        case JobErrorKind::BadAudioSpec: return "bad-audio-spec";
    }
    return "unknown";
}

std::string JobError::describe() const {
    return std::string(jobErrorKindName(kind)) + " on '" + field + "': " + message;
}

std::string_view receiptStatusName(const ReceiptStatus status) noexcept {
    switch (status) {
        case ReceiptStatus::Ok: return "ok";
        case ReceiptStatus::Missing: return "missing";
        case ReceiptStatus::BadMagic: return "bad-magic";
        case ReceiptStatus::TruncatedHeader: return "truncated-header";
        case ReceiptStatus::BadHeader: return "bad-header";
        case ReceiptStatus::UnsupportedVersion: return "unsupported-version";
        case ReceiptStatus::BadBody: return "bad-body";
        case ReceiptStatus::WriteFailed: return "write-failed";
    }
    return "unknown";
}

std::string ReceiptError::describe() const {
    return std::string(receiptStatusName(status)) + ": " + detail;
}

// ─────────────────────────────────────────────────────────────────────────────
// Job
// ─────────────────────────────────────────────────────────────────────────────

fs::path RenderJob::receiptPath() const {
    fs::path sidecar = outputPath;
    sidecar += kReceiptSuffix;
    return sidecar;
}

bool RenderJob::receiptCarriesPartialOutput() const noexcept {
    // Byte count first, verdict second. A failed job that wrote nothing leaves
    // nothing behind and must not be described as leaving a partial file; a
    // "completed" job that wrote nothing but claims frames is caught at the
    // write boundary in writeReceipt instead.
    return receipt.bytesWritten > 0 && receipt.outcome != RenderState::Completed;
}

std::expected<void, JobError> RenderJob::validate() const {
    if (id.empty())
        return makeError(JobErrorKind::EmptyId, "job.id", "a job needs a unique id");
    if (label.empty())
        return makeError(JobErrorKind::EmptyLabel, "job.label",
                         "the receipt's label is the only human-readable name");
    if (audioPath.empty())
        return makeError(JobErrorKind::EmptyAudioPath, "source.audio_path",
                         "there is no input to render");
    if (outputPath.empty())
        return makeError(JobErrorKind::EmptyOutputPath, "video.output_path",
                         "there is nowhere to write");
    if (projectmVersion.empty())
        return makeError(JobErrorKind::EmptyProjectMVersion, "projectm.version",
                         "a receipt that cannot name its visualizer cannot explain "
                         "why two renders differ");

    if (!audioSha256.empty() && !isLowerHex(audioSha256))
        return makeError(JobErrorKind::BadContentHash, "source.audio_sha256",
                         "must be 64 lowercase hex characters, or empty for "
                         "'not hashed'");

    if (!(std::isfinite(durationSeconds) && durationSeconds > 0.0))
        return makeError(JobErrorKind::BadDuration, "source.duration_seconds",
                         "must be a finite value greater than zero");

    if (width == 0 || height == 0)
        return makeError(JobErrorKind::BadDimension, "video.width/video.height",
                         "must both be greater than zero");
    // Odd dimensions are refused here rather than at `avformat_write_header`,
    // where the encoder is already committed. yuv420p is subsampled 2x2, so an
    // odd extent is not representable at all.
    if ((width % 2) != 0 || (height % 2) != 0)
        return makeError(JobErrorKind::BadDimension, "video.width/video.height",
                         "must both be even for a subsampled pixel format");

    if (fps == 0 || fps > 480)
        return makeError(JobErrorKind::BadFps, "video.fps",
                         "must be between 1 and 480 inclusive");
    // 0-51 is libx264/libx265's CRF range and 0-63 is libvpx-vp9's, so 63 is the
    // only bound the frozen vocabulary can honestly enforce.
    if (crf > 63)
        return makeError(JobErrorKind::BadCrf, "video.crf", "must be at most 63");

    if (audioSampleRate == 0 || audioChannels == 0 || audioChannels > 2 ||
        audioBitrateKbps == 0)
        return makeError(JobErrorKind::BadAudioSpec, "audio.*",
                         "sample rate, bitrate and 1-2 channels are all required");

    if (scenes.empty())
        return makeError(JobErrorKind::NoScenes, "scenes",
                         "at least one scene is required; an empty list has "
                         "nothing to render");
    for (const auto& scene : scenes) {
        if (scene.presetName.empty())
            return makeError(JobErrorKind::BadScene, "scenes.preset_name",
                             "every scene names a preset");
        if (!(std::isfinite(scene.durationSeconds) && scene.durationSeconds > 0.0))
            return makeError(JobErrorKind::BadScene, "scenes.duration_seconds",
                             "every scene must hold for a finite, positive time");
        if (!isFiniteNonNegative(scene.crossfadeSeconds))
            return makeError(JobErrorKind::BadScene, "scenes.crossfade_seconds",
                             "must be a finite, non-negative number of seconds");
        // A crossfade longer than the scene it belongs to has no second segment
        // to cross into, which is silent nonsense rather than a legal value.
        if (scene.crossfadeSeconds >= scene.durationSeconds)
            return makeError(JobErrorKind::BadScene, "scenes.crossfade_seconds",
                             "must be shorter than the scene that carries it");
    }

    struct TokenField {
        Vocabulary vocab;
        std::string_view field;
        const std::string* value;
    };
    static constexpr std::array<TokenField, 6> kTokens{{
        {Vocabulary::VideoCodec, "video.codec", nullptr},
        {Vocabulary::PixelFormat, "video.pixel_format", nullptr},
        {Vocabulary::Container, "video.container", nullptr},
        {Vocabulary::EncoderPreset, "video.preset", nullptr},
        {Vocabulary::AudioCodec, "audio.codec", nullptr},
        {Vocabulary::HardwareAccel, "video.hardware_accel", nullptr},
    }};
    const std::array<const std::string*, 6> kValues{
        &videoCodec, &pixelFormat, &container, &encoderPreset, &audioCodec,
        &hardwareAccel};
    for (usize i = 0; i < kTokens.size(); ++i) {
        if (isKnownToken(kTokens[i].vocab, *kValues[i])) continue;
        return makeError(JobErrorKind::UnknownToken, std::string(kTokens[i].field),
                         "'" + *kValues[i] + "' is not in the schema's frozen "
                         "vocabulary");
    }

    if (!isKnownToken(Vocabulary::KaraokeMode, karaokeMode))
        return makeError(JobErrorKind::UnknownToken, "karaoke.mode",
                         "'" + karaokeMode + "' is not one of none/muxed/burned-in");
    // Fail closed rather than "render without lyrics": a receipt claiming
    // `burned-in` on a build without libavfilter is a lie the schema can prevent.
    if (karaokeMode != "none" && karaokeAssPath.empty())
        return makeError(JobErrorKind::MissingKaraokeSource, "karaoke.ass_path",
                         "mode '" + karaokeMode + "' needs the .ass to use");

    return {};
}

// ─────────────────────────────────────────────────────────────────────────────
// Encoding
// ─────────────────────────────────────────────────────────────────────────────

std::string encodeReceipt(const RenderJob& job) {
    std::string bytes;
    bytes.reserve(kReceiptHeaderBytes + 2048);

    const auto header16 = static_cast<u16>(kReceiptHeaderBytes);
    const std::array<u8, kReceiptHeaderBytes> header{
        kFormatTag,
        static_cast<u8>(kSchemaMajor),
        static_cast<u8>(kSchemaMinor),
        0u,
        static_cast<u8>(header16 & 0xFFu),
        static_cast<u8>((header16 >> 8) & 0xFFu),
        0u,
        0u,
    };
    for (const u8 byte : header) bytes.push_back(static_cast<char>(byte));

    // toml++ 3.4 serialises through `operator<<`, not a `format()` free function
    // -- the same spelling `ConfigLoader::save` uses.
    std::ostringstream body;
    body << serialiseBody(job);
    bytes += body.str();
    return bytes;
}
ReceiptDecode decodeReceipt(const std::string_view bytes) {
    ReceiptDecode out;

    if (bytes.size() < kReceiptHeaderBytes) {
        out.status = ReceiptStatus::TruncatedHeader;
        out.detail = "only " + std::to_string(bytes.size()) + " bytes; the header is " +
                     std::to_string(kReceiptHeaderBytes);
        return out;
    }

    const auto* raw = reinterpret_cast<const u8*>(bytes.data());
    if (raw[0] != kFormatTag) {
        // A TOML file cannot reach here: its first byte is '[' or whitespace, so
        // a body whose header was stripped is rejected rather than half-believed.
        out.status = ReceiptStatus::BadMagic;
        out.detail = "first byte is 0x" +
                     std::string(1, "0123456789abcdef"[(raw[0] >> 4) & 0x0F]) +
                     std::string(1, "0123456789abcdef"[raw[0] & 0x0F]) +
                     ", not the render-receipt format tag";
        return out;
    }

    out.major = raw[1];
    out.minor = raw[2];
    const u16 headerBytes =
        static_cast<u16>(static_cast<u16>(raw[4]) | (static_cast<u16>(raw[5]) << 8));

    if (raw[3] != 0 || raw[6] != 0 || raw[7] != 0) {
        out.status = ReceiptStatus::BadHeader;
        out.detail = "a reserved header byte is non-zero";
        return out;
    }
    if (headerBytes < kReceiptHeaderBytes) {
        out.status = ReceiptStatus::BadHeader;
        out.detail = "header length " + std::to_string(headerBytes) + " is below " +
                     std::to_string(kReceiptHeaderBytes);
        return out;
    }
    if (bytes.size() < headerBytes) {
        out.status = ReceiptStatus::TruncatedHeader;
        out.detail = "header claims " + std::to_string(headerBytes) +
                     " bytes but only " + std::to_string(bytes.size()) + " are present";
        return out;
    }

    // The whole version rule in one place. Unknown major is refused in *both*
    // directions; unknown minor is tolerated, which is only sound because a minor
    // bump is additive by rule.
    if (out.major != kSchemaMajor) {
        out.status = ReceiptStatus::UnsupportedVersion;
        out.detail = "schema major " + std::to_string(out.major) + ", this build reads " +
                     std::to_string(kSchemaMajor) + "; refusing rather than "
                     "substituting defaults for fields it does not understand";
        return out;
    }

    toml::table root;
    try {
        root = toml::parse(bytes.substr(headerBytes));
    } catch (const std::exception& err) {
        // `std::exception`, deliberately, not `toml::parse_error`. Measured on
        // toml++ 3.4.0: a malformed *table header* (`[a!dio]`) throws a plain
        // `std::runtime_error` -- "Error while parsing table header: expected ']',
        // saw '!'" -- which is not derived from `toml::parse_error`, so the
        // narrow catch `ConfigLoader.cpp` uses lets it escape. A receipt is a
        // file on disk that anything can corrupt, so the guard here is the one
        // that actually holds. Cost of the broad catch: one bad body produces
        // `BadBody` with libav-style wording rather than a TOML class name.
        out.status = ReceiptStatus::BadBody;
        out.detail = std::string("TOML body is unreadable: ") + err.what();
        return out;
    }

    std::array<const toml::table*, kRequiredSections.size()> views{};
    const toml::array* scenesArray = nullptr;
    for (usize i = 0; i < kRequiredSections.size(); ++i) {
        const RequiredSection& required = kRequiredSections[i];
        const toml::table* section = nullptr;
        const toml::array* array = nullptr;
        if (const auto node = root[required.name]) {
            if (required.kind == SectionKind::Table) {
                section = node.as_table();
            } else {
                array = node.as_array();
            }
        }
        if ((required.kind == SectionKind::Table && section == nullptr) ||
            (required.kind == SectionKind::ArrayOfTables && array == nullptr)) {
            out.status = ReceiptStatus::BadBody;
            out.detail = std::string("required ") +
                         (required.kind == SectionKind::Table ? "section [" : "array [[") +
                         std::string(required.name) +
                         (required.kind == SectionKind::Table ? "]" : "]]") +
                         " is missing or the wrong type; a receipt with a lost "
                         "section has lost facts";
            return out;
        }
        views[i] = section;
        if (array != nullptr) scenesArray = array;
    }
    const toml::table& jobTbl = *views[0];
    const toml::table& sourceTbl = *views[1];
    const toml::table& videoTbl = *views[2];
    const toml::table& audioTbl = *views[3];
    const toml::table& karaokeTbl = *views[5];
    const toml::table& projectmTbl = *views[6];
    const toml::table& receiptTbl = *views[7];

    RenderJob job;
    job.id = readString(jobTbl, "id");
    job.label = readString(jobTbl, "label");
    job.createdUtc = readString(jobTbl, "created_utc");

    job.audioPath = pathFromUtf8(readString(sourceTbl, "audio_path"));
    job.audioSha256 = readString(sourceTbl, "audio_sha256");
    job.durationSeconds = readFloat(sourceTbl, "duration_seconds", 0.0);
    job.expectedFrames = readU64(sourceTbl, "expected_frames", 0);

    job.outputPath = pathFromUtf8(readString(videoTbl, "output_path"));
    job.width = readU32(videoTbl, "width", 1920);
    job.height = readU32(videoTbl, "height", 1080);
    job.fps = readU32(videoTbl, "fps", 60);
    job.videoCodec = readString(videoTbl, "codec", "h264");
    job.pixelFormat = readString(videoTbl, "pixel_format", "yuv420p");
    job.container = readString(videoTbl, "container", "mp4");
    job.crf = readU32(videoTbl, "crf", 18);
    job.encoderPreset = readString(videoTbl, "preset", "medium");
    job.twoPass = readBool(videoTbl, "two_pass", false);
    job.hardwareAccel = readString(videoTbl, "hardware_accel", "none");
    job.gopSize = readU32(videoTbl, "gop_size", 0);

    job.audioCodec = readString(audioTbl, "codec", "aac");
    job.audioSampleRate = readU32(audioTbl, "sample_rate", 48000);
    job.audioChannels = readU32(audioTbl, "channels", 2);
    job.audioBitrateKbps = readU32(audioTbl, "bitrate_kbps", 320);

    for (const auto& node : *scenesArray) {
        const toml::table* sceneTbl = node.as_table();
        // A non-table element cannot be a scene, and inventing one would
        // silently shorten the render.
        if (sceneTbl == nullptr) continue;
        job.scenes.push_back(RenderScene{
            readString(*sceneTbl, "preset_name"),
            readFloat(*sceneTbl, "duration_seconds", 0.0),
            readFloat(*sceneTbl, "crossfade_seconds", 0.0),
        });
    }

    job.karaokeMode = readString(karaokeTbl, "mode", "none");
    job.karaokeAssPath = pathFromUtf8(readString(karaokeTbl, "ass_path"));
    job.projectmVersion = readString(projectmTbl, "version");

    // An unreadable outcome is a refusal, not a default. Defaulting would turn
    // a corrupt receipt into a claim of success.
    const std::string outcome = readString(receiptTbl, "outcome");
    const auto parsed = renderStateFromSlug(outcome);
    if (!parsed) {
        out.status = ReceiptStatus::BadBody;
        out.detail = "receipt.outcome '" + outcome + "' is not a known state slug";
        return out;
    }
    job.receipt.outcome = *parsed;
    job.receipt.framesExpected = readU64(receiptTbl, "frames_expected", 0);
    job.receipt.framesWritten = readU64(receiptTbl, "frames_written", 0);
    job.receipt.bytesWritten = readU64(receiptTbl, "bytes_written", 0);
    job.receipt.elapsedMs = readU64(receiptTbl, "elapsed_ms", 0);
    job.receipt.startedUtc = readString(receiptTbl, "started_utc");
    job.receipt.finishedUtc = readString(receiptTbl, "finished_utc");
    job.receipt.note = readString(receiptTbl, "note");

    out.job = std::move(job);
    out.status = ReceiptStatus::Ok;
    return out;
}

std::expected<void, ReceiptError> writeReceipt(const RenderJob& job,
                                               const fs::path& dest) {
    const auto fail = [](ReceiptStatus status, std::string detail) {
        return std::unexpected(ReceiptError{status, std::move(detail)});
    };

    if (auto invalid = job.validate(); !invalid)
        return fail(ReceiptStatus::BadBody, invalid.error().describe());
    if (!isTerminal(job.receipt.outcome))
        return fail(ReceiptStatus::BadBody,
                    "receipt outcome '" + std::string(renderStateName(job.receipt.outcome)) +
                        "' is not terminal; a receipt records what a finished job did");
    // The confusion this whole state exists to prevent, caught at the boundary.
    if (job.receipt.outcome == RenderState::Completed &&
        job.receipt.framesWritten < job.receipt.framesExpected)
        return fail(ReceiptStatus::BadBody,
                    "outcome 'completed' with " + std::to_string(job.receipt.framesWritten) +
                        " of " + std::to_string(job.receipt.framesExpected) +
                        " frames written; that is completed-partial");

    fs::path tempPath = dest;
    tempPath += ".tmp";

    {
        std::ofstream file(tempPath, std::ios::binary | std::ios::trunc);
        if (!file) {
            std::error_code removeEc;
            fs::remove(tempPath, removeEc);
            return fail(ReceiptStatus::WriteFailed,
                        "could not open " + tempPath.string() + " for writing");
        }
        const std::string bytes = encodeReceipt(job);
        file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        file.flush();
        // Read before close: a partially flushed stream renamed over the real
        // receipt is exactly the corruption this dance exists to prevent.
        const bool complete = static_cast<bool>(file);
        file.close();
        if (!complete) {
            std::error_code removeEc;
            fs::remove(tempPath, removeEc);
            return fail(ReceiptStatus::WriteFailed,
                        "failed while writing " + tempPath.string());
        }
    }

    flushToDisk(tempPath);

    std::error_code ec;
    if (!replaceFile(tempPath, dest, ec)) {
        std::error_code removeEc;
        fs::remove(tempPath, removeEc);
        return fail(ReceiptStatus::WriteFailed,
                    "could not replace " + dest.string() + ": " + ec.message());
    }
    return {};
}

std::expected<RenderJob, ReceiptError> readReceipt(const fs::path& src) {
    std::error_code ec;
    if (!fs::exists(src, ec))
        return std::unexpected(ReceiptError{ReceiptStatus::Missing,
                                            "no receipt at " + src.string()});

    std::ifstream file(src, std::ios::binary);
    if (!file)
        return std::unexpected(
            ReceiptError{ReceiptStatus::Missing, "could not open " + src.string()});
    std::string bytes((std::istreambuf_iterator<char>(file)),
                      std::istreambuf_iterator<char>());

    ReceiptDecode decoded = decodeReceipt(bytes);
    if (!decoded.ok())
        return std::unexpected(
            ReceiptError{decoded.status, std::move(decoded.detail)});
    return std::move(decoded.job);
}

std::string sha256Hex(const fs::path& file) {
    QFile handle(QString::fromStdString(file.string()));
    if (!handle.open(QIODevice::ReadOnly)) return {};

    QCryptographicHash hash(QCryptographicHash::Sha256);
    // 64 KiB is one libuv-sized read: large enough that the syscall is not the
    // cost, small enough to stay off the large-allocation path for a 100 MB FLAC.
    std::array<char, 64 * 1024> chunk{};
    while (true) {
        const qint64 got = handle.read(chunk.data(), static_cast<qint64>(chunk.size()));
        if (got <= 0) break;
        hash.addData(QByteArrayView(chunk.data(), static_cast<usize>(got)));
    }
    // QByteArray::toHex is lowercase, which is what isLowerHex() requires.
    return hash.result().toHex().toStdString();
}

} // namespace vc
