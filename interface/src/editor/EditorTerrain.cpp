//
//  EditorTerrain.cpp
//  interface/src/editor
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include "EditorTerrain.h"

#include <algorithm>
#include <cmath>

#include <QtCore/QByteArray>

#include <EntityItemID.h>
#include <EntityItemProperties.h>
#include <EntityScriptingInterface.h>
#include <EntityTree.h>
#include <EntityTreeRenderer.h>
#include <GLMHelpers.h>
#include <NumericalConstants.h>
#include <PickFilter.h>
#include <RenderableTerrainEntityItem.h>
#include <terrain/TerrainFile.h>

#include "Application.h"
#include "EditorCreate.h"
#include "InterfaceLogging.h"

namespace {
    const float RAISE_RATE = 6.0f;          // voxels of distance a second at full strength, for Raise and Lower
    const float BLEND_RATE = 4.0f;          // blending a second at full strength, for Smooth and Flatten
    const float STEP_MIN = 0.2f;            // the smallest step applied: below it the byte distances lose the falloff
    const float REAPPLY_FRACTION = 0.25f;   // Dig, Fill and Paint go again once the pointer has moved this much of the radius
    const float MIN_VOXEL_RADIUS = 0.75f;   // a brush smaller than a voxel would change nothing
    const int TERRAIN_SEARCH_FRAMES = 30;   // how often the world's terrains are looked for
    const float TERRAIN_SEARCH_RADIUS = 4000.0f;
    const int OUTLINE_SEGMENTS = 48;
    const float OUTLINE_WIDTH_PIXELS = 2.0f;
    const glm::vec4 OUTLINE_COLOR(0.45f, 0.75f, 1.0f, 1.0f);
    const int SAVE_DELAY_MS = 2500;         // after the last change, before the file goes up
    const int SAVED_NOTE_MS = 5000;
    const int FAILED_NOTE_MS = 12000;
    const QString TERRAIN_FOLDER = QStringLiteral("/terrain");
    const QString TERRAIN_MIME_TYPE = QStringLiteral("application/octet-stream");

    // the world's visible terrains, picked against their ground
    PickFilter terrainPickFilter() {
        PickFilter::Flags flags;
        flags.set(PickFilter::DOMAIN_ENTITIES);
        flags.set(PickFilter::VISIBLE);
        flags.set(PickFilter::COLLIDABLE);
        flags.set(PickFilter::NONCOLLIDABLE);
        flags.set(PickFilter::PRECISE);
        return PickFilter(flags);
    }

    EntityTreePointer entityTree() {
        auto renderer = qApp->getEntities();
        return renderer ? renderer->getTree() : nullptr;
    }

    // Dig, Fill and Paint set what is under the brush rather than pushing it a little each step
    bool isShaping(terrain::SculptMode mode) {
        return mode == terrain::SculptMode::Dig || mode == terrain::SculptMode::Fill || mode == terrain::SculptMode::Paint;
    }

    bool isBlending(terrain::SculptMode mode) {
        return mode == terrain::SculptMode::Smooth || mode == terrain::SculptMode::Flatten;
    }

    terrain::SculptMode invert(terrain::SculptMode mode) {
        switch (mode) {
            case terrain::SculptMode::Raise: return terrain::SculptMode::Lower;
            case terrain::SculptMode::Lower: return terrain::SculptMode::Raise;
            case terrain::SculptMode::Dig: return terrain::SculptMode::Fill;
            case terrain::SculptMode::Fill: return terrain::SculptMode::Dig;
            default: return mode;
        }
    }

    QString modeName(terrain::SculptMode mode) {
        switch (mode) {
            case terrain::SculptMode::Raise: return QStringLiteral("Raise");
            case terrain::SculptMode::Lower: return QStringLiteral("Lower");
            case terrain::SculptMode::Smooth: return QStringLiteral("Smooth");
            case terrain::SculptMode::Flatten: return QStringLiteral("Flatten");
            case terrain::SculptMode::Dig: return QStringLiteral("Dig");
            case terrain::SculptMode::Fill: return QStringLiteral("Fill");
            case terrain::SculptMode::Paint: return QStringLiteral("Paint");
        }
        return QStringLiteral("Sculpt");
    }
}

EditorTerrain::EditorTerrain(EditorUndo* undo, QObject* parent) : QObject(parent), _undo(undo) {
    _saveTimer.setSingleShot(true);
    _saveTimer.setInterval(SAVE_DELAY_MS);
    connect(&_saveTimer, &QTimer::timeout, this, &EditorTerrain::saveDirty);
}

void EditorTerrain::setBrush(const EditorTerrainBrush& brush) {
    _brush = brush;
    if (_hasLastHit && !_stroking) {
        if (auto entity = terrainEntity(_lastHit.id)) {
            showOutline(*entity, _lastHit);
        }
    }
}

void EditorTerrain::setView(const glm::vec3& cameraPosition, float pixelSize) {
    _cameraPosition = cameraPosition;
    _pixelSize = pixelSize;
}

// --- finding the ground -------------------------------------------------------------------------

void EditorTerrain::findTerrains() {
    _searchFrame = _frameCounter;
    auto tree = entityTree();
    if (!tree) {
        _terrains.clear();
        return;
    }
    QVector<QUuid> found;
    tree->withReadLock([&] {
        tree->evalEntitiesInSphereWithType(_cameraPosition, TERRAIN_SEARCH_RADIUS, EntityTypes::Terrain, terrainPickFilter(), found);
    });
    _terrains = found;
}

std::shared_ptr<RenderableTerrainEntityItem> EditorTerrain::terrainEntity(const QUuid& id) const {
    auto tree = entityTree();
    if (!tree) {
        return nullptr;
    }
    return std::dynamic_pointer_cast<RenderableTerrainEntityItem>(tree->findEntityByID(id));
}

bool EditorTerrain::pick(const PickRay& ray, Hit& hit) {
    if (!ray) {
        return false;
    }
    if (_terrains.isEmpty()) {
        findTerrains();
        if (_terrains.isEmpty()) {
            return false;
        }
    }
    QVector<EntityItemID> include;
    for (const auto& id : _terrains) {
        include << EntityItemID(id);
    }
    auto entities = DependencyManager::get<EntityScriptingInterface>();
    auto result = entities->evalRayIntersectionVector(ray, terrainPickFilter(), include, QVector<EntityItemID>());
    if (!result.intersects) {
        return false;
    }
    auto entity = terrainEntity(result.entityID);
    if (!entity || !entity->hasGrid()) {
        return false;
    }
    hit.id = result.entityID;
    hit.worldPoint = result.intersection;
    hit.voxelPoint = glm::vec3(entity->worldToVoxelMatrix() * glm::vec4(hit.worldPoint, 1.0f));
    return true;
}

bool EditorTerrain::pointsAtTerrain(const PickRay& ray) {
    Hit hit;
    return pick(ray, hit);
}

bool EditorTerrain::hover(const PickRay& ray) {
    if (_stroking) {
        return true;
    }
    Hit hit;
    auto entity = pick(ray, hit) ? terrainEntity(hit.id) : nullptr;
    if (!entity) {
        _hasLastHit = false;
        _outline.clear();
        return false;
    }
    showOutline(*entity, hit);
    return true;
}

// --- the brush ----------------------------------------------------------------------------------

terrain::Brush EditorTerrain::voxelBrush(const RenderableTerrainEntityItem& entity, const Hit& hit) const {
    const glm::vec3 voxelScale = entity.getVoxelScale();
    const float metresPerVoxel = std::max((voxelScale.x + voxelScale.y + voxelScale.z) / 3.0f, 1.0e-4f);
    terrain::Brush brush;
    brush.center = hit.voxelPoint;
    brush.radius = std::max(MIN_VOXEL_RADIUS, _brush.radius / metresPerVoxel);
    brush.hardness = glm::clamp(_brush.hardness, 0.0f, 1.0f);
    brush.material = (uint8_t)glm::clamp(_brush.material, 0, 255);
    brush.flattenHeight = _flattenHeight;
    return brush;
}

void EditorTerrain::captureBefore(const RenderableTerrainEntityItem& entity, const terrain::Brush& brush) {
    const terrain::VoxelGrid& grid = entity.getGrid();
    // the same reach as the brush's (Brush.cpp): Dig and Fill go one voxel past the radius
    const int reach = 1;
    const glm::ivec3 low = glm::max(glm::ivec3(glm::floor(brush.center - brush.radius)) - reach, glm::ivec3(0));
    const glm::ivec3 high = glm::min(glm::ivec3(glm::ceil(brush.center + brush.radius)) + reach, grid.getVoxelCount() - 1);
    if (glm::any(glm::greaterThan(low, high))) {
        return;
    }
    const glm::ivec3 chunkLow = terrain::VoxelGrid::chunkOf(low);
    const glm::ivec3 chunkHigh = terrain::VoxelGrid::chunkOf(high);
    for (int z = chunkLow.z; z <= chunkHigh.z; ++z) {
        for (int y = chunkLow.y; y <= chunkHigh.y; ++y) {
            for (int x = chunkLow.x; x <= chunkHigh.x; ++x) {
                const glm::ivec3 chunk(x, y, z);
                if (grid.containsChunk(chunk) && _before.find(chunk) == _before.end()) {
                    _before.emplace(chunk, grid.getChunk(chunk));
                }
            }
        }
    }
}

// --- the stroke ---------------------------------------------------------------------------------

bool EditorTerrain::beginStroke(const PickRay& ray, bool inverted, QString& refusal) {
    refusal.clear();
    if (_stroking) {
        endStroke(false);
    }
    Hit hit;
    if (!pick(ray, hit)) {
        return false;
    }
    if (EditorUndo::isLocked(hit.id)) {
        refusal = QStringLiteral("The terrain is locked");
        return false;
    }
    _stroking = true;
    _strokeId = hit.id;
    _ray = ray;
    _hasRay = true;
    _strokeMode = inverted ? invert(_brush.mode) : _brush.mode;
    _flattenHeight = hit.voxelPoint.y;
    _accumulated = STEP_MIN;   // the first step lands at once: a click alone does something
    _steps = 0;
    _changed = 0;
    _before.clear();
    update(0.0f);
    return true;
}

void EditorTerrain::moveStroke(const PickRay& ray) {
    _ray = ray;
    _hasRay = true;
}

void EditorTerrain::update(float deltaTime) {
    ++_frameCounter;
    if (_searchFrame < 0 || _frameCounter - _searchFrame >= TERRAIN_SEARCH_FRAMES) {
        findTerrains();
    }
    if (!_stroking) {
        return;
    }
    auto entity = terrainEntity(_strokeId);
    if (!entity || !entity->hasGrid()) {
        dropStroke();   // the terrain went while it was being shaped
        return;
    }
    Hit hit;
    if (!_hasRay || !pick(_ray, hit) || hit.id != _strokeId) {
        return;         // the pointer is off the terrain: the stroke waits for it to come back
    }
    terrain::Brush brush = voxelBrush(*entity, hit);
    if (isShaping(_strokeMode)) {
        if (_steps > 0 && glm::distance(hit.voxelPoint, _lastStepPoint) < brush.radius * REAPPLY_FRACTION) {
            showOutline(*entity, hit);
            return;
        }
        brush.strength = 1.0f;
    } else {
        _accumulated += deltaTime * (isBlending(_strokeMode) ? BLEND_RATE : RAISE_RATE) * glm::clamp(_brush.strength, 0.0f, 1.0f);
        if (_accumulated < STEP_MIN) {
            showOutline(*entity, hit);
            return;
        }
        brush.strength = isBlending(_strokeMode) ? std::min(_accumulated, 1.0f) : _accumulated;
        _accumulated = 0.0f;
    }
    captureBefore(*entity, brush);
    const int changed = terrain::sculpt(entity->getGrid(), brush, _strokeMode);
    if (changed > 0) {
        _changed += changed;
        entity->gridEdited();
    }
    _lastStepPoint = hit.voxelPoint;
    ++_steps;
    showOutline(*entity, hit);
}

void EditorTerrain::endStroke(bool cancel) {
    if (!_stroking) {
        return;
    }
    _stroking = false;
    auto entity = terrainEntity(_strokeId);
    if (entity && entity->hasGrid()) {
        if (cancel) {
            restore(*entity, _before);
        } else if (_changed > 0 && !_before.empty()) {
            TerrainChunks after;
            for (const auto& pair : _before) {
                after.emplace(pair.first, entity->getGrid().getChunk(pair.first));
            }
            _undo->pushTerrainEdit(modeName(_strokeMode) + QStringLiteral(" Terrain"), _strokeId, _before, after);
            entity->markCollisionShapeStale();
            entity->gridEdited();
            markChanged(_strokeId);
        }
    }
    _before.clear();
    _steps = 0;
    _changed = 0;
    _accumulated = 0.0f;
}

void EditorTerrain::dropStroke() {
    _stroking = false;
    _before.clear();
    _steps = 0;
    _changed = 0;
    _accumulated = 0.0f;
    _hasLastHit = false;
    _outline.clear();
}

void EditorTerrain::restore(RenderableTerrainEntityItem& entity, const TerrainChunks& chunks) {
    terrain::VoxelGrid& grid = entity.getGrid();
    for (const auto& pair : chunks) {
        if (grid.containsChunk(pair.first)) {
            grid.setChunk(pair.first, pair.second);
        }
    }
    entity.markCollisionShapeStale();
    entity.gridEdited();
}

void EditorTerrain::clear() {
    if (_stroking) {
        endStroke(false);
    }
    _hasRay = false;
    _hasLastHit = false;
    _outline.clear();
}

// --- the file -----------------------------------------------------------------------------------

void EditorTerrain::markChanged(const QUuid& id) {
    _dirty.insert(id);
    _saveTimer.start();   // from the start again: the file goes up once the changes pause
}

void EditorTerrain::flushSaves() {
    _saveTimer.stop();
    saveDirty();
}

void EditorTerrain::saveDirty() {
    bool waiting = false;
    for (const QUuid& id : QSet<QUuid>(_dirty)) {
        if ((_stroking && id == _strokeId) || _saving.contains(id)) {
            waiting = true;   // in the middle of a stroke, or of the last save: it goes up after
            continue;
        }
        _dirty.remove(id);
        save(id);
    }
    if (waiting && !_saveTimer.isActive()) {
        _saveTimer.start();
    }
}

void EditorTerrain::save(const QUuid& id) {
    auto entity = terrainEntity(id);
    if (!entity || !entity->hasGrid()) {
        return;   // gone, or nothing to save
    }
    const terrain::Bytes bytes = terrain::saveTerrain(entity->getGrid());
    const QByteArray data((const char*)bytes.data(), (int)bytes.size());
    const QString fileName = QStringLiteral("terrain-") + id.toString(QUuid::WithoutBraces) + QStringLiteral(".drkt");
    const uint32_t version = std::max(entity->getTerrainVersion(), entity->getGridVersion()) + 1;

    auto upload = new EditorUpload(this);
    QString error;
    if (!upload->startData(data, fileName, TERRAIN_FOLDER, TERRAIN_MIME_TYPE, error)) {
        upload->deleteLater();
        _dirty.insert(id);   // kept for the next change to try again
        qCWarning(interfaceapp) << "terrain" << id << "could not be saved:" << error;
        emit status(QStringLiteral("Terrain not saved: ") + error, FAILED_NOTE_MS);
        return;
    }
    _saving.insert(id);
    emit status(QStringLiteral("Saving terrain…"), 0);
    connect(upload, &EditorUpload::finished, this, [this, upload, id, version](const QString& url) {
        upload->deleteLater();
        _saving.remove(id);
        auto entity = terrainEntity(id);
        if (entity) {
            // this client already holds what the file holds: told so, the renderer does not fetch its own save
            entity->setGridSource(url, version);
            EntityItemProperties properties;
            properties.setTerrainURL(url);
            properties.setTerrainVersion(version);
            DependencyManager::get<EntityScriptingInterface>()->editEntity(id, properties);
        }
        qCDebug(interfaceapp) << "terrain" << id << "saved as" << url << "version" << version;
        emit status(QStringLiteral("Terrain saved"), SAVED_NOTE_MS);
        if (_dirty.contains(id) && !_saveTimer.isActive()) {
            _saveTimer.start();   // changed again while this went up
        }
    });
    connect(upload, &EditorUpload::failed, this, [this, upload, id](const QString& error) {
        upload->deleteLater();
        _saving.remove(id);
        _dirty.insert(id);
        qCWarning(interfaceapp) << "terrain" << id << "could not be saved:" << error;
        emit status(QStringLiteral("Terrain not saved: ") + error, FAILED_NOTE_MS);
    });
}

// --- the outline --------------------------------------------------------------------------------

void EditorTerrain::showOutline(const RenderableTerrainEntityItem& entity, const Hit& hit) {
    _lastHit = hit;
    _hasLastHit = true;
    const glm::quat orientation = entity.getWorldOrientation();
    const glm::vec3 up = orientation * Vectors::UNIT_Y;
    const glm::vec3 right = orientation * Vectors::UNIT_X;
    const glm::vec3 forward = orientation * Vectors::UNIT_Z;
    const glm::mat4 worldToVoxel = entity.worldToVoxelMatrix();
    const glm::mat4 voxelToWorld = entity.voxelToWorldMatrix();
    // each point of the ring is dropped onto the ground from above it, so the ring lies on hills and in hollows
    const float lift = _brush.radius + 1.0f;
    const glm::vec3 voxelDown(worldToVoxel * glm::vec4(-up, 0.0f));
    QVector<glm::vec3> ring;
    ring.reserve(OUTLINE_SEGMENTS);
    for (int i = 0; i < OUTLINE_SEGMENTS; ++i) {
        const float angle = TWO_PI * (float)i / (float)OUTLINE_SEGMENTS;
        glm::vec3 point = hit.worldPoint + _brush.radius * (std::cos(angle) * right + std::sin(angle) * forward);
        const glm::vec3 voxelOrigin(worldToVoxel * glm::vec4(point + up * lift, 1.0f));
        float t = 0.0f;
        glm::vec3 normal;
        if (entity.rayHitsGround(voxelOrigin, voxelDown, t, normal)) {
            point = glm::vec3(voxelToWorld * glm::vec4(voxelOrigin + voxelDown * t, 1.0f));
        }
        ring << point;
    }
    const glm::vec3 lineOfSight = _cameraPosition - hit.worldPoint;
    const float depth = std::max(0.01f, glm::length(lineOfSight));
    const glm::vec3 toCamera = depth > 1.0e-4f ? lineOfSight / depth : Vectors::UNIT_Z;
    _outline.set(ring, OUTLINE_COLOR, _pixelSize * depth * OUTLINE_WIDTH_PIXELS, toCamera);
}
