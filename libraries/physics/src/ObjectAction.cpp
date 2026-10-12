//
//  ObjectAction.cpp
//  libraries/physcis/src
//
//  Created by Seth Alves 2015-6-2
//  Copyright 2015 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include "ObjectAction.h"

#include "EntitySimulation.h"
#include "PhysicsLogging.h"

ObjectAction::ObjectAction(EntityDynamicType type, const QUuid& id, EntityItemPointer ownerEntity) :
    ObjectDynamic(type, id, ownerEntity)
{
}

void ObjectAction::updateAction(float deltaTimeStep) {
    quint64 expiresWhen = 0;
    EntityItemPointer ownerEntity = nullptr;

    withReadLock([&]{
        ownerEntity = _ownerEntity.lock();
        expiresWhen = _expires;
    });

    if (!ownerEntity) {
        // the entity is gone; the engine drops the action when the owner's body goes
        return;
    }

    if (expiresWhen > 0) {
        quint64 now = usecTimestampNow();
        if (now > expiresWhen) {
            QUuid myID;
            withWriteLock([&]{
                _active = false;
                myID = getID();
            });
            if (ownerEntity) {
                ownerEntity->removeAction(nullptr, myID);
            }
        }
    }

    if (!_active) {
        return;
    }

    if (ownerEntity->getLocked()) {
        return;
    }

    updateActionWorker(deltaTimeStep);
}
