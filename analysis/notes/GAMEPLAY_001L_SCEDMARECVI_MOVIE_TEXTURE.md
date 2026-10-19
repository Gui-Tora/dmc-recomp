# GAMEPLAY_001L — `sceDmaRecvI` / `Movie_texture`: diagnóstico y propuesta

**Contexto**

- Fecha: 2026-10-16.
- Rama local: `gameplay-001l`, creada desde `master` `b6f7934` (GAMEPLAY_001K fusionado).
- ELF: `SLES_503.58`, SHA-256 `d0753a6b…bab4`, CRC32 `77654AD2`, entry `0x00100008`.
- AUDIO_001 queda en pausa. Su nota se conserva en `analysis/local/audio_001/` (sha256 `0da3df3a…`, ver
  `README_PAUSED.txt`).
- Experimentos y salidas en `analysis/local/gameplay_001l/`: `ipu_probe/` (prueba con FFmpeg y oráculo IDEC en Python,
  `idec.py`), `ref/` (fuentes públicas de FFmpeg 7.1, solo para lectura) y `find_ipu_containers.py`.
- No se ha modificado el runtime.

## 1. Semántica original

**HECHO (desensamblado del ELF, `gameplay_001j/irx/movie_tex.dis.txt` y `disfunc.py`):**

**`sceDmaRecvI@0x102618`**

- Es la función de librería de Sony que lanza una transferencia DMA hacia memoria en modo intercalado.
- Espera `CHCR.STR=0`, escribe `MADR`/`QWC` y escribe `CHCR` con `MOD=interleave`, `DIR=0` y `STR=1`.
- El runtime la tiene como stub `TODO_NAMED` (`Kernel/Stubs/DMA.cpp:163`): lanza la excepción que termina el proceso.
- **Único llamador en el ELF:** `Movie_texture` (`0x3FEE2C`).

**`Movie_texture@0x3FE9F0`**, por fotograma:

1. Avanza el fotograma del slot `0x87D720 + slot·0x34` (`0x3FEA40–0x3FEC28`).
2. `sceIpuInit`. Hace tres cosas:
   - reinicia el IPU (`IPU_CTRL.RST`);
   - carga datos escribiendo directamente en la FIFO de entrada `0x10007010`;
   - emite `SETIQ` intra y no intra (`0x50000000`/`0x58000000`), `SETVQ` (`0x60000000`) y `SETTH` (`0x90000000`).
3. `Movie_set_toIPU_tag` construye una cadena DMA en `0x773440`, y se escribe `D4` (toIPU):
   - `TADR = 0x773440`;
   - `QWC = 0`;
   - `CHCR = 0x105`.
4. `IPU_CMD = 0x40000000` (**FDEC**): lee los 8 bits de flags del fotograma.
5. `IPU_CTRL = (flags & ~4) << 16`.
6. `IPU_CMD = 0x10000008 | (DTD << 24)` (**IDEC**):
   - `FB = 8`;
   - `QSC = 0`;
   - `OFM = 0`, es decir, salida RGB32.
7. `D_SQWC = ((width−16)·4/16) | (4 << 16)`. Es intercalado: 4 qwords de transferencia y el resto de la fila como salto.
8. Por cada macrobloque de 16×16:
   1. espera `D8.STR = 0`;
   2. `SetfromIPUDma(0x80000000, …)`: D3 fromIPU de 64 qwords al scratchpad, dirección 0;
   3. `D8_SADR = 0`;
   4. espera `D3.STR = 0`;
   5. `sceDmaRecvI(0x1000D000, dst, 0x40)`, con `dst = textura + fila·cols·1024 + col·64`.
9. Espera `D8`, `sceIpuSync`, opcionalmente `sceDmaSync` de VIF1/GIF (si `mode & 8`) y `Movie_loadimage`.

**Estado del runtime (HECHO, `ps2_memory.cpp:1161-1179` y `2206-2224`):**

- Los registros IPU solo se almacenan: no hay decodificador, y `IPU_CTRL` nunca aparece ocupado.
- Leer `CHCR` borra `STR`, así que las esperas terminan sin más.
- DMA toIPU/fromIPU: no tienen efecto.
- **Consecuencia:** `Movie_texture` recorre la secuencia sin efecto hasta `sceDmaRecvI`, que aborta.

## 2. Las dos rutas

| | Escenario A: Menú → Opciones | Escenario B: tras la intro |
|---|---|---|
| Llamador | `Option_load` → `Movie_task_exec2`; `Option_main` → `Movie_task_scheduler` → `Movie_texture` | `Game_loop` → `Movie_task_scheduler` → `Movie_texture` |
| Vídeo | recurso **`0x6a`**: contenedor IPU 512×448, 60 fotogramas, flags `0x20` | INFERENCIA: niebla **`0x118`** (`Mist_movie_read` → `CdFileRead(0x118, …, 0x81)`), contenedor IPU 128×128, 120 fotogramas, flags `0x20`; cargado en esa corrida. Otros llamadores posibles: `Background_movie_*` |
| Mecanismo | IDEC → fromIPU → scratchpad → `sceDmaRecvI` | el mismo; la variante `mode & 8` (`Movie_set_loadimage2`, `sceDmaSync` de VIF1/GIF) está **por confirmar** |

**HECHO:**

- Contenedor (ISO del usuario): `+0x04` tamaño total, `+0x08/+0x0A/+0x0C` ancho, alto y nº de fotogramas (u16),
  `+0x10` tabla `{offset, tamaño}`.
- Cada fotograma es un byte de flags seguido del bitstream IDEC, rellenado a múltiplos de 16 bytes.

**INFERENCIA:** ambas rutas requieren decodificación IPU real (IDEC). Ninguna usa el DMA para otra cosa. Que la ruta B
use la niebla y no otro vídeo es inferencia: la confirmará la corrida B.

## 3. Hipótesis refutada y causa real

**Hipótesis de GAMEPLAY_001J:** el borrador HLE (`analysis/local/gameplay_001j/provisional_ipu/`) suponía que el
decodificador IPU de FFmpeg (`AV_CODEC_ID_IPU`) decodifica estos fotogramas.

**Prueba offline** (`ipu_probe`, fotogramas reales de `0x6a`): FFmpeg 7.1 (el `avcodec-61` del build) devuelve
`AVERROR_INVALIDDATA` en **55 de 60** fotogramas.

**HIPÓTESIS intermedia** (*stuffing* o escape de macrobloque no soportados): **refutada.** Mi oráculo `idec.py`
analiza los 60 fotogramas de `0x6a` y los 120 de `0x118` completos, con 0 *stuffing* y 0 escapes.

**Causa real (HECHO, `mpeg12dec.c` de FFmpeg):**

- `ipu_decode_frame` termina con `align_get_bits; if (get_bits_left(gb) != 32) return AVERROR_INVALIDDATA`.
- Exige el formato de paquete del contenedor «ipum».
- En DMC, tras el último macrobloque quedan entre 32 y 159 bits de relleno.
- Los 5 fotogramas que FFmpeg acepta (23, 33, 36, 42 y 47) son exactamente los que tienen entre 32 y 39 bits sobrantes.

**Estado actualizado:**

- El bitstream es MPEG-2 intra estándar del IPU (tablas B.14/B.15, escape de MPEG-2) y está entendido por completo.
- El fotograma 0 de `0x6a` (Dante sobre degradado rojo) y el de `0x118` (niebla) se reconstruyen como imágenes
  plausibles.
- Los 60 fotogramas fijan `quantiser_scale_code` en el primer macrobloque, así que el `QSC = 0` de IDEC no afecta al
  resultado.
- La matriz que carga `sceIpuInit` por `SETIQ` está **pendiente de verificar**. El oráculo usa la intra por defecto de
  MPEG.

## 4. Propuesta: emulación genérica de IPU-IDEC y DMA SPR/IPU (parche de PS2Recomp)

**Por qué genérica y no un HLE de `Movie_texture`:**

- El código original del juego sigue ejecutándose sin cambios: avance de fotograma, slots, globales y bucles. No hay
  transcripción de lógica del juego.
- El decodificador IDEC hace falta en las dos opciones. La diferencia es una capa de registros y DMA (~300 líneas).
- Es reutilizable para cualquier juego de PS2Recomp que use IDEC.

**Componentes:**

1. **Dispositivo IPU** (fichero nuevo `ps2xRuntime/src/lib/ps2_ipu.cpp` + cabecera privada; no lo incluye el código
   generado):
   - registros CMD, CTRL, BP y TOP;
   - FIFO de entrada (escrituras en `0x10007010` y DMA toIPU con cadenas de tags);
   - FIFO de salida;
   - comandos `BCLR`, `FDEC`, `VDEC` (lectura de bits), `SETIQ`, `SETVQ`, `SETTH` e `IDEC`:
     - salida RGB32, y RGB16 si `OFM = 1`;
     - `QSC`, `DTD`, `SGN`; `DTE` (dither) solo si se usa;
     - CSC BT.601 del IPU con umbrales de alfa.
   - Ejecución síncrona: cada comando termina al escribirse, porque el juego espera con `sceIpuSync`.
   - Un comando o modo no implementado se informa como error explícito, sin resultado ficticio.
2. **DMA:**
   - D4 toIPU (cadena) alimenta la FIFO de entrada;
   - D3 fromIPU copia `QWC` qwords de la FIFO de salida a `MADR` (scratchpad si bit 31) y limpia `STR`;
   - `sceDmaRecvI` (stub, `DMA.cpp`) implementa la transferencia real de canal: fromSPR con intercalado según `D_SQWC`,
     avanzando `MADR`/`SADR`, con `STR` a 0 al terminar.
3. **Enganche** en `ps2_memory.cpp`: lectura y escritura de `0x10002000–0x10002030`, de las FIFO `0x10007000/7010` y de
   `CHCR` de D3/D4. **Sin cambios en `ps2_memory.h`**; el estado vive en `ps2_ipu.cpp`.
4. **Checkpoints:** sin cambios de ABI. INFERENCIA: el estado IPU no persiste entre llamadas, porque `Movie_texture` lo
   reinicializa con `sceIpuInit` y termina dentro de la misma llamada.

**Archivos:**

- vendor:
  - `ps2xRuntime/src/lib/ps2_ipu.cpp` y su cabecera privada, nuevos;
  - `ps2_memory.cpp`;
  - `Kernel/Stubs/DMA.cpp`;
  - `ps2xRuntime/CMakeLists.txt`, si el fichero nuevo no entra por un glob existente; hay que comprobarlo para evitar
    reconfigurar.
- `ps2xTest/src/ps2_memory_tests.cpp` o un fichero de tests nuevo.
- `patches/GAMEPLAY_001L_ipu_idec_spr_dma.patch` y `upstream.lock.json` (parche 20).

Nada específico de DMC en el vendor.

## 5. Validación propuesta

**Offline, sin juego:**

- IDEC en C++ frente al oráculo Python en los 180 fotogramas (`0x6a` + `0x118`): Y/Cb/Cr y RGB idénticos.
- Frente a FFmpeg, pasándole el tamaño exacto que espera (calculado con el analizador): diferencia ≤ 1 por píxel.
  FFmpeg sirve como comprobación independiente de VLC, decuantificación e IDCT.

**Tests unitarios:**

- intercalado de `sceDmaRecvI` (TQWC/SQWC);
- D3/D4 con cadena de tags;
- `FDEC`/`IDEC`: número de macrobloques y bits consumidos;
- `SETIQ` por la FIFO;
- comandos no soportados → error explícito.

**En el juego, escenario A:**

- Desde `dmc_checkpoint_mainmenu_abi2_tsnd_v1.dmck` → Opciones, ~1–2 min.
- Sin el aborto en `sceDmaRecvI`, con el fondo animado de Dante visible (captura).
- Navegar y salir de Opciones; el juego sigue.

**Regresión:**

- La película de menú/atracción `0x234` sigue reproduciéndose (la ruta MPEG también llama a `sceIpuInit`).
- Native VU1 y Verify sin cambios.

**Escenario B (después):**

- NEW GAME → `0x232` → oficina → `0x231` → escena siguiente.
- ~12–15 min con VU1 nativo (la oficina dura ~8 800 VSyncs a ~12,5 VSync/s). Requiere autorización.

**PCSX2 (opcional):** volcado por PINE de la textura de destino tras `Movie_texture` en Opciones, frente a la nuestra.

## 6. Riesgos

- **Interferencia con la ruta MPEG:** las escrituras IPU de `sceIpuInit` pasarán a ejecutarse de verdad, y esa ruta es
  HLE de librería. Mitigación: regresión con la película `0x234`.
- **Detalles del CSC/IDCT del hardware** (redondeo, umbrales de alfa): afectan solo al color, no al avance.
- **La variante `mode & 8`** de la ruta B está sin confirmar.
- **El siguiente bloqueo después de `sceDmaRecvI`** es UNKNOWN.

## 7. Implementación (parche 20, aprobada)

**Correcciones de estado respecto a las secciones anteriores** (no se reescriben; se precisan aquí):

- **Matriz de `SETIQ`: verificada.**
  - HECHO: la `sceIpuInit@0x10B868` del ELF escribe en la FIFO 8 qwords desde `0x4FC860`: `+0x00..+0x30`, y luego
    `+0x40` cuatro veces.
  - Son la matriz intra por defecto en orden zigzag (`8,16,16,19,…,83`) y una no-intra plana (16).
  - Después escribe 2 qwords de VQCLUT desde `0x4FC8B0`.
- **Terminación de IDEC.**
  - HECHO: todos los fotogramas de `0x6a` y `0x118` terminan con `00 00 01 B0`.
  - El IDEC genérico termina cuando, en la posición de `macroblock_address_increment`, hay bits a cero hasta el byte
    y después `0x000001`.
- **Stub upstream de `sceIpuInit` (HECHO).**
  - DMC enlaza `sceIpuInit` y `sceIpuSync` como stubs (`recomp/symtabfirst.toml`).
  - El código generado los llama de forma directa (`ps2_stubs::sceIpuInit`), así que un `registerFunction` por
    dirección no los intercepta.
  - El stub copia las tablas desde direcciones fijas de otro juego: `0x1721E0`/`0x172230`. En este ELF caen dentro del
    código de `Homing2`.
  - Su helper `0x126428` cae en mitad de `Em10_R1_catch_front`; no está registrado, así que no se invocaba.
  - Mientras el IPU no ejecutaba nada esto era inocuo. Con un IPU real habría cargado instrucciones MIPS como matriz.
  - DMC tiene `setD4_CHCR@0x10B800` (registrado). Su cuerpo, verificado: DI, `D_ENABLEW |= 0x10000`,
    `D4_CHCR = a0`, `D_ENABLEW &= ~0x10000`, EI.

**Alcance del parche** (`patches/GAMEPLAY_001L_ipu_idec_spr_dma.patch`, SHA-256 `12a23df1…0b5c`; lock
`patched_commit` `054bbc84…49a3`, 20 parches; el replay en un directorio limpio coincide con el lock y con el
vendor):

- **`ps2xRuntime/src/lib/ipu/ps2_ipu.{h,cpp}`** (nuevo, cabecera privada; no lo incluye el código generado).
  - Decodificador IDEC puro (`decodeIdec`), portado del oráculo `idec_ref.py`:
    - VLC por tablas de búsqueda;
    - IDCT MSSG en int64;
    - CSC del IPU, alfa `0x80`;
    - salida de 1024 B por macrobloque.
  - Dispositivo (`Device`):
    - FIFO de entrada (escrituras `sq` y D4) y de salida;
    - comandos `BCLR`, `IDEC`, `FDEC`, `SETIQ`, `SETVQ`, `SETTH`; `IPU_CTRL.RST` (las matrices sobreviven);
    - lectura de `CMD`/`CTRL`/`BP`/`TOP`.
  - Canales:
    - D4 toIPU: normal y cadena `refe`/`cnt`/`next`/`ref`/`refs`/`end`. Los bloques se leen solo cuando un comando
      necesita datos, así que una cadena cuyo siguiente tag no hace falta queda activa.
    - D3 fromIPU: normal; si no hay salida, queda activo.
    - D8 fromSPR: normal e intercalado con `D_SQWC`; `MADR` avanza `(bloque + SQWC)·16` tras cada bloque, como
      `_SPR0interleave` de PCSX2.
  - No soportado (error explícito por `Host::error`, `IPU_CTRL.ECD`, sin salida):
    - `BDEC`, `VDEC`, `CSC`, `PACK`;
    - IDEC con `OFM`, `DTD`, `SGN`, `DTE`, `AS`, `QST`, `MP1` o umbrales `SETTH` distintos de 0;
    - D3 y D8 en modo cadena;
    - tags `call`/`ret`;
    - `TTE` en toIPU;
    - lecturas de la FIFO por la CPU.
  - Ejecución síncrona: `BUSY` solo queda activo si un comando necesita entrada que todavía no existe. Se reanuda al
    escribir en la FIFO o al arrancar D4.
- **`ps2_memory.cpp`.**
  - Registros `0x10002000-0x1000203F`, incluidas las lecturas de 64 bits con `BUSY` en el bit 63.
  - FIFO de entrada por `write128` a `0x10007010`.
  - `CHCR` de D3/D4/D8. El estado vive en un mapa por `PS2Memory` dentro del `.cpp`: `ps2_memory.h` no cambia.
  - Las finalizaciones activan `D_STAT.CIS3/4/8` y encolan la causa DMAC, igual que GIF/VIF.
- **`Kernel/Stubs/DMA.cpp`, `sceDmaRecvI`.**
  - Reproduce las escrituras de libdma: `MADR` salvo que lea `0xFFFFFFFF`, `QWC`, y `CHCR = (CHCR & ~0xD) | 8 | 0x100`.
  - Devuelve ese `CHCR`. Solo fromSPR; otro canal → error y `-1`.
- **`Kernel/Stubs/IPU.cpp`.**
  - `sceIpuSync`: modo 1 devuelve `BUSY`; modo 0 informa si sigue ocupado.
  - `sceIpuInit`: direcciones configurables con `ps2_ipu::setLibIpuInitLayout`; por defecto, las de siempre.
- **Tests (`ps2xTest/src/ps2_ipu_tests.cpp`, 9 tests).**
  - Dos expectativas de `ps2_runtime_expansion_tests.cpp` pasan de `IPU_CTRL = 0x40000000` a `0`: `RST` se borra solo
    y ya no se lee de vuelta.
  - Consecuencia: el test «IPU initialization should finish only after the guest invocation completes» pierde ese
    discriminante. Siguen comprobando el orden el recuento del helper y el resultado de la reanudación.

**Ajustes DMC** (fuera del vendor; ELF `SLES_503.58`, SHA-256 `d0753a6b…bab4`, entry `0x00100008`, CRC32
`77654AD2`):

- `runtime/dmc_overrides.cpp` registra `dmc-sles-503.58-libipu-init-layout`:
  - `setD4ChcrEntry = 0x10B800`;
  - `iqTable = 0x4FC860`;
  - `vqTable = 0x4FC8B0`.
- `CMakeLists.txt` añade a ese fichero el include privado `ps2xRuntime/src/lib` (propiedad por fuente; las flags del
  código generado no cambian).

**Builds:**

- Release incremental: `ps2_memory.cpp`, `ps2_ipu.cpp`, `DMA.cpp`, `IPU.cpp` y `dmc_overrides.cpp`, más el enlace.
  2,8 min, 0 errores, 0 unidades generadas.
- La reconfiguración de CMake (fuente nueva) no recompiló el código generado.

## 8. Validación

**Etapa A, offline** (`analysis/local/gameplay_001l/idec_cpp/`; compila `ps2_ipu.cpp` del vendor de forma
independiente):

- Pasada 1, `decodeIdec`: idéntico al oráculo en 60/60 fotogramas de `0x6a` y 120/120 de `0x118`. Se comparan los
  bytes RGBA, el número de macrobloques y el bit final.
- Pasada 2, dispositivo con DMA: la secuencia completa de `Movie_texture` sobre memoria simulada.
  - `sceIpuInit` con los bytes reales de las tablas del ELF;
  - tag `ref` en D4, `FDEC`, `IPU_CTRL`, `IDEC`;
  - por macrobloque: D3 al scratchpad y D8 intercalado a la textura.
  - Resultado: textura idéntica a la referencia en ráster en los 180 fotogramas. `SADR`, `MADR`, `QWC`, `STR` y las
    finalizaciones son las esperadas.
- Decodificar los 180 fotogramas cuesta 0,22 s.
- Control negativo: perturbar una entrada de la matriz (`--mutate`) hace fallar 138 fotogramas.

**Etapa B, `ps2x_tests` Release** (árbol de pruebas local):

- 441 tests, 25 fallos, frente a la base de 19 parches compilada en el mismo árbol: 432 tests, 25 fallos.
- El conjunto de fallos preexistentes (arena, GS, etc.) es idéntico por nombre; no hay fallos nuevos.
- Los 9 tests `PS2Ipu` pasan:
  - intercalado fromSPR;
  - modo normal y wrap de `SADR`;
  - `sceDmaRecvI` (más `MADR = -1` y canal no soportado);
  - FIFO, `SETIQ` y `FDEC` con `BUSY` hasta que llegan datos;
  - cadena `ref` toIPU, orden de macrobloques y D3 al scratchpad;
  - `refe` completa D4;
  - textura de 32×16 por `sceDmaRecvI`;
  - errores explícitos;
  - matrices tras `RST`.
- Corrección propia: la primera versión del test de matrices usaba `QSC = 1`. El coeficiente decuantizado (2) no
  altera ningún píxel tras la IDCT, así que la premisa era falsa. Pasó a `QSC = 16`.

**Etapa D, en el juego** (Release `2d9948cf…`, `PS2X_VU1_NATIVE=native`; checkpoint
`dmc_checkpoint_mainmenu_abi2_tsnd_v1.dmck`; mando por `DMC_P311_PAD_FILE`; sonda temporal
`DMC_G001L_MOVIE_PROBE=1`). Corridas `run_d1` (247 s) y `run_d2` (229 s):

- HECHO: sin aborto en `sceDmaRecvI`. Menú → OPTION abre la pantalla Option sobre el fondo rojo con Dante.
- HECHO: la navegación (cursor hasta EXIT) y la salida al menú principal funcionan.
- HECHO, la textura en RAM (`0x79D680`, slot 0, contenedor 512×448, n=60, cargado desde `0x6a` en `0x1EE1780`) es
  bit-exacta:
  - `run_d2`, fotogramas 1, 3, 5, 7 y 9: idénticos a la referencia.
  - Fotogramas 13–58 (20 muestras): idénticos a la referencia desplazada un macrobloque (MB1…MB895, MB895), justo lo
    que predice el mecanismo descrito abajo.
  - El índice avanza de 1 a 58 en unos 2,5 s y se detiene en el último fotograma (lógica original del slot, flags
    `0x1`/`0x3`).
- HECHO, divergencia: el fotograma 11 dispara `[IPU] IDEC bitstream error: invalid DCT coefficient VLC at bit
  112870`, dos veces y de forma reproducible en ambas corridas. Ver el bloqueo en la sección 9.

**Etapa E, regresión MPEG/PSS** (`run_d1`):

- Antes y después de pasar por Opciones, las películas 0x234 (atracción, 51 s), 0x233 (36 s) y 0x22E (título, 16 s)
  se reproducen completas.
- Capturas de 0x234 en dos instantes distintos.
- Sin errores IPU fuera de Opciones y sin errores de VU1 nativo.
- INFERENCIA: la ruta MPEG pasa por `sceIpuInit` (`MpegMovieDecodeInit` la llama en el código generado), así que el
  layout DMC con su invocación de `setD4_CHCR` no interfiere con el HLE MPEG.

## 9. Bloqueo nuevo: el HLE de `SifCallRpc` escribe en RAM del EE encima del contenedor

**HECHO:**

- **Comparación del contenedor.** Volcado de la sonda en `run_d2` (`dumps/cont_0.bin`, único cambio del contenedor
  durante la reproducción) frente a la ISO: difieren 32 bytes, en `0x1F1008C-0x1F100B7`.
- **Por qué falla el fotograma 11.** Esos bytes caen dentro del fotograma 11, en su byte relativo `0x371C` (bit
  112864); el error salta en el bit 112870.
- **Qué se escribió.** Los valores encajan campo a campo con un `t_SifRpcServerData` con base `0x1F10080`:

  | Campo | Valor |
  |---|---|
  | `size` | `0x10` |
  | `client` | `0x9E5490` (`GetStClientData`) |
  | `pkt_addr` | `0` |
  | `rpc_number` | `2` |
  | `recvbuf` | `0x9E5500` (`getbuff`) |
  | `rsize` | `0x10` |
  | `rmode` | `1` |
  | `rid` | `0` |

- **Quién lo escribe.** `SdrGetStateSend@0x3CD2E0` llama `sceSifCallRpc(&GetStClientData, rpc, 1 /*nowait*/,
  getbuff, 0x10, getbuff, 0x10, 0x3CD2D0, …)`. El HLE `SifCallRpc` (`Kernel/Syscalls/RPC.cpp`) escribe
  `client/pkt_addr/rpc_number/size/recvbuf/rsize/rmode/rid` en `getMemPtr(rdram, client->server)`.

**INFERENCIA:**

- `GetStClientData.server` vale `0x1F10080`; la sonda no lo leyó directamente.
- En una PS2 ese puntero es del IOP y el EE nunca escribe en él. El HLE trata `client->server` como dirección del EE y
  escribe en memoria del juego.
- Opciones carga `0x6a` en `0x1EE1780-0x1FD2E04`, que contiene `0x1F10080`. Cada `SdrGetStateSend` posterior a la
  carga corrompe esos 32 bytes del fotograma 11.
- No depende del parche 20: el HLE RPC escribe igual. Antes no se veía porque Opciones abortaba en `sceDmaRecvI`.

**Consecuencia observada (HECHO, por los hashes de la sonda):**

1. El IDEC del fotograma 11 termina con `ECD` y sin salida.
2. El D3 que el juego había armado queda activo con 64 qwords; el bucle del juego sigue porque la lectura de `CHCR`
   limpia `STR`.
3. El D3 consume el primer macrobloque del IDEC siguiente.
4. Desde ahí, cada fotograma queda desplazado un macrobloque (16 píxeles) y repite el último.

En hardware real, un D3 sin datos haría esperar indefinidamente al bucle del juego; aquí lo oculta la lectura de
`CHCR`, que es comportamiento preexistente del runtime.

**Estado:**

- Los criterios 1, 4 y 5 se cumplen. El 2 se cumple: 0x6A se decodifica de verdad y de forma bit-exacta cuando la
  entrada está intacta.
- El 3 y el 6 no se cumplen del todo: el fondo animado aparece desplazado desde el fotograma 11, con dos errores IDEC
  causados por la corrupción.
- No se ha corregido: es otro subsistema (SIF RPC/sonido) y requiere aprobación.

**Condición para cerrarlo:** que el HLE de `SifCallRpc` no escriba `t_SifRpcServerData` en RAM del EE cuando el
servidor es del IOP. Verificación: el contenedor se mantiene idéntico a la ISO y desaparecen los errores IDEC.

## 10. Instrumentación temporal activa

**`runtime/g001l_movie_probe.inc`:**

- La incluye solo `dmc_overrides.cpp` y se activa con `DMC_G001L_MOVIE_PROBE=1` (`…_DIR` para volcados).
- Es de solo lectura:
  - muestrea cada 100 ms los slots de `Movie_texture` (`0x87D720 + n·0x34`);
  - registra el hash FNV-1a de la textura y el del contenedor;
  - vuelca el contenedor cuando cambia (máximo 24 veces).
- Prueba que la textura en RAM es el resultado real del IDEC (comparación con `probe_check.py` y `shift_check.py`) y
  localizó la corrupción.
- Retirar al cerrar el bloqueo de la sección 9: quitar el `#include` y la llamada, y recompilar `dmc_overrides.cpp`.
  **Retirada en la sección 11.**

## 11. Cierre del bloqueo de la sección 9: servidores RPC del IOP fuera de la RAM del EE (parche 21)

**Causa confirmada.** Se resuelven las INFERENCIAS de la sección 9.

HECHO, con instrumentación temporal (ya retirada) en `RPC.cpp` y en la sonda:

- **Arranque en frío corto** (`run_r1_cold`, 46 s, sin llegar a la introducción). `SifBindRpc` vincula cuatro SID sin
  servidor EE registrado y les asigna servidores ficticios de `rpcAllocServerAddr`, del pool `0x01F10000` en RDRAM:

  | SID | Servidor ficticio | Cliente |
  |---|---|---|
  | 0 | `0x1F10000` | `0x9E54C0` |
  | 1 | `0x1F10080` | `0x9E5490` (`GetStClientData`) |
  | `0x12345678` | `0x1F10100` | `0x87DBF0` |
  | `0x87654321` | `0x1F10180` | `0x87DBC0` |

  DMC no llama a `SifRegisterRpc`. Son servidores IOP atendidos por el HLE: el perfil de DMC solo tiene CDMODULE y
  TSNDDRVM, y TSNDDRVM atiende los SID 0/1.
- **El checkpoint del menú los restaura con esos `sd_ptr`.** La sonda lee `GetStClientData.server = 0x01F10080`
  directamente de la RAM.
- **`SdrGetStateSend`** (SID 1, `rpc=2`, NOWAIT, end function `0x3CD2D0`) llama `SifCallRpc` más de 500 veces en el
  menú. Cada llamada escribe `client/pkt_addr/rpc_number/size/recvbuf/rsize/rmode/rid` en `0x1F1008C–0x1F100B7`.
- **El contenedor 0x6A** (`0x1EE1780–0x1FD2E04`) es idéntico a la ISO justo tras cargarse (primer volcado). Después,
  esos 32 bytes son los únicos que cambian.
- **Otras escrituras en esa zona:**
  - la puesta a cero de 0x80 bytes y el `memset` del ficticio, al vincular durante el arranque, antes de cualquier
    carga;
  - las llamadas de los SID 0 y `0x12345678`, que escriben en `0x1F10000`/`0x1F10100`, dentro del rango del
    contenedor, aunque en esta corrida no después de la carga;
  - el pool de paquetes (`0x01F00000–0x01F10000`) solo lo usa `IopHost::allocateIopHandle`, y solo lo llama
    `cri_dtx`, que DMC no carga. `cd->hdr.pkt_addr` se pone a 0 al vincular.

**Corrección** (`patches/GAMEPLAY_001L_sif_rpc_iop_server_handles.patch`, SHA-256 `e29a54a5…e419`; lock con 21
parches, `patched_commit` `87b4725a…a030`; el replay limpio coincide con el lock y con el vendor):

- Un `SifBindRpc` a un SID sin servidor EE registrado crea la entrada con `hle_iop = true`.
  - El descriptor queda en memoria del runtime (`RpcServerState::hle`).
  - `cd->server` recibe un handle del espacio IOP: `0x1C1F0000 + n·0x80`, la ventana física de la RAM del IOP vista
    desde el EE, fuera de RDRAM.
  - No se escribe nada en RAM del EE salvo el propio `cd`.
- `SifCallRpc` resuelve el servidor solo a través del registro (por SID y, si no, por `cd->server`). Un `cd->server`
  que el registro no conoce ya no se desreferencia.
- Los servidores EE (`SifRegisterRpc`) no cambian: su descriptor sigue en RAM del EE y se actualiza en cada
  llamada. Se clasifican por cómo se registraron, no por la dirección.
- **Checkpoint sin cambio de formato.**
  - Una entrada antigua se migra a un handle IOP solo si su `sd_ptr` es exactamente un hueco ya asignado del pool:
    base + `slot·0x80`, con `slot < g_rpc_server_index` serializado. Ese conjunto es justo la salida del asignador
    antiguo; es la única información disponible en el formato ABI 2.
  - El cliente toma el handle nuevo en su siguiente `SifCallRpc`. Observado: `GetStClientData.server` pasa de
    `0x01F10080` a `0x1C1F0180`.

**Validación:**

- **Suite `ps2x_tests` Release, comparada por nombre:**
  - base de 19 parches: 432 tests, 25 fallos;
  - parche 20: 441 tests, 25 fallos;
  - parche 21: 444 tests, 25 fallos.
  - El conjunto de fallos es idéntico en las tres; no hay regresiones nuevas.
  - `PS2SifRpc` 16/16, `PS2SifDma` 15/15, `PS2IopSubsystem` 8/8 (incluye el modelo XAG de TSNDDRVM), `PS2Ipu` 9/9.
- **Tests nuevos:**
  1. Perfil DMC, cuatro SID de DMC y llamadas con la forma de `SdrGetStateSend` y de comandos.
     - Los handles están en el rango IOP y son distintos entre sí.
     - TSNDDRVM sigue respondiendo (`XagChkPlay → -1`).
     - Los datos del antiguo pool (`0x01F00000–0x01F20000`) quedan intactos.
     - Ningún byte de RDRAM cambia fuera del cliente, del buffer y del argumento de pila.
  2. Migración de un checkpoint antiguo.
     - No se escribe nada en `0x01F10080`.
     - El cliente toma el handle.
     - El fallback de un RPC no atendido sigue rellenando la recepción.
     - Un round trip conserva el handle.
  3. Un servidor EE registrado dentro del antiguo rango sigue actualizando su descriptor en RAM.
- **Corrección de un test propio:** la primera versión del test 1 exigía que un NOWAIT sin end function señalara su
  semáforo. Era una premisa falsa: esa señal depende del servicio (TSNDDRVM no la pide), no del servidor. En DMC la
  señal la da la end function. La semántica de NOWAIT, callbacks y semáforos la siguen cubriendo los tests
  LotR/RECVX/snddrv, que pasan.
- **Opciones (`run_v1`, sonda cada 100 ms; `run_v2`, cada 2 ms):**
  - contenedor idéntico a la ISO durante toda la reproducción (incluidos `0x1F1008C–0x1F100B7`);
  - 0 errores IDEC;
  - `run_v2`: 103 muestras. Los 60 fotogramas distintos aparecen bit-exactos en la textura, sin desplazamiento.
    Las 7 muestras sin coincidencia son lecturas a mitad de escritura: la siguiente muestra del mismo índice
    coincide con la referencia.
  - Navegación hasta EXIT y vuelta al menú.
  - Build final sin sonda (`run_final`): mismo comportamiento y 0 mensajes IPU.
- **MPEG/PSS:** tras Opciones, 0x234 se reproduce completa (49 s) y vuelve el título 0x22E.
- **XAG** (`run_xag`, la misma *smoke* de 3 min que `clean_smoke_release`: un Cross en NEW GAME):
  - 0x232 completa;
  - los marcadores TSNDDRVM/XAG son idénticos a la referencia (standby, `XagSetSkip3 40/272`, `XagStart`, cola de
    7 798 784 muestras completa, `XagStop`).
  - Única diferencia: `XagStart` empieza 20 VSyncs más tarde, con la misma duración (8843), por el instante del Cross.
- **F9 durante la *smoke*, a petición del usuario:** la petición no generó fichero, `size=0`:
  `[CKPT:REQUEST] FAILED tick=23820 … safe-point validation failed: GS vertex queue holds a half-submitted
  primitive`.

**Instrumentación:**

- Retirada: `runtime/g001l_movie_probe.inc` (copia en `analysis/local/gameplay_001l/retired/`), su include y su
  llamada.
- La instrumentación temporal de `RPC.cpp` nunca entró en el parche.

**Limitaciones restantes:**

- `rpcAllocPacketAddr`/`rpcAllocServerAddr` siguen escribiendo en `0x01F00000–0x01F20000` cuando un servicio llama
  a `IopHost::allocateIopHandle` (`cri_dtx`; DMC no lo usa).
- Los pools TLS (`0x01F20000`) y BootMode (`0x01F30000`) del runtime también están en RAM del EE. No se han
  auditado para DMC.
- Un servidor registrado por `SifRegisterRpc` en una dirección que coincida con un hueco del pool antiguo, en un
  checkpoint antiguo, se migraría como HLE. Es improbable y el runtime antiguo ya lo habría corrompido.

## 12. Corrida autorizada posterior a la oficina (`analysis/local/gameplay_001l/run_post_office/`)

**Condiciones:**

- Release `207a251f…` (parches 1–21, sin instrumentación), `PS2X_VU1_NATIVE=native`.
- Checkpoint `dmc_checkpoint_mainmenu_abi2_tsnd_v1.dmck`.
- Un Cross en NEW GAME y capturas cada ~18 s.
- 1201 s reales (límite de 20 min). Una sola corrida.

**HECHO:**

- **Cronología:**
  - t = 6,2 s: NEW GAME carga, entre otros, `0x118` (contenedor IPU de 128×128, en `0x1E00000`).
  - t = 6,5–58,7 s: 0x232.
  - t = 58,7–775,4 s: oficina 3D.
  - t = 775,4–784,5 s: 0x231.
  - t = 784,9 s: carga de `0x1D4` (mode 10) y de `0x2E`, `0x31`, `0x32` y `0x33`.
  - Después, escena 3D del castillo con bandas de cinemática y un segundo clip XAG (`0x1B8`, bloques `[312,+44)`,
    ticks 34041–35472, completo).
- **VSync:** de 20154 a 38933, es decir, 18 779 en 1201 s (15,6 VSync/s de media). El VSync avanza hasta el final.
- **Errores:** 0 mensajes `[IPU]`, `sceDmaRecvI`, `sceIpuSync`, out-of-bounds o fatales. Sin aborto en
  `sceDmaRecvI`. 2 RPC no atendidos (SID 1 `0x80000006`, SID `0x12345678` rpc 2), uno de cada.
- **Escena final:** desde el tick ~35700 la cámara queda fija sobre el castillo, pero la lógica sigue ejecutándose;
  el log muestra llamadas continuas a `dmcStub_Get_light_str` y `dmcStub_capVu0ecossin`, dos *stand-ins* `ret0` que
  siguen pendientes.
- **Entrada:**
  - START abre el menú de estado de juego: barra de vida, «Vital Star», orbes ×0.
  - START lo cierra.
  - LEFT y DOWN mantenidos 2,5 s no producen cambios visibles (diferencia media entre capturas ≈ 0,02). Dante no
    aparece en pantalla.

**INFERENCIA:**

- El antiguo bloqueo tras 0x231 (aborto en `sceDmaRecvI`) queda superado.
- El juego está en estado de juego: el menú de estado solo existe en gameplay.
- No se ha observado ni un personaje visible ni respuesta del mundo a las direcciones.

**Sin evidencia:** este build no tiene trazas de IDEC/DMA, porque no se permitieron modificaciones para preparar
la corrida. No consta si `Movie_texture` se ejecutó tras la oficina ni con qué recurso; `0x118` se cargó.

**HIPÓTESIS, no comprobada:** la escena inmóvil y la ausencia de respuesta a las direcciones guardan relación con
los *stand-ins* `ret0` aún activos (`capVu0ecossin` devuelve 0 en lugar de seno y coseno; `Get_light_str`), u otro
cálculo de cámara o personaje. Requiere su propia fase.

## 13. Personajes y geometría en la oficina: diagnóstico y consolidación (`analysis/local/gameplay_001l/visual_debug/`)

**Método:**

- Mismo checkpoint del menú, NEW GAME y capturas por tick de VSync, es decir, en estados equivalentes.
- Sonda de dibujo y volcados de RAM activados por variable de entorno (`runtime/g001l_*.inc`, temporales).
- *Savestates* de PCSX2 del usuario (*slots* 7 y 8, en la oficina), extraídos con `zstd` (`p2s_extract.py`).

**HECHO, carga:**

- Los recursos de la oficina están en PCSX2 en las mismas direcciones que en el runtime: los `mode=1` y seis de los ocho `mode=2` (estos con una cabecera de un sector), casi intactos.
- Los objetos de modelo (`md`) tienen las mismas direcciones, punteros y *flags* de dibujo en ambos.

**HECHO, primera pérdida: matrices NaN (`_capVu0ecossin`).**

- `_capVu0ecossin@0x1A03C0` usa una convención solo de registros: entrada en `vf6.x`; seno y coseno en `vf4`; además pone `vf5 = 0`.
- Sus llamadores en la oficina son `Motion_move_main`, `Motion_move_pl` y `Calc_make_matrix(_pl)`: el esqueleto de los personajes.
- Con el *stand-in*, la matriz de mundo del objeto de Dante (`md+0xE0..+0x11F`) era 16 NaN y su profundidad (`md+0x14`) `0x80000000`. `Md_draw_entry_db` lo descartaba.
- En PCSX2 esa matriz es válida: traslación `(9243, −3098, 2634)`.
- Con el cuerpo real, las matrices coinciden con PCSX2 hasta el tercer decimal, en rotación y en posición.

**HECHO, segunda pérdida: cadena DMA truncada.**

- `Md_draw_entry` envía la OT a VIF1 en modo cadena (`CHCR = 0x145`).
- La cadena real mide 9 248–16 620 tags por fotograma y siempre termina bien (sin ciclos, sin desborde de `CALL`, sin tags fuera de memoria).
- El runtime solo procesaba 4096 tags y descartaba el resto en silencio.
- Las llamadas a VU1 por VSync eran constantes (613) aunque los personajes estuvieran ya en la OT.

**HECHO, `Get_light_str@0x16D8E0`:**

- Función hoja: `max(0, 1 − |p − L|²·L.w)` en `$f0`. No escribe memoria.
- Corpus de 600 llamadas reales: **600/600 EXACT** frente a un modelo `float32` independiente (`light_model.py`), sin NaN y con resultados en [0; 0,985].
- El *stand-in* dejaba `$f0` residual, de 1·10⁻⁷ a 4·10⁻⁷, en lugar de la atenuación real (mediana 0,635 en las muestras iluminadas). En 212 de 600 muestras una luz con más del 10 % de aportación quedaba a cero.
- Con el cuerpo real se ven las puertas iluminadas y la moto con brillo, como en PCSX2.

**Correcciones consolidadas** (definitivas, sin variables de entorno):

1. **`_capVu0ecossin` y `Get_light_str` dejan de ser `ret0`.**
   - `recomp/symtabfirst.toml` pasa de 17 a 15 `ret0@`, y `runtime/dmc_overrides.cpp` deja de registrarlos.
   - Los cuerpos del árbol activo son idénticos a los que genera el TOML final (`gen_final15`); el de `ecossin` es además idéntico al auditado en 001I (600/600).
   - Diferencias conocidas con el generador, que no se tocan:
     - 11 ficheros de película y audio con diagnósticos `BLOCKER_004` añadidos a mano en una fase antigua;
     - `ps2_recompiled_stubs.h`, que conserva unas declaraciones de más inocuas. Sincronizarlo recompilaría todo el código generado.
2. **Parche 22 `GAMEPLAY_001L_dma_chain_walk_budget.patch`** (sección 22 de `patches/README.md`; lock con 22 parches, `patched_commit` `dfbb35f0…`; replay limpio = lock = vendor).
   - Recorrido completo de la cadena con detección exacta de ciclos: mismo tag con la misma pila `ASP/ASR0/ASR1`.
   - Presupuesto de seguridad de 2^18 tags, unas 16 veces el máximo medido.
   - Errores explícitos (`RUNTIME_ERROR`), sin encolar nada y con `STR` activo.

**Validación** (`joint1`, ejecutable `befc2862…`, correcciones por defecto):

- **Cadenas:** 0 errores `[DMA]`; todas terminan correctamente entre 9 269 y 16 620 tags.
- **VU1:** ~1 400 llamadas por VSync frente a 613.
- **Matrices:** 0 NaN en 11 modelos; profundidades coherentes con PCSX2 (`0x5C38D0`: 797/796; recursos 9102/9101…).
- **Capturas:**
  - la pared de trofeos (tick 24 000) coincide con la captura de PCSX2;
  - Trish entra en la moto atravesando la puerta (tick 24 300);
  - persisten polígonos negros grandes en el plano general;
  - Dante todavía no aparece en esta ventana corta (en PCSX2 se le ve más tarde).
- **`ps2x_tests`:** 447 tests y 25 fallos, el mismo conjunto preexistente. Los 3 tests DMA nuevos pasan.

**Rendimiento** (estadísticas del despachador VU1, ms de host por VSync):

| Fase | Total | VU1 | PATH1/GS | Resto |
|---|---|---|---|---|
| Antes (cadena truncada) | 82 (12,2 VSync/s) | 24 | 34 | ~25 |
| Después | ~295 (3,4 VSync/s) | ~176 | ~58 | ~60 |

El coste nuevo se concentra en VU1: ×2,3 llamadas, pero ×7 de tiempo, porque las de personaje son más pesadas. No se ha optimizado.

**Siluetas negras: diagnóstico inicial.**

- HECHO, código: `katVu0SetClip` carga dos matrices en `vf1–vf8`. `katVu0Clip_ld` transforma un punto y ejecuta `vclipw.xyz`, que desplaza 6 bits en el registro de *flags*. `katVu0Clip2_st` los lee con `cfc2 $vi18`.
- HECHO, código: `Scr_parts_near_trans` combina los *flags* de los 3 vértices (18 bits). Si ningún triángulo los activa (`fp == 0`), toma la vía rápida y no hace recorte cercano. Con el *stand-in* de `Clip_ld` los *flags* no se actualizan, y en la corrida larga hubo 0 llamadas a `ClipCheck2`, `InterpNode` y `RotTransPersQ`.
- HECHO, traductor: `vclipw` generado usa `bit0 = x < −w` y `bit1 = x > +w`; PCSX2 usa `bit0 = x > +|w|` y `bit1 = x < −|w|`. Son dos diferencias: orden de bits invertido y falta de `|w|`.
- HIPÓTESIS: los triángulos del escenario que cruzan el plano de cámara se proyectan sin recortar (`w ≤ 0`) y salen como polígonos negros gigantes.
- Para restaurar la rama hacen falta `SetClip`, `Clip_ld`, `ClipCheck2`, `ViewVolumeClip`, `RotTransPersQ`, `InterpNode` y `Calc_scr_light`. Varias dependen de los defectos `CLIP`, `DIV` y `FTOI` del traductor, que serían un parche general del generador con regeneración acotada.

**Instrumentación temporal activa** (activada por variable de entorno; retirar al cerrar las siluetas):

- `runtime/g001l_mem_dump.inc` (`DMC_VD_DUMP_*`).
- `runtime/g001l_draw_probe.inc` (`DMC_VD_DRAW_PROBE`): envuelve `Md_draw_entry_db`, `Mod_ot_set_tag` y `Md_draw_entry`.
- `runtime/g001l_light_probe.inc` (`DMC_VD_LIGHT_CORPUS`).
- Conmutador `DMC_RET0_NATIVE` en `dmc_overrides.cpp`.
- Cuerpos nativos de `katVu0SetClip` y `katVu0Clip_ld` disponibles en `visual_debug/gen_variant4/`, no instalados.

## 14. Recorte cercano: semántica VU0, restauración de las 7 funciones y A/B en la oficina (`analysis/local/gameplay_001l/clip_debug/`)

**Primer cálculo de recorte incorrecto (HECHO):**

- En la build por defecto, `katVu0Clip_ld@0x132DA0` es un `ret0`: no ejecuta su `vclipw.xyz`. `katVu0Clip2_st@0x132DC0`, que sí es nativa, lee con `cfc2 $vi18` flags residuales de otros `vclipw` (`katVu0Clip_shw*`).
- Sonda (brazo A): todos los vértices salen con los mismos bits, por ejemplo `−x −y +z`.
- En `Scr_parts_near_trans@0x132F40`, un triángulo entra en la rama de recorte solo si mezcla vértices con y sin flags. El código: `s4 & 0x3FFFF ≠ 0` y no todos los vértices marcados, luego byte del triángulo y `fp++`.
- Con flags residuales los tres vértices aparecen siempre «fuera», `fp = 0`, y la rama no se ejecuta nunca: `branch = 0` en todas las ventanas.
- Aunque se restaurase el cuerpo, el traductor emitía `VCLIPw` mal: orden de bits invertido (`−x` en el bit 0) y comparación con `w` en lugar de `|w|`. Además, `VDIV` daba `x/0 = 0` y `VFTOI` desbordaba a `0x80000000`. Referencia: PCSX2 `VUops.cpp` y `VUflags.cpp`, copias en `clip_debug/ref/`.

**Corrección.** Parche 23, `GAMEPLAY_001L_vu0_clip_div_ftoi_semantics.patch`, descrito en `patches/README.md`:

- Lock con 23 parches, `patched_commit 932ceb8a…`.
- Replay limpio idéntico al vendor (268 ficheros).
- Con el mismo TOML, el traductor corregido cambia exactamente 15 cuerpos (`gen_clip7_old` frente a `gen_clip7`):
  - 6 del recorte;
  - `atari_check_4(R)`, `capVu0Atan2`, `eff2d_draw_poly_3d`, `katVu0Clip_shw_tmc/wc`, `strip_draw_3d`, `strip_draw_polyline` y `strip_draw_simple_3d`.
- Los 9 últimos eran idénticos al árbol activo antes del cambio.
- Instalados sin edición manual 17 ficheros de `gen_clip7` (`clip7.toml`, que es `symtabfirst.toml` sin los 7 `ret0`), más `register_functions.cpp` (entrada interna `0x132E40` de `InterpNode`). Copia previa en `clip_debug/active_backup_p22/`.
- Build incremental: 18 unidades, exe `fc10efdf…`.

**Pruebas:**

- `ps2x_tests`: 448 tests y 25 fallos, el mismo conjunto preexistente. El test de emisión nuevo pasa.
- Arnés offline (`clip_debug/harness/`): compila los cuerpos generados tal cual y los compara bit a bit con un modelo transcrito de PCSX2. Para `CLIP` hay además un oráculo matemático (`|x| > |w|`). Cubre:
  - `w` positivo, negativo, `±0` y desnormal;
  - frontera `x = w`;
  - `−0`;
  - 20 000 casos aleatorios;
  - puntos delante y detrás de la cámara;
  - 3 000 triángulos (cruzan, delante, detrás);
  - `DIV` entre 0, `0/0` y `w` diminuto;
  - saturación `FTOI4/FTOI0`;
  - `ITOF/FTOI` con enteros grandes;
  - `Calc_scr_light` con valores extremos.

  | Árbol | Fallos | Triángulos con bit cercano `−z` (referencia 2000) |
  |---|---|---|
  | traductor previo | 52 390 | 0 |
  | corregido | 6 | 2000; flags acumulados idénticos en los 3000 |

  Los 6 fallos restantes se producen solo en `InterpNode` con `0/0` (los dos nodos sobre el plano). `Q = +max` es correcto, pero los `vmulx` siguientes dan `±Inf` donde PCSX2 satura a `±FLT_MAX` (`VU_MAC_UPDATE`). Es la falta general de saturación en `PS2_VMUL`/`PS2_VADD`, previa y fuera del parche 23. No se corrige.

**A/B corto en la oficina:**

- Mismo checkpoint, NEW GAME, capturas en los ticks de `joint1` y sonda `DMC_VD_CLIP_PROBE=23400,24700`.
  - A2: por defecto, los 7 `ret0`.
  - B4: `DMC_RET0_NATIVE` con las 7.
- A1 descartado: capturas en blanco por fallo de `PrintWindow`.
- B1–B3 descartados: capturas tardías. `log.txt` se escribe con búfer cuando el log es pequeño. Corregido con el marcador `clip` en `stage_d_run.py` (líneas `[G001L:clip]` a `events.jsonl`).
- Desfase final de las capturas respecto al tick pedido: A2 +13…+53, B4 +5…+45.
- HECHO, B4 entra en la rama: 1 082–1 739 llamadas de `Scr_parts_near_trans` con recorte y 4 848–13 057 triángulos recortados por cada 60 ticks (≈80–220 por VSync). Domina el bit `−z`. Los recuentos son idénticos ventana a ventana en B1, B2 y B4 (determinista).
- HECHO, salud: 0 errores `[DMA]` en ambos brazos, sin excepciones; VU1 y GS siguen dibujando.
- HECHO, imagen (`clipAB_sheet_1.png`, `clipAB_sheet_2.png`):
  - escenario, trofeos, moto y Trish iguales en A y B;
  - **los grandes polígonos oscuros del primer plano (ticks 23600–23800) persisten en B prácticamente idénticos**;
  - en PCSX2 (`joint1_vs_pcsx2.png`) el plano general no los tiene;
  - la franja/banda verdosa en el letterbox superior aparece en ambos brazos (problema distinto, no investigado).
- HECHO, coste: A2 234 ms/VSync (4,27/s); B4 244 ms/VSync (4,10/s), +4 %. A2 incluye además la sobrecarga del log del stub instrumentado, 20 MB. No se ha optimizado.

**Hipótesis refutada:**

- Hipótesis (sección 13): las siluetas negras son triángulos que cruzan el plano cercano y se dibujan sin recortar porque la rama nunca se ejecuta.
- Evidencia que la apoyaba: rama con 0 llamadas en la corrida larga; defectos de `CLIP` en el traductor.
- Evidencia que la refuta: con la rama funcionando y validada bit a bit frente a PCSX2, los polígonos oscuros no cambian en ticks equivalentes.
- INFERENCIA: el recorte cercano ausente no es la causa (o no la principal) de las siluetas visibles en esa ventana.
- HIPÓTESIS abiertas, sin probar: geometría válida mal coloreada o iluminada (por ejemplo las aspas del ventilador del techo, oscuras y cercanas), un problema de textura/CLUT o de alfa, u otra función `ret0` restante en la ruta de dibujo (`Calc_md`, `capVu0MulMatrix_r`, `sceVu0SetRotTransPers`…).

**Estado:**

- Por defecto, los 7 siguen registrados como `ret0`. Los cuerpos nativos están instalados y se activan con `DMC_RET0_NATIVE`.
- `recomp/symtabfirst.toml` todavía lista los 7 `ret0@`. Si se aprueba restaurarlos, hay que quitarlos de ahí y de `dmc_overrides.cpp` a la vez.

**Instrumentación temporal** (retirar al cerrar las siluetas):

- `runtime/g001l_clip_probe.inc` (`DMC_VD_CLIP_PROBE`);
- marcador `clip` en `stage_d_run.py` (inocuo).

## 15. Siluetas negras: primera divergencia en `UNPACK V4-5` del VIF1 (`analysis/local/gameplay_001l/silhouette/`)

**Parche 23 (separado de esta sección):**

- Se conserva con sus pruebas.
- Los **6 casos discrepantes** del arnés persisten y no se declara equivalencia total:
  - son `InterpNode` con `0/0` (3 casos «ambos nodos sobre el plano» y 3 «nodo con `w=0`», para `a3 = 0, 1, 2`);
  - `Q = +0x7F7FFFFF` es correcto, pero los `vmulx` posteriores dan `±Inf` (`0x7F800000`/`0xFF800000`) donde PCSX2 satura a `±0x7F7FFFFF` (`VU_MAC_UPDATE`);
  - la causa es la falta general de saturación de `PS2_VMUL`/`PS2_VADD`, fuera del parche 23.
- Las 7 funciones de recorte siguen como experimento (`DMC_RET0_NATIVE`).

**Método:**

- Configuración por defecto (siluetas visibles), corridas cortas sin pasar de la oficina.
- Tap GS temporal: puntero nulo por defecto en `GS::vertexKick`, lógica en `runtime/g001l_gs_tap.inc`. Vuelca todas las primitivas con estado completo, más la VRAM al final de cada tick.
- Sonda de dibujo ampliada (`DMC_VD_SKIP_MD`, `DMC_VD_MD_DUMP`) para ocultar un modelo y volcar la RAM que consume el DMA.

**HECHO, reproducibilidad:**

- Las etiquetas de tick **no** son reproducibles entre corridas, pero los fotogramas sí. tap1 y tap2 son idénticos primitiva a primitiva con 8 ticks de desfase; ese es el desfase visto en las A/B de la sección 14.
- Por eso los fotogramas se nombran con una clave de contenido: FNV-1a de las primeras 256 primitivas.
- Fotograma de estudio: clave `482E69BE623BD3A8` (23 171 primitivas; búfer de dibujo 512×224, `fbp=56`). La cuña negra se ve en la VRAM (`fb56_23750.png`).

**HECHO, objeto:**

- Con el mismo fotograma (misma clave), ocultar solo `md 0x015476F0` (recurso `0x2E`) con `DMC_VD_SKIP_MD` elimina por completo la cuña (`skip_md_compare.png`). Detrás aparecen la batería y los trofeos, como en el estado 07 de PCSX2, un plano equivalente sin cuña (`pcsx2_states_78.png`).
- El modelo aporta 950 primitivas (`tbp 10240/10496/10752/9984`, `TEST 5010D/7010D`, sin alpha blending).
- Primitivas seguidas, por ejemplo `idx 22514/22545/22555–22557` (`tbp 10240`, `cbp 4684`): triángulos con un vértice en el punto de colapso (≈(58–66, 202–205), `q≈0,000224–0,000227`) y otros cerca de la cámara (`q≈0,0019`).
- Ganan el test de Z (`ZTST=GEQUAL`) y tapan la escena. Los 76 de `tbp 10752` son degenerados: los tres vértices están en el punto de colapso.

**HECHO, entrada a VU1:**

- Subcadena DMA del modelo: cabeza y cola en `md+0x64/+0x6C[buf]`, según `Md_draw_entry_db` → `Mod_ot_set_tag`.
- Contenido decodificado (`vifdec.py`):
  - paleta de 32 matrices (`UNPACK V4-32 num=128 addr=0`): sana, rotaciones ortonormales y traslaciones ≈(−4800, 300, 600);
  - cámara en VU 176–183;
  - por lote de 48 vértices: `V3-32` posición, `V3-32` normal, `V2-16` UV, `V4-8` color y **`V4-5` pesos**.
- Los datos `V4-5` son pesos de *skinning* de 5 bits que suman 31: (31,0,0) ×4331, (16,15,0), (25,6,0)…
- RAM frente a PCSX2 s07: 917 de 919 bloques REF del modelo son idénticos. Los 2 distintos son la cámara (`0x886380`) y la paleta (`0x15E5E20`), que se recalculan cada fotograma. Los 164 bloques `V4-5` son idénticos.

**HECHO, microprograma VU1** (imagen `a0dbee2aeac4dafa`, `MSCAL 0x1E0` = byte `0xF00`):

- `LQ.xyz vf31, 4(vi3)` y `ITOF0.xyz vf31` cargan y convierten los pesos.
- `MULi.xyz vf31, vf31, I` con `I = 0,004032258 = 1/248`.
- `ILW.w vi7, 4(vi3)` lee el bit A.
- `MULA/MADDA` con `vf31x/y/z` mezclan 3 matrices de hueso, incluida la traslación.
- Con `vi12 = 0` hay una vía sin mezcla.

**HECHO, primera divergencia:**

- `ps2_vif1_interpreter.cpp` (VIF1, `UNPACK V4-5`) escribe los campos sin desplazar: `x = d & 0x1F`, `y = (d>>5) & 0x1F`, `z = (d>>10) & 0x1F`, `w = d>>15`.
- PCSX2 (`Vif_Unpack.cpp` `UNPACK_V4_5`) y el hardware hacen `x = (d&0x1F)<<3`, `y = (d&0x3E0)>>2`, `z = (d&0x7C00)>>7`, `w = (d&0x8000)>>8`.
- La memoria de datos VU1 del estado 07 de PCSX2 contiene 82 qwords de pesos con suma 248 y `w ∈ {0, 0x80}`.
- Upstream `main` mantiene el mismo código.
- Prueba mínima (`proposed/v45_unpack_test.inc`): **falla** con el runtime actual en `x = 16<<3`, `y = 15<<3`, `x = 31<<3` y `w = A<<7`. Ejecutada en un árbol de tests aparte (449 tests, 26 fallos = 25 previos + este) y retirada del vendor.

**INFERENCIA (muy apoyada), mecanismo:**

- Con pesos ×1/8, la suma de pesos vale 31/248 = 0,125. Los vértices de la vía mezclada quedan en 0,125·(R·v + T) con `w = 1`, cerca del origen del mundo.
- Comprobación numérica con la matriz de pantalla del paquete (VU 180–183): proyectar 0,125·T de los huesos 0/8/15 da GS (1843–1859, 2140–2148) y `q = 0,000222–0,000229`. Restando el offset da (51–67, 204–212), que coincide con el punto de colapso observado. Con escala 1,0 caen fuera de pantalla con `q ≈ 0,0013`, como los vértices correctos.
- Los vértices de la vía rígida no se ven afectados. De ahí la mezcla de vértices bien colocados y colapsados, y las aristas largas que forman la cuña.
- Afecta a todo modelo con skinning que use estos pesos (probablemente también Dante y Trish). Sin verificar.

**Pendiente:**

- La causalidad completa (que con la corrección desaparezca la cuña) **no** se ha comprobado ejecutando: requiere aplicar la corrección.
- Corrección mínima propuesta, sin aplicar: `proposed/v45_unpack_fix.diff`, 4 líneas en el intérprete VIF1, como parche general nuevo con la prueba anterior.
- Aparte: la ruta `UNPACK` de VIF0 solo decodifica `vl = 0`. DMC no la usa en esta ruta y no se ha investigado.

**Instrumentación temporal** (desactivada por defecto, retirar al cerrar la fase):

- `runtime/g001l_gs_tap.inc` y el puntero `g_ps2xGsDrawTap` en `vendor/.../gs_frontend.cpp`, que se restaura desde `silhouette/vendor_backup/gs_frontend.cpp.p23`;
- `DMC_VD_SKIP_MD` y `DMC_VD_MD_DUMP` en `runtime/g001l_draw_probe.inc`.

**Herramientas:** `silhouette/run_tap.sh`, `parse_prims.py`, `vram.py` (PSMCT32) y `vifdec.py`.

## 16. Parche 24: `UNPACK V4-5` corregido; validación en la oficina (`analysis/local/gameplay_001l/silhouette/`)

**Corrección de la sección 15 (método):**

- Hipótesis anterior: «los fotogramas son idénticos entre corridas salvo un desfase de ticks», con una clave de fotograma = hash de las primeras 256 primitivas de un **tick**.
- Evidencia que la apoyaba: tap1 y tap2 eran idénticos con 8 ticks de desfase, y la clave `482E69BE…` coincidía en tap3, tap4 y tap5.
- Evidencia que la refuta:
  - un fotograma real son ≈46 300 primitivas, es decir **2 ticks**, empieza en el sprite de borrado (`type 6, fbp 56, TEST 0x30000`), y el corte entre ticks cambia de una corrida a otra;
  - segmentando por fotograma real, los fotogramas de corridas distintas **no** son idénticos: difieren en detalles, por ejemplo `s/t` del borrado, y el número de primitivas varía en decenas;
  - la clave de 256 primitivas solo coincidía al principio del fotograma.
- Estado actualizado:
  - la escena avanza de forma casi determinista, pero no bit a bit;
  - la comparación antes/después se hace con **métricas por fotograma real** en toda la ventana, más imágenes de VRAM en ticks equivalentes;
  - el experimento de ocultación de la sección 15 sigue siendo válido: la cuña desaparecía entera, mientras el resto de la imagen quedaba igual.
- Herramienta: `frames.py`.

**Implementación:**

- `patches/GAMEPLAY_001L_vif_v45_unpack_fix.patch`: 4 líneas en `ps2_vif1_interpreter.cpp` y 2 tests en `ps2_memory_tests.cpp`.
- Lock con 24 parches, `patched_commit 5a90ae79…`. Replay limpio: 268 ficheros idénticos al vendor.
- `ps2x_tests`: 450 tests y 25 fallos, **el mismo conjunto preexistente**; los dos tests V4-5 pasan, y ningún fallo es de VIF, UNPACK ni DMA.
  - Antes de la corrección, el test fallaba en `x = 16<<3`, `y = 15<<3`, `x = 31<<3` y `w = A<<7` (sección 15).

**Validación** (corridas cortas, configuración por defecto, sin pasar de la oficina): `v45_before` con el exe `9d209ea8` frente a `v45_after` con el exe `cee3ec75`, ambos con el tap GS.

| Métrica de `md 0x015476F0`, por fotograma (23744–23756) | Antes | Después |
|---|---|---|
| Primitivas del modelo | 950–954 | 473–478 (el modelo se sigue dibujando) |
| Vértices colapsados (`q < 0,0003`) | 1689–1692 de 2850–2862 | **0** de 1419–1434 |
| `q` mínimo | 0,000224 (w≈4460) | 0,00174 (w≈575) |
| Píxeles cubiertos por el modelo | 40 500–42 600 | 5 900–9 000 (crecen al acercarse) |
| Zona derecha casi negra (VRAM) | 0,48–0,50 | 0,12–0,24 |
| Pesos en la VU1 (suma, A) | (31, 0/1): 247/819/13 | (248, 0/0x80): 247/819/13; PCSX2 s07: (248, 0/0x80) |

- HECHO: la cuña diagonal desaparece y vuelven a verse la batería y los trofeos (`v45_before_after_office.png`).
- HECHO, escena de la moto (24300/24308, `v45_before_after_trish.png`): antes Trish era una púa blanca estirada; después tiene su forma, con mano, pelo y piernas sobre la moto, como en la captura de PCSX2 del usuario. La moto y el escenario no cambian.
- HECHO, salud: 0 errores `[DMA]`, ninguna excepción. Smoke del exe final `51e46e38` sin instrumentación: llega a 25313 sin errores. La banda verdosa del letterbox en la captura de pantalla está antes y después: es un defecto previo de la presentación, que no aparece en VRAM.
- HECHO, coste (líneas de sonda en `events.jsonl`): 23760–24250 de 180 a **170** ms/VSync; 24320–24600 de 306 a **285** ms/VSync. Es un 5–7 % menos (menos triángulos gigantes que rasterizar). No se ha optimizado nada.

**Lo que queda: figura oscura con forma correcta:**

- HECHO: después de la corrección, el mismo modelo aparece cerca de la cámara (borde derecho; en 23801, una forma de mano o brazo que cruza la pantalla), oscuro (RGB 14–42 sobre 128, modulando la textura).
- HECHO, el `V4-8` del lote no es color sino **índices de hueso** (`ILW vi12/vi13`). El color sale solo de la iluminación de VU1, con la tabla del bloque VU 184–191 (datos en `0x15E5DA0`): luces 1 y 2 con color 0, luz 3 (29,6; 48,2; 44,5), ambiente 15.
- HECHO: esa tabla es **casi idéntica a la de PCSX2 s07** (29,655; 48,189; 44,482; mismas direcciones). La iluminación oscura es la del juego original.
- HECHO: con la cámara y la paleta de s07, 0 de 32 huesos caen en pantalla. En nuestro fotograma, 6 de 32. Que la captura de s07 no muestre la figura es coherente con que el modelo esté fuera de cuadro en ese instante.
- INFERENCIA: no hay una divergencia identificada en la figura restante: posición (sin colapso) e iluminación coinciden con los datos de PCSX2.
- Pendiente: confirmarlo visualmente con un fotograma de PCSX2 en el que el modelo esté en cuadro, en el barrido de cámara justo antes de la entrada de Trish.

**Personajes con skinning:**

- La corrección afecta a todo modelo cuyos pesos lleguen por `V4-5` a este programa VU1.
- Observado: el modelo `0x2E` de la oficina y Trish en la moto.
- Dante no aparece en esta ventana corta y no se ha verificado; requiere la corrida larga no autorizada.

**Instrumentación:**

- Retiradas (copias en `analysis/local/gameplay_001l/retired/`):
  - `g001l_gs_tap.inc`, más el puntero en `gs_frontend.cpp`, restaurado desde la copia `.p23`; vendor = replay;
  - `g001l_draw_probe.inc`, `g001l_mem_dump.inc` y `g001l_light_probe.inc`.
- Se conservan, por el experimento abierto del recorte: `g001l_clip_probe.inc` y `DMC_RET0_NATIVE`. También el marcador `clip` de `stage_d_run.py`.
- Las capturas y volcados de la fase siguen en `silhouette/`.

**Asuntos separados que siguen abiertos:**

- Parche 23 con los 6 casos `InterpNode 0/0` (sección 15).
- Las 7 funciones de recorte, que siguen como experimento.

## 17. Cierre de GAMEPLAY_001L (2026-10-18)

**Estado:** DMC-Recomp sigue siendo **experimental y no jugable**; `FIRST_PLAYABLE_FRAME` no se ha conseguido.

**Cambios definitivos de la fase:**

- Parches generales 20–24 (`patches/README.md`, `upstream.lock.json`):
  - lock con 24 parches, `patched_commit 54fbaadaa1ce7fee2b5235b0685b93c1e22f6b30` (tras normalizar a LF el parche 20; antes `5a90ae79…`, sección 18);
  - replay limpio idéntico al vendor (268 ficheros); SHA-256 de los 24 parches verificados.
- `recomp/symtabfirst.toml`: 15 `ret0@` (`_capVu0ecossin` y `Get_light_str` restauradas).
- `runtime/dmc_overrides.cpp`: restauraciones, *override* de layout de libipu y conmutador de diagnóstico `DMC_RET0_NATIVE` (sin efecto si no se define).
- `CMakeLists.txt`: *include* privado de IPU solo para `dmc_overrides.cpp`.

**Auditoría del código generado:**

- Regenerando desde el TOML del repositorio, el árbol de trabajo difiere en 20 ficheros:
  - los 7 cuerpos experimentales de recorte (`clip7.toml`) y `register_functions.cpp`;
  - `ps2_recompiled_stubs.h` (declaraciones de más, inocuas);
  - 11 ficheros de película/audio con trazas añadidas a mano en BLOCKER_004, marcadas «not for commit». Solo escriben en `stderr`; ninguna escribe estado del guest.
- La build limpia usa el árbol regenerado, sin ninguna de esas diferencias.

**Validación del cierre** (directorios nuevos, sin reutilizar la build de trabajo):

- `ps2_recomp` compilado desde cero.
- Regeneración completa (10 129 ficheros): idéntica a la del `ps2_recomp` de trabajo (0 diferencias).
- Build Release completa desde cero: 10 191 unidades, **0 errores**, 122 min; exe `e13ccbbe0aef87be…`.
  - Configurada con `-DPS2X_ENABLE_FFMPEG=ON`, igual que la build de trabajo.
  - **HECHO:** con la opción por defecto (`OFF`) la build falla, porque `MPEG.cpp` llama a `drained()`, que no existe sin FFmpeg (parche 10, BLOCKER_004 p423/p424). Problema preexistente, no corregido en esta fase; documentado en `GAMEPLAY_CURRENT_STATUS.md` §11.
- `ps2x_tests` desde cero: 450 tests, 425 pasan, **25 fallos = el mismo conjunto preexistente**. Todos los tests de los parches 20–24 pasan.
- Prueba corta con el exe limpio (checkpoint del menú, NEW GAME, película `0x232`, oficina hasta 24420; 267 s; VU1 compilado):
  - 0 errores `[DMA]`, sin errores fatales, sin `[IPU]`, sin stubs no implementados;
  - un único RPC no atendido, el ya conocido `SID 1 0x80000006` (SDRDRV);
  - capturas: la oficina sin cuña negra (23753), la pared de trofeos (24053) y Trish con su forma en la moto (24353);
  - la franja del letterbox persiste;
  - oficina a **≈5,1 VSync/s**. Las cifras de 3–4 VSync/s de las secciones 13–16 se tomaron con instrumentación activa.
- La ruta IPU posterior a `0x231` no se repitió en el cierre: no se permiten corridas largas. Su validación es la corrida autorizada de la sección 12, más los tests del parche 20.

**Abierto al cerrar:**

- Franja horizontal de presentación: siguiente fase.
- Rendimiento: PERF_001.
- 7 funciones de recorte experimentales y 6 casos `InterpNode 0/0` (sección 15).
- 15 `ret0`.
- Figuras oscuras de primer plano sin comparar con PCSX2 (sección 16).
- Escena posterior a `0x231` sin personaje visible ni respuesta al movimiento (sección 12).
- Dante sin verificar.

**Instrumentación retirada**, con copias en `analysis/local/gameplay_001l/retired/` para recuperarla:

- `g001l_movie_probe.inc`, `g001l_mem_dump.inc`, `g001l_draw_probe.inc`, `g001l_light_probe.inc`, `g001l_clip_probe.inc`, `g001l_gs_tap.inc`;
- el puntero del tap en `gs_frontend.cpp`.

## 18. Normalización LF del parche 20 (empaquetado, sin cambio funcional)

**Problema (HECHO):**

- El parche 20 creaba `ps2xRuntime/src/lib/ipu/ps2_ipu.cpp` y `ps2xTest/src/ps2_ipu_tests.cpp` con CRLF: 1009 y 374 líneas dentro del propio parche.
- `.gitattributes` fija `patches/*.patch text eol=lf`, así que al versionarlo Git habría normalizado esas líneas. El SHA-256 del lock y el `patched_commit` del replay habrían dejado de coincidir con lo publicado.
- Los parches 21–24 se guardan tal cual y no tocan esos ficheros.

**Corrección (opción A, aprobada):**

- El parche 20 se regeneró sobre los parches 1–19, aplicando el parche anterior y pasando a LF solo esos dos ficheros.
- Diferencias con el anterior: únicamente los `\r` y las dos líneas `index` (hashes de blob). El resto, 2015 líneas, es idéntico. Copia del anterior en `analysis/local/gameplay_001l/closeout/p20_lf/`.
- Ni `.gitattributes` ni los parches 21–24 cambian.

| | Antes | Después |
|---|---|---|
| SHA-256 del parche 20 | `12a23df187059d84…` | `0100aa7827b02056…` |
| `patched_commit` (24 parches) | `5a90ae79317461ce…` | `54fbaadaa1ce7fee2b5235b0685b93c1e22f6b30` |

**Verificación (HECHO):**

- Replay limpio desde el árbol de trabajo: coincide con el lock y el vendor es idéntico en los 268 ficheros. Los únicos ficheros con CRLF del árbol son 4 binarios de upstream (`.suprx`, `.ttf`).
- Replay con los **bytes del índice de Git** (`git show :<ruta>`; `closeout/replay_from_index.py`): mismo `patched_commit`, con los SHA-256 verificados sobre los blobs indexados. Índice y árbol de trabajo coinciden en los 24 parches y en el lock.
- Equivalencia funcional:
  - los dos ficheros son idénticos tras quitar los `\r`, sin literales *raw* ni continuaciones con `\`;
  - compilados con los mismos flags y la misma ruta, los `.obj` de `ps2_ipu.cpp` en CRLF y en LF son **idénticos byte a byte** (`d81c22a2…`).
- Builds incrementales:
  - juego: solo `ps2_ipu.cpp` y `dmc_overrides.cpp` (este por la retirada de la sonda de recorte), 0 errores, exe `feee3cd3…`;
  - tests: solo `ps2_ipu.cpp` y `ps2_ipu_tests.cpp`; 450 tests, los mismos 25 fallos preexistentes, IPU 5/5.
- No se repitió la build completa: el contenido funcional no cambió.

## 19. Build sin FFmpeg: parche 25 (`analysis/local/gameplay_001l/ffmpeg_off/`)

**Causa (HECHO):**

- El parche 10 (`BLOCKER_004_p423_p424_mpeg_movie_completion.patch`, P4.2.4) añadió `MpegFfmpegDecoder::drained()` solo a la clase con FFmpeg. También añadió tres condiciones de fin de película que lo llaman sin `#if` (`Kernel/Stubs/MPEG.cpp`, líneas 3517, 3898 y 3953).
- La clase sustituta sin FFmpeg (`#else`) tiene `feed`, `flush` y `reset`, pero no `drained()`. Es el único miembro que falta.
- Upstream `main` no tiene el problema: no contiene ni el accesor ni esas condiciones.
- Además, el `CMakeLists.txt` raíz del repositorio siembra `PS2X_ENABLE_FFMPEG=OFF` («Initial bring-up»). Una configuración nueva sin `-D` cae en ese modo. La build de trabajo tenía `ON` porque se configuró a mano.

**Corrección: parche 25**, `GAMEPLAY_001L_mpeg_stub_drained_without_ffmpeg.patch`. Lock con 25 parches, `patched_commit e86516d5c335a443d53b40b79eabc210bbaf043a`.

- El sustituto devuelve siempre `drained() = true`. Sin decodificador no hay nada en búfer ni ningún vaciado de fin de flujo pendiente.
- Copiar la lógica de la clase con FFmpeg sería un error: allí solo pasa a `true` tras `flush()` con el decodificador inicializado. El sustituto nunca se inicializa, así que la película no terminaría nunca.
- Con el valor fijo, el fin de película sin FFmpeg depende solo de los bytes ES consumidos, como antes de P4.2.4 y como en upstream.
- No se modifica el parche 10.

**Validación (HECHO):**

- Sin FFmpeg (runtime + `ps2x_tests` en una configuración nueva):
  - sin la corrección, exactamente los 3 errores `C2039 drained`; con ella, 0 errores;
  - 450 tests y los mismos 25 fallos preexistentes.
- Con FFmpeg:
  - el preprocesado de `MPEG.cpp` con los flags reales es idéntico token a token;
  - `MPEG.obj` antes y después es idéntico byte a byte (`6812eecb…`), así que el ejecutable validado de 001L no cambia;
  - builds incrementales (solo `MPEG.cpp`), 0 errores; 450 tests y los mismos 25 fallos.
- Replay de los 25 parches desde el árbol de trabajo y desde los bytes del índice de Git: coincide con el lock, y el vendor es idéntico en los 268 ficheros.
- No se repitió la build completa ni hubo corridas largas.
