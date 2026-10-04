#pragma once
// Color.hpp - The hex codec for vc::Color.
//
// Why this file exists: parsing and formatting a colour is not a file-system
// operation, and it lived in FileUtils.cpp purely because that was the first
// .cpp that happened to define Color::fromHex. A reader looking for "how do we
// parse a colour" was sent to a file-utility header, and the one function that
// could throw out of a config load was shipped from there too.
//
// The contract this file establishes, and the reason it was rewritten at all:
//
//   * fromChars, not stoi. std::stoi allocates a std::string per channel and
//     throws std::invalid_argument on a non-hex digit. ConfigLoader::load
//     catches only toml::parse_error, so `accent_color = "blue"` -- or any
//     6/8-character run of non-hex -- escaped the loader as an exception and
//     terminated the process on a hand-edited config file. std::from_chars
//     allocates nothing and cannot throw, so the escape path is gone rather
//     than merely narrowed.
//
//   * Failure is a value, not a colour. The old function had no failure value:
//     a length other than 6 or 8 fell through the if/else chain and returned
//     whatever Color's default member initialisers happened to be, which is
//     {255, 255, 255, 255} -- OPAQUE WHITE, not the opaque black the P2 item
//     described. A colour the user never typed, with no way to tell it apart
//     from one they did, is the exact failure mode this replaces.
//
// The type itself (struct Color) stays in Types.hpp, and that is a deliberate
// blast-radius choice rather than an oversight: Color is a four-byte aggregate
// embedded by value in ConfigData.hpp's UIConfig/KaraokeConfig/OverlayElement
// and consumed by ConfigParsers, ThemeBridge and three test suites, all of
// which get vc::Color from an existing Types.hpp include. Moving the type
// would mean a circular include (Color.hpp needs u8 from Types.hpp) or editing
// every includer for no behavioural gain. The logic moves; the four bytes and
// the constexpr constructors do not.
//
// MIGRATION -- Color::fromHex is the legacy entry point and is kept so the four
// production call sites compile unchanged. It is now a lossy shim over
// parseHexColor() and callers should move to the latter, because the shim
// substitutes a colour the user did not ask for. Call sites, all of which
// already have a validated or defaulted string in hand:
//   src/core/ConfigData.hpp:137        Color::fromHex("#00FF88")  -- a constant
//   src/core/ConfigParsers.cpp:194     the VC_PARSE_COLOR macro
//   src/core/ConfigParsers.cpp:316     overlay element colour
//   src/qml_bridge/ThemeBridge.cpp:20  setAccent   (QColor::name(), always 6 or 8)
//   src/qml_bridge/ThemeBridge.cpp:36  setBackground (ditto)

#include "Types.hpp"
#include <cstddef>
#include <expected>
#include <string>
#include <string_view>

namespace vc {

/// Why a hex colour string was refused. Distinct codes so a caller can report
/// the actual defect instead of "invalid"; `MissingHash` in particular is a
/// far more likely user mistake than `BadLength` and deserves its own message.
enum class ColorParseError : u8 {
    Empty,       ///< "" or "#" — nothing to parse
    MissingHash, ///< digits are present but the leading '#' is not
    BadLength,   ///< not exactly 6 or 8 digits after the '#'
    NotHex,      ///< a character outside [0-9A-Fa-f]
};

/// The failure payload. Two machine-readable fields and no std::string: the
/// parser allocates nothing, and a diagnostic that allocates is a diagnostic
/// that can throw.
struct ColorParseFailure {
    ColorParseError code{ColorParseError::Empty};

    /// Byte offset into the ORIGINAL string_view. 0 for Empty/MissingHash/
    /// BadLength, which are whole-string verdicts; for NotHex it is the offset
    /// of the first offending digit, counting the leading '#' as offset 0 —
    /// i.e. the index you would hand to std::string_view::substr to show the
    /// user where the parse stopped.
    std::size_t offset{0};

    [[nodiscard]] constexpr const char* message() const noexcept {
        switch (code) {
        case ColorParseError::Empty:       return "colour string is empty";
        case ColorParseError::MissingHash: return "colour string has no leading '#'";
        case ColorParseError::BadLength:   return "colour needs exactly 6 or 8 hex digits after '#'";
        case ColorParseError::NotHex:      return "colour contains a non-hex digit";
        }
        return "colour could not be parsed";
    }
};

/// Parse "#RRGGBB" (alpha forced to 255) or "#RRGGBBAA".
///
/// Accepts exactly those two shapes. A 3-digit form is refused rather than
/// expanded: "#f0a" and "#00ff00" are both "a colour", and silently accepting
/// one shape while refusing the other is how a config ends up with a colour
/// nobody chose. The leading '#' is required — every producer in this tree
/// emits one (QColor::name(), Color::toHex(), config/default.toml), so
/// tolerance for its absence would only mask a typo.
///
/// No allocation, no throw, no partial result: either a Color or a reason.
[[nodiscard]] std::expected<Color, ColorParseFailure> parseHexColor(std::string_view hex) noexcept;

/// Uppercase "#RRGGBBAA", always 8 digits — the exact inverse of the 8-digit
/// accept case, so parseHexColor(toHexString(c)) == c round-trips.
[[nodiscard]] std::string toHexString(const Color& color);

} // namespace vc
