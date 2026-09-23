# BLOCKER_004 — Baseline limpio visual/timing (post-Q9)

Agente: FABLE — Fecha: 2026-09-21
Build: `analysis/local/symtabfirst/build` (recompilado justo antes:
backup-lib pristine + MPEG.cpp dirty P4.2.3+P4.2.4; cero instrumentación
Q6/Q7/Q9 en código y ejecutable — verificado que el diff de MPEG.cpp es
byte-idéntico al backup pre-Q7 y que no queda ningún marcador Q7/Q9).
Pad.cpp conserva el diagnóstico INPUT_001 en fuente (opt-in, NO-Q9, y además
no compilado en este exe). NO_COMMIT / NO_PUSH.

Run: manual, misma vía de input física (pad-file), SOLO env obligatorio
(CD/arena/ownership/sync-redecode). Sin DMC_P311_MPEG_DIAG, sin capturas,
sin trazas opt-in. Log total: 366 KB en 142 s (~2,6 KB/s de spam residual de
los `std::cerr` incondicionales del código generado — no recompilable en esta
fase; despreciable como causa de una ralentización ~3×).

## Medidas (wall-clock)

| Marca | Evento | t (s) |
|---|---|---|
| A | Salida de Language Select (tecla X) | 14,62 |
| B | `[CDMODULE/MOVIE:start]` | 71,34 |
| C | Primer frame presentado (`getPicture success #1`) | 71,61 |
| D | Fin del movie (`[MPEG:delete]`) | 127,50 |

- **LANGUAGE_TO_MOVIE_START_SECONDS = 56,72**
- **MOVIE_START_TO_FIRST_FRAME_SECONDS = 0,27**
- **MOVIE_REAL_DURATION_SECONDS = 55,89**
- VSYNC en B/C/D: 480 / 480 / 1500 (muestras `[GP:VSync]` cada 60 ticks)
- Pictures servidas: contador siempre-on marcó ≥450 (log cada 50) — coherente
  con ~470-498 totales.

## Fuente canónica (ffprobe, HECHO)

`canonical_video.es`: mpeg2video 512×448, **25 fps exactos (25/1), 498
frames, 19,92 s** de duración nominal.

## Análisis de cadencia

- Cadencia PROGRAMADA: `frameIntervalQ32 = 0x200000000` = 1 picture cada 2
  vsync ticks. A 50 Hz PAL ⇒ 25 fps. **CORRECTA para la fuente** — no es un
  error de pacing.
- Cadencia INTERNA cumplida: (1500−480) = 1020 ticks para ~470 pictures ≈
  2,17 ticks/picture ✓ (el pequeño exceso son frames repeat/espera inicial).
- Cadencia REAL de pared: 1020 vsyncs en 55,89 s ⇒ **18,2 vsync/s frente a
  los 50 Hz nominales → el runtime ejecuta el movie al ~36 % de tiempo real**
  (55,89 s para 19,92 s de video = 2,81× lento; ~8,9 fps efectivos).

**Conclusión (HECHO):** la percepción de "bajo framerate" es el emulador
corriendo por debajo de tiempo real, no una cadencia mal programada. La
percepción de "carga larga" también es real y limpia de instrumentación:
56,7 s de Language Select a movie. Ambas cifras son ahora baseline oficial
sin overhead de diagnóstico opt-in (el residual generado ~2,6 KB/s no explica
un 2,8×).

## Comparación de cadencias (pedida por el prompt)

| Cadencia | Valor |
|---|---|
| FPS de la fuente MPEG | 25 (498 frames / 19,92 s) |
| Cadencia original PCSX2 | 25 fps (1 frame / 2 fields a 50 Hz PAL) — por contrato de la fuente; no re-medida aquí |
| Cadencia pretendida del recomp | 25 fps (frameIntervalQ32 = 2 ticks) ✓ |
| Cadencia real de pared del recomp | ~8,9 fps (2,81× lento; vsync ~18,2 Hz) |

## Preguntas visuales al usuario (pendientes de su respuesta)

- SEVERE_BANDS_OR_GLITCHES: YES / NO
- WHOLE_CANVAS_VERTICAL_JITTER: YES / NO
- MOVIE_FEELS_LOW_FPS: YES / NO

## Notas

- El movie terminó de forma natural vía la ruta guest (gate18 → flush →
  `[MPEG:delete]`) tras ~470+ pictures de 498 — el corte por contador guest
  precede a servir el stream completo; consistente con el diseño del juego
  (fin por tiempo/audio), no un deadlock.
- Clasificación heredada del Q9 del usuario (congelada aquí):
  CURRENT_GUEST_DISPFB_PAIR = {0x00, 0xA0} CORRECTO (494 muestras, 0×0xE0);
  el par {0xA0,0xE0} del Q7 histórico queda válido solo para aquel build/run
  y su reconciliación de identidad de build queda pendiente por separado.
- Siguiente si el jitter persiste en la observación del usuario:
  Q10_FIELD_BOB_PRESENTATION_ORACLE (SMODE2/paridad de field/DISPLAY DY,
  PCSX2 vs recomp, sin tocar FIELD/bob antes de comparar).
- El coste de A→B (56,7 s de carga) es un problema de rendimiento separado
  del movie; ninguna optimización debe basarse aún en runs instrumentados.
