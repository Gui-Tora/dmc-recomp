# AUDIO_002_3_xag_volume.patch — parche 38

**Estado:** activo (entrada 38 de `upstream.lock.json`). Fase AUDIO_002-3; nota:
`analysis/notes/AUDIO_002_3_XAG_VOLUME.md`.

| Campo | Valor |
|---|---|
| Commit base | `acacdf88` (37 parches) |
| `patched_commit` con 38 parches | `b911ca6ef8afcba2cc5d297ef8e31904167f244b` (árbol `922754af1af04df8a52de19ec42baebf1dc0db7a`) |
| SHA-256 | `142ab95e31f421e6792f840602ee809117d7143e8c9c66ae4b616b318911d156` |
| Archivos | 7 (+1009/−16) |

## Causa

El servicio XAG del parche 17 solo modela la máquina de estados de la voz. Ignora `XagSetVol` (0x38),
`XagSetFadeout` (0x37) y `XagSetMVol` (0x72), que el EE sí envía. Consecuencia:

- al saltar un evento, el juego envía `37 00 78`, pero la voz sigue sonando hasta el final del clip;
- `XagChkPlay` sigue respondiendo «sonando» cuando el IRX ya habría parado el canal con `xag_stop`.

## Cambio

1. **`ps2xIOP/include/ps2x/iop/audio_dsp.h`:** modelo de volumen XAG reconstruido del IRX, en enteros.
   - Registro del canal: `+0x02` flags, `+0x04` volumen de la entrada, `+0x06` volumen, `+0x08` objetivo y `+0x0A`
     paso; `XagMVol` global.
   - `XagSetVol`, `XagSetFade`, `XagSetFadeout`, `XagSetMVol`, `xag_default` y `xag_atick`.
   - Ganancia: `xag_setvol`/`calc_pan_vol_r`, con nivel `((vol × entry) >> 14) × MVol >> 14`, ley de paneo de seno y
     volumen de canal; VOLL/VOLR de 15 bits.
   - `xag_atick` a 240 Hz (temporizador 0x25800 del IRX), es decir, un atick cada 200 frames a 48 kHz, desfasado
     100 frames.
   - `XagVoiceControl`: dos colas acotadas de 64 comandos, una para la fuente de guest (con posición) y otra para
     el flujo `free`.
   - `XagGainRenderer`: aplica los VOL de la SPU a las muestras ya remuestreadas (`x × VOL >> 14`), con una rampa
     lineal de 200 frames por cambio (sin clics). Un fundido de salida que llega a 0 pasa a silencio en una rampa y
     termina.
2. **`ps2xIOP/src/modules/tsnddrvm_xag.cpp`:**
   - comandos `0x36`, `0x37`, `0x38`, `0x70` y `0x72`;
   - volúmenes de `XagStandby`/`XagStandby2` y valores por defecto de `TsndInitXag`;
   - aticks en `advanceLocked`. Un fundido de salida que llega a 0 deja el canal en el estado 0 en ese atick, como
     `xag_stop` en el IRX; es visible para el juego en `XagChkPlay`.
   - La fuente de guest y el worker `free` aplican el mismo modelo.
   - Payload v2 compatible: el campo `volume` guarda el volumen actual. Al cargar un v2, `XagMVol` se fija a 0x3FFF
     si el driver está inicializado, porque su único escritor es `SndInit` con 0x3FFF.
3. **`ps2xIOP/src/{module_factories.h,builtin_profiles.cpp}`:** tablas de paneo de las entradas (`0xA928`, `0xA92C`)
   y `xag_pan_tbl` (129 valores; es `round(4096 · sin(p · π / 256))`).
4. **`ps2xTest`:** `audio_xag_volume_tests.cpp`, con 11 tests.

No cambia nada de `ps2xRuntime` ni ninguna cabecera que incluya el EE generado: `ninja -n` da 18 objetos, ninguno
de `generated`.

## Diferencias observables

- **WAV a volumen por defecto:** la ganancia es 0x3FFE/0x4000 (−0,001 dB), como el registro de la SPU, en vez de
  la unidad del parche 37. Hasta −2 LSB a fondo de escala; validado bit a bit con una referencia independiente.
- **Saltos de evento con voz XAG:** la voz se desvanece en 120 aticks (0,5 s) y el canal pasa a 0. El juego ve
  `XagChkPlay = −1` antes que con el 37 (oficina: tick 16420, frente al `XagStop` del juego en el 16483).
- **`PS2X_AUDIO_MIXER=0`:** misma lógica y mismo estado lógico, sin ganancia (ruta anterior).

## Activación y reversión

- Siempre activo con el parche, sin variables nuevas.
- Reversión: quitar la entrada del lock y recompilar el runtime y el IOP de forma incremental.

## Limitaciones documentadas

- **240 Hz:** es una INFERENCIA. El temporizador hardware compara con 0x25800 ciclos del reloj de sistema del IOP
  (36,864 MHz); no se ha medido en vivo.
- **Fase del atick:** 100 frames tras el inicio de la voz. Es una elección del modelo, sin medir.
- **Suavizado de 200 frames:** la SPU real cambia VOLL/VOLR de golpe; aquí cada cambio es una rampa lineal hasta el
  siguiente atick, y la parada por fundido de salida es una rampa a 0 de 200 frames.
- **Parada lógica:** el canal pasa al estado 0 en el atick en que el fundido de salida llega a 0 (`xag_stop`).
- **Ganancia frente al parche 37:** 0x3FFE/0x4000 a volumen por defecto (hasta −2 LSB).
- **Ruta anterior (`PS2X_AUDIO_MIXER=0`):** misma lógica, sin ganancia, y corte inmediato al parar.
- **Opcodes no modelados** (el EE de DMC no los envía): `0x30`, `0x34`, `0x35`, `0x39`–`0x3C`, `0x3E`, `0x3F`
  (fase, pausa, canales de reproducción, pitch, volumen, paneo y fundido por canal, fundido de pitch), además del
  bucle (`0x71`, almacenado pero no reproducido).
