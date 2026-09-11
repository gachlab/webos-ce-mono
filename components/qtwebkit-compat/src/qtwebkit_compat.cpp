#include "qtwebkit_compat.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaMethod>
#include <QMetaProperty>
#include <QPainter>
#include <QPixmap>
#include <QQuickWidget>
#include <QSet>
#include <QStyleOptionGraphicsItem>
#include <QQuickWindow>
#include <QTimer>
#include <QUrlQuery>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineScript>
#include <QWebEngineScriptCollection>
#include <QWebEngineSettings>
#include <QWebEngineUrlRequestJob>
#include <QWebEngineUrlScheme>
#include <QWebEngineUrlSchemeHandler>
#include <QWebEngineView>

namespace {

const char kScheme[] = "webos-bridge";
const char kInjectedScriptName[] = "webos-document-creation";

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

    // WebAppMgr only ever draws pages offscreen and reads them back; Chromium's
    // GPU process loses its context there ("Context lost during MakeCurrent").
    // An explicit choice in the environment still wins.
    if (qEnvironmentVariableIsEmpty("QTWEBENGINE_CHROMIUM_FLAGS"))
        qputenv("QTWEBENGINE_CHROMIUM_FLAGS", "--disable-gpu");
}
Q_CONSTRUCTOR_FUNCTION(beforeApplication)

// ---------------------------------------------------------------------------
// The JavaScript side of the object bridge. Runs once per document, first.

const char kBridgeCore[] = R"JS(
(function () {
    if (window.__webosBridge)
        return;
    var proxies = {};

    function unwrap(value) {
        if (value && typeof value === "object") {
            if (value.__webosObject !== undefined)
                return proxy(value.__webosObject, value.meta);
            if (Array.isArray(value))
                return value.map(unwrap);
        }
        return value;
    }

    function request(id, op, name, args) {
        var xhr = new XMLHttpRequest();
        xhr.open("GET", "webos-bridge:///" + id + "/" + op + "/" + encodeURIComponent(name)
                 + "?a=" + encodeURIComponent(JSON.stringify(args || [])), false);
        xhr.send();
        var reply = JSON.parse(xhr.responseText);
        if (reply.e !== undefined)
            throw new Error(reply.e);
        return unwrap(reply.v);
    }

    function proxy(id, meta) {
        if (proxies[id])
            return proxies[id];
        var object = {};
        meta.properties.forEach(function (name) {
            Object.defineProperty(object, name, {
                get: function () { return request(id, "get", name); },
                set: function (value) { request(id, "set", name, [value]); },
                enumerable: true
            });
        });
        meta.methods.forEach(function (name) {
            object[name] = function () {
                return request(id, "call", name, Array.prototype.slice.call(arguments));
            };
        });
        meta.signals.forEach(function (name) {
            var handlers = [];
            object[name] = {
                connect: function (fn) { handlers.push(fn); },
                disconnect: function (fn) {
                    var i = handlers.indexOf(fn);
                    if (i >= 0)
                        handlers.splice(i, 1);
                },
                __handlers: handlers
            };
        });
        proxies[id] = object;
        return object;
    }

    window.__webosBridge = {
        proxy: proxy,
        emit: function (id, name, args) {
            var object = proxies[id];
            if (!object || !object[name] || !object[name].__handlers)
                return;
            object[name].__handlers.slice().forEach(function (fn) {
                fn.apply(null, args.map(unwrap));
            });
        }
    };
})();
)JS";

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
        m_page->runJavaScript(script);
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

class BridgeHandler : public QWebEngineUrlSchemeHandler
{
public:
    void requestStarted(QWebEngineUrlRequestJob* job) override
    {
        // webos-bridge:///<id>/<get|set|call>/<name>?a=<JSON array>
        const QStringList parts = job->requestUrl().path().split('/', Qt::SkipEmptyParts);
        const QJsonArray args = QJsonDocument::fromJson(
            QUrlQuery(job->requestUrl()).queryItemValue("a", QUrl::FullyDecoded).toUtf8()).array();

        QByteArray body;
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
    return profile;
}

QWebEngineSettings::WebAttribute engineAttribute(QWebSettings::WebAttribute attribute, bool* exists)
{
    *exists = true;
    switch (attribute) {
    case QWebSettings::AutoLoadImages:                  return QWebEngineSettings::AutoLoadImages;
    case QWebSettings::JavascriptEnabled:               return QWebEngineSettings::JavascriptEnabled;
    case QWebSettings::PluginsEnabled:                  return QWebEngineSettings::PluginsEnabled;
    case QWebSettings::JavascriptCanOpenWindows:        return QWebEngineSettings::JavascriptCanOpenWindows;
    case QWebSettings::JavascriptCanAccessClipboard:    return QWebEngineSettings::JavascriptCanAccessClipboard;
    case QWebSettings::LinksIncludedInFocusChain:       return QWebEngineSettings::LinksIncludedInFocusChain;
    case QWebSettings::PrintElementBackgrounds:         return QWebEngineSettings::PrintElementBackgrounds;
    case QWebSettings::LocalStorageEnabled:             return QWebEngineSettings::LocalStorageEnabled;
    case QWebSettings::LocalContentCanAccessRemoteUrls: return QWebEngineSettings::LocalContentCanAccessRemoteUrls;
    case QWebSettings::DnsPrefetchEnabled:              return QWebEngineSettings::DnsPrefetchEnabled;
    case QWebSettings::XSSAuditingEnabled:              return QWebEngineSettings::XSSAuditingEnabled;
    case QWebSettings::SpatialNavigationEnabled:        return QWebEngineSettings::SpatialNavigationEnabled;
    case QWebSettings::LocalContentCanAccessFileUrls:   return QWebEngineSettings::LocalContentCanAccessFileUrls;
    default:
        *exists = false;
        return QWebEngineSettings::AutoLoadImages;
    }
}

QWebEngineSettings::FontFamily engineFont(QWebSettings::FontFamily family)
{
    switch (family) {
    case QWebSettings::FixedFont:     return QWebEngineSettings::FixedFont;
    case QWebSettings::SerifFont:     return QWebEngineSettings::SerifFont;
    case QWebSettings::SansSerifFont: return QWebEngineSettings::SansSerifFont;
    case QWebSettings::CursiveFont:   return QWebEngineSettings::CursiveFont;
    case QWebSettings::FantasyFont:   return QWebEngineSettings::FantasyFont;
    default:                          return QWebEngineSettings::StandardFont;
    }
}

} // namespace

QString qWebKitVersion()
{
    return QStringLiteral("534.34");
}

// ---------------------------------------------------------------------------
// QWebSettings

QWebSettings* QWebSettings::globalSettings()
{
    static QWebSettings* global = new QWebSettings(sharedProfile()->settings());
    return global;
}

void QWebSettings::setAttribute(WebAttribute attribute, bool on)
{
    m_attributes[attribute] = on;
    bool exists;
    const QWebEngineSettings::WebAttribute mapped = engineAttribute(attribute, &exists);
    if (exists && m_settings)
        m_settings->setAttribute(mapped, on);
}

bool QWebSettings::testAttribute(WebAttribute attribute) const
{
    bool exists;
    const QWebEngineSettings::WebAttribute mapped = engineAttribute(attribute, &exists);
    if (exists && m_settings)
        return m_settings->testAttribute(mapped);
    return m_attributes.value(attribute, false);
}

void QWebSettings::setFontFamily(FontFamily which, const QString& family)
{
    if (m_settings)
        m_settings->setFontFamily(engineFont(which), family);
}

// ---------------------------------------------------------------------------
// QWebPage

class QWebPage::Engine : public QWebEnginePage
{
public:
    Engine(QWebPage* owner) : QWebEnginePage(sharedProfile(), owner), m_owner(owner) {}

protected:
    QWebEnginePage* createWindow(WebWindowType type) override
    {
        QWebPage* created = m_owner->createWindow(type == WebDialog ? QWebPage::WebModalDialog
                                                                    : QWebPage::WebBrowserWindow);
        return created ? created->enginePage() : nullptr;
    }

    bool acceptNavigationRequest(const QUrl& url, NavigationType type, bool isMainFrame) override
    {
        QWebPage::NavigationType mapped = QWebPage::NavigationTypeOther;
        switch (type) {
        case NavigationTypeLinkClicked:  mapped = QWebPage::NavigationTypeLinkClicked; break;
        case NavigationTypeFormSubmitted: mapped = QWebPage::NavigationTypeFormSubmitted; break;
        case NavigationTypeBackForward:  mapped = QWebPage::NavigationTypeBackOrForward; break;
        case NavigationTypeReload:       mapped = QWebPage::NavigationTypeReload; break;
        default: break;
        }
        if (!m_owner->acceptNavigationRequest(isMainFrame ? m_owner->mainFrame() : nullptr,
                                              QNetworkRequest(url), mapped))
            return false;
        if (isMainFrame)
            m_owner->mainFrame()->prepareNewDocument();
        return true;
    }

    void javaScriptConsoleMessage(JavaScriptConsoleMessageLevel, const QString& message,
                                  int lineNumber, const QString& sourceId) override
    {
        m_owner->javaScriptConsoleMessage(message, lineNumber, sourceId);
    }

private:
    QWebPage* m_owner;
};

QWebPage::QWebPage(QObject* parent)
    : QObject(parent)
    , m_engine(nullptr)
    , m_view(new QWebEngineView)
    , m_frame(nullptr)
    , m_settings(nullptr)
    , m_viewportSize(1024, 768)
{
    m_engine = new Engine(this);
    m_frame = new QWebFrame(this);
    m_settings = new QWebSettings(m_engine->settings());

    m_view->setAttribute(Qt::WA_DontShowOnScreen);
    m_view->setPage(m_engine);
    m_view->resize(m_viewportSize);
    m_view->show();

    connect(m_engine, &QWebEnginePage::loadStarted, this, [this]() {
        followRenderSurface();
        Q_EMIT loadStarted();
    });
    // Every connection below goes through a lambda. Connected to a member
    // function instead, Qt's debug build dynamic_casts the receiver on each
    // emission, and WebAppMgr -- which subclasses this as SysMgrWebPage -- is
    // compiled with -fno-rtti: no type_info, and a segfault in loadProgress.
    connect(m_engine, &QWebEnginePage::loadProgress, this, [this](int progress) { Q_EMIT loadProgress(progress); });
    connect(m_engine, &QWebEnginePage::loadFinished, this, [this](bool ok) {
        followRenderSurface();
        Q_EMIT loadFinished(ok);
    });
    connect(m_engine, &QWebEnginePage::geometryChangeRequested, this,
            [this](const QRect& geometry) { Q_EMIT geometryChangeRequested(geometry); });
    connect(m_engine, &QWebEnginePage::windowCloseRequested, this, [this]() { Q_EMIT windowCloseRequested(); });
    connect(m_engine, &QWebEnginePage::titleChanged, m_frame,
            [this](const QString& title) { Q_EMIT m_frame->titleChanged(title); });
    connect(m_engine, &QWebEnginePage::urlChanged, m_frame,
            [this](const QUrl& url) { Q_EMIT m_frame->urlChanged(url); });
    connect(m_engine, &QWebEnginePage::contentsSizeChanged, m_frame,
            [this](const QSizeF& size) { Q_EMIT m_frame->contentsSizeChanged(size.toSize()); });

    // The bridge's JavaScript half, for documents loaded before any client
    // added an object.
    QWebEngineScript core;
    core.setName(kInjectedScriptName);
    core.setInjectionPoint(QWebEngineScript::DocumentCreation);
    core.setWorldId(QWebEngineScript::MainWorld);
    core.setSourceCode(QString::fromLatin1(kBridgeCore));
    m_engine->scripts().insert(core);
}

QWebPage::~QWebPage()
{
    delete m_settings;
    delete m_view;   // owns nothing of ours; the engine page is our child
}

QWebEnginePage* QWebPage::enginePage() const
{
    return m_engine;
}

void QWebPage::followRenderSurface()
{
    // QtWebEngine draws a page through a QQuickWidget that it creates, and may
    // replace, as documents load. Its scene graph renders once per new frame,
    // which is when QtWebKit would have asked for a repaint.
    QQuickWidget* surface = qobject_cast<QQuickWidget*>(m_view->focusProxy());
    if (!surface || !surface->quickWindow() || m_renderSurface == surface->quickWindow())
        return;
    m_renderSurface = surface->quickWindow();
    connect(surface->quickWindow(), &QQuickWindow::afterRendering, this, [this]() {
        Q_EMIT repaintRequested(QRect(QPoint(0, 0), m_viewportSize));
    }, Qt::QueuedConnection);
}

void QWebPage::triggerAction(WebAction action, bool checked)
{
    switch (action) {
    case Cut:       m_engine->triggerAction(QWebEnginePage::Cut, checked); break;
    case Copy:      m_engine->triggerAction(QWebEnginePage::Copy, checked); break;
    case Paste:     m_engine->triggerAction(QWebEnginePage::Paste, checked); break;
    case Undo:      m_engine->triggerAction(QWebEnginePage::Undo, checked); break;
    case Redo:      m_engine->triggerAction(QWebEnginePage::Redo, checked); break;
    case SelectAll: m_engine->triggerAction(QWebEnginePage::SelectAll, checked); break;
    default: break;
    }
}

void QWebPage::setViewportSize(const QSize& size)
{
    if (size.isEmpty() || size == m_viewportSize)
        return;
    m_viewportSize = size;
    m_view->resize(size);
    followRenderSurface();
}

QWebPage::ViewportAttributes QWebPage::viewportAttributesForSize(const QSize& availableSize) const
{
    ViewportAttributes attributes;
    attributes.m_size = QSizeF(availableSize);
    return attributes;
}

void QWebPage::setPalette(const QPalette& palette)
{
    m_palette = palette;
    if (palette.brush(QPalette::Base).color().alpha() == 0)
        m_engine->setBackgroundColor(Qt::transparent);
}

bool QWebPage::event(QEvent* event)
{
    switch (event->type()) {
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonRelease:
    case QEvent::MouseButtonDblClick:
    case QEvent::MouseMove:
    case QEvent::Wheel:
    case QEvent::KeyPress:
    case QEvent::KeyRelease:
    case QEvent::InputMethod:
    case QEvent::TouchBegin:
    case QEvent::TouchUpdate:
    case QEvent::TouchEnd:
    case QEvent::TouchCancel: {
        QWidget* target = m_view->focusProxy() ? m_view->focusProxy() : m_view;
        return QCoreApplication::sendEvent(target, event);
    }
    default:
        return QObject::event(event);
    }
}

QWebPage* QWebPage::createWindow(WebWindowType)
{
    return nullptr;
}

bool QWebPage::acceptNavigationRequest(QWebFrame*, const QNetworkRequest&, NavigationType)
{
    return true;
}

void QWebPage::javaScriptConsoleMessage(const QString& message, int lineNumber, const QString& sourceId)
{
    qInfo().noquote() << QString("JS: %1:%2: %3").arg(sourceId).arg(lineNumber).arg(message);
}

// ---------------------------------------------------------------------------
// QWebFrame

QWebFrame::QWebFrame(QWebPage* page)
    : QObject(page)
    , m_page(page)
{
}

QUrl QWebFrame::url() const
{
    return m_page->m_engine->url();
}

void QWebFrame::load(const QUrl& url)
{
    m_page->m_engine->load(url);
}

void QWebFrame::setHtml(const QString& html, const QUrl& baseUrl)
{
    m_page->m_engine->setHtml(html, baseUrl);
}

QString QWebFrame::title() const
{
    return m_page->m_engine->title();
}

void QWebFrame::render(QPainter* painter, RenderLayer, const QRegion& clip)
{
    const QPixmap frame = m_page->m_view->grab();
    painter->save();
    if (!clip.isEmpty())
        painter->setClipRegion(clip, Qt::IntersectClip);
    painter->drawPixmap(0, 0, frame);
    painter->restore();
}

void QWebFrame::prepareNewDocument()
{
    m_collecting = true;
    m_collected.clear();
    Q_EMIT javaScriptWindowObjectCleared();
    m_collecting = false;

    QWebEngineScriptCollection& scripts = m_page->m_engine->scripts();
    for (const QWebEngineScript& old : scripts.find(kInjectedScriptName))
        scripts.remove(old);
    QWebEngineScript script;
    script.setName(kInjectedScriptName);
    script.setInjectionPoint(QWebEngineScript::DocumentCreation);
    script.setWorldId(QWebEngineScript::MainWorld);
    script.setSourceCode(QString::fromLatin1(kBridgeCore) + m_collected);
    scripts.insert(script);
}

void QWebFrame::addToJavaScriptWindowObject(const QString& name, QObject* object)
{
    if (!object)
        return;
    const int id = publishObject(object, m_page->m_engine);
    const QString assignment = QString("window[%1] = window.__webosBridge.proxy(%2, %3);\n")
        .arg(QString::fromUtf8(QJsonDocument(QJsonArray{name}).toJson(QJsonDocument::Compact)).mid(1).chopped(1))
        .arg(id)
        .arg(QString::fromUtf8(QJsonDocument(describe(object->metaObject())).toJson(QJsonDocument::Compact)));
    if (m_collecting)
        m_collected += assignment;
    else
        m_page->m_engine->runJavaScript(QString::fromLatin1(kBridgeCore) + assignment);
}

QVariant QWebFrame::evaluateAndWait(const QString& script) const
{
    QVariant result;
    bool done = false;
    m_page->m_engine->runJavaScript(script, [&result, &done](const QVariant& value) {
        result = value;
        done = true;
    });
    QElapsedTimer timer;
    timer.start();
    while (!done && timer.elapsed() < 5000) {
        QEventLoop loop;
        QTimer::singleShot(5, &loop, &QEventLoop::quit);
        loop.exec(QEventLoop::ExcludeUserInputEvents);
    }
    return result;
}

QVariant QWebFrame::evaluateJavaScript(const QString& script)
{
    if (m_collecting) {
        // Part of the new document's first script: QtWebKit ran it against the
        // fresh global object before any of the page's own code.
        m_collected += script + QStringLiteral(";\n");
        return QVariant();
    }
    return evaluateAndWait(script);
}

QWebElement QWebFrame::findFirstElement(const QString& selectorQuery) const
{
    const QString query = QString::fromUtf8(QJsonDocument(QJsonArray{selectorQuery}).toJson(QJsonDocument::Compact)).mid(1).chopped(1);
    const QVariantMap found = evaluateAndWait(QString(R"JS((function () {
        var e = document.querySelector(%1);
        if (!e) return null;
        var r = e.getBoundingClientRect(), attrs = {};
        for (var i = 0; i < e.attributes.length; ++i) attrs[e.attributes[i].name] = e.attributes[i].value;
        return { tag: e.tagName, attrs: attrs, rect: [r.left, r.top, r.width, r.height] };
    })())JS").arg(query)).toMap();

    QWebElement element;
    if (found.isEmpty())
        return element;
    element.m_null = false;
    element.m_tagName = found.value("tag").toString();
    const QVariantMap attrs = found.value("attrs").toMap();
    for (auto it = attrs.constBegin(); it != attrs.constEnd(); ++it)
        element.m_attributes.insert(it.key(), it.value().toString());
    const QVariantList rect = found.value("rect").toList();
    if (rect.size() == 4)
        element.m_geometry = QRectF(rect[0].toDouble(), rect[1].toDouble(), rect[2].toDouble(), rect[3].toDouble()).toRect();
    return element;
}

QWebHitTestResult QWebFrame::hitTestContent(const QPoint& pos) const
{
    const QVariantMap found = evaluateAndWait(QString(R"JS((function () {
        var e = document.elementFromPoint(%1, %2);
        if (!e) return null;
        var r = e.getBoundingClientRect(), attrs = {};
        for (var i = 0; i < e.attributes.length; ++i) attrs[e.attributes[i].name] = e.attributes[i].value;
        var editable = e.isContentEditable || e.tagName === "TEXTAREA"
            || (e.tagName === "INPUT" && !/^(button|checkbox|radio|submit|reset|image|file|hidden)$/i.test(e.type));
        return { tag: e.tagName, attrs: attrs, rect: [r.left, r.top, r.width, r.height], editable: editable };
    })())JS").arg(pos.x()).arg(pos.y())).toMap();

    QWebHitTestResult result;
    if (found.isEmpty())
        return result;
    result.m_editable = found.value("editable").toBool();
    result.m_element.m_null = false;
    result.m_element.m_tagName = found.value("tag").toString();
    const QVariantMap attrs = found.value("attrs").toMap();
    for (auto it = attrs.constBegin(); it != attrs.constEnd(); ++it)
        result.m_element.m_attributes.insert(it.key(), it.value().toString());
    const QVariantList rect = found.value("rect").toList();
    if (rect.size() == 4)
        result.m_element.m_geometry = QRectF(rect[0].toDouble(), rect[1].toDouble(), rect[2].toDouble(), rect[3].toDouble()).toRect();
    return result;
}

void QWebFrame::setScrollBarPolicy(Qt::Orientation, Qt::ScrollBarPolicy policy)
{
    // QtWebEngine has one switch for both orientations.
    m_page->m_engine->settings()->setAttribute(QWebEngineSettings::ShowScrollBars,
                                               policy != Qt::ScrollBarAlwaysOff);
}

// ---------------------------------------------------------------------------
// QGraphicsWebView

QGraphicsWebView::QGraphicsWebView(QGraphicsItem* parent)
    : QGraphicsWidget(parent)
{
    setFlag(QGraphicsItem::ItemUsesExtendedStyleOption, true);
}

QGraphicsWebView::~QGraphicsWebView() = default;

void QGraphicsWebView::setPage(QWebPage* page)
{
    if (m_page == page)
        return;
    if (m_page)
        disconnect(m_page, nullptr, this, nullptr);
    m_page = page;
    if (!m_page)
        return;
    m_page->setViewportSize(size().toSize());
    connect(m_page, &QWebPage::repaintRequested, this, [this](const QRect& rect) { update(QRectF(rect)); });
}

void QGraphicsWebView::setGeometry(const QRectF& rect)
{
    QGraphicsWidget::setGeometry(rect);
    if (m_page)
        m_page->setViewportSize(rect.size().toSize());
}

void QGraphicsWebView::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget*)
{
    if (!m_page)
        return;
    const QRegion clip = option ? QRegion(option->exposedRect.toAlignedRect()) : QRegion();
    m_page->mainFrame()->render(painter, QWebFrame::ContentsLayer, clip);
}
