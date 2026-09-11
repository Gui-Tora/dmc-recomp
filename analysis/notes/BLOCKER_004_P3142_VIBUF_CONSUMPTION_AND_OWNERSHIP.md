# BLOCKER_004 — Prompt P3.14.2: GUEST_VISIBLE_VIBUF_CONSUMPTION_AND_DECODE_OWNERSHIP

Corrige los dos defectos BLOQUEANTES que la auditoría adversarial P3.14.1
(Fable) encontró en P3.14: (1) el productor guest se congelaba
determinísticamente a ~520 KiB porque ningún consumo guest-visible del
`viBuf` existía nunca, y (2) el camino de decode asíncrono seguía cableado
en paralelo al síncrono, con riesgo real (no solo teórico) de destruir el
decoder síncrono. Ver
[BLOCKER_004_P314_SYNCHRONOUS_GUEST_ES_REDECODE.md](BLOCKER_004_P314_SYNCHRONOUS_GUEST_ES_REDECODE.md)
y
[BLOCKER_004_P3141_FABLE_SYNC_MPEG_AUDIT.md](BLOCKER_004_P3141_FABLE_SYNC_MPEG_AUDIT.md).

## 1. Estado de entrada

- Main HEAD: `3483f92de5ed2e4d1875a6a962f6c0a23a421eff` (esperado, verificado).
- Vendor HEAD: `ed83ab53fa4635715eb10b72bdfe9fa7469e26cd` (esperado, verificado).
- Dirty exacto según lo declarado por el prompt, sin cambios ajenos: main
  (`M pss_video_output.md`, `?? P3141_FABLE...md`, `?? P314_SYNCHRONOUS...md`),
  vendor (`M MPEG.cpp`). Verificado antes de tocar nada.

## 2. Mapa independiente de lectores/escritores de +0x28/+0x2C0/+0x2C4/+0x2C8

Búsqueda exhaustiva sobre todo `analysis/local/symtabfirst/generated/`
(no limitada a las funciones ya conocidas por P3.14.1), vía agente de
exploración + lectura directa completa de cada función encontrada.

| Campo | Función | Acción |
|---|---|---|
| `+0x28` (videoBytesTotal) | `MovieBufferInit` (0x3fe220) | **WRITE** `=0` (init) |
| `+0x28` | `viBufEndPut` (0x47af80) | **WRITE** `+= lenWritten` (único incremento en todo el árbol) |
| `+0x2C0` (blockCursor) | `MovieBufferInit` | **WRITE** `=0` (init) |
| `+0x2C0` | `viBufBeginPut` (0x47ae90) | READ (fórmula de posición de escritura) |
| `+0x2C0` | `viBufAddDMA` (0x47b0d0) | READ + **WRITE** (retirement, ver §4) |
| `+0x2C0` | `MpegRestartDmaCallBack` (0x47b4f0) | READ + **WRITE** (mecanismo compañero, ver §4) |
| `+0x2C4` (queuedBlocks) | `MovieBufferInit` | **WRITE** `=0` (init) |
| `+0x2C4` | `viBufBeginPut` | READ |
| `+0x2C4` | `viBufAddDMA` | READ + **WRITE** (decrementa en retirement, incrementa en enqueue) |
| `+0x2C4` | `MpegRestartDmaCallBack` | READ + **WRITE** |
| `+0x2C8` (pendingBytes) | `MovieBufferInit` | **WRITE** `=0` (init) |
| `+0x2C8` | `viBufBeginPut` | READ |
| `+0x2C8` | `viBufEndPut` | **WRITE** `+= lenWritten` |
| `+0x2C8` | `viBufAddDMA` | READ + **WRITE** (`-= bloquesFormados*2048`) |
| `+0x300` (gate) | `MovieBufferInit` | **WRITE** `=1` (habilitado desde el inicio, HECHO — corrige la suposición inicial de que podía estar en 0) |
| `+0x300` | `viBufAddDMA` | READ (gate de entrada) |
| `+0x300` | `MpegRestartDmaCallBack` | **WRITE** `=1` (re-habilita al final) |

`MpegNodataCallBack` (0x47b300, la función que el prompt asumía como
lectora/escritora directa) **no toca ninguno de estos 4 offsets
directamente** — opera sobre offsets distintos (`+0x4/+0x10/+0x14/+0x18`,
bookkeeping de demux/CD) del mismo objeto `movieState`, y termina
llamando a `viBufAddDMA(movieState)`. Corrección explícita sobre la
premisa implícita del prompt.

Ningún otro archivo de los ~200 con coincidencia numérica de offset
resultó ser el `movieState` real (verificado por muestreo: usan `lq`/
`swc1`, instrucciones vectoriales/flotantes de 128 bits, sobre structs de
animación/efectos de personajes — el `movieState` real siempre se accede
vía el puntero global cacheado en `0x87D9xx`).

## 3. Significado exacto de `queuedBlocks` (+0x2C4)

Número de bloques de 2048 bytes ya convertidos en tags DMA, pendientes de
ser realmente consumidos por hardware IPU/DMA. Se incrementa cuando
`viBufAddDMA` convierte `pendingBytes` en bloques nuevos; se decrementa
cuando la misma función (o `MpegRestartDmaCallBack`) detecta, vía
`getFIFOindex`, que el hardware ya avanzó más allá de esos bloques.

## 4. Evento que REALMENTE libera capacidad del productor

Desensamblado completo de `viBufAddDMA` (0x47b0d0-0x47b2fc): dos fases.

**Fase de retirement** (0x47b144-0x47b184): llama a
`func_47AE50(movieState)` — identificada, desensamblada completa,
renombrada `getFIFOindex`:

```
v1 = *(movieState+0x68)         // tamaño de ring de DMA tags
v0 = *(movieState+0x64) + (v1<<4) + 0x10   // dirección "fin de tags válidos", normalizada a 28 bits
if (a1 == v0) return 0          // a1 == "siguiente tag, aún no usado" -> 0 bloques nuevos
return (a1 - *(movieState+0x1C)) >> 11     // (dirección_actual - base) / 2048 -> índice de bloque
```

`a1` es el segundo argumento de `getFIFOindex`, cargado en `viBufAddDMA`
como `Load32(0x1000B410)` — **una dirección fija de hardware/kernel**,
leída inmediatamente antes de la llamada. `0x1000B410`/`0x1000B400` son
direcciones de registro DMA/IPU reales (memoria baja reservada de
kernel), no campos del `movieState`. `getFIFOindex` compara esa posición
de hardware contra `blockCursor` para calcular cuántos bloques
"realmente" consumió el DMA, y **eso** es lo que decrementa `queuedBlocks`
(`v0 = *(+0x2C4) - v1; sw v0, +0x2C4`) y avanza `blockCursor`.

**Fase de enqueue** (0x47b1bc-0x47b298): convierte bloques enteros de
`pendingBytes` en nuevos tags, incrementando `queuedBlocks` y reduciendo
`pendingBytes` al remanente.

**El evento que libera capacidad es la fase de retirement — y depende de
leer un registro de hardware DMA real que esta arquitectura HLE no
emula.**

## 5. ¿`viBufAddDMA` sola libera espacio?

**NO, por dos razones independientes, ambas confirmadas:**

1. **Algebraica** (hipótesis del prompt, confirmada): mover bytes de
   `pendingBytes` a `queuedBlocks` (fase de enqueue) no cambia
   `free = (0xFE-queuedBlocks)*2048 - pendingBytes` — es exactamente la
   identidad que el prompt derivó. Solo la fase de *retirement*
   (decrementar `queuedBlocks`) libera espacio real.
2. **Estructural** (nueva, más fuerte): la fase de retirement depende de
   `getFIFOindex`, que lee un registro DMA/IPU de hardware real
   (`0x1000B410`) no emulado por esta HLE. Bajo la arquitectura actual,
   ese registro no refleja ningún progreso de consumo real — invocar
   `viBufAddDMA` (vía el guest callback real) no liberaría espacio de
   forma significativa, solo leería lo que sea que el runtime tenga en
   esa dirección fija (probablemente 0 o basura), produciendo
   retirement incoherente, no una réplica fiel del hardware original.

## 6. Ciclo de vida completo del `viBuf` original

```
StrM2vCallBack (guest, stream=true, SÍ se despacha)
    -> viBufBeginPut (calcula posición de escritura mod capacity)
    -> copia bytes
    -> viBufEndPut (pendingBytes += N; videoBytesTotal += N)
    ...
MpegNodataCallBack (guest, stream=false, NUNCA se despacha en esta HLE
                     -- confirmado por P3.14.1: el filtro de despacho
                     `callback.stream && callback.type==streamType` no
                     tiene ninguna vía para callbacks stream=false)
    -> sceMpegDemuxPssRing (demux adicional, bookkeeping +0x14/+0x18)
    -> viBufAddDMA
         -> retirement real (requiere getFIFOindex + registro DMA/IPU real)
         -> enqueue (pendingBytes -> queuedBlocks)
    -> ring recupera espacio libre
```

El eslabón `MpegNodataCallBack -> viBufAddDMA -> retirement real` es
estructuralmente inalcanzable en esta HLE (ni por el filtro de despacho
de callbacks, ni por la falta de emulación del registro DMA/IPU que
`getFIFOindex` necesita). Confirma P3.14.1 con un nivel de detalle
adicional (identificación completa de `getFIFOindex` y su dependencia de
hardware real).

## 7. Modelo elegido: MODELO B, con evidencia

**MODELO A descartado explícitamente** (no "probado coherente", como
exigía el prompt como condición para usarlo): requeriría emular un
registro DMA/IPU de hardware real (`0x1000B410`) del que no hay ninguna
implementación en esta HLE, y `MpegNodataCallBack` además ejecuta
`sceMpegDemuxPssRing` por su cuenta — despacharlo introduciría una
segunda vía de demux paralela a la ya usada por el pipeline de callbacks
del scheduler, con riesgo real de doble contabilización.

**MODELO B implementado**: espejar el efecto FINAL guest-observable del
consumo, no el mecanismo real de dos fases. Derivación algebraica
(verificada, no asumida) a partir de la fórmula real de
`viBufBeginPut`:

```
dest = base + ((blockCursor+queuedBlocks)*2048 + pendingBytes) % capacity
```

Se demuestra por sustitución directa que el efecto neto de "encolar N
bloques y retirarlos inmediatamente por completo" deja `queuedBlocks`
sin cambio neto, avanza `blockCursor` en N bloques y
reduce `pendingBytes` en N*2048 bytes — y que, bajo esa transformación,
`dest` permanece **invariante** (la posición de escritura del productor
no se ve afectada por el consumo, solo por la producción, exactamente
como debe comportarse un ring buffer correcto). Implementación:

```cpp
mirroredBlocks   = p314ConsumedEsBytes / 2048        // floor
mirroredBytes    = mirroredBlocks * 2048
blockCursor      = mirroredBlocks % dmaTagRingSize    // dmaTagRingSize=256, ver §9
pendingBytes     = producedTotal - mirroredBytes       // clamp a 0
// queuedBlocks (+0x2C4) NO se toca -- efecto neto cero, ver arriba
```

Redondea hacia ABAJO a múltiplos de 2048 (nunca hacia arriba): el
"crédito" perdido por redondeo es como máximo 2047 bytes, absoluto (no
acumulativo, se recalcula desde `p314ConsumedEsBytes` completo en cada
llamada, no por incremento) y siempre juega a favor de la seguridad (el
productor ve *menos* espacio libre del que realmente hay, nunca más).

## 8. Aritmética exacta espejada

Ver bloque de código anterior (§7) y el diff real en `sceMpegGetPicture`
(sección 17 de este informe). `queuedBlocks` se deja deliberadamente sin
tocar: la prueba algebraica de invariancia de `dest` (§7) depende
exactamente de que el efecto neto sobre `queuedBlocks` sea cero.

## 9. Prueba de propiedad del input del decoder / `dmaTagRingSize`

`dmaTagRingSize = *(movieState+0x68)`: confirmado estáticamente en
`MovieBufferInit` (`= 0x100 = 256`) y confirmado dinámicamente en las 3
corridas finales (`dmaTagRingSize=256` en cada línea `[P3142:ring]`).
Coincide exactamente con `capacity/2048 = 524288/2048 = 256`,
consistente con que el divisor de retirement (`+0x68`) y el divisor de
bloques de capacidad sean el mismo espacio modular — condición necesaria
para que la prueba de invariancia de `dest` (§7) sea válida usando
`% dmaTagRingSize` para `blockCursor`.

## 10. Comportamiento exacto del camino asíncrono bajo P3.14.2

`feedElementaryStream` (el camino async) **sigue siendo llamado desde el
mismo call site** (dentro del bucle de demux PES, inmediatamente después
de `queueStreamCallbackEvent`) — la entrega guest-visible
(`queueStreamCallbackEvent`, que dispara `StrM2vCallBack` y por tanto
`viBufBeginPut`/`viBufEndPut`) **nunca se tocó**. Lo que cambió: la
llamada a `feedElementaryStream` en sí queda condicionada a
`!playback.syncGuestEsDecodeActive`. Cuando P3.14 está activo, esa
llamada se omite (confirmado dinámicamente: 31 omisiones por corrida en
el log `[P3142:owner]`), y el async NUNCA toca `decoder`/`decodedFrames`/
`waitingForVideoSequenceHeader`/`videoSequenceSyncBuffer` para esa
`playback`.

## 11. Prueba de que el decoder síncrono es el único dueño post-init

- `playback.syncGuestEsDecodeActive` se fija en `applyP313OwnershipMode`
  (independiente del modo P3.13 elegido), a `isP314SyncRedecodeEnabled()`
  — HECHO por lectura del diff.
- El call site del async (§10) queda condicionado a esa misma bandera —
  HECHO por lectura del diff.
- Dinámicamente: `[P3142:owner] async feedElementaryStream skipped`
  aparece 31 veces en cada corrida de validación, confirmando que el
  guard se ejecuta y bloquea el camino async en la práctica, no solo en
  teoría.
- `decoder.reset()`/`decodedFrames.clear()` (las líneas 1246-1247 que
  P3.14.1 identificó como destructivas) están DENTRO de
  `feedElementaryStream`, que ahora nunca se ejecuta para esta
  `playback` — por construcción, no pueden dispararse mientras
  `syncGuestEsDecodeActive` sea verdadero.

## 12. Semántica `producedTotal`/`consumedTotal`

`producedTotal` = `videoBytesTotal` (+0x28), leído fresco cada llamada,
monótono, gobernado por el productor guest real (sin cambios). Nunca se
decrementa (confirmado exhaustivamente, §2).

`consumedTotal` = `playback.p314ConsumedEsBytes`, cursor host absoluto
desde el último `sceMpegInit`, avanzado en bloques de hasta 4096 bytes
por cada `decoder->feed()` exitoso. Reinicia a 0 en cada init (no está
en la lista de preservación de `applyP313OwnershipMode`).

## 13. Fórmula de wrap

```
physOffset = consumedTotal % capacity
```

Derivada y verificada (no asumida) en §7: dado que `dest` para el byte
en posición absoluta P es siempre `base + (P mod capacity)`
independientemente del consumo, el byte MÁS ANTIGUO aún no consumido
(posición absoluta = consumedTotal) vive físicamente en
`base + (consumedTotal mod capacity)`.

## 14. Guarda de staleness

```cpp
stale = capacity != 0 && available > capacity   // available = producedTotal - consumedTotal
```

Si se viola, el bloque completo se salta (cae a `waitExternal`, no
decodifica). **0 activaciones en las 6 corridas P3.14.2 registradas**
(RUN_033 a RUN_040) — el propio Modelo B, al liberar espacio
guest-visible en sincronía con el consumo real, evita estructuralmente
que el productor se adelante más de una capacidad completa.

## 15. Implementación de lectura partida en el borde (wrap-split)

Implementada exactamente como pide el prompt: si
`physOffset + chunk > capacity`, copia `[physOffset, capacity)` +
`[0, chunk-firstLen)` a un buffer de scratch contiguo antes de
alimentar al decoder — nunca lee más allá de `viBufBase+capacity`.

**Limitación honesta, no oculta**: en las 6 corridas de esta iteración
(RUN_033-040), esta rama de código **nunca se ejecutó** (`wraps=0` en
todas las entradas `[P3142:ring]` observadas, incluidas las de rastreo
ampliado sin capar). Causa estructural identificada: `capacity=524288`
es múltiplo exacto de `kP314ChunkBytes=4096` (524288/4096=128), y el
propio bucle de alimentación solo puede detectar "trama lista" entre
chunks completos (nunca a mitad de uno), por lo que
`p314ConsumedEsBytes` permanece siempre múltiplo de 4096 mientras el
bucle termine por "trama producida" (el caso observado en el 100% de
las llamadas exitosas). El wrap SÍ se ejerció en su forma "limpia"
(reinicio exacto a `physOffset=0`, confirmado 2 veces por corrida vía
`FIRST_WRAP`/`SECOND_WRAP`), pero la rama de copia partida específica
queda sin cobertura empírica directa en este conjunto de corridas. Se
documenta como limitación de la validación, no como defecto del código
(la rama fue revisada manualmente línea por línea y es consistente con
la misma prueba algebraica de §7).

## 16. Prueba de wrap byte-exacto — alcance real logrado

Dado que un wrap-split literal no ocurrió (§15), la prueba se hizo sobre
el caso que SÍ ocurrió: el reinicio limpio a `physOffset=0` tras cruzar
`capacity`. Se añadió un volcado de un solo uso
(`analysis/local/p3142/wrap_event_dump.bin`, capturado en RUN_038): 256
bytes físicos inmediatamente antes del borde (`[capacity-256,
capacity)`) + 256 bytes leídos en la primera llamada tras el wrap desde
`physOffset=0`. Verificación offline (Python):

- Ninguno de los dos bloques es todo-ceros (descarta lectura de memoria
  no inicializada).
- Los dos bloques de 256 bytes **no son idénticos** entre sí (descarta
  relectura accidental del mismo contenido).
- Alta entropía consistente con datos MPEG comprimidos reales (no
  patrón repetitivo, no relleno).
- **0 violaciones de staleness** en ninguna corrida (§14), lo cual es
  una garantía MATEMÁTICA (no solo empírica) de que, en el momento de
  cada lectura, el byte físico en `physOffset` todavía correspondía a
  la posición lógica esperada y no había sido sobrescrito por el
  productor — precondición necesaria y suficiente para byte-exactitud
  bajo la fórmula de §13.

**Conclusión honesta**: la corrección de la fórmula de direccionamiento
está demostrada algebraicamente (§7, §13) y corroborada empíricamente
(guarda de staleness limpia + volcado no-basura/no-duplicado). El
requisito literal del prompt ("cero bytes distintos para al menos un
intervalo envuelto", comparado contra una reconstrucción externa del ES
esperado) **no se cumplió en su forma más estricta** porque el
sub-caso que requeriría esa comparación (lectura partida a mitad de
chunk) no se pudo ejercitar con los parámetros actuales
(`kP314ChunkBytes=4096`, `capacity=524288`). Se reporta como tal, sin
inflar la evidencia disponible a una categoría que no alcanzó.

## 17. Archivos modificados

Solo `vendor/PS2Recomp/ps2xRuntime/src/lib/Kernel/Stubs/MPEG.cpp`, sobre
el HEAD `ed83ab53...`. Diff total de P3.14.2 (incluye P3.14 original de
la iteración previa, aún sin empaquetar en patch separado):
**+353/-8** líneas. Cambios de esta iteración específicamente: campo
`syncGuestEsDecodeActive` en `MpegPlaybackState`; línea de propagación
en `applyP313OwnershipMode`; guard condicional en el call site de
`feedElementaryStream` dentro del bucle de demux PES; bloque de
`sceMpegGetPicture` reescrito con direccionamiento modular,
lectura partida en el borde, guarda de staleness, mirror Modelo B,
diagnósticos `[P3142:ring]`/`[P3142:owner]` y volcado de evidencia de
wrap. `#include <fstream>` añadido.

## 18. Prueba de build acotado

Cuatro ciclos build+relink durante esta iteración (uno por cada adición
de diagnóstico), todos verificados: `grep -c "CL.exe"` = exactamente 1
en cada log de compilación (solo `MPEG.cpp`, con `PS2X_HAS_FFMPEG=1`), y
= 0 en cada log de relink (`ps2EntryRunner /t:_BuildLinkAction`, ningún
archivo de `recomp/generated/` recompilado). Backup previo tomado antes
del primer build de P3.14.2:
`analysis/local/p3142/ps2_runtime.p314_baseline.lib`
(SHA256 `275be008180ffa32f34a4009518cd2459677767301d9de3bc1b8bc08661a7c4a`).
Un build inicial apuntó por error al árbol `build/runtime/active/` (no
es el árbol que usa `run.py`) — compiló correctamente (10 `CL.exe`,
todos archivos legítimos de `ps2xRuntime/src/lib/...`, ninguno de
`generated/`, por tanto no peligroso) pero su resultado se descartó sin
usarse para ninguna validación.

## 19. SHA256 del ejecutable final

`75615aaed0e6b84a55043414f92c6bf571fa74e5a49fb855136bdab515807baf` —
usado idénticamente en RUN_038, RUN_039 y RUN_040 (la validación 3/3
formal). Builds intermedios (diagnóstico incremental, sin cambio de
comportamiento): `2126fbf1d8c30f5ce583d73fe78d59d6f74b9a351106943d3d4d5f677721d3d8`
(RUN_033-035) y un build intermedio no separadamente hasheado (RUN_036,
RUN_037-fallido).

## 20. Control por defecto (P3.14 deshabilitado)

RUN_033: `DMC_P314_SYNC_REDECODE` ausente, `[P313:init] mode=baseline`,
0 líneas `P314`/`P3142` en el log completo, 0 éxitos de `getPicture` —
comportamiento histórico reproducido exactamente, sin cambios.

## 21. Resultado del smoke test

RUN_034 (primer build P3.14.2): 17+ éxitos confirmados en la ventana
observada (el conteo real, no el diagnóstico disperso, alcanzó al
menos #350 — ver §22), `videoBytesTotal` superó 900000 (>1.7×
capacity), `FIRST_WRAP`/`SECOND_WRAP` disparados, 0 `STALE`, 0
`feedFailed`, 0 `GP:H`, 0 agotamiento de `RuntimeGuestArena`. El techo
histórico de ~517313 bytes / 10 éxitos quedó roto de forma inequívoca
en la primera corrida habilitada.

## 22. Tabla de validación 3/3 (build final idéntico)

| Campo | RUN_038 | RUN_039 | RUN_040 |
|---|---|---|---|
| exe SHA256 | `75615aae…` | `75615aae…` | `75615aae…` |
| último "getPicture success" impreso | #250 | #250 | #250 (conteo real ≥250, log disperso cada 50) |
| `FIRST_WRAP` producedTotal | 566181 | 566181 | 566181 |
| `SECOND_WRAP` producedTotal | 1075358 | 1075358 | 1075358 |
| `STALE_DATA_DETECTED` | 0 | 0 | 0 |
| `feedFailed=1` | 0 | 0 | 0 |
| `GP:H` (waitExternal) | 0 | 0 | 0 |
| `async callback stack exhausted` | 0 | 0 | 0 |
| `[P3142:owner]` omisiones async | ≥31 | ≥31 | ≥31 |
| CD/MOVIE chunk offset máximo | ≥0x1880000 | ≥0x1880000 (no confirmado exacto) | ≥0x1880000 (no confirmado exacto) |
| Imagen de película reconocible | SÍ (fuego, ver §23) | SÍ (fuego) | SÍ (fuego, mismo instante) |
| `result.json: result` | SUCCESS | SUCCESS | SUCCESS |

Reproducibilidad byte-exacta de los marcadores de wrap (566181/1075358
idénticos en las 3) confirma determinismo total del pipeline bajo el
mismo input/pad file.

## 23. Resultado visual — HALLAZGO MAYOR

Con `--visual-trace` (RUN_039, RUN_040): la secuencia deja de ser la
pantalla estática de advertencia sin cambios (como en TODAS las
iteraciones anteriores, P3.13/P3.13.1/P3.14). A partir de
~t≈80-90s (bien después del primer wrap, coincidiendo con la ráfaga
sostenida de éxitos de GetPicture) aparecen, de forma reproducible en
ambas corridas en los MISMOS timestamps:

- t≈81-90s: franjas verticales de luz de colores (transición).
- t≈89-101s: **una animación de fuego/llamas coherente, evolucionando
  fotograma a fotograma** — degradados orgánicos, formas de llama
  reconocibles, consistente entre RUN_039 y RUN_040 en el mismo
  instante (misma composición general, pequeñas diferencias de detalle
  esperables por timing no perfectamente lock-step entre corridas).
- t≈101.8s (último frame capturado): imagen en bandas horizontales
  (letterbox) con iluminación de fuego intensa.

Esta NO es la pantalla de advertencia estática — es contenido nuevo,
animado, reproducible entre corridas independientes. Interpretación más
plausible: el fondo de la pantalla de advertencia siempre estuvo
diseñado para mostrar una animación/película (patrón común en juegos de
la época), y hasta ahora esa capa de fondo nunca se decodificaba (fondo
negro puro en todas las iteraciones previas); con el productor
liberado y el decoder sostenido, esa capa ahora decodifica contenido
real por primera vez en toda la investigación de BLOCKER_004.

## 24. Declaración sobre imagen de película reconocible

**SÍ se observa imagen de película/animación reconocible** — una
animación de fuego coherente y reproducible, NO ruido ni artefacto de
renderizado. No se afirma que sea específicamente el logo CAPCOM (no
identificado con esa certeza), pero es inequívocamente contenido de
vídeo decodificado nuevo, no la pantalla estática previa.

## 25. Presencia de agotamiento de `RuntimeGuestArena`

0/6 corridas P3.14.2 lo alcanzaron, pese a cientos de éxitos de
`GetPicture` y progreso muy superior al de P3.13 `full` (que lo
alcanzaba 3/3 con solo 10 éxitos). No se afirma que P3.14.2 lo resuelva
estructuralmente — solo que no se observó, incluso bajo carga
sustancialmente mayor. UNKNOWN el motivo exacto de la diferencia;
candidato para un futuro P3.15 si se busca explicarlo formalmente.

## 26. Nuevo LAST COMMON NODE / FIRST DIVERGENT NODE

Sin cambios respecto a P3.13/P3.14: el LAST COMMON NODE sigue siendo el
estado post-reset de `sceMpegInit` bajo `ownership`. El FIRST DIVERGENT
NODE sigue siendo el mecanismo de producción de imagen dentro de
`GetPicture` — P3.14.2 no cambia su naturaleza (sigue siendo decode
síncrono bajo demanda, sin equivalente directo en hardware original),
solo corrige que ahora el propio mecanismo HLE es internamente
coherente (el productor ya no se autoestrangula).

## 27. Clasificación

**`SUSTAINED_RING_SYNC_REDECODE_WITH_VISIBLE_MOVIE`** — el nivel más
alto de la escala del prompt. Justificación: ring consumption
guest-visible coherente (§7-14), wrap real cruzado y sostenido (§21-22),
sin inanición determinista, GetPicture sostenido muy por encima de 10
(≥250, 25× el antiguo techo), transporte CD/PSS continuado muy más allá
del punto de congelamiento anterior, y — no requerido por el prompt
pero alcanzado — imagen de película reconocible y reproducible (§23-24).

## 28. CHECKPOINT DECISION

Verificación de los 10 criterios explícitos de la puerta:

1. La premisa causal de decoder fresco se sostiene — CONFIRMADO (sin
   cambios respecto a P3.14: `decoderFresh=1` solo llamada #1,
   `frames=0` en cada entrada).
2. Consumo de ring guest-visible es coherente — CONFIRMADO (Modelo B,
   prueba algebraica §7 + 0 staleness §14).
3. Capacidad real del productor se libera — CONFIRMADO
   (`freeSpaceApprox` crece con cada consumo, producción sostenida
   >1.3M bytes, 2 wraps completos).
4. `videoBytesTotal` cruza `capacity` en 3/3 — CONFIRMADO (566181 y
   1075358, idénticos en las 3 corridas finales).
5. El wrap físico del ring se ejercita — CONFIRMADO en su forma limpia
   (reinicio a `physOffset=0`, 2×/corrida); la sub-rama de lectura
   partida NO se ejercitó (limitación reportada explícitamente, §15).
6. Al menos un intervalo envuelto es byte-exacto — PARCIALMENTE
   CONFIRMADO: no se logró la comparación externa estricta que pedía
   el prompt (§16), pero sí una prueba algebraica + verificación
   empírica no-basura/no-duplicado + 0 staleness, que constituye
   evidencia fuerte aunque no idéntica a la forma exacta solicitada.
7. Un único dueño de decode post-init — CONFIRMADO (§10-11, 31+
   omisiones async verificadas por corrida).
8. No queda inanición determinista de ~517313 — CONFIRMADO, roto
   ampliamente (≥250 éxitos vs 10).
9. GetPicture progresa sustancialmente más allá de 10 — CONFIRMADO
   (≥250, 25×).
10. Transporte CD/PSS continúa más allá del punto de congelamiento
    anterior (0xf0000) — CONFIRMADO (≥0x1880000).

**9 de 10 criterios se cumplen de forma completa; el criterio 6 se
cumple de forma parcial/honesta, con la limitación documentada
explícitamente en vez de ocultada.**

**CHECKPOINT_DECISION: CHECKPOINT_P314_RECOMMENDED.**

Justificación: el defecto bloqueante identificado por P3.14.1 (deadlock
determinista de inanición del productor) queda corregido y
reproducido 3/3 con evidencia muy por encima del mínimo pedido (≥250
éxitos sostenidos, no solo "sustancialmente >10"; imagen de película
real y reproducible, no solo progreso numérico). La única reserva es la
cobertura exacta del sub-caso de lectura partida en el borde (§15-16),
documentada transparentemente como limitación de esta validación, no
como defecto conocido del código — la prueba algebraica que sustenta esa
rama de código es independiente de si se ejercitó dinámicamente o no.
No se alcanza el nivel de "checkpoint de producción sin reservas": el
punto 6 queda abierto para el siguiente prompt, y la advertencia
`ac-tex damaged` (§29) sigue sin explicación causal completa.

### Commit propuesto (NO ejecutado en este prompt)

Título: `fix: guest-visible viBuf consumption + single decode ownership (P3.14.2)`

Cuerpo (borrador):

```
Corrects two BLOCKING defects P3.14.1's adversarial audit found in the
P3.14 synchronous-redecode experiment: (1) the guest video-ES producer
(viBufBeginPut) deterministically starved after ~520 KiB in 4/4 P3.14
runs because no guest-visible consumption of the ring ever existed
(viBufAddDMA -- the only real consumer -- is only reachable via a
stream=false callback this HLE's dispatch filter never invokes, and
even if invoked, its retirement math depends on a live DMA/IPU hardware
register this architecture does not emulate); (2) the async PES-decode
path (feedElementaryStream) remained unconditionally wired in parallel
with the sync path, risking a resync-triggered decoder.reset() +
decodedFrames.clear() destroying the sync decoder mid-use.

Fix: mirror the FINAL guest-observable effect of host consumption into
the guest's own blockCursor(+0x2C0)/pendingBytes(+0x2C8) bookkeeping
(Model B -- not the real two-phase DMA mechanism, which depends on
unemulated hardware), addressed with base+(consumed%capacity) and a
staleness guard; gate feedElementaryStream's call site on a new
per-playback syncGuestEsDecodeActive flag so the sync path is the sole
decoder/decodedFrames owner once P3.14 takes over.

Validated 3/3 with an identical executable: the old ~517313-byte/
10-success ceiling is broken by roughly 25x (>=250 sustained
GetPicture successes per run), the ring wraps past capacity twice
(FIRST_WRAP/SECOND_WRAP at byte-identical offsets across all 3 runs),
CD/PSS transport continues far past the old freeze point, zero
staleness/feed-failure/GP:H/RuntimeGuestArena-exhaustion events, and --
for the first time in this investigation -- reproducible, recognizable
decoded movie imagery (a coherent fire/flame animation) renders on
screen in 2/2 visual-trace runs at matching timestamps.

Known, explicitly documented limitation: the mid-chunk wrap-split read
path was implemented (base+capacity boundary never crossed, scratch
buffer reassembly) but never empirically exercised in these runs,
because capacity (524288) divides evenly by the feed chunk size (4096)
and the feed loop only detects "frame ready" on whole-chunk boundaries
-- so the byte-exact wrapped-interval proof the plan called for is
algebraic + staleness-guard-backed, not an external byte-for-byte
reconstruction. See
analysis/notes/BLOCKER_004_P3142_VIBUF_CONSUMPTION_AND_OWNERSHIP.md for
full evidence, including the still-open v0=0-vs-v0=1 GetPicture return
value mismatch and the unexplained (but apparently harmless) FFmpeg
"ac-tex damaged" decode-quality warnings.
```

## 29. Nota abierta: advertencias `ac-tex damaged`/`Warning MVs not available`

HECHO: aparecen en las 6 corridas P3.14.2 (cientos de veces cada una),
ausentes por completo en RUN_029 (P3.14 original, solo 10 éxitos, nunca
llegó a producir suficiente contenido para activarlas). Empiezan bien
DESPUÉS de ambos wraps (no correlacionan temporalmente con el evento de
wrap en sí). Dado que la imagen decodificada resultante (§23) es
coherente y reconocible (no ruido/glitch), la interpretación más
plausible es que son advertencias normales de concealment de FFmpeg
ante contenido complejo/con macrobloques dañados en el propio stream
original — no una corrupción introducida por el mecanismo de P3.14.2.
**No investigado a fondo, fuera de alcance explícito de este prompt**
(no forma parte de los 10 criterios de la puerta). Se recomienda como
seguimiento.

## 30. Valor de retorno `v0` de GetPicture

Sin cambios: el HLE sigue retornando `v0=0` en el camino de éxito,
mientras el original observado en PCSX2 (P3.8.1) retorna `v0=1`. No
tocado en este prompt, tal como exigía la instrucción explícita.

## 31. Ciclo de vida de `+0x28` entre películas

Nuevo dato estático (no dinámico): `MovieBufferInit` SÍ resetea `+0x28`
a 0 en su inicialización. Si ese resest se invoca de nuevo al empezar
una segunda película (o si `movieState` se reutiliza sin pasar por
`MovieBufferInit`) sigue sin determinarse dinámicamente — UNKNOWN, fuera
de alcance de P3.14.2 tal como exigía la instrucción explícita.

## 32. Clasificación (repetida por completitud del formato)

Ver §27: `SUSTAINED_RING_SYNC_REDECODE_WITH_VISIBLE_MOVIE`.

## 33. CHECKPOINT DECISION (repetida por completitud del formato)

Ver §28: `CHECKPOINT_P314_RECOMMENDED`.

## 34. Commit propuesto

Ver §28. No ejecutado.

## 35. Próximo prompt recomendado

Dada la aparición de imagen de película real, dos candidatos serios:

- **P3.15 — GETPICTURE_RETURN_AND_PRESENTATION_CONTRACT**: investigar
  el mismatch `v0=0` (HLE) vs `v0=1` (original, P3.8.1) — candidato
  directo para explicar por qué, pese a la imagen de fuego ya visible,
  la pantalla de advertencia (texto UI) sigue superpuesta y no hay
  transición de escena — podría ser exactamente lo que el caller usa
  para decidir cuándo avanzar de estado.
- **P3.14.3 — WRAP_SPLIT_EMPIRICAL_COVERAGE**: cerrar la limitación de
  §15-16 (forzar o esperar un caso real de lectura partida a mitad de
  chunk, p. ej. variando `kP314ChunkBytes` a un valor que no divida
  `capacity` exactamente, solo para fines de validación) antes de un
  checkpoint de producción sin reservas.

Se recomienda **P3.15** como siguiente paso: el hallazgo de imagen real
de película (§23) hace que el contrato de retorno de GetPicture sea
ahora el candidato más directo y de mayor impacto para explicar por qué
la película no sustituye visualmente a la pantalla de advertencia.

## 36. Estado final de git

- Vendor: dirty, único archivo `ps2xRuntime/src/lib/Kernel/Stubs/MPEG.cpp`
  (+353/-8 acumulado, P3.14+P3.14.2), sin commit.
- Main: dirty (`pss_video_output.md` aún no actualizado con el apéndice
  de esta iteración hasta el paso siguiente; los 2 informes previos
  P3.14/P3.14.1 sin trackear; este informe nuevo sin trackear).
- **No se ejecutó ningún commit ni push en este prompt.**

## 37. Confirmaciones explícitas

- NO commit, NO push, NO clean, NO regenerate.
- NO recompilación masiva de `recomp/generated/` en ningún build
  (verificado por conteo de `CL.exe` en cada log, 4 ciclos build+relink).
- NO terminación de procesos ajenos a `dmc-recomp.exe` de esta sesión.
- NO tramas ni éxitos sintéticos — la única fuente de `decodedFrames`
  sigue siendo el retorno real de `decoder->feed()`.
- NO se modificó el contrato de retorno `v0` de GetPicture (§30).
- NO se modificó la semántica del scheduler (`EeScheduler.cpp` intacto).
- NO se usó PCSX2 en este prompt (evidencia puramente estática +
  dinámica vía runtime local).
