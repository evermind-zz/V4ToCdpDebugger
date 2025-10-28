#pragma once
#include <QVariant>
#include <QString>

class V4Helpers {
    public:
        static QVariant getNestedValue(
                const QVariantMap &map,
                std::initializer_list<QString> path,
                const QVariant &defaultValue = QVariant()
                );
};
