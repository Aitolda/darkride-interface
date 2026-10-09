//
//  Editor.cpp
//  interface/src/editor
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include "Editor.h"

#include <QtCore/QFile>
#include <QtGui/QKeyEvent>
#include <QtGui/QMouseEvent>
#include <QtGui/QWheelEvent>

#include <MainWindow.h>
#include <PathUtils.h>

#include "Application.h"
#include "Menu.h"
#include "ui/EditorToolBar.h"
#include "ui/HierarchyPanel.h"
#include "ui/InspectorPanel.h"

Editor::Editor() {
}

void Editor::setActive(bool active) {
    if (active == _active) {
        return;
    }
    _active = active;
    if (active) {
        createPanels();
        showPanels(true);
        _camera.enter();
        _grid.setVisible(_gridVisible);
        _axesValid = false;
        updateAxes();
    } else {
        if (auto window = qApp->getWindow()) {
            _layoutSetting.set(window->saveState());
        }
        showPanels(false);
        _camera.leave();
        _grid.setVisible(false);
    }
    Menu::getInstance()->setIsOptionChecked(MenuOption::BuildMode, active);
    emit activeChanged(active);
}

void Editor::setGridVisible(bool visible) {
    if (visible == _gridVisible) {
        return;
    }
    _gridVisible = visible;
    if (_active) {
        _grid.setVisible(visible);
    }
    Menu::getInstance()->setIsOptionChecked(MenuOption::BuildGrid, visible);
    if (_toolBar) {
        _toolBar->setGridChecked(visible);
    }
    emit gridVisibleChanged(visible);
}

void Editor::createPanels() {
    if (_panelsCreated) {
        return;
    }
    auto window = qApp->getWindow();
    if (!window) {
        return;
    }
    _panelsCreated = true;

    // one dark stylesheet for the panels; scoped by object name so the rest of the window is untouched
    QFile styleSheet(PathUtils::resourcesPath() + "styles/editor.qss");
    if (styleSheet.open(QIODevice::ReadOnly)) {
        window->setStyleSheet(window->styleSheet() + "\n" + styleSheet.readAll());
    }

    _hierarchy = new HierarchyPanel(window);
    _inspector = new InspectorPanel(window);
    _toolBar = new EditorToolBar(window);
    _toolBar->setGridChecked(_gridVisible);

    window->addDockWidget(Qt::LeftDockWidgetArea, _hierarchy);
    window->addDockWidget(Qt::RightDockWidgetArea, _inspector);
    window->addToolBar(Qt::TopToolBarArea, _toolBar);

    connect(_toolBar, &EditorToolBar::exitRequested, this, [this] { setActive(false); });
    connect(_toolBar, &EditorToolBar::gridToggled, this, &Editor::setGridVisible);

    QByteArray layout = _layoutSetting.get();
    if (!layout.isEmpty()) {
        window->restoreState(layout);
    }
}

void Editor::showPanels(bool show) {
    if (!_panelsCreated) {
        return;
    }
    _hierarchy->setVisible(show);
    _inspector->setVisible(show);
    _toolBar->setVisible(show);
}

void Editor::update(float deltaTime) {
    if (!_active) {
        return;
    }
    _camera.update(deltaTime);
    updateAxes();
}

void Editor::updateAxes() {
    glm::quat orientation = _camera.orientation();
    if (_axesValid && orientation == _axesOrientation) {
        return;
    }
    _axesOrientation = orientation;
    _axesValid = true;
    glm::quat inverse = glm::inverse(orientation);
    for (int i = 0; i < 3; ++i) {
        glm::vec3 axis(0.0f);
        axis[i] = 1.0f;
        glm::vec3 v = inverse * axis;
        _axes[i] = QVector3D(v.x, v.y, v.z);
    }
    emit cameraChanged();
}

void Editor::viewFromAxis(int axis, int sign) {
    if (!_active) {
        return;
    }
    _camera.viewFromAxis(axis, sign);
    updateAxes();
}

bool Editor::handleMousePress(QMouseEvent* event) {
    if (!_active) {
        return false;
    }
    if (_camera.mousePress(event)) {
        return true;
    }
    // selection comes next; until then swallow clicks so entity scripts never fire in build mode
    return true;
}

bool Editor::handleMouseMove(QMouseEvent* event) {
    if (!_active) {
        return false;
    }
    return _camera.mouseMove(event);
}

bool Editor::handleMouseRelease(QMouseEvent* event) {
    if (!_active) {
        return false;
    }
    _camera.mouseRelease(event);
    return true;
}

bool Editor::handleMouseDoubleClick(QMouseEvent* event) {
    Q_UNUSED(event);
    return _active;
}

bool Editor::handleWheel(QWheelEvent* event) {
    if (!_active) {
        return false;
    }
    bool consumed = _camera.wheel(event);
    emit cameraChanged();
    return consumed;
}

bool Editor::handleKeyPress(QKeyEvent* event) {
    if (!_active) {
        return false;
    }
    if (_camera.keyPress(event)) {
        return true;
    }
    switch (event->key()) {
        case Qt::Key_F:
            // frame the selection: nothing to frame yet
            return true;
        default:
            return false;
    }
}

bool Editor::handleKeyRelease(QKeyEvent* event) {
    if (!_active) {
        return false;
    }
    return _camera.keyRelease(event);
}
