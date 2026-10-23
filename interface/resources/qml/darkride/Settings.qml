//
//  Settings.qml
//  qml/darkride
//
//  Darkride's Settings panel: graphics, the view, the nametag, the disk cache, on one page in the panels' iOS look
//  (darkride/panel). The bar's Settings button opens it in a window on the desktop and on the tablet in a headset
//  (tivoli-scripts' ButtonManager.addQmlButton). It reads and sets the engine's own values through the context
//  properties Render, Performance and DiskCache, and the nametag through the metaverse's API (MetaverseRequests). It
//  replaces the web page of the same name (ARCHITECTURE.md §5.2.1: the first of Darkride's panels in QML, the pattern
//  for the others). Every number can be scrubbed by dragging its label, as in the build tools.
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
    title: "Settings"

    // a desktop window keeps the page between openings: read the values without a change signal again on each
    onVisibleChanged: {
        if (visible) {
            cache.refresh();
            nametag.load();
        }
    }

    readonly property real defaultFieldOfView: 120
    readonly property real defaultFarClip: 16384

    // ---- graphics

    Section {
        title: "Graphics"
        footer: "A preset sets the frame rate, antialiasing, texture memory, shadows, ambient occlusion and rendering "
            + "at once. Custom leaves them as they are."

        ListRow {
            label: "Quality"
            Segmented {
                model: [{ text: "Potato", value: 1 }, { text: "Low", value: 2 }, { text: "Medium", value: 3 },
                        { text: "High", value: 4 }, { text: "Custom", value: 0 }]
                current: Performance.performancePreset
                onChosen: Performance.performancePreset = value
            }
        }
        ListRow {
            label: "Frame rate"
            detail: "frames a second"
            Segmented {
                model: [{ text: "Display", value: 3 }, { text: "60", value: 2 }, { text: "30", value: 1 },
                        { text: "20", value: 0 }]
                current: Performance.refreshRateProfile
                onChosen: Performance.refreshRateProfile = value
            }
        }
        ListRow {
            label: "Antialiasing"
            Segmented {
                model: [{ text: "None", value: 0 }, { text: "TAA", value: 1 }, { text: "FXAA", value: 2 }]
                current: Render.antialiasingMethod
                onChosen: Render.antialiasingMethod = value
            }
        }
        ListRow {
            label: "Texture memory"
            detail: "MB"
            Segmented {
                model: [{ text: "512", value: 512 }, { text: "1024", value: 1024 }, { text: "2048", value: 2048 },
                        { text: "4096", value: 4096 }, { text: "8192", value: 8192 }]
                current: Render.maximumTextureMemory
                onChosen: Render.maximumTextureMemory = value
            }
        }
    }

    Section {
        ListRow {
            label: "Shadows"
            Toggle { checked: Render.shadowsEnabled; onToggled: Render.shadowsEnabled = checked }
        }
        ListRow {
            label: "Ambient occlusion"
            Toggle { checked: Render.ambientOcclusionEnabled; onToggled: Render.ambientOcclusionEnabled = checked }
        }
        ListRow {
            label: "Deferred rendering"
            Toggle { checked: Render.renderMethod === 0; onToggled: Render.renderMethod = checked ? 0 : 1 }
        }
    }

    // ---- the view

    Section {
        title: "View"
        footer: "Drag a label sideways to change its number: Shift for big steps, Alt for small ones. A resolution "
            + "scale above 1× draws more pixels than the window has: sharper, and slower in proportion."

        ListRow {
            label: "Resolution scale"
            fill: true
            labelWidth: 120 * Theme.unit
            scrubbable: true
            scrubValue: Render.viewportResolutionScale
            scrubFrom: 0.1
            scrubTo: 2
            scrubStep: 0.01
            onScrubbed: Render.viewportResolutionScale = Math.round(value * 100) / 100

            ValueSlider {
                from: 0.1; to: 2; stepSize: 0.05; decimals: 2; units: "×"
                value: Render.viewportResolutionScale
                onMoved: Render.viewportResolutionScale = value
            }
            ResetButton {
                needed: Math.abs(Render.viewportResolutionScale - 1) > 0.001
                onClicked: Render.viewportResolutionScale = 1
            }
        }
        ListRow {
            label: "Field of view"
            fill: true
            labelWidth: 120 * Theme.unit
            scrubbable: true
            scrubValue: Render.fieldOfView
            scrubFrom: 30
            scrubTo: 150
            scrubStep: 0.5
            onScrubbed: Render.fieldOfView = Math.round(value)

            ValueSlider {
                from: 30; to: 150; stepSize: 1; decimals: 0; units: "°"
                value: Render.fieldOfView
                onMoved: Render.fieldOfView = value
            }
            ResetButton {
                needed: Math.round(Render.fieldOfView) !== root.defaultFieldOfView
                onClicked: Render.fieldOfView = root.defaultFieldOfView
            }
        }
        ListRow {
            label: "Draw distance"
            detail: "metres"
            scrubbable: true
            scrubValue: Render.farClip
            scrubFrom: 10
            scrubTo: 100000
            scrubStep: 10
            onScrubbed: Render.farClip = Math.round(value)

            Field {
                text: Math.round(Render.farClip)
                validator: IntValidator { bottom: 10; top: 100000 }
                onEditingFinished: Render.farClip = parseInt(text)
            }
            ResetButton {
                needed: Math.round(Render.farClip) !== root.defaultFarClip
                onClicked: Render.farClip = root.defaultFarClip
            }
        }
    }

    // ---- the nametag

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

    Section {
        title: "Nametag"
        footer: nametag.signedIn ? nametag.status : "Sign in to set the name and pronoun others see above your head."

        Item {
            visible: nametag.signedIn && picture.status === Image.Ready
            width: parent ? parent.width : 0
            height: visible ? picture.height + 2 * Theme.cardPadding : 0

            Image {
                id: picture
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

    Section {
        ListRow {
            label: "Show nametags"
            Toggle { checked: Render.nametagsEnabled; onToggled: Render.nametagsEnabled = checked }
        }
        ListRow {
            label: "Show my own"
            Toggle { checked: Render.nametagsShowSelf; onToggled: Render.nametagsShowSelf = checked }
        }
    }

    // ---- the disk cache

    QtObject {
        id: cache

        // the disk cache's values carry no change signal: read again when the page opens and after each change;
        // both sizes are in GB
        property real used: 0
        property real maximum: 0
        property string directory: ""
        property bool confirmClear: false
        property real defaultMaximum: NaN

        function refresh() {
            used = DiskCache.usedCacheSize;
            maximum = DiskCache.maximumCacheSize;
            directory = DiskCache.cacheDirectory;
        }

        Component.onCompleted: refresh()
    }

    Section {
        title: "Cache"
        footer: "The cache keeps the models, sounds and pictures of the worlds you visit, so a second visit loads "
            + "from this computer. Clearing it makes every world download again."

        ListRow {
            label: "In use"
            Text {
                text: cache.used.toFixed(2) + " of " + cache.maximum.toFixed(1) + " GB"
                color: Theme.secondaryText
                font.pixelSize: Theme.bodySize
            }
        }
        ListRow {
            label: "Largest size"
            detail: "GB"
            scrubbable: true
            scrubValue: cache.maximum
            scrubFrom: 0.5
            scrubTo: 100
            scrubStep: 0.1
            onScrubbed: {
                DiskCache.maximumCacheSize = Math.round(value * 10) / 10;
                cache.refresh();
            }

            Field {
                text: cache.maximum.toFixed(1)
                validator: DoubleValidator { bottom: 0.5; top: 100; decimals: 1 }
                onEditingFinished: {
                    DiskCache.maximumCacheSize = parseFloat(text);
                    cache.refresh();
                }
            }
            ResetButton {
                onClicked: {
                    DiskCache.restoreDefaultMaximumCacheSize();
                    cache.refresh();
                }
            }
        }
        ListRow {
            label: "Folder"
            fill: true
            Field {
                Layout.fillWidth: true
                text: cache.directory
                horizontalAlignment: TextInput.AlignLeft
                onEditingFinished: {
                    DiskCache.cacheDirectory = text;
                    cache.refresh();
                }
            }
            ResetButton {
                onClicked: {
                    DiskCache.restoreDefaultCacheDirectory();
                    cache.refresh();
                }
            }
        }
        ListRow {
            visible: !cache.confirmClear
            action: true
            destructive: true
            label: "Clear Cache…"
            onClicked: cache.confirmClear = true
        }
        // the confirmation, in the popups' manner: Cancel on the left, the deletion in red and never the default
        ListRow {
            visible: cache.confirmClear
            action: true
            label: "Cancel"
            onClicked: cache.confirmClear = false
        }
        ListRow {
            visible: cache.confirmClear
            action: true
            destructive: true
            label: "Clear the Cache Now"
            onClicked: {
                DiskCache.clearCache();
                cache.confirmClear = false;
                cache.refresh();
            }
        }
    }
}
