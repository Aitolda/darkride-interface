//
//  ObjectAction.h
//  libraries/physcis/src
//
//  Created by Seth Alves 2015-6-2
//  Copyright 2015 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#ifndef hifi_ObjectAction_h
#define hifi_ObjectAction_h

#include <QUuid>

#include "ObjectDynamic.h"

// An action pushes on its entity's body every substep; the engine calls updateAction(dt) before stepping.
class ObjectAction : public ObjectDynamic {
public:
    ObjectAction(EntityDynamicType type, const QUuid& id, EntityItemPointer ownerEntity);
    virtual ~ObjectAction() {}

    virtual bool isAction() const override { return true; }

    virtual void updateActionWorker(float deltaTimeStep) = 0;
    void updateAction(float deltaTimeStep);
};

#endif // hifi_ObjectAction_h
