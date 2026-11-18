# PERF_004D_gs_async_backend.patch — parche 36: GS asíncrono opcional

**Estado:** opcional y **desactivado por defecto**. Con `DMC_GS_ASYNC` sin definir o `0`, se usa el backend
síncrono.

**Nombre del archivo:** «PERF_004D» identifica la fase en que empezó la implementación. Su revisión actual
corresponde a **PERF_004F**, que añade el presupuesto de bytes pendientes. Las revisiones anteriores no se
publicaron: PERF_004D (`2c81f09c…`) y PERF_004E (`b84293a2…`, retención de búferes de subida).

| Campo | Valor |
|---|---|
| Commit base | `14b1e5cb` (35 parches → `0a696611`) |
| `patched_commit` con 36 parches | `247af5902d0c4936aa6b2b8a0e79f7a3f24ddbde` |
| Árbol | `a442a7522efdeb43478a569865911ae2f9efe8e7` |
| SHA-256 del parche | `b54c06be5e3b2ab3606a335ef1795988c915516bd75a4436ec9775295dd2a641` |

## Causa

El rasterizador GS por CPU se ejecuta en el hilo del juego y ocupa el 50–53 % de su tiempo
([PERF_004A](../analysis/notes/PERF_004A_RUNTIME_BASELINE.md)). En las cinemáticas medidas no hay ninguna operación que
obligue al juego a esperar al GS ([PERF_004C](../analysis/notes/PERF_004C_GS_SYNCHRONIZATION.md)), así que el
rasterizado puede ir en otro hilo.

## Cambio

1. **`ps2xRuntime/include/runtime/gs/gs_async_backend.h`** (nuevo, solo cabecera). Contiene
   `gsasync::GSAsyncBackend`, un `GSRasterBackend` que encola las operaciones delante de un `GSCpuBackend` ejecutado
   en un hilo consumidor.
   - **Cola:** SPSC de 8192 ranuras, con copia profunda y MXCSR por operación.
   - **Barreras:** en las lecturas (`ReadVram`, `SnapshotVram`, `GetTransferSnapshot`, `ConsumeLocalToHostBytes`,
     `Initialize`, `Sync` salvo `Finish` y `Presentation`).
   - **Presentación:** marca pedida por el presentador mediante un buzón y buffer triple.
   - **Fallos:** *fail-stop* (el proceso termina con código 70) ante una excepción o un bloqueo del consumidor,
     también al cerrar.
   - **Retención de subidas (PERF_004E):** el consumidor libera el búfer de cada subida después de ejecutarla
     (`uploadRetainBudget = 0`).
   - **Presupuesto de bytes pendientes (PERF_004F):** `pendingUploadBudget`, por defecto 32 MiB.
     - El productor reserva la carga de cada subida antes de publicarla y espera si se superaría el presupuesto.
     - El consumidor la devuelve tras ejecutarla o descartarla, antes de avanzar la cola.
     - Una subida mayor que todo el presupuesto se admite sola, cuando no hay nada pendiente: es la única excepción
       al límite nominal.
   - **Opcional:** afinidad a núcleos físicos.
   - **Estadísticas:** línea `[GS:async]`.

   Ninguna cabecera del runtime la incluye, así que el código EE generado no se recompila.
2. **`ps2xRuntime/src/lib/checkpoint/checkpoint_sections.cpp`**. `serializeMem` y `deserializeMem` llaman a
   `GS::checkpointIdle()` antes de leer o escribir `m_gsVRAM` en crudo. Con el backend asíncrono, eso vacía la cola e
   invalida los cuadros publicados. En `GSCpuBackend` no hace nada.

**Lado DMC** (`runtime/dmc_overrides.cpp`, fuera del vendor): instala el backend solo si `DMC_GS_ASYNC=1`. Variables
opcionales:

- `DMC_GS_ASYNC_PENDING_BUDGET`
- `DMC_GS_ASYNC_CAPACITY`
- `DMC_GS_ASYNC_UPLOAD_RETAIN`
- `DMC_GS_ASYNC_UPLOAD_BUDGET`
- `DMC_GS_ASYNC_AFFINITY=physical`
- `DMC_GS_ASYNC_FINISH_BARRIER=1`

## Reproducción

`scripts/pipeline.py bootstrap` sobre un vendor limpio aplica los 36 parches y obtiene `247af590`. Se comprobó con un
clon nuevo del baseline aplicando `apply_patchset` del pipeline: el árbol resultante es idéntico al vendor.

## Validación

Detalle completo en [PERF_004F_GS_QUEUE_BYTE_BUDGET.md](../analysis/notes/PERF_004F_GS_QUEUE_BYTE_BUDGET.md) y
[PERF_004E_ASYNC_GS_HARDENING.md](../analysis/notes/PERF_004E_ASYNC_GS_HARDENING.md).

- **Tests:** 85/85 (concurrencia, integración, memoria de subidas y presupuesto).
- **GS Replay `cap2`:** VRAM y hashes idénticos al síncrono, también con ZTE forzado, con capacidad 1 y con
  presupuestos de 64 KiB a sin límite.
- **Checkpoints:** guardados síncrono y asíncrono idénticos en las 12 secciones.
- **Cierre:** con `WM_CLOSE`, ordenado y con código 0.
- **Rendimiento (PERF_004E, carga comparable):** ×1,64 en la oficina y ×1,77 en el castillo frente al síncrono.
- **Memoria (PERF_004F):** en la pantalla de Opciones, los bytes pendientes quedan acotados a 32 MiB (78,8 MB sin
  límite).
- **Pendiente de validar:** combate, menú de pausa y gameplay con mando en asíncrono.
- **No se hizo:** comparación de imágenes presentadas en PERF_004F.
