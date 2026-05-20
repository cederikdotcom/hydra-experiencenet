import QtQuick 2.0
import QtQuick.Controls 2.2
import QtQuick.Window 2.2

import Session 1.0
import KioskBridge 1.0

// Floating, frameless, translucent window that rides on top of the
// streaming SDL window during an active session. Shows a subtle
// circular handle that expands into a dropdown with "Exit experience"
// (primary) and a smaller secondary row — Diagnostics | Logs — for
// operator use.
//
// The diagnostics/logs panels open in a SEPARATE child Window so the
// main overlay never resizes during a stream. Resizing an always-on-top
// window above a macOS fullscreen stream can trigger a Space transition
// event that disconnects Moonlight; using a new Tool window avoids this.
Window {
    id: streamOverlayWindow

    property Session session: null
    property int handleSize: 40
    property int menuWidth: 220
    property int menuHeight: 89   // 52 (exit) + 1 (sep) + 36 (secondary row)

    // "" = handle only, "menu" = dropdown, "diagnostics" / "logs" = panel window
    property string displayMode: ""

    // Diagnostics state
    property var  diagnosticsData:    null
    property bool diagnosticsLoading: false

    // Logs state
    property var  logsData:    []
    property bool logsLoading: false

    readonly property int collapsedWidth:  menuWidth
    readonly property int collapsedHeight: handleSize + 8 + menuHeight
    readonly property int collapsedX:      (Screen.width  - collapsedWidth)  / 2
    readonly property int collapsedY:      24

    width:  collapsedWidth
    height: collapsedHeight
    x:      collapsedX
    y:      collapsedY

    flags: Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.Tool | Qt.WindowDoesNotAcceptFocus
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

    onQuittingChanged: {
        if (quitting) {
            streamOverlayWindow.width  = Screen.width
            streamOverlayWindow.height = Screen.height
            streamOverlayWindow.x      = 0
            streamOverlayWindow.y      = 0
        } else {
            streamOverlayWindow.width  = collapsedWidth
            streamOverlayWindow.height = collapsedHeight
            streamOverlayWindow.x      = collapsedX
            streamOverlayWindow.y      = collapsedY
        }
    }

    function runDiagnostics() {
        diagnosticsLoading = true
        diagnosticsData    = null
        var xhr = new XMLHttpRequest()
        xhr.open("GET", "http://127.0.0.1:9740/api/v1/diagnostics", true)
        xhr.onreadystatechange = function() {
            if (xhr.readyState === 4) {
                diagnosticsLoading = false
                if (xhr.status === 200)
                    diagnosticsData = JSON.parse(xhr.responseText)
            }
        }
        xhr.send()
    }

    function fetchLogs() {
        logsLoading = true
        logsData    = []
        var xhr = new XMLHttpRequest()
        xhr.open("GET", "http://127.0.0.1:9740/api/v1/logs", true)
        xhr.onreadystatechange = function() {
            if (xhr.readyState === 4) {
                logsLoading = false
                if (xhr.status === 200) {
                    var resp = JSON.parse(xhr.responseText)
                    if (resp.entries && resp.entries.length > 0)
                        logsData = resp.entries
                    else if (resp.lines)
                        logsData = resp.lines.map(function(l) { return { message: l, timestamp: "" } })
                }
            }
        }
        xhr.send()
    }

    // ─── Quitting veil ──────────────────────────────────────────────────────
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

    // ─── Handle ─────────────────────────────────────────────────────────────
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
                displayMode = (displayMode === "") ? "menu" : ""
            }
        }
    }

    // ─── Dropdown menu ──────────────────────────────────────────────────────
    Rectangle {
        id: menuDropdown
        visible: displayMode === "menu" && !streamOverlayWindow.quitting
        anchors.top: handleButton.bottom
        anchors.topMargin: 8
        anchors.horizontalCenter: parent.horizontalCenter
        width: menuWidth
        height: menuHeight
        color: "#CC000000"
        radius: 10
        border.color: "#FFFFFFFF"
        border.width: 1
        clip: true

        Column {
            anchors.fill: parent
            spacing: 0

            // Exit experience — primary
            Rectangle {
                width: parent.width
                height: 52
                color: exitItemArea.containsMouse ? "#E0000000" : "transparent"

                Text {
                    anchors.centerIn: parent
                    text: qsTr("Exit experience")
                    color: "white"
                    font.pixelSize: 18
                }

                MouseArea {
                    id: exitItemArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    enabled: !streamOverlayWindow.quitting
                    onClicked: {
                        displayMode = ""
                        if (streamOverlayWindow.session !== null) {
                            streamOverlayWindow.quitting = true
                            collapseTimer.restart()
                            streamOverlayWindow.session.triggerExitFromMenu()
                        } else {
                            var xhr = new XMLHttpRequest()
                            xhr.open("POST", "http://127.0.0.1:9740/api/v1/stream/stop", true)
                            xhr.send()
                            streamOverlayWindow.quitting = true
                            collapseTimer.restart()
                        }
                    }
                }
            }

            // Separator
            Rectangle {
                width: parent.width
                height: 1
                color: "#FFFFFFFF"
                opacity: 0.3
            }

            // Secondary row: Diagnostics | Logs
            Row {
                width: parent.width
                height: 36

                Rectangle {
                    width: parent.width / 2
                    height: parent.height
                    color: diagSecArea.containsMouse ? "#30000000" : "transparent"

                    Text {
                        anchors.centerIn: parent
                        text: qsTr("Diagnostics")
                        color: "#a1a1aa"
                        font.pixelSize: 13
                    }

                    MouseArea {
                        id: diagSecArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        enabled: !streamOverlayWindow.quitting
                        onClicked: {
                            displayMode = "diagnostics"
                            runDiagnostics()
                        }
                    }
                }

                Rectangle {
                    width: 1
                    height: parent.height
                    color: "#FFFFFFFF"
                    opacity: 0.3
                }

                Rectangle {
                    width: parent.width / 2 - 1
                    height: parent.height
                    color: logsSecArea.containsMouse ? "#30000000" : "transparent"

                    Text {
                        anchors.centerIn: parent
                        text: qsTr("Logs")
                        color: "#a1a1aa"
                        font.pixelSize: 13
                    }

                    MouseArea {
                        id: logsSecArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        enabled: !streamOverlayWindow.quitting
                        onClicked: {
                            displayMode = "logs"
                            fetchLogs()
                        }
                    }
                }
            }
        }
    }

    // ─── Panel window ────────────────────────────────────────────────────────
    // A separate OS window so the main overlay never resizes during a stream.
    Window {
        id: panelWindow

        readonly property int panelWidth:          420
        readonly property int diagContentHeight:   340
        readonly property int logsContentHeight:   400
        readonly property int panelX:              (Screen.width - panelWidth) / 2
        readonly property int panelY:              streamOverlayWindow.y
        readonly property int contentTopMargin:    streamOverlayWindow.handleSize + 8

        visible: (displayMode === "diagnostics" || displayMode === "logs") &&
                 !streamOverlayWindow.quitting && streamOverlayWindow.visible
        width:  panelWidth
        height: contentTopMargin + (displayMode === "logs" ? logsContentHeight : diagContentHeight)
        x:      panelX
        y:      panelY

        flags: Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.Tool | Qt.WindowDoesNotAcceptFocus
        color: "transparent"

        Component.onCompleted: {
            KioskBridge.makeFollowAllSpaces(panelWindow)
        }

        // ── Diagnostics panel ──────────────────────────────────────────────
        Rectangle {
            visible: displayMode === "diagnostics"
            anchors.top: parent.top
            anchors.topMargin: panelWindow.contentTopMargin
            anchors.horizontalCenter: parent.horizontalCenter
            width: panelWindow.panelWidth
            height: panelWindow.diagContentHeight
            color: "#CC000000"
            radius: 10
            border.color: "#FFFFFFFF"
            border.width: 1
            clip: true

            Column {
                anchors.fill: parent
                anchors.margins: 16
                spacing: 8

                // Header
                Item {
                    width: parent.width
                    height: 28

                    Rectangle {
                        id: backDiagBtn
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        width: backDiagText.implicitWidth + 16
                        height: 26
                        radius: 5
                        color: backDiagArea.containsMouse ? "#3f3f46" : "#27272a"

                        Text {
                            id: backDiagText
                            anchors.centerIn: parent
                            text: "← Back"
                            color: "#a1a1aa"
                            font.pixelSize: 12
                        }

                        MouseArea {
                            id: backDiagArea
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: displayMode = "menu"
                        }
                    }

                    Text {
                        anchors.centerIn: parent
                        text: qsTr("Diagnostics")
                        color: "white"
                        font.pixelSize: 14
                        font.weight: Font.DemiBold
                    }

                    Rectangle {
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        width: rerunText.implicitWidth + 16
                        height: 26
                        radius: 5
                        color: rerunArea.containsMouse ? "#3f3f46" : "#27272a"
                        visible: !diagnosticsLoading

                        Text {
                            id: rerunText
                            anchors.centerIn: parent
                            text: qsTr("Re-run")
                            color: "#a1a1aa"
                            font.pixelSize: 12
                        }

                        MouseArea {
                            id: rerunArea
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: runDiagnostics()
                        }
                    }
                }

                Rectangle {
                    width: parent.width
                    height: 1
                    color: "#FFFFFFFF"
                    opacity: 0.3
                }

                Text {
                    visible: diagnosticsLoading
                    width: parent.width
                    text: qsTr("Running checks…")
                    color: "#a1a1aa"
                    font.pixelSize: 13
                    horizontalAlignment: Text.AlignHCenter
                    topPadding: 12
                }

                Column {
                    visible: !diagnosticsLoading && diagnosticsData !== null
                    width: parent.width
                    spacing: 0

                    Repeater {
                        model: diagnosticsData ? diagnosticsData.checks : []

                        Item {
                            width: parent.width
                            height: 42

                            Rectangle {
                                anchors.fill: parent
                                color: "#10ffffff"
                                visible: checkHover.containsMouse
                                radius: 4
                            }

                            Row {
                                anchors.fill: parent
                                anchors.leftMargin: 4
                                anchors.rightMargin: 4
                                spacing: 10

                                Text {
                                    width: 18
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: modelData.status === "passed" ? "✓" :
                                          modelData.status === "failed" ? "✗" : "−"
                                    color: modelData.status === "passed" ? "#22c55e" :
                                           modelData.status === "failed" ? "#ef4444" : "#71717a"
                                    font.pixelSize: 15
                                }

                                Text {
                                    width: 160
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: modelData.label || ""
                                    color: "white"
                                    font.pixelSize: 13
                                    elide: Text.ElideRight
                                }

                                Text {
                                    width: parent.width - 18 - 160 - 20
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: modelData.detail || ""
                                    color: "#a1a1aa"
                                    font.pixelSize: 12
                                    elide: Text.ElideRight
                                    horizontalAlignment: Text.AlignRight
                                }
                            }

                            MouseArea {
                                id: checkHover
                                anchors.fill: parent
                                hoverEnabled: true
                            }

                            Rectangle {
                                anchors.bottom: parent.bottom
                                width: parent.width
                                height: 1
                                color: "#FFFFFFFF"
                                opacity: 0.07
                            }
                        }
                    }
                }
            }
        }

        // ── Logs panel ─────────────────────────────────────────────────────
        Rectangle {
            visible: displayMode === "logs"
            anchors.top: parent.top
            anchors.topMargin: panelWindow.contentTopMargin
            anchors.horizontalCenter: parent.horizontalCenter
            width: panelWindow.panelWidth
            height: panelWindow.logsContentHeight
            color: "#CC000000"
            radius: 10
            border.color: "#FFFFFFFF"
            border.width: 1
            clip: true

            Column {
                anchors.fill: parent
                anchors.margins: 16
                spacing: 8

                // Header
                Item {
                    width: parent.width
                    height: 28

                    Rectangle {
                        id: backLogsBtn
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        width: backLogsText.implicitWidth + 16
                        height: 26
                        radius: 5
                        color: backLogsArea.containsMouse ? "#3f3f46" : "#27272a"

                        Text {
                            id: backLogsText
                            anchors.centerIn: parent
                            text: "← Back"
                            color: "#a1a1aa"
                            font.pixelSize: 12
                        }

                        MouseArea {
                            id: backLogsArea
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: displayMode = "menu"
                        }
                    }

                    Text {
                        anchors.centerIn: parent
                        text: qsTr("Logs")
                        color: "white"
                        font.pixelSize: 14
                        font.weight: Font.DemiBold
                    }
                }

                Rectangle {
                    width: parent.width
                    height: 1
                    color: "#FFFFFFFF"
                    opacity: 0.3
                }

                Text {
                    visible: logsLoading
                    width: parent.width
                    text: qsTr("Loading…")
                    color: "#a1a1aa"
                    font.pixelSize: 13
                    horizontalAlignment: Text.AlignHCenter
                    topPadding: 12
                }

                Flickable {
                    visible: !logsLoading
                    width: parent.width
                    height: panelWindow.logsContentHeight - 16 - 28 - 8 - 1 - 8 - 16
                    contentHeight: logColumn.implicitHeight
                    clip: true

                    ScrollBar.vertical: ScrollBar {
                        policy: ScrollBar.AsNeeded
                    }

                    Column {
                        id: logColumn
                        width: parent.width
                        spacing: 1

                        Repeater {
                            model: logsData

                            Row {
                                width: parent.width
                                spacing: 8

                                Text {
                                    text: {
                                        var ts = modelData.timestamp || ""
                                        return ts.length >= 19 ? ts.substr(11, 8) : ts.substr(0, 8)
                                    }
                                    color: "#52525b"
                                    font.pixelSize: 11
                                    font.family: "Courier New, Courier, monospace"
                                    width: 58
                                }

                                Text {
                                    width: parent.width - 58 - 8
                                    text: modelData.message || ""
                                    color: "#d4d4d8"
                                    font.pixelSize: 11
                                    font.family: "Courier New, Courier, monospace"
                                    elide: Text.ElideRight
                                    wrapMode: Text.NoWrap
                                }
                            }
                        }
                    }

                    onContentHeightChanged: {
                        if (contentHeight > height)
                            contentY = contentHeight - height
                    }
                }
            }
        }
    }
}
