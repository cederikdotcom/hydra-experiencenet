import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Controls.Material 2.2
import QtQuick.Layouts 1.3
import QtQuick.Window 2.2

Item {
    id: kioskRoot

    property var experiences: []
    property string configDistrict: ""
    property string configVenue: ""
    property bool loading: true
    property bool streaming: false
    property string streamingExperience: ""
    property string errorMessage: ""
    property var statusPollTimer: null

    StackView.onActivated: {
        toolBar.visible = false

        // Maximize the kiosk window. We specifically AVOID
        // window.showFullScreen() on macOS because that creates a
        // separate macOS Space, and the floating Qt overlay window
        // that holds our ⋯ exit handle is stuck on the original
        // Space and would disappear. The macOS menu bar and dock
        // are auto-hidden by enableKioskPresentation() in main.cpp
        // (kiosk mode), and we drop the window's title bar + traffic
        // lights by setting FramelessWindowHint so the end result is
        // edge-to-edge kiosk chrome without losing the overlay.
        if (typeof window !== "undefined" && window !== null) {
            // Go into real macOS fullscreen (a new Space) now that the
            // floating exit overlay window has been marked with
            // NSWindowCollectionBehaviorCanJoinAllSpaces, so it still
            // sits above the kiosk when we enter that Space. This is
            // the only reliable way to cover every pixel of the
            // display (menu bar area included) without wrestling with
            // macOS work-area math.
            window.showFullScreen()
        }

        // Bring up the floating exit overlay so the handle and dropdown
        // are available on the experience library too, not only during a
        // stream. StreamOverlay.qml tolerates a null session (tap becomes
        // a no-op) so it's safe to show here.
        if (kioskOverlayLoader.item === null) {
            kioskOverlayLoader.active = true
        }
        if (kioskOverlayLoader.item !== null) {
            kioskOverlayLoader.item.visible = true
        }

        fetchConfig()
    }

    StackView.onDeactivating: {
        if (kioskOverlayLoader.item !== null) {
            kioskOverlayLoader.item.visible = false
        }
    }

    Loader {
        id: kioskOverlayLoader
        active: false
        source: "qrc:/gui/StreamOverlay.qml"
    }

    // HTTP helper: GET
    function fetchData(url, callback) {
        var xhr = new XMLHttpRequest()
        xhr.onreadystatechange = function() {
            if (xhr.readyState === XMLHttpRequest.DONE && xhr.status === 200) {
                callback(JSON.parse(xhr.responseText))
            }
        }
        xhr.open("GET", url)
        xhr.send()
    }

    // HTTP helper: POST
    function postData(url, body, callback) {
        var xhr = new XMLHttpRequest()
        xhr.onreadystatechange = function() {
            if (xhr.readyState === XMLHttpRequest.DONE) {
                callback(JSON.parse(xhr.responseText))
            }
        }
        xhr.open("POST", url)
        xhr.setRequestHeader("Content-Type", "application/json")
        xhr.send(JSON.stringify(body))
    }

    function fetchConfig() {
        fetchData("http://localhost:9740/api/v1/config", function(data) {
            if (data.district) configDistrict = data.district
            if (data.venue) configVenue = data.venue
            fetchExperiences()
        })
    }

    function fetchExperiences() {
        loading = true
        errorMessage = ""
        fetchData("http://localhost:9740/api/v1/experiences", function(data) {
            if (data && data.length !== undefined) {
                experiences = data
            } else {
                experiences = []
            }
            loading = false
        })
    }

    function startStream(experienceName) {
        streaming = true
        streamingExperience = experienceName
        errorMessage = ""

        postData("http://localhost:9740/api/v1/stream/start",
                 { "experience": experienceName },
                 function(data) {
            if (data.error) {
                streaming = false
                streamingExperience = ""
                errorMessage = data.error
                return
            }
            // Start polling stream status
            pollStreamStatus()
        })
    }

    function pollStreamStatus() {
        if (statusPollTimer) {
            statusPollTimer.destroy()
        }
        statusPollTimer = Qt.createQmlObject(
            'import QtQuick 2.9; Timer { interval: 2000; repeat: true; running: true }',
            kioskRoot, 'statusPollTimer')
        statusPollTimer.triggered.connect(function() {
            fetchData("http://localhost:9740/api/v1/stream/status", function(data) {
                if (data.status === "ready") {
                    // Agent launches moonlight as separate process.
                    // Kiosk stays behind it. Stop polling.
                    if (statusPollTimer) {
                        statusPollTimer.running = false
                        statusPollTimer.destroy()
                        statusPollTimer = null
                    }
                    streaming = false
                    streamingExperience = ""
                    // Refresh experience list when moonlight exits
                    fetchExperiences()
                } else if (data.status === "error") {
                    if (statusPollTimer) {
                        statusPollTimer.running = false
                        statusPollTimer.destroy()
                        statusPollTimer = null
                    }
                    streaming = false
                    streamingExperience = ""
                    errorMessage = data.message || "Stream failed"
                }
                // Otherwise keep polling (status is "starting" or similar)
            })
        })
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
                font.family: ""
            }

            Item { Layout.fillWidth: true }

            // District badge
            Rectangle {
                visible: {
                    var d = configDistrict !== "" ? configDistrict :
                            (typeof kioskDistrict !== "undefined" ? kioskDistrict : "")
                    return d !== ""
                }
                color: "#18181b"
                border.color: "#27272a"
                border.width: 1
                radius: 8
                implicitWidth: districtLabel.implicitWidth + 24
                implicitHeight: 36

                Text {
                    id: districtLabel
                    anchors.centerIn: parent
                    text: configDistrict !== "" ? configDistrict :
                          (typeof kioskDistrict !== "undefined" ? kioskDistrict : "")
                    color: "#d4d4d8"
                    font.pixelSize: 14
                    font.family: ""
                }
            }

            // Venue badge
            Rectangle {
                visible: {
                    var v = configVenue !== "" ? configVenue :
                            (typeof kioskVenue !== "undefined" ? kioskVenue : "")
                    return v !== ""
                }
                color: "#18181b"
                border.color: "#27272a"
                border.width: 1
                radius: 8
                implicitWidth: venueLabel.implicitWidth + 24
                implicitHeight: 36

                Text {
                    id: venueLabel
                    anchors.centerIn: parent
                    text: configVenue !== "" ? configVenue :
                          (typeof kioskVenue !== "undefined" ? kioskVenue : "")
                    color: "#d4d4d8"
                    font.pixelSize: 14
                    font.family: ""
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

    // Loading state
    Column {
        anchors.centerIn: parent
        spacing: 16
        visible: loading && !streaming

        BusyIndicator {
            anchors.horizontalCenter: parent.horizontalCenter
            running: visible
            Material.accent: "#6366f1"
        }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: qsTr("Loading experiences...")
            color: "#a1a1aa"
            font.pixelSize: 18
            font.weight: Font.Medium
            font.family: ""
        }
    }

    // Streaming overlay
    Column {
        anchors.centerIn: parent
        spacing: 16
        visible: streaming

        BusyIndicator {
            anchors.horizontalCenter: parent.horizontalCenter
            running: visible
            Material.accent: "#6366f1"
        }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: qsTr("Starting %1...").arg(streamingExperience)
            color: "#ffffff"
            font.pixelSize: 20
            font.weight: Font.Medium
            font.family: ""
        }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: qsTr("Preparing stream")
            color: "#a1a1aa"
            font.pixelSize: 14
            font.family: ""
        }
    }

    // Error message
    Column {
        anchors.centerIn: parent
        spacing: 16
        visible: errorMessage !== "" && !loading && !streaming

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: errorMessage
            color: "#f87171"
            font.pixelSize: 18
            font.weight: Font.Medium
            font.family: ""
        }

        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            width: retryText.implicitWidth + 32
            height: 40
            radius: 8
            color: "#6366f1"

            Text {
                id: retryText
                anchors.centerIn: parent
                text: qsTr("Retry")
                color: "#ffffff"
                font.pixelSize: 14
                font.weight: Font.Medium
                font.family: ""
            }

            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: {
                    errorMessage = ""
                    fetchExperiences()
                }
            }
        }
    }

    // Experience grid
    GridView {
        id: experienceGrid
        visible: !loading && !streaming && errorMessage === ""
        anchors.top: header.bottom
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.topMargin: 32
        anchors.bottomMargin: 16
        anchors.leftMargin: 32
        anchors.rightMargin: 32

        cellWidth: 230
        cellHeight: 160
        focus: true

        model: experiences.length

        delegate: Item {
            width: 220
            height: 150

            Rectangle {
                id: tileBackground
                anchors.fill: parent
                radius: 12
                color: "#18181b"
                border.color: tileMouseArea.containsMouse ? "#6366f1" : "#27272a"
                border.width: 1

                Behavior on border.color {
                    ColorAnimation { duration: 150 }
                }

                Text {
                    anchors.centerIn: parent
                    anchors.leftMargin: 16
                    anchors.rightMargin: 16
                    width: parent.width - 32
                    text: experiences[index].name || ""
                    color: "#ffffff"
                    font.pixelSize: 18
                    font.weight: Font.DemiBold
                    font.family: ""
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    elide: Text.ElideRight
                    maximumLineCount: 3
                }

                MouseArea {
                    id: tileMouseArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        startStream(experiences[index].name)
                    }
                }
            }
        }

        // Empty state
        Text {
            anchors.centerIn: parent
            visible: experienceGrid.count === 0
            text: qsTr("No experiences available")
            color: "#a1a1aa"
            font.pixelSize: 20
            font.family: ""
        }

        ScrollBar.vertical: ScrollBar {}
    }
}
