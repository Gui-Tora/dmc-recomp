# P3.12 — SCHEDULER_BATCH_INVOCATION_ORDER_FIX_AND_REVERIFY

Investigación + implementación acotada. Continúa directamente P3.11
(Astra) y P3.11.1 (Fable, auditoría adversarial). No reabre heap,
CDMODULE ni BLOCKER_003. `sceMpegInit` queda explícitamente sin tocar
en este prompt (causal isolation): el objetivo es aislar el efecto de
UN SOLO fix (orden de drenaje de invocaciones por lote del scheduler)
y reobservar BLOCKER_004 con `sceMpegInit` HLE sin cambios.

## 1. Checkpoint de entrada

- `git status --short` (repo principal) al inicio, idéntico a los
  informes P3.11/P3.11.1 (ningún cambio propio previo a este prompt):
  ```
   M analysis/notes/BLOCKER_004_pss_video_output.md
   M runtime/dmc_overrides.cpp
   M upstream.lock.json
  ?? analysis/notes/BLOCKER_004_ASTRA_AUDIT.md
  ?? analysis/notes/BLOCKER_004_ASTRA_P311_OVERNIGHT.md
  ?? analysis/notes/BLOCKER_004_FABLE_P3111_CAUSAL_AUDIT.md
  ?? analysis/notes/BLOCKER_004_P36_HEAP_COLLISION_AUDIT.md
  ?? analysis/tools/
  ?? patches/BUILD_ffmpeg_private_link_scope.patch
  ?? runtime/p311_pad_test.inc
  ```
- HEAD principal: `888f22ef7a3dc685815e9995386100a91fd94c32` (sin cambios).
- Vendor HEAD: `0f76399b1aa35b018a89477c078924775d35b79c` (sin cambios).
- Vendor dirty previo (idéntico a P3.11.1): `ps2xRuntime/include/ps2_runtime.h`,
  `EeScheduler.cpp`, `Stubs/Audio.cpp`, `Stubs/MPEG.cpp`,
  `Syscalls/System.cpp`, `ps2_runtime.cpp`.
- Ejecutable inicial (antes de esta iteración): SHA256
  `79c7b25e0392945bcdb1270b997cff83931b193ed38d9a74d234c877aa335d03`,
  347604480 bytes, coincide exactamente con el registrado en P3.11.
- ELF verificado por lectura binaria propia: `original/SLES_503.58`,
  SHA256 `d0753a6b3b2f00802a50758a872d8cf051725aa31c839aa8894eee30ce58bab4`
  (mismo valor citado en el prompt; el CRC32 `77654AD2` coincide con lo
  ya verificado en P3.11).

## 2. Reverificación independiente del diagnóstico de Fable

Releído `BLOCKER_004_ASTRA_P311_OVERNIGHT.md` y
`BLOCKER_004_FABLE_P3111_CAUSAL_AUDIT.md` completos, más las secciones
P3.8–P3.11 pertinentes de la nota canónica. Verificación propia,
byte/línea, sobre el código FUENTE actual (no sobre los informes):

- `vendor/PS2Recomp/ps2xRuntime/include/runtime/ee_scheduler.h:124-134`:
  `GuestThread::invocations` es `std::vector<GuestInvocation>`;
  `activeContext()` devuelve `invocations.back().context` cuando no
  está vacío. **HECHO, confirmado.**
- `vendor/PS2Recomp/ps2xRuntime/include/runtime/ee_scheduler.h:434`:
  `m_pendingInvocations` es `std::deque<GuestInvocation>` (confirma que
  el contenedor soporta ambos extremos sin cambio de tipo).
- `EeScheduler.cpp` (antes del fix), dispatcher principal (`run()`):
  - Rama "adquirir hilo de invocación sintético" (antes en 184-186):
    `m_pendingInvocations.front()` + `pop_front()`.
  - Rama de drenaje por-iteración (antes en 256-266, dentro del bucle
    principal, evaluada en CADA iteración antes de ejecutar cualquier
    función guest en la línea ~298): `m_pendingInvocations.front()` +
    `pop_front()`, `push_back` a `running->invocations`, `continue`.
    Mientras `m_pendingInvocations` no esté vacío, el bucle NUNCA llega
    a ejecutar `function(...)`: drena un ítem por iteración hasta
    vaciar la cola pendiente. **HECHO: confirma la afirmación de Fable
    "drena TODO el lote pendiente antes de ejecutar nada"** — con la
    precisión de que el drenaje es incremental (un `continue` por
    ítem), no un volcado de una sola vez, pero el efecto neto es
    idéntico: cero ejecución de guest code hasta vaciar la cola.
  - Finalización de invocación (antes en 237-238, sin cambios en este
    fix): `running->invocations.back()` + `pop_back()`.
- **Prueba directa de (B) del prompt**: con `pending=[A,B,C]` (A
  encolado primero vía `push_back` en `queueInvocation`), el drenaje
  antiguo consume `front()` repetidamente (A, luego B, luego C) y hace
  `push_back` a la pila cada vez → pila final `[A,B,C]`con C en
  `.back()`. La ejecución consume por `pop_back()`: primero C, luego B,
  luego A. **Orden de ejecución = inverso del orden de encolado.
  CONFIRMADO, no hipótesis** (ver también el test aislado, sección 8).
- **Confirmación de (C) — por qué `invokeCurrentSequence` inserta en
  reversa**: `EeScheduler.cpp:1122-1139` (línea exacta reverificada
  tras el fix, sin desplazamiento significativo) itera
  `invocations.rbegin()` → `rend()` sobre su parámetro `std::vector`
  (una secuencia YA ordenada, ítem 0 = "debe ejecutar primero"),
  haciendo `push_back` en ese orden inverso. Esto dejaría el ítem 0
  como el ÚLTIMO empujado, es decir, en `.back()` — exactamente lo que
  `pop_back()`/`activeContext()` recuperan primero. **Es la prueba
  directa, en el propio código, de que "empujar en reversa para que
  `pop_back()` recupere en orden directo" es el patrón ya establecido
  y deliberado para este mismo contrato de pila** — no una invención
  de esta iteración. `invokeCurrentSequence` NO usa
  `m_pendingInvocations` en ningún punto: opera directo sobre
  `owner->invocations`. Confirmado: disjunto del código tocado por
  este fix.
- `queueInvocation` (`EeScheduler.cpp:1099-1105`, sin cambios):
  `invocation.sequence = ++m_invocationSequence;` seguido de
  `m_pendingInvocations.push_back(...)`. Confirma encolado FIFO puro en
  el productor, con un contador monótono global asignado en el momento
  exacto de encolado (no usado por el fix, pero confirma que existe un
  orden temporal total y bien definido para reconstruir/verificar).

**Veredicto de la reverificación: el diagnóstico de Fable (P3.11.1,
§1) es CORRECTO en su mecanismo y sus citas de código, con la única
precisión añadida de que el drenaje es incremental (un `continue` por
ítem) en vez de un volcado atómico — sin cambiar la conclusión.**

## 3. Auditoría de TODOS los productores de `queueInvocation` (código actual, no solo MPEG)

`grep` exhaustivo de `queueInvocation(` en todo `vendor/PS2Recomp`:
exactamente 5 resultados — la definición (`ee_scheduler.h:313` y
`EeScheduler.cpp:1099`) más 4 call sites reales:

| Call site | Semántica esperada | ¿Batch posible? | Efecto bajo LIFO (antes) | Efecto tras el fix |
|---|---|---|---|---|
| `MPEG.cpp` (`dispatchGuestStreamCallback`, invocado desde `dispatchStreamCallbacksUnlocked` iterando `callbackEvents` en orden de parseo) | Orden de parseo PES (vídeo y audio intercalados según aparecen en el stream) | SÍ — hasta docenas por llamada de demux (P3.11.1 documentó lotes de 12-16) | Invertido por lote — CONFIRMADO byte-exacto por Fable (§1 de su auditoría) | Orden de parseo restaurado (ver §6-7, validación dinámica) |
| `EeScheduler.cpp::dispatchIrq` | Orden ascendente de `handler.order` para la misma causa IRQ (la lista se ordena explícitamente con `std::sort` antes del bucle que encola) | SÍ, si >1 handler comparte la misma causa | Invertido | Orden `order` restaurado |
| `EeScheduler.cpp::processEvent` caso `VBlankStart` (encola el callback GS-VSync, luego llama `dispatchIrq(false, 2u)`) | El callback GS-VSync debe encolarse y por tanto ejecutar antes que los handlers de la causa IRQ 2 que `dispatchIrq` encola a continuación, dentro de la misma ventana de drenaje | SÍ — se combinan en el mismo lote pendiente si nada drena entre medias | Orden relativo GS-callback vs. handlers de causa=2 podía invertirse | Orden temporal real (GS callback antes que los handlers de causa=2) restaurado |
| `EeScheduler.cpp::processEvent` caso `Alarm` | Orden cronológico de disparo de alarmas | SÍ si >1 evento (temporizador o alarma) queda pendiente dentro de la misma llamada a `processPendingEvents()` — confirmado que esa función SÍ puede procesar múltiples eventos encolados en una sola pasada (`EeScheduler.cpp`, bucle `for (const EeEvent &event : pending) processEvent(event);`) antes de que el dispatcher principal vuelva a comprobar `m_pendingInvocations` | Invertido si >1 | Orden cronológico correcto |

Mecanismos relacionados pero **no afectados** por este fix (no usan
`m_pendingInvocations`):

| Mecanismo | Por qué no lo toca el fix |
|---|---|
| `invokeCurrent` (`EeScheduler.cpp:1107-1120`) | Un solo ítem, `push_back` directo a `owner->invocations`, lanza `EeDispatcherTransfer` de inmediato. Nunca pasa por `m_pendingInvocations`. |
| `invokeCurrentSequence` (`EeScheduler.cpp:1122-1139`) | Ya inserta en reversa (`rbegin`→`rend`) directamente sobre `owner->invocations`. Disjunto de `m_pendingInvocations`; el fix no puede doble-invertirlo porque no comparten código. |

**No se asumió que MPEG fuera el único productor** — la auditoría es
exhaustiva sobre el árbol vendor actual, no una muestra.

## 4. Pregunta semántica crítica: ¿qué es un "lote"?

`GuestInvocation` (`ee_scheduler.h:95-102`) NO tiene ningún campo de
hilo-objetivo — solo `kind`, `sequence`, `tag`, `context`,
`onComplete`. El dispatcher principal (`run()`) no enruta invocaciones
pendientes a un hilo distinto de `running` (el hilo/contexto actual);
TODO lo que esté en `m_pendingInvocations` en el momento del drenaje se
empuja, sin excepción, sobre la pila del MISMO `running` (o del hilo de
invocación sintético recién adquirido si no hay ninguno listo).
`processPendingEvents()` confirma que eventos de MÚLTIPLES productores
lógicos (varios timers, una alarma, un VBlank) pueden acumularse en
`m_pendingInvocations` dentro de una sola pasada antes de que el
dispatcher vuelva a drenar.

**Conclusión (HECHO, por inspección de código, no suposición):** dado
que (a) no existe enrutamiento por hilo-objetivo en este mecanismo y
(b) todo lo pendiente en un momento dado termina, sin excepción, en la
pila de UN ÚNICO hilo `running`, preservar el orden FIFO verdadero de
encolado de TODO `m_pendingInvocations` en cada drenaje es correcto de
forma genérica — no hace falta lógica de agrupación por productor ni
por hilo. Una reversión global del contenido pendiente en cada drenaje
NO puede reordenar incorrectamente productores no relacionados entre
sí, porque de todas formas comparten el mismo destino (una sola pila,
un solo hilo) y el orden temporal real de `queueInvocation()` (mismo
intérprete de un solo hilo, sin concurrencia real entre encolados) es
siempre la intención semántica correcta.

## 5. Diseño del fix mínimo

Cambiar el extremo de consumo de `m_pendingInvocations` de `front()`/
`pop_front()` a `back()`/`pop_back()`, en los DOS puntos donde se
drena (rama de adquisición de hilo de invocación sintético, y rama de
drenaje por-iteración del bucle principal). `queueInvocation` sigue
usando `push_back()` sin cambios (el encolado FIFO en el productor no
se toca). Con esto, el ítem encolado MÁS ANTIGUO de un lote pendiente
es el que se drena y se empuja ÚLTIMO sobre `running->invocations`,
quedando en `.back()` — que es precisamente lo que `activeContext()` y
la finalización (`pop_back()`) consumen PRIMERO. Efecto neto: orden de
ejecución = orden real de encolado, sin tocar `activeContext()`,
`pop_back()` de `running->invocations`, ni convertir la arquitectura de
pila en cola.

Por qué preserva la semántica de anidamiento: `invokeCurrent`/
`invokeCurrentSequence` (los mecanismos que producen anidamiento real,
p.ej. una invocación que dispara otra antes de completarse) operan
EXCLUSIVAMENTE sobre `owner->invocations` de forma directa, sin pasar
por `m_pendingInvocations` en ningún punto — el fix no puede alterar su
comportamiento porque no comparten código ni estado con las dos líneas
modificadas. La finalización de invocación (`context.pc==0` →
`invocations.back()` + `pop_back()`, `EeScheduler.cpp` antes en
237-238) tampoco se tocó.

## 6. Archivos cambiados

Un archivo, dos sitios, ambos en el mismo bucle `EeScheduler::run()`:

- `vendor/PS2Recomp/ps2xRuntime/src/lib/Kernel/EeScheduler.cpp`
  - Rama de adquisición de hilo de invocación sintético (dentro de
    `if (m_currentThreadId == 0) { ... GuestThread *owner =
    &acquireInvocationThread(); ... }`): `front()`/`pop_front()` →
    `back()`/`pop_back()`, con comentario explicativo.
  - Rama de drenaje por-iteración (`if (!m_pendingInvocations.empty())
    { ... continue; }`, evaluada cada iteración del bucle principal
    antes de ejecutar cualquier función guest): mismo cambio, con
    comentario que referencia el primero para dejar explícito que
    ambos deben mantenerse consistentes.

Ningún otro archivo fuente modificado en este prompt. `MPEG.cpp` queda
exactamente como lo dejó P3.11 (instrumentación `[MPEG:init]`, R0-R8,
dumps opt-in) — **sin tocar `sceMpegInit`, `sceMpegGetPicture`,
`waitExternal`, ni ningún comportamiento MPEG**, por instrucción
explícita del prompt.

## 7. Método de build/test (targeted, sin build masivo)

1. `ps2_runtime.vcxproj` (destino por defecto de MSBuild — confirmado
   seguro repetidas veces en P3.7-P3.11, distinto del peligroso destino
   por defecto de `ps2EntryRunner.vcxproj`): compiló únicamente
   `EeScheduler.cpp` (verificado por la línea `CL.exe ... EeScheduler.cpp`
   en el log), 0 advertencias, 0 errores, 5.67 s.
2. `ps2EntryRunner.vcxproj` con `/t:_BuildLinkAction` exclusivamente:
   relink, 0 invocaciones de `CL.exe` (verificado con `grep -c` sobre el
   log completo de link — cero recompilación de C++ generado), 0
   errores, ~2m59s (tiempo de linkeo puro, coherente con P3.11/P3.11.1).
3. Ningún archivo `generated/` tocado ni recompilado en ningún momento.
   No hubo clean, no hubo regeneración, no hubo build completo.

Ejecutable resultante: SHA256
`a5b0a4c78491bd605cb30eb7471167ed2fbd728e6179072fdb3d54c3ce7f9086`,
347603456 bytes (difiere del baseline P3.11 `79c7b25e...`/347604480
bytes, como se espera de un cambio de código real).

## 8. Test del scheduler en aislamiento

No existe destino CMake para `ps2xTest` en el árbol de build activo
(`analysis/local/symtabfirst/build`) — confirmado por búsqueda (`find
... -iname "*ps2xTest*.vcxproj"`, sin resultados) y coincide con lo ya
documentado por Astra en P3.11 ("No había ejecutable de tests en el
build activo; no se construyó una suite grande"). Añadir ese destino
requeriría reconfigurar CMake, con riesgo real y no acotado de disparar
una compilación mucho más amplia — exactamente lo que este prompt (y
toda la disciplina de build de la sesión) prohíbe explícitamente. Por
tanto, siguiendo la salida explícita que el propio prompt prevé ("If no
safe small test target is available, document why and use a tiny
targeted test mechanism"), se optó por un mecanismo de test acotado y
autocontenido en vez de forzar el target de test existente.

`p312_scheduler_order_test.cpp` (mantenido fuera del repositorio, en el
directorio de trabajo temporal de la sesión, no en `<repo>`):
reproduce EXACTAMENTE la semántica de los contenedores tocados por el
fix (`std::deque` para el equivalente de `m_pendingInvocations`,
`std::vector` usado como pila con `push_back`/`pop_back`/`.back()` para
el equivalente de `GuestThread::invocations`) y el mismo algoritmo de
drenaje, tanto en su forma ANTERIOR (`front()`/`pop_front()`) como
POSTERIOR (`back()`/`pop_back()`) al fix — sin enlazar contra
`ps2_runtime.lib` ni el resto del árbol (cero riesgo de build masivo).
Compilado con `cl.exe` de forma completamente aislada (sin relación con
el árbol CMake del proyecto). Resultados:

```
[OK] pre-fix mock reproduces the reported reversal: C,B,A
[OK] post-fix mock yields FIFO order: A,B,C
[OK] cross-producer batch preserves true enqueue order
[NOTE] invokeCurrent/invokeCurrentSequence do not touch m_pendingInvocations; unaffected by construction (source-verified, not re-tested here).
ALL CHECKS PASSED
```

Cobertura de los 4 sub-objetivos pedidos:

1. **Tres callbacks A,B,C para un hilo → ejecución A,B,C**: cubierto y
   PASA (ver arriba).
2. **Comportamiento anidado sigue usando semántica de pila
   correctamente**: NO re-testeado dinámicamente — verificado por
   INSPECCIÓN DE CÓDIGO (sección 5): `invokeCurrent`/
   `invokeCurrentSequence` son estructuralmente inalcanzables por este
   fix porque no comparten `m_pendingInvocations`. No hay estado
   compartido que este mock pudiera ejercer de forma no trivial más
   allá de lo ya establecido por lectura de fuente.
3. **Callbacks para hilos distintos no se agrupan/invierten
   incorrectamente**: cubierto parcialmente por el caso "cross-producer
   batch" (dos productores lógicos distintos dentro del mismo lote
   pendiente preservan orden real de encolado) más el análisis de la
   sección 4 (no existe enrutamiento por hilo-objetivo en el mecanismo
   real; todo lo pendiente en un momento dado comparte destino, así que
   no hay escenario real de "hilos distintos" dentro de un mismo
   drenaje que este mecanismo pueda mezclar incorrectamente).
4. **`invokeCurrentSequence` no queda doble-invertida**: verificado por
   inspección de código, no por test dinámico — opera sobre
   `owner->invocations` directamente, nunca sobre
   `m_pendingInvocations`; el fix no toca ninguna línea de
   `invokeCurrentSequence`.

La validación dinámica primaria (sección 9, corrida real del juego con
comparación byte-exacta de `viBuf`) es la evidencia más fuerte y
directamente relevante para BLOCKER_004: ejercita el código REAL
(`EeScheduler::run()` completo, con MPEG real como productor) de
principio a fin, no una reproducción aislada del algoritmo.

## 9. Validación dinámica primaria: 3/3 corridas reales

Comando exacto (infraestructura P3.11 reutilizada sin cambios):

```powershell
python analysis/tools/runtime_loop/run.py --runs 3 --seconds 180 --confirm --keys ENTER,X --method runtime --diagnostics --post-target 20
```

Sin proceso `dmc-recomp.exe` previo ni `loop.lock` residual antes de
lanzar. Resultado: **RUN_012, RUN_013, RUN_014**, las tres `SUCCESS`,
mismo `exe_sha256` (`a5b0a4c7...`, el ejecutable con el fix), mismo
entorno (`PS2X_CD_IMAGE`/`PS2X_RUNTIME_ARENA_BASE`/`_LIMIT` exactos del
prompt), `exit_code=1`/`exit_before_stop=None` en las tres (parada
deliberada del harness, no crash — mismo patrón que P3.11).

| Run | MOVIE_START | PSS_REACHED | MPEG_INIT | TARGET (`[GP:H]`) |
|---|---:|---:|---:|---:|
| RUN_012 | 68.890 s | 68.890 s | 69.422 s | 69.422 s |
| RUN_013 | 67.906 s | 68.156 s | 68.421 s | 68.421 s |
| RUN_014 | 68.172 s | 68.437 s | 68.703 s | 68.703 s |

Timings consistentes con el baseline P3.11 (68.9-70.1 s), sin
degradación ni cambio de comportamiento de arranque/menú/input.

## 10. Comparación byte-exacta `viBuf` vs. `demuxed_video_expected.es` (por corrida)

`analyze_ram.py` + `probe_buffers.py` (sin cambios respecto a P3.11)
más un cálculo propio de SHA256 exclusivamente sobre el slice
`video_es_preinit.bin[0:228084]` para reportar explícitamente el SHA
del lado guest (la herramienta existente solo compara byte a byte y
reporta la primera diferencia, no imprime el SHA del guest por
separado):

| Run | packet_count | es_bytes (esperado) | expected_sha256 | guest_slice_sha256 (bytes 0-228084) | first_differing_byte | ¿Exacto? |
|---|---:|---:|---|---|---|---|
| RUN_012 | 56 | 228084 | `127e3b62…b82c` | `127e3b62…b82c` | `None` | **SÍ** |
| RUN_013 | 56 | 228084 | `127e3b62…b82c` | `127e3b62…b82c` | `None` | **SÍ** |
| RUN_014 | 56 | 228084 | `127e3b62…b82c` | `127e3b62…b82c` | `None` | **SÍ** |

(SHA completo en las tres: `127e3b621ba1d33b1ee9584681fb78431befaf13cbd4972fc0b2de1cb132b82c`
— literalmente el mismo valor que P3.11 ya había calculado para
`demuxed_video_expected.es`, confirmando que el multiconjunto de bytes
siempre fue correcto — sección 6 de P3.11.1 — y que ahora también lo es
su DISPOSICIÓN.)

`video_es_preinit.bin[0:64]` en las tres corridas, guest y esperado,
idénticos byte a byte: `00 00 01 b3 20 01 c0 13 17 31 a3 81 10 11 11
12 21 21 21 31 31 31 31 41 41 41 41 41 51 51 51 51 51 51 61 61 61 61
61 61 71 71 71 71 71 71 71 81 81 81 91 81 81 81 91 a1 a1 a1 91 b1 b1
...` — el buffer guest empieza DIRECTAMENTE con el start code de
secuencia MPEG-2 `00 00 01 B3` en el offset 0, no en el offset 61084
como antes del fix. Los 33554432 bytes de las dos capturas RAM
(preinit/getpicture) por corrida se generaron con las mismas dos
variables opt-in de P3.11 (`DMC_P311_RAM_PREINIT`/`_GETPICTURE`), sin
tocar esa instrumentación.

Región de vídeo (`0x1c`/`0x20`, capacidad completa 524288 bytes):
`same=True` entre preinit y getpicture en las tres corridas (igual que
P3.11 — el propio `sceMpegInit` no toca ese buffer). Región PSS
(`0x4`/`0x10`): SHA `de59ded2274119fcd74385aeb0283740a089d70f020e59fa8e210b31b0486ec4`
en las tres — **idéntico al valor exacto registrado por Astra en
RUN_008** (transporte CD/PSS no tocado por este fix, como se esperaba).

**Anomalía "228084 bytes con layout invertido" (P3.11 §, "Anomalía
adicional anterior al init"; P3.11.1 §1, "CONFIRMED CORRUPTION de
ORDEN"): estado tras el fix = FIXED**, sin reservas — coincidencia
exacta de tamaño, SHA y primer-byte-diferente=`None` en 3/3 corridas.

## 11. Conteo de frames FFmpeg pre-init y comportamiento `sceMpegInit`

`[P311:R3-R4]`/`[P311:R5-R6]` (feed real a FFmpeg, vía el camino HOST,
no el guest-visible): mismos 4 tamaños de paquete en el mismo orden que
P3.11 (`4074, 4077, 4077, 4048`) en las tres corridas — **sin cambio**.
Esto es consistente con lo ya establecido por Fable (P3.11.1 §6): el
feed a FFmpeg SIEMPRE recibió los bytes en el orden correcto de
parseo, porque ese camino no pasa por el `queueInvocation`/pila que
este fix corrige; el desorden solo afectaba la escritura GUEST-visible
en `viBuf` vía `StrM2vCallBack`.

`[MPEG:diag] frame decoded, total=1..4` en los mismos `feed call
#9/#23/#36/#49` que P3.10/P3.11, idéntico en las tres corridas.

`[MPEG:init] #1 trackedStates=1` / `about to wipe mpeg=0x87d8f8
frames=4 sawInput=1` — **exactamente igual que antes del fix**:
`sceMpegInit` sigue ejecutando `playbackByMpeg.clear()` sin condición,
sin ningún cambio de comportamiento (no se tocó `MPEG.cpp` en esta
iteración salvo lo ya heredado de P3.10/P3.11).

`[GP:A] #1 entry mpeg=0x87d8f8 decodedFrames=0 streamEnded=0
decoderFailed=0 cdStreamEofSeen=0 sawInput=0 picturesServed=0` seguido
de `[GP:H] #1 waitExternal(...) -- SUSPENDING, no frame available` —
**idéntico, byte a byte del mensaje, en las tres corridas y respecto a
P3.10/P3.11**. `[P311:R2] new playback key=0x0087D8F8` aparece dos
veces por corrida (una antes del reset, vía `sceMpegCreate`; otra
después, vía la recreación silenciosa en `sceMpegIsEnd`/`GetPicture`) —
mismo patrón exacto que P3.11.

**Conclusión de la sección: CASO 1 del prompt confirmado.** `viBuf`
queda corregido (byte-exacto) y, con `sceMpegInit` sin tocar, el
primer `GetPicture` sigue produciendo el mismo stall vacío idéntico —
las dos causas quedan separadas limpiamente, sin interferencia mutua
observable.

## 12. Último nodo común / primera divergencia (alcance redefinido)

Con el fix de orden aplicado, el segmento auditado (`MpegMovieDecode
ENTRY → MpegMovieDecodeInit → sceMpegInit@0x109CD0 → GetPicture#1 →
waitExternal`) ya NO tiene ninguna divergencia contractual demostrada
ANTES de `sceMpegInit`: el layout de `viBuf` es ahora byte-exacto
respecto al ES esperado, y el transporte PSS ya lo era.

- **Último nodo común ampliado (HECHO, nuevo respecto a P3.11.1)**:
  demux/callback → `viBuf` guest, en orden de parseo correcto,
  byte-exacto (3/3 corridas). El scheduler ya no es una fuente de
  divergencia demostrada en esta ventana.
- **Primera divergencia contractual vigente (HECHO, sin cambios
  respecto a P3.11)**: `sceMpegInit@0x109CD0` — el HLE de RECOMP
  ejecuta `playbackByMpeg.clear()` (destruye callbacks, configuración y
  ownership de entrada aceptada) donde el original solo resetea
  hardware IPU/DMA vía MMIO/stack, sin tocar RAM guest software (P3.11
  §"Contraste de estados"; re-confirmado aquí sin cambios, ya que
  `MPEG.cpp` no se tocó en este prompt).

## 13. Clasificación actual de BLOCKER_004

**Antes de P3.12**, el modelo dominante (P3.11.1 §9, Model 3) exigía
DOS defectos coexistentes: orden de entrega del demux (pre-init) +
over-reset de ownership en `sceMpegInit` (post-demux, pre-GetPicture).

**Después de P3.12**: el primero de los dos —**scheduler ordering**—
queda **CONFIRMADO y CORREGIDO** (sección 10-11), sin necesidad de
reinterpretación. El segundo —**sceMpegInit ownership**— permanece
exactamente como lo dejó P3.11.1: over-reset confirmado para
callbacks/config/input-ownership; equivalencia de
`decoder`/`decodedFrames` con cualquier estado que debiera sobrevivir
sigue **UNKNOWN**. BLOCKER_004 es ahora, con evidencia directa y no
ambigua: **`sceMpegInit` ownership**, en solitario dentro de la ventana
auditada — el componente "scheduler ordering" deja de ser una variable
de confusión.

## 14. Veredicto sobre la teoría del scheduler de P3.11.1

**CONFIRMADA**, sin matices ni reinterpretación preservadora de teoría.
`viBuf == demuxed_video_expected.es` byte-exacto en 3/3 corridas tras
el fix mínimo. El experimento era directamente falsable (si el stall o
el layout no hubiesen cambiado, o si `viBuf` siguiera divergiendo, la
teoría habría quedado RETRACTADA o PARCIAL) y sobrevivió sin
ambigüedad.

## 15. Recomendación para el próximo prompt

**sceMpegInit ownership** — diseñar e implementar un fix que separe la
inicialización de hardware IPU/DMA (que SÍ debe ejecutarse, coincide
con el original) de la destrucción de la sesión software MPEG
(callbacks, configuración, ownership de bytes ya aceptados), sin
inventar un bridge síncrono de `GetPicture` todavía y sin asumir que la
mera conservación de `decodedFrames`/`decoder` basta (P3.11 "Diseño
recomendado del futuro fix", sin cambios: sigue siendo la hoja de ruta
válida, ahora con el ruido de orden de scheduler eliminado del
experimento). **No implementado en este prompt**, por instrucción
explícita ("P3.12 ES aislar UNA variable; DO NOT fix sceMpegInit").

## 16. Disciplina de evidencia — correcciones vigentes

- **RETRACTADO** (histórico, sin cambios): teoría de bit `0x4000` en
  arranque.
- **RETRACTADO** (histórico, sin cambios): `state+0x70` = retorno de
  `CreateSema`.
- **RETRACTADO** (histórico, sin cambios; no confundir con el defecto
  de scheduler descubierto en P3.11.1/corregido en P3.12): la
  afirmación "el scheduler era la primera divergencia de P3.4" se
  refería a un problema DISTINTO y ANTERIOR (`guestMalloc` colapsado),
  ya resuelto en P3.7.1. El defecto de orden de lotes de invocaciones
  es un hallazgo nuevo e independiente de P3.11.1, no una reapertura de
  aquella afirmación.
- **RETRACTADO** (P3.11.1, sin cambios): "el original descarta
  deliberadamente los cuatro frames de FFmpeg".
- **RETRACTADO** (P3.11.1, sin cambios): "`_getpic` es enteramente
  software" — la cadena alcanza MMIO IPU real vía `_nextBit`.
- **NARROWED** (P3.11.1, sin cambios en este prompt): over-reset
  completo de `sceMpegInit` → over-reset confirmado de
  callbacks/config/ownership de entrada; equivalencia de
  decoder/frames permanece UNKNOWN.
- **CONFIRMADO (nuevo, P3.12)**: la inversión del orden de ejecución de
  lotes de `queueInvocation` (P3.11.1 §1, hipótesis) es ahora un HECHO
  verificado por implementación y prueba dinámica byte-exacta, no una
  inferencia. El fix es mínimo, genérico (no específico de MPEG/DMC) y
  no altera la semántica de pila ni el anidamiento de invocaciones.

## 17. Confirmaciones de seguridad

NO commit. NO push. NO clean. NO regenerate. NO build masivo de
`generated/` (verificado en cada build: 0 invocaciones de `CL.exe`
sobre archivos generados, solo `EeScheduler.cpp` y el relink vía
`_BuildLinkAction`). NO se tocó ningún archivo de configuración del
sistema ni se instaló ningún driver. NO se terminó ningún proceso ajeno
a esta investigación — el único `taskkill`/verificación de proceso
previo a cada build/corrida fue sobre instancias propias de
`dmc-recomp.exe` lanzadas por esta misma sesión (ninguna encontrada
antes de esta iteración; el harness `run.py` además se niega
explícitamente a lanzar si detecta un proceso `dmc-recomp` preexistente
no propio). No se interactuó con el proceso PCSX2 del usuario (no se
usó PCSX2 en absoluto en esta iteración, según lo previsto por el
prompt — la hipótesis del scheduler era testeable íntegramente dentro
de RECOMP).

## 18. Git status/diff final

Repo principal (sin cambios propios de contenido más allá de este
informe nuevo, más la nota canónica que se actualizará después de este
punto):

```
 M analysis/notes/BLOCKER_004_pss_video_output.md
 M runtime/dmc_overrides.cpp
 M upstream.lock.json
?? analysis/notes/BLOCKER_004_ASTRA_AUDIT.md
?? analysis/notes/BLOCKER_004_ASTRA_P311_OVERNIGHT.md
?? analysis/notes/BLOCKER_004_FABLE_P3111_CAUSAL_AUDIT.md
?? analysis/notes/BLOCKER_004_P312_SCHEDULER_BATCH_ORDER.md
?? analysis/notes/BLOCKER_004_P36_HEAP_COLLISION_AUDIT.md
?? analysis/tools/
?? patches/BUILD_ffmpeg_private_link_scope.patch
?? runtime/p311_pad_test.inc
```

Vendor (`vendor/PS2Recomp`), diffstat:

```
 ps2xRuntime/include/ps2_runtime.h              |  82 ++++-
 ps2xRuntime/src/lib/Kernel/EeScheduler.cpp     |  43 ++-
 ps2xRuntime/src/lib/Kernel/Stubs/Audio.cpp     |  17 +
 ps2xRuntime/src/lib/Kernel/Stubs/MPEG.cpp      | 346 +++++++++++++++++-
 ps2xRuntime/src/lib/Kernel/Syscalls/System.cpp |  19 +-
 ps2xRuntime/src/lib/ps2_runtime.cpp            | 486 +++++++++++++++++++------
 6 files changed, 846 insertions(+), 147 deletions(-)
```

El único cambio semántico propio de este prompt está contenido dentro
del diff de `EeScheduler.cpp` (las dos ramas de drenaje de
`m_pendingInvocations`, front→back). El resto del diff de vendor
pertenece íntegramente a P3.7.1-P3.11 (heap arena, hardening,
diagnósticos MPEG/scheduler previos), sin reversión ni modificación
adicional. HEADs principal y vendor sin cambios: `888f22ef…` /
`0f76399b…`.

## 19. Artefactos de esta iteración

- `analysis/notes/BLOCKER_004_P312_SCHEDULER_BATCH_ORDER.md`: este
  informe.
- `vendor/PS2Recomp/ps2xRuntime/src/lib/Kernel/EeScheduler.cpp`: fix de
  dos líneas (más comentarios), sección 5-6.
- `analysis/local/p311/RUN_012`, `RUN_013`, `RUN_014`: corridas nuevas
  (misma convención de directorio que P3.11; no se creó una carpeta
  `p312` separada porque el harness numera automáticamente dentro de
  `analysis/local/p311/` y reutilizarlo evita duplicar infraestructura).
  Cada una con `runtime.log`, `result.json`, capturas, dos dumps RAM de
  32 MiB, `ram_analysis.json`, `buffer_probe.json`,
  `demuxed_video_expected.es`, `video_es_preinit.bin`, `pss_preinit.bin`.
- `p312_scheduler_order_test.cpp` (+ `.exe` compilado): test aislado del
  invariante de orden, mantenido fuera del árbol del repositorio en el
  directorio de trabajo temporal de la sesión (no es un artefacto de
  build del proyecto).
- Ningún artefacto nuevo bajo `analysis/tools/` — se reutilizó
  `run.py`/`analyze_ram.py`/`probe_buffers.py` de P3.11 sin
  modificarlos.

**Resultado Prompt P3.12 —
SCHEDULER_BATCH_INVOCATION_ORDER_FIX_AND_REVERIFY**
