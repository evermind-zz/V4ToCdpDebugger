#pragma once

#include <QCryptographicHash>
#include <QJsonObject>
#include <QString>

class V4CdpHelper {
    public:
        static QJsonObject cdpScriptParsedEventBuilder(const QVariantMap &s, int contextId, QString frontendName)
        {
            const QString contents = s.value("contents").toString();
            const QString hash = QString(QCryptographicHash::hash(
                contents.toUtf8(), QCryptographicHash::Sha256).toHex());

            int endLine = contents.count('\n');

            return QJsonObject{
                {"method", "Debugger.scriptParsed"},
                {"params", QJsonObject{
                    {"scriptId", QString::number(s.value("id", 1).toInt())},
                    {"url", QString("%1://%2").arg(frontendName).arg(s.value("fileName", "main.js").toString())},
                    {"startLine", 0},
                    {"startColumn", 0},
                    {"endLine", endLine},
                    {"endColumn", 0},
                    {"executionContextId", contextId},
                    {"hash", hash}
                }}
            };
        }
};
