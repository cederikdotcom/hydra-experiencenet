#include "videoitem.h"

#include "streaming/video/quicksinkbridge.h"
#include "streaming/video/ffmpeg-renderers/renderer.h"

#include <QSGRenderNode>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QOpenGLExtraFunctions>
#include <QMatrix4x4>

// Sized single and dual channel texture formats. Core in desktop GL 3.0+
// and GLES 3.0+, but not guaranteed to be in the headers we compile against.
#ifndef GL_R8
#define GL_R8 0x8229
#endif
#ifndef GL_RG8
#define GL_RG8 0x822B
#endif
#ifndef GL_RED
#define GL_RED 0x1903
#endif
#ifndef GL_RG
#define GL_RG 0x8227
#endif
#ifndef GL_UNPACK_ROW_LENGTH
#define GL_UNPACK_ROW_LENGTH 0x0CF2
#endif

namespace {

// Minimal IFFmpegRenderer implementation used only to reuse the shared CSC
// constant math from renderer.h (getFramePremultipliedCscConstants) without
// duplicating it here. It is never registered or used as a real renderer.
// Its defaults match QuickSinkRenderer: Rec 601, limited range.
class CscConstantHelper : public IFFmpegRenderer
{
public:
    CscConstantHelper() : IFFmpegRenderer(RendererType::Unknown) {}
    bool initialize(PDECODER_PARAMETERS) override { return false; }
    bool prepareDecoderContext(AVCodecContext*, AVDictionary**) override { return false; }
    void renderFrame(AVFrame*) override {}
};

const char k_VertexShader[] =
    "attribute vec2 aPosition;\n"
    "attribute vec2 aTexCoord;\n"
    "uniform mat4 uMvp;\n"
    "varying vec2 vTexCoord;\n"
    "void main() {\n"
    "    vTexCoord = aTexCoord;\n"
    "    gl_Position = uMvp * vec4(aPosition, 0.0, 1.0);\n"
    "}\n";

// NV12: plane 0 is Y (R8), plane 1 is interleaved CbCr (RG8).
// Adapted from shaders/egl_nv12.frag with the samplerExternalOES samplers
// replaced by plain 2D textures. The chroma cositing offset is intentionally
// omitted in the M1 copy path (half-texel chroma shift at most).
const char k_FragmentShaderNv12[] =
    "varying vec2 vTexCoord;\n"
    "uniform sampler2D uPlane0;\n"
    "uniform sampler2D uPlane1;\n"
    "uniform mat3 uCscMatrix;\n"
    "uniform vec3 uYuvOffsets;\n"
    "uniform float uAlpha;\n"
    "void main() {\n"
    "    vec3 yuv = vec3(texture2D(uPlane0, vTexCoord).r,\n"
    "                    texture2D(uPlane1, vTexCoord).rg);\n"
    "    yuv -= uYuvOffsets;\n"
    "    vec3 rgb = clamp(uCscMatrix * yuv, 0.0, 1.0);\n"
    "    gl_FragColor = vec4(rgb, 1.0) * uAlpha;\n"
    "}\n";

// YUV420P/YUVJ420P: three separate R8 planes
const char k_FragmentShaderTriPlanar[] =
    "varying vec2 vTexCoord;\n"
    "uniform sampler2D uPlane0;\n"
    "uniform sampler2D uPlane1;\n"
    "uniform sampler2D uPlane2;\n"
    "uniform mat3 uCscMatrix;\n"
    "uniform vec3 uYuvOffsets;\n"
    "uniform float uAlpha;\n"
    "void main() {\n"
    "    vec3 yuv = vec3(texture2D(uPlane0, vTexCoord).r,\n"
    "                    texture2D(uPlane1, vTexCoord).r,\n"
    "                    texture2D(uPlane2, vTexCoord).r);\n"
    "    yuv -= uYuvOffsets;\n"
    "    vec3 rgb = clamp(uCscMatrix * yuv, 0.0, 1.0);\n"
    "    gl_FragColor = vec4(rgb, 1.0) * uAlpha;\n"
    "}\n";

GLuint compileShader(QOpenGLFunctions* f, GLenum shaderType, const char* source)
{
    GLuint shader = f->glCreateShader(shaderType);
    if (shader == 0) {
        return 0;
    }

    f->glShaderSource(shader, 1, &source, nullptr);
    f->glCompileShader(shader);

    GLint status = GL_FALSE;
    f->glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (status != GL_TRUE) {
        char log[512] = {};
        f->glGetShaderInfoLog(shader, sizeof(log) - 1, nullptr, log);
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "VideoItem shader compilation failed: %s",
                     log);
        f->glDeleteShader(shader);
        return 0;
    }

    return shader;
}

// QSGRenderNode drawing the current video frame with raw OpenGL. All
// methods run on the scene graph render thread with the GL context current.
// With QSG_RENDER_LOOP=basic (forced in main.cpp) that is the GUI thread.
class VideoRenderNode : public QSGRenderNode
{
public:
    VideoRenderNode() = default;

    ~VideoRenderNode() override
    {
        releaseGlResources();
        av_frame_free(&m_PendingFrame);
        av_frame_free(&m_CurrentFrame);
    }

    void setItemSize(const QSizeF& itemSize)
    {
        m_ItemSize = itemSize;
    }

    // Called from updatePaintNode() with the render thread synced.
    // Takes ownership of the frame.
    void submitFrame(AVFrame* frame)
    {
        av_frame_free(&m_PendingFrame);
        m_PendingFrame = frame;
    }

    StateFlags changedStates() const override
    {
        return BlendState | ScissorState | DepthState | CullState | StencilState;
    }

    RenderingFlags flags() const override
    {
        // All drawing stays inside rect(). The letterbox bars are not
        // painted, so the node is not opaque.
        return BoundedRectRendering;
    }

    QRectF rect() const override
    {
        return QRectF(0, 0, m_ItemSize.width(), m_ItemSize.height());
    }

    void releaseResources() override
    {
        releaseGlResources();
    }

    void render(const RenderState* state) override
    {
        QOpenGLContext* ctx = QOpenGLContext::currentContext();
        if (ctx == nullptr) {
            // The scene graph is not running on the OpenGL RHI backend.
            // Scene mode requires it (Linux kiosk only), so just bail.
            return;
        }

        QOpenGLFunctions* f = ctx->functions();

        // Latch the newest frame delivered by the VideoItem. The previous
        // frame is freed only after the new upload has completed.
        if (m_PendingFrame != nullptr) {
            if (uploadFrame(ctx, f, m_PendingFrame)) {
                av_frame_free(&m_CurrentFrame);
                m_CurrentFrame = m_PendingFrame;
                m_PendingFrame = nullptr;
            }
            else {
                av_frame_free(&m_PendingFrame);
            }
        }

        if (m_Program == 0 || m_TextureCount == 0 || m_TexWidth == 0 || m_TexHeight == 0) {
            return;
        }

        const float itemW = float(m_ItemSize.width());
        const float itemH = float(m_ItemSize.height());
        if (itemW <= 0.0f || itemH <= 0.0f) {
            return;
        }

        // Set all GL state we depend on; everything we touch is declared
        // in changedStates()
        f->glDisable(GL_DEPTH_TEST);
        f->glDisable(GL_CULL_FACE);
        f->glDisable(GL_STENCIL_TEST);

        if (state->scissorEnabled()) {
            f->glEnable(GL_SCISSOR_TEST);
            const QRect r = state->scissorRect();
            f->glScissor(r.x(), r.y(), r.width(), r.height());
        }
        else {
            f->glDisable(GL_SCISSOR_TEST);
        }

        const float alpha = float(inheritedOpacity());
        if (alpha < 1.0f) {
            // The fragment shader outputs premultiplied alpha
            f->glEnable(GL_BLEND);
            f->glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        }
        else {
            f->glDisable(GL_BLEND);
        }

        QMatrix4x4 mvp = *state->projectionMatrix();
        if (matrix() != nullptr) {
            mvp *= *matrix();
        }

        // Letterbox: scale the quad to preserve the video aspect ratio
        // inside the item rectangle
        const float scale = qMin(itemW / float(m_TexWidth), itemH / float(m_TexHeight));
        const float dstW = float(m_TexWidth) * scale;
        const float dstH = float(m_TexHeight) * scale;
        const float x0 = (itemW - dstW) / 2.0f;
        const float y0 = (itemH - dstH) / 2.0f;
        const float x1 = x0 + dstW;
        const float y1 = y0 + dstH;

        // Interleaved x, y, u, v as a triangle strip: TL, TR, BL, BR
        const GLfloat vertices[] = {
            x0, y0, 0.0f, 0.0f,
            x1, y0, 1.0f, 0.0f,
            x0, y1, 0.0f, 1.0f,
            x1, y1, 1.0f, 1.0f,
        };

        // Use our own VAO where available (GL 3+ or GLES 3+) so we never
        // pollute the scene graph's vertex state
        QOpenGLExtraFunctions* ef = nullptr;
        if (!m_VaoDecided) {
            m_VaoDecided = true;
            m_UseVao = ctx->format().majorVersion() >= 3;
            if (m_UseVao) {
                ctx->extraFunctions()->glGenVertexArrays(1, &m_Vao);
            }
        }
        if (m_UseVao) {
            ef = ctx->extraFunctions();
            ef->glBindVertexArray(m_Vao);
        }

        if (m_Vbo == 0) {
            f->glGenBuffers(1, &m_Vbo);
        }
        f->glBindBuffer(GL_ARRAY_BUFFER, m_Vbo);
        f->glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_DYNAMIC_DRAW);

        f->glEnableVertexAttribArray(0);
        f->glEnableVertexAttribArray(1);
        f->glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), nullptr);
        f->glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat),
                                 reinterpret_cast<const void*>(2 * sizeof(GLfloat)));

        f->glUseProgram(m_Program);
        f->glUniformMatrix4fv(m_LocMvp, 1, GL_FALSE, mvp.constData());
        f->glUniformMatrix3fv(m_LocCsc, 1, GL_FALSE, m_CscMatrix.data());
        f->glUniform3fv(m_LocOffsets, 1, m_YuvOffsets.data());
        f->glUniform1f(m_LocAlpha, alpha);

        for (int i = 0; i < m_TextureCount; i++) {
            f->glActiveTexture(GL_TEXTURE0 + i);
            f->glBindTexture(GL_TEXTURE_2D, m_Textures[i]);
        }

        f->glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

        // Unbind everything we bound so no stale bindings leak into the
        // scene graph's own rendering
        for (int i = m_TextureCount - 1; i >= 0; i--) {
            f->glActiveTexture(GL_TEXTURE0 + i);
            f->glBindTexture(GL_TEXTURE_2D, 0);
        }
        f->glUseProgram(0);
        f->glBindBuffer(GL_ARRAY_BUFFER, 0);
        if (ef != nullptr) {
            ef->glBindVertexArray(0);
        }
        else {
            f->glDisableVertexAttribArray(0);
            f->glDisableVertexAttribArray(1);
        }
    }

private:
    bool createProgram(QOpenGLContext* ctx, QOpenGLFunctions* f, int planeCount)
    {
        if (m_Program != 0) {
            f->glDeleteProgram(m_Program);
            m_Program = 0;
        }

        QByteArray fragSource;
        if (ctx->isOpenGLES()) {
            // Required on GLES; invalid in desktop GLSL 1.10, so only
            // prepend it for GLES contexts
            fragSource += "precision mediump float;\n";
        }
        fragSource += (planeCount == 2) ? k_FragmentShaderNv12 : k_FragmentShaderTriPlanar;

        GLuint vertexShader = compileShader(f, GL_VERTEX_SHADER, k_VertexShader);
        if (vertexShader == 0) {
            return false;
        }

        GLuint fragmentShader = compileShader(f, GL_FRAGMENT_SHADER, fragSource.constData());
        if (fragmentShader == 0) {
            f->glDeleteShader(vertexShader);
            return false;
        }

        GLuint program = f->glCreateProgram();
        f->glAttachShader(program, vertexShader);
        f->glAttachShader(program, fragmentShader);
        f->glBindAttribLocation(program, 0, "aPosition");
        f->glBindAttribLocation(program, 1, "aTexCoord");
        f->glLinkProgram(program);
        f->glDeleteShader(vertexShader);
        f->glDeleteShader(fragmentShader);

        GLint status = GL_FALSE;
        f->glGetProgramiv(program, GL_LINK_STATUS, &status);
        if (status != GL_TRUE) {
            char log[512] = {};
            f->glGetProgramInfoLog(program, sizeof(log) - 1, nullptr, log);
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "VideoItem shader linking failed: %s",
                         log);
            f->glDeleteProgram(program);
            return false;
        }

        m_Program = program;
        m_ProgramPlaneCount = planeCount;
        m_LocMvp = f->glGetUniformLocation(program, "uMvp");
        m_LocCsc = f->glGetUniformLocation(program, "uCscMatrix");
        m_LocOffsets = f->glGetUniformLocation(program, "uYuvOffsets");
        m_LocAlpha = f->glGetUniformLocation(program, "uAlpha");

        // Sampler units never change, set them once
        f->glUseProgram(program);
        f->glUniform1i(f->glGetUniformLocation(program, "uPlane0"), 0);
        f->glUniform1i(f->glGetUniformLocation(program, "uPlane1"), 1);
        if (planeCount == 3) {
            f->glUniform1i(f->glGetUniformLocation(program, "uPlane2"), 2);
        }
        f->glUseProgram(0);

        return true;
    }

    bool uploadFrame(QOpenGLContext* ctx, QOpenGLFunctions* f, AVFrame* frame)
    {
        if (frame->format != AV_PIX_FMT_NV12 &&
            frame->format != AV_PIX_FMT_YUV420P &&
            frame->format != AV_PIX_FMT_YUVJ420P) {
            if (!m_LoggedUploadError) {
                m_LoggedUploadError = true;
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                             "VideoItem: unsupported frame format: %d",
                             frame->format);
            }
            return false;
        }

        if (ctx->isOpenGLES() && ctx->format().majorVersion() < 3) {
            // GLES 2 lacks sized R8/RG8 formats and GL_UNPACK_ROW_LENGTH.
            // Scene mode targets desktop GL or GLES 3+ only.
            if (!m_LoggedUploadError) {
                m_LoggedUploadError = true;
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                             "VideoItem: OpenGL ES 2 context is not supported");
            }
            return false;
        }

        const int planeCount = (frame->format == AV_PIX_FMT_NV12) ? 2 : 3;
        const int chromaWidth = (frame->width + 1) / 2;
        const int chromaHeight = (frame->height + 1) / 2;

        if (m_Program == 0 || m_ProgramPlaneCount != planeCount) {
            if (!createProgram(ctx, f, planeCount)) {
                return false;
            }
        }

        struct PlaneDesc {
            int width;
            int height;
            int bytesPerPixel;
            GLenum glFormat;
            GLint glInternalFormat;
        } planes[3];

        planes[0] = { frame->width, frame->height, 1, GL_RED, GL_R8 };
        if (planeCount == 2) {
            planes[1] = { chromaWidth, chromaHeight, 2, GL_RG, GL_RG8 };
        }
        else {
            planes[1] = { chromaWidth, chromaHeight, 1, GL_RED, GL_R8 };
            planes[2] = { chromaWidth, chromaHeight, 1, GL_RED, GL_R8 };
        }

        bool allocate = frame->width != m_TexWidth ||
                        frame->height != m_TexHeight ||
                        frame->format != m_TexFormat ||
                        m_TextureCount != planeCount;
        if (allocate && m_TextureCount > 0) {
            f->glDeleteTextures(m_TextureCount, m_Textures);
            m_TextureCount = 0;
        }
        if (m_TextureCount == 0) {
            f->glGenTextures(planeCount, m_Textures);
            m_TextureCount = planeCount;
            allocate = true;
        }

        // Frame linesizes are not required to be multiples of 4
        f->glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

        const bool canUseRowLength = !ctx->isOpenGLES() || ctx->format().majorVersion() >= 3;

        f->glActiveTexture(GL_TEXTURE0);
        for (int i = 0; i < planeCount; i++) {
            const PlaneDesc& plane = planes[i];
            const uint8_t* data = frame->data[i];
            const int linesize = frame->linesize[i];

            f->glBindTexture(GL_TEXTURE_2D, m_Textures[i]);

            if (allocate) {
                f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                f->glTexImage2D(GL_TEXTURE_2D, 0, plane.glInternalFormat,
                                plane.width, plane.height, 0,
                                plane.glFormat, GL_UNSIGNED_BYTE, nullptr);
            }

            if (canUseRowLength && linesize % plane.bytesPerPixel == 0) {
                // Upload the whole plane at once with the stride expressed
                // in pixels via GL_UNPACK_ROW_LENGTH
                f->glPixelStorei(GL_UNPACK_ROW_LENGTH, linesize / plane.bytesPerPixel);
                f->glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
                                   plane.width, plane.height,
                                   plane.glFormat, GL_UNSIGNED_BYTE, data);
                f->glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
            }
            else if (linesize == plane.width * plane.bytesPerPixel) {
                // Tightly packed plane
                f->glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
                                   plane.width, plane.height,
                                   plane.glFormat, GL_UNSIGNED_BYTE, data);
            }
            else {
                // Fallback: upload row by row to honor the stride
                for (int row = 0; row < plane.height; row++) {
                    f->glTexSubImage2D(GL_TEXTURE_2D, 0, 0, row,
                                       plane.width, 1,
                                       plane.glFormat, GL_UNSIGNED_BYTE,
                                       data + (size_t)row * linesize);
                }
            }
        }
        f->glBindTexture(GL_TEXTURE_2D, 0);
        f->glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

        m_TexWidth = frame->width;
        m_TexHeight = frame->height;
        m_TexFormat = frame->format;

        // Color conversion constants for this frame's colorspace and range
        m_CscHelper.getFramePremultipliedCscConstants(frame, m_CscMatrix, m_YuvOffsets);

        return true;
    }

    void releaseGlResources()
    {
        QOpenGLContext* ctx = QOpenGLContext::currentContext();
        if (ctx != nullptr) {
            QOpenGLFunctions* f = ctx->functions();
            if (m_TextureCount > 0) {
                f->glDeleteTextures(m_TextureCount, m_Textures);
            }
            if (m_Program != 0) {
                f->glDeleteProgram(m_Program);
            }
            if (m_Vbo != 0) {
                f->glDeleteBuffers(1, &m_Vbo);
            }
            if (m_Vao != 0) {
                ctx->extraFunctions()->glDeleteVertexArrays(1, &m_Vao);
            }
        }

        // If no context is current, the objects die with the context
        m_TextureCount = 0;
        m_Program = 0;
        m_ProgramPlaneCount = 0;
        m_Vbo = 0;
        m_Vao = 0;
        m_UseVao = false;
        m_VaoDecided = false;
        m_TexWidth = 0;
        m_TexHeight = 0;
        m_TexFormat = -1;
    }

    QSizeF m_ItemSize;

    AVFrame* m_PendingFrame = nullptr;
    AVFrame* m_CurrentFrame = nullptr;

    CscConstantHelper m_CscHelper;
    std::array<float, 9> m_CscMatrix = {};
    std::array<float, 3> m_YuvOffsets = {};

    GLuint m_Textures[3] = {};
    int m_TextureCount = 0;
    int m_TexWidth = 0;
    int m_TexHeight = 0;
    int m_TexFormat = -1;

    GLuint m_Program = 0;
    int m_ProgramPlaneCount = 0;
    GLint m_LocMvp = -1;
    GLint m_LocCsc = -1;
    GLint m_LocOffsets = -1;
    GLint m_LocAlpha = -1;

    GLuint m_Vbo = 0;
    GLuint m_Vao = 0;
    bool m_UseVao = false;
    bool m_VaoDecided = false;

    bool m_LoggedUploadError = false;
};

} // namespace

VideoItem::VideoItem(QQuickItem* parent)
    : QQuickItem(parent)
{
    setFlag(ItemHasContents, true);

    // Queued connection: frameReady() is emitted from the decoder thread
    connect(QuickSinkBridge::instance(), &QuickSinkBridge::frameReady,
            this, &VideoItem::onFrameReady, Qt::QueuedConnection);
}

void VideoItem::onFrameReady()
{
    if (!m_FirstFrameSeen) {
        m_FirstFrameSeen = true;
        // This slot runs on the GUI thread through a queued connection,
        // safely outside the scene graph sync phase, so QML handlers may
        // change item visibility in response.
        emit firstFrameReceived();
    }

    update();
}

QSGNode* VideoItem::updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData*)
{
    VideoRenderNode* node = static_cast<VideoRenderNode*>(oldNode);
    if (node == nullptr) {
        node = new VideoRenderNode();
    }

    node->setItemSize(size());

    // Latch the newest frame. Ownership moves from the bridge to the node.
    AVFrame* frame = QuickSinkBridge::instance()->takeFrame();
    if (frame != nullptr) {
        node->submitFrame(frame);
    }

    node->markDirty(QSGNode::DirtyMaterial);
    return node;
}
