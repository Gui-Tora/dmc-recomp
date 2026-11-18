# PERF_004F: presupuesto de bytes en la cola del GS asíncrono (parche 36)

**ELF:** `SLES_503.58` (PAL), SHA-256 `d0753a6b…bab4`, CRC32 `77654AD2`, entry `0x00100008`.

**Base:** `master` `5f64f1f` (35 parches, vendor `0a696611`). Investigación en la rama local
`perf-004f-gs-queue-byte-budget`, sin commits. Promoción en la rama `perf-004f-gs-async-patch36`.

**Fechas:** 2026-11-18.

**Estado:** el parche 36 (`patches/PERF_004D_gs_async_backend.patch`) se propone para integración. El GS asíncrono
sigue **desactivado por defecto**: solo actúa con `DMC_GS_ASYNC=1`.

**Procedencia:** informe local `analysis/local/PERF_004F_RESULTS.md` y datos, series, exes, bench y candidato en
`analysis/local/perf004f/`. **No están versionados.**

**Etiquetas:** HECHO, INFERENCIA, HIPÓTESIS, UNKNOWN.

**Fase anterior:** [PERF_004E_ASYNC_GS_HARDENING.md](PERF_004E_ASYNC_GS_HARDENING.md). Entrada de la línea:
[PERF_004_PROGRESS.md](PERF_004_PROGRESS.md).

## 1. Problema observado en Opciones

La cola de PERF_004E solo estaba limitada por ranuras (8192). Cada subida host→local lleva su carga en un búfer
propio de la ranura.

**HECHO (PERF_004E):** en la pantalla estática de Opciones el productor va por delante del consumidor. La cola llegaba
a 8192 ranuras con unos 82 MB de subidas en vuelo, y el RSS crecía de 177 a 256 MB.

**HECHO (PERF_004F, exe sin límite de bytes):** se reprodujo con estas cifras:

- 8192 ranuras;
- 165 236 esperas por cola llena;
- 78,8 MB pendientes;
- RSS de 181 a 262 MB.

## 2. Diseño del presupuesto por bytes

`Options::pendingUploadBudget` (cabecera `gs_async_backend.h`) limita los bytes de carga de subidas **publicados y aún
no ejecutados**. Es independiente del número de ranuras.

- **Contabilidad exacta:**
  - cada `UploadImage` con datos cuesta `sizeBytes`;
  - sin datos, cuesta 0;
  - las demás operaciones también cuestan 0.

  El coste se guarda en la ranura. El contador atómico (`seq_cst`) solo lo incrementa el productor, bajo su mutex, y
  solo lo decrementa el consumidor.
- **Reserva antes de publicar:** el productor reserva antes de tomar la ranura y publicarla. Si la reserva superase el
  presupuesto, espera. Si la toma de ranura falla (fallo del consumidor), devuelve la reserva antes de propagar el
  error.
- **Liberación al consumir:** el consumidor devuelve el coste después de ejecutar o descartar la operación y antes de
  avanzar la cola. Después avisa al productor si está esperando.
- **Espera:** es la misma que la de cola llena:
  - giro y después variable de condición con el protocolo Dekker de `m_producerWaiting`;
  - mismo tiempo máximo;
  - misma política de fallo (*fail-stop*: el proceso termina con código 70).
- **Orden:** la cola sigue siendo FIFO SPSC. El presupuesto solo retrasa la publicación; no se pierden ni reordenan
  operaciones.
- **Sin interbloqueo (INFERENCIA, cubierta por tests):** si el productor espera por bytes, hay subidas publicadas sin
  ejecutar. El consumidor las ejecuta, o las descarta tras un fallo, y devuelve sus bytes. Si no avanza, se aplica el
  tiempo máximo y *fail-stop*.
- **Sin cambios:**
  - MXCSR por operación;
  - presentación (buzón y buffer triple);
  - barreras de checkpoint;
  - rasterizador, VU1 y EE.
- **Estadísticas nuevas** en la línea `[GS:async]`: `pend`, `pend_max`, `bwaits`, `bwait_tk` y `oversize`.

## 3. Operaciones mayores que el presupuesto

**Excepción al límite nominal:** una subida individual mayor que todo el presupuesto se admite cuando no hay ningún
byte pendiente. Mientras está en vuelo, el pendiente supera el presupuesto, pero solo por esa operación.

- La siguiente subida espera hasta que esa se consume, porque la condición de admisión exige `p ≤ presupuesto` o
  `p = 0`. Por tanto, nunca hay más de una subida sobredimensionada en vuelo, ni acumulación adicional sobre ella.
- El máximo en vuelo es `max(presupuesto, mayor subida individual)`.
- Las operaciones sin carga (coste 0) siguen limitadas solo por las ranuras.
- Se cuentan en `oversize`.

## 4. Resultados con 16, 32 y 64 MiB

### Opciones (una corrida de 200 s por variante)

| Variante | RSS antes → máx. (MB) | Ranuras máx. | Pendiente máx. (MB) | Esperas por bytes |
|---|---|---|---|---|
| Síncrono (referencia `7571ae5b`) | 161 → 163 | — | — | — |
| Asíncrono PERF_004E | 189 → 209 | 2898 | 27,5 (retenido) | — |
| 16 MiB | 249 → 257 ¹ | 1682 | 16,0 | 10313 |
| 32 MiB | 179 → 212 | 3363 | 32,0 | 7767 |
| 64 MiB | 189 → 246 | 6699 | 64,0 | 12693 |
| Sin límite | 181 → 262 | 8192 | 78,8 | 0 |
| 32 MiB por defecto, sin variable | 178 → 180 | 423 | 3,9 | 0 |

¹ Base anómala de 249 MB. PERF_004E ya observó bimodalidad del RSS entre arranques, así que este incremento no es
comparable.

- **HECHO:** cuando hay retraso acumulado, el pendiente se queda en el presupuesto exacto. El crecimiento de RSS sigue
  al presupuesto: +33 MB con 32 MiB, +57 MB con 64 MiB y +81 MB sin límite.
- **HECHO:** el retraso acumulado varía de una corrida a otra. La corrida por defecto no acumuló, y la de PERF_004E de
  esta fase solo llegó a 27,5 MB.
- **HECHO:** 0 cuadros obsoletos.

### Oficina y castillo

**HECHO:** ningún presupuesto actúa. En las 36 corridas asíncronas con presupuesto hubo 0 esperas por bytes, y el
pendiente llegó como máximo a 0,62 MB en la oficina y a 6,6 MB en el castillo.

## 5. Motivo de 32 MiB

- La velocidad no distingue entre 16, 32 y 64 MiB, porque en juego el límite no se alcanza.
- 32 MiB da 4,8× de margen sobre el máximo observado en juego (16 MiB daría 2,4×). Eso reduce el riesgo de frenar
  escenas no medidas con ráfagas de subidas.
- Limita el peor caso de Opciones a unos +33 MB de RSS, frente a +81 MB.
- Es el valor compilado en el exe medido (`130bfa42`).
- `DMC_GS_ASYNC_PENDING_BUDGET` permite cambiarlo.

## 6. Tests

**HECHO:** 85/85, con la cabecera del parche 36. Bench local con las fuentes gs del vendor.

| Conjunto | Contenido | Resultado |
|---|---|---|
| `tests` | concurrencia (PERF_004B–D) | 23/23 |
| `tests2` | integración | 11/11 |
| `tests3` | memoria de subidas (PERF_004E) | 19/19 |
| `tests4` | presupuesto (nuevo) | 32/32 |

`tests4` cubre:

- igualdad del resultado y respeto del límite con capacidades 8192, 64 y 1 y presupuestos de 64 KiB a sin límite;
- consumidor lento: sin límite se llega a 51,7 MB pendientes;
- subidas mayores que el presupuesto y fronteras S, 2S−1 y 2S;
- cola llena más presupuesto con operaciones mixtas (20 semillas);
- presentador concurrente;
- excepción del consumidor durante la espera;
- bloqueo del consumidor: da tiempo máximo, no interbloqueo;
- subidas vacías o nulas.

## 7. Fidelidad con GS Replay

Captura `cap2` (castillo, 2 VSync).

- **HECHO:** el síncrono da los hashes de referencia de 35 parches (final `97605ff2a9703f79`, ZTE forzado
  `c81568efc6ca86a1`).
- **HECHO:** el asíncrono es idéntico al síncrono en hashlog, en VRAM final byte a byte y con ZTE forzado. Se probó
  en estas configuraciones:
  - libre ×3;
  - capacidad 1;
  - presupuestos de 64 KiB, 1 MiB, 16, 32 y 64 MiB y sin límite.
- **HECHO:** el control sin MXCSR diverge (`d92d8de2cef4ce75`), como se esperaba.

**Checkpoints en juego (HECHO):** las 12 secciones son idénticas en todos estos casos:

- guardados síncrono, asíncrono por defecto y asíncrono con 64 KiB;
- frente a PERF_004E;
- en recargas ×3.

**Estrés (HECHO):**

- Castillo con presupuesto de 1 B y 16 ranuras: 2112 subidas sobredimensionadas, todas solas, sin fallos.
- `WM_CLOSE` bajo presión de presupuesto: cierre en 0,16 s con código 0.

**Validación de la promoción (HECHO).** Se repitió con el árbol promovido:

- **Vendor:** en `247af590`, igual a un clon nuevo con `apply_patchset`.
- **Exe incremental:** sus fuentes son byte a byte las del exe medido (`130bfa42`); difiere solo en 4 bytes de marcas
  de tiempo del PE.
- **Tests:** 85/85, con el bench compilado contra la cabecera del vendor.
- **GS Replay:** igual que arriba en todas las configuraciones.
- **Checkpoints:** las 12 secciones idénticas en los mismos guardados, recargas y comparaciones con PERF_004E.
- **Arranque:**
  - en asíncrono, `pending budget 33554432` por defecto;
  - sin `DMC_GS_ASYNC`, el log no contiene la línea `[GS:async] installed`.
- **Cierre:** con `WM_CLOSE`, en 0,20 s con código 0.

## 8. Rendimiento

Series alternadas de 17 corridas por escena:

- referencia síncrona;
- asíncrono de PERF_004E;
- asíncrono de PERF_004F con 16, 32 y 64 MiB;
- exe de PERF_004F sin `DMC_GS_ASYNC`.

| Escena | Referencia | PERF_004E | PERF_004F (32 MiB) | Observación |
|---|---|---|---|---|
| Oficina, sin carga externa | 6,27 | 9,52 / 10,77 | 10,88 / 10,88 | PERF_004F no es peor |
| Castillo, carga moderada | 4,07 | 7,11 | 6,94 | −2,3 % frente a PERF_004E |

En VSync/s.

- **HECHO:** con carga comparable, la diferencia de PERF_004F frente a PERF_004E en el castillo va de −1,8 % a
  −2,3 % según el presupuesto. Está dentro de la dispersión entre corridas.
- **HIPÓTESIS:** se debe a la carga o al enlazado. El presupuesto nunca actuó y su coste es de unas 12 subidas por
  VSync con una operación atómica cada una.
- **HECHO:** sin `DMC_GS_ASYNC`, la diferencia frente a la referencia va de −7 % (oficina, con carga) a +5 %
  (castillo). El signo cambia, así que no es repetible.
- **HECHO:** la memoria en juego no cambia frente a PERF_004E: RSS media de 178 MB en la oficina y 196 MB en el
  castillo.
- **Mejora del GS asíncrono:** sigue siendo la medida en PERF_004E en sus condiciones, ×1,64 en la oficina y ×1,77
  en el castillo.

## 9. Limitaciones por carga externa

Durante las series había un render de Blender activo de forma intermitente. El contador de rendimiento del procesador
bajó de 122–126 % a 98–111 %. Las corridas más cargadas son más lentas en todas las variantes. Solo se comparan
corridas vecinas con carga parecida; las medias globales mezclan condiciones.

## 10. Comparación de imágenes

**No se hizo** comparación de imágenes presentadas con `DMC_FRAME_DUMP` en esta fase. El cambio no toca el código de
presentación. La evidencia es la siguiente:

- GS Replay y checkpoints bit a bit;
- 0 cuadros obsoletos;
- latencia de presentación equivalente a PERF_004E.

## 11. Restricciones actuales de gameplay

- Validados con pad por archivo en PERF_004E: menús, FMV y movimiento de Dante.
- **Sin validar:** combate (ataque) y menú de pausa en asíncrono.
- Ninguna medida de rendimiento en gameplay con mando.
- El juego no es completamente jugable y no alcanza 50 VSync/s.

## 12. Activación y desactivación

- **Desactivado (por defecto):** `DMC_GS_ASYNC` sin definir o `0`. Se usa el backend síncrono y no se crea ningún hilo.
- **Activado:** `DMC_GS_ASYNC=1`. Variables opcionales:

| Variable | Efecto |
|---|---|
| `DMC_GS_ASYNC_PENDING_BUDGET=<bytes>` | presupuesto de subidas pendientes (por defecto 33554432; `18446744073709551615` = sin límite) |
| `DMC_GS_ASYNC_CAPACITY=<n>` | ranuras de la cola (por defecto 8192) |
| `DMC_GS_ASYNC_UPLOAD_RETAIN`, `DMC_GS_ASYNC_UPLOAD_BUDGET` | retención de búferes de subida (PERF_004E; por defecto 64 KB y 0) |
| `DMC_GS_ASYNC_AFFINITY=physical` | afinidad a núcleos físicos |
| `DMC_GS_ASYNC_FINISH_BARRIER=1` | `Sync(Finish)` como barrera |

La línea `[GS:async] installed` del log muestra los valores efectivos, incluido `pending budget`.

## 13. Riesgos pendientes

- **Carreras:** sin detector formal para MSVC; la cobertura es por tests de estrés.
- **Escenas no medidas:** más de 32 MiB pendientes frenarían al productor hasta el ritmo del consumidor, igual que
  la cola llena pero antes.
- **Robustez ante fallos de memoria (INFERENCIA):** si la copia de una subida lanzase `bad_alloc` después de
  reservar, la reserva no se devolvería. El juego no captura esa excepción, así que no es alcanzable en la ejecución
  actual. No se cambió.
- **Rendimiento:** el castillo queda un 2 % por debajo de PERF_004E con carga, sin explicar.
- **Fidelidad gráfica:** el moteado, las líneas de la oficina, GS-5 y el aspecto siguen abiertos.
- **Checkpoints:** guardar en el castillo falla en los dos modos por el local→host pendiente.
- **Transferencias local→host:** siguen sin entregarse al guest (PERF_004C), sin corregir.
- **Presentación:** 1–2 VSync de latencia de guest en asíncrono.
- **Regresiones:** si una fase posterior encuentra una diferencia entre los modos, la referencia es el modo síncrono.
