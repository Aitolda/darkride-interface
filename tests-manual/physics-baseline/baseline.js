//
//  baseline.js
//  tests-manual/physics-baseline
//
//  Round 0 of PHYSICS.md: measure how the client as it stands keeps an avatar on a moving platform.
//  A cabin (a floor with low walls) is created in a serverless world and moved in several ways while the
//  avatar stands on it, parented and unparented. Every script frame records the cabin's transform, the
//  avatar's world position and the avatar's position in the cabin's frame. At the end a summary is printed
//  (lines starting with PHYSICS-BASELINE) and the samples are written with Test.saveObject when the client
//  was started with --testResultsLocation.
//
//  Run (serverless, no domain needed):
//    interface --url file:///<this dir>/world.json --testScript <this dir>/baseline.js --testResultsLocation <dir>
//
//  Distributed under the Apache License, Version 2.0.
//

(function () {
    var SPEED = 5.0;              // m/s, a slow monorail
    var CURVE_RADIUS = 15.0;      // m
    var WALK_SPEED = 1.0;         // m/s, the scripted motor
    var NO_JOINT = 65535;         // INVALID_JOINT_INDEX as a quint16: parented to the entity, not to a joint
    var CABIN_TOP = 0.5;          // the cabin floor's top surface, m

    var cabin = null;
    var walls = [];
    var samples = [];
    var phaseLog = [];
    var phase = { name: "boot", t0: 0 };
    var t0 = Date.now() / 1000;
    var curveT = 0;

    function now() { return Date.now() / 1000 - t0; }
    function log(s) { print("PHYSICS-BASELINE " + s); }

    function setPhase(name) {
        phase = { name: name, t0: now() };
        phaseLog.push(phase);
        log("phase " + name + " at " + phase.t0.toFixed(2) + " s");
    }

    function spawnCabin() {
        cabin = Entities.addEntity({
            type: "Box", name: "baseline-cabin", shapeType: "box",
            position: { x: 0, y: CABIN_TOP - 0.25, z: 0 },
            dimensions: { x: 4, y: 0.5, z: 8 },
            color: { red: 200, green: 160, blue: 60 },
            dynamic: false, collisionless: false,
            damping: 0, angularDamping: 0,
            grab: { grabbable: false }
        });
        // low walls, children of the cabin, so the rider has something to bump against
        var wallSpecs = [
            { pos: { x: 2.1, y: 0.5, z: 0 }, dim: { x: 0.2, y: 1.0, z: 8 } },
            { pos: { x: -2.1, y: 0.5, z: 0 }, dim: { x: 0.2, y: 1.0, z: 8 } },
            { pos: { x: 0, y: 0.5, z: 4.1 }, dim: { x: 4.4, y: 1.0, z: 0.2 } },
            { pos: { x: 0, y: 0.5, z: -4.1 }, dim: { x: 4.4, y: 1.0, z: 0.2 } }
        ];
        wallSpecs.forEach(function (w) {
            walls.push(Entities.addEntity({
                type: "Box", name: "baseline-wall", shapeType: "box",
                parentID: cabin, localPosition: w.pos, dimensions: w.dim,
                color: { red: 120, green: 90, blue: 40 },
                dynamic: false, collisionless: false, grab: { grabbable: false }
            }));
        });
        log("cabin " + cabin + " with " + walls.length + " walls");
    }

    function cabinState() {
        return Entities.getEntityProperties(cabin, ["position", "rotation", "velocity", "angularVelocity"]);
    }

    function resetCabin() {
        Entities.editEntity(cabin, {
            position: { x: 0, y: CABIN_TOP - 0.25, z: 0 }, rotation: Quat.IDENTITY,
            velocity: Vec3.ZERO, angularVelocity: Vec3.ZERO
        });
    }

    function putAvatarOnCabin() {
        MyAvatar.position = { x: 0, y: CABIN_TOP + 1.2, z: 0 };
        MyAvatar.orientation = Quat.IDENTITY;
    }

    function parentAvatar(on) {
        if (on) {
            MyAvatar.parentJointIndex = NO_JOINT;
            MyAvatar.parentID = cabin;
        } else {
            MyAvatar.parentID = Uuid.NULL;
        }
        log("avatar parentID now " + MyAvatar.parentID);
    }

    function sample(dt) {
        var c = cabinState();
        var worldPos = MyAvatar.position;
        var parented = MyAvatar.parentID == cabin;
        var localComputed = Vec3.multiplyQbyV(Quat.inverse(c.rotation), Vec3.subtract(worldPos, c.position));
        samples.push({
            t: now(), dt: dt, phase: phase.name, parented: parented,
            cabinPos: c.position, cabinRot: c.rotation, cabinVel: c.velocity,
            avatarPos: worldPos,
            local: localComputed,
            localProp: parented ? MyAvatar.localPosition : null
        });
    }

    // the curve: the cabin drives itself around a circle by having its position and rotation set every frame,
    // which is how scripted elevators and trains move in this engine today
    function driveCurve(dt) {
        curveT += dt;
        var omega = SPEED / CURVE_RADIUS;                // rad/s
        var a = omega * curveT;
        var pos = { x: CURVE_RADIUS * Math.sin(a), y: CABIN_TOP - 0.25, z: CURVE_RADIUS * (Math.cos(a) - 1) };
        var rot = Quat.fromPitchYawRollRadians(0, -a, 0);
        Entities.editEntity(cabin, { position: pos, rotation: rot });
    }

    // ---- the schedule ----
    var steps = [
        { name: "boot", dur: 8, enter: function () {} },
        { name: "spawn", dur: 1, enter: function () { spawnCabin(); putAvatarOnCabin(); } },
        { name: "settle", dur: 4, enter: function () {} },

        { name: "straight-unparented", dur: 8, enter: function () {
            Entities.editEntity(cabin, { velocity: { x: 0, y: 0, z: -SPEED } });
        } },
        { name: "reset-1", dur: 4, enter: function () { resetCabin(); putAvatarOnCabin(); } },

        { name: "straight-parented", dur: 8, enter: function () {
            parentAvatar(true);
            Entities.editEntity(cabin, { velocity: { x: 0, y: 0, z: -SPEED } });
        } },
        { name: "reset-2", dur: 4, enter: function () { parentAvatar(false); resetCabin(); putAvatarOnCabin(); } },

        { name: "curve-unparented", dur: 10, enter: function () { curveT = 0; }, update: driveCurve },
        { name: "reset-3", dur: 4, enter: function () { resetCabin(); putAvatarOnCabin(); } },

        { name: "curve-parented", dur: 10, enter: function () { parentAvatar(true); curveT = 0; }, update: driveCurve },
        { name: "reset-4", dur: 4, enter: function () { parentAvatar(false); resetCabin(); putAvatarOnCabin(); } },

        { name: "walk-parented", dur: 6, enter: function () {
            parentAvatar(true);
            Entities.editEntity(cabin, { velocity: { x: 0, y: 0, z: -SPEED } });
            MyAvatar.motorReferenceFrame = "world";
            MyAvatar.motorTimescale = 0.2;
            MyAvatar.motorVelocity = { x: WALK_SPEED, y: 0, z: 0 };
        }, update: function () {
            // walk across the cabin and back: 3 s each way
            var tt = now() - phase.t0;
            MyAvatar.motorVelocity = { x: (tt < 3 ? WALK_SPEED : -WALK_SPEED), y: 0, z: 0 };
        } },
        { name: "end", dur: 1, enter: function () {
            MyAvatar.motorVelocity = Vec3.ZERO;
            parentAvatar(false);
            resetCabin();
        } }
    ];
    var stepIndex = -1;

    function nextStep() {
        stepIndex++;
        if (stepIndex >= steps.length) {
            finish();
            return;
        }
        var s = steps[stepIndex];
        setPhase(s.name);
        s.enter();
    }

    // ---- analysis ----
    function stats(name, parented) {
        var rows = samples.filter(function (r) { return r.phase === name; });
        if (rows.length < 3) { return null; }
        var first = rows[0], last = rows[rows.length - 1];
        var drift = Vec3.subtract(last.local, first.local);
        // frame-to-frame change of the local position, minus the phase's mean change
        var deltas = [];
        for (var i = 1; i < rows.length; i++) {
            deltas.push(Vec3.subtract(rows[i].local, rows[i - 1].local));
        }
        var mean = { x: 0, y: 0, z: 0 };
        deltas.forEach(function (d) { mean = Vec3.sum(mean, d); });
        mean = Vec3.multiply(1 / deltas.length, mean);
        var sumH = 0, sumV = 0, maxH = 0, maxV = 0;
        deltas.forEach(function (d) {
            var e = Vec3.subtract(d, mean);
            var h = Math.sqrt(e.x * e.x + e.z * e.z), v = Math.abs(e.y);
            sumH += h * h; sumV += v * v;
            if (h > maxH) { maxH = h; }
            if (v > maxV) { maxV = v; }
        });
        var jitterProp = null;
        if (parented) {
            // the engine's own stored local position, the value other clients receive
            var pd = [];
            for (var j = 1; j < rows.length; j++) {
                if (rows[j].localProp && rows[j - 1].localProp) {
                    pd.push(Vec3.subtract(rows[j].localProp, rows[j - 1].localProp));
                }
            }
            if (pd.length > 1) {
                var pm = { x: 0, y: 0, z: 0 };
                pd.forEach(function (d) { pm = Vec3.sum(pm, d); });
                pm = Vec3.multiply(1 / pd.length, pm);
                var ps = 0, pmax = 0;
                pd.forEach(function (d) {
                    var e = Vec3.subtract(d, pm);
                    var h = Math.sqrt(e.x * e.x + e.z * e.z);
                    ps += h * h; if (h > pmax) { pmax = h; }
                });
                jitterProp = { rmsMm: 1000 * Math.sqrt(ps / pd.length), maxMm: 1000 * pmax };
            }
        }
        return {
            phase: name, frames: rows.length, seconds: last.t - first.t,
            driftM: drift, driftHorizontalM: Math.sqrt(drift.x * drift.x + drift.z * drift.z),
            jitterHorizontalRmsMm: 1000 * Math.sqrt(sumH / deltas.length), jitterHorizontalMaxMm: 1000 * maxH,
            jitterVerticalRmsMm: 1000 * Math.sqrt(sumV / deltas.length), jitterVerticalMaxMm: 1000 * maxV,
            storedLocalJitter: jitterProp,
            localStart: first.local, localEnd: last.local
        };
    }

    function finish() {
        Script.update.disconnect(onUpdate);
        var summary = ["straight-unparented", "straight-parented", "curve-unparented", "curve-parented", "walk-parented"]
            .map(function (n) { return stats(n, n.indexOf("parented") >= 0 && n.indexOf("unparented") < 0); })
            .filter(function (s) { return s; });
        summary.forEach(function (s) {
            log(s.phase + ": " + s.frames + " frames in " + s.seconds.toFixed(1) + " s; drift " +
                s.driftHorizontalM.toFixed(3) + " m horizontal (" + s.driftM.y.toFixed(3) + " m vertical); " +
                "jitter rms " + s.jitterHorizontalRmsMm.toFixed(1) + " mm, max " + s.jitterHorizontalMaxMm.toFixed(1) +
                " mm horizontal; rms " + s.jitterVerticalRmsMm.toFixed(1) + " mm, max " + s.jitterVerticalMaxMm.toFixed(1) + " mm vertical" +
                (s.storedLocalJitter ? "; stored local position jitter rms " + s.storedLocalJitter.rmsMm.toFixed(1) +
                    " mm, max " + s.storedLocalJitter.maxMm.toFixed(1) + " mm" : ""));
        });
        Test.saveObject({ summary: summary, phases: phaseLog, samples: samples }, "physics-baseline.json");
        log("done, " + samples.length + " samples");
        walls.forEach(function (w) { Entities.deleteEntity(w); });
        if (cabin) { Entities.deleteEntity(cabin); }
        Script.setTimeout(function () { Test.quit(); }, 1000);
    }

    function onUpdate(dt) {
        if (stepIndex < 0) { nextStep(); return; }
        var s = steps[stepIndex];
        if (s.update) { s.update(dt); }
        if (cabin && stepIndex >= 2) { sample(dt); }
        if (now() - phase.t0 >= s.dur) { nextStep(); }
    }

    Script.update.connect(onUpdate);
    Script.scriptEnding.connect(function () {
        if (MyAvatar.parentID == cabin) { parentAvatar(false); }
    });
    log("started");
}());
