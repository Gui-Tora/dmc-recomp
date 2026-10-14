# Estado actual del proyecto — punto de entrada para nuevos agentes

Última actualización: 2026-10-14, publicación técnica de GAMEPLAY_001I en la rama `gameplay-001i` ([GAMEPLAY_001I_TTE_PROMOTION_ADD_SUB_VF0.md](GAMEPLAY_001I_TTE_PROMOTION_ADD_SUB_VF0.md)).
Índice histórico completo: [RESEARCH_INDEX.md](RESEARCH_INDEX.md).
Reglas del proyecto: [AGENTS.md](../../AGENTS.md) (obligatorias) y [README.md](../../README.md).

> **Aviso:** el avance técnico de GAMEPLAY_001D–001I está publicado en la rama `gameplay-001i`, que aún no
> está fusionada en `master`. Lee la sección «Estado remoto frente a estado local».
> **El juego no es jugable.** El primer frame jugable **no** está demostrado.

Categorías: HECHO, INFERENCIA, HIPÓTESIS, UNKNOWN, RETRACTADO.

## 1. Identidad y arquitectura

| Elemento | Valor |
|---|---|
| Juego | Devil May Cry (2001), PS2, **PAL Europa**, v1.02 (`SYSTEM.CNF`: `VER = 1.02`, `VMODE = PAL`) |
| ELF | `SLES_503.58`; SHA-256 `d0753a6b3b2f00802a50758a872d8cf051725aa31c839aa8894eee30ce58bab4`; CRC32 `0x77654AD2`; entry `0x00100008` |
| `CDMODULE.IRX` | SHA-256 `9d51f16d1ed861e508daa1389e6aaf4fe597687b3f0cd5feb920e62a479b34fa` |
| `TSNDDRVM.IRX` | SHA-256 `c00b17f07aa0dfed089a517498074eec251c7b571f58a7649414ef88be7f31fd` |
| Recompilador | `ran-j/PS2Recomp`, baseline `14b1e5cb39b4af7e6fc12f9a29fdc751efde49d7` + parches de `upstream.lock.json`. `master`: 13 parches (`patched_commit` `dcd03052…`). Rama `gameplay-001i`: 15 parches (`patched_commit` `6bea2eb0146c321903dc895a6290f36fa1803df4`) |
| Límites de función | `.symtab` del ELF (*symtab-first*), no Ghidra. Ver [SYMTAB_BASELINE_PROMOTION.md](SYMTAB_BASELINE_PROMOTION.md) |
| Herramientas | Rabbitizer (en ps2_recomp), Ghidra solo para navegación y xrefs, PCSX2 2.8.2 como oráculo (PINE de solo lectura + debugger manual del usuario) |
| Repositorio | `https://github.com/Gui-Tora/dmc-recomp` (rama principal `master`) |

Arquitectura: el ELF se traduce a C++ (≈10 129 archivos generados, **nunca versionados ni editados a mano**).
El runtime de PS2Recomp aporta EE scheduler, GS por software, VIF/VU1 interpretados y HLE de IOP.
Los ajustes específicos de DMC van en `runtime/` mediante Game Overrides ligados a ELF/entry/CRC. Los cambios
generales del runtime van en `patches/`.

## 2. Estado de recompilación

| Componente | Estado |
|---|---|
| ELF → C++ (symtab-first) | funciona; build incremental |
| Arranque, IOP base, memory card, idioma, PSS inicial, Main Menu | alcanzados (BLOCKER_001–004; [BLOCKER_004_PRE_PERF_KNOWN_GOOD_CHECKPOINT.md](BLOCKER_004_PRE_PERF_KNOWN_GOOD_CHECKPOINT.md)) |
| Checkpoint de desarrollo Main Menu | formato v2; runtime ABI 2 en el parche 14 (`gameplay-001i`) |
| CDMODULE `fno=2 mode=1/2/8/0xA`, `fno=1/8`, SeReplace | validado; parche 14 (`gameplay-001i`) |
| Película `0x232` tras NEW GAME | reproducida completa con los parches 14 + 15 |
| Escena posterior a la película | ejecución por frames observada (INFERENCIA); **no validada visualmente** |
| Audio | no audible en ningún punto del proyecto (observación del usuario) |
| 20 `ret0` VU0 | 3 restauradas localmente (`capVu0MulMatrix2`, `capVu0AddVectorXYZ`, `capVu0SubVectorXYZ`); **17 pendientes** ([GAMEPLAY_001_RET0_RESTORATION_STATUS.md](GAMEPLAY_001_RET0_RESTORATION_STATUS.md)) |

## 3. Última ejecución comprobada (local, 2026-10-14)

- **Ejecutable reproducible:** `7110206b429e20198cef6e7f436ae4e33465287c4dd52e47e28e62d0a218459e` (corrida `pubR1`).
  - Construido con el vendor idéntico al replay de los 15 parches.
  - 17 `ret0` y overrides limpios, sin diagnósticos ni experimentos.
  - Selftests 97/0.
- **Corrida anterior equivalente:** `finalF1` (exe `d80ac6f2…`).
- **Condiciones:** checkpoint ABI 2. Detalle en
  [GAMEPLAY_001I_TTE_PROMOTION_ADD_SUB_VF0.md](GAMEPLAY_001I_TTE_PROMOTION_ADD_SUB_VF0.md).

Secuencia:

1. Carga del checkpoint en Main Menu (`CKPT:LOAD` compatible) y Cross por archivo PAD (NEW GAME).
2. CDMODULE carga 24 recursos, idénticos a ORIGINAL hasta `0x32`: `0x52 0x50 0x51 0x54 0x53 0xFE 0xFF 0x108 0x103 0x4F 0x118 0x14E 0x119 0x11A 0x5A 0x1B9 0x2E 0x31 0x36 0x3C 0x3A 0x33 0x44 0x32`.
3. `SndSceneBgmStart → SdrGetState(0x80000006)`: RPC no manejado, RECOMP devuelve 0 y ORIGINAL 1.
4. Película `0x232` (`MOVIE:start lba=0x215524 totalSize=0x490C004`), ~7,6 s después de `0x32`:
   - 1169 trozos hasta `remaining=0x0` y 1550 fotogramas MPEG;
   - 0 errores VU1/XGKICK.
5. Después de la película: `MPEG:delete`, inicialización de escena (`Get_light_str` ×21 500) y bucle por frame
   del hilo 12 (`Game_main`; ~346 600 `katVu0Clip_ld` en la ventana de 320 s).

**Sin el parche TTE:** después de `0x32` el GS se queda bloqueado en un paquete DIRECT falso de ~55 KiB y
`0x232` nunca se solicita (001H).

## 4. Checkpoints

| Archivo (no versionado; `*.dmck` en `.gitignore`) | Formato / ABI | Estado |
|---|---|---|
| `dmc_checkpoint_mainmenu_v1.dmck` | formato v1 (build ID estricto) | inválido tras cualquier relink (ver GAMEPLAY_001) |
| `dmc_checkpoint_mainmenu_v2.dmck` | formato v2, ABI 1 | rechazado por el runtime ABI 2 (`checkpoint runtime ABI mismatch`): no contiene `SdtAddr` |
| `dmc_checkpoint_mainmenu_abi2.dmck` | formato v2, **ABI 2**; SHA-256 `88fe10cd…7078`; `compat=c3db86c0…4fdc` | **operativo** (001F–001H); creado por cold boot el 2026-10-11 |

- Formato v2 ([DEVSTATE_003D_CHECKPOINT_COMPATIBILITY_ID.md](DEVSTATE_003D_CHECKPOINT_COMPATIBILITY_ID.md)):
  - el gate estricto usa una identidad de compatibilidad de estado;
  - el build ID del ejecutable es solo procedencia;
  - un relink no semántico no invalida el checkpoint.
- ABI 2 ([GAMEPLAY_001E_CDMODULE_MIXED_EE_IOP_SPU.md](GAMEPLAY_001E_CDMODULE_MIXED_EE_IOP_SPU.md)):
  `kCheckpointRuntimeAbiVersion = 2` y `kIopSectionVersion = 2`, porque el estado de CDMODULE incluye
  `SdtAddr`. Incluido en el parche 14.
- Procedimiento:
  - cargar con `DMC_DEVSTATE_LOAD=<archivo.dmck>`;
  - guardar en Main Menu con F9 en una sesión de cold boot ([DEVSTATE_003D](DEVSTATE_003D_CHECKPOINT_COMPATIBILITY_ID.md));
  - autotests con `DMC_CHECKPOINT_SELFTEST=1` (97 PASS / 0 FAIL en el ejecutable reproducible `7110206b…`).
- **Regla:** cualquier cambio del estado serializado exige cambiar la ABI. No aceptar checkpoints antiguos con
  semántica nueva.

## 5. CDMODULE (HLE IOP, `vendor/PS2Recomp/ps2xIOP/src/modules/cdmodule.cpp`)

| RPC / modo | Semántica | Estado | Versionado |
|---|---|---|---|
| `fno=2 mode=1` | lectura lineal; BLOCKER_002 | validado | **sí** (parche BLOCKER_002) |
| `fno=2 mode=1` no alineado | DMA y `recv` redondeados a 16 (`0x118`) | local | no |
| `fno=9/0xA/0xC/0xD` | película/PSS; BLOCKER_003 | validado | **sí** |
| `fno=2 mode&2` tipo 0/3 (EE) | cabecera más descriptores (`0xFF`) | validado contra oráculo (001C/001D) | no |
| `fno=2 mode&2` tipo 1 (IOP) y 2 (SPU) | `0x103`; `SdtAddr` desde `fno=1` | destino, tamaño y `recv` iguales al oráculo; bytes en ejecución no comparados | no |
| `mode&8` (`mode=0xA`) | `TsndPortInit`, selección de slots | `0x1B9 recv=0x470A00` = ORIGINAL | no |
| `fno=8` | devuelve `EE_TransSize` | local | no |
| `sid=0x87654321 fno=0x100` | SeReplace | local; sin oráculo | no |

- **HECHO:** el modelo estático reproduce 14/14 `recv` ORIGINAL capturados.
- RPC todavía no manejados en la ruta actual: `(sid 0x0, 0x0)`, `(sid 0x1, 0x2)` y `(sid 0x1, 0x80000006)`
  (SDRDRV `SdrGetState`).

## 6. R5900 / VU0

- 20 funciones VU0 sustituidas por `ret0` durante el bring-up.
- Estado detallado, grupos A/B/C y prioridades en
  [GAMEPLAY_001_RET0_RESTORATION_STATUS.md](GAMEPLAY_001_RET0_RESTORATION_STATUS.md).
- Defectos del traductor (sin parche): CLIP, MAC, escritura a `vf0`, DIV/0, SQRT, FTOI y redondeo. Ver
  [GAMEPLAY_001F_RET0_BATCH_AUDIT.md](GAMEPLAY_001F_RET0_BATCH_AUDIT.md).
- Defecto de runtime: **`vf0 = (0,0,0,0)`** en todos los hilos salvo el principal.
  - Causa: `StartThread` e invocaciones `GsCallback` crean contextos con `memset 0` (HECHO, 001I).
  - En hardware `vf0 = (0,0,0,1)` es constante.
  - Con el experimento `DMC_VF0_FIX`, `_capVu0ecossin` coincide bit a bit con el modelo independiente
    (600/600). La corrección sigue siendo **experimental**.

## 7. VIF1 / VU1 / GS

- **Causa raíz demostrada (001H):**
  - DMC usa cadenas DMA VIF1 con `CHCR.TTE = 1` (`0x145`).
  - El runtime descartaba los 64 bits altos (dos VIFcodes) de las etiquetas REF; 2271 de 2454 contenían
    comandos.
  - Parser independiente: 56 838 anomalías en el flujo del runtime y 0 en el flujo con semántica TTE.
- **Corrección permanente (001I):** `patches/GAMEPLAY_001I_vif1_tte_all_tag_ids.patch` (parche 15).
  - Misma semántica que upstream `75d729c` (#244).
  - Incluye la conservación de TTE en `sceDmaSend` y tests.
  - Entrada 15 de `upstream.lock.json` (`patched_commit` `6bea2eb0…`, reproducido de forma limpia).
  - `ps2x_tests`: +3 tests nuevos que pasan; mismo conjunto de fallos preexistentes que sin el parche.
  - Se retiraron el conmutador `DMC_VIF_TTE_FIX` y los diagnósticos de 001H.
- Sin desbordes XGKICK ni agotamiento de presupuesto VU1 con la corrección.
- **HECHO:** 0 cadenas VIF1 con TTE=0 pierden datos con la semántica nueva (medición de la película más la
  escena).
- Residual UNKNOWN: `kMaxChainTags = 4096` trunca cadenas más largas.

## 8. Próximos bloqueadores (orden por evidencia)

1. **Pantalla oscura tras la fachada** (VISUAL_003, observación del usuario), que impide demostrar el primer
   frame jugable. Fase GAMEPLAY_001J.
2. **`vf0` en hilos:** convertir el experimento en cambio general (HECHO de causa; validado en ecossin).
3. **`ret0` en la ruta por frame:**
   - `katVu0Clip_ld` (CLIP) y `katVu0SetClip`;
   - `_capVu0ecossin` (tras `vf0`);
   - `Get_light_str`.

   Corrección visual o de lógica pendiente (INFERENCIA).
4. **Defectos generales del traductor VU0** (CLIP/MAC/DIV/SQRT/FTOI/escritura a `vf0`): ya afectan a código
   nativo activo (HECHO).
5. **SDRDRV `SdrGetState`:** divergencia abierta, no causal para `0x232` (HECHO).
6. **Verificación visual del primer frame jugable y rendimiento de la escena** (UNKNOWN; el runtime no tiene
   volcado de framebuffer).

## 9. Próxima fase: GAMEPLAY_001J (propuesta, no iniciada)

1. Corrección general de `vf0`:
   - inicializar en `StartThread` e invocaciones (o en el constructor si se justifica la recompilación);
   - descartar escrituras a `vf0` en el traductor.
2. Después, A/B de `_capVu0ecossin` (`ret0` frente a nativa) con la corrección común en ambos brazos.
3. Parche general del traductor para CLIP (`katVu0Clip_ld`) y oráculo de una llamada.
4. Mecanismo de evidencia visual (volcado de framebuffer) para poder evaluar el primer frame jugable.

GAMEPLAY_001I (TTE, Add/Sub, sonda `vf0`) está **cerrada**:
[GAMEPLAY_001I_TTE_PROMOTION_ADD_SUB_VF0.md](GAMEPLAY_001I_TTE_PROMOTION_ADD_SUB_VF0.md).

## 10. Estado remoto frente a estado local (verificado 2026-10-14, cierre de 001I)

- `origin/master = 6cc8ce8`; la rama documental `docs/gameplay-001-history` (`da7ec4d`) está publicada pero no
  integrada.
- `master = 0cc5243` (PR #1 documental fusionado). El trabajo técnico de 001D–001I está publicado en la rama
  `gameplay-001i` (`7e7e65b`, `22088d0`, `5cee31b` + documentación), **sin merge**.
- Vendor `HEAD = 0efd17c`. El árbol vendor publicado equivale a baseline + 13 parches; se verificó
  reconstruyéndolo y comparándolo archivo por archivo.

| Elemento | Implementado localmente | Validado localmente | Versionado en Git | Publicado en GitHub |
|---|---|---|---|---|
| BLOCKER_001–004, checkpoint v1/v2 (DEVSTATE) | sí | sí | sí | sí |
| Notas GAMEPLAY_001 a 001H | sí | — | sí (`da7ec4d`) | sí, en `master` (PR #1) |
| Notas GAMEPLAY_001I y actualizaciones | sí | — | sí | rama `gameplay-001i` |
| CDMODULE `mode&2/8`, `0x103`, `0x118`, `fno=1/8`, SeReplace + ABI 2 (parche 14) | sí | sí | sí (`7e7e65b`) | rama `gameplay-001i` |
| Parche VIF1 TTE (parche 15) | sí | sí (tests, 7 ejecuciones completas) | sí (`22088d0`) | rama `gameplay-001i` |
| `capVu0MulMatrix2`, `capVu0Add/SubVectorXYZ` nativas (TOML 17 `ret0@`, overrides 17) | sí | sí | sí (`5cee31b`) | rama `gameplay-001i` |
| Modo sombra / `DMC_RET0_NATIVE` / sombra de ecossin | copia en `analysis/local/gameplay_001j/` | diagnóstico | **no** (excluido a propósito) | **no** |
| Experimento `vf0` (`DMC_VF0_PROBE` / `DMC_VF0_FIX`) | retirado del vendor; diff en `analysis/local/gameplay_001i/` | sí (ecossin 600/600) | **no** | **no** |
| Pendiente | merge de `gameplay-001i` a `master`; diseño general de `vf0` | | | |

Consecuencia: un clon de la rama `gameplay-001i` reproduce el avance hasta la película `0x232` y la escena
posterior. Verificado: el replay de los 15 parches coincide archivo por archivo con el vendor usado para el
ejecutable `7110206b…`. El código generado se regenera desde `recomp/symtabfirst.toml`, que contiene 17 `ret0`.

## 11. Cómo reproducir

Requiere los archivos originales (ELF, ISO), que el repositorio **no** contiene.

```powershell
python scripts/pipeline.py bootstrap      # vendor = baseline + patches del lock
python scripts/pipeline.py tools
python scripts/pipeline.py identify original/SLES_503.58
python scripts/pipeline.py prepare --elf original/SLES_503.58   # --toml por defecto: recomp/symtabfirst.toml
python scripts/pipeline.py generate
python scripts/pipeline.py build
python scripts/pipeline.py run --seconds 60
```

`pipeline.py` sincroniza el código generado en `analysis/local/symtabfirst/generated` y compila en
`analysis/local/symtabfirst/build` ([SYMTAB_BASELINE_PROMOTION.md](SYMTAB_BASELINE_PROMOTION.md)).

Pasos locales del mantenedor (scripts bajo `analysis/local/`, no versionados):

- **Build incremental:** `cmake --build analysis/local/symtabfirst/build --config Debug --target ps2EntryRunner --parallel 4`
  (`build_canonical.ps1`). **No** usar MSBuild directo: provocó recompilación masiva.
- **Ejecución por consola sin automatizar la GUI:** `run_progress.py <tag> <segundos>`, con estas variables:

| Variable | Valor |
|---|---|
| `DMC_DEVSTATE_LOAD` | `<checkpoint ABI 2>` |
| `PS2X_CD_IMAGE` | `<ruta a la ISO>` |
| `PS2X_RUNTIME_ARENA_BASE` | `0x009FA000` |
| `PS2X_RUNTIME_ARENA_LIMIT` | `0x00ABE000` |
| `DMC_P313_INIT_MODE` | `ownership` |
| `DMC_P314_SYNC_REDECODE` | `1` |
| `DMC_P311_PAD_FILE` | `<archivo de pad>` (máscara hexadecimal; Cross para NEW GAME) |

- **Experimentos:** ya no están en el código publicado. `DMC_RET0_NATIVE` y `DMC_RET0_SHADOW*` están en
  `analysis/local/gameplay_001j/dmc_overrides.cpp.diag_001g_001i`; `DMC_VF0_PROBE` y `DMC_VF0_FIX`, en
  `analysis/local/gameplay_001i/G001I_VF0_EXPERIMENT.diff`.
- **Retirados en 001I:** `DMC_VIF_TTE_FIX` (el TTE ya es permanente) y los diagnósticos de 001H
  (`DMC_VIF1_DIAG`, `DMC_GS_WATCHDOG`, `DMC_HEARTBEAT`, `DMC_LOOP_PROFILE`, `DMC_DIAG_VU1_SKIP`). Siguen
  disponibles en `analysis/local/gameplay_001h/G001H_TEMP_INSTRUMENTATION.diff`.
- **Reproducción del patchset:** `analysis/local/gameplay_001i/patch_replay.py` replica `apply_patchset` en un
  clon aparte y verifica `patched_commit`, sin tocar `vendor/`.
- **Selftests:** `DMC_CHECKPOINT_SELFTEST=1`.

Evidencia no reproducible sin el entorno original: savestates y capturas PCSX2 (oráculos 001C/001E/RET0),
volcados de memoria y checkpoints `.dmck`.

## 12. Política documental (obligatoria)

**Ninguna fase de investigación se considera documentalmente cerrada si sus hallazgos solo existen en
`analysis/local/`.** Al terminar cada fase:

1. guardar las evidencias detalladas en `analysis/local/`;
2. consolidar los hallazgos en `analysis/notes/` (HECHO/INFERENCIA/HIPÓTESIS/UNKNOWN/RETRACTADO, rutas
   relativas, hashes);
3. actualizar este documento;
4. actualizar [RESEARCH_INDEX.md](RESEARCH_INDEX.md);
5. preparar el checkpoint documental en Git (solo documentación, nunca datos del juego);
6. publicarlo cuando esté autorizado.
