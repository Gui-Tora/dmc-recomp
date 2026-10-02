# DEVSTATE_003B — SCHED + GS + SAVE real en el safe-point

Fecha: 2026-09-25. Normativos: DEVSTATE_001A / 002 / 003A.

## 1. Baseline

Capturado antes de tocar código en `analysis/local/devstate_003b/`
(`baseline_root.txt`, `baseline_vendor.txt`, `baseline_vendor_dirty.patch`
SHA-256 `c1d7343bf085a5130bf62cdc22e8a6ef475af2306ca6e4a669780aad1e7bafbb`).
El dirty contiene: (a) el dirty de producción PRE-DEVSTATE (MPEG.cpp, Pad.cpp,
gs_cpu_backend.cpp) y (b) los cambios DEVSTATE_003A. Nada se revirtió; no se
limpió el build.

## 2. Limpieza documental DEVSTATE_002

§17 actualizado: UNKNOWN-1/2/3 marcados **CLOSED** (remiten a §16-bis y a
DEVSTATE_003A); único pendiente real: **UNKNOWN-4** (dirección RDRAM del
cursor de menú para TEST B, OPEN, no afecta al formato).

## 3. Layout exacto SCHED (sección id=4, v1)

Codec como métodos de `EeScheduler`
(`serializeCheckpointState`/`deserializeCheckpointState`, EeScheduler.cpp),
todo little-endian por `ByteWriter`, contenedores unordered emitidos en orden
de clave:

1. threads (u32 count; orden id asc): id u32, `R5900Context` (codec
   compartido `writeR5900Context` — mismo del CORE), entry/stack/stackSize/
   gp/attr/option/arg u32, initialPriority/currentPriority u32, status u8,
   suspendCount u32, wakeupCount u32, ownsStack u8, tlsBase u32, wait.reason
   u8, wait.payload discriminado (tag u8: 0=mono,1=sem{id},2=evf{id,bits,
   mode,resultAddress},3=vsync{afterTick u64,fixedResult},4=external{type,
   token u64}), invocations (u32 count; kind u8, sequence u64, tag u64,
   contexto R5900). **Rechazo en SAVE** si `wait.completion`,
   `resumeCompletion` o cualquier `onComplete` está vivo.
2. readyQueues[128]: u32 count + ids en orden exacto.
3. semaphores (orden id): id,count,maxCount,initCount,attr,option + waiters
   en orden. 4. eventFlags: id,attr,option,initBits,bits + waiters en orden.
5. alarms: id,ticks u16,handler,argument,gp,sp (solo direcciones guest).
6. intcHandlers y 7. dmacHandlers: id,cause,handler,argument,gp,sp,enabled
   u8,order.
8. allocators/orden/masks/dispatch: nextThreadId, nextInvocationThreadId,
   nextSemaphoreId, nextEventFlagId, nextAlarmId, nextIntc/DmacHandlerId,
   intc/dmacHead/TailOrder, enabledIntc/DmacMask, currentThreadId,
   rescheduleRequested/timeSliceExpired/insideInterrupt u8,
   pendingEeTimerInterrupts u32, eeCycle u64, sliceEndCycle u64,
   debugPublishCountdown u32.
9. pendingInvocations: u32 count + (kind,sequence,tag,contexto) — **nunca**
   `onComplete` (rechazo si existe).
10. vsyncTick u64, vsyncFlagAddress, vsyncTickAddress, gsVSyncCallback/Gp/Sp,
    eventSequence u64, invocationSequence u64, invocationStackTops (u32 count
    + pares u64→u32 en orden de clave).
11. events: u32 count **que debe ser 0** (rechazo si no; LOAD también rechaza
    checkpoints con eventos encolados).
12. deadlines: u32 count + por entrada `deadlineCycle u64, sequence u64,
    event{type u8,id u32,value u64}` — **sin** `hostDeadline`.

## 4. Layout exacto GS (sección id=5, v1)

Codec como métodos de `GS` (gs_frontend.cpp; el header compartido
`gs_frontend.h` solo ganó fwd-decls de ByteWriter/Reader y las tres firmas —
tocado UNA única vez, ver §16-build):

m_ctx[2] campo a campo (frame{fbp,fbw,psm,fbmsk}, scissor{x0,x1,y0,y1},
tex0 completo, xyoffset, zbuf{zbp,psm,zmask}, tex1/miptbp1/miptbp2/clamp/
alpha/test/fba u64) · m_prim/m_primRegister/m_prmodeRegister (9 campos u8
c/u) · curR/G/B/A u8, curQ/S/T f32, curU/V u16, curFog u8, fogR/G/B u8 ·
prmodecont/pabe u8 · scanmsk/dimx/dthe/colclamp u64 · texa{ta0,aem,ta1} ·
texclut{cbw,cou,cov} · bitbltbuf/trxpos/trxreg + trxdir u32 · vertex queue
(vtxCount/vtxIndex u32 + 6×GSVertex campo a campo, z como u64 bits) ·
lastDisplayBaseBytes u32.

Backend: NO se serializa por separado; en restore se re-ancla el comando de
transferencia lógico vía `GSRasterBackend::BeginTransfer` con `direction=3`
(idle) — los contadores copied/total quedan normalizados al estado
completado por construcción (gate del safe-point: sin transferencia parcial,
`localToHostPendingBytes==0`, `vtxCount==0` — nuevo `GS::checkpointIdle()`
integrado en el validador global).

GS-6/7 por construcción: la sección no contiene VRAM (vive en MEM) ni ningún
buffer de presentación host (sección ~1 KiB).

## 5. Política de deadlines (PARTE 3)

SAVE persiste solo `deadlineCycle/sequence/event`. LOAD: restaura `m_eeCycle`
primero (mismo payload, campo 8), luego por deadline calcula
`remaining = deadlineCycle − m_eeCycle` y `hostDeadline = steady_clock::now()
+ eeCyclesToHostDuration(remaining)` (la MISMA conversión que usa el
scheduler en producción), y finaliza con `updateNextDeadline()`. El VBlank
periódico es una entrada más de `m_deadlines`, por lo que conserva su fase
lógica exacta (mismo `deadlineCycle`), y su re-programación en cadena la hace
el propio `processDueDeadlines` al reanudar en fases futuras. Test dirigido
SCHED-9/10: mismo `remainingCycles` tras restore y `hostDelayNanos` ≈
remaining/294.912 MHz (±200 ms de tolerancia de reloj).

## 6. Coherencia VSync

Authoritative: `EeScheduler::m_vsyncTick`. SAVE rechaza si
`gs_regs.vsyncTick != m_vsyncTick` (gate en `serializeSched`). LOAD: MMIO
escribe el espejo GS y `deserializeSched` re-escribe el espejo desde la
fuente autoritativa restaurada. Test SCHED-11 cubre ambas direcciones.

## 7. Host graphics invalidation

`GS::deserializeCheckpointState` limpia: displaySnapshot, preferred-source
latch, hostPresentationFrame (+flags/dimensiones), debug history. Nada de
raylib/GL/mutex/thread-locals entra jamás en el checkpoint; la primera
presentación tras un restore se regenera desde VRAM+registros restaurados.

## 8. Diseño requestCheckpoint

`EeScheduler::requestCheckpoint(path, identity)` — llamable desde cualquier
hilo; guarda path/identity bajo `m_checkpointRequestMutex`, publica
`m_checkpointRequestPending` (release) y despierta al executor
(`m_eventCv.notify_all`). Estados: `Idle → Pending → Saving →
Success|Failed` (`CheckpointRequestStatus` con message, completedTick,
fileSizeBytes, saveMillis; consultable vía `checkpointRequestStatus()`).
Segundo request con uno en vuelo ⇒ `false` (busy). El flag pending se limpia
SIEMPRE al terminar (éxito o fallo) — sin atascos.

## 9. Boundary hook

En `EeScheduler::run()`, inmediatamente después de `processPendingEvents()` y
del check de stop, ANTES de seleccionar/despachar guest:
`if (m_checkpointRequestPending) handleCheckpointRequestAtBoundary();`
El handler (executor-only, `assertExecutor`) ejecuta
`CheckpointManager::save()` — que valida el safe-point completo primero — y
publica el resultado + línea `[CKPT:REQUEST]`. Ningún otro hilo lee estado
guest durante el save (el executor está dentro del handler, no despachando).
Trigger de usuario: F9 en el render loop (ps2_runtime.cpp) SOLO ENCOLA el
request; la identidad del proceso la registra `main()` tras `loadELF`
(`setProcessIdentity`: gameId=elfName, digest del ELF, build-id).

Orden real de LOAD ampliado (loadPartial): MEM → MMIO → CORE
(stage `CoreMemMmioRestored`) → GS → SCHED (stage `SchedGsRestored`) →
`PartialRestore` terminal. `Complete` sigue sin una sola asignación en el
código.

## 10-14. Tests y SAVE real

(Se completa tras el build/ejecución — ver bloque de resultados.)

## 15. Determinismo

SCHED/GS emiten por campo, sin padding, contenedores ordenados; los únicos
datos no deterministas (hostDeadline) quedan fuera del formato. SCHED-12 y
GS-8 verifican bytes idénticos en doble serialización.

## 16. Archivos modificados (esta fase)

- `include/runtime/ee_scheduler.h` (barato: no lo incluye generated):
  API request + codec + deadline views + test helpers + include checkpoint.h.
- `src/lib/Kernel/EeScheduler.cpp`: codec SCHED completo, request handling,
  hook del boundary, helpers de test.
- `include/runtime/gs/gs_frontend.h` (**única** modificación de header
  compartido de la fase → una recompilación masiva one-time de generated):
  fwd-decls + 3 firmas.
- `src/lib/gs/gs_frontend.cpp`: codec GS + checkpointIdle + invalidación
  host.
- `include/runtime/checkpoint/checkpoint_format.h`: versiones SCHED/GS.
- `include/runtime/checkpoint/checkpoint.h`: stages intermedios, codec R5900
  compartido, wrappers sched/gs, identidad de proceso, computeFileDigest.
- `src/lib/checkpoint/checkpoint_sections.cpp`: wrappers, gate GS en el
  validador, save() con 5 secciones, loadPartial reordenado.
- `src/lib/checkpoint/checkpoint_selftest.cpp`: SCHED-1..12, GS-1..8.
- `src/lib/ps2_runtime.cpp`: trigger F9 (solo encola).
- `src/main.cpp`: registro de identidad de proceso.

## 17. Dirty previo vs nuevo

Previo (intacto): MPEG.cpp / Pad.cpp / gs_cpu_backend.cpp (producción
pre-DEVSTATE) + todo DEVSTATE_003A. Nuevo: la lista de §16.

## 18. Riesgos / UNKNOWN nuevos

- La reconstrucción del backend-transfer normaliza copied/total al estado
  idle (documentado; el gate garantiza equivalencia semántica).
- El GS `writeRegister`-path no se usa para restore (asignación directa por
  métodos de la clase) — sin riesgo de re-disparar transfers.
- `reset()` en el selftest convierte el hilo de test en executor; documentado
  como uso test-only.

## 19. Próxima fase propuesta

DEVSTATE_003C: secciones IOP/SIF_RPC/CD/MC/AUDIO/PAD/MPEG (interfaz
por-servicio IOP), y después el LOAD integrado en el arranque + resume
(TESTs A-G de DEVSTATE_002).

## Resultados (tests + SAVE real)

HECHO. Build masivo completado (recompilación one-time por `gs_frontend.h`,
tal como se anticipó en §13/§16). Suite completa de self-tests ejecutada
contra el runtime real (`DMC_CHECKPOINT_SELFTEST=1`):

```
[CKPT:SELFTEST] done, failures=0
```
60 aserciones PASS, 0 FAIL (TESTs 1-10 de DEVSTATE_003A + SCHED-1..12 +
GS-1..8 de esta fase). Primer intento falló con 1 FAIL en la fabricación de
estado (`setAlarm` exige un handler registrado en la tabla de funciones del
recompiler — se corrigió usando la entry real del ELF `0x100008`), corregido
y re-verificado limpio.

### Tests SCHED (resultado)

SCHED-1 (threads), SCHED-2 (ready queue order), SCHED-3 (semáforos +
waiters), SCHED-4 (event flags + waiters), SCHED-5 (allocators/ids), SCHED-6
(invocación pendiente sin onComplete — round-trip), SCHED-7 (invocación CON
onComplete — rechazo, incluido rechazo del `save()` completo vía
validador), SCHED-8 (wait completion — rechazo): **todos PASS**, verificados
por round-trip byte-idéntico tras restore (`schedC == schedA`).
SCHED-9 (deadline lógico preservado: mismos `deadlineCycle`/`remainingCycles`
tras restore) y SCHED-10 (`hostDeadline` re-derivado a los mismos ciclos
restantes, tolerancia ±200 ms de reloj): **PASS**. SCHED-11 (coherencia
`vsyncTick` scheduler↔GS: mismatch forzado rechazado, valor coherente
aceptado): **PASS**. SCHED-12 (determinismo byte a byte en doble
serialización): **PASS**.

### Tests GS (resultado)

GS-1 (round-trip frontend), GS-2/3 (contextos y vertex queue exactos tras
escritura de registros reales vía `writeRegister`), GS-4 (transfer state
round-trip en idle), GS-6 (sección sin VRAM embebida, <64 KiB), GS-7 (sin
payload de presentación host), GS-8 (determinismo): **todos PASS**. GS-5
(transferencia host→local parcial forzada vía BITBLTBUF/TRXREG/TRXDIR real):
`checkpointIdle()` la detecta y el validador global la rechaza — **PASS**;
limpieza posterior (restore del estado idle capturado antes) confirmada.

### SAVE real en vivo (Main Menu certificado)

Sesión real lanzada visiblemente (`analysis/local/devstate_003b/
runtime_save_session2.log`), navegación manual del usuario hasta Main Menu
confirmada por el log (`[MPEG:delete]` + tick de VSync en el rango
certificado DEVSTATE_001A). Trigger F9 (render thread, solo encola) →
procesado en el boundary real del scheduler:

```
[CKPT:REQUEST] queued (F9); waiting for scheduler boundary
[CKPT:REQUEST] SUCCESS tick=1222 path=dmc_checkpoint_mainmenu.dmck size=41105997 ms=333.85
```

**SAVE-REAL-1**: PASS — el safe-point validator (todos los gates: activeInv,
pendingInvFn, rpcInv, resumeFn, waitFn, events, timerIrq, las cuatro colas,
dmaActive, gifArbiter, GS idle, mcOpen/Pending, mpeg/cd/audio guest-visibles,
fioOpen) pasó en la corrida real, no en un runtime fabricado.
**SAVE-REAL-4**: PASS — tras el save el guest siguió avanzando con
normalidad (VSync 1260→1320 observado), ventana `Responding=True`; el SAVE
no alteró la continuidad de ejecución.

Verificación independiente del archivo (script Python standalone,
`analysis/local/devstate_003b/verify_checkpoint.py`, reimplementa el framing
exacto de `checkpoint_format.h` y CRC-32 IEEE sin depender del build C++):

```
RESULT: ALL_SECTIONS_VALID
  [CORE ] offset=288    size=2,929      crc32=0x1A1B00AF -> OK
  [MEM  ] offset=3217   size=39,903,480 crc32=0x77654011 -> OK
  [MMIO ] offset=39906697 size=1,191,139 crc32=0x1C242F4D -> OK
  [GS   ] offset=41097836 size=565      crc32=0xFF055089 -> OK
  [SCHED] offset=41098401 size=7,596    crc32=0x21D3040E -> OK
```

header CRC32, magic, formatVersion, architectureId, safePointId: todos OK.

**Tamaño real del checkpoint (5 secciones de esta fase): 41.105.997 bytes
(~39,2 MiB)** — CORE 2.929 B, MEM 39.903.480 B, MMIO 1.191.139 B, GS 565 B,
SCHED 7.596 B. Tiempo de SAVE: 333,85 ms.

**Nota para DEVSTATE_003C**: este archivo de 5 secciones (`dmc_checkpoint_
mainmenu.dmck`, conservado en el repo de trabajo) es la prueba de SAVE real
v1-parcial; NO es un checkpoint v1-completo (le faltan IOP/SIF_RPC/CD/MC/
AUDIO/PAD/MPEG) y debe ser rechazado explícitamente por el parser una vez el
esquema v1 complete exija las 12 secciones ("required section missing"), no
aceptado parcialmente. Útil en 003C como fixture de test de rechazo
retrocompatible.
