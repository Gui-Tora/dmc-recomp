# PERF_003: incidencias del GS pendientes tras el parche 32

Abiertas el 2026-10-30, al integrar el parche 32 (`PERF_003_gs_zte_depth_test_enable.patch`). Ninguna se corrige
en esa integración. ELF: `SLES_503.58`, SHA-256 `d0753a6b…bab4`, CRC32 `77654AD2`, entry `0x00100008`.

Evidencia local: `analysis/local/perf003/e3a/`, que no está en Git. Contexto:
`PERF_003_ARCHITECTURE_FEASIBILITY.md`, §15.

Estado a 2026-11-06:
- GS-4 está corregido visualmente con el parche 34 (`PERF_003_ARCHITECTURE_FEASIBILITY.md`, §27.7).
- GS-1, GS-2, GS-3 y GS-5 siguen abiertas y son independientes de GS-4.

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
- **UNKNOWN (estado anterior, 2026-10-29):** la causa; podía venir del VU1, del GIF o del rasterizador.
- **Actualización 2026-10-31 (diagnóstico con GS Replay, sin ejecutar el juego):** ver
  `PERF_003_ARCHITECTURE_FEASIBILITY.md`, §24. Resumen:
  - **HECHO:** el triángulo pertenece a la malla de la cabeza de Dante (textura T8 `TBP0=10752`, registros
    9591–14271 de `cap2`, 1 641 triángulos, 935 posiciones de vértice distintas). 52 de sus vértices llegan al GS agrupados en un
    área de 4×28 píxeles en (21–25, 236–264), con `q` 2,4 veces mayor que el resto, y 3 más en (305, 485). Los 51
    triángulos de tira que unen esos vértices con la cabeza son las cuñas naranjas (registros 10181 y 10382, ~60 000
    píxeles² cada uno, frente a una mediana de 1,1 en la malla).
  - **HECHO:** el rasterizador dibuja exactamente los triángulos recibidos; el frontal respeta ADC; no es un fallo de
    rasterización, profundidad, recorte ni composición.
  - **INFERENCIA:** es un fallo del generador de geometría: un grupo de vértices de la cabeza se transforma con una
    matriz degenerada. Origen (EE/VU0, VIF/DMA o microcódigo VU1) **UNKNOWN**.
  - **Corrección (§25.2):** el racimo es compatible con una transformación distinta (giro y traslación), no
    necesariamente degenerada; el error es anterior a la proyección (`z/q` constante en los tres grupos).
- **Actualización 2026-10-31 (una corrida instrumentada VU1/VIF/DMA; §26):**
  - **HECHO:** la matriz y los índices de los vértices anómalos son los mismos que los de la cabeza sana. Lo que
    llega mal a la memoria VU1 es la posición.
  - **HECHO:** el VIF y la DMA transportan exactamente lo que hay en la RDRAM:
    - 0 diferencias en 203 740 QW;
    - 24 829 segmentos idénticos;
    - cadena de 9 773 etiquetas reproducida.
  - **HECHO:** el VU1 reproduce sus entradas: intérprete exacto en 2 772 de 2 772 llamadas y compilado igual al
    intérprete.
  - **HECHO:** las posiciones vienen de un búfer animado de la cara que escribe el EE. Sus valores corresponden a la
    traducción generada de `Morph_sub_h` (`0x1b2090`):
    - `PROT3W` rota las 4 palabras en lugar de 3;
    - `LWU` extiende el signo en lugar de extender con ceros.

    Comprobado con el propio C++ generado en un arnés offline; la semántica del R5900 deja intactas las 466 fuentes.
  - **INFERENCIA:** primera divergencia en el EE, en la traducción de `PROT3W`/`LWU`. Se refutan las hipótesis
    anteriores de matriz incorrecta, VIF/DMA y microcódigo VU1.
  - **Abierto:**
    - 145 entradas que no se explican con el modelo de delta 0;
    - el llamador;
    - la confirmación en PCSX2.
  - **Propuesta (no aplicada):** parche general del generador, con regeneración y validación.
- **Actualización 2026-11-05 (parche 34 integrado en local; `PERF_003_ARCHITECTURE_FEASIBILITY.md`, §27):**
  - **HECHO:** con la corrección de `PROT3W` y `LWU`, las cuñas desaparecen en las escenas observadas del castillo
    (27060, 27240, 27420); con el exe estable siguen apareciendo.
  - **HECHO:** el checkpoint del castillo contiene el búfer facial limpio. La hipótesis de un búfer dañado heredado
    queda refutada.
  - **HECHO:** el moteado de suelo y muro en 27240 es idéntico con y sin el parche; es previo.
  - **Abierto:** la observación directa del búfer facial en ejecución y la cobertura general de LWU.
  - Estado: **corregido en las escenas observadas**, pendiente de validar el exe integrado.
- **Actualización 2026-11-06 (exe integrado, §27.5):** HECHO: el exe integrado (`a1e08309…`) tiene el mismo código
  que el experimental validado (`1e845972…`); solo difieren las marcas de tiempo. Falta una corrida del exe
  integrado.
- **Actualización 2026-11-06 (P34_VAL_001, §27.6):** HECHO: en una corrida del exe integrado desde el checkpoint del
  castillo (714 capturas hasta el tick 27540) no aparece ninguna cuña, frente a 210 de 433 capturas de la corrida
  estable; el menú y la oficina no muestran regresiones.
  - Estado: **corregido** con el exe integrado en las escenas observadas. Abierto: la observación directa del
    búfer facial y la cobertura de LWU fuera de esas escenas.

## GS-5. Mar corrupto

- **HECHO:** la referencia s09 de PCSX2 muestra el mar a la izquierda; en las capturas de gameplay del runtime esa
  zona sale con bloques corruptos.
- **UNKNOWN:** la causa. No hay evidencia de relación con ZTE.
- **Siguiente paso:** fijar un fotograma equivalente en PCSX2 y en el runtime, y buscar la primera divergencia.
