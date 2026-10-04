#include "Color.hpp"
#include "core/Logger.hpp"
#include <array>
#include <format>

namespace vc {

namespace {

/// Value of one hex digit, or -1. Hand-rolled because the ranges are three
/// disjoint spans and std::from_chars with a `base` of 16 would also accept
/// "0x" prefixes and a leading '+', neither of which is a colour anybody wrote.
constexpr int hexDigitValue(char c) noexcept {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

} // namespace

std::expected<Color, ColorParseFailure> parseHexColor(std::string_view hex) noexcept {
    if (hex.empty()) {
        return std::unexpected(ColorParseFailure{ColorParseError::Empty, 0});
    }
    if (hex.front() != '#') {
        return std::unexpected(ColorParseFailure{ColorParseError::MissingHash, 0});
    }

    const std::string_view digits = hex.substr(1);
    if (digits.size() != 6 && digits.size() != 8) {
        return std::unexpected(ColorParseFailure{ColorParseError::BadLength, 0});
    }

    // Four channels, index 3 defaulting to opaque. The loop walks digits two at
    // a time because a hex pair is exactly one channel; `channel[i / 2] << 4`
    // is the high nibble of the pair and the OR folds in the low one.
    std::array<u8, 4> channel{0, 0, 0, 255};
    for (std::size_t i = 0; i < digits.size(); ++i) {
        const int value = hexDigitValue(digits[i]);
        if (value < 0) {
            // Offset is into the original string, so the '#' occupies 0 and the
            // first digit is 1 — which is what a substring for the diagnostic
            // needs.
            return std::unexpected(ColorParseFailure{ColorParseError::NotHex, i + 1});
        }
        channel[i / 2] = static_cast<u8>((channel[i / 2] << 4) | value);
    }

    return Color{channel[0], channel[1], channel[2], channel[3]};
}

std::string toHexString(const Color& color) {
    return std::format("#{:02X}{:02X}{:02X}{:02X}", color.r, color.g, color.b, color.a);
}

// ── Legacy entry points, kept so existing callers keep compiling ──────────────
// Both are declared in util/Types.hpp:74-75. They are the *only* reason this TU
// exists from the linker's point of view for every object that already calls
// them, so moving these definitions out of FileUtils.cpp makes Color.cpp a
// required compile unit — see the cmake/Sources.cmake append block.

Color Color::fromHex(std::string_view hex) {
    auto parsed = parseHexColor(hex);
    if (parsed) {
        return *parsed;
    }
    // Loud on purpose. This used to return a value with no error path at all, so
    // a typo'd colour in config.toml became opaque white with nothing in the
    // log; the caller has a real choice available (fall back to a default it
    // chose) and cannot make it without being told the input was refused.
    LOG_WARN("Color::fromHex: refusing \"{}\" ({}) -- returning opaque white; use "
             "vc::parseHexColor() to handle the failure yourself",
             hex, parsed.error().message());
    return Color::white();
}

std::string Color::toHex() const {
    return toHexString(*this);
}

} // namespace vc
