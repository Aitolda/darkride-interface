//
//  Editor.h
//  interface/src/editor
//
//  Build mode: the native scene editor. Owns the scene-view camera, the grid, the selection,
//  the undo stack, the gizmo and the panels docked around the viewport; Application hands it
//  the viewport's input while it is active.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#ifndef hifi_Editor_h
#define hifi_Editor_h

#include <QtCore/QByteArray>
#include <QtCore/QElapsedTimer>
#include <QtCore/QList>
#include <QtCore/QObject>
#include <QtCore/QPoint>
#include <QtCore/QString>
#include <QtCore/QUuid>
#include <QtCore/QVector>
#include <QtGui/QVector3D>

#include <glm/gtc/quaternion.hpp>

#include <DependencyManager.h>
#include <EntityItemProperties.h>
#include <RegisteredMetaTypes.h>
#include <SettingHandle.h>

#include "EditorCamera.h"
#include "EditorClipboard.h"
#include "EditorGizmo.h"
#include "EditorGrid.h"
#include "EditorSelection.h"
#include "EditorUndo.h"
#include "ui/EditorToolBar.h"

class QKeyEvent;
class QMouseEvent;
class QWheelEvent;
class EntityTreeModel;
class HierarchyPanel;
class InspectorPanel;

class Editor : public QObject, public Dependency {
    Q_OBJECT
    SINGLETON_DEPENDENCY

    Q_PROPERTY(bool active READ isActive WRITE setActive NOTIFY activeChanged)
    Q_PROPERTY(bool gridVisible READ isGridVisible WRITE setGridVisible NOTIFY gridVisibleChanged)
    Q_PROPERTY(float flySpeed READ flySpeed NOTIFY cameraChanged)
    // the world axes in camera space (x right, y up, z towards the viewer), for the scene gizmo
    Q_PROPERTY(QVector3D axisX READ axisX NOTIFY cameraChanged)
    Q_PROPERTY(QVector3D axisY READ axisY NOTIFY cameraChanged)
    Q_PROPERTY(QVector3D axisZ READ axisZ NOTIFY cameraChanged)

public:
    Editor();

    bool isActive() const { return _active; }
    bool isGridVisible() const { return _gridVisible; }
    float flySpeed() const { return _camera.flySpeed(); }
    QVector3D axisX() const { return _axes[0]; }
    QVector3D axisY() const { return _axes[1]; }
    QVector3D axisZ() const { return _axes[2]; }

    EditorCamera& camera() { return _camera; }
    EditorGrid& grid() { return _grid; }
    EditorSelection& selection() { return _selection; }
    EditorUndo& undoStack() { return _undo; }

    // called by Application every frame while active, before the camera is read
    void update(float deltaTime);

    // viewport input; each returns true when the editor consumed the event
    bool handleMousePress(QMouseEvent* event);
    bool handleMouseMove(QMouseEvent* event);
    bool handleMouseRelease(QMouseEvent* event);
    bool handleMouseDoubleClick(QMouseEvent* event);
    bool handleWheel(QWheelEvent* event);
    bool handleKeyPress(QKeyEvent* event);
    bool handleKeyRelease(QKeyEvent* event);

    Q_INVOKABLE void viewFromAxis(int axis, int sign);

public slots:
    void setActive(bool active);
    void toggleActive() { setActive(!_active); }
    void setGridVisible(bool visible);
    void setTool(EditorToolBar::Tool tool);

    void undo();
    void redo();
    void frameSelection();
    void deleteSelection();
    void duplicateSelection();
    void copySelection();
    void cutSelection();
    void paste();
    void selectAll();

signals:
    void activeChanged(bool active);
    void gridVisibleChanged(bool visible);
    void cameraChanged();

private:
    void createPanels();
    void showPanels(bool show);
    void focusViewport();
    void setMenuActionsEnabled(bool enabled);
    void updateAxes();
    void updateGizmo();
    void updateInspector();
    void onSelectionChanged();
    void rename(const QUuid& id, const QString& name);

    PickRay pickRay(const QPoint& viewportPos) const;
    QUuid pickEntity(const QPoint& viewportPos) const;
    void clickSelect(const QPoint& viewportPos, Qt::KeyboardModifiers modifiers);
    bool snapActive() const;
    void beginGizmoDrag(GizmoHandle handle, const PickRay& ray);
    void applyDrag(bool final);

    bool _active { false };
    bool _gridVisible { true };
    EditorCamera _camera;
    EditorGrid _grid;
    EditorSelection _selection;
    EditorUndo _undo;
    EditorClipboard _clipboard;
    EditorGizmo _gizmo;
    EditorToolBar::Tool _tool { EditorToolBar::Tool::Move };
    bool _snapAlways { false };
    float _moveSnap { 0.25f };

    QVector3D _axes[3];
    glm::quat _axesOrientation;
    bool _axesValid { false };
    int _frameCounter { 0 };

    // a click in the viewport
    bool _clickPending { false };
    QPoint _pressPos;
    Qt::KeyboardModifiers _pressModifiers;

    // a gizmo drag: the top-level selected entities and where they started
    QList<QUuid> _dragIds;
    QVector<glm::vec3> _dragStart;
    QVector<EntityItemProperties> _dragBefore;
    glm::vec3 _dragPivot;
    glm::vec3 _dragDelta;
    QElapsedTimer _editTimer;

    bool _panelsCreated { false };
    bool _syncingSelection { false };
    HierarchyPanel* _hierarchy { nullptr };
    InspectorPanel* _inspector { nullptr };
    EditorToolBar* _toolBar { nullptr };
    EntityTreeModel* _treeModel { nullptr };
    Setting::Handle<QByteArray> _layoutSetting { "editor/layout", QByteArray() };
};

#endif // hifi_Editor_h
