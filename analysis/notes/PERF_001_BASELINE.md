# PERF_001 — Línea base de rendimiento y diagnóstico

Fecha: 2026-10-19.

- Rama `perf-001`, creada desde `master` `cc7e9ced0dadb853019bd0b867af682621a5174d`, sin commits.
- Lock de 27 parches, `patched_commit` `e58f58b9…`; el vendor es idéntico al replay.
- ELF `SLES_503.58`, SHA-256 `d0753a6b…bab4`, CRC32 `77654AD2`, entry `0x00100008`.

Categorías: HECHO (medido u observado), INFERENCIA, HIPÓTESIS, ESTIMACIÓN.

## 1. Benchmark y condiciones

**Referencia:** `dmc_checkpoint_mainmenu_abi2_tsnd_visual001_v1.dmck`, el checkpoint de VISUAL_001: menú principal,
tick 12904, SHA-256 `94b22929…3eeb`.

- **HECHO, validación estática:** el checkpoint guarda la RDRAM sin comprimir (desplazamiento `0xDA1`).
  - Las estructuras de escena (`0x740770`) y de película (`0x740C30`) contienen el `DISPLAY` del SDK
    (`0x1BF9FF02068290`) en sus 8 copias, y también lo contiene la copia de los registros GS.
  - No hay ningún valor del HLE antiguo. El checkpoint anterior (`…_tsnd_v1`) contiene solo valores antiguos.
- **HECHO, validación en ejecución:** `[CKPT:LOAD] SUCCESS`, con «executable build differs, checkpoint ABI
  compatible».
  - En la oficina, `PMODE` FF67, `SMODE2` 3, `DISPFB1` FBP 504 y `DISPLAY2` DX656 DY104 MAGH4 DW2559 DH447,
    iguales a PCSX2 s07.
- **INFERENCIA:** `g_gparam` no está en el checkpoint y queda en su valor por defecto (NTSC, campo). No afecta a
  la medida: DMC solo llama a `sceGsResetGraph`/`sceGsSetDefDBuffDc` desde `Main_init`, y los registros
  medidos son los correctos.

**Escena:** carga, CROSS (NEW GAME), película `0x232` (termina en el tick ~16264) y oficina. Ventana de medida:
**ticks emulados 16500–17000** (500 VSyncs, 10 s de tiempo PAL emulado).

- **Ejecutable de referencia sin sondas:** Release `f62d670f…` (`dmc-recomp-perf-base.exe`). Sus únicas medidas
  son las estadísticas permanentes del despachador VU1.
- **Configuración:** `PS2X_VU1_NATIVE=native`, FFmpeg ON, `PS2X_VU1_STATS_SEC=2`.

**Equivalencia entre corridas:**

- HECHO: en las cinco corridas, la carga de trabajo de la ventana es la misma: 1 056–1 063 llamadas VU1 por
  VSync, con un 14,6–14,8 % interpretadas.
- Las corridas solo difieren en la instrumentación.
- Ninguna supera los 5 minutos de ejecución del juego.

| Corrida | Instrumentación | VSync/s | ms de pared por VSync (media / mediana / p95)* |
|---|---|---|---|
| A | ninguna | **5,49** | **182,2** / 134,8 / 285,9 |
| B | muestreo 1 ms (PDB) | 4,95 | 201,9 / 154,3 / 333,4 |
| C | muestreo 1 ms y pilas crudas (mapa) | 4,87 | 205,5 / 154,2 / 333,4 |
| D | C y contadores de trabajo | 4,87 | 205,5 / 154,4 / 333,4 |
| E | contadores y muestreo de 20 ms | 5,22 | 191,6 / 144,3 / 285,8 |

\* La mediana y el p95 salen de las ventanas de 2 s del despachador, no de cada VSync, así que son aproximados.

- HECHO: el perfilador de 1 ms cuesta ~11 % (5,49 → 4,87 VSync/s).
- Los porcentajes del perfil se usan como reparto relativo; los milisegundos absolutos vienen de la corrida A.

**Tiempos:**

- *Tiempo emulado:* 500 VSyncs equivalen a 10 s de juego PAL.
- *Tiempo de pared:* 91 s, es decir 0,11× tiempo real.
- *Tiempo de CPU:* el hilo del juego consume el 97,7 % de un núcleo durante la ventana (CPU ≈ pared). El hilo de
  render, el 7,7 %, casi todo esperando. El resto de hilos, ≤ 3 %.
- **El proceso está limitado por un solo hilo.**

## 2. Perfil por subsistema (hilo del juego)

**Método:**

- Perfilador por muestreo dentro del proceso: cada 1 ms suspende cada hilo y desenrolla su pila
  (`RtlVirtualUnwind`, 32 marcos).
- Se simboliza fuera del proceso con el mapa del enlazador, que incluye las funciones estáticas y las lambdas.
- Cada muestra se atribuye a **un solo** subsistema, según el fichero objeto del primer marco del runtime,
  empezando por la hoja. Los marcos del CRT (memcpy, `lroundf`…) se cuentan en su llamante, así que no hay
  doble contabilidad.
- **Corrección de método:** el primer intento (corrida B) simbolizaba con el PDB de enlace. Los objetos Release
  no tienen `/Zi`, así que las funciones estáticas tomaban el nombre del símbolo público anterior (`sscanf` 7 %,
  `wcsrchr` 100 % inclusivo). Ese reparto se descartó; las cifras son de las corridas C y D.

| Subsistema (exclusivo) | % del hilo (C / D) | ms/VSync (ESTIMACIÓN, ×182 ms) |
|---|---|---|
| GS rasterizador por software | 46,3 / 46,7 | ~85 |
| ↳ triángulos por PATH1 (geometría VU1, XGKICK) | 23,7 | ~43 |
| ↳ sprites por DMA de la CPU (PATH2/3) | 22,1 | ~40 |
| VU1 intérprete (14,8 % de las llamadas) | 28,0 / 27,4 | ~50 |
| VU1 programa compilado | 18,5 / 19,0 | ~34 |
| DMA y memoria EE | 2,9 / 2,5 | ~5 |
| VIF1 | 2,0 / 2,1 | ~4 |
| Código EE recompilado del juego | 1,1 / 1,2 | ~2 |
| GIF / front end del GS | 0,6 | ~1 |
| Despachador VU1 (snapshots, paquetes retenidos) | 0,3 | <1 |
| Planificador EE | 0,2 | <1 |

**Contraste con los relojes del despachador** (corrida A, sin sondas; HECHO): por VSync, VU1 91,3 ms (compilado
más intérprete, que incluye el GS de las llamadas interpretadas), envío PATH1 de las llamadas compiladas 38,6 ms
y resto 52,3 ms. Es coherente con el perfil.

**Funciones más costosas (exclusivas, % del hilo):**

- **VU1 intérprete:** `commitReadyPipelines` 9,3; `run` 2,9; `normalizeOperand` 2,1; `calculatePairReadyCycle`
  2,1; `updateFmacFlags` 2,1; `calculateFmacExactResult` 1,8; `execUpper` 1,8.
- **VU1 compilado:** el cuerpo `vu1c_a0dbee2aeac4dafa` 7,0; las ayudas de exactitud FMAC `dsign` (CRT) 3,6,
  `exactFlags` 3,0, `nrm` 2,4 y `productSticky` 1,5.
- **GS:**
  - `WritePixel` 4,9; `DrawTriangle` 4,0; `ReadCT32` 6,3 (destino y textura).
  - `SampleTexture` y su lambda 7,0; `lerpChannel` 3,3, más `lroundf` 2,8.
  - `LookupCLUT` 1,6; `combineTexture` 2,3.
  - La indirección `std::function` de lectura y escritura de VRAM: 3,1.
- **DMA:** `unordered_set<DmaChainState>::emplace` (detección de ciclos del parche 22): 1,2–1,5.

**Presentación (VISUAL_001):** composición PCRTC, latch y subida de textura suman el **1,5 % del hilo de
render** (~0,08 s de CPU en 103 s). Ese hilo no está en el camino crítico. **El coste de VISUAL_001 es
despreciable.**

## 3. Trabajo por VSync (corrida E, contadores temporales)

| Medida | Por VSync |
|---|---|
| Llamadas VU1 | 1 056–1 063 (14,8 % interpretadas, fallback 0) |
| Triángulos | 11 933–11 952 |
| Sprites | 20 |
| Área recortada de los sprites | **1,28 M píxeles** (~64 k por sprite) |
| Llamadas a `WritePixel` | **1,63 M** (el buffer de 512×224 tiene 115 k: unas 14 veces de *overdraw*) |
| Muestras de textura | 690 k, todas bilineales; el 16,5 % con fracción cero |
| Tags DMA recorridos | 10 700 |
| Bytes VIF1 | 1,66 MB |

- **INFERENCIA:** los sprites aportan unos 1,28 M de los 1,63 M de píxeles escritos. Como solo hay 690 k
  muestras de textura en total, buena parte del área de sprites son rellenos sin textura, con mezcla o pruebas.

**Fases (HECHO, corrida A):**

- dentro de la ventana, los tramos con ~25 % de llamadas interpretadas van a ~3,5 VSync/s (VU1 ~170 ms);
- los tramos con ~8 % van a ~7,5 VSync/s (VU1 ~52 ms);
- el número total de llamadas es parecido en ambos casos.

## 4. Comparación con mediciones anteriores

- **GAMEPLAY_001J/001K** («83 % intérprete VU1», 12,5 VSync/s): **no comparable.**
  - Se midió con las cadenas DMA truncadas a 4 096 tags (corregido por el parche 22), así que no se dibujaba la
    mayor parte de cada fotograma.
  - El VU1 era 100 % interpretado (001J) o con las 4 entradas de entonces compiladas (001K).
- **GAMEPLAY_001L, cierre** (~5,1 VSync/s, build limpia): escena equivalente (oficina), pero con otro checkpoint,
  otra ventana y antes de VISUAL_001.
  - El orden de magnitud coincide con los 5,49 VSync/s de ahora. No se usa como comparación exacta.
- **001L, tabla con instrumentación activa** (3,4 VSync/s): no comparable, por la sobrecarga de sus sondas.

## 5. Los tres cuellos de botella principales

1. **Entradas VU1 sin compilar.**
   - HECHO: el despachador registra 16 entradas de la misma imagen `a0dbee2aeac4dafa` sin programa compilado:
     MSCAL 0xf00, 0x1b00, 0x2100 y 0x2700, y 12 continuaciones MSCNT.
   - Solo hay 4 compiladas.
   - El 14,8 % de las llamadas interpretadas cuesta el **27–28 % del hilo**; el intérprete es del orden de 10
     veces más lento por llamada.
2. **Sprites por DMA de la CPU.** 20 sprites, 1,28 M píxeles por VSync y el **22 % del hilo**.
   - Cada píxel pasa por `WritePixel`, que vuelve a decodificar los registros del contexto (scissor, test,
     alpha, frame, zbuf) en cada llamada.
   - Además lee el destino para mezclar y aplica muestreo bilineal en coma flotante con `lroundf`.
3. **Texturizado de triángulos por PATH1** (**24 % del hilo**): `SampleTexture` bilineal con 4 lecturas por
   muestra.
   - Cada lectura pasa por `std::function` y por la conversión o la CLUT.
   - Cuatro `lerpChannel` en `float`, con `lroundf`.

Detrás, la exactitud FMAC del VU1 compilado (~10 %) y la detección de ciclos DMA (~1,3 %).

## 6. Propuesta de optimización priorizada (no aplicada)

### P1. Compilar las entradas VU1 que faltan

- **Cuello de botella:** §5.1.
- **Ficheros:**
  - `tools/vu1_replay/dmc_vu1_programs.json`: metadatos, sin bytes del juego;
  - `tools/vu1_replay/gen_vu1c.py`: soporte de `OPMULA` para las 3 entradas que hoy se rechazan;
  - salida local `recomp/vu1c/` (ignorada por Git).
  - No toca el vendor ni el código generado del EE.
- **Cambio:**
  - añadir las 16 entradas al manifiesto;
  - HECHO, prueba en seco del generador en un directorio temporal: acepta 13 de las 16; rechaza MSCNT 0x21c8,
    0x2668 y 0x26b0 por `upper OPMULA` en 0x2488;
  - después, añadir `OPMULA` al subconjunto del generador.
- **Ganancia (ESTIMACIÓN):**
  - si las entradas nuevas rinden como las de 001K (~10× frente al intérprete), el VU1 interpretado bajaría de
    ~50 a ~5–10 ms por VSync;
  - eso es ~135–140 ms por VSync, es decir ~7,2 VSync/s (+30 %);
  - lo respaldan los tramos medidos con pocas llamadas interpretadas (~7,5 VSync/s);
  - con solo las 13 entradas aceptadas hoy, la ganancia será menor, en una proporción todavía no medida.
- **Riesgo para la fidelidad:** bajo. El modo `PS2X_VU1_NATIVE=verify` ejecuta el intérprete como autoridad y
  compara cada campo de estado, los datos, cada paquete PATH1 y el delta de ciclos.
- **Pruebas:**
  - verify en la ventana de la oficina, con criterio 0 diferencias en el 100 % de las llamadas;
  - hash PATH1 (`PS2X_VU1_PATH1_HASH=1`) idéntico entre native e interpreter;
  - banco offline de `tools/vu1_replay` si se añaden capturas.
- **Coste:**
  - generación en segundos;
  - compilar una unidad (`vu1c_a0dbee2aeac4dafa.cpp`, ~950 KB) y reenlazar;
  - corrida corta de verify. `OPMULA`, aparte: poco código del generador más sus tests.

### P2. Camino rápido de sprites en el GS (parche general del runtime)

- **Cuello de botella:** §5.2.
- **Ficheros:** `gs/gs_cpu_backend.cpp` (`DrawSprite`, `WritePixel`), sin cabeceras compartidas.
- **Cambio:**
  - decodificar una sola vez por primitiva el estado que hoy `WritePixel` decodifica por píxel (scissor, ZTE/ZTST,
    ATE/ATST, ABE/ALPHA, FBMSK, PSM del frame y del Z);
  - recorrer el rectángulo por filas con la misma aritmética por píxel;
  - variante específica para TME=0 (relleno) y para los casos sin test Z ni alpha.
- **Ganancia (ESTIMACIÓN):** 30–50 % del coste de sprites, es decir 7–11 % del hilo (~13–20 ms por VSync).
- **Riesgo:** medio. Hay muchas combinaciones de estado.
  - Mitigación: el camino general sigue para todo lo que la variante rápida no cubra exactamente.
- **Pruebas:**
  - diferencial en `ps2x_tests`: lotes de sprites con estados aleatorios (PSM de frame y Z, tests, mezcla, FBMSK,
    scissor), dibujados por el camino viejo y el nuevo, con la VRAM comparada byte a byte;
  - en el juego, volcados de VRAM de la ventana idénticos a la línea base.
- **Coste:** una unidad del runtime, unos 3 min de compilación; tests de 10 s de compilación.

### P3. Muestreo de textura sin cambiar la aritmética

- **Cuello de botella:** §5.3, y la parte texturizada de §5.2.
- **Ficheros:** `gs/gs_cpu_backend.cpp` (`SampleTexture`, tabla `m_readVramFuncs`). La cabecera
  `gs_cpu_backend.h` solo la incluyen `gs_cpu_backend.cpp` y `gs_frontend.cpp`.
- **Cambios:**
  - sustituir la tabla de `std::function` por punteros a función, o resolver el lector una vez por primitiva;
  - aplicar el filtro bilineal con fracción cero como lectura puntual: exacto por construcción, porque
    `lerpChannel(c, …, 0, 0)` devuelve `c`; afecta al 16,5 % de las muestras;
  - no se toca `lroundf` ni la fórmula: sustituirla exige demostrar equivalencia para todo `fx`/`fy`.
- **Ganancia (ESTIMACIÓN):** 15–25 % del coste de muestreo (~25 % inclusivo), es decir 4–6 % del hilo.
- **Riesgo:** bajo, porque son cambios exactos por construcción.
- **Pruebas:** los tests GS existentes; un test exhaustivo de la fracción cero; muestras con PSM, CLUT y wrap
  aleatorios comparadas entre el camino viejo y el nuevo; VRAM de la ventana igual a la línea base.
- **Coste:** pequeño, una unidad.

**Orden recomendado:** P1 primero. Es la mayor ganancia, no toca el vendor y la fidelidad ya está cubierta por el
modo verify. Después P3, pequeño y exacto, y por último P2, que necesita el diferencial más amplio.

- **Pendiente menor:** sustituir la detección de ciclos del parche 22 por una sin `unordered_set`. Supone ~1,3 %;
  hay que conservar la detección exacta.
- **Medida pendiente:** por qué hay unas 14 veces de *overdraw* en sprites. Hay que identificar qué sprites son
  (capas a pantalla completa) antes de optimizar P2.

## 7. Riesgos de fidelidad y pruebas de equivalencia

- Ninguna propuesta reduce la precisión de VU0/VU1, descarta geometría, limita cadenas DMA ni omite efectos.
- **Criterio común:** igualdad exacta frente a la implementación actual, nunca «parecido visual».
  - VU1: verify con 0 diferencias.
  - GS: VRAM byte a byte en tests y en volcados de la ventana.
  - Siempre: hash PATH1 y `ps2x_tests` sin fallos nuevos.
- **Benchmark para cada cambio:** el mismo checkpoint y la misma ventana (ticks 16500–17000), el ejecutable sin
  sondas y las estadísticas del despachador. Comparar solo corridas con la misma carga (llamadas VU1 por VSync y
  la proporción interpretada).

## Instrumentación temporal (retirada)

Toda la instrumentación ya está retirada.

- **Perfilador** local `perf001_profiler.inc` (nunca formó parte del repositorio) y su gancho en `dmc_overrides.cpp`: retirados.
- **Contadores temporales** en `gs_cpu_backend.cpp`, `ps2_memory.cpp` y `ps2_vif1_interpreter.cpp`: revertidos.
- **Estado tras retirarla:**
  - el vendor es idéntico al replay de 27 parches;
  - el ejecutable de trabajo se recompiló limpio (`2bc20787…`, sin cadenas del perfilador).
- **Evidencia local:** `analysis/local/perf001/` contiene las corridas A–E, los perfiles, las pilas crudas, los
  mapas del enlazador, `analyze_stacks.py`, `window_stats.py`, `bench.sh` y `retired_instrumentation/`.

## 8. P1-A: 13 entradas VU1 compiladas (en curso, detenida antes del benchmark)

**Cambio:**

- `tools/vu1_replay/dmc_vu1_programs.json`: 13 entradas más de la imagen `a0dbee2aeac4dafa`.
  - MSCAL 0x0f00, 0x1b00, 0x2100 y 0x2700.
  - MSCNT 0x0fc0, 0x13a8, 0x13f0, 0x1bc0, 0x2098, 0x20e0, 0x27c8, 0x2bc8 y 0x2c10.
- El generador no cambia: con el manifiesto anterior reproduce byte a byte el fichero activo previo
  (`9605bf4d…`).
- Salida local `recomp/vu1c/vu1c_a0dbee2aeac4dafa.cpp` (`337e2849…`, ignorada por Git). Se recompiló solo esa
  unidad (ejecutable `9dc7f50d…`).
- Siguen sin compilar MSCNT 0x21c8, 0x2668 y 0x26b0: el generador rechaza su código por `OPMULA` en 0x2488.

**Verificación** (`PS2X_VU1_NATIVE=verify`, una corrida de 5 min desde el checkpoint de VISUAL_001):

- HECHO: 322 813 llamadas.
  - 322 589 comparadas, con **0 diferencias** de estado, datos, PATH1 y ciclos; 0 omitidas y 0 rechazadas.
  - Las 224 restantes son las 3 entradas sin programa.
- Llamadas comparadas por entrada nueva:

| Entrada | Llamadas comparadas |
|---|---|
| MSCNT 0x13f0 | 27 694 |
| MSCNT 0x13a8 | 10 959 |
| MSCNT 0x2bc8 | 7 200 |
| MSCAL 0x0f00 | 5 975 |
| MSCNT 0x0fc0 | 5 975 |
| MSCAL 0x2700 | 4 950 |
| MSCNT 0x27c8 | 4 950 |
| MSCNT 0x2098 | 4 725 |
| MSCAL 0x1b00 | 225 |
| MSCNT 0x1bc0 | 225 |
| MSCAL 0x2100 | 48 |

- **No ejecutadas, por tanto NO verificadas:** MSCNT 0x20e0 y 0x2c10. En la línea base aparecen por primera vez
  en los VSync 16990 y 16645.
- **Bloqueo:** verify avanza a ~1,15 VSync/s y solo cubrió la oficina 16264–16541. La ventana del benchmark
  (16500–17000) necesitaría ~11 min.
  - No se ejecutó la corrida de rendimiento.
- **Peso en la oficina cubierta** (ciclos VU1, aproximación del coste): las 11 entradas nuevas verificadas suman
  el 19,5 %; las 3 entradas `OPMULA`, el 0,07 %.
- `ps2x_tests`: 458 tests y 24 fallos, los mismos preexistentes.

### 8.1 Opción B: 11 entradas verificadas activas (2026-10-20)

**Cambio:**

- MSCNT 0x20e0 y 0x2c10 salen de `entries` y pasan a `deferred_entries` del manifiesto, con su nota.
  `make_vu1c.py` solo lee `entries`.
- HECHO: el código regenerado (`f56bd84b…`) difiere del verificado de 13 entradas solo en:
  - los casos, las etiquetas y los bloques exclusivos de 0x20e0 y 0x2c10, y sus líneas del registro (17 → 15);
  - dos `return vu1c::Fault;` tras las ranuras de retardo del bit E en 0x20d8 y 0x2c08.
- Ninguna de esas dos direcciones es destino de salto, y el bloque previo siempre sale por `L_END`, así que los
  `return` son inalcanzables. El código que ejecutan las 11 entradas verificadas es el mismo.
- La verificación de §8 sigue siendo válida, pero **cubrió los ticks 16264–16541, no toda la ventana del
  benchmark**.

**Rendimiento** (una corrida, sin sondas, `native`, mismo checkpoint y misma ventana 16500–17000; ejecutable
`d1ebe1c1…`):

| | Línea base A | Opción B |
|---|---|---|
| VSync/s | 5,49 | **4,61** (−16 %) |
| ms de pared por VSync | 182,2 | 216,8 |
| Llamadas VU1 por VSync | 1 056 | 1 060 |
| Llamadas interpretadas | 14,6 % | **2,0 %** |
| VU1 (compilado más intérprete), ms por VSync | 91,3 | 116,5 |
| Envío PATH1 de llamadas compiladas, ms por VSync | 38,6 | 46,2 |
| Resto, ms por VSync | 52,3 | 54,2 |

- HECHO: fallback 0 (budget, fault, precondition y unsupported a 0); `chainBlocked` 46; `primes` 4 080; sin
  errores fatales ni de DMA.
- HECHO: en los tramos que en la línea base eran mayoritariamente compilados (~9 % interpretado, VU1 ~52 ms), el
  VU1 pasa a ~95–110 ms. Las entradas antiguas también se han vuelto más lentas.

**Causa** (HECHO, estático):

- `gen_vu1c.py` emite todas las entradas de una imagen en una sola función.
- Con 4 entradas, `vu1c_a0dbee2aeac4dafa` ocupa 31 264 bytes de código máquina (mapa de la corrida C).
- Con 15 entradas ocupa 989 264 bytes.
- Recompilada con `/w14883`, MSVC emite **C4883: «el tamaño de la función suprime las optimizaciones»**. La
  función entera, incluidas las entradas antiguas, se compila sin optimizar.

**Estado:** P1-A, tal como se generó, **empeora** el rendimiento. La fidelidad no está en duda: la semántica es
la verificada. El ejecutable activo contiene esta configuración, aún sin revertir.

**Siguiente paso propuesto** (pendiente de autorización; cualquiera de las dos opciones):

- **(a)** Compilar solo esa unidad con `/d2OptimizeHugeFunctions` (opción de MSVC para optimizar funciones
  grandes), desde el `CMakeLists.txt` del proyecto, sin tocar el vendor. Puede alargar la compilación de la
  unidad.
- **(b)** Cambiar la emisión del generador a una función por entrada, o por grupo de entradas que compartan
  código, sin cambiar la semántica.
  - Requiere verificar que el código emitido por entrada es equivalente.
  - Después, repetir verify y medir.

### 8.2 Opción A: `/d2OptimizeHugeFunctions` en la unidad VU1 generada (2026-10-20)

**Cambio:** `CMakeLists.txt` del proyecto aplica `/d2OptimizeHugeFunctions` solo a `recomp/vu1c/*.cpp`, solo en
Release y solo con MSVC. No cambian el generador, el manifiesto (11 entradas nuevas activas y 2 en
`deferred_entries`), el vendor ni la lógica VU1.

**Compilación:**

- HECHO: MSVC 19.44.35219 acepta la opción. En un fichero mínimo compila sin aviso, mientras que una opción
  `/d2` inventada da el error fatal C1007 («marca irreconocible en p2»).
- HECHO: el flag llega a la unidad. El `.vcxproj` generado lleva
  `<AdditionalOptions Condition="…Release|x64…">… /d2OptimizeHugeFunctions` solo en `vu1c_a0dbee2aeac4dafa.cpp`.
- HECHO: con `/w14883` ya no aparece C4883.
- Solo se recompiló esa unidad y se reenlazó: 2,6 min en total y pico de ~1,5 GB en `cl`.
- HECHO, tamaño del código máquina de `vu1c_a0dbee2aeac4dafa` (mapas del enlazador):

| Configuración | Bytes de código máquina |
|---|---|
| 4 entradas | 31 264 |
| 15 entradas, sin la opción | 989 264 |
| 15 entradas, con la opción | **226 800** |

**Rendimiento** (una corrida, sin sondas, `native`, mismo checkpoint y misma ventana 16500–17000; ejecutable
`906b5db6…`):

| | A: original (4 entradas) | B: 11 nuevas, sin optimizar | **C: 11 nuevas, `/d2OptimizeHugeFunctions`** |
|---|---|---|---|
| VSync/s | 5,49 | 4,61 | **6,42** (+17 % sobre A) |
| ms de pared por VSync (media / mediana / p95)* | 182,2 / 134,8 / 285,9 | 216,8 / 200,0 / 285,9 | **155,8** / 134,7 / 200,2 |
| Llamadas VU1 por VSync | 1 056 | 1 060 | 1 067 |
| Llamadas interpretadas | 14,6 % | 2,0 % | 1,9 % |
| VU1 (compilado más intérprete), ms por VSync | 91,3 | 116,5 | **55,5** |
| Envío PATH1 de llamadas compiladas, ms por VSync | 38,6 | 46,2 | 45,8 |
| Resto, ms por VSync | 52,3 | 54,2 | 54,5 |
| Fallbacks | 0 | 0 | 0 |

\* En ventanas de 2 s.

- **Lectura:** el VU1 baja 36 ms por VSync. Unos 7 ms de GS pasan de la cuenta «VU1» (las llamadas interpretadas
  incluían su PATH1) a «envío PATH1». El ahorro neto es de 26 ms por VSync.
  - La estimación previa (~7,2 VSync/s) era optimista: el GS por software (PATH1 más sprites, ~100 ms por VSync)
    es ahora el coste dominante.
- **Fidelidad:** esto es una mejora de rendimiento, **no una validación**.
  - El binario C (otra optimización de la misma fuente) todavía tiene que pasar `PS2X_VU1_NATIVE=verify` antes de
    integrarse.
  - La verificación anterior (§8) se hizo con el binario no optimizado y cubrió los ticks 16264–16541.
  - 0x20e0 y 0x2c10 siguen sin verificar y fuera del conjunto compilado.
- `ps2x_tests`: sin cambios (no se tocan fuentes del runtime ni de los tests).

### 8.3 Verificación del ejecutable optimizado (2026-10-20)

**Corrida:**

- Ejecutable `906b5db6…`, el mismo de la medida de 6,42 VSync/s, sin recompilar.
- `PS2X_VU1_NATIVE=verify`, `PS2X_VU1_VERIFY_STRICT=1` (se detiene en la primera divergencia), traza por
  llamada y hash PATH1.
- Una corrida desde el checkpoint de VISUAL_001, con tope de 5 min. La traza llega al VSync 16550: cubre el
  tramo 16264–16541.

**Resultados:**

- HECHO: 341 786 llamadas; **341 562 comparadas con 0 diferencias** (vf/vi, ACC, Q/P/I/R, MAC/STATUS/CLIP, pc,
  bits E/D/T, memoria de datos, paquetes PATH1 y delta de ciclos).
  - 0 omitidas, 0 rechazadas; el modo estricto no se activó.
  - Las 224 restantes son de las 3 entradas sin programa (`OPMULA`).
- **Llamadas comparadas por entrada nueva:**

| Entrada | Llamadas comparadas |
|---|---|
| MSCNT 0x13f0 | 29 280 |
| MSCNT 0x13a8 | 11 206 |
| MSCNT 0x2bc8 | 7 616 |
| MSCAL 0x0f00 | 6 274 |
| MSCNT 0x0fc0 | 6 274 |
| MSCAL 0x2700 | 5 236 |
| MSCNT 0x27c8 | 5 236 |
| MSCNT 0x2098 | 4 998 |
| MSCAL 0x1b00 | 238 |
| MSCNT 0x1bc0 | 238 |
| MSCAL 0x2100 | 48 |

- **Sin verificar:** MSCNT 0x20e0 y 0x2c10 no se ejecutaron; además no están en el conjunto compilado y siguen
  en el intérprete.
- **Fallbacks:** la corrida native de §8.2 (la de 6,42 VSync/s, sin sondas) tuvo 0 en budget, fault,
  precondition y unsupported.

**Conclusión:**

- **HECHO:** el ejecutable optimizado conserva, en el tramo 16264–16541, la equivalencia exacta con el
  intérprete para las 4 entradas originales y las 11 nuevas.
- Los 6,42 VSync/s proceden de la corrida native sin sondas de §8.2.

**Alcance del flag:**

- `/d2OptimizeHugeFunctions` es una opción interna, no documentada, de MSVC, comprobada con cl 19.44.35219 (VS
  2022, toolset 14.44.35207).
- Se aplica solo a `recomp/vu1c/*.cpp`, solo con `$<CONFIG:Release>` y solo si `MSVC`.
- En el `.vcxproj` generado aparece una única vez, condicionada a `Release|x64`.
- Si una versión futura de MSVC la retirara, aparecería el error C1007 (comprobado con una opción `/d2`
  inventada); la alternativa es dividir la función en el generador.

## 9. Cierre de P1-A (2026-10-20)

**Integrado:**

- `tools/vu1_replay/dmc_vu1_programs.json`: 11 entradas nuevas, todas verificadas, además de las 4 originales.
  0x20e0 y 0x2c10 quedan en `deferred_entries`.
- `CMakeLists.txt`: `/d2OptimizeHugeFunctions` solo para `recomp/vu1c/*.cpp`, con MSVC y en Release.
- El código VU1 se sigue generando en local desde el ELF del usuario (`recomp/vu1c/`, ignorado por Git). Con este
  manifiesto, `make_vu1c.py` reproduce byte a byte el fichero medido y verificado (`f56bd84b…`).

**Rendimiento** (oficina, ticks 16500–17000, `native`, sin sondas):

| Estado | VSync/s |
|---|---|
| Original | 5,49 |
| 11 entradas nuevas sin optimizar la función | 4,61 (C4883) |
| 11 entradas nuevas con la opción | **6,42 (+17 %)** |

- La estimación previa de ~7,2 VSync/s (§6) no se alcanzó: el coste dominante pasa a ser el GS por software.

**Verificación exacta:**

- Con el binario medido, 341 562 llamadas comparadas y 0 diferencias en el tramo 16264–16541 (§8.3).
- No se verificó toda la ventana 16500–17000.

**Pendiente:**

- MSCNT 0x20e0 y 0x2c10: aplazadas, sin verificar, interpretadas.
- MSCNT 0x21c8, 0x2668 y 0x26b0: necesitan `OPMULA` en el generador; interpretadas (0,07 % de los ciclos VU1).
- Dependencia de MSVC 19.44 (toolset 14.44.35207) y de la opción interna `/d2OptimizeHugeFunctions`. Si
  desapareciera, la alternativa es dividir la función en el generador.
- Siguiente cuello de botella: el GS por software (P2/P3 de §6), no iniciado.

**`FIRST_PLAYABLE_FRAME` sigue pendiente. DMC-Recomp todavía no es jugable.**

## 10. P3: atajo de fracción cero en el muestreo bilineal (2026-10-20, parche 28)

**Cambio:** en `SampleTexture` (`gs_cpu_backend.cpp`), si `fx == 0 && fy == 0` se devuelve `samplePoint(u0, v0)`
sin leer los otros tres texels ni interpolar. No cambian las coordenadas, el wrap/clamp, PSM/CLUT/TEXA, la
fórmula bilineal, `lroundf` ni la tabla `std::function`.

**Equivalencia** (HECHO, análisis del código):

- Con pesos exactamente 0, `lerpChannel` calcula `top = c00 + (c10 − c00)·0 = c00` y `c00 + (bottom − top)·0 =
  c00`, para `c` en 0..255 y diferencias finitas. Lo mismo vale si el compilador contrae a FMA, y el cero con
  signo no altera la suma.
- `lround(c00) = c00` y `clampU8` no lo modifica. Los 4 bytes resultantes son los de `c00`.
- `samplePoint` (wrap, `ReadVramUnlocked`, `LookupCLUT`, `applyTexa`, 5551→8888) solo lee, así que omitir tres
  llamadas no tiene efectos.
- Las fracciones NaN o infinitas no son iguales a 0 y siguen el camino general.
- **Casos que evita:** las muestras centradas en el texel. En la oficina son el 16,5 % de las muestras
  bilineales (114 k por VSync, contador de §3), sobre todo en sprites con UV 1:1.

**Tests** (`ps2x_tests`):

- Corpus diferencial de 168 casos:
  - 6 PSM (CT32, CT24, CT16, CT16S, T8 y T4, con CLUT CT32 o CT16);
  - 4 modos de clamp;
  - sprites con fracción cero, casi cero y grande, con origen interior y cruzando el borde;
  - TEXA, TFX y mezcla aleatorios, y triángulos STQ.
  - Los hashes de VRAM se registraron con el runtime **anterior** al cambio y se exige igualdad exacta.
  - 148 de los 168 marcos son distintos entre sí; hay 20 casos cuya salida no depende de la textura (limitación
    del corpus).
- Test semántico: el bilineal en el centro del texel da exactamente el mismo marco que el muestreo puntual (4
  PSM × 4 modos de clamp).
- **Ambos pasan antes y después del cambio.**
- Prueba de mutación (devolver `u1` en vez de `u0`): ambos fallan. El cambio se revirtió de inmediato.
- Suite: 460 tests, los mismos 24 fallos preexistentes.

**Rendimiento** (una corrida, sin sondas, `native`, FFmpeg ON, mismo checkpoint y misma ventana; ejecutable
`33e289ba…`):

| | C: P1-A (6,42) | P3 |
|---|---|---|
| VSync/s | 6,42 | **6,76** (+5,3 %) |
| ms de pared por VSync | 155,8 | **147,9** (−7,9) |
| VU1, ms por VSync | 55,5 | 55,4 |
| Envío PATH1, ms por VSync | 45,8 | 44,8 |
| Resto (incluye sprites por DMA), ms por VSync | 54,5 | **47,6** |
| Llamadas VU1 por VSync / interpretadas | 1 067 / 1,9 % | 1 058 / 2,0 % |

- INFERENCIA: la mejora no se explica por ruido.
  - El componente que el cambio no toca (VU1) varía un 0,2 %.
  - La bajada se concentra en el resto, donde están los sprites con UV 1:1.
  - Su tamaño cuadra con unas 114 k muestras por VSync que se ahorran 3 lecturas y 4 interpolaciones cada una.
- Solo hay una corrida por configuración, así que no se ha medido la dispersión entre corridas.

**Integración:**

- Parche 28 `patches/PERF_001_gs_bilinear_zero_fraction.patch` (sha256 `6bbc8a12…41c3`).
- Lock: 28 parches, `patched_commit` `e3d4250d793372be74a2bcd65363810d577aa00d`. El replay coincide con el lock y
  el vendor es idéntico; los parches 1–27 no cambian.
