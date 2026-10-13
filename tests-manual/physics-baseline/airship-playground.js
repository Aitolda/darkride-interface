//
//  airship-playground.js
//  tests-manual/physics-baseline
//
//  Fly the airship yourself (PHYSICS.md round 7). Makes the terrain and the ship on its pad (or finds the ones already
//  there), stands you beside it, and leaves the rest to scripts/system/vehicles.js: walk aboard (the deck is a step up
//  from the pad at the stern; its top is 0.4 m above the ground) and click the ship to take the helm: W/S set the throttle
//  (X cuts it), A/D or left/right the rudder, up/down the elevator, Space and C the vertical thruster, B drops ballast, V
//  vents gas, F leaves the helm. R puts a fresh ship on the pad; T takes you back to the pad. The terrain's URL comes from
//  AIRSHIP_CONFIG (fly-airship.sh writes it, with landrover.js and airship-rig.js, into one script).
//
//  Distributed under the Apache License, Version 2.0.
//

(function () {
    var config = (typeof AIRSHIP_CONFIG !== "undefined" && AIRSHIP_CONFIG) ? AIRSHIP_CONFIG : {};
    var TERRAIN_URL = config.terrainURL || "";
    var LIFETIME = config.lifetime || 3600;
    var terrain = null;
    var ship = null;
    var tries = 0;

    function log(s) { print("AIRSHIP-PLAYGROUND " + s); }

    function toPad() {
        var c = LandRover.TERRAIN_CENTER;
        MyAvatar.position = { x: c.x + 4, y: LandRover.groundHeight(c.x + 4, c.z) + 1.2, z: c.z + 6 };
        MyAvatar.orientation = Quat.fromPitchYawRollDegrees(0, 90, 0);
    }

    function existingShip() {
        var found = Entities.findEntitiesByName("airship-hull", LandRover.TERRAIN_CENTER, 2 * LandRover.TERRAIN_SIZE, false);
        return found.length ? found[0] : null;
    }

    function newShip() {
        Airship.deleteAll(LandRover.TERRAIN_CENTER, 2 * LandRover.TERRAIN_SIZE);
        var c = LandRover.TERRAIN_CENTER;
        ship = Airship.create(c.x, c.z, 0, LIFETIME).hull;
        log("a fresh airship on the pad: " + ship);
    }

    function setUp() {
        terrain = LandRover.createTerrain(TERRAIN_URL, LIFETIME);
        if (!terrain || terrain === Uuid.NULL) {
            terrain = null;
            if (++tries < 60) { Script.setTimeout(setUp, 1000); } else { log("no entity server; giving up"); }
            return;
        }
        toPad();
        Script.setTimeout(function () {
            ship = existingShip();
            if (ship) { log("the airship is already here: " + ship); } else { newShip(); }
            log("walk aboard and click the ship for the helm; W/S throttle, X cut, A/D rudder, up/down elevator, Space/C up and down, B ballast, V vent, F leave; R fresh ship; T to the pad");
        }, 3000);
    }

    function onKey(event) {
        if (!event.text || event.isControl || event.isAlt) { return; }
        var key = event.text.toLowerCase();
        if (key === "r" && MyAvatar.parentID == Uuid.NULL) { newShip(); }
        if (key === "t" && MyAvatar.parentID == Uuid.NULL) { toPad(); }
    }

    Controller.keyPressEvent.connect(onKey);
    Script.scriptEnding.connect(function () { Controller.keyPressEvent.disconnect(onKey); });
    Script.setTimeout(setUp, 4000);
    log("started; terrain " + TERRAIN_URL);
}());
