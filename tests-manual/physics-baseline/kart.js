//
//  kart.js
//  tests-manual/physics-baseline
//
//  The go-kart (kart-rig.js) driven through Entities.setVehicleControls on world.json's flat ground, serverless: the
//  avatar sits in its seat as vehicles.js would (parented to the chassis, collisions off, the driver). Phases, read every
//  frame from the chassis: full throttle (top speed, the time to 10 m/s), full brake (the stopping distance), full lock
//  at 2 m/s, as indoors (the turning radius from speed over yaw rate, the most body roll), full lock at 10 m/s (the
//  radius, and the slip between where the kart points and where it goes: it must not spin), full reverse (its speed).
//  The ground's friction is set to 0.9, the test track's and Darkside's, or to KART_CONFIG.floorFriction (run-kart.sh's
//  KART_FLOOR_FRICTION; 0.5 is the default ground's): the tyres' grip is their curve times the ground's friction.
//  Prints KART lines; samples in kart.json.
//
//  Copyright 2026 Darkride.
//
//  Distributed under the MIT License.
//

(function () {
    var SIT_ANIMATION_URL = "https://s3-us-west-1.amazonaws.com/hifi-content/clement/production/animations/sitting_idle.fbx";
    var LIFETIME = 300;
    var FLOOR_FRICTION = (typeof KART_CONFIG !== "undefined" && KART_CONFIG.floorFriction) || 0.9;
    var GRIPPY = FLOOR_FRICTION >= 0.8;
    var samples = [];
    var kart = null;
    var failures = 0;

    function log(s) { print("KART " + s); }
    function after(seconds, f) { Script.setTimeout(f, seconds * 1000); }
    function check(ok, text) {
        if (!ok) {
            failures++;
        }
        log((ok ? "PASS " : "FAIL ") + text);
    }

    function sit() {
        var seat = JSON.parse(Kart.SPEC.seats)[0];
        MyAvatar.collisionsEnabled = false;
        MyAvatar.getAnimationRoles().filter(function (role) {
            return !(role.indexOf("right") === 0 || role.indexOf("left") === 0);
        }).forEach(function (role) { MyAvatar.overrideRoleAnimation(role, SIT_ANIMATION_URL, 30, true, 1, 350); });
        MyAvatar.parentID = kart.chassis;
        MyAvatar.parentJointIndex = 65535;
        MyAvatar.localPosition = { x: seat.position[0], y: seat.position[1], z: seat.position[2] };
        MyAvatar.localOrientation = Quat.IDENTITY;
        Entities.editEntity(kart.chassis, { vehicle: { driver: MyAvatar.sessionUUID } });
    }

    // drives with the controls for the seconds (or until done(sample) says so), recording; then(list)
    function drive(name, controls, seconds, then, done) {
        var list = [];
        var t0 = Date.now();
        var previous = null;
        function tick() {
            var t = (Date.now() - t0) / 1000;
            var p = Entities.getEntityProperties(kart.chassis, ["position", "rotation", "velocity"]);
            Entities.setVehicleControls(kart.chassis, typeof controls === "function" ?
                controls(Vec3.dot(p.velocity, Quat.getFront(p.rotation))) : controls);
            var front = Quat.getFront(p.rotation);
            var up = Quat.getUp(p.rotation);
            var yaw = Math.atan2(-front.x, -front.z);
            var s = { phase: name, t: t, position: p.position, speed: Vec3.dot(p.velocity, front),
                      lateral: Vec3.dot(p.velocity, Quat.getRight(p.rotation)),
                      roll: Math.acos(Math.max(-1, Math.min(1, up.y))) * 180 / Math.PI, yaw: yaw, yawRate: 0 };
            if (previous && t > previous.t) {
                var dYaw = yaw - previous.yaw;
                dYaw = Math.atan2(Math.sin(dYaw), Math.cos(dYaw));
                s.yawRate = dYaw / (t - previous.t);
            }
            previous = s;
            list.push(s);
            samples.push(s);
            if (t >= seconds || (done && done(s))) {
                Script.update.disconnect(tick);
                then(list);
            }
        }
        Script.update.connect(tick);
    }

    function course() {
        drive("throttle", { throttle: 1, steering: 0, brake: 0, handBrake: 0 }, 6, function (list) {
            var top = 0, tenAt = null;
            list.forEach(function (s) {
                top = Math.max(top, s.speed);
                if (tenAt === null && s.speed >= 10) {
                    tenAt = s.t;
                }
            });
            var tenLimit = GRIPPY ? 2.5 : 3.5;
            check(top >= 15 && top <= 19 && tenAt !== null && tenAt <= tenLimit,
                  "full throttle: top speed " + top.toFixed(2) + " m/s, 10 m/s after " +
                  (tenAt === null ? "never" : tenAt.toFixed(2) + " s") +
                  " (15 to 19 m/s; 10 m/s within " + tenLimit + " s on a " + FLOOR_FRICTION + " ground)");
            var from = Entities.getEntityProperties(kart.chassis, ["position"]).position;
            drive("brake", { throttle: 0, steering: 0, brake: 1, handBrake: 0 }, 5, function (list) {
                var last = list[list.length - 1];
                var distance = Vec3.distance(from, last.position);
                check(last.speed < 0.3 && distance < 20, "full brake: from " + list[0].speed.toFixed(2) + " m/s, stopped in " +
                      distance.toFixed(2) + " m and " + last.t.toFixed(2) + " s");
                // full lock with the speed held at 2 m/s, as in a room: a little throttle under it, coasting over it
                drive("full lock", function (speed) {
                    return { throttle: speed < 2.0 ? 0.3 : 0, steering: 1, brake: 0, handBrake: 0 };
                }, 7, function (list) {
                    var turning = list.filter(function (s) { return s.t > 3 && Math.abs(s.yawRate) > 0.05; });
                    var radius = 0, speed = 0, roll = 0;
                    turning.forEach(function (s) { radius += Math.abs(s.speed / s.yawRate); speed += s.speed; });
                    list.forEach(function (s) { roll = Math.max(roll, s.roll); });
                    radius /= Math.max(1, turning.length);
                    speed /= Math.max(1, turning.length);
                    check(turning.length > 0 && radius < 1.7, "full lock at 2 m/s: turning radius " + radius.toFixed(2) +
                          " m at " + speed.toFixed(2) + " m/s (under 1.7 m; the rover's is 3.6 m)");
                    check(roll < 25, "full lock: most body roll " + roll.toFixed(1) + " degrees (under 25: it doesn't tip)");
                    fastTurn();
                });
            }, function (s) { return s.t > 0.5 && s.speed < 0.05; });
        });
    }

    // up to 10 m/s straight, then full lock with the speed held near 10: the turning radius (the ground's grip sets it
    // at speed), the most slip between where the kart points and where it goes (a spin would pass 35 degrees), the most
    // body roll; then a stop and full reverse
    function fastTurn() {
        drive("to 10 m/s", { throttle: 1, steering: 0, brake: 0, handBrake: 0 }, 6, function () {
            drive("fast turn", function (speed) {
                return { throttle: speed < 10 ? 1 : 0.4, steering: 1, brake: 0, handBrake: 0 };
            }, 4, function (list) {
                var turning = list.filter(function (s) { return s.t > 1.5 && Math.abs(s.yawRate) > 0.05; });
                var radius = 0, speed = 0, slip = 0, roll = 0;
                turning.forEach(function (s) {
                    var v = Math.sqrt(s.speed * s.speed + s.lateral * s.lateral);
                    radius += v / Math.abs(s.yawRate);
                    speed += v;
                });
                list.forEach(function (s) {
                    slip = Math.max(slip, Math.atan2(Math.abs(s.lateral), Math.abs(s.speed)) * 180 / Math.PI);
                    roll = Math.max(roll, s.roll);
                });
                radius /= Math.max(1, turning.length);
                speed /= Math.max(1, turning.length);
                var radiusLimit = GRIPPY ? 16 : 22;
                check(turning.length > 0 && radius < radiusLimit,
                      "full lock at speed: turning radius " + radius.toFixed(2) + " m at " + speed.toFixed(2) +
                      " m/s (under " + radiusLimit + " m on a " + FLOOR_FRICTION + " ground)");
                check(slip < 35, "full lock at speed: most slip " + slip.toFixed(1) +
                      " degrees (under 35: it doesn't spin)");
                check(roll < 25, "full lock at speed: most body roll " + roll.toFixed(1) +
                      " degrees (under 25: it doesn't tip)");
                drive("stop", { throttle: 0, steering: 0, brake: 1, handBrake: 1 }, 3, function () {
                    drive("reverse", { throttle: -1, steering: 0, brake: 0, handBrake: 0 }, 4, function (list) {
                        var back = 0;
                        list.forEach(function (s) { back = Math.min(back, s.speed); });
                        check(back < -2 && back > -8, "full reverse: " + back.toFixed(2) + " m/s (2 to 8)");
                        finish();
                    });
                }, function (s) { return s.t > 0.5 && Math.abs(s.speed) < 0.05; });
            });
        }, function (s) { return s.speed >= 10; });
    }

    function finish() {
        Entities.setVehicleControls(kart.chassis, { throttle: 0, steering: 0, brake: 1, handBrake: 1 });
        Test.saveObject({ failures: failures, samples: samples }, "kart.json");
        log(failures === 0 ? "ALL PASS" : failures + " FAILED");
        after(1, function () { Test.quit(); });
    }

    function start(tries) {
        if (!Window.isPhysicsEnabled()) {
            if (tries < 120) {
                after(1, function () { start(tries + 1); });
            } else {
                log("FAIL no physics after 120 s");
                Test.quit();
            }
            return;
        }
        // the ground's grip: 0.9 as the test track's, Darkside's and the camera world's ground (world.json's: 0.5)
        Entities.findEntitiesByName("Ground", { x: 0, y: 0, z: 0 }, 50, true).forEach(function (id) {
            Entities.editEntity(id, { friction: FLOOR_FRICTION });
        });
        log("the ground's friction " + FLOOR_FRICTION);
        kart = Kart.create(0, 0, -5, 0, LIFETIME);
        MyAvatar.position = { x: 2, y: 1, z: -5 };
        after(3, function () {
            sit();
            after(1.5, course);
        });
    }

    after(2, function () { start(0); });
}());
