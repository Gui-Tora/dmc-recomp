# SCHED_DEBT — Deuda de reloj de pared de la cadena de VBlanks (parche 41, experimental)

- **Fecha:** 2026-11-27. Diagnóstico SCHED_PAL_001 (2026-11-26); elección del límite SCHED_PAL_002; candidato final
  SCHED_DEBT_FINAL (2026-11-27).
- **ELF:** `SLES_503.58` PAL, SHA-256 `d0753a6b…bab4`, CRC32 `77654AD2`, entry `0x00100008`.
- **Base:** `master` `daa49ddd` (PR #19), vendor `d21ad507` (40 parches), exe de referencia `e0dd5163`.
- **Resultado:** `patches/SCHED_DEBT_host_lag_bound.patch`, SHA-256
  `8fae74ea5f3860e8b011df069248e46f95578202bad5bcc5339be7dab9692edb`. `patched_commit` con 41 parches:
  `205151143b01769d58f3327f4dbbf73c6d182581`. Exe validado `32b6e9a2…`.
- **Alcance:** experimental y opt-in (`PS2X_SCHED_MAX_HOST_LAG_MS`). No toca el periodo de VBlank, GS, audio ni el
  formato de checkpoint. No regenera el EE.
- **Evidencia local** (no versionada):
  - `analysis/local/SCHED_PAL_001_DIAGNOSIS.md`, `SCHED_PAL_001_EXPERIMENT.md`, `SCHED_PAL_002_DESIGN.md`,
    `SCHED_PAL_002_VALIDATION.md`, `SCHED_DEBT_FINAL_REVIEW.md`;
  - `analysis/local/sched_pal_001/`, `sched_pal_002/`, `sched_debt_final/`: corridas, WAV, salidas de la suite,
    diffs y exes.

Clasificación: **HECHO**, **INFERENCIA**, **HIPÓTESIS**, **UNKNOWN**.

## 1. Problema

- **Síntoma:** al investigar los saltos audibles del audio PSS (parche 40) aparecieron dos causas independientes en
  el planificador del EE (`EeScheduler.cpp`):
  1. la cadencia de VBlank de 60 Hz aplicada a contenido PAL, que es la causa de los saltos continuos y queda **fuera
     de este parche**;
  2. la **deuda de pared sin límite**, objeto de este parche.
- **Mecanismo (HECHO, lectura de código):**
  - un VBlankStart se despacha cuando se cumplen su plazo en ciclos EE **y** su plazo de pared;
  - el siguiente plazo de pared se calcula como «plazo de pared anterior + periodo», nunca como «ahora + periodo»;
  - si el host se retrasa, la cadena acumula deuda sin límite;
  - en cuanto vuelve a ir rápido, los VBlanks atrasados salen seguidos.
- **Medidas a 60 Hz (HECHO):**
  - oficina cargada en un fundido: 14–20 fields/s mientras el host está saturado, después **178–184 fields/s durante
    unos 900 fields** y después 60;
  - castillo: la deuda llega a 22,3 s tras 250 fields;
  - cargar un checkpoint pone la deuda a cero (los plazos de pared se recalculan desde los ciclos restantes).

## 2. Hipótesis refutada

- **Hipótesis (SCHED_PAL_001, inicio):** bastaría con pasar el periodo de 16 667 µs a 20 000 µs (50 Hz) para que el
  audio PSS dejara de saltar.
- **Apoyo:** a 50 Hz la demo PSS estable no recortaba (0 de 3 456 000 frames).
- **Refutación:** con 50 Hz, el fundido de la oficina seguía produciendo ráfagas (133–162 fields/s), y una parada
  simulada de 2 s seguía recortando 28 800 frames. La deuda es independiente de la frecuencia.
- **Estado:** hacen falta dos correcciones separadas: el periodo PAL (fase posterior) y este límite.

## 3. Cambio

- **Regla:** al programar el siguiente VBlankStart, la base del plazo de pared pasa a ser
  `max(plazo_pared_anterior, ahora − L)`, con L = `PS2X_SCHED_MAX_HOST_LAG_MS`.
  - Sin la variable: ilimitado (histórico).
  - Valores fuera de 0–10 000 o con texto extra: se ignoran con un aviso.
- **Invariantes (HECHO por construcción y por test):**
  - el plazo en ciclos (`deadlineCycle + periodo`) no cambia;
  - el VBlankEnd no cambia;
  - sale exactamente un VBlankStart por cada uno procesado, en el mismo orden;
  - no se serializa nada nuevo.
- **Efecto:** tras una parada de S ms se recuperan como mucho L ms en ráfaga; los S − L restantes se descartan.

## 4. Elección de L (SCHED_PAL_002, combinado con 50 Hz forzados)

Paradas simuladas durante una película PSS estable:

| L | Recuperación tras 2 s | Frames recortados | Latencia del anillo, mediana / máximo |
|---|---|---|---|
| ilimitado | 139 VBlanks a 172 fields/s | 97 440 | 100 / 250 ms |
| **50 ms** | **2 VBlanks (unos 15 ms)** | **0** | 70 / 70 ms |
| 100 ms | 5 VBlanks (unos 37 ms) | 0 | 110 / 120 ms |
| 250 ms | 16 VBlanks | 19 200 | 70 / 210 ms |
| 500 ms | 36 VBlanks | 30 240 | 100 / 250 ms |

- Los WAV de las cinco corridas son idénticos.
- **Fundido a 60 Hz:**
  - L50 y L100 pasan directamente a 60 fields/s;
  - L250 deja una ventana de 84 fields/s;
  - L500, dos ventanas de 136 y 91 fields/s.
- **Propuesta:** L = 50. Es el menor valor medido sin ráfaga ni recortes, y no añade subdesbordamientos en
  reproducción estable. L = 100 es la alternativa si un host más lento tuviera más jitter.

## 5. Validación del parche final (exe `32b6e9a2`, sin instrumentación)

- **`ps2x_tests`:** 555 tests; 547 pasan sin variable y con L50, con los mismos 8 fallos que con 40 parches. Los 4
  tests `SchedHostDebt` pasan.
- **WAV idénticos a 40 parches:**
  - demo PSS completa (alias + ADMA), sin variable y con L50;
  - oficina (XAG);
  - fundido;
  - castillo (prefijo de la referencia);
  - checkpoint XAG v4.
- **Fundido de la oficina:** mismo tick final a los 26,1 s sin variable y a los 35,8 s con L50. La ráfaga desaparece.
- **Build incremental:** 1 objeto; 0 de `generated`. Los 10 130 archivos EE no cambian.
- **Reproducción:** los 41 parches, desde un clon limpio, dan `205151143b01…` (árbol `d47f5aef…`).

## 6. Correcciones durante las fases

- **Tests DEBT (SCHED_PAL_002):** exigían el periodo de 60 Hz y fallaban con 50 Hz forzados. Era una dependencia de
  los tests, no del código. La versión final solo exige un periodo constante.
- **Falso positivo:** una ejecución de la suite sin las variables del arena del runtime dio 24 fallos. Era la
  configuración de la ejecución.
- **Instrumentación temporal:** imprimía las marcas de tiempo con 6 cifras significativas (resolución de unos 100 ms).
  Las medidas de paradas se repitieron con coma fija. La instrumentación no forma parte del parche.

## 7. Limitaciones

- **No corrige los 60 Hz del contenido PAL:** con L50 la demo PSS sigue recortando un 16,7 % en reproducción
  estable. Los saltos continuos del audio PSS no se resuelven con este parche.
- L50 solo se ha medido en una máquina.
- Tras una parada, la latencia del anillo de audio puede quedar hasta L por encima de la habitual.
- Las paradas del host ya no se recuperan acelerando el tiempo perdido. Es lo buscado, pero el tiempo de pared del
  juego se retrasa respecto al reloj real.
