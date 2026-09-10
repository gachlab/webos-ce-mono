# Estado del código: qué estamos portando

Medido sobre el drop original de HP, no estimado.

## Qué es esto realmente

**No es un sistema operativo. Es una pila de aplicaciones de userland.**

En el target de escritorio, webOS no trae kernel, drivers, init ni distribución.
`LunaSysMgr` no carga **ningún módulo de kernel propio**, y sus dependencias de
sistema son ordinarias:

```
c dl m pthread rt stdc++ gcc_s     runtime de C/C++
QtCore QtGui QtDeclarative
QtNetwork QtOpenGL QtScript QtSql  Qt 4  <- el ancla real
X11 xcb Xext Xfixes Xdamage GL drm X11 y graficos
sqlite3 yajl pcre z                librerias corrientes
```

Ubuntu 12.04 es **donde compila hoy**, no algo que el sistema arrastre.

## Tamaño

El monorepo son ~3.6M lineas, pero eso engaña: `build-support-ce` (1.4M) son
**cabeceras**, y `nodejs` (297K) trae V8 dentro.

| Componente | Lineas | Nota |
|---|---|---|
| `luna-sysmgr-ce` | **193K** | 153K `.cpp` + 39K `.h`, en 605 archivos. El corazon |
| `mojomail`, `db8` | 89K + 66K | Servicios C++ |
| `app-services`, `enyo-1.0`, `core-apps` | ~270K | JavaScript |
| `luna-service2` | 29K | El bus |

`luna-sysmgr-ce` son 293 archivos `.cpp` con **mediana de 238 lineas**. Los 20 mas
grandes son el **37%** del codigo.

## Atadura al hardware: ninguna

```
ensamblador ARM inline ......  0 archivos
llamadas a nyx (capa HW) ....  0 archivos
TARGET_DEVICE .............. 47 de 605 archivos (8%)
MACHINE_* (modelo concreto) . 10 archivos
```

HP aislo la abstraccion de hardware en `nyx-lib`, una libreria aparte que
LunaSysMgr **no toca directamente**.

## La atadura real: Qt 4

Los bloqueadores clasicos **no estan**:

```
Q_WS_*  (eliminado en Qt5) ...  0 archivos
QRegExp (eliminado en Qt6) ...  0 archivos
QHttp   ......................  0 archivos
```

Lo que usa masivamente sigue vivo en Qt6: `QGraphicsView`/`QGraphicsScene`
(140 archivos), `QWidget` (135), `QPainter` (206).

**El unico bloqueador duro es QtDeclarative (QML1)**, eliminado en Qt5:
19 archivos, 14.774 lineas. Tres cargan la mitad — `dimensionslauncher.cpp` (4.236),
`LockWindow.cpp` (2.838), `WindowServerLuna.cpp` (1.334). Mas 8 archivos con
`QGLWidget` -> `QOpenGLWidget`.

**Superficie de modernizacion: 27 archivos**, no 605.

## El motor web esta aislado

`LunaSysMgr` **no incluye una sola cabecera de WebKit**. Cero `QtWebKit`, cero
`QWebView`, cero `QWebPage`. Habla con el motor por **IPC, en 65 archivos**; el
navegador corre en otro proceso (`BrowserServer`).

Consecuencia: se puede reemplazar el motor sin tocar el gestor de ventanas. Y como
las apps son paginas web (`"type": "web"`, `main: index.html`), **modernizar el motor
habilita apps con JS moderno sin tocar LunaSysMgr**.

Enyo 1.0 es ES5 de 2011 (verificado: 0 arrow functions, 0 `let`, 0 `class`), pero no
ata: cualquier framework sirve mientras produzca `index.html` + `appinfo.json`.

## El target de dispositivo NO es compilable

`device.pri` exige 10 librerias. **Seis no existen en ningun lado**, ni fuente ni binario:

| Libreria | Blob ARM | Fuente |
|---|---|---|
| `luna-prefs`, `PmLogLib` | no | **si** |
| `rolegen`, `serviceinstall` | **si** | si |
| `hid`, `memchute`, `media-api`, `napp`, `hal`, `affinity` | no | **NO EXISTE** |

Son justamente las que tocan el hardware. Eso significaba "Community **Edition**":
HP libero lo que legalmente podia.

Por eso LunaCE compilaba solo `LunaSysMgr` y lo instalaba sobre un TouchPad **que ya
corria webOS 3.0.5**: los blobs salian del aparato, no del release.

**El camino vanilla a dispositivos existe, pero es el otro**: en Open webOS, HP
reescribio esas piezas cerradas como abiertas (`nyx-lib` en vez del `hal` propietario)
y dejo `build-webos` (OpenEmbedded) para construir imagenes. Sigue siendo codigo de
HP de 2012.

## C++: pre-C++11 puro

0 `nullptr`, 2 punteros inteligentes en 605 archivos, punteros crudos en todas partes.
Compilar con GCC 14 dara miles de avisos y bastantes errores por reglas endurecidas.
Mecanico, no conceptual — pero es volumen.

## Plan, por dificultad creciente

| | Esfuerzo | Desbloquea |
|---|---|---|
| Build system -> CMake | Bajo | Compilar sin red, en paralelo |
| Qt4 -> Qt5 | **Medio: 27 archivos** | Correr en distros modernas |
| Motor web | Medio, **aislado por IPC** | Apps con JS moderno |
| C++03 -> moderno | Alto por volumen | Compiladores actuales |

## Lo que NO se toca

El modelo de tarjetas, el compositor sobre `QGraphicsView`, el bus de luna, la
separacion en procesos y el formato de app. **Eso es webOS.** LuneOS cambio
justamente esas cosas y termino siendo otro sistema.

Es la diferencia entre **restaurar** y **reconstruir**.

## nodejs: no hay que compilarlo

El `nodejs` de HP es **0.4.12** (2011) y trae V8 dentro, que se construye con
**SCons escrito en Python 2**. Portar eso es un proyecto con cola incierta.

No hace falta. Los parches de HP a node (`#if WEBOS` en `node.cc`) son **limites
de memoria para un aparato empotrado**:

```c
#define MAX_OLD_SPACE_OPTION  "--max_old_space_size 10485760"   // 10 MB
static int max_stack_size = 524288;                             // 512 KB
#if WEBOS
  abort();          // en vez de exit(1)
```

Ninguno es funcionalidad de webOS: son ajustes para un TouchPad con 1 GB de RAM.

**El camino barato es usar el node de la distribucion** (Debian trae v26) y portar
solo lo que si es de webOS:

| Pieza | Tamano | Que es |
|---|---|---|
| `sysbus` | 2660 lineas | El puente entre JS y el bus de luna. **El trabajo real** |
| `dynaload` | 334 lineas | Carga dinamica |
| `pmlog` | 104 lineas | Registro |

Hay que pasarlos de la API cruda de V8 de node 0.4 a N-API.

Y el JavaScript de los servicios (**17.671 lineas**) esta practicamente limpio:

```
require('sys')   3 usos   ->  se llama 'util' desde node 0.8
new Buffer(      2 usos   ->  Buffer.from()
require('webos') 15 usos  ->  el addon nativo
```

Cinco arreglos. Compilando V8 en cambio se pelea con un build system de Python 2
para acabar con un motor de JavaScript de 2011.
