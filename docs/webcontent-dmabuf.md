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
- **Web content** defaults to `--use-gl=angle --use-angle=gl --enable-gpu-rasterization`
  (#84). WebAppMgr inherits the session `QT_QPA_PLATFORM` (wayland) instead of
  forcing offscreen — offscreen freezes GPU scroll even with ANGLE. Native
  `--use-gl=egl` under Wayland still floods context-loss; ANGLE+gl keeps Mesa
  hardware GL. `tests/webengine-gpu-boot` + `tests/webengine-gpu-scroll` cover
  boot/paint and scroll-under-GPU (SwiftShader under offscreen CI).
  Card buffers default to dma-buf when a render node works (`WEBOS_DMABUF=0`
  opts out). Remote redirects `QQuickWindow` into the card FBO
  (`setRenderTarget`); staging `QImage` upload is fallback only. Contract:
  `tests/webengine-gpu-fbo-present`.
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

Factories select `*DmaBuf` when a render node is usable (default; `WEBOS_DMABUF=0`
opts out).

- **In-process:** registry keyed by `key()`.
- **Cross-process:** Remote writes a `Handoff` (fd number + layout) into the
  identity `PIpcBuffer`; Host uses `pidfd_getfd` against WebAppMgr’s pid
  (`WebAppMgrProxy::pid()`). Same-user; works with `yama.ptrace_scope=0`
  (this lab). SCM_RIGHTS on `PIpcChannel` remains a hardening option.

Default without the env var is still SysV shm. Opt in deliberately.

### Remote paint → GPU buffer

`RemoteWindowDataDmaBuf` prefers redirecting QtWebEngine's `QQuickWindow` into
the card FBO texture (`QWebPage::bindPresentTexture` / #84). Flow when bound:

1. `GlRenderTarget` creates an EGL FBO and exports dma-buf once.
2. Engine Quick renders straight into that texture (shared GL context).
3. Host `paintContents` imports via `EXTERNAL_OES` (no CPU readback).

Fallback while the Quick surface is not ready yet: staging `QImage` →
`uploadArgb32` (same as pre-#84).

Contract: `tests/dmabuf-gl-paint` (FBO clear → export → OES sample);
`tests/webengine-gpu-fbo-present` (engine scroll into redirected texture).

Import paths on Host (`HostWindowDataDmaBuf`):

1. **Texture compose (hot path)** — `paintContents()` attaches to the shell’s
   current EGL context, `importFrame` (`EGLImage` + `EXTERNAL_OES` → RGBA
   texture), then draws a textured quad via `QPainter::beginNativePainting`
   (`HostWindow::paint` / `CardWindow::paintBase`). No CPU readback.
2. **acquirePixmap fallback** — `mmap` → `QImage` copy for screenshot / non-GL
   callers.

## Phase 2 present path

`QWebFrame::render` paints with `QWidget::render` into the destination painter
(the card buffer). `WEBOS_GRAB_PRESENT=1` restores grab→drawPixmap for A/B.

Product-shaped harnesses (same family as #81’s WPE↔Qt tables; axis is present
path, engine fixed to QtWebEngine via qtwebkit-compat):

**`tests/engine-scroll-load`** — under-load scroll, image-diff (or FBO sample +
final readback for `dmabuf-gl`) proof the page moved (400×600, 20 steps).
Measured this machine (2026-09-26, Wayland + ANGLE+gl; Remote redirect + Host OES
where noted):

| present | moved | median present ms | scroll wall ms | VmHWM |
|---|---|---:|---:|---:|
| direct (QImage) | yes | 2.764 | 327 | ~458 MB |
| dmabuf-gl (redirect) | yes | **0.057** | 335 | ~460 MB |

Earlier (2026-09-25, Chromium still CPU-raster + staging upload):

| present | moved | median present ms | scroll wall ms | VmHWM |
|---|---|---:|---:|---:|
| grab | yes | 0.305 | 475 | ~265 MB |
| direct (QImage) | yes | **0.229** | 478 | ~265 MB |
| dmabuf + mmap Host | yes | 2.509 | 591 | ~267 MB |
| dmabuf-gl (upload+OES) | yes | 3.325 | 600 | ~383 MB |

**Verdict:** with GPU raster + Quick→dma-buf redirect, present drops ~50× vs
direct snapshot. Default is **dma-buf** when a render node exists;
`WEBOS_DMABUF=0` restores SysV shm / direct.

**`tests/engine-card-load`** — 25 local browser-like cards, proof = title + paint
(grab vs direct only; no Host import in this harness):

| present | ok/fail | wall ms | peak tree RSS | median present ms |
|---|---|---:|---:|---:|
| grab | 25/0 | 755 | ~2.99 GB | 0.145 |
| direct | 25/0 | 711 | ~2.95 GB | **0.105** |

Microbench `tests/present-cost` / `dmabuf-present` / `dmabuf-gl-present` /
`dmabuf-gl-paint` remain for isolation.

## Adapters

Primitives: `adapters/dmabuf-window` (`Device` / `Frame` / `Importer` /
`GlImporter` / `GlRenderTarget` / registry). CE windowdata + thin `HostWindow` /
`CardWindow` hooks for `paintContents`.
