// Calls every function a .qml declares and fails on an undefined identifier.
//
// Loading a QML file only proves it parses. A typo inside a function body --
// HP's `if(inProgress)` where the property is `airplaneModeInProgress` -- costs
// nothing at load and throws the moment the function runs, aborting it. The
// shell logs one line nobody reads and the feature silently does half its job.
//
// The four names luna-sysmgr injects as context properties are stubbed, or
// every reference to them would look like the same bug. Anything else that
// comes back "is not defined" is a real one.
//
// Arguments matter, which is not obvious. Passing default-constructed QVariants
// looks harmless -- an undefined identifier is undefined whatever the arguments
// are -- but an undefined argument makes an earlier line throw first and the
// function never reaches the bug. SystemMenu.setAirplaneModeStatus is exactly
// that: `airplane.modeText = newText` throws on undefined, two lines above the
// `inProgress` typo. So each function is called several times with different
// plausible fillers and everything they say is collected.
//
// Lines behind a condition that stays false are still not reached, so this is a
// floor on coverage, not a ceiling.
//
//   ./qml-functions-qt5 -platform offscreen <file.qml> [<file.qml>...]
#include <QGuiApplication>
#include <QMetaMethod>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QStringList>
#include <cstdio>

static QStringList g_messages;

static void collect(QtMsgType, const QMessageLogContext &, const QString &msg)
{
    g_messages << msg;
}

// Answers anything without complaining, so a reference to a context property
// resolves and any method call on it returns undefined rather than throwing.
class PermissiveStub : public QObject
{
    Q_OBJECT
public:
    Q_INVOKABLE QVariant getLocalizedString(const QString &s) const { return s; }
};

// Stands in for the types luna-sysmgr registers from C++. QQuickItem itself
// cannot be registered -- qmlRegisterType refuses it -- and the file then fails
// to load, which would quietly skip everything in it.
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

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);

    QStringList files;
    for (int i = 1; i < argc; ++i) {
        QString a = QString::fromLocal8Bit(argv[i]);
        if (a.endsWith(QLatin1String(".qml")))
            files << a;
    }
    if (files.isEmpty()) {
        fprintf(stderr, "usage: qml-functions-qt5 -platform offscreen <file.qml>...\n");
        return 2;
    }

    qmlRegisterType<StubItem>("SystemMenu", 1, 0, "AnimatedSpinner");
    qmlRegisterType<StubItem>("CustomComponents", 1, 0, "InputItem");

    // Missing artwork and deprecation notices would bury the one line that
    // matters, so everything is collected and only the relevant part printed.
    qInstallMessageHandler(collect);

    QQmlEngine engine;
    PermissiveStub stub;
    const char *contextProperties[] = {
        "runtime", "NativeSystemMenuHandler",
        "DashboardContainer", "DockModeAppMenuContainer"
    };
    for (const char *name : contextProperties)
        engine.rootContext()->setContextProperty(name, &stub);

    int calls = 0, bad = 0;

    for (const QString &file : files) {
        QQmlComponent component(&engine, QUrl::fromLocalFile(file));
        if (component.isError()) {
            printf("FAIL %s does not load:\n", qPrintable(file));
            for (const QQmlError &e : component.errors())
                printf("       %s\n", qPrintable(e.toString()));
            ++bad;
            continue;
        }

        QObject *root = component.create();
        if (!root) {
            printf("FAIL %s produced no object\n", qPrintable(file));
            ++bad;
            continue;
        }

        const QMetaObject *mo = root->metaObject();
        for (int i = mo->methodOffset(); i < mo->methodCount(); ++i) {
            const QMetaMethod m = mo->method(i);
            if (m.methodType() != QMetaMethod::Method && m.methodType() != QMetaMethod::Slot)
                continue;
            if (m.parameterCount() > 5)
                continue;

            g_messages.clear();

            static const QVariant kFillers[] = {
                QVariant(1), QVariant(QStringLiteral("1")), QVariant(true), QVariant(0)
            };
            for (const QVariant &filler : kFillers) {
                QGenericArgument a[5];
                QVariant args[5];
                for (int p = 0; p < m.parameterCount(); ++p) {
                    args[p] = filler;
                    a[p] = QGenericArgument("QVariant", &args[p]);
                }
                m.invoke(root, Qt::DirectConnection, a[0], a[1], a[2], a[3], a[4]);
            }

            ++calls;

            // Everything a function said, not just the undefined identifiers.
            // Useful when a function throws before reaching the line you care
            // about, which is how the inProgress bug hid from this test at first.
            if (qEnvironmentVariableIsSet("QML_FUNCTIONS_VERBOSE"))
                for (const QString &said : g_messages)
                    printf("    [%s] %s\n", m.name().constData(), qPrintable(said));
            QStringList reported;
            for (const QString &msg : g_messages) {
                if (!msg.contains(QLatin1String("is not defined")) || reported.contains(msg))
                    continue;
                reported << msg;
                printf("FAIL %s :: %s()\n", qPrintable(file),
                       QString::fromLatin1(m.name()).toUtf8().constData());
                printf("       %s\n", qPrintable(msg));
                ++bad;
            }
        }
        delete root;
    }

    printf("%d functions called across %d files, %d undefined identifiers\n",
           calls, files.size(), bad);
    printf("%s\n", bad == 0 ? "OK" : "FAIL");
    return bad == 0 ? 0 : 1;
}

#include "qml-functions.moc"
