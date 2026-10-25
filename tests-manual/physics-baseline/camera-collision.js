//
//  camera-collision.js
//  tests-manual/physics-baseline
//
//  Camera collision for the third-person camera (ViewState::collideCameraBoom, View > Camera Collision), serverless on
//  world.json's ground. The avatar faces -z with the camera behind it at +z. Cases, each read off Camera.position and
//  the avatar's eye every frame:
//    open        nothing behind: the camera stands at the boom's full length (the reference for the rest)
//    walk back   a wall 3 m behind; the avatar is walked back into it: no frame may show the camera behind the wall's
//                face (it moves in at once)
//    ease out    the wall is deleted with the avatar against it: the camera goes back out, eased (95 % in 0.2-0.6 s)
//    look at     the same wall in the "look at" camera mode: held in front of it
//    glass       a wall you can see through (alpha 0.3): the camera goes through it, to its full length
//    small ball  a moving 0.3 m ball on the boom: ignored
//    big box     a moving 1.5 m box on the boom: stops the camera
//    off         View > Camera Collision unchecked, the wall behind: the camera goes through it, as before
//  Pictures of the wall case with the option on and off. Prints CAMERA-COLLISION lines; samples in
//  camera-collision.json.
//
//  Copyright 2026 Darkride.
//
//  Distributed under the MIT License.
//

(function () {
    var LIFETIME = 300;
    var WALL_THICKNESS = 0.3;
    var MENU_OPTION = "Camera Collision";
    var failures = 0;
    var samples = [];
    var made = [];
    var fullLength = null;

    function log(s) { print("CAMERA-COLLISION " + s); }
    function after(seconds, f) { Script.setTimeout(f, seconds * 1000); }
    function check(ok, text) {
        if (!ok) {
            failures++;
        }
        log((ok ? "PASS " : "FAIL ") + text);
    }
    function eye() { return MyAvatar.getDefaultEyePosition(); }
    function boomLength() { return Vec3.distance(Camera.position, eye()); }

    function add(properties) {
        properties.lifetime = LIFETIME;
        var id = Entities.addEntity(properties);
        made.push(id);
        return id;
    }
    // a wall across the boom, its face (the side toward the avatar) at z = face
    function wall(face, alpha) {
        return add({ type: "Box", name: "camera-collision wall", position: { x: 0, y: 2, z: face + WALL_THICKNESS / 2 },
                     dimensions: { x: 8, y: 4, z: WALL_THICKNESS }, color: { red: 200, green: 120, blue: 60 },
                     alpha: alpha === undefined ? 1.0 : alpha, dynamic: false, collisionless: false });
    }

    // stand the avatar at z, facing -z, and settle
    function stand(z, then) {
        MyAvatar.motorVelocity = { x: 0, y: 0, z: 0 };
        MyAvatar.position = { x: 0, y: 1.0, z: z };
        MyAvatar.orientation = Quat.IDENTITY;
        MyAvatar.headPitch = 0;
        after(1.5, then);
    }

    // records the boom's length every frame for the seconds; then(list of { t, length, cameraZ })
    function record(seconds, then) {
        var list = [];
        var t0 = Date.now();
        function tick() {
            var t = (Date.now() - t0) / 1000;
            list.push({ t: t, length: boomLength(), cameraZ: Camera.position.z, avatarZ: MyAvatar.position.z });
            if (t >= seconds) {
                Script.update.disconnect(tick);
                then(list);
            }
        }
        Script.update.connect(tick);
    }

    function steps(list) {
        var i = 0;
        function next() {
            if (i < list.length) {
                list[i++](next);
            }
        }
        next();
    }

    function open(next) {
        stand(0, function () {
            record(1.0, function (list) {
                fullLength = list[list.length - 1].length;
                samples.push({ name: "open", list: list });
                check(fullLength > 0.5, "open: the camera stands " + fullLength.toFixed(3) + " m from the eye");
                next();
            });
        });
    }

    function walkBack(next) {
        var face = 3.0;
        stand(0, function () {
            wall(face);
            after(1.0, function () {
                // walked back (+z) at 1 m/s until the wall stops the avatar
                MyAvatar.motorReferenceFrame = "world"; MyAvatar.motorMode = "simple"; MyAvatar.motorTimescale = 0.25;
                MyAvatar.motorVelocity = { x: 0, y: 0, z: 1.0 };
                record(4.0, function (list) {
                    MyAvatar.motorVelocity = { x: 0, y: 0, z: 0 }; MyAvatar.motorTimescale = 1000000;
                    samples.push({ name: "walk back", list: list });
                    var worst = -1000;
                    list.forEach(function (s) { worst = Math.max(worst, s.cameraZ - face); });
                    var last = list[list.length - 1];
                    check(worst <= 0.0, "walk back: the camera's furthest past the wall's face over " + list.length +
                          " frames: " + worst.toFixed(3) + " m (must be <= 0); at the end the avatar is at z " +
                          last.avatarZ.toFixed(2) + ", the camera " + last.length.toFixed(3) + " m from the eye");
                    next();
                });
            });
        });
    }

    function easeOut(next) {
        // the avatar against the wall from walk back; the wall goes
        var walls = Entities.findEntitiesByName("camera-collision wall", MyAvatar.position, 20, true);
        var before = boomLength();
        walls.forEach(function (id) { Entities.deleteEntity(id); });
        record(1.5, function (list) {
            samples.push({ name: "ease out", list: list });
            var reached = null;
            list.forEach(function (s) {
                if (reached === null && s.length >= 0.95 * fullLength) {
                    reached = s.t;
                }
            });
            check(reached !== null && reached >= 0.2 && reached <= 0.6, "ease out: from " + before.toFixed(3) +
                  " m to 95 % of " + fullLength.toFixed(3) + " m in " + (reached === null ? "never" : reached.toFixed(2) + " s") +
                  " (0.2 to 0.6 s)");
            next();
        });
    }

    // a case with something on the boom; expectHeld: the camera must stay short of fullLength
    function boomCase(name, make, expectHeld, next, mode) {
        stand(0, function () {
            if (mode) {
                Camera.mode = mode;
            }
            var id = make();
            after(1.5, function () {
                record(0.5, function (list) {
                    var length = list[list.length - 1].length;
                    samples.push({ name: name, list: list });
                    var held = length < fullLength - 0.2;
                    check(held === expectHeld, name + ": the camera " + length.toFixed(3) + " m from the eye (" +
                          (expectHeld ? "held in" : "full length") + " expected; full " + fullLength.toFixed(3) + ")");
                    Entities.deleteEntity(id);
                    Camera.mode = "third person";
                    after(1.0, next);
                });
            });
        });
    }

    function picture(name, then) {
        Window.takeSnapshot(false, false, 0, name);
        log("picture " + name);
        after(1.5, then);
    }

    function pictures(next) {
        stand(0, function () {
            var id = wall(1.0);
            after(1.5, function () {
                picture("camera-collision-on", function () {
                    Menu.setIsOptionChecked(MENU_OPTION, false);
                    after(1.0, function () {
                        picture("camera-collision-off", function () {
                            Menu.setIsOptionChecked(MENU_OPTION, true);
                            Entities.deleteEntity(id);
                            after(1.0, next);
                        });
                    });
                });
            });
        });
    }

    function finish() {
        made.forEach(function (id) { Entities.deleteEntity(id); });
        Test.saveObject({ failures: failures, samples: samples }, "camera-collision.json");
        log((failures === 0 ? "ALL PASS" : failures + " FAILED"));
        after(1.0, function () { Test.quit(); });
    }

    function start(tries) {
        if (!Window.isPhysicsEnabled()) {
            if (tries < 120) {
                after(1.0, function () { start(tries + 1); });
            } else {
                log("FAIL no physics after 120 s");
                Test.quit();
            }
            return;
        }
        Camera.mode = "third person";
        Menu.setIsOptionChecked(MENU_OPTION, true);
        log("option " + MENU_OPTION + ": " + Menu.isOptionChecked(MENU_OPTION));
        steps([
            open,
            walkBack,
            easeOut,
            function (next) { boomCase("look at", function () { return wall(1.0); }, true, next, "look at"); },
            function (next) { boomCase("glass", function () { return wall(1.0, 0.3); }, false, next); },
            function (next) {
                boomCase("small ball", function () {
                    return add({ type: "Sphere", name: "camera-collision ball", position: Vec3.sum(eye(), { x: 0, y: 0, z: 0.8 }),
                                 dimensions: { x: 0.3, y: 0.3, z: 0.3 }, dynamic: true, gravity: { x: 0, y: 0, z: 0 },
                                 collisionless: false, color: { red: 40, green: 160, blue: 40 } });
                }, false, next);
            },
            function (next) {
                boomCase("big box", function () {
                    return add({ type: "Box", name: "camera-collision box", position: Vec3.sum(eye(), { x: 0, y: 0, z: 1.6 }),
                                 dimensions: { x: 1.5, y: 1.5, z: 1.5 }, dynamic: true, gravity: { x: 0, y: 0, z: 0 },
                                 collisionless: false, color: { red: 40, green: 40, blue: 160 } });
                }, true, next);
            },
            function (next) {
                Menu.setIsOptionChecked(MENU_OPTION, false);
                boomCase("off", function () { return wall(1.0); }, false, function () {
                    Menu.setIsOptionChecked(MENU_OPTION, true);
                    next();
                });
            },
            pictures,
            finish
        ]);
    }

    after(2, function () { start(0); });
}());
