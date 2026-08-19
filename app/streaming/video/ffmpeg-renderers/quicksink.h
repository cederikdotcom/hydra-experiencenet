#pragma once

#include "renderer.h"
#include "swframemapper.h"

// QuickSinkRenderer is the scene-mode frontend renderer (issue #507 M1).
// It does not render anything itself. It normalizes each frame to a
// software frame on the decoder thread (readback for hwaccel frames via
// SwFrameMapper) and hands ownership to QuickSinkBridge, where the Qt Quick
// VideoItem picks it up.
//
// It reports isRenderThreadSupported() false so Pacer runs in main-thread
// mode, and isDirectFrameDelivery() true so Pacer calls renderFrame()
// directly on the submitting thread instead of pushing SDL_CODE_FRAME_READY
// into an SDL event loop that is not running in scene mode. In that direct
// delivery mode, renderFrame() takes ownership of the frame.
class QuickSinkRenderer : public IFFmpegRenderer
{
public:
    QuickSinkRenderer();

    bool initialize(PDECODER_PARAMETERS params) override;
    bool prepareDecoderContext(AVCodecContext* context, AVDictionary** options) override;
    void renderFrame(AVFrame* frame) override;
    bool isRenderThreadSupported() override;
    bool isDirectFrameDelivery() override;
    bool isPixelFormatSupported(int videoFormat, AVPixelFormat pixelFormat) override;

private:
    SwFrameMapper m_SwFrameMapper;
};
