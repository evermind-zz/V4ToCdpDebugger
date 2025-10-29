#include <QCoreApplication>
#include <QCommandLineParser>
#include <QFile>
#include <QJSEngine>
#include <QJSValue>
#include <QVariantList>
#include <QTimer>
#include <QDebug>

#include "EngineManager.h"
#include "V4EngineExt.h"

// Host object for JS bindings
class Host : public QObject {
    Q_OBJECT
    public:
        explicit Host(QObject *parent = nullptr) : QObject(parent) {}

        Q_INVOKABLE void log(const QJSValue &args) {
            QString msg;
            if (args.isArray()) {
                int len = args.property("length").toInt();
                for (int i = 0; i < len; ++i) {
                    if (i > 0) msg += " ";
                    msg += args.property(i).toString();
                }
            } else {
                msg = args.toString();
            }
            qInfo().noquote() << "[host.log]" << msg;
        }

        // For cppPrint in demo
        Q_INVOKABLE void print(const QString &msg) {
            qInfo().noquote() << msg;
        }
};

// Read file content as QString
QString readFile(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();
    return QString::fromUtf8(file.readAll());
}

class ScriptRunner : public QObject {
    Q_OBJECT
    public:
        ScriptRunner(CV4EngineExt &engine, const QString &scriptPath, int count, int interval, QObject *parent = nullptr)
            : QObject(parent), m_engine(engine), m_count(count), m_interval(interval) {
                QString script = readFile(scriptPath);
                if (script.isEmpty()) {
                    qCritical() << "Error: Could not read script:" << scriptPath;
                    QCoreApplication::exit(1);
                    return;
                }

                QJSValue result = m_engine.evaluateScript(script, scriptPath);
                if (result.isError()) {
                    qCritical().noquote() << "JS Error:" << result.toString();
                    QCoreApplication::exit(1);
                    return;
                }

                m_func = m_engine.globalObject().property("myTester");
                if (!m_func.isCallable()) {
                    qWarning() << "Function myTester() not found. Exiting.";
                    QCoreApplication::exit(0);
                    return;
                }

                m_counter = 0;
                m_timer = new QTimer(this);
                connect(m_timer, &QTimer::timeout, this, &ScriptRunner::invoke);
                m_timer->start(m_interval);
            }

    private slots:
            void invoke() {
                if (m_counter >= m_count) {
                    qInfo() << "Done.";
                    QCoreApplication::quit();
                    return;
                }

                qInfo() << "Call" << (m_counter + 1) << "of" << m_count;
                QJSValue res = m_func.call(QJSValueList() << m_counter << m_interval);
                if (res.isError()) {
                    qCritical().noquote() << "Error in myTester:" << res.toString();
                    QCoreApplication::quit();
                    return;
                }
                ++m_counter;
            }

    private:
        CV4EngineExt &m_engine;
        QJSValue m_func;
        QTimer *m_timer;
        int m_counter = 0;
        int m_count;
        int m_interval;
};

int main(int argc, char *argv[])
{
    qputenv("QT_LOGGING_RULES", "qt.network.ssl=false"); // do not spam log on ssl not available
    QCoreApplication app(argc, argv);
    QCommandLineParser parser;
    parser.setApplicationDescription("Minimal QJSEngine Script Runner");
    parser.addHelpOption();

    QCommandLineOption inputOpt({"i", "input"}, "Path to a JavaScript file.", "file");
    QCommandLineOption countOpt({"c", "count"}, "How many times to call the function.", "n", "1");
    QCommandLineOption intervalOpt({"t", "interval"}, "Interval in milliseconds between calls.", "ms", "1000");

    parser.addOption(inputOpt);
    parser.addOption(countOpt);
    parser.addOption(intervalOpt);
    parser.process(app);

    EngineManager manager;
    CV4EngineExt &engine = manager.getEngine();
    Host hostObj;

    // Expose host.log()
    QJSValue host = engine.newQObject(&hostObj);
    engine.globalObject().setProperty("host", host);

    QString inputPath = parser.value(inputOpt);
    int count = parser.value(countOpt).toInt();
    int interval = parser.value(intervalOpt).toInt();

    if (!inputPath.isEmpty()) {
        new ScriptRunner(engine, inputPath, count, interval, &app);
        return app.exec();
    }

    // No script given -> Default demo
    qInfo() << "No --input given, running demo.";

    QJSValue result = engine.evaluateScript("1 + 2 * 3", "");
    qDebug() << "JS Result:" << result.toNumber();

    // Expose cppPrint via same host
    engine.globalObject().setProperty("cppPrint", host.property("print"));

    engine.evaluateScript(R"(
        cppPrint("Hello from JavaScript!");
        var square = function(x) { return x * x; };
        cppPrint("Square(5) = " + square(5));
    )", "");

    QJSValue jsFunc = engine.globalObject().property("square");
    if (jsFunc.isCallable()) {
        QJSValue res = jsFunc.call(QJSValueList() << 9);
        qDebug() << "Square(9) =" << res.toNumber();
    }

    return 0;
}

#include "main.moc"
