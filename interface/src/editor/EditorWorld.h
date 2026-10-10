//
//  EditorWorld.h
//  interface/src/editor
//
//  What the current domain is to the metaverse: a world instance (which is read-only until
//  edit mode is on, and whose entities Save writes back to the world) or a world's own domain.
//  The same calls the Explore window's instance bar makes.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#ifndef hifi_EditorWorld_h
#define hifi_EditorWorld_h

#include <QtCore/QJsonObject>
#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QStringList>

class QNetworkReply;

class EditorWorld : public QObject {
    Q_OBJECT
public:
    explicit EditorWorld(QObject* parent = nullptr);

    // asks the metaverse about the domain the client is in now
    void refresh();

    bool isInstance() const { return !_instanceId.isEmpty(); }
    bool canEdit() const { return _canEdit; }         // the user may edit this world
    bool isEditing() const { return _editing; }       // edit mode is on in this instance, with the user
    bool isBusy() const { return _busy; }             // a request is in flight
    QString label() const { return _label; }
    QString editingBy() const { return _editingBy; }  // someone editing in another instance
    QStringList editors() const { return _editors; }

    void setEditing(bool on);
    void save();

    Q_INVOKABLE void onCurrentReply(QNetworkReply* reply);
    Q_INVOKABLE void onCurrentError(QNetworkReply* reply);
    Q_INVOKABLE void onEditReply(QNetworkReply* reply);
    Q_INVOKABLE void onEditError(QNetworkReply* reply);
    Q_INVOKABLE void onSaveReply(QNetworkReply* reply);
    Q_INVOKABLE void onSaveError(QNetworkReply* reply);

signals:
    void changed();
    void message(const QString& text);

private:
    void apply(const QJsonObject& current);
    void clear();
    QString errorText(QNetworkReply* reply) const;

    QString _domainId;
    QString _instanceId;
    QString _label;
    QString _editingBy;
    QStringList _editors;
    bool _canEdit { false };
    bool _editing { false };
    bool _busy { false };
};

#endif // hifi_EditorWorld_h
