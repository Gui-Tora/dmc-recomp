# BLOCKER_004 — PSS movie data reaches EE but produces no visible video

## Estado

`OPEN / FIRST DIVERGENCE LOCATED (build configuration, not a code bug)`.
No implementado.

No lo llamo todavía "bug de IPU", "bug de decodificador MPEG" ni "bug de
GS" porque, aunque la primera divergencia real ya está localizada (ver
más abajo) y apunta a una sola capa concreta, las capas posteriores
(subida a GS, presentación) siguen sin ejercitarse y por tanto sin
verificar — no se puede afirmar todavía que sean correctas.

## Evidencia inicial

**Original (PCSX2)**: los chunks reales del PSS llegan al buffer EE →
vídeo visible (confirmado por el usuario en sesión interactiva real,
turnos previos).

**RECOMP (tras BLOCKER_003)**: los chunks reales del PSS llegan al buffer
EE (verificado byte a byte contra la ISO, ver
`BLOCKER_003_cdmodule_movie_streaming.md`), la máquina de estados de
movie del EE progresa con normalidad (menú → `DEMO1.PSS` por timeout →
abortable con input → vuelve al menú) — **pero el vídeo visible es
pantalla negra**. El logo CAPCOM tampoco se ve correctamente, pero (ver
sección 4) no comparte pipeline con `DEMO1` — se trata como candidato
separado, no se funde con este blocker.

## 1. Cadena consumidora — qué capa es cada función

Reconstruida desde los símbolos reales del ELF (mismo mecanismo de
`ps2_call_list.h`/`sce_symbol_database_data.h` que sustituye funciones SDK
conocidas por HLE nativo en vez de recompilar su MIPS real):

| Función | Dirección ELF | Capa |
|---|---|---|
| `CdMovieTrans` (wrapper `fno=0xA`) | `0x1CF090` | **Código de juego recompilado normal** — no está en `PS2_SYSCALL_LIST` |
| `ReadFileGetAddr`/`ReadFileSetAddr`/`ReadDataGetAddr`/`ReadDataSetAddr` | `0x3FE380` y vecinas | **Código de juego recompilado normal** — no está en `PS2_SYSCALL_LIST` |
| `sceMpegCreate`/`sceMpegDemuxPssRing`/`sceMpegGetPicture`/etc | `0x109D70`, `0x109180`, ... | **HLE nativo** — están en `PS2_SYSCALL_LIST` (`ps2_call_list.h:447-466`); su código MIPS real del ELF se ignora y se sustituye por `vendor/PS2Recomp/ps2xRuntime/src/lib/Kernel/Stubs/MPEG.cpp` |
| `videoDecFlush`/`videoDecIsFlushed`/`audioDecSendToIOP`/`audioDecReset` | `0x47b860`, `0x47b970`, `0x47a160`, `0x479f80` | **Código de juego recompilado normal** — envoltorios propios de DMC alrededor de la API `sceMpeg`, no están en la lista de syscalls |
| `sceIpuInit`/`RestartDMA`/`StopDMA`/`Sync` | — (llamadas desde código de juego) | **HLE nativo, mínimo** — `Stubs/IPU.cpp` (108 líneas): solo pokes de registro de inicialización, no hay decodificación real de macrobloques ahí |
| `sceGsPutDispEnv`/`sceGsSyncV`/etc | — | **HLE nativo** — en `PS2_SYSCALL_LIST` (`ps2_call_list.h:372-392`) |

Conclusión de esta tabla: el punto donde el juego deja de ejecutar su
propio código y pasa a depender de una reimplementación nativa de RECOMP
es exactamente `sceMpegCreate`/`sceMpegDemuxPssRing`/`sceMpegGetPicture` —
`Stubs/MPEG.cpp`. Todo lo anterior en la cadena (el wrapper `CdMovieTrans`,
la gestión del ring buffer `ReadFileGetAddr`/`SetAddr`) es código del
juego recompilado normalmente y ya vimos en BLOCKER_003 que efectivamente
mueve bytes reales hasta ahí.

## 2. Primera divergencia real — localizada, no adivinada

`Stubs/MPEG.cpp` **no es un stub trivial**: son ~2600 líneas con estado de
reproducción real (`MpegPlaybackState`), consumo de bytes desde RAM guest
(`appendGuestBytes`/`appendGuestRingBytes`), y un decodificador MPEG real
respaldado por **FFmpeg** (`libavcodec`/`libswscale`,
`MpegFfmpegDecoder::feed()`/`flush()`) — no una implementación "de
juguete".

Pero el binario que construimos y validamos en BLOCKER_003 **se compiló
sin FFmpeg enlazado**:

```
CMakeCache.txt:  PS2X_ENABLE_FFMPEG:BOOL=OFF
build/bin/:      cero DLLs de FFmpeg (avcodec/avformat/avutil/swscale)
build/:          ninguna carpeta ThirdParty/ffmpeg-prefix (el ExternalProject_Add nunca se ejecutó)
```

Y el propio log de ejecución lo confirma en texto plano:

```
[MPEG] runtime built without FFmpeg; MPEG video decode is disabled.
```

Con `PS2X_HAS_FFMPEG` no definido, `MpegFfmpegDecoder::feed()` se
compila como una versión alternativa (`MPEG.cpp:446-468`) que **siempre
devuelve `false` y nunca añade nada a `decodedFrames`** — código
demostrado, no inferido:

```cpp
bool feed(const uint8_t *, size_t, std::deque<MpegDecodedFrame> &, int64_t = -1, int64_t = -1)
{
    // imprime el warning una vez
    return false;   // nunca decodifica nada
}
```

**Esta es la primera divergencia real: no hace falta decidir todavía si
el demux, el `sceMpegGetPicture`, la subida a GS o la presentación son
correctos — el pipeline nunca produce un solo fotograma decodificado,
incondicionalmente, para ningún PSS, independientemente de si las capas
anteriores (ya verificadas: transporte CD, demux input) son correctas.**
Esto no es un bug de lógica de decodificación — es una opción de build
(`PS2X_ENABLE_FFMPEG`) que quedó cacheada en `OFF` en
`analysis/local/symtabfirst/build/CMakeCache.txt`, probablemente de una
configuración anterior a que esta opción existiera en el CMake del
vendor (este directorio de build se reutiliza de forma incremental desde
el bring-up inicial). El CMake del vendor por defecto la pone en `ON`
para Windows (`CMakeLists.txt:245-251`) y, si estuviera activada,
descargaría un FFmpeg prebuilt vía `ExternalProject_Add`
(`CMakeLists.txt:256-282`) — eso nunca llegó a ejecutarse en este build.

## Mapa de capas pedido

```
PSS bytes in EE RAM        ✅  (BLOCKER_003, verificado byte a byte)
demux input                ✅  FUERTEMENTE SOPORTADO — appendGuestRingBytes
                                consume bytes reales del buffer que ya
                                alimentamos; no se leyó su parser interno
                                línea a línea, pero recibe entrada correcta
demux output /
video elementary stream    ABIERTO — no verificable mientras feed()
                                nunca decodifica nada
decoded frame               ❌  NUNCA se produce en este build — causa
                                localizada (PS2X_ENABLE_FFMPEG=OFF)
GS upload/presentation      ABIERTO — no verificable sin fotogramas que subir
```

## 3. Experimento mínimo para cerrar la duda

Un solo experimento, automatizable, sin PCSX2: **reconfigurar con
`-DPS2X_ENABLE_FFMPEG=ON`, dejar que `ExternalProject_Add` descargue el
FFmpeg prebuilt, hacer el build (ya no puramente incremental — primera
vez que se enlaza FFmpeg, requiere red) y repetir exactamente la misma
prueba automatizada de BLOCKER_003** (`pipeline.py run` con
`PS2X_CD_IMAGE`, mismo flujo `title.pss`→`demo1.pss`). Si con FFmpeg
enlazado aparecen fotogramas reales (verificable por código: log de
`sceMpegGetPicture`/tamaño de `decodedFrames`, o visualmente por el
usuario), la causa queda cerrada aquí mismo y las capas de GS/presentación
quedan bajo sospecha solo si la imagen sigue sin verse **con FFmpeg ya
activo**. Si el vídeo sigue negro incluso con FFmpeg linkado, la
divergencia está más adelante (subida a GS/`sceGsPutDispEnv`/etc) y hay
que investigar esa capa específicamente — no antes.

Este experimento implica build no puramente incremental (nueva
dependencia externa) y descarga de red — **no lo ejecuto sin tu
aprobación explícita**, tal como pediste.

## 4. CAPCOM — candidato separado, no fusionado

El log de la corrida de validación de BLOCKER_003 confirma que, antes del
primer `[CDMODULE/MOVIE:start]`, solo hay lecturas `fno=2` normales
(`resourceId=0x5c`, `0x7d`, ...) — **ningún `fno=9` precede al logo
CAPCOM**. El directorio `/DATA/MOVIE/` de la ISO tampoco contiene ningún
archivo con nombre relacionado a Capcom (se listaron los 18 `.PSS`
reales: `TITLEP`, `DEMO1P`, `DEMO0P`, `S01_1P`, `S0101_3P`, `S0502P`,
`S0502SP`, `S11012SP`, `S1101_2P`, `S1401_2P`, `S1402B2P`, `S1402B4P`,
`S15_1AP`, `S15_1ASP`, `S15_1BP`, `S15_1BSP`, `S15_2P`, `S15_2SP` — ni
uno con nombre asociable al logo).

**Conclusión, opción B del enunciado**: el logo CAPCOM usa una textura o
imagen normal (`fno=2`), no el pipeline PSS/movie. Es un candidato
**separado**, con causa todavía sin investigar — probablemente en el
lado GS/rendering directamente, no relacionado con `sceMpeg`/FFmpeg. No
se investiga aquí para no mezclar dos cadenas causales distintas.

## Qué NO se ha hecho

- No se ha tocado ningún build (`PS2X_ENABLE_FFMPEG` sigue en `OFF`).
- No se ha leído `appendGuestRingBytes`/`appendGuestBytes` línea a línea
  (demux input queda en FUERTEMENTE SOPORTADO, no HECHO).
- No se ha investigado la causa del logo CAPCOM.
- No se ha abierto ninguna investigación de GS/presentación — prematuro
  mientras no haya un solo fotograma decodificado que subir.
