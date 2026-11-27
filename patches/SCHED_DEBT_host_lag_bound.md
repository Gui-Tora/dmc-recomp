# SCHED_DEBT_host_lag_bound.patch — parche 41 (experimental, opt-in)

**Estado:** entrada 41 de `upstream.lock.json`. Nota: `analysis/notes/SCHED_DEBT_HOST_LAG.md`.

| Campo | Valor |
|---|---|
| Commit base | `d21ad5075aea44d1a15704a88388ab691993c32f` (40 parches) |
| `patched_commit` con 41 parches | `205151143b01769d58f3327f4dbbf73c6d182581` (árbol `d47f5aefd15ecc415f2a47fb883cb2586124c993`) |
| SHA-256 | `8fae74ea5f3860e8b011df069248e46f95578202bad5bcc5339be7dab9692edb` |
| Archivos | 5 (+270/−1) |
| Exe validado | `32b6e9a23a466de6f5ce46d1576c20862591e08c50a32edc936d2a4ba50449b6` (build incremental desde `d21ad507`) |

## Problema

- **Cómo se encadenan los plazos:** el planificador del EE programa cada VBlankStart con dos plazos, uno en ciclos
  EE y otro de reloj de pared, y el VBlank sale cuando se cumplen los dos. El plazo de pared se encadena como
  «plazo anterior + periodo».
- **Efecto de una fase lenta:** si el host va más lento que el periodo (escenas pesadas, una parada), la cadena se
  queda atrás sin límite. Cuando el host vuelve a ir rápido, los VBlanks atrasados salen seguidos hasta saldar la
  deuda.
- **En Devil May Cry (SLES_503.58):** se midieron ráfagas de unos 180 fields/s, unas tres veces el tiempo real,
  durante unos 900 fields tras cargar la oficina en un fundido. Tras una parada de 2 s, el audio PSS recortaba
  28 800 frames.

## Cambio

Todo es opcional. Sin la variable, el runtime se comporta como con 40 parches.

1. **`ps2xRuntime/src/lib/Kernel/sched_host_debt.h`** (nuevo, privado del runtime; no lo incluye el EE generado):
   - `maxHostLag()`: el límite configurado; vacío significa ilimitado;
   - `boundHostDebt(anterior, ahora)` = `max(anterior, ahora − L)`;
   - un gancho de test.
2. **`ps2xRuntime/src/lib/Kernel/EeScheduler.cpp`** (+56/−1):
   - lectura de `PS2X_SCHED_MAX_HOST_LAG_MS` (0–10 000 ms). Si el valor está fuera de rango o lleva texto extra, se
     ignora con el aviso `[SCHED:debt] … ignored`; con un valor válido imprime `[SCHED:debt] max host lag N ms`;
   - al programar el siguiente VBlankStart, solo el **plazo de pared** se calcula sobre
     `boundHostDebt(plazo_pared_anterior, ahora)`;
   - el plazo en ciclos (`deadlineCycle + periodo`) y el VBlankEnd no cambian.
3. **`ps2xTest`:** `sched_host_debt_tests.cpp` (nuevo, 4 tests), más su alta en `CMakeLists.txt` y `main.cpp`.

## Semántica

| Configuración | Comportamiento |
|---|---|
| sin variable | deuda ilimitada: idéntico a 40 parches |
| `PS2X_SCHED_MAX_HOST_LAG_MS=50` | **recomendada para las pruebas actuales de DMC**: tras un retraso se recuperan como mucho 50 ms; el resto se descarta |
| `=100` | alternativa documentada (más margen para jitter); no es la predeterminada |
| `=0` | sin recuperación: cada VBlank tardío reprograma el siguiente desde «ahora» |

- **Lo que no cambia:**
  - no se omite ningún VBlank ni cambia su orden;
  - no cambian los plazos en ciclos ni el tiempo emulado;
  - no cambia el formato de checkpoint: no se serializa nada nuevo, y la carga ya recalcula los plazos de pared
    desde los ciclos restantes.
- **Lo que sí cambia:** una parada del host ya no se recupera acelerando todo el tiempo perdido; ese tiempo de pared
  se pierde.

## Tests

`ps2x_tests`: **555** tests (551 con 40 parches + 4 `SchedHostDebt`). **547 pasan**, sin variable y con L50. Los 8
fallos son los mismos que con 40 parches.

Tests nuevos:
1. ilimitado: ráfaga tras una parada de 250 ms;
2. L = 0: sin ráfaga;
3. L = 50: el plazo pendiente nunca queda más de L (más margen) en el pasado, y la ráfaga queda acotada;
4. sin parada: los mismos ticks y plazos en ciclos con y sin límite.

En todos, los ticks son consecutivos, siempre hay un único VBlankStart pendiente y los plazos en ciclos están
separados por un periodo constante.

## Validación en el juego (exe `32b6e9a2`)

- **WAV idénticos** a los de referencia de 40 parches:
  - demo PSS completa, sin variable y con L50;
  - oficina (XAG);
  - fundido;
  - castillo;
  - checkpoint XAG v4.
- **Fundido de la oficina:** llega al mismo tick a los 26,1 s sin límite y a los 35,8 s con L50. Ya no hay ráfaga; los
  unos 9,7 s de deuda no se recuperan.
- **Evidencia previa con la misma lógica** (fase SCHED_PAL_002, otro exe):
  - con paradas simuladas de 80 ms, 250 ms y 2 s durante una película a 50 Hz: con L50, 2 VBlanks de recuperación y
    0 recortes; sin límite, 139 VBlanks a 172 fields/s y 97 440 frames recortados;
  - L250 y L500 vuelven a recortar.

## Limitaciones

- **Este parche no corrige los 60 Hz del contenido PAL:** con L50 la demo PSS sigue recortando un 16,7 % en reproducción
  estable, como con 40 parches.
  - Los saltos continuos del audio PSS los causa la cadencia de VBlank de 60 Hz, que se estudiará y promocionará por
    separado.
  - DEBT solo elimina las ráfagas tras escenas lentas o paradas del host.
- L50 solo se ha medido en una máquina. En un host más lento, un jitter normal de más de 50 ms se convertiría en un
  pequeño retraso permanente, no en ráfagas.
- Tras una parada, la latencia del anillo de audio puede quedar hasta L por encima de la habitual.
- El valor de L no depende del juego ni del perfil de vídeo.
