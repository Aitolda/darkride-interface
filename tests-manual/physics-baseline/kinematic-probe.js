// A box at rest is given a velocity; its reported position and velocity are logged twice a second.
// For checking that a keyframed (kinematic) entity moves at the speed it was given, once.
(function () {
    var SPEED = 5.0;
    var id = null, t0 = Date.now() / 1000, started = false;
    function log(s) { print("KINEMATIC-PROBE " + s); }
    Script.setTimeout(function () {
        id = Entities.addEntity({ type: "Box", name: "probe", shapeType: "box", position: { x: 0, y: 3, z: 0 },
                                  dimensions: { x: 1, y: 1, z: 1 }, color: { red: 200, green: 60, blue: 60 },
                                  dynamic: false, collisionless: false, damping: 0, grab: { grabbable: false } });
        log("box " + id);
    }, 6000);
    Script.setTimeout(function () {
        Entities.editEntity(id, { velocity: { x: 0, y: 0, z: -SPEED } });
        started = true;
        log("velocity set to " + -SPEED + " at " + (Date.now() / 1000 - t0).toFixed(2));
    }, 10000);
    var timer = Script.setInterval(function () {
        if (!id) { return; }
        var p = Entities.getEntityProperties(id, ["position", "velocity"]);
        log("t=" + (Date.now() / 1000 - t0).toFixed(2) + " z=" + p.position.z.toFixed(3) + " vz=" + p.velocity.z.toFixed(3));
    }, 500);
    Script.setTimeout(function () { Script.clearInterval(timer); Entities.deleteEntity(id); Test.quit(); }, 16000);
}());
