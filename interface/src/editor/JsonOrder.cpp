//
//  JsonOrder.cpp
//  interface/src/editor
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include "JsonOrder.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonValue>

namespace {
    const QChar PLACE_SEPARATOR(0x1f);
    const int INDENT = 4;

    bool isIndex(const QVariant& step) {
        return step.userType() == QMetaType::Int;
    }

    // Walks the text as QJsonDocument does, taking down the keys of each object in the order they come. It stops
    // quietly at anything that is not JSON: the text was parsed before it gets here, so that is not expected.
    class Scanner {
    public:
        explicit Scanner(const QString& text) : _text(text) {}

        JsonOrder::KeyOrder run() {
            QVariantList path;
            value(path);
            return _order;
        }

    private:
        void space() {
            while (_at < _text.size() && _text[_at].isSpace()) {
                ++_at;
            }
        }

        // a string, unescaped as QJsonDocument unescapes it, so that keys match the object's
        bool string(QString& out) {
            if (_at >= _text.size() || _text[_at] != QLatin1Char('"')) {
                return false;
            }
            ++_at;
            while (_at < _text.size()) {
                QChar c = _text[_at++];
                if (c == QLatin1Char('"')) {
                    return true;
                }
                if (c != QLatin1Char('\\') || _at >= _text.size()) {
                    out += c;
                    continue;
                }
                QChar escaped = _text[_at++];
                switch (escaped.unicode()) {
                    case 'b': out += QChar('\b'); break;
                    case 'f': out += QChar('\f'); break;
                    case 'n': out += QChar('\n'); break;
                    case 'r': out += QChar('\r'); break;
                    case 't': out += QChar('\t'); break;
                    case 'u': {
                        bool ok = false;
                        ushort code = _text.mid(_at, 4).toUShort(&ok, 16);
                        if (ok) {
                            out += QChar(code);
                            _at += 4;
                        }
                        break;
                    }
                    default: out += escaped; break;
                }
            }
            return false;
        }

        void value(QVariantList& path) {
            space();
            if (_at >= _text.size()) {
                return;
            }
            QChar c = _text[_at];
            if (c == QLatin1Char('{')) {
                object(path);
            } else if (c == QLatin1Char('[')) {
                array(path);
            } else if (c == QLatin1Char('"')) {
                QString text;
                string(text);
            } else {
                // a number, true, false or null: up to what ends it
                while (_at < _text.size() && !_text[_at].isSpace() && !QStringLiteral(",]}").contains(_text[_at])) {
                    ++_at;
                }
            }
        }

        void object(QVariantList& path) {
            ++_at;   // {
            QString place = JsonOrder::placeKey(path);
            while (true) {
                space();
                if (_at >= _text.size()) {
                    return;
                }
                if (_text[_at] == QLatin1Char('}')) {
                    ++_at;
                    return;
                }
                if (_text[_at] == QLatin1Char(',')) {
                    ++_at;
                    continue;
                }
                QString key;
                if (!string(key)) {
                    return;
                }
                QStringList& keys = _order[place];
                if (!keys.contains(key)) {   // a key given twice is one entry
                    keys << key;
                }
                space();
                if (_at < _text.size() && _text[_at] == QLatin1Char(':')) {
                    ++_at;
                }
                path << key;
                value(path);
                path.removeLast();
            }
        }

        void array(QVariantList& path) {
            ++_at;   // [
            int index = 0;
            while (true) {
                space();
                if (_at >= _text.size()) {
                    return;
                }
                if (_text[_at] == QLatin1Char(']')) {
                    ++_at;
                    return;
                }
                if (_text[_at] == QLatin1Char(',')) {
                    ++_at;
                    continue;
                }
                int before = _at;
                path << index++;
                value(path);
                path.removeLast();
                if (_at == before) {
                    return;   // not a value: not JSON
                }
            }
        }

        const QString& _text;
        int _at { 0 };
        JsonOrder::KeyOrder _order;
    };

    // a string, number, true, false or null as QJsonDocument writes it (its escaping and number format), taken out
    // of a one-item array
    QString scalar(const QJsonValue& value) {
        QString text = QString::fromUtf8(QJsonDocument(QJsonArray { value }).toJson(QJsonDocument::Compact)).trimmed();
        return text.mid(1, text.size() - 2);
    }

    QString spaces(int count) {
        return QString(count, QLatin1Char(' '));
    }

    // indent is the current depth's indentation, or -1 for compact
    void writeValue(QString& out, const QJsonValue& value, QVariantList& path, const JsonOrder::KeyOrder& order, int indent);

    void writeObject(QString& out, const QJsonObject& object, QVariantList& path, const JsonOrder::KeyOrder& order, int indent) {
        QStringList keys = JsonOrder::keysInOrder(object, order, path);
        if (keys.isEmpty()) {
            out += QStringLiteral("{}");
            return;
        }
        bool pretty = indent >= 0;
        out += QLatin1Char('{');
        for (int i = 0; i < keys.size(); ++i) {
            if (i > 0) {
                out += QLatin1Char(',');
            }
            if (pretty) {
                out += QLatin1Char('\n') + spaces(indent + INDENT);
            }
            out += scalar(QJsonValue(keys[i])) + (pretty ? QStringLiteral(": ") : QStringLiteral(":"));
            path << keys[i];
            writeValue(out, object.value(keys[i]), path, order, pretty ? indent + INDENT : -1);
            path.removeLast();
        }
        if (pretty) {
            out += QLatin1Char('\n') + spaces(indent);
        }
        out += QLatin1Char('}');
    }

    void writeArray(QString& out, const QJsonArray& array, QVariantList& path, const JsonOrder::KeyOrder& order, int indent) {
        if (array.isEmpty()) {
            out += QStringLiteral("[]");
            return;
        }
        bool pretty = indent >= 0;
        out += QLatin1Char('[');
        for (int i = 0; i < array.size(); ++i) {
            if (i > 0) {
                out += QLatin1Char(',');
            }
            if (pretty) {
                out += QLatin1Char('\n') + spaces(indent + INDENT);
            }
            path << i;
            writeValue(out, array[i], path, order, pretty ? indent + INDENT : -1);
            path.removeLast();
        }
        if (pretty) {
            out += QLatin1Char('\n') + spaces(indent);
        }
        out += QLatin1Char(']');
    }

    void writeValue(QString& out, const QJsonValue& value, QVariantList& path, const JsonOrder::KeyOrder& order, int indent) {
        if (value.isObject()) {
            writeObject(out, value.toObject(), path, order, indent);
        } else if (value.isArray()) {
            writeArray(out, value.toArray(), path, order, indent);
        } else {
            out += scalar(value);
        }
    }
}

QString JsonOrder::placeKey(const QVariantList& path) {
    QStringList parts;
    for (const auto& step : path) {
        parts << (isIndex(step) ? QStringLiteral("#%1").arg(step.toInt()) : step.toString());
    }
    return parts.join(PLACE_SEPARATOR);
}

JsonOrder::KeyOrder JsonOrder::scan(const QString& text) {
    return Scanner(text).run();
}

QStringList JsonOrder::keysInOrder(const QJsonObject& object, const KeyOrder& order, const QVariantList& path) {
    QStringList keys;
    for (const auto& key : order.value(placeKey(path))) {
        if (object.contains(key) && !keys.contains(key)) {
            keys << key;
        }
    }
    for (const auto& key : object.keys()) {
        if (!keys.contains(key)) {
            keys << key;
        }
    }
    return keys;
}

void JsonOrder::move(KeyOrder& order, const QVariantList& from, const QVariantList& to) {
    QString fromKey = placeKey(from);
    QString toKey = placeKey(to);
    QString fromPrefix = fromKey + PLACE_SEPARATOR;
    for (const auto& key : order.keys()) {
        if (key == fromKey || key.startsWith(fromPrefix)) {
            order.insert(toKey + key.mid(fromKey.size()), order.take(key));
        }
    }
}

QString JsonOrder::write(const QJsonObject& object, const KeyOrder& order, bool indented) {
    QString out;
    QVariantList path;
    writeObject(out, object, path, order, indented ? 0 : -1);
    return out;
}
