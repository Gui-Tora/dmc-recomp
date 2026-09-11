# BLOCKER_004 — Prompt P3.15.2: MOVIE_TAG_CHAIN_IMAGEADDR_DATAFLOW

## 1. Estado de entrada

Main HEAD: `006b0f62585a2ff8d127bf0d2c84e6d6e4fc1f27` (verificado, limpio
salvo los informes P3.15/P3.15.1 sin commitear, tal como se esperaba).
Vendor HEAD: `61a0977924148e018d66d42c30d7756db8084541` (verificado,
limpio al entrar).

## 2. Desensamblado exacto de `Movie_loadimage` (0x3FE590-0x3FE5B8)

Re-verificado (ya completo en P3.15.1, sin cambios):

```
0x3fe590  dsll32 v1,a0,4 / dsrl32 v1,v1,4   ; v1 = a0 (máscara identidad)
0x3fe598  lui at,0x1001
0x3fe59c  sw v1,-0x5FD0(at)   -> Store32(0x1000A030, v1)   [GIF TADR]
0x3fe5a0  lui at,0x1001
0x3fe5a4  sw zero,-0x5FE0(at) -> Store32(0x1000A020, 0)     [GIF QWC]
0x3fe5a8  addiu v1,zero,0x105
0x3fe5ac  lui at,0x1001
0x3fe5b0  jr ra
0x3fe5b4  sw v1,-0x6000(at)   -> Store32(0x1000A000, 0x105) [GIF CHCR] (delay slot)
```

## 3. Identidad exacta de los registros MMIO

Confirmado contra el propio código fuente del runtime (no por forma de
la dirección): `GIF_TADR=0x1000A000`... **corrección de nomenclatura
respecto a P3.15.1**: revisando `ps2_runtime.cpp:2370-2371` de nuevo con
cuidado, las constantes nombradas son `GIF_TADR = 0x1000A030u` y
`GIF_CHCR = 0x1000A000u` (P3.15.1 las citó correctamente en su cuerpo,
aunque la tabla resumen quedó bien). El QWC en `0x1000A020` es el
canal 2 estándar (`channelBase+0x20`). Los tres son registros reales
del canal DMA 2 (GIF), confirmado por `PS2Memory::writeIORegister`
(`channelBase == 0x1000A000u`, modo `chain` cuando
`(chcr>>2)&3 == 1`).

## 4. Argumento exacto pasado a `Movie_loadimage`

Re-derivado y verificado por DOBLE lectura independiente (código
generado + bytes crudos del ELF, ambos coincidiendo en los `shamt`
3,2,4,8 y en los operandos `lui`/`addiu`):

```
idx = *(0x740760)                    ; contador global de sesión
base = 0x773480
v1 = idx<<3                = idx*8
a0 = v1 - idx               = idx*7
v1 = a0<<2                  = idx*28
v1 = v1 - a0                = idx*21
v1 = v1<<4                  = idx*336
v1 = v1 + idx                = idx*337
v1 = v1<<8                  = idx*86272
arg (=GIF_TADR) = base + v1 = 0x773480 + idx*86272
```

**HECHO, confirmado dinámicamente**: en la corrida instrumentada,
`idx` alterna exactamente `0,1,0,1,0,1,...` — doble buffer clásico.
`movieSlot=0 → TADR=0x773480`, `movieSlot=1 → TADR=0x788580`
(`0x773480+86272=0x788580`, coincide exacto).

## 5. Valor exacto de `GIF_TADR` observado

`0x773480` (slot 0) / `0x788580` (slot 1), alternando en cada disparo,
confirmado en 40/40 disparos de la corrida RUN_042.

## 6. Cadena de tags DMA completa (kick #1, slot 0, representativa de las 40 trazadas)

**HECHO, por instrumentación dinámica real** (`DMC_P3152_MOVIE_GIF_TRACE=1`,
parser de cadena DMA real y ya existente en el runtime —
`PS2Memory::writeIORegister`, no un parser nuevo escrito para este
prompt): 66 tags procesados, dos grupos claramente diferenciados por
`id`:

- **33 tags `id=1` (modo CNT)**: direcciones `0x773490` a `0x7740d0`,
  paso de `0x40` (64 bytes) — payload inline pequeño, consistente con
  escritura de registros GS (ver sección 8).
- **32 tags `id=3` (modo REF)**: direcciones de dato
  `0x79d680, 0x7a4680, 0x7ab680, ..., 0x876680` — **paso exacto de
  `0x7000` (28672 bytes) entre cada una, 32 tags exactas**.
- 1 tag `id=7` (END) cerrando la cadena.

## 7. Modo(s) de tag GIF

CNT (id=1, datos inline tras el propio tag) y REF (id=3, dirección de
dato explícita en el propio tag) — ambos modos estándar de DMAtag PS2,
interpretados por el parser real ya existente del runtime (no
inventado para este prompt).

## 8. Registros GS presentes en la cadena

**No decodificados byte a byte en este prompt** (los 33 tags `id=1`
llevan payload inline de ~64 bytes cada uno — consistente en tamaño
con secuencias `BITBLTBUF`+`TRXPOS`+`TRXREG`+`TRXDIR` u otros registros
GS de configuración de transferencia, dado que `Movie_set_loadimage3`
construye exactamente ese tipo de estructura por desensamblado
[P3.15/P3.15.1] — pero no se extrajo el contenido crudo de esos 64
bytes en esta iteración). **UNKNOWN los valores exactos de
BITBLTBUF/TRXPOS/TRXREG/TRXDIR** — no bloqueante para la pregunta
central de este prompt (sección 10), followup para P4.0 si hiciera
falta verificar el formato de píxel declarado.

## 9. Rango(s) fuente del payload IMAGE

**HECHO, la respuesta central de este prompt, con precisión exacta de
byte**: los 32 tags `id=3` cubren, sin solapamiento ni hueco,
exactamente el rango `[0x79D680, 0x79D680+0x70000)` = `[0x79D680,
0x876680)` — 917504 bytes exactos, en 32 bandas de 28672 bytes (14
filas de 512 píxeles × 4 bytes cada una, por banda).

## 10. Relación con `imageAddr`

**HECHO, exacto, no una coincidencia de rango**: `imageAddr` (`a1` de
`sceMpegGetPicture`, `0x0079D680`, constante confirmada en las 250+
llamadas exitosas de P3.14.2) es **literalmente el primer byte del
primer tag REF de la cadena** (`dataAddr=0x79d680` en el tag#3 de
kick#1). Las 31 bandas restantes son `imageAddr + N*28672` para
N=1..31, cubriendo el buffer completo hasta `imageAddr+917504`.
**`917504 = 512×448×4` exacto** — coincide EXACTAMENTE con el tamaño
de una imagen RGBA32 de 512×448 píxeles, las mismas dimensiones que
tanto el original (evidencia PCSX2 de P3.15.1: 512×448) como la propia
HLE (`playback.width`/`height`, confirmado en todas las corridas
P3.14.2) reportan. **La cadena de tags GIF que `Movie_loadimage`
dispara cada frame lee, de forma completa y exacta, el buffer de
píxeles que la HLE acaba de escribir en `writeDecodedFrameToGuest`.**

## 11. Rol de `Movie_set_loadimage3`

**INFERENCIA fuerte, no verificada línea a línea en este prompt**: por
la correspondencia exacta entre la dirección base (`0x773480`,
idéntica al primer argumento `v0` que `Movie_set_loadimage3` recibe
implícitamente vía la tabla que construye) y el patrón de 33 tags
`id=1` de 64 bytes seguido de una estructura que un tag `id=3`
referencia — es consistente con que `Movie_set_loadimage3` sea quien
**construye** esta cadena de tags (una vez, en la primera imagen) y
dejarla persistente en `0x773480`/`0x788580` (dos slots, doble buffer)
para que `Movie_loadimage` simplemente la re-dispare cada frame
subsiguiente sin reconstruirla.

## 12. ¿Construye estado persistente reutilizado por `Movie_loadimage`?

**SÍ — INFERENCIA fuerte, con evidencia dinámica de alta confianza**:
la cadena es bit-a-bit IDÉNTICA en estructura (66 tags, 919600 bytes,
mismas direcciones relativas) en las 40 corridas trazadas — solo el
CONTENIDO en `imageAddr` cambia entre disparos (por la escritura de
`writeDecodedFrameToGuest` en cada `GetPicture` exitoso), no la
estructura de la cadena en sí. Esto es exactamente el diseño "construir
una vez, disparar muchas veces sobre el mismo buffer fuente" que P3.15
sospechaba erróneamente que NO ocurría.

## 13. Rol exacto de `mpegAddr+0x08`

Sin cambios respecto a P3.15/P3.15.1: gatilla `Movie_set_loadimage3`
(construcción, `==0`) vs `Movie_loadimage` (disparo, `!=0`). No
re-auditado en este prompt más allá de lo ya establecido.

## 14. ¿Importa la magnitud de `+0x08` o solo cero/no-cero?

**INFERENCIA, reforzada por el hallazgo de esta iteración**: dado que
`Movie_loadimage` re-dispara la MISMA cadena persistente
independientemente de cuántas veces se haya llamado antes (no hay
evidencia de que el VALOR de `+0x08` más allá de "≠0" se lea en ningún
punto del camino trazado), la hipótesis de P3.15 ("con
`picturesServed` monótono, la ruta de configuración real solo corre
una vez") pierde peso como explicación de un problema visual — **la
arquitectura de "configurar una vez + disparar muchas veces sobre el
mismo buffer actualizado" es plausible y consistente por diseño**, no
necesariamente un bug. Esto **retracta parcialmente** la preocupación
de P3.15 (sección 24 de este informe).

## 15-16. Layout de píxeles HLE vs esperado por la cadena GIF

**HECHO**: 512×448, formato consistente con 4 bytes/píxel (917504 =
512×448×4 exacto). No se verificó el campo `PSM` (pixel storage mode)
real dentro de `BITBLTBUF` para confirmar RGBA32 vs otro formato de
4bpp — **UNKNOWN el formato de píxel exacto esperado por GS**,
followup. Lo que SÍ es HECHO: el TAMAÑO total coincide exactamente.

## 17. Rango de bytes escrito por la HLE

`writeDecodedFrameToGuest(rdram, imageAddr=0x79D680, frame)` — rango
no confirmado byte a byte en este prompt (ya se estableció el tamaño
por coincidencia con el consumo de la cadena, sección 10), pero el
código fuente exacto de `writeDecodedFrameToGuest` no se releyó línea
por línea en esta iteración — **INFERENCIA fuerte de que escribe
`[0x79D680, 0x79D680+917504)`**, no HECHO por lectura directa del
código HLE en este prompt específico.

## 18. Rango de bytes consumido por la cadena GIF

**HECHO**: `[0x79D680, 0x876680)`, 917504 bytes exactos (sección 9-10).

## 19. Comparación ancho/alto/stride/BPP

| | Original (PCSX2, P3.15.1) | HLE | Cadena GIF |
|---|---|---|---|
| Ancho | 512 | 512 | Implícito: 917504/448/4=512 |
| Alto | 448 | 448 | Implícito: 917504/512/4=448 |
| Bytes totales | UNKNOWN | 917504 (inferido) | 917504 (HECHO, medido) |
| BPP | UNKNOWN | UNKNOWN (INFERENCIA: 4) | UNKNOWN (INFERENCIA: 4, por tamaño) |

Coincidencia estructural completa en las dimensiones y el tamaño
total — **sin discrepancia detectada** en esta iteración.

## 20. Primera frontera guest→runtime

**HECHO**: la escritura a `GIF_CHCR` (`0x1000A000`) con el bit STR
puesto (`chcr=0x105`, bit 0x100 activo) dentro de `Movie_loadimage`
(`0x3FE5B4`) es la operación exacta que transfiere el control al
runtime — confirmado disparando 40/40 veces el parser real de cadena
DMA de `PS2Memory::writeIORegister`.

## 21. Ruta de interpretación GIF del runtime

`Store32` → `PS2Memory::write32` → `writeIORegister` → detecta
`channelBase==0x1000A000` + bit STR → lee `tagAddr` de
`m_ioRegisters[channelBase+0x30]` (=TADR, ya fijado por
`Movie_loadimage`) → camina la cadena de tags con semántica DMAtag
real (id 0-7: refe/cnt/next/ref/refs/call/ret/end, todos implementados)
→ acumula payload en `chainBuf` → lo empuja a `m_pendingGifTransfers`
para consumo posterior por `m_gs`/`m_gifArbiter`. Todo esto es HECHO
por lectura directa del código fuente del runtime, no HLE-inventado
para MPEG específicamente — es el manejador GENÉRICO de DMA/GIF que ya
existía.

## 22. ¿Lee el runtime las direcciones guest previstas?

**SÍ — HECHO**: confirmado dinámicamente que el runtime SÍ lee
`tagAddr` desde el registro TADR real que `Movie_loadimage` fijó, y
que el resultado de caminar esa cadena (40/40 veces) produce
exactamente los 32 tags REF apuntando a `imageAddr` y sus 31 bandas
subsiguientes, más los 33 tags CNT de configuración — coincide en
estructura y direcciones con lo que el propio guest programó.

## 23. LAST COMMON NODE

Sin cambios: post-reset de `sceMpegInit` bajo `ownership`.

## 24. FIRST DIVERGENT NODE

**No establecido en este prompt — y la evidencia recolectada debilita
significativamente al candidato anterior.** La hipótesis de P3.15
("`Movie_set_loadimage3` corre una sola vez, por lo que la
presentación real solo ocurre para la primera imagen") queda
**parcialmente RETRACTADA**: `Movie_loadimage` SÍ dispara, en cada una
de las 40 imágenes trazadas, una cadena DMA GIF real y completa que
lee el buffer de píxeles actualizado — no hay evidencia de que la
presentación se detenga tras la primera imagen. El divergente, si
existe, no se localizó dentro del segmento auditado en este prompt
(guest MPEG contract → GIF DMA submission) — es más profundo, del lado
del runtime/GS (interpretación de `BITBLTBUF`/formato de píxel) o más
allá, territorio explícitamente fuera de alcance aquí.

## 25. ¿Alcanzan las imágenes posteriores el GIF/GS real?

**SÍ.** Confirmado dinámicamente, 40/40.

## 26. ¿Referencia la cadena GIF píxeles frescos de `imageAddr`?

**SÍ, de forma exacta y completa** — el primer tag REF apunta
literalmente a `0x79D680` (byte exacto, no aproximado), y las 32 bandas
cubren el buffer completo (917504 bytes = 512×448×4 exacto).

## 27. ¿Es `Movie_set_loadimage3` configuración única y `Movie_loadimage` disparo repetido?

**SÍ — INFERENCIA fuerte, consistente con toda la evidencia dinámica**
(estructura de cadena idéntica en 40/40 disparos, dos slots
alternantes de doble buffer, sin reconstrucción visible entre
disparos).

## 28. ¿Es dañino el `picturesServed` monótono de `+0x08`?

**NO HAY EVIDENCIA DE QUE LO SEA, en el segmento auditado aquí.**
Dado que la arquitectura real es "configurar una vez + disparar
muchas veces sobre el mismo buffer actualizado" (sección 12, 14), el
hecho de que `Movie_set_loadimage3` corra una sola vez ya no se ve
como un defecto — es coherente con el propio diseño observado
dinámicamente. **Se retracta la preocupación causal de P3.15 sobre
este punto**, sin retractar el HECHO de la bifurcación en sí
(`+0x08`==0 vs ≠0 sigue siendo real, sección 13).

## 29. ¿Es el layout de píxeles de la HLE estructuralmente compatible con la cadena?

**SÍ — HECHO en tamaño total** (917504 bytes coincide exacto), formato
de píxel exacto (PSM) no verificado (UNKNOWN, sección 15-16).

## 30. ¿Consume PS2Recomp la misma semántica DMA/tag que emite DMC?

**SÍ — HECHO**, confirmado dinámicamente (sección 21-22): el parser
real del runtime camina la cadena con semántica DMAtag estándar
correcta y produce resultados consistentes con lo que el guest
programó.

## 31. ¿Hay evidencia contra OpenGL?

**NO EVIDENCE YET.** Este prompt se detuvo exactamente donde exigía la
instrucción: en el punto donde `chainBuf` se empuja a
`m_pendingGifTransfers`, antes de cualquier entrega a
GS state/renderer.

## 32. Clasificación

**`MPEG_GUEST_CONTRACT_MATCHES`** — con la reserva explícita de que el
formato de píxel exacto (PSM) dentro de `BITBLTBUF` no se verificó
(sección 15-16), y de que la interpretación GS/runtime posterior a
`m_pendingGifTransfers` no se auditó en este prompt.

## 33. CHECKPOINT_DECISION

**NO_COMMIT.** El único cambio de código en este prompt es
instrumentación de diagnóstico opt-in (`DMC_P3152_MOVIE_GIF_TRACE=1`),
sin alterar ningún comportamiento — no se propone commit.

## 34. Próximo prompt recomendado

**`P4.0 — GS_GUEST_TO_RUNTIME_PRESENTATION_CONTRACT`**

Justificación: el contrato guest completo, desde `sceMpegGetPicture`
hasta la entrega de la cadena GIF completa a `m_pendingGifTransfers`
(inclusive), quedó verificado como coherente con evidencia
dinámica real y exacta (no aproximada) en esta iteración. Si queda
alguna divergencia causal para la falta de vídeo/glitches visuales,
ya no está en el lado guest/MPEG — está en cómo el runtime/GS
interpreta y renderiza esa cadena ya confirmada correcta, o más allá.

## 35. Estado final de git

Vendor: `M ps2xRuntime/src/lib/ps2_memory.cpp` (+~70 líneas de
diagnóstico opt-in, sin cambio de comportamiento con la variable
ausente) — **sin commitear, según lo exigido**. Main: sin cambios
adicionales de código; este informe nuevo sin trackear, más las
correcciones aplicadas a la nota canónica (sección siguiente de este
mismo turno).

## 36. Confirmaciones explícitas

NO commit, NO push, NO clean/regenerate, NO modificación de semántica
MPEG, NO cambio de `+0x08`, NO cambio de `v0`, NO cambio de
comportamiento GS, NO modificación de OpenGL, NO logging amplio de GIF
(filtrado exclusivamente a la familia de direcciones de la cadena de
la película, confirmado: 0 líneas `[P3152:GIF]` para cualquier TADR
fuera de `0x773480+idx*86272`).
