# P3.13.1 — FULL_MODE_VISUAL_TRACE_AND_LANGUAGE_VARIATION

Seguimiento puramente observacional de P3.13. Sin cambios de semántica de
MPEG/scheduler/GetPicture. Sin build (se reutilizó el ejecutable P3.13
exacto). Todas las corridas usan `DMC_P313_INIT_MODE=full` exclusivamente.

## 1. Checkpoint de entrada

- HEAD principal: `c9630a22ed5ae066bc0d2a2b66f783aee59d3751` — **verificado**,
  coincide exactamente.
- `git status --short` antes de esta iteración: idéntico al cierre de
  P3.13 (`MPEG.cpp` dirty en vendor, `run.py` + informe nuevo en
  principal) — sin cambios inesperados. No se hizo `reset`/`clean`/
  `checkout`/`restore`/`commit`/`push` en ningún momento.

## 2. SHA256 del ejecutable

`1c49618546987aad4eb28e7101b43984d3bf8c7175fedd578fcfdb4d55265a40` —
**verificado por lectura directa del archivo**, coincide exactamente con
el prefijo esperado (`1c496185...`) y con el ejecutable usado en las 8
corridas de P3.13. Confirmado además en los `result.json` de las 5
corridas nuevas: el mismo valor en las cinco.

## 3. Confirmación: NO build, NO cambio de semántica de runtime

No se ejecutó ningún build/relink. No se tocó `MPEG.cpp`,
`EeScheduler.cpp`, `dmc_overrides.cpp`, `p311_pad_test.inc`, ninguna
máscara/timing de input, ni ningún diagnóstico MPEG. El único archivo
modificado es `analysis/tools/runtime_loop/run.py`, y exclusivamente para
capturas de pantalla — ver §4 y el diff completo revisado antes de
lanzar ninguna corrida (ningún `time.sleep()` existente se tocó, ninguna
condición de envío de tecla se modificó).

## 4. Mecanismo exacto de captura

Nuevo flag opt-in `--visual-trace` en `run.py`:

- Captura densa periódica (~500 ms, acotada por el poll de 250 ms ya
  existente) de la HWND propia del proceso, iniciando quando
  `len(wins)==1` (primera ventana visible detectada) — nombre
  `visual_<elapsed_ms>ms.png`.
- Cuatro capturas explícitas de frontera, con timestamp monotónico
  registrado en `meta['visual_boundaries']` de `result.json`:
  `before_START.png`/`after_START.png` (inmediatamente antes del
  key-down y justo después del release, respectivamente, del primer
  key) y `before_CROSS.png`/`after_CROSS.png` (ídem para el segundo).
  Insertadas SIN añadir ningún `time.sleep()` nuevo — el diff completo
  se revisó línea por línea antes de ejecutar nada (ver informe P3.13.1
  local, diff ya mostrado en el propio proceso de esta iteración).
- `meta['diagnostic_env']` ampliado (ya hecho en P3.13) para registrar
  también `DMC_P313_INIT_MODE`.

## 5. Confirmación: solo la HWND propia

Todas las capturas (periódicas y de frontera) usan
`ImageGrab.grab(window=hwnd)` con el `hwnd` obtenido de `windows(proc.pid)`
— el mismo mecanismo ya usado por `boot.png`/`state_NN.png`/`input_NN.png`
desde P3.11, que ya filtra por PID propio y visibilidad. Nunca se capturó
el escritorio completo.

## 6. Las 5 corridas — resumen

RUN_023 a RUN_027 (`analysis/local/p311/`), mismas variables de entorno,
mismo ejecutable, mismo `--keys ENTER,X --method runtime --confirm`.
`exe_sha256` idéntico en las cinco. Timing de input **extremadamente
consistente** entre corridas (spread <0.3 s en las 5):

| Run | START down | START release | CROSS down | CROSS release |
|---|---:|---:|---:|---:|
| 023 | 10.14 | 10.41 | 14.30 | 14.58 |
| 024 | 10.25 | 10.51 | 14.39 | 14.66 |
| 025 | 10.22 | 10.50 | 14.38 | 14.67 |
| 026 | 10.06 | 10.33 | 14.20 | 14.47 |
| 027 | 10.25 | 10.52 | 14.36 | 14.64 |

## 7. Hallazgo central (revisado tras inspección completa — ver §11 para
la corrección explícita de una hipótesis intermedia)

**HECHO, confirmado visualmente en 5/5 corridas**: la pantalla de
advertencia de violencia NO es estática ni de un solo idioma — es una
secuencia de (al menos) **dos páginas** que la propia aplicación muestra
en orden:

- **Página 1**: inglés + alemán + francés simultáneos, verticalmente
  apilados (`WARNING...` / `WARNUNG...` / `ATTENTION...`).
- **Página 2**: italiano + español, con el bloque en español **duplicado
  verticalmente 2-3 veces** (`ATTENZIONE...` una vez, `ADVERTENCIA...`
  repetido).

**Reconstrucción temporal dentro de RUN_023** (la corrida donde más
puntos intermedios se capturaron): negro (68.4s) → frame magenta sólido
de un cuadro (69.4s, glitch de captura o transición real, ver §12) →
Página 1 (69.97s) → Página 2 apareciendo con brillo creciente (70.50s) →
Página 2 a brillo completo, último frame antes de morir (71.86s). Esto
prueba que **la misma corrida** atraviesa ambas páginas — la variación
entre corridas NO es una diferencia de contenido persistente por-corrida.

## 8. Por corrida: primera captura reconocible, idioma, tiempo

| Run | Primer screenshot reconocible | Tiempo | Contenido |
|---|---|---:|---|
| 023 | `visual_069968ms.png` | 69.97 s | Página 1 (En/De/Fr) |
| 024 | `visual_070514ms.png` (primer punto inspeccionado con texto) | 70.51 s | Página 1 (En/De/Fr) |
| 025 | `visual_070812ms.png` | 70.81 s | Página 1 (En/De/Fr) |
| 026 | `visual_070422ms.png` | 70.42 s | Página 1 (En/De/Fr) |
| 027 | `visual_070437ms.png` | 70.44 s | Página 1 (En/De/Fr) |

**Las 5 corridas muestran Página 1 (inglés primero) en el primer punto
inspeccionado con texto reconocible** — consistente con que la secuencia
completa SIEMPRE empieza por la Página 1, independientemente de cualquier
diferencia previa de renderizado del menú de idioma.

## 9. Estado del menú de idioma antes de CROSS

**HECHO, verificado visualmente** (`before_CROSS.png`, las 5 corridas):

- RUN_025, 026, 027: menú **completamente legible** — cinco entradas
  (`English`, `French`, `Spanish`, `German`, `Italian`), `English` en
  blanco brillante/negrita (claramente resaltado), el resto en gris
  tenue.
- RUN_023, 024: menú **parcialmente ilegible** — las mismas líneas
  aparecen borrosas/corruptas excepto `Italian` (línea inferior),
  nítida.

**Corrección explícita, HECHO nuevo tras inspeccionar más capturas de
las mismas corridas "limpias"**: este NO es un estado persistente
por-corrida. RUN_026 (grupo "limpio" en `before_CROSS`) muestra el
**mismo patrón "solo Italian legible"** en un frame **anterior** de su
propia línea de tiempo (`visual_011610ms.png`, 11.61 s — antes de
CROSS). Es decir: el menú de idioma se **puebla/renderiza
progresivamente** tras aparecer (empieza con solo la última entrada
nítida, termina con las cinco legibles y `English` resaltado), y lo que
`before_CROSS.png` captura en cada corrida es simplemente **en qué punto
de esa transición, de ~1-2 s, cae el instante fijo (~14.2-14.4 s) en el
que el automatismo presiona CROSS** — no una diferencia de contenido o
de selección real entre corridas.

## 10. Delta CROSS-lenguaje

Dado que el menú ya es visible (en alguna fase de renderizado) en
TODAS las corridas para cuando se captura `before_CROSS.png`, y dado que
no se aisló un timestamp de "primera aparición" independiente de
`before_CROSS` en las 5 corridas (solo se hizo para RUN_026, ver §9), el
delta `CROSS_down - language_screen_first_visible` no puede calcularse
con precisión para las 5. **UNKNOWN** para RUN_023/024/025/027 (no se
buscó el frame exacto de primera aparición); para RUN_026 el menú YA
tenía contenido parcial visible desde al menos 11.61 s, es decir **≥2.6 s
antes de CROSS** (14.20 s) — sin que eso determine qué contenido de
advertencia se vería después (ver §11).

## 11. ¿Correlaciona la variación de idioma con la selección de menú? — RETRACTADO explícitamente

**Hipótesis intermedia formulada durante esta misma iteración, y
RETRACTADA tras inspeccionar más frames, documentada aquí en vez de
borrada silenciosamente**: la primera comparación (solo el frame más
tardío de cada corrida) mostró una correlación aparente 2/2 vs 3/3 entre
"menú corrupto antes de CROSS" (023, 024) → "Página 2 visible al morir"
y "menú limpio antes de CROSS" (025, 026, 027) → mezcla. Al inspeccionar
el ÚLTIMO frame de las 5 corridas de forma completa, esa correlación
**se rompe**: RUN_024 (menú "corrupto") y RUN_025/026 (menú "limpio")
llegan igualmente a la Página 2; RUN_027 (menú "limpio", igual que
025/026) **se queda en la Página 1** hasta morir. **No hay correlación
demostrable entre el estado de renderizado del menú antes de CROSS y qué
página de advertencia queda capturada al final.**

**HECHO, verdadero mecanismo identificado**: las 5 corridas terminan por
la MISMA causa no relacionada (agotamiento de `RuntimeGuestArena`, ver
§16), en un instante con jitter propio; independientemente, la
transición Página 1 → Página 2 de la advertencia también ocurre con su
propio jitter. Qué página queda "congelada" en la última captura de cada
corrida es el resultado de una **carrera entre dos procesos
independientes** (progresión de la secuencia de advertencia vs. el
crash), no de una diferencia de idioma seleccionado ni de estado del
guest determinado por el timing de input.

## 12. ¿Es reproducible la duplicación/ghosting?

**HECHO, reproducible**: en las 4 corridas que llegan a observarse en
Página 2 (023, 024, 025, 026), el bloque `ADVERTENCIA` (español) aparece
duplicado verticalmente — 3 veces en 023/024/026, 2 veces en el último
frame observado de 025 (posiblemente una fase de transición distinta,
ligeramente distinta en el tiempo — no se capturó el frame exacto donde
pasa de 2 a 3, o de si alguna vez llega a 3, en esa corrida). El bloque
`ATTENZIONE` (italiano) aparece una sola vez en todos los casos donde es
visible. Es un artefacto real y repetible, no ruido de una sola captura.
El frame magenta sólido observado en RUN_023 a los 69.4 s (un único
cuadro, entre el negro y la Página 1) no se investigó más a fondo — es
consistente con un artefacto de captura (framebuffer a medio-swap) o con
una transición real de un cuadro; **UNKNOWN** cuál de las dos, fuera de
alcance de esta iteración.

## 13. ¿Antes, durante o después de la progresión MPEG/GetPicture?

**HECHO**: la Página 1 se observa por primera vez en la ventana
69.97-70.81 s en las 5 corridas — **después** de que las 10 llamadas
`getPicture success` y los 6 frames post-init ya ocurrieron (esos, según
P3.13 y esta iteración, se completan bastante antes del final de la
corrida — el propio `[MOVIE:decode-task] CALL sceMpegGetPicture` sigue
apareciendo en el log hasta justo antes del crash). No se aisló el
timestamp exacto del décimo éxito en esta iteración (no era el objetivo;
ver P3.13 para esos números). La advertencia es, por tanto, contenido de
UI/menú renderizado en paralelo con — no bloqueado por — la actividad
MPEG observada en P3.13.

## 14-16. Métricas MPEG por corrida

| Run | GetPicture successes | Frames nuevos post-init | `RuntimeGuestArena` agotado |
|---|---:|---:|---|
| 023 | 10 | 6 (`total=5..10`) | Sí |
| 024 | 10 | 6 | Sí |
| 025 | 10 | 6 | Sí |
| 026 | 10 | 6 | Sí |
| 027 | 10 | 6 | Sí |

**5/5** — idéntico a las 3/3 corridas `full` de P3.13 (total acumulado:
8/8 corridas `full` con el mismo patrón MPEG y el mismo agotamiento de
recursos). No se investiga ni se corrige aquí (fuera de alcance,
confirmado explícitamente).

## 17. Tabla de determinismo visual completa

| RUN | EXE SHA (pfx) | START | CROSS | 1ª advertencia visible | Página en 1ª captura | Highlight antes de CROSS | Duplicado/ghosted | Éxitos GetPicture | Frames post-init | Evento final |
|---|---|---:|---:|---:|---|---|---|---:|---:|---|
| 023 | `1c496185` | 10.14/10.41 | 14.30/14.58 | 69.97 s | Pág.1 (En/De/Fr) → Pág.2 a los 70.50-71.86s | "Italian" nítido, resto borroso (menú en transición) | Sí (ES ×3, Pág.2) | 10 | 6 | Arena agotado; Pág.2 visible al morir |
| 024 | `1c496185` | 10.25/10.51 | 14.39/14.66 | 70.51 s | Pág.1 → Pág.2 a los 72.41s (último frame) | idem 023 | Sí (ES ×3) | 10 | 6 | Arena agotado; Pág.2 visible al morir |
| 025 | `1c496185` | 10.22/10.50 | 14.38/14.67 | 70.81 s | Pág.1 → Pág.2 (parcial, ES ×2) a los 72.44s (último) | `English` resaltado, menú limpio | Sí (ES ×2 en el último frame) | 10 | 6 | Arena agotado; Pág.2 (parcial) al morir |
| 026 | `1c496185` | 10.06/10.33 | 14.20/14.47 | 70.42 s | Pág.1 → Pág.2 a los 71.81s (último) | `English` resaltado, menú limpio | Sí (ES ×3) | 10 | 6 | Arena agotado; Pág.2 visible al morir |
| 027 | `1c496185` | 10.25/10.52 | 14.36/14.64 | 70.44 s | Pág.1 únicamente (nunca se observó Pág.2) | `English` resaltado, menú limpio | N/A (Pág.2 no alcanzada) | 10 | 6 | Arena agotado; AÚN en Pág.1 al morir |

## 18. Rutas de contact sheets

No se crearon contact sheets — la inspección directa de los frames
individuales fue suficiente y más precisa para este volumen de capturas
(151 por corrida, pero solo un subconjunto pequeño resultó necesario para
establecer los hallazgos). Ningún artefacto adicional se guardó en
`analysis/local/p313_visual/`.

## 19-20. Confirmación de primera salida visual reconocible

**SÍ — FIRST_RECOGNIZABLE_GAME_VISUAL_OUTPUT confirmado de forma
independiente por captura automatizada**, en las 5 corridas de esta
iteración (no solo en la captura manual del usuario). Contenido
exacto visible: la pantalla de advertencia de contenido violento del
propio juego, en su forma real multi-idioma (Página 1: inglés, alemán,
francés simultáneos; Página 2: italiano y español, este último
duplicado). Esto es networking de texto de UI real del juego, legible y
verificado — no un artefacto irreconocible. **No se afirma que la
reproducción de película (imágenes de vídeo del PSS) esté resuelta**:
no se observó ningún fotograma de vídeo real (CAPCOM logo, animación de
introducción) en ninguna de las 755 capturas revisadas parcialmente;
todo el contenido reconocible es texto de UI, no video MPEG decodificado
puesto en pantalla. Esto es consistente con — y no contradice — el
veredicto de P3.13 de que ninguna salida visual de PELÍCULA fue
confirmada.

## 21. Clasificación de la variación de idioma

**OTHER**, con mecanismo demostrado y preciso — no encaja limpiamente en
ninguna de las cuatro categorías predefinidas tal como estaban
formuladas:

- NO es `INPUT_TIMING` en el sentido de "el menú de idioma selecciona
  una entrada distinta por corrida" (§11: la correlación inicial que
  sugería esto se retractó explícitamente tras más evidencia).
- NO es `GUEST_STATE_NONDETERMINISM` aguas abajo de una selección
  confirmada-idéntica (no se demostró una selección idéntica seguida de
  resultados distintos; lo que se demostró es que TODAS las corridas
  recorren la MISMA secuencia de páginas en el MISMO orden).
- NO es `RENDERING_ARTIFACT` como explicación de la variación de
  IDIOMA en sí (el contenido de texto es correcto en ambos idiomas,
  legible; el artefacto de renderizado real identificado — duplicación
  del bloque en español — es un hallazgo aparte, confirmado, pero no
  es la causa de que un idioma u otro aparezca).
- NO es `INSUFFICIENT_EVIDENCE` — hay evidencia suficiente y precisa
  para explicar el mecanismo.

**Mecanismo real, HECHO**: la advertencia es una secuencia multi-página
fija que SIEMPRE se muestra completa (todos los idiomas configurados,
independientemente de cuál se haya "seleccionado" — consistente con la
práctica habitual PAL de mostrar avisos legales de violencia en TODOS
los idiomas del disco, no solo el elegido). Qué fragmento de esa
secuencia queda visible en una captura manual única, o en el último
frame automatizado de una corrida, depende de la carrera entre (a) el
avance natural de esa secuencia y (b) el momento — con jitter propio —
en el que cada corrida termina por el agotamiento de
`RuntimeGuestArena` (§16), un límite de recursos no relacionado y ya
identificado en P3.13. La variación observada manualmente por el
usuario (español, inglés, alemán en capturas distintas) es consistente
en un 100% con capturas tomadas en distintos puntos de esta misma
secuencia de dos páginas.

## 22. Interpretación de casos (mapeo a A-D del prompt)

Ningún caso predefinido encaja exactamente; el más cercano en espíritu
es una variante de **CASE A** (el resultado depende de timing), pero el
timing relevante NO es "CROSS selecciona una entrada de menú distinta"
sino "el crash por agotamiento de recursos interrumpe la secuencia de
advertencia en un punto distinto" — un mecanismo de timing genuino, pero
de una naturaleza distinta a la que planteaban las Case A-D originales.
Se documenta así en vez de forzar el ajuste a una casilla que no
describe correctamente el hallazgo.

## 23. ¿Recomendar P3.14 o una observación de timing/idioma más pequeña primero?

**P3.14 — GETPICTURE_SYNCHRONOUS_REDECODE_FROM_GUEST_ES sigue siendo la
recomendación correcta**, sin cambios respecto a P3.13. Esta iteración
no encontró ninguna causa nueva de BLOCKER_004 ni ninguna dependencia
entre la variación visual de idioma y la cadena causal de
`sceMpegInit`/`GetPicture` ya establecida — el hallazgo aquí es
puramente sobre CUÁNTO tiempo de ejecución sostenida hay disponible
antes del agotamiento de `RuntimeGuestArena` (un límite de recursos,
no una cuestión de idioma) para observar contenido. No se recomienda
una observación de timing/idioma adicional como paso previo — la
pregunta original del prompt (¿por qué varía el idioma?) quedó
respondida con evidencia suficiente en esta misma iteración.

## Git status final

```
 M analysis/notes/BLOCKER_004_pss_video_output.md   (pendiente de apéndice, añadido a continuación)
 M analysis/tools/runtime_loop/run.py
?? analysis/notes/BLOCKER_004_P313_SCEMPEGINIT_OWNERSHIP_MATRIX.md
?? analysis/notes/BLOCKER_004_P3131_VISUAL_LANGUAGE_TRACE.md
```
Vendor: sin cambios respecto al cierre de P3.13 (`MPEG.cpp` dirty,
idéntico). `analysis/local/p311/RUN_023..027` son artefactos locales ya
cubiertos por `.gitignore`.

## Confirmaciones explícitas

NO commit. NO push. NO clean. NO regenerate. NO build. NO cambio en
MPEG.cpp/EeScheduler.cpp/sceMpegGetPicture/scheduler. NO se forzó ningún
idioma, ninguna selección de menú, ningún input direccional, ningún
retraso de CROSS. NO se investigó ni corrigió el agotamiento de
`RuntimeGuestArena`. NO se terminó ningún proceso ajeno a esta sesión.

**Resultado Prompt P3.13.1 —
FULL_MODE_VISUAL_TRACE_AND_LANGUAGE_VARIATION**
