# GAMEPLAY_001E — CDMODULE `mode&2` mixto (EE/IOP/SPU), recurso `0x103` y checkpoint ABI 2

Fechas de la fase: 2026-10-06 a 2026-10-11. Consolidación permanente: 2026-10-14.
Informes locales: `analysis/local/gameplay_001e/GAMEPLAY_001E_STATIC_AUDIT.md`,
`ORACLE_103_CONSOLIDATED.md` e `IMPLEMENTATION_103_STATUS.md` (no versionados).

> **Estado:** la implementación de CDMODULE y el cambio a ABI 2 existen **solo en el vendor local** (sin
> parche en `patches/`, sin commit y sin publicar). La validación en ejecución de `0x103` se obtuvo después,
> desde el checkpoint ABI 2, en GAMEPLAY_001F/001G.

## 1. Objetivo

Especificar e implementar la semántica de `CDMODULE fno=2 mode&2` para descriptores mixtos. El recurso
`0x103` incluye tipo 0 (EE), tipo 1 (IOP) y tipo 2 (SPU). La implementación debía validarse contra un oráculo
ORIGINAL. Antecedentes: [GAMEPLAY_001_MAIN_MENU_TO_FIRST_PLAYABLE_FRAME.md](GAMEPLAY_001_MAIN_MENU_TO_FIRST_PLAYABLE_FRAME.md)
(001C/001D) y [GAMEPLAY_001_RET0_AUDIT.md](GAMEPLAY_001_RET0_AUDIT.md) (RET0_FIRSTCALL).

## 2. Estado inicial y problema

- **HECHO:** después de 001D y RET0_FIRSTCALL, la ejecución llegaba a `0x108` y se detenía en el primer
  descriptor tipo 1 no soportado de `0x103`. Request: `LBA=0xEAAA5`, `size=0x55000`, `dest=0xE72E20`, `mode=2`.
  El HLE rechazaba la tabla mixta antes de mutar EE.

## 3. Evidencias utilizadas

- `CDMODULE.IRX`, SHA-256 `9d51f16d1ed861e508daa1389e6aaf4fe597687b3f0cd5feb920e62a479b34fa`. Es idéntico a
  `/DATA/MODULES/CDMODULE.IRX` de la ISO.
- `TSNDDRVM.IRX`, SHA-256 `c00b17f07aa0dfed089a517498074eec251c7b571f58a7649414ef88be7f31fd`.
- Desensamblado con Rabbitizer a partir de los bytes de los IRX (`analysis/local/gameplay_001e/asm/`), símbolos
  y relocalizaciones (`irx_elf.json`) y tablas de exportación (`sound_export_tables.json`).
- Cabecera del recurso en la ISO: `resource_103.json` (SHA-256 `9b1b2ca4…1eb75`).
- Oráculo ORIGINAL: savestates PCSX2 `.03`–`.06` con volcados EE/IOP/SPU (`oracle_kit/`), capturados
  manualmente por el usuario.

## 4. Primera divergencia identificada

**HECHO:** con un request idéntico, ORIGINAL aplica los cuatro efectos siguientes. RECOMP rechazaba la
tabla en el primer descriptor tipo 1.

| Paso | Destino ORIGINAL | Tamaño | Verificación |
|---|---|---:|---|
| tipo 0 → EE | `0xE72E20` | `0x4D7D0` | byte exacto ISO (SHA-256 `6177d0d3…f88a`) |
| tipo 1 → IOP | `pSndHeadBuff[5] = 0x128900` | `0x380` | byte exacto ISO (`5ef6bee4…0d79`) |
| copia secundaria EE | `SdtAddr + 0x2000 = 0x745640` | `0xA0` | byte exacto ISO tipo 1 `[0x250,0x2F0)` (`9d579bc2…4d35`) |
| tipo 2 → SPU | `0x129900` | `0x6270` | byte exacto ISO (`93a4fbcc…560e`) |

`recv = 0x4D7D0`. Solo cuenta el DMA EE.

## 5. Hipótesis investigadas

- H1: `mode&2` es una copia lineal del recurso. **Descartada** en 001D: hay que leer el sector de cabecera
  y recorrer los descriptores.
- H2: `recv` debería sumar todos los bloques. **Descartada:** solo `trans_mem_to_ee` incrementa
  `EE_TransSize` (`0x6E4–0x6EC`). Tipos 1 y 2 incrementan `TransSize[port]`, que es otra variable global.
- H3: el destino secundario `SdtAddr + 0x2000` es una constante del juego. **Descartada como diseño:**
  `SdtAddr` procede de `fno=1` (`Cd_init` → `CallCdModule(fno=1)` escribe `0x743640`) y debe derivarse
  de ese request.

## 6. Hipótesis descartadas y motivo

- **RETRACTADO:** las primeras capturas PINE de memoria IOP a cero se consideraron divergencias. No lo eran:
  la ruta de lectura de PCSX2 no era válida para esa memoria.
- H1, H2 y H3: ver §5.

## 7. Causa raíz

**HECHO:** el HLE `CdModuleService` no modelaba:

- los descriptores tipo 1 (IOP, buffer de cabecera de sonido del slot, con copia secundaria a EE);
- los descriptores tipo 2 (SPU, `TsndSpuTrans` + `TsndVabRegist`);
- ni el estado `SdtAddr` de `fno=1`.

Semántica estática completa en el informe local (`CdReadProc@0x7E0`, `trans_process_tbl@0x3AA8`,
`trans_mem_to_iop@0x314`, `trans_mem_to_spu@0x4C4`; exportaciones TSNDDRV ordinales 4, 5 y 8).

## 8. Modificaciones (locales, no versionadas)

- `vendor/PS2Recomp/ps2xIOP/src/modules/cdmodule.cpp`:
  - `fno=1` captura y conserva `SdtAddr`;
  - `mode&2` recorre los descriptores en orden;
  - tipos 0/3 → EE; tipo 1 → buffer IOP del slot más copia secundaria EE; tipo 2 → RAM SPU modelada más
    metadatos de registro VAB.
- `ps2xIOP/src/builtin_profiles.cpp` y `module_factories.h`: tablas de slots y destinos SPU procedentes del
  IRX identificado por hash. No dependen del resource ID ni del LBA.
- `ps2xRuntime/include/runtime/checkpoint/checkpoint_format.h`: `kCheckpointRuntimeAbiVersion` 1 → **2** y
  `kIopSectionVersion` 1 → **2**. El nuevo estado persistente de CDMODULE cambia la compatibilidad.
- **No** se editó código generado.

## 9. Pruebas realizadas

- Build canónico (`cmake --build … --target ps2EntryRunner`) PASS.
  - El primer intento iba a recompilar 10 129 unidades generadas por una dependencia transitiva.
  - Se verificó que ninguna unidad generada referencia las constantes cambiadas y se reutilizaron sus
    objetos (`build_dependency_note.txt`).
- Selftests del checkpoint: 97 PASS / 0 FAIL (`checkpoint_selftest.log`).
- El checkpoint `dmc_checkpoint_mainmenu_v2.dmck` (ABI 1) se rechaza con `FATAL: checkpoint runtime ABI mismatch`
  (`v2_load_after_sound_abi.log`). El rechazo es correcto: el checkpoint no contiene `SdtAddr`.
- Creación del checkpoint ABI 2 por cold boot (`cold_boot_abi2.log`, `cold_boot_abi2_retry.log`, 2026-10-11).

## 10. Resultados verificados

- **HECHO:** existe `dmc_checkpoint_mainmenu_abi2.dmck`:
  - 49 219 000 bytes, SHA-256 `88fe10cd0927a6c04fab3791caffa0ca6c920f93b6390ddd9781b34eb6a17078`;
  - identidad registrada al cargar: `abi=2 format=2 compat=c3db86c055f4c19f3bf496298dfb9193904e0b7c9bc832a5bd830613e75d4fdc`;
  - carga con `[CKPT:LOAD] executable build differs, checkpoint ABI compatible` en las ejecuciones de 001F, 001G y 001H.
- **INFERENCIA:** se guardó en la sesión de cold boot del 2026-10-11. El archivo tiene `mtime` 23:55 y el
  log `cold_boot_abi2_retry.log` cubre 23:50–23:56. **UNKNOWN:** la línea `CKPT:REQUEST` del guardado no
  aparece en ese log.
- **HECHO (001F, desde ABI 2, `analysis/local/gameplay_001f/ret0_hits_run.log`):**
  - `0x103 … blocks=3 bytesRead=0x4D7D0 status=ok recv=0x4D7D0`;
  - `mode2:IOP slot=5 bytes=0x380 secondaryEE=0xA0`;
  - `mode2:SPU slot=5 dest=0x129900 bytes=0x6270 vabHeaderOffset=0x20`.

  Los cuatro efectos tienen destino y tamaño iguales al oráculo.
- **UNKNOWN:** no se ha hecho una comparación byte a byte de la RAM SPU ni de la IOP RECOMP frente a los
  volcados ORIGINAL en ejecución.

## 11. Limitaciones y UNKNOWN

- Las consecuencias del driver TSNDDRVM (`sceHSyn_Load`, `TsndPortLoadInit`) no están modeladas y no existe
  backend de reproducción de audio.
- Según el usuario, el proyecto nunca ha producido audio audible. Esto no es una regresión de esta fase.
- `TsndSpuTrans` y `TsndVabRegist` devuelven 0 en ORIGINAL según la anotación manual del debugger. Esos
  valores no figuran en los JSON del kit.

## 12. Estado actual

`0x103`: implementado y alcanzado con destino, tamaño y `recv` iguales al oráculo. Equivalencia byte a byte
en ejecución: **no medida**. Código: **local, no versionado**. Checkpoint ABI 2: **operativo localmente**;
los `.dmck` nunca se versionan.

## 13. Archivos de código

`vendor/PS2Recomp/ps2xIOP/src/modules/cdmodule.cpp`, `ps2xIOP/src/builtin_profiles.cpp`,
`ps2xIOP/src/module_factories.h`, `ps2xRuntime/include/runtime/checkpoint/checkpoint_format.h`.

## 14. Evidencia local

`analysis/local/gameplay_001e/`:

- `asm/`, `irx_elf.json`, `resource_103.json`, `sound_export_tables.json`, `sound_irx_symbols.json`;
- `oracle_kit/` (JSON y `.bin` de los savestates `.03`–`.06`; contienen datos derivados del juego: **no
  publicar**);
- `checkpoint_selftest.log`, `v2_load_after_sound_abi.log`, `cold_boot_abi2*.log`, `build_dependency_note.txt`.

Los savestates PCSX2 y los volcados no pueden regenerarse sin la ISO original y una captura manual.

## 15. Relación con otras fases

- Anterior: 001D (`0xFF` mode&2 tipo 0) y RET0_FIRSTCALL (`capVu0MulMatrix2`).
- Siguiente: [GAMEPLAY_001F_RET0_BATCH_AUDIT.md](GAMEPLAY_001F_RET0_BATCH_AUDIT.md), que ya ejecuta desde
  ABI 2 y llega a `0x118`/`mode=0xA`.
