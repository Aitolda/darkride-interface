//
//  ApplicationImport.cpp
//  interface/src
//
//  Application's content in and out: a model, image or zip from a URL or a drop becomes an asset on the domain's
//  asset server (the download, the unzip, the upload, the mapping) and then an entity, with the size check and the
//  info and error boxes along the way (addAssetToWorld and its steps); the URL dispatch (canAcceptURL, acceptURL and
//  the table of importers by extension, with the extensions ImportExtensions.h declares); the import wrappers for a
//  dropped or opened file (importJSONFromURL, importSVOFromURL, importFromZIP, importImage, acceptSnapshot); and the
//  entities' export, import and paste (exportEntities, importEntities, pasteEntities). The members as they stood in
//  Application.cpp, in their order, with the one helper they share; a home of their own until an owner takes them
//  (ARCHITECTURE.md §5.4 step 4).
//
//  Copyright 2026 Darkride.
//
//  Distributed under the MIT License.
//

#include "Application.h"
#include <glm/glm.hpp>
#include <QtWidgets/QMessageBox>
#include <QTemporaryDir>
#include <AddressManager.h>
#include <AssetClient.h>
#include <AssetUpload.h>
#include <EntityScriptingInterface.h>
#include <MainWindow.h>
#include <MappingRequest.h>
#include <OffscreenUi.h>
#include <ResourceRequest.h>
#include "InterfaceLogging.h"
#include "scripting/AssetMappingsScriptingInterface.h"
#include "ui/Snapshot.h"

#include "ImportExtensions.h"

using namespace std;

QSharedPointer<OffscreenUi> getOffscreenUI();   // Application.cpp's

void Application::addAssetToWorldFromURL(QString url) {

    QString filename;
    if (url.contains("filename")) {
        filename = url.section("filename=", 1, 1);  // Filename is in "?filename=" parameter at end of URL.
    }
    if (url.contains("poly.google.com/downloads")) {
        filename = url.section('/', -1);
        if (url.contains("noDownload")) {
            filename.remove(".zip?noDownload=false");
        } else {
            filename.remove(".zip");
        }

    }

    if (!DependencyManager::get<NodeList>()->getThisNodeCanWriteAssets()) {
        // QString errorInfo = "You do not have permissions to write to the Asset Server.";
        // qDebug(interfaceapp) << "Error downloading model: " + errorInfo;
        // addAssetToWorldError(filename, errorInfo);
        return;
    }

    addAssetToWorldInfo(filename, "Downloading model file " + filename + ".");

    auto request = DependencyManager::get<ResourceManager>()->createResourceRequest(
        nullptr, QUrl(url), true, -1, "Application::addAssetToWorldFromURL");
    connect(request, &ResourceRequest::finished, this, &Application::addAssetToWorldFromURLRequestFinished);
    request->send();
}

void Application::addAssetToWorldFromURLRequestFinished() {
    auto request = qobject_cast<ResourceRequest*>(sender());
    auto url = request->getUrl().toString();
    auto result = request->getResult();

    QString filename;
    bool isBlocks = false;

    if (url.contains("filename")) {
        filename = url.section("filename=", 1, 1);  // Filename is in "?filename=" parameter at end of URL.
    }
    if (url.contains("poly.google.com/downloads")) {
        filename = url.section('/', -1);
        if (url.contains("noDownload")) {
            filename.remove(".zip?noDownload=false");
        } else {
            filename.remove(".zip");
        }
        isBlocks = true;
    }

    if (result == ResourceRequest::Success) {
        QTemporaryDir temporaryDir;
        temporaryDir.setAutoRemove(false);
        if (temporaryDir.isValid()) {
            QString temporaryDirPath = temporaryDir.path();
            QString downloadPath = temporaryDirPath + "/" + filename;

            QFile tempFile(downloadPath);
            if (tempFile.open(QIODevice::WriteOnly)) {
                tempFile.write(request->getData());
                addAssetToWorldInfoClear(filename);  // Remove message from list; next one added will have a different key.
                tempFile.close();
                qApp->getFileDownloadInterface()->runUnzip(downloadPath, url, true, false, isBlocks);
            } else {
                qWarning(interfaceapp) << "Couldn't open temporary file for download";
                addAssetToWorldError(filename, "It couldn't be downloaded.");
            }
        } else {
            qWarning(interfaceapp) << "Couldn't create temporary directory for download";
            addAssetToWorldError(filename, "It couldn't be downloaded.");
        }
    } else {
        qWarning(interfaceapp) << "Error downloading" << url << ":" << request->getResultString();
        addAssetToWorldError(filename, "It couldn't be downloaded.");
    }

    request->deleteLater();
}


QString filenameFromPath(QString filePath) {
    return filePath.right(filePath.length() - filePath.lastIndexOf("/") - 1);
}

void Application::addAssetToWorldUnzipFailure(QString filePath) {
    QString filename = filenameFromPath(QUrl(filePath).toLocalFile());
    qWarning(interfaceapp) << "Couldn't unzip file" << filePath;
    addAssetToWorldError(filename, "It couldn't be unzipped.");
}

void Application::addAssetToWorld(QString path, QString zipFile, bool isZip, bool isBlocks) {
    // Automatically upload and add asset to world as an alternative manual process initiated by showAssetServerWidget().
    QString mapping;
    QString filename = filenameFromPath(path);
    if (isZip || isBlocks) {
        QString assetName = zipFile.section("/", -1).remove(QRegExp("[.]zip(.*)$"));
        QString assetFolder = path.section("model_repo/", -1);
        mapping = "/" + assetName + "/" + assetFolder;
    } else {
        mapping = "/" + filename;
    }

    // Test repeated because possibly different code paths.
    if (!DependencyManager::get<NodeList>()->getThisNodeCanWriteAssets()) {
        // QString errorInfo = "You do not have permissions to write to the Asset Server.";
        // qWarning(interfaceapp) << "Error downloading model: " + errorInfo;
        // addAssetToWorldError(filename, errorInfo);
        return;
    }

    addAssetToWorldInfo(filename, "Adding " + mapping.mid(1) + " to the Asset Server.");

    addAssetToWorldWithNewMapping(path, mapping, 0, isZip, isBlocks);
}

void Application::addAssetToWorldWithNewMapping(QString filePath, QString mapping, int copy, bool isZip, bool isBlocks) {
    auto request = DependencyManager::get<AssetClient>()->createGetMappingRequest(mapping);

    QObject::connect(request, &GetMappingRequest::finished, this, [=](GetMappingRequest* request) mutable {
        const int MAX_COPY_COUNT = 100;  // Limit number of duplicate assets; recursion guard.
        auto result = request->getError();
        if (result == GetMappingRequest::NotFound) {
            addAssetToWorldUpload(filePath, mapping, isZip, isBlocks);
        } else if (result != GetMappingRequest::NoError) {
            QString errorInfo = "Could not map asset name: "
                + mapping.left(mapping.length() - QString::number(copy).length() - 1);
            //qWarning(interfaceapp) << "Error downloading model: " + errorInfo;
            qDebug() << "Couldn't map asset name... Error downloading model: " + errorInfo;
           // addAssetToWorldError(filenameFromPath(filePath), errorInfo);
        } else if (copy < MAX_COPY_COUNT - 1) {
            if (copy > 0) {
                mapping = mapping.remove(mapping.lastIndexOf("-"), QString::number(copy).length() + 1);
            }
            copy++;
            mapping = mapping.insert(mapping.lastIndexOf("."), "-" + QString::number(copy));
            addAssetToWorldWithNewMapping(filePath, mapping, copy, isZip, isBlocks);
        } else {
            QString original = mapping.left(mapping.length() - QString::number(copy).length() - 1);
            qWarning(interfaceapp) << "Error downloading model: Too many copies of asset name: " + original;
            addAssetToWorldError(filenameFromPath(filePath),
                                 "There are too many files called " + filenameFromPath(original) + ".");
        }
        request->deleteLater();
    });

    request->start();
}

void Application::addAssetToWorldUpload(QString filePath, QString mapping, bool isZip, bool isBlocks) {
    qInfo(interfaceapp) << "Uploading" << filePath << "to Asset Server as" << mapping;
    auto upload = DependencyManager::get<AssetClient>()->createUpload(filePath);
    QObject::connect(upload, &AssetUpload::finished, this, [=](AssetUpload* upload, const QString& hash) mutable {
        if (upload->getError() != AssetUpload::NoError) {
            QString errorInfo = "Unspecified error downloading from the Asset Server.";
            qDebug(interfaceapp) << "Error downloading model: " + errorInfo;
            //addAssetToWorldError(filenameFromPath(filePath), errorInfo);
        } else {
            addAssetToWorldSetMapping(filePath, mapping, hash, isZip, isBlocks);
        }

        // Remove temporary directory created by Clara.io market place download.
        int index = filePath.lastIndexOf("/model_repo/");
        if (index > 0) {
            QString tempDir = filePath.left(index);
            qCDebug(interfaceapp) << "Removing temporary directory at: " + tempDir;
            QDir(tempDir).removeRecursively();
        }

        upload->deleteLater();
    });

    upload->start();
}

void Application::addAssetToWorldSetMapping(QString filePath, QString mapping, QString hash, bool isZip, bool isBlocks) {
    auto request = DependencyManager::get<AssetClient>()->createSetMappingRequest(mapping, hash);
    connect(request, &SetMappingRequest::finished, this, [=](SetMappingRequest* request) mutable {
        if (request->getError() != SetMappingRequest::NoError) {
            qWarning(interfaceapp) << "Error downloading model: Could not set asset mapping.";
            addAssetToWorldError(filenameFromPath(filePath), "The world's assets didn't take it.");
        } else {
            // to prevent files that aren't models or texture files from being loaded into world automatically
            if ((filePath.toLower().endsWith(OBJ_EXTENSION) || filePath.toLower().endsWith(FBX_EXTENSION)) ||
                ((filePath.toLower().endsWith(JPG_EXTENSION) || filePath.toLower().endsWith(PNG_EXTENSION)) &&
                ((!isBlocks) && (!isZip)))) {
                addAssetToWorldAddEntity(filePath, mapping);
            } else {
                qCDebug(interfaceapp) << "Zipped contents are not supported entity files";
                addAssetToWorldInfoDone(filenameFromPath(filePath));
            }
        }
        request->deleteLater();
    });

    request->start();
}

void Application::addAssetToWorldAddEntity(QString filePath, QString mapping) {
    EntityItemProperties properties;
    properties.setName(mapping.right(mapping.length() - 1));
    if (filePath.toLower().endsWith(PNG_EXTENSION) || filePath.toLower().endsWith(JPG_EXTENSION)) {
        properties.setType(EntityTypes::Image);
        properties.setImageURL(QString("atp:" + mapping));
        properties.setKeepAspectRatio(false);
    } else {
        properties.setType(EntityTypes::Model);
        properties.setModelURL("atp:" + mapping);
        properties.setShapeType(SHAPE_TYPE_SIMPLE_COMPOUND);
    }
    properties.setCollisionless(true);  // Temporarily set so that doesn't collide with avatar.
    properties.setVisible(false);  // Temporarily set so that don't see at large unresized dimensions.
    bool grabbable = (Menu::getInstance()->isOptionChecked(MenuOption::CreateEntitiesGrabbable));
    properties.setUserData(grabbable ? GRABBABLE_USER_DATA : NOT_GRABBABLE_USER_DATA);
    glm::vec3 positionOffset = getMyAvatar()->getWorldOrientation() * (getMyAvatar()->getSensorToWorldScale() * glm::vec3(0.0f, 0.0f, -2.0f));
    properties.setPosition(getMyAvatar()->getWorldPosition() + positionOffset);
    properties.setRotation(getMyAvatar()->getWorldOrientation());
    properties.setGravity(glm::vec3(0.0f, 0.0f, 0.0f));
    auto entityID = DependencyManager::get<EntityScriptingInterface>()->addEntity(properties);

    // Note: Model dimensions are not available here; model is scaled per FBX mesh in RenderableModelEntityItem::update() later
    // on. But FBX dimensions may be in cm, so we monitor for the dimension change and rescale again if warranted.

    if (entityID == QUuid()) {
        qWarning(interfaceapp) << "Could not add model to world: " + mapping;
        addAssetToWorldError(filenameFromPath(filePath), "It couldn't be placed.");
    } else {
        // Monitor when asset is rendered in world so that can resize if necessary.
        _addAssetToWorldResizeList.insert(entityID, 0);  // List value is count of checks performed.
        if (!_addAssetToWorldResizeTimer.isActive()) {
            _addAssetToWorldResizeTimer.start();
        }

        // Close progress message box.
        addAssetToWorldInfoDone(filenameFromPath(filePath));
    }
}

void Application::addAssetToWorldCheckModelSize() {
    if (_addAssetToWorldResizeList.size() == 0) {
        return;
    }

    auto item = _addAssetToWorldResizeList.begin();
    while (item != _addAssetToWorldResizeList.end()) {
        auto entityID = item.key();

        EntityPropertyFlags propertyFlags;
        propertyFlags += PROP_NAME;
        propertyFlags += PROP_DIMENSIONS;
        auto entityScriptingInterface = DependencyManager::get<EntityScriptingInterface>();
        auto properties = entityScriptingInterface->getEntityProperties(entityID, propertyFlags);
        auto name = properties.getName();
        auto dimensions = properties.getDimensions();

        bool doResize = false;

        const glm::vec3 DEFAULT_DIMENSIONS = glm::vec3(0.1f, 0.1f, 0.1f);
        if (dimensions != DEFAULT_DIMENSIONS) {

            // Scale model so that its maximum is exactly specific size.
            const float MAXIMUM_DIMENSION = getMyAvatar()->getSensorToWorldScale();
            auto previousDimensions = dimensions;
            auto scale = std::min(MAXIMUM_DIMENSION / dimensions.x, std::min(MAXIMUM_DIMENSION / dimensions.y,
                MAXIMUM_DIMENSION / dimensions.z));
            dimensions *= scale;
            qInfo(interfaceapp) << "Model" << name << "auto-resized from" << previousDimensions << " to " << dimensions;
            doResize = true;

            item = _addAssetToWorldResizeList.erase(item);  // Finished with this entity; advance to next.
        } else {
            // Increment count of checks done.
            _addAssetToWorldResizeList[entityID]++;

            const int CHECK_MODEL_SIZE_MAX_CHECKS = 300;
            if (_addAssetToWorldResizeList[entityID] > CHECK_MODEL_SIZE_MAX_CHECKS) {
                // Have done enough checks; model was either the default size or something's gone wrong.

                // Rescale all dimensions.
                const glm::vec3 UNIT_DIMENSIONS = glm::vec3(1.0f, 1.0f, 1.0f);
                dimensions = UNIT_DIMENSIONS;
                qInfo(interfaceapp) << "Model" << name << "auto-resize timed out; resized to " << dimensions;
                doResize = true;

                item = _addAssetToWorldResizeList.erase(item);  // Finished with this entity; advance to next.
            } else {
                // No action on this entity; advance to next.
                ++item;
            }
        }

        if (doResize) {
            EntityItemProperties properties;
            properties.setDimensions(dimensions);
            properties.setVisible(true);
            if (!name.toLower().endsWith(PNG_EXTENSION) && !name.toLower().endsWith(JPG_EXTENSION)) {
                properties.setCollisionless(false);
            }
            bool grabbable = (Menu::getInstance()->isOptionChecked(MenuOption::CreateEntitiesGrabbable));
            properties.setUserData(grabbable ? GRABBABLE_USER_DATA : NOT_GRABBABLE_USER_DATA);
            properties.setLastEdited(usecTimestampNow());
            entityScriptingInterface->editEntity(entityID, properties);
        }
    }

    // Stop timer if nothing in list to check.
    if (_addAssetToWorldResizeList.size() == 0) {
        _addAssetToWorldResizeTimer.stop();
    }
}


void Application::addAssetToWorldInfo(QString modelName, QString infoText) {
    // Displays the most recent info message, subject to being overridden by error messages.

    if (_aboutToQuit) {
        return;
    }

    /*
    Cancel info timer if running.
    If list has an entry for modelName, delete it (just one).
    Append modelName, infoText to list.
    Display infoText in message box unless an error is being displayed (i.e., error timer is running).
    Show message box if not already visible.
    */

    _addAssetToWorldInfoTimer.stop();

    addAssetToWorldInfoClear(modelName);

    _addAssetToWorldInfoKeys.append(modelName);
    _addAssetToWorldInfoMessages.append(infoText);

    return; // this message is annoying and disruptive
    if (!_addAssetToWorldErrorTimer.isActive()) {
        if (!_addAssetToWorldMessageBox) {
            _addAssetToWorldMessageBox = getOffscreenUI()->createMessageBox(OffscreenUi::ICON_INFORMATION,
                "Downloading Model", "", QMessageBox::NoButton, QMessageBox::NoButton);
            connect(_addAssetToWorldMessageBox, SIGNAL(destroyed()), this, SLOT(onAssetToWorldMessageBoxClosed()));
        }

        _addAssetToWorldMessageBox->setProperty("text", "\n" + infoText);
        _addAssetToWorldMessageBox->setVisible(true);
    }
}

void Application::addAssetToWorldInfoClear(QString modelName) {
    // Clears modelName entry from message list without affecting message currently displayed.

    if (_aboutToQuit) {
        return;
    }

    /*
    Delete entry for modelName from list.
    */

    auto index = _addAssetToWorldInfoKeys.indexOf(modelName);
    if (index > -1) {
        _addAssetToWorldInfoKeys.removeAt(index);
        _addAssetToWorldInfoMessages.removeAt(index);
    }
}

void Application::addAssetToWorldInfoDone(QString modelName) {
    // Continues to display this message if the latest for a few seconds, then deletes it and displays the next latest.

    if (_aboutToQuit) {
        return;
    }

    /*
    Delete entry for modelName from list.
    (Re)start the info timer to update message box. ... onAddAssetToWorldInfoTimeout()
    */

    addAssetToWorldInfoClear(modelName);
    _addAssetToWorldInfoTimer.start();
}

void Application::addAssetToWorldInfoTimeout() {
    if (_aboutToQuit) {
        return;
    }

    /*
    If list not empty, display last message in list (may already be displayed ) unless an error is being displayed.
    If list empty, close the message box unless an error is being displayed.
    */

    if (!_addAssetToWorldErrorTimer.isActive() && _addAssetToWorldMessageBox) {
        if (_addAssetToWorldInfoKeys.length() > 0) {
            _addAssetToWorldMessageBox->setProperty("text", "\n" + _addAssetToWorldInfoMessages.last());
        } else {
            disconnect(_addAssetToWorldMessageBox);
            _addAssetToWorldMessageBox->setVisible(false);
            _addAssetToWorldMessageBox->deleteLater();
            _addAssetToWorldMessageBox = nullptr;
        }
    }
}

void Application::addAssetToWorldError(QString modelName, QString errorText) {
    // Says what went wrong in an alert of its own, which stays until it is answered.

    if (_aboutToQuit) {
        return;
    }

    addAssetToWorldInfoClear(modelName);
    OffscreenUi::asyncWarning("Couldn't add " + modelName + " to the world", errorText);
}

void Application::addAssetToWorldErrorTimeout() {
    if (_aboutToQuit) {
        return;
    }

    /*
    If list is not empty, display message from last entry.
    If list is empty, close the message box.
    */

    if (_addAssetToWorldMessageBox) {
        if (_addAssetToWorldInfoKeys.length() > 0) {
            _addAssetToWorldMessageBox->setProperty("text", "\n" + _addAssetToWorldInfoMessages.last());
        } else {
            disconnect(_addAssetToWorldMessageBox);
            _addAssetToWorldMessageBox->setVisible(false);
            _addAssetToWorldMessageBox->deleteLater();
            _addAssetToWorldMessageBox = nullptr;
        }
    }
}


void Application::addAssetToWorldMessageClose() {
    // Clear messages, e.g., if Interface is being closed or domain changes.

    /*
    Call if user manually closes message box.
    Call if domain changes.
    Call if application is shutting down.

    Stop timers.
    Close the message box if open.
    Clear lists.
    */

    _addAssetToWorldInfoTimer.stop();
    _addAssetToWorldErrorTimer.stop();

    if (_addAssetToWorldMessageBox) {
        disconnect(_addAssetToWorldMessageBox);
        _addAssetToWorldMessageBox->setVisible(false);
        _addAssetToWorldMessageBox->deleteLater();
        _addAssetToWorldMessageBox = nullptr;
    }

    _addAssetToWorldInfoKeys.clear();
    _addAssetToWorldInfoMessages.clear();
}

void Application::onAssetToWorldMessageBoxClosed() {
    if (_addAssetToWorldMessageBox) {
        // User manually closed message box; perhaps because it has become stuck, so reset all messages.
        qInfo(interfaceapp) << "User manually closed download status message box";
        disconnect(_addAssetToWorldMessageBox);
        _addAssetToWorldMessageBox = nullptr;
        addAssetToWorldMessageClose();
    }
}


void Application::handleUnzip(QString zipFile, QStringList unzipFile, bool autoAdd, bool isZip, bool isBlocks) {
    if (autoAdd) {
        if (!unzipFile.isEmpty()) {
            for (int i = 0; i < unzipFile.length(); i++) {
                if (QFileInfo(unzipFile.at(i)).isFile()) {
                    qCDebug(interfaceapp) << "Preparing file for asset server: " << unzipFile.at(i);
                    addAssetToWorld(unzipFile.at(i), zipFile, isZip, isBlocks);
                }
            }
        } else {
            addAssetToWorldUnzipFailure(zipFile);
        }
    } else {
        showAssetServerWidget(unzipFile.first());
    }
}

// The URL dispatch and the entities' export, import and paste, after the import steps above: the extensions
// ImportExtensions.h declares and the table of importers by extension first, then the members in Application.cpp's order.
const QString SNAPSHOT_EXTENSION = ".jpg";
const QString JPG_EXTENSION = ".jpg";
const QString PNG_EXTENSION = ".png";
const QString SVO_EXTENSION = ".svo";
const QString SVO_JSON_EXTENSION = ".svo.json";
const QString JSON_EXTENSION = ".json";
const QString JS_EXTENSION = ".js";
const QString FST_EXTENSION = ".fst";
const QString FBX_EXTENSION = ".fbx";
const QString OBJ_EXTENSION = ".obj";
const QString AVA_JSON_EXTENSION = ".ava.json";
static const QString WEB_VIEW_TAG = "noDownload=true";
const QString ZIP_EXTENSION = ".zip";

const std::vector<std::pair<QString, Application::AcceptURLMethod>> Application::_acceptedExtensions {
    { SVO_EXTENSION, &Application::importSVOFromURL },
    { SVO_JSON_EXTENSION, &Application::importSVOFromURL },
    { AVA_JSON_EXTENSION, &Application::askToWearAvatarAttachmentUrl },
    { JSON_EXTENSION, &Application::importJSONFromURL },
    { JS_EXTENSION, &Application::askToLoadScript },
    { FST_EXTENSION, &Application::askToSetAvatarUrl },
    { ZIP_EXTENSION, &Application::importFromZIP },
    { JPG_EXTENSION, &Application::importImage },
    { PNG_EXTENSION, &Application::importImage }
};

bool Application::importJSONFromURL(const QString& urlString) {
    // we only load files that terminate in just .json (not .svo.json and not .ava.json)
    QUrl jsonURL { urlString };

    emit svoImportRequested(urlString);
    return true;
}

bool Application::importSVOFromURL(const QString& urlString) {
    emit svoImportRequested(urlString);
    return true;
}

bool Application::importFromZIP(const QString& filePath) {
    qDebug() << "A zip file has been dropped in: " << filePath;
    QUrl empty;
    qApp->getFileDownloadInterface()->runUnzip(filePath, empty, true, true, false);
    return true;
}

bool Application::importImage(const QString& urlString) {
    qCDebug(interfaceapp) << "An image file has been dropped in";
    QString filepath(urlString);
    filepath.remove("file:///");
    addAssetToWorld(filepath, "", false, false);
    return true;
}

// This is currently not used, but could be invoked if the user wants to go to the place embedded in an
// Interface-taken snapshot. (It was developed for drag and drop, before we had asset-server loading or in-world browsers.)
bool Application::acceptSnapshot(const QString& urlString) {
    QUrl url(urlString);
    QString snapshotPath = url.toLocalFile();

    SnapshotMetaData* snapshotData = DependencyManager::get<Snapshot>()->parseSnapshotData(snapshotPath);
    if (snapshotData) {
        if (!snapshotData->getURL().toString().isEmpty()) {
            DependencyManager::get<AddressManager>()->handleLookupString(snapshotData->getURL().toString());
        }
    } else {
        OffscreenUi::asyncWarning("", "No location details were found in the file\n" +
                             snapshotPath + "\nTry dragging in a Darkride Snap saved on this computer.");
    }
    return true;
}

bool Application::exportEntities(const QString& filename,
                                 const QVector<QUuid>& entityIDs,
                                 const glm::vec3* givenOffset) {
    QHash<EntityItemID, EntityItemPointer> entities;

    auto nodeList = DependencyManager::get<NodeList>();
    const QUuid myAvatarID = nodeList->getSessionUUID();

    auto entityTree = getEntities()->getTree();
    auto exportTree = std::make_shared<EntityTree>();
    exportTree->setMyAvatar(getMyAvatar());
    exportTree->createRootElement();
    glm::vec3 root(TREE_SCALE, TREE_SCALE, TREE_SCALE);
    bool success = true;
    entityTree->withReadLock([entityIDs, entityTree, givenOffset, myAvatarID, &root, &entities, &success, &exportTree] {
        for (auto entityID : entityIDs) { // Gather entities and properties.
            auto entityItem = entityTree->findEntityByEntityItemID(entityID);
            if (!entityItem) {
                qCWarning(interfaceapp) << "Skipping export of" << entityID << "that is not in scene.";
                continue;
            }

            if (!givenOffset) {
                EntityItemID parentID = entityItem->getParentID();
                bool parentIsAvatar = (parentID == AVATAR_SELF_ID || parentID == myAvatarID);
                if (!parentIsAvatar && (parentID.isInvalidID() ||
                                        !entityIDs.contains(parentID) ||
                                        !entityTree->findEntityByEntityItemID(parentID))) {
                    // If parent wasn't selected, we want absolute position, which isn't in properties.
                    auto position = entityItem->getWorldPosition();
                    root.x = glm::min(root.x, position.x);
                    root.y = glm::min(root.y, position.y);
                    root.z = glm::min(root.z, position.z);
                }
            }
            entities[entityID] = entityItem;
        }

        if (entities.size() == 0) {
            success = false;
            return;
        }

        if (givenOffset) {
            root = *givenOffset;
        }
        for (EntityItemPointer& entityDatum : entities) {
            auto properties = entityDatum->getProperties();
            EntityItemID parentID = properties.getParentID();
            bool parentIsAvatar = (parentID == AVATAR_SELF_ID || parentID == myAvatarID);
            if (parentIsAvatar) {
                properties.setParentID(AVATAR_SELF_ID);
            } else {
                if (parentID.isInvalidID()) {
                    properties.setPosition(properties.getPosition() - root);
                } else if (!entities.contains(parentID)) {
                    entityDatum->globalizeProperties(properties, "Parent %3 of %2 %1 is not selected for export.", -root);
                } // else valid parent -- don't offset
            }
            exportTree->addEntity(entityDatum->getEntityItemID(), properties);
        }
    });
    if (success) {
        success = exportTree->writeToJSONFile(filename.toLocal8Bit().constData());

        // restore the main window's active state
        _window->activateWindow();
    }
    return success;
}

bool Application::exportEntities(const QString& filename, float x, float y, float z, float scale) {
    glm::vec3 center(x, y, z);
    glm::vec3 minCorner = center - vec3(scale);
    float cubeSize = scale * 2;
    AACube boundingCube(minCorner, cubeSize);
    QVector<QUuid> entities;
    auto entityTree = getEntities()->getTree();
    entityTree->withReadLock([&] {
        entityTree->evalEntitiesInCube(boundingCube, PickFilter(), entities);
    });
    return exportEntities(filename, entities, &center);
}

bool Application::importEntities(const QString& urlOrFilename, const bool isObservable, const qint64 callerId) {
    bool success = false;
    _entityClipboard->withWriteLock([&] {
        _entityClipboard->eraseAllOctreeElements();

        // FIXME: readFromURL() can take over the main event loop which may cause problems, especially if downloading the JSON 
        // from the Web.
        success = _entityClipboard->readFromURL(urlOrFilename, isObservable, callerId, true);
        if (success) {
            _entityClipboard->reaverageOctreeElements();
        }
    });
    return success;
}

QVector<EntityItemID> Application::pasteEntities(const QString& entityHostType, float x, float y, float z) {
    return _entityClipboard->sendEntities(&_entityEditSender, getEntities()->getTree(), entityHostType, x, y, z);
}

bool Application::canAcceptURL(const QString& urlString) const {
    QUrl url(urlString);
    if (url.query().contains(WEB_VIEW_TAG)) {
        return false;
    } else if (urlString.startsWith(URL_SCHEME_HIFI)) {
        return true;
    }
    QString lowerPath = url.path().toLower();
    for (auto& pair : _acceptedExtensions) {
        if (lowerPath.endsWith(pair.first, Qt::CaseInsensitive)) {
            return true;
        }
    }
    return false;
}

bool Application::acceptURL(const QString& urlString, bool defaultUpload) {
    QUrl url(urlString);

    if (url.scheme() == URL_SCHEME_HIFI) {
        // this is a hifi URL - have the AddressManager handle it
        QMetaObject::invokeMethod(DependencyManager::get<AddressManager>().data(), "handleLookupString",
                                  Qt::AutoConnection, Q_ARG(const QString&, urlString));
        return true;
    }

    QString lowerPath = url.path().toLower();
    for (auto& pair : _acceptedExtensions) {
        if (lowerPath.endsWith(pair.first, Qt::CaseInsensitive)) {
            AcceptURLMethod method = pair.second;
            return (this->*method)(urlString);
        }
    }

    
    return defaultUpload;
}
