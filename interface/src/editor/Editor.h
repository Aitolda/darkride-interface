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
#include <QtCore/QHash>
#include <QtCore/QList>
#include <QtCore/QPointer>
#include <QtCore/QObject>
#include <QtCore/QPoint>
#include <QtCore/QRectF>
#include <QtCore/QString>
#include <QtCore/QTimer>
#include <QtCore/QUuid>
#include <QtCore/QVariant>
#include <QtCore/QVector>
#include <QtGui/QVector3D>

#include <glm/gtc/quaternion.hpp>

#include <AABox.h>
#include <DependencyManager.h>
#include <EntityItemProperties.h>
#include <RegisteredMetaTypes.h>
#include <SettingHandle.h>

#include "EditorBounds.h"
#include "EditorCamera.h"
#include "EditorClipboard.h"
#include "EditorGizmo.h"
#include "EditorGrid.h"
#include "EditorSelection.h"
#include "EditorUndo.h"
#include "ui/EditorToolBar.h"

class QAction;
class QDropEvent;
class QKeyEvent;
class QMenu;
class QMouseEvent;
class QUrl;
class QWheelEvent;
class EditorWorld;
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
    // the selection rectangle being dragged in the viewport, in viewport pixels
    Q_PROPERTY(bool marqueeActive READ isMarqueeActive NOTIFY marqueeChanged)
    Q_PROPERTY(QRectF marquee READ marquee NOTIFY marqueeChanged)

public:
    Editor();

    bool isActive() const { return _active; }
    bool isGridVisible() const { return _gridVisible; }
    float flySpeed() const { return _camera.flySpeed(); }
    QVector3D axisX() const { return _axes[0]; }
    QVector3D axisY() const { return _axes[1]; }
    QVector3D axisZ() const { return _axes[2]; }
    bool isMarqueeActive() const { return _marqueeActive; }
    QRectF marquee() const { return _marqueeRect; }

    EditorCamera& camera() { return _camera; }
    EditorGrid& grid() { return _grid; }
    EditorSelection& selection() { return _selection; }
    EditorUndo& undoStack() { return _undo; }

    // Application calls this once the window, the scene and the desktop UI exist. Until then build
    // mode cannot be entered: the menu restores its saved state at startup by triggering the item.
    void setReady();

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
    // the editor's keys when the focus is in one of its panels rather than the viewport
    bool eventFilter(QObject* watched, QEvent* event) override;

    // a Create menu with every entity kind, for the toolbar and context menus; with a viewport
    // position, new entities go where that position points instead of the middle of the view
    QMenu* buildCreateMenu(QWidget* parent, const QPoint* viewportPos = nullptr);
    // a file or URL dropped on the viewport: models and images are placed where it landed
    bool handleDrop(QDropEvent* event);
    // a Build menu item that only works while build mode is on
    void addMenuAction(QAction* action);

    Q_INVOKABLE void viewFromAxis(int axis, int sign);

public slots:
    void setActive(bool active);
    void toggleActive() { setActive(!_active); }
    void setGridVisible(bool visible);
    void setTool(EditorToolBar::Tool tool);
    void setLocalSpace(bool local);
    void setPivotMode(bool pivot);

    void undo();
    void redo();
    // docks the panels back where they started, for a panel that was torn off and lost
    void resetLayout();
    void frameSelection();
    void goToSelection();           // Shift+F: frame it and take the avatar there
    void bringSelectionHere();      // Option+Shift+F: move it to the avatar
    void create(const QString& key);
    void createModel();             // asks for a URL
    void createFromMenu();          // the sender's data() is the catalogue key
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
    void marqueeChanged();

private:
    enum class DragKind { None, Move, Rotate, Scale };

    void createPanels();
    void showPanels(bool show);
    void focusViewport();
    void takeFocusUnlessTyping();
    void setMenuActionsEnabled(bool enabled);
    void updateAxes();
    void updateGizmo();
    void updateRenderMethod();
    void onSelectionChanged();
    void rename(const QUuid& id, const QString& name);

    // the Inspector: snapshots of the selection, and edits coming back from its fields
    void refreshInspector(bool selectionChanged);
    bool selectionSnapshots(QVector<EntityItemProperties>& snapshots, EntityTypes::EntityType& type, bool& multiple) const;
    void onInspectorEditBegan(const QString& key);
    void onInspectorLiveEdit(const QString& key, const QVariant& value);
    void onInspectorCommitted(const QString& key, const QVariant& value);
    void onInspectorEditCancelled(const QString& key);
    // the edit an Inspector value amounts to for one entity: Scale is turned into dimensions
    EntityItemProperties inspectorEdit(const QString& key, const QVariant& value, const glm::vec3& naturalSize) const;

    PickRay pickRay(const QPoint& viewportPos) const;
    QUuid pickEntity(const QPoint& viewportPos) const;
    // where the cursor points in the world: a surface, or the grid plane; false when off the viewport
    bool cursorWorldPoint(glm::vec3& point) const;
    glm::vec3 selectionAnchor() const;
    // the last selected entity's world transform; false when it is not in the tree
    bool primaryTransform(glm::vec3& position, glm::quat& rotation) const;
    glm::vec3 gizmoPosition() const;
    glm::quat gizmoRotation() const;
    glm::vec3 viewDirection() const;

    void clickSelect(const QPoint& viewportPos, Qt::KeyboardModifiers modifiers);
    void updateMarquee(const QPoint& viewportPos);
    void finishMarquee(Qt::KeyboardModifiers modifiers);
    QList<QUuid> entitiesInRect(const QRectF& rect) const;

    bool snapActive() const;
    void beginGizmoDrag(GizmoHandle handle, const PickRay& ray);
    void moveGizmoDrag(const QPoint& viewportPos);
    // V held while moving: the vertex of another object's mesh under the cursor, or its surface
    bool vertexSnapTarget(const QPoint& viewportPos, glm::vec3& target) const;
    void applyDrag(bool final);

    // creating
    glm::vec3 placementPoint(const QPoint* viewportPos = nullptr) const;
    float cameraYaw() const;
    void createAt(const QString& key, const glm::vec3& point);
    void createModelAt(const glm::vec3& point);
    void pasteAt(const glm::vec3& target);
    QList<QUuid> place(EntityItemProperties properties, const QString& text, const glm::vec3& point);
    void placeModel(const QUrl& url, const glm::vec3& point);
    void placeImage(const QUrl& url, const glm::vec3& point);
    void beginUpload(const QString& localPath, const glm::vec3& point, bool image);
    void pollModelDimensions();
    void showHierarchyMenu(const QPoint& globalPos);
    void showViewportMenu(const QPoint& viewportPos, const QPoint& globalPos);
    void addSelectionActions(QMenu& menu, const glm::vec3* pasteTarget);
    // a note in the middle of the toolbar; one with a timeout clears itself
    void setStatus(const QString& text, int clearAfterMs = 0);

    // read-only: the domain gives no building rights (an instance before World Edit is on)
    void updateReadOnly();
    QString readOnlyReason() const;
    // true when the world can be changed; otherwise says why on the toolbar and returns false
    bool editable();

    // wireframes for selected zones, lights and particle systems
    void updateBounds();

    bool _ready { false };
    bool _active { false };
    bool _gridVisible { true };
    EditorCamera _camera;
    EditorGrid _grid;
    EditorSelection _selection;
    EditorUndo _undo;
    EditorClipboard _clipboard;
    EditorGizmo _gizmo;
    EditorToolBar::Tool _tool { EditorToolBar::Tool::Move };
    bool _localSpace { false };
    bool _pivotMode { false };
    bool _snapAlways { false };
    bool _vertexSnap { false };         // V is held
    float _moveSnap { 0.25f };          // metres
    float _rotateSnap { 15.0f };        // degrees
    float _scaleSnap { 0.1f };          // of the scale factor

    QVector3D _axes[3];
    glm::quat _axesOrientation;
    bool _axesValid { false };
    int _frameCounter { 0 };

    // a click or a marquee in the viewport
    bool _clickPending { false };
    QPoint _pressPos;
    Qt::KeyboardModifiers _pressModifiers;
    bool _marqueeActive { false };
    QRectF _marqueeRect;

    // a gizmo drag: the top-level selected entities and where they started
    DragKind _dragKind { DragKind::None };
    QList<QUuid> _dragIds;
    QVector<glm::vec3> _dragPositions;
    QVector<glm::quat> _dragRotations;
    QVector<glm::vec3> _dragDimensions;
    QVector<EntityItemProperties> _dragBefore;
    glm::vec3 _dragPivot;
    glm::quat _dragGizmoRotation;
    glm::vec3 _dragDelta;
    glm::quat _dragRotationDelta;
    glm::vec3 _dragScale { 1.0f };
    QPoint _lastDragPos;
    QElapsedTimer _editTimer;

    // an edit in progress in the Inspector: the ids and the values before, for undo
    QTimer _inspectorRefresh;
    QString _inspectorEditKey;
    QList<QUuid> _inspectorEditIds;
    QVector<EntityItemProperties> _inspectorBefore;

    // a teleport puts the camera back on the avatar: what Shift+F framed is framed again
    bool _reframePending { false };
    AABox _reframeBox;
    int _reframeFrames { 0 };
    // a new model is a 1 m box until it loads and reports its size
    QTimer _modelDimensionsTimer;
    QHash<QUuid, int> _modelsAwaitingDimensions;   // id -> polls so far
    QList<QPointer<QAction>> _menuActions;
    int _uploadsActive { 0 };
    EditorWorld* _world { nullptr };
    void updateWorldControls();
    bool _readOnly { false };
    QTimer _statusTimer;
    QPoint _rightPressPos;
    EditorBounds _bounds;
    std::vector<EditorBoundsShape> _boundsShapes;
    // per entity in an Inspector edit of Scale: its own size, to turn scale into dimensions
    QVector<glm::vec3> _inspectorNatural;

    bool _panelsCreated { false };
    bool _syncingSelection { false };
    HierarchyPanel* _hierarchy { nullptr };
    InspectorPanel* _inspector { nullptr };
    EditorToolBar* _toolBar { nullptr };
    EntityTreeModel* _treeModel { nullptr };
    Setting::Handle<QByteArray> _layoutSetting { "editor/layout2", QByteArray() };   // 2: the panels no longer float
    // build mode narrows the field of view; the user's own is kept here so it survives a crash
    Setting::Handle<float> _fovBeforeBuild { "editor/fovBeforeBuild", 0.0f };
    float _savedFov { 0.0f };
};

#endif // hifi_Editor_h
