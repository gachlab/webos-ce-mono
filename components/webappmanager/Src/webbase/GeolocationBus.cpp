// Geolocation's calls, on the bus. Apart from GeolocationAdapter.cpp so that
// its tests need no luna-service2.

#include "GeolocationAdapter.h"

#include <memory>

#include <QJsonDocument>

#include <glib.h>
#include <lunaservice.h>

namespace Geolocation {

namespace {

const char kService[] = "palm://com.palm.location/";

struct Pending {
    Reply reply;
    LSHandle* handle = nullptr;
    LSMessageToken token = LSMESSAGE_TOKEN_INVALID;
    bool oneReply = true;
    bool finished = false;
};

// The context luna-service2 carries: one reference, dropped when the call is
// over (its one reply, or its cancel).
using Context = std::shared_ptr<Pending>;

bool onReply(LSHandle*, LSMessage* message, void* ctx)
{
    const Context pending = *static_cast<Context*>(ctx);
    if (pending->finished)
        return true;
    if (pending->oneReply) {
        pending->finished = true;
        delete static_cast<Context*>(ctx);
    }
    const QJsonDocument document = QJsonDocument::fromJson(QByteArray(LSMessageGetPayload(message)));
    pending->reply(document.object());
    return true;
}

} // namespace

Call busCall(LSHandle* handle)
{
    return [handle](const QString& method, const QJsonObject& payload, Reply reply) -> std::function<void()> {
        auto pending = std::make_shared<Pending>();
        pending->reply = std::move(reply);
        pending->handle = handle;
        pending->oneReply = !payload.value("subscribe").toBool(false);
        auto* context = new Context(pending);
        const QByteArray uri = QByteArray(kService) + method.toUtf8();
        const QByteArray body = QJsonDocument(payload).toJson(QJsonDocument::Compact);
        LSError error;
        LSErrorInit(&error);
        const bool sent = pending->oneReply
            ? LSCallOneReply(handle, uri.constData(), body.constData(), onReply, context, &pending->token, &error)
            : LSCall(handle, uri.constData(), body.constData(), onReply, context, &pending->token, &error);
        if (!sent) {
            g_warning("Geolocation: %s: %s", uri.constData(), error.message);
            LSErrorFree(&error);
            delete context;
            pending->finished = true;
            pending->reply(QJsonObject{{"returnValue", false}, {"errorCode", -1}});
            return [] {};
        }
        return [pending, context]() {
            if (pending->finished)
                return;
            pending->finished = true;
            LSError cancelError;
            LSErrorInit(&cancelError);
            if (!LSCallCancel(pending->handle, pending->token, &cancelError))
                LSErrorFree(&cancelError);
            delete context;
        };
    };
}

void install(LSHandle* handle)
{
    setDefault(busCall(handle));
}

} // namespace Geolocation
