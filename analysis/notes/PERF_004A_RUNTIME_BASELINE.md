# PERF_004A: línea base de rendimiento del runtime con 35 parches

**ELF:** `SLES_503.58` (PAL), SHA-256 `d0753a6b…bab4`, CRC32 `77654AD2`, entry `0x00100008`.

**Base:**

- `master` `529e9ed` (35 parches, vendor `0a696611…`);
- exe `7571ae5b…`;
- rama local `perf-004-runtime-baseline`, sin commits.

**Fecha:** 2026-11-10.

**Alcance:** solo medición. No se modificó código, vendor, parches ni lock, y no se compiló nada.

**Procedencia:** informe local `analysis/local/PERF_004_BASELINE.md` y datos en `analysis/local/perf004/`. **No están
versionados.** Esta nota resume sus resultados. El nombre PERF_004A se usa en las fases siguientes; el informe local
se titula «PERF_004».

**Etiquetas:**

- **HECHO:** medido.
- **ESTIMACIÓN:** cálculo sobre medidas.
- **INFERENCIA.**
- **UNKNOWN.**

**Fases anteriores:**

- [PERF_001_BASELINE.md](PERF_001_BASELINE.md): oficina, 6,76 VSync/s tras el parche 28.
- [PERF_002_CASTLE_BASELINE.md](PERF_002_CASTLE_BASELINE.md): castillo, cinemática y gameplay.
- [PERF_003_ARCHITECTURE_FEASIBILITY.md](PERF_003_ARCHITECTURE_FEASIBILITY.md): viabilidad, GS Replay y experimentos
  del GS.

## 1. Objetivo

La versión PAL presenta a 50 Hz. El objetivo de velocidad completa es **50 VSync/s**, es decir, 20 ms por VSync.
PERF_004A mide dónde está el runtime respecto a ese objetivo y qué subsistema limita.

**Importante:** todos los datos son de **cinemáticas sin mando**, de la oficina y del castillo, no de gameplay.
PERF_002 midió en gameplay un VU1 relativamente más pesado (109,8 ms de VU1 y 41,8 ms de PATH1 por VSync).

## 2. Metodología

| Elemento | Valor |
|---|---|
| Ejecutables | 35 parches `7571ae5b…`; 34 parches `a1e08309…` |
| Checkpoints | copias locales de `g002_O_office.dmck` (`5f9576eb…`) y `g002_castle_entry.dmck` (`4c841943…`), los de GAMEPLAY_002 |
| Ventanas | oficina, VSync (16420, 17000]; castillo, (26800, 28000] |
| Parada | VSync 17000 y 28000 |
| Orden | alternado 35, 34, 34, 35, 35, 34 en cada escenario: 6 corridas por escenario, 12 en total |
| Entorno | VU1 `native`, sin mando, `PS2X_VU1_STATS_SEC=2`; sin capturas, perfilador ni registro intensivo en las corridas de velocidad |
| Máquina | portátil Ryzen 7 7735HS (8 núcleos y 16 hilos), 32 GB, GPU RTX 4060 Laptop |
| Comprobación por corrida | hash del exe, VSync final alcanzado, 0 mensajes fatales |

**Cálculo de la velocidad:**

- VSync/s = suma de VSync / suma de las ventanas que el propio runtime cronometra con `steady_clock` (líneas
  `[VU1:dispatch] … window=`). Solo se cuentan las ventanas que terminan dentro del intervalo.
- El reloj monotónico del conductor externo se usa como contraste: difiere ≤ 0,24 %.

**Herramientas:** `analysis/local/perf004/tools/` (no versionadas): `run_bench.py`, `series.py` y `analyze_bench.py`.
Las fases PERF_004C–E reutilizan este método.

## 3. Velocidades medidas (HECHO)

| Escenario | 35 parches (media de 3) | 34 parches (media de 3) | Rango de las 6 corridas |
|---|---|---|---|
| Oficina | 6,164 VSync/s (162,3 ms) | 6,200 VSync/s (161,5 ms) | 6,018–6,474 |
| Castillo | 4,863 VSync/s (205,8 ms) | 4,938 VSync/s (203,2 ms) | 4,613–5,315 |

**Tramos del castillo** (35 parches):

| Tramo | VSync/s | ms por VSync |
|---|---|---|
| 26800–27000 | 5,71 | 175,3 |
| 27000–27600 | 4,79 | 208,9 |
| 27600–28000 | 4,62 | 216,5 |

**HECHO.** El trabajo del juego es reproducible: en el castillo, 1062–1064 llamadas VU1 y 2,47 M ciclos VU1 por VSync
en todas las corridas. Lo que varía entre corridas es la máquina.

**INFERENCIA.** El ruido entre corridas es de ±7 % y sigue a la CPU total del sistema: 188 ms por VSync con un 11 % de
CPU ajena y 217 ms con un 23 %.

**Distancia al objetivo:** 50 VSync/s está ×8 por encima de la oficina y ×10 por encima del castillo.

## 4. Tiempo por VSync, CPU y memoria (HECHO)

**Desglose por VSync** (ms de cada bloque, del cronometraje del runtime):

| Escenario | ms totales | VU1 | PATH1 | Resto |
|---|---|---|---|---|
| Oficina | 154,5–166,2 | 55,7–60,2 | 44,5–48,7 | 54,2–58,6 |
| Castillo | 188,2–216,8 | 87,1–99,9 | 46,4–54,3 | 54,7–63,6 |

**Hilos:**

| Hilo | CPU | Esperas |
|---|---|---|
| Juego | 99–100 % de un núcleo | 0,04 % de muestras en espera |
| Ventana | ~8 % | 92 % de sus muestras durmiendo |
| Driver | ~2,5 % | — |

- El proceso usa el 110 % de un núcleo. El hilo del juego es el que limita, y es **cálculo**, no esperas.
- **Memoria:** RSS de 158–160 MB en la oficina y ~180 MB en el castillo.
- **GPU** (contadores PDH): motor 3D al 0,7–0,8 %. Está prácticamente ociosa.
- **Presentación:** la ventana va a 60 Hz como máximo y solo sube un cuadro cuando cambia el VSync. Las
  presentaciones reales por segundo son **UNKNOWN**: hacen falta herramientas con elevación (WPR o PresentMon), que no
  estaban disponibles.

## 5. Perfil por subsistema (HECHO como porcentaje)

**Método de muestreo:**

- Se suspende el hilo del juego, se copian sus registros y 64 KB de pila, y se reanuda; la pila se recorre con
  `StackWalk64`.
- Unas 165 muestras/s, con un coste del 1,4 % de tiempo suspendido.
- 40 808 muestras en el castillo y 16 033 en la oficina (±0,3 puntos).

**Columnas «ms»:** son ESTIMACIÓN, % × ms por VSync.

| Subsistema (% exclusivo del hilo del juego) | Oficina | ms | Castillo | ms |
|---|---|---|---|---|
| **GS rasterizador** | **53,1 %** | 86 | **49,6 %** | 102 |
| ↳ triángulos | 26,8 % | 44 | 28,1 % | 58 |
| ↳ sprites | 26,0 % | 42 | 21,2 % | 44 |
| **VU1 compilado** | **32,1 %** | 52 | **31,4 %** | 65 |
| VU1 intérprete | 5,1 % | 8 | 9,8 % | 20 |
| DMA y memoria EE | 4,1 % | 7 | 3,0 % | 6 |
| VIF | 3,0 % | 5 | 2,9 % | 6 |
| Código EE recompilado | 1,1 % | 2 | 1,1 % | 2 |
| GIF y frontal GS | 0,7 % | 1 | 0,6 % | 1 |

**Coste del GS y del VU1:**

- El GS es el mayor bloque: 50–53 %.
- El VU1 es el segundo: compilado más intérprete, 37–41 %.
- Las ayudas FMAC del VU1 compilado (`exactFlags`, `productSticky`, `nrm` y `macBits`) suman el 15,1–15,4 %.

## 6. Funciones más costosas (castillo, HECHO)

| Exclusivo | % | Inclusivo | % |
|---|---|---|---|
| `vu1c_a0dbee2aeac4dafa` (cuerpo del VU1 compilado) | 14,0 | `GSCpuBackend::Submit` | 49,4 |
| Lectores de VRAM (`ps2_gs_memory`) | 7,8 | `DrawTriangle` | 28,1 |
| `WritePixel` | 7,8 | `SampleTexture` | 22,8 |
| `DrawTriangle` | 6,7 | `DrawSprite` | 21,2 |
| `exactFlags` | 6,1 | `WritePixel` | 15,4 |
| `SampleTexture` | 5,0 | `VU1Interpreter::run` | 14,7 |
| `productSticky` | 3,8 | | |
| `commitReadyPipelines` | 3,8 | | |

`DmaChainState` (detección de ciclos con `unordered_set`) supone un 1,2–1,7 %.

## 7. Recuperación de símbolos

- **HECHO.** El exe `7571ae5b` no tiene mapa ni PDB. Los nombres se recuperaron cotejando los bytes del ejecutable
  con los `.obj` de su propio build, con las relocaciones enmascaradas y sin compilar nada.
- **Cobertura:** 94,7 % de `.text`; hojas sin resolver, 0,24 %.
- **Corrección de método (HECHO):** el primer cotejo aceptaba cuerpos con prefijos poco discriminantes. Daba
  símbolos diminutos falsos dentro de funciones grandes, con 13,5–15,6 % de hojas sin resolver y un reparto
  distorsionado.
  - El método definitivo exige un prefijo de al menos 2 bytes: 17 465 direcciones, todas con el mismo nombre que en
    el primer intento.
- **Limitación:** las funciones minúsculas podrían conservar algún nombre ambiguo, porque no se revisaron una a una.

## 8. Comparación con PERF_003

| | E4 (33 parches, castillo 27000–27600) | `runP` (30 parches) | PERF_004A (35 parches, castillo 26800–28000) |
|---|---|---|---|
| GS | 56,0 % | 55,0 % | 49,6 % |
| VU1 compilado | 26,1 % | 28,7 % | 31,4 % |
| VU1 intérprete | 10,2 % | 9,0 % | 9,8 % |

- **HECHO.** El GS sigue siendo el bloque principal en los dos escenarios.
- **Parche 35 sobre el juego completo:**
  - **HECHO.** En tiempo total, la diferencia entre 34 y 35 parches no se puede resolver: el signo es inconsistente
    entre pares y el ruido es de ±7 %.
  - **HECHO.** Normalizando PATH1/VU1 dentro de cada corrida, los 6 de 6 pares dan 35 mayor, entre +2,0 % y +5,3 %.
  - **ESTIMACIÓN.** +1,3 a +1,9 ms de PATH1 por VSync, en torno al 1 % del total. Es coherente con el ≈ +2 % del
    rasterizador medido en el banco de VISUAL_010.
- **Velocidad absoluta frente a PERF_003:** no se concluye. E4 llevaba un perfilador interno y el ruido es alto.

## 9. Experimentos anteriores descartados

Están documentados en [PERF_003_ARCHITECTURE_FEASIBILITY.md](PERF_003_ARCHITECTURE_FEASIBILITY.md) y no se repiten:

- **E1 (§10):** `__forceinline` en las ayudas FMAC del VU1 compilado. Las guardas de compilación lo detuvieron:
  memoria ×4,4, tiempo ×3,5 y código +60 %.
- **C2a-3 (§19):** acceso directo a VRAM en las rutas de dibujo. Ganancia < 3 %.
- **C2b-2 (§21):** empaquetado directo de `combineTexture`. Ganancia +0,1 %.
- **C2b-3 (§22):** redondeo e interpolación. Ninguna alternativa supera el umbral.

**Lección de método:** una concentración de muestras no es tiempo ahorrable si no se mide antes su coste marginal.

## 10. Cotas y estimaciones

**Cotas** (ESTIMACIÓN aritmética sobre la sección 5; **no son predicciones**):

| Si… | Oficina | Castillo |
|---|---|---|
| Actual (HECHO) | 162 ms, 6,2 VSync/s | 206 ms, 4,9 VSync/s |
| GS a coste 0 en el hilo del juego | 76 ms, 13,1 | 104 ms, 9,6 |
| VU1 a coste 0 | 102 ms, 9,8 | 121 ms, 8,3 |
| GS y VU1 a coste 0 | 16 ms, 64 | 19 ms, 53 |

**INFERENCIA:** llegar a 50 VSync/s exige reducir a la vez el GS y el VU1. Ninguno de los dos basta por separado.

**Candidatos y estimaciones de entonces** (ESTIMACIÓN, incertidumbre alta):

1. **GS asíncrono en un hilo propio:** cota ×2,0; estimación de **+40 a +80 %** de VSync/s.
   - PERF_004B bajó esa estimación a +30–60 %.
   - PERF_004D y PERF_004E midieron ×1,64–1,96 según el escenario y la carga
     ([PERF_004_PROGRESS.md](PERF_004_PROGRESS.md)).
2. **VU1:** una función por entrada y compilar las entradas que hoy se interpretan. Estimación de +9 a +16 % de
   VSync/s; pesa el doble una vez desacoplado el GS.
3. **Especialización del rasterizador por estado de lote:** estimación de −35 a −50 % del GS, **sin prototipo**.
4. **GPU:** no recomendado todavía. Elimina como máximo lo mismo que el candidato 1 sobre el hilo del juego.

**Dato a favor del candidato 1 (HECHO):** en `cap2` hay 0 lecturas de VRAM hacia el EE. Su frecuencia en ventanas
largas era UNKNOWN y la midió PERF_004C.

## 11. Limitaciones

- Solo cinemáticas sin mando.
- Ruido de ±7 % por carga ajena (sobre todo el navegador). Las diferencias menores del 2 % no se distinguen en tiempo
  total.
- Tres repeticiones por ejecutable y escenario; portátil sin afinidad ni frecuencia fijadas.
- Sin PMU: no hay datos de fallos de caché ni de predicción.
- Los porcentajes de muestras no son tiempo ahorrable por sí mismos.
- **Anomalía (HECHO):** el hilo de ventana se quedó sin CPU en 2 de las 6 corridas del castillo, sin efecto en la
  velocidad. Causa no determinada.
