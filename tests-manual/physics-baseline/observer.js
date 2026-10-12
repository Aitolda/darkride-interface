//
//  observer.js
//  tests-manual/physics-baseline
//
//  Round 3 of PHYSICS.md: the second client. While baseline-frame.js moves the cabin on one client, this
//  script runs on another client in the same domain: it waits for the cabin, steps on from the station
//  platform while the cabin stands still (the crossing), rides every phase, steps off behind the cabin when
//  the mover does (step-off) and watches it go from the ground. Every script frame it records the cabin as
//  this client sees it, its own pose, and the other avatar's pose as the avatar mixer delivers it. At the end
//  a summary is printed (lines starting with PHYSICS-OBSERVER) and the samples are written with Test.saveObject.
//
//  Run (see run-pair.sh):
//    interface --allowMultipleInstances --url hifi://localhost:<port> --testScript observer.js --testResultsLocation <dir>
//
//  Distributed under the Apache License, Version 2.0.
//

(function () {
    var WALK_SPEED = 1.5;         // m/s, the scripted motor
    var PLATFORM_SPOT = { x: 0, y: 1.3, z: 6.3 };   // on the station platform, behind the cabin's open end
    var ABOARD_Z = 2.5;           // where to stop inside the cabin, in its coordinates
    var TIMEOUT = 150;            // s, the whole run

    var cabin = null;
    var mode = "waiting";         // waiting, arriving, stepping-on, riding, stepping-off, ground
    var samples = [];
    var events = [];
    var t0 = Date.now() / 1000;
    var modeT0 = 0;
    var lastPhase = "";
    var tookSteps = { "step-off": false };

    function now() { return Date.now() / 1000 - t0; }
    function log(s) { print("PHYSICS-OBSERVER " + s); }
    function setMode(m) {
        mode = m; modeT0 = now();
        events.push({ t: now(), mode: m });
        log("mode " + m + " at " + now().toFixed(2) + " s");
    }

    function findCabin() {
        var found = Entities.findEntitiesByName("baseline-cabin", { x: 0, y: 0, z: 0 }, 500);
        return found.length ? found[0] : null;
    }

    function cabinState() {
        return Entities.getEntityProperties(cabin, ["position", "rotation", "velocity", "angularVelocity", "description"]);
    }

    function otherAvatar() {
        var ids = AvatarList.getAvatarIdentifiers();
        for (var i = 0; i < ids.length; i++) {
            if (ids[i] && ids[i] !== MyAvatar.sessionUUID && ids[i] !== Uuid.NULL) {
                return AvatarList.getAvatar(ids[i]);
            }
        }
        return null;
    }

    function sample(dt) {
        var c = cabinState();
        if (!c.position) { return false; }
        var phase = c.description || "";
        if (phase !== lastPhase) {
            lastPhase = phase;
            log("mover phase " + phase + " at " + now().toFixed(2) + " s");
        }
        var inv = Quat.inverse(c.rotation);
        var myPos = MyAvatar.position;
        var parented = MyAvatar.parentID == cabin;
        var row = {
            t: now(), dt: dt, phase: phase, mode: mode, parented: parented,
            cabinPos: c.position, cabinRot: c.rotation, cabinVel: c.velocity,
            myPos: myPos,
            myLocal: Vec3.multiplyQbyV(inv, Vec3.subtract(myPos, c.position)),
            myLocalProp: parented ? MyAvatar.localPosition : null
        };
        var other = otherAvatar();
        if (other) {
            var otherPos = other.position;
            var otherParented = other.parentID == cabin;
            row.otherPos = otherPos;
            row.otherParented = otherParented;
            row.otherLocal = Vec3.multiplyQbyV(inv, Vec3.subtract(otherPos, c.position));
            row.otherLocalProp = otherParented ? other.localPosition : null;
            // what this rider sees: the other rider's position relative to its own, in the cabin's frame
            row.relative = Vec3.multiplyQbyV(inv, Vec3.subtract(otherPos, myPos));
        }
        samples.push(row);
        return true;
    }

    function drive() {
        var tt = now() - modeT0;
        if (mode === "arriving") {
            if (tt > 0.5) {
                MyAvatar.motorReferenceFrame = "world";
                MyAvatar.motorTimescale = 0.2;
                MyAvatar.motorVelocity = { x: 0, y: 0, z: -WALK_SPEED };
                setMode("stepping-on");
            }
        } else if (mode === "stepping-on") {
            var c = cabinState();
            var local = Vec3.multiplyQbyV(Quat.inverse(c.rotation), Vec3.subtract(MyAvatar.position, c.position));
            if (local.z < ABOARD_Z || tt > 6) {
                MyAvatar.motorVelocity = Vec3.ZERO;
                log("aboard at local " + JSON.stringify(local) + ", parentID " + MyAvatar.parentID + (tt > 6 ? " (timed out)" : ""));
                setMode("riding");
            }
        } else if (mode === "riding") {
            if (lastPhase === "step-off" && !tookSteps["step-off"] && now() - stepOffSeen > 3) {
                // the mover walked off at two seconds; this rider follows a second later
                tookSteps["step-off"] = true;
                MyAvatar.motorReferenceFrame = "world";
                MyAvatar.motorTimescale = 0.2;
                MyAvatar.motorVelocity = { x: 0, y: 0, z: WALK_SPEED };
                setMode("stepping-off");
            }
        } else if (mode === "stepping-off") {
            if (MyAvatar.parentID == Uuid.NULL || tt > 5) {
                MyAvatar.motorVelocity = Vec3.ZERO;
                setMode("ground");
            }
        }
    }
    var stepOffSeen = -1;

    // ---- analysis ----
    function jitter(vectors) {
        // frame-to-frame change minus the mean change, rms and max, horizontal and vertical
        if (vectors.length < 3) { return null; }
        var deltas = [];
        for (var i = 1; i < vectors.length; i++) { deltas.push(Vec3.subtract(vectors[i], vectors[i - 1])); }
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
        return { n: deltas.length, meanStepMm: 1000 * Vec3.length(mean),
            rmsMm: 1000 * Math.sqrt(sumH / deltas.length), maxMm: 1000 * maxH,
            verticalRmsMm: 1000 * Math.sqrt(sumV / deltas.length), verticalMaxMm: 1000 * maxV };
    }

    function stats(name) {
        var rows = samples.filter(function (r) { return r.phase === name; });
        if (rows.length < 3) { return null; }
        var first = rows[0], last = rows[rows.length - 1];
        var crossings = [];
        for (var k = 1; k < rows.length; k++) {
            if (rows[k].parented !== rows[k - 1].parented) {
                var before = Vec3.subtract(rows[k - 1].myPos, rows[k - 2 >= 0 ? k - 2 : 0].myPos);
                var at = Vec3.subtract(rows[k].myPos, rows[k - 1].myPos);
                crossings.push({ t: rows[k].t, parented: rows[k].parented,
                    worldStepMm: 1000 * Vec3.length(at), previousStepMm: 1000 * Vec3.length(before) });
            }
        }
        var ridingRows = rows.filter(function (r) { return r.parented; });
        var groundRows = rows.filter(function (r) { return !r.parented && r.mode === "ground"; });
        var pick = function (list, key) { return list.filter(function (r) { return r[key]; }).map(function (r) { return r[key]; }); };
        return {
            phase: name, frames: rows.length, seconds: last.t - first.t,
            parentedFrames: ridingRows.length,
            otherParentedFrames: rows.filter(function (r) { return r.otherParented; }).length,
            // this rider's own stored local position, while riding (should not move)
            myStoredLocal: jitter(pick(ridingRows, "myLocalProp")),
            // the other rider's stored local position as received (should not move while they stand still)
            otherStoredLocal: jitter(pick(ridingRows, "otherLocalProp")),
            // the other rider as seen from here, relative, in the cabin's frame (two reads a moment apart)
            relative: jitter(pick(ridingRows, "relative")),
            // the cabin's own world motion on this client: the step per frame, and how even it is
            cabinStepRiding: jitter(ridingRows.map(function (r) { return r.cabinPos; })),
            cabinStepGround: jitter(groundRows.map(function (r) { return r.cabinPos; })),
            crossings: crossings
        };
    }

    function fmt(j) {
        return j ? ("rms " + j.rmsMm.toFixed(1) + " mm, max " + j.maxMm.toFixed(1) + " mm (vertical rms " +
            j.verticalRmsMm.toFixed(1) + ", max " + j.verticalMaxMm.toFixed(1) + " mm; mean step " + j.meanStepMm.toFixed(1) +
            " mm over " + j.n + " frames)") : "n/a";
    }

    var finished = false;
    function finish(reason) {
        if (finished) { return; }
        finished = true;
        Script.update.disconnect(onUpdate);
        MyAvatar.motorVelocity = Vec3.ZERO;
        var phases = [];
        samples.forEach(function (r) { if (phases.indexOf(r.phase) < 0) { phases.push(r.phase); } });
        var summary = phases.map(stats).filter(function (s) { return s; });
        summary.forEach(function (s) {
            log(s.phase + ": " + s.frames + " frames in " + s.seconds.toFixed(1) + " s; riding " + s.parentedFrames +
                ", other riding " + s.otherParentedFrames +
                "; my stored local " + fmt(s.myStoredLocal) +
                "; other's stored local " + fmt(s.otherStoredLocal) +
                "; other relative to me " + fmt(s.relative) +
                "; cabin step while riding " + fmt(s.cabinStepRiding) +
                "; cabin step from the ground " + fmt(s.cabinStepGround) +
                (s.crossings.length ? "; crossings: " + s.crossings.map(function (c) {
                    return (c.parented ? "in" : "out") + " at " + c.t.toFixed(2) + " s, world step " + c.worldStepMm.toFixed(1) +
                        " mm (previous " + c.previousStepMm.toFixed(1) + " mm)"; }).join(", ") : ""));
        });
        Test.saveObject({ summary: summary, events: events, samples: samples }, "physics-observer.json");
        log("done (" + reason + "), " + samples.length + " samples");
        Script.setTimeout(function () { Test.quit(); }, 1000);
    }

    var lastPoll = 0;
    function onUpdate(dt) {
        if (now() > TIMEOUT) { finish("timeout"); return; }
        if (mode === "waiting") {
            if (now() - lastPoll < 0.1) { return; }
            lastPoll = now();
            cabin = findCabin();
            if (cabin) {
                log("cabin " + cabin + " seen at " + now().toFixed(2) + " s");
                MyAvatar.position = PLATFORM_SPOT;
                MyAvatar.orientation = Quat.IDENTITY;
                setMode("arriving");
            }
            return;
        }
        if (!sample(dt)) {
            finish("cabin gone");
            return;
        }
        if (lastPhase === "step-off" && stepOffSeen < 0) { stepOffSeen = now(); }
        drive();
    }

    Script.update.connect(onUpdate);
    Script.scriptEnding.connect(function () { MyAvatar.motorVelocity = Vec3.ZERO; });
    log("started");
}());
