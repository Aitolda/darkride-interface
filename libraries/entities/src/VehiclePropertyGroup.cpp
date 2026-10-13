//
//  VehiclePropertyGroup.cpp
//  libraries/entities/src
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include "VehiclePropertyGroup.h"

#include <QtCore/QCryptographicHash>
#include <QtCore/QDataStream>

#include <OctreePacketData.h>

#include "EntityItemProperties.h"
#include "EntityItemPropertiesMacros.h"

void VehiclePropertyGroup::copyToScriptValue(const EntityPropertyFlags& desiredProperties, QScriptValue& properties,
                                             QScriptEngine* engine, bool skipDefaults,
                                             EntityItemProperties& defaultEntityProperties) const {
    COPY_GROUP_PROPERTY_TO_QSCRIPTVALUE(PROP_VEHICLE_TYPE, Vehicle, vehicle, Type, type);
    COPY_GROUP_PROPERTY_TO_QSCRIPTVALUE(PROP_VEHICLE_MASS, Vehicle, vehicle, Mass, mass);
    COPY_GROUP_PROPERTY_TO_QSCRIPTVALUE(PROP_VEHICLE_CENTER_OF_MASS, Vehicle, vehicle, CenterOfMass, centerOfMass);
    COPY_GROUP_PROPERTY_TO_QSCRIPTVALUE(PROP_VEHICLE_WHEELS, Vehicle, vehicle, Wheels, wheels);
    COPY_GROUP_PROPERTY_TO_QSCRIPTVALUE(PROP_VEHICLE_SEATS, Vehicle, vehicle, Seats, seats);
    COPY_GROUP_PROPERTY_TO_QSCRIPTVALUE(PROP_VEHICLE_MAX_STEER_ANGLE, Vehicle, vehicle, MaxSteerAngle, maxSteerAngle);
    COPY_GROUP_PROPERTY_TO_QSCRIPTVALUE(PROP_VEHICLE_MAX_TORQUE, Vehicle, vehicle, MaxTorque, maxTorque);
    COPY_GROUP_PROPERTY_TO_QSCRIPTVALUE(PROP_VEHICLE_MIN_RPM, Vehicle, vehicle, MinRPM, minRPM);
    COPY_GROUP_PROPERTY_TO_QSCRIPTVALUE(PROP_VEHICLE_MAX_RPM, Vehicle, vehicle, MaxRPM, maxRPM);
    COPY_GROUP_PROPERTY_TO_QSCRIPTVALUE(PROP_VEHICLE_GEAR_RATIOS, Vehicle, vehicle, GearRatios, gearRatios);
    COPY_GROUP_PROPERTY_TO_QSCRIPTVALUE(PROP_VEHICLE_REVERSE_GEAR_RATIO, Vehicle, vehicle, ReverseGearRatio, reverseGearRatio);
    COPY_GROUP_PROPERTY_TO_QSCRIPTVALUE(PROP_VEHICLE_FINAL_DRIVE_RATIO, Vehicle, vehicle, FinalDriveRatio, finalDriveRatio);
    COPY_GROUP_PROPERTY_TO_QSCRIPTVALUE(PROP_VEHICLE_MAX_BRAKE_TORQUE, Vehicle, vehicle, MaxBrakeTorque, maxBrakeTorque);
    COPY_GROUP_PROPERTY_TO_QSCRIPTVALUE(PROP_VEHICLE_MAX_HAND_BRAKE_TORQUE, Vehicle, vehicle, MaxHandBrakeTorque, maxHandBrakeTorque);
    COPY_GROUP_PROPERTY_TO_QSCRIPTVALUE(PROP_VEHICLE_DRIVE_TYPE, Vehicle, vehicle, DriveType, driveType);
    COPY_GROUP_PROPERTY_TO_QSCRIPTVALUE(PROP_VEHICLE_SUSPENSION_FREQUENCY, Vehicle, vehicle, SuspensionFrequency, suspensionFrequency);
    COPY_GROUP_PROPERTY_TO_QSCRIPTVALUE(PROP_VEHICLE_SUSPENSION_DAMPING, Vehicle, vehicle, SuspensionDamping, suspensionDamping);
    COPY_GROUP_PROPERTY_TO_QSCRIPTVALUE(PROP_VEHICLE_WHEEL, Vehicle, vehicle, Wheel, wheel);
    COPY_GROUP_PROPERTY_TO_QSCRIPTVALUE(PROP_VEHICLE_AXLE, Vehicle, vehicle, Axle, axle);
    COPY_GROUP_PROPERTY_TO_QSCRIPTVALUE(PROP_VEHICLE_DRIVER, Vehicle, vehicle, Driver, driver);
    COPY_GROUP_PROPERTY_TO_QSCRIPTVALUE(PROP_VEHICLE_INPUT_SEQUENCE, Vehicle, vehicle, InputSequence, inputSequence);
    COPY_GROUP_PROPERTY_TO_QSCRIPTVALUE(PROP_VEHICLE_THROTTLE, Vehicle, vehicle, Throttle, throttle);
    COPY_GROUP_PROPERTY_TO_QSCRIPTVALUE(PROP_VEHICLE_STEERING, Vehicle, vehicle, Steering, steering);
    COPY_GROUP_PROPERTY_TO_QSCRIPTVALUE(PROP_VEHICLE_BRAKE, Vehicle, vehicle, Brake, brake);
    COPY_GROUP_PROPERTY_TO_QSCRIPTVALUE(PROP_VEHICLE_HAND_BRAKE, Vehicle, vehicle, HandBrake, handBrake);
    COPY_GROUP_PROPERTY_TO_QSCRIPTVALUE(PROP_VEHICLE_ENGINE_RPM, Vehicle, vehicle, EngineRPM, engineRPM);
    COPY_GROUP_PROPERTY_TO_QSCRIPTVALUE(PROP_VEHICLE_GEAR, Vehicle, vehicle, Gear, gear);
    COPY_GROUP_PROPERTY_TO_QSCRIPTVALUE(PROP_VEHICLE_STATE_TIME, Vehicle, vehicle, StateTime, stateTime);
}

void VehiclePropertyGroup::copyFromScriptValue(const QScriptValue& object, bool& _defaultSettings) {
    COPY_GROUP_PROPERTY_FROM_QSCRIPTVALUE(vehicle, type, QString, setType);
    COPY_GROUP_PROPERTY_FROM_QSCRIPTVALUE(vehicle, mass, float, setMass);
    COPY_GROUP_PROPERTY_FROM_QSCRIPTVALUE(vehicle, centerOfMass, vec3, setCenterOfMass);
    COPY_GROUP_PROPERTY_FROM_QSCRIPTVALUE(vehicle, wheels, QString, setWheels);
    COPY_GROUP_PROPERTY_FROM_QSCRIPTVALUE(vehicle, seats, QString, setSeats);
    COPY_GROUP_PROPERTY_FROM_QSCRIPTVALUE(vehicle, maxSteerAngle, float, setMaxSteerAngle);
    COPY_GROUP_PROPERTY_FROM_QSCRIPTVALUE(vehicle, maxTorque, float, setMaxTorque);
    COPY_GROUP_PROPERTY_FROM_QSCRIPTVALUE(vehicle, minRPM, float, setMinRPM);
    COPY_GROUP_PROPERTY_FROM_QSCRIPTVALUE(vehicle, maxRPM, float, setMaxRPM);
    COPY_GROUP_PROPERTY_FROM_QSCRIPTVALUE(vehicle, gearRatios, QString, setGearRatios);
    COPY_GROUP_PROPERTY_FROM_QSCRIPTVALUE(vehicle, reverseGearRatio, float, setReverseGearRatio);
    COPY_GROUP_PROPERTY_FROM_QSCRIPTVALUE(vehicle, finalDriveRatio, float, setFinalDriveRatio);
    COPY_GROUP_PROPERTY_FROM_QSCRIPTVALUE(vehicle, maxBrakeTorque, float, setMaxBrakeTorque);
    COPY_GROUP_PROPERTY_FROM_QSCRIPTVALUE(vehicle, maxHandBrakeTorque, float, setMaxHandBrakeTorque);
    COPY_GROUP_PROPERTY_FROM_QSCRIPTVALUE(vehicle, driveType, QString, setDriveType);
    COPY_GROUP_PROPERTY_FROM_QSCRIPTVALUE(vehicle, suspensionFrequency, float, setSuspensionFrequency);
    COPY_GROUP_PROPERTY_FROM_QSCRIPTVALUE(vehicle, suspensionDamping, float, setSuspensionDamping);
    COPY_GROUP_PROPERTY_FROM_QSCRIPTVALUE(vehicle, wheel, QString, setWheel);
    COPY_GROUP_PROPERTY_FROM_QSCRIPTVALUE(vehicle, axle, QString, setAxle);
    COPY_GROUP_PROPERTY_FROM_QSCRIPTVALUE(vehicle, driver, QUuid, setDriver);
    COPY_GROUP_PROPERTY_FROM_QSCRIPTVALUE(vehicle, inputSequence, quint32, setInputSequence);
    COPY_GROUP_PROPERTY_FROM_QSCRIPTVALUE(vehicle, throttle, float, setThrottle);
    COPY_GROUP_PROPERTY_FROM_QSCRIPTVALUE(vehicle, steering, float, setSteering);
    COPY_GROUP_PROPERTY_FROM_QSCRIPTVALUE(vehicle, brake, float, setBrake);
    COPY_GROUP_PROPERTY_FROM_QSCRIPTVALUE(vehicle, handBrake, float, setHandBrake);
    COPY_GROUP_PROPERTY_FROM_QSCRIPTVALUE(vehicle, engineRPM, float, setEngineRPM);
    COPY_GROUP_PROPERTY_FROM_QSCRIPTVALUE(vehicle, gear, int, setGear);
    COPY_GROUP_PROPERTY_FROM_QSCRIPTVALUE(vehicle, stateTime, quint64, setStateTime);
}

void VehiclePropertyGroup::merge(const VehiclePropertyGroup& other) {
    COPY_PROPERTY_IF_CHANGED(type);
    COPY_PROPERTY_IF_CHANGED(mass);
    COPY_PROPERTY_IF_CHANGED(centerOfMass);
    COPY_PROPERTY_IF_CHANGED(wheels);
    COPY_PROPERTY_IF_CHANGED(seats);
    COPY_PROPERTY_IF_CHANGED(maxSteerAngle);
    COPY_PROPERTY_IF_CHANGED(maxTorque);
    COPY_PROPERTY_IF_CHANGED(minRPM);
    COPY_PROPERTY_IF_CHANGED(maxRPM);
    COPY_PROPERTY_IF_CHANGED(gearRatios);
    COPY_PROPERTY_IF_CHANGED(reverseGearRatio);
    COPY_PROPERTY_IF_CHANGED(finalDriveRatio);
    COPY_PROPERTY_IF_CHANGED(maxBrakeTorque);
    COPY_PROPERTY_IF_CHANGED(maxHandBrakeTorque);
    COPY_PROPERTY_IF_CHANGED(driveType);
    COPY_PROPERTY_IF_CHANGED(suspensionFrequency);
    COPY_PROPERTY_IF_CHANGED(suspensionDamping);
    COPY_PROPERTY_IF_CHANGED(wheel);
    COPY_PROPERTY_IF_CHANGED(axle);
    COPY_PROPERTY_IF_CHANGED(driver);
    COPY_PROPERTY_IF_CHANGED(inputSequence);
    COPY_PROPERTY_IF_CHANGED(throttle);
    COPY_PROPERTY_IF_CHANGED(steering);
    COPY_PROPERTY_IF_CHANGED(brake);
    COPY_PROPERTY_IF_CHANGED(handBrake);
    COPY_PROPERTY_IF_CHANGED(engineRPM);
    COPY_PROPERTY_IF_CHANGED(gear);
    COPY_PROPERTY_IF_CHANGED(stateTime);
}

void VehiclePropertyGroup::debugDump() const {
    qCDebug(entities) << "   VehiclePropertyGroup: ---------------------------------------------";
    qCDebug(entities) << "            _type:" << _type;
    qCDebug(entities) << "            _mass:" << _mass;
    qCDebug(entities) << "            _centerOfMass:" << _centerOfMass;
    qCDebug(entities) << "            _wheels:" << _wheels;
    qCDebug(entities) << "            _seats:" << _seats;
    qCDebug(entities) << "            _maxSteerAngle:" << _maxSteerAngle;
    qCDebug(entities) << "            _maxTorque:" << _maxTorque;
    qCDebug(entities) << "            _minRPM:" << _minRPM;
    qCDebug(entities) << "            _maxRPM:" << _maxRPM;
    qCDebug(entities) << "            _gearRatios:" << _gearRatios;
    qCDebug(entities) << "            _reverseGearRatio:" << _reverseGearRatio;
    qCDebug(entities) << "            _finalDriveRatio:" << _finalDriveRatio;
    qCDebug(entities) << "            _maxBrakeTorque:" << _maxBrakeTorque;
    qCDebug(entities) << "            _maxHandBrakeTorque:" << _maxHandBrakeTorque;
    qCDebug(entities) << "            _driveType:" << _driveType;
    qCDebug(entities) << "            _suspensionFrequency:" << _suspensionFrequency;
    qCDebug(entities) << "            _suspensionDamping:" << _suspensionDamping;
    qCDebug(entities) << "            _wheel:" << _wheel;
    qCDebug(entities) << "            _axle:" << _axle;
    qCDebug(entities) << "            _driver:" << _driver;
    qCDebug(entities) << "            _inputSequence:" << _inputSequence;
    qCDebug(entities) << "            _throttle:" << _throttle;
    qCDebug(entities) << "            _steering:" << _steering;
    qCDebug(entities) << "            _brake:" << _brake;
    qCDebug(entities) << "            _handBrake:" << _handBrake;
    qCDebug(entities) << "            _engineRPM:" << _engineRPM;
    qCDebug(entities) << "            _gear:" << _gear;
    qCDebug(entities) << "            _stateTime:" << _stateTime;
}

void VehiclePropertyGroup::listChangedProperties(QList<QString>& out) {
    if (typeChanged()) {
        out << "vehicle-type";
    }
    if (massChanged()) {
        out << "vehicle-mass";
    }
    if (centerOfMassChanged()) {
        out << "vehicle-centerOfMass";
    }
    if (wheelsChanged()) {
        out << "vehicle-wheels";
    }
    if (seatsChanged()) {
        out << "vehicle-seats";
    }
    if (maxSteerAngleChanged()) {
        out << "vehicle-maxSteerAngle";
    }
    if (maxTorqueChanged()) {
        out << "vehicle-maxTorque";
    }
    if (minRPMChanged()) {
        out << "vehicle-minRPM";
    }
    if (maxRPMChanged()) {
        out << "vehicle-maxRPM";
    }
    if (gearRatiosChanged()) {
        out << "vehicle-gearRatios";
    }
    if (reverseGearRatioChanged()) {
        out << "vehicle-reverseGearRatio";
    }
    if (finalDriveRatioChanged()) {
        out << "vehicle-finalDriveRatio";
    }
    if (maxBrakeTorqueChanged()) {
        out << "vehicle-maxBrakeTorque";
    }
    if (maxHandBrakeTorqueChanged()) {
        out << "vehicle-maxHandBrakeTorque";
    }
    if (driveTypeChanged()) {
        out << "vehicle-driveType";
    }
    if (suspensionFrequencyChanged()) {
        out << "vehicle-suspensionFrequency";
    }
    if (suspensionDampingChanged()) {
        out << "vehicle-suspensionDamping";
    }
    if (wheelChanged()) {
        out << "vehicle-wheel";
    }
    if (axleChanged()) {
        out << "vehicle-axle";
    }
    if (driverChanged()) {
        out << "vehicle-driver";
    }
    if (inputSequenceChanged()) {
        out << "vehicle-inputSequence";
    }
    if (throttleChanged()) {
        out << "vehicle-throttle";
    }
    if (steeringChanged()) {
        out << "vehicle-steering";
    }
    if (brakeChanged()) {
        out << "vehicle-brake";
    }
    if (handBrakeChanged()) {
        out << "vehicle-handBrake";
    }
    if (engineRPMChanged()) {
        out << "vehicle-engineRPM";
    }
    if (gearChanged()) {
        out << "vehicle-gear";
    }
    if (stateTimeChanged()) {
        out << "vehicle-stateTime";
    }
}

bool VehiclePropertyGroup::appendToEditPacket(OctreePacketData* packetData,
                                              EntityPropertyFlags& requestedProperties,
                                              EntityPropertyFlags& propertyFlags,
                                              EntityPropertyFlags& propertiesDidntFit,
                                              int& propertyCount,
                                              OctreeElement::AppendState& appendState) const {

    bool successPropertyFits = true;

    APPEND_ENTITY_PROPERTY(PROP_VEHICLE_TYPE, getType());
    APPEND_ENTITY_PROPERTY(PROP_VEHICLE_MASS, getMass());
    APPEND_ENTITY_PROPERTY(PROP_VEHICLE_CENTER_OF_MASS, getCenterOfMass());
    APPEND_ENTITY_PROPERTY(PROP_VEHICLE_WHEELS, getWheels());
    APPEND_ENTITY_PROPERTY(PROP_VEHICLE_SEATS, getSeats());
    APPEND_ENTITY_PROPERTY(PROP_VEHICLE_MAX_STEER_ANGLE, getMaxSteerAngle());
    APPEND_ENTITY_PROPERTY(PROP_VEHICLE_MAX_TORQUE, getMaxTorque());
    APPEND_ENTITY_PROPERTY(PROP_VEHICLE_MIN_RPM, getMinRPM());
    APPEND_ENTITY_PROPERTY(PROP_VEHICLE_MAX_RPM, getMaxRPM());
    APPEND_ENTITY_PROPERTY(PROP_VEHICLE_GEAR_RATIOS, getGearRatios());
    APPEND_ENTITY_PROPERTY(PROP_VEHICLE_REVERSE_GEAR_RATIO, getReverseGearRatio());
    APPEND_ENTITY_PROPERTY(PROP_VEHICLE_FINAL_DRIVE_RATIO, getFinalDriveRatio());
    APPEND_ENTITY_PROPERTY(PROP_VEHICLE_MAX_BRAKE_TORQUE, getMaxBrakeTorque());
    APPEND_ENTITY_PROPERTY(PROP_VEHICLE_MAX_HAND_BRAKE_TORQUE, getMaxHandBrakeTorque());
    APPEND_ENTITY_PROPERTY(PROP_VEHICLE_DRIVE_TYPE, getDriveType());
    APPEND_ENTITY_PROPERTY(PROP_VEHICLE_SUSPENSION_FREQUENCY, getSuspensionFrequency());
    APPEND_ENTITY_PROPERTY(PROP_VEHICLE_SUSPENSION_DAMPING, getSuspensionDamping());
    APPEND_ENTITY_PROPERTY(PROP_VEHICLE_WHEEL, getWheel());
    APPEND_ENTITY_PROPERTY(PROP_VEHICLE_AXLE, getAxle());
    APPEND_ENTITY_PROPERTY(PROP_VEHICLE_DRIVER, getDriver());
    APPEND_ENTITY_PROPERTY(PROP_VEHICLE_INPUT_SEQUENCE, getInputSequence());
    APPEND_ENTITY_PROPERTY(PROP_VEHICLE_THROTTLE, getThrottle());
    APPEND_ENTITY_PROPERTY(PROP_VEHICLE_STEERING, getSteering());
    APPEND_ENTITY_PROPERTY(PROP_VEHICLE_BRAKE, getBrake());
    APPEND_ENTITY_PROPERTY(PROP_VEHICLE_HAND_BRAKE, getHandBrake());
    APPEND_ENTITY_PROPERTY(PROP_VEHICLE_ENGINE_RPM, getEngineRPM());
    APPEND_ENTITY_PROPERTY(PROP_VEHICLE_GEAR, getGear());
    APPEND_ENTITY_PROPERTY(PROP_VEHICLE_STATE_TIME, getStateTime());

    return true;
}

bool VehiclePropertyGroup::decodeFromEditPacket(EntityPropertyFlags& propertyFlags,
                                                const unsigned char*& dataAt , int& processedBytes) {

    int bytesRead = 0;
    bool overwriteLocalData = true;
    bool somethingChanged = false;

    READ_ENTITY_PROPERTY(PROP_VEHICLE_TYPE, QString, setType);
    READ_ENTITY_PROPERTY(PROP_VEHICLE_MASS, float, setMass);
    READ_ENTITY_PROPERTY(PROP_VEHICLE_CENTER_OF_MASS, glm::vec3, setCenterOfMass);
    READ_ENTITY_PROPERTY(PROP_VEHICLE_WHEELS, QString, setWheels);
    READ_ENTITY_PROPERTY(PROP_VEHICLE_SEATS, QString, setSeats);
    READ_ENTITY_PROPERTY(PROP_VEHICLE_MAX_STEER_ANGLE, float, setMaxSteerAngle);
    READ_ENTITY_PROPERTY(PROP_VEHICLE_MAX_TORQUE, float, setMaxTorque);
    READ_ENTITY_PROPERTY(PROP_VEHICLE_MIN_RPM, float, setMinRPM);
    READ_ENTITY_PROPERTY(PROP_VEHICLE_MAX_RPM, float, setMaxRPM);
    READ_ENTITY_PROPERTY(PROP_VEHICLE_GEAR_RATIOS, QString, setGearRatios);
    READ_ENTITY_PROPERTY(PROP_VEHICLE_REVERSE_GEAR_RATIO, float, setReverseGearRatio);
    READ_ENTITY_PROPERTY(PROP_VEHICLE_FINAL_DRIVE_RATIO, float, setFinalDriveRatio);
    READ_ENTITY_PROPERTY(PROP_VEHICLE_MAX_BRAKE_TORQUE, float, setMaxBrakeTorque);
    READ_ENTITY_PROPERTY(PROP_VEHICLE_MAX_HAND_BRAKE_TORQUE, float, setMaxHandBrakeTorque);
    READ_ENTITY_PROPERTY(PROP_VEHICLE_DRIVE_TYPE, QString, setDriveType);
    READ_ENTITY_PROPERTY(PROP_VEHICLE_SUSPENSION_FREQUENCY, float, setSuspensionFrequency);
    READ_ENTITY_PROPERTY(PROP_VEHICLE_SUSPENSION_DAMPING, float, setSuspensionDamping);
    READ_ENTITY_PROPERTY(PROP_VEHICLE_WHEEL, QString, setWheel);
    READ_ENTITY_PROPERTY(PROP_VEHICLE_AXLE, QString, setAxle);
    READ_ENTITY_PROPERTY(PROP_VEHICLE_DRIVER, QUuid, setDriver);
    READ_ENTITY_PROPERTY(PROP_VEHICLE_INPUT_SEQUENCE, quint32, setInputSequence);
    READ_ENTITY_PROPERTY(PROP_VEHICLE_THROTTLE, float, setThrottle);
    READ_ENTITY_PROPERTY(PROP_VEHICLE_STEERING, float, setSteering);
    READ_ENTITY_PROPERTY(PROP_VEHICLE_BRAKE, float, setBrake);
    READ_ENTITY_PROPERTY(PROP_VEHICLE_HAND_BRAKE, float, setHandBrake);
    READ_ENTITY_PROPERTY(PROP_VEHICLE_ENGINE_RPM, float, setEngineRPM);
    READ_ENTITY_PROPERTY(PROP_VEHICLE_GEAR, int, setGear);
    READ_ENTITY_PROPERTY(PROP_VEHICLE_STATE_TIME, quint64, setStateTime);

    DECODE_GROUP_PROPERTY_HAS_CHANGED(PROP_VEHICLE_TYPE, Type);
    DECODE_GROUP_PROPERTY_HAS_CHANGED(PROP_VEHICLE_MASS, Mass);
    DECODE_GROUP_PROPERTY_HAS_CHANGED(PROP_VEHICLE_CENTER_OF_MASS, CenterOfMass);
    DECODE_GROUP_PROPERTY_HAS_CHANGED(PROP_VEHICLE_WHEELS, Wheels);
    DECODE_GROUP_PROPERTY_HAS_CHANGED(PROP_VEHICLE_SEATS, Seats);
    DECODE_GROUP_PROPERTY_HAS_CHANGED(PROP_VEHICLE_MAX_STEER_ANGLE, MaxSteerAngle);
    DECODE_GROUP_PROPERTY_HAS_CHANGED(PROP_VEHICLE_MAX_TORQUE, MaxTorque);
    DECODE_GROUP_PROPERTY_HAS_CHANGED(PROP_VEHICLE_MIN_RPM, MinRPM);
    DECODE_GROUP_PROPERTY_HAS_CHANGED(PROP_VEHICLE_MAX_RPM, MaxRPM);
    DECODE_GROUP_PROPERTY_HAS_CHANGED(PROP_VEHICLE_GEAR_RATIOS, GearRatios);
    DECODE_GROUP_PROPERTY_HAS_CHANGED(PROP_VEHICLE_REVERSE_GEAR_RATIO, ReverseGearRatio);
    DECODE_GROUP_PROPERTY_HAS_CHANGED(PROP_VEHICLE_FINAL_DRIVE_RATIO, FinalDriveRatio);
    DECODE_GROUP_PROPERTY_HAS_CHANGED(PROP_VEHICLE_MAX_BRAKE_TORQUE, MaxBrakeTorque);
    DECODE_GROUP_PROPERTY_HAS_CHANGED(PROP_VEHICLE_MAX_HAND_BRAKE_TORQUE, MaxHandBrakeTorque);
    DECODE_GROUP_PROPERTY_HAS_CHANGED(PROP_VEHICLE_DRIVE_TYPE, DriveType);
    DECODE_GROUP_PROPERTY_HAS_CHANGED(PROP_VEHICLE_SUSPENSION_FREQUENCY, SuspensionFrequency);
    DECODE_GROUP_PROPERTY_HAS_CHANGED(PROP_VEHICLE_SUSPENSION_DAMPING, SuspensionDamping);
    DECODE_GROUP_PROPERTY_HAS_CHANGED(PROP_VEHICLE_WHEEL, Wheel);
    DECODE_GROUP_PROPERTY_HAS_CHANGED(PROP_VEHICLE_AXLE, Axle);
    DECODE_GROUP_PROPERTY_HAS_CHANGED(PROP_VEHICLE_DRIVER, Driver);
    DECODE_GROUP_PROPERTY_HAS_CHANGED(PROP_VEHICLE_INPUT_SEQUENCE, InputSequence);
    DECODE_GROUP_PROPERTY_HAS_CHANGED(PROP_VEHICLE_THROTTLE, Throttle);
    DECODE_GROUP_PROPERTY_HAS_CHANGED(PROP_VEHICLE_STEERING, Steering);
    DECODE_GROUP_PROPERTY_HAS_CHANGED(PROP_VEHICLE_BRAKE, Brake);
    DECODE_GROUP_PROPERTY_HAS_CHANGED(PROP_VEHICLE_HAND_BRAKE, HandBrake);
    DECODE_GROUP_PROPERTY_HAS_CHANGED(PROP_VEHICLE_ENGINE_RPM, EngineRPM);
    DECODE_GROUP_PROPERTY_HAS_CHANGED(PROP_VEHICLE_GEAR, Gear);
    DECODE_GROUP_PROPERTY_HAS_CHANGED(PROP_VEHICLE_STATE_TIME, StateTime);

    processedBytes += bytesRead;

    Q_UNUSED(somethingChanged);

    return true;
}

void VehiclePropertyGroup::markAllChanged() {
    _typeChanged = true;
    _massChanged = true;
    _centerOfMassChanged = true;
    _wheelsChanged = true;
    _seatsChanged = true;
    _maxSteerAngleChanged = true;
    _maxTorqueChanged = true;
    _minRPMChanged = true;
    _maxRPMChanged = true;
    _gearRatiosChanged = true;
    _reverseGearRatioChanged = true;
    _finalDriveRatioChanged = true;
    _maxBrakeTorqueChanged = true;
    _maxHandBrakeTorqueChanged = true;
    _driveTypeChanged = true;
    _suspensionFrequencyChanged = true;
    _suspensionDampingChanged = true;
    _wheelChanged = true;
    _axleChanged = true;
    _driverChanged = true;
    _inputSequenceChanged = true;
    _throttleChanged = true;
    _steeringChanged = true;
    _brakeChanged = true;
    _handBrakeChanged = true;
    _engineRPMChanged = true;
    _gearChanged = true;
    _stateTimeChanged = true;
}

EntityPropertyFlags VehiclePropertyGroup::getChangedProperties() const {
    EntityPropertyFlags changedProperties;

    CHECK_PROPERTY_CHANGE(PROP_VEHICLE_TYPE, type);
    CHECK_PROPERTY_CHANGE(PROP_VEHICLE_MASS, mass);
    CHECK_PROPERTY_CHANGE(PROP_VEHICLE_CENTER_OF_MASS, centerOfMass);
    CHECK_PROPERTY_CHANGE(PROP_VEHICLE_WHEELS, wheels);
    CHECK_PROPERTY_CHANGE(PROP_VEHICLE_SEATS, seats);
    CHECK_PROPERTY_CHANGE(PROP_VEHICLE_MAX_STEER_ANGLE, maxSteerAngle);
    CHECK_PROPERTY_CHANGE(PROP_VEHICLE_MAX_TORQUE, maxTorque);
    CHECK_PROPERTY_CHANGE(PROP_VEHICLE_MIN_RPM, minRPM);
    CHECK_PROPERTY_CHANGE(PROP_VEHICLE_MAX_RPM, maxRPM);
    CHECK_PROPERTY_CHANGE(PROP_VEHICLE_GEAR_RATIOS, gearRatios);
    CHECK_PROPERTY_CHANGE(PROP_VEHICLE_REVERSE_GEAR_RATIO, reverseGearRatio);
    CHECK_PROPERTY_CHANGE(PROP_VEHICLE_FINAL_DRIVE_RATIO, finalDriveRatio);
    CHECK_PROPERTY_CHANGE(PROP_VEHICLE_MAX_BRAKE_TORQUE, maxBrakeTorque);
    CHECK_PROPERTY_CHANGE(PROP_VEHICLE_MAX_HAND_BRAKE_TORQUE, maxHandBrakeTorque);
    CHECK_PROPERTY_CHANGE(PROP_VEHICLE_DRIVE_TYPE, driveType);
    CHECK_PROPERTY_CHANGE(PROP_VEHICLE_SUSPENSION_FREQUENCY, suspensionFrequency);
    CHECK_PROPERTY_CHANGE(PROP_VEHICLE_SUSPENSION_DAMPING, suspensionDamping);
    CHECK_PROPERTY_CHANGE(PROP_VEHICLE_WHEEL, wheel);
    CHECK_PROPERTY_CHANGE(PROP_VEHICLE_AXLE, axle);
    CHECK_PROPERTY_CHANGE(PROP_VEHICLE_DRIVER, driver);
    CHECK_PROPERTY_CHANGE(PROP_VEHICLE_INPUT_SEQUENCE, inputSequence);
    CHECK_PROPERTY_CHANGE(PROP_VEHICLE_THROTTLE, throttle);
    CHECK_PROPERTY_CHANGE(PROP_VEHICLE_STEERING, steering);
    CHECK_PROPERTY_CHANGE(PROP_VEHICLE_BRAKE, brake);
    CHECK_PROPERTY_CHANGE(PROP_VEHICLE_HAND_BRAKE, handBrake);
    CHECK_PROPERTY_CHANGE(PROP_VEHICLE_ENGINE_RPM, engineRPM);
    CHECK_PROPERTY_CHANGE(PROP_VEHICLE_GEAR, gear);
    CHECK_PROPERTY_CHANGE(PROP_VEHICLE_STATE_TIME, stateTime);

    return changedProperties;
}

void VehiclePropertyGroup::getProperties(EntityItemProperties& properties) const {
    COPY_ENTITY_GROUP_PROPERTY_TO_PROPERTIES(Vehicle, Type, getType);
    COPY_ENTITY_GROUP_PROPERTY_TO_PROPERTIES(Vehicle, Mass, getMass);
    COPY_ENTITY_GROUP_PROPERTY_TO_PROPERTIES(Vehicle, CenterOfMass, getCenterOfMass);
    COPY_ENTITY_GROUP_PROPERTY_TO_PROPERTIES(Vehicle, Wheels, getWheels);
    COPY_ENTITY_GROUP_PROPERTY_TO_PROPERTIES(Vehicle, Seats, getSeats);
    COPY_ENTITY_GROUP_PROPERTY_TO_PROPERTIES(Vehicle, MaxSteerAngle, getMaxSteerAngle);
    COPY_ENTITY_GROUP_PROPERTY_TO_PROPERTIES(Vehicle, MaxTorque, getMaxTorque);
    COPY_ENTITY_GROUP_PROPERTY_TO_PROPERTIES(Vehicle, MinRPM, getMinRPM);
    COPY_ENTITY_GROUP_PROPERTY_TO_PROPERTIES(Vehicle, MaxRPM, getMaxRPM);
    COPY_ENTITY_GROUP_PROPERTY_TO_PROPERTIES(Vehicle, GearRatios, getGearRatios);
    COPY_ENTITY_GROUP_PROPERTY_TO_PROPERTIES(Vehicle, ReverseGearRatio, getReverseGearRatio);
    COPY_ENTITY_GROUP_PROPERTY_TO_PROPERTIES(Vehicle, FinalDriveRatio, getFinalDriveRatio);
    COPY_ENTITY_GROUP_PROPERTY_TO_PROPERTIES(Vehicle, MaxBrakeTorque, getMaxBrakeTorque);
    COPY_ENTITY_GROUP_PROPERTY_TO_PROPERTIES(Vehicle, MaxHandBrakeTorque, getMaxHandBrakeTorque);
    COPY_ENTITY_GROUP_PROPERTY_TO_PROPERTIES(Vehicle, DriveType, getDriveType);
    COPY_ENTITY_GROUP_PROPERTY_TO_PROPERTIES(Vehicle, SuspensionFrequency, getSuspensionFrequency);
    COPY_ENTITY_GROUP_PROPERTY_TO_PROPERTIES(Vehicle, SuspensionDamping, getSuspensionDamping);
    COPY_ENTITY_GROUP_PROPERTY_TO_PROPERTIES(Vehicle, Wheel, getWheel);
    COPY_ENTITY_GROUP_PROPERTY_TO_PROPERTIES(Vehicle, Axle, getAxle);
    COPY_ENTITY_GROUP_PROPERTY_TO_PROPERTIES(Vehicle, Driver, getDriver);
    COPY_ENTITY_GROUP_PROPERTY_TO_PROPERTIES(Vehicle, InputSequence, getInputSequence);
    COPY_ENTITY_GROUP_PROPERTY_TO_PROPERTIES(Vehicle, Throttle, getThrottle);
    COPY_ENTITY_GROUP_PROPERTY_TO_PROPERTIES(Vehicle, Steering, getSteering);
    COPY_ENTITY_GROUP_PROPERTY_TO_PROPERTIES(Vehicle, Brake, getBrake);
    COPY_ENTITY_GROUP_PROPERTY_TO_PROPERTIES(Vehicle, HandBrake, getHandBrake);
    COPY_ENTITY_GROUP_PROPERTY_TO_PROPERTIES(Vehicle, EngineRPM, getEngineRPM);
    COPY_ENTITY_GROUP_PROPERTY_TO_PROPERTIES(Vehicle, Gear, getGear);
    COPY_ENTITY_GROUP_PROPERTY_TO_PROPERTIES(Vehicle, StateTime, getStateTime);
}

bool VehiclePropertyGroup::setProperties(const EntityItemProperties& properties) {
    bool somethingChanged = false;

    SET_ENTITY_GROUP_PROPERTY_FROM_PROPERTIES(Vehicle, Type, type, setType);
    SET_ENTITY_GROUP_PROPERTY_FROM_PROPERTIES(Vehicle, Mass, mass, setMass);
    SET_ENTITY_GROUP_PROPERTY_FROM_PROPERTIES(Vehicle, CenterOfMass, centerOfMass, setCenterOfMass);
    SET_ENTITY_GROUP_PROPERTY_FROM_PROPERTIES(Vehicle, Wheels, wheels, setWheels);
    SET_ENTITY_GROUP_PROPERTY_FROM_PROPERTIES(Vehicle, Seats, seats, setSeats);
    SET_ENTITY_GROUP_PROPERTY_FROM_PROPERTIES(Vehicle, MaxSteerAngle, maxSteerAngle, setMaxSteerAngle);
    SET_ENTITY_GROUP_PROPERTY_FROM_PROPERTIES(Vehicle, MaxTorque, maxTorque, setMaxTorque);
    SET_ENTITY_GROUP_PROPERTY_FROM_PROPERTIES(Vehicle, MinRPM, minRPM, setMinRPM);
    SET_ENTITY_GROUP_PROPERTY_FROM_PROPERTIES(Vehicle, MaxRPM, maxRPM, setMaxRPM);
    SET_ENTITY_GROUP_PROPERTY_FROM_PROPERTIES(Vehicle, GearRatios, gearRatios, setGearRatios);
    SET_ENTITY_GROUP_PROPERTY_FROM_PROPERTIES(Vehicle, ReverseGearRatio, reverseGearRatio, setReverseGearRatio);
    SET_ENTITY_GROUP_PROPERTY_FROM_PROPERTIES(Vehicle, FinalDriveRatio, finalDriveRatio, setFinalDriveRatio);
    SET_ENTITY_GROUP_PROPERTY_FROM_PROPERTIES(Vehicle, MaxBrakeTorque, maxBrakeTorque, setMaxBrakeTorque);
    SET_ENTITY_GROUP_PROPERTY_FROM_PROPERTIES(Vehicle, MaxHandBrakeTorque, maxHandBrakeTorque, setMaxHandBrakeTorque);
    SET_ENTITY_GROUP_PROPERTY_FROM_PROPERTIES(Vehicle, DriveType, driveType, setDriveType);
    SET_ENTITY_GROUP_PROPERTY_FROM_PROPERTIES(Vehicle, SuspensionFrequency, suspensionFrequency, setSuspensionFrequency);
    SET_ENTITY_GROUP_PROPERTY_FROM_PROPERTIES(Vehicle, SuspensionDamping, suspensionDamping, setSuspensionDamping);
    SET_ENTITY_GROUP_PROPERTY_FROM_PROPERTIES(Vehicle, Wheel, wheel, setWheel);
    SET_ENTITY_GROUP_PROPERTY_FROM_PROPERTIES(Vehicle, Axle, axle, setAxle);
    SET_ENTITY_GROUP_PROPERTY_FROM_PROPERTIES(Vehicle, Driver, driver, setDriver);
    SET_ENTITY_GROUP_PROPERTY_FROM_PROPERTIES(Vehicle, InputSequence, inputSequence, setInputSequence);
    SET_ENTITY_GROUP_PROPERTY_FROM_PROPERTIES(Vehicle, Throttle, throttle, setThrottle);
    SET_ENTITY_GROUP_PROPERTY_FROM_PROPERTIES(Vehicle, Steering, steering, setSteering);
    SET_ENTITY_GROUP_PROPERTY_FROM_PROPERTIES(Vehicle, Brake, brake, setBrake);
    SET_ENTITY_GROUP_PROPERTY_FROM_PROPERTIES(Vehicle, HandBrake, handBrake, setHandBrake);
    SET_ENTITY_GROUP_PROPERTY_FROM_PROPERTIES(Vehicle, EngineRPM, engineRPM, setEngineRPM);
    SET_ENTITY_GROUP_PROPERTY_FROM_PROPERTIES(Vehicle, Gear, gear, setGear);
    SET_ENTITY_GROUP_PROPERTY_FROM_PROPERTIES(Vehicle, StateTime, stateTime, setStateTime);

    return somethingChanged;
}

EntityPropertyFlags VehiclePropertyGroup::getEntityProperties(EncodeBitstreamParams& params) const {
    EntityPropertyFlags requestedProperties;

    requestedProperties += PROP_VEHICLE_TYPE;
    requestedProperties += PROP_VEHICLE_MASS;
    requestedProperties += PROP_VEHICLE_CENTER_OF_MASS;
    requestedProperties += PROP_VEHICLE_WHEELS;
    requestedProperties += PROP_VEHICLE_SEATS;
    requestedProperties += PROP_VEHICLE_MAX_STEER_ANGLE;
    requestedProperties += PROP_VEHICLE_MAX_TORQUE;
    requestedProperties += PROP_VEHICLE_MIN_RPM;
    requestedProperties += PROP_VEHICLE_MAX_RPM;
    requestedProperties += PROP_VEHICLE_GEAR_RATIOS;
    requestedProperties += PROP_VEHICLE_REVERSE_GEAR_RATIO;
    requestedProperties += PROP_VEHICLE_FINAL_DRIVE_RATIO;
    requestedProperties += PROP_VEHICLE_MAX_BRAKE_TORQUE;
    requestedProperties += PROP_VEHICLE_MAX_HAND_BRAKE_TORQUE;
    requestedProperties += PROP_VEHICLE_DRIVE_TYPE;
    requestedProperties += PROP_VEHICLE_SUSPENSION_FREQUENCY;
    requestedProperties += PROP_VEHICLE_SUSPENSION_DAMPING;
    requestedProperties += PROP_VEHICLE_WHEEL;
    requestedProperties += PROP_VEHICLE_AXLE;
    requestedProperties += PROP_VEHICLE_DRIVER;
    requestedProperties += PROP_VEHICLE_INPUT_SEQUENCE;
    requestedProperties += PROP_VEHICLE_THROTTLE;
    requestedProperties += PROP_VEHICLE_STEERING;
    requestedProperties += PROP_VEHICLE_BRAKE;
    requestedProperties += PROP_VEHICLE_HAND_BRAKE;
    requestedProperties += PROP_VEHICLE_ENGINE_RPM;
    requestedProperties += PROP_VEHICLE_GEAR;
    requestedProperties += PROP_VEHICLE_STATE_TIME;

    return requestedProperties;
}

void VehiclePropertyGroup::appendSubclassData(OctreePacketData* packetData, EncodeBitstreamParams& params,
                                              EntityTreeElementExtraEncodeDataPointer entityTreeElementExtraEncodeData,
                                              EntityPropertyFlags& requestedProperties,
                                              EntityPropertyFlags& propertyFlags,
                                              EntityPropertyFlags& propertiesDidntFit,
                                              int& propertyCount,
                                              OctreeElement::AppendState& appendState) const {

    bool successPropertyFits = true;

    APPEND_ENTITY_PROPERTY(PROP_VEHICLE_TYPE, getType());
    APPEND_ENTITY_PROPERTY(PROP_VEHICLE_MASS, getMass());
    APPEND_ENTITY_PROPERTY(PROP_VEHICLE_CENTER_OF_MASS, getCenterOfMass());
    APPEND_ENTITY_PROPERTY(PROP_VEHICLE_WHEELS, getWheels());
    APPEND_ENTITY_PROPERTY(PROP_VEHICLE_SEATS, getSeats());
    APPEND_ENTITY_PROPERTY(PROP_VEHICLE_MAX_STEER_ANGLE, getMaxSteerAngle());
    APPEND_ENTITY_PROPERTY(PROP_VEHICLE_MAX_TORQUE, getMaxTorque());
    APPEND_ENTITY_PROPERTY(PROP_VEHICLE_MIN_RPM, getMinRPM());
    APPEND_ENTITY_PROPERTY(PROP_VEHICLE_MAX_RPM, getMaxRPM());
    APPEND_ENTITY_PROPERTY(PROP_VEHICLE_GEAR_RATIOS, getGearRatios());
    APPEND_ENTITY_PROPERTY(PROP_VEHICLE_REVERSE_GEAR_RATIO, getReverseGearRatio());
    APPEND_ENTITY_PROPERTY(PROP_VEHICLE_FINAL_DRIVE_RATIO, getFinalDriveRatio());
    APPEND_ENTITY_PROPERTY(PROP_VEHICLE_MAX_BRAKE_TORQUE, getMaxBrakeTorque());
    APPEND_ENTITY_PROPERTY(PROP_VEHICLE_MAX_HAND_BRAKE_TORQUE, getMaxHandBrakeTorque());
    APPEND_ENTITY_PROPERTY(PROP_VEHICLE_DRIVE_TYPE, getDriveType());
    APPEND_ENTITY_PROPERTY(PROP_VEHICLE_SUSPENSION_FREQUENCY, getSuspensionFrequency());
    APPEND_ENTITY_PROPERTY(PROP_VEHICLE_SUSPENSION_DAMPING, getSuspensionDamping());
    APPEND_ENTITY_PROPERTY(PROP_VEHICLE_WHEEL, getWheel());
    APPEND_ENTITY_PROPERTY(PROP_VEHICLE_AXLE, getAxle());
    APPEND_ENTITY_PROPERTY(PROP_VEHICLE_DRIVER, getDriver());
    APPEND_ENTITY_PROPERTY(PROP_VEHICLE_INPUT_SEQUENCE, getInputSequence());
    APPEND_ENTITY_PROPERTY(PROP_VEHICLE_THROTTLE, getThrottle());
    APPEND_ENTITY_PROPERTY(PROP_VEHICLE_STEERING, getSteering());
    APPEND_ENTITY_PROPERTY(PROP_VEHICLE_BRAKE, getBrake());
    APPEND_ENTITY_PROPERTY(PROP_VEHICLE_HAND_BRAKE, getHandBrake());
    APPEND_ENTITY_PROPERTY(PROP_VEHICLE_ENGINE_RPM, getEngineRPM());
    APPEND_ENTITY_PROPERTY(PROP_VEHICLE_GEAR, getGear());
    APPEND_ENTITY_PROPERTY(PROP_VEHICLE_STATE_TIME, getStateTime());
}

int VehiclePropertyGroup::readEntitySubclassDataFromBuffer(const unsigned char* data, int bytesLeftToRead,
                                                           ReadBitstreamToTreeParams& args,
                                                           EntityPropertyFlags& propertyFlags, bool overwriteLocalData,
                                                           bool& somethingChanged) {

    int bytesRead = 0;
    const unsigned char* dataAt = data;

    READ_ENTITY_PROPERTY(PROP_VEHICLE_TYPE, QString, setType);
    READ_ENTITY_PROPERTY(PROP_VEHICLE_MASS, float, setMass);
    READ_ENTITY_PROPERTY(PROP_VEHICLE_CENTER_OF_MASS, glm::vec3, setCenterOfMass);
    READ_ENTITY_PROPERTY(PROP_VEHICLE_WHEELS, QString, setWheels);
    READ_ENTITY_PROPERTY(PROP_VEHICLE_SEATS, QString, setSeats);
    READ_ENTITY_PROPERTY(PROP_VEHICLE_MAX_STEER_ANGLE, float, setMaxSteerAngle);
    READ_ENTITY_PROPERTY(PROP_VEHICLE_MAX_TORQUE, float, setMaxTorque);
    READ_ENTITY_PROPERTY(PROP_VEHICLE_MIN_RPM, float, setMinRPM);
    READ_ENTITY_PROPERTY(PROP_VEHICLE_MAX_RPM, float, setMaxRPM);
    READ_ENTITY_PROPERTY(PROP_VEHICLE_GEAR_RATIOS, QString, setGearRatios);
    READ_ENTITY_PROPERTY(PROP_VEHICLE_REVERSE_GEAR_RATIO, float, setReverseGearRatio);
    READ_ENTITY_PROPERTY(PROP_VEHICLE_FINAL_DRIVE_RATIO, float, setFinalDriveRatio);
    READ_ENTITY_PROPERTY(PROP_VEHICLE_MAX_BRAKE_TORQUE, float, setMaxBrakeTorque);
    READ_ENTITY_PROPERTY(PROP_VEHICLE_MAX_HAND_BRAKE_TORQUE, float, setMaxHandBrakeTorque);
    READ_ENTITY_PROPERTY(PROP_VEHICLE_DRIVE_TYPE, QString, setDriveType);
    READ_ENTITY_PROPERTY(PROP_VEHICLE_SUSPENSION_FREQUENCY, float, setSuspensionFrequency);
    READ_ENTITY_PROPERTY(PROP_VEHICLE_SUSPENSION_DAMPING, float, setSuspensionDamping);
    READ_ENTITY_PROPERTY(PROP_VEHICLE_WHEEL, QString, setWheel);
    READ_ENTITY_PROPERTY(PROP_VEHICLE_AXLE, QString, setAxle);
    READ_ENTITY_PROPERTY(PROP_VEHICLE_DRIVER, QUuid, setDriver);
    READ_ENTITY_PROPERTY(PROP_VEHICLE_INPUT_SEQUENCE, quint32, setInputSequence);
    READ_ENTITY_PROPERTY(PROP_VEHICLE_THROTTLE, float, setThrottle);
    READ_ENTITY_PROPERTY(PROP_VEHICLE_STEERING, float, setSteering);
    READ_ENTITY_PROPERTY(PROP_VEHICLE_BRAKE, float, setBrake);
    READ_ENTITY_PROPERTY(PROP_VEHICLE_HAND_BRAKE, float, setHandBrake);
    READ_ENTITY_PROPERTY(PROP_VEHICLE_ENGINE_RPM, float, setEngineRPM);
    READ_ENTITY_PROPERTY(PROP_VEHICLE_GEAR, int, setGear);
    READ_ENTITY_PROPERTY(PROP_VEHICLE_STATE_TIME, quint64, setStateTime);

    return bytesRead;
}

// The definition's version: a hash of every property that shapes the vehicle, none of the state. The simulation keeps
// its Jolt vehicle while this stands and rebuilds it when it changes (a wheel edited in the Inspector).
uint64_t VehiclePropertyGroup::definitionHash() const {
    QByteArray bytes;
    QDataStream stream(&bytes, QIODevice::WriteOnly);
    stream.setFloatingPointPrecision(QDataStream::SinglePrecision);
    stream << _type << _mass << _centerOfMass.x << _centerOfMass.y << _centerOfMass.z << _wheels << _seats
           << _maxSteerAngle << _maxTorque << _minRPM << _maxRPM << _gearRatios << _reverseGearRatio << _finalDriveRatio
           << _maxBrakeTorque << _maxHandBrakeTorque << _driveType << _suspensionFrequency << _suspensionDamping
           << _wheel << _axle;
    QByteArray digest = QCryptographicHash::hash(bytes, QCryptographicHash::Md5);
    uint64_t hash = 0;
    memcpy(&hash, digest.constData(), sizeof(hash));
    return hash == 0 ? 1 : hash;
}
