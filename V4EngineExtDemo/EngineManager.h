#pragma once

#include <QObject>
#include <QJSValue>
#include <QString>

class CV4EngineExt;
class QString;
class DebuggerWorker;
class QThread;

/**
 * create the CV4EngineExt, threads and the DebuggerWorker
 */
class EngineManager: public QObject
{
    Q_OBJECT

    public:
        EngineManager();
        ~EngineManager();

        CV4EngineExt& getEngine() { return *scriptEngine; }

    private:
        CV4EngineExt* scriptEngine;
        QThread* debuggerThread;
        DebuggerWorker* worker;
};
