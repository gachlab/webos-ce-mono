#include "bridge-scheme.h"

#include "wpewebkit_compat.h"
#include "wpe_webcontent.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaMethod>
#include <QMetaProperty>
#include <QPointer>
#include <QSet>
#include <QUrlQuery>

namespace wpewebkit_compat {
namespace bridge {

// The last features string each page announced, by page number, until the
// window it was meant for is created.
QHash<int, QString>& pendingWindowFeatures()
{
    static QHash<int, QString> table;
    return table;
}

// ---------------------------------------------------------------------------
// The C++ side: which QObject each id is, and what JavaScript may reach.

struct Published
{
    QPointer<QObject> object;
    QPointer<QWebPage> page;   // where its signals are delivered
};

QHash<int, Published>& published()
{
    static QHash<int, Published> table;
    return table;
}

int publishObject(QObject* object, QWebPage* page);

bool isOwnMember(const QMetaObject* meta, int index, bool method)
{
    // QObject's own members (destroyed, deleteLater, objectName...) stay out.
    const int offset = method ? QObject::staticMetaObject.methodCount()
                              : QObject::staticMetaObject.propertyCount();
    Q_UNUSED(meta);
    return index >= offset;
}

QJsonObject describe(const QMetaObject* meta)
{
    QJsonArray properties, methods, signalNames;
    for (int i = 0; i < meta->propertyCount(); ++i)
        if (isOwnMember(meta, i, false))
            properties.append(QString::fromLatin1(meta->property(i).name()));
    QSet<QString> seen;
    for (int i = 0; i < meta->methodCount(); ++i) {
        if (!isOwnMember(meta, i, true))
            continue;
        const QMetaMethod m = meta->method(i);
        if (m.access() != QMetaMethod::Public)
            continue;
        const QString name = QString::fromLatin1(m.name());
        if (seen.contains(name))
            continue;
        seen.insert(name);
        if (m.methodType() == QMetaMethod::Signal)
            signalNames.append(name);
        else
            methods.append(name);
    }
    return QJsonObject{{"properties", properties}, {"methods", methods}, {"signals", signalNames}};
}

QJsonValue toJson(const QVariant& value, QWebPage* page)
{
    if (value.metaType().flags() & QMetaType::PointerToQObject) {
        QObject* object = value.value<QObject*>();
        if (!object)
            return QJsonValue::Null;
        return QJsonObject{{"__webosObject", publishObject(object, page)},
                           {"meta", describe(object->metaObject())}};
    }
    if (!value.isValid())
        return QJsonValue::Null;
    return QJsonValue::fromVariant(value);
}

void runJavaScript(QWebPage* page, const QString& script)
{
    if (page && page->mainFrame())
        page->mainFrame()->evaluateJavaScript(script);
}

// Receives one signal of one object and hands its arguments to JavaScript. It
// is QSignalSpy's technique: connect to a method index just past QObject's own
// and catch the call in qt_metacall.
class SignalRelay : public QObject
{
public:
    SignalRelay(QObject* sender, const QMetaMethod& signal, int id, QWebPage* page)
        : QObject(sender), m_signal(signal), m_id(id), m_page(page)
    {
        QMetaObject::connect(sender, signal.methodIndex(), this,
                             QObject::staticMetaObject.methodCount(), Qt::DirectConnection);
    }

    int qt_metacall(QMetaObject::Call call, int methodId, void** argv) override
    {
        methodId = QObject::qt_metacall(call, methodId, argv);
        if (methodId < 0)
            return methodId;
        if (call == QMetaObject::InvokeMetaMethod) {
            if (methodId == 0)
                deliver(argv);
            --methodId;
        }
        return methodId;
    }

private:
    void deliver(void** argv)
    {
        if (!m_page)
            return;
        QJsonArray args;
        for (int i = 0; i < m_signal.parameterCount(); ++i)
            args.append(toJson(QVariant(QMetaType(m_signal.parameterType(i)), argv[i + 1]), m_page));
        const QString script = QString("window.__webosBridge && window.__webosBridge.emit(%1, %2, %3);")
            .arg(m_id)
            .arg(QString::fromUtf8(QJsonDocument(QJsonArray{QString::fromLatin1(m_signal.name())}).toJson(QJsonDocument::Compact)).mid(1).chopped(1))
            .arg(QString::fromUtf8(QJsonDocument(args).toJson(QJsonDocument::Compact)));
        runJavaScript(m_page, script);
    }

    QMetaMethod m_signal;
    int m_id;
    QPointer<QWebPage> m_page;
};

int publishObject(QObject* object, QWebPage* page)
{
    static int nextId = 1;
    for (auto it = published().constBegin(); it != published().constEnd(); ++it)
        if (it.value().object == object)
            return it.key();

    const int id = nextId++;
    published().insert(id, Published{object, page});
    QObject::connect(object, &QObject::destroyed, [id]() { published().remove(id); });

    const QMetaObject* meta = object->metaObject();
    for (int i = QObject::staticMetaObject.methodCount(); i < meta->methodCount(); ++i) {
        const QMetaMethod m = meta->method(i);
        if (m.methodType() == QMetaMethod::Signal && m.access() == QMetaMethod::Public)
            new SignalRelay(object, m, id, page);
    }
    return id;
}

QByteArray replyValue(const QJsonValue& value)
{
    return QJsonDocument(QJsonObject{{"v", value}}).toJson(QJsonDocument::Compact);
}

QByteArray replyError(const QString& message)
{
    return QJsonDocument(QJsonObject{{"e", message}}).toJson(QJsonDocument::Compact);
}

QByteArray invoke(QObject* object, QWebPage* page, const QString& name, const QJsonArray& args)
{
    const QMetaObject* meta = object->metaObject();
    for (int i = QObject::staticMetaObject.methodCount(); i < meta->methodCount(); ++i) {
        const QMetaMethod m = meta->method(i);
        if (m.methodType() == QMetaMethod::Signal || m.access() != QMetaMethod::Public)
            continue;
        if (QString::fromLatin1(m.name()) != name || m.parameterCount() != args.size())
            continue;
        if (args.size() > 10)
            return replyError(name + ": too many arguments");

        // Arguments, converted to what the method declares.
        QList<QVariant> values;
        values.reserve(args.size());
        for (int a = 0; a < args.size(); ++a) {
            QVariant v = args.at(a).toVariant();
            const QMetaType type(m.parameterType(a));
            if (type.id() != QMetaType::QVariant && !v.convert(type))
                v = QVariant(type);
            values.append(v);
        }
        QGenericArgument generic[10];
        for (int a = 0; a < values.size(); ++a) {
            const QMetaType type(m.parameterType(a));
            generic[a] = type.id() == QMetaType::QVariant
                ? QGenericArgument("QVariant", &values[a])
                : QGenericArgument(type.name(), values[a].constData());
        }

        QVariant result;
        bool ok;
        if (m.returnType() == QMetaType::Void) {
            ok = m.invoke(object, Qt::DirectConnection, generic[0], generic[1], generic[2], generic[3],
                          generic[4], generic[5], generic[6], generic[7], generic[8], generic[9]);
        } else {
            const QMetaType returnType(m.returnType());
            result = QVariant(returnType);
            ok = m.invoke(object, Qt::DirectConnection,
                          returnType.id() == QMetaType::QVariant
                              ? QGenericReturnArgument("QVariant", &result)
                              : QGenericReturnArgument(returnType.name(), result.data()),
                          generic[0], generic[1], generic[2], generic[3], generic[4],
                          generic[5], generic[6], generic[7], generic[8], generic[9]);
        }
        if (!ok)
            return replyError(name + ": the call failed");
        return replyValue(toJson(result, page));
    }
    return replyError(QString("%1 has no method %2 taking %3 arguments")
                          .arg(QString::fromLatin1(meta->className()), name).arg(args.size()));
}

// webos-bridge:///<id>/<get|set|call>/<name>?a=<JSON array>
// webos-bridge:///window/<page>?a=[<features>]
// webos-bridge:///fetch?u=<url>  (day one: not implemented)
QByteArray handleBridgeRequest(const QUrl& url)
{
    const QStringList parts = url.path().split('/', Qt::SkipEmptyParts);
    const QJsonArray args = QJsonDocument::fromJson(
        QUrlQuery(url).queryItemValue("a", QUrl::FullyDecoded).toUtf8()).array();

    if (parts.size() == 1 && parts[0] == QLatin1String("fetch"))
        return replyError(QStringLiteral("fetch not implemented on WPE yet"));

    if (parts.size() == 2 && parts[0] == QLatin1String("window")) {
        bool ok = false;
        const int page = parts[1].toInt(&ok);
        if (ok && pendingWindowFeatures().contains(page)) {
            pendingWindowFeatures()[page] = args.at(0).toString();
            return replyValue(QJsonValue::Null);
        }
        return replyError("no such page");
    }

    const Published entry = parts.size() == 3 ? published().value(parts[0].toInt()) : Published{};
    if (!entry.object)
        return replyError("no such object");

    QObject* object = entry.object;
    const QString op = parts[1];
    const QString name = QUrl::fromPercentEncoding(parts[2].toUtf8());
    const int propertyIndex = object->metaObject()->indexOfProperty(name.toLatin1());
    if (op == "get" && propertyIndex >= 0)
        return replyValue(toJson(object->metaObject()->property(propertyIndex).read(object), entry.page));
    if (op == "set" && propertyIndex >= 0 && !args.isEmpty()) {
        object->metaObject()->property(propertyIndex).write(object, args.at(0).toVariant());
        return replyValue(QJsonValue::Null);
    }
    if (op == "call")
        return invoke(object, entry.page, name, args);
    return replyError(op + " " + name + ": not available");
}

void ensureRegistered()
{
    static bool registered = false;
    if (registered)
        return;
    registered = true;
    wpe_webcontent::HeadlessView::setBridgeSchemeHandler(
        [](const QUrl& url, QString* contentType) -> QByteArray {
            if (contentType)
                *contentType = QStringLiteral("application/json");
            return handleBridgeRequest(url);
        });
}

} // namespace bridge
} // namespace wpewebkit_compat
