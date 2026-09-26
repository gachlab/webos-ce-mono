# WebContent + dma-buf factories (#79)

Architecture for a lean desktop buffer path **without** rewriting the Palm card
compositor. Parent product context: #68. Engine spike that informed this: #81.
WebContent port boundary: [webcontent.md](webcontent.md).

## Decision (2026-09-25)

Stay on **QtWebEngine** for the default stack. WPE behind the QtWebKit-shaped
API was brought up and measured (#81 / PR #82); multi-card + heavy HTTPS loses
to Chromium on desktop wall time, RSS, and process count. Optimize the current
engine’s present path instead of cutting over.

## What is already accelerated

- **LunaSysMgr** already hardware-composites (`QOpenGLWidget` + Mesa) as one
  Wayland client surface to the host.
- **Web content** is still CPU-rasterized (`--disable-gpu`) into a buffer the
  shell reads. Present no longer defaults to `QWidget::grab()` (see phase 2).

Phase 1–2 modernize the **WebAppMgr → HostWindowData** buffer path, not the
shell’s OpenGL or its Wayland-client role.

## Target shape

```
LunaSysMgr (Palm card UI — unchanged feel)
  HostWindowDataFactory → HostWindowDataDmaBuf (ours)
         ▲  dma-buf fd (pidfd_getfd) / damage / ack
WebAppMgr (app lifecycle, input, LS2)
  RemoteWindowDataFactory → RemoteWindowDataDmaBuf (ours)
         ▲  present frame / input / JS bridge
WebContent port (qtwebkit-compat today; rename later — webcontent.md)
         ▲
Engine: QtWebEngine (default) | future backends behind the same port
```

Non-goals: Luna Surface Manager, QML card rewrite, Vulkanizing `QGraphicsView`,
reviving closed `napp` / `HAVE_TEXTURESHARING`.

## Durable contract

**dma-buf** (+ modifiers, fences later) is the shared-framebuffer contract.
Shell compose stays OpenGL/EGL. Engine-internal GL vs Vulkan is behind the
WebContent port.

## Phases

| Phase | Status | Notes |
|---|---|---|
| 0 Agree architecture | done | this doc + link from architecture.md |
| 1 dma-buf behind factories + test present | done | `tests/dmabuf-present`; `WEBOS_DMABUF=1` |
| 2 Drop grab on hot path + numbers | done | direct `QWidget::render`; `tests/present-cost` |
| 3 WebContent port boundary | done | [webcontent.md](webcontent.md); capabilities still green |
| 4 Engine spikes | done via #81 | WPE no-go for cutover |
| 5 Engine decision | QtWebEngine | Recorded above |

## Phase 1–2 transport

Factories select `*DmaBuf` when `WEBOS_DMABUF=1` and a render node is usable.

- **In-process:** registry keyed by `key()`.
- **Cross-process:** Remote writes a `Handoff` (fd number + layout) into the
  identity `PIpcBuffer`; Host uses `pidfd_getfd` against WebAppMgr’s pid
  (`WebAppMgrProxy::pid()`). Same-user; works with `yama.ptrace_scope=0`
  (this lab). SCM_RIGHTS on `PIpcChannel` remains a hardening option.

Default without the env var is still SysV shm. Opt in deliberately.

Import samples via `gbm_bo_import` / `mmap`. Zero-copy `EGLImage` →
`GL_TEXTURE_2D` is not used yet (Mesa rejects that target for these linear BOs;
`GL_TEXTURE_EXTERNAL_OES` + blit is a follow-up for shell compose).

## Phase 2 present path

`QWebFrame::render` paints with `QWidget::render` into the destination painter
(the card buffer). `WEBOS_GRAB_PRESENT=1` restores grab→drawPixmap for A/B.

Measured on this machine (`tests/present-cost`, 1024×768, 40 frames, offscreen):

| Path | median ms | notes |
|---|---:|---|
| QImage + grab→drawPixmap | 1.11 | `WEBOS_GRAB_PRESENT=1` |
| QImage + direct render | 0.72 | default |
| dma-buf + direct render | 0.44 | `--dmabuf` |

## Adapters

Primitives: `adapters/dmabuf-window`. CE windowdata classes beside HP’s factories.
