//
//  ViewportOverlay.qml
//
//  Build mode: the controls drawn over the viewport. The scene gizmo (the world axes as the
//  camera sees them; click a tip to look along that axis) and the view strip. Nothing here
//  holds state: everything binds to the Editor object.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

import QtQuick 2.7

Item {
    id: root
    objectName: "EditorViewportOverlay"
    anchors.fill: parent
    visible: Editor.active
    readonly property bool shouldReposition: false

    Item {
        id: gizmo
        width: 104
        height: 104
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.margins: 14

        readonly property real radius: 36
        readonly property real tipRadius: 9.5
        readonly property var labels: ["X", "Y", "Z"]
        readonly property var colors: ["#e5423c", "#8ac926", "#3d85e0"]
        property var tips: []

        function axisVector(axis) {
            return axis === 0 ? Editor.axisX : axis === 1 ? Editor.axisY : Editor.axisZ;
        }

        // the six axis tips in gizmo pixels, far ones first so they draw underneath
        function rebuildTips() {
            var list = [];
            for (var axis = 0; axis < 3; axis++) {
                var v = axisVector(axis);
                for (var sign = 1; sign >= -1; sign -= 2) {
                    list.push({ axis: axis, sign: sign,
                                x: width / 2 + sign * v.x * radius,
                                y: height / 2 - sign * v.y * radius,
                                z: sign * v.z });
                }
            }
            list.sort(function (a, b) { return a.z - b.z; });
            tips = list;
            canvas.requestPaint();
        }

        Connections {
            target: Editor
            function onCameraChanged() { gizmo.rebuildTips(); }
        }
        Component.onCompleted: rebuildTips()

        Canvas {
            id: canvas
            anchors.fill: parent
            onPaint: {
                var ctx = getContext("2d");
                ctx.reset();
                var cx = width / 2;
                var cy = height / 2;
                ctx.textAlign = "center";
                ctx.textBaseline = "middle";
                ctx.font = "bold 10px sans-serif";
                for (var i = 0; i < gizmo.tips.length; i++) {
                    var t = gizmo.tips[i];
                    var color = gizmo.colors[t.axis];
                    if (t.sign > 0) {
                        ctx.lineWidth = 2;
                        ctx.strokeStyle = color;
                        ctx.beginPath();
                        ctx.moveTo(cx, cy);
                        ctx.lineTo(t.x, t.y);
                        ctx.stroke();
                        ctx.fillStyle = color;
                        ctx.beginPath();
                        ctx.arc(t.x, t.y, gizmo.tipRadius, 0, 2 * Math.PI);
                        ctx.fill();
                        ctx.fillStyle = "#1c1c1c";
                        ctx.fillText(gizmo.labels[t.axis], t.x, t.y + 0.5);
                    } else {
                        ctx.lineWidth = 1.5;
                        ctx.strokeStyle = color;
                        ctx.fillStyle = Qt.rgba(0.18, 0.18, 0.18, 0.85);
                        ctx.beginPath();
                        ctx.arc(t.x, t.y, gizmo.tipRadius * 0.7, 0, 2 * Math.PI);
                        ctx.fill();
                        ctx.stroke();
                    }
                }
            }
        }

        MouseArea {
            anchors.fill: parent
            hoverEnabled: true
            onClicked: {
                var best = null;
                var bestDistance = gizmo.tipRadius + 2;
                for (var i = gizmo.tips.length - 1; i >= 0; i--) {   // the near tips are last
                    var t = gizmo.tips[i];
                    var dx = mouse.x - t.x;
                    var dy = mouse.y - t.y;
                    var d = Math.sqrt(dx * dx + dy * dy);
                    if (d < bestDistance) {
                        best = t;
                        bestDistance = d;
                    }
                }
                if (best) {
                    Editor.viewFromAxis(best.axis, best.sign);
                }
            }
        }
    }

    // the selection rectangle while it is dragged
    Rectangle {
        visible: Editor.marqueeActive
        x: Editor.marquee.x
        y: Editor.marquee.y
        width: Editor.marquee.width
        height: Editor.marquee.height
        color: Qt.rgba(0.3, 0.6, 1.0, 0.15)
        border.color: Qt.rgba(0.5, 0.75, 1.0, 0.9)
        border.width: 1
    }

    // the view strip: readouts, bottom left of the viewport
    Rectangle {
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.margins: 12
        width: speedText.implicitWidth + 16
        height: 22
        radius: 3
        color: Qt.rgba(0.16, 0.16, 0.16, 0.8)
        Text {
            id: speedText
            anchors.centerIn: parent
            color: "#d2d2d2"
            font.pixelSize: 11
            text: "Fly " + Editor.flySpeed.toFixed(1) + " m/s"
        }
    }
}
