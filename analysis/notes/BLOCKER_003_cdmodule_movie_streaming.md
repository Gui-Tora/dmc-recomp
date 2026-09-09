# BLOCKER_003 — Missing CDMODULE movie/PSS streaming HLE

## Estado

`OPEN / CAUSE CONFIRMED`, implementación no iniciada.

No es un bug de pantalla negra, ni un bug de `demo1.pss`, ni de `title.pss`.
La causa es general y afecta a cualquier reproducción de película que use
este protocolo:

**El protocolo de streaming de `CDMODULE.IRX` para películas PSS (`fno`
`9`/`0xA`/`0xC`/`0xD`) no está implementado en `CdModuleService` de RECOMP.**
`CdModuleService` únicamente maneja `fno=2` (lectura simple, contrato
cerrado en BLOCKER_002). Las llamadas de movie caen en el *fallback*
genérico de RPC no manejada.

## Cadena causal

### Original (PCSX2, verificado por desensamblado + captura dinámica)

```
fno=9  (CdMovieReadProcess, EE call site 0x1CECB8, compartido con fno 2/3/7)
  -> arranca/inicializa el pipeline de movie (semáforos si hace falta)
  -> IOP Thread_movie ejecuta CdMovieReadProc: CDFileOpen(request+4=LBA),
     lee el PSS del CD hacia el buffer interno Trans_buffer
  -> IOP Thread_trans ejecuta CdMovieTransProc: DMA real
     Trans_buffer -> dest EE

fno=0xA (CdMovieTransProcess, EE call site 0x1CEDAC, propio, NO compartido
         con 2/3/7/9)
  -> "siguiente chunk": SignalSema(Sema_trans) + espera + DMA real
  -> devuelto al EE como EE_TransSize

fno=0xC (EE call site 0x1CEDE0)
  -> MovieExit (IOP) — cierre/reset del pipeline

fno=0xD (EE call site 0x1CEE18, compartido con fno=8)
  -> CdMovieProcessEndCheck (IOP) — OR de los handles de
     Sema_cd/Sema_ready/Sema_trans; "¿sigue vivo el pipeline?"
```

### Evidencia dinámica (PCSX2, sesión interactiva real)

- **`title.pss`**: varias llamadas sucesivas a `fno=0xA` (breakpoint
  `0x001CEDAC`) ocurren mientras la pantalla sigue completamente negra;
  tras varias más aparece el aviso "WARNING: This game contains scenes of
  explicit violence and gore." — confirma una fase real de *buffering*, no
  una carga monolítica.
- **`demo1.pss`**: `request+4 = 0x0021E73D`, coincide exactamente con la
  LBA real de `/DATA/MOVIE/DEMO1P.PSS` en la ISO (verificado por parseo
  directo del árbol ISO9660). `dest = 0x01CC37E0`/`0x01CB37E0` (title.pss).
- **Contenido de `dest`**: comparación byte a byte contra el archivo real
  de la ISO — coincidencia exacta de 32/32 bytes en un offset, y 46/47
  bytes en otro (la única discrepancia es una lectura de un byte del
  hexdump, no un dato real distinto), en offsets de archivo **crecientes**
  con el tiempo de reproducción (`0x1870000` → `0x36C0000`, sobre un
  archivo de `0x36C8004` bytes). Descarta coincidencia por azar.

### RECOMP actual

`vendor/PS2Recomp/ps2xIOP/src/modules/cdmodule.cpp`:

- `CdModuleService::handleRpc` solo procesa `request.function ==
  m_bindings.readFunction` (`fno==2`); para `fno` 9/A/C/D devuelve
  `RpcResult{}` vacío.
- `vendor/PS2Recomp/ps2xRuntime/src/lib/Kernel/Syscalls/RPC.cpp:566-660`
  (`finishCall`): con `handled=false` y buffers de envío/recepción no
  nulos, copia los primeros `min(sendSize, receiveSize)` bytes del propio
  *send* packet al *receive* buffer (eco de basura del propio header, no
  una respuesta con sentido) y pone `busy=false` de forma síncrona e
  inmediata.
- No se crea ningún estado de sesión de movie, no se abre el archivo PSS,
  no se ejecuta ninguna transferencia hacia `dest`, no hay `EE_TransSize`
  real.

**No existe ruta alternativa que alimente el stream.** Se revisó
`vendor/PS2Recomp/ps2xRuntime/src/lib/Kernel/Stubs/IPU.cpp` (emulación de
registros IPU): es pasiva — consume lo que ya haya en RAM EE vía DMA, no
obtiene bytes del disco por sí sola. Ningún otro servicio está registrado
para `sid=0x12345678` aparte de `CdModuleService`.

## Correcciones que quedan fijadas (para no repetir errores de reconstrucción)

1. **`CallCdModule` (EE) tiene su propia tabla de salto**, indexada por
   `fno` crudo (0-13, guardada con `sltiu at,a1,14`), base `0x584D10` —
   **no** es el mismo dispatcher fno-1 del lado IOP.

   | `fno` | EE call site (`jal sceSifCallRpc`) |
   |---|---|
   | 0, 4, 0xB | sin RPC (salida inmediata) |
   | 1 | `0x1CEB8C` |
   | 2, 3, 7, 9 | `0x1CECB8` (compartido) |
   | 5 | `0x1CED58` |
   | 6 | `0x1CED08` |
   | 8, 0xD | `0x1CEE18` (compartido) |
   | 0xA | `0x1CEDAC` (propio) |
   | 0xC | `0x1CEDE0` (propio) |

2. **El cierre real de movie es `fno=0xC`** (→ `MovieExit` en el IOP), **no
   `fno=0xB`** — `fno=0xB` no emite ninguna RPC desde el lado EE.

3. **`0x87DBF0` = `SifRpcClientData_t`** (estructura del SDK, ya modelada
   en RECOMP como `t_SifRpcClientData`,
   `ps2xRuntime/src/lib/Kernel/Syscalls/Helpers/State.h:53`, `sizeof==0x28`).
   `+0x04` (`0x87DBF4`) = `hdr.rpc_id`, un contador de secuencia **genérico
   de SIF**, incrementado por cualquier `sceSifCallRpc` del sistema (pad,
   memory card, CD, lo que sea). **No** es contador de frame, timestamp ni
   posición de movie — se descartó como marcador tras identificarlo en
   código.

4. **`0x87DC80`** = el packet/*scratch* de `CallCdModule` (`send`,
   112 bytes) — reutilizado entre llamadas, no es estado de streaming.

5. **`0x87DCB0`** (`= 0x87DC80 + 0x30`) = *string* de depuración generado
   por `sprintf` dentro de `CallCdModule` — descriptivo, no operativo.

## Contrato mínimo general del futuro HLE (NO implementado todavía)

Debe funcionar para **cualquier** PSS que use este protocolo — prohibido
condicionar por nombre de archivo (`if title.pss` / `if demo1.pss`).

| `fno` | Contrato observable mínimo |
|---|---|
| `9` (start) | Registra LBA inicial (`request+4`), posición actual = LBA inicial, marca la sesión "activa" |
| `0xA` (next chunk) | Lee `dest` de `request+0xC` **en cada llamada** (no cachear del `fno=9`); tamaño de chunk = `request+8` (confirmado por código, ver abajo); copia esa cantidad exacta de bytes reales desde la posición actual del PSS hacia `dest`; avanza la posición; devuelve `request+8` como `EE_TransSize` (eco confirmado por código) |
| `0xC` (close) | Cierra/resetea el estado de sesión |
| `0xD` (status) | Refleja si la sesión sigue viva (equivalente observable al OR de semáforos original) |
| `8` | ABIERTO si participa en el flujo de movie — no observado todavía en las capturas de title/demo1 |

**No hace falta reproducir hilos/semáforos IOP literalmente** — una máquina
de estados HLE (LBA + posición + tamaño de archivo) puede dar la misma
superficie observable al EE.

**Prohibido**: copiar el PSS completo de una vez en `fno=9` — contradice
directamente la evidencia (pantalla negra durante varios `fno=0xA` antes
de imagen real, `dest` con contenido que avanza chunk a chunk).

### Ya demostrado por código (sin necesidad de más capturas PCSX2)

Desensamblado de `CdMovieTransProc` (IOP, `CDMODULE.IRX` offset `0xF38`,
instrucciones `0xFB8`-`0x1060`):

```
dma.dest (sceSifSetDma)     = *(request+0x0C)   ; leído de nuevo en CADA llamada
dma.qwc  (sceSifSetDma)     = *(request+0x08)   ; SIN redondear, valor directo
EE_TransSize (tras DMA)     = *(request+0x08)   ; eco exacto de lo pedido
```

Por tanto: **`s0+0x08` (`= *(s3+64)` en `CallCdModule` para `fno=0xA`) es
el tamaño de chunk solicitado por el llamador EE, usado directamente como
tamaño de DMA** — confirmado por código, ya no es una asunción. El valor
de retorno al EE es ese mismo tamaño, siempre (no hay señal de
transferencia parcial en esta ruta).

## Mediciones que faltan antes de implementar

1. **Valor(es) numérico(s) concretos** que el juego envía en `request+8`
   para `fno=0xA` durante una reproducción real — la fórmula ya está
   demostrada por código, falta el número real para dimensionar/validar
   una implementación.
2. **Si `fno=0xD` y/o `fno=8` aparecen realmente** durante la reproducción
   de `title.pss`/`demo1.pss`, o si el llamador nunca los usa en este
   flujo — call site común `0x001CEE18`, distinguir por `a1`.
3. **Si `dest` permanece siempre fijo por sesión de movie**, o si puede
   rotar en otros streams/llamadores — solo se observó fijo en las dos
   sesiones capturadas hasta ahora.

## Siguiente prueba PCSX2 propuesta (una sola sesión, dos breakpoints)

Mantener `0x001CEDAC` (`fno=0xA`) y añadir `0x001CEE18` (`fno=8`/`0xD`,
distinguir por `a1`). Dejar correr una reproducción completa (o al menos
hasta la aparición de imagen) y registrar, en cada hit:

- breakpoint disparado y valor de `a1`,
- `[s0+0x08]` y `[s0+0x0C]`,
- valor de `$v0` al volver del `jal` (return value real),
- si `dest` cambia entre hits.

Esto cierra las tres mediciones pendientes en una sola pasada.
