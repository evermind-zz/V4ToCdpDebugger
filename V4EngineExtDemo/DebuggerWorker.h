#pragma once

#include <QObject>
#include <QDebug>
#include <QString>

class CV4EngineExt;
class CV4ScriptDebuggerBackend;
class CdpDebuggerFrontend;

/**
 * connect engine with backend and qt::connect frontend with backend
 */
class DebuggerWorker : public QObject
{
    Q_OBJECT
    public:
        explicit DebuggerWorker(CV4EngineExt* engine, const QString frontendName, QObject* parent = nullptr);

    public slots:
        void startDebugger();

    private:
        CV4EngineExt* m_engine;
        QString m_frontendName;
        CV4ScriptDebuggerBackend* m_backend = nullptr;
        CdpDebuggerFrontend* m_frontend = nullptr;
};
