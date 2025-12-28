#pragma once
// RenderTarget.hpp - OpenGL framebuffer management
// Because rendering to a texture shouldn't require a PhD

#include "util/Types.hpp"
#include "util/Result.hpp"
#include "util/GLIncludes.hpp"

namespace vc {

class RenderTarget {
public:
    RenderTarget();
    ~RenderTarget();
    
    // Non-copyable
    RenderTarget(const RenderTarget&) = delete;
    RenderTarget& operator=(const RenderTarget&) = delete;
    
    // Moveable
    RenderTarget(RenderTarget&& other) noexcept;
    RenderTarget& operator=(RenderTarget&& other) noexcept;
    
    // Initialize with size
    Result<void> create(u32 width, u32 height, bool withDepth = false);
    void destroy();
    
    // Resize (recreates buffers)
    Result<void> resize(u32 width, u32 height);
    
    // Binding
    void bind();
    void unbind();
    static void bindDefault();
    
    // Access
    GLuint fbo() const { return fbo_; }
    GLuint texture() const { return texture_; }
    u32 width() const { return width_; }
    u32 height() const { return height_; }
    Size size() const { return {width_, height_}; }
    bool isValid() const { return fbo_ != 0; }
    
    // Read pixels
    void readPixels(void* data, GLenum format = GL_RGBA, GLenum type = GL_UNSIGNED_BYTE);
    
    // Blit to another target
    void blitTo(RenderTarget& other, bool linear = true);
    void blitToScreen(u32 screenWidth, u32 screenHeight, bool linear = true);
    
private:
    GLuint fbo_{0};
    GLuint texture_{0};
    GLuint depthBuffer_{0};
    u32 width_{0};
    u32 height_{0};
    bool hasDepth_{false};
};

// RAII bind guard
class RenderTargetGuard {
public:
    explicit RenderTargetGuard(RenderTarget& target) : target_(target) {
        target_.bind();
    }
    ~RenderTargetGuard() {
        target_.unbind();
    }
    
private:
    RenderTarget& target_;
};

} // namespace vc