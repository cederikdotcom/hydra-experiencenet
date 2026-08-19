#pragma once

#include <QObject>
#include <QElapsedTimer>
#include <QHash>
#include <QSet>
#include <QRectF>
#include <QPointF>

class QMouseEvent;
class QWheelEvent;
class QTouchEvent;
class QKeyEvent;

// QuickInputHandler translates Qt input events from the scene-mode
// VideoItem into Limelight calls (issue #507 M3). It is a plain QObject
// owned by the VideoItem and lives on the GUI thread; all LiSend* input
// functions are queue-based and connection-scoped, so no extra threading
// is involved.
//
// Pointer model (owner decision 2026-08-19, overriding the mouse-first
// order in #507 section 5): ALL pointer interaction is sent as touch.
// - A mouse press on the video becomes LI_TOUCH_EVENT_DOWN with a fixed
//   synthetic pointer id; drags become MOVE; release becomes UP. Hover
//   without buttons sends nothing.
// - Real QTouchEvents pass through natively, with Qt's per-point ids
//   mapped to stable small integers that can never collide with the
//   synthetic mouse pointer id.
// - The wheel is sent through LiSendHighResScrollEvent (and the
//   horizontal variant), which is harmless on touch-only hosts and
//   useful on mixed ones.
// - If the host does not support LiSendTouchEvent (LI_FF_PEN_TOUCH_EVENTS
//   missing, or a defensive LI_ERR_UNSUPPORTED return), the handler logs
//   once and falls back to LiSendMousePositionEvent plus
//   LiSendMouseButtonEvent for the rest of the session.
//
// Keyboard: QKeyEvent to LiSendKeyboardEvent2 through an evdev-to-VK
// table (nativeScanCode() minus 8 is the evdev code under xcb; the
// handler verifies the platform is xcb at runtime and disables keyboard
// forwarding with one warning elsewhere). Pressed keys are tracked so
// raise-all-keys can fire on deactivation and stream end, mirroring
// SdlInputHandler::raiseAllKeys().
//
// All coordinates are normalized against the video rectangle the caller
// passes with each event: the VideoItem computes it with the exact same
// letterbox math its render node uses, so pointer coordinates and pixels
// agree.
class QuickInputHandler : public QObject
{
    Q_OBJECT

public:
    explicit QuickInputHandler(QObject* parent = nullptr);

    // Mouse press/move/release on the video item. videoRect is the
    // letterboxed video rectangle in item-local coordinates.
    void handleMousePress(QMouseEvent* event, const QRectF& videoRect);
    void handleMouseMove(QMouseEvent* event, const QRectF& videoRect);
    void handleMouseRelease(QMouseEvent* event, const QRectF& videoRect);

    // Wheel event over the video item
    void handleWheel(QWheelEvent* event, const QRectF& videoRect);

    // Native multi-touch passthrough
    void handleTouch(QTouchEvent* event, const QRectF& videoRect);

    // Keyboard forwarding (requires activeFocus on the VideoItem)
    void handleKeyPress(QKeyEvent* event);
    void handleKeyRelease(QKeyEvent* event);

    // The item lost its implicit mouse grab mid-interaction: cancel the
    // synthetic pointer (or release fallback buttons)
    void cancelMousePointer();

    // The item lost its touch grab: cancel all active real touches
    void cancelAllTouches();

    // Window deactivated or the stream is ending: raise all pressed keys
    // and cancel every active pointer
    void deactivate();

signals:
    // Ctrl+Alt+Shift+Q parity with SdlInputHandler's quit combo. The
    // kiosk stream page connects this to Session::stopSession().
    void quitRequested();

private:
    bool touchModeActive();
    void engageMouseFallback(const char* reason);
    bool keyboardUsable();
    void raiseAllKeys();
    void handleKeyEvent(QKeyEvent* event, bool pressed);

    bool normalizePoint(const QPointF& pos, const QRectF& videoRect,
                        float& x, float& y) const;
    void sendFallbackPosition(const QPointF& pos, const QRectF& videoRect);

    uint32_t acquireTouchPointId(int qtId);
    void releaseTouchPointId(int qtId);

    // Touch capability state. Resolved lazily on the first pointer event
    // (the connection is up by then, so LiGetHostFeatureFlags() is valid).
    bool m_TouchSupportKnown = false;
    bool m_TouchSupported = false;

    // Synthetic mouse pointer state (touch mode)
    int m_MouseTouchButton = 0; // Qt::MouseButton of the initiating press, 0 when up

    // Fallback mouse state (non-touch host)
    QSet<int> m_FallbackButtonsDown; // BUTTON_* codes currently pressed
    int m_FallbackTouchQtId = -1;    // Qt touch point id acting as the left button

    // Real touch pointer id mapping: Qt point id to stable small integer
    QHash<int, uint32_t> m_TouchIdMap;
    QSet<uint32_t> m_TouchIdsInUse;

    // Move coalescing gates (one frame interval at 60 Hz)
    QElapsedTimer m_MouseMoveGate;
    QElapsedTimer m_TouchMoveGate;

    // Keyboard state
    bool m_KeyboardPlatformChecked = false;
    bool m_KeyboardPlatformOk = false;
    QSet<short> m_KeysDown;
};
