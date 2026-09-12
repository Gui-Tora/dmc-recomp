# P4.1.3 — SCEGSSETDEFDBUFFDC_DRAWENV_FRAME_FIX

Implementación y validación del fix de producción derivado de la
auditoría adversarial P4.1.2 (Fable). El fix P4.1 (`disp[0].dispfb`)
permanece `P41_FIX_CAUSAL_AND_VALIDATED`, sin reconsiderar.

## Correction ledger

- **P4.1.1B** «FBP 0xA0 nunca recibe las filas 256-447 de la subida» →
  **RETRACTADO** (ya narrowed por P4.1.2, confirmado aquí dinámicamente
  post-fix): la subida SÍ es completa y simétrica para ambos slots; lo
  que ocurre es que, tras la subida, el propio sprite de fade/UI del
  guest se dibuja sobre esas filas con un `FRAME` env erróneo y las
  borra, en el mismo ciclo, antes de la presentación.
- **P4.1** «`zbufAddr`/`fbp1` sigue usándose legítimamente en
  `draw11`/`draw12`» → **RETRACTADO**: es la causa raíz de este defecto,
  segunda instancia de la misma clase de bug que P4.1 corrigió para el
  entorno de *display*.
- **HECHO** (preservado de P4.1.2): la subida GIF de película es
  completa (384/384 strips, `copied==total`, relectura de VRAM
  confirma filas 0-447 para ambos DBP).
- **HECHO** (preservado de P4.1.2): el draw posterior con
  `FRAME=0xE0` destruye las filas 256-447 de `FBP=0xA0` — confirmado
  aquí que, tras el fix, ese draw deja de ocurrir (0 casos en 3
  corridas) y el patrón de destrucción desaparece (0/N vs 52/52
  pre-fix).

## Estado de entrada (verificado)

Main HEAD `c9ee65d` ✓, sin dirt ajeno (los 5 informes esperados sin
trackear). Vendor HEAD `16ef7ab8dc961c44921c3776705da6bc3b4806fb` ✓,
limpio ✓.

## Lectura obligatoria

`BLOCKER_004_P412_FABLE_FBPA0_PARTIAL_UPLOAD_ROOT_CAUSE_AUDIT.md` (Fable)
leído en full — ver resumen de la cadena causal en el prompt y
reproducido aquí en el ledger. `BLOCKER_004_P41_SCEGSSETDEFDBUFFDC_DISPFB0_ZERO_FIX.md`
y `BLOCKER_004_P411B_MOVIE_FRAME_COMPLETENESS_BOUNDARY_TRACE_RETRY.md`
re-verificados como contexto.

## Oráculo PCSX2 original (opcional) — INFERENCIA FUERTE, no bloqueante

Búsqueda de un volcado PCSX2 existente con `0x740E20` (FRAME_1 del
paquete slot1 original): ninguno encontrado en `analysis/`. Leer esa
dirección en vivo desde PCSX2 habría requerido una investigación lateral
grande, no realizada en este prompt (tal como el prompt autorizaba
explícitamente no bloquear la implementación por esto). El default
original de FBP=0 para el draw-env del slot1 permanece como
**INFERENCIA FUERTE** (misma base que P4.1.2: coherencia de ping-pong +
precedente directo de P4.1 para el campo hermano `dispfb`), no como
HECHO verificado en PCSX2.

## Causa raíz (congelada de P4.1.2, no re-derivada)

`GS.cpp`, `sceGsSetDefDBuffDc`: `const uint32_t fbp1 = zbufAddr;` sembraba
el `FRAME FBP` de `db.draw11`/`db.draw12` (slot1, aplicado cuando el
guest muestra `FBP=0xA0`) con `0xE0` (la dirección de Z-buffer, ya
identificada exacta en P4.0.2). Por direccionamiento real de páginas GS
(PSMCT32, FBW=8 → 8 páginas/fila-de-páginas de 32 líneas): la fila
y=256 de un buffer con FBP=0xA0 vive en la página `0xA0+(256/32)·8=0xE0`
— exactamente el FBP contaminado. Cada frame en que el guest dibuja su
sprite de fade/UI (512×448, opaco, negro) mientras muestra `FBP=0xA0`,
lo hace a través de este `FRAME` env erróneo, escribiendo negro sobre
las filas 256-447 del buffer que se está mostrando, después de que la
subida de película ya las llenó correctamente.

## Implementación

Cambio mínimo, forma preferida del prompt (`fbp1` sin otros usos más
allá de `draw11`/`draw12`, confirmado por grep antes de editar):

```diff
-        const uint32_t fbp1 = zbufAddr;
+        const uint32_t fbp1 = 0u;
```

(con comentario explicativo de 11 líneas). `zbufAddr` en sí **no se
toca** — sigue usándose intacto en las 4 llamadas `ZBUF` (`draw01`,
`draw02`, `draw11`, `draw12`) y en `sceGszbufaddr`. `dispfb0`/`dispfb1`
(fix de P4.1) **no se tocan**. Diff total: `git -C vendor/PS2Recomp diff`
confirmado exactamente 1 hunk, +12/-1 líneas, un único archivo
(`GS.cpp`).

## Aserciones estáticas post-fix

- slot0 (`draw01`/`draw02`) `FRAME FBP` inicial: `0` (sin cambio,
  literal ya presente).
- slot1 (`draw11`/`draw12`) `FRAME FBP` inicial: `0` (antes `0xE0`,
  ahora corregido).
- `ZBUF` de ambos slots: `zbufAddr` (para este arranque de DMC, `0xE0`)
  — sin cambio.
- `dispfb0`/`dispfb1` (P4.1): `FBP=0` por defecto — sin cambio.
- Ninguna otra línea semántica cambió (confirmado por el diff completo).

## Build

Quirúrgico en las 3 iteraciones de este prompt (fix solo, fix+
instrumentación, fix+instrumentación con cap ampliado, y build final
limpio tras revertir la instrumentación): `grep -c "CL.exe"` = 1 por
compilación (exactamente los archivos tocados en cada paso — `GS.cpp`
para el fix; `MPEG.cpp`+`gs_cpu_backend.cpp` para la instrumentación de
validación), 0 en cada relink (`ps2EntryRunner /t:_BuildLinkAction`).
Ningún archivo de `analysis/local/symtabfirst/generated/` compiló en
ningún momento.

## Entorno de validación (gate obligatorio, satisfecho en las 3 corridas)

Lanzador con asignaciones explícitas en la misma invocación (lección de
P4.1.1R):

```
DMC_P313_INIT_MODE=ownership
DMC_P314_SYNC_REDECODE=1
PS2X_RUNTIME_ARENA_BASE=0x009FA000
PS2X_RUNTIME_ARENA_LIMIT=0x00ABE000
PS2X_CD_IMAGE=<ISO real>
DMC_P413_TRACE=1   (instrumentación de validación, opt-in)
```

Las 3 corridas (`RUN_064`, `RUN_065`, `RUN_066`) muestran `[P314:GP]`=24,
`[P3142:ring]`=23, `[GP:H]`=0 — camino síncrono confirmado activo antes
de interpretar cualquier evidencia, en las 3.

## Instrumentación de validación implementada (opt-in, revertida al cierre)

- **`MPEG.cpp`** (boundary A/B): antes/después de
  `writeDecodedFrameToGuest`, mismo diseño de P4.1.1B (firmas de 7
  bloques de 64 filas, hash FNV-1a + conteo no-negro).
- **`gs_cpu_backend.cpp`**:
  - `DrawPrimitive` (Validación B): registra `fbp`/`type`/`tme`/`abe`
    de cada draw real, capado a 600000 líneas (suficiente para cubrir
    la corrida completa sin truncar — el primer intento, capado a
    20000, se agotó durante el arranque/menú antes de llegar a la
    película; corregido).
  - `copySource` (Validación D/E): lectura cruda de VRAM en el
    `displayFrame` activo + estado completo de `preferredSource`.
  - `PresentFromLocalMemory` (post-field): mismo patrón que P4.1.1B.

## Validación A — Estado guest del draw-env (HECHO, dinámico, 3/3 idéntico)

Leído directamente de `getpicture.bin` en las 3 corridas:

| Campo | Dirección | RUN_064 | RUN_065 | RUN_066 |
|---|---|---|---|---|
| slot0 `FRAME_1` (reg 0x4C) | `0x740CB0` | `0x000800a0` | `0x000800a0` | `0x000800a0` |
| slot1 `FRAME_1` (reg 0x4C) | `0x740E20` | `0x00080000` | `0x00080000` | `0x00080000` |
| slot0 `ZBUF` (reg 0x4E) | `0x740CC0` | `0x10a0000e0` | `0x10a0000e0` | `0x10a0000e0` |
| slot1 `ZBUF` (reg 0x4E) | `0x740E30` | `0x10a0000e0` | `0x10a0000e0` | `0x10a0000e0` |

`FBP` = bits bajos: slot0 `FRAME`=`0xA0` (sin cambio, parcheado por
`Main_init` como siempre), **slot1 `FRAME`=`0x000` (antes `0xE0` per
P4.1.2 — corregido)**. `ZBUF` idéntico en ambos slots y en las 3
corridas: `ZBP=0xE0`, `ZMSK=1` — sin cambio, exactamente como exigía el
gate. `FRAME_2` (contexto2, `draw02`/`draw12`) no se releyó por
separado: está garantizado idéntico por construcción, ya que la fuente
pasa el mismo argumento `fbp1` a ambas llamadas (`seedGsDrawEnv1`/`2`),
y el diff confirma que ningún otro argumento cambió.

## Validación B — Destino real de los draws (HECHO, mandatory gate, 3/3)

Con `DMC_P413_TRACE=1`, cada llamada real a `DrawPrimitive` en toda la
corrida (arranque + menú + película) registrada:

| | RUN_064 | RUN_065 | RUN_066 |
|---|---|---|---|
| draws totales capturados | 20000 (capado — ver nota) | 26976 | 28677 |
| `fbp=0x0` | 20000 | 26950 | 28652 |
| `fbp=0xa0` | 0 (no alcanzado, cap agotado antes) | 26 | 25 |
| **`fbp=0xe0`** | **0** | **0** | **0** |

RUN_064 usó el cap inicial de 20000 (agotado durante boot/menú, antes de
alcanzar la película) — no invalida la conclusión (0 casos de `0xe0` en
lo capturado) pero se corrigió a 600000 para RUN_065/066, cubriendo la
corrida completa sin truncar. **Cero draws con `FRAME=0xE0` en ninguna
corrida** — el mandatory gate de la Validación B se cumple. Los destinos
que sí alternan son `0x0` (mayoritariamente UI/texto) y `0xa0`
(minoritario, el propio sprite de fade de película) — exactamente el
ping-pong `A0`↔`0` esperado, nunca `E0`.

## Validación C — Subida GIF de película (sin regresión, no re-investigada a fondo)

No re-trazada byte a byte (ya cerrada por P4.1.2, fuera del alcance
recomendado por este prompt). Confirmación indirecta suficiente: la
Validación D (abajo) muestra que las filas 256-447 de `FBP=0xA0` **SÍ**
llegan a estar presentes en el momento de la presentación post-fix
(0/N incompletas) — lo cual solo es posible si la subida sigue siendo
completa, ya que nada más las escribe.

## Validación D — Región inferior de FBP=0xA0 (HECHO, mandatory gate, 3/3)

Misma metodología de P4.1.1B, restringida a `t ≥` timestamp de
`picture=0` (ancla de `MPEG_INIT`) en cada corrida:

| | RUN_064 | RUN_065 | RUN_066 |
|---|---|---|---|
| `FBP=0xA0`, muestras totales | 45 | 45 | 44 |
| `FBP=0xA0`, mitad inferior (bloques 4-6) en negro puro | **0** | **0** | **0** |
| `FBP=0x000`, muestras totales | 47 | 44 | 44 |
| `FBP=0x000`, mitad inferior en negro puro | 0 | 0 | 0 |

**Comparación directa con el baseline pre-fix (P4.1.1B, RUN_061,
mismo método exacto): 52/52 (100%) → 0/45, 0/45, 0/44 (0%) post-fix.**
Regresión obligatoria descartada: no hay ennegrecimiento sistemático de
los bloques 4-6 en ninguna corrida.

## Validación E — Doble buffer de display (sin regresión, 3/3)

`dispFbp`/`displayFbp` observados en las 3 corridas: únicamente `0x0` y
`0xa0`. **0 apariciones de `0xe0` como `dispFbp`** en ninguna corrida.
**0 apariciones de `0x10e0` codificado**. El fix de P4.1 permanece
íntegro — este fix concierne exclusivamente al `FRAME` de draw, no a la
selección de display.

## Validación F — Resultado visual de película (HECHO, múltiples capturas, 3 corridas con `--visual-trace`)

Ventana temprana previamente afectada (post-`MPEG_INIT`, ~+0.7 a +4.1s),
RUN_064 (`MPEG_INIT`=77.406s): capturas en 77.671s (artefacto de
composición de pantalla — relleno magenta uniforme, no resultado de
render, descartada), 78.218s, 78.780s, 79.046s, 79.609s, 80.718s,
81.546s — **las 6 capturas de contenido real muestran los 5 párrafos
completos**, sin corte horizontal, sin alternancia completo/parcial.
Contraste directo con P4.1.1/P4.1.1B (mismo tipo de corrida, mismo
método, pre-fix): allí la alternancia completo↔parcial era visible en
capturas consecutivas de ~500ms; aquí, en una secuencia de igual
densidad temporal, no aparece ni una sola vez. **No se observa el corte
horizontal recurrente cerca de la fila 256 en ninguna de las 3
corridas.**

## Validación G — Flicker de UI no-MPEG (secundaria, evidencia estructural, sin A/B visual dedicado)

La Validación B (arriba) es, por construcción, una prueba estructural
para TODA la corrida (arranque+menú+película), no solo para la ventana
de película: **0 de ~20000-28677 draws totales por corrida usan
`FRAME=0xE0`**, en ninguna fase. Dado que P4.1.2 estableció que el
MISMO paquete de env contaminado servía tanto a los draws de película
como a los de UI genérica (misma estructura `db.draw11`/`draw12`,
reutilizada por cualquier código guest que dibuje al slot1), esta
evidencia estructural es suficiente para afirmar que el mecanismo de
flicker compartido queda eliminado igualmente para UI no-MPEG. **No se
realizó una comparación visual A/B dedicada de pantalla de memory
card/título** (fuera del alcance explícito y "secundario" de este
prompt) — se reporta `UI_FLICKER_IMPROVED: YES` con esta reserva
explícita, no como una validación visual completa.

## Validación H — No conflación con MPEG tardío (preservada)

No se investigó ni se intentó corregir la corrupción determinista de
MPEG (≥picture 43) en este prompt. Los `getPicture success` observados
en las 3 corridas (throttled, ≥11 cada una, corridas cortadas
deliberadamente temprano vía `--target 'getPicture success #10'`) no
alcanzan esa zona — sin datos nuevos sobre ese residual, como se
esperaba.

## Limpieza

`git -C vendor/PS2Recomp checkout --` sobre `MPEG.cpp`/`gs_cpu_backend.cpp`
(revertidos a `16ef7ab8...` exacto); `GS.cpp` se dejó con el único
cambio de producción. Reconstruido y re-linkeado desde esa fuente
(1 `CL.exe` por paso, 0 inesperados). Patch `patches/BLOCKER_004_p413_scegssetdefdbuffdc_drawenv_frame_zero.patch`
creado (SHA256 `b3437384c3bd2d15faafcf4be26d1f0ffe89224746cf49943f5e640764f1dac7`);
verificado por reconstrucción determinista independiente (clon temporal,
cadena completa de 8 patches, `git diff` contra el vendor real vacío
ANTES de normalizar) que el nuevo `patched_commit`
(`19911ce35fb8f2029853f25612b1cc420a8c74bb`, tree
`4cac0a023e0d42a7cf1d3e547d9f201a7f31303d`) es exacto; vendor real
normalizado a ese commit y confirmado limpio. `upstream.lock.json`
actualizado (8º patch + nuevo `patched_commit`). `scripts/pipeline.py
bootstrap` confirma consistencia sin recompilar nada.

## Respuestas requeridas

1. `const uint32_t fbp1 = zbufAddr;` → `const uint32_t fbp1 = 0u;`
   (`GS.cpp`, `sceGsSetDefDBuffDc`).
2. SÍ — `draw11`/`draw12` `FRAME`=`0x000` (antes `0xE0`), confirmado
   dinámicamente 3/3.
3. SÍ — `ZBUF`=`0xE0` sin cambio en ambos slots, 3/3.
4. SÍ — `ZMSK`=`1` sin cambio, 3/3.
5. SÍ — 0 draws reales con `FRAME=0xE0` en ~76000 draws observados
   across 3 corridas.
6. `FRAME=0x0` y `FRAME=0xA0` (ping-pong correcto, nunca `0xE0`).
7. SÍ — subida a `FBP=0x000` sigue completa (inferido por Validación D,
   nunca fue el problema).
8. SÍ — subida a `FBP=0x0A0` sigue completa (confirmado: sus filas
   256-447 ahora SOBREVIVEN hasta la presentación, lo que solo es
   posible si la subida las escribió).
9. SÍ — filas 256-447 de `FBP=0xA0` preservadas tras la fase de draw,
   0/N incompletas en las 3 corridas (antes 52/52).
10. Desapareció por completo: 0/45, 0/45, 0/44 post-fix vs 52/52
    pre-fix (P4.1.1B, mismo método).
11. SÍ — `DISPFB2` sigue alternando `0x000`/`0x0A0`, 3/3, sin excepción.
12. NO — 0 apariciones de `0x10E0` codificado en ninguna corrida.
13. SÍ — `[P314:GP]`=24, `[P3142:ring]`=23 en las 3 corridas, gate
    verificado ANTES de interpretar cualquier evidencia.
14. SÍ — `[GP:H]`=0 en las 3 corridas; `getPicture success`≥11
    (muestreado) en cada una, sin estancamiento.
15. SÍ — confirmado visualmente en RUN_064 (6/6 capturas de contenido
    real completas, sin corte, en la ventana previamente afectada).
16. Mejora estructural confirmada (0 draws a `0xE0` en toda la corrida,
    no solo en la ventana de película); sin comparación visual A/B
    dedicada de menú/memory-card — reservado como `YES` con esa
    limitación explícita.
17. No investigado en este prompt — se asume sin cambios (fuera de
    alcance, corridas cortadas antes de esa zona deliberadamente).
18. No medido específicamente en este prompt (corridas cortadas
    temprano); sin evidencia de cambio.
19. NO — el diff completo confirma que ningún otro comportamiento de
    producción cambió (`zbufAddr`, `ZBUF`, `DISPFB`, MPEG, subida GIF,
    `preferredSource`, FIELD, scheduler y arena runtime intactos).
20. SÍ — 3/3 corridas independientes, mismo resultado exacto en todas
    las validaciones obligatorias (A, B, D, E), mismo hash de
    ejecutable en las 3 (`6a8dd8855f985c54e921aef82b5c08e6b1a650c2e53e86269c71229d349e0312`).
21. SÍ — causal, no meramente visual: el mecanismo exacto (draw con
    `FRAME=0xE0` sobre `FBP=0xA0`) fue localizado por P4.1.2, corregido
    con un cambio de una línea, y su ausencia (0 draws a `0xE0`) se
    verificó directamente, no solo su efecto visual.
22. SÍ — success gate cumplido en su totalidad.

## Clasificación

`P413_DRAWENV_FRAME_FIX_CAUSAL_AND_VALIDATED`

## Residuales de BLOCKER_004 tras este fix (BLOCKER_004 permanece OPEN)

- Corrupción determinista tardía de MPEG (≥picture 43, camino de
  feed/recirculación) — residual mayor conocido, sin tocar aquí.
- Flicker general de UI/presentación — mejora estructural esperada y
  parcialmente confirmada (Validación G), sin comparación visual
  dedicada; posible residual adicional no descartado.
- Semántica PSM/formato de píxel (P3.15.2) — sin auditar exhaustivamente.
- `AMOD` (bit6 `PMODE`) — divergencia de paridad SDK conocida desde
  P4.0.2, deliberadamente no corregida en ningún prompt hasta ahora.
