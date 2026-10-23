//
//  ApplicationCursor.cpp
//  interface/src
//
//  Application's cursor: the icon the compositor draws, and the system cursor hidden or shown over the GL widget
//  (showCursor, updateSystemCursor), applied on the main thread when it next looks (checkChangeCursor). The members
//  as they stood in Application.cpp, in their order; a home of their own until an owner takes them (ARCHITECTURE.md
//  §5.4 step 4).
//
//  Copyright 2026 Darkride.
//
//  Distributed under the MIT License.
//

#include "Application.h"
#include <MainWindow.h>
#if defined(Q_OS_MACOS) || defined(Q_OS_WIN)
#include "SpeechRecognizer.h"
#endif
#if defined(Q_OS_MACOS)
#include "AppNapDisabler.h"
#endif

using namespace std;

void Application::checkChangeCursor() {
    QMutexLocker locker(&_changeCursorLock);
    if (_cursorNeedsChanging) {
#ifdef Q_OS_MACOS
        auto cursorTarget = _window; // macOS doesn't seem to provide for hiding the cursor only on the GL widget
#else
        // On windows and linux, hiding the top level cursor also means it's invisible when hovering over the
        // window menu, which is a pain, so only hide it for the GL surface
        auto cursorTarget = _glWidget;
#endif
        cursorTarget->setCursor(_desiredCursor);

        _cursorNeedsChanging = false;
    }
}

void Application::showCursor(const Cursor::Icon& cursor) {
    QMutexLocker locker(&_changeCursorLock);

    auto managedCursor = Cursor::Manager::instance().getCursor();
    auto curIcon = managedCursor->getIcon();
    if (curIcon != cursor) {
        managedCursor->setIcon(cursor);
        curIcon = cursor;
    }
    _desiredCursor = cursor == Cursor::Icon::SYSTEM ? Qt::ArrowCursor : Qt::BlankCursor;
    _cursorNeedsChanging = true;
}

void Application::updateSystemCursor(const Qt::CursorShape cursor) {
    QMutexLocker locker(&_changeCursorLock);

    if (_desiredCursor != Qt::BlankCursor && _desiredCursor != cursor) {
        _desiredCursor = cursor;
        _cursorNeedsChanging = true;
    }
}
