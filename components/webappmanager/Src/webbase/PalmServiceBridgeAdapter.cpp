// Ver PalmServiceBridgeAdapter.h para el porque de todo esto.
#include "PalmServiceBridgeAdapter.h"

#include <glib.h>
#include <unistd.h>

// -------------------------------------------------------------------------
// LunaServiceManager: portado de LunaServiceMgr.cpp de HP.
// Lo unico que cambia son las cadenas (WTF::String -> QByteArray) y la poda de
// los buses de prioridad.
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

    // El nombre lleva el pid, como en HP. El role de com.palm.webappmgr lo
    // permite con el comodin "com.palm.luna-*".
    QByteArray id = QByteArray("com.palm.luna-") + QByteArray::number(static_cast<int>(getpid()));

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

    // HP usaba el bus privado siempre que la llamada viniera de un documento con
    // frame, o sea siempre que la hace una app de verdad.
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
// El adaptador propiamente dicho
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

    // HP sacaba el callerId evaluando "PalmSystem.getIdentifier()" dentro del
    // documento, porque desde WebCore no habia forma mas directa. Aqui el
    // appId nos llega ya hecho desde SysMgrWebBridge.
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
    // luna-service2 esta enganchado al contexto glib por defecto, que es el que
    // bombea el event loop de Qt en Linux: esto llega en el hilo correcto.
    Q_EMIT response(QString::fromUtf8(body));
}

PalmServiceBridgeFactory::PalmServiceBridgeFactory(const QString& appId, QObject* parent)
    : QObject(parent)
    , m_appId(appId)
{
}

QObject* PalmServiceBridgeFactory::create()
{
    // Colgado de la fabrica, que vive lo que vive la pagina. Asi el objeto no se
    // muere mientras JS lo tenga, y se destruye entero al cerrar la app.
    return new PalmServiceBridgeAdapter(m_appId, this);
}
