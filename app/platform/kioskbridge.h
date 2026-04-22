#pragma once

#include <QObject>
#include <QWindow>

#ifdef __APPLE__
#include "macos_permissions.h"
#endif

// Small QObject exposed as a QML singleton to let QML code invoke the
// platform-specific kiosk helpers without each caller having to thread
// a C++ object through. Currently only used to ask the floating Qt exit
// overlay to follow all macOS Spaces once its Window is constructed.
class KioskBridge : public QObject
{
    Q_OBJECT

public:
    explicit KioskBridge(QObject* parent = nullptr) : QObject(parent) {}

    // Called from StreamOverlay.qml's Component.onCompleted, passing
    // the overlay Window itself. Grabs the native winId (an NSView* on
    // macOS) and asks the platform helper to set collectionBehavior so
    // the window appears on every Space, including a fullscreen Space
    // that belongs to the kiosk or stream window.
    Q_INVOKABLE void makeFollowAllSpaces(QWindow* window)
    {
        if (window == nullptr) {
            return;
        }
#ifdef __APPLE__
        WId id = window->winId();
        makeWindowFollowAllSpaces(static_cast<uint64_t>(id));
#else
        Q_UNUSED(window);
#endif
    }
};
