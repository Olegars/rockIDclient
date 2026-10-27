#pragma once

#include <QAbstractSocket>
#include <QByteArray>
#include <QHash>
#include <QJsonObject>
#include <QSet>
#include <QObject>
#include <QString>
#include <QTcpSocket>
#include <QVariantList>
#include <QVariantMap>

#include <functional>

class QAudioOutput;
class QAudioSink;
class QAudioSource;
class QBuffer;
class QIODevice;
class QMediaPlayer;
class QNetworkAccessManager;
class QTimer;

qint64 rockJsonInt(const QJsonValue &value);
QString rockErrorCode(const QJsonObject &response);
QString rockHumanize(const QString &code);
QVariantList rockDedupeSort(QVariantList list);

// Desktop client for rockIDserver: one TCP socket, uint32be length + JSON.
class RockClient : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString host READ host NOTIFY endpointChanged)
    Q_PROPERTY(int port READ port NOTIFY endpointChanged)
    Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged)
    Q_PROPERTY(QString linkState READ linkState NOTIFY linkStateChanged)
    Q_PROPERTY(QString statusLine READ statusLine NOTIFY statusLineChanged)
    Q_PROPERTY(bool authenticated READ authenticated NOTIFY authenticatedChanged)
    Q_PROPERTY(bool restoring READ restoring NOTIFY restoringChanged)
    Q_PROPERTY(bool authBusy READ authBusy NOTIFY authBusyChanged)
    Q_PROPERTY(QString authError READ authError WRITE setAuthError NOTIFY authErrorChanged)
    Q_PROPERTY(QVariantMap me READ me NOTIFY meChanged)
    Q_PROPERTY(QVariantList dialogs READ dialogs NOTIFY dialogsChanged)
    Q_PROPERTY(QString dialogFilter READ dialogFilter WRITE setDialogFilter NOTIFY dialogFilterChanged)
    Q_PROPERTY(QVariantList contacts READ contacts NOTIFY contactsChanged)
    Q_PROPERTY(QVariantList messages READ messages NOTIFY messagesChanged)
    Q_PROPERTY(QVariantList members READ members NOTIFY membersChanged)
    Q_PROPERTY(QString myRole READ myRole NOTIFY membersChanged)
    Q_PROPERTY(QVariantList searchResults READ searchResults NOTIFY searchResultsChanged)
    Q_PROPERTY(qint64 activeDialogId READ activeDialogId NOTIFY activeDialogIdChanged)
    Q_PROPERTY(QString activeTitle READ activeTitle NOTIFY activeDialogChanged)
    Q_PROPERTY(QString activeHandle READ activeHandle NOTIFY activeDialogChanged)
    Q_PROPERTY(QString activeStatus READ activeStatus NOTIFY activeDialogChanged)
    Q_PROPERTY(QString activeKind READ activeKind NOTIFY activeDialogChanged)
    Q_PROPERTY(bool activeOnline READ activeOnline NOTIFY activeDialogChanged)
    Q_PROPERTY(qint64 activePeerId READ activePeerId NOTIFY activeDialogChanged)
    Q_PROPERTY(qint64 activeSeed READ activeSeed NOTIFY activeDialogChanged)
    Q_PROPERTY(bool activeIsContact READ activeIsContact NOTIFY profileChanged)
    Q_PROPERTY(int unreadTotal READ unreadTotal NOTIFY unreadTotalChanged)
    Q_PROPERTY(bool canLoadOlder READ canLoadOlder NOTIFY canLoadOlderChanged)
    Q_PROPERTY(bool historyLoading READ historyLoading NOTIFY historyLoadingChanged)
    Q_PROPERTY(QString cloudChallenge READ cloudChallenge NOTIFY cloudChallengeChanged)
    Q_PROPERTY(QVariantList folders READ folders NOTIFY foldersChanged)
    Q_PROPERTY(QVariantList stories READ stories NOTIFY storiesChanged)
    Q_PROPERTY(QVariantList sessions READ sessions NOTIFY sessionsChanged)
    Q_PROPERTY(QVariantList topics READ topics NOTIFY topicsChanged)
    Q_PROPERTY(QVariantMap callInfo READ callInfo NOTIFY callChanged)
    Q_PROPERTY(bool callLive READ callLive NOTIFY callChanged)
    Q_PROPERTY(bool callIncoming READ callIncoming NOTIFY callChanged)
    Q_PROPERTY(bool callMuted READ callMuted NOTIFY callChanged)
    Q_PROPERTY(QString listMode READ listMode WRITE setListMode NOTIFY listModeChanged)
    Q_PROPERTY(bool voiceRecording READ voiceRecording NOTIFY voiceRecordingChanged)
    Q_PROPERTY(QVariantList accounts READ accounts NOTIFY accountsChanged)
    Q_PROPERTY(bool pinSet READ pinSet NOTIFY pinChanged)
    Q_PROPERTY(QString boardTitle READ boardTitle NOTIFY boardChanged)
    Q_PROPERTY(QVariantList board READ board NOTIFY boardChanged)

public:
    explicit RockClient(QObject *parent = nullptr);
    ~RockClient() override;

    void start();

    QString host() const { return m_host; }
    int port() const { return m_port; }
    bool connected() const { return m_connected; }
    QString linkState() const { return m_linkState; }
    QString statusLine() const { return m_statusLine; }
    bool authenticated() const { return m_authenticated; }
    bool restoring() const;
    bool authBusy() const { return m_authBusy; }
    QString authError() const { return m_authError; }
    QVariantMap me() const { return m_me; }
    QVariantList dialogs() const;
    QString dialogFilter() const { return m_dialogFilter; }
    void setDialogFilter(const QString &text);
    QVariantList contacts() const { return m_contacts; }
    QVariantList messages() const { return m_messages; }
    QVariantList members() const { return m_members; }
    QString myRole() const { return m_myRole; }
    QVariantList searchResults() const { return m_searchResults; }
    qint64 activeDialogId() const { return m_activeDialogId; }
    QString activeTitle() const;
    QString activeHandle() const;
    QString activeStatus() const;
    QString activeKind() const;
    bool activeOnline() const;
    qint64 activePeerId() const;
    qint64 activeSeed() const;
    bool activeIsContact() const;
    int unreadTotal() const;
    bool canLoadOlder() const;
    bool historyLoading() const { return m_historyLoading; }
    QString cloudChallenge() const { return m_cloudChallenge; }
    QVariantList folders() const { return m_folders; }
    QVariantList stories() const { return m_stories; }
    QVariantList sessions() const { return m_sessions; }
    QVariantList topics() const { return m_topics; }
    QVariantMap callInfo() const { return m_call; }
    bool callLive() const { return m_callLive; }
    bool callIncoming() const { return m_callIncoming; }
    bool callMuted() const { return m_callMuted; }
    QString listMode() const { return m_listMode; }
    bool voiceRecording() const { return m_voiceRecording; }
    QVariantList accounts() const { return m_accounts; }
    bool pinSet() const { return m_pinSet; }
    QString boardTitle() const { return m_boardTitle; }
    QVariantList board() const { return m_board; }

    Q_INVOKABLE void setEndpoint(const QString &host, int port);
    Q_INVOKABLE void signIn(const QString &username, const QString &password);
    Q_INVOKABLE void signUp(const QString &username, const QString &password, const QString &displayName);
    Q_INVOKABLE void signOut();
    Q_INVOKABLE void forgetSession();
    Q_INVOKABLE void retrySession();
    Q_INVOKABLE void openDialog(qint64 dialogId);
    Q_INVOKABLE bool sendMessage(const QString &text);
    Q_INVOKABLE void loadOlder();
    Q_INVOKABLE void searchUsers(const QString &query);
    Q_INVOKABLE void startPrivate(qint64 userId);
    Q_INVOKABLE void addContactByName(const QString &username);
    Q_INVOKABLE void addContactById(qint64 userId);
    Q_INVOKABLE void createGroup(const QString &title, const QVariantList &memberIds, bool forum = false);
    Q_INVOKABLE void addMember(qint64 userId);
    Q_INVOKABLE void loadMembers();
    Q_INVOKABLE void refresh();
    Q_INVOKABLE int utf8Size(const QString &text) const;
    Q_INVOKABLE QString formatListTime(qint64 unixSecs) const;
    Q_INVOKABLE QString userName(qint64 userId) const;
    Q_INVOKABLE void setListMode(const QString &mode);
    Q_INVOKABLE void submitCloudPassword(const QString &password);
    Q_INVOKABLE bool sendRich(const QString &text, qint64 replyTo, const QString &quote, bool silent,
                              qint64 scheduleAt, qint64 topicId, const QString &entities);
    Q_INVOKABLE void editMessage(qint64 messageId, const QString &text);
    Q_INVOKABLE void deleteMessage(qint64 messageId);
    Q_INVOKABLE void react(qint64 messageId, const QString &emoji);
    Q_INVOKABLE void forward(qint64 messageId, qint64 dialogId);
    Q_INVOKABLE void forwardToSaved(qint64 messageId);
    Q_INVOKABLE void translateMessage(qint64 messageId);
    Q_INVOKABLE void setTranscript(qint64 messageId, const QString &text);
    Q_INVOKABLE void uploadFile(const QString &path, const QString &kind, const QString &caption, bool spoiler);
    Q_INVOKABLE void uploadEditedPhoto(const QString &path, int brightness, bool square, const QString &overlay, bool spoiler);
    Q_INVOKABLE void startVoice();
    Q_INVOKABLE void stopVoice();
    Q_INVOKABLE void playLocal(const QString &path);
    Q_INVOKABLE void downloadFile(qint64 fileId);
    Q_INVOKABLE void openSaved();
    Q_INVOKABLE void createSecret(qint64 userId, int ttlSeconds);
    Q_INVOKABLE void createChannel(const QString &title);
    Q_INVOKABLE void createForum(const QString &title);
    Q_INVOKABLE void joinChannel(qint64 dialogId);
    Q_INVOKABLE void leaveDialog();
    Q_INVOKABLE void archiveDialog(bool on);
    Q_INVOKABLE void pinDialog(bool on);
    Q_INVOKABLE void muteDialog(qint64 until);
    Q_INVOKABLE void loadTopics();
    Q_INVOKABLE void createTopic(const QString &title);
    Q_INVOKABLE void setDiscussion(qint64 groupId);
    Q_INVOKABLE void kick(qint64 userId);
    Q_INVOKABLE void ban(qint64 userId, bool on);
    Q_INVOKABLE void setAdmin(qint64 userId, bool canDelete, bool canBan, bool canPin, bool canManage, bool canPost);
    Q_INVOKABLE void loadFolders();
    Q_INVOKABLE void createFolder(const QString &title);
    Q_INVOKABLE void addToFolder(qint64 folderId);
    Q_INVOKABLE void removeFromFolder(qint64 folderId);
    Q_INVOKABLE void deleteFolder(qint64 folderId);
    Q_INVOKABLE void loadStories();
    Q_INVOKABLE void postStory(const QString &caption, int hours, const QString &privacy, bool noScreenshots, qint64 fileId);
    Q_INVOKABLE void viewStory(qint64 storyId);
    Q_INVOKABLE void reportStoryScreenshot(qint64 storyId);
    Q_INVOKABLE void reportScreenshot();
    Q_INVOKABLE void loadSessions();
    Q_INVOKABLE void terminateSession(qint64 sessionId);
    Q_INVOKABLE void terminateOthers();
    Q_INVOKABLE void setCloudPassword(const QString &password, const QString &current);
    Q_INVOKABLE void clearCloudPassword(const QString &password);
    Q_INVOKABLE void setPrivacy(const QString &phone, const QString &lastSeen, const QString &photo);
    Q_INVOKABLE void updateProfile(const QString &displayName, const QString &phone, const QString &bio);
    Q_INVOKABLE void setLocalLock(const QString &kind);
    Q_INVOKABLE void setPin(const QString &pin);
    Q_INVOKABLE bool checkPin(const QString &pin);
    Q_INVOKABLE void savePreferences(const QString &json);
    Q_INVOKABLE void setAppIcon(const QString &color);
    Q_INVOKABLE void startCall();
    Q_INVOKABLE void acceptCall();
    Q_INVOKABLE void declineCall();
    Q_INVOKABLE void hangUp();
    Q_INVOKABLE void toggleCallMute();
    Q_INVOKABLE void callAction(const QString &action, qint64 callId, bool muted, bool video, bool screen, bool recording);
    Q_INVOKABLE void createBot(const QString &username, const QString &displayName);
    Q_INVOKABLE void botSignIn(const QString &botToken);
    Q_INVOKABLE void pollBotUpdates();
    Q_INVOKABLE void createInvoice(qint64 userId, const QString &title, qint64 amount, const QString &currency, const QString &purpose, const QString &payUrl = QString());
    Q_INVOKABLE void setInvoice(qint64 invoiceId, bool confirm, const QString &providerRef);
    Q_INVOKABLE void loadInvoices();
    Q_INVOKABLE void createApp(const QString &title, const QString &shortName, const QString &url);
    Q_INVOKABLE void setAppContent(const QString &shortName, const QString &html);
    Q_INVOKABLE void openMiniApp(const QString &shortName);
    Q_INVOKABLE void editServerFile(qint64 fileId, int brightness, int contrast, int rotate);
    Q_INVOKABLE void loadApps();
    Q_INVOKABLE void loadStickers();
    Q_INVOKABLE void createStickerPack(const QString &title, const QString &shortName, bool premium);
    Q_INVOKABLE void sendSticker(qint64 fileId, const QString &emoji);
    Q_INVOKABLE void loadSavedTabs();
    Q_INVOKABLE void createSavedTab(const QString &title);
    Q_INVOKABLE void addSavedToTab(qint64 tabId, qint64 messageId);
    Q_INVOKABLE void tagSaved(qint64 messageId, const QString &tag);
    Q_INVOKABLE void rememberAccount(const QString &label);
    Q_INVOKABLE void switchAccount(int index);
    Q_INVOKABLE QString previewOf(qint64 dialogId, const QString &text) const;
    Q_INVOKABLE void reveal(qint64 messageId);

signals:
    void endpointChanged();
    void connectedChanged();
    void linkStateChanged();
    void statusLineChanged();
    void authenticatedChanged();
    void restoringChanged();
    void authBusyChanged();
    void authErrorChanged();
    void meChanged();
    void dialogsChanged();
    void dialogFilterChanged();
    void contactsChanged();
    void messagesChanged();
    void membersChanged();
    void searchResultsChanged();
    void activeDialogIdChanged();
    void activeDialogChanged();
    void profileChanged();
    void unreadTotalChanged();
    void canLoadOlderChanged();
    void historyLoadingChanged();
    void userNotice(const QString &text);
    void sendFailed(const QString &text);
    void chatOpened();
    void historyReset();
    void historyPrepended();
    void messageArrived(bool own);
    void cloudChallengeChanged();
    void foldersChanged();
    void storiesChanged();
    void sessionsChanged();
    void topicsChanged();
    void callChanged();
    void listModeChanged();
    void voiceRecordingChanged();
    void accountsChanged();
    void pinChanged();
    void boardChanged();
    void fileReady(const QString &path);

private slots:
    void onConnected();
    void onDisconnected();
    void onReadyRead();
    void onSocketError(QAbstractSocket::SocketError error);
    void beginConnect();
    void sendPing();
    void checkTimeouts();

private:
    struct PendingCall {
        QString method;
        qint64 deadline = 0;
        std::function<void(const QJsonObject &)> callback;
    };
    enum class QueuedAuth { None, SignIn, SignUp };

    void connectNow(bool force);
    void loadSettings();
    void saveSettings() const;
    void setLink(const QString &state, const QString &line);
    void setAuthError(const QString &text);
    void setAuthBusy(bool busy);
    void call(const QString &method, const QJsonObject &params,
              const std::function<void(const QJsonObject &)> &callback);
    void sendFrame(const QJsonObject &object);
    void failPending(const QString &code);
    void handleFrame(const QByteArray &payload);
    void handleUpdate(const QString &type, const QJsonObject &data);
    void doSignIn();
    void doSignUp();
    void doBind();
    void applySession(const QJsonObject &user, const QString &newToken);
    void handleAuthLoss();
    void clearChatState();
    void refreshDialogs();
    void refreshContacts();
    void requestHistory(qint64 dialogId, bool older);
    void publishActive();
    void markRead(qint64 dialogId);
    void upsertDialog(const QJsonObject &dialog);
    void touchLastMessage(qint64 dialogId, const QVariantMap &message, int unreadDelta);
    void cacheUser(const QVariantMap &user);
    void ensureUser(qint64 userId);
    void notifyDialogs();
    int dialogIndex(qint64 dialogId) const;
    QVariantMap activeDialog() const;
    QVariantMap dialogToMap(const QJsonObject &dialog) const;
    QVariantMap userToMap(const QJsonObject &user) const;
    QVariantMap messageToMap(const QJsonObject &message) const;
    QString dayLabel(qint64 unixSecs) const;
    QString validateUsername(const QString &username) const;
    QString validatePassword(const QString &password) const;
    QString validateDisplayName(const QString &name) const;
    bool hasForbiddenControls(const QString &text, bool allowBreaks) const;
    bool sendDraft(qint64 dialogId, const QString &kind, const QString &text, qint64 fileId, qint64 replyTo,
                   const QString &quote, bool silent, qint64 scheduleAt, qint64 topicId, const QString &entities);
    void rpcOk(const QString &method, const QJsonObject &params, const std::function<void(const QJsonObject &)> &ok);
    void refreshProfile();
    void ensureSecret(qint64 dialogId);
    void setCaptureBlocked(bool blocked);
    void noteViews(qint64 dialogId);
    void ingest(const QVariantMap &message);
    void eraseMessage(qint64 dialogId, qint64 messageId);
    QString presentMessage(const QVariantMap &source) const;
    QString secretPlain(qint64 dialogId, const QString &text) const;
    QByteArray secretAes(qint64 dialogId) const;
    void showBoard(const QString &title, const QVariantList &rows);
    void writeChunk();
    void finishUpload();
    void loadAccountList();
    QString deviceName() const;
    QString deviceId() const;
    void armSession();
    void publishIdentity();
    void onCallMedia(const QJsonObject &data);
    void noteRoom(const QJsonObject &data, const QString &kind);
    void openCallMedia(int port);
    void closeCallMedia();
    void sendCallHello();
    void readCallMedia();
    void beginCallAudio();
    void stopCallAudio();
    void pumpCallMic();
    void sendCallPcm(const QByteArray &pcm);
    void ensureCallSpeaker();
    void writeCallRing();
    bool callNeedsRing() const;
    bool selfInCall(const QVariantMap &call) const;

    QTcpSocket *m_socket = nullptr;
    QTimer *m_reconnectTimer = nullptr;
    QTimer *m_pingTimer = nullptr;
    QTimer *m_timeoutTimer = nullptr;
    QByteArray m_buffer;
    QHash<qint64, PendingCall> m_pending;
    qint64 m_nextId = 0;
    int m_retryMs = 1000;
    bool m_wantConnection = false;
    bool m_connectAfterAbort = false;
    bool m_connected = false;
    QString m_linkState = QStringLiteral("offline");
    QString m_statusLine = QStringLiteral("Нет соединения");
    QString m_host = QStringLiteral("95.165.30.178");
    int m_port = 7410;
    QString m_token;
    bool m_authenticated = false;
    bool m_bindFailed = false;
    bool m_authBusy = false;
    QString m_authError;
    int m_authGen = 0;
    QueuedAuth m_queued = QueuedAuth::None;
    QString m_qUser;
    QString m_qPass;
    QString m_qName;
    qint64 m_authDeadline = 0;
    QVariantMap m_me;
    qint64 m_meId = 0;
    QVariantList m_dialogs;
    QString m_dialogFilter;
    QVariantList m_contacts;
    QVariantList m_messages;
    QVariantList m_members;
    QString m_myRole;
    qint64 m_membersDialogId = 0;
    QVariantList m_searchResults;
    int m_searchGen = 0;
    qint64 m_activeDialogId = 0;
    QHash<qint64, QVariantList> m_store;
    QHash<qint64, bool> m_hasMore;
    QHash<qint64, qint64> m_readCursor;
    QHash<qint64, int> m_historyGen;
    QHash<qint64, QVariantMap> m_users;
    QSet<qint64> m_pendingUsers;
    int m_historySerial = 0;
    bool m_historyLoading = false;
    QString m_cloudChallenge;
    QVariantList m_folders;
    QVariantList m_stories;
    QVariantList m_sessions;
    QVariantList m_topics;
    QVariantList m_board;
    QVariantList m_accounts;
    QVariantMap m_call;
    bool m_callLive = false;
    bool m_callIncoming = false;
    bool m_callMuted = false;
    qint64 m_ignoredCall = 0;
    int m_mediaPort = 0;
    bool m_mediaReady = false;
    bool m_mediaClosing = false;
    bool m_jsonAudio = false;
    bool m_jsonWarned = false;
    qint64 m_ringSample = 0;
    QTcpSocket *m_mediaSocket = nullptr;
    QByteArray m_mediaBuf;
    QTimer *m_callTimer = nullptr;
    QAudioSource *m_callMic = nullptr;
    QIODevice *m_callInput = nullptr;
    QAudioSink *m_callSpeaker = nullptr;
    QIODevice *m_callOutput = nullptr;
    QByteArray m_callPending;
    QString m_boardTitle;
    QString m_listMode = QStringLiteral("Все");
    bool m_voiceRecording = false;
    bool m_pinSet = false;
    QHash<qint64, QByteArray> m_aes;
    QSet<qint64> m_revealed;
    QSet<qint64> m_delivered;
    QByteArray m_xPriv;
    bool m_compress = false;
    int m_uploadChunk = 384 * 1024;
    QTimer *m_expireTimer = nullptr;
    QString m_upPath;
    QString m_upKind;
    QString m_upCaption;
    bool m_upSpoiler = false;
    bool m_uploading = false;
    bool m_upRecord = false;
    qint64 m_upFile = 0;
    qint64 m_upSent = 0;
    qint64 m_upSize = 0;
    QAudioSource *m_mic = nullptr;
    QBuffer *m_pcm = nullptr;
    QMediaPlayer *m_player = nullptr;
    QAudioOutput *m_audioOut = nullptr;
    QNetworkAccessManager *m_net = nullptr;
    qint64 m_packId = 0;
};
