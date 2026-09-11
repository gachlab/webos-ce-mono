/*
 * Adaptador de PalmServiceBridge.
 *
 * PalmServiceBridge era un objeto nativo de la WebKit propia de HP
 * (isis-project/WebKit @ 0.54, Source/WebCore/platform/webos/). QtWebKit 5.212
 * no lo tiene, y portar aquello significaria meter IDL, generadores de bindings
 * y ficheros de build dentro de WebCore.
 *
 * No hace falta. Aquel codigo eran dos mitades muy distintas:
 *
 *   LunaServiceMgr.cpp    289 lineas, CERO referencias a WebCore. Puro
 *                         luna-service2 + glib. Se reusa casi tal cual.
 *   PalmServiceBridge.cpp 413 lineas, 24 referencias a WebCore
 *                         (ActiveDOMObject, EventTarget, ScriptExecutionContext).
 *                         Eso es lo unico que hay que sustituir.
 *
 * Aqui se sustituye por un QObject expuesto con addToJavaScriptWindowObject, que
 * es el mismo mecanismo con el que webOS ya publica PalmSystem. Solo API publica
 * de Qt: cero parches a WebKit.
 *
 * La superficie se midio antes de escribirla, instrumentando el puente con un
 * doble que solo registraba llamadas. Con las 7 apps arrancando: 43 instancias,
 * y siempre el mismo trio -- new, onservicecallback=, call(uri, payload).
 * Nunca version(), nunca token(). cancel() tampoco salio, pero se implementa
 * igual: son diez lineas y sin el se quedan vivas las suscripciones.
 */
#ifndef PALMSERVICEBRIDGEADAPTER_H
#define PALMSERVICEBRIDGEADAPTER_H

#include <QObject>
#include <QString>
#include <lunaservice.h>

// De LunaServiceMgr.h de HP, sin cambios de fondo.
struct LunaServiceManagerListener {
    LunaServiceManagerListener() : listenerToken(LSMESSAGE_TOKEN_INVALID), sh(0) { }
    virtual ~LunaServiceManagerListener() { }
    virtual void serviceResponse(const char* body) = 0;
    LSMessageToken listenerToken;
    LSHandle* sh;
};

// De LunaServiceMgr.h de HP. Se omiten los buses de prioridad media y alta: HP
// los tenia solo para que com.palm.app.phone adelantara al resto, y esa app no
// esta en el drop de escritorio.
class LunaServiceManager {
public:
    static LunaServiceManager* instance();
    unsigned long call(const char* uri, const char* payload,
                       LunaServiceManagerListener* listener, const char* callerId);
    void cancel(LunaServiceManagerListener* listener);

private:
    LunaServiceManager() : publicBus(0), privateBus(0), palmServiceHandle(0) { }
    bool init();

    LSHandle* publicBus;
    LSHandle* privateBus;
    LSPalmService* palmServiceHandle;
};

// Un puente por cada "new PalmServiceBridge()" del lado JS.
class PalmServiceBridgeAdapter : public QObject, public LunaServiceManagerListener {
    Q_OBJECT
public:
    explicit PalmServiceBridgeAdapter(const QString& appId, QObject* parent = 0);
    virtual ~PalmServiceBridgeAdapter();

    Q_INVOKABLE int call(const QString& uri, const QString& payload);
    Q_INVOKABLE void cancel();

    // LunaServiceManagerListener
    virtual void serviceResponse(const char* body);

Q_SIGNALS:
    void response(const QString& body);

private:
    QByteArray m_appId;
};

// addToJavaScriptWindowObject publica una INSTANCIA, no un constructor, y las
// apps hacen "new PalmServiceBridge()". De ahi la fabrica: el shim JS la llama
// desde su propio constructor.
class PalmServiceBridgeFactory : public QObject {
    Q_OBJECT
public:
    explicit PalmServiceBridgeFactory(const QString& appId, QObject* parent = 0);
    Q_INVOKABLE QObject* create();

private:
    QString m_appId;
};

#endif // PALMSERVICEBRIDGEADAPTER_H
