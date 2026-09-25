#include "page_engine.h"
#include "bridge-scheme.h"
#include "scripts.h"

#include <QKeyEvent>
#include <QNetworkRequest>
#include <QStyleOptionGraphicsItem>

#include <glib.h>

QString qWebKitVersion()
{
    return QStringLiteral("538.1");
}

QWebSettings* QWebSettings::globalSettings()
{
    static QWebSettings settings;
    return &settings;
}

void QWebSettings::setAttribute(WebAttribute attribute, bool on)
{
    m_attributes.insert(static_cast<int>(attribute), on);
}

bool QWebSettings::testAttribute(WebAttribute attribute) const
{
    return m_attributes.value(static_cast<int>(attribute), false);
}

static QWebPage::GeolocationPolicy& geolocationPolicySlot()
{
    static QWebPage::GeolocationPolicy policy;
    return policy;
}

void QWebPage::setGeolocationPolicy(GeolocationPolicy policy)
{
    geolocationPolicySlot() = std::move(policy);
}

QWebPage::QWebPage(QObject* parent)
    : QObject(parent)
    , m_frame(new QWebFrame(this))
    , m_settings(new QWebSettings)
    , m_engine(std::make_unique<Engine>())
{
    static int nextNumber = 1;
    m_number = nextNumber++;

    wpewebkit_compat::bridge::ensureRegistered();

    m_engine->glibPump = new QTimer(this);
    QObject::connect(m_engine->glibPump, &QTimer::timeout, this, [] {
        while (g_main_context_pending(nullptr))
            g_main_context_iteration(nullptr, FALSE);
    });
    m_engine->glibPump->start(5);

    ensureView();
}

QWebPage::~QWebPage()
{
    delete m_settings;
}

void QWebPage::ensureView()
{
    if (m_engine->view)
        return;

    m_engine->view = std::make_unique<wpe_webcontent::HeadlessView>(
        m_viewportSize.width(), m_viewportSize.height());

    m_engine->view->setLoadFinishedCallback([this](bool ok) {
        Q_EMIT loadFinished(ok);
    });
    m_engine->view->setFrameCallback([this] {
        Q_EMIT repaintRequested(QRect(QPoint(0, 0), m_viewportSize));
    });
    m_engine->view->setTitleCallback([this](const QString& title) {
        Q_EMIT m_frame->titleChanged(title);
    });
}

void QWebPage::prepareNewDocument()
{
    m_frame->prepareNewDocument();
}

void QWebPage::setViewportSize(const QSize& size)
{
    if (size.isEmpty() || size == m_viewportSize)
        return;
    m_viewportSize = size;
    ensureView();
    m_engine->view->resize(size.width(), size.height());
}

QWebPage::ViewportAttributes QWebPage::viewportAttributesForSize(const QSize& availableSize) const
{
    ViewportAttributes attrs;
    attrs.m_valid = true;
    attrs.m_size = availableSize;
    return attrs;
}

void QWebPage::setPalette(const QPalette& palette)
{
    m_palette = palette;
    m_transparent = palette.brush(QPalette::Base).color().alpha() == 0;
}

void QWebPage::sendKeyToHostPage(QKeyEvent* event)
{
    QWebPage::event(event);
}

void QWebPage::embedPage(QWebPage* page, const QRect& rect)
{
    for (EmbeddedPage& embedded : m_embedded) {
        if (embedded.page == page) {
            embedded.rect = rect;
            return;
        }
    }
    m_embedded.append(EmbeddedPage{page, rect, {}});
    if (page)
        page->setViewportSize(rect.size());
}

void QWebPage::removeEmbeddedPage(QWebPage* page)
{
    for (int i = 0; i < m_embedded.size(); ++i) {
        if (m_embedded[i].page == page) {
            m_embedded.removeAt(i);
            return;
        }
    }
}

void QWebPage::setEmbeddedCutouts(QWebPage* page, const QRegion& cutouts)
{
    for (EmbeddedPage& embedded : m_embedded) {
        if (embedded.page == page) {
            embedded.cutouts = cutouts;
            return;
        }
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

void QWebPage::javaScriptConsoleMessage(const QString&, int, const QString&)
{
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
    connect(m_page, &QWebPage::repaintRequested, this, [this](const QRect& rect) {
        update(QRectF(rect));
    });
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
