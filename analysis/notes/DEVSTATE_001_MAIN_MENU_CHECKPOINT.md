# DEVSTATE_001 — Main Menu development checkpoint (architecture audit)

Estado: **NO READY**. Esta nota es la auditoría de fase 1 y el gate de fase 2. No existe aún formato ni código de save/load. No se creó un archivo `.dmcstate`.

## Baseline y alcance

- HECHO: HEAD raíz al iniciar la fase: `95af5dce02f0125781196fc265934caa28a27a10`; producción validada: `88f22334170a67de70d71e67d81f4a7e7adb572c`. `upstream.lock.json` fija `patched_commit=fd7f2b4358689cbbc46e98ce1c78e3b8b2478963`.
- HECHO: `git status --short` inicial mostraba solo `?? .claude/` en la raíz; el checkout de vendor seguía con los cambios ya existentes de MPEG, GS y Pad. No se ha modificado producción, ejecutado build, creado checkpoint, hecho commit ni push en esta fase.
- HECHO (validación previa del usuario, no una captura nueva): Memory Card, Language Select, movie y Main Menu eran visibles; no había bandas graves y el jitter vertical estaba ausente o era imperceptible. Ver `BLOCKER_004_PRE_PERF_KNOWN_GOOD_CHECKPOINT.md`.
- HECHO: `EeScheduler::checkpointDue()` es un corte de despacho/tiempo, **no** una operación de guardado de estado (`ee_scheduler.cpp:368`). `EeScheduler::snapshot()`/`publishSnapshot()` solo publican parte del estado para depuración (`ee_scheduler.cpp:1499-1555`). No hay serializer de estado completo reutilizable.

## Matriz de estado (fase 1)

Las clases son obligaciones de diseño, no afirmaciones de que un punto Main Menu ya cumpla la condición. `UNKNOWN` impide guardar hasta resolverlo.

| Componente | Clase | Evidencia y criterio |
|---|---|---|
| EE `R5900Context` principal y de cada `GuestThread` | MUST_SERIALIZE | `ee_scheduler.h:104-128`; `PS2Runtime::m_cpuContext` en `ps2_runtime.h:520-530`. El contexto activo puede ser el de una invocación, no el contexto base. |
| Scheduler: estados de threads, ready queues, prioridades, ID allocators, semáforos, event flags, IRQ handlers, alarmas, clocks y deadlines guest | MUST_SERIALIZE | `ee_scheduler.h:395-444`. `EeKernelSnapshot` omite colas, payloads de wait, alarmas, handlers, invocaciones y secuencias. Los `hostDeadline` deben reconstruirse a partir del tiempo emulado restante, nunca copiarse como valor absoluto. |
| Invocaciones guest, `resumeCompletion`, `EeWaitState::completion`, callback `onComplete` | MUST_BE_QUIESCENT / UNKNOWN | `ee_scheduler.h:75-128,434`: contienen `std::function` no serializable. Hay que probar que no están pendientes, o diseñar una codificación semántica específica que no guarde punteros. Estar entre dos llamadas guest no demuestra que estén vacías. |
| Eventos EE pendientes y `m_pendingEeTimerInterrupts` | MUST_SERIALIZE | `ee_scheduler.h:420,432-438`; pueden alterar la primera transición restaurada. Mutex/CV/host thread: HOST_ONLY_DO_NOT_SERIALIZE. |
| RDRAM (32 MiB), scratchpad (16 KiB) | MUST_SERIALIZE | `ps2_memory.h:26-31,364-367`; ambos son memoria guest visible. |
| IOP RAM (2 MiB), VU0/1 code+data (4/4/16/16 KiB) | MUST_SERIALIZE | `ps2_memory.cpp:351-380`; `ps2_memory.h:39-51,370,405-408`. La RAM IOP existe aunque no se emule CPU IOP. |
| MMIO, TLB, EE timers, VIF/DMA registers, FIFO GIF/VIF, DMA completions, VU execution state | MUST_SERIALIZE / MUST_BE_QUIESCENT | `ps2_memory.h:380-451`; `ps2_vu1.h:203-241`; `ps2_gif_arbiter.h:17-37`. Registros y estado guest persistente se guardan; transferencias parciales/FIFO deben estar vacíos o representarse explícitamente. Los punteros/caches VU se reconstruyen. |
| GS VRAM (4 MiB), privileged registers, drawing contexts and transfer state | MUST_SERIALIZE / MUST_BE_QUIESCENT | `ps2_memory.h:51,382-385`; `gs_frontend.h:180-215`; `gs_cpu_backend.h:71-74`. Transferencia de imagen parcial debe estar inactiva o serializada semánticamente. |
| Textura raylib, OpenGL, frame host de presentación y debug history | HOST_ONLY_DO_NOT_SERIALIZE / CAN_RECONSTRUCT | `PS2Runtime::run()` crea textura nueva (`ps2_runtime.cpp:2573-2576`); presentación se regenera desde GS/VRAM. No copiar handles ni punteros. |
| IOP CPU, threads y módulos IRX ejecutándose | CAN_RECONSTRUCT / NOT_PRESENT | `analysis/UPSTREAM.md` documenta que `ps2xIOP` es HLE RPC/DMA y no ejecuta R3000A/IRX. No inventar estado CPU IOP. Sí existe estado de módulos HLE y registros guest/SIF. |
| Perfil IOP, rutas SID, servicios HLE y estado CDMODULE movie | MUST_SERIALIZE / MUST_BE_QUIESCENT | `iop_subsystem.cpp:60-125`; `cdmodule.cpp:64,107-115,543-546`. La ruta/identidad puede reconstruirse al cargar el mismo ELF; estado mutable de servicios no queda restaurado por `reset()`. Para v1, movie `active` debe ser falso. Otros servicios (por ejemplo CLFILE con handles) requieren auditoría por servicio. |
| RPC: módulos cargados, bindings/paquetes, callbacks, SIF registros, heap y transporte | MUST_SERIALIZE / MUST_BE_QUIESCENT | `Syscalls/RPC.cpp`; `Stubs/SIF.cpp:57-69`; `ClFileService` en `clfile.cpp:591-599`. La sesión RPC en curso debe estar ausente. No copiar `FILE*` ni handles: reabrir con rutas y offsets si hay ficheros activos, o exigir cero handles. |
| VSync tick, EE cycles, slice/deadline counters y timers | MUST_SERIALIZE | `ee_scheduler.h:420-443`; VBlank se reprograma en `ee_scheduler.cpp:152,1863-1870`. Restaurar a cero cambiaría waits y pacing visibles al guest. |
| CD HLE: streaming/timing, LBN, índices y solicitudes | MUST_SERIALIZE / MUST_BE_QUIESCENT | `Stubs/CD.cpp:16-32,175-192`. Estado de stream/límites es mutable; solicitud parcial debe estar ausente. Índices de disco podrían reconstruirse de la misma ISO, pero su equivalencia requiere hash/identidad de medio. |
| Memory card HLE: puertos, directorios, FDs y comandos pendientes | MUST_SERIALIZE / MUST_BE_QUIESCENT | `Stubs/MemoryCard.cpp:69-94`. `FILE*` es host-only; fd guest, path y offset serían necesarios si abierto. Para v1 se puede exigir cero FDs y `g_mcCommandPending=false`; directorios/resultado deben persistirse si afectan la siguiente llamada. Medio externo puede cambiar entre procesos. |
| MPEG state, decoded frames, FFmpeg decoder | MUST_BE_QUIESCENT / CAN_RECONSTRUCT | `Stubs/MPEG.cpp:501-591`; `MpegDemuxTransaction::active`, playback map y callbacks. v1 debe rechazar reproducción, demux y callback pendientes; crear decoder idle nuevo. El puntero AVCodecContext nunca va al fichero. |
| Audio guest/HLE: transferencias, banco de samples, comandos/estado de voz | UNKNOWN / MUST_SERIALIZE | `Stubs/Audio.cpp:49-50`; `ps2_audio.h:26-42`. No está demostrado que resetear banco/transferencias conserve la transición NEW GAME. Handles de dispositivo/voice raylib: HOST_ONLY_DO_NOT_SERIALIZE. |
| Pad: estado de puerto guest, entrada física host | MUST_SERIALIZE / CAN_RECONSTRUCT | `Stubs/Pad.cpp:48-79`. Estado guest de protocolo/puerto puede importar; entrada física se muestrea de nuevo. El override temporal INPUT_001 no es parte del checkpoint. |
| Heap guest HLE, arena interna, exit handlers, syscall overrides, módulos cargados | MUST_SERIALIZE | `ps2_runtime.h:485-572`. Los allocators y tablas de funciones/módulos condicionan ejecución después de restaurar. Punteros bound se reconstruyen. |
| Host threads, mutex/CV, audio device, `FILE*`, renderer/FFmpeg pointers | HOST_ONLY_DO_NOT_SERIALIZE | Objetos de proceso; el runtime los crea normalmente. Guardar su representación binaria sería inválido. |

## Gate de safe point (fase 2)

- HECHO: `PS2Runtime::run()` reinicia SIF, IOP, audio, MPEG y kernel EE, y después el GameThread llama `EeScheduler::reset()` seguido de `run()` (`ps2_runtime.cpp:2554-2585`). Una carga futura necesitará un punto de restauración *después* de crear objetos host y *antes* de iniciar el guest, sin volver a resetear los datos cargados.
- HECHO: `EeScheduler::run()` procesa eventos, selecciona threads, ejecuta `resumeCompletion`, maneja invocaciones y despacha funciones guest (`ee_scheduler.cpp:156-321`). Por tanto el punto propuesto debe estar en el hilo EE entre despachos, no en el hilo de presentación. Un VBlank no constituye por sí solo un punto seguro.
- UNKNOWN: no hay medición atómica del Main Menu de PC/thread, ready queues, waits con payload/completion, invocaciones por thread, cola de eventos, RPC/CD/MPEG/DMA/VU/audio. El snapshot público no contiene esos campos (`EeScheduler::publishSnapshot`, `ee_scheduler.cpp:1499-1555`). Tampoco se ha identificado un predicado comprobado que distinga Main Menu visible de transición de movie o attract mode.
- INFERENCIA: la interfaz actual no permite demostrar las condiciones de quiescencia requeridas sin agregar primero diagnósticos acotados y lecturas seguras de los subsistemas. Una captura de RDRAM+VRAM desde el hilo de render sería una savestate asíncrona insegura.

Condición para levantar el gate: instrumentar temporalmente el límite entre iteraciones del scheduler, identificar Main Menu con evidencia guest/GS, y registrar **en el mismo límite** PC+thread, colas/waits/invocaciones, eventos/alarma, RPC/CD/MPEG, GIF/VIF/DMA/VU y audio. Confirmar estabilidad en varias VSync consecutivas, sin trabajo no serializable pendiente, y definir la política de estado persistente de cada servicio HLE. Solo entonces diseñar formato versionado, hooks de save/load y prueba fría/restaurada de NEW GAME.

### Mediciones exigidas y todavía no disponibles

| Campo | Resultado |
|---|---|
| SAFEPOINT_PC / thread | UNKNOWN |
| Scheduler queue sizes / waits / GuestInvocation | UNKNOWN |
| Active RPC / CD request | UNKNOWN |
| Active MPEG playback / demux / guest callback | UNKNOWN |
| Active DMA/GIF/VIF/VU | UNKNOWN |
| Audio guest-visible state/equivalence | UNKNOWN |

## DEVSTATE_001 RESULT

SAFEPOINT_PROVEN: NO

CHECKPOINT_FORMAT_VERSION: NOT_DEFINED

SERIALIZED_COMPONENTS: NONE

RECONSTRUCTED_COMPONENTS: NONE (design only: renderer, audio device, file handles, decoder)

QUIESCENT_REQUIRED_COMPONENTS: guest invocation/completion, RPC, CD request, MPEG playback/demux/callback, partial DMA/GIF/VIF/GS transfer, host decoder ownership

UNSUPPORTED_ACTIVE_COMPONENTS: all above for v1 until a safe-point audit proves absence

CHECKPOINT_SIZE: NOT_CREATED

COLD_BOOT_TO_MENU: >60 s prior observation; not remeasured in this phase

RESTORE_TO_MENU: NOT_MEASURED

RESTORE_SPEEDUP: NOT_MEASURED

MAIN_MENU_VISUALLY_CORRECT: YES (prior user validation; no new run)

INPUT_AFTER_RESTORE: NOT_TESTED

VSYNC_AFTER_RESTORE: NOT_TESTED

CD_AFTER_RESTORE: NOT_TESTED

IOP_AFTER_RESTORE: NOT_TESTED

GS_AFTER_RESTORE: NOT_TESTED

COLD_NEW_GAME_BEHAVIOR: NOT_MEASURED_IN_THIS_PHASE

RESTORED_NEW_GAME_BEHAVIOR: NOT_AVAILABLE

COLD_VS_RESTORED_EQUIVALENCE: NOT_TESTED

PRIMARY_CLASSIFICATION: DEVSTATE_001_NOT_READY

NEXT: prove Main Menu safe point and HLE quiescence before file format or save/load; do not begin GAMEPLAY_001 via checkpoint

CHECKPOINT_DECISION: NO_COMMIT

PUSH: NO
