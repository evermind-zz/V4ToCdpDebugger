#pragma once
#include <QVariant>
#include <QMutex>
#include <QHash>

// if a V4Request set to true, the request is ignored not forwarded to V4 backend
// but passed back to the client.
#define MAPPER_PASSTHROUGH "_mapper_passthrough"

// V4CdpMapper -- central mapper between Chrome DevTools Protocol (CDP)
// and the V4 internal debugger protocol (V4).
//
// Some hints:
// - methods per CDP domain's (debugger, runtime) and for events.
//   Others that cannot be assigned to a domain are grouped together hopefully
//   somewhat logical in post-fixed methods like *_location, *_scripts, *_stack, *_custom
//
// Dispatchers:
// - There is a CDP->V4 request mapper function and
//   a V4->CDP response mapper function that are public
//   mapV4ToCdpResponse() and mapCdpToV4Request()
// - The dispatcher functions try domains in sequence; a default
//   fallback (empty map) is returned if the module does not process the request
// - The dispatcher can then try other modules or report an error.
// - The original CDP request (origCdpRequest) is included for the response mappings
//   -- this makes the mapping robust in case of ambiguities.
// - Thread-safe storage of the original CDP requests using static
//   QHash<int, QVariantMap> and QMutex.
// - There are also a number of inconsistencies that other developers can fix :)

using BackendV4SyncCall = std::function<QVariant(const QVariant&)>;

class V4CdpMapper
{
    public:
        // Top-level dispatcher
        static QVariantMap mapCdpToV4Request(QVariantMap &cdpRequest);
        static QVariantMap mapV4ToCdpResponse(const QVariantMap &v4Response);

        // Events coming from V4 backend -> convert to CDP event
        static QVariantMap mapV4EventToCdp(const QVariantMap &v4Event, BackendV4SyncCall);

    public:
        // V4 Commands that are not mapped to any CDP counterpart as there may be none
        enum V4OnlyCommands {
            GetContextCount,
            GetContextInfo,
            GetScripts,
            GetScriptsDelta,
            ScriptsCheckpoint,
            RunToLocation,
            RunToLocationById,
            GetThisObject,
            Request,
            None
        };

        // Domain-level mappers (CDP -> V4)
        static QVariantMap mapCdpToV4Request_debugger(QVariantMap& cdpRequest);
        static QVariantMap mapCdpToV4Request_runtime(QVariantMap& cdpRequest);

        // Domain-level mappers (V4 -> CDP) — note: origCdpRequest provided
        static QVariantMap mapV4ToCdpResponse_debugger(const QVariantMap& v4Response, const QVariantMap& origCdpRequest);
        static QVariantMap mapV4ToCdpResponse_runtime(const QVariantMap& v4Response, const QVariantMap& origCdpRequest);

        // some helper function that might be called directly by the user
        static QVariantMap v4Request_scripts(V4OnlyCommands method, int id, int since = 0); // since only for GetScriptsDelta

    private:
        // helpers for request tracking (so we know orig CDP request when V4 response arrives)
        static void storeOrigCdpRequest(int id, const QVariantMap &cdpRequest);
        static QVariantMap takeOrigCdpRequest(int id);


        static QHash<int, QVariantMap> s_origCdpRequests;
        static QMutex s_mutex;

        struct Modules {
            inline static const QString Debugger     = QStringLiteral("Debugger");
            inline static const QString Runtime      = QStringLiteral("Runtime");
        };


        // more helper functions not all are used yet -- maybe never will :)
        static QVariantMap mapCdpToV4Request_helper_evaluate(QString &method, QVariantMap &cdpRequest);
        static QVariantMap mapV4ToCdpResponse_helper_stack(const QVariantMap &v4Response, const QVariantMap &origCdpRequest);

        static QVariantMap v4Request_location(V4OnlyCommands method, int id, QString fileName, int lineNumber, int scriptId);
        static QVariantMap v4Request_stack(V4OnlyCommands method, int id, int contextIndex);
        static QVariantMap v4Request_custom(V4OnlyCommands method, int id, QString command, QString args);

        static QVariantMap v4ToCdpResponse_location(const QVariantMap &v4Response, const QVariantMap &origV4Request);
        static QVariantMap v4ToCdpResponse_scripts(const QVariantMap &v4Response, const QVariantMap &origV4Request);
        static QVariantMap v4ToCdpResponse_stack(const QVariantMap &v4Response, const QVariantMap &origV4Request);
        static QVariantMap v4ToCdpResponse_custom(const QVariantMap &v4Response, const QVariantMap &origV4Request);

        using MapperFn = std::function<QVariantMap(const QVariantMap&, const QVariantMap&)>;
};
