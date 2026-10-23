//
//  Theme.qml
//  qml/darkride/panel
//
//  The look of Darkride's QML panels (Settings first, then Friends and Explore): iOS's grouped lists in its dark
//  palette, the same on the desktop and on the tablet (Caity, 10-23: "classy ... kinda like iOS design language").
//  One per QML engine, so the tablet's surface may set its own unit.
//
//  Copyright 2026 Darkride.
//
//  Distributed under the MIT License.
//

pragma Singleton
import QtQuick 2.15

QtObject {
    // 1 on the desktop; a surface seen from further away may scale everything up
    property real unit: 1

    // iOS dark: the grouped background, its cells, their separators
    readonly property color background: "#000000"
    readonly property color card: "#1c1c1e"
    readonly property color cardPressed: "#2c2c2e"
    readonly property color separator: Qt.rgba(84 / 255, 84 / 255, 88 / 255, 0.6)
    readonly property color text: "#ffffff"
    readonly property color secondaryText: Qt.rgba(235 / 255, 235 / 255, 245 / 255, 0.6)
    readonly property color tertiaryText: Qt.rgba(235 / 255, 235 / 255, 245 / 255, 0.3)
    readonly property color tint: "#0a84ff"
    readonly property color on: "#30d158"            // a switch that is on
    readonly property color destructive: "#ff453a"
    readonly property color fill: Qt.rgba(118 / 255, 118 / 255, 128 / 255, 0.24)   // fields, segmented tracks
    readonly property color segmentThumb: "#636366"
    readonly property color sliderTrack: Qt.rgba(120 / 255, 120 / 255, 128 / 255, 0.36)

    readonly property real largeTitleSize: 26 * unit
    readonly property real bodySize: 14 * unit
    readonly property real detailSize: 11.5 * unit
    readonly property real sectionSize: 11.5 * unit
    readonly property real controlTextSize: 12.5 * unit

    readonly property real pageMargin: 16 * unit
    readonly property real cardPadding: 14 * unit
    readonly property real cardRadius: 10 * unit
    readonly property real rowHeight: 40 * unit
    readonly property real rowPadding: 7 * unit
    readonly property real controlHeight: 26 * unit
    readonly property real gap: 10 * unit

    // the build tools' label scrub (editor/ui/PropertyEditors.cpp): a few pixels before it starts; Shift coarse, Alt fine
    readonly property real scrubSlop: 3
    readonly property real scrubCoarse: 10
    readonly property real scrubFine: 0.1
}
