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

## Estado

Migración de la orquestación (bash + URLs → CMake + fuentes vendoreadas).
El toolchain sigue siendo el de 2012: gcc 4.6, Qt 4.8, Ubuntu 12.04. Eso es otro proyecto.
