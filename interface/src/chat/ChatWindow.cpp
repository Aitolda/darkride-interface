//
//  ChatWindow.cpp
//  interface/src/chat
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include "ChatWindow.h"

#include <algorithm>

#include <QtCore/QDateTime>
#include <QtCore/QRegularExpression>
#include <QtCore/QTimer>
#include <QtGui/QDesktopServices>
#include <QtGui/QKeyEvent>
#include <QtGui/QPainter>
#include <QtGui/QPainterPath>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QListWidget>
#include <QtWidgets/QPlainTextEdit>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QScrollBar>
#include <QtWidgets/QShortcut>
#include <QtWidgets/QStackedWidget>
#include <QtWidgets/QStyledItemDelegate>
#include <QtWidgets/QTextBrowser>
#include <QtWidgets/QVBoxLayout>

#include <AccountManager.h>
#include <ChatService.h>
#include <SettingHandle.h>

#include "Application.h"
#include "MainWindow.h"

// the web chat's colours (darkride_chat client/src/style.css), from the Darkride website's
static const QColor COLOUR_SIDEBAR("#171b2d");
static const QColor COLOUR_PAGE("#1c2136");
static const QColor COLOUR_SURFACE("#242a44");
static const QColor COLOUR_SELECTION("#333a5a");
static const QColor COLOUR_TEXT("#f4f6f8");
static const QColor COLOUR_MUTED("#9aa1bd");
static const QColor COLOUR_CYAN("#00afff");
static const QColor COLOUR_RED("#ff5555");
static const QColor COLOUR_VIOLET("#a548ff");
// a person's or room's own colour behind its first letter, as the web chat picks it (avatar.ts)
static const char* AVATAR_COLOURS[] = { "#0090d6", "#8a3bdc", "#2f9e5b", "#d4692b", "#c43f82", "#23a39a" };

// messages from one person this close together share one name and picture, as in Discord
static const qint64 GROUP_GAP_MS = 7 * 60 * 1000;
// the library sends a room's newest 100 messages; loading older ones stops there for now
static const int TIMELINE_ITEMS = 100;

static Setting::Handle<QByteArray> chatWindowGeometry("chat/windowGeometry");
static Setting::Handle<QString> chatLastRoom("chat/room");

enum RoomRole {
    RoomIdRole = Qt::UserRole,
    RoomKindRole,       // header, channel, dm, invite
    RoomUnreadRole,
    RoomNotificationsRole,
    RoomAvatarRole,     // a dm's picture
};

static QColor colourFor(const QString& seed) {
    quint32 n = 0;
    for (QChar c : seed) {
        n = n * 31 + c.unicode();
    }
    return QColor(AVATAR_COLOURS[n % (sizeof(AVATAR_COLOURS) / sizeof(AVATAR_COLOURS[0]))]);
}

static QString initialOf(const QString& name) {
    QString trimmed = name;
    if (trimmed.startsWith('@') || trimmed.startsWith('#')) {
        trimmed.remove(0, 1);
    }
    trimmed = trimmed.trimmed();
    return trimmed.isEmpty() ? QString("?") : trimmed.left(trimmed.at(0).isHighSurrogate() ? 2 : 1).toUpper();
}

static QImage avatarImage(const QString& seed, const QString& name, int size, qreal pixelRatio) {
    QImage image(QSize(size, size) * pixelRatio, QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(pixelRatio);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(colourFor(seed));
    painter.drawEllipse(QRectF(0, 0, size, size));
    QFont font = painter.font();
    font.setPixelSize(qRound(size * 0.42));
    font.setWeight(QFont::Medium);
    painter.setFont(font);
    painter.setPen(Qt::white);
    painter.drawText(QRectF(0, 0, size, size), Qt::AlignCenter, initialOf(name));
    return image;
}

// the part of @name:server before the colon
static QString localpartOf(const QString& userId) {
    QString id = userId.startsWith('@') ? userId.mid(1) : userId;
    int colon = id.indexOf(':');
    return colon < 0 ? id : id.left(colon);
}

static QString whenText(qint64 ms) {
    QDateTime when = QDateTime::fromMSecsSinceEpoch(ms);
    QDate today = QDate::currentDate();
    QString clock = when.time().toString("HH:mm");
    if (when.date() == today) {
        return "Today at " + clock;
    }
    if (when.date() == today.addDays(-1)) {
        return "Yesterday at " + clock;
    }
    return when.date().toString("dd/MM/yyyy") + " " + clock;
}

static QString dayText(qint64 ms) {
    return QDateTime::fromMSecsSinceEpoch(ms).date().toString("MMMM d, yyyy");
}

// plain text as rich text: escaped, web addresses made links, line breaks kept
static QString richBody(const QString& body) {
    static const QRegularExpression url(R"((https?://[^\s<]*[^\s<.,;:!?)\]'"]))");
    QString html = body.toHtmlEscaped();
    html.replace(url, "<a href=\"\\1\">\\1</a>");
    return html;
}

// --- the room list: section headings, channels with "#", direct messages with the person's picture ----------------

class RoomDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        QString kind = index.data(RoomKindRole).toString();
        return QSize(option.rect.width(), kind == "header" ? 34 : kind == "dm" ? 42 : 34);
    }

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        QString kind = index.data(RoomKindRole).toString();
        QString name = index.data(Qt::DisplayRole).toString();
        QRect rect = option.rect;
        QFont font = option.font;

        if (kind == "header") {
            font.setPixelSize(11);
            font.setWeight(QFont::DemiBold);
            font.setCapitalization(QFont::AllUppercase);
            painter->setFont(font);
            painter->setPen(COLOUR_MUTED);
            painter->drawText(rect.adjusted(16, 12, -8, 0), Qt::AlignLeft | Qt::AlignVCenter, name);
            painter->restore();
            return;
        }

        int unread = index.data(RoomUnreadRole).toInt();
        int notifications = index.data(RoomNotificationsRole).toInt();
        bool selected = option.state & QStyle::State_Selected;
        bool hovered = option.state & QStyle::State_MouseOver;
        bool bright = selected || hovered || unread > 0 || kind == "invite";

        QRectF row = QRectF(rect).adjusted(8, 1, -8, -1);
        QPainterPath shape;
        shape.addRoundedRect(row, 4, 4);
        if (selected) {
            painter->fillPath(shape, COLOUR_SELECTION);
        } else if (kind == "invite") {
            painter->fillPath(shape, QColor(165, 72, 255, 31));
        } else if (hovered) {
            painter->fillPath(shape, QColor(255, 255, 255, 13));
        }
        // a channel with new messages: the small white pip at the left edge, as Discord marks it
        if (kind == "channel" && unread > 0 && !selected) {
            QPainterPath pip;
            pip.addRoundedRect(QRectF(rect.left(), rect.center().y() - 4, 4, 8), 2, 2);
            painter->fillPath(pip, COLOUR_TEXT);
        }

        int x = row.left() + 8;
        if (kind == "dm" || kind == "invite") {
            QImage picture = index.data(RoomAvatarRole).value<QImage>();
            int size = 28;
            painter->drawImage(QRectF(x, row.center().y() - size / 2.0, size, size), picture);
            x += size + 10;
        } else {
            QFont hash = font;
            hash.setPixelSize(20);
            painter->setFont(hash);
            painter->setPen(COLOUR_MUTED);
            painter->drawText(QRect(x, row.top(), 20, row.height()), Qt::AlignCenter, "#");
            x += 28;
        }

        // the count on the right: red for mentions and direct messages, as Discord's
        QString badge;
        QColor badgeColour = COLOUR_SELECTION;
        if (notifications > 0) {
            badge = QString::number(notifications);
            badgeColour = COLOUR_RED;
        } else if (kind == "dm" && unread > 0) {
            badge = QString::number(unread);
        }
        int right = row.right() - 8;
        if (!badge.isEmpty()) {
            QFont small = font;
            small.setPixelSize(12);
            small.setWeight(QFont::Bold);
            painter->setFont(small);
            int width = std::max(18, QFontMetrics(small).horizontalAdvance(badge) + 10);
            QRectF pill(right - width, row.center().y() - 9, width, 18);
            QPainterPath path;
            path.addRoundedRect(pill, 9, 9);
            painter->fillPath(path, badgeColour);
            painter->setPen(Qt::white);
            painter->drawText(pill, Qt::AlignCenter, badge);
            right -= width + 6;
        }

        font.setPixelSize(15);
        font.setWeight(unread > 0 ? QFont::Medium : QFont::Normal);
        painter->setFont(font);
        painter->setPen(bright ? COLOUR_TEXT : COLOUR_MUTED);
        QRect textRect(x, row.top(), right - x, row.height());
        if (kind == "invite") {
            QFontMetrics metrics(font);
            painter->drawText(textRect.adjusted(0, 0, 0, -12), Qt::AlignLeft | Qt::AlignVCenter,
                              metrics.elidedText(name, Qt::ElideRight, textRect.width()));
            QFont sub = font;
            sub.setPixelSize(12);
            sub.setWeight(QFont::Normal);
            painter->setFont(sub);
            painter->setPen(COLOUR_MUTED);
            painter->drawText(textRect.adjusted(0, 16, 0, 0), Qt::AlignLeft | Qt::AlignVCenter, "Invited");
        } else {
            painter->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter,
                              QFontMetrics(font).elidedText(name, Qt::ElideRight, textRect.width()));
        }
        painter->restore();
    }
};

// --- the window ---------------------------------------------------------------------------------------------------

QPointer<ChatWindow> ChatWindow::_instance;

QKeySequence ChatWindow::shortcut() {
    return QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_D);
}

void ChatWindow::toggle() {
    if (!_instance) {
        _instance = new ChatWindow(qApp->getWindow());
    }
    if (_instance->isVisible() && _instance->isActiveWindow()) {
        _instance->hide();
        return;
    }
    _instance->show();
    _instance->raise();
    _instance->activateWindow();
    if (_instance->_composer->isEnabled()) {
        _instance->_composer->setFocus();
    }
}

ChatWindow::ChatWindow(QWidget* parent) : QWidget(parent, Qt::Tool) {
    setObjectName("DarkrideChat");
    setWindowTitle("Darkride Chat");
    setMinimumSize(640, 420);
    resize(940, 640);
    setStyleSheet(QString(R"(
        #DarkrideChat { background: %1; }
        #ChatSidebar { background: %2; }
        #ChatSidebarHead, #ChatRoomHead {
            color: %3; font-size: 15px; font-weight: 500; padding: 0 16px;
            border-bottom: 1px solid rgba(0, 0, 0, 0.35);
        }
        #ChatRooms { background: %2; border: none; outline: 0; }
        #ChatMe { background: #10142a; }
        #ChatMeName { color: %3; font-size: 14px; font-weight: 500; }
        #ChatMeStatus { color: %4; font-size: 12px; }
        #ChatState { color: %4; font-size: 15px; }
        #ChatTimeline { background: %1; border: none; }
        #ChatComposer {
            background: %5; color: %3; border: none; border-radius: 8px; padding: 8px 12px; font-size: 15px;
            selection-background-color: %6;
        }
        #ChatComposerNote { color: %4; font-size: 12px; }
        QPushButton {
            background: %7; color: #091023; border: none; border-radius: 4px; padding: 7px 16px; font-weight: 500;
        }
        QPushButton:hover { background: #33c0ff; }
        QPushButton#ChatDecline { background: %5; color: %3; }
        QScrollBar:vertical { background: transparent; width: 10px; margin: 2px; }
        QScrollBar::handle:vertical { background: %2; border-radius: 4px; min-height: 30px; }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
        QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }
    )")
                      .arg(COLOUR_PAGE.name(), COLOUR_SIDEBAR.name(), COLOUR_TEXT.name(), COLOUR_MUTED.name(),
                           COLOUR_SURFACE.name(), COLOUR_SELECTION.name(), COLOUR_CYAN.name()));

    auto layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // the left: Darkride's channels, then direct messages, then who you are
    auto sidebar = new QWidget(this);
    sidebar->setObjectName("ChatSidebar");
    sidebar->setAttribute(Qt::WA_StyledBackground);
    sidebar->setFixedWidth(240);
    auto sidebarLayout = new QVBoxLayout(sidebar);
    sidebarLayout->setContentsMargins(0, 0, 0, 0);
    sidebarLayout->setSpacing(0);
    auto sidebarHead = new QLabel("Darkride", sidebar);
    sidebarHead->setObjectName("ChatSidebarHead");
    sidebarHead->setFixedHeight(48);
    sidebarLayout->addWidget(sidebarHead);
    _roomList = new QListWidget(sidebar);
    _roomList->setObjectName("ChatRooms");
    _roomList->setItemDelegate(new RoomDelegate(_roomList));
    _roomList->setMouseTracking(true);
    _roomList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    _roomList->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    sidebarLayout->addWidget(_roomList, 1);
    auto me = new QWidget(sidebar);
    me->setObjectName("ChatMe");
    me->setAttribute(Qt::WA_StyledBackground);
    me->setFixedHeight(52);
    auto meLayout = new QHBoxLayout(me);
    meLayout->setContentsMargins(8, 6, 8, 6);
    meLayout->setSpacing(8);
    _userAvatar = new QLabel(me);
    _userAvatar->setFixedSize(32, 32);
    meLayout->addWidget(_userAvatar);
    auto meText = new QVBoxLayout();
    meText->setSpacing(0);
    _userName = new QLabel(me);
    _userName->setObjectName("ChatMeName");
    _userStatus = new QLabel(me);
    _userStatus->setObjectName("ChatMeStatus");
    meText->addWidget(_userName);
    meText->addWidget(_userStatus);
    meLayout->addLayout(meText, 1);
    sidebarLayout->addWidget(me);
    layout->addWidget(sidebar);

    // the right: the room's name, then its conversation and the message box (or why there is none)
    auto main = new QWidget(this);
    auto mainLayout = new QVBoxLayout(main);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);
    _roomTitle = new QLabel(main);
    _roomTitle->setObjectName("ChatRoomHead");
    _roomTitle->setFixedHeight(48);
    _roomTitle->setTextFormat(Qt::PlainText);
    mainLayout->addWidget(_roomTitle);
    _pages = new QStackedWidget(main);
    mainLayout->addWidget(_pages, 1);
    layout->addWidget(main, 1);

    // page 0: signing in, signed out, or chat unavailable
    auto statePage = new QWidget(_pages);
    auto stateLayout = new QVBoxLayout(statePage);
    stateLayout->addStretch(1);
    _stateText = new QLabel(statePage);
    _stateText->setObjectName("ChatState");
    _stateText->setAlignment(Qt::AlignCenter);
    _stateText->setWordWrap(true);
    stateLayout->addWidget(_stateText);
    _retryButton = new QPushButton("Try Again", statePage);
    stateLayout->addWidget(_retryButton, 0, Qt::AlignHCenter);
    stateLayout->addStretch(1);
    _pages->addWidget(statePage);
    connect(_retryButton, &QPushButton::clicked, this, [] {
        if (auto service = DependencyManager::get<ChatService>()) {
            service->retry();
        }
    });

    // page 1: an invitation, with Join and Decline
    auto invitePage = new QWidget(_pages);
    auto inviteLayout = new QVBoxLayout(invitePage);
    inviteLayout->addStretch(1);
    _inviteText = new QLabel(invitePage);
    _inviteText->setObjectName("ChatState");
    _inviteText->setAlignment(Qt::AlignCenter);
    _inviteText->setWordWrap(true);
    inviteLayout->addWidget(_inviteText);
    auto inviteButtons = new QHBoxLayout();
    inviteButtons->addStretch(1);
    auto join = new QPushButton("Join", invitePage);
    auto decline = new QPushButton("Decline", invitePage);
    decline->setObjectName("ChatDecline");
    inviteButtons->addWidget(join);
    inviteButtons->addWidget(decline);
    inviteButtons->addStretch(1);
    inviteLayout->addLayout(inviteButtons);
    inviteLayout->addStretch(1);
    _pages->addWidget(invitePage);
    connect(join, &QPushButton::clicked, this, [this] {
        if (auto service = DependencyManager::get<ChatService>()) {
            service->join(_room);
        }
    });
    connect(decline, &QPushButton::clicked, this, [this] {
        if (auto service = DependencyManager::get<ChatService>()) {
            service->leave(_room);
        }
    });

    // page 2: the conversation
    auto conversation = new QWidget(_pages);
    auto conversationLayout = new QVBoxLayout(conversation);
    conversationLayout->setContentsMargins(0, 0, 0, 0);
    conversationLayout->setSpacing(0);
    _timeline = new QTextBrowser(conversation);
    _timeline->setObjectName("ChatTimeline");
    _timeline->setOpenLinks(false);
    _timeline->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    _timeline->document()->setDocumentMargin(0);
    _timeline->document()->setDefaultStyleSheet(
        QString("a { color: %1; text-decoration: none; } body { color: %2; }").arg(COLOUR_CYAN.name(), COLOUR_TEXT.name()));
    conversationLayout->addWidget(_timeline, 1);
    auto composerArea = new QWidget(conversation);
    auto composerLayout = new QVBoxLayout(composerArea);
    composerLayout->setContentsMargins(16, 0, 16, 4);
    composerLayout->setSpacing(2);
    _composer = new QPlainTextEdit(composerArea);
    _composer->setObjectName("ChatComposer");
    _composer->setTabChangesFocus(true);
    _composer->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    _composer->installEventFilter(this);
    composerLayout->addWidget(_composer);
    _composerNote = new QLabel(composerArea);
    _composerNote->setObjectName("ChatComposerNote");
    _composerNote->setFixedHeight(18);
    composerLayout->addWidget(_composerNote);
    conversationLayout->addWidget(composerArea);
    _pages->addWidget(conversation);

    connect(_timeline, &QTextBrowser::anchorClicked, this, [](const QUrl& url) {
        if (url.scheme() == "https" || url.scheme() == "http") {
            QDesktopServices::openUrl(url);
        }
    });
    connect(_timeline->verticalScrollBar(), &QScrollBar::valueChanged, this, [this](int value) {
        auto service = DependencyManager::get<ChatService>();
        if (value == 0 && service && !_room.isEmpty() && !_roomStart && !_loadingOlder && !_items.isEmpty() &&
            _items.size() < TIMELINE_ITEMS) {
            _loadingOlder = true;
            _anchorBottom = true;
            service->loadOlder(_room);
        }
        markReadIfSeen();
    });
    connect(_composer->document(), &QTextDocument::contentsChanged, this, &ChatWindow::fitComposer);
    connect(_roomList, &QListWidget::currentItemChanged, this, [this](QListWidgetItem* item) {
        if (item && item->data(RoomKindRole).toString() != "header") {
            selectRoom(item->data(RoomIdRole).toString());
        }
    });

    // the menu's shortcut hides it again from here too (on Windows the menu's shortcuts work only in the main window)
    auto hideShortcut = new QShortcut(shortcut(), this);
    connect(hideShortcut, &QShortcut::activated, this, &QWidget::hide);

    if (auto service = DependencyManager::get<ChatService>()) {
        connect(service.data(), &ChatService::stateChanged, this, &ChatWindow::updateState);
        connect(service.data(), &ChatService::roomsChanged, this, &ChatWindow::updateRooms);
        connect(service.data(), &ChatService::timelineChanged, this, &ChatWindow::updateTimeline);
        connect(service.data(), &ChatService::commandDone, this, &ChatWindow::commandDone);
        connect(service.data(), &ChatService::syncStateChanged, this, &ChatWindow::updateSync);
        connect(service.data(), &ChatService::commandFailed, this, [this](const QString& op, const QString& message) {
            if (op == "send" || op == "join" || op == "leave" || op == "open") {
                _composerNote->setText("Couldn't " + op + ": " + message);
            }
        });
    }

    QByteArray geometry = chatWindowGeometry.get();
    if (!geometry.isEmpty()) {
        restoreGeometry(geometry);
    }
    fitComposer();
    updateState();
}

void ChatWindow::updateState() {
    auto service = DependencyManager::get<ChatService>();
    bool ready = service && service->state() == ChatService::State::Ready;

    QString username = DependencyManager::get<AccountManager>()->getAccountInfo().getUsername();
    QString userId = service ? service->userId() : QString();
    _userName->setText(username.isEmpty() ? QString("Not signed in") : username);
    _userAvatar->setPixmap(QPixmap::fromImage(
        avatarImage(userId.isEmpty() ? username : userId, username.isEmpty() ? "?" : username, 32, devicePixelRatioF())));
    updateSync(service ? service->syncState() : QString());

    if (!ready) {
        if (!_room.isEmpty() && service) {
            service->close(_room);
        }
        _room.clear();
        _items = QJsonArray();
        _rooms = QJsonArray();
        _roomList->clear();
        _roomTitle->clear();
        _stateText->setText(service ? service->stateText() : QString("Chat isn't running."));
        _retryButton->setVisible(service && service->state() == ChatService::State::Unavailable);
        _pages->setCurrentIndex(0);
        return;
    }
    updateRooms(service->rooms());
}

void ChatWindow::updateSync(const QString& state) {
    auto service = DependencyManager::get<ChatService>();
    if (!service || service->state() != ChatService::State::Ready) {
        _userStatus->setText(service && service->state() == ChatService::State::SigningIn ? "Signing in…" : "Offline");
    } else if (state == "running" || state == "idle") {
        _userStatus->setText("Online");
    } else if (state == "offline" || state == "error") {
        _userStatus->setText("Reconnecting…");
    } else {
        _userStatus->setText("Connecting…");
    }
}

void ChatWindow::updateRooms(const QJsonArray& rooms) {
    _rooms = rooms;
    QList<QJsonObject> invites, channels, dms;
    for (const QJsonValue& value : rooms) {
        QJsonObject room = value.toObject();
        if (room.value("invited").toBool()) {
            invites << room;
        } else if (room.value("dm").toBool()) {
            dms << room;
        } else {
            channels << room;
        }
    }
    // channels stay put, by name, as a server's channels do in Discord; direct messages come newest first, as the
    // library sends them
    std::sort(channels.begin(), channels.end(), [](const QJsonObject& a, const QJsonObject& b) {
        return a.value("name").toString().compare(b.value("name").toString(), Qt::CaseInsensitive) < 0;
    });

    QSignalBlocker blocker(_roomList);
    int scroll = _roomList->verticalScrollBar()->value();
    _roomList->clear();
    auto addHeader = [this](const QString& title) {
        auto item = new QListWidgetItem(title, _roomList);
        item->setData(RoomKindRole, "header");
        item->setFlags(Qt::NoItemFlags);
    };
    QListWidgetItem* current = nullptr;
    auto addRoom = [&](const QJsonObject& room, const QString& kind) {
        QString id = room.value("id").toString();
        QString name = room.value("name").toString();
        auto item = new QListWidgetItem(name, _roomList);
        item->setData(RoomIdRole, id);
        item->setData(RoomKindRole, kind);
        item->setData(RoomUnreadRole, room.value("unread").toInt());
        item->setData(RoomNotificationsRole, room.value("notifications").toInt());
        if (kind != "channel") {
            item->setData(RoomAvatarRole, avatarImage(id, name, 28, devicePixelRatioF()));
        }
        if (id == _room) {
            current = item;
        }
    };
    if (!invites.isEmpty()) {
        addHeader("Invites");
        for (const auto& room : invites) {
            addRoom(room, "invite");
        }
    }
    addHeader("Channels");
    for (const auto& room : channels) {
        addRoom(room, "channel");
    }
    if (!dms.isEmpty()) {
        addHeader("Direct Messages");
        for (const auto& room : dms) {
            addRoom(room, "dm");
        }
    }
    if (current) {
        _roomList->setCurrentItem(current);
    }
    _roomList->verticalScrollBar()->setValue(scroll);
    blocker.unblock();

    if (_room.isEmpty() || !current) {
        // the room shown last time, else the first channel (General)
        QString wanted = chatLastRoom.get();
        QString chosen;
        for (const QJsonValue& value : rooms) {
            if (value.toObject().value("id").toString() == wanted) {
                chosen = wanted;
            }
        }
        if (chosen.isEmpty() && !channels.isEmpty()) {
            chosen = channels.first().value("id").toString();
        }
        if (chosen.isEmpty() && !dms.isEmpty()) {
            chosen = dms.first().value("id").toString();
        }
        if (!chosen.isEmpty()) {
            selectRoom(chosen);
        } else {
            _room.clear();
            _roomTitle->clear();
            _stateText->setText("No rooms yet.");
            _retryButton->hide();
            _pages->setCurrentIndex(0);
        }
    } else {
        // an invitation accepted here or elsewhere becomes a room to talk in
        showRoomPage();
    }
}

QJsonObject ChatWindow::roomById(const QString& room) const {
    for (const QJsonValue& value : _rooms) {
        if (value.toObject().value("id").toString() == room) {
            return value.toObject();
        }
    }
    return QJsonObject();
}

void ChatWindow::selectRoom(const QString& room) {
    auto service = DependencyManager::get<ChatService>();
    if (!service || room == _room) {
        return;
    }
    if (!_room.isEmpty()) {
        service->close(_room);
    }
    _room = room;
    _items = QJsonArray();
    _roomStart = false;
    _loadingOlder = false;
    _anchorBottom = false;
    _composerNote->clear();
    chatLastRoom.set(room);
    for (int i = 0; i < _roomList->count(); i++) {
        if (_roomList->item(i)->data(RoomIdRole).toString() == room) {
            QSignalBlocker blocker(_roomList);
            _roomList->setCurrentRow(i);
        }
    }
    if (!roomById(room).value("invited").toBool()) {
        service->open(room);
    }
    showRoomPage();
    renderTimeline();
}

void ChatWindow::showRoomPage() {
    QJsonObject room = roomById(_room);
    bool dm = room.value("dm").toBool();
    QString name = room.value("name").toString();
    _roomTitle->setText((dm ? "@ " : "# ") + name);
    if (room.value("invited").toBool()) {
        _inviteText->setText(dm ? name + " wants to talk with you." : "You're invited to " + name + ".");
        _pages->setCurrentIndex(1);
        return;
    }
    _composer->setPlaceholderText("Message " + QString(dm ? "@" : "#") + name);
    if (_pages->currentIndex() != 2) {
        _pages->setCurrentIndex(2);
        if (isActiveWindow()) {
            _composer->setFocus();
        }
    }
}

void ChatWindow::updateTimeline(const QString& room, const QJsonArray& items) {
    if (room != _room) {
        return;
    }
    _items = items;
    renderTimeline();
    markReadIfSeen();
}

void ChatWindow::commandDone(const QJsonObject& done) {
    QString op = done.value("op").toString();
    QString room = done.value("room").toString();
    if ((op == "open" || op == "back") && room == _room) {
        _roomStart = done.value("start").toBool();
        _loadingOlder = false;
        if (op == "open") {
            renderTimeline();   // the beginning of the room, or the end of "Loading…"
        }
    } else if (op == "join") {
        _room.clear();
        selectRoom(room);
    } else if (op == "leave" && room == _room) {
        _room.clear();
        chatLastRoom.set(QString());
        updateRooms(_rooms);
    }
}

QString ChatWindow::avatarSource(const QString& seed, const QString& name) {
    QString key = seed + "\n" + initialOf(name);
    QString source = "avatar:" + QString::number(qHash(key));
    if (!_avatars.contains(key)) {
        _timeline->document()->addResource(QTextDocument::ImageResource, QUrl(source),
                                           avatarImage(seed, name, 40, devicePixelRatioF()));
        _avatars.insert(key);
    }
    return source;
}

void ChatWindow::renderTimeline() {
    QScrollBar* scroll = _timeline->verticalScrollBar();
    bool atBottom = scroll->value() >= scroll->maximum() - 4;
    int fromBottom = scroll->maximum() - scroll->value();
    int fromTop = scroll->value();

    QJsonObject room = roomById(_room);
    QString name = room.value("name").toString();
    bool dm = room.value("dm").toBool();
    QString muted = COLOUR_MUTED.name();

    QString html = "<html><body style=\"font-size:15px;\">";
    if (_items.isEmpty() && !_roomStart) {
        html += QString("<p style=\"color:%1; margin:24px 16px;\">Loading…</p>").arg(muted);
    }
    if (_roomStart) {
        // the room's first message is here: a welcome, as Discord starts a channel
        html += QString("<p style=\"margin:32px 16px 4px 16px; font-size:28px; font-weight:bold;\">%1</p>")
                    .arg(dm ? name.toHtmlEscaped() : "Welcome to #" + name.toHtmlEscaped());
        html += QString("<p style=\"color:%1; margin:0 16px 8px 16px;\">%2</p>")
                    .arg(muted, dm ? "This is the beginning of your direct messages with " + name.toHtmlEscaped() + "."
                                   : "This is the start of the #" + name.toHtmlEscaped() + " channel.");
    } else if (_loadingOlder) {
        html += QString("<p style=\"color:%1; margin:12px 16px;\">Loading…</p>").arg(muted);
    }

    QString lastSender;
    qint64 lastTs = 0;
    QDate lastDay;
    for (const QJsonValue& value : _items) {
        QJsonObject item = value.toObject();
        qint64 ts = (qint64)item.value("ts").toDouble();
        QString sender = item.value("sender").toString();
        QString senderName = item.value("name").toString();
        if (senderName.isEmpty()) {
            senderName = localpartOf(sender);
        }
        QDate day = QDateTime::fromMSecsSinceEpoch(ts).date();
        if (day != lastDay) {
            html += QString("<p align=\"center\" style=\"color:%1; font-size:12px; font-weight:500; margin:20px 16px 4px 16px;\">"
                            "%2</p>")
                        .arg(muted, dayText(ts));
            lastDay = day;
            lastSender.clear();
        }

        QString kind = item.value("kind").toString();
        QString body = item.value("body").toString();
        QString text;
        if (kind == "text") {
            text = richBody(body);
        } else if (kind == "notice") {
            text = QString("<span style=\"color:%1;\">%2</span>").arg(muted, richBody(body));
        } else if (kind == "emote") {
            text = "<i>* " + senderName.toHtmlEscaped() + " " + richBody(body) + "</i>";
        } else if (kind == "image" || kind == "file" || kind == "video" || kind == "audio") {
            static const QMap<QString, QString> words {
                { "image", "Picture" }, { "file", "File" }, { "video", "Video" }, { "audio", "Sound" } };
            text = QString("<span style=\"color:%1;\">[%2] %3</span>").arg(muted, words.value(kind), body.toHtmlEscaped());
        } else if (kind == "undecryptable") {
            text = QString("<i style=\"color:%1;\">An encrypted message this device can't read yet.</i>").arg(muted);
        } else {
            text = QString("<i style=\"color:%1;\">A message this chat can't show yet.</i>").arg(muted);
        }
        QString state = item.value("state").toString();
        if (state == "sending" || state == "waiting") {
            text = QString("<span style=\"color:%1;\">%2</span>").arg(muted, text);
        } else if (state == "failed") {
            text += QString(" <span style=\"color:%1; font-size:12px;\">Not sent</span>").arg(COLOUR_RED.name());
        }
        text = "<span style=\"white-space:pre-wrap;\">" + text + "</span>";

        bool grouped = sender == lastSender && ts - lastTs < GROUP_GAP_MS;
        if (grouped) {
            html += QString("<table width=\"100%\" cellspacing=\"0\" cellpadding=\"0\" style=\"margin-top:2px;\"><tr>"
                            "<td width=\"72\"></td><td style=\"padding-right:16px;\">%1</td></tr></table>")
                        .arg(text);
        } else {
            html += QString("<table width=\"100%\" cellspacing=\"0\" cellpadding=\"0\" style=\"margin-top:14px;\"><tr>"
                            "<td width=\"72\" valign=\"top\" style=\"padding-left:16px; padding-top:2px;\">"
                            "<img src=\"%1\" width=\"40\" height=\"40\"></td>"
                            "<td valign=\"top\" style=\"padding-right:16px;\">"
                            "<span style=\"color:%2; font-weight:500;\">%3</span>&nbsp;&nbsp;"
                            "<span style=\"color:%4; font-size:12px;\">%5</span><br>%6</td></tr></table>")
                        .arg(avatarSource(sender, senderName), COLOUR_CYAN.name(), senderName.toHtmlEscaped(), muted,
                             whenText(ts), text);
        }
        lastSender = sender;
        lastTs = ts;
    }
    html += "<p style=\"margin:0 0 12px 0;\"></p></body></html>";
    _timeline->setHtml(html);

    // stay at the bottom when there; keep one's place when reading back; older messages loading come in above
    auto place = [this, atBottom, fromBottom, fromTop, anchorBottom = _anchorBottom] {
        QScrollBar* scroll = _timeline->verticalScrollBar();
        if (anchorBottom) {
            scroll->setValue(scroll->maximum() - fromBottom);
        } else if (atBottom) {
            scroll->setValue(scroll->maximum());
        } else {
            scroll->setValue(fromTop);
        }
    };
    if (!_items.isEmpty()) {
        _anchorBottom = false;
    }
    place();
    // the document may still be laying itself out
    QTimer::singleShot(0, this, place);
}

void ChatWindow::sendMessage() {
    auto service = DependencyManager::get<ChatService>();
    QString text = _composer->toPlainText();
    while (text.endsWith('\n') || text.endsWith(' ')) {
        text.chop(1);
    }
    if (!service || _room.isEmpty() || text.trimmed().isEmpty()) {
        return;
    }
    service->send(_room, text);
    _composer->clear();
    _composerNote->clear();
    // one's own message is the newest: down to it
    QScrollBar* scroll = _timeline->verticalScrollBar();
    scroll->setValue(scroll->maximum());
}

void ChatWindow::markReadIfSeen() {
    auto service = DependencyManager::get<ChatService>();
    if (!service || _room.isEmpty() || !isVisible() || !isActiveWindow() || _pages->currentIndex() != 2) {
        return;
    }
    QScrollBar* scroll = _timeline->verticalScrollBar();
    if (scroll->value() < scroll->maximum() - 4) {
        return;
    }
    QJsonObject room = roomById(_room);
    if (room.value("unread").toInt() > 0 || room.value("notifications").toInt() > 0) {
        service->markRead(_room);
    }
}

void ChatWindow::fitComposer() {
    // one line to start, growing with what's typed up to eight
    int lines = std::min(8, std::max(1, (int)_composer->document()->size().height()));
    int height = lines * _composer->fontMetrics().lineSpacing() + 2 * (int)_composer->document()->documentMargin() + 18;
    _composer->setFixedHeight(height);
}

bool ChatWindow::eventFilter(QObject* watched, QEvent* event) {
    if (watched == _composer && event->type() == QEvent::KeyPress) {
        auto key = static_cast<QKeyEvent*>(event);
        if ((key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) && !(key->modifiers() & Qt::ShiftModifier)) {
            sendMessage();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void ChatWindow::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    markReadIfSeen();
}

void ChatWindow::hideEvent(QHideEvent* event) {
    saveGeometrySetting();
    QWidget::hideEvent(event);
}

void ChatWindow::changeEvent(QEvent* event) {
    QWidget::changeEvent(event);
    if (event->type() == QEvent::ActivationChange) {
        markReadIfSeen();
    }
}

void ChatWindow::saveGeometrySetting() {
    chatWindowGeometry.set(saveGeometry());
}
