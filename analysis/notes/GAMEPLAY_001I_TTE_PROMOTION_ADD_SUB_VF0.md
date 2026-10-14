# GAMEPLAY_001I — Promoción VIF1 TTE, restauración de Add/Sub y experimento `vf0`

Fecha: 2026-10-14. Informe local detallado: `analysis/local/gameplay_001i/REPORT_GAMEPLAY_001I.md` (no versionado).
Categorías: **HECHO**, **INFERENCIA**, **HIPÓTESIS**, **UNKNOWN**, **RETRACTADO**.

> **Estado:** todo lo descrito existe en el árbol de trabajo local, en la rama local `gameplay-001i`, **sin
> commit ni push**. El commit y el push de código requieren autorización explícita.
>
> - **TTE:** promovido a `patches/` y a `upstream.lock.json`.
> - **Add/Sub:** restauradas de forma permanente en TOML y overrides.
> - **`vf0`:** sigue siendo experimental.
> - **Primer frame jugable:** no demostrado.

## 1. Objetivo

1. Convertir en parche permanente la corrección VIF1 TTE de
   [GAMEPLAY_001H_VIF1_TTE_ROOT_CAUSE.md](GAMEPLAY_001H_VIF1_TTE_ROOT_CAUSE.md).
2. Hacer un A/B de `capVu0AddVectorXYZ` y `capVu0SubVectorXYZ` con el TTE permanente como base común.
3. Investigar y probar de forma aislada el defecto `vf0.w = 0`.

## 2. Estado inicial

- **HECHO:** el PR documental `docs/gameplay-001-history` (`da7ec4d`) no estaba integrado en `master`
  (`origin/master = 6cc8ce8`).
- Para no cambiar de rama con código sin commit, el trabajo se hizo en una rama local nueva,
  `gameplay-001i`, creada desde la rama documental. Todos los cambios sin commit (CDMODULE, ABI 2, VU0) se
  conservaron.
- **HECHO:** la corrección TTE existía solo como experimento local (`DMC_VIF_TTE_FIX`).

## 3. Evidencias utilizadas

- Árbol publicado reconstruido de forma limpia: baseline más los 13 parches. Reproduce exactamente
  `patched_commit = dcd03052`.
- Precedente upstream `ran-j/PS2Recomp` `75d729c` (PR #244): diff de `ps2_memory.cpp`, de `Support.h` y de
  sus tests.
- Suite `ps2x_tests` compilada sobre los árboles publicado y parcheado.
- Corridas por consola desde el checkpoint ABI 2, con el mismo ejecutable y la misma configuración en
  ambos brazos.
- Sombra pasiva de `_capVu0ecossin` y modelo independiente `ecossin_ref_model.py`.

## 4. Primeras divergencias / hallazgos

- **HECHO (riesgo detectado al portar):** el stub HLE `sceDmaSend` del árbol publicado arranca las cadenas
  con `CHCR = 0x185`, es decir, **sin TTE**. Con la semántica TTE estricta, una cadena VIF1 lanzada por ese
  stub perdería los VIFcodes de las etiquetas, que el comportamiento anterior sí entregaba para
  cnt/next/call/ret/end.
  - Upstream resolvió esto en el mismo commit conservando el TTE ya configurado en el registro del canal
    (en libdma el canal es el propio registro hardware).
  - Se portó también ese cambio.
- **HECHO (medición `tte0diagD1`, TTE activo, película más 300 s de escena):** **0** cadenas VIF1 con TTE=0
  contienen etiquetas cnt/next/call/ret/end con mitad alta no nula. La semántica nueva no hace perder datos
  que antes se entregaban en esta ruta.
- **HECHO (`vf0`, sonda `vf0V1`):** después de cargar el checkpoint, solo el hilo principal (1) tiene
  `vf0 = (0,0,0,1)`.
  - Todos los demás hilos tienen `vf0 = (0,0,0,0)`: 2, 10–17, incluido el 12 (`Game_main`).
  - También los creados después de la carga, como el 16 (`Movie_ctrl`) y el 17 (`MpegMovieDecode`).
  - Las invocaciones `GsCallback` (`Main_Get_Vsync`) nacen con `vf0 = 0`. Las `RpcCallback` (`cb_sifRpc`)
    heredan el contexto del hilo padre.

## 5. Hipótesis investigadas

- H1: el parche TTE solo afecta a etiquetas REF en cadenas con TTE=1. **Confirmada** con tests y medición.
- H2: la restauración nativa de Add/Sub no produce regresiones. **Confirmada** en los hitos medidos.
- H3: `vf0.w = 0` procede de la restauración del checkpoint. **Descartada como causa única**: hilos creados
  después de la carga también nacen con `vf0 = 0`.
- H4: `vf0.w = 0` procede de la creación de contextos. **Confirmada**: `StartThread` usa `R5900Context{}`
  y `GuestInvocation` usa `R5900Context context{}`; el constructor hace `memset 0`. Solo el contexto
  principal (`ps2_runtime.cpp`) y la copia de estado de microprograma VU0 fijan `vf0`. El checkpoint
  guarda y restaura `vu0_vf` tal cual, así que perpetúa el valor.

## 6. Hipótesis descartadas y motivo

- H3, por la evidencia de §4.
- Escrituras del código generado a `vf0`: solo `atari_check_4` y `atari_check_4R` asignan
  `ctx->vu0_vf[0]` (defecto del traductor de 001F). No explican el valor nulo inicial de todos los hilos.

## 7. Causas raíz

- **TTE:** demostrada en 001H y corregida aquí de forma permanente (§8).
- **`vf0` (HECHO):** inicialización de contexto en `EeScheduler.cpp` (`StartThread`, invocaciones).
  Corregirlo en el constructor de `R5900Context` (`ps2_runtime.h`) obligaría a recompilar las ~10 000
  unidades generadas, porque es una cabecera compartida. Por eso el experimento se hizo en el punto de
  despacho del scheduler.

## 8. Modificaciones

| Cambio | Estado |
|---|---|
| `patches/GAMEPLAY_001I_vif1_tte_all_tag_ids.patch` (SHA-256 `6958ff08…9c7d`): TTE para todo ID de etiqueta en canales VIF; `sceDmaSend` conserva el `CHCR.TTE` del canal; 3 tests nuevos; 3 tests VIF1 previos pasan a `CHCR=0x144` (como en upstream) | **promovido** (local, sin commit) |
| `upstream.lock.json`: entrada 14 y `patched_commit = b3cb0bd77fe15e2dc706ebf3f265c340a8becc8d` | actualizado (local) |
| `patches/README.md`: entrada 14 | actualizado (local) |
| Conmutador `DMC_VIF_TTE_FIX` y diagnósticos de 001H (VIF1 dump, VU1STAT, heartbeat, watchdog GS, VU1 skip, loop profile, ruta GIF) | **retirados** del vendor; los archivos coinciden byte a byte con «publicado + parche 14» |
| `recomp/symtabfirst.toml`: quitados `ret0@0x0019FBB0` y `ret0@0x0019FBD0` → **17 `ret0@`** | permanente (local) |
| `runtime/dmc_overrides.cpp`: sin override de Add/Sub (17 registros); `dmc_ret0_shadow_bodies.inc` regenerado mecánicamente solo con ecossin | permanente (local) |
| Árbol generado activo: solo se sincronizaron `capVu0AddVectorXYZ_0x19fbb0.cpp` y `capVu0SubVectorXYZ_0x19fbd0.cpp`, regenerados con 17 `ret0` (SHA-256 `eb4d4c4c…` y `78e5be78…`, idénticos a los validados en sombra) | sin edición manual |
| `EeScheduler.cpp`: `DMC_VF0_PROBE` (registro) y `DMC_VF0_FIX` (fuerza `vf0 = (0,0,0,1)` antes de cada despacho) | **experimento temporal**, desactivado por defecto |

La regeneración de 17 `ret0` frente a la de 19 (RET0_FIRSTCALL) solo difiere en los dos cuerpos y en
`ps2_recompiled_stubs.h`, que tiene declaraciones redundantes. No se sincronizó para no recompilar las
unidades que lo incluyen.

## 9. Pruebas realizadas

- **Reproducción limpia** (`patch_replay.py`, que replica `apply_patchset`):
  - 13 parches → `dcd03052` (= lock anterior);
  - 13 + el nuevo → `b3cb0bd7` (= lock nuevo, `matches_lock: true`).
  - El vendor de trabajo no se tocó.
- **Tests** (`ps2x_tests`, mismo arnés en ambos árboles):

| Árbol | PASS | Tests fallidos (nombres) | Aborto |
|---|---:|---|---|
| publicado (13) | 261 | 4 | aserción del scheduler en la suite `PS2SifDma` |
| publicado + parche 14 | 264 | 4 (los mismos) | el mismo |

  - Los 4 tests fallidos son preexistentes y ajenos al parche: `VU0 macro mappings…`, dos de
    `sceGsSetDefDBuffDc…` y `latched host presentation…`.
  - Los 3 tests nuevos pasan:
    - `VIF1 DMA chain transfers REF tag high bytes when TTE is enabled`;
    - `… ignores tag high bytes when TTE is disabled`;
    - `sceDmaSend preserves guest-configured VIF1 TTE`.
- **Build canónico incremental** (`cmake --build … --target ps2EntryRunner`): ~10 unidades del runtime, más
  después 2 cuerpos generados y `dmc_overrides.cpp`. 0 regeneraciones masivas.
- **Selftests del checkpoint:** 97/0 con el ejecutable `8ddd8759…` y 97/0 con el final `d80ac6f2…`.
- **A/B Add/Sub** (exe `8ddd8759dc5cdd18cadd5695c2dfa39acd4d895013f2b20eed91602232155f80`, ABI 2, TTE
  permanente, orden alternado A1 B1 A2 B2, ventana de 320 s).
- **`vf0`:** `vf0V1` (sonda) y `vf0V2` (corrección), ambas con sombra pasiva de ecossin y Add/Sub en `ret0`;
  sin mezclar con el A/B.
- **Validación final:** `finalF1` (exe `d80ac6f2976abc48a532bf0d78f80e3e2695e4ab665134c49b988cfdf1ad41fd`).

## 10. Resultados verificados

**Película `0x232` con TTE permanente (HECHO, 6/6 ejecuciones completas: A1, B1, A2, B2, V2 y F1):**

`vf0V1` también solicita `0x232`, pero su ventana de 40 s termina con la película a medias (223 trozos),
como estaba previsto. `tte0diagD1` (exe `cfb929bb…`) usó todavía el conmutador experimental y reproduce lo
mismo.

- 24 recursos CDMODULE idénticos;
- `0x232` 7,56–8,06 s después de `0x32`;
- 1169 trozos de película hasta `remaining=0x0` y 1550 fotogramas MPEG;
- `MOVIE:status alive=0`;
- 0 VU1 *reserved*, 0 XGKICK, 0 avisos IOP y 0 fatales;
- los mismos 3 RPC no manejados.

**A/B Add/Sub (HECHO):**

| Brazo | CDMODULE | `0x232` | Película | Add (antes de `0x32` / fin de película / total) | Sub total |
|---|---|---|---|---|---|
| A1, A2 (`ret0`) | idéntico | sí | completa | 1 / 2450 / 8150 | 6750 |
| B1, B2 (nativo) | idéntico | sí | completa | 1 / 2250 / 7250 | 6750 |

- Cada brazo es determinista: las dos corridas dan exactamente los mismos conteos de Add/Sub.
- La diferencia entre brazos es consistente. La primera divergencia aparece entre la llamada #250 y la
  #300, dentro de `Coat_matrix_calc@0x1B29B0` (física de la gabardina): con resultados reales de Add, ese
  bucle toma otro camino.
- **INFERENCIA:** es el efecto esperado de dejar de omitir la escritura `sqc2`. No es una regresión.
- **UNKNOWN:** equivalencia con ORIGINAL de esa ruta (sin oráculo).
- `katVu0Clip_ld` varía ya dentro del brazo A (335 450 frente a 352 200): es ruido de tiempo, no un efecto
  de Add/Sub.

**`vf0` / ecossin (HECHO, 600 casos reales por corrida):**

| Corrida | `vf0.w` deducido | Seno = modelo | Coseno = modelo | Error máx. frente a cos real |
|---|---|---|---|---|
| 001H / V1 (sin corrección) | 0 en 600/600 | 600/600 | 199/600 | 1,0 |
| V2 (`DMC_VF0_FIX`) | 1 en 600/600 | 600/600 | **600/600** | 5,2·10⁻⁴ (aproximación de Taylor del juego) |

Con `DMC_VF0_FIX` el juego mantiene todos los hitos. La escena cambia algo de ritmo (menos llamadas a
`ecossin` y `Clip_ld` en la ventana), lo esperable al cambiar la aritmética VU0 de todos los hilos.

## 11. Limitaciones y UNKNOWN

- **Primer frame jugable: NO demostrado.** No hay evidencia visual ni prueba de control.
- La sombra de ecossin no contiene casos con `1 − s² < 0`, así que la diferencia entre
  `sqrtf(max(0,·))` y `sqrt(|·|)` de VSQRT no se ejercitó.
- No hay oráculo ORIGINAL de Add/Sub, de ecossin ni de `Coat_matrix_calc`.
- El aborto preexistente en `PS2SifDma` impide ejecutar las suites posteriores de `ps2x_tests` en ambos
  árboles. Es UNKNOWN en ambos árboles por igual.
- `kMaxChainTags = 4096`: impacto no medido (heredado de 001H).
- La corrección `vf0` al despachar no cubre contextos ejecutados fuera del scheduler ni escrituras a `vf0`
  dentro de una misma función generada (`atari_check_4/4R`).

## 12. Estado actual de la solución

- **TTE:** permanente en `patches/` + lock. Pendiente de commit y publicación (requiere autorización).
- **Add/Sub:** restauradas de forma permanente en local. `RET0_COUNT = 17`.
- **`vf0`:** experimental y validado contra el modelo. Pendiente de diseñar el cambio general:
  - inicializar `vf0` en `StartThread` y en las invocaciones, o en el constructor si se justifica la
    recompilación;
  - descartar escrituras a `vf0` en el traductor.
- **`_capVu0ecossin`:** se mantiene en `ret0` hasta que la corrección `vf0` sea permanente.

## 13. Archivos de código

- `patches/GAMEPLAY_001I_vif1_tte_all_tag_ids.patch`, `upstream.lock.json`, `patches/README.md`.
- `vendor/PS2Recomp/ps2xRuntime/src/lib/ps2_memory.cpp`, `.../Kernel/Stubs/Helpers/Support.h`,
  `ps2xTest/src/ps2_memory_tests.cpp`.
- `vendor/.../Kernel/EeScheduler.cpp` (experimento `vf0`).
- `recomp/symtabfirst.toml`, `runtime/dmc_overrides.cpp`, `runtime/dmc_ret0_shadow_bodies.inc`.

## 14. Evidencia local (`analysis/local/gameplay_001i/`)

| Evidencia | Qué demuestra | SHA-256 |
|---|---|---|
| `patch_replay.py` | reproducción limpia del patchset (13 y 14) | `2dd6210d…7358` |
| `tests_run_baseline13.log`, `tests_run_patched.log`, `*.failed.txt` | mismos fallos; +3 tests | `928a62f4…f2e5`, `823b87fd…8387` |
| `tte0diagD1_run.log` | 0 pérdidas TTE=0 | — |
| `ab{A1,B1,A2,B2}_run.{log,json}`, `compare_ab.json`, `phase_counts.py` | A/B Add/Sub | `dd3089c7…feb5` (json) |
| `vf0V1_run.log`, `vf0V2_run.log`, `shadow_ecossin_V{1,2}.jsonl`, `ecossin_compare_V{1,2}.json`, `compare_ecossin.py` | origen de `vf0` y validación de ecossin | `8f44a8eb…01c2`, `134a894d…42fd` |
| `finalF1_run.*`, `checkpoint_selftest.log` | ejecutable final | — |
| `native_17.toml`, `regenerate_17.log`, `regen_17_diff.txt`, `generated_17_relevant/` | regeneración acotada | — |
| `G001I_VF0_EXPERIMENT.diff`, `pre_promotion/`, `*.pre001i` | estado previo e instrumentación retirada | `667f04a0…8e46` (diff) |

Los árboles temporales (`r*`, builds de tests y el árbol regenerado completo) se borraron tras extraer la
evidencia. Se pueden regenerar con `patch_replay.py` y `ps2_recomp` + `native_17.toml`.

## 15. Relación con otras fases

- Anterior: [GAMEPLAY_001H_VIF1_TTE_ROOT_CAUSE.md](GAMEPLAY_001H_VIF1_TTE_ROOT_CAUSE.md).
- Seguimiento de `ret0`: [GAMEPLAY_001_RET0_RESTORATION_STATUS.md](GAMEPLAY_001_RET0_RESTORATION_STATUS.md).
- Siguiente: ver [GAMEPLAY_CURRENT_STATUS.md](GAMEPLAY_CURRENT_STATUS.md) §9.

## RETRACTADO / correcciones

- 001H afirmaba que el parche mínimo propuesto era suficiente. **Matizado:** sin conservar el TTE en
  `sceDmaSend`, la semántica estricta podía perder VIFcodes en cadenas lanzadas por el stub. El parche
  promovido incluye ese cambio de upstream. En DMC, la medición no encontró cadenas afectadas.
- 001H atribuía el `vf0.w = 0` del hilo 12 a «`StartThread` o al checkpoint (UNKNOWN)». **Resuelto:** es la
  creación de contexto; el checkpoint solo perpetúa el valor.

## ACTUALIZACIÓN — publicación (2026-10-14)

Esta sección se añade sin modificar las anteriores, que describen el estado al cierre de la fase.

Publicación técnica de GAMEPLAY_001I en la rama `gameplay-001i` (2026-10-14), con cuatro commits:
- `7e7e65b`: CDMODULE + ABI 2 (`patches/GAMEPLAY_001D_001G_cdmodule_mode2_sound_abi2.patch`, parche 14);
- `22088d0`: VIF1 TTE (parche 15);
- `5cee31b`: restauración VU0, 17 `ret0`;
- un commit documental.

Cambios respecto al cierre de la fase:

- **Numeración del lock:** el trabajo CDMODULE/ABI 2 de 001D–001G, que solo existía en el vendor local, se
  convirtió en el parche 14. El parche TTE pasa a ser el 15.
  - `patched_commit` final: `6bea2eb0146c321903dc895a6290f36fa1803df4`.
  - Intermedio tras el parche 14: `2fe81812…`.
  - El `b3cb0bd7…` citado arriba correspondía a «13 + TTE» y ya no está en uso.
- **Reproducibilidad:** el replay limpio de los 15 parches coincide archivo por archivo con el vendor de trabajo.
- **Experimento `vf0`:** retirado del vendor (`EeScheduler.cpp` igual al replay). Queda en
  `analysis/local/gameplay_001i/G001I_VF0_EXPERIMENT.diff`.
- **Overrides:** `runtime/dmc_overrides.cpp` se publica sin los diagnósticos de 001G/001H (sombra,
  `DMC_RET0_NATIVE`, resumen de llamadores). Esa versión queda en
  `analysis/local/gameplay_001j/dmc_overrides.cpp.diag_001g_001i`.
- **Validación repetida** con el ejecutable reproducible `7110206b…` (corrida `pubR1`):
  - selftests 97/0;
  - el checkpoint ABI 2 carga;
  - 24 recursos;
  - `0x232` a 7,56 s, con la película completa (1169 trozos, 1550 fotogramas) y 0 errores.
  - Los tests de parches (`ps2x_tests`) no se repitieron: los archivos de los parches 14/15 no cambiaron
    respecto a la ejecución de 001I. El parche 14 no toca código cubierto por esos tests.
