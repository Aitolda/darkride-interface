//
//  ListRow.qml
//  qml/darkride/panel
//
//  One row of a section: its label (and a small detail under it) on the left, its control on the right; when the two
//  don't fit side by side, the control goes under the label. A row with a number can be scrubbed: dragging its label
//  sideways changes the number, as the build tools' labels do (editor/ui/PropertyEditors.cpp ScrubLabel: one step per
//  pixel, Shift ten, Alt a tenth). An action row is a whole-row button: its label in blue, or red when it deletes.
//
//  Copyright 2026 Darkride.
//
//  Distributed under the MIT License.
//

import QtQuick 2.15
import QtQuick.Layouts 1.15
import "."

Item {
    id: row

    property string label: ""
    property string detail: ""
    // the control fills the room beside the label (a slider, a text field) rather than sitting at the right
    property bool fill: false
    // a fixed width for the label, so the controls of neighbouring rows line up (0: the words' own width)
    property real labelWidth: 0
    default property alias content: contentRow.data

    // scrubbing: the value it starts from, its bounds, the change per pixel; scrubbed() gives each new value
    property bool scrubbable: false
    property real scrubValue: 0
    property real scrubFrom: -1e9
    property real scrubTo: 1e9
    property real scrubStep: 1
    signal scrubbed(real value)

    // an action row
    property bool action: false
    property bool destructive: false
    signal clicked()

    width: parent ? parent.width : 0
    // from the parts themselves, not the grid's own implicit height, which changes with the layout it feeds
    implicitHeight: {
        var labelHeight = labelColumn.visible ? labelColumn.implicitHeight : 0;
        var contentHeight = contentRow.visible ? contentRow.implicitHeight : 0;
        var inner = stacked ? labelHeight + grid.rowSpacing + contentHeight : Math.max(labelHeight, contentHeight);
        return Math.max(Theme.rowHeight, inner + 2 * Theme.rowPadding);
    }

    // the separator runs above every row but a card's first, inset to the text as iOS draws it
    readonly property bool firstShown: {
        if (!parent) {
            return true;
        }
        for (var i = 0; i < parent.children.length; i++) {
            if (parent.children[i].visible) {
                return parent.children[i] === row;
            }
        }
        return true;
    }
    readonly property bool stacked: !action && width > 0 && contentRow.visibleChildrenWidth > 0
        && Math.max(labelWidth, labelColumn.implicitWidth) + Theme.gap + contentRow.visibleChildrenWidth
           > width - 2 * Theme.cardPadding

    Rectangle {
        anchors.fill: parent
        visible: row.action && actionArea.pressed
        color: Theme.cardPressed
        radius: Theme.cardRadius
    }

    Rectangle {
        visible: !row.firstShown
        x: Theme.cardPadding
        width: parent.width - x
        height: 1
        color: Theme.separator
    }

    MouseArea {
        id: actionArea
        anchors.fill: parent
        enabled: row.action && row.enabled
        onClicked: row.clicked()
    }

    GridLayout {
        id: grid
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: Theme.cardPadding
        anchors.rightMargin: Theme.cardPadding
        anchors.verticalCenter: parent.verticalCenter
        columns: row.stacked ? 1 : 2
        columnSpacing: Theme.gap
        rowSpacing: 8 * Theme.unit

        Column {
            id: labelColumn
            Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
            Layout.preferredWidth: row.labelWidth > 0 && !row.stacked ? row.labelWidth : implicitWidth
            visible: row.label !== ""

            Text {
                id: labelText
                text: row.label
                color: row.action ? (row.destructive ? Theme.destructive : Theme.tint) : Theme.text
                opacity: row.enabled ? 1 : 0.4
                font.pixelSize: Theme.bodySize

                MouseArea {
                    id: scrubArea
                    // a little wider and taller than the words, so the label is easy to catch
                    anchors.fill: parent
                    anchors.margins: -4 * Theme.unit
                    enabled: row.scrubbable && row.enabled
                    cursorShape: enabled ? Qt.SizeHorCursor : Qt.ArrowCursor
                    preventStealing: true

                    property real pressX: 0
                    property real lastX: 0
                    property real accumulated: 0
                    property bool scrubbing: false

                    onPressed: {
                        pressX = lastX = mouse.x;
                        scrubbing = false;
                    }
                    onPositionChanged: {
                        if (!scrubbing) {
                            if (Math.abs(mouse.x - pressX) < Theme.scrubSlop) {
                                return;
                            }
                            scrubbing = true;
                            lastX = mouse.x;
                            accumulated = row.scrubValue;
                            return;
                        }
                        var step = row.scrubStep;
                        if (mouse.modifiers & Qt.ShiftModifier) {
                            step *= Theme.scrubCoarse;
                        } else if (mouse.modifiers & Qt.AltModifier) {
                            step *= Theme.scrubFine;
                        }
                        accumulated = Math.max(row.scrubFrom, Math.min(row.scrubTo, accumulated + (mouse.x - lastX) * step));
                        lastX = mouse.x;
                        row.scrubbed(accumulated);
                    }
                    onReleased: scrubbing = false
                }
            }

            Text {
                visible: row.detail !== ""
                text: row.detail
                color: Theme.secondaryText
                font.pixelSize: Theme.detailSize
            }
        }

        RowLayout {
            id: contentRow
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
            spacing: 8 * Theme.unit
            visible: !row.action

            // what the controls need side by side (the spacer below is not counted)
            readonly property real visibleChildrenWidth: {
                var total = 0;
                var shown = 0;
                for (var i = 0; i < children.length; i++) {
                    var child = children[i];
                    if (child !== spacer && child.visible) {
                        total += child.implicitWidth;
                        shown++;
                    }
                }
                return total + Math.max(0, shown - 1) * spacing;
            }

            // pushes the controls to the right, unless the row fills or they sit under the label
            Item {
                id: spacer
                Layout.fillWidth: !row.fill && !row.stacked
                visible: Layout.fillWidth
            }
        }
    }
}
