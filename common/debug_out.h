#pragma once
#include <QDebug>
#include <QDateTime>
#include <QThread>
#include <QFileInfo>
#include <QFile>
#include <QMutex>
#include <QTextStream>

// =======================================================
// configuration
// =======================================================
// if enabled, time/Thread/ClassInfo will be shown
#ifndef DO_DEBUG_SHOW_CONTEXT
#define DO_DEBUG_SHOW_CONTEXT 1
#endif

// if enabled, logs starting with "XXX" will be written to a logfile
#ifndef DO_DEBUG_LOG_TO_FILE
#define DO_DEBUG_LOG_TO_FILE 1
#endif

// name of the logfile
#ifndef DO_DEBUG_LOG_FILENAME
#define DO_DEBUG_LOG_FILENAME "debug_output.log"
#endif

// =======================================================
// Release-Build Dummy
// =======================================================
#ifndef DEBUG_LOGGING_ENABLED
class NullDebug {
    public:
        template<typename T> NullDebug &operator<<(const T &) { return *this; }
};
static NullDebug nullDebug;
#define DEBUG_LOG nullDebug
#define WARN_LOG qWarning()

#else
// =======================================================
// Debug build: extended logger
// =======================================================

// helper function: Extract class names from Q_FUNC_INFO
inline QString extractClassName(const char *funcInfo)
{
    QString info(funcInfo);
    int scopeIndex = info.lastIndexOf("::");
    if (scopeIndex > 0) {
        QString prefix = info.left(scopeIndex);
        int classIndex = prefix.lastIndexOf(' ');
        return prefix.mid(classIndex + 1);
    }
    return QStringLiteral("<global>");
}

// =======================================================
// Thread-safe log file handler (lazy init, truncate once)
// =======================================================
struct LogFileHandler {
    QFile file;
    QTextStream stream;
    QMutex mutex;
    bool initialized = false;

    void init()
    {
#if DO_DEBUG_LOG_TO_FILE
                file.setFileName(DO_DEBUG_LOG_FILENAME);
                file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text);
                stream.setDevice(&file);
                initialized = true;
#endif
    }

    void writeLine(const QString &line)
    {
#if DO_DEBUG_LOG_TO_FILE
        QMutexLocker locker(&mutex);
        if (!initialized)
            init();
        stream << line << '\n';
        stream.flush();
#endif
    }
};

inline LogFileHandler &getLogHandler()
{
    static LogFileHandler handler;
    return handler;
}

// =======================================================
// Smarter QDebug Wrapper with File Output
// =======================================================
class DebugProxy {
public:
    explicit DebugProxy(const char *file, int line, const char *func)
        : m_file(file), m_line(line), m_func(func),
          m_dbg(qDebug().noquote().nospace())
    {
#if DO_DEBUG_SHOW_CONTEXT
        m_logPrefix = makeLogPrefix(file, line, func);
        m_dbg << m_logPrefix;
#endif
    }

QString makeLogPrefix(const char* file,
                      int line,
                      const char* func)
{
        QString time = QDateTime::currentDateTime().toString("hh:mm:ss.zzz");
        QString thread = QString::number(reinterpret_cast<quintptr>(QThread::currentThreadId()), 16);
        QString className = extractClassName(func);

    QString prefix;
    QTextStream(&prefix)
        << "[" << time << "]"
        << "[" << thread << "]"
        << "[" << className << "::" << func
        << " @ " << QFileInfo(file).fileName() << ":" << line << "] ";

    return prefix;
}

    ~DebugProxy()
    {
#if DO_DEBUG_LOG_TO_FILE
        // prüfe, ob Log mit "XXX" beginnt
        QString msg = m_buffer.trimmed();
        if (msg.startsWith("XXX")) {
            QString line = QStringLiteral("%1 %2")
                               .arg(m_logPrefix, msg);
            getLogHandler().writeLine(line);
        }
#endif
    }

    template<typename T>
    DebugProxy &operator<<(const T &value)
    {
        appendToBuffer(value);
        m_dbg << value;
        return *this;
    }

private:
    QString m_buffer;
    QString m_logPrefix;
    const char *m_file;
    int m_line;
    const char *m_func;
    QDebug m_dbg;

    // helper function: Special handling for different types
    void appendToBuffer(const QString &v) { m_buffer.append(v); }
    void appendToBuffer(const QLatin1String &v) { m_buffer.append(v); }
    void appendToBuffer(const QByteArray &v) { m_buffer.append(QString::fromUtf8(v)); }
    void appendToBuffer(const char *v) { m_buffer.append(QString::fromUtf8(v)); }
    void appendToBuffer(const std::string &v) { m_buffer.append(QString::fromStdString(v)); }

    template<typename T>
    void appendToBuffer(const T &v)
    {
        QTextStream ts(&m_buffer);
        ts << v;
    }
};

// =======================================================
// Macros
// =======================================================
#define DEBUG_LOG DebugProxy(__FILE__, __LINE__, Q_FUNC_INFO)
#define WARN_LOG  DEBUG_LOG

#endif /* DEBUG_LOGGING_ENABLED */
