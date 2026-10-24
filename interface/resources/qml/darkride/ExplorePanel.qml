//
//  ExplorePanel.qml
//  qml/darkride
//
//  Darkride's Explore panel, in the panels' iOS look (darkride/panel): worlds to go to, in three lists (Popular, with
//  the worlds that open as instances; Liked; Private, your own), searchable; each a card with its picture, author,
//  likes, who is there, and Hop (where the people are, else somewhere running, else a new public instance), with the
//  places running now and new instances (public, friends, private) under More. The instance you are in has its own
//  card: its link, edit mode, save, close. Back and Forward walk your history of places.
//
//  The version check (with the metaverse's version-check, Caity's yes 10-24): the client's protocol signature goes with
//  a list's request and with an instance's start, so a world or an instance running another version of Darkride is
//  shown as such and can't be hopped to, and a start the servers can't serve is refused at once (409) instead of
//  after a burst server boots and the world turns the client away. An empty protocol counts as compatible.
//
//  A hop takes the screen at once (worldLoading.js, on the local "Darkride-Hop" channel) and Explore closes; the
//  join is AddressManager's. It replaces the web page of the same name and its handler (tivoli-scripts explore).
//
//  Copyright 2026 Darkride.
//
//  Distributed under the MIT License.
//

import QtQuick 2.15
import QtQuick.Layouts 1.15
import TabletScriptingInterface 1.0
import "panel"

Page {
    id: root

    property real unit: 1
    signal sendToScript(var message)   // the tablet's loader connects it; nothing is sent

    anchors.fill: parent
    title: "Explore"
    tabs: ["Popular", "Liked", "Private"]

    readonly property var kinds: ["popular", "liked", "private"]
    readonly property string tabSetting: "io.darkride.explorePanel.tab"
    readonly property string protocol: Window.protocolSignature()
    Component.onCompleted: {
        currentTab = Math.max(0, Math.min(2, Settings.getValue(tabSetting, 0)));
        explore.load(true);
        here.refresh();
    }
    onCurrentTabChanged: {
        Settings.setValue(tabSetting, currentTab);
        explore.load(true);
    }
    onVisibleChanged: {
        if (visible) {
            explore.load(true);
            here.refresh();
        }
    }

    // ==== the worlds ==================================================================================================

    QtObject {
        id: explore

        property var worlds: []
        property int page: 1
        property bool loading: false
        property bool more: false          // the last page was full: there may be another
        property string status: ""
        property string search: ""
        property string hoppingWorld: ""   // a hop under way, so a second click doesn't start another
        readonly property int amount: 50

        function query(extra) {
            var parts = ["page=" + page, "amount=" + amount, "search=" + encodeURIComponent(search),
                         "protocol=" + encodeURIComponent(root.protocol)];
            return "?" + parts.concat(extra || []).join("&");
        }

        function load(reset) {
            page = reset ? 1 : page + 1;
            loading = true;
            var kind = root.kinds[root.currentTab];
            var path = kind === "liked" ? "/api/user/domains/liked" : kind === "private" ? "/api/user/domains/private"
                : "/api/domains";
            var pending = kind === "popular" && page === 1 ? 2 : 1;
            var online = [];
            var withInstances = [];
            var done = function () {
                if (--pending > 0) {
                    return;
                }
                loading = false;
                var merged = merge(online, withInstances);
                more = online.length >= amount;
                worlds = reset ? merged : worlds.concat(merged);
            };
            MetaverseRequests.send("GET", path + query(), null, function (error, code, data) {
                if (error) {
                    status = "Couldn't list the worlds: " + error + ".";
                } else {
                    status = "";
                    online = Array.isArray(data) ? data : [];
                }
                done();
            });
            if (pending === 2) {
                // the worlds that open as instances are listed with the online ones, whether or not anyone is in them
                MetaverseRequests.send("GET", "/api/worlds?search=" + encodeURIComponent(search), null,
                                       function (error, code, data) {
                    withInstances = !error && Array.isArray(data) ? data : [];
                    done();
                });
            }
        }

        function people(world) {
            return (world.numUsers || 0) + ((world.instances && world.instances.list) || []).reduce(function (n, i) {
                return n + (i.players || 0);
            }, 0);
        }

        // the online worlds as listed, each replaced by its record with instances when there is one; then the rest of
        // the instance worlds, those with people in them first
        function merge(online, withInstances) {
            var byId = {};
            withInstances.forEach(function (world) { byId[world.id] = world; });
            var merged = online.map(function (world) { return byId[world.id] || world; });
            var listed = {};
            merged.forEach(function (world) { listed[world.id] = true; });
            var rest = withInstances.filter(function (world) { return !listed[world.id]; })
                .sort(function (a, b) { return people(b) - people(a); });
            return merged.concat(rest.filter(function (w) { return people(w) > 0; }))
                .concat(rest.filter(function (w) { return people(w) === 0; }));
        }

        function compatible(protocol) {
            return !protocol || !root.protocol || protocol === root.protocol;
        }

        function picture(worldId) {
            return MetaverseRequests.metaverseUrl() + "/api/domain/" + worldId + "/image";
        }

        // a hop: the loading screen and the pop at once, Explore out of the way, then the join
        function hopStart(world) {
            Tablet.playSound(TabletEnums.TabletOpen);
            Messages.sendLocalMessage("Darkride-Hop", JSON.stringify({
                type: "start", world: world.label || "", author: world.username || "",
                picture: world.id ? picture(world.id) : ""
            }));
            root.closePanel();
        }
        function joinWorld(world) {
            hoppingWorld = world.id;
            hopStart(world);
            AddressManager.handleLookupString(world.id + (world.path || ""));
        }
        function joinInstance(world, instance) {
            hoppingWorld = world.id;
            hopStart(world);
            AddressManager.handleLookupString(instance.address);
        }
        // a new instance: started first, so a refusal (another version, the servers full) is said here at once
        function startInstance(world, visibility, done) {
            if (hoppingWorld !== "") {
                return;
            }
            hoppingWorld = world.id;
            MetaverseRequests.send("POST", "/api/worlds/" + world.id + "/instances",
                                   { visibility: visibility, protocol: root.protocol }, function (error, code, data) {
                if (error || !data || !data.address) {
                    hoppingWorld = "";
                    done(data && data.message ? String(data.message) : "Couldn't start an instance: " + error + ".");
                    return;
                }
                done("");
                joinInstance(world, data);
            });
        }

        function like(world, on, done) {
            MetaverseRequests.send("POST", "/api/domains/" + world.id + "/" + (on ? "like" : "unlike"), null,
                                   function (error) {
                if (!error) {
                    done();
                }
            });
        }
    }

    Connections {
        target: AddressManager
        function onHostChanged() { explore.hoppingWorld = ""; here.refresh(); }
    }
    Connections {
        target: Window
        function onDomainChanged() { explore.hoppingWorld = ""; here.refresh(); }
    }

    // ==== the instance you are in ======================================================================================

    QtObject {
        id: here

        property var current: null     // { world, instance, editingBy } when the world you're in is an instance
        property bool busy: false
        property string status: ""
        property bool confirmClose: false

        function domainId() {
            return String(AddressManager.domainID).replace("{", "").replace("}", "");
        }

        function refresh() {
            var id = domainId();
            if (id === "" || id === "00000000-0000-0000-0000-000000000000") {
                current = null;
                return;
            }
            MetaverseRequests.send("GET", "/api/instances/by-domain/" + id, null, function (error, code, data) {
                current = !error && data && data.instance ? data : null;
            });
        }

        function editMode(on) {
            busy = true;
            MetaverseRequests.send("POST", "/api/instances/" + current.instance.id + "/edit", { on: on },
                                   function (error, code, data) {
                busy = false;
                if (error) {
                    status = "Couldn't change edit mode: " + ((data && data.message) || error) + ".";
                    return;
                }
                current = data;
                status = on ? "Edit mode is on: you can build here. Save to update the world." : "Edit mode is off.";
            });
        }

        function save() {
            busy = true;
            MetaverseRequests.send("POST", "/api/instances/" + current.instance.id + "/save", null,
                                   function (error, code, data) {
                busy = false;
                status = error ? "Couldn't save: " + ((data && data.message) || error) + "."
                    : "Saved as version " + data.version + ", " + data.entities + " entities.";
                refresh();
            });
        }

        function close() {
            busy = true;
            confirmClose = false;
            MetaverseRequests.send("DELETE", "/api/instances/" + current.instance.id, null, function (error, code, data) {
                busy = false;
                status = error ? "Couldn't close the instance: " + ((data && data.message) || error) + "." : "";
                if (!error) {
                    current = null;
                }
            });
        }
    }

    // ==== the page ====================================================================================================

    // searching, and walking back and forth through the places you've been
    Section {
        ListRow {
            label: "Search"
            fill: true
            Field {
                id: searchField
                Layout.fillWidth: true
                horizontalAlignment: TextInput.AlignLeft
                placeholderText: "worlds by name"
                onTextChanged: searchDelay.restart()
                onAccepted: { searchDelay.stop(); explore.search = text.trim(); explore.load(true); }
            }
        }
        ListRow {
            action: true
            label: "‹ Back"
            visible: history.canGoBack
            onClicked: AddressManager.goBack()
        }
        ListRow {
            action: true
            label: "Forward ›"
            visible: history.canGoForward
            onClicked: AddressManager.goForward()
        }
    }
    Timer {
        id: searchDelay
        interval: 400
        onTriggered: { explore.search = searchField.text.trim(); explore.load(true); }
    }
    QtObject {
        id: history
        property bool canGoBack: false
        property bool canGoForward: false
    }
    Connections {
        target: AddressManager
        function onGoBackPossible(possible) { history.canGoBack = possible; }
        function onGoForwardPossible(possible) { history.canGoForward = possible; }
    }

    // the instance you're in
    Section {
        visible: here.current !== null
        title: here.current ? "You're in " + here.current.world.label : ""
        footer: here.status !== "" ? here.status
            : here.current && here.current.editingBy ? "Being edited by " + here.current.editingBy + "." : ""

        ListRow {
            label: here.current && here.current.instance
                ? (here.current.instance.visibility.charAt(0).toUpperCase() + here.current.instance.visibility.slice(1))
                  + " instance"
                : ""
            detail: here.current && here.current.instance
                ? here.current.instance.players + (here.current.instance.players === 1 ? " person" : " people")
                  + (here.current.instance.latest ? "" : " · an older version")
                : ""
            Text {
                text: here.current && here.current.instance ? here.current.instance.address : ""
                color: Theme.tertiaryText
                font.pixelSize: Theme.detailSize
                elide: Text.ElideMiddle
                Layout.maximumWidth: 200 * Theme.unit
            }
        }
        ListRow {
            action: true
            label: "Copy Link"
            onClicked: {
                Window.copyToClipboard(here.current.instance.address);
                here.status = "Copied " + here.current.instance.address + ".";
            }
        }
        ListRow {
            visible: here.current !== null && here.current.world.instances && here.current.world.instances.canEdit
            label: "Edit mode"
            detail: "build here, then save to update the world"
            Toggle {
                checked: here.current !== null && here.current.instance && here.current.instance.editing
                enabled: !here.busy
                onToggled: here.editMode(checked)
            }
        }
        ListRow {
            visible: here.current !== null && here.current.instance && here.current.instance.editing
            action: true
            label: "Save the World"
            enabled: !here.busy
            onClicked: here.save()
        }
        ListRow {
            visible: here.current !== null && here.current.instance && here.current.instance.canStop && !here.confirmClose
            action: true
            destructive: true
            label: "Close This Instance…"
            onClicked: here.confirmClose = true
        }
        ListRow {
            visible: here.confirmClose
            action: true
            label: "Cancel"
            onClicked: here.confirmClose = false
        }
        ListRow {
            visible: here.confirmClose
            action: true
            destructive: true
            label: "Close It for Everyone"
            enabled: !here.busy
            onClicked: here.close()
        }
    }

    // a small button inside a row or a card
    component CardButton: Rectangle {
        id: cardButton
        property string text: ""
        property bool strong: false
        signal pressed()
        implicitWidth: cardButtonLabel.implicitWidth + 22 * Theme.unit
        implicitHeight: 28 * Theme.unit
        radius: 8 * Theme.unit
        color: strong ? Theme.tint : Theme.fill
        opacity: enabled ? 1 : 0.4
        Text {
            id: cardButtonLabel
            anchors.centerIn: parent
            text: cardButton.text
            color: cardButton.strong ? "#ffffff" : Theme.tint
            font.pixelSize: Theme.controlTextSize
            font.bold: cardButton.strong
        }
        MouseArea {
            anchors.fill: parent
            enabled: cardButton.enabled
            onClicked: cardButton.pressed()
        }
    }

    // one world: its picture, name, author, likes, who's there, Hop; More opens the places and new instances
    component WorldCard: Section {
        id: card
        objectName: "worldCard"   // a test opens one by it
        property var world: ({})
        property bool open: false
        property string status: ""
        property bool liked: !!world.liked
        property int likes: world.likes || 0
        readonly property var instances: world.instances || null
        readonly property bool compatible: explore.compatible(world.protocol)
        readonly property bool ownServerOpen: !!world.online && !(instances && instances.instanceOnly)
        readonly property int people: explore.people(world)
        readonly property var places: {
            var list = [];
            if (ownServerOpen) {
                list.push({ name: "Always on", detail: "the world's own server · " + (world.numUsers || 0) + " here",
                            people: world.numUsers || 0, instance: null, compatible: card.compatible });
            }
            ((instances && instances.list) || []).forEach(function (instance) {
                var bits = [(instance.players || 0) + " here"];
                if (instance.startedBy) bits.push("started by " + instance.startedBy);
                if (!instance.latest) bits.push("an older version");
                if (instance.editing) bits.push("being edited");
                var ok = explore.compatible(instance.protocol);
                if (!ok) bits.push("another version of Darkride");
                list.push({ name: instance.visibility.charAt(0).toUpperCase() + instance.visibility.slice(1) + " instance",
                            detail: bits.join(" · "), people: instance.players || 0, instance: instance, compatible: ok });
            });
            return list.sort(function (a, b) { return b.people - a.people; });
        }
        readonly property bool canHop: compatible && (places.some(function (p) { return p.compatible; })
                                                      || !!(instances && instances.canStart))
        readonly property string summary: {
            if (!compatible) return "Runs another version of Darkride.";
            if (people > 0) return people + (people === 1 ? " person here" : " people here");
            if (places.length > 0) return "Open now. Be the first one in.";
            if (instances && instances.canStart) return "Hop opens a new instance for you.";
            return instances ? "Sign in to open this world." : "Nobody's here right now.";
        }

        footer: status

        // where the people are; else somewhere running; else a new public instance
        function hop() {
            var open = places.filter(function (p) { return p.compatible; });
            var best = open.filter(function (p) { return p.people > 0; })[0]
                || open.filter(function (p) { return p.instance === null; })[0]
                || open.filter(function (p) { return p.instance && p.instance.visibility === "public"; })[0]
                || (instances && instances.canStart ? null : open[0]);
            if (best) {
                join(best);
            } else if (instances && instances.canStart) {
                start("public");
            }
        }
        function join(place) {
            if (place.instance) {
                explore.joinInstance(world, place.instance);
            } else {
                explore.joinWorld(world);
            }
        }
        function start(visibility) {
            status = "Starting a " + visibility + " instance…";
            explore.startInstance(world, visibility, function (problem) { status = problem; });
        }

        Item {
            width: parent ? parent.width : 0
            height: worldPicture.status === Image.Ready ? width * 0.42 : 0
            clip: true
            visible: height > 0
            Image {
                id: worldPicture
                anchors.fill: parent
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
                source: card.world.id ? explore.picture(card.world.id) : ""
                opacity: card.compatible ? 1 : 0.45
            }
        }
        ListRow {
            label: card.world.label || ""
            detail: (card.world.username ? "by " + card.world.username + " · " : "") + card.summary
            clickable: true
            onClicked: card.open = !card.open

            // likes: a heart and its count
            Text {
                text: (card.liked ? "♥ " : "♡ ") + card.likes
                color: card.liked ? Theme.destructive : Theme.secondaryText
                font.pixelSize: Theme.bodySize
                MouseArea {
                    anchors.fill: parent
                    anchors.margins: -6 * Theme.unit
                    enabled: AccountServices.loggedIn
                    onClicked: {
                        var on = !card.liked;
                        explore.like(card.world, on, function () {
                            card.liked = on;
                            card.likes += on ? 1 : -1;
                        });
                    }
                }
            }
            CardButton {
                text: explore.hoppingWorld === card.world.id ? "Hopping…" : "Hop"
                strong: true
                enabled: card.canHop && explore.hoppingWorld === ""
                onPressed: card.hop()
            }
        }
        ListRow {
            visible: !!card.world.description && card.open
            label: card.world.description || ""
            enabled: false
        }
        Repeater {
            model: card.open ? card.places : []
            delegate: ListRow {
                label: modelData.name
                detail: modelData.detail
                enabled: modelData.compatible
                CardButton {
                    text: "Join"
                    enabled: modelData.compatible && explore.hoppingWorld === ""
                    onPressed: card.join(modelData)
                }
            }
        }
        ListRow {
            visible: card.open && !!(card.instances && card.instances.canStart) && card.compatible
            label: "New instance"
            detail: "public: anyone · friends: your friends · private: only you"
            CardButton { text: "Public"; enabled: explore.hoppingWorld === ""; onPressed: card.start("public") }
            CardButton { text: "Friends"; enabled: explore.hoppingWorld === ""; onPressed: card.start("friends") }
            CardButton { text: "Private"; enabled: explore.hoppingWorld === ""; onPressed: card.start("private") }
        }
        ListRow {
            visible: !card.open && (card.places.length > 0 || !!(card.instances && card.instances.canStart))
            action: true
            label: "More ⌄"
            onClicked: card.open = true
        }
    }

    Section {
        visible: explore.worlds.length === 0
        footer: explore.loading ? "…" : explore.status !== "" ? explore.status
            : explore.search !== "" ? "No world goes by that name."
            : root.currentTab === 1 ? "Worlds you like show here." : root.currentTab === 2 ? "Your private worlds show here."
            : "No worlds are open right now."
    }

    Repeater {
        model: explore.worlds
        delegate: WorldCard { world: modelData }
    }

    Section {
        visible: explore.more
        ListRow {
            action: true
            label: explore.loading ? "Loading…" : "Show More Worlds"
            enabled: !explore.loading
            onClicked: explore.load(false)
        }
    }
}
