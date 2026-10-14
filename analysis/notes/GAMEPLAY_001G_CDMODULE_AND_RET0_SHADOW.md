# GAMEPLAY_001G — CDMODULE `0x118` / `mode=0xA` / `fno=8` / SeReplace y modo sombra `ret0`

Fecha de la fase: 2026-10-12. Consolidación permanente: 2026-10-14.
Informes locales: `analysis/local/gameplay_001g/REPORT_GAMEPLAY_001G.md` y `SHADOW_ADD_SUB.md` (no versionados).

> **Estado:** los cambios de CDMODULE y el modo sombra existen **solo en local** (vendor dirty y
> `runtime/dmc_overrides.cpp` sin commit). No hay parche en `patches/` ni publicación.

## 1. Objetivo

Implementar desde el binario de los IRX las rutas CDMODULE que 001F dejó diagnosticadas, avanzar hasta la
siguiente diferencia ORIGINAL/RECOMP y crear una infraestructura de **modo sombra** para validar los cuerpos
nativos `ret0` sin cambiar la ejecución real.

## 2. Estado inicial y problema

- **HECHO:** desde ABI 2, RECOMP se detenía en `0x118` (`mode=1` no alineado) y en `mode=0xA` (`0x1B9`).
  Había dos RPC no manejados: `sid=0x87654321 rpc=0x100` y `fno=8`.

## 3. Evidencias utilizadas

- Bytes y asm de `CDMODULE.IRX` y `TSNDDRVM.IRX` (hashes en
  [GAMEPLAY_001E_CDMODULE_MIXED_EE_IOP_SPU.md](GAMEPLAY_001E_CDMODULE_MIXED_EE_IOP_SPU.md)).
- Log PINE ORIGINAL de 001C (`recv` de 14 recursos).
- Cuerpos nativos generados sin `ret0` (`analysis/local/ret0_audit/native_generated/`).
- Modelo float32 independiente (`numpy`) para Add/Sub.

## 4. Primera divergencia identificada

- **HECHO:** con ABI 2, la secuencia de recursos RECOMP coincide con ORIGINAL hasta `0x32`, con 14/14 `recv`
  iguales donde existe captura ORIGINAL (`recv_recomp_vs_original.json`).
- **HECHO (primera diferencia observable):** después de `0x32`, ORIGINAL solicita la película `0x232`.
  RECOMP no la solicita en 100–120 s y registra:
  1. RPC no manejado `sid=0x1 rpc=0x80000006` (`SndSceneBgmStart → SdrGetState`);
  2. `[VU1 reserved lower] pc=0x2F0 0xFFFFFFFB` (6 veces);
  3. llamadas masivas a `ret0`: `Get_light_str` ≥9900, `capVu0AddVectorXYZ` ≥1450,
     `capVu0SubVectorXYZ` ≥1200, `_capVu0ecossin` ≥850, `katVu0Setmat4_shw` ≥1.
- La causa quedó **UNKNOWN** en esta fase. Se resolvió en
  [GAMEPLAY_001H_VIF1_TTE_ROOT_CAUSE.md](GAMEPLAY_001H_VIF1_TTE_ROOT_CAUSE.md).

## 5. Hipótesis investigadas

- Cuál de los tres síntomas anteriores impide `0x232` (sin resolver en 001G).
- Si los cuerpos nativos de Add/Sub son correctos (validado en sombra).

## 6. Hipótesis descartadas y motivo

- **RETRACTADO (en 001H):** «el fallback de `SdrGetState` lee como estado su propio argumento (eco)». Con
  `send == recv` el fallback pone a cero `recv`. Ver la nota de 001H.
- **INVALIDADO:** las capturas pasivas `shadow1_frame_*.png` mostraban el escritorio, no la ventana del juego,
  y se borraron. El estado visual posterior a `0x1B9` quedó UNKNOWN.

## 7. Causa raíz (de los bloqueos CDMODULE)

- **`0x118`, HECHO (IRX):**
  - `cdfunc fno=2 → CdReadProcess(fno, req, 0)` sintetiza un descriptor tipo 0.
  - `trans_mem_to_ee` hace DMA de `round_up(chunk,16)` y suma el redondeo a `EE_TransSize`.
  - El HLE local rechazaba los tamaños no alineados.
- **`mode=0xA`, HECHO (IRX):**
  - `TsndPortInit@0x6390` = `port_stop` + `DelayATick(1)` + `port_state[port]=value`; retorna 0.
  - `CdReadProc` con `mode&8`:
    - `TsndPortInit(port,0)` para los puertos 0 y 1 según `req[0x10]` (`0x8B8–0x8DC`);
    - selección de tipo 1 con `slot<4` (`0x91C–0x9F4`);
    - salto de pares de `slot==6` (`0xAF4–0xB58`).
- **`fno=8`, HECHO:** tabla `0x2798 → 0x18CC` devuelve `EE_TransSize`.
- **`sid=0x87654321 rpc=0x100`, HECHO:** `SeReplaceProcess@0x16E4` / `SeReplaceProc@0x14E4`:
  - toma una tabla de 0x800 B desde EE (`req+0xC`);
  - selecciona el par `u16 req+2`;
  - copia en trozos ≤ `0x8000`;
  - aplica `trans_process_tbl` con slot de llamada 1;
  - devuelve `EE_TransSize`.

## 8. Modificaciones (locales, no versionadas)

- `vendor/PS2Recomp/ps2xIOP/src/modules/cdmodule.cpp`:
  - `mode=1` con DMA y respuesta redondeados a 16, comprobando la cola contra la imagen;
  - análisis completo de la tabla con selección `mode&8`;
  - `TsndPortInit` como reinicio del estado de puerto (no hay backend TSND);
  - `fno=8` = `EE_TransSize` modelado (no serializado para no cambiar la ABI 2);
  - segundo servidor `sid=0x87654321 fno=0x100` (SeReplace) con el helper tipo 1/2 de `0x103`.
- `runtime/dmc_overrides.cpp` y `runtime/dmc_ret0_shadow_bodies.inc` (sin trackear):
  - `DMC_SHADOWED_RET0`: copia el contexto, ejecuta el cuerpo nativo sobre una RDRAM *scratch*, registra
    JSONL y deja la ejecución real en `ret0`;
  - el `.inc` se deriva **mecánicamente** con `analysis/local/gameplay_001g/gen_shadow_bodies.py` (no es
    edición manual de código generado).
- Variables de entorno: `DMC_RET0_SHADOW=<stubs>`, `DMC_RET0_SHADOW_LOG=<jsonl>`, `DMC_RET0_SHADOW_MAX`
  (por defecto 200).

## 9. Pruebas realizadas

- Build:
  - **HECHO:** MSBuild directo con entorno `VsDevCmd` empezó a recompilar todo (~700 unidades antes de
    detenerlo).
  - `cmake --build analysis/local/symtabfirst/build --config Debug --target ps2EntryRunner --parallel 4`
    (`build_canonical.ps1`) fue incremental: 0 unidades generadas recompiladas.
  - **Regla desde entonces:** usar siempre `build_canonical.ps1`.
- Exe final `5becd13521b36b6a4c7306ddcbb862558c3415c0511f7996e57a71fda1c44a8b`. Selftests 97/0.
- Ejecuciones `progress1` (avance CDMODULE) y `shadow1` (sombra).

## 10. Resultados verificados

- `0x118`: `dmaSize=0x23FC0`, cola de 12 bytes a cero que coincide con la imagen (`tailVerify=image-exact`).
  **UNKNOWN:** el `recv` ORIGINAL de `0x118`.
- `0x1B9`: `blocks=1 bytesRead=0x470A00 selection=0x0 portInit0 portInit1 recv=0x470A00`, igual que ORIGINAL.
- SeReplace: `pair=0 table=0x10524D0 type1(slot=7,size=0x610) type2(slot=7,size=0x208E0) ret=0x43800`.
  **UNKNOWN:** validación ORIGINAL.
- `0xFF` y `0x103` siguen devolviendo `0xC5F20` y `0x4D7D0`.
- Modo sombra Add/Sub (`shadow_add_sub.jsonl`, SHA-256 `4a29ad8f…ded1`; 400 registros, 0 omitidos):

| Función | EXACT | ULP_ONLY | SEMANTIC_DIFF | `a0` nativo ≠ efecto `ret0` |
|---|---:|---:|---:|---:|
| capVu0AddVectorXYZ | 200 | 0 | 0 | 116 |
| capVu0SubVectorXYZ | 200 | 0 | 0 | 191 |

- **Inventario de opcodes v2** (`ret0_opcode_inventory_v2.json`, SHA-256 `bdeacf54…94b7`): corrige 12 filas
  de 001F.
  - Rabbitizer imprime `vclipw`/`vsqrt` como `.word`.
  - `_capVu0ecossin` usa `vsqrt`.
  - `katVu0ViewVolumeClip`, `ClipCheck2` y `katVu0Clip_ld` usan `vclipw`.
  - `Calc_scr_light` usa `vftoi0` y pasa de A a B.
  - `Calc_md` usa `vftoi0/4`.
  - **Grupos finales: A=9, B=8, C=2.**

## 11. Limitaciones y UNKNOWN

- La sombra compara contra un modelo IEEE con redondeo al más cercano. **No demuestra equivalencia con
  ORIGINAL**: no hay captura PCSX2 de Add/Sub.
- Solo cubre las 200 primeras llamadas por función (llamador `Get_speed`).
- Los efectos de sonido de `port_stop`/`DelayATick` no están modelados.

## 12. Estado actual

- CDMODULE hasta `0x32`: implementado y coincidente con ORIGINAL donde hay captura. **Local, no versionado.**
- Sombra Add/Sub: diagnóstico temporal activo. `ret0` se mantiene.

## 13. Archivos de código

`vendor/PS2Recomp/ps2xIOP/src/modules/cdmodule.cpp`, `runtime/dmc_overrides.cpp`,
`runtime/dmc_ret0_shadow_bodies.inc`.

## 14. Evidencia local

`analysis/local/gameplay_001g/`:

- `progress1_run.{log,json}`, `shadow1_run.{log,json}`;
- `shadow_add_sub.jsonl`, `compare_shadow_add_sub.py`, `shadow_add_sub_compare.json`;
- `recv_recomp_vs_original.json`;
- `ret0_opcode_inventory_v2.{py,json}`, `ret0_opcode_inventory_changes.json`;
- `gen_shadow_bodies.py`;
- `baseline/` (copias de los archivos tocados), `*.diff`;
- `build_canonical.ps1`, `checkpoint_selftest.log`.

## 15. Relación con otras fases

- Anterior: [GAMEPLAY_001F_RET0_BATCH_AUDIT.md](GAMEPLAY_001F_RET0_BATCH_AUDIT.md).
- Siguiente: [GAMEPLAY_001H_VIF1_TTE_ROOT_CAUSE.md](GAMEPLAY_001H_VIF1_TTE_ROOT_CAUSE.md).
