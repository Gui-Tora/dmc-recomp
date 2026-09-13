# P4.1F — Fable: auditoría adversarial del fix DISPFB0 y aislamiento del flicker residual

Auditoría post-fix independiente. Sin build, sin corridas nuevas del juego,
sin commit, sin push, sin cambios de producción. Evidencia: RUN_043-046
existentes, dumps RAM, fuente actual del vendor, ELF, y verdad de terreno
extraída OFFLINE del ISO (PSS del opening, lba 0x1F62B8).

## 1. Estado de entrada (HECHO)

Main HEAD `c9ee65d` ("fix: correct GS double-buffer display base
initialization") ✓; origin sigue en `6beacfb` (sin push) ✓. Vendor HEAD
`16ef7ab8dc961c44921c3776705da6bc3b4806fb` (patchset determinista) ✓,
limpio ✓. `patches/BLOCKER_004_p41_scegssetdefdbuffdc_dispfb0_zero.patch`
presente. Sin dirt ajeno. Nada de lo prohibido se modificó.

## 2. AUDIT A — ¿Es P4.1 causal? → SÍ (P41_FIX_CAUSAL_AND_VALIDATED)

1. **Patch**: leído completo — exactamente un cambio semántico
   (`makeDispFb(fbp1,…)` → `makeDispFb(0u,…)`) + comentario; el fix está
   presente en el árbol vendor actual (GS.cpp:907-912). HECHO.
2. **Mismo exe 3/3**: `27faab90…` en result.json de RUN_044/045/046. HECHO.
3. **Dumps 3/3**: leídos independientemente de los getpicture.bin:
   `0x740C30=0xff26, 0x740C40=0x1000, 0x740C68=0xff26, 0x740C78=0x10a0`
   byte-idéntico en las tres. HECHO.
4. **0x10E0 ausente** del env en las tres. HECHO.
5. **Confundidores RUN_043→RUN_044**: mismo ISO/arena/inputs/modo
   (`DMC_P313_INIT_MODE=ownership`); deltas = (a) diagnósticos P40
   retirados (solo logging, auditados línea a línea en P4.0F: sin cambio
   de comportamiento), (b) env de logging MPEG_DIAG/RAM añadido (solo
   logging), (c) el fix. Único delta semántico = el fix. La mejora visual
   (fragmentos irreconocibles → película/texto legible) coincide
   temporalmente con él. Sin confundidor superviviente.

No hay ninguna razón para revertir. El checkpoint `c9ee65d` vale la pena.

## 3. AUDIT B — DISPFB2 activo: promovido a HECHO-por-contrato

Enumeración exhaustiva de escritores de `0x12000090` (grep del árbol
generado completo, ya hecho en P4.0F y re-usado): exactamente TRES
funciones guest — `MainGsSwapDBuffDc` (viva; copia literal `Store64` del
struct, desensamblado completo en P4.0.1 §8), `MainGsSwapDBuffDc_movie` y
`Main_Get_Vsync` (ambas muertas por el gate `0x5CE3FF=0`, probado en
P4.0F con tabla estática sin escritores). Lado runtime: el pseudo-registro
GIF 0x5B solo lo emiten los HLE `sceGsPutDispEnv`/`sceGsSwapDBuff*`, sin
ningún caller guest (grep exhaustivo, P4.0F); el worker de vsync solo toca
CSR. `Store64`→`write64`→`gs_regs.dispfb2` verificado. Con el struct guest
probado (§2.3), **el DISPFB2 activo alterna {0x1000, 0x10A0} por contrato
cerrado de flujo de datos** — no hace falta la medición directa.

## 4. AUDIT C/D — el supuesto "warnings FFmpeg inofensivos": REFUTADO, con sorpresa doble

**Datos (HECHO, 3/3 corridas):**
- 150 mensajes mpeg2video por corrida: 41-42 `ac-tex damaged`, 77-80
  `MVs not available`, 26 `skipped MB in I-frame`, 5 `Invalid mb type`,
  1 `slice mismatch`.
- **Cero mensajes en las pictures 1–42. Primer daño SIEMPRE en la
  picture 43**, y el conjunto de pictures dañadas es IDÉNTICO en las tres
  corridas (43,46,47,49,52,55,56,58,59,60,61,62,65,66,70,…) —
  determinismo perfecto.
- Posiciones de slice dañado aleatorias (mb_x 0-30, mb_y 1-27).
- **Verdad de terreno**: el PSS extraído directamente del ISO decodifica
  con **0 warnings en 200 frames** (ffmpeg, mismo decoder).

**Conclusión (HECHO por eliminación): el bitstream que llega al decoder
en runtime está corrupto a partir de la picture 43, la corrupción la
introduce el camino de alimentación del RECOMP (feed/recirculación
viBuf), es determinista en función de la posición del stream, y la fuente
es limpia.** La frase de P4.1 «mensajes normales y recurrentes de ffmpeg…
no errores» queda **RETRACTADA como caracterización**: son la huella de
un defecto real de feed (aunque su presencia no afectaba a la validación
del fix DISPFB0, que era el objetivo de aquel prompt). El concealment
resultante (`MVs not available`) explica los frames granulados/blocky y
los manchones/estiramientos en la zona ≥43 (p. ej. visual_082641 ≈
picture ~92 ∈ cluster dañado {91,92,93}; visual_091655 ≈ ~172 ∈
{170,171,172,174}).

Nota de correlación con el wrap: a ~47.5 KB de ES por picture (medido con
ffprobe sobre el PSS), la picture 43-44 cae cerca del 4º ciclo de la
capacidad útil del ring (4×520192=2 080 768 vs acumulado 1 991 400–
2 086 402) — sugerente pero NO concluyente (los 3 primeros wraps son
limpios); el disparador exacto queda UNKNOWN para P4.1.1.

## 5. El SEGUNDO componente residual — half-frames tempranos SIN warnings

**HECHO (secuencia visual RUN_044)**: 73.7s = warning PAL completo (5
párrafos, tenue) → **74.2s = solo ~3.5 párrafos, más brillantes, negro
debajo (~58% de altura)** → 75.3s = completo (brillante). La picture
correspondiente (~17-40) está en la zona con CERO mensajes del decoder, y
la verdad de terreno (gt_017/gt_040 del PSS del ISO) muestra los 5
párrafos SIEMPRE presentes (el fade es simultáneo, no progresivo). El
fondo negro del half-frame corresponde a un estado del fade MÁS ANTIGUO:
**mezcla temporal intra-imagen (arriba nuevo / abajo viejo), transitoria,
no correlacionada con el daño del decoder.**

Mecanismo: **UNPROVEN**. Candidatos, ordenados tras la evidencia (Audit E/F/G/H/J):

1. **Coreografía clear/upload/flip sobre 0x140000** — el buffer de
   película `disp[1]` (0x140000=FBP 0xA0) es TAMBIÉN el objetivo del
   sprite de fade negro (FRAME a0), en tareas guest distintas
   (Movie task vs ps2_main) cuyo interleaving depende del scheduler del
   RECOMP; un orden distinto al del hardware puede exhibir el buffer
   recién clareado o a medio actualizar. (Candidato principal; no
   medido.)
2. **Fallback `fbp==0` de la presentación** — POST-FIX, `disp[0]` es
   FBP 0, lo que habilita por primera vez la heurística
   `countNonBlackPixels==0 → sustituir por un context frame`
   (gs_cpu_backend.cpp:1816): con frames del vídeo totalmente negros
   (fundidos), la presentación puede mostrar un buffer AJENO (a0/e0 del
   fade). Riesgo nuevo específico post-fix; no medido.
3. **Tearing de captura/compositor** — el screenshot del harness puede
   partir horizontalmente entre dos presents consecutivos; explicaría
   fronteras variables, no una estable.
4. Concealment silencioso del decoder — improbable (decoder mudo, parser
   presente, fuente limpia).

Descartados con mecanismo concreto: los 32 strips VERTICALES no pueden
producir una frontera horizontal (Audit E; TRXREG fijo 16×448 desde la
cadena estática); el bob de campo duplica líneas dentro de altura
completa, no puede ennegrecer la mitad (Audit F); la subida GIF es
síncrona en el hilo EE (`processPendingTransfers` inline en el kick,
ps2_memory.cpp:1540) así que no hay carrera upload-vs-flip a nivel guest
(Audit G) — el único interleaving posible (Snapshot entre strips, lock
liberado entre llamadas a `UploadImage`) daría cortes VERTICALES; el
canvas host 640×512 solo añade un letterbox constante (Audit J).

## 6. AUDIT I — AMOD: NO causal

En el RECOMP, `PresentFromLocalMemory` con EN1=0 toma el camino de
circuito único, que **no usa** `pmode.alp/mmod/amod` (solo el camino dual
mezcla). En hardware, AMOD solo selecciona la fuente del alfa de salida
(para mezcla externa) — sin efecto geométrico y sin consumidor con EN1=0.
**No puede producir half-frames, alternancia vertical ni flicker.**
Clasificación: bug de paridad SDK, NO causal — no debe corregirse antes
de investigar la geometría residual (aunque su fix de una línea puede
acompañar a otro checkpoint).

## 7. AUDIT sobre las afirmaciones del informe P4.1 (A–F del prompt)

- A «fix validado» — **SOSTENIDA** (§2).
- B «sin regresión MPEG; 187/192/194» — **SOSTENIDA CON CORRECCIÓN DE
  REDACCIÓN**: son CONTEOS DE LLAMADAS (`[MOVIE:decode-task] CALL`); los
  éxitos reales están muestreados por throttle (`success #N` en
  N=1..10,50,100,150) y el contador de éxito IGUALA al de entrada en cada
  muestra hasta #150 → **≥150 pictures reales, ratio ~100% hasta ahí**;
  el total exacto de éxitos no está en el log. La sustancia (sin
  regresión) se sostiene; la cifra debe re-etiquetarse como llamadas.
- C «warnings normales/recurrentes» — **REFUTADA** (§4): deterministas,
  ausentes de la fuente, introducidos por el feed. Corrección documental
  requerida.
- D «half-frame no bloquea la conclusión» — correcto para validar el fix;
  y SÍ, indica el siguiente divergente (dos componentes, §4-§5).
- E «DISPFB2 activo coincide» — inferencia PROMOVIDA a
  HECHO-por-contrato (§3).
- F «PSM sin auditar» — degradada a caveat de baja prioridad: con
  imagen/texto/colores coherentes tras el fix, un error de PSM global es
  incompatible con lo observado.

## 8. Tabla causal requerida

| Layer | P4.1 evidence | Residual-frame status | Classification | Caveat |
|---|---|---|---|---|
| FFmpeg decoded frame | 512×448 constante; ≥150 éxitos | pictures 1-42 limpio; **≥43 corrupto por bitstream de entrada (determinista, fuente limpia)** | **MISMATCH (≥43)** / UNKNOWN (<43) | daño introducido ANTES del decoder (feed) |
| writeDecodedFrameToGuest | R,G,B,A=0x80; strips 16×448; siempre 448 filas | sin mecanismo de truncado vertical | MATCH | no re-medido por-frame |
| guest frame 0x79D680 | consumido por 32 REF exactos (P3.15.2) | sin evidencia de defecto propio | MATCH | hash por-frame pendiente |
| 32-strip GIF upload | geometría completa (P4.0F, 384/384) | strips verticales ⇒ no pueden causar frontera horizontal | MATCH | pre-fix; post-fix sin GSBUF |
| GS local-memory framebuffer | mismatch=0 (autoconsistencia) | contenido depende de coreografía clear/upload | PARTIAL | fade escribe el mismo buffer 0x140000 |
| DISPFB2 active selection | env {0x1000,0x10A0} == original; HECHO-por-contrato | correcto | MATCH | fase idx↔which no medida |
| field/interlace transform | bob por vsyncTick | no puede producir mitad negra | PARTIAL (aprox.) | shimmer/res. vertical |
| CopyFrameToHostRgba | fiel al DISPFB | **fallback fbp==0 nuevo post-fix** (sustitución con frame negro) | PARTIAL | riesgo no medido |
| host presentation buffer | canvas 640×512, rect 512×448 | letterbox constante benigno | MATCH | — |
| renderer handoff | coherente | sin evidencia en contra | MATCH | tearing de captura posible aguas abajo |

## 9. Tres nodos

- **P4.1_LAST_PROVEN_COMMON_NODE**: la selección de scanout post-fix —
  env guest {0x1000, 0x10A0} == medición original, DISPFB2 activo
  HECHO-por-contrato, presentación leyendo el buffer seleccionado.
- **P4.1_FIRST_PROVEN_DIVERGENT_NODE**: el bitstream ES de vídeo
  entregado al decoder desde la picture 43 — corrupto de forma
  determinista (conjunto de pictures dañadas idéntico 3/3) mientras la
  misma fuente decodifica limpia offline; introducido por el camino de
  feed/recirculación del RECOMP.
- **P4.1_FIRST_UNPROVEN_NODE**: el mecanismo del half-frame/flicker
  temprano (pictures <43, decoder mudo) — capa entre la salida del
  decoder y la captura presentada (candidatos §5: coreografía
  clear/upload/flip sobre 0x140000, fallback fbp==0, tearing de captura).

## 10. Respuestas 1–25

1. SÍ. 2. SÍ (único delta semántico). 3. SÍ. 4. NO. 5. SÍ —
HECHO-por-contrato (§3). 6. LLAMADAS; éxitos ≥150 probados por el
contador muestreado. 7. Solo la redacción; la sustancia no. 8. Solo en
la zona ≥43 (los half-frames tempranos NO correlacionan). 9. SÍ en zona
≥43 (concealment); improbable en <43 (decoder mudo). 10. NO (siempre
448 filas; relleno negro solo si el frame fuente fuera menor — no
observado, dims 512×448 constantes). 11. NO — geometría vertical. 12. NO
— TRXREG fijo desde cadena estática. 13. NO — el bob no ennegrece.
14. Parcialmente — no como carrera DMA (síncrona), sí como coreografía
guest clear/upload/flip (candidato #1, no probado). 15. Solo el
interleaving Snapshot-entre-strips (vertical, no observado como síntoma
dominante). 16. UNKNOWN (capturas ~cada 4.5 pictures: sin phase-lock).
17. UNKNOWN (ídem). 18. NO (§6). 19. NO. 20. Baja prioridad ya (§7.F).
21. NO. 22. La completitud del MISMO frame a través de las fronteras
decode→guest→VRAM→pre-field→post-field, junto con los offsets del feed
(bytes ES alimentados + eventos de recirculación/wrap) por picture.
23. SÍ — una sola corrida instrumentada. 24. Firma compacta por picture:
(a) en el feed: contador acumulado de bytes ES + offset/wrap de
recirculación por chunk; (b) hash de 16 bandas de 28 filas del frame
FFmpeg antes de `writeDecodedFrameToGuest`; (c) mismo hash tras escribir
0x79D680; (d) hash de bandas del buffer VRAM destino tras el kick; (e)
por latch: DISPFB2 activo, sourceFbp, usedPreferred (¡el fallback
fbp==0!), field, hash de bandas pre/post `applyFieldPresentation`.
25. **P4.1.1 — MOVIE_FRAME_COMPLETENESS_BOUNDARY_TRACE** (cubre ambos
residuales en una corrida y discrimina los candidatos de §5 entre sí).

## 11. Recomendaciones documentales (para el próximo checkpoint de docs)

1. Reetiquetar 187/192/194 como llamadas; añadir «≥150 éxitos probados».
2. Retractar «ac-tex/MVs = normales»: son corrupción de feed
   determinista (fuente limpia 0/200), primer divergente residual.
3. Registrar el riesgo nuevo del fallback `fbp==0` de presentación
   habilitado por el propio fix (correcto, pero con heurística aguas
   abajo que ahora puede activarse).
4. AMOD: no causal; fix de paridad diferido sigue siendo correcto.

Sin cambios a informes históricos ni a la nota canónica en este prompt.
Git al cierre: main `c9ee65d` + este informe sin trackear; vendor limpio
`16ef7ab`. NO commit, NO push, NO build, NO corrida nueva.
