#pragma once
/**
 * @file PcmFormat.hpp
 * @file Purpose: pure, device-free conversion of interleaved PCM into f32.
 *
 * @section Why this exists
 * The conversion used to live inline in AudioEngine::processAudioBuffer, where
 * it had an `if Float / else if Int16` with **no else**: any other sample
 * format fell through and the engine pushed `scratchBuffer_`'s *previous*
 * contents into the visualizer and recorder queues. An Int32 sink therefore
 * produced stale audio and no diagnostic, and the branch could not be tested at
 * all because it needed a live audio device.
 *
 * Everything here is a free function over spans. No Qt object, no device, no
 * logging, and no dependency upward into the QML bridge layer -- which is what
 * makes the unsupported-format contract testable headlessly.
 *
 * @section Contract
 * convertInterleaved() is total: it either writes exactly as many floats as the
 * byte count implies, or it writes nothing and returns the reason. It never
 * partially fills, never truncates, and never falls back to whatever the
 * destination already held.
 */

#include "util/Types.hpp"

#include <QAudioFormat>

#include <cstddef>
#include <expected>
#include <span>
#include <string>
#include <string_view>

namespace vc::pcm {

/// Largest interleaved sample count a single conversion accepts.
///
/// This is the hard ceiling that used to be AudioEngine::kMaxScratchSamples, a
/// private constant with a silent `return` when a buffer exceeded it. It lives
/// here so the boundary is a property of the conversion rather than of the
/// caller's bookkeeping, and so it can be pinned by a test.
inline constexpr usize kMaxInterleavedSamples = 16384;

/// Smallest conversion window AudioEngine will honour for a configured buffer
/// size. Below this a platform sink callback would be dropped rather than
/// converted, so smaller requests are clamped up rather than obeyed -- the UI
/// states the clamp instead of silently disagreeing with the stored value.
inline constexpr usize kMinInterleavedSamples = 4096;

/// Exact i16 full-scale -> [-1.0f, 1.0f) scale factor.
///
/// A power of two, so `static_cast<f32>(i16) * kInt16Scale` is exact for every
/// one of the 65536 inputs: the cast is exact (16 significand bits into f32's
/// 24) and the scaling is exact (no mantissa change, and the smallest result
/// 2^-15 is comfortably normal). Exposed as a named constant so a test can
/// assert against it instead of re-deriving the divisor -- the recorder's
/// centisecond-to-millisecond defect was exactly a re-derived constant.
inline constexpr f32 kInt16Scale = 1.0f / 32768.0f;

/// Why a conversion was refused. Every value is a condition the caller could
/// otherwise only discover by getting silence.
enum class ConversionError : u8 {
    /// The sample format has no defined conversion here (Unknown, UInt8, Int32,
    /// NSampleFormats, or a value from a future Qt).
    UnsupportedSampleFormat,
    /// The byte count is not a whole number of samples of this format.
    SourceNotSampleAligned,
    /// The destination cannot hold every sample the source decodes to. The
    /// engine reaches this when a sink callback is larger than its window.
    DestinationTooSmall,
    /// The source decodes to more than kMaxInterleavedSamples, so converting it
    /// would mean allocating inside the audio callback.
    TooManySamples,
};

/// Failure detail. Carries the offending values so the log line and the UI can
/// name them instead of saying "conversion failed".
struct FormatError {
    ConversionError reason{ConversionError::UnsupportedSampleFormat};
    QAudioFormat::SampleFormat sampleFormat{QAudioFormat::Unknown};
    usize sourceBytes{0};
    usize destinationSamples{0};
    usize sampleCount{0};

    /// Human-readable, log- and UI-ready. Never throws, never allocates more
    /// than a short sentence.
    [[nodiscard]] std::string describe() const;
};

/// Whether this layer can convert the format at all.
[[nodiscard]] bool isSupported(QAudioFormat::SampleFormat sampleFormat) noexcept;

/// Width of one sample of the format in bytes, or 0 when unsupported.
///
/// Deliberately 0 rather than a guess for an unsupported format: a caller that
/// needs a width for buffer arithmetic wants to know it cannot trust one.
[[nodiscard]] usize bytesPerSample(QAudioFormat::SampleFormat sampleFormat) noexcept;

/// Stable name for a sample format, for logs and error text. Never returns
/// "Unknown" for a genuinely unknown *value*, only for QAudioFormat::Unknown.
[[nodiscard]] std::string_view sampleFormatName(QAudioFormat::SampleFormat sampleFormat) noexcept;

/// How many interleaved f32 samples `sourceBytes` of this format decodes to.
/// Returns 0 for an unsupported format, which is also the answer for 0 bytes --
/// check isSupported() first if the distinction matters.
[[nodiscard]] usize interleavedSampleCount(usize sourceBytes,
                                           QAudioFormat::SampleFormat sampleFormat) noexcept;

/// The conversion window to use for a requested buffer size, clamped into
/// [kMinInterleavedSamples, kMaxInterleavedSamples].
///
/// A `constexpr` clamp rather than a runtime check so a caller cannot disagree
/// with this about the window it just resized a buffer to.
[[nodiscard]] constexpr usize conversionWindow(usize requestedSamples) noexcept {
    if (requestedSamples < kMinInterleavedSamples) return kMinInterleavedSamples;
    if (requestedSamples > kMaxInterleavedSamples) return kMaxInterleavedSamples;
    return requestedSamples;
}

/// Converts interleaved PCM of `sampleFormat` into `destination`.
///
/// On success `destination[0 .. count)` holds the decoded samples, where
/// `count == source.size() / bytesPerSample(sampleFormat)`; nothing past `count`
/// is touched, so a stale value left in the destination can never reach the
/// queues. An empty source is a success that writes nothing.
///
/// @return `std::expected<void, FormatError>`: an error for an unsupported
///         format, a byte count that is not a whole number of samples, a
///         destination that is too small, or a sample count above
///         kMaxInterleavedSamples.
[[nodiscard]] std::expected<void, FormatError>
convertInterleaved(std::span<const std::byte> source, std::span<f32> destination,
                   QAudioFormat::SampleFormat sampleFormat);

} // namespace vc::pcm
