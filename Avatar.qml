import QtQuick
import rockid

Rectangle {
    id: root
    property string name: ""
    property var seed: 0
    property bool online: false
    property color ring: Theme.sidebar
    property int dotSize: Math.round(width * 0.3)

    radius: width / 2
    color: Theme.avatarColor(seed)

    Text {
        anchors.centerIn: parent
        text: Theme.initials(root.name)
        color: "white"
        font.family: Theme.fontFamily
        font.pixelSize: Math.max(11, Math.round(root.width * 0.36))
        font.bold: true
    }

    Rectangle {
        visible: root.online
        width: root.dotSize
        height: root.dotSize
        radius: width / 2
        color: Theme.online
        border.color: root.ring
        border.width: 2
        anchors.right: parent.right
        anchors.bottom: parent.bottom
    }
}
