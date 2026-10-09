//
//  InspectorPanel.cpp
//  interface/src/editor/ui
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include "InspectorPanel.h"

#include <QtWidgets/QLabel>
#include <QtWidgets/QScrollArea>

InspectorPanel::InspectorPanel(QWidget* parent) : QDockWidget("Inspector", parent) {
    setObjectName("EditorInspector");
    setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    setMinimumWidth(280);

    _scroll = new QScrollArea(this);
    _scroll->setWidgetResizable(true);
    _scroll->setFrameShape(QFrame::NoFrame);

    _empty = new QLabel("Nothing selected", _scroll);
    _empty->setAlignment(Qt::AlignCenter);
    _empty->setObjectName("EditorInspectorEmpty");

    setContent(nullptr);
    setWidget(_scroll);
}

void InspectorPanel::setContent(QWidget* content) {
    if (!content) {
        content = _empty;
    }
    if (_scroll->widget() == content) {
        return;
    }
    // takeWidget hands ownership back so the previous content survives the swap
    _scroll->takeWidget();
    _scroll->setWidget(content);
}
