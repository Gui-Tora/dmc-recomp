# GAMEPLAY_001F — Auditoría conjunta de los 19 `ret0`, defectos del traductor VU0 y diagnóstico CDMODULE `0x118` / `mode=0xA`

Fecha de la fase: 2026-10-12. Consolidación permanente: 2026-10-14.
Informe local: `analysis/local/gameplay_001f/RET0_BATCH_AUDIT.md` (no versionado).
Fase **solo de auditoría**: sin cambios de runtime, código generado, TOML ni overrides.

## 1. Objetivo

Clasificar los 19 `ret0` restantes para decidir cómo restaurarlos. Además, diagnosticar los dos bloqueos
CDMODULE siguientes: `0x118` (tamaño no alineado) y `0x1B9` (`mode=0xA`).

## 2. Estado inicial y problema

- **HECHO:** `RET0_COUNT = 19` en local, porque `capVu0MulMatrix2` ya estaba validada en
  [GAMEPLAY_001_RET0_AUDIT.md](GAMEPLAY_001_RET0_AUDIT.md). Exe canónico `02f8282c…170d` (build 001E).
- **HECHO:** desde el checkpoint ABI 2 la ejecución pasaba `0x103` y se detenía en `0x118` (rechazado por
  tamaño no alineado) y después en `unsupported fno=2 mode=10 size=0x471800`.

## 3. Evidencias utilizadas

- Cuerpos nativos generados sin `ret0` (`analysis/local/ret0_audit/native_generated/`) y `elf_audit.json`
  (`.symtab`, `JAL` estáticos).
- Semántica de referencia: `pcsx2/VUops.cpp` (`_vuCLIP`, `_vuDIV`, `_vuSQRT`, `floatToInt`), solo lectura
  estática. PCSX2 no se ejecutó.
- Una ejecución por consola desde ABI 2 (`ret0_hits_run.{log,json}`).
- Asm del IRX y log PINE ORIGINAL de 001C (`recv` capturados).
- Scripts:
  - `ret0_static_audit.py` → `ret0_static_audit.json` (SHA-256 `edacfc72…78ec`);
  - `vu0_dataflow.py`;
  - `run_ret0_hits.py`;
  - `cdmodule_static_model.py` → `cdmodule_model_vs_original.json`.

## 4. Primera divergencia identificada

Fase de auditoría: no se buscó una divergencia ORIGINAL/RECOMP nueva. **HECHO:** el primer `ret0` alcanzado
después de `0x103` es `capVu0AddVectorXYZ` (#1, `ra=0x253D14`, `Get_speed`); le siguen `capVu0SubVectorXYZ`
y `_capVu0ecossin`.

## 5. Hipótesis investigadas

- Las 19 funciones pueden restaurarse en lote con su cuerpo nativo generado.
- El rechazo de `0x118` debe resolverse con DMA redondeado a 16.
- `mode=0xA` equivale a `mode&2` más el efecto de `mode&8`.

## 6. Hipótesis descartadas y motivo

- **Descartada:** «restaurar en lote es seguro». El traductor VU0 tiene defectos generales (§7). Hay que
  agrupar las funciones según los opcodes que usan.
- **Matizada en 001G:** la primera clasificación (Grupo A=10, B=7, C=2) se corrigió a **A=9, B=8, C=2** con
  un inventario de opcodes v2. `Calc_scr_light` pasa de A a B por usar `vftoi0`. Ver
  [GAMEPLAY_001G_CDMODULE_AND_RET0_SHADOW.md](GAMEPLAY_001G_CDMODULE_AND_RET0_SHADOW.md).

## 7. Defectos del traductor VU0 (HECHO: código generado frente a `VUops.cpp`)

1. **`vclipw`:** el código generado pone bit0 = `x < −w` y bit1 = `x > +w`, y compara con `w`. PCSX2 usa
   bit0 = `x > +|w|` y bit1 = `x < −|w|`, compara con `|w|` y aplica umbral de denormales. La polaridad está
   invertida y falta `|w|`.
2. **Flags MAC nunca calculados:** ningún código generado escribe `ctx->vu0_mac_flags`. `cfc2 $vi17` siempre
   lee 0.
3. **Escrituras a `vf0` no descartadas:** por ejemplo `vsub.xyzw $vf0, …` asigna `ctx->vu0_vf[0]`. En
   hardware, `vf0` es constante `(0,0,0,1)`.
4. **`vdiv` con divisor 0:** da `Q = 0`. PCSX2 da `±0x7F7FFFFF` según los signos.
5. **`vsqrt`:** el código generado calcula `sqrtf(max(0, ft))`. PCSX2 calcula `sqrt(fabs(ft))`.
6. **`vftoi*`:** usa `cvttps`, que con desbordamiento positivo da `0x80000000`. PCSX2 satura a `0x7FFFFFFF`.
7. No hay configuración `MXCSR` ni redondeo hacia cero: SSE redondea al más cercano y conserva denormales.
   **UNKNOWN:** la distancia en ULP frente a ORIGINAL.

**HECHO:** estos defectos ya afectan a código nativo activo fuera de los 19:

- `atari_check_4` y `atari_check_4R` escriben `vf0` y leen MAC;
- `katVu0Clip2_st`, `katVu0Clip_shw_tmc`, `katVu0Clip_shw_wc` y `strip_draw_polyline` usan `vclipw`;
- `capVu0Atan2`, `eff2d_draw_poly_3d`, `strip_draw_3d`, `strip_draw_simple_3d`, entre otras, usan `vdiv`.

**INFERENCIA:** son defectos generales del traductor upstream. Según [AGENTS.md](../../AGENTS.md), deben ir
como parches generales en `patches/`, separados de DMC.

**HECHO (001F):** upstream `main = 2c5fbb9` no corrige ninguno de estos defectos. **UNKNOWN:** el estado de
upstream posterior a esa revisión.

Un defecto adicional de **runtime** (no del traductor) se encontró en 001H: `vf0.w = 0` en contextos de hilo.
Ver [GAMEPLAY_001H_VIF1_TTE_ROOT_CAUSE.md](GAMEPLAY_001H_VIF1_TTE_ROOT_CAUSE.md).

## 8. Modificaciones

Ninguna. Solo se crearon scripts, JSON y logs nuevos bajo `analysis/local/gameplay_001f/`.

## 9. Pruebas realizadas

- Auditoría estática de los 19 cuerpos nativos: las 19 son **funciones hoja** (sin `jal`/`jalr`), sin
  marcadores *unhandled* y con el final exacto de `.symtab`.
- Ejecución `ret0_hits_run` (exe `02f8282c…`, ABI 2), en este orden:
  1. LOAD OK y Cross;
  2. `0xFF recv=0xC5F20`;
  3. `0x108`;
  4. `0x103 blocks=3 recv=0x4D7D0`;
  5. `capVu0AddVectorXYZ #1`, `capVu0SubVectorXYZ #1`, `_capVu0ecossin #1/#50`;
  6. `0x4F`;
  7. `0x118` no alineado;
  8. `0x14E`, `0x119`, `0x11A`, `0x5A`;
  9. RPC no manejado `sid=0x87654321 rpc=0x100`;
  10. `unsupported fno=2 mode=10 size=0x471800`;
  11. RPC no manejado `fno=8`.
- Modelo estático CDMODULE (`mode&2` + `mode&8` + redondeo por trozos de `0x60000`) frente a ORIGINAL:
  **14/14** `recv` coincidentes (`0xFF, 0x103, 0x4F, 0x11A, 0x5A, 0x1B9, 0x2E, 0x31, 0x36, 0x3C, 0x3A, 0x33, 0x44, 0x32`).

## 10. Resultados verificados

- **`0x118`** (HECHO):
  - El request es `LBA=0xED0C0` (`/DATA/STATUS/MOYA9.IPS`), `size=0x23FB4`, `dst=0x1E00000`, `mode=1`.
  - El IRX hace DMA de `(size+15) & ~15` y suma el tamaño redondeado a `EE_TransSize`.
  - **INFERENCIA:** ORIGINAL escribe `0x23FC0` bytes y `recv = 0x23FC0`.
- **`0x1B9` `mode=0xA`** (HECHO):
  - El request es `LBA=0x14D801` (`/DATA/FSD/R002.FSD`), `size=0x471800`, `dst=0x1095D00`, `request[0x10]=0`.
  - La cabecera tiene un solo descriptor tipo 0 de `0x470A00`.
  - ORIGINAL `recv = 0x470A00`.
  - `mode&8` añade `TsndPortInit(port,0)` para los puertos 0 y 1 cuando `request[0x10] >> (8·port) == 0`, y
    la selección de pares de descriptores tipo 1 por slot.
- Clasificación de los 19 (versión final corregida en 001G):
  [GAMEPLAY_001_RET0_RESTORATION_STATUS.md](GAMEPLAY_001_RET0_RESTORATION_STATUS.md).
- Primer candidato seguro: `capVu0AddVectorXYZ` + `capVu0SubVectorXYZ` (5 instrucciones,
  `lqc2/vadd|vsub.xyz/sqc2`, 523/592 llamadores estáticos).

## 11. Limitaciones y UNKNOWN

- Los conteos de llamadas `ret0` tienen granularidad 1/50.
- El `recv` ORIGINAL de `0x118` no se capturó.
- La semántica completa de `TsndPortInit`, `fno=8` y `sid 0x87654321` quedó pendiente en esta fase
  (resuelta estáticamente en 001G).
- El dataflow VU0 de las funciones con ramas es aproximado: `vu0_dataflow.json` no distingue ramas.

## 12. Estado actual

La auditoría está cerrada. Las recomendaciones de 001F se ejecutaron en 001G (CDMODULE `0x118`/`mode&8`/
`fno=8`/SeReplace y shadow Add/Sub). El parche general del traductor VU0 está **pendiente** y no se ha
implementado.

## 13. Archivos de código relacionados

- Traductor upstream: `vendor/PS2Recomp/ps2xRecomp/src/lib/vu_translator.cpp` y `code_generator.cpp`.
- `runtime/dmc_overrides.cpp`.
- `recomp/symtabfirst.toml` (19 `ret0@` en local).

## 14. Evidencia local

`analysis/local/gameplay_001f/`:

- `ret0_static_audit.{py,json}`, `vu0_dataflow.{py,json}`;
- `run_ret0_hits.py`, `ret0_hits_run.{log,json}`;
- `cdmodule_static_model.py`, `cdmodule_model_vs_original.json`.

Fuentes reutilizadas:

- `analysis/local/ret0_audit/` (`elf_audit.json`, SHA-256 `299e89de…27e8`);
- `analysis/local/gameplay_001c/pine_oracle.log`;
- `analysis/local/gameplay_001e/asm/`.

## 15. Relación con otras fases

- Anterior: [GAMEPLAY_001E_CDMODULE_MIXED_EE_IOP_SPU.md](GAMEPLAY_001E_CDMODULE_MIXED_EE_IOP_SPU.md).
- Siguiente: [GAMEPLAY_001G_CDMODULE_AND_RET0_SHADOW.md](GAMEPLAY_001G_CDMODULE_AND_RET0_SHADOW.md).
