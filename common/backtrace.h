#pragma once
#include <execinfo.h>
#include <QDebug>
#include <QStringList>

QStringList generateBacktrace(int maxFrames = 32) {
    void *callstack[32];
    int frames = backtrace(callstack, maxFrames);
    char **symbols = backtrace_symbols(callstack, frames);
    QStringList trace;

    for (int i = 0; i < frames; ++i) {
        trace << QString::fromLocal8Bit(symbols[i]);
    }

    free(symbols);
    return trace;
}

// Beispielverwendung:
void dumpStackTrace() {
    qDebug().noquote() << "=== BACKTRACE ===";
    for (const QString &line : generateBacktrace()) {
        qDebug().noquote() << line;
    }
    qDebug().noquote() << "=================";
}
