#include "videoitem.h"

#include "streaming/video/quicksinkbridge.h"
#include "streaming/video/ffmpeg-renderers/renderer.h"
#include "streaming/video/ffmpeg-renderers/quicksink.h"

extern "C" {
#include <libavutil/hwcontext.h>
}

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

#ifdef HAVE_EGL
// glEGLImageTargetTexture2DOES() from GL_OES_EGL_image. We declare our own
// function pointer type instead of including SDL_opengles2.h, which does not
// mix safely with Qt's OpenGL headers in the same translation unit.
typedef void (EGLAPIENTRYP PfnGlEGLImageTargetTexture2DOES)(GLenum target, EGLImage image);

// DRM format constants for the per-plane import of composed NV12 dmabufs
// (issue #507 M0 amendment). Defined locally so we don't take a dependency
// on libdrm headers, following eglimagefactory.cpp.
#ifndef DRM_FORMAT_MOD_INVALID
#define DRM_FORMAT_MOD_INVALID ((1ULL << 56) - 1)
#endif
#ifndef DRM_FORMAT_MOD_LINEAR
#define DRM_FORMAT_MOD_LINEAR 0
#endif
#ifndef fourcc_code
#define fourcc_code(a, b, c, d) ((uint32_t)(a) | ((uint32_t)(b) << 8) | ((uint32_t)(c) << 16) | ((uint32_t)(d) << 24))
#endif
#ifndef DRM_FORMAT_R8
#define DRM_FORMAT_R8 fourcc_code('R', '8', ' ', ' ')
#endif
#ifndef DRM_FORMAT_GR88
#define DRM_FORMAT_GR88 fourcc_code('G', 'R', '8', '8')
#endif
#ifndef DRM_FORMAT_NV12
#define DRM_FORMAT_NV12 fourcc_code('N', 'V', '1', '2')
#endif
#endif

namespace {

#ifdef HAVE_EGL
// Bounded client-wait on the frame retirement fence. Long enough for any
// realistic frame on this hardware, short enough that a wedged GPU cannot
// hang the GUI thread. On timeout we proceed; the worst case is a transient
// artifact on a frame that was already multiple vsyncs late.
const EGLTime k_FenceTimeoutNs = 100000000ull; // 100 ms
#endif

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
        // frame is freed only after the new frame's texture content is in
        // place (upload completed, or imported images bound after the
        // retirement fence for the previous frame signaled).
        if (m_PendingFrame != nullptr) {
            AVFrame* frame = m_PendingFrame;
            m_PendingFrame = nullptr;

#ifdef HAVE_EGL
            if (frame->format == AV_PIX_FMT_VAAPI) {
                latchVaapiFrame(ctx, f, frame);
            }
            else
#endif
            {
                latchUploadedFrame(ctx, f, frame);
            }
        }

        // Select the texture set for the active path. The import path and
        // the M1 upload path keep separate texture objects so a mid-stream
        // fallback never draws with half-switched texture state.
        const GLuint* textures = m_Textures;
        int textureCount = m_TextureCount;
#ifdef HAVE_EGL
        if (m_ImportActive) {
            textures = m_ImportTextures;
            textureCount = m_ImportTextureCount;
        }
#endif

        if (m_Program == 0 || textureCount == 0 || m_TexWidth == 0 || m_TexHeight == 0) {
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

        for (int i = 0; i < textureCount; i++) {
            f->glActiveTexture(GL_TEXTURE0 + i);
            f->glBindTexture(GL_TEXTURE_2D, textures[i]);
        }

        f->glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

#ifdef HAVE_EGL
        // Fence the draw so the imported frame's dmabuf is not released
        // back to the decoder while the GPU may still be reading it. The
        // fence is consumed (client-waited) before the frame retires.
        // Mirrors eglvid.cpp's m_LastRenderSync pattern.
        if (m_ImportActive && m_eglClientWaitSync != nullptr) {
            if (m_LastRenderSync != EGL_NO_SYNC) {
                // Superseded: the new fence covers strictly more commands
                m_eglDestroySync(m_EGLDisplay, m_LastRenderSync);
            }
            if (m_eglCreateSync != nullptr) {
                m_LastRenderSync = m_eglCreateSync(m_EGLDisplay, EGL_SYNC_FENCE, nullptr);
            }
            else {
                m_LastRenderSync = m_eglCreateSyncKHR(m_EGLDisplay, EGL_SYNC_FENCE, nullptr);
            }
        }
#endif

        // Unbind everything we bound so no stale bindings leak into the
        // scene graph's own rendering
        for (int i = textureCount - 1; i >= 0; i--) {
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
    // Uploads the frame (software copy path, exactly as M1) and makes it
    // current on success. Takes ownership of the frame in all cases.
    void latchUploadedFrame(QOpenGLContext* ctx, QOpenGLFunctions* f, AVFrame* frame)
    {
        if (uploadFrame(ctx, f, frame)) {
            retireCurrentFrame(f);
            m_CurrentFrame = frame;
#ifdef HAVE_EGL
            m_ImportActive = false;
#endif
        }
        else {
            av_frame_free(&frame);
        }
    }

    // Frees the current frame. If it was rendered from imported EGLImages,
    // first waits until the GPU is done with them, because av_frame_free()
    // destroys the images (EglImageContext chained on frame->opaque_ref)
    // and releases the dmabuf surface back to the decoder.
    void retireCurrentFrame(QOpenGLFunctions* f)
    {
#ifdef HAVE_EGL
        if (m_ImportActive) {
            waitAndDestroyRenderFence(f);
        }
        destroyOwnedImages();
#else
        Q_UNUSED(f);
#endif
        av_frame_free(&m_CurrentFrame);
    }

#ifdef HAVE_EGL
    // Client-waits (bounded) on the fence created after the last draw that
    // sampled imported images, then destroys it. Without fence support the
    // degraded path is a full glFinish(), same as eglvid.cpp.
    void waitAndDestroyRenderFence(QOpenGLFunctions* f)
    {
        if (m_eglClientWaitSync != nullptr) {
            if (m_LastRenderSync != EGL_NO_SYNC) {
                m_eglClientWaitSync(m_EGLDisplay, m_LastRenderSync,
                                    EGL_SYNC_FLUSH_COMMANDS_BIT, k_FenceTimeoutNs);
                m_eglDestroySync(m_EGLDisplay, m_LastRenderSync);
                m_LastRenderSync = EGL_NO_SYNC;
            }
        }
        else if (f != nullptr) {
            f->glFinish();
        }
    }

    // Handles an AV_PIX_FMT_VAAPI frame per the issue #507 M2 contract:
    // zero-copy import when possible; on ANY import failure, log once, flip
    // the bridge to software readback for the rest of the session, and
    // transfer THIS frame as a one-off so the stream does not glitch.
    // Takes ownership of the frame in all cases.
    void latchVaapiFrame(QOpenGLContext* ctx, QOpenGLFunctions* f, AVFrame* frame)
    {
        // A hardware frame arriving while the bridge preference is back at
        // false means a new session started (QuickSinkBridge::enable()
        // resets it), so give zero copy a fresh chance.
        if (m_ImportDisabled && !QuickSinkBridge::instance()->preferSoftware()) {
            m_ImportDisabled = false;
            m_FallbackLogged = false;
            m_ZeroCopyLogged = false;
        }

        if (!m_ImportDisabled && tryImportFrame(ctx, f, frame)) {
            return;
        }

        engageSoftwareFallback();

        // One-off readback on the GUI thread. Frames already in flight on
        // the hardware path keep taking this until the decoder side sees
        // the preference; after that all frames arrive as software frames
        // and render exactly as M1.
        AVFrame* swFrame = av_frame_alloc();
        if (swFrame == nullptr) {
            av_frame_free(&frame);
            return;
        }

        int err = av_hwframe_transfer_data(swFrame, frame, 0);
        if (err < 0) {
            if (!m_LoggedUploadError) {
                m_LoggedUploadError = true;
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                             "VideoItem: av_hwframe_transfer_data() failed: %d",
                             err);
            }
            av_frame_free(&swFrame);
            av_frame_free(&frame);
            return;
        }

        // av_hwframe_transfer_data() doesn't transfer metadata, and the
        // CSC constants depend on it
        av_frame_copy_props(swFrame, frame);
        av_frame_free(&frame);

        latchUploadedFrame(ctx, f, swFrame);
    }

    void engageSoftwareFallback()
    {
        if (!m_FallbackLogged) {
            m_FallbackLogged = true;
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "VideoItem: zero-copy import failed (%s), falling back to software readback",
                        m_ImportFailReason != nullptr ? m_ImportFailReason : "unknown reason");
        }
        m_ImportDisabled = true;
        QuickSinkBridge::instance()->setPreferSoftware(true);
    }

    // One-time EGL setup per backend renderer instance, mirroring the
    // relevant parts of EGLRenderer::initialize(). Re-runs when the backend
    // changes (mid-stream decoder reset or a new session). The backend
    // pointer is re-fetched from QuickSinkRenderer on every pass per its
    // contract; m_EglBackend only tracks which instance we initialized.
    bool ensureEglSetup(QOpenGLContext* ctx)
    {
        IFFmpegRenderer* backend = QuickSinkRenderer::activeBackendRenderer();
        if (backend == nullptr) {
            m_ImportFailReason = "no backend renderer registered";
            return false;
        }

        // The generation check catches a decoder reset that allocated the
        // new backend at the old backend's address (ABA), which the pointer
        // comparison alone would miss.
        const uint64_t generation = QuickSinkRenderer::activeBackendGeneration();
        if (backend == m_EglBackend && generation == m_EglBackendGeneration) {
            return true;
        }

        m_EglBackend = nullptr;

        m_EGLDisplay = eglGetCurrentDisplay();
        if (m_EGLDisplay == EGL_NO_DISPLAY) {
            m_ImportFailReason = "no current EGL display";
            return false;
        }

        if (!ctx->hasExtension(QByteArrayLiteral("GL_OES_EGL_image"))) {
            m_ImportFailReason = "GL_OES_EGL_image unsupported";
            return false;
        }

        m_glEGLImageTargetTexture2DOES =
            (PfnGlEGLImageTargetTexture2DOES)eglGetProcAddress("glEGLImageTargetTexture2DOES");
        if (m_glEGLImageTargetTexture2DOES == nullptr) {
            m_ImportFailReason = "glEGLImageTargetTexture2DOES not found";
            return false;
        }

        const EGLExtensions eglExtensions(m_EGLDisplay);
        if (!eglExtensions.isSupported("EGL_KHR_image_base") &&
            !eglExtensions.isSupported("EGL_KHR_image")) {
            m_ImportFailReason = "EGL_KHR_image unsupported";
            return false;
        }

        if (!backend->canExportEGL()) {
            m_ImportFailReason = "backend cannot export EGLImages";
            return false;
        }

        if (!backend->initializeEGL(m_EGLDisplay, eglExtensions)) {
            m_ImportFailReason = "backend EGL initialization failed";
            return false;
        }

        // Determine the export shape the backend settled on. Separate-layer
        // export yields per-plane images directly from exportEGLImages().
        // Composed export (AV_PIX_FMT_DRM_PRIME, the only shape i965 offers
        // per the issue #507 M0 amendment) yields one opaque image instead,
        // so for that shape we take the raw composed dmabuf through
        // mapDrmPrimeFrame() and import the R8 (Y) and GR88 (CbCr) planes
        // ourselves using the descriptor's per-plane offset and pitch.
        m_EglExportFormat = backend->getEGLImagePixelFormat();
        if (m_EglExportFormat == AV_PIX_FMT_DRM_PRIME) {
#ifdef HAVE_DRM
            if (!backend->canExportDrmPrime()) {
                m_ImportFailReason = "composed export without DRM PRIME support";
                return false;
            }

            if (!eglExtensions.isSupported("EGL_EXT_image_dma_buf_import")) {
                m_ImportFailReason = "EGL_EXT_image_dma_buf_import unsupported";
                return false;
            }
            m_DmaBufModifiersSupported =
                eglExtensions.isSupported("EGL_EXT_image_dma_buf_import_modifiers");

            // NB: eglCreateImage() and eglCreateImageKHR() have slightly
            // different definitions, mirroring eglimagefactory.cpp
            m_eglCreateImage = (PFNEGLCREATEIMAGEPROC)eglGetProcAddress("eglCreateImage");
            m_eglCreateImageKHR = (PFNEGLCREATEIMAGEKHRPROC)eglGetProcAddress("eglCreateImageKHR");
            m_eglDestroyImage = (PFNEGLDESTROYIMAGEPROC)eglGetProcAddress("eglDestroyImage");
            m_eglDestroyImageKHR = (PFNEGLDESTROYIMAGEKHRPROC)eglGetProcAddress("eglDestroyImageKHR");
            if (!(m_eglCreateImage && m_eglDestroyImage) &&
                !(m_eglCreateImageKHR && m_eglDestroyImageKHR)) {
                m_ImportFailReason = "missing eglCreateImage()/eglDestroyImage()";
                return false;
            }
#else
            m_ImportFailReason = "composed export requires DRM support";
            return false;
#endif
        }
        else if (m_EglExportFormat != AV_PIX_FMT_NV12 &&
                 m_EglExportFormat != AV_PIX_FMT_YUV420P &&
                 m_EglExportFormat != AV_PIX_FMT_YUVJ420P) {
            // Separate-layer shapes our 8-bit shaders cannot sample
            // (P010 and friends are filtered out by isPixelFormatSupported,
            // but stay defensive here)
            m_ImportFailReason = "unsupported separate-layer export format";
            return false;
        }

        // Fence sync is optional; without it we glFinish() before retiring
        // frames, the same degradation eglvid.cpp accepts
        if (eglExtensions.isSupported("EGL_KHR_fence_sync")) {
            // eglCreateSyncKHR() has a slightly different prototype to eglCreateSync()
            m_eglCreateSyncKHR = (PFNEGLCREATESYNCKHRPROC)eglGetProcAddress("eglCreateSyncKHR");
            m_eglDestroySync = (PFNEGLDESTROYSYNCPROC)eglGetProcAddress("eglDestroySyncKHR");
            m_eglClientWaitSync = (PFNEGLCLIENTWAITSYNCPROC)eglGetProcAddress("eglClientWaitSyncKHR");
        }
        else {
            // EGL 1.5 introduced sync support to the core specification
            m_eglCreateSync = (PFNEGLCREATESYNCPROC)eglGetProcAddress("eglCreateSync");
            m_eglDestroySync = (PFNEGLDESTROYSYNCPROC)eglGetProcAddress("eglDestroySync");
            m_eglClientWaitSync = (PFNEGLCLIENTWAITSYNCPROC)eglGetProcAddress("eglClientWaitSync");
        }
        if ((m_eglCreateSync == nullptr && m_eglCreateSyncKHR == nullptr) ||
            m_eglDestroySync == nullptr || m_eglClientWaitSync == nullptr) {
            m_eglCreateSync = nullptr;
            m_eglCreateSyncKHR = nullptr;
            m_eglDestroySync = nullptr;
            m_eglClientWaitSync = nullptr;
        }

        m_EglBackend = backend;
        m_EglBackendGeneration = generation;
        return true;
    }

#ifdef HAVE_DRM
    // Imports the two planes of a composed NV12 dmabuf as R8 and GR88
    // EGLImages (issue #507 M0 amendment). The dmabuf fds are closed with
    // the frame (mapDrmPrimeFrame chains a cleanup buffer on
    // frame->opaque_ref); the EGLImages become ours to destroy, which the
    // caller does through m_OwnedImages once the GPU is done sampling.
    // Returns the plane count, or -1 with m_ImportFailReason set.
    ssize_t importComposedPlanes(AVFrame* frame, EGLImage images[EGL_MAX_PLANES])
    {
        AVDRMFrameDescriptor drmDescriptor;
        if (!m_EglBackend->mapDrmPrimeFrame(frame, &drmDescriptor)) {
            m_ImportFailReason = "mapDrmPrimeFrame() failed";
            return -1;
        }

        if (drmDescriptor.nb_layers != 1 ||
            drmDescriptor.layers[0].format != DRM_FORMAT_NV12 ||
            drmDescriptor.layers[0].nb_planes != 2) {
            m_ImportFailReason = "composed descriptor is not two-plane NV12";
            return -1;
        }

        const uint32_t planeFourccs[2] = { DRM_FORMAT_R8, DRM_FORMAT_GR88 };

        for (int i = 0; i < 2; i++) {
            const AVDRMPlaneDescriptor& plane = drmDescriptor.layers[0].planes[i];
            const AVDRMObjectDescriptor& object = drmDescriptor.objects[plane.object_index];

            const bool haveModifier = object.format_modifier != DRM_FORMAT_MOD_INVALID;
            if (haveModifier &&
                object.format_modifier != DRM_FORMAT_MOD_LINEAR &&
                !m_DmaBufModifiersSupported) {
                m_ImportFailReason = "tiled dmabuf without modifier import support";
                if (i == 1) {
                    destroyEglImage(images[0]);
                }
                return -1;
            }

            // NV12: full-resolution Y plane, half-resolution CbCr plane
            const EGLAttrib planeWidth = (i == 0) ? frame->width : (frame->width + 1) / 2;
            const EGLAttrib planeHeight = (i == 0) ? frame->height : (frame->height + 1) / 2;

            const int MAX_ATTRIB_COUNT = 19;
            EGLAttrib attribs[MAX_ATTRIB_COUNT] = {
                EGL_LINUX_DRM_FOURCC_EXT, (EGLAttrib)planeFourccs[i],
                EGL_WIDTH, planeWidth,
                EGL_HEIGHT, planeHeight,
                EGL_IMAGE_PRESERVED_KHR, EGL_TRUE,
                EGL_DMA_BUF_PLANE0_FD_EXT, object.fd,
                EGL_DMA_BUF_PLANE0_OFFSET_EXT, (EGLAttrib)plane.offset,
                EGL_DMA_BUF_PLANE0_PITCH_EXT, (EGLAttrib)plane.pitch,
            };
            int attribIndex = 14;
            if (m_DmaBufModifiersSupported && haveModifier) {
                attribs[attribIndex++] = EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT;
                attribs[attribIndex++] = (EGLAttrib)(EGLint)(object.format_modifier & 0xFFFFFFFF);
                attribs[attribIndex++] = EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT;
                attribs[attribIndex++] = (EGLAttrib)(EGLint)(object.format_modifier >> 32);
            }
            attribs[attribIndex++] = EGL_NONE;

            if (m_eglCreateImage != nullptr) {
                images[i] = m_eglCreateImage(m_EGLDisplay, EGL_NO_CONTEXT,
                                             EGL_LINUX_DMA_BUF_EXT,
                                             nullptr, attribs);
            }
            else {
                // Cast the EGLAttrib array elements to EGLint for the KHR extension
                EGLint intAttribs[MAX_ATTRIB_COUNT];
                for (int j = 0; j < MAX_ATTRIB_COUNT; j++) {
                    intAttribs[j] = (EGLint)attribs[j];
                }
                images[i] = m_eglCreateImageKHR(m_EGLDisplay, EGL_NO_CONTEXT,
                                                EGL_LINUX_DMA_BUF_EXT,
                                                nullptr, intAttribs);
            }

            if (images[i] == nullptr) {
                m_ImportFailReason = "eglCreateImage() failed for a dmabuf plane";
                if (i == 1) {
                    destroyEglImage(images[0]);
                }
                return -1;
            }
        }

        return 2;
    }
#endif

    void destroyEglImage(EGLImage image)
    {
        if (m_eglDestroyImage != nullptr) {
            m_eglDestroyImage(m_EGLDisplay, image);
        }
        else if (m_eglDestroyImageKHR != nullptr) {
            m_eglDestroyImageKHR(m_EGLDisplay, image);
        }
    }

    // Destroys the EGLImages we created ourselves for the current imported
    // frame (composed path only; exported images from the backend are
    // destroyed with the frame via its opaque_ref chain). Call only after
    // the GPU is known to be done sampling them.
    void destroyOwnedImages()
    {
        for (ssize_t i = 0; i < m_OwnedImageCount; i++) {
            destroyEglImage(m_OwnedImages[i]);
        }
        m_OwnedImageCount = 0;
    }

    // Zero-copy import of one VAAPI frame: obtain one EGLImage per plane
    // and bind each one to a plain 2D texture for the existing two-texture
    // NV12 shader (or the tri-planar shader). Separate-layer backends hand
    // us per-plane images from exportEGLImages(); composed-layer backends
    // (i965) hand us the raw composed dmabuf via mapDrmPrimeFrame() and we
    // build the R8/GR88 plane images ourselves. Returns false WITHOUT
    // freeing the frame so the caller can run the fallback on it. On
    // success the frame becomes the current frame and the previous frame
    // retires.
    bool tryImportFrame(QOpenGLContext* ctx, QOpenGLFunctions* f, AVFrame* frame)
    {
        if (!ensureEglSetup(ctx)) {
            return false;
        }

        EGLImage images[EGL_MAX_PLANES];
        ssize_t planeCount;
        // True when the images are ours to destroy (composed path). On the
        // separate-layer path they are chained on frame->opaque_ref
        // (EglImageContext, see eglimagefactory.cpp) and destroyed with
        // the frame instead.
        bool ownImages = false;

#ifdef HAVE_DRM
        if (m_EglExportFormat == AV_PIX_FMT_DRM_PRIME) {
            planeCount = importComposedPlanes(frame, images);
            if (planeCount <= 0) {
                // importComposedPlanes() set m_ImportFailReason
                return false;
            }
            ownImages = true;
        }
        else
#endif
        {
            planeCount = m_EglBackend->exportEGLImages(frame, m_EGLDisplay, images);
            if (planeCount <= 0) {
                m_ImportFailReason = "backend failed to export EGLImages";
                return false;
            }

            // Each plane image must be usable as an ordinary 2D texture (R8
            // and GR88 for NV12). A single composed image would need an
            // external sampler, which this render path does not use; the
            // composed shape is handled by the branch above instead.
            if (planeCount != 2 && planeCount != 3) {
                m_ImportFailReason = "unsupported exported plane layout";
                return false;
            }
        }

        if (m_Program == 0 || m_ProgramPlaneCount != (int)planeCount) {
            if (!createProgram(ctx, f, (int)planeCount)) {
                m_ImportFailReason = "shader program creation failed";
                if (ownImages) {
                    for (ssize_t i = 0; i < planeCount; i++) {
                        destroyEglImage(images[i]);
                    }
                }
                return false;
            }
        }

        // Wait for the GPU to finish with the previous imported frame
        // before rebinding its textures and freeing it
        if (m_ImportActive) {
            waitAndDestroyRenderFence(f);
        }

        // The previous frame's owned plane images (composed path) can be
        // destroyed now that the fence has signaled
        destroyOwnedImages();

        if (m_ImportTextureCount != (int)planeCount) {
            if (m_ImportTextureCount > 0) {
                f->glDeleteTextures(m_ImportTextureCount, m_ImportTextures);
            }
            f->glGenTextures((GLsizei)planeCount, m_ImportTextures);
            m_ImportTextureCount = (int)planeCount;

            f->glActiveTexture(GL_TEXTURE0);
            for (int i = 0; i < m_ImportTextureCount; i++) {
                f->glBindTexture(GL_TEXTURE_2D, m_ImportTextures[i]);
                f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            }
        }

        // Until the first import succeeds, verify with glGetError() that
        // this driver accepts the images on GL_TEXTURE_2D. Steady state
        // repeats the identical operation, so we stop checking after that
        // (no per-frame overhead, no per-frame logging).
        const bool checkErrors = !m_ZeroCopyLogged;
        if (checkErrors) {
            // Clear stale errors so the post-bind check is attributable
            while (f->glGetError() != GL_NO_ERROR) {}
        }

        f->glActiveTexture(GL_TEXTURE0);
        for (int i = 0; i < (int)planeCount; i++) {
            f->glBindTexture(GL_TEXTURE_2D, m_ImportTextures[i]);
            m_glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, images[i]);
        }
        f->glBindTexture(GL_TEXTURE_2D, 0);

        if (checkErrors && f->glGetError() != GL_NO_ERROR) {
            // The textures now reference rejected images; drop them so the
            // draw guard skips rendering until the fallback frame lands
            f->glDeleteTextures(m_ImportTextureCount, m_ImportTextures);
            m_ImportTextureCount = 0;
            if (ownImages) {
                for (ssize_t i = 0; i < planeCount; i++) {
                    destroyEglImage(images[i]);
                }
            }
            m_ImportFailReason = "GL rejected the imported images on GL_TEXTURE_2D";
            return false;
        }

        // On the composed path the plane images are ours; hold them until
        // the GPU is done sampling this frame (fence-guarded, destroyed by
        // the destroyOwnedImages() calls at retire points)
        if (ownImages) {
            for (ssize_t i = 0; i < planeCount; i++) {
                m_OwnedImages[i] = images[i];
            }
            m_OwnedImageCount = planeCount;
        }

        // Retire the previous frame (fence already awaited above) and make
        // this frame current. Lifetime: at most current plus previous, as M1.
        av_frame_free(&m_CurrentFrame);
        m_CurrentFrame = frame;
        m_ImportActive = true;
        m_TexWidth = frame->width;
        m_TexHeight = frame->height;
        m_TexFormat = frame->format;

        // Color conversion constants for this frame's colorspace and range
        m_CscHelper.getFramePremultipliedCscConstants(frame, m_CscMatrix, m_YuvOffsets);

        if (!m_ZeroCopyLogged) {
            m_ZeroCopyLogged = true;
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                        "Zero-copy dmabuf import active (%d plane textures)",
                        (int)planeCount);
        }

        return true;
    }
#endif

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
#ifdef HAVE_EGL
            if (m_ImportActive) {
                // The GPU may still be sampling the current frame's
                // imported images; they are destroyed when the destructor
                // frees the frame right after this (separate-layer path)
                // or by destroyOwnedImages() below (composed path)
                waitAndDestroyRenderFence(f);
            }
            if (m_ImportTextureCount > 0) {
                f->glDeleteTextures(m_ImportTextureCount, m_ImportTextures);
            }
#endif
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

#ifdef HAVE_EGL
        // Destroying EGL objects needs only the display, not a current
        // GL context
        destroyOwnedImages();

        // Drop any leftover fence handle (no context current case)
        if (m_LastRenderSync != EGL_NO_SYNC) {
            if (m_eglDestroySync != nullptr) {
                m_eglDestroySync(m_EGLDisplay, m_LastRenderSync);
            }
            m_LastRenderSync = EGL_NO_SYNC;
        }
        m_ImportTextureCount = 0;
        m_ImportActive = false;
        m_EglBackend = nullptr;
        m_EglBackendGeneration = 0;
        m_EglExportFormat = AV_PIX_FMT_NONE;
        m_EGLDisplay = EGL_NO_DISPLAY;
        m_glEGLImageTargetTexture2DOES = nullptr;
        m_eglCreateSync = nullptr;
        m_eglCreateSyncKHR = nullptr;
        m_eglDestroySync = nullptr;
        m_eglClientWaitSync = nullptr;
        m_eglCreateImage = nullptr;
        m_eglCreateImageKHR = nullptr;
        m_eglDestroyImage = nullptr;
        m_eglDestroyImageKHR = nullptr;
        m_DmaBufModifiersSupported = false;
#endif

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

#ifdef HAVE_EGL
    // Zero-copy import state (issue #507 M2)
    IFFmpegRenderer* m_EglBackend = nullptr;
    uint64_t m_EglBackendGeneration = 0;
    AVPixelFormat m_EglExportFormat = AV_PIX_FMT_NONE;
    EGLDisplay m_EGLDisplay = EGL_NO_DISPLAY;
    PfnGlEGLImageTargetTexture2DOES m_glEGLImageTargetTexture2DOES = nullptr;
    PFNEGLCREATESYNCPROC m_eglCreateSync = nullptr;
    PFNEGLCREATESYNCKHRPROC m_eglCreateSyncKHR = nullptr;
    PFNEGLDESTROYSYNCPROC m_eglDestroySync = nullptr;
    PFNEGLCLIENTWAITSYNCPROC m_eglClientWaitSync = nullptr;
    PFNEGLCREATEIMAGEPROC m_eglCreateImage = nullptr;
    PFNEGLCREATEIMAGEKHRPROC m_eglCreateImageKHR = nullptr;
    PFNEGLDESTROYIMAGEPROC m_eglDestroyImage = nullptr;
    PFNEGLDESTROYIMAGEKHRPROC m_eglDestroyImageKHR = nullptr;
    bool m_DmaBufModifiersSupported = false;
    EGLSync m_LastRenderSync = EGL_NO_SYNC;

    // EGLImages created by importComposedPlanes() for the CURRENT frame.
    // Empty on the separate-layer path (those images ride the frame's
    // opaque_ref chain instead).
    EGLImage m_OwnedImages[EGL_MAX_PLANES] = {};
    ssize_t m_OwnedImageCount = 0;

    GLuint m_ImportTextures[3] = {};
    int m_ImportTextureCount = 0;
    bool m_ImportActive = false;
    bool m_ImportDisabled = false;
    bool m_ZeroCopyLogged = false;
    bool m_FallbackLogged = false;
    const char* m_ImportFailReason = nullptr;
#endif
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
