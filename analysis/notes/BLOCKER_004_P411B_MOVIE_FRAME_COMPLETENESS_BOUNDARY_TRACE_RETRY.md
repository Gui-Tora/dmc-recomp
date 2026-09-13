# P4.1.1B — MOVIE_FRAME_COMPLETENESS_BOUNDARY_TRACE_RETRY

Reintento de P4.1.1 tras resolver su error de entorno de lanzamiento
(P4.1.1R). El fix P4.1 (`disp[0].dispfb`) permanece
`P41_FIX_CAUSAL_AND_VALIDATED`, sin reconsiderar.

## Correction ledger

- **P4.1.1**: conclusiones dinámicas inválidas — el camino síncrono
  P3.14.2 nunca estuvo activo en ninguna de las 12 corridas (variable de
  entorno `DMC_P314_SYNC_REDECODE` ausente del script invocador).
- **P4.1.1R**: causa identificada (variable de entorno faltante en el
  invocador, no un problema de build/binario/vendor); línea base
  restaurada y confirmada (RUN_059: 0 `[GP:H]`, ≥150 éxitos).
- **P4.1.1B** (este informe): reintento válido, con activación del
  camino síncrono probada explícitamente ANTES de recolectar evidencia
  (Fase 11 obligatoria) — primer divergencia encontrada y confirmada
  dinámicamente con una sola corrida.

## Estado de entrada (verificado)

Main HEAD `c9ee65d` ✓, sin dirt ajeno (solo los 3 informes ya esperados
sin trackear). Vendor HEAD `16ef7ab8dc961c44921c3776705da6bc3b4806fb` ✓,
limpio ✓.

## Puerta obligatoria de entorno (satisfecha antes de tocar código)

Script invocador (`$CLAUDE_JOB_DIR/tmp/p411b_run.sh`) fija explícitamente,
en la MISMA invocación que ejecuta `run.py` (sin depender de estado de
shell heredado):

```
export PS2X_CD_IMAGE="...Devil May Cry 2001.iso"
export PS2X_RUNTIME_ARENA_BASE=0x009FA000
export PS2X_RUNTIME_ARENA_LIMIT=0x00ABE000
export DMC_P313_INIT_MODE=ownership
export DMC_P314_SYNC_REDECODE=1
export DMC_P411B_FRAME_BOUNDARY_TRACE=1
echo "LAUNCH_ENV_CHECK: ..."   # verificado impreso ANTES de invocar run.py
```

`LAUNCH_ENV_CHECK` confirmado en el log del harness:
`DMC_P314_SYNC_REDECODE=1 DMC_P313_INIT_MODE=ownership
PS2X_RUNTIME_ARENA_BASE=0x009FA000 PS2X_RUNTIME_ARENA_LIMIT=0x00ABE000
PS2X_CD_IMAGE_SET=yes`.

## Instrumentación implementada

Un único flag opt-in `DMC_P411B_FRAME_BOUNDARY_TRACE=1`, firmas de 7
bloques de 64 filas (hash FNV-1a de R/G/B + conteo no-negro), escritura
única por línea (evita el storm de flush de `std::cerr` con `unitbuf`,
optimización heredada de P4.1.1 aunque ya no se cree necesaria — ver
P4.1.1R):

- **`MPEG.cpp`** (boundary A/B): antes/después de
  `writeDecodedFrameToGuest`, gateado por `haveFrame`, etiqueta
  `picture=<frameCount>`.
- **`gs_cpu_backend.cpp`** (boundary C/D/E dentro de `copySource`; F tras
  `applyFieldPresentation`): C/D = lectura cruda de VRAM en el
  `displayFrame` (DISPFB2) activo; E = estado completo de
  `preferredSource` (`dispFbp`, `prefValid`, `prefSrcFbp`,
  `prefDestFbp`, `usedPreferred`, `selFbp`) + contenido final
  seleccionado; PREFIELD = mismo contenido antes de
  `applyFieldPresentation`; POSTFIELD = después, con `vsyncTick`/
  `oddField`/`fieldMode`/`sourceFbp`.

Build quirúrgico verificado: `grep -c "CL.exe"` = 1 (exactamente
`MPEG.cpp` + `gs_cpu_backend.cpp` en un solo lote), relink
`_BuildLinkAction` con `CL.exe` = 0.

## Fase 11 — Aserción obligatoria de activación síncrona (satisfecha)

`RUN_061` (`exe_sha256=b90a4f4e70b7fc0bb71915ec11bc29d480074a59b79353a409ca089112b08bd2`,
`result=SUCCESS`, 75.9s): `[P314:GP]`=24, `[P3142:ring]`=23, `[GP:H]`=0,
`getPicture success`≥11 (muestreado hasta #10, target alcanzado). **Camino
síncrono confirmado activo antes de interpretar cualquier evidencia de
límites de frame** — exactamente como exige la Fase 11. (RUN_060, intento
previo, terminó en `HARNESS_ERROR` por un proceso `dmc-recomp` previo aún
vivo al lanzar — reintentado limpiamente sin tocar código.)

## Fase 1 — Ventana temprana confirmada en vivo

Con `--visual-trace`, capturas de la MISMA secuencia buena→mala→buena ya
conocida de P4.1.1, ahora dentro de una corrida con P3.14.2
genuinamente activo:

- `visual_071500ms.png`: 5 párrafos completos (BUENO).
- `visual_071782ms.png`: **solo 3 párrafos** (English/German/French),
  corte horizontal limpio, mitad inferior negra (MALO).
- `visual_072313ms.png`: 5 párrafos completos de nuevo (BUENO).

`MPEG_INIT` en esta corrida: 69.625s (vs 72.3s en el baseline original
RUN_044 — variación normal de timing entre corridas). Usando
`picture=0`'s timestamp interno (`t=301897969`, línea de log
inmediatamente después de `[MPEG:init]`) como ancla para convertir entre
el reloj `steady_clock` interno del proceso y el reloj `elapsed` del
harness, el frame MALO (71.782s) cae en la ventana `picture≈15-19` —
consistente con la estimación de P4.1.1 (~17-40) y muy anterior a
picture 43.

## Fase 2/3 — Boundary A (decodificado FFmpeg) y B (guest 0x79D680)

**HECHO, respuesta directa a las preguntas 6-8**: para CADA picture de la
ventana observada (0-39), los 7 bloques de fila en boundary A tienen
conteo no-negro > 0 en los 7 — **el frame decodificado por FFmpeg está
SIEMPRE completo**, nunca hay bloques negros. Boundary B (lectura del
guest `0x79D680` tras `writeDecodedFrameToGuest`, vía el mismo layout de
franjas que la cadena GIF real consume) es **hash-idéntico byte a byte**
a boundary A para cada picture (verificado picture 15-19 explícitamente,
patrón consistente en toda la muestra). `writeDecodedFrameToGuest`
preserva las 448 filas sin excepción.

**Primer divergencia: NO está en decode ni en la escritura a guest.**

## Fase 4 — Boundary C/D: GS VRAM tras la subida — AQUÍ ESTÁ LA DIVERGENCIA

**HECHO, hallazgo principal**, calculado sobre el rango completo de la
ventana de película (t ≥ MPEG_INIT, 613 muestras de `Present()`):

| Buffer (DISPFB2 activo) | Muestras | Bloques 4-6 (filas 256-447) completamente negros |
|---|---|---|
| `FBP=0x000` (disp[0], el corregido por P4.1) | 50 | **0/50 (0%)** |
| `FBP=0x0A0` (disp[1]) | 52 | **52/52 (100%)** |

Sin ambigüedad ni intermitencia: **cada vez** que el DISPFB2 activo es
`0xA0`, las filas 256-447 (bloques 4, 5, 6 — exactamente el ~43% inferior
del cuadro, complementario al "~58% superior" ya estimado visualmente)
están en negro puro (hash `c6ba2821652f0383`, la constante de negro total
ya vista en toda la sesión), mientras las filas 0-255 (bloques 0-3)
contienen datos reales — confirmado por coincidencia de hash EXACTA con
el contenido real de boundary A/B de la picture correspondiente (p. ej.
el bloque 0 de una muestra `dispFbp=0xa0` en `t=301899955` iguala
byte-exacto al bloque 0 de picture=15 en boundary A/B; una muestra
posterior en `t=301900178` iguala a picture=17 — el buffer SÍ se
actualiza con contenido nuevo en su mitad superior en cada ciclo, nunca
en la inferior).

`FBP=0x000` nunca mostró este patrón ni una sola vez en las 50 muestras
post-MPEG_INIT observadas.

**Esto es exactamente la causa geométrica del defecto**: la mitad
inferior del cuadro (filas 256-447) del buffer `0xA0` nunca recibe
contenido de la subida de película — no es una carrera intermitente, es
un estado estructural y perfectamente reproducible dentro de esta
corrida. Cuando la presentación muestrea el DISPFB2 en un momento en que
apunta a `0xA0` (que alterna con `0x000` aproximadamente cada 2 ticks de
vsync, por el propio esquema de doble buffer ya establecido desde
P4.0.1), el resultado es exactamente el defecto observado: 3 de 5
párrafos (los que caen en filas <256) visibles, los 2 restantes
(`ATTENZIONE`/`ADVERTENCIA`, que caen en filas ≥256 en el layout real de
la pantalla de advertencia) ausentes.

## Fase 5 — Coreografía clear/upload/flip

**Reclasificado, no confirmado en su forma original**: la evidencia no
muestra un clear/fade que SOBRESCRIBE contenido ya subido a `0xA0`
después del hecho — muestra que esa región **nunca recibe contenido en
primer lugar**, de forma consistente y repetida en cada ciclo. Esto es
más compatible con una asimetría de geometría/dirección de la subida GIF
específica del slot `0xA0` (p. ej. un `TRXREG`/`DSAY`/`DBP` distinto al
del slot `0x000` para ese destino) que con una carrera de
"clear-después-de-upload". **No instrumentado directamente en este
prompt** (la cadena GIF/BITBLTBUF específica del slot `0xA0` no se trazó
byte a byte) — candidato concreto para P4.1.2.

## Fase 6 — Auditoría `preferredSource`: descartado dinámicamente para este defecto

**HECHO**: `prefValid=0` y `usedPreferred=0` en las 613 muestras de la
corrida completa, sin una sola excepción dentro de la ventana de
película. `selFbp` es idéntico a `dispFbp` en el 100% de las muestras de
la ventana de película (2 excepciones encontradas, ambas ANTES de
`MPEG_INIT`, en pantalla de menú/boot, no relacionadas con la película —
ver nota abajo). El mecanismo `preferredSource` **no está causando el
defecto observado en esta corrida** — permanece como candidato estático
válido para otros escenarios, pero no es la explicación de este
medio-cuadro temprano.

*Nota lateral, fuera de alcance*: en `t=301855355` y `t=301860054`
(ambos antes de `MPEG_INIT`, fase de menú), sí se observó una
sustitución completa hacia `selFbp=0x70` con el mismo patrón de "solo
bloques 0-3 con contenido" — sugiere que el fallback de negro total
(Fase 7) también puede producir contenido con la misma incompletitud
estructural cuando la fuente sustituta ES ella misma un buffer parcial,
pero esto ocurre en pantalla de menú, no en la ventana de película
investigada. No perseguido más allá por estar fuera del alcance
declarado de este prompt.

## Fase 7 — Caso especial FBP==0

**HECHO, sin cambios respecto a P4.1.1**: dentro de la ventana de
película, el fallback de negro total (`countNonBlackPixels==0`) nunca se
activó (`FBP=0x000` nunca tuvo count total cero). El defecto de esta
investigación no pasa por esa rama.

## Fase 8/9 — Pre-field y post-field

**HECHO**: las firmas PREFIELD y POSTFIELD replican EXACTAMENTE el mismo
patrón de incompletitud que C/D — bloques 4-6 en negro cuando
`dispFbp=0xA0`, completos cuando `dispFbp=0x000`, en el 100% de las
muestras correspondientes. El procesamiento de campo (`applyFieldPresentation`)
ni corrige ni agrava el defecto — simplemente propaga fielmente el
contenido de VRAM ya incompleto. **FIELD exonerado dinámicamente**
(confirma el análisis estático ya hecho en P4.1.1).

## Tabla de capas requerida

Frame BUENO de referencia: picture≈16 (`t≈301899921`, `dispFbp=0x000`).
Frame MALO de referencia: muestra `dispFbp=0xA0` en `t≈301899955`
(contenido superior = picture 15, sin actualizar en las filas 256-447).

| Boundary | BUENO | MALO | ¿Completo? | Evidencia |
|---|---|---|---|---|
| decoded host frame | 7/7 bloques no-negro | 7/7 bloques no-negro (la MISMA picture, leída del lado FFmpeg, está completa) | **MATCH — ambos completos** | Fase 2, HECHO |
| guest `0x79D680` | idéntico a host | idéntico a host | **MATCH — ambos completos** | Fase 3, HECHO |
| GS VRAM tras 32 franjas (`dispFbp`) | `0x000`: 7/7 bloques no-negro | `0xA0`: **solo bloques 0-3**, 4-6 en negro puro | **DIVERGE AQUÍ** | Fase 4, HECHO — 50/50 vs 52/52 |
| fuente seleccionada (`preferredSource`/`selFbp`) | `selFbp=dispFbp=0x000`, sin sustitución | `selFbp=dispFbp=0xA0`, sin sustitución | igual patrón que VRAM (no hay sustitución que lo cambie) | Fase 6, HECHO |
| pre-field host RGBA | 7/7 completo | solo bloques 0-3 | **hereda el defecto de VRAM, sin cambio** | Fase 8, HECHO |
| post-field | 7/7 completo | solo bloques 0-3 | **hereda el defecto, campo no lo causa ni lo arregla** | Fase 9, HECHO |
| captura final | 5 párrafos (`visual_071500ms.png`) | 3 párrafos, corte en fila ~256 (`visual_071782ms.png`) | **confirma visualmente el mismo corte** | Fase 1, HECHO |

## Respuestas requeridas

1. ¿`DMC_P314_SYNC_REDECODE=1` activo? **SÍ**, confirmado antes y durante.
2. ¿`[P314:GP]` apareció? **SÍ**, 24 líneas.
3. ¿`[P3142:ring]` apareció? **SÍ**, 23 líneas.
4. Picture/timestamp BUENO trazado: **picture≈16, ~71.5-72.3s** (múltiples muestras `dispFbp=0x000`).
5. Picture/timestamp MALO trazado: **contenido de picture≈15/17 en buffer `dispFbp=0xA0`, ~71.78s**.
6. ¿MALO ya incompleto en FFmpeg? **NO** — siempre completo (Fase 2).
7. ¿`writeDecodedFrameToGuest` preserva todas las filas? **SÍ** (Fase 2-3).
8. ¿Guest `0x79D680` completo? **SÍ**, siempre (Fase 3).
9. ¿GS VRAM completo tras la franja final? **NO para `FBP=0xA0`** (0% de 52 muestras), **SÍ para `FBP=0x000`** (100% de 50 muestras).
10. ¿Qué FBP contiene la picture MALA? **`0x0A0`** (disp[1]).
11. ¿Un clear/fade modifica ese FBP tras la subida? **No confirmado como mecanismo** — la evidencia apunta a que esa región nunca recibe contenido en absoluto, no a que se borre después (Fase 5, reclasificado).
12. ¿Qué DISPFB2 selecciona el guest? Alterna `0x000`/`0x0A0` (patrón ya conocido desde P4.0.1, confirmado de nuevo).
13. ¿Qué `sourceFbp` copia finalmente el runtime? Siempre igual al `dispFbp` activo (sin sustitución, Fase 6).
14. ¿`preferredSource` anula el DISPFB2 del guest? **NO**, nunca activo en la ventana de película (Fase 6).
15. ¿Se trata `FBP=0` especialmente? Solo vía el fallback de negro total (Fase 7), no activado en esta ventana.
16. ¿Pre-field host RGBA completo? **NO cuando `dispFbp=0xA0`** (hereda el defecto), **SÍ cuando `dispFbp=0x000`**.
17. ¿Post-field RGBA completo? Mismo patrón que pre-field — el campo no cambia la completitud.
18. ¿La paridad de FIELD correlaciona con el defecto? **NO** — `oddField` se reparte ~50/50 (307/306) independientemente de qué FBP esté activo.
19. ¿La paridad de framebuffer correlaciona? **SÍ, perfectamente** — no es "paridad" en sentido estricto, es el FBP específico: `0xA0` = 100% defectuoso, `0x000` = 0% defectuoso.
20. Primer capa exacta donde BUENO y MALO divergen: **GS VRAM tras la subida de película, específicamente para el destino `FBP=0x0A0`** (boundary C/D) — decode y escritura a guest son siempre idénticos e completos para ambos.
21. ¿El parpadeo temprano sigue siendo independiente de la corrupción tardía de MPEG? **SÍ** — toda la ventana analizada (pictures 0-39) tiene boundary A/B siempre completos, consistente con estar muy antes de picture 43.
22. ¿Se conoce ya un fix de producción? **NO todavía** — se sabe DÓNDE (el destino `0xA0` de la subida de película nunca recibe sus filas 256-447), pero no el mecanismo exacto que causa esa subida parcial específica de ese slot (requiere trazar la cadena GIF/BITBLTBUF real del slot `0xA0`, no instrumentada en este prompt).

## Fase 14 — Limpieza

`git -C vendor/PS2Recomp checkout --` sobre los 2 archivos tocados
(`MPEG.cpp`, `gs_cpu_backend.cpp`); verificado `git diff
16ef7ab8dc961c44921c3776705da6bc3b4806fb --stat` vacío. Reconstruido
desde la fuente revertida (build quirúrgico, 1 `CL.exe` = exactamente
esos 2 archivos, 0 errores) y re-linkeado (`_BuildLinkAction`, 0
`CL.exe` inesperados) para dejar el binario activo coherente con la
fuente committeada. Sin patch de vendor nuevo. Sin proceso
`dmc-recomp.exe` en ejecución al cierre.

## Clasificación

`P411_GS_TRANSFER_COMPLETENESS_DIVERGENCE`

Justificación: decode (A) y escritura a guest (B) son siempre completos
para las 40 pictures observadas; la divergencia aparece exactamente en
el contenido de VRAM del destino de presentación `FBP=0x0A0` (C/D),
donde las filas 256-447 nunca se pueblan (52/52 muestras, 0/50 en el
buffer hermano `FBP=0x000`) — y se propaga sin cambios a través de
selección de fuente, pre-field y post-field hasta la captura final,
coincidiendo exactamente con el patrón visual de "3 de 5 párrafos"
observado.

---

Resultado Prompt P4.1.1B — MOVIE_FRAME_COMPLETENESS_BOUNDARY_TRACE_RETRY

PRIMARY_CLASSIFICATION:
P411_GS_TRANSFER_COMPLETENESS_DIVERGENCE

SYNC_PATH_CONFIRMED:
YES

GOOD_FRAME_REFERENCE:
RUN_061, picture≈16, dispFbp=0x000, ~71.5-72.3s (visual_071500ms.png/visual_072313ms.png)

BAD_FRAME_REFERENCE:
RUN_061, dispFbp=0x0A0 (contenido de picture≈15/17 en su mitad superior), ~71.78s (visual_071782ms.png)

LAST_PROVEN_COMMON_NODE:
guest 0x79D680 tras writeDecodedFrameToGuest (byte-idéntico al frame FFmpeg decodificado, 7/7 bloques completos, para ambas pictures)

FIRST_PROVEN_DIVERGENT_NODE:
GS VRAM en el destino de presentación FBP=0x0A0 — filas 256-447 (bloques 4-6) nunca reciben contenido de la subida de película (52/52 muestras incompletas vs 0/50 en FBP=0x000)

PREFERRED_SOURCE_CAUSAL:
NO

FIELD_CAUSAL:
NO

EARLY_FLICKER_INDEPENDENT_OF_LATE_MPEG_CORRUPTION:
YES

IMPLEMENTATION_READY:
NO

NEXT:
P4.1.2 — MOVIE_FBP0XA0_PARTIAL_UPLOAD_ROOT_CAUSE: trazar la cadena GIF/BITBLTBUF real que sube contenido de película al slot FBP=0x0A0 específicamente (TRXREG/DSAY/DBP de esa subida en particular, comparada byte a byte contra la del slot FBP=0x000 que sí es completa) para determinar por qué esa subida en particular nunca cubre las filas 256-447.

CHECKPOINT_DECISION:
NO_COMMIT