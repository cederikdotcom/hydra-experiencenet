import QtQuick 2.0
import QtQuick.Controls 2.2
import QtQuick.Window 2.2

import Session 1.0
import KioskBridge 1.0

// Floating, frameless, translucent window that rides on top of the
// streaming SDL window during an active session. Shows a subtle
// circular handle that expands into a dropdown with a single
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

    readonly property int collapsedWidth:  menuWidth
    readonly property int collapsedHeight: handleSize + 8 + menuHeight
    readonly property int collapsedX:      (Screen.width  - collapsedWidth)  / 2
    readonly property int collapsedY:      24

    width:  collapsedWidth
    height: collapsedHeight
    x:      collapsedX
    y:      collapsedY

    flags: Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.Tool |
           (pinPrompt.visible ? 0 : Qt.WindowDoesNotAcceptFocus)
    color: "transparent"
    visible: false

    Component.onCompleted: {
        KioskBridge.makeFollowAllSpaces(streamOverlayWindow)
    }

    Timer {
        id: collapseTimer
        interval: 1500
        repeat: false
        onTriggered: {
            streamOverlayWindow.quitting = false
        }
    }

    property bool quitting: false

    function updateGeometry() {
        var expanded = quitting || pinPrompt.visible
        width = expanded ? Screen.width : collapsedWidth
        height = expanded ? Screen.height : collapsedHeight
        x = Screen.virtualX + (expanded ? 0 : collapsedX)
        y = Screen.virtualY + (expanded ? 0 : collapsedY)
    }

    onQuittingChanged: updateGeometry()

    function exitExperience() {
        quitting = true
        collapseTimer.restart()
        if (session !== null) {
            session.triggerExitFromMenu()
        } else {
            var xhr = new XMLHttpRequest()
            xhr.open("POST", "http://127.0.0.1:9740/api/v1/stream/stop", true)
            xhr.send()
        }
    }

    OperatorPinPrompt {
        id: pinPrompt
        anchors.fill: parent
        z: 10
        onVisibleChanged: {
            streamOverlayWindow.updateGeometry()
            if (visible) {
                Qt.callLater(function() {
                    streamOverlayWindow.raise()
                    streamOverlayWindow.requestActivate()
                    pinPrompt.focusInput()
                })
            }
        }
        onAuthorized: streamOverlayWindow.exitExperience()
    }

    // Fullscreen black veil shown during the exit transition.
    Rectangle {
        id: quittingVeil
        anchors.fill: parent
        visible: streamOverlayWindow.quitting
        color: "#0a0a0a"

        MouseArea { anchors.fill: parent }

        Text {
            anchors.centerIn: parent
            text: qsTr("Quitting experience")
            color: "#ffffff"
            font.pixelSize: 22
        }
    }

    Rectangle {
        id: handleButton
        visible: !streamOverlayWindow.quitting && !pinPrompt.visible
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
                pinPrompt.request()
            }
        }
    }
}
