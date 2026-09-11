# P3.13 — SCEMPEGINIT_SESSION_OWNERSHIP_MATRIX_AND_REVERIFY

Continúa directamente P3.12 (scheduler batch ordering, CERRADO y CORREGIDO).
No reabre el scheduler, no toca `sceMpegGetPicture`/`waitExternal`/`waitVSync`,
no reordena el demux. Experimento causal temporal en `sceMpegInit`, opt-in por
variable de entorno, con `baseline` byte-a-byte idéntico al P3.12 validado.

## 1. Checkpoint de entrada

- HEAD principal: `c9630a22ed5ae066bc0d2a2b66f783aee59d3751` — **verificado**,
  coincide exactamente.
- `git status --short` (principal) antes de tocar nada: limpio, coincide con
  el cierre de P3.12.1.
- Vendor HEAD: `bea812535e0595697b4b28e1889e422553112b34` — **verificado**,
  coincide exactamente. Vendor status: limpio antes de esta iteración.
- Ejecutable P3.12 antes de cualquier edición: SHA256
  `a5b0a4c78491bd605cb30eb7471167ed2fbd728e6179072fdb3d54c3ce7f9086` —
  **verificado por lectura directa del archivo**, coincide exactamente.
- Build activo: `analysis/local/symtabfirst/build`. Ninguno de estos cuatro
  valores se asumió; los cuatro se recalcularon independientemente antes de
  editar código.

## 2. Inventario completo de campos de `MpegPlaybackState` (código actual)

Leído directamente de `vendor/PS2Recomp/ps2xRuntime/src/lib/Kernel/Stubs/MPEG.cpp`
(struct en su definición actual, no de notas históricas):

| Campo | Tipo |
|---|---|
| `picturesServed` | `uint32_t` |
| `width` | `uint32_t` |
| `height` | `uint32_t` |
| `decodeMode` | `uint32_t` |
| `imageBufferAddr` | `uint32_t` |
| `sawInput` | `bool` |
| `sawSequenceEnd` | `bool` |
| `streamEnded` | `bool` |
| `decoderFailed` | `bool` |
| `cdStreamGeneration` | `uint64_t` |
| `waitingForVideoSequenceHeader` | `bool` (default `true`) |
| `videoSequenceSyncBuffer` | `std::vector<uint8_t>` |
| `pssBuffer` | `std::vector<uint8_t>` |
| `pssGuestAddrs` | `std::vector<uint32_t>` |
| `decodedFrames` | `std::deque<MpegDecodedFrame>` |
| `decoder` | `std::unique_ptr<MpegFfmpegDecoder>` |
| `frameRateCode`/`frameRateExtensionN`/`frameRateExtensionD`/`hasFrameRateExtension` | parseo de secuencia |
| `videoTimingScanBuffer` | `std::vector<uint8_t>` |
| `pictureIntervalQ32`, `nextPictureTickQ32`, `presentationEndTickQ32`, `firstPresentedPts90k`, `ptsPresentationBaseTickQ32` | estado de presentación |

`MpegRegisteredCallback` (registro de callback, mapa `callbacksByMpeg`
separado): `type`, `streamId`, `func`, `data`, `handle`, `stream`.

## 3. Tabla de clasificación de ownership

| Campo | Rol | Escrito por | ¿Leído tras Init? | Análogo ORIGINAL | Categoría | ¿Debe sobrevivir? | Confianza |
|---|---|---|---|---|---|---|---|
| `width`/`height` | Dimensiones configuradas | `sceMpegCreate` (defaults), `GetPicture` (por frame servido) | Sí | Config en RAM guest, ORIGINAL Init no la toca (RAM survival, P3.11) | SESSION CONFIGURATION | SÍ | Alta |
| `decodeMode` | Modo de decode configurado | guest (no visto aquí, config externa) | Sí | Config en RAM guest | SESSION CONFIGURATION | SÍ | Alta (ya preservado por `makeFreshPlaybackStatePreservingConfig` existente) |
| `imageBufferAddr` | Destino guest del picture decodificado | guest/config | Sí | Config en RAM guest | SESSION CONFIGURATION | SÍ | Alta (idem) |
| `sawInput` | "esta sesión ya recibió bytes de vídeo" | `feedElementaryStream` | No como gate directo de `GetPicture`, sí como señal de identidad de sesión | Sin análogo 1:1; el original no lo necesita porque no separa host/guest | INPUT OWNERSHIP | SÍ (nombrado explícitamente en el prompt) | Alta |
| `sawSequenceEnd` | "el ES anunció su propio fin" | `feedElementaryStream` (start code de secuencia-fin) | Indirectamente, vía `cdStreamGeneration` actualizado en el mismo sitio | Señal derivada del stream, no de hardware | EOF/GENERATION | Media (se incluyó por completitud/paridad con `cdStreamGeneration`) | Media |
| `picturesServed` | Contador monótono de frames servidos al guest | `GetPicture` | Sí (informativo, no gate) | `frameCount` real en RAM guest (P3.9, mpegAddr+0x8) | DERIVED CACHE / SESSION IDENTITY | Opcional, se incluyó por fidelidad (no afecta control de flujo) | Media |
| `pssBuffer` | Bytes PES aceptados aún no parseados | `processPssBuffer`/`feedElementaryStream` | Sí | Ring PSS guest sobrevive (HECHO, P3.11) | INPUT BUFFERING | SÍ | Media-alta (vacío en las corridas observadas; incluido por corrección) |
| `pssGuestAddrs` | Direcciones guest paralelas a `pssBuffer` | idem | Sí | idem | INPUT BUFFERING | SÍ (va emparejado con `pssBuffer`) | Media-alta |
| `cdStreamGeneration` | Identidad de generación de stream de ESTA sesión | `makeFreshPlaybackState()`, resync | Sí (gate en `sceMpegIsEnd`) | Sin análogo — bookkeeping propio del HLE | EOF/GENERATION | Ya resuelto automáticamente por construir vía `makeFreshPlaybackState()` (ver §5) | Alta |
| `decodedFrames` | Cola de imágenes YA decodificadas | `decoder->feed()`/`convertFrame` | Sí (gate directo en `GetPicture`) | Sin análogo directo — el original decodifica DESPUÉS del init, síncrono | DECODED OUTPUT | Depende del modo — es la variable central del experimento | N/A (experimento) |
| `decoder` | Objeto FFmpeg en vivo (parser+codec) | `feedElementaryStream` (`make_unique` lazy) | Sí | Pipeline hardware IPU, el original SÍ lo resetea (HECHO, disasm) | CODEC/PARSER PIPELINE | Depende del modo | N/A (experimento) |
| `streamEnded` | Señal terminal de decodificación | `finishPlaybackStream` | Sí (gate en `GetPicture`/`IsEnd`) | Atado al decoder viejo, destruido | CODEC/PARSER PIPELINE (derivado) | NO — el valor fresco (`false`) ya es correcto para una sesión que retoma | Alta |
| `decoderFailed` | Señal de fallo de decodificación | `feedElementaryStream` | Sí (gate) | idem | CODEC/PARSER PIPELINE | NO | Alta |
| `waitingForVideoSequenceHeader` | "necesito resincronizar con la próxima cabecera de secuencia" | default `true`; `feedElementaryStream` tras fallo | Sí (gate de resync) | Atado al parser, que se resetea | CODEC/PARSER PIPELINE | NO — `true` fresco es exactamente lo correcto para un decoder nuevo | Alta |
| `videoSequenceSyncBuffer` | Buffer de resync de secuencia | `feedElementaryStream` | Sí | Scratch del parser | CODEC/PARSER PIPELINE | NO | Alta |
| `frameRateCode`/`Extension*` | Parseado de la cabecera de secuencia MPEG | parser (no localizado en detalle, pero claramente derivado del bitstream) | Indirecto (vía `pictureIntervalQ32`) | Se re-derivaría al reparsear | CODEC/PARSER PIPELINE / DERIVED CACHE | NO | Media-alta |
| `videoTimingScanBuffer` | Scratch de escaneo de timing | parser | — | idem | CODEC/PARSER PIPELINE | NO | Media-alta |
| `pictureIntervalQ32` | Intervalo de presentación derivado | parser (o default) | Sí | idem | PRESENTATION STATE / DERIVED CACHE | NO | Alta |
| `nextPictureTickQ32`, `presentationEndTickQ32`, `firstPresentedPts90k`, `ptsPresentationBaseTickQ32` | Estado de presentación acumulado | `GetPicture`/`presentationTickForFrame` | Sí (gate de timing) | Sin análogo — bookkeeping HLE de presentación | PRESENTATION STATE | NO — **demostrado por lectura de `presentationTickForFrame` (§6): los defaults frescos (`max`/`-1`) hacen que el PRIMER frame preservado quede "debido ahora" automáticamente**, sin necesidad de copiar nada | Alta (verificado leyendo la función, no supuesto) |

## 4. Ciclo de vida de `callbacksByMpeg`

Mapa global independiente (`std::unordered_map<uint32_t, std::vector<MpegRegisteredCallback>>`),
NO es parte de `MpegPlaybackState`. `resetMpegStubStateUnlocked()` hace
`callbacksByMpeg.clear()` incondicionalmente en cada `sceMpegInit`. El
ORIGINAL `sceMpegInit` (disasm re-verificado en P3.11, sección 3) no escribe
NINGUNA RAM guest — solo stack/MMIO — por lo que cualquier registro de
callback que el juego crea que sobrevive en RAM guest, en RECOMP se pierde
sin motivo. CATEGORÍA: CALLBACK/REGISTRATION. Confianza alta en que debe
sobrevivir (nombrado explícitamente en el prompt).

## 5. Ciclo de vida del estado de transporte GLOBAL

`sceMpegInit` (código actual, sin cambios en esta iteración salvo la
instrumentación aditiva P3.13) ya preserva manualmente, guardando antes de
`resetMpegStubStateUnlocked()` y restaurando después:
`cdStreamGeneration`, `cdStreamBytesProduced`, `cdStreamBytesDemuxed`,
`cdStreamEofPending`, `currentCdStreamEofSeen`. Todo lo demás de
`MpegStubState` (`initialized`, `nextCallbackHandle`, los 6 contadores de
traza) se resetea sin excepción — administrativo/diagnóstico, no ownership
de sesión, correcto que se reinicie.

## 6. Auditoría de generación / `sceMpegIsEnd` — riesgo de contradicción

`sceMpegIsEnd` (código actual): `producerEnded = currentCdStreamEofSeen &&
playback.cdStreamGeneration == g_mpeg_stub_state.cdStreamGeneration`.

**HECHO, verificado por lectura de código**: el ÚNICO sitio que incrementa
`g_mpeg_stub_state.cdStreamGeneration` es `notifyMpegCdStreamStart()`
(`++g_mpeg_stub_state.cdStreamGeneration`). **HECHO, reconfirmado esta
iteración con un grep nuevo sobre TODO el repositorio** (no solo `MPEG.cpp`,
incluyendo `ps2xTest/`): `notifyMpegCdStreamStart` tiene CERO call sites
fuera de su propia definición — código muerto, igual que P3.10 ya había
establecido, con la precisión de que esta vez se verificó también contra
`ps2xTest` explícitamente (una duda propia de esta sesión, resuelta: no
hay ningún uso en tests tampoco).

**Consecuencia demostrada**: `g_mpeg_stub_state.cdStreamGeneration`
permanece en `0` durante toda la vida de cualquier corrida real de RECOMP
actual (nunca se incrementa en la práctica). `getPlaybackState()` (bare
`operator[]`, usado por `sceMpegIsEnd`/`sceMpegGetPicture` para
auto-vivificar una entrada tras el `clear()`) usa el inicializador de
miembro por defecto de la struct (`cdStreamGeneration = 0u`), NO el valor
global actual — mientras que `makeFreshPlaybackState()` SÍ lo toma
correctamente de `g_mpeg_stub_state.cdStreamGeneration`.

**Riesgo de contradicción, tal como lo planteó Fable (P3.11.1)**:
CONFIRMADO como mecanismo latente real — si `cdStreamGeneration` global
fuera alguna vez distinto de 0 en el momento de una auto-vivificación vía
`operator[]` puro (no `makeFreshPlaybackState()`), `sceMpegIsEnd` quedaría
permanentemente incapaz de reportar `ended=true` para esa sesión. **NO
afecta la corrida actualmente auditada** (ambos lados son 0 por
coincidencia, dado que el mecanismo de incremento nunca se ejecuta). Es un
defecto arquitectural latente, no la causa del stall observado en P3.8-P3.12.
No se corrige aquí (pertenece al futuro diseño de ownership, según pide el
prompt) — el diseño experimental P3.13 lo evita estructuralmente al
construir SIEMPRE los estados reconstruidos vía `makeFreshPlaybackState()`
(nunca vía `operator[]` puro), ver §8.

## 7. Comportamiento exacto de `sceMpegInit` baseline (P3.12, sin cambios)

Guarda 5 campos globales de transporte → `resetMpegStubStateUnlocked()`
(`playbackByMpeg.clear()`, `callbacksByMpeg.clear()`, resto de
`MpegStubState` a default) → restaura los 5 campos globales →
`initialized=true` → retorna `v0=0`. El mapa `playbackByMpeg` queda VACÍO
hasta la siguiente auto-vivificación (vía `sceMpegIsEnd`, confirmado en
P3.10/P3.11). Ningún campo por-sesión sobrevive.

## 8. Diseño experimental exacto (P3.13)

Un único mecanismo, cuatro modos, sin duplicar `sceMpegInit` cuatro veces:

1. `parseP313InitMode()`: lee `DMC_P313_INIT_MODE` (`baseline` si ausente;
   valor inválido → log `[P313:init] INVALID ...` + `baseline`).
2. Antes del reset: se calculan contadores agregados "before" (genéricos,
   iterando `g_mpeg_stub_state.playbackByMpeg` como esté, SIN hard-codear
   ninguna dirección) y, solo si el modo no es `baseline`, se hace
   `std::move` de `playbackByMpeg`/`callbacksByMpeg` a variables locales
   (necesario: `MpegPlaybackState` no es copiable por el `unique_ptr` del
   decoder).
3. Camino común sin cambios: guardar 5 campos globales →
   `resetMpegStubStateUnlocked()` → restaurar los 5 campos globales.
4. Si el modo no es `baseline`: por cada `(key, oldState)` del snapshot,
   `g_mpeg_stub_state.playbackByMpeg[key] = applyP313OwnershipMode(mode,
   oldState)` — genérico sobre las claves realmente presentes, nunca
   `0x0087D8F8` hard-codeado. `callbacksByMpeg` se restaura completo.
5. `applyP313OwnershipMode`: parte SIEMPRE de `makeFreshPlaybackState()`
   (por eso `cdStreamGeneration` queda correcto automáticamente, ver §6),
   copia el subconjunto de campos de la tabla §3 según el modo, y retorna.
6. Log único por invocación: `[P313:init] mode=... keysBefore=...
   keysAfter=... callbackKeysBefore/After=... framesBefore/After=...
   decoderBefore/After=... sawInputBefore/After=...`.

`DO NOT modify sceMpegGetPicture/waitExternal/waitVSync/scheduler/demux`:
cumplido — el único archivo tocado es `MPEG.cpp`, y dentro de él solo se
añadieron las funciones nuevas y se editó exclusivamente el cuerpo de
`sceMpegInit` (más una nota de mismatch en el mismo cuerpo de
`makeFreshPlaybackStatePreservingConfig`, sin cambiarla).

## 9. Archivos cambiados

- `vendor/PS2Recomp/ps2xRuntime/src/lib/Kernel/Stubs/MPEG.cpp`: nuevas
  funciones `P313InitMode`/`parseP313InitMode`/`p313ModeName`/
  `applyP313OwnershipMode` (tras `makeFreshPlaybackStatePreservingConfig`);
  `sceMpegInit` extendida con snapshot/rebuild/log P3.13. Ninguna otra
  función tocada.
- `analysis/tools/runtime_loop/run.py`: un cambio de una línea —
  `meta['diagnostic_env']` ahora incluye prefijos `DMC_P311_` Y `DMC_P313_`
  genéricamente (antes solo `DMC_P311_`). Sin cambios de timing/automation.

## 10. Método de build

1. `analysis/local/p313/` creado.
2. Backup del `ps2_runtime.lib` VALIDADO de P3.12 (el que ya contiene el
   fix de `EeScheduler.cpp`) a
   `analysis/local/p313/ps2_runtime.p312_baseline.lib`, SHA256
   `042f2e39f69119e9c0f11d55f72ac95f3480b6d3b1308c40b0c2fef1b6c4de4f`,
   ANTES de cualquier compilación P3.13.
3. Build incremental vía `ps2_runtime.vcxproj` (destino por defecto,
   confirmado seguro repetidas veces en esta sesión): compiló
   **únicamente** `MPEG.cpp` (verificado: 1 sola invocación de `CL.exe` en
   el log, sobre esa ruta exacta), 0 advertencias, 0 errores, 5.51 s.
4. Relink vía `ps2EntryRunner.vcxproj /t:_BuildLinkAction` exclusivamente:
   0 invocaciones de `CL.exe` (verificado con `grep -c` sobre el log
   completo — cero recompilación de C++ generado), 1 advertencia
   (`/INCREMENTAL` ignorado por `/FORCE`, ya conocida e inocua), 0
   errores, ~2m54s.
5. No se usó `build_one.ps1 -Mpeg` ni el backup antiguo de P3.11
   (`ps2_runtime.before_mpeg_probe.lib`) — advertencia explícita del
   prompt respetada.

## 11. Prueba de que NO se reutilizó una librería P3.11 obsoleta

El backup tomado en el paso 2 de §10 es el `ps2_runtime.lib` que estaba
activo INMEDIATAMENTE antes de esta iteración — el mismo que produjo el
ejecutable P3.12 validado (`a5b0a4c7...`), que a su vez YA contiene el fix
de `EeScheduler.cpp` de P3.12 (confirmado: ese exe fue el resultado directo
del relink de P3.12, sin ediciones posteriores hasta este prompt). No se
tocó ni se restauró en ningún momento
`analysis/local/p311/ps2_runtime.before_mpeg_probe.lib` (el backup P3.11
que SÍ predata el fix del scheduler).

## 12. Nuevo ejecutable

SHA256 `1c49618546987aad4eb28e7101b43984d3bf8c7175fedd578fcfdb4d55265a40`,
347644416 bytes, 2026-09-11 16:07.

## 13. Confirmación: mismo ejecutable en los 4 modos

Los `result.json` de las 8 corridas (RUN_015 a RUN_022) registran
`exe_sha256=1c49618546987aad4eb28e7101b43984d3bf8c7175fedd578fcfdb4d55265a40`
en las ocho, sin excepción — verificado, no asumido. Solo cambió
`DMC_P313_INIT_MODE` entre corridas (confirmado también vía
`diagnostic_env` en cada `result.json`, gracias al cambio de §9).

## 14. Resultado — control BASELINE (RUN_015)

```
[P313:init] mode=baseline keysBefore=1 keysAfter=0 callbackKeysBefore=1 callbackKeysAfter=0
            framesBefore=4 framesAfter=0 decoderBefore=1 decoderAfter=0 sawInputBefore=1 sawInputAfter=0
[GP:A] #1 entry ... decodedFrames=0 ... sawInput=0 ...
[GP:H] #1 waitExternal(...) -- SUSPENDING, no frame available
```

Idéntico, campo a campo, a P3.10/P3.11/P3.12 (mismo stall, mismos valores
en `GP:A`). **La maquinaria del experimento no altera el comportamiento
baseline.** Una sola corrida fue suficiente (regla explícita del prompt: la
reproducción 3/3 de este comportamiento ya la demostró P3.12).

## 15. Resultado — OWNERSHIP (RUN_016)

```
[P313:init] mode=ownership keysBefore=1 keysAfter=1 callbackKeysBefore=1 callbackKeysAfter=1
            framesBefore=4 framesAfter=0 decoderBefore=1 decoderAfter=0 sawInputBefore=1 sawInputAfter=1
[GP:A] #1 entry ... decodedFrames=0 ... sawInput=1 ...
[GP:H] #1 waitExternal(...) -- SUSPENDING, no frame available
```

`sawInput` y la existencia de la clave/callbacks SÍ sobreviven (a
diferencia de baseline); `decodedFrames`/`decoder` correctamente NO
sobreviven (por diseño del modo). **Resultado: GetPicture sigue entrando en
`GP:H` con `decodedFrames=0` — ownership por sí solo es INSUFICIENTE** para
cruzar el primer `GetPicture`. Una corrida basta (no fue un éxito
sorpresivo que requiriera repetición).

## 16. Resultado — QUEUED (RUN_017, RUN_018, RUN_019 — 3/3)

```
[P313:init] mode=queued ... framesBefore=4 framesAfter=4 decoderBefore=1 decoderAfter=0 sawInputAfter=1
[GP:A]#1 decodedFrames=4 ... -> GP:B -> GP:D -> [MPEG:diag] getPicture success #1 size=512x448 imageAddr=0x79d680 -> [GP:CONT]#1 v0=0
[GP:A]#2 decodedFrames=3 ... success #2 ... [GP:CONT]#2
[GP:A]#3 decodedFrames=2 ... success #3 ... [GP:CONT]#3
[GP:A]#4 decodedFrames=1 ... (RUN_019: un desvío GP:G/waitVSync de una sola llamada, luego) success #4 ... [GP:CONT]#4
[GP:A] siguiente: decodedFrames=0 -> [GP:H] waitExternal SUSPENDING
```

**Exactamente 4 éxitos de `GetPicture` en las 3 corridas** (coincide con las
4 imágenes pre-init), luego stall inmediato. `grep -c "frame decoded,
total=5"` → **0 en las tres corridas**: ningún frame nuevo se decodifica
tras el init. Ventana de observación post-target de 30 s: solo
`[GP:VSync]` (proceso vivo, scheduler corriendo), ningún nuevo `feed
call`/`frame decoded`/`getPicture ENTRY` más allá del quinto (vacío). El
pequeño desvío de RUN_019 (`GP:G` una vez, antes del 4º éxito) es el
mecanismo de pacing por PTS ya establecido y explícitamente no tocado —
NO es una discrepancia real; el conteo de éxitos (`getPicture success #N`)
es idéntico en las tres corridas.

## 17. Resultado — FULL (RUN_020, RUN_021, RUN_022 — 3/3)

```
[P313:init] mode=full ... framesBefore=4 framesAfter=4 decoderBefore=1 decoderAfter=1 sawInputAfter=1
```

`decoder` correctamente preservado (`decoderAfter=1`). En las tres
corridas: **10 éxitos de `GetPicture`** (`getPicture success #1` .. `#10`),
y **6 frames NUEVOS decodificados DESPUÉS del init**
(`frame decoded, total=5` en feed call #62, hasta `total=10` en feed call
#127 — todos posteriores al feed call #49 pre-init). El juego alcanza
`getPicture ENTRY #50` y sigue leyendo chunks reales de CD/movie
(`[ps2xIOP] [CDMODULE/MOVIE:chunk] offset=0x210000..0x230000`, muy por
encima del prefijo pre-init de 262144 bytes) y despachando callbacks de
audio PCM reales (`PCM:StrPcmCallBack-entry #68..#78`). **Progreso
sostenido real, no solo un puente de 4 frames.**

En las tres corridas, el proceso termina por sí solo con `exit_code=0`
(salida limpia, no crash) al agotar un recurso NO relacionado con la
pregunta causal de este prompt:
```
[RuntimeGuestArena] async callback stack exhausted (requested 0x4000 bytes, arena=[0x9fa000,0xabe000)
Error during program execution: EE invocation stack space exhausted
```
Este es el mismo arena `[0x9fa000,0xabe000)` establecido en P3.7.1 para
`RuntimeGuestArena`/pila de callbacks asíncronos — dimensionado bajo el
supuesto (válido hasta ahora) de que la ejecución se estancaría pronto.
MODE D permite tanta profundidad de invocaciones anidadas sostenidas que
agota ese arena. **Hallazgo lateral real, HECHO, pero explícitamente fuera
de alcance de la pregunta causal de `sceMpegInit`** — no se investiga ni se
corrige aquí. RUN_022 quedó clasificado `BOOT_ONLY` en su `result.json` por
un artefacto de la heurística de clasificación del harness ante una salida
abrupta (el propio `runtime.log` de RUN_022 muestra la MISMA evidencia
real que RUN_020/021: 10 éxitos, 10 frames totales, mismo agotamiento de
arena) — no se interpreta como una corrida distinta o inválida.

Capturas de pantalla (`state_01.png` etc.): fondo predominantemente negro
con un artefacto de color menor en una esquina, sin contenido de película
reconocible — consistente con la limitación de renderizado GS ya conocida
y fuera de alcance (README: "Rendering is incomplete/corrupted past the
title screen"). No se afirma corrección visual a partir de esto.

## 18. Tabla resumen

| Modo | callbacks sobreviven | config sesión sobrevive | decoder sobrevive | frames en cola sobreviven | 1er GetPicture | ¿GP:H? | Éxitos | Feeds post-init | Frames decodificados post-init | Progresión |
|---|---|---|---|---|---|---|---|---|---|---|
| baseline | No | No | No | No | vacío | Sí | 0 | 0 | 0 | Ninguna (stall inmediato) |
| ownership | Sí | Sí | No | No | vacío | Sí | 0 | 0 | 0 | Ninguna (stall inmediato) |
| queued | Sí | Sí | No | Sí (4) | con frame | No (hasta agotar cola) | 4 | 0 | 0 | Puente de exactamente 4, luego stall |
| full | Sí | Sí | Sí | Sí (4) | con frame | No (nunca, en la ventana observada) | 10 | Sí (≥6 feeds nuevos) | 6 | Sostenida, hasta agotar un recurso no relacionado |

## 19. Frames pre-init (exacto, todas las corridas)

**4**, en los mismos feed calls #9/#23/#36/#49 en las 8 corridas, sin
variación.

## 20. Frames post-init (exacto, por modo)

baseline: 0. ownership: 0. queued: 0 (3/3). full: 6 (3/3, `total=5..10`).

## 21. ¿Está representado en RECOMP el boundary equivalente a `0x47A8EC`?

**SÍ, demostrado dinámicamente, en QUEUED (4 veces, 3/3) y en FULL (10
veces, 3/3)**: `[GP:CONT] #N guest continuation after sceMpegGetPicture
reached, v0=0` — este tag está anclado exactamente en `label_47a8ec` del
código generado `MpegMovieDecode_0x47a760.cpp` (confirmado en P3.8, sin
cambios desde entonces). `v0=0` (no negativo) es consistente con el hecho
ya establecido en P3.9 de que el caller solo examina el SIGNO de `v0`
(`bgez`), no su valor exacto — el 1 real de ORIGINAL y el 0 de RECOMP
ambos toman la misma rama "éxito" desde la perspectiva del guest. En
baseline y ownership este boundary NUNCA se alcanza (la ejecución queda
suspendida en `waitExternal` antes de llegar ahí).

## 22. ¿El progreso se extiende más allá de la cola preservada inicial?

**NO en QUEUED (3/3, exactamente 4, ni uno más)**. **SÍ en FULL (3/3, 10
en total, 6 nuevos)** — pero solo cuando el objeto `decoder` en vivo
también se preserva. Esto aísla con precisión la variable: la cola de
salida por sí sola basta para el boundary inmediato; la CONTINUIDAD del
decoder es lo que permite sostener el progreso más allá de eso, con la
arquitectura HLE actual.

## 23. ¿Apareció salida visual de película?

No se observó contenido de película reconocible en ninguna captura de
las 8 corridas (ver §17). Esto es consistente con — y no aporta evidencia
nueva sobre — la limitación de renderizado GS ya documentada como fuera de
alcance del propio BLOCKER_004.

## 24. Veredicto causal

**Contribuyente causal necesario, confirmado**: el `playbackByMpeg.clear()`
(+`callbacksByMpeg.clear()`) de `sceMpegInit` SÍ es necesario para producir
el stall vacío observado desde P3.8 — sin él (ownership/queued/full),
`GetPicture` deja de entrar en `GP:H` inmediatamente.

**Suficiencia del ownership puro**: REFUTADA por evidencia directa (§15) —
preservar SOLO identidad/config lógica de sesión NO basta; el stall
persiste idéntico.

**Rol de la cola de frames ya decodificados**: bridge SUFICIENTE para el
boundary caller-visible inmediato (4/4 éxitos, 3/3 corridas), pero
INSUFICIENTE por sí sola para progreso sostenido (se detiene en
exactamente 4, 3/3 corridas).

**Rol de la continuidad del decoder**: DEMOSTRADO REQUISITO ARQUITECTURAL
de la HLE ACTUAL (no del hardware original) para progreso sostenido más
allá de la cola inicial — 3/3 corridas alcanzan 10 éxitos y 6 frames
nuevos únicamente cuando el `decoder` vive a través del Init.

## 25. Clasificación

**QUEUED_OUTPUT_BRIDGE_REQUIRED**, con una precisión adicional demostrada
más allá de lo mínimo pedido por el prompt: el bridge de cola por sí solo
es necesario Y suficiente para cruzar el primer boundary caller-visible,
pero la arquitectura actual REQUIERE ADEMÁS continuidad del decoder para
cualquier progreso sostenido más allá de esa cola fija — un segundo hecho
demostrado (FULL, 3/3), no una inferencia.

## 26. `sceMpegInit clear()` — necesario, suficiente o parte de un mismatch mayor

**Contribuyente causal necesario para ESTE stall específico**: SÍ,
demostrado (baseline vs. ownership/queued/full).
**Causa raíz suficiente por sí sola para explicar el bloqueo completo de
BLOCKER_004**: NO — incluso con el `clear()` corregido/evitado
(ownership), el stall persiste; hace falta ADEMÁS un puente de salida
(queued) y, para sostener progreso, continuidad de decoder (full). Es
**una pieza de un mismatch arquitectural mayor** entre cómo el HLE separa
trabajo host/guest y cómo el original decodifica síncronamente dentro de
una única llamada a `_getpic` (P3.9).

## 27. Preservar decoder/frames: ¿requisito de la HLE actual o equivalencia con hardware original?

**Exclusivamente lo primero, demostrado; NO lo segundo, no reclamado.**
Los cuatro modos son sondas causales temporales sobre la arquitectura HLE
actual. El original resetea hardware IPU/DMA real en su `sceMpegInit`
(HECHO, P3.11 §3) — preservar el objeto `AVCodecContext`/cola de frames de
FFmpeg en RECOMP NO es, ni se afirma que sea, un análogo de ningún estado
que el hardware IPU original conserve. Es una demostración de qué necesita
la implementación ACTUAL de RECOMP para no bloquearse — un dato de diseño
para el futuro fix, no una validación de fidelidad con el hardware.

## 28. Nuevo LAST COMMON NODE

Todo el segmento `MpegMovieDecode ENTRY → MpegMovieDecodeInit →
sceMpegInit@0x109CD0` sigue siendo el último nodo común demostrado con
ORIGINAL (sin cambios respecto a P3.11/P3.12 — este prompt no tocó nada
antes de `sceMpegInit`).

## 29. Nuevo FIRST DIVERGENT NODE

Sin cambios en su ubicación (`sceMpegInit`'s destrucción de
callbacks/config/ownership vs. el reset solo-hardware del original), pero
con alcance MÁS PRECISO tras esta iteración: la divergencia no es un
único defecto binario, sino DOS necesidades arquitecturales distintas y
demostradas por separado en la arquitectura HLE actual — (a) ownership de
sesión + cola de salida, para cruzar el primer `GetPicture`
caller-visible; (b) continuidad del decoder, para sostener progreso más
allá de eso. ("Primera divergencia contractual DENTRO del segmento
auditado" — no se reclama que sea la primera divergencia absoluta desde
cold boot, siguiendo la instrucción explícita del prompt.)

## 30. Próximo prompt recomendado

**P3.14 — GETPICTURE_SYNCHRONOUS_REDECODE_FROM_GUEST_ES.** Justificación:
P3.13 ya demostró que ni "solo ownership" ni "ownership + cola fija" bastan
para progreso sostenido con la arquitectura HLE actual, y que preservar
literalmente el decoder/cola completos (FULL) SÍ sostiene progreso pero (a)
no es una implementación de producción aceptable (el original resetea
hardware real) y (b) expone un límite de recursos no relacionado
(`RuntimeGuestArena`) antes de poder observarse indefinidamente. El
siguiente paso de máxima información es diseñar — sin implementar todavía
en ese mismo prompt salvo que se autorice explícitamente — un contrato de
`GetPicture`/decode que reconstruya síncronamente desde el ES guest ya
disponible (`viBuf`, ahora byte-exacto por P3.12) en vez de depender de
persistir el objeto decoder de FFmpeg a través de `sceMpegInit`,
preservando SOLO la ownership lógica demostrada suficiente en P3.13
(§15-16) y evitando el mismatch de "adelantar decode al demux" ya
identificado por Fable (P3.11.1 §6). **No implementado en este prompt.**

## 31. No implementado en este prompt

Confirmado: no se implementó ningún fix de producción para `sceMpegInit`;
los cuatro modos son sondas experimentales temporales, marcadas
explícitamente como tales en el código (comentarios "P3.13 experimental
ownership matrix", "temporary, causal probe only").

## 32. Git status final

```
 M vendor/PS2Recomp/ps2xRuntime/src/lib/Kernel/Stubs/MPEG.cpp   (vendor, no versionado directamente en principal)
 M analysis/tools/runtime_loop/run.py
?? analysis/notes/BLOCKER_004_P313_SCEMPEGINIT_OWNERSHIP_MATRIX.md
```
más este informe. `analysis/local/p313/` y las nuevas `analysis/local/p311/RUN_015..022/`
son artefactos locales, ya cubiertos por `.gitignore` (`/analysis/local/`).
Repo principal: solo los cambios de esta iteración (`run.py` + este
informe) más lo ya pendiente de la nota canónica (apéndice, añadido a
continuación). Vendor: dirty únicamente en `MPEG.cpp` (instrumentación
P3.13 aditiva) respecto a `bea812535e...`.

## 33. Confirmaciones explícitas de seguridad

NO commit. NO push. NO clean. NO regenerate. NO build masivo de
`generated/` (verificado en cada build: exactamente 1 `CL.exe` sobre
`MPEG.cpp` en el primer build, 0 en el relink). NO se modificó PCSX2 (no se
usó en esta iteración). NO se cambió `sceMpegGetPicture`, `waitExternal`,
`waitVSync`, el scheduler (`EeScheduler.cpp` no se tocó), ni el orden del
demux. NO se terminó ningún proceso ajeno — solo instancias propias de
`dmc-recomp.exe` lanzadas por esta misma sesión, verificadas antes de cada
build/corrida.

**Resultado Prompt P3.13 —
SCEMPEGINIT_SESSION_OWNERSHIP_MATRIX_AND_REVERIFY**
