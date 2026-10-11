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

#ifdef Q_OS_MACOS
#include <CoreFoundation/CoreFoundation.h>
#endif

namespace {
    const int SETTLE_MS = 250;   // a splitter drag has stopped when nothing moved for this long

#ifdef Q_OS_MACOS
    // Core Animation commits a pass's view changes from a main run loop observer of order 2000000, as the loop is
    // about to wait or exits; one just after it knows they are in place, and the pause after a change can end
    const CFIndex AFTER_CORE_ANIMATION_COMMIT = 2000001;

    void afterCoreAnimationCommit(CFRunLoopObserverRef, CFRunLoopActivity, void*) {
        gl::viewChangesCommitted();
    }
#endif
}

ViewportContainer::ViewportContainer(QWidget* viewport, QWidget* parent) : QWidget(parent), _viewport(viewport) {
    setObjectName("ViewportContainer");
    setStyleSheet("#ViewportContainer { background: #000000; }");
    _viewport->setParent(this);
    _viewport->move(0, 0);
    _settle.setSingleShot(true);
    _settle.setInterval(SETTLE_MS);
    connect(&_settle, &QTimer::timeout, this, &ViewportContainer::applySize);
#ifdef Q_OS_MACOS
    static CFRunLoopObserverRef commitObserver = [] {
        CFRunLoopObserverRef observer = CFRunLoopObserverCreate(kCFAllocatorDefault, kCFRunLoopBeforeWaiting | kCFRunLoopExit,
            true, AFTER_CORE_ANIMATION_COMMIT, &afterCoreAnimationCommit, nullptr);
        CFRunLoopAddObserver(CFRunLoopGetMain(), observer, kCFRunLoopCommonModes);
        return observer;
    }();
    (void)commitObserver;
#endif
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
