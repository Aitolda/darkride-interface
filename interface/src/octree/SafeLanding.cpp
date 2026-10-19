//
//  SafeLanding.cpp
//  interface/src/octree
//
//  Created by Simon Walton.
//  Copyright 2018 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include "SafeLanding.h"
#include <SharedUtil.h>

#include "EntityTreeRenderer.h"
#include "RenderableModelEntityItem.h"
#include "InterfaceLogging.h"
#include "Application.h"
#include "avatar/AvatarManager.h"


CalculateEntityLoadingPriority SafeLanding::entityLoadingOperatorElevateCollidables = [](const EntityItem& entityItem) {
    const int COLLIDABLE_ENTITY_PRIORITY = 10.0f;
    return entityItem.getCollisionless() * COLLIDABLE_ENTITY_PRIORITY;
};

namespace {
    template<typename T> bool lessThanWraparound(int32_t a, int32_t b) {
        constexpr int32_t MAX_T_VALUE = std::numeric_limits<T>::max();
        if (b <= a) {
            b += MAX_T_VALUE;
        }
        return (b - a) < (MAX_T_VALUE / 2);
    }
}

bool SafeLanding::SequenceLessThan::operator()(const OCTREE_PACKET_SEQUENCE& a, const OCTREE_PACKET_SEQUENCE& b) const {
    return lessThanWraparound<OCTREE_PACKET_SEQUENCE>(a, b);
}

void SafeLanding::startTracking(QSharedPointer<EntityTreeRenderer> entityTreeRenderer) {
    if (!entityTreeRenderer.isNull()) {
        auto entityTree = entityTreeRenderer->getTree();
        if (entityTree && !_trackingEntities) {
            Locker lock(_lock);
            _entityTreeRenderer = entityTreeRenderer;
            _trackedEntities.clear();
            _maxTrackedEntityCount = 0;
            _sequenceStart = SafeLanding::INVALID_SEQUENCE;
            _sequenceEnd = SafeLanding::INVALID_SEQUENCE;
            _sequenceNumbers.clear();
            _trackingEntities = true;
            _startTime = usecTimestampNow();
            _addedCount = 0;
            _notTrackedCreatedLater = 0;
            _notTrackedNotFound = 0;
            _notTrackedLocal = 0;
            _completionCount = 0;

            connect(std::const_pointer_cast<EntityTree>(entityTree).get(),
                &EntityTree::addingEntity, this, &SafeLanding::addTrackedEntity, Qt::DirectConnection);
            connect(std::const_pointer_cast<EntityTree>(entityTree).get(),
                &EntityTree::deletingEntity, this, &SafeLanding::deleteTrackedEntity);

            _prevEntityLoadingPriorityOperator = EntityTreeRenderer::getEntityLoadingPriorityOperator();
            EntityTreeRenderer::setEntityLoadingPriorityFunction(entityLoadingOperatorElevateCollidables);
        }
    }
}

void SafeLanding::addTrackedEntity(const EntityItemID& entityID) {
    if (_trackingEntities && _entityTreeRenderer) {
        Locker lock(_lock);
        auto entityTree = _entityTreeRenderer->getTree();
        if (entityTree) {
            EntityItemPointer entity = entityTree->findEntityByID(entityID);
            if (!entity) {
                _notTrackedNotFound++;
            } else if (entity->isLocalEntity()) {
                _notTrackedLocal++;
            } else if (entity->getCreated() >= _startTime) {
                _notTrackedCreatedLater++;
            }
            if (entity && !entity->isLocalEntity() && entity->getCreated() < _startTime) {
                _addedCount++;
                _trackedEntities.emplace(entityID, entity);

                int32_t trackedEntityCount = (int32_t)_trackedEntities.size();
                if (trackedEntityCount > _maxTrackedEntityCount) {
                    _maxTrackedEntityCount = trackedEntityCount;
                    _trackedEntityStabilityCount = 0;
                }
            }
        }
    }
}

void SafeLanding::deleteTrackedEntity(const EntityItemID& entityID) {
    Locker lock(_lock);
    _trackedEntities.erase(entityID);
}

void SafeLanding::finishSequence(OCTREE_PACKET_SEQUENCE first, OCTREE_PACKET_SEQUENCE last) {
    Locker lock(_lock);
    if (_trackingEntities) {
        _sequenceStart = first;
        _sequenceEnd = last;
        _completionCount++;
        qDebug() << "safe landing: the entity server reports the initial scene complete (" << _completionCount << "), sequence"
            << first << "to" << last << "," << _sequenceNumbers.size() << "packets seen,"
            << (usecTimestampNow() - _startTime) / 1000 << "ms after tracking began";
    }
}

void SafeLanding::addToSequence(OCTREE_PACKET_SEQUENCE sequenceNumber) {
    Locker lock(_lock);
    _sequenceNumbers.insert(sequenceNumber);
}

void SafeLanding::updateTracking() {
    if (!_trackingEntities || !_entityTreeRenderer) {
        return;
    }

    {
        Locker lock(_lock);
        bool enableInterstitial = DependencyManager::get<NodeList>()->getDomainHandler().getInterstitialModeEnabled() ||
            _waitForVisuals;
        auto entityMapIter = _trackedEntities.begin();
        while (entityMapIter != _trackedEntities.end()) {
            auto entity = entityMapIter->second;
            bool isVisuallyReady = true;
            if (enableInterstitial) {
                auto entityRenderable = _entityTreeRenderer->renderableForEntityId(entityMapIter->first);
                if (!entityRenderable) {
                    _entityTreeRenderer->addingEntity(entityMapIter->first);
                }
                isVisuallyReady = entity->isVisuallyReady() || (!entityRenderable && !entity->isParentPathComplete());
            }
            if (isEntityPhysicsReady(entity) && isVisuallyReady) {
                entityMapIter = _trackedEntities.erase(entityMapIter);
            } else {
                entityMapIter++;
            }
        }
        if (enableInterstitial) {
            _trackedEntityStabilityCount++;
        }
    }

    {
        // a line a second while tracking (PHYSICS.md round 10: what the loading screen waits for)
        quint64 now = usecTimestampNow();
        if (now - _lastProgressLog > USECS_PER_SECOND) {
            _lastProgressLog = now;
            Locker lock(_lock);
            bool enableInterstitial = DependencyManager::get<NodeList>()->getDomainHandler().getInterstitialModeEnabled() ||
                _waitForVisuals;
            int physicsWaiting = 0, visualsWaiting = 0;
            QStringList examples;
            glm::vec3 avatarPosition = DependencyManager::get<AvatarManager>()->getMyAvatar()->getWorldPosition();
            for (const auto& entry : _trackedEntities) {
                const auto& entity = entry.second;
                bool physicsReady = isEntityPhysicsReady(entity);
                bool visuallyReady = !enableInterstitial || entity->isVisuallyReady();
                if (!physicsReady) { physicsWaiting++; }
                if (!visuallyReady) { visualsWaiting++; }
                if (examples.size() < 4 && (!physicsReady || !visuallyReady)) {
                    examples << QString("%1 '%2' %3 m %4%5").arg(EntityTypes::getEntityTypeName(entity->getType()), entity->getName(),
                        QString::number(glm::distance(avatarPosition, entity->getWorldPosition()), 'f', 1),
                        physicsReady ? "" : "physics ", visuallyReady ? "" : "drawing");
                }
            }
            QStringList gaps;
            if (_sequenceStart != SafeLanding::INVALID_SEQUENCE) {
                for (OCTREE_PACKET_SEQUENCE n = _sequenceStart; n != _sequenceEnd && gaps.size() < 8; ++n) {
                    if (_sequenceNumbers.find(n) == _sequenceNumbers.end()) {
                        gaps << QString::number(n);
                    }
                }
            }
            qDebug() << "safe landing: tracking" << _trackedEntities.size() << "of" << _maxTrackedEntityCount << "entities ("
                << physicsWaiting << "not ready for physics," << visualsWaiting << "not drawn; added" << _addedCount
                << ", not tracked: created after the start" << _notTrackedCreatedLater << ", not found" << _notTrackedNotFound
                << ", local" << _notTrackedLocal << "), sequence"
                << _sequenceStart << "to" << _sequenceEnd << "," << _sequenceNumbers.size() << "packets seen, gaps ["
                << qPrintable(gaps.join(" ")) << "], missing" << qApp->isMissingSequenceNumbers()
                << (examples.isEmpty() ? "" : qPrintable("; waiting for " + examples.join("; ")));
        }
    }
    if (_trackedEntities.empty()) {
        // no more tracked entities --> check sequenceNumbers
        if (_sequenceStart != SafeLanding::INVALID_SEQUENCE) {
            bool shouldStop = false;
            {
                Locker lock(_lock);
                auto sequenceSize = _sequenceEnd - _sequenceStart; // this works even in rollover case
                auto startIter = _sequenceNumbers.find(_sequenceStart);
                auto endIter = _sequenceNumbers.find(_sequenceEnd - 1);

                bool missingSequenceNumbers = qApp->isMissingSequenceNumbers();
                shouldStop = (sequenceSize == 0 ||
                    (startIter != _sequenceNumbers.end() &&
                     endIter != _sequenceNumbers.end() &&
                     ((distance(startIter, endIter) == sequenceSize - 1) || !missingSequenceNumbers)));
            }
            if (shouldStop) {
                qDebug() << "safe landing: complete," << (usecTimestampNow() - _startTime) / 1000 << "ms after tracking began ("
                    << _maxTrackedEntityCount << "entities tracked, sequence" << _sequenceStart << "to" << _sequenceEnd << ")";
                stopTracking();
            }
        }
    }
}

void SafeLanding::stopTracking() {
    Locker lock(_lock);
    if (_trackingEntities) {
        _trackingEntities = false;
        if (_entityTreeRenderer) {
            auto entityTree = _entityTreeRenderer->getTree();
            disconnect(std::const_pointer_cast<EntityTree>(entityTree).get(),
                &EntityTree::addingEntity, this, &SafeLanding::addTrackedEntity);
            disconnect(std::const_pointer_cast<EntityTree>(entityTree).get(),
                &EntityTree::deletingEntity, this, &SafeLanding::deleteTrackedEntity);
            _entityTreeRenderer.reset();
        }
        EntityTreeRenderer::setEntityLoadingPriorityFunction(_prevEntityLoadingPriorityOperator);
    }
}

void SafeLanding::reset() {
    _trackingEntities = false;
    _trackedEntities.clear();
    _maxTrackedEntityCount = 0;
    _sequenceStart = SafeLanding::INVALID_SEQUENCE;
    _sequenceEnd = SafeLanding::INVALID_SEQUENCE;
}

bool SafeLanding::trackingIsComplete() const {
    return !_trackingEntities && (_sequenceStart != SafeLanding::INVALID_SEQUENCE);
}

float SafeLanding::loadingProgressPercentage() {
    Locker lock(_lock);

    float entityReadyPercentage = 0.0f;
    if (_maxTrackedEntityCount > 0) {
        entityReadyPercentage = ((_maxTrackedEntityCount - _trackedEntities.size()) / (float)_maxTrackedEntityCount);
        qDebug() << "SAFE LANDING PROGRESS  " << _maxTrackedEntityCount << ". ERP IS " << entityReadyPercentage;
    }

    constexpr int32_t MINIMUM_TRACKED_ENTITY_STABILITY_COUNT = 15;
    if (_trackedEntityStabilityCount < MINIMUM_TRACKED_ENTITY_STABILITY_COUNT) {
        entityReadyPercentage *= 0.20f;
    }

    return entityReadyPercentage;
}

bool SafeLanding::isEntityPhysicsReady(const EntityItemPointer& entity) {
    if (entity && !entity->getCollisionless()) {
        const auto& entityType = entity->getType();
        if (entityType == EntityTypes::Model) {
            RenderableModelEntityItem * modelEntity = std::dynamic_pointer_cast<RenderableModelEntityItem>(entity).get();
            static const std::set<ShapeType> downloadedCollisionTypes
                { SHAPE_TYPE_COMPOUND, SHAPE_TYPE_SIMPLE_COMPOUND, SHAPE_TYPE_STATIC_MESH,  SHAPE_TYPE_SIMPLE_HULL };
            bool hasAABox;
            entity->getAABox(hasAABox);
            if (hasAABox && downloadedCollisionTypes.count(modelEntity->getShapeType()) != 0) {
                auto space = _entityTreeRenderer->getWorkloadSpace();
                uint8_t region = space ? space->getRegion(entity->getSpaceIndex()) : (uint8_t)workload::Region::INVALID;

                // Note: the meanings of the workload regions are:
                //   R1 = in physics simulation and willing to own simulation
                //   R2 = in physics simulation but does NOT want to own simulation
                //   R3 = not in physics simulation but kinematically animated when velocities are non-zero
                //   R4 = sorted by workload and found to be outside R3
                //   UNKNOWN = known to workload but not yet sorted
                //   INVALID = not known to workload
                // So any entity sorted into R3 or R4 is definitelyNotPhysical

                bool definitelyNotPhysical = region == workload::Region::R3 ||
                    region == workload::Region::R4 ||
                    !entity->shouldBePhysical() ||
                    modelEntity->unableToLoadCollisionShape();
                bool definitelyPhysical = entity->isInPhysicsSimulation();
                return definitelyNotPhysical || definitelyPhysical;
            }
        }
    }
    return true;
}

void SafeLanding::debugDumpSequenceIDs() const {
    qCDebug(interfaceapp) << "Sequence set size:" << _sequenceNumbers.size();

    auto itr = _sequenceNumbers.begin();
    OCTREE_PACKET_SEQUENCE p = SafeLanding::INVALID_SEQUENCE;
    if (itr != _sequenceNumbers.end()) {
        p = (*itr);
        qCDebug(interfaceapp) << "First:" << (int32_t)p;
        ++itr;
        while (itr != _sequenceNumbers.end()) {
            OCTREE_PACKET_SEQUENCE s = *itr;
            if (s != p + 1) {
                qCDebug(interfaceapp) << "Gap from" << (int32_t)p << "to" << (int32_t)s << "(exclusive)";
                p = s;
            }
            ++itr;
        }
        if (p != SafeLanding::INVALID_SEQUENCE) {
            qCDebug(interfaceapp) << "Last:" << p;
        }
    }
}
