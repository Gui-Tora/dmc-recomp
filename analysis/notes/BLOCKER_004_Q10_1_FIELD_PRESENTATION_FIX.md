# BLOCKER_004 — Q10.1: corrección de la presentación FIELD

Fecha: 2026-09-23. Sin commit ni push. Baseline: informe Q10
`BLOCKER_004_Q10_FIELD_BOB_PRESENTATION_ORACLE.md`; no se repitió el oráculo
PCSX2. ELF de la ejecución: `original/SLES_503.58`, SHA-256
`d0753a6b3b2f00802a50758a872d8cf051725aa31c839aa8894eee30ce58bab4`.
El cambio es del presentador host y no introduce un binding al ELF.

## Alcance y corrección

- HECHO: antes de editar, `applyFieldPresentation` tenía solo dos llamadas,
  ambas en `GSCpuBackend::PresentFromLocalMemory`, una para dos circuitos
  compuestos y otra para un circuito. `GSCpuBackend::Present` toma un snapshot
  de VRAM y llama a esa misma función.
- HECHO: `decodeDisplaySize` da la anchura y altura activas de DISPLAY;
  `CopyFrameToHostRgba` recorre `y = 0 .. height-1` y reserva un búfer RGBA
  completo de 640×512. La rama de composición también recorre todas las
  filas activas y crea un búfer de altura `max(height1,height2)`. No hay
  entrada de medio campo en esta ruta ni otra llamada al helper de bob.
- HECHO: la condición anterior `INT && !FFMD` excluía la UI en FRAME mode.
  El cambio no altera la lectura de VRAM, las dimensiones, las direcciones
  de origen, el mezclado de circuitos ni los registros GS.
- HECHO: se retiraron `applyFieldPresentation`, su decodificador SMODE2
  usado solo para activar bob y las dos llamadas. Las filas del resultado
  completo pasan ahora a la salida sin remuestreo dependiente de paridad.
- INFERENCIA: en esta ruta full-height el bob anterior duplicaba y sustituía
  filas que ya existían. Su eliminación es la corrección semántica mínima;
  no implica haber demostrado la presentación exacta de PCSX2 en vivo.
- UNKNOWN: el oráculo visual/byte a byte de PCSX2 sigue sin capturarse.

## Build y ejecución

- HECHO: se imprimieron y comprobaron raíz del repo, raíz de build y ruta
  del ejecutable antes de cada build/run. Única raíz de build:
  `analysis/local/symtabfirst/build`. Target real: `ps2EntryRunner`;
  ejecutable: `analysis/local/symtabfirst/build/bin/Debug/dmc-recomp.exe`.
- HECHO: el primer intento `cmake --build ... --target dmc-recomp` indicó
  que no existe ese target; `dmc-recomp` es `OUTPUT_NAME`. El siguiente
  intento con `ps2EntryRunner` encontró una colisión `Path`/`PATH` en el
  entorno de MSBuild antes de compilar. Se normalizó únicamente el entorno
  del proceso y se usó el proyecto MSBuild generado por la misma raíz de
  CMake, con grafo de dependencias normal. Sin clean, regenerate, compilación
  manual de objetos ni flags mixtos.
- HECHO: el primer build del cambio pasó. Se añadió temporalmente una
  medida opt-in y limitada a 40 presents FIELD en `gs_frontend.cpp`; tras
  medir, se retiró íntegramente y se reconstruyó el ejecutable final
  (SHA-256 `7835d830ee1ff6e73e91013f820975bbf47404380e6ee3434887bad9c5b17b88`).
- RETRACTADO: la primera ejecución de validación (`RUN_087`) llegó al PSS,
  pero no produjo frames FIELD; omitía `DMC_P314_SYNC_REDECODE=1`, requerido
  por este baseline MPEG. No se usa como evidencia del arreglo.
- HECHO: `RUN_088`, mismo ejecutable instrumentado, ELF y disco, con
  `DMC_P314_SYNC_REDECODE=1`, aceptó START y CROSS, alcanzó PSS y produjo
  40 muestras FIELD (ticks 547–586), conservadas en
  `analysis/local/p311/RUN_088/runtime.log` (local, excluido de Git).
  El flujo prosiguió por el fin de la película y borró la sesión MPEG.
- HECHO: en ticks 549–586 `topRow=40` en **38/38** presents consecutivos,
  con paridad alterna. Ticks 547–548 dieron `topRow=42` en ambos campos;
  el cambio de 42 a 40 ocurrió al cambiar el contenido, sin patrón por
  paridad. FBP repitió `00,00,A0,A0`; DBY fue 0 y DISPLAY.DY 64 en todas
  las 40 muestras.
- INFERENCIA: la oscilación `N/N+2` de Q10 desapareció en la ventana
  medida; el cambio de contenido de fade no equivale al bob causal.
- UNKNOWN: las capturas de ventana del arnés quedaron tapadas por la UI
  anfitriona; no son evidencia válida para juzgar Memory Card, Language
  Select ni Main Menu. FPS bajo pertenece a PERF_001.
- HECHO (observación visual comunicada por el usuario): en una ejecución
  directa y visible del ejecutable final con el mismo SHA-256, el usuario
  informó `SEVERE_BANDS_OR_GLITCHES: NO`,
  `WHOLE_CANVAS_VERTICAL_JITTER: No o es tan poco que es imperceptible` y
  `MOVIE_IMAGE_STABLE: SI`. La segunda respuesta describe percepción visual,
  no una medición de desplazamiento exactamente cero; la medición objetiva
  de los 40 presents figura arriba.
- HECHO: después de la limpieza, el diff de `MPEG.cpp` y `Pad.cpp` volvió a
  tener exactamente el SHA-256 del backup pre-Q10.1; `gs_frontend.cpp` no
  tiene diff y `git diff --check` pasó. El ejecutable final se reconstruyó
  mediante el mismo proyecto MSBuild generado por CMake.

## Q10.1 RESULT — FIELD PRESENTATION FIX

PRE_FIX_BACKUP:
`analysis/local/q10_1/vendor_before_q10_1.patch`

PRE_FIX_BACKUP_SHA256:
`5abc3b0798e08eb7b85f9c582b0565cc836b005b9562dcb52b098d9cbf16feca`

FILES_CHANGED:
`vendor/PS2Recomp/ps2xRuntime/src/lib/gs/gs_cpu_backend.cpp`

CALLERS_OF_APPLY_FIELD_PRESENTATION:
Dos llamadas internas de `PresentFromLocalMemory` (composición CRT1/CRT2
y circuito único); ninguna otra.

FULL_HEIGHT_SOURCE_PROVEN:
YES — `CopyFrameToHostRgba` lee todas las filas de `height` y la composición
genera todas las filas hasta `max(height1,height2)`.

HALF_HEIGHT_PATH_PRESERVED:
NOT_APPLICABLE — no existe en esta ruta.

OLD_BEHAVIOR:
En FIELD, `sourceY=((y>>1)<<1)+(vsyncTick&1)`: duplicaba filas pares o
impares del framebuffer completo, cambiando la fuente una línea por tick.

NEW_BEHAVIOR:
La fila de salida `y` conserva la fila fuente `y` en ambas ramas.

GS_STATE_CHANGED:
NO

PARITY_SOURCE_USED_FOR_FULL_HEIGHT_PRESENTATION:
NONE

TOPROW_BEFORE:
Q10: con FBP y estado GS iguales, tick 561 = 48, tick 562 = 50;
patrón `N/N+2/N/N+2` para un borde impar.

TOPROW_AFTER:
Ticks 547–548 = 42/42; ticks 549–586 = 40 en 38/38 presents.

FBP_PATTERN_AFTER:
`00,00,A0,A0` repetido en los 40 presents.

DBY_AFTER:
0 en 40/40.

DISPLAY_DY_AFTER:
64 en 40/40.

SEVERE_BANDS_OR_GLITCHES:
NO — informado por el usuario.

WHOLE_CANVAS_VERTICAL_JITTER:
NO o imperceptible — formulación exacta del usuario: «No o es tan poco que
es impersectible».

MOVIE_IMAGE_STABLE:
YES — informado por el usuario («SI»).

MEMORY_CARD_REGRESSION:
UNKNOWN — la pantalla se alcanzó, pero la captura visual quedó tapada.

LANGUAGE_REGRESSION:
UNKNOWN — la entrada progresó al vídeo, pero la captura visual quedó tapada.

MAIN_MENU_REGRESSION:
UNKNOWN — el fin de película se observó por log; falta captura válida del menú.

TEMP_INSTRUMENTATION_REMOVED:
YES — `gs_frontend.cpp` sin diff y sin `DMC_Q101_TRACE`.

PRODUCTION_DIFF_MINIMAL:
YES — una fuente GS adicional respecto al backup; MPEG.cpp y Pad.cpp previos
conservados.

PRIMARY_CLASSIFICATION:
Q10_1_FIELD_PRESENTATION_FIXED — sin patrón de paridad en la medición y
película estable según el usuario; regresiones visuales de UI aún UNKNOWN.

SEMANTIC_FIX_VALIDATED:
YES — criterio objetivo de ausencia de desplazamiento por paridad, 40 presents.

NEXT:
PERF_001_RUNTIME_REALTIME_AND_PREMOVIE_LOAD.

CHECKPOINT_DECISION:
NO_COMMIT

PUSH:
NO
