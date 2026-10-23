//
//  Settings.qml
//  qml/darkride
//
//  Darkride's Settings panel: graphics, the nametag, the disk cache, on one page. The bar's Settings button opens it in a
//  window on the desktop and on the tablet in a headset (tivoli-scripts' ButtonManager.addQmlButton). It reads and sets
//  the engine's own values through the context properties Render, Performance and DiskCache, and the nametag through the
//  metaverse's API (MetaverseRequests). It replaces the web page of the same name (ARCHITECTURE.md §5.2.1: the first of
//  Darkride's panels in QML, the pattern for the others).
//
//  Copyright 2026 Darkride.
//
//  Distributed under the MIT License.
//

import QtQuick 2.15
import QtQuick.Controls 2.15 as QQC2
import QtQuick.Layouts 1.15

Rectangle {
    id: root

    // 1 on the desktop; larger in the tablet, which is seen from further away
    property real unit: 1
    property string title: "Settings"
    signal sendToScript(var message)   // the tablet's loader connects it; nothing is sent

    // the tablet's loader sizes the page; a desktop window parents it to its content area
    anchors.fill: parent
    color: "#2b2b2e"

    // a desktop window keeps the page between openings: read the values without a change signal again on each
    onVisibleChanged: {
        if (visible) {
            cache.refresh();
            nametag.load();
        }
    }

    readonly property color textColor: "#ffffff"
    readonly property color dimColor: Qt.rgba(1, 1, 1, 0.62)
    readonly property color lineColor: Qt.rgba(1, 1, 1, 0.10)
    readonly property color accent: "#0a84ff"
    readonly property color destructive: "#ff453a"

    // ---- the small pieces every row uses

    component Heading: Text {
        color: root.textColor
        font.pixelSize: 13 * root.unit
        font.bold: true
        topPadding: 14 * root.unit
        bottomPadding: 4 * root.unit
    }

    component Note: Text {
        color: root.dimColor
        font.pixelSize: 11 * root.unit
        wrapMode: Text.WordWrap
        Layout.fillWidth: true
        Layout.columnSpan: 2
    }

    component RowLabel: Text {
        color: root.dimColor
        font.pixelSize: 11 * root.unit
        Layout.preferredWidth: 128 * root.unit
        Layout.alignment: Qt.AlignVCenter
        elide: Text.ElideRight
    }

    // a row of mutually exclusive choices: model [{ text, value }], current the selected value
    component Choices: Row {
        id: choices
        property var model: []
        property var current
        signal chosen(var value)
        spacing: 1 * root.unit
        Repeater {
            model: choices.model
            delegate: QQC2.AbstractButton {
                id: choice
                readonly property bool selected: modelData.value === choices.current
                implicitWidth: Math.max(36 * root.unit, label.implicitWidth + 14 * root.unit)
                implicitHeight: 22 * root.unit
                onClicked: choices.chosen(modelData.value)
                background: Rectangle {
                    radius: 4 * root.unit
                    color: choice.selected ? root.accent : (choice.down ? Qt.rgba(1, 1, 1, 0.16) : Qt.rgba(1, 1, 1, 0.08))
                }
                contentItem: Text {
                    id: label
                    text: modelData.text
                    color: root.textColor
                    font.pixelSize: 11 * root.unit
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }
    }

    component Toggle: QQC2.Switch {
        id: toggle
        implicitHeight: 22 * root.unit
        indicator: Rectangle {
            implicitWidth: 34 * root.unit
            implicitHeight: 18 * root.unit
            x: toggle.leftPadding
            y: (toggle.height - height) / 2
            radius: height / 2
            color: toggle.checked ? root.accent : Qt.rgba(1, 1, 1, 0.16)
            Rectangle {
                x: toggle.checked ? parent.width - width - 2 * root.unit : 2 * root.unit
                y: 2 * root.unit
                width: 14 * root.unit
                height: 14 * root.unit
                radius: 7 * root.unit
                color: "#ffffff"
            }
        }
        contentItem: Item {}
    }

    component SmallButton: QQC2.AbstractButton {
        id: smallButton
        property bool danger: false
        implicitWidth: Math.max(56 * root.unit, smallLabel.implicitWidth + 16 * root.unit)
        implicitHeight: 22 * root.unit
        background: Rectangle {
            radius: 4 * root.unit
            color: smallButton.down ? Qt.rgba(1, 1, 1, 0.16) : Qt.rgba(1, 1, 1, 0.08)
            opacity: smallButton.enabled ? 1 : 0.4
        }
        contentItem: Text {
            id: smallLabel
            text: smallButton.text
            color: smallButton.danger ? root.destructive : root.textColor
            font.pixelSize: 11 * root.unit
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            opacity: smallButton.enabled ? 1 : 0.4
        }
    }

    component Field: QQC2.TextField {
        implicitHeight: 22 * root.unit
        color: root.textColor
        font.pixelSize: 11 * root.unit
        selectByMouse: true
        leftPadding: 6 * root.unit
        background: Rectangle {
            radius: 4 * root.unit
            color: Qt.rgba(1, 1, 1, 0.06)
            border.color: parent.activeFocus ? root.accent : root.lineColor
        }
    }

    // a slider with its value in tabular figures and a way back to the default
    component ValueSlider: RowLayout {
        id: valueSlider
        property real from: 0
        property real to: 1
        property real stepSize: 0.1
        property real value: 0
        property real defaultValue: NaN
        property int decimals: 1
        property string units: ""
        signal moved(real value)
        spacing: 6 * root.unit
        QQC2.Slider {
            id: slider
            Layout.fillWidth: true
            implicitHeight: 22 * root.unit
            from: valueSlider.from
            to: valueSlider.to
            stepSize: valueSlider.stepSize
            value: valueSlider.value
            onMoved: valueSlider.moved(value)
        }
        Text {
            text: valueSlider.value.toFixed(valueSlider.decimals) + valueSlider.units
            color: root.textColor
            font.pixelSize: 11 * root.unit
            font.family: "Menlo"
            Layout.preferredWidth: 52 * root.unit
            horizontalAlignment: Text.AlignRight
        }
        SmallButton {
            text: "Default"
            visible: !isNaN(valueSlider.defaultValue)
            enabled: valueSlider.value !== valueSlider.defaultValue
            onClicked: valueSlider.moved(valueSlider.defaultValue)
        }
    }

    // ---- the page

    QQC2.ScrollView {
        id: scroll
        anchors.fill: parent
        anchors.margins: 14 * root.unit
        clip: true
        contentWidth: availableWidth

        ColumnLayout {
            width: scroll.availableWidth
            spacing: 0

            // ---- graphics

            Heading { text: "Graphics" }
            Rectangle { Layout.fillWidth: true; height: 1; color: root.lineColor }

            GridLayout {
                columns: 2
                columnSpacing: 10 * root.unit
                rowSpacing: 6 * root.unit
                Layout.fillWidth: true
                Layout.topMargin: 6 * root.unit

                RowLabel { text: "Quality preset" }
                Choices {
                    model: [{ text: "Potato", value: 1 }, { text: "Low", value: 2 }, { text: "Medium", value: 3 },
                            { text: "High", value: 4 }, { text: "Custom", value: 0 }]
                    current: Performance.performancePreset
                    onChosen: Performance.performancePreset = value
                }

                RowLabel { text: "Frame rate" }
                Choices {
                    model: [{ text: "Display", value: 3 }, { text: "60", value: 2 }, { text: "30", value: 1 },
                            { text: "20", value: 0 }]
                    current: Performance.refreshRateProfile
                    onChosen: Performance.refreshRateProfile = value
                }

                RowLabel { text: "Antialiasing" }
                Choices {
                    model: [{ text: "None", value: 0 }, { text: "TAA", value: 1 }, { text: "FXAA", value: 2 }]
                    current: Render.antialiasingMethod
                    onChosen: Render.antialiasingMethod = value
                }

                RowLabel { text: "Texture memory" }
                Choices {
                    model: [{ text: "512", value: 512 }, { text: "1024", value: 1024 }, { text: "2048", value: 2048 },
                            { text: "4096", value: 4096 }, { text: "8192 MB", value: 8192 }]
                    current: Render.maximumTextureMemory
                    onChosen: Render.maximumTextureMemory = value
                }

                RowLabel { text: "Shadows" }
                Toggle { checked: Render.shadowsEnabled; onToggled: Render.shadowsEnabled = checked }

                RowLabel { text: "Ambient occlusion" }
                Toggle { checked: Render.ambientOcclusionEnabled; onToggled: Render.ambientOcclusionEnabled = checked }

                RowLabel { text: "Deferred rendering" }
                Toggle { checked: Render.renderMethod === 0; onToggled: Render.renderMethod = checked ? 0 : 1 }

                RowLabel { text: "Nametags" }
                Toggle { checked: Render.nametagsEnabled; onToggled: Render.nametagsEnabled = checked }

                RowLabel { text: "My own nametag" }
                Toggle { checked: Render.nametagsShowSelf; onToggled: Render.nametagsShowSelf = checked }

                RowLabel { text: "Resolution scale" }
                ValueSlider {
                    Layout.fillWidth: true
                    from: 0.1; to: 2; stepSize: 0.1; decimals: 1; units: "×"
                    value: Render.viewportResolutionScale
                    defaultValue: 1
                    onMoved: Render.viewportResolutionScale = value
                }

                RowLabel { text: "Field of view" }
                ValueSlider {
                    Layout.fillWidth: true
                    from: 30; to: 150; stepSize: 1; decimals: 0; units: "°"
                    value: Render.fieldOfView
                    defaultValue: 120
                    onMoved: Render.fieldOfView = value
                }

                RowLabel { text: "Draw distance" }
                RowLayout {
                    spacing: 6 * root.unit
                    Field {
                        id: farClipField
                        Layout.preferredWidth: 80 * root.unit
                        text: Math.round(Render.farClip)
                        validator: IntValidator { bottom: 10; top: 100000 }
                        onEditingFinished: Render.farClip = parseInt(text)
                    }
                    Text { text: "m"; color: root.dimColor; font.pixelSize: 11 * root.unit }
                    Item { Layout.fillWidth: true }
                    SmallButton {
                        text: "Default"
                        enabled: Math.round(Render.farClip) !== 16384
                        onClicked: Render.farClip = 16384
                    }
                }

                Note {
                    text: "A resolution scale above 1× draws more pixels than the window has: sharper, and slower in "
                        + "proportion. A quality preset sets the frame rate, antialiasing, texture memory, shadows, "
                        + "ambient occlusion, rendering and resolution scale at once; Custom leaves them as they are."
                }
            }

            // ---- the nametag

            Heading { text: "Nametag" }
            Rectangle { Layout.fillWidth: true; height: 1; color: root.lineColor }

            Item {
                id: nametag
                Layout.fillWidth: true
                implicitHeight: nametagColumn.implicitHeight + 8 * root.unit

                readonly property bool signedIn: AccountServices.loggedIn
                property string username: AccountServices.username
                property bool busy: false
                property string status: ""
                property bool reconnectNeeded: false
                property string pictureStamp: ""

                function load() {
                    if (!signedIn || busy) {   // a first desktop opening asks twice: on creation, then on showing
                        return;
                    }
                    busy = true;
                    MetaverseRequests.send("GET", "/api/user/nametag-details", null, function (error, code, data) {
                        busy = false;
                        if (error) {
                            status = "Could not read the nametag: " + error;
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
                            status = "Not saved: " + error;
                            return;
                        }
                        status = "Saved. Others see it after you rejoin the world.";
                        reconnectNeeded = true;
                        pictureStamp = String(Date.now());
                    });
                }

                Component.onCompleted: load()
                onSignedInChanged: load()

                ColumnLayout {
                    id: nametagColumn
                    width: parent.width
                    spacing: 6 * root.unit

                    Note {
                        visible: !nametag.signedIn
                        Layout.topMargin: 6 * root.unit
                        text: "Sign in to set the name and pronoun others see above your head."
                    }

                    Image {
                        visible: nametag.signedIn && status === Image.Ready
                        Layout.topMargin: 6 * root.unit
                        Layout.fillWidth: true
                        Layout.preferredHeight: visible ? width * sourceSize.height / Math.max(1, sourceSize.width) : 0
                        fillMode: Image.PreserveAspectFit
                        source: nametag.signedIn && nametag.username !== ""
                            ? MetaverseRequests.metaverseUrl() + "/api/user/" + nametag.username.toLowerCase()
                              + "/nametag?" + nametag.pictureStamp
                            : ""
                        cache: false
                    }

                    GridLayout {
                        visible: nametag.signedIn
                        columns: 2
                        columnSpacing: 10 * root.unit
                        rowSpacing: 6 * root.unit
                        Layout.fillWidth: true

                        RowLabel { text: "Display name" }
                        Field {
                            id: displayNameField
                            Layout.fillWidth: true
                            enabled: !nametag.busy
                            placeholderText: nametag.username
                        }

                        RowLabel { text: "Pronoun" }
                        Choices {
                            id: pronounChoices
                            current: ""
                            model: [{ text: "None", value: "" }, { text: "She/Her", value: "she/her" },
                                    { text: "He/Him", value: "he/him" }, { text: "They/Them", value: "they/them" }]
                            onChosen: current = value
                        }

                        Item { width: 1; height: 1 }
                        RowLayout {
                            spacing: 6 * root.unit
                            SmallButton { text: "Save"; enabled: !nametag.busy; onClicked: nametag.save() }
                            SmallButton {
                                text: "Rejoin the world"
                                visible: nametag.reconnectNeeded
                                onClicked: {
                                    var here = String(AddressManager.href);
                                    AddressManager.handleLookupString("file:///");
                                    rejoin.target = here;
                                    rejoin.start();
                                    nametag.reconnectNeeded = false;
                                }
                            }
                            SmallButton {
                                text: "Picture…"
                                onClicked: Qt.openUrlExternally(MetaverseRequests.metaverseUrl() + "/user/settings")
                            }
                        }
                    }

                    Note { visible: nametag.status !== ""; text: nametag.status }

                    Timer {
                        id: rejoin
                        property string target: ""
                        interval: 1000
                        onTriggered: AddressManager.handleLookupString(target)
                    }
                }
            }

            // ---- the disk cache

            Heading { text: "Cache" }
            Rectangle { Layout.fillWidth: true; height: 1; color: root.lineColor }

            GridLayout {
                id: cache
                columns: 2
                columnSpacing: 10 * root.unit
                rowSpacing: 6 * root.unit
                Layout.fillWidth: true
                Layout.topMargin: 6 * root.unit

                // the disk cache's values carry no change signal: read again when the page opens and after each
                // change; both sizes are in GB
                property real used: 0
                property real maximum: 0
                property string directory: ""
                property bool confirmClear: false
                function refresh() {
                    used = DiskCache.usedCacheSize;
                    maximum = DiskCache.maximumCacheSize;
                    directory = DiskCache.cacheDirectory;
                }
                Component.onCompleted: refresh()

                RowLabel { text: "In use" }
                Text {
                    text: cache.used.toFixed(2) + " of " + cache.maximum.toFixed(1) + " GB"
                    color: root.textColor
                    font.pixelSize: 11 * root.unit
                    font.family: "Menlo"
                }

                RowLabel { text: "Largest size" }
                RowLayout {
                    spacing: 6 * root.unit
                    Field {
                        Layout.preferredWidth: 60 * root.unit
                        text: cache.maximum.toFixed(1)
                        validator: DoubleValidator { bottom: 0.5; top: 100; decimals: 1 }
                        onEditingFinished: { DiskCache.maximumCacheSize = parseFloat(text); cache.refresh(); }
                    }
                    Text { text: "GB"; color: root.dimColor; font.pixelSize: 11 * root.unit }
                    Item { Layout.fillWidth: true }
                    SmallButton { text: "Default"; onClicked: { DiskCache.restoreDefaultMaximumCacheSize(); cache.refresh(); } }
                }

                RowLabel { text: "Folder" }
                RowLayout {
                    spacing: 6 * root.unit
                    Field {
                        Layout.fillWidth: true
                        text: cache.directory
                        onEditingFinished: { DiskCache.cacheDirectory = text; cache.refresh(); }
                    }
                    SmallButton { text: "Default"; onClicked: { DiskCache.restoreDefaultCacheDirectory(); cache.refresh(); } }
                }

                Item { width: 1; height: 1 }
                RowLayout {
                    spacing: 6 * root.unit
                    SmallButton {
                        text: "Clear the cache…"
                        danger: true
                        visible: !cache.confirmClear
                        onClicked: cache.confirmClear = true
                    }
                    SmallButton { text: "Cancel"; visible: cache.confirmClear; onClicked: cache.confirmClear = false }
                    SmallButton {
                        text: "Clear"
                        danger: true
                        visible: cache.confirmClear
                        onClicked: { DiskCache.clearCache(); cache.confirmClear = false; cache.refresh(); }
                    }
                }

                Note {
                    text: "The cache keeps the models, sounds and pictures of the worlds you visit, so a second visit "
                        + "loads from this computer. Clearing it makes every world download again."
                }
            }

            Item { Layout.preferredHeight: 14 * root.unit }
        }
    }
}
