/* See BrowserViewAdapter.h for why this replaces 29k lines of HP's. */

#include "BrowserViewAdapter.h"

#include <QRegion>
#include <QUrl>
#include <QWebFrame>
#include <QWebPage>

#ifndef WEBOS_WEB_ENGINE_WPE
#include <QWebEngineFullScreenRequest>
#include <QWebEngineCookieStore>
#include <QWebEngineHistory>
#include <QWebEngineProfile>
#include <QWebEnginePage>
#include <QWebEngineSettings>
#endif

#include <atomic>

BrowserViewAdapter::BrowserViewAdapter(QWebPage* host, QObject* parent)
    : QObject(parent)
    , m_host(host)
    , m_view(new QWebPage(this))
    , m_fullScreen(false)
{
#ifndef WEBOS_WEB_ENGINE_WPE
    // Without this the request is never made: QtWebEngine does not emit
    // fullScreenRequested at all unless the page is allowed to ask, so the
    // handler below was correct and could never have run. Nothing in this tree
    // turns it on anywhere else.
    m_view->enginePage()->settings()->setAttribute(
        QWebEngineSettings::FullScreenSupportEnabled, true);

    // Nobody was answering this, so a video's fullscreen button did nothing.
    connect(m_view->enginePage(), &QWebEnginePage::fullScreenRequested, this,
            [this](QWebEngineFullScreenRequest request) {
        request.accept();
        if (!m_host)
            return;
        if (request.toggleOn()) {
            m_rectBeforeFullScreen = m_rect;
            m_fullScreen = true;
            m_host->embedPage(m_view, QRect(QPoint(0, 0), m_host->viewportSize()));
        } else {
            m_fullScreen = false;
            if (!m_rectBeforeFullScreen.isEmpty())
                m_host->embedPage(m_view, m_rectBeforeFullScreen);
        }
    });
#endif

    connect(m_view, &QWebPage::loadStarted, this, [this]() { Q_EMIT loadStarted(); });
    connect(m_view, &QWebPage::loadProgress, this, [this](int p) { Q_EMIT loadProgress(p); });
    connect(m_view, &QWebPage::loadFinished, this, [this](bool ok) { Q_EMIT loadFinished(ok); });

    connect(m_view->mainFrame(), &QWebFrame::titleChanged, this,
            [this](const QString& title) { Q_EMIT titleChanged(title); });
    connect(m_view->mainFrame(), &QWebFrame::urlChanged, this,
            [this](const QUrl& url) { Q_EMIT urlChanged(url.toString()); });
    connect(m_view, &QWebPage::downloadRequested, this,
            [this](const QUrl& url, const QString& mimeType) { Q_EMIT fileRequested(mimeType, url.toString()); });
}

BrowserViewAdapter::~BrowserViewAdapter()
{
    close();
}

void BrowserViewAdapter::setGeometry(int x, int y, int width, int height)
{
    m_rect = QRect(x, y, width, height);
    if (!m_host || !m_view)
        return;
    if (m_fullScreen)
        return;
    m_host->embedPage(m_view, m_rect);
}

void BrowserViewAdapter::setCutouts(const QVariantList& rects)
{
    QRegion region;
    for (const QVariant& value : rects) {
        const QVariantList r = value.toList();
        if (r.size() == 4)
            region += QRect(r[0].toInt(), r[1].toInt(), r[2].toInt(), r[3].toInt());
    }
    if (m_host && m_view)
        m_host->setEmbeddedCutouts(m_view, region);
}

void BrowserViewAdapter::setUrl(const QString& url)
{
    if (!m_view)
        return;
    m_view->mainFrame()->load(QUrl::fromUserInput(url));
}

QString BrowserViewAdapter::url() const
{
    return m_view ? m_view->mainFrame()->url().toString() : QString();
}

void BrowserViewAdapter::goBack()
{
#ifndef WEBOS_WEB_ENGINE_WPE
    if (m_view)
        m_view->enginePage()->triggerAction(QWebEnginePage::Back);
#else
    if (m_view)
        m_view->mainFrame()->evaluateJavaScript(QStringLiteral("history.back()"));
#endif
}

void BrowserViewAdapter::goForward()
{
#ifndef WEBOS_WEB_ENGINE_WPE
    if (m_view)
        m_view->enginePage()->triggerAction(QWebEnginePage::Forward);
#else
    if (m_view)
        m_view->mainFrame()->evaluateJavaScript(QStringLiteral("history.forward()"));
#endif
}

void BrowserViewAdapter::reload()
{
#ifndef WEBOS_WEB_ENGINE_WPE
    if (m_view)
        m_view->enginePage()->triggerAction(QWebEnginePage::Reload);
#else
    if (m_view)
        m_view->mainFrame()->evaluateJavaScript(QStringLiteral("location.reload()"));
#endif
}

void BrowserViewAdapter::stop()
{
#ifndef WEBOS_WEB_ENGINE_WPE
    if (m_view)
        m_view->enginePage()->triggerAction(QWebEnginePage::Stop);
#else
    if (m_view)
        m_view->mainFrame()->evaluateJavaScript(QStringLiteral("window.stop()"));
#endif
}

void BrowserViewAdapter::findInPage(const QString& text)
{
#ifndef WEBOS_WEB_ENGINE_WPE
    if (m_view)
        m_view->enginePage()->findText(text);
#else
    Q_UNUSED(text);
#endif
}

bool BrowserViewAdapter::canGoBack() const
{
#ifndef WEBOS_WEB_ENGINE_WPE
    return m_view && m_view->enginePage()->history()->canGoBack();
#else
    return false;
#endif
}

bool BrowserViewAdapter::canGoForward() const
{
#ifndef WEBOS_WEB_ENGINE_WPE
    return m_view && m_view->enginePage()->history()->canGoForward();
#else
    return false;
#endif
}

void BrowserViewAdapter::setEnableJavaScript(bool enable)
{
#ifndef WEBOS_WEB_ENGINE_WPE
    if (m_view)
        m_view->enginePage()->settings()->setAttribute(QWebEngineSettings::JavascriptEnabled, enable);
#else
    if (m_view)
        m_view->settings()->setAttribute(QWebSettings::JavascriptEnabled, enable);
#endif
}

void BrowserViewAdapter::setBlockPopups(bool block)
{
#ifndef WEBOS_WEB_ENGINE_WPE
    if (m_view)
        m_view->enginePage()->settings()->setAttribute(QWebEngineSettings::JavascriptCanOpenWindows, !block);
#else
    if (m_view)
        m_view->settings()->setAttribute(QWebSettings::JavascriptCanOpenWindows, !block);
#endif
}

bool BrowserViewAdapter::blocksPopups() const
{
#ifndef WEBOS_WEB_ENGINE_WPE
    return m_view && !m_view->enginePage()->settings()->testAttribute(QWebEngineSettings::JavascriptCanOpenWindows);
#else
    return m_view && !m_view->settings()->testAttribute(QWebSettings::JavascriptCanOpenWindows);
#endif
}

static std::atomic<bool> s_acceptCookies(true);

#ifndef WEBOS_WEB_ENGINE_WPE
static bool cookieAllowed(const QWebEngineCookieStore::FilterRequest& request)
{
    if (s_acceptCookies.load())
        return true;
    const QString scheme = request.firstPartyUrl.scheme();
    return scheme != QLatin1String("http") && scheme != QLatin1String("https");
}
#endif

void BrowserViewAdapter::setAcceptCookies(bool accept)
{
    s_acceptCookies.store(accept);
#ifndef WEBOS_WEB_ENGINE_WPE
    if (!m_view)
        return;
    static QWebEngineCookieStore* filtered = nullptr;
    QWebEngineCookieStore* store = m_view->enginePage()->profile()->cookieStore();
    if (store != filtered) {
        store->setCookieFilter(cookieAllowed);
        filtered = store;
    }
#else
    Q_UNUSED(accept);
#endif
}

bool BrowserViewAdapter::acceptsCookies() const
{
    return s_acceptCookies.load();
}

void BrowserViewAdapter::setZoom(double factor)
{
#ifndef WEBOS_WEB_ENGINE_WPE
    if (m_view && factor > 0)
        m_view->enginePage()->setZoomFactor(factor);
#else
    Q_UNUSED(factor);
#endif
}

double BrowserViewAdapter::zoom() const
{
#ifndef WEBOS_WEB_ENGINE_WPE
    return m_view ? m_view->enginePage()->zoomFactor() : 1.0;
#else
    return 1.0;
#endif
}

void BrowserViewAdapter::close()
{
    if (!m_view)
        return;
    if (m_host)
        m_host->removeEmbeddedPage(m_view);
    delete m_view;
    m_view = 0;
}

BrowserViewFactory::BrowserViewFactory(QWebPage* host, QObject* parent)
    : QObject(parent)
    , m_host(host)
{
}

QObject* BrowserViewFactory::create()
{
    if (!m_host)
        return 0;
    return new BrowserViewAdapter(m_host, this);
}
