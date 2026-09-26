#include "bridge-scheme.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaMethod>
#include <QMetaProperty>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QSet>
#include <QUrlQuery>
#include <QWebEngineDownloadRequest>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineUrlRequestJob>
#include <QWebEngineUrlScheme>
#include <QWebEngineUrlSchemeHandler>
#include <QWebEngineFrame>

namespace qtwebkit_compat {
namespace bridge {

namespace {

const char kScheme[] = "webos-bridge";

DownloadHook& downloadHookSlot()
{
    static DownloadHook hook = nullptr;
    return hook;
}

} // namespace

void setDownloadHook(DownloadHook hook)
{
    downloadHookSlot() = hook;
}

// Before QApplication exists: a URL scheme can only be registered then, and
// QtWebEngine wants shared GL contexts decided before the first one is made.
void beforeApplication()
{
    QWebEngineUrlScheme scheme(kScheme);
    scheme.setSyntax(QWebEngineUrlScheme::Syntax::Path);
    // LocalScheme and SecureScheme are what let a file:// page call it.
    scheme.setFlags(QWebEngineUrlScheme::LocalScheme | QWebEngineUrlScheme::LocalAccessAllowed
                    | QWebEngineUrlScheme::SecureScheme | QWebEngineUrlScheme::CorsEnabled
                    | QWebEngineUrlScheme::FetchApiAllowed);
    QWebEngineUrlScheme::registerScheme(scheme);

    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);

    // Prefer Chromium GPU raster (#84). An explicit QTWEBENGINE_CHROMIUM_FLAGS
    // in the environment still wins (CI sets --no-sandbox; product can force
    // --disable-gpu if a host regresses). Historically we defaulted to
    // --disable-gpu because WebAppMgr's offscreen path lost the GPU context;
    // tests/webengine-gpu-boot proves load+paint with --use-gl=egl today.
    if (qEnvironmentVariableIsEmpty("QTWEBENGINE_CHROMIUM_FLAGS"))
        qputenv("QTWEBENGINE_CHROMIUM_FLAGS",
                "--use-gl=egl --enable-gpu-rasterization");
}
Q_CONSTRUCTOR_FUNCTION(beforeApplication)

// The last features string each page announced, by page number, until the
// window it was meant for is created.
QHash<int, QString>& pendingWindowFeatures()
{
    static QHash<int, QString> table;
    return table;
}

// ---------------------------------------------------------------------------
// The C++ side: which QObject each id is, and what JavaScript may reach.

struct Published
{
    QPointer<QObject> object;
    QPointer<QWebEnginePage> page;   // where its signals are delivered
};

QHash<int, Published>& published()
{
    static QHash<int, Published> table;
    return table;
}

int publishObject(QObject* object, QWebEnginePage* page);

bool isOwnMember(const QMetaObject* meta, int index, bool method)
{
    // QObject's own members (destroyed, deleteLater, objectName...) stay out.
    const int offset = method ? QObject::staticMetaObject.methodCount()
                              : QObject::staticMetaObject.propertyCount();
    Q_UNUSED(meta);
    return index >= offset;
}

QJsonObject describe(const QMetaObject* meta)
{
    QJsonArray properties, methods, signalNames;
    for (int i = 0; i < meta->propertyCount(); ++i)
        if (isOwnMember(meta, i, false))
            properties.append(QString::fromLatin1(meta->property(i).name()));
    QSet<QString> seen;
    for (int i = 0; i < meta->methodCount(); ++i) {
        if (!isOwnMember(meta, i, true))
            continue;
        const QMetaMethod m = meta->method(i);
        if (m.access() != QMetaMethod::Public)
            continue;
        const QString name = QString::fromLatin1(m.name());
        if (seen.contains(name))
            continue;
        seen.insert(name);
        if (m.methodType() == QMetaMethod::Signal)
            signalNames.append(name);
        else
            methods.append(name);
    }
    return QJsonObject{{"properties", properties}, {"methods", methods}, {"signals", signalNames}};
}

QJsonValue toJson(const QVariant& value, QWebEnginePage* page)
{
    if (value.metaType().flags() & QMetaType::PointerToQObject) {
        QObject* object = value.value<QObject*>();
        if (!object)
            return QJsonValue::Null;
        return QJsonObject{{"__webosObject", publishObject(object, page)},
                           {"meta", describe(object->metaObject())}};
    }
    if (!value.isValid())
        return QJsonValue::Null;
    return QJsonValue::fromVariant(value);
}

// Runs one script in every frame of a page, depth first.
//
// QWebEnginePage::runJavaScript reaches the main frame and no other, while the
// bridge core keeps its proxies, and the handlers connected to them, in a map
// private to each frame. A reply to an object that a child frame proxied was
// landing in the main frame's map, which had never heard of that id, so
// __webosBridge.emit returned without calling anything. Ids come from a single
// counter for the whole page, so running this everywhere reaches exactly the
// frame that owns the object: in every other one emit finds nothing and stops.
void runInEveryFrame(QWebEngineFrame frame, const QString& script)
{
    if (!frame.isValid())
        return;
    frame.runJavaScript(script);
    for (QWebEngineFrame child : frame.children())
        runInEveryFrame(child, script);
}

// Receives one signal of one object and hands its arguments to JavaScript. It
// is QSignalSpy's technique: connect to a method index just past QObject's own
// and catch the call in qt_metacall.
class SignalRelay : public QObject
{
public:
    SignalRelay(QObject* sender, const QMetaMethod& signal, int id, QWebEnginePage* page)
        : QObject(sender), m_signal(signal), m_id(id), m_page(page)
    {
        QMetaObject::connect(sender, signal.methodIndex(), this,
                             QObject::staticMetaObject.methodCount(), Qt::DirectConnection);
    }

    int qt_metacall(QMetaObject::Call call, int methodId, void** argv) override
    {
        methodId = QObject::qt_metacall(call, methodId, argv);
        if (methodId < 0)
            return methodId;
        if (call == QMetaObject::InvokeMetaMethod) {
            if (methodId == 0)
                deliver(argv);
            --methodId;
        }
        return methodId;
    }

private:
    void deliver(void** argv)
    {
        if (!m_page)
            return;
        QJsonArray args;
        for (int i = 0; i < m_signal.parameterCount(); ++i)
            args.append(toJson(QVariant(QMetaType(m_signal.parameterType(i)), argv[i + 1]), m_page));
        const QString script = QString("window.__webosBridge && window.__webosBridge.emit(%1, %2, %3);")
            .arg(m_id)
            .arg(QString::fromUtf8(QJsonDocument(QJsonArray{QString::fromLatin1(m_signal.name())}).toJson(QJsonDocument::Compact)).mid(1).chopped(1))
            .arg(QString::fromUtf8(QJsonDocument(args).toJson(QJsonDocument::Compact)));
        runInEveryFrame(m_page->mainFrame(), script);
    }

    QMetaMethod m_signal;
    int m_id;
    QPointer<QWebEnginePage> m_page;
};

int publishObject(QObject* object, QWebEnginePage* page)
{
    static int nextId = 1;
    for (auto it = published().constBegin(); it != published().constEnd(); ++it)
        if (it.value().object == object)
            return it.key();

    const int id = nextId++;
    published().insert(id, Published{object, page});
    QObject::connect(object, &QObject::destroyed, [id]() { published().remove(id); });

    const QMetaObject* meta = object->metaObject();
    for (int i = QObject::staticMetaObject.methodCount(); i < meta->methodCount(); ++i) {
        const QMetaMethod m = meta->method(i);
        if (m.methodType() == QMetaMethod::Signal && m.access() == QMetaMethod::Public)
            new SignalRelay(object, m, id, page);
    }
    return id;
}

QByteArray replyValue(const QJsonValue& value)
{
    return QJsonDocument(QJsonObject{{"v", value}}).toJson(QJsonDocument::Compact);
}

QByteArray replyError(const QString& message)
{
    return QJsonDocument(QJsonObject{{"e", message}}).toJson(QJsonDocument::Compact);
}

QByteArray invoke(QObject* object, QWebEnginePage* page, const QString& name, const QJsonArray& args)
{
    const QMetaObject* meta = object->metaObject();
    for (int i = QObject::staticMetaObject.methodCount(); i < meta->methodCount(); ++i) {
        const QMetaMethod m = meta->method(i);
        if (m.methodType() == QMetaMethod::Signal || m.access() != QMetaMethod::Public)
            continue;
        if (QString::fromLatin1(m.name()) != name || m.parameterCount() != args.size())
            continue;
        if (args.size() > 10)
            return replyError(name + ": too many arguments");

        // Arguments, converted to what the method declares.
        QList<QVariant> values;
        values.reserve(args.size());
        for (int a = 0; a < args.size(); ++a) {
            QVariant v = args.at(a).toVariant();
            const QMetaType type(m.parameterType(a));
            if (type.id() != QMetaType::QVariant && !v.convert(type))
                v = QVariant(type);
            values.append(v);
        }
        QGenericArgument generic[10];
        for (int a = 0; a < values.size(); ++a) {
            const QMetaType type(m.parameterType(a));
            generic[a] = type.id() == QMetaType::QVariant
                ? QGenericArgument("QVariant", &values[a])
                : QGenericArgument(type.name(), values[a].constData());
        }

        QVariant result;
        bool ok;
        if (m.returnType() == QMetaType::Void) {
            ok = m.invoke(object, Qt::DirectConnection, generic[0], generic[1], generic[2], generic[3],
                          generic[4], generic[5], generic[6], generic[7], generic[8], generic[9]);
        } else {
            const QMetaType returnType(m.returnType());
            result = QVariant(returnType);
            ok = m.invoke(object, Qt::DirectConnection,
                          returnType.id() == QMetaType::QVariant
                              ? QGenericReturnArgument("QVariant", &result)
                              : QGenericReturnArgument(returnType.name(), result.data()),
                          generic[0], generic[1], generic[2], generic[3], generic[4],
                          generic[5], generic[6], generic[7], generic[8], generic[9]);
        }
        if (!ok)
            return replyError(name + ": the call failed");
        return replyValue(toJson(result, page));
    }
    return replyError(QString("%1 has no method %2 taking %3 arguments")
                          .arg(QString::fromLatin1(meta->className()), name).arg(args.size()));
}

QNetworkAccessManager* network()
{
    static QNetworkAccessManager* manager = new QNetworkAccessManager(qApp);
    return manager;
}

// webos-bridge:///fetch?u=<url>: the request a file:// document made, made
// here, and its answer handed back (see kRemoteRequests). The job is answered
// later, when the network is done; a request the page abandons is aborted.
//
// A job cannot carry an HTTP status, so an error status fails the request:
// the page sees status 0, as for a network error.
void proxyRequest(QWebEngineUrlRequestJob* job)
{
    const QUrl target(QUrlQuery(job->requestUrl()).queryItemValue("u", QUrl::FullyDecoded));
    if (!target.isValid() || (target.scheme() != QLatin1String("http") && target.scheme() != QLatin1String("https"))) {
        job->fail(QWebEngineUrlRequestJob::UrlInvalid);
        return;
    }

    QNetworkRequest request(target);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    const QMap<QByteArray, QByteArray> headers = job->requestHeaders();
    for (auto it = headers.constBegin(); it != headers.constEnd(); ++it) {
        // Chromium's own, about the file:// document; the network stack sets
        // the rest.
        const QByteArray name = it.key().toLower();
        if (name == "origin" || name == "referer" || name == "host" || name.startsWith("sec-"))
            continue;
        request.setRawHeader(it.key(), it.value());
    }
    QByteArray body;
    if (QIODevice* device = job->requestBody()) {
        if (!device->isOpen())
            device->open(QIODevice::ReadOnly);
        body = device->readAll();
    }

    QNetworkReply* reply = network()->sendCustomRequest(request, job->requestMethod(), body);
    QPointer<QWebEngineUrlRequestJob> pending(job);
    QObject::connect(job, &QObject::destroyed, reply, &QNetworkReply::abort);
    QObject::connect(reply, &QNetworkReply::finished, reply, [pending, reply]() {
        reply->deleteLater();
        if (!pending)
            return;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 0 || status >= 400) {
            pending->fail(QWebEngineUrlRequestJob::RequestFailed);
            return;
        }
        QByteArray type = reply->header(QNetworkRequest::ContentTypeHeader).toByteArray();
        if (type.isEmpty())
            type = "application/octet-stream";
        auto* device = new QBuffer(pending.data());
        device->setData(reply->readAll());
        pending->reply(type, device);
    });
}

class BridgeHandler : public QWebEngineUrlSchemeHandler
{
public:
    void requestStarted(QWebEngineUrlRequestJob* job) override
    {
        // webos-bridge:///<id>/<get|set|call>/<name>?a=<JSON array>
        // webos-bridge:///window/<page>?a=[<features>]
        // webos-bridge:///fetch?u=<url>
        const QStringList parts = job->requestUrl().path().split('/', Qt::SkipEmptyParts);
        const QJsonArray args = QJsonDocument::fromJson(
            QUrlQuery(job->requestUrl()).queryItemValue("a", QUrl::FullyDecoded).toUtf8()).array();

        if (parts.size() == 1 && parts[0] == QLatin1String("fetch")) {
            proxyRequest(job);
            return;
        }

        QByteArray body;
        if (parts.size() == 2 && parts[0] == QLatin1String("window")) {
            bool ok = false;
            const int page = parts[1].toInt(&ok);
            if (ok && pendingWindowFeatures().contains(page)) {
                pendingWindowFeatures()[page] = args.at(0).toString();
                body = replyValue(QJsonValue::Null);
            } else {
                body = replyError("no such page");
            }
            auto* device = new QBuffer(job);
            device->setData(body);
            job->reply("application/json", device);
            return;
        }
        const Published entry = parts.size() == 3 ? published().value(parts[0].toInt()) : Published{};
        if (!entry.object) {
            body = replyError("no such object");
        } else {
            QObject* object = entry.object;
            const QString op = parts[1];
            const QString name = QUrl::fromPercentEncoding(parts[2].toUtf8());
            const int propertyIndex = object->metaObject()->indexOfProperty(name.toLatin1());
            if (op == "get" && propertyIndex >= 0) {
                body = replyValue(toJson(object->metaObject()->property(propertyIndex).read(object), entry.page));
            } else if (op == "set" && propertyIndex >= 0 && !args.isEmpty()) {
                object->metaObject()->property(propertyIndex).write(object, args.at(0).toVariant());
                body = replyValue(QJsonValue::Null);
            } else if (op == "call") {
                body = invoke(object, entry.page, name, args);
            } else {
                body = replyError(op + " " + name + ": not available");
            }
        }

        auto* device = new QBuffer(job);
        device->setData(body);
        job->reply("application/json", device);
    }
};

// ---------------------------------------------------------------------------
// The profile every page shares.

QWebEngineProfile* sharedProfile()
{
    static QWebEngineProfile* profile = nullptr;
    if (profile)
        return profile;

    // A named profile keeps its storage on disk; QtWebEngine's default one does
    // not. Its location follows PERSISTENT_STORAGE_PATH, as SysMgrWebPage's
    // paths do.
    profile = new QWebEngineProfile(QStringLiteral("webOS"), qApp);
    const QByteArray root = qgetenv("PERSISTENT_STORAGE_PATH");
    if (!root.isEmpty() && QDir().mkpath(QString::fromLocal8Bit(root) + "/webengine"))
        profile->setPersistentStoragePath(QString::fromLocal8Bit(root) + "/webengine");

    static BridgeHandler handler;
    profile->installUrlSchemeHandler(kScheme, &handler);

    // Whether a site may have the location is com.palm.location's to say --
    // "Always Allow" and "Clear My Location Data" are kept there -- so the
    // engine keeps no answer of its own.
    profile->setPersistentPermissionsPolicy(QWebEngineProfile::PersistentPermissionsPolicy::AskEveryTime);

    // A response the engine will not show becomes a download, and a download
    // is the profile's, not the page's. On a device the browser plugin told
    // the app instead (mimeNotSupported), and the app handed it to
    // com.palm.downloadmanager. So the engine's own download is refused and the
    // page that asked is told. The page concern registers who to tell; this
    // file does not include QWebPage.
    QObject::connect(profile, &QWebEngineProfile::downloadRequested, profile,
                     [](QWebEngineDownloadRequest* download) {
        const QUrl url = download->url();
        const QString mimeType = download->mimeType();
        QObject* parent = download->page() ? download->page()->parent() : nullptr;
        download->cancel();
        if (DownloadHook hook = downloadHookSlot())
            hook(url, mimeType, parent);
    });
    return profile;
}

} // namespace bridge
} // namespace qtwebkit_compat
