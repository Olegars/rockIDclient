import QtQuick
import QtQuick.Controls
import rockid

ApplicationWindow {
    id: window
    width: 1180
    height: 760
    minimumWidth: 960
    minimumHeight: 640
    visible: true
    color: Theme.window
    title: {
        if (!client.authenticated)
            return "rockID"
        var unread = client.unreadTotal
        return unread > 0 ? "rockID (" + unread + ")" : "rockID"
    }
    font.family: Theme.fontFamily
    font.pixelSize: 14
    property bool unlocked: false

    palette.window: Theme.window
    palette.windowText: Theme.text
    palette.base: Theme.input
    palette.text: Theme.text
    palette.button: Theme.sidebar
    palette.buttonText: Theme.text
    palette.highlight: Theme.accent
    palette.highlightedText: "white"
    palette.placeholderText: Theme.secondary
    palette.mid: Theme.line

    LoginPage {
        anchors.fill: parent
        visible: !client.authenticated
    }

    MessengerPage {
        anchors.fill: parent
        visible: client.authenticated && (!client.pinSet || window.unlocked)
    }

    Rectangle {
        anchors.fill: parent
        visible: client.authenticated && client.pinSet && !window.unlocked
        color: Theme.window
        Column {
            anchors.centerIn: parent
            spacing: 12
            Text { text: "Код-пароль"; color: Theme.text; font.pixelSize: 22; anchors.horizontalCenter: parent.horizontalCenter }
            TextField {
                id: pinField
                width: 220
                echoMode: TextInput.Password
                placeholderText: "Пин-код"
                color: Theme.text
                background: Rectangle { radius: 10; color: Theme.input }
                onAccepted: window.tryPin()
            }
            Button {
                text: "Открыть"
                anchors.horizontalCenter: parent.horizontalCenter
                onClicked: window.tryPin()
            }
        }
    }

    function tryPin() {
        if (client.checkPin(pinField.text))
            unlocked = true
        else
            client.authError = "Неверный код"
    }

    function applyTheme() {
        var raw = client.me && client.me.preferences ? String(client.me.preferences) : ""
        var day = raw.indexOf("\"theme\":\"day\"") >= 0
        if (!day)
            return
        Theme.window = "#e7eef3"
        Theme.sidebar = "#ffffff"
        Theme.chat = "#d7e6f0"
        Theme.header = "#ffffff"
        Theme.bubbleOut = "#effdde"
        Theme.bubbleIn = "#ffffff"
        Theme.text = "#1d2733"
        Theme.secondary = "#6d7f8f"
        Theme.hover = "#e7eef3"
        Theme.input = "#f4f7fa"
        Theme.chip = "#e7eef3"
        Theme.line = "#d5dee6"
    }

    Rectangle {
        id: toast
        property string message: ""
        z: 50
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 28
        radius: 12
        color: "#243444"
        opacity: 0
        visible: opacity > 0.01
        width: Math.min(window.width - 48, toastText.implicitWidth + 32)
        height: 40

        Behavior on opacity {
            NumberAnimation { duration: 140 }
        }

        Text {
            id: toastText
            anchors.centerIn: parent
            width: Math.min(implicitWidth, toast.width - 32)
            text: toast.message
            color: "white"
            font.pixelSize: 13
            elide: Text.ElideRight
            horizontalAlignment: Text.AlignHCenter
        }

        Timer {
            id: toastTimer
            interval: 3200
            onTriggered: toast.opacity = 0
        }
    }

    Connections {
        target: client
        function onUserNotice(text) {
            toast.message = text
            toast.opacity = 1
            toastTimer.restart()
        }
        function onMeChanged() { window.applyTheme() }
        function onAuthenticatedChanged() { window.applyTheme() }
    }
}
