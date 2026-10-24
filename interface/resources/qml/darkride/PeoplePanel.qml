//
//  PeoplePanel.qml
//  qml/darkride
//
//  Darkride's People panel, in the panels' iOS look (darkride/panel), with two tabs (Caity, 10-23/24):
//  Here: everyone in this world, with how loud they are now, your volume and mute for each, and a line of actions
//    (Block; Mute for Everyone and Kick for whoever may kick here);
//  Friends: requests to and from you, adding a friend by their exact username, and your friends, online first, with
//    the world each is in and Join, and Remove.
//  The friends are the metaverse's (/api/v1/friends and its requests, through MetaverseRequests); joining one is
//  AddressManager.goToUser. There is no search for people by name: that is a privacy call (Caity's). It replaces
//  High Fidelity's People app (pal.js and Pal.qml), whose connections the metaverse no longer has.
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

    anchors.fill: parent
    title: "People"
    tabs: ["Here", "Friends"]

    readonly property string tabSetting: "io.darkride.peoplePanel.tab"
    Component.onCompleted: currentTab = Math.max(0, Math.min(1, Settings.getValue(tabSetting, 0)))
    onCurrentTabChanged: {
        Settings.setValue(tabSetting, currentTab);
        if (currentTab === 1) {
            friends.load();
        }
    }
    onVisibleChanged: {
        if (visible && currentTab === 1) {
            friends.load();
        }
    }

    // ==== Here =======================================================================================================

    NearbyPeople {
        id: peopleHere
        active: root.visible && root.currentTab === 0
    }

    Tab {
        visible: root.currentTab === 0

        Section {
            title: "In this world"
            footer: "Click a name for more. Muting is mutual: neither of you hears the other. Blocking hides them "
                + "and their voice from you. Volumes last for this visit."

            Repeater {
                model: peopleHere.model
                delegate: PersonRow {
                    nearby: peopleHere
                    index: model.index
                    name: model.name
                    distance: model.distance
                    level: model.level
                    gain: model.gain
                    muted: model.muted
                    blocked: model.blocked
                    actions: true
                }
            }
            ListRow {
                visible: peopleHere.count === 0
                label: "No one else is here."
                enabled: false
            }
        }
    }

    // ==== Friends ====================================================================================================

    QtObject {
        id: friends

        readonly property bool signedIn: AccountServices.loggedIn
        property var online: []         // [{ username, ai, image, location }], by username
        property var offline: []
        property var incoming: []       // requests to you: [{ username, image, createdAt }], newest first
        property var outgoing: []       // yours
        property bool busy: false
        property string status: ""      // the last action's result, under the Add section
        property string removing: ""    // the friend whose Remove waits for its second press
        property string adding: ""      // a request on its way

        function picture(entry) {
            var image = entry.image || "";
            if (/^https?:\/\//.test(image)) {
                return image;
            }
            return MetaverseRequests.metaverseUrl() + (image !== "" ? image
                : "/api/user/" + encodeURIComponent(entry.username) + "/image");
        }

        function load() {
            if (!signedIn || busy) {
                return;
            }
            busy = true;
            MetaverseRequests.send("GET", "/api/v1/friends", null, function (error, code, answer) {
                busy = false;
                if (error) {
                    status = "Couldn't read your friends: " + error + ".";
                    return;
                }
                var list = (answer && answer.data && answer.data.friends) || [];
                online = list.filter(function (friend) { return friend.online; });
                offline = list.filter(function (friend) { return !friend.online; });
            });
            MetaverseRequests.send("GET", "/api/v1/friends/requests", null, function (error, code, answer) {
                if (error) {
                    return;
                }
                incoming = (answer && answer.data && answer.data.incoming) || [];
                outgoing = (answer && answer.data && answer.data.outgoing) || [];
            });
        }

        // a request's or a removal's answer: { username, friends, requested }, or a message
        function act(method, path, body, done) {
            MetaverseRequests.send(method, path, body, function (error, code, answer) {
                var message = answer && answer.message ? String(answer.message) : "";
                if (error) {
                    status = message !== "" ? message : "That didn't go through: " + error + ".";
                } else if (done) {
                    status = done(answer && answer.data ? answer.data : {});
                }
                load();
            });
        }

        function request(username) {
            username = username.trim();
            if (username === "") {
                return;
            }
            status = "Asking " + username + "…";
            act("POST", "/api/v1/friends/requests", { username: username }, function (data) {
                addField.text = "";
                return data.friends ? "You and " + data.username + " are friends now."
                    : "Asked " + (data.username || username) + ". You'll be friends when they accept.";
            });
        }
        function accept(username) {
            act("POST", "/api/v1/friends/requests/" + encodeURIComponent(username) + "/accept", {}, function (data) {
                return "You and " + username + " are friends now.";
            });
        }
        function decline(username) {
            act("POST", "/api/v1/friends/requests/" + encodeURIComponent(username) + "/decline", {}, function () {
                return "Declined " + username + "'s request.";
            });
        }
        function withdraw(username) {
            act("DELETE", "/api/v1/friends/requests/" + encodeURIComponent(username), null, function () {
                return "Withdrew your request to " + username + ".";
            });
        }
        function remove(username) {
            removing = "";
            act("DELETE", "/api/v1/friends/" + encodeURIComponent(username), null, function () {
                return username + " is no longer a friend.";
            });
        }
        function join(username) {
            AddressManager.goToUser(username);
        }

        onSignedInChanged: load()
    }

    Timer {
        interval: 30 * 1000
        repeat: true
        running: root.visible && root.currentTab === 1 && friends.signedIn
        onTriggered: friends.load()
    }

    // a small button inside a row
    component RowButton: Rectangle {
        id: rowButton
        property string text: ""
        property bool danger: false
        property bool strong: false
        signal pressed()
        implicitWidth: rowButtonLabel.implicitWidth + 18 * Theme.unit
        implicitHeight: 26 * Theme.unit
        radius: 7 * Theme.unit
        color: strong ? Theme.tint : Theme.fill
        Text {
            id: rowButtonLabel
            anchors.centerIn: parent
            text: rowButton.text
            color: rowButton.strong ? "#ffffff" : rowButton.danger ? Theme.destructive : Theme.tint
            font.pixelSize: Theme.controlTextSize
        }
        MouseArea {
            anchors.fill: parent
            onClicked: rowButton.pressed()
        }
    }

    // a friend: where they are and Join when online; Remove behind a second press
    component FriendRow: ListRow {
        id: friendRow
        property var friend: ({})
        label: friend.username || ""
        detail: friend.online
            ? (friend.location && friend.location.name ? "in " + friend.location.name : "online")
              + (friend.ai ? " · AI resident" : "")
            : (friend.ai ? "AI resident · offline" : "offline")
        icon: friends.picture(friend)
        RowButton {
            visible: friends.removing === friendRow.friend.username
            text: "Cancel"
            onPressed: friends.removing = ""
        }
        RowButton {
            text: friends.removing === friendRow.friend.username ? "Remove them?" : "Remove"
            danger: true
            onPressed: {
                if (friends.removing === friendRow.friend.username) {
                    friends.remove(friendRow.friend.username);
                } else {
                    friends.removing = friendRow.friend.username;
                }
            }
        }
        RowButton {
            visible: friendRow.friend.online && !!friendRow.friend.location
            text: "Join"
            strong: true
            onPressed: friends.join(friendRow.friend.username)
        }
    }

    Tab {
        visible: root.currentTab === 1

        Section {
            visible: !friends.signedIn
            footer: "Sign in to see your friends and their requests."
        }

        Section {
            visible: friends.signedIn && (friends.incoming.length > 0 || friends.outgoing.length > 0)
            title: "Requests"

            Repeater {
                model: friends.incoming
                delegate: ListRow {
                    label: modelData.username
                    detail: "wants to be friends"
                    icon: friends.picture(modelData)
                    RowButton { text: "Decline"; onPressed: friends.decline(modelData.username) }
                    RowButton { text: "Accept"; strong: true; onPressed: friends.accept(modelData.username) }
                }
            }
            Repeater {
                model: friends.outgoing
                delegate: ListRow {
                    label: modelData.username
                    detail: "you asked"
                    icon: friends.picture(modelData)
                    RowButton { text: "Withdraw"; onPressed: friends.withdraw(modelData.username) }
                }
            }
        }

        Section {
            visible: friends.signedIn
            title: "Add a friend"
            footer: friends.status !== "" ? friends.status : "Ask by their exact username: they'll see your request here."

            ListRow {
                label: "Username"
                fill: true
                Field {
                    id: addField
                    Layout.fillWidth: true
                    horizontalAlignment: TextInput.AlignLeft
                    placeholderText: "someone"
                    onAccepted: friends.request(text)
                }
            }
            ListRow {
                action: true
                label: "Send Friend Request"
                enabled: addField.text.trim() !== ""
                onClicked: friends.request(addField.text)
            }
        }

        Section {
            visible: friends.signedIn
            title: "Online" + (friends.online.length ? " · " + friends.online.length : "")
            footer: friends.online.length === 0 ? (friends.busy ? "…" : "None of your friends is online.") : ""

            Repeater {
                model: friends.online
                delegate: FriendRow { friend: modelData }
            }
        }

        Section {
            visible: friends.signedIn && friends.offline.length > 0
            title: "Offline · " + friends.offline.length

            Repeater {
                model: friends.offline
                delegate: FriendRow { friend: modelData }
            }
        }
    }
}
