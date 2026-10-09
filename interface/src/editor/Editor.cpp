//
//  Editor.cpp
//  interface/src/editor
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include "Editor.h"

#include <algorithm>
#include <cmath>

#include <QtCore/QFile>
#include <QtGui/QGuiApplication>
#include <QtGui/QKeyEvent>
#include <QtGui/QMouseEvent>
#include <QtGui/QWheelEvent>
#include <QtWidgets/QAction>

#include <EntityItem.h>
#include <EntityScriptingInterface.h>
#include <EntityTree.h>
#include <EntityTreeRenderer.h>
#include <gl/GLWidget.h>
#include <GLMHelpers.h>
#include <MainWindow.h>
#include <PathUtils.h>
#include <PickFilter.h>

#include "Application.h"
#include "EntityTreeModel.h"
#include "Menu.h"
#include "ui/HierarchyPanel.h"
#include "ui/InspectorPanel.h"

namespace {
    const float GIZMO_PIXELS = 110.0f;      // the gizmo's size on screen
    const int CLICK_SLOP_PIXELS = 5;        // a press that moves less than this is a click
    const int EDIT_INTERVAL_MS = 33;        // while dragging, edits go out at most this often
    const int PRUNE_EVERY_FRAMES = 30;      // how often the selection drops entities that are gone

    float tanHalfFov() {
        return 1.0f / qApp->getCamera().getProjection()[1][1];
    }

    float viewportHeight() {
        auto viewport = qApp->getPrimaryWidget();
        return viewport ? (float)std::max(1, viewport->height()) : 1.0f;
    }

    glm::vec3 snapVector(const glm::vec3& v, float increment) {
        return glm::round(v / increment) * increment;
    }
}

Editor::Editor() : _selection(this), _undo(this) {
    connect(&_selection, &EditorSelection::changed, this, &Editor::onSelectionChanged);
}

// --- build mode on and off ----------------------------------------------------------------------

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
        _selection.setHighlightEnabled(true);
        if (_treeModel) {
            _treeModel->attach();
        }
        _axesValid = false;
        updateAxes();
        updateInspector();
        focusViewport();
    } else {
        if (_gizmo.isDragging()) {
            applyDrag(true);
            _gizmo.endDrag();
        }
        if (auto window = qApp->getWindow()) {
            _layoutSetting.set(window->saveState());
        }
        showPanels(false);
        _camera.leave();
        _grid.setVisible(false);
        _gizmo.setVisible(false);
        _selection.setHighlightEnabled(false);
        _selection.clear();
    }
    setMenuActionsEnabled(active);
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

void Editor::setTool(EditorToolBar::Tool tool) {
    if (_gizmo.isDragging()) {
        return;
    }
    _tool = tool;
    if (_toolBar) {
        _toolBar->setTool(tool);
    }
    updateGizmo();
}

void Editor::setMenuActionsEnabled(bool enabled) {
    auto menu = Menu::getInstance();
    for (const auto& name : { MenuOption::BuildUndo, MenuOption::BuildRedo, MenuOption::BuildDuplicate, MenuOption::BuildFrame }) {
        if (auto action = menu->getActionForOption(name)) {
            action->setEnabled(enabled);
        }
    }
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

    _treeModel = new EntityTreeModel(this);
    connect(_treeModel, &EntityTreeModel::renameRequested, this, &Editor::rename);

    _hierarchy = new HierarchyPanel(window);
    _hierarchy->setModel(_treeModel);
    connect(_hierarchy, &HierarchyPanel::selectionChangedByUser, this, [this](const QList<QUuid>& ids) {
        if (_syncingSelection) {
            return;
        }
        _syncingSelection = true;
        _selection.set(ids);
        _syncingSelection = false;
    });
    connect(_hierarchy, &HierarchyPanel::frameRequested, this, [this](const QUuid& id) {
        _selection.set({ id });
        frameSelection();
    });

    _inspector = new InspectorPanel(window);

    _toolBar = new EditorToolBar(window);
    _toolBar->setGridChecked(_gridVisible);
    _toolBar->setTool(_tool);
    connect(_toolBar, &EditorToolBar::exitRequested, this, [this] { setActive(false); });
    connect(_toolBar, &EditorToolBar::gridToggled, this, &Editor::setGridVisible);
    connect(_toolBar, &EditorToolBar::toolChanged, this, [this](EditorToolBar::Tool tool) {
        _tool = tool;
        updateGizmo();
    });
    connect(_toolBar, &EditorToolBar::snapToggled, this, [this](bool on) { _snapAlways = on; });

    window->addDockWidget(Qt::LeftDockWidgetArea, _hierarchy);
    window->addDockWidget(Qt::RightDockWidgetArea, _inspector);
    window->addToolBar(Qt::TopToolBarArea, _toolBar);

    QByteArray layout = _layoutSetting.get();
    if (!layout.isEmpty()) {
        window->restoreState(layout);
    }
}

void Editor::focusViewport() {
    if (auto viewport = qApp->getPrimaryWidget()) {
        if (!viewport->hasFocus()) {
            viewport->setFocus(Qt::MouseFocusReason);
        }
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

// --- every frame --------------------------------------------------------------------------------

void Editor::update(float deltaTime) {
    if (!_active) {
        return;
    }
    _camera.update(deltaTime);
    updateAxes();
    if (++_frameCounter % PRUNE_EVERY_FRAMES == 0 && !_gizmo.isDragging()) {
        _selection.prune();
    }
    updateGizmo();
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

void Editor::updateGizmo() {
    bool show = _active && _tool == EditorToolBar::Tool::Move && !_selection.isEmpty();
    if (show) {
        glm::vec3 pivot = _gizmo.isDragging() ? _dragPivot + _dragDelta : _selection.center();
        float distance = std::max(0.01f, glm::length(pivot - _camera.position()));
        float scale = distance * 2.0f * tanHalfFov() * (GIZMO_PIXELS / viewportHeight());
        _gizmo.setTransform(pivot, glm::quat(), scale);
    }
    _gizmo.setVisible(show);
}

void Editor::viewFromAxis(int axis, int sign) {
    if (!_active) {
        return;
    }
    _camera.viewFromAxis(axis, sign);
    updateAxes();
}

// --- selection ----------------------------------------------------------------------------------

void Editor::onSelectionChanged() {
    if (_hierarchy && !_syncingSelection) {
        _syncingSelection = true;
        _hierarchy->selectIds(_selection.ids());
        _syncingSelection = false;
    }
    updateInspector();
    updateGizmo();
}

void Editor::updateInspector() {
    if (!_inspector) {
        return;
    }
    int count = _selection.count();
    if (count == 0) {
        _inspector->setSummary("Nothing selected");
    } else if (count == 1 && _treeModel) {
        QModelIndex index = _treeModel->indexForId(_selection.primary());
        QString name = index.data(Qt::DisplayRole).toString();
        QString type = index.data(EntityTreeModel::TypeNameRole).toString();
        _inspector->setSummary(name + "\n" + type);
    } else {
        _inspector->setSummary(QString("%1 entities selected").arg(count));
    }
}

PickRay Editor::pickRay(const QPoint& viewportPos) const {
    return qApp->computePickRay((float)viewportPos.x(), (float)viewportPos.y());
}

QUuid Editor::pickEntity(const QPoint& viewportPos) const {
    PickFilter::Flags flags;
    flags.set(PickFilter::DOMAIN_ENTITIES);
    flags.set(PickFilter::VISIBLE);
    flags.set(PickFilter::COLLIDABLE);
    flags.set(PickFilter::NONCOLLIDABLE);
    flags.set(PickFilter::PRECISE);
    auto entities = DependencyManager::get<EntityScriptingInterface>();
    auto result = entities->evalRayIntersectionVector(pickRay(viewportPos), PickFilter(flags),
        QVector<EntityItemID>(), QVector<EntityItemID>());
    return result.intersects ? result.entityID : QUuid();
}

void Editor::clickSelect(const QPoint& viewportPos, Qt::KeyboardModifiers modifiers) {
    bool shift = modifiers.testFlag(Qt::ShiftModifier);
    bool command = modifiers.testFlag(Qt::ControlModifier);
    QUuid hit = pickEntity(viewportPos);
    if (hit.isNull()) {
        if (!shift && !command) {
            _selection.clear();
        }
        return;
    }
    if (command) {
        _selection.toggle(hit);
    } else if (shift) {
        _selection.add(hit);
    } else {
        _selection.set({ hit });
    }
}

void Editor::selectAll() {
    if (_active && _treeModel) {
        _selection.set(_treeModel->allIds());
    }
}

void Editor::frameSelection() {
    AABox box;
    if (_active && _selection.bounds(box)) {
        _camera.frame(box);
        updateAxes();
    }
}

// --- the gizmo drag -----------------------------------------------------------------------------

bool Editor::snapActive() const {
    return _snapAlways || QGuiApplication::keyboardModifiers().testFlag(Qt::ControlModifier);
}

void Editor::beginGizmoDrag(GizmoHandle handle, const PickRay& ray) {
    _dragIds.clear();
    _dragStart.clear();
    _dragBefore.clear();
    auto tree = qApp->getEntities() ? qApp->getEntities()->getTree() : nullptr;
    if (!tree) {
        return;
    }
    tree->withReadLock([&] {
        for (const auto& id : _selection.topLevel()) {
            auto entity = tree->findEntityByEntityItemID(id);
            if (!entity || entity->getLocked()) {
                continue;
            }
            glm::vec3 position = entity->getWorldPosition();
            EntityItemProperties before;
            before.setPosition(position);
            _dragIds << id;
            _dragStart << position;
            _dragBefore << before;
        }
    });
    if (_dragIds.isEmpty()) {
        return;
    }
    _dragPivot = _gizmo.position();
    _dragDelta = glm::vec3(0.0f);
    _editTimer.start();
    _gizmo.beginDrag(handle, ray, _camera.orientation() * Vectors::FRONT);
}

void Editor::applyDrag(bool final) {
    _editTimer.restart();
    auto entities = DependencyManager::get<EntityScriptingInterface>();
    QVector<EntityItemProperties> after;
    for (int i = 0; i < _dragIds.size(); ++i) {
        EntityItemProperties properties;
        properties.setPosition(_dragStart[i] + _dragDelta);
        entities->editEntity(_dragIds[i], properties);
        after << properties;
    }
    if (final && _dragDelta != glm::vec3(0.0f)) {
        _undo.pushPropertyEdit("Move", _dragIds, _dragBefore, after, true);
    }
}

// --- edits --------------------------------------------------------------------------------------

void Editor::undo() {
    if (_active && !_gizmo.isDragging()) {
        _undo.stack()->undo();
    }
}

void Editor::redo() {
    if (_active && !_gizmo.isDragging()) {
        _undo.stack()->redo();
    }
}

void Editor::rename(const QUuid& id, const QString& name) {
    if (!_treeModel) {
        return;
    }
    EntityItemProperties before;
    before.setName(_treeModel->indexForId(id).data(Qt::EditRole).toString());
    EntityItemProperties after;
    after.setName(name);
    _undo.pushPropertyEdit("Rename", { id }, { before }, { after }, false);
}

void Editor::deleteSelection() {
    if (!_active || _selection.isEmpty() || _gizmo.isDragging()) {
        return;
    }
    _undo.pushDelete("Delete", _selection.topLevel());
    _selection.clear();
}

void Editor::duplicateSelection() {
    if (!_active || _selection.isEmpty() || _gizmo.isDragging()) {
        return;
    }
    EditorClipboard copies;
    copies.copy(_selection.topLevel());
    QList<QUuid> created = copies.paste(_undo, "Duplicate");
    if (!created.isEmpty()) {
        _selection.set(created);
    }
}

void Editor::copySelection() {
    if (_active && !_selection.isEmpty()) {
        _clipboard.copy(_selection.topLevel());
    }
}

void Editor::cutSelection() {
    if (!_active || _selection.isEmpty() || _gizmo.isDragging()) {
        return;
    }
    _clipboard.copy(_selection.topLevel());
    deleteSelection();
}

void Editor::paste() {
    if (!_active || _clipboard.isEmpty() || _gizmo.isDragging()) {
        return;
    }
    QList<QUuid> created = _clipboard.paste(_undo, "Paste");
    if (!created.isEmpty()) {
        _selection.set(created);
    }
}

// --- input --------------------------------------------------------------------------------------

bool Editor::handleMousePress(QMouseEvent* event) {
    if (!_active) {
        return false;
    }
    // a press in the viewport takes the keyboard back from the panels (a right click would not, on macOS)
    focusViewport();
    if (_camera.mousePress(event)) {
        return true;
    }
    if (event->button() != Qt::LeftButton) {
        return true;
    }
    PickRay ray = pickRay(event->pos());
    if (_gizmo.isVisible()) {
        GizmoHandle handle = _gizmo.hitTest(ray);
        if (handle != GizmoHandle::None) {
            beginGizmoDrag(handle, ray);
            return true;
        }
    }
    _clickPending = true;
    _pressPos = event->pos();
    _pressModifiers = event->modifiers();
    return true;
}

bool Editor::handleMouseMove(QMouseEvent* event) {
    if (!_active) {
        return false;
    }
    if (_camera.mouseMove(event)) {
        return true;
    }
    if (_gizmo.isDragging()) {
        glm::vec3 delta = _gizmo.dragDelta(pickRay(event->pos()));
        if (snapActive()) {
            delta = snapVector(delta, _moveSnap);
        }
        _dragDelta = delta;
        if (_editTimer.elapsed() >= EDIT_INTERVAL_MS) {
            applyDrag(false);
        }
        return true;
    }
    if (_clickPending) {
        if ((event->pos() - _pressPos).manhattanLength() > CLICK_SLOP_PIXELS) {
            _clickPending = false;
        }
        return true;
    }
    if (_gizmo.isVisible() && event->buttons() == Qt::NoButton) {
        _gizmo.setHover(_gizmo.hitTest(pickRay(event->pos())));
    }
    return false;
}

bool Editor::handleMouseRelease(QMouseEvent* event) {
    if (!_active) {
        return false;
    }
    if (_camera.mouseRelease(event)) {
        return true;
    }
    if (event->button() != Qt::LeftButton) {
        return true;
    }
    if (_gizmo.isDragging()) {
        applyDrag(true);
        _gizmo.endDrag();
        return true;
    }
    if (_clickPending) {
        _clickPending = false;
        clickSelect(event->pos(), _pressModifiers);
    }
    return true;
}

bool Editor::handleMouseDoubleClick(QMouseEvent* event) {
    if (!_active) {
        return false;
    }
    if (event->button() == Qt::LeftButton && !event->modifiers().testFlag(Qt::AltModifier)) {
        QUuid hit = pickEntity(event->pos());
        if (!hit.isNull()) {
            _selection.set({ hit });
            frameSelection();
        }
    }
    return true;
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
    bool command = event->modifiers().testFlag(Qt::ControlModifier);
    bool shift = event->modifiers().testFlag(Qt::ShiftModifier);
    switch (event->key()) {
        case Qt::Key_Delete:
        case Qt::Key_Backspace:
            deleteSelection();
            return true;
        case Qt::Key_Escape:
            _selection.clear();
            return true;
        case Qt::Key_F:
            frameSelection();
            return true;
        case Qt::Key_Q:
            if (!command) { setTool(EditorToolBar::Tool::View); return true; }
            break;
        case Qt::Key_W:
            if (!command) { setTool(EditorToolBar::Tool::Move); return true; }
            break;
        case Qt::Key_E:
            if (!command) { setTool(EditorToolBar::Tool::Rotate); return true; }
            break;
        case Qt::Key_R:
            if (!command) { setTool(EditorToolBar::Tool::Scale); return true; }
            break;
        case Qt::Key_A:
            if (command) { selectAll(); return true; }
            break;
        case Qt::Key_D:
            if (command) { duplicateSelection(); return true; }
            break;
        case Qt::Key_C:
            if (command) { copySelection(); return true; }
            break;
        case Qt::Key_X:
            if (command) { cutSelection(); return true; }
            break;
        case Qt::Key_V:
            if (command) { paste(); return true; }
            break;
        case Qt::Key_Z:
            if (command) {
                if (shift) { redo(); } else { undo(); }
                return true;
            }
            break;
        default:
            break;
    }
    return false;
}

bool Editor::handleKeyRelease(QKeyEvent* event) {
    if (!_active) {
        return false;
    }
    return _camera.keyRelease(event);
}
