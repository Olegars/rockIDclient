#include "rockclient.h"

#include <QDateTime>
#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocale>
#include <QNetworkProxy>
#include <QRegularExpression>
#include <QSettings>
#include <QTimer>

#include <QtEndian>

#include <algorithm>
#include <cstring>

extern "C" {
#include "miniz.h"
}

namespace {

constexpr int kHistoryPage = 50;
constexpr int kMaxFrame = 1024 * 1024;
constexpr int kMaxBuffer = 2 * 1024 * 1024;
constexpr int kRequestTimeoutMs = 15000;

QByteArray inflateFrame(const QByteArray &packed)
{
    if (packed.isEmpty())
        return {};
    mz_ulong bound = static_cast<mz_ulong>(packed.size()) * 4;
    if (bound < 64)
        bound = 64;
    for (int attempt = 0; attempt < 8; ++attempt) {
        if (bound > static_cast<mz_ulong>(kMaxFrame))
            bound = static_cast<mz_ulong>(kMaxFrame);
        QByteArray out(static_cast<int>(bound), 0);
        mz_ulong got = bound;
        const int rc = mz_uncompress(reinterpret_cast<unsigned char *>(out.data()), &got,
                                     reinterpret_cast<const unsigned char *>(packed.constData()),
                                     static_cast<mz_ulong>(packed.size()));
        if (rc == MZ_OK) {
            out.resize(static_cast<int>(got));
            return out;
        }
        if (rc != MZ_BUF_ERROR || bound >= static_cast<mz_ulong>(kMaxFrame))
            return {};
        bound *= 2;
    }
    return {};
}

QByteArray deflateFrame(const QByteArray &plain)
{
    if (plain.isEmpty())
        return {};
    mz_ulong bound = mz_compressBound(static_cast<mz_ulong>(plain.size()));
    QByteArray out(static_cast<int>(bound), 0);
    mz_ulong got = bound;
    if (mz_compress(reinterpret_cast<unsigned char *>(out.data()), &got,
                    reinterpret_cast<const unsigned char *>(plain.constData()),
                    static_cast<mz_ulong>(plain.size())) != MZ_OK)
        return {};
    out.resize(static_cast<int>(got));
    return out;
}

qint64 jsonInt(const QJsonValue &value)
{
    if (value.isString())
        return value.toString().toLongLong();
    return static_cast<qint64>(value.toDouble());
}

QString errorCodeOf(const QJsonObject &response)
{
    return response.value(QStringLiteral("error")).toObject().value(QStringLiteral("message")).toString();
}

QString humanize(const QString &code)
{
    static const QHash<QString, QString> text = {
        {QStringLiteral("BAD_JSON"), QStringLiteral("Сервер не принял запрос")},
        {QStringLiteral("BAD_REQUEST"), QStringLiteral("Неверный запрос")},
        {QStringLiteral("BAD_ID"), QStringLiteral("Неверный идентификатор запроса")},
        {QStringLiteral("BAD_METHOD"), QStringLiteral("Не указан метод")},
        {QStringLiteral("BAD_PARAMS"), QStringLiteral("Неверные параметры")},
        {QStringLiteral("BAD_USERNAME"), QStringLiteral("Имя: 3–32 символа, латиница, цифры и _")},
        {QStringLiteral("BAD_PASSWORD"), QStringLiteral("Пароль: от 8 до 128 символов")},
        {QStringLiteral("BAD_DISPLAY_NAME"), QStringLiteral("Имя на экране: 1–64 символа без управляющих")},
        {QStringLiteral("BAD_QUERY"), QStringLiteral("Слишком короткий или длинный запрос")},
        {QStringLiteral("BAD_CONTACT"), QStringLiteral("Себя добавить нельзя")},
        {QStringLiteral("BAD_PEER"), QStringLiteral("Нельзя написать самому себе")},
        {QStringLiteral("BAD_TITLE"), QStringLiteral("Название группы: 1–128 символов")},
        {QStringLiteral("BAD_MEMBERS"), QStringLiteral("Неверный список участников")},
        {QStringLiteral("TOO_MANY_MEMBERS"), QStringLiteral("В группе больше 200000 участников")},
        {QStringLiteral("READ_ONLY"), QStringLiteral("В этом чате нельзя писать")},
        {QStringLiteral("NO_FORWARD"), QStringLiteral("Пересылка запрещена")},
        {QStringLiteral("PREMIUM_REQUIRED"), QStringLiteral("Нужна подписка Premium")},
        {QStringLiteral("NOT_SECRET"), QStringLiteral("Это не секретный чат")},
        {QStringLiteral("NOT_A_CHANNEL"), QStringLiteral("Это не канал")},
        {QStringLiteral("USE_MEDIA_SOCKET"), QStringLiteral("Звук звонка идёт отдельным соединением")},
        {QStringLiteral("NOT_A_FORUM"), QStringLiteral("В этой группе нет тем")},
        {QStringLiteral("FILE_REQUIRED"), QStringLiteral("Нужен файл")},
        {QStringLiteral("FILE_NOT_FOUND"), QStringLiteral("Файл не найден")},
        {QStringLiteral("BAD_SIZE"), QStringLiteral("Неверный размер файла")},
        {QStringLiteral("TOO_MANY_PINS"), QStringLiteral("Слишком много закреплённых чатов")},
        {QStringLiteral("TOO_MANY_FOLDERS"), QStringLiteral("Слишком много папок")},
        {QStringLiteral("TOO_MANY_SUBSCRIPTIONS"), QStringLiteral("Слишком много подписок")},
        {QStringLiteral("SESSION_REVOKED"), QStringLiteral("Сеанс завершён")},
        {QStringLiteral("PAYMENT_DETAILS_REJECTED"), QStringLiteral("Номер карты и ключ кошелька сервер не принимает")},
        {QStringLiteral("NOT_A_BOT"), QStringLiteral("Это может сделать только бот")},
        {QStringLiteral("NOT_A_GROUP"), QStringLiteral("Это личный чат")},
        {QStringLiteral("BAD_TEXT"), QStringLiteral("Текст пустой, длиннее 4096 байт или содержит запрещённый символ")},
        {QStringLiteral("BAD_LIMIT"), QStringLiteral("Неверный размер страницы")},
        {QStringLiteral("BAD_BEFORE"), QStringLiteral("Неверная метка истории")},
        {QStringLiteral("BAD_MAX_ID"), QStringLiteral("Неверная отметка прочтения")},
        {QStringLiteral("UNAUTHORIZED"), QStringLiteral("Нужно войти")},
        {QStringLiteral("INVALID_CREDENTIALS"), QStringLiteral("Неверное имя или пароль")},
        {QStringLiteral("INVALID_TOKEN"), QStringLiteral("Сессия недействительна")},
        {QStringLiteral("NOT_MEMBER"), QStringLiteral("Вы не в этом чате")},
        {QStringLiteral("NOT_OWNER"), QStringLiteral("Это может сделать только владелец группы")},
        {QStringLiteral("UNKNOWN_METHOD"), QStringLiteral("Метод не поддерживается сервером")},
        {QStringLiteral("USER_NOT_FOUND"), QStringLiteral("Пользователь не найден")},
        {QStringLiteral("DIALOG_NOT_FOUND"), QStringLiteral("Чат не найден")},
        {QStringLiteral("USERNAME_TAKEN"), QStringLiteral("Такое имя уже занято")},
        {QStringLiteral("INTERNAL"), QStringLiteral("Ошибка сервера")},
        {QStringLiteral("TIMEOUT"), QStringLiteral("Сервер не ответил")},
        {QStringLiteral("NOT_CONNECTED"), QStringLiteral("Нет соединения")},
    };
    if (text.contains(code))
        return text.value(code);
    if (code.isEmpty())
        return QStringLiteral("Неизвестная ошибка");
    return code;
}

QString membersLabel(int count)
{
    const int n10 = count % 10;
    const int n100 = count % 100;
    QString word;
    if (n10 == 1 && n100 != 11)
        word = QStringLiteral("участник");
    else if (n10 >= 2 && n10 <= 4 && (n100 < 12 || n100 > 14))
        word = QStringLiteral("участника");
    else
        word = QStringLiteral("участников");
    return QString::number(count) + QLatin1Char(' ') + word;
}

QVariantList dedupeSort(QVariantList list)
{
    std::sort(list.begin(), list.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap().value(QStringLiteral("id")).toLongLong()
            < b.toMap().value(QStringLiteral("id")).toLongLong();
    });
    QVariantList out;
    qint64 previous = -1;
    for (const QVariant &item : list) {
        const qint64 id = item.toMap().value(QStringLiteral("id")).toLongLong();
        if (id == previous)
            continue;
        previous = id;
        out.append(item);
    }
    return out;
}

} // namespace

RockClient::RockClient(QObject *parent)
    : QObject(parent)
    , m_socket(new QTcpSocket(this))
    , m_reconnectTimer(new QTimer(this))
    , m_pingTimer(new QTimer(this))
    , m_timeoutTimer(new QTimer(this))
{
    connect(m_socket, &QTcpSocket::connected, this, &RockClient::onConnected);
    connect(m_socket, &QTcpSocket::disconnected, this, &RockClient::onDisconnected);
    connect(m_socket, &QTcpSocket::readyRead, this, &RockClient::onReadyRead);
    connect(m_socket, &QTcpSocket::errorOccurred, this, &RockClient::onSocketError);

    m_reconnectTimer->setSingleShot(true);
    connect(m_reconnectTimer, &QTimer::timeout, this, &RockClient::beginConnect);

    m_pingTimer->setInterval(20000);
    connect(m_pingTimer, &QTimer::timeout, this, &RockClient::sendPing);

    m_timeoutTimer->setInterval(1000);
    connect(m_timeoutTimer, &QTimer::timeout, this, &RockClient::checkTimeouts);
    m_timeoutTimer->start();
}

RockClient::~RockClient()
{
    m_wantConnection = false;
    disconnect(m_socket, nullptr, this, nullptr);
    m_pending.clear();
    m_socket->abort();
}

void RockClient::start()
{
    loadSettings();
    m_wantConnection = true;
    m_bindFailed = m_token.isEmpty();
    emit restoringChanged();
    emit endpointChanged();
    connectNow(false);
    if (!m_expireTimer) {
        m_expireTimer = new QTimer(this);
        connect(m_expireTimer, &QTimer::timeout, this, [this] {
            if (!m_authenticated || m_activeDialogId <= 0)
                return;
            const qint64 now = QDateTime::currentSecsSinceEpoch();
            for (const QVariant &item : m_store.value(m_activeDialogId)) {
                const qint64 exp = item.toMap().value(QStringLiteral("expires_at")).toLongLong();
                if (exp > 0 && now >= exp) {
                    publishActive();
                    return;
                }
            }
        });
        m_expireTimer->start(5000);
    }
}

bool RockClient::restoring() const
{
    return !m_token.isEmpty() && !m_authenticated && !m_bindFailed;
}

QVariantList RockClient::dialogs() const
{
    const QString query = m_dialogFilter.trimmed().toLower();
    const bool archive = m_listMode == QStringLiteral("Архив");
    const bool unreadOnly = m_listMode == QStringLiteral("Непрочитанные");
    const bool custom = m_listMode != QStringLiteral("Все") && !archive && !unreadOnly;
    QSet<qint64> folderIds;
    if (custom) {
        for (const QVariant &folder : m_folders) {
            const QVariantMap row = folder.toMap();
            if (row.value(QStringLiteral("title")).toString() != m_listMode)
                continue;
            for (const QVariant &id : row.value(QStringLiteral("dialog_ids")).toList())
                folderIds.insert(id.toLongLong());
        }
    }
    QVariantList pinned;
    QVariantList rest;
    for (const QVariant &item : m_dialogs) {
        const QVariantMap dialog = item.toMap();
        const bool archived = dialog.value(QStringLiteral("archived")).toBool();
        if (archive) {
            if (!archived)
                continue;
        } else if (archived) {
            continue;
        }
        if (unreadOnly && dialog.value(QStringLiteral("unread")).toInt() <= 0)
            continue;
        if (custom && !folderIds.contains(dialog.value(QStringLiteral("id")).toLongLong()))
            continue;
        if (!query.isEmpty()) {
            const QVariantMap peer = dialog.value(QStringLiteral("peer")).toMap();
            const QString blob = (dialog.value(QStringLiteral("title")).toString() + QLatin1Char(' ')
                                  + peer.value(QStringLiteral("display_name")).toString() + QLatin1Char(' ')
                                  + peer.value(QStringLiteral("username")).toString())
                                     .toLower();
            if (!blob.contains(query))
                continue;
        }
        if (dialog.value(QStringLiteral("pinned")).toBool())
            pinned.append(item);
        else
            rest.append(item);
    }
    pinned.append(rest);
    return pinned;
}

void RockClient::setDialogFilter(const QString &text)
{
    if (m_dialogFilter == text)
        return;
    m_dialogFilter = text;
    emit dialogFilterChanged();
    emit dialogsChanged();
}

QVariantMap RockClient::activeDialog() const
{
    const int index = dialogIndex(m_activeDialogId);
    if (index < 0)
        return {};
    return m_dialogs.at(index).toMap();
}

QString RockClient::activeTitle() const
{
    const QVariantMap dialog = activeDialog();
    if (dialog.isEmpty())
        return {};
    if (dialog.value(QStringLiteral("type")).toString() == QStringLiteral("group"))
        return dialog.value(QStringLiteral("title")).toString();
    const QVariantMap peer = dialog.value(QStringLiteral("peer")).toMap();
    const QString display = peer.value(QStringLiteral("display_name")).toString();
    if (!display.isEmpty())
        return display;
    return peer.value(QStringLiteral("username")).toString();
}

QString RockClient::activeHandle() const
{
    return activeDialog().value(QStringLiteral("peer")).toMap().value(QStringLiteral("username")).toString();
}

QString RockClient::activeStatus() const
{
    const QVariantMap dialog = activeDialog();
    if (dialog.isEmpty())
        return {};
    if (dialog.value(QStringLiteral("type")).toString() == QStringLiteral("group"))
        return membersLabel(dialog.value(QStringLiteral("member_count")).toInt());
    if (dialog.value(QStringLiteral("peer")).toMap().value(QStringLiteral("online")).toBool())
        return QStringLiteral("в сети");
    return QStringLiteral("не в сети");
}

QString RockClient::activeKind() const
{
    return activeDialog().value(QStringLiteral("type")).toString();
}

bool RockClient::activeOnline() const
{
    return activeDialog().value(QStringLiteral("peer")).toMap().value(QStringLiteral("online")).toBool();
}

qint64 RockClient::activePeerId() const
{
    return activeDialog().value(QStringLiteral("peer")).toMap().value(QStringLiteral("id")).toLongLong();
}

qint64 RockClient::activeSeed() const
{
    const qint64 peer = activePeerId();
    return peer > 0 ? peer : m_activeDialogId;
}

bool RockClient::activeIsContact() const
{
    const qint64 peer = activePeerId();
    if (peer <= 0)
        return false;
    for (const QVariant &item : m_contacts) {
        if (item.toMap().value(QStringLiteral("id")).toLongLong() == peer)
            return true;
    }
    return false;
}

int RockClient::unreadTotal() const
{
    int total = 0;
    for (const QVariant &item : m_dialogs)
        total += item.toMap().value(QStringLiteral("unread")).toInt();
    return total;
}

bool RockClient::canLoadOlder() const
{
    if (m_activeDialogId <= 0)
        return false;
    return m_hasMore.value(m_activeDialogId, false);
}

int RockClient::utf8Size(const QString &text) const
{
    return text.toUtf8().size();
}

QString RockClient::formatListTime(qint64 unixSecs) const
{
    if (unixSecs <= 0)
        return {};
    const QDateTime stamp = QDateTime::fromSecsSinceEpoch(unixSecs);
    const QDate date = stamp.date();
    const QDate today = QDate::currentDate();
    if (date == today)
        return stamp.toString(QStringLiteral("HH:mm"));
    if (date == today.addDays(-1))
        return QStringLiteral("вчера");
    if (date.year() == today.year())
        return stamp.toString(QStringLiteral("dd.MM"));
    return stamp.toString(QStringLiteral("dd.MM.yy"));
}

QString RockClient::userName(qint64 userId) const
{
    if (userId == m_meId && !m_me.isEmpty()) {
        const QString display = m_me.value(QStringLiteral("display_name")).toString();
        if (!display.isEmpty())
            return display;
        return m_me.value(QStringLiteral("username")).toString();
    }
    const auto it = m_users.constFind(userId);
    if (it == m_users.cend())
        return {};
    const QString display = it->value(QStringLiteral("display_name")).toString();
    if (!display.isEmpty())
        return display;
    return it->value(QStringLiteral("username")).toString();
}

QString RockClient::dayLabel(qint64 unixSecs) const
{
    const QDate date = QDateTime::fromSecsSinceEpoch(unixSecs).date();
    const QDate today = QDate::currentDate();
    if (date == today)
        return QStringLiteral("Сегодня");
    if (date == today.addDays(-1))
        return QStringLiteral("Вчера");
    const QLocale locale(QLocale::Russian);
    if (date.year() == today.year())
        return locale.toString(date, QStringLiteral("d MMMM"));
    return locale.toString(date, QStringLiteral("d MMMM yyyy"));
}

void RockClient::loadSettings()
{
    QSettings settings(QStringLiteral("rockID"), QStringLiteral("rockIDclient"));
    const QString storedHost = settings.value(QStringLiteral("host"), m_host).toString().trimmed();
    const bool local = storedHost.isEmpty()
        || storedHost == QStringLiteral("127.0.0.1")
        || storedHost == QStringLiteral("192.168.222.2")
        || storedHost.compare(QStringLiteral("localhost"), Qt::CaseInsensitive) == 0;
    m_host = local ? QStringLiteral("95.165.30.178") : storedHost;
    m_port = settings.value(QStringLiteral("port"), m_port).toInt();
    if (m_port < 1 || m_port > 65535)
        m_port = 7410;
    m_token = settings.value(QStringLiteral("token")).toString();
    if (local && storedHost != m_host)
        saveSettings();
    loadAccountList();
}

void RockClient::saveSettings() const
{
    QSettings settings(QStringLiteral("rockID"), QStringLiteral("rockIDclient"));
    settings.setValue(QStringLiteral("host"), m_host);
    settings.setValue(QStringLiteral("port"), m_port);
    settings.setValue(QStringLiteral("token"), m_token);
}

void RockClient::setEndpoint(const QString &host, int port)
{
    QString nextHost = host.trimmed();
    if (nextHost.isEmpty() || nextHost == QStringLiteral("127.0.0.1")
        || nextHost.compare(QStringLiteral("localhost"), Qt::CaseInsensitive) == 0)
        nextHost = QStringLiteral("95.165.30.178");
    if (port < 1 || port > 65535)
        port = 7410;
    const bool changed = nextHost != m_host || port != m_port;
    m_host = nextHost;
    m_port = port;
    saveSettings();
    emit endpointChanged();
    if (changed || m_socket->state() == QAbstractSocket::UnconnectedState)
        connectNow(changed);
}

void RockClient::setLink(const QString &state, const QString &line)
{
    if (m_linkState != state) {
        m_linkState = state;
        emit linkStateChanged();
    }
    if (m_statusLine != line) {
        m_statusLine = line;
        emit statusLineChanged();
    }
}

void RockClient::setAuthError(const QString &text)
{
    if (m_authError == text)
        return;
    m_authError = text;
    emit authErrorChanged();
}

void RockClient::setAuthBusy(bool busy)
{
    if (m_authBusy == busy)
        return;
    m_authBusy = busy;
    emit authBusyChanged();
}

void RockClient::connectNow(bool force)
{
    m_reconnectTimer->stop();
    const auto state = m_socket->state();
    if (!force && (state == QAbstractSocket::ConnectedState || state == QAbstractSocket::ConnectingState))
        return;
    if (state != QAbstractSocket::UnconnectedState) {
        m_connectAfterAbort = true;
        m_socket->abort();
        return;
    }
    beginConnect();
}

void RockClient::beginConnect()
{
    if (m_socket->state() != QAbstractSocket::UnconnectedState)
        return;
    m_buffer.clear();
    QNetworkProxy::setApplicationProxy(QNetworkProxy::NoProxy);
    m_socket->setProxy(QNetworkProxy::NoProxy);
    m_socket->setSocketOption(QAbstractSocket::LowDelayOption, 1);
    m_socket->setSocketOption(QAbstractSocket::KeepAliveOption, 1);
    const QString line = m_authenticated ? QStringLiteral("Повторяем соединение…")
                                         : QStringLiteral("Подключение…");
    setLink(QStringLiteral("connecting"), line);
    m_socket->connectToHost(m_host, static_cast<quint16>(m_port));
}

void RockClient::onConnected()
{
    m_connected = true;
    m_retryMs = 1000;
    emit connectedChanged();
    setLink(QStringLiteral("online"), QStringLiteral("Сервер доступен"));
    m_pingTimer->start();
    if (m_queued == QueuedAuth::SignIn)
        doSignIn();
    else if (m_queued == QueuedAuth::SignUp)
        doSignUp();
    else if (!m_token.isEmpty())
        doBind();
}

void RockClient::onDisconnected()
{
    m_pingTimer->stop();
    if (m_callLive || m_callIncoming) {
        m_callLive = false;
        m_callIncoming = false;
        closeCallMedia();
        emit callChanged();
    } else {
        closeCallMedia();
    }
    if (m_connected) {
        m_connected = false;
        emit connectedChanged();
    }
    m_compress = false;
    m_buffer.clear();
    failPending(QStringLiteral("NOT_CONNECTED"));
    if (m_connectAfterAbort) {
        m_connectAfterAbort = false;
        beginConnect();
        return;
    }
    if (!m_wantConnection) {
        setLink(QStringLiteral("offline"), QStringLiteral("Нет соединения"));
        return;
    }
    const QString line = m_authenticated ? QStringLiteral("Нет связи, повторяем…")
                                         : QStringLiteral("Нет соединения");
    setLink(m_authenticated ? QStringLiteral("reconnecting") : QStringLiteral("offline"), line);
    m_reconnectTimer->start(m_retryMs);
    m_retryMs = qMin(m_retryMs * 2, 15000);
}

void RockClient::onSocketError(QAbstractSocket::SocketError)
{
    if (m_socket->state() == QAbstractSocket::ConnectedState)
        return;
    const QString line = QStringLiteral("Нет соединения: ") + m_socket->errorString();
    if (m_statusLine != line) {
        m_statusLine = line;
        emit statusLineChanged();
    }
    qWarning().noquote() << "rockID:" << m_socket->errorString();
}

void RockClient::onReadyRead()
{
    m_buffer.append(m_socket->readAll());
    if (m_buffer.size() > kMaxBuffer) {
        qWarning().noquote() << "rockID: input buffer overflow";
        m_socket->abort();
        return;
    }
    while (m_buffer.size() >= 4) {
        quint32 raw = 0;
        memcpy(&raw, m_buffer.constData(), 4);
        raw = qFromBigEndian(raw);
        const bool packed = (raw & 0x80000000u) != 0;
        const quint32 length = raw & 0x7fffffffu;
        if (length == 0 || length > static_cast<quint32>(kMaxFrame)) {
            qWarning().noquote() << "rockID: frame too large";
            m_socket->abort();
            return;
        }
        if (m_buffer.size() < 4 + static_cast<int>(length))
            break;
        QByteArray payload = m_buffer.mid(4, static_cast<int>(length));
        m_buffer.remove(0, 4 + static_cast<int>(length));
        if (packed) {
            payload = inflateFrame(payload);
            if (payload.isEmpty()) {
                m_socket->abort();
                break;
            }
        }
        if (!payload.isEmpty())
            handleFrame(payload);
    }
}

void RockClient::sendFrame(const QJsonObject &object)
{
    QByteArray payload = QJsonDocument(object).toJson(QJsonDocument::Compact);
    if (payload.size() > kMaxFrame)
        return;
    quint32 flags = 0;
    if (m_compress) {
        const QByteArray packed = deflateFrame(payload);
        if (!packed.isEmpty() && packed.size() < payload.size()) {
            payload = packed;
            flags = 0x80000000u;
        }
    }
    QByteArray frame(4, 0);
    qToBigEndian(static_cast<quint32>(payload.size()) | flags, frame.data());
    frame.append(payload);
    m_socket->write(frame);
    m_socket->flush();
}

void RockClient::call(const QString &method, const QJsonObject &params,
                      const std::function<void(const QJsonObject &)> &callback)
{
    if (m_socket->state() != QAbstractSocket::ConnectedState) {
        QJsonObject response;
        response.insert(QStringLiteral("ok"), false);
        QJsonObject error;
        error.insert(QStringLiteral("message"), QStringLiteral("NOT_CONNECTED"));
        response.insert(QStringLiteral("error"), error);
        QTimer::singleShot(0, this, [callback, response] { callback(response); });
        return;
    }
    const qint64 id = ++m_nextId;
    QJsonObject request;
    request.insert(QStringLiteral("id"), static_cast<double>(id));
    request.insert(QStringLiteral("method"), method);
    request.insert(QStringLiteral("params"), params);
    PendingCall pending;
    pending.method = method;
    pending.deadline = QDateTime::currentMSecsSinceEpoch() + kRequestTimeoutMs;
    pending.callback = callback;
    m_pending.insert(id, pending);
    sendFrame(request);
}

void RockClient::failPending(const QString &code)
{
    const auto pending = m_pending;
    m_pending.clear();
    for (auto it = pending.cbegin(); it != pending.cend(); ++it) {
        QJsonObject response;
        response.insert(QStringLiteral("ok"), false);
        QJsonObject error;
        error.insert(QStringLiteral("message"), code);
        response.insert(QStringLiteral("error"), error);
        const auto callback = it->callback;
        QTimer::singleShot(0, this, [callback, response] { callback(response); });
    }
}

void RockClient::checkTimeouts()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    QList<qint64> expired;
    bool pingExpired = false;
    for (auto it = m_pending.cbegin(); it != m_pending.cend(); ++it) {
        if (it->deadline > 0 && it->deadline <= now) {
            expired.append(it.key());
            if (it->method == QStringLiteral("ping"))
                pingExpired = true;
        }
    }
    for (qint64 id : expired) {
        const PendingCall pending = m_pending.take(id);
        QJsonObject response;
        response.insert(QStringLiteral("ok"), false);
        QJsonObject error;
        error.insert(QStringLiteral("message"), QStringLiteral("TIMEOUT"));
        response.insert(QStringLiteral("error"), error);
        const auto callback = pending.callback;
        QTimer::singleShot(0, this, [callback, response] { callback(response); });
    }
    if (pingExpired && m_socket->state() == QAbstractSocket::ConnectedState)
        m_socket->abort();

    if (m_authBusy && m_queued != QueuedAuth::None && m_authDeadline > 0 && now > m_authDeadline
        && m_socket->state() != QAbstractSocket::ConnectedState) {
        m_queued = QueuedAuth::None;
        m_qPass.clear();
        setAuthBusy(false);
        setAuthError(QStringLiteral("Не удалось подключиться к серверу"));
    }
}

void RockClient::sendPing()
{
    if (m_socket->state() != QAbstractSocket::ConnectedState)
        return;
    for (const PendingCall &pending : m_pending) {
        if (pending.method == QStringLiteral("ping"))
            return;
    }
    call(QStringLiteral("ping"), QJsonObject(), [](const QJsonObject &) {});
}

void RockClient::handleFrame(const QByteArray &payload)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return;
    const QJsonObject object = document.object();
    if (object.contains(QStringLiteral("update"))) {
        handleUpdate(object.value(QStringLiteral("update")).toString(),
                     object.value(QStringLiteral("data")).toObject());
        return;
    }
    const qint64 id = jsonInt(object.value(QStringLiteral("id")));
    const auto it = m_pending.find(id);
    if (it == m_pending.end())
        return;
    const PendingCall pending = it.value();
    m_pending.erase(it);
    pending.callback(object);
}

void RockClient::handleUpdate(const QString &type, const QJsonObject &data)
{
    if (type == QStringLiteral("message.new")) {
        const QVariantMap message = messageToMap(data);
        const qint64 dialogId = message.value(QStringLiteral("dialog_id")).toLongLong();
        const qint64 senderId = message.value(QStringLiteral("sender_id")).toLongLong();
        const qint64 messageId = message.value(QStringLiteral("id")).toLongLong();
        auto list = m_store.value(dialogId);
        list.append(message);
        m_store.insert(dialogId, dedupeSort(list));
        if (dialogIndex(dialogId) < 0) {
            refreshDialogs();
        } else {
            const bool viewing = dialogId == m_activeDialogId;
            const int delta = (!viewing && senderId != m_meId) ? 1 : 0;
            touchLastMessage(dialogId, message, delta);
        }
        if (dialogId == m_activeDialogId) {
            publishActive();
            markRead(dialogId);
            emit messageArrived(senderId == m_meId);
        }
        const bool silent = message.value(QStringLiteral("silent")).toBool();
        bool muted = false;
        const int index = dialogIndex(dialogId);
        if (index >= 0) {
            const qint64 until = m_dialogs.at(index).toMap().value(QStringLiteral("muted_until")).toLongLong();
            const qint64 now = QDateTime::currentSecsSinceEpoch();
            muted = until == -1 || (until > 0 && until > now);
        }
        if (dialogId != m_activeDialogId && senderId != m_meId && !silent && !muted)
            emit userNotice(previewOf(dialogId, message.value(QStringLiteral("text")).toString()).left(80));
        if (senderId != m_meId && messageId > 0) {
            QJsonObject ack;
            ack.insert(QStringLiteral("message_id"), static_cast<double>(messageId));
            call(QStringLiteral("messages.ack"), ack, [](const QJsonObject &) {});
        }
        return;
    }
    if (type == QStringLiteral("message.delivered")) {
        const qint64 messageId = jsonInt(data.value(QStringLiteral("message_id")));
        if (messageId > 0)
            m_delivered.insert(messageId);
        if (jsonInt(data.value(QStringLiteral("dialog_id"))) == m_activeDialogId)
            publishActive();
        return;
    }
    if (type == QStringLiteral("call.media")) {
        onCallMedia(data);
        return;
    }
    if (type == QStringLiteral("call.ring") || type == QStringLiteral("call.ended") || type == QStringLiteral("call.update")) {
        noteRoom(data, type);
        return;
    }
    if (type == QStringLiteral("message.edit")) {
        ingest(messageToMap(data));
        return;
    }
    if (type == QStringLiteral("message.deleted")) {
        eraseMessage(jsonInt(data.value(QStringLiteral("dialog_id"))), jsonInt(data.value(QStringLiteral("message_id"))));
        return;
    }
    if (type == QStringLiteral("message.reaction")) {
        QJsonObject fake;
        fake.insert(QStringLiteral("dialog_id"), data.value(QStringLiteral("dialog_id")));
        fake.insert(QStringLiteral("message_id"), data.value(QStringLiteral("message_id")));
        fake.insert(QStringLiteral("emoji"), data.value(QStringLiteral("emoji")));
        fake.insert(QStringLiteral("added"), data.value(QStringLiteral("added")));
        const qint64 dialogId = jsonInt(data.value(QStringLiteral("dialog_id")));
        const qint64 messageId = jsonInt(data.value(QStringLiteral("message_id")));
        const QString emoji = data.value(QStringLiteral("emoji")).toString();
        const bool added = data.value(QStringLiteral("added")).toBool();
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
                row.insert(QStringLiteral("count"), qMax(0, row.value(QStringLiteral("count")).toInt() + (added ? 1 : -1)));
                reactions[r] = row;
                found = true;
            }
            if (!found && added) {
                QVariantMap row;
                row.insert(QStringLiteral("emoji"), emoji);
                row.insert(QStringLiteral("count"), 1);
                row.insert(QStringLiteral("mine"), false);
                reactions.append(row);
            }
            message.insert(QStringLiteral("reactions"), reactions);
            list[i] = message;
        }
        m_store.insert(dialogId, list);
        if (dialogId == m_activeDialogId)
            publishActive();
        Q_UNUSED(fake)
        return;
    }
    if (type == QStringLiteral("screenshot") || type == QStringLiteral("story.screenshot")) {
        emit userNotice(QStringLiteral("Собеседник сделал скриншот"));
        return;
    }
    if (type == QStringLiteral("dialog.new")) {
        upsertDialog(data);
        return;
    }
    if (type == QStringLiteral("dialog.member")) {
        const qint64 dialogId = jsonInt(data.value(QStringLiteral("dialog_id")));
        cacheUser(userToMap(data.value(QStringLiteral("user")).toObject()));
        if (m_membersDialogId == dialogId)
            loadMembers();
        return;
    }
    if (type == QStringLiteral("presence")) {
        const qint64 userId = jsonInt(data.value(QStringLiteral("user_id")));
        const bool online = data.value(QStringLiteral("online")).toBool();
        if (m_users.contains(userId)) {
            QVariantMap user = m_users.value(userId);
            user.insert(QStringLiteral("online"), online);
            m_users.insert(userId, user);
        }
        bool dialogsChangedLocal = false;
        for (int i = 0; i < m_dialogs.size(); ++i) {
            QVariantMap dialog = m_dialogs.at(i).toMap();
            QVariantMap peer = dialog.value(QStringLiteral("peer")).toMap();
            if (peer.value(QStringLiteral("id")).toLongLong() != userId)
                continue;
            peer.insert(QStringLiteral("online"), online);
            dialog.insert(QStringLiteral("peer"), peer);
            m_dialogs[i] = dialog;
            dialogsChangedLocal = true;
        }
        bool contactsChangedLocal = false;
        for (int i = 0; i < m_contacts.size(); ++i) {
            QVariantMap contact = m_contacts.at(i).toMap();
            if (contact.value(QStringLiteral("id")).toLongLong() != userId)
                continue;
            contact.insert(QStringLiteral("online"), online);
            m_contacts[i] = contact;
            contactsChangedLocal = true;
        }
        bool membersChangedLocal = false;
        for (int i = 0; i < m_members.size(); ++i) {
            QVariantMap member = m_members.at(i).toMap();
            if (member.value(QStringLiteral("id")).toLongLong() != userId)
                continue;
            member.insert(QStringLiteral("online"), online);
            m_members[i] = member;
            membersChangedLocal = true;
        }
        if (dialogsChangedLocal)
            notifyDialogs();
        if (contactsChangedLocal) {
            emit contactsChanged();
            emit profileChanged();
        }
        if (membersChangedLocal)
            emit membersChanged();
    }
}

void RockClient::signIn(const QString &username, const QString &password)
{
    const QString nameError = validateUsername(username.trimmed());
    if (!nameError.isEmpty()) {
        setAuthError(nameError);
        return;
    }
    const QString passError = validatePassword(password);
    if (!passError.isEmpty()) {
        setAuthError(passError);
        return;
    }
    m_qUser = username.trimmed();
    m_qPass = password;
    m_qName.clear();
    m_queued = QueuedAuth::SignIn;
    ++m_authGen;
    m_bindFailed = false;
    m_authDeadline = QDateTime::currentMSecsSinceEpoch() + 12000;
    setAuthError({});
    setAuthBusy(true);
    emit restoringChanged();
    if (m_socket->state() == QAbstractSocket::ConnectedState && !m_connectAfterAbort)
        doSignIn();
    else
        connectNow(false);
}

void RockClient::signUp(const QString &username, const QString &password, const QString &displayName)
{
    const QString nameError = validateUsername(username.trimmed());
    if (!nameError.isEmpty()) {
        setAuthError(nameError);
        return;
    }
    const QString passError = validatePassword(password);
    if (!passError.isEmpty()) {
        setAuthError(passError);
        return;
    }
    const QString displayError = validateDisplayName(displayName.trimmed());
    if (!displayError.isEmpty()) {
        setAuthError(displayError);
        return;
    }
    m_qUser = username.trimmed();
    m_qPass = password;
    m_qName = displayName.trimmed();
    m_queued = QueuedAuth::SignUp;
    ++m_authGen;
    m_bindFailed = false;
    m_authDeadline = QDateTime::currentMSecsSinceEpoch() + 12000;
    setAuthError({});
    setAuthBusy(true);
    emit restoringChanged();
    if (m_socket->state() == QAbstractSocket::ConnectedState && !m_connectAfterAbort)
        doSignUp();
    else
        connectNow(false);
}

void RockClient::doSignIn()
{
    m_queued = QueuedAuth::None;
    const int generation = m_authGen;
    const QString password = m_qPass;
    m_qPass.clear();
    QJsonObject params;
    params.insert(QStringLiteral("username"), m_qUser);
    params.insert(QStringLiteral("password"), password);
    params.insert(QStringLiteral("device"), deviceName());
    params.insert(QStringLiteral("device_id"), deviceId());
    params.insert(QStringLiteral("platform"), QStringLiteral("windows"));
    call(QStringLiteral("auth.signIn"), params, [this, generation](const QJsonObject &response) {
        if (generation != m_authGen)
            return;
        setAuthBusy(false);
        if (!response.value(QStringLiteral("ok")).toBool()) {
            setAuthError(humanize(errorCodeOf(response)));
            return;
        }
        const QJsonObject result = response.value(QStringLiteral("result")).toObject();
        if (result.value(QStringLiteral("need_cloud_password")).toBool()) {
            m_cloudChallenge = result.value(QStringLiteral("challenge")).toString();
            emit cloudChallengeChanged();
            setAuthError(QStringLiteral("Нужен облачный пароль"));
            return;
        }
        applySession(result.value(QStringLiteral("user")).toObject(),
                     result.value(QStringLiteral("token")).toString());
    });
}

void RockClient::doSignUp()
{
    m_queued = QueuedAuth::None;
    const int generation = m_authGen;
    const QString password = m_qPass;
    m_qPass.clear();
    QJsonObject params;
    params.insert(QStringLiteral("username"), m_qUser);
    params.insert(QStringLiteral("password"), password);
    params.insert(QStringLiteral("display_name"), m_qName);
    params.insert(QStringLiteral("device"), deviceName());
    params.insert(QStringLiteral("device_id"), deviceId());
    params.insert(QStringLiteral("platform"), QStringLiteral("windows"));
    call(QStringLiteral("auth.signUp"), params, [this, generation](const QJsonObject &response) {
        if (generation != m_authGen)
            return;
        setAuthBusy(false);
        if (!response.value(QStringLiteral("ok")).toBool()) {
            setAuthError(humanize(errorCodeOf(response)));
            return;
        }
        const QJsonObject result = response.value(QStringLiteral("result")).toObject();
        applySession(result.value(QStringLiteral("user")).toObject(),
                     result.value(QStringLiteral("token")).toString());
    });
}

void RockClient::doBind()
{
    if (m_token.isEmpty())
        return;
    const int generation = m_authGen;
    QJsonObject params;
    params.insert(QStringLiteral("token"), m_token);
    call(QStringLiteral("auth.bind"), params, [this, generation](const QJsonObject &response) {
        if (generation != m_authGen)
            return;
        if (!response.value(QStringLiteral("ok")).toBool()) {
            const QString code = errorCodeOf(response);
            if (code == QStringLiteral("INVALID_TOKEN") || code == QStringLiteral("UNAUTHORIZED")) {
                m_token.clear();
                saveSettings();
                m_bindFailed = true;
                if (m_authenticated) {
                    clearChatState();
                    m_authenticated = false;
                    m_me.clear();
                    m_meId = 0;
                    emit meChanged();
                    emit authenticatedChanged();
                }
                emit restoringChanged();
                setAuthError(QStringLiteral("Сессия истекла, войдите снова"));
                return;
            }
            if (code == QStringLiteral("NOT_CONNECTED") || code == QStringLiteral("TIMEOUT"))
                return;
            m_bindFailed = !m_authenticated;
            emit restoringChanged();
            if (!m_authenticated)
                setAuthError(humanize(code));
            return;
        }
        const QJsonObject user = response.value(QStringLiteral("result")).toObject().value(QStringLiteral("user")).toObject();
        if (m_authenticated) {
            refreshDialogs();
            refreshContacts();
            if (m_activeDialogId > 0)
                requestHistory(m_activeDialogId, false);
            return;
        }
        applySession(user, QString());
    });
}

void RockClient::applySession(const QJsonObject &user, const QString &newToken)
{
    clearChatState();
    if (!newToken.isEmpty()) {
        m_token = newToken;
        saveSettings();
    }
    m_me = userToMap(user);
    m_meId = m_me.value(QStringLiteral("id")).toLongLong();
    cacheUser(m_me);
    m_bindFailed = false;
    m_authenticated = true;
    emit meChanged();
    emit authenticatedChanged();
    emit restoringChanged();
    refreshDialogs();
    refreshContacts();
    refreshProfile();
    loadFolders();
    loadStories();
    armSession();
}

void RockClient::signOut()
{
    ++m_authGen;
    m_queued = QueuedAuth::None;
    m_qPass.clear();
    setAuthBusy(false);
    if (m_connected && m_authenticated)
        call(QStringLiteral("auth.signOut"), QJsonObject(), [](const QJsonObject &) {});
    m_token.clear();
    saveSettings();
    clearChatState();
    m_me.clear();
    m_meId = 0;
    m_authenticated = false;
    m_bindFailed = true;
    emit meChanged();
    emit authenticatedChanged();
    emit restoringChanged();
    setAuthError({});
}

void RockClient::forgetSession()
{
    ++m_authGen;
    m_queued = QueuedAuth::None;
    m_qPass.clear();
    setAuthBusy(false);
    m_token.clear();
    saveSettings();
    clearChatState();
    m_me.clear();
    m_meId = 0;
    m_authenticated = false;
    m_bindFailed = true;
    emit meChanged();
    emit authenticatedChanged();
    emit restoringChanged();
    setAuthError({});
}

void RockClient::retrySession()
{
    if (m_token.isEmpty())
        return;
    m_bindFailed = false;
    ++m_authGen;
    setAuthError({});
    emit restoringChanged();
    if (m_socket->state() == QAbstractSocket::ConnectedState)
        doBind();
    else
        connectNow(false);
}

void RockClient::handleAuthLoss()
{
    if (!m_authenticated && m_token.isEmpty())
        return;
    ++m_authGen;
    m_token.clear();
    saveSettings();
    clearChatState();
    m_me.clear();
    m_meId = 0;
    m_authenticated = false;
    m_bindFailed = true;
    emit meChanged();
    emit authenticatedChanged();
    emit restoringChanged();
    setAuthError(QStringLiteral("Нужно войти снова"));
}

void RockClient::clearChatState()
{
    m_dialogs.clear();
    m_store.clear();
    m_contacts.clear();
    m_members.clear();
    m_users.clear();
    m_hasMore.clear();
    m_readCursor.clear();
    m_historyGen.clear();
    m_pendingUsers.clear();
    m_searchResults.clear();
    m_myRole.clear();
    m_membersDialogId = 0;
    m_dialogFilter.clear();
    m_messages.clear();
    if (m_activeDialogId != 0) {
        m_activeDialogId = 0;
        emit activeDialogIdChanged();
    }
    emit dialogFilterChanged();
    emit dialogsChanged();
    emit messagesChanged();
    emit contactsChanged();
    emit membersChanged();
    emit searchResultsChanged();
    emit activeDialogChanged();
    emit profileChanged();
    emit unreadTotalChanged();
    emit canLoadOlderChanged();
}

void RockClient::refresh()
{
    if (!m_authenticated)
        return;
    refreshDialogs();
    refreshContacts();
    refreshProfile();
    loadFolders();
    loadStories();
    if (m_activeDialogId > 0)
        requestHistory(m_activeDialogId, false);
}

void RockClient::refreshDialogs()
{
    call(QStringLiteral("dialogs.get"), QJsonObject(), [this](const QJsonObject &response) {
        if (!response.value(QStringLiteral("ok")).toBool()) {
            if (errorCodeOf(response) == QStringLiteral("UNAUTHORIZED"))
                handleAuthLoss();
            return;
        }
        const QJsonArray rows = response.value(QStringLiteral("result")).toObject().value(QStringLiteral("dialogs")).toArray();
        QVariantList next;
        next.reserve(rows.size());
        for (const QJsonValue &value : rows) {
            if (!value.isObject())
                continue;
            const QVariantMap dialog = dialogToMap(value.toObject());
            cacheUser(dialog.value(QStringLiteral("peer")).toMap());
            const QVariantMap last = dialog.value(QStringLiteral("last_message")).toMap();
            if (!last.isEmpty())
                ensureUser(last.value(QStringLiteral("sender_id")).toLongLong());
            next.append(dialog);
        }
        m_dialogs = next;
        if (m_activeDialogId > 0 && dialogIndex(m_activeDialogId) < 0) {
            m_activeDialogId = 0;
            m_messages.clear();
            emit activeDialogIdChanged();
            emit messagesChanged();
        }
        notifyDialogs();
    });
}

void RockClient::refreshContacts()
{
    call(QStringLiteral("contacts.get"), QJsonObject(), [this](const QJsonObject &response) {
        if (!response.value(QStringLiteral("ok")).toBool()) {
            if (errorCodeOf(response) == QStringLiteral("UNAUTHORIZED"))
                handleAuthLoss();
            return;
        }
        const QJsonArray rows = response.value(QStringLiteral("result")).toObject().value(QStringLiteral("contacts")).toArray();
        QVariantList next;
        for (const QJsonValue &value : rows) {
            if (!value.isObject())
                continue;
            const QVariantMap contact = userToMap(value.toObject());
            cacheUser(contact);
            next.append(contact);
        }
        m_contacts = next;
        emit contactsChanged();
        emit profileChanged();
    });
}

void RockClient::openDialog(qint64 dialogId)
{
    if (dialogId <= 0)
        return;
    if (m_activeDialogId != dialogId) {
        m_activeDialogId = dialogId;
        m_members.clear();
        m_myRole.clear();
        m_membersDialogId = 0;
        emit activeDialogIdChanged();
        emit membersChanged();
        emit activeDialogChanged();
        emit profileChanged();
        emit canLoadOlderChanged();
    }
    if (m_store.contains(dialogId)) {
        publishActive();
        emit historyReset();
        markRead(dialogId);
    }
    requestHistory(dialogId, false);
    if (activeKind() == QStringLiteral("secret")) {
        ensureSecret(dialogId);
        setCaptureBlocked(true);
    } else {
        setCaptureBlocked(false);
    }
    if (activeKind() == QStringLiteral("group") || activeKind() == QStringLiteral("channel"))
        loadTopics();
}

void RockClient::loadOlder()
{
    if (m_activeDialogId <= 0 || m_historyLoading || !m_hasMore.value(m_activeDialogId, false))
        return;
    requestHistory(m_activeDialogId, true);
}

void RockClient::requestHistory(qint64 dialogId, bool older)
{
    if (dialogId <= 0)
        return;
    qint64 before = 0;
    if (older) {
        const QVariantList existing = m_store.value(dialogId);
        if (existing.isEmpty())
            return;
        before = existing.first().toMap().value(QStringLiteral("id")).toLongLong();
        if (before <= 0)
            return;
    }
    const int generation = ++m_historySerial;
    m_historyGen.insert(dialogId, generation);
    if (!m_historyLoading) {
        m_historyLoading = true;
        emit historyLoadingChanged();
    }
    QJsonObject params;
    params.insert(QStringLiteral("dialog_id"), static_cast<double>(dialogId));
    params.insert(QStringLiteral("limit"), kHistoryPage);
    if (older)
        params.insert(QStringLiteral("before_id"), static_cast<double>(before));
    call(QStringLiteral("messages.getHistory"), params,
         [this, dialogId, generation, older](const QJsonObject &response) {
             if (m_historyGen.value(dialogId) != generation)
                 return;
             m_historyLoading = false;
             emit historyLoadingChanged();
             if (!response.value(QStringLiteral("ok")).toBool()) {
                 const QString code = errorCodeOf(response);
                 if (code == QStringLiteral("UNAUTHORIZED"))
                     handleAuthLoss();
                 else if (code != QStringLiteral("NOT_CONNECTED") && code != QStringLiteral("TIMEOUT"))
                     emit userNotice(humanize(code));
                 return;
             }
             const QJsonArray rows = response.value(QStringLiteral("result")).toObject().value(QStringLiteral("messages")).toArray();
             QVariantList incoming;
             incoming.reserve(rows.size());
             for (const QJsonValue &value : rows) {
                 if (value.isObject())
                     incoming.append(messageToMap(value.toObject()));
             }
             if (older) {
                 auto merged = incoming;
                 merged.append(m_store.value(dialogId));
                 m_store.insert(dialogId, dedupeSort(merged));
             } else if (incoming.isEmpty()) {
                 m_store.insert(dialogId, {});
             } else {
                 qint64 maxIncoming = 0;
                 for (const QVariant &item : incoming)
                     maxIncoming = qMax(maxIncoming, item.toMap().value(QStringLiteral("id")).toLongLong());
                 QVariantList merged = incoming;
                 for (const QVariant &item : m_store.value(dialogId)) {
                     if (item.toMap().value(QStringLiteral("id")).toLongLong() > maxIncoming)
                         merged.append(item);
                 }
                 m_store.insert(dialogId, dedupeSort(merged));
             }
             m_hasMore.insert(dialogId, rows.size() >= kHistoryPage);
             if (dialogId == m_activeDialogId) {
                 publishActive();
                 if (older)
                     emit historyPrepended();
                 else
                     emit historyReset();
                 markRead(dialogId);
                 noteViews(dialogId);
             }
         });
}

void RockClient::publishActive()
{
    m_messages.clear();
    const QVariantList list = m_store.value(m_activeDialogId);
    const bool group = activeKind() == QStringLiteral("group");
    QDate previousDay;
    qint64 previousSender = -1;
    bool haveDay = false;
    for (const QVariant &item : list) {
        const QVariantMap source = item.toMap();
        const qint64 date = source.value(QStringLiteral("date")).toLongLong();
        const QDate day = QDateTime::fromSecsSinceEpoch(date).date();
        if (!haveDay || day != previousDay) {
            QVariantMap separator;
            separator.insert(QStringLiteral("kind"), QStringLiteral("date"));
            separator.insert(QStringLiteral("key"), QStringLiteral("day-%1").arg(day.toJulianDay()));
            separator.insert(QStringLiteral("label"), dayLabel(date));
            m_messages.append(separator);
            previousDay = day;
            previousSender = -1;
            haveDay = true;
        }
        const qint64 expires = source.value(QStringLiteral("expires_at")).toLongLong();
        if (expires > 0 && QDateTime::currentSecsSinceEpoch() >= expires)
            continue;
        const qint64 sender = source.value(QStringLiteral("sender_id")).toLongLong();
        const bool mine = sender == m_meId;
        QVariantMap message = source;
        message.insert(QStringLiteral("raw"), source.value(QStringLiteral("text")).toString());
        message.insert(QStringLiteral("text"), presentMessage(source));
        message.insert(QStringLiteral("kind"), QStringLiteral("message"));
        message.insert(QStringLiteral("key"), QStringLiteral("m-%1").arg(source.value(QStringLiteral("id")).toLongLong()));
        message.insert(QStringLiteral("time"), QDateTime::fromSecsSinceEpoch(date).toString(QStringLiteral("HH:mm")));
        message.insert(QStringLiteral("mine"), mine);
        message.insert(QStringLiteral("sender_name"), userName(sender));
        message.insert(QStringLiteral("show_name"), group && !mine && sender != previousSender);
        message.insert(QStringLiteral("seed"), sender);
        m_messages.append(message);
        previousSender = sender;
        if (!mine)
            ensureUser(sender);
    }
    emit messagesChanged();
    emit canLoadOlderChanged();
}

void RockClient::markRead(qint64 dialogId)
{
    if (dialogId <= 0 || dialogId != m_activeDialogId)
        return;
    const QVariantList list = m_store.value(dialogId);
    if (list.isEmpty())
        return;
    const qint64 maxId = list.last().toMap().value(QStringLiteral("id")).toLongLong();
    if (maxId <= 0 || maxId <= m_readCursor.value(dialogId))
        return;
    m_readCursor.insert(dialogId, maxId);
    const int index = dialogIndex(dialogId);
    if (index >= 0) {
        QVariantMap dialog = m_dialogs.at(index).toMap();
        if (dialog.value(QStringLiteral("unread")).toInt() != 0) {
            dialog.insert(QStringLiteral("unread"), 0);
            m_dialogs[index] = dialog;
            notifyDialogs();
        }
    }
    QJsonObject params;
    params.insert(QStringLiteral("dialog_id"), static_cast<double>(dialogId));
    params.insert(QStringLiteral("max_id"), static_cast<double>(maxId));
    call(QStringLiteral("messages.read"), params, [](const QJsonObject &) {});
}

bool RockClient::sendMessage(const QString &text)
{
    return sendRich(text, 0, {}, false, 0, 0, {});
}

void RockClient::searchUsers(const QString &query)
{
    const QString needle = query.trimmed();
    const int generation = ++m_searchGen;
    if (needle.isEmpty()) {
        m_searchResults.clear();
        emit searchResultsChanged();
        return;
    }
    if (needle.toUtf8().size() > 32) {
        emit userNotice(QStringLiteral("Запрос длиннее 32 байт"));
        return;
    }
    QJsonObject params;
    params.insert(QStringLiteral("query"), needle);
    params.insert(QStringLiteral("limit"), 20);
    call(QStringLiteral("users.search"), params, [this, generation](const QJsonObject &response) {
        if (generation != m_searchGen)
            return;
        if (!response.value(QStringLiteral("ok")).toBool()) {
            const QString code = errorCodeOf(response);
            if (code == QStringLiteral("UNAUTHORIZED"))
                handleAuthLoss();
            else if (code != QStringLiteral("NOT_CONNECTED"))
                emit userNotice(humanize(code));
            return;
        }
        const QJsonArray rows = response.value(QStringLiteral("result")).toObject().value(QStringLiteral("users")).toArray();
        QVariantList next;
        for (const QJsonValue &value : rows) {
            if (!value.isObject())
                continue;
            const QVariantMap user = userToMap(value.toObject());
            cacheUser(user);
            next.append(user);
        }
        m_searchResults = next;
        emit searchResultsChanged();
    });
}

void RockClient::startPrivate(qint64 userId)
{
    if (userId <= 0 || userId == m_meId) {
        emit userNotice(QStringLiteral("Нельзя написать самому себе"));
        return;
    }
    QJsonObject params;
    params.insert(QStringLiteral("user_id"), static_cast<double>(userId));
    call(QStringLiteral("dialogs.createPrivate"), params, [this](const QJsonObject &response) {
        if (!response.value(QStringLiteral("ok")).toBool()) {
            const QString code = errorCodeOf(response);
            if (code == QStringLiteral("UNAUTHORIZED"))
                handleAuthLoss();
            else
                emit userNotice(humanize(code));
            return;
        }
        upsertDialog(response.value(QStringLiteral("result")).toObject());
        openDialog(jsonInt(response.value(QStringLiteral("result")).toObject().value(QStringLiteral("id"))));
        emit chatOpened();
    });
}

void RockClient::addContactByName(const QString &username)
{
    const QString nameError = validateUsername(username.trimmed());
    if (!nameError.isEmpty()) {
        emit userNotice(nameError);
        return;
    }
    QJsonObject params;
    params.insert(QStringLiteral("username"), username.trimmed());
    call(QStringLiteral("contacts.add"), params, [this](const QJsonObject &response) {
        if (!response.value(QStringLiteral("ok")).toBool()) {
            const QString code = errorCodeOf(response);
            if (code == QStringLiteral("UNAUTHORIZED"))
                handleAuthLoss();
            else
                emit userNotice(humanize(code));
            return;
        }
        cacheUser(userToMap(response.value(QStringLiteral("result")).toObject()));
        refreshContacts();
        emit userNotice(QStringLiteral("Контакт добавлен"));
    });
}

void RockClient::addContactById(qint64 userId)
{
    if (userId <= 0)
        return;
    QJsonObject params;
    params.insert(QStringLiteral("user_id"), static_cast<double>(userId));
    call(QStringLiteral("contacts.add"), params, [this](const QJsonObject &response) {
        if (!response.value(QStringLiteral("ok")).toBool()) {
            const QString code = errorCodeOf(response);
            if (code == QStringLiteral("UNAUTHORIZED"))
                handleAuthLoss();
            else
                emit userNotice(humanize(code));
            return;
        }
        cacheUser(userToMap(response.value(QStringLiteral("result")).toObject()));
        refreshContacts();
        emit userNotice(QStringLiteral("Контакт добавлен"));
    });
}

void RockClient::createGroup(const QString &title, const QVariantList &memberIds, bool forum)
{
    const QString trimmed = title.trimmed();
    if (trimmed.isEmpty() || trimmed.toUtf8().size() > 128 || hasForbiddenControls(trimmed, false)) {
        emit userNotice(QStringLiteral("Название группы: 1–128 символов"));
        return;
    }
    QJsonArray ids;
    QSet<qint64> seen;
    seen.insert(m_meId);
    for (const QVariant &item : memberIds) {
        const qint64 id = item.toLongLong();
        if (id <= 0 || seen.contains(id))
            continue;
        seen.insert(id);
        ids.append(static_cast<double>(id));
    }
    if (ids.size() + 1 > 200000) {
        emit userNotice(QStringLiteral("В группе больше 200000 участников"));
        return;
    }
    QJsonObject params;
    params.insert(QStringLiteral("title"), trimmed);
    params.insert(QStringLiteral("member_ids"), ids);
    if (forum)
        params.insert(QStringLiteral("forum"), true);
    call(QStringLiteral("dialogs.createGroup"), params, [this](const QJsonObject &response) {
        if (!response.value(QStringLiteral("ok")).toBool()) {
            const QString code = errorCodeOf(response);
            if (code == QStringLiteral("UNAUTHORIZED"))
                handleAuthLoss();
            else
                emit userNotice(humanize(code));
            return;
        }
        upsertDialog(response.value(QStringLiteral("result")).toObject());
        openDialog(jsonInt(response.value(QStringLiteral("result")).toObject().value(QStringLiteral("id"))));
        emit chatOpened();
    });
}

void RockClient::addMember(qint64 userId)
{
    if (m_activeDialogId <= 0 || userId <= 0)
        return;
    QJsonObject params;
    params.insert(QStringLiteral("dialog_id"), static_cast<double>(m_activeDialogId));
    params.insert(QStringLiteral("user_id"), static_cast<double>(userId));
    call(QStringLiteral("dialogs.addMember"), params, [this](const QJsonObject &response) {
        if (!response.value(QStringLiteral("ok")).toBool()) {
            const QString code = errorCodeOf(response);
            if (code == QStringLiteral("UNAUTHORIZED"))
                handleAuthLoss();
            else
                emit userNotice(humanize(code));
            return;
        }
        const QJsonObject result = response.value(QStringLiteral("result")).toObject();
        if (result.value(QStringLiteral("dialog")).isObject())
            upsertDialog(result.value(QStringLiteral("dialog")).toObject());
        loadMembers();
        if (result.value(QStringLiteral("added")).toBool())
            emit userNotice(QStringLiteral("Участник добавлен"));
        else
            emit userNotice(QStringLiteral("Уже в группе"));
    });
}

void RockClient::loadMembers()
{
    if (m_activeDialogId <= 0)
        return;
    const qint64 dialogId = m_activeDialogId;
    QJsonObject params;
    params.insert(QStringLiteral("dialog_id"), static_cast<double>(dialogId));
    call(QStringLiteral("dialogs.members"), params, [this, dialogId](const QJsonObject &response) {
        if (dialogId != m_activeDialogId)
            return;
        if (!response.value(QStringLiteral("ok")).toBool()) {
            const QString code = errorCodeOf(response);
            if (code == QStringLiteral("UNAUTHORIZED"))
                handleAuthLoss();
            else if (code != QStringLiteral("NOT_CONNECTED"))
                emit userNotice(humanize(code));
            return;
        }
        const QJsonArray rows = response.value(QStringLiteral("result")).toObject().value(QStringLiteral("members")).toArray();
        QVariantList next;
        QString role = QStringLiteral("member");
        for (const QJsonValue &value : rows) {
            if (!value.isObject())
                continue;
            const QVariantMap member = userToMap(value.toObject());
            cacheUser(member);
            next.append(member);
            if (member.value(QStringLiteral("id")).toLongLong() == m_meId)
                role = member.value(QStringLiteral("role")).toString();
        }
        m_members = next;
        m_myRole = role;
        m_membersDialogId = dialogId;
        emit membersChanged();
    });
}

void RockClient::upsertDialog(const QJsonObject &dialog)
{
    const QVariantMap map = dialogToMap(dialog);
    cacheUser(map.value(QStringLiteral("peer")).toMap());
    const qint64 id = map.value(QStringLiteral("id")).toLongLong();
    const int index = dialogIndex(id);
    if (index >= 0)
        m_dialogs[index] = map;
    else
        m_dialogs.append(map);
    std::sort(m_dialogs.begin(), m_dialogs.end(), [](const QVariant &a, const QVariant &b) {
        const qint64 left = a.toMap().value(QStringLiteral("last_message")).toMap().value(QStringLiteral("date")).toLongLong();
        const qint64 right = b.toMap().value(QStringLiteral("last_message")).toMap().value(QStringLiteral("date")).toLongLong();
        if (left != right)
            return left > right;
        return a.toMap().value(QStringLiteral("id")).toLongLong() > b.toMap().value(QStringLiteral("id")).toLongLong();
    });
    notifyDialogs();
}

void RockClient::touchLastMessage(qint64 dialogId, const QVariantMap &message, int unreadDelta)
{
    const int index = dialogIndex(dialogId);
    if (index < 0)
        return;
    QVariantMap dialog = m_dialogs.at(index).toMap();
    dialog.insert(QStringLiteral("last_message"), message);
    if (unreadDelta > 0)
        dialog.insert(QStringLiteral("unread"), dialog.value(QStringLiteral("unread")).toInt() + unreadDelta);
    m_dialogs[index] = dialog;
    std::sort(m_dialogs.begin(), m_dialogs.end(), [](const QVariant &a, const QVariant &b) {
        const qint64 left = a.toMap().value(QStringLiteral("last_message")).toMap().value(QStringLiteral("date")).toLongLong();
        const qint64 right = b.toMap().value(QStringLiteral("last_message")).toMap().value(QStringLiteral("date")).toLongLong();
        if (left != right)
            return left > right;
        return a.toMap().value(QStringLiteral("id")).toLongLong() > b.toMap().value(QStringLiteral("id")).toLongLong();
    });
    notifyDialogs();
}

void RockClient::cacheUser(const QVariantMap &user)
{
    const qint64 id = user.value(QStringLiteral("id")).toLongLong();
    if (id <= 0)
        return;
    QVariantMap merged = m_users.value(id);
    for (auto it = user.cbegin(); it != user.cend(); ++it)
        merged.insert(it.key(), it.value());
    merged.insert(QStringLiteral("id"), id);
    m_users.insert(id, merged);
}

void RockClient::ensureUser(qint64 userId)
{
    if (userId <= 0 || userId == m_meId || m_users.contains(userId) || m_pendingUsers.contains(userId))
        return;
    m_pendingUsers.insert(userId);
    QJsonObject params;
    params.insert(QStringLiteral("user_id"), static_cast<double>(userId));
    call(QStringLiteral("users.get"), params, [this, userId](const QJsonObject &response) {
        m_pendingUsers.remove(userId);
        if (!response.value(QStringLiteral("ok")).toBool())
            return;
        cacheUser(userToMap(response.value(QStringLiteral("result")).toObject()));
        if (m_activeDialogId > 0)
            publishActive();
        notifyDialogs();
    });
}

void RockClient::notifyDialogs()
{
    emit dialogsChanged();
    emit activeDialogChanged();
    emit profileChanged();
    emit unreadTotalChanged();
}

int RockClient::dialogIndex(qint64 dialogId) const
{
    for (int i = 0; i < m_dialogs.size(); ++i) {
        if (m_dialogs.at(i).toMap().value(QStringLiteral("id")).toLongLong() == dialogId)
            return i;
    }
    return -1;
}

QVariantMap RockClient::dialogToMap(const QJsonObject &dialog) const
{
    QVariantMap map;
    map.insert(QStringLiteral("id"), jsonInt(dialog.value(QStringLiteral("id"))));
    map.insert(QStringLiteral("type"), dialog.value(QStringLiteral("type")).toString());
    map.insert(QStringLiteral("title"), dialog.value(QStringLiteral("title")).toString());
    map.insert(QStringLiteral("member_count"), jsonInt(dialog.value(QStringLiteral("member_count"))));
    map.insert(QStringLiteral("unread"), jsonInt(dialog.value(QStringLiteral("unread"))));
    map.insert(QStringLiteral("archived"), dialog.value(QStringLiteral("archived")).toBool());
    map.insert(QStringLiteral("pinned"), dialog.value(QStringLiteral("pinned")).toBool());
    map.insert(QStringLiteral("muted_until"), jsonInt(dialog.value(QStringLiteral("muted_until"))));
    map.insert(QStringLiteral("no_forwards"), dialog.value(QStringLiteral("no_forwards")).toBool());
    map.insert(QStringLiteral("forum"), dialog.value(QStringLiteral("forum")).toBool());
    map.insert(QStringLiteral("ttl_seconds"), jsonInt(dialog.value(QStringLiteral("ttl_seconds"))));
    if (!dialog.value(QStringLiteral("linked_id")).isNull())
        map.insert(QStringLiteral("linked_id"), jsonInt(dialog.value(QStringLiteral("linked_id"))));
    if (dialog.value(QStringLiteral("peer")).isObject())
        map.insert(QStringLiteral("peer"), userToMap(dialog.value(QStringLiteral("peer")).toObject()));
    else
        map.insert(QStringLiteral("peer"), QVariant());
    if (dialog.value(QStringLiteral("last_message")).isObject())
        map.insert(QStringLiteral("last_message"), messageToMap(dialog.value(QStringLiteral("last_message")).toObject()));
    else
        map.insert(QStringLiteral("last_message"), QVariant());
    return map;
}

QVariantMap RockClient::userToMap(const QJsonObject &user) const
{
    QVariantMap map;
    map.insert(QStringLiteral("id"), jsonInt(user.value(QStringLiteral("id"))));
    map.insert(QStringLiteral("username"), user.value(QStringLiteral("username")).toString());
    map.insert(QStringLiteral("display_name"), user.value(QStringLiteral("display_name")).toString());
    if (user.contains(QStringLiteral("online")))
        map.insert(QStringLiteral("online"), user.value(QStringLiteral("online")).toBool());
    if (user.contains(QStringLiteral("role")))
        map.insert(QStringLiteral("role"), user.value(QStringLiteral("role")).toString());
    return map;
}

QVariantMap RockClient::messageToMap(const QJsonObject &message) const
{
    QVariantMap map;
    map.insert(QStringLiteral("id"), jsonInt(message.value(QStringLiteral("id"))));
    map.insert(QStringLiteral("dialog_id"), jsonInt(message.value(QStringLiteral("dialog_id"))));
    map.insert(QStringLiteral("sender_id"), jsonInt(message.value(QStringLiteral("sender_id"))));
    map.insert(QStringLiteral("text"), message.value(QStringLiteral("text")).toString());
    map.insert(QStringLiteral("date"), jsonInt(message.value(QStringLiteral("date"))));
    map.insert(QStringLiteral("kind"), message.value(QStringLiteral("kind")).toString(QStringLiteral("text")));
    if (!message.value(QStringLiteral("reply_to")).isNull())
        map.insert(QStringLiteral("reply_to"), jsonInt(message.value(QStringLiteral("reply_to"))));
    map.insert(QStringLiteral("quote"), message.value(QStringLiteral("quote")).toString());
    if (!message.value(QStringLiteral("file_id")).isNull())
        map.insert(QStringLiteral("file_id"), jsonInt(message.value(QStringLiteral("file_id"))));
    map.insert(QStringLiteral("silent"), message.value(QStringLiteral("silent")).toBool());
    map.insert(QStringLiteral("edited_at"), jsonInt(message.value(QStringLiteral("edited_at"))));
    map.insert(QStringLiteral("entities"), message.value(QStringLiteral("entities")).toString());
    map.insert(QStringLiteral("views"), jsonInt(message.value(QStringLiteral("views"))));
    map.insert(QStringLiteral("expires_at"), jsonInt(message.value(QStringLiteral("expires_at"))));
    map.insert(QStringLiteral("topic_id"), jsonInt(message.value(QStringLiteral("topic_id"))));
    map.insert(QStringLiteral("no_forward"), message.value(QStringLiteral("no_forward")).toBool());
    map.insert(QStringLiteral("transcript"), message.value(QStringLiteral("transcript")).toString());
    map.insert(QStringLiteral("translation"), message.value(QStringLiteral("translation")).toString());
    map.insert(QStringLiteral("reactions"), message.value(QStringLiteral("reactions")).toArray().toVariantList());
    return map;
}

QString RockClient::validateUsername(const QString &username) const
{
    static const QRegularExpression pattern(QStringLiteral("^[A-Za-z0-9_]{3,32}$"));
    if (!pattern.match(username).hasMatch())
        return QStringLiteral("Имя: 3–32 символа, латиница, цифры и _");
    return {};
}

QString RockClient::validatePassword(const QString &password) const
{
    const int size = password.toUtf8().size();
    if (size < 8 || size > 128)
        return QStringLiteral("Пароль: от 8 до 128 символов");
    return {};
}

QString RockClient::validateDisplayName(const QString &name) const
{
    const int size = name.toUtf8().size();
    if (name.isEmpty() || size > 64 || hasForbiddenControls(name, false))
        return QStringLiteral("Имя на экране: 1–64 символа без управляющих");
    return {};
}

bool RockClient::hasForbiddenControls(const QString &text, bool allowBreaks) const
{
    for (const QChar ch : text) {
        if (ch.unicode() >= 0x20)
            continue;
        if (allowBreaks && (ch == QLatin1Char('\n') || ch == QLatin1Char('\t')))
            continue;
        return true;
    }
    return false;
}

QString RockClient::presentMessage(const QVariantMap &source) const
{
    const qint64 id = source.value(QStringLiteral("id")).toLongLong();
    const qint64 dialogId = source.value(QStringLiteral("dialog_id")).toLongLong();
    QString body = secretPlain(dialogId, source.value(QStringLiteral("text")).toString());
    const QJsonArray marks = QJsonDocument::fromJson(source.value(QStringLiteral("entities")).toString().toUtf8()).array();
    if (!m_revealed.contains(id)) {
        for (const QJsonValue &value : marks) {
            const QJsonObject row = value.toObject();
            if (row.value(QStringLiteral("t")).toString() != QStringLiteral("spoiler"))
                continue;
            const int offset = row.value(QStringLiteral("o")).toInt();
            const int length = row.value(QStringLiteral("l")).toInt();
            if (offset < 0 || length <= 0 || offset >= body.size())
                continue;
            body.replace(offset, qMin(length, body.size() - offset), QString(qMin(length, body.size() - offset), QChar(0x2022)));
        }
    }
    const QString kind = source.value(QStringLiteral("kind")).toString();
    QString prefix;
    if (kind == QStringLiteral("voice"))
        prefix = QStringLiteral("Голосовое. ");
    else if (kind == QStringLiteral("video_note"))
        prefix = QStringLiteral("Видеосообщение. ");
    else if (kind == QStringLiteral("file"))
        prefix = QStringLiteral("Файл. ");
    else if (kind == QStringLiteral("photo"))
        prefix = QStringLiteral("Фото. ");
    else if (kind == QStringLiteral("sticker"))
        prefix = QStringLiteral("Стикер ");
    else if (kind == QStringLiteral("sponsor"))
        prefix = QStringLiteral("Реклама. ");
    QStringList lines;
    const QString quote = source.value(QStringLiteral("quote")).toString();
    if (!quote.isEmpty())
        lines << QStringLiteral("↩ ") + quote;
    lines << prefix + body;
    if (source.value(QStringLiteral("edited_at")).toLongLong() > 0)
        lines << QStringLiteral("изменено");
    if (!source.value(QStringLiteral("translation")).toString().isEmpty())
        lines << QStringLiteral("перевод: ") + source.value(QStringLiteral("translation")).toString();
    if (!source.value(QStringLiteral("transcript")).toString().isEmpty())
        lines << source.value(QStringLiteral("transcript")).toString();
    QStringList reactions;
    for (const QVariant &item : source.value(QStringLiteral("reactions")).toList()) {
        const QVariantMap row = item.toMap();
        if (row.value(QStringLiteral("count")).toInt() > 0)
            reactions << row.value(QStringLiteral("emoji")).toString() + QString::number(row.value(QStringLiteral("count")).toInt());
    }
    if (!reactions.isEmpty())
        lines << reactions.join(QStringLiteral(" "));
    if (source.value(QStringLiteral("views")).toInt() > 0)
        lines << QStringLiteral("просмотры: ") + QString::number(source.value(QStringLiteral("views")).toInt());
    if (m_delivered.contains(id))
        lines << QStringLiteral("доставлено");
    const qint64 topic = source.value(QStringLiteral("topic_id")).toLongLong();
    if (topic > 0)
        lines << QStringLiteral("тема ") + QString::number(topic);
    return lines.join(QLatin1Char('\n'));
}

qint64 rockJsonInt(const QJsonValue &value)
{
    return jsonInt(value);
}

QString rockErrorCode(const QJsonObject &response)
{
    return errorCodeOf(response);
}

QString rockHumanize(const QString &code)
{
    return humanize(code);
}

QVariantList rockDedupeSort(QVariantList list)
{
    return dedupeSort(std::move(list));
}
