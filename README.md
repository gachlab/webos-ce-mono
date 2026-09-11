# webOS CE — monorepo

El código que **HP liberó** para webOS, en un solo repositorio que se clona una vez
y se compila sin bajar nada de internet.

No contiene nada de LuneOS, LunaCE ni webOS OSE de LG. Solo el drop original de HP,
en los tags exactos que su propio build script fijaba.

## Por qué

El build original (`build-webos-desktop.sh`) no depende de código: depende de **URLs**.
Descarga 45 zipballs de tags fijos repartidos en tres organizaciones de GitHub.
Trece años después ya hay tres muertas:

| Descarga | Qué pasó |
|---|---|
| WebKit desde `github.com/downloads/...` | GitHub eliminó Downloads en 2013 |
| leveldb desde `googlecode.com` | Google Code cerró en 2016 |
| cmake precompilado de `cmake.org` | ya no alojan ese binario |

Y eso solo va a empeorar.

## Estructura

- `components/` — las fuentes de HP, vendoreadas con `git subtree --squash`.
  Cada una trae en su commit el repo y el sha exactos de donde salió.
- `third-party/` — Qt 4.8 y WebKit **no** están vendoreados: son cientos de MB
  que no se van a editar. Se consumen pineados como artefactos.
- `patches/` — parches de portabilidad. **No vienen de HP**: son lo mínimo para que
  el código de 2012 compile hoy. Cada uno explica en su cabecera qué arregla y por qué.
- `MANIFEST.tsv` — los 55 componentes con su repo, su ref y su sistema de build.
- `tools/build-webos-desktop.sh.reference` — el script original de HP. Se conserva
  porque **su orden de llamadas es el grafo de dependencias**, ya ordenado
  topológicamente. Es la fuente de la que sale la orquestación nueva.

## Los dos LunaSysMgr

Conviven a propósito:

- `components/luna-sysmgr/` — el de **Open webOS** (`openwebos/luna-sysmgr`), que es
  el que compila verde hoy. Es la referencia contra la cual comparar.
- `components/luna-sysmgr-ce/` — el del **CE 3.0.5 del TouchPad**
  (`woce/LunaSysMgr` en el commit "Push from tarball"), que es el objetivo.

## Compilar

Requiere Debian moderno (probado en sid) con Qt5, y las cabeceras de desarrollo
de glib, sqlite3, openssl, libxml2 y boost.

```sh
tools/build.sh          # todo, en el orden del MANIFEST
tools/run-lunasysmgr.sh  # arranca el shell
```

Cada etapa se puede correr sola: `autotools`, `cmake`, `qmake`, `rootfs`.

`tools/run-lunasysmgr.sh` **no instala nada en el sistema**. Solo `/etc/palm`
esta clavado en el codigo (`Settings.cpp`); todo lo demas es configurable, asi
que se monta con `bwrap` un namespace donde `/etc/palm` apunta al rootfs local.

## Estado

El shell corre. `LunaSysMgr` compila con gcc 16 y Qt 5.15 y arranca sobre X11
—ver `docs/lunasysmgr-en-debian.png`— con **un solo cambio de codigo** en todo
el componente (`KineticScroller.cpp`, un `qInf()` que Qt5 ya trae).

| | |
|---|---|
| Compilan | 31 de los componentes que se construyen |
| Codigo tocado | ~180 lineas sobre el drop de HP |
| Toolchain | Debian sid, gcc 16, Qt 5.15, CMake del sistema |

Lo que falta, en orden:

- **QtWebKit 5.212** (`build-modern/third-party/`, parche en `patches/`). Es lo
  que desbloquea `webappmanager` y con el las apps visibles.
- **`webappmanager`**. Ya revisado: 69 archivos, cero bloqueadores de Qt5, y sus
  cinco librerias ya estan en staging. Solo espera el motor.
- **`luna-sysmgr-ce`** (el del TouchPad) portado a Qt5, usando como referencia
  los 61 archivos donde el propio HP puso guardas `QT_VERSION_CHECK` en Open webOS.
- **Los addons de node** (`sysbus`, `pmlog`, `dynaload`): API v8 vieja, hay que
  llevarlos a N-API para correr los servicios sobre el node de Debian.
- **El navegador** (`BrowserServer`/`BrowserAdapter`): dependen de NPAPI de
  verdad, no solo de un include path. Es el trozo mas caro y va de ultimo.
