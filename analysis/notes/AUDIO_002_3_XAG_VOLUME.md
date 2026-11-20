# AUDIO_002-3 — Volumen, volumen maestro y fundidos de las voces XAG (parche 38)

- **Fecha:** 2026-11-20.
- **ELF:** `SLES_503.58` PAL, SHA-256 `d0753a6b…bab4`, CRC32 `77654AD2`, entry `0x00100008`.
- **IRX:** `TSNDDRVM.IRX`, SHA-256 `c00b17f0…31fd` (`.symtab` propia).
- **Base:** `master` `46af3155`, vendor `acacdf88` (37 parches).
- **Resultado:** `patches/AUDIO_002_3_xag_volume.patch` (SHA-256 `142ab95e…d156`); `patched_commit` con 38 parches
  `b911ca6ef8afcba2cc5d297ef8e31904167f244b`.
- **Estado:** solo cambia la salida de audio y el estado lógico de la voz que el IRX también cambia. No implementa
  SE, BGM, PSS ni SPU2.

Clasificación: **HECHO**, **INFERENCIA**, **HIPÓTESIS**, **UNKNOWN**.

## 1. Problema (HECHO)

- El servicio XAG (parche 17) ignoraba `XagSetFadeout` (`0x37`), `XagSetVol` (`0x38`) y `XagSetMVol` (`0x72`),
  aunque el EE los envía.
- Al saltar un evento (START durante la animación del castillo, SELECT en la cinemática de la oficina), el juego
  envía `37 00 78`.
  - La voz seguía sonando hasta el final del clip.
  - `XagChkPlay` seguía respondiendo «sonando» después de que el IRX hubiera parado el canal.

## 2. Semántica del IRX (HECHO, desensamblado y `.symtab`)

### 2.1 Despacho (`treq_XAG@0x36EC`, tabla `.rodata 0xA678`, índice `opcode − 0x30`)

| Opcode | Función | Argumentos | Envoltorio EE |
|---|---|---|---|
| `0x3D` | `XagStandby2` | entrada, volumen (`0x7F → 0x80`, `<< 7 & 0xFF80`) | `SdrXagStandby2` |
| `0x37` | `XagSetFadeout` | tiempo de 16 bits (aticks) | `SdrXagFadeout` (con 0 llama a `SdrXagStop`) |
| `0x38` | `XagSetVol` | volumen (`0x7F → 0x80`, `<< 7 & 0xFF80`) | `SdrXagVol` |
| `0x72` | `XagSetMVol` | 16 bits | `SdrXagMasterVol` (solo `SndInit`, con `0x3FFF`) |
| `0x36` | `XagSetFade` | objetivo (`<< 7`), tiempo de 16 bits | — |
| `0x31` / `0x70` | `XagStandby` / `XagDefault` | entrada / — | — |

### 2.2 Registro del canal (`0xE3B0 + core × 0xA8`)

- `+0x02` flags: 0x1/0x2 volumen sucio, 0x10 fundido, 0x20 fundido de salida;
- `+0x04` volumen de la entrada (palabra 2 baja de `Tsnd_xag_tbl`; 0x3FFF);
- `+0x06` volumen, `+0x08` objetivo, `+0x0A` paso;
- `+0x0C+i` volumen de canal (0x80 = unidad);
- `+0x54+i` paneo (de la tabla de la entrada).
- Global `XagMVol`: vale 0 en `.sdata`; su único escritor es `SndInit` (0x3FFF), antes de `SdrXagInit`.

### 2.3 Funciones

- **`XagSetVol`:** `vol = v` y marca de sucio. Se aplica en el **siguiente atick**.
- **`XagSetFade(objetivo, t)`:** se ignora durante un fundido de salida.
  - `paso = trunc((objetivo − vol) / t)`; con `t = 0`, la diferencia entera;
  - si sale 0, vale `−1` cuando el objetivo es 0 y, si no, el signo de la diferencia.
- **`XagSetFadeout(t)`:** `XagSetFade(0, t)` y marca 0x30.
- **`XagSetMVol(v)`:** limitado a [0, 0x4000]; marca de sucio solo en el core actual.
- **`XagStandby2`:** `vol = v`, flags a 0.
- **`XagStandby`:** `XagDefault` (salvo durante un fundido de salida) y flags a 0.
- **`TsndInitXag`:** `xag_default` y flags a 0 en los dos registros.
- **`xag_atick`** (estados 3/5):
  - con 0x10: `vol += paso`, limitado al objetivo;
  - al llegar al objetivo quita 0x10, y si había 0x20 llama a **`xag_stop`** sin escribir más volumen;
  - si no, escribe los VOL.
- **Nivel:** `((vol × entry) >> 14) × XagMVol >> 14`, con redondeo hacia cero y desplazamiento lógico.
- **`xag_setvol` / `calc_pan_vol_r`**, por canal:
  - `VOLR = (nivel × tbl[pan] >> 12) × chvol >> 7`, y `VOLL` con `0x80 − pan`;
  - limitado a 0x3FFF;
  - `tbl` = `xag_pan_tbl` (`.data 0xC510`) = `round(4096 · sin(p · π / 256))`.
- **Paneos:** las entradas 0/1 (`0x1B7`/`0x1B8`) usan `{0x00, 0x80}` (L y R puros); las 2/3, `{0x40, 0x40}`.
- **Linealidad, comprobada:** volumen, volumen maestro y volumen de canal son lineales en amplitud. El paneo sigue
  una ley de seno. El fundido es lineal en unidades de volumen por atick.

### 2.4 Frecuencia del atick

- **HECHO:** `TsndInitTimer@0x2238` usa un temporizador hardware de fuente 1 con comparación `0x25800`.
  `TimerIntFunc` despierta el hilo `ATick`, que llama a `XagATick` una vez.
- **INFERENCIA:** con el reloj del sistema del IOP (36,864 MHz), **240 Hz**: 200 frames a 48 kHz, 4,8 por field PAL.
  Lo respaldan las duraciones del juego (120, 200, 400 y 480 aticks = 0,5–2 s).
- **No medido en vivo:** el PCSX2 instalado no expone el servidor GDB.

## 3. Modelo implementado

- Funciones 1:1 en `audio_dsp.h`, en enteros. El servicio y la fuente de audio ejecutan la misma secuencia: el
  estado lógico y la ganancia coinciden.
- **Aticks:** en `200 · n − 100` frames desde el inicio de la voz. La fase **es una elección**: el temporizador es
  libre y su fase no es observable. Medio periodo evita empates con los bordes de field.
- **Servicio:** ejecuta los aticks anteriores al field actual en cada RPC. Un fundido de salida que llega a 0 deja
  el canal en el **estado 0** en ese atick (**parada lógica**, como `xag_stop`); `XagChkPlay` pasa a −1.
- **Fuente:** recibe cada comando con su posición en frames y aplica la ganancia después del remuestreo
  (`x × VOL >> 14`, como el volumen fijo de la SPU).
- **Suavizado:** la SPU cambia el volumen de golpe. Aquí cada cambio es una **rampa lineal de 200 frames** hasta el
  siguiente atick; la parada por fundido de salida es una rampa a 0 de 200 frames (unos 4 ms) tras la parada
  lógica. Es una diferencia deliberada frente al hardware.
- **Flujo `free`:** el mismo modelo en tiempo real, con los comandos aplicados al llegar.
- **Colas:** dos de 64 comandos por voz, acotadas; si se llenan se descarta y se cuenta (nunca ocurrió).

## 4. Validación (HECHO)

- **Tests:** 29/29 ×5 (18 del mezclador + 11 de volumen: fórmulas, fundidos, parada, rampas, bloques,
  servicio frente a fuente, cola entre hilos). GS: 85/85, y el GS Replay coincide con la referencia.
- **WAV del castillo con fundido** (START, `37 00 78`): **0 diferencias** frente a una referencia independiente en
  Python del modelo IRX aplicada al WAV del parche 37, que a su vez es idéntico a un decodificador independiente.
- **WAV completo del castillo** sin comandos: 0 diferencias frente al 37 por la ganancia del modelo. Idéntico con
  GS síncrono y asíncrono.
- **Oficina, SELECT:**
  - fundido de salida en el tick 16389;
  - `xag_stop` en el atick 500, estado 0 en el 16415, y el juego ve `XagChkPlay = −1` en el 16420;
  - con el 37 seguía en 1 hasta que el propio juego enviaba `XagStop` en el 16483.
- **`PS2X_AUDIO_MIXER=0`:** las líneas lógicas del castillo son idénticas a las de la ruta con mezclador, y el
  fundido de la oficina para en el mismo tick.
- **Rendimiento en la oficina** (alternando con la referencia de 37 parches): +0,4 %. Memoria: 173–238 MB por
  proceso.

## 5. Diferencias y limitaciones

1. **Ganancia frente al parche 37:** a volumen por defecto, VOL = 0x3FFE (0x3FFE/0x4000 = −0,001 dB); hasta −2 LSB
   a fondo de escala.
2. **Comportamiento visible (conforme al IRX):** un fundido de salida termina la voz antes del final del clip.
3. **Ruta anterior (`PS2X_AUDIO_MIXER=0`):** aplica la lógica pero no la ganancia; `streamPcm` reproduce el PCM tal
   cual y la parada corta el sonido.
4. **Sin modelar** (el EE de DMC no los envía):
   - fase, volumen, paneo y fundido por canal (`0x30`, `0x3A`–`0x3C`);
   - pausa (`0x34`), canales de reproducción (`0x35`), pitch (`0x39`, `0x3E`, `0x3F`);
   - bucle (`0x71`: se guarda, no se reproduce).
5. **Sin ejercitar en el juego:** `0x38` y `0x36` (solo tests). `SdrXagVol` llega por `SndAdsVolSet`/`SndControl`
   en escenas no medidas.
6. **Checkpoints:** el payload sigue siendo v2. Un fundido en curso no se guarda, y una voz guardada no se relanza
   al cargar; eso es AUDIO_002-4, todavía no integrado.
