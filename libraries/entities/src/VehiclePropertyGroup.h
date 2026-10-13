//
//  VehiclePropertyGroup.h
//  libraries/entities/src
//
//  The vehicle property group (PHYSICS.md 4.8 and round 6): what makes an entity a vehicle and the state of its simulation.
//  See the jsdoc below for every property. Generated once from a table (scratchpad/gen_vehicle_group.py) and kept by hand.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#ifndef hifi_VehiclePropertyGroup_h
#define hifi_VehiclePropertyGroup_h

#include <stdint.h>

#include <glm/glm.hpp>

#include <QtCore/QUuid>
#include <QtScript/QScriptEngine>

#include "PropertyGroup.h"
#include "EntityItemPropertiesMacros.h"

class EntityItemProperties;
class EncodeBitstreamParams;
class OctreePacketData;
class ReadBitstreamToTreeParams;

// The vehicle's kind, as the type property names it.
namespace VehicleType {
    const QString NONE = "none";
    const QString WHEELED = "wheeled";
    const QString WHEEL = "wheel";
}

/**jsdoc
 * A vehicle is any entity with a <code>vehicle</code> group whose <code>type</code> is not <code>"none"</code> (PHYSICS.md
 * 4.8 and round 6). The entity is the chassis: its own collision shape (a box is best; the physics server always
 * uses a box of the entity's dimensions), its <code>mass</code> and <code>centerOfMass</code>, the wheels hung from it
 * and the seats in it. The names are Unity's where Unity has them (<code>Rigidbody</code>, <code>WheelCollider</code>)
 * so a Unity vehicle carries across, and Jolt's for the engine and transmission, which Unity leaves to scripts.
 * The vehicle is simulated by the physics server when the domain has one, else by the client that owns it; the driver's
 * client predicts its own inputs and is corrected by the server; everyone else runs the model from the last known
 * inputs and blends corrections. The state properties are read-only: the simulator writes them.
 *
 * @typedef {object} Entities.Vehicle
 * @property {string} type="none" - <code>"none"</code>, <code>"wheeled"</code> (a chassis with wheels), or
 *     <code>"wheel"</code> (a child entity that shows one of its parent's wheels: see <code>wheel</code>).
 * @property {number} mass=0 - The chassis mass in kg (Unity's <code>Rigidbody.mass</code>); <code>0</code> uses the
 *     entity's density and volume as for any entity.
 * @property {Vec3} centerOfMass=0,0,0 - The centre of mass, in the entity's local metres (<code>Rigidbody.centerOfMass</code>).
 *     Low and central keeps a car on its wheels.
 * @property {string} wheels="" - The wheels as a JSON list, each with Unity's <code>WheelCollider</code> names:
 *     <code>name</code>; <code>center</code> [x, y, z] in local metres, the suspension's top (fully compressed) attachment
 *     point; <code>radius</code> and <code>width</code> m; <code>mass</code> kg (default 20); <code>suspensionDistance</code>
 *     m, the travel from <code>center</code> downwards (default 0.2); <code>spring</code> N/m and <code>damper</code> N s/m
 *     (when absent, <code>suspensionFrequency</code> and <code>suspensionDamping</code> below set the spring);
 *     <code>steer</code>, a fraction of <code>maxSteerAngle</code> this wheel turns, negative to counter-steer (default 0);
 *     <code>drive</code>, <code>true</code> when the engine turns it (default from <code>driveType</code>);
 *     <code>brake</code> and <code>handBrake</code>, fractions of the brake torques this wheel takes (defaults 1 and 0).
 * @property {string} seats="" - The seats as a JSON list: <code>name</code>, <code>position</code> [x, y, z] in local
 *     metres (where the avatar's hips go), <code>yaw</code> in degrees about the entity's up (default 0, facing
 *     forward, the entity's +z... the vehicle's forward is its local -z as for every entity), and <code>driver</code>,
 *     <code>true</code> for the one seat whose occupant drives.
 * @property {number} maxSteerAngle=30 - The steering lock, in degrees, for a wheel with <code>steer</code> 1.
 * @property {number} maxTorque=500 - The engine's peak torque, N m (Jolt's <code>VehicleEngineSettings</code>).
 * @property {number} minRPM=1000 - The engine's idle, rpm.
 * @property {number} maxRPM=6000 - The engine's red line, rpm.
 * @property {string} gearRatios="" - The forward gear ratios as a JSON list, first gear first; empty for Jolt's
 *     five-speed default. The automatic gearbox shifts for the driver.
 * @property {number} reverseGearRatio=2.9 - The reverse gear's ratio.
 * @property {number} finalDriveRatio=3.42 - The differential's ratio (the axle, times the transfer box if any).
 * @property {number} maxBrakeTorque=1500 - The foot brake's torque per wheel, N m.
 * @property {number} maxHandBrakeTorque=4000 - The hand brake's torque per wheel with <code>handBrake</code> 1, N m.
 * @property {string} driveType="rear" - Which wheels the engine turns when a wheel does not say: <code>"rear"</code>,
 *     <code>"front"</code> or <code>"all"</code>. Wheels are paired into axles by their <code>center</code> z.
 * @property {number} suspensionFrequency=1.5 - The suspension's natural frequency in Hz, for wheels without a
 *     <code>spring</code>; 1 to 2 Hz is a road car, lower is softer.
 * @property {number} suspensionDamping=0.5 - The suspension's damping ratio for those wheels (1 is critical).
 * @property {string} wheel="" - On a child entity with <code>type</code> <code>"wheel"</code>: the <code>name</code> of
 *     the parent's wheel it shows. Every client places it from its own simulation: it steers, spins and rides the
 *     suspension, and it costs no packets.
 * @property {string} axle="x" - On a wheel entity: which of its own axes is the axle, <code>"x"</code> (a model made
 *     for Unity), <code>"y"</code> (a Cylinder shape entity) or <code>"z"</code>.
 * @property {Uuid} driver=null - <em>State.</em> The session of the avatar in the driver's seat, who steers it;
 *     set by that client when it sits, cleared when it stands.
 * @property {number} inputSequence=0 - <em>State.</em> The sequence number of the driver's last input the simulator
 *     applied; the driver's client reconciles its prediction from there.
 * @property {number} throttle=0 - <em>State.</em> The last applied forward input, -1 (reverse) to 1.
 * @property {number} steering=0 - <em>State.</em> The last applied steering input, -1 (left) to 1 (right).
 * @property {number} brake=0 - <em>State.</em> The last applied brake, 0 to 1.
 * @property {number} handBrake=0 - <em>State.</em> The last applied hand brake, 0 to 1.
 * @property {number} engineRPM=0 - <em>State.</em> The engine's speed.
 * @property {number} gear=0 - <em>State.</em> The gear: 1 and up forward, 0 neutral, -1 reverse.
 * @property {number} stateTime=0 - <em>State.</em> The domain time, in microseconds, that this state and the entity's
 *     position, rotation and velocities in the same update are for.
 */
class VehiclePropertyGroup : public PropertyGroup {
public:
    virtual void copyToScriptValue(const EntityPropertyFlags& desiredProperties, QScriptValue& properties,
                                   QScriptEngine* engine, bool skipDefaults,
                                   EntityItemProperties& defaultEntityProperties) const override;
    virtual void copyFromScriptValue(const QScriptValue& object, bool& _defaultSettings) override;

    void merge(const VehiclePropertyGroup& other);

    virtual void debugDump() const override;
    virtual void listChangedProperties(QList<QString>& out) override;

    virtual bool appendToEditPacket(OctreePacketData* packetData,
                                    EntityPropertyFlags& requestedProperties,
                                    EntityPropertyFlags& propertyFlags,
                                    EntityPropertyFlags& propertiesDidntFit,
                                    int& propertyCount,
                                    OctreeElement::AppendState& appendState) const override;

    virtual bool decodeFromEditPacket(EntityPropertyFlags& propertyFlags,
                                      const unsigned char*& dataAt, int& processedBytes) override;
    virtual void markAllChanged() override;
    virtual EntityPropertyFlags getChangedProperties() const override;

    // EntityItem related helpers
    // methods for getting/setting all properties of an entity
    virtual void getProperties(EntityItemProperties& propertiesOut) const override;

    /// returns true if something changed
    virtual bool setProperties(const EntityItemProperties& properties) override;

    virtual EntityPropertyFlags getEntityProperties(EncodeBitstreamParams& params) const override;

    virtual void appendSubclassData(OctreePacketData* packetData, EncodeBitstreamParams& params,
                                    EntityTreeElementExtraEncodeDataPointer entityTreeElementExtraEncodeData,
                                    EntityPropertyFlags& requestedProperties,
                                    EntityPropertyFlags& propertyFlags,
                                    EntityPropertyFlags& propertiesDidntFit,
                                    int& propertyCount,
                                    OctreeElement::AppendState& appendState) const override;

    virtual int readEntitySubclassDataFromBuffer(const unsigned char* data, int bytesLeftToRead,
                                                 ReadBitstreamToTreeParams& args,
                                                 EntityPropertyFlags& propertyFlags, bool overwriteLocalData,
                                                 bool& somethingChanged) override;

    // true for a chassis: a vehicle the physics simulates (not a wheel, not "none")
    bool isVehicle() const { return _type == VehicleType::WHEELED; }
    bool isWheel() const { return _type == VehicleType::WHEEL; }
    // the definition (everything but the state) as a version: the simulation rebuilds its vehicle when it changes
    uint64_t definitionHash() const;

    DEFINE_PROPERTY_REF(PROP_VEHICLE_TYPE, Type, type, QString, QString("none"));
    DEFINE_PROPERTY(PROP_VEHICLE_MASS, Mass, mass, float, 0.0f);
    DEFINE_PROPERTY_REF(PROP_VEHICLE_CENTER_OF_MASS, CenterOfMass, centerOfMass, glm::vec3, glm::vec3(0.0f));
    DEFINE_PROPERTY_REF(PROP_VEHICLE_WHEELS, Wheels, wheels, QString, QString(""));
    DEFINE_PROPERTY_REF(PROP_VEHICLE_SEATS, Seats, seats, QString, QString(""));
    DEFINE_PROPERTY(PROP_VEHICLE_MAX_STEER_ANGLE, MaxSteerAngle, maxSteerAngle, float, 30.0f);
    DEFINE_PROPERTY(PROP_VEHICLE_MAX_TORQUE, MaxTorque, maxTorque, float, 500.0f);
    DEFINE_PROPERTY(PROP_VEHICLE_MIN_RPM, MinRPM, minRPM, float, 1000.0f);
    DEFINE_PROPERTY(PROP_VEHICLE_MAX_RPM, MaxRPM, maxRPM, float, 6000.0f);
    DEFINE_PROPERTY_REF(PROP_VEHICLE_GEAR_RATIOS, GearRatios, gearRatios, QString, QString(""));
    DEFINE_PROPERTY(PROP_VEHICLE_REVERSE_GEAR_RATIO, ReverseGearRatio, reverseGearRatio, float, 2.9f);
    DEFINE_PROPERTY(PROP_VEHICLE_FINAL_DRIVE_RATIO, FinalDriveRatio, finalDriveRatio, float, 3.42f);
    DEFINE_PROPERTY(PROP_VEHICLE_MAX_BRAKE_TORQUE, MaxBrakeTorque, maxBrakeTorque, float, 1500.0f);
    DEFINE_PROPERTY(PROP_VEHICLE_MAX_HAND_BRAKE_TORQUE, MaxHandBrakeTorque, maxHandBrakeTorque, float, 4000.0f);
    DEFINE_PROPERTY_REF(PROP_VEHICLE_DRIVE_TYPE, DriveType, driveType, QString, QString("rear"));
    DEFINE_PROPERTY(PROP_VEHICLE_SUSPENSION_FREQUENCY, SuspensionFrequency, suspensionFrequency, float, 1.5f);
    DEFINE_PROPERTY(PROP_VEHICLE_SUSPENSION_DAMPING, SuspensionDamping, suspensionDamping, float, 0.5f);
    DEFINE_PROPERTY_REF(PROP_VEHICLE_WHEEL, Wheel, wheel, QString, QString(""));
    DEFINE_PROPERTY_REF(PROP_VEHICLE_AXLE, Axle, axle, QString, QString("x"));
    DEFINE_PROPERTY_REF(PROP_VEHICLE_DRIVER, Driver, driver, QUuid, QUuid());
    DEFINE_PROPERTY(PROP_VEHICLE_INPUT_SEQUENCE, InputSequence, inputSequence, quint32, 0);
    DEFINE_PROPERTY(PROP_VEHICLE_THROTTLE, Throttle, throttle, float, 0.0f);
    DEFINE_PROPERTY(PROP_VEHICLE_STEERING, Steering, steering, float, 0.0f);
    DEFINE_PROPERTY(PROP_VEHICLE_BRAKE, Brake, brake, float, 0.0f);
    DEFINE_PROPERTY(PROP_VEHICLE_HAND_BRAKE, HandBrake, handBrake, float, 0.0f);
    DEFINE_PROPERTY(PROP_VEHICLE_ENGINE_RPM, EngineRPM, engineRPM, float, 0.0f);
    DEFINE_PROPERTY(PROP_VEHICLE_GEAR, Gear, gear, int, 0);
    DEFINE_PROPERTY(PROP_VEHICLE_STATE_TIME, StateTime, stateTime, quint64, 0);
};

#endif // hifi_VehiclePropertyGroup_h
