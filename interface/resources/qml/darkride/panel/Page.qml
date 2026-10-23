//
//  Page.qml
//  qml/darkride/panel
//
//  A panel's page: the large title, then its sections on the grouped background, scrolling.
//
//  Copyright 2026 Darkride.
//
//  Distributed under the MIT License.
//

import QtQuick 2.15
import QtQuick.Controls 2.15 as QQC2
import QtQuick.Layouts 1.15
import "."

Rectangle {
    id: page

    property string title: ""
    default property alias content: column.data

    color: Theme.background

    QQC2.ScrollView {
        id: scroll
        objectName: "pageScroll"   // a test can scroll the page by it
        anchors.fill: parent
        clip: true
        contentWidth: availableWidth
        contentHeight: holder.implicitHeight

        Item {
            id: holder
            width: scroll.availableWidth
            // room under the last section, so it doesn't sit on the window's edge
            implicitHeight: column.implicitHeight + Theme.pageMargin * 1.5

            ColumnLayout {
                id: column
                x: Theme.pageMargin
                width: parent.width - 2 * Theme.pageMargin
                spacing: 0

                Text {
                    visible: page.title !== ""
                    text: page.title
                    color: Theme.text
                    font.pixelSize: Theme.largeTitleSize
                    font.bold: true
                    topPadding: 14 * Theme.unit
                    bottomPadding: 2 * Theme.unit
                    leftPadding: 4 * Theme.unit
                }
            }
        }
    }
}
