import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Controls.Material 2.2
import QtQuick.Layouts 1.3

import AppModel 1.0
import ComputerManager 1.0
import SdlGamepadKeyNavigation 1.0

Item {
    id: kioskRoot

    property AppModel appModel: null
    property bool hostConnected: false

    StackView.onActivated: {
        toolBar.visible = false

        if (!kioskLauncher.isExecuted()) {
            kioskLauncher.searchingComputer.connect(onSearchingComputer)
            kioskLauncher.computerReady.connect(onComputerReady)
            kioskLauncher.failed.connect(onFailed)
            kioskLauncher.execute(ComputerManager)
        }
    }

    function onSearchingComputer() {
        statusText.text = qsTr("Connecting...")
    }

    function onComputerReady(computerIndex) {
        hostConnected = true
        statusText.text = ""

        appModel = Qt.createQmlObject('import AppModel 1.0; AppModel {}', kioskRoot, '')
        appModel.initialize(ComputerManager, computerIndex, false)
        appModel.computerLost.connect(onComputerLost)
        appGrid.model = appModel
    }

    function onFailed(message) {
        statusText.text = message
        statusText.color = "#f87171"
    }

    function onComputerLost() {
        hostConnected = false
        statusText.text = qsTr("Connection lost. Reconnecting...")
        statusText.color = "#a1a1aa"
        // Re-execute the launcher to reconnect
        kioskLauncher.execute(ComputerManager)
    }

    // Background
    Rectangle {
        anchors.fill: parent
        color: "#0a0a0a"
    }

    // Header
    Rectangle {
        id: header
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 80
        color: "#0a0a0a"

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 32
            anchors.rightMargin: 32
            spacing: 16

            // Brand
            Text {
                text: "Hydra ExperienceNet"
                color: "#ffffff"
                font.pixelSize: 24
                font.weight: Font.DemiBold
                font.family: "system-ui"
            }

            Item { Layout.fillWidth: true }

            // District badge
            Rectangle {
                visible: typeof kioskDistrict !== "undefined" && kioskDistrict !== ""
                color: "#18181b"
                border.color: "#27272a"
                border.width: 1
                radius: 8
                implicitWidth: districtLabel.implicitWidth + 24
                implicitHeight: 36

                Text {
                    id: districtLabel
                    anchors.centerIn: parent
                    text: typeof kioskDistrict !== "undefined" ? kioskDistrict : ""
                    color: "#d4d4d8"
                    font.pixelSize: 14
                    font.family: "system-ui"
                }
            }

            // Venue badge
            Rectangle {
                visible: typeof kioskVenue !== "undefined" && kioskVenue !== ""
                color: "#18181b"
                border.color: "#27272a"
                border.width: 1
                radius: 8
                implicitWidth: venueLabel.implicitWidth + 24
                implicitHeight: 36

                Text {
                    id: venueLabel
                    anchors.centerIn: parent
                    text: typeof kioskVenue !== "undefined" ? kioskVenue : ""
                    color: "#d4d4d8"
                    font.pixelSize: 14
                    font.family: "system-ui"
                }
            }
        }

        // Bottom border
        Rectangle {
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            height: 1
            color: "#27272a"
        }
    }

    // Loading / status area
    Column {
        anchors.centerIn: parent
        spacing: 16
        visible: !hostConnected

        BusyIndicator {
            anchors.horizontalCenter: parent.horizontalCenter
            running: visible && statusText.color !== "#f87171"
            Material.accent: "#6366f1"
        }

        Text {
            id: statusText
            anchors.horizontalCenter: parent.horizontalCenter
            text: qsTr("Connecting...")
            color: "#a1a1aa"
            font.pixelSize: 18
            font.weight: Font.Medium
            font.family: "system-ui"
        }
    }

    // App grid
    CenteredGridView {
        id: appGrid
        visible: hostConnected
        anchors.top: header.bottom
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.topMargin: 24
        anchors.bottomMargin: 8

        focus: true
        cellWidth: 230; cellHeight: 297

        Component.onCompleted: {
            currentIndex = -1
        }

        delegate: NavigableItemDelegate {
            width: 220; height: 287
            grid: appGrid

            Image {
                property bool isPlaceholder: false

                id: appIcon
                anchors.horizontalCenter: parent.horizontalCenter
                y: 10
                source: model.boxart

                onSourceSizeChanged: {
                    if (!model.isAppCollectorGame &&
                        ((sourceSize.width === 130 && sourceSize.height === 180) ||
                         (sourceSize.width === 628 && sourceSize.height === 888) ||
                         (sourceSize.width === 200 && sourceSize.height === 266)))
                    {
                        isPlaceholder = true
                    } else {
                        isPlaceholder = false
                    }
                    width = 200
                    height = 267
                }

                ToolTip.text: model.name
                ToolTip.delay: 1000
                ToolTip.timeout: 5000
                ToolTip.visible: (parent.hovered || parent.highlighted) && appNameText.truncated
            }

            // Show running app overlay (resume/quit buttons)
            Loader {
                active: model.running
                asynchronous: true
                anchors.fill: appIcon

                sourceComponent: Item {
                    RoundButton {
                        focusPolicy: Qt.NoFocus
                        anchors.centerIn: parent
                        anchors.verticalCenterOffset: -60
                        implicitWidth: 85
                        implicitHeight: 85
                        icon.source: "qrc:/res/play_arrow_FILL1_wght700_GRAD200_opsz48.svg"
                        icon.width: 75
                        icon.height: 75
                        onClicked: launchOrResumeSelectedApp(true)
                        ToolTip.text: qsTr("Resume Game")
                        ToolTip.delay: 1000
                        ToolTip.timeout: 3000
                        ToolTip.visible: hovered
                        Material.background: "#D0808080"
                    }
                }
            }

            // App name label (shown when box art is a placeholder)
            Loader {
                active: appIcon.isPlaceholder
                width: appIcon.width
                height: model.running ? 175 : appIcon.height
                anchors.left: appIcon.left
                anchors.right: appIcon.right
                anchors.bottom: appIcon.bottom

                sourceComponent: Label {
                    id: appNameText
                    text: model.name
                    font.pointSize: 22
                    leftPadding: 20
                    rightPadding: 20
                    verticalAlignment: Text.AlignVCenter
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    elide: Text.ElideRight
                }
            }

            function launchOrResumeSelectedApp(quitExistingApp)
            {
                var runningId = appModel.getRunningAppId()
                if (runningId !== 0 && runningId !== model.appid) {
                    if (quitExistingApp) {
                        quitAppDialog.appName = appModel.getRunningAppName()
                        quitAppDialog.nextAppName = model.name
                        quitAppDialog.nextAppIndex = index
                        quitAppDialog.open()
                    }
                    return
                }

                var component = Qt.createComponent("StreamSegue.qml")
                var segue = component.createObject(stackView, {
                    "appName": model.name,
                    "session": appModel.createSessionForApp(index),
                    "isResume": runningId === model.appid,
                    "quitAfter": false
                })
                stackView.push(segue)
            }

            onClicked: {
                if (!model.running) {
                    launchOrResumeSelectedApp(true)
                }
            }

            Keys.onReturnPressed: {
                if (!model.running) {
                    launchOrResumeSelectedApp(true)
                }
            }

            Keys.onEnterPressed: {
                if (!model.running) {
                    launchOrResumeSelectedApp(true)
                }
            }
        }

        // Empty state
        Row {
            anchors.centerIn: parent
            spacing: 5
            visible: appGrid.count === 0 && hostConnected

            Label {
                text: qsTr("No experiences available")
                font.pointSize: 20
                color: "#a1a1aa"
                verticalAlignment: Text.AlignVCenter
                wrapMode: Text.Wrap
            }
        }

        ScrollBar.vertical: ScrollBar {}
    }

    // Quit app confirmation dialog
    NavigableMessageDialog {
        id: quitAppDialog
        property string appName: ""
        property string nextAppName: ""
        property int nextAppIndex: 0
        text: qsTr("Are you sure you want to quit %1? Any unsaved progress will be lost.").arg(appName)
        standardButtons: Dialog.Yes | Dialog.No

        function quitApp() {
            var component = Qt.createComponent("QuitSegue.qml")
            var params = {
                "appName": appName,
                "quitRunningAppFn": function() { appModel.quitRunningApp() },
                "nextAppName": nextAppName,
                "nextSession": appModel.createSessionForApp(nextAppIndex)
            }
            stackView.push(component.createObject(stackView, params))
        }

        onAccepted: quitApp()
    }
}
