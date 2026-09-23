# BLOCKER_004 — Clasificación READ-ONLY de primera divergencia visual (interrupción de P4.2.4)

Agente: FABLE — Fecha: 2026-09-13
Contexto: P4.2.4 SUSPENDIDO temporalmente por orden del usuario. Cero ediciones
de código durante esta clasificación. Preservados: backup P4.2.3
(`analysis/local/p424/p423_validated_baseline.patch`), diff dirty P4.2.4
actual en vendor, RUN_094/095/096/097.

Evidencia primaria: screenshots `state_XX.png` (cadencia 20 s) de RUN_094 y
RUN_095 — que SÍ evolucionan y son visualmente distintos entre sí (el oracle
de captura NO está congelado en estos runs, a diferencia de RUN_091, cuyos
state_04/05/06 comparten un único hash `bb5e3cad…` y siguen siendo inválidos).

Cronología común de ambos runs: MOVIE_START ≈ 70 s; source EOF
(`[P424:SOURCE_EOF] consumed=23183360`) ≈ 148 s; teardown ≈ 150 s; fin de
proceso 180 s. Samples de movie: state_04=80 s, state_05=100 s, state_06=120 s,
state_07=140 s — TODOS anteriores al source EOF.

## Respuestas 1-12

**1. ¿Cuándo aparece la corrupción por primera vez?**
HECHO: ya en state_04 (t≈80 s, ~10 s tras el arranque del movie) en AMBOS
runs. Es el primer sample disponible del movie: no existe ningún screenshot
de movie limpio. La corrupción persiste en todos los samples (80/100/120/140 s).

**2. ¿Ocurre ANTES del source EOF?**
HECHO: sí. Corrupción a 80-140 s; source EOF a ≈148 s (68 s de margen).

**3. ¿El ES consumido por FFmpeg era canónico en ese momento?**
HECHO: sí. Capturas consumed: RUN_094 = 23.228.416 bytes, RUN_095 =
23.277.568 bytes; ambas verificadas por `cmp` como prefijos EXACTOS byte a
byte del canónico (SHA b2e29f64…). Accepted completo canónico
(23.654.877 / SHA exacto) en ambos runs.

**4. ¿Warnings FFmpeg antes del primer frame corrupto?**
HECHO: cero. `ac-tex damaged` = 0 y `MVs not available` = 0 en los logs
COMPLETOS de ambos runs. El único match de "warning" (RUN_094 línea 250) es
`[ps2xIOP:warning] [CDMODULE] unsupported fno=2` — IOP/CD, no decode.
⇒ CONFIRMADO el enunciado del usuario: cero warnings FFmpeg ≠ salida visual
correcta.

**5. ¿El RGBA decodificado en guest RAM es correcto antes de Movie_loadimage?**
UNKNOWN — no medido en estos runs. `getpicture.bin` (32 MB RDRAM) se vuelca a
la ENTRADA del PRIMER `sceMpegGetPicture` (MPEG.cpp ~3338), antes de escribir
frame alguno: la región `imageAddr 0x79D680` está 100 % a cero en los dumps de
RUN_094 y RUN_095 (verificado; esperado; no informativo).

**6. Método mínimo para compararlo (definido, NO implementado ahora):**
(a) volcar `imageAddr 0x79D680` (917.504 B) inmediatamente DESPUÉS de que
GetPicture escriba el frame N (o en la entrada de Movie_loadimage), con N
determinista (frame 1);
(b) en host: `ffmpeg -i canonical_video.es` → frame N RGBA 512×448 (ffmpeg
9.0 CLI disponible en el sistema, verificado);
(c) comparación píxel a píxel. La infraestructura de dump ya existe
(patrón DMC_P311_RAM_GETPICTURE); solo hay que mover el punto de captura —
un cambio de ~1 línea cuando se reanude la implementación.

**7-8. Trazado por etapas y clasificación de la primera corrupción**

| Etapa | Estado | Evidencia |
|---|---|---|
| MPEG decode (FFmpeg) | SIN evidencia de fallo | entrada byte-canónica (Q3) + 0 warnings (Q4); los fallos MPEG producen basura DCT en macroblocks de 16 px, jamás negro puro alineado a páginas ni duplicación exacta |
| Layout/copia a guest RAM | UNKNOWN | sin oracle aún (Q5/Q6) |
| Upload GIF (strips) | HECHO previo (P4.1.2): completo | GSBUF readback verificó las 448 filas presentes tras upload; además un fallo de strips daría período 14 (448/32), no observado |
| GS local (MoveImage / aliasing FRAME-ZBUF) | **PRIMERA DIVERGENCIA (INFERENCIA fuerte)** | geometría de páginas, abajo |
| Framebuffer selection / presentación | contribuye | alternancia de patrones A/B entre los dos buffers del double-buffer |

Forense de píxeles (numpy, filas exactas, ambos runs):

- **Patrón B** (RUN_095 state_06): negro EXACTO en filas **256-447**. Es
  precisamente la región donde ZBUF 0xE0 solapa FRAME 0xA0 (FBW 8, PSMCT32:
  page-row = 32 filas; 0xE0−0xA0 = 64 páginas = 8 page-rows = fila 256).
  Reproduce el HECHO P4.1.2 "written then erased".
- **Patrón A** (RUN_094 state_04/06, RUN_095 state_04/05): negro EXACTO en
  filas **192-223** (page-row 6, 32 filas) y **352-447** (page-rows 11-13);
  banda visible 224-351 con **duplicación píxel-idéntica de período 64 filas**
  (= 2 page-rows; `dup64 = 0.0` de diferencia media en RUN_094 state_04).
- Todas las fronteras son múltiplos exactos de 32 filas (páginas PSMCT32
  64×32). Ninguna unidad MPEG (macroblock 16) ni de strip-upload (14) encaja.

Clasificación: la primera corrupción pertenece a la etapa **GS
(transferencia local / aliasing FRAME-ZBUF / composición-presentación de
buffers)**, con la etapa "layout/copia guest" pendiente de exclusión formal
mediante el oracle de Q6. NO es decode MPEG.

**9. ¿Es el FIELD bob de ~2 px?**
NO (probado): bandas negras de 32-192 filas y duplicación exacta de 64 filas
no son producibles por paridad de campo (desplazamiento de 1 línea
alternante). Residual FIELD sigue siendo un asunto separado.

**10. ¿Puede el EOF drain de P4.2.4 arreglar esta corrupción?**
NO — HECHO por análisis de flujo: TODAS las ediciones P4.2.4 (deferral de
flush, gate sin `!streamEnded`, drain, IsRefBuffEmpty, IsEnd) solo alteran el
comportamiento a partir de `streamEnded=true` (≈148 s). A los 80-140 s el
código ejecutado es idéntico al de P4.2.3. El EOF drain afecta únicamente a
los últimos ~2-3 s de video (471.517+4 bytes finales). Son problemas
ortogonales: P4.2.4 cierra el pipeline de BYTES; esta corrupción vive en el
pipeline de PRESENTACIÓN.

**11. Re-evaluación del "drain completo en un GetPicture" (edición 9):**
INFERENCIA / compensación HLE — NO semántica original probada. En hardware
real el IPU consume viBuf por DMA asíncrono mientras el guest itera su flush
loop acotado; el burst síncrono replica el efecto final, no el mecanismo.
Riesgos anotados: ~60 MB transitorios de frames RGBA en el deque; frames
finales potencialmente nunca presentados (el guest corta por gate18).
Estado de validación dinámica: los runs del harness fallaron
(RUN_096/RUN_097 = HARNESS_ERROR `screen grab failed` a ~10 s, fallo del
capturador, no del fix). Un run manual lanzado ANTES de la orden de STOP
terminó por sí solo; su log (evidencia ya existente, no continuación de
implementación) muestra: `[P424:TAIL] chunk=481 remainingAfter=0`,
`[P424:DECODER_EOF] accepted=consumed=23654881 backlog=0`, 0 warnings, y el
prefijo consumed = canónico exacto + `00 00 01 B7`. HECHO nuevo: los 4 bytes
extra de `videoBytesTotal` son un **sequence_end_code MPEG inyectado por el
propio guest** (verificado en la frontera del capture), no bytes fabricados
por el HLE. El mismo run muestra al juego reproduciendo después una SEGUNDA
película PSS (accepted 25.124.999 bytes, también drenada a backlog 0). Nada
de esto se considera validación formal de P4.2.4: queda pendiente de reanudar
bajo las reglas del prompt.

**12. Cambios de código durante esta clasificación:** NINGUNO. Solo lecturas,
análisis de PNG/binarios y este informe.

## Objetivo primario — respuesta

La primera etapa donde un frame decodificado conocido-bueno se vuelve
visualmente corrupto es la etapa **GS** (aliasing FRAME/ZBUF en el buffer
0xA0 + composición/duplicación con geometría de páginas en el otro buffer),
por delante de la presentación y por detrás del upload (completo según
P4.1.2). Para convertir la INFERENCIA en HECHO falta un único oracle: la
comparación guest-RAM vs FFmpeg host del punto Q6, que además excluiría (o
señalaría) formalmente la etapa layout/copia del stub.

NO_COMMIT. NO_PUSH.
