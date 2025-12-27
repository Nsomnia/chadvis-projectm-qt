#pragma once
// TextAnimator.hpp - Text animation engine
// Making text move so you don't have to do the math

#include "util/Types.hpp"
#include "TextElement.hpp"
#include <chrono>
#include <unordered_map>

namespace vc {

struct AnimationState {
    f32 time{0.0f};
    f32 phase{0.0f};
    f32 beatAccum{0.0f};
    i32 charIndex{0};        // For typewriter
    bool direction{true};    // For scroll/bounce
    
    // Computed values
    f32 opacity{1.0f};
    Vec2 offset{0.0f, 0.0f};
    f32 scale{1.0f};
    QColor color;
    QString visibleText;
};

class TextAnimator {
public:
    TextAnimator();
    
    // Update animations
    void update(f32 deltaTime);
    void onBeat(f32 intensity);
    
    // Get state for element
    AnimationState& stateFor(const std::string& elementId);
    const AnimationState& stateFor(const std::string& elementId) const;
    
    // Apply animation to element, returns modified values
    AnimationState computeAnimatedState(const TextElement& element, 
                                         u32 canvasWidth, u32 canvasHeight);
    
    // Reset state for element
    void resetState(const std::string& elementId);
    void resetAll();
    
    // Global settings
    void setGlobalSpeed(f32 speed) { globalSpeed_ = speed; }
    f32 globalSpeed() const { return globalSpeed_; }
    
private:
    void applyFadePulse(AnimationState& state, const AnimationParams& params);
    void applyScroll(AnimationState& state, const AnimationParams& params, 
                     const QString& text, u32 canvasWidth);
    void applyBounce(AnimationState& state, const AnimationParams& params);
    void applyTypeWriter(AnimationState& state, const AnimationParams& params,
                         const QString& text);
    void applyWave(AnimationState& state, const AnimationParams& params);
    void applyShake(AnimationState& state, const AnimationParams& params);
    void applyScale(AnimationState& state, const AnimationParams& params);
    void applyRainbow(AnimationState& state, const AnimationParams& params,
                      const QColor& baseColor);
    
    std::unordered_map<std::string, AnimationState> states_;
    f32 globalSpeed_{1.0f};
    f32 totalTime_{0.0f};
    f32 lastBeatIntensity_{0.0f};
};

} // namespace vc