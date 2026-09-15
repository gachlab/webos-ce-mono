sysfs-powerd
============

`com.palm.power` for a machine that reports power through
`/sys/class/power_supply` instead of a phone's fuel gauge.

Ours, not HP's. On a device this was powerd, and nothing in the CE drop provides
it: on the desktop every caller got `Service does not exist: com.palm.power`, so
the status bar never learned the battery level.

What it answers
---------------

Once a `com.palm.power` exists, every caller that had been failing quietly starts
talking to it, so the whole interface they use is answered, not only the battery.

| Category | Method or signal | Caller |
|---|---|---|
| `/com/palm/power` | `batteryStatusQuery`, `chargerStatusQuery` | status bar |
| | signal `batteryStatus` | status bar, `DisplayManager`, `PowerdService.js` |
| | signals `chargerStatus` **and** `USBDockStatus` | `DisplayManager` listens on the first; the status bar and `PowerdService.js` on the second |
| | `identify`, `suspendRequestRegister`, `prepareSuspendRegister`, `*Ack` | `SuspendBlocker` |
| | `activityStart`, `activityEnd` | WebAppManager, SoundPlayer, InputManager, DisplayManager |
| `/timeout` | `set`, `clear` | luna-sysservice |
| `/shutdown` | `machineOff`, `machineReboot` | the power menu and systemui's alerts |

The payload field names are not chosen here; each is read by a caller that drops
the whole payload when one is missing. `src/power_state.h` names them and
`tests/power-state.cpp` checks them as strings.

Two things it never does
------------------------

- **Emit `suspendRequest` or `prepareSuspend`.** There is no suspend here, and
  `SuspendBlocker.cpp` carries HP's own FIXME about hangs on that path.
- **Power off or reboot the machine.** `machineOff` ends the webOS session and
  `machineReboot` restarts it: the service writes `poweroff` or `restart` to
  `$WEBOS_SESSION_REQUEST` and ends the shell, and `tools/webos-session.sh`
  brings the session back when asked to.

Why C++
-------

The status bar and `DisplayManager` do not poll; they `addmatch` luna-service2
signals. A JavaScript service cannot emit one: `palmbus` exposes no
`LSSignalSend`, and signals are a transport message rather than a callable hub
method.

Things worth knowing
--------------------

- **A machine with no battery reports 100% on external power.** HP's status bar
  has no way to hide its battery icon; with no powerd at all it shows " ? ".
- **Schema validation is off, and this relies on it.** `DisplayManager` declares
  `batteryStatus` as `{"percent": integer}` with `additionalProperties:false`,
  while the status bar reads `percent_ui`. One signal carries both, which works
  because `luna.conf` sets `schemaValidationOption=0` (ignore). Turning validation
  on would make `DisplayManager` drop every battery update.
- **State is polled every five seconds.** A battery moves a percent in minutes;
  five seconds keeps a pulled cable feeling immediate.

Built by `tools/build.sh powerd`; started with the other static services in the
launcher's `services` stage.
