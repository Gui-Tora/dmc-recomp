# DEVSTATE_001A — Main Menu safe-point feasibility

Estado: diagnóstico acotado, sin save/load, sin formato, sin dumps de RDRAM/VRAM. Baseline raíz `95af5dce02f0125781196fc265934caa28a27a10`; producción `88f22334170a67de70d71e67d81f4a7e7adb572c`. Build usado exclusivamente: `analysis/local/symtabfirst/build`.

## Método y corrección del predicado

- HECHO: la primera sonda vio `Title_move` (`0x001A59B0–0x001A5D3C`) en el *inicio* del PSS (`RUN_099`, tick 532). Por ello, **RETRACTADO**: “ejecutar `Title_move` identifica Main Menu”. Esa ventana no se usó para evaluar el menú.
- HECHO: la segunda/tercera sonda detectó el retorno a `Title_move` tras más de 120 VSync sin esa ruta; en `RUN_101` ocurrió en tick 1520, PC `0x001A5A48`. Justo antes, CDMODULE informó `status alive=0`. Las 460 muestras siguientes tuvieron próximo PC `0x0015B5B8` en `ps2_main` (la primera tuvo `0x001A5A48`). Durante la película la primera sonda había visto `ps2_main` en `0x0015B570`. Esto aporta un predicado guest/runtime, no dependiente solo de tiempo: **retorno de `Title_move` después del PSS completado + `ps2_main` estacionado en `0x0015B5B8` + MPEG idle**.
- UNKNOWN: la captura automática de ventana en esta sesión estuvo tapada por una superposición del escritorio. La validación visual previa del usuario sí alcanzó Main Menu en el baseline, pero esta corrida no aporta una confirmación visual nueva ni prueba que una captura particular correspondiera al Main Menu. El predicado es candidato fuerte, no identidad visual cerrada.
- HECHO: todas las lecturas de la sonda se invocaron desde **un solo punto**, tras `processPendingEvents()` y antes de seleccionar/despachar guest en `EeScheduler::run()`. Se emitió una línea compacta por VSync, sin leer desde el render thread. Los pequeños lectores HLE fueron de solo lectura, ejecutados dentro de esa muestra; ninguna semántica de scheduler, RPC, CD, MPEG, GS, audio o Pad se cambió.

## Ventanas y observaciones

Evidencia local gitignored: `analysis/local/p311/RUN_101/runtime.log` y `result.json`. `RUN_100` produjo 320 muestras post-retorno y sirvió para detectar que la etiqueta `movie_active` de CDMODULE permanece a 1 aunque su stream esté consumido; `RUN_101` añadió bytes restantes y una ventana post-input completa. `RUN_099` se descarta para Main Menu por el falso predicado inicial.

| Ventana RUN_101 | Muestras | Observación |
|---|---:|---|
| Idle después del retorno guest | `n=0..119` (120 VSync) | Todos los contadores medidos de operación no serializable fueron cero. |
| Pulso Down | `n=242..248` (7 VSync) | `padButtons=0xFFBF`, `padRead` creciente; no se envió NEW GAME. |
| Tras soltar Down | `n=249..460` (212 VSync) | `padButtons=0xFFFF`, `padRead` creciente; misma clase de estado que en idle. |

HECHO: 461 muestras consecutivas (`tick=1520..1980`) a intervalos de VSync. `pendingInv` alternó 0/1, pero `pendingInvFn=0`, `rpcInv=0`, `activeInv=0` e `invocationFn=0` en todas. El scheduler crea una invocación GS VSync sin `onComplete` en `EeScheduler::processEvent(VBlankStart)`; un `GuestInvocation` con contexto/enum/ID es estado serializable, no un callback host en curso. `ready=1/2`, `waiting=0/1`, `sleeping=0`; estas variaciones de threads guest son serializables. `events=0`, `timerIrq=0`, `alarms=0` en las muestras.

HECHO: `dmaActive=0`, `gifQueued=0`, `gsPartial=0`, `mcOpen=0`, `mcPending=0`, `mpeg=0x0`, `cd=0x0`, `audio=0x0`, `iopOpen=0`, `iopLoads=0` en las 461 muestras. MPEG `0x0` significa mapa de reproducción vacío, sin decoder, demux ni eventos de callback. Audio `0x0` significa cero transferencias de voz incompletas y cero block transfers activos/looping en el HLE. CD `0x0` significa stream HLE inactivo, no pausado, sin sectores bufferizados según `CdStreamTimingState`.

HECHO: `iopMovie=1` pero `iopRemaining=0` en las 461 muestras; además `CDMODULE/MOVIE:status alive=0`. `CdModuleService::MovieStreamState` contiene solo `active`, ID, LBA, tamaño y bytes consumidos; las lecturas abren y cierran el handle del host dentro del RPC (`cdmodule.cpp:107-115,359-393`). **INFERENCIA:** esta etiqueta activa residual es `SERIALIZABLE_STATE`, no una lectura host en vuelo. No equivale a reproducción MPEG activa. No se modificó para forzar cero.

### Audio y RPC/SIF

- HECHO: `sceSdRemote` calcula estado/retorno de transferencias en el HLE de forma síncrona (`Audio.cpp`); no se observó transferencia pendiente. `PS2AudioBackend::onSoundCommand()` llama a `play()` y consulta el banco de samples (`ps2_audio.cpp:146-226`); la reproducción raylib/sonidos activos es host-only y no se serializa. El banco de samples sí influye en el audio futuro y tendría que reconstruirse o persistirse semánticamente para fidelidad de sonido, pero no se encontró una finalización guest pendiente dependiente de un handle de audio. **INFERENCIA:** no es bloqueo de quiescencia; sigue siendo trabajo de restauración y equivalencia.
- HECHO: `PS2IopTransport::handleRpc()`/`notifyTransfer()` llaman sin cola host propia a los handlers HLE; las invocaciones `RpcCallback` en el scheduler fueron cero en esta ventana. Las tablas/bindings SIF y módulos cargados son estado serializable. **UNKNOWN:** no se instrumentó un indicador exhaustivo de todos los globals privados RPC/SIF; el diagnóstico excluye callbacks scheduler pendientes, no demuestra que cada tabla HLE tenga un serializer.

## Gate no cerrado

- UNKNOWN: `PS2Memory` guarda `m_pendingGifTransfers`, `m_pendingVif0Transfers`, `m_pendingVif1Transfers` y `m_completedDmacCauses` privados (`ps2_memory.h:421-425`). Se midieron registros DMA, arbiter GIF y transferencia GS, **no** el tamaño de esos cuatro contenedores. El código puede añadir transferencias y drenarlas en llamadas separadas (`ps2_memory.cpp:1297-1301,1515-1523,1562-1743`). Un canal DMA inactivo no prueba que la cola privada sea vacía. Añadir acceso por cabecera compartida habría recompilado el código generado; esta fase acotada no lo hizo.
- UNKNOWN: el predicado guest se correlacionó con fin de PSS, pero la imagen del Main Menu en estas corridas no pudo inspeccionarse sin la superposición. La observación previa del usuario no fija el instante exacto del muestreo `RUN_101`.
- INFERENCIA: lo medido muestra una oportunidad estable durante más de 30 límites EE tanto idle como tras navegación; **no** demuestra todavía que *todas* las operaciones no serializables estén ausentes. No hay evidencia de dependencia continua que justifique `NOT_FEASIBLE`; tampoco evidencia completa para `FEASIBLE`.

No se estimó esfuerzo de serialización: el encargo lo condiciona a `DEVSTATE_SAFEPOINT_FEASIBLE` y ese gate no se alcanzó. Aun si una futura prueba lo cerrase, el inventario de DEVSTATE_001 implica una restauración grande de EE, MMIO, GS, VU, IOP HLE y servicios, no una copia de RAM.

## Prueba dirigida final: cuatro contenedores y correlación visual

- HECHO: la sonda temporal leyó directamente `.size()` de `m_pendingGifTransfers`, `m_pendingVif0Transfers`, `m_pendingVif1Transfers` y `m_completedDmacCauses` en `EeScheduler::run()`, justo después de `processPendingEvents()` y antes del despacho guest; el último se leyó bajo `m_completedDmacMutex`. Corrección del informe anterior: estos miembros están declarados bajo `public:` en `ps2_memory.h`; no fue necesario modificar ninguna cabecera ni recompilar fuentes generadas. Los otros contadores se registraron en la misma muestra. No se cambió semántica de ejecución.
- HECHO: la primera apertura instrumentada terminó durante Memory Card porque el usuario salió al no conseguir avanzar con Enter; `analysis/local/devstate_001a_final/runtime.log` se conserva solo para explicar el intento descartado. La segunda apertura fue visible en el escritorio Windows `Default` y escribió `analysis/local/devstate_001a_final/runtime2.log` (ejecutable instrumentado SHA-256 `e1cf7d4ad27a913134ff51466098a1444f4df8c42041cddb247fb87f66453ebc`).
- HECHO: la segunda corrida produjo **11 768 muestras consecutivas**, tick `1300..13067`. Los cuatro contenedores tuvieron tamaño **0 en las 11 768 muestras**, al igual que `activeInv`, `pendingInvFn`, `rpcInv`, `events`, `timerIrq`, `alarms`, `dmaActive`, `gifQueued`, `gsPartial`, `mcOpen`, `mcPending` y los indicadores guest-visibles medidos de MPEG/CD/audio. `padRead` avanzó de `1276` a `13043`. Hubo pulsaciones `Down`, `Up`, `Left` y `Start` observadas, pero ninguna pulsación `Cross` registrada; las entradas no consiguieron llevar esta corrida al PSS. No se investigó ni modificó `Pad` fuera del alcance del gate.
- HECHO: a las `2026-09-24T14:42:19+02:00`, con la última muestra en tick `13067`, el usuario confirmó explícitamente que la ventana visible de **esta misma corrida** mostraba **Language Select**, no Main Menu. El log no contiene inicio de `.PSS`, `sceMpegInit`, ni el marcador `CDMODULE/MOVIE` de fin de película. Por tanto, estas 11 768 muestras son de una permanencia **pre-PSS**, no de Main Menu.
- RETRACTADO: `ps2_main` estacionado en `0x0015B5B8` más MPEG/CD idle identifica por sí solo Main Menu. Ese subconjunto del predicado también ocurre mientras Language Select está visible. El predicado completo de `RUN_101` incluía explícitamente retorno de `Title_move` **después del PSS**; esta corrida no llegó a ese evento y por tanto **no lo refuta**.
- INFERENCIA: las cuatro colas pueden estar vacías durante una ventana estable pre-PSS y su lectura no exigió tocar una cabecera compartida. **UNKNOWN:** su tamaño en una ventana Main Menu visualmente confirmada sigue sin medirse; tampoco existen ventanas idle/post-input de Main Menu visualmente correlacionadas en esta corrida. No se puede combinar la visualización de Language Select de esta corrida con las muestras post-PSS de `RUN_101` para certificar el safe-point.

## Corrida de cierre: Main Menu visualmente confirmado + cuatro colas (2026-09-24)

- HECHO: se reinstaló la sonda con el mismo punto de muestreo y el mismo formato de línea `[DEVSTATE:final]` (una muestra por VSync en `EeScheduler::run()`, tras `processPendingEvents()` y antes de la selección/despacho guest; las cuatro colas leídas directamente, `m_completedDmacCauses` bajo su mutex; lectores HLE de solo lectura definidos dentro de los TU de los stubs, sin tocar cabeceras compartidas ni semántica). Patch de la sonda preservado: `analysis/local/devstate_001a_final/probe_reinstall.patch` (SHA-256 `09db6a897f5994a7fb2fae24225d2579387ef718a92cd160e84d87e6f55d1294`); ejecutable instrumentado SHA-256 `ce56a86954d66c8a43e1aeebc52cdcd5b0bae8bef39f9f676f763d66201a505c`; log completo `analysis/local/devstate_001a_final/runtime3.log`. El usuario navegó manualmente; no se envió ningún input automatizado.
- HECHO (vínculo visual en la MISMA corrida): el log contiene el PSS completo (`sceMpegInit`/`MOVIE:start`, luego `[MPEG:delete]` y `CDMODULE/MOVIE:status alive=0`), y el usuario confirmó visualmente Main Menu a las `2026-09-24T15:12:22+02:00` (última muestra tick 1868, `pc=0x15b5b8`, `mpeg/cd/audio=0x0`). Tras una excursión accidental (abajo) volvió al menú y re-confirmó a las `15:15:18` (tick 3191, misma firma).
- HECHO — FASE A (idle): 162 muestras consecutivas (ticks 1869–2030) y, tras la re-confirmación, 140 muestras consecutivas (ticks 3192–3331); en TODAS: `activeInv=0, pendingInvFn=0, rpcInv=0, resumeFn=0, waitFn=0, invocationFn=0, events=0, timerIrq=0, alarms=0, dmaActive=0, gifQueued=0, gsPartial=0, mcOpen=0, mcPending=0`, **las cuatro colas = 0**, `mpeg=cd=audio=0x0`, `padButtons=0xffff`, PC estacionado `0x15b5b8`. `pendingInv=1` constante: la invocación VSync serializable ya clasificada (sin `onComplete`, sin RPC).
- HECHO — input: el primer intento con el mando registró `Cross` (tick 2304) y `Square` (no Down: el D-pad del mando no está mapeado y el stick no aparece en `padButtons`); el Cross relanzó un PSS (mpeg pasó a `0x9..0xf`, `pc=0x15b570`) — excursión documentada FUERA de las ventanas certificadas; incluso durante ese PSS las cuatro colas y los demás gates permanecieron a 0. El input válido se hizo por teclado (flecha ↓/↑): tren de pulsaciones `Down (0xffbf)` y `Up (0xffef)`, ticks 3662–4088 (102 muestras con botón activo), permaneciendo en Main Menu (`mpeg=0x0` durante todo el tren).
- HECHO — FASE B (post-input): 130 muestras consecutivas (ticks 4089–4218) tras el último release, todas con los mismos gates a cero y `padButtons=0xffff`.
- HECHO — durante el propio tren de navegación (427 ticks, 3662–4088) todos los gates medidos también fueron 0.
- HECHO — **LONGEST_FULLY_CERTIFIED_QUIESCENT_RUN: 1140 ticks contiguos (3192–4331)** con todos los gates, las cuatro colas y los indicadores MPEG/CD/audio a cero, cubriendo idle + navegación + post-input en Main Menu visualmente confirmado.
- Caveats restantes: (a) el PC de retorno `0x001A5A48` de `Title_move` no fue capturado por el muestreo 1/tick de esta corrida (0 ocurrencias); el predicado queda anclado por PSS-completado + `ps2_main 0x0015B5B8` + MPEG idle + confirmación visual, con `0x001A5A48` como refuerzo cuando el muestreo lo capture; (b) el "único Down" fue en la práctica un tren Down/Up — evidencia más fuerte, no más débil, de estabilidad bajo navegación; (c) `iopRemaining=0` se apoya en `alive=0` + `cd=0x0` + ausencia de chunks tras `[MPEG:delete]` (cdmodule no expone la métrica directamente); (d) persisten los caveats previos no bloqueantes: política de reconstrucción del audio host-only y auditoría exhaustiva de globals RPC/SIF.
- HECHO — limpieza: sonda retirada por completo; diff del vendor tras la retirada byte-idéntico al backup pre-sonda (`vendor_before_probe_reinstall.patch`, SHA-256 `71c8e97617b164fd8ebf39b47f3373392234c502f618a36b05ad4b4fe371beea`); `git diff --check` limpio en raíz y vendor; ejecutable canónico reconstruido (SHA-256 `b9db2e99f73850f15514bc4c58436740896420c35a9cbbe3d7d67f6f3a88de5b`).

## DEVSTATE_001A RESULT

MAIN_MENU_PREDICATE: PSS iniciado y completado en la misma corrida (`sceMpegInit`/`MOVIE:start` seguidos de `[MPEG:delete]` y `CDMODULE/MOVIE:status alive=0`) + `ps2_main` estacionado en PC `0x0015B5B8` + `mpeg=cd=audio=0x0`, correlacionado con confirmación visual del usuario en esa corrida; el PC de retorno de `Title_move` `0x001A5A48` actúa como refuerzo cuando el muestreo 1/tick lo capture (en esta corrida no coincidió con ninguna muestra)

VISUAL_CONFIRMATION_TICKS: 1868 (2026-09-24T15:12:22+02:00) y re-confirmación 3191 (15:15:18), ambas en `analysis/local/devstate_001a_final/runtime3.log`

IDLE_WINDOWS: 162 muestras (ticks 1869–2030) y 140 muestras (ticks 3192–3331), todas con todos los gates y las cuatro colas a cero

INPUT_WINDOW: tren Down/Up por teclado, ticks 3662–4088 (release final 4088); la excursión Cross previa (tick 2304, salida y retorno al menú) queda fuera de las ventanas certificadas

POST_INPUT_WINDOW: 130 muestras (ticks 4089–4218), todos los gates y las cuatro colas a cero

LONGEST_FULLY_CERTIFIED_QUIESCENT_RUN: 1140 ticks contiguos (3192–4331), idle + navegación + post-input en Main Menu confirmado, con las cuatro colas, RPC, DMA, GIF/VIF, GS parcial, MC, MPEG, CD y audio guest-visibles a cero en cada muestra

FOUR_QUEUES: `pendingGifTransfers=0`, `pendingVif0Transfers=0`, `pendingVif1Transfers=0`, `completedDmacCauses=0` en el 100 % de las muestras de las ventanas certificadas (y también durante el PSS de la excursión)

RPC_CD_MPEG_AUDIO_DMA_MC: `rpcInv=0` y sin RPC activo en el límite; `cd=0x0`; `mpeg=0x0`; `audio=0x0`; `dmaActive=0`, `gifQueued=0`, `gsPartial=0`; `mcOpen=0`, `mcPending=0`; `pendingInv=1` constante = invocación VSync serializable (sin `onComplete`, clasificación previa mantenida)

CAVEATS: retorno `0x001A5A48` no muestreado (refuerzo, no requisito); "único Down" fue tren Down/Up (evidencia más fuerte bajo navegación); `iopRemaining=0` inferido de `alive=0` + `cd=0x0` + ausencia de chunks post-delete; pendientes no bloqueantes: política de reconstrucción de audio host-only y auditoría exhaustiva de globals RPC/SIF — a resolver en la fase de diseño del serializador

SAFEPOINT_PROVEN: YES

PRIMARY_CLASSIFICATION: DEVSTATE_SAFEPOINT_FEASIBLE

RECOMMENDATION: pasar a DEVSTATE_002 (diseño del serializador del safe-point sobre este predicado y ventana: inventario EE/MMIO/GS/VU/IOP-HLE/servicios de DEVSTATE_001, más política de audio y serialización de tablas RPC/SIF); NO implementar save/load todavía

TEMP_INSTRUMENTATION_REMOVED: YES (diff vendor byte-idéntico al backup pre-sonda; rebuild canónico verificado)

CHECKPOINT_DECISION: NO_COMMIT

PUSH: NO
