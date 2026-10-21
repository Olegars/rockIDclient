import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import rockid

Rectangle {
    id: panel
    color: Theme.sidebar
    property bool opened: false

    Rectangle {
        width: 1
        height: parent.height
        color: "#101921"
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            Text {
                text: client.activeKind === "group" ? "Группа" : "Профиль"
                color: Theme.text
                font.pixelSize: 18
                font.bold: true
                Layout.fillWidth: true
            }
            Button {
                flat: true
                implicitWidth: 32
                implicitHeight: 32
                onClicked: panel.opened = false
                contentItem: Text {
                    text: "\uE711"
                    color: Theme.secondary
                    font.family: Theme.iconFont
                    font.pixelSize: 12
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Item {}
            }
        }

        Avatar {
            Layout.alignment: Qt.AlignHCenter
            width: 84
            height: 84
            name: client.activeTitle
            seed: client.activeSeed
            online: client.activeKind === "private" && client.activeOnline
            ring: Theme.sidebar
        }

        Text {
            Layout.fillWidth: true
            text: client.activeTitle
            color: Theme.text
            font.pixelSize: 18
            font.bold: true
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
        }
        Text {
            Layout.fillWidth: true
            visible: client.activeHandle.length > 0
            text: "@" + client.activeHandle
            color: Theme.secondary
            font.pixelSize: 13
            horizontalAlignment: Text.AlignHCenter
        }
        Text {
            Layout.fillWidth: true
            text: client.activeStatus
            color: client.activeKind === "private" && client.activeOnline ? Theme.online : Theme.secondary
            font.pixelSize: 13
            horizontalAlignment: Text.AlignHCenter
        }

        Button {
            Layout.fillWidth: true
            visible: client.activeKind === "private" && client.activePeerId > 0 && !client.activeIsContact
            implicitHeight: 40
            onClicked: client.addContactById(client.activePeerId)
            contentItem: Text {
                text: "Добавить в контакты"
                color: "white"
                font.pixelSize: 13
                font.bold: true
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
            background: Rectangle {
                radius: 10
                color: parent.down ? Theme.accentDark : Theme.accent
            }
        }

        Text {
            visible: client.activeKind === "group"
            text: "Участники"
            color: Theme.secondary
            font.pixelSize: 12
        }

        ListView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: client.activeKind === "group"
            clip: true
            spacing: 2
            model: client.members
            boundsBehavior: Flickable.StopAtBounds

            delegate: Item {
                required property var modelData
                required property int index
                width: ListView.view.width
                height: 52
                Row {
                    anchors.fill: parent
                    spacing: 10
                    Avatar {
                        width: 36
                        height: 36
                        anchors.verticalCenter: parent.verticalCenter
                        name: modelData.display_name || modelData.username
                        seed: modelData.id
                        online: !!modelData.online
                        ring: Theme.sidebar
                    }
                    Column {
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width - 46
                        spacing: 1
                        Text {
                            width: parent.width
                            text: (modelData.display_name || modelData.username) + (modelData.id === client.me.id ? "  ·  вы" : "")
                            color: Theme.text
                            font.pixelSize: 14
                            elide: Text.ElideRight
                        }
                        Text {
                            width: parent.width
                            text: "@" + modelData.username + "  ·  " + (modelData.role === "owner" ? "владелец" : "участник")
                            color: Theme.secondary
                            font.pixelSize: 12
                            elide: Text.ElideRight
                        }
                    }
                }
            }
        }

        ColumnLayout {
            visible: client.activeKind === "group" && client.myRole === "owner"
            Layout.fillWidth: true
            spacing: 8

            Text {
                text: "Добавить участника"
                color: Theme.secondary
                font.pixelSize: 12
            }
            TextField {
                id: memberQuery
                Layout.fillWidth: true
                placeholderText: "username"
                placeholderTextColor: Theme.secondary
                color: Theme.text
                font.pixelSize: 13
                leftPadding: 10
                selectByMouse: true
                background: Rectangle { radius: 8; color: Theme.input }
                onTextChanged: memberDebounce.restart()
            }
            Timer {
                id: memberDebounce
                interval: 220
                onTriggered: client.searchUsers(memberQuery.text)
            }
            ListView {
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(160, count * 44)
                visible: memberQuery.text.trim().length > 0
                clip: true
                model: client.searchResults
                boundsBehavior: Flickable.StopAtBounds
                delegate: Item {
                    required property var modelData
                    required property int index
                    width: ListView.view.width
                    height: 44
                    RowLayout {
                        anchors.fill: parent
                        spacing: 8
                        Text {
                            Layout.fillWidth: true
                            text: (modelData.display_name || modelData.username) + "  @" + modelData.username
                            color: Theme.text
                            font.pixelSize: 13
                            elide: Text.ElideRight
                        }
                        Button {
                            text: "Добавить"
                            implicitHeight: 30
                            onClicked: client.addMember(modelData.id)
                            contentItem: Text {
                                text: parent.text
                                color: "white"
                                font.pixelSize: 12
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                            background: Rectangle { radius: 8; color: Theme.accent }
                        }
                    }
                }
            }
        }
    }

    onOpenedChanged: {
        if (opened && client.activeKind === "group")
            client.loadMembers()
        if (!opened)
            client.searchUsers("")
    }

    Connections {
        target: client
        function onActiveDialogIdChanged() {
            if (panel.opened && client.activeKind === "group")
                client.loadMembers()
        }
    }
}
