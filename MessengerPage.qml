import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import rockid

Item {
    id: root
    property bool infoOpen: false
    property double replyId: 0
    property string replyQuote: ""
    property bool silentSend: false
    property bool spoilerSend: false
    property double scheduleAt: 0
    property double topicId: 0
    property var picked: null

    NewChatPopup { id: newChat }
    ToolsSheet {
        id: tools
        onReplyAsked: function(id, quote) {
            root.replyId = id
            root.replyQuote = quote
        }
    }

    Shortcut {
        sequences: ["Print", "Ctrl+Print"]
        onActivated: client.reportScreenshot()
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.preferredWidth: 340
            Layout.fillHeight: true
            color: Theme.sidebar

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                RowLayout {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 56
                    Layout.leftMargin: 16
                    Layout.rightMargin: 8
                    Text {
                        text: "rockID"
                        color: Theme.text
                        font.pixelSize: 20
                        font.bold: true
                        Layout.fillWidth: true
                    }
                    IconButton {
                        glyph: "\uE70F"
                        tip: "Новый чат"
                        onClicked: newChat.showPeople()
                    }
                    IconButton {
                        glyph: "\uE713"
                        tip: "Сервер"
                        onClicked: serverPopup.open()
                    }
                }

                ListView {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 34
                    Layout.leftMargin: 8
                    orientation: ListView.Horizontal
                    spacing: 6
                    clip: true
                    model: {
                        var names = ["Все", "Непрочитанные", "Архив"]
                        var folders = client.folders
                        for (var i = 0; i < folders.length; ++i)
                            names.push(folders[i].title)
                        return names
                    }
                    delegate: Text {
                        required property var modelData
                        text: modelData
                        color: client.listMode === modelData ? Theme.accent : Theme.secondary
                        font.pixelSize: 13
                        leftPadding: 8
                        rightPadding: 8
                        height: 28
                        verticalAlignment: Text.AlignVCenter
                        MouseArea {
                            anchors.fill: parent
                            onClicked: client.listMode = modelData
                        }
                    }
                }

                TextField {
                    id: filterField
                    Layout.fillWidth: true
                    Layout.leftMargin: 12
                    Layout.rightMargin: 12
                    Layout.bottomMargin: 8
                    placeholderText: "Поиск"
                    placeholderTextColor: Theme.secondary
                    color: Theme.text
                    font.pixelSize: 14
                    leftPadding: 32
                    selectByMouse: true
                    background: Rectangle { radius: 10; color: Theme.input }
                    onTextChanged: client.dialogFilter = text
                    Text {
                        x: 10
                        anchors.verticalCenter: parent.verticalCenter
                        text: "\uE721"
                        color: Theme.secondary
                        font.family: Theme.iconFont
                        font.pixelSize: 13
                    }
                }

                ListView {
                    id: dialogList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: client.dialogs
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                    delegate: Item {
                        id: row
                        required property var modelData
                        required property int index
                        width: dialogList.width
                        height: 72

                        readonly property bool group: modelData.type === "group"
                        readonly property string title: {
                            if (group)
                                return modelData.title || "Группа"
                            var peer = modelData.peer
                            if (!peer)
                                return "Чат"
                            return peer.display_name || peer.username || "Чат"
                        }
                        readonly property string preview: {
                            var last = modelData.last_message
                            if (!last || !last.text)
                                return "Нет сообщений"
                            var body = client.previewOf(row.modelData.id, String(last.text)).replace(/\s+/g, " ")
                            if (body.length > 80)
                                body = body.substring(0, 80) + "…"
                            if (client.me && last.sender_id === client.me.id)
                                return "Вы: " + body
                            if (group) {
                                var name = client.userName(last.sender_id)
                                if (name)
                                    return name + ": " + body
                            }
                            return body
                        }

                        Rectangle {
                            anchors.fill: parent
                            color: client.activeDialogId === row.modelData.id ? Theme.selected : (area.containsMouse ? Theme.hover : "transparent")
                        }

                        Row {
                            anchors.fill: parent
                            anchors.leftMargin: 12
                            anchors.rightMargin: 12
                            spacing: 10
                            Avatar {
                                width: 48
                                height: 48
                                anchors.verticalCenter: parent.verticalCenter
                                name: row.title
                                seed: row.group ? row.modelData.id : (row.modelData.peer ? row.modelData.peer.id : row.modelData.id)
                                online: !row.group && row.modelData.peer && !!row.modelData.peer.online
                                ring: client.activeDialogId === row.modelData.id ? Theme.selected : Theme.sidebar
                            }
                            Column {
                                anchors.verticalCenter: parent.verticalCenter
                                width: parent.width - 58
                                spacing: 3
                                Row {
                                    width: parent.width
                                    spacing: 8
                                    Text {
                                        width: parent.width - timeLabel.width - 8
                                        text: row.title
                                        color: Theme.text
                                        font.pixelSize: 15
                                        font.bold: row.modelData.unread > 0
                                        elide: Text.ElideRight
                                    }
                                    Text {
                                        id: timeLabel
                                        text: row.modelData.last_message && row.modelData.last_message.date ? client.formatListTime(row.modelData.last_message.date) : ""
                                        color: row.modelData.unread > 0 ? Theme.accent : Theme.secondary
                                        font.pixelSize: 12
                                    }
                                }
                                Row {
                                    width: parent.width
                                    spacing: 8
                                    Text {
                                        width: parent.width - (badge.visible ? badge.width + 8 : 0)
                                        text: row.preview
                                        color: row.modelData.unread > 0 ? Theme.text : Theme.secondary
                                        font.pixelSize: 13
                                        elide: Text.ElideRight
                                    }
                                    Rectangle {
                                        id: badge
                                        visible: row.modelData.unread > 0
                                        width: Math.max(20, badgeText.implicitWidth + 10)
                                        height: 20
                                        radius: 10
                                        color: Theme.accent
                                        Text {
                                            id: badgeText
                                            anchors.centerIn: parent
                                            text: row.modelData.unread > 99 ? "99+" : row.modelData.unread
                                            color: "white"
                                            font.pixelSize: 11
                                            font.bold: true
                                        }
                                    }
                                }
                            }
                        }

                        MouseArea {
                            id: area
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: client.openDialog(row.modelData.id)
                        }
                    }

                    Text {
                        anchors.centerIn: parent
                        width: parent.width - 40
                        visible: dialogList.count === 0
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                        color: Theme.secondary
                        font.pixelSize: 14
                        text: filterField.text.length > 0 ? "Ничего не найдено" : "Чатов пока нет.\nНачните новый."
                    }
                }

                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.line }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 64
                    Layout.leftMargin: 12
                    Layout.rightMargin: 8
                    Avatar {
                        Layout.preferredWidth: 40
                        Layout.preferredHeight: 40
                        name: client.me.display_name || client.me.username || ""
                        seed: client.me.id || 0
                        ring: Theme.sidebar
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        Text {
                            Layout.fillWidth: true
                            text: client.me.display_name || ""
                            color: Theme.text
                            font.pixelSize: 14
                            elide: Text.ElideRight
                        }
                        Text {
                            Layout.fillWidth: true
                            text: client.me.username ? "@" + client.me.username : ""
                            color: Theme.secondary
                            font.pixelSize: 12
                            elide: Text.ElideRight
                        }
                    }
                    IconButton {
                        glyph: "\uE72D"
                        tip: "Обновить"
                        onClicked: client.refresh()
                    }
                    IconButton {
                        glyph: "\uE7E8"
                        tip: "Выйти"
                        onClicked: client.signOut()
                    }
                }
            }
        }

        Rectangle { Layout.preferredWidth: 1; Layout.fillHeight: true; color: Theme.line }

        Item {
            id: chatPane
            Layout.fillWidth: true
            Layout.fillHeight: true

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                z: 3
                height: client.callIncoming || client.callLive ? 52 : 0
                visible: client.callIncoming || client.callLive
                color: client.callLive ? "#1d4e3a" : "#1e4d7b"
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 12
                    anchors.rightMargin: 8
                    spacing: 8
                    Text {
                        Layout.fillWidth: true
                        text: client.callIncoming ? "Входящий звонок" : (client.callMuted ? "Звонок · микрофон выключен" : "Звонок")
                        color: "white"
                        font.pixelSize: 14
                        elide: Text.ElideRight
                    }
                    Button {
                        visible: client.callIncoming
                        text: "Принять"
                        onClicked: client.acceptCall()
                    }
                    Button {
                        visible: client.callIncoming
                        text: "Отклонить"
                        onClicked: client.declineCall()
                    }
                    Button {
                        visible: client.callLive
                        text: client.callMuted ? "Включить" : "Выключить"
                        onClicked: client.toggleCallMute()
                    }
                    Button {
                        visible: client.callLive
                        text: "Завершить"
                        onClicked: client.hangUp()
                    }
                }
            }

            ColumnLayout {
                anchors.fill: parent
                spacing: 0
                visible: client.activeDialogId !== 0

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 56
                    color: Theme.header
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        anchors.rightMargin: 8
                        spacing: 10
                        Avatar {
                            Layout.preferredWidth: 40
                            Layout.preferredHeight: 40
                            name: client.activeTitle
                            seed: client.activeSeed
                            online: client.activeKind === "private" && client.activeOnline
                            ring: Theme.header
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 0
                            Text {
                                Layout.fillWidth: true
                                text: client.activeTitle
                                color: Theme.text
                                font.pixelSize: 16
                                font.bold: true
                                elide: Text.ElideRight
                            }
                            Text {
                                Layout.fillWidth: true
                                text: client.activeStatus
                                color: client.activeKind === "private" && client.activeOnline ? Theme.online : Theme.secondary
                                font.pixelSize: 12
                                elide: Text.ElideRight
                            }
                        }
                        IconButton {
                            glyph: "\uE717"
                            tip: "Звонок"
                            visible: !client.callLive
                            onClicked: client.startCall()
                        }
                        IconButton {
                            glyph: "\uE712"
                            tip: "Действия"
                            onClicked: {
                                tools.picked = root.picked
                                tools.open()
                            }
                        }
                        IconButton {
                            glyph: "\uE946"
                            tip: "О чате"
                            onClicked: root.toggleInfo()
                        }
                    }
                    MouseArea {
                        anchors.fill: parent
                        anchors.rightMargin: 140
                        onClicked: root.toggleInfo()
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: client.linkState === "online" ? 0 : 28
                    visible: client.linkState !== "online"
                    color: "#2b5278"
                    Text {
                        anchors.centerIn: parent
                        text: client.statusLine
                        color: "white"
                        font.pixelSize: 12
                    }
                }

                ListView {
                    id: messageList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    spacing: 6
                    model: client.messages
                    boundsBehavior: Flickable.StopAtBounds
                    verticalLayoutDirection: ListView.TopToBottom
                    cacheBuffer: 400
                    property bool stickToEnd: true
                    property real heightBefore: 0

                    onContentYChanged: stickToEnd = contentHeight - contentY - height < 120
                    onContentHeightChanged: {
                        if (stickToEnd)
                            Qt.callLater(function() { messageList.positionViewAtEnd() })
                    }

                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                    header: Item {
                        width: messageList.width
                        height: client.canLoadOlder ? 48 : 8
                        Button {
                            anchors.centerIn: parent
                            visible: client.canLoadOlder
                            enabled: !client.historyLoading
                            text: client.historyLoading ? "Загрузка…" : "Ранние сообщения"
                            flat: true
                            onClicked: {
                                messageList.heightBefore = messageList.contentHeight
                                client.loadOlder()
                            }
                            contentItem: Text {
                                text: parent.text
                                color: Theme.accent
                                font.pixelSize: 13
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                            background: Item {}
                        }
                    }

                    delegate: Item {
                        id: bubbleRow
                        required property var modelData
                        required property int index
                        width: messageList.width
                        height: modelData.kind === "date" ? 36 : bubble.implicitHeight + 2

                        Rectangle {
                            visible: modelData.kind === "date"
                            anchors.horizontalCenter: parent.horizontalCenter
                            anchors.verticalCenter: parent.verticalCenter
                            width: dateText.implicitWidth + 18
                            height: 24
                            radius: 12
                            color: Theme.chip
                            Text {
                                id: dateText
                                anchors.centerIn: parent
                                text: modelData.label || ""
                                color: Theme.secondary
                                font.pixelSize: 12
                            }
                        }

                        TextMetrics {
                            id: bodyMetrics
                            text: modelData.text || ""
                            font.pixelSize: 15
                            font.family: Theme.fontFamily
                        }

                        Rectangle {
                            id: bubble
                            visible: modelData.kind === "message"
                            anchors.right: modelData.mine ? parent.right : undefined
                            anchors.left: modelData.mine ? undefined : parent.left
                            anchors.rightMargin: 16
                            anchors.leftMargin: 16
                            readonly property real textWidth: Math.min(Math.max(bodyMetrics.width, timeText.implicitWidth), Math.max(120, messageList.width * 0.66))
                            width: textWidth + 22
                            implicitHeight: (nameText.visible ? nameText.implicitHeight + 2 : 0) + body.implicitHeight + 22
                            radius: 16
                            topLeftRadius: 16
                            topRightRadius: 16
                            bottomLeftRadius: modelData.mine ? 16 : 5
                            bottomRightRadius: modelData.mine ? 5 : 16
                            color: modelData.mine ? Theme.bubbleOut : Theme.bubbleIn

                            Text {
                                id: nameText
                                visible: !!modelData.show_name
                                x: 10
                                y: 6
                                width: parent.width - 20
                                text: modelData.sender_name || ""
                                color: Theme.avatarColor(modelData.seed)
                                font.pixelSize: 13
                                font.bold: true
                                elide: Text.ElideRight
                            }
                            TapHandler {
                                onLongPressed: {
                                    root.picked = modelData
                                    tools.picked = modelData
                                    tools.open()
                                }
                            }
                            TextEdit {
                                id: body
                                x: 10
                                y: nameText.visible ? nameText.implicitHeight + 8 : 8
                                width: bubble.textWidth
                                text: modelData.text || ""
                                color: Theme.text
                                font.pixelSize: 15
                                font.family: Theme.fontFamily
                                wrapMode: TextEdit.Wrap
                                readOnly: true
                                selectByMouse: true
                                textFormat: TextEdit.PlainText
                                selectionColor: Theme.accent
                                selectedTextColor: "white"
                            }
                            Text {
                                id: timeText
                                anchors.right: parent.right
                                anchors.bottom: parent.bottom
                                anchors.rightMargin: 8
                                anchors.bottomMargin: 4
                                text: modelData.time || ""
                                color: "#9bb8d0"
                                font.pixelSize: 11
                            }
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: composerRow.implicitHeight
                    color: Theme.header
                    RowLayout {
                        id: composerRow
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 8
                        TextArea {
                            id: composer
                            Layout.fillWidth: true
                            Layout.minimumHeight: 40
                            Layout.maximumHeight: 140
                            Layout.preferredHeight: Math.min(140, Math.max(40, contentHeight + 12))
                            placeholderText: "Сообщение"
                            placeholderTextColor: Theme.secondary
                            color: Theme.text
                            font.pixelSize: 15
                            wrapMode: TextArea.Wrap
                            selectByMouse: true
                            leftPadding: 12
                            rightPadding: 12
                            topPadding: 8
                            bottomPadding: 8
                            background: Rectangle { radius: 12; color: Theme.input }
                            Keys.onPressed: function(event) {
                                if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && !(event.modifiers & Qt.ShiftModifier)) {
                                    event.accepted = true
                                    root.sendCurrent()
                                }
                            }
                        }
                        Button {
                            Layout.preferredWidth: 42
                            Layout.preferredHeight: 42
                            enabled: composer.text.trim().length > 0 && client.utf8Size(composer.text) <= 4096
                            onClicked: root.sendCurrent()
                            contentItem: Text {
                                text: "\uE724"
                                color: "white"
                                font.family: Theme.iconFont
                                font.pixelSize: 16
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                            background: Rectangle {
                                radius: 21
                                color: parent.down ? Theme.accentDark : Theme.accent
                                opacity: parent.enabled ? 1 : 0.4
                            }
                        }
                    }
                }
            }

            Item {
                anchors.fill: parent
                visible: client.activeDialogId === 0
                Column {
                    anchors.centerIn: parent
                    spacing: 12
                    Rectangle {
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: 72
                        height: 72
                        radius: 36
                        color: Theme.header
                        Text {
                            anchors.centerIn: parent
                            text: "\uE8BD"
                            color: Theme.accent
                            font.family: Theme.iconFont
                            font.pixelSize: 28
                        }
                    }
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: "Выберите чат"
                        color: Theme.text
                        font.pixelSize: 20
                        font.bold: true
                    }
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: "или начните новый"
                        color: Theme.secondary
                        font.pixelSize: 14
                    }
                }
            }

            Button {
                visible: client.activeDialogId !== 0 && !messageList.stickToEnd
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.rightMargin: 18
                anchors.bottomMargin: 78
                implicitWidth: 40
                implicitHeight: 40
                z: 2
                onClicked: messageList.positionViewAtEnd()
                contentItem: Text {
                    text: "\uE74B"
                    color: Theme.text
                    font.family: Theme.iconFont
                    font.pixelSize: 14
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle { radius: 20; color: Theme.header; border.color: "#243140"; border.width: 1 }
            }
        }
    }

    InfoPanel {
        id: info
        z: 3
        width: 320
        height: parent.height
        x: root.infoOpen ? parent.width - width : parent.width
        enabled: root.infoOpen
        opened: root.infoOpen
        Behavior on x { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }
        onOpenedChanged: root.infoOpen = opened
    }

    Popup {
        id: serverPopup
        parent: Overlay.overlay
        modal: true
        focus: true
        width: 380
        height: 210
        anchors.centerIn: parent
        padding: 18
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Rectangle { radius: 14; color: Theme.sidebar; border.color: "#243140"; border.width: 1 }
        ColumnLayout {
            anchors.fill: parent
            spacing: 10
            Text {
                text: "Сервер"
                color: Theme.text
                font.pixelSize: 18
                font.bold: true
            }
            Text {
                text: client.statusLine
                color: Theme.secondary
                font.pixelSize: 12
            }
            RowLayout {
                Layout.fillWidth: true
                TextField {
                    id: serverHost
                    Layout.fillWidth: true
                    text: client.host
                    placeholderText: "95.165.30.178"
                    color: Theme.text
                    placeholderTextColor: Theme.secondary
                    selectByMouse: true
                    background: Rectangle { radius: 8; color: Theme.input }
                }
                TextField {
                    id: serverPort
                    Layout.preferredWidth: 80
                    text: String(client.port)
                    placeholderText: "7410"
                    color: Theme.text
                    placeholderTextColor: Theme.secondary
                    validator: IntValidator { bottom: 1; top: 65535 }
                    background: Rectangle { radius: 8; color: Theme.input }
                }
            }
            Button {
                Layout.fillWidth: true
                implicitHeight: 40
                text: "Подключить"
                onClicked: {
                    var port = parseInt(serverPort.text, 10)
                    client.setEndpoint(serverHost.text, isNaN(port) ? 7410 : port)
                    serverPopup.close()
                }
                contentItem: Text {
                    text: parent.text
                    color: "white"
                    font.bold: true
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle { radius: 10; color: Theme.accent }
            }
        }
        onOpened: {
            serverHost.text = client.host
            serverPort.text = String(client.port)
        }
    }

    Shortcut {
        sequences: ["Ctrl+N"]
        onActivated: newChat.showPeople()
    }
    Shortcut {
        sequences: [StandardKey.Cancel]
        enabled: root.infoOpen
        onActivated: root.infoOpen = false
    }

    Connections {
        target: client
        function onHistoryReset() {
            messageList.stickToEnd = true
            Qt.callLater(function() { messageList.positionViewAtEnd() })
        }
        function onHistoryPrepended() {
            Qt.callLater(function() {
                var delta = messageList.contentHeight - messageList.heightBefore
                if (delta > 0)
                    messageList.contentY += delta
            })
        }
        function onMessageArrived(own) {
            if (own || messageList.stickToEnd) {
                messageList.stickToEnd = true
                Qt.callLater(function() { messageList.positionViewAtEnd() })
            }
        }
        function onSendFailed(text) {
            if (composer.text.length === 0)
                composer.text = text
        }
        function onActiveDialogIdChanged() {
            composer.text = ""
            if (client.activeDialogId === 0)
                root.infoOpen = false
        }
    }

    function sendCurrent() {
        var entities = root.spoilerSend ? "[{\"t\":\"spoiler\",\"o\":0,\"l\":" + composer.text.length + "}]" : ""
        var when = root.scheduleAt
        if (client.sendRich(composer.text, root.replyId, root.replyQuote, root.silentSend, when, root.topicId, entities)) {
            composer.text = ""
            root.replyId = 0
            root.replyQuote = ""
            root.scheduleAt = 0
        }
    }

    function toggleInfo() {
        if (client.activeDialogId === 0)
            return
        infoOpen = !infoOpen
    }

    component IconButton: Button {
        property string glyph: ""
        property string tip: ""
        implicitWidth: 36
        implicitHeight: 36
        flat: true
        hoverEnabled: true
        ToolTip.visible: hovered && tip.length > 0
        ToolTip.text: tip
        ToolTip.delay: 400
        contentItem: Text {
            text: parent.glyph
            color: parent.hovered ? Theme.text : Theme.secondary
            font.family: Theme.iconFont
            font.pixelSize: 16
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            radius: 8
            color: parent.hovered ? Theme.hover : "transparent"
        }
    }
}
