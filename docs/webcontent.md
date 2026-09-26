# WebContent port

The stable embed API WebAppMgr compiles against. Today it lives in
`adapters/qtwebkit-compat` and is shaped like QtWebKit (`QWebPage`, `QWebFrame`,
…). The **implementation** is QtWebEngine. After #81, QtWebEngine remains the
default engine; other backends are not cut over.

Parent architecture: [webcontent-dmabuf.md](webcontent-dmabuf.md) (#79).

## Capabilities (gated by `tests/webengine-capabilities`)

1. Render / present a frame into the window buffer path  
2. Input at coordinates  
3. Pre-document script injection (`PalmSystem` / bridge)  
4. Sync local resource reads  
5. Sync JS→C++ (`webos-bridge:///`)

These stay the contract whether the pixels travel over SysV shm or dma-buf, and
whether present uses `QWidget::grab` or direct `QWidget::render` into the
destination painter (`WEBOS_GRAB_PRESENT=1` restores grab for A/B).

## Rename plan (no behaviour change)

When a second engine backend exists (or sooner if we want a neutral name):

| Today | Target |
|---|---|
| `adapters/qtwebkit-compat` | `adapters/webcontent` (API façade) + `adapters/webcontent-qtwebengine` |
| QtWebKit-shaped headers | Keep until WebAppMgr migrates to `webcontent::Page`, or keep as the façade |

Phase 1–2 of #79 do **not** require the rename. Do not leave “qtwebkit” in the
name once a non-Qt engine is linked; until then the QtWebKit *API shape* is the
port WebAppMgr already uses.
