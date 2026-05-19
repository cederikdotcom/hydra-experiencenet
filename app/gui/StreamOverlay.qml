import QtQuick 2.0
import QtQuick.Controls 2.2
import QtQuick.Window 2.2

import Session 1.0
import KioskBridge 1.0

// Floating, frameless, translucent window that rides on top of the
// streaming SDL window during an active session. Shows a subtle
// circular handle that expands into a dropdown menu with a single
// "Exit experience" item. Click hit-testing, hover, and animations
// are handled natively by Qt Quick — no SDL overlay coordinate math.
//
// When the visitor taps "Exit experience", the window briefly
// expands to fullscreen with a black veil and a "Quitting
// experience" label to cover the macOS Space transition between the
// stream subprocess closing and the kiosk regaining its fullscreen
// Space. A hard timeout collapses the veil back regardless of how
// that transition plays out so the user can never get stuck behind
// a black screen.
Window {
    id: streamOverlayWindow

    property Session session: null
    property int handleSize: 40
    property int menuHeight: 52
    property int menuWidth: 220
    property bool quitting: false

    // Base (collapsed) geometry for the handle + dropdown. The window
    // switches between this and fullscreen bounds via the `quitting`
    // state; keeping both paths explicit here means failures in the
    // quitting flow just leave the window at its collapsed size
    // instead of anywhere weird.
    readonly property int collapsedWidth: menuWidth
    readonly property int collapsedHeight: handleSize + 8 + menuHeight
    readonly property int collapsedX: (Screen.width - collapsedWidth) / 2
    readonly property int collapsedY: 24

    width: collapsedWidth
    height: collapsedHeight
    x: collapsedX
    y: collapsedY

    flags: Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.Tool | Qt.WindowDoesNotAcceptFocus
    color: "transparent"
    visible: false

    Component.onCompleted: {
        // Ask the platform to mark this window so it appears on every
        // macOS Space, including any fullscreen Space the kiosk or
        // stream window enters.
        KioskBridge.makeFollowAllSpaces(streamOverlayWindow)
    }

    // Timer safeguard: no matter what, shrink the window back to its
    // collapsed geometry after a second and a half. Guards against the
    // kiosk Space taking unusually long to become active (or never
    // doing so) leaving the visitor looking at an opaque black
    // overlay.
    Timer {
        id: collapseTimer
        interval: 1500
        repeat: false
        onTriggered: {
            streamOverlayWindow.quitting = false
        }
    }

    onQuittingChanged: {
        if (quitting) {
            streamOverlayWindow.width = Screen.width
            streamOverlayWindow.height = Screen.height
            streamOverlayWindow.x = 0
            streamOverlayWindow.y = 0
        } else {
            streamOverlayWindow.width = collapsedWidth
            streamOverlayWindow.height = collapsedHeight
            streamOverlayWindow.x = collapsedX
            streamOverlayWindow.y = collapsedY
        }
    }

    // Fullscreen black veil shown during the exit transition. Covers
    // the macOS desktop flash that was visible between the stream
    // Space collapsing and the kiosk Space coming forward.
    Rectangle {
        id: quittingVeil
        anchors.fill: parent
        visible: streamOverlayWindow.quitting
        color: "#0a0a0a"

        // Block any stray click from reaching whatever is underneath
        // during the transition — avoids accidentally tapping kiosk
        // tiles that flicker through on the way back.
        MouseArea {
            anchors.fill: parent
        }

        Text {
            anchors.centerIn: parent
            text: qsTr("Quitting experience")
            color: "#ffffff"
            font.pixelSize: 22
        }
    }

    Rectangle {
        id: handleButton
        visible: !streamOverlayWindow.quitting
        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        width: handleSize
        height: handleSize
        radius: handleSize / 2
        color: handleArea.containsMouse ? "#B0000000" : "#80000000"
        border.color: "#FFFFFFFF"
        border.width: 1

        Text {
            anchors.centerIn: parent
            text: "⋯"
            color: "white"
            font.pixelSize: handleSize * 0.55
            font.bold: true
        }

        MouseArea {
            id: handleArea
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            enabled: !streamOverlayWindow.quitting
            onClicked: {
                exitDropdown.visible = !exitDropdown.visible
            }
        }
    }

    Rectangle {
        id: exitDropdown
        visible: false
        anchors.top: handleButton.bottom
        anchors.topMargin: 8
        anchors.horizontalCenter: parent.horizontalCenter
        width: menuWidth
        height: menuHeight
        color: exitArea.containsMouse ? "#E0000000" : "#CC000000"
        radius: 10
        border.color: "#FFFFFFFF"
        border.width: 1

        Text {
            anchors.centerIn: parent
            text: qsTr("Exit experience")
            color: "white"
            font.pixelSize: 18
        }

        MouseArea {
            id: exitArea
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            enabled: !streamOverlayWindow.quitting
            onClicked: {
                exitDropdown.visible = false
                if (streamOverlayWindow.session !== null) {
                    // Kiosk-initiated stream: exit via the Qt Session object.
                    streamOverlayWindow.quitting = true
                    collapseTimer.restart()
                    streamOverlayWindow.session.triggerExitFromMenu()
                } else {
                    // Agent-initiated stream: no Qt Session object.
                    // Ask the hydraheadflatscreen agent to stop the stream,
                    // then show the quitting veil to cover the transition.
                    var xhr = new XMLHttpRequest()
                    xhr.open("POST", "http://127.0.0.1:9740/api/v1/stream/stop", true)
                    xhr.send()
                    streamOverlayWindow.quitting = true
                    collapseTimer.restart()
                }
            }
        }
    }
}
