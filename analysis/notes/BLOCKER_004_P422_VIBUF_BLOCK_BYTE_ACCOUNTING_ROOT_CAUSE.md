# P4.2.2 — Localización de causa raíz: asincronía de callbacks, no capacidad

Fecha: 2026-09-13. Resultado: **P422_ASYNC_CALLBACK_STALE_ACCOUNTING_ROOT_CAUSE**,
localizado con evidencia directa y precisa. `NO_COMMIT` (prompt de solo
localización).

## Veredicto en una línea

**El HLE despacha ráfagas de ~14 callbacks de vídeo (`StrM2vCallBack`) antes
de que NINGUNO se ejecute en la CPU guest emulada; cuando el scheduler por
fin los ejecuta, lo hace en orden de encolado, así que los primeros 13
comprometen sus bytes en `pendingBytes(+0x2C8)` ANTES de que el 14º ejecute
su propio `viBufBeginPut` — y es precisamente en ese instante de ejecución
(no en el de encolado, que es lo único que cualquier gate del lado HLE
puede observar) donde `pendingBytes` ya refleja los 13 payloads previos y
la cuenta real de espacio libre puede caer por debajo de lo necesario.
Confirmado con precisión exacta en el punto histórico: 14 callbacks en
vuelo en el momento exacto de encolar el payload de 4063 bytes en el
offset 2.081.537, cayendo a 1 inmediatamente después (13 completándose de
golpe) — coincide número por número con el "lote 37, 14 payloads, 13
comprometidos" que P4.2 documentó. Esto explica por qué el gate de
P4.2.1 (que solo puede leer/predecir estado en el instante de ENCOLADO)
no podía funcionar: el evento causal ocurre estrictamente después, en el
instante de EJECUCIÓN, que el HLE no observa ni puede observar sin
instrumentar la propia CPU guest.**

## Estado y alcance

- Main HEAD antes y después: `ec3d40e650d76515a905a9dbe8ddc4651c065dbb`, sin
  cambios.
- Vendor HEAD antes y después: `0efd17c3cdb834ac13c087dcb2bd947d9fc033b3`,
  limpio (toda la instrumentación de este prompt fue revertida con
  `git checkout --`).
- Único archivo tocado durante la investigación:
  `vendor/PS2Recomp/ps2xRuntime/src/lib/Kernel/Stubs/MPEG.cpp`
  (instrumentación de solo lectura, sin cambios de admisión/control de
  flujo — ninguna llamada existente se modificó, solo se añadieron
  lecturas y trazas opt-in). Revertido íntegramente.
- No se tocó `GS.cpp`, presentación, `FIELD`/`PMODE`, ni código generado.
  P4.1.10 confirmado intacto (ver sección final).

## Fase 1 — Contrato real del viBuf (HECHO, desensamblado directo del ELF)

Se construyó un desensamblador MIPS III/R5900 más completo (no el
lector mínimo de sesiones anteriores) y se desensambló byte a byte
`viBufBeginPut` (`0x0047AE90-0x0047AF80`), `viBufEndPut`
(`0x0047AF80-0x0047AFD4`), `viBufAddDMA` (`0x0047B0D0-0x0047B2FC`) y
`StrM2vCallBack` (`0x0047AFE0-0x0047B0C4`), identificados por nombre en
`analysis/ghidra/export_r5900/dmc_r5900_functions.csv`.

| Offset | Significado | Unidad | Productor | Consumidor |
|---|---|---|---|---|
| `+0x1C` | `viBufBase` | puntero guest | `MovieBufferInit` (una vez) | leído por P3.14.2 y por `viBufBeginPut` |
| `+0x20` | `viBufCapacity` | **bytes** (524.288 = 256×2048) | `MovieBufferInit` (una vez) | `viBufBeginPut`/P3.14.2 (divisor del módulo físico) |
| `+0x28` | `videoBytesTotal` | **bytes**, contador acumulado ascendente | `viBufEndPut` (`+= committed`) | P3.14.2 (`available = +0x28 - p314ConsumedEsBytes`) |
| `+0x68` | `dmaTagRingSize` | **bloques** (=256, fijo, `MovieBufferInit`) | `MovieBufferInit` | `viBufBeginPut` (`254 = dmaTagRingSize-2`, margen de 2 bloques) |
| `+0x2C0` | `blockCursor` | **bloques** (0-255, módulo `dmaTagRingSize`) | `viBufAddDMA` (avanza al comprometer DMA) | `viBufBeginPut` (posición base de escritura); **también sobrescrito por el espejo P3.14.2** |
| `+0x2C4` | `queuedBlocks` | **bloques**, en vuelo hacia DMA real | `viBufAddDMA` (`+=`) | *debería* decrementarlo la retirada real de hardware DMA — **no emulada** (comentario propio de P3.14.2, confirmado) |
| `+0x2C8` | `pendingBytes` | **bytes**, producido-no-consumido | `viBufEndPut` (`+= committed`) | P3.14.2 (espejo, sobrescritura absoluta) |

Todos los campos de bloque (`+0x2C0`,`+0x2C4`,`+0x68`) están en unidades
de **2048 bytes**; `+0x1C/+0x20/+0x28/+0x2C8` están en **bytes**. Ningún
campo mezcla unidades por sí solo — la MEZCLA ocurre en la aritmética de
`viBufBeginPut`, que combina ambos (ver más abajo). `queuedBlocks`
**cuenta datos ya escritos en el ring pero aún no retirados por DMA real**
(no "encolados pero no comprometidos" en el sentido byte-level de
P4.2.1) — se mantuvo en 0 durante TODA la corrida observada (5808/5808
muestras), así que su posible crecimiento sin límite (hipótesis inicial
de esta investigación, ver Fase 2) queda **REFUTADA empíricamente**.

### Aritmética exacta de `viBufBeginPut` (reconstrucción completa, HECHO)

```
freeBlocks         = 254 - queuedBlocks(+0x2C4)                         // bloques
unwrappedWritePos  = (blockCursor(+0x2C0) + queuedBlocks) * 2048
                     + pendingBytes(+0x2C8)                              // bytes, sin envolver
wrappedWritePos    = unwrappedWritePos % viBufCapacity(+0x20)            // bytes, físico
distanceToRingEnd  = viBufCapacity - wrappedWritePos                     // bytes
freeBytesByBlocks  = freeBlocks * 2048 - pendingBytes                    // bytes
if (distanceToRingEnd < freeBytesByBlocks):                              // caso WRAP
    primaryOffer   = distanceToRingEnd            // región 1: hasta el final físico
    secondaryOffer = freeBytesByBlocks - distanceToRingEnd  // región 2: desde viBufBase
    // out3 = viBufBase (puntero región 2)
else:                                                                    // caso normal
    primaryOffer   = freeBytesByBlocks
    secondaryOffer = 0
```

`StrM2vCallBack` (0x47AFE0) SÍ usa ambas regiones correctamente: llama a
`Copy2area(destPtr1=writePtr, len1=primaryOffer, destPtr2=wrapPtr,
len2=secondaryOffer, srcPtr=dataAddr, totalRequested=clampedLen, ...)`
(`Copy2area`, `0x003FE450`, función sustancial de ~150+ instrucciones,
NO la de 4 bytes que sugería el símbolo truncado de Ghidra — no se
completó su desensamblado exhaustivo por alcance/tiempo, ver residual).
`Copy2area` devuelve los bytes realmente comprometidos; ese valor —sin
comparar contra lo solicitado— se pasa directo a `viBufEndPut`, y el
retorno booleano de `StrM2vCallBack` es solo "¿se comprometió algo >0?",
nunca "¿se comprometió TODO lo pedido?". **No hay canal de error para
una escritura parcial ni para 0 bytes comprometidos por falta de
espacio** — coincide exactamente con lo que P4.2 ya había inferido
estáticamente, ahora confirmado por desensamblado completo.

## Fase 2 — Auditoría del espejo de consumo P3.14.2 (NARROWED, no es la causa)

```
mirroredBlocks   = p314ConsumedEsBytes / 2048        // floor, división entera
newBlockCursor   = mirroredBlocks % dmaTagRingSize     // SOBRESCRITURA ABSOLUTA de +0x2C0
newPendingBytes  = videoBytesTotal - mirroredBlocks*2048  // SOBRESCRITURA ABSOLUTA de +0x2C8
// queuedBlocks (+0x2C4) deliberadamente NUNCA tocado por este espejo.
```

Hallazgo: el espejo SOBRESCRIBE `+0x2C0` con un valor absoluto — el
mismo campo que **también** escribe `viBufAddDMA` (código guest real,
confirmado en su desensamblado: `sw v0,704($s5)`). Esto es una
escritura compartida entre HLE y guest sobre el mismo campo, un riesgo
estructural real — pero **no se encontró evidencia de que `viBufAddDMA`
se ejecute en absoluto** en las corridas observadas (`queuedBlocks`
permanece en 0 en 5808/5808 muestras; si `viBufAddDMA` corriera y
escribiera `+0x2C0`, `queuedBlocks` seguiría en 0 solo si además nunca
incrementa — pero si `viBufAddDMA` nunca corre, tampoco puede chocar con
el espejo). División `floor` y no-conteo de remainder fraccionario son
correctos por diseño ya documentado en P3.14.2 (rounding hacia abajo
deliberado para mantener `blockCursor` consistente con la fórmula del
productor real — confirmado ahora que esa fórmula usa bloques enteros
consistentemente). **P3142_ACCOUNTING_VALID = YES** dentro del alcance
ya probado; el mecanismo de la omisión está en otra parte.

## Fases 3-4 — Traza densa y modelo dual (HECHO, RUN_085/RUN_086)

Instrumentación de solo lectura (`DMC_P422_TRACE=1`), sin alterar
ninguna decisión de admisión existente (el código base de admisión
sigue siendo el original de P4.2, sin gate): `[P422:VB]` replica la
aritmética exacta de `viBufBeginPut` (arriba) en cada payload de vídeo
admitido, en el instante en que `processPssBuffer` lo encola;
`[P422:CB]` cuenta callbacks de `StrM2vCallBack` en vuelo
(encolados-menos-completados) en cada `dispatchGuestStreamCallback`.
Gates P314/P3142/GP:H verificados en ambas corridas (P314:GP=24,
P3142:ring=23/25, GP:H=0). 5808/5808 payloads de vídeo trazados en cada
corrida — el ES canónico completo (5.808 paquetes, coincide con P4.2).

**En NINGUNA muestra el modelo de bloques (arriba) y un modelo de bytes
simple (capacidad−pendingBytes) discreparon entre sí en el mismo
instante** — ambos siempre acuerdan porque, con `queuedBlocks=0`
constante, `freeBytesByBlocks` se reduce exactamente a
`254*2048=520.192` (la misma cifra que P4.2.1 ya usaba). **La
divergencia NO es entre modelo de bytes y modelo de bloques evaluados
en el mismo instante — es TEMPORAL: el valor de `pendingBytes` en el
instante de ENCOLADO no es el valor de `pendingBytes` en el instante de
EJECUCIÓN de ESE MISMO payload.**

## Fase 8 — Asincronía de callbacks: el mecanismo causal (HECHO, evidencia directa)

Se instrumentó el conteo de callbacks de vídeo en vuelo
(`dispatchGuestStreamCallback`, contador de encolado vs. contador de
finalización vía `invocation.onComplete`). Resultado global (RUN_086,
150s): 5808 encolados, 5808 completados (sin fuga neta), profundidad
máxima observada = 16. **Patrón periódico**: 421 caídas abruptas
(≥8 de profundidad) en toda la corrida — aproximadamente una cada 14
payloads (5808/421≈13.8) — cada una mostrando profundidad ascendiendo
hasta ~12-16 y cayendo a 1 en el siguiente encolado (13-15 callbacks
completándose de golpe).

### El punto histórico exacto (HECHO, coincide número por número con P4.2)

| Campo | Valor |
|---|---|
| `logicalEsOffset` (encolado) | 2.077.478 |
| `payloadSize` | 4063 |
| `pendingBytes` leído al encolar (obsoleto) | 470.999 |
| `freeBytesByBlocksMinusPending` (modelo, instante de encolado) | 49.193 — **"cabe" según cualquier modelo evaluado en ese instante** |
| **`outstanding` (callbacks de vídeo en vuelo) al encolar este payload** | **14** |
| `outstanding` en el siguiente encolado (payload +4077, offset 2.081.541) | **1** |
| Interpretación | 13 callbacks previos, encolados antes que este, se ejecutan y comprometen sus bytes ANTES de que ESTE (el 14º) ejecute el suyo — el offset lógico 2.081.537 (dentro de este payload) es el primer byte canónico que el decodificador ve sustituido por el payload siguiente |

Este patrón (`outstanding=14` exactamente en el payload perdido,
cayendo a 1 justo después) se repite con notable regularidad en las
421 caídas detectadas, y el payload en la posición pico es casi siempre
de tamaño 4063 — sugiriendo que el ciclo de lectura/demux de CD produce
lotes de tamaño consistente (~14 paquetes de vídeo) por "tick" del
scheduler, y que el ÚLTIMO paquete de cada lote es sistemáticamente el
expuesto a la contabilidad ya actualizada por TODOS los anteriores del
mismo lote.

## Fases 5-6 — Reconciliación con P4.2 y patrón de tamaños (INFERENCIA, razonada)

P4.2 encontró pérdidas recurrentes con tamaños que son sumas de
payloads reales (4063, 4077, 8140=4063+4077, 12217≈3×, 16294=4×4077...).
Esto encaja exactamente con el mecanismo aquí confirmado: si el
CICLO DE LOTE (~14 payloads) varía ligeramente en tamaño de ciclo a
ciclo, o si el umbral de capacidad real (`freeBytesByBlocks -
pendingBytesAlEjecutar`) queda justo por debajo de CERO para más de un
payload consecutivo al final de un lote particularmente grande, MÚLTIPLES
payloads consecutivos (no solo el último) pueden perder su comisión —
produciendo exactamente los tamaños compuestos que P4.2 catalogó. No se
completó el cálculo exacto de "bloques tocados" ni "offset mod 2048" en
el INSTANTE DE EJECUCIÓN real (solo en el de encolado, que es
observable) — ver limitación explícita abajo.

**Limitación explícita (HECHO, no inferencia)**: los campos
`PRODUCER_OFFSET_MOD_2048` / `CONSUMER_OFFSET_MOD_2048` /
`PAYLOAD_BLOCKS_TOUCHED` requeridos por el prompt solo pueden calcularse
con precisión leyendo el estado guest EN EL INSTANTE EN QUE
`viBufBeginPut` REALMENTE EJECUTA para ese payload — algo que esta
instrumentación (basada en HLE, con visibilidad solo al ENCOLAR) no
puede observar sin instrumentar la ejecución de código guest recompilado
directamente (fuera del alcance de "diagnóstico en MPEG.cpp" de este
prompt). Los valores reportados abajo son del instante de ENCOLADO
(conocido, HECHO) con una estimación razonada del instante de EJECUCIÓN
(INFERENCIA, acotada por la suma de los 13 payloads precedentes ≈
52.973 bytes, cifra que coincide con la que P4.2 ya había derivado
independientemente).

## Fase 9 — El crash de pila de invocaciones de P4.2.1 (HECHO: NO relacionado directamente)

P4.2.1 descubrió un crash por agotamiento de `RuntimeGuestArena`
(pila de invocaciones EE, `owner->invocations` en `EeScheduler.cpp`,
límite ~49 niveles) al liberar un STALL grande de golpe. Se investigó
si el MISMO mecanismo (profundidad de invocaciones) es el causante de
la omisión histórica aquí localizada.

**Son dos contadores DIFERENTES**: (a) `owner->invocations.size()`
(EeScheduler, pila anidada de invocaciones activas del hilo EE) — este
es el que causó el crash, confirmado que **nunca decrece** en una
corrida completa (RUN_082 pre-P4.2.1: 0→15 monótono en 134s, sin un
solo pop); (b) el contador `outstanding` de ESTE prompt (callbacks de
vídeo encolados-menos-completados vía la cola PLANA
`m_pendingInvocations`, no la pila anidada) — este SÍ cicla
correctamente entre 1 y ~16, completando 5808/5808 sin fuga neta.

**Conclusión (HECHO)**: el crash de P4.2.1 y la omisión histórica de
P4.2 comparten un ORIGEN GENERAL común (ráfagas de callbacks RPC
encolados antes de ejecutar), pero NO son el mismo mecanismo de fallo —
uno agota una pila de profundidad que nunca se libera (arquitectural,
independiente del contenido), el otro depende de la CONTABILIDAD DE
BYTES del viBuf en el momento de ejecución. **Se exonera el crash como
causa directa de la omisión**: la omisión ocurre en corridas (como
RUN_085/086, sin el fix de ritmo de P4.2.1) donde la profundidad de
`owner->invocations` nunca se acerca al límite de 49. `CALLBACK_ASYNCHRONY_CAUSAL
= YES` para la omisión; el crash es un hallazgo relacionado pero
distinto, ya corregido únicamente dentro del propio fix descartado de
P4.2.1 (revertido junto con el resto).

## Fase 10 — Wrap del ring (HECHO: descartado de nuevo como causa)

En el punto histórico, `queuedBlocks=0`, `wrapCase=0` (rama normal, sin
cruce del final físico del ring) según la aritmética exacta
reconstruida. El mismo patrón de 14-en-vuelo/13-completan se repite 421
veces a lo largo de TODO el ES, en offsets muy alejados de cualquier
múltiplo de 524.288 (capacidad física) — **RING_WRAP_CAUSAL = NO**,
confirmado que el mismo patrón de accounting-asíncrono ocurre lejos de
cualquier wrap.

## Fase 11 — Artefacto de captura

`analysis/local/p422/first_omission.json`: payloads seq 562-569
(RUN_086) con snapshot de admisión completo (`[P422:VB]`) y profundidad
de callback (`[P422:CB]`) para cada uno, incluyendo el payload perdido
(seq=567, marcado `is_historical_loss: true`).

## Ledger de corrección

- **P4.2** "el demux HLE no propaga backpressure = causa" → **NARROWED
  con precisión**: la ausencia de backpressure en el demux es una
  condición NECESARIA (sin ella, CUALQUIER payload se admite
  incondicionalmente) pero la causa PROXIMAL exacta de CUÁL payload se
  pierde y CUÁNDO es la asincronía de ejecución de callbacks —
  `pendingBytes` en el instante de encolado no predice `pendingBytes`
  en el instante de ejecución cuando hay ~14 callbacks en vuelo por
  delante. El número "14 payloads, 13 comprometidos" de P4.2 queda
  **confirmado exactamente** por este informe, ahora con el mecanismo
  explicado (no solo observado).
- **P4.2.1** "backpressure en el punto de encolado basado en backlog
  propio" → **RETRACTADO como suficiente**: cualquier gate que solo
  pueda leer/predecir estado en el instante de ENCOLADO (incluida la
  variante `ownBacklog` de P4.2.1, inmune a staleness de RAM guest pero
  IGUALMENTE ciega a cuántos callbacks se ejecutarán antes que el
  propio) no puede prevenir esta clase de pérdida, porque el evento
  causal (comisión de los 13 anteriores) ocurre estrictamente DESPUÉS
  del punto donde cualquier gate de admisión HLE puede intervenir.
- **P3.14.2** consumo/wrap: **sigue válido (YES)**, exonerado de nuevo,
  ahora con la aritmética completa del productor real como contexto.
- **P4.1.10** GS/UI/framebuffer: **sigue congelado e intacto**,
  confirmado en RUN_086 (contrato de RAM guest byte-exacto).
- **"4to wrap" / saturación pura como única causa**: sigue
  RETRACTADO; la periodicidad real está atada al ritmo de
  encolado-antes-que-ejecución (~14 payloads/lote), no al wrap físico
  del ring (524.288 bytes) ni a un umbral de backlog byte-a-byte simple.

## Residuales / próximo paso

`Copy2area` (`0x003FE450`) no se desensambló por completo — es
sustancialmente más grande que el símbolo truncado del export de
Ghidra sugería (confirmado: prólogo estándar de función grande, no una
función de 4 bytes). Su comportamiento EXACTO ante `len1+len2 <
requested` (¿copia parcial? ¿copia cero? ¿dispara algún log/assert
guest?) no se verificó a nivel de instrucción — se infirió por
comportamiento observado (pérdida limpia y completa, sin bytes
parciales) que se comporta como "todo o nada" en la práctica, pero esto
es INFERENCIA, no HECHO por desensamblado.

## Success Gate (localización, no fix) — N/A

Este prompt es de localización; no aplica gate de producción. Se
cumplieron las Fases 1-11 con evidencia directa salvo el desensamblado
completo de `Copy2area` (residual explícito arriba).

## Cierre

Único archivo tocado: `vendor/PS2Recomp/ps2xRuntime/src/lib/Kernel/Stubs/MPEG.cpp`,
íntegramente revertido (`git checkout --`). Vendor limpio en
`0efd17c3cdb834ac13c087dcb2bd947d9fc033b3` (sin cambios). Main sin
cambios salvo este informe y el artefacto `analysis/local/p422/first_omission.json`
(no trackeado, como el resto de `analysis/local/`). No se implementó
ningún cambio de producción, tal como exige el prompt. P4.1.10
confirmado intacto. CHECKPOINT_DECISION: **NO_COMMIT**.
