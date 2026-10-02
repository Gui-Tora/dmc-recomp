# GAMEPLAY_001 — Main Menu a primer frame jugable

Fecha: 2026-10-02. Fase abierta. No se ha demostrado la primera divergencia ORIGINAL/RECOMP ni se ha implementado un fix.

## Identidad y estado inicial

- **HECHO:** `git rev-parse HEAD = 702f1644d261ff8af116382b6e2ca74eb077f5b7`; `upstream.lock.json.patched_commit = ae378ab333501ec76b8db16069f8ac616514f040`.
- **HECHO:** ejecutable canónico: `analysis/local/symtabfirst/build/bin/Debug/dmc-recomp.exe`, SHA-256 `8e8cd6249d4c16170f7f43ea42f04e301d13f22628041c411afc42891630a742`. ELF `original/SLES_503.58`, SHA-256 `d0753a6b3b2f00802a50758a872d8cf051725aa31c839aa8894eee30ce58bab4` (entry/CRC32: ver `runtime/p311_pad_test.inc`, `0x00100008`/`0x77654AD2`).
- **HECHO:** antes de tocar código se guardaron `root_status.txt`, `vendor_status.txt` y `vendor_dirty_before_gameplay001.patch` en `analysis/local/gameplay_001/`; SHA-256 del patch `0a1c48381c566025de22aa80b0629101bd0e06eeaa2cb28a6b113c3fe698d872`. El vendor estaba sucio; no se revirtió ni limpió.
- **HECHO:** no hay cambios de runtime ni de código generado hechos en esta fase.

## Intento de observación con checkpoint

- **HECHO:** `analysis/local/gameplay_001/run_baseline.py` imprimió HEAD, `patched_commit`, build root y ruta absoluta antes de arrancar. Usó exclusivamente el ejecutable canónico, un proceso nuevo, la ISO local y `DMC_DEVSTATE_LOAD=dmc_checkpoint_mainmenu_v1.dmck`.
- **HECHO:** el proceso terminó limpiamente antes de que apareciera Main Menu o se enviara input. `baseline_180s.log` contiene `[CKPT:LOAD] FATAL: runtime build mismatch (v1 strict policy)`. Duración observada: 3,344 s; la prueba de 180 s **no comenzó**.
- **HECHO:** el build ID almacenado en el checkpoint es `29b02bb45d37bdf9aded78b4730228a7e4ac11dd5605cd4ade7b2d9ce4894ef2`; el ID calculado con el algoritmo de cuatro lanes FNV-1a del código sobre el ejecutable canónico actual es `94d8511801d1ead1b80af0b8a5aade654ea9a06bc5445c98d7300a71269d6444`. El checkpoint se guardó a las 18:43:58 y el ejecutable canónico tiene `LastWriteTime` 19:43:23 del mismo día.
- **INFERENCIA:** el relink posterior al guardado invalidó el checkpoint bajo la política estricta v1. Esta explicación concuerda con la comparación byte a byte del build ID; no implica corrupción de las 12 secciones ni un error en la lógica de restauración.
- **UNKNOWN:** si existe otro checkpoint completo compatible con el ejecutable actual. El archivo de evidencia `analysis/local/devstate_003c/main_menu_full.dmck` es idéntico al v1 operativo (SHA-256 `9955fb4e66aa6d6ee96c929c50fb34d4b19c3d2701adf0433eec636bb9e62c4d`).

## `fno=2 mode=2`: alcance demostrado hasta ahora

- **HECHO:** el log anterior de DEVSTATE_003C observó después de NEW GAME seis lecturas `mode=1` (`0x52/0x50/0x51/0x54/0x53/0xFE`), luego `[CDMODULE] unsupported fno=2 mode=2 size=0xc6800`, y luego `[RuntimeGuestArena:async-stack]`. Era una ejecución anterior, no la prueba nueva de 180 s.
- **HECHO:** `CdModuleService::handleRead` en `vendor/PS2Recomp/ps2xIOP/src/modules/cdmodule.cpp` lee resource ID, LBA, tamaño, destino y byte mode del buffer EE. Con mode distinto al soportado (`1`), registra el warning y devuelve `RpcResult{}` sin copiar datos ni escribir el conteo de transferencia de este servicio.
- **HECHO:** `SifCallRpc` en `vendor/PS2Recomp/ps2xRuntime/src/lib/Kernel/Syscalls/RPC.cpp` tiene una ruta genérica para `!handled`: puede copiar del send buffer al receive buffer, o poner ceros, y completar la llamada. Por tanto, el efecto guest exacto depende de los argumentos y del servidor registrado en *esta* invocación; el warning por sí solo no demuestra espera ausente ni retorno ficticio concreto.
- **HECHO:** la auditoría primaria de `CDMODULE.IRX` en `analysis/notes/BLOCKER_002_ASTRA_AUDIT.md` establece que `mode & 2` lee una cabecera del disco y puede seleccionar destinos EE/IOP/SPU; una copia lineal equivalente a `mode=1` sería semánticamente incorrecta.
- **UNKNOWN:** caller PC/thread, resource ID, LBA, destino, resultado final de RPC, rama posterior y semántica exacta de este `0xc6800`; aún falta comparar la operación correspondiente en PCSX2.

## Resultado provisional y siguiente paso

`PRIMARY_CLASSIFICATION = GAMEPLAY_001_INSUFFICIENT_EVIDENCE`. `ROOT_CAUSE_READY = NO`. `SEMANTIC_FIX_IMPLEMENTED = NO`. `FIRST_PLAYABLE_FRAME_REACHED = NO`.

**Siguiente acción única:** obtener un checkpoint Main Menu v1 creado por el ejecutable canónico actual mediante una sola navegación en frío, sin cambiar el formato ni desactivar la comprobación de build ID; repetir entonces la prueba NEW GAME de 180 s con instrumentación acotada.

**ACTUALIZACIÓN DEVSTATE_003D (2026-10-02):** la acción anterior quedó reemplazada por un cambio de infraestructura autorizado: formato v2 con identidad de compatibilidad de estado y build ID de ejecutable solo para procedencia. `dmc_checkpoint_mainmenu_v2.dmck` se guardó y cargó en procesos frescos, incluso tras un relink no semántico, con Main Menu e input confirmados por el usuario. Véase `DEVSTATE_003D_CHECKPOINT_COMPATIBILITY_ID.md`. La prueba GAMEPLAY_001 de 180 s y la primera divergencia ORIGINAL/RECOMP siguen pendientes.

`RETRACTADO`: ninguno en esta fase. La sospecha sobre `fno=2 mode=2` continúa explícitamente como hipótesis, sin promoción a causa raíz.
