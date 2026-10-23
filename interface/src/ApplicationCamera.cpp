//
//  ApplicationCamera.cpp
//  interface/src
//
//  Application's camera and view: the camera modes cycled and set from the menu (cycleCamera, cameraModeChanged,
//  cameraMenuChanged), the switch between first and third person as the boom length changes (changeViewAsNeeded),
//  the head pitch reset when the player is not centred in view (rotationModeChanged), the pick ray from a screen
//  point (computePickRay), the view frustums copied under their lock (copyViewFrustum, copyDisplayViewFrustum), the
//  sensors, avatar and audio reset (resetSensors) and the reticle's position (getMouse). The camera's per-frame
//  update stays with the frame loop (ApplicationFrame.cpp). The members as they stood in Application.cpp, in their
//  order; a home of their own until an owner takes them (ARCHITECTURE.md §5.4 step 4).
//
//  Copyright 2026 Darkride.
//
//  Distributed under the MIT License.
//

#include "Application.h"
#include <plugins/PluginManager.h>
#include <display-plugins/CompositorHelper.h>
#include "AudioClient.h"

using namespace std;

ivec2 Application::getMouse() const {
    return getApplicationCompositor().getReticlePosition();
}

void Application::cycleCamera() {
    auto menu = Menu::getInstance();
    if (menu->isOptionChecked(MenuOption::FirstPersonLookAt)) {

        menu->setIsOptionChecked(MenuOption::FirstPersonLookAt, false);
        menu->setIsOptionChecked(MenuOption::LookAtCamera, true);

    } else if (menu->isOptionChecked(MenuOption::LookAtCamera)) {

        menu->setIsOptionChecked(MenuOption::LookAtCamera, false);
        if (menu->getActionForOption(MenuOption::SelfieCamera)->isVisible()) {
            menu->setIsOptionChecked(MenuOption::SelfieCamera, true);
        } else {
            menu->setIsOptionChecked(MenuOption::FirstPersonLookAt, true);
        }

    } else if (menu->isOptionChecked(MenuOption::SelfieCamera)) {

        menu->setIsOptionChecked(MenuOption::SelfieCamera, false);
        menu->setIsOptionChecked(MenuOption::FirstPersonLookAt, true);

    }
    cameraMenuChanged(); // handle the menu change
}

void Application::cameraModeChanged() {
    switch (_myCamera.getMode()) {
        case CAMERA_MODE_FIRST_PERSON_LOOK_AT:
            Menu::getInstance()->setIsOptionChecked(MenuOption::FirstPersonLookAt, true);
            break;
        case CAMERA_MODE_LOOK_AT:
            Menu::getInstance()->setIsOptionChecked(MenuOption::LookAtCamera, true);
            break;
        case CAMERA_MODE_SELFIE:
            Menu::getInstance()->setIsOptionChecked(MenuOption::SelfieCamera, true);
            break;
        default:
            // we don't have menu items for the others, so just leave it alone.
            return;
    }
    cameraMenuChanged();
}

void Application::changeViewAsNeeded(float boomLength) {
    // Switch between first and third person views as needed
    // This is called when the boom length has changed
    bool boomLengthGreaterThanMinimum = (boomLength > MyAvatar::ZOOM_MIN);

    if (
        (
            _myCamera.getMode() == CAMERA_MODE_FIRST_PERSON_LOOK_AT ||
            _myCamera.getMode() == CAMERA_MODE_FIRST_PERSON
        ) && boomLengthGreaterThanMinimum
    ) {
        Menu::getInstance()->setIsOptionChecked(MenuOption::FirstPersonLookAt, false);
        Menu::getInstance()->setIsOptionChecked(MenuOption::LookAtCamera, true);
        cameraMenuChanged();
    } else if (
        (
            _myCamera.getMode() == CAMERA_MODE_LOOK_AT ||
            _myCamera.getMode() == CAMERA_MODE_THIRD_PERSON
        ) && !boomLengthGreaterThanMinimum
    ) {
        Menu::getInstance()->setIsOptionChecked(MenuOption::FirstPersonLookAt, true);
        Menu::getInstance()->setIsOptionChecked(MenuOption::LookAtCamera, false);
        cameraMenuChanged();
    }
}

void Application::cameraMenuChanged() {
    auto menu = Menu::getInstance();
    if (menu->isOptionChecked(MenuOption::FirstPersonLookAt)) {
        if (_myCamera.getMode() != CAMERA_MODE_FIRST_PERSON_LOOK_AT) {
            _myCamera.setMode(CAMERA_MODE_FIRST_PERSON_LOOK_AT);
            getMyAvatar()->setBoomLength(MyAvatar::ZOOM_MIN);
        }
    } else if (menu->isOptionChecked(MenuOption::LookAtCamera)) {
        if (_myCamera.getMode() != CAMERA_MODE_LOOK_AT) {
            _myCamera.setMode(CAMERA_MODE_LOOK_AT);
            if (getMyAvatar()->getBoomLength() == MyAvatar::ZOOM_MIN) {
                getMyAvatar()->setBoomLength(MyAvatar::ZOOM_DEFAULT);
            }
        }
    } else if (menu->isOptionChecked(MenuOption::SelfieCamera)) {
        if (_myCamera.getMode() != CAMERA_MODE_SELFIE) {
            _myCamera.setMode(CAMERA_MODE_SELFIE);
            if (getMyAvatar()->getBoomLength() == MyAvatar::ZOOM_MIN) {
                getMyAvatar()->setBoomLength(MyAvatar::ZOOM_DEFAULT);
            }
        }
    }
}

void Application::rotationModeChanged() const {
    if (!Menu::getInstance()->isOptionChecked(MenuOption::CenterPlayerInView)) {
        getMyAvatar()->setHeadPitch(0);
    }
}

PickRay Application::computePickRay(float x, float y) const {
    vec2 pickPoint { x, y };
    PickRay result;
    if (isHMDMode()) {
        getApplicationCompositor().computeHmdPickRay(pickPoint, result.origin, result.direction);
    } else {
        pickPoint /= getCanvasSize();
        if (_myCamera.getMode() == CameraMode::CAMERA_MODE_MIRROR) {
            pickPoint.x = 1.0f - pickPoint.x;
        }
        QMutexLocker viewLocker(&_viewMutex);
        _viewFrustum.computePickRay(pickPoint.x, pickPoint.y, result.origin, result.direction);
    }
    return result;
}

void Application::copyViewFrustum(ViewFrustum& viewOut) const {
    QMutexLocker viewLocker(&_viewMutex);
    viewOut = _viewFrustum;
}

void Application::copyDisplayViewFrustum(ViewFrustum& viewOut) const {
    QMutexLocker viewLocker(&_viewMutex);
    viewOut = _displayViewFrustum;
}

// resentSensors() is a bit of vestigial feature. It used to be used for Oculus DK2 to recenter the view around
// the current head orientation.  With the introduction of "room scale" tracking we no longer need that particular
// feature.  However, we still use this to reset face trackers, eye trackers, audio and to optionally re-load the avatar
// rig and animations from scratch.
void Application::resetSensors(bool andReload) {
    _overlayConductor.centerUI();
    getActiveDisplayPlugin()->resetSensors();
    getMyAvatar()->reset(true, andReload);
    QMetaObject::invokeMethod(DependencyManager::get<AudioClient>().data(), "reset", Qt::QueuedConnection);
}
