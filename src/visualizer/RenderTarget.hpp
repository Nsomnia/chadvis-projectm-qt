#pragma once
// RenderTarget.hpp - OpenGL framebuffer object management
//
// projectM v4 cannot draw into a framebuffer object: it binds framebuffer 0 for
// its closing texture copy, so an FBO is a *readback* destination here, never a
// render target. The only current user is VisualizerRenderer's capture scaling.

#include <QOpenGLFunctions_3_3_Core>
#include "util/Result.hpp"
#include "util/Types.hpp"

namespace vc {

class RenderTarget : protected QOpenGLFunctions_3_3_Core {
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

    // Access
    GLuint fbo() const {
        return fbo_;
    }
    GLuint texture() const {
        return texture_;
    }
    u32 width() const {
        return width_;
    }
    u32 height() const {
        return height_;
    }
    Size size() const {
        return {width_, height_};
    }
    bool isValid() const {
        return fbo_ != 0;
    }

    // Scale the default framebuffer's `sourceWidth` x `sourceHeight` region
    // into this target. Used to resize a framebuffer-0 readback, which is the
    // only way to change resolution once projectM has drawn.
    void blitFromDefault(u32 sourceWidth, u32 sourceHeight, bool linear = true);

private:
    GLuint fbo_{0};
    GLuint texture_{0};
    GLuint depthBuffer_{0};
    u32 width_{0};
    u32 height_{0};
    bool hasDepth_{false};
};

} // namespace vc