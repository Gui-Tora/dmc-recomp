# P4.1.1R — RESTORE_P314_SYNC_REDECODE_BASELINE_REPRODUCIBILITY

Auditoría de estado de build/runtime, sin investigar aún el medio-cuadro
temprano. Objetivo único: restaurar o explicar por qué el camino síncrono
de re-decodificación P3.14.2 dejó de dispararse de forma reproducible
durante P4.1.1 — sin modificar lógica de presentación, `preferredSource`
ni comportamiento de MPEG.

## Estado de entrada (verificado)

Main HEAD `c9ee65d` ✓. Main status: solo los dos informes ya esperados
sin trackear (`BLOCKER_004_P411_MOVIE_FRAME_COMPLETENESS_BOUNDARY_TRACE.md`,
`BLOCKER_004_P41F_DISPFB0_FIX_AND_RESIDUAL_FLICKER_AUDIT.md`) — sin dirt
ajeno. Vendor HEAD `16ef7ab8dc961c44921c3776705da6bc3b4806fb` ✓, limpio ✓,
`git diff` contra ese mismo commit vacío ✓. Sin proceso `dmc-recomp.exe`
corriendo ✓.

## Causa raíz — encontrada en la Fase 1, confirmada antes de llegar a la Fase 8

**HECHO, causa exacta identificada por lectura de código + reproducción
controlada — jerarquía de hipótesis resuelta en el hipótesis #1, sin
necesidad de examinar #2-8:**

`analysis/tools/runtime_loop/run.py` (línea 68): `env=os.environ.copy()`
— el proceso hijo `dmc-recomp.exe` **hereda el entorno completo del
proceso padre** que invoca `run.py`, y `run.py` solo sobreescribe
explícitamente `PS2X_CD_IMAGE`/`PS2X_RUNTIME_ARENA_BASE`/
`PS2X_RUNTIME_ARENA_LIMIT` (línea 69) y, si `--diagnostics` está
presente, tres variables `DMC_P311_*` (línea 84). **`run.py` nunca fija
ni depende de `DMC_P314_SYNC_REDECODE` — es responsabilidad exclusiva del
invocador fijarla en su propio proceso antes de lanzar `run.py`.**

El script que usé durante los 7 intentos de P4.1.1
(`$CLAUDE_JOB_DIR/tmp/p411_run.sh`) fija explícitamente
`PS2X_CD_IMAGE`, `PS2X_RUNTIME_ARENA_BASE`, `PS2X_RUNTIME_ARENA_LIMIT` y
`DMC_P313_INIT_MODE=ownership` — **pero nunca `DMC_P314_SYNC_REDECODE`**.
Verificado leyendo el archivo directamente (línea por línea, sin
paráfrasis): las 8 líneas del script no contienen esa variable en ningún
punto.

Esta bash tool de esta sesión **no preserva el estado del shell entre
invocaciones** (`working directory persists between commands, but shell
state does not` — instrucción de entorno explícita). Cada corrida de
P4.1.1 se lanzó ejecutando ese mismo script en una invocación de Bash
nueva, por lo que **ninguna** de las 12 corridas de P4.1.1 (ni las 7
instrumentadas ni las 4 "de control" tras revertir el vendor al 100%)
tuvo jamás `DMC_P314_SYNC_REDECODE=1` en el proceso hijo real —
independientemente de lo que hubiera hecho cualquier `export` anterior en
otra invocación de Bash, porque esas invocaciones no comparten estado.

## Por qué esto produce exactamente el síntoma observado

`vendor/PS2Recomp/ps2xRuntime/src/lib/Kernel/Stubs/MPEG.cpp` (código
actual, sin cambios — ver Fase 3):

```cpp
bool isP314SyncRedecodeEnabled()
{
    const char *raw = std::getenv("DMC_P314_SYNC_REDECODE");
    return raw != nullptr && std::string(raw) == "1";
}
```

Sin esta variable, `isP314SyncRedecodeEnabled()` devuelve `false` en los
tres puntos de uso (línea 969: `fresh.syncGuestEsDecodeActive = false`;
línea 2722: el bloque completo `pre-sync`/`[P3142:ring]`/`post-sync` de
`sceMpegGetPicture` **se salta entero**, `if (... && isP314SyncRedecodeEnabled() && ...)`;
línea 3231: `p313Mode` no se fuerza a `Ownership` — aunque en mi caso ya
era `Ownership` por `DMC_P313_INIT_MODE=ownership`, así que este punto
específico no cambiaba el resultado). El efecto neto: la sesión corre en
el modo `P3.13 ownership` **puro**, sin el mecanismo de re-sincronización
síncrona de P3.14.2. Por la propia historia ya documentada de P3.13
(`BLOCKER_004_P313_SCEMPEGINIT_OWNERSHIP_MATRIX.md`, resumen ya
conocido: "ownership/config continuity alone is insufficient" para
sostener el decode tras el reset), esto es exactamente consistente con
lo observado 12/12 veces: `sceMpegGetPicture` entra en
`[GP:H] waitExternal(...) -- SUSPENDING, no frame available` justo
después del segundo `sceMpegInit` (el de `ownership`) y nunca se
resuelve dentro de la ventana observada, porque nada vuelve a alimentar
`decodedFrames` sin el mecanismo síncrono.

**Esto invalida la interpretación central del informe P4.1.1**: el
bloqueo no era una fragilidad nueva de temporización del propio
mecanismo P3.14.2 expuesta por casualidad — era, de forma mucho más
simple, que P3.14.2 **nunca estuvo activo** en ninguna de esas 12
corridas. Ver "Correction ledger" más abajo.

## Fase 1 — Prueba del entorno efectivo

`run.py` no imprime su propio entorno efectivo, así que en vez de
modificar runtime source se usó el propio código fuente MPEG.cpp como
oráculo — la condición exacta de activación (`isP314SyncRedecodeEnabled`)
ya emite, cuando SÍ está habilitada, líneas de log inequívocas
(`[P3142:ring]`, dentro del bloque de la línea 2721). Su total ausencia
en los 12 logs de P4.1.1, contrastada con su presencia (23 líneas) en la
corrida de confirmación de este prompt (RUN_059, ver Fase 11), es prueba
directa y suficiente sin necesidad de instrumentación nueva.

**Respuesta**: `DMC_P314_SYNC_REDECODE` **NO** estaba presente (ni con
valor `1` ni con ningún otro) en el proceso hijo real durante los 12
intentos de P4.1.1. `DMC_P313_INIT_MODE=ownership` sí estaba presente
(confirmado también por `meta['diagnostic_env']` en cada `result.json`,
que sí incluye el prefijo `DMC_P313_` — pero **no** el prefijo `DMC_P314_`,
línea 86 de `run.py`: `k.startswith(('DMC_P311_','DMC_P313_'))` — esto
explica por qué el propio `result.json` nunca mostró la ausencia: el
campo que la habría revelado ni siquiera se registra para ese prefijo).

## Fase 2 — Tabla de firmas de log

| Firma | P3.14.2 conocido-bueno (informe P3.14.2, RUN_038-040) | P4.1.1 fallido (RUN_047-058, 12/12) | RUN_059 (esta corrida, corregida) |
|---|---|---|---|
| `[P313:init] mode=ownership ...` | presente | presente | presente |
| `[P314:GP] #N pre-sync ...` | presente (cientos de veces) | **ausente, 0/12** | presente (24 líneas) |
| `[P3142:ring] #N ...` | presente (cientos de veces) | **ausente, 0/12** | presente (23 líneas) |
| `[P314:GP] #N post-sync ...` | presente | **ausente, 0/12** | presente |
| `[GP:B] #N have-frame path ...` | presente | **ausente, 0/12** | presente (implícito, ver éxitos) |
| `[GP:D] #N presentation due now ...` | presente | **ausente, 0/12** | presente (implícito) |
| `[MPEG:diag] getPicture success #N` | ≥250/corrida | **0/12 corridas, siempre 0** | ≥150 (muestreado #1-10,50,100,150; corte del log a los 90s, no agotamiento) |
| `[GP:H] waitExternal(...) SUSPENDING` | 0 (nunca en P3.14.2 validado) | **presente, 12/12, nunca se resuelve** | **0** |

**Primer diagnóstico faltante**: `[P314:GP] #1 pre-sync` — su ausencia
es la primera divergencia observable respecto al baseline conocido-bueno,
y coincide exactamente con la primera llamada a `sceMpegGetPicture` tras
el segundo `sceMpegInit`. Esto identifica el modo como "nunca habilitado"
(no "habilitado pero falló después").

## Fase 3 — Presencia del código P3.14.2 en el vendor actual

Confirmado por lectura directa, sin modificar nada:

- `isP314SyncRedecodeEnabled()` (línea 876-880): presente, sin cambios.
- `fresh.syncGuestEsDecodeActive = isP314SyncRedecodeEnabled();` (línea
  969, dentro de la reconstrucción de `applyP313OwnershipMode`):
  presente, sin cambios.
- Guard de exclusión mutua con `DMC_P313_INIT_MODE=full` (línea
  3231-3240): presente, sin cambios.
- Bloque completo `pre-sync`/`[P3142:ring]`/`post-sync`/mirror Modelo
  B/guarda de staleness dentro de `sceMpegGetPicture` (línea 2721 en
  adelante): presente, sin cambios visibles en la porción inspeccionada.

**Respuesta**: SÍ, el código de P3.14.2 sigue presente semánticamente en
el vendor P4.1 actual. P4.1 no tocó `MPEG.cpp` (su único cambio de
producción fue en `GS.cpp`, ya documentado). No se requirió comparar
contra el patch byte a byte porque la lectura directa de las 4
ubicaciones clave ya es concluyente y el propio `git diff` contra
`16ef7ab8...` está vacío (nada se modificó desde el commit ya validado).

## Fase 4 — Identidad del ejecutable

| | Valor |
|---|---|
| Ruta lanzada por `run.py` | `analysis/local/symtabfirst/build/bin/Debug/dmc-recomp.exe` (línea 61 de `run.py`, confirmado) |
| SHA256 actual (antes de la corrida de confirmación) | `2b9ee9b092f075eda0dc6d5799263d0ff108e21b66aa44c378d16e3a70673b02` |
| Tamaño/fecha | 347655680 bytes, 2026-09-12 17:09 |
| SHA256 validado por P4.1 | `27faab90b0db40705b6c559c5b79c44f4b7bf2bab5bb62d338bde5b17880d13b` |
| ¿Coinciden? | **NO** |

**El hash no coincide, pero esto NO es la causa del bloqueo** (ver Fase
5): los builds Debug de MSVC con `/Zi` incrustan un GUID de PDB
aleatorio distinto en cada invocación de `link.exe`, incluso a partir de
fuente 100% idéntica — no hay flags de build determinista (`/Brepro` o
equivalente) configurados en este `.vcxproj`. Prueba directa: tras
revertir el vendor byte a byte a `16ef7ab8...` (verificado `git diff`
vacío) y recompilar sin ningún cambio de fuente, el hash resultante
(`2b9ee9b0...`) **tampoco** coincidió con el hash de la build anterior
inmediatamente previa a esa (`80007d87...`, ella misma ya distinta de
`27faab90...`) — dos builds consecutivas de la MISMA fuente produjeron
DOS hashes distintos entre sí. Esto descarta que la diferencia de hash
señale una diferencia de código; es ruido de build esperado y ya
documentado como tal en este mismo prompt.

Otra copia de `dmc-recomp.exe` existe en `build/runtime/active/bin/Debug/`
— ya identificada en checkpoints anteriores (P3.12/P3.13) como un árbol
NO usado por `run.py`; confirmado de nuevo aquí que `run.py` (línea 61)
apunta exclusivamente a `analysis/local/symtabfirst/build/...`.

## Fase 5 — Consistencia de artefactos de build

Auditada por comparación de hash de `.lib`, no por timestamp (más
confiable). El `.lib` activo (`analysis/local/symtabfirst/build/upstream/
ps2xRuntime/Debug/ps2_runtime.lib`) fue recompilado y re-linkeado por
última vez inmediatamente después de revertir los 3 archivos tocados por
P4.1.1 (`MPEG.cpp`, `gs_cpu_backend.cpp`, `gs_frontend.cpp`) a su estado
exacto en `16ef7ab8...` — verificado en su momento con `git diff --stat`
vacío antes de la recompilación, y de nuevo ahora al inicio de este
prompt. **No se encontró ningún objeto instrumentado remanente**: el
build fue quirúrgico (`grep -c "CL.exe"` = 1 por cada recompilación desde
fuente revertida, exactamente el archivo cuya fuente había cambiado de
vuelta) y el relink posterior (`_BuildLinkAction`) tuvo `grep -c "CL.exe"`
= 0 en cada caso, confirmando que ningún archivo de `recomp/generated/`
ni ningún otro `.obj` se recompiló de forma inesperada.

**Respuesta**: NO, la reversión de fuente no dejó objetos ni miembros de
archivo `.lib` obsoletos — la secuencia revert→build→relink ya ejecutada
al final de P4.1.1 fue correcta y suficiente. Esto NO fue la causa del
bloqueo (la causa es puramente de entorno, Fase 1).

## Fase 6 — Árbol de build activo

Confirmado por lectura directa de `run.py` (línea 61): exclusivamente
`analysis/local/symtabfirst/build/bin/Debug/dmc-recomp.exe`, enlazado
desde `analysis/local/symtabfirst/build/upstream/ps2xRuntime/Debug/
ps2_runtime.lib`. Ningún uso de `analysis/local/p311/` para linkear o
ejecutar (esa carpeta solo almacena las carpetas `RUN_NNN/` de salida del
harness, nunca binarios de entrada). Ningún uso del árbol obsoleto
`build/runtime/active/`.

## Fase 7 — Identidad de FFmpeg

No se requirió inspección estática de DLLs/CMakeCache: la propia corrida
de confirmación (Fase 11, RUN_059) decodificó ≥150 imágenes reales vía
FFmpeg (`getPicture success #N`, `size=512x448`) — prueba dinámica
directa y más fuerte que cualquier verificación estática de que FFmpeg
está enlazado, presente y funcional de forma idéntica a la validación de
P4.1. Sin mensajes `"runtime built without FFmpeg"` en ningún log de
esta sesión.

## Fase 8 — Diferencial de comando

| | RUN_044 (P4.1, conocido-bueno) | `p411_run.sh` (P4.1.1, 12/12 fallido) |
|---|---|---|
| `PS2X_CD_IMAGE` | mismo ISO | mismo ISO |
| `PS2X_RUNTIME_ARENA_BASE` | `0x009FA000` | `0x009FA000` |
| `PS2X_RUNTIME_ARENA_LIMIT` | `0x00ABE000` | `0x00ABE000` |
| `DMC_P313_INIT_MODE` | `ownership` | `ownership` |
| **`DMC_P314_SYNC_REDECODE`** | **presumiblemente `1`** (inferido: el log de RUN_044 muestra `[P314:GP]`/`[P3142:ring]`, imposibles sin esta variable — el comando exacto de esa corrida no está preservado textualmente en este repo, solo su log y `result.json`, que no registra este prefijo, ver Fase 1) | **ausente** |
| `--runs` | 1 (asumido) | 1 |
| `--seconds` | 90 (asumido por duración observada) | 90 |
| `--confirm` | sí | sí |
| `--keys` | `ENTER,X` | `ENTER,X` |
| `--method` | `runtime` | `runtime` |
| `--diagnostics` | sí | sí |
| `--post-target` | 15 (inferido de milestones) | 3 → 25 (variado entre intentos) |
| `--target` | `[GP:H]` (default, inferido) | `[MPEG:init]` (elegido para acortar la ventana) |
| `--visual-trace` | sí | sí (algunos intentos) / no (otros) |
| Estado del shell entre invocaciones | N/A (una sola invocación) | **cada invocación de Bash de esta sesión es un proceso nuevo sin `export` heredado de la anterior** |

**La única diferencia que importa causalmente es `DMC_P314_SYNC_REDECODE`.**
Las diferencias de `--post-target`/`--target` cambian cuándo el harness
decide detenerse, no si el runtime decodifica.

## Fase 9 — Interacción de modos P313/P314

Código (línea 3231-3240): `DMC_P314_SYNC_REDECODE=1` fuerza `p313Mode` a
`Ownership` incluso si `DMC_P313_INIT_MODE=full`, y emite un log
explícito de advertencia (`"...overriding to the ownership baseline"`) en
ese caso — no observado en ningún log de esta sesión (nunca se usó
`full`). Con `DMC_P313_INIT_MODE=ownership` explícito (mi caso) y
`DMC_P314_SYNC_REDECODE=1`, no hay conflicto: ambos coinciden en
`Ownership`, sin mensajes de invalidez/incompatibilidad en el log de
RUN_059 (ninguno esperado ni encontrado). **La combinación
`ownership`+`sync=1` es válida y fue exactamente la usada en la
confirmación exitosa.**

## Fase 10 — Restauración

No fue necesaria ninguna restauración de binario: las Fases 4-6 probaron
que el binario/artefactos ya correspondían a la fuente limpia (el
mismatch de hash es ruido de build no-determinista, no una diferencia de
código). No se ejecutó ningún paso de la Fase 10 (A/B/C) por no ser
aplicable — el problema nunca estuvo en el binario.

## Fase 11 — Corrida mínima de línea base (RUN_059)

Comando, con asignaciones explícitas en la misma sesión de Bash (sin
depender de exports previos):

```bash
export PS2X_CD_IMAGE="...Devil May Cry 2001.iso"
export PS2X_RUNTIME_ARENA_BASE=0x009FA000
export PS2X_RUNTIME_ARENA_LIMIT=0x00ABE000
export DMC_P313_INIT_MODE=ownership
export DMC_P314_SYNC_REDECODE=1
py analysis/tools/runtime_loop/run.py --runs 1 --seconds 90 --confirm \
  --keys ENTER,X --method runtime --diagnostics --post-target 15 \
  --target '[GP:H]' > .../p411r_baseline_run.log 2>&1
```

Resultado (`RUN_059`, `exe_sha256=2b9ee9b0...`, el mismo binario
reconstruido desde fuente 100% limpia en este prompt):

| Criterio de éxito | Resultado |
|---|---|
| 1. P314 sync mode explícitamente activo | ✅ 24 líneas `[P314:GP]` |
| 2. decoder síncrono recibe ES guest fresco | ✅ 23 líneas `[P3142:ring]` |
| 3. ownership de decode async excluido | ✅ 0 líneas `[GP:H]` (nunca se suspende) |
| 4. GetPicture progresa repetidamente | ✅ ≥150 éxitos (muestreado #1-10,50,100,150) |
| 5. sin inanición terminal inmediata | ✅ corrida completa 90.1s, sin corte prematuro |
| 6. imagen de película reconocible | no verificado visualmente en esta corrida (sin `--visual-trace`, no requerido por este prompt) |

`result.json: "TARGET_NOT_REACHED"` — **esperado y no un fallo**: el
target elegido (`[GP:H]`, la marca de suspensión) nunca aparece en una
corrida sana, precisamente porque el mecanismo síncrono evita esa
suspensión por diseño; el harness agota `--target-timeout` (30s tras
`PSS_REACHED`) y termina con ese código, pero el runtime en sí decodificó
sosteniblemente durante toda la ventana observada. Para una futura
corrida de este tipo, `--target` debería ser un marcador que SÍ ocurre en
el camino sano (p. ej. `[MPEG:diag] getPicture success #`) en vez de
`[GP:H]`.

**Línea base P3.14.2 restaurada: SÍ.**

## Preguntas requeridas

1. ¿`DMC_P314_SYNC_REDECODE=1` estaba realmente presente en el proceso
   hijo? **NO**, en ninguna de las 12 corridas de P4.1.1.
2. ¿`DMC_P313_INIT_MODE=ownership` estaba realmente presente? **SÍ**, en
   las 12.
3. ¿Los logs actuales mostraban la firma de activación del modo sync?
   **NO**, 0/12 antes de este prompt; **SÍ**, en RUN_059.
4. ¿El código de P3.14.2 sigue presente en el vendor P4.1 actual? **SÍ**,
   verificado en las 4 ubicaciones clave, sin cambios.
5. ¿Qué ejecutable se lanzó realmente?
   `analysis/local/symtabfirst/build/bin/Debug/dmc-recomp.exe`.
6. ¿Cuál es su SHA256? `2b9ee9b092f075eda0dc6d5799263d0ff108e21b66aa44c378d16e3a70673b02`
   (antes y durante RUN_059; sin recompilar en este prompt).
7. ¿Coincide con el hash validado por P4.1? **NO** — explicado como
   ruido de build Debug no-determinista (Fase 4), no como diferencia de
   código (fuente idéntica confirmada por `git diff` vacío).
8. ¿La reversión de fuente dejó `.obj` obsoletos? **NO** (Fase 5).
9. ¿Dejó un miembro obsoleto en `ps2_runtime.lib`? **NO** (Fase 5).
10. ¿Se re-linkeó el ejecutable tras revertir los diagnósticos? **SÍ**,
    ya hecho al cierre de P4.1.1, re-verificado aquí.
11. ¿`run.py` usaba el árbol de build esperado? **SÍ** (Fase 6).
12. ¿Las dependencias de FFmpeg eran idénticas/estaban presentes? **SÍ**,
    confirmado dinámicamente por decodificación real exitosa (Fase 7).
13. ¿Qué comando/entorno exacto difirió entre RUN_044 y las corridas
    fallidas de P4.1.1? **`DMC_P314_SYNC_REDECODE`** — presente
    (inferido) en RUN_044, ausente en las 12 corridas de P4.1.1 (Fase 8).
14. ¿Cuál fue la PRIMERA firma de diagnóstico P314 faltante?
    `[P314:GP] #1 pre-sync` (Fase 2).
15. ¿El fallo fue de entorno/estado de build, o un genuino problema de
    estado en runtime? **De entorno** — una variable de entorno faltante
    en el script invocador, no un problema de build ni de runtime.
16. Si fue de entorno, ¿cuál fue el desajuste exacto? `p411_run.sh` nunca
    exportó `DMC_P314_SYNC_REDECODE=1`; combinado con que cada invocación
    de Bash de esta sesión no hereda `export`s de invocaciones previas,
    ninguna corrida de P4.1.1 pudo haberla tenido jamás.
17. ¿Se restauró la línea base? **SÍ** (RUN_059, Fase 11).
18. ¿La línea base restaurada alcanzó progresión sostenida de
    GetPicture? **SÍ**, ≥150 éxitos, 0 suspensiones sin resolver.
19. ¿Es seguro retomar P4.1.1 ahora? **SÍ**, siempre que el script
    invocador de la corrida futura incluya explícitamente
    `DMC_P314_SYNC_REDECODE=1` (y se verifique su presencia en el log
    antes de invertir tiempo en instrumentación).
20. ¿Se modificó código de producción? **NO** — ningún archivo de
    `vendor/PS2Recomp` fue editado en este prompt; solo se leyó código y
    se ejecutó una corrida de confirmación con variables de entorno
    corregidas.

## Correction ledger

- **Se retracta** la interpretación central de
  `BLOCKER_004_P411_MOVIE_FRAME_COMPLETENESS_BOUNDARY_TRACE.md` de que
  el bloqueo revelaba "una fragilidad de temporización preexistente del
  propio mecanismo P3.14.2, expuesta por casualidad" — la causa real era
  mucho más simple: `DMC_P314_SYNC_REDECODE=1` nunca estuvo presente en
  el script invocador (`p411_run.sh`) usado en los 12 intentos,
  incluyendo las 4 "corridas de control" que se interpretaron como
  prueba de que el problema sobrevivía a la reversión completa del
  código — en realidad esas corridas de control nunca ejecutaron el
  camino P3.14.2 en absoluto, con o sin reversión de fuente, por lo que
  no podían haber probado nada sobre la fragilidad del mecanismo.
- **Se mantiene sin cambios** todo lo demás de P4.1.1: el fix P4.1
  (`disp[0].dispfb`) sigue `P41_FIX_CAUSAL_AND_VALIDATED`; el medio-cuadro
  temprano sigue reproducible 3/3 (evidencia offline, Fase 1 de P4.1.1,
  no afectada por este hallazgo); FIELD sigue no-causal; el fallback
  `FBP==0` sigue geométricamente incapaz de producir un defecto parcial;
  el candidato `preferredSource` sigue como pista estática no confirmada,
  ahora finalmente investigable dinámicamente sin el bloqueo espurio.

## Clasificación

`P411R_MISSING_SYNC_ENVIRONMENT`

## Estado final de git (verificado al cierre)

- Main: `c9ee65d`, mismos 2 informes sin trackear más este nuevo informe
  sin trackear. Sin commit.
- Vendor: `git diff --stat` vacío, `HEAD` = `16ef7ab8dc961c44921c3776705da6bc3b4806fb`.
  Ningún archivo de `vendor/PS2Recomp` fue editado en este prompt.
- Ningún proceso `dmc-recomp.exe` en ejecución.
- Sin push.
