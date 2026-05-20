import QtQuick 2.0
import QtQuick.Controls 2.2
import QtQuick.Window 2.2

import Session 1.0
import KioskBridge 1.0

// Floating, frameless, translucent window that rides on top of the
// streaming SDL window during an active session. Shows a subtle
// circular handle that expands into a dropdown with "Exit experience"
// (primary) and two operator tools — Diagnostics and Logs — in a
// smaller secondary row below.
//
// Tapping "Diagnostics" or "Logs" expands the window into a panel that
// calls the hydraheadflatscreen local API (port 9740) and renders the
// results inline. A ← Back button returns to the menu.
Window {
    id: streamOverlayWindow

    property Session session: null
    property int handleSize: 40
    property int menuWidth: 220
    property int menuHeight: 89   // 52 (exit) + 1 (separator) + 36 (secondary row)
    property int panelWidth: 420
    property int diagPanelHeight: 340
    property int logsPanelHeight: 400

    // "" = handle only, "menu" = dropdown open, "diagnostics" / "logs" = panel
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

    // Resize window when display mode changes; quitting overrides separately.
    onDisplayModeChanged: {
        if (displayMode === "diagnostics") {
            streamOverlayWindow.width  = panelWidth
            streamOverlayWindow.height = handleSize + 8 + diagPanelHeight
            streamOverlayWindow.x      = (Screen.width - panelWidth) / 2
        } else if (displayMode === "logs") {
            streamOverlayWindow.width  = panelWidth
            streamOverlayWindow.height = handleSize + 8 + logsPanelHeight
            streamOverlayWindow.x      = (Screen.width - panelWidth) / 2
        } else {
            streamOverlayWindow.width  = collapsedWidth
            streamOverlayWindow.height = collapsedHeight
            streamOverlayWindow.x      = collapsedX
        }
    }

    // Timer safeguard: collapse window back after exit transition.
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

    // Fetch diagnostics from the local agent API.
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

    // Fetch log entries from the local agent API.
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

    // Circular ⋯ handle button at the top of the window.
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
    // Shown when displayMode === "menu". Primary item: Exit experience.
    // Secondary row: Diagnostics | Logs (smaller, muted).
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

            // Exit experience — primary, full-width
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

            // Secondary row: Diagnostics | divider | Logs
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

    // ─── Diagnostics panel ──────────────────────────────────────────────────
    Rectangle {
        id: diagnosticsPanel
        visible: displayMode === "diagnostics" && !streamOverlayWindow.quitting
        anchors.top: handleButton.bottom
        anchors.topMargin: 8
        anchors.horizontalCenter: parent.horizontalCenter
        width: panelWidth
        height: diagPanelHeight
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
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    width: backDiagArea.implicitWidth + 16
                    implicitWidth: backDiagText.implicitWidth + 16
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
                    width: rerunArea.implicitWidth + 16
                    implicitWidth: rerunText.implicitWidth + 16
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

            // Separator
            Rectangle {
                width: parent.width
                height: 1
                color: "#FFFFFFFF"
                opacity: 0.3
            }

            // Loading indicator
            Text {
                visible: diagnosticsLoading
                width: parent.width
                text: qsTr("Running checks…")
                color: "#a1a1aa"
                font.pixelSize: 13
                horizontalAlignment: Text.AlignHCenter
                topPadding: 16
            }

            // Check rows
            Column {
                visible: !diagnosticsLoading && diagnosticsData !== null
                width: parent.width
                spacing: 0

                Repeater {
                    model: diagnosticsData ? diagnosticsData.checks : []

                    Item {
                        width: parent.width
                        height: 44

                        // Subtle hover highlight
                        Rectangle {
                            anchors.fill: parent
                            color: "#10ffffff"
                            visible: checkRowArea.containsMouse
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
                            id: checkRowArea
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

    // ─── Logs panel ─────────────────────────────────────────────────────────
    Rectangle {
        id: logsPanel
        visible: displayMode === "logs" && !streamOverlayWindow.quitting
        anchors.top: handleButton.bottom
        anchors.topMargin: 8
        anchors.horizontalCenter: parent.horizontalCenter
        width: panelWidth
        height: logsPanelHeight
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
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    width: backLogsArea.implicitWidth + 16
                    implicitWidth: backLogsText.implicitWidth + 16
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

            // Separator
            Rectangle {
                width: parent.width
                height: 1
                color: "#FFFFFFFF"
                opacity: 0.3
            }

            // Loading indicator
            Text {
                visible: logsLoading
                width: parent.width
                text: qsTr("Loading…")
                color: "#a1a1aa"
                font.pixelSize: 13
                horizontalAlignment: Text.AlignHCenter
                topPadding: 16
            }

            // Scrollable log entries
            Flickable {
                visible: !logsLoading
                width: parent.width
                height: logsPanelHeight - 16 - 28 - 8 - 1 - 8 - 16  // panel - margins - header - spacing - sep - spacing - bottom margin
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
