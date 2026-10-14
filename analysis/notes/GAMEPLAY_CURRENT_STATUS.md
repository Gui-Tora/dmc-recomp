# Estado actual del proyecto — punto de entrada para nuevos agentes

Última actualización: 2026-10-14, al cierre documental de GAMEPLAY_001H.
Índice histórico completo: [RESEARCH_INDEX.md](RESEARCH_INDEX.md).
Reglas del proyecto: [AGENTS.md](../../AGENTS.md) (obligatorias) y [README.md](../../README.md).

> **Aviso:** el estado del **árbol de trabajo local** del mantenedor está bastante por delante de lo que hay en
> Git/GitHub. Lee primero la sección «Estado remoto frente a estado local».
> **El juego no es jugable.** El primer frame jugable **no** está demostrado.

Categorías: HECHO, INFERENCIA, HIPÓTESIS, UNKNOWN, RETRACTADO.

## 1. Identidad y arquitectura

| Elemento | Valor |
|---|---|
| Juego | Devil May Cry (2001), PS2, **PAL Europa**, v1.02 (`SYSTEM.CNF`: `VER = 1.02`, `VMODE = PAL`) |
| ELF | `SLES_503.58`; SHA-256 `d0753a6b3b2f00802a50758a872d8cf051725aa31c839aa8894eee30ce58bab4`; CRC32 `0x77654AD2`; entry `0x00100008` |
| `CDMODULE.IRX` | SHA-256 `9d51f16d1ed861e508daa1389e6aaf4fe597687b3f0cd5feb920e62a479b34fa` |
| `TSNDDRVM.IRX` | SHA-256 `c00b17f07aa0dfed089a517498074eec251c7b571f58a7649414ef88be7f31fd` |
| Recompilador | `ran-j/PS2Recomp`, baseline `14b1e5cb39b4af7e6fc12f9a29fdc751efde49d7` + 13 parches (`upstream.lock.json`, `patched_commit` sintético `dcd03052…`) |
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
| Checkpoint de desarrollo Main Menu | versionado (formato v2, ABI 1); ABI 2 **solo local** |
| CDMODULE `fno=2 mode=1/2/8/0xA`, `fno=1/8`, SeReplace | **implementado y validado localmente**; no versionado |
| Película `0x232` tras NEW GAME | reproducida **solo con experimento local TTE** |
| Escena posterior a la película | ejecución por frames observada (INFERENCIA); **no validada visualmente** |
| Audio | no audible en ningún punto del proyecto (observación del usuario) |
| 20 `ret0` VU0 | 1 restaurada localmente; 19 pendientes ([GAMEPLAY_001_RET0_RESTORATION_STATUS.md](GAMEPLAY_001_RET0_RESTORATION_STATUS.md)) |

## 3. Última ejecución comprobada (local, 2026-10-13)

- **Ejecutable:** `860d7a055cab34a79c10c636f8ae5fc8e31cf9549481c1fc6e85f3353c0f5c78`.
- **Condiciones:** checkpoint ABI 2 y `DMC_VIF_TTE_FIX=1`. Detalle en
  [GAMEPLAY_001H_VIF1_TTE_ROOT_CAUSE.md](GAMEPLAY_001H_VIF1_TTE_ROOT_CAUSE.md).

Secuencia:

1. Carga del checkpoint en Main Menu (`CKPT:LOAD` compatible) y Cross por archivo PAD (NEW GAME).
2. CDMODULE carga 24 recursos, idénticos a ORIGINAL hasta `0x32`: `0x52 0x50 0x51 0x54 0x53 0xFE 0xFF 0x108 0x103 0x4F 0x118 0x14E 0x119 0x11A 0x5A 0x1B9 0x2E 0x31 0x36 0x3C 0x3A 0x33 0x44 0x32`.
3. `SndSceneBgmStart → SdrGetState(0x80000006)`: RPC no manejado, RECOMP devuelve 0 y ORIGINAL 1.
4. Película `0x232` (`MOVIE:start lba=0x215524 totalSize=0x490C004`): leída completa, más de 1350 fotogramas
   MPEG decodificados, 0 errores VU1.
5. Después de la película: `MPEG:delete`, inicialización de escena (`Get_light_str` ×20 000) y bucle por frame
   del hilo 12 (~5500 `katVu0Clip_ld` por tick). Ritmo ~2 ticks/s al final de la ventana de 320 s.

**Sin el experimento TTE:** después de `0x32` el GS se queda bloqueado en un paquete DIRECT falso de ~55 KiB y
`0x232` nunca se solicita.

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
  `SdtAddr`. **Solo local.**
- Procedimiento:
  - cargar con `DMC_DEVSTATE_LOAD=<archivo.dmck>`;
  - guardar en Main Menu con F9 en una sesión de cold boot ([DEVSTATE_003D](DEVSTATE_003D_CHECKPOINT_COMPATIBILITY_ID.md));
  - autotests con `DMC_CHECKPOINT_SELFTEST=1` (97 PASS / 0 FAIL en el ejecutable `860d7a05…`).
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
- Defecto de runtime: **`vf0.w = 0`** en contextos de hilo (`StartThread`). En hardware `vf0 = (0,0,0,1)`
  es constante.

## 7. VIF1 / VU1 / GS

- **Causa raíz demostrada (001H):**
  - DMC usa cadenas DMA VIF1 con `CHCR.TTE = 1` (`0x145`).
  - El runtime descartaba los 64 bits altos (dos VIFcodes) de las etiquetas REF; 2271 de 2454 contenían
    comandos.
  - Parser independiente: 56 838 anomalías en el flujo del runtime y 0 en el flujo con semántica TTE.
- **Corrección:** semántica de upstream `75d729c` (#244).
  - Validada en 4/4 ejecuciones como experimento local (`DMC_VIF_TTE_FIX=1`).
  - Parche propuesto `analysis/local/gameplay_001h/PROPOSED_VIF_TTE_ALL_TAG_IDS.patch`.
  - **No promovido** a `patches/` ni a `upstream.lock.json`.
- Sin desbordes XGKICK ni agotamiento de presupuesto VU1 con la corrección.
- Residual UNKNOWN: `kMaxChainTags = 4096` trunca cadenas más largas.

## 8. Próximos bloqueadores (orden por evidencia)

1. **Promover la corrección TTE.** Sin ella, la ejecución se bloquea en `0x32` (HECHO).
2. **`vf0.w = 0` en hilos.** Afecta a todo código VU0 que use `vf0.w` (HECHO en ecossin; INFERENCIA general).
3. **`ret0` en la ruta por frame:** `katVu0Clip_ld` (CLIP), `katVu0SetClip`, `_capVu0ecossin`, Add/Sub y
   `Get_light_str`. Corrección visual o de lógica pendiente (INFERENCIA).
4. **Defectos generales del traductor VU0** (CLIP/MAC/DIV/SQRT/FTOI/`vf0`): ya afectan a código nativo activo
   (HECHO).
5. **SDRDRV `SdrGetState`:** divergencia abierta, no causal para `0x232` (HECHO).
6. **Rendimiento de la escena posterior a la película** (~2 ticks/s en Debug) y verificación visual del primer
   frame jugable (UNKNOWN).

## 9. Próxima fase: GAMEPLAY_001I (propuesta, no iniciada)

1. Promover el parche TTE a `patches/` + `upstream.lock.json` (cambio general documentado) y retirar los
   diagnósticos temporales de 001H.
2. Experimento independiente: `vf0 = (0,0,0,1)` en contextos `StartThread` y al restaurar el checkpoint.
   Después, repetir la sombra de ecossin contra el modelo.
3. A/B Add/Sub con TTE como base común en ambos brazos.

No mezclar 2 y 3 en la misma prueba experimental.

## 10. Estado remoto frente a estado local (verificado 2026-10-14)

`HEAD = origin/master = 6cc8ce8`. Vendor `HEAD = 0efd17c`. El árbol vendor publicado equivale a
baseline + 13 parches; se verificó reconstruyéndolo y comparándolo archivo por archivo.

| Elemento | Implementado localmente | Validado localmente | Versionado en Git | Publicado en GitHub |
|---|---|---|---|---|
| BLOCKER_001–004, checkpoint v1/v2 (DEVSTATE) | sí | sí | sí | sí |
| Notas GAMEPLAY_001 (180 s, 001C, 001D, RET0, 001E, 001G) | sí | — | **solo con el commit documental de 2026-10-14** | **en la rama `docs/gameplay-001-history`** (pendiente de merge) |
| CDMODULE `mode&2/8`, `0x103`, `0x118`, `fno=1/8`, SeReplace | sí (`cdmodule.cpp`, `builtin_profiles.cpp`, `module_factories.h`) | sí | **no** | **no** |
| ABI 2 del checkpoint (`checkpoint_format.h`) | sí | sí (97/0) | **no** | **no** |
| `capVu0MulMatrix2` nativa (TOML 19 `ret0@`, overrides) | sí | sí (oráculo) | **no** (en Git hay 20 `ret0`) | **no** |
| Modo sombra / `DMC_RET0_NATIVE` / shadow ecossin | sí (`runtime/`) | diagnóstico | **no** | **no** |
| Corrección VIF1 TTE | sí, experimental (`DMC_VIF_TTE_FIX`) | sí (4/4) | **no** | **no** |
| Diagnósticos 001H (VU1STAT, heartbeat, watchdog GS, VIF1 dump) | sí, temporales | — | **no** (deben retirarse) | **no** |
| Pendiente de promoción | parches CDMODULE/ABI2, TTE, `capVu0MulMatrix2` | | | |

Consecuencia: un clon limpio de GitHub reproduce el estado de **Main Menu** (DEVSTATE/BLOCKER_004), **no** el
avance de GAMEPLAY_001D–001H. Los hallazgos están documentados aquí; el código correspondiente debe promoverse
mediante parches revisados.

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

- **Experimentos:** `DMC_VIF_TTE_FIX=1`, `DMC_RET0_NATIVE=<stubs>`, `DMC_RET0_SHADOW=<stubs>` con
  `DMC_RET0_SHADOW_LOG` y `DMC_RET0_SHADOW_MAX`.
- **Diagnósticos:** `DMC_VIF1_DIAG=<dir>`, `DMC_GS_WATCHDOG=1`, `DMC_HEARTBEAT`, `DMC_LOOP_PROFILE`.
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
