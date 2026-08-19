#pragma once

#include <QObject>
#include <QMutex>

extern "C" {
#include <libavutil/frame.h>
}

// QuickSinkBridge is the frame handoff point between the FFmpeg decode
// pipeline and the Qt Quick scene graph in scene mode (issue #507 M1).
//
// The decoder side (QuickSinkRenderer, running on the decoder thread) calls
// submitFrame() with a software AVFrame and transfers ownership. The bridge
// keeps only the newest frame and frees any superseded frame. The GUI side
// (VideoItem, running on the scene graph render thread, which is the GUI
// thread because QSG_RENDER_LOOP=basic is forced in main.cpp) consumes the
// newest frame with takeFrame() and then owns it.
//
// frameReady() is emitted at most once per undelivered frame so the Qt event
// queue never piles up stale update requests. VideoItem connects it to
// QQuickItem::update() with a queued connection.
//
// Session only ever calls enable()/disable() around a scene-mode stream.
// While disabled, submitted frames are freed immediately.
class QuickSinkBridge : public QObject
{
    Q_OBJECT

public:
    static QuickSinkBridge* instance();

    // Called by Session when entering scene mode. Clears any stale state
    // from a previous stream.
    void enable();

    // Called by Session on stream teardown. Frees any pending frame and
    // makes subsequent submitFrame() calls free their frames immediately.
    void disable();

    bool isEnabled() const;

    // Called from the decoder/pacer thread. Takes ownership of the frame.
    void submitFrame(AVFrame* frame);

    // Called by VideoItem from the scene graph. Transfers ownership of the
    // newest pending frame to the caller, or returns nullptr if none.
    AVFrame* takeFrame();

signals:
    // Emitted (queued across threads) when a new frame is pending. At most
    // one emission is outstanding until the next takeFrame() call.
    void frameReady();

private:
    QuickSinkBridge() = default;
    ~QuickSinkBridge() override;

    mutable QMutex m_Lock;
    AVFrame* m_PendingFrame = nullptr;
    bool m_Enabled = false;
    bool m_SignalPending = false;
};
