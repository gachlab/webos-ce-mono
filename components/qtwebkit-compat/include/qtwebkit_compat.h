// QtWebKit's classes as WebAppMgr uses them, implemented on QtWebEngine.
//
// The same approach as components/node-v8-shim and qt6-compat: WebAppMgr keeps
// calling the API it was written against, and this maps it onto the engine that
// exists for Qt 6. Only what WebAppMgr uses is here -- the list came from
// compiling it against empty class declarations.
//
// How each QtWebKit behaviour is obtained, all checked in
// tests/webengine-capabilities-qt6:
//
//  - Painting. Every QWebPage owns a QWebEngineView that is never shown on
//    screen. QWebFrame::render() grabs it, and repaintRequested() follows the
//    view's scene graph (afterRendering), which fires once per new frame.
//  - Input. QWebPage::event() forwards input events to the view's focus proxy,
//    the widget QtWebEngine takes input through.
//  - JavaScript objects. addToJavaScriptWindowObject() publishes a QObject as a
//    JavaScript proxy. Property reads, writes and method calls are synchronous,
//    as they were in QtWebKit: a synchronous XMLHttpRequest to the
//    webos-bridge:/// scheme, answered in this process through QMetaObject.
//    Signals reach JavaScript through runJavaScript().
//  - The moment objects are added. QtWebKit emitted javaScriptWindowObjectCleared
//    when a new document's global object was created, and what the handler added
//    was visible to the page's first script. Here it is emitted when a main-frame
//    navigation is accepted; objects added and scripts evaluated from that
//    handler are collected into a script that runs at DocumentCreation, before
//    the page's own.
//
// Known differences: evaluateJavaScript() waits for its result in a nested event
// loop; linkClicked() and microFocusChanged() are declared but never emitted
// (WebAppMgr connects them, but QtWebKit only emitted linkClicked with a link
// delegation policy WebAppMgr never set); settings with no QtWebEngine
// counterpart are remembered and have no effect.

#ifndef QTWEBKIT_COMPAT_H
#define QTWEBKIT_COMPAT_H

#include <QGraphicsWidget>
#include <QHash>
#include <QMap>
#include <QNetworkRequest>
#include <QObject>
#include <QPalette>
#include <QPointer>
#include <QRect>
#include <QRegion>
#include <QSize>
#include <QString>
#include <QUrl>
#include <QVariant>

class QPainter;
class QWebEnginePage;
class QWebEngineSettings;
class QWebEngineView;
class QWebFrame;
class QWebPage;

// Same string QtWebKit 2.2 reported; apps that look at the WebKit version see
// what they were written for.
QString qWebKitVersion();

class QWebSettings
{
public:
    enum FontFamily { StandardFont, FixedFont, SerifFont, SansSerifFont, CursiveFont, FantasyFont };
    enum WebAttribute {
        AutoLoadImages, JavascriptEnabled, JavaEnabled, PluginsEnabled, PrivateBrowsingEnabled,
        JavascriptCanOpenWindows, JavascriptCanCloseWindows, JavascriptCanAccessClipboard,
        DeveloperExtrasEnabled, LinksIncludedInFocusChain, ZoomTextOnly, PrintElementBackgrounds,
        OfflineStorageDatabaseEnabled, OfflineWebApplicationCacheEnabled, LocalStorageEnabled,
        LocalContentCanAccessRemoteUrls, DnsPrefetchEnabled, XSSAuditingEnabled,
        AcceleratedCompositingEnabled, SpatialNavigationEnabled, LocalContentCanAccessFileUrls,
        TiledBackingStoreEnabled, FrameFlatteningEnabled, SiteSpecificQuirksEnabled
    };

    // Settings shared by every page, as in QtWebKit.
    static QWebSettings* globalSettings();

    void setAttribute(WebAttribute attribute, bool on);
    bool testAttribute(WebAttribute attribute) const;
    void setFontFamily(FontFamily which, const QString& family);

    // QtWebEngine keeps storage per profile, not per page. The profile these
    // pages share takes its location from PERSISTENT_STORAGE_PATH -- the same
    // variable SysMgrWebPage reads to compute the paths it passes here -- so
    // these only record what was asked.
    void setIconDatabasePath(const QString& path) { m_paths["icons"] = path; }
    void setOfflineStoragePath(const QString& path) { m_paths["offline"] = path; }
    void setOfflineWebApplicationCachePath(const QString& path) { m_paths["appcache"] = path; }
    void setLocalStoragePath(const QString& path) { m_paths["local"] = path; }

private:
    explicit QWebSettings(QWebEngineSettings* settings) : m_settings(settings) {}

    QWebEngineSettings* m_settings;
    QHash<int, bool> m_attributes;
    QHash<QString, QString> m_paths;

    friend class QWebPage;
};

class QWebElement
{
public:
    QWebElement() = default;

    bool isNull() const { return m_null; }
    QString tagName() const { return m_tagName; }
    QString attribute(const QString& name, const QString& defaultValue = QString()) const
    {
        return m_attributes.value(name, defaultValue);
    }
    QRect geometry() const { return m_geometry; }

private:
    bool m_null = true;
    QString m_tagName;
    QMap<QString, QString> m_attributes;
    QRect m_geometry;

    friend class QWebFrame;
    friend class QWebHitTestResult;
};

class QWebHitTestResult
{
public:
    QWebHitTestResult() = default;

    bool isNull() const { return m_element.isNull(); }
    bool isContentEditable() const { return m_editable; }
    QWebElement element() const { return m_element; }

private:
    QWebElement m_element;
    bool m_editable = false;

    friend class QWebFrame;
};

class QWebPage : public QObject
{
    Q_OBJECT
public:
    enum WebWindowType { WebBrowserWindow, WebModalDialog };
    enum NavigationType {
        NavigationTypeLinkClicked, NavigationTypeFormSubmitted, NavigationTypeBackOrForward,
        NavigationTypeReload, NavigationTypeFormResubmitted, NavigationTypeOther
    };
    enum WebAction { NoWebAction = -1, Cut, Copy, Paste, Undo, Redo, SelectAll };

    class ViewportAttributes
    {
    public:
        bool isValid() const { return m_valid; }
        qreal initialScaleFactor() const { return m_initialScale; }
        qreal minimumScaleFactor() const { return m_minimumScale; }
        qreal maximumScaleFactor() const { return m_maximumScale; }
        qreal devicePixelRatio() const { return m_devicePixelRatio; }
        bool isUserScalable() const { return m_userScalable; }
        QSizeF size() const { return m_size; }

    private:
        bool m_valid = false;
        qreal m_initialScale = 1.0;
        qreal m_minimumScale = 1.0;
        qreal m_maximumScale = 1.0;
        qreal m_devicePixelRatio = 1.0;
        bool m_userScalable = false;
        QSizeF m_size;

        friend class QWebPage;
    };

    explicit QWebPage(QObject* parent = nullptr);
    ~QWebPage() override;

    QWebFrame* mainFrame() const { return m_frame; }
    QWebSettings* settings() const { return m_settings; }

    void triggerAction(WebAction action, bool checked = false);

    QSize viewportSize() const { return m_viewportSize; }
    void setViewportSize(const QSize& size);
    // QtWebEngine does not expose a page's viewport meta tag; this returns the
    // attributes of a page without one, for the size asked.
    ViewportAttributes viewportAttributesForSize(const QSize& availableSize) const;

    QPalette palette() const { return m_palette; }
    // Only the Base brush's transparency is carried over: it is what WebAppMgr
    // sets, so a transparent app lets the card underneath show through.
    void setPalette(const QPalette& palette);

    // Input events go to the page, as QtWebKit's QWebPage::event() did.
    bool event(QEvent* event) override;

    QWebEnginePage* enginePage() const;
    QWebEngineView* engineView() const { return m_view; }

    // NOT QtWebKit API: an extension of this layer.
    //
    // A page painted inside another, at a rect the host chooses. It is what the
    // browser app needs where its <object type="application/x-palm-browser">
    // used to be: on a device that object was an NPAPI plugin blitting what
    // BrowserServer had painted in another process, and Chromium has no plugin
    // socket to put anything in. Here both engines are ours and in one process,
    // so the host's render() blits the embedded page's own pixels into the same
    // painter WebAppMgr aims at the shared buffer the shell reads.
    //
    // The embedded page is not owned. Its viewport is resized to the rect, and
    // each of its frames asks the host to repaint that rect.
    void embedPage(QWebPage* page, const QRect& rect);
    void removeEmbeddedPage(QWebPage* page);

Q_SIGNALS:
    void loadStarted();
    void loadProgress(int progress);
    void loadFinished(bool ok);
    void repaintRequested(const QRect& dirtyRect);
    void geometryChangeRequested(const QRect& geometry);
    void linkClicked(const QUrl& url);
    void viewportChangeRequested();
    void microFocusChanged();
    void windowCloseRequested();

protected:
    virtual QWebPage* createWindow(WebWindowType type);
    virtual bool acceptNavigationRequest(QWebFrame* frame, const QNetworkRequest& request, NavigationType type);
    virtual void javaScriptConsoleMessage(const QString& message, int lineNumber, const QString& sourceId);

private:
    class Engine;
    friend class Engine;
    friend class QWebFrame;

    void followRenderSurface();

    // Hands an event that carries a position to the page painted at that
    // position, in that page's own coordinates. Returns false when there is
    // none there, and the host keeps it.
    bool deliverToEmbedded(QEvent* event);

    Engine* m_engine;
    QWebEngineView* m_view;
    QWebFrame* m_frame;
    QWebSettings* m_settings;
    QSize m_viewportSize;
    QPalette m_palette;
    QPointer<QObject> m_renderSurface;

    // Held by pointer, never owned: a page embedded in this one, and where it
    // goes. A null page is one that was deleted from under us; render() steps
    // over those rather than making its owner remember to unregister.
    struct EmbeddedPage {
        QPointer<QWebPage> page;
        QRect rect;
        QMetaObject::Connection repaintLink;
    };
    QList<EmbeddedPage> m_embedded;

    // Which surface the keyboard belongs to: the last one pressed. Null is the
    // host's own page, which is where the app's address bar lives. Keys follow
    // a focus, not a position, so they cannot be routed the way touches are.
    QPointer<QWebPage> m_keyboardOwner;
};

class QWebFrame : public QObject
{
    Q_OBJECT
public:
    enum RenderLayer { ContentsLayer = 0x10, ScrollBarLayer = 0x20, PanIconLayer = 0x40, AllLayers = 0xff };

    QWebPage* page() const { return m_page; }

    QUrl url() const;
    void load(const QUrl& url);
    void setUrl(const QUrl& url) { load(url); }
    void setHtml(const QString& html, const QUrl& baseUrl = QUrl());
    QString title() const;

    void render(QPainter* painter, RenderLayer layer, const QRegion& clip = QRegion());
    void render(QPainter* painter, const QRegion& clip = QRegion()) { render(painter, AllLayers, clip); }

    void addToJavaScriptWindowObject(const QString& name, QObject* object);
    QVariant evaluateJavaScript(const QString& script);

    QWebElement findFirstElement(const QString& selectorQuery) const;
    QWebHitTestResult hitTestContent(const QPoint& pos) const;

    void setScrollBarPolicy(Qt::Orientation orientation, Qt::ScrollBarPolicy policy);

Q_SIGNALS:
    void javaScriptWindowObjectCleared();
    void titleChanged(const QString& title);
    void urlChanged(const QUrl& url);
    void contentsSizeChanged(const QSize& size);

private:
    explicit QWebFrame(QWebPage* page);

    // Called when a main-frame navigation has been accepted: lets the page's
    // client add its objects, and turns what it added into the script that runs
    // before the new document's own.
    void prepareNewDocument();
    QVariant evaluateAndWait(const QString& script) const;

    QWebPage* m_page;
    bool m_collecting = false;
    QString m_collected;

    friend class QWebPage;
};

class QGraphicsWebView : public QGraphicsWidget
{
    Q_OBJECT
public:
    explicit QGraphicsWebView(QGraphicsItem* parent = nullptr);
    ~QGraphicsWebView() override;

    QWebPage* page() const { return m_page; }
    void setPage(QWebPage* page);

    bool resizesToContents() const { return m_resizesToContents; }
    void setResizesToContents(bool enabled) { m_resizesToContents = enabled; }

    void setGeometry(const QRectF& rect) override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget = nullptr) override;

private:
    QPointer<QWebPage> m_page;
    bool m_resizesToContents = false;
};

#endif // QTWEBKIT_COMPAT_H
