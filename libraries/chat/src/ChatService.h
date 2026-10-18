//
//  ChatService.h
//  libraries/chat/src
//
//  Darkride chat in the viewer: Matrix at chat.darkride.ai through the Rust library in ../native (matrix-rust-sdk),
//  signed in with the viewer's Darkride account. The library takes commands as JSON and answers with events as JSON
//  on its own threads; this posts them to the main thread and keeps the latest room list for the panels.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#ifndef hifi_ChatService_h
#define hifi_ChatService_h

#include <QtCore/QElapsedTimer>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonObject>
#include <QtCore/QObject>
#include <QtCore/QTimer>

#include <DependencyManager.h>

class QNetworkReply;
struct DarkrideChat;

class ChatService : public QObject, public Dependency {
    Q_OBJECT
    SINGLETON_DEPENDENCY

public:
    enum class State {
        SignedOut,      // no Darkride account, or chat not started yet
        SigningIn,      // asking the metaverse for a chat token, or the homeserver for a session
        Ready,          // signed in as the Darkride account
        Unavailable,    // the metaverse refused (chat off, the account banned or unverified) or can't be reached
    };
    Q_ENUM(State)

    ChatService();
    ~ChatService();

    // follows the Darkride account from now on: signed in to chat while it is, signed out when it signs out
    void start();
    // at quit, before the networking goes; forget: sign out of chat too (the account isn't remembered on this
    // computer), waiting a moment for the homeserver to hear it
    void stop(bool forget);

    State state() const { return _state; }
    // a line for the panel when chat isn't ready: why, or what it is waiting for
    QString stateText() const { return _stateText; }
    QString userId() const { return _userId; }
    // the sync's state: running, offline (waiting for the server), error, idle, terminated
    QString syncState() const { return _syncState; }
    // [{id, name, dm, encrypted, invited, unread, notifications}]
    QJsonArray rooms() const { return _rooms; }

    void open(const QString& room, int count = 50);
    void close(const QString& room);
    void loadOlder(const QString& room, int count = 30);
    void send(const QString& room, const QString& body);
    void markRead(const QString& room);
    void join(const QString& room);
    void leave(const QString& room);
    // tries again after Unavailable
    void retry();

signals:
    void stateChanged(State state);
    void syncStateChanged(const QString& state);
    void roomsChanged(const QJsonArray& rooms);
    // the newest items of an open room: [{id, sender, name, body, kind, ts, own, encrypted, state}]
    void timelineChanged(const QString& room, const QJsonArray& items);
    // a command finished: {op, room?, start?}; start: the room's first message is in, there's nothing older
    void commandDone(const QJsonObject& done);
    void commandFailed(const QString& op, const QString& message);

private slots:
    void onAccountLoggedIn();
    void onAccountLoggedOut();
    void chatTokenReceived(QNetworkReply* reply);
    void chatTokenFailed(QNetworkReply* reply);

private:
    static void receiveEvent(const char* event, void* userData);
    void handleEvent(const QByteArray& json);
    void command(const QJsonObject& command);
    void requestChatToken();
    void createLibrary(const QString& homeserver);
    void freeLibrary();
    void settle();
    void setState(State state, const QString& text = QString());

    DarkrideChat* _chat { nullptr };
    QString _homeserver;
    bool _started { false };
    bool _tokenRequested { false };

    // the library's own state (signed_out, starting, ready) and whom it is signed in as
    QString _libraryState;
    QString _libraryUser;
    // from the metaverse: a two-minute token and the chat account it signs in as
    QString _token;
    QElapsedTimer _tokenAge;
    QString _expectedUser;
    // sign-ins tried since the last one that worked, so a refusal doesn't loop
    int _signInAttempts { 0 };
    QString _lastSignInError;

    State _state { State::SignedOut };
    QString _stateText;
    QString _userId;
    QString _syncState;
    QJsonArray _rooms;
    QTimer _retryTimer;
};

#endif // hifi_ChatService_h
