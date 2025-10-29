#pragma once

#include <QVariant>
#include <QVariantMap>
#include <QString>

static QString dumpVariant(const QVariant &v, int indent = 2)
{
    const QString pad(indent, ' ');

    switch (v.typeId()) {
    case QMetaType::QVariantMap: {
        QStringList out;
        const QVariantMap map = v.toMap();
        for (auto it = map.begin(); it != map.end(); ++it) {
            out << QString("%1%2: %3")
                       .arg(pad)
                       .arg(it.key())
                       .arg(dumpVariant(it.value(), indent + 2));
        }
        return "{\n" + out.join(",\n") + "\n" + pad + "}";
    }
    case QMetaType::QVariantList: {
        QStringList out;
        const QVariantList list = v.toList();
        for (const QVariant &elem : list) {
            out << dumpVariant(elem, indent + 2);
        }
        return "[\n" + pad + out.join(",\n" + pad) + "\n" + pad + "]";
    }
    case QMetaType::QObjectStar: {
        QObject *obj = v.value<QObject*>();
        return obj ? QString("QObject(%1, class=%2)")
                         .arg(obj->objectName(), obj->metaObject()->className())
                   : "QObject(nullptr)";
    }
    default:
        return v.toString();
    }
}
