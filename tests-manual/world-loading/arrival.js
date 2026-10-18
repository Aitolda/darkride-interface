//
//  arrival.js
//  tests-manual/world-loading
//
//  Records one arrival in a domain, every 250 ms: whether physics has started (the avatar can move), whether the
//  whole-world loading screen is up and its counts, how many entities the client holds, and the avatar's height
//  (a fall through a floor shows as a drop below where physics started). With ARRIVAL_SKIP_AFTER (seconds, prepended
//  by run.sh) it presses "Show me now" then. Prints WORLD-LOADING lines and saves the samples with Test.saveObject.
//
//  Distributed under the Apache License, Version 2.0.
//

(function () {
    var DURATION = typeof ARRIVAL_DURATION !== "undefined" ? ARRIVAL_DURATION : 90;    // seconds
    var SKIP_AFTER = typeof ARRIVAL_SKIP_AFTER !== "undefined" ? ARRIVAL_SKIP_AFTER : -1;
    var start = Date.now();
    var samples = [];
    var events = {};
    var physicsY = null;
    var lowestY = null;
    var skipped = false;
    var done = false;

    function now() {
        return (Date.now() - start) / 1000;
    }

    function mark(name) {
        if (events[name] === undefined) {
            events[name] = now();
            print("WORLD-LOADING event " + name + " at " + events[name].toFixed(2) + " s");
        }
    }

    Window.worldLoadingChanged.connect(function (loading) {
        mark(loading ? "loadingScreenUp" : "loadingScreenDown");
    });

    var timer = Script.setInterval(function () {
        if (done) {
            return;
        }
        var t = now();
        var physics = Window.isPhysicsEnabled();
        var state = Window.getWorldLoading();
        if (state.active) {
            mark("loadingScreenUp");
            if (state.serverDone) {
                mark("wholeWorldReceived");
            }
        }
        if (physics) {
            mark("physics");
        }
        var count = Entities.findEntities(MyAvatar.position, 100000).length;
        var y = MyAvatar.position.y;
        if (physics) {
            if (physicsY === null) {
                physicsY = y;
            }
            lowestY = lowestY === null ? y : Math.min(lowestY, y);
        }
        samples.push({
            t: t, physics: physics, loading: state.active, received: state.received || 0, ready: state.ready || 0,
            serverDone: !!state.serverDone, entities: count, y: y
        });
        if (SKIP_AFTER >= 0 && !skipped && state.active && t >= SKIP_AFTER) {
            skipped = true;
            mark("showMeNow");
            Window.showWorldNow();
        }
        if (t >= DURATION) {
            done = true;
            Script.clearInterval(timer);
            var summary = {
                events: events,
                entitiesAtPhysics: null,
                entitiesAtEnd: count,
                fallBelowPhysicsStart: physicsY === null ? null : physicsY - lowestY
            };
            for (var i = 0; i < samples.length; i++) {
                if (samples[i].physics) {
                    summary.entitiesAtPhysics = samples[i].entities;
                    break;
                }
            }
            print("WORLD-LOADING summary " + JSON.stringify(summary));
            Test.saveObject({ summary: summary, samples: samples }, "arrival.json");
            Script.setTimeout(function () { Test.quit(); }, 2000);
        }
    }, 250);
}());
