# BLOCKER_004 — checkpoint known-good previo a PERF_001

Fecha: 2026-09-23. Commit principal de entrada:
`65dd4aad9bb9d79e06d9ff9e90bb32c78714ecd1`.
Commit de producción: `88f2233` (`fix: stabilize movie playback and field
presentation`). No push.

## Estado observado y alcance

- HECHO (usuario): Memory Card, Language Select, PSS inicial y Main Menu se
  alcanzaron. En la película, `SEVERE_BANDS_OR_GLITCHES=NO`, temblor vertical
  de todo el canvas ausente o imperceptible, y `MOVIE_IMAGE_STABLE=SI`. El
  usuario la considera visualmente la versión recomp más cercana a PCSX2
  observada hasta esta fecha. El principal problema restante es el tiempo
  de carga y el rendimiento, separado como PERF_001.
- HECHO (Q10.1): con las filas completas del framebuffer y sin bob host,
  `topRow=40` durante 38 presents FIELD consecutivos de paridad alterna;
  FBP continuó `00,00,A0,A0`, DBY=0 y DISPLAY.DY=64. Ver
  `BLOCKER_004_Q10_1_FIELD_PRESENTATION_FIX.md`.
- INFERENCIA: el cambio del presentador elimina la causa identificada de la
  oscilación de línea completa. La percepción visual del usuario no demuestra
  ausencia matemática de cualquier desplazamiento subpíxel.
- UNKNOWN: sigue sin existir una captura en vivo PCSX2 de su algoritmo de
  presentación campo a campo. Este checkpoint no afirma viabilidad del juego
  completo ni cierre global de BLOCKER_004.

## Código y reproducibilidad

- HECHO: los cambios permanentes de vendor se versionan como patches, según
  `patches/README.md` y `upstream.lock.json`. El commit de producción añadió
  `BLOCKER_004_p423_p424_mpeg_movie_completion.patch` (MPEG.cpp sin trazas
  temporales) y `BLOCKER_004_q101_field_presentation.patch`
  (gs_cpu_backend.cpp), más el lock actualizado. No se versionó el checkout
  vendor ni se reescribió el baseline upstream.
- HECHO: los dos patch files, aplicados al vendor previo
  `0efd17c3cdb834ac13c087dcb2bd947d9fc033b3`, reproducen MPEG.cpp y
  gs_cpu_backend.cpp byte por byte. Un segundo replay partiendo del upstream
  real `14b1e5cb39b4af7e6fc12f9a29fdc751efde49d7` aplicó los once
  patches del lock con SHA-256 verificado y produjo exactamente el árbol
  `e68e78e8c86aee1bd0a0ae6c7927a8dcebd6af21` y el commit sintético
  `fd7f2b4358689cbbc46e98ce1c78e3b8b2478963` declarados por el lock.
- HECHO: el diff de Pad.cpp son solo 32 líneas de diagnóstico opt-in
  INPUT_001. No contiene un fix de entrada y no entró en el patchset de
  producción. Se conservó en el checkout vendor local, sin reset ni commit.
  El estado dirty previo al saneamiento MPEG se preservó en
  `analysis/local/pre_perf_checkpoint/vendor_dirty_before_cleanup.patch`,
  SHA-256 `aef6e8339e1e3520d10375018cb2430f1e85e22c262608131c0364fb34f1b2de`
  (artefacto local ignorado).
- HECHO: build incremental normal del target `ps2EntryRunner` bajo
  `analysis/local/symtabfirst/build` pasó después de retirar las trazas
  P4.2.3/P4.2.4. SHA-256 del ejecutable limpio:
  `14605a8af9ab0268215da77f9efc632835eeb1b96b18e73b0f339315c8a6a052`.
  Sin regenerate, clean build ni compilación manual de C++ generado.
- HECHO: `RUN_090` del binario limpio aceptó START y CROSS, llegó al PSS,
  registró `getPicture ENTRY #450`, `MOVIE:post-flush` y `MPEG:delete`, sin
  warnings `ac-tex damaged` ni `MVs not available` y sin líneas de las
  trazas P423/P424 retiradas. Resultado del arnés: `SUCCESS`. Log y metadata
  quedan solo en `analysis/local/p311/RUN_090/`.
- INFERENCIA: la eliminación de trazas opt-in no alteró el camino observable
  de reproducción probado por el smoke. La evaluación visual del usuario fue
  sobre la versión inmediatamente anterior con esas trazas desactivadas;
  el ejecutable limpio se validó objetivamente, no con un segundo juicio
  visual del usuario.

## Inventario previo al commit

Cada archivo modificado o untracked visible en `git status`, incluyendo el
checkout vendor independiente, fue clasificado antes de hacer commit:

| Archivo | Clase | Decisión |
|---|---|---|
| `vendor/PS2Recomp/ps2xRuntime/src/lib/Kernel/Stubs/MPEG.cpp` | PRODUCTION_REQUIRED + TEMP_DIAGNOSTIC | Retirar solo trazas P423/P424; versionar la lógica en patch |
| `vendor/PS2Recomp/ps2xRuntime/src/lib/gs/gs_cpu_backend.cpp` | PRODUCTION_REQUIRED | Versionar en patch |
| `vendor/PS2Recomp/ps2xRuntime/src/lib/Kernel/Stubs/Pad.cpp` | TEMP_DIAGNOSTIC | Preservar local, excluir |
| `.claude/settings.local.json` | LOCAL_ARTIFACT | Excluir |
| `analysis/notes/ARCH_AUDIT_002_SOL_CROSS_IMPLEMENTATION_MPEG_RENDER_ARCHITECTURE.md` | DOCUMENTATION | Checkpoint documental |
| `analysis/notes/BLOCKER_004_CLEAN_VISUAL_TIMING_BASELINE.md` | DOCUMENTATION | Checkpoint documental |
| `analysis/notes/BLOCKER_004_P423_MPEG_DEMUX_CALLBACK_COMPLETION_OWNERSHIP_FIX.md` | DOCUMENTATION | Checkpoint documental |
| `analysis/notes/BLOCKER_004_P424_FIRST_DIVERGENCE_VISUAL_CLASSIFICATION.md` | DOCUMENTATION | Checkpoint documental |
| `analysis/notes/BLOCKER_004_P424_MPEG_END_OF_STREAM_VIBUF_DRAIN_BEFORE_DECODER_FLUSH.md` | DOCUMENTATION | Checkpoint documental |
| `analysis/notes/BLOCKER_004_Q6_MPEG_HOST_RGBA_VS_GUEST_POST_GETPICTURE_ORACLE.md` | DOCUMENTATION | Checkpoint documental |
| `analysis/notes/BLOCKER_004_Q7_GS_STAGE_FIRST_CORRUPTION_ORACLE.md` | DOCUMENTATION | Checkpoint documental |
| `analysis/notes/BLOCKER_004_Q8_PCSX2_MOVIE_GS_DISPLAY_STATE_ORACLE.md` | DOCUMENTATION | Checkpoint documental |
| `analysis/notes/BLOCKER_004_Q10_FIELD_BOB_PRESENTATION_ORACLE.md` | DOCUMENTATION | Checkpoint documental |
| `analysis/notes/BLOCKER_004_Q10_1_FIELD_PRESENTATION_FIX.md` | DOCUMENTATION | Checkpoint documental |

Los originales, ISO, logs, capturas, dumps, builds y respaldos bajo
`analysis/local/**` están ignorados: LOCAL_ARTIFACT. UNKNOWN: ninguno.

Las decisiones `NO_COMMIT` de las notas P4.2.3 y Q10.1 describían esas
fases en su momento. No se reescriben retroactivamente: este checkpoint
posterior, autorizado tras la validación visual Q10.1, registra la promoción
conjunta del comportamiento MPEG P4.2.3/P4.2.4 y la presentación FIELD.

## Estado para la siguiente fase

El baseline reproducible está fijado en los patches y en
`upstream.lock.json`. El checkout vendor de esta sesión conserva Pad.cpp
diagnóstico dirty y HEAD antiguo; no debe tratarse como checkout limpio del
lock ni usarse con `scripts/pipeline.py upstream` hasta resolver ese estado
local sin perder su respaldo. El inicio de PERF_001 debe partir del lock
reproducido o registrar explícitamente el overlay local que utilice.

Siguiente investigación: `PERF_001_RUNTIME_REALTIME_AND_PREMOVIE_LOAD`.
No se realizó ningún push.
