//
//  SafeLanding.h
//  interface/src/octree
//
//  Created by Simon Walton.
//  Copyright 2018 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

// Controller for logic to wait for local collision bodies before enabling physics.

#ifndef hifi_SafeLanding_h
#define hifi_SafeLanding_h

#include <QtCore/QObject>
#include <QtCore/QSharedPointer>

#include <atomic>
#include <set>

#include "EntityItem.h"
#include "EntityDynamicInterface.h"

#include "EntityTreeRenderer.h"

class EntityTreeRenderer;
class EntityItemID;

class SafeLanding : public QObject {
public:
    static constexpr OCTREE_PACKET_SEQUENCE MAX_SEQUENCE = std::numeric_limits<OCTREE_PACKET_SEQUENCE>::max();
    static constexpr OCTREE_PACKET_SEQUENCE INVALID_SEQUENCE = MAX_SEQUENCE; // not technically invalid, but close enough

    void startTracking(QSharedPointer<EntityTreeRenderer> entityTreeRenderer);
    void updateTracking();
    void stopTracking();
    void reset();
    bool isTracking() const { return _trackingEntities; }
    bool trackingIsComplete() const;

    void finishSequence(OCTREE_PACKET_SEQUENCE first, OCTREE_PACKET_SEQUENCE last);  // 'last' exclusive.
    void addToSequence(OCTREE_PACKET_SEQUENCE sequenceNumber);
    float loadingProgressPercentage();

    // Whole-world loading (the domain's loading.whole_world): wait until the entities received are drawn as well as
    // ready for physics, as interstitial mode does: those within `radius` metres of the avatar, or all with 0
    void setWaitForVisuals(bool wait, float radius = 0.0f) { _visualsRadius = radius; _waitForVisuals = wait; }
    struct Progress {
        int received { 0 };            // entities received since tracking started
        int waiting { 0 };             // of those, not yet ready (and near enough to be waited for)
        bool serverDone { false };     // the entity server has said its first pass is complete
    };
    Progress getProgress();

private slots:
    void addTrackedEntity(const EntityItemID& entityID);
    void deleteTrackedEntity(const EntityItemID& entityID);

private:
    bool isEntityPhysicsReady(const EntityItemPointer& entity);
    void debugDumpSequenceIDs() const;

    std::mutex _lock;
    using Locker = std::lock_guard<std::mutex>;
    bool _trackingEntities { false };
    QSharedPointer<EntityTreeRenderer> _entityTreeRenderer;
    using EntityMap = std::map<EntityItemID, EntityItemPointer>;
    EntityMap _trackedEntities;

    OCTREE_PACKET_SEQUENCE _sequenceStart { INVALID_SEQUENCE };
    OCTREE_PACKET_SEQUENCE _sequenceEnd { INVALID_SEQUENCE };
    int32_t _maxTrackedEntityCount { 0 };
    int32_t _receivedEntityCount { 0 };
    std::atomic<bool> _waitForVisuals { false };
    std::atomic<float> _visualsRadius { 0.0f };
    int32_t _blockingCount { 0 };       // tracked entities the last update still waited for
    int32_t _trackedEntityStabilityCount { 0 };

    quint64 _startTime { 0 };

    struct SequenceLessThan {
        bool operator()(const OCTREE_PACKET_SEQUENCE& a, const OCTREE_PACKET_SEQUENCE& b) const;
    };

    using SequenceSet = std::set<OCTREE_PACKET_SEQUENCE, SequenceLessThan>;
    SequenceSet _sequenceNumbers;

    float loadingPriority(const EntityItem& entityItem) const;
    CalculateEntityLoadingPriority _prevEntityLoadingPriorityOperator { nullptr };
};

#endif  // hifi_SafeLanding_h
