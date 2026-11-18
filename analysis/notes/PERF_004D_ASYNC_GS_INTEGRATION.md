# PERF_004D: integración experimental del GS asíncrono en el juego

**ELF:** `SLES_503.58` (PAL), SHA-256 `d0753a6b…bab4`, CRC32 `77654AD2`, entry `0x00100008`.

**Base:**

- `master` `529e9ed` (35 parches, vendor `0a696611`);
- rama local `perf-004d-async-gs-integration`, sin commits.

**Fecha:** 2026-11-17.

**Estado:** **experimental, no integrado ni publicado.** El backend asíncrono se activa solo con `DMC_GS_ASYNC=1`.
Sin esa variable, el runtime usa el GS síncrono de siempre.

**Procedencia:** informe local `analysis/local/PERF_004D_ASYNC_GS_INTEGRATION.md` y datos, ejecutables y candidato en
`analysis/local/perf004d/`. **No están versionados.**

**Etiquetas:** HECHO, INFERENCIA, HIPÓTESIS, UNKNOWN.

**Revisión posterior:** el candidato descrito aquí se revisó en
[PERF_004E_ASYNC_GS_HARDENING.md](PERF_004E_ASYNC_GS_HARDENING.md), que cambió la política de memoria.

## 1. Integración experimental

```text
gameThread (productor)                          hilo «GS async consumer»
 GS frontend ─ Submit/Transfer/Upload/Clear ─▶ cola SPSC de 8192 ranuras ─▶ GSCpuBackend (sin cambios) ─▶ VRAM
   copia profunda + MXCSR por operación
   barrera (vaciar): ReadVram, SnapshotVram, GetTransferSnapshot, Consume, Initialize, Sync ≠ Finish/Presentation
   siguiente llamada GS: si el buzón tiene petición ─▶ MARCA ─▶ el consumidor copia la VRAM al buffer triple
hilo principal (presentador): Present(req) ─▶ deja req en el buzón (sin esperar) ─▶ convierte la última copia completa
```

- **Productor:** el diseño de [PERF_004B_ASYNC_GS_PROTOTYPE.md](PERF_004B_ASYNC_GS_PROTOTYPE.md), con:
  - una cola de 8192 ranuras;
  - un mutex de productor, por seguridad si aparece otro hilo llamante, que se cuenta;
  - estadísticas en una línea `[GS:async]`.
- **No se tocaron** `gs_frontend.h`, `gs_backend.h` ni `gs_types.h`, que incluye el código EE generado. Tampoco se
  regeneró EE ni se tocó VU1.

## 2. Parche candidato 36 y activación

**Parche del vendor**, `PERF_004D_gs_async_backend.patch`, que toca dos archivos:

- `ps2xRuntime/include/runtime/gs/gs_async_backend.h`: nuevo, solo cabecera. Ninguna cabecera del runtime lo incluye,
  así que el código EE no se recompila.
- `ps2xRuntime/src/lib/checkpoint/checkpoint_sections.cpp`: barreras de checkpoint (sección 6).

**Lado DMC**, fuera del vendor: `runtime/dmc_overrides.cpp` instala el backend solo con `DMC_GS_ASYNC=1`. Variables
opcionales:

- `DMC_GS_ASYNC_AFFINITY=physical`;
- `DMC_GS_ASYNC_FINISH_BARRIER=1`;
- `DMC_GS_ASYNC_CAPACITY`.

**Compilación incremental:** el plan de Ninja fue de 4 tareas (`checkpoint_sections.cpp.obj`, `ps2_runtime.lib`,
`dmc_overrides.cpp.obj` y el enlazado), de 10 a 25 s.

**Identidad en PERF_004D** (HECHO), sustituida después por la revisión de PERF_004E:

- parche: SHA-256 `2c81f09c…`;
- 35 parches + candidato: commit `531b04a7…`, árbol `8d956f51`, reproducido desde el commit base `14b1e5cb`.

**No se aplicó** a `patches/` ni a `upstream.lock.json`. El lock propuesto quedó solo en local.

**Diagnóstico local, fuera del candidato:** `DMC_FRAME_DUMP` registra la imagen que presenta el hilo principal. Solo
existía en el exe experimental (`7ab8b44e…`).

## 3. Arquitectura de presentación final

**Marca pedida por el presentador:**

1. El hilo principal deja una petición en un buzón, sin esperar.
2. En la siguiente operación GS del productor, se encola una **marca**.
3. Al llegar a la marca, el consumidor copia la VRAM y los registros de pantalla de ese mismo punto del flujo a un
   buffer triple.
4. El presentador convierte la última copia completa.

**INFERENCIA:** VRAM y registros son coherentes entre sí y corresponden al mismo punto que vería el presentador
síncrono, con un VSync de retraso (sección 14).

## 4. Estrategias de presentación descartadas

Las dos son hipótesis de diseño refutadas durante la fase, y se conservan como tales:

**1. Marca en la primera operación GS de cada VSync, con los registros de pantalla de ese instante.**

- **Evidencia en contra (HECHO):** 43 imágenes de 640×512 en el castillo, con `DISPFB` 100 u 84. 4 de ellas superan
  el umbral de la métrica de cuñas. El síncrono presenta siempre 512×448 con FBP 504.
- **Causa (INFERENCIA):** en ese instante el juego tiene valores transitorios de `DISPFB` y `DISPLAY`, en los mismos
  VSync que las ráfagas de `Finish` y local→host.

**2. La misma marca, pero con los registros del presentador.**

- Arregla el castillo.
- **Evidencia en contra (HECHO):** 6 de 200 imágenes de la oficina muestran la franja superior (filas 0–63) aún sin
  dibujar.
- **Causa:** se combina la VRAM del límite de VSync con registros posteriores.

## 5. Solución adoptada

La marca pedida por el presentador (sección 3). Resultado funcional en la sección 11.

## 6. Barreras para checkpoints

**Cambio:** `serializeMem` y `deserializeMem` llaman a `GS::checkpointIdle()` antes de leer o escribir `m_gsVRAM` en
crudo.

- **Modo asíncrono:** vacía la cola e invalida los cuadros publicados y no presentados.
- **`GSCpuBackend`:** no tiene ningún efecto.

**INFERENCIA:** tras una carga no quedan comandos antiguos, porque la carga ocurre en el hilo productor después de
vaciar la cola.

## 7. Manejo de errores y cierre seguro

Política ***fail-stop***. Cualquiera de estos casos registra el error y termina el proceso con código 70:

- una excepción del consumidor;
- una espera más larga que el plazo (10 s);
- un consumidor que no termina al cerrar.

**Nunca se desacopla** un hilo que pueda seguir escribiendo VRAM. Esto cambia el diseño de PERF_004B, que
desacoplaba al consumidor si no terminaba.

`m_memory` se destruye después de `m_gs`.

**Validación (HECHO):**

- *fail-stop* en 3 procesos hijo: salen con código 70 en 0,04–0,38 s;
- cierre con `WM_CLOSE` durante la ejecución: código 0 en 0,38 s, y el consumidor se une sin quedar colgado.

## 8. Afinidad de CPU (opcional)

- **Topología real:** `GetLogicalProcessorInformationEx` da 8 núcleos con pares lógicos (0,1), (2,3)…
- **Con afinidad:** el juego va al núcleo físico 1 y el GS al 2.

**HECHO.** Colocación efectiva:

- con afinidad, 0 de ~20 700 muestras en el mismo núcleo;
- sin afinidad, el 7,4–8,3 % en el castillo y el 3–4 % en la oficina.

**HECHO.** Efecto sobre la velocidad:

- castillo: −1,3 %, dentro del ruido;
- oficina limpia: +0,4 % y +2,6 %.

**Decisión:** no hay ganancia clara, así que la afinidad queda desactivada por defecto.

## 9. Comparación con `Finish` como barrera (castillo)

| | Sin barrera | `Finish` como barrera |
|---|---|---|
| VSync/s | 7,40 (con afinidad) | 7,17: −3,1 %, y −4,4 % frente al asíncrono sin afinidad |
| Barreras por VSync | 0 | 4,4 |
| Espera en barreras | 0 | 10,3–10,6 ms/VSync |
| Imágenes no vistas en síncrono | 3, en la transición | 1, en la transición |

**HECHO:**

- CSR.FINISH, SIGNAL y LABEL los pone el frontend en el mismo orden con los dos backends;
- los guardados síncrono y asíncrono son idénticos.

**Decisión:** `Finish` sin barrera por defecto. La barrera queda como interruptor de diagnóstico.

## 10. GS Replay y tests (HECHO)

**Banco:** incluye la cabecera del árbol experimental.

| Prueba | Resultado |
|---|---|
| 23 tests de PERF_004B | 23/23 |
| 11 tests nuevos | 11/11 |
| `cap2` síncrono frente a la referencia de 35 parches de VISUAL_010 | `hashlog` y VRAM final idénticos |
| Asíncrono con barrera en cada hash | 1681 hashes y 2 presentaciones idénticos |
| Asíncrono sin barreras (3 repeticiones y capacidad 1) | presentaciones y VRAM final idénticas |
| ZTE forzado | idéntico |
| Sin propagar MXCSR | diverge, como se espera |

Los 11 tests nuevos cubren:

- marcas con presentador concurrente;
- marcas pedidas por el presentador: 891 imágenes, todas prefijos exactos del flujo y en orden;
- invalidación en checkpoint;
- segundo productor;
- afinidad;
- liberación de subidas grandes;
- `Finish`;
- *fail-stop*.

## 11. Fidelidad visual y checkpoints

**Comparación de imágenes presentadas (HECHO, con `DMC_FRAME_DUMP`):** se comparó el mismo exe experimental con y sin
`DMC_GS_ASYNC`, y se usaron varias corridas síncronas para medir la variación propia.

| | Oficina 16400–16600 | Castillo 26700–27200 |
|---|---|---|
| Imágenes asíncronas distintas | 200 | 445 |
| … que ninguna corrida síncrona mostró | **0** | **3**, todas en la transición 26745–26770 |
| Métrica de cuñas GS-4 | 0 (síncrono 0) | máximo 1876 (síncrono 1870) |
| Puntos claros aislados | mediana 3131 (síncrono 3122) | máximo 7694 (síncrono 7694) |
| Tamaño | 512×448 | 512×448 |

- **HECHO.** En la transición 26745–26770, el propio juego alterna cuadros negros con geometría a medio dibujar en el
  búfer visible. Cada corrida, síncrona o asíncrona, captura estados parciales distintos.
- **HECHO.** `PrintWindow` devolvió imágenes blancas en todas las corridas, también en las síncronas. Por eso se
  añadió `DMC_FRAME_DUMP`.

**Checkpoints (HECHO):**

| Prueba | Resultado |
|---|---|
| Cargar los checkpoints de oficina y castillo en asíncrono | siempre `SUCCESS` |
| Guardar en la oficina en el tick 16450, síncrono frente a asíncrono | **12 de 12 secciones idénticas** |
| Restaurar ese guardado 3 veces y guardar en el 16480 | 3 guardados idénticos |
| Guardar en el castillo en el tick 26900 | **falla en los dos modos** por el local→host sin consumir (ver [PERF_004C_GS_SYNCHRONIZATION.md](PERF_004C_GS_SYNCHRONIZATION.md) §10). Limitación previa, no corregida |

## 12. Aceleración real en oficina y castillo (HECHO)

**Método:** el de PERF_004A, con ventanas `steady_clock`, orden alternado y espera previa si la carga externa supera
el 300 %.

**Carga externa:** un render de Blender del usuario, de unos 2 núcleos, estuvo activo en **las 13 corridas del
castillo** y en las corridas 5–10 de la oficina. El rendimiento del procesador bajó del 120–130 % al 105–111 %.

**Castillo** (todas las corridas con Blender, comparables entre sí):

| Configuración | n | VSync/s | Frente a referencia |
|---|---|---|---|
| Referencia `7571ae5b` | 3 | 4,24 (4,22–4,25) | — |
| Asíncrono sin afinidad | 3 | **7,49** (7,46–7,54) | **×1,77** |
| Asíncrono con afinidad física | 3 | 7,40 | ×1,745 |
| Asíncrono con `Finish` como barrera | 3 | 7,17 | ×1,69 |
| Experimental sin activar | 1 | 4,78 | no concluyente (corrida única con más reloj) |

**Oficina:**

| Configuración | Corridas limpias (r01–r04) | Corridas con Blender (r05–r10) |
|---|---|---|
| Referencia | 5,81 | 5,85; 5,82 |
| Asíncrono | **11,13** (×1,92) | 9,80; 8,74 |
| Asíncrono con afinidad | **11,17; 11,42** (×1,92–1,96) | 9,25 |
| Experimental sin activar | — | 5,51 (corrida única) |

**HECHO.** Con carga externa, la referencia apenas pierde velocidad y el asíncrono baja un 15–22 %, porque usa dos
núcleos.

**Las cifras con y sin Blender no se mezclan.** El modo desactivado no se pudo validar aquí: solo hubo dos corridas
únicas, en condiciones distintas. PERF_004E hizo una serie dedicada.

## 13. CPU, memoria y cola (HECHO, medias de las corridas asíncronas)

| | Oficina | Castillo |
|---|---|---|
| CPU del proceso | 189–191 % (referencia 109 %) | 193 % (referencia 109 %) |
| Hilo del juego / hilo GS | 80 % / 98 % | 86 % / 96 % |
| RSS | 217–219 MB (referencia 157) | 288–290 MB (referencia 178) |
| Ejecución del consumidor | 85–112 ms/VSync | 127–130 ms/VSync |
| Encolado | 0,43–0,56 ms/VSync | 0,72–0,76 ms/VSync |
| Ocupación media de la cola | ~4650 de 8192 | ~3950 de 8192 |
| Esperas por cola llena | 22–28 ms/VSync | 19–21 ms/VSync |
| Barreras | 0 | 0 |
| Llamadas desde otros hilos | 0 | 0 |

**INFERENCIA.** El hilo GS está al 96–98 % y la cola se llena. El límite pasa a ser el propio rasterizador. Para ir
más allá de ~×1,8–1,9 hay que acelerar el GS.

**Memoria:** +61 MB de RSS en la oficina y +110 MB en el castillo. La causa son los búferes de subida retenidos por
ranura, y se corrigió en PERF_004E.

## 14. Latencia de presentación (HECHO, contadores del backend)

| | Castillo | Oficina |
|---|---|---|
| Cuadros completos publicados | 7,45/s | 11,1/s |
| Consumidos | 98,5 % | 84 % |
| Sustituidos sin mostrar | 1,5 % | 15 % |
| Obsoletos | 0 | 0 |
| Retraso frente al VSync del guest | 1 VSync en el 99,6 %, 2 en el resto | 1 VSync en el 70 %, 2 en el 30 % |

- Son contadores del backend, **no presentaciones del sistema operativo**: no había PresentMon.
- El hilo del juego no espera nunca por presentar.

## 15. Limitaciones y riesgos

1. **Memoria:** +61 a +110 MB de RSS. Resuelto en PERF_004E en lo que se refiere a la memoria retenida.
2. **Carga externa:** el asíncrono pierde un 15–22 % con dos núcleos ocupados por otro proceso.
3. **Escenarios validados:** solo dos cinemáticas sin mando (oficina y castillo).
   - **No están validados** el gameplay con mando, las FMV/IPU ni los menús.
   - Tampoco otros modos de juego ni otras escenas.
4. **VSync sin operaciones GS:** la petición del presentador espera a la siguiente operación. Se sigue mostrando el
   último cuadro.
5. **Local→host sin consumir:** impide guardar en el castillo en los dos modos.
6. **Modo desactivado:** sin serie de control dedicada en esta fase.
7. **Carreras:** sin detector formal de carreras para MSVC.
8. **Presentaciones reales del sistema operativo:** UNKNOWN.

**Decisión de la fase:** continuar, con `DMC_GS_ASYNC` desactivado por defecto, y endurecer la memoria. Eso se hizo en
[PERF_004E_ASYNC_GS_HARDENING.md](PERF_004E_ASYNC_GS_HARDENING.md).
