//
//  Popup.h
//  interface/src/ui
//
//  Darkride's native popups, in the design language Caity chose for all of them (10-18): a narrow centred alert in the
//  manner of macOS, with a picture on top, a bold title, at most one short line, and full-width buttons, side by side
//  when there are two (Cancel on the left, the default on the right) and stacked when there are more (the default on
//  top), the default in blue; a button that deletes something for good is red and never the default (Cancel is);
//  further detail sits behind a small Details fold-out, in a scrolling field; a question that needs a word or a link
//  has one field under the line. A success gets no alert but a banner in the window's corner that fades by itself.
//  The viewer's other popups share this design in QML (interface/resources/qml/dialogs/alert/AlertPanel.qml).
//  Moved out of the avatar importer (10-25) so build mode's questions use it too.
//
//  Copyright 2026 Darkride.
//
//  Distributed under the MIT License.
//

#ifndef hifi_Popup_h
#define hifi_Popup_h

#include <vector>

#include <QDialog>
#include <QPixmap>
#include <QString>
#include <QWidget>

class QLineEdit;
class QPushButton;

namespace popup {

enum class Role { Default, Cancel, Plain, Destructive };

struct Button {
    QString label;  // "&&" for an ampersand: a button reads "&" as a keyboard shortcut mark
    Role role;
};

// The one field of a question that needs a word or a link; the default button waits until it holds something.
struct Field {
    QString placeholder;
    QString text;
};

class Alert : public QDialog {
public:
    Alert(const QPixmap& picture, const QString& title, const QString& text, const QString& details,
          const std::vector<Button>& buttons, QWidget* parent, const Field* field = nullptr);

    int answer() const { return _answer; }
    QString fieldText() const;  // trimmed; empty without a field

protected:
    void paintEvent(QPaintEvent*) override;
    void keyPressEvent(QKeyEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    int _answer { -1 };
    int _default { -1 };
    int _cancel { -1 };
    QLineEdit* _field { nullptr };
    QPushButton* _defaultButton { nullptr };
};

// A note that comes and goes in the top right corner of the window: clicked, it goes at once.
class Banner : public QWidget {
public:
    Banner(const QPixmap& picture, const QString& title, const QString& text, QWidget* anchor, int milliseconds = 4500);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;

private:
    void fade(qreal to, int milliseconds, bool thenClose = false);
};

// Darkride's logo, the picture of a popup with no subject of its own.
QPixmap appIcon();

}

#endif  // hifi_Popup_h
