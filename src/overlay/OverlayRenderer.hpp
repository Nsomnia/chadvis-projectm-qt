#pragma once
// OverlayRenderer.hpp - Handles raw OpenGL rendering for the overlay
// Separated to keep GL header insanity in check

// clang-format off
#include "util/GLIncludes.hpp" // Must be first
#include <QOpenGLShaderProgram>
#include <QOpenGLBuffer>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLTexture>
// clang-format on

#include <memory>

namespace vc {

class OverlayRenderer {
public:
    OverlayRenderer();
    ~OverlayRenderer();

    // Initialize GL resources (must be called with context current)
    void init();

    // Upload CPU image to GPU texture
    void upload(const QImage& image);

    // Draw the texture to the full screen quad
    void draw();

    // Check if initialized
    bool isInitialized() const {
        return initialized_;
    }

private:
    std::unique_ptr<QOpenGLShaderProgram> program_;
    QOpenGLBuffer vbo_{QOpenGLBuffer::VertexBuffer};
    QOpenGLVertexArrayObject vao_;
    std::unique_ptr<QOpenGLTexture> texture_;
    bool initialized_{false};
};

} // namespace vc
