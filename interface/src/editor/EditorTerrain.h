//
//  EditorTerrain.h
//  interface/src/editor
//
//  The Terrain tool of build mode: Unity's terrain brushes on a Terrain entity's voxels and its paint map
//  (TERRAIN.md). The pointer over a terrain shows the brush's outline on the ground; a press begins a stroke, which
//  applies the brush under the pointer every frame until the button goes up, and one stroke is one undo step. The
//  voxels or the map change on this client first; a moment after the last stroke (or undo) both are saved as a
//  terrain file in the user's files, and the entity's terrainURL and terrainVersion are set so every other client
//  fetches it. Resample remakes a terrain's voxels at its voxel size (the Inspector's Voxel Size over the box's
//  current size), the same ground finer or coarser, for a terrain that was scaled or whose voxel size changed.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#ifndef hifi_EditorTerrain_h
#define hifi_EditorTerrain_h

#include <memory>

#include <QtCore/QObject>
#include <QtCore/QSet>
#include <QtCore/QString>
#include <QtCore/QTimer>
#include <QtCore/QUuid>
#include <QtCore/QVector>

#include <glm/glm.hpp>

#include <RegisteredMetaTypes.h>
#include <terrain/Brush.h>

#include "EditorBrushOutline.h"
#include "EditorUndo.h"

class RenderableTerrainEntityItem;

// The brush as the tool bar sets it. The radius is in metres and becomes voxels on each terrain; the strength is
// how fast Raise, Lower, Smooth and Flatten work, and how much one pass of Paint lays down (Dig, Fill and Paint
// Voxels always take the whole sphere).
struct EditorTerrainBrush {
    terrain::SculptMode mode { terrain::SculptMode::Raise };
    float radius { 3.0f };
    float strength { 0.5f };    // 0 to 1
    float hardness { 0.5f };    // 0 to 1: the part of the radius with the full effect
    int material { 0 };
};

class EditorTerrain : public QObject {
    Q_OBJECT
public:
    explicit EditorTerrain(EditorUndo* undo, QObject* parent = nullptr);

    const EditorTerrainBrush& brush() const { return _brush; }
    void setBrush(const EditorTerrainBrush& brush);

    // the camera, for the outline's width on screen and its facing; pixelSize is the size of a viewport pixel one
    // metre in front of the camera
    void setView(const glm::vec3& cameraPosition, float pixelSize);
    // every frame while the tool is active: a stroke applies a step under the pointer
    void update(float deltaTime);

    // the pointer over the viewport with no button down: the outline follows the ground under it; false when it
    // is over no terrain
    bool hover(const PickRay& ray);
    bool pointsAtTerrain(const PickRay& ray);
    // a press on a terrain: a stroke begins there, with Raise and Lower, Dig and Fill swapped when inverted (Shift
    // held). False when the ray meets no terrain, or with the reason when the terrain can't be sculpted
    bool beginStroke(const PickRay& ray, bool inverted, QString& refusal);
    void moveStroke(const PickRay& ray);
    // the stroke's end: one undo step, and the collision shape follows; cancelled, the ground goes back as it was
    void endStroke(bool cancel);
    bool isStroking() const { return _stroking; }
    // the outline goes, and a stroke in progress ends as it is
    void clear();

    // resamples the terrains among the ids (or, with none there, the terrain under the pointer) so that their voxels
    // are their Voxel Size again: one undo step each, then saved. The status line says what happened, or why not
    void resample(const QList<QUuid>& ids);

    // a terrain's voxels changed on this client (a stroke, an undo): its file is saved a moment later, once no
    // stroke is in progress; flushSaves() saves what is waiting now (build mode ending)
    void markChanged(const QUuid& id);
    void flushSaves();
    bool hasPendingSaves() const { return !_dirty.isEmpty() || !_saving.isEmpty(); }

signals:
    // a note for the toolbar: saving, saved, or why not; clearAfterMs 0 keeps it until the next note
    void status(const QString& text, int clearAfterMs);

private:
    struct Hit {
        QUuid id;
        glm::vec3 worldPoint { 0.0f };
        glm::vec3 voxelPoint { 0.0f };
    };
    bool pick(const PickRay& ray, Hit& hit);
    std::shared_ptr<RenderableTerrainEntityItem> terrainEntity(const QUuid& id) const;
    void findTerrains();
    terrain::Brush voxelBrush(const RenderableTerrainEntityItem& entity, const Hit& hit) const;
    terrain::PaintBrush paintBrush(const RenderableTerrainEntityItem& entity, const Hit& hit) const;
    // keeps a copy of every chunk (or, painting, every column's block of the map) the brush can reach, the first
    // time the stroke reaches it
    void captureBefore(const RenderableTerrainEntityItem& entity, const terrain::Brush& brush);
    void captureBeforePaint(const RenderableTerrainEntityItem& entity, const terrain::PaintBrush& brush);
    void showOutline(const RenderableTerrainEntityItem& entity, const Hit& hit);
    void dropStroke();
    // puts the chunks, or the paint blocks, a cancelled stroke kept back
    void restore(RenderableTerrainEntityItem& entity, const TerrainChunks& chunks);
    void restorePaint(RenderableTerrainEntityItem& entity, const TerrainPaintBlocks& blocks);
    void saveDirty();
    void save(const QUuid& id);
    void resampleOne(const QUuid& id);

    EditorUndo* _undo;
    EditorTerrainBrush _brush;
    EditorBrushOutline _outline;
    glm::vec3 _cameraPosition { 0.0f };
    float _pixelSize { 0.001f };

    QVector<QUuid> _terrains;       // the world's terrains, looked for now and then
    int _searchFrame { -1 };
    int _frameCounter { 0 };
    Hit _lastHit;                   // where the outline was last shown, to redraw it when the brush changes
    bool _hasLastHit { false };

    bool _stroking { false };
    QUuid _strokeId;
    PickRay _ray;
    bool _hasRay { false };
    terrain::SculptMode _strokeMode { terrain::SculptMode::Raise };
    bool _erasing { false };        // a Paint stroke with Shift: back toward the voxels' own material
    float _flattenHeight { 0.0f };  // voxel units: where the stroke began
    float _accumulated { 0.0f };    // the brush's effect waiting to be applied as one step
    int _steps { 0 };
    int _changed { 0 };             // voxels, or texels, changed by the stroke so far
    glm::vec3 _lastStepPoint { 0.0f };
    TerrainChunks _before;
    TerrainPaintBlocks _beforePaint;

    // the terrains whose files are out of date, and those whose upload is on its way
    QSet<QUuid> _dirty;
    QSet<QUuid> _saving;
    QTimer _saveTimer;
};

#endif // hifi_EditorTerrain_h
