storaged
========

`com.palm.storage`, for a machine with no USB gadget and no partitions of its
own.

This is **ours, not HP's**. The CE drop ships nothing for this name, so every
call to it failed and the log filled with "com.palm.storage is not running".
Open webOS released its own `storaged` (Apache 2.0, 1900 lines of C), but almost
all of it is a phone's mass storage mode: udev rules, nyx, the kernel's USB
gadget, `mlabel`, fsck of the media partition. None of that exists on a laptop.
What is worth keeping is the API, which is what its callers here are written
against, and that is what this answers.

Who is calling
--------------

Found by reading the code, not guessed:

| Caller | What it asks for |
|---|---|
| `SystemService.cpp` (the shell) | `diskmode/enterMSM` from the power+volume combo, and the five `/storaged` signals that drive brick mode |
| `Security.cpp` | `erase/Wipe`, when the EAS passcode policy runs out of retries |
| `WindowServerLuna.cpp` | `erase/EraseAll`, from the full-erase key combo |
| luna-systemui `StoragedService.js`, `StoragedAlerts.js` | `diskmode/hostIsConnected` and `enterMSM` for the USB alerts, `MSMAvail` and `PartitionAvail` |
| luna-systemui `SystemManagerAlerts.js` | `erase/EraseVar`, the "Restart Required" and "database is full" alerts |
| `BrowserServer.cpp` | the `MSMStatus` signal: it suspends its plugin watcher while the media is exported |
| luna-sysservice `SystemRestore.cpp` | `PartitionAvail`: it creates the media folders when the partition arrives |
| Accounts' `DeviceEraseConfirmDialog.js` | `erase/EraseAll` (unreachable: the HP account row stays disabled) |

Mass storage mode
-----------------

A laptop does not export its disk over USB, so `/diskmode` answers rather than
pretends: `hostIsConnected` is false, `queryMSMStatus` is not in mass storage
mode, and `enterMSM` refuses with HP's own message ("not entering brick mode
because no usb connection"). Every caller checks before it shows anything, so
the USB alerts and the brick-mode screen never appear -- as before, except that
now nothing is left waiting for a service that is not there.

`changed`, `avail` and `busSuspended` are the three udev told HP's service
about. They answer `{"returnValue":true}` and do nothing; nothing here watches a
USB cable.

Of the signals, only `PartitionAvail` means anything: it is sent once at startup
for `/media/internal`, which is what makes luna-sysservice create the media
folders and lets the application installer start working.

Erasing
-------

`EraseVar`, `EraseMedia`, `EraseAll` and `Wipe` erase what this port owns:

* **its data directory** (`/var` inside the session: db8, the preferences, what
  its services keep);
* **the files it downloaded**, and only those. `/media/internal` is the user's
  Downloads folder here (see `com.palm.downloadmanager`'s `paths.ts`), so what
  this erases is what that service recorded in its history -- never the folder
  itself, never a folder in it, never anything else of the user's. `erase.cpp`
  is where that line lives, and `tests/storaged-erase.cpp` is what holds it
  there.

`Wipe` erases what `EraseAll` does. HP's overwrote the freed blocks; on a host
filesystem that is the operating system's business, not this service's.

**When** it happens is HP's design: the request is recorded and the erase runs
before the session is next started (`storaged --apply-erase`, from
`tools/run-lunasysmgr.sh`). A service cannot erase the database its own callers
have open; HP rebooted for the same reason, and its callers already expect the
system to go down -- LunaSysMgr exits as soon as the reply says the erase was
accepted. Deleting the data directory also takes `.webos-initialised` with it,
so the next start reinitialises the tree.

Where things are
----------------

* `src/erase.h` / `src/erase.cpp`: what each level erases and what counts as
  this port's, free of luna-service2 and of any real tree, so the test can put
  a temporary one in front of it.
* `src/main.cpp`: the service, and `--apply-erase`.
* `desktop-support/`: the role and `.service` files for both buses.

Not implemented, on purpose: `requestMedia` (a leftover in HP's udev script that
its own binary does not answer) and background collection of anything.
