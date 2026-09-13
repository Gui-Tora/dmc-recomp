# P4.1.1 — MOVIE_FRAME_COMPLETENESS_BOUNDARY_TRACE

Localización del defecto temprano de medio-cuadro/parpadeo (pre-picture-43,
decoder limpio) identificado y dejado sin resolver por P4.1F. El fix P4.1
(`disp[0].dispfb`) **no está en reconsideración** — permanece congelado como
`P41_FIX_CAUSAL_AND_VALIDATED`.

**Resultado de este prompt, resumido por adelantado**: la Fase 1 (línea base
offline, sin tocar código) confirma el fenómeno con evidencia sólida y
reproducible 3/3. Las Fases 2-9 (instrumentación dinámica) quedaron
**bloqueadas** por un hallazgo nuevo e inesperado, no relacionado con la
causa raíz original: bajo las condiciones actuales del sistema, el camino
rápido síncrono de P3.14.2 (`sceMpegGetPicture` re-sincronizando desde
`viBuf` sin suspenderse) dejó de dispararse de forma reproducible —
incluyendo en corridas de control con el vendor **completamente revertido**
al commit `16ef7ab8...` ya validado por P4.1, sin ningún cambio de código.
Esto demuestra que el bloqueo **no lo causó la instrumentación de este
prompt**; es una fragilidad de temporización preexistente del propio
mecanismo P3.14.2, expuesta por casualidad durante esta sesión. Se
documenta como hallazgo principal y se recomienda como el siguiente paso
obligatorio antes de poder continuar P4.1.1.

## Fase 1 — Línea base offline desde RUN_044-046 (sin modificar código)

HECHO, evidencia reutilizada de corridas ya existentes y validadas por P4.1
(mismo hash de ejecutable `27faab90...` en las tres, `DMC_P313_INIT_MODE=ownership`):

- **RUN_044**: `visual_073687ms.png` = 5 párrafos completos, tenues.
  `visual_074218ms.png` = **solo 3 de 5 párrafos** (English/German/French),
  corte horizontal limpio a ~58% de la altura, mitad inferior negro sólido.
  `visual_075296ms.png` = 5 párrafos completos, brillantes.
- **RUN_045** (`visual_073889ms.png`) y **RUN_046** (`visual_073828ms.png`):
  mismo patrón de 3 párrafos / corte limpio, en timestamps ligeramente
  distintos (±300-400ms, consistente con muestreo no phase-locked al vsync
  guest — respuesta a la pregunta 16/17 de P4.1F: sigue `UNKNOWN` el
  phase-lock exacto, pero el fenómeno en sí es **reproducible 3/3**).
- Ventana temporal: ~73.7-75.3s, inmediatamente después de `MPEG_INIT`
  (~72.3s en las tres corridas) y muy anterior a la zona de corrupción de
  picture ≥43 (log de RUN_044: 187 llamadas `CALL sceMpegGetPicture`
  totales en ~21s ⇒ ~9 pictures/seg ⇒ picture 43 estimada ~77.1s). El
  fenómeno teporal cae dentro de la zona con **cero mensajes de warning del
  decoder** (confirmado por P4.1F Auditoría C/D: primer warning siempre en
  picture 43, 3/3).

**Primer frame BUENO temprano**: ~73.7-73.9s (según corrida).
**Primer frame MEDIO-NEGRO**: ~73.8-74.2s.
**Siguiente frame BUENO**: ~75.3s.
**Índice de picture exacto**: no determinado con los logs existentes (la
única correlación disponible, `getPicture success #N`, está muestreada solo
en N=1..10,50,100,150 — el rango ~17-40 estimado por P4.1F es una
aproximación por tasa de pictures/seg, no una medición directa). **Esto es
justo lo que las Fases 2-9 debían cerrar — ver más abajo por qué no fue
posible en este prompt.**
**Paridad de buffer activo**: no derivable de los logs existentes (requiere
instrumentación dinámica). **Falta explícitamente reconocida.**

## Fases 2-4, 7-9 — Instrumentación dinámica: BLOQUEADA (hallazgo principal)

### Qué se implementó

Un único flag opt-in `DMC_P411_FRAME_BOUNDARY_TRACE=1`, con firmas
compactas de 7 bloques de 64 filas (hash FNV-1a de R/G/B + conteo de
píxeles no-negros por bloque — partición elegida porque 448/7=64 exacto,
sin resto, cubriendo el alto completo sin bloques parciales) en:

- **`MPEG.cpp`** (boundary A/B): justo antes y después de
  `writeDecodedFrameToGuest(rdram, imageAddr, frame)` dentro de
  `sceMpegGetPicture`, gateado por `if (haveFrame)` — A = frame FFmpeg
  decodificado (host, row-major); B = lectura del buffer guest
  `0x79D680` reconstruida con el mismo layout de franjas verticales de 16
  columnas que `writeDecodedFrameToGuest` escribe y que P3.15.2 ya probó
  que la cadena GIF real de la película lee byte-exacto.
- **`gs_cpu_backend.cpp`** (boundary C/D/E, dentro de `copySource`, y F
  tras `applyFieldPresentation` en `PresentFromLocalMemory`): C/D = lectura
  cruda de VRAM en el `displayFrame` (DISPFB2) activo; E = contenido final
  seleccionado (posiblemente sustituido por `preferredSource`); F =
  contenido tras el procesamiento de campo.
- **`gs_frontend.cpp`**: trazas `PREF-SET`/`PREF-CLEAR` en
  `updatePreferredDisplaySourceForDraw` (mecanismo `preferredSource`, ver
  Fase 6 más abajo).

Build quirúrgico verificado en cada iteración (`grep -c "CL.exe"` = 1,
exactamente los archivos tocados; relink `_BuildLinkAction` con 0
`CL.exe`).

### Qué pasó al ejecutar (cronología completa, honesta)

1. **RUN_047** (`--post-target 3`): 0 eventos A/B — la ventana terminó
   antes de que `sceMpegGetPicture` tuviera éxito ni una sola vez. Ventana
   demasiado corta.
2. **RUN_048** (`--post-target 25`): mismo resultado — 0 éxitos de
   `GetPicture` en toda la corrida (90s). Traza del log: tras el **segundo**
   `sceMpegInit` (el que la propia arquitectura `ownership` de P3.13
   dispara y que resetea `framesAfter=0/decoderAfter=0` — evento normal,
   presente idéntico en el baseline RUN_044 línea 378-379), la primera
   `sceMpegGetPicture` cae en `[GP:H] ... SUSPENDING, no frame available`
   y **nunca se resume** en los ~18s restantes observados.
3. Sospeché sobreactividad de la instrumentación (lectura extra de VRAM
   incondicional en `copySource`, llamada en cada `Present()` ~10 Hz desde
   arranque). La eliminé (solo se lee dos veces cuando `usedPreferred`
   realmente sustituye) → **RUN_049: mismo bloqueo exacto.**
4. Sospeché el patrón de "storm de flush" (`std::cerr` tiene `unitbuf`, por
   lo que cada `<<` encadenado fuerza un flush — hasta ~9 flushes por
   línea de log). Reescribí las 3 funciones de log para construir la línea
   completa en un buffer y emitirla con una sola escritura → **RUN_052:
   mismo bloqueo exacto** (después de dos intentos con `HARNESS_ERROR` de
   captura de pantalla, ajenos a esto — ver nota abajo).
5. Aislé revirtiendo `gs_frontend.cpp` por completo (dejando solo
   `MPEG.cpp` + `gs_cpu_backend.cpp` instrumentados) → **RUN_053: mismo
   bloqueo exacto.**
6. Aislé más revirtiendo también `gs_cpu_backend.cpp` (dejando **solo**
   `MPEG.cpp`, cuyo código añadido está dentro de `if (haveFrame)` — una
   rama que, por definición del propio bloqueo, **nunca se ejecuta**
   mientras el problema está activo) → **RUN_054: mismo bloqueo exacto**,
   pese a que el código instrumentado no puede haberse ejecutado ni una
   vez durante el fallo.
7. **Revertí el vendor por completo** (`git checkout --` los 3 archivos;
   verificado `git -C vendor/PS2Recomp diff --stat` vacío, `HEAD` en
   `16ef7ab8dc961c44921c3776705da6bc3b4806fb`, exactamente el commit ya
   validado por P4.1). Reconstruí y re-linkeé desde esa fuente limpia
   (0 `CL.exe` inesperados en cada paso) → **RUN_056 y RUN_058 (corridas
   de control, cero instrumentación): el MISMO bloqueo exacto.** Traza
   comparada línea a línea contra el baseline original:

   | | Baseline (RUN_044, validado por P4.1) | Control post-reversión (RUN_056/058, HOY) |
   |---|---|---|
   | tras "about to wipe" | `[GP:A]` → `[P314:GP] pre-sync` → `[P3142:ring]` → `[P314:GP] post-sync` → `[GP:B] have-frame` → `[GP:D] presentation due now` → **éxito** (13 líneas, mismo tick) | `[GP:A]` → **`[GP:H] SUSPENDING, no frame available`** — el camino síncrono P3.14.2 (`pre-sync`/`ring`/`post-sync`) **no se ejecuta en absoluto**, cae directo a la espera asíncrona, que no se resuelve en los ~15-18s observados |

### Interpretación (HECHO, no INFERENCIA — confirmado por control con fuente idéntica)

El bloqueo **no lo causó la instrumentación de P4.1.1**. Es una
fragilidad de temporización preexistente del propio mecanismo síncrono de
re-decodificación P3.14.2 (`sceMpegGetPicture`'s `pre-sync`/`ring`/
`post-sync`), que en las condiciones actuales del sistema (esta sesión
lleva ~50 minutos de compilaciones MSBuild pesadas y consecutivas en
segundo plano) deja de dispararse de forma reproducible, cayendo al
camino asíncrono (`waitExternal`/`GP:H`) que — dentro de la ventana
observada — no se resuelve. Esto es coherente con, y extiende, la
fragilidad de temporización ya documentada históricamente para esta
misma región del código (P3.11.1 encontró una inversión de orden de
lotes del scheduler; P3.14.1 encontró un deadlock determinista en una
versión anterior del propio diseño síncrono). **INFERENCIA** (no
verificada más a fondo en este prompt, por presupuesto): la causa
inmediata más probable es carga/contención de CPU del host acumulada por
las compilaciones repetidas de esta sesión, no un cambio de
comportamiento del propio binario — pero esto queda como `UNKNOWN`
explícito, no como hecho cerrado.

**Consecuencia práctica**: ninguna de las Fases 2, 3, 4, 7, 8 o 9 pudo
producir datos dinámicos correlacionados con picture — no porque el
mecanismo bajo prueba no exista, sino porque el prerequisito (que
`GetPicture` alcance el buffer de imagen guest dentro de la ventana
observable) dejó de cumplirse de forma reproducible bajo las condiciones
actuales, independientemente de qué código esté compilado.

## Fase 5 — Coreografía clear/upload/flip sobre 0x140000: hallazgo estático (no dinámico)

No trazada dinámicamente (bloqueada, ver arriba). Pero la lectura de
código (necesaria para instrumentar, hecha de todas formas) localizó el
mecanismo concreto más probable, ya insinuado por P4.1F Candidato #1/#2 y
por el propio lenguaje del prompt ("preferredSource invalid"):

`GS::updatePreferredDisplaySourceForDraw` (`gs_frontend.cpp`, sin cambios
respecto al código ya vigente en el commit `16ef7ab8...`) mantiene un
mecanismo de "fuente de presentación preferida": cuando un draw de sprite
de **pantalla completa** (`x0<=0, y0<=0, xEnd>=639, yEnd>=447`) con
blending específico (`tme && abe && fst && ctxt`, `alphaMode==0x64`,
`alphaFix∈{0x60,0x80}`) escribe a un `frame.fbp` **distinto** de su propia
textura fuente (`tex0.tbp0`), el runtime memoriza `(destFbp, srcTbp0)` y,
en la siguiente presentación cuyo `displayFrame.fbp` coincida con ese
`destFbp`, **sustituye por completo** el contenido presentado por una
lectura de `srcTbp0` en vez de leer el VRAM real de `destFbp`
(`gs_cpu_backend.cpp`, `copySource`, rama `allowPreferred && hasPreferredSource
&& preferredDestFbp==displayFrame.fbp`). Esta sustitución es
**incondicional** (no depende de `countNonBlackPixels`) y se aplica a
**cualquiera** de los dos FBP de película (`0` o `0xA0`), no solo a
`FBP==0`.

**Candidato principal no descartado para el medio-cuadro temprano**: si un
draw de fade/composición de pantalla completa (candidato ya señalado por
P4.1F como "sprite de fade negro") apunta a uno de los dos FBP de
película mientras su propia textura fuente (`srcTbp0`) solo tiene
contenido válido parcial en el momento del muestreo (p. ej. si esa
textura está siendo llenada progresivamente por otra operación), la
sustitución produciría exactamente un corte geométrico estable
(determinado por el layout de esa textura, no por una carrera aleatoria)
— consistente con el corte horizontal limpio y reproducible observado.
**No confirmado dinámicamente en este prompt** por el bloqueo de la Fase
2-9.

## Fase 6 — Fallback `FBP==0`: descartado por análisis estático (geometría, no requiere corrida)

`gs_cpu_backend.cpp`, `copySource` (código vigente, sin cambios):

```cpp
if (!usedPreferred && displayFrame.fbp == 0u && countNonBlackPixels(pixels, width, height) == 0u)
{
    for (const GSFrameReg &candidate : request.contextFrames) { /* sustituye TODO el frame */ }
}
```

Este mecanismo **solo** se activa cuando `countNonBlackPixels(...)==0` —
es decir, el frame leído de `FBP==0` debe estar **100% negro**, sin
excepción. El defecto observado tiene contenido real (texto/imagen) en la
mitad SUPERIOR del cuadro — por definición, `countNonBlackPixels` sería
`>0` en esos frames, por lo que esta rama **no puede ejecutarse** cuando
el defecto está presente. **Descartado como mecanismo directo del
medio-cuadro parcial** (HECHO, por análisis estático de la condición de
guarda, no requiere instrumentación dinámica). Se mantiene como riesgo
real pero **distinto**: solo podría producir una sustitución de frame
COMPLETO (no parcial) en el raro caso de un frame de película
enteramente negro (p. ej. durante un fundido a negro real) — no está
relacionado con el fenómeno de esta investigación.

## Fases 10, 12 — Separación de la corrupción tardía de MPEG (mantenida)

No se mezcló evidencia de picture ≥43 en ningún punto de este análisis
(Fase 1 usa exclusivamente la ventana ~73.7-75.3s, picture estimada
~17-40, zona con cero warnings del decoder). La caracterización de P4.1F
sobre `ac-tex damaged`/`MVs not available` como corrupción determinista de
feed (no ruido inofensivo) se **preserva sin alterar**; no se tocó código
de producción de P3.14 en este prompt.

## Fase 11 — Corridas

7 intentos de instrumentación (RUN_047-054, con 2 `HARNESS_ERROR` de
captura de pantalla ajenos, ver nota) más 4 corridas de control
post-reversión (RUN_055-058, 2 `HARNESS_ERROR` de captura ajenos). El
resultado (bloqueo reproducible, causa localizada como ajena a la
instrumentación) es **inequívoco**, no ambiguo — no se justifican más
repeticiones de la misma prueba; el paso que falta es una investigación
dedicada de la fragilidad P3.14.2 en sí misma, no más repeticiones de
P4.1.1.

*Nota sobre `HARNESS_ERROR`*: 4 de las 12 corridas totales de este prompt
fallaron con `OSError('screen grab failed')` a los ~10s (captura de
pantalla del harness, `query session` confirmó la sesión de consola
activa, no bloqueada) — un fallo intermitente del mecanismo de captura del
host, no relacionado con el código bajo prueba; se manejó reintentando.

## Fase 13 — Clasificación

`INSUFFICIENT_EVIDENCE` para el mecanismo causal exacto del medio-cuadro
temprano — no por falta de candidatos (Fase 5 identifica uno concreto,
plausible y no descartado: la sustitución `preferredSource`), sino porque
la confirmación dinámica quedó bloqueada por un hallazgo distinto y más
urgente (fragilidad de temporización de P3.14.2, reproducible incluso con
fuente 100% revertida). El fix P4.1 (`disp[0].dispfb`) permanece
`P41_FIX_CAUSAL_AND_VALIDATED`, sin cambios.

## Diagnósticos temporales: revertidos

`git -C vendor/PS2Recomp diff --stat` vacío; `HEAD` verificado en
`16ef7ab8dc961c44921c3776705da6bc3b4806fb` (idéntico al `patched_commit`
de `upstream.lock.json`, sin nuevo patch). Ejecutable reconstruido desde
esa fuente limpia (hash de build no bit-reproducible entre invocaciones
de MSBuild Debug distintas — `PDB`/timestamps embebidos varían aunque el
código fuente sea idéntico; verificado que esto es evidencia de
no-determinismo de build, no de una diferencia de código, comparando
`git diff --stat` vacío). Ningún proceso `dmc-recomp.exe` en ejecución al
cierre. `analysis/local/p411/ps2_runtime.pre_p411.lib` conservado como
backup del `.lib` pre-instrumentación (no necesario para restaurar, ya
que la restauración se hizo recompilando desde fuente revertida, pero se
deja documentado).

## Correction ledger

- Ninguna conclusión de P4.1 o P4.1F fue retractada en este prompt. El fix
  `disp[0].dispfb` sigue siendo correcto y validado.
- **Añadido** (no en P4.1F): el mecanismo `preferredSource`/
  `updatePreferredDisplaySourceForDraw` fue identificado por lectura de
  código como el candidato causal más concreto y plausible para el
  medio-cuadro temprano — más específico que la caracterización genérica
  de P4.1F ("sprite de fade" / "preferredSource invalid").
- **Añadido**: el fallback `FBP==0` (`countNonBlackPixels==0`) queda
  formalmente descartado como mecanismo del defecto PARCIAL observado
  (solo puede sustituir el frame COMPLETO, y solo si es 100% negro).
- **Nuevo hallazgo, no anticipado por ningún prompt anterior**: el camino
  síncrono de re-decodificación P3.14.2 no se dispara de forma
  reproducible bajo las condiciones actuales del sistema — confirmado con
  fuente 100% revertida al commit validado por P4.1. Esto es un hallazgo
  independiente del alcance original de P4.1.1 y requiere su propia
  investigación antes de que cualquier instrumentación dinámica futura
  del pipeline de presentación sea viable.

## Tabla de capas requerida

| Boundary | Bueno (~73.7-75.3s) | Malo (~73.8-74.2s) | ¿Igual/completo? | Evidencia |
|---|---|---|---|---|
| FFmpeg host frame (A) | No medido dinámicamente | No medido dinámicamente | UNKNOWN | Bloqueado (Fase 2-9) |
| guest 0x79D680 (B) | No medido dinámicamente | No medido dinámicamente | UNKNOWN | Bloqueado |
| GS VRAM tras última franja (C/D) | No medido dinámicamente | No medido dinámicamente | UNKNOWN | Bloqueado |
| buffer DISPFB2 seleccionado (E) | No medido dinámicamente | No medido dinámicamente | UNKNOWN | Bloqueado; candidato estático: sustitución `preferredSource` (Fase 5) |
| pre-field host RGBA | No medido dinámicamente | No medido dinámicamente | UNKNOWN | Bloqueado |
| post-field RGBA (F) | No medido dinámicamente | No medido dinámicamente | UNKNOWN | Bloqueado |
| presentación/captura final | 5 párrafos, corte ausente (screenshots RUN_044/045/046) | 3 párrafos, corte horizontal limpio ~58% (screenshots RUN_044/045/046) | **DIFERENTE, confirmado visualmente 3/3** | Fase 1, HECHO |

## Preguntas requeridas

1. ¿El frame malo ya está incompleto en la salida de FFmpeg? **UNKNOWN** (bloqueado).
2. ¿`writeDecodedFrameToGuest` preserva las 448 filas? **UNKNOWN dinámicamente**; estáticamente el bucle siempre escribe `outHeight` filas completas por franja, sin lógica de truncado condicional visible en el código — no descartado pero sin mecanismo de truncado identificado.
3. ¿El guest `0x79D680` contiene el frame completo? **UNKNOWN** (bloqueado).
4. ¿GS VRAM contiene las 448 filas tras la franja 32? **UNKNOWN dinámicamente**; estáticamente TRXREG es fijo (16×448) por franja en las 32 franjas (ya establecido por P4.0F/P3.15.2), por lo que una franja incompleta no puede producir un corte horizontal — mecanismo de truncado vertical, no horizontal.
5. ¿Una operación de clear/fade modifica el buffer de película tras la subida? **UNKNOWN dinámicamente**; candidato estático concreto identificado (Fase 5), no confirmado.
6. ¿Se trata `FBP==0` de forma especial en la selección de presentación? **SÍ** — pero solo como sustitución de frame COMPLETO condicionada a 100% negro (Fase 6), geométricamente incompatible con el defecto parcial observado.
7. ¿La selección de fuente alguna vez reemplaza `FBP==0` por otra superficie? **SÍ**, dos mecanismos distintos: (a) el fallback de negro total de la Fase 6 (descartado para este defecto); (b) el mecanismo `preferredSource` de la Fase 5 (no descartado, candidato principal).
8. ¿`CopyFrameToHostRgba` preserva todas las filas? Estáticamente sí — itera `y` en `[0,height)` sin condición de corte temprano; no hay mecanismo de truncado visible.
9. ¿El procesamiento de campo preserva todas las filas? Estáticamente sí — `applyFieldPresentation` mapea cada `y` de salida a un `sourceY` siempre dentro de `[0,height)` (clamp explícito), nunca escribe negro; confirma el análisis estático ya hecho por P4.1F (Auditoría F), no revisado dinámicamente en este prompt.
10. ¿La paridad de FIELD correlaciona con el patrón de medio-cuadro? **UNKNOWN** (bloqueado; además, en las muestras C/D/E/F que sí se lograron capturar antes de aislar la causa —RUN_047/048, fase pre-película— `fieldMode` fue `0` en el 100% de las observaciones, sugiriendo que el modo entrelazado podría no estar activo en absoluto durante esta fase, pero esto no está confirmado para el momento específico de la reproducción de película).
11. ¿La paridad de buffer correlaciona con el patrón? **UNKNOWN** (bloqueado).
12. ¿El defecto está antes o después de la presentación GS? **UNKNOWN dinámicamente**; el candidato estático de la Fase 5 lo ubicaría en la propia selección de fuente de presentación (dentro de `copySource`), no en el pipeline de campo/captura posterior (ambos descartados por mecanismo estático, preguntas 8-9).
13. ¿El parpadeo temprano es independiente de la corrupción tardía de picture 43? **SÍ, HECHO** — confirmado en Fase 1: el parpadeo ocurre en la ventana ~73.7-75.3s, dentro de la zona con cero mensajes de warning del decoder (picture <43 estimada), mientras que P4.1F ya estableció que la corrupción determinista comienza siempre exactamente en picture 43.
14. ¿Primer nodo exacto donde BUENO y MALO difieren? **UNKNOWN a nivel de boundary interno** (bloqueado); a nivel de presentación final, confirmado por captura de pantalla (Fase 1).
15. ¿Hay un fix de producción listo tras este prompt? **NO.**

---

Resultado Prompt P4.1.1 — MOVIE_FRAME_COMPLETENESS_BOUNDARY_TRACE

PRIMARY_CLASSIFICATION:
INSUFFICIENT_EVIDENCE

GOOD_FRAME_REFERENCE:
RUN_044 visual_073687ms.png y visual_075296ms.png (~73.7s y ~75.3s; picture ~17-40 estimado, no confirmado)

BAD_FRAME_REFERENCE:
RUN_044 visual_074218ms.png (~74.2s; picture ~17-40 estimado, no confirmado); reproducido en RUN_045 (~73.9s) y RUN_046 (~73.8s)

LAST_PROVEN_COMMON_NODE:
Presentación final capturada (screenshot) — el único boundary con evidencia dinámica real (visual, 3/3); todos los boundaries internos (A-F) quedaron sin instrumentar por el bloqueo de Fase 2-9

FIRST_PROVEN_DIVERGENT_NODE:
UNKNOWN — no se pudo establecer dinámicamente; candidato estático no confirmado: sustitución `preferredSource` dentro de `copySource` (gs_cpu_backend.cpp)

FIELD_CAUSAL:
NO (análisis estático: `applyFieldPresentation` no puede producir filas negras, clamp explícito a `[0,height)`)

FBP0_FALLBACK_CAUSAL:
NO (análisis estático: la rama `countNonBlackPixels==0` exige negro total, incompatible con un defecto parcial)

EARLY_FLICKER_INDEPENDENT_OF_LATE_MPEG_CORRUPTION:
YES

IMPLEMENTATION_READY:
NO

NEXT:
Un prompt dedicado y aislado (no otro intento de P4.1.1) a diagnosticar por qué el camino síncrono de re-decodificación de P3.14.2 (`pre-sync`/`ring`/`post-sync`) dejó de dispararse de forma reproducible bajo las condiciones actuales del sistema — confirmado con el vendor 100% revertido al commit ya validado por P4.1 (`16ef7ab8...`), por lo que el problema es de temporización/entorno, no de código. Sin resolver esto, ninguna instrumentación dinámica futura de la cadena de presentación es viable. Una vez resuelto, repetir P4.1.1 (Fases 2-9) contra el candidato ya identificado en la Fase 5 (`preferredSource`).

CHECKPOINT_DECISION:
NO_COMMIT
