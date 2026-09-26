// QtWebKit-shaped API on WPE WebKit + WPEPlatform headless (#81).
// Parallel to adapters/qtwebkit-compat (QtWebEngine). Enough for WebAppMgr to
// compile and run cards; browser embed/history/cookies are stubs where noted.

#ifndef WPEWEBKIT_COMPAT_H
#define WPEWEBKIT_COMPAT_H

#include <functional>

#include <QGraphicsWidget>
#include <QHash>
#include <QMap>
#include <QObject>
#include <QPalette>
#include <QPointer>
#include <QRect>
#include <QRegion>
#include <QSize>
#include <QString>
#include <QUrl>
#include <QVariant>

#include <memory>

class QEvent;
class QKeyEvent;
class QNetworkRequest;
class QPainter;
class QWebFrame;
class QWebPage;

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

    static QWebSettings* globalSettings();

    void setAttribute(WebAttribute attribute, bool on);
    bool testAttribute(WebAttribute attribute) const;
    void setFontFamily(FontFamily, const QString&) {}

    void setIconDatabasePath(const QString&) {}
    void setOfflineStoragePath(const QString&) {}
    void setOfflineWebApplicationCachePath(const QString&) {}
    void setLocalStoragePath(const QString&) {}

    QWebSettings() = default;

private:
    QHash<int, bool> m_attributes;
};

class QWebElement
{
public:
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
};

class QWebHitTestResult
{
public:
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

    void triggerAction(WebAction, bool = false) {}

    QSize viewportSize() const { return m_viewportSize; }
    void setViewportSize(const QSize& size);
    ViewportAttributes viewportAttributesForSize(const QSize& availableSize) const;

    QPalette palette() const { return m_palette; }
    void setPalette(const QPalette& palette);

    bool event(QEvent* event) override;

    // Extensions matching qtwebkit-compat (WebAppMgr needs these symbols).
    QString attributes() const { return m_attributes; }
    void sendKeyToHostPage(QKeyEvent* event);

    void embedPage(QWebPage* page, const QRect& rect);
    void removeEmbeddedPage(QWebPage* page);
    void setEmbeddedCutouts(QWebPage* page, const QRegion& cutouts);

    using GeolocationPolicy = std::function<void(const QUrl& origin, std::function<void(bool)> decide)>;
    static void setGeolocationPolicy(GeolocationPolicy policy);

    // No QWebEnginePage behind WPE. Returns nullptr; BrowserViewAdapter is
    // compiled without enginePage() calls when WEBOS_WEB_ENGINE_WPE is set.
    void* enginePage() const { return nullptr; }

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
    void downloadRequested(const QUrl& url, const QString& mimeType);

protected:
    virtual QWebPage* createWindow(WebWindowType);
    virtual bool acceptNavigationRequest(QWebFrame*, const QNetworkRequest&, NavigationType);
    virtual void javaScriptConsoleMessage(const QString&, int, const QString&);

private:
    friend class QWebFrame;

    void ensureView();
    void prepareNewDocument();

    // Engine before m_frame: parenting the frame delivers QEvent to this page
    // while later members are still unconstructed. Reading m_engine then is UB.
    struct Engine;
    std::unique_ptr<Engine> m_engine;

    QWebFrame* m_frame = nullptr;
    QWebSettings* m_settings = nullptr;
    QSize m_viewportSize{1024, 768};
    QPalette m_palette;
    bool m_transparent = false;
    QString m_attributes;
    int m_number = 0;

    struct EmbeddedPage {
        QPointer<QWebPage> page;
        QRect rect;
        QRegion cutouts;
    };
    QList<EmbeddedPage> m_embedded;
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

    void setScrollBarPolicy(Qt::Orientation, Qt::ScrollBarPolicy) {}

Q_SIGNALS:
    void javaScriptWindowObjectCleared();
    void titleChanged(const QString& title);
    void urlChanged(const QUrl& url);
    void contentsSizeChanged(const QSize& size);

private:
    explicit QWebFrame(QWebPage* page);
    void prepareNewDocument();

    QWebPage* m_page;
    QUrl m_url;
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

#endif // WPEWEBKIT_COMPAT_H
