//
//  JoltLayers.h
//  libraries/physics/src
//
//  How the engine's collision groups and masks (PhysicsCollisionGroups.h) become Jolt object layers.
//
//  Jolt decides whether two bodies may collide from their object layers. With 16-bit layers and
//  ObjectLayerPairFilterMask a layer packs a group in its low 8 bits and a mask in its high 8 bits, and two
//  bodies collide when each one's group is in the other's mask: exactly the rule the groups and masks in
//  PhysicsCollisionGroups.h were written for. The seven groups fit in 8 bits; the COLLISIONLESS group
//  becomes group 0 with mask 0, which collides with nothing.
//
//  Broad phase layers split bodies that never move (static) from the rest, so the static tree is never
//  rebuilt for a moving body.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#ifndef hifi_JoltLayers_h
#define hifi_JoltLayers_h

#include <stdint.h>

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>
#include <Jolt/Physics/Collision/ObjectLayerPairFilterMask.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayerInterfaceMask.h>
#include <Jolt/Physics/Collision/BroadPhase/ObjectVsBroadPhaseLayerFilterMask.h>

#include <PhysicsCollisionGroups.h>

namespace JoltLayers {
    static_assert(JPH_OBJECT_LAYER_BITS == 16, "JoltLayers packs an 8-bit group and an 8-bit mask into a 16-bit layer");

    const uint32_t GROUP_BITS = JPH::ObjectLayerPairFilterMask::cMask; // 0xff

    // The group and mask are the 32-bit values the engine has always used (PhysicsCollisionGroups.h).
    // Only their low 8 bits are meaningful; the COLLISIONLESS bit (bit 31) falls away, which is correct
    // because a collisionless body has mask 0 and collides with nothing.
    inline JPH::ObjectLayer layerFor(int32_t group, int32_t mask) {
        return JPH::ObjectLayerPairFilterMask::sGetObjectLayer((uint32_t)group & GROUP_BITS, (uint32_t)mask & GROUP_BITS);
    }

    inline int32_t groupOf(JPH::ObjectLayer layer) {
        return (int32_t)JPH::ObjectLayerPairFilterMask::sGetGroup(layer);
    }

    inline int32_t maskOf(JPH::ObjectLayer layer) {
        return (int32_t)JPH::ObjectLayerPairFilterMask::sGetMask(layer);
    }

    // Broad phase layers: static bodies in one tree, everything that can move in the other.
    namespace BroadPhase {
        const JPH::BroadPhaseLayer NON_MOVING(0);
        const JPH::BroadPhaseLayer MOVING(1);
        const uint32_t NUM_LAYERS = 2;
    }

    // One of each per physics world; Jolt keeps references to them for the world's lifetime.
    class Tables {
    public:
        Tables() : broadPhaseLayers(BroadPhase::NUM_LAYERS), objectVsBroadPhase(broadPhaseLayers) {
            // static bodies go to the NON_MOVING tree; everything else, including the groups that have no
            // static members, lands in the last (MOVING) layer, which may collide with anything
            broadPhaseLayers.ConfigureLayer(BroadPhase::NON_MOVING, BULLET_COLLISION_GROUP_STATIC, 0);
            broadPhaseLayers.ConfigureLayer(BroadPhase::MOVING, ~(uint32_t)BULLET_COLLISION_GROUP_STATIC & GROUP_BITS, 0);
        }
        JPH::BroadPhaseLayerInterfaceMask broadPhaseLayers;
        JPH::ObjectVsBroadPhaseLayerFilterMask objectVsBroadPhase;
        JPH::ObjectLayerPairFilterMask objectLayerPairs;
    };
}

#endif // hifi_JoltLayers_h
