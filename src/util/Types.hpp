#pragma once
// Types.hpp - Common type definitions
// Because typing std::chrono::milliseconds gets old fast

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace vc {

// Namespace alias for less carpal tunnel
namespace fs = std::filesystem;
namespace chr = std::chrono;

// Integer types (we're not animals)
using i8  = std::int8_t;
using i16 = std::int16_t;
using i32 = std::int32_t;
using i64 = std::int64_t;
using u8  = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;
using f32 = float;
using f64 = double;

// Size type for indices
using usize = std::size_t;
using isize = std::ptrdiff_t;

// Time types
using Duration = chr::milliseconds;
using TimePoint = chr::steady_clock::time_point;
using Timestamp = chr::system_clock::time_point;

// Audio types
using SampleRate = u32;
using ChannelCount = u8;
using SampleBuffer = std::vector<f32>;
using StereoSample = std::pair<f32, f32>;

// Normalized value [0.0, 1.0]
struct Normalized {
    f32 value{0.0f};
    
    constexpr Normalized() = default;
    constexpr explicit Normalized(f32 v) : value(std::clamp(v, 0.0f, 1.0f)) {}
    constexpr operator f32() const { return value; }
};

// 2D position (normalized or pixel)
struct Vec2 {
    f32 x{0.0f};
    f32 y{0.0f};
    
    constexpr Vec2 operator+(Vec2 other) const { return {x + other.x, y + other.y}; }
    constexpr Vec2 operator-(Vec2 other) const { return {x - other.x, y - other.y}; }
    constexpr Vec2 operator*(f32 s) const { return {x * s, y * s}; }
};

// Color with alpha
struct Color {
    u8 r{255}, g{255}, b{255}, a{255};
    
    static constexpr Color white() { return {255, 255, 255, 255}; }
    static constexpr Color black() { return {0, 0, 0, 255}; }
    static constexpr Color transparent() { return {0, 0, 0, 0}; }
    
    static Color fromHex(std::string_view hex);
    std::string toHex() const;
};

// Rectangle
struct Rect {
    f32 x{0}, y{0}, width{0}, height{0};
    
    constexpr bool contains(Vec2 p) const {
        return p.x >= x && p.x <= x + width && p.y >= y && p.y <= y + height;
    }
};

// Size
struct Size {
    u32 width{0};
    u32 height{0};
    
    constexpr u64 pixels() const { return static_cast<u64>(width) * height; }
    constexpr f32 aspect() const { return height > 0 ? static_cast<f32>(width) / height : 1.0f; }
};

// Forward declarations
class Config;
class AudioEngine;
class Playlist;
class VisualizerWindow;
class OverlayEngine;
class VideoRecorder;

} // namespace vc