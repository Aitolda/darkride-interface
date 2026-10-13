"use strict";
//
//  vehicles.js
//  scripts/system
//
//  Sitting in and driving vehicles (PHYSICS.md round 6). Click a vehicle (an entity with vehicle.type "wheeled", or one of
//  its wheels) within reach to take a seat: the driver's seat if nobody holds it, else the nearest other seat. Seated, the
//  avatar is parented to the vehicle at the seat; the driver's W/S (throttle, brake), A/D (steering) and Space (hand
//  brake) go to the vehicle through Entities.setVehicleControls, and a chase camera follows. F stands up.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

(function() {
    var SIT_ANIMATION_URL = "https://s3-us-west-1.amazonaws.com/hifi-content/clement/production/animations/sitting_idle.fbx";
    var SIT_ANIMATION_FPS = 30;
    var SIT_ANIMATION_FIRST_FRAME = 1;
    var SIT_ANIMATION_LAST_FRAME = 350;
    var REACH = 4.0;             // m from the avatar to the vehicle's surface, to get in
    var CAMERA_BACK = 7.0;       // m behind the vehicle
    var CAMERA_UP = 2.5;         // m above it
    var CAMERA_RATE = 6.0;       // per second: how fast the camera settles behind the vehicle
    var REVERSE_SPEED = 0.5;     // m/s: pressing against the direction of travel brakes above this speed
    var STAND_KEY = "f";
    var DRIVE_KEYS = [DriveKeys.TRANSLATE_X, DriveKeys.TRANSLATE_Y, DriveKeys.TRANSLATE_Z, DriveKeys.YAW, DriveKeys.STEP_YAW,
                      DriveKeys.STEP_TRANSLATE_X, DriveKeys.STEP_TRANSLATE_Y, DriveKeys.STEP_TRANSLATE_Z];

    var seated = null;           // { vehicleID, seat, driver }
    var previousCameraMode = null;
    var cameraPosition = null;

    function parseList(text) {
        if (!text) {
            return [];
        }
        try {
            var value = JSON.parse(text);
            return Array.isArray(value) ? value : [];
        } catch (e) {
            return [];
        }
    }

    function vec3Of(value) {
        if (Array.isArray(value) && value.length >= 3) {
            return { x: value[0], y: value[1], z: value[2] };
        }
        if (value && typeof value.x === "number") {
            return { x: value.x, y: value.y, z: value.z };
        }
        return { x: 0, y: 0, z: 0 };
    }

    function rolesToOverride() {
        return MyAvatar.getAnimationRoles().filter(function(role) {
            return !(role.indexOf("right") === 0 || role.indexOf("left") === 0);
        });
    }

    // the vehicle behind a clicked entity: itself, or the chassis of a clicked wheel
    function vehicleOf(entityID) {
        var properties = Entities.getEntityProperties(entityID, ["vehicle", "parentID", "position", "rotation", "dimensions"]);
        if (!properties || !properties.vehicle) {
            return null;
        }
        if (properties.vehicle.type === "wheel" && properties.parentID && properties.parentID !== Uuid.NULL) {
            properties = Entities.getEntityProperties(properties.parentID, ["vehicle", "position", "rotation", "dimensions"]);
            properties.id = properties.id || properties.parentID;
            if (!properties || !properties.vehicle) {
                return null;
            }
            entityID = properties.id;
        }
        if (properties.vehicle.type !== "wheeled") {
            return null;
        }
        properties.id = entityID;
        return properties;
    }

    function sit(vehicle, seat) {
        var isDriver = !!seat.driver;
        seated = { vehicleID: vehicle.id, seat: seat, driver: isDriver };
        MyAvatar.collisionsEnabled = false;
        MyAvatar.hmdLeanRecenterEnabled = false;
        var roles = rolesToOverride();
        for (var i = 0; i < roles.length; i++) {
            MyAvatar.overrideRoleAnimation(roles[i], SIT_ANIMATION_URL, SIT_ANIMATION_FPS, true, SIT_ANIMATION_FIRST_FRAME, SIT_ANIMATION_LAST_FRAME);
        }
        for (i = 0; i < DRIVE_KEYS.length; i++) {
            MyAvatar.disableDriveKey(DRIVE_KEYS[i]);
        }
        // the avatar rides the vehicle: parented at the seat, facing the seat's way (0 is the vehicle's forward)
        MyAvatar.parentID = vehicle.id;
        MyAvatar.parentJointIndex = 65535;
        MyAvatar.localPosition = vec3Of(seat.position);
        MyAvatar.localOrientation = Quat.fromPitchYawRollDegrees(0, seat.yaw || 0, 0);
        if (isDriver) {
            Entities.editEntity(vehicle.id, { vehicle: { driver: MyAvatar.sessionUUID } });
        }
        previousCameraMode = Camera.mode;
        Camera.mode = "independent";
        cameraPosition = null;
        Script.update.connect(update);
        print("vehicles.js: sitting in " + (seat.name || "a seat") + " of " + vehicle.id + (isDriver ? " as the driver" : ""));
    }

    function stand() {
        if (!seated) {
            return;
        }
        var vehicleID = seated.vehicleID;
        var wasDriver = seated.driver;
        seated = null;
        Script.update.disconnect(update);
        Entities.clearVehicleControls(vehicleID);
        if (wasDriver) {
            var properties = Entities.getEntityProperties(vehicleID, ["vehicle"]);
            if (properties && properties.vehicle && properties.vehicle.driver === MyAvatar.sessionUUID) {
                Entities.editEntity(vehicleID, { vehicle: { driver: Uuid.NULL } });
            }
        }
        // step out beside the vehicle
        var position = MyAvatar.position;
        var orientation = MyAvatar.orientation;
        var right = Quat.getRight(orientation);
        MyAvatar.parentID = Uuid.NULL;
        MyAvatar.position = Vec3.sum(position, Vec3.sum(Vec3.multiply(1.5, right), { x: 0, y: 0.5, z: 0 }));
        MyAvatar.orientation = Quat.fromPitchYawRollDegrees(0, Quat.safeEulerAngles(orientation).y, 0);
        for (var i = 0; i < DRIVE_KEYS.length; i++) {
            MyAvatar.enableDriveKey(DRIVE_KEYS[i]);
        }
        var roles = rolesToOverride();
        for (i = 0; i < roles.length; i++) {
            MyAvatar.restoreRoleAnimation(roles[i]);
        }
        MyAvatar.collisionsEnabled = true;
        MyAvatar.hmdLeanRecenterEnabled = true;
        if (previousCameraMode !== null) {
            Camera.mode = previousCameraMode;
            previousCameraMode = null;
        }
        print("vehicles.js: standing up from " + vehicleID);
    }

    function update(dt) {
        if (!seated) {
            return;
        }
        var properties = Entities.getEntityProperties(seated.vehicleID, ["position", "rotation", "velocity", "vehicle"]);
        if (!properties || !properties.position) {
            stand();   // the vehicle is gone
            return;
        }
        var front = Quat.getFront(properties.rotation);
        if (seated.driver) {
            var forward = Controller.getActionValue(Controller.Actions.Forward) - Controller.getActionValue(Controller.Actions.Backward);
            var steering = -Controller.getActionValue(Controller.Actions.Yaw);
            var handBrake = Controller.getActionValue(Controller.Actions.Up) > 0.5 ? 1 : 0;
            var brake = 0;
            // pressing against the way the car is going is the brake until it has stopped; then it is reverse
            var speedAlong = Vec3.dot(properties.velocity, front);
            if (forward !== 0 && forward * speedAlong < 0 && Math.abs(speedAlong) > REVERSE_SPEED) {
                brake = Math.abs(forward);
                forward = 0;
            }
            Entities.setVehicleControls(seated.vehicleID, { throttle: forward, steering: steering, brake: brake, handBrake: handBrake });
        }
        // the chase camera, behind and above, settling at a rate so a bump does not swing it
        var wanted = Vec3.sum(properties.position, Vec3.sum(Vec3.multiply(-CAMERA_BACK, front), { x: 0, y: CAMERA_UP, z: 0 }));
        if (!cameraPosition) {
            cameraPosition = wanted;
        } else {
            cameraPosition = Vec3.mix(cameraPosition, wanted, 1 - Math.exp(-dt * CAMERA_RATE));
        }
        Camera.position = cameraPosition;
        Camera.orientation = Quat.lookAtSimple(cameraPosition, Vec3.sum(properties.position, { x: 0, y: 1, z: 0 }));
    }

    function onMousePress(entityID, event) {
        if (seated || !event.isPrimaryButton || event.isAlt || event.isControl) {
            return;
        }
        var vehicle = vehicleOf(entityID);
        if (!vehicle) {
            return;
        }
        var reach = REACH + 0.5 * Math.max(vehicle.dimensions.x, vehicle.dimensions.z);
        if (Vec3.distance(MyAvatar.position, vehicle.position) > reach) {
            return;
        }
        var seats = parseList(vehicle.vehicle.seats);
        if (seats.length === 0) {
            return;
        }
        var driverFree = !vehicle.vehicle.driver || vehicle.vehicle.driver === Uuid.NULL;
        var chosen = null;
        var best = Infinity;
        for (var i = 0; i < seats.length; i++) {
            var seat = seats[i];
            if (seat.driver && driverFree) {
                chosen = seat;
                break;
            }
            if (seat.driver) {
                continue;
            }
            var world = Vec3.sum(vehicle.position, Vec3.multiplyQbyV(vehicle.rotation, vec3Of(seat.position)));
            var distance = Vec3.distance(MyAvatar.position, world);
            if (distance < best) {
                best = distance;
                chosen = seat;
            }
        }
        if (chosen) {
            sit(vehicle, chosen);
        }
    }

    function onKeyPress(event) {
        if (seated && event.text && event.text.toLowerCase() === STAND_KEY && !event.isControl && !event.isAlt) {
            stand();
        }
    }

    Entities.mousePressOnEntity.connect(onMousePress);
    Controller.keyPressEvent.connect(onKeyPress);
    Script.scriptEnding.connect(function() {
        Entities.mousePressOnEntity.disconnect(onMousePress);
        Controller.keyPressEvent.disconnect(onKeyPress);
        stand();
    });
}());
