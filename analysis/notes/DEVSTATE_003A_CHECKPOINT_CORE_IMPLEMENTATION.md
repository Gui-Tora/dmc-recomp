# DEVSTATE_003A — Cierre de UNKNOWNs + infraestructura mínima del checkpoint

Fecha: 2026-09-24/25. Normativo: `DEVSTATE_002_CHECKPOINT_SERIALIZATION_DESIGN.md`.
Estado previo preservado: dirty vendor pre-fase = MPEG.cpp / Pad.cpp /
gs_cpu_backend.cpp (SHA del diff `71c8e976…`, snapshot en
`analysis/local/devstate_003a/pre_phase_vendor_dirty.patch`). Esos cambios NO
pertenecen a esta fase.

## 1. Cierre UNKNOWN-1 (IOP por servicio) — CLOSED

Auditoría completa de los 9 servicios de `ps2x::iop::IopSubsystem`
(ps2xIOP/src/modules). Tabla owner → miembros mutables → clase:

| Servicio | TU | Miembros mutables | Host refs/handles | Clase |
|---|---|---|---|---|
| cdmodule | cdmodule.cpp | `m_movie{active,resourceId,lba,totalSize,bytesConsumed}` | handle host abierto/cerrado DENTRO del RPC (l.359-393), nunca retenido | B (POD); handle F→ausente en safe-point |
| clfile | clfile.cpp:591-599 | `m_fileHandles` (u32→`ClFileHandle{handle u64, size u32, position u64}`), `m_loads` (u32→`ClFileLoad{status,size}`), `m_nextFileHandle`, `m_nextLoadHandle` | `handle` u64 = recurso del provider host | handles: SERIALIZE_LOGICAL (nombre/recurso+posición) o gate vacío; loads/contadores: A; `m_root`: E |
| cri_dtx | cri_dtx.cpp:1268-1277 | 5 maps u32→POD (`TransferState{dtxId,remoteHandle,eeWork,iopWork,size}`, SjxState, Ps2RnaState, SjrmtState, remoteById) + `m_nextUrpcObject` + contadores dma/urpc | ninguno | A/B |
| dbcman | dbcman.cpp | `m_unknownRpcLogCount` | — | D |
| libsd | libsd.cpp | (sin miembros; el estado vive en Audio.cpp EE-side) | — | D/E |
| mcserv | mcserv.cpp | `m_unknownRpcLogCount` | — | D |
| sdrdrv | sdrdrv.cpp:297-300 | warn counters | `openSiblingFile` handles u64 transitorios (no retenidos) | D |
| sound_update_stub | sound_update_stub.cpp:211-214 | `m_updateCounter`, `m_completedStreamCount` | — | A |
| tsnddrv | tsnddrv.cpp:265-275,563 | `State{initialized + 6 direcciones IOP-RAM}` | — | A (datos apuntados en MEM) |

Interfaz propuesta (a materializar cuando se codifique la sección IOP, patrón
`appendDebugMetrics` ya existente en `iop_service.h:36`):
`virtual void serializeState(ByteWriter&) const`,
`virtual bool deserializeState(ByteReader&)`,
`virtual bool validateCheckpointSafe(std::string &reason) const`.
Residual de implementación (no de diseño): semántica de reapertura del handle
u64 del provider para clfile.

## 2. Cierre UNKNOWN-2 (timers + globals) — CLOSED

- Timers EE: `PS2Memory::EeTimer{count,mode,compare,hold,clockRemainder}` ×4
  (`m_eeTimers`, ps2_memory.h:442-451); `resetEeTimers()` = `{}`;
  `advanceEeTimers()` acumula con resto fraccional en `clockRemainder`.
  Estado REAL fuera de `m_ioRegisters` → serializado explícitamente en MMIO.
- Barrido de globals mutables (por TU):
  - `Syscalls/Helpers/State.h`: `inline g_fileDescriptors` (fd→FILE*,
    **F**; nuevo gate `fioOpen==0`), `inline g_nextFd` (A), `inline
    g_fd_mutex` (F→C). `inline` ⇒ instancia única entre TUs.
  - `Syscalls/RPC.cpp`: `g_rpc_active_queue` (u32 guest, A; la cola en sí
    vive en RDRAM), `g_sif_modules_by_id` (A), ring debug (D).
  - `Syscalls/Deci2.cpp`: `g_deci2Sessions` (map int→POD) + `g_nextDeci2Socket` (A).
  - `Stubs/DMA.cpp`: `g_dmaCurrentEnv` (SceDmaEnv, A).
  - `Stubs/SIF.cpp`: ya inventariado (g_sifRegs/Sregs/CmdHandlers/Heap*, A).
  - `Stubs/GS.cpp`: `g_gparam` (A; copia viva del TU GS.cpp).
  - `Support.h` (anon-ns POR TU): CD state (TU autoritativo CD.cpp),
    índices CD (C), `g_iopHeapNext` (A, TU del mutador). Hallazgo
    arquitectónico documentado: cada TU que incluye Support.h posee su copia;
    los serializers deben residir en el TU autoritativo.
  - `Stubs/{Font,IPU,Compatibility}`: sin estado mutable global.
  - statics locales restantes: contadores de log (D).
- ps2_memory adicional: `m_codeRegions` (start/end + bitmap modified) →
  incluido en MMIO.

## 3. Cierre UNKNOWN-3 (semáforos/event flags) — CLOSED

Lectura línea a línea `ee_scheduler.h:40-160`:
`EeSemaphore` = {id,count,maxCount,initCount,attr,option,waiters:deque<int>};
`EeEventFlag` = {id,attr,option,initBits,bits,waiters:deque<int>}; payloads
de wait (`EeSemaphoreWait/EeEventFlagWait/EeVSyncWait/EeExternalWait`) PODs.
**Ningún callable persistente en esos structs.** Los únicos `std::function`
de la cadena son `EeWaitState::completion` (h:81) y
`GuestInvocation::onComplete` (h:101) — exactamente los cubiertos por los
gates `waitFn/pendingInvFn/invocationFn = 0` del safe-point. `EeAlarm` y
`EeIrqHandler` portan solo direcciones guest + gp/sp (serializables).

## 4. Estado mutable nuevo descubierto

(1) Tabla fio `g_fileDescriptors`/`g_nextFd` (FILE* host) — nuevo gate del
validador `fioOpen==0` + `g_nextFd` a serializar en fase SIF/IO;
(2) `EeTimer.clockRemainder` (acumulador fraccional);
(3) `g_gparam`, `g_iopHeapNext`, `g_dmaCurrentEnv`, `g_deci2Sessions`,
`g_rpc_active_queue`, `m_codeRegions`;
(4) semántica per-TU de Support.h (copias por unidad de traducción).
Todo incorporado a DEVSTATE_002 §16-bis; ninguna contradicción con el diseño,
solo adiciones.

## 5. Cambios al diseño DEVSTATE_002

Sección §16-bis añadida (UNKNOWN-1/2/3 = CLOSED, inventario nuevo). El resto
del diseño permanece válido; el gate del validador gana `fioOpen==0`.

## 6-7. Arquitectura implementada / archivos

Nueva unidad `runtime/checkpoint` (nombres alineados con las convenciones del
árbol `include/runtime/**` + `src/lib/**`):

- `include/runtime/checkpoint/checkpoint_format.h` — magic/versions/ids,
  `FileHeader`/`SectionEntry` (padding-free, static_asserts), CRC-32.
- `include/runtime/checkpoint/checkpoint.h` — ByteWriter/Reader,
  CheckpointFileWriter (tmp+rename atómico), CheckpointFileReader
  (validate-all-first), CheckpointManager (save/loadPartial/validateSafePoint
  /codecs), `CheckpointLoadStage{None,Parsed,PartialRestore,Complete}` con
  `Complete` inalcanzable en esta fase, runtimeBuildId(), selftests.
- `include/runtime/checkpoint/checkpoint_probes.h` — probes de quiescencia.
- `src/lib/checkpoint/checkpoint_io.cpp` — framing/CRC/commit/parse.
- `src/lib/checkpoint/checkpoint_sections.cpp` — codecs CORE/MEM/MMIO campo a
  campo, validador de safe-point, orquestación.
- `src/lib/checkpoint/checkpoint_selftest.cpp` — tests 1-10 (+8b).
- Modificados: `ee_scheduler.h/EeScheduler.cpp` (struct+método público
  `collectCheckpointQuiescence()` read-only), `ps2_runtime.h` (fwd-decl +
  `friend CheckpointManager`), `main.cpp` (hook opt-in
  `DMC_CHECKPOINT_SELFTEST=1`: corre tests contra el runtime construido y
  sale ANTES de ejecutar guest), probes en `MPEG.cpp`/`CD.cpp`/`Audio.cpp`/
  `MemoryCard.cpp`/`FileIO.cpp`, `CMakeLists.txt` (3 unidades nuevas), y el
  vcxproj GENERADO LOCAL (artefacto de build en analysis/local, editado a
  mano para compilar sin regenerar CMake).

Guardas anti-resume: nada en la unidad arranca el scheduler; `loadPartial`
termina en `PartialRestore` (estado explícito); el hook selftest retorna de
`main` antes de `runtime.run()`.

## 8. Header v1 exacto

`FileHeader` (128 bytes, little-endian, sin padding): magic "DMCK"
(0x4B434D44), formatVersion=1, architectureId=1 (x86-64 LE), safePointId=1
(MAIN_MENU_V1), gameId[32] NUL-padded, elfSha256[32], runtimeBuildId[32]
(hash 4×FNV-1a-64 lanes del exe — identidad estricta v1, no criptográfico),
savedAtUnixMs u64 (informativo, excluido de idempotencia), sectionCount u32,
headerCrc32 u32 (CRC de header con el campo a 0). Tabla:
`SectionEntry{id,version,offset u64,size u64,crc32,flags}` (32 B). Política:
secciones desconocidas ⇒ rechazo; identidad juego/ELF/build distinta ⇒
rechazo; todo checksum se valida antes de mutar estado.

## 9. Layout de secciones

- CORE v1: R5900Context campo a campo (r[32] como 16 B c/u, PC/HI/LO/SA,
  VU0-macro completo, COP0 22 regs, llbit/lladdr, delay-slot como u8,
  cop2_ccr, FPU) + `VU1State`×2 campo a campo (bools como u8). Sin bytes de
  padding en el payload ⇒ determinista.
- MEM v1: 8 regiones etiquetadas (tag,u64 size,bytes): RDRAM 32 MiB, SPR
  16 KiB, IOP RAM 2 MiB, VU0 code/data 4+4 KiB, VU1 code/data 16+16 KiB,
  GS VRAM 4 MiB; + 2 `GuestArenaState` (bloques {addr,size,free}) +
  loadedModules (name len+bytes, base, size, active). LOAD valida tag y
  tamaño exacto de cada región antes de escribirla.
- MMIO v1: m_ioRegisters ordenado por clave; dma_regs[10] campo a campo;
  vif0/vif1 (bloques all-u32 con static_assert); gs_regs (20×u64, atomics
  por valor); TLB; **EeTimer[4] incl. clockRemainder**; path3Masked +
  vif1PendingPath2 + path3MaskedFifo; seenGifCopy + 6 contadores/generaciones
  atómicos; m_codeRegions con bitmap. Las cuatro colas NO se serializan:
  SAVE exige vacías, LOAD las limpia (contrato v1).

## 10. Safe-point validator

`CheckpointManager::validateSafePoint()` implementa el contrato DEVSTATE_001A
con las mismas reglas de conteo de la sonda certificada:
scheduler (nuevo `collectCheckpointQuiescence()`): activeInv=0,
pendingInvFn=0, rpcInv=0, activeInvFn=0, resumeFn=0, waitFn=0, events=0,
timerIrqMask=0, pendingInv≤1 (y sin onComplete — la única invocación
tolerada es la VSync serializable); memoria: 4 colas vacías, CHCR.STR=0 en
los 10 canales; `gifArbiter().empty()`; GS sin transferencia parcial
(GSDebugSnapshot); probes: MC open/pending=0, MPEG=0x0, CD=0x0, audio=0x0,
**fioOpen=0** (gate nuevo). Devuelve `{ok, reason, vsyncTick, pendingInv,
failedGateValue}`. `save()` lo ejecuta primero y no escribe nada si falla.

## 11-13. Tests, resultados y tamaño

(Se completa tras la ejecución del selftest — ver bloque de resultados al
final.)

## 14. Padding / no determinismo encontrado

- `FileHeader`/`SectionEntry`: sin padding (static_assert).
- R5900Context/VU1State contienen bools y u16 ⇒ NO se vuelcan como bloque;
  serialización por campo (payload sin padding).
- `m_ioRegisters` es unordered ⇒ ordenado por clave al serializar.
- Atomics: leídos/escritos por valor.
- `savedAtUnixMs`: único campo intencionalmente no determinista, excluido de
  comparaciones (test 8b lo enmascara junto con headerCrc32).

## 15. Riesgos restantes

- La edición de `ps2_runtime.h` (friend) obliga a recompilar las unidades
  generadas que lo incluyen — one-time, mismos fuentes y flags (no hay
  regeneración de pipeline); documentado como desviación operativa.
- clfile u64 handle: definir reapertura lógica antes de la sección IOP.
- El validador asume ejecutarse en el boundary del executor (como la sonda);
  el punto de integración de SAVE en runtime vivo (hook de scheduler) llega
  con SCHED en 003B.

## 16. Próxima fase propuesta

DEVSTATE_003B: sección SCHED (threads/queues/sem/flags/alarms/ids/deadlines
re-derivados) + GS frontend/backend PODs + gancho de save en el boundary del
scheduler; después IOP/SIF_RPC/CD/MC/AUDIO/PAD/MPEG y el load integrado en el
arranque (`PS2Runtime::run()` pre-reset), con TESTs A-G.

## 11-13. Resultados de tests (PARTE 7) y tamaño

Ejecutados vía `DMC_CHECKPOINT_SELFTEST=1 dmc-recomp.exe SLES_503.58` (el
hook corre tras `initialize()+loadELF()` — memoria guest alocada — y retorna
de `main` ANTES de `runtime.run()`: el guest jamás ejecuta). Exit code 0.

| Test | Resultado |
|---|---|
| 1 header round-trip (commit/parse/campos/payload) | PASS (4/4) |
| 2 corrupción header (magic, versión/CRC, offset de sección) | PASS (3/3) |
| 3 checksum de payload (1 byte corrupto) | PASS |
| 4 truncados (5 offsets de corte) | PASS |
| 5 incompatibilidad (game/elf/build + control positivo) | PASS (4/4) |
| 6 CORE determinista + round-trip idéntico | PASS (2/2) |
| 7 MEM determinista + round-trip idéntico | PASS (2/2) |
| 8 MMIO determinista + round-trip idéntico | PASS (2/2) |
| 8b save completo real + re-parse + idempotencia (savedAt/CRC enmascarados) | PASS (4/4) |
| 9 safe-point rejection (cola forzada no vacía ⇒ save rehusado, sin archivo) | PASS (2/2) |
| 10 atomicidad (fallo pre-rename sin archivo final; commit limpio sin .tmp) | PASS (4/4) |

Total: 28 aserciones, 0 fallos.

**Tamaño real del checkpoint parcial (CORE+MEM+MMIO): 41.097.508 bytes
(~39,2 MiB)** — dominado por MEM (38,04 MiB de regiones) + MMIO (~2,9 MiB,
mayormente el bitmap de m_codeRegions y el estado VIF/path3) + CORE ~4,7 KiB.

Nota de ejecución: el primer intento segfaulteó porque el hook original corría
antes de `initialize()` (regiones de memoria sin alocar); corregido moviendo
el hook tras `initialize()+loadELF()`. Ningún otro defecto apareció.
