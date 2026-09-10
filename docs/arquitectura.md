# Arquitectura de webOS

![Arquitectura de webOS](arquitectura-webos-palm.png)

Diapositiva de una presentacion de **Palm**, de la epoca de este codigo. Se conserva
como referencia de estudio: es el mapa contra el que se puede ubicar cada componente
del monorepo.

## Como se mapea sobre este repo

| Caja del diagrama | Componentes | Estado en Debian moderno |
|---|---|---|
| **Palm Bus** (la columna naranja) | `luna-service2` | compila |
| **OS Services / OS Middleware** | `pmloglib`, `nyx-lib`, `libsandbox`, `jemalloc`, `filecache`, `db8`, `configurator`, `luna-prefs`, `luna-init`, `librolegen`, `pmstatemachineengine` | compilan |
| **App Services** | `luna-universalsearchmgr` (compila), `mojomail`, `activitymanager` | bloqueados |
| **UI System Manager** | `luna-sysmgr-ipc`, `luna-sysmgr-ipc-messages` (compilan) y **`LunaSysMgr`** | pendiente: Qt |
| **Browser / DocViewers** (por NPAPI) | `BrowserServer`, `BrowserAdapter`, WebKit | sin tocar |
| **Media / Wireless** | -- | HP nunca las libero |
| Todo bajo *Kernel/User Space Boundary* | -- | no se carga: no hay kernel ni drivers |

## Tres cosas que el diagrama aclara

**El bus es la columna vertebral, literalmente.** Esta dibujado como la barra
vertical a la que se conecta todo. Sobrevivio intacto hasta el webOS de LG.

**"UI System Manager" son cuatro cajas en un solo proceso.** Window Manager,
Window Server, Mojo Framework y Application Manager viven todos dentro de
`LunaSysMgr`, y se corresponden con sus archivos mas grandes:
`ApplicationManagerService.cpp` (4536 lineas), `WindowServerLuna.cpp`, `WindowServer.cpp`.

**NPAPI es la frontera con el navegador.** El diagrama dibuja Browser y DocViewers
FUERA del UI System Manager. Por eso `LunaSysMgr` no incluye una sola cabecera de
WebKit y habla con el motor por IPC: `BrowserAdapter` es, textualmente, un plugin
NPAPI. Consecuencia practica: el motor web se puede reemplazar sin tocar el gestor
de ventanas.

**Y donde estan los huecos:** las cajas *Media* y *Wireless* son justo donde viven
`media-api`, `hid` y `hal` -- las librerias que HP nunca libero. El diagrama dibuja
las cajas; en el release del CE estan vacias. Ver [estado-del-codigo.md](estado-del-codigo.md).
