# BLOCKER_004 — ORACLE_Q7: GS STAGE FIRST CORRUPTION ORACLE

Agente: FABLE — Fecha: 2026-09-20
Modo: diagnóstico temporal (opt-in `DMC_Q7_TRACE=1`), retirado al final.
Sin fix de producción. NO_COMMIT / NO_PUSH.

Backup pre-Q7: `analysis/local/q7/vendor_before_q7.patch`, SHA256
`6771b381054ffa08682a7f83b3ac3786477417675a7356a4ce1f74846c58c424`
(idéntico al estado pre-Q6: P4.2.3 + P4.2.4 experimental, solo MPEG.cpp dirty).
Restauración final verificada: `git diff` == backup byte a byte, y ejecutable
recompilado desde la lib pristine + MPEG dirty.

Instrumentación temporal usada (5 archivos, toda retirada): MPEG.cpp
(`[Q7:FRAME_BEGIN]` hash del strip buffer por frame), gs_cpu_backend.cpp
(`[Q7:XFER]/[Q7:UPLOAD]` con readback VRAM, `[Q7:MOVE]`, `[Q7:CLEAR]`,
`[Q7:DRAW]`, `[Q7:FALLBACK]`, contadores de writes color/Z del raster),
gs_frontend.cpp (`[Q7:DISPLAY]`, `[Q7:PRIVW]`), GS.cpp
(`[Q7:DBUFFDC]/[Q7:SWAP]/[Q7:SWAPN]`), Support.h (`[Q7:APPLYDISP]`).
Build quirúrgico propio (q7_build.ps1: 4 unidades + reemplazo de miembros lib
partiendo del backup pristine; cero unidades generadas).

Runs: 5 runs manuales (q7run..q7run5, movie+60/20/15/15/12 s), todos parando
antes del source EOF (P4.2.4 irrelevante para lo medido).

---

## Cadena medida (frames primarios: 250 con paridad Q6 exacta; grupo de
## uploads 1..438 completo)

### Etapa 1 — Movie_loadimage / packetización GIF: CORRECTA (HECHO)

- Los 32 strips llegan al GS como 32 transfers host→local PSMCT32
  independientes, cada uno `BITBLTBUF dbw=8, TRXREG rrw=16 rrh=448`,
  `TRXPOS dsax = strip*16, dsay=0`, en orden 0..31 (`[Q7:XFER]`).
- Destino: `dbp` alterna **0x0000 y 0x1400 blocks = páginas 0x00 y 0xA0** —
  exactamente los dos framebuffers del contrato original.
- `[Q7:UPLOAD]`: strips=32, bytes=917.504 por frame, y el hash concatenado de
  los payloads == `stripHash` de `[Q7:FRAME_BEGIN]` del frame decodificado
  (frame 250: `5821630848ad00e5`, igual además a la referencia Q6 offline).
  Cobertura exacta: cada byte fuente una vez, sin omisión, duplicado ni
  reordenación. 438/438 uploads con strips=32 y bytes exactos.

### Etapa 2 — VRAM post-upload: CORRECTA (HECHO)

Readback del rect destino completo (512×448) vía el addressing PSMCT32 real
del backend (`ReadVramUnlocked`) inmediatamente al completar el strip 31:
- `surfHash` del upload 250 == `linearFNV` de la referencia Q6 des-stripeada
  (`c896838419b59201`), y el dump `q7_upload_250_recon_dbp1400.bin` es
  **byte-idéntico** (`cmp`) a `q7_expected_250_linear.bin`.
- POST_UPLOAD_EQUAL = YES. El frame conocido-bueno está PERFECTO en VRAM en
  la página 0xA0 (o 0x00, alternante) tras el upload.

### Etapa 3 — Z: NO INTERVIENE (HECHO; hipótesis ZBUF RETRACTADA)

- Contador de depth-writes del raster: **zW=0 en los 438 intervalos** entre
  uploads del movie (el único zW>0 es el acumulado pre-movie del boot/UI).
- Los sprites del movie llevan `zmsk=1, ztst=1(ALWAYS)` (`[Q7:DRAW]`).
- La explicación "ZBUF alias destruye filas 256-447" queda RETRACTADA como
  mecanismo directo: no hay UNA sola escritura Z durante el movie.

### Etapa 4 — Draws y MoveImage del guest (HECHO)

- Por frame: exactamente **229.376 = 512×448 escrituras de color** de UN
  sprite plano fullscreen (`tme=0` — SIN textura; fade/overlay), con FRAME
  alternando **0xA0 y 0xE0** (blocks 0x1400/0x1C00). No existe ninguna copia
  texturizada del frame: el frame viaja SOLO por el upload.
- 2 `[Q7:MOVE]` por frame (local→local, PSMCT32 512×64):
  `sbp={0x1400|0x0} ssay=352 → dbp=0x2500` y `sbp=0x2500 → dbp=0x2300`
  (páginas 0x128 y 0x118): el guest copia la banda de filas 352-415 del
  framebuffer del frame a dos superficies auxiliares, ambas con el MISMO
  contenido de 64 filas.
- clears=0 durante el movie.

### Etapa 5 — Presentación (HECHO): AQUÍ está la primera corrupción visible

- `[Q7:DISPLAY]`: durante el movie DISPFB alterna **0xA0 y 0xE0**
  (pre-movie UI: 0x00 y 0x70). `usedPreferred=0` siempre;
  `[Q7:FALLBACK]`=0 — las heurísticas host NO intervienen.
- **El buffer 0x00 — que recibe la mitad de los frames — NUNCA se presenta.**
- Vías de escritura de DISPFB: `[Q7:APPLYDISP]`=0, `[Q7:SWAP]`=0 (DBuffDc),
  `[Q7:SWAPN]`=0 (DBuff), `[Q7:PRIVW]` (frontend)=2 (solo boot, fbp=0) ⇒ por
  eliminación, el guest escribe DISPFB **directamente** en los registros
  privilegiados (vía PS2Memory), con el par {0xA0, 0xE0}.
- `[Q7:DBUFFDC]` (2 llamadas): UI zbufAddr=0x70 → tailFbp=0x38 (contrato UI
  {0x00,0x38} ✓); MOVIE zbufAddr=0xE0 → tailFbp=**0x70** (no 0xE0). El init
  HLE del struct movie NO introduce el 0xE0.

## Derivación exacta de los patrones observados (geometría PSMCT32, FBW=8)

La página 0xE0 = página 0xA0 + 64 páginas = **fila 256 del buffer 0xA0**.
"Presentar FBP 0xE0" muestra, fila a fila:

| Filas display (0xE0) | Páginas | Contenido real | Observado (forense P424-FD) |
|---|---|---|---|
| 0-191 | 0xE0-0x10F | filas 256-447 del buffer 0xA0 (frame par) | contenido desplazado verticalmente ✓ |
| 192-223 | 0x110-0x117 | nunca escritas | **banda negra 192-223 exacta** ✓ |
| 224-287 | 0x118-0x127 | destino del MOVE 2 (filas 352-415 del frame) | contenido ✓ |
| 288-351 | 0x128-0x137 | destino del MOVE 1 (LAS MISMAS 64 filas) | **duplicación píxel-exacta de 64 filas (dup64=0.0)** ✓ |
| 352-447 | 0x138-0x14F | nunca escritas | **banda negra 352-447** ✓ |

Patrón B (negro 256-447 al presentar 0xA0): el sprite plano (fade) de los
frames impares se rasteriza con FRAME=0xE0 → escribe físicamente las páginas
0xE0-0x14F = **filas 256-447 del buffer 0xA0** → las borra. Es el
"written then erased" de P4.1.2, ahora con el mecanismo correcto: lo borra el
FADE de color vía FRAME 0xE0, no el Z-buffer.

Todos los rasgos del forense (bandas negras 192-223/352-447/256-447,
duplicación exacta de 64 filas, contenido de dos frames mezclado con corte en
256) quedan derivados al píxel de una única causa.

## Clasificación

**PRIMERA ETAPA CORRUPTA: selección/uso del par de framebuffers.** El frame
decodificado vive intacto en {0x00, 0xA0}; el runtime dibuja el fade y
presenta sobre el par {0xA0, **0xE0**}. La mitad de los frames (los subidos a
0x00) jamás se muestra; los vsyncs impares presentan una página (0xE0) que
nunca recibe el frame y que solapa el buffer real; el fade impar borra la
mitad inferior del buffer par. ⇒ `Q7_FRAMEBUFFER_SWAP_SELECTION_CORRUPT`.

## Relación con el oráculo de Astra (Phase 16) — no contradicho, sí acotado

El contrato congelado (DISPLAY {0x00,0xA0} / DRAW {0xA0,0x00} / ZBUF 0xE0)
describe el **struct** DBuff que el ELF inicializa/parchea. HECHO nuevo: en
runtime ese struct NO gobierna el movie — `sceGsSwapDBuff(Dc)` no se llama ni
una vez; el guest programa DISPFB por escritura directa y FRAME por GIF A+D.
Los VALORES directos observados ({0xA0, 0xE0}) no provienen del init HLE
(tail = 0x70). Queda una única incógnita causal (UNKNOWN):

- ¿El DMC original en hardware también escribe DISPFB={0xA0,0xE0} y FRAME
  impar=0xE0 (y entonces nuestra emulación de alguna semántica asociada —
  p. ej. DBY/origen de lectura, o el modo FIELD half-height — difiere)?
- ¿O el guest recompilado deriva esos FBP de algún dato que nuestro HLE le
  entrega distinto del original, y el original escribe {0x00, 0xA0}?

Resolver esto requiere el oráculo PCSX2 en el movie real (leer DISPFB1/2,
DISPLAY1/2, SMODE2 y los FRAME del display list durante la reproducción) —
propuesto como NEXT (Q8).

## Ledger

- HECHO: packetización GIF de los 32 strips exacta (fuente, orden, bytes).
- HECHO: VRAM post-upload byte-idéntica al frame canónico (readback real).
- HECHO: zW=0 en todo el movie → RETRACTADO "ZBUF overwrite" como mecanismo.
- HECHO: no hay copia texturizada del frame; solo un fade plano fullscreen.
- HECHO: display/draw usan {0xA0,0xE0}; el buffer 0x00 nunca se presenta;
  todos los patrones de corrupción derivados exactamente de esa geometría.
- HECHO: el par {0xA0,0xE0} no sale del init HLE (tail=0x70) ni de los swaps
  HLE (0 llamadas); lo escribe el guest directamente.
- UNKNOWN: paridad con el hardware original de esas escrituras directas
  (pendiente oráculo PCSX2 = NEXT).

Artefactos (untracked): `analysis/local/q7/` (backup, recon dump upload 250,
expected linears 250/480); logs en scratchpad `p424_manual_q7run*/runtime.log`.
