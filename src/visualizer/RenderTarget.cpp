#include "RenderTarget.hpp"
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include "core/Logger.hpp"

namespace vc {

RenderTarget::RenderTarget() = default;

RenderTarget::~RenderTarget() {
    destroy();
}

RenderTarget::RenderTarget(RenderTarget&& other) noexcept
    : QOpenGLFunctions_3_3_Core(),
      fbo_(std::exchange(other.fbo_, 0)),
      texture_(std::exchange(other.texture_, 0)),
      depthBuffer_(std::exchange(other.depthBuffer_, 0)),
      width_(std::exchange(other.width_, 0)),
      height_(std::exchange(other.height_, 0)),
      hasDepth_(std::exchange(other.hasDepth_, false)) {
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

    auto* ctx = QOpenGLContext::currentContext();
    if (ctx == nullptr) {
        return Result<void>::err(
                "No OpenGL context current for RenderTarget::create()");
    }

    if (!this->initializeOpenGLFunctions()) {
        return Result<void>::err(
                "Failed to initialize OpenGL functions for RenderTarget");
    }

    destroy();

    width_ = width;
    height_ = height;
    hasDepth_ = withDepth;

    this->glGenFramebuffers(1, &fbo_);
    this->glBindFramebuffer(GL_FRAMEBUFFER, fbo_);

    this->glGenTextures(1, &texture_);
    this->glBindTexture(GL_TEXTURE_2D, texture_);
    this->glTexImage2D(GL_TEXTURE_2D,
                       0,
                       GL_RGBA8,
                       width,
                       height,
                       0,
                       GL_RGBA,
                       GL_UNSIGNED_BYTE,
                       nullptr);
    this->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    this->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    this->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    this->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    this->glFramebufferTexture2D(
            GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture_, 0);

    if (withDepth) {
        this->glGenRenderbuffers(1, &depthBuffer_);
        this->glBindRenderbuffer(GL_RENDERBUFFER, depthBuffer_);
        this->glRenderbufferStorage(
                GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
        this->glFramebufferRenderbuffer(GL_FRAMEBUFFER,
                                        GL_DEPTH_STENCIL_ATTACHMENT,
                                        GL_RENDERBUFFER,
                                        depthBuffer_);
    }

    this->glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    this->glClear(GL_COLOR_BUFFER_BIT);

    GLenum status = this->glCheckFramebufferStatus(GL_FRAMEBUFFER);
    this->glBindFramebuffer(GL_FRAMEBUFFER, 0);

    if (status != GL_FRAMEBUFFER_COMPLETE) {
        destroy();
        return Result<void>::err("Framebuffer incomplete: " +
                                 std::to_string(status));
    }

    LOG_DEBUG("Created render target {}x{}", width, height);
    return Result<void>::ok();
}

void RenderTarget::destroy() {
    auto* ctx = QOpenGLContext::currentContext();
    if (ctx == nullptr) {
        depthBuffer_ = 0;
        texture_ = 0;
        fbo_ = 0;
        width_ = height_ = 0;
        return;
    }

    if (!this->initializeOpenGLFunctions())
        return;

    if (depthBuffer_) {
        this->glDeleteRenderbuffers(1, &depthBuffer_);
        depthBuffer_ = 0;
    }
    if (texture_) {
        this->glDeleteTextures(1, &texture_);
        texture_ = 0;
    }
    if (fbo_) {
        this->glDeleteFramebuffers(1, &fbo_);
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
    if (!this->initializeOpenGLFunctions())
        return;
    this->glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    this->glViewport(0, 0, width_, height_);
}

void RenderTarget::unbind() {
    if (!this->initializeOpenGLFunctions())
        return;
    this->glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void RenderTarget::bindDefault() {
    auto* ctx = QOpenGLContext::currentContext();
    if (ctx && ctx->functions()) {
        ctx->functions()->glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
}

void RenderTarget::readPixels(void* data, GLenum format, GLenum type) {
    if (!this->initializeOpenGLFunctions())
        return;
    this->glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo_);
    this->glReadPixels(0, 0, width_, height_, format, type, data);
    this->glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
}

void RenderTarget::blitTo(RenderTarget& other, bool linear) {
    if (!this->initializeOpenGLFunctions())
        return;
    this->glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo_);
    this->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, other.fbo_);

    this->glBlitFramebuffer(0,
                            0,
                            width_,
                            height_,
                            0,
                            0,
                            other.width_,
                            other.height_,
                            GL_COLOR_BUFFER_BIT,
                            linear ? GL_LINEAR : GL_NEAREST);

    this->glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    this->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
}

void RenderTarget::blitToScreen(u32 screenWidth,
                                u32 screenHeight,
                                bool linear,
                                GLuint targetFbo) {
    if (!this->initializeOpenGLFunctions())
        return;
    this->glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo_);
    this->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, targetFbo);

    this->glBlitFramebuffer(0,
                            0,
                            width_,
                            height_,
                            0,
                            0,
                            screenWidth,
                            screenHeight,
                            GL_COLOR_BUFFER_BIT,
                            linear ? GL_LINEAR : GL_NEAREST);

    this->glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
}

} // namespace vc
