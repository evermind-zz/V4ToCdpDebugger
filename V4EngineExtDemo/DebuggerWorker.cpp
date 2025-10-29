#include "DebuggerWorker.h"

#include "V4EngineExt.h"
#include "V4ScriptDebuggerBackend.h"
#include "CdpDebuggerFrontend.h"

#include <QThread>
#include <QMetaObject>
#include <QVariant>

DebuggerWorker::DebuggerWorker(CV4EngineExt* engine, const QString frontendName, QObject* parent)
    : QObject(parent),
    m_engine(engine),
    m_frontendName(frontendName)
{
}

void DebuggerWorker::startDebugger()
{
    m_backend = new CV4ScriptDebuggerBackend(this);
    m_backend->attachTo(m_engine);

    BackendSyncCall backendCall = [this](const QVariant& request) -> QVariant {
        QVariant response;

        if (QThread::currentThread() == m_backend->thread()) {
            // call directly if in same thread
            response = m_backend->handleRequest(request);
        } else {
            // Synchronized across threads
            QMetaObject::invokeMethod(m_backend, "handleRequest",
                    Qt::BlockingQueuedConnection,
                    Q_RETURN_ARG(QVariant, response),
                    Q_ARG(QVariant, request));
        }

        return response;
    };

    m_frontend = new CdpDebuggerFrontend(backendCall, m_frontendName, this);
    m_frontend->startServer();

    connect(m_frontend, &CdpDebuggerFrontend::sendRequestToBackend, m_backend, &CV4ScriptDebuggerBackend::processRequest);
    connect(m_backend, &CV4ScriptDebuggerBackend::sendResponse, m_frontend, &CdpDebuggerFrontend::onBackendResponse);
    connect(m_backend, &CV4ScriptDebuggerBackend::newV4EventAvailable, m_frontend, &CdpDebuggerFrontend::onV4EventAvailable);

    qDebug() << "Debugger with CDP Adapter started";
}
