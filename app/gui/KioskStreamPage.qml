import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Window 2.2

import SdlGamepadKeyNavigation 1.0
import Session 1.0
import SystemProperties 1.0
import VideoItem 1.0

// In-process kiosk stream page (issue #507 M1, Linux scene mode only).
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

    function connectionStarted() {
        // The loading veil drops on the first decoded frame. A missing
        // firstFrameReceived signal or a stalled decoder must not strand
        // the veil forever, so a timer backstop drops it shortly after
        // the connection is up.
        veilFallbackTimer.start()
    }

    function displayLaunchError(text) {
        console.error(text)
    }

    function quitStarting() {
        // The host app is being quit (quitAppAfter is on for the kiosk).
        // Raise the veil again until sessionFinished pops the page.
        veilFallbackTimer.stop()
        firstFrameSeen = false
        stageLabel.text = qsTr("Ending experience")
    }

    function sessionFinished(portTestResult) {
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
        session.displayLaunchError.connect(displayLaunchError)
        session.quitStarting.connect(quitStarting)
        session.sessionFinished.connect(sessionFinished)
        session.readyForDeletion.connect(sessionReadyForDeletion)

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

    // TEMPORARY M1 exit control. M4 replaces this with the in-scene
    // 3-dot handle and "Exit experience" menu.
    Rectangle {
        id: exitButton
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.margins: 24
        width: exitLabel.implicitWidth + 32
        height: 40
        radius: 8
        color: "#33000000"
        border.color: "#55ffffff"
        border.width: 1
        z: 10

        Text {
            id: exitLabel
            anchors.centerIn: parent
            text: qsTr("Exit")
            color: "#ffffff"
            font.pixelSize: 16
        }

        MouseArea {
            anchors.fill: parent
            onClicked: {
                if (session !== null) {
                    session.stopSession()
                }
            }
        }
    }
}
