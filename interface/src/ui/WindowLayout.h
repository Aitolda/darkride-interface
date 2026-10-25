//
//  WindowLayout.h
//  interface/src/ui
//
//  The main window's one saved layout (QMainWindow::saveState): where the dock panels are, chat's and Build mode's,
//  docked or floating, their sizes, and whether each is open. Restored once, right after sign-in, so nothing moves
//  under the sign-in screen; saved a moment after any watched panel moves, resizes, floats, docks, opens or closes,
//  and never while quitting. Build mode's panels, made later, take their saved places then (restoreDock). It takes
//  over from Build mode's own layout ("editor/layout3"), which the first restore still reads. Chat docking, step 1 of
//  team/skunkworks/briefs/dockable-panels.md (Caity's "GO CHAT DOCKING", 2026-10-25).
//
//  Copyright 2026 Darkride.
//
//  Distributed under the MIT License.
//

#ifndef hifi_WindowLayout_h
#define hifi_WindowLayout_h

#include <QtCore/QObject>
#include <QtCore/QTimer>

#include <SettingHandle.h>

class QDockWidget;

class WindowLayout : public QObject {
    Q_OBJECT

public:
    static WindowLayout* instance();

    /// after sign-in, once: chat's panel first (so the saved layout finds it), then the saved layout
    void restore();

    /// a dock panel made after the restore takes its saved place, shown or hidden as it was; false if the layout
    /// has none for it (the caller adds it where it starts)
    bool restoreDock(QDockWidget* dock);

    /// the panel's moves, sizes, floating and showing are saved
    void watch(QDockWidget* dock);

    /// a save a moment from now: many changes in a row (a separator's drag) make one save
    void saveSoon();

signals:
    void restored();   // chat's panel exists from here on

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    WindowLayout();
    void save();

    QTimer _saveTimer;
    bool _restored { false };   // no save before the restore: it would write the start's layout over the saved one
    Setting::Handle<QByteArray> _layoutSetting { "window/layout", QByteArray() };
};

#endif // hifi_WindowLayout_h
