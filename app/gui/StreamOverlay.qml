import QtQuick 2.0
import QtQuick.Controls 2.2
import QtQuick.Window 2.2

import Session 1.0

// Floating, frameless, translucent window that rides on top of the
// streaming SDL window during an active session. Shows a subtle
// circular handle that expands into a dropdown menu with a single
// "Exit experience" item. Click hit-testing, hover, and animations
// are handled natively by Qt Quick — no SDL overlay coordinate math.
Window {
    id: streamOverlayWindow

    property Session session: null
    property int handleSize: 40
    property int menuHeight: 52
    property int menuWidth: 220

    // Anchor the window to the top-right of the primary screen with
    // a small margin so the handle sits comfortably above the stream.
    x: Screen.width - width - 24
    y: 24
    width: menuWidth
    // Reserve space for the expanded menu too so the window's input
    // surface covers the dropdown area when it opens.
    height: handleSize + 8 + menuHeight

    flags: Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.Tool | Qt.WindowDoesNotAcceptFocus
    color: "transparent"
    visible: false

    // Keep the window click-through for the transparent gaps between
    // the handle and the menu, but still responsive to clicks on the
    // handle and menu rectangles themselves.
    Rectangle {
        id: handleButton
        anchors.top: parent.top
        anchors.right: parent.right
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
        anchors.right: parent.right
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
            onClicked: {
                exitDropdown.visible = false
                streamOverlayWindow.visible = false
                if (streamOverlayWindow.session !== null) {
                    streamOverlayWindow.session.triggerExitFromMenu()
                }
            }
        }
    }
}
