Device Info
===========

`com.gachlab.app.deviceinfo` (#22): device name, profile facts, and Reset
Options (restart, shut down, erase via `com.palm.power` / `com.palm.storage`).
HP's Device Info on the TouchPad CE image is the specification.

```sh
tools/build-cards.sh com.gachlab.app.deviceinfo
tools/test-web.sh apps/deviceinfo/test/deviceinfo.service.test.ts
```
