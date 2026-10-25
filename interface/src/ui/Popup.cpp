//
//  Popup.cpp
//  interface/src/ui
//
//  Copyright 2026 Darkride.
//
//  Distributed under the MIT License.
//

#include "Popup.h"

#include <algorithm>

#include <QBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QScreen>
#include <QTimer>
#include <QToolButton>

#include <PathUtils.h>

namespace popup {

namespace {

const int ALERT_WIDTH = 260;
const int BANNER_WIDTH = 300;
#ifdef Q_OS_MAC
const int SHADOW = 0;  // macOS draws a frameless window's shadow from its opaque pixels
#else
const int SHADOW = 24;  // elsewhere the popup paints its own
#endif

const char* const STYLE = R"(
QLabel { background: transparent; }
QLabel#title { color: #ffffff; font-size: 13px; font-weight: bold; }
QLabel#text { color: rgba(255, 255, 255, 189); font-size: 11px; }
QLabel#bannerTitle { color: #ffffff; font-size: 13px; font-weight: 600; }
QLabel#bannerText { color: rgba(255, 255, 255, 189); font-size: 12px; }
QToolButton#details { color: rgba(255, 255, 255, 189); font-size: 11px; border: none; background: transparent; padding: 2px 4px; }
QPlainTextEdit#detailsText { color: rgba(255, 255, 255, 199); font-size: 11px; background: rgba(0, 0, 0, 71);
    border: 1px solid rgba(255, 255, 255, 20); border-radius: 6px; padding: 4px 6px; }
QLineEdit#field { color: #ffffff; font-size: 12px; background: rgba(0, 0, 0, 71); border: 1px solid rgba(255, 255, 255, 26);
    border-radius: 5px; padding: 3px 6px; selection-background-color: #0b6bd8; }
QLineEdit#field:focus { border: 1px solid rgba(10, 132, 255, 200); }
QPushButton { color: #ffffff; font-size: 13px; border: none; border-radius: 7px; background: rgba(255, 255, 255, 36); padding: 0 10px; }
QPushButton:pressed { background: rgba(255, 255, 255, 56); }
QPushButton[role="default"] { background: #0b6bd8; }
QPushButton[role="default"]:pressed { background: #0a5bb8; }
QPushButton[role="default"]:disabled { background: rgba(255, 255, 255, 20); color: rgba(255, 255, 255, 77); }
QPushButton[role="destructive"] { color: #ff453a; }
QPushButton:focus { border: 2px solid rgba(10, 132, 255, 150); }
)";

// The rounded panel every popup is drawn on, with its shadow where the system draws none.
void paintPanel(QWidget* widget, qreal radius) {
    QPainter painter(widget);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF panel = QRectF(widget->rect()).adjusted(SHADOW + 0.5, SHADOW + 0.5, -SHADOW - 0.5, -SHADOW - 0.5);
    painter.setPen(Qt::NoPen);
    for (int spread = SHADOW; spread > 0; spread -= 2) {
        const qreal fade = 1.0 - (qreal)spread / SHADOW;
        painter.setBrush(QColor(0, 0, 0, (int)(10 * fade * fade)));
        painter.drawRoundedRect(panel.adjusted(-spread, -spread + SHADOW / 3, spread, spread + SHADOW / 3),
                                radius + spread, radius + spread);
    }
    painter.setBrush(QColor(43, 43, 46));
    painter.setPen(QPen(QColor(255, 255, 255, 26), 1.0));
    painter.drawRoundedRect(panel, radius, radius);
}

// A popup's height for its fixed width. (adjustSize asks the layout for its height at the layout's own preferred
// width, narrower than the popup, so wrapped lines got counted twice and left gaps under them.)
void fitHeight(QWidget* popup) {
    popup->ensurePolished();
    QLayout* layout = popup->layout();
    layout->activate();
    popup->resize(popup->width(), layout->hasHeightForWidth() ? layout->totalHeightForWidth(popup->width())
                                                              : layout->totalSizeHint().height());
}

// A picture at a size in points, sharp on a high-density screen.
QPixmap sized(const QPixmap& picture, int points, qreal devicePixelRatio) {
    if (picture.isNull()) {
        return picture;
    }
    QPixmap result = picture.scaled(QSize(points, points) * devicePixelRatio, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    result.setDevicePixelRatio(devicePixelRatio);
    return result;
}

// The Details fold-out's chevron, pointing right while closed and down while open.
QIcon chevron(bool open) {
    const qreal ratio = 2.0;
    QPixmap pixmap(QSize(10, 10) * ratio);
    pixmap.setDevicePixelRatio(ratio);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(QColor(255, 255, 255, 189), 1.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    QPolygonF line;
    if (open) {
        line << QPointF(2.0, 3.5) << QPointF(5.0, 6.5) << QPointF(8.0, 3.5);
    } else {
        line << QPointF(3.5, 2.0) << QPointF(6.5, 5.0) << QPointF(3.5, 8.0);
    }
    painter.drawPolyline(line);
    return QIcon(pixmap);
}

QLabel* label(const char* name, const QString& text) {
    auto result = new QLabel(text);
    result->setObjectName(name);
    result->setWordWrap(true);
    result->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
    return result;
}

}

Alert::Alert(const QPixmap& picture, const QString& title, const QString& text, const QString& details,
             const std::vector<Button>& buttons, QWidget* parent, const Field* field) :
    QDialog(parent, Qt::Dialog | Qt::FramelessWindowHint) {
    setAttribute(Qt::WA_TranslucentBackground);
    setWindowModality(Qt::ApplicationModal);
    setWindowTitle(title);
    setFocusPolicy(Qt::StrongFocus);  // the alert holds the focus, so Return and Escape reach it
    setStyleSheet(STYLE);
    setFixedWidth(ALERT_WIDTH + 2 * SHADOW);
    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(SHADOW + 16, SHADOW + 20, SHADOW + 16, SHADOW + 16);
    layout->setSpacing(0);

    auto pictureLabel = new QLabel();
    pictureLabel->setPixmap(sized(picture, 64, devicePixelRatioF()));
    pictureLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(pictureLabel);
    layout->addSpacing(12);
    layout->addWidget(label("title", title));
    if (!text.isEmpty()) {
        layout->addSpacing(6);
        layout->addWidget(label("text", text));
    }
    if (field) {
        layout->addSpacing(10);
        _field = new QLineEdit(field->text);
        _field->setObjectName("field");
        _field->setPlaceholderText(field->placeholder);
        _field->setAttribute(Qt::WA_MacShowFocusRect, false);
        layout->addWidget(_field);
    }
    if (!details.isEmpty()) {
        layout->addSpacing(6);
        auto toggle = new QToolButton();
        toggle->setObjectName("details");
        toggle->setText("Details");
        toggle->setIcon(chevron(false));
        toggle->setIconSize(QSize(10, 10));
        toggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        toggle->setLayoutDirection(Qt::RightToLeft);  // the chevron after the word
        toggle->setFocusPolicy(Qt::TabFocus);
        toggle->setAttribute(Qt::WA_MacShowFocusRect, false);
        layout->addWidget(toggle, 0, Qt::AlignHCenter);
        auto box = new QPlainTextEdit(details);
        box->setObjectName("detailsText");
        box->setReadOnly(true);
        box->setFixedHeight(112);
        box->setFrameShape(QFrame::NoFrame);
        box->setAttribute(Qt::WA_MacShowFocusRect, false);
        box->setVisible(false);
        layout->addSpacing(4);
        layout->addWidget(box);
        QObject::connect(toggle, &QToolButton::clicked, this, [this, toggle, box]() {
            const bool open = !box->isVisible();
            box->setVisible(open);
            toggle->setIcon(chevron(open));
            fitHeight(this);
        });
    }
    layout->addSpacing(details.isEmpty() ? 16 : 12);

    // Two buttons side by side, Cancel always on the left (Caity, 10-19) and the default on the right; more,
    // stacked with the default on top. A button that deletes for good is never the default: Cancel is (10-18).
    bool destructive = false;
    for (int i = 0; i < (int)buttons.size(); i++) {
        if (buttons[i].role == Role::Default && _default < 0) {
            _default = i;
        }
        if (buttons[i].role == Role::Cancel && _cancel < 0) {
            _cancel = i;
        }
        destructive = destructive || buttons[i].role == Role::Destructive;
    }
    if (_default < 0 && destructive) {
        _default = _cancel;
    }
    const bool sideBySide = buttons.size() == 2;
    auto row = sideBySide ? (QBoxLayout*)new QHBoxLayout() : (QBoxLayout*)new QVBoxLayout();
    row->setSpacing(8);
    std::vector<int> order;
    for (int i = 0; i < (int)buttons.size(); i++) {
        if (i != _default) {
            order.push_back(i);
        }
    }
    if (_default >= 0) {
        if (sideBySide) {
            order.push_back(_default);
        } else {
            order.insert(order.begin(), _default);
        }
    }
    if (sideBySide && _cancel >= 0 && order.front() != _cancel) {
        order.erase(std::find(order.begin(), order.end(), _cancel));
        order.insert(order.begin(), _cancel);
    }
    for (int i : order) {
        auto button = new QPushButton(buttons[i].label);
        button->setProperty("role", i == _default ? "default" : buttons[i].role == Role::Destructive ? "destructive" : "plain");
        button->setFixedHeight(28);
        button->setAutoDefault(false);
        button->setFocusPolicy(Qt::TabFocus);  // no focus ring until the keyboard moves between buttons
        button->setAttribute(Qt::WA_MacShowFocusRect, false);
        QObject::connect(button, &QPushButton::clicked, this, [this, i]() {
            _answer = i;
            accept();
        });
        if (i == _default) {
            _defaultButton = button;
        }
        row->addWidget(button, 1);
    }
    layout->addLayout(row);

    // the default button waits for the field to hold something (unless the default is Cancel)
    if (_field && _defaultButton && _default != _cancel) {
        auto update = [this]() { _defaultButton->setEnabled(!fieldText().isEmpty()); };
        QObject::connect(_field, &QLineEdit::textChanged, this, update);
        update();
    }
    fitHeight(this);
}

QString Alert::fieldText() const {
    return _field ? _field->text().trimmed() : QString();
}

void Alert::paintEvent(QPaintEvent*) {
    paintPanel(this, 12.0);
}

// Return is the default button, Escape the cancel button, as in every Mac alert. (The field passes both on.)
void Alert::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        if (_defaultButton && _defaultButton->isEnabled()) {
            _answer = _default;
            accept();
        }
    } else if (event->key() == Qt::Key_Escape) {
        _answer = _cancel;
        reject();
    } else {
        QDialog::keyPressEvent(event);
    }
}

// Over the middle of the window, a third of the way down, where Mac alerts appear.
void Alert::showEvent(QShowEvent* event) {
    QDialog::showEvent(event);
    QWidget* anchor = parentWidget() ? parentWidget()->window() : nullptr;
    const QRect area = anchor ? anchor->geometry() : screen()->availableGeometry();
    move(area.center().x() - width() / 2, area.top() + (area.height() - height()) / 3);
    if (_field) {
        _field->setFocus();
        _field->selectAll();
    } else {
        setFocus();
    }
}

Banner::Banner(const QPixmap& picture, const QString& title, const QString& text, QWidget* anchor, int milliseconds) :
    QWidget(anchor, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowDoesNotAcceptFocus) {
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_DeleteOnClose);
    setStyleSheet(STYLE);
    setFixedWidth(BANNER_WIDTH + 2 * SHADOW);
    auto layout = new QHBoxLayout(this);
    layout->setContentsMargins(SHADOW + 12, SHADOW + 10, SHADOW + 12, SHADOW + 10);
    layout->setSpacing(10);
    auto pictureLabel = new QLabel();
    pictureLabel->setPixmap(sized(picture, 40, devicePixelRatioF()));
    pictureLabel->setFixedSize(40, 40);
    layout->addWidget(pictureLabel, 0, Qt::AlignVCenter);
    auto words = new QVBoxLayout();
    words->setSpacing(2);
    auto titleLabel = new QLabel(title);
    titleLabel->setObjectName("bannerTitle");
    titleLabel->setWordWrap(true);
    auto textLabel = new QLabel(text);
    textLabel->setObjectName("bannerText");
    textLabel->setWordWrap(true);
    words->addWidget(titleLabel);
    words->addWidget(textLabel);
    layout->addLayout(words, 1);
    fitHeight(this);
    if (anchor) {
        const QRect area = anchor->window()->geometry();
        move(area.right() - width() - 16 + SHADOW, area.top() + 16 - SHADOW);
        setWindowOpacity(0.0);
        show();
        fade(1.0, 180);
        QTimer::singleShot(milliseconds, this, [this]() { fade(0.0, 400, true); });
    }
}

void Banner::paintEvent(QPaintEvent*) {
    paintPanel(this, 14.0);
}

void Banner::mousePressEvent(QMouseEvent*) {
    close();
}

void Banner::fade(qreal to, int milliseconds, bool thenClose) {
    auto animation = new QPropertyAnimation(this, "windowOpacity", this);
    animation->setDuration(milliseconds);
    animation->setEndValue(to);
    if (thenClose) {
        QObject::connect(animation, &QPropertyAnimation::finished, this, &QWidget::close);
    }
    animation->start(QAbstractAnimation::DeleteWhenStopped);
}

QPixmap appIcon() {
    return QPixmap(PathUtils::resourcesPath() + "images/darkride-logo.png");
}

}
