# DEVSTATE_002 — Diseño del serializador/restaurador de checkpoint

Fase: SOLO DISEÑO. Sin implementación, sin archivo de checkpoint, sin commit, sin push.
Método: lectura de código del árbol actual (main `95af5dce`, vendor `0efd17c3` + dirty de producción MPEG/Pad/gs_cpu_backend). Ninguna modificación de producción en esta fase.

## 1. Executive summary

El runtime es una recompilación ESTÁTICA (no hay JIT ni cache de código en caliente: la tabla PC→función se registra en `register_functions.cpp` generado y es inmutable), con un solo hilo ejecutor EE (`EeScheduler::run()`), memoria guest plana (RDRAM 32 MiB + scratchpad 16 KiB + IOP RAM 2 MiB + VU mems + GS VRAM 4 MiB, todas propiedad de `PS2Memory`), y subsistemas HLE cuyo estado mutable vive en structs POD o contenedores STL de tipos planos, con un número pequeño y bien identificado de objetos host no serializables (`std::function`, mutexes, hilos, FFmpeg, raylib/GL, FILE handles). El safe-point certificado de DEVSTATE_001A (Main Menu, boundary del scheduler, 1140 ticks quiescentes con las cuatro colas y todo trabajo host a cero) elimina por construcción casi todos los casos difíciles: no hay invocación con `onComplete`, no hay RPC en vuelo, no hay transferencia GIF/VIF/DMA parcial, no hay MC/CD/MPEG/audio activo. La conclusión del diseño es que un checkpoint v1 restringido a ese safe-point es implementable con secciones mayoritariamente `SERIALIZE_EXACT` (memorias y PODs) más un conjunto acotado de `SERIALIZE_LOGICAL` (colas del scheduler sin `std::function`, deadlines re-derivados, banco de samples de audio) y una política clara de reconstrucción host. Clasificación final: **DEVSTATE_SERIALIZER_DESIGN_READY**, con los UNKNOWN listados en §17 (ninguno bloquea el diseño; dos condicionan la implementación).

## 2. Safe-point contract heredado (DEVSTATE_001A — congelado)

Precondición de SAVE (verificable en el mismo boundary donde se guarda):
`EeScheduler::run()`, tras `processPendingEvents()` y antes de seleccionar/despachar guest, con: `activeInv=0`, `pendingInvFn=0`, `rpcInv=0`, `resumeFn=0`, `waitFn=0`, `invocationFn=0`, `events=0`, `timerIrq=0` (pendientes ya drenados), `alarms=0`*, `dmaActive=0`, `gifQueued=0`, `gsPartial=0`, `pendingGifTransfers=pendingVif0=pendingVif1=completedDmacCauses=0`, `mcOpen=0`, `mcPending=0`, `mpeg=cd=audio=0x0` guest-visibles, y predicado Main Menu (PSS completado + `ps2_main` estacionado + confirmación). `pendingInv<=1` permitido SOLO si es la invocación GS-VSync sin `onComplete` (serializable como dato).
(*`alarms=0` fue lo observado; el diseño §8 admite serializar alarmas si existieran, ya que `EeAlarm` guarda dirección de handler guest, no callable host — ver `ee_scheduler.h`.)

## 3-5. Tabla completa de componentes: owner → estado → clasificación → dependencias de restore

Leyenda de clase: A=SERIALIZE_EXACT, B=SERIALIZE_LOGICAL, C=RECONSTRUCT_ON_LOAD, D=RESET_ON_LOAD_SAFE, E=IMMUTABLE_RECREATE_FROM_BOOT, F=FORBIDDEN_HOST_STATE.

### 3.1 EE / CPU guest

| Estado | Owner (archivo/miembro) | Clase | Notas restore |
|---|---|---|---|
| GPR 128-bit `r[32]`, `pc`, `hi/lo/hi1/lo1`, `sa`, `insn_count` | `R5900Context` (`ps2_runtime.h:58-144`); instancia principal `PS2Runtime::m_cpuContext` + una por `GuestThread.context` y por `GuestInvocation.context` | A | POD alineado; copiar por miembro/bloque. Arquitectura fija x86-64 little-endian (el header usa `__m128i`); el formato declara arch-id (§8). |
| VU0 macro-mode (`vu0_vf[32]`, `vi[16]`, q/p/i/r/acc, status, mac/clip flags, cmsar, vpu_stat*, tpc, fbrst*, itop/top, `cop2_ccr[32]`) | mismo struct | A | Parte del mismo POD. |
| COP0 completo, `llbit/lladdr`, `in_delay_slot`, `branch_pc` | mismo struct | A | El recompilado retorna al dispatcher en boundaries; `in_delay_slot/branch_pc` viajan con el contexto. |
| FPU `f[32]`, `f_acc`, `fcr31` | mismo struct | A | — |
| Estado del recompiler | tabla `registerFunction/lookupFunction` (`ps2_runtime.h:327-328`, poblada por `register_functions.cpp` generado) | E | Inmutable por build; el header del checkpoint fija identidad de build (§8). |

### 3.2 Scheduler EE (`EeScheduler`, `ee_scheduler.h:394-450`)

| Estado | Miembro | Clase | Notas |
|---|---|---|---|
| Threads guest (id, `R5900Context`, entry/stack/gp/attr/option/arg, prioridades, `EeThreadStatus`, suspendCount, wakeupCount, ownsStack, tlsBase) | `m_threads` (`GuestThread`, `ee_scheduler.h:104-135`) | A | Mapa id→struct; el contexto es el POD de §3.1. |
| `GuestThread.wait` (`EeWaitState{reason,payload}`) | ídem | B | Se serializa reason+payload; **`completion` (std::function) es F** — safe-point garantiza `waitFn=0` (D). |
| `GuestThread.resumeCompletion`, `GuestThread.invocations[*].onComplete` | ídem | F→D | Safe-point: `resumeFn=0`, `activeInv=0`. SAVE valida y aborta si no. |
| `m_readyQueues[128]` (deques de ids) | `m_readyQueues` | A | Orden importa (round-robin). |
| Semáforos, event flags | `m_semaphores`, `m_eventFlags` | A/B | Structs de contadores/valores guest; los waiters se reconstruyen de `m_threads.wait`. `EeEventFlagWait` con callable: verificar en SAVE — ver UNKNOWN-3. |
| Alarmas | `m_alarms` (`EeAlarm`) | B | Handler = dirección guest + ciclos restantes; `hostDeadline` NO se copia: se re-deriva (ver `m_deadlines`). Safe-point observó 0. |
| INTC/DMAC handlers | `m_intcHandlers`, `m_dmacHandlers` + orden head/tail + masks (`m_enabledIntcMask/DmacMask`) | A | Direcciones guest + prioridades: datos planos. |
| Allocators/ids | `m_nextThreadId`, `m_nextSemaphoreId`, `m_nextEventFlagId`, `m_nextAlarmId`, `m_nextIntc/DmacHandlerId`, `m_intc/dmacHead/TailOrder`, `m_nextInvocationThreadId` | A | Imprescindibles para no colisionar ids tras load. |
| Estado de despacho | `m_currentThreadId`, `m_rescheduleRequested`, `m_timeSliceExpired`, `m_insideInterrupt`, `m_pendingEeTimerInterrupts`, `m_eeCycle`, `m_sliceEndCycle`, `m_debugPublishCountdown` | A | En safe-point `m_currentThreadId` es el estado del boundary. |
| `m_pendingInvocations` | deque de `GuestInvocation{kind,sequence,tag,context,onComplete}` | B | Serializar kind/sequence/tag/context SOLO si `onComplete==nullptr` (safe-point lo garantiza); si no, abortar SAVE. |
| Eventos y deadlines | `m_events` (0 en safe-point), `m_deadlines` (`ScheduledEvent` con `hostDeadline` `steady_clock::time_point`) | B | `m_events` vacío (D con validación). `m_deadlines`: persistir `deadlineCycle` + evento; `hostDeadline` es F → recomputar en LOAD a partir de `m_eeCycle` restaurado y el reloj host actual (`scheduleEvent`, `updateNextDeadline`). El VBlank periódico se reprograma en LOAD (mismo camino que `reset()`/arranque, `ee_scheduler.cpp` VBlank rescheduling). |
| VSync | `m_vsyncTick`, `m_vsyncFlagAddress`, `m_vsyncTickAddress`, `m_gsVSyncCallback/Gp/Sp`, `m_eventSequence`, `m_invocationSequence`, `m_invocationStackTops` | A | `m_vsyncTick` también se espeja en `PS2Memory.gs().vsyncTick` (§3.4). |
| Hilo ejecutor, mutex/cv, atomics de control | `m_executorThread`, `m_eventMutex/Cv`, `m_running`, `m_stopRequested`, `m_checkpointPending`, `m_snapshotMutex` | F/C | Objetos de proceso; LOAD ocurre con el ejecutor parado y los recrea el arranque normal. |

### 3.3 Memoria guest (`PS2Memory`, `ps2_memory.h`)

| Estado | Miembro | Clase | Tamaño |
|---|---|---|---|
| RDRAM | `m_rdram` (32 MiB) | A | 32 MiB |
| Scratchpad | `m_scratchpad` (16 KiB) | A | 16 KiB |
| IOP RAM | `iop_ram` (2 MiB) | A | 2 MiB |
| VU0 code/data, VU1 code/data | `m_vu0Code/Data`, `m_vu1Code/Data` (4/4/16/16 KiB) | A | 40 KiB |
| GS VRAM | `m_gsVRAM` (4 MiB) | A | 4 MiB |
| Heaps guest HLE | `PS2Runtime::m_gameHeapState`, `m_runtimeArena` (`GuestArenaState`: base/limit/bloques) | A | pequeño; los datos apuntados ya están en RDRAM |
| Módulos cargados | `PS2Runtime::m_loadedModules` (name/base/size/active) | A | pequeño |
| BIOS region | no poseída como copia mutable relevante (boot HLE) | E | — |

### 3.4 MMIO / hardware emulado (`PS2Memory`)

| Estado | Miembro | Clase | Notas |
|---|---|---|---|
| Registros I/O (DMAC canales D0-D9, INTC, timers EE, SIF regs mapeados…) | `m_ioRegisters` (mapa addr→u32) + `dma_regs[10]` + contadores de timer internos (`resetEeTimers/advanceEeTimers`) | A | El mapa completo se persiste tal cual. Timers EE: además del registro, persistir el acumulador interno que alimenta `advanceEeTimers` (ver UNKNOWN-2 si hay estado privado adicional). |
| VIF0/VIF1 | `vif0_regs`, `vif1_regs`, `m_vif1PendingPath2ImageQwc`, `m_vif1PendingPath2DirectHl`, `m_path3Masked`, `m_path3MaskedFifo` | A/B | En safe-point los FIFO/pendientes deben ser 0/vacíos (validar en SAVE; si no, abortar). Se serializan igualmente por robustez (vector de vectores de bytes). |
| GS privileged regs | `gs_regs` (`GSRegisters`: pmode/smode1/2/dispfb1/2/display1/2/bgcolor/csr atómico/imr/busdir/siglblid/vsyncTick…) | A | `csr` es atómico → copiar por valor. `vsyncTick` debe cuadrar con `m_vsyncTick` del scheduler (una sola fuente en el formato; LOAD escribe ambos). |
| Las cuatro colas | `m_pendingGifTransfers/Vif0/Vif1`, `m_completedDmacCauses` | D | Safe-point = vacías; SAVE valida y NO las serializa (formato registra que deben estar vacías). |
| TLB | `m_tlbEntries` | A | — |
| Contadores atómicos de diagnóstico (`m_dmaStartCount`, `m_gifCopyCount`, `m_gsWriteCount`, `m_vifWriteCount`), `m_seenGifCopy` | ídem | B(opc)/D | No afectan semántica guest observable conocida; se serializan por baratos (u64) para determinismo de trazas. `m_vu0/1CodeGeneration`: serializar (invalidación de caches VU derivadas). |
| Callbacks host | `m_gifPacketCallback`, `m_gifArbiter` (ptr), `m_vu1MscalCallback`, `m_vu1MscntCallback` | F→C | Rebind en LOAD por el mismo camino de inicialización del runtime (constructor/`PS2Runtime` init). |

### 3.5 GS

| Estado | Owner | Clase | Notas |
|---|---|---|---|
| Estado de dibujo guest-visible (contextos `m_ctx[2]`, PRIM/PRMODE/PRMODECONT, curR/G/B/A/Q/S/T/U/V/fog, PABE, SCANMSK, DIMX, DTHE, COLCLAMP, TEXA, TEXCLUT, BITBLTBUF, TRXPOS, TRXREG, TRXDIR, vertex queue `m_vtxQueue/m_vtxCount/m_vtxIndex`) | `GS` frontend (`gs_frontend.h:175-235`) | A | Todo POD/arrays. TRXDIR=3 (idle) esperado en safe-point (gsPartial=0), pero se serializa igual. |
| Estado de transferencia backend | `GSCpuBackend::m_transfer`, `m_transferState`, `m_localToHostBuffer/ReadPos` (`gs_cpu_backend.h:66-74`) | B/D | Safe-point: transferencia completa (`direction=3`) y buffer local→host vacío o consumido; validar en SAVE, serializar los PODs. |
| VRAM | vive en `PS2Memory::m_gsVRAM` (backend recibe puntero en `Initialize`) | A | ya contada en §3.3 |
| Presentación host (`m_hostPresentationFrame`, `m_displaySnapshot`, preferred-source latch, debug history, contadores nativos) | frontend | C/D | Derivados de VRAM+regs: se limpian en LOAD y el primer `latchHostPresentationFrame()` los regenera. El latch `m_preferredDisplaySource*` se resetea (D) — es heurística por-draw que el siguiente frame re-deriva. |
| Textura raylib/GL, ventana | `PS2Runtime::run()` (crea textura), raylib | F→C | Nunca en el checkpoint; el proceso destino ya los tiene creados. |
| `m_privRegs` (ptr a `PS2Memory::gs_regs`), mutexes | frontend | F→C | Rebinding en init normal. |

### 3.6 VU

| Estado | Owner | Clase | Notas |
|---|---|---|---|
| `VU1State` (vf/vi/acc/q/p/i/r/pc/mac/clip/status/cycles/ebit/halt flags/top/itop/branchPending/Target/Delay) ×2 unidades | `PS2Runtime::m_vu0`, `m_vu1` (`ps2_vu1.h:10-36`) | A | POD puro. Safe-point: VU no ejecuta como worker separado (interpretación síncrona), no hay hilo que quiescer. |
| Micro/data memories | en `PS2Memory` (§3.3) | A | — |
| Caches derivadas de análisis de microprograma (tablas `InstructionUsage` internas si se memoizan por generación) | `VU1Interpreter` internals + `m_vu0/1CodeGeneration` | C | Invalidar/reconstruir en LOAD; la generación serializada (§3.4) permite coherencia. |

### 3.7 IOP / HLE

| Estado | Owner | Clase | Notas |
|---|---|---|---|
| Subsistema IOP: perfil/proveedor activo, servicios y su estado mutable (p. ej. `cdmodule` `m_movie{active,id,lba,size,bytesConsumed}`) | `ps2x::iop::IopSubsystem::Impl` (pimpl), `cdmodule.cpp` | B + UNKNOWN-1 | El safe-point exige movie `alive=0`; los campos numéricos residuales son serializables. **Cada servicio del Impl requiere auditoría por-servicio** (UNKNOWN-1): el diseño exige añadir en DEVSTATE_003 una interfaz `serialize/deserialize` por servicio o certificar reset-safe por servicio. |
| SIF EE-side | `SIF.cpp`: `g_sifRegs`, `g_sifSregs`, `g_sifCmdHandlers`, `g_sifHeapAllocations`, `g_sifHeapStorage` (array 5 MiB región IOP heap), `g_sifCmdBuffer` | A | Todo datos planos (mapas u32→u32, storage de bytes). |
| RPC EE-side | `Syscalls/RPC.cpp`: `g_sif_modules_by_id` (módulos ligados), bindings/paquetes, seq de debug | A/B | Estructuras de ids/direcciones guest; validar en SAVE que no hay llamada RPC en curso (`rpcInv=0` + sin handler en pila — garantizado por el boundary). Historia de debug: D. |
| Transporte | `PS2IopTransport`/`PS2IopHostAdapter` (`ps2_iop_host.h`) | C | Síncrono, sin cola host propia (DEVSTATE_001A); recrear en init y rebind. |
| CDMODULE stream handle host | abre/cierra dentro del RPC (`cdmodule.cpp:359-393`) | F→D | Safe-point: ninguna lectura en vuelo. |

### 3.8 CD/DVD/ISO (EE-side stubs)

| Estado | Owner | Clase | Notas |
|---|---|---|---|
| Identidad del medio | `PS2X_CD_IMAGE` path + tamaño (`g_cdImageSizeBytes`) | E + hash en header | El checkpoint guarda hash/tamaño/nombre para validar; el ISO en sí no va dentro. |
| Streaming state | `Support.h`/`CD.cpp` (copia del TU de CD.cpp): `g_cdMode`, `g_cdStreamingLbn`, `g_cdStreamingEndLbn`, `g_cdInitialized`, `g_lastCdError`, `g_nextPseudoLbn`, `CdStreamTimingState g_cdStreamTiming` (active/paused/capacity/banks/rate/produced/consumed/remainder/lastVSyncTick) | A | `lastVSyncTick` es tick emulado (serializable); safe-point: `active=0`. |
| Índices de archivos loose/leaf (`g_cdFilesByKey`, `g_cdLeafIndex`, …) | Support.h (TU CD/FileIO) | C | Derivables del medio; reconstruir on-demand como en boot. |
| Handles host (`FILE*`, fstreams por lectura) | abiertos por operación | F→D | Ninguno persistente en safe-point. |

### 3.9 MPEG

| Estado | Owner | Clase | Notas |
|---|---|---|---|
| `g_mpeg_stub_state` (`MpegStubState`: `playbackByMpeg`, `callbacksByMpeg`, generaciones/contadores CD-stream, flags EOF) | `MPEG.cpp:574-595` | B/D | Safe-point certificado: `mpeg=0x0` = mapa de playback vacío. Política v1: SAVE exige mapa vacío y serializa solo los escalares de generación/contadores (`cdStreamGeneration`, `cdStreamBytesProduced/Demuxed`, flags EOF) para coherencia de una futura reproducción; LOAD parte con mapas vacíos. |
| Decoder FFmpeg (`MpegFfmpegDecoder`: AVCodecContext/parser/sws) | dentro de `MpegPlaybackState.decoder` | F→D | No existe en safe-point (0 playbacks). Nunca serializable. |
| Capturas/diagnóstico P42x | opt-in env | D | Fuera del checkpoint. |

### 3.10 Audio

| Estado | Owner | Clase | Notas |
|---|---|---|---|
| HLE guest-visible (`AudioStubState`: `voiceTransfers[2]` {src,dst,size,mode,completed}, `blockTransfers[2]` {base,size,pauseBase,offset,mode,active,loop}) | `Audio.cpp:21-50` | A | Safe-point: `audio=0x0` (completed=true, active=false), pero se serializa el struct entero (offsets/modos residuales pueden afectar la siguiente consulta de status). |
| Banco de samples decodificados | `PS2AudioBackend::m_sampleBank` (key→{pcm,int16,rate}), `m_loadOrderSamples/Keys`, `m_mostRecentSampleKey` (`ps2_audio.h:28-42`) | **B** | Política §10: se persiste LÓGICAMENTE (clave + PCM + rate + orden de carga). Es cache derivada de transfers VAG históricos cuyos bytes fuente NO están garantizados en RDRAM al momento del save, así que la reconstrucción desde memoria no es demostrable → persistir el banco es la única opción correcta-por-construcción. Tamaño: no acotado formalmente; en menú, decenas de samples (MiB bajos). |
| Reproducción en curso raylib/miniaudio (voces sonando), `Impl`, mutex, `m_audioReady` | backend | F→D | Política §10: descartar; ninguna finalización guest depende de un sonido host (DEVSTATE_001A). |

### 3.11 Memory Card

| Estado | Owner | Clase | Notas |
|---|---|---|---|
| Puertos (dir actual, formatted, rootPath), `nextFd`, `lastCmd`, `lastResult`, `cvFileCursor` | `MemoryCard.cpp`/`MemoryCard.h:21-40` | A/B | `rootPath` host se re-deriva de config (E); resto datos planos. |
| FDs abiertos (`g_mcFiles`) y `g_mcCommandPending` | `MemoryCard.cpp:88` | D | Safe-point: `mcOpen=0`, `mcPending=0` → SAVE valida y no serializa contenido de handles. v2 podría serializar path+offset si se quisiera safe-point más laxo. |
| Backing storage (archivos de MC en disco) | host filesystem | E(externo) | Fuera del checkpoint; el header registra la identidad/último write si se quiere detectar divergencia (política §15: WARN, no bloqueo). |

### 3.12 Input / Pad

| Estado | Owner | Clase | Notas |
|---|---|---|---|
| Estado de protocolo por puerto (`PadPortState`: open, analogMode, pressureEnabled, buttonMask, dmaAddr, reqState, readCount, lastReadDataAddr, `lastInput`, lastData) | `Pad.cpp:57-71` (`g_padPorts`) | A/B | Se serializa el estado de protocolo (open/analogMode/mask/reqState/dmaAddr/readCount). `lastInput`/`lastData`: serializar con política §13 de neutralización. |
| Entrada física actual (teclado/gamepad raylib) | host | F→C | Se muestrea de nuevo tras LOAD. |
| Override INPUT_001 / pad-file | diagnóstico | D | Nunca parte del checkpoint. |

### 3.13 Runtime host restante

| Estado | Owner | Clase |
|---|---|---|
| `m_eeExitHandlers` (registro por thread, direcciones guest) / `m_eeSyscallOverrides` / `m_eeSyscallMirrorAddresses` | `ps2_runtime.h:534-537` | A |
| Mutexes (`m_eeKernelStateMutex`, `m_gameHeapMutex`, …), hilos (GameThread, ejecutor), CVs | runtime | F→C |
| `m_debugUi*` callbacks, debug atomics (`m_debugPc/Ra/Sp/Gp`) | runtime | F→C / D |
| `m_boundRdram`, `m_boundGSVram`, punteros cruzados (`setGifArbiter`, `init(vram…)`) | runtime/GS/PS2Memory | F→C (rebinding en init) |
| Política missing-function, `m_stopRequested` | runtime | E/D |

## 6-7. Grafo de dependencias y orden de LOAD

Dependencias reales observadas: (a) el scheduler escribe en RDRAM (`writeGuestU32`, vsync flag/tick address) ⇒ memoria antes que scheduler; (b) los stubs leen `PS2Memory` y `runtime` ⇒ MMIO antes de tocar HLE; (c) `GSCpuBackend::Initialize(vram)` y `m_privRegs` apuntan a buffers de `PS2Memory` ⇒ VRAM/regs antes de re-init GS; (d) deadlines host se derivan de `m_eeCycle` + reloj actual ⇒ scheduler restaurado antes de recomputar deadlines; (e) presentación/audio host se derivan de estado ya cargado ⇒ al final; (f) nada del guest debe ejecutar hasta completar todo.

Orden propuesto (derivado, no genérico):

1. **Validate**: header (magic/version/juego/ELF hash/build-id/arch) + tabla de secciones + checksums, TODO antes de mutar nada (§14).
2. **Quiesce host**: detener ejecutor EE (o arrancar el proceso en modo "load pending" antes de `GameThread`→`EeScheduler::run()`, el punto que DEVSTATE_001 identificó tras la creación de objetos host y antes del guest, `ps2_runtime.cpp:2554-2585`), `stopAll()` de audio, sin present en curso (latch pausado).
3. **MEM**: RDRAM, scratchpad, IOP RAM, VU mems, GS VRAM, heaps/arenas, loadedModules.
4. **MMIO**: `m_ioRegisters`, `dma_regs`, `vif*_regs`, TLB, `gs_regs` (incl. csr/vsyncTick), path3/VIF pendientes, generaciones VU; colas cuatro = vacías (assert).
5. **GS state**: frontend PODs (contextos, prim, blit/trx, vertex queue); limpiar derivados host (snapshots/latch); `GSCpuBackend` re-`Initialize` sobre VRAM ya cargada + `m_transfer/State` restaurados.
6. **CPU/VU**: `m_cpuContext`, `VU1State`×2.
7. **SCHED**: threads (contextos incluidos), ready queues, sem/eventflags/alarms/handlers, ids/counters, pendingInvocations (sin callables), `m_eeCycle`, `m_vsyncTick` (+coherencia con gs_regs), direcciones vsync; **recomputar** `m_deadlines`/`m_nextDeadlineCycle` con `scheduleEvent` desde ciclos restantes y reprogramar VBlank.
8. **IOP/HLE**: SIF (regs/sregs/handlers/heap), RPC bindings/modules, IopSubsystem por-servicio (interfaz nueva, UNKNOWN-1), CD stream state, MC state, Audio HLE structs, MPEG escalares, Pad protocol state.
9. **Rebind host**: `setGifArbiter`, gif/vu callbacks de PS2Memory, transporte IOP, `m_privRegs`, punteros bound.
10. **Reconstruct derivados**: audio `m_sampleBank` desde sección AUDIO (repoblar mapa+orden), caches VU invalidadas, primera presentación forzada (latch) desde VRAM.
11. **Input reconcile**: política §13 (neutralizar edges).
12. **Resume**: arrancar/reanudar `EeScheduler::run()`; la primera iteración entra por el mismo boundary del save.

Callbacks que NO sobreviven: todos los `std::function` (wait/resume/onComplete — garantizados ausentes), callbacks de PS2Memory (rebind), debug UI (proceso destino decide). Caches que se invalidan: presentación GS, displaySnapshot, preferred latch, análisis VU, índices CD loose (lazy), historia debug.

## 8. Formato lógico

Header conceptual: `magic "DMCK"`, `format_version`, `game_id "SLES_503.58"`, `elf_sha256`, `iso_identity {path_hint, size, sha256 opcional-costoso o hash de cabecera}`, `runtime_build_id` (hash del exe o versión de esquema de structs), `arch "x86_64-le"`, `section_table[{id, offset, size, crc32}]`, `saved_at`, `safe_point_id "MAIN_MENU_V1"`.

Secciones (owner → contenido → save/restore → dependencias → tamaño → versionado):

| Sección | Contenido | Owner | Tamaño | Nota |
|---|---|---|---|---|
| CORE | `m_cpuContext`, VU1State×2 | runtime | ~4 KiB | struct-version-tag propio |
| MEM | RDRAM+SPR+IOP+VUmem+VRAM+arenas+modules | PS2Memory/runtime | ~38 MiB (sin comprimir) | por-región subsecciones |
| MMIO | ioRegisters/dma/vif/tlb/gs_regs/etc. | PS2Memory | <64 KiB | mapa serializado ordenado por clave (determinismo) |
| GS | frontend PODs + backend transfer | GS/GSCpuBackend | <8 KiB | — |
| SCHED | threads/queues/sem/flags/alarms/handlers/ids/invocations/cycles/vsync | EeScheduler | ~ nº threads × ctx | invariantes: sin callables |
| IOP | subsystem por-servicio (TLV por servicio con id de servicio) | IopSubsystem | pequeño | permite sección por servicio versionada (UNKNOWN-1) |
| SIF_RPC | g_sif* + módulos RPC | SIF.cpp/RPC.cpp | heap storage ≈5 MiB + mapas | heap comprimible |
| CD | streaming+timing state | CD.cpp | <1 KiB | — |
| MC | ports/nextFd/lastCmd (mcOpen==0 asserted) | MemoryCard.cpp | <1 KiB | — |
| AUDIO | AudioStubState + sample bank lógico (key,rate,pcm[]) + load order | Audio.cpp + PS2AudioBackend | MiB bajos, unbounded formal | v1 admite truncar nunca: correctness first |
| PAD | PadPortState×2 saneado | Pad.cpp | <1 KiB | — |
| MPEG | escalares de generación/flags (mapas vacíos asserted) | MPEG.cpp | <1 KiB | — |

Prioridad correctness: sin compresión en v1 (~45 MiB), TLV por sección, todo little-endian nativo con build-id que ata el layout de structs (los PODs se vuelcan por campo nombrado o por bloque + build-id estricto; v1: por bloque + build-id estricto, v2: por campo para tolerancia entre builds).

## 9. Política host-only graphics

Nunca serializar: textura raylib, GL ids, `m_hostPresentationFrame`, `m_displaySnapshot`, debug history, preferred-source latch. LOAD: limpiar todos, re-`Initialize` backend sobre VRAM restaurada, forzar un `latchHostPresentationFrame()` antes de mostrar (evita frame basura persistente: el primer present ya sale de VRAM+DISPFB restaurados). El snapshot-backend thread_local de `Present` no guarda estado entre frames (se re-inicializa por llamada) — nada que restaurar.

## 10. Política host-only audio

Descartar en LOAD: voces sonando, `Impl` de dispositivo, `m_audioReady` (lo fija el init del proceso). Persistir (B): `m_sampleBank` completo (key→pcm/rate), `m_loadOrderSamples/Keys`, `m_mostRecentSampleKey` — así el próximo `play()` guest encuentra el banco idéntico y no hay "mudo permanente" ni re-decodificación imposible (las fuentes VAG no están garantizadas en RDRAM). Evitar finalización falsa/perdida: el HLE guest-visible (`voiceTransfers.completed`, `blockTransfers.active/offset`) viaja en AUDIO y el safe-point garantiza que no hay transferencia a medias; ningún completion guest depende de reproducción host (DEVSTATE_001A §Audio). Sonidos "antiguos" no pueden reproducirse: no se serializa ninguna voz activa.

## 11. Política RPC/SIF

Estado plano (g_sifRegs/Sregs/CmdHandlers/Heap*/CmdBuffer, módulos RPC ligados) = SERIALIZE_EXACT. Precondición: `rpcInv=0` y ausencia de llamada HLE en ejecución (el boundary lo garantiza: los handlers corren síncronos dentro del despacho). Transporte y adapter: recrear + rebind. La historia de debug RPC: descartar. Auditoría exhaustiva de globals privados restantes de RPC/SIF sigue siendo el caveat heredado → UNKNOWN-2 con plan (grep sistemático de `g_` y `static` mutables en Syscalls/Stubs en DEVSTATE_003 con checklist por TU).

## 12. Política CD/MPEG/MC

CD: serializar el estado de streaming/timing (todo emulado por ticks, sin tiempo host); índices de disco se reconstruyen lazy; identidad del ISO validada por header (mismatch ⇒ rechazo). MPEG: v1 exige mapas vacíos (safe-point los certifica); solo escalares de generación; decoder jamás; al cargar, cualquier PSS futuro arranca limpio por el camino normal `sceMpegInit`. MC: v1 exige `mcOpen=0 && mcPending=0` (certificado); se serializa el estado de puertos/dirs/ids; el backing store es externo — divergencia detectada se reporta como WARN y el guest verá el contenido actual del medio (coherente con una MC física intercambiable).

## 13. Política Pad/input

Serializar protocolo (open/analogMode/pressure/buttonMask/reqState/dmaAddr/readCount). En LOAD, `lastInput.buttons` se fija a 0xFFFF (sin botones) y lastData al patrón neutro correspondiente antes de reanudar: el primer sondeo tras load entrega "nada pulsado" y elimina botones fantasma edge-triggered; el estado físico actual se incorpora en el siguiente sample real. El override de diagnóstico y el estado físico del mando quedan fuera del checkpoint.

## 14. Atomicidad / error handling

SAVE: (1) en el boundary, validar TODAS las precondiciones del §2 (las mismas lecturas de la sonda DEVSTATE_001A, convertidas en asserts de save); si falla una → no se escribe nada y se reporta la causa; (2) serializar a staging en memoria/archivo temporal `*.tmp`; (3) checksums por sección + header al final; (4) rename atómico al nombre final. Nunca escribir in-place. LOAD: (1) validar header+tabla+checksums completos ANTES de mutar; (2) deserializar a staging structs (donde sea barato) o, para MEM, escribir directo SOLO tras pasar toda la validación estructural, con el guest parado desde antes; (3) cualquier fallo ⇒ fail-fast con el proceso aún sin guest ejecutado (el punto de carga está antes de `EeScheduler::run()`, así que "abortar y salir/reintentar" nunca deja un guest a medias); (4) el scheduler solo se lanza tras completar los pasos 3-11 del §7. No hay rollback parcial: el modelo es "cargar en un runtime aún no arrancado"; si se quisiera load en caliente, se derivaría a "reset completo + load frío" (política v1).

## 15. Compatibilidad / versionado

- Otro juego / otro ELF hash: RECHAZAR (error claro).
- Otra `format_version` mayor: RECHAZAR; menor con secciones conocidas: permitido si el build-id coincide.
- Build incompatible (`runtime_build_id` distinto): RECHAZAR en v1 (los PODs van por bloque); v2 podría relajar con serialización por campo.
- Truncado/corrupto (checksum): RECHAZAR antes de mutar.
- Sección desconocida: RECHAZAR en v1 (conservador) — un checkpoint con sección extra implica build más nuevo.
- Checkpoint antiguo sin sección nueva: política por sección declarada en tabla (required/optional); v1: todas required ⇒ RECHAZAR y pedir re-save. Migraciones: fuera de alcance, la política es "un formato por build certificado".
- MC backing divergente: WARN + continuar (medio externo).

## 16. Plan de validación (para DEVSTATE_003+)

- TEST A (ciclo frío completo): save en Main Menu idle → cerrar proceso → proceso nuevo → load → PASS si: primer present muestra el menú (no negro/basura >1 frame), navegación Down/Up mueve la selección, sonido de menú suena. FAIL si crash, deadlock (>5 s sin present), imagen basura persistente o input muerto.
- TEST B (estado fino): mover selección al ítem k → save → load → PASS si la selección visible y su reflejo en RDRAM (dirección del cursor de menú a determinar con la sonda de lectura) es k.
- TEST C (determinismo funcional): load → navegar una secuencia fija → volver a idle → PASS si la sonda DEVSTATE (reutilizada como oráculo de igualdad) muestra la misma firma de gates y el mismo PC estacionado.
- TEST D (determinismo fuerte): save → correr N=600 ticks sin input registrando por tick {pc, padRead, hash de una región RDRAM candidata estable} → load → repetir → PASS si las series coinciden; se acepta divergencia SOLO en campos declarados no deterministas (documentarlos si aparecen; attract-mode timer contará ticks igual). 
- TEST E (idempotencia): save→load→save→load; PASS si el segundo checkpoint es byte-idéntico al primero fuera de campos de timestamp del header (comparación con máscara).
- TEST F (rechazos): corromper 1 byte de cada sección / cambiar elf_hash / truncar → PASS si cada caso se rechaza antes de mutar estado y el proceso puede seguir arrancando en frío.
- TEST G (AV post-load): PASS si no hay: audio duplicado (dos BGM), mute permanente (>10 s sin ningún sample tras interacción que debería sonar), sonidos de antes del save reproduciéndose solos, frame basura persistente (>2 presents), jitter/artefactos nuevos respecto al baseline.

## 16-bis. Actualización DEVSTATE_003A (cierre de UNKNOWNs)

- **UNKNOWN-1: CLOSED.** Inventario por-servicio completo del `IopSubsystem`
  (ps2xIOP/src/modules): `cdmodule` → `m_movie` POD (B); `clfile` →
  `m_fileHandles` (map u32→`ClFileHandle{handle u64, size, position}` — el
  u64 es un handle del provider host, SERIALIZE_LOGICAL por
  nombre+posición o exigir vacío), `m_loads` (map u32→`ClFileLoad{status,size}`
  A), `m_nextFileHandle/m_nextLoadHandle` (A), `m_root` (E); `cri_dtx` → 5
  maps de PODs por handle + contadores (A/B); `dbcman`/`mcserv`/`sdrdrv` →
  solo contadores de log (D); `libsd` → stateless (D/E; el estado real vive
  en Audio.cpp EE-side); `sound_update_stub` → `m_updateCounter`,
  `m_completedStreamCount` (A); `tsnddrv` → `State` POD de direcciones
  IOP-RAM (A; los datos apuntados van en MEM). Interfaz propuesta por
  servicio: `serializeState(ByteWriter&)`, `deserializeState(ByteReader&)`,
  `validateCheckpointSafe()` sobre `IopService` (siguiendo el patrón ya
  existente `appendDebugMetrics`). Residual menor: confirmar la semántica de
  reapertura del u64 handle del provider en clfile antes de codificar su
  serializer (nota de implementación de la sección IOP, no del formato).
- **UNKNOWN-2: CLOSED.** Timers EE: `PS2Memory::EeTimer{count,mode,compare,
  hold,clockRemainder}` ×4 en `m_eeTimers` (ps2_memory.h:442-451) — estado
  REAL fuera de `m_ioRegisters`, con acumulador fraccional; incorporado a la
  sección MMIO. Barrido de globals mutables EE-side no inventariados antes:
  `Syscalls/Helpers/State.h` → `inline g_fileDescriptors` (fd→`FILE*`,
  FORBIDDEN; nuevo gate `fioOpen==0`) + `inline g_nextFd` (A);
  `Syscalls/RPC.cpp` → `g_rpc_active_queue` (u32 guest ptr, A; el resto de la
  cola vive en RDRAM), `g_sif_modules_by_id` (A), ring de debug (D);
  `Syscalls/Deci2.cpp` → `g_deci2Sessions` map de PODs + `g_nextDeci2Socket`
  (A); `Stubs/DMA.cpp` → `g_dmaCurrentEnv` (A); `Stubs/GS.cpp` → `g_gparam`
  (A, copia viva del TU GS.cpp); `Support.h` → `g_iopHeapNext` (A, TU
  autoritativo el que lo muta). Hallazgo arquitectónico: `Support.h` declara
  estado en anon-namespace POR TU (una copia por unidad de traducción); el
  serializer de cada dato debe definirse EN el TU autoritativo (CD state →
  CD.cpp, gparam → GS.cpp), exactamente como se hizo con las probes. Los
  `static` locales de función restantes son contadores de log (D).
- **UNKNOWN-3: CLOSED.** Lectura línea a línea de `ee_scheduler.h:40-160`:
  `EeSemaphore{id,count,maxCount,initCount,attr,option,waiters:deque<int>}` y
  `EeEventFlag{id,attr,option,initBits,bits,waiters:deque<int>}` no contienen
  ningún callable; los payloads de wait (`EeSemaphoreWait`, `EeEventFlagWait`,
  `EeVSyncWait`, `EeExternalWait`) son PODs puros. El ÚNICO callable de la
  cadena de espera es `EeWaitState::completion` (h:81) y el de invocaciones
  `GuestInvocation::onComplete` (h:101) — ambos ya cubiertos por los gates
  `waitFn=0`/`pendingInvFn=0`/`invocationFn=0`. `EeAlarm`/`EeIrqHandler`
  guardan solo direcciones guest (serializables), confirmando su clase B/A.

## 17. UNKNOWN / BLOCKERS reales

Estado actualizado en DEVSTATE_003A/003B (la redacción original se conserva
en el historial de este archivo y en §16-bis):

- UNKNOWN-1 (inventario por-servicio de `IopSubsystem::Impl`): **CLOSED**
  (ver §16-bis y DEVSTATE_003A §1; residual de implementación: semántica de
  reapertura del handle u64 de clfile, a resolver al codificar la sección IOP).
- UNKNOWN-2 (timers EE + barrido de `static`/`g_` mutables): **CLOSED**
  (§16-bis y DEVSTATE_003A §2).
- UNKNOWN-3 (callables en `EeSemaphore`/`EeEventFlagWait`): **CLOSED**
  (§16-bis y DEVSTATE_003A §3 — no contienen callables).
- UNKNOWN-4 (solo para TEST B): dirección RDRAM del cursor de menú —
  **OPEN**, se obtendrá con una sonda de lectura puntual cuando se implemente
  ese test; no afecta al formato ni al orden de restauración.

## 18. Estimación de complejidad (basada en el código inspeccionado)

- Secciones triviales (volcado de POD/regiones): CORE, MEM, MMIO, GS, CD, MC, PAD, MPEG — mecánicas; el coste real es el arnés de formato+checksums (~1 sesión de implementación cuidadosa cada bloque grande).
- SCHED: media — reconstrucción de deadlines y validaciones de invariantes (sin callables, coherencia vsyncTick) requieren tests dirigidos.
- IOP/SIF_RPC: media-alta por UNKNOWN-1/2 (auditoría por servicio antes de codificar).
- AUDIO: media — persistir el banco es directo; la validación AV (TEST G) es lo laborioso.
- Punto de integración: el "load antes de arrancar guest" encaja con el flujo actual de `PS2Runtime::run()` sin refactor grande (gancho entre creación de objetos host y `EeScheduler::reset()/run()`, evitando que el reset pise lo cargado — este ordering es el único cambio delicado del arranque).

## 19. Recomendación

**GO** para DEVSTATE_003 (implementación incremental), con este orden: (1) cerrar UNKNOWN-2/3 con el barrido estático; (2) definir la interfaz por-servicio IOP (UNKNOWN-1); (3) implementar header+MEM+CORE+MMIO+asserts de safe-point y el TEST F; (4) SCHED+GS; (5) IOP/SIF/AUDIO/resto; (6) TESTs A-E/G.

## Resultado

FILES_CHANGED: solo `analysis/notes/DEVSTATE_002_CHECKPOINT_SERIALIZATION_DESIGN.md` (este documento). Producción intacta.

PRIMARY_CLASSIFICATION: DEVSTATE_SERIALIZER_DESIGN_READY

NO_COMMIT. NO_PUSH.
