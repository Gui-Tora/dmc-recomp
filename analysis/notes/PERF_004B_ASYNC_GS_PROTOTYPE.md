# PERF_004B: prototipo de GS asíncrono en banco aislado

**ELF:** `SLES_503.58` (PAL), SHA-256 `d0753a6b…bab4`, CRC32 `77654AD2`, entry `0x00100008`.

**Base:**

- `master` `529e9ed` (35 parches, vendor `0a696611`, sin cambios);
- rama local `perf-004b-async-gs-prototype`, sin commits.

**Fecha:** entre 2026-11-10 (PERF_004A) y 2026-11-17. El informe local tiene fecha de modificación 2026-11-17.

**Alcance:**

- No se ejecutó el juego ni PCSX2, ni se recompiló nada del juego.
- Solo se compilaron las 3 unidades del banco, enlazadas con el `ps2_runtime.lib` ya existente de 35 parches.

**Procedencia:** informe local `analysis/local/PERF_004B_ASYNC_GS.md` y código, scripts y resultados en
`analysis/local/perf004b/`. **No están versionados.**

**Etiquetas:** HECHO, INFERENCIA, HIPÓTESIS, ESTIMACIÓN, UNKNOWN.

**Conclusión:**

- El banco aislado demostró que el GS asíncrono es **paralelo y fiel byte a byte**.
- **No midió el rendimiento del juego completo.** Eso lo hicieron PERF_004C, que contó las barreras en el juego, y
  PERF_004D, que integró el backend y midió en el juego.

## 1. Motivación

Datos de [PERF_004A_RUNTIME_BASELINE.md](PERF_004A_RUNTIME_BASELINE.md) (HECHO):

- el rasterizador GS es el 50–53 % del hilo del juego;
- se ejecuta de forma síncrona dentro de `vertexKick` → `Submit`;
- la máquina tiene 15 hilos lógicos libres.

**Cota** (ESTIMACIÓN): si el GS se ejecuta en paralelo, el hilo del juego tarda como mucho `max(GS, resto)`, es
decir, ≈×2.

## 2. Arquitectura productor-consumidor

```text
 hilo del juego (productor)                      hilo GS (consumidor)
 GS frontend ── Submit/Upload/Transfer/Clear ─▶ ┌──────────────────────┐ ─▶ GSCpuBackend (sin cambios)
   (copia profunda + MXCSR por operación)       │ cola SPSC ordenada   │      rasteriza en el mismo orden
 ReadVram/Snapshot/TransferSnapshot/Consume ──▶ │ (anillo de ranuras)  │
   = barrera: espera a que la cola se vacíe     └──────────────────────┘
 Present ─▶ marca ordenada en la cola; el consumidor copia la VRAM en ese punto
```

- `AsyncGSBackend` implementa `GSRasterBackend` delante de cualquier backend interno. Se instala con
  `setRasterBackend`.
- El rasterizador (`GSCpuBackend`) **no cambia**.

## 3. Cola SPSC

- Anillo de ranuras de 672 bytes con índices `head` y `tail` atómicos.
- **Una ranura** puede llevar un lote, una transferencia, un contexto, los bytes de una subida o una petición de
  presentación.
- **Cola llena:** el productor espera.
- **Espera:** 4000 iteraciones de `_mm_pause` y después `condition_variable`. Cada lado tiene un indicador «durmiendo»
  (Dekker con `seq_cst`), así que solo se notifica a quien duerme de verdad.
- **Fallos** (diseño del prototipo):
  - una excepción del consumidor se guarda y la siguiente barrera la relanza;
  - las esperas tienen un plazo;
  - al cerrar, si el consumidor no termina, se desacopla.

  PERF_004D sustituyó este manejo por *fail-stop*.

## 4. Orden de operaciones

**Clasificación (HECHO, revisión del código del vendor):**

| Clase | Operaciones |
|---|---|
| Encolables, en orden estricto | `Submit`, `BeginTransfer` (cualquier dirección), `UploadImage`, `ClearFramebuffer`, `WriteVram`, `Reset` |
| Barrera (vaciar la cola antes de devolver) | `ReadVram`, `ConsumeLocalToHostBytes`, `GetTransferSnapshot`, `SnapshotVram`, `Initialize`, `Sync(Reset / LocalToHost / DebugReadback)` |
| Sin barrera | `Sync(Finish)`, porque el frontend pone CSR.FINISH de inmediato (INFERENCIA) |
| Sin efecto en el backend de CPU | `Flush`, `TextureFlush` |
| Marca ordenada | `Present` |

**`ClearFramebuffer`:** su resultado `bool` solo depende de los argumentos, así que el productor lo predice sin
esperar. En los tests hubo 0 predicciones erróneas.

**`ConsumeLocalToHostBytes`:** necesita una barrera completa. Su estado `pending` cambia el orden respecto a
`BeginTransfer`, de modo que una barrera parcial no basta.

## 5. Copias profundas

Cada operación se copia en la ranura, porque el llamante puede reutilizar sus búferes en cuanto la llamada vuelve:

- **Lote:** `GSPrimitiveBatch`, unos 0,3 KB, con el estado GS completo por valor. Es trivialmente copiable.
- **Bytes de subida:** su origen puede ser memoria del guest o un búfer de 8 bytes en la pila de `gs_frontend.cpp`.

**HECHO:** el backend no guarda punteros al frontend. CSR, SIGNAL, FINISH y LABEL los gestiona solo el frontend.

## 6. Propagación de MXCSR

- **HECHO.** El resultado del rasterizador depende del modo de redondeo. En `cap2`, 362 `Submit` se ejecutan en modo
  hacia cero (código VU1) y 26 493 en modo al más cercano.
- **Diseño:** el productor guarda en la ranura los bits de control de MXCSR del momento, y el consumidor los aplica
  antes de ejecutar la operación.
- **Control negativo (HECHO):** sin propagar MXCSR, la reproducción de `cap2` diverge, con VRAM final `86848e6f…`. En
  los tests también divergen 10 de 10 semillas. Las pruebas son sensibles a este error.

## 7. Sincronización de VRAM

**HECHO.** Accesos a la VRAM fuera del backend:

- `checkpoint_sections.cpp` lee y escribe `m_gsVRAM` en crudo al guardar y al cargar un checkpoint.
- No hay otros escritores: `GS::ReadVram` y `WriteVram` no tienen llamantes en el runtime ni en `runtime/`.

**Llamantes que forzarían una barrera en el juego** (HECHO, por búsqueda):

- `sceGsExecStoreImage`: local→host seguido de `Consume`.
- `GS::checkpointIdle`: solo al guardar.
- La instantánea de depuración.
- `GS::reset` y `setRasterBackend`.

## 8. Presentación

**HECHO (código):** `UploadFrame` se ejecuta en el hilo principal y copia la VRAM en un momento arbitrario respecto
al EE. Hoy funciona porque los dibujos son síncronos.

**Con GS asíncrono:**

- **Incorrecto:** copiar la VRAM donde vaya el consumidor. Mostraría registros `DISPFB` actuales con un contenido
  retrasado, es decir, búferes a medio dibujar.
- **Propuesto:** una marca ordenada en la cola. El consumidor copia la VRAM a un buffer triple en ese punto, y el
  presentador toma la última copia completa.

**Implementación:** el prototipo implementó `PresentAsync`. PERF_004B suponía que integrarla exigiría tocar
`gs_frontend.cpp` y `ps2_runtime.cpp`. PERF_004D lo resolvió sin tocarlos, con una marca pedida por el presentador
(ver [PERF_004D_ASYNC_GS_INTEGRATION.md](PERF_004D_ASYNC_GS_INTEGRATION.md)).

## 9. Checkpoints

**Requisito** (INFERENCIA, del código):

- vaciar la cola antes de guardar y antes de cargar, porque se lee y escribe `m_gsVRAM` en crudo;
- la carga debe dejar la cola vacía.

**Implementación:** no estaba en el banco. La añadió PERF_004D en `checkpoint_sections.cpp`.

## 10. Tests de concurrencia (HECHO, 23/23)

Cada test ejecuta el mismo guion determinista contra `GSCpuBackend` y contra el backend asíncrono, y compara todas las
salidas observables y la VRAM final. Cubren:

- **Cola:** vacía; llena, con capacidades 1, 2, 7, 64 y 4096 (3378 operaciones y 571 barreras).
- **Ritmos:** productor más rápido y consumidor más rápido.
- **Orden:** 2000 sprites opacos superpuestos; primitivas mezcladas con lecturas.
- **Transferencias:** h→l, l→l y l→h con `Consume`, intercaladas con dibujos (2215 salidas).
- **Lecturas y escrituras** de VRAM intercaladas, y presentación intercalada.
- **Fuzz:** 20 semillas × 1500 operaciones de todos los tipos y estados GS.
- **FPU:** cambio de redondeo entre dibujos, con su control negativo.
- **Ciclo de vida:** destrucción con 400 operaciones pendientes; búferes del llamante sobrescritos tras cada
  llamada.
- **Determinismo:** 10 repeticiones.
- **Fallos:** excepción inyectada (la barrera la relanza en 1,8 ms) y bloqueo inyectado (error en 215 ms, sin acceso a
  memoria liberada).
- **Reset** en mitad de una subida.

**Limitación:** no hay ThreadSanitizer para MSVC y no se instaló nada. La ausencia de carreras se apoya en el diseño y
en la igualdad de resultados en miles de repeticiones; **no está demostrada formalmente**.

## 11. Comparaciones byte a byte con GS Replay (HECHO)

**Captura:** `cap2` (castillo, VSync ~27101, 2 VSync): 28 631 registros y 26 855 `Submit`.

| Comparación | Resultado |
|---|---|
| Síncrono del banco frente al GS Replay de 35 parches de VISUAL_010 | `hashlog` (1681 hashes de VRAM y 2 presentaciones), VRAM final y PPM idénticos |
| Asíncrono con vaciado en cada hash, frente al síncrono | 1683 líneas idénticas |
| Asíncrono sin vaciados intermedios, 5 repeticiones | presentaciones y VRAM final idénticas (`d96b6394…`) |
| Asíncrono con capacidad 1 | idéntico |
| ZTE forzado | idéntico (`79787eec…`) |
| Sin propagar MXCSR (control negativo) | diverge |

`cap2` no contiene transferencias local→host, `ReadVram` ni `Clear`. Esas operaciones se cubren con los tests.

## 12. Coste de la cola (HECHO)

| Concepto | Medida |
|---|---|
| Encolar (copia y publicación) | 48–92 ns por operación; el 0,6–0,8 % del tiempo del GS con `cap2` |
| Desencolar y espera activa del consumidor | 1,8 ms sobre 194,6 ms de ejecución (0,9 %) |
| GS Replay puro sin trabajo que solapar | +0,5 % (núcleos físicos distintos) a +2 % (sin afinidad); +10 % en hermanos SMT |
| Memoria | 672 B por ranura: 2,6 MB con 4096 ranuras. Las ranuras de subida conservan su capacidad (hasta 701 KB en `cap2`) |

**INFERENCIA:** en el castillo, con ≈13 400 `Submit` por VSync, el encolado cuesta ≈0,7–1,2 ms por VSync (≈0,5 %).

**Retención de búferes de subida:** esta retención por ranura es el origen del crecimiento de memoria que PERF_004E
corrigió.

## 13. Núcleos físicos frente a SMT (HECHO)

- **Sin afinidad:** el planificador de Windows coloca a menudo los dos hilos en hermanos SMT, y el consumidor se
  ralentiza un 21–31 %.
- **Hermanos SMT forzados:** +36 %.
- **Hilos en núcleos físicos distintos:** el tiempo del consumidor dentro del rasterizador es igual al del GS solo
  (−2 % a −5 %).

**Corrección de método (HECHO):** en las primeras series, la carga sintética tardó ≈1,55 veces lo calibrado, y el GS
se ralentizó en la misma proporción.

- **Causa:** el reloj del portátil acelera ráfagas cortas y baja con carga sostenida.
- **Corrección:** las series definitivas calientan 4 s y miden «solo GS» y «solo trabajo» en cada ronda.
- Las primeras series se conservan en local, pero no se usan.

## 14. Resultados de rendimiento del banco (HECHO, banco; no juego)

**Carga sintética:** R = trabajo del productor / tiempo del GS solo. Capacidad 4096 y estado sostenido.

| R | Síncrono | Asíncrono, núcleos separados | Velocidad | Asíncrono sin afinidad | Velocidad |
|---|---|---|---|---|---|
| 0,5 | 480 ms | 323 ms | ×1,49 | 400 ms | ×1,22 |
| 1,0 | 643 ms | 410 ms | **×1,57** | 488 ms | ×1,34 |
| 2,0 | 961 ms | 679 ms | ×1,42 | 732 ms | ×1,37 |

- **Modelo aditivo:** el síncrono coincide con «solo GS + solo trabajo» (±2 %). Confirma el modelo de PERF_004A.
- **Solapamiento real entre hilos:** del 70 % al 88 % del hilo más corto.

**Capacidad de la cola** (R = 1, núcleos separados):

| Ranuras | Velocidad |
|---|---|
| 64 | ×0,73 (sin calentar) |
| 256 | ×1,26 |
| 1024 | ×1,21 |
| 4096 | ×1,57 |
| 16 384 | ×1,59 (ocupación máxima 5776) |

**INFERENCIA:** el flujo GS es muy irregular y una cola pequeña anula el solapamiento. Se recomendaron 8192 ranuras,
o una cola medida en bytes.

**Estimación para el juego** (HIPÓTESIS en su momento):

- +30 % a +60 % en el castillo, algo menos en la oficina;
- supone 0 barreras por VSync, una cola de al menos 8192 ranuras y que el reloj se mantiene con dos núcleos cargados.

La medida real de PERF_004D fue mayor: ×1,77 en el castillo y ×1,92–1,96 en la oficina limpia.

## 15. Limitaciones del modelo sintético

- **La carga del productor** es un bucle de coma flotante sobre 64 KB. No equivale al EE ni al VU1 reales: no
  comparte VRAM, no produce los lotes y no compite igual por la caché L3.
- **Barreras:** `cap2` son 2 VSync de una cinemática con 0 barreras necesarias. La frecuencia de barreras en el juego
  era UNKNOWN y la midió PERF_004C.
- **Potencia:** no se midió el efecto del límite de potencia del portátil durante minutos con dos núcleos cargados.
- **Integración pendiente:** el banco no implementaba la presentación ni los checkpoints integrados.
- **Gameplay:** sin escenario con mando.

## 16. Decisión de la fase

**Continuar.** Antes de integrar, contar en el juego las operaciones que serían barrera. Eso es PERF_004C:
[PERF_004C_GS_SYNCHRONIZATION.md](PERF_004C_GS_SYNCHRONIZATION.md).
