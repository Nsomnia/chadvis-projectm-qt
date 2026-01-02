#include "OverlayRenderer.hpp"
#include "core/Logger.hpp"

namespace vc {

OverlayRenderer::OverlayRenderer() = default;
OverlayRenderer::~OverlayRenderer() = default;

void OverlayRenderer::init() {
    if (initialized_)
        return;

    // GLEW is already initialized by VisualizerWindow
    // Just ensure we can create our resources

    // Compile Shaders
    program_ = std::make_unique<QOpenGLShaderProgram>();

    const char* vertSource = R"(
        #version 330 core
        layout (location = 0) in vec2 position;
        layout (location = 1) in vec2 texCoord;
        out vec2 TexCoord;
        void main() {
            gl_Position = vec4(position, 0.0, 1.0);
            TexCoord = texCoord;
        }
    )";

    const char* fragSource = R"(
        #version 330 core
        in vec2 TexCoord;
        out vec4 color;
        uniform sampler2D overlayTexture;
        void main() {
            vec4 texColor = texture(overlayTexture, TexCoord);
            if (texColor.a == 0.0) discard;
            color = texColor;
        }
    )";

    if (!program_->addShaderFromSourceCode(QOpenGLShader::Vertex, vertSource)) {
        LOG_ERROR("OverlayRenderer: Vertex shader failed: {}",
                  program_->log().toStdString());
    }
    if (!program_->addShaderFromSourceCode(QOpenGLShader::Fragment,
                                           fragSource)) {
        LOG_ERROR("OverlayRenderer: Fragment shader failed: {}",
                  program_->log().toStdString());
    }
    if (!program_->link()) {
        LOG_ERROR("OverlayRenderer: Link failed: {}",
                  program_->log().toStdString());
    }

    // Setup Quad
    // x, y, u, v
    float vertices[] = {
            -1.0f, 1.0f,  0.0f, 1.0f, // Top Left
            -1.0f, -1.0f, 0.0f, 0.0f, // Bottom Left
            1.0f,  -1.0f, 1.0f, 0.0f, // Bottom Right
            -1.0f, 1.0f,  0.0f, 1.0f, // Top Left (tri 2)
            1.0f,  -1.0f, 1.0f, 0.0f, // Bottom Right
            1.0f,  1.0f,  1.0f, 1.0f // Top Right
    };

    if (!vao_.create())
        LOG_ERROR("OverlayRenderer: Failed to create VAO");
    vao_.bind();

    if (!vbo_.create())
        LOG_ERROR("OverlayRenderer: Failed to create VBO");
    vbo_.bind();
    vbo_.allocate(vertices, sizeof(vertices));

    program_->enableAttributeArray(0);
    program_->setAttributeBuffer(0, GL_FLOAT, 0, 2, 4 * sizeof(float));

    program_->enableAttributeArray(1);
    program_->setAttributeBuffer(
            1, GL_FLOAT, 2 * sizeof(float), 2, 4 * sizeof(float));

    vbo_.release();
    vao_.release();

    // Setup Texture
    texture_ = std::make_unique<QOpenGLTexture>(QOpenGLTexture::Target2D);
    texture_->setMinificationFilter(QOpenGLTexture::Linear);
    texture_->setMagnificationFilter(QOpenGLTexture::Linear);
    texture_->setWrapMode(QOpenGLTexture::ClampToEdge);

    initialized_ = true;
    LOG_INFO("OverlayRenderer: Initialized GL resources");
}

void OverlayRenderer::upload(const QImage& image) {
    if (!initialized_)
        init();

    // Recreate/Upload texture
    // For performance, we could check size match, but let's trust
    // QOpenGLTexture for now
    if (texture_->isCreated()) {
        texture_->destroy();
    }
    texture_->create();
    texture_->setData(image);
}

void OverlayRenderer::draw() {
    if (!initialized_ || !program_)
        return;

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    program_->bind();

    glActiveTexture(GL_TEXTURE0);
    if (texture_ && texture_->isCreated()) {
        texture_->bind();
    }
    program_->setUniformValue("overlayTexture", 0);

    vao_.bind();
    glDrawArrays(GL_TRIANGLES, 0, 6);
    vao_.release();

    if (texture_)
        texture_->release();
    program_->release();

    glDisable(GL_BLEND);
}

} // namespace vc
