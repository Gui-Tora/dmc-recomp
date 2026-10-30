# PERF_003: incidencias del GS pendientes tras el parche 32

Abiertas el 2026-10-30, al integrar el parche 32 (`PERF_003_gs_zte_depth_test_enable.patch`). Ninguna se corrige
en esa integración. ELF: `SLES_503.58`, SHA-256 `d0753a6b…bab4`, CRC32 `77654AD2`, entry `0x00100008`.

Evidencia local: `analysis/local/perf003/e3a/`, que no está en Git. Contexto:
`PERF_003_ARCHITECTURE_FEASIBILITY.md`, §15.

## GS-1. Recorte de la profundidad al máximo del formato (Z16 y Z24)

- **HECHO:**
  - PCSX2 limita la Z de cada vértice al máximo del formato del Z-buffer: `0xFFFFFFFF >> (fmt·8)`, es decir,
    0xFFFF en Z16 y Z16S y 0xFFFFFF en Z24 (`GSRendererSW.cpp`, `sel.zclamp`; `GSDrawScanline.cpp`, `min_u32`).
  - La sonda `depth_is_clamped_to_the_z_format_maximum` falla en el runtime con el parche 32: en Z16 la Z 0x12345
    no se guarda como 0xFFFF, y en Z24 la Z 0x01234567 no se guarda como 0xFFFFFF.
- **INFERENCIA:** `WritePixel` compara la Z completa con la almacenada y el escritor del formato guarda solo los
  bits bajos (0x12345 queda como 0x2345). Un valor fuera de rango puede pasar el test y escribir una Z pequeña.
- **UNKNOWN:** si DMC dibuja con Z fuera de rango y ZTE=1. En los 16 lotes con ZTE=0 de `cap2` la Z vale 2^32,
  pero no se usa.
- **Siguiente paso:** censar en `cap2` los lotes con ZTE=1 cuya Z supere el máximo del formato antes de corregir.
- Sondas (GS-1 y GS-2): `analysis/tools/perf003_gs_probes/PERF_003_gs_independent_probes.diag.patch`, suite
  `PS2GSDepthIndependentProbes`. Es un parche diagnóstico fuera del pipeline que se aplica sobre los 32 parches;
  ver su `README.md`.

## GS-2. DATE con framebuffer de 24 bits

- **HECHO:**
  - PCSX2 descarta el dibujo entero si FRAME.PSM es CT24 y TEST.DATE=1 (`GSRendererSW.cpp`, líneas 1076–1083).
    Según su comentario, se comprobó en una PS2: «nothing passes, it ignores the draw».
  - El runtime trata DATE como superado en CT24 (`passesDestinationAlphaTest`).
  - La sonda `date_on_a_24bit_frame_skips_the_draw` falla.
- **UNKNOWN:** si DMC usa DATE con CT24. Ninguno de los 16 lotes con ZTE=0 lo usa.
- **Siguiente paso:** censar `TEST.DATE` por formato de FRAME en `cap2`.
- Sonda: la misma del parche diagnóstico de GS-1 (`date_on_a_24bit_frame_skips_the_draw`).

## GS-3. Validación visual frente a PCSX2

- **HECHO:**
  - La única corrida del juego con la corrección (2026-10-29) produjo capturas blancas.
  - En GS Replay, la imagen presentada es idéntica con y sin la corrección en la ventana `cap2`.
- **INFERENCIA:** fallo de `PrintWindow`, no de la imagen del juego.
- **Pendiente:** comparar una escena en la que el oscurecimiento sustractivo valga distinto de 0 (por ejemplo, un
  fundido) con PCSX2, usando el mismo ELF. Requiere autorizar otra ejecución del juego.

## GS-4. Triángulo naranja delante de Dante

- **HECHO:** en la imagen reproducida de `cap2` (VSync ~27101) aparece un gran triángulo naranja plano delante de
  Dante. Es igual con la semántica de ZTE antigua y con la corregida.
- **UNKNOWN:** la causa; puede venir del VU1, del GIF o del rasterizador. Sin investigar.
- **Siguiente paso:** localizar con GS Replay el lote que lo dibuja y su origen.

## GS-5. Mar corrupto

- **HECHO:** la referencia s09 de PCSX2 muestra el mar a la izquierda; en las capturas de gameplay del runtime esa
  zona sale con bloques corruptos.
- **UNKNOWN:** la causa. No hay evidencia de relación con ZTE.
- **Siguiente paso:** fijar un fotograma equivalente en PCSX2 y en el runtime, y buscar la primera divergencia.
