#!/usr/bin/env python3
# What the Bluetooth service reads from BlueZ, and what it asks of it, against a
# fake BlueZ on a private bus.
#
# tests/bluez-state.cpp checks what a BluetoothState becomes on the webOS bus.
# This checks the other half: that the BluetoothState is the one BlueZ
# described, and that a radio toggle or a profile connect reaches BlueZ as the
# right call. None of it can be seen from the mapping alone -- the one
# GetManagedObjects round trip, Battery1 living on a separate interface of the
# same object, Device1.Trusted being the filter gettrusteddevices applies, and
# ConnectProfile taking the SIG UUID that uuidForProfile builds.
#
# The fake is python-dbusmock's bluez5 template, extended per device with the
# fields HP's stack never had (Trusted, a real Class, UUIDs, Battery1). The
# service's own GDBus client is exercised through tests/bluez-probe, built
# beside this, so the C++ under test is the C++ that ships -- not a Python
# re-implementation of it.
#
# SKIP (exit 77, which ctest reports as skipped) when python-dbusmock or
# dbus-daemon is absent, so a machine without them does not fail the build. The
# host's real adapter is never touched: everything is on the private bus.
import os
import subprocess
import sys
import time

SKIP = 77

try:
    import dbus
    from dbus.bus import BusConnection
    import dbusmock  # noqa: F401  (import is the availability check)
except ImportError as exc:
    print(f"SKIP: python-dbusmock is not installed ({exc})")
    sys.exit(SKIP)

PROBE = os.environ.get("BLUEZ_PROBE", os.path.join(os.path.dirname(__file__), "bluez-probe"))
if not os.path.exists(PROBE):
    print(f"SKIP: bluez-probe is not built at {PROBE}")
    sys.exit(SKIP)

HEADSET = "F8:DF:15:F2:29:ED"
A2DP = "0000110b-0000-1000-8000-00805f9b34fb"
HANDSFREE = "0000111e-0000-1000-8000-00805f9b34fb"

failures = 0


def check(ok, what):
    global failures
    print(f"  {what:<70} {'OK' if ok else '<-- FAIL'}")
    if not ok:
        failures += 1


def probe(addr, *args):
    # The probe talks to the private bus through DBUS_SYSTEM_BUS_ADDRESS, the
    # same bus the fake BlueZ is on.
    env = {**os.environ, "DBUS_SYSTEM_BUS_ADDRESS": addr, "DBUS_SESSION_BUS_ADDRESS": addr}
    out = subprocess.run([PROBE, *args], env=env, capture_output=True, text=True, timeout=30)
    return out.stdout.strip()


def main():
    daemon = subprocess.Popen(
        ["dbus-daemon", "--session", "--print-address", "--nofork"],
        stdout=subprocess.PIPE, text=True)
    addr = daemon.stdout.readline().strip()
    bus = BusConnection(addr)

    mock = subprocess.Popen(
        [sys.executable, "-m", "dbusmock", "--template", "bluez5"],
        env={**os.environ, "DBUS_SYSTEM_BUS_ADDRESS": addr, "DBUS_SESSION_BUS_ADDRESS": addr},
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(1.5)

    try:
        blz = dbus.Interface(bus.get_object("org.bluez", "/org/bluez"), "org.bluez.Mock")
        blz.AddAdapter("hci0", "my-computer")
        blz.AddDevice("hci0", HEADSET, "AKG Y500 WIRELESS")

        dpath = "/org/bluez/hci0/dev_" + HEADSET.replace(":", "_")
        dev = dbus.Interface(bus.get_object("org.bluez", dpath), "org.freedesktop.DBus.Mock")
        dev.UpdateProperties("org.bluez.Device1", {
            "Trusted": dbus.Boolean(True),
            "Paired": dbus.Boolean(True),
            "Connected": dbus.Boolean(True),
            "Class": dbus.UInt32(2360324),
            "Icon": dbus.String("audio-headset"),
            "UUIDs": dbus.Array([A2DP, HANDSFREE], signature="s"),
        })
        # Bonded is modern and the template does not create it, so add it rather
        # than update it.
        dev.AddProperties("org.bluez.Device1", {"Bonded": dbus.Boolean(True)})
        dev.AddProperties("org.bluez.Battery1", {"Percentage": dbus.Byte(60)})

        # --- readState -> gettrusteddevices -------------------------------
        print("readState: the adapter and the device BlueZ describes")
        status = probe(addr, "status")
        check('"address":"F8:DF:15:F2:29:ED"' in status, "the trusted device's address is read")
        check('"name":"AKG Y500 WIRELESS"' in status, "its Alias is read as the name")
        check('"status":"connected"' in status, "Connected becomes status:connected")
        check('"cod":2360324' in status, "Class is read as the numeric cod")
        check('"battery":60' in status, "Battery1.Percentage is read off the separate interface")
        check('"icon":"audio-headset"' in status, "the resolved icon is read")

        print("readState: an untrusted device stays out of the trusted list")
        blz.AddDevice("hci0", "00:11:22:33:44:55", "Stranger")
        status = probe(addr, "status")
        check("Stranger" not in status, "a device with Trusted=false is not in trusteddevices")

        # --- readState -> profgetstate ------------------------------------
        print("profgetstate: the connected device under the profiles its UUIDs carry")
        prof = probe(addr, "profstate")
        check('"a2dp":[{"state":"connected"' in prof, "110b puts it under a2dp")
        check('"hf":[{"state":"connected"' in prof, "111e puts it under hf")
        check('"hid":[]' in prof, "a profile it lacks is an empty array")

        # --- the radio ----------------------------------------------------
        print("setPowered: the radio reaches Adapter1.Powered")
        check(probe(addr, "radio") == "powered true", "the adapter starts powered")
        check(probe(addr, "poweroff").startswith("OK"), "poweroff is accepted by BlueZ")
        apath = "/org/bluez/hci0"
        powered = bus.get_object("org.bluez", apath).Get(
            "org.bluez.Adapter1", "Powered", dbus_interface="org.freedesktop.DBus.Properties")
        check(bool(powered) is False, "and Adapter1.Powered is now false")
        check(probe(addr, "poweron").startswith("OK"), "poweron is accepted")
        powered = bus.get_object("org.bluez", apath).Get(
            "org.bluez.Adapter1", "Powered", dbus_interface="org.freedesktop.DBus.Properties")
        check(bool(powered) is True, "and Adapter1.Powered is back to true")

        # --- connect / disconnect / pair ----------------------------------
        print("connectProfile: by address, through the device BlueZ has")
        devctl = dbus.Interface(bus.get_object("org.bluez", dpath), "org.freedesktop.DBus.Mock")
        check(probe(addr, "connect", HEADSET, "a2dp").startswith("OK"), "connecting a known device's a2dp works")
        # The right UUID must reach BlueZ: a2dp is the A2DP sink UUID, built by
        # uuidForProfile. GetMethodCalls records each ConnectProfile with its arg.
        calls = devctl.GetMethodCalls("ConnectProfile")
        connected_uuids = [str(c[1][0]) for c in calls]
        check(A2DP in connected_uuids, "ConnectProfile receives the a2dp SIG UUID, not another")
        check(probe(addr, "connect", "AA:BB:CC:DD:EE:FF", "a2dp").startswith("FAIL"),
              "connecting an address BlueZ has no object for fails cleanly")
        check(probe(addr, "pair", HEADSET).startswith("OK"), "pairing a known device is accepted")
        check(probe(addr, "trust", HEADSET).startswith("OK"), "setting Trusted is accepted")

        # --- the pairing agent --------------------------------------------
        print("Agent1: the service registers as BlueZ's pairing agent")
        check(probe(addr, "agent").startswith("OK"), "RegisterAgent on AgentManager1 is accepted")
        # The mock records the agent path RegisterAgent was called with.
        mgr = dbus.Interface(bus.get_object("org.bluez", "/org/bluez"),
                             "org.freedesktop.DBus.Mock")
        agent_calls = mgr.GetMethodCalls("RegisterAgent")
        registered_paths = [str(c[1][0]) for c in agent_calls]
        check("/org/webos/bluetooth/agent" in registered_paths,
              "the agent is registered at our object path")
        capabilities = [str(c[1][1]) for c in agent_calls]
        check("KeyboardDisplay" in capabilities,
              "with KeyboardDisplay, the capability that allows every SSP method")

    finally:
        mock.terminate()
        daemon.terminate()

    if failures == 0:
        print("\nall bluez-client checks passed")
        return 0
    print(f"\n{failures} bluez-client check(s) failed")
    return 1


if __name__ == "__main__":
    sys.exit(main())
