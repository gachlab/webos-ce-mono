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

Import paths on Host (`HostWindowDataDmaBuf`):

1. **GL** — `GlImporter`: `EGLImage` + `GL_TEXTURE_EXTERNAL_OES` → blit to
   RGBA FBO → `glReadPixels` into `QPixmap` (`tests/dmabuf-gl-present`).
   Mesa rejects `GL_TEXTURE_2D` for these linear BOs.
2. **CPU fallback** — `gbm_bo_import` / `mmap` → `QImage` copy.

Neither path is zero-copy into the card compositor yet: both still end in a
CPU `QPixmap`. True win needs CardWindow to sample the OES texture (or a
shared GL texture) without a readback round-trip.

## Phase 2 present path

`QWebFrame::render` paints with `QWidget::render` into the destination painter
(the card buffer). `WEBOS_GRAB_PRESENT=1` restores grab→drawPixmap for A/B.

Product-shaped harnesses (same family as #81’s WPE↔Qt tables; axis is present
path, engine fixed to QtWebEngine via qtwebkit-compat):

**`tests/engine-scroll-load`** — under-load scroll, image-diff proof the page
moved (400×600, 20 steps). Measured this machine (2026-09-25, GL import
wired):

| present | moved | median present ms | scroll wall ms | VmHWM |
|---|---|---:|---:|---:|
| grab | yes | 0.263 | 429 | ~264 MB |
| direct (QImage) | yes | **0.188** | 449 | ~265 MB |
| dmabuf + mmap Host | yes | 4.267 | 327 | ~266 MB |
| dmabuf + GL readback Host | yes | 3.456 | 634 | ~439 MB |

**Verdict:** while Host still materializes a `QPixmap`, dma-buf loses to
painting into a normal `QImage`. GL import beats mmap slightly on present
median but costs RSS and wall time; default card path stays **direct**.
Keep `WEBOS_DMABUF=1` opt-in for transport/factory work and the next
texture-compose spike.

**`tests/engine-card-load`** — 25 local browser-like cards, proof = title + paint
(grab vs direct only; no Host import in this harness):

| present | ok/fail | wall ms | peak tree RSS | median present ms |
|---|---|---:|---:|---:|
| grab | 25/0 | 755 | ~2.99 GB | 0.145 |
| direct | 25/0 | 711 | ~2.95 GB | **0.105** |

Microbench `tests/present-cost` / `dmabuf-present` / `dmabuf-gl-present`
remain for isolation; the scroll harness is the go/no-go for Host import cost.

## Adapters

Primitives: `adapters/dmabuf-window` (`Device` / `Frame` / `Importer` /
`GlImporter` / registry). CE windowdata classes beside HP’s factories.
