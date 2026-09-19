// NetworkAppProxy's Luna subscriptions. Apart from the adapter so its tests
// need no luna-service2.

#include "NetworkAppProxyAdapter.h"

#include <memory>

#include <QJsonDocument>

#include <glib.h>
#include <lunaservice.h>

namespace NetworkAppProxy {

namespace {

const char kStatusUri[] = "palm://com.palm.connectionmanager/getstatus";
const char kProxiesUri[] = "palm://com.palm.connectionmanager/getNwProxiesConfig";

struct State {
    LSHandle* handle = nullptr;
    LSMessageToken statusToken = LSMESSAGE_TOKEN_INVALID;
    LSMessageToken proxiesToken = LSMESSAGE_TOKEN_INVALID;
    int wifiProfileId = 0;
    std::vector<NmNet::ProxyInfo> proxies;
};

State& stateSlot()
{
    static State state;
    return state;
}

void refresh()
{
    const State& state = stateSlot();
    consider(state.wifiProfileId, state.proxies);
}

bool onStatus(LSHandle*, LSMessage* message, void*)
{
    const QJsonDocument document = QJsonDocument::fromJson(QByteArray(LSMessageGetPayload(message)));
    stateSlot().wifiProfileId = wifiProfileIdOf(document.object());
    refresh();
    return true;
}

bool onProxies(LSHandle*, LSMessage* message, void*)
{
    const QJsonDocument document = QJsonDocument::fromJson(QByteArray(LSMessageGetPayload(message)));
    stateSlot().proxies = proxiesOf(document.object());
    refresh();
    return true;
}

} // namespace

void install(LSHandle* handle)
{
    setApply(applyWithQt);
    reset();
    State& state = stateSlot();
    state = State{};
    state.handle = handle;
    if (!handle)
        return;

    LSError error;
    LSErrorInit(&error);
    if (!LSCall(handle, kStatusUri, "{\"subscribe\":true}", onStatus, nullptr, &state.statusToken,
                &error)) {
        g_warning("NetworkAppProxy: getstatus: %s", error.message);
        LSErrorFree(&error);
    }
    LSErrorInit(&error);
    if (!LSCall(handle, kProxiesUri, "{\"subscribe\":true}", onProxies, nullptr, &state.proxiesToken,
                &error)) {
        g_warning("NetworkAppProxy: getNwProxiesConfig: %s", error.message);
        LSErrorFree(&error);
    }
}

} // namespace NetworkAppProxy
