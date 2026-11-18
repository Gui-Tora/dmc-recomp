# PERF_004: progreso de rendimiento y fidelidad del GS (punto de entrada)

Esta nota resume las investigaciones VISUAL_007–010 y PERF_004A–E y enlaza a sus notas. Los informes originales
están en `analysis/local/` (no versionado, no incluido en el repositorio). Las notas versionadas enlazadas conservan
sus conclusiones.

**Etiquetas:**

- **HECHO:** medido u observado.
- **INFERENCIA.**
- **HIPÓTESIS.**
- **ESTIMACIÓN:** cálculo, no medida.
- **UNKNOWN.**

## 1. Estado del proyecto (2026-11-18)

| Elemento | Estado |
|---|---|
| Juego | Devil May Cry PAL, `SLES_503.58`, SHA-256 `d0753a6b…bab4`, CRC32 `77654AD2`, entry `0x00100008` |
| `master` | `529e9ed572241b15793ac47dd850a5b5c259b924` (PR #13) |
| Vendor | `0a696611250982a739a385072357ba36ffee5dfc`, limpio |
| Parches integrados | **35** (`patches/`, `upstream.lock.json`; ver [../../patches/README.md](../../patches/README.md)) |
| Parche 36 (GS asíncrono) | **candidato experimental, no integrado ni publicado.** Desactivado por defecto: solo actúa con `DMC_GS_ASYNC=1` |
| Ejecutable de referencia | `7571ae5b…`, master con 35 parches, build local |
| Checkpoints de medida | copias locales de `g002_O_office.dmck` (`5f9576eb…`, oficina) y `g002_castle_entry.dmck` (`4c841943…`, castillo), los de [GAMEPLAY_002_FIRST_PLAYABLE_DIAGNOSIS.md](GAMEPLAY_002_FIRST_PLAYABLE_DIAGNOSIS.md) |
| Ventanas de medida | oficina, VSync (16420, 17000]; castillo, (26800, 28000] |

**Notas versionadas de esta línea:**

| Fase | Nota |
|---|---|
| VISUAL_007–009 | [VISUAL_007_009_GS_INVESTIGATIONS.md](VISUAL_007_009_GS_INVESTIGATIONS.md) |
| VISUAL_010 | [VISUAL_010_GS_TRIANGLE_COVERAGE.md](VISUAL_010_GS_TRIANGLE_COVERAGE.md) |
| PERF_004A | [PERF_004A_RUNTIME_BASELINE.md](PERF_004A_RUNTIME_BASELINE.md) |
| PERF_004B | [PERF_004B_ASYNC_GS_PROTOTYPE.md](PERF_004B_ASYNC_GS_PROTOTYPE.md) |
| PERF_004C | [PERF_004C_GS_SYNCHRONIZATION.md](PERF_004C_GS_SYNCHRONIZATION.md) |
| PERF_004D | [PERF_004D_ASYNC_GS_INTEGRATION.md](PERF_004D_ASYNC_GS_INTEGRATION.md) |
| PERF_004E | [PERF_004E_ASYNC_GS_HARDENING.md](PERF_004E_ASYNC_GS_HARDENING.md) |
| Antecedentes | [PERF_001_BASELINE.md](PERF_001_BASELINE.md), [PERF_002_CASTLE_BASELINE.md](PERF_002_CASTLE_BASELINE.md), [PERF_003_ARCHITECTURE_FEASIBILITY.md](PERF_003_ARCHITECTURE_FEASIBILITY.md), [PERF_003_GS_OPEN_ISSUES.md](PERF_003_GS_OPEN_ISSUES.md) |

## 2. Cronología

| Fase | Problema | Hipótesis | Resultado | Decisión | Estado | Nota |
|---|---|---|---|---|---|---|
| VISUAL_007 | Defectos visuales tras el parche 34 | Inventario con PCSX2 | GS-4 corregido; quedan el moteado, las líneas de la oficina, el aspecto y GS-5 | Investigar primero el moteado (reproducible offline) | completada | [VISUAL_007_009](VISUAL_007_009_GS_INVESTIGATIONS.md) |
| VISUAL_008 | 11 puntos claros del cielo en `cap2` | Rasterizador o textura | Doble escritura en aristas compartidas, por la regla inclusiva con margen y la mezcla aditiva | Experimento offline | completada | ídem |
| VISUAL_009 | Confirmar la causa | Una regla semiabierta los elimina | 11/11 desaparecen; 433 bytes de VRAM, todos explicados; 0 grietas | Proponer el parche 35 | completada | ídem |
| VISUAL_010 | Regla de cobertura | — | Parche 35 con tests y nuevas referencias de Replay | Integrado (PR #13) | **integrada** | [VISUAL_010](VISUAL_010_GS_TRIANGLE_COVERAGE.md) |
| PERF_004A | Velocidad con 35 parches | GS y VU1 dominan | 6,2 / 4,9 VSync/s; GS 50–53 %, VU1 37–41 % del hilo | Prototipar el GS asíncrono | completada | [PERF_004A](PERF_004A_RUNTIME_BASELINE.md) |
| PERF_004B | ¿El GS puede ir en otro hilo? | Cola SPSC fiel | Byte a byte idéntico; 23/23 tests; ×1,57 en el banco | Contar las barreras en el juego | completada (solo banco) | [PERF_004B](PERF_004B_ASYNC_GS_PROTOTYPE.md) |
| PERF_004C | Barreras en el juego | Pocas o ninguna | 0 obligatorias en 5340 VSync de dos cinemáticas; `Finish` sin barrera; local→host sin consumir | Integración experimental | completada | [PERF_004C](PERF_004C_GS_SYNCHRONIZATION.md) |
| PERF_004D | Integrar y medir | Marca de presentación y barreras de checkpoint | ×1,77 castillo, ×1,92–1,96 oficina limpia; guardados idénticos; 2 diseños de presentación refutados | Continuar; reducir memoria | completada (experimental) | [PERF_004D](PERF_004D_ASYNC_GS_INTEGRATION.md) |
| PERF_004E | Memoria retenida y validación final | Liberar tras usar sin perder velocidad | RSS del castillo 288 → 196 MB; ×1,64 / ×1,77; menús, FMV y movimiento iguales en los dos modos | Candidato 36 preparado; queda el riesgo de memoria en vuelo | completada (experimental) | [PERF_004E](PERF_004E_ASYNC_GS_HARDENING.md) |
| PERF_004F | Memoria en vuelo con la cola llena | Limitar la cola también por bytes | — | — | **pendiente** (propuesta) | — |

## 3. Resultados principales

### Línea base original

**HECHO (PERF_004A):** cinemáticas sin mando con 35 parches.

- **Oficina:** 6,16 VSync/s (162 ms por VSync).
- **Castillo:** 4,86 VSync/s (206 ms por VSync).
- **Hilo limitante:** el del juego, al 99–100 % de un núcleo y sin esperas.
- **Objetivo:** 50 VSync/s (PAL), que queda a ×8–10.

### Cuello de botella

**HECHO:** el rasterizador GS es el 50–53 % del hilo del juego, y el VU1 el 37–41 %.

**ESTIMACIÓN:** quitar solo el GS da una cota de ×2; quitar GS y VU1 llevaría a ~50–64 VSync/s.

### GS asíncrono offline

**HECHO (PERF_004B):**

- fiel byte a byte en GS Replay `cap2`;
- 23/23 tests de concurrencia;
- la cola cuesta 48–92 ns por operación;
- ×1,57 en el banco con carga sintética.

No es una medida del juego.

### Barreras

**HECHO (PERF_004C):** 0 barreras obligatorias en 5340 VSync de dos cinemáticas. Esto no demuestra su ausencia en
gameplay ni en otros escenarios.

### Integración experimental

**HECHO (PERF_004D):**

- presentación con una marca pedida por el presentador;
- barreras de checkpoint;
- *fail-stop*;
- dos diseños de presentación refutados.

### Mejoras medidas (HECHO; la carga externa se indica en cada caso)

| Fase | Escenario | Referencia | Asíncrono | Mejora | Condición |
|---|---|---|---|---|---|
| PERF_004D | Castillo | 4,24 | 7,49 | ×1,77 | Blender activo en todas las corridas |
| PERF_004D | Oficina | 5,81 | 11,13–11,42 | ×1,92–1,96 | corridas limpias |
| PERF_004E | Oficina | 6,61 | 10,86 | ×1,64 | carga baja |
| PERF_004E | Castillo | 4,02 | 7,13 | ×1,77 | Blender ≈175 % en ambos |

**INFERENCIA:** tras desacoplarlo, el rasterizador GS es el límite: su hilo está al 96–98 % y la cola se llena.

### Reducción de memoria

**HECHO (PERF_004E):** RSS media del castillo en asíncrono, de 288 a 196 MB, sin cambio de velocidad. Exceso frente al
síncrono: +19 MB.

### Fidelidad

**HECHO:**

- Replay byte a byte en todos los modos;
- guardados síncrono y asíncrono idénticos en las 12 secciones;
- imágenes presentadas iguales salvo en una transición donde también varían las corridas síncronas;
- menús, FMV y movimiento de Dante iguales en los dos modos.

**Visual (VISUAL_007–010):**

- GS-4 corregido por el parche 34;
- los 11 puntos del cielo, por el parche 35;
- siguen abiertos: el moteado oscuro, las líneas de la oficina, GS-5 y el aspecto 1:1.

## 4. Riesgos pendientes

**GS asíncrono:**

- **Memoria en vuelo con la cola llena:** 82 MB en Opciones. Es el motivo de PERF_004F.
- **Carreras:** sin detector formal para MSVC.
- **Carga externa:** con dos núcleos ocupados, el asíncrono pierde un 15–22 %.

**Rendimiento:**

- El rasterizador es ahora el límite.
- El VU1 sigue siendo el 37–41 % del hilo síncrono.
- Hay ruido de carga externa en todas las series (±7 % o más).

**Gameplay y mando:**

- Validados con el pad por archivo: el movimiento de Dante y la navegación de menús.
- Sin confirmar: el ataque y el menú de pausa.
- Ninguna medida de rendimiento en gameplay con mando.

**Fidelidad gráfica:**

- moteado oscuro y líneas diagonales de la oficina sin diagnosticar;
- GS-5 (mar);
- posición de muestreo y medio píxel de `XYOFFSET.OFY`;
- aspecto 1:1.

**Transferencias local→host:**

- 26 transferencias por ventana del castillo que el runtime nunca entrega al guest (PERF_004C).
- **Sin corregir.**

**Checkpoints:**

- Guardar en el castillo falla en los dos modos por el local→host pendiente.
- En asíncrono, guardar y cargar vacían la cola.

**Presentación:**

- **Latencia:** 1–2 VSync de guest en asíncrono, frente a 0 en síncrono.
- **Cambios de resolución:** no observados.
- **Presentaciones reales del sistema operativo:** UNKNOWN (sin PresentMon).

## 5. Próximas fases

| Fase | Contenido | Estado |
|---|---|---|
| PERF_004F | Presupuesto de bytes de la cola del GS asíncrono, para limitar la memoria en vuelo | **pendiente** (propuesta, no iniciada) |
| Publicación del parche 36 | Revisar el candidato de PERF_004E, aplicarlo a `patches/` y al lock, *bootstrap* limpio y PR. Desactivado por defecto | **pendiente de autorización** |
| PERF_005 | Optimizar el rasterizador GS: especialización por estado de lote, con la ruta genérica como oráculo | **propuesta** |
| VU1 | Una función por entrada y compilar las entradas interpretadas (plan A2 de PERF_003) | **propuesta** |
| Gameplay y fluidez | Validar el ataque, la pausa y escenas nuevas, y medir con mando | **pendiente** |
