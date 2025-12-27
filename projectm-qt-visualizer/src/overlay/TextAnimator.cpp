#include "TextAnimator.hpp"
#include <cmath>
#include <random>

namespace vc {

namespace {
    std::mt19937 rng{std::random_device{}()};
    std::uniform_real_distribution<f32> shakeDist{-1.0f, 1.0f};
}

TextAnimator::TextAnimator() = default;

void TextAnimator::update(f32 deltaTime) {
    totalTime_ += deltaTime * globalSpeed_;
    
    for (auto& [id, state] : states_) {
        state.time += deltaTime * globalSpeed_;
        
        // Decay beat accumulator
        state.beatAccum *= 0.9f;
    }
    
    lastBeatIntensity_ *= 0.95f;
}

void TextAnimator::onBeat(f32 intensity) {
    lastBeatIntensity_ = intensity;
    
    for (auto& [id, state] : states_) {
        state.beatAccum = std::min(state.beatAccum + intensity, 2.0f);
    }
}

AnimationState& TextAnimator::stateFor(const std::string& elementId) {
    return states_[elementId];
}

const AnimationState& TextAnimator::stateFor(const std::string& elementId) const {
    static AnimationState empty;
    auto it = states_.find(elementId);
    return it != states_.end() ? it->second : empty;
}

AnimationState TextAnimator::computeAnimatedState(const TextElement& element,
                                                    u32 canvasWidth, u32 canvasHeight) {
    AnimationState& state = stateFor(element.id());
    const auto& anim = element.animation();
    const auto& style = element.style();
    
    // Reset computed values
    state.opacity = style.opacity;
    state.offset = {0.0f, 0.0f};
    state.scale = 1.0f;
    state.color = style.color;
    state.visibleText = element.text();
    
    // Apply animation based on type
    switch (anim.type) {
        case AnimationType::None:
            break;
            
        case AnimationType::FadePulse:
            applyFadePulse(state, anim);
            break;
            
        case AnimationType::Scroll:
            applyScroll(state, anim, element.text(), canvasWidth);
            break;
            
        case AnimationType::Bounce:
            applyBounce(state, anim);
            break;
            
        case AnimationType::TypeWriter:
            applyTypeWriter(state, anim, element.text());
            break;
            
        case AnimationType::Wave:
            applyWave(state, anim);
            break;
            
        case AnimationType::Shake:
            applyShake(state, anim);
            break;
            
        case AnimationType::Scale:
            applyScale(state, anim);
            break;
            
        case AnimationType::Rainbow:
            applyRainbow(state, anim, style.color);
            break;
    }
    
    // Apply beat reactivity if enabled
    if (anim.beatReactive && state.beatAccum > 0.1f) {
        state.scale *= 1.0f + state.beatAccum * 0.1f;
        state.opacity = std::min(1.0f, state.opacity + state.beatAccum * 0.2f);
    }
    
    return state;
}

void TextAnimator::resetState(const std::string& elementId) {
    states_.erase(elementId);
}

void TextAnimator::resetAll() {
    states_.clear();
    totalTime_ = 0.0f;
}

void TextAnimator::applyFadePulse(AnimationState& state, const AnimationParams& params) {
    // Sinusoidal fade between 0.3 and 1.0
    f32 t = state.time * params.speed + params.phase;
    f32 fade = 0.65f + 0.35f * std::sin(t * 2.0f);
    state.opacity *= fade;
}

void TextAnimator::applyScroll(AnimationState& state, const AnimationParams& params,
                                const QString& text, u32 canvasWidth) {
    // Scroll horizontally across screen
    f32 speed = params.speed * 50.0f;  // Pixels per second
    f32 textWidth = text.length() * 15.0f;  // Rough estimate
    f32 totalWidth = canvasWidth + textWidth;
    
    f32 x = std::fmod(state.time * speed, totalWidth);
    if (!state.direction) {
        x = totalWidth - x;
    }
    
    state.offset.x = x - textWidth;
}

void TextAnimator::applyBounce(AnimationState& state, const AnimationParams& params) {
    // Bounce up and down
    f32 t = state.time * params.speed * 3.0f + params.phase;
    f32 bounce = std::abs(std::sin(t)) * params.amplitude * 20.0f;
    state.offset.y = -bounce;
}

void TextAnimator::applyTypeWriter(AnimationState& state, const AnimationParams& params,
                                    const QString& text) {
    // Reveal characters one at a time
    f32 charsPerSecond = params.speed * 10.0f;
    i32 visibleChars = static_cast<i32>(state.time * charsPerSecond);
    
    if (visibleChars >= text.length()) {
        // Reset after showing full text for a bit
        if (state.time > text.length() / charsPerSecond + 2.0f) {
            state.time = 0.0f;
        }
        state.visibleText = text;
    } else {
        state.visibleText = text.left(std::max(0, visibleChars));
    }
}

void TextAnimator::applyWave(AnimationState& state, const AnimationParams& params) {
    // Wavy vertical offset (per-character would need shader)
    f32 t = state.time * params.speed * 4.0f + params.phase;
    f32 wave = std::sin(t) * params.amplitude * 10.0f;
    state.offset.y = wave;
}

void TextAnimator::applyShake(AnimationState& state, const AnimationParams& params) {
    // Random shake, more intense with beat
    f32 intensity = params.amplitude * (1.0f + state.beatAccum * 2.0f);
    state.offset.x = shakeDist(rng) * intensity * 5.0f;
    state.offset.y = shakeDist(rng) * intensity * 5.0f;
}

void TextAnimator::applyScale(AnimationState& state, const AnimationParams& params) {
    // Pulsing scale
    f32 t = state.time * params.speed * 2.0f + params.phase;
    f32 pulse = 1.0f + std::sin(t) * params.amplitude * 0.2f;
    state.scale = pulse;
}

void TextAnimator::applyRainbow(AnimationState& state, const AnimationParams& params,
                                 const QColor& baseColor) {
    // Cycle through hues
    f32 hue = std::fmod(state.time * params.speed * 60.0f + params.phase * 360.0f, 360.0f);
    f32 sat = baseColor.saturationF();
    f32 val = baseColor.valueF();
    
    // Keep some of original color character
    sat = std::max(sat, 0.7f);
    
    state.color.setHsvF(hue / 360.0f, sat, val, state.color.alphaF());
}

} // namespace vc