# AUDIO_002-1/2 — Mezclador con reloj de guest y salida WAV determinista (parche 37)

- **Fecha:** 2026-11-19 (validación) / 2026-11-20 (promoción).
- **ELF:** `SLES_503.58` PAL, SHA-256 `d0753a6b…bab4`, CRC32 `77654AD2`, entry `0x00100008`.
- **Base:** `master` `3f212b11`, vendor `247af590` (36 parches). Exe de referencia `f9b98c83…`.
- **Resultado:** parche 37 `patches/AUDIO_002_1_mixer_clock.patch` (SHA-256 `411e4862…297f`); `patched_commit`
  con 37 parches `acacdf88cf4ea06b8dd50fd6f5face6974a46e38`. Exe candidato validado `756d4d1f…`.
- **Estado:** **experimental**. Solo cambia la **salida** de audio; las respuestas que ve el juego no cambian.
  No implementa SE, BGM, PSS ni SPU2.
- **Hallazgo separado** (sin corrección): la causa de que no suenen los pasos de Dante está en
  [AUDIO_002_SDRGETSTATE_PORT_STATES.md](AUDIO_002_SDRGETSTATE_PORT_STATES.md).

Clasificación: **HECHO** (observado o demostrado), **INFERENCIA**, **HIPÓTESIS**, **UNKNOWN**.

## 1. Arquitectura del mezclador

Antes del parche, el XAG (la voz de las cinemáticas, servicio TSNDDRVM del parche 17) se decodificaba en un hilo
y se encolaba en un `std::deque` sin límite (≈31 MB en la oficina) que el dispositivo reproducía por reloj de host,
unas 9× por delante de un juego más lento que tiempo real. No había una referencia determinista del audio ni un
punto donde conectar otras fuentes.

Piezas nuevas (solo cabeceras, ninguna incluida por el código EE generado):

- `ps2xIOP/include/ps2x/iop/audio_dsp.h`:
  - decodificador SPU-ADPCM (movido sin cambios desde TSNDDRVM);
  - `CubicResampler`: Catmull-Rom en enteros, fase 32.32, reproducible bit a bit;
  - `XagClipReader` y la interfaz `AudioRenderSource`.
- `ps2xRuntime/src/lib/audio/audio_mixer.h` (privado del runtime):
  - **`SpscFrameRing`**: anillo de un productor y un consumidor. El productor (hilo del guest) solo escribe `W`; el
    consumidor (callback del dispositivo) solo escribe `R`.
    - Si se llena, el productor descarta lo más nuevo y nunca toca `R`.
    - El recorte a latencia objetivo (descartar lo más antiguo: a 60 ms si se superan 250 ms) lo hace el consumidor
      sobre su propio `R`.
    - Vaciado (checkpoint, `XagStop`): el productor publica `flushTo = W` y `flushSeq++` con release; el
      consumidor hace `R = max(R, flushTo)`.
  - **`DeviceGate`**: protocolo `seq_cst` entre el callback y el cierre. El callback hace `++inside`, carga el
    puntero, lo usa y hace `--inside`; `detach` pone el puntero a `nullptr` y espera a `inside == 0`.
  - El callback no reserva memoria, no usa mutex y no espera al guest (búferes en pila de 256 frames).
  - **`WavSink`** y **`AudioMixer`**.
- `ps2_iop_host.{h,cpp}`: un único `AudioStream` de 48 kHz s16 estéreo, abierto en la primera VBlank.
- `EeScheduler`: `setHostVBlankObserver`, llamado en `VBlankStart` después de publicar el tick. No cambia ciclos,
  prioridades, orden de eventos ni el periodo del planificador (60 Hz).
- `iop_host.h`: API opcional con implementación vacía por defecto (`audioMixerEnabled`, `audioDeviceMode`,
  `addGuestAudioSource`, `removeGuestAudioSource`, `pushFreeRunningPcm`, `flushFreeRunningPcm`).

## 2. Reloj: 960 frames por tick PAL

- El mezclador corre en el hilo ejecutor del guest, dentro del observador de VBlank.
- **HECHO:** el juego llama a `sceGsResetGraph` con `omode 3` (PAL), y su bucle de sonido avanza 1 iteración por
  tick (1 por cada 2 en películas; AUDIO_002-0). El planificador sigue a 60 Hz: **no se cambió**.
- El mezclador interpreta cada tick como un field PAL: `48 000 / PS2X_AUDIO_FIELD_HZ` (50 por defecto) = **960
  frames por tick**, con resto acumulado para frecuencias que no dividen exacto (test: 47 Hz sin deriva en 10⁶
  fields).
- El field `T−1` se renderiza en `VBlankStart(T)`. En el WAV, la muestra del field `f` está en
  `(f − primer_field) × 960`.
- **HECHO:** el clip XAG del castillo dura 1 430,4 ticks (`XagStart` en 26757, fin en 28188): 1 261 568 muestras a
  44 098 Hz = 28,6 s a 50 ticks/s. Coincide con `clipTicksLocked`, la duración lógica que ya usaba TSNDDRVM.
- **INFERENCIA:** el VBlank emulado de PCSX2 para este juego es de 50 Hz (`FrameratePAL = 50`, `omode 3`). **No se
  midió en vivo** (§10).

## 3. WAV determinista frente a audio del dispositivo

Cada field produce dos mezclas:

| Mezcla | Fuentes | Reloj | Uso |
|---|---|---|---|
| WAV | todas las fuentes de guest | tiempo emulado (960 frames por tick) | referencia reproducible, regresiones |
| Dispositivo | solo los grupos en modo `guest` | tiempo emulado, vía el anillo SPSC | lo que se oye |

- El WAV no depende del host: es igual con o sin dispositivo, con GS síncrono o asíncrono y en cualquier modo
  (§6).
- El dispositivo depende de la velocidad del juego. Mientras el juego vaya más lento que tiempo real, una fuente en
  modo `guest` suena ralentizada o con cortes. Por eso el XAG va en `free` por defecto (§4).

## 4. Modos por grupo (`guest`, `free`, `off`)

Grupos: `Xag`, `Pss`, `Se`, `Bgm`. Hoy solo existe la fuente `Xag`.

| Modo | Dispositivo | WAV |
|---|---|---|
| `guest` | la fuente de guest, a ritmo del juego | sí |
| `free` (XAG por defecto) | un worker reproduce una **copia independiente** del clip en tiempo real, con su propio lector, remuestreador y fichero, por un flujo acotado (~0,5 s) | sí (la fuente de guest) |
| `off` | nada | sí |

- **HECHO:** no hay voz duplicada. En `free`, el anillo del dispositivo solo recibe silencio de guest y el flujo
  `free` entrega el clip completo (1 373 198 frames). En `guest`, el flujo `free` no se usa y no hay worker.
- `XagStop` retira la fuente y vacía el flujo; el worker se cancela.

Variables de entorno:

| Variable | Defecto | Efecto |
|---|---|---|
| `PS2X_AUDIO_MIXER` | `1` | `0`: ruta anterior; el mezclador no se crea |
| `PS2X_AUDIO_XAG_MODE` | `free` | `guest` u `off` |
| `PS2X_AUDIO_DEVICE` | `1` | `0`: ningún grupo va al dispositivo (pruebas sin audio) |
| `PS2X_AUDIO_WAV` | — | ruta del WAV (s16 estéreo, 48 kHz) |
| `PS2X_AUDIO_WAV_TICKS` | todo | `a:b`: solo los fields `[a, b)`; el fichero se cierra al llegar a `b` |
| `PS2X_AUDIO_FIELD_HZ` | `50` | frecuencia de field (24–120). Es general, pero el defecto corresponde a este ELF PAL |
| `PS2X_AUDIO_STATS_TICKS` | `500` | periodo de la línea `[AUDIO:mix]` |

## 5. Compatibilidad con la ruta anterior (`PS2X_AUDIO_MIXER=0`)

- La máquina de estados que ve el juego (`XagChkPlay`, `XagChkStandby`, duración lógica, payload de checkpoint v2)
  no cambia en ninguna ruta.
- **HECHO:** con `PS2X_AUDIO_MIXER=0`, el exe del parche 37 da en el castillo las mismas 15 líneas `[TSNDDRVM]` que
  la referencia (mismo MD5 `3f891068…`), igual que con el mezclador activo y con GS asíncrono.
- La ruta `streamPcm` queda intacta y se puede elegir sin recompilar.

## 6. Pruebas

### 6.1 Tests unitarios: 18/18 (HECHO)

`ps2xTest/src/audio_mixer_tests.cpp`, ejecutados 5 veces (18/18 cada vez):

- remuestreador: identidad a 48 kHz; longitud `ceil(N·2³²/step)` e independencia del tamaño de bloque (1, 7, 960,
  4096); seno de 1 kHz 44 098 → 48 000 Hz con SNR de 81,8 dB; saturación;
- lector XAG igual a decodificar bloque a bloque;
- reloj de 960 frames por field; 47 Hz sin deriva en 10⁶ fields;
- mezclador: suma y saturación, alta y baja de fuentes, modos, WAV idéntico entre corridas y con o sin
  dispositivo, cabecera tras cierre y tras terminación, rango de ticks, discontinuidad;
- anillo: desborde, subdesborde, recorte, vaciado; estrés con pérdidas (10⁷ frames, todas las rutas ejercidas,
  `consumidos + recortados + vaciados = aceptados`) y sin pérdidas (10⁷ frames contiguos, exactamente una vez y en
  orden);
- `DeviceGate`: 2 000 ciclos attach/detach con el callback ocupado, 0 usos tras `detach`;
- flujo `free`: rechazo sin dispositivo, cancelación inmediata con el anillo lleno, entrega completa, vaciado.

### 6.2 GS: 85/85 (HECHO)

- El parche no toca ningún archivo de `gs/` (0 líneas de diff).
- Bench de la promoción del parche 36 (`async_bench`): `tests` 23/23, `tests2` 11/11, `tests3` 19/19, `tests4`
  32/32.
- GS Replay `castle_t27100_n2`: síncrono = referencia (final `97605ff2a9703f79`; ZTE forzado
  `c81568efc6ca86a1`); asíncrono idéntico en todas las variantes; el control sin MXCSR diverge
  (`d92d8de2cef4ce75`), como se espera.

## 7. Fidelidad del WAV del castillo (HECHO)

Checkpoint `g002_castle_entry`, WAV de los fields `[26640, 28300)`:

- **WAV idénticos byte a byte** (SHA-256 `af7208bd…`) en 5 corridas: `free` ×2, GS asíncrono, `guest` y sin
  dispositivo.
- **Referencia independiente** (ADPCM y remuestreo reescritos en Python a partir de la especificación y la ISO
  local): los **1 373 198 frames** del clip `[312,+44)` (1 261 568 de entrada a 44 098 Hz) coinciden **bit a bit**.
  El clip empieza exactamente en el frame `(26757 − 26640) × 960`, con silencio exacto antes y después.
- **Coste:** render de 27,9 µs por field de media (máximo 1,87 ms, lectura de la ISO), frente a ~170 ms por tick
  del juego.

## 8. Checkpoints (HECHO)

- **Guardado en el tick 26800** con la voz sonando: referencia frente a parche 37, **12/12 secciones idénticas**
  (VRAM incluida), 49 751 307 bytes cada uno.
- **Restauración a mitad de clip:** WAV de 150 fields de silencio exacto, sin ningún `XagStart` tras restaurar. El
  juego ve `XagChkPlay` con estado 3 (lógica sin cambios), pero no hay audio antiguo ni se relanza la voz.
- **Checkpoint del menú:** carga correcta (ABI compatible), dispositivo abierto, película y entrada en marcha.

## 9. Rendimiento (HECHO)

Monitor del harness; «sysCPU» = CPU total del sistema (16 hilos lógicos), usada para detectar carga externa.

| Escena | Referencia | Parche 37 | Diferencia |
|---|---|---|---|
| Oficina, par limpio (150 s) | 6,49 VSync/s | 6,51 VSync/s | +0,3 % |
| Castillo (`c_ref` / `c_B`) | 285,2 s | 281,1 s | −1,4 % |
| Castillo, ruta anterior repetida (`c_legacy2`) | 285,2 s | 278,7 s | −2,3 % |

- Dos corridas iniciales se descartaron por **carga externa demostrada**, no por el parche:
  - `c_legacy`, +20,6 %: igual que la referencia durante ~180 s y después la sysCPU sube del 13–20 % al 51–75 %;
  - `o_ref1`: 68 % de sysCPU en los primeros 30 s.
  - Las repeticiones con el sistema tranquilo están dentro de ±2,5 %.
- **Memoria:** pico privado de 172–231 MB por proceso, igual que la referencia; sin crecimiento sin límite.

## 10. Reproducción del parche (HECHO)

- `upstream.lock.json` con 37 entradas: los 37 SHA-256 coinciden, todos se aplican en orden sobre `14b1e5cb`, y
  `commit-tree` con `patch_identity` da **`acacdf88cf4ea06b8dd50fd6f5face6974a46e38`** (árbol `7b86b7ab…`) en un
  clon local limpio.
- Los 36 parches anteriores no cambian (36 = `247af590`, MATCH).
- Los 12 archivos del parche en ese árbol son idénticos (salvo CRLF/LF) a los del vendor con el que se compiló el
  exe validado `756d4d1f`.
- **No se regenera el EE:** `ninja -n` lista 31 objetos de runtime e IOP y **0** de `generated`. Ninguna de las
  19 cabeceras que incluye el código generado cambia.

## 11. Limitaciones conocidas

### 11.1 El exe termina con `std::_Exit(0)` (anterior al parche)

- **HECHO:** `ps2xRuntime/src/main.cpp` llama a `std::_Exit(0)` cuando vuelve `runtime.run()`. Al cerrar la
  ventana no se ejecuta ningún destructor: ni `~PS2Runtime`, ni `PS2IopHostAdapter::shutdownAudio`, ni
  `~TsnddrvmXagService`. El sistema operativo termina los hilos.
- El cierre ordenado del parche (detach de la `DeviceGate`, `Unload`, join del worker) solo lo cubren los tests
  unitarios.
- **HECHO:** `WM_CLOSE` a mitad de clip, con el worker `free` todavía empujando audio: código 0 en 0,17–0,19 s, sin
  procesos residuales (2 corridas).
  - **Corrección:** una primera prueba de cierre llegó a t ≈ 60 s, cuando el worker ya había terminado (t ≈ 34 s),
    y no cubría la condición. Se repitió cerrando a t ≈ 15 s.

### 11.2 Cabecera del WAV al cerrar

- Con `_Exit` no se llama a `WavSink::close()`. La cabecera RIFF se reescribe cada 50 fields, así que puede quedar
  hasta 1 s por detrás de los datos (en la prueba, declara 150 de 186 fields).
- **HECHO:** los datos están completos (los 186 fields son idénticos byte a byte al tramo equivalente de otra
  corrida), y la cabecera nunca declara más de lo que hay.
- Si se necesita una cabecera exacta, cerrar con `PS2X_AUDIO_WAV_TICKS=a:b` (el fichero se cierra al llegar a
  `b`).

### 11.3 Medidas en vivo con PCSX2: no realizadas

- **HECHO:** el PCSX2 instalado abre PINE pero no los servidores GDB, aunque estén activados en `PCSX2.ini`. Sin
  ellos no se pueden cargar estados, avanzar fotogramas ni leer la memoria del IOP desde el MCP.
- **No se midió** el periodo de VBlank PAL ni la duración del XAG de la oficina en VSync. El reloj de 960 frames
  por tick se apoya en la configuración PAL del juego y en la duración lógica que ya usaba TSNDDRVM (§2), no en una
  medida de PCSX2.

### 11.4 Checkpoints v2

- El formato y el payload v2 de TSNDDRVM no cambian. Los checkpoints existentes cargan y los nuevos son idénticos a
  los de la referencia.
- Al restaurar, la voz que sonaba al guardar **no se relanza** (igual que en la ruta anterior). Con el mezclador,
  además, se detiene antes cualquier audio en curso. Reanudar la voz a mitad de clip queda fuera de este parche.

### 11.5 Fuera de alcance

- **Sin implementar:** audio de las películas PSS, efectos (SE, comandos `0x0X`/TQ), música (BGM, `0x2X`) y
  emulación de SPU2. Los grupos `Pss`, `Se` y `Bgm` existen en el mezclador (modo `guest`), pero no tienen fuentes.
- Los pasos de Dante siguen sin sonar: la causa está en el EE, antes de cualquier motor SE
  ([AUDIO_002_SDRGETSTATE_PORT_STATES.md](AUDIO_002_SDRGETSTATE_PORT_STATES.md)).
- No cambia el planificador (60 Hz) ni el formato de checkpoint.

## 12. Activación y reversión

- Activo por defecto. Ruta anterior sin recompilar: `PS2X_AUDIO_MIXER=0`.
- Reversión completa: quitar la entrada 37 del lock (`patched_commit` → `247af590`) y recompilar de forma
  incremental (solo runtime e IOP).
