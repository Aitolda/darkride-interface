//
//  ApplicationSnapshots.cpp
//  interface/src
//
//  Application's snapshots: the operators queued for the next frame's render and drained by the frame loop
//  (addSnapshotOperator, takeSnapshotOperators), the still and the animated snapshot (takeSnapshot), the secondary
//  camera's snapshot and 360 snapshot (takeSecondaryCameraSnapshot, takeSecondaryCamera360Snapshot) and the upload
//  (shareSnapshot). The members as they stood in Application.cpp, in their order; a home of their own until an owner
//  takes them (ARCHITECTURE.md §5.4 step 4).
//
//  Copyright 2026 Darkride.
//
//  Distributed under the MIT License.
//

#include "Application.h"
#include <glm/glm.hpp>
#include "scripting/TestScriptingInterface.h"
#include "scripting/WindowScriptingInterface.h"
#include "ui/Snapshot.h"
#include "ui/SnapshotAnimated.h"

using namespace std;

void Application::addSnapshotOperator(const SnapshotOperator& snapshotOperator) {
    std::lock_guard<std::mutex> lock(_snapshotMutex);
    _snapshotOperators.push(snapshotOperator);
    _hasPrimarySnapshot = _hasPrimarySnapshot || std::get<2>(snapshotOperator);
}

bool Application::takeSnapshotOperators(std::queue<SnapshotOperator>& snapshotOperators) {
    std::lock_guard<std::mutex> lock(_snapshotMutex);
    bool hasPrimarySnapshot = _hasPrimarySnapshot;
    _hasPrimarySnapshot = false;
    _snapshotOperators.swap(snapshotOperators);
    return hasPrimarySnapshot;
}

void Application::takeSnapshot(bool notify, bool includeAnimated, float aspectRatio, const QString& filename) {
    addSnapshotOperator(std::make_tuple([notify, includeAnimated, aspectRatio, filename](const QImage& snapshot) {
        qApp->postLambdaEvent([snapshot, notify, includeAnimated, aspectRatio, filename] {
            QString path = DependencyManager::get<Snapshot>()->saveSnapshot(snapshot, filename, TestScriptingInterface::getInstance()->getTestResultsLocation());

            // If we're not doing an animated snapshot as well...
            if (!includeAnimated) {
                if (!path.isEmpty()) {
                    // Tell the dependency manager that the capture of the still snapshot has taken place.
                    emit DependencyManager::get<WindowScriptingInterface>()->stillSnapshotTaken(path, notify);
                }
            } else if (!SnapshotAnimated::isAlreadyTakingSnapshotAnimated()) {
                // Get an animated GIF snapshot and save it
                SnapshotAnimated::saveSnapshotAnimated(path, aspectRatio, DependencyManager::get<WindowScriptingInterface>());
            }
        });
    }, aspectRatio, true));
}

void Application::takeSecondaryCameraSnapshot(const bool& notify, const QString& filename) {
    addSnapshotOperator(std::make_tuple([notify, filename](const QImage& snapshot) {
        qApp->postLambdaEvent([snapshot, notify, filename] {
            QString snapshotPath = DependencyManager::get<Snapshot>()->saveSnapshot(snapshot, filename, TestScriptingInterface::getInstance()->getTestResultsLocation());

            emit DependencyManager::get<WindowScriptingInterface>()->stillSnapshotTaken(snapshotPath, notify);
        });
    }, 0.0f, false));
}

void Application::takeSecondaryCamera360Snapshot(const glm::vec3& cameraPosition, const bool& cubemapOutputFormat, const bool& notify, const QString& filename) {
    postLambdaEvent([notify, filename, cubemapOutputFormat, cameraPosition] {
        DependencyManager::get<Snapshot>()->save360Snapshot(cameraPosition, cubemapOutputFormat, notify, filename);
    });
}

void Application::shareSnapshot(const QString& path, const QUrl& href) {
    postLambdaEvent([path, href] {
        // not much to do here, everything is done in snapshot code...
        DependencyManager::get<Snapshot>()->uploadSnapshot(path, href);
    });
}
