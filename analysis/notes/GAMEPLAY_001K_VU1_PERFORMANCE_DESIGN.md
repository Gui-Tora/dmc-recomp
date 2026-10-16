# GAMEPLAY_001K — Rendimiento del VU1: investigación y diseño

Fecha de inicio: 2026-10-15. Rama: `gameplay-001k` (desde `master` `f18921e`, `v0.1.0-alpha`).
ELF: `SLES_503.58`, SHA-256 `d0753a6b3b2f00802a50758a872d8cf051725aa31c839aa8894eee30ce58bab4`, CRC32 `77654AD2`,
entry `0x00100008`. Vendor: base `14b1e5c` + 18 parches (`patched_commit` `585c347e`).

Fase 1: solo investigación y diseño. Sin JIT, sin código copiado de BT3-Recomp, sin recompilación completa y sin
corridas largas.

## 1. Punto de partida (de GAMEPLAY_001J)

**HECHO** (perfil de 60 s en la oficina y contadores `[G001J:vu1]`; ver
[GAMEPLAY_001J_VISUAL_TRANSITION_DIAGNOSIS.md](GAMEPLAY_001J_VISUAL_TRANSITION_DIAGNOSIS.md)):

- El hilo del juego ocupa el 98 % de un núcleo: ~83 % en el intérprete VU1 y ~8 % en el GS por software.
- Reparto dentro del VU1: `commitReadyPipelines` 30 %, `normalizeOperand` 7,8 %, `run` 6,7 %,
  `calculatePairReadyCycle` 6,2 %, `updateFmacFlags` 5,9 %, `calculateFmacExactResult` 5,8 %, `execUpper` 4,4 %,
  `queueVfWrite` 3,1 %.
- Carga de la oficina: ~10 700 microprogramas y ~21,5 M ciclos VU1 cada 5 s, es decir:
  - ~630 programas y ~1,27 M ciclos por VSync;
  - **~2 000 ciclos por programa** de media;
  - 4,6–4,9 M ciclos VU1 por segundo de host, **~215 ns por ciclo VU1**.
- En la película, ~1 500 ciclos por VSync: la carga es despreciable.

## 2. Arquitectura actual del VU1 (vendor)

**HECHO** (código):

- **Archivos:**
  - `ps2xRuntime/include/runtime/ps2_vu1.h` (clase `VU1Interpreter`);
  - `src/lib/vu/ps2_vu1_core.cpp` (planificador, 1 838 líneas);
  - `ps2_vu1_upper.cpp` y `ps2_vu1_lower.cpp` (semántica).
  - Upstream `ran-j/PS2Recomp` no ha tocado `src/lib/vu` ni `ps2_vu1.h` desde la base `14b1e5c` (2026-08-18).
    Comprobado contra la API de GitHub el 2026-10-15; HEAD upstream `eb8688b2fd` (2026-10-14).
- **Invocación:**
  - es síncrona: el callback `MSCAL` del VIF1 (`ps2_runtime.cpp:670-687`) llama a
    `m_vu1.execute(code, data, startPC, top, itop, maxCycles = 65536)`;
  - `MSCNT` llama a `resume` (`ps2_runtime.cpp:688-705`);
  - el microprograma se ejecuta entero dentro de esa llamada, salvo que agote el presupuesto de 65 536 ciclos.
- **Efectos externos:** el estado (`VU1State`), la memoria de datos VU1, los paquetes PATH1
  (`submitGifPacket(Path1, …)` al terminar cada XGKICK) y los bits D/T (`vu0_vpu_stat`).
  - `state().cycles` solo lo lee el checkpoint; ningún otro código del runtime lo consume.
- **Bucle por par de instrucciones** (`run`, `ps2_vu1_core.cpp:1623-1838`):
  1. `commitReadyPipelines()` recorre siempre las 51 ranuras de las colas: flags 8, FDIV 1, EFU 2, stores 8,
     VF 16, VI 8, ACC 8.
  2. `calculatePairReadyCycle()` recorre lecturas VF por carril, 15 VI y 4 ACC.
  3. Ejecuta upper y lower sobre el estado.
  4. Deshace las escrituras y las reencola con su latencia (`queueVfWrite`, `queueViWrite`, `queueAccWrite`).
  5. `advanceOneCycle()` vuelve a recorrer las 51 ranuras y hace avanzar XGKICK.
- **Coste aritmético:**
  - cada operando pasa por `normalizeOperand` (sujeción de denormales e Inf/NaN);
  - cada resultado FMAC calcula el resultado «exacto» en `long double` (`calculateFmacExactResult`) para los flags
    de underflow y overflow, y actualiza MAC/status por carril.
- **Caché de decodificación:**
  - un único array de 2 048 pares, invalidado por `PS2Memory::m_vu1CodeGeneration`;
  - cualquier `VIF MPG` lo incrementa (`ps2_vif1_interpreter.cpp:438-456`) y fuerza a redecodificar los 16 KiB.
- **Restricción de compilación:** `ps2_vu1.h` lo incluye `ps2_runtime.h`, que a su vez incluyen las ~10 000
  unidades generadas.
  - Cualquier cambio en la clase recompila todo el código generado.
  - Cualquier diseño de fase 2 debe dejar `ps2_vu1.h` intacto.
- **Microcódigo:** en el checkpoint `dmc_checkpoint_mainmenu_abi2_tsnd_v1.dmck` (menú principal), la micromemoria VU1
  (región 6) y la memoria de datos VU1 (región 7) son **todo ceros**.
  - **INFERENCIA:** DMC sube los microprogramas por `VIF MPG` al entrar en cada escena.
  - El catálogo de programas hay que capturarlo en la escena, no en el checkpoint.

**HECHO (semántica temporal relevante, código):**

- **Registros VF/VI/ACC.** `calculatePairReadyCycle` bloquea la emisión hasta que están listos todos los carriles
  leídos. El scoreboard tiene interbloqueo, así que una lectura nunca observa un valor viejo.
  - El orden «última escritura gana» lo garantiza `m*LatestWrite`.
  - **INFERENCIA:** el valor observable equivale a escribir en orden de programa; lo único que cambia es el número de
    ciclos (stalls). Hay que comprobarlo en el banco A/B, incluidos los pares con `upperVfShadowReg`.
- **Recursos sin interbloqueo, cuyo valor depende del ciclo:**
  - Q (FDIV, 7 ciclos) y P (EFU);
  - flags MAC/status/clip (4 ciclos, leídos por `FMAND`/`FSAND`/`FCAND`/…);
  - lectura de VI por ramas (`readBranchVi`);
  - stores (1 ciclo, `queueStore`) frente a XGKICK, que copia un qword cada 2 ciclos desde la memoria de datos
    mientras el programa sigue y puede ver stores posteriores.

## 3. BT3-Recomp (z3xox/BT3-Recomp, commit `e22a34beb564e2d0f32bd60b02f0a95d9645ddcc`)

Estudio de solo lectura; clon en `analysis/local/gameplay_001k/bt3` (no versionado). No se ha copiado código.

**HECHO (código de BT3):**

- **Base.** Fork de PS2Recomp hacia el PR #140 (`5196a66`, 2026-07-04).
  - Es anterior al modelo de pipeline FMAC que tenemos (`f49ca4e`, PR #191).
  - Su intérprete es el «simple» antiguo, más pipelines propios de Q y CLIP.
- **Identificación.**
  - FNV-1a de 64 bits sobre `vuCode[0..extent)`, recalculado solo cuando cambia la generación MPG; caché
    `thread_local`.
  - Tabla lineal `{extent, hash, fn, needsMac}`. El PC de entrada se resuelve dentro de la función con *computed goto*.
  - Programas sin compilar: se interpretan y se vuelca su imagen.
  - Los programas se recortan del ELF con un manifiesto JSON ligado al SHA-256 del ELF; el `.inc` generado queda
    fuera de Git.
- **Generador `gen_vu1.py`.**
  - Una función C++ por programa, con una etiqueta por par y helpers SSE con plantilla sobre la palabra de
    instrucción.
  - Ramas con `goto`, ejecutando el delay slot antes. El bit E sale tras el delay slot.
  - **No modela latencia FMAC, stalls, retardo de flags ni retardo de VI en ramas.** Un par equivale a un ciclo.
  - Q y CLIP se modelan de forma perezosa: anotan el ciclo de vencimiento y solo sincronizan donde se observan.
  - XGKICK es síncrono, sin temporización de PATH1.
  - Ignora los bits M/D/T. Las ramas en delay slot solo generan un aviso.
  - Coma flotante: RZ+FTZ+DAZ por MXCSR; solo sujeta Inf/NaN en el resultado.
  - Flags MAC con `cmpeq`+`movemask`, omitidos por programa si este no los lee (`macelide`).
- **Fallback.** Programa entero cuando no coincide el hash; instrucción a instrucción para opcodes no implementados
  (MAX/MINI y otros).
- **Validación.** `PS2X_VUJIT_VERIFY=1` ejecuta el intérprete sobre una copia del estado, compara el estado, la memoria
  de datos y el hash de paquetes GIF, y afirma «7.7M runs, 0 mismatches».
- **Rendimiento.** No hay una medición aislada del recompilador genérico. Los kernels SSE escritos a mano por
  programa bajaron VU1 de ~230 a ~6 ms/s.
- **Licencia.** GPL-3.0, igual que PS2Recomp y que este repositorio. **INFERENCIA (no es asesoría legal):**
  reutilizar código sería compatible con atribución. En esta fase solo se toman ideas de diseño.

**INFERENCIA (comparación):**

- **Aprovechable como diseño:**
  - hash por generación MPG;
  - catálogo ligado al SHA-256 del ELF, con código generado fuera de Git;
  - helpers con la palabra constante;
  - fallback por programa e instrucción;
  - sincronización perezosa de Q, CLIP y flags;
  - modo VERIFY con hash GIF.
- **No transferible:**
  - su modelo de tiempo «par = ciclo» sin stalls: nuestro intérprete sí los modela y la equivalencia exige
    conservarlos;
  - las tablas y kernels de BT3;
  - el *computed goto*, que MSVC no soporta.
- **HIPÓTESIS:** BT3 no depende de la temporización FMAC; para DMC no está demostrado.

## 4. Arquitectura recomendada: recompilación estática con «scoreboard compilado»

1. **Catálogo:** programas identificados por FNV-1a de la imagen de microcódigo y PC de entrada.
   - El hash se recalcula solo cuando cambia `getVU1CodeGeneration()`.
   - El catálogo se captura en ejecución, porque el microcódigo no está en el checkpoint del menú.
   - Queda ligado al SHA-256 del ELF y guardado fuera de Git, como el código generado.
2. **Generador propio** (Python, desde cero): una función C++ por programa y entrada.
   - **Valores** VF/VI/ACC escritos en orden de programa, sin colas (§2).
   - **Ciclos:** stalls calculados con máximos sobre sellos «listo en el ciclo X» de los registros concretos que lee
     cada par. Es el modelo de `calculatePairReadyCycle`, desenrollado: 2–6 `max` por par en lugar de bucles y colas.
   - **Q, P, flags y clip:** cola mínima y perezosa (vencimiento + valor), consultada solo en los pares que los
     observan. Los flags se omiten si el programa no los lee, pero el valor final al acabar el programa debe ser el
     mismo.
   - **XGKICK:** la copia de PATH1 avanza perezosamente hasta el ciclo actual antes de cada store, del siguiente
     XGKICK y del final del programa. Así se reproduce exactamente qué stores ve el paquete.
   - **Coma flotante:** las mismas reglas de normalización que el intérprete, en helpers *inline*. Atajo: si los
     exponentes de los operandos no pueden producir overflow ni underflow, se evita el cálculo `long double`.
3. **Fallback al intérprete para el programa entero:** sin coincidencia, en `MSCNT`, si el presupuesto de 65 536
   ciclos podría agotarse, o con opcodes no soportados.
4. **Integración sin tocar `ps2_vu1.h`:**
   - Un parche general añade un registro de programas nativos (cabecera nueva, no incluida por `ps2_runtime.h`) y lo
     consulta en el callback `MSCAL` de `ps2_runtime.cpp`.
   - El catálogo y las funciones de DMC viven fuera del vendor (`runtime/` y código generado local).
   - Recompila `ps2_runtime.cpp`, la librería VU y el enlace; no el código generado del juego.
   - Diferencia aceptada: el contador privado `m_cycle` del intérprete no avanza durante los programas nativos. Solo
     afecta a `state.cycles`, que nadie lee salvo el checkpoint.

## 5. Identificar los programas calientes y su coste (sin corridas largas)

- **Captura** (instrumentación temporal en el callback `MSCAL`, activada por variable de entorno):
  - en cada llamada: `startPC`, `top`, `itop`, bits D/T, generación de código, `VU1State` y los 16 KiB de datos antes
    y después;
  - el microcódigo solo cuando cambia la generación.
  - Ventana: ~5 s de la oficina, unas 10 000 llamadas, limitable a N llamadas.
  - Corrida: checkpoint del menú → NEW GAME → película `0x232` (~1 min) → oficina. Unos 3–4 min en total.
- **Banco offline `vu1_replay`** (objetivo de prueba aparte, sin código generado del juego):
  - reejecuta el intérprete sobre el corpus;
  - verifica que reproduce los estados «después» del juego, es decir, que el corpus es fiel;
  - obtiene un histograma por `(hash, startPC)`: llamadas, ciclos VU1, tiempo de host, MPG por VSync y si algún
    programa agota el presupuesto.

## 6. Plan de validación A/B

1. **Offline, por llamada:** A = intérprete actual y B = nativo, sobre el mismo corpus. Se compara bit a bit:
   - `vf`, `vi`, `acc`, `q`, `p`, `i`, `r`, `pc`, `mac`, `status`, `clip`;
   - `stoppedByD`/`stoppedByT` y `ebit`;
   - los 16 KiB de datos;
   - el número de ciclos (Δ`cycles`);
   - la secuencia completa de paquetes PATH1 (bytes y orden).
   - Criterio: 0 diferencias en el 100 % de las llamadas. Un programa con una sola diferencia se queda en el
     intérprete.
2. **En el juego, modo VERIFY:** cada llamada nativa se repite en el intérprete sobre una copia y se aborta en la
   primera diferencia.
3. **En el juego, A/A y A/B de PATH1:** hash FNV por bloques de 1 000 paquetes de 001J. A/A ya coincide en 191 de 191
   bloques; B debe coincidir en todos.
4. **Pruebas unitarias por opcode:** con el montaje de `ps2_vu1_tests.cpp`.
5. **Límite:** la equivalencia se demuestra frente a nuestro intérprete, no frente al hardware.

## 7. Estimación (INFERENCIA)

- El hilo del juego tarda ~286 ms por VSync: VU1 ~83 % (~237 ms) y el resto ~49 ms.
  - **Techo con VU1 a coste cero:** ~20 VSync/s.
  - 5× en VU1 → ~10 VSync/s.
  - 10× → ~13–14 VSync/s.
  - 20× → ~16 VSync/s.
- **25 VSync/s no se alcanzan solo con el VU1:** después domina el GS por software y el resto.
- **Plausibilidad de 5–20×:** hoy cuesta ~215 ns (~1 000 ciclos de host) por ciclo VU1, casi todo en recorrer las
  colas. Un par compilado debería costar decenas de ciclos. No está medido.

## 8. Prototipo mínimo propuesto

1. Captura del corpus y banco `vu1_replay`, con el histograma de coste.
2. Generador para el programa más caro (o los que cubran ≥ 50 % de los ciclos), con el scoreboard compilado y
   fallback.
3. Medida offline: ciclos VU1 por segundo de host, A frente a B, con 0 diferencias.
4. Después, la integración en el juego con VERIFY y PATH1 A/B en una ventana corta de la oficina.

**Objetivo medible propuesto:**

- Offline: ≥ 5× ciclos VU1 por segundo de host en los programas que cubren ≥ 90 % de los ciclos de la oficina, con
  0 diferencias en el 100 % del corpus.
- En el juego: hash PATH1 idéntico a la base y ≥ 9 VSync/s en la oficina (Release).

**Riesgos:**

- Programas de otras escenas: los cubre el fallback, sin aceleración.
- Subidas MPG frecuentes, que invalidan la caché y obligan a recalcular el hash. Se medirá.
- Programas que agotan los 65 536 ciclos: van al intérprete.
- Ramas en delay slot y opcodes raros: fallback.
- MSVC sin *computed goto*: se usa un `switch` sobre los PCs de entrada.
- Esto acelera, no corrige: la equivalencia con el hardware no mejora.

## 9. Fase experimental 1: captura y banco `vu1_replay` (2026-10-15)

Autorizada por el usuario: captura activada por variable de entorno, una sola corrida desde el checkpoint del menú
hasta ~5 s emulados de la oficina (límite real de 8 min) y banco offline. Sin generador ni despachador nativo.

### 9.1 Instrumentación (temporal, documentada)

- **Cambios en el vendor** respecto al estado de los 18 parches (diff en
  `analysis/local/gameplay_001k/G001K_VU1_CAPTURE_INSTRUMENTATION.diff`):
  - nuevo `ps2xRuntime/src/lib/ps2_vu1_capture.inc`, incluido solo por `ps2_runtime.cpp`;
  - `ps2_runtime.cpp`: llamadas `g001k::before/after` alrededor de `execute` y `resume` en los callbacks `MSCAL` y
    `MSCNT`;
  - `ps2_memory.cpp`: gancho PATH1 en `submitGifPacket`.
- **Sin cambios en cabeceras.** Build incremental Release: `ps2_runtime.cpp`, `ps2_memory.cpp` (y `dmc_overrides.cpp`,
  recompilado por MSBuild) + enlace, 2 min.
  - Ejecutable `f705ee43e90aced085e3067c842ed72ad65fa6146ca1eb371debd1b922792ae0`.
  - Copia del limpio `a1934eb3…` en `analysis/local/gameplay_001k/`.
- **Desactivada por defecto.** Variables:
  - `DMC_VU1_CAPTURE_DIR` (activa la captura);
  - `DMC_VU1_CAPTURE_MAX_MB` (256);
  - `DMC_VU1_CAPTURE_SAMPLE_CYCLES` (80 000, muestreo ponderado por ciclos);
  - `DMC_VU1_CAPTURE_FIRST_PER_KEY` (4);
  - `DMC_VU1_CAPTURE_HEAVY_VSYNCS` (250).
- **Salida:**
  - `stats.csv` por `(hash, entrada, tipo)`: llamadas, ciclos, ns de host, mín./máx., cortes de presupuesto, D/T;
  - `vsync.csv`;
  - `images.csv` y `code_<hash>.bin` (una vez por imagen);
  - `samples.bin`: estado y datos antes/después más los paquetes PATH1 de las muestras.
- **Retirar** cuando termine la fase de aceleración, o convertirla en parche documentado si se decide conservarla.

### 9.2 Banco `tools/vu1_replay`

- Código propio. Enlaza la librería `ps2_runtime` Debug ya compilada de un árbol de compilación del vendor (raíz `vendor/PS2Recomp`, con tests), sin
  recompilar el runtime.
- Reejecuta cada muestra con el `VU1Interpreter` sin modificar y compara bit a bit:
  - VF, VI, ACC, Q, P, I, R, PC, MAC, status y clip;
  - bits E/D/T y estado de rama;
  - TOP/ITOP;
  - delta de ciclos;
  - los 16 KiB de datos;
  - la secuencia de paquetes PATH1.
- **MSCNT:** se reproduce reiniciando el planificador con un `execute` de presupuesto 0 y restaurando el estado. Es
  exacto si la llamada anterior terminó limpia, y lo fue en todas: `budget_hits = 0`.
- **`--profile 1`:** reejecuta cada muestra ciclo a ciclo (`resume` con presupuesto 1) y atribuye cada ciclo al PC del
  par emitido o en espera. Solo se usa la muestra si el resultado ciclo a ciclo es también idéntico al capturado.
- **Errores propios corregidos durante la fase:**
  - el delta de ciclos usaba el contador absoluto capturado en lugar del contador privado del replay;
  - el bucle del modo perfil no arrancaba por la misma causa;
  - en ambos casos se detectó antes de sacar conclusiones.
  - El heredoc de bash volvió a convertir `\n` en saltos reales; se corrigió con la herramienta de edición.

### 9.3 Resultados

**Corridas:**

- Prueba corta `cap_smoke`: 90 s y 32 MiB. 901 muestras: 901 exactas.
- **Corrida única `cap_office`:**
  - del checkpoint `dmc_checkpoint_mainmenu_abi2_tsnd_v1.dmck` + un Cross a 250 VSyncs pesados (≥ 100 000 ciclos VU1);
  - 133 s reales, `DONE` por objetivo;
  - 153 266 llamadas;
  - 3 779 muestras completas, 140,7 MB (`samples.bin` sha256 `1ff9ca49…`).

**HECHO (capturas):**

- **Una sola imagen de microcódigo:** `a0dbee2aeac4dafa` (16 KiB, 1 627 pares no vacíos).
  - Se sube una vez (generación MPG 11, VSync 20359) y no cambia; 0 diferencias al volver a calcular el hash.
- **Tres puntos de entrada**, con un solo camino de código (conjunto de PCs) cada uno:

  | Entrada | Llamadas | Ciclos | % ciclos | Host (Release) | ns/ciclo | Ciclos/llamada (mín.–máx., media) |
  |---|---|---|---|---|---|---|
  | `0x0300` MSCNT | 74 905 | 226,7 M | 73,7 % | 47,6 s (68,5 %) | 210 | 138–3 427, 3 027 |
  | `0x00b8` MSCNT | 39 157 | 80,0 M | 26,0 % | 21,6 s (31,1 %) | 270 | 215–3 405, 2 042 |
  | `0x0000` MSCAL | 39 204 | 0,94 M | 0,3 % | 0,23 s (0,3 %) | 246 | 24 |

- **Oficina estable:** ~613 llamadas y ~1,17–1,23 M ciclos por VSync. La película `0x232` no usa el VU1.
- **Perfil por PC** (3 767 muestras, estimador corregido por el muestreo ponderado por longitud):
  - Solo se ejecutan 98 pares.
  - Bloques:

    | Bloque | Pares | % ciclos | Espera | Contenido |
    |---|---|---|---|---|
    | `0x0178–0x0278` | 32 | 63,8 % | 0 % | bucle (`IBEQ`) |
    | `0x0280–0x02d8` | 11 | 21,9 % | 0 % | bucle (`IBNE`) |
    | `0x02e8–0x0300` | 3 | 12,1 % | 99,2 % | XGKICK + bit E: espera de la transferencia PATH1 |
    | `0x00b8–0x0178` | 24 | 1,1 % | 20 % | preparación |
    | resto | — | ~1 % | — | — |

  - Cobertura: 50 % → 1 bloque (21 pares); 80 % → 2 bloques (36 pares); 90 % → 3 bloques (41 pares).
  - El 12,3 % de todos los ciclos son esperas, casi todas al final, por PATH1.
- **Instrucciones del camino caliente:**
  - lower: LQ 29, MOVE 22, IADDIU 11, SQ 6, IAND/IADD 3, DIV 2, IBEQ 2, IBNE 1, XGKICK 1, XTOP 1, LQI 1,
    ILWR 1, MR32 1, MTIR 1, FCAND 1, B 1; 3 pares con bit I;
  - upper: CLIP 1, MULq 2, MADDq 1, MULi 1 y FMAC variadas.
  - No hay `FMAND`/`FSAND`/`FMOR`/`FSOR`/`FMEQ`/`FSEQ`, ni EFU, ni bits M/D/T.
- **Replay:**
  - 3 779/3 779 exactas en modo normal y 3 779/3 779 en modo ciclo a ciclo;
  - control negativo (un bit de VF antes, un byte de datos después, un byte de PATH1): 3/3 detectadas.

**INFERENCIA:**

- En la oficina, los «microprogramas» de DMC son un único camino: continuación `0x0300` → `B` → `0x00b8` → dos bucles
  → XGKICK y bit E. Lo invocan `MSCNT` alternos.
- Los bucles calientes no tienen esperas: el microcódigo ya está planificado a mano. La temporización exacta se
  reduce a contar pares, más Q (DIV → MULq/MADDq), clip (CLIP → FCAND), ramas sobre VI y la carrera
  stores/XGKICK.
- Los flags MAC/status no se leen en el camino caliente, pero su valor final por llamada debe seguir siendo idéntico.
- El 12 % de los ciclos es espera de PATH1 al final del programa. Compilado, esto es aritmética de ciclos más una copia
  de memoria.

**Lo que el banco todavía no puede reproducir (UNKNOWN o fuera de alcance):**

- `resume` tras un corte de presupuesto: no observado.
- El respaldo de VI de rama que cruza llamadas: no observado; todo exacto.
- El valor absoluto de `state.cycles`: se compara el delta.
- El efecto de PATH1 en el GS: se comparan los bytes.
- Otras escenas: la captura solo cubre la oficina.
- El tiempo de host del banco es Debug y no es comparable con Release.

### 9.4 Recomendación (pendiente de autorización)

**Primer objetivo de compilación:** el camino `MSCNT 0x0300/0x00b8 → bit E` de la imagen `a0dbee2aeac4dafa`.

- Son 75 pares y cubren el 99,7 % de los ciclos de la oficina.
- La medida principal es el bucle `0x0178–0x0278` (63,8 %).
- La unidad mínima es la llamada completa, porque el intérprete no puede retomar un programa a mitad sin cambiar
  `ps2_vu1.h`.
- La validación está lista: `vu1_replay` sobre `cap_office`, con 0 diferencias exigidas en las 3 779 muestras.

## 10. Fase experimental 2: generador propio y A/B offline (2026-10-15)

Autorizada por el usuario. El generador y el banco quedan separados del runtime del juego. No se modifica `ps2_vu1.h`,
no se integra nada en el juego, no se copia código de BT3 y no hay compilación completa.

### 10.1 Implementación

- **`tools/vu1_replay/gen_vu1c.py`**, propio:
  - porta la decodificación de uso y latencias del intérprete (`decodeUpperUsage`/`decodeLowerUsage`), además de
    `calculatePairReadyCycle` y `markPairWrites`;
  - emite una función C++ por imagen, con una etiqueta por par;
  - para cada entrada, recorre los caminos alcanzables hasta el delay slot del bit E;
  - **rechaza la entrada completa** si contiene algo fuera del subconjunto soportado: JR/JALR, ramas o bit E en un
    delay slot, bits D/T, EFU, FSSET/FCSET, lectores de MAC/status u opcodes no implementados.
- **`tools/vu1_replay/vu1c_support.h`**, propio: reexpresa las reglas del intérprete. Los valores se escriben en
  orden de programa (§2) y solo se modelan con tiempo:
  - Q (FDIV);
  - la cola de clip;
  - el valor final de MAC/status;
  - el respaldo de VI en ramas;
  - stores frente a XGKICK, con avance perezoso de PATH1;
  - el vaciado final de pipelines.
- **Código generado:** `analysis/local/gameplay_001k/gen/vu1c_a0dbee2aeac4dafa.cpp` (ignorado por Git; sha256
  `08f74318…`).
  - 105 pares; entradas `MSCAL 0x0000`, `MSCNT 0x00b8` y `MSCNT 0x0300`; ninguna rechazada.
- **Banco Release:** enlaza `ps2_runtime.lib` Release, compilada en ese árbol de compilación del vendor (solo la
  librería, sin código del juego) con `/O2 /Ob2`, los mismos flags Release que el juego.
- **Errores propios detectados al revisar el código antes de ejecutar:**
  - el mapeo de las variantes ACC;
  - el bit E terminaba en el propio par;
  - en `MSCAL` se tocaba el estado antes de validar la entrada;
  - faltaba una etiqueta tras el delay slot del bit E.

  También hizo falta una tabla de funciones vacía para el enlace Release, porque la librería con la captura arrastra
  `ps2_runtime.obj`.

### 10.2 Resultados

**HECHO (equivalencia, banco Release):**

- **Corpus `cap_office`:** 3 779/3 779 muestras idénticas bit a bit entre el código compilado y la captura del juego.
  - Comprobado: registros, Q/P/I/R, MAC/status/clip, bits de parada y de rama, memoria de datos, ciclos y paquetes
    PATH1.
  - Es el mismo resultado que da el intérprete.
- **Prueba corta `cap_smoke`:** 901/901.
- **Control negativo `cap_smoke_perturbed`:** el código compilado da las mismas 3 diferencias que el intérprete, en los
  mismos campos.
- **Fallback (9/9 comprobaciones):**
  - una entrada desconocida devuelve `UnsupportedEntry` sin tocar el estado;
  - una rama pendiente en `MSCNT` devuelve `Precondition`;
  - un presupuesto insuficiente devuelve `Budget` tras ejecución parcial, que el llamador debe descartar.
- **Fuzzing diferencial** (8 variantes deterministas por muestra: bits de datos, valores especiales en datos y VF,
  registros VI): 30 232 ejecuciones.
  - 26 620 idénticas al intérprete y **0 diferencias**.
  - 3 453 rechazos por presupuesto; en esos casos el intérprete tampoco terminaba.
  - 159 rechazos `Fault`, que coinciden con las 159 instrucciones reservadas que informa el intérprete por paquetes
    GIF malformados.
  - Salidas cubiertas: `0x0300` 23 162, **`0x0348` 3 370** (la región `0x0310–0x0340`, nunca ejecutada en el corpus
    real) y `0x00b8` 88.
- **Casos exigidos, todos dentro del corpus:** DIV → MULq/MADDq, CLIP → FCAND, `IBEQ` en ambos sentidos, el bucle
  `IBNE`, XGKICK con la espera de PATH1 (el 99 % del bloque final) y las esperas del bloque `0x00b8`.

**HECHO (rendimiento):** mismo equipo y mismas muestras, Release, mínimo de 5 repeticiones por muestra; las cifras
son la media de 3 procesos.

| Entrada | Intérprete (ns/ciclo VU) | Compilado (ns/ciclo VU) | Aceleración | % de ciclos en la partida |
|---|---|---|---|---|
| `MSCAL 0x0000` | 204,0 | 4,42 | 46,2× | 0,3 % |
| `MSCNT 0x00b8` | 194,3 | 19,27 | 10,1× | 26,0 % |
| `MSCNT 0x0300` | 194,4 | 19,15 | 10,2× | 73,7 % |
| **Ponderado por ciclos de la partida** | 194,4 | 19,13 | **10,16×** | — |

- Variación entre procesos: 10,11×, 10,16× y 10,14× ponderado por muestras.
- El intérprete del banco Release (194 ns/ciclo) es coherente con el del juego medido en la captura (210–271
  ns/ciclo).
- **Costes separados por muestra:**
  - selección del programa, 36–124 ns;
  - copia de entrada (estado + 16 KiB), 200–234 ns;
  - verificación del banco, 0,6–1,0 µs;
  - ejecución compilada, ~53–62 µs por llamada de 0x00b8/0x0300 (~3 000 ciclos).
- **Fallback:** ninguno en el corpus real.
  - El rechazo por entrada o precondición no ejecuta nada.
  - El rechazo por presupuesto cuesta la ejecución parcial más el intérprete completo. **INFERENCIA:** hasta
    ~65 536 × 19 ns ≈ 1,3 ms desperdiciados por llamada en el peor caso; no se observó en la partida
    (`budget_hits = 0`).

**INFERENCIA:**

- El objetivo de ≥ 5× se cumple con margen: 10× sin vectorizar ni simplificar flags.
- El nuevo coste por ciclo VU (~19 ns, ~80 ciclos de host) es probablemente el cálculo exacto de flags por carril
  (`long double` + clasificación, más `productSticky` en las MADD) y la cadena de esperas con sellos de 64 bits.
  **HIPÓTESIS sin perfilar.**
- **No se extrapola a VSync/s.** La estimación de Amdahl de §7 queda como orientación, no como resultado.

**Pendiente antes de integrar (no iniciado):**

- **Despachador en el juego:** consulta por `(hash, tipo, PC)`.
- **Snapshot:** de estado, datos y paquetes, solo para poder descartar en `Budget`/`Fault`.
- **PATH1 en búfer:** retener los paquetes hasta `Ok`.
- **Ciclo privado:** decidir cómo reflejar el contador privado `m_cycle` del intérprete sin tocar `ps2_vu1.h`.
- **Verificación en el juego:** modo VERIFY y A/B del hash PATH1.
- **Cobertura:** solo la escena de la oficina.

## 11. Fase experimental 3: integración en el juego (2026-10-16)

Autorizada por el usuario. Rama local `gameplay-001k`; sin commit. El código generado de DMC sigue fuera de Git.

### 11.1 Implementación

**Despachador** `vendor/PS2Recomp/ps2xRuntime/src/lib/ps2_vu1_native_dispatch.inc`:

- Lo incluye solo `ps2_runtime.cpp`, y entra en los callbacks `MSCAL`/`MSCNT`.
- Sin `PS2X_VU1_NATIVE`, se usa la ruta original. Con ella hay tres modos:
  - `interpreter`;
  - `native`: compilado con fallback;
  - `verify`: el intérprete es autoritativo y el compilado se ejecuta sobre copias y se compara campo a campo.
- Opciones: `PS2X_VU1_PATH1_HASH`, `PS2X_VU1_STATS_SEC` y `PS2X_VU1_TRACE_CALLS`.
- Identifica el programa por FNV de la imagen (solo cuando cambia la generación MPG), tipo y PC.

**Soporte genérico** movido a `vendor/.../src/lib/vu/ps2_vu1_native.h`; `tools/vu1_replay/vu1c_support.h` lo reenvía.

**Build:**

- `CMakeLists.txt` raíz: variable `DMC_VU1C_SOURCES`, vacía por defecto, con las fuentes generadas locales.
- La reconfiguración con el CMake del árbol (VS 3.31.6) solo añadió una línea `ClCompile` al `vcxproj`; verificado
  con diff.
- Builds incrementales de ~2 min: `ps2_runtime.cpp` + fuente generada + enlace.
- No se ha modificado `ps2_vu1.h`.

**Estado y fallback (HECHO por diseño; verificado en §11.2):**

1. **Rollback:**
   - copia de `VU1State` y de los 16 KiB de datos antes de cada llamada compilada;
   - PATH1 retenido en un búfer y enviado en orden solo con `Ok`;
   - con `Budget`/`Fault`/rechazo se restaura todo, se descartan los paquetes y ejecuta el intérprete;
   - las colas de la versión compilada son locales y no tocan el intérprete.
2. **Continuidad compilado → intérprete:**
   - antes de que el intérprete continúe un `MSCNT` tras una llamada compilada, `execute(…, 0 ciclos)` reinicia su
     planificador y después se restaura el estado;
   - ese es exactamente su estado tras un final limpio: colas vacías, clip de trabajo = clip, sin respaldo de VI;
   - el generador rechaza programas cuyo último par dejaría un respaldo de VI.
3. **Continuidad intérprete → compilado:** un `MSCNT` compilado solo continúa una llamada compilada que terminó `Ok`
   (`chainBlocked` en caso contrario). `MSCAL` siempre puede usar el compilado.
4. **Contador privado de ciclos:**
   - el intérprete sobrescribe `state.cycles` con su contador privado, que no avanza en las llamadas compiladas;
   - el despachador acumula los ciclos compilados en un offset y deja `state.cycles = privado + offset` tras cada
     llamada interpretada;
   - con eso `state.cycles` vale en cada momento lo mismo que con el intérprete solo, también para la
     serialización de checkpoints;
   - los deltas del intérprete se miden sobre el contador privado, porque tras cargar un checkpoint `state.cycles`
     no tiene por qué cumplir el invariante.
   - **Error propio detectado en las pruebas aisladas:** el primer diseño medía el delta como diferencia de
     `state.cycles`.

### 11.2 Validación

**Pruebas aisladas** (`vu1_replay --dispatch-tests 1`, el `.inc` real compilado en el banco):

| Prueba | Ejecuciones | Fallos |
|---|---|---|
| T1: cadena compilada (PATH1 retenido y enviado) | 3 779 | 0 |
| T2: intérprete tras llamada compilada (cebado) frente a la cadena solo de intérprete | 3 779 | 0 |
| T3: rollback por `Budget` | 482 | 0 |
| T3: rollback por `Fault` | 25 | 0 |
| T4: Verify | 3 779 | 0 |

**Fuzz con llamada de continuación:** 30 232 ejecuciones, 0 diferencias. Continuaciones desde `0x0300` (23 162),
`0x0348` (3 370) y `0x00b8` (88), todas idénticas.

**En el juego**, checkpoint `dmc_checkpoint_mainmenu_abi2_tsnd_v1.dmck` + un Cross:

- **Verify:**
  - 91 800 llamadas, 0 diferencias (40 s de oficina);
  - dos Verify largos: 274 436 y 274 873 llamadas, 0 diferencias, 0 omitidas, 0 rechazos, hasta el VSync
    23982/23983, que incluye la entrada `0x0348`.
- **Hash PATH1 por bloques de 1 000 paquetes:**
  - Interpreter frente a Verify (A/A): 68/68 iguales en contenido y VSync.
  - **Divergencia observada y explicada:** Native frente a Interpreter con arranques no alineados daba 0/73 bloques
    iguales.
    - **HECHO:** el VSync de la primera llamada VU1 depende solo del retardo en tiempo de host del Cross del
      driver: 0,5 s → 20360/20361/20362; 0,6 s → 20367; 0,7 s → 20373; 0,8 s → 20379.
    - **HECHO:** la primera llamada es idéntica en ambos modos (entrada, salida, PATH1, ciclos) salvo el VSync. En la
      segunda ya difiere la memoria de datos de **entrada**, que el EE envía por VIF.
    - **HECHO:** con el mismo VSync inicial, las primeras 2 400 llamadas coinciden en entrada, salida, PATH1, ciclos y
      VSync entre Interpreter y Native (todas compiladas).
    - **HECHO:** con arranque alineado (20361), 5/5 corridas Native coinciden con el Verify largo en los 216 bloques
      (216 000 paquetes), en contenido **y** VSync, hasta el VSync 23995.
    - **INFERENCIA:** la divergencia la provoca la entrada temporizada por host, no el VU1 compilado.
    - En una corrida alineada aparte se vio un desfase constante de +2 VSyncs con contenido idéntico (73/73). Encaja
      con fases asíncronas de host (IOP/CD) durante la carga; es una **HIPÓTESIS** sin aislar.
- **Sin fallos, bloqueos ni instrucciones reservadas** en ninguna corrida.

### 11.3 Rendimiento real

Release, sin captura, Verify, hash ni traza. La comparación es sobre la misma ventana emulada de la oficina, VSync
23540–23744.

| Modo | VSync/s | ms de host por VSync | ms VU1 por VSync | Fracción VU1 | Llamadas |
|---|---|---|---|---|---|
| Interpreter | 3,29 | 304,1 | 280,9 | 92 % | 100 % interpretadas |
| Native | **12,48** | 80,1 | 25,6 | 32 % | 100 % compiladas |

- **Native, oficina completa** (VSync 23567–24300, 733 VSyncs): 12,62 VSync/s; 470 172 llamadas, todas compiladas;
  0 fallback, 0 interpretadas, 0 sin programa.
- **Antes de compilar `0x0348`:** la parte posterior bajaba a 5,05 VSync/s (38,7 % interpretado por la regla de
  cadena). Con `0x0348` compilada sube a 12,86 VSync/s.
- **Desglose medido en Native** (502 VSyncs, ejecutable `e9a21aa2…`): 79,8 ms por VSync.
  - VU1 compilado: 26,3 ms.
  - **Envío PATH1** (GIF + GS por software): 30,7 ms.
  - Resto (EE, VIF, planificador…): 22,8 ms.
- **Medición:** en modo intérprete el procesado GS de PATH1 ocurre dentro de la llamada VU1 y cuenta como tiempo VU1.
  **INFERENCIA:** el intérprete VU1 propiamente dicho costaba ~250 ms por VSync.
- **Nuevo cuello de botella:** el GS por software (~38 % del tiempo de host); después, el VU1 compilado (~33 %).

**Objetivo de la fase (≥ 9 VSync/s sin diferencias de comportamiento): cumplido**, con 12,5–12,9 VSync/s en la
oficina.

### 11.4 Estado local y pendiente

**Vendor** (diff en `analysis/local/gameplay_001k/G001K_VENDOR_CHANGES_INTEGRATION.diff`):

- `ps2_runtime.cpp` y `ps2_memory.cpp` modificados;
- `ps2_vu1_capture.inc`, `ps2_vu1_native_dispatch.inc` y `vu/ps2_vu1_native.h` nuevos.

**Pendiente:**

- Separar en parches la captura (diagnóstico, retirable) y el despachador con el soporte (genérico).
- Documentar el procedimiento de generación local.
- Decidir cómo hacer determinista el Cross del driver (por VSync emulado) para futuras comparaciones.
- Cubrir otras escenas: hoy solo existe la imagen `a0dbee2aeac4dafa`; el resto cae al intérprete.

## 12. Cierre de GAMEPLAY_001K (2026-10-16)

### 12.1 Consolidación

**Instrumentación de captura retirada del runtime:**

- `ps2_vu1_capture.inc`, sus llamadas en `ps2_runtime.cpp` y el gancho de captura en `ps2_memory.cpp`.
- Su diff se conserva en local (`analysis/local/gameplay_001k/G001K_VU1_CAPTURE_INSTRUMENTATION.diff` y
  `G001K_VENDOR_CHANGES_INTEGRATION.diff`), junto con las capturas.
- El banco sigue leyendo esas capturas locales.

**Parche 19** `patches/GAMEPLAY_001K_vu1_native_dispatch.patch` (sha256 `9100fbb6…1dec` en el cierre; sustituido en §12.5), contra el estado de los 18
parches:

- `vu/ps2_vu1_native.h` y `ps2_vu1_native_dispatch.inc`, nuevos;
- `ps2_runtime.cpp`: el despachador en los callbacks `MSCAL`/`MSCNT`;
- `ps2_memory.cpp`: un observador PATH1 opcional (`g_ps2xPath1Observer`) que sustituye al gancho de captura;
- todo en ASCII;
- `upstream.lock.json`: 19 parches, `patched_commit` `f6a23de9bd0eb60bb11ec5dbba8162fc7104c9b7` en el cierre (actual:
  `912b1c23…`, §12.5).

**Herramientas del proyecto:** `tools/vu1_replay/` (generador, banco, `make_vu1c.py` y su manifiesto). Ver su README.

**Sin cambios en `ps2_vu1.h`** ni en cabeceras incluidas por el código generado. Todas las compilaciones fueron
incrementales.

### 12.2 Reproducibilidad del código nativo

**HECHO:** el microcódigo está en el propio ELF. La imagen `a0dbee2aeac4dafa` sale exacta (16 KiB idénticos a la
captura) al aplicar sobre una memoria vacía los 10 comandos VIF `MPG` situados en los offsets de archivo
`0x3F4184`–`0x3F6E74`. Los dos `MPG` siguientes (`0x3F7554`, `0x3F7AF4`) suben variantes a `0x3980` en otros momentos
y no forman parte de esta imagen.

**Procedimiento:**

```
python tools/vu1_replay/make_vu1c.py
```

1. Comprueba el SHA-256 del ELF.
2. Reconstruye la imagen.
3. Comprueba su hash.
4. Genera `recomp/vu1c/vu1c_a0dbee2aeac4dafa.cpp`.

Después basta un build incremental.

- **Manifiesto** `tools/vu1_replay/dmc_vu1_programs.json`: solo metadatos (identidad del ELF, 10 offsets, hash y 4
  entradas).
- **CMake raíz:** compila `recomp/vu1c/*.cpp` si existen (glob `CONFIGURE_DEPENDS`). Les da el include del soporte con
  una propiedad por fuente (`TARGET_DIRECTORY`), así que el resto de fuentes no cambia de flags. El primer intento sin
  `TARGET_DIRECTORY` no aplicaba la propiedad; se detectó en el diff del `vcxproj`.
- **`.gitignore`:** `/recomp/vu1c/`.

**Validado:**

- `make_vu1c.py` regenera byte a byte el mismo archivo.
- Un ELF con un byte alterado se rechaza sin generar nada.
- Sin `recomp/vu1c/`, el juego compila (enlace incremental) y, con `PS2X_VU1_NATIVE=native`, informa
  `compiled_entries=0` e interpreta las 28 088 llamadas sin fallos.

### 12.3 Validación final

**Replay del patchset:** 19 parches → `f6a23de9…` (`matches_lock: true`); el vendor es idéntico al replay.

**Offline** (banco Release enlazado con la librería del vendor ya parcheada y con el C++ de `make_vu1c.py`):

- **Pruebas aisladas, 0 fallos:**

  | Prueba | Ejecuciones |
  |---|---|
  | T1: cadena compilada | 3 779 |
  | T2: continuidad intérprete tras compilado | 3 779 |
  | T3: rollback `Budget` | 482 |
  | T3: rollback `Fault` | 25 |
  | T4: Verify | 3 779 |
  | **T5: Verify detecta una divergencia inyectada** (1 bit de `vf5.y`; resultado autoritativo intacto) | 3 779 |

- **A/B:** 3 779/3 779 exactas; 9/9 comprobaciones de fallback.
- **Fuzz:** 30 232 ejecuciones con continuaciones desde `0x0300`, `0x0348` y `0x00b8`, 0 diferencias.
- **Control negativo:** las mismas 3 diferencias en ambos motores.

**En el juego** (ejecutable `a65c4201…`, mismas fuentes que el final `e42d5dae…`):

| Modo | Resultado |
|---|---|
| Sin variable (por defecto) | ninguna línea del despachador; corre normalmente |
| `native` | 168 464 llamadas, todas compiladas; ~12 VSync/s |
| `verify` | 41 513 llamadas comparadas, 0 diferencias |

**Comparaciones PATH1** (§11.2):

- Interpreter frente a Verify: 68/68 bloques.
- Native frente a Verify con arranque alineado: 216/216 bloques en contenido y VSync, en 5 de 5 corridas.
- Verify largos: 274 436 y 274 873 llamadas, 0 diferencias.

### 12.4 Limitaciones conocidas

1. **Arranque temporizado por host.** El Cross de NEW GAME lo envía el driver por tiempo de host, así que el VSync de
   la primera llamada VU1 varía entre corridas (20360–20362 con 0,5 s). Dos corridas solo son comparables bloque a
   bloque si arrancan en el mismo VSync. No afecta a Verify, que compara cada llamada con las mismas entradas.
   Pendiente: inyectar el Cross por VSync emulado.
2. **Desfase de +2 VSyncs con contenido idéntico** en una corrida alineada: **HIPÓTESIS** de fases asíncronas de host
   (IOP/CD); no aislado.
3. **Cobertura:**
   - solo la imagen `a0dbee2aeac4dafa` (escena de la oficina posterior a la intro), con entradas `0x0000`, `0x00b8`,
     `0x0300` y `0x0348`;
   - otras imágenes o entradas se interpretan (`noProgram`);
   - un `MSCNT` que sigue a una llamada interpretada se interpreta hasta el siguiente `MSCAL` (`chainBlocked`).
4. **La equivalencia es con nuestro intérprete, no con el hardware:** sus defectos conocidos se conservan.
5. **El generador soporta un subconjunto de instrucciones:**
   - quedan fuera JR/JALR, EFU, FSSET/FCSET, lectores de MAC/status, bits D/T y ramas en delay slots;
   - si una entrada los necesita, se rechaza entera.
6. **Coste de `Budget`/`Fault`:** se repite la llamada en el intérprete. No se ha observado en el juego.
7. **Captura retirada:** generar un corpus nuevo exige reaplicar el diff local de captura.
8. **Rendimiento:** ~12,5 VSync/s en la oficina, lejos de 50. El GS por software (~31 ms por VSync) es ahora el mayor
   coste. El juego sigue **NOT PLAYABLE** y el bloqueo `sceDmaRecvI` no cambia.

### 12.5 Ajustes de la revisión del PR #4 (2026-10-16)

**1. Comparación de `dBitEnabled` y `tBitEnabled` en Verify**

- **Defecto:** `diffState()` del despachador no comparaba `dBitEnabled` ni `tBitEnabled`, así que Verify no cubría todo
  `VU1State`.
- **Corrección:** ahora se comparan.

**2. Modo estricto `PS2X_VU1_VERIFY_STRICT=1`**

- Tras informar de la primera divergencia, imprime `[VU1:verify] FATAL` y termina el proceso con código 3.
- Sin la variable, Verify mantiene el comportamiento anterior: registra y continúa.
- El fallo pasa por un manejador sustituible (`Dispatcher::strictFailure`) para poder probarlo; el detalle de la primera
  divergencia queda en `Dispatcher::firstMismatch`.

**Pruebas aisladas** (`vu1_replay --dispatch-tests 1`; T1–T9 sobre 3 779 muestras, 0 fallos):

| Prueba | Qué comprueba |
|---|---|
| T6 | alterar solo `dBitEnabled` se detecta como exactamente `dBitEnabled` |
| T7 | alterar solo `tBitEnabled` se detecta como exactamente `tBitEnabled` |
| T8 | en modo estricto, una divergencia inyectada en `vf5` llama al manejador una sola vez con ese detalle, y el resultado autoritativo del intérprete queda intacto |
| T9 | en modo estricto sin divergencia, no se llama al manejador |

- **Terminación real:** con el manejador por defecto (`--strict-exit 1`), el proceso termina con código 3 tras imprimir
  la discrepancia y la línea `FATAL`.

**Reproducibilidad:**

- Parche 19 regenerado (sha256 `90372be1…`).
- Replay de 19 parches → `912b1c23ac1f1984e93a930d620fd02ac873b100`; el vendor coincide.
- Build incremental del juego: solo `ps2_runtime.cpp`.
