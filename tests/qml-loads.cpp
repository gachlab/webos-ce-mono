// Every .qml file in a tree must load under QtQuick 2.
//
// The shell's QML was written for QML 1 ("import Qt 4.7"), a module Qt 5
// removed. Every file failed to load, and the only thing the shell logged was
// "QQmlComponent: Component is not ready" -- so the UI silently did not draw
// and nothing said why. This turns that into something a build can check.
//
// Two of the files import modules that luna-sysmgr registers from C++
// (SystemMenu 1.0, CustomComponents 1.0). Minimal QQuickItem stubs stand in for
// them: the point here is whether the QML is valid, not whether those C++ types
// behave -- QmlSceneItem's test covers the C++ side.
//
//   ./qml-loads-qt5 -platform offscreen <dir> [<dir>...]
#include <QGuiApplication>
#include <QDirIterator>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <cstdio>

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

    QStringList roots;
    for (int i = 1; i < argc; ++i) {
        QString a = QString::fromLocal8Bit(argv[i]);
        if (!a.startsWith('-') && a != QLatin1String("offscreen"))
            roots << a;
    }
    if (roots.isEmpty()) {
        fprintf(stderr, "usage: qml-loads-qt5 -platform offscreen <dir>...\n");
        return 2;
    }

    qmlRegisterType<StubItem>("SystemMenu", 1, 0, "AnimatedSpinner");
    qmlRegisterType<StubItem>("CustomComponents", 1, 0, "InputItem");

    QQmlEngine engine;
    int total = 0, failed = 0;

    for (const QString &root : roots) {
        QDirIterator it(root, QStringList() << QStringLiteral("*.qml"),
                        QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString path = it.next();
            ++total;
            QQmlComponent c(&engine, QUrl::fromLocalFile(path));
            if (c.isError()) {
                ++failed;
                printf("FAIL %s\n", qPrintable(path));
                for (const QQmlError &e : c.errors())
                    printf("       %s\n", qPrintable(e.toString()));
            }
        }
    }

    if (total == 0) {
        printf("FAIL: no .qml files found -- the paths are wrong\n");
        return 1;
    }
    printf("%d/%d QML files load\n", total - failed, total);
    printf("%s\n", failed == 0 ? "OK" : "FAIL");
    return failed == 0 ? 0 : 1;
}

#include "qml-loads.moc"
