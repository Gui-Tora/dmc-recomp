# PERF_002 — Línea base de rendimiento en el castillo

Fecha: 2026-10-29. Rama local `perf-002-castle`, creada desde `master` `314aaa70177e760f29b025cce1448e2f8ae05774`,
sin commits.

- Lock de 30 parches, `patched_commit` `ce2d053db5772642e756eacce4205ac38594894d`; el árbol del vendor
  (`fa1b4d7a…`) es idéntico al replay, también después de retirar la instrumentación.
- ELF `SLES_503.58`, SHA-256 `d0753a6b…bab4`, CRC32 `77654AD2`, entry `0x00100008`.

Categorías: HECHO (medido u observado), INFERENCIA, HIPÓTESIS, ESTIMACIÓN.

**Actualización (§12):** una corrida autorizada de hasta 9 min (corrida G) midió el **gameplay con Dante
controlable en reposo**: VSync 28050–28550, **4,62 VSync/s** (216,3 ms por VSync).

**Resumen inicial (corridas A y P).** En 5 minutos de pared el benchmark solo cubre la **cinemática de entrada al castillo** (VSync
26788–28024). El control llega hacia el VSync 28010, así que **no hay todavía ninguna medida de gameplay
controlable**. En esa cinemática el juego va a 4,31 VSync/s (3,87 en el tramo más pesado). El hilo del juego está
al 98 % de un núcleo. El GS por software consume el 55 % del hilo y el VU1, el 38 %. El parche 30 cuesta como
máximo el 0,22 %.

## 1. Configuración reproducible

| Elemento | Valor |
|---|---|
| Checkpoint | `analysis/local/gameplay_002/ckpt/g002_castle_entry.dmck`, SHA-256 `4c841943bf729db17d38999a6b782af16ac19481a9f0efbe123d45f1f8d8b6d2`, tick 26640 (no versionado) |
| Ejecutable de referencia | Release `d1e6a3bd09f0e50414757055edb55024df94641602d94d91fc3905f06d7c40f9` (estado de `master`: 30 parches, sin sondas G002/G003, log de stubs en potencias de 2), copiado como `dmc-recomp-perf002-base.exe` junto a sus DLL |
| Ejecutable perfilado | `ae29722e…`: el de referencia más el perfilador y los contadores de PERF_001, enlazado con `-MAP` |
| Build | `-DPS2X_ENABLE_FFMPEG=ON`, Release, `/d2OptimizeHugeFunctions` en `recomp/vu1c/*.cpp` |
| Entorno | el de `run_g002.py` (`PS2X_VU1_NATIVE=native`, arena `0x009FA000–0x00ABE000`, `DMC_P313_INIT_MODE=ownership`, `DMC_P314_SYNC_REDECODE=1`) más `PS2X_VU1_STATS_SEC=2` y `DMC_P311_PAD_FILE=` (vacía: sin canal P311) |
| Entradas del mando | ninguna (la cinemática no las necesita) |
| Guardados | ninguna petición de checkpoint |
| Resolución y gráficos | sin cambios |
| Duración | límite duro de 5 min por corrida |
| Guion | `analysis/local/perf002/bench_castle.sh OUT EXE MINUTOS [args]` |
| Ventanas | `analysis/local/perf001/window_stats.py RUN DESDE HASTA` (ventanas de 2 s del despachador que terminan en el intervalo) |

**Corridas** (`analysis/local/perf002/`):

- `runA_base`: intento fallido. El exe copiado fuera de su carpeta no encontraba las DLL (`0xC0000135` a los 1,6 s);
  el juego no llegó a ejecutarse.
- **`runA`:** sin instrumentación. Es la referencia de rendimiento.
- **`runP_prof`:** perfilador de 1 ms (ventana 27000–27600) y contadores de trabajo.

## 2. Línea base del castillo (corrida A, sin instrumentación)

**HECHO:** carga correcta (`[CKPT:LOAD] SUCCESS`), 0 fallbacks VU1 y ningún error fatal. Llega al VSync 28024 a los
301 s.

| Ventana (VSync) | Contenido | VSync/s | ms de pared por VSync (media / mediana / p95)* | VU1 ms | PATH1 ms | Resto ms | Llamadas VU1 por VSync | Interpretadas |
|---|---|---|---|---|---|---|---|---|
| 26788–28024 | cinemática completa medida | **4,31** | 231,8 / 200,6 / 400,0 | 100,8 | 74,4 | 56,6 | 1 063 | 2,9 % |
| 26788–26995 | inicio | 5,74 | 174,4 / 167,3 / 200,0 | 81,7 | 39,0 | 53,7 | 834 | 3,4 % |
| **26995–27598** | **tramo pesado (ventana del perfil)** | **3,86** | 259,1 / 225,8 / 400,1 | 111,1 | 88,4 | 59,6 | 1 100 | 3,0 % |
| 27793–28024 | final de la cinemática, justo antes del control | 5,24 | 190,7 / 184,3 / 200,1 | 88,4 | 51,3 | 50,9 | 991 | 3,1 % |

\* La mediana y el p95 salen de ventanas de 2 s, no de cada VSync.

- **Tiempo de pared hasta cada tick** (relojes del despachador): VSync 27000 a los 46 s de cargar, 27800 a los
  253 s y 28000 a los 291 s.
- **HECHO:** el pico es el tramo 27500–27600, a 2,63 VSync/s (VU1 137 ms y PATH1 176 ms por VSync).
- **Gameplay controlable: no medido.** Hasta el control hacen falta ~290 s de pared; para una ventana de 500 VSync
  con control (28050–28550) harían falta unos 100 s más, es decir **~7 min de corrida** sin instrumentación (más
  con el perfilador). Necesita autorización.
- **Variabilidad:** una corrida por configuración. En la misma ventana 27000–27600, la corrida perfilada fue *más
  rápida* que la de referencia (4,03 frente a 3,86 VSync/s, +4 %), mientras que en PERF_001 el perfilador costaba
  un 11 %. **INFERENCIA:** la dispersión entre corridas es al menos del ±5 %; diferencias menores no son
  significativas sin repeticiones.

## 3. Perfil de CPU (corrida P, ventana 27000–27600)

**Método** (el de PERF_001, §2):

- Muestreo de 1 ms dentro del proceso, desenrollando la pila de cada hilo (`RtlVirtualUnwind`, 32 marcos).
- Simbolización fuera del proceso con el **mapa del enlazador** del mismo ejecutable (`runP_dmc-recomp.map`). La
  simbolización interna del perfilador (PDB Release) vuelve a fallar (`dmc-recomp!?` 89,6 %) y se descarta.
- Cada muestra se atribuye a **un solo** subsistema (el primer marco del runtime desde la hoja; el CRT cuenta para su
  llamante), así que el reparto exclusivo no cuenta nada dos veces. El GS que se ejecuta dentro de una llamada VU1
  cuenta como GS, no como VU1.
- 70 972 muestras del hilo del juego (149 s de pared, 600 VSync).
- Los ms por VSync son ESTIMACIONES: porcentaje del perfil × 259,1 ms (corrida A, misma ventana).

**Hilos (HECHO):** el hilo del juego consume el **98,3 %** de un núcleo; el de render, el 7,7 % (90 % de sus muestras
esperando); el resto, ≤ 2,4 %. **El proceso está limitado por un solo hilo.** La presentación (`GS presentation`)
es el 1,2 % del hilo de render y no está en el camino crítico.

| Subsistema (exclusivo, hilo del juego) | % | ms por VSync (ESTIMACIÓN) |
|---|---|---|
| **GS rasterizador por software** | **55,0** | ~143 |
| ↳ triángulos por PATH1 (`DrawTriangle` bajo una llamada VU1) | 38,7 | ~100 |
| ↳ sprites por DMA de la CPU (`DrawSprite`) | 15,9 | ~41 |
| ↳ `UploadImage` y otros | 0,4 | ~1 |
| **VU1 programa compilado** | **28,7** | ~74 |
| ↳ cuerpo `vu1c_a0dbee2aeac4dafa` | 12,6 | ~33 |
| ↳ ayudas de exactitud FMAC (`dsign` 4,1; `exactFlags` 3,7; `nrm` 2,3; `productSticky` 2,0; `macBits` 1,9; `floatToInt` 0,4) | 14,4 | ~37 |
| **VU1 intérprete** (3,0 % de las llamadas) | **9,0** | ~23 |
| DMA y memoria EE | 2,4 | ~6 |
| VIF1 | 2,3 | ~6 |
| Código EE recompilado del juego | 0,9 | ~2 |
| GIF / front end del GS | 0,7 | ~2 |
| Audio / MPEG / IPU | 0,5 | ~1 |
| Despachador VU1 | 0,2 | <1 |
| Planificador EE | 0,2 | <1 |

**Inclusivo** (una vez por muestra): `PS2Runtime_vu1DispatchCall` 77,2 %; `GSCpuBackend::Submit` 54,7 %;
`SampleTexture` **29,7 %**; `VU1Interpreter::run` 14,6 % (incluye el GS de las llamadas interpretadas);
`WritePixel` 12,4 %; lectores de VRAM (lambdas de `ps2_gs_memory`) 10,9 %; `lerpChannel` 8,0 %; `exactFlags` 6,8 %;
`LookupCLUT` 6,0 %.

**Funciones más costosas (exclusivas, % del hilo):**

- **GS:** lector de VRAM de `ps2_gs_memory` (`ReadCT32` y afines) 10,0; `DrawTriangle` 8,1; `SampleTexture` 5,5 más su
  lambda 3,5; `WritePixel` 4,7; `lerpChannel` 3,7 más `lroundf` 3,2 y `roundf` 1,1; `combineTexture` 2,8;
  indirección `std::function` de lectura y escritura de VRAM (`_Do_call`) 3,2; `LookupCLUT` 2,1; `DrawSprite` 1,4.
- **VU1 intérprete:** `commitReadyPipelines` 3,1; `run` 0,9; `normalizeOperand` 0,8.
- **DMA:** `unordered_set<DmaChainState>::emplace` (detección de ciclos del parche 22) 0,9, más el *rehash* 0,1.

**Contraste con los relojes del despachador** (corrida A, misma ventana): VU1 111 ms, que equivale al 43 % del
perfil (compilado 28,7 más intérprete inclusivo 14,6); PATH1 88 ms, que es el GS bajo llamadas compiladas
(~33 % → ~86 ms). Es coherente.

## 4. Medidas de carga por VSync (corrida P, contadores temporales)

| Medida | Castillo (27000–27600) | Oficina (PERF_001 §3) |
|---|---|---|
| Llamadas VU1 | 1 098 (3,0 % interpretadas) | 1 056–1 063 (14,8 % antes de P1-A; 2,0 % después) |
| Ciclos VU1 por llamada | 2 333 | 1 885 |
| Ciclos VU1 | 2,57 M | 1,99 M |
| Triángulos | **17 757** | 11 933–11 952 |
| Sprites | 20 | 20 |
| Área recortada de los sprites | 1,28 M px | 1,28 M px |
| Llamadas a `WritePixel` | **1,96 M** | 1,63 M |
| Muestras de textura (todas bilineales) | **1,02 M** | 690 k |
| Muestras con fracción cero (atajo del parche 28) | 114 k | 114 k |
| Tags DMA recorridos | 11 152 | 10 700 |
| Bytes VIF1 | 2,28 MB | 1,66 MB |

- **Llamadas VU1 interpretadas en la ventana** (HECHO, totales del despachador): 20 129 de 663 195. De ellas,
  12 347 (61 %) son `noProgram` y 7 782 (39 %) `chainBlocked`.
  - Entradas sin programa vistas en el castillo: MSCNT 0x20e0 y 0x2c10 (aplazadas en P1-A por no verificadas) y
    0x21c8, 0x2668 y 0x26b0 (necesitan `OPMULA` en el generador).
- **INFERENCIA:** los 20 sprites con 1,28 M px son las mismas capas a pantalla completa que en la oficina (mismo
  número y misma área). El aumento del castillo está en la geometría (triángulos +49 %, muestras +48 %).
- **No medido:** operaciones VU0 por tipo. Contarlas exigiría instrumentar el código EE generado, que lo prohíben las
  reglas; se sustituye por la cota de tiempo de §6. Tampoco se midió el tiempo del hilo de presentación por VSync
  (solo su CPU total, 7,7 %).

## 5. Cuellos de botella ordenados

1. **Triángulos texturizados por PATH1** (38,7 %, ~100 ms): rasterizado (`DrawTriangle` 8,1), muestreo bilineal
   (`SampleTexture` 29,7 % inclusivo, compartido con los sprites), lectores de VRAM con `std::function` y conversión o
   CLUT, y `lerpChannel` con `lroundf`.
2. **VU1 compilado** (28,7 %, ~74 ms): **la mitad son ayudas de exactitud FMAC** (14,4 %). Solo `dsign`, la función
   del CRT que implementa `std::signbit(long double)` en `exactFlags`, ya es el 4,1 % del hilo.
3. **Sprites por DMA de la CPU** (15,9 %, ~41 ms): 20 sprites y 1,28 M px por VSync, con `WritePixel` por píxel.
4. **VU1 intérprete** (9,0 % exclusivo, 14,6 % inclusivo): 5 entradas sin programa y continuaciones bloqueadas.
5. **Lectura de VRAM** (transversal a 1 y 3): 10,9 % inclusivo en los lectores más 3,2 % de indirección
   `std::function`.

Detrás: DMA y VIF1 (4,7 %, con la detección de ciclos en ~1 %) y el código EE (0,9 %).

## 6. Coste del parche 30

**HECHO:** las 395 actualizaciones MAC están *inline* en 43 funciones generadas
(`analysis/local/perf002/mac_functions.json`). El muestreo no separa la actualización del resto del cuerpo, así que
la medida válida es una **cota superior**: el tiempo exclusivo de esas 43 funciones completas.

- 155 muestras de 70 972: **0,22 % del hilo** (0,23 % inclusivo), ≈ **0,6 ms por VSync** como máximo.
- La mayor es `atari_check_4` (0,10 %), y en ese 0,10 % entra todo su trabajo de colisión, no solo los flags.
- Todo el código EE recompilado es el 0,9 %.
- **Conclusión:** el parche 30 no es un coste significativo en esta ventana. **Límite:** es una cinemática. Con
  control habrá más consultas de colisión, aunque la cota (0,9 % para todo el EE) deja poco margen para que importe.
- **No se puede** comparar con y sin el parche 30 en el castillo: sin él Dante cae a través del suelo y la carga
  cambia.

## 7. Comparación con PERF_001 (oficina)

**No son el mismo benchmark:** escenas, checkpoints, ventanas y cargas distintas. La oficina va a 6,76 VSync/s
(147,9 ms) y la cinemática del castillo a 3,86–4,31 VSync/s (231–259 ms). La diferencia no se atribuye al parche 30
(§6).

| Por VSync | Oficina (P3, 16500–17000) | Castillo (27000–27600) | Cambio |
|---|---|---|---|
| ms de pared | 147,9 | 259,1 | +111 |
| VU1 (compilado más intérprete) | 55,4 ms | 111,1 ms | ×2,0 |
| Envío PATH1 | 44,8 ms | 88,4 ms | ×2,0 |
| Resto (sprites, DMA, VIF, EE…) | 47,6 ms | 59,6 ms | +12 ms |
| Ciclos VU1 | 1,99 M | 2,57 M | +29 % |
| ns de host por ciclo VU1 (reloj VU1 / ciclos) | 27,8 | 43,3 | +56 % |

1. **Cuellos de botella que siguen:** el GS por software (P2 y el resto de P3 de PERF_001, sin hacer), las ayudas de
   exactitud del VU1 compilado (en PERF_001 ya figuraban: `dsign` 3,6 %, `exactFlags` 3,0 %, `nrm` 2,4 %) y la detección
   de ciclos DMA.
2. **Costes que aumentan en el castillo:** la geometría por PATH1 (triángulos +49 %, muestras +48 %, VIF1 +37 %) y el
   VU1 (más ciclos por llamada y más tiempo de host por ciclo).
   - **HIPÓTESIS** para el coste por ciclo: las llamadas interpretadas, aunque sean el 3 %, son de entradas
     largas; y el reloj VU1 incluye el GS de esas llamadas, que aquí es mayor. No está demostrado.
3. **Parche 30:** ≤ 0,22 % (§6). No es significativo.
4. **Sprites:** siguen siendo el mismo trabajo fijo (20 capas, 1,28 M px, ~41 ms). En proporción pesan menos (15,9 %
   frente a 22 %), pero son el mayor bloque de una sola función de dibujo después de los triángulos.
5. **Optimizaciones anteriores que siguen teniendo sentido:** P3 restante (lectores de VRAM sin `std::function`), P2
   (sprites), compilar las entradas VU1 que faltan y la detección de ciclos DMA sin `unordered_set`.

## 8. Cinco optimizaciones candidatas (no aplicadas)

### C1. `exactFlags` sin la llamada al CRT para el signo

- **Dónde:** `vendor/PS2Recomp/ps2xRuntime/src/lib/vu/ps2_vu1_native.h`, `vu1c::exactFlags` (también lo usa
  `productSticky`).
- **Coste actual:** `dsign` 4,1 % exclusivo (~11 ms por VSync); `exactFlags` 3,7 % más (6,8 % inclusivo).
  - HECHO (pilas, `dsign_callers.py`): de las muestras de `dsign`, el 3,05 % del hilo viene de `vu1c::exactFlags` y el
    1,08 % de `vu1c::productSticky`; solo el 0,12 % viene del intérprete (`normalizeFmacExactResult`).
- **Qué se elimina:** `std::signbit(long double)` compila a una llamada no *inline* a `dsign` del CRT. En MSVC x64,
  `long double` tiene la misma representación que `double`, así que el signo se obtiene con una lectura del bit 63, sin
  cambiar el resultado (también para −0 y NaN). `std::fabs(long double)` se puede tratar igual.
- **Ganancia (ESTIMACIÓN):** 4–6 % del hilo (~10–15 ms por VSync), es decir +4–6 % de VSync/s. Incertidumbre
  baja-media: depende de lo que el compilador haga después con la función *inline*.
- **Riesgo para la fidelidad:** muy bajo; exacto por construcción. Hay que fijar la premisa con un
  `static_assert(sizeof(long double) == sizeof(double))` y con la comprobación de `std::numeric_limits`.
- **Pruebas:** test exhaustivo de `exactFlags` frente a la versión actual en valores especiales (±0, ±min, ±max,
  subnormales, ±Inf, NaN y frontera de exponentes) y aleatorios; `PS2X_VU1_NATIVE=verify` con 0 diferencias en el
  castillo; hash PATH1; `ps2x_tests` sin fallos nuevos.
- **Compilación:** la cabecera la incluyen `ps2_runtime.cpp`, `ps2_vu1_native_dispatch.inc` y la unidad
  `vu1c_a0dbee2aeac4dafa.cpp` (~2,6 min) más el enlace. No toca el código EE generado.
- **Parche:** sí, un parche nuevo del runtime (31). **Regenerar:** no (ni EE ni vu1c).

### C2. Lectores de VRAM sin `std::function` (resto de P3 de PERF_001)

- **Dónde:** `gs/gs_cpu_backend.cpp`, tabla `m_readVramFuncs` y `m_writeVramFuncs`, y su uso en `ReadVramUnlocked`,
  `SampleTexture` y `WritePixel`.
- **Coste actual:** `_Do_call` 3,2 % exclusivo; los lectores 10,9 % inclusivo.
- **Qué se elimina:** la indirección `std::function`. Se usan punteros a función, o el lector se resuelve una vez por
  primitiva. La conversión de PSM no cambia.
- **Ganancia (ESTIMACIÓN):** 3–5 % del hilo. Incertidumbre media.
- **Riesgo:** bajo; la aritmética es la misma.
- **Pruebas:** corpus diferencial de PERF_001 P3 (168 casos con hash de VRAM) y los tests GS; hash PATH1.
- **Compilación:** una unidad (`gs_cpu_backend.cpp`, ~3 min con enlace). Su cabecera solo la incluyen
  `gs_cpu_backend.cpp` y `gs_frontend.cpp`.
- **Parche:** sí. **Regenerar:** no.

### C3. Camino rápido de sprites (P2 de PERF_001)

- **Dónde:** `gs/gs_cpu_backend.cpp`, `DrawSprite` y `WritePixel`.
- **Coste actual:** 15,9 % (~41 ms por VSync); 20 sprites y 1,28 M px.
- **Qué se elimina:** volver a decodificar el estado del contexto en cada píxel. Se decodifica una vez por primitiva,
  con variantes para TME=0 y para los casos sin test Z ni alpha.
- **Ganancia (ESTIMACIÓN):** 30–50 % del coste de sprites, es decir 5–8 % del hilo. Incertidumbre media-alta: falta
  identificar qué capas son y con qué estado.
- **Riesgo:** medio; hay muchas combinaciones de estado.
- **Pruebas:** diferencial de sprites con estados aleatorios y VRAM byte a byte; volcados de VRAM de la ventana.
- **Compilación:** una unidad. **Parche:** sí. **Regenerar:** no.

### C4. Compilar las entradas VU1 que faltan

- **Dónde:** `tools/vu1_replay/dmc_vu1_programs.json` (0x20e0 y 0x2c10, ahora en `deferred_entries`) y
  `tools/vu1_replay/gen_vu1c.py` (`OPMULA` para 0x21c8, 0x2668 y 0x26b0).
- **Coste actual:** el intérprete es el 9,0 % exclusivo y el 14,6 % inclusivo. El 61 % de las llamadas interpretadas
  son `noProgram`.
- **Qué se elimina:** interpretar esas entradas, unas 10 veces más lento por llamada. Las continuaciones
  `chainBlocked` (39 %) no se resuelven así.
- **Ganancia (ESTIMACIÓN):** 3–6 % del hilo. Incertidumbre alta: no se mide el coste por entrada.
- **Riesgo:** bajo con verify. 0x20e0 y 0x2c10 se ejecutan en el castillo, así que ahora se pueden verificar.
- **Pruebas:** verify con 0 diferencias en el castillo, hash PATH1 y los tests del generador para `OPMULA`.
- **Compilación:** regenerar `recomp/vu1c` (segundos, local) y compilar una unidad (~2,6 min). **Parche del
  vendor:** no. **Regenerar el EE:** no. Cuidado con el tamaño de función (C4883, PERF_001 §8.1).

### C5. Detección de ciclos DMA sin `unordered_set`

- **Dónde:** `ps2_memory.cpp` (detección de ciclos del parche 22).
- **Coste actual:** ~1,0 % (`emplace` más *rehash*).
- **Qué se elimina:** las asignaciones y el *hash* por tag. Se usa un conjunto plano reservado, o Floyd o Brent, con
  la misma detección exacta.
- **Ganancia:** ~1 %. Incertidumbre baja.
- **Riesgo:** bajo si conserva la detección exacta.
- **Pruebas:** los tests de cadenas DMA del parche 22 y cadenas con ciclos sintéticos.
- **Compilación:** una unidad. **Parche:** sí. **Regenerar:** no.

**Descartada por ahora:** sustituir `lroundf` en `lerpChannel` (3,2 % más `roundf` 1,1 %). Exige demostrar la
equivalencia para todo el dominio de pesos y valores; no es un cambio pequeño ni exacto por construcción.

## 9. Recomendación: primera optimización

**C1** (`exactFlags` sin `dsign`):

- Es el cambio más pequeño, exacto por construcción y con un coste medido aislado (4,1 % en una sola función del CRT).
- No regenera código, compila tres unidades y se valida con el modo verify del VU1, que ya existe.
- Le sigue **C2**, también exacto y de una unidad. C3 y C4 dan más margen, pero necesitan más validación.

**Antes de implementar:** autorizar una corrida sin instrumentación de **~7 min** para tener una línea base de
gameplay con control (ventana 28050–28550). Sin ella, las mejoras solo se pueden medir en la cinemática.

## 10. Pruebas de fidelidad necesarias (cualquier candidata)

- Igualdad exacta frente a la implementación actual, nunca «parecido visual».
- VU1: `PS2X_VU1_NATIVE=verify` con 0 diferencias y hash PATH1 igual entre native e interpreter.
- GS: VRAM byte a byte en tests y en volcados de la ventana.
- `ps2x_tests` sin fallos nuevos (hoy 467 y los 24 preexistentes).
- Benchmark: el mismo checkpoint, la misma ventana y el ejecutable sin sondas. Solo se comparan corridas con la misma
  carga (llamadas VU1 por VSync y proporción interpretada). Hacen falta varias repeticiones, porque la dispersión
  observada es de ±5 %.

## 11. Limitaciones de las mediciones

- **Sin gameplay controlable:** las ventanas son de la cinemática de entrada (§2). No se inventan datos posteriores
  al control.
- Una corrida por configuración; sin estadística de dispersión (§2: ±5 % observado entre A y P).
- Los ms por subsistema son estimaciones (porcentaje del perfil × la corrida A).
- La mediana y el p95 salen de ventanas de 2 s.
- Atribución exclusiva por objeto del primer marco del runtime. El código *inline* (MAC del parche 30, ayudas de
  `exactFlags` dentro del cuerpo vu1c) se cuenta en su función contenedora.
- El reloj VU1 del despachador incluye el GS de las llamadas interpretadas.
- Contadores y perfilador solo en la corrida P. La corrida A no tiene instrumentación salvo las estadísticas
  permanentes del despachador.
- Las operaciones VU0 por tipo no se midieron (§4).

## Instrumentación temporal (retirada)

- Perfilador `perf001_profiler.inc` y su gancho en `runtime/dmc_overrides.cpp`; contadores en `gs_cpu_backend.cpp`,
  `ps2_memory.cpp` y `ps2_vif1_interpreter.cpp` (`analysis/local/perf001/retired_instrumentation/`, los mismos de
  PERF_001; el contador de fracción cero se aplicó con *fuzz* sobre el parche 28 y se comprobó su posición).
- Retirados después de la corrida P: el vendor vuelve a ser idéntico al replay (`fa1b4d7a…`), `runtime/` coincide
  con `master` y el ejecutable de trabajo se recompiló sin el perfilador (4 unidades más el enlace, 2 min 50 s;
  `12a6a935…`, 0 cadenas `PERF001`). El hash no coincide con `d1e6a3bd…`, aunque las fuentes
  son las mismas (INFERENCIA: el enlace de MSVC no es reproducible byte a byte; no se ha comparado sección a sección). El ejecutable de referencia medido
  se conserva como `dmc-recomp-perf002-base.exe`.
- **Corrección de método:** la primera compilación de perfil falló al enlazar porque Git Bash convirtió `LINK=/MAP` en
  una ruta; se repitió con `LINK=-MAP`.
- **Evidencia local:** `analysis/local/perf002/` (corridas, `runP_dmc-recomp.map`, `runP_analysis.txt`,
  `gs_breakdown_p.py`, `mac_cost.py`, `mac_functions.json`, `bench_castle.sh`, logs de compilación).

## 12. Línea base de gameplay (corrida G, 2026-10-29)

**Corrida:** `analysis/local/perf002/runG`, guion `bench_gameplay.sh`. Una sola corrida autorizada, límite duro de 9
min; duró 466 s.

- **Configuración:** el mismo ejecutable de referencia `d1e6a3bd…` y la misma configuración que la corrida A
  (Release, 30 parches, `native`, FFmpeg ON, `PS2X_VU1_STATS_SEC=2`), sin perfilador, sondas, contadores ni
  peticiones de checkpoint, y sin cambios de código.
- **Única diferencia con A:** el canal P311 está activo y fijo en neutro (`FFFF`, sticks 0x80). Así la entrada es
  determinista (Dante quieto) y el mando real no puede interferir. Su coste es un hilo que lee un fichero cada 10 ms.
  **INFERENCIA:** es despreciable frente a 216 ms por VSync; no se ha medido.
- **Entradas (HECHO, log de P311):**
  1. neutro desde la carga;
  2. después de la ventana, stick izquierdo `lx=0x00` durante 4 s (t = 456–461 s);
  3. neutro otra vez.
- **Capturas:** solo 3, puntuales (`runG/shots/`): al llegar al VSync 28060, al pasar el 28555 y tras el stick.
  - **Corrección de método:** las marcas de las dos últimas registran `tick=28680`, porque el log llega al guion con
    retraso (búfer). Ambas se dispararon cuando el log ya mostraba un VSync ≥ 28555, es decir después de la
    ventana, así que la ventana de reposo no contiene ni capturas ni movimiento.

**Estado de Dante y del control (HECHO, capturas):**

- En 28060 y en ≥ 28555, Dante está de pie ante la puerta del castillo, con la cámara fija.
- Tras el stick, cambia de orientación y de pose (carrera, abrigo en movimiento) y la cámara se desplaza. El control
  está liberado.
- Se ven los errores gráficos conocidos (bloques de colores en el lado izquierdo).
- No hay medida directa de la variable de control (`0x742168`), porque la sonda G002 está retirada.

**Resultados (HECHO, despachador VU1, 0 fallbacks, 0 FATAL):**

| Ventana | VSync/s | ms por VSync (media / mediana / p95)* | VU1 ms | PATH1 ms | Resto ms | Llamadas VU1 por VSync | Interpretadas |
|---|---|---|---|---|---|---|---|
| **28050–28550, gameplay en reposo** | **4,62** | **216,3** / 222,2 / 250,7 | **109,8** | **41,8** | **64,7** | 1 135 | 2,6 % |
| 26799–28020, cinemática completa | 4,12 | 243,0 / 222,3 / 335,5 | 105,7 | 76,2 | 61,1 | 1 065 | 2,9 % |
| 26997–27596, tramo pesado | 4,09 | 244,3 / 222,2 / 336,6 | 104,5 | 82,1 | 57,7 | 1 098 | 3,0 % |

\* En ventanas de 2 s.

- **No medido en gameplay:** triángulos, sprites, `WritePixel`, muestras de textura y el reparto de CPU por función.
  - Esos contadores no existen en el runtime sin instrumentación temporal, y esta corrida no admitía cambios de
    código ni perfilador.
  - Las únicas cifras de trabajo del GS son las de la cinemática (§4).
  - **Actividad del GS en gameplay (HECHO, reloj del despachador):** el envío PATH1 baja a 41,8 ms (la mitad que en
    la cinemática pesada) y el resto sube a 64,7 ms.

### 12.1 Variabilidad conocida

La misma ventana 27000–27600 (cinemática, sin entrada, misma carga de VU1: 1 098–1 100 llamadas, 3,0 %) en tres
corridas:

| Corrida | Instrumentación | VSync/s |
|---|---|---|
| A | ninguna | 3,86 |
| P | perfilador de 1 ms y contadores | 4,03 |
| G | P311 neutro | 4,09 |

- **HECHO:** con la misma carga, la dispersión entre corridas es de ~6 % (3,86–4,09), y la corrida con perfilador no
  fue la más lenta.
- La cinemática completa da 4,31 (A) frente a 4,12 (G), un 4,5 %.
- **INFERENCIA:** el entorno (otros procesos, frecuencia de la CPU, temperatura) mueve el resultado un ±3–6 %. Una
  mejora menor del ~6 % no se puede afirmar con una corrida por configuración.

### 12.2 Comparación entre escenas

| Por VSync | A. Oficina (PERF_001 P3, 16500–17000) | B. Cinemática del castillo (27000–27600, A / G) | C. Gameplay del castillo en reposo (28050–28550, G) |
|---|---|---|---|
| VSync/s | 6,76 | 3,86 / 4,09 | **4,62** |
| ms de pared | 147,9 | 259,1 / 244,3 | **216,3** |
| VU1 ms | 55,4 | 111,1 / 104,5 | **109,8** |
| PATH1 ms | 44,8 | 88,4 / 82,1 | **41,8** |
| Resto ms | 47,6 | 59,6 / 57,7 | **64,7** |
| Llamadas VU1 / interpretadas | 1 058 / 2,0 % | 1 100 / 3,0 % | 1 135 / 2,6 % |
| Triángulos / sprites / `WritePixel` / muestras | 11,9 k / 20 / 1,63 M / 690 k | 17,8 k / 20 / 1,96 M / 1,02 M (corrida P) | no medido |

Todo es HECHO salvo la columna B de trabajo del GS, que solo existe para la corrida P.

- El **VU1 es el componente más estable entre las escenas del castillo** (105–111 ms por VSync) y duplica el de la
  oficina con un número parecido de llamadas.
- En gameplay en reposo, **el VU1 es la mitad del tiempo de pared** (110 de 216 ms).
- El envío PATH1 cae a la mitad respecto a la cinemática. **INFERENCIA:** la cámara fija de gameplay dibuja menos
  píxeles por PATH1 que los planos de la cinemática; no hay contadores que lo confirmen.
- **No se atribuye ninguna diferencia al parche 30:** las tres escenas tienen cargas distintas y no hay una
  comparación equivalente con y sin el parche.

### 12.3 C1 (`exactFlags`): revisión antes de implementar

**1. ¿Sigue siendo relevante en gameplay?**

- **HECHO:** `dsign` aparece en dos perfiles: la oficina (PERF_001, 3,6 %) y la cinemática del castillo (§3, 4,1 %;
  3,05 % desde `exactFlags` y 1,08 % desde `productSticky`).
- **No hay perfil de gameplay.**
- **INFERENCIA:** en gameplay en reposo el VU1 cuesta lo mismo (110 ms) con un 3 % más de llamadas, y las ayudas
  dependen del número de FMAC ejecutadas, no de la escena. Su peso relativo debería ser parecido o mayor (el VU1 es
  el 51 % de la pared en gameplay, frente al 43 % en la cinemática). No está medido.

**2. Semántica (HECHO, cabeceras de MSVC 14.44 y UCRT 10.0.26100):**

- `std::signbit(long double)` es la función *inline* de UCRT `signbit(long double)` → `_ldsign(_X) != 0`, exportada
  por `ucrtbase` y no inlineable. Eso explica la hoja `dsign` del perfil.
  - **INFERENCIA:** el símbolo exportado más cercano es `_dsign`; no se ha comprobado si `_ldsign` comparte código con
    él.
- En MSVC x64, `long double` usa la misma representación IEEE-754 binary64 que `double` (64 bits,
  `LDBL_MANT_DIG == DBL_MANT_DIG == 53`). Hay que fijarlo en el propio cambio con
  `static_assert(sizeof(long double) == 8 && LDBL_MANT_DIG == 53)`.
- `_dsign` y `_ldsign` devuelven un valor distinto de 0 si y solo si el bit de signo está activo. Leer el bit 63 de la
  representación (`memcpy` a `uint64_t`) da lo mismo en todas las clases:
  - +0 → 0, −0 → 1;
  - subnormales y normales → su signo;
  - ±Inf → su signo;
  - NaN → su bit de signo, sea cual sea la carga útil. No se usa ninguna comparación, que con NaN daría falso.
- `exact` llega siempre como un valor ya calculado. La extracción lee los mismos bits que `_ldsign`, así que no cambia
  qué NaN o qué −0 se observa.
- `std::fabs(long double)` y las comparaciones de magnitud no llaman al CRT (no aparecen en el perfil) y no cambian.
- **Riesgo residual:** un compilador o una plataforma donde `long double` sea de 80 o 128 bits. El `static_assert`
  lo convierte en error de compilación, y en ese caso hay que dejar el camino `std::signbit`.

**3. Pruebas diferenciales propuestas:**

- **Unitaria** (`ps2xTest`):
  - `exactFlags` nuevo frente a la implementación actual, copiada en el test como referencia;
  - entradas: ±0, ±`FLT_MIN`, ±`FLT_MAX` y sus vecinos, ±`FLT_TRUE_MIN`, subnormales `double`, ±Inf, NaN con
    signo positivo y negativo y cargas variadas, y fronteras de exponente;
  - los 2^32 patrones de `float` convertidos a `double` (exhaustivo, unos segundos);
  - 10^7 productos aleatorios `float × float` calculados en `long double`, como hace `productSticky`;
  - se comparan los flags devueltos y los bits del `value` resultante;
  - la mutación (invertir el signo) debe hacer fallar los tests.
- **En el juego:** `PS2X_VU1_NATIVE=verify` desde `g002_castle_entry.dmck` con 0 diferencias en estado, flags,
  PATH1 y ciclos; hash PATH1 igual; `ps2x_tests` con los mismos 24 fallos.

**Benchmark antes/después, capaz de separar la ganancia del ruido:**

- Corridas alternas A/B/A/B (al menos 3 de cada), con los mismos dos ejecutables y la misma ventana. Se comparan las
  medianas y el rango.
- Ventana preferente: 27000–27600 de la cinemática, que no depende de la entrada y llega en 5 min. Más una corrida
  de gameplay en reposo si se autoriza.
- **Componente objetivo:** el reloj VU1 del despachador por VSync. La ganancia esperada (~10–15 ms) es mayor que la
  dispersión observada del componente VU1 en la misma ventana (104,5–111,1 ms).
- **Control negativo:** PATH1 y el resto no deberían cambiar más allá de su dispersión, como en PERF_001 P3.
- **Complemento sin ruido del juego:** `tools/vu1_replay` con `--profile` sobre capturas de llamadas, para medir el
  coste por ciclo VU1 antes y después de forma aislada.

### 12.4 Recomendación final

- **Primera optimización: C1**, sin cambios respecto a §9.
  - El coste medido existe en la oficina y en la cinemática del castillo. En gameplay hay que confirmarlo con el
    benchmark A/B.
  - Es exacta por construcción y la protege un `static_assert`.
- **Duración estimada (ESTIMACIÓN):**

| Paso | Tiempo |
|---|---|
| Implementar el cambio y el test | ~1 h |
| Compilación incremental: `ps2_runtime.cpp`, `vu1c_a0dbee2aeac4dafa.cpp` y enlace; más `ps2x_tests` | ~6 min |
| Corrida verify (5 min; cubre unos 300 VSync del castillo a ~1 VSync/s) | 5 min |
| Benchmark A/B/A/B/A/B en la ventana de la cinemática | ~30 min |
| Documentación, parche 31 y lock | ~30 min |

  - Total: unas **2,5 h** de trabajo y unos 45 min de máquina.
  - No hace falta regenerar el código EE ni vu1c, ni compilación completa.
- **Si el A/B no muestra una ganancia mayor que la dispersión**, no integrar y pasar a C2.

## 13. C1 implementado y validado experimentalmente (2026-10-29, sin integrar)

### 13.1 Causa concreta del coste

- **HECHO (cabeceras):** `vu1c::exactFlags` llamaba a `std::signbit(long double)`. Con MSVC 14.44 y UCRT 10.0.26100
  eso es la función *inline* de UCRT `signbit(long double)` → `_ldsign(_X) != 0`. `_ldsign` está exportada por
  `ucrtbase` (`api-ms-win-crt-math-l1-1-0.dll`) y no se puede inlinear.
- **HECHO (ensamblador del ejecutable de perfil):** `exactFlags` (47 instrucciones) y `productSticky` (43, con
  `exactFlags` *inline*) contienen `call qword ptr [__imp__ldsign]`. El mapa resuelve esa dirección a
  `__imp__ldsign`.
- El cuerpo vu1c llama a `exactFlags` 1 059 veces y a `productSticky` 645 veces (sitios de llamada estáticos).
- En el perfil de la cinemática, esa llamada externa suma el 4,13 % del hilo.
- **Llamadas a `signbit` conservadas a propósito:**
  - `VU1Interpreter::normalizeFmacExactResult` (`ps2_vu1_core.cpp`, 0,12 % del perfil): el intérprete es la
    referencia del modo verify y, al dejarlo intacto, la verificación compara la versión nueva con la antigua;
  - `DIV`/`RSQRT` del intérprete y de vu1c, sobre `float`: van a `_fdsign` y no aparecen en el perfil; tocar la de
    vu1c exigiría regenerar `recomp/vu1c`.

### 13.2 Archivos modificados (experimento local; vendor fuera del replay)

- `vendor/PS2Recomp/ps2xRuntime/src/lib/vu/ps2_vu1_native.h`:
  - `#include <bit>`;
  - plantilla `vu1c::signBit(T)`: si `T` es IEEE-754 binary64 (`sizeof == 8`, `is_iec559`, `digits == 53`)
    devuelve el bit 63 mediante `std::bit_cast<uint64_t>`, sin aliasing; si no, `std::signbit`;
  - `static_assert` para MSVC x64 (`long double` binary64);
  - `exactFlags` usa `signBit(exact)`. El resto de la función no cambia.
- `vendor/PS2Recomp/ps2xTest/src/ps2_vu_tests.cpp`: suite `PS2VU1NativeExactFlags` (3 tests).
- **Dependencias de la cabecera** (HECHO): `ps2_runtime.cpp` (con `ps2_vu1_native_dispatch.inc`), la unidad
  `recomp/vu1c/vu1c_a0dbee2aeac4dafa.cpp`, la herramienta aparte `tools/vu1_replay` y ahora el test. **Ningún
  fichero EE generado la incluye** (0 de 10 129).
- **Compilación:**
  - juego: 2 unidades más el enlace, 2 min 34 s;
  - tests: 18 s;
  - ejecutable C1: `c04090cae51b6a73f26c05961c00844b6507286f0d9076056d41e4cd1bcf8b35`, copiado como
    `dmc-recomp-perf002-c1.exe`.
- **Opciones** (HECHO, `.vcxproj`): el runtime, la unidad vu1c y los tests compilan en Release con `/O2`, `/Ob2`,
  C++20 y `/fp:precise` por defecto; la unidad vu1c añade `/d2OptimizeHugeFunctions`.
- **Propuesta de parche** (no publicada; no está en `patches/` ni en el lock):
  `analysis/local/perf002/c1/PERF_002_vu1_exactflags_signbit.patch.proposal`, sha256 `170b6717…`. Aplica limpio
  sobre el replay de 30 parches; +140/−1 líneas en 2 ficheros.

### 13.3 Pruebas diferenciales (HECHO, `ps2x_tests` Release, compilado desde el vendor en un directorio de build aparte)

- **Oráculo:** la implementación anterior de `exactFlags`, copiada literalmente en el test. Se comparan los flags,
  los bits del `float` resultante y `signBit` frente a `std::signbit`.
- `directed_classes_both_signs`: 19 patrones por signo (±0, subnormales y `DBL_MIN`, `FLT_TRUE_MIN` y alrededor de
  `FLT_MIN`, 1, alrededor de `FLT_MAX`, `DBL_MAX`, Inf, NaN silencioso y señalizador con 5 *payloads*), cada uno con 3
  valores iniciales. 0 diferencias. Además, `signBit` coincide con el bit de signo esperado en los 38 patrones.
- `exponent_sweep`: los 2 048 exponentes × 2 signos × 5 mantisas = 20 480 casos. 0 diferencias.
- `random_patterns_and_products`: 4 000 000 patrones binary64 aleatorios (splitmix64, semilla fija) más
  2 000 000 productos `float × float` aleatorios por `productSticky`. 0 diferencias.
- **Suite:** 470 tests (467 + 3); 446 pasan y fallan los mismos 24 preexistentes (el conjunto atribuible no cambia).
- **Mutación:** leer el bit 62 en lugar del 63 hace fallar los 3 tests nuevos (27 fallos en total). Se revirtió y se
  recompiló; la cabecera es idéntica a la versión C1 guardada.
- **Ensamblador** (HECHO):
  - unidad mínima con la cabecera real y las opciones Release: `exactFlags` y `productSticky` obtienen el signo con
    `movq` + `shr reg, 63` y no hacen ninguna llamada. El contraste, `std::signbit(long double)`, hace
    `call _ldsign`;
  - en el ejecutable C1, `exactFlags` (38 instrucciones) y `productSticky` (36) no tienen ninguna llamada; antes
    tenían 47 y 43, con `call [__imp__ldsign]`;
  - el cuerpo vu1c tiene el mismo patrón de llamadas a las ayudas (5 297 frente a 5 298 llamadas; no se investigó la
    diferencia de una);
  - la importación de `_ldsign` sigue en el ejecutable, para el intérprete.

### 13.4 Verificación VU1 (HECHO, corrida `c1_verify`)

- Configuración: ejecutable C1, `PS2X_VU1_NATIVE=verify`, `PS2X_VU1_VERIFY_STRICT=1`, traza de llamadas, hash
  PATH1, `g002_castle_entry.dmck`, 5 min, sin entrada.
- Cobertura: VSync 26640–27176 (cinemática del castillo; no incluye gameplay).
- **362 767 llamadas comparadas, 0 diferencias** (vf/vi, ACC, Q/P/I/R, MAC/STATUS/CLIP, pc, bits E/D/T, memoria de
  datos, paquetes PATH1 y delta de ciclos). 0 omitidas y 0 rechazadas; el modo estricto no se activó.
- Las 7 157 llamadas restantes son `noProgram` (entradas sin compilar, que siguen en el intérprete).
- 335 bloques de hash PATH1 registrados.
- **Las 15 entradas compiladas se ejecutaron y se compararon:**

| Entrada | Llamadas comparadas |
|---|---|
| MSCNT 0x0348 | 131 211 |
| MSCNT 0x13a8 | 124 275 |
| MSCNT 0x0300 | 24 236 |
| MSCNT 0x00b8 | 17 113 |
| MSCAL 0x0000 | 17 113 |
| MSCNT 0x0fc0 | 15 832 |
| MSCAL 0x0f00 | 15 832 |
| MSCNT 0x2098 | 10 716 |
| MSCNT 0x1bc0 | 2 561 |
| MSCAL 0x1b00 | 2 561 |
| MSCNT 0x13f0 | 1 766 |
| MSCAL 0x2100 | 852 |
| MSCNT 0x2bc8 | 12 |
| MSCNT 0x27c8 | 4 |
| MSCAL 0x2700 | 4 |

- **Cobertura baja:** MSCNT 0x2bc8 (12), MSCNT 0x27c8 (4) y MSCAL 0x2700 (4) se verificaron con muy pocas llamadas.
- **Sin compilar, por tanto fuera de esta verificación:** 0x20e0, 0x2668, 0x21c8 y 0x26b0, que aparecieron como
  `interp`; 0x2c10 no apareció en este tramo.

### 13.5 Rendimiento A/B (corrida `runB_c1`)

**Condiciones:** las de la corrida G, con el mismo guion `bench_gameplay.sh`: P311 neutro, la misma secuencia
posterior de stick y 3 capturas. Solo cambia el ejecutable (C1 frente a `d1e6a3bd…`). 0 FATAL. Las capturas muestran
la misma escena y la respuesta al stick.

| Ventana | Medida | A (G, base) | B (C1) | Diferencia |
|---|---|---|---|---|
| **Gameplay 28050–28550** | VSync/s | 4,62 | **5,91** | +28 % |
| | ms por VSync (media / mediana / p95) | 216,3 / 222,2 / 250,7 | 169,2 / 166,7 / 182,3 | −47,1 |
| | VU1 ms | 109,8 | 80,8 | **−29,0** |
| | PATH1 ms (control) | 41,8 | 34,6 | **−7,2** |
| | Resto ms (control) | 64,7 | 53,8 | **−10,9** |
| | Llamadas VU1 / interpretadas | 1 135 / 2,6 % | 1 134 / 2,6 % | igual |
| Cinemática 27000–27600 | VSync/s | 4,09 | 4,56 | +11 % |
| | VU1 / PATH1 / resto ms | 104,5 / 82,1 / 57,7 | 88,4 / 76,8 / 54,2 | −16,1 / −5,3 / −3,5 |
| Cinemática completa | VSync/s | 4,12 | 4,87 | +18 % |

**Interpretación:**

- **HECHO:** la carga es la misma (llamadas VU1 y proporción interpretada idénticas).
- **HECHO:** también bajan los componentes que C1 no toca (PATH1 y resto: −18,1 ms, −17 %, en gameplay; −8,8 ms,
  −6 %, en la cinemática).
- **INFERENCIA:** la máquina fue más rápida durante la corrida B. La mejora total (+28 %) **no es atribuible a C1**.
- **ESTIMACIÓN del efecto propio de C1**, descontando la deriva con los componentes de control (relación VU1 /
  (PATH1 + resto)):
  - gameplay: 1,031 → 0,914 (−11 %); cinemática: 0,748 → 0,675 (−10 %);
  - la misma relación en la línea base sin C1, en la misma ventana de la cinemática y en tres corridas (A, P, G),
    varió 0,748–0,767;
  - en tiempo, unos −9 a −11 ms de VU1 por VSync, coherente con el 4,1 % de `dsign` × ~220–260 ms;
  - **≈ 4–5 % de VSync/s**, por debajo de la dispersión entre corridas (~6 %).
- **Conclusión:** la ganancia **no está confirmada** con una corrida por lado. El indicador relativo es prometedor,
  y su efecto (≈ −0,075 en la cinemática) es unas 4 veces el rango de la base (0,019), pero se basa en 3 + 1 medidas.

**Corridas adicionales propuestas para que la ganancia sea creíble:**

- Alternar A y B en la ventana determinista de la cinemática (27000–27600, sin entrada, 5 min por corrida): **2 A + 2
  B más** (unos 20–22 min de máquina). Así habría ≥ 3 por lado en la misma sesión.
- **Criterio:** que la relación VU1 / (PATH1 + resto) de todas las B quede por debajo de todas las A, que las medias
  difieran en más de 3 desviaciones típicas de la base, y que el VU1 absoluto baje ≥ 8 ms sin que los controles se
  desvíen más del 6 %.
- Si se quiere el resultado en gameplay: 1 A + 1 B más en la ventana 28050–28550 (~2 × 7,5 min).
- No se lanzó ninguna de estas corridas.

### 13.6 Riesgos y limitaciones

- **Fidelidad:** exacta por construcción para binary64. La protegen el `static_assert`, las pruebas diferenciales
  (6 M de casos más el barrido de exponentes y los casos dirigidos) y la verificación. Otros modelos de
  `long double` (80 o 128 bits) usan `std::signbit` sin cambios.
- **Verify cubre solo la cinemática** (26640–27176); no el gameplay ni las entradas con poca cobertura (§13.4).
- **Rendimiento:** una corrida por lado; la deriva del entorno domina la diferencia absoluta (§13.5).
- **Vendor fuera del replay:** el árbol de trabajo del vendor contiene el experimento (2 ficheros) y no coincide
  con `patched_commit`. Integrarlo exige el parche 31, actualizar el lock y verificar el replay. Para descartarlo:
  restaurar los 2 ficheros desde `analysis/local/perf002/c1/*.before`.
- El ejecutable de trabajo `bin/Release/dmc-recomp.exe` es ahora el de C1. La referencia `d1e6a3bd…` se conserva
  como `dmc-recomp-perf002-base.exe`.

### 13.7 Recomendación

**Repetir el benchmark antes de integrar.** C1 supera todas las pruebas de fidelidad, elimina la llamada y el
indicador relativo muestra unos −10 % de VU1. Pero la ganancia absoluta no se puede separar de la deriva del entorno
con una corrida por lado.

- Con la serie alternada de §13.5 (2 A + 2 B en la cinemática), si se cumple el criterio: preparar el parche 31
  desde la propuesta para su revisión.
- Si no se cumple: conservar el experimento sin integrar y pasar a C2.

### 13.8 Serie alternada A/B/B/A (2026-10-29)

**Condiciones** (fijadas antes de ejecutar; `analysis/local/perf002/series_ab.txt`):

- Ventana **VSync 27000–27500** de la cinemática 3D del castillo.
- Orden A, B, B, A; corridas consecutivas de 5 min (19:50–20:11).
- Los mismos ejecutables, sin recompilar: A `d1e6a3bd…` y B `c04090ca…`.
- `bench_castle.sh`: `g002_castle_entry.dmck`, `native`, FFmpeg ON, sin entrada, sin P311, sin perfilador ni
  instrumentación, sin peticiones de checkpoint.
- Plan de energía «Equilibrado» en las cuatro. Ninguna se repitió.

**Resultados** (HECHO: las cuatro completan la ventana, 0 fallbacks, 0 FATAL):

| Corrida | VSync/s | ms por VSync | VU1 ms | PATH1 ms | Resto ms | VU1 / (PATH1 + resto) | Llamadas VU1 por VSync | Interpretadas |
|---|---|---|---|---|---|---|---|---|
| s1 A | 4,60 | 217,4 | 97,6 | 64,6 | 55,2 | 0,815 | 1 047 | 3,2 % |
| s2 B | 5,04 | 198,2 | 83,5 | 62,3 | 52,5 | 0,727 | 1 045 | 3,2 % |
| s3 B | 5,07 | 197,1 | 82,8 | 62,7 | 51,6 | 0,724 | 1 044 | 3,2 % |
| s4 A | 4,81 | 208,0 | 93,1 | 62,5 | 52,4 | 0,810 | 1 047 | 3,2 % |

**Variabilidad y diferencias:**

| Medida | A (media; rango) | B (media; rango) | B − A |
|---|---|---|---|
| VSync/s | 4,70; 0,21 | 5,06; 0,03 | +7,6 % |
| ms por VSync | 212,7; 9,4 | 197,7; 1,1 | −15,0 ms |
| VU1 ms | 95,4; 4,5 | 83,1; 0,7 | **−12,3 ms (−12,9 %)** |
| PATH1 ms | 63,5; 2,1 | 62,5; 0,5 | −1,0 ms |
| Resto ms | 53,8; 2,8 | 52,1; 0,9 | −1,8 ms |
| VU1 / (PATH1 + resto) | 0,813; 0,004 | 0,725; 0,003 | **−0,087 (−10,7 %)** |

- **Carga equivalente (HECHO):** 1 044–1 047 llamadas VU1 por VSync y 3,2 % interpretadas en las cuatro corridas.
- **Consistencia (HECHO):** las dos B tienen menos VU1 y menor relación normalizada que las dos A. La diferencia de
  la relación (0,087) es unas 20 veces el rango dentro de cada versión (0,003–0,004).
- **Deriva:** s1 A fue la más lenta en todos los componentes (los controles sumaron 119,8 ms frente a 114,3–114,9 en
  las otras tres). La comparación más limpia es s4 A frente a las B, con controles iguales al 0,5 %:
  - VU1 −10,0 ms;
  - −10,3 ms por VSync en total;
  - +5,2 % de VSync/s.
- **Conclusión:** la reducción de VU1 (10–12 ms por VSync, ~11 % normalizada) es reproducible, supera el umbral
  orientativo de 8 ms y es mucho mayor que la variación entre repeticiones de la misma versión. La ganancia total en
  esta ventana es de **+5 a +8 % de VSync/s**.
- **Coherencia:** el 4,1 % de `dsign` en el perfil equivale a ~9–10 ms por VSync a estos tiempos de pared, que
  coincide con lo medido.
- **Separado:** las medidas de gameplay de §13.5 no se mezclan con esta ventana.

**Parche 31 preparado (sin integrar):** `analysis/local/perf002/c1/PERF_002_vu1_exactflags_signbit.patch.proposal`
(sha256 `170b6717…`).

- Aplica limpio (`git apply --check`) sobre un replay de 30 parches recién reconstruido (`ce2d053d…`, árbol
  `fa1b4d7a…`).
- No está en `patches/` ni en el lock. Los parches 1–30 no cambian.

**Decisión propuesta: INTEGRAR C1** como parche 31 tras la revisión del usuario. Faltaría:

- copiar el parche a `patches/` y la entrada 31 en el README;
- actualizar el lock: sha256 y nuevo `patched_commit`;
- replay = vendor;
- documentar los límites que siguen: verify solo en la cinemática y 3 entradas con pocas llamadas.

### 13.9 C1 aprobado e integrado localmente como parche 31 (2026-10-29, sin commit)

**Conclusión: favorable.** El usuario aprobó C1 con la evidencia de §13.3–§13.8.

**Ganancia observada frente a ganancia atribuible:**

- **Observada:**
  - gameplay, una corrida por lado: +28 %, dominada por la deriva de la máquina (§13.5);
  - cinemática, serie A/B/B/A: +7,6 % de media.
- **Atribuible a C1:**
  - VU1 −10 a −12 ms por VSync y relación VU1/(PATH1+resto) −10,7 %, con controles estables;
  - frente a la A con los mismos controles (s4): **+5,2 % de VSync/s**.
- La cifra a usar es +5 %, no la observada.

**Equivalencia de VU1 (HECHO):** 362 767 llamadas comparadas con 0 diferencias; 0 diferencias en 6 M de casos
diferenciales más el barrido de exponentes; sin llamadas a `_ldsign` en las rutas optimizadas.

**Identidad del parche 31:**

- `patches/PERF_002_vu1_exactflags_signbit.patch`, sha256
  `170b67176df8f333434e5da73e4fc27cef42113d7f704cbf78b5b7d8105679d7`. Es byte a byte la propuesta revisada:
  `ps2_vu1_native.h` (+20/−1) y `ps2_vu_tests.cpp` (+120).
- `upstream.lock.json`: entrada 31; `patched_commit` **`dfa856e76663479a38de583cd2b3e3e5ad1a1be9`**, calculado con
  el procedimiento de `apply_patchset` (git apply en orden, `add -A`, `write-tree`, `commit-tree` con
  `patch_identity`).
- `patches/README.md`: entrada 31.

**Replay (HECHO):**

- desde el baseline limpio, 31 parches → árbol `3c908f788d6f55cc877f5bef7c9ff5d26229ff7e`, **idéntico al vendor de
  trabajo**;
- 30 parches → `ce2d053d…` (sin cambios), así que los parches 1–30 no cambian;
- los 31 sha256 coinciden con el lock.

**Tests y ejecutable:**

- `ps2x_tests`: la compilación incremental no recompiló nada (las fuentes no cambiaron desde la medida). 470 tests,
  446 pasan y fallan los mismos 24 preexistentes.
- El ejecutable de trabajo `bin/Release/dmc-recomp.exe` es `c04090ca…`, el mismo de la corrida B. Se compiló desde
  la cabecera idéntica a la integrada y no contiene cadenas de instrumentación (`PERF001`, `G002:probe`, `G003:`).

**Limitaciones que siguen:**

- verify no cubre el gameplay (solo los VSync 26640–27176);
- MSCNT 0x2bc8 (12), MSCNT 0x27c8 (4) y MSCAL 0x2700 (4) tienen pocas muestras.

**Pendiente:** un benchmark acumulativo en gameplay (28050–28550) con varias repeticiones cuando se combinen más
optimizaciones. No se repitió el benchmark, porque el código integrado es idéntico al medido.

## 14. Propuesta C2: lectores y escritores de VRAM sin `std::function` (no implementada)

### 14.1 Rutas exactas (HECHO, código del vendor con 31 parches)

- **Tablas:**
  - `gs_cpu_backend.h:61-69`: `ReadVramFunc` y `WriteVramFunc` son `std::function<…>`;
  - `m_readVramFuncs` y `m_writeVramFuncs` son `std::array` de 64 entradas (índice `psm & 0x3F`);
  - se rellenan una vez en el constructor (`gs_cpu_backend.cpp:474-532`) con funciones libres de `GSMem`
    (`ps2_gs_memory.cpp`), todas con firma `u32(u8*, u32, u32, u32, u32)` o `void(u8*, u32, u32, u32, u32, u32)`.
- **Despacho:** `ReadVramUnlocked` (`:593`) y `WriteVramUnlocked` (`:606`) llaman a `m_…Funcs[psm & 0x3F](m_vram, …)`.
  Cada llamada pasa por `std::_Func_impl_no_alloc::_Do_call`, una llamada virtual más, antes de llegar a `GSMem::Read*`.
- **Usos calientes en la ventana perfilada:**
  - `samplePoint` dentro de `SampleTexture` (`:1002`): una lectura por texel, 4 por muestra bilineal sin fracción
    cero;
  - `WritePixel`: lectura del destino para la mezcla y el FBMSK (`:817`), lectura de Z (`:847`, `:851`), escritura del
    color (`:930`) y de Z (`:935`);
  - transferencias y relleno (`:1441-1738`): `UploadImage`, local→local, *clear*; poco peso en la ventana.
- **Coste medido** (§3, cinemática):
  - `_Do_call` de lectura 2,43 % más `_Do_call` de escritura 0,79 % = **3,2 % exclusivo**;
  - las funciones `GSMem` de destino suman el 11,3 % exclusivo (`ps2_gs_memory.obj`); esa parte no la elimina C2.

### 14.2 PSM afectados

Los 14 de la tabla: CT32, CT24, CT16, CT16S, T8 (P8), T8H, T4 (P4), T4HH, T4HL, Z32, Z24, Z16 y Z16S, más la entrada
nula para los índices no válidos.

- **HECHO:** en el perfil domina `ReadCT32` (4,3 % en triángulos PATH1 y 2,3 % en sprites) y `LookupCLUT` (2,1 %),
  así que hay texturas paletizadas.
- **UNKNOWN:** el reparto por PSM en el castillo; los contadores temporales de PERF_001 no separan por PSM.

### 14.3 Cambio propuesto (mínimo, exacto por construcción)

- **C2a (recomendado):** cambiar los dos alias por punteros a función:
  - `using ReadVramFunc = uint32_t (*)(uint8_t *, uint32_t, uint32_t, uint32_t, uint32_t);`
  - `using WriteVramFunc = void (*)(uint8_t *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);`
  - las tablas, el relleno y los usos no cambian; se llama exactamente a la misma función con los mismos argumentos;
  - se elimina la capa `_Do_call` y la comprobación de `std::function` vacía. Ninguna entrada queda vacía: el bucle
    asigna `ReadNull`/`WriteNull` a los índices no válidos.
- **C2b (posterior, no propuesto ahora):** resolver el lector una vez por primitiva en `SampleTexture`/`WritePixel`.
  Mayor ganancia potencial, pero toca la estructura del rasterizador. Solo se plantearía tras medir C2a.

### 14.4 Pruebas diferenciales necesarias

- **Unitaria, por PSM:** para los 14 PSM, escribir con `WriteVram` patrones aleatorios en coordenadas y `bw` variados
  y releer con `ReadVram`. Comparar los bytes de VRAM y los valores leídos con los esperados.
  - Hashes registrados con el runtime **anterior** al cambio, como en el corpus de PERF_001 P3.
- **Existentes:** el corpus diferencial de 168 casos del parche 28 (6 PSM, CLUT, *clamp*, TEXA y mezcla, con hash de
  VRAM), los tests GS del parche 29 y la suite completa (470 tests, los mismos 24 fallos).
- **Mutación:** intercambiar dos entradas de la tabla (por ejemplo CT16 y CT16S) debe hacer fallar el test por PSM.
- **En el juego:** hash PATH1 sin cambios en un tramo corto (C2 no toca el VU1, así que debe ser idéntico) y
  comparación de capturas o volcados de VRAM de la ventana. Benchmark con la serie A/B/B/A del §13.8 en 27000–27500.

### 14.5 Ganancia, riesgo y coste (ESTIMACIÓN)

- **Ganancia:** hasta el 3,2 % del hilo (~6–8 ms por VSync), es decir +2–4 % de VSync/s. Incertidumbre media: el
  compilador puede seguir sin inlinear `GSMem::Read*` (otra unidad), y parte del coste de llamada se queda.
- **Riesgo para la fidelidad:** muy bajo; la misma función y los mismos argumentos.
- **Compilación:** `gs_cpu_backend.h` solo la incluyen `gs_cpu_backend.cpp` y `gs_frontend.cpp` (HECHO), es decir 2
  unidades más el enlace (~3–4 min) y el test (segundos). No toca el código EE ni vu1c.
- **Parche:** sí, el 32 (general del runtime). **Regenerar:** no.
- **Duración:** ~1,5 h de implementación y tests y ~25 min de serie A/B/B/A.
- **Nota:** con una ganancia esperada del 2–4 %, por debajo de la dispersión entre corridas (~6 %), C2a solo se podrá
  confirmar con la métrica normalizada. El indicador adecuado es (PATH1 + resto) / VU1, la inversa de la de C1,
  porque C2 actúa sobre el GS.
