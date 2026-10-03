// A thin command-line front for BtClient, so the dbusmock-driven integration
// test (tests/bluez-client.py) can exercise what the service reads from BlueZ
// and asks of it without re-implementing the GDBus client in Python.
//
// It connects to the bus named by DBUS_SYSTEM_BUS_ADDRESS -- the private bus the
// test's dbus-daemon prints -- so the host's real adapter is never touched. Each
// invocation runs one command and prints the resulting webOS payload (readState
// mapped through bluez_state.h) or an "OK <call>" / "FAIL <message>" line for a
// mutating call, then exits.
//
//   bluez-probe status                      -> trustedDevicesPayload
//   bluez-probe profstate <profile>         -> profileStatePayload
//   bluez-probe radio                        -> "powered <true|false>"
//   bluez-probe poweron | poweroff           -> setPowered
//   bluez-probe connect <addr> <profile>     -> connectProfile
//   bluez-probe disconnect <addr> <profile>  -> disconnectProfile
//   bluez-probe pair <addr> | removedev <addr> | trust <addr>
//
// This is test-only; it is not installed and the service does not use it.
#include "bluez_client.h"
#include "bluez_state.h"
#include "bluez_agent.h"

#include <gio/gio.h>

#include <cstdio>
#include <cstring>
#include <string>

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::fprintf(stderr, "usage: bluez-probe <command> [args]\n");
        return 2;
    }

    GError* error = nullptr;
    GDBusConnection* bus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, nullptr, &error);
    if (!bus) {
        std::fprintf(stderr, "FAIL no bus: %s\n", error ? error->message : "?");
        g_clear_error(&error);
        return 1;
    }

    const std::string cmd = argv[1];
    const auto arg = [&](int i) -> std::string {
        return i < argc ? std::string(argv[i]) : std::string();
    };

    int rc = 0;
    std::string message;

    if (cmd == "status") {
        BtState::BluetoothState state = BtClient::readState(bus);
        std::printf("%s\n", BtState::trustedDevicesPayload(state).c_str());
    } else if (cmd == "profstate") {
        BtState::BluetoothState state = BtClient::readState(bus);
        std::printf("%s\n", BtState::profileStatePayload(state, arg(2).empty() ? "all" : arg(2)).c_str());
    } else if (cmd == "radio") {
        BtState::BluetoothState state = BtClient::readState(bus);
        std::printf("powered %s\n", state.adapter.powered ? "true" : "false");
    } else if (cmd == "poweron") {
        rc = BtClient::setPowered(bus, true, false, true, message) ? 0 : 1;
    } else if (cmd == "poweroff") {
        rc = BtClient::setPowered(bus, false, false, false, message) ? 0 : 1;
    } else if (cmd == "connect") {
        rc = BtClient::connectProfile(bus, arg(2), arg(3), message) ? 0 : 1;
    } else if (cmd == "disconnect") {
        rc = BtClient::disconnectProfile(bus, arg(2), arg(3), message) ? 0 : 1;
    } else if (cmd == "pair") {
        rc = BtClient::pairDevice(bus, arg(2), message) ? 0 : 1;
    } else if (cmd == "removedev") {
        rc = BtClient::removeDevice(bus, arg(2), message) ? 0 : 1;
    } else if (cmd == "trust") {
        rc = BtClient::setTrusted(bus, arg(2), true, message) ? 0 : 1;
    } else if (cmd == "agent") {
        // Register the Agent1 and report whether BlueZ accepted it. The prompt
        // callback is a no-op here; the test checks registration, which is what
        // a fake BlueZ can model (it does not call the agent back during Pair).
        rc = BtAgent::start(bus, [](const BtAgent::Prompt&) {}) ? 0 : 1;
        message = rc == 0 ? "" : "RegisterAgent refused";
        BtAgent::stop(bus);
    } else {
        std::fprintf(stderr, "unknown command %s\n", cmd.c_str());
        rc = 2;
    }

    // For mutating calls, say whether it worked and carry BlueZ's message.
    if (cmd == "poweron" || cmd == "poweroff" || cmd == "connect"
        || cmd == "disconnect" || cmd == "pair" || cmd == "removedev"
        || cmd == "trust" || cmd == "agent") {
        if (rc == 0)
            std::printf("OK %s\n", cmd.c_str());
        else
            std::printf("FAIL %s\n", message.c_str());
    }

    g_object_unref(bus);
    return rc;
}
