# QtWebKit 5.212 en Debian sid (gcc 16, C++17, libxml2 2.15)

`qtwebkit-5.212-debian-sid.patch` se aplica sobre el arbol de
`qtwebkit/qtwebkit` en el commit **756e1c8f23dc2720471298281c421c0076d02df8**
(27-may-2024, rama 5.212 tras el ultimo release).

    cd build-modern/third-party/qtwebkit
    git apply ../../../patches/qtwebkit-5.212-debian-sid.patch

QtWebKit hace falta porque `webappmanager` y `BrowserServer` hablan por
**NPAPI**, que es la frontera que Palm dibujo entre el shell y el motor web.
QtWebEngine (Chromium) no expone NPAPI, asi que no sirve de reemplazo.

## Que cambia y por que

**1. C++11 -> C++17** (`Source/cmake/OptionsCommon.cmake`)
El arbol pide `-std=c++11`; los headers de Qt 5.15 y de libstdc++ 16 ya no
compilan bajo esa bandera.

**2. Guardas de los polyfills de C++14** (`Source/WTF/wtf/StdLibExtras.h`)
WTF define su propio `std::make_unique`, `std::exchange`, etc. Con C++17
esas definiciones chocan con las de verdad. Las guardas pasan a mirar
`__cplusplus < 201402L` en vez de asumir que no existen.

**3. `#include <cstdint>` en woff2** (23 archivos)
libstdc++ 13+ dejo de arrastrar `<cstdint>` por transitividad. `uint8_t` y
compania quedan sin declarar.

**4. `Annotation#=~` en el ensamblador offline** (`offlineasm/parser.rb`)
Ruby 3.2 quito `Object#=~`. El parser de LLInt lo invoca sobre nodos
`Annotation`; se le devuelve `nil`, que es lo que hacia el metodo heredado.

**5. `const xmlError*` en los callbacks de XSLT** (`XSLTProcessor.h`,
`XSLTProcessorLibxslt.cpp`)
libxml2 2.12 volvio const el parametro de `xmlStructuredErrorFunc`. La firma
de `XSLTProcessor::parseErrorFunc` tiene que seguirla o el puntero no
convierte. Se resuelve con un typedef atado a `LIBXML_VERSION >= 21200`, para
que el parche siga sirviendo en distros con libxml2 vieja.

Los puntos 3, 4 y 5 son de la misma familia: **no son errores de QtWebKit**,
son APIs que se movieron debajo. El 1 y el 2 si son deuda del arbol.
