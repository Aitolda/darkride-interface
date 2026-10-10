//
//  EditorToolBar.cpp
//  interface/src/editor/ui
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include "EditorToolBar.h"

#include <QtWidgets/QAction>
#include <QtWidgets/QActionGroup>
#include <QtWidgets/QLabel>
#include <QtWidgets/QMenu>
#include <QtWidgets/QSizePolicy>
#include <QtWidgets/QToolButton>
#include <QtWidgets/QWidget>

EditorToolBar::EditorToolBar(QWidget* parent) : QToolBar("Build", parent) {
    setObjectName("EditorToolBar");
    setMovable(false);
    setFloatable(false);
    setAllowedAreas(Qt::TopToolBarArea);
    setToolButtonStyle(Qt::ToolButtonTextOnly);

    // the tool keys are handled by the editor while the viewport has focus, not as global
    // shortcuts, so typing a W into chat never switches tools
    _tools = new QActionGroup(this);
    _tools->setExclusive(true);
    addTool("View", "Q", Tool::View);
    addTool("Move", "W", Tool::Move)->setChecked(true);
    addTool("Rotate", "E", Tool::Rotate);
    addTool("Scale", "R", Tool::Scale);
    addSeparator();

    _spaceAction = addAction("Global");
    _spaceAction->setCheckable(true);
    _spaceAction->setToolTip("Handle space: Global or Local (X)");
    connect(_spaceAction, &QAction::toggled, this, [this](bool local) {
        _spaceAction->setText(local ? "Local" : "Global");
        emit localSpaceToggled(local);
    });

    _pivotAction = addAction("Center");
    _pivotAction->setCheckable(true);
    _pivotAction->setToolTip("Handle position: Center or Pivot (Z)");
    connect(_pivotAction, &QAction::toggled, this, [this](bool pivot) {
        _pivotAction->setText(pivot ? "Pivot" : "Center");
        emit pivotToggled(pivot);
    });
    addSeparator();

    _snapAction = addAction("Snap");
    _snapAction->setCheckable(true);
    _snapAction->setToolTip("Snap to the grid increments (hold Cmd to snap while dragging)");
    connect(_snapAction, &QAction::toggled, this, &EditorToolBar::snapToggled);

    auto spacer = new QWidget(this);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    _spacerAction = addWidget(spacer);

    // a world instance is read-only until World Edit is on; Save World writes it back
    _worldEditAction = addAction("World Edit");
    _worldEditAction->setCheckable(true);
    _worldEditAction->setToolTip("Turn on edit mode in this instance so your changes count");
    connect(_worldEditAction, &QAction::triggered, this, &EditorToolBar::worldEditToggled);
    _saveWorldAction = addAction("Save World");
    _saveWorldAction->setToolTip("Save this instance's entities as the world");
    connect(_saveWorldAction, &QAction::triggered, this, &EditorToolBar::saveWorldRequested);
    _worldSeparator = addSeparator();
    setWorld(false, false, false, false);

    _status = new QLabel(this);
    _status->setObjectName("EditorStatus");
    addWidget(_status);

    _gridAction = addAction("Grid");
    _gridAction->setCheckable(true);
    _gridAction->setChecked(true);
    _gridAction->setToolTip("Show the grid");
    connect(_gridAction, &QAction::toggled, this, &EditorToolBar::gridToggled);

    auto exitAction = addAction("Exit Build Mode");
    exitAction->setToolTip("Back to your avatar (Cmd+B)");
    connect(exitAction, &QAction::triggered, this, &EditorToolBar::exitRequested);
}

void EditorToolBar::setCreateMenu(QMenu* menu) {
    if (!_createButton) {
        insertSeparator(_spacerAction);
        _createButton = new QToolButton(this);
        _createButton->setText("Create ▾");
        _createButton->setToolTip("Add an entity in the middle of the view");
        _createButton->setPopupMode(QToolButton::InstantPopup);
        insertWidget(_spacerAction, _createButton);
    }
    _createButton->setMenu(menu);
}

void EditorToolBar::setStatus(const QString& text) {
    _status->setText(text);
}

void EditorToolBar::setWorld(bool instance, bool canEdit, bool editing, bool busy) {
    _worldEditAction->setVisible(instance);
    _saveWorldAction->setVisible(instance);
    _worldSeparator->setVisible(instance);
    _worldEditAction->setEnabled(canEdit && !busy);
    _saveWorldAction->setEnabled(canEdit && editing && !busy);
    if (_worldEditAction->isChecked() != editing) {
        QSignalBlocker blocker(_worldEditAction);
        _worldEditAction->setChecked(editing);
    }
}

QAction* EditorToolBar::addTool(const QString& text, const QString& shortcutHint, Tool tool) {
    auto action = addAction(text);
    action->setCheckable(true);
    action->setToolTip(text + " (" + shortcutHint + ")");
    action->setData((int)tool);
    _tools->addAction(action);
    connect(action, &QAction::triggered, this, [this, tool] {
        if (_tool != tool) {
            _tool = tool;
            emit toolChanged(tool);
        }
    });
    return action;
}

void EditorToolBar::setTool(Tool tool) {
    for (auto action : _tools->actions()) {
        if ((Tool)action->data().toInt() == tool) {
            action->setChecked(true);
        }
    }
    if (_tool != tool) {
        _tool = tool;
        emit toolChanged(tool);
    }
}

void EditorToolBar::setLocalSpace(bool local) {
    _spaceAction->setChecked(local);
}

void EditorToolBar::setPivotMode(bool pivot) {
    _pivotAction->setChecked(pivot);
}

void EditorToolBar::setGridChecked(bool checked) {
    if (_gridAction->isChecked() != checked) {
        QSignalBlocker blocker(_gridAction);
        _gridAction->setChecked(checked);
    }
}
