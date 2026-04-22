import QtQuick 2.0
import QtQuick.Controls 2.2
import QtQuick.Controls.Material 2.2
import QtQuick.Window 2.2

import ComputerManager 1.0

Item {
    id: cliStartRoot

    function onSearchingComputer() {
        stageLabel.text = qsTr("Connecting")
    }

    function onSearchingApp() {
        stageLabel.text = qsTr("Loading experience")
    }

    function onSessionCreated(appName, session) {
        var component = Qt.createComponent("StreamSegue.qml")
        var segue = component.createObject(stackView, {
            "appName": appName,
            "session": session,
            "quitAfter": true
        })
        stackView.push(segue)
    }

    function onLaunchFailed(message) {
        errorDialog.text = message
        errorDialog.open()
        console.error(message)
    }

    function onAppQuitRequired(appName) {
        quitAppDialog.appName = appName
        quitAppDialog.open()
    }

    StackView.onActivated: {
        if (!launcher.isExecuted()) {
            toolBar.visible = false

            // Fill the display with a dark loading screen while we
            // reach out to Sunshine and bring up the decoder. Matches
            // the kiosk's fullscreen chrome so there's no small grey
            // window flash between tile-tap and first frame.
            if (typeof window !== "undefined" && window !== null) {
                window.showFullScreen()
            }

            launcher.searchingComputer.connect(onSearchingComputer)
            launcher.searchingApp.connect(onSearchingApp)
            launcher.sessionCreated.connect(onSessionCreated)
            launcher.failed.connect(onLaunchFailed)
            launcher.appQuitRequired.connect(onAppQuitRequired)
            launcher.execute(ComputerManager)
        }
    }

    // Full-bleed dark backdrop so we never show the default grey Qt
    // window fill between kiosk-tap and the first video frame.
    Rectangle {
        anchors.fill: parent
        color: "#0a0a0a"
    }

    Column {
        anchors.centerIn: parent
        spacing: 20

        BusyIndicator {
            id: stageSpinner
            anchors.horizontalCenter: parent.horizontalCenter
            running: visible
            Material.accent: "#6366f1"
        }

        Label {
            id: stageLabel
            anchors.horizontalCenter: parent.horizontalCenter
            text: qsTr("Loading experience")
            font.pointSize: 20
            font.weight: Font.Medium
            color: "#ffffff"
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
        }
    }

    ErrorMessageDialog {
        id: errorDialog

        onClosed: {
            Qt.quit();
        }
    }

    NavigableMessageDialog {
        id: quitAppDialog
        text:qsTr("Are you sure you want to quit %1? Any unsaved progress will be lost.").arg(appName)
        standardButtons: Dialog.Yes | Dialog.No
        property string appName : ""

        function quitApp() {
            var component = Qt.createComponent("QuitSegue.qml")
            var params = {"appName": appName, "quitRunningAppFn": function() { launcher.quitRunningApp() }}
            stackView.push(component.createObject(stackView, params))
        }

        onAccepted: quitApp()
        onRejected: Qt.quit()
    }
}
