//
//  AvatarPanel.qml
//  qml/darkride
//
//  Darkride's Avatar panel, in the panels' iOS look (darkride/panel), with three tabs (Caity, 10-23):
//  Avatar: the avatar worn (its picture and name), wearing another by its URL, from My Files or from Favorites, the
//    default, its scale, its collisions with the world, and putting it in My Files;
//  Menu: the avatar's own menu, as its package made it (toggles, buttons held while pressed, radial sliders and
//    submenus; MyAvatar.getAvatarMenu()), drawn for whatever avatar is worn; a choice goes to avatarMenu.js on the
//    local channel io.darkride.avatarMenu, which sets the parameter, remembers the saved ones and plays emotes;
//  Nametag: the name and pronoun others see above your head, through the metaverse's API.
//  The bar's Avatar button opens it (tivoli-scripts' ButtonManager.addQmlButton). It replaces the web page of the
//  same name and the Avatar Menu app's web page.
//
//  Copyright 2026 Darkride.
//
//  Distributed under the MIT License.
//

import QtQuick 2.15
import QtQuick.Layouts 1.15
import "panel"

Page {
    id: root

    property real unit: 1
    signal sendToScript(var message)   // the tablet's loader connects it; nothing is sent

    // the tablet's loader sizes the page; a desktop window parents it to its content area
    anchors.fill: parent
    title: "Avatar"
    tabs: ["Avatar", "Menu", "Nametag"]

    readonly property string tabSetting: "io.darkride.avatarPanel.tab"
    Component.onCompleted: currentTab = Math.max(0, Math.min(2, Settings.getValue(tabSetting, 0)))
    onCurrentTabChanged: Settings.setValue(tabSetting, currentTab)

    // a desktop window keeps the page between openings: read the values without a change signal again on each
    onVisibleChanged: {
        if (visible) {
            avatar.refresh();
            menu.reload();
            myFiles.load();
            nametag.load();
        }
    }

    Connections {
        target: MyAvatar
        function onSkeletonModelURLChanged() {
            avatar.refresh();
            menu.reload();
        }
        function onAvatarMenuChanged() {
            menu.reload();
        }
    }

    // ==== Avatar =====================================================================================================

    QtObject {
        id: avatar

        property string url: ""
        property string name: ""
        property string pictureUrl: ""
        property var favorites: []           // the favourites' names, sorted
        property string favoriteName: ""     // the favourite worn, when the avatar came from one
        property bool collisions: true
        property real scale: 1
        property bool adding: false
        property string removing: ""         // the favourite whose Remove waits for a second press

        // the Avatar page's own setting, kept so a favourite worn there is still named here
        readonly property string favoriteNameSetting: "com.tivolicloud.defaultScripts.avatarBookmarkName"

        function refresh() {
            url = String(MyAvatar.getFullAvatarURLFromPreferences());
            name = MyAvatar.getFullAvatarModelName();
            pictureUrl = webUrl(String(MyAvatar.getAvatarPreviewImageURL()).trim());
            favorites = Object.keys(AvatarBookmarks.getBookmarks() || {}).sort(function (a, b) {
                return a.toLowerCase() < b.toLowerCase() ? -1 : 1;
            });
            favoriteName = Settings.getValue(favoriteNameSetting, "");
            collisions = MyAvatar.collisionsEnabled;
            scale = MyAvatar.scale;
        }

        // tea://<user>/<path> is the files host's https://files.<metaverse host>/<user>/<path> (ResourceManager.cpp)
        function webUrl(address) {
            if (/^tea:\/\//i.test(address)) {
                var host = MetaverseRequests.metaverseUrl().replace(/^https?:\/\//, "").replace(/\/.*$/, "");
                return "https://files." + host + "/" + address.substring("tea://".length);
            }
            return address;
        }

        function wear(address, modelName) {
            MyAvatar.useFullAvatarURL(address, modelName);
            MyAvatar.scale = 1;
            Settings.setValue(favoriteNameSetting, "");
            refresh();
        }
        function wearDefault() {
            MyAvatar.randomizeDefaultAvatar();
            MyAvatar.useFullAvatarURL(MyAvatar.getDefaultFullAvatarURL(), "Default");
            MyAvatar.scale = 1;
            Settings.setValue(favoriteNameSetting, "");
            refresh();
        }
        function wearFavorite(favorite) {
            AvatarBookmarks.loadBookmark(favorite);
            Settings.setValue(favoriteNameSetting, favorite);
            refresh();
        }
        function addFavorite(favorite) {
            favorite = favorite.trim();
            if (favorite === "") {
                return;
            }
            AvatarBookmarks.addBookmark(favorite);
            Settings.setValue(favoriteNameSetting, favorite);
            adding = false;
            refresh();
        }
        function removeFavorite(favorite) {
            AvatarBookmarks.removeBookmark(favorite);
            if (favoriteName === favorite) {
                Settings.setValue(favoriteNameSetting, "");
            }
            removing = "";
            refresh();
        }

        Component.onCompleted: refresh()
    }

    // the avatars (.fst files) in My Files; a tea-only (private) one is fetched with your login (the client asks again
    // with it when the files host refuses: main 3c9b0d3468, and the metaverse's private files, 10-24)
    QtObject {
        id: myFiles

        property var avatars: []     // [{ name, url }]
        property int privateCount: 0
        property bool open: false
        property bool busy: false
        property string status: ""

        function load() {
            if (!AccountServices.loggedIn || busy) {
                return;
            }
            busy = true;
            MetaverseRequests.send("GET", "/api/user/files", null, function (error, code, data) {
                busy = false;
                if (error || !data) {
                    status = "Couldn't list My Files: " + error + ".";
                    return;
                }
                var found = [];
                var privates = 0;
                (data.files || []).forEach(function (file) {
                    if (!/\.fst$/i.test(file.key)) {
                        return;
                    }
                    var key = file.key.charAt(0) === "/" ? file.key : "/" + file.key;
                    privates += file.tea ? 1 : 0;
                    found.push({ name: key.split("/").pop().replace(/\.fst$/i, ""), path: key, url: data.url + key,
                                 isPrivate: !!file.tea });
                });
                found.sort(function (a, b) { return a.path.toLowerCase() < b.path.toLowerCase() ? -1 : 1; });
                avatars = found;
                privateCount = privates;
                status = "";
            });
        }

        Component.onCompleted: load()
    }

    Tab {
        visible: root.currentTab === 0

        Section {
            Item {
                width: parent ? parent.width : 0
                height: avatarCard.implicitHeight + 2 * Theme.cardPadding

                Column {
                    id: avatarCard
                    anchors.centerIn: parent
                    width: parent.width - 2 * Theme.cardPadding
                    spacing: 8 * Theme.unit

                    Image {
                        id: avatarPicture
                        anchors.horizontalCenter: parent.horizontalCenter
                        visible: status === Image.Ready
                        width: Math.min(180 * Theme.unit, parent.width)
                        height: visible ? width * sourceSize.height / Math.max(1, sourceSize.width) : 0
                        fillMode: Image.PreserveAspectFit
                        source: avatar.pictureUrl
                        asynchronous: true
                    }
                    Text {
                        width: parent.width
                        horizontalAlignment: Text.AlignHCenter
                        text: avatar.favoriteName !== "" ? avatar.favoriteName : (avatar.name !== "" ? avatar.name : "Your avatar")
                        color: Theme.text
                        font.pixelSize: 18 * Theme.unit
                        font.bold: true
                        elide: Text.ElideMiddle
                    }
                    Text {
                        width: parent.width
                        horizontalAlignment: Text.AlignHCenter
                        text: avatar.url
                        color: Theme.tertiaryText
                        font.pixelSize: Theme.detailSize
                        elide: Text.ElideMiddle
                    }
                }
            }
        }

        Section {
            title: "Wear"
            footer: myFiles.status !== "" ? myFiles.status
                : myFiles.privateCount > 0
                  ? "Private: other players see it when they're on Darkride 1.2 or newer; it can't be downloaded "
                    + "from a browser."
                  : ""

            ListRow {
                label: "URL"
                fill: true
                Field {
                    id: urlField
                    Layout.fillWidth: true
                    text: avatar.url
                    horizontalAlignment: TextInput.AlignLeft
                    placeholderText: "https://…/avatar.fst"
                    onAccepted: if (text.trim() !== "" && text.trim() !== avatar.url) { avatar.wear(text.trim(), text.trim().split("/").pop()); }
                }
            }
            ListRow {
                visible: urlField.text.trim() !== "" && urlField.text.trim() !== avatar.url
                action: true
                label: "Wear This URL"
                onClicked: avatar.wear(urlField.text.trim(), urlField.text.trim().split("/").pop())
            }

            // My Files: a row that opens the list under it
            Column {
                id: filesPicker
                visible: AccountServices.loggedIn
                width: parent ? parent.width : 0

                ListRow {
                    label: "From My Files"
                    cardItem: filesPicker
                    clickable: true
                    onClicked: {
                        myFiles.open = !myFiles.open;
                        if (myFiles.open) {
                            myFiles.load();
                        }
                    }
                    Text {
                        text: myFiles.busy ? "…" : String(myFiles.avatars.length)
                        color: Theme.secondaryText
                        font.pixelSize: Theme.bodySize
                    }
                    Text {
                        text: myFiles.open ? "⌃" : "⌄"
                        color: Theme.tertiaryText
                        font.pixelSize: Theme.bodySize
                    }
                }
                Repeater {
                    model: myFiles.open ? myFiles.avatars : []
                    delegate: ListRow {
                        label: modelData.name
                        detail: modelData.path + (modelData.isPrivate ? " · private" : "")
                        clickable: true
                        onClicked: {
                            avatar.wear(modelData.url, modelData.name);
                            myFiles.open = false;
                        }
                        Text {
                            text: "✓"
                            color: Theme.tint
                            font.pixelSize: Theme.bodySize
                            font.bold: true
                            opacity: modelData.url === avatar.url ? 1 : 0
                        }
                    }
                }
            }
            ListRow {
                action: true
                label: "Use the Default Avatar"
                onClicked: avatar.wearDefault()
            }
        }

        Section {
            title: "Favorites"
            footer: avatar.favorites.length === 0 && !avatar.adding ? "Add the avatar you wear to keep it a click away." : ""

            Repeater {
                model: avatar.favorites
                delegate: ListRow {
                    label: modelData
                    clickable: true
                    onClicked: avatar.wearFavorite(modelData)
                    Text {
                        text: "✓"
                        color: Theme.tint
                        font.pixelSize: Theme.bodySize
                        font.bold: true
                        visible: modelData === avatar.favoriteName
                    }
                    // a first press asks, a second removes
                    Text {
                        text: avatar.removing === modelData ? "Remove" : "−"   // −
                        color: avatar.removing === modelData ? Theme.destructive : Theme.tertiaryText
                        font.pixelSize: Theme.bodySize
                        font.bold: avatar.removing === modelData
                        MouseArea {
                            anchors.fill: parent
                            anchors.margins: -8 * Theme.unit
                            onClicked: {
                                if (avatar.removing === modelData) {
                                    avatar.removeFavorite(modelData);
                                } else {
                                    avatar.removing = modelData;
                                }
                            }
                        }
                    }
                }
            }
            ListRow {
                visible: avatar.adding
                label: "Name"
                fill: true
                Field {
                    id: favoriteField
                    Layout.fillWidth: true
                    horizontalAlignment: TextInput.AlignLeft
                    onAccepted: avatar.addFavorite(text)
                }
            }
            ListRow {
                visible: avatar.adding
                action: true
                label: "Cancel"
                onClicked: avatar.adding = false
            }
            ListRow {
                visible: avatar.adding
                action: true
                label: "Save"
                enabled: favoriteField.text.trim() !== ""
                onClicked: avatar.addFavorite(favoriteField.text)
            }
            ListRow {
                visible: !avatar.adding
                action: true
                label: "Add This Avatar…"
                onClicked: {
                    favoriteField.text = avatar.name;
                    avatar.adding = true;
                    favoriteField.forceActiveFocus();
                    favoriteField.selectAll();
                }
            }
        }

        Section {
            title: "Body"
            footer: "Put This Avatar in My Files uploads an avatar that is on this computer, so everyone can see it."

            ListRow {
                label: "Scale"
                fill: true
                labelWidth: 80 * Theme.unit
                scrubbable: true
                scrubValue: avatar.scale
                scrubFrom: 0.1
                scrubTo: 4
                scrubStep: 0.01
                onScrubbed: { MyAvatar.scale = Math.round(value * 100) / 100; avatar.refresh(); }

                ValueSlider {
                    from: 0.25; to: 3; stepSize: 0.05; decimals: 2; units: "×"
                    value: avatar.scale
                    onMoved: { MyAvatar.scale = value; avatar.refresh(); }
                }
                ResetButton {
                    needed: Math.abs(avatar.scale - 1) > 0.005
                    onClicked: { MyAvatar.scale = 1; avatar.refresh(); }
                }
            }
            ListRow {
                label: "Collide with the world"
                detail: "off: walk through walls (noclip)"
                Toggle {
                    checked: avatar.collisions
                    onToggled: { MyAvatar.collisionsEnabled = checked; avatar.refresh(); }
                }
            }
            ListRow {
                action: true
                label: "Put This Avatar in My Files"
                onClicked: MenuInterface.triggerOption("Put Avatar in My Files")
            }
        }
    }

    // ==== Menu =======================================================================================================

    QtObject {
        id: menu

        property var avatarMenu: ({})   // MyAvatar.getAvatarMenu(): { menu: { controls }, parameters: [{ name, value, saved }] }
        property var stack: []          // the submenus opened, as indexes
        property var values: ({})       // each parameter's value, read ten times a second while the tab shows
        property string folder: ""      // the menu's icons sit beside the .fst

        readonly property bool hasMenu: !!(avatarMenu.menu && avatarMenu.menu.controls && avatarMenu.menu.controls.length)
        readonly property var controls: {
            var level = hasMenu ? avatarMenu.menu.controls : [];
            for (var i = 0; i < stack.length; i++) {
                level = (level[stack[i]] && level[stack[i]].controls) || [];
            }
            return level;
        }
        readonly property string path: {
            var names = [];
            var level = hasMenu ? avatarMenu.menu.controls : [];
            for (var i = 0; i < stack.length; i++) {
                if (!level[stack[i]]) {
                    break;
                }
                names.push(clean(level[stack[i]].name));
                level = level[stack[i]].controls || [];
            }
            return names.join(" › ");   // ›
        }

        function clean(text) {
            // a package's names may carry rich-text tags and escaped line breaks
            return String(text || "").replace(/<[^>]*>/g, "").replace(/\\n/g, " ").trim();
        }

        function reload() {
            avatarMenu = MyAvatar.getAvatarMenu() || {};
            stack = [];
            var address = String(MyAvatar.skeletonModelURL);
            folder = address.substring(0, address.lastIndexOf("/") + 1);
            poll();
        }

        function poll() {
            var read = {};
            (avatarMenu.parameters || []).forEach(function (parameter) {
                read[parameter.name] = MyAvatar.getAvatarParameter(parameter.name);
            });
            values = read;
        }

        function set(name, value) {
            // avatarMenu.js sets it, remembers it when the avatar saves it, and plays an emote it starts
            Messages.sendLocalMessage("io.darkride.avatarMenu", JSON.stringify({ type: "set", name: name, value: value }));
            var shown = Object.assign({}, values);
            shown[name] = value;
            values = shown;
        }

        function open(index) {
            stack = stack.concat([index]);
        }
        function back() {
            stack = stack.slice(0, -1);
        }

        function same(a, b) {
            return a !== undefined && Math.abs(a - b) < 0.001;
        }

        Component.onCompleted: reload()
    }

    Timer {
        interval: 100
        repeat: true
        running: root.visible && root.currentTab === 1 && menu.hasMenu
        onTriggered: menu.poll()
    }

    Tab {
        visible: root.currentTab === 1

        Section {
            title: menu.stack.length > 0 ? menu.path : "Avatar menu"
            footer: !menu.hasMenu ? "This avatar has no menu. Avatars imported from Unity packages bring theirs: "
                                    + "clothes to show or hide, expressions, emotes."
                  : "Buttons act while pressed. Choices the avatar saves are put back the next time you wear it."

            ListRow {
                visible: menu.stack.length > 0
                action: true
                label: "‹ Back"   // ‹
                onClicked: menu.back()
            }

            Repeater {
                model: menu.controls
                delegate: ListRow {
                    id: controlRow
                    readonly property string type: modelData.type || ""
                    readonly property string target: type === "radial"
                        ? ((modelData.subParameters && modelData.subParameters[0]) || modelData.parameter || "")
                        : (modelData.parameter || "")
                    readonly property bool known: type === "toggle" || type === "button" || type === "submenu"
                        || type === "radial"

                    label: menu.clean(modelData.name)
                    icon: modelData.icon ? menu.folder + encodeURI(modelData.icon) : ""
                    detail: known ? (type === "button" ? "hold" : "") : type + ", not yet"
                    enabled: known
                    fill: type === "radial"
                    clickable: type === "button" || type === "submenu"
                    onClicked: if (type === "submenu") { menu.open(index); }
                    onPressed: if (type === "button") { menu.set(target, modelData.value); }
                    onReleased: if (type === "button") { menu.set(target, 0); }

                    Toggle {
                        visible: controlRow.type === "toggle"
                        checked: menu.same(menu.values[controlRow.target], modelData.value)
                        // a menu toggle: on sets its value, off puts the parameter back to 0
                        onToggled: menu.set(controlRow.target, checked ? modelData.value : 0)
                    }
                    Rectangle {
                        visible: controlRow.type === "button"
                        implicitWidth: 10 * Theme.unit
                        implicitHeight: implicitWidth
                        radius: width / 2
                        color: menu.same(menu.values[controlRow.target], modelData.value) ? Theme.on : Theme.fill
                    }
                    ValueSlider {
                        visible: controlRow.type === "radial"
                        from: 0; to: 1; stepSize: 0.01; decimals: 2
                        value: menu.values[controlRow.target] || 0
                        onMoved: menu.set(controlRow.target, value)
                    }
                    Text {
                        visible: controlRow.type === "submenu"
                        text: "›"
                        color: Theme.tertiaryText
                        font.pixelSize: 18 * Theme.unit
                    }
                }
            }
        }
    }

    // ==== Nametag ====================================================================================================

    QtObject {
        id: nametag

        readonly property bool signedIn: AccountServices.loggedIn
        readonly property string username: AccountServices.username
        property bool busy: false
        property string status: ""
        property bool rejoinNeeded: false
        property string pictureStamp: ""

        function load() {
            if (!signedIn || busy) {   // a first desktop opening asks twice: on creation, then on showing
                return;
            }
            busy = true;
            MetaverseRequests.send("GET", "/api/user/nametag-details", null, function (error, code, data) {
                busy = false;
                if (error) {
                    status = "Couldn't read your nametag: " + error + ".";
                    return;
                }
                displayNameField.text = data && data.displayName ? data.displayName : "";
                pronounChoices.current = data && data.genderPronoun ? data.genderPronoun : "";
                status = "";
            });
            pictureStamp = String(Date.now());
        }

        function save() {
            busy = true;
            status = "Saving…";
            MetaverseRequests.send("POST", "/api/user/nametag-details",
                                   { displayName: displayNameField.text, genderPronoun: pronounChoices.current },
                                   function (error, code, data) {
                busy = false;
                if (error) {
                    status = "Not saved: " + error + ".";
                    return;
                }
                status = "Saved. Others see it after you rejoin the world.";
                rejoinNeeded = true;
                pictureStamp = String(Date.now());
            });
        }

        function rejoin() {
            rejoinTimer.target = String(AddressManager.href);
            AddressManager.handleLookupString("file:///");
            rejoinTimer.start();
            rejoinNeeded = false;
            status = "";
        }

        Component.onCompleted: load()
        onSignedInChanged: load()
    }

    Timer {
        id: rejoinTimer
        property string target: ""
        interval: 1000
        onTriggered: AddressManager.handleLookupString(target)
    }

    Tab {
        visible: root.currentTab === 2

        Section {
            title: "Nametag"
            footer: nametag.signedIn
                ? (nametag.status !== "" ? nametag.status : "Whether nametags show at all is in Settings, Displays.")
                : "Sign in to set the name and pronoun others see above your head."

            Item {
                visible: nametag.signedIn && nametagPicture.status === Image.Ready
                width: parent ? parent.width : 0
                height: visible ? nametagPicture.height + 2 * Theme.cardPadding : 0

                Image {
                    id: nametagPicture
                    anchors.centerIn: parent
                    width: Math.min(sourceSize.width, parent.width - 2 * Theme.cardPadding)
                    height: width * sourceSize.height / Math.max(1, sourceSize.width)
                    fillMode: Image.PreserveAspectFit
                    cache: false
                    source: nametag.signedIn && nametag.username !== ""
                        ? MetaverseRequests.metaverseUrl() + "/api/user/" + nametag.username.toLowerCase()
                          + "/nametag?" + nametag.pictureStamp
                        : ""
                }
            }
            ListRow {
                visible: nametag.signedIn
                label: "Display name"
                fill: true
                Field {
                    id: displayNameField
                    Layout.fillWidth: true
                    enabled: !nametag.busy
                    placeholderText: nametag.username
                }
            }
            ListRow {
                visible: nametag.signedIn
                label: "Pronoun"
                Segmented {
                    id: pronounChoices
                    enabled: !nametag.busy
                    current: ""
                    model: [{ text: "None", value: "" }, { text: "She/Her", value: "she/her" },
                            { text: "He/Him", value: "he/him" }, { text: "They/Them", value: "they/them" }]
                    onChosen: current = value
                }
            }
            ListRow {
                visible: nametag.signedIn
                action: true
                label: "Save"
                enabled: !nametag.busy
                onClicked: nametag.save()
            }
            ListRow {
                visible: nametag.signedIn && nametag.rejoinNeeded
                action: true
                label: "Rejoin the World"
                onClicked: nametag.rejoin()
            }
            ListRow {
                visible: nametag.signedIn
                action: true
                label: "Change Picture…"
                onClicked: Qt.openUrlExternally(MetaverseRequests.metaverseUrl() + "/user/settings")
            }
        }
    }
}
