#include "detail.h"

#include <QBuffer>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPainter>
#include <QPixmap>
#include <QStyleOptionGraphicsItem>
#include <QTimer>
#include <QWebEngineScript>
#include <QWebEngineScriptCollection>
#include <QWebEngineSettings>
#include <QWebEngineView>
#include <memory>

using namespace qtwebkit_compat_detail;

// ---------------------------------------------------------------------------
// QWebFrame

QWebFrame::QWebFrame(QWebPage* page)
    : QObject(page)
    , m_page(page)
{
}

QUrl QWebFrame::url() const
{
    return m_page->enginePage()->url();
}

void QWebFrame::load(const QUrl& url)
{
    m_page->enginePage()->load(url);
}

void QWebFrame::setHtml(const QString& html, const QUrl& baseUrl)
{
    m_page->enginePage()->setHtml(html, baseUrl);
}

QString QWebFrame::title() const
{
    return m_page->enginePage()->title();
}

// The view as the page painted it. grab() fills the widget's background with
// the palette's window colour first, which is invisible under an opaque page
// and shows through a transparent one: dashboards, whose page is transparent
// so the shell's dark menu is their background, came out light grey.
static QPixmap grabView(QWebEngineView* view, bool transparent)
{
    if (!transparent)
        return view->grab();
    QPixmap frame(view->size() * view->devicePixelRatioF());
    frame.setDevicePixelRatio(view->devicePixelRatioF());
    frame.fill(Qt::transparent);
    view->render(&frame, QPoint(), QRegion(), QWidget::DrawChildren);
    return frame;
}

void QWebFrame::render(QPainter* painter, RenderLayer, const QRegion& clip)
{
    const QPixmap frame = grabView(m_page->m_view, m_page->m_transparent);
    painter->save();
    if (!clip.isEmpty())
        painter->setClipRegion(clip, Qt::IntersectClip);
    painter->drawPixmap(0, 0, frame);

    // Then the pages embedded in this one, each over its own hole. This is what
    // BrowserAdapter did with the buffer BrowserServer had filled, without the
    // plugin, the second process, the shared buffers or the semaphore: both
    // engines are ours and in this process. tests/embedded-view checks that
    // what the embedded page painted lands inside the host's pixels, in the
    // right place and nowhere else.
    for (const QWebPage::EmbeddedPage& embedded : m_page->m_embedded) {
        if (embedded.page.isNull() || embedded.rect.isEmpty())
            continue;
        const QPixmap content = grabView(embedded.page->m_view, embedded.page->m_transparent);
        if (content.isNull())
            continue;
        painter->save();
        // Whatever the host has over the hole stays on top.
        painter->setClipRegion(QRegion(embedded.rect).subtracted(embedded.cutouts), Qt::IntersectClip);
        painter->drawPixmap(embedded.rect.topLeft(), content);
        painter->restore();
    }

    painter->restore();
}

void QWebFrame::prepareNewDocument()
{
    m_collecting = true;
    m_collected.clear();
    Q_EMIT javaScriptWindowObjectCleared();
    m_collecting = false;

    QWebEngineScriptCollection& scripts = m_page->enginePage()->scripts();
    for (const QWebEngineScript& old : scripts.find(kInjectedScriptName))
        scripts.remove(old);
    QWebEngineScript script;
    script.setName(kInjectedScriptName);
    script.setInjectionPoint(QWebEngineScript::DocumentCreation);
    script.setWorldId(QWebEngineScript::MainWorld);
    // The objects a client published belong to every frame, as they did under
    // QtWebKit; see the collection built in QWebPage's constructor.
    script.setRunsOnSubFrames(true);
    script.setSourceCode(QString::fromLatin1(kBridgeCore) + m_collected);
    scripts.insert(script);
}

void QWebFrame::addToJavaScriptWindowObject(const QString& name, QObject* object)
{
    if (!object)
        return;
    const int id = publishObject(object, m_page->enginePage());
    const QString assignment = QString("window[%1] = window.__webosBridge.proxy(%2, %3);\n")
        .arg(QString::fromUtf8(QJsonDocument(QJsonArray{name}).toJson(QJsonDocument::Compact)).mid(1).chopped(1))
        .arg(id)
        .arg(QString::fromUtf8(QJsonDocument(describe(object->metaObject())).toJson(QJsonDocument::Compact)));
    if (m_collecting)
        m_collected += assignment;
    else
        m_page->enginePage()->runJavaScript(QString::fromLatin1(kBridgeCore) + assignment);
}

QVariant QWebFrame::evaluateAndWait(const QString& script) const
{
    // Shared with the callback rather than captured by reference: a script that
    // has not answered within the wait leaves its callback pending, and
    // QtWebEngine still runs it later -- at the latest while the page is being
    // destroyed -- when this frame's locals are long gone. Written through a
    // reference, that answer landed on a dead stack and crashed the process.
    struct Outcome {
        QVariant result;
        bool done = false;
    };
    const auto outcome = std::make_shared<Outcome>();
    m_page->enginePage()->runJavaScript(script, [outcome](const QVariant& value) {
        outcome->result = value;
        outcome->done = true;
    });
    QElapsedTimer timer;
    timer.start();
    while (!outcome->done && timer.elapsed() < 5000) {
        QEventLoop loop;
        QTimer::singleShot(5, &loop, &QEventLoop::quit);
        loop.exec(QEventLoop::ExcludeUserInputEvents);
    }
    return outcome->result;
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
    m_page->enginePage()->settings()->setAttribute(QWebEngineSettings::ShowScrollBars,
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
