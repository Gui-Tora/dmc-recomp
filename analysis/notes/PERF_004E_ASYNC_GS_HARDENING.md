# PERF_004E: validación final y memoria del GS asíncrono (candidato a parche 36)

**ELF:** `SLES_503.58` (PAL), SHA-256 `d0753a6b…bab4`, CRC32 `77654AD2`, entry `0x00100008`.

**Base:**

- `master` `529e9ed` (35 parches, vendor `0a696611`);
- rama local `perf-004e-async-gs-hardening`, sin commits.

**Fechas:** 2026-11-17/18.

**Estado:**

- El parche 36 es un **candidato experimental**: no está en `patches/` ni en `upstream.lock.json` y no se ha
  publicado.
- El GS asíncrono sigue **desactivado por defecto** (`DMC_GS_ASYNC` sin definir o `0`).

**Procedencia:** informe local `analysis/local/PERF_004E_RESULTS.md` y datos, series, exes y candidato en
`analysis/local/perf004e/`. **No están versionados.**

**Etiquetas:** HECHO, INFERENCIA, HIPÓTESIS, UNKNOWN.

**Fase anterior:** [PERF_004D_ASYNC_GS_INTEGRATION.md](PERF_004D_ASYNC_GS_INTEGRATION.md).

## 1. Motivo del crecimiento de memoria

**HECHO (PERF_004D).** Con el GS asíncrono, la RSS subía:

- de 157 a 217–219 MB en la oficina;
- de 178 a 288–290 MB en el castillo.

**Causa (HECHO, código y estadísticas nuevas):**

- Cada ranura de la cola conservaba el búfer de su última subida host→local, si medía 64 KB o menos.
- No había ningún límite global.
- Con 8192 ranuras, eso retenía 76–77 MB en la oficina y 137 MB en el castillo.

## 2. Política anterior y política nueva

**Política anterior** (PERF_004D):

- `uploadRetainBudget = ~0`, es decir, sin límite global;
- límite de 64 KB por ranura;
- reserva con el crecimiento geométrico de `assign`.

**Política nueva**, el único cambio de código respecto a PERF_004D:

- Nueva opción `uploadRetainBudget`, el presupuesto global de bytes retenidos, **0 por defecto**.
- Se mantiene `uploadRetainBytes`, el límite por búfer, en 64 KB.
- Tras ejecutar una subida, el consumidor libera el búfer, salvo que quepa en los dos límites. Con el valor por
  defecto, **lo libera siempre**.
- El productor reserva el tamaño exacto.
- Estadísticas nuevas: `uploads`, `upload_allocs`, `upload_frees`, `upload_held(_max)` y los tiempos de reserva y
  liberación.
- Variables nuevas: `DMC_GS_ASYNC_UPLOAD_BUDGET` y `DMC_GS_ASYNC_UPLOAD_RETAIN`.

**No cambian:** las barreras, la presentación ni el *fail-stop*.

## 3. RSS media y máxima (HECHO)

**Fase 3 (comparación de políticas):** con carga de ffmpeg y python del usuario, entre 30 y 290 %. MB.

| Escenario | Política | VSync/s | RSS media | RSS máx. | Retenido máx. |
|---|---|---|---|---|---|
| Oficina | PERF_004D | 10,93 | 219 | 261 | — |
| Oficina | antigua | 11,02 | 219 | 261 | 76–77 |
| Oficina | presupuesto 0 | 10,74 (pareja con ruido: 10,40 / 11,09) | 178 | 183 | 0,6 |
| Oficina | presupuesto 8 MB | 11,37 | 186 | 192 | 8,4 |
| Castillo | PERF_004D | 9,35 | 288 | 340 | — |
| Castillo | antigua | 9,42 | 288 | 340 | 137 |
| Castillo | presupuesto 0 | 9,43 | 196 | 198 | 0,8 |
| Castillo | presupuesto 8 MB | 9,40 | 205 | 210 | — |

**Benchmark final con el exe candidato** (sección 13):

| Escenario | Síncrono (media / máx.) | Asíncrono (media / máx.) | Retenido |
|---|---|---|---|
| Oficina | 157 / 163 MB | 176 / 180 MB | ≤ 0,6 MB |
| Castillo | 177 / 183 MB | 196 / 202 MB | ≤ 1,1 MB |

**INFERENCIA.** El exceso que queda, +19 MB, es estructural: las ranuras de la cola (≈5,6 MB) más las 3 copias de VRAM
de 4 MB del buffer triple de presentación.

**Decisión:** presupuesto 0 por defecto. Con 8 MB no se mejora la velocidad de forma medible.

## 4. Coste de reservas y liberaciones (HECHO)

Ventanas del benchmark final:

| Escenario | Subidas por ventana | Reserva (productor) | Liberación (consumidor) | Coste por VSync |
|---|---|---|---|---|
| Oficina (~580 VSync) | ≈5300 (134 MB) | 2,5–3,1 ms | 17,6–25,9 ms | ≈0,04 ms |
| Castillo (~1200 VSync) | ≈14 000 (337 MB) | 4,1–5,2 ms | 43–57 ms | ≈0,05 ms |

Las esperas por cola llena son iguales con todas las políticas.

**INFERENCIA:** liberar siempre cuesta menos del 0,1 % del tiempo por VSync.

## 5. Tests adicionales (HECHO)

| Suite | Resultado |
|---|---|
| Tests de concurrencia (PERF_004B) | 23/23 |
| Tests de integración (PERF_004D) | 11/11 |
| `tests3`, política de memoria (nuevos) | 19/19 |

`tests3` cubre:

- VRAM y contabilidad con capacidades 8192 y 64, y con presupuestos ilimitado, 0, 256 KB y 4 MB;
- reutilización frente a liberación;
- el límite por ranura;
- estrés con 20 semillas y capacidades de 2 a 8.

**Corrección de método (HECHO).** La primera versión del banco fijaba por defecto la política antigua, así que las
reproducciones sin opción explícita no probaban la nueva. Se corrigió para tomar los valores por defecto de
`gsasync::Options{}` y se repitió la suite. El primer intento se conserva en local.

## 6. Fidelidad del candidato definitivo (HECHO)

**El exe probado es el candidato exacto:** `60aa4d18…`, compilado con exactamente el código propuesto. El plan de
Ninja fue de 2 tareas.

**GS Replay `cap2`:**

- **Síncrono:** el registro de hashes, la VRAM final y la reproducción con ZTE forzado son iguales a las referencias de
  35 parches.
- **Asíncrono:** VRAM final igual a la síncrona en estos modos (hash final `97605ff2…` en 9 de 9 casos):
  - libre;
  - capacidad 1;
  - presupuestos 0, 8 MB e ilimitado.
- **ZTE forzado:** `c81568ef…` en los dos modos.
- **Sin MXCSR:** diverge (`d92d8de2…`), como se espera.

**Juego:**

- Los guardados síncrono y asíncrono en la oficina, tick 16450, son idénticos en las 12 secciones, y también al de
  PERF_004D.
- Se recargó ese guardado 3 veces y se guardó en el 16480: los 3 guardados son idénticos.
- Colas de 1 y 16 ranuras: 0 fatales.
- Cierre con `WM_CLOSE`: código 0 en 0,19 s.

**Diferencias entre exes:**

| Exe | Contenido | Uso |
|---|---|---|
| `7ab8b44e` | candidato de PERF_004D + volcado de cuadros `DMC_FRAME_DUMP` | diagnóstico de PERF_004D |
| `60aa4d18` | candidato definitivo, sin volcado | **todas las medidas y validaciones** |
| `d61498ec` | candidato definitivo + volcado de cuadros | **solo** las capturas de menús, FMV y gameplay |

## 7. Equivalencia del modo desactivado (HECHO)

**Serie dedicada:** referencia `7571ae5b` frente al candidato sin `DMC_GS_ASYNC`. 4 corridas de cada uno por escenario,
alternadas.

**Condición de carga:** todas las corridas tuvieron un render de Blender del 190–650 %, porque no hubo corridas sin
carga.

**Huellas funcionales idénticas en las 16 corridas:**

| Huella | Oficina | Castillo |
|---|---|---|
| Ciclos VU1 por VSync | 2,08 M | 2,47 M |
| Líneas DMA | 128 | 128 |
| Líneas stub | 49 | 6 |
| Fatales | 0 | 0 |

Además: RSS igual con una diferencia de ±2 MB, y 0 líneas `[GS:async]`.

**Velocidad:**

- oficina: diferencia ≤ 0,3 %, sin contar la primera corrida;
- castillo: el candidato fue un +7,4 % más rápido de media, dentro de la dispersión que causaba la carga.

**Benchmark final** (sección 13): el candidato desactivado fue un 3–7 % más rápido que la referencia en el castillo, y
en la oficina la diferencia quedó dentro del ruido.

**INFERENCIA:** no hay regresión. En modo desactivado, el camino caliente es el mismo código que el de la referencia.

**HIPÓTESIS:** la diferencia del castillo se debe a la disposición del binario o al ruido de carga. No está
demostrada.

## 8. Menús y FMV (HECHO, con el exe de capturas `d61498ec`)

- El checkpoint «mainmenu» arranca en realidad en la demo de atracción. En los dos modos, la secuencia es la misma:
  1. DOWN+CROSS salta la demo;
  2. pantalla WARNING;
  3. FMV MPEG de introducción;
  4. título.
- **Comparación de cuadros:** 3247 hashes únicos comunes. 92 solo aparecen en síncrono y 38 solo en asíncrono, todos
  junto a los instantes de entrada, cuyo tiempo real difiere entre corridas. Todos los cuadros son de 512×448.
- **Navegación del título:** DOWN lleva a OPTION y UP a NEW GAME. El menú Opciones se abre y se dibuja igual en los dos
  modos, con el mismo hash de último cuadro.
- **Transitorio:** un cuadro con texturas de VRAM aparece solo en síncrono; en asíncrono, ese instante se ve negro.
  **INFERENCIA:** es un transitorio de presentación previo, no una regresión.
- **VSync sin operaciones GS** (pantalla estática de Opciones): se sigue mostrando el último cuadro.
- **Cambios de resolución:** no se observó ninguno. Su comportamiento es **UNKNOWN**.

## 9. Entrada por archivo y movimiento de Dante (HECHO)

- **Entrada:** el pad se controla por archivo (`DMC_P311_PAD_FILE`), sin modificar el sistema de entrada.
- **Recorrido:** desde el checkpoint del castillo hasta el VSync 28520.
  - Duración: 328 s en síncrono y 192 s en asíncrono.
  - Dante corre, gira y se desplaza, y la cámara lo sigue.
  - Síncrono y asíncrono muestran las mismas poses en los mismos ticks.
  - Aparece el texto de la puerta del castillo.
- **Cielo:** los bloques de color del cielo aparecen también en síncrono. Es un defecto conocido, no del backend.
- **Corrección de método (HECHO):** en pantallas estáticas, el driver de entrada solo recibe el tick cada 60 VSync.
  Las pulsaciones duran más y se autorrepiten, lo que explica posiciones de cursor distintas entre corridas.

## 10. Limitaciones en ataque y pausa

- **No demostrado:** tras START no se vio el menú de pausa.
- **No demostrado:** las acciones de ataque son visualmente ambiguas en las capturas.

**No se afirma que el mando funcione por completo.** Solo están comprobados:

- la dirección y la confirmación en los menús;
- el movimiento en el castillo.

## 11. Latencia de 1–2 VSync (HECHO)

Retraso entre el VSync del guest y la imagen presentada, en las corridas asíncronas del benchmark final:

| Escenario | Publicados | Reemplazados (sin imagen nueva) | Obsoletos | 1 VSync | 2 VSync | ≥3 |
|---|---|---|---|---|---|---|
| Oficina | 567–570 | 53–71 | 0 | ≈63 % | ≈37 % | 0 |
| Castillo | 1194–1199 | 16–18 | 0 | ≈99 % | ≈1 % | 0 |

- **Síncrono:** presenta con 0 VSync de retraso.
- **Simulación:** la latencia es solo de presentación y no afecta a la simulación (guardados idénticos).

**INFERENCIA:**

- A la velocidad actual, 1 VSync asíncrono dura unos 92–140 ms, y un VSync síncrono dura 175–250 ms. En tiempo real,
  la latencia de imagen es comparable o menor.
- A 50 VSync/s, el modo asíncrono añadiría 1–2 cuadros (20–40 ms). No se ha medido a esa velocidad.

## 12. Riesgo de la memoria en vuelo en la pantalla de Opciones (HECHO)

En la pantalla estática de Opciones, en asíncrono:

- la cola se llena (8192 operaciones);
- las subidas encoladas y aún no ejecutadas llegan a **82 MB**;
- la RSS pasa de 177 a 256 MB.

En el banco, `tests3` muestra el mismo fenómeno: con capacidad 8192 y presupuesto 0, el máximo en vuelo es de 150 MB,
y con capacidad 64 baja a 2,7 MB.

**La reducción de la memoria retenida no resuelve por sí sola este límite.** Ningún presupuesto de retención puede
liberar datos que aún no se han ejecutado. El techo actual es la capacidad de la cola por el tamaño de las subidas.

**Propuesta: PERF_004F, limitar la cola también por bytes.** Cambiaría el control de flujo del productor.

**Estado: pendiente.** No se ha empezado ni hay resultados.

## 13. Benchmarks finales (HECHO)

**Configuraciones:**

- **A, referencia:** `7571ae5b`, master.
- **B:** candidato `60aa4d18` sin variables.
- **C:** candidato con `DMC_GS_ASYNC=1`.

**Corridas:** 3 de cada configuración por escenario, en orden alternado. La carga de Blender no fue constante, así que
las corridas se agrupan por carga y no se mezclan.

**Oficina** (VSync/s):

| Carga | A | B | C | C/A |
|---|---|---|---|---|
| Baja (Blender al 0 % al empezar) | 6,611 | 6,573 | 10,899; 10,819 | **×1,64** |
| Con Blender (176–858 %) | 5,279; 5,392 | 5,707; 5,480 | 8,533 | ×1,60 |

**Castillo** (VSync/s):

| Carga | A | B | C | C/A |
|---|---|---|---|---|
| Blender ≈175 % al empezar | 4,017 | 4,161 | 7,035; 7,153; 7,192 | **×1,77** |
| Blender 733–919 % | 4,038 | 4,137 | — | — |
| Baja | 5,224 | 5,576 | — | — |

- **Huellas:** las mismas en las 18 corridas, con 0 fatales.
- **CPU del proceso:** ≈110 % en síncrono y ≈190 % en asíncrono.
- **Castillo sin carga:** no hubo ninguna corrida asíncrona sin carga.

**Comparación con PERF_004D:** allí se midió ×1,77 en el castillo y ×1,92–1,96 en la oficina limpia. Las cifras no son
directamente comparables, porque cambian la carga externa y la máquina en cada sesión.

## 14. Candidato a parche 36 y su identidad

Material experimental, solo en local, en `analysis/local/perf004e/candidate/`:

| Elemento | Valor |
|---|---|
| Parche `PERF_004D_gs_async_backend.patch` (el nombre se conserva) | SHA-256 `b84293a25e8d97dd1307c3497055fcc2201004d162ec2af60f3f640e4d57537f` |
| 35 parches desde el commit base `14b1e5cb` | `0a696611` (coincide con el lock de master) |
| 35 + candidato | árbol `5e1d939e9b63813a02f142dae8436644a5c9fe21`, commit `2a072bacb033aec925fd4a908d9f6950984a0e62` |
| Lock propuesto | `93ec64a3…`. Solo cambian la entrada 36 y `patched_commit`; los 35 primeros son idénticos a master |
| Cambio de `runtime/dmc_overrides.cpp` | diff `a03baae4…` |
| Exe probado | `60aa4d18…` |

Sustituye a la identidad de PERF_004D (`2c81f09c…`, commit `531b04a7…`).

**Pendiente de autorización:** aplicarlo a `patches/` y al lock, el *bootstrap* limpio y la publicación.

## 15. Riesgos pendientes

1. Memoria en vuelo con la cola llena (sección 12). Pendiente de PERF_004F.
2. Gameplay validado de forma parcial: no se confirmaron el ataque ni la pausa.
3. Cambio de resolución no observado.
4. Carga externa variable durante las series. No hay corrida asíncrona del castillo sin carga.
5. Latencia de 1–2 VSync a velocidad completa, sin medir.
6. Local→host sin consumir ([PERF_004C_GS_SYNCHRONIZATION.md](PERF_004C_GS_SYNCHRONIZATION.md) §10) y GS-5. Ninguno
   está en el alcance del GS asíncrono.
7. Sin detector formal de carreras.
