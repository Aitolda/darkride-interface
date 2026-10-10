//
//  EditorWorld.cpp
//  interface/src/editor
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include "EditorWorld.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtNetwork/QNetworkReply>

#include <AccountManager.h>
#include <NodeList.h>

#include "InterfaceLogging.h"

EditorWorld::EditorWorld(QObject* parent) : QObject(parent) {
}

void EditorWorld::clear() {
    _instanceId.clear();
    _label.clear();
    _editingBy.clear();
    _editors.clear();
    _canEdit = false;
    _editing = false;
}

void EditorWorld::refresh() {
    QUuid domain = DependencyManager::get<NodeList>()->getDomainHandler().getUUID();
    _domainId = domain.isNull() ? QString() : domain.toString(QUuid::WithoutBraces);
    if (_domainId.isEmpty()) {
        clear();
        emit changed();
        return;
    }
    JSONCallbackParameters callback(this, "onCurrentReply", "onCurrentError");
    DependencyManager::get<AccountManager>()->sendRequest("/api/instances/by-domain/" + _domainId,
        AccountManagerAuth::Optional, QNetworkAccessManager::GetOperation, callback);
}

void EditorWorld::apply(const QJsonObject& current) {
    QJsonObject world = current.value("world").toObject();
    QJsonObject instance = current.value("instance").toObject();
    _label = world.value("label").toString();
    _canEdit = world.value("instances").toObject().value("canEdit").toBool();
    _editingBy = current.value("editingBy").toString();
    _instanceId = instance.value("id").toString();
    _editors.clear();
    for (const auto& editor : instance.value("editors").toArray()) {
        _editors << editor.toString();
    }
    QString me = DependencyManager::get<AccountManager>()->getAccountInfo().getUsername();
    _editing = instance.value("editing").toBool() && _editors.contains(me, Qt::CaseInsensitive);
}

QString EditorWorld::errorText(QNetworkReply* reply) const {
    QString detail = QJsonDocument::fromJson(reply->readAll()).object().value("message").toString();
    return detail.isEmpty() ? reply->errorString() : detail;
}

void EditorWorld::onCurrentReply(QNetworkReply* reply) {
    QJsonObject current = QJsonDocument::fromJson(reply->readAll()).object();
    // the client may have moved on while the answer was on its way
    if (!reply->url().path().endsWith(_domainId)) {
        return;
    }
    apply(current);
    emit changed();
}

void EditorWorld::onCurrentError(QNetworkReply* reply) {
    // 404: not a world with instances (or not one the user may see); nothing to offer
    qCDebug(interfaceapp) << "editor world lookup:" << reply->url().path() << reply->errorString();
    clear();
    emit changed();
}

void EditorWorld::setEditing(bool on) {
    if (_instanceId.isEmpty() || _busy) {
        return;
    }
    _busy = true;
    emit changed();
    QJsonObject body;
    body["on"] = on;
    JSONCallbackParameters callback(this, "onEditReply", "onEditError");
    DependencyManager::get<AccountManager>()->sendRequest("/api/instances/" + _instanceId + "/edit",
        AccountManagerAuth::Required, QNetworkAccessManager::PostOperation, callback,
        QJsonDocument(body).toJson(QJsonDocument::Compact));
}

void EditorWorld::onEditReply(QNetworkReply* reply) {
    _busy = false;
    apply(QJsonDocument::fromJson(reply->readAll()).object());
    emit changed();
    emit message(QString());   // the highlighted button says it
}

void EditorWorld::onEditError(QNetworkReply* reply) {
    _busy = false;
    emit changed();
    emit message("World Edit: " + errorText(reply));
}

void EditorWorld::save() {
    if (_instanceId.isEmpty() || _busy) {
        return;
    }
    _busy = true;
    emit changed();
    emit message("Saving…");
    JSONCallbackParameters callback(this, "onSaveReply", "onSaveError");
    DependencyManager::get<AccountManager>()->sendRequest("/api/instances/" + _instanceId + "/save",
        AccountManagerAuth::Required, QNetworkAccessManager::PostOperation, callback, QByteArray("{}"));
}

void EditorWorld::onSaveReply(QNetworkReply* reply) {
    _busy = false;
    QJsonObject result = QJsonDocument::fromJson(reply->readAll()).object();
    emit changed();
    emit message(QString("Saved (%1 entities)").arg(result.value("entities").toInt()));
}

void EditorWorld::onSaveError(QNetworkReply* reply) {
    _busy = false;
    emit changed();
    emit message("Save failed: " + errorText(reply));
}
