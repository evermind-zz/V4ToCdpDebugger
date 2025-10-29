#include "raw_settings.h"

#include <QFileInfo>
#include <QDebug>
#include <QTextStream>

namespace {
    static QString absoluteFilePath(const QString fileName) {
        QFileInfo fileInfo(fileName);
        if (!fileInfo.exists()) {
            qDebug() << "Warning: Configuration file" << fileName << "does not exist.";
        }
        return fileInfo.absoluteFilePath();
    }
}

// as we need to feed registerFormat() static method we need some kind of static
// helper to map the static methods to non static class methods
bool RawSettings::getInstanceData(QIODevice& device, RawSettings::InstanceData*& data, QString& fileName) {
    if (auto* file = qobject_cast<QFile*>(&device)) {
        fileName = file->fileName();
    } else {
        qDebug() << "Error: Unable to determine file name.";
        return false;
    }

    QMutexLocker locker(&RawSettings::mutex_);
    data = &RawSettings::instanceData_[fileName];
    if (!data->instance) {
        qDebug() << "Error: No RawSettings instance found for file" << fileName;
        return false;
    }
    return true;
}

RawSettings::RawSettings(const QString& fileName)
    : QSettings(absoluteFilePath(fileName), createFormat(fileName)) {
}


QSettings::Format RawSettings::createFormat(const QString &fileName) {
    QMutexLocker locker(&mutex_);
    instanceData_[absoluteFilePath(fileName)].instance = this;

    return registerFormat("rawini", readCustomWrapper, writeCustomWrapper, Qt::CaseSensitive);
}

RawSettings::~RawSettings() {
    QMutexLocker locker(&mutex_);
    instanceData_.remove(fileName());
}

bool RawSettings::fileExists() const {
    return QFileInfo(fileName()).exists();
}

QStringList RawSettings::orderedGroups() const {
    QMutexLocker locker(&mutex_);
    return instanceData_.value(absoluteFilePath(fileName())).groupOrder;
}

void RawSettings::parseAndDebug() {
    QStringList groups = orderedGroups();
    if (groups.isEmpty() && !fileExists()) {
        qDebug() << "No groups found, possibly because the file does not exist.";
        return;
    }

    for (const QString& group : groups) {
        beginGroup(group);
        QString request = value("request", "<none>").toString();
        QString response = value("response", "<none>").toString();

        qDebug().noquote()
            << "Block:" << group
            << "| request =" << request
            << "| response =" << response;

        endGroup();
    }
}

bool RawSettings::readCustomNonStatic(QIODevice& device, QSettings::SettingsMap& map, QStringList& groupOrder) {
    QTextStream in(&device);
    QString currentGroup;
    groupOrder.clear(); // reset order for this instance

    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        if (line.isEmpty() || line.startsWith('#') || line.startsWith(';')) {
            continue;
        }

        if (line.startsWith('[') && line.endsWith(']')) {
            currentGroup = line.mid(1, line.length() - 2).trimmed();
            if (!currentGroup.isEmpty() && !groupOrder.contains(currentGroup)) {
                groupOrder.append(currentGroup); // store order eg. if you want to iterate over the groups and want to have the same order as in the file
            }
        } else {
            int eq = line.indexOf('=');
            if (eq > 0) {
                QString key = line.left(eq).trimmed();
                QString value = line.mid(eq + 1); // no trimming

                QString fullKey = currentGroup.isEmpty() ? key : currentGroup + '/' + key;
                map.insert(fullKey, value);
            }
        }
    }
    return true;
}

bool RawSettings::writeCustomNonStatic(QIODevice& device, const QSettings::SettingsMap& map, const QStringList& groupOrder) {
    QTextStream out(&device);
    QString lastGroup;

    // Write groups in the stored order
    for (const QString& group : groupOrder) {
        if (!lastGroup.isEmpty()) {
            out << "\n";
        }
        out << "[" << group << "]\n";
        lastGroup = group;

        // Find all keys for the current group
        QString prefix = group + '/';
        for (auto it = map.constBegin(); it != map.constEnd(); ++it) {
            if (it.key().startsWith(prefix)) {
                QString key = it.key().mid(prefix.length());
                out << key << " = " << it.value().toString() << "\n";
            }
        }
    }

    // Write keys without a group (if any)
    for (auto it = map.constBegin(); it != map.constEnd(); ++it) {
        if (!it.key().contains('/')) {
            if (!lastGroup.isEmpty()) {
                out << "\n";
            }
            out << it.key() << " = " << it.value().toString() << "\n";
            lastGroup = QString();
        }
    }
    return true;
}

bool RawSettings::readCustomWrapper(QIODevice& device, QSettings::SettingsMap& map) {
    QString fileName;
    InstanceData* data;
    if (!getInstanceData(device, data, fileName)) {
        return false;
    }
    return data->instance->readCustomNonStatic(device, map, data->groupOrder);
}

bool RawSettings::writeCustomWrapper(QIODevice& device, const QSettings::SettingsMap& map) {
    QString fileName;
    InstanceData* data;
    if (!getInstanceData(device, data, fileName)) {
        return false;
    }
    return data->instance->writeCustomNonStatic(device, map, data->groupOrder);
}
