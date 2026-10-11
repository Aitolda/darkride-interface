//
//  ViewportContainer.cpp
//  interface/src/editor/ui
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include "ViewportContainer.h"

#include <QtGui/QResizeEvent>

#include <gl/GLHelpers.h>

namespace {
    const int SETTLE_MS = 250;   // a splitter drag has stopped when nothing moved for this long
}

ViewportContainer::ViewportContainer(QWidget* viewport, QWidget* parent) : QWidget(parent), _viewport(viewport) {
    setObjectName("ViewportContainer");
    setStyleSheet("#ViewportContainer { background: #000000; }");
    _viewport->setParent(this);
    _viewport->move(0, 0);
    _settle.setSingleShot(true);
    _settle.setInterval(SETTLE_MS);
    connect(&_settle, &QTimer::timeout, this, &ViewportContainer::applySize);
}

void ViewportContainer::setDeferResizes(bool defer) {
    _defer = defer;
    if (!defer) {
        _settle.stop();
        applySize();
    }
}

void ViewportContainer::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    if (_defer) {
        _settle.start();
    } else {
        applySize();
    }
}

void ViewportContainer::applySize() {
    if (_viewport->size() != size()) {
        // never while a frame is drawn to it, nor just after (see gl::ViewChangeGuard): toggling build mode, whose
        // panels resize the view, crashed in Apple's GL driver, clearing the view while it was being resized
        gl::ViewChangeGuard guard;
        _viewport->setGeometry(0, 0, width(), height());
    }
}
