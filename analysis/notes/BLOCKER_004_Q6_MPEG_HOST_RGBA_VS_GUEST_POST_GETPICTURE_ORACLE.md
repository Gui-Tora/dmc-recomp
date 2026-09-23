# BLOCKER_004 — ORACLE_Q6: HOST RGBA vs GUEST imageAddr POST-GetPicture

Agente: FABLE — Fecha: 2026-09-20
Modo: DIAGNOSTIC ORACLE ONLY. Sin fix de producción. Sin cambios GS. Sin
continuación de P4.2.4. NO_COMMIT / NO_PUSH.

Estado de entrada verificado: main `65dd4aa`, vendor `0efd17c3` DIRTY
(P4.2.3 + P4.2.4 experimental, +844/−35).
Backup pre-Q6: `analysis/local/q6/vendor_before_q6.patch`,
SHA256 `6771b381054ffa08682a7f83b3ac3786477417675a7356a4ce1f74846c58c424`
(1082 líneas, untracked).

## Fase 1 — Ruta exacta de escritura del frame (HECHO, código activo)

- **Host frame**: `MpegDecodedFrame.rgba` — RGBA contiguo `width*height*4`,
  producido por `sws_scale` en `MpegFfmpegDecoder::convertFrame`
  (src = formato nativo del codec (YUV420P), dst = `AV_PIX_FMT_RGBA`, misma
  resolución, `SWS_BILINEAR`; `dstLinesize = width*4` — sin padding).
  `pts90k = best_effort_timestamp` (para este ES m2v crudo resulta **−1**:
  sin PTS utilizables).
- **Escritura al guest**: `writeDecodedFrameToGuest(rdram, imageAddr, frame)`
  (MPEG.cpp; único caller: `sceMpegGetPicture`, camino `haveFrame`).
  `imageAddr` = registro a1 (`getRegU32(ctx,5)`); observado constante
  `0x0079D680`.
- **Layout guest — NO lineal (HECHO nuevo verificado en código)**: 32 strips
  VERTICALES de 16 píxeles de ancho × `outHeight` filas. Para cada columna de
  macroblock `mbx` (0..31 con width 512):
  `dst = imageAddr + mbx*(outHeight*64) + y*64`, 64 bytes por fila de strip.
  Alpha forzado a **0x80** en cada píxel (transformación intencional del
  código; PS2 alpha neutro). Píxeles fuera de `width×height` → negro+0x80.
- Dimensiones observadas: 512×448, `align16` sin efecto (ya alineado),
  total escrito = 32×448×64 = **917.504 bytes**. La interpretación histórica
  "512×448×4 = 917.504" queda elevada a HECHO, con la precisión de que el
  layout es por strips de columna, no filas lineales.

## Fases 2-4 — Oracle en la frontera de escritura

Instrumentación temporal (`DMC_Q6_TRACE=1` / `DMC_Q6_DUMP=1`, OFF por
defecto, cero cambio semántico): tras `writeDecodedFrameToGuest`, en el mismo
hilo y bajo el mismo lock, se construye BUFFER A (réplica exacta de la
transformación que el código escribe: strips + alpha 0x80) y BUFFER B
(lectura de RDRAM por las mismas direcciones), se comparan con
`memcmp` + hash FNV-1a 64 por frame (SHA256 offline para los frames
volcados), log `[Q6:FRAME]` por frame.

Punto de captura exacto: `sceMpegGetPicture`, inmediatamente después de
`writeDecodedFrameToGuest`, antes de retornar al guest — ningún
Movie_loadimage/GIF/GS puede haber tocado el buffer.

## Resultados

**Runs**: 2 runs manuales válidos (runner sin ImageGrab; harness run.py sigue
fallando con `screen grab failed`). Cada run: MOVIE_START ≈ 72 s, observación
60 s de movie (global ~72-132 s), parada ANTES del source EOF (≈ movie+78 s)
⇒ el diff experimental P4.2.4 es irrelevante para los frames medidos.

| Métrica | Run 1 (q6run) | Run 2 (q6run2) |
|---|---|---|
| Frames comparados | 489 (1..489) | 489 (1..489) |
| `equal=0` (mismatches) | **0** | **0** |
| readOk | 489/489 | 489/489 |

**HECHO**: 978 comparaciones frame-completo, **cero** divergencias
host↔guest. SHA256 offline de los pares volcados:

| Frame | host_strips == guest_strips (SHA256) |
|---|---|
| 1 | `00a0a599c50b2769…` idéntico |
| 250 | `2304b36efabb5726…` (prefijo) idéntico |
| 480 | `7f4e3a7859d20fa1…` (prefijo) idéntico |

**Ventana corrupta cubierta**: los screenshots corruptos de RUN_094/095 caen
en t≈80-140 s global con el movie arrancando a ≈70 s; los 489 frames servidos
cubren t≈72-132 s — antes, durante y después de la primera ventana corrupta.
Además el frame 480 decodificado (título "Devil May Cry" con Dante,
`analysis/local/q6/q6_frame_480.png`) es visualmente PERFECTO — el mismo tipo
de composición de título que `state_07` de RUN_095 muestra masacrada con el
peine de rectángulos negros.

## Fase 6 — Cross-check canónico externo (FFmpeg CLI 9.0)

Referencias generadas de `canonical_video.es` con `-sws_flags bilinear`
(replica el `SWS_BILINEAR` del runtime), frames por índice de salida n=0,
249, 479 (`select`, `-fps_mode passthrough`).

Alineación: `pts90k=−1` en runtime (m2v sin PTS) ⇒ alineación por PTS
imposible; se usa **orden de salida** — justificado porque (a) el ES
alimentado al decoder runtime es byte-idéntico al canónico (HECHO P4.2.3/
P4.2.4: capturas consumed = prefijo canónico exacto), (b) mismo libavcodec
determinista ⇒ misma secuencia de salida, y (c) la identidad byte-exacta
resultante es en sí la prueba más fuerte de alineación (un desalineamiento de
±1 frame no puede producir 917.504 bytes idénticos en vídeo en movimiento).

| Frame (runtime idx / CLI n) | runtime host linear RGBA vs CLI |
|---|---|
| 1 / 0 | **IDÉNTICO** (`cmp` byte a byte) |
| 250 / 249 | **IDÉNTICO** |
| 480 / 479 | **IDÉNTICO** |

**HECHO**: el decoder runtime (decode + conversión RGBA) produce bytes
idénticos al FFmpeg canónico offline.

## Decisión (árbol Fase 11): CASO A

- `HOST == GUEST` en todos los frames medidos, incluida la ventana corrupta ⇒
  **decoder→guest copy/layout EXONERADO**.
- `runtime HOST == FFmpeg externo` en los 3 frames muestreados ⇒
  **salida del decoder EXONERADA**.
- La clasificación visual de P4.2.4-FIRST-DIVERGENCE queda reforzada: la
  corrupción nace DESPUÉS de guest imageAddr. Primera frontera UNKNOWN
  restante: **Movie_loadimage → GIF IMAGE (32 strips) → GS upload →
  MoveImage local → FRAME/ZBUF → DISPFB/presentación**.
- Nota: Q6 NO afirma que "todo el path MPEG es visualmente correcto" — solo
  exonera hasta imageAddr inclusive.

## Fase 13 — Restauración (HECHO)

- Instrumentación Q6 retirada por completo (0 ocurrencias de `Q6/q6` en
  MPEG.cpp).
- `git -C vendor/PS2Recomp diff` post-retirada: SHA256
  `6771b381…` — **byte-idéntico** al backup `vendor_before_q6.patch`.
- Ejecutable recompilado (`build_one.ps1 -Mpeg`) sobre el estado restaurado.
- Ningún cambio semántico de producción en ningún momento (oracle 100 %
  opt-in y ya retirado).

Artefactos (untracked, `analysis/local/q6/`): vendor_before_q6.patch,
q6_frame_{1,250,480}_{host_strips,guest_strips,host_linear_rgba}.bin,
cli_frame_{1,250,480}_rgba.bin, q6_frame_{1,250,480}.png; logs en
scratchpad p424_manual_q6run{,2}/runtime.log.

## Ledger

- HECHO (nuevo): layout guest = 32 strips verticales de 16px × 448 filas,
  alpha 0x80.
- HECHO (nuevo): decoder→guest byte-idéntico (978/978 frames).
- HECHO (nuevo): decoder runtime == FFmpeg canónico externo (3/3 frames).
- UNKNOWN restante: primera etapa corrupta dentro de
  Movie_loadimage/GIF/GS/presentación.
- Sin RETRACTADOs: la clasificación GS de la fase anterior queda reforzada,
  y su rama "layout/copia guest" queda formalmente cerrada (exonerada).
