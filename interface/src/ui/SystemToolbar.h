//
//  SystemToolbar.h
//  interface/src/ui
//
//  The bar of app buttons (the system tablet's: Audio, Settings, Snap, Avatar, People, Explore, Create...) as a
//  native toolbar (QToolBar) on a side of the main window, so the 3D view sits beside it instead of under it (a
//  player's ask, through Caity, 10-24). place "floating" keeps the QML bar over the bottom of the view (hifi/Desktop.qml's
//  sysToolbar, as before); "top", "left", "right" or "bottom" docks this one there, and dragging it by its handle to
//  another side moves it. The sign-in screen and VR hide whichever bar is in use: SignInGate says when a bar may show
//  (setAllowed). Its look is the Qt style sheet's, so a skin reaches it. QML panels set the place (Settings).
//
//  Copyright 2026 Darkride.
//
//  Distributed under the MIT License.
//

#ifndef hifi_SystemToolbar_h
#define hifi_SystemToolbar_h

#include <QtCore/QPointer>
#include <QtWidgets/QToolBar>

#include <SettingHandle.h>

class QMainWindow;
class QToolButton;
class TabletButtonProxy;
class TabletProxy;

class SystemToolbar : public QToolBar {
    Q_OBJECT
    Q_PROPERTY(QString place READ getPlace WRITE setPlace NOTIFY placeChanged)
    Q_PROPERTY(bool docked READ isDocked NOTIFY placeChanged)

public:
    static SystemToolbar* instance();

    /// on the main window, once it and the system tablet exist
    void attach(QMainWindow* window, TabletProxy* tablet);

    QString getPlace() const;
    void setPlace(const QString& place);
    bool isDocked() const { return getPlace() != "floating"; }

    /// the sign-in gate's say: a bar may show (false on the sign-in screen)
    void setAllowed(bool allowed);

    /// a skin is on: the bar drops its own look for the application's style sheet (Skins)
    void setSkinned(bool skinned);

signals:
    void placeChanged();

protected:
    bool event(QEvent* event) override;   // a drag by the handle let go: remember the side it went to

private:
    SystemToolbar();

    void rebuild();
    void refresh(QToolButton* button, TabletButtonProxy* proxy);
    void updateVisibility();
    void rememberArea();
    QPixmap buttonPicture(const QVariantMap& properties, bool active) const;

    QPointer<QMainWindow> _window;
    QPointer<TabletProxy> _tablet;
    Setting::Handle<QString> _placeSetting { "toolbar/systemPlace", "floating" };
    // no bar until the sign-in gate says one may show (SignInGate::resumeAfterLoginDialogActionTaken), as the QML bar
    // always waited: a docked bar shown at start-up and hidden by the sign-in screen resized the 3D view twice, and
    // the sign-in dialog's clicks landed on a stale size (a bar at the bottom moved them off "Continue as", 10-24)
    bool _allowed { false };
};

#endif // hifi_SystemToolbar_h
