import QtQuick 2.9

Rectangle {
    id: tile
    property string label: ""
    property url artwork: ""
    signal clicked()
    readonly property bool hasArtwork: picture.status === Image.Ready

    radius: 14
    color: area.containsMouse ? "#23232c" : "#18181f"
    border.color: area.containsMouse ? "#a78bfa" : "#34343e"
    border.width: area.containsMouse ? 2 : 1

    Behavior on color { ColorAnimation { duration: 140 } }
    Behavior on border.color { ColorAnimation { duration: 140 } }

    Image {
        id: picture
        objectName: "tileArtwork"
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: 8
        height: parent.height - 70
        source: tile.artwork
        fillMode: Image.PreserveAspectCrop
        asynchronous: true
        smooth: true
        visible: tile.hasArtwork
    }

    Text {
        objectName: "tileLabel"
        x: 16
        width: parent.width - 48
        y: tile.hasArtwork ? picture.y + picture.height + 14 : (parent.height - height) / 2
        text: tile.label
        color: "#fafafa"
        font.pixelSize: 18
        font.weight: Font.DemiBold
        wrapMode: Text.Wrap
        elide: Text.ElideRight
        maximumLineCount: 2
    }

    Text {
        anchors.right: parent.right
        anchors.rightMargin: 16
        y: tile.hasArtwork ? picture.y + picture.height + 14 : (parent.height - height) / 2
        text: "›"
        color: area.containsMouse ? "#c4b5fd" : "#85858e"
        font.pixelSize: 22
    }

    MouseArea {
        id: area
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: tile.clicked()
    }
}
