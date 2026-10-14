"use strict";

//  avatarMenu.js
//
//  The Avatar Menu app: an imported avatar's menu, with its toggles, buttons and submenus. Its button is on the tablet while the avatar has a menu. Choices set the avatar's
//  parameters (MyAvatar.setAvatarParameter), which drive its effects layer: clothes shown or hidden, shape
//  keys, bones. Parameters the avatar marks as saved are remembered per avatar and put back the next time it's worn.
//
//  Created on 2026-10-14 for Darkride's import of avatars from Unity packages.
//
//  Distributed under the MIT License.
//  See the accompanying file LICENSE-MIT or https://opensource.org/licenses/MIT

/* globals Script, Tablet, MyAvatar, Settings */

(function () {
    var APP_BASE = "html/AvatarMenuApp.html";
    var APP_URL = Script.resolvePath(APP_BASE);
    var APP_LABEL = "Avatar Menu";
    var APP_SORT_ORDER = 13;
    var SAVED_SETTING = "io.darkride.avatarMenu.saved";  // {avatar URL: {parameter: value}}

    var tablet = Tablet.getTablet("com.highfidelity.interface.tablet.system");
    var button = null;
    var onScreen = false;

    function savedValues() {
        var all = Settings.getValue(SAVED_SETTING, {}) || {};
        return all[MyAvatar.skeletonModelURL] || {};
    }

    function save(name, value) {
        var all = Settings.getValue(SAVED_SETTING, {}) || {};
        var mine = all[MyAvatar.skeletonModelURL] || {};
        mine[name] = value;
        all[MyAvatar.skeletonModelURL] = mine;
        Settings.setValue(SAVED_SETTING, all);
    }

    function hasMenu(menuData) {
        return menuData && menuData.menu && menuData.menu.controls && menuData.menu.controls.length > 0;
    }

    function sendState() {
        var menuData = MyAvatar.getAvatarMenu();
        var url = MyAvatar.skeletonModelURL;
        tablet.emitScriptEvent(JSON.stringify({
            app: "avatarMenu",
            type: "avatarMenu",
            menu: menuData.menu,
            parameters: menuData.parameters,
            folder: url.substring(0, url.lastIndexOf("/") + 1)  // the menu's icons sit beside the .fst
        }));
    }

    function onClicked() {
        if (onScreen) {
            tablet.gotoHomeScreen();
        } else {
            onScreen = true;
            tablet.gotoWebScreen(APP_URL);
        }
    }

    function onAvatarMenuChanged() {
        var menuData = MyAvatar.getAvatarMenu();
        if (hasMenu(menuData)) {
            var saved = savedValues();
            menuData.parameters.forEach(function (parameter) {
                if (parameter.saved && saved[parameter.name] !== undefined) {
                    MyAvatar.setAvatarParameter(parameter.name, saved[parameter.name]);
                }
            });
            if (!button) {
                button = tablet.addButton({
                    icon: "icons/tablet-icons/avatar-i.svg",
                    activeIcon: "icons/tablet-icons/avatar-a.svg",
                    text: APP_LABEL,
                    sortOrder: APP_SORT_ORDER
                });
                button.clicked.connect(onClicked);
            }
        } else if (button) {
            if (onScreen) {
                tablet.gotoHomeScreen();
            }
            button.clicked.disconnect(onClicked);
            tablet.removeButton(button);
            button = null;
        }
        if (onScreen) {
            sendState();
        }
    }

    function onScreenChanged(type, url) {
        onScreen = type === "Web" && url.indexOf(APP_BASE) !== -1;
        if (button) {
            button.editProperties({ isActive: onScreen });
        }
    }

    function onWebEventReceived(event) {
        if (typeof event === "string") {
            try {
                event = JSON.parse(event);
            } catch (e) {
                return;
            }
        }
        if (!event || event.app !== "avatarMenu") {
            return;  // the tablet's web events reach every app
        }
        if (event.type === "ready") {
            sendState();
        } else if (event.type === "set") {
            MyAvatar.setAvatarParameter(event.name, event.value);
            var parameter = MyAvatar.getAvatarMenu().parameters.filter(function (p) {
                return p.name === event.name;
            })[0];
            if (parameter && parameter.saved) {
                save(event.name, event.value);
            }
            sendState();
        }
    }

    MyAvatar.avatarMenuChanged.connect(onAvatarMenuChanged);
    tablet.screenChanged.connect(onScreenChanged);
    tablet.webEventReceived.connect(onWebEventReceived);
    Script.scriptEnding.connect(function () {
        MyAvatar.avatarMenuChanged.disconnect(onAvatarMenuChanged);
        tablet.screenChanged.disconnect(onScreenChanged);
        tablet.webEventReceived.disconnect(onWebEventReceived);
        if (onScreen) {
            tablet.gotoHomeScreen();
        }
        if (button) {
            button.clicked.disconnect(onClicked);
            tablet.removeButton(button);
        }
    });
    onAvatarMenuChanged();
}());
