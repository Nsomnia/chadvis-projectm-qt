#include "PcmFormat.hpp"

#include <cstring>

namespace vc::pcm {

bool isSupported(QAudioFormat::SampleFormat sampleFormat) noexcept {
    return sampleFormat == QAudioFormat::Float || sampleFormat == QAudioFormat::Int16;
}

usize bytesPerSample(QAudioFormat::SampleFormat sampleFormat) noexcept {
    switch (sampleFormat) {
        case QAudioFormat::Float:
            return sizeof(f32);
        case QAudioFormat::Int16:
            return sizeof(i16);
        default:
            return 0;
    }
}

std::string_view sampleFormatName(QAudioFormat::SampleFormat sampleFormat) noexcept {
    switch (sampleFormat) {
        case QAudioFormat::Unknown:
            return "Unknown";
        case QAudioFormat::UInt8:
            return "UInt8";
        case QAudioFormat::Int16:
            return "Int16";
        case QAudioFormat::Int32:
            return "Int32";
        case QAudioFormat::Float:
            return "Float";
        case QAudioFormat::NSampleFormats:
            return "NSampleFormats";
    }
    // A value from a future Qt that this switch has not learned yet. Reported by
    // name rather than as a number so an unsupported-format log line is still
    // actionable instead of "SampleFormat(9)".
    return "UnrecognisedSampleFormat";
}

usize interleavedSampleCount(usize sourceBytes, QAudioFormat::SampleFormat sampleFormat) noexcept {
    const usize width = bytesPerSample(sampleFormat);
    return width == 0 ? 0 : sourceBytes / width;
}

std::string FormatError::describe() const {
    switch (reason) {
        case ConversionError::UnsupportedSampleFormat:
            return "unsupported PCM sample format " + std::string{sampleFormatName(sampleFormat)} +
                   " (only Int16 and Float are decoded)";
        case ConversionError::SourceNotSampleAligned: {
            const usize width = bytesPerSample(sampleFormat);
            return "PCM byte count " + std::to_string(sourceBytes) + " is not a whole number of " +
                   std::string{sampleFormatName(sampleFormat)} + " samples (" +
                   std::to_string(width) + " bytes per sample)";
        }
        case ConversionError::DestinationTooSmall:
            return "PCM conversion needs " + std::to_string(sampleCount) +
                   " samples but the destination holds " + std::to_string(destinationSamples);
        case ConversionError::TooManySamples:
            return "PCM conversion of " + std::to_string(sampleCount) + " samples exceeds the " +
                   std::to_string(kMaxInterleavedSamples) + "-sample ceiling";
    }
    return "unknown PCM conversion error";
}

std::expected<void, FormatError> convertInterleaved(std::span<const std::byte> source,
                                                    std::span<f32> destination,
                                                    QAudioFormat::SampleFormat sampleFormat) {
    const usize width = bytesPerSample(sampleFormat);
    if (width == 0) {
        return std::unexpected(FormatError{
                .reason = ConversionError::UnsupportedSampleFormat,
                .sampleFormat = sampleFormat,
                .sourceBytes = source.size(),
                .destinationSamples = destination.size(),
                .sampleCount = 0,
        });
    }

    if (source.size() % width != 0) {
        return std::unexpected(FormatError{
                .reason = ConversionError::SourceNotSampleAligned,
                .sampleFormat = sampleFormat,
                .sourceBytes = source.size(),
                .destinationSamples = destination.size(),
                .sampleCount = source.size() / width,
        });
    }

    const usize count = source.size() / width;
    if (count > destination.size()) {
        return std::unexpected(FormatError{
                .reason = ConversionError::DestinationTooSmall,
                .sampleFormat = sampleFormat,
                .sourceBytes = source.size(),
                .destinationSamples = destination.size(),
                .sampleCount = count,
        });
    }
    if (count > kMaxInterleavedSamples) {
        return std::unexpected(FormatError{
                .reason = ConversionError::TooManySamples,
                .sampleFormat = sampleFormat,
                .sourceBytes = source.size(),
                .destinationSamples = destination.size(),
                .sampleCount = count,
        });
    }

    // Nothing decoded from nothing. Not an error: an empty callback is a normal
    // thing for a sink to deliver, and it must not be reported as a fault.
    if (count == 0) {
        return {};
    }

    if (sampleFormat == QAudioFormat::Float) {
        // A whole-buffer memcpy rather than a per-sample loop: it is the exact
        // conversion for Float (every bit pattern survives, including -0.0,
        // infinities and NaN payloads) and it is the one branch that runs on
        // every callback for a float sink.
        std::memcpy(destination.data(), source.data(), count * sizeof(f32));
        return {};
    }

    for (usize index = 0; index < count; ++index) {
        i16 sample = 0;
        // memcpy rather than a reinterpret_cast to i16*: the buffer's declared
        // format is untrusted here (it comes from the platform), and reading it
        // through a pointer of a type it may not actually be is exactly the
        // aliasing bug the old inlined conversion could not rule out.
        std::memcpy(&sample, source.data() + index * sizeof(i16), sizeof(i16));
        destination[index] = static_cast<f32>(sample) * kInt16Scale;
    }
    return {};
}

} // namespace vc::pcm
