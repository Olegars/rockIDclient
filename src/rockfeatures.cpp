#include "rockclient.h"
#include "secretbox.h"

#include <QAudioOutput>
#include <QAudioSink>
#include <QAudioSource>
#include <QNetworkProxy>
#include <QBuffer>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMediaPlayer>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QDesktopServices>
#include <QPainter>
#include <QRandomGenerator>
#include <QSettings>
#include <QUuid>
#include <QStandardPaths>
#include <QTimer>
#include <QSysInfo>
#include <QCryptographicHash>

#include <QtEndian>

#include <cmath>
#include <cstring>
#include <QUrl>
#include <QUrlQuery>
#include <QWindow>

#include <cstring>
#include <memory>

#include <windows.h>

namespace {

QString localPath(const QString &path)
{
    const QUrl url(path);
    if (url.isLocalFile())
        return url.toLocalFile();
    return path;
}

void appendBe16(QByteArray &out, quint16 value)
{
    char bytes[2];
    qToBigEndian(value, bytes);
    out.append(bytes, 2);
}

void appendBe32(QByteArray &out, quint32 value)
{
    char bytes[4];
    qToBigEndian(value, bytes);
    out.append(bytes, 4);
}

void appendBe64(QByteArray &out, qint64 value)
{
    char bytes[8];
    qToBigEndian(value, bytes);
    out.append(bytes, 8);
}

quint32 readBe32(const QByteArray &bytes, int offset)
{
    const uchar *p = reinterpret_cast<const uchar *>(bytes.constData() + offset);
    return qFromBigEndian<quint32>(p);
}

QString spoilers(const QString &text)
{
    QJsonArray arr;
    QJsonObject row;
    row.insert(QStringLiteral("t"), QStringLiteral("spoiler"));
    row.insert(QStringLiteral("o"), 0);
    row.insert(QStringLiteral("l"), text.size());
    arr.append(row);
    return QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

QJsonObject idParam(const char *key, qint64 value)
{
    QJsonObject params;
    params.insert(QString::fromLatin1(key), static_cast<double>(value));
    return params;
}

}  // namespace

extern "C" int crypto_scalarmult_base(unsigned char *q, const unsigned char *n);
bool rockX25519SelfTest();

QString RockClient::deviceName() const
{
    return QSysInfo::machineHostName().left(64);
}

QString RockClient::deviceId() const
{
    QSettings settings(QStringLiteral("rockID"), QStringLiteral("rockIDclient"));
    QString id = settings.value(QStringLiteral("device_id")).toString();
    if (id.size() < 8 || id.size() > 64) {
        id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        settings.setValue(QStringLiteral("device_id"), id);
    }
    return id;
}

void RockClient::armSession()
{
    QJsonObject opt;
    opt.insert(QStringLiteral("compress"), true);
    call(QStringLiteral("session.setOptions"), opt, [this](const QJsonObject &response) {
        m_compress = response.value(QStringLiteral("ok")).toBool()
            && response.value(QStringLiteral("result")).toObject().value(QStringLiteral("compress")).toBool();
    });
    publishIdentity();
}

void RockClient::publishIdentity()
{
    if (!rockX25519SelfTest()) {
        emit userNotice(QStringLiteral("Шифрование X25519 на этом устройстве не прошло проверку"));
        return;
    }
    QSettings settings(QStringLiteral("rockID"), QStringLiteral("rockIDclient"));
    m_xPriv = QByteArray::fromBase64(settings.value(QStringLiteral("x25519")).toByteArray());
    if (m_xPriv.size() != 32) {
        m_xPriv = QByteArray(32, 0);
        QRandomGenerator::system()->fillRange(reinterpret_cast<quint32 *>(m_xPriv.data()), 8);
        settings.setValue(QStringLiteral("x25519"), m_xPriv.toBase64());
    }
    unsigned char pub[32];
    crypto_scalarmult_base(pub, reinterpret_cast<const unsigned char *>(m_xPriv.constData()));
    QJsonObject params;
    params.insert(QStringLiteral("public_key"),
                  QString::fromLatin1(QByteArray(reinterpret_cast<const char *>(pub), 32).toBase64()));
    call(QStringLiteral("keys.publish"), params, [](const QJsonObject &) {});
}

bool RockClient::selfInCall(const QVariantMap &call) const
{
    const QVariantList peers = call.value(QStringLiteral("peers")).toList();
    for (const QVariant &item : peers) {
        if (item.toMap().value(QStringLiteral("user_id")).toLongLong() == m_meId)
            return true;
    }
    return false;
}

void RockClient::noteRoom(const QJsonObject &data, const QString &kind)
{
    const qint64 id = rockJsonInt(data.value(QStringLiteral("id")));
    const bool active = data.value(QStringLiteral("active")).toBool(true);
    const int port = data.value(QStringLiteral("media_port")).toInt();
    if (port > 0)
        m_mediaPort = port;
    const qint64 current = m_call.value(QStringLiteral("id")).toLongLong();
    if (kind == QStringLiteral("call.ended") || !active) {
        if (current == 0 || current == id) {
            m_callLive = false;
            m_callIncoming = false;
            m_ignoredCall = 0;
            closeCallMedia();
            m_call = data.toVariantMap();
            emit callChanged();
        }
        return;
    }
    const QVariantMap room = data.toVariantMap();
    if (selfInCall(room)) {
        m_call = room;
        m_callIncoming = false;
        emit callChanged();
        return;
    }
    if (!m_callLive && id != m_ignoredCall) {
        const bool wasIncoming = m_callIncoming;
        m_call = room;
        m_callIncoming = true;
        emit callChanged();
        if (!wasIncoming)
            emit userNotice(QStringLiteral("Входящий звонок"));
        ensureCallSpeaker();
    }
}

void RockClient::openCallMedia(int port)
{
    closeCallMedia();
    if (port <= 0) {
        m_jsonAudio = true;
        m_mediaReady = true;
        beginCallAudio();
        return;
    }
    if (!m_mediaSocket) {
        m_mediaSocket = new QTcpSocket(this);
        m_mediaSocket->setProxy(QNetworkProxy::NoProxy);
        connect(m_mediaSocket, &QTcpSocket::connected, this, [this] { sendCallHello(); });
        connect(m_mediaSocket, &QTcpSocket::readyRead, this, [this] { readCallMedia(); });
        connect(m_mediaSocket, &QTcpSocket::disconnected, this, [this] {
            m_mediaReady = false;
            stopCallAudio();
        });
        connect(m_mediaSocket, &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError error) {
            if (m_mediaClosing || !m_callLive || error == QAbstractSocket::RemoteHostClosedError)
                return;
            emit userNotice(QStringLiteral("Нет звука звонка"));
        });
    }
    m_mediaReady = false;
    m_mediaBuf.clear();
    m_mediaSocket->connectToHost(m_host, static_cast<quint16>(port));
}

void RockClient::closeCallMedia()
{
    m_mediaReady = false;
    m_jsonAudio = false;
    m_jsonWarned = false;
    stopCallAudio();
    m_mediaBuf.clear();
    if (!m_mediaSocket)
        return;
    m_mediaClosing = true;
    if (m_mediaSocket->state() == QAbstractSocket::ConnectedState) {
        QByteArray bye;
        bye.append(char(3));
        appendBe32(bye, 0);
        m_mediaSocket->write(bye);
        m_mediaSocket->flush();
    }
    m_mediaSocket->abort();
    m_mediaClosing = false;
}

void RockClient::sendCallHello()
{
    if (!m_mediaSocket || !m_callLive)
        return;
    const QByteArray token = m_token.toUtf8();
    if (token.size() < 1 || token.size() > 128) {
        emit userNotice(QStringLiteral("Нет сессии для звонка"));
        return;
    }
    const qint64 callId = m_call.value(QStringLiteral("id")).toLongLong();
    QByteArray frame;
    frame.append(char(1));
    appendBe32(frame, static_cast<quint32>(2 + token.size() + 8));
    appendBe16(frame, static_cast<quint16>(token.size()));
    frame.append(token);
    appendBe64(frame, callId);
    m_mediaSocket->write(frame);
}

void RockClient::readCallMedia()
{
    if (!m_mediaSocket)
        return;
    m_mediaBuf.append(m_mediaSocket->readAll());
    while (m_mediaBuf.size() >= 5) {
        const quint32 len = readBe32(m_mediaBuf, 1);
        if (len > 8192) {
            closeCallMedia();
            return;
        }
        if (m_mediaBuf.size() < 5 + static_cast<int>(len))
            break;
        const uchar op = static_cast<uchar>(m_mediaBuf.at(0));
        const QByteArray payload = m_mediaBuf.mid(5, static_cast<int>(len));
        m_mediaBuf.remove(0, 5 + static_cast<int>(len));
        if (op == 1) {
            m_mediaReady = true;
            beginCallAudio();
        } else if (op == 255) {
            emit userNotice(QStringLiteral("Звонок не принял звук"));
            closeCallMedia();
            return;
        } else if (op == 2 && payload.size() >= 20 && m_callOutput) {
            const QByteArray pcm = payload.mid(20);
            const int even = pcm.size() - (pcm.size() % 2);
            if (even >= 2)
                m_callOutput->write(pcm.constData(), even);
        }
    }
}

void RockClient::beginCallAudio()
{
    if (m_voiceRecording) {
        if (m_mic)
            m_mic->stop();
        if (m_pcm)
            m_pcm->close();
        m_voiceRecording = false;
        m_upRecord = false;
        emit voiceRecordingChanged();
    }
    QAudioFormat format;
    format.setSampleRate(16000);
    format.setChannelCount(1);
    format.setSampleFormat(QAudioFormat::Int16);
    ensureCallSpeaker();
    if (!m_callMic)
        m_callMic = new QAudioSource(format, this);
    if (!m_callInput) {
        m_callInput = m_callMic->start();
        if (!m_callInput)
            emit userNotice(QStringLiteral("Нет микрофона"));
    }
}

void RockClient::stopCallAudio()
{
    if (m_callTimer)
        m_callTimer->stop();
    if (m_callMic)
        m_callMic->stop();
    m_callInput = nullptr;
    if (m_callSpeaker)
        m_callSpeaker->stop();
    m_callOutput = nullptr;
    m_callPending.clear();
    m_ringSample = 0;
}

bool RockClient::callNeedsRing() const
{
    if (m_callIncoming)
        return true;
    if (!m_callLive)
        return false;
    const QVariantList peers = m_call.value(QStringLiteral("peers")).toList();
    for (const QVariant &item : peers) {
        const qint64 id = item.toMap().value(QStringLiteral("user_id")).toLongLong();
        if (id > 0 && id != m_meId)
            return false;
    }
    return true;
}

void RockClient::ensureCallSpeaker()
{
    QAudioFormat format;
    format.setSampleRate(16000);
    format.setChannelCount(1);
    format.setSampleFormat(QAudioFormat::Int16);
    if (!m_callSpeaker) {
        m_callSpeaker = new QAudioSink(format, this);
        m_callSpeaker->setBufferSize(6400);
        m_callSpeaker->setVolume(1.0);
    }
    if (!m_callOutput) {
        m_callOutput = m_callSpeaker->start();
        if (!m_callOutput)
            emit userNotice(QStringLiteral("Нет динамика"));
    }
    if (!m_callTimer) {
        m_callTimer = new QTimer(this);
        connect(m_callTimer, &QTimer::timeout, this, [this] { pumpCallMic(); });
    }
    if (!m_callTimer->isActive())
        m_callTimer->start(40);
}

void RockClient::writeCallRing()
{
    if (!m_callOutput)
        return;
    constexpr int samples = 640;
    QByteArray pcm(samples * 2, 0);
    qint16 *out = reinterpret_cast<qint16 *>(pcm.data());
    const bool incoming = m_callIncoming;
    for (int i = 0; i < samples; ++i) {
        const qint64 t = m_ringSample + i;
        const qint64 cycle = incoming ? 48000 : 80000;
        const qint64 pos = t % cycle;
        const bool on = incoming ? (pos < 6400 || (pos >= 9600 && pos < 16000)) : pos < 16000;
        if (!on)
            continue;
        const double wave = std::sin(2.0 * 3.141592653589793 * 425.0 * static_cast<double>(t % 16000) / 16000.0);
        out[i] = static_cast<qint16>(wave * 14000.0);
    }
    m_ringSample += samples;
    m_callOutput->write(pcm);
}

void RockClient::pumpCallMic()
{
    if (m_callLive && m_mediaReady && m_callInput) {
        m_callPending.append(m_callInput->readAll());
        constexpr int kFrame = 1280;
        while (m_callPending.size() >= kFrame) {
            const QByteArray frame = m_callPending.left(kFrame);
            m_callPending.remove(0, kFrame);
            if (!m_callMuted)
                sendCallPcm(frame);
            if (m_callPending.size() > kFrame)
                m_callPending.remove(0, m_callPending.size() - kFrame);
        }
    }
    if (callNeedsRing())
        writeCallRing();
    else if (!m_callLive)
        stopCallAudio();
}

void RockClient::sendCallPcm(const QByteArray &pcm)
{
    if (!m_mediaReady || pcm.isEmpty() || pcm.size() > 8192)
        return;
    if (m_jsonAudio) {
        const qint64 callId = m_call.value(QStringLiteral("id")).toLongLong();
        if (callId <= 0)
            return;
        QJsonObject params;
        params.insert(QStringLiteral("call_id"), static_cast<double>(callId));
        params.insert(QStringLiteral("kind"), QStringLiteral("audio"));
        params.insert(QStringLiteral("data"), QString::fromLatin1(pcm.toBase64()));
        call(QStringLiteral("calls.media"), params, [this](const QJsonObject &response) {
            if (response.value(QStringLiteral("ok")).toBool() || !m_jsonAudio || m_jsonWarned)
                return;
            m_jsonWarned = true;
            const QString code = rockErrorCode(response);
            if (code == QStringLiteral("USE_MEDIA_SOCKET")) {
                m_jsonAudio = false;
                m_mediaReady = false;
                emit userNotice(QStringLiteral("Сервер ждёт звук на порту 7412, а он закрыт"));
                return;
            }
            emit userNotice(rockHumanize(code));
        });
        return;
    }
    if (!m_mediaSocket)
        return;
    QByteArray frame;
    frame.append(char(2));
    appendBe32(frame, static_cast<quint32>(pcm.size()));
    frame.append(pcm);
    m_mediaSocket->write(frame);
}

void RockClient::onCallMedia(const QJsonObject &data)
{
    const QByteArray raw = QByteArray::fromBase64(data.value(QStringLiteral("data")).toString().toLatin1());
    if (data.value(QStringLiteral("kind")).toString() == QStringLiteral("audio") && m_callLive && raw.size() >= 2) {
        if (!m_callOutput)
            beginCallAudio();
        const int even = raw.size() - (raw.size() % 2);
        if (m_callOutput && even >= 2)
            m_callOutput->write(raw.constData(), even);
        return;
    }
    if (data.value(QStringLiteral("kind")).toString() != QStringLiteral("audio") || raw.size() < 32) {
        emit userNotice(QStringLiteral("Фрагмент эфира"));
        return;
    }
    const QString dest = QStandardPaths::writableLocation(QStandardPaths::TempLocation) + QStringLiteral("/rockid-call.wav");
    QFile file(dest);
    if (!file.open(QIODevice::WriteOnly))
        return;
    const QByteArray pcm = raw.left(256 * 1024);
    QByteArray wav(44 + pcm.size(), 0);
    std::memcpy(wav.data(), "RIFF", 4);
    const quint32 chunk = 36 + static_cast<quint32>(pcm.size());
    std::memcpy(wav.data() + 4, &chunk, 4);
    std::memcpy(wav.data() + 8, "WAVEfmt ", 8);
    const quint32 sub = 16;
    const quint16 audio = 1;
    const quint16 channels = 1;
    const quint32 rate = 16000;
    const quint32 byteRate = 32000;
    const quint16 block = 2;
    const quint16 bits = 16;
    const quint32 dataLen = static_cast<quint32>(pcm.size());
    std::memcpy(wav.data() + 16, &sub, 4);
    std::memcpy(wav.data() + 20, &audio, 2);
    std::memcpy(wav.data() + 22, &channels, 2);
    std::memcpy(wav.data() + 24, &rate, 4);
    std::memcpy(wav.data() + 28, &byteRate, 4);
    std::memcpy(wav.data() + 32, &block, 2);
    std::memcpy(wav.data() + 34, &bits, 2);
    std::memcpy(wav.data() + 36, "data", 4);
    std::memcpy(wav.data() + 40, &dataLen, 4);
    std::memcpy(wav.data() + 44, pcm.constData(), pcm.size());
    file.write(wav);
    file.close();
    playLocal(dest);
}

void RockClient::rpcOk(const QString &method, const QJsonObject &params,
                       const std::function<void(const QJsonObject &)> &ok)
{
    if (!m_connected) {
        emit userNotice(QStringLiteral("Нет соединения"));
        return;
    }
    call(method, params, [this, ok](const QJsonObject &response) {
        if (!response.value(QStringLiteral("ok")).toBool()) {
            const QString code = rockErrorCode(response);
            if (code == QStringLiteral("UNAUTHORIZED") || code == QStringLiteral("SESSION_REVOKED"))
                handleAuthLoss();
            else
                emit userNotice(rockHumanize(code));
            return;
        }
        if (ok)
            ok(response.value(QStringLiteral("result")).toObject());
    });
}

void RockClient::showBoard(const QString &title, const QVariantList &rows)
{
    m_boardTitle = title;
    m_board = rows;
    emit boardChanged();
}

void RockClient::setListMode(const QString &mode)
{
    if (m_listMode == mode)
        return;
    m_listMode = mode;
    emit listModeChanged();
    emit dialogsChanged();
}

bool RockClient::sendRich(const QString &text, qint64 replyTo, const QString &quote, bool silent, qint64 scheduleAt,
                          qint64 topicId, const QString &entities)
{
    return sendDraft(m_activeDialogId, QStringLiteral("text"), text, 0, replyTo, quote, silent, scheduleAt, topicId,
                     entities);
}

bool RockClient::sendDraft(qint64 dialogId, const QString &kind, const QString &text, qint64 fileId, qint64 replyTo,
                           const QString &quote, bool silent, qint64 scheduleAt, qint64 topicId, const QString &entities)
{
    if (dialogId <= 0)
        return false;
    const bool media = kind != QStringLiteral("text") && kind != QStringLiteral("sponsor");
    QString body = text;
    if (!media && body.trimmed().isEmpty())
        return false;
    if (body.toUtf8().size() > (media ? 65536 : 4096)) {
        emit userNotice(QStringLiteral("Слишком длинный текст"));
        return false;
    }
    if (hasForbiddenControls(body, true) || hasForbiddenControls(quote, true)) {
        emit userNotice(QStringLiteral("В тексте есть запрещённый символ"));
        return false;
    }
    if (activeKind() == QStringLiteral("secret") && kind == QStringLiteral("text"))
        ensureSecret(dialogId);
    if (!m_connected) {
        emit userNotice(QStringLiteral("Нет соединения"));
        return false;
    }
    QJsonObject params;
    params.insert(QStringLiteral("dialog_id"), static_cast<double>(dialogId));
    params.insert(QStringLiteral("kind"), kind);
    if (!body.isEmpty())
        params.insert(QStringLiteral("text"), body);
    if (fileId > 0)
        params.insert(QStringLiteral("file_id"), static_cast<double>(fileId));
    if (replyTo > 0)
        params.insert(QStringLiteral("reply_to"), static_cast<double>(replyTo));
    if (!quote.trimmed().isEmpty())
        params.insert(QStringLiteral("quote"), quote.left(256));
    if (silent)
        params.insert(QStringLiteral("silent"), true);
    if (scheduleAt > 0)
        params.insert(QStringLiteral("schedule_at"), static_cast<double>(scheduleAt));
    if (topicId > 0)
        params.insert(QStringLiteral("topic_id"), static_cast<double>(topicId));
    if (!entities.isEmpty())
        params.insert(QStringLiteral("entities"), entities);
    const QString restore = text;
    call(QStringLiteral("messages.send"), params, [this, dialogId, restore](const QJsonObject &response) {
        if (!response.value(QStringLiteral("ok")).toBool()) {
            const QString code = rockErrorCode(response);
            if (code == QStringLiteral("UNAUTHORIZED") || code == QStringLiteral("SESSION_REVOKED"))
                handleAuthLoss();
            else
                emit userNotice(rockHumanize(code));
            emit sendFailed(restore);
            return;
        }
        ingest(messageToMap(response.value(QStringLiteral("result")).toObject()));
    });
    return true;
}

void RockClient::ingest(const QVariantMap &message)
{
    const qint64 dialogId = message.value(QStringLiteral("dialog_id")).toLongLong();
    const qint64 messageId = message.value(QStringLiteral("id")).toLongLong();
    if (dialogId <= 0 || messageId <= 0)
        return;
    auto list = m_store.value(dialogId);
    bool replaced = false;
    for (int i = 0; i < list.size(); ++i) {
        if (list.at(i).toMap().value(QStringLiteral("id")).toLongLong() == messageId) {
            list[i] = message;
            replaced = true;
            break;
        }
    }
    if (!replaced)
        list.append(message);
    m_store.insert(dialogId, rockDedupeSort(list));
    touchLastMessage(dialogId, message, 0);
    if (dialogId == m_activeDialogId) {
        publishActive();
        markRead(dialogId);
        emit messageArrived(message.value(QStringLiteral("sender_id")).toLongLong() == m_meId);
    }
}

void RockClient::eraseMessage(qint64 dialogId, qint64 messageId)
{
    auto list = m_store.value(dialogId);
    QVariantList next;
    for (const QVariant &item : list) {
        if (item.toMap().value(QStringLiteral("id")).toLongLong() != messageId)
            next.append(item);
    }
    m_store.insert(dialogId, next);
    if (dialogId == m_activeDialogId)
        publishActive();
}

void RockClient::editMessage(qint64 messageId, const QString &text)
{
    QJsonObject params = idParam("message_id", messageId);
    params.insert(QStringLiteral("text"), text);
    rpcOk(QStringLiteral("messages.edit"), params, [this](const QJsonObject &result) { ingest(messageToMap(result)); });
}

void RockClient::deleteMessage(qint64 messageId)
{
    rpcOk(QStringLiteral("messages.delete"), idParam("message_id", messageId), [this](const QJsonObject &result) {
        eraseMessage(rockJsonInt(result.value(QStringLiteral("dialog_id"))),
                     rockJsonInt(result.value(QStringLiteral("message_id"))));
    });
}

void RockClient::react(qint64 messageId, const QString &emoji)
{
    QJsonObject params = idParam("message_id", messageId);
    params.insert(QStringLiteral("emoji"), emoji);
    rpcOk(QStringLiteral("messages.react"), params, [this](const QJsonObject &result) {
        const qint64 dialogId = rockJsonInt(result.value(QStringLiteral("dialog_id")));
        const qint64 messageId = rockJsonInt(result.value(QStringLiteral("message_id")));
        const QString emoji = result.value(QStringLiteral("emoji")).toString();
        const bool added = result.value(QStringLiteral("added")).toBool();
        auto list = m_store.value(dialogId);
        for (int i = 0; i < list.size(); ++i) {
            QVariantMap message = list.at(i).toMap();
            if (message.value(QStringLiteral("id")).toLongLong() != messageId)
                continue;
            QVariantList reactions = message.value(QStringLiteral("reactions")).toList();
            bool found = false;
            for (int r = 0; r < reactions.size(); ++r) {
                QVariantMap row = reactions.at(r).toMap();
                if (row.value(QStringLiteral("emoji")).toString() != emoji)
                    continue;
                int count = row.value(QStringLiteral("count")).toInt() + (added ? 1 : -1);
                row.insert(QStringLiteral("count"), qMax(0, count));
                row.insert(QStringLiteral("mine"), added);
                reactions[r] = row;
                found = true;
            }
            if (!found && added) {
                QVariantMap row;
                row.insert(QStringLiteral("emoji"), emoji);
                row.insert(QStringLiteral("count"), 1);
                row.insert(QStringLiteral("mine"), true);
                reactions.append(row);
            }
            message.insert(QStringLiteral("reactions"), reactions);
            list[i] = message;
        }
        m_store.insert(dialogId, list);
        if (dialogId == m_activeDialogId)
            publishActive();
    });
}

void RockClient::forward(qint64 messageId, qint64 dialogId)
{
    QJsonObject params = idParam("message_id", messageId);
    params.insert(QStringLiteral("dialog_id"), static_cast<double>(dialogId));
    rpcOk(QStringLiteral("messages.forward"), params, [this](const QJsonObject &result) {
        ingest(messageToMap(result));
        emit userNotice(QStringLiteral("Переслано"));
    });
}

void RockClient::forwardToSaved(qint64 messageId)
{
    rpcOk(QStringLiteral("dialogs.getSaved"), QJsonObject(), [this, messageId](const QJsonObject &result) {
        forward(messageId, rockJsonInt(result.value(QStringLiteral("id"))));
    });
}

void RockClient::translateMessage(qint64 messageId)
{
    QString source;
    qint64 dialogId = 0;
    for (auto it = m_store.cbegin(); it != m_store.cend(); ++it) {
        for (const QVariant &item : it.value()) {
            const QVariantMap message = item.toMap();
            if (message.value(QStringLiteral("id")).toLongLong() != messageId)
                continue;
            source = secretPlain(it.key(), message.value(QStringLiteral("text")).toString());
            dialogId = it.key();
        }
    }
    if (source.trimmed().isEmpty()) {
        emit userNotice(QStringLiteral("Нечего переводить"));
        return;
    }
    bool cyr = false;
    for (const QChar ch : source) {
        if (ch.unicode() >= 0x0400 && ch.unicode() <= 0x04FF)
            cyr = true;
    }
    const QString pair = cyr ? QStringLiteral("ru|en") : QStringLiteral("en|ru");
    if (!m_net)
        m_net = new QNetworkAccessManager(this);
    QUrl url(QStringLiteral("https://api.mymemory.translated.net/get"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("q"), source.left(450));
    query.addQueryItem(QStringLiteral("langpair"), pair);
    url.setQuery(query);
    QNetworkReply *reply = m_net->get(QNetworkRequest(url));
    connect(reply, &QNetworkReply::finished, this, [this, reply, messageId, pair] {
        const QByteArray payload = reply->readAll();
        reply->deleteLater();
        const QString translated = QJsonDocument::fromJson(payload)
                                       .object()
                                       .value(QStringLiteral("responseData"))
                                       .toObject()
                                       .value(QStringLiteral("translatedText"))
                                       .toString();
        if (translated.isEmpty()) {
            emit userNotice(QStringLiteral("Переводчик не ответил"));
            return;
        }
        QJsonObject params = idParam("message_id", messageId);
        params.insert(QStringLiteral("lang"), pair.section(QLatin1Char('|'), 1, 1));
        params.insert(QStringLiteral("text"), translated.left(4000));
        rpcOk(QStringLiteral("messages.setTranslation"), params, [this, messageId](const QJsonObject &result) {
            const QString text = result.value(QStringLiteral("text")).toString();
            for (auto it = m_store.begin(); it != m_store.end(); ++it) {
                QVariantList list = it.value();
                for (int i = 0; i < list.size(); ++i) {
                    QVariantMap message = list.at(i).toMap();
                    if (message.value(QStringLiteral("id")).toLongLong() != messageId)
                        continue;
                    message.insert(QStringLiteral("translation"), text);
                    list[i] = message;
                }
                m_store.insert(it.key(), list);
            }
            publishActive();
            emit userNotice(text);
        });
    });
}

void RockClient::setTranscript(qint64 messageId, const QString &text)
{
    QJsonObject params = idParam("message_id", messageId);
    params.insert(QStringLiteral("text"), text);
    rpcOk(QStringLiteral("messages.setTranscript"), params, [this, messageId](const QJsonObject &result) {
        const QString text = result.value(QStringLiteral("text")).toString();
        for (auto it = m_store.begin(); it != m_store.end(); ++it) {
            QVariantList list = it.value();
            for (int i = 0; i < list.size(); ++i) {
                QVariantMap message = list.at(i).toMap();
                if (message.value(QStringLiteral("id")).toLongLong() != messageId)
                    continue;
                message.insert(QStringLiteral("transcript"), text);
                list[i] = message;
            }
            m_store.insert(it.key(), list);
        }
        publishActive();
    });
}

void RockClient::uploadFile(const QString &path, const QString &kind, const QString &caption, bool spoiler)
{
    if (m_uploading) {
        emit userNotice(QStringLiteral("Уже идёт загрузка"));
        return;
    }
    const QString file = localPath(path);
    QFileInfo info(file);
    if (!info.exists() || !info.isFile()) {
        emit userNotice(QStringLiteral("Файл не найден"));
        return;
    }
    const qint64 cap = m_me.value(QStringLiteral("limits")).toMap().value(QStringLiteral("file_bytes")).toLongLong();
    const qint64 limit = cap > 0 ? cap : (2LL * 1024 * 1024 * 1024);
    if (info.size() < 1 || info.size() > limit) {
        emit userNotice(QStringLiteral("Размер файла больше лимита"));
        return;
    }
    m_upPath = file;
    m_upKind = kind;
    m_upCaption = caption;
    m_upSpoiler = spoiler;
    m_upSent = 0;
    m_upSize = info.size();
    m_uploading = true;
    QJsonObject params;
    params.insert(QStringLiteral("name"), info.fileName().left(255));
    params.insert(QStringLiteral("mime"), QStringLiteral("application/octet-stream"));
    params.insert(QStringLiteral("size"), static_cast<double>(info.size()));
    rpcOk(QStringLiteral("files.create"), params, [this](const QJsonObject &result) {
        m_upFile = rockJsonInt(result.value(QStringLiteral("id")));
        const qint64 chunk = rockJsonInt(result.value(QStringLiteral("chunk_bytes")));
        if (chunk >= 4096 && chunk <= 700 * 1024)
            m_uploadChunk = static_cast<int>(chunk);
        writeChunk();
    });
}

void RockClient::writeChunk()
{
    QFile file(m_upPath);
    if (!file.open(QIODevice::ReadOnly)) {
        m_uploading = false;
        emit userNotice(QStringLiteral("Не удалось прочитать файл"));
        return;
    }
    file.seek(m_upSent);
    const QByteArray chunk = file.read(m_uploadChunk);
    if (chunk.isEmpty()) {
        finishUpload();
        return;
    }
    QJsonObject params = idParam("file_id", m_upFile);
    params.insert(QStringLiteral("offset"), static_cast<double>(m_upSent));
    params.insert(QStringLiteral("data"), QString::fromLatin1(chunk.toBase64()));
    const qint64 next = m_upSent + chunk.size();
    rpcOk(QStringLiteral("files.write"), params, [this, next](const QJsonObject &) {
        m_upSent = next;
        if (m_upSent >= m_upSize)
            finishUpload();
        else
            writeChunk();
    });
}

void RockClient::finishUpload()
{
    rpcOk(QStringLiteral("files.finish"), idParam("file_id", m_upFile), [this](const QJsonObject &) {
        const qint64 fileId = m_upFile;
        const QString kind = m_upKind;
        const QString caption = m_upCaption;
        const bool spoiler = m_upSpoiler;
        const bool record = m_upRecord;
        m_upRecord = false;
        m_uploading = false;
        if (record) {
            QJsonObject params = idParam("call_id", m_call.value(QStringLiteral("id")).toLongLong());
            params.insert(QStringLiteral("recording"), false);
            params.insert(QStringLiteral("file_id"), static_cast<double>(fileId));
            rpcOk(QStringLiteral("calls.record"), params, [this](const QJsonObject &result) {
                m_call = result.toVariantMap();
                emit callChanged();
                emit userNotice(QStringLiteral("Запись эфира в Избранном"));
            });
            return;
        }
        if (kind == QStringLiteral("story")) {
            postStory(caption, 24, QStringLiteral("everybody"), false, fileId);
            return;
        }
        if (kind == QStringLiteral("sticker-add") && m_packId > 0) {
            QJsonObject params = idParam("pack_id", m_packId);
            params.insert(QStringLiteral("file_id"), static_cast<double>(fileId));
            params.insert(QStringLiteral("emoji"), caption.left(32));
            rpcOk(QStringLiteral("stickers.add"), params, [this](const QJsonObject &) {
                emit userNotice(QStringLiteral("Стикер добавлен"));
                loadStickers();
            });
            return;
        }
        const QString entities = spoiler ? spoilers(caption) : QString();
        sendDraft(m_activeDialogId, kind == QStringLiteral("sticker") ? QStringLiteral("sticker") : kind, caption,
                  fileId, 0, {}, false, 0, 0, entities);
    });
}

void RockClient::uploadEditedPhoto(const QString &path, int brightness, bool square, const QString &overlay, bool spoiler)
{
    QImage image(localPath(path));
    if (image.isNull()) {
        emit userNotice(QStringLiteral("Это не изображение"));
        return;
    }
    image = image.convertToFormat(QImage::Format_ARGB32);
    if (square) {
        const int side = qMin(image.width(), image.height());
        image = image.copy((image.width() - side) / 2, (image.height() - side) / 2, side, side);
    }
    if (brightness != 0) {
        for (int y = 0; y < image.height(); ++y) {
            QRgb *line = reinterpret_cast<QRgb *>(image.scanLine(y));
            for (int x = 0; x < image.width(); ++x) {
                const int r = qBound(0, qRed(line[x]) + brightness, 255);
                const int g = qBound(0, qGreen(line[x]) + brightness, 255);
                const int b = qBound(0, qBlue(line[x]) + brightness, 255);
                line[x] = qRgba(r, g, b, qAlpha(line[x]));
            }
        }
    }
    if (!overlay.trimmed().isEmpty()) {
        QPainter painter(&image);
        painter.setPen(Qt::white);
        painter.setFont(QFont(QStringLiteral("Segoe UI"), qMax(14, image.width() / 18)));
        painter.drawText(image.rect().adjusted(12, 12, -12, -12), Qt::AlignBottom | Qt::AlignLeft, overlay);
    }
    const QString dest = QStandardPaths::writableLocation(QStandardPaths::TempLocation) + QStringLiteral("/rockid-photo.jpg");
    if (!image.save(dest, "JPEG", 85)) {
        emit userNotice(QStringLiteral("Не удалось сохранить изображение"));
        return;
    }
    uploadFile(dest, QStringLiteral("photo"), overlay, spoiler);
}

void RockClient::startVoice()
{
    if (m_voiceRecording)
        return;
    QAudioFormat format;
    format.setSampleRate(16000);
    format.setChannelCount(1);
    format.setSampleFormat(QAudioFormat::Int16);
    if (!m_mic)
        m_mic = new QAudioSource(format, this);
    if (!m_pcm)
        m_pcm = new QBuffer(this);
    m_pcm->buffer().clear();
    m_pcm->open(QIODevice::WriteOnly);
    m_mic->start(m_pcm);
    m_voiceRecording = true;
    emit voiceRecordingChanged();
}

void RockClient::stopVoice()
{
    if (!m_voiceRecording)
        return;
    if (m_mic)
        m_mic->stop();
    m_voiceRecording = false;
    emit voiceRecordingChanged();
    const QByteArray pcm = m_pcm ? m_pcm->buffer() : QByteArray();
    if (m_pcm)
        m_pcm->close();
    if (pcm.size() < 32) {
        emit userNotice(QStringLiteral("Слишком короткая запись"));
        return;
    }
    QByteArray wav;
    wav.resize(44 + pcm.size());
    std::memcpy(wav.data(), "RIFF", 4);
    const quint32 chunk = 36 + static_cast<quint32>(pcm.size());
    std::memcpy(wav.data() + 4, &chunk, 4);
    std::memcpy(wav.data() + 8, "WAVEfmt ", 8);
    const quint32 sub = 16;
    const quint16 audio = 1;
    const quint16 channels = 1;
    const quint32 rate = 16000;
    const quint32 alignBytes = 32000;
    const quint16 block = 2;
    const quint16 bits = 16;
    std::memcpy(wav.data() + 16, &sub, 4);
    std::memcpy(wav.data() + 20, &audio, 2);
    std::memcpy(wav.data() + 22, &channels, 2);
    std::memcpy(wav.data() + 24, &rate, 4);
    std::memcpy(wav.data() + 28, &alignBytes, 4);
    std::memcpy(wav.data() + 32, &block, 2);
    std::memcpy(wav.data() + 34, &bits, 2);
    std::memcpy(wav.data() + 36, "data", 4);
    const quint32 dataLen = static_cast<quint32>(pcm.size());
    std::memcpy(wav.data() + 40, &dataLen, 4);
    std::memcpy(wav.data() + 44, pcm.constData(), pcm.size());
    const QString dest = QStandardPaths::writableLocation(QStandardPaths::TempLocation) + QStringLiteral("/rockid-voice.wav");
    QFile file(dest);
    if (!file.open(QIODevice::WriteOnly))
        return;
    file.write(wav);
    file.close();
    if (m_upRecord) {
        const qint64 callId = m_call.value(QStringLiteral("id")).toLongLong();
        if (callId > 0 && !pcm.isEmpty()) {
            QJsonObject media;
            media.insert(QStringLiteral("call_id"), static_cast<double>(callId));
            media.insert(QStringLiteral("kind"), QStringLiteral("audio"));
            media.insert(QStringLiteral("data"), QString::fromLatin1(pcm.left(256 * 1024).toBase64()));
            call(QStringLiteral("calls.media"), media, [](const QJsonObject &) {});
        }
    }
    uploadFile(dest, QStringLiteral("voice"), QString(), false);
}

void RockClient::playLocal(const QString &path)
{
    if (!m_player) {
        m_player = new QMediaPlayer(this);
        m_audioOut = new QAudioOutput(this);
        m_player->setAudioOutput(m_audioOut);
    }
    m_player->setSource(QUrl::fromLocalFile(localPath(path)));
    m_player->play();
}

void RockClient::downloadFile(qint64 fileId)
{
    const qint64 cap = m_me.value(QStringLiteral("limits")).toMap().value(QStringLiteral("download_chunk")).toLongLong();
    const qint64 chunk = cap > 0 ? cap : 64 * 1024;
    const QString dest = QStandardPaths::writableLocation(QStandardPaths::TempLocation)
        + QStringLiteral("/rockid-%1.bin").arg(fileId);
    QFile::remove(dest);
    auto pull = std::make_shared<std::function<void(qint64)>>();
    *pull = [this, fileId, chunk, dest, pull](qint64 offset) {
        QJsonObject params = idParam("file_id", fileId);
        params.insert(QStringLiteral("offset"), static_cast<double>(offset));
        params.insert(QStringLiteral("length"), static_cast<double>(chunk));
        rpcOk(QStringLiteral("files.read"), params, [this, dest, offset, pull](const QJsonObject &result) {
            const QByteArray bytes = QByteArray::fromBase64(result.value(QStringLiteral("data")).toString().toLatin1());
            QFile file(dest);
            if (!file.open(QIODevice::WriteOnly | QIODevice::Append))
                return;
            file.write(bytes);
            const qint64 next = offset + bytes.size();
            const qint64 size = rockJsonInt(result.value(QStringLiteral("size")));
            if (bytes.isEmpty() || next >= size) {
                emit fileReady(dest);
                emit userNotice(QStringLiteral("Файл сохранён: ") + dest);
                playLocal(dest);
            } else {
                (*pull)(next);
            }
        });
    };
    (*pull)(0);
}

void RockClient::openSaved()
{
    rpcOk(QStringLiteral("dialogs.getSaved"), QJsonObject(), [this](const QJsonObject &result) {
        upsertDialog(result);
        openDialog(rockJsonInt(result.value(QStringLiteral("id"))));
        emit chatOpened();
    });
}

void RockClient::createSecret(qint64 userId, int ttlSeconds)
{
    QJsonObject params = idParam("user_id", userId);
    params.insert(QStringLiteral("ttl_seconds"), ttlSeconds);
    rpcOk(QStringLiteral("dialogs.createSecret"), params, [this](const QJsonObject &result) {
        upsertDialog(result);
        const qint64 id = rockJsonInt(result.value(QStringLiteral("id")));
        openDialog(id);
        ensureSecret(id);
        emit chatOpened();
    });
}

void RockClient::createChannel(const QString &title)
{
    QJsonObject params;
    params.insert(QStringLiteral("title"), title.trimmed());
    rpcOk(QStringLiteral("dialogs.createChannel"), params, [this](const QJsonObject &result) {
        upsertDialog(result);
        openDialog(rockJsonInt(result.value(QStringLiteral("id"))));
        emit chatOpened();
    });
}

void RockClient::createForum(const QString &title)
{
    createGroup(title, {}, true);
}

void RockClient::joinChannel(qint64 dialogId)
{
    rpcOk(QStringLiteral("dialogs.join"), idParam("dialog_id", dialogId), [this](const QJsonObject &result) {
        upsertDialog(result);
        openDialog(rockJsonInt(result.value(QStringLiteral("id"))));
    });
}

void RockClient::leaveDialog()
{
    rpcOk(QStringLiteral("dialogs.leave"), idParam("dialog_id", m_activeDialogId), [this](const QJsonObject &) {
        refreshDialogs();
        emit userNotice(QStringLiteral("Вы вышли из чата"));
    });
}

void RockClient::archiveDialog(bool on)
{
    QJsonObject params = idParam("dialog_id", m_activeDialogId);
    params.insert(QStringLiteral("archived"), on);
    rpcOk(QStringLiteral("dialogs.archive"), params, [this](const QJsonObject &result) {
        upsertDialog(result);
    });
}

void RockClient::pinDialog(bool on)
{
    QJsonObject params = idParam("dialog_id", m_activeDialogId);
    params.insert(QStringLiteral("pinned"), on);
    rpcOk(QStringLiteral("dialogs.pin"), params, [this](const QJsonObject &result) { upsertDialog(result); });
}

void RockClient::muteDialog(qint64 until)
{
    QJsonObject params = idParam("dialog_id", m_activeDialogId);
    params.insert(QStringLiteral("until"), static_cast<double>(until));
    rpcOk(QStringLiteral("dialogs.mute"), params, [this](const QJsonObject &result) { upsertDialog(result); });
}

void RockClient::loadTopics()
{
    rpcOk(QStringLiteral("dialogs.topics"), idParam("dialog_id", m_activeDialogId), [this](const QJsonObject &result) {
        m_topics.clear();
        const QJsonArray rows = result.value(QStringLiteral("topics")).toArray();
        for (const QJsonValue &value : rows)
            m_topics.append(value.toObject().toVariantMap());
        emit topicsChanged();
    });
}

void RockClient::createTopic(const QString &title)
{
    QJsonObject params = idParam("dialog_id", m_activeDialogId);
    params.insert(QStringLiteral("title"), title.trimmed());
    rpcOk(QStringLiteral("dialogs.createTopic"), params, [this](const QJsonObject &) { loadTopics(); });
}

void RockClient::setDiscussion(qint64 groupId)
{
    QJsonObject params;
    params.insert(QStringLiteral("channel_id"), static_cast<double>(m_activeDialogId));
    params.insert(QStringLiteral("group_id"), static_cast<double>(groupId));
    rpcOk(QStringLiteral("dialogs.setDiscussion"), params, [this](const QJsonObject &) {
        emit userNotice(QStringLiteral("Чат комментариев подключён"));
    });
}

void RockClient::kick(qint64 userId)
{
    QJsonObject params = idParam("dialog_id", m_activeDialogId);
    params.insert(QStringLiteral("user_id"), static_cast<double>(userId));
    rpcOk(QStringLiteral("dialogs.kick"), params, [this](const QJsonObject &) { loadMembers(); });
}

void RockClient::ban(qint64 userId, bool on)
{
    QJsonObject params = idParam("dialog_id", m_activeDialogId);
    params.insert(QStringLiteral("user_id"), static_cast<double>(userId));
    rpcOk(on ? QStringLiteral("dialogs.ban") : QStringLiteral("dialogs.unban"), params,
          [this](const QJsonObject &) { loadMembers(); });
}

void RockClient::setAdmin(qint64 userId, bool canDelete, bool canBan, bool canPin, bool canManage, bool canPost)
{
    QJsonObject params = idParam("dialog_id", m_activeDialogId);
    params.insert(QStringLiteral("user_id"), static_cast<double>(userId));
    params.insert(QStringLiteral("can_delete"), canDelete);
    params.insert(QStringLiteral("can_ban"), canBan);
    params.insert(QStringLiteral("can_pin"), canPin);
    params.insert(QStringLiteral("can_manage"), canManage);
    params.insert(QStringLiteral("can_post"), canPost);
    rpcOk(QStringLiteral("dialogs.setAdmin"), params, [this](const QJsonObject &) {
        emit userNotice(QStringLiteral("Права обновлены"));
    });
}

void RockClient::loadFolders()
{
    rpcOk(QStringLiteral("folders.list"), QJsonObject(), [this](const QJsonObject &result) {
        m_folders.clear();
        for (const QJsonValue &value : result.value(QStringLiteral("folders")).toArray())
            m_folders.append(value.toObject().toVariantMap());
        emit foldersChanged();
        emit dialogsChanged();
    });
}

void RockClient::createFolder(const QString &title)
{
    QJsonObject params;
    params.insert(QStringLiteral("title"), title.trimmed());
    rpcOk(QStringLiteral("folders.create"), params, [this](const QJsonObject &) { loadFolders(); });
}

void RockClient::addToFolder(qint64 folderId)
{
    QJsonObject params = idParam("folder_id", folderId);
    params.insert(QStringLiteral("dialog_id"), static_cast<double>(m_activeDialogId));
    rpcOk(QStringLiteral("folders.add"), params, [this](const QJsonObject &) { loadFolders(); });
}

void RockClient::removeFromFolder(qint64 folderId)
{
    QJsonObject params = idParam("folder_id", folderId);
    params.insert(QStringLiteral("dialog_id"), static_cast<double>(m_activeDialogId));
    rpcOk(QStringLiteral("folders.remove"), params, [this](const QJsonObject &) { loadFolders(); });
}

void RockClient::deleteFolder(qint64 folderId)
{
    rpcOk(QStringLiteral("folders.delete"), idParam("folder_id", folderId), [this](const QJsonObject &) { loadFolders(); });
}

void RockClient::loadStories()
{
    rpcOk(QStringLiteral("stories.get"), QJsonObject(), [this](const QJsonObject &result) {
        m_stories.clear();
        for (const QJsonValue &value : result.value(QStringLiteral("stories")).toArray())
            m_stories.append(value.toObject().toVariantMap());
        emit storiesChanged();
    });
}

void RockClient::postStory(const QString &caption, int hours, const QString &privacy, bool noScreenshots, qint64 fileId)
{
    QJsonObject params;
    params.insert(QStringLiteral("hours"), hours);
    params.insert(QStringLiteral("privacy"), privacy);
    params.insert(QStringLiteral("caption"), caption);
    params.insert(QStringLiteral("no_screenshots"), noScreenshots);
    if (fileId > 0)
        params.insert(QStringLiteral("file_id"), static_cast<double>(fileId));
    rpcOk(QStringLiteral("stories.post"), params, [this](const QJsonObject &) { loadStories(); });
}

void RockClient::viewStory(qint64 storyId)
{
    rpcOk(QStringLiteral("stories.view"), idParam("story_id", storyId), [this](const QJsonObject &result) {
        emit userNotice(result.value(QStringLiteral("caption")).toString());
        loadStories();
    });
}

void RockClient::reportStoryScreenshot(qint64 storyId)
{
    rpcOk(QStringLiteral("stories.reportScreenshot"), idParam("story_id", storyId), [](const QJsonObject &) {});
}

void RockClient::reportScreenshot()
{
    if (activeKind() != QStringLiteral("secret"))
        return;
    rpcOk(QStringLiteral("messages.reportScreenshot"), idParam("dialog_id", m_activeDialogId), [](const QJsonObject &) {});
}

void RockClient::loadSessions()
{
    rpcOk(QStringLiteral("account.sessions"), QJsonObject(), [this](const QJsonObject &result) {
        m_sessions.clear();
        for (const QJsonValue &value : result.value(QStringLiteral("sessions")).toArray())
            m_sessions.append(value.toObject().toVariantMap());
        emit sessionsChanged();
        showBoard(QStringLiteral("Сеансы"), m_sessions);
    });
}

void RockClient::terminateSession(qint64 sessionId)
{
    rpcOk(QStringLiteral("account.terminateSession"), idParam("session_id", sessionId), [this](const QJsonObject &) {
        loadSessions();
    });
}

void RockClient::terminateOthers()
{
    rpcOk(QStringLiteral("account.terminateOthers"), QJsonObject(), [this](const QJsonObject &) { loadSessions(); });
}

void RockClient::setCloudPassword(const QString &password, const QString &current)
{
    QJsonObject params;
    params.insert(QStringLiteral("password"), password);
    if (!current.isEmpty())
        params.insert(QStringLiteral("current"), current);
    rpcOk(QStringLiteral("account.setCloudPassword"), params, [this](const QJsonObject &) {
        emit userNotice(QStringLiteral("Облачный пароль включён"));
        refreshProfile();
    });
}

void RockClient::clearCloudPassword(const QString &password)
{
    QJsonObject params;
    params.insert(QStringLiteral("password"), password);
    rpcOk(QStringLiteral("account.clearCloudPassword"), params, [this](const QJsonObject &) {
        emit userNotice(QStringLiteral("Облачный пароль выключен"));
        refreshProfile();
    });
}

void RockClient::setPrivacy(const QString &phone, const QString &lastSeen, const QString &photo)
{
    QJsonObject params;
    params.insert(QStringLiteral("phone"), phone);
    params.insert(QStringLiteral("last_seen"), lastSeen);
    params.insert(QStringLiteral("photo"), photo);
    rpcOk(QStringLiteral("users.setPrivacy"), params, [this](const QJsonObject &) {
        emit userNotice(QStringLiteral("Приватность сохранена"));
        refreshProfile();
    });
}

void RockClient::updateProfile(const QString &displayName, const QString &phone, const QString &bio)
{
    QJsonObject params;
    if (!displayName.trimmed().isEmpty())
        params.insert(QStringLiteral("display_name"), displayName.trimmed());
    params.insert(QStringLiteral("phone"), phone.trimmed());
    params.insert(QStringLiteral("bio"), bio);
    rpcOk(QStringLiteral("users.updateProfile"), params, [this](const QJsonObject &) { refreshProfile(); });
}

void RockClient::setLocalLock(const QString &kind)
{
    QJsonObject params;
    params.insert(QStringLiteral("kind"), kind);
    rpcOk(QStringLiteral("account.setLocalLock"), params, [](const QJsonObject &) {});
}

void RockClient::setPin(const QString &pin)
{
    QSettings settings(QStringLiteral("rockID"), QStringLiteral("rockIDclient"));
    if (pin.isEmpty()) {
        settings.remove(QStringLiteral("pin"));
        m_pinSet = false;
        setLocalLock(QStringLiteral("none"));
    } else {
        settings.setValue(QStringLiteral("pin"), QString::fromLatin1(QCryptographicHash::hash(pin.toUtf8(), QCryptographicHash::Sha256).toHex()));
        m_pinSet = true;
        setLocalLock(QStringLiteral("pin"));
    }
    emit pinChanged();
}

bool RockClient::checkPin(const QString &pin)
{
    QSettings settings(QStringLiteral("rockID"), QStringLiteral("rockIDclient"));
    const QString stored = settings.value(QStringLiteral("pin")).toString();
    const QString hash = QString::fromLatin1(QCryptographicHash::hash(pin.toUtf8(), QCryptographicHash::Sha256).toHex());
    return !stored.isEmpty() && stored == hash;
}

void RockClient::savePreferences(const QString &json)
{
    QJsonObject params;
    params.insert(QStringLiteral("preferences"), json);
    rpcOk(QStringLiteral("account.setPreferences"), params, [this, json](const QJsonObject &) {
        m_me.insert(QStringLiteral("preferences"), json);
        emit meChanged();
    });
}

void RockClient::setAppIcon(const QString &color)
{
    QPixmap pix(64, 64);
    pix.fill(Qt::transparent);
    QPainter painter(&pix);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setBrush(QColor(color));
    painter.setPen(Qt::NoPen);
    painter.drawRoundedRect(4, 4, 56, 56, 16, 16);
    painter.end();
    QGuiApplication::setWindowIcon(QIcon(pix));
}

void RockClient::startCall()
{
    if (m_callLive)
        return;
    if (m_activeDialogId <= 0) {
        emit userNotice(QStringLiteral("Откройте чат"));
        return;
    }
    callAction(QStringLiteral("start"), m_activeDialogId, false, false, false, false);
}

void RockClient::acceptCall()
{
    const qint64 id = m_call.value(QStringLiteral("id")).toLongLong();
    if (id <= 0 || m_callLive)
        return;
    callAction(QStringLiteral("join"), id, false, false, false, false);
}

void RockClient::declineCall()
{
    m_ignoredCall = m_call.value(QStringLiteral("id")).toLongLong();
    m_callIncoming = false;
    stopCallAudio();
    emit callChanged();
}

void RockClient::hangUp()
{
    const qint64 id = m_call.value(QStringLiteral("id")).toLongLong();
    callAction(QStringLiteral("leave"), id, false, false, false, false);
}

void RockClient::toggleCallMute()
{
    if (!m_callLive)
        return;
    const qint64 id = m_call.value(QStringLiteral("id")).toLongLong();
    callAction(QStringLiteral("state"), id, !m_callMuted, false, false, false);
}

void RockClient::callAction(const QString &action, qint64 callId, bool muted, bool video, bool screen, bool recording)
{
    QString method = QStringLiteral("calls.join");
    QJsonObject params;
    const bool joined = action == QStringLiteral("start") || action == QStringLiteral("join");
    if (action == QStringLiteral("start")) {
        method = QStringLiteral("calls.start");
        params.insert(QStringLiteral("dialog_id"), static_cast<double>(callId > 0 ? callId : m_activeDialogId));
    } else if (action == QStringLiteral("leave")) {
        method = QStringLiteral("calls.leave");
        params.insert(QStringLiteral("call_id"), static_cast<double>(callId));
        m_ignoredCall = callId;
        m_callLive = false;
        m_callIncoming = false;
        closeCallMedia();
        emit callChanged();
    } else if (action == QStringLiteral("state")) {
        method = QStringLiteral("calls.setState");
        params.insert(QStringLiteral("call_id"), static_cast<double>(callId));
        params.insert(QStringLiteral("muted"), muted);
        params.insert(QStringLiteral("video"), video);
        params.insert(QStringLiteral("screen"), screen);
        m_callMuted = muted;
        emit callChanged();
    } else if (action == QStringLiteral("record")) {
        method = QStringLiteral("calls.record");
        params.insert(QStringLiteral("call_id"), static_cast<double>(callId));
        params.insert(QStringLiteral("recording"), recording);
    } else {
        params.insert(QStringLiteral("call_id"), static_cast<double>(callId));
    }
    rpcOk(method, params, [this, joined](const QJsonObject &result) {
        m_call = result.toVariantMap();
        const int port = result.value(QStringLiteral("media_port")).toInt();
        if (port > 0)
            m_mediaPort = port;
        const bool active = result.value(QStringLiteral("active")).toBool();
        if (joined) {
            m_callLive = active;
            m_callIncoming = false;
            m_ignoredCall = 0;
            m_callMuted = false;
            emit callChanged();
            if (active)
                openCallMedia(m_mediaPort);
            return;
        }
        if (!active) {
            m_callLive = false;
            m_callIncoming = false;
            closeCallMedia();
        }
        emit callChanged();
    });
}

void RockClient::createBot(const QString &username, const QString &displayName)
{
    QJsonObject params;
    params.insert(QStringLiteral("username"), username.trimmed());
    params.insert(QStringLiteral("display_name"), displayName.trimmed());
    rpcOk(QStringLiteral("bots.create"), params, [this](const QJsonObject &result) {
        QVariantMap row;
        row.insert(QStringLiteral("title"), result.value(QStringLiteral("username")).toString());
        row.insert(QStringLiteral("text"), result.value(QStringLiteral("token")).toString());
        showBoard(QStringLiteral("Бот"), {row});
        emit userNotice(QStringLiteral("Токен бота показан один раз"));
    });
}

void RockClient::botSignIn(const QString &botToken)
{
    rememberAccount(m_me.value(QStringLiteral("username")).toString());
    QJsonObject params;
    params.insert(QStringLiteral("token"), botToken.trimmed());
    rpcOk(QStringLiteral("auth.botSignIn"), params, [this](const QJsonObject &result) {
        applySession(result.value(QStringLiteral("user")).toObject(), result.value(QStringLiteral("token")).toString());
    });
}

void RockClient::pollBotUpdates()
{
    rpcOk(QStringLiteral("bots.getUpdates"), QJsonObject(), [this](const QJsonObject &result) {
        QVariantList rows;
        for (const QJsonValue &value : result.value(QStringLiteral("updates")).toArray()) {
            QVariantMap row;
            row.insert(QStringLiteral("title"), QStringLiteral("update"));
            row.insert(QStringLiteral("text"), QString::fromUtf8(QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact)));
            rows.append(row);
        }
        showBoard(QStringLiteral("Обновления бота"), rows);
    });
}

void RockClient::createInvoice(qint64 userId, const QString &title, qint64 amount, const QString &currency, const QString &purpose, const QString &payUrl)
{
    QJsonObject params;
    params.insert(QStringLiteral("user_id"), static_cast<double>(userId));
    params.insert(QStringLiteral("title"), title.trimmed());
    params.insert(QStringLiteral("amount"), static_cast<double>(amount));
    params.insert(QStringLiteral("currency"), currency.trimmed().toUpper());
    params.insert(QStringLiteral("purpose"), purpose.trimmed());
    if (payUrl.startsWith(QStringLiteral("http://")) || payUrl.startsWith(QStringLiteral("https://")))
        params.insert(QStringLiteral("pay_url"), payUrl.left(512));
    rpcOk(QStringLiteral("payments.createInvoice"), params, [this](const QJsonObject &result) {
        QVariantMap row = result.toVariantMap();
        row.insert(QStringLiteral("title"), result.value(QStringLiteral("title")).toString());
        row.insert(QStringLiteral("text"), result.value(QStringLiteral("status")).toString());
        showBoard(QStringLiteral("Счёт"), {row});
    });
}

void RockClient::setInvoice(qint64 invoiceId, bool confirm, const QString &providerRef)
{
    QJsonObject params = idParam("invoice_id", invoiceId);
    if (!providerRef.trimmed().isEmpty())
        params.insert(QStringLiteral("provider_ref"), providerRef.trimmed());
    rpcOk(confirm ? QStringLiteral("payments.confirm") : QStringLiteral("payments.cancel"), params,
          [this](const QJsonObject &result) {
              emit userNotice(result.value(QStringLiteral("status")).toString());
              loadInvoices();
          });
}

void RockClient::loadInvoices()
{
    rpcOk(QStringLiteral("payments.list"), QJsonObject(), [this](const QJsonObject &result) {
        QVariantList rows;
        for (const QJsonValue &value : result.value(QStringLiteral("invoices")).toArray()) {
            const QJsonObject row = value.toObject();
            QVariantMap item = row.toVariantMap();
            item.insert(QStringLiteral("title"), row.value(QStringLiteral("title")).toString());
            item.insert(QStringLiteral("text"), row.value(QStringLiteral("status")).toString() + QLatin1Char(' ')
                                                   + row.value(QStringLiteral("purpose")).toString());
            rows.append(item);
        }
        showBoard(QStringLiteral("Счета"), rows);
    });
}

void RockClient::createApp(const QString &title, const QString &shortName, const QString &url)
{
    QJsonObject params;
    params.insert(QStringLiteral("title"), title.trimmed());
    params.insert(QStringLiteral("short_name"), shortName.trimmed());
    params.insert(QStringLiteral("url"), url.trimmed());
    rpcOk(QStringLiteral("apps.create"), params, [this](const QJsonObject &) { loadApps(); });
}

void RockClient::setAppContent(const QString &shortName, const QString &html)
{
    QJsonObject params;
    params.insert(QStringLiteral("short_name"), shortName.trimmed());
    params.insert(QStringLiteral("html"), html);
    rpcOk(QStringLiteral("apps.setContent"), params, [this](const QJsonObject &) {
        emit userNotice(QStringLiteral("Страница мини-приложения сохранена"));
    });
}

void RockClient::openMiniApp(const QString &shortName)
{
    const QUrl url(QStringLiteral("http://%1:7411/a/%2").arg(m_host, shortName.trimmed()));
    if (!QDesktopServices::openUrl(url))
        emit userNotice(QStringLiteral("Не удалось открыть мини-приложение"));
}

void RockClient::editServerFile(qint64 fileId, int brightness, int contrast, int rotate)
{
    QJsonObject params = idParam("file_id", fileId);
    params.insert(QStringLiteral("brightness"), brightness);
    params.insert(QStringLiteral("contrast"), contrast);
    params.insert(QStringLiteral("rotate"), rotate);
    rpcOk(QStringLiteral("files.edit"), params, [this](const QJsonObject &result) {
        emit userNotice(QStringLiteral("Новый файл ") + QString::number(rockJsonInt(result.value(QStringLiteral("id")))));
    });
}

void RockClient::loadApps()
{
    rpcOk(QStringLiteral("apps.list"), QJsonObject(), [this](const QJsonObject &result) {
        QVariantList rows;
        for (const QJsonValue &value : result.value(QStringLiteral("apps")).toArray()) {
            const QJsonObject row = value.toObject();
            QVariantMap item = row.toVariantMap();
            item.insert(QStringLiteral("title"), row.value(QStringLiteral("title")).toString());
            item.insert(QStringLiteral("text"), row.value(QStringLiteral("url")).toString());
            rows.append(item);
        }
        showBoard(QStringLiteral("Мини-приложения"), rows);
    });
}

void RockClient::loadStickers()
{
    rpcOk(QStringLiteral("stickers.list"), QJsonObject(), [this](const QJsonObject &result) {
        QVariantList rows;
        for (const QJsonValue &value : result.value(QStringLiteral("packs")).toArray()) {
            const QJsonObject pack = value.toObject();
            if (m_packId == 0)
                m_packId = rockJsonInt(pack.value(QStringLiteral("id")));
            for (const QJsonValue &sticker : pack.value(QStringLiteral("stickers")).toArray()) {
                const QJsonObject item = sticker.toObject();
                QVariantMap row;
                row.insert(QStringLiteral("title"), item.value(QStringLiteral("emoji")).toString());
                row.insert(QStringLiteral("text"), pack.value(QStringLiteral("title")).toString());
                row.insert(QStringLiteral("file_id"), rockJsonInt(item.value(QStringLiteral("file_id"))));
                rows.append(row);
            }
        }
        showBoard(QStringLiteral("Стикеры"), rows);
    });
}

void RockClient::createStickerPack(const QString &title, const QString &shortName, bool premium)
{
    QJsonObject params;
    params.insert(QStringLiteral("title"), title.trimmed());
    params.insert(QStringLiteral("short_name"), shortName.trimmed());
    params.insert(QStringLiteral("premium"), premium);
    rpcOk(QStringLiteral("stickers.createPack"), params, [this](const QJsonObject &result) {
        m_packId = rockJsonInt(result.value(QStringLiteral("id")));
        emit userNotice(QStringLiteral("Набор создан, теперь выберите файл стикера"));
    });
}

void RockClient::sendSticker(qint64 fileId, const QString &emoji)
{
    sendDraft(m_activeDialogId, QStringLiteral("sticker"), emoji, fileId, 0, {}, false, 0, 0, {});
}

void RockClient::loadSavedTabs()
{
    rpcOk(QStringLiteral("saved.tabs"), QJsonObject(), [this](const QJsonObject &result) {
        QVariantList rows;
        for (const QJsonValue &value : result.value(QStringLiteral("tabs")).toArray()) {
            QVariantMap row = value.toObject().toVariantMap();
            row.insert(QStringLiteral("title"), value.toObject().value(QStringLiteral("title")).toString());
            rows.append(row);
        }
        showBoard(QStringLiteral("Вкладки Избранного"), rows);
    });
}

void RockClient::createSavedTab(const QString &title)
{
    QJsonObject params;
    params.insert(QStringLiteral("title"), title.trimmed());
    rpcOk(QStringLiteral("saved.createTab"), params, [this](const QJsonObject &) { loadSavedTabs(); });
}

void RockClient::addSavedToTab(qint64 tabId, qint64 messageId)
{
    QJsonObject params = idParam("tab_id", tabId);
    params.insert(QStringLiteral("message_id"), static_cast<double>(messageId));
    rpcOk(QStringLiteral("saved.addToTab"), params, [this](const QJsonObject &) {
        emit userNotice(QStringLiteral("Добавлено во вкладку"));
    });
}

void RockClient::tagSaved(qint64 messageId, const QString &tag)
{
    QJsonObject params = idParam("message_id", messageId);
    params.insert(QStringLiteral("tag"), tag.trimmed());
    rpcOk(QStringLiteral("saved.tag"), params, [this](const QJsonObject &) {
        emit userNotice(QStringLiteral("Тег сохранён"));
    });
}

void RockClient::loadAccountList()
{
    QSettings settings(QStringLiteral("rockID"), QStringLiteral("rockIDclient"));
    m_accounts.clear();
    const QJsonArray rows = QJsonDocument::fromJson(settings.value(QStringLiteral("accounts")).toByteArray()).array();
    for (const QJsonValue &value : rows)
        m_accounts.append(value.toObject().toVariantMap());
    m_pinSet = settings.contains(QStringLiteral("pin"));
}

void RockClient::rememberAccount(const QString &label)
{
    if (m_token.isEmpty())
        return;
    loadAccountList();
    const int cap = m_me.value(QStringLiteral("limits")).toMap().value(QStringLiteral("accounts")).toInt();
    const int limit = cap > 0 ? cap : 3;
    for (const QVariant &item : m_accounts) {
        if (item.toMap().value(QStringLiteral("token")).toString() == m_token)
            return;
    }
    if (m_accounts.size() >= limit) {
        emit userNotice(QStringLiteral("Лимит аккаунтов на устройстве"));
        return;
    }
    QVariantMap row;
    row.insert(QStringLiteral("label"), label.trimmed().isEmpty() ? m_me.value(QStringLiteral("username")).toString() : label.trimmed());
    row.insert(QStringLiteral("token"), m_token);
    row.insert(QStringLiteral("host"), m_host);
    row.insert(QStringLiteral("port"), m_port);
    m_accounts.append(row);
    QJsonArray rows;
    for (const QVariant &item : m_accounts)
        rows.append(QJsonObject::fromVariantMap(item.toMap()));
    QSettings settings(QStringLiteral("rockID"), QStringLiteral("rockIDclient"));
    settings.setValue(QStringLiteral("accounts"), QJsonDocument(rows).toJson(QJsonDocument::Compact));
    emit accountsChanged();
}

void RockClient::switchAccount(int index)
{
    loadAccountList();
    if (index < 0 || index >= m_accounts.size())
        return;
    const QVariantMap row = m_accounts.at(index).toMap();
    rememberAccount(m_me.value(QStringLiteral("username")).toString());
    ++m_authGen;
    m_token = row.value(QStringLiteral("token")).toString();
    m_host = row.value(QStringLiteral("host")).toString();
    m_port = row.value(QStringLiteral("port")).toInt();
    saveSettings();
    clearChatState();
    m_me.clear();
    m_meId = 0;
    m_authenticated = false;
    m_bindFailed = false;
    emit meChanged();
    emit authenticatedChanged();
    emit endpointChanged();
    connectNow(true);
}

QString RockClient::previewOf(qint64 dialogId, const QString &text) const
{
    const QString plain = secretPlain(dialogId, text);
    if (plain.isEmpty() && (text.startsWith(QStringLiteral("e2e:")) || text.startsWith(QStringLiteral("{\"e\":1"))))
        return QStringLiteral("Секретное сообщение");
    return plain;
}

void RockClient::reveal(qint64 messageId)
{
    m_revealed.insert(messageId);
    publishActive();
}

QString RockClient::secretPlain(qint64 dialogId, const QString &text) const
{
    if (text.startsWith(QStringLiteral("{\"e\":1"))) {
        const QString plain = secretbox::openRk1(m_xPriv, m_meId, text);
        return plain.isEmpty() ? QStringLiteral("Секретное сообщение") : plain;
    }
    if (!text.startsWith(QStringLiteral("e2e:")))
        return text;
    const QByteArray key = secretAes(dialogId);
    if (key.isEmpty())
        return QStringLiteral("Секретное сообщение");
    const QString plain = secretbox::open(key, text);
    return plain.isEmpty() ? QStringLiteral("Секретное сообщение") : plain;
}

QByteArray RockClient::secretAes(qint64 dialogId) const
{
    return m_aes.value(dialogId);
}

void RockClient::ensureSecret(qint64 dialogId)
{
    QSettings settings(QStringLiteral("rockID"), QStringLiteral("rockIDclient"));
    const QString key = QStringLiteral("secret/%1").arg(dialogId);
    secretbox::Identity identity;
    identity.privBlob = QByteArray::fromBase64(settings.value(key).toByteArray());
    identity.publicRaw = QByteArray::fromBase64(settings.value(key + QStringLiteral("/pub")).toByteArray());
    if (identity.privBlob.isEmpty() || identity.publicRaw.size() != 64) {
        if (!secretbox::generate(&identity))
            return;
        settings.setValue(key, identity.privBlob.toBase64());
        settings.setValue(key + QStringLiteral("/pub"), identity.publicRaw.toBase64());
    }
    const QByteArray mine = identity.privBlob;
    QJsonObject params = idParam("dialog_id", dialogId);
    params.insert(QStringLiteral("public_key"), QString::fromLatin1(identity.publicRaw.toBase64()));
    rpcOk(QStringLiteral("dialogs.setSecretKey"), params, [this, dialogId, mine](const QJsonObject &) {
        rpcOk(QStringLiteral("dialogs.secretKeys"), idParam("dialog_id", dialogId), [this, dialogId, mine](const QJsonObject &result) {
            for (const QJsonValue &value : result.value(QStringLiteral("keys")).toArray()) {
                const QJsonObject row = value.toObject();
                if (rockJsonInt(row.value(QStringLiteral("user_id"))) == m_meId)
                    continue;
                const QByteArray peer = QByteArray::fromBase64(row.value(QStringLiteral("public_key")).toString().toLatin1());
                const QByteArray aes = secretbox::agree(mine, peer);
                if (!aes.isEmpty())
                    m_aes.insert(dialogId, aes);
            }
            if (dialogId == m_activeDialogId)
                publishActive();
        });
    });
}

void RockClient::setCaptureBlocked(bool blocked)
{
    const auto windows = QGuiApplication::allWindows();
    for (QWindow *window : windows) {
        const HWND hwnd = reinterpret_cast<HWND>(window->winId());
        if (hwnd)
            SetWindowDisplayAffinity(hwnd, blocked ? 0x00000011 : 0);
    }
}

void RockClient::noteViews(qint64 dialogId)
{
    if (activeKind() != QStringLiteral("channel"))
        return;
    QJsonArray ids;
    for (const QVariant &item : m_store.value(dialogId)) {
        const QVariantMap message = item.toMap();
        if (message.value(QStringLiteral("sender_id")).toLongLong() == m_meId)
            continue;
        ids.append(static_cast<double>(message.value(QStringLiteral("id")).toLongLong()));
        if (ids.size() >= 100)
            break;
    }
    if (ids.isEmpty())
        return;
    QJsonObject params = idParam("dialog_id", dialogId);
    params.insert(QStringLiteral("message_ids"), ids);
    rpcOk(QStringLiteral("messages.view"), params, [](const QJsonObject &) {});
}

void RockClient::refreshProfile()
{
    rpcOk(QStringLiteral("users.getMe"), QJsonObject(), [this](const QJsonObject &result) {
        QVariantMap me = userToMap(result);
        me.insert(QStringLiteral("phone"), result.value(QStringLiteral("phone")).toString());
        me.insert(QStringLiteral("bio"), result.value(QStringLiteral("bio")).toString());
        me.insert(QStringLiteral("premium"), result.value(QStringLiteral("premium")).toBool());
        me.insert(QStringLiteral("premium_until"), rockJsonInt(result.value(QStringLiteral("premium_until"))));
        me.insert(QStringLiteral("cloud_password"), result.value(QStringLiteral("cloud_password")).toBool());
        me.insert(QStringLiteral("preferences"), result.value(QStringLiteral("preferences")).toString());
        me.insert(QStringLiteral("privacy_phone"), result.value(QStringLiteral("privacy_phone")).toString());
        me.insert(QStringLiteral("privacy_last_seen"), result.value(QStringLiteral("privacy_last_seen")).toString());
        me.insert(QStringLiteral("privacy_photo"), result.value(QStringLiteral("privacy_photo")).toString());
        me.insert(QStringLiteral("is_bot"), result.value(QStringLiteral("is_bot")).toBool());
        me.insert(QStringLiteral("limits"), result.value(QStringLiteral("limits")).toObject().toVariantMap());
        const qint64 chunk = rockJsonInt(result.value(QStringLiteral("limits")).toObject().value(QStringLiteral("upload_chunk")));
        if (chunk >= 4096 && chunk <= 700 * 1024)
            m_uploadChunk = static_cast<int>(chunk);
        m_me = me;
        m_meId = me.value(QStringLiteral("id")).toLongLong();
        emit meChanged();
    });
}

void RockClient::submitCloudPassword(const QString &password)
{
    if (m_cloudChallenge.isEmpty())
        return;
    QJsonObject params;
    params.insert(QStringLiteral("challenge"), m_cloudChallenge);
    params.insert(QStringLiteral("password"), password);
    params.insert(QStringLiteral("device"), deviceName());
    params.insert(QStringLiteral("device_id"), deviceId());
    params.insert(QStringLiteral("platform"), QStringLiteral("windows"));
    setAuthBusy(true);
    call(QStringLiteral("auth.checkCloudPassword"), params, [this](const QJsonObject &response) {
        setAuthBusy(false);
        if (!response.value(QStringLiteral("ok")).toBool()) {
            setAuthError(rockHumanize(rockErrorCode(response)));
            return;
        }
        const QJsonObject result = response.value(QStringLiteral("result")).toObject();
        m_cloudChallenge.clear();
        emit cloudChallengeChanged();
        applySession(result.value(QStringLiteral("user")).toObject(), result.value(QStringLiteral("token")).toString());
    });
}
