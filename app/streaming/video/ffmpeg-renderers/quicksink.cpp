#include "quicksink.h"

#include "streaming/video/quicksinkbridge.h"

QuickSinkRenderer::QuickSinkRenderer()
    : IFFmpegRenderer(RendererType::Unknown),
      m_SwFrameMapper(this)
{
}

bool QuickSinkRenderer::initialize(PDECODER_PARAMETERS params)
{
    // No window or graphics resources are needed here. The Qt Quick scene
    // graph owns all rendering. We only remember the video format so
    // SwFrameMapper can pick a readback format for hwaccel frames.
    m_SwFrameMapper.setVideoFormat(params->videoFormat);
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
    // M1 scope: 8-bit 4:2:0 only. VideoItem's copy path uploads NV12 or
    // planar YUV 4:2:0 textures. VAAPI hwframes are read back to one of the
    // software formats below in renderFrame().
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
    // We own the frame (direct delivery mode). Normalize hwaccel frames to
    // software frames here on the decoder thread, never on the GUI thread.
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
