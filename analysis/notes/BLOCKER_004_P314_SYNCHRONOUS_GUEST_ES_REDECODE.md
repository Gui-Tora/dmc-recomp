# BLOCKER_004 — Prompt P3.14: GETPICTURE_SYNCHRONOUS_REDECODE_FROM_GUEST_ES

Continuación de [BLOCKER_004_P313_SCEMPEGINIT_OWNERSHIP_MATRIX.md](BLOCKER_004_P313_SCEMPEGINIT_OWNERSHIP_MATRIX.md)
y [BLOCKER_004_P3131_VISUAL_LANGUAGE_TRACE.md](BLOCKER_004_P3131_VISUAL_LANGUAGE_TRACE.md),
sobre el estado congelado en `CHECKPOINT_P313` (commit `3483f92`,
vendor `patched_commit=ed83ab53fa4635715eb10b72bdfe9fa7469e26cd`).

## 1. Punto de partida (checkpoint de entrada)

- Vendor limpio en `ed83ab53...` antes de tocar `MPEG.cpp` (verificado
  con `git -C vendor/PS2Recomp status --short` antes de empezar).
- P3.13 estableció (HECHO, 8 corridas): `ownership` (config/callbacks/
  sawInput sobreviven, decoder/frames se descartan) es **insuficiente**
  por sí solo — el stall original se reproduce igual. `queued` (+4
  frames preservados) da exactamente 4 éxitos de GetPicture y nada más.
  `full` (+decoder vivo preservado) da 10 éxitos + 6 frames nuevos, pero
  3/3 termina en agotamiento de `RuntimeGuestArena` (async callback
  stack), un límite de recursos separado y fuera de alcance.
- Hipótesis de P3.14: si el problema NO es "necesitamos el decoder viejo"
  sino "necesitamos que el mecanismo interno de decode tenga ES real que
  consumir después del reset", entonces un decoder fresco + reconstrucción
  del ES pendiente desde el propio `viBuf` guest (que P3.11 ya demostró
  que sobrevive byte-idéntico al init) debería bastar sin depender de
  ningún objeto pre-init.

## 2. Mapa exacto de `viBuf` (offsets relativos a `movieState`)

Obtenido por lectura completa del código generado real (no de notas
previas), archivos en `analysis/local/symtabfirst/generated/`:

| Offset | Campo | Fuente |
|---|---|---|
| `+0x1C` | base guest del buffer ES | `viBufBeginPut_0x47ae90.cpp:137,169,187` |
| `+0x20` | capacidad del ring buffer | `viBufBeginPut_0x47ae90.cpp:94,178,193` (divisor real de un `div`, confirma ring buffer, no buffer plano) |
| `+0x28` | `videoBytesTotal`, contador acumulado monótono de bytes escritos por el productor | `viBufEndPut_0x47af80.cpp` (`state[0x28] += lenWritten`) |
| `+0x2C0` | cursor de bloque (consumo real, DMA) | `viBufAddDMA_0x47b0d0.cpp` |
| `+0x2C4` | bloques encolados (consumo real, DMA) | ídem |
| `+0x2C8` | bytes pendientes dentro del bloque actual | ídem |
| `+0x2F8` | semáforo de acceso al buffer | `viBufBeginPut`/`viBufEndPut` (`WaitSema`/`SignalSema`) |

`movieState = mpegAddr - 0x278` — offset estructural confirmado
estáticamente por Astra en P3.11 ("MPEG state+0x278" == mpegAddr),
específico de este binario DMC, no una convención genérica de PS2/MPEG.

## 3. Fórmula de "ES lógicamente pendiente, no leído"

```
consumedBefore = playback.p314ConsumedEsBytes   // cursor host-only, arranca en 0 tras cada sceMpegInit
available       = videoBytesTotal - consumedBefore   // 0 si no hay dato nuevo
srcAddr         = viBufBase + consumedBefore
```

**No** replica el mecanismo real de consumo (`viBufAddDMA`, que convierte
bytes pendientes en tags DMA para el IPU y avanza `blockCursor`/
`queuedBlocks`/`pendingBytes`) — esos tres campos nunca se leen ni se
escriben desde el código nuevo. El cursor `p314ConsumedEsBytes` es
puramente host-side y nunca se refleja en RAM guest.

### 3.1 Manejo de wraparound — verificado, no solo asumido

`viBufBeginPut` calcula la posición de escritura como
`base + (posición_acumulada % capacity)`, confirmando que el buffer real
es un ring buffer, no un buffer plano infinito. Mi fórmula
`srcAddr = viBufBase + consumedBefore` es **lineal, sin `% capacity`** —
si el consumo acumulado en una corrida superase `capacity`, leería más
allá del ring buffer real (memoria guest no relacionada), produciendo
"éxito" sobre datos inválidos sin que el código lo detectara como error
estructural (aunque el decoder probablemente fallaría a la primera
trama no-MPEG válida).

Esto era un supuesto no verificado en la primera tanda de validación
(RUN_029–031). Se cerró explícitamente en esta misma iteración: se
añadió una lectura adicional de solo diagnóstico (`viBufCapacity =
movieState+0x20`, sin cambio de comportamiento) y se rehizo el build
dirigido + una corrida de confirmación (RUN_032, ver sección 15).
**HECHO, verificado dinámicamente**: `viBufCapacity=524288` (512 KiB)
en las 10 llamadas exitosas de RUN_032; el consumo acumulado máximo
observado en cualquier corrida fue `516096 + 1217 = 517313` bytes,
**por debajo** de la capacidad — ninguna corrida de este prompt cruzó
el límite del ring buffer. La fórmula lineal es correcta *dentro de esta
ventana de observación (10 fotogramas, ~100 s)*, no por diseño general.

**LIMITACIÓN EXPLÍCITA (no corregida en este prompt)**: si una sesión
de reproducción más larga consumiera más de 524288 bytes tras el reset,
`srcAddr` cruzaría el buffer real sin wraparound y leería memoria guest
incorrecta. Esto no se ha observado ni ejercitado — se documenta como
riesgo conocido, no como bug confirmado.

### 3.2 Validación byte-exacta — heredada, no re-derivada de forma independiente

Los campos `+0x1C` (base) y `+0x28` (total de bytes) son los MISMOS que
P3.12 ya validó como correctos indirectamente: el fix de orden de lotes
del scheduler hizo que el `viBuf` guest completo coincidiera
byte-a-byte con el ES MPEG esperado (SHA256
`127e3b62…b82c`, 3/3). P3.14 reutiliza esos mismos offsets y confía en
esa validación previa — **no se repitió aquí, como paso independiente,
un nuevo hash-compare del ES reconstruido por P3.14 contra el ES
esperado**. Lo que SÍ se verificó de forma independiente en esta
iteración: que FFmpeg decodifica 10 tramas reales, consecutivas y
válidas desde esos bytes en 4/4 corridas (evidencia indirecta fuerte de
que el contenido es MPEG real, no basura), y que el propio consumo
nunca superó la capacidad del buffer (sección 3.1). Se documenta como
INFERENCIA fuertemente soportada, no como HECHO de re-validación directa.

## 4. Por qué se descarta el decoder pre-init y las 4 tramas pre-init

- **Decoder pre-init**: `sceMpegInit` fuerza el modo `ownership` de
  P3.13 (`decoderAfter=0` en el log `[P313:init]`), así que al llegar a
  `sceMpegGetPicture` no existe ningún `AVCodecContext` heredado.
  `playback.decoder` se crea con `std::make_unique<MpegFfmpegDecoder>()`
  únicamente la primera vez que hace falta (`decoderFresh=1` solo en la
  llamada #1 de cada corrida, `=0` en el resto porque ya existe el
  fresco creado en #1 — nunca el viejo).
- **4 tramas pre-init**: el mismo `[P313:init]` log confirma
  `framesAfter=0` — la cola `decodedFrames` queda vacía tras el reset.
  Cada entrada a `sceMpegGetPicture` en el bloque nuevo registra
  `frames=0` antes de decidir si sincroniza, confirmando que nunca hay
  una trama residual disponible para "hacer trampa" devolviendo una
  vieja.

## 5. Arquitectura exacta implementada (P3.14)

Nuevo bloque en `sceMpegGetPicture`, insertado inmediatamente después
del log `[GP:A]` y **antes** del `if` existente que decide
`waitExternal` (no lo reemplaza, no lo bypasea — simplemente, si el
bloque nuevo llena `decodedFrames`, la condición existente
`decodedFrames.empty()` pasa a falso y el camino de espera nunca se
ejecuta, sin tocar su lógica).

Disparador exacto: `playback.decodedFrames.empty() &&
isP314SyncRedecodeEnabled() && !playback.streamEnded &&
!playback.decoderFailed`.

Política de alimentación acotada (NO decodifica el ES completo de una
vez): bucle `while (decodedFrames.empty() && fedBytes < available)`
alimentando en fragmentos de `kP314ChunkBytes=4096` bytes (tamaño
observado de los payloads PES reales), avanzando
`playback.p314ConsumedEsBytes` en cada fragmento, hasta que
`decoder->feed()` produzca una trama o el ES disponible se agote. Si
`available==0` o `viBufBase==0`, no se intenta nada y se cae al camino
`waitExternal` existente sin cambios.

Ninguna trama se fabrica: la única fuente de `decodedFrames` es el
`bool` de retorno real de `MpegFfmpegDecoder::feed()`.

## 6. Distinción "sin salida todavía" vs "sin ES disponible"

El log `[P314:GP] #N pre-sync` registra `available` antes de decidir.
Cuando `available==0` (ES agotado hasta el próximo lote de demux), se
emite `"no guest ES available ... falling back to waitExternal"` y el
control cae al camino existente — no se reintenta en bucle ni se
inventa una espera nueva. Esto se observó exactamente una vez por
corrida (llamada #11, `available=1217 < 4096`, insuficiente para otra
trama completa — el decoder no produjo salida con ese remanente y el
código correctamente reportó `framesAfter=0`, sin declarar éxito).

## 7. Archivos modificados

Solo `vendor/PS2Recomp/ps2xRuntime/src/lib/Kernel/Stubs/MPEG.cpp`, sobre
el HEAD ya congelado en `ed83ab53...` (P3.13). Diff final de este
prompt: **+125/-1** líneas (incluye el campo de diagnóstico
`viBufCapacity` añadido en la sección 3.1). Ningún otro archivo del
vendor ni del repo principal se tocó. `EeScheduler.cpp` no se modificó
(confirmado por `git diff --stat`).

## 8. Método de build (seguro, verificado)

1. Backup previo del `.lib` pre-P3.14:
   `analysis/local/p314/ps2_runtime.p313_baseline.lib`
   (SHA256 `0a21fc5483ff4b7d96a2f38de22801a89cc213b8b7d02911f9c95c1ba6149482`),
   tomado antes de escribir cualquier código de P3.14 — no se reutilizó
   ningún backup de P3.11/P3.13.
2. Build dirigido de un único archivo: MSBuild directo sobre
   `ps2_runtime.vcxproj` (árbol real usado por `run.py`:
   `analysis/local/symtabfirst/build/upstream/ps2xRuntime/`), verificado
   con `grep -c "CL.exe"` = 1 invocación, exclusivamente sobre
   `MPEG.cpp` con `PS2X_HAS_FFMPEG=1`.
3. Relink: `ps2EntryRunner.vcxproj /t:_BuildLinkAction`, verificado 0
   invocaciones de `CL.exe` en el log (ningún archivo generado
   recompilado).
4. **Nota de proceso**: para la corrección de la sección 3.1 (campo
   `viBufCapacity`) se repitió build+relink sobre el mismo árbol sin
   tomar un nuevo backup intermedio — el cambio es un `Read` adicional
   más una línea de log, sin alterar ninguna decisión de control; se
   verificó el diff exacto antes de reconstruir (sección 3.1) y el
   resultado (RUN_032) reproduce byte-a-byte el patrón de RUN_029-031.
   Se documenta como desviación menor del procedimiento estricto de
   backup-por-build, no como riesgo real dado el contenido verificado
   del cambio.
5. Un intento inicial de build apuntó por error al árbol
   `build/runtime/active/` (no es el árbol que usa `run.py`) — terminó
   en compilación correcta (10 `CL.exe`, todos sobre archivos legítimos
   de `ps2xRuntime/src/lib/...`, ninguno de `generated/`, por lo tanto
   no peligroso), pero no se usó su resultado para ninguna validación;
   se descartó y se repitió sobre el árbol correcto. Documentado por
   transparencia, no representa una corrida contaminada.

## 9. Ejecutables resultantes

- Primer build P3.14 (usado en RUN_028–031): SHA256
  `87d28e63d1ded918ee96cc231910616e29eb195a56ad7714ae2d4451a85435e5`.
- Segundo build P3.14 (añade solo el campo diagnóstico `viBufCapacity`,
  usado en RUN_032): SHA256
  `313cb9f2c93d1851848cd7aa1dcd5d6e8b93ef421292358261e33004f23fd28b`.
  Diferencia de comportamiento entre ambos: **ninguna** — RUN_032
  reproduce exactamente la secuencia `consumedBefore` y el conteo de
  éxitos de RUN_029–031.

## 10. Run 0 — regresión con P3.14 deshabilitado

`RUN_028`, `DMC_P314_SYNC_REDECODE` ausente: `[P313:init] mode=baseline`
(nota: no `ownership`, porque sin la variable P3.14 el modo efectivo es
el que decida `DMC_P313_INIT_MODE`, ausente también ⇒ `baseline` por
defecto), `0` líneas `P314` en el log, `0` éxitos de `getPicture`,
`result=SUCCESS` (el harness, no crash) — reproduce el stall histórico
sin ningún cambio de comportamiento cuando la variable está apagada.
Confirma que el mecanismo es estrictamente opt-in.

## 11. Corridas con P3.14 habilitado — resultados por corrida

4 corridas totales (se exigían 3; RUN_032 es una cuarta confirmación
dirigida específicamente a cerrar la duda de la sección 3.1):

| Campo | RUN_029 | RUN_030 | RUN_031 | RUN_032 |
|---|---|---|---|---|
| exe SHA256 (prefijo) | `87d28e63…` | `87d28e63…` | `87d28e63…` | `313cb9f2…` |
| `[P313:init] mode` | ownership | ownership | ownership | ownership |
| `framesAfter`/`decoderAfter` en init | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| éxitos `getPicture` | 10 | 10 | 10 | 10 |
| secuencia `consumedBefore` | idéntica (ver abajo) | idéntica | idéntica | idéntica |
| `decoderFresh=1` | solo llamada #1 | solo #1 | solo #1 | solo #1 |
| `GP:H` (waitExternal) | 0 | 0 | 0 | 0 |
| `videoBytesTotal` final | 517313 | 517313 | 517313 | 517313 |
| `viBufCapacity` | no medido | no medido | no medido | **524288** |
| `async callback stack exhausted` | 0 | 0 | 0 | 0 |
| `result.json: result` | SUCCESS | SUCCESS | SUCCESS | SUCCESS |
| imagen de película reconocible (visual) | NO (pantalla de warning estática) | NO (ídem) | NO (ídem) | no capturado (corrida corta, sin `--visual-trace`; ya confirmado NO en 3/3 anteriores) |

Secuencia `consumedBefore` (idéntica byte-a-byte en las 4 corridas):
`0, 36864, 94208, 147456, 200704, 249856, 303104, 352256, 405504,
462848, 516096`. La llamada #11 (no contada como éxito) ve
`available=1217`, insuficiente para otra trama, y correctamente NO
produce salida (`framesAfter=0`, sin fabricar éxito).

## 12. Prueba de que no se consumió ninguna trama/decoder preservado

- `frames=0` en el log `[P314:GP] #N pre-sync` en **todas** las 40
  entradas observadas (10 por corrida × 4 corridas) — nunca hay una
  trama previa disponible al entrar al bloque nuevo.
- `decoderFresh=1` ocurre **exactamente una vez** por corrida (la
  llamada #1) — confirma que el decoder se crea de cero después del
  reset y se reutiliza (no se recrea ni se repite) para las llamadas
  #2–#10 de la misma corrida, nunca proviene de antes del init.
- `consumedBefore` es estrictamente monótono creciente y arranca en 0
  en cada corrida — no hay relectura ni "rebobinado" de bytes ya
  consumidos.

## 13. Comparación P3.13 FULL vs P3.14 SYNC

| Métrica | P3.13 `full` | P3.14 `sync` |
|---|---|---|
| Decoder pre-init preservado | SÍ | NO |
| Tramas pre-init preservadas | SÍ (4) | NO (0) |
| Decoder fresco post-init | NO (reutiliza el viejo) | SÍ |
| Primer GetPicture retorna síncrono | SÍ, pero repitiendo salida vieja | SÍ, decodificando ES nuevo |
| Tramas post-init genuinamente nuevas | 6 | 10 |
| Total éxitos GetPicture observados | 10 (4 viejas + 6 nuevas) | 10 (0 viejas + 10 nuevas) |
| Progresa más allá del techo de 4 | SÍ | SÍ |
| CD/PSS demux continúa (`videoBytesTotal` crece) | SÍ | SÍ |
| UI reconocible renderizada | SÍ (pantalla de warning) | SÍ (misma pantalla) |
| Imagen de película/CAPCOM reconocible | NO | NO |
| Agotamiento `RuntimeGuestArena` | SÍ, 3/3 | NO, 0/4 (ventana de observación de ~100 s; no reclamado como resuelto) |
| Dependencia de objeto pre-init para sostener progreso | SÍ (decoder vivo) | NO |

## 14. Contrato síncrono representado

El original expone `sceMpegGetPicture` como una syscall que retorna
inmediatamente con éxito o fallo (P3.8.1/P3.9, HECHO ya establecido)
mientras el IPU decodifica de forma asíncrona vía DMA en paralelo al
resto del juego. P3.14 reproduce el contrato **observable por el
llamador** (retorno síncrono con una trama real) pero mediante un
mecanismo host completamente distinto: decodifica de forma síncrona,
dentro de la propia llamada a la syscall, en el hilo host que atiende
la invocación — no hay IPU asíncrono ni DMA real involucrados. Se
declara explícitamente como equivalencia de contrato observable, **no**
como réplica del mecanismo/temporización original.

## 15. Resultado visual

Inspección directa (Read de imágenes) de capturas al final de
RUN_029/030/031 (`--visual-trace`): la pantalla es, en las tres,
idéntica a la advertencia estática de P3.13.1 (página 1: inglés +
alemán + francés) — sin cambios respecto al estado ya documentado. **No
se observó ninguna imagen de película, logo CAPCOM ni fotograma
reconocible como contenido de vídeo decodificado** en ninguna corrida.

## 16. Agotamiento de `RuntimeGuestArena`

`0/4` corridas de este prompt lo alcanzaron dentro de su ventana de
observación (~80-100 s cada una). A diferencia de P3.13 `full` (3/3) y
P3.13.1 (5/5), que sí lo alcanzaban consistentemente. **No se afirma
que P3.14 resuelva este límite** — solo que no se observó dentro de
esta ventana; podría aparecer con ventanas más largas. UNKNOWN,
explícitamente no investigado en este prompt.

## 17. LAST COMMON NODE / FIRST DIVERGENT NODE

Sin cambios respecto a P3.13 en la comparación RECOMP-vs-ORIGINAL: el
**LAST COMMON NODE** sigue siendo el estado post-reset de
`sceMpegInit` bajo el modo `ownership` (config/callbacks/identidad de
sesión sobreviven; decoder/salida no) — la mejor aproximación actual al
comportamiento real de hardware dentro del segmento auditado.

El **FIRST DIVERGENT NODE** tampoco cambia de naturaleza, pero P3.14
reemplaza el mecanismo concreto usado dentro de él: en vez de "IPU real
decodificando asíncronamente vía DMA mientras el resto del juego
continúa", RECOMP ahora usa "decode síncrono bajo demanda, íntegramente
dentro del handler de la syscall `sceMpegGetPicture`, alimentado desde
un cursor host-only sobre el `viBuf` guest". Sigue siendo, como en
P3.13, una construcción exclusiva del lado HLE sin equivalente directo
en hardware original — el punto de divergencia no se cierra, se
reimplementa de una forma que produce el contrato observable correcto.

## 18. Clasificación

**`SUSTAINED_SYNC_REDECODE`** (Nivel 2 de la escala del prompt):
decoder fresco, cero dependencia de tramas/decoder pre-init, primer
GetPicture síncrono, progreso sostenido más allá de 4 tramas (10, en
las 4 corridas), reproducido 4/4. **No** alcanza
`SUSTAINED_SYNC_REDECODE_WITH_VISIBLE_MOVIE` (Nivel 3) — no se observó
ninguna imagen de película reconocible.

## 19. CHECKPOINT DECISION

Verificación explícita de los 9 criterios de la puerta condicional del
prompt:

1. No preserva el decoder vivo pre-init — CONFIRMADO (`decoderAfter=0`
   en `[P313:init]`; `decoderFresh=1` solo en la llamada #1 de cada
   corrida).
2. No depende de las 4 tramas predecodificadas — CONFIRMADO
   (`framesAfter=0` en init; `frames=0` en las 40 entradas `[P314:GP]`
   observadas).
3. Reconstruye entrada usable desde estado guest sobreviviente —
   CONFIRMADO, con la limitación de wraparound explícita de la sección
   3.1 cerrada dinámicamente (capacidad medida = 524288 bytes, consumo
   máximo observado = 517313 bytes, sin cruce de límite en esta ventana).
4. El primer GetPicture retorna síncrono — CONFIRMADO (`GP:H`=0 en las
   4 corridas; el bloque nuevo llena `decodedFrames` antes de que se
   evalúe el camino `waitExternal` existente).
5. Produce tramas nuevas post-init — CONFIRMADO (10 tramas nuevas por
   corrida, cero reproducidas).
6. Progresa más allá del puente de 4 tramas de P3.13 `queued` —
   CONFIRMADO (10 > 4 en las 4 corridas).
7. Reproducido en 3 corridas controladas — CONFIRMADO y superado (4/4).
8. Sin cambios al scheduler — CONFIRMADO (`git diff --stat`: solo
   `MPEG.cpp`).
9. Sin éxito/tramas fabricadas — CONFIRMADO (llamada #11 en las 4
   corridas correctamente reporta `framesAfter=0` cuando el ES
   disponible es insuficiente; la única fuente de tramas es el retorno
   real de `decoder->feed()`).

**Los 9 criterios se cumplen.**

**CHECKPOINT_DECISION: COMMIT_RECOMMENDED.**

Justificación: el mecanismo cumple estrictamente la puerta condicional
del prompt, fue reproducido 4/4 (no solo 3/3), y la duda de
correctitud más seria que surgió durante esta misma iteración
(wraparound del ring buffer, sección 3.1) se investigó y cerró con
evidencia dinámica antes de proponer el commit, en vez de dejarse como
supuesto implícito. Se recomienda como commit de tipo "avance validado,
opt-in, no destructivo" — igual que `BLOCKER_004_runtime_p37_p312.patch`
y `BLOCKER_004_p313_scempeginit_experiment.patch` — **no** como cierre
de BLOCKER_004: sigue sin observarse ninguna imagen de película, el
mecanismo de consumo síncrono no tiene equivalente en hardware original
(sección 14/17), y la limitación de wraparound de la sección 3.1 sigue
sin corregirse (solo verificada como no-alcanzada en la ventana actual).

### Commit propuesto (NO ejecutado en este prompt)

Título: `experiment: synchronous guest-ES redecode for sceMpegGetPicture (P3.14)`

Cuerpo (borrador):

```
Adds an opt-in (DMC_P314_SYNC_REDECODE=1) synchronous decode path to
sceMpegGetPicture: when the decoded-output queue is empty after
sceMpegInit's ownership-preserving reset, feed a fresh FFmpeg decoder
directly from the guest viBuf's logically-pending video ES (tracked via
a host-only cursor, never mirrored to guest RAM) in bounded 4 KiB
chunks, before falling back to the existing waitExternal path.

Does not preserve any pre-init decoder or decoded-frame state (unlike
the P3.13 `full` experimental mode). Validated 4/4: 10 synchronous
GetPicture successes per run, zero dependency on pre-init state, no
fabricated frames, no scheduler changes, and -- within the observed
~100s window -- no RuntimeGuestArena async callback stack exhaustion
(unlike P3.13 full, which hit it 3/3).

Known limitation, not fixed here: the host-side consumption cursor is
linear (viBufBase + consumedBytes), not modulo the guest ring buffer's
real capacity (confirmed 512 KiB via movieState+0x20). No observed run
in this validation exceeded that capacity, but a longer session could.

Still does not produce any visible decoded movie/CAPCOM imagery --
BLOCKER_004 remains open. See
analysis/notes/BLOCKER_004_P314_SYNCHRONOUS_GUEST_ES_REDECODE.md for
full validation evidence.
```

## 20. Próximo prompt recomendado

`P3.14.1 — FABLE_SYNCHRONOUS_MPEG_CONTRACT_AUDIT` (auditoría adversarial
independiente de este mecanismo, en particular de la sección 3.1) antes
de avanzar a por qué no aparece imagen de película pese al progreso de
decode — o, alternativamente, `P3.15 —
RUNTIME_GUEST_ARENA_INVOCATION_STACK_LIFETIME` si se prioriza cerrar el
límite de recursos observado en P3.13 (aunque P3.14 no lo alcanzó en
esta ventana, sigue sin explicarse por qué P3.13 `full` sí lo hacía).

## 21. Estado final de git

- Vendor (`vendor/PS2Recomp`): dirty, único archivo modificado
  `ps2xRuntime/src/lib/Kernel/Stubs/MPEG.cpp` (+125/-1), sin commit.
- Repo principal: limpio (`git status --short` sin salida) salvo este
  informe nuevo (sin trackear todavía) y la sección añadida al apéndice
  canónico (`BLOCKER_004_pss_video_output.md`).
- **No se ejecutó ningún commit ni push en este prompt**, conforme a la
  instrucción explícita. El commit propuesto en la sección 19 queda
  pendiente de autorización explícita en un prompt de checkpoint
  posterior.

## 22. Confirmaciones de seguridad explícitas

- No se modificó la semántica del scheduler (`EeScheduler.cpp` intacto).
- No se modificó el camino de éxito existente de `waitExternal`/
  `GetPicture` — el bloque nuevo se inserta antes y solo actúa cuando la
  cola de tramas está vacía.
- No se fabricó ningún frame ni éxito sintético en ningún punto.
- No se reutilizó ningún backup de librería de P3.11/P3.13.
- No se usó PCSX2 en este prompt.
- No se recompiló ningún archivo de `recomp/generated/` en ningún build
  de este prompt (verificado por conteo de `CL.exe` en cada log).
- No se mató ningún proceso ajeno a `dmc-recomp.exe` de esta sesión.
