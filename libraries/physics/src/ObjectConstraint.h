//
//  ObjectConstraint.h
//  libraries/physcis/src
//
//  Created by Seth Alves 2017-4-11
//  Copyright 2017 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#ifndef hifi_ObjectConstraint_h
#define hifi_ObjectConstraint_h

#include <QUuid>

#include <Jolt/Jolt.h>
#include <Jolt/Core/Reference.h>
#include <Jolt/Physics/Constraints/Constraint.h>

#include "ObjectDynamic.h"

// A constraint ties the entity's body to another body (or to the world) through a Jolt constraint, which the
// engine adds to the physics system once the constraint can be built, that is, once both bodies exist.
class ObjectConstraint : public ObjectDynamic
{
public:
    ObjectConstraint(EntityDynamicType type, const QUuid& id, EntityItemPointer ownerEntity);
    virtual ~ObjectConstraint() {}

    virtual JPH::Constraint* getConstraint() = 0;

    virtual bool isConstraint() const override { return true; }

    virtual void invalidate() override;

protected:
    JPH::Ref<JPH::Constraint> _constraint;
};

#endif // hifi_ObjectConstraint_h
