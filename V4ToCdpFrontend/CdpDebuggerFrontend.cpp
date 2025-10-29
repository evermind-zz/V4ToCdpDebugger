#include "CdpDebuggerFrontend.h"

#include <QHttpServer>
#include <QHttpServerRequest>
#include <QHttpServerResponse>
#include <QHttpServerRouter>
#include <QHttpServerWebSocketUpgradeResponse>
#include <QWebSocket>
#include <QTcpServer>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QHostAddress>
#include <QDebug>
#include <QList>
#include <QVariant>
#include <QDebug>
#include <QStringList>

// DEBUG_LOGGING_ENABLED via CMake needs to be enabled to spill out stuff. See debug_out.h for more
#include "debug_out.h"
#include "dump_variant.h"

#include "V4CdpMapper.h"
#include "V4CdpHelper.h"
#include "V4Helpers.h"

// some helper functions
static std::string variantMapToJsonString(const QVariantMap& map, bool compact = true) {
    QJsonObject obj = QJsonObject::fromVariantMap(map);
    QJsonDocument doc(obj);

    // Create a QString, either compact or indented
    QString jsonString = compact ? doc.toJson(QJsonDocument::Compact)
                                 : doc.toJson(QJsonDocument::Indented);

    return jsonString.toStdString();
}

// CdpDebuggerFrontend implementation
CdpDebuggerFrontend::CdpDebuggerFrontend(
    BackendSyncCall getHandledByBackend,
    const QString frontendName,
    QObject* parent)
    : QObject(parent),
      m_getHandledByBackend(std::move(getHandledByBackend)),
      m_frontendName(frontendName),
      m_httpServer(nullptr)
{
}

CdpDebuggerFrontend::~CdpDebuggerFrontend()
{
    if (m_httpServer) {
        m_httpServer->deleteLater();
    }
}

void CdpDebuggerFrontend::startServer(quint16 port)
{
    if (m_httpServer)
        return;

    m_httpServer = new QHttpServer(this);

    connect(m_httpServer, &QHttpServer::newWebSocketConnection,
        this, &CdpDebuggerFrontend::onNewWebSocketConnection);
        //TODO disconnect on destruction


    auto *tcpServer = new QTcpServer(this);
    if (!tcpServer->listen(QHostAddress::LocalHost, port)) {
        qWarning() << "HTTP server could not listen on port" << port;
        return;
    }

    if (!m_httpServer->bind(tcpServer)) {
        qWarning() << "HTTP server failed to bind to tcpServer";
        return;
    }

    qInfo() << "HTTP server listening on"
            << tcpServer->serverAddress().toString()
            << ":" << tcpServer->serverPort();

    setupHttpRoutes();

    DEBUG_LOG << "XXX CDP HTTP/WS server listening on port" << port;
    DEBUG_LOG << "XXX CDP Debugger Frontend ready - use " << QString("http://localhost:%1/json/list").arg(port) << " to connect";
}

void CdpDebuggerFrontend::onNewWebSocketConnection()
{
    while (m_httpServer->hasPendingWebSocketConnections()) {
        std::unique_ptr<QWebSocket> sock = m_httpServer->nextPendingWebSocketConnection();
        if (!sock) continue;
        QWebSocket* client = sock.release(); // we take over now -> websockets are managed by us
        if (client == nullptr) {
            qWarning() << "Failed to get pending WebSocket connection";
        }

        m_responseClients.append(client);

        connect(client, &QWebSocket::textMessageReceived,
                this, [this, client](const QString &msg){ onCdpMessageReceived(msg, client); });
        connect(client, &QWebSocket::disconnected,
                this, [this, client](){
                    onCdpDisconnected(client);
                });

        sendInitialEvents(client);
    }
}

void CdpDebuggerFrontend::setupHttpRoutes()
{
    quint16 port = m_httpServer->serverPorts().isEmpty()
	? 0
	: m_httpServer->serverPorts().first();

    m_httpServer->addWebSocketUpgradeVerifier(
        m_httpServer,
        [this](const QHttpServerRequest &req) -> QHttpServerWebSocketUpgradeResponse {
            if (req.url().path() == QString("/devtools/page/%1-js").arg(m_frontendName.toLower())
             || req.url().path() == QString("/devtools/browser/%1-js").arg(m_frontendName.toLower())) {
                DEBUG_LOG << "Accepted WebSocket upgrade request to" << req.url().path();
                return QHttpServerWebSocketUpgradeResponse::accept();
             } else {
                DEBUG_LOG << "Rejected WebSocket upgrade request to" << req.url().path();
                // in case of failure to the next verifier -- if there is one
                return QHttpServerWebSocketUpgradeResponse::passToNext();
             }
        }
    );

    m_httpServer->route("/json/version", QHttpServerRequest::Method::Get,
        [this, port](const QHttpServerRequest &request) {
            DEBUG_LOG << "HTTP GET /json/version from" << request.remoteAddress().toString();
            QJsonObject version {
                {"Browser", QString("%1-CDP/1.0").arg(m_frontendName)},
                {"Protocol-Version", "1.3"},
                {"User-Agent", QString("%1 JavaScript Debugger").arg(m_frontendName)},
                {"V8-Version", "9.4.0"},
                {"webSocketDebuggerUrl", QString("ws://localhost:%1/devtools/browser/%2-js").arg(port).arg(m_frontendName.toLower())}
            };

            return QHttpServerResponse(version);
        });

    m_httpServer->route("/json/list", QHttpServerRequest::Method::Get,
        [this, port](const QHttpServerRequest &request) {
            // DEBUG_LOG << "HTTP GET /json/list from" << request.remoteAddress().toString();
            QJsonArray targets;
            QJsonObject target {{
                {"id", QString("%1-js").arg(m_frontendName.toLower())},
                {"title", QString("%1 JS Debugger").arg(m_frontendName)},
                {"type", "page"},
                {"description", QString("%1 V4 JavaScript Execution Context").arg(m_frontendName)},
                {"url", QString("%1://javascript").arg(m_frontendName.toLower())},
                {"devtoolsFrontendUrl", QString("/devtools/inspector.html?ws=localhost:%1/devtools/page/%2-js").arg(port).arg( m_frontendName.toLower())},
                {"webSocketDebuggerUrl", QString("ws://localhost:%1/devtools/page/%2-js").arg(port).arg(m_frontendName.toLower())}
            }};

            targets.append(target);

            return QHttpServerResponse(targets);
    });

    m_httpServer->route("/json/protocol", QHttpServerRequest::Method::Get,
        [](const QHttpServerRequest &) {
            QJsonObject domain {
                {"domain", "Debugger"},
                {"version", "1.3"},
                {"commands", QJsonArray{
                    QJsonObject{{"name", "enable"}},
                    QJsonObject{{"name", "disable"}},
                    QJsonObject{{"name", "setBreakpointByUrl"}},
                    QJsonObject{{"name", "removeBreakpoint"}},
                    QJsonObject{{"name", "resume"}},
                    QJsonObject{{"name", "stepOver"}},
                    QJsonObject{{"name", "stepInto"}},
                    QJsonObject{{"name", "getScriptSource"}},
                    QJsonObject{{"name", "evaluateOnCallFrame"}}
                }},
                {"events", QJsonArray{
                    QJsonObject{{"name", "paused"}},
                    QJsonObject{{"name", "resumed"}},
                    QJsonObject{{"name", "scriptParsed"}}
                }}
            };
            QJsonArray schema {domain};
            return QHttpServerResponse(schema);
    });

    m_httpServer->addAfterRequestHandler(this,
        [](const QHttpServerRequest &req, QHttpServerResponse &resp) {
            Q_UNUSED(req);
            auto h = resp.headers();
            h.append("Access-Control-Allow-Origin", "*");
            h.append("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
            h.append("Access-Control-Allow-Headers", "Content-Type");
            resp.setHeaders(std::move(h));
    });
}

void CdpDebuggerFrontend::onCdpMessageReceived(const QString& message, QWebSocket* client)
{
    if (!client) {
        return;
    }

    DEBUG_LOG << "XXX --> CDP Received message:" << message;
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(message.toUtf8(), &parseError);

    if (parseError.error != QJsonParseError::NoError) {
        qWarning() << "Failed to parse CDP message:" << parseError.errorString();
        return;
    }

    if (!doc.isObject()) {
        qWarning() << "CDP message is not a JSON object";
        return;
    }

    QJsonObject cmd = doc.object();

    if (cmd.contains("id")) {
        int id = cmd["id"].toInt(-1);
        if (id == -1) {
            qWarning() << "Invalid ID in CDP message";
            return;
        }

        QString method = cmd["method"].toString();
        DEBUG_LOG << "Processing CDP command:" << method << " with id:" << id;
        // Immediate responses (no backend)
        if (method == "Runtime.enable") {
            QJsonObject response{
                {"id", id},
                {"result", QJsonObject()}
            };
            sendToClient(client, QJsonDocument(response));
            return;
        } else if (method == "Debugger.enable") {
            QJsonObject response{
                {"id", id},
                {"result", QJsonObject{{"debuggerId", QString("%1-debugger-1").arg(m_frontendName.toLower())}}}
            };
            sendToClient(client, QJsonDocument(response));

            createAndSentScriptParsedEvents(client);
            return;
        } else if (method == "Debugger.disable") {
            QJsonObject response{
                {"id", id},
                {"result", QJsonObject()}
            };
            sendToClient(client, QJsonDocument(response));
            return;
        }

        // Map to V4 Command (type, attributes)
        QVariantMap cdpReq = cmd.toVariantMap();
        QVariantMap v4Map = V4CdpMapper::mapCdpToV4Request(cdpReq);
        if (v4Map.contains(MAPPER_PASSTHROUGH) && v4Map.value(MAPPER_PASSTHROUGH).toBool()) {
            QVariantMap cdpResponse = V4CdpMapper::mapV4ToCdpResponse(v4Map);
            sendToClient(client, QJsonDocument::fromVariant(cdpResponse));
        }

        else if (!v4Map.isEmpty()) {
            QVariant v4Request = QVariant::fromValue(v4Map);
            wrapperSendRequestToBackend(v4Request);
            DEBUG_LOG << "Forwarded CDP command to backend:" << method;
        } else {
            qWarning() << "Failed to map CDP command to V4:" << method;
            QJsonObject errorResp{
                {"id", id},
                {"error", QJsonObject{
                    {"code", -32601},
                    {"message", "Method not found"}
                }}
            };
            sendToClient(client, QJsonDocument(errorResp));
        }
    }
    // TODO: Handle client-sent events (rare)
}

void CdpDebuggerFrontend::wrapperSendRequestToBackend(const QVariant& request)
{
    DEBUG_LOG << "XXX <-- V4 sending request to backend:" << dumpVariant(request, 2);
    emit sendRequestToBackend(request);
}

void CdpDebuggerFrontend::onCdpDisconnected(QWebSocket *client)
{
    if (!client)
        return;

    DEBUG_LOG << "CDP client disconnected";
    // Mark for async deletion
    client->deleteLater();

    // Remove all now-invalid pointers
    m_responseClients.removeIf([](const QPointer<QWebSocket> &ptr) {
        return ptr.isNull();
    });
}


void CdpDebuggerFrontend::onBackendResponse(const QVariant& response)
{
    DEBUG_LOG << "XXX --> V4 received response from backend:" << dumpVariant(response, 2);
    if (!response.canConvert<QVariantMap>()) {
        qWarning() << "Backend response is not a QVariantMap";
        return;
    }

    QVariantMap v4Response = response.toMap();
    int id = v4Response.value("ID", -1).toInt();  // V4 Backend uses "ID" (uint)

    if (id == -1) { // assuming we are in event notification mode
        if (v4Response.contains("Event")) {
            if (autoReplyForSomeEvents(v4Response))
                return;
            QVariantMap cdpEvent = V4CdpMapper::mapV4EventToCdp(v4Response, m_getHandledByBackend);
            DEBUG_LOG << "XXX Result of V4CdpMapper::mapV4EventToCdp " << dumpVariant(cdpEvent);
            if (!cdpEvent.isEmpty()) {
                DEBUG_LOG << "XXX clients are like going crazy: " << m_responseClients.size();
                for (QPointer<QWebSocket> &client : m_responseClients) {
                    if (!client) {
                        DEBUG_LOG << "XXX invalid client entry";
                        continue;
                    }
                    sendToClient(client, QJsonDocument::fromVariant(cdpEvent));
                }

            } else {
                qWarning() << "Failed to map V4 event to CDP";
            }
        }
        else {
            qWarning() << "Backend response missing ID";
        }

        return;
    }

    // Map V4 to CDP non-event messages
    QVariantMap cdpResp = V4CdpMapper::mapV4ToCdpResponse(v4Response);

    for (QPointer<QWebSocket> &client : m_responseClients) {
        if (!client) {
            DEBUG_LOG << "XXX invalid client entry";
            continue;
        }
        sendToClient(client, QJsonDocument::fromVariant(cdpResp));
    }

    DEBUG_LOG << "Sent backend response to client for ID:" << id;
}

bool CdpDebuggerFrontend::autoReplyForSomeEvents(QVariantMap &v4Resp)
{
    if (V4Helpers::getNestedValue(v4Resp, {"Event", "type"}).toString() == "InlineEvalFinished" &&
        V4Helpers::getNestedValue(v4Resp, {"Event", "attributes", "message"}).toString() == "undefined")
    {
        // automatically resume if no clients are connected (avoid paused state)
        QVariantMap v4Req;
        v4Req["Command"] = QVariantMap{
            {"type", "Resume"},
            {"attributes", QVariantMap{}}};
        blockingV4BackendCall(v4Req);
        DEBUG_LOG << "XXX --> V4 Event: auto handled event: " << dumpVariant(v4Resp, 2) << " with here generated answer: " << dumpVariant(v4Req, 2);
    }
    else
    {
        DEBUG_LOG << "XXX --> V4 Event: NOT auto handled unknown event: " << dumpVariant(v4Resp, 2);
        return false;
    }

    return true;
}

void CdpDebuggerFrontend::sendInitialEvents(QWebSocket* client)
{
    if (!client || client->state() != QAbstractSocket::ConnectedState)
        return;

    QJsonObject contextCreatedEvent{
        {"method", "Runtime.executionContextCreated"},
        {"params", QJsonObject{
            {"context", QJsonObject{
                {"id", 1},
                {"origin", QString("%1://javascript").arg(m_frontendName.toLower())},
                {"name", QString("%1 JavaScript Context").arg(m_frontendName)},
                {"uniqueId", QString("%1-js-context").arg(m_frontendName.toLower())},
            }}
        }}
    };
    sendToClient(client, QJsonDocument(contextCreatedEvent));

    createAndSentScriptParsedEvents(client);
    DEBUG_LOG << "XXX Sent initial CDP events to client based on backend data where needed.";
}

void CdpDebuggerFrontend::createAndSentScriptParsedEvents(QWebSocket* client)
{
    // fetch scripts via existing mapper function
    QVariantMap v4Req = V4CdpMapper::v4Request_scripts(V4CdpMapper::V4OnlyCommands::GetScripts, 0);
    QVariant v4Resp = blockingV4BackendCall(v4Req);
    QVariantList scripts = v4Resp.toMap().value("Result").toMap().value("result").toList();

    int contextId = 1; // seems ok

    for (const QVariant& scriptVar : scripts) {
        QVariantMap s = scriptVar.toMap();
        QJsonObject scriptParsedEvent = V4CdpHelper::cdpScriptParsedEventBuilder(s, contextId, m_frontendName.toLower());
        sendToClient(client, QJsonDocument(scriptParsedEvent));
    }
}

QVariant CdpDebuggerFrontend::blockingV4BackendCall(QVariantMap& request) {
    DEBUG_LOG << "<-- Wrapping request for backend call:" << variantMapToJsonString(request, true);
    // Add a dummy ID for this direct call
    request["ID"] = 0;
    QVariant response = m_getHandledByBackend(request);
    DEBUG_LOG << "--> Wrapping response for backend call:" << variantMapToJsonString(response.toMap(), true);
    return response;
}

void CdpDebuggerFrontend::sendToClient(QWebSocket* client, const QJsonDocument& doc)
{
    if (client && client->state() == QAbstractSocket::ConnectedState) {
        QString message = doc.toJson(QJsonDocument::Compact);
        client->sendTextMessage(message);
        DEBUG_LOG << "XXX <-- CDP Sent to client:" << message;
    } else {
        qWarning() << "Cannot send to client - not connected";
    }
}

void CdpDebuggerFrontend::onV4EventAvailable(const int noOfPendingEvents)
{
    DEBUG_LOG << "XXX V4 new event available, pending events:" << noOfPendingEvents;
    int remainingEvents = noOfPendingEvents;
    // fetching all events from backend
    while (remainingEvents-- > 0) {
        wrapperSendRequestToBackend(QVariantMap {{"Control", "PullEvent"}});
        // the backend will than send the events and they will than be processed
        // by the frontend via onBackendResponse()
    }
}
