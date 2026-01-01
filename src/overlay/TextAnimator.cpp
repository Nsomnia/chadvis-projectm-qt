#include "TextAnimator.hpp"
#include <cmath>
#include <random>

namespace vc {

namespace {
std::mt19937 rng{std::random_device{}()};
std::uniform_real_distribution<f32> shakeDist{-1.0f, 1.0f};
std::uniform_real_distribution<f32> velDist{100.0f, 200.0f};
} // namespace

TextAnimator::TextAnimator() = default;

void TextAnimator::update(f32 deltaTime) {
    totalTime_ += deltaTime * globalSpeed_;
    for (auto& [id, state] : states_) {
        state.time += deltaTime * globalSpeed_;
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
const AnimationState& TextAnimator::stateFor(
        const std::string& elementId) const {
    static AnimationState empty;
    auto it = states_.find(elementId);
    return it != states_.end() ? it->second : empty;
}

AnimationState TextAnimator::computeAnimatedState(const TextElement& element,
                                                  u32 canvasWidth,
                                                  u32 canvasHeight) {
    AnimationState& state = stateFor(element.id());
    const auto& anim = element.animation();
    const auto& style = element.style();

    state.opacity = style.opacity;
    state.offset = {0.0f, 0.0f};
    state.scale = 1.0f;
    state.color = style.color;
    state.visibleText = element.text();

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
    case AnimationType::DVDBounce:
        applyDVDBounce(state, anim, canvasWidth, canvasHeight, element);
        break;
    }

    if (anim.beatReactive && state.beatAccum > 0.1f) {
        state.scale *= 1.0f + state.beatAccum * 0.1f;
        state.opacity = std::min(1.0f, state.opacity + state.beatAccum * 0.2f);
    }
    return state;
}

void TextAnimator::applyDVDBounce(AnimationState& state,
                                  const AnimationParams& params,
                                  u32 canvasWidth,
                                  u32 canvasHeight,
                                  const TextElement& element) {
    if (state.velocity.x == 0 && state.velocity.y == 0) {
        state.velocity = {velDist(rng), velDist(rng)};
    }

    f32 dt = 0.016f; // Assume 60fps base for velocity integration
    state.offset.x += state.velocity.x * dt * params.speed;
    state.offset.y += state.velocity.y * dt * params.speed;

    // Use rough bounds check (should ideally use actual text metrics)
    f32 margin = 50.0f;
    if (state.offset.x < -canvasWidth / 2.0f + margin ||
        state.offset.x > canvasWidth / 2.0f - margin) {
        state.velocity.x *= -1.0f;
        state.offset.x = std::clamp(state.offset.x,
                                    -canvasWidth / 2.0f + margin,
                                    canvasWidth / 2.0f - margin);
    }
    if (state.offset.y < -canvasHeight / 2.0f + margin ||
        state.offset.y > canvasHeight / 2.0f - margin) {
        state.velocity.y *= -1.0f;
        state.offset.y = std::clamp(state.offset.y,
                                    -canvasHeight / 2.0f + margin,
                                    canvasHeight / 2.0f - margin);
    }
}

void TextAnimator::resetState(const std::string& elementId) {
    states_.erase(elementId);
}
void TextAnimator::resetAll() {
    states_.clear();
    totalTime_ = 0.0f;
}

void TextAnimator::applyFadePulse(AnimationState& state,
                                  const AnimationParams& params) {
    f32 t = state.time * params.speed + params.phase;
    state.opacity *= 0.65f + 0.35f * std::sin(t * 2.0f);
}

void TextAnimator::applyScroll(AnimationState& state,
                               const AnimationParams& params,
                               const QString& text,
                               u32 canvasWidth) {
    f32 speed = params.speed * 100.0f;
    f32 textWidth = text.length() * 20.0f;
    f32 totalWidth = canvasWidth + textWidth;
    f32 x = std::fmod(state.time * speed, totalWidth);
    state.offset.x = x - textWidth;
}

void TextAnimator::applyBounce(AnimationState& state,
                               const AnimationParams& params) {
    f32 t = state.time * params.speed * 3.0f + params.phase;
    state.offset.y = -std::abs(std::sin(t)) * params.amplitude * 40.0f;
}

void TextAnimator::applyTypeWriter(AnimationState& state,
                                   const AnimationParams& params,
                                   const QString& text) {
    f32 charsPerSecond = params.speed * 15.0f;
    i32 visibleChars = static_cast<i32>(state.time * charsPerSecond);
    if (visibleChars >= text.length()) {
        if (state.time > text.length() / charsPerSecond + 3.0f)
            state.time = 0.0f;
        state.visibleText = text;
    } else
        state.visibleText = text.left(std::max(0, visibleChars));
}

void TextAnimator::applyWave(AnimationState& state,
                             const AnimationParams& params) {
    f32 t = state.time * params.speed * 4.0f + params.phase;
    state.offset.y = std::sin(t) * params.amplitude * 20.0f;
}

void TextAnimator::applyShake(AnimationState& state,
                              const AnimationParams& params) {
    f32 intensity = params.amplitude * (1.0f + state.beatAccum * 3.0f);
    state.offset.x = shakeDist(rng) * intensity * 10.0f;
    state.offset.y = shakeDist(rng) * intensity * 10.0f;
}

void TextAnimator::applyScale(AnimationState& state,
                              const AnimationParams& params) {
    f32 t = state.time * params.speed * 2.0f + params.phase;
    state.scale = 1.0f + std::sin(t) * params.amplitude * 0.3f;
}

void TextAnimator::applyRainbow(AnimationState& state,
                                const AnimationParams& params,
                                const QColor& baseColor) {
    f32 hue = std::fmod(
            state.time * params.speed * 100.0f + params.phase * 360.0f, 360.0f);
    state.color.setHsvF(hue / 360.0f,
                        std::max(0.7f, baseColor.saturationF()),
                        baseColor.valueF(),
                        state.color.alphaF());
}

} // namespace vc
