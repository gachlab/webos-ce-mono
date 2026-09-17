/*
 * BrowserView adapter.
 *
 * What the browser app's content area used to be, rebuilt on QtWebEngine.
 *
 * HP's browser drew its chrome -- address bar, bookmarks, history -- as an
 * ordinary enyo page and left the content to
 * <object type="application/x-palm-browser">. That object was an NPAPI plugin,
 * BrowserAdapter (13k lines), blitting a buffer that a second process,
 * BrowserServer (16k lines), had painted with its own WebKit. Chromium removed
 * NPAPI in 2015: nothing can be registered for that mime type any more, so the
 * object is inert and the content area comes up blank.
 *
 * Almost none of those 29k lines need replacing. QtWebEngine already is the
 * separate-process web engine BrowserServer was, and the layer under us already
 * paints one page inside another (QWebPage::embedPage). What was missing is
 * this: an object the app can hold, which owns a real page, puts it where the
 * app's hole is, and answers the handful of commands the app actually sends.
 *
 * Measured surface, not guessed: isis-browser touches its view in nine places,
 * all in Browser.js -- callBrowserAdapter five times, plus setUrl,
 * setIdentifier, setZoom, getZoom and resize -- and of the 23 verbs
 * callBrowserAdapter accepts it uses five: goBack, goForward, reloadPage,
 * stopLoad and findInPage. Every one has a direct counterpart in
 * QWebEnginePage, which is why this file is short.
 *
 * The signals are the other half: the chrome only updates its title, its
 * progress bar and its back/forward buttons if the view tells it to. They
 * reach JavaScript through the same bridge PalmSystem uses.
 */
#ifndef BROWSERVIEWADAPTER_H
#define BROWSERVIEWADAPTER_H

#include <QObject>
#include <QPointer>
#include <QRect>
#include <QString>
#include <QVariantList>

class QWebPage;

// One embedded page per "new BrowserView()" on the JS side.
class BrowserViewAdapter : public QObject {
    Q_OBJECT
public:
    explicit BrowserViewAdapter(QWebPage* host, QObject* parent = 0);
    virtual ~BrowserViewAdapter();

    // Where the app's hole is, in the host page's coordinates. Called again
    // whenever it moves or resizes: the hole travels with the page that owns it.
    Q_INVOKABLE void setGeometry(int x, int y, int width, int height);
    // What the app has over the view, as [x, y, width, height] rects in the
    // host page's coordinates: the page is not painted there.
    Q_INVOKABLE void setCutouts(const QVariantList& rects);

    Q_INVOKABLE void setUrl(const QString& url);
    Q_INVOKABLE QString url() const;

    // The five verbs isis-browser sends through callBrowserAdapter.
    Q_INVOKABLE void goBack();
    Q_INVOKABLE void goForward();
    Q_INVOKABLE void reload();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void findInPage(const QString& text);

    Q_INVOKABLE bool canGoBack() const;
    Q_INVOKABLE bool canGoForward() const;

    // The browser's preferences, which BasicWebView sends when the view
    // connects and whenever the user changes one.
    Q_INVOKABLE void setEnableJavaScript(bool enable);
    Q_INVOKABLE void setBlockPopups(bool block);
    Q_INVOKABLE bool blocksPopups() const;
    // For every web page the browser shows: cookies belong to the profile all
    // pages share, so this is not per view (see the .cpp).
    Q_INVOKABLE void setAcceptCookies(bool accept);
    Q_INVOKABLE bool acceptsCookies() const;

    Q_INVOKABLE void setZoom(double factor);
    Q_INVOKABLE double zoom() const;

    // The app is done with it. Not left to the destructor: a card can outlive
    // the view it showed.
    Q_INVOKABLE void close();

Q_SIGNALS:
    void loadStarted();
    void loadProgress(int progress);
    void loadFinished(bool ok);
    void titleChanged(const QString& title);
    void urlChanged(const QString& url);
    // Something the page would download rather than show: what the browser
    // plugin reported as mimeNotSupported, which the app downloads through
    // com.palm.downloadmanager.
    void fileRequested(const QString& mimeType, const QString& url);

private:
    QPointer<QWebPage> m_host;
    QWebPage* m_view;
    QRect m_rect;

    // Fullscreen video. The page asks through QWebEnginePage's
    // fullScreenRequested, which nobody was answering, so the button did
    // nothing. Answering it means growing the hole to the whole card and
    // putting it back afterwards -- and ignoring the geometry the app keeps
    // reporting meanwhile, which is the size of its ordinary content area.
    QRect m_rectBeforeFullScreen;
    bool m_fullScreen;
};

// addToJavaScriptWindowObject publishes an INSTANCE, not a constructor, and the
// app wants one view per content area. Hence the factory, as with
// PalmServiceBridgeFactory: the JS shim calls it when it renders its hole.
class BrowserViewFactory : public QObject {
    Q_OBJECT
public:
    explicit BrowserViewFactory(QWebPage* host, QObject* parent = 0);
    Q_INVOKABLE QObject* create();

private:
    QPointer<QWebPage> m_host;
};

#endif // BROWSERVIEWADAPTER_H
