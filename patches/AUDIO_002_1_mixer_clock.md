# AUDIO_002_1_mixer_clock.patch — parche 37 (experimental)

**Estado:** experimental, activo por defecto con la ruta anterior disponible (`PS2X_AUDIO_MIXER=0`). Entrada 37 de
`upstream.lock.json`. Fase AUDIO_002-1/2; nota: `analysis/notes/AUDIO_002_1_2_MIXER_CLOCK.md`.

| Campo | Valor |
|---|---|
| Commit base | `247af590` (36 parches) |
| `patched_commit` con 37 parches | `acacdf88cf4ea06b8dd50fd6f5face6974a46e38` (árbol `7b86b7ab733bd6165346edd3da6303e594cdd900`) |
| SHA-256 del parche | `411e4862a462f24dc368546015495ccac23a3bad836992c9375fc5d0c6b4297f` |

## Causa

AUDIO_001 y AUDIO_002-0 (resumen en `analysis/notes/AUDIO_002_SDRGETSTATE_PORT_STATES.md`) dejaron tres problemas de
salida:

- el XAG va por un `std::deque` sin límite: ≈31 MB en la oficina;
- no hay referencia determinista del audio: todo se reproduce por reloj de host, unas 9× por delante del juego;
- no hay dónde conectar las futuras fuentes PSS, SE y BGM.

## Cambio

1. **`ps2xIOP/include/ps2x/iop/audio_dsp.h`** (nuevo, solo cabecera).
   - Decodificador SPU-ADPCM, movido sin cambios desde TSNDDRVM.
   - `CubicResampler`: Catmull-Rom entero, fase 32.32.
   - `XagClipReader` y la interfaz `AudioRenderSource`.
2. **`ps2xRuntime/src/lib/audio/audio_mixer.h`** (nuevo, privado, solo cabecera).
   - **`SpscFrameRing`:** el productor nunca toca el índice de lectura. Si se llena, se descarta lo más nuevo. El
     recorte a latencia objetivo (descartar lo más antiguo) y el vaciado los hace el consumidor.
   - **`DeviceGate`:** protocolo `seq_cst` entre el callback y el cierre.
   - **`WavSink`:** RIFF reescrito cada segundo de guest.
   - **`AudioMixer`:** 960 frames por field (48 000 / `PS2X_AUDIO_FIELD_HZ`, por defecto 50), con resto acumulado.
     Hay dos mezclas: WAV (todas las fuentes) y dispositivo (solo los grupos en modo `guest`).
3. **`ps2xIOP/include/ps2x/iop/iop_host.h`:** API opcional con implementación vacía por defecto.
   - `audioMixerEnabled`, `audioDeviceMode`;
   - `addGuestAudioSource`, `removeGuestAudioSource`;
   - `pushFreeRunningPcm`, `flushFreeRunningPcm`.
4. **`ps2xRuntime/src/lib/ps2_iop_host.{h,cpp}`:**
   - crea el mezclador salvo con `PS2X_AUDIO_MIXER=0`;
   - un único `AudioStream` de 48 kHz s16 estéreo, que se abre en la primera VBlank;
   - el callback accede al mezclador solo a través de la `DeviceGate`;
   - cierre ordenado: detach, `Unload`, cierre de fuentes y del WAV.
5. **`ps2xRuntime/include/runtime/ee_scheduler.h`, `Kernel/EeScheduler.cpp`:** `setHostVBlankObserver`.
   - Se llama en `VBlankStart`, después de publicar el tick.
   - No cambia ciclos, prioridades, orden de eventos ni el periodo de 60 Hz.
6. **`ps2xRuntime/src/lib/ps2_runtime.cpp`:** registra el observador y lo retira antes de destruir el host.
7. **`ps2xIOP/src/modules/tsnddrvm_xag.cpp`:**
   - **Sin cambios en la máquina de estados que ve el juego:** `XagChkPlay`, duración lógica, checkpoint v2.
   - **Con el mezclador:**
     - `XagStart` añade una `XagGuestSource` (ISO → ADPCM → 48 kHz, en el hilo ejecutor);
     - en modo `free` (el defecto), un worker reproduce una copia independiente en tiempo real por un flujo
       acotado (~0,5 s), con su propio lector, remuestreador y fichero;
     - `XagStop` retira la fuente y vacía el flujo;
     - al restaurar un checkpoint se detiene el audio previo (no se relanzan las voces v2).
   - **Sin el mezclador:** la ruta `streamPcm` queda intacta.
8. **`ps2xTest`:** `audio_mixer_tests.cpp` (18 tests) y su registro en `main.cpp` y `CMakeLists.txt`.

Ninguna de las 19 cabeceras que incluye el código EE generado se modifica (`ps2_runtime.h`, `ps2_audio.h`,
`ps2_memory.h`, `iop_types.h`, …). El EE no se regenera ni se recompila: `ninja -n` lista 31 objetos del runtime y
del IOP y ninguno de `generated`.

## Activación

| Variable | Valor por defecto | Efecto |
|---|---|---|
| `PS2X_AUDIO_MIXER` | `1` | `0`: ruta anterior (`PcmStreamSlot`); el mezclador no se crea |
| `PS2X_AUDIO_XAG_MODE` | `free` | `guest`: el dispositivo recibe el XAG en tiempo de guest; `off`: solo WAV |
| `PS2X_AUDIO_DEVICE` | `1` | `0`: ningún grupo va al dispositivo (pruebas) |
| `PS2X_AUDIO_WAV` | — | ruta del WAV determinista (s16 estéreo, 48 kHz, 960 frames por field) |
| `PS2X_AUDIO_WAV_TICKS` | todo | `a:b`: solo los fields `[a, b)`; el fichero se cierra al llegar a `b` |
| `PS2X_AUDIO_FIELD_HZ` | `50` | frecuencia de field del juego (DMC PAL). Es general, pero el valor por defecto lo es para este ELF |
| `PS2X_AUDIO_STATS_TICKS` | `500` | periodo de la línea `[AUDIO:mix]` |

PSS, SE y BGM quedan en modo `guest`, pero todavía no existen fuentes de esos grupos.

## Reversión

- Quitar la entrada 37 del lock y del `README` y volver al `patched_commit` `247af590`, con `pipeline.py bootstrap`
  o con `git checkout` del vendor.
- Recompilación incremental: solo runtime e IOP.
- Sin recompilar: `PS2X_AUDIO_MIXER=0`.

## Validación

Ver `analysis/notes/AUDIO_002_1_2_MIXER_CLOCK.md` (tests 18/18, GS 85/85, WAV del castillo bit a bit frente a un
decodificador independiente, checkpoints 12/12, rendimiento dentro de ±2,5 %, reproducción del lock).
