//
//  airship.js
//  tests-manual/physics-baseline
//
//  Round 7 of PHYSICS.md: the airship. One client is the pilot: it makes the terrain (landrover.js, the file make-terrain.py
//  wrote, served over HTTP), waits for the ground, makes the ship on the pad (airship-rig.js), takes the helm as vehicles.js
//  would (it stands at the helm on the deck, a member of the ship's frame by where it stands; no parenting by hand) and
//  flies a scripted course through Entities.setVehicleControls: moored, ballast dropped, lift-off on the vertical thruster,
//  a climb to cruising height, a cruise, a turn each way under full rudder, the nose up and down with the elevator, the
//  way back to the pad, a descent with gas vented, a landing; then it leaves the helm. A second client is the crew: it
//  walks aboard from the pad, drops a ball on the deck, and through the flight stands and walks fore and aft on the deck
//  by turns; after the landing it moves the ship with one explicit edit of its position (the editor's teleport, which the
//  physics server adopts) and watches everyone come along. Every script frame both record the hull's pose and velocities,
//  the live telemetry (altitude, air density, buoyancy, ballast, fill, thrust, airspeed), the authority's state as
//  published (the input sequence, the applied controls, ballast, fill), the vehicle statistics, their own membership of
//  the frame and stored local position, and the crew the ball's local position and the pilot's stored local position as
//  the mixer delivers it. The summary (lines starting with PHYSICS-AIRSHIP) is per phase; the samples land in
//  physics-airship.json.
//
//  The role and the terrain's URL come from a global, AIRSHIP_CONFIG; run-airship.sh writes one script per role: a line
//  that sets it, then landrover.js, airship-rig.js and this file. Serverless (the pilot alone, the fallback) or on a domain
//  with the physics server flying the ship.
//
//  Distributed under the Apache License, Version 2.0.
//

(function () {
    var config = (typeof AIRSHIP_CONFIG !== "undefined" && AIRSHIP_CONFIG) ? AIRSHIP_CONFIG : {};
    var ROLE = config.role || "pilot";
    var TERRAIN_URL = config.terrainURL || "";
    var LIFETIME = config.lifetime || 600;
    var TIMEOUT = config.timeout || 260;
    var BOOT = (config.boot !== undefined) ? config.boot : 8;
    var CRUISE_HEIGHT = 35;                  // m above the pad's ground
    var RETURN_RADIUS = 60;                  // m from the pad: close enough to brake and come down (the braking takes about 50 m)
    var GUARD_RADIUS = 80;                   // m from the pad: beyond it the autopilot heads home whatever the phase (the terrain ends at 96 m)
    var WALK_SPEED = 1.0;                    // m/s, the crew's scripted motor
    var DRIVE_KEYS = [DriveKeys.TRANSLATE_X, DriveKeys.TRANSLATE_Y, DriveKeys.TRANSLATE_Z, DriveKeys.YAW, DriveKeys.STEP_YAW,
                      DriveKeys.STEP_TRANSLATE_X, DriveKeys.STEP_TRANSLATE_Y, DriveKeys.STEP_TRANSLATE_Z];

    // the pilot's course: each phase holds its controls, or an autopilot setting, until its time is up or its `until` says so
    var COURSE = [
        { name: "moored", seconds: 3, controls: {} },
        { name: "ballast", seconds: 4, controls: { trim: 1 } },
        { name: "lift-off", seconds: 4, controls: { lift: 0.5 } },
        { name: "climb", seconds: 25, auto: { throttle: 0.2, height: CRUISE_HEIGHT, heading: "hold" }, until: function (s) { return s.height > CRUISE_HEIGHT - 5; } },
        { name: "cruise", seconds: 6, auto: { throttle: 0.4, height: CRUISE_HEIGHT, heading: "hold" } },
        { name: "turn-right", seconds: 12, auto: { throttle: 0.3, height: CRUISE_HEIGHT, steering: 1 } },
        { name: "turn-left", seconds: 24, auto: { throttle: 0.3, height: CRUISE_HEIGHT, steering: -1 } },
        { name: "nose-up", seconds: 4, auto: { throttle: 0.3, pitch: 1 } },
        { name: "nose-down", seconds: 4, auto: { throttle: 0.3, pitch: -1 } },
        { name: "return", seconds: 90, auto: { throttle: "home", height: CRUISE_HEIGHT, heading: "pad" }, until: function (s) { return s.padDistance < RETURN_RADIUS; } },
        { name: "brake", seconds: 25, auto: { throttle: -0.6, height: CRUISE_HEIGHT, heading: "hold" }, until: function (s) { return s.speedAlong < 0.5; } },
        { name: "vent", seconds: 6, auto: { throttle: 0, trim: -1, height: CRUISE_HEIGHT } },
        { name: "descend", seconds: 90, auto: { throttle: 0, descend: true }, until: function (s) { return s.aboveGround < 2.0 && Math.abs(s.climbRate) < 0.05; } },
        { name: "landed", seconds: 5, controls: { lift: -0.3 } },
        { name: "teleport", seconds: 8, auto: { throttle: 0, hover: true } },
        { name: "done", seconds: 2, controls: {} }
    ];

    var terrain = null;
    var hull = null;
    var ball = null;
    var atHelm = false;
    var mode = "boot";
    var modeT0 = 0;
    var phaseIndex = -1;
    var phase = "";
    var published = "";
    var holdHeading = null;
    var samples = [];
    var events = [];
    var crossings = [];
    var lastParent = null;
    var lastControls = {};
    var t0 = Date.now() / 1000;
    var finished = false;
    var bootTried = -10;
    var teleportDone = false;
    var padGround = 0;
    var crewLeg = { t: 0, walking: false, direction: -1 };

    function now() { return Date.now() / 1000 - t0; }
    function log(s) { print("PHYSICS-AIRSHIP " + s); }
    function setMode(m) { mode = m; modeT0 = now(); events.push({ t: now(), mode: m }); log("mode " + m + " at " + now().toFixed(2) + " s"); }
    function setPhase(p) {
        if (p === phase) { return; }
        phase = p;
        events.push({ t: now(), phase: p });
        log("phase " + p + " at " + now().toFixed(2) + " s");
        if (ROLE === "pilot" && hull && phase !== published) { published = phase; Entities.editEntity(hull, { description: phase }); }
    }
    function vec3Of(value) {
        if (Array.isArray(value) && value.length >= 3) { return { x: value[0], y: value[1], z: value[2] }; }
        if (value && typeof value.x === "number") { return value; }
        return { x: 0, y: 0, z: 0 };
    }
    function facingRange(rows) {
        var min = null, max = null;
        rows.forEach(function (r) {
            if (typeof r.myLocalYaw !== "number") { return; }
            var yaw = r.myLocalYaw <= -179.5 ? r.myLocalYaw + 360 : r.myLocalYaw;   // -180 and 180 are one facing
            if (min === null || yaw < min) { min = yaw; }
            if (max === null || yaw > max) { max = yaw; }
        });
        return min === null ? null : { min: min, max: max };
    }
    function fmtV(v) { return v ? "(" + v.x.toFixed(2) + ", " + v.y.toFixed(2) + ", " + v.z.toFixed(2) + ")" : "none"; }
    function pad() { return { x: LandRover.TERRAIN_CENTER.x, z: LandRover.TERRAIN_CENTER.z }; }
    function wrap(d) { while (d > 180) { d -= 360; } while (d < -180) { d += 360; } return d; }
    function headingOf(rotation) { var f = Quat.getFront(rotation); return 180 / Math.PI * Math.atan2(f.x, -f.z); }
    function clamp1(v) { return Math.max(-1, Math.min(1, v)); }

    // ---- the pilot at the helm, as vehicles.js puts them there ----
    function takeHelm() {
        var p = Entities.getEntityProperties(hull, ["position", "rotation"]);
        var seat = Airship.HELM;
        var helm = Vec3.sum(p.position, Vec3.multiplyQbyV(p.rotation, vec3Of(seat.position)));
        DRIVE_KEYS.forEach(function (key) { MyAvatar.disableDriveKey(key); });
        MyAvatar.position = Vec3.sum(helm, { x: 0, y: 1.0, z: 0 });
        MyAvatar.orientation = p.rotation;
        Entities.editEntity(hull, { vehicle: { driver: MyAvatar.sessionUUID } });
        atHelm = true;
        log("at the helm: world " + fmtV(MyAvatar.position) + ", parent " + (MyAvatar.parentID == hull ? "the ship" : MyAvatar.parentID));
    }
    function leaveHelm() {
        if (!atHelm) { return; }
        atHelm = false;
        Entities.clearVehicleControls(hull);
        var p = Entities.getEntityProperties(hull, ["vehicle"]);
        if (p && p.vehicle && p.vehicle.driver === MyAvatar.sessionUUID) { Entities.editEntity(hull, { vehicle: { driver: Uuid.NULL } }); }
        DRIVE_KEYS.forEach(function (key) { MyAvatar.enableDriveKey(key); });
        log("left the helm at " + fmtV(MyAvatar.position) + ", parent " + (MyAvatar.parentID == hull ? "the ship" : MyAvatar.parentID));
    }

    // ---- the world ----
    function standBesidePad(side) {
        var c = pad();
        MyAvatar.position = { x: c.x + (side || 6), y: LandRover.groundHeight(c.x + (side || 6), c.z) + 1.2, z: c.z };
        MyAvatar.orientation = Quat.fromPitchYawRollDegrees(0, 90, 0);
    }
    function groundReady() {
        var c = pad();
        var expected = LandRover.groundHeight(c.x, c.z);
        var ray = { origin: { x: c.x, y: expected + 30, z: c.z }, direction: { x: 0, y: -1, z: 0 } };
        var hit = Entities.findRayIntersection(ray, true, [terrain], [], false, true);
        if (hit && hit.intersects && hit.entityID == terrain && Math.abs(hit.intersection.y - expected) < 0.5) { return hit.intersection.y; }
        return null;
    }
    function hullState() {
        return Entities.getEntityProperties(hull, ["position", "rotation", "velocity", "angularVelocity", "vehicle", "description", "simulationOwner"]);
    }
    function findShip() {
        var found = Entities.findEntitiesByName("airship-hull", LandRover.TERRAIN_CENTER, 2 * LandRover.TERRAIN_SIZE, false);
        var best = null, bestAge = Infinity;
        found.forEach(function (id) {
            var age = Entities.getEntityProperties(id, ["age"]).age;
            if (age !== undefined && age < bestAge && age < now() + 1) { bestAge = age; best = id; }
        });
        return best;
    }

    // ---- sampling ----
    var lastOwnerLine = "";
    var pilotAvatarID = null;
    var pilotLookupLogged = false;
    function flight(c) {
        // what the autopilot and the summary read off the hull
        var ground = LandRover.groundHeight(c.position.x, c.position.z);
        var dx = c.position.x - pad().x, dz = c.position.z - pad().z;
        return {
            height: c.position.y - padGround, aboveGround: c.position.y - ground, climbRate: c.velocity.y,
            heading: headingOf(c.rotation), padDistance: Math.sqrt(dx * dx + dz * dz),
            padBearing: 180 / Math.PI * Math.atan2(-dx, dz),   // the compass direction from the ship to the pad
            speed: Vec3.length(c.velocity), speedAlong: Vec3.dot(c.velocity, Quat.getFront(c.rotation))
        };
    }
    function sample(dt) {
        var c = hullState();
        if (!c || !c.position) { return false; }
        // the nose's pitch and the deck's bank from the hull's axes (Euler angles misbehave about a yaw of 90 degrees)
        var frontY = Quat.getFront(c.rotation).y, rightY = Quat.getRight(c.rotation).y;
        var euler = { x: 180 / Math.PI * Math.asin(Math.max(-1, Math.min(1, frontY))), z: 180 / Math.PI * Math.asin(Math.max(-1, Math.min(1, rightY))) };
        var stats = Entities.getVehicleStats();
        var v = c.vehicle || {};
        var f = flight(c);
        var parented = MyAvatar.parentID == hull;
        if (lastParent !== null && parented !== lastParent) {
            crossings.push({ t: now(), phase: phase, aboard: parented, world: MyAvatar.position });
            log((parented ? "boarded" : "left") + " the ship at " + now().toFixed(2) + " s, world " + fmtV(MyAvatar.position));
        }
        lastParent = parented;
        var row = {
            index: samples.length, t: now(), dt: dt, mode: mode, phase: ROLE === "pilot" ? phase : (c.description || ""),
            pos: c.position, rot: c.rotation, vel: c.velocity, angVel: c.angularVelocity,
            height: f.height, aboveGround: f.aboveGround, climbRate: f.climbRate, heading: f.heading, padDistance: f.padDistance,
            speed: f.speed, speedAlong: f.speedAlong, pitch: euler.x, roll: euler.z, upY: Quat.getUp(c.rotation).y,
            controls: lastControls,
            state: { sequence: v.inputSequence, throttle: v.throttle, steering: v.steering, pitch: v.pitch, lift: v.lift, trim: v.trim,
                     ballast: v.ballast, fill: v.fill, stateTime: v.stateTime, driver: v.driver },
            stats: { corrections: stats.corrections, replaySteps: stats.replaySteps, maxError: stats.maxError, inputsSent: stats.inputsSent,
                     inputsReceived: stats.inputsReceived, inputsDropped: stats.inputsDropped, inputsStarved: stats.inputsStarved, vehicles: stats.vehicles },
            live: stats.live && stats.live.airship ? { altitude: stats.live.altitude, airDensity: stats.live.airDensity, buoyancy: stats.live.buoyancy,
                    ballastWeight: stats.live.ballastWeight, thrust: stats.live.thrust, verticalThrust: stats.live.verticalThrust,
                    airspeed: stats.live.airspeed, sideslip: stats.live.sideslip, incidence: stats.live.incidence, ballast: stats.live.ballast,
                    fill: stats.live.fill } : null,
            parented: parented, myPos: MyAvatar.position, myLocal: parented ? MyAvatar.localPosition : null,
            myLocalVel: parented ? MyAvatar.localVelocity : null,
            myLocalYaw: parented ? Quat.safeEulerAngles(MyAvatar.localOrientation).y : null,   // the facing on the deck, degrees
            atHelm: atHelm, walking: crewLeg.walking
        };
        if (ROLE === "crew") {
            if (ball) {
                var b = Entities.getEntityProperties(ball, ["localPosition", "parentID"]);
                row.ballLocal = (b && b.parentID == hull) ? b.localPosition : null;
            }
            // the driver as the state names it (a property comes to a script as a UUID, not a string: Uuid.isNull, not
            // a comparison with Uuid.NULL; and AvatarList.getAvatar of the null UUID is this client's own avatar)
            if (v.driver && !Uuid.isNull(v.driver) && !Uuid.isEqual(v.driver, MyAvatar.sessionUUID)) { pilotAvatarID = v.driver; }
            if (pilotAvatarID) {
                var a = AvatarList.getAvatar(pilotAvatarID);
                if (!pilotLookupLogged && a && a.sessionUUID) {
                    pilotLookupLogged = true;
                    log("the pilot " + pilotAvatarID + " looked up as " + a.sessionUUID + " (me " + MyAvatar.sessionUUID + "), at " +
                        JSON.stringify(a.position) + " local " + JSON.stringify(a.localPosition) + "; I am at " + JSON.stringify(MyAvatar.position) +
                        "; avatars " + JSON.stringify(AvatarList.getAvatarIdentifiers()));
                }
                if (a && a.sessionUUID) {
                    row.pilotAboard = a.parentID == hull;
                    row.pilotLocal = a.parentID == hull ? a.localPosition : null;
                }
            }
        }
        samples.push(row);
        if (samples.length % 60 === 0) {
            var owner = c.simulationOwner ? (c.simulationOwner.ownerID + " at priority " + c.simulationOwner.priority) : "none";
            var me = c.simulationOwner && c.simulationOwner.ownerID == MyAvatar.sessionUUID ? " (me)" : "";
            var line = "owner " + owner + me;
            if (line !== lastOwnerLine) {
                lastOwnerLine = line;
                events.push({ t: now(), owner: owner });
                log(line + " at " + now().toFixed(2) + " s; state sequence " + v.inputSequence + ", ballast " + (v.ballast || 0).toFixed(1) +
                    " kg, fill " + (v.fill || 0).toFixed(3) + ", driver " + v.driver);
            }
        }
        return true;
    }

    // ---- the pilot's course and autopilot ----
    var controlsLogged = false;
    function autopilot(step, s, tt) {
        var c = { throttle: 0, steering: 0, pitch: 0, lift: 0, trim: 0 };
        if (step.controls) {
            Object.keys(step.controls).forEach(function (k) { c[k] = step.controls[k]; });
            return c;
        }
        var a = step.auto;
        c.throttle = a.throttle || 0;
        if (a.throttle === "home") {
            // slow while the nose is off the pad (an airship's turning circle is its speed over its turn rate)
            c.throttle = Math.abs(wrap(s.padBearing - s.heading)) < 25 ? 0.5 : 0.15;
        }
        c.trim = a.trim || 0;
        c.pitch = a.pitch || 0;
        if (a.steering !== undefined) {
            c.steering = a.steering;
        } else if (a.heading === "hold" || a.heading === "pad") {
            var target = a.heading === "pad" ? s.padBearing : holdHeading;
            if (target !== null) { c.steering = clamp1(wrap(target - s.heading) / 30); }
        }
        if (s.padDistance > GUARD_RADIUS) {
            // too far out whatever the phase: home
            c.steering = clamp1(wrap(s.padBearing - s.heading) / 30);
        }
        if (a.hover) {
            c.lift = clamp1(-1.0 * s.climbRate);
        } else if (a.descend) {
            // down at a metre a second until the last metres, then gently, and not into the ground
            var wanted = s.aboveGround > 8 ? -1.5 : -0.3;
            c.lift = clamp1((wanted - s.climbRate) * 0.8);
            if (s.aboveGround < 0.6) { c.lift = -1; }   // settle onto the ground and stay
        } else if (a.height !== undefined) {
            c.lift = clamp1(0.1 * (a.height - s.height) - 0.5 * s.climbRate);
        }
        return c;
    }
    function applyPhase(tt) {
        var step = COURSE[phaseIndex];
        var hs = hullState();
        if (!hs || !hs.position) { return; }
        var s = flight(hs);
        var c = autopilot(step, s, tt);
        lastControls = c;
        var accepted = Entities.setVehicleControls(hull, c);
        if (!controlsLogged) {
            controlsLogged = true;
            log("controls " + (accepted ? "accepted" : "REFUSED (not a vehicle here)") + "; parent " + (MyAvatar.parentID == hull ? "the ship" : MyAvatar.parentID) +
                ", local " + fmtV(MyAvatar.localPosition) + ", world " + fmtV(MyAvatar.position));
        }
        if (step.until && step.until(s)) {
            log(step.name + " reached its mark at " + tt.toFixed(1) + " s: height " + s.height.toFixed(1) + " m, " + s.padDistance.toFixed(0) + " m from the pad");
            nextPhase();
        }
    }
    function nextPhase() {
        phaseIndex++;
        if (phaseIndex >= COURSE.length) { return false; }
        var step = COURSE[phaseIndex];
        if (step.auto && step.auto.heading === "hold") {
            var hs = hullState();
            holdHeading = hs && hs.rotation ? headingOf(hs.rotation) : holdHeading;
        }
        setPhase(step.name);
        modeT0 = now();
        return true;
    }

    function drivePilot() {
        var tt = now() - modeT0;
        if (mode === "boot") {
            if (tt > BOOT && tt > bootTried + 1) {
                bootTried = tt;
                var removed = Airship.deleteAll(LandRover.TERRAIN_CENTER, 2 * LandRover.TERRAIN_SIZE);
                if (removed > 0) { log("deleted " + removed + " leftover airship entities of an earlier run"); }
                terrain = LandRover.createTerrain(TERRAIN_URL, LIFETIME);
                if (!terrain || terrain === Uuid.NULL) {
                    terrain = null;
                    if (bootTried < BOOT + 2) { log("no entity server yet at " + tt.toFixed(1) + " s; trying again each second"); }
                    return;
                }
                log("terrain " + terrain + ", file " + TERRAIN_URL + " (at " + tt.toFixed(1) + " s)");
                setMode("terrain");
            }
        } else if (mode === "terrain") {
            var y = groundReady();
            if (y !== null) {
                padGround = y;
                log("ground ready after " + tt.toFixed(1) + " s: y " + y.toFixed(3));
                standBesidePad(6);
                setMode("ground-settle");
            } else if (tt > 60) { finish("no ground"); return; }
        } else if (mode === "ground-settle") {
            if (tt > 2) {
                var c = pad();
                var made = Airship.create(c.x, c.z, 0, LIFETIME);
                hull = made.hull;
                var hp = Entities.getEntityProperties(hull, ["position", "vehicle", "dynamic", "frame"]);
                log("airship " + hull + " at " + fmtV(hp.position) + ", dynamic " + hp.dynamic + ", frame " + hp.frame + ", type " +
                    (hp.vehicle && hp.vehicle.type) + ", mass " + (hp.vehicle && hp.vehicle.mass) + " kg, ballast " + (hp.vehicle && hp.vehicle.ballast) +
                    " kg, envelope " + (hp.vehicle && hp.vehicle.envelopeVolume) + " m3, " + made.parts.length + " parts");
                setMode("settle");
            }
        } else if (mode === "settle") {
            if (tt > 4) {
                var s = Entities.getEntityProperties(hull, ["position", "velocity"]);
                var ground = LandRover.groundHeight(s.position.x, s.position.z);
                log("settled: hull y " + s.position.y.toFixed(3) + " = ground + " + (s.position.y - ground).toFixed(3) + " (expected about " +
                    Airship.DECK_CENTER_ABOVE_GROUND.toFixed(2) + "), speed " + Vec3.length(s.velocity).toFixed(3));
                if (s.position.y < ground - 1) { finish("fell through"); return; }
                if (s.position.y > ground + 5) { log("the ship is already airborne: too light on the ground"); }
                takeHelm();
                setMode("helm");
            }
        } else if (mode === "helm") {
            if (tt > 3) { setMode("course"); nextPhase(); }
        } else if (mode === "course") {
            if (tt >= COURSE[phaseIndex].seconds) {
                if (!nextPhase()) { setMode("leave"); leaveHelm(); return; }
                tt = 0;
            }
            if (phase === "done") { setMode("leave"); leaveHelm(); return; }
            applyPhase(tt);
        } else if (mode === "leave") {
            if (tt > 2) { finish("done"); }
        }
    }

    // ---- the crew ----
    function walk(direction) {
        // along the deck, facing the bow throughout, in the ship's own axes: forward to the bow (direction -1), backward
        // to the stern (+1). The scripted motor's "avatar" frame has +z forward (MyAvatar::updateMotors turns it by 180
        // degrees); a crew that faced the stern would be turned back by a look-at camera, the default, which turns the
        // avatar to face where the camera looks once it is more than 135 degrees off. The facing is set at every leg, so
        // a stray turn of the avatar (a hand on the mouse) does not send the crew into a rail.
        crewLeg.walking = true;
        crewLeg.direction = direction;
        var hp = Entities.getEntityProperties(hull, ["rotation"]);
        if (hp && hp.rotation) { MyAvatar.orientation = hp.rotation; }
        MyAvatar.motorReferenceFrame = "avatar";
        MyAvatar.motorTimescale = 0.2;
        MyAvatar.motorVelocity = { x: 0, y: 0, z: direction < 0 ? WALK_SPEED : -WALK_SPEED };
    }
    function stopWalking() {
        crewLeg.walking = false;
        MyAvatar.motorVelocity = Vec3.ZERO;
    }
    function dropBall() {
        ball = Entities.addEntity({
            type: "Sphere", name: "airship-ball", parentID: hull, lifetime: LIFETIME,
            localPosition: { x: 0.8, y: 1.2, z: 2.5 }, dimensions: { x: 0.3, y: 0.3, z: 0.3 },
            color: { red: 220, green: 40, blue: 40 }, dynamic: true, collisionless: false,
            gravity: { x: 0, y: -9.8, z: 0 }, damping: 0.1, angularDamping: 0.1, friction: 0.5, restitution: 0.2,
            grab: { grabbable: false }
        });
        log("dropped a ball on the deck: " + ball);
    }
    function teleportShip() {
        var p = Entities.getEntityProperties(hull, ["position"]);
        var to = Vec3.sum(p.position, { x: 30, y: 20, z: 0 });
        var before = { ship: p.position, me: MyAvatar.position, local: MyAvatar.localPosition };
        Entities.editEntity(hull, { position: to });
        teleportDone = true;
        events.push({ t: now(), teleport: { from: p.position, to: to, meBefore: before.me, localBefore: before.local } });
        log("teleported the ship by an edit from " + fmtV(p.position) + " to " + fmtV(to) + "; I was at " + fmtV(before.me) + ", local " + fmtV(before.local));
        Script.setTimeout(function () {
            var q = Entities.getEntityProperties(hull, ["position"]);
            log("3 s after the teleport: the ship is at " + fmtV(q.position) + " (" + Vec3.distance(q.position, to).toFixed(2) + " m from where it was put), I am at " +
                fmtV(MyAvatar.position) + ", local " + fmtV(MyAvatar.localPosition) + ", parent " + (MyAvatar.parentID == hull ? "the ship" : MyAvatar.parentID));
            events.push({ t: now(), afterTeleport: { ship: q.position, me: MyAvatar.position, local: MyAvatar.localPosition, aboard: MyAvatar.parentID == hull } });
        }, 3000);
    }
    function driveCrew() {
        var tt = now() - modeT0;
        if (mode === "boot") {
            if (tt > 2) { standBesidePad(6); setMode("find"); }
        } else if (mode === "find") {
            if (tt > 3) { standBesidePad(6); modeT0 = now(); }
            var id = findShip();
            if (id) {
                hull = id;
                var hp = Entities.getEntityProperties(hull, ["position", "rotation"]);
                log("found the airship " + hull + " at " + fmtV(hp.position) + " after " + tt.toFixed(1) + " s");
                padGround = LandRover.groundHeight(pad().x, pad().z);
                // beside the deck's starboard rail, facing the bow... no: facing the deck, to walk aboard across it
                var side = Vec3.sum(hp.position, Vec3.multiplyQbyV(hp.rotation, { x: 3.2, y: 0, z: 3.0 }));
                MyAvatar.position = { x: side.x, y: LandRover.groundHeight(side.x, side.z) + 1.0, z: side.z };
                MyAvatar.orientation = Quat.multiply(hp.rotation, Quat.fromPitchYawRollDegrees(0, 90, 0));   // facing -x of the ship: across the deck
                setMode("board");
            } else if (tt > 90) { finish("no airship"); }
        } else if (mode === "board") {
            // the rails stand at the deck's edges: the crew steps over the rail's gap at the stern... the rails have no gap,
            // so the crew climbs aboard where the rail ends at the stern corner, then walks to the middle
            if (tt < 0.5) {
                var hp2 = Entities.getEntityProperties(hull, ["position", "rotation"]);
                var corner = Vec3.sum(hp2.position, Vec3.multiplyQbyV(hp2.rotation, { x: 0.0, y: 1.2, z: 5.5 }));
                MyAvatar.position = corner;
                MyAvatar.orientation = hp2.rotation;
            } else if (tt > 2 && MyAvatar.parentID == hull) {
                log("aboard: local " + fmtV(MyAvatar.localPosition) + " (stored), world " + fmtV(MyAvatar.position));
                dropBall();
                setMode("ride");
                crewLeg.t = now();
            } else if (tt > 10) {
                log("not aboard after " + tt.toFixed(0) + " s: parent " + MyAvatar.parentID + ", world " + fmtV(MyAvatar.position));
                finish("not aboard");
            }
        } else if (mode === "ride") {
            var c = Entities.getEntityProperties(hull, ["description"]);
            var p = c ? (c.description || "") : "";
            if (p !== phase) {
                setPhase(p);
                if (p === "teleport" && !teleportDone) { stopWalking(); Script.setTimeout(teleportShip, 1500); }
            }
            // by turns: 5 s standing, 5 s walking forward, 5 s standing, 5 s walking back, while the ship flies
            var flying = ["climb", "cruise", "turn-right", "turn-left", "nose-up", "nose-down", "return", "descend"].indexOf(p) >= 0;
            var leg = Math.floor((now() - crewLeg.t) / 5) % 4;
            if (flying && leg === 1 && !crewLeg.walking) { walk(-1); }
            else if (flying && leg === 3 && !crewLeg.walking) { walk(1); }
            else if ((!flying || leg === 0 || leg === 2) && crewLeg.walking) { stopWalking(); }
            // the rails keep the crew aboard; should it be walking into a rail, turn back early
            if (crewLeg.walking && MyAvatar.parentID == hull) {
                var lz = MyAvatar.localPosition.z;
                if ((crewLeg.direction < 0 && lz < -4.0) || (crewLeg.direction > 0 && lz > 4.5)) { stopWalking(); }
            }
            if (p === "done" || tt > 240) { stopWalking(); setMode("leave"); }
        } else if (mode === "leave") {
            if (tt > 2) { finish("done"); }
        }
    }

    // ---- analysis ----
    function steps(rows, key) {
        var lengths = [];
        for (var i = 1; i < rows.length; i++) {
            if (rows[i].index === rows[i - 1].index + 1 && rows[i][key] && rows[i - 1][key]) {
                lengths.push(Vec3.length(Vec3.subtract(rows[i][key], rows[i - 1][key])));
            }
        }
        if (lengths.length < 2) { return null; }
        var mean = lengths.reduce(function (a, b) { return a + b; }, 0) / lengths.length;
        var dev = 0, max = 0, over = 0;
        lengths.forEach(function (l) { dev += (l - mean) * (l - mean); if (l > max) { max = l; } if (l > 2 * mean && l > 0.02) { over++; } });
        return { n: lengths.length, meanMm: 1000 * mean, devMm: 1000 * Math.sqrt(dev / lengths.length), maxMm: 1000 * max, overTwice: over };
    }
    function jitter(rows, key) {
        var out = { n: 0, rmsMm: 0, maxMm: 0, driftMm: 0 };
        var first = null, last = null, sum = 0;
        for (var i = 1; i < rows.length; i++) {
            if (rows[i].index === rows[i - 1].index + 1 && rows[i][key] && rows[i - 1][key]) {
                var d = Vec3.length(Vec3.subtract(rows[i][key], rows[i - 1][key]));
                sum += d * d; out.n++; if (d > out.maxMm) { out.maxMm = d; }
            }
            if (rows[i][key]) { if (!first) { first = rows[i][key]; } last = rows[i][key]; }
        }
        if (out.n === 0) { return null; }
        out.rmsMm = 1000 * Math.sqrt(sum / out.n); out.maxMm *= 1000;
        out.driftMm = first && last ? 1000 * Vec3.distance(first, last) : 0;
        return out;
    }
    function phaseStats(name, rows) {
        if (rows.length < 3) { return null; }
        var first = rows[0], last = rows[rows.length - 1];
        var hMin = Infinity, hMax = -Infinity, climbMax = -Infinity, climbMin = Infinity, speedMax = 0, heading = 0, distance = 0;
        var pitchMax = 0, rollMax = 0, airspeedMax = 0, aboard = 0, ballOn = 0, ballN = 0, ballX = 0, ballZ = 0, ballYMin = Infinity, ballYMax = -Infinity;
        var standing = [], walking = [], pilotAboard = 0, pilotN = 0;
        for (var i = 0; i < rows.length; i++) {
            var r = rows[i];
            if (r.height < hMin) { hMin = r.height; } if (r.height > hMax) { hMax = r.height; }
            if (r.climbRate > climbMax) { climbMax = r.climbRate; } if (r.climbRate < climbMin) { climbMin = r.climbRate; }
            if (r.speed > speedMax) { speedMax = r.speed; }
            if (Math.abs(r.pitch) > pitchMax) { pitchMax = Math.abs(r.pitch); }
            if (Math.abs(r.roll) > rollMax) { rollMax = Math.abs(r.roll); }
            if (r.live && r.live.airspeed > airspeedMax) { airspeedMax = r.live.airspeed; }
            if (i > 0 && r.index === rows[i - 1].index + 1) { heading += wrap(r.heading - rows[i - 1].heading); distance += Vec3.distance(rows[i - 1].pos, r.pos); }
            if (r.parented) { aboard++; }
            (r.walking ? walking : standing).push(r);
            if (r.ballLocal) {
                ballN++;
                var on = Math.abs(r.ballLocal.x) < 2.0 && Math.abs(r.ballLocal.z) < 7.0 && r.ballLocal.y > 0.1 && r.ballLocal.y < 1.5;
                if (on) { ballOn++; }
                ballX = Math.max(ballX, Math.abs(r.ballLocal.x)); ballZ = Math.max(ballZ, Math.abs(r.ballLocal.z));
                ballYMin = Math.min(ballYMin, r.ballLocal.y); ballYMax = Math.max(ballYMax, r.ballLocal.y);
            }
            if (r.pilotAboard !== undefined) { pilotN++; if (r.pilotAboard) { pilotAboard++; } }
        }
        var s0 = first.stats || {}, s1 = last.stats || {};
        var l1 = last.live || {};
        return {
            phase: name, frames: rows.length, seconds: last.t - first.t, aboardFrames: aboard,
            heightMin: hMin, heightMax: hMax, heightEnd: last.height, climbMax: climbMax, climbMin: climbMin, speedMax: speedMax,
            headingDeg: heading, distanceM: distance, pitchMaxDeg: pitchMax, rollMaxDeg: rollMax, airspeedMax: airspeedMax,
            padDistanceEnd: last.padDistance,
            buoyancy: l1.buoyancy, ballast: l1.ballast, fill: l1.fill, airDensity: l1.airDensity,
            stateBallast: last.state.ballast, stateFill: last.state.fill,
            myLocalStanding: jitter(standing, "myLocal"), myLocalWalking: jitter(walking, "myLocal"), walkingFrames: walking.length,
            facing: facingRange(rows),   // the crew's heading on the deck: a camera that stays in the world turns it as the ship turns
            pilotLocal: jitter(rows, "pilotLocal"), pilotAboardFrames: pilotAboard, pilotFrames: pilotN,
            ball: ballN ? { frames: ballN, onDeck: ballOn, maxX: ballX, maxZ: ballZ, yMin: ballYMin, yMax: ballYMax } : null,
            hullStep: steps(rows, "pos"),
            corrections: (s1.corrections || 0) - (s0.corrections || 0), replaySteps: (s1.replaySteps || 0) - (s0.replaySteps || 0),
            maxError: s1.maxError, inputsSent: (s1.inputsSent || 0) - (s0.inputsSent || 0),
            sequenceFrom: first.state.sequence, sequenceTo: last.state.sequence, stateUpdates: countStateUpdates(rows)
        };
    }
    function countStateUpdates(rows) {
        var n = 0;
        for (var i = 1; i < rows.length; i++) { if (rows[i].state.stateTime !== rows[i - 1].state.stateTime) { n++; } }
        return n;
    }
    function fmtSteps(j) {
        return j ? ("mean " + j.meanMm.toFixed(1) + " mm, rms dev " + j.devMm.toFixed(1) + " mm, max " + j.maxMm.toFixed(1) + " mm, " +
            j.overTwice + " over twice the mean of " + j.n) : "n/a";
    }
    function fmtJitter(j) {
        return j ? ("rms " + j.rmsMm.toFixed(2) + " mm, max " + j.maxMm.toFixed(2) + " mm, drift " + j.driftMm.toFixed(1) + " mm over " + j.n) : "n/a";
    }

    function finish(reason) {
        if (finished) { return; }
        finished = true;
        Script.update.disconnect(onUpdate);
        if (ROLE === "pilot") { leaveHelm(); } else { stopWalking(); }
        var summary = [];
        var seen = [];
        samples.forEach(function (r) { if (r.phase && seen.indexOf(r.phase) < 0) { seen.push(r.phase); } });
        seen.forEach(function (name) {
            var s = phaseStats(name, samples.filter(function (r) { return r.phase === name; }));
            if (s) { summary.push(s); }
        });
        summary.forEach(function (s) {
            log(s.phase + ": " + s.frames + " frames in " + s.seconds.toFixed(1) + " s; aboard " + s.aboardFrames + "/" + s.frames +
                "; height " + s.heightMin.toFixed(1) + " to " + s.heightMax.toFixed(1) + " m (end " + s.heightEnd.toFixed(1) + ")" +
                "; climb " + s.climbMin.toFixed(2) + " to " + s.climbMax.toFixed(2) + " m/s; speed up to " + s.speedMax.toFixed(2) + " m/s (airspeed " + s.airspeedMax.toFixed(1) + ")" +
                "; " + s.distanceM.toFixed(0) + " m; heading " + s.headingDeg.toFixed(0) + " deg; pitch up to " + s.pitchMaxDeg.toFixed(1) + " deg, roll " + s.rollMaxDeg.toFixed(1) +
                "; " + s.padDistanceEnd.toFixed(0) + " m from the pad at the end" +
                "; buoyancy " + (s.buoyancy || 0).toFixed(0) + " N, ballast " + (s.ballast || 0).toFixed(1) + " kg, fill " + (s.fill || 0).toFixed(3) +
                " (state " + (s.stateBallast || 0).toFixed(1) + " kg, " + (s.stateFill || 0).toFixed(3) + ")" +
                "; my stored local standing " + fmtJitter(s.myLocalStanding) + (s.walkingFrames ? ", walking " + fmtJitter(s.myLocalWalking) : "") +
                (s.facing ? "; facing on the deck " + s.facing.min.toFixed(1) + " to " + s.facing.max.toFixed(1) + " deg" : "") +
                (ROLE === "crew" ? "; the pilot's stored local " + fmtJitter(s.pilotLocal) + ", aboard " + s.pilotAboardFrames + "/" + s.pilotFrames +
                    (s.ball ? "; the ball on the deck " + s.ball.onDeck + "/" + s.ball.frames + " (|x| to " + s.ball.maxX.toFixed(2) + ", |z| to " + s.ball.maxZ.toFixed(2) +
                        ", y " + s.ball.yMin.toFixed(2) + " to " + s.ball.yMax.toFixed(2) + ")" : "") : "") +
                "; hull world step " + fmtSteps(s.hullStep) +
                "; corrections " + s.corrections + " (" + s.replaySteps + " substeps replayed), inputs sent " + s.inputsSent +
                ", state sequence " + s.sequenceFrom + " to " + s.sequenceTo + ", " + s.stateUpdates + " state updates");
        });
        var last = samples.length ? samples[samples.length - 1].stats : {};
        log("totals: " + samples.length + " samples; corrections " + (last.corrections || 0) + ", replay substeps " + (last.replaySteps || 0) +
            ", max error " + (1000 * (last.maxError || 0)).toFixed(0) + " mm, inputs sent " + (last.inputsSent || 0) + ", received " + (last.inputsReceived || 0) +
            ", dropped " + (last.inputsDropped || 0) + ", starved " + (last.inputsStarved || 0) + ", vehicles " + (last.vehicles || 0) +
            "; crossings " + crossings.length);
        Test.saveObject({ role: ROLE, summary: summary, events: events, crossings: crossings, samples: samples }, "physics-airship.json");
        log("done (" + reason + "), " + samples.length + " samples");
        if (ROLE === "pilot" && hull) {
            Airship.deleteAll(LandRover.TERRAIN_CENTER, 2 * LandRover.TERRAIN_SIZE);
        }
        Script.setTimeout(function () { Test.quit(); }, 1500);
    }

    function onUpdate(dt) {
        if (now() > TIMEOUT) { finish("timeout"); return; }
        if (hull && !sample(dt)) { finish("airship gone"); return; }
        if (ROLE === "pilot") { drivePilot(); } else { driveCrew(); }
    }

    Script.update.connect(onUpdate);
    Script.scriptEnding.connect(function () { if (atHelm) { leaveHelm(); } });
    log("started as " + ROLE + (TERRAIN_URL ? ", terrain " + TERRAIN_URL : ""));
}());
