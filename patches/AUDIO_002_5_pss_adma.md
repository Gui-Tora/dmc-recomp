# AUDIO_002_5_pss_adma.patch — parche 40 (experimental, opt-in)

**Estado:** entrada 40 de `upstream.lock.json`. Fase AUDIO_002-5; nota: `analysis/notes/AUDIO_002_5_PSS_ADMA.md`.

| Campo | Valor |
|---|---|
| Commit base | `a46af4f5` (39 parches) |
| `patched_commit` con 40 parches | `d21ad5075aea44d1a15704a88388ab691993c32f` (árbol `7e30ec98d726cead10386d5311be55f4d1bf6b89`) |
| SHA-256 | `a98ac5189ad45052bcece39033a1a3c3d21c33273dea90ab888b1a698a5a50a4` |
| Archivos | 9 (+1438/−20) |
| Exe validado | `d497a9a04fa36374cad0f436270a0cdb04521070f3ffe1d90c867eb7a564cbab` (build incremental desde `d21ad507`) |

**Revisión.** Los bytes de este archivo son los de la **revisión r2** del candidato (durante la fase,
`AUDIO_002_5_pss_adma_r2.patch`). El nombre público no lleva «r2». **No** es la revisión c3 (`b0a2fc31…`), que no se
publica: con solo `PS2X_SD_ADMA=1` activaba ADMA y la película se detenía en silencio, y aceptaba un alias con base 0
(ver la nota, §6). El `patched_commit` solo depende del árbol resultante, no del nombre del archivo; se ha reproducido
desde un clon limpio con el lock de 40 entradas.

## Causa

1. **Direcciones del heap IOP.**
   - `sceSifAllocIopHeap` devuelve direcciones `0x04xxxxxx`.
   - libsd informa de la posición de la transferencia en 24 bits, y el juego (`iopGetArea@0x47A320`) calcula
     `(status & 0xFFFFFF) − iopBuff`.
   - El resultado siempre es negativo: tras la precarga de 24 KiB no se envía ni un byte más al IOP.
2. **Reloj de `BlockTransStatus`.** El stub avanzaba 1024 bytes por consulta (128 frames/tick en películas), no según
   el tiempo (960 frames por field PAL).
3. **Callbacks de audio que devuelven 0.** El demux del runtime los descartaba; libmpeg detiene el demux y reintenta.
   Un paquete perdido desalinea L/R durante el resto de la película (demostrado con un candidato intermedio, c1).
4. **No había fuente PSS** en el mezclador del parche 37.

## Cambio

Todo es opcional. Sin las variables de entorno, el runtime se comporta como con 39 parches.

1. **`ps2xRuntime/src/lib/Kernel/Stubs/SIF.cpp`** (+76/−1). Con `PS2X_IOP_HEAP_ALIAS=base:limit`, alias de 24 bits
   del principio del heap SIF:
   - los bloques que caben enteros en la ventana se entregan como `base + desplazamiento`; el resto conserva la
     dirección canónica;
   - lectura, escritura, borrado, liberación y comprobación de rango traducen el alias;
   - `isSifIopHeapAddress` incluye toda la ventana: dentro de ella, SIF y el host IOP nunca llegan a la RDRAM del EE
     (o es un bloque del heap, o se rechaza);
   - almacén, claves del mapa y codec del checkpoint siguen siendo canónicos (`0x04xxxxxx`): **el formato no cambia**;
   - el parser exige `0 < base < limit ≤ 0x1000000`, alineación de 64 bytes, tamaño no mayor que el heap y nada tras
     el límite. Si no, ignora la variable con un aviso `[SIF:heap]`;
   - `sifIopHeapAliasActive()`: consulta pública.
2. **`ps2xRuntime/src/lib/audio/pss_audio.h`** (nuevo, +206; privado del runtime, no lo incluye el EE generado):
   - reloj de la ADMA: 48 000/`field_hz` frames por field, un bloque de 1024 bytes cada 256 frames;
   - `AdmaBlockSource`: fuente del mezclador. Lee cada bloque del búfer IOP al renderizarlo, desintercala 512 L /
     512 R s16 LE y aplica BVOL con signo (`x·v >> 15`, saturado);
   - `sdAdmaEnabled()`: verdadero solo con `PS2X_SD_ADMA=1` **y** un alias activo. Con `PS2X_SD_ADMA=1` sin alias
     válido imprime una vez `[AUDIO:adma] PS2X_SD_ADMA=1 ignored: it needs a valid PS2X_IOP_HEAP_ALIAS …` y se
     comporta como la configuración predeterminada;
   - puente con el host; declaración de `sdAdmaDebugState` (instantánea de solo lectura para tests y diagnóstico).
3. **`ps2xRuntime/src/lib/Kernel/Stubs/Audio.cpp`** (+127/−9), con ADMA efectivo:
   - la posición de `BlockTrans` es función del tick del guest desde el inicio; estado y STOP la devuelven con
     granularidad de bloque; bucle y bit de banco en la mitad alta;
   - el inicio crea una fuente `Pss`, que se retira con STOP, con un nuevo `BlockTrans`, con `sceSdRemoteInit` y al
     cargar un checkpoint;
   - `SetParam` `0x0F80`/`0x1080` (`SD_P_BVOLL/R`) fija el volumen de entrada de la ADMA;
   - un `BlockTrans` cuyo búfer no es una dirección de 24 bits imprime un aviso por núcleo (alias válido pero
     insuficiente: sin bloqueo silencioso);
   - el codec de la sección AUDIO no cambia: el reloj se rearranca en la primera llamada tras la carga.
4. **`ps2xRuntime/src/lib/Kernel/Stubs/MPEG.cpp`** (+24/−10), con ADMA efectivo: el PES de audio se retiene hasta que
   su callback lo acepta, como el de vídeo desde P4.2.3. Un retorno 0 hace que el demux devuelva los bytes confirmados
   y reintente, como libmpeg.
5. **`ps2xRuntime/src/lib/ps2_iop_host.cpp`** (+38): puente (`ps2audio::bridge`) hacia el mezclador del host.
   `ps2_runtime.h` no se toca.
6. **`ps2xTest`** (+967): `CMakeLists.txt` y `main.cpp` registran dos archivos nuevos:
   - `audio_pss_tests.cpp`: suite `AudioPssAdma`, 11 tests (formato de bloque, reloj, modelo del protocolo del juego,
     determinismo, subdesbordamiento, volumen, búfer ilegible, fin sin bucle, pausa/reanudación, cambio de película,
     cierre con transferencia activa);
   - `pss_sif_libsd_tests.cpp`: suites `PssGateSif` (9 tests) y `PssGateLibSd` (11 tests) sobre los stubs reales de
     SIF, DMA, host IOP, codecs de checkpoint, `sceSdRemote` y `sceSdRemoteInit`. Toman las expectativas del entorno
     del proceso y deben ejecutarse en las cuatro configuraciones.

No cambia ninguna cabecera incluida por el EE generado: el build incremental recompila 4 objetos del runtime y 0 de
`generated`. No toca GS, planificador, SE, BGM, SPU2 ni TSNDDRVM.

## Activación para DMC

```
PS2X_IOP_HEAP_ALIAS=0x00020000:0x00080000
PS2X_SD_ADMA=1
```

| Configuración | Comportamiento |
|---|---|
| ninguna | igual que con 39 parches |
| solo alias | heap IOP con direcciones de 24 bits; libsd con el contrato antiguo; sin audio PSS |
| solo ADMA | ADMA ignorado con aviso; igual que sin variables |
| las dos | audio PSS por el mezclador |

## Requisito de la ventana del alias

- La ventana es un rango numérico que también pertenece a la RDRAM del EE. Debe ser memoria que el programa nunca use
  para datos visibles por SIF o por el host IOP: dentro de ella, esas rutas trabajan sobre el heap IOP, nunca sobre la
  RDRAM.
- Para SLES_503.58:
  - el ELF solo carga en `0x00100000–0x009F8480` y `0x01F00000`;
  - en los 14 checkpoints inspeccionados (menú, castillo, oficina), toda la RDRAM `0x20000–0x100000` está a cero.
- Esto **no prueba** la seguridad en todo el juego. Si un dato llegara a la ventana, el síntoma sería un rechazo de
  SIF, no una corrupción silenciosa de la RDRAM.

## Tests

`ps2x_tests` cuenta un test por cada `tc.Run` (MiniTest no tiene secciones ni subcasos). Con 39 parches: 520. Este
parche añade 31 (11 + 9 + 11): **551**, todos ejecutados.

| Configuración | Pasan | Fallan | Fallos nuevos frente a 39 parches |
|---|---|---|---|
| ninguna | 543 | 8 | 0 (los 8 ya fallaban con 39 parches) |
| solo ADMA | 543 | 8 | 0 |
| solo alias | 522 | 29 | 21 tests de SIF de upstream |
| las dos | 520 | 31 | 21 de SIF + 2 de `sceSdRemote` de upstream |

- **SIF con el alias de DMC (21):** 20 tests de upstream usan `0x20000–0x3D100` como memoria temporal del EE; con la
  ventana desplazada a `0x00E00000:0x00E60000` pasan. El restante comprueba que el heap IOP queda fuera de la RDRAM,
  invariante que el alias rompe por diseño.
- **`sceSdRemote` con ADMA (2):** esperan el cursor antiguo (+1024 por consulta, sin runtime). Con ADMA la posición
  depende del tiempo emulado; el contrato nuevo lo cubren `PssGateLibSd` y `AudioPssAdma`.
- **La suite estándar de upstream debe ejecutarse sin el alias.** Las suites `Audio*`, `Xag*` y `Pss*` pasan 62/62
  en las cuatro configuraciones.
- Ejecución con la arena de DMC (`PS2X_RUNTIME_ARENA_BASE/LIMIT`) y `PS2X_AUDIO_DEVICE=0`; sin arena, 14 tests de
  scheduler terminan en `[Error]` por falta de memoria de guest, con o sin este parche.

## Validación en el juego

Ver `analysis/notes/AUDIO_002_5_PSS_ADMA.md` §4–§5. Resumen:
- sin variables, solo alias y solo ADMA: WAV de la intro idéntico al del exe de 39 parches; la película termina;
- con las dos: PCM idéntico a una referencia extraída de la ISO en los tramos comparados; WAV determinista; sin
  fuentes residuales; XAG de la oficina idéntico.

## Compatibilidad

- Formato de checkpoint sin cambios (secciones SIF y AUDIO).
- Un checkpoint guardado **con** el alias puede contener punteros del guest a la ventana; cargarlo sin la misma
  configuración los deja inaccesibles. Guardar y cargar con las mismas variables.
- Los checkpoints existentes (guardados sin alias) cargan con y sin las variables.

## Limitaciones

- **Salida audible con saltos:** el planificador corre a 60 Hz con contenido PAL de 50 Hz; el anillo del dispositivo
  recorta un 16,6 %. El WAV (reloj del guest) no se ve afectado. Lo corrige el planificador, no este parche.
- **Ganancia sin oráculo de hardware:** BVOL negativo (0xF77F, 0xEEF7) aplicado con signo; MVOL y la cadena SPU2
  (core 0 → core 1 → salida) no modelados; nivel PSS/XAG no verificado. El PCM sí coincide con la ISO.
- **Final de película:** el juego llama a `audioDecReset` al acabar el vídeo y se pierden ~0,25 s de cola.
- **Ritmo del demux:** la segunda película del menú arranca 2 ticks (una imagen) más tarde.
- **Cinemáticas PSS dentro del juego:** no probadas.
- **Alias:** solución local. La alternativa arquitectónica es un espacio de memoria IOP propio (upstream, PR #244).
- **Puente del mezclador:** apunta al último `PS2IopHostAdapter` construido (en el juego hay uno).
