import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import rockid

Popup {
    id: sheet
    parent: Overlay.overlay
    modal: true
    focus: true
    padding: 16
    width: Math.min(520, Overlay.overlay ? Overlay.overlay.width - 48 : 520)
    height: Math.min(680, Overlay.overlay ? Overlay.overlay.height - 48 : 680)
    anchors.centerIn: parent
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    property var picked: null
    property string fileKind: "file"
    property bool squareCrop: false
    property int brightness: 0

    background: Rectangle { radius: 16; color: Theme.sidebar; border.color: Theme.line; border.width: 1 }

    function line(title, text) {
        return title + (text ? "\n" + text : "")
    }

    FileDialog {
        id: files
        title: "Файл"
        onAccepted: {
            if (fileKind === "photo")
                client.uploadEditedPhoto(selectedFile, sheet.brightness, sheet.squareCrop, extra.text, spoiler.checked)
            else if (fileKind === "sticker-add")
                client.uploadFile(selectedFile, "sticker-add", extra.text, false)
            else if (fileKind === "story")
                client.uploadFile(selectedFile, "story", extra.text, false)
            else
                client.uploadFile(selectedFile, fileKind, extra.text, spoiler.checked)
        }
    }

    contentItem: Flickable {
        clip: true
        contentWidth: width
        contentHeight: column.implicitHeight
        ScrollBar.vertical: ScrollBar { }
        ColumnLayout {
            id: column
            width: parent.width
            spacing: 8

            Text { text: "Действия"; color: Theme.text; font.pixelSize: 18; font.bold: true }

            TextField { id: extra; placeholderText: "Текст, подпись, тег или цитата"; color: Theme.text; placeholderTextColor: Theme.secondary; background: Rectangle { radius: 8; color: Theme.input } }
            TextField { id: second; placeholderText: "Второе поле: имя, ссылка, пароль"; color: Theme.text; placeholderTextColor: Theme.secondary; background: Rectangle { radius: 8; color: Theme.input } }
            RowLayout {
                CheckBox { id: spoiler; text: "Спойлер"; }
                CheckBox { id: silentBox; text: "Без звука"; onToggled: sheet.parent ? 0 : 0 }
                CheckBox { id: square; text: "Квадрат"; onToggled: sheet.squareCrop = checked }
            }
            Slider { from: -80; to: 80; value: 0; onMoved: sheet.brightness = value }

            Flow {
                Layout.fillWidth: true
                spacing: 6
                Repeater {
                    model: ["👍", "❤", "😂", "🔥", "🎉", "😢", "👎", "👏"]
                    delegate: Button {
                        text: modelData
                        onClicked: if (sheet.picked) client.react(sheet.picked.id, modelData)
                    }
                }
            }

            Button { text: "Ответить"; onClicked: if (sheet.picked) { sheet.replyAsked(sheet.picked.id, sheet.picked.raw || sheet.picked.text); sheet.close() } }
            Button { text: "Изменить"; onClicked: if (sheet.picked) client.editMessage(sheet.picked.id, extra.text) }
            Button { text: "Удалить у всех"; onClicked: if (sheet.picked) client.deleteMessage(sheet.picked.id) }
            Button { text: "Переслать в Избранное"; onClicked: if (sheet.picked && !sheet.picked.no_forward) client.forwardToSaved(sheet.picked.id) }
            Button { text: "Перевести"; onClicked: if (sheet.picked) client.translateMessage(sheet.picked.id) }
            Button { text: "Открыть спойлер"; onClicked: if (sheet.picked) client.reveal(sheet.picked.id) }
            Button { text: "Расшифровка Premium"; onClicked: if (sheet.picked) client.setTranscript(sheet.picked.id, extra.text) }
            Button { text: "Скачать вложение"; onClicked: if (sheet.picked && sheet.picked.file_id) client.downloadFile(sheet.picked.file_id) }
            Button { text: "Тег в Избранном"; onClicked: if (sheet.picked) client.tagSaved(sheet.picked.id, extra.text) }

            Button { text: "Голосовое"; onClicked: client.voiceRecording ? client.stopVoice() : client.startVoice() }
            Button { text: "Видеосообщение из файла"; onClicked: { fileKind = "video_note"; files.open() } }
            Button { text: "Файл до лимита"; onClicked: { fileKind = "file"; files.open() } }
            Button { text: "Фото с правкой"; onClicked: { fileKind = "photo"; files.open() } }
            Button { text: "Стикер из файла"; onClicked: { fileKind = "sticker-add"; files.open() } }

            Button { text: "Избранное"; onClicked: client.openSaved() }
            Button { text: "Секретный чат, таймер 1 час"; onClicked: client.createSecret(Number(extra.text), 3600) }
            Button { text: "Канал"; onClicked: client.createChannel(extra.text) }
            Button { text: "Группа-форум"; onClicked: client.createForum(extra.text) }
            Button { text: "Вступить в канал"; onClicked: client.joinChannel(Number(extra.text)) }
            Button { text: "Тема форума"; onClicked: client.createTopic(extra.text) }
            Button { text: "Архив"; onClicked: client.archiveDialog(true) }
            Button { text: "Закрепить"; onClicked: client.pinDialog(true) }
            Button { text: "Без звука навсегда"; onClicked: client.muteDialog(-1) }
            Button { text: "Выйти из чата"; onClicked: client.leaveDialog() }
            Button { text: "Папка"; onClicked: client.createFolder(extra.text) }
            Button { text: "Чат в папку №"; onClicked: client.addToFolder(Number(extra.text)) }

            Button { text: "История 24 ч"; onClicked: client.postStory(extra.text, 24, "everybody", spoiler.checked, 0) }
            Button { text: "История с файлом"; onClicked: { fileKind = "story"; files.open() } }
            Button { text: "Обновить истории"; onClicked: client.loadStories() }

            Button { text: "Начать эфир"; onClicked: client.startCall() }
            Button { text: "Микрофон выкл"; onClicked: client.callAction("state", client.callInfo.id || 0, true, false, false, false) }
            Button { text: "Камера"; onClicked: client.callAction("state", client.callInfo.id || 0, false, true, false, false) }
            Button { text: "Экран"; onClicked: client.callAction("state", client.callInfo.id || 0, false, false, true, false) }
            Button { text: "Запись эфира в Избранное"; onClicked: client.callAction("record", client.callInfo.id || 0, false, false, false, true) }
            Button { text: "Остановить запись и сохранить"; onClicked: { client.stopVoice(); } }
            Button { text: "Покинуть эфир"; onClicked: client.callAction("leave", client.callInfo.id || 0, false, false, false, false) }

            Button { text: "Сеансы"; onClicked: client.loadSessions() }
            Button { text: "Завершить другие сеансы"; onClicked: client.terminateOthers() }
            Button { text: "Облачный пароль"; onClicked: client.setCloudPassword(extra.text, second.text) }
            Button { text: "Убрать облачный пароль"; onClicked: client.clearCloudPassword(extra.text) }
            Button { text: "Скрыть телефон, онлайн и фото"; onClicked: client.setPrivacy("nobody", "nobody", "nobody") }
            Button { text: "Показать данные всем"; onClicked: client.setPrivacy("everybody", "everybody", "everybody") }
            Button { text: "Код-пароль устройства"; onClicked: client.setPin(extra.text) }
            Button { text: "Запомнить аккаунт"; onClicked: client.rememberAccount(extra.text) }

            Button { text: "Ночь"; onClicked: client.savePreferences("{\"theme\":\"night\",\"radius\":16}") }
            Button { text: "День"; onClicked: client.savePreferences("{\"theme\":\"day\",\"radius\":18}") }
            Button { text: "Иконка синяя"; onClicked: client.setAppIcon("#2AABEE") }
            Button { text: "Иконка зелёная"; onClicked: client.setAppIcon("#2ea043") }

            Button { text: "Создать бота"; onClicked: client.createBot(extra.text, second.text) }
            Button { text: "Войти ботом"; onClicked: client.botSignIn(extra.text) }
            Button { text: "Обновления бота"; onClicked: client.pollBotUpdates() }
            Button { text: "Счёт Premium"; onClicked: client.createInvoice(Number(extra.text), "Premium", 100, "RUB", "premium", second.text) }
            Button { text: "Подтвердить счёт"; onClicked: client.setInvoice(Number(extra.text), true, second.text) }
            Button { text: "Счета"; onClicked: client.loadInvoices() }
            Button { text: "Мини-приложение"; onClicked: client.createApp(extra.text, second.text, "https://example.com") }
            Button { text: "HTML мини-приложения"; onClicked: client.setAppContent(second.text, "<!doctype html><meta charset=utf-8><p>" + extra.text + "</p>") }
            Button { text: "Открыть мини-приложение"; onClicked: client.openMiniApp(second.text) }
            Button { text: "Яркость файла на сервере"; onClicked: client.editServerFile(Number(extra.text), Number(second.text), 0, 0) }
            Button { text: "Список приложений"; onClicked: client.loadApps() }
            Button { text: "Набор стикеров"; onClicked: client.createStickerPack(extra.text, second.text, false) }
            Button { text: "Стикеры"; onClicked: client.loadStickers() }
            Button { text: "Вкладка Избранного"; onClicked: client.createSavedTab(extra.text) }
            Button { text: "Вкладки"; onClicked: client.loadSavedTabs() }

            Text {
                Layout.fillWidth: true
                color: Theme.secondary
                wrapMode: Text.WordWrap
                text: client.boardTitle + (client.board.length ? " (" + client.board.length + ")" : "")
            }
            Repeater {
                model: client.board
                delegate: Text {
                    required property var modelData
                    Layout.fillWidth: true
                    color: Theme.text
                    wrapMode: Text.WordWrap
                    text: (modelData.title || modelData.device || modelData.caption || "") + "  " + (modelData.text || modelData.platform || "")
                    MouseArea {
                        anchors.fill: parent
                        onClicked: {
                            if (modelData.file_id)
                                client.sendSticker(modelData.file_id, modelData.title || "")
                            else if (modelData.text && String(modelData.text).indexOf("http") === 0)
                                Qt.openUrlExternally(modelData.text)
                            else if (modelData.id && client.boardTitle === "Сеансы" && !modelData.current)
                                client.terminateSession(modelData.id)
                        }
                    }
                }
            }
        }
    }

    signal replyAsked(qint64 id, string quote)
}
