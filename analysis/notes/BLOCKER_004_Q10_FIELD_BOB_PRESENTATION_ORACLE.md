# BLOCKER_004 — Q10: FIELD/BOB PRESENTATION ORACLE

Agente: FABLE — Fecha: 2026-09-23
Modo: diagnóstico temporal (opt-in `DMC_Q10_TRACE=1`), retirado al final.
NO_COMMIT / NO_PUSH.

Backup pre-Q10: `analysis/local/q10/vendor_before_q10.patch`, SHA256
`5abc3b0798e08eb7b85f9c582b0565cc836b005b9562dcb52b098d9cbf16feca`
(MPEG.cpp P4.2.3+P4.2.4 + Pad.cpp INPUT_001). Restauración final verificada:
diff post-Q10 == backup byte a byte; ejecutable recompilado.

Instrumentación: 1 hook en `GS::latchHostPresentationFrame`
(gs_frontend.cpp): por present, estado completo SMODE2/DISPFB2/DISPLAY2 +
paridad + `topRow` = primera fila de salida con contenido no-negro (feature
medido, CHECK 3). Run: manual, movie+30 s, 360 presents en FIELD mode.

## CHECK 1 — Estado GS por field (360 fields consecutivos del movie, HECHO)

Constantes en TODOS los fields, sin excepción:

| Registro | Valor | ¿Alterna por field? |
|---|---|---|
| SMODE2 | INT=1, FFMD=0 (FIELD mode) | NO |
| DISPFB2.DBX / **DBY** | 0 / **0** | **NO** |
| DISPLAY2 DX/DY | 636 / **64** | **NO** |
| DISPLAY2 MAGH/MAGV | 0 / 0 | NO |
| DISPLAY2 DW/DH | 511 / 447 (512×448) | NO |
| DISPFB2.FBP | alterna 0xA0/0x00 **cada 2 ticks** | double-buffer correcto, no por field |

Control pre-movie (UI): INT=1, FFMD=1 (FRAME mode) → `fieldMode=0`, sin bob,
sin temblor (consistente con el baseline SEVERE=NO y con que el jitter solo
se percibe en el movie).

HECHO: el guest NO produce ninguna alternancia vertical por field. Las
clasificaciones GS_STATE_FIELD_DIVERGENCE y
GS_DISPLAY_DY_INTERPRETATION_DIVERGENCE quedan descartadas.

## CHECK 2 — Presentador host (lectura de código, HECHO)

`PresentFromLocalMemory` (gs_cpu_backend.cpp): con `fieldMode =
INT && !FFMD` aplica `applyFieldPresentation(pixels, w, h, oddField)` donde
`oddField = vsyncTick & 1` (paridad SINTÉTICA del host, ni siquiera el bit
FIELD del CSR). El algoritmo:

```cpp
sourceY = ((y >> 1) << 1) + (oddField ? 1 : 0)
```

es un **bob line-doubling sin compensación de media línea**: el field par
duplica las filas pares del framebuffer, el impar duplica las impares — es
decir, el field impar presenta contenido que está físicamente 1 línea más
abajo, sin desplazar el destino. Es la ÚNICA operación de toda la cadena de
presentación que depende de la paridad.

## CHECK 3 — Medición del feature (HECHO)

`topRow` (primera fila de salida no-negra) durante el fade del warning:

- Con el borde del contenido en fila fuente IMPAR: alternancia exacta
  `odd→N, even→N+2` sostenida (48/50, 46/44←→46, 42/44…).
- Con el borde en fila PAR: estable (48/48, 44/44) — exactamente lo que
  predice el algoritmo (el bob solo desplaza el contenido cuyo borde cae en
  paridad opuesta al field).
- Misma alternancia con el MISMO framebuffer y el MISMO estado GS (p. ej.
  ticks 561 (odd, fbp 0xA0, topRow 48) → 562 (even, fbp 0xA0, topRow 50)).

**MEASURED_VERTICAL_OSCILLATION = 1 línea de contenido fuente = 2 filas de
salida, alternando con `vsyncTick & 1`.** El "canvas entero" tiembla porque
cada estructura de la imagen con bordes en fila impar salta ±1 línea cada
field a 50 Hz (percibido ~2 px por el escalado de ventana).

## Lado original (parcial — oráculo vivo NO obtenido)

Intenté el oráculo PCSX2 2.8.2 en vivo: `configure_gdb_settings` escribió
PCSX2.ini (GDB servers habilitados) y el servidor MCP de PCSX2 se desconectó
inmediatamente (mismo incidente que en P4.1.x); las tools quedaron
inaccesibles en esta sesión. PCSX2.ini queda con los GDB servers habilitados
(no es un cambio del repo). El proceso PCSX2 lanzado fue terminado.

Por tanto el patrón original se establece por semántica + estado medido, no
por captura en vivo (marcado como INFERENCIA fuerte):
- El estado GS que alimentaría a PCSX2/hardware es EL MISMO (lo genera el
  mismo guest y aquí está medido constante por field).
- En FIELD mode con DBY fijo, el GS real muestrea las MISMAS líneas cada
  field; el offset del field impar es de MEDIA línea y lo realiza el raster
  físico del CRT (no desplaza el contenido una línea entera). Los
  deinterlacers de PCSX2 (weave/blend/adaptativo) producen imagen estable
  para este caso.
- Coherente con la observación del usuario: el juego original no tiembla.

## Clasificación

**Q10_HOST_BOB_OFFSET_CAUSAL.** FIRST_DIVERGENCE =
`applyFieldPresentation()` en `GSCpuBackend::PresentFromLocalMemory`
(gs_cpu_backend.cpp): único punto paridad-dependiente; inyecta un offset de
contenido de 1 línea por field sobre un estado GS que no alterna.

Fix semántico obvio (NO implementado en Q10, per prompt): con FFMD=0 y un
framebuffer full-height ya conteniendo el frame completo, la presentación
host debe ser paridad-independiente (weave/pass-through: presentar las 448
líneas tal cual, o al menos compensar el offset con el desplazamiento de
destino de media línea). Eliminar/neutralizar el bob es un cambio de una
única función, trivial de validar visualmente con el usuario.

## Ledger

- HECHO: guest field-state constante (360 fields).
- HECHO: bob host = única fuente de paridad; oscilación medida 1 línea
  fuente / 2 filas salida sincronizada con `vsyncTick&1`.
- INFERENCIA (fuerte): el original con el mismo estado presenta estable.
- UNKNOWN: captura en vivo PCSX2 (MCP caído) — no bloquea la clasificación,
  el guest-state ya excluye las alternativas.
- Fuera de alcance respetado: performance (~36 % real-time) intacta para
  PERF_001.
