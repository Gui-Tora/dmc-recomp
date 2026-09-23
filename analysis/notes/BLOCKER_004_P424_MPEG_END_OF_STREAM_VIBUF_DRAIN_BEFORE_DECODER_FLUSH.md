# BLOCKER_004 — P4.2.4: MPEG END-OF-STREAM VIBUF DRAIN BEFORE DECODER FLUSH

Agente: FABLE
Fecha: 2026-09-13
Prompt: P4.2.4 — MPEG_END_OF_STREAM_VIBUF_DRAIN_BEFORE_DECODER_FLUSH
Base: main `65dd4aa` + vendor `0efd17c3` DIRTY con P4.2.3 (intencional, preservado, NO revertido)

Backup de seguridad de P4.2.3 (previo a cualquier edición, untracked):
`analysis/local/p424/p423_validated_baseline.patch`
SHA256 `e4f07c0e673e2b355afa1ccac477e2fb02891fe6adf7f14fb7e34d94d7bc8f64` (832 líneas).

Archivo tocado (único): `vendor/PS2Recomp/ps2xRuntime/src/lib/Kernel/Stubs/MPEG.cpp`.
Build: `analysis/tools/runtime_loop/build_one.ps1 -Mpeg` (solo MPEG.cpp + relink; sin full build).

---

## 1. State machine de EOF ANTES del fix (Phase 1-2)

Todas las referencias de línea son del MPEG.cpp **post-fix** salvo que se indique
"pre-fix"; los sitios son los mismos, solo desplazados por los inserts.

### 1.1 `playback.streamEnded` — escrituras

HECHO (lectura directa de código):

| Sitio | Función | Evento que lo activa |
|---|---|---|
| decl (campo, init `false`) | `MpegPlaybackState` | — |
| `processPssBuffer`, rama `streamId == kMpegProgramEnd` (0xB9) | demux PSS | **SOURCE EOF variante A**: el propio stream PSS contiene el pack `program_end` |
| `finishPlaybackStream` (llamada desde `finalizeCdStreamEofUnlocked`) | CD stream EOF | **SOURCE EOF variante B**: el CD terminó de producir y `demuxed >= produced` |
| preservación en reset (full-mode preserve / reset preserve) | lifecycle | copia el valor, no lo origina |

HECHO: ningún sitio lo activa por "video ES sequence end", "decoder EOF" ni
"host stream EOF". Los dos activadores reales son ambos **SOURCE EOF**
(el productor PSS/CD no entregará más bytes).

### 1.2 Semántica antigua (el bug)

Pre-fix, en AMBOS sitios de source EOF se ejecutaba inmediatamente:

```cpp
playback.streamEnded = true;
playback.cdStreamGeneration = g_mpeg_stub_state.cdStreamGeneration;
flushDecoderIfEnded(playback);   // <-- parser flush + avcodec_send_packet(nullptr) + drain
```

y además el gate de entrada del bloque sync-redecode (P3.14.2) en
`sceMpegGetPicture` contenía `!playback.streamEnded`:

```cpp
if (playback.decodedFrames.empty() &&
    isP314SyncRedecodeEnabled() &&
    !playback.streamEnded &&      // <-- stop condition literal
    !playback.decoderFailed)
```

HECHO: la condición exacta que detenía el consumo en 23.183.360 era ese
`!playback.streamEnded` del gate. En cuanto `finalizeCdStreamEofUnlocked` /
`kMpegProgramEnd` ponía `streamEnded=true`, el consumidor P3.14.2 dejaba de
entrar al bloque para siempre, con `accepted (23.654.877) − consumed
(23.183.360) = 471.517` bytes canónicos aún en viBuf. El byte 23.183.361 no se
procesaba porque el gate ya era falso ANTES de la siguiente llamada a
GetPicture que lo habría alimentado — no por límite de ring, no por wrap, no
por falta de datos.

HECHO (confirmación de la hipótesis primaria del prompt): `streamEnded`
conflaba **SOURCE EOF** ("no llegarán más bytes") con **CONSUMER/DECODER INPUT
EOF** ("todos los bytes aceptados ya fueron consumidos"). El flush inmediato
en el sitio de source EOF señalaba EOF a parser+codec con 471.517 bytes de
input pendientes → truncamiento del final del stream → `ac-tex damaged` /
`Warning MVs not available` exactamente tras el último feed.

### 1.3 Por qué 23.183.360 exactamente

HECHO aritmético (verificado independientemente):

- 23.183.360 = 5.660 × 4096 (múltiplo exacto del chunk `kP314ChunkBytes`).
- 471.517 = 115 × 4096 + 477.
- 23.654.877 mod 4096 = 477.

INFERENCIA (mecanismo, consistente con los logs de P4.2.3): el consumidor
alimenta en chunks de `min(4096, available − fed)` y se detiene cuando produce
un frame; el punto de corte en un múltiplo exacto de 4096 refleja que la
última pasada previa al EOF terminó en frontera de chunk, y el gate
`!streamEnded` impidió toda pasada posterior.

### 1.4 Pipeline FFmpeg real (Phase 4)

HECHO (lectura de `MpegFfmpegDecoder`): el wrapper usa exactamente
`av_parser_parse2` (parser MPEG-2 independiente) → `avcodec_send_packet` →
`avcodec_receive_frame`. `flush(frames)` implementa ya la secuencia correcta
de drain final:

1. `av_parser_parse2(..., nullptr, 0, ...)` — flush del parser (puede emitir
   una última access unit bufferizada);
2. si produjo packet → `sendPacket(...)`;
3. `avcodec_send_packet(m_codecCtx, nullptr)` — EOF al codec;
4. `receiveFrames(frames)` — drena los frames delayed;
5. `m_drained = true` — y `feed()` rechaza input posterior
   (`if (!m_initialized || m_drained) return true;`).

HECHO: por tanto el problema NO era el contenido de `flush()` (parser EOF y
decoder EOF están correctamente separados y ordenados dentro de él), sino su
**momento de invocación**: se llamaba en source EOF, antes del drain.

### 1.5 Chunk parcial final (Phase 5)

HECHO: el bucle de feed ya soporta chunks parciales:
`const uint32_t chunk = std::min(kP314ChunkBytes, available - fedBytes);`
con wrap-split de P3.14.2 intacto. No existía restricción a bloques de 4096:
la restricción era únicamente el gate `!streamEnded`. El tail de 477 bytes se
alimenta por la misma ruta, sin padding, sin duplicación, sin redondeo.

---

## 2. Fix mínimo implementado (Phases 6-10)

Semánticas separadas resultantes (equivalencia con Phase 6, sin estado nuevo
redundante — todo deriva de estado ya existente):

- `sourceEnded` ≡ `playback.streamEnded` (conserva su nombre; ahora significa
  SOLO source EOF).
- `bufferedInputDrained` ≡ `p314ConsumedEsBytes >= videoBytesTotal(+0x28)`
  (accepted queda congelado tras source EOF, la comparación es estable).
- `decoderInputEofSent` / `decoderDrained` ≡ `MpegFfmpegDecoder::m_drained`,
  expuesto vía nuevo accessor `drained()` (líneas ~201-209).
- `playbackFinished` ≡ retorno 1 de `sceMpegIsEnd` (ahora gated por drain).

Invariante implementado: **SOURCE EOF no implica DECODER INPUT EOF mientras
`accepted > consumed`.**

Cambios (7 ediciones, solo MPEG.cpp):

1. **Accessor `drained()`** en `MpegFfmpegDecoder` (~201): expone `m_drained`.
2. **Helper `isP424TraceEnabled()`** (~954): trace opt-in `DMC_P424_TRACE=1`,
   OFF por defecto.
3. **Sitio source-EOF A** (`kMpegProgramEnd`, ~1531): el flush inmediato queda
   condicionado a `!playback.syncGuestEsDecodeActive`. En modo sync se difiere
   (log `[P424:SOURCE_EOF] site=program_end`). El camino async conserva el
   comportamiento previo (flush inmediato) — sin cambio de semántica fuera de
   sync-redecode.
4. **Sitio source-EOF B** (`finishPlaybackStream`, ~1776): misma deferral
   (log `[P424:SOURCE_EOF] site=cd_eof`).
5. **Gate del bloque sync** (~3413): se elimina `!playback.streamEnded`. El
   consumidor sigue drenando el backlog aceptado tras source EOF por la ruta
   P3.14.2 intacta (ring addressing, wrap split y mirror NO tocados).
6. **Flush diferido propiedad del consumidor** (~3681, al cierre del bloque
   sync): cuando `streamEnded && decoder && !decoder->drained() &&
   !decoderFailed && consumed >= accepted` → `flushDecoderIfEnded(playback)`
   (la MISMA llamada que antes se hacía prematuramente; idempotente después
   vía `m_drained`). Log `[P424:DECODER_EOF]`.
7. **`sceMpegIsEnd`** (~4069): nueva condición `syncInputDrained` — en modo
   sync con `streamEnded` y sin `decoderFailed`, exige
   `consumed >= accepted(guest +0x28)` y `decoder->drained()` antes de
   permitir `ended=1`. Evita fin de movie prematuro durante el drain.
   Log único `[P424:FINISH]` al primer retorno 1.

Trazas añadidas (todas gated por `DMC_P424_TRACE=1`): `[P424:SOURCE_EOF]`,
`[P424:FEED]` (solo post-EOF, cap 140), `[P424:TAIL]` (chunk parcial <4096),
`[P424:DECODER_EOF]`, `[P424:FINISH]`.

Explícitamente NO tocado: productor/demux (P4.2.3 congelado), P3.14.2 ring/
wrap/mirror, GS, pacing, inputs, buffers, sin sleeps, sin padding, sin
fabricación de bytes, sin decoder reset.

---

## 3. Resultados de validación

(Se completa con RUN 1/2/3 + clean smoke.)

### 3.0.b Iteración de mecanismo (RUN_094 → RUN_095 → fix final)

El fix inicial (ediciones 1-7) resultó necesario pero NO suficiente. Dos
hallazgos de mecanismo, ambos HECHO por lectura directa de los logs:

**RUN_094** — `[P424:SOURCE_EOF] site=program_end consumed=23183360` confirmó
el modelo (backlog en source EOF exactamente en el valor predicho) y el
consumidor SÍ continuó tras EOF (11 chunks, prefijo canónico verificado por
`cmp`). Pero el guest ejecutó su teardown (`[VIDEO:isFlushed]
sceMpegIsRefBuffEmpty=1` → `[MOVIE:post-flush]` → `[MPEG:delete]`) tras un
solo GetPicture post-EOF. Causa: `sceMpegIsRefBuffEmpty` devolvía
`decodedFrames.empty()`, que es 1 en cuanto el guest consume el último frame
entregado — señal de "flushed" prematura con 426.461 bytes aún sin drenar.
→ **Edición 8**: `sceMpegIsRefBuffEmpty` devuelve 0 en modo sync tras source
EOF mientras `consumed < accepted(guest +0x28)` o el decoder no esté drained.

**RUN_095** — con la edición 8, el guest recibió `isFlushed=0` y ejecutó UN
GetPicture adicional (12 chunks más, consumed llegó a 23.277.568)… y aún así
ejecutó `[MOVIE:post-flush]`. La secuencia completa del guest quedó expuesta:
`[MOVIE:gate18] count18=4294950916 lt5=1` (contador guest expira) →
`[MOVIE:flush-loop] ENTER` → `[VIDEO:flush]` → GetPicture acotados →
`[VIDEO:isFlushed]` → `[MOVIE:post-flush]`. HECHO: la decisión de terminar el
movie es del guest (gate18/tiempo), y su flush-loop solo ejecuta un puñado
acotado de GetPicture. INFERENCIA (consistente con la arquitectura IPU): en
hardware real ese puñado bastaba porque el IPU drenaba viBuf a velocidad DMA;
en el HLE el bucle sync paraba en el primer frame decodificado (~11 chunks
por llamada), por lo que ningún número razonable de iteraciones del guest
completaría 115 chunks.
→ **Edición 9**: post-source-EOF el bucle de feed drena TODO el backlog
restante en una sola pasada (`while ((decodedFrames.empty() || streamEnded)
&& fedBytes < available)`); los frames decodificados se acumulan en el deque
y se entregan en las GetPicture siguientes. Pre-EOF el comportamiento es
idéntico al anterior (para en el primer frame).

**Observación de 4 bytes (pendiente de resolución en RUN 1 final):** en
source EOF el guest reporta `videoBytesTotal(+0x28) = 23.654.881` =
canonical + 4 (available=471.521 = 471.517+4). El accepted capture del stub
es exactamente canónico (23.654.877). INFERENCIA a confirmar con el capture
consumed completo: los 4 bytes extra son un `sequence_end_code` (00 00 01 B7)
que el propio código guest anexa al viBuf al procesar el fin de stream
(comportamiento original del juego, no bytes fabricados por el HLE). Se
verificará inspeccionando los últimos 4 bytes del consumed capture.

### 3.0 Incidente RUN_093 (primer intento de RUN 1, inválido por harness)

HECHO: el primer lanzamiento usó el target por defecto del harness
(`--target '[GP:H]'`). Tras los fixes P3.14.x, `[GP:H]` ya NO aparece en un
run sano (0 ocurrencias es precisamente el gate de éxito), por lo que run.py
cortó el proceso 30 s después de PSS_REACHED (`TARGET_NOT_REACHED`, 101,7 s,
consumed capturado ~11,3 MB). No es un fallo del fix: los prefijos capturados
(11.358.208 bytes consumed, 11.825.349 accepted) se verificaron byte a byte
contra el canónico (`cmp` OK ambos). Los runs válidos usan
`--target "[P314:GP]"` + `--post-target 360`.
