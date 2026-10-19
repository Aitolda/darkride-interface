// PHYSICS.md round 10: the loading screen's length. Polls Window.isPhysicsEnabled() (the gate that lifts the loading
// screen, "Physics enabled" in the log) from the script's start, prints the moment it turns on, and quits 5 s later.
// Quits after LOADING_LIMIT s regardless. Run by run-loading.sh.
//   Darkride --url hifi://localhost:42102 --testScript loading.js
(function () {
    var LIMIT = (typeof LOADING_LIMIT === "number") ? LOADING_LIMIT : 240;
    var start = Date.now();
    var enabledAt = -1;
    print("PHYSICS-LOADING started, limit " + LIMIT + " s");
    var timer = Script.setInterval(function () {
        var elapsed = (Date.now() - start) / 1000;
        if (enabledAt < 0 && Window.isPhysicsEnabled()) {
            enabledAt = elapsed;
            print("PHYSICS-LOADING physics enabled " + enabledAt.toFixed(1) + " s after the script started, avatar at "
                + JSON.stringify(MyAvatar.position));
        }
        if ((enabledAt >= 0 && elapsed > enabledAt + 5) || elapsed > LIMIT) {
            Script.clearInterval(timer);
            print("PHYSICS-LOADING done: physics " + (enabledAt >= 0 ? "enabled at " + enabledAt.toFixed(1) + " s" : "never enabled")
                + ", quitting at " + elapsed.toFixed(1) + " s");
            Test.quit();
        }
    }, 200);
}());
