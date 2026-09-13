# P4.2.1 — MPEG demux/viBuf backpressure: fix implementado y refutado como causa única

Fecha: 2026-09-13. Resultado: **P421_BACKPRESSURE_DEADLOCK_OR_ACCOUNTING_FAILURE**
(en el sentido de "accounting failure": el modelo de capacidad que P4.2
propuso como causa no predice ni previene la pérdida real). `NO_COMMIT`.

## Resumen

Se implementó exactamente el fix que P4.2 recomendó (backpressure real
entre el demux PSS y el viBuf guest, deteniendo el parseo cuando el
payload de vídeo no cabe, reteniendo los bytes en `pssBuffer` para
reintento). El mecanismo **funciona correctamente para lo que fue
diseñado a hacer**: la extracción del demux (boundary B) sigue siendo
byte-perfecta, 101 eventos STALL se resolvieron con 101 RETRY exitosos y
sin pérdida en esos puntos específicos. **Pero la omisión histórica que
P4.2 diagnosticó en el offset lógico 2.081.537 sigue ocurriendo
íntegramente después del fix**, con la MISMA firma (pérdidas de payloads
PES completos, tamaños recurrentes 4063/4077/8140/12217/16294/20357...),
y se confirmó directamente que ocurre **sin saturación real** del
backlog rastreado por este runtime (`ownBacklog` muy por debajo de
`capacity` en el momento exacto de la pérdida, incluso en admisiones
`ACCEPT` limpias, no solo en `STALL`/`RETRY`). Esto refuta la atribución
causal única de P4.2 ("el demux no propaga backpressure" como ÚNICA
causa) y exige un `NEXT` distinto: localizar el verdadero mecanismo
aguas abajo del punto de admisión, fuera del alcance de este prompt.

## Estado y alcance

- Main HEAD antes y después de este prompt: `ec3d40e650d76515a905a9dbe8ddc4651c065dbb`
  (sin cambios; `NO_COMMIT`).
- Vendor HEAD antes y después: `0efd17c3cdb834ac13c087dcb2bd947d9fc033b3`,
  limpio (el fix se implementó, se validó, y se revirtió con
  `git checkout --` tras confirmar que no pasa el Success Gate).
- Único archivo tocado durante la investigación:
  `vendor/PS2Recomp/ps2xRuntime/src/lib/Kernel/Stubs/MPEG.cpp`
  (implementación + instrumentación de diagnóstico, ambas revertidas).
  No se tocó `GS.cpp`, `gs_cpu_backend.cpp`, ni código generado.
- Build quirúrgico verificado en cada iteración (`grep -c "CL.exe"` /
  `grep -c "symtabfirst/generated"` en 0 para archivos generados).

## Fase 1 — Ruta de código reverificada (HECHO)

`processPssBuffer` (rama de vídeo, `isVideoStreamId`) calculaba
`payloadStart`/`packetEnd`, llamaba SIEMPRE `queueStreamCallbackEvent`
(encolando un evento) y SIEMPRE `erasePssPrefix(playback, packetEnd)` al
final del `while(true)`, sin importar si el guest tendría espacio. El
evento encolado se despacha más tarde vía `dispatchStreamCallbacksUnlocked`
→ `dispatchGuestStreamCallback`, que construye una `GuestInvocation`
(`RpcCallback`) y la entrega a `runtime->eeScheduler().queueInvocation(...)`
— una cola plana (`m_pendingInvocations`), **sin canal de retorno**: el
valor de retorno real de `StrM2vCallBack`/`viBufBeginPut`/`viBufEndPut`
(código guest real, no HLE) nunca se observa. El cursor PSS ya había
avanzado (`erasePssPrefix`) para cuando ese guest callback se ejecuta.

## Fase 2 — Derivación de espacio libre (HECHO)

Campos leídos de `movieState = mpegAddr - 0x278`:

- `+0x68` `dmaTagRingSize` (=256 en este runtime) → `capacity =
  (dmaTagRingSize-2)*2048 = 520.192` — fórmula idéntica a la ya usada por
  el consumidor P3.14.2 ([P3142:ring] trace), no inventada.
- `+0x20` `viBufCapacity` (=524.288, el tamaño físico del anillo,
  DISTINTO de la capacidad "usable" de arriba — usado solo por el
  consumidor para el módulo de wrap, no por el gate de admisión).

**Decisión de diseño importante, y por qué**: el primer intento leyó
`pendingBytes` (+0x2C8, escrito por el guest al producir y espejado por
este runtime al consumir) directamente del guest en cada chequeo de
admisión. Se descartó: `processPssBuffer` puede admitir MUCHOS payloads
en una sola llamada (el lote 37 de P4.2 tenía 14) antes de que CUALQUIERA
de sus callbacks RPC encolados se ejecute — `pendingBytes` permanece
"stale" durante todo ese lote, así que cada payload individual pasaría el
chequeo contra el MISMO snapshot pre-lote, reproduciendo sobre-admisión
dentro de un solo lote. Se sustituyó por un backlog rastreado
ENTERAMENTE por este runtime: `ownBacklog = p421AcceptedVideoEsBytes -
p314ConsumedEsBytes` (ambos contadores nuevos/existentes en
`MpegPlaybackState`, sin dependencia de cuándo el guest ejecuta su
callback) — provablemente ≥ el backlog real en todo momento (nunca
subestima), así que el gate solo puede ser más conservador que el
necesario, nunca menos.

## Fase 3 — Implementación

```
if (!finalChunk && rdram != nullptr) {
    capacity = (dmaTagRingSize-2)*2048;
    ownBacklog = p421AcceptedVideoEsBytes - p314ConsumedEsBytes;
    freeSpace = capacity > ownBacklog ? capacity - ownBacklog : 0;
    admitted = (capacity == 0) || (freeSpace >= payloadLength);
}
if (!admitted) { playback.p421PendingStall = true; return; }  // STALL
playback.p421AcceptedVideoEsBytes += payloadLength;             // ACCEPT/RETRY
queueStreamCallbackEvent(...);
```

`finalChunk` (flush de EOF) y `rdram == nullptr` bypasean el gate sin
cambios, igual que el resto del bucle ya trataba `finalChunk`. Atomicidad
de paquete: todo-o-nada, sin commits parciales (Phase "IMPORTANT: PACKET
ATOMICITY" del prompt).

### Hallazgo durante la validación: ráfaga de invocaciones RPC (corregido)

La primera corrida de validación (RUN_083) **crasheó** a los 91s con
`RuntimeGuestArena` / `EE invocation stack space exhausted`. Causa:
cuando un STALL se libera (RETRY), el `while(true)` sin límite de
`processPssBuffer` drena de un tirón TODO el backlog acumulado en
`pssBuffer` durante el stall, encolando una ráfaga de eventos muy mayor
al tamaño natural de lote de una sola lectura de CD del código anterior.
Cada evento se vuelve una invocación RPC anidada; se confirmó (RUN_082,
pre-P4.2.1) que la pila de invocaciones por hilo de este runtime **nunca
hace pop** de una invocación completada a lo largo de toda una corrida
(profundidad sube monótonamente 0→15 en 134s sin retroceder ni una vez)
— una característica preexistente del runtime, no introducida por este
fix. Una ráfaga suficientemente grande agota el arena fijo de
callback-stacks (~49 niveles). Fix aplicado: acotar
`callbackEvents.size()` a 16 por llamada a `processPssBuffer`,
devolviendo el resto intacto en `pssBuffer` (misma semántica de
retención que STALL, por ritmo en vez de por capacidad). RUN_084
(reintento) completó 200s sin crash, profundidad máxima 15 (igual que la
línea base pre-fix).

## Fase 4 — Retención de pssBuffer (HECHO)

Verificado por construcción: en STALL no se llama `erasePssPrefix` para
el paquete en cuestión, así que el header+payload completos permanecen
byte-idénticos en `pssBuffer`/`pssGuestAddrs`. La siguiente llamada
reanuda desde el mismo start code. No hay doble-demux, duplicación,
pérdida de prefijo ni cursor obsoleto — confirmado indirectamente por la
captura de boundary B (ver Fase 7): 0 duplicaciones detectadas en todo
el rango comparado.

## Fase 5 — Diagnóstico (revertido antes del checkpoint)

`DMC_P421_TRACE=1`: `[P421:BP] seq logicalEsOffset payloadSize ownBacklog
guestPending guestQueuedBlocks free capacity action=ACCEPT|STALL|RETRY`.
`DMC_P421_CAPTURE_ES=<path>`: captura opt-in de los bytes exactos
entregados a FFmpeg (boundary F, mismo punto que P4.2). Nuevo,
`DMC_P421_CAPTURE_DEMUX_ES=<path>`: captura opt-in del payload extraído
en el momento de admisión (boundary B) — añadida para poder comparar
ambos boundaries en la MISMA corrida, algo que P4.2 no hizo directamente
(sus capturas B y F eran de corridas distintas). Toda la instrumentación
fue revertida (`git checkout -- MPEG.cpp`) tras la validación.

## Fase 6/7 — Regresión del primer divergente y comparación byte a byte

**RUN_084** (`DMC_P421_TRACE=1`, `DMC_P421_CAPTURE_ES`,
`DMC_P421_CAPTURE_DEMUX_ES`, 200s, gates `[P314:GP]`=24,
`[P3142:ring]`=25, `[GP:H]`=0 — válida). Distribución de admisión: 3305
`ACCEPT`, 101 `RETRY`, 101 `STALL` — el gate se ejercitó genuinamente
(no es un no-op).

**Verificación de boundary B contra el propio capture ya-validado de
P4.2** (`analysis/local/p42/p42_demux_es.bin`, que P4.2 ya demostró
byte-perfecto contra el ES canónico): **idéntico byte a byte** en los
13.874.330 bytes comparables. Esto descarta un bug de parseo/demux — el
gate de admisión extrae exactamente los mismos bytes correctos que antes
del fix, solo cambia CUÁNDO se admiten.

**Comparación boundary B (`run1_demux_es.bin`, 13.874.330 bytes) vs
boundary F (`run1_ffmpeg_input.bin`, 13.642.263 bytes) de la MISMA
corrida**: divergen por primera vez exactamente en el offset **2.081.537**
— el MISMO offset que P4.2 identificó en el código sin arreglar — con un
resync exacto tras omitir **4063 bytes** (idéntico byte a byte a la
tabla de P4.2, incluidos los mismos bytes hexadecimales de contexto:
`...05c71bc70478fbdb|deeda1d5...` canónico vs
`...05c71bc70478fbdb|dd030f23...` recomp).

**Verificación crítica que refuta la causa de P4.2 para este caso
concreto**: en el momento exacto en que ese payload de 4063 bytes fue
admitido (`[P421:BP] seq=568 logicalEsOffset=2081541 ... action=ACCEPT`,
el payload ANTERIOR que termina en ese offset), `ownBacklog` era
**279.301–320.294**, muy por debajo de `capacity=520.192` — **no había
saturación**. El gate de backpressure NUNCA se activó cerca de este
punto en esta corrida; el payload fue `ACCEPT`ado sin fricción y aun así
se perdió entre la admisión y el guest viBuf.

**Un resync completo con re-alineación de ventana grande (evita falsos
positivos por contenido periódico) encontró 21 eventos de divergencia
adicionales** en los primeros ~4.6MB comparados, todos con la MISMA
firma que P4.2 documentó ("tamaños siempre iguales a sumas de payloads
PES reales: 4063, 4077, 8140, 12217, 16294, 20357, 24448..."). Se
verificó puntualmente un evento adicional en el offset **8.501.387**,
dentro de un payload que también fue `ACCEPT`ado limpiamente (`free
=26.434`, muy lejos de saturación) — confirmando que el patrón de
pérdida es completamente independiente de si el gate decide
`ACCEPT`/`STALL`/`RETRY`.

## Ledger de corrección

- **P3.14.2** "mecánica de consumo/wrap del viBuf": **sigue correcta
  dentro del alcance ya probado** — los wraps 1 y 2 de esta corrida
  fueron limpios (`FIRST_WRAP`/`SECOND_WRAP` en `producedTotal`
  esperados, `STALE_DATA_DETECTED`=0 en toda la corrida).
- **P4.2** "primer productor perdido en offset lógico 2.081.537" = sigue
  **HECHO** como observación (el byte se pierde, confirmado de nuevo
  aquí).
- **P4.2** "la omisión ocurre por falta de backpressure en el demux
  (causa única)" = **RETRACTADO/NARROWED**. El mecanismo de
  backpressure que P4.2 recomendó (y que este prompt implementó
  fielmente) se probó funcional y suficiente para SU propio modelo de
  capacidad (101/101 stall→retry sin pérdida en esos puntos, boundary B
  byte-perfecto), pero **no elimina la pérdida real**, que ocurre
  también bajo `ACCEPT` limpio sin presión de backlog. La causa raíz
  verdadera está aguas abajo del punto de admisión — en la escritura
  real del guest (`StrM2vCallBack`/`viBufEndPut`, código MIPS
  recompilado no instrumentado) o en el espejo de bloques de P3.14.2
  (redondeo a bloques de 2048 bytes de payloads que NUNCA son múltiplos
  de 2048: 4063, 4077) — ninguna de las dos investigada aquí, ambas
  fuera del alcance de este prompt (`MPEG.cpp`, capa de demux/admisión
  únicamente; ni P3.14.2 ni el código guest recompilado están
  autorizados a tocarse bajo este prompt).
- **P4.1.10** cadena GS/UI/framebuffer: **sigue congelada e intacta**,
  confirmado en RUN_084 (contrato de RAM guest byte-exacto: UI
  `DISPLAY={0,0x38}`/`DRAW={0x38,0}`, película post-`Main_init`
  `DISPLAY={0,0xA0}`/`DRAW={0xA0,0}`).
- **RUN_082** "primer `ac-tex damaged` en `GetPicture #43`" = confirmado
  como línea base reproducible; RUN_084 muestra 213 `ac-tex
  damaged`/183 `Warning MVs not available` en 200s — el residual visual
  no desaparece, consistente con que la causa real no fue tocada.

## Por qué se aplica la Failure Rule (STOP)

El prompt especifica explícitamente: *"If backpressure removes the first
omission but another divergence appears: STOP. Locate the new FIRST bad
byte. Do not stack a second speculative MPEG fix."* — Aunque el offset
del primer divergente no cambió numéricamente (sigue siendo 2.081.537),
la evidencia demuestra que la CAUSA que este fix ataca (saturación de
capacidad) no es la causa real de ESA pérdida ni de la mayoría de las 21+
subsecuentes — es, en efecto, "otra divergencia" en el sentido causal
exigido por la regla: el mecanismo real permanece sin localizar. Seguir
ajustando el modelo de capacidad (p.ej. bajar el margen, cambiar la
fórmula) sería exactamente el "segundo fix especulativo" que el prompt
prohíbe explícitamente. Se detiene aquí.

## Success Gate — resultado

| # | Criterio | Resultado |
|---|---|---|
| 1 | Ningún payload PES completo se pierde cuando el viBuf está lleno | **Parcialmente cumplido**: cuando el gate SÍ detecta saturación (101 casos), no se pierde nada. No cubre las pérdidas que ocurren SIN saturación detectada. |
| 2 | El offset 2.081.537 permanece byte-idéntico al canónico | **FALLA** — sigue divergiendo, mismo tamaño de hueco (4063). |
| 3 | El payload histórico de 4063 bytes se admite eventualmente completo | **FALLA** — se pierde igual que antes. |
| 4 | Sin duplicación por reintentos | **CUMPLE** (verificado: boundary B sin duplicados). |
| 5 | El input a FFmpeg coincide con el ES canónico en la región históricamente corrupta | **FALLA**. |
| 6 | SHA256 completo del ES de apertura coincide | **NO** (no evaluado formalmente dado el fallo de 2/3/5, pero las divergencias confirmadas lo descartan). |
| 7 | 0 `ac-tex damaged` en la película de apertura | **FALLA** (213 en RUN_084). |
| 8 | 0 `Warning MVs not available` | **FALLA** (183 en RUN_084). |
| 9 | La corrupción visual MPEG de RUN_082 desaparece | **No confirmado** (dado 2/3/5/7/8, no se espera que desaparezca; no se hizo validación visual dedicada dado el fallo temprano de los gates de integridad de bytes). |
| 10 | Sin deadlock/bucle de stall | **CUMPLE** (tras el fix de ritmo; 0/0 tras corrección). |
| 11 | Comportamiento de consumo/wrap de P3.14.2 intacto | **CUMPLE**. |
| 12 | Línea base UI/GS de P4.1.10 intacta | **CUMPLE**. |
| 13 | Validación diagnóstica 3/3 | **N/A** — se detuvo tras 1 corrida válida por el fallo claro y reproducible de los criterios 2/3/5/7/8; correr 2 más no cambiaría la conclusión causal. |
| 14 | Corrida de humo de producción limpia pasa | **N/A** — no se generó binario de producción con este fix (revertido antes del checkpoint). |
| 15 | Sin cambios semánticos no relacionados | **CUMPLE** (único archivo: `MPEG.cpp`, revertido en su totalidad). |

Gate global: **FALLA** (criterios 2, 3, 5, 6, 7, 8 no se cumplen).
`CHECKPOINT_DECISION: NO_COMMIT`.

## Residuales / siguiente paso

BLOCKER_004 **sigue abierto**. El mecanismo de backpressure implementado
aquí es correcto para su propio alcance pero insuficiente como fix de
producción dado que no ataca la causa real. Recomendación explícita para
un prompt futuro (**P4.2.2**, no ejecutado): instrumentar el ESPEJO de
bloques de P3.14.2 (`mirroredBlocks = p314ConsumedEsBytes / 2048`,
redondeo hacia abajo) y/o el contador guest real `videoBytesTotal(+0x28)`
con granularidad densa (no capada a 20 entradas) a lo largo de TODA una
corrida, para determinar si la pérdida periódica de payloads completos
(4063/4077 bytes, ninguno múltiplo de 2048) se origina en un desajuste
sistemático entre la contabilidad por bytes de este runtime y la
contabilidad por bloques de 2048 bytes que el anillo DMA-tag del guest
usa internamente. No tocar `GS.cpp`/P4.1.10 (frozen, confirmado intacto
aquí). No declarar BLOCKER_004 cerrado ni parcialmente cerrado por este
prompt — el vendor y el main quedan exactamente como estaban antes de
empezar.

## Cierre

Único archivo de producción tocado durante la investigación:
`vendor/PS2Recomp/ps2xRuntime/src/lib/Kernel/Stubs/MPEG.cpp` —
íntegramente revertido (`git checkout --`) tras confirmar que el Success
Gate falla. Vendor limpio en `0efd17c3cdb834ac13c087dcb2bd947d9fc033b3`
(sin cambios respecto al HEAD anterior a este prompt). Main sin cambios
salvo este informe (nuevo, sin trackear hasta el commit de
documentación) y los informes históricos ya pendientes de sesiones
previas. No se tocó GS.cpp, `gs_cpu_backend.cpp`, ni código generado. No
se creó patch ni se modificó `upstream.lock.json` (no aplica sin fix
comprometido). Build final de producción reconstruido y relinkeado desde
el vendor limpio para dejar el binario coincidente con la fuente
comprometida. CHECKPOINT_DECISION: **NO_COMMIT**.
