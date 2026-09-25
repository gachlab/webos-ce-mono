#include "page_engine.h"
#include "bridge-scheme.h"
#include "scripts.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>

using wpewebkit_compat::scripts::kBridgeCore;

QWebFrame::QWebFrame(QWebPage* page)
    : QObject(page)
    , m_page(page)
{
}

QUrl QWebFrame::url() const
{
    return m_url;
}

void QWebFrame::prepareNewDocument()
{
    m_collecting = true;
    m_collected.clear();
    Q_EMIT javaScriptWindowObjectCleared();
    m_collecting = false;

    m_page->ensureView();
    using namespace wpewebkit_compat::scripts;
    // Separate user scripts (same as qtwebkit-compat): a quirk parse error must
    // not kill the bridge. kWindowOpen is a template — %1 is the page number.
    auto* view = m_page->m_engine->view.get();
    view->clearUserScripts();
    view->injectAtDocumentStart(QString::fromLatin1(kBridgeCore) + m_collected);
    view->injectAtDocumentStart(QString::fromLatin1(kPrefixedEvents));
    view->injectAtDocumentStart(QString::fromLatin1(kFrameCancel));
    view->injectAtDocumentStart(QString::fromLatin1(kBorderImageCompat));
    view->injectAtDocumentStart(QString::fromLatin1(kWindowOpen).arg(m_page->m_number));
    view->injectAtDocumentStart(QString::fromLatin1(kRemoteRequests));
    view->rebindUserContent();
}

void QWebFrame::load(const QUrl& url)
{
    m_url = url;
    m_page->ensureView();
    Q_EMIT m_page->loadStarted();
    prepareNewDocument();
    Q_EMIT urlChanged(url);
    m_page->m_engine->view->load(url);
}

void QWebFrame::setHtml(const QString& html, const QUrl& baseUrl)
{
    m_url = baseUrl;
    m_page->ensureView();
    Q_EMIT m_page->loadStarted();
    prepareNewDocument();
    m_page->m_engine->view->loadHtml(html, baseUrl);
}

QString QWebFrame::title() const
{
    if (!m_page->m_engine || !m_page->m_engine->view)
        return {};
    // webkit_web_view_get_title can lag behind document.title assignments from
    // script (measured empty while JS had the value). Prefer the live DOM.
    const QVariant live = m_page->m_engine->view->evaluateJavaScript(
        QStringLiteral("document.title"));
    if (live.isValid())
        return live.toString();
    return m_page->m_engine->view->title();
}

void QWebFrame::render(QPainter* painter, RenderLayer, const QRegion& clip)
{
    if (!m_page->m_engine || !m_page->m_engine->view)
        return;
    const QImage frame = m_page->m_engine->view->frame();
    if (frame.isNull())
        return;
    painter->save();
    if (!clip.isEmpty())
        painter->setClipRegion(clip, Qt::IntersectClip);
    painter->drawImage(0, 0, frame);

    for (const QWebPage::EmbeddedPage& embedded : m_page->m_embedded) {
        if (!embedded.page || embedded.rect.isEmpty())
            continue;
        embedded.page->ensureView();
        const QImage content = embedded.page->m_engine->view->frame();
        if (content.isNull())
            continue;
        painter->save();
        if (!embedded.cutouts.isEmpty()) {
            QRegion visible(embedded.rect);
            visible -= embedded.cutouts;
            painter->setClipRegion(visible, Qt::IntersectClip);
        }
        painter->drawImage(embedded.rect.topLeft(), content);
        painter->restore();
    }
    painter->restore();
}

void QWebFrame::addToJavaScriptWindowObject(const QString& name, QObject* object)
{
    if (!object)
        return;
    m_page->ensureView();
    const int id = wpewebkit_compat::bridge::publishObject(object, m_page);
    const QJsonObject meta = wpewebkit_compat::bridge::describe(object->metaObject());
    const QString assignment = QStringLiteral(
        "window[%1] = window.__webosBridge.proxy(%2, %3);\n")
        .arg(QString::fromUtf8(QJsonDocument(QJsonArray{name}).toJson(QJsonDocument::Compact)).mid(1).chopped(1))
        .arg(id)
        .arg(QString::fromUtf8(QJsonDocument(meta).toJson(QJsonDocument::Compact)));
    if (m_collecting)
        m_collected += assignment;
    else
        evaluateJavaScript(assignment);
}

QVariant QWebFrame::evaluateJavaScript(const QString& script)
{
    if (m_collecting) {
        m_collected += script + QStringLiteral(";\n");
        return {};
    }
    if (!m_page->m_engine || !m_page->m_engine->view)
        return {};
    return m_page->m_engine->view->evaluateJavaScript(script);
}

QWebElement QWebFrame::findFirstElement(const QString& selectorQuery) const
{
    QWebElement element;
    if (!m_page->m_engine || !m_page->m_engine->view)
        return element;

    const QString query = QString::fromUtf8(
        QJsonDocument(QJsonArray{selectorQuery}).toJson(QJsonDocument::Compact))
                              .mid(1)
                              .chopped(1);
    const QVariant found = m_page->m_engine->view->evaluateJavaScript(QString(R"JS((function () {
        var e = document.querySelector(%1);
        if (!e) return null;
        var r = e.getBoundingClientRect();
        var attrs = {};
        for (var i = 0; i < e.attributes.length; ++i)
            attrs[e.attributes[i].name] = e.attributes[i].value;
        return JSON.stringify({
            tag: e.tagName,
            attrs: attrs,
            rect: [r.left, r.top, r.width, r.height]
        });
    })())JS")
                                                                          .arg(query));
    if (!found.isValid() || found.toString().isEmpty() || found.toString() == QLatin1String("null"))
        return element;

    const QJsonObject obj = QJsonDocument::fromJson(found.toString().toUtf8()).object();
    if (obj.isEmpty())
        return element;
    element.m_null = false;
    element.m_tagName = obj.value(QStringLiteral("tag")).toString();
    const QJsonObject attrs = obj.value(QStringLiteral("attrs")).toObject();
    for (auto it = attrs.begin(); it != attrs.end(); ++it)
        element.m_attributes.insert(it.key(), it.value().toString());
    const QJsonArray rect = obj.value(QStringLiteral("rect")).toArray();
    if (rect.size() == 4) {
        element.m_geometry = QRect(static_cast<int>(rect.at(0).toDouble()),
                                   static_cast<int>(rect.at(1).toDouble()),
                                   static_cast<int>(rect.at(2).toDouble()),
                                   static_cast<int>(rect.at(3).toDouble()));
    }
    return element;
}

QWebHitTestResult QWebFrame::hitTestContent(const QPoint& pos) const
{
    QWebHitTestResult result;
    if (!m_page->m_engine || !m_page->m_engine->view)
        return result;

    const QVariant found = m_page->m_engine->view->evaluateJavaScript(QString(R"JS((function () {
        var e = document.elementFromPoint(%1, %2);
        if (!e) return null;
        var attrs = {};
        for (var i = 0; i < e.attributes.length; ++i)
            attrs[e.attributes[i].name] = e.attributes[i].value;
        var editable = false;
        if (e.tagName === "INPUT" || e.tagName === "TEXTAREA" || e.isContentEditable)
            editable = true;
        return JSON.stringify({
            tag: e.tagName,
            attrs: attrs,
            editable: editable,
            rect: (function () {
                var r = e.getBoundingClientRect();
                return [r.left, r.top, r.width, r.height];
            })()
        });
    })())JS")
                                                                          .arg(pos.x())
                                                                          .arg(pos.y()));
    if (!found.isValid() || found.toString().isEmpty() || found.toString() == QLatin1String("null"))
        return result;

    const QJsonObject obj = QJsonDocument::fromJson(found.toString().toUtf8()).object();
    if (obj.isEmpty())
        return result;
    result.m_element.m_null = false;
    result.m_element.m_tagName = obj.value(QStringLiteral("tag")).toString();
    const QJsonObject attrs = obj.value(QStringLiteral("attrs")).toObject();
    for (auto it = attrs.begin(); it != attrs.end(); ++it)
        result.m_element.m_attributes.insert(it.key(), it.value().toString());
    const QJsonArray rect = obj.value(QStringLiteral("rect")).toArray();
    if (rect.size() == 4) {
        result.m_element.m_geometry = QRect(static_cast<int>(rect.at(0).toDouble()),
                                            static_cast<int>(rect.at(1).toDouble()),
                                            static_cast<int>(rect.at(2).toDouble()),
                                            static_cast<int>(rect.at(3).toDouble()));
    }
    result.m_editable = obj.value(QStringLiteral("editable")).toBool();
    return result;
}
