# AUDIO_002_4_xag_checkpoint_v4.patch — parche 39

**Estado:** entrada 39 de `upstream.lock.json`. Fase AUDIO_002-4; nota: `analysis/notes/AUDIO_002_4_XAG_CHECKPOINTS.md`.

| Campo | Valor |
|---|---|
| Commit base | `b911ca6e` (38 parches) |
| `patched_commit` con 39 parches | `a46af4f5781dae74194cc09203fefa27071c7cec` (árbol `9a6f62348c31a055cbeb8065b7cfd60d552198ea`) |
| SHA-256 | `15ad478b8027bd365b54ccb8cb15486733a89e8baf0e10a5d021b36db2afa72d` |
| Archivos | 5 (+710/−40) |

**Nombre.** Durante la fase, el candidato se llamó `AUDIO_002_4_xag_checkpoint_v3.patch`. Se publica con el nombre
`…_v4` porque el payload que escribe es v4. Los bytes son los del candidato validado (revisión r2). El
`patched_commit` solo depende del árbol resultante, no del nombre del archivo, y se ha verificado. En el código, los
comentarios «v3 extension» se refieren a la disposición introducida por v3, que v4 comparte.

La revisión r1 (`1b1ecc35…`) **no** se publica: perdía la cola del fundido de salida (ver «Cola residual»).

## Causa

- El payload v2 de TSNDDRVM guarda el estado lógico del canal XAG: estado, entrada, volumen, skip, `clipSamples`,
  `playStartTick` y `playEndTick`.
- No guarda ninguna fuente de audio. Al cargar, el mezclador (parche 37) detenía las fuentes y no relanzaba nada.
- Ejemplo, el checkpoint de la oficina:
  - el juego ve la voz en el estado 3 («sonando»);
  - no se oye nada hasta la siguiente voz.

## Cambio

1. **`ps2xIOP/include/ps2x/iop/audio_dsp.h`:**
   - `XagClipReader::stateHash` y `CubicResampler::stateHash`: FNV-1a del estado del decodificador ADPCM (s1/s2 por
     canal y posición de lectura) y del remuestreador (fase, ventana y contadores);
   - `XagVoiceControl::publish/snapshot`: la fuente viva publica su posición y su hash tras cada render;
   - `XagGainRenderer::resumeStopping`: reanuda una fuente dentro de la rampa final de 200 frames.
2. **`ps2xIOP/src/modules/tsnddrvm_xag.cpp`:**
   - **Payload v4** (sección IOP; la versión de sección sigue siendo 2):
     - el cuerpo v2 con el byte de versión a 4;
     - una extensión con `XagMVol` y, por canal:
       - rango del clip;
       - `XagVolumeState` completo: volumen, objetivo, paso, flags, volumen de canal, maestro, VOL actuales y
         previos, rampa y aticks;
       - tipo de voz: 0 = ninguna, 1 = sonando, 2 = rampa final de un fundido de salida;
       - posición en frames;
       - LBA, bloque, canales y frecuencia;
       - hash del estado de la fuente viva, solo si había renderizado exactamente hasta esa posición.
     - Antes de escribir, una copia de los canales se avanza hasta el tick del guardado (aticks y parada por
       fundido), como haría el siguiente RPC.
   - **Carga:**
     - acepta v2, v3 y v4;
     - detiene las fuentes y workers del estado anterior;
     - deja pendiente el relanzamiento de cada voz sonando o en rampa final.
   - **Relanzamiento** en el primer RPC tras la carga, con el tick restaurado ya en vigor:
     - crea la fuente de guest y la lleva a la posición reproduciendo de forma determinista el decodificador y el
       remuestreador desde el inicio del clip (`seek`);
     - en v3/v4 comprueba la identidad del clip, la posición y, si se guardó, el hash;
     - ante cualquier discrepancia, la voz queda en silencio y se registra en el log.
   - **v2:** posición `(tick − playStartTick) × 960`; rango del clip recalculado desde la entrada y el skip
     (comprobado con `clipSamples`); volumen del payload; `XagMVol` = 0x3FFF si el driver estaba inicializado.
   - **Cola residual del fundido** (revisión r2): la parada lógica por fundido (`xag_stop`) cancela solo la copia
     `free` (`stopFreeCopy`); la fuente de guest termina sola tras su rampa.
   - **Worker `free`:** empieza en la misma posición. Si el dispositivo aún no está abierto (carga antes de la
     primera VBlank), reintenta el primer bloque durante un máximo de 2 s.
3. **`ps2xTest`:** `audio_xag_checkpoint_tests.cpp`, con 2 tests:
   - **reproducción hasta las posiciones 1, 960, 35 537 y 76 800:** mismo hash y misma continuación que el render
     continuo; el hash distingue posiciones vecinas;
   - **renderizador reconstruido desde su estado** a mitad de rampa, de fundido y de fundido de salida, y dentro de
     la rampa final con `resumeStopping`: salida bit a bit igual a la continua.

No cambia `ps2xRuntime` ni ninguna cabecera incluida por el EE generado: no se recompila `generated`.

## Cola residual del fundido (r1 → r2)

Una prueba aislada, con el servicio real y la ISO, encontró dos defectos en r1:

1. **Restaurar en el tick de la parada.** El juego ya ve el estado 0, pero la rampa de 200 frames se extiende a ese
   field. r1 no tenía estado que la representara y perdía la cola: PCM distinto en 3 de 4 tiempos de fundido.
2. **Ejecución continua sin restaurar.** Al parar por fundido, r1 pedía `stopAudio`. El siguiente RPC de comandos
   retiraba la fuente antes de la cola, así que el WAV dependía de si llegaba un comando en ese tick.

r2 corrige los dos defectos solo en la ruta XAG con mezclador. Resultado de la prueba: 6 fallos en r1; todo pasa en
r2, en 3 ejecuciones idénticas.

## Compatibilidad

- **Los exes con este parche** cargan v2, v3 y v4.
- **v4 no es compatible hacia atrás.** Los exes de 37 y 38 parches rechazan un payload v4 al cargar:

  ```
  [CKPT:LOAD] FATAL: IOP deserialize: IOP: service 'TSNDDRVM' restore failed: TSNDDRVM: truncated or unknown payload
  ```

  - El juego no llega a ejecutarse (0 VSync).
  - El proceso sale con código 0.
- **v2 escrito por un exe del 38:** pierde un fundido en curso, porque ese escritor no lo guardaba.
- **Sin cambios en el formato:** ni en el formato general ni en las versiones de sección. Solo cambia el contenido
  de la sección 6 (IOP).
- **Ruta anterior (`PS2X_AUDIO_MIXER=0`):** no hay relanzamiento, porque no tiene fuente que reconstruir.

## Validación (exe `cce52558`, construido desde el árbol `a46af4f5`)

- **Restauraciones en el juego:**
  - v4 a mitad de voz en el castillo, ×2: WAV idéntico a la ejecución continua, con el hash verificado;
  - carga de un v3 de r1: WAV idéntico;
  - durante un fundido en la oficina, ×2: WAV idéntico;
  - v2 de la oficina: 0 diferencias frente a un decodificador independiente.
- **Prueba aislada del final de la voz y de la cola del fundido:** todo idéntico. Cubre guardados equivalentes a
  los ticks 28150, 28170, 28171, 28180, 28187 y 28188.
- **Fuentes y dispositivo:**
  - sin fuentes acumuladas ni voces duplicadas en `free`;
  - el worker `free` entrega al dispositivo tras restaurar.
- **Determinismo y cierre:** WAV del castillo completo determinista; `WM_CLOSE` limpio.
- **Tests:** 31/31 de audio, ×5.
- **Rendimiento:** castillo 279,1 s frente a 279,3 s con 38 parches.
- **Coste del relanzamiento** (voz más larga, ISO en caché): 1,7 ms, 57,1 ms y 122,7 ms desde los frames 96 000,
  3,84 M y 8,45 M.

## Limitaciones

- **Final de voz en el juego:** probado solo de forma aislada. Los guardados del juego cerca de los ticks
  28150–28180 los rechaza la validación de punto seguro del GS («GS local->host buffer not fully consumed»), que no
  se toca.
- **Coste del relanzamiento:** crece de forma lineal con la posición, porque se decodifica desde el inicio del clip.
  Con la ISO fría no se ha medido.
- **Cancelación de un worker `free` activo:** no se ha ejercitado en el juego. La cubren los tests del parche 37.
- **Modelo de volumen:** la fase del atick y la rampa anti-clic vienen del parche 38 y no se han medido en vivo.
