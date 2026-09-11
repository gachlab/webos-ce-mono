// Loads a QML file and prints why it fails. LunaSysMgr only logs Qt's
// "QQmlComponent: Component is not ready", which says nothing useful.
#include <QGuiApplication>
#include <QQmlEngine>
#include <QQmlComponent>
#include <QUrl>
#include <cstdio>
int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);
    if (argc < 2) { printf("usage: qmlcheck <file.qml>\n"); return 2; }
    QQmlEngine engine;
    QQmlComponent c(&engine, QUrl::fromLocalFile(argv[1]));
    if (c.isError()) {
        for (const QQmlError& e : c.errors())
            printf("%s\n", qPrintable(e.toString()));
        return 1;
    }
    printf("OK: %s loads\n", argv[1]);
    return 0;
}
