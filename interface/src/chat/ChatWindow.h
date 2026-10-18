//
//  ChatWindow.h
//  interface/src/chat
//
//  Darkride chat's window (View > Chat): the channels and direct messages down the left, the conversation and the
//  message box on the right, as the web chat at chat.darkride.ai lays them out. A window of its own beside the
//  viewer's, so opening it never resizes the 3D view. ChatService (libraries/chat) does the talking.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#ifndef hifi_ChatWindow_h
#define hifi_ChatWindow_h

#include <QtCore/QJsonArray>
#include <QtCore/QJsonObject>
#include <QtCore/QPointer>
#include <QtCore/QSet>
#include <QtGui/QKeySequence>
#include <QtWidgets/QWidget>

class QLabel;
class QListWidget;
class QListWidgetItem;
class QPlainTextEdit;
class QPushButton;
class QStackedWidget;
class QTextBrowser;

class ChatWindow : public QWidget {
    Q_OBJECT
public:
    // View > Chat: shows the window (made the first time), or hides it when it is already in front
    static void toggle();
    static QKeySequence shortcut();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void changeEvent(QEvent* event) override;

private:
    explicit ChatWindow(QWidget* parent);

    void updateState();
    void updateRooms(const QJsonArray& rooms);
    void updateTimeline(const QString& room, const QJsonArray& items);
    void commandDone(const QJsonObject& done);
    void updateSync(const QString& state);
    void selectRoom(const QString& room);
    QJsonObject roomById(const QString& room) const;
    void showRoomPage();
    void renderTimeline();
    void sendMessage();
    void markReadIfSeen();
    void fitComposer();
    // a round picture with the first letter of the name, on the person's or room's own colour
    QString avatarSource(const QString& seed, const QString& name);
    void saveGeometrySetting();

    static QPointer<ChatWindow> _instance;

    QListWidget* _roomList { nullptr };
    QLabel* _userAvatar { nullptr };
    QLabel* _userName { nullptr };
    QLabel* _userStatus { nullptr };
    QLabel* _roomTitle { nullptr };
    QStackedWidget* _pages { nullptr };
    QLabel* _stateText { nullptr };
    QPushButton* _retryButton { nullptr };
    QLabel* _inviteText { nullptr };
    QTextBrowser* _timeline { nullptr };
    QPlainTextEdit* _composer { nullptr };
    QLabel* _composerNote { nullptr };

    QJsonArray _rooms;
    QString _room;            // the room shown
    QJsonArray _items;        // its newest messages
    bool _roomStart { false };    // its first message is among them: nothing older to load
    bool _loadingOlder { false };
    bool _anchorBottom { false }; // the next render keeps the distance from the bottom (older messages came in above)
    QSet<QString> _avatars;   // the pictures already given to the timeline's document
};

#endif // hifi_ChatWindow_h
