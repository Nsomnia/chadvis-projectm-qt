#include "RenderTarget.hpp"
#include "core/Logger.hpp"

namespace vc {

RenderTarget::RenderTarget() = default;

RenderTarget::~RenderTarget() {
    destroy();
}

RenderTarget::RenderTarget(RenderTarget&& other) noexcept
    : fbo_(std::exchange(other.fbo_, 0))
    , texture_(std::exchange(other.texture_, 0))
    , depthBuffer_(std::exchange(other.depthBuffer_, 0))
    , width_(std::exchange(other.width_, 0))
    , height_(std::exchange(other.height_, 0))
    , hasDepth_(std::exchange(other.hasDepth_, false))
{
}

RenderTarget& RenderTarget::operator=(RenderTarget&& other) noexcept {
    if (this != &other) {
        destroy();
        fbo_ = std::exchange(other.fbo_, 0);
        texture_ = std::exchange(other.texture_, 0);
        depthBuffer_ = std::exchange(other.depthBuffer_, 0);
        width_ = std::exchange(other.width_, 0);
        height_ = std::exchange(other.height_, 0);
        hasDepth_ = std::exchange(other.hasDepth_, false);
    }
    return *this;
}

Result<void> RenderTarget::create(u32 width, u32 height, bool withDepth) {
    if (width == 0 || height == 0) {
        return Result<void>::err("Invalid render target size");
    }
    
    destroy();
    
    width_ = width;
    height_ = height;
    hasDepth_ = withDepth;
    
    // Create framebuffer
    glGenFramebuffers(1, &fbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    
    // Create texture
    glGenTextures(1, &texture_);
    glBindTexture(GL_TEXTURE_2D, texture_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture_, 0);
    
    // Create depth buffer if requested
    if (withDepth) {
        glGenRenderbuffers(1, &depthBuffer_);
        glBindRenderbuffer(GL_RENDERBUFFER, depthBuffer_);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, depthBuffer_);
    }
    
    // Clear to transparent black immediately to prevent ghosting/artifacts
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    
    // Check completeness
    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        destroy();
        return Result<void>::err("Framebuffer incomplete: " + std::to_string(status));
    }
    
    LOG_DEBUG("Created render target {}x{}", width, height);
    return Result<void>::ok();
}

void RenderTarget::destroy() {
    if (depthBuffer_) {
        glDeleteRenderbuffers(1, &depthBuffer_);
        depthBuffer_ = 0;
    }
    if (texture_) {
        glDeleteTextures(1, &texture_);
        texture_ = 0;
    }
    if (fbo_) {
        glDeleteFramebuffers(1, &fbo_);
        fbo_ = 0;
    }
    width_ = height_ = 0;
}

Result<void> RenderTarget::resize(u32 width, u32 height) {
    if (width == width_ && height == height_) {
        return Result<void>::ok();
    }
    return create(width, height, hasDepth_);
}

void RenderTarget::bind() {
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glViewport(0, 0, width_, height_);
}

void RenderTarget::unbind() {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void RenderTarget::bindDefault() {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void RenderTarget::readPixels(void* data, GLenum format, GLenum type) {
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo_);
    glReadPixels(0, 0, width_, height_, format, type, data);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
}

void RenderTarget::blitTo(RenderTarget& other, bool linear) {
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo_);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, other.fbo_);
    
    glBlitFramebuffer(
        0, 0, width_, height_,
        0, 0, other.width_, other.height_,
        GL_COLOR_BUFFER_BIT,
        linear ? GL_LINEAR : GL_NEAREST
    );
    
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
}

void RenderTarget::blitToScreen(u32 screenWidth, u32 screenHeight, bool linear) {
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo_);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    
    glBlitFramebuffer(
        0, 0, width_, height_,
        0, 0, screenWidth, screenHeight,
        GL_COLOR_BUFFER_BIT,
        linear ? GL_LINEAR : GL_NEAREST
    );
    
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
}

} // namespace vc