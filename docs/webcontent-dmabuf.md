# WebContent + dma-buf factories (#79)

Architecture for a lean desktop buffer path **without** rewriting the Palm card
compositor. Parent product context: #68. Engine spike that informed this: #81.

## Decision (2026-09-25)

Stay on **QtWebEngine** for the default stack. WPE behind the QtWebKit-shaped
API was brought up and measured (#81 / PR #82); multi-card + heavy HTTPS loses
to Chromium on desktop wall time, RSS, and process count. Optimize the current
engine’s present path instead of cutting over.

## What is already accelerated

- **LunaSysMgr** already hardware-composites (`QOpenGLWidget` + Mesa) as one
  Wayland client surface to the host.
- **Web content** is still CPU-rasterized (`--disable-gpu` + offscreen
  `QWidget::grab`) into SysV shm (`PIpcBuffer`), then uploaded for shell compose.

Phase 1–2 modernize the **WebAppMgr → HostWindowData** buffer path, not the
shell’s OpenGL or its Wayland-client role.

## Target shape

```
LunaSysMgr (Palm card UI — unchanged feel)
  HostWindowDataFactory → HostWindowDataDmaBuf (ours)
         ▲  dma-buf fd / lock / damage / ack
WebAppMgr (app lifecycle, input, LS2)
  RemoteWindowDataFactory → RemoteWindowDataDmaBuf (ours)
         ▲  present frame / input / JS bridge
WebContent port (qtwebkit-compat today; rename later)
         ▲
Engine: QtWebEngine (default) | future backends behind the same port
```

Non-goals: Luna Surface Manager, QML card rewrite, Vulkanizing `QGraphicsView`,
reviving closed `napp` / `HAVE_TEXTURESHARING`.

## Durable contract

**dma-buf** (+ modifiers, fences later) is the shared-framebuffer contract.
Shell compose stays OpenGL/EGL (`EGLImage` import). Engine-internal GL vs Vulkan
is behind the WebContent port.

## Phases

| Phase | Status | Notes |
|---|---|---|
| 0 Agree architecture | done | this doc + link from architecture.md |
| 1 dma-buf behind factories + test present | done | CPU blit; in-process registry; WEBOS_DMABUF=1 |
| 2 Drop grab-from-widget on the hot path | later | Desktop before/after numbers |
| 3 WebContent port boundary / rename plan | later | |
| 4 Engine spikes | done via #81 | WPE no-go for cutover |
| 5 Engine decision | QtWebEngine | Recorded above |

## Phase 1 transport note

Factories select `*DmaBuf` when `WEBOS_DMABUF=1` and a render node is usable.
Attachment from Host to Remote’s buffer uses a **process-local registry** keyed
by the same integer `key()` the shm path used (backed by a small `PIpcBuffer`
for identity). That is enough to exercise the factories in
`tests/dmabuf-present` (one process).

**Do not set `WEBOS_DMABUF` in a live two-process session** until dma-buf fds
cross the existing Unix `PIpcChannel` (SCM_RIGHTS). Without that, the Host
cannot import the Remote’s framebuffer.

Phase 1 import samples via `gbm_bo_import` + CPU map (proven on Mesa). Zero-copy
`EGLImage` → `GL_TEXTURE_2D` is not used yet: Mesa rejects that target for these
linear BOs (`GL_TEXTURE_EXTERNAL_OES` + blit is the follow-up for shell compose).

## Adapters

Primitives live in `adapters/dmabuf-window` (GBM create/map/export, EGL import).
CE `RemoteWindowDataDmaBuf` / `HostWindowDataDmaBuf` sit beside HP’s windowdata
and are selected from the factories with a minimal hook.
