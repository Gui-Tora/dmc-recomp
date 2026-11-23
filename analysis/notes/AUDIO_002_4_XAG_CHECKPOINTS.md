# AUDIO_002-4 — Restauración de las voces XAG desde checkpoints (parche 39)

- **Fecha:** 2026-11-23 (fase cerrada el 2026-11-20; revisión final r2).
- **ELF:** `SLES_503.58` PAL, SHA-256 `d0753a6b…bab4`, CRC32 `77654AD2`, entry `0x00100008`.
- **IRX:** `TSNDDRVM.IRX`, SHA-256 `c00b17f0…31fd`.
- **Base:** `master` `def2144e` (PR #17), vendor `b911ca6e` (38 parches).
- **Resultado:** `patches/AUDIO_002_4_xag_checkpoint_v4.patch`, SHA-256 `15ad478b…2d72d`. `patched_commit` con 39
  parches: `a46af4f5781dae74194cc09203fefa27071c7cec`.
- **Alcance:** solo cambia el payload TSNDDRVM de la sección IOP del checkpoint y el relanzamiento de la voz XAG. No
  toca GS, planificador, PSS, SE, BGM ni SPU2. No regenera el EE.

Clasificación: **HECHO**, **INFERENCIA**, **HIPÓTESIS**, **UNKNOWN**.

## 1. Problema (HECHO)

- El payload v2 de TSNDDRVM (parches 17 y 38) guarda el estado lógico del canal XAG: estado, entrada, volumen,
  skip, `clipSamples`, `playStartTick` y `playEndTick`.
- No guarda la fuente de audio, porque la fuente es estado del host.
- Al cargar, el mezclador del parche 37 detiene las fuentes y no relanza nada. En el checkpoint de la oficina, el
  juego ve la voz en el estado 3 (`XagChkPlay` = 1) y no suena nada.

## 2. Payload v4

- **Cuerpo:** el mismo que v2, con el byte de versión a 4.
- **Extensión:** `XagMVol` y, por canal:
  - `clipOffset`/`clipBytes`;
  - `XagVolumeState` completo: volumen, objetivo, paso, flags, volumen de canal, maestro, VOL actuales y previos,
    inicio de rampa, rampa activa y aticks;
  - **tipo de voz:** 0 = ninguna, 1 = sonando, 2 = rampa final de un fundido de salida;
  - posición en frames de 48 kHz;
  - LBA, bloque, canales y frecuencia del clip;
  - hash FNV-1a del estado de la fuente viva. Solo se guarda si su posición publicada es exactamente la del
    guardado.
- **Antes de escribir:** una copia de los canales se avanza hasta el tick del guardado (aticks y parada por
  fundido), como haría el siguiente RPC.
- **Versiones:**
  - v3 tenía la misma disposición, con el tipo limitado a 0/1;
  - v4 añade el tipo 2 (§8);
  - la versión de la sección IOP (2) y el formato general del checkpoint no cambian.
- **Nombre:** el candidato se llamó `…_v3.patch` durante la fase y se publica como `…_v4.patch` con los mismos
  bytes. El `patched_commit` depende del árbol resultante, no del nombre, y se ha verificado. En el código,
  «v3 extension» designa la disposición que v4 comparte.

## 3. Reconstrucción de los v2 (HECHO)

| Dato | ¿En el v2? | Reconstrucción |
|---|---|---|
| Clip | entrada, `skipStart`/`skipCount` | el rango es una función pura de entrada, skip y bindings (misma aritmética que `XagCdRead`); se comprueba frente a `clipSamples`, y si no coincide no se relanza |
| Posición | `playStartTick` + tick restaurado | `(tick − playStartTick) × 960`: la fuente de guest es determinista en tiempo de guest |
| Decodificador y remuestreador | no | reproducción determinista desde el inicio del clip (§5), sin hash que comprobar |
| Volumen | `volume` | directo; el escritor v2 nunca guardó fundidos |
| `XagMVol` | no | 0x3FFF si el driver estaba inicializado (único escritor en el ELF: `SndInit`, AUDIO_002-3 §1.4) |
| Fundido en curso | no | no se puede saber; se carga sin fundido |

Validación: el v2 de la oficina se relanza en el frame 73 920, con 0 diferencias frente a un decodificador
independiente en 78 720 frames. El v2 del parche 37 en el castillo (tick 26800) da el mismo WAV que la ejecución
continua.

## 4. Lectura de los v3

- Un v3 (escrito por la revisión r1 del candidato) se lee con el mismo código que v4. Su tipo de voz solo puede
  ser 0 o 1.
- Validación: el checkpoint v3 `k39_26800` (castillo, a mitad de voz) carga con el exe definitivo y da un WAV
  idéntico a la ejecución continua.
- Un v3 guardado justo en la parada de un fundido no lleva la rampa final, que r1 no representaba: esa cola se
  pierde al cargarlo. No existen tales archivos fuera de las pruebas de la fase.

## 5. Reconstrucción del decodificador por reproducción determinista

- El relanzamiento ocurre en el **primer RPC** tras la carga, con el tick restaurado ya en vigor y antes de los
  comandos de ese RPC.
- Se crea una fuente de guest nueva y `seek(posición)` decodifica el ADPCM y remuestrea desde el inicio del clip
  hasta la posición.
  - El decodificador y el remuestreador son enteros, así que el estado resultante es único (HECHO por los tests
    unitarios y las restauraciones en el juego).
- Las fuentes y workers del estado anterior se detienen al deserializar. No se acumulan fuentes: tras restaurar,
  `sources=1 added=1`.

## 6. Verificación del hash interno

- La fuente viva publica `{posición, hash}` tras cada render. El hash cubre:
  - el estado del decodificador: s1/s2 por canal, bytes leídos, bloques leídos, posición dentro del bloque
    decodificado y fallo de lectura;
  - el del remuestreador: fase, ventana y contadores.
- **v3/v4:**
  - se comprueban la identidad del clip (LBA, rango, bloque, canales y frecuencia) y la posición;
  - si el payload lleva hash, el estado reproducido debe dar el mismo valor;
  - ante cualquier discrepancia, la voz queda en silencio y se registra «checkpoint relaunch … refused».
- **Resultado (HECHO):** todos los relanzamientos de la fase encontraron el hash verificado. Por ejemplo:
  - castillo, frame 41 280: `10075b7b58178e00`;
  - oficina, frame 86 400: `4d2803ed3cc73a91`.
- **INFERENCIA:** el guardado ocurre después de que la VBlank haya renderizado el field anterior, así que el hash
  está disponible en los guardados habituales.

## 7. Volumen y fundidos

- El estado de volumen completo del parche 38 se restaura tal cual: un fundido en curso continúa desde el mismo
  atick.
- Prueba en la oficina: guardado a mitad de un fundido de salida (tick 16400, fundido entre 16389 y 16415).
  - En las dos restauraciones, el fundido termina en el mismo atick (500) y el mismo tick (16415).
  - El WAV `[16400, 16450)` es idéntico al de la ejecución continua.

## 8. Corrección de la cola residual (revisión r2)

### 8.1 Hipótesis previa y refutación

- **Hipótesis de la fase (r1):** guardar durante la rampa de 200 frames que sigue a la parada por fundido era una
  diferencia teórica, sin observar, de muy bajo nivel.
- **Evidencia que la refutó:** una prueba aislada con el servicio real (ver §13).
  - Con fundidos de 33, 37 y 42 aticks, la rampa se extiende al field en que el juego ya ve el estado 0. Al
    restaurar en ese tick, r1 perdía la cola: el canal estaba en estado 0 y no había nada que relanzar.
  - **Segundo defecto, en la ejecución continua:** al parar, r1 pedía `stopAudio`, y el siguiente RPC de comandos
    retiraba la fuente antes de renderizar la cola. Con un comando en cada tick, la última muestra no nula pasaba
    de 45 298 a 45 119 (igual con 46 098 → 46 079 y 47 098 → 47 019). El WAV dependía de si llegaba un comando.
- **Resultado en r1:** 6 casos fallidos.

### 8.2 Corrección

- La parada lógica cancela solo la copia `free` (`stopFreeCopy`). La fuente de guest termina sola tras su rampa.
- El tipo de voz 2 («rampa final») se guarda mientras la rampa se extiende al field del guardado.
  - Al cargar, se relanza una fuente que solo reproduce la rampa restante (`XagGainRenderer::resumeStopping`).
  - La rampa empieza en el atick de la parada, con VOL previo = VOL guardado y VOL = 0.
- La ruta anterior (`PS2X_AUDIO_MIXER=0`) no cambia.
- **Resultado en r2:** todos los casos pasan, en 3 ejecuciones idénticas, y la continua da el mismo PCM con y sin un
  comando en cada tick.

## 9. Separación entre estado guest y dispositivo host

- **Se guarda:** solo estado determinista del guest (canales, volumen, posición en tiempo de guest e identidad del
  clip). El hash es una huella de ese estado, no estado del host.
- **No se guarda; se reconstruye:**
  - los anillos del mezclador;
  - el dispositivo;
  - el flujo `free` y su posición en tiempo real.
- **Worker `free`:**
  - empieza en la misma posición que la fuente de guest;
  - el dispositivo se abre de forma perezosa en la primera VBlank, después del primer RPC; por eso el worker
    reintenta el primer bloque durante un máximo de 2 s;
  - **hipótesis refutada en la fase:** «basta con relanzar el worker en el primer RPC». La cadena con el exe
    `7bbfe08c` registró «host has no audio output» en todas las restauraciones.
  - Con el exe definitivo, `free.pushed` vale 1 088 288 en el castillo y 652 448 en el v2 de la oficina.
- **Voces duplicadas:** no hay. Hay una fuente de guest (WAV) y un worker `free` por voz; tras el fundido,
  `sources=0 removed=1`.

## 10. Coste medido de reconstrucción (HECHO, ISO en la caché del sistema)

Voz más larga (oficina, `[40,+272)`, ~8,49 M frames de salida). Tiempo de carga más relanzamiento:

| Posición | Tiempo |
|---|---|
| frame 96 000 (2 s de voz) | 1,7 ms |
| frame 3,84 M (~45 % del clip) | 57,1 ms |
| frame 8,45 M (cerca del final) | 122,7 ms |

- El coste es lineal con la posición, porque se decodifica desde el inicio del clip.
- **UNKNOWN:** el coste con la ISO fría.
- Corrige una estimación previa de 0,1–0,3 s que no estaba medida.

## 11. Compatibilidad y limitaciones de los checkpoints

- **Los exes con el parche 39** cargan v2, v3 y v4.
- **v4 no es compatible hacia atrás.** Los exes de 37 (`dfad804c`) y 38 parches (`b1717d60` y la referencia tras
  la fusión `1690380c`) lo rechazan al cargar:

  ```
  [CKPT:LOAD] FATAL: IOP deserialize: IOP: service 'TSNDDRVM' restore failed: TSNDDRVM: truncated or unknown payload
  ```

  - No se ejecuta ningún VSync.
  - El proceso sale con código **0**: quien dependa solo del código de salida no detecta el rechazo.
- **Diferencias de sección:** un v4 frente al v2 del 37 en el mismo tick difiere solo en la sección 6 (IOP).
- **Fundidos en un v2:** un v2 escrito con el parche 38 pierde el fundido en curso.
- **Ruta anterior (`PS2X_AUDIO_MIXER=0`):** no hay relanzamiento.

## 12. Tests y rendimiento (HECHO)

- **Tests unitarios (`ps2xTest/src/audio_xag_checkpoint_tests.cpp`, en el parche):**
  - reproducción hasta las posiciones 1, 960, 35 537 y 76 800 con el mismo hash y la misma continuación;
  - renderizador reconstruido a mitad de rampa, de fundido, de fundido de salida y dentro de la rampa final:
    salida bit a bit igual.
  - Total: 31/31 tests de audio (parches 37, 38 y 39), ×5.
- **En el juego, con el exe `cce52558`** (construido desde el árbol `a46af4f5`):
  - restauraciones v4 ×2 y v3 a mitad de voz: WAV idéntico;
  - durante un fundido, ×2: WAV idéntico;
  - v2 de la oficina: 0 diferencias;
  - menú v2: carga;
  - castillo completo `[26640, 28300)` idéntico al de otra ejecución;
  - `WM_CLOSE` a mitad de voz: código 0 en 0,16 s, sin procesos residuales.
- **Rendimiento:** castillo completo en 279,1 s, frente a 279,3 s con 38 parches.
- **Memoria:** 173–238 MB por proceso.

## 13. Prueba aislada del final de la voz y bloqueo del GS

- **Bloqueo en el juego (HECHO):** los guardados cerca de los ticks 28150, 28170 y 28180 los rechaza la validación
  de punto seguro del checkpoint («GS local->host buffer not fully consumed»). Esa validación es ajena al audio y
  no se ha modificado.
- **Prueba aislada** (local, fuera del repositorio: necesita la ISO):
  - usa el `TsnddrvmXagService` real, compilado desde el árbol de 39 parches;
  - un host simulado lee la ISO y emula el mezclador en el orden del exe: render del field T−1 en la VBlank de T,
    límite del checkpoint y después los RPC del tick;
  - restaurar consiste en serializar, crear un servicio nuevo y deserializar.
- **Casos del final de la voz:** guardados equivalentes a 28150, 28170 (último field con muestras no nulas), 28171,
  28180, 28187 (último field producido por el clip) y 28188 (primero tras el clip). En todos, PCM, `XagChkPlay` y
  fuentes son idénticos a la ejecución continua, sin muestras perdidas ni duplicadas.
- **Casos de la cola del fundido** (§8): 4 tiempos, con y sin un comando en cada tick. Todos idénticos en r2.
- **Qué queda en el repositorio:** el test permanente del parche cubre la parte del renderizador (reanudación dentro
  de la rampa final). La prueba de servicio con la ISO no está en la suite mantenida.
- **El final de la voz no se ha validado con un checkpoint completo del juego.**

## Riesgos que permanecen

- Final de la voz en el juego (§13).
- Cancelación de un worker `free` todavía activo: no ejercitada en el juego; la cubren los tests del parche 37.
- Coste con la ISO fría (UNKNOWN).
- La fase del atick y la rampa anti-clic son elecciones del modelo del parche 38, sin medir en vivo.
