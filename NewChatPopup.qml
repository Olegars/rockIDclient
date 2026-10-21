import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import rockid

Popup {
    id: popup
    parent: Overlay.overlay
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    width: 440
    height: Math.min(640, Overlay.overlay ? Overlay.overlay.height - 48 : 640)
    anchors.centerIn: parent
    padding: 0

    property bool groupMode: false
    property var picked: ({})

    background: Rectangle {
        radius: 16
        color: Theme.sidebar
        border.color: "#243140"
        border.width: 1
    }

    function showPeople() {
        groupMode = false
        picked = ({})
        query.text = ""
        client.searchUsers("")
        open()
        query.forceActiveFocus()
    }

    function showGroup() {
        groupMode = true
        picked = ({})
        query.text = ""
        titleField.text = ""
        client.searchUsers("")
        client.refresh()
        open()
        titleField.forceActiveFocus()
    }

    function togglePick(id) {
        var next = Object.assign({}, picked)
        var key = String(id)
        if (next[key])
            delete next[key]
        else
            next[key] = true
        picked = next
    }

    function pickedCount() {
        var n = 0
        for (var k in picked)
            if (picked[k])
                n++
        return n
    }

    function peopleModel() {
        if (query.text.trim().length > 0)
            return client.searchResults
        return client.contacts
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            Text {
                text: popup.groupMode ? "Новая группа" : "Новый чат"
                color: Theme.text
                font.pixelSize: 20
                font.bold: true
                Layout.fillWidth: true
            }
            Button {
                flat: true
                implicitWidth: 32
                implicitHeight: 32
                onClicked: popup.close()
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

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Button {
                Layout.fillWidth: true
                text: "Человек"
                checked: !popup.groupMode
                onClicked: popup.groupMode = false
                font.family: Theme.fontFamily
                contentItem: Text {
                    text: parent.text
                    color: parent.checked ? "white" : Theme.secondary
                    font.pixelSize: 13
                    font.bold: parent.checked
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    radius: 10
                    color: parent.checked ? Theme.accent : Theme.input
                }
            }
            Button {
                Layout.fillWidth: true
                text: "Группа"
                checked: popup.groupMode
                onClicked: popup.groupMode = true
                font.family: Theme.fontFamily
                contentItem: Text {
                    text: parent.text
                    color: parent.checked ? "white" : Theme.secondary
                    font.pixelSize: 13
                    font.bold: parent.checked
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    radius: 10
                    color: parent.checked ? Theme.accent : Theme.input
                }
            }
        }

        TextField {
            id: titleField
            Layout.fillWidth: true
            visible: popup.groupMode
            placeholderText: "Название группы"
            placeholderTextColor: Theme.secondary
            color: Theme.text
            font.pixelSize: 14
            selectByMouse: true
            leftPadding: 12
            background: Rectangle { radius: 10; color: Theme.input }
        }

        TextField {
            id: query
            Layout.fillWidth: true
            placeholderText: popup.groupMode ? "Найти людей для группы" : "Имя пользователя"
            placeholderTextColor: Theme.secondary
            color: Theme.text
            font.pixelSize: 14
            selectByMouse: true
            leftPadding: 12
            background: Rectangle { radius: 10; color: Theme.input }
            onTextChanged: searchDebounce.restart()
            onAccepted: {
                if (!popup.groupMode && text.trim().length > 0)
                    client.addContactByName(text.trim())
            }
        }

        Timer {
            id: searchDebounce
            interval: 220
            onTriggered: client.searchUsers(query.text)
        }

        Text {
            visible: !popup.groupMode && query.text.trim().length === 0
            text: "Контакты"
            color: Theme.secondary
            font.pixelSize: 12
        }
        Text {
            visible: popup.groupMode
            text: "Отметьте участников. Себя добавлять не нужно."
            color: Theme.secondary
            font.pixelSize: 12
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }

        ListView {
            id: people
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 2
            model: popup.peopleModel()
            boundsBehavior: Flickable.StopAtBounds

            delegate: Item {
                required property var modelData
                required property int index
                width: people.width
                height: 58

                Rectangle {
                    anchors.fill: parent
                    radius: 10
                    color: area.containsMouse ? Theme.hover : "transparent"
                }

                Row {
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8
                    spacing: 10
                    Avatar {
                        width: 40
                        height: 40
                        anchors.verticalCenter: parent.verticalCenter
                        name: modelData.display_name || modelData.username
                        seed: modelData.id
                        online: !!modelData.online
                        ring: Theme.sidebar
                    }
                    Column {
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width - 40 - (popup.groupMode ? 28 : 0) - 20
                        spacing: 2
                        Text {
                            width: parent.width
                            text: modelData.display_name || modelData.username
                            color: Theme.text
                            font.pixelSize: 14
                            elide: Text.ElideRight
                        }
                        Text {
                            width: parent.width
                            text: "@" + modelData.username + (modelData.online ? "  ·  в сети" : "")
                            color: Theme.secondary
                            font.pixelSize: 12
                            elide: Text.ElideRight
                        }
                    }
                    Rectangle {
                        visible: popup.groupMode
                        width: 22
                        height: 22
                        radius: 6
                        anchors.verticalCenter: parent.verticalCenter
                        color: popup.picked[String(modelData.id)] ? Theme.accent : "transparent"
                        border.color: popup.picked[String(modelData.id)] ? Theme.accent : Theme.secondary
                        border.width: 1
                        Text {
                            anchors.centerIn: parent
                            visible: popup.picked[String(modelData.id)]
                            text: "\uE73E"
                            color: "white"
                            font.family: Theme.iconFont
                            font.pixelSize: 10
                        }
                    }
                }

                MouseArea {
                    id: area
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        if (popup.groupMode)
                            popup.togglePick(modelData.id)
                        else
                            client.startPrivate(modelData.id)
                    }
                }
            }

            Text {
                anchors.centerIn: parent
                width: parent.width - 24
                visible: people.count === 0
                text: query.text.trim().length > 0 ? "Никого не нашли" : "Контактов пока нет. Введите username и нажмите Enter."
                color: Theme.secondary
                font.pixelSize: 13
                wrapMode: Text.WordWrap
                horizontalAlignment: Text.AlignHCenter
            }
        }

        Button {
            Layout.fillWidth: true
            visible: popup.groupMode
            implicitHeight: 44
            enabled: titleField.text.trim().length > 0
            onClicked: {
                var ids = []
                for (var k in popup.picked)
                    if (popup.picked[k])
                        ids.push(Number(k))
                client.createGroup(titleField.text, ids)
            }
            contentItem: Text {
                text: "Создать" + (popup.pickedCount() > 0 ? "  ·  " + popup.pickedCount() : "")
                color: "white"
                font.pixelSize: 14
                font.bold: true
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                opacity: parent.enabled ? 1 : 0.5
            }
            background: Rectangle {
                radius: 12
                color: parent.down ? Theme.accentDark : Theme.accent
                opacity: parent.enabled ? 1 : 0.4
            }
        }
    }

    Connections {
        target: client
        function onChatOpened() { popup.close() }
    }
}
