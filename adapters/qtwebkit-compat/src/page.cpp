#include "qtwebkit_compat.h"
#include "bridge-scheme.h"
#include "scripts.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QWebEnginePage>
#include <QWebEnginePermission>
#include <QWebEngineProfile>
#include <QWebEngineScript>
#include <QWebEngineScriptCollection>
#include <QWebEngineSettings>
#include <QWebEngineView>
#include <QQuickWidget>
#include <QNetworkRequest>

namespace {

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

using qtwebkit_compat::bridge::pendingWindowFeatures;
using qtwebkit_compat::bridge::sharedProfile;
using qtwebkit_compat::scripts::kAppViewShimScriptName;
using qtwebkit_compat::scripts::kAppViewShims;
using qtwebkit_compat::scripts::kBorderImageCompat;
using qtwebkit_compat::scripts::kBorderImageScriptName;
using qtwebkit_compat::scripts::kBridgeCore;
using qtwebkit_compat::scripts::kBrowserView;
using qtwebkit_compat::scripts::kBrowserViewScriptName;
using qtwebkit_compat::scripts::kEnyoWheelCompat;
using qtwebkit_compat::scripts::kEnyoWheelScriptName;
using qtwebkit_compat::scripts::kFlexWidthCompat;
using qtwebkit_compat::scripts::kFlexWidthScriptName;
using qtwebkit_compat::scripts::kFrameCancel;
using qtwebkit_compat::scripts::kFrameCancelScriptName;
using qtwebkit_compat::scripts::kInjectedScriptName;
using qtwebkit_compat::scripts::kNumberInputScriptName;
using qtwebkit_compat::scripts::kNumberInputs;
using qtwebkit_compat::scripts::kPrefixedEventScriptName;
using qtwebkit_compat::scripts::kPrefixedEvents;
using qtwebkit_compat::scripts::kRemoteRequestScriptName;
using qtwebkit_compat::scripts::kRemoteRequests;
using qtwebkit_compat::scripts::kWindowOpen;
using qtwebkit_compat::scripts::kWindowOpenScriptName;

namespace {

// The bridge cancels engine downloads and calls this; we know they belong to
// a QWebPage because that is what parented the engine page.
void forwardDownload(const QUrl& url, const QString& mimeType, QObject* parent)
{
    if (auto* page = qobject_cast<QWebPage*>(parent))
        Q_EMIT page->downloadRequested(url, mimeType);
}

struct RegisterDownloadHook {
    RegisterDownloadHook() { qtwebkit_compat::bridge::setDownloadHook(forwardDownload); }
};

const RegisterDownloadHook registerDownloadHook;

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
        const QString features = pendingWindowFeatures().value(m_owner->m_number);
        pendingWindowFeatures()[m_owner->m_number].clear();
        if (!created)
            return nullptr;
        created->m_attributes = attributesOf(features);
        return created->enginePage();
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
    // Palm's QtWebKit handed the new page what follows "attributes=", the
    // rest of the string, which is JSON and may itself hold commas.
    static QString attributesOf(const QString& features)
    {
        static const QLatin1String key("attributes=");
        const qsizetype at = features.indexOf(key);
        return at < 0 ? QString() : features.mid(at + key.size()).trimmed();
    }

    QWebPage* m_owner;
};

namespace {
QWebPage::GeolocationPolicy& geolocationPolicy()
{
    static QWebPage::GeolocationPolicy policy;
    return policy;
}
}

void QWebPage::setGeolocationPolicy(GeolocationPolicy policy)
{
    geolocationPolicy() = std::move(policy);
}

QWebPage::QWebPage(QObject* parent)
    : QObject(parent)
    , m_engine(nullptr)
    , m_view(new QWebEngineView)
    , m_frame(nullptr)
    , m_settings(nullptr)
    , m_viewportSize(1024, 768)
{
    static int nextNumber = 1;
    m_number = nextNumber++;
    pendingWindowFeatures().insert(m_number, QString());

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
    // Only the location is asked for; anything else a page wants is refused,
    // as it was when nothing answered.
    connect(m_engine, &QWebEnginePage::permissionRequested, this, [](QWebEnginePermission permission) {
        if (permission.permissionType() != QWebEnginePermission::PermissionType::Geolocation
            || !geolocationPolicy()) {
            permission.deny();
            return;
        }
        geolocationPolicy()(permission.origin(), [permission](bool allowed) mutable {
            if (!permission.isValid())
                return;
            if (allowed)
                permission.grant();
            else
                permission.deny();
        });
    });

    // Every script below runs in the child frames too. A QWebEngineScript is
    // main-frame-only unless it says otherwise, while QtWebKit cleared and
    // repopulated every frame's global object -- so HP's code assumes a frame
    // is a frame. The mail card loads ../accounts/ into an iframe, where the
    // account wizard asked for PalmServiceBridge and found nothing.

    // The bridge's JavaScript half, for documents loaded before any client
    // added an object.
    QWebEngineScript core;
    core.setName(kInjectedScriptName);
    core.setInjectionPoint(QWebEngineScript::DocumentCreation);
    core.setWorldId(QWebEngineScript::MainWorld);
    core.setRunsOnSubFrames(true);
    core.setSourceCode(QString::fromLatin1(kBridgeCore));
    m_engine->scripts().insert(core);

    // Separate from the bridge: prepareNewDocument() rewrites that one on every
    // document, and this has nothing to do with the objects it publishes.
    QWebEngineScript borderImage;
    borderImage.setName(kBorderImageScriptName);
    borderImage.setInjectionPoint(QWebEngineScript::DocumentCreation);
    borderImage.setWorldId(QWebEngineScript::MainWorld);
    borderImage.setRunsOnSubFrames(true);
    borderImage.setSourceCode(QString::fromLatin1(kBorderImageCompat));
    m_engine->scripts().insert(borderImage);

    // The zero width enyo's flex layout leaves behind; see above. Separate
    // from the border image script because the two answer different questions
    // and one is not a good place to hide the other.
    QWebEngineScript flexWidth;
    flexWidth.setName(kFlexWidthScriptName);
    flexWidth.setInjectionPoint(QWebEngineScript::DocumentCreation);
    flexWidth.setWorldId(QWebEngineScript::MainWorld);
    flexWidth.setRunsOnSubFrames(true);
    flexWidth.setSourceCode(QString::fromLatin1(kFlexWidthCompat));
    m_engine->scripts().insert(flexWidth);

    // The legacy wheel event enyo listens for; see above. Separate again,
    // because this one is about an event Chromium renamed and the others are
    // about layout.
    QWebEngineScript enyoWheel;
    enyoWheel.setName(kEnyoWheelScriptName);
    enyoWheel.setInjectionPoint(QWebEngineScript::DocumentCreation);
    enyoWheel.setWorldId(QWebEngineScript::MainWorld);
    enyoWheel.setRunsOnSubFrames(true);
    enyoWheel.setSourceCode(QString::fromLatin1(kEnyoWheelCompat));
    m_engine->scripts().insert(enyoWheel);

    // The prefixed events Chromium dropped; see above.
    QWebEngineScript prefixedEvents;
    prefixedEvents.setName(kPrefixedEventScriptName);
    prefixedEvents.setInjectionPoint(QWebEngineScript::DocumentCreation);
    prefixedEvents.setWorldId(QWebEngineScript::MainWorld);
    prefixedEvents.setRunsOnSubFrames(true);
    prefixedEvents.setSourceCode(QString::fromLatin1(kPrefixedEvents));
    m_engine->scripts().insert(prefixedEvents);

    // The WebView methods the mail app calls on a plain view; see above.
    QWebEngineScript appViewShims;
    appViewShims.setName(kAppViewShimScriptName);
    appViewShims.setInjectionPoint(QWebEngineScript::DocumentCreation);
    appViewShims.setWorldId(QWebEngineScript::MainWorld);
    appViewShims.setRunsOnSubFrames(true);
    appViewShims.setSourceCode(QString::fromLatin1(kAppViewShims));
    m_engine->scripts().insert(appViewShims);

    // Number inputs that keep their text; see above.
    QWebEngineScript numberInputs;
    numberInputs.setName(kNumberInputScriptName);
    numberInputs.setInjectionPoint(QWebEngineScript::DocumentCreation);
    numberInputs.setWorldId(QWebEngineScript::MainWorld);
    numberInputs.setRunsOnSubFrames(true);
    numberInputs.setSourceCode(QString::fromLatin1(kNumberInputs));
    m_engine->scripts().insert(numberInputs);

    // The frame canceller Chromium dropped; see above.
    QWebEngineScript frameCancel;
    frameCancel.setName(kFrameCancelScriptName);
    frameCancel.setInjectionPoint(QWebEngineScript::DocumentCreation);
    frameCancel.setWorldId(QWebEngineScript::MainWorld);
    frameCancel.setRunsOnSubFrames(true);
    frameCancel.setSourceCode(QString::fromLatin1(kFrameCancel));
    m_engine->scripts().insert(frameCancel);

    // The browser's content area; see above.
    QWebEngineScript browserView;
    browserView.setName(kBrowserViewScriptName);
    browserView.setInjectionPoint(QWebEngineScript::DocumentCreation);
    browserView.setWorldId(QWebEngineScript::MainWorld);
    browserView.setRunsOnSubFrames(true);
    browserView.setSourceCode(QString::fromLatin1(kBrowserView));
    m_engine->scripts().insert(browserView);

    // An app's requests to the network; see kRemoteRequests.
    QWebEngineScript remoteRequests;
    remoteRequests.setName(kRemoteRequestScriptName);
    remoteRequests.setInjectionPoint(QWebEngineScript::DocumentCreation);
    remoteRequests.setWorldId(QWebEngineScript::MainWorld);
    remoteRequests.setRunsOnSubFrames(true);
    remoteRequests.setSourceCode(QString::fromLatin1(kRemoteRequests));
    m_engine->scripts().insert(remoteRequests);

    // What a window opened from here is; see kWindowOpen.
    QWebEngineScript windowOpen;
    windowOpen.setName(kWindowOpenScriptName);
    windowOpen.setInjectionPoint(QWebEngineScript::DocumentCreation);
    windowOpen.setWorldId(QWebEngineScript::MainWorld);
    windowOpen.setRunsOnSubFrames(true);
    windowOpen.setSourceCode(QString::fromLatin1(kWindowOpen).arg(m_number));
    m_engine->scripts().insert(windowOpen);
}

QWebPage::~QWebPage()
{
    pendingWindowFeatures().remove(m_number);
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
    m_transparent = palette.brush(QPalette::Base).color().alpha() == 0;
    if (m_transparent)
        m_engine->setBackgroundColor(Qt::transparent);
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
