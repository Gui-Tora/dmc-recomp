# GAMEPLAY_001 — Estado de restauración de las 20 funciones `ret0`

Documento de seguimiento permanente. Última verificación: 2026-10-14 (cierre de GAMEPLAY_001I), contra el
árbol de trabajo local; `origin/master = 6cc8ce8`.

Historia y metodología:

- [GAMEPLAY_001_RET0_AUDIT.md](GAMEPLAY_001_RET0_AUDIT.md): inventario `.symtab` y RET0_FIRSTCALL.
- [GAMEPLAY_001F_RET0_BATCH_AUDIT.md](GAMEPLAY_001F_RET0_BATCH_AUDIT.md): grupos y defectos del traductor.
- [GAMEPLAY_001G_CDMODULE_AND_RET0_SHADOW.md](GAMEPLAY_001G_CDMODULE_AND_RET0_SHADOW.md): sombra e inventario v2.
- [GAMEPLAY_001H_VIF1_TTE_ROOT_CAUSE.md](GAMEPLAY_001H_VIF1_TTE_ROOT_CAUSE.md): A/B Add/Sub previo, ecossin y `vf0`.
- [GAMEPLAY_001I_TTE_PROMOTION_ADD_SUB_VF0.md](GAMEPLAY_001I_TTE_PROMOTION_ADD_SUB_VF0.md): restauración permanente de Add/Sub y experimento `vf0`.

## Resumen verificado

| Concepto | Local (árbol de trabajo) | GitHub (`origin/master` = `6cc8ce8`) |
|---|---|---|
| Funciones sustituidas originalmente | 20 | 20 |
| `ret0@` en `recomp/symtabfirst.toml` | **17** | **20** |
| `registerFunction` en `runtime/dmc_overrides.cpp` | 17 | 20 |
| `capVu0MulMatrix2` | nativa, validada contra ORIGINAL | todavía `ret0` |
| `capVu0AddVectorXYZ`, `capVu0SubVectorXYZ` | nativas (001I) | todavía `ret0` |
| Grupo A / B / C (inventario v2, 001G) | 9 / 8 / 2 | — (documentado aquí) |

**HECHO:** las restauraciones de `capVu0MulMatrix2` (RET0_FIRSTCALL) y de `capVu0AddVectorXYZ` y
`capVu0SubVectorXYZ` (GAMEPLAY_001I) están **versionadas** en la rama `gameplay-001i` (commit `5cee31b`: TOML
con 17 `ret0@` y overrides con 17 stubs instrumentados), pendientes de merge a `master`. Los cuerpos nativos se
regeneran desde el TOML. **`RET0_COUNT = 17`.**

**Nota de historia:** la primera clasificación de 001F era A=10, B=7, C=2. 001G la corrigió a **A=9, B=8,
C=2**, porque `Calc_scr_light` usa `vftoi0`.

## Constantes de hardware y defectos que condicionan la restauración

- **HECHO (hardware):** `vf0` es constante `(0, 0, 0, 1)`. Las escrituras a `vf0` se ignoran.
- **HECHO (RECOMP, 001H):** en 600/600 llamadas shadow reales de `_capVu0ecossin` (hilo 12), `vf0.w = 0`.
- **HECHO (001I, causa):** `StartThread` (`EeScheduler.cpp`) crea el contexto con `R5900Context{}` (`memset 0`) sin
  fijar `vf0`, y lo mismo ocurre con las invocaciones `GuestInvocation`. Tras cargar el checkpoint solo el hilo 1
  tiene `vf0 = (0,0,0,1)`. Los hilos creados después de la carga también nacen con `vf0 = 0`. El checkpoint
  conserva el valor, pero no lo origina.
- **HECHO (001I, experimento `DMC_VF0_FIX`):** con `vf0` forzado a la constante hardware, el cuerpo nativo de
  ecossin coincide bit a bit con el modelo independiente en seno y coseno en 600/600 casos.
- **HECHO (traductor, 001F, frente a `pcsx2/VUops.cpp`):**

| Defecto | Comportamiento generado | Referencia PCSX2 |
|---|---|---|
| CLIP (`vclipw`) | bit0 = `x<−w`, bit1 = `x>+w`; usa `w` | bit0 = `x>+\|w\|`, bit1 = `x<−\|w\|`; usa `\|w\|`; umbral de denormales |
| MAC | `vu0_mac_flags` nunca se escribe (`cfc2 vi17` = 0) | flags calculados por operación |
| `vf0` | las escrituras a `vf0` modifican `vu0_vf[0]` | `vf0` constante |
| DIV (`vdiv`) | divisor 0 → `Q = 0` | `±0x7F7FFFFF` según los signos |
| SQRT (`vsqrt`) | `sqrtf(max(0, ft))` | `sqrt(fabs(ft))` |
| FTOI (`vftoi*`) | `cvttps`: desbordamiento positivo → `0x80000000` | satura a `0x7FFFFFFF` |
| Redondeo | SSE al más cercano, denormales conservados | VU redondea hacia cero, sin Inf/NaN (UNKNOWN en ULP) |

## Tabla de seguimiento

«Cuerpo nativo» significa que existe en `analysis/local/ret0_audit/native_generated/`, generado sin `ret0`:
20/20 emitidos, 0 decode fallidos, 0 *unhandled*, fin exacto de `.symtab`. «Llamadas observadas» son los
máximos registrados con el experimento TTE (`ttefixC16`, granularidad 1/50) o en logs anteriores.

| # | Nombre | Dirección ELF | Grupo | Estado actual | Cuerpo nativo | Defectos del traductor / runtime | Resultado de pruebas | Evidencia ORIGINAL | Prioridad | Siguiente paso |
|---:|---|---|:-:|---|---|---|---|---|:-:|---|
| 1 | capVu0MulMatrix2 | `0x1A0080` | — | **NATIVA VALIDADA** (local; ret0 en GitHub) | sí, sincronizado en el árbol generado local | ninguno usado | primera llamada: AFTER de 64 B = oráculo (`oracle_compare.json`) | **sí**: 64 B BEFORE/AFTER, primera llamada | — | publicar TOML/overrides cuando se autorice código |
| 2 | capVu0AddVectorXYZ | `0x19FBB0` | A | **NATIVA (001I, local)** | sí, sincronizado (SHA-256 `eb4d4c4c…`) | ninguno | sombra 200/200 EXACT; A/B 2+2 con TTE permanente: sin regresiones; diverge (de forma determinista) el camino de `Coat_matrix_calc`; exe final probado | no | — | publicar cuando se autorice código; oráculo ORIGINAL opcional |
| 3 | capVu0SubVectorXYZ | `0x19FBD0` | A | **NATIVA (001I, local)** | sí, sincronizado (SHA-256 `78e5be78…`) | ninguno | sombra 200/200 EXACT; A/B 2+2 sin regresiones (6750 llamadas en ambos brazos); exe final probado | no | — | ídem |
| 4 | _capVu0ecossin | `0x1A03C0` | A* | ret0 + sombra VU0 | sí | **SQRT**; depende de **`vf0.w = 1`** | sin corrección: coseno incorrecto (199/600). Con `DMC_VF0_FIX`: seno y coseno 600/600 bit a bit = modelo (001I); sin casos `1−s²<0` | no | 1 | hacer permanente la corrección `vf0` (cambio general); después A/B ret0 frente a nativa |
| 5 | katVu0SetClip | `0x132D70` | A† | ret0 | sí | ninguno (8 `lqc2`) | ≥6150 llamadas por frame tras la película | no | 3 | restaurar junto a su consumidora `katVu0Clip_ld` |
| 6 | katVu0Clip_ld | `0x132DA0` | B | ret0 | sí | **CLIP** | ≥251 550 llamadas (~5500/tick) | no | 3 | corregir `vclipw` en el traductor (parche general) y oráculo de una llamada |
| 7 | Get_light_str | `0x16D8E0` | A | ret0 | sí | `vmaxx` no ejercitado en otro código | ≥20 600 llamadas; inicialización de escena normal | no | 4 | sombra con modelo de referencia |
| 8 | katVu0Setmat4_shw | `0x1DA7B0` | A† | ret0 | sí | ninguno (16 `lqc2`) | ≥100 llamadas | no | 5 | restaurar junto a `katVu0RotTransPers_shw` |
| 9 | capVu0MulMatrix_r | `0x1A0180` | A | ret0 | sí | ninguno (mismos opcodes que MulMatrix2) | no observada | no | 6 | sombra cuando se observe |
| 10 | capCopyMatrix | `0x1A01F0` | A | ret0 | sí | ninguno (`lqc2`/`sqc2`) | no observada | no | 6 | ídem |
| 11 | capVu0ScaleVectorXYZ2 | `0x19FC40` | A | ret0 | sí | ninguno | no observada | no | 6 | ídem |
| 12 | Calc_scr_light | `0x16D920` | B | ret0 | sí | **FTOI** (`vftoi0`); variantes de campo no ejercitadas | no observada | no | 7 | parche FTOI, luego sombra |
| 13 | katVu0ViewVolumeClip | `0x132D20` | B | ret0 | sí | **CLIP** + `cfc2` | no observada | no | 7 | parche CLIP |
| 14 | ClipCheck2 | `0x132EC0` | B | ret0 | sí | **CLIP** + `cfc2 vi18` | no observada | no | 7 | parche CLIP |
| 15 | capVu0Compare | `0x1A0300` | B | ret0 | sí | **MAC** + escritura a **`vf0`** | no observada | no | 7 | parche MAC + descarte de escrituras a `vf0` |
| 16 | katVu0RotTransPersQ | `0x132EF0` | B | ret0 | sí | **DIV** + **FTOI** | no observada | no | 8 | parches DIV/FTOI |
| 17 | sceVu0SetRotTransPers | `0x1D5E40` | B | ret0 | sí | **DIV** + **FTOI** | no observada | no | 8 | parches DIV/FTOI |
| 18 | katVu0RotTransPers_shw | `0x1DA8E0` | B | ret0 | sí | **DIV** + FTOI (`vftoi4`) | no observada | no | 8 | parches; restaurar con `katVu0Setmat4_shw` |
| 19 | InterpNode | `0x132DD0` | C | ret0 | sí | DIV, FTOI; MMI `prot3w`, `vabs` no ejercitados | no observada | no | 9 | **oráculo ORIGINAL** antes de restaurar |
| 20 | Calc_md | `0x16B8E0` | C | ret0 | sí | DIV, FTOI; MMI `pext5`/`pcgtb` no ejercitados en ningún otro código | no observada | no | 9 | **oráculo ORIGINAL** antes de restaurar |

\* `_capVu0ecossin` está en el Grupo A por opcodes, pero su `vsqrt` y su dependencia de `vf0.w` la
condicionan a la corrección de `vf0` (001H).
† Productoras de estado VU0: restaurarlas solas no cambia nada observable hasta que se restauren sus consumidoras.

**INFERENCIA (prioridad):** el orden propuesto se basa en alcanzabilidad observada (ruta por frame después de
la película) y en el riesgo del grupo. No es un hecho demostrado de impacto visual.

## Procedimiento de restauración permanente (vigente)

1. Corpus en sombra (`DMC_RET0_SHADOW`) comparado con un modelo independiente (`EXACT` / `ULP_ONLY` /
   `SEMANTIC_DIFF`).
2. A/B real con `DMC_RET0_NATIVE=<stub>` desde el mismo checkpoint, cambiando una sola variable.
3. Oráculo PCSX2 puntual si la función tiene semántica dudosa (Grupo C o casos límite).
4. Retirada de `ret0@` en `recomp/symtabfirst.toml` y del override; regeneración **solo** del archivo afectado;
   build incremental con `build_canonical.ps1`; ejecutable final probado.
5. Documentarlo aquí y en [GAMEPLAY_CURRENT_STATUS.md](GAMEPLAY_CURRENT_STATUS.md).

No declarar una función recuperada de forma permanente hasta que el ejecutable final esté construido y probado.
