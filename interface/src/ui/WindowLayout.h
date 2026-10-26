//
//  WindowLayout.h
//  interface/src/ui
//
//  The main window's saved layouts (QMainWindow::saveState): where the dock panels are, docked or floating, their
//  sizes, and whether each is open. Normal play and Build mode each have their own (Caity's "go step 2", 2026-10-26),
//  so each opens the way it was left: entering Build mode saves normal play's and puts Build mode's back, leaving does
//  the reverse. Normal play's is restored once, right after sign-in, so nothing moves under the sign-in screen; the
//  current one is saved a moment after any watched panel moves, resizes, floats, docks, opens or closes, and never
//  while quitting. A panel made later takes its saved place then (restoreDock). It took over from Build mode's own
//  layout ("editor/layout3"), which the first restore still reads. Chat docking, step 1 of
//  team/skunkworks/briefs/dockable-panels.md (Caity's "GO CHAT DOCKING", 2026-10-25); dockable panels, step 2.
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

    /// Build mode's own arrangement: normal play's saved, Build mode's put back as it was left; false when Build mode
    /// has none yet (the caller places its panels where they start, the others staying as they are)
    bool enterBuildMode();
    /// Build mode's arrangement saved, normal play's put back
    void leaveBuildMode();

signals:
    void restored();   // chat's panel exists from here on

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    WindowLayout();
    void save();

    QTimer _saveTimer;
    bool _restored { false };   // no save before the restore: it would write the start's layout over the saved one
    bool _buildMode { false };
    Setting::Handle<QByteArray> _layoutSetting { "window/layout", QByteArray() };             // normal play's
    Setting::Handle<QByteArray> _buildLayoutSetting { "window/buildLayout", QByteArray() };   // Build mode's
};

#endif // hifi_WindowLayout_h
