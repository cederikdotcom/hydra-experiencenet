#include "quickinput.h"

#include <Limelight.h>
#include "SDL_compat.h"

#include <QGuiApplication>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QTouchEvent>
#include <QKeyEvent>

namespace {

// Fixed pointer id for the synthetic touch pointer driven by the mouse.
// Real touch points are mapped to small integers counting up from zero,
// so a value at the top of the 32-bit range can never collide.
const uint32_t k_MousePointerId = 0xFFFFFFFEu;

// Minimum interval between coalesced touch MOVE sends. One frame interval
// at 60 Hz: Qt can flood pointer moves far faster than the stream can
// show them, and the UP event carries the final coordinates anyway.
const qint64 k_MoveIntervalMs = 16;

// Windows VK codes used by the table below (values as in keyboard.cpp)
const short k_Vk0 = 0x30;
const short k_VkA = 0x41;
const short k_VkF1 = 0x70;
const short k_VkF13 = 0x7C;
const short k_VkNumpad0 = 0x60;

// Maps a Linux evdev key code (X11 keycode minus 8 under xcb) to a
// Windows VK code, or 0 if unmapped. This is the mechanical composition
// of the kernel's evdev-to-HID table with keyboard.cpp's SDL-scancode
// (USB HID) switch, per issue #507 section 5. nonNormalized mirrors the
// SS_KBE_FLAG_NON_NORMALIZED cases in keyboard.cpp (SDL INTERNATIONAL1
// and INTERNATIONAL3, which are evdev KEY_RO and KEY_YEN).
short evdevToVk(quint32 evdevCode, bool* nonNormalized)
{
    *nonNormalized = false;

    // Contiguous ranges first, mirroring keyboard.cpp's range checks
    if (evdevCode >= 2 && evdevCode <= 10) {
        // KEY_1 (2) .. KEY_9 (10)
        return (short)(evdevCode - 2) + k_Vk0 + 1;
    }
    if (evdevCode >= 59 && evdevCode <= 68) {
        // KEY_F1 (59) .. KEY_F10 (68)
        return (short)(evdevCode - 59) + k_VkF1;
    }
    if (evdevCode >= 183 && evdevCode <= 194) {
        // KEY_F13 (183) .. KEY_F24 (194)
        return (short)(evdevCode - 183) + k_VkF13;
    }

    switch (evdevCode) {
    // Letters (evdev is laid out in QWERTY rows, not alphabetically)
    case 16: return k_VkA + ('Q' - 'A');
    case 17: return k_VkA + ('W' - 'A');
    case 18: return k_VkA + ('E' - 'A');
    case 19: return k_VkA + ('R' - 'A');
    case 20: return k_VkA + ('T' - 'A');
    case 21: return k_VkA + ('Y' - 'A');
    case 22: return k_VkA + ('U' - 'A');
    case 23: return k_VkA + ('I' - 'A');
    case 24: return k_VkA + ('O' - 'A');
    case 25: return k_VkA + ('P' - 'A');
    case 30: return k_VkA;              // A
    case 31: return k_VkA + ('S' - 'A');
    case 32: return k_VkA + ('D' - 'A');
    case 33: return k_VkA + ('F' - 'A');
    case 34: return k_VkA + ('G' - 'A');
    case 35: return k_VkA + ('H' - 'A');
    case 36: return k_VkA + ('J' - 'A');
    case 37: return k_VkA + ('K' - 'A');
    case 38: return k_VkA + ('L' - 'A');
    case 44: return k_VkA + ('Z' - 'A');
    case 45: return k_VkA + ('X' - 'A');
    case 46: return k_VkA + ('C' - 'A');
    case 47: return k_VkA + ('V' - 'A');
    case 48: return k_VkA + ('B' - 'A');
    case 49: return k_VkA + ('N' - 'A');
    case 50: return k_VkA + ('M' - 'A');

    case 11: return k_Vk0;   // KEY_0
    case 1:  return 0x1B;    // KEY_ESC
    case 12: return 0xBD;    // KEY_MINUS
    case 13: return 0xBB;    // KEY_EQUAL
    case 14: return 0x08;    // KEY_BACKSPACE
    case 15: return 0x09;    // KEY_TAB
    case 26: return 0xDB;    // KEY_LEFTBRACE
    case 27: return 0xDD;    // KEY_RIGHTBRACE
    case 28: return 0x0D;    // KEY_ENTER
    case 29: return 0xA2;    // KEY_LEFTCTRL
    case 39: return 0xBA;    // KEY_SEMICOLON
    case 40: return 0xDE;    // KEY_APOSTROPHE
    case 41: return 0xC0;    // KEY_GRAVE
    case 42: return 0xA0;    // KEY_LEFTSHIFT
    case 43: return 0xDC;    // KEY_BACKSLASH
    case 51: return 0xBC;    // KEY_COMMA
    case 52: return 0xBE;    // KEY_DOT
    case 53: return 0xBF;    // KEY_SLASH
    case 54: return 0xA1;    // KEY_RIGHTSHIFT
    case 55: return 0x6A;    // KEY_KPASTERISK
    case 56: return 0xA4;    // KEY_LEFTALT
    case 57: return 0x20;    // KEY_SPACE
    case 58: return 0x14;    // KEY_CAPSLOCK
    case 69: return 0x90;    // KEY_NUMLOCK
    case 70: return 0x91;    // KEY_SCROLLLOCK

    // Keypad digits (VK_NUMPAD0 is 0x60 and counts up; evdev lays the
    // pad out 7-8-9 / 4-5-6 / 1-2-3 / 0)
    case 71: return k_VkNumpad0 + 7; // KEY_KP7
    case 72: return k_VkNumpad0 + 8; // KEY_KP8
    case 73: return k_VkNumpad0 + 9; // KEY_KP9
    case 74: return 0x6D;            // KEY_KPMINUS
    case 75: return k_VkNumpad0 + 4; // KEY_KP4
    case 76: return k_VkNumpad0 + 5; // KEY_KP5
    case 77: return k_VkNumpad0 + 6; // KEY_KP6
    case 78: return 0x6B;            // KEY_KPPLUS
    case 79: return k_VkNumpad0 + 1; // KEY_KP1
    case 80: return k_VkNumpad0 + 2; // KEY_KP2
    case 81: return k_VkNumpad0 + 3; // KEY_KP3
    case 82: return k_VkNumpad0;     // KEY_KP0
    case 83: return 0x6E;            // KEY_KPDOT

    case 86: return 0xE2;    // KEY_102ND (SDL NONUSBACKSLASH)
    case 87: return 0x7A;    // KEY_F11
    case 88: return 0x7B;    // KEY_F12
    case 89:                 // KEY_RO (SDL INTERNATIONAL1)
        *nonNormalized = true;
        return 0xE2;
    case 96: return 0x0D;    // KEY_KPENTER
    case 97: return 0xA3;    // KEY_RIGHTCTRL
    case 98: return 0x6F;    // KEY_KPSLASH
    case 99: return 0x2C;    // KEY_SYSRQ (print screen)
    case 100: return 0xA5;   // KEY_RIGHTALT
    case 102: return 0x24;   // KEY_HOME
    case 103: return 0x26;   // KEY_UP
    case 104: return 0x21;   // KEY_PAGEUP
    case 105: return 0x25;   // KEY_LEFT
    case 106: return 0x27;   // KEY_RIGHT
    case 107: return 0x23;   // KEY_END
    case 108: return 0x28;   // KEY_DOWN
    case 109: return 0x22;   // KEY_PAGEDOWN
    case 110: return 0x2D;   // KEY_INSERT
    case 111: return 0x2E;   // KEY_DELETE
    case 119: return 0x13;   // KEY_PAUSE
    case 121: return 0x6C;   // KEY_KPCOMMA
    case 122: return 0x1C;   // KEY_HANGEUL (SDL LANG1)
    case 123: return 0x1D;   // KEY_HANJA (SDL LANG2)
    case 124:                // KEY_YEN (SDL INTERNATIONAL3)
        *nonNormalized = true;
        return 0xDC;
    // KEY_LEFTMETA (125) and KEY_RIGHTMETA (126) are deliberately absent:
    // the Super key is never forwarded (see handleKeyEvent)
    case 127: return 0x5D;   // KEY_COMPOSE (SDL APPLICATION)
    case 128: return 0xA9;   // KEY_STOP (SDL AC_STOP)
    case 138: return 0x2F;   // KEY_HELP
    case 156: return 0xAB;   // KEY_BOOKMARKS (SDL AC_BOOKMARKS)
    case 158: return 0xA6;   // KEY_BACK (SDL AC_BACK)
    case 159: return 0xA7;   // KEY_FORWARD (SDL AC_FORWARD)
    case 172: return 0xAC;   // KEY_HOMEPAGE (SDL AC_HOME)
    case 173: return 0xA8;   // KEY_REFRESH (SDL AC_REFRESH)
    case 217: return 0xAA;   // KEY_SEARCH (SDL AC_SEARCH)

    default:
        return 0;
    }
}

int mapMouseButton(Qt::MouseButton button)
{
    switch (button) {
    case Qt::LeftButton:
        return BUTTON_LEFT;
    case Qt::MiddleButton:
        return BUTTON_MIDDLE;
    case Qt::RightButton:
        return BUTTON_RIGHT;
    case Qt::XButton1:
        return BUTTON_X1;
    case Qt::XButton2:
        return BUTTON_X2;
    default:
        return 0;
    }
}

} // namespace

QuickInputHandler::QuickInputHandler(QObject* parent)
    : QObject(parent)
{
    m_MouseMoveGate.start();
    m_TouchMoveGate.start();
}

bool QuickInputHandler::normalizePoint(const QPointF& pos, const QRectF& videoRect,
                                       float& x, float& y) const
{
    if (videoRect.width() <= 0.0 || videoRect.height() <= 0.0) {
        // No frame yet; there is nothing meaningful to normalize against
        return false;
    }

    // Clamp so drags that leave the video rectangle stick to its edge,
    // matching the SDL path's clamping behavior in mouse.cpp
    x = (float)qBound(0.0, (pos.x() - videoRect.x()) / videoRect.width(), 1.0);
    y = (float)qBound(0.0, (pos.y() - videoRect.y()) / videoRect.height(), 1.0);
    return true;
}

// Resolves the touch capability of the host once per session (which is
// once per handler, since the kiosk stream page creates a fresh VideoItem
// and handler for every stream). Only called after the connection is up,
// because input events are gated on streamActive in VideoItem.
bool QuickInputHandler::touchModeActive()
{
    if (!m_TouchSupportKnown) {
        m_TouchSupportKnown = true;
        m_TouchSupported = (LiGetHostFeatureFlags() & LI_FF_PEN_TOUCH_EVENTS) != 0;
        if (m_TouchSupported) {
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                        "QuickInputHandler: host supports touch events, sending pointer input as touch");
        }
        else {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "QuickInputHandler: host does not support touch events, "
                        "using mouse position/button fallback for this session");
        }
    }

    return m_TouchSupported;
}

// Defensive path for an LI_ERR_UNSUPPORTED return despite the feature
// flag: log once and stay on the mouse fallback for the session.
void QuickInputHandler::engageMouseFallback(const char* reason)
{
    if (m_TouchSupported) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "QuickInputHandler: %s, using mouse position/button fallback for this session",
                    reason);
    }
    m_TouchSupportKnown = true;
    m_TouchSupported = false;
}

void QuickInputHandler::sendFallbackPosition(const QPointF& pos, const QRectF& videoRect)
{
    float x, y;
    if (!normalizePoint(pos, videoRect, x, y)) {
        return;
    }

    // Same semantics as mouse.cpp's absolute mode: coordinates relative
    // to the video rectangle with the rectangle size as the reference
    LiSendMousePositionEvent((short)(x * videoRect.width()),
                             (short)(y * videoRect.height()),
                             (short)videoRect.width(),
                             (short)videoRect.height());
}

void QuickInputHandler::handleMousePress(QMouseEvent* event, const QRectF& videoRect)
{
    if (event->source() != Qt::MouseEventNotSynthesized) {
        // Real QTouchEvents pass through natively; ignore Qt's synthetic
        // mouse duplicates (analog of SDL_TOUCH_MOUSEID filtering)
        return;
    }

    // Ignore presses in the letterbox bars, like the SDL path ignores
    // button events outside the video region
    if (!videoRect.contains(event->position())) {
        return;
    }

    float x, y;
    if (!normalizePoint(event->position(), videoRect, x, y)) {
        return;
    }

    event->accept();

    if (touchModeActive()) {
        if (m_MouseTouchButton != 0) {
            // The synthetic pointer is single-touch; a second button
            // during a drag has no touch representation
            return;
        }

        int err = LiSendTouchEvent(LI_TOUCH_EVENT_DOWN, k_MousePointerId,
                                   x, y, 1.0f, 0.0f, 0.0f, LI_ROT_UNKNOWN);
        if (err == 0) {
            m_MouseTouchButton = (int)event->button();
            m_MouseMoveGate.restart();
            return;
        }

        if (err != LI_ERR_UNSUPPORTED) {
            // Transient send failure; drop the event
            return;
        }

        engageMouseFallback("LiSendTouchEvent() returned LI_ERR_UNSUPPORTED");
        // Fall through so the press is not lost
    }

    int button = mapMouseButton(event->button());
    if (button == 0) {
        return;
    }

    sendFallbackPosition(event->position(), videoRect);
    LiSendMouseButtonEvent(BUTTON_ACTION_PRESS, button);
    m_FallbackButtonsDown.insert(button);
}

void QuickInputHandler::handleMouseMove(QMouseEvent* event, const QRectF& videoRect)
{
    if (event->source() != Qt::MouseEventNotSynthesized) {
        return;
    }

    // QQuickItem only delivers mouse moves while a button grab is held,
    // so this is always a drag. Hover without buttons sends nothing in
    // the touch model by design.
    if (event->buttons() == Qt::NoButton) {
        return;
    }

    event->accept();

    if (m_MouseMoveGate.elapsed() < k_MoveIntervalMs) {
        // Coalesce: Qt can flood far beyond the stream frame rate. The
        // release always carries the final coordinates.
        return;
    }

    if (touchModeActive()) {
        if (m_MouseTouchButton == 0) {
            return; // No active synthetic pointer (press was ignored)
        }

        float x, y;
        if (!normalizePoint(event->position(), videoRect, x, y)) {
            return;
        }

        if (LiSendTouchEvent(LI_TOUCH_EVENT_MOVE, k_MousePointerId,
                             x, y, 1.0f, 0.0f, 0.0f, LI_ROT_UNKNOWN) == 0) {
            m_MouseMoveGate.restart();
        }
    }
    else if (!m_FallbackButtonsDown.isEmpty()) {
        sendFallbackPosition(event->position(), videoRect);
        m_MouseMoveGate.restart();
    }
}

void QuickInputHandler::handleMouseRelease(QMouseEvent* event, const QRectF& videoRect)
{
    if (event->source() != Qt::MouseEventNotSynthesized) {
        return;
    }

    event->accept();

    if (touchModeActive()) {
        if (m_MouseTouchButton == 0 || (int)event->button() != m_MouseTouchButton) {
            return;
        }

        float x, y;
        if (normalizePoint(event->position(), videoRect, x, y)) {
            LiSendTouchEvent(LI_TOUCH_EVENT_UP, k_MousePointerId,
                             x, y, 0.0f, 0.0f, 0.0f, LI_ROT_UNKNOWN);
        }
        else {
            LiSendTouchEvent(LI_TOUCH_EVENT_CANCEL, k_MousePointerId,
                             0.0f, 0.0f, 0.0f, 0.0f, 0.0f, LI_ROT_UNKNOWN);
        }
        m_MouseTouchButton = 0;
        return;
    }

    int button = mapMouseButton(event->button());
    if (button == 0 || !m_FallbackButtonsDown.contains(button)) {
        return;
    }

    sendFallbackPosition(event->position(), videoRect);
    LiSendMouseButtonEvent(BUTTON_ACTION_RELEASE, button);
    m_FallbackButtonsDown.remove(button);
}

void QuickInputHandler::handleWheel(QWheelEvent* event, const QRectF& videoRect)
{
    // Match the SDL path: ignore scrolls outside the video region
    if (!videoRect.contains(event->position())) {
        return;
    }

    // QWheelEvent::angleDelta() is in eighths of a degree with one wheel
    // notch at 120 units, which is exactly WHEEL_DELTA (the unit
    // LiSendHighResScrollEvent expects, matching mouse.cpp's
    // preciseY * 120)
    const QPoint delta = event->angleDelta();
    if (delta.y() != 0) {
        LiSendHighResScrollEvent((short)qBound(-32768, delta.y(), 32767));
    }
    if (delta.x() != 0) {
        LiSendHighResHScrollEvent((short)qBound(-32768, delta.x(), 32767));
    }

    event->accept();
}

uint32_t QuickInputHandler::acquireTouchPointId(int qtId)
{
    auto it = m_TouchIdMap.constFind(qtId);
    if (it != m_TouchIdMap.constEnd()) {
        return it.value();
    }

    // Lowest free small integer, so ids stay stable and compact and can
    // never collide with k_MousePointerId
    uint32_t id = 0;
    while (m_TouchIdsInUse.contains(id)) {
        id++;
    }

    m_TouchIdMap.insert(qtId, id);
    m_TouchIdsInUse.insert(id);
    return id;
}

void QuickInputHandler::releaseTouchPointId(int qtId)
{
    auto it = m_TouchIdMap.constFind(qtId);
    if (it != m_TouchIdMap.constEnd()) {
        m_TouchIdsInUse.remove(it.value());
        m_TouchIdMap.erase(it);
    }
}

void QuickInputHandler::handleTouch(QTouchEvent* event, const QRectF& videoRect)
{
    event->accept();

    if (event->type() == QEvent::TouchCancel) {
        cancelAllTouches();
        return;
    }

    if (!touchModeActive()) {
        // Fallback for non-touch hosts: compress the first finger to the
        // left mouse button, like emulateAbsoluteFingerEvent() does at
        // its core (without the long-press gesture layer)
        for (const QEventPoint& point : event->points()) {
            switch (point.state()) {
            case QEventPoint::Pressed:
                if (m_FallbackTouchQtId == -1 && videoRect.contains(point.position())) {
                    m_FallbackTouchQtId = point.id();
                    sendFallbackPosition(point.position(), videoRect);
                    LiSendMouseButtonEvent(BUTTON_ACTION_PRESS, BUTTON_LEFT);
                    m_FallbackButtonsDown.insert(BUTTON_LEFT);
                }
                break;
            case QEventPoint::Updated:
                if (point.id() == m_FallbackTouchQtId &&
                    m_TouchMoveGate.elapsed() >= k_MoveIntervalMs) {
                    sendFallbackPosition(point.position(), videoRect);
                    m_TouchMoveGate.restart();
                }
                break;
            case QEventPoint::Released:
                if (point.id() == m_FallbackTouchQtId) {
                    sendFallbackPosition(point.position(), videoRect);
                    LiSendMouseButtonEvent(BUTTON_ACTION_RELEASE, BUTTON_LEFT);
                    m_FallbackButtonsDown.remove(BUTTON_LEFT);
                    m_FallbackTouchQtId = -1;
                }
                break;
            default:
                break;
            }
        }
        return;
    }

    // One gate decision per event batch so multi-finger updates stay
    // coherent (either the whole batch goes out or none of it)
    const bool moveGateOpen = m_TouchMoveGate.elapsed() >= k_MoveIntervalMs;
    bool sentMove = false;

    for (const QEventPoint& point : event->points()) {
        float x, y;

        switch (point.state()) {
        case QEventPoint::Pressed:
            // Ignore touches that begin in the letterbox bars
            if (videoRect.contains(point.position()) &&
                normalizePoint(point.position(), videoRect, x, y)) {
                uint32_t pointerId = acquireTouchPointId(point.id());
                float pressure = point.pressure() > 0.0 ? (float)point.pressure() : 0.0f;
                int err = LiSendTouchEvent(LI_TOUCH_EVENT_DOWN, pointerId,
                                           x, y, pressure, 0.0f, 0.0f, LI_ROT_UNKNOWN);
                if (err != 0) {
                    releaseTouchPointId(point.id());
                    if (err == LI_ERR_UNSUPPORTED) {
                        engageMouseFallback("LiSendTouchEvent() returned LI_ERR_UNSUPPORTED");
                        // Do not lose the touch that discovered the
                        // missing feature: start the fallback press for
                        // it (no earlier DOWN can have succeeded, the
                        // feature is missing host-side)
                        m_FallbackTouchQtId = point.id();
                        sendFallbackPosition(point.position(), videoRect);
                        LiSendMouseButtonEvent(BUTTON_ACTION_PRESS, BUTTON_LEFT);
                        m_FallbackButtonsDown.insert(BUTTON_LEFT);
                        return;
                    }
                }
            }
            break;

        case QEventPoint::Updated:
            if (moveGateOpen && m_TouchIdMap.contains(point.id()) &&
                normalizePoint(point.position(), videoRect, x, y)) {
                float pressure = point.pressure() > 0.0 ? (float)point.pressure() : 0.0f;
                LiSendTouchEvent(LI_TOUCH_EVENT_MOVE, m_TouchIdMap.value(point.id()),
                                 x, y, pressure, 0.0f, 0.0f, LI_ROT_UNKNOWN);
                sentMove = true;
            }
            break;

        case QEventPoint::Released:
            if (m_TouchIdMap.contains(point.id())) {
                uint32_t pointerId = m_TouchIdMap.value(point.id());
                if (normalizePoint(point.position(), videoRect, x, y)) {
                    LiSendTouchEvent(LI_TOUCH_EVENT_UP, pointerId,
                                     x, y, 0.0f, 0.0f, 0.0f, LI_ROT_UNKNOWN);
                }
                else {
                    LiSendTouchEvent(LI_TOUCH_EVENT_CANCEL, pointerId,
                                     0.0f, 0.0f, 0.0f, 0.0f, 0.0f, LI_ROT_UNKNOWN);
                }
                releaseTouchPointId(point.id());
            }
            break;

        default:
            // Stationary points need no event
            break;
        }
    }

    if (sentMove) {
        m_TouchMoveGate.restart();
    }
}

void QuickInputHandler::handleKeyPress(QKeyEvent* event)
{
    handleKeyEvent(event, true);
}

void QuickInputHandler::handleKeyRelease(QKeyEvent* event)
{
    handleKeyEvent(event, false);
}

// The evdev translation (nativeScanCode() minus 8) only holds under xcb.
// The kiosk always runs under xcb (the agent forces QT_QPA_PLATFORM=xcb),
// but assert it at runtime so the M5 Wayland experiment cannot silently
// send garbage VK codes: on any other platform, warn once and skip
// keyboard forwarding.
bool QuickInputHandler::keyboardUsable()
{
    if (!m_KeyboardPlatformChecked) {
        m_KeyboardPlatformChecked = true;
        m_KeyboardPlatformOk =
            QGuiApplication::platformName() == QLatin1String("xcb");
        if (!m_KeyboardPlatformOk) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "QuickInputHandler: keyboard forwarding requires the xcb platform "
                        "(running on '%s'), keyboard input disabled",
                        qPrintable(QGuiApplication::platformName()));
        }
    }

    return m_KeyboardPlatformOk;
}

void QuickInputHandler::handleKeyEvent(QKeyEvent* event, bool pressed)
{
    // Ignore auto-repeat events entirely, like keyboard.cpp ignores SDL
    // repeat downs: the host sees the key held (one DOWN, one final UP)
    // and generates its own repeats. Forwarding Qt repeats would either
    // double the repeat rate (detectable auto-repeat delivers extra
    // downs) or defeat host repeat (the legacy X11 mode delivers up and
    // down pairs). The real release has isAutoRepeat() false.
    if (event->isAutoRepeat()) {
        return;
    }

    const Qt::KeyboardModifiers mods = event->modifiers();

    // Ctrl+Alt+Shift+Q quit combo, parity with SdlInputHandler
    if (pressed &&
        (mods & Qt::ControlModifier) &&
        (mods & Qt::AltModifier) &&
        (mods & Qt::ShiftModifier) &&
        event->key() == Qt::Key_Q) {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "QuickInputHandler: detected quit key combo");
        deactivate();
        emit quitRequested();
        return;
    }

    if (!keyboardUsable()) {
        return;
    }

    const quint32 nativeScanCode = event->nativeScanCode();
    if (nativeScanCode < 8) {
        return;
    }

    const quint32 evdevCode = nativeScanCode - 8;

    // Never forward the Super key (evdev KEY_LEFTMETA/KEY_RIGHTMETA).
    // Hyprland owns Super on omarchy, and the SDL path only forwards
    // LGUI/RGUI when system key capture is active, which scene mode
    // never has. Silent: this fires on every Hyprland-owned chord.
    if (evdevCode == 125 || evdevCode == 126) {
        return;
    }

    bool nonNormalized = false;
    const short keyCode = evdevToVk(evdevCode, &nonNormalized);
    if (keyCode == 0) {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "QuickInputHandler: unhandled key event, evdev code %u",
                    evdevCode);
        return;
    }

    // Modifier byte per keyboard.cpp's MODIFIER mapping. MODIFIER_META is
    // deliberately never set: the SDL path only sets it with system key
    // capture, which has no scene-mode analog, and the Super key itself
    // is never forwarded (Hyprland owns it on omarchy).
    char modifiers = 0;
    if (mods & Qt::ShiftModifier) {
        modifiers |= MODIFIER_SHIFT;
    }
    if (mods & Qt::ControlModifier) {
        modifiers |= MODIFIER_CTRL;
    }
    if (mods & Qt::AltModifier) {
        modifiers |= MODIFIER_ALT;
    }

    // Track pressed keys so raise-all-keys can release them
    if (pressed) {
        m_KeysDown.insert(keyCode);
    }
    else {
        m_KeysDown.remove(keyCode);
    }

    // 0x8000 marks the VK as scancode-derived, exactly as keyboard.cpp
    LiSendKeyboardEvent2((short)(0x8000 | keyCode),
                         pressed ? KEY_ACTION_DOWN : KEY_ACTION_UP,
                         modifiers,
                         nonNormalized ? SS_KBE_FLAG_NON_NORMALIZED : 0);
}

void QuickInputHandler::raiseAllKeys()
{
    if (m_KeysDown.isEmpty()) {
        return;
    }

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "QuickInputHandler: raising %d keys",
                (int)m_KeysDown.count());

    for (auto keyDown : std::as_const(m_KeysDown)) {
        LiSendKeyboardEvent(keyDown, KEY_ACTION_UP, 0);
    }

    m_KeysDown.clear();
}

void QuickInputHandler::cancelMousePointer()
{
    if (m_MouseTouchButton != 0) {
        // LI_TOUCH_EVENT_CANCEL: only the pointerId parameter is valid
        LiSendTouchEvent(LI_TOUCH_EVENT_CANCEL, k_MousePointerId,
                         0.0f, 0.0f, 0.0f, 0.0f, 0.0f, LI_ROT_UNKNOWN);
        m_MouseTouchButton = 0;
    }

    if (!m_FallbackButtonsDown.isEmpty()) {
        for (int button : std::as_const(m_FallbackButtonsDown)) {
            LiSendMouseButtonEvent(BUTTON_ACTION_RELEASE, button);
        }
        m_FallbackButtonsDown.clear();
        m_FallbackTouchQtId = -1;
    }
}

void QuickInputHandler::cancelAllTouches()
{
    for (auto it = m_TouchIdMap.constBegin(); it != m_TouchIdMap.constEnd(); ++it) {
        LiSendTouchEvent(LI_TOUCH_EVENT_CANCEL, it.value(),
                         0.0f, 0.0f, 0.0f, 0.0f, 0.0f, LI_ROT_UNKNOWN);
    }
    m_TouchIdMap.clear();
    m_TouchIdsInUse.clear();

    if (m_FallbackTouchQtId != -1) {
        LiSendMouseButtonEvent(BUTTON_ACTION_RELEASE, BUTTON_LEFT);
        m_FallbackButtonsDown.remove(BUTTON_LEFT);
        m_FallbackTouchQtId = -1;
    }
}

void QuickInputHandler::deactivate()
{
    raiseAllKeys();

    // Cancel every active pointer. LI_TOUCH_EVENT_CANCEL_ALL covers the
    // synthetic mouse pointer and all real touches in one event (the
    // documented focus-loss usage in Limelight.h).
    if (m_MouseTouchButton != 0 || !m_TouchIdMap.isEmpty()) {
        LiSendTouchEvent(LI_TOUCH_EVENT_CANCEL_ALL, 0,
                         0.0f, 0.0f, 0.0f, 0.0f, 0.0f, LI_ROT_UNKNOWN);
        m_MouseTouchButton = 0;
        m_TouchIdMap.clear();
        m_TouchIdsInUse.clear();
    }

    if (!m_FallbackButtonsDown.isEmpty()) {
        for (int button : std::as_const(m_FallbackButtonsDown)) {
            LiSendMouseButtonEvent(BUTTON_ACTION_RELEASE, button);
        }
        m_FallbackButtonsDown.clear();
    }
    m_FallbackTouchQtId = -1;
}
