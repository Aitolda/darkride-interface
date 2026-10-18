"use strict";
//
//  worldLoading.js
//  scripts/system
//
//  The loading screen of whole-world loading. A domain's owner can ask (the domain's settings, Loading) that visitors
//  see the world only once all of it has arrived and its models and textures have loaded. While the client loads it
//  (Window.getWorldLoading().active), this puts a dark sphere round the camera and a card in front of it: the world's
//  name, how much has arrived and loaded, a progress bar, the time left before the screen lifts anyway, and a
//  "Show me now" button (or Enter) that lifts it at once. Everything here is local entities that follow the camera,
//  so it works on a desktop and in a headset alike.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

/* global Script, Entities, MyAvatar, Window, Controller, location */

(function () {
    // Darkride's colours (the website's --dr-* tokens)
    var COLOR_BG = { red: 0x1c, green: 0x21, blue: 0x36 };
    var COLOR_TRACK = { red: 0x33, green: 0x3a, blue: 0x5a };
    var COLOR_CYAN = { red: 0x00, green: 0xaf, blue: 0xff };
    var COLOR_CYAN_HOVER = { red: 0x4d, green: 0xc8, blue: 0xff };
    var COLOR_NIGHT = { red: 0x09, green: 0x10, blue: 0x23 };
    var COLOR_TEXT = { red: 0xf4, green: 0xf6, blue: 0xf8 };
    var COLOR_MUTED = { red: 0x9a, green: 0xa1, blue: 0xbd };

    var SPHERE_URL = Script.resolvePath("/~/system/assets/models/black-sphere.fbx");
    var LOGO_URL = Script.resourcesPath() + "images/darkride-logo.png";

    var CARD_DISTANCE = 1.1;     // metres in front of the camera: nearer than the avatar's body in third person
    var SCALE = CARD_DISTANCE / 2.2;   // the card's sizes below are for 2.2 m; nearer, smaller, so it looks the same
    var CARD_WIDTH = 1.6;
    var CARD_HEIGHT = 0.9;
    var BAR_WIDTH = 1.36;
    var BAR_HEIGHT = 0.028;
    var SPHERE_SIZE = 6.0;       // hides everything farther than 3 m
    var TEXT_INTERVAL = 100;     // ms between updates of the card's text

    var ids = {};                // the screen's entities while it is up
    var textTimer = null;
    var shownProgress = 0.0;
    var peakWaiting = 0;

    function worldName() {
        return location.placename || location.hostname || "this world";
    }

    // a point or a size on the card, from the card's units
    function card(x, y, z) {
        return { x: SCALE * x, y: SCALE * y, z: SCALE * z };
    }

    function text(name, properties) {
        var defaults = {
            type: "Text",
            name: "World loading " + name,
            parentID: ids.card,
            unlit: true,
            backgroundAlpha: 0.0,
            textColor: COLOR_TEXT,
            leftMargin: SCALE * 0.0,
            topMargin: SCALE * 0.0,
            ignorePickIntersection: true,
            collisionless: true,
            drawInFront: true  // over anything inside the sphere, the avatar's own body included
        };
        for (var key in properties) {
            if (properties.hasOwnProperty(key)) {
                defaults[key] = properties[key];
            }
        }
        return Entities.addEntity(defaults, "local");
    }

    function show() {
        if (ids.card) {
            return;
        }
        var cameraJoint = MyAvatar.getJointIndex("_CAMERA_MATRIX");
        ids.sphere = Entities.addEntity({
            type: "Model",
            name: "World loading sphere",
            modelURL: SPHERE_URL,
            parentID: MyAvatar.SELF_ID,
            parentJointIndex: cameraJoint,
            localPosition: { x: 0, y: 0, z: 0 },
            dimensions: { x: SPHERE_SIZE, y: SPHERE_SIZE, z: SPHERE_SIZE },
            ignorePickIntersection: true,
            collisionless: true
        }, "local");
        ids.card = Entities.addEntity({
            type: "Text",
            name: "World loading card",
            parentID: MyAvatar.SELF_ID,
            parentJointIndex: cameraJoint,
            localPosition: { x: 0, y: 0, z: -CARD_DISTANCE },
            localRotation: { x: 0, y: 0, z: 0, w: 1 },
            dimensions: card(CARD_WIDTH, CARD_HEIGHT, 0.01),
            text: "",
            unlit: true,
            backgroundColor: COLOR_BG,
            backgroundAlpha: 1.0,
            ignorePickIntersection: true,
            collisionless: true,
            drawInFront: true
        }, "local");

        var left = -CARD_WIDTH / 2 + 0.12;  // the card's inner left edge
        ids.logo = Entities.addEntity({
            type: "Image",
            name: "World loading logo",
            parentID: ids.card,
            imageURL: LOGO_URL,
            emissive: true,
            keepAspectRatio: true,
            localPosition: card(left + 0.07, 0.30, 0.006),
            dimensions: card(0.14, 0.14, 0.01),
            ignorePickIntersection: true,
            collisionless: true,
            drawInFront: true
        }, "local");
        ids.title = text("title", {
            text: "Loading " + worldName(),
            lineHeight: SCALE * 0.085,
            localPosition: card(0.10, 0.30, 0.006),
            dimensions: card(CARD_WIDTH - 0.44, 0.12, 0.01)
        });
        ids.status = text("status", {
            text: "Waiting for the world",
            lineHeight: SCALE * 0.05,
            textColor: COLOR_MUTED,
            localPosition: card(0, 0.10, 0.006),
            dimensions: card(BAR_WIDTH, 0.08, 0.01)
        });
        ids.track = text("track", {
            text: "",
            backgroundColor: COLOR_TRACK,
            backgroundAlpha: 1.0,
            localPosition: card(0, -0.02, 0.006),
            dimensions: card(BAR_WIDTH, BAR_HEIGHT, 0.01)
        });
        ids.fill = text("fill", {
            text: "",
            backgroundColor: COLOR_CYAN,
            backgroundAlpha: 1.0,
            localPosition: card(-BAR_WIDTH / 2 + 0.005, -0.02, 0.008),
            dimensions: card(0.01, BAR_HEIGHT, 0.01)
        });
        ids.countdown = text("countdown", {
            text: "",
            lineHeight: SCALE * 0.04,
            textColor: COLOR_MUTED,
            localPosition: card(-0.24, -0.27, 0.006),
            dimensions: card(0.88, 0.07, 0.01)
        });
        ids.button = text("button", {
            text: "Show me now",
            lineHeight: SCALE * 0.05,
            textColor: COLOR_NIGHT,
            backgroundColor: COLOR_CYAN,
            backgroundAlpha: 1.0,
            leftMargin: SCALE * 0.06,
            topMargin: SCALE * 0.025,
            localPosition: card(CARD_WIDTH / 2 - 0.12 - 0.19, -0.27, 0.006),
            dimensions: card(0.38, 0.1, 0.01),
            ignorePickIntersection: false
        });

        shownProgress = 0.0;
        peakWaiting = 0;
        textTimer = Script.setInterval(updateCard, TEXT_INTERVAL);
        updateCard();
    }

    function hide() {
        if (textTimer) {
            Script.clearInterval(textTimer);
            textTimer = null;
        }
        // the card's children go with it
        if (ids.card) {
            Entities.deleteEntity(ids.card);
        }
        if (ids.sphere) {
            Entities.deleteEntity(ids.sphere);
        }
        ids = {};
    }

    function formatCount(n) {
        return String(n).replace(/\B(?=(\d{3})+(?!\d))/g, ",");
    }

    function updateCard() {
        var state = Window.getWorldLoading();
        if (!state.active) {
            hide();
            return;
        }
        var progress, status;
        if (!state.serverDone) {
            // the total is not known until the entity server has sent everything: creep towards the first part
            progress = 0.15 * (1.0 - Math.exp(-state.received / 500.0));
            status = state.received > 0 ? "Receiving the world: " + formatCount(state.received) + " things so far" :
                "Waiting for the world";
        } else {
            // what is still loading near the arrival (the farther ones count as ready), against the most that ever
            // waited, so the bar moves while the last models load rather than resting at 99 %
            var waiting = Math.max(0, state.received - state.ready);
            peakWaiting = Math.max(peakWaiting, waiting, 1);
            progress = 0.15 + 0.85 * (1 - waiting / peakWaiting);
            status = waiting > 0 ? "Loading models and textures: " + formatCount(waiting) + " still to come" :
                "Settling textures";
        }
        // never backwards, and eased so a jump in the counts does not jerk the bar
        var target = Math.max(shownProgress, Math.min(progress, 1.0));
        shownProgress += (target - shownProgress) * 0.35;

        var fillWidth = Math.max(0.01, BAR_WIDTH * shownProgress);
        Entities.editEntity(ids.fill, {
            localPosition: card(-BAR_WIDTH / 2 + fillWidth / 2, -0.02, 0.008),
            dimensions: card(fillWidth, BAR_HEIGHT, 0.01)
        });
        Entities.editEntity(ids.status, { text: status });
        Entities.editEntity(ids.countdown, {
            text: "Showing the world anyway in " + Math.ceil(state.secondsLeft) + " s"
        });
    }

    function onWorldLoadingChanged(loading) {
        if (loading) {
            show();
        } else {
            hide();
        }
    }

    function onMousePress(entityID, event) {
        if (ids.button && entityID === ids.button && event.isPrimaryButton) {
            Window.showWorldNow();
        }
    }

    function onHoverEnter(entityID) {
        if (ids.button && entityID === ids.button) {
            Entities.editEntity(ids.button, { backgroundColor: COLOR_CYAN_HOVER });
        }
    }

    function onHoverLeave(entityID) {
        if (ids.button && entityID === ids.button) {
            Entities.editEntity(ids.button, { backgroundColor: COLOR_CYAN });
        }
    }

    function onKeyPress(event) {
        if (ids.card && (event.key === 16777220 || event.key === 16777221)) {  // Return, Enter
            Window.showWorldNow();
        }
    }

    Window.worldLoadingChanged.connect(onWorldLoadingChanged);
    Entities.mousePressOnEntity.connect(onMousePress);
    Entities.hoverEnterEntity.connect(onHoverEnter);
    Entities.hoverLeaveEntity.connect(onHoverLeave);
    Controller.keyPressEvent.connect(onKeyPress);

    // loading may already be under way when this script starts
    if (Window.getWorldLoading().active) {
        show();
    }

    Script.scriptEnding.connect(function () {
        hide();
        Window.worldLoadingChanged.disconnect(onWorldLoadingChanged);
        Entities.mousePressOnEntity.disconnect(onMousePress);
        Entities.hoverEnterEntity.disconnect(onHoverEnter);
        Entities.hoverLeaveEntity.disconnect(onHoverLeave);
        Controller.keyPressEvent.disconnect(onKeyPress);
    });
}());
