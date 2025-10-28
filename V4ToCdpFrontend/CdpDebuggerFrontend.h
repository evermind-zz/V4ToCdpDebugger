#pragma once

#include <QObject>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QVariant>
#include <QString>
#include <QPointer>

// Forward Declarations
class QHttpServer;
class QWebSocket;
class QTcpSocket;
template <typename Value> class QList;

using BackendSyncCall = std::function<QVariant(const QVariant&)>;

class CdpDebuggerFrontend : public QObject
{
    Q_OBJECT

    public:
        explicit CdpDebuggerFrontend(BackendSyncCall getHandledByBackend, const QString frontendName, QObject* parent = nullptr);
        ~CdpDebuggerFrontend();

        void startServer(quint16 port = 9222);

    signals:
        void sendRequestToBackend(const QVariant& request);

    public slots:
        void onBackendResponse(const QVariant& response);
        void onV4EventAvailable(const int noOfPendingEvents);

    private slots:
        void onCdpMessageReceived(const QString& message, QWebSocket* client);
        void onCdpDisconnected(QWebSocket* client);

    private:
        void onNewWebSocketConnection();
        void handleWebSocketConnection(QWebSocket* client);
        void setupHttpRoutes();
        void sendInitialEvents(QWebSocket* client);
        QVariantMap mapCdpToV4(const QJsonObject& cdpCmd);
        QJsonObject mapV4ToCdp(const QVariantMap& v4Resp);
        void sendToClient(QWebSocket* client, const QJsonDocument& doc);
        void wrapperSendRequestToBackend(const QVariant& request);
        void createAndSentScriptParsedEvents(QWebSocket *client);

        QVariant blockingV4BackendCall(QVariantMap& request);

        BackendSyncCall m_getHandledByBackend;
        const QString m_frontendName;
        QHttpServer* m_httpServer;
        QList<QPointer<QWebSocket>> m_responseClients;
        QVariantMap debuggerGlobals;
        bool autoReplyForSomeEvents(QVariantMap &v4Resp);
};
