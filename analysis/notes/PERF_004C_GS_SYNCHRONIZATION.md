# PERF_004C: barreras de sincronización del GS en el juego real

**ELF:** `SLES_503.58` (PAL), SHA-256 `d0753a6b…bab4`, CRC32 `77654AD2`, entry `0x00100008`.

**Base:**

- `master` `529e9ed` (35 parches, vendor `0a696611`);
- rama local `perf-004b-async-gs-prototype`, sin commits.

**Fecha:** informe local con fecha de modificación 2026-11-17.

**Alcance:** instrumentación temporal fuera del vendor, retirada al terminar.

**Procedencia:** informe local `analysis/local/PERF_004C_GS_SYNC_RESULTS.md` y datos en `analysis/local/perf004c/`.
**No están versionados.**

**Etiquetas:** HECHO, INFERENCIA, HIPÓTESIS, UNKNOWN.

**Pregunta:** ¿con qué frecuencia obliga el juego a esperar al rasterizador? Es lo que decide si el GS asíncrono de
[PERF_004B_ASYNC_GS_PROTOTYPE.md](PERF_004B_ASYNC_GS_PROTOTYPE.md) puede solapar trabajo.

## 1. Diseño de la instrumentación

**Archivos temporales:**

- **`runtime/perf004b_gs_sync_stats.inc`.** Un `CountingBackend : GSRasterBackend` **síncrono**:
  - delega cada llamada en su propio `GSCpuBackend`, en el mismo hilo, orden y argumentos;
  - no tiene cola ni hilo;
  - mide cada llamada con `rdtsc` y anota bajo un mutex propio.
- **`runtime/dmc_overrides.cpp`:** dos líneas temporales, el `#include` y la instalación. Solo se activan con
  `DMC_GS_SYNC_STATS=1`, y se instalan antes de cargar el checkpoint.

**Compilación:** 2 tareas de Ninja (objeto de `dmc_overrides.cpp` y enlazado). Sin EE ni vendor.

**Registros:**

| Registro | Contenido |
|---|---|
| Por VSync | llamadas por hilo y operación, y trabajo GS |
| Por barrera o `Finish` | operaciones y trabajo desde la anterior |
| `Present` y local→host | un registro por evento |

**Tick de cada llamada:** `memory().gs().vsyncTick`, leído al entrar.

**Validez de la medida TSC (HECHO):**

- TSC invariante a 3193,9 MHz;
- el trabajo GS medido es el 54,3 % (oficina) y el 51,0 % (castillo) del tiempo por VSync, frente al 53,1 % y 49,6 %
  del perfil de [PERF_004A_RUNTIME_BASELINE.md](PERF_004A_RUNTIME_BASELINE.md).

**Coste (HECHO):**

- 0,12–0,18 % del VSync de media, con mediana de 0,08–0,10 %;
- picos aislados de 15–130 ms (tiempo de pared, posibles expropiaciones).

**Comportamiento no alterado (HECHO):**

- mismas llamadas y ciclos VU1 por VSync en todas las variantes;
- flujo GS idéntico en las 3 corridas instrumentadas de cada escenario;
- 0 fatales en 14 corridas.

**Restauración (HECHO):**

- Se retiró la instrumentación y se repusieron el objeto y el exe originales (`7571ae5b`).
- `ninja: no work to do`; git y vendor limpios.
- La recompilación restaurada no fue idéntica byte a byte, por las marcas de tiempo de MSVC. Por eso se repusieron los
  binarios originales.

## 2. Clasificación de operaciones GS

Se contaron todas y no se aplicó ninguna:

| Clase | Operaciones |
|---|---|
| Encolables | `Submit`, `BeginTransfer` (cualquier dirección), `UploadImage`, `ClearFramebuffer`, `WriteVram`, `Reset` |
| Barrera obligatoria | `ReadVram`, `ConsumeLocalToHostBytes`, `SnapshotVram`, `GetTransferSnapshot`, `Initialize`, `Sync(LocalToHost / DebugReadback / Reset)` |
| Aparte | `Sync(Finish)` |
| Marca de presentación (no hace esperar al productor) | `Present`, `Sync(Presentation)`, `Flush` |

## 3. Barreras obligatorias: resultado

**HECHO:** en las ventanas medidas no se observó **ninguna** barrera obligatoria:

- 0 `ConsumeLocalToHostBytes`, 0 `ReadVram`, 0 `SnapshotVram` y 0 `GetTransferSnapshot`;
- 0 `Sync(LocalToHost / DebugReadback / Reset)`.

**Volumen analizado:** **5340 VSync** acumulados entre las corridas de los dos escenarios:

- oficina: 3 corridas × 580 VSync = 1740;
- castillo: 3 corridas × 1200 VSync = 3600.

**Fuera de las ventanas:** `Initialize` en el tick 0 (instalación) y la carga del checkpoint.

**Límite:** esto **no demuestra** que no haya barreras en gameplay, menús, FMV, guardado de partida u otros
escenarios. Solo se midieron dos cinemáticas sin mando.

## 4. Resultados de la oficina (VSync 16420–17000, HECHO)

- **Hilos:** solo dos llaman al GS.
  - El hilo principal, de ventana, que presenta.
  - `gameThread`, desde el tick 16387, el de carga del checkpoint.
- **Por VSync:**
  - 13 367 `Submit` de media (mediana 6629, máximo 22 914);
  - 9,3 subidas host→local;
  - 2,0 transferencias local→local;
  - 4,8 `TextureFlush`;
  - 1 `Present`.
- **Trabajo GS por VSync:** media de 82 ms (p50 71, p90 109, p99 124, máximo 145).
- **Barreras obligatorias:** 0. `Finish`: 0. Local→host: 0.
- **Segmento sin barreras:** toda la ventana, con 7,75 M `Submit` y 47,5 s de trabajo GS por corrida.

## 5. Resultados del castillo (VSync 26800–28000, HECHO)

- **Por VSync:**
  - 17 710 `Submit` de media (mediana 13 805, máximo 41 210);
  - 11,8 subidas;
  - 2,0 transferencias local→local;
  - 1 `Present`.
- **Trabajo GS por VSync:** media de 99 ms (p50 95, p90 125, p99 167, máximo 195).
- **Barreras obligatorias:** 0.

## 6. Frecuencia de `Sync(Finish)` (castillo, HECHO)

- **Total:** 5304 `Finish` en la ventana, 4,42 por VSync, siempre desde el hilo del juego.
- **Concentración:** solo aparecen en un tercio de los VSync.
  - 805 VSync sin ninguno, 156 con 1–9 y 239 con 10 o más (máximo 27).
  - Forman 547 ráfagas, unas 0,46 por VSync.
- **Trabajo GS entre ráfagas:** mediana de 91–92 ms.
- **En la oficina:** 0.

**INFERENCIA.** `Finish` no necesita barrera:

- el frontend pone CSR.FINISH de inmediato;
- el guest solo podría observar la VRAM mediante local→host, que nunca se consume.

## 7. Presentaciones desde el hilo principal (HECHO)

- Todas las `Present` vienen del hilo principal, una por VSync, precedidas de `Flush` y `Sync(Presentation)`.
- Duran 2,3–2,5 ms de media: la copia de 4 MB bajo el mutex del backend más la conversión.
- El hilo del juego **nunca** llama a `Present` ni a `Sync(Presentation)`.

**INFERENCIA:** basta una marca de presentación en el flujo del productor. La presentación no tiene por qué hacer
esperar al hilo del juego.

## 8. Trabajo GS entre posibles barreras y cotas de solapamiento

- **Sin barreras obligatorias:** cada ventana es un solo segmento. Son 47,5 s de trabajo GS en la oficina y unos 119 s
  en el castillo.
- **Si `Finish` se tratara como barrera:** 152 de las 547 ráfagas tienen menos de 5 ms de GS detrás, pero suman poco
  tiempo.

**Cota de solapamiento** (ESTIMACIÓN; un techo, no una predicción): por segmento, `max(W, T − W)`, con una cola
ilimitada y sin coste de cola.

| Escenario | `Finish` sin barrera | `Finish` como barrera |
|---|---|---|
| Castillo | ×1,94–1,96 | ×1,85–1,86 |
| Oficina | ×1,84 (`1/0,543`) | — (no hay `Finish`) |

PERF_004D midió después el coste real de tratar `Finish` como barrera: −4,4 %. Es coherente con la diferencia entre las
dos cotas.

## 9. Checkpoints

**Carga (HECHO, código):**

- `deserializeMem` escribe `m_gsVRAM` en crudo y el envoltorio no lo ve.
- Después, `deserializeGs` hace `BeginTransfer(dir 3)`, que sí se ve.
- Las cargas fueron en los ticks 16387 y 26640, fuera de las ventanas.

**Guardado (HECHO, código):**

- Pasa por `checkpointIdle` → `GetTransferSnapshot`, que sería una barrera.
- `serializeMem` lee `m_gsVRAM` en crudo.
- En estas corridas no se guardó ningún checkpoint.

**Consecuencia de diseño:** con un GS asíncrono, guardar y cargar son barreras obligatorias, y la carga debe dejar la
cola vacía. Esto toca el vendor (`checkpoint_sections.cpp`). Se implementó en PERF_004D.

## 10. Hallazgo: transferencias local→host sin consumir

**HECHO.** En la ventana del castillo, DMC pide 26 transferencias local→host (`BeginTransfer` dir 1) de 65 536 píxeles
(256×256):

- llegan por parejas de VSync, por ejemplo 26804/26805;
- cada una cuesta 0,56 ms;
- **ningún** `ConsumeLocalToHostBytes` ni `GetTransferSnapshot` las sigue.

El runtime no entrega esos datos al guest por ningún camino.

**Consecuencia.** Tras esas transferencias, el búfer local→host queda con bytes pendientes y `checkpointIdle` rechaza
guardar:

- **INFERENCIA** en PERF_004C;
- **HECHO** confirmado en PERF_004D: guardar en el castillo en el tick 26900 falla en síncrono y en asíncrono con «GS
  local->host buffer not fully consumed».

**Estado:** es una **carencia funcional previa**, sin relación con el GS asíncrono. **No está corregida.** Puede ser
una divergencia con el PS2 real.

**Si se implementara**, cada lectura sería una barrera obligatoria, en torno a 0,02 por VSync en el castillo. Queda
como línea de investigación aparte.

## 11. Riesgos de integración identificados

1. `Sync(Finish)` sin barrera se apoya en el código. Si local→host llega a funcionar, la lectura sí será barrera.
2. Local→host sin consumir: la carencia de la sección 10.
3. Cola llena con trabajo irregular: hasta 41 210 `Submit` y 195 ms de GS en un VSync. Hace falta una cola de al menos
   8192 ranuras, o medida en bytes.
4. Colocación en núcleos y potencia del portátil. Una carga externa, como un render de Blender, cambió la velocidad
   hasta ×1,4.
5. Checkpoints: barreras explícitas con un cambio en el vendor.
6. Solo cinemáticas sin mando.

## 12. Decisión

**Integración experimental**, que se hizo en
[PERF_004D_ASYNC_GS_INTEGRATION.md](PERF_004D_ASYNC_GS_INTEGRATION.md), con:

- una cola de 8192 ranuras;
- MXCSR por operación;
- barreras de checkpoint;
- una marca de presentación;
- `Finish` sin barrera, con un interruptor de diagnóstico para forzarla.
