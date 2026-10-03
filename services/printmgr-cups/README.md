printmgr-cups
=============

`com.palm.printmgr` from CUPS.

Ours, not HP's. HP built printmgr on its own wprint library; nothing in the CE
drop answers `com.palm.printmgr`, so the one surviving consumer -- enyo's
PrintDialog (`components/enyo-1.0/framework/lib/printdialog`) -- calls into a
void and the Print button in Email and the browser does nothing. This provides
the service over CUPS; the Print Manager card (`com.palm.app.printmanager`,
`apps/printmanager`) is the other half of the ticket.

What it answers
---------------

The dialog is the specification; it was read request by request (`PrintJob.js`,
`PrinterSelector.js`, `PrinterOptions.js`, `PrintManagerError.js`).

| Category | Method | Shape |
|---|---|---|
| `/printers` | `list` (subscribe) | events `{eventType:"Add"\|"Rmv", printerID, printerName, printerAddress}` |
| | `getCurrent` | `{printerID, printerName, printerAddress}`, or no `printerID` for none |
| | `setCurrent` | `{printerID}` |
| | `add` | accepted; CUPS discovers IPP Everywhere printers itself |
| | `getCapabilities` | `{mediaType:[], mediaSize:[], quality:[], canDuplex, hasColor}` |
| `/jobs` | `open` (subscribe) | `{printerID, description, appName}` → `{jobID}` |
| | `editPrintParams` | `{jobID, numCopies, ...}` |
| | `getFinalParamsAndArea` | `{jobID}` |
| | `addFile` | `{jobID, file}` → submits to CUPS |
| | `getStatus` (subscribe) | `{jobID, printerState:"DONE", jobStatus:"Success"\|"Cancelled"\|"Error"\|"Corrupt"}` |
| | `getRenderStatus` (subscribe), `cancel`, `close` | |

Failures are `{returnValue:false, errorCode}`, with the codes the dialog's
`PrintManagerError.getErrorText` keys its text off (−203 no response, −204/−205
duplicate, −206 invalid IP, −238 not supported, −298 no wifi, −400..−499
communication).

How it is built
---------------

Three layers, as `nm-connectionmanager`:

- `src/print_state.h` -- the payloads, header-only and free of both the Luna bus
  and CUPS, so the field names and shapes can be checked without ls-hubd or a
  printing daemon (`tests/print-state.cpp`).
- `src/cups_client.{h,cpp}` -- the CUPS half, libcups. Every call takes the
  `http_t*` connection it should use, so a test can hand it a cupsd it controls.
- `src/main.cpp` -- the Luna glue: registers `com.palm.printmgr`, wires the
  methods, holds the small job table, and polls CUPS job status for subscribers.

libcups is the host's (`libcups2-dev`), linked the way `audiod-pipewire` links
libpulse. The daemon it talks to is whatever `cupsd` the session can reach;
printers are CUPS's local queues and the IPP Everywhere printers it discovers.

Built by `tools/build.sh printmgr`.

What is deliberately simple
---------------------------

- **`add` is a no-op success.** HP added printers by IP; CUPS discovers IPP
  Everywhere printers on its own, so there is nothing to add on a modern setup.
  The dialog's "Add a printer" flow still completes.
- **`getRenderStatus` reports DONE at once.** The file is submitted to CUPS
  whole; CUPS and its filters render. There is no separate per-page render here.
- **Per-job options beyond copies are accepted but not all applied yet.** The
  dialog's flow completes; honouring every option (duplex, media, quality) at
  submit time is a refinement on top of the working path.
