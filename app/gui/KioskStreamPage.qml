import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Window 2.2

import SdlGamepadKeyNavigation 1.0
import Session 1.0
import SystemProperties 1.0
import VideoItem 1.0

// In-process kiosk stream page (issue #507, Linux scene mode only).
// The decoded video renders inside this page through VideoItem, so the
// kiosk keeps ONE OS window for both the grid and the stream. This page
// is only reachable behind the Qt.platform.os === "linux" gate in
// KioskView.qml; it receives its Session the same way StreamSegue.qml
// does: as a property set at push time.
Item {
    id: kioskStreamPage

    property Session session
    property string appName
    property bool firstFrameSeen: false
    property bool popped: false

    // Issue #507 M4 state reporting. The page pushes its lifecycle to the
    // agent: "connecting" was already posted by KioskView at launcher
    // start; this page posts "streaming" on connectionStarted, "finished"
    // on sessionFinished, "error" on stageFailed or displayLaunchError,
    // and re-asserts the current state every 15 seconds while active so a
    // restarted agent re-learns the truth. All posts are fire-and-forget
    // with no UI coupling.
    property string streamState: "connecting"
    property bool terminalStatePosted: false

    function postStreamState(state, detail) {
        var xhr = new XMLHttpRequest()
        xhr.open("POST", "http://127.0.0.1:9740/api/v1/stream/state")
        xhr.setRequestHeader("Content-Type", "application/json")
        var body = { "state": state }
        if (detail !== undefined && detail !== "") {
            body["detail"] = detail
        }
        xhr.send(JSON.stringify(body))
    }

    function transitionStreamState(state, detail) {
        if (terminalStatePosted) {
            return
        }
        streamState = state
        if (state === "finished" || state === "error") {
            terminalStatePosted = true
            reassertTimer.stop()
        }
        postStreamState(state, detail)
    }

    function connectionStarted() {
        // The loading veil drops on the first decoded frame. A missing
        // firstFrameReceived signal or a stalled decoder must not strand
        // the veil forever, so a timer backstop drops it shortly after
        // the connection is up.
        veilFallbackTimer.start()

        transitionStreamState("streaming")

        // Input goes live with the connection (issue #507 M3): the
        // VideoItem takes keyboard focus and starts forwarding pointer
        // and key events to the host. The local cursor stays visible per
        // the M3 cursor policy.
        videoItem.streamActive = true
        videoItem.forceActiveFocus()
    }

    function stageFailed(stage, errorCode, failingPorts) {
        transitionStreamState("error",
                              "stage failed: " + stage + " (error " + errorCode + ")")
    }

    function displayLaunchError(text) {
        console.error(text)
        transitionStreamState("error", text)
    }

    function quitStarting() {
        // The host app is being quit (quitAppAfter is on for the kiosk).
        // Raise the veil again until sessionFinished pops the page.
        // Dropping streamActive raises all keys, cancels active pointers
        // and restores the local cursor.
        videoItem.streamActive = false
        veilFallbackTimer.stop()
        firstFrameSeen = false
        stageLabel.text = qsTr("Ending experience")
    }

    function sessionFinished(portTestResult) {
        // Stream input ends with the session
        videoItem.streamActive = false

        transitionStreamState("finished")

        // Re-enable GUI gamepad usage now
        SdlGamepadKeyNavigation.enable()

        if (!popped) {
            popped = true
            stackView.pop()
        }
    }

    function sessionReadyForDeletion() {
        // Garbage collect the Session object since it's pretty heavyweight
        // and keeps other libraries around until it is deleted.
        session = null
        gc()
    }

    StackView.onDeactivating: {
        // Release stream input and keyboard focus, and restore the local
        // cursor, before the page leaves the stack
        videoItem.streamActive = false
        videoItem.focus = false

        reassertTimer.stop()

        // Show the toolbar again when popped off the stack
        toolBar.visible = true

        // Re-enable GUI gamepad usage now
        SdlGamepadKeyNavigation.enable()
    }

    StackView.onActivated: {
        // Hide the toolbar while streaming
        toolBar.visible = false

        // Hook up our signals
        session.connectionStarted.connect(connectionStarted)
        session.stageFailed.connect(stageFailed)
        session.displayLaunchError.connect(displayLaunchError)
        session.quitStarting.connect(quitStarting)
        session.sessionFinished.connect(sessionFinished)
        session.readyForDeletion.connect(sessionReadyForDeletion)

        // Agent-restart self-healing: re-assert the current state every
        // 15 seconds while this page is active.
        reassertTimer.start()

        // Ensure the SystemProperties async thread is finished,
        // since it may currently be using the SDL video subsystem
        SystemProperties.waitForAsyncLoad()

        // Stop GUI gamepad usage while the stream is active
        SdlGamepadKeyNavigation.disable()

        // Scene mode must be set before initialize() and start() so
        // exec() takes the non-blocking branch, creates no SDL window,
        // and leaves the Qt event loop (and this scene) running.
        session.setSceneMode(true)

        // Initialize the session and probe for host/client capabilities
        if (!session.initialize(window)) {
            sessionFinished(0)
            sessionReadyForDeletion()
            return
        }

        // Garbage collect QML stuff before we start streaming. In scene
        // mode session.start() returns immediately; the stream then runs
        // until stopSession() or a connection termination.
        gc()
        session.start()
    }

    Timer {
        id: reassertTimer
        interval: 15000
        repeat: true
        onTriggered: postStreamState(kioskStreamPage.streamState)
    }

    // Dark backdrop behind the video so letterboxing and the loading
    // phase look intentional instead of showing the default window fill.
    Rectangle {
        anchors.fill: parent
        color: "#0a0a0a"
        z: -1
    }

    VideoItem {
        id: videoItem
        anchors.fill: parent

        // Ctrl+Alt+Shift+Q parity with the SDL path's quit combo
        onQuitRequested: {
            if (session !== null) {
                session.stopSession()
            }
        }

        Component.onCompleted: {
            // M1 contract: VideoItem may expose a firstFrameReceived()
            // signal. Connect defensively so this page also works with a
            // VideoItem build that does not have the signal yet; the
            // timer backstop drops the veil in that case.
            if (videoItem.firstFrameReceived !== undefined) {
                videoItem.firstFrameReceived.connect(function() {
                    kioskStreamPage.firstFrameSeen = true
                })
            }
        }
    }

    Timer {
        id: veilFallbackTimer
        interval: 2000
        onTriggered: kioskStreamPage.firstFrameSeen = true
    }

    // Loading veil: covers the video until the first frame arrives and
    // again while the host app is quitting on the way out.
    Rectangle {
        anchors.fill: parent
        color: "#0a0a0a"
        visible: !firstFrameSeen
        z: 5

        Column {
            anchors.centerIn: parent
            spacing: 20

            BusyIndicator {
                anchors.horizontalCenter: parent.horizontalCenter
                running: visible
            }

            Label {
                id: stageLabel
                anchors.horizontalCenter: parent.horizontalCenter
                text: qsTr("Loading experience")
                color: "#ffffff"
                font.pointSize: 20
                font.weight: Font.Medium
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
            }
        }
    }

    // In-scene exit overlay (issue #507 M4). Ports the visual language of
    // StreamOverlay.qml (the subtle circular handle top-center and the
    // "Exit experience" dropdown) into plain Items above the VideoItem.
    // The handle is always faintly visible; tap or hover opens the
    // dropdown; the dropdown auto-hides after 5 seconds without
    // interaction. Only the handle and dropdown rectangles hit-test, so
    // stream input everywhere else is untouched.
    Item {
        id: exitOverlay
        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.topMargin: 24
        width: menuWidth
        height: handleSize + 8 + menuHeight
        z: 10

        property int handleSize: 40
        property int menuHeight: 52
        property int menuWidth: 220
        property bool dropdownOpen: false

        function touch() {
            dropdownAutoHideTimer.restart()
        }

        function openDropdown() {
            dropdownOpen = true
            touch()
        }

        function closeDropdown() {
            dropdownOpen = false
            dropdownAutoHideTimer.stop()
        }

        Timer {
            id: dropdownAutoHideTimer
            interval: 5000
            onTriggered: exitOverlay.closeDropdown()
        }

        Rectangle {
            id: handleButton
            anchors.top: parent.top
            anchors.horizontalCenter: parent.horizontalCenter
            width: exitOverlay.handleSize
            height: exitOverlay.handleSize
            radius: exitOverlay.handleSize / 2
            color: handleArea.containsMouse ? "#B0000000" : "#80000000"
            border.color: "#FFFFFFFF"
            border.width: 1

            // Faint at rest so the handle never fights the experience for
            // attention, fully visible on approach or while the menu is
            // open.
            opacity: (handleArea.containsMouse || exitOverlay.dropdownOpen) ? 1.0 : 0.4

            Behavior on opacity {
                NumberAnimation { duration: 150 }
            }

            Text {
                anchors.centerIn: parent
                text: "⋯"
                color: "white"
                font.pixelSize: exitOverlay.handleSize * 0.55
                font.bold: true
            }

            MouseArea {
                id: handleArea
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                // Both tap and hover OPEN the dropdown (a tap synthesizes
                // a hover enter first, so a click toggle would open and
                // immediately close it again on touch heads). Closing is
                // the auto-hide timer or the Exit action.
                onEntered: exitOverlay.openDropdown()
                onClicked: exitOverlay.openDropdown()
            }
        }

        Rectangle {
            id: exitDropdown
            visible: exitOverlay.dropdownOpen
            anchors.top: handleButton.bottom
            anchors.topMargin: 8
            anchors.horizontalCenter: parent.horizontalCenter
            width: exitOverlay.menuWidth
            height: exitOverlay.menuHeight
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
                onEntered: exitOverlay.touch()
                onPositionChanged: exitOverlay.touch()
                onClicked: {
                    exitOverlay.closeDropdown()
                    if (session !== null) {
                        session.stopSession()
                    }
                }
            }
        }
    }
}
