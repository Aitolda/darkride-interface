//
//  JsonOrder.h
//  interface/src/editor
//
//  The order of a JSON object's entries, which QJsonObject loses (it keeps its keys sorted): read from the text an
//  entity holds, kept while entries are added, renamed and removed, and written back, so that User Data lists its
//  entries in the order they were made, as Blender's custom properties do, and keeps that order when it is saved.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#ifndef hifi_JsonOrder_h
#define hifi_JsonOrder_h

#include <QtCore/QHash>
#include <QtCore/QJsonObject>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QVariantList>

namespace JsonOrder {
    // a place in the JSON, the keys of objects (QString) and positions in arrays (int) from the top, as one key
    QString placeKey(const QVariantList& path);

    // the keys of every object in the text, in the text's order, by the object's place; nothing for text that is
    // not JSON
    using KeyOrder = QHash<QString, QStringList>;
    KeyOrder scan(const QString& text);

    // the object's keys in the kept order, with any it has gained since after them, as QJsonObject sorts them
    QStringList keysInOrder(const QJsonObject& object, const KeyOrder& order, const QVariantList& path);
    // an entry has been renamed: the order kept for what is inside it follows it
    void move(KeyOrder& order, const QVariantList& from, const QVariantList& to);

    // the object as JSON, compact as entities hold it or indented to read, its entries in the kept order
    QString write(const QJsonObject& object, const KeyOrder& order, bool indented = false);
}

#endif // hifi_JsonOrder_h
