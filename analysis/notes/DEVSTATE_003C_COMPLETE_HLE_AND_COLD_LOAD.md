# DEVSTATE_003C — Complete HLE state + cold load + Main Menu resume

Fecha: 2026-10-02. Normativos: DEVSTATE_001A/002/003A/003B.

## 1. Baseline

Capturado en `analysis/local/devstate_003c/` antes de editar
(`baseline_root.txt`, `baseline_vendor.txt`, `baseline_vendor_dirty.patch`,
SHA-256 `e0a340d9caaeeafc5ac8f26b0d6e30e2198c23f1eb79d6491f9a2462822ffe1e`).
El dirty preservado contiene: producción PRE-DEVSTATE (MPEG.cpp/Pad.cpp/
gs_cpu_backend.cpp) + DEVSTATE_003A + DEVSTATE_003B (CheckpointManager
save/loadPartial de 5 secciones, SCHED+GS, F9 request/boundary hook). Nada
revertido.

## 2. Cambios documentales DEVSTATE_002

§17 corregido: UNKNOWN-1/2/3 marcados CLOSED (ya cerrados desde 003A/§16-bis);
único pendiente documental real: UNKNOWN-4 (dirección RDRAM del cursor de
menú, para TEST B — no bloquea esta fase).

## 3. Auditoría IOP (nuevos hallazgos sobre 003A)

Confirmación línea a línea de los 9 servicios con interfaz de codec real
añadida a `IopService`/`IopSubsystem`:

| Servicio | Hallazgo nuevo vs 003A |
|---|---|
| clfile | `m_root` es mutable guest-visible (seteado por RPC `SetRoot`), **no** reconstruible desde config — corrige la clasificación 003A de IMMUTABLE_RECREATE a SERIALIZE_EXACT. `m_fileHandles`/`m_loads` confirmados: el u64 `handle` es el valor devuelto por `IopHost::openHostFile` (FORBIDDEN_HOST_STATE real, sin path almacenado para reabrir) — gate `empty()` en vez de reconstrucción. |
| tsnddrv | `m_state` (direcciones IOP-RAM) confirmado guest-afectante: gatea `ensureMemoryLocked()` y se lee directamente en la ruta de completación de transferencia (línea ~246) — SERIALIZE_EXACT con evidencia de sitio de uso. |
| sound_update_stub | `m_updateCounter` se escribe de vuelta al guest cada llamada (contador de secuencia por-update) — guest-visible, no solo diagnóstico; corrige 003A. |
| cri_dtx | Confirmado: 5 maps de PODs puros por handle, sin punteros host — SERIALIZE_EXACT sin reservas. |
| cdmodule | `MovieStreamState` POD confirmado; gate `active==false` en Main Menu v1. |
| dbcman/mcserv/sdrdrv | Confirmado: solo contadores de rate-limit de logs, nunca escritos de vuelta al guest ni afectan RPC results — D (sin override, default no-op). |
| libsd | Sin miembros mutables — confirmado stateless (el estado real vive en `Audio.cpp` EE-side). |

Interfaz implementada (`IopService` en `iop_service.h`): `serializeCheckpointState(vector<uint8_t>&) const`, `deserializeCheckpointState(data,size,consumed&,error&)`, `validateCheckpointSafe(reason&) const` — defaults no-op/true, raw bytes (sin dependencia de `checkpoint.h` en ps2xIOP). `IopSubsystem` orquesta: framing `[u32 count][u16 nameLen][name][u32 payloadLen][payload]*`, matching por nombre contra los servicios YA configurados (nunca crea/destruye servicios). Ningún servicio activo contiene estado host-only irreconstruible que bloquee el LOAD — **sin blocker**.

## 4. Barrido SIF/RPC (nuevos hallazgos)

- `SIF.cpp`: confirmados `g_sifRegs/Sregs/CmdHandlers/HeapAllocations/HeapStorage/CmdBuffer` (ya en 003A) + nuevos: `g_sifSysCmdBuffer`, `g_sifCmdInitialized`, `g_nextSifDmaTransferId` (allocator; `g_sifDmaTransferMutex` protege SOLO este allocator — "Transfers are applied immediately by sceSifSetDma" confirmado por comentario del propio código, sin estado de transferencia pendiente que gatear).
- `RPC.cpp`/`Helpers/State.h`: confirmados `g_rpc_servers`/`g_rpc_clients` (`RpcServerState{sid,sd_ptr}`, `RpcClientState{busy,last_rpc,sid}`), `g_rpc_active_queue` (puntero cabeza de lista enlazada EN RDRAM, ya cubierto por MEM), `g_sif_modules_by_id`/`g_sif_module_id_by_path`/`g_next_sif_module_id` (bindings de módulos cargados, con `path`/`pathKey`/`refCount`/`loaded`). **Gate real de "RPC en vuelo" encontrado**: `RpcClientState.busy` — si algún cliente tiene `busy==true`, hay una llamada en curso; nuevo gate `checkpointValidateRpcSafe()`.

## 5. Formato v1 — 12 secciones

IDs 1-12 (Core/Mem/Mmio/Sched/Gs ya existían; nuevos: Iop=6, SifRpc=7, Cd=8,
Mc=9, Audio=10, Pad=11, Mpeg=12), cada uno con versión propia (`kXxxSectionVersion=1`). `kRequiredSectionsV1[]` lista las 12; `CheckpointFileReader::parseAndValidate` las exige TODAS presentes (PART 8 paso 8) **antes** de verificar ningún checksum de payload — un archivo con secciones faltantes se rechaza con `"required section missing: id=N"` sin tocar ningún estado.

## 6. Layout de cada sección nueva

- **IOP**: ver §3 (framing por servicio).
- **SIF_RPC**: SIF (maps u32→u32 ordenados + heap storage completo + scalars) seguido de RPC (servers/clients ordenados + scalars + module bindings). Debug history (`g_sif_rpc_debug_history`) deliberadamente NO serializado (diagnóstico puro).
- **CD**: `g_cdInitialized/g_lastCdError/g_cdMode/g_cdStreamingLbn/g_cdStreamingEndLbn/g_nextPseudoLbn` + `CdStreamTimingState` completo (10 campos). Índices de disco (`g_cdFilesByKey` etc.): RECONSTRUCT_ON_LOAD, lazy, nunca serializados.
- **MC**: `g_mcNextFd/g_mcLastCmd/g_mcLastResult/g_cvMcFileCursor` + por-puerto `{currentDir, formatted}`. Gate: `g_mcFiles.empty() && !g_mcCommandPending` (FILE* es FORBIDDEN_HOST_STATE).
- **AUDIO**: dos partes — HLE (`voiceTransfers[2]`/`blockTransfers[2]`, gate `checkpointProbeAudioMask()==0`) + sample bank lógico de `PS2AudioBackend` (nuevo método público `serializeCheckpointSampleBank`: `mostRecentSampleKey` + banco completo (key,rate,pcm) ordenado por clave + load-order paralelo (key,rate,pcm) — las claves de load-order pueden referenciar samples ya evictados del banco principal, por eso se serializan completos por separado). Nunca toca `m_impl`/dispositivo/mutex.
- **PAD**: por-puerto `{open,analogMode,pressureEnabled,buttonMask,dmaAddr,reqState,readCount,lastReadDataAddr}`. Neutralización en LOAD: `lastInput`→`{0xFFFF,center,center,center,center}`, `lastData` a cero, `lastUsedOverride/lastUsedBackend/lastReadOk/transientState` reseteados — ningún botón físico sobrevive como flanco fantasma.
- **MPEG**: escalares de `MpegStubState` (initialized, nextCallbackHandle, cdStreamGeneration, cdStreamBytesProduced/Demuxed, cdStreamEofPending, currentCdStreamEofSeen) + `g_p423NextTransactionId` (allocator P4.2.3). Gate: `checkpointProbeMpegMask()==0`. `playbackByMpeg`/`callbacksByMpeg` se limpian explícitamente en deserialize (deben estar vacíos, consistente con el gate).

## 7. Validador de safe-point — gates añadidos en 003C

Sobre el validador 003A/003B: + `ps2_syscalls::checkpointValidateRpcSafe` (sin cliente RPC `busy`) + `runtime.m_iopSubsystem->validateCheckpointSafe` (agrega el `validateCheckpointSafe` de cada servicio, hoy solo cdmodule/clfile tienen gate real: movie inactivo, sin handles abiertos). CD/MC/Audio/MPEG reusan sus probes 003A (`checkpointProbeCdMask/Mc/AudioMask/MpegMask == 0`) ya presentes en el validador desde 003A/003B.

## 8. Cold load — arquitectura

`PS2Runtime::run(const std::filesystem::path &devstateLoadPath = {})`:
reemplaza el `void run()` sin argumentos (único sitio de llamada:
`main.cpp`, que ahora lee `DMC_DEVSTATE_LOAD` DESPUÉS de
`initialize()+loadELF()+setProcessIdentity()` — exactamente el punto que
PART 9 exige: todo host init normal ya corrió, igual en ambas rutas).

- **COLD** (`devstateLoadPath` vacío): exactamente el código previo —
  `resetSifState/resetIop/resetAudioStubState/resetMpegStubState/
  initializeEeKernelState` + zero de r4/r5/sp + `m_eeScheduler->reset()`.
- **RESTORE**: NADA de lo anterior corre. `gameThread` llama
  `m_eeScheduler->prepareForRestore(rdram)` (fija SOLO
  `m_executorThread`/`m_rdram`/`m_stopRequested`/`m_checkpointPending` —
  nunca limpia threads/colas/etc., porque SCHED está a punto de
  sobrescribirlo todo) y después `CheckpointManager::loadFull(...)`.

`CheckpointManager::loadFull`: Paso 0 valida el archivo completo (ya exige
las 12 secciones + todos los CRC) sin mutar nada; después restaura en el
orden MEM→MMIO→CORE/VU→GS→SCHED (stage `CoreMemMmioRestored`) →
IOP→SIF_RPC→CD→MC→AUDIO→PAD→MPEG (stage `SchedGsRestored`) → `PartialRestore`
(terminal de `loadFull`). El **rebind de host (paso 14) es un no-op
demostrado por construcción**: `m_gifArbiter`/callbacks VU/`m_iopHost`/
`m_iopSubsystem`/`GS::m_privRegs`+VRAM ya fueron conectados por el MISMO
`initialize()+loadELF()` que corre en ambas rutas, y ningún deserializer
toca esos punteros (solo escribe dentro de buffers/objetos ya existentes).
La reconstrucción derivada (paso 15) ya ocurre DENTRO de
`GS::deserializeCheckpointState` (invalida snapshot/latch/frame de
presentación/historial de debug — el próximo `latchHostPresentationFrame()`
del render loop los regenera desde VRAM+regs restaurados); los índices CD
son lazy (paso 15 no hace nada ahí, igual que en frío). El reconcile de
input (paso 16) ya ocurre dentro del deserialize de PAD.

`Complete` (el único estado que debe preceder al despacho real de guest) se
asigna en **un único sitio de todo el código**: dentro del lambda de
`gameThread`, inmediatamente después de que `loadFull` retorne éxito, justo
antes de `m_eeScheduler->run()`. Si `loadFull` falla, se loguea
`[CKPT:LOAD] FATAL: <razón>` y el hilo retorna SIN llamar a
`m_eeScheduler->run()` — el proceso no ejecuta guest, no hay fallback
silencioso a frío.

## 9-19. Resultados (tests, SAVE completo, fresh load, TEST A-E)

Fecha de ejecución: 2026-10-02. Build final: relink incremental único
(`ps2_runtime.cpp` recompilado — no se tocó ningún header compartido en el
último cambio, el único required antes de la sesión en vivo era renombrar
la ruta de salida del F9 de `dmc_checkpoint_mainmenu.dmck` a
`dmc_checkpoint_mainmenu_v1.dmck` para no pisar el artefacto 003B).

### 9. Self-tests (previos a la sesión en vivo)

89 PASS / 0 FAIL (`[CKPT:SELFTEST] done, failures=0`). Incluye, entre
otros: TEST1-10 reescritos para el framing de 12 secciones; round-trip +
determinismo para IOP/SIF_RPC/CD/MC/AUDIO/PAD/MPEG (serialize/deserialize/
bytes deterministas); FULL12 (`save()` escribe 12 secciones, `parseAndValidate`
confirma `sectionCount==12`, `loadFull()` tiene éxito); LOADORDER
(`isExecutingGuest()` falso antes Y después de `loadFull()`, `stage` llega
a `PartialRestore` y nunca a `Complete` dentro del self-test — prueba de
que ningún guest-dispatch ocurre durante la carga); FORMAT (`dmc_checkpoint_mainmenu.dmck`,
el artefacto real de 5 secciones de 003B, es **rechazado** por el nuevo
validador — `[CKPT:SELFTEST] PASS FORMAT old 5-section 003B checkpoint rejected`).

### 10. SAVE real (sesión en vivo, proceso en frío)

Usuario navegó manualmente Memory Card → idioma → película de apertura →
Main Menu certificado. F9 solicitado una vez:

```
[CKPT:REQUEST] queued (F9); waiting for scheduler boundary
[CKPT:REQUEST] SUCCESS tick=11612 path=dmc_checkpoint_mainmenu_v1.dmck size=46350553 ms=344.36
```

Verificación independiente (`analysis/local/devstate_003c/verify_checkpoint_v1.py`,
reimplementación pura-Python del formato y CRC-32, sin depender del build
C++): las 12 secciones requeridas están presentes, CRC de cada una y del
header validan, `gameId='SLES_503.58'`, `sectionCount=12`.

```
RESULT: ALL_SECTIONS_VALID
```

Copia de evidencia preservada en `analysis/local/devstate_003c/main_menu_full.dmck`
(SHA-256 idéntico al archivo operativo, confirmado con `sha256sum`). El
artefacto 003B (`dmc_checkpoint_mainmenu.dmck`, 41,105,997 bytes) permanece
intacto y sin sobrescribir.

### 11-12. Carga en proceso nuevo ("the money test")

Proceso en frío cerrado completamente (`Stop-Process` + verificación
`PROCESS_GONE`). Proceso **nuevo** lanzado con
`DMC_DEVSTATE_LOAD=dmc_checkpoint_mainmenu_v1.dmck`:

```
[CKPT:LOAD] DMC_DEVSTATE_LOAD=...\dmc_checkpoint_mainmenu_v1.dmck
DEVSTATE_003C: restoring from checkpoint ...
[CKPT:LOAD] SUCCESS stage=Complete pc=0x19f7dc resuming scheduler
[GP:VSync] tick=11613
```

No se ejecutó ninguna secuencia de Memory Card/idioma/película — el
scheduler reanudó exactamente en `tick=11613` (un tick después del guardado
en `tick=11612`), sin pasar por `reset()` ni por el boot guest normal.
Proceso `Responding=True`, sin errores/excepciones/crashes en el log.

RESTORE_TIME_MS / FIRST_MENU_PRESENT_MS: no se instrumentó con precisión
sub-segundo (habría requerido añadir `fflush`+timestamps en
`ps2_runtime.cpp` y un rebuild adicional solo para pulir una métrica, lo
cual se consideró innecesario para esta fase — ver política de build
mínimo). Cota observada por polling externo: ≤ ~2000 ms desde el lanzamiento
del proceso hasta `[CKPT:LOAD] SUCCESS]` (confirmado en dos corridas
independientes). stdout/stderr del proceso están completamente bufferizados
al redirigirse a archivo (no-TTY), por lo que el polling de archivo no da
precisión de milisegundo real — se documenta como limitación conocida, no
como dato exacto.

### 13-14. TEST A — Main Menu visible tras carga en frío

Confirmación visual explícita del usuario (no inferida de logs): "Si se ha
cargado". **PASS.** Sin pantalla negra persistente, sin crash/deadlock,
VSync avanzando, imagen estable (confirmado implícitamente por la
interacción subsiguiente con el menú).

### 15. TEST B — input tras carga

Usuario presionó Abajo y luego Arriba en el Main Menu restaurado: "Si se
movio". **PASS.** Selección se mueve correctamente, sin input fantasma
(gate de neutralización PAD-on-load funcionando como diseñado).

### 16. TEST C — audio/reconstrucción host

Evidencia estructural: `AUDIO: Device initialized successfully` (miniaudio
| WASAPI, 48000 Hz) en ambas rutas; pipeline `[MPEG:audio-pes]`/
`[PCM:audioDecEndPut]` decodifica PCM normalmente en la sesión restaurada
(mismo patrón que en frío), sin crash/deadlock. Verificación audible no
concluyente por una razón **no relacionada con esta fase**: el usuario
confirmó que "en lo que vamos de desarrollo nunca se ha oído nada" — el
proyecto nunca ha tenido salida de audio audible, en ninguna sesión previa,
independientemente del checkpoint (ver memoria de proyecto
`project_dmc_audio_silent_baseline.md`). Confirmado que el mismo silencio
ocurre igual en la sesión en frío de control (mismo conteo de eventos
`PCM:audioDecEndPut`). **Clasificación: PASS estructural / sin regresión
introducida por DEVSTATE_003C** — el bug de audio real preexistente queda
fuera de alcance.

### 17. TEST D — continuidad de subsistemas

PASS. Evidencia: SCHED avanza desde el tick exacto del guardado (continuidad
de scheduler, no reinicio); GS presenta (menú visible, película de
attract-mode se reproduce); el temporizador de inactividad del menú
(dependiente de SCHED) disparó correctamente la película de attract-mode,
ejercitando CD+MPEG+GS desde estado restaurado; MPEG arrancó limpio desde
idle y decodificó exitosamente (`getPicture success #2000 size=512x448`);
CD sirvió nuevas lecturas (`[CDMODULE] fno=2 resourceId=...`) tanto para
la película como después para NEW GAME; SIF/RPC siguió respondiendo
(mismo patrón de trazas `IOP/RPC trace:unhandled` que en frío, tráfico de
background normal); MC coherente (mismas llamadas `GetInfo`/`Sync` sin
error); ningún puntero host prohibido sobrevivió (sin crash en ningún
momento de la sesión).

### 18. TEST E — equivalencia NEW GAME (mandatorio, boundary a GAMEPLAY_001)

Se generaron ambos traces en vivo (no existía trace COLD previo archivado):

- **RESTORED**: usuario seleccionó NEW GAME desde el Main Menu restaurado.
  Secuencia observada: teardown de la película de attract-mode
  (`MPEG:delete #6`, `CDMODULE/MOVIE:close`) → ~4 ticks de VSync → ráfaga
  de lecturas CD (`fno=2 resourceId=0x52/0x50/0x51/0x54/0x53/0xfe`,
  mismos `lba`/tamaños) → `unsupported fno=2 mode=2 size=0xc6800` → spawn
  de stack async (`RuntimeGuestArena:async-stack thread=22`) → VSync se
  enlentece drásticamente (de ~60 ticks/seg a ~60 ticks cada varios
  segundos), CPU alta (~1 núcleo). Usuario confirma visualmente:
  **pantalla negra**.
- **COLD**: proceso en frío nuevo, navegación manual completa repetida,
  NEW GAME seleccionado. Secuencia **idéntica**: mismo teardown
  (`MPEG:delete #1`), misma ráfaga CD con **los mismos `lba`/tamaños/
  `first32` byte a byte** (`fno=2 resourceId=0x52` con `lba=0xe3778
  size=0x44160` idéntico en ambas rutas, etc.), mismo warning
  `unsupported fno=2 mode=2 size=0xc6800`, mismo patrón de enlentecimiento
  de VSync. Usuario confirma visualmente: **pantalla negra**.
- Únicas diferencias observadas: id de thread async (`22` vs `7`) y
  dirección de bloque de arena (`0xa58ac0...` vs `0xa0a640...`) — ambas
  son contadores/asignaciones dependientes del historial de sesión (número
  de threads/allocaciones previas), no del código ejecutado ni del
  contenido de datos; no constituyen una divergencia de comportamiento.

**No se encontró ninguna primera divergencia de restauración antes del
blocker de gameplay conocido.** Comportamiento COLD y RESTORED son
funcionalmente equivalentes hasta el mismo punto exacto donde el blocker
preexistente de pantalla negra post-NEW GAME ya documentado ocurre.
Evidencia preservada en
`analysis/local/devstate_003c/test_e_restored_new_game_window.log` y en
`runtime_session_cold_newgame.log`/`runtime_session_restore.log` completos.

### 19. Resumen de campos requeridos

```
BASELINE_PRESERVED: YES (vendor dirty preservado, nada revertido; diffs+SHA256 en analysis/local/devstate_003c/)
OLD_003B_CHECKPOINT: REJECTED_AS_EXPECTED (selftest FORMAT, verificado también manualmente: archivo intacto en disco, 41,105,997 bytes)
REQUIRED_SECTIONS: CORE, MEM, MMIO, SCHED, GS, IOP, SIF_RPC, CD, MC, AUDIO, PAD, MPEG (12/12)
IOP: PASS
SIF_RPC: PASS
CD: PASS
MC: PASS
AUDIO: PASS
PAD: PASS
MPEG: PASS
FULL_SAVE_RESULT: SUCCESS
FULL_SAVE_TICK: 11612
FULL_SAVE_SIZE_BYTES: 46350553
FULL_SAVE_TIME_MS: 344.36
ALL_SECTION_CRCS_VALID: YES (12/12, verificador Python independiente)
FRESH_PROCESS_LOAD: SUCCESS (stage=Complete, pc=0x19f7dc, scheduler resume desde tick=11613)
RESTORE_TIME_MS: <=~2000 (cota por polling externo, sin instrumentación sub-ms — ver §11-12)
FIRST_MENU_PRESENT_MS: <=~2000 (misma cota, sin instrumentación dedicada)
MAIN_MENU_VISIBLE_AFTER_LOAD: YES (confirmación visual explícita del usuario)
MAIN_MENU_IMAGE_STABLE: YES
INPUT_AFTER_LOAD: PASS (Abajo/Arriba mueven selección correctamente)
AUDIO_AFTER_LOAD: PASS estructural (ver §16 — silencio es condición preexistente del proyecto, no regresión)
VSYNC_AFTER_LOAD: PASS (avanza continuamente desde tick=11613)
GS_AFTER_LOAD: PASS (menú y película presentados correctamente)
SIF_RPC_AFTER_LOAD: PASS (tráfico de background idéntico al patrón en frío)
CD_AFTER_LOAD: PASS (sirvió lecturas nuevas para película y para NEW GAME)
COLD_NEW_GAME: pantalla negra tras ráfaga de lecturas CD (blocker preexistente, no investigado a fondo aquí — fuera de alcance)
RESTORED_NEW_GAME: pantalla negra, secuencia byte-idéntica a COLD
COLD_VS_RESTORED_EQUIVALENCE: DEVSTATE_COLD_RESTORE_EQUIVALENT
FIRST_RESTORE_DIVERGENCE: NONE_FOUND (sin divergencia antes del blocker conocido)
PRIMARY_CLASSIFICATION: DEVSTATE_003C_MAIN_MENU_COLD_LOAD_WORKING
DEVSTATE_V1_READY_FOR_GAMEPLAY_001: YES
TEMP_DIAGNOSTICS_REMOVED: N/A (no se añadió instrumentación temporal de diagnóstico en esta fase; toda la instrumentación añadida — [CKPT:*] — es parte permanente del diseño del checkpoint, documentada en §3-8)
PRODUCTION_DIFF_DOCUMENTED: YES (vendor sigue dirty según baseline capturado en §1 + cambios 003C descritos en §3-8; root solo con notas nuevas + los dos .dmck sin trackear)
NEXT: GAMEPLAY_001_MAIN_MENU_TO_FIRST_PLAYABLE_FRAME
CHECKPOINT_DECISION: NO_COMMIT
PUSH: NO
```
