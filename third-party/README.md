# third-party — pineado, no vendoreado

Estos cuatro **no** viven en el repo. Son cientos de MB que nadie va a editar:
meterlos convertiría el clon en algo que nadie quiere hacer.

Se consumen como artefactos en una ref fija, y se cachean localmente
(`~/.cache/webos-ce/`) para que un rebuild no vuelva a bajarlos.

| Qué | Origen | Ref | Nota |
|---|---|---|---|
| Qt 4.8 (fork de HP) | `openwebos/qt` | `submissions/4` | Qt de HP, con `qmake-palm` y `moc-palm` |
| WebKit (fork de Isis) | `isis-project/WebKit` | `0.54` | ~800 MB comprimido. Trae los LayoutTests |
| cmake | `cmake.org` | `2.8.7` | **Solo el tarball de fuente**: el binario precompilado ya no existe (404) |
| leveldb | `google/leveldb` | `v1.9` | Google Code cerró en 2016. El tarball de GitHub se extrae como `leveldb-1.9`, no `leveldb-1.9.0` |

## Por qué WebKit no se puede simplemente sustituir

El `desktop.pri` de HP espera un WebKit **con V8 y con el bridge de servicios de Palm**
(`ENABLE_PALM_SERVICE_BRIDGE`). No es un WebKit de la época cualquiera: es el de Isis
con los parches de HP. Cambiarlo por uno vivo es parte de modernizar el toolchain,
que es otro proyecto.
