import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import rockid

Item {
    id: root

    component LineField: TextField {
        property string caption: ""
        color: Theme.text
        placeholderTextColor: Theme.secondary
        font.pixelSize: 15
        font.family: Theme.fontFamily
        selectByMouse: true
        leftPadding: 14
        rightPadding: 14
        implicitHeight: 46
        background: Rectangle {
            radius: 12
            color: Theme.input
            border.width: parent.activeFocus ? 1 : 0
            border.color: Theme.accent
        }
    }

    component PrimaryButton: Button {
        implicitHeight: 46
        font.pixelSize: 15
        font.bold: true
        font.family: Theme.fontFamily
        contentItem: Text {
            text: parent.text
            color: "white"
            font: parent.font
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            opacity: parent.enabled ? 1 : 0.55
        }
        background: Rectangle {
            radius: 12
            color: parent.down ? Theme.accentDark : Theme.accent
            opacity: parent.enabled ? 1 : 0.45
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.preferredWidth: Math.max(340, root.width * 0.42)
            Layout.fillHeight: true
            gradient: Gradient {
                GradientStop { position: 0; color: "#1c3044" }
                GradientStop { position: 1; color: "#0e1621" }
            }

            Column {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: 48
                anchors.rightMargin: 36
                spacing: 18

                Rectangle {
                    width: 84
                    height: 84
                    radius: 26
                    color: Theme.accent
                    Text {
                        anchors.centerIn: parent
                        text: "r"
                        color: "white"
                        font.pixelSize: 46
                        font.bold: true
                        font.family: Theme.fontFamily
                    }
                }

                Text {
                    text: "rockID"
                    color: "white"
                    font.pixelSize: 40
                    font.bold: true
                    font.family: Theme.fontFamily
                }

                Text {
                    width: parent.width
                    text: "Личные сообщения и группы.\nОдин аккаунт, несколько устройств."
                    color: Theme.secondary
                    font.pixelSize: 16
                    font.family: Theme.fontFamily
                    wrapMode: Text.WordWrap
                    lineHeight: 1.25
                }

                Row {
                    spacing: 8
                    Rectangle {
                        width: 8
                        height: 8
                        radius: 4
                        anchors.verticalCenter: parent.verticalCenter
                        color: client.linkState === "online" ? Theme.online : (client.linkState === "connecting" ? "#e5ca77" : Theme.danger)
                    }
                    Text {
                        text: client.host + ":" + client.port + "  ·  " + client.statusLine
                        color: Theme.secondary
                        font.pixelSize: 13
                        font.family: Theme.fontFamily
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: Theme.window

            Column {
                anchors.centerIn: parent
                width: Math.min(380, parent.width - 64)
                spacing: 14
                visible: client.restoring

                BusyIndicator {
                    anchors.horizontalCenter: parent.horizontalCenter
                    running: true
                    palette.dark: Theme.accent
                }
                Text {
                    width: parent.width
                    text: "Входим в сохранённую сессию"
                    color: Theme.text
                    font.pixelSize: 20
                    font.bold: true
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                }
                Text {
                    width: parent.width
                    text: client.statusLine
                    color: Theme.secondary
                    font.pixelSize: 13
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                }
                PrimaryButton {
                    width: parent.width
                    text: "Войти другим аккаунтом"
                    onClicked: client.forgetSession()
                }
            }

            Column {
                id: form
                anchors.centerIn: parent
                width: Math.min(380, parent.width - 64)
                spacing: 12
                visible: !client.restoring

                Text {
                    text: signup.checked ? "Создать аккаунт" : "Войти"
                    color: Theme.text
                    font.pixelSize: 28
                    font.bold: true
                }
                Text {
                    width: parent.width
                    text: signup.checked ? "Имя латиницей, пароль от 8 символов." : "Имя и пароль от сервера rockID."
                    color: Theme.secondary
                    wrapMode: Text.WordWrap
                    font.pixelSize: 13
                }

                LineField {
                    id: username
                    width: parent.width
                    caption: "username"
                    placeholderText: "username"
                    inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhPreferLowercase
                    onTextChanged: client.authError = ""
                }
                LineField {
                    id: displayName
                    width: parent.width
                    visible: signup.checked
                    placeholderText: "Имя на экране"
                    onTextChanged: client.authError = ""
                }
                LineField {
                    id: password
                    width: parent.width
                    placeholderText: "Пароль"
                    echoMode: TextInput.Password
                    onTextChanged: client.authError = ""
                    onAccepted: root.submit()
                }
                LineField {
                    id: passwordAgain
                    width: parent.width
                    visible: signup.checked
                    placeholderText: "Пароль ещё раз"
                    echoMode: TextInput.Password
                    onAccepted: root.submit()
                }

                Text {
                    width: parent.width
                    visible: client.authError.length > 0
                    text: client.authError
                    color: Theme.danger
                    wrapMode: Text.WordWrap
                    font.pixelSize: 13
                }

                PrimaryButton {
                    width: parent.width
                    text: client.authBusy ? "Подождите…" : (signup.checked ? "Зарегистрироваться" : "Войти")
                    enabled: !client.authBusy
                    onClicked: root.submit()
                }

                Button {
                    id: signup
                    checkable: true
                    flat: true
                    font.pixelSize: 13
                    contentItem: Text {
                        text: signup.checked ? "Уже есть аккаунт" : "Создать аккаунт"
                        color: Theme.accent
                        font: signup.font
                        horizontalAlignment: Text.AlignHCenter
                    }
                    background: Item {}
                    onToggled: client.authError = ""
                }

                Rectangle {
                    width: parent.width
                    height: serverBox.visible ? serverBox.implicitHeight + 36 : 36
                    radius: 12
                    color: Theme.sidebar

                    Column {
                        anchors.fill: parent
                        anchors.margins: 8
                        spacing: 8

                        Button {
                            width: parent.width
                            height: 28
                            flat: true
                            onClicked: serverBox.visible = !serverBox.visible
                            contentItem: Row {
                                spacing: 8
                                Text {
                                    text: "\uE713"
                                    font.family: Theme.iconFont
                                    font.pixelSize: 14
                                    color: Theme.secondary
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                                Text {
                                    text: "Сервер  " + client.host + ":" + client.port
                                    color: Theme.secondary
                                    font.pixelSize: 13
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                            }
                            background: Item {}
                        }

                        Row {
                            id: serverBox
                            visible: false
                            width: parent.width
                            spacing: 8
                            LineField {
                                id: hostField
                                width: parent.width - portField.width - 8
                                placeholderText: "95.165.30.178"
                                text: client.host
                            }
                            LineField {
                                id: portField
                                width: 90
                                placeholderText: "7410"
                                text: String(client.port)
                                validator: IntValidator { bottom: 1; top: 65535 }
                                inputMethodHints: Qt.ImhDigitsOnly
                            }
                        }
                    }
                }
            }
        }
    }

    function submit() {
        var host = hostField.text.length ? hostField.text : client.host
        var port = parseInt(portField.text, 10)
        if (isNaN(port))
            port = client.port
        client.setEndpoint(host, port)
        if (client.cloudChallenge && client.cloudChallenge.length > 0) {
            client.submitCloudPassword(password.text)
            return
        }
        if (signup.checked) {
            if (password.text !== passwordAgain.text) {
                client.authError = "Пароли не совпадают"
                return
            }
            client.signUp(username.text, password.text, displayName.text)
        } else {
            client.signIn(username.text, password.text)
        }
    }

    Component.onCompleted: username.forceActiveFocus()
}
