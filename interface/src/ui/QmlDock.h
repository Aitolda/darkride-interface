//
//  QmlDock.h
//  interface/src/ui
//
//  A dock panel that shows one of Darkride's QML panels (qml/darkride/*Panel.qml: People first) natively, so it can sit
//  beside the 3D view, be tabbed with other panels or float in its own window, as chat and Build mode's panels do.
//  Dockable panels, step 2 (Caity's "go step 2", 2026-10-26; team/editor-ui/DOCKING-STEP2-PLAN.md). The panel is the
//  same QML its overlay window showed, made on the app's one QML engine in a context under the desktop's (so it finds
//  MetaverseRequests, the account, MyAvatar and the rest by the same names) and drawn by a QQuickWidget. Its QML is
//  made the first time the panel shows, and a hidden panel draws nothing. It opens floating at the overlay window's
//  size, so opening it never resizes the 3D view; docking it is the player's drag. Every float, dock and close happens
//  under gl::ViewChangeGuard, as chat's (chat/ChatWindow.cpp's ChatDock).
//
//  Copyright 2026 Darkride.
//
//  Distributed under the MIT License.
//

#ifndef hifi_QmlDock_h
#define hifi_QmlDock_h

#include <QtCore/QUrl>
#include <QtWidgets/QDockWidget>

class QQuickItem;
class QQuickWidget;

class QmlDock : public QDockWidget {
    Q_OBJECT
public:
    // the panels a QmlDock can show, by name: "People"
    static QStringList names();
    // the panel by its name, made (hidden, floating) the first time it is asked for; null for a name it doesn't know
    static QmlDock* get(const QString& name);

    // shown in front, or hidden when it is in front already
    void toggle();
    // the panel's QML, once made (for tests)
    QQuickItem* panel() const;

protected:
    bool event(QEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    QmlDock(const QString& name, const QUrl& source, QWidget* parent);
    void makeContent();

    QUrl _source;
    QQuickWidget* _view { nullptr };
    bool _releasing { false };
};

#endif // hifi_QmlDock_h
