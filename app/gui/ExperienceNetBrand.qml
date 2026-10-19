import QtQuick 2.9

// Packaged Samsung receiver artwork, also available before the network is up.
Column {
    property real badgeSize: 144
    width: badgeSize * 2
    spacing: badgeSize * 1.4 / 18

    Rectangle {
        anchors.horizontalCenter: parent.horizontalCenter
        width: badgeSize
        height: badgeSize
        radius: badgeSize / 2
        color: "white"

        Image {
            anchors.fill: parent
            anchors.margins: badgeSize * 2.2 / 18
            source: "qrc:/res/experiencenet-mark.png"
            fillMode: Image.PreserveAspectFit
            smooth: true
        }
    }

    Text {
        anchors.horizontalCenter: parent.horizontalCenter
        text: "experiencenet"
        color: "white"
        font.pixelSize: badgeSize * 2.6 / 18
        font.letterSpacing: badgeSize * 0.35 / 18
    }
}
