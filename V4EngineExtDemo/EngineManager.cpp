#include <QDebug>
#include <QThread>

#include "V4EngineExt.h"
#include "DebuggerWorker.h"
#include "EngineManager.h"

EngineManager::EngineManager()
{
    scriptEngine = new CV4EngineExt();

    // thread for the debugger
    debuggerThread = new QThread();
    worker = new DebuggerWorker(scriptEngine, "JsRunner");
    worker->moveToThread(debuggerThread);

    QObject::connect(debuggerThread, &QThread::started, worker, &DebuggerWorker::startDebugger);
    debuggerThread->start();
}

EngineManager::~EngineManager()
{
    if (scriptEngine)
    {
        scriptEngine->evaluateScript("","");
        debuggerThread->quit();
        debuggerThread->wait();
        delete worker;

        delete scriptEngine;
        scriptEngine = nullptr;
    }
}
