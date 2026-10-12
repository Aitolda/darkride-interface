// Start the client, wait for physics to come up, then quit. For checking that a build runs at all.
//   interface --allowMultipleInstances --url file://<dir>/world.json --testScript <dir>/smoke.js
(function () {
    var SECONDS = 12;
    print("PHYSICS-SMOKE started, quitting in " + SECONDS + " s");
    Script.setTimeout(function () {
        print("PHYSICS-SMOKE avatar at " + JSON.stringify(MyAvatar.position) + ", quitting");
        Test.quit();
    }, SECONDS * 1000);
}());
