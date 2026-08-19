#pragma once

#include "renderer.h"
#include "swframemapper.h"

// QuickSinkRenderer is the scene-mode frontend renderer (issue #507 M1/M2).
// It does not render anything itself. It hands frame ownership to
// QuickSinkBridge, where the Qt Quick VideoItem picks frames up:
//
// - VAAPI hwframes are submitted UNTOUCHED (M2 zero copy) unless the
//   bridge reports preferSoftware(). VideoItem imports them through the
//   backend renderer's exported-images interface (canExportEGL(),
//   initializeEGL(), exportEGLImages() in renderer.h), reached via
//   activeBackendRenderer(). On any import failure VideoItem flips
//   preferSoftware() and this renderer falls back to readback.
// - All other hwaccel frames are read back to software frames on the
//   decoder thread via SwFrameMapper (the M1 copy path).
// - Software frames pass through unchanged.
//
// It reports isRenderThreadSupported() false so Pacer runs in main-thread
// mode, and isDirectFrameDelivery() true so Pacer calls renderFrame()
// directly on the submitting thread instead of pushing SDL_CODE_FRAME_READY
// into an SDL event loop that is not running in scene mode. In that direct
// delivery mode, renderFrame() takes ownership of the frame.
class QuickSinkRenderer : public IFFmpegRenderer
{
public:
    // The backend renderer pointer mirrors how EGLRenderer receives its
    // backend in FFmpegVideoDecoder::createFrontendRenderer. It is null
    // when this instance is itself created as a backend renderer by the
    // scene-gated unknown-decoder factory in ffmpeg.cpp (software decode).
    explicit QuickSinkRenderer(IFFmpegRenderer* backendRenderer = nullptr);
    ~QuickSinkRenderer() override;

    bool initialize(PDECODER_PARAMETERS params) override;
    bool prepareDecoderContext(AVCodecContext* context, AVDictionary** options) override;
    void renderFrame(AVFrame* frame) override;
    bool isRenderThreadSupported() override;
    bool isDirectFrameDelivery() override;
    bool isPixelFormatSupported(int videoFormat, AVPixelFormat pixelFormat) override;

    // Backend renderer of the active scene-mode frontend, or null when no
    // scene decoder is alive. VideoItem uses it to drive the
    // exported-images contract for zero-copy VAAPI import. Scene decoders
    // are created and destroyed on the main thread and VideoItem also
    // renders on the main thread (QSG_RENDER_LOOP=basic is forced), so the
    // pointer must be re-fetched on every render pass and never cached
    // across frames or decoder resets.
    static IFFmpegRenderer* activeBackendRenderer();

    // Monotonic counter incremented each time a scene-mode frontend
    // registers. Pairing it with the pointer lets VideoItem distinguish a
    // new backend allocated at a recycled address from the one it already
    // ran EGL setup on. Main-thread only, like activeBackendRenderer().
    static uint64_t activeBackendGeneration();

private:
    IFFmpegRenderer* m_BackendRenderer;
    SwFrameMapper m_SwFrameMapper;
};
