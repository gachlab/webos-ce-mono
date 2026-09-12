/* See BrowserViewAdapter.h for why this replaces 29k lines of HP's. */

#include "BrowserViewAdapter.h"

#include <QUrl>
#include <QWebFrame>
#include <QWebPage>

#include <QWebEngineFullScreenRequest>
#include <QWebEngineHistory>
#include <QWebEnginePage>
#include <QWebEngineSettings>

BrowserViewAdapter::BrowserViewAdapter(QWebPage* host, QObject* parent)
    : QObject(parent)
    , m_host(host)
    , m_view(new QWebPage(this))
    , m_fullScreen(false)
{
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

    // Straight through to the engine. Our QWebPage::triggerAction only carries
    // the editing actions QtWebKit's callers used, so navigation goes to
    // QWebEnginePage, which has all four.
    connect(m_view, &QWebPage::loadStarted, this, [this]() { Q_EMIT loadStarted(); });
    connect(m_view, &QWebPage::loadProgress, this, [this](int p) { Q_EMIT loadProgress(p); });
    connect(m_view, &QWebPage::loadFinished, this, [this](bool ok) { Q_EMIT loadFinished(ok); });

    // The compat layer forwards these onto the frame, not the page.
    connect(m_view->mainFrame(), &QWebFrame::titleChanged, this,
            [this](const QString& title) { Q_EMIT titleChanged(title); });
    connect(m_view->mainFrame(), &QWebFrame::urlChanged, this,
            [this](const QUrl& url) { Q_EMIT urlChanged(url.toString()); });
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
    // While a video is fullscreen the app keeps reporting the geometry of its
    // ordinary content area, which would put the hole straight back and end the
    // fullscreen a frame after it started. Remember it, do not apply it.
    if (m_fullScreen)
        return;
    // An empty rect is not a mistake: it is the page saying "not now", because
    // the hole is hidden or the app has opened something over it. It goes
    // through, so the host stops blitting until a real rect arrives.
    m_host->embedPage(m_view, m_rect);
}

void BrowserViewAdapter::setUrl(const QString& url)
{
    if (!m_view)
        return;
    // QUrl::fromUserInput, not QUrl: what arrives here came from an address
    // bar, so "example.com" has to mean http://example.com rather than a
    // relative path.
    m_view->mainFrame()->load(QUrl::fromUserInput(url));
}

QString BrowserViewAdapter::url() const
{
    return m_view ? m_view->mainFrame()->url().toString() : QString();
}

void BrowserViewAdapter::goBack()
{
    if (m_view)
        m_view->enginePage()->triggerAction(QWebEnginePage::Back);
}

void BrowserViewAdapter::goForward()
{
    if (m_view)
        m_view->enginePage()->triggerAction(QWebEnginePage::Forward);
}

void BrowserViewAdapter::reload()
{
    if (m_view)
        m_view->enginePage()->triggerAction(QWebEnginePage::Reload);
}

void BrowserViewAdapter::stop()
{
    if (m_view)
        m_view->enginePage()->triggerAction(QWebEnginePage::Stop);
}

void BrowserViewAdapter::findInPage(const QString& text)
{
    if (m_view)
        m_view->enginePage()->findText(text);
}

bool BrowserViewAdapter::canGoBack() const
{
    return m_view && m_view->enginePage()->history()->canGoBack();
}

bool BrowserViewAdapter::canGoForward() const
{
    return m_view && m_view->enginePage()->history()->canGoForward();
}

void BrowserViewAdapter::setZoom(double factor)
{
    if (m_view && factor > 0)
        m_view->enginePage()->setZoomFactor(factor);
}

double BrowserViewAdapter::zoom() const
{
    return m_view ? m_view->enginePage()->zoomFactor() : 1.0;
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
