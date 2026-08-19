#include "quicksinkbridge.h"

#include "SDL_compat.h"

QuickSinkBridge* QuickSinkBridge::instance()
{
    // Constructed on first use. All cross-thread access is guarded by
    // m_Lock, and frameReady() deliveries are queued connections, so the
    // bridge's own thread affinity does not matter.
    static QuickSinkBridge s_Instance;
    return &s_Instance;
}

QuickSinkBridge::~QuickSinkBridge()
{
    av_frame_free(&m_PendingFrame);
}

void QuickSinkBridge::enable()
{
    QMutexLocker locker(&m_Lock);

    av_frame_free(&m_PendingFrame);
    m_SignalPending = false;
    m_Enabled = true;

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "QuickSinkBridge enabled");
}

void QuickSinkBridge::disable()
{
    QMutexLocker locker(&m_Lock);

    av_frame_free(&m_PendingFrame);
    m_SignalPending = false;
    m_Enabled = false;

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "QuickSinkBridge disabled");
}

bool QuickSinkBridge::isEnabled() const
{
    QMutexLocker locker(&m_Lock);
    return m_Enabled;
}

void QuickSinkBridge::submitFrame(AVFrame* frame)
{
    bool emitSignal = false;

    {
        QMutexLocker locker(&m_Lock);

        if (!m_Enabled) {
            av_frame_free(&frame);
            return;
        }

        // Keep only the newest frame. The scene graph latches the newest
        // frame at render time, so older undelivered frames are stale.
        av_frame_free(&m_PendingFrame);
        m_PendingFrame = frame;

        if (!m_SignalPending) {
            m_SignalPending = true;
            emitSignal = true;
        }
    }

    // Emit outside the lock. The receiver runs on the GUI thread via a
    // queued connection, so this is safe from the decoder thread.
    if (emitSignal) {
        emit frameReady();
    }
}

AVFrame* QuickSinkBridge::takeFrame()
{
    QMutexLocker locker(&m_Lock);

    m_SignalPending = false;

    AVFrame* frame = m_PendingFrame;
    m_PendingFrame = nullptr;
    return frame;
}
