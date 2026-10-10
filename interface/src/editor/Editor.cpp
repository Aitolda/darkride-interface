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
#include <QtGui/QCursor>
#include <QtGui/QGuiApplication>
#include <QtGui/QKeyEvent>
#include <QtGui/QMouseEvent>
#include <QtGui/QWheelEvent>
#include <QtCore/QEvent>
#include <QtWidgets/QAbstractSpinBox>
#include <QtWidgets/QAction>
#include <QtWidgets/QApplication>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QPlainTextEdit>
#include <QtWidgets/QTextEdit>
#include <QtWidgets/QTreeView>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>

#include <OffscreenUi.h>

#include <EntityItem.h>
#include <EntityScriptingInterface.h>
#include <EntityTree.h>
#include <EntityTreeRenderer.h>
#include <gl/GLWidget.h>
#include <GLMHelpers.h>
#include <MainWindow.h>
#include <PathUtils.h>
#include <PickFilter.h>
#include <SimulationFlags.h>
#include <ViewFrustum.h>

#include "Application.h"
#include "EntityTreeModel.h"
#include "InterfaceLogging.h"
#include "Menu.h"
#include "PropertySpec.h"
#include "ui/HierarchyPanel.h"
#include "ui/InspectorPanel.h"

namespace {
    const float GIZMO_PIXELS = 110.0f;      // the gizmo's size on screen
    const int INSPECTOR_REFRESH_MS = 100;   // how often the Inspector re-reads a small selection
    const int INSPECTOR_REFRESH_SLOW_MS = 500;   // and a large one
    const int INSPECTOR_FAST_LIMIT = 16;
    const int CLICK_SLOP_PIXELS = 5;        // a press that moves less than this is a click
    const int EDIT_INTERVAL_MS = 33;        // while dragging, edits go out at most this often
    const int SCALE_EDIT_INTERVAL_MS = 100; // scale edits are heavier for the server
    const int PRUNE_EVERY_FRAMES = 30;      // how often the selection drops entities that are gone
    const float PASTE_FALLBACK_DISTANCE = 3.0f;   // in front of the camera, when the cursor points at nothing
    const float MIN_DIMENSION = 0.001f;
    const float BUILD_MODE_FOV = 90.0f;     // the client's units (its default is 120); less edge stretch on the gizmos

    // what a click or a paste can land on: the world's visible entities, picked against their meshes
    PickFilter worldPickFilter() {
        PickFilter::Flags flags;
        flags.set(PickFilter::DOMAIN_ENTITIES);
        flags.set(PickFilter::VISIBLE);
        flags.set(PickFilter::COLLIDABLE);
        flags.set(PickFilter::NONCOLLIDABLE);
        flags.set(PickFilter::PRECISE);
        return PickFilter(flags);
    }

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

    float snapValue(float v, float increment) {
        return std::round(v / increment) * increment;
    }

    EntityTreePointer entityTree() {
        auto renderer = qApp->getEntities();
        return renderer ? renderer->getTree() : nullptr;
    }

    // the shortcut modifier: Command, and also Option and Control, for hands trained on other keyboards
    // (a PC keyboard's Alt key is Option on macOS; Option never pairs with a letter in the editor otherwise)
    bool commandModifier(Qt::KeyboardModifiers modifiers) {
        return modifiers.testFlag(Qt::ControlModifier) || modifiers.testFlag(Qt::AltModifier)
            || modifiers.testFlag(Qt::MetaModifier);
    }

    GizmoMode gizmoModeFor(EditorToolBar::Tool tool) {
        switch (tool) {
            case EditorToolBar::Tool::Rotate: return GizmoMode::Rotate;
            case EditorToolBar::Tool::Scale: return GizmoMode::Scale;
            default: return GizmoMode::Move;
        }
    }
}

Editor::Editor() : _selection(this), _undo(this) {
    connect(&_selection, &EditorSelection::changed, this, &Editor::onSelectionChanged);
    _inspectorRefresh.setInterval(INSPECTOR_REFRESH_MS);
    connect(&_inspectorRefresh, &QTimer::timeout, this, [this] { refreshInspector(false); });
}

// --- build mode on and off ----------------------------------------------------------------------

void Editor::setReady() {
    _ready = true;
    // the client died in build mode last time: give the field of view back
    float stale = _fovBeforeBuild.get();
    if (stale > 0.0f) {
        qApp->setFieldOfView(stale);
        _fovBeforeBuild.set(0.0f);
    }
}

void Editor::setActive(bool active) {
    if (active == _active) {
        return;
    }
    if (active && !_ready) {
        // the menu's saved state, restored at startup: build mode always starts off
        Menu::getInstance()->setIsOptionChecked(MenuOption::BuildMode, false);
        return;
    }
    _active = active;
    if (active) {
        createPanels();
        showPanels(true);
        _savedFov = qApp->getFieldOfView();
        _fovBeforeBuild.set(_savedFov);
        qApp->setFieldOfView(BUILD_MODE_FOV);
        _camera.enter();
        _grid.setVisible(_gridVisible);
        _selection.setHighlightEnabled(true);
        if (_treeModel) {
            _treeModel->attach();
        }
        _axesValid = false;
        updateAxes();
        refreshInspector(true);
        _inspectorRefresh.start();
        focusViewport();
    } else {
        _inspectorRefresh.stop();
        if (_gizmo.isDragging()) {
            applyDrag(true);
            _gizmo.endDrag();
            _dragKind = DragKind::None;
        }
        if (auto window = qApp->getWindow()) {
            _layoutSetting.set(window->saveState());
        }
        showPanels(false);
        _camera.leave();
        if (_savedFov > 0.0f) {
            qApp->setFieldOfView(_savedFov);
        }
        _fovBeforeBuild.set(0.0f);
        _grid.setVisible(false);
        _gizmo.setVisible(false);
        _selection.setHighlightEnabled(false);
        _selection.clear();
        _marqueeActive = false;
        emit marqueeChanged();
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

void Editor::setLocalSpace(bool local) {
    if (_localSpace == local) {
        return;
    }
    _localSpace = local;
    if (_toolBar) {
        _toolBar->setLocalSpace(local);
    }
    updateGizmo();
}

void Editor::setPivotMode(bool pivot) {
    if (_pivotMode == pivot) {
        return;
    }
    _pivotMode = pivot;
    if (_toolBar) {
        _toolBar->setPivotMode(pivot);
    }
    updateGizmo();
}

void Editor::setMenuActionsEnabled(bool enabled) {
    auto menu = Menu::getInstance();
    for (const auto& name : { MenuOption::BuildUndo, MenuOption::BuildRedo, MenuOption::BuildDuplicate,
                              MenuOption::BuildFrame, MenuOption::BuildResetPanels }) {
        auto action = menu->getActionForOption(name);
        if (action) {
            action->setEnabled(enabled);
        }
        qCDebug(interfaceapp) << "editor menu action" << name << (action ? (enabled ? "enabled" : "disabled") : "NOT FOUND")
            << (action ? action->shortcut().toString() : QString());
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
    connect(_inspector, &InspectorPanel::editBegan, this, &Editor::onInspectorEditBegan);
    connect(_inspector, &InspectorPanel::liveEdit, this, &Editor::onInspectorLiveEdit);
    connect(_inspector, &InspectorPanel::committed, this, &Editor::onInspectorCommitted);

    _toolBar = new EditorToolBar(window);
    _toolBar->setGridChecked(_gridVisible);
    _toolBar->setTool(_tool);
    _toolBar->setLocalSpace(_localSpace);
    _toolBar->setPivotMode(_pivotMode);
    connect(_toolBar, &EditorToolBar::exitRequested, this, [this] { setActive(false); });
    connect(_toolBar, &EditorToolBar::gridToggled, this, &Editor::setGridVisible);
    connect(_toolBar, &EditorToolBar::toolChanged, this, [this](EditorToolBar::Tool tool) {
        _tool = tool;
        updateGizmo();
    });
    connect(_toolBar, &EditorToolBar::localSpaceToggled, this, &Editor::setLocalSpace);
    connect(_toolBar, &EditorToolBar::pivotToggled, this, &Editor::setPivotMode);
    connect(_toolBar, &EditorToolBar::snapToggled, this, [this](bool on) { _snapAlways = on; });

    window->addDockWidget(Qt::LeftDockWidgetArea, _hierarchy);
    window->addDockWidget(Qt::RightDockWidgetArea, _inspector);
    window->addToolBar(Qt::TopToolBarArea, _toolBar);
    window->resizeDocks({ _hierarchy, _inspector }, { 260, 390 }, Qt::Horizontal);
    qApp->installEventFilter(this);

    QByteArray layout = _layoutSetting.get();
    if (!layout.isEmpty()) {
        window->restoreState(layout);
    }
    // a saved layout from before the panels were fixed can still float them
    _hierarchy->setFloating(false);
    _inspector->setFloating(false);
}

void Editor::focusViewport() {
    if (auto viewport = qApp->getPrimaryWidget()) {
        if (!viewport->hasFocus()) {
            viewport->setFocus(Qt::MouseFocusReason);
        }
    }
}

void Editor::takeFocusUnlessTyping() {
    QWidget* focus = QApplication::focusWidget();
    if (focus && (qobject_cast<QLineEdit*>(focus) || qobject_cast<QAbstractSpinBox*>(focus) ||
                  qobject_cast<QTextEdit*>(focus) || qobject_cast<QPlainTextEdit*>(focus))) {
        return;
    }
    focusViewport();
}

void Editor::showPanels(bool show) {
    if (!_panelsCreated) {
        return;
    }
    _hierarchy->setVisible(show);
    _inspector->setVisible(show);
    _toolBar->setVisible(show);
}

void Editor::resetLayout() {
    auto window = qApp->getWindow();
    if (!_panelsCreated || !window) {
        return;
    }
    _hierarchy->setFloating(false);
    _inspector->setFloating(false);
    window->addDockWidget(Qt::LeftDockWidgetArea, _hierarchy);
    window->addDockWidget(Qt::RightDockWidgetArea, _inspector);
    window->addToolBar(Qt::TopToolBarArea, _toolBar);
    showPanels(_active);
    _layoutSetting.set(QByteArray());
}

// --- every frame --------------------------------------------------------------------------------

void Editor::update(float deltaTime) {
    if (!_active) {
        return;
    }
    // a teleport or a domain change puts the camera back on the avatar; take it again from there
    if (qApp->getCamera().getMode() != CAMERA_MODE_INDEPENDENT) {
        _camera.enter();
        _axesValid = false;
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

glm::vec3 Editor::viewDirection() const {
    return _camera.orientation() * Vectors::FRONT;
}

bool Editor::primaryTransform(glm::vec3& position, glm::quat& rotation) const {
    auto tree = entityTree();
    QUuid id = _selection.primary();
    if (!tree || id.isNull()) {
        return false;
    }
    bool found = false;
    tree->withReadLock([&] {
        auto entity = tree->findEntityByEntityItemID(id);
        if (entity) {
            position = entity->getWorldPosition();
            rotation = entity->getWorldOrientation();
            found = true;
        }
    });
    return found;
}

glm::vec3 Editor::gizmoPosition() const {
    glm::vec3 position;
    glm::quat rotation;
    if (_pivotMode && primaryTransform(position, rotation)) {
        return position;
    }
    return _selection.center();
}

glm::quat Editor::gizmoRotation() const {
    // scale is always in the object's own axes; move and rotate follow the space toggle
    if (_tool == EditorToolBar::Tool::Scale || _localSpace) {
        glm::vec3 position;
        glm::quat rotation;
        if (primaryTransform(position, rotation)) {
            return rotation;
        }
    }
    return glm::quat();
}

void Editor::updateGizmo() {
    bool show = _active && _tool != EditorToolBar::Tool::View && !_selection.isEmpty();
    if (show) {
        _gizmo.setMode(gizmoModeFor(_tool));
        glm::vec3 position;
        glm::quat rotation;
        if (_gizmo.isDragging()) {
            position = _dragPivot + (_dragKind == DragKind::Move ? _dragDelta : glm::vec3(0.0f));
            rotation = _dragGizmoRotation;
        } else {
            position = gizmoPosition();
            rotation = gizmoRotation();
        }
        // sized by depth along the view axis, not straight-line distance, so the handles keep the
        // same projected size at the edge of the frame as in the middle (Unity does the same)
        float depth = std::max(0.01f, glm::dot(position - _camera.position(), viewDirection()));
        float scale = depth * 2.0f * tanHalfFov() * (GIZMO_PIXELS / viewportHeight());
        _gizmo.setTransform(position, rotation, scale, viewDirection());
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
    refreshInspector(true);
    updateGizmo();
}

// --- the Inspector ------------------------------------------------------------------------------

bool Editor::selectionSnapshots(QVector<EntityItemProperties>& snapshots, EntityTypes::EntityType& type, bool& multiple) const {
    auto tree = entityTree();
    if (!tree) {
        return false;
    }
    snapshots.clear();
    type = EntityTypes::Unknown;
    multiple = false;
    tree->withReadLock([&] {
        for (const auto& id : _selection.ids()) {
            auto entity = tree->findEntityByEntityItemID(id);
            if (!entity) {
                continue;
            }
            EntityTypes::EntityType entityType = entity->getType();
            if (entityType == EntityTypes::Box || entityType == EntityTypes::Sphere) {
                entityType = EntityTypes::Shape;
            }
            if (snapshots.isEmpty()) {
                type = entityType;
            } else if (entityType != type) {
                multiple = true;
            }
            snapshots << entity->getProperties();
        }
    });
    return !snapshots.isEmpty();
}

void Editor::refreshInspector(bool selectionChanged) {
    if (!_inspector || !_active) {
        return;
    }
    QVector<EntityItemProperties> snapshots;
    EntityTypes::EntityType type;
    bool multiple;
    if (!selectionSnapshots(snapshots, type, multiple)) {
        _inspector->setSummary("Nothing selected");
        return;
    }
    _inspectorRefresh.setInterval(snapshots.size() > INSPECTOR_FAST_LIMIT ? INSPECTOR_REFRESH_SLOW_MS : INSPECTOR_REFRESH_MS);
    if (selectionChanged) {
        QString title;
        if (snapshots.size() == 1) {
            QString name = snapshots.first().getName();
            QString typeName = EntityTypes::getEntityTypeName(type);
            title = name.isEmpty() ? typeName : name + "  ·  " + typeName;
        } else {
            title = QString("%1 entities selected").arg(snapshots.size());
        }
        _inspector->showEntities(snapshots, type, multiple, title);
    } else if (_inspectorEditKey.isEmpty()) {
        _inspector->refreshValues(snapshots);
    }
}

void Editor::onInspectorEditBegan(const QString& key) {
    // a scrub starts: what the entities hold now is the undo point
    _inspectorEditKey = key;
    _inspectorEditIds.clear();
    _inspectorBefore.clear();
    for (const auto& id : _selection.ids()) {
        EntityItemProperties snapshot;
        if (!EditorUndo::snapshot(id, snapshot) || EditorUndo::isLocked(id)) {
            continue;
        }
        EntityItemProperties before;
        PropertySpecs::set(key, before, PropertySpecs::get(key, snapshot));
        _inspectorEditIds << id;
        _inspectorBefore << before;
    }
}

void Editor::onInspectorLiveEdit(const QString& key, const QVariant& value) {
    if (key != _inspectorEditKey) {
        return;
    }
    auto entities = DependencyManager::get<EntityScriptingInterface>();
    for (const auto& id : _inspectorEditIds) {
        EntityItemProperties properties;
        PropertySpecs::set(key, properties, value);
        entities->editEntity(id, properties);
    }
}

void Editor::onInspectorCommitted(const QString& key, const QVariant& value) {
    QList<QUuid> ids;
    QVector<EntityItemProperties> before;
    bool alreadyApplied = key == _inspectorEditKey && !_inspectorEditIds.isEmpty();
    if (alreadyApplied) {
        // the end of a scrub: the values went out live, the undo point was taken at its start
        ids = _inspectorEditIds;
        before = _inspectorBefore;
        onInspectorLiveEdit(key, value);
    } else {
        for (const auto& id : _selection.ids()) {
            EntityItemProperties snapshot;
            if (!EditorUndo::snapshot(id, snapshot) || EditorUndo::isLocked(id)) {
                continue;
            }
            EntityItemProperties properties;
            PropertySpecs::set(key, properties, PropertySpecs::get(key, snapshot));
            ids << id;
            before << properties;
        }
    }
    _inspectorEditKey.clear();
    _inspectorEditIds.clear();
    _inspectorBefore.clear();
    if (ids.isEmpty()) {
        return;
    }
    QVector<EntityItemProperties> after;
    for (int i = 0; i < ids.size(); ++i) {
        EntityItemProperties properties;
        PropertySpecs::set(key, properties, value);
        after << properties;
    }
    auto spec = PropertySpecs::find(key);
    _undo.pushPropertyEdit(spec ? spec->label : key, ids, before, after, alreadyApplied);
    refreshInspector(false);
}

PickRay Editor::pickRay(const QPoint& viewportPos) const {
    return qApp->computePickRay((float)viewportPos.x(), (float)viewportPos.y());
}

QUuid Editor::pickEntity(const QPoint& viewportPos) const {
    auto entities = DependencyManager::get<EntityScriptingInterface>();
    auto result = entities->evalRayIntersectionVector(pickRay(viewportPos), worldPickFilter(),
        QVector<EntityItemID>(), QVector<EntityItemID>());
    return result.intersects ? result.entityID : QUuid();
}

bool Editor::cursorWorldPoint(glm::vec3& point) const {
    auto viewport = qApp->getPrimaryWidget();
    if (!viewport) {
        return false;
    }
    QPoint local = viewport->mapFromGlobal(QCursor::pos());
    if (!viewport->rect().contains(local)) {
        return false;
    }
    PickRay ray = pickRay(local);
    auto entities = DependencyManager::get<EntityScriptingInterface>();
    auto result = entities->evalRayIntersectionVector(ray, worldPickFilter(), QVector<EntityItemID>(), QVector<EntityItemID>());
    if (result.intersects) {
        point = result.intersection;
        return true;
    }
    // nothing under the cursor: the grid plane
    if (std::abs(ray.direction.y) > 1.0e-4f) {
        float t = (_grid.height() - ray.origin.y) / ray.direction.y;
        if (t > 0.0f) {
            point = ray.origin + ray.direction * t;
            return true;
        }
    }
    return false;
}

glm::vec3 Editor::selectionAnchor() const {
    // the middle of the selection's footprint: what a paste puts on the point under the cursor
    AABox box;
    if (!_selection.bounds(box)) {
        return glm::vec3(0.0f);
    }
    glm::vec3 center = box.calcCenter();
    return glm::vec3(center.x, box.getMinimumPoint().y, center.z);
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

void Editor::updateMarquee(const QPoint& viewportPos) {
    _marqueeRect = QRectF(QPointF(_pressPos), QPointF(viewportPos)).normalized();
    emit marqueeChanged();
}

QList<QUuid> Editor::entitiesInRect(const QRectF& rect) const {
    QList<QUuid> ids;
    auto tree = entityTree();
    auto viewport = qApp->getPrimaryWidget();
    if (!tree || !viewport || !_treeModel) {
        return ids;
    }
    ViewFrustum frustum;
    qApp->copyViewFrustum(frustum);
    glm::mat4 viewProjection = frustum.getProjection() * frustum.getView();
    float width = (float)std::max(1, viewport->width());
    float height = (float)std::max(1, viewport->height());
    QList<QUuid> all = _treeModel->allIds();
    tree->withReadLock([&] {
        for (const auto& id : all) {
            auto entity = tree->findEntityByEntityItemID(id);
            if (!entity || !entity->getVisible()) {
                continue;
            }
            glm::vec4 clip = viewProjection * glm::vec4(entity->getWorldPosition(), 1.0f);
            if (clip.w <= 0.0f) {
                continue;   // behind the camera
            }
            float sx = (clip.x / clip.w + 1.0f) * 0.5f * width;
            float sy = (1.0f - clip.y / clip.w) * 0.5f * height;
            if (rect.contains(QPointF(sx, sy))) {
                ids << id;
            }
        }
    });
    return ids;
}

void Editor::finishMarquee(Qt::KeyboardModifiers modifiers) {
    _marqueeActive = false;
    emit marqueeChanged();
    QList<QUuid> inside = entitiesInRect(_marqueeRect);
    bool shift = modifiers.testFlag(Qt::ShiftModifier);
    bool command = modifiers.testFlag(Qt::ControlModifier);
    if (command) {
        for (const auto& id : inside) {
            _selection.toggle(id);
        }
    } else if (shift) {
        QList<QUuid> ids = _selection.ids();
        ids << inside;
        _selection.set(ids);
    } else {
        _selection.set(inside);
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
    return _snapAlways || commandModifier(QGuiApplication::keyboardModifiers());
}

void Editor::beginGizmoDrag(GizmoHandle handle, const PickRay& ray) {
    _dragIds.clear();
    _dragPositions.clear();
    _dragRotations.clear();
    _dragDimensions.clear();
    _dragBefore.clear();
    auto tree = entityTree();
    if (!tree) {
        return;
    }
    DragKind kind = EditorGizmo::isRing(handle) ? DragKind::Rotate
                  : EditorGizmo::isScale(handle) ? DragKind::Scale : DragKind::Move;
    tree->withReadLock([&] {
        for (const auto& id : _selection.topLevel()) {
            auto entity = tree->findEntityByEntityItemID(id);
            if (!entity || entity->getLocked()) {
                continue;
            }
            glm::vec3 position = entity->getWorldPosition();
            glm::quat rotation = entity->getWorldOrientation();
            glm::vec3 dimensions = entity->getScaledDimensions();
            EntityItemProperties before;
            before.setPosition(position);
            if (kind == DragKind::Rotate) {
                before.setRotation(rotation);
            } else if (kind == DragKind::Scale) {
                before.setDimensions(dimensions);
            }
            _dragIds << id;
            _dragPositions << position;
            _dragRotations << rotation;
            _dragDimensions << dimensions;
            _dragBefore << before;
        }
    });
    if (_dragIds.isEmpty()) {
        return;
    }
    _dragKind = kind;
    _dragPivot = _gizmo.position();
    _dragGizmoRotation = _gizmo.rotation();
    _dragDelta = glm::vec3(0.0f);
    _dragRotationDelta = glm::quat();
    _dragScale = glm::vec3(1.0f);
    _lastDragPos = _pressPos;
    _editTimer.start();
    _gizmo.beginDrag(handle, ray);
    if (!_gizmo.isDragging()) {
        _dragKind = DragKind::None;
    }
}

bool Editor::vertexSnapTarget(const QPoint& viewportPos, glm::vec3& target) const {
    auto entities = DependencyManager::get<EntityScriptingInterface>();
    QVector<EntityItemID> discard;
    for (const auto& id : _dragIds) {
        discard << EntityItemID(id);
    }
    auto result = entities->evalRayIntersectionVector(pickRay(viewportPos), worldPickFilter(), QVector<EntityItemID>(), discard);
    if (!result.intersects) {
        return false;
    }
    target = result.intersection;
    // a model pick reports the triangle it hit: take its nearest corner
    QVariantMap triangle = result.extraInfo.value("subMeshTriangleWorld").toMap();
    float best = FLT_MAX;
    for (const auto& key : { "v0", "v1", "v2" }) {
        bool valid = false;
        glm::vec3 vertex = vec3FromVariant(triangle.value(key), valid);
        if (valid) {
            float distance = glm::length(vertex - result.intersection);
            if (distance < best) {
                best = distance;
                target = vertex;
            }
        }
    }
    return true;
}

void Editor::moveGizmoDrag(const QPoint& viewportPos) {
    PickRay ray = pickRay(viewportPos);
    QPoint mouseDelta = viewportPos - _lastDragPos;
    _lastDragPos = viewportPos;
    switch (_dragKind) {
        case DragKind::Move: {
            glm::vec3 target;
            if (_vertexSnap && vertexSnapTarget(viewportPos, target)) {
                _dragDelta = target - _dragPivot;   // the pivot lands on the vertex
            } else {
                glm::vec3 delta = _gizmo.dragDelta(ray);
                _dragDelta = snapActive() ? snapVector(delta, _moveSnap) : delta;
            }
            break;
        }
        case DragKind::Rotate: {
            if (_gizmo.dragHandle() == GizmoHandle::Sphere) {
                _dragRotationDelta = _gizmo.dragFree((float)mouseDelta.x(), (float)mouseDelta.y());
                break;
            }
            float angle = _gizmo.dragAngle(ray);
            if (snapActive()) {
                angle = glm::radians(snapValue(glm::degrees(angle), _rotateSnap));
            }
            _dragRotationDelta = glm::angleAxis(angle, _gizmo.dragAxis());
            break;
        }
        case DragKind::Scale: {
            glm::vec3 factor = _gizmo.dragScale(ray);
            if (snapActive()) {
                for (int i = 0; i < 3; ++i) {
                    factor[i] = std::max(_scaleSnap, snapValue(factor[i], _scaleSnap));
                }
            }
            _dragScale = factor;
            _gizmo.setStretch(factor);
            break;
        }
        default:
            break;
    }
    // scale edits also reach the entity server, which rebuilds its own shapes: send those less often
    int interval = _dragKind == DragKind::Scale ? SCALE_EDIT_INTERVAL_MS : EDIT_INTERVAL_MS;
    if (_editTimer.elapsed() >= interval) {
        applyDrag(false);
    }
}

void Editor::applyDrag(bool final) {
    _editTimer.restart();
    auto entities = DependencyManager::get<EntityScriptingInterface>();
    auto tree = entityTree();
    QVector<EntityItemProperties> after;
    QString text;
    for (int i = 0; i < _dragIds.size(); ++i) {
        EntityItemProperties properties;
        switch (_dragKind) {
            case DragKind::Move:
                text = "Move";
                properties.setPosition(_dragPositions[i] + _dragDelta);
                break;
            case DragKind::Rotate:
                text = "Rotate";
                properties.setRotation(_dragRotationDelta * _dragRotations[i]);
                // around the common centre, or each about its own pivot
                properties.setPosition(_pivotMode ? _dragPositions[i]
                    : _dragPivot + _dragRotationDelta * (_dragPositions[i] - _dragPivot));
                break;
            case DragKind::Scale: {
                text = "Scale";
                properties.setDimensions(glm::max(_dragDimensions[i] * _dragScale, glm::vec3(MIN_DIMENSION)));
                if (_pivotMode) {
                    properties.setPosition(_dragPositions[i]);
                } else {
                    // the group grows about the pivot, along the gizmo's axes
                    glm::vec3 local = glm::inverse(_dragGizmoRotation) * (_dragPositions[i] - _dragPivot);
                    properties.setPosition(_dragPivot + _dragGizmoRotation * (local * _dragScale));
                }
                break;
            }
            default:
                return;
        }
        entities->editEntity(_dragIds[i], properties);
        if (_dragKind == DragKind::Scale && !final && tree) {
            // a dimension change makes physics rebuild the collision shape from the mesh, which is
            // slow and stalls the main thread; while the drag lasts the old shape is good enough
            tree->withWriteLock([&] {
                if (auto entity = tree->findEntityByEntityItemID(_dragIds[i])) {
                    entity->clearDirtyFlags(Simulation::DIRTY_SHAPE | Simulation::DIRTY_MASS);
                }
            });
        }
        after << properties;
    }
    bool changed = _dragKind == DragKind::Move ? _dragDelta != glm::vec3(0.0f)
                 : _dragKind == DragKind::Rotate ? _dragRotationDelta != glm::quat()
                 : _dragScale != glm::vec3(1.0f);
    if (final && changed) {
        _undo.pushPropertyEdit(text, _dragIds, _dragBefore, after, true);
    }
}

// --- edits --------------------------------------------------------------------------------------

void Editor::undo() {
    auto stack = _undo.stack();
    qCDebug(interfaceapp) << "editor undo:" << stack->undoText() << "index" << stack->index() << "of" << stack->count()
        << "active" << _active << "dragging" << _gizmo.isDragging();
    if (_active && !_gizmo.isDragging()) {
        stack->undo();
    }
}

void Editor::redo() {
    auto stack = _undo.stack();
    qCDebug(interfaceapp) << "editor redo:" << stack->redoText() << "index" << stack->index() << "of" << stack->count();
    if (_active && !_gizmo.isDragging()) {
        stack->redo();
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
    // in place, as Unity does; the copy is selected and the gizmo is on it
    EditorClipboard copies;
    copies.copy(_selection.topLevel(), selectionAnchor());
    QList<QUuid> created = copies.paste(_undo, "Duplicate");
    if (!created.isEmpty()) {
        _selection.set(created);
    }
}

void Editor::copySelection() {
    if (_active && !_selection.isEmpty()) {
        _clipboard.copy(_selection.topLevel(), selectionAnchor());
    }
}

void Editor::cutSelection() {
    if (!_active || _selection.isEmpty() || _gizmo.isDragging()) {
        return;
    }
    _clipboard.copy(_selection.topLevel(), selectionAnchor());
    deleteSelection();
}

void Editor::paste() {
    if (!_active || _clipboard.isEmpty() || _gizmo.isDragging()) {
        return;
    }
    // the copies land where the cursor points, or a few metres in front of the camera
    glm::vec3 target;
    if (!cursorWorldPoint(target)) {
        target = _camera.position() + viewDirection() * PASTE_FALLBACK_DISTANCE;
    }
    QList<QUuid> created = _clipboard.paste(_undo, "Paste", target - _clipboard.anchor());
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
    _pressPos = event->pos();
    _pressModifiers = event->modifiers();
    if (_gizmo.isVisible()) {
        GizmoHandle handle = _gizmo.hitTest(ray);
        if (handle != GizmoHandle::None) {
            beginGizmoDrag(handle, ray);
            return true;
        }
    }
    _clickPending = true;
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
        moveGizmoDrag(event->pos());
        return true;
    }
    if (_marqueeActive) {
        updateMarquee(event->pos());
        return true;
    }
    if (_clickPending) {
        if ((event->pos() - _pressPos).manhattanLength() > CLICK_SLOP_PIXELS) {
            // a drag on empty space is a selection rectangle
            _clickPending = false;
            _marqueeActive = true;
            updateMarquee(event->pos());
        }
        return true;
    }
    if (event->buttons() == Qt::NoButton) {
        // the keys follow the mouse into the viewport, as in Unity, unless something is being typed
        takeFocusUnlessTyping();
        if (_gizmo.isVisible()) {
            _gizmo.setHover(_gizmo.hitTest(pickRay(event->pos())));
        }
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
        _dragKind = DragKind::None;
        updateGizmo();
        return true;
    }
    if (_marqueeActive) {
        updateMarquee(event->pos());
        finishMarquee(_pressModifiers);
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
    bool command = commandModifier(event->modifiers());
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
        case Qt::Key_X:
            if (command) { cutSelection(); } else { setLocalSpace(!_localSpace); }
            return true;
        case Qt::Key_Z:
            if (command) {
                if (shift) { redo(); } else { undo(); }
            } else {
                setPivotMode(!_pivotMode);
            }
            return true;
        case Qt::Key_A:
            if (command) { selectAll(); return true; }
            break;
        case Qt::Key_D:
            if (command) { duplicateSelection(); return true; }
            break;
        case Qt::Key_C:
            if (command) { copySelection(); return true; }
            break;
        case Qt::Key_V:
            if (command) {
                paste();
            } else if (!event->isAutoRepeat()) {
                _vertexSnap = true;
            }
            return true;
        default:
            break;
    }
    return false;
}

bool Editor::handleKeyRelease(QKeyEvent* event) {
    if (!_active) {
        return false;
    }
    if (event->key() == Qt::Key_V && !event->isAutoRepeat()) {
        _vertexSnap = false;
        return true;
    }
    return _camera.keyRelease(event);
}

bool Editor::eventFilter(QObject* watched, QEvent* event) {
    if (!_active || event->type() != QEvent::KeyPress || !_panelsCreated) {
        return false;
    }
    auto widget = qobject_cast<QWidget*>(watched);
    if (!widget) {
        return false;
    }
    auto keyEvent = static_cast<QKeyEvent*>(event);
    bool command = commandModifier(keyEvent->modifiers());
    bool shift = keyEvent->modifiers().testFlag(Qt::ShiftModifier);
    bool typing = qobject_cast<QLineEdit*>(widget) || qobject_cast<QPlainTextEdit*>(widget) || qobject_cast<QTextEdit*>(widget)
        || qobject_cast<QAbstractSpinBox*>(widget);
    bool inTree = qobject_cast<QTreeView*>(widget) || (widget->parentWidget() && qobject_cast<QTreeView*>(widget->parentWidget()));
    bool inPanels = _hierarchy->isAncestorOf(widget) || _inspector->isAncestorOf(widget) || _toolBar->isAncestorOf(widget);

    // undo and redo are taken here, ahead of everything, from any widget of the window: the menu
    // shortcut and the viewport's key handler were never reached on macOS
    if (command && keyEvent->key() == Qt::Key_Z && !typing) {
        // unless a QML text field (chat, a dialog) is being typed in
        bool qmlTyping = false;
        if (auto offscreenUi = DependencyManager::get<OffscreenUi>()) {
            if (auto window = offscreenUi->getWindow()) {
                if (auto item = window->activeFocusItem()) {
                    qmlTyping = item->inherits("QQuickTextInput") || item->inherits("QQuickTextEdit")
                        || item->inherits("QQuickWebEngineView");
                }
            }
        }
        if (!qmlTyping) {
            if (shift) { redo(); } else { undo(); }
            return true;
        }
        return false;
    }

    if (!inPanels) {
        return false;
    }
    // text fields keep their own keys while typing; the JSON box keeps everything
    if (qobject_cast<QPlainTextEdit*>(widget)) {
        return false;
    }
    switch (keyEvent->key()) {
        case Qt::Key_D:
            if (command && !typing) { duplicateSelection(); return true; }
            break;
        case Qt::Key_Delete:
        case Qt::Key_Backspace:
            if (inTree) { deleteSelection(); return true; }
            break;
        case Qt::Key_F:
            if (inTree && !command) { frameSelection(); return true; }
            break;
        default:
            break;
    }
    return false;
}
