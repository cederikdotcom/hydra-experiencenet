#include "quicksink.h"

#include "streaming/video/quicksinkbridge.h"

// Registry of the active scene-mode sink (issue #507 M2). VideoItem cannot
// hold a renderer pointer across decoder resets, so the sink publishes the
// backend renderer it was paired with and VideoItem re-fetches it on every
// render pass through activeBackendRenderer(). All writers and readers run
// on the main thread: scene-mode decoders are created and destroyed in
// Session::exec(), Session::handleSceneDecoderReset() and
// Session::stopSession(), and VideoItem renders on the main thread because
// QSG_RENDER_LOOP=basic is forced in main.cpp.
static QuickSinkRenderer* s_ActiveSink = nullptr;
static IFFmpegRenderer* s_ActiveBackendRenderer = nullptr;
static uint64_t s_BackendGeneration = 0;

QuickSinkRenderer::QuickSinkRenderer(IFFmpegRenderer* backendRenderer)
    : IFFmpegRenderer(RendererType::Unknown),
      m_BackendRenderer(backendRenderer),
      m_SwFrameMapper(this)
{
}

QuickSinkRenderer::~QuickSinkRenderer()
{
    if (s_ActiveSink == this) {
        s_ActiveSink = nullptr;
        s_ActiveBackendRenderer = nullptr;
    }
}

IFFmpegRenderer* QuickSinkRenderer::activeBackendRenderer()
{
    return s_ActiveBackendRenderer;
}

uint64_t QuickSinkRenderer::activeBackendGeneration()
{
    return s_BackendGeneration;
}

bool QuickSinkRenderer::initialize(PDECODER_PARAMETERS params)
{
    // No window or graphics resources are needed here. The Qt Quick scene
    // graph owns all rendering. We only remember the video format so
    // SwFrameMapper can pick a readback format for hwaccel frames.
    m_SwFrameMapper.setVideoFormat(params->videoFormat);

    // Publish the backend renderer for VideoItem. When a decoder pairs a
    // backend with this frontend, the frontend initializes last in
    // completeInitialization(), so the registration made here is the one
    // VideoItem observes.
    s_ActiveSink = this;
    s_ActiveBackendRenderer = m_BackendRenderer;
    // The generation lets VideoItem detect a decoder reset that reallocates
    // a new backend at the address of the old one (ABA), so it never skips
    // re-running EGL setup on a fresh backend instance.
    s_BackendGeneration++;
    return true;
}

bool QuickSinkRenderer::prepareDecoderContext(AVCodecContext*, AVDictionary**)
{
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "Using Qt Quick sink renderer (scene mode)");
    return true;
}

bool QuickSinkRenderer::isRenderThreadSupported()
{
    // Force Pacer into main-thread mode so no Pacer render thread is
    // created. Combined with isDirectFrameDelivery(), frames are delivered
    // to renderFrame() on the thread that submits them to Pacer.
    return false;
}

bool QuickSinkRenderer::isDirectFrameDelivery()
{
    return true;
}

bool QuickSinkRenderer::isPixelFormatSupported(int videoFormat, AVPixelFormat pixelFormat)
{
    // 8-bit 4:2:0 only. VideoItem's copy path uploads NV12 or planar
    // YUV 4:2:0 textures. VAAPI hwframes are either handed off untouched
    // for zero-copy import (M2) or read back to one of the software
    // formats below in renderFrame().
    if (videoFormat & (VIDEO_FORMAT_MASK_10BIT | VIDEO_FORMAT_MASK_YUV444)) {
        return false;
    }

    switch (pixelFormat) {
    case AV_PIX_FMT_NV12:
    case AV_PIX_FMT_YUV420P:
    case AV_PIX_FMT_YUVJ420P:
#ifdef HAVE_LIBVA
    case AV_PIX_FMT_VAAPI:
#endif
        return true;

    default:
        return false;
    }
}

void QuickSinkRenderer::renderFrame(AVFrame* frame)
{
    // We own the frame (direct delivery mode).

#if defined(HAVE_LIBVA) && defined(HAVE_EGL)
    // Zero-copy path (issue #507 M2): hand VAAPI hwframes to the bridge
    // untouched. VideoItem imports the frame's dmabuf planes through the
    // backend renderer's exported-images interface. If any import step
    // fails there, VideoItem flips preferSoftware() on the bridge and every
    // subsequent frame takes the readback path below instead. The HAVE_EGL
    // gate must match VideoItem's: a build without EGL support has no
    // import path, so its frames must be read back here.
    if (frame->format == AV_PIX_FMT_VAAPI &&
            !QuickSinkBridge::instance()->preferSoftware()) {
        // Ownership transfers to the bridge
        QuickSinkBridge::instance()->submitFrame(frame);
        return;
    }
#endif

    // Normalize remaining hwaccel frames to software frames here on the
    // decoder thread, never on the GUI thread.
    if (frame->hw_frames_ctx != nullptr) {
        AVFrame* swFrame = m_SwFrameMapper.getSwFrameFromHwFrame(frame);

        // Free the hardware frame in both outcomes. A mapped swFrame holds
        // its own references on the underlying surface, so this is safe.
        av_frame_free(&frame);

        if (swFrame == nullptr) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "QuickSinkRenderer: hwframe readback failed, dropping frame");
            return;
        }

        frame = swFrame;
    }

    // Ownership transfers to the bridge
    QuickSinkBridge::instance()->submitFrame(frame);
}
