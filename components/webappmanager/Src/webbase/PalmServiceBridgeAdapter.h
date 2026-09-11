/*
 * PalmServiceBridge adapter.
 *
 * PalmServiceBridge was a native object of HP's own WebKit
 * (isis-project/WebKit @ 0.54, Source/WebCore/platform/webos/). QtWebKit 5.212
 * does not have it, and porting that would mean adding IDL, binding generators
 * and build files inside WebCore.
 *
 * That is not necessary. The original code was two very different halves:
 *
 *   LunaServiceMgr.cpp    289 lines, ZERO references to WebCore. Pure
 *                         luna-service2 + glib. Reused almost verbatim.
 *   PalmServiceBridge.cpp 413 lines, 24 references to WebCore
 *                         (ActiveDOMObject, EventTarget, ScriptExecutionContext).
 *                         That is the only part that needs replacing.
 *
 * Here it is replaced by a QObject exposed with addToJavaScriptWindowObject,
 * the same mechanism webOS already uses to publish PalmSystem. Public Qt API
 * only: no patches to WebKit.
 *
 * The surface was measured before writing it, by instrumenting the bridge with
 * a stub that only logged calls. With the 7 apps starting up: 43 instances, and
 * always the same trio -- new, onservicecallback=, call(uri, payload). Never
 * version(), never token(). cancel() did not show up either, but it is
 * implemented anyway: it is ten lines and without it subscriptions leak.
 */
#ifndef PALMSERVICEBRIDGEADAPTER_H
#define PALMSERVICEBRIDGEADAPTER_H

#include <QObject>
#include <QString>
#include <lunaservice.h>

// From HP's LunaServiceMgr.h, no substantive changes.
struct LunaServiceManagerListener {
    LunaServiceManagerListener() : listenerToken(LSMESSAGE_TOKEN_INVALID), sh(0) { }
    virtual ~LunaServiceManagerListener() { }
    virtual void serviceResponse(const char* body) = 0;
    LSMessageToken listenerToken;
    LSHandle* sh;
};

// From HP's LunaServiceMgr.h. The medium and high priority buses are omitted:
// HP had them only so com.palm.app.phone could jump the queue, and that app is
// not part of the desktop drop.
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

// One bridge per "new PalmServiceBridge()" on the JS side.
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

// addToJavaScriptWindowObject publishes an INSTANCE, not a constructor, and
// apps do "new PalmServiceBridge()". Hence the factory: the JS shim calls it
// from its own constructor.
class PalmServiceBridgeFactory : public QObject {
    Q_OBJECT
public:
    explicit PalmServiceBridgeFactory(const QString& appId, QObject* parent = 0);
    Q_INVOKABLE QObject* create();

private:
    QString m_appId;
};

#endif // PALMSERVICEBRIDGEADAPTER_H
