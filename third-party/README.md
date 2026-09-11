# third-party — pineado, no vendoreado

Aquí vive una sola dependencia, y no está en el repo: son cientos de MB que
nadie va a editar, y meterlos convertiría el clon en algo que nadie quiere
hacer. Se consume en una ref fija.

| Qué | Origen | Ref |
|---|---|---|
| QtWebKit 5.212 | `qtwebkit/qtwebkit` | `756e1c8f` (27-may-2024) |

La construye `tools/construir-third-party.sh`, que clona en ese sha exacto,
aplica `patches/qtwebkit-5.212-debian-sid.patch` y compila. `construir.sh`
la llama como primera etapa y la salta si ya está instalada.

## Por qué QtWebKit y no otro motor

`webappmanager` y `BrowserServer` hablan por **NPAPI**, que es la frontera que
Palm dibujó entre el shell y el motor web. QtWebEngine es Chromium y no expone
NPAPI, así que no es un reemplazo: sustituirlo obligaría a rehacer esa costura.

Lo que sí hay es futuro: **movableink/webkit** mantiene el puerto Qt sobre
WebKit moderno (último merge con upstream en 2025), con la misma API pública
—`QWebPage`, `QWebFrame`, `QWebSettings`— que usa `webappmanager`. Es Qt6 y
tampoco trae NPAPI, pero demuestra que este camino no está muerto.

## Lo que ya no se pinea

El drop original de HP pineaba además **Qt 4.8** (su propio fork, con
`qmake-palm`), la **WebKit de Isis** con V8, **cmake 2.8.7** y **leveldb 1.9**.
Nada de eso hace falta ahora: Qt5 y CMake salen de Debian, y db8 compila sin
leveldb. Se deja escrito aquí porque es la diferencia de fondo entre aquel
build y éste.
