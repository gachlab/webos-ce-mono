// See PalmServiceBridgeAdapter.h for why any of this exists.
#include "PalmServiceBridgeAdapter.h"

#include <glib.h>
#include <unistd.h>

// -------------------------------------------------------------------------
// LunaServiceManager: ported from HP's LunaServiceMgr.cpp.
// The only changes are the strings (WTF::String -> QByteArray) and dropping the
// priority buses.
// -------------------------------------------------------------------------

static bool message_filter(LSHandle*, LSMessage* reply, void* ctx)
{
    LunaServiceManagerListener* listener = static_cast<LunaServiceManagerListener*>(ctx);
    if (!listener)
        return false;

    listener->serviceResponse(LSMessageGetPayload(reply));
    return true;
}

static LunaServiceManager* s_instance = 0;

LunaServiceManager* LunaServiceManager::instance()
{
    if (s_instance)
        return s_instance;

    s_instance = new LunaServiceManager();
    if (!s_instance->init()) {
        delete s_instance;
        s_instance = 0;
    }
    return s_instance;
}

bool LunaServiceManager::init()
{
    LSError lserror;
    LSErrorInit(&lserror);

    // HP registered "com.palm.luna-<pid>". That cannot work here: the
    // com.palm.webappmgr role lists "com.palm.luna-*" under allowedNames, so
    // the name may be registered, but no role in the desktop drop declares
    // PERMISSIONS for it -- and ls-hubd looks permissions up by exact name
    // (LSHubPermissionMapLookup does a plain g_hash_table_lookup; the only
    // wildcard is a special case for the media server). The result was every
    // outbound call being denied:
    //
    //   "com.palm.luna-NNN" does not have sufficient outbound permissions
    //   to communicate with "com.palm.db"
    //
    // A fixed name can be granted permissions, and one is enough because
    // LunaServiceManager is a singleton inside the single WebAppMgr process.
    // The caller identity apps are checked against is the appId, which travels
    // separately as LSCallFromApplication's callerId.
    QByteArray id("com.palm.webappmgr.bridge");

    if (!LSRegisterPalmService(id.constData(), &palmServiceHandle, &lserror))
        goto error;

    if (!LSGmainAttachPalmService(palmServiceHandle,
                                  g_main_loop_new(g_main_context_default(), TRUE), &lserror))
        goto error;

    publicBus  = LSPalmServiceGetPublicConnection(palmServiceHandle);
    privateBus = LSPalmServiceGetPrivateConnection(palmServiceHandle);
    return true;

error:
    g_warning("LunaServiceManager: no se pudo inicializar. ERROR %d: %s (%s @ %s:%d)",
              lserror.error_code, lserror.message, lserror.func, lserror.file, lserror.line);
    LSErrorFree(&lserror);
    return false;
}

unsigned long LunaServiceManager::call(const char* uri, const char* payload,
                                       LunaServiceManagerListener* listener, const char* callerId)
{
    LSError lserror;
    LSErrorInit(&lserror);
    LSMessageToken token = 0;

    if (callerId && !(*callerId))
        callerId = 0;

    // HP used the private bus whenever the call came from a document with a
    // frame, that is, whenever a real app makes it.
    LSHandle* serviceHandle = privateBus;

    if (!LSCallFromApplication(serviceHandle, uri, payload, callerId,
                               listener ? message_filter : 0, listener, &token, &lserror)) {
        g_warning("LSCallFromApplication ERROR %d: %s (%s @ %s:%d)",
                  lserror.error_code, lserror.message, lserror.func, lserror.file, lserror.line);
        LSErrorFree(&lserror);
        return 0;
    }

    if (listener) {
        listener->listenerToken = token;
        listener->sh = serviceHandle;
    }
    return token;
}

void LunaServiceManager::cancel(LunaServiceManagerListener* listener)
{
    if (!listener || !listener->listenerToken)
        return;

    LSError lserror;
    LSErrorInit(&lserror);

    if (!LSCallCancel(listener->sh, listener->listenerToken, &lserror)) {
        g_warning("LSCallCancel ERROR %d: %s (%s @ %s:%d)",
                  lserror.error_code, lserror.message, lserror.func, lserror.file, lserror.line);
        LSErrorFree(&lserror);
    }

    listener->listenerToken = 0;
}

// -------------------------------------------------------------------------
// The adapter itself
// -------------------------------------------------------------------------

PalmServiceBridgeAdapter::PalmServiceBridgeAdapter(const QString& appId, QObject* parent)
    : QObject(parent)
    , m_appId(appId.toUtf8())
{
}

PalmServiceBridgeAdapter::~PalmServiceBridgeAdapter()
{
    cancel();
}

int PalmServiceBridgeAdapter::call(const QString& uri, const QString& payload)
{
    LunaServiceManager* mgr = LunaServiceManager::instance();
    if (!mgr)
        return 0;

    // HP obtained the callerId by evaluating "PalmSystem.getIdentifier()" inside
    // the document, because from WebCore there was no more direct way. Here the
    // appId already reaches us from SysMgrWebBridge.
    return static_cast<int>(mgr->call(uri.toUtf8().constData(),
                                      payload.toUtf8().constData(),
                                      this, m_appId.constData()));
}

void PalmServiceBridgeAdapter::cancel()
{
    if (LunaServiceManager* mgr = LunaServiceManager::instance())
        mgr->cancel(this);
}

void PalmServiceBridgeAdapter::serviceResponse(const char* body)
{
    // luna-service2 is attached to the default glib context, which is what
    // drives Qt's event loop on Linux: this arrives on the right thread.
    Q_EMIT response(QString::fromUtf8(body));
}

PalmServiceBridgeFactory::PalmServiceBridgeFactory(const QString& appId, QObject* parent)
    : QObject(parent)
    , m_appId(appId)
{
}

QObject* PalmServiceBridgeFactory::create()
{
    // Parented to the factory, which lives as long as the page. That keeps the
    // object alive while JS holds it, and tears it all down when the app closes.
    return new PalmServiceBridgeAdapter(m_appId, this);
}
