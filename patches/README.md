# Cambios generales de runtime

Guardar aquí parches revisables contra el commit de upstream.lock.json si
un bloqueo demuestra un fallo general. Cada parche debe documentar causa,
reproducción, validación y commit base.

## Convención de archivos

- `NOMBRE.patch`: diff puro, aplicable directamente con `git apply`
  (sin comentarios ni encabezados humanos — un comentario `#` antes del
  primer `diff --git` rompe `git apply`).
- Cada patch debe tener documentación asociada (causa, reproducción,
  fix, validación), en una de estas dos formas:
  - **A) companion `NOMBRE.md`** junto al patch, en este mismo
    directorio (patrón original, usado por `BLOCKER_002_cdmodule_service`); o
  - **B) una nota técnica ya existente bajo `analysis/notes/`**,
    referenciada explícitamente desde la tabla de "Patches activos" más
    abajo — usado cuando el patch agrupa trabajo ya cubierto en detalle
    por informes de investigación existentes, para no duplicar
    documentación extensa.

No crear un nuevo `.md` de patch que solo repita/resuma un informe de
`analysis/notes/` ya existente — enlazar directamente ese informe.

## Mecanismo de aplicación (`scripts/pipeline.py bootstrap`)

`upstream.lock.json` declara `patches[]` (lista ordenada de
`{path, sha256}`, rutas relativas a la raíz del proyecto),
`patch_identity` (autor/fecha/mensaje fijos) y `patched_commit` (el
commit sintético esperado). `bootstrap()`, solo cuando clona
`vendor/PS2Recomp` desde cero:

1. fija `core.autocrlf=false` en el clon antes de cualquier checkout
   (necesario para que el resultado sea idéntico bit a bit
   independientemente de la configuración global de git del host);
2. hace checkout del commit baseline real (`commit`, recuperable del
   remoto);
3. verifica SHA256 de **todos** los patches antes de tocar el árbol;
4. aplica cada patch en orden (`git apply --check` + `git apply`; ante
   cualquier fallo, revierte al baseline limpio con `git reset --hard`
   y aborta explícitamente — nunca deja baseline + un subconjunto de
   patches aplicado);
5. genera un commit determinista vía `git commit-tree` (autor,
   committer, fecha y mensaje fijos en `patch_identity`; sin rama, sin
   hooks, sin depender de `user.name`/`commit.gpgsign` del host) y
   verifica que su hash coincide exactamente con `patched_commit`;
6. deja `HEAD` detached en `patched_commit` (mismo modelo que el
   checkout de la baseline pura: sin rama local).

`upstream()` (invocado antes de `build`/`run`) verifica en O(1) que
`HEAD == patched_commit` (o `== commit` si `patches[]` está vacío) y que
el working tree está limpio — sigue detectando cualquier drift
accidental exactamente igual que antes, solo que el commit "reconocido"
ahora es baseline+patchset en vez de baseline puro. `commit` (el
baseline real) nunca se sustituye por un commit sintético: ambas
identidades quedan separadas en el lock.

Detalle completo del algoritmo, verificación de determinismo (CRLF,
reproducibilidad, dos clones independientes desde GitHub) y migración:
`analysis/notes/BLOCKER_002_indirect_jump_top_of_ram.md`, FASE K.

## Patches activos

Orden de aplicación = orden declarado en `upstream.lock.json: patches[]`
(cada uno se aplica sobre el resultado del anterior; ver mecanismo
arriba). Los cuatro patches actualmente activos:

1. `BLOCKER_002_cdmodule_service.patch` (convención A — companion
   local): servicio HLE mínimo de lectura de CD para Devil May Cry
   (`SLES_503.58`). Documentación: `BLOCKER_002_cdmodule_service.md`
   (este directorio), causa/validación completas también en
   `analysis/notes/BLOCKER_002_indirect_jump_top_of_ram.md`.

2. `BLOCKER_003_cdmodule_movie_streaming.patch` (convención B): HLE de
   las RPCs de CDMODULE.IRX para arranque/transferencia por
   bloques/cierre/estado de reproducción de películas (PSS),
   `fno=9/0xA/0xC/0xD`. Documentación:
   `analysis/notes/BLOCKER_003_cdmodule_movie_streaming.md`.

3. `BUILD_ffmpeg_private_link_scope.patch` (convención B, patch de
   build sin blocker asociado): cambia el link scope de FFmpeg en
   `ps2xRuntime` de `PUBLIC` a `PRIVATE` en `CMakeLists.txt`, requerido
   para que el build reproducible con FFmpeg activo (`PS2X_HAS_FFMPEG`)
   no propague símbolos/includes de FFmpeg a consumidores que no los
   necesitan. Diff de 13 líneas, autoexplicativo; sin nota dedicada.

4. `BLOCKER_004_runtime_p37_p312.patch` (convención B): agrupa el
   trabajo de runtime acumulado y validado de P3.7 a P3.12 para
   BLOCKER_004 — separación y endurecimiento del `RuntimeGuestArena`/
   heap guest, asignación de pila de callbacks asíncronos, soporte
   MPEG/audio ya requerido por el pipeline, y el fix de orden de lotes
   de invocaciones pendientes del scheduler (P3.12). Incluye
   instrumentación de diagnóstico marcada explícitamente como temporal
   (`temporary`, `Remove once BLOCKER_004 is closed`) — deliberadamente
   sin retirar todavía: este patch representa el runtime exacto usado
   para las validaciones P3.11/P3.12; su limpieza será una fase
   separada. Documentación (no duplicada aquí, ver cada informe):
   - `analysis/notes/BLOCKER_004_pss_video_output.md` — historia
     canónica y acumulativa de BLOCKER_004.
   - `analysis/notes/BLOCKER_004_P36_HEAP_COLLISION_AUDIT.md` — auditoría
     de colisión de heap en runtime (P3.6).
   - `analysis/notes/BLOCKER_004_ASTRA_P311_OVERNIGHT.md` — contrato de
     `sceMpegInit`, loop de reproducción autónomo (P3.11).
   - `analysis/notes/BLOCKER_004_FABLE_P3111_CAUSAL_AUDIT.md` —
     auditoría causal adversarial que localizó la inversión de orden de
     lotes del scheduler (P3.11.1).
   - `analysis/notes/BLOCKER_004_P312_SCHEDULER_BATCH_ORDER.md` — diseño,
     implementación y validación byte-exacta del fix de orden de
     invocaciones pendientes (P3.12).

5. `BLOCKER_004_p313_scempeginit_experiment.patch` (convención B).
   **EXPERIMENTAL / NOT A PRODUCTION FIX.** Añade una matriz de 4 modos
   opt-in (`DMC_P313_INIT_MODE=baseline|ownership|queued|full`) sobre
   `sceMpegInit`, usada exclusivamente para determinar experimentalmente
   qué continuidad de estado host hace falta para cruzar el primer
   `sceMpegGetPicture` — no implementa ningún diseño de producción.
   `baseline` (el valor por defecto cuando la variable está ausente) es
   byte-a-byte idéntico al comportamiento validado en
   `BLOCKER_004_runtime_p37_p312.patch`. Se preserva reproduciblemente
   porque P3.14 construirá sobre este estado exacto ya validado
   dinámicamente (8 corridas `full`, 3/3 y 5/5 reproducibles).
   Documentación (no duplicada aquí):
   - `analysis/notes/BLOCKER_004_P313_SCEMPEGINIT_OWNERSHIP_MATRIX.md` —
     tabla de ownership completa, diseño de los 4 modos, resultados de
     las 8 corridas (P3.13).
   - `analysis/notes/BLOCKER_004_P3131_VISUAL_LANGUAGE_TRACE.md` —
     seguimiento visual observacional, primera salida de UI reconocible
     confirmada, resolución de la variación de idioma como secuencia
     PAL fija de 2 páginas (P3.13.1).

6. `BLOCKER_004_p314_sync_vibuf_redecode.patch` (convención B). Sobre
   el `sceMpegInit` de `ownership` (P3.13), reemplaza el diseño de
   "decoder preservado" por un decoder FFmpeg **fresco** post-init,
   alimentado sincrónicamente desde el `viBuf` guest superviviente,
   con consumo guest-visible espejado hacia `blockCursor`/
   `pendingBytes` (Modelo B — efecto final observable, no la mecánica
   real de dos fases, que depende de un registro DMA/IPU de hardware
   no emulado por esta HLE) y ownership único de decode post-init
   (corta el camino asíncrono mientras el síncrono está activo).
   Corrige el deadlock determinista de inanición del productor a ~520
   KiB que la auditoría P3.14.1 encontró en la primera versión de este
   diseño. Validado 3/3 con el mismo ejecutable: techo antiguo roto
   ~25× (≥250 éxitos sostenidos de `GetPicture`/corrida), ring cruza
   `capacity` dos veces de forma reproducible, y aparece por primera
   vez en toda la investigación de BLOCKER_004 imagen de
   película/PSS reconocible (animación de fuego). Checkpoint de tipo
   `FIX_CHECKPOINT_WITH_KNOWN_CAVEATS`, no cierre de BLOCKER_004 (ver
   caveats documentados en el informe P3.14.2). Documentación (no
   duplicada aquí):
   - `analysis/notes/BLOCKER_004_P314_SYNCHRONOUS_GUEST_ES_REDECODE.md` —
     diseño e implementación inicial, validación 4/4 dentro de la
     ventana pre-inanición (P3.14).
   - `analysis/notes/BLOCKER_004_P3141_FABLE_SYNC_MPEG_AUDIT.md` —
     auditoría adversarial independiente que encontró el deadlock
     determinista y el riesgo de doble ownership de decode (P3.14.1).
   - `analysis/notes/BLOCKER_004_P3142_VIBUF_CONSUMPTION_AND_OWNERSHIP.md` —
     diseño e implementación del Modelo B, ownership único de decode,
     validación 3/3 con imagen de película reconocible (P3.14.2).

7. `BLOCKER_004_p41_scegssetdefdbuffdc_dispfb0_zero.patch` (convención
   B). Cambio de una sola línea en `sceGsSetDefDBuffDc` (HLE del SDK,
   `GS.cpp`): `disp[0].dispfb` (`FBP`) pasa de usar `zbufAddr` (valor de
   dirección de Z-buffer, `0xE0`) a literal `0`, igual que `disp[1]`.
   Root cause aislado y cerrado en P4.0.1/P4.0.2 (divergencia de
   entorno de presentación de película guest vs. hardware original,
   remontada hasta esta asignación incorrecta de campo dentro del HLE);
   validado 3/3 en P4.1: entorno guest post-fix coincide exactamente
   con las mediciones en vivo de PCSX2 original (`DISPFB2` alterna
   `0x1000`/`0x10A0`), sin regresión en progresión MPEG (150 éxitos de
   `GetPicture` en las 3 corridas), y mejora visual reproducible y
   dramática (fotogramas completos y coherentes en vez de bandas/
   fragmentos). Checkpoint `FIX_CHECKPOINT_WITH_KNOWN_CAVEATS` —
   `AMOD` (bit6 de `PMODE`) permanece divergente (no tocado
   deliberadamente en este patch, ver informe) y la semántica PSM/
   formato de píxel de P3.15.2 sigue sin auditar; no es un cierre
   global de BLOCKER_004. Documentación (no duplicada aquí):
   - `analysis/notes/BLOCKER_004_P40_FABLE_GS_PRESENTATION_CAUSAL_BASELINE.md` —
     baseline causal independiente que primero ubicó la divergencia en
     el entorno de presentación GS (P4.0), con una teoría de EN1/
     circuito-1 posteriormente retractada.
   - `analysis/notes/BLOCKER_004_P401_MOVIE_DISPLAY_ENV_RECONCILIATION.md` —
     reconciliación con mediciones directas de PCSX2 original,
     retracción explícita de la teoría EN1, aislamiento del nodo de
     divergencia en la construcción del entorno guest antes de la
     escritura GS (P4.0.1).
   - `analysis/notes/BLOCKER_004_P402_SCEGSSETDEFDBUFFDC_ROOT_CAUSE.md` —
     cierre de causa raíz: cómputo exacto de `zbufAddr`, identificación
     del escritor real de `disp[1].dispfb=0xA0` en código guest, y
     prueba por simetría de que `disp[0].dispfb` debe inicializarse en
     `0` (P4.0.2).
   - `analysis/notes/BLOCKER_004_P41_SCEGSSETDEFDBUFFDC_DISPFB0_ZERO_FIX.md` —
     implementación del fix de una línea y validación causal A/B 3/3
     (P4.1).

8. `BLOCKER_004_p413_scegssetdefdbuffdc_drawenv_frame_zero.patch`
   (convención B). Cambio de una sola línea en `sceGsSetDefDBuffDc`
   (HLE del SDK, `GS.cpp`): el `FRAME FBP` de los draw-envs del segundo
   slot (`draw11`/`draw12`) pasa de usar `zbufAddr` (`0xE0`) a literal
   `0`, igual que `draw01`/`draw02` — la MISMA clase de bug que el
   patch 7 corrigió para el entorno de *display* (`dispfb0`), esta vez
   en el entorno de *draw*. Causa raíz aislada por auditoría adversarial
   independiente (P4.1.2, Fable): `FRAME FBP=0xE0` alias físicamente,
   vía direccionamiento real de páginas GS (`FBW=8`, fila y=256 → página
   `0xA0+8·8=0xE0`), las filas 256-447 del buffer de película/display
   `FBP=0xA0` — cada frame en que el guest muestra ese buffer, su propio
   sprite de fade/UI opaco negro se rasteriza sin querer sobre ese
   `FRAME` erróneo y borra la mitad inferior del buffer que se está
   mostrando, justo antes de la presentación. `ZBUF` no se ve afectado
   (`ZMSK=1`, sin escrituras Z reales). Retracta la interpretación previa
   de P4.1.1B ("el buffer 0xA0 nunca recibe la mitad inferior de la
   subida") — la subida SÍ es completa y simétrica; lo que ocurre
   después la destruye. Validado 3/3 (P4.1.3): 0 draws con
   `FRAME=0xE0` en toda la corrida (antes: 100% de las presentaciones de
   `FBP=0xA0` mostraban la mitad inferior en negro puro, 52/52 en
   P4.1.1B); post-fix, 0/N corridas muestran ese patrón; entorno guest
   confirmado (`draw11` slot1 `FRAME=0x000`, `draw01` slot0 `FRAME=0xA0`
   sin cambio, `ZBUF=0xE0`/`ZMSK=1` sin cambio en ambos); `DISPFB2` de
   P4.1 sigue alternando `0x000`/`0x0A0` sin regresión; camino síncrono
   P3.14.2 confirmado activo en las 3 corridas. Checkpoint
   `FIX_CHECKPOINT_WITH_KNOWN_CAVEATS` — la corrupción tardía
   determinista de MPEG (≥picture 43) y el flicker general de UI (mismo
   mecanismo estructural, mejora esperada pero sin comparación visual
   A/B dedicada) quedan como residuales separados; no es un cierre
   global de BLOCKER_004. Documentación (no duplicada aquí):
   - `analysis/notes/BLOCKER_004_P411_MOVIE_FRAME_COMPLETENESS_BOUNDARY_TRACE.md`,
     `BLOCKER_004_P411R_P314_BASELINE_REPRODUCIBILITY.md`,
     `BLOCKER_004_P411B_MOVIE_FRAME_COMPLETENESS_BOUNDARY_TRACE_RETRY.md` —
     cadena de localización dinámica del defecto (P4.1.1/R/B), incluido
     el hallazgo y resolución de un bloqueo de entorno de lanzamiento
     ajeno al propio bug.
   - `analysis/notes/BLOCKER_004_P412_FABLE_FBPA0_PARTIAL_UPLOAD_ROOT_CAUSE_AUDIT.md` —
     auditoría adversarial que identificó la causa raíz exacta (P4.1.2).
   - `analysis/notes/BLOCKER_004_P413_SCEGSSETDEFDBUFFDC_DRAWENV_FRAME_FIX.md` —
     implementación del fix de una línea y validación 3/3 (P4.1.3).

9. `BLOCKER_004_p4110_scegssetdefdbuffdc_conditional_tail.patch`
   (convención B). Implementa la cola condicional original de
   `sceGsSetDefDBuffDc` (SDK real, ELF `0x101D70-0x101DF8`, recuperada por
   desensamblado estático en P4.1.8 y confirmada por oracle en vivo de
   PCSX2 original en P4.1.9): cuando `(interlace==1 && ffmode==1) ||
   interlace==0` (siempre cierto para el `g_gparam` fijo de DMC), la SDK
   parchea `disp[1].dispfb.FBP`, `draw01.FRAME.FBP` y `draw02.FRAME.FBP` a
   `(zbufAddr>>1)&0x1FF`, preservando todos los bits no-FBP — la HLE previa
   los dejaba siempre en `FBP=0` (el default de los helpers internos,
   nunca parcheado). `disp[0]`/`draw11`/`draw12` (patches 7 y 8) no se
   tocan: la cola original tampoco los toca. Regla expresada solo en
   términos de `zbufAddr`/`g_gparam` (semántica del SDK, no un caso
   especial de DMC). Validado 3/3 (P4.1.10) + 1 corrida adicional con el
   binario de producción limpio (sin instrumentación de diagnóstico
   compilada): contrato de RAM guest byte-exacto contra el oracle en las
   4 corridas (UI `DISPLAY={0,0x38}`/`DRAW={0x38,0}`, película post-`Main_init`
   `DISPLAY={0,0xA0}`/`DRAW={0xA0,0}`); 0 draws con `FBP=0xE0` (bug
   anterior) o `FBP=0x70` (valor SDK sin parchear por `Main_init`) en las 3
   corridas con traza; 0/164 muestras de VRAM cruda del buffer de película
   (`dispFbp=0xa0`) con corrupción sistemática en ninguna mitad; Memory
   Card, Language Select y video de advertencia (5 idiomas) completos y
   legibles en las 3 corridas. Checkpoint `FIX_CHECKPOINT_WITH_KNOWN_CAVEATS`
   — `PMODE`/`AMOD` sigue deferido, P4.2.1 (backpressure del ring ES de
   MPEG) independiente y sigue abierto; no es un cierre global de
   BLOCKER_004. Documentación (no duplicada aquí):
   - `analysis/notes/BLOCKER_004_P418_ASTRA_VISUAL_REGRESSION_TIMELINE_COMPARATIVE_AUDIT.md` —
     recuperación de la cola condicional por desensamblado estático (P4.1.8).
   - `analysis/notes/BLOCKER_004_P419_ASTRA_ORIGINAL_UI_DBUFF_CONDITIONAL_TAIL_ORACLE.md` —
     instrucciones de medición manual, oracle en vivo bloqueado en el
     momento de escritura (P4.1.9).
   - `analysis/notes/BLOCKER_004_P419_ADDENDUM_MANUAL_ORACLE_COMPLETION.md` —
     adenda documentando la medición manual posterior del usuario que
     completó el oracle sin reescribir el informe original.
   - `analysis/notes/BLOCKER_004_P410_SCEGSSETDEFDBUFFDC_CONDITIONAL_TAIL_PARITY_FIX.md` —
     implementación del fix, ledger de correcciones y validación 3/3+1
     (P4.1.10).
