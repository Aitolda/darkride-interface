//
//  QmlDock.cpp
//  interface/src/ui
//
//  Copyright 2026 Darkride.
//
//  Distributed under the MIT License.
//

#include "QmlDock.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QHash>
#include <QtCore/QPointer>
#include <QtGui/QScreen>
#include <QtQml/QQmlComponent>
#include <QtQml/QQmlContext>
#include <QtQml/QQmlEngine>
#include <QtQuick/QQuickItem>
#include <QtQuickWidgets/QQuickWidget>
#include <QtWidgets/QAbstractButton>

#include <gl/GLHelpers.h>
#include <MainWindow.h>
#include <OffscreenUi.h>

#include "Application.h"
#include "DockTitleBar.h"
#include "InterfaceLogging.h"
#include "WindowLayout.h"

namespace {
    // the overlay window's size, which the panels were laid out for
    const QSize FLOATING_SIZE { 560, 640 };

    struct Panel {
        QString name;
        QString source;
    };
    const QList<Panel> PANELS {
        { "People", "qrc:///qml/darkride/PeoplePanel.qml" },
    };

    QHash<QString, QPointer<QmlDock>>& docks() {
        static QHash<QString, QPointer<QmlDock>> theDocks;
        return theDocks;
    }

    // floating at its first size in the middle of the main window, kept on the main window's screen (as chat's)
    void placeFloating(QDockWidget* panel, QWidget* window) {
        QRect screen = window->screen() ? window->screen()->availableGeometry() : QRect();
        QRect rect(QPoint(), screen.isValid() ? FLOATING_SIZE.boundedTo(screen.size()) : FLOATING_SIZE);
        rect.moveCenter(window->geometry().center());
        if (screen.isValid()) {
            rect.moveLeft(std::max(screen.left(), std::min(rect.left(), screen.right() + 1 - rect.width())));
            rect.moveTop(std::max(screen.top(), std::min(rect.top(), screen.bottom() + 1 - rect.height())));
        }
        panel->resize(rect.size());
        panel->move(rect.topLeft());
    }
}

QStringList QmlDock::names() {
    QStringList result;
    for (const auto& panel : PANELS) {
        result << panel.name;
    }
    return result;
}

QmlDock* QmlDock::get(const QString& name) {
    QPointer<QmlDock>& dock = docks()[name];
    if (dock) {
        return dock;
    }
    for (const auto& panel : PANELS) {
        if (panel.name == name) {
            gl::ViewChangeGuard guard;
            MainWindow* window = qApp->getWindow();
            dock = new QmlDock(panel.name, QUrl(panel.source), window);
            // hidden before it joins the main window, so it never shows there by itself; floating, so opening it
            // never resizes the 3D view
            dock->hide();
            window->addDockWidget(Qt::RightDockWidgetArea, dock);
            dock->setFloating(true);
            placeFloating(dock, window);
            // where the saved layout had it, once the layout has been restored (after sign-in)
            WindowLayout::instance()->restoreDock(dock);
            WindowLayout::instance()->watch(dock);
            return dock;
        }
    }
    return nullptr;
}

QmlDock::QmlDock(const QString& name, const QUrl& source, QWidget* parent) : QDockWidget(name, parent), _source(source) {
    setObjectName(name + "Dock");
    setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    setFeatures(DockWidgetClosable | DockWidgetMovable | DockWidgetFloatable);
    setTitleBarWidget(new DockTitleBar(this));
    for (auto button : findChildren<QAbstractButton*>()) {
        button->installEventFilter(this);
    }
    // its QML made the first time it shows (a drag, the saved layout, or toggle)
    connect(this, &QDockWidget::visibilityChanged, this, [this](bool visible) {
        if (visible) {
            makeContent();
        }
    });
    connect(this, &QDockWidget::topLevelChanged, this, [this](bool floating) {
        qCDebug(interfaceapp) << objectName() << (floating ? "floating" : "docked");
    });
}

void QmlDock::makeContent() {
    if (_view) {
        return;
    }
    auto offscreenUi = DependencyManager::get<OffscreenUi>();
    QQmlContext* desktop = offscreenUi ? offscreenUi->getSurfaceContext() : nullptr;
    if (!desktop) {
        qCWarning(interfaceapp) << objectName() << "has no QML engine to make its panel in";
        return;
    }
    gl::ViewChangeGuard guard;
    QQmlEngine* engine = desktop->engine();
    _view = new QQuickWidget(engine, this);
    _view->setResizeMode(QQuickWidget::SizeRootObjectToView);
    _view->setMinimumSize(320, 240);
    // the names the overlay panels find are the desktop's: the panel is made in a context under it
    auto context = new QQmlContext(desktop, _view);
    auto component = new QQmlComponent(engine, _source, QQmlComponent::PreferSynchronous, _view);
    QObject* object = component->isReady() ? component->create(context) : nullptr;
    if (!object) {
        qCWarning(interfaceapp) << objectName() << "couldn't make" << _source << component->errorString();
        return;
    }
    _view->setContent(_source, component, object);
    setWidget(_view);
    qCDebug(interfaceapp) << objectName() << "made," << (isFloating() ? "floating" : "docked");
}

QQuickItem* QmlDock::panel() const {
    return _view ? _view->rootObject() : nullptr;
}

void QmlDock::toggle() {
    gl::ViewChangeGuard guard;
    if (isVisible() && (isActiveWindow() || !isFloating())) {
        hide();
        return;
    }
    show();
    // in front of the other windows when floating; docked among others as tabs, the tab in front
    raise();
    if (isFloating()) {
        activateWindow();
    }
}

// a panel's own window coming or going beside the viewer's changes the GL view's surface on macOS: its float and close
// buttons, a double-click on its title and the end of a drag that docks it happen under the guard (as chat's)
bool QmlDock::event(QEvent* event) {
    switch (event->type()) {
        case QEvent::MouseButtonDblClick:
        case QEvent::MouseButtonRelease:
        case QEvent::NonClientAreaMouseButtonDblClick:
        case QEvent::NonClientAreaMouseButtonRelease:
        case QEvent::Close: {
            gl::ViewChangeGuard guard(event->type());
            return QDockWidget::event(event);
        }
        default:
            return QDockWidget::event(event);
    }
}

bool QmlDock::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::MouseButtonRelease && !_releasing) {
        gl::ViewChangeGuard guard(event->type());
        _releasing = true;
        QCoreApplication::sendEvent(watched, event);
        _releasing = false;
        return true;
    }
    return QDockWidget::eventFilter(watched, event);
}
