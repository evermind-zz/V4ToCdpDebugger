#include "V4CdpMapper.h"

#include <QVariant>
#include <QVariantMap>
#include <QVariantList>
#include <QMutexLocker>
#include <QString>
#include <QDebug>
#include <QUrl>
#include <QRegularExpression>

#include "V4Helpers.h"

#define MAPPER_METADATA "_mapper_metadata" // will only be set in a module if it can handle the request
#define INTERNAL_MAPPER "_mapper_internal" // used for no where no direct CDP equal method exists

// helper functions
static void createNoOpCdpToV4(QVariantMap& v4Request, QVariantMap& cdpRequest) {
        v4Request["ID"] = cdpRequest.value("id");
        v4Request["Command"] = QVariantMap{{"type", "NoOp"}};
        //v4Request["handled"] = true;
        v4Request[MAPPER_PASSTHROUGH] = true;
}

static QString normalizeScriptName(const QString &input)
{
       static const QRegularExpression re(
               R"(^\s*(?:.*://)?\s*([^()]+?)(?:\s*\(\d+\))?\s*$)"
       );

       QRegularExpressionMatch match = re.match(input);
       if (match.hasMatch()) {
               QString name = match.captured(1).trimmed();
               return name;
       }

       return input.trimmed();
}

static inline V4CdpMapper::V4OnlyCommands getInternalMethod(const QVariantMap &v4)
{
    return static_cast<V4CdpMapper::V4OnlyCommands>(
        v4.value(INTERNAL_MAPPER, static_cast<int>(V4CdpMapper::V4OnlyCommands::None)).toInt()
    );
}

// V4CdpMapper implementation
// Static members
QHash<int, QVariantMap> V4CdpMapper::s_origCdpRequests;
QMutex V4CdpMapper::s_mutex;

QVariantMap V4CdpMapper::mapCdpToV4Request(QVariantMap &cdpRequest)
{
    // Try module mappers in an order that makes sense; the first non-empty
    // result is considered the mapping for this CDP request.
    QVariantMap v4;

    auto tryMap = [&](auto mapper) -> bool {
        v4 = mapper(cdpRequest);
        if (cdpRequest.contains(MAPPER_METADATA)) {
            storeOrigCdpRequest(cdpRequest.value("id").toInt(), cdpRequest);
            return true;
        }
        return false;
    };

    // Versuche alle Mapper der Reihe nach
    if (tryMap(mapCdpToV4Request_debugger) ||        // Debugger (Debugger.* commands)
            tryMap(mapCdpToV4Request_runtime))       // Runtime (Runtime.* commands)
    {
        return v4;
    }

    // No mapping found — return empty (dispatcher / caller should handle fallback)
    return QVariantMap();
}

QVariantMap V4CdpMapper::mapV4ToCdpResponse(const QVariantMap &v4Response)
{
    static const QHash<QString, MapperFn> mappers = {
        {V4CdpMapper::Modules::Debugger,     mapV4ToCdpResponse_debugger},
        {V4CdpMapper::Modules::Runtime,      mapV4ToCdpResponse_runtime}
    };

    // We expect v4Response to contain "ID" (as the backend sets it).
    int id = v4Response.value("ID").toInt(); // TODO we may need to set a default value like -1
    if (id < 0) {
        qWarning() << "V4CdpMapper::mapV4ToCdpResponse: V4 response missing ID" << v4Response;
        return QVariantMap();
    }

    QVariantMap orig = takeOrigCdpRequest(id);
    if (orig.isEmpty()) {
        // We don't have the original CDP request — fallback: try modules by v4Response.type or return empty
        qWarning() << "V4CdpMapper::mapV4ToCdpResponse: original CDP request not found for ID" << id;
        // Default fallback: wrap result into a generic CDP response
        QVariantMap cdp;
        cdp["id"] = id;
        if (v4Response.contains("Result"))
            cdp["result"] = v4Response.value("Result");
        else if (v4Response.contains("Response"))
            cdp["result"] = v4Response.value("Response");
        else
            cdp["result"] = QVariantMap();
        return cdp;
    }

    // If a reponse could be handled it has MAPPER_METADATA set in its original CDP request
    const QString type = orig.value(MAPPER_METADATA).toString();
    if (mappers.contains(type)) {
        return mappers.value(type)(v4Response, orig);
    }

    // Nothing matched — return generic wrapper
    qWarning() << "V4CdpMapper::mapV4ToCdpResponse: no module matched for response ID" << id;
    QVariantMap cdp;
    cdp["id"] = id;
    cdp["result"] = v4Response.value("Result");
    return cdp;
}

// ---------------------- Request store helpers ----------------------
void V4CdpMapper::storeOrigCdpRequest(int id, const QVariantMap &cdpRequest)
{
    QMutexLocker locker(&s_mutex);
    s_origCdpRequests.insert(id, cdpRequest);
}

QVariantMap V4CdpMapper::takeOrigCdpRequest(int id)
{
    QMutexLocker locker(&s_mutex);
    if (!s_origCdpRequests.contains(id))
        return QVariantMap();
    QVariantMap r = s_origCdpRequests.take(id);
    return r;
}

// ---------------------- domain helpers  ----------------------
QVariantMap V4CdpMapper::v4Request_location(V4OnlyCommands method, int id, QString fileName, int lineNumber, int scriptId) // fileName only RunToLocation, scriptId only RunToLocationById
{
    QVariantMap v4;
    v4["ID"] = id;
    QVariantMap attrs;

    if (method == V4OnlyCommands::RunToLocation) {
        attrs["fileName"] = fileName;
        attrs["lineNumber"] = lineNumber;
        v4["Command"] = QVariantMap{{"type", "RunToLocation"}, {"attributes", attrs}};
    } else if (method == V4OnlyCommands::RunToLocationById) {
        attrs["scriptId"] = scriptId;
        attrs["lineNumber"] = lineNumber;
        v4["Command"] = QVariantMap{{"type", "RunToLocationById"}, {"attributes", attrs}};
    } else {
        // Not handled by this module
        v4.clear();
    }
    return v4;
}

QVariantMap V4CdpMapper::v4ToCdpResponse_location(const QVariantMap &v4Response, const QVariantMap &origV4Request)
{
    QVariantMap cdp;
    int id = v4Response.value("ID").toInt();
    V4OnlyCommands method = getInternalMethod(origV4Request);
    cdp["id"] = id;

    if (method == V4OnlyCommands::RunToLocation || method == V4OnlyCommands::RunToLocation) {
        // Most control commands return success/empty result
        if (v4Response.contains("Result"))
            cdp["result"] = v4Response.value("Result");
        else
            cdp["result"] = QVariantMap();
    } else {
        // Not handled by this module
        cdp.clear();
    }

    return cdp;
}

// Evaluate
QVariantMap V4CdpMapper::mapCdpToV4Request_helper_evaluate(QString &method, QVariantMap &cdpRequest)
{
    QVariantMap params = cdpRequest.value("params").toMap();
    int id = cdpRequest.value("id").toInt();

    if (method != "Runtime.evaluate" && method != "Debugger.evaluateOnCallFrame")
        return QVariantMap();

    QVariantMap v4;
    v4["ID"] = id;
    QVariantMap cmd;
    cmd["type"] = "Evaluate";
    QVariantMap attrs;

    if (method == "Runtime.evaluate") {
        attrs["program"] = params.value("expression");
    } else { // Debugger.evaluateOnCallFrame
        QString expr = params.value("expression").toString();
        if (expr == "this") {
            attrs["contextIndex"] = params.value("callFrameId").toInt();
            cmd["type"] = "GetThisObject";
            v4[INTERNAL_MAPPER] = static_cast<int>(V4OnlyCommands::GetThisObject);
        } else {
            // general evaluation -> Evaluate
            attrs["program"] = expr;
            attrs["contextIndex"] = params.value("callFrameId").toString();
        }
    }

    cmd["attributes"] = attrs;
    v4["Command"] = cmd;
    return v4;
}

// Scripts
QVariantMap V4CdpMapper::v4Request_scripts(V4OnlyCommands method, int id, int since)
{
    QVariantMap v4Request; v4Request["ID"] = id;
    QVariant &v4CommandRef = v4Request["Command"];

    if (method == V4OnlyCommands::GetScripts) {
        v4CommandRef = QVariantMap{{"type", "GetScripts"}};
    } else if (method == ScriptsCheckpoint) {
        v4CommandRef = QVariantMap{{"type", "ScriptsCheckpoint"}};
    } else if (method == V4OnlyCommands::GetScriptsDelta) {
        QVariantMap attrs;
        attrs["since"] = since;
        v4CommandRef = QVariantMap{{"type", "GetScriptsDelta"}, {"attributes", attrs}};
    } else {
        v4Request.clear();
        return v4Request;
    }

    return v4Request;
}

QVariantMap V4CdpMapper::v4ToCdpResponse_scripts(const QVariantMap &v4Response, const QVariantMap &origV4Request)
{
    QVariantMap cdp;
    int id = v4Response.value("ID").toInt();
    cdp["id"] = id;

    V4OnlyCommands method = getInternalMethod(origV4Request);

    if (method == V4OnlyCommands::GetScripts) {
        QVariantList v4Scripts = v4Response.value("Result").toList();
        QVariantList outList;
        for (const QVariant &s : v4Scripts) {
            QVariantMap sm = s.toMap();
            QVariantMap out;
            out["scriptId"] = sm.value("id");
            out["url"] = sm.value("fileName");
            out["startLine"] = sm.value("baseLineNumber");
            out["source"] = sm.value("contents");
            outList.append(out);
        }
        cdp["result"] = QVariantMap{{"scripts", outList}};
    } else if (method == V4OnlyCommands::ScriptsCheckpoint) {
        cdp["result"] = v4Response.value("Result");
    } else if (method == V4OnlyCommands::GetScriptsDelta) {
        cdp["result"] = v4Response.value("Result");
    } else {
        cdp.clear();
    }

    return cdp;
}

// Stack & Contexts
QVariantMap V4CdpMapper::v4Request_stack(V4OnlyCommands method, int id, int contextIndex) // contextIndex only for V4OnlyCommands::GetContextInfo used
{
    QVariantMap v4; v4["ID"] = id;
    v4[INTERNAL_MAPPER] = static_cast<int>(method);

    if (method == V4OnlyCommands::GetContextCount) {
        v4["Command"] = QVariantMap{{"type", "GetContextCount"}};
    } else if (method == V4OnlyCommands::GetContextInfo) {
        QVariantMap attrs; attrs["contextIndex"] = contextIndex;
        v4["Command"] = QVariantMap{{"type", "GetContextInfo"}, {"attributes", attrs}};
    } else {
        v4.clear();
        return v4;
    }

    return v4;
}

QVariantMap V4CdpMapper::v4ToCdpResponse_stack(const QVariantMap &v4Response, const QVariantMap &origV4Request)
{
    QVariantMap cdp; int id = v4Response.value("ID").toInt(); cdp["id"] = id;
    V4OnlyCommands method = getInternalMethod(origV4Request);

    if (method == V4OnlyCommands::GetContextInfo) { // map to CDP Runtime.CallFrame object
        QVariantMap r = v4Response.value("Result").toMap();
        QVariantMap frame;
        frame["functionName"] = r.value("functionName");
        frame["url"] = r.value("fileName");
        frame["lineNumber"] = r.value("lineNumber");
        cdp["result"] = QVariantMap{{"callFrames", QVariantList{frame}}};
    } else {
        cdp.clear();
    }

    return cdp;
}

QVariantMap V4CdpMapper::mapV4ToCdpResponse_helper_stack(const QVariantMap &v4Response, const QVariantMap &origCdpRequest)
{
    QVariantMap cdp; int id = v4Response.value("ID").toInt(); cdp["id"] = id;
    QString method = origCdpRequest.value("method").toString();

    if (method == "Debugger.getStackTrace") {
        QVariantList frames = v4Response.value("Result").toList();
        // Convert string frames or V4 frames to CDP callFrames[] structure
        QVariantList callFrames;
        for (const QVariant &f : frames) {
            // If v4 provided plain strings like "func() at a.js:10", parse minimally
            if (f.typeId() == QMetaType::QString) {
                QString s = f.toString();
                // naive parse
                QString func = "";
                QString file = "";
                int line = 0;
                int at = s.indexOf(" at ");
                if (at != -1) {
                    func = s.left(at);
                    QString rest = s.mid(at + 4);
                    int colon = rest.lastIndexOf(":");
                    if (colon != -1) {
                        file = rest.left(colon);
                        line = rest.mid(colon + 1).toInt();
                    } else {
                        file = rest;
                    }
                }
                QVariantMap cf;
                cf["functionName"] = func;
                cf["url"] = file;
                cf["lineNumber"] = line;
                callFrames.append(cf);
            } else if (f.canConvert<QVariantMap>()) {
                QVariantMap fm = f.toMap();
                QVariantMap cf;
                cf["functionName"] = fm.value("functionName");
                cf["url"] = fm.value("fileName");
                cf["lineNumber"] = fm.value("lineNumber");
                callFrames.append(cf);
            }
        }
        cdp["result"] = QVariantMap{{"callFrames", callFrames}};
    } else {
        cdp.clear();
    }

    return cdp;
}

// Events (backend -> frontend) — map V4 event to CDP event
QVariantMap V4CdpMapper::mapV4EventToCdp(const QVariantMap &v4Resp, BackendV4SyncCall backendSyncCall)
{
    QVariantMap cdp;

    QVariantMap v4Event = v4Resp.value("Event").toMap();
    QString type = v4Event.value("type").toString();
    QVariantMap attrs = v4Event.value("attributes").toMap();

    if (type == "Interrupted") {
        cdp["method"] = "Debugger.paused";
        cdp["params"] = QVariantMap{{"reason", QString("interrupted")}, {"callFrames", QVariantList()}};
    } else if (type == "Breakpoint") {
        cdp["method"] = "Debugger.paused";
        QVariantMap p;
        p["reason"] = "other"; // is breakpoint
        QStringList hits;
        hits.append(attrs.value("breakPointId", -1).toString());
        p["hitBreakpoints"] = hits;
        p["callFrames"] = QVariantList();
        cdp["params"] = p;
    } else if (type == "SteppingFinished") {
        cdp["method"] = "Debugger.paused";
        cdp["params"] = QVariantMap{{"reason", QString("step")}, {"callFrames", QVariantList()}};
    } else if (type == "LocationReached") {
        cdp["method"] = "Debugger.paused";
        cdp["params"] = QVariantMap{{"reason", QString("location")}, {"callFrames", QVariantList()}};
    } else if (type == "DebuggerInvocationRequest") {
        cdp["method"] = "Debugger.paused";
        cdp["params"] = QVariantMap{{"reason", QString("debuggerStatement DebuggerInvocationRequest")}, {"callFrames", QVariantList()}};
    } else if (type == "Exception") {
        cdp["method"] = "Runtime.exceptionThrown";
        QVariantMap ed;
        ed["text"] = attrs.value("message").toString();
        ed["exception"] = attrs.value("value");
        cdp["params"] = QVariantMap{{"exceptionDetails", ed}};
    } else if (type == "InlineEvalFinished") {
        cdp["method"] = "Debugger.paused";
        QVariantMap request = QVariantMap{{"method", "Debugger.getStackTrace"}}; // I guess we are completly wrong here as we need callFrames
        QVariantMap v4StackTraceReq = mapCdpToV4Request_debugger(request);
        QVariantMap v4StackTraceResp = backendSyncCall(v4StackTraceReq).toMap();
        QVariantMap cdpResponse = mapV4ToCdpResponse_helper_stack(v4StackTraceResp, request);

        cdp["params"] = QVariantMap{{"reason", QString("debuggerStatement InlineEvalFinished")}, {"callFrames", QVariantList()}};
    } else if (type == "Trace") {
        cdp["method"] = "Console.messageAdded";
        QVariantMap msg;
        msg["text"] = attrs.value("message").toString();
        msg["level"] = attrs.value("level").toString();
        cdp["params"] = QVariantMap{{"message", msg}};
    } else {
        // Unknown event — return empty map as fallback
        cdp.clear();
    }

    //cdp["method"] = "Runtime.executionContextDestroyed"; // is a event not mapped. I guess not needed here
    return cdp;
}

// Other / Generic
QVariantMap V4CdpMapper::v4Request_custom(V4OnlyCommands method, int id, QString command, QString args)
{
    QVariantMap v4; v4["ID"] = id;

    if (method == V4OnlyCommands::Request) {
        QVariantMap attrs; attrs["command"] = command; attrs["args"] = args;
        v4["Command"] = QVariantMap{{"type", "Request"}, {"attributes", attrs}};
    } else {
        v4.clear();
        return v4; // return here so no MAPPER_METADATA can be set
    }

    return v4;
}

// the response is not CDP compliance nor used at all
QVariantMap V4CdpMapper::v4ToCdpResponse_custom(const QVariantMap &v4Response, const QVariantMap &origV4Request)
{
    QVariantMap cdp; int id = v4Response.value("ID").toInt(); cdp["id"] = id;
    V4OnlyCommands method = getInternalMethod(origV4Request);

    if (method == V4OnlyCommands::Request) {
        cdp["result"] = v4Response.value("Result");
    } else {
        cdp.clear();
    }

    return cdp;
}

//
// CDP -> V4 (Debugger.*)
//
QVariantMap V4CdpMapper::mapCdpToV4Request_debugger(QVariantMap& cdpRequest)
{
    QVariantMap v4Request;
    v4Request["ID"] = cdpRequest.value("id");
    QVariant &v4CommandRef = v4Request["Command"];
    QString method = cdpRequest.value("method").toString();
    QVariantMap params = cdpRequest.value("params").toMap();

    // Fallback (nicht erkannt)
    //v4Request["handled"] = false;

    // --------------------
    // Debugger.enable / disable
    // --------------------
    if (method == "Debugger.enable") {
        v4CommandRef = QVariantMap{{"type", "Attach"}};
    }
    else if (method == "Debugger.disable") {
        v4CommandRef = QVariantMap{{"type", "Detach"}};
    }

    // --------------------
    // runtime debugger controls
    // --------------------
    else if (method == "Debugger.pause") {
        v4CommandRef = QVariantMap{{"type", "Interrupt"}};
    }
    else if (method == "Debugger.resume") {
        v4CommandRef = QVariantMap{{"type", "Continue"}};
    }
    else if (method == "Debugger.stepInto") {
        v4CommandRef = QVariantMap{{"type", "StepInto"}};
    }
    else if (method == "Debugger.stepOver") {
        v4CommandRef = QVariantMap{{"type", "StepOver"}};
    }
    else if (method == "Debugger.stepOut") {
        v4CommandRef = QVariantMap{{"type", "StepOut"}};
    }

    // --------------------
    // Breakpoints
    // --------------------
    else if (method == "Debugger.setBreakpointByUrl") {
        QVariantMap bpData{
            {"fileName", normalizeScriptName(params.value("url").toString())},
            {"lineNumber", params.value("lineNumber")},
            {"condtion", params.value("condition")},
            {"enabled", true} // we assume breakpoints are always enabled when set
        };
        QVariantMap attributes;
        attributes["breakpointData"] = bpData;
        v4CommandRef = QVariantMap{{"type", "SetBreakpoint"}, {"attributes", attributes}};
    }
    else if (method == "Debugger.removeBreakpoint") {
        QVariantMap attributes{
            {"breakpointId", params.value("breakpointId")}
        };
        v4CommandRef = QVariantMap{{"type", "DeleteBreakpoint"}, {"attributes", attributes}};
    }
    else if (method == "Debugger.getPossibleBreakpoints") {
        v4CommandRef = QVariantMap{{"type", "GetBreakpoints"}};
    }

    // --------------------
    // Script / Source
    // --------------------
    else if (method == "Debugger.getScriptSource") {
        QVariantMap attributes{
            {"scriptId", params.value("scriptId")}
        };
        v4CommandRef = QVariantMap{{"type", "GetScriptData"}, {"attributes", attributes}};
    }

    // --------------------
    // Stack
    // --------------------
    else if (method == "Debugger.getStackTrace") {
        v4CommandRef = QVariantMap{{"type", "GetBacktrace"}};
    }

    // --------------------
    // Debugger Setup / Configuration -- no real backend mapping needed as they are not supported by V4
    // --------------------
    else if (method == "Debugger.setPauseOnExceptions" ||
             method == "Debugger.setAsyncCallStackDepth" ||
             method == "Debugger.setBlackboxPatterns") {
        createNoOpCdpToV4(v4Request, cdpRequest);
    }

    else if (method == "Debugger.evaluateOnCallFrame") {
        v4Request = mapCdpToV4Request_helper_evaluate(method, cdpRequest);
    }

    else {
        // Not handled by this module
        v4Request.clear();
        return v4Request; // return here so no MAPPER_METADATA can be set
    }

    cdpRequest[MAPPER_METADATA] = V4CdpMapper::Modules::Debugger;

    return v4Request;
}

//
// V4 -> CDP (Debugger.*)
//
QVariantMap V4CdpMapper::mapV4ToCdpResponse_debugger(const QVariantMap& v4Response, const QVariantMap& origCdpRequest)
{
    QVariantMap cdpResponse;
    QString method = origCdpRequest.value("method").toString();
    QVariantMap result;

    cdpResponse["id"] = v4Response.value("ID");
    QVariantMap v4Result = v4Response.value("Result").toMap();

    // Debugger.getScriptSource
    if (method == "Debugger.getScriptSource") {
        result["scriptSource"] = V4Helpers::getNestedValue(v4Response, {"Result", "result", "contents"});
        cdpResponse["result"] = result;
    }
    else if (method == "Debugger.removeBreakpoint") {
        cdpResponse["result"] = QVariantMap(); // empty result
    }
    // Breakpoint Mapping
    else if (method == "Debugger.setBreakpointByUrl") {
        bool isValidId;
        int scriptId = V4Helpers::getNestedValue(v4Response, {"Result", "result"}).toInt(&isValidId);

        if (isValidId) {
            result["breakpointId"] = QString::number(scriptId);
            cdpResponse["result"] = result;
        } else {
            QString unknownScript = V4Helpers::getNestedValue(origCdpRequest, {"params", "url"}).toString();
            // Failed to set breakpoint
            cdpResponse["error"] = QVariantMap{
                {"code", -32000},
                {"message", QString("No script matching %1").arg(unknownScript)}
            };
        }
    }
    // Stacktrace Mapping
    else if (method == "Debugger.getStackTrace") {
       cdpResponse = mapV4ToCdpResponse_helper_stack(v4Response, origCdpRequest);

    } else if (method == "Debugger.getPossibleBreakpoints") {
        // V4 Result is likely a list of breakpoints
        QVariantList v4List = V4Helpers::getNestedValue(v4Response, {"Result", "result"}).toList();
        QVariantList outList;
        for (const QVariant &b : v4List) {
            QVariantMap bm = b.toMap();
            QVariantMap e;
            e["lineNumber"] = bm.value("lineNumber");
            e["scriptId"] = bm.value("scriptId");
            outList.append(e);
        }
        cdpResponse["result"] = QVariantMap{{"locations", outList}};
    }

    else if (method == "Debugger.evaluateOnCallFrame") {
        QVariantMap res = v4Response.value("Result").toMap();
        // Expecting object handle
        QVariantMap out;
        if (v4Result.contains("type") && v4Result.value("type").toString() == "ObjectValue") {
            out["result"] = QVariantMap{{"type", "object"}, {"objectId", v4Result.value("value")}};
        } else {
            out["result"] = v4Result;
        }
        cdpResponse["result"] = out;
    }

    // no real backend mapping needed as they are not supported by V4
    else if (method == "Debugger.setPauseOnExceptions" ||
             method == "Debugger.setAsyncCallStackDepth" ||
             method == "Debugger.setBlackboxPatterns") {
        // No-Op passthrough
        cdpResponse["result"] = QVariantMap{};
    }

    // Default passthrough
    else {
        cdpResponse["result"] = v4Result;
    }

    return cdpResponse;
}

//
// CDP -> V4 (Runtime.*)
//
QVariantMap V4CdpMapper::mapCdpToV4Request_runtime(QVariantMap& cdpRequest)
{
    QVariantMap v4Request;
    v4Request["ID"] = cdpRequest.value("id");
    QVariant &v4CommandRef = v4Request["Command"];
    QString method = cdpRequest.value("method").toString();
    QVariantMap params = cdpRequest.value("params").toMap();

    //v4Request["handled"] = false;

    // Runtime.evaluate
    if (method == "Runtime.evaluate") {
        v4Request = mapCdpToV4Request_helper_evaluate(method, cdpRequest);
    }

    // Runtime.getProperties
    else if (method == "Runtime.getProperties") {
        QVariantMap attributes{
            {"iteratorId", params.value("objectId")}
        };
        v4CommandRef = QVariantMap{{"type", "GetPropertiesByIterator"}, {"attributes", attributes}};
    }

    // Runtime.callFunctionOn
    else if (method == "Runtime.callFunctionOn") {
        QVariantMap attributes{
            {"scriptValue", QVariantMap{{"type", "ObjectValue" }, {"value", params.value("functionDeclaration")}} }
        };
        v4CommandRef = QVariantMap{{"type", "ScriptValueToString"}, {"attributes", attributes}};
    }

    // no real backend mapping needed as they are not supported by V4
    else if (method == "Runtime.addBinding" ||
             method == "Runtime.removeBinding" ||
             method == "Runtime.releaseObject" ||
             method == "Runtime.releaseObjectGroup" ||
             method == "Runtime.getHeapUsage" ||
             method == "Runtime.awaitPromise") {
        createNoOpCdpToV4(v4Request, cdpRequest);
    }
    else {
        // Not handled by this module
        v4Request.clear();
        return v4Request; // return here so no MAPPER_METADATA can be set
    }

    cdpRequest[MAPPER_METADATA] = V4CdpMapper::Modules::Runtime;

    return v4Request;
}

//
// V4 -> CDP (Runtime.*)
//
QVariantMap V4CdpMapper::mapV4ToCdpResponse_runtime(const QVariantMap& v4Response, const QVariantMap& origCdpRequest)
{
    QVariantMap cdpResponse;
    QString method = origCdpRequest.value("method").toString();
    QVariantMap result;

    cdpResponse["id"] = v4Response.value("ID");
    cdpResponse["_mapper_metadata"] = origCdpRequest.value("_mapper_metadata");

    if (method == "Runtime.evaluate") {
        result["result"] = QVariantMap{
            {"type", "string"},
            {"value", v4Response.value("Result")}
        };
        cdpResponse["result"] = result;
    }
    else if (method == "Runtime.getProperties") {
        result["result"] = v4Response.value("Result").toList();
        cdpResponse["result"] = result;
    }
    else if (method == "Runtime.callFunctionOn") {
        cdpResponse["result"] = v4Response.value("Result"); // TODO not implemented in backend
    }
    // no real backend mapping needed as they are not supported by V4
    else if (method == "Runtime.addBinding" ||
             method == "Runtime.removeBinding" ||
             method == "Runtime.releaseObject" ||
             method == "Runtime.releaseObjectGroup" ||
             method == "Runtime.getHeapUsage" ||
             method == "Runtime.awaitPromise") {
        // No-Op passthrough
        cdpResponse["result"] = QVariantMap{};
    }
    else {
        cdpResponse["result"] = v4Response.value("Result");
    }

    return cdpResponse;
}
