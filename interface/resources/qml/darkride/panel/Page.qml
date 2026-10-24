//
//  Page.qml
//  qml/darkride/panel
//
//  A panel's page: the large title, its tabs (a segmented control under the title, when it has more than one), then
//  its sections on the grouped background, scrolling. A tab's sections sit in a Tab whose index says which it is.
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
    property var tabs: []          // the tabs' names; none or one: no tab bar
    property int currentTab: 0
    default property alias content: column.data

    color: Theme.background

    // closes the panel the page is on, as its close box does: a desktop window hides (the bar's button follows it);
    // the tablet goes back to its home screen. Explore closes itself so a hop isn't watched from behind it.
    function closePanel() {
        for (var item = page.parent; item; item = item.parent) {
            if (item.hasOwnProperty("shown") && item.windowClosed !== undefined) {
                item.shown = false;
                item.windowClosed();
                return;
            }
        }
        var tablet = Tablet.getTablet("com.highfidelity.interface.tablet.system");
        if (tablet) {
            tablet.gotoHomeScreen();
        }
    }

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

                Segmented {
                    visible: page.tabs.length > 1
                    Layout.alignment: Qt.AlignHCenter
                    Layout.topMargin: 10 * Theme.unit
                    model: page.tabs.map(function (name, index) { return { text: name, value: index }; })
                    current: page.currentTab
                    onChosen: {
                        page.currentTab = value;
                        scroll.contentItem.contentY = 0;
                    }
                }
            }
        }
    }
}
