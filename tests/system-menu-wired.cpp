// The cable's row in the system menu, tapped twice.
//
// A tap sets delayUpdate so the row does not flicker back to the old state
// before NetworkManager answers; whatever arrives meanwhile is parked and
// applied when the menu closes, in updateChangedFields(). The row was left out
// of that function, so after the first tap it never updated again: the icon did
// not dim, and the second tap asked to disconnect a second time instead of
// connecting. Found live, against a fake NetworkManager with a cable in.
//
// A second bug hid behind the first: in this port the menu is hosted by
// QmlSceneItem, and hiding the host's parent -- which is how the shell closes
// the menu -- never reached the QML, so onVisibleChanged did not run either.
//
// So this hosts SystemMenu.qml the way the shell does, in a QmlSceneItem under
// a parent that stands in for SystemMenu.cpp, and drives it the same way:
// setWiredStatus() from C++, action() from the row, and the parent hidden and
// shown for the menu closing and opening.
//
//   ./system-menu-wired -platform offscreen <SystemMenu.qml>
#include "QmlSceneItem.h"

#include <QApplication>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <cstdio>

static int g_failures = 0;

static void check(bool ok, const char *what)
{
    std::printf("  %-66s %s\n", what, ok ? "OK" : "<-- FAIL");
    if (!ok)
        ++g_failures;
}

static void quiet(QtMsgType, const QMessageLogContext &, const QString &) {}

class PermissiveStub : public QObject
{
    Q_OBJECT
public:
    Q_INVOKABLE QVariant getLocalizedString(const QString &s) const { return s; }
};

// The types luna-sysmgr registers from C++, as in qml-functions.
class StubItem : public QQuickItem
{
    Q_OBJECT
    Q_PROPERTY(int duration READ duration WRITE setDuration)
    Q_PROPERTY(bool on READ on WRITE setOn)
public:
    int duration() const { return m_duration; }
    void setDuration(int d) { m_duration = d; }
    bool on() const { return m_on; }
    void setOn(bool o) { m_on = o; }
private:
    int m_duration = 0;
    bool m_on = false;
};

// What SystemMenu.cpp receives from wiredToggleTriggered.
class Recorder : public QObject
{
    Q_OBJECT
public:
    QList<bool> asked;
public slots:
    void toggled(bool isConnected) { asked << isConnected; }
};

static void setStatus(QObject *menu, const char *text, bool connected)
{
    QMetaObject::invokeMethod(menu, "setWiredStatus",
                              Q_ARG(QVariant, QString::fromUtf8(text)),
                              Q_ARG(QVariant, connected),
                              Q_ARG(QVariant, true),
                              Q_ARG(QVariant, true));
}

static void tap(QObject *row)
{
    QMetaObject::invokeMethod(row, "action");
}

// The menu closing and opening again, as StatusBarItemGroup does it: on the
// SystemMenu graphics object, the host's parent, never on the QML itself.
static void closeAndReopen(QGraphicsItem *systemMenu)
{
    systemMenu->setVisible(false);
    QCoreApplication::processEvents();
    systemMenu->setVisible(true);
    QCoreApplication::processEvents();
}

int main(int argc, char **argv)
{
    QmlSceneItem::setUpSoftwareBackend();
    QApplication app(argc, argv);
    if (argc < 2) {
        std::fprintf(stderr, "usage: system-menu-wired -platform offscreen <SystemMenu.qml>\n");
        return 2;
    }
    const QString file = QString::fromLocal8Bit(argv[argc - 1]);

    qmlRegisterType<StubItem>("SystemMenu", 1, 0, "AnimatedSpinner");
    qmlRegisterType<StubItem>("CustomComponents", 1, 0, "InputItem");
    // Missing artwork under /usr/palm would otherwise fill the output.
    qInstallMessageHandler(quiet);

    QQmlEngine engine;
    PermissiveStub stub;
    for (const char *name : { "runtime", "NativeSystemMenuHandler",
                              "DashboardContainer", "DockModeAppMenuContainer" })
        engine.rootContext()->setContextProperty(name, &stub);

    QQmlComponent component(&engine, QUrl::fromLocalFile(file));
    QGraphicsScene scene;
    QGraphicsRectItem *systemMenu = new QGraphicsRectItem(0, 0, 320, 480);
    scene.addItem(systemMenu);
    QmlSceneItem *host = component.isError() ? nullptr : new QmlSceneItem(&component, systemMenu);
    QObject *menu = host ? host->rootItem() : nullptr;
    if (!menu) {
        qInstallMessageHandler(nullptr);
        std::printf("FAIL %s does not load: %s\n", qPrintable(file),
                    qPrintable(component.errorString()));
        return 1;
    }
    QObject *row = menu->findChild<QObject *>(QStringLiteral("wiredMenu"));
    if (!row) {
        std::printf("FAIL no object named wiredMenu in %s\n", qPrintable(file));
        return 1;
    }
    Recorder recorder;
    QObject::connect(menu, SIGNAL(wiredToggleTriggered(bool)), &recorder, SLOT(toggled(bool)));

    std::printf("cable in and connected\n");
    setStatus(menu, "10.20.30.40", true);
    check(row->property("visible").toBool(), "the row is shown");
    check(row->property("connected").toBool(), "as connected");
    check(row->property("statusText").toString() == "10.20.30.40", "with the address");

    std::printf("first tap\n");
    tap(row);
    check(recorder.asked == QList<bool>{ true }, "it reports the row as connected, so it disconnects");
    setStatus(menu, "Not connected", false);
    check(row->property("connected").toBool(), "the answer waits while the menu is open");
    closeAndReopen(systemMenu);
    check(!row->property("connected").toBool(), "and is shown once the menu closes");
    check(row->property("statusText").toString() == "Not connected", "text included");

    std::printf("second tap\n");
    tap(row);
    check(recorder.asked == (QList<bool>{ true, false }), "it reports disconnected, so it connects");
    setStatus(menu, "10.20.30.40", true);
    closeAndReopen(systemMenu);
    check(row->property("connected").toBool(), "connected again after the menu closes");
    check(row->property("statusText").toString() == "10.20.30.40", "with the address back");

    std::printf("no tap in between\n");
    setStatus(menu, "Not connected", false);
    check(!row->property("connected").toBool(), "a change from elsewhere shows at once");

    std::printf("\n%s\n", g_failures == 0 ? "OK" : "FAILED");
    return g_failures == 0 ? 0 : 1;
}

#include "system-menu-wired.moc"
