#include <QCoreApplication>
#include <QCommandLineParser>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QWebSocket>
#include <QTimer>
#include <QUrl>
#include <QDebug>
#include <QLoggingCategory>
#include <QRegularExpression>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QThread>
#include <QProcess>
#include <QDateTime>
#include <QFileInfo>
#include "raw_settings.h"

#define IGNORE "IGNORE"
// example: cdp_tests.txt
/*
   [TestCaseOne]
   request={"id":52,"method":"Debugger.setBreakpointByUrl","params":{"lineNumber":2,"url":"jsrunner://test.js","columnNumber":0,"condition":""}}
   response={"id":52,"result":{"breakpointId":"2"}}

   [TestCaseTwo]
   request={"id":55,"method":"Debugger.setBreakpointByUrl","params":{"lineNumber":6,"url":"jsrunner://test.js","columnNumber":0,"condition":""}}
   response=IGNORE

   [TestCaseThree]
   response={"method":"Debugger.paused","params":{"callFrames":[],"hitBreakpoints":["1"],"reason":"other"}}
 */

class CDPTestClient : public QObject {
    Q_OBJECT
    public:
        explicit CDPTestClient(const QString &testFile, QObject *parent = nullptr)
            : QObject(parent), m_testFile(testFile), m_currentIndex(-1) {
                connect(&m_webSocket, &QWebSocket::connected, this, &CDPTestClient::onConnected);
                connect(&m_webSocket, &QWebSocket::textMessageReceived, this, &CDPTestClient::onMessage);
                connect(&m_webSocket, &QWebSocket::errorOccurred, this, &CDPTestClient::onError);
                connect(&m_responseTimer, &QTimer::timeout, this, &CDPTestClient::onResponseTimeout);
                connect(&m_webSocket, &QWebSocket::disconnected, this, &CDPTestClient::onDisconnected);
                m_responseTimer.setSingleShot(true);

                if (!loadTestCases()) {
                    QCoreApplication::quit();
                }
            }

        void start(const QUrl &httpUrl) {
            qInfo() << "Connecting to HTTP endpoint:" << httpUrl;
            QNetworkRequest request(httpUrl);
            m_httpReply = m_nam.get(request);
            connect(m_httpReply, &QNetworkReply::finished, this, &CDPTestClient::onHttpFinished);
        }

        private slots:
            void onResponseTimeout() {
                if (m_pendingResponseId != -1) {
                    qWarning() << "[TEST" << m_currentIndex << "] Timeout waiting for response ID" << m_pendingResponseId;
                    m_pendingResponseId = -1;
                    runNextTest(); // next test despite timeout
                }
            }

        void onHttpFinished() {
            if (m_httpReply->error() != QNetworkReply::NoError) {
                qCritical() << "HTTP error:" << m_httpReply->errorString();
                QCoreApplication::quit();
                return;
            }

            QJsonDocument doc = QJsonDocument::fromJson(m_httpReply->readAll());
            if (!doc.isArray()) {
                qCritical() << "Expected JSON array from /json";
                QCoreApplication::quit();
                return;
            }

            // Find the first WebSocket debugger URL
            for (const auto &v : doc.array()) {
                QJsonObject obj = v.toObject();
                if (obj["type"].toString() == "page" || obj["type"].toString() == "node") {
                    QString wsUrl = obj["webSocketDebuggerUrl"].toString();
                    if (!wsUrl.isEmpty()) {
                        qInfo() << "Switching to WebSocket:" << wsUrl;
                        m_webSocket.open(QUrl(wsUrl));
                        return;
                    }
                }
            }
            qCritical() << "No WebSocket URL found";
            QCoreApplication::quit();
        }

        void onDisconnected() {
            //qDebug() << "WebSocket disconnected unexpectedly.";
            QCoreApplication::quit();
        }
        void onConnected() {
            qInfo() << "WebSocket connected. Loading test cases...";
            runNextTest();
        }

        QString prettyJsonWithQuotes(const QJsonDocument &doc) {
            return QString("\"%1\"").arg(doc.toJson(QJsonDocument::Compact));
        }

        void onMessage(const QString &message) {
            QString time = QDateTime::currentDateTime().toString("hh:mm:ss.zzz");
            //qDebug() << "onMessage() incomming [" << time << "]" << message << " the m_currentIndex" << m_currentIndex;
            //qDebug() << "WebSocket state:" << m_webSocket.state();

            m_responseTimer.stop();
            QJsonDocument doc = QJsonDocument::fromJson(message.toUtf8());
            if (!doc.isObject()) return;

            if (m_currentIndex >= m_tests.size()) {
                QCoreApplication::quit();
                return;
            }

            QJsonObject obj = doc.object();
            int id = obj["id"].toInt(-1);

            if (m_pendingResponseId == id) {
                // await response
                if (m_currentTestCase.response.isEmpty() || m_currentTestCase.response == IGNORE) {
                    qInfo() << "[TEST" << m_currentIndex << "] Response ignored or not expected.";
                } else {
                    QJsonDocument expected = QJsonDocument::fromJson(m_currentTestCase.response.toUtf8());
                    if (expected == doc) {
                        qInfo() << "[TEST" << m_currentIndex << "] PASS";
                    } else {
                        qWarning().noquote() << "[TEST" << m_currentIndex << "] FAIL - Expected: \"" << expected.toJson(QJsonDocument::Compact) << "\""
                            << "Got:" << doc.toJson(QJsonDocument::Compact);
                        QJsonDocument requestWas = QJsonDocument::fromJson(m_currentTestCase.request.toUtf8());
                        qWarning().noquote() << "[TEST" << m_currentIndex << "] FAIL - Request was: " << prettyJsonWithQuotes(requestWas);
                    }
                }
                m_pendingResponseId = -1;
                runNextTest();
            } else if (id == -1) {
                // Event (no id)
                if (!m_currentTestCase.response.isEmpty() && m_currentTestCase.response != IGNORE) {
                    QJsonDocument expected = QJsonDocument::fromJson(m_currentTestCase.response.toUtf8());
                    if (expected == doc) {
                        qInfo() << "[TEST" << m_currentIndex << "] EVENT MATCH";
                        m_pendingResponseId = -1;
                        runNextTest();
                    }
                }
            }
        }

        void onError(QAbstractSocket::SocketError error) {
            qCritical() << "WebSocket error:" << m_webSocket.errorString() << "State:" << m_webSocket.state();
            QCoreApplication::quit();
        }

    private:
        bool loadTestCases() {
            QFileInfo fileInfo(m_testFile);
            if (!fileInfo.exists()) {
                qCritical() << "Warning: Configuration file" << m_testFile << "does not exist.";
                return false;
            }

            RawSettings settings(m_testFile);

            for (const QString& testCaseName : settings.orderedGroups()) {
                settings.beginGroup(testCaseName);
                QString request = settings.value("request", "").toString();
                QString response = settings.value("response", IGNORE).toString();

                //qDebug().noquote()
                //    << "testName:" << testCaseName
                //    << "| request =" << request
                //    << "| response =" << response;


                settings.endGroup();
                if (request.isEmpty() && response == "IGNORE") continue;
                m_tests.append({testCaseName, request, response});
            }

            if (m_tests.isEmpty()) {
                qCritical() << "No valid test cases found";
                return false;
            }
            return true;
        }

        void runNextTest() {
            m_responseTimer.stop();
            ++m_currentIndex;
            if (m_currentIndex >= m_tests.size()) {
                qInfo() << "All tests completed.";
                QCoreApplication::quit();
                return;
            }

            const auto &test = m_tests[m_currentIndex];
            qInfo() << ""; // separate the tests
            qInfo() << "[TEST" << m_currentIndex << "] << Test:" << test.name;

            m_currentTestCase = test;
            if (!test.request.isEmpty()) {
                QJsonDocument doc = QJsonDocument::fromJson(test.request.toUtf8());
                if (doc.isObject()) {
                    QJsonObject obj = doc.object();
                    m_pendingResponseId = obj["id"].toInt();
                    m_webSocket.sendTextMessage(doc.toJson(QJsonDocument::Compact));
                    m_responseTimer.start(RESPONSE_TIMEOUT_MS);
                }
            } else {
                qInfo() << "Waiting for event";
                //m_responseTimer.start(RESPONSE_TIMEOUT_MS);
            }
        }

    private:
        struct TestCase {
            QString name;
            QString request;  // JSON string or empty
            QString response; // JSON string or IGNORE
        };

        QWebSocket m_webSocket;
        QNetworkAccessManager m_nam;
        QNetworkReply *m_httpReply = nullptr;
        QList<TestCase> m_tests;
        int m_currentIndex;
        int m_pendingResponseId = -1;
        TestCase m_currentTestCase;
        QString m_testFile;

        QTimer m_responseTimer;
        static constexpr int RESPONSE_TIMEOUT_MS = 5000;
};

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    QLoggingCategory::setFilterRules("qt.network.ssl=false");

    QProcess externalProc(&app);

    QCommandLineParser parser;
    parser.setApplicationDescription("CDP Test Client");
    parser.addHelpOption();
    parser.addOption(QCommandLineOption({"t", "test-cases"}, "Path to test cases file.", "file"));
    QCommandLineOption delayOpt({"d", "delay"}, "Delay in ms before starting tests.", "ms", "500");
    QCommandLineOption cmdOpt({"e", "external-command"}, "External command to start CDP server.", "command");
    QCommandLineOption externalLogfileOpt({"l", "logfile"}, "Logfile for external command (default log.txt).", "file");
    parser.addOption(delayOpt);
    parser.addOption(cmdOpt);
    parser.addOption(externalLogfileOpt);
    parser.addPositionalArgument("url", "CDP HTTP endpoint, e.g. http://localhost:9222");
    parser.process(app);

    const auto args = parser.positionalArguments();
    if (args.isEmpty() || !parser.isSet("test-cases")) {
        parser.showHelp(1);
    }

    QString externalCmd = parser.value(cmdOpt);
    if (!externalCmd.isEmpty()) {
        QString externalLogfile = parser.isSet(externalLogfileOpt) ? parser.value(externalLogfileOpt) : "log.txt";
        externalProc.setStandardOutputFile(externalLogfile);
        externalProc.setStandardErrorFile(externalLogfile);

        QStringList cmdArgs = QProcess::splitCommand(externalCmd);
        QString program = cmdArgs.takeFirst(); // the binary is there

        qInfo() << "Starting external command:" << externalCmd;
        externalProc.start(program, cmdArgs);
        QThread::msleep(500); // wait for external command to startup
    }

    QUrl url(args[0]);
    if (!url.isValid() || url.scheme() != "http") {
        qCritical() << "Invalid URL. Use http://host:port";
        return 1;
    }

    int delay = parser.value(delayOpt).toInt(0);
    if (delay > 0) {
        qInfo() << "Delay test for:" << delay << "ms";
        QThread::msleep(delay);
    }

    url.setPath("/json/list");

    CDPTestClient client(parser.value("test-cases"));
    client.start(url);

    return app.exec();
}

#include "cdp_test_client.moc"
