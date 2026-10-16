# Estado actual del proyecto — punto de entrada para nuevos agentes

Última actualización: 2026-10-15, cierre de GAMEPLAY_001J (TSNDDRVM/XAG, la cinemática de la oficina ya no se detiene, preparación de v0.1.0-alpha) — [GAMEPLAY_001J_VISUAL_TRANSITION_DIAGNOSIS.md](GAMEPLAY_001J_VISUAL_TRANSITION_DIAGNOSIS.md).
Índice histórico completo: [RESEARCH_INDEX.md](RESEARCH_INDEX.md).
Reglas del proyecto: [AGENTS.md](../../AGENTS.md) (obligatorias) y [README.md](../../README.md).

> **Aviso:** `master` (`f3c4e33`, PR #2) contiene el avance de 001D–001I. El trabajo de 001J (parches 16–18,
> notas) está en la rama local `gameplay-001j`, preparado para PR y pendiente de autorización. Lee la sección «Estado remoto frente a estado local».
> **El juego no es jugable.** El primer frame jugable **no** está demostrado.

Categorías: HECHO, INFERENCIA, HIPÓTESIS, UNKNOWN, RETRACTADO.

## 1. Identidad y arquitectura

| Elemento | Valor |
|---|---|
| Juego | Devil May Cry (2001), PS2, **PAL Europa**, v1.02 (`SYSTEM.CNF`: `VER = 1.02`, `VMODE = PAL`) |
| ELF | `SLES_503.58`; SHA-256 `d0753a6b3b2f00802a50758a872d8cf051725aa31c839aa8894eee30ce58bab4`; CRC32 `0x77654AD2`; entry `0x00100008` |
| `CDMODULE.IRX` | SHA-256 `9d51f16d1ed861e508daa1389e6aaf4fe597687b3f0cd5feb920e62a479b34fa` |
| `TSNDDRVM.IRX` | SHA-256 `c00b17f07aa0dfed089a517498074eec251c7b571f58a7649414ef88be7f31fd` |
| Recompilador | `ran-j/PS2Recomp`, baseline `14b1e5cb39b4af7e6fc12f9a29fdc751efde49d7` + parches de `upstream.lock.json`. `master`: 15 parches (`patched_commit` `6bea2eb0…`). Rama `gameplay-001j`: **18 parches** (`patched_commit` `585c347e4cf3e26c804b561b3f2d5aa27b0fe1ae`), vendor idéntico byte a byte al replay limpio |
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
| Escena posterior a la película | cinemática en tiempo real de la oficina **ya no se detiene**: con el parche 17 (TSNDDRVM) `Event_voice_call` encuentra el stream XAG listo y, en una corrida Release de ~46 min, el script llegó a su final (tick 32366); sigue el vídeo `0x231`, la carga del escenario siguiente y el bloqueo `sceDmaRecvI` (IPU/scratchpad en `Movie_texture`). Defectos visuales: oscuridad, tinte verde, huecos, circuito 1 mal colocado (VISUAL_001/004/005/006) |
| Audio | **voz/pista XAG de la cinemática audible** (parche 17; confirmado por el usuario 2026-10-15), a velocidad real y por tanto desincronizada con un juego más lento; efectos (TQ), BGM y audio de películas PSS siguen mudos |
| 20 `ret0` VU0 | 3 restauradas localmente (`capVu0MulMatrix2`, `capVu0AddVectorXYZ`, `capVu0SubVectorXYZ`); **17 pendientes** ([GAMEPLAY_001_RET0_RESTORATION_STATUS.md](GAMEPLAY_001_RET0_RESTORATION_STATUS.md)) |

## 3. Última ejecución comprobada (local, 2026-10-15, estado publicado)

- **Ejecutables reproducibles** (vendor = base + 18 parches, sin instrumentación):
  - Release: `a1934eb32f7967b8a59512aff1909c18abc1e4f847fc5f9f9faade5231956d25`;
  - Debug: `b35b2f558fe4dafd8679625a0880015666fc4c1fae723db8e750a1c516c491c9`.
- **Selftests:** 97/0. `ps2x_tests`: 268 PASS, mismos fallos preexistentes.
- **Prueba de humo** (`clean_smoke_release`):
  - checkpoint `dmc_checkpoint_mainmenu_abi2_tsnd_v1.dmck`, un Cross;
  - NEW GAME y película `0x232` completa;
  - `Event_voice_call` → stream XAG (fichero `0x1B8`, bloques `[40,+272)`) en standby y reproducción;
  - audio encolado completo.
- **Corrida larga de referencia** (`tsnd_release_60m`, ~46 min reales):
  1. El script de la cinemática avanza sin detenerse.
  2. Final por script en el tick 32366 (`Event_skip(3)` → `XagStop`).
  3. Vídeo `0x231`, carga de `0x1D4`/`0x2E`/`0x31`/`0x33`/`0x32` y `SdrBgmReq`.
  4. `Unimplemented PS2 stub called: sceDmaRecvI` (PC `0x3FEE34`, `Movie_texture`).
- **Rendimiento:** ~3–4 VSync/s en escenas 3D (Release); el intérprete VU1 ocupa ~83 % del hilo del juego.

## 4. Checkpoints

| Archivo (no versionado; `*.dmck` en `.gitignore`) | Formato / ABI | Estado |
|---|---|---|
| `dmc_checkpoint_mainmenu_v1.dmck` | formato v1 (build ID estricto) | inválido tras cualquier relink (ver GAMEPLAY_001) |
| `dmc_checkpoint_mainmenu_v2.dmck` | formato v2, ABI 1 | rechazado por el runtime ABI 2 (`checkpoint runtime ABI mismatch`): no contiene `SdtAddr` |
| `dmc_checkpoint_mainmenu_abi2.dmck` | formato v2, **ABI 2**; SHA-256 `88fe10cd…7078`; `compat=c3db86c0…4fdc` | operativo hasta 001I; **no contiene el estado TSNDDRVM**: los comandos de inicialización de `SndInit` se perdieron, así que no sirve para la voz XAG |
| `dmc_checkpoint_mainmenu_abi2_tsnd_v1.dmck` | formato v2, ABI 2; SHA-256 `5fc03336…c09e`; tick 20153 | **operativo desde 001J**: cold boot con el servicio TSNDDRVM (`TsndInitXag 0x70000 2/2`, `XagSetBuff 0x30000`); validado cargándolo en un proceso nuevo |

- Formato v2 ([DEVSTATE_003D_CHECKPOINT_COMPATIBILITY_ID.md](DEVSTATE_003D_CHECKPOINT_COMPATIBILITY_ID.md)):
  - el gate estricto usa una identidad de compatibilidad de estado;
  - el build ID del ejecutable es solo procedencia;
  - un relink no semántico no invalida el checkpoint.
- ABI 2 ([GAMEPLAY_001E_CDMODULE_MIXED_EE_IOP_SPU.md](GAMEPLAY_001E_CDMODULE_MIXED_EE_IOP_SPU.md)):
  `kCheckpointRuntimeAbiVersion = 2` y `kIopSectionVersion = 2`, porque el estado de CDMODULE incluye `SdtAddr`.
  Incluido en el parche 14.
- El servicio TSNDDRVM serializa su propio estado (payload v2). Un checkpoint sin esa sección carga el servicio en
  su estado inicial.
- **Procedimiento:**
  - cargar con `DMC_DEVSTATE_LOAD=<archivo.dmck>`;
  - guardar con F9 o con el disparador `PS2X_CHECKPOINT_TRIGGER_FILE` (parche 18);
  - fijar el destino con `PS2X_CHECKPOINT_SAVE_PATH`; nunca sobrescribe.
- **Autotests:** `DMC_CHECKPOINT_SELFTEST=1` (97 PASS / 0 FAIL).
- **Checkpoint de final de intro:** no se pudo crear. El punto seguro lo rechaza con `CDMODULE: movie stream active`
  entre el final de `0x231` y el bloqueo; ver la nota de 001J.
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
  - **Parche 16 (local, sin commit)**: corrección general en `StartThread`, en las invocaciones y en
    lectura/escritura del checkpoint; el recompilador descarta las escrituras a `vf0`. Probado (tests,
    selftests 97/0) y equivalente al experimento `DMC_VF0_FIX`, que se retiró. Con él, `_capVu0ecossin`
    coincide bit a bit con el modelo (600/600).

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

1. **`sceDmaRecvI` / `Movie_texture`:** decodificación IPU por hardware (toIPU → IPU → scratchpad → fromSPR) usada
   por niebla, fondos y pantallas de estado. El runtime no emula IPU ni DMA de scratchpad.
   - Decisión tomada: HLE de `Movie_texture` específica de DMC con FFmpeg (`AV_CODEC_ID_IPU`).
   - Hay un borrador **no validado ni integrado** en `analysis/local/gameplay_001j/provisional_ipu/`.
2. **Rendimiento:** con el intérprete VU1 (por defecto), ~3,3 VSync/s en la oficina.
   - **GAMEPLAY_001K (local, en revisión):** programas VU1 compilados, generados localmente desde el ELF del
     usuario (`tools/vu1_replay/make_vu1c.py`, parche 19), llevan la oficina a ~12,5 VSync/s con equivalencia
     verificada.
   - El mayor coste pasa a ser el GS por software (~31 ms por VSync).
   - Ver [GAMEPLAY_001K_VU1_PERFORMANCE_DESIGN.md](GAMEPLAY_001K_VU1_PERFORMANCE_DESIGN.md).
3. **Checkpoint posterior a la intro:** bloqueado por `CDMODULE: movie stream active` tras `0x231`. Causa UNKNOWN.
4. **Audio:** sincronizar el stream XAG con el tiempo emulado (borrador no verificado preservado); efectos (TQ),
   BGM y audio PSS.
5. **Defectos visuales de la oficina** (VISUAL_001/004/005/006). Requieren oráculo PCSX2.
6. **Defectos generales del traductor VU0** (CLIP/MAC/DIV/SQRT/FTOI) y 17 `ret0` pendientes.

## 9. Próxima fase (propuesta, no iniciada)

1. Revisar y publicar GAMEPLAY_001K (programas VU1 compilados; parche 19).
2. **GAMEPLAY_001L:** superar `sceDmaRecvI` / `Movie_texture` (HLE con FFmpeg; borrador en
   `analysis/local/gameplay_001j/provisional_ipu/`).

GAMEPLAY_001J está documentada en
[GAMEPLAY_001J_VISUAL_TRANSITION_DIAGNOSIS.md](GAMEPLAY_001J_VISUAL_TRANSITION_DIAGNOSIS.md).

## 10. Estado remoto frente a estado local (verificado 2026-10-15, cierre de 001J)

- `origin/master = f3c4e33`: PR #2 fusionado (parches 14 y 15, 17 `ret0`, notas de 001I). Sin releases ni tags.
- La rama local `gameplay-001j` (desde `f3c4e33`) contiene el trabajo de 001J **sin commit**, pendiente de
  autorización.

| Elemento | Implementado localmente | Validado localmente | Versionado en Git | Publicado en GitHub |
|---|---|---|---|---|
| BLOCKER_001–004, DEVSTATE, notas hasta 001I | sí | sí | sí | sí (`master`) |
| CDMODULE + ABI 2 (parche 14), VIF1 TTE (parche 15), 17 `ret0` | sí | sí | sí | sí (`master`) |
| Parche 16 `vf0` | sí | sí (tests, selftests, equivalencia) | **no** (pendiente) | **no** |
| Parche 17 TSNDDRVM/XAG (estado, stream, audio) | sí | sí (tests 268, corridas, audio confirmado por el usuario) | **no** (pendiente) | **no** |
| Parche 18 ruta F9 y disparador de checkpoint | sí | sí (creó `..._abi2_tsnd_v1.dmck`) | **no** (pendiente) | **no** |
| Notas 001J, README, CHANGELOG | sí | — | **no** (pendiente) | **no** |
| Ritmo de audio por tiempo emulado | borrador | **no** | no (preservado en `analysis/local`) | no |
| HLE de `Movie_texture` / decodificador IPU | borrador | **no** | no (preservado en `analysis/local`) | no |
| Instrumentación de 001J (GS, GIF, render, perfilador, VU1, traza de sonido) | retirada | — | no | no |

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
