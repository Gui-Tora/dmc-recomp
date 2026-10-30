# PERF_003 — Viabilidad de optimizaciones estructurales (auditoría estática)

Fecha: 2026-10-29. Rama local `perf-002-castle` (31 parches, `patched_commit` `dfa856e7…`, sin commit).

- No se ha cambiado código ni se han lanzado corridas nuevas.
- Datos de partida:
  - perfil de la cinemática del castillo (PERF_002 §3, corrida P, VSync 27000–27600, sin C1);
  - relojes del despachador de gameplay (PERF_002 §12, corrida G, VSync 28050–28550);
  - serie A/B/B/A de C1 (PERF_002 §13.8);
  - el código del vendor, el código vu1c generado y el ensamblador del ejecutable C1 (`c04090ca…`).
- Categorías: HECHO, INFERENCIA, HIPÓTESIS, ESTIMACIÓN, UNKNOWN. No se usan multiplicadores de velocidad no medidos.

## 1. Arquitectura actual

### 1.1 VU1

- **Despacho** (`ps2_vu1_native_dispatch.inc`, incluido solo por `ps2_runtime.cpp`). Cada `MSCAL`/`MSCNT` pasa por
  `PS2Runtime_vu1DispatchCall`.
  - En modo `native`, si existe programa compilado y la llamada es `MSCAL`, o un `MSCNT` cuya llamada anterior fue
    compilada sin fallo, guarda una instantánea (`VU1State` y los 16 KB de memoria de datos) y ejecuta el programa
    compilado; si falla, revierte y usa el intérprete.
  - En los demás casos usa el intérprete. **HECHO:** `chainBlocked` cuenta los `MSCNT` con programa compilado que
    tienen que interpretarse porque la llamada anterior fue interpretada.
- **Programa compilado** (`recomp/vu1c/vu1c_a0dbee2aeac4dafa.cpp`, generado localmente por `tools/vu1_replay/gen_vu1c.py`):
  - 21 226 líneas y 924 KB de fuente;
  - **una sola función** `static int vu1c_a0dbee2aeac4dafa(vu1c::Ctx&)` para las 15 entradas, con `switch` de
    entrada, 810 `goto` y 772 etiquetas, una por instrucción;
  - cada instrucción reproduce la temporización del intérprete con escalares (`vfR[r][lane]`, `accR`, `viR`, `cyc`,
    `pend`);
  - cada FMAC se calcula por componente en `float`, con un resultado exacto paralelo en `long double` para los
    flags.
- **Código máquina** (HECHO, mapas del enlazador): la función ocupa 229 360 bytes en el ejecutable C1. Con
  `/d2OptimizeHugeFunctions` se optimiza (sin la opción, C4883 la dejaba sin optimizar; PERF_001 §8.1).
- **Ayudas FMAC no inlineadas** (HECHO, ensamblador del ejecutable). Aunque están declaradas `inline` en
  `ps2_vu1_native.h`, el cuerpo vu1c las **llama**:

| Ayuda | Sitios de llamada en el cuerpo | % exclusivo del hilo (cinemática, sin C1) |
|---|---|---|
| `nrm` | 2 113 | 2,34 |
| `exactFlags` | 1 059 | 3,70 (más `dsign` 3,05, eliminado por C1) |
| `macBits` | 1 059 | 1,86 |
| `productSticky` | 645 | 1,99 (más `dsign` 1,08, eliminado por C1) |
| `FmacFlags::issue` | 242 | — |
| `Xgkick::advance` | 55 | — |
| `floatToInt` | — | 0,42 |

  - El cuerpo vu1c hace 5 297 llamadas en total: 4 876 a las cuatro ayudas FMAC de la tabla y 421 a otras funciones
    (`issue` 242, `Xgkick::advance` 55, `floatToInt` 32, una lambda interna 19, `clipExceeds` 18, `nrmResult` 16,
    `signbit(float)` 12, `Xgkick::start` 8, `ClipQueue` 9, `_fdsign` 4, `fesetround`/`fegetround` 3 y otras 3).
    **Corrección:** una versión anterior de esta nota hablaba de «5 297 llamadas a ayudas»; las ayudas FMAC son 4 876
    (§10).
  - **INFERENCIA:** MSVC no inlinea en una función de este tamaño, ni siquiera con `/d2OptimizeHugeFunctions`.
    Cada componente de una FMAC paga 4–5 llamadas (2–3 `nrm`, `exactFlags`, `macBits` y, en las MADD/MSUB,
    `productSticky`), con sus argumentos y su retorno por memoria o registro.
- **Intérprete** (`ps2_vu1_core.cpp`, `ps2_vu1_upper.cpp`, `ps2_vu1_lower.cpp`): ejecuta pares de instrucciones
  con un modelo de pipeline (`commitReadyPipelines` 3,07 %, `calculatePairReadyCycle` 0,65 %,
  `normalizeOperand` 0,84 %, `updateFmacFlags` 0,60 %, `calculateFmacExactResult` 0,55 %).

### 1.2 GS por CPU

- **Interfaz:** `GSRasterBackend` (`gs_backend.h`), con 15 métodos virtuales: `Initialize`, `Reset`, `Submit`,
  `BeginTransfer`, `UploadImage`, `Flush`, `TextureFlush`, `Sync`, `Present`, `ClearFramebuffer`,
  `ConsumeLocalToHostBytes`, `ReadVram`, `WriteVram`, `SnapshotVram` y `GetTransferSnapshot`.
  - La única implementación es `GSCpuBackend`.
  - `GS::setRasterBackend(std::unique_ptr<GSRasterBackend>)` (`gs_frontend.cpp:1927`) ya permite sustituirla. Usa la
    memoria de 4 MiB como formato de traspaso, y el código menciona expresamente un futuro backend GPU con
    representación privada.
- **Ruta de un píxel de triángulo** (`gs_cpu_backend.cpp`):
  - `DrawTriangle` (`:1196`) recorre la caja envolvente recortada al *scissor*.
  - Por cada píxel de la caja calcula `w0`, `w1` y `w2` en `float` y descarta los de fuera. Para los de dentro
    interpola z (`double`), color, ST/Q o UV y niebla; llama a `SampleTexture` y `combineTexture`; y llama a
    `WritePixel`.
- **`SampleTexture`** (`:969`), por muestra:
  - decodifica `CLAMP` (wrap y min/max), tamaño y `fst`, y calcula `1/q`;
  - con bilineal, 4 llamadas a `samplePoint` (salvo el atajo del parche 28). Cada una hace `wrapTextureCoordinate` ×2,
    `ReadVramUnlocked` (tabla `std::function`) y un `switch` por PSM con `applyTexa` o `LookupCLUT`;
  - después, 4 `lerpChannel` en `float` con `lroundf`.
- **`LookupCLUT`** (`:939`), por texel paletizado: `resolveClutIndex`, decodificación de `TEXCLUT` y una lectura
  `GSMem::Read*` de la VRAM con *swizzle* completo.
- **`WritePixel`** (`:773`), por píxel:
  - scissor y niebla;
  - **decodifica de nuevo** `FRAME`, `ZBUF`, `TEST` y `ALPHA`: `framePageBaseToBlock` ×2, `classifyAlphaTest`, el
    método Z, las condiciones de lectura del destino y la selección ABCD con el valor fijo, y llama 2 veces a
    `bitsPerPixel(fpsm)` (0,88 % del hilo, sin inlinear);
  - lee el destino y Z si hace falta (`std::function`), mezcla, aplica FBMSK, convierte a 16 bits y escribe color y
    Z (`std::function`).
- **`DrawSprite`** (`:1080`): rectángulo recortado; por píxel calcula la UV en `float` y llama a `SampleTexture` y
  `WritePixel` (o solo `WritePixel` si TME=0).

## 2. Cuellos de botella demostrados

**Perfil de la cinemática, % exclusivo del hilo del juego** (98 % de un núcleo; HECHO):

| Bloque | % | Detalle |
|---|---|---|
| GS triángulos PATH1 | 38,7 | `DrawTriangle` 8,1; lectores `GSMem` 10,0 (mayoría `ReadCT32`); `SampleTexture` 5,5 más su lambda 3,5; `lerpChannel` 3,7 más `lroundf` 3,2 más `roundf` 1,1; `combineTexture` 2,8; `_Do_call` 3,2; `LookupCLUT` 2,1 |
| GS sprites | 15,9 | `WritePixel` 2,95; `ReadCT32` 2,33; `SampleTexture` 1,43; `DrawSprite` 1,37 |
| VU1 compilado | 28,7 | cuerpo 12,6; ayudas FMAC 14,4 (de ellas `dsign` 4,1, ya eliminado por C1) |
| VU1 intérprete | 9,0 | 14,6 % inclusivo (incluye el GS de sus llamadas) |
| DMA, VIF, EE, GIF, audio, despachador, planificador | 7,3 | DMA 2,4 (detección de ciclos ~1,0), VIF 2,3, EE 0,9 |

**Intérprete VU1 (HECHO):**

- Cinemática: 3,0 % de las llamadas (20 129 de 663 195); 61 % `noProgram` (MSCNT 0x20e0, 0x2c10, 0x21c8, 0x2668 y
  0x26b0) y 39 % `chainBlocked`, consecuencia de las anteriores.
- Gameplay: 2,6 % de las llamadas.
- Coste: 9,0 % exclusivo y 14,6 % inclusivo, unos 23–38 ms por VSync en la cinemática (ESTIMACIÓN, × 259 ms).

**Overdraw y sprites (HECHO, contadores de la cinemática):**

- 1,96 M de `WritePixel` por VSync, en un framebuffer de 512×224 = 115 k píxeles (unas 17 veces).
- 20 sprites con 1,28 M px, iguales en número y área que en la oficina.
- **UNKNOWN:** qué capas son, con qué estado (TME, ABE, test Z y alfa) y si alguna se rechaza entera por el test
  alfa o Z. Hace falta un contador por estado antes de valorar cualquier atajo; no se asume que se puedan eliminar.

## 3. Oportunidades estructurales

### A. VU1

- **A1. Inlinear las ayudas FMAC.** Es la oportunidad con más evidencia:
  - las ayudas restantes suman ~10,3 % exclusivo (sin `dsign`) y se llaman desde 4 876 sitios (5 297 llamadas en total en el cuerpo);
  - forzar el inlining (`__forceinline` en `nrm`, `macBits`, `exactFlags`, `productSticky` y `issue`) no cambia la
    semántica: es el mismo código en el mismo orden;
  - **HIPÓTESIS:** eliminar las llamadas permite mantener los valores en registros y simplificar `macBits` con
    constantes (el carril es un literal en cada sitio);
  - **riesgo de compilación:** más código en una función ya enorme. Puede reaparecer C4883 o dispararse el tiempo y
    la memoria del compilador (PERF_001 §8.2: 1,5 GB y 2,6 min ahora). Hay que comprobarlo con `/w14883`.
- **A2. Una función por entrada en el generador.** Ataca la causa raíz: hoy la unidad es una sola función gigante,
  que provocó C4883 y probablemente impide el inlining.
  - Con funciones por entrada, o por grupo que comparta código, cada una sería mucho menor y MSVC podría inlinear y
    asignar registros con normalidad.
  - También desbloquea añadir entradas (0x20e0 y 0x2c10, que ahora se ejecutan en el castillo y se pueden verificar)
    sin volver a una función gigante.
  - No resuelve `OPMULA` (0x21c8, 0x2668 y 0x26b0), que es un trabajo aparte del generador.
  - Cambio del generador: hay que demostrar que el código emitido por entrada es equivalente (verify por entrada).
- **A3. Especializaciones exactas de flags.**
  - El comentario de `ps2_vu1_native.h` dice que solo el MAC y el estado *finales* son arquitectónicos en las
    rutas compiladas, pero los bits *sticky* exigen el OR de todas las FMAC.
  - Un análisis de vida en el generador podría omitir `macBits` en las FMAC cuyo MAC sobrescribe otra posterior,
    conservando `cur` y `extra`. Es exacto si el análisis es correcto, pero toca la semántica del generador.
  - Se plantearía después de A1 o A2.
- **A4. SIMD.**
  - Los 4 componentes de una FMAC son independientes, pero el resultado exacto en `long double` (binary64 en MSVC
    x64) obliga a usar 2 carriles `double` por registro SSE2, o 4 con AVX.
  - El orden y el redondeo deben ser idénticos: `(long double)a + (long double)b * (long double)c` sin FMA.
  - Es posible en principio, pero exige demostrar que el compilador no contrae a FMA y comparar bit a bit. Potencial
    sin medir; más complejo que A1 y A2.
- **No se propone** compilar más entradas sin resolver antes A2 y `OPMULA`.

### B. GS por CPU

- **B1. Estado de la primitiva precalculado.** Calcular una vez por `GSPrimitiveBatch` lo que hoy se decodifica por
  píxel:
  - bloque de frame y Z, `fbw`, PSM y bits por píxel;
  - método Z y si hace falta leer el destino;
  - selección ABCD y valor fijo de la mezcla;
  - FBMSK, FBA y PABE;
  - wrap y min/max de textura, tamaño, `1/W` y `1/H`, PSM de la textura y su lector.
  
  Lo que depende del píxel (`classifyAlphaTest` con su alfa, z y color) se mantiene por píxel. Es exacto por
  construcción si la aritmética por píxel no cambia; solo cambia dónde se decodifican campos puros.
  - **Cota:** `WritePixel` 4,7 % exclusivo, `bitsPerPixel` 0,9, `_Do_call` 3,2 y la parte de decodificación de
    `SampleTexture` (5,5 exclusivo). No toda es eliminable: ESTIMACIÓN de 4–8 % del hilo, incertidumbre alta.
- **B2. Lectores sin `std::function`** (C2a de PERF_002 §14), que encaja dentro de B1: 3,2 %.
- **B3. CLUT por primitiva.**
  - Hoy cada texel paletizado lee la CLUT de la VRAM con *swizzle* (`LookupCLUT` 6,0 % inclusivo).
  - Una caché de 256 entradas decodificadas por primitiva es exacta mientras la primitiva no escriba en la zona de
    la CLUT. Hay que invalidarla, o comprobar que no hay solapamiento entre el frame o Z y la CLUT.
  - El hardware real carga la CLUT en un búfer al escribir `TEX0` con `CLD`, pero aquí el criterio es la igualdad con
    la implementación actual, no con el hardware.
- **B4. Cobertura de triángulos con SIMD exacto.**
  - El recorrido de la caja calcula `w0`/`w1`/`w2` por píxel aunque el píxel caiga fuera. Evaluar 4 u 8 píxeles a la
    vez con las mismas operaciones `float` en el mismo orden (SSE escalar y SSE empaquetado dan resultados IEEE
    idénticos con `/fp:precise` si no hay FMA) produce la misma máscara de cobertura.
  - El sombreado sigue siendo escalar.
  - **UNKNOWN:** la fracción de píxeles de la caja que caen fuera; hace falta un contador.
- **B5. Interpolación de UV de sprites y bilineal:** `lerpChannel` y `lroundf` (8 % inclusivo).
  - Cambiar la fórmula exige demostrar la equivalencia en todo el dominio; se descarta como primer paso.
  - Una variante SIMD de los 4 canales con la misma fórmula `float` y el mismo redondeo es posible si se demuestra
    que `_mm_cvtps_epi32` con el modo de redondeo adecuado equivale a `lroundf`. No lo es directamente (`lroundf`
    redondea los medios lejos del cero), así que haría falta una corrección exacta.
- **B6. Bloques y cachés de textura (*tiling*):** potencial sin medir; cambia el orden de acceso pero no el
  resultado. Se dejaría para después de tener un banco de pruebas (§7, E2).

### C. GPU

- **Viabilidad de interfaz (HECHO):** la sustitución está prevista (`setRasterBackend`, `GSRasterBackend`). Un
  backend GPU tendría que implementar los 15 métodos y mantener la memoria del GS coherente donde otros la leen:
  - `SnapshotVram`: usada por la instantánea de presentación y depuración (`gs_frontend.cpp:502`) y por el traspaso
    entre backends;
  - **checkpoint:** `checkpoint_sections.cpp:437` serializa `mem.m_gsVRAM` directamente, sin pasar por el backend.
    Un backend GPU tendría que volcar su VRAM antes de guardar (`Sync`); hoy no hay ningún gancho para eso;
  - `ReadVram`/`WriteVram` desde el frontend y los stubs;
  - `ConsumeLocalToHostBytes` y `GetTransferSnapshot`: lecturas GS→host que el juego hace. **HECHO:** en el castillo
    hay transferencias local→host pendientes en casi todas las fronteras (PERF_002, causa del rechazo del punto
    seguro).
- **Dependencias que obligan a sincronizar CPU y GPU:**
  - lecturas local→host (bloqueantes para el EE);
  - transferencias local→local y texturas que leen lo que se acaba de dibujar (*render-to-texture*);
  - CLUT cargada de la VRAM;
  - mezcla y test de alfa de destino (lectura y escritura del mismo píxel dentro de una primitiva y entre primitivas
    solapadas, con orden estricto);
  - formatos con *swizzle* (PSMCT32/24/16/16S, T8/T4 con sus variantes H, Z32/24/16/16S): un *render target*
    nativo no reproduce el *swizzle*, así que la VRAM tendría que emularse en un búfer con direccionamiento
    explícito (compute) o convertirse en cada sincronización;
  - FBMSK por bit, PABE, FBA y el bit 7 de alfa en 16 bits;
  - escrituras de 24 bits que conservan el byte alto.
- **Qué se podría acelerar:** rellenos y sprites de pantalla completa sin lectura del destino, y triángulos
  texturizados de formatos de 32 bits sin dependencias de lectura dentro del lote.
  - **Qué exige sincronizar o volver a la CPU:** lecturas host, transferencias, CLUT en VRAM recién escrita y
    *feedback*.
- **D3D12, Vulkan y compute:** las tres opciones son posibles. Con compute sobre un búfer de 4 MiB con
  direccionamiento GS explícito se mantienen el *swizzle* y la exactitud byte a byte. Con *rasterización nativa*
  sobre *render targets* la exactitud sería mucho más difícil (reglas de cobertura, interpolación y redondeo
  distintos de los de `DrawTriangle`).
  - La exactitud byte a byte frente a `GSCpuBackend` solo es alcanzable con compute que reproduzca las mismas
    operaciones `float`, y aun así hay que confirmar la equivalencia IEEE en GPU (FMA, denormales, división).
  - No se elige ninguna todavía. La dependencia más fuerte es el *host*: hoy la ventana es raylib/OpenGL.

## 4. Dependencias técnicas

- **VU1:** cabecera `ps2_vu1_native.h`, que incluyen `ps2_runtime.cpp` y la unidad vu1c; generador
  `tools/vu1_replay/gen_vu1c.py` y su manifiesto; despachador; modo `verify` como oráculo; MSVC 19.44 con
  `/d2OptimizeHugeFunctions` (PERF_001 §8.3).
- **GS:** `gs_cpu_backend.cpp/.h`, cuya cabecera solo incluyen `gs_cpu_backend.cpp` y `gs_frontend.cpp`;
  `ps2_gs_memory.cpp` (lectores *swizzle*); tests GS y el corpus diferencial del parche 28. **No hay** banco de
  repetición del GS como el `vu1_replay` del VU1: cada medida exige el juego.
- **GPU:** además de lo anterior, una API gráfica nueva en el host, sincronización con el checkpoint, la presentación
  y las lecturas host. Hoy la presentación es raylib (`UploadFrame`/`UpdateTexture`, `ps2_runtime.cpp`).

## 5. Riesgos de fidelidad

- **A1:** nulo en semántica (mismo código); el riesgo es de compilación (tamaño, C4883).
- **A2:** medio. Cambia la estructura del código emitido; exige verify por entrada y cobertura de las 15 entradas.
- **A3 y A4:** medio-alto. Liveness de flags o resultado SIMD; exigen pruebas exhaustivas y verify.
- **B1, B2 y B4:** bajos si se mantiene la aritmética por píxel; exigen igualdad byte a byte de la VRAM.
- **B3:** medio por el caso de autoescritura de la CLUT.
- **B5:** alto, por el redondeo.
- **C:** alto. Otra máquina de cálculo, sincronización y formatos; requiere un modo «sombra» que compare VRAM con la
  CPU.

## 6. Costes aproximados de implementación (ESTIMACIÓN)

| Opción | Implementación | Compilación | Validación |
|---|---|---|---|
| A1 | horas | 2 unidades (`ps2_runtime.cpp` y vu1c, ~3–5 min; el riesgo de memoria y tiempo de `cl` se mide) | verify y A/B/B/A (~45 min) |
| A2 | 1–3 días | regenerar `recomp/vu1c` (segundos) y una unidad | verify por entrada y A/B/B/A |
| B1 + B2 | 1–2 días | 2 unidades (`gs_cpu_backend.cpp` y `gs_frontend.cpp`) | diferencial VRAM y A/B/B/A |
| B3 / B4 | días cada uno | 1 unidad | diferencial VRAM |
| Banco de repetición del GS (E2) | 1–2 días | instrumentación temporal en una unidad más una herramienta aparte | comparación de la VRAM con la corrida grabada |
| Prototipo GPU (compute, subconjunto) | semanas | dependencia nueva de API gráfica | modo sombra con VRAM byte a byte |

## 7. Presupuesto de tiempo (área D)

Base: el perfil de la cinemática (259 ms por VSync sin C1; los porcentajes son exclusivos y suman 100 %). Para el
gameplay solo hay relojes del despachador, no un perfil.

| Escenario (cinemática) | Lo que queda | ms por VSync | VSync/s (cota superior) |
|---|---|---|---|
| Actual (corrida A, sin C1) | 100 % | ~259 | 3,9 |
| GS a coste 0 | 45 % (VU1 37,7, resto 7,3) | ~117 | ~8,6 |
| VU1 a coste 0, compilado e intérprete | 62,3 % (el GS de las llamadas interpretadas se queda) | ~161 | ~6,2 |
| GS y VU1 a coste 0 | 7,3 % (DMA, VIF, EE, GIF, audio, despachador, planificador) | **~19** | ~53 |

- Son **cotas**, no estimaciones de ganancia: ninguna optimización real lleva un subsistema a cero.
- **Coste que queda aunque se aceleren los dos:** ~19 ms por VSync, del mismo orden que el presupuesto PAL (20 ms).
  Recorrido de DMA y detección de ciclos, VIF1 (2,28 MB por VSync), código EE (0,9 %) y despachador.
- **Gameplay (corrida G):**
  - VU1 109,8 ms; PATH1 41,8 ms; resto 64,7 ms (incluye los sprites, ~41 ms en la cinemática);
  - **INFERENCIA:** en gameplay el VU1 pesa más que el GS, lo contrario que en la cinemática;
  - con C1, el VU1 baja unos 10–12 ms (PERF_002 §13.8).
- **Datos que faltan para estimar una arquitectura GPU:**
  - número de lotes y de cambios de estado por VSync;
  - lecturas local→host por VSync y su tamaño (cada una es un punto de sincronización);
  - transferencias local→local y casos de *render-to-texture*;
  - cargas de CLUT;
  - reparto de PSM de texturas y de frame;
  - fracción de píxeles con lectura del destino (mezcla, FBMSK, test de alfa de destino) y de lotes con
    autodependencia.
  
  Sin estos datos no se puede estimar ni el beneficio ni el coste de sincronización.
- **Mayor potencial demostrado frente a complejidad:**
  - en el VU1, **A1** (~10 % del hilo en llamadas a ayudas de pocas instrucciones, exacto por construcción, una
    cabecera);
  - en el GS, **B1 + B2** (~4–8 % más 3 %), que también necesitan un banco de pruebas que hoy no existe.

## 8. Tres experimentos pequeños (por potencial y reversibilidad)

1. **E1 — Inlining forzado de las ayudas FMAC de vu1c (A1).**
   - Cambio: `__forceinline` (con *fallback* `inline` fuera de MSVC) en `nrm`, `nrmResult`, `macBits`,
     `exactFlags`, `productSticky`, `floatToInt` y `FmacFlags::issue`, en `ps2_vu1_native.h`. Sin cambio de lógica.
   - **Primer criterio, sin ejecutar el juego:** compilar la unidad vu1c con `/w14883` y contar en el ensamblador las
     llamadas a las ayudas desde el cuerpo (hoy 4 876 a las FMAC; 5 297 en total). Si MSVC no las inlinea, o si el tiempo o la memoria de `cl`
     se disparan, se abandona y se pasa a A2.
   - Si se inlinean: `ps2x_tests`, verify en la cinemática con las 15 entradas y la serie A/B/B/A en 27000–27500.
   - Reversible: una cabecera.
2. **E2 — Banco de repetición del GS** (infraestructura, sin optimizar nada).
   - Grabar, con instrumentación temporal y acotada, la secuencia de `Submit`, `BeginTransfer`, `UploadImage`,
     `ClearFramebuffer` y lecturas de una ventana corta del castillo, junto con la VRAM inicial y final.
   - Una herramienta aparte, como `tools/vu1_replay`, repite la secuencia sobre `GSCpuBackend` y comprueba la VRAM
     final byte a byte.
   - Así B1–B4 y un futuro prototipo GPU se miden y validan sin el juego y sin ruido del entorno.
   - Además da los datos que faltan en §7 (lotes, estados, PSM, lecturas del destino, CLUT).
   - Reversible: la instrumentación se retira; la herramienta queda local.
3. **E3 — `WritePixel` con estado precalculado y lectores sin `std::function` (B1 + B2)**, validado con E2.
   - El estado de la primitiva se precalcula en `DrawTriangle`/`DrawSprite`; la aritmética por píxel no cambia.
   - Igualdad byte a byte en el banco E2 y en el corpus del parche 28; después, A/B/B/A en el juego.
   - Reversible: una unidad y su cabecera privada.

**El prototipo GPU no entra entre los tres primeros.** Primero hace falta E2: sin datos de sincronización (§7) no
se puede dimensionar. La forma mínima reversible sería un backend compute en modo sombra que dibuje solo rellenos y
sprites sin lectura del destino y compare con `GSCpuBackend` en el banco E2, sin sustituirlo en el juego.

## 9. Recomendación

**Experimentar primero E1.** Es el experimento con más evidencia directa (4 876 llamadas a ayudas FMAC de pocas
instrucciones y ~10 % del hilo en ellas), exacto por construcción y validable con la infraestructura que ya existe
(verify, tests, A/B/B/A). Además, su primer criterio de éxito se comprueba sin ejecutar el juego (el ensamblador).
Si falla, el resultado justifica A2.

**En paralelo, de bajo riesgo:** E2. Es lo que hace falta para que las optimizaciones del GS (el 55 % del hilo en
la cinemática) dejen de depender de corridas de 5–9 min con ±6 % de ruido.

## 10. Experimento E1: `__forceinline` en las ayudas FMAC (2026-10-29, rama local `perf-003-e1-inline`)

**Fase A, situación de partida** (HECHO; ejecutable C1 y objeto de la unidad vu1c recompilada sin cambios de fuente):

- Recuento reconciliado del cuerpo vu1c: **5 297 llamadas = 4 876 a las cuatro ayudas FMAC** (`nrm` 2 113,
  `exactFlags` 1 059, `macBits` 1 059, `productSticky` 645) **+ 421 a otras funciones** (§1.1). El desensamblado del
  `.obj` da los mismos recuentos por ayuda.
- Función `vu1c_a0dbee2aeac4dafa`: 229 360 bytes de código máquina (mapa del enlazador); objeto de 323 822 bytes.
- Compilación de la unidad (`cl` con `/w14883`): **41,5 s, pico de ~2 030 MB** de memoria de trabajo, sin C4883.
  - **Corrección de método:** el build no usa `/GL` ni LTCG. Que antes no se pudiera desensamblar el `.obj` se debió
    a que Git Bash convirtió las opciones `/…` de `dumpbin` en rutas, no a objetos LTCG.
- Ensamblador de una MADDA típica (`analysis/local/perf003/e1/fmac_excerpt_c1.asm`):
  - `macBits` se llama con el carril como constante (`mov ecx,2`, `mov ecx,8`, `mov ecx,1`) y su resultado se combina
    con `or`;
  - `productSticky` se llama por componente con los operandos en `xmm0`/`xmm1`.

**Fase B, variante E1a:** `__forceinline` (macro `VU1C_FORCEINLINE`, `inline` fuera de MSVC) solo en `nrm` y
`exactFlags`, las dos de mayor coste; ningún otro cambio.

| Medida | Base (parche 31) | E1a |
|---|---|---|
| Llamadas a `nrm` / `exactFlags` en el objeto | 2 113 / 1 059 | **0 / 0** |
| Llamadas nuevas a `fabs(long double)` | 0 | **877** |
| `macBits` / `FmacFlags::issue` / `clipExceeds` | 1 059 / 242 / 18 | 1 088 / 279 / 24 |
| Llamadas totales en el objeto | 5 346 | 3 123 |
| Código máquina de la función | 229 360 bytes | **367 360 bytes (+60 %)** |
| Objeto | 323 822 bytes | 454 009 bytes |
| Tiempo de `cl` de la unidad vu1c | 41,5 s | **145,6 s (×3,5)** |
| Pico de memoria de `cl` | ~2 030 MB | **~8 862 MB (×4,4)** |
| C4883 | no | no |

**Fase C, guardas (HECHO):** se activan dos guardas, el consumo de memoria del compilador (8,9 GB) y un tiempo de
compilación difícil de mantener (×3,5). Además, el código crece un 60 % y aparecen llamadas a funciones que antes se
inlineaban:

- `std::fabs(long double)` pasa a ser una llamada (877);
- `macBits`, `issue` y `clipExceeds` tienen más sitios de llamada.

**INFERENCIA:** el inlinador de MSVC trabaja con un presupuesto por función. Forzar dos ayudas lo agota y desplaza
el problema a otras. La causa es el tamaño de la función única, no la falta de `inline`.

**Fase D:** no se ejecutaron las pruebas, verify ni ninguna corrida, porque el experimento se detuvo en las guardas.
La cabecera se restauró a la del parche 31 (vendor = replay de 31 parches, árbol `3c908f78…`) y el ejecutable de
trabajo se recompiló desde ella. La variante queda en `analysis/local/perf003/e1/e1a_nrm_exactflags/` (diff de la
cabecera, objeto, desensamblado, mapa y ejecutable).

**Fase E, conclusión: C, inlining contraproducente con `__forceinline`.** Las llamadas desaparecen donde se fuerza,
pero el coste de compilación (×3,5 en tiempo, ×4,4 en memoria) y el crecimiento del código (+60 %) son
desproporcionados, y el inlinador deja de inlinear otras funciones triviales. No se insiste con más
`__forceinline`.

**Cierre (decisión del usuario, 2026-10-29): E1 descartado.**

- No se harán más experimentos de inlining forzado sobre la función VU1 única.
- La reducción de llamadas (5 346 → 3 123 en el objeto) **no es una ganancia de rendimiento medida**: no se
  ejecutaron pruebas, verify ni benchmarks de E1.
- No se conserva ninguna modificación funcional de E1. La cabecera es la del parche 31, y la rama local
  `perf-003-e1-inline` no tiene commits.
- La evidencia queda en `analysis/local/perf003/e1/`.

**Siguiente experimento recomendado: A2.** El generador emite una función por entrada, o por bloque, en lugar de una
para las 15 entradas.

- Cada función sería del orden de 1/15 del tamaño actual (media de unos 15 KB de código frente a 229 KB). Con eso,
  el inlinador puede inlinear `nrm`, `exactFlags`, `macBits` y `fabs` sin forzarlo, y el compilador procesa
  funciones normales.
- El primer criterio se comprueba igual que en E1, sin ejecutar el juego: llamadas a ayudas en el objeto, tamaño,
  tiempo y memoria de `cl`.
- Después: tests, verify por entrada (las 15 se ejecutaron en la cinemática) y A/B/B/A.
- Cambio en `tools/vu1_replay/gen_vu1c.py`; no toca el vendor ni el código EE. Hay que revisar cómo comparten
  código las entradas: hoy los `goto` cruzan entre entradas (MSCAL y su MSCNT) y habría que duplicar los bloques
  compartidos o agruparlos.

## 11. Diseño mínimo de E2: banco de repetición del GS (no implementado)

**Objetivo:** capturar una secuencia corta de comandos del GS en el juego y reproducirla fuera de él sobre
`GSCpuBackend` (o una variante), comparando la VRAM byte a byte.

- Lo que demuestra la igualdad: **ausencia de regresión frente al backend actual**, no fidelidad frente a PCSX2 ni
  frente al hardware.

**Punto de captura** (HECHO, interfaces):

- Todo el trabajo del GS pasa por `GSRasterBackend`. `GS::setRasterBackend()` permite instalar un backend
  envoltorio, y `PS2Runtime::gs()` es accesible desde `runtime/`.
- **Grabador:** un `GSRecordingBackend : GSRasterBackend` que reenvía cada llamada a un `GSCpuBackend` real y la
  escribe en un fichero. Se instalaría desde `runtime/` como instrumentación temporal, activada por variable de
  entorno y por ventana de VSync. **No modifica el vendor ni el comportamiento**: el backend real sigue haciendo el
  trabajo.
- **Qué se graba:**
  - al empezar: la VRAM completa (4 MiB, por `SnapshotVram`) y el estado de transferencia (`GetTransferSnapshot`);
  - en orden, cada llamada con sus argumentos: `Submit` (el `GSPrimitiveBatch`, que no tiene punteros ni
    contenedores), `BeginTransfer`, `UploadImage` (con sus bytes), `ClearFramebuffer`, `WriteVram`, `Flush`,
    `TextureFlush`, `Sync` y `Present` (solo la petición);
  - las **salidas** observables, para comprobarlas: valores de `ReadVram`, bytes de `ConsumeLocalToHostBytes`,
    `GetTransferSnapshot` y el hash de la VRAM en cada `Present`, es decir por fotograma;
  - al terminar: la VRAM completa.
- **Dependencias de VRAM:** quedan preservadas porque se reproduce el mismo orden de llamadas sobre la misma VRAM
  inicial.
  - Las lecturas del destino, la CLUT desde la VRAM, las transferencias local→local y local→host y el
    *render-to-texture* ocurren dentro del backend y se reproducen solas.
  - Las escrituras directas a la VRAM fuera del backend (`m_gsVRAM` desde otras partes del runtime) romperían la
    reproducción. Para detectarlas, se compara el hash de la VRAM antes de cada llamada grabada con el del
    reproductor en el mismo punto, al menos por fotograma.
- **Reproductor:** una herramienta aparte (como `tools/vu1_replay`) enlazada con la biblioteca del runtime.
  - Carga la VRAM inicial, inicializa el backend con ella, ejecuta la secuencia y compara las salidas grabadas y la
    VRAM final y por fotograma, informando de la primera divergencia (llamada, primitiva y dirección).
  - Modo `--bench`: repite la secuencia N veces y mide el tiempo sin el resto del emulador, sin ruido del VU1 ni del
    planificador.
  - Modo `--stats`: lotes por tipo, estados, PSM de textura y de frame, píxeles con lectura del destino, cargas de
    CLUT y lecturas host. Son los datos que faltan en §7.
- **Tamaño (ESTIMACIÓN):** 500 VSync con ~18 k triángulos y 20 sprites por VSync. Los lotes son de 3 vértices más
  el estado, del orden de cientos de bytes, así que se esperan **varios GB**. Para un primer banco basta con **10–30
  VSync**, más compresión simple.
- **Coste:** el grabador (una unidad en `runtime/`, temporal) y la herramienta (una unidad nueva, local). Captura de
  unos 5 min en el juego. No toca el vendor, los parches ni el código EE.
- **Riesgos:**
  - estado del backend fuera de la interfaz: el reproductor debe partir de un backend recién creado más la VRAM y el
    estado de transferencia grabados, y hay que confirmar que `GSCpuBackend` no tiene más estado entre lotes (por
    ejemplo `m_localToHostBuffer`);
  - concurrencia: `Present` llega desde el hilo de render; se graba con su orden relativo, protegido por el mismo
    mutex.
- **Uso posterior:** validar E3 (B1 + B2) y B3/B4 byte a byte y medirlos sin ruido; más adelante, como banco de un
  prototipo GPU en modo sombra.

## 12. E2 implementado: banco de repetición del GS (2026-10-29, rama local `perf-003-gs-replay`, sin commits)

Contexto: PR #11 (C1, parche 31) fusionada en `master` (`dea53cc65ae47dad870bebd2e387f8af698cc97a`). La rama de E2
sale de ese `master` y no contiene nada de E1.

### 12.1 Revisión de la interfaz (etapa A, HECHO)

- **`GSRasterBackend`** (`gs_backend.h`), 15 métodos virtuales. Clasificación:

| Método | VRAM | Estado interno | Argumentos con indirección |
|---|---|---|---|
| `Initialize(vram, size)` | adopta un puntero **externo** | puntero y tamaño | puntero a la memoria del GS de `PS2Memory` (`m_gsVRAM`), compartida |
| `Reset` | — | borra la transferencia y el búfer local→host | — |
| `Submit(batch)` | escribe y lee (destino, Z, textura, CLUT) | — | ninguna (`GSPrimitiveBatch` es trivialmente copiable) |
| `BeginTransfer(cmd)` | local→local escribe; local→host lee | `m_transfer`, `m_transferState`; dir 1 rellena el búfer | ninguna |
| `UploadImage(data, n)` | escribe | avanza la posición | **puntero y tamaño: copia profunda** |
| `Flush`, `TextureFlush`, `Sync` | — (vacíos en la CPU) | — | — |
| `Present(req)` | lee | — | devuelve `PresentationFrame` con `std::vector` (salida) |
| `ClearFramebuffer(ctx, rgba)` | escribe | — | ninguna |
| `ConsumeLocalToHostBytes(dst, n)` | — | avanza la lectura | **búfer de salida: copia** |
| `ReadVram`, `SnapshotVram` | leen | — | `SnapshotVram`: vector de salida |
| `WriteVram` | escribe | — | — |
| `GetTransferSnapshot` | — | lee el estado oculto | — |

- **Estado persistente oculto de `GSCpuBackend`:** `m_transfer`, `m_transferState` y `m_localToHostBuffer` con su
  posición de lectura. No se puede leer por la interfaz, salvo el resumen de `GetTransferSnapshot`.
- **Frontend frente a backend:** registros, cola de vértices y estado de dibujo son del frontend
  (`GS`/`gs_frontend.cpp`); el backend recibe lotes ya resueltos.
- **Fuera del envoltorio:** escrituras directas a `m_gsVRAM` en el arranque (`memset`) y al cargar un checkpoint
  (`checkpoint_sections.cpp:437`). No se encontraron otras en el código.
- **Orden:** el frontend llama desde el hilo del juego; `Present` desde el de render. El envoltorio serializa con un
  mutex la llamada y su registro, así que el orden grabado es el ejecutado.
- **Sustituir el backend en caliente pierde el estado oculto.** Por eso el grabador se instala al arrancar el proceso,
  antes de cargar el checkpoint, y siempre trabaja con su propio `GSCpuBackend`.

### 12.2 Arquitectura

**Grabador** (`tools/gs_replay/gs_replay.h`, una sola cabecera compartida):

- `gsreplay::RecordingBackend : GSRasterBackend` reenvía cada llamada a su `GSCpuBackend` interno sin modificarla.
- Fuera de la ventana solo lleva la cuenta del último `BeginTransfer` y del tamaño de cada `UploadImage` y de los
  bytes consumidos desde entonces.
- **Inicio de la ventana:** en la primera llamada con tick ≥ `fromTick` sin una lectura local→host con bytes
  pendientes. Escribe:
  - `Header` (magia, versión, tamaños de los 5 structs, tick inicial);
  - `Prologue` (último comando, tamaño de cada subida, bytes consumidos e instantánea de la transferencia);
  - `InitVram` (4 MiB).
- **Durante la ventana**, cada llamada con sus argumentos y copias profundas de los *payloads*, y sus salidas
  observables:
  - valor de `ReadVram`;
  - bytes de `Consume`;
  - `TransferSnapshot`;
  - hash de `SnapshotVram`;
  - en `Present`: hash de los píxeles presentados y de la VRAM.
- **Fin:** `End` (VRAM final) y un `Footer` con el número de registros y el FNV-1a de todo lo anterior. El registro
  se acumula en memoria y se escribe una sola vez.
- **Fallos registrados:** `Initialize` dentro de la ventana, límite de tamaño, ningún arranque representable en el
  plazo de espera, o error de escritura.
- **Gancho temporal en el juego** (`runtime/perf003_gs_capture.inc` más 2 líneas en `dmc_overrides.cpp`): activado
  por `DMC_GS_CAPTURE`, ventana de 1–2 VSync y límite en MB. **Retirado** tras la captura; copia en
  `analysis/local/perf003/e2/retired_hook/`.

**Reproductor** (`tools/gs_replay/main.cpp`, `CMakeLists.txt`; enlaza con un build del vendor ya compilado, como
`tools/vu1_replay`):

- `parse`: magia, versión, tamaños de struct, encuadre de registros, hash y número de registros del pie, presencia de
  `Prologue`/`InitVram`/`End`. Un fallo aquí es **error de formato (código 2)**.
- **Prólogo:** un `GSCpuBackend` nuevo repite `BeginTransfer` y las subidas (con ceros) y los consumos para recrear el
  estado oculto; después restaura la VRAM grabada y compara la instantánea de transferencia. Si no coincide, también
  es código 2.
- **Reproducción en orden:** compara cada salida observable y, en cada `Present`, el hash de la VRAM y de los
  píxeles; en `End`, la VRAM byte a byte (primer desplazamiento distinto). Una diferencia es **divergencia del
  backend (código 1)** y se informa de la primera.
- Las lecturas intermedias se comparan una a una; la igualdad final no se usa como sustituto.
- **Opciones:**
  - `--stats`: recuentos derivados de la captura;
  - `--bench N`: solo se ejecuta si la reproducción es exacta;
  - `--continue`, `--dump-final`;
  - `--allow-unobserved-stale-l2h` (§12.4);
  - `--rounding-heuristic` (solo diagnóstico, §12.5).

### 12.3 Pruebas sintéticas (HECHO, `gs_replay selftest`)

Escenas a través del frontend real (`GS::writeRegister`/`processGIFPacket`) con el grabador instalado como backend:

- triángulo Gouraud;
- dos triángulos con Z GEQUAL;
- sprite con mezcla alfa;
- textura CT32 bilineal (sprite UV);
- triángulo T8 con CLUT CT32 (ST/Q);
- transferencias host→local, local→local y local→host (leída en dos partes);
- `ClearFramebuffer`, `WriteVram`/`ReadVram`, `SnapshotVram`, `GetTransferSnapshot` y 3 `Present`;
- una subida a medias **antes** de la ventana, para probar el prólogo (32 de 64 píxeles).

Resultado: 10/10 PASS.

- 29 registros: 6 `Submit`, 5 `BeginTransfer`, 3 `Upload`, 2 `Consume`, 3 `Present`…
- El prólogo contiene la transferencia en curso.
- **Reproducción idéntica**, y el registro `End` es igual a la VRAM real.
- **Prueba negativa:** omitir el primer dibujo se detecta como divergencia en el primer `Present`.
- **Byte alterado:** se detecta como error de formato (hash de integridad).

### 12.4 Captura real (HECHO, una corrida de 71 s)

- Ejecutable de captura `ff1f89ab…` (`master` más el gancho; solo se recompiló `dmc_overrides.cpp`).
- Checkpoint `g002_castle_entry.dmck`, sin entrada, ventana **VSync 27100–27102** (cinemática 3D del castillo).
- La corrida se detuvo al escribirse la captura. 0 FATAL.
- Captura `analysis/local/perf003/e2/cap1/castle_t27100_n2.gsrp`: **26 932 registros, 17,6 MB**:
  - 26 855 `Submit`, 30 `BeginTransfer`, 26 `Upload`, 12 `TextureFlush`;
  - 2 `Present`, 2 `Sync`, 2 `Flush`;
  - ningún `Consume` ni `TransferSnapshot` dentro de la ventana.

**Primera dependencia que falta (1): búfer local→host obsoleto.**

- Al abrir la ventana, el backend tenía **262 144 bytes local→host pendientes**, aunque la última transferencia era
  una subida terminada (`dir=3`).
- `BeginTransfer` solo vacía ese búfer en una nueva transferencia local→host; una lectura GS→host anterior que no se
  consumió entera sigue pendiente indefinidamente.
- El grabador solo esperaba cuando `direction == 1` (defecto del criterio de arranque) y el prólogo no puede
  reconstruir su contenido, que viene de la VRAM de un momento anterior.
- El reproductor lo detecta como error de prólogo (código 2).
- Como ninguna operación de la ventana lo observa, `--allow-unobserved-stale-l2h` acepta esa única diferencia y
  marcaría como divergencia cualquier `Consume` o `TransferSnapshot` posterior.
- **INFERENCIA:** es el mismo estado que hace que el punto seguro del checkpoint rechace siempre en el castillo
  (`GS local->host buffer not fully consumed`, PERF_002 y GAMEPLAY_002): 256 KB leídos del GS que no se consumen
  hasta la siguiente lectura. No se ha comprobado quién inicia esa lectura ni cuánto consume.

**Primera divergencia del backend (2): modo de redondeo de la FPU.** *(Actualización: causa demostrada en §13.4.)*

- Con la opción anterior, la reproducción **diverge en el primer `Present`** (registro 15 056, tick 27101).
- La VRAM final difiere en **352 bytes**, repartidos en 81 bloques de 256 bytes (de 1 a 9 bytes por bloque, en las
  mismas posiciones relativas de cada página): píxeles sueltos, no escrituras ajenas al backend.
- **HECHO (código):**
  - el intérprete VU1 pone `FE_TOWARDZERO` durante `run()` (`ps2_vu1_core.cpp:1633`, restaurado en `:1837`);
  - el código VU1 compilado usa `RoundingScope` (`ps2_vu1_native.h:374`).
- Los dibujos PATH1 que ocurren dentro de esas ejecuciones calculan con redondeo hacia cero; el reproductor lo
  ejecuta todo con redondeo al más cercano.
- **HECHO (contraste, `--rounding-heuristic`):** reproducir todos los triángulos con redondeo hacia cero y los
  sprites al más cercano empeora la diferencia (66 554 bytes). El rasterizador **depende del modo de redondeo**,
  pero la mayoría de los dibujos se hace con redondeo al más cercano.
- **INFERENCIA:** solo una minoría de lotes se dibuja con redondeo hacia cero, probablemente los XGKICK emitidos
  dentro del intérprete (~3 % de las llamadas VU1). Los del código compilado se envían después, como paquetes
  retenidos, fuera del ámbito de redondeo. **No está demostrado lote a lote.**
- **Decisión:** identificar el modo de cada lote exige que el grabador registre el entorno de coma flotante
  (`fegetround()` y, por prudencia, MXCSR con FTZ/DAZ) en cada llamada. Es una **ampliación de la instrumentación**;
  conforme a las instrucciones, **se detiene aquí**.

**Estadísticas de la captura** (HECHO, derivadas del flujo grabado, no de la reproducción; 2 VSync):

| Medida | Valor |
|---|---|
| Lotes tristrip / sprite | 14 993 / 21 |
| Con textura / bilineales | 15 003 / 15 003 |
| Con mezcla (ABE) | 960 |
| Test Z: NEVER / ALWAYS / GEQUAL / GREATER | 8 / 3 / 14 253 / 750 |
| Píxeles recorridos por triángulos (caja envolvente recortada, como `DrawTriangle`) | 3 981 093 |
| Píxeles de sprites (rectángulo recortado) | 1 396 001 |
| Píxeles con textura CT32 / **T8** / T4 | 350 851 / **3 638 410** / 335 161 |
| Píxeles con lectura del destino (ABE o FBMSK) | 1 494 547 |
| Píxeles con lectura de Z | 4 115 526 |
| Transferencias host→local / local→local / local→host | 13 / 3 / 0 |
| Bytes subidos | 358 400 |

- **Observación:** en esta ventana dominan las texturas **paletizadas T8** (84 % de los píxeles con textura), así que
  la ruta `LookupCLUT` (B3) pesa más de lo que sugería el perfil por función.
- **Etapa E (medidas de GS aislado) no realizada:** exige una reproducción exacta, que todavía no se tiene. No se
  publican tiempos.

### 12.5 Limitaciones del banco (también para un futuro backend GPU)

1. **Entorno de coma flotante por llamada** (§12.4): sin él, la igualdad byte a byte no es alcanzable con capturas
   reales. Un backend GPU tendría el mismo problema en otra forma: para compararse con la CPU tiene que reproducir
   el modo de redondeo de cada lote, o se acepta que la referencia depende del hilo y del momento.
2. **Búfer local→host obsoleto:** las ventanas que empiecen con él solo son comparables si nada lo observa.
3. **Estado del frontend:** se graban los lotes ya resueltos; el banco no prueba el frontend (registros, cola de
   vértices) ni el VU1.
4. **Presentación:** se compara el hash de los píxeles presentados, pero no se valida su contenido visual.
5. **Tamaño:** ~4,8 MB de llamadas por VSync en esta escena, más 8 MB fijos de VRAM inicial y final; ventanas
   largas requieren compresión.
6. **Para una GPU:** el banco da la secuencia y el oráculo de VRAM, pero no mide la sincronización real con el EE
   (lecturas local→host bloqueantes, *render-to-texture*), porque reproduce sin el emulador.

### 12.6 Estado y siguiente paso propuesto

- **Archivos locales sin commit:**
  - `tools/gs_replay/` (`gs_replay.h`, `main.cpp`, `CMakeLists.txt`, `function_table_stub.cpp`);
  - captura, informes y scripts en `analysis/local/perf003/e2/`.
- El runtime vuelve a coincidir con `master`; el vendor es el replay de 31 parches.
- **Siguiente paso** (pendiente de autorización):
  1. Ampliar el grabador para registrar el modo de redondeo (y MXCSR) en cada llamada como un registro `FpEnv` cuando
     cambie, y que el reproductor lo aplique.
  2. Corregir el criterio de arranque para que tenga en cuenta los bytes pendientes en cualquier dirección.
  3. Repetir **una** captura corta (≤ 5 min).
  
  Si la reproducción es entonces idéntica, se pasa a la etapa E.

## 13. E2 completado: reproducción exacta de una captura real (2026-10-29)

### 13.1 Ampliación del grabador y del reproductor (formato v2)

**Entorno de coma flotante:**

- Cada llamada registra a la entrada, sin modificarlo, el entorno del hilo que llama:
  - `MXCSR & 0xFFC0` (DAZ, máscaras de excepción, control de redondeo, FTZ);
  - la palabra de control del CRT (`_control87(0, 0)`);
  - `fegetround()`.
- Se escribe un registro `FpEnv` cuando cambia. Si una llamada termina con un entorno distinto del de entrada, se
  escribe `FpEnvChanged`, solo como observación.
- **HECHO (cabeceras de UCRT 10.0.26100):** `__control87_2`, la única función que separa la palabra de control x87, solo
  existe para `_M_IX86`. En x64 la palabra de control x87 no se puede leer con el CRT.
  - MSVC x64 hace toda la aritmética `float`/`double`/`long double` con SSE, así que la x87 no influye en los
    resultados del rasterizador.
  - La palabra de control del CRT se registra como comprobación cruzada: después de aplicar MXCSR, el reproductor
    comprueba que coincide.
- **Reproductor:**
  - aplica los bits de control de MXCSR grabados antes de las llamadas siguientes, sin tocar los indicadores de
    estado;
  - guarda el MXCSR de su hilo al empezar y lo restaura al terminar (RAII);
  - nunca fija un único modo para toda la captura.

**Búfer GS→host:**

- **Caso A (reconstruible):** el grabador copia la VRAM justo antes de cada `BeginTransfer` local→host, que es la
  fuente exacta del búfer, y cuenta los bytes consumidos de ese búfer.
- Si al abrir la ventana hay bytes pendientes, cualquiera que sea la dirección de la última transferencia, escribe
  `L2HState` con ese comando, la VRAM y los bytes consumidos.
- El reproductor regenera el mismo búfer y la misma posición de lectura con la propia función del backend, y después
  rehace la última transferencia y restaura la VRAM de la ventana. No hay datos inventados.
- **Caso C:** bytes pendientes cuyo origen no se observó (solo es posible si el grabador se instala después de una
  lectura). La ventana no se abre y la captura falla tras el plazo de espera.
- **Caso B** (excepción por no observación): ya no hace falta. La opción `--allow-unobserved-stale-l2h` de §12 se
  conserva solo para la captura antigua.
- **Criterio de arranque corregido:** se miran los bytes pendientes sin depender de la dirección.

**Localización de divergencias:** `VramHash` (hash de la VRAM completa) después de cada 16 llamadas que modifican la
VRAM. El reproductor informa del primer intervalo de registros que contiene el primer estado distinto, en lugar del
primer `Present`.

### 13.2 Pruebas sintéticas (HECHO, 24/24)

- Las 10 anteriores siguen pasando.
- **Nuevas de entorno FPU:**
  1. La escena (40 triángulos texturizados, bilineales, Gouraud, con vértices subpíxel) es sensible al redondeo:
     **2 268 bytes de VRAM** difieren entre redondeo al más cercano y hacia cero.
  2. La misma escena grabada en tres partes (más cercano; hacia cero; hacia arriba con FTZ y DAZ) registra 3 entornos
     distintos y ningún cambio dentro de las llamadas.
  3. La reproducción con los entornos grabados es idéntica (5 hashes intermedios).
  4. Con `--ignore-fpenv` diverge, localizada en los registros 38–54, que son exactamente las llamadas 33–48, donde
     empieza la parte con redondeo hacia cero.
  5. El entorno del proceso no cambia ni tras el *selftest*, ni tras la reproducción buena, ni tras la negativa.
- **Nuevas de GS→host (caso A):**
  - una lectura de 32×32 CT32 consumida a medias (1 000 bytes) antes de la ventana, seguida de una subida (última
    transferencia `dir=0`) y de un dibujo que cambia la VRAM de origen;
  - dentro de la ventana se consumen los 3 096 bytes restantes y se pide `GetTransferSnapshot`;
  - la captura contiene `L2HState` y la reproducción da **los mismos bytes y la misma instantánea**.
- **No hay prueba sintética del caso C:** con el grabador instalado desde el principio no se puede producir. El código
  lo rechaza, pero no está ejercitado.

### 13.3 Segunda captura real (HECHO, una corrida)

- Ejecutable `master` más el gancho v2. Mismo checkpoint, ventana **VSync 27100–27102**, sin entrada, límite de
  5 min; la corrida se detuvo al escribirse.
- Al abrir la ventana había **262 144 bytes GS→host pendientes**, y esta vez se capturaron como caso A.
- `castle_t27100_n2.gsrp` (en `cap2/`): **28 631 registros, 21,9 MB**; 0 cambios de entorno durante las llamadas.
- **Observación:** con la misma ventana de ticks, esta captura tiene 26 855 `Submit` y la primera 14 993. **UNKNOWN:**
  el trabajo que cae en un mismo intervalo de ticks varía entre corridas; no se ha investigado.

### 13.4 Equivalencia (HECHO)

`gs_replay replay cap2/castle_t27100_n2.gsrp` da **IDENTICAL**:

- 0 divergencias en el prólogo (estado de transferencia y búfer GS→host incluidos);
- **1 681 hashes intermedios de VRAM**, 2 `Present` (imagen y VRAM) y la VRAM final byte a byte;
- dentro de la ventana no hubo `Consume` ni `TransferSnapshot`; las pruebas sintéticas (§13.2) los cubren.

**Entornos FPU observados:**

| Llamadas | Entorno |
|---|---|
| 26 493 `Submit` y todas las demás llamadas | redondeo al más cercano (`MXCSR` 0x1F80, CRT 0x8001F) |
| **362 `Submit` (1,35 %)** | **redondeo hacia cero** (`MXCSR` 0x7F80, CRT 0x8031F) |

- Están en 8 ráfagas, 4 por VSync, de 30 a 80 lotes cada una.
- Nunca se vio FTZ ni DAZ.

**Causa demostrada de los 352 bytes:**

- La misma captura reproducida con `--ignore-fpenv` diverge en **exactamente 352 bytes y 81 bloques**, lo mismo que
  en la primera captura.
- La primera divergencia está en el intervalo de registros 1878–1893, y el registro 1878 es **el primer `Submit` con
  redondeo hacia cero**.
- Los dibujos que el VU1 emite con `FE_TOWARDZERO` activo dan resultados distintos de los del redondeo al más
  cercano, y eran la única diferencia.
- **INFERENCIA, no demostrada lote a lote:** las ráfagas son XGKICK emitidos dentro del intérprete VU1
  (`ps2_vu1_core.cpp:1633`). Las del código compilado se envían después, como paquetes retenidos.
- **Implicación para la fidelidad (fuera del alcance de E2):** el resultado del rasterizador depende de qué ruta del
  VU1 emitió el dibujo (intérprete o compilado). Es un comportamiento del runtime actual, no un defecto del banco.

**Búfer GS→host y checkpoint (HIPÓTESIS, aparte):** en las dos capturas, al llegar al tick 27100 había 256 KB de una
lectura GS→host sin consumir, conservados por el backend a través de transferencias posteriores. Es compatible con el
rechazo permanente del punto seguro en el castillo («GS local->host buffer not fully consumed»). No se ha comprobado
quién pide esa lectura, cuánto consume ni si el hardware se comportaría igual.

### 13.5 GS aislado (fase 6; HECHO salvo donde se indica)

**Carga fija:** la captura `cap2` (2 VSync) reproducida sobre `GSCpuBackend` (Release, el mismo código del juego), en
el mismo hilo, sin el resto del emulador.

| Medida (5 repeticiones) | ms |
|---|---|
| Backend sin verificación | 303,3 / 303,8 / 313,8 / 314,2 / 323,0 (**~155 ms por VSync**) |
| Pared sin verificación (backend más lectura de registros) | 314,8–335,0 |
| Backend con verificación | 321,1–344,1; comprobaciones de `Present`/`End` 16–20 ms; los 1 681 hashes intermedios llevan la pared a ~9,4 s |
| Backend con todo al más cercano (`--ignore-fpenv`, solo tiempo) | 308,7–336,4 |

- **Influencia del entorno FPU en el tiempo:** no medible. La diferencia entre entorno grabado y todo al más cercano
  (≤ 10 ms) es del orden de la dispersión entre repeticiones (~20 ms).
- **Contraste con el juego (INFERENCIA):** ~155 ms por VSync del GS aislado en la cinemática, frente al ~55 % de
  ~250 ms (~140 ms) del perfil en el juego. Es coherente, aunque son ventanas distintas.

**Tiempo por clase de lote** (una reproducción cronometrada lote a lote, 310 ms):

| ms | Lotes | Clase |
|---|---|---|
| 82,4 | 15 598 | tristrip T8 bilineal, Z GEQUAL |
| 52,9 | **4** | sprite CT32 bilineal con mezcla, Z NEVER (ZTST 0) |
| 42,7 | **56** | tristrip T8 bilineal con mezcla, GEQUAL, **redondeo hacia cero** |
| 39,7 | **66** | tristrip T8 bilineal, GEQUAL, **redondeo hacia cero** |
| 33,5 | 7 472 | tristrip T4 bilineal, GEQUAL |
| 18,1 | 16 | sprite CT32 bilineal con mezcla, GEQUAL |
| 15,8 | 8 | sprite sin textura con mezcla, ZTST 0 |
| 12,0 | 4 | sprite sin textura, ZTST 1 |
| resto | ~3 650 | < 6 ms por clase |

- **HECHO:** 126 lotes, el 0,5 % del total (los 122 dibujados con redondeo hacia cero más 4 sprites), suman **135 ms
  (44 %)**. Son primitivas muy grandes: 0,3–13 ms cada una, frente a ~5 µs de un lote medio.
- **HECHO:** la ruta T8 (paletizada) domina tanto en píxeles (§12.4) como en tiempo.
- **Nota de método:** con ZTST=0 (NEVER), `WritePixel` descarta el píxel después de muestrear la textura y combinar el
  color. Esos 4 sprites (52,9 ms) **se calculan sin escribir nada** en el test Z. **INFERENCIA:** el coste se pierde
  porque la decisión llega al final de la tubería por píxel. Que el resultado sea correcto respecto al GS (ZTE, ZTST) es
  otra cuestión y no se ha evaluado.

**Perfil de funciones** (muestreo de hoja de 1 ms durante 10 reproducciones; 4 829 muestras):

- El muestreo encarece la reproducción (468 frente a ~310 ms), así que **solo se usan porcentajes relativos**.
- Coste exclusivo, sin pila: las funciones del CRT (`lroundf`, `floorf`…) aparecen como `ucrtbase.dll`.

| % | Función o grupo |
|---|---|
| **20,7** | **despacho de acceso a VRAM:** `ReadVramUnlocked` 11,0; `std::function::_Do_call` de lectura 5,9; `WriteVramUnlocked` 2,1; `_Do_call` de escritura 1,7 |
| 15,9 | `SampleTexture` 9,1 más su lambda `samplePoint` 6,8 |
| 13,1 | `WritePixel` |
| ~8,5 | lectores y escritores con *swizzle* (`ReadCT32` 4,8, `ReadP8` 1,6, `ReadZ16S`, `ReadP4`, escrituras) |
| 8,3 | `DrawTriangle` |
| 13,3 | `lerpChannel` 6,4 más CRT 6,9 (`lroundf`/`roundf`) |
| 6,7 | `combineTexture` |
| 3,6 | `LookupCLUT` (exclusivo; sus lecturas de VRAM están en las filas de despacho y lectores) |
| 2,7 | `DrawSprite` |
| 0,6 | `bitsPerPixel` |

- **Corrección respecto a §2 y PERF_002 §14:** el perfil del juego atribuía al despacho de VRAM ~3,2 %
  (`_Do_call`). Allí `ReadVramUnlocked` aparecía con un nombre no resuelto. Medido de forma aislada, el despacho es
  **~21 % del GS**.
- **No medido:** el coste inclusivo de `LookupCLUT`, porque el muestreador no desenrolla la pila.

### 13.6 Limitaciones para un futuro backend GPU

- **El entorno FPU forma parte de la referencia:** para igualar `GSCpuBackend` byte a byte, un backend GPU tendría
  que reproducir el redondeo hacia cero de los lotes que el VU1 emite así (1,35 % de los lotes y el 26 % del tiempo en
  esta captura).
- **El búfer GS→host persiste entre transferencias:** cualquier backend debe reproducir esa persistencia, o el estado
  observable (`GetTransferSnapshot`, checkpoint) cambia.
- **Variación del contenido:** la misma ventana de ticks contiene un trabajo distinto en cada corrida (§13.3); los
  bancos deben compararse sobre la misma captura, no sobre «el mismo tick».
- El banco no prueba el frontend, el VU1 ni la sincronización con el EE.
- El muestreador es de hoja y con sobrecarga; para costes inclusivos haría falta desenrollar la pila, como el
  perfilador de PERF_001.

### 13.7 Estado

- **E2: reproducción exacta conseguida** en una captura real del castillo, con 24/24 pruebas sintéticas.
- **Archivos locales sin commit:** `tools/gs_replay/` (grabador v2, reproductor, perfil por clase y RIP).
- **Evidencia** en `analysis/local/perf003/e2/`: `cap1/`, `cap2/` con la captura, los informes y el perfil, scripts y
  `retired_hook/`.
- El gancho está retirado del runtime (copia en `retired_hook/`) y el ejecutable de trabajo se recompila sin él.
- Vendor = replay de 31 parches. Sin parches nuevos, sin cambios en el rasterizador y sin commits.

## 14. Reorganización de E3: ZTE/ZTST, despacho de VRAM, PCSX2 y paraLLEl-GS (2026-10-29, sin cambios funcionales)

### 14.1 Los sprites con ZTST=0 no son una optimización: probable error gráfico

**HECHO (captura `cap2`, `gs_replay replay … --dump-znever`):** las 16 primitivas con campo ZTST=0 (4 por cada medio
fotograma) son sprites de pantalla completa, 512×224, con scissor 0..511/0..223:

- **todas con `TEST.ZTE = 0`** (`TEST` = 0x1003 o 0x1001: ATE=1, ATST ALWAYS o NEVER, AFAIL=1 FB_ONLY);
- tres pasadas sin textura sobre un búfer auxiliar (`FRAME.FBP=0xA0`), colores 128/111/239 con A=0 y ALPHA 0x62,
  0x29 y 0xA1;
- una cuarta pasada texturizada que **lee ese mismo búfer** (`TEX0.TBP0=0x1400 = 0xA0×32`, CT32 512×256) y lo mezcla
  sobre el framebuffer (`FBP` 0x0 o 0x38) con ALPHA 0x62.

**INFERENCIA:** es un efecto de posprocesado a pantalla completa (tinte, brillo o fundido) construido en un búfer
auxiliar y compuesto sobre la imagen.

**Semántica (HECHO, código de PCSX2 en el commit `1f67c53b`):**

- **Renderer SW** (`GSRendererSW.cpp`):
  - con ZTE=0, la máscara de Z se bloquea (`zm = ZBUF.ZMSK || TEST.ZTE == 0 ? 0xffffffff : 0`, l. 955);
  - el test solo se activa con `ZTE && ZTST > ALWAYS` (l. 1008) y el selector usa ALWAYS (l. 1335–1342);
  - **el color se escribe; Z no**.
- **Renderer HW:** igual (`ztst = ZTST_ALWAYS` si ZTE=0). El comentario literal de `GSRendererHW.h` es «ZTE == 0 is
  bug on the real hardware, write is blocked then».
- **Descarte temprano:** `GSState::FlushPrim` solo omite la primitiva si `ZTE && ZTST == NEVER` (`GSState.cpp`,
  l. 2636–2658).

**HECHO (nuestro runtime):** `WritePixel` usa `(test >> 17) & 3` sin mirar ZTE (`gs_cpu_backend.cpp:804`). Con ZTST=0
devuelve antes de escribir, así que **estos sprites nunca dibujan nada**.

**Conclusión:**

- **HIPÓTESIS (alta confianza):** DMC pierde un efecto de posprocesado a pantalla completa en el castillo, y quizá en
  más escenas. Encaja con los defectos visuales pendientes (oscuridad o tinte; VISUAL_004/005/006), pero **no está
  comprobado visualmente**.
- La «optimización Z-NEVER» de §13.5 **no se debe implementar tal como se planteó**. Descartar sin mirar ZTE
  consolidaría el error. El descarte seguro es el de PCSX2 (`ZTE=1 && NEVER`), que no aparece en esta captura.
- Corregir la semántica de ZTE es un **cambio de fidelidad**, no de rendimiento, y **aumentará** el coste del GS: esos
  sprites se dibujarán.
- **Pruebas necesarias:** comparar con PCSX2 el fotograma del castillo (savestate s09 o la misma escena); después,
  diferencial en el banco (la VRAM cambia solo en esos sprites).

### 14.2 C2a: qué parte del 21 % es despacho

- **HECHO (ensamblador de `gs_replay.exe`, mismo código que el juego):**
  - `GSCpuBackend::ReadVramUnlocked` es despacho puro: comprueba `m_vram`, indexa la tabla, **guarda los 5 argumentos
    en la pila** (`std::function` los pasa por referencia) y hace una llamada virtual a la implementación
    (`call [rax+10h]`);
  - `_Func_impl_no_alloc::_Do_call` vuelve a leer los argumentos de la pila y hace un salto indirecto (`jmp r10`) al
    lector real;
  - por tanto, **las cuatro filas «despacho» del perfil (20,7 %) no contienen trabajo de VRAM**. El trabajo real está
    en `ReadCT32`/`ReadP8`/… (~8,5 %).
- **HECHO (microbenchmark, `gs_replay microbench`, 16 M de lecturas, 3 repeticiones, recorrido por filas):**

| Lectura | `std::function` (actual) | Puntero a función | Directa |
|---|---|---|---|
| CT32 (frame) | 5,43–8,12 ns | 3,25–4,05 ns | 3,13–3,24 ns |
| T8 (textura) | 5,43–6,30 ns | 3,09–3,52 ns | 2,56–3,32 ns |

- **INFERENCIA:** el *swizzle* real cuesta ~3 ns por acceso. `std::function` le añade ~2,2–2,8 ns, y un puntero a
  función deja ~0,2–0,5 ns.
- **ESTIMACIÓN:** con ~29 % del GS dedicado a acceso a VRAM (despacho más trabajo) y ~5,5–6 ns por acceso, la
  captura hace del orden de 15 M de accesos por cada 2 VSync. Sustituir `std::function` por punteros ahorraría
  ~2,2–2,8 ns × 15 M ≈ **35–43 ms por cada 2 VSync, es decir ~11–14 % del GS**.
  - Tiene incertidumbre: el muestreo de hoja tiende a cargar el despacho y el microbenchmark tiene mejor localidad
    que la escena.
  - Se puede medir exactamente con GS Replay.

### 14.3 Cómo optimiza PCSX2 esas rutas (HECHO, código de PCSX2; GPL-3.0+)

1. **Direcciones:** `GSPixelOffset4` con tablas precalculadas `row[2048]` y `col[512]` de frame y Z. La dirección es
   `fzbr[y] + fzbc[x>>2]`: dos lecturas de tabla y una suma, **sin ninguna llamada**.
2. **Texturas:** `GSTextureCacheSW` desswizzlea a búferes lineales por bloque, bajo demanda, con invalidación por
   página en las escrituras. T8/T4 se conservan como índices de 8 bits.
3. **CLUT:** `GSClut::Read32` expande la CLUT a `u32[256]` (con TEXA) solo cuando cambia (`IsDirty`), y el muestreo
   indexa esa tabla.
4. **Estado:** se decodifica una vez por draw en una clave `u64` (`GSScanlineSelector`), simplificando los tests
   triviales.
5. **JIT** (xbyak/vixl) de `SetupPrim`/`DrawScanline` por clave, con una referencia en C++ (`CDrawScanline`)
   seleccionable como oráculo.
6. **SIMD:** 4 u 8 píxeles por vector.
7. **Relleno rápido** de sprites sin test (`FillRect`/`FillBlock`).
8. **Multihilo:** franjas de 16 scanlines repartidas por turnos entre hilos; determinista.
9. **Bilineal:** entero de 4 bits de fracción. **No** coincide con nuestra aritmética `float`/`lroundf`, así que
   adoptarlo cambiaría los resultados y no es reutilizable si se exige igualdad con el backend actual.

- **Reutilizable sin cambiar resultados** (como memoización o reimplementación propia, sin copiar código GPL): 1, 2, 3,
  4 y 7 (este último cuando no haya test), y 8 si se mantiene el orden por píxel.
- **Licencia:** copiar o traducir de cerca obliga a GPL-3.0+; reimplementar las ideas no (interpretación, no
  asesoría legal).

### 14.4 paraLLEl-GS, BT3-Recomp y upstream

- **paraLLEl-GS** (`Arntzen-Software/parallel-gs`):
  - LGPL-3.0+; Vulkan compute vía Granite;
  - **consume GIF y registros GS en crudo** (`gif_transfer`, `write_register`, `map_vram_read/write`,
    `read_transfer_fifo`, `vsync`);
  - **no busca exactitud de bits** (blending declarado exacto; AA1 y el desentrelazado, dudosos; ignora TEXFLUSH y
    usa heurísticas de *feedback*);
  - SSAA y FSR; requisitos de Vulkan modernos; problemas conocidos con el compilador de shaders de AMD en Windows.
- **BT3-Recomp** (`z3xox/BT3-Recomp`, GPL-3.0, «PS2Recomp fork» sin relación de fork en GitHub):
  - integra paraLLEl-GS enganchado al árbitro GIF en su propio GS (OpenGL), no detrás de `GSRasterBackend`, que no
    existe en su árbol;
  - modos «dual» y exclusivo;
  - en Windows usa Mesa lavapipe (Vulkan por CPU);
  - las lecturas GS→host no están verificadas en su código.
  - **No es reutilizable directamente** en nuestra arquitectura.
- **Upstream `ran-j/PS2Recomp` PR #285 «feat: GS performance patch 1»** (HECHO, API de GitHub: abierto, **borrador**,
  sin fusionar, 103 ficheros, 2 commits, última actualización 2026-10-14):
  - añade `ParallelBackend : GSRasterBackend` y amplía `gs_backend.h` (`UsesRawCommands`, `ProcessGIF`,
    `WriteRegisterRaw`…);
  - el frontend desvía el GIF en crudo al backend;
  - las lecturas GS→host usan `read_transfer_fifo`, con un parche de redondeo de la FIFO;
  - `ReadVram`/`WriteVram` mapean los 4 MiB en cada llamada;
  - presenta mediante interoperabilidad Vulkan→GL;
  - se selecciona **en compilación**, sin respaldo en tiempo de ejecución;
  - además toca IOP, el recompilador y el planificador.
  - **Es la integración que encaja con nuestra capa**, pero es un cambio grande, no estabilizado y mezclado con otros.
    Según AGENTS.md no se aplica a ciegas.

### 14.5 Presupuesto hacia 50 VSync/s

Datos de PERF_002 §12 y §13.8 y de este banco:

- **Gameplay en reposo:** 216 ms por VSync (VU1 109,8; PATH1 41,8; resto 64,7). Con C1, el VU1 baja unos 10–12 ms.
- **GS a coste 0 en la CPU** (por ejemplo, en GPU sin esperas): quedan VU1 ~100 ms más ~15–20 ms del resto, es decir
  **~8 VSync/s**.
- **VU1 a coste 0:** quedan GS más el resto, **~10 VSync/s**.
- **Los dos a coste 0:** ~19 ms, ~50 VSync/s (PERF_003 §7).
- **INFERENCIA:**
  - 50 VSync/s exige **a la vez** sacar el GS del hilo del juego (GPU o hilos propios) y **reducir el VU1 unas 5
    veces**;
  - ninguna optimización del `GSCpuBackend` por sí sola acerca el juego a 50; su techo es ~8–9 VSync/s;
  - el VU1 compilado es hoy el mayor bloque en gameplay.

### 14.6 Recomendación

1. **Antes de cualquier optimización del GS:**
   - comprobar con PCSX2 la escena del castillo y, si se confirma, corregir la semántica de `ZTE=0` (test ALWAYS sin
     escritura de Z, como PCSX2) como cambio de fidelidad, con su diferencial en GS Replay;
   - cambia la carga del GS, así que los benchmarks posteriores deben medirse ya con la corrección.
2. **Primera optimización a implementar: C2a**, despacho de VRAM sin `std::function`.
   - Es exacta por construcción y tiene la mayor ganancia medida en aislado (~11–14 % del GS, ESTIMACIÓN).
   - Es pequeña (una cabecera de solo 2 unidades) y **se mide byte a byte y en tiempo con GS Replay** sobre `cap2`.
   - **Siguiente paso natural:** tablas de offsets por primitiva y la CLUT expandida (ideas de PCSX2 reimplementadas),
     también medibles en el banco.
3. **Solución existente aprovechable:** paraLLEl-GS por la vía del **PR #285 de upstream** (backend detrás de
   `GSRasterBackend`).
   - Por ahora, solo como **evaluación aislada**: extraer la parte de GS, ver cómo encaja con nuestras lecturas
     GS→host persistentes (§13.4) y el checkpoint, y medir en el castillo.
   - No es exacto bit a bit: su validación sería por imagen o contra PCSX2, no contra GS Replay. GS Replay no puede
     alimentarlo hoy, porque graba lotes ya decodificados y no GIF en crudo; haría falta grabar en la entrada del
     frontend.
4. **Mayor potencial hacia 50 VSync/s:** la combinación de **GS en GPU** (paraLLEl-GS o equivalente) y
   **reestructurar el VU1 compilado** (A2: una función por entrada, que permite inlinear las ayudas, más
   especializaciones exactas).
   - Mantener `GSCpuBackend` como **respaldo exacto y oráculo**, con mejoras baratas y exactas (C2a, tablas, CLUT),
     pero sin esperar de él más de ~8–9 VSync/s.

## 15. E3-A: corrección de la semántica de ZTE (2026-10-29, experimento local, sin integrar)

### 15.1 Auditoría (HECHO)

- El único punto que interpreta el test de profundidad es `GSCpuBackend::WritePixel` (`gs_cpu_backend.cpp`):
  - el método de comparación sale de `(test >> 17) & 3`, **sin mirar `TEST.ZTE`** (bit 16);
  - la escritura de Z depende solo de `AFAIL` y `ZBUF.ZMSK`.
- `DrawTriangle`, `DrawSprite` y `DrawLine` pasan todos por `WritePixel`; no hay descartes previos por Z.
- `ClearFramebuffer` no usa el test.
- El frontend guarda `TEST` en crudo.
- El HLE de libgraph (`makeTest`) ya escribe `ZTE=1`.
- **Defecto:** con `ZTE=0`, el runtime aplica igualmente ZTST (NEVER no dibuja; GEQUAL/GREATER comparan) **y escribe Z**.

### 15.2 Pruebas semánticas (HECHO, `ps2xTest/src/ps2_gs_tests.cpp`, suite `PS2GSDepthTestEnable`)

**Expectativas** según el manual del GS y PCSX2 (`GSState::FlushPrim`, `GSRendererSW`): con ZTE=0 todo píxel pasa, sea
cual sea ZTST, y Z no se escribe; solo ZTE=1 con NEVER descarta.

| Prueba | Antes | Después |
|---|---|---|
| ZTE=0 con los 4 valores de ZTST: color escrito y Z intacto | **FAIL** | PASS |
| ZTE=1 NEVER: sin color ni Z | PASS | PASS |
| ZTE=1 ALWAYS: color y Z; con ZMSK, Z intacto | PASS | PASS |
| ZTE=1 GEQUAL (igual pasa, menor falla) y GREATER (igual falla, mayor pasa y escribe) | PASS | PASS |
| Interacciones: `TEST=0x1001` (el de DMC) escribe color y no Z; ZB_ONLY con ZTE=1 solo escribe Z y con ZTE=0 nada; FBMSK con ZTE=0 | **FAIL** | PASS |
| Formatos Z32/Z24/Z16/Z16S: GEQUAL pasa o rechaza, y ZTE=0 dibuja sin escribir Z | **FAIL** | PASS |

- **Suite completa:** antes de la corrección, 476 tests y 27 fallos (los 24 preexistentes más 3 nuevos, exactamente
  los de ZTE=0). Después, **476 tests y los mismos 24 fallos**. Ninguna prueba existente dependía del comportamiento
  anterior.
- **Fuera del alcance (no probado):** el recorte del valor de Z al rango del formato (Z24/Z16). Las pruebas usan
  valores dentro del rango.

### 15.3 Corrección mínima (código, `WritePixel`)

```cpp
const bool depthTestEnabled = ((ctx.test >> 16) & 1u) != 0u;
const uint32_t ztestMethod = depthTestEnabled ? static_cast<uint32_t>((ctx.test >> 17) & 3u) : 1u;
...
if (writeMask.writeDepth && !ctx.zbuf.zmask && depthTestEnabled)
```

- Es general: no hay casos especiales para DMC.
- No toca VU0/VU1, GIF, DMA, texturas, lectores de VRAM ni temporización.
- No añade un descarte temprano: con ZTE=1 y NEVER, el resultado ya era correcto píxel a píxel.
- **Propuesta de parche** (no está en `patches/` ni en el lock):
  `analysis/local/perf003/e3a/GAMEPLAY_GS_zte_depth_test_enable.patch.proposal` (sha256 `4ad101c5…`); +116/−2 en 2
  ficheros; aplica limpio sobre el replay de 31 parches.

### 15.4 GS Replay con la captura `cap2` (HECHO)

- **Lotes afectados:** 16 con `ZTE=0` en toda la captura, todos con ZTST=0. El primero es el registro 2274.
- **(a) Solo cambian los lotes con ZTE=0:**
  - la semántica antigua es exactamente la nueva con `ZTE=1` forzado (el código antiguo ignoraba ZTE);
  - con el backend corregido y `--force-zte`, la reproducción es **IDENTICAL** en los 1 681 hashes, los 2 `Present`
    y la VRAM final.
- **(b) La diferencia empieza en el lote afectado:** con la semántica real, la primera divergencia está en el
  intervalo de registros 2258–2274, que contiene el registro 2274, el primer sprite con ZTE=0.
- **Determinismo:** dos reproducciones corregidas dan la misma VRAM final (`50D7BB62…`).
- **Nueva referencia:** `e3a/ref_fixed_run1.bin` (VRAM final corregida). Las capturas y referencias anteriores no se
  han tocado.
- **Qué hacen los 4 sprites** (volcados de VRAM tras cada registro, semántica antigua frente a corregida):

| Paso | Efecto con la corrección |
|---|---|
| Sprite 1 (2274) | llena el búfer `0xA0` (512×224) de gris 128: 114 688 píxeles cambian |
| Sprites 2 y 3 (2276, 2277) | `+111` y luego `−239`: el RGB del búfer queda en 0 |
| Sprite 4 (2278) | compone el búfer sobre el framebuffer con ALPHA `0x62`, FIX `0x41` = `Cd − Cs·65/128` (**oscurecimiento sustractivo**); con el búfer a 0, el RGB no cambia y **solo cambia el alfa** del framebuffer (151 632 bytes) |

- En esta ventana, la imagen presentada es **idéntica** antes y después (0 píxeles distintos en los 2 `Present`).
  La VRAM final difiere solo en 392 bytes de alfa: el siguiente fotograma sobrescribe casi todo.
- **INFERENCIA:** es un oscurecimiento parametrizado por los colores de las pasadas 2 y 3 (aquí se anulan). Puede
  verse en otros momentos (fundidos o transiciones), pero no hay evidencia visual de ello.
- **Observación aparte:** la imagen reproducida en el VSync ~27101 muestra un gran triángulo naranja plano delante de
  Dante. Es igual con las dos semánticas, así que es otro defecto gráfico, sin investigar.

### 15.5 Validación visual (NO obtenida)

- Corrida única: ejecutable corregido `c85183a0…`, 286 s, VSync 26797–27962. 0 FATAL. 4,33 VSync/s.
- **Las tres capturas (VSync ~27386, ~27804 y la final) son blanco puro** (#FFFFFF en todos los píxeles, sin franjas
  de letterbox).
- **INFERENCIA:** fallo de `PrintWindow` sobre la ventana OpenGL, no la imagen del juego.
  - El efecto recuperado solo puede oscurecer.
  - En la reproducción exacta de la misma escena, la imagen no es blanca.
  - La captura de pantalla funcionó en todas las corridas anteriores; posible sesión bloqueada o pantalla apagada a
    esa hora.
- **No se repitió** la corrida (solo había una autorizada). Sigue pendiente la comparación con PCSX2.
- La referencia s09 de PCSX2 muestra el mar a la izquierda, que en nuestras capturas de gameplay sale con bloques
  corruptos. **No hay evidencia** de que este defecto dependa de ZTE.

### 15.6 Coste de rendimiento (GS Replay, `cap2`; A = backend anterior, B = corregido; 5 rondas alternadas, sin verificación)

| | ms por cada 2 VSync |
|---|---|
| A | 398,2 / 401,1 / 407,9 / 425,4 / 414,2 (media 409,4) |
| B | 500,6 / 490,6 / 507,3 / 509,1 / 527,8 (media 507,1) |

- **Media B − A = +97,7 ms (+24 %).**
- **El entorno estaba cargado:** el mismo A medía ~300 ms horas antes, y se observó un proceso ajeno del usuario
  activo. La diferencia relativa entre corridas alternadas es válida, pero ruidosa.
- **Desglose por clase** (una reproducción cada uno): las clases afectadas suben 95,0 → 122,7 ms (sprite texturizado
  con mezcla, ZTST 0) y 31,0 → 52,2 ms (sprite sin textura con mezcla, ZTST 0). Juntas, unos +49 ms. El resto de la
  diferencia global no se puede separar del ruido de esta sesión.
- **Lectura:** el coste extra es **fidelidad**, no regresión: son 16 sprites de pantalla completa que antes se
  calculaban sin escribir y ahora se escriben. No debe compararse como si la omisión hubiera sido una optimización.
- **En el juego:** 4,33 VSync/s en la cinemática con C1 y la corrección, frente a 4,31 de la corrida A sin C1 ni la
  corrección. Las ganancias y pérdidas no se pueden separar con una corrida y este entorno.

### 15.7 Riesgos y limitaciones

- **Fidelidad:** la semántica coincide con PCSX2 (SW y HW). No se ha comparado ningún fotograma con PCSX2 en el que el
  efecto sea visible.
- **Rendimiento:** +24 % en el GS aislado de esta captura (cifra ruidosa). En escenas sin ZTE=0 el coste no cambia.
- **No cubierto:** el recorte de Z por formato; la interacción con el test de alfa de destino (DATE), que no cambia,
  pero no tiene prueba propia para ZTE=0.
- **Estado del vendor:** contiene el experimento (2 ficheros) y **no** coincide con el replay de 31 parches.
  - Las copias originales están en `analysis/local/perf003/e3a/*.p31`.
  - El ejecutable de trabajo es el corregido (`c85183a0…`); el anterior se conserva como
    `dmc-recomp-perf002-c1.exe` (con C1, sin la corrección).

### 15.8 Revisión previa a la integración (2026-10-30)

Validación pendiente de §15.7, sin optimizaciones nuevas y sin ejecutar el juego. Evidencia en
`analysis/local/perf003/e3a/` (`tests_ext*.log`, `audit/`).

**Fase 1 — fallos históricos (HECHO).** Se compararon por identidad (suite + nombre), estado y mensajes de aserción
el registro previo a la corrección (`tests_before_fix.log`) y el posterior (`tests_after_fix.log`, sin códigos de
color ANSI):

- 476 pruebas en ambos con los mismos nombres.
- Antes: 13 `Failed` + 14 `Error`. Después: 10 `Failed` + 14 `Error` = 24.
- Los únicos que dejan de fallar son los 3 de `PS2GSDepthTestEnable` que probaban `ZTE=0`. Ningún fallo nuevo.
- Los 24 restantes tienen los **mismos mensajes, línea a línea**:
  - 14 `Error`: «EE invocation stack space exhausted»;
  - 7 `Failed`: falta `PS2X_RUNTIME_ARENA_BASE` en el entorno de pruebas, de modo que `RuntimeGuestArena` no está
    disponible;
  - `instructions.h` no se encuentra desde el directorio de trabajo;
  - dos de `sceGsSetDefDBuffDc`.

**Fase 2 — cobertura adicional (HECHO).**
- **Cinco pruebas nuevas** en `PS2GSDepthTestEnable`. Leen los valores en bruto mediante un auxiliar `drawRaw`, que
  lee las palabras de VRAM con el formato de almacenamiento:
  - **DATE con ZTE=0:** CT32 y CT16, con DATM 0 y 1, y DATE fallido con ZTE=1;
  - **los cuatro modos de AFAIL con ZTE=0:** también RGB_ONLY con ZTE=1;
  - **conservación de Z con ZMSK=0:** en sprite, triángulo y línea, con los cuatro ZTST; además, un dibujo posterior
    con ZTE=1 compara contra la Z conservada;
  - **representación por formato:** Z32 de 32 bits; Z24 conserva el byte alto y compara solo 24 bits; Z16 y Z16S
    no tocan los vecinos;
  - **bits no escribibles y máscaras:** CT24 conserva el byte alto; FBMSK parcial y total; CT16.
- **Resultado:** las 11 pruebas de `PS2GSDepthTestEnable` pasan.
- **Dos sondas separadas** en la suite `PS2GSDepthIndependentProbes`. Prueban comportamientos que no dependen de
  ZTE; sus expectativas salen de PCSX2 `GSRendererSW.cpp` (líneas 1076–1083 y 1463) y `GSDrawScanline.cpp` (línea
  679).
  - **Las dos fallan** y son **divergencias nuevas, independientes de ZTE**:
    1. **Recorte de Z:**
       - PCSX2 limita la Z al máximo del formato (0xFFFF en Z16, 0xFFFFFF en Z24);
       - el runtime no la limita: compara la Z completa y escribe los bits bajos;
       - INFERENCIA: 0x12345 queda escrito como 0x2345 en Z16.
    2. **DATE con framebuffer CT24:**
       - PCSX2 descarta el dibujo entero (según su comentario, comprobado en una PS2);
       - el runtime lo deja pasar.
  - **HIPÓTESIS:** no se ha demostrado que DMC use ninguno de los dos casos. En los 16 lotes con ZTE=0 la Z no se
    usa, y ninguno usa DATE.
  - **Alcance:** no se han corregido; ampliarlo es decisión del usuario. Las sondas no forman parte de la propuesta.
- **Suite completa:** 483 pruebas, 26 fallos = los 24 históricos + las 2 sondas.

**Fase 3 — auditoría de GS Replay (HECHO, `audit/replay_*.txt`, `audit/vram_classify.txt`).**
- **Primer lote divergente: registro 2274.**
  - Estado: sprite; `TEST=0x1003` (ATE=1, ATST=ALWAYS, AFAIL=FB_ONLY, DATE=0, ZTE=0, ZTST=NEVER);
  - FRAME `fbp=0xA0` CT32 512×224; ZBUF `zbp=0x70` Z16S; abe=0.
  - La verificación lo detecta en el registro de hash siguiente (2275): «first differing state within records
    2258..2274». Los volcados tras el registro 2273 son idénticos byte a byte.
- **Primera diferencia de VRAM:**
  - byte `0x140000` (página 0xA0, búfer auxiliar, canal R);
  - palabra antes `0x7F000000`, después `0x00808080`.
- **Regiones y canales por paso** (antigua frente a corregida):

| Tras registro | Búfer aux 0xA0 (CT32) | Framebuffer 0x00 (CT32) | Z (Z16S 0x70) |
|---|---|---|---|
| 2273 | = | = | = |
| 2275 (sprite 1) | 114 688 píxeles: R/G/B ~114 670 bytes cada uno, A 36 944 | = | = |
| 2276–2277 (sprites 2–3) | antigua: contenido residual; corregida: **todo 0 (RGBA)** | = | = |
| 2278 (sprite 4, `TEST=0x1001`, textura = búfer aux) | igual que 2277 | **solo alfa**: 114 688 bytes (antes 0x7F/0x80, ahora 0x00); **RGB idéntico (0 bytes)** | = |
| VRAM final | 12 bytes de alfa | 184 bytes de alfa (+184 en el framebuffer 0x38, +12 en la página 0x1FF) | = |

  - **Color:** solo cambia en el búfer auxiliar (pasos intermedios). **Alfa:** búfer auxiliar y framebuffers.
    **Profundidad:** ningún byte en ningún paso.
- **Imágenes presentadas:** los dos `Present` son idénticos byte a byte (`frame_{old,fixed}_{0,1}.ppm`).
- **Determinismo:** dos reproducciones nuevas corregidas dan `50d7bb62…`, igual que las de §15.4.
- **Control con ZTE forzado a 1:** IDENTICAL frente a la captura; VRAM final `e2f02be8…`, idéntica a la del backend
  antiguo.
- **Referencias históricas preservadas:**
  - con el backend antiguo, `cap2` sigue dando IDENTICAL en los 1 681 hashes, los `Present` y la VRAM final;
  - las capturas `cap1` y `cap2` y sus volcados conservan fechas anteriores al experimento.
  - SHA-256: `cap2` `88895af1…`, `cap1` `6b283499…`.
- **Estado de la referencia nueva:** `50d7bb62…` **no** se adopta como valor de aceptación. Las pruebas de ZTE pasan,
  pero quedan abiertas dos divergencias independientes y la validación visual con PCSX2.

**Fase 4 — rendimiento (HECHO, entorno más tranquilo: carga ~18 %, sobre todo el navegador).**
- **Configuraciones** (9 rondas alternadas, backend sin verificación, ms por cada 2 VSync):
  - A = ejecutable antiguo;
  - A′ = ejecutable corregido con `--force-zte`, mismo binario que B;
  - B = corregido.

| | mediana | IQR | mín–máx | desv. típica |
|---|---|---|---|---|
| A | 318,2 | 310,7–321,0 | 299,5–393,9 | 27,7 |
| A′ | 308,9 | 304,7–313,5 | 303,4–322,2 | 6,4 |
| B | 334,9 | 334,7–343,9 | 329,2–350,5 | 7,3 |

- **Diferencias emparejadas:**
  - B − A′: mediana **+30,4 ms (+9,8 %)**, de +12,7 a +41,9; B más lento en 9 de 9 rondas;
  - B − A: +21,2 ms (+6,7 %); A tiene un valor atípico.
- **Por clase** (5 rondas alternadas A/B, medianas): solo suben las tres clases con ZTST 0:
  - sprite sin textura con mezcla: +16,7;
  - sprite texturizado con mezcla: +5,6;
  - sprite sin textura: +5,7;
  - en total, **+28,0 ms**; el resto de las clases varía entre −2,2 y +1,6.
  - Total del backend: A 316,7, B 342,6 (+25,9).
- **Corrección del dato de §15.6:** el +24 % (+97,7 ms) de §15.6 estaba inflado por la carga. La cifra estable es
  **+8 a +10 % (~+26 a +30 ms por cada 2 VSync)**, toda en los 16 lotes con ZTE=0.
- **Observación:** con la semántica antigua estas clases ya costaban ~74 ms, porque se calculaban los píxeles para
  luego descartarlos; ahora además se escriben.

**Fase 5 — estado de archivos (HECHO).**
- **Parches y lock:** `patches/` contiene los 31 parches, cuyos SHA-256 coinciden con `upstream.lock.json`, y
  `git diff HEAD` sobre `patches/` y el lock está vacío.
- **Repositorio:** HEAD = `dea53cc` = origin/master. Sin commits; la rama no está en el remoto.
- **Código EE y checkpoints:**
  - ningún fichero de `recomp/generated` es posterior al inicio de E3-A;
  - ninguno de los 9 `.dmck` de `analysis/local` es posterior tampoco; el más reciente es `g002_castle_entry.dmck`
    (29-10, 13:43), SHA-256 `4c841943…`.
- **Vendor:** frente al árbol del replay de 31 parches (`3c908f78`), solo difieren `gs_cpu_backend.cpp` (+5/−2) y
  `ps2_gs_tests.cpp`. Las copias `*.p31` tienen los mismos hashes de blob que ese árbol (`d88dd6f9…`, `1c02ff41…`).
- **Propuesta v2:** `e3a/GAMEPLAY_GS_zte_depth_test_enable.v2.patch.proposal`, SHA-256 `4f79c093…`.
  - Contiene la corrección, las 6 pruebas originales, las 5 nuevas y su auxiliar; no incluye las sondas.
  - `git apply --check` pasa sobre las copias `*.p31`, y el resultado coincide con el vendor salvo el bloque de
    sondas.
  - La v1 (`4ad101c5…`) se conserva.

### 15.9 Integración local del parche 32 (2026-10-30, sin commit)

- **Revisión estática:** el diff v2 elimina 2 líneas, ambas en `WritePixel`, y el resto son adiciones en
  `ps2_gs_tests.cpp`: el auxiliar `drawRaw` y la suite de 11 pruebas `PS2GSDepthTestEnable`. No contiene la suite
  `PS2GSDepthIndependentProbes`.
- **Parche:** `patches/PERF_003_gs_zte_depth_test_enable.patch`, idéntico a la propuesta v2 (SHA-256 `4f79c093…`).
  La propuesta v1, las copias `*.p31` y los resultados de E2 se conservan.
- **Reconstrucción limpia** (clon nuevo de la base `14b1e5cb`, `core.autocrlf=false`):
  - **control con 31 parches:** reproduce exactamente `dfa856e7…` (árbol `3c908f78…`);
  - **con 32 parches:** `patched_commit` `b7840ea9dcff04fbac1293e2812ff2e8ba970d4c`, árbol `0cc350d9…`; entre los
    dos árboles solo cambian los 2 ficheros;
  - **con el lock actualizado,** `pipeline.apply_patchset` sobre otro clon nuevo verifica los 32 hashes, reproduce
    `b7840ea9…` y deja el árbol limpio.
- **Vendor real:**
  - **Estado anterior:** HEAD `0efd17c` con los parches 29–31 y el experimento sin confirmar.
  - **Respaldo previo:** el estado experimental se guardó en `e3a/vendor_experiment_vs_replay31.diff` (árbol
    `710f3d67…`) y `e3a/ps2_gs_tests.cpp.with_probes`.
  - **Alineación:**
    - se trajo `b7840ea9` desde el clon limpio y se hizo `git reset --mixed`;
    - la única diferencia que quedó fue el bloque de sondas (36 líneas), que se retiró con `git checkout`.
  - **Resultado:** `git status` vacío, HEAD `b7840ea9`, árbol `0cc350d9` (el de la reconstrucción limpia), y
    `pipeline.upstream()` lo acepta.
  - **Por qué así:** el contenido se valida contra la reconstrucción limpia y no se reescriben los ~55 ficheros que
    ya tenían el mismo contenido. Así no cambian sus fechas de modificación (por ejemplo, `ps2_runtime.h`) y una
    compilación futura no recompila el código generado.
- **Pruebas:**
  - compilación incremental: solo se recompila `ps2_gs_tests.cpp`;
  - 481 pruebas; las 11 de ZTE pasan;
  - 24 fallos, idénticos en identidad y mensajes a los históricos.
- **GS Replay** (runtime y `gs_replay` recompilados de forma incremental sin cambios; el backend es idéntico byte a
  byte al del clon limpio):
  - dos reproducciones: VRAM final `50d7bb62…` las dos, igual que en E3-A;
  - primera divergencia: «within records 2258..2274»;
  - control `--force-zte`: IDENTICAL, `e2f02be8…`, que es la VRAM del backend anterior;
  - respecto de la VRAM antigua difieren 392 bytes, todos de alfa.
  - Los hashes históricos no se usan como criterio de aceptación.
- **Rendimiento:** sigue valiendo como referencia provisional la medida A′/B de §15.8 (+8–10 %).
- **Incidencias pendientes:** registradas por separado en `PERF_003_GS_OPEN_ISSUES.md` (GS-1 a GS-5).

### 15.10 Preparación del checkpoint (2026-10-30)

- **481 pruebas frente a 483:** la cifra de 483 de §15.8 incluía las 2 sondas `PS2GSDepthIndependentProbes`,
  que solo existían en el vendor experimental. El parche 32 no las incluye: 476 + 5 pruebas de ZTE nuevas = 481.
- **Sondas conservadas fuera de la suite integrada:**
  `analysis/tools/perf003_gs_probes/PERF_003_gs_independent_probes.diag.patch` (SHA-256 `5b1189bd…`).
  - Solo añade líneas y `git apply --check` pasa sobre el árbol de 32 parches. Contenido idéntico al bloque
    ejecutado en §15.8: 483 pruebas, 26 fallos.
- **GS Replay pasa a ser una herramienta permanente** en `tools/gs_replay/`, con `README.md`:
  - se ha eliminado de las cabeceras la etiqueta «experimental, local»; el mensaje de uso lista todas las opciones;
  - prueba de CTest `gs_replay_selftest`;
  - el hook de captura `capture_hook.inc` sustituye a `e2/retired_hook/perf003_gs_capture.inc`: mismo código, con la
    función renombrada a `startGsReplayCapture` y la inclusión de `gs_replay.h` relativa a su propio directorio.
- **Verificación con el vendor del pipeline** (`b7840ea9`, limpio):
  - configuración y compilación desde cero de `tools/gs_replay`;
  - `ctest`: 1/1;
  - `cap2`: dos reproducciones con VRAM final `50d7bb62…`, primera divergencia en 2258..2274;
  - `--force-zte`: IDENTICAL;
  - `capture_hook.inc` compila incluido desde otro directorio (`analysis/local/perf003/hookcheck`).
  - La captura en sí no se ha vuelto a ejecutar porque no se ha arrancado el juego.
  - `cap1` (formato v1) es rechazada por la herramienta v2 por diseño («unsupported version 1»); la referencia es
    `cap2`.
- **Aviso conocido:** C4244 en `main.cpp:733` (perfilador; nombre de DLL de `wchar_t` a `char`). Es inocuo y no se ha
  cambiado.

## 16. C2a-1: perfilado del rasterizador CPU y viabilidad (2026-10-30, sin cambios funcionales)

Base: HEAD `6054354`, vendor `b7840ea9` (32 parches), captura `cap2` (2 VSync del castillo). Banco local en
`analysis/local/perf003/c2a1/` (no versionado): `gs_replay` enlazado contra una copia generada de `gs_cpu_backend.cpp`
(`make_probe.py`), con sondas tras macros. Sin cambios en el vendor ni en el runtime.

### 16.1 Método

- **Fidelidad de las sondas (HECHO):** referencia intermedia de 32 parches = registro de los 1 681 hashes de VRAM y los
  2 `Present` que calcula la reproducción con el `GSCpuBackend` de `ps2_runtime.lib` (SHA-256 del registro
  `d3f437c8…`, idéntico en dos ejecuciones). Las 12 variantes de sonda producen exactamente ese registro y la VRAM
  final `50d7bb62…`; `--force-zte` sigue dando `IDENTICAL` frente a `cap2`.
- **Coste marginal por duplicación:** cada variante `DUP_*` repite una operación sin efecto observable (lectura a un
  sumidero `volatile`, escritura idempotente del mismo valor, muestreo repetido) y mide la diferencia. Es un coste
  **marginal y optimista** (la repetición encuentra la caché caliente), no un coste aislado.
- **Tiempos:** un núcleo (afinidad 0x10), prioridad alta, sin verificación en el proceso cronometrado, orden rotatorio,
  15 rondas × 3 reproducciones; diferencia emparejada por ronda frente a `base` (copia sin sondas). Una primera tanda
  sin fijar afinidad dio una dispersión de ±20 % entre procesos y solo se usa como contraste.
- **Perfil:** muestreo de hoja (20 reproducciones, 8 773 muestras) de la variante `lib`.

### 16.2 Carga (HECHO, contadores de una reproducción)

| | Lotes | Iteraciones de caja | Píxeles (`WritePixel`) | Escrituras FB | Lecturas Z | Muestreos | Texels leídos | CLUT |
|---|---|---|---|---|---|---|---|---|
| Triángulos (tristrip) | 26 815 | 7 852 457 | 973 264 (12,4 %) | 653 022 | 971 919 | 973 264 (todos bilineales de 4 taps) | 3 893 056 | 3 893 056 |
| Sprites | 40 | — | 2 562 626 | 2 562 626 | 268 866 | 686 658 (227 906 centrados de 1 tap) | 2 062 914 | 0 |
| Líneas, puntos | 0 | | | | | | | |

- Rechazo por scissor dentro de `WritePixel`: **0** (triángulo y sprite ya recortan al scissor).
- Mezcla: 1 813 438 píxeles; lecturas de FB: 1 884 788; escrituras de Z: 652 868; niebla: 514 220.
- **20 estados distintos** en toda la captura; 10 suman el 96 % del tiempo y el 92 % de los píxeles.

### 16.3 Tiempo (HECHO salvo donde se indica)

- Por tipo (TIMEKEY, 4 reproducciones): triángulos 130 ms (59 %), sprites 92 ms (41 %) por reproducción.
- `base` 228–243 ms de mediana según la tanda (fijado a un núcleo); `lib` − `base` = +0,5 a −1,9 ms (sin efecto de
  enlazado ni de disposición del código).

| Variante (marginal) | Δ emparejado | Rondas | Operaciones | ns por operación (ESTIMACIÓN) |
|---|---|---|---|---|
| `DUP_SAMPLE` | **+111,3 ms** (IQR +97..+118) | 15/15 | 1 659 922 muestreos | ~67 |
| `DUP_FBWRITE` | +22,1 ms | 14/15 | 3 215 648 | ~7 |
| `DUP_ZWRITE` | +12,4 ms | 14/15 | 652 868 | ~19 |
| `DUP_EDGE` (función de borde) | +9,9 ms | 10/10 | 7 852 457 iteraciones | ~1,3 |
| `DUP_BLEND` | +8,0 ms | 12/15 | 1 813 438 | ~4 |
| `DUP_FBREAD` | +7,9 ms (IQR −5,5..+13,9) | 10/15 | 1 884 788 | ~4 |
| `DUP_ZREAD` | +6,3 ms (IQR −8,9..+12,0) | 9/15 | 1 240 785 | ~5 |
| `DISPATCH_PTR` (punteros en lugar de `std::function`; exacto) | **−35,6 ms (−15 %)** (IQR −39,9..−32,6) | 15/15 más rápido | ~15 M accesos | ~2,4 ahorrados |

Perfil de hoja (`lib`): `WritePixel` 17,5 %; despacho de VRAM 24,1 % (`ReadVramUnlocked` 12,3, `_Do_call` de lectura
5,7, `WriteVramUnlocked` 3,5, `_Do_call` de escritura 2,6); `SampleTexture` 8,3 más su lambda 6,3; `DrawTriangle`
7,4; `combineTexture` 7,2; `lerpChannel` 5,4 más CRT 5,2 (`lround`); `LookupCLUT` 3,8; lectores y escritores con
*swizzle* 6,6; `DrawSprite` 2,4; `bitsPerPixel` 1,0.

- **INFERENCIA:** la ruta de textura (muestreo bilineal de 4 taps sobre T8/T4 con CLUT leída de VRAM en cada tap)
  es el cuello de botella: ~45–50 % del GS. El despacho por `std::function` es el mayor coste puramente accidental.
- **INFERENCIA:** el código caliente ocupa ~8 KB (tamaños del `.map`), cabe en la caché L1 de instrucciones; no hay
  indicios de presión de caché de instrucciones. No se han medido fallos de predicción (sin contadores PMU).
- **HECHO (código):** un píxel de la ruta T8 bilineal hace ~45 llamadas no inlineables: 4 × (lambda, despacho,
  `_Do_call`, `ReadP8`, `LookupCLUT`, `ReadCT32`, `lerpChannel`, `lround`) más `combineTexture`, `WritePixel` y
  2–4 accesos de FB/Z con despacho. Los lectores están en `ps2_gs_memory.cpp`, otra unidad sin `/GL`.

### 16.4 Decisiones por píxel trasladables al lote (HECHO, código; exactas por construcción)

Decodificación de `TEST`, `ALPHA`, `FRAME`, `ZBUF`, `TEX0`, `CLAMP` y `TEXCLUT`; `framePageBaseToBlock`;
`bitsPerPixel` (llamada fuera de línea, dos veces por píxel); test de scissor de `WritePixel` (0 rechazos medidos);
selección de lector y escritor por PSM; `resolveClutIndex` (CSM/CSA); `switch` de TFX en `combineTexture`; modo de
envoltura; `iip`/`tme`/`fst` en el bucle del triángulo.

### 16.5 Estrategias

| | Potencial (ESTIMACIÓN salvo B1) | Complejidad | Riesgo de fidelidad |
|---|---|---|---|
| A. Mantener | 0 | 0 | 0 |
| B. Punteros a función por lote | **B1 medido: −15 %**; con selección por lote de lector FB/Z/texel, −15 a −20 % | Baja: 1 cabecera privada (2 unidades del runtime, ninguna del código generado) | Nulo: mismas funciones, mismos argumentos |
| C. Plantillas y variantes precompiladas | −35 a −50 % (ESTIMACIÓN; elimina la mayoría de llamadas si los lectores se exponen inline) | Alta: combinatoria de 20 estados, ruta genérica de respaldo, tamaño de código | Medio: misma aritmética, pero muchas rutas que validar |
| D. Estado invariante precalculado | −5 a −10 % (ESTIMACIÓN; parte del 17,5 % de `WritePixel` y de la decodificación de textura) | Media | Bajo, si la ruta general queda como oráculo |

Técnicas de PCSX2 (§14.3) relevantes: clave de selector por draw (C/D), tablas de direcciones por fila y columna, CLUT
expandida por draw, relleno rápido de sprites. Su bilineal entero no es reutilizable si se exige igualdad de bits.

Otras oportunidades medidas o acotadas, para fases posteriores:
- caché de CLUT por lote (3,9 M lecturas de CLUT; requiere invalidación si el lote escribe sobre la CLUT);
- `lround` inline en `lerpChannel` (CRT 5,2 %; exige prueba exhaustiva de equivalencia en [0, 256));
- recorrido por intervalos en lugar de caja (~9 ms medidos, ~4 %).

### 16.6 Observación fuera de alcance

El runtime lee la CLUT de VRAM en cada muestreo. El GS real la carga en su búfer de CLUT según `TEX0.CLD`. Si un
juego reescribe la zona de CLUT en VRAM sin recargarla, los resultados pueden diferir del hardware. No investigado;
no afecta a la equivalencia con la línea base.

## 17. C2a-2: prototipo de despacho de VRAM con punteros a función (2026-10-30, experimento local, sin integrar)

Base A: vendor `b7840ea9` (32 parches). Prototipo B: el mismo vendor con un único cambio en
`gs_cpu_backend.h` (cabecera privada del backend): los alias `ReadVramFunc`/`WriteVramFunc` pasan de `std::function`
a punteros a función con las mismas firmas. El constructor, `ReadVramUnlocked`, `WriteVramUnlocked` y los lectores
no cambian. Evidencia local en `analysis/local/perf003/c2a2/` (ejecutables A y B congelados, registros y propuesta).

### 17.1 Revisión del cambio (HECHO)

- Las tablas solo se rellenan en el constructor con funciones libres de `GSMem` (sin capturas) y solo se llaman en
  `ReadVramUnlocked`/`WriteVramUnlocked`. Las 64 entradas se rellenan siempre (por defecto `ReadNull`/`WriteNull`), así
  que la diferencia de un `std::function` vacío (`bad_function_call`) frente a un puntero nulo no puede darse.
- Compilación incremental: solo `gs_cpu_backend.cpp` y `gs_frontend.cpp` (más `gs_replay` y el driver local, que
  incluyen la cabecera); ninguna unidad de `recomp/generated`; sin avisos nuevos.
- En el `.map` de B ya no hay instancias de `_Func_impl_no_alloc` en `gs_cpu_backend.obj` (13 en A).

### 17.2 Fidelidad (HECHO)

- Registro intermedio de la reproducción de `cap2` (1 681 hashes de VRAM y 2 `Present`): `d3f437c8…`, idéntico en A
  y en dos ejecuciones de B; VRAM final `50d7bb62…`; `--force-zte` `IDENTICAL` con el driver local y con
  `gs_replay`.
- `ps2x_tests`: 481 pruebas y los mismos 24 fallos (identidad y mensajes); las 11 de ZTE superadas; `ctest` 1/1.
- Con el parche diagnóstico de GS-1/GS-2 aplicado temporalmente: 483 pruebas y 26 fallos, idénticos a la referencia
  de E3-A (mismos mensajes de las dos sondas).

### 17.3 Rendimiento (HECHO; banco de GS Replay, no tiempo del juego)

Un núcleo (afinidad 0x10), prioridad alta, sin verificación, 4 reproducciones por proceso con la primera descartada
como calentamiento, orden A/B alterno por ronda.

| Medida | A | B | Diferencia emparejada |
|---|---|---|---|
| Backend por reproducción de `cap2` (2 VSync), 21 rondas | 235,8 ms (IQR 227,9–244,9) | 203,5 ms (IQR 196,5–216,2) | **−30,9 ms**; por ronda **−13,6 %** (cuartiles −15,6 / −9,1; mín. −19,1, máx. −3,2); B más rápido en 21/21; ≥ 10 % en 15/21 |
| Variante con tiempo por lote, 11 rondas | 247,5 ms | 215,8 ms | −31,5 ms (IQR −32,4..−30,8), −12,7 % |
| Triángulos (por reproducción) | 129,5 ms | 114,8 ms | −14,7 ms (−11 %) |
| Sprites (por reproducción) | 90,7 ms | 76,0 ms | −14,7 ms (−16 %) |

- El prototipo da algo menos que la sonda de C2a-1 (−35,6 ms, −15 %), dentro de la variación entre tandas.
- **No extrapolable directamente al juego:** el ahorro es del rasterizador aislado en una ventana del castillo.

### 17.4 Propuesta local de parche 33

`analysis/local/perf003/c2a2/PERF_003_gs_vram_dispatch_function_pointers.patch.proposal` (SHA-256 `429f1eb7…`,
LF). Aplicada con el procedimiento del pipeline sobre los 32 parches da el árbol `0b6a5eb7…` y el commit sintético
`614c0eee…`. No está en `patches/` ni en el lock. El vendor se ha devuelto a `b7840ea9` y el árbol de compilación se
ha recompilado de forma incremental a A.

## 18. C2a-2: integración local del parche 33 (2026-10-30, sin commit)

Se integra el prototipo de §17 como parche 33, `patches/PERF_003_gs_vram_dispatch_function_pointers.patch`
(SHA-256 `429f1eb778ed78aac653c8ea8def48e4586e760f53def9fb2845fb350ba08121`), idéntico a la propuesta. Se añade
al lock y a `patches/README.md`; los parches 1–32 no cambian. Evidencia local en `analysis/local/perf003/c2a2/int33/`.

### 18.1 Reconstrucción (HECHO)

- Copia limpia de upstream `14b1e5cb` con los 33 parches del lock: árbol `0b6a5eb7…`, `patched_commit`
  `614c0eee0b3b52b8e55dca7dc0b35999a2ecbd62`, los previstos en §17.4. El vendor está en ese commit, limpio, y
  `pipeline.upstream()` y la verificación de hashes de los 33 parches pasan.
- Compilación incremental: `gs_cpu_backend.cpp` y `gs_frontend.cpp` (más `gs_replay` y el banco local); ninguna
  unidad de `recomp/generated`; sin avisos nuevos. En el `.map` no queda ninguna instancia de `std::function` en
  `gs_cpu_backend.obj` (13 con 32 parches).

### 18.2 Fidelidad (HECHO)

- `ps2x_tests`: 481 pruebas, los mismos 24 fallos con los mismos mensajes (14 `Error`, 10 `Failed`); las 11 de
  `PS2GSDepthTestEnable` superadas; `ctest` de GS Replay 1/1.
- Reproducción de `cap2` (dos veces): registro de 1 681 hashes y 2 `Present` `d3f437c8…`, idéntico byte a byte a la
  referencia de 32 parches; VRAM final `50d7bb62…` con el driver local y con `gs_replay`; `--force-zte` `IDENTICAL`
  (0 divergencias) con ambos.
- Sondas GS-1/GS-2 aplicadas temporalmente: 483 pruebas, 26 fallos, idénticos a la referencia de E3-A; las dos sondas
  siguen fallando con los mismos mensajes. Retiradas después.

### 18.3 Rendimiento (HECHO; banco de GS Replay, no tiempo del juego)

A: ejecutables congelados de §17 (vendor reconstruido con 32 parches). B: vendor reconstruido con 33 parches. Mismo
procedimiento que §17.3.

| Medida | A | B | Diferencia emparejada |
|---|---|---|---|
| Backend por reproducción de `cap2` (2 VSync), 21 rondas | 233,9 ms (IQR 231,2–237,4) | 202,0 ms (IQR 199,8–206,5) | **−31,0 ms** (IQR −32,7..−28,6); por ronda **−13,3 %** (cuartiles −14,4 / −12,2); B más rápido en 20/21; ≥ 10 % en 20/21 |
| Variante con tiempo por lote, 11 rondas | 224,4 ms | 194,2 ms | −28,1 ms (IQR −32,0..−26,9), −13,4 %; 11/11 |
| Triángulos (por reproducción) | 132,2 ms | 117,2 ms | −15,0 ms (−11,3 %) |
| Sprites (por reproducción) | 92,2 ms | 77,9 ms | −14,3 ms (−15,5 %) |

- Una ronda (+9,5 %) es un atípico de B (máximo 256,6 ms frente a una mediana de 202,0); el resto está entre −10,7 y
  −19,2 %. Se reproduce la ganancia de §17.3 (−13,6 %).
- **No extrapolable directamente al juego.**
