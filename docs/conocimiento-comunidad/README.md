# Conocimiento de la comunidad (webOS Archive)

Copia de la base de conocimiento de **[webOSArchive/webos-mcp][mcp]**, tomada el
10-sep-2026. 43 documentos, licencia **MIT**, copyright (c) 2026 webOS Archive
(ver `LICENSE`). **No es codigo de HP** y no forma parte del stack: esta aqui
como referencia.

El original es un servidor MCP, pensado para que Claude tenga este conocimiento
cargado en cada sesion sin que haya que reexplicarselo. Lo de aqui es solo el
texto; si se quiere la via completa, `npx webos-mcp` y un `CLAUDE.md` que lo
cargue.

## Que cubre y que no

Cubre **desarrollar apps** para el webOS de Palm/HP (2009-2012): Mojo, Enyo,
el bus, db8, empaquetado, el PDK. Explicitamente no cubre el webOS de LG.

**No cubre compilar el sistema operativo desde fuentes**, que es lo que hace
este repositorio. O sea que es complementario, no solapado.

## Lo que ya nos sirvio

- `gotchas.md` -> "Always cancel subscriptions you no longer need or they will
  keep the service running". Eso respalda haber implementado `cancel()` en
  `PalmServiceBridgeAdapter` aunque el espia no lo viera llamar nunca.
- `gotchas.md` -> no llamar a un servicio privilegiado desde una app cuyo id no
  empiece por `com.palm`. Es justo el `callerId` que pasa nuestro adaptador.
- `tls-and-networking.md` -> como resolvieron ellos el TLS moderno sobre el
  stack viejo. Prior art para cuando el navegador tenga que salir a internet.

## Contexto

Viene del mismo grupo que publico **webOS CE 3.1.0** (3-sep-2026), que es un
Doctor binario para TouchPad: el rootfs de HP 3.0.5 repacado con 14 anos de
trabajo de la comunidad encima, con LunaCE y OpenSSL 1.1.1w. **Nada se
recompila** para ese release. Va por un carril distinto al de este repo, que
compila el codigo original de HP sobre Linux moderno.

[mcp]: https://github.com/webOSArchive/webos-mcp
