#include "V4Helpers.h"

QVariant V4Helpers::getNestedValue(
    const QVariantMap &map,
    std::initializer_list<QString> path,
    const QVariant &defaultValue)
{
    QVariant current = map; // no address, just copy it

    for (const QString &key : path) {
        if (!current.canConvert<QVariantMap>())
            return defaultValue;

        const QVariantMap inner = current.toMap();
        if (!inner.contains(key))
            return defaultValue;

        current = inner.value(key);
    }

    return current;
}
