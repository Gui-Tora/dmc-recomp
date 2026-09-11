# BLOCKER_004 — PSS movie data reaches EE but produces no visible video

## Estado

`OPEN`. **Primera divergencia real localizada con precisión total**
(demostrada dinámica Y estáticamente, ver sección 12): dentro de
`dispatchGuestStreamCallback` (`MPEG.cpp`), el despacho de la invocación
guest de `StrPcmCallBack` nunca llega a `queueInvocation` porque
`runtime->guestMalloc(...)` devuelve `0` en el 100% de los intentos
(16/16 en la corrida limpia). La causa no está en ninguna función del
juego ni en semántica MIPS/R5900: el **guest heap sintético de RECOMP
está colapsado a capacidad cero** (`base=0, limit=0`) desde el arranque,
por un problema de *sizing* en `PS2Runtime::resetGuestHeapLocked`/
`ensureGuestHeapInitializedLocked` (`ps2_runtime.cpp`) — infraestructura
de host, no traducción de juego. Ver sección 12.6-12.7 para la cadena
completa y los números exactos. No implementado (no se ha tocado
`guestMalloc`, `resetGuestHeapLocked` ni ningún otro código de
infraestructura).

**Actualización (sección 13, Prompt P3.5)**: diseño de fix investigado
en profundidad, sin implementar. Los 14 `PT_LOAD` altos son marcadores
de un sistema de overlays del linker (identidad confirmada por nombre
y símbolos, no una hipótesis). Hallazgo importante que cambia la
recomendación: DMC tiene su **propio** gestor de heap dinámico
(`Mem_alloc`/`Mem_alloc_R`) que probablemente reclama buena parte del
"hueco libre" entre el final de `main` y la ventana de overlays —
"libre en el ELF" no implica "libre en runtime" para este juego,
confirmado por evidencia real (13.4). Recomendación: corregir la
heurística de `maxLoadedRdramEnd` para excluir estructuralmente los
segmentos marcador (no es un parche específico de DMC), combinado con
verificación adicional antes de confiar en el rango resultante — ver
13.6 para la propuesta completa y sus invariantes.

**Actualización (sección 14, Prompt P3.6)**: auditoría de colisión en
runtime, independiente de P3.5. Reconstruido el mecanismo completo de
`Mem_alloc_R` (arena que crece hacia ABAJO desde un techo real y
demostrado, `0x01E00000`, con margen de `0xAC0000` por-asignación, sin
suelo demostrado). Hallazgo nuevo importante: existe un SEGUNDO
mecanismo de reserva de memoria guest en RECOMP
(`reserveAsyncCallbackStack`), activamente usado por el scheduler para
CUALQUIER invocación de callback guest, que reclama la MISMA ventana
de 1MB superior que ya usan el overlay, el heap C y el stack nativo de
DMC — arreglar solo `guestMalloc` no basta. Conclusión explícita: **NO
SAFE RANGE PROVEN** — ningún intervalo cumple el estándar de evidencia
exigido; se especifica la medición dinámica exacta que falta
(low-water-mark real de `Mem_alloc_R`, sección 14.8/14.12).

**Cadena causal histórica, de más a menos reciente (todas las capas
1-3 siguientes quedan RECLASIFICADAS como consecuencias posteriores, no
como la primera divergencia — ver sección 12 para la explicación
completa de por qué)**:
1. (Sección 9, HECHO dinámico) `Movie_ctrl` queda atrapado en el bucle
   `label_1ce750→label_1ce5b4`, gateado por el bit `0x10` de
   `*(state+0x2FC)`, y por eso **nunca llama a `videoDecFlush`** ni
   avanza hasta el chequeo de audio ni hasta `Task_execute`.
2. (Sección 9.4, HECHO estático exhaustivo) el bit `0x10` que
   destrabaría ese bucle **solo se escribe dentro de `MpegMovieDecode`**
   (`0x47AA7C`) — la función que ese mismo bucle impide alcanzar. Ciclo
   cerrado demostrado en el binario recompilado.
3. (Sección 8.4, refutado) la hipótesis anterior — "`audioDecReset`
   corre cada tick y resetea el contador de audio" — quedó **refutada
   por instrumentación dinámica real** (cero llamadas observadas); ya no
   es el mecanismo relevante, superada por 1-2.

Estas tres capas siguen siendo HECHOS válidos sobre el estado de
`Movie_ctrl` en RECOMP, pero la sección 12 demuestra que todas ellas son
**consecuencia** de que `audioDecIsPreset` nunca tenga éxito, que a su
vez es consecuencia de que `mode` nunca llegue a `1`, que a su vez es
consecuencia directa del fallo de `guestMalloc` — no líneas causales
independientes.

**Actualización (sección 11)**: la vía de alcance de `audioDecResume`
quedó RESUELTA dinámicamente (`MpegMovieDecode → audioDecStart →
[tail-jump] → audioDecResume`), y esto sigue siendo válido — pero la
sección 11.3 (vía rápida `flags16&0x4000` como mecanismo del PRIMER
éxito de `audioDecIsPreset`) queda **RETRACTADA** en la sección 12.2 con
evidencia dinámica de dos sesiones independientes: el mecanismo real de
arranque es la vía B (`+0x58>=+0x4C`), no la vía A (`bit4000`).

**Actualización (secciones 16-18, P3.7.1-P3.7.1.2, IMPLEMENTADO)**: el
colapso de `guestMalloc` descrito arriba está **resuelto para DMC**
mediante la separación arquitectónica `GameHeapState`/
`RuntimeGuestArena` (override explícito `PS2X_RUNTIME_ARENA_BASE=
0x9FA000 PS2X_RUNTIME_ARENA_LIMIT=0xABE000`). Con el fix activo, la
escalera P3.4 avanza completa hasta `sceMpegGetPicture` ENTRY — algo
nunca visto en ninguna iteración anterior. Ver sección 18 para el
diseño completo, invariantes y validación (7/7 tests de configuración,
15/15 pasos de escalera).

**Actualización (secciones 19-20, P3.8/P3.8.1)**: con `guestMalloc`
resuelto, apareció una **divergencia nueva y distinta**, ya
identificada con precisión y **confirmada con evidencia ORIGINAL real
(PCSX2) además de estática**: la HLE de `sceMpegGetPicture` en RECOMP
trata la llamada como **bloqueante** (suspende el hilo guest completo
vía `waitExternal` si no hay fotograma listo) cuando la función real
de `libmpeg` (desensamblada directamente del ELF, sección 20.2) es
**no bloqueante por diseño** — retorna de inmediato con un código de
estado, dejando el reintento al propio bucle por-tick del juego. Esta
es ahora la primera divergencia real vigente para BLOCKER_004 — ver
sección 20 para el detalle completo y la retractación explícita de la
interpretación causal previa (sección 19.7). **No implementado.**

**Actualización (sección 21, Prompt P3.9)**: caracterizado el contrato
completo de `_getpic` (`0x10A3D0`, símbolo real), la función interna
que hace todo el trabajo real. Es, literalmente, el propio bucle de
parseo/decodificación MPEG en software (confirmado por nombres reales
de símbolos: `_nextHeader`, `_isOutputPicture`, `_picture_structure`,
`_isMpeg2`, `_decodeOrSkip`, `_sceMpegFlush`), ejecutado síncronamente
dentro de la llamada, sin ninguna interacción IPU demostrada. Retorna
`v0=1` cuando `_isOutputPicture` se activa dentro del propio bucle, o
`v0=-1` en un único error de alineación — no existe un tercer estado
"todavía no". Hallazgo adicional: el caller (`MpegMovieDecode`) no
examina el valor exacto de `v0`, solo su signo — la señal real que usa
es `frameCount` (`mpegAddr+0x8`), que la HLE de RECOMP ya escribe
correctamente hoy. Esto acota mucho el contrato mínimo necesario para
una futura HLE no bloqueante. **No implementado.** Ver sección 21.

**Actualización (sección 22, Prompt P3.10)**: resuelta con evidencia
estática Y dinámica (no hipótesis) la aparente contradicción entre
"frame decoded total=4" y "decodedFrames=0" en la primera llamada real
a `GetPicture`. Causa: `MpegMovieDecode`, en su primer tick, llama
incondicionalmente a `MpegMovieDecodeInit`, que a su vez llama
incondicionalmente a `sceMpegInit` — el cual borra por completo
(`playbackByMpeg.clear()`) el estado de reproducción, incluidos los 4
frames ya decodificados, justo antes de que se registre la primera
llamada a `sceMpegGetPicture`. Es código real del juego (bytes MIPS
del ELF), no un bug de RECOMP: los 4 frames nunca estuvieron
destinados a ser consumidos por ese `GetPicture`. CASO C confirmado
(ver sección 22.11). La pregunta que sigue abierta para BLOCKER_004 —
si el demux vuelve a alimentar la entrada fresca lo bastante rápido
tras el reset — queda fuera de esta iteración; conecta directamente
con el "stall" ya caracterizado en las secciones 19-20. **No
implementado.** Ver sección 22.

**RETRACTADO (P3.11.1, sobre la sección 22 anterior)**: "los 4 frames
nunca estuvieron destinados a ser consumidos por ese GetPicture" como
justificación de que el borrado sea inocuo. El original NO destruye
objetos software MPEG en `sceMpegInit` (solo hardware IPU/DMA vía MMIO,
disasm re-verificado byte a byte); el HLE sí lo hace, sin contrato de
reconstrucción. Full detail: `BLOCKER_004_ASTRA_P311_OVERNIGHT.md`
(P3.11) y `BLOCKER_004_FABLE_P3111_CAUSAL_AUDIT.md` (P3.11.1,
auditoría adversarial). Clasificación vigente:
**"callbacks/config/input-ownership over-reset CONFIRMADO; equivalencia
decoder/decodedFrames UNKNOWN"** — no "over-reset completo".

**Actualización (Prompt P3.12, SCHEDULER_BATCH_INVOCATION_ORDER_FIX_AND_REVERIFY,
IMPLEMENTADO)**: P3.11.1 encontró que, ANTES de `sceMpegInit`, el
propio scheduler (`EeScheduler::run()`) invertía el orden de ejecución
de lotes de invocaciones encoladas con `queueInvocation()` — drenaba
`m_pendingInvocations` en FIFO (`front()`/`pop_front()`) hacia la pila
por-hilo `GuestThread::invocations`, que se ejecuta LIFO
(`activeContext()`/`pop_back()` sobre `.back()`), invirtiendo cada
lote. Esto explicaba la "anomalía ES guest" que P3.11 había dejado como
UNKNOWN: el `viBuf` de vídeo escrito por los callbacks guest de MPEG
tenía el contenido íntegro pero en el orden equivocado por lotes de 64
KiB. Fix mínimo y genérico implementado (no específico de MPEG/DMC):
drenar `m_pendingInvocations` por `back()`/`pop_back()` en vez de
`front()`/`pop_front()`, en los dos únicos puntos donde el dispatcher
consume la cola pendiente (`EeScheduler.cpp`). Sin tocar
`activeContext()`, el `pop_back()` de la pila por-hilo, ni
`invokeCurrentSequence` (que ya compensaba correctamente este mismo
contrato insertando en reversa). Auditados los 4 productores reales de
`queueInvocation` en todo el runtime (MPEG demux, `dispatchIrq`,
VBlank→GS-callback, Alarm) — ninguno es un caso especial, todos
corregidos por el mismo fix genérico. **Validado 3/3 en corridas reales
(RUN_012-014)**: `viBuf` guest ahora es **byte-exacto** frente al ES
MPEG esperado (SHA256 `127e3b62…b82c` idéntico en ambos lados, primera
diferencia = ninguna, en las tres corridas). Con `sceMpegInit` sin
tocar (instrucción explícita del prompt), el primer `GetPicture` sigue
produciendo el mismo stall vacío observado desde P3.8 — las dos causas
(orden del scheduler y ownership de `sceMpegInit`) quedan separadas
limpiamente: la primera CORREGIDA, la segunda sigue siendo el bloqueo
vigente de BLOCKER_004. **No implementado**: fix de `sceMpegInit`. Ver
`BLOCKER_004_P312_SCHEDULER_BATCH_ORDER.md` para el detalle completo
(auditoría de productores, diseño del fix, test aislado, comparación
byte a byte por corrida).

**Actualización (Prompt P3.13, SCEMPEGINIT_SESSION_OWNERSHIP_MATRIX_AND_REVERIFY,
EXPERIMENTAL, no implementado como fix)**: matriz causal de 4 modos
opt-in (`baseline`/`ownership`/`queued`/`full`, `DMC_P313_INIT_MODE`) para
determinar qué continuidad de estado host through `sceMpegInit` hace falta
para cruzar el primer `GetPicture`. Resultado, 3/3 por modo salvo
baseline/ownership (1 corrida cada uno, sin sorpresas que ameritaran
repetición): **ownership puro (callbacks+config+`sawInput` preservados,
decoder/frames NO) es INSUFICIENTE** — sigue en `GP:H` idéntico a baseline.
**Preservar la cola de 4 frames ya decodificados (`queued`) SÍ basta para
cruzar el primer `GetPicture` caller-visible** (`[GP:CONT]`, boundary
equivalente a `0x47A8EC`, 4/4 éxitos en las tres corridas) pero se detiene
en exactamente esos 4 frames — cero frames nuevos post-init. **Preservar
además el objeto decoder en vivo (`full`) SÍ sostiene progreso más allá de
la cola**: 10 éxitos y 6 frames nuevos decodificados tras el init en las
tres corridas, hasta agotar un recurso no relacionado
(`RuntimeGuestArena`, hallazgo lateral, fuera de alcance). Clasificación:
**QUEUED_OUTPUT_BRIDGE_REQUIRED**, con continuidad de decoder demostrada
como requisito adicional de la arquitectura HLE ACTUAL para progreso
sostenido — explícitamente NO una afirmación de equivalencia con el
reset de hardware IPU/DMA real del original. **No implementado ningún fix
de producción.** Ver `BLOCKER_004_P313_SCEMPEGINIT_OWNERSHIP_MATRIX.md`
para la tabla de ownership completa, la auditoría de generación
(`cdStreamGeneration`, riesgo latente confirmado pero no disparado en la
corrida actual) y el detalle de las 8 corridas.

**Actualización (Prompt P3.13.1, FULL_MODE_VISUAL_TRACE_AND_LANGUAGE_VARIATION,
observacional puro)**: capturas densas (~500ms) de la ventana propia en 5
corridas nuevas en modo `full` confirmaron, de forma independiente y
automatizada, la **primera salida visual reconocible del juego en
RECOMP**: la pantalla de advertencia de contenido violento, real y
legible. Hallazgo clave: es una secuencia de DOS páginas (inglés+alemán+
francés, luego italiano+español) que TODAS las corridas recorren en el
mismo orden — la variación de idioma observada manualmente por el
usuario en capturas sueltas queda explicada como distintos puntos de esa
misma secuencia fija, capturados en momentos distintos por una carrera
con el agotamiento de `RuntimeGuestArena` (mismo límite de recursos ya
identificado en P3.13, reproducido 5/5 aquí también) — NO por selección
de idioma de menú ni por nondeterminismo de estado del guest. Una
hipótesis intermedia de correlación con el estado del menú antes de
CROSS se formuló y se **RETRACTÓ explícitamente** dentro de la misma
iteración al inspeccionar más capturas. Artefacto de renderizado real y
reproducible confirmado por separado: el bloque en español aparece
duplicado 2-3 veces en la página 2. Ningún fotograma de vídeo MPEG
decodificado (CAPCOM logo, animación PSS) fue observado — solo texto de
UI. No cambia ninguna conclusión causal de P3.13; `sceMpegInit` sigue
siendo el bloqueo vigente. Ver
`BLOCKER_004_P3131_VISUAL_LANGUAGE_TRACE.md` para el detalle completo.

**Actualización**: la causa documentada originalmente (`PS2X_ENABLE_FFMPEG=OFF`,
sección 2) ya se corrigió y se probó — ver sección 5. Con FFmpeg realmente
activo apareció una divergencia **nueva y más profunda**, que es ahora la
primera divergencia vigente: el decodificador produce fotogramas reales,
pero `sceMpegGetPicture` no se llama nunca, lo que autobloquea todo el
pipeline por diseño de *backpressure*. Ver sección 5 para la cadena
completa. No lo llamo todavía "bug de scheduler", "bug de MPEG" ni "bug de
dispatch HLE" — eso es justo lo que queda por localizar.

**Corrección explícita (esta actualización)**: "`sceMpegGetPicture` ENTRY=0"
(sección 5) **no demuestra** que `MpegMovieDecode` nunca llegue a
ejecutarse. `MpegMovieDecode` podría entrar (ENTRY>0) y no alcanzar su
único `jal sceMpegGetPicture` por una rama interna — de hecho, sección 6.5
identifica, por desensamblado completo y no genérico, **dos mecanismos
concretos dentro de la propia función** que evitan la llamada sin que la
función deje de "ejecutarse". El caso "el video pump nunca se ejecuta en
absoluto" (antes llamado CASO A) ya **NO está abierto**: la sección 7
lo confirma con evidencia dinámica real (instrumentación + corrida) —
`MpegMovieDecode` nunca llega a `ENTRY` porque su `Task_execute` nunca se
llama, porque `audioDecIsPreset()` nunca devuelve distinto de 0. Los dos
mecanismos de 6.5 quedan documentados pero no son la causa primaria.

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

## 3. Experimento mínimo para cerrar la duda — EJECUTADO, ver sección 5

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

## 5. Experimento de BLOCKER_004 ejecutado — nueva divergencia, más profunda

Se activó `PS2X_ENABLE_FFMPEG=ON` de verdad (build reparado quirúrgicamente
tras una interrupción que dejó un `.obj` truncado — ver historial de la
sesión; no afecta a esta nota) y se validó en runtime: el mensaje `[MPEG]
runtime built without FFmpeg` desaparece, las DLLs de FFmpeg
(`avcodec-61.dll` etc.) se copian y cargan correctamente.

Con instrumentación temporal mínima en `MPEG.cpp` (contadores dispersos,
sin spam por paquete — pendiente de revertir cuando se cierre este
blocker), una corrida real de `TITLEP.PSS` dio:

```
feed call #1, #2, #5     → fedOk=1
frame decoded total=1..8  (en feed calls #9, #23, #36, #49, #62, #74, #87, #100)
[CDMODULE/MOVIE:chunk] ... hasta offset=0xC0000
```

y ahí el proceso se **detiene de forma permanente** — más de 10 minutos
reales, CPU activa todo el tiempo (~habitual, no colgado en I/O), sin una
sola línea nueva de progreso. **Cero llamadas a `sceMpegGetPicture` en
toda la ejecución** (instrumentado con un contador de entrada
independiente del de éxito).

### Causa mecánica exacta, confirmada por código (`MPEG.cpp`)

```cpp
constexpr size_t kMaxDecodedPicturesAhead = 8u;
// mpegDemuxBackpressured(): decodedFrames.size() >= kMaxDecodedPicturesAhead
```

Cadena demostrada:

```
FFmpeg produce frames reales
  -> nadie llama sceMpegGetPicture para consumirlos
  -> decodedFrames llega a kMaxDecodedPicturesAhead = 8
  -> mpegDemuxBackpressured() = true
  -> el demux deja de aceptar más datos (consumed=0)
  -> el juego (código recompilado normal) deja de pedir nuevos chunks
     vía fno=0xA a CDMODULE
  -> todo el pipeline de movie queda bloqueado permanentemente
```

**No se toca `kMaxDecodedPicturesAhead` ni el backpressure** — es el
mecanismo que expuso esta divergencia; quitarlo solo la ocultaría.

### Corrección importante sobre el comportamiento con FFmpeg OFF

Con `PS2X_ENABLE_FFMPEG=OFF` (estado documentado en la sección 2, ya
superado como causa principal) el juego avanzaba más lejos —
completaba `TITLEP.PSS` entero, encadenaba varias películas seguidas
(`title → demo1 → title → demo0`, ver `BLOCKER_003`) — **pero eso no era
comportamiento correcto**: `decodedFrames` nunca crecía (porque `feed()`
nunca decodificaba nada), así que el backpressure nunca se activaba. El
avance observado antes era un efecto secundario accidental de que el
decoder real estuviera desactivado, no una señal de que el pipeline
funcionara — el bloqueo actual estaba ahí todo el tiempo, solo quedó
enmascarado.

### Mapa de capas — actualizado

```
PSS bytes in EE RAM        ✅  (BLOCKER_003, verificado byte a byte)
demux input                ✅  FUERTEMENTE SOPORTADO (sin leer el parser línea a línea)
decoded frame               ✅  HECHO — 8 frames reales confirmados por instrumentación
sceMpegGetPicture llamado   ❌  NUNCA, en ninguna de las corridas — nueva primera divergencia
GS upload/presentation      ABIERTO — no alcanzable mientras el pipeline esté bloqueado antes de esto
```

## 6. Localización estática completa: ENTRY → `jal sceMpegGetPicture` (corrección de sobreafirmación)

Objetivo: instrumentar (secciones 2-4 pendientes) la ruta completa `movie
main loop -> audioDecSendToIOP -> audioDecIsPreset -> flags ->
Task_execute(3, MpegMovieDecode) -> MpegMovieDecode ENTRY -> jal
sceMpegGetPicture`, sin asumir todavía cuál de los eslabones falla. Esta
sección cubre la parte **estática** (estado: HECHO por desensamblado
directo salvo donde se marca explícitamente HIPÓTESIS/ABIERTO); la
instrumentación dinámica y la clasificación CASO A-F quedan para la
siguiente iteración de este documento.

### 6.1 Corrección de atribución de archivo — HECHO

El rango que en turnos anteriores se trató como "una sola función
`0x1CE3A0-0x1CE83C`" **no es una función real**: cruza tres símbolos ELF
distintos:

```
0x1ce340  Movie_set          size=0x1b0  end=0x1ce4f0
0x1ce4f0  Movie_task_init    size=0x1c   end=0x1ce50c
0x1ce510  Movie_ctrl         size=0x3bc  end=0x1ce8cc   <- Task_execute(3, MpegMovieDecode) vive aquí
```

Archivos generados confirmados en disco (`analysis/local/symtabfirst/generated/`):

- `Movie_ctrl_0x1ce510.cpp` (0x1CE510-0x1CE8CC) — función real, "tick" de
  control de la reproducción; se lee completa (todo el `switch(ctx->pc)`
  y las 43 etiquetas internas) en esta iteración.
- `MpegMovieDecode_0x47a760.cpp` (0x47A760-0x47AA9C, 828 bytes) — cuerpo
  completo desensamblado y revisado línea a línea.
- `audioDecIsPreset_0x47a120.cpp` (0x47A120-0x47A154, 52 bytes).
- `audioDecSendToIOP_0x47a160.cpp` (0x47A160-0x47A320, 448 bytes).

### 6.2 Gate real de `Task_execute(3, MpegMovieDecode)` dentro de `Movie_ctrl` — HECHO

`Movie_ctrl` se invoca una vez por "tick" (llamador externo aún no
identificado — probablemente el propio motor de `Task`, no investigado en
esta iteración). Cada llamada:

1. Resetea `$s2 = 0` **incondicionalmente** en el delay-slot de
   `0x1ce538` (`paddub $s2,$zero,$zero`) — corrección respecto a una
   hipótesis anterior: `$s2` **no** es una latch que persista bloqueada
   entre ticks; se reinicia a 0 en cada llamada a `Movie_ctrl`, así que
   `s2==0` es prácticamente siempre verdadero en el punto donde se
   comprueba (línea 0x1ce6d4), salvo que `Task_execute` ya se haya
   llamado **dentro de esa misma invocación** (0x1ce734 pone `s2=1` justo
   después de llamarlo).
2. Llama a `sceMpegDemuxPssRing(state)` (HLE, `0x1CE6A8`) — alimenta el
   demux con más bytes del PSS.
3. Llama **incondicionalmente** a `audioDecSendToIOP(state)` (`0x1CE6CC`,
   justo antes del gate).
4. Gate real (todo verificado por desensamblado, no por memoria de
   turnos anteriores):

```cpp
if (s2 == 0) {                                   // 0x1ce6d4 — prácticamente siempre true
    if (audioDecIsPreset(state) != 0) {           // 0x1ce6dc-0x1ce6e4
        uint32_t flags = *(state + 0x2FC);        // 0x1ce6ec
        if (flags & 0x08000000) {                 // 0x1ce6f0-0x1ce6f8  ("readyBit")
            if (flags & 0x200) Task_suspend(0);    // 0x1ce700-0x1ce714 ("suspendBit")
            Fade_kill(...);                        // 0x1ce71c
            Task_execute(3, MpegMovieDecode);      // 0x1ce72c  (func_19F820 = Task_execute, confirmado por símbolo)
            s2 = 1;                                // 0x1ce734
        }
    }
}
```

`readyBit` se construye con `lui $v0,0x800` (=0x08000000) + `and` — se
re-verificó el byte exacto ahora mismo por desensamblado directo, confirma
la autocorrección de una sesión anterior sobre este mismo bit (no es
`0x800`, es `0x08000000`, bit 27).

### 6.3 `audioDecIsPreset` — desensamblado directo, HECHO

```c
uint16_t v = *(uint16_t*)(*(state + 112) + 2);
if (v & 0x4000) return 1;
return (*(int32_t*)(state + 88) >= *(int32_t*)(state + 76)) ? 1 : 0;
```

### 6.4 Quién avanza `*(state+88)` — HECHO parcial, resto HIPÓTESIS

`audioDecSendToIOP` (llamada cada tick desde `Movie_ctrl`, ver 6.2.3) es
la función que escribe `*(state+88)` — confirmado por desensamblado
directo completo (448 bytes, íntegro):

- Dispatcher de 4 modos según `*(state+44)` (valores 0,1,2,3).
- Modo `2` llama al syscall real **`sceSdRemote`** (`0x10FB78`, HLE en
  `Stubs/Audio.cpp:77`) con `cmd=0x8100` (`kLibSdCmdBlockTransStatus`).
- El resultado alimenta un cálculo de espacio disponible (`iopGetArea`,
  función local `0x47A320`) que, si supera dos umbrales de 1024 bytes,
  llama a la función local `sendToIOP2area` (`0x47A450`) — y **en
  cualquier caso**, al final del camino común (`0x47a2e0-0x47a2e8`),
  hace `*(state+88) += delta`, con `delta` derivado de ese cálculo de
  espacio.
- `*(state+76)` (el tope contra el que compara `audioDecIsPreset`) **no
  se escribe** en ningún punto de `audioDecSendToIOP` ni de
  `audioDecIsPreset` — parece una capacidad/constante fija cuyo origen
  no se ha investigado (**ABIERTO**).

`Stubs/Audio.cpp::sceSdRemote` **no es un stub trivial**: implementa
semántica real de `VoiceTrans`/`BlockTrans`/`*Status` con estado propio
(`g_audio_stub_state`), incluyendo avance real de `blockTransfer.offset`
en modo `BlockTransStatus` — **pero solo si `blockTransfer.active ==
true`**, y eso solo lo activa un `cmd == kLibSdCmdBlockTrans` (0x80E0)
con `direction != stop`.

Escaneo exhaustivo de inmediatos `ori $a1,$zero,0x80e0` en todo `.text`
(`0x100000-0x600000`) da **exactamente 2 sitios**, ambos en funciones
reales con símbolo propio:

```
0x479ea8  dentro de audioDecPause   (0x479E80-0x479F00)
0x479f40  dentro de audioDecResume  (0x479F00-0x479F68)
```

Ninguno de los dos aparece en la cadena `Movie_ctrl -> Task_execute` ni
dentro de `MpegMovieDecode` (que solo llama a `audioDecStart`, una
función distinta de 8 bytes — `0x479F70-0x479F78`, casi con seguridad un
simple *setter* de flag, no una activación de `BlockTrans`).

**HIPÓTESIS (no confirmada dinámicamente todavía)**: si `audioDecResume`
nunca llega a invocarse antes/durante el arranque de una película,
`blockTransfer.active` se queda en `false` en el estado nativo de
`sceSdRemote`, las llamadas a `BlockTransStatus` devuelven siempre el
estado de un `blockTransfer` vacío/estático, `*(state+88)` avanza con un
delta potencialmente insuficiente, y `audioDecIsPreset()` nunca cumple
`*(state+88) >= *(state+76)` — lo que dejaría el gate de
`Task_execute(3, MpegMovieDecode)` permanentemente cerrado (encajaría con
CASO A/B). **No se ha determinado todavía quién debería llamar a
`audioDecResume` ni si llega a llamarse** — siguiente paso natural de
rastreo estático, o de instrumentación directa.

### 6.5 Enumeración completa de ramas dentro de `MpegMovieDecode` entre ENTRY y `jal sceMpegGetPicture @ 0x47A8E4` — HECHO, 828 bytes íntegros revisados

No se asume que ENTRY implique alcanzar la llamada. Ramas reales
encontradas por desensamblado completo del cuerpo generado:

1. **ENTRY** (`0x47A760`): llama a una función sin resolver (`0x47A6E0`)
   y luego a `sceMpegIsEnd()` (HLE, `0x10A108`).
   - Si `sceMpegIsEnd() != 0` → salto directo a `0x47AA70`
     (`sceMpegInit()`, reinicio de sesión) — **`sceMpegGetPicture` NUNCA
     se alcanza en esta pasada de la función.**
2. Si `sceMpegIsEnd() == 0`: bloque condicionado por `flags & 0x4000` —
   si está puesto, se salta la configuración de canal IPU/DMA
   (lecturas del registro hardware `0x7406A4`) y va directo al punto 3.
3. En `0x47A880`: comprueba `flags & 0x8000`.
   - Si **clear** → salto directo a `0x47A8D8` (justo antes de construir
     los argumentos del `jal sceMpegGetPicture`) — sí llega a la llamada.
   - Si **set** → decrementa un contador de 16 bits en el registro
     hardware `0x9F2890` (aparenta ser una cuenta atrás de vsync/IPU),
     llama a `changeInputVolume(cnt)` (`0x47A5B0`) y luego a
     **`Fade_status()`** (`0x15AB80`):
     - Si `Fade_status() & 4` → llama a **`CdMovieExit()`** (`0x1CF1C0`)
       y salta **incondicionalmente** a `0x47AA70` (mismo camino de
       reinicio que el punto 1) — **`sceMpegGetPicture` se SALTA otra
       vez, aunque la función sí llegó a ejecutarse por completo hasta
       aquí.**
     - Si no → cae a `0x47A8D8` igual que la otra rama, y de ahí sí
       llega al `jal sceMpegGetPicture @ 0x47A8E4`.

**Conclusión de 6.5**: existen exactamente dos condiciones demostradas,
internas a `MpegMovieDecode`, que evitan `sceMpegGetPicture` con
`ENTRY > 0`: `sceMpegIsEnd() != 0` al entrar, o
`(flags & 0x8000) != 0 && (Fade_status() & 4) != 0`. Esto es un CASO D
concreto (con nombres de función reales, no genérico "rama interna sin
identificar") — ya no una posibilidad abstracta.

### 6.6 Reclasificación de los casos A-F a la luz de esta evidencia

- **CASO A/B** (`Task_execute` nunca se llama): sigue abierto. Tiene ahora
  una hipótesis concreta y verificable (6.4: `audioDecResume` nunca
  invocado → `audioDecIsPreset()` nunca `!=0`), pendiente de confirmación
  dinámica.
- **CASO C** (`Task_execute` se llama pero `MpegMovieDecode` ENTRY=0):
  no investigado — dependería del motor de tareas (`Task_execute`,
  `0x19F820`), no se ha leído su cuerpo en esta iteración.
- **CASO D** (`MpegMovieDecode` ENTRY>0 pero no llega a `getPicture`):
  ahora tiene dos mecanismos concretos identificados (6.5), no genéricos.
- **CASO E** (llega al `jal` pero `MPEG.cpp` ENTRY=0 — problema de
  dispatch HLE): la ausencia de logs de `sceMpegGetPicture` en la sección
  5 es consistente con A, B, C **o** D — no los distingue entre sí. Sigue
  sin poder descartarse sin instrumentación dinámica.
- **CASO F**: sin candidato nuevo identificado en esta iteración.

**Pendiente para cerrar la duda entre A/B/C/D**: instrumentación mínima
(secciones 2-4 del plan, todavía no aplicada) en `Movie_ctrl` (gate),
`MpegMovieDecode` (ENTRY + los dos puntos de salto de 6.5) y
`audioDecIsPreset`/`audioDecSendToIOP` (operandos reales de 6.3/6.4),
seguida de una corrida corta (sección 7) para obtener la clasificación
CASO A-F con evidencia dinámica real, no solo estática.

## 7. Instrumentación aplicada y corrida real — CASO A confirmado dinámicamente

Se aplicó la instrumentación descrita en 6.2-6.3 (`[MOVIE:pump-gate]`,
`[MOVIE:pump-start]` en `Movie_ctrl`; `ENTRY`/dos `SKIP`/`CALL
sceMpegGetPicture` en `MpegMovieDecode`; `[AUDIO:preset]` con los
operandos exactos en `audioDecIsPreset`). Build sin regenerar ni tocar
CMake: los 3 `.cpp` afectados se compilaron directamente con `cl.exe`
(la propiedad `SelectedFiles` de MSBuild dejó de filtrar correctamente en
este árbol — con uno o varios archivos volvía a listar los ~10.125
`.cpp` generados en una sola invocación batch de `CL.exe`; se abortó esa
vía a los 5 minutos, sin que llegara a compilar nada, y se usó en su
lugar la línea de comandos exacta de `CL.exe` ya capturada, ejecutada
manualmente solo para los 3 archivos). Relink con `_BuildLinkAction`
(mismo mecanismo probado antes en la sesión): **0 `.cpp` generados
recompilados**, solo relink — confirmado por el log de MSBuild.

Corrida real con `PS2X_CD_IMAGE` apuntando a la ISO, lanzando el exe
directamente (no vía `pipeline.py run`, porque el vendor sigue "sucio"
por la instrumentación temporal sin commit). Resultado, estable y
repetido en 10 lecturas consecutivas de `audioDecIsPreset`, incluyendo
**después** de que arrancó una película real (`[CDMODULE/MOVIE:start]
resourceId=0x22e lba=0x1f62b8 totalSize=0x1a5c004`, `fno=9`):

```
[AUDIO:preset] flags16=0x8000 bit4000=0 counter88=0 counter76=24576 result=0
```

— idéntico en las 10 lecturas, tanto antes como después del arranque de
la película. **Cero** líneas `[MOVIE:pump-gate]`, `[MOVIE:pump-start]` o
`[MOVIE:decode-task]` en toda la corrida.

### Clasificación

**CASO A, confirmado con evidencia dinámica real** (no solo estática):
`Task_execute(3, MpegMovieDecode)` nunca se llama porque
`audioDecIsPreset(state)` nunca devuelve distinto de 0 — el gate falla
en su primera condición (0x1ce6e4 en `Movie_ctrl`), antes siquiera de
llegar al chequeo de `readyBit`/`suspendBit` (por eso no aparece ningún
`[MOVIE:pump-gate]`: ese log está colocado justo después de que
`audioDecIsPreset()!=0` se cumple, y nunca se cumple). Esto confirma
también, como corolario, que **CASO D (los dos saltos internos de
`MpegMovieDecode` en 6.5) no es la causa primaria** — la función ni
siquiera llega a ejecutarse (`ENTRY` nunca se registró), así que sus
ramas internas son irrelevantes mientras el gate de `Movie_ctrl` no se
abra. CASO B, C, E, F quedan descartados como causa primaria por la
misma razón: nada después de `audioDecIsPreset()` importa todavía.

Causa exacta del `result=0`, ya derivada estáticamente en 6.3 y ahora
confirmada con los valores reales: `bit4000=0` (vía de salida rápida no
disponible) y `counter88=0 < counter76=24576` (la comparación lenta
falla). `counter88` se queda **congelado en 0** de principio a fin —
consistente con la hipótesis de 6.4: si `audioDecResume`
(`kLibSdCmdBlockTrans`, activación real del *block transfer* en
`sceSdRemote`) nunca llega a invocarse, `audioDecSendToIOP` nunca avanza
`*(state+88)`.

### Siguiente paso natural (no ejecutado todavía)

Determinar, por el lado del EE, quién debería llamar a `audioDecResume`
(`0x479F00`) y por qué no llega a invocarse (o se invoca pero
`sceSdRemote` no activa `blockTransfer.active`). Candidatos sin
investigar: falta de llamada a `audioDecResume` en la secuencia de
arranque del movie, o una llamada previa a `sceSdRemote` con
`cmd=kLibSdCmdBlockTrans` cuyos argumentos `arg4`/`arg5` llegan en 0
(camino que en el HLE devuelve `0xFFFFFFFF` y dejaría `active=false`
permanentemente — ver `Stubs/Audio.cpp:149-152`).

**Sección 8 (comparación con PCSX2) — no ejecutada.** Con esta
evidencia, la medición mínima propuesta sería: breakpoint en el
callsite real de `audioDecResume` (`0x479F00`, o su(s) llamador(es) en
el ELF original) para confirmar si el hardware real lo invoca durante el
arranque de una película y con qué argumentos — pero esto es un paso
nuevo, todavía no solicitado ni ejecutado.

## 8. Rastreo de `audioDecResume`/`audioDecPause` — respuesta completa a la Sección 1-4 del plan, con una contradicción dinámica importante

### 8.1 `audioDecResume` — CERO referencias reales en todo el ELF (HECHO)

Búsqueda exhaustiva en todo `.text` (`0x100000-0x589880`, el único segmento
`LOAD` con contenido de archivo) por los tres mecanismos pedidos:

| Mecanismo | `audioDecPause` (0x479E80) | `audioDecResume` (0x479F00) |
|---|---|---|
| `jal` directo | 1 sitio: `0x479F8C` (dentro de `audioDecReset`) | **0 sitios** |
| Puntero crudo en datos | 0 | **0** |
| Construcción `lui/addiu` (materialización de la dirección como literal) | no aplica (ya cubierto por el `jal`) | **0** — ni un solo `addiu` con inmediato `0xDF00` (la mitad baja de `0x479F00`) en todo el binario |
| `jalr` (llamada indirecta) cerca del clúster de audio (`0x470000-0x480000`) | 9 sitios, todos en funciones no relacionadas (rango `0x472xxx`/`0x47bxxx`/`0x47cxxx`, código de efectos/enemigos) | mismo resultado — ninguno aterriza de forma verificable en `0x479F00` |

**Conclusión, HECHO**: `audioDecResume` es código genuinamente sin
referencias estáticas en el ELF de la EE, por los tres mecanismos que el
plan pedía comprobar. Esto no dice todavía SI el hardware real lo llega a
ejecutar (podría alcanzarse solo por una tabla de punteros calculada en
tiempo de ejecución que un escaneo estático no puede ver) — pero descarta
que sea un "olvido" trivial de PS2Recomp (RECOMP no puede inventar ni
omitir una `jal` que exista en el ELF; si no hay ninguna, no hay ninguna
que traducir).

### 8.2 `audioDecResume` completo desensamblado — confirma que SÍ activaría `blockTransfer` correctamente (HECHO)

```c
void audioDecResume(state) {
    changeInputVolume(0x7FFF);                       // "unmute" a volumen máximo
    // calcula tamaño alineado a 1024 desde *(state+76) [=counter76, el mismo tope que usa audioDecIsPreset]
    size = roundup1024(*(state+76));
    delta = *(state+84);                              // guardado por audioDecPause en su última ejecución
    sceSdRemote(cmd=0x80E0 /*BlockTrans*/, cmdArg0=0,
                cmdArg1=19 /*0b10011: dirección=3 (no STOP), bit LOOP puesto*/,
                arg4=*(state+72) /*base*/, arg5=size, arg6=*(state+72)+delta);
    *(state+44) = 2;   // pasa audioDecSendToIOP al modo 2 (el que sondea BlockTransStatus cada tick)
}
```

Con `arg4`/`arg5` presumiblemente no-cero en la práctica (base de buffer
real, tamaño real de 24576), esta llamada **activaría**
`blockTransfer.active=true` según el contrato ya revisado de
`Stubs/Audio.cpp::sceSdRemote` — es la única llamada de las 6 que existen
en todo el juego que lo haría (ver 8.3).

### 8.3 Mapa completo de los 6 sitios `sceSdRemote` en todo el juego (HECHO, exhaustivo)

| Dirección | Función contenedora | `cmd` | Efecto sobre `blockTransfer` |
|---|---|---|---|
| `0x479EB4` | `audioDecPause` | `0x80E0` BlockTrans, `cmdArg1=2` (STOP) | **vacía** `blockTransfer` (`active=false`) |
| `0x479EE8` | `audioDecPause` | `0x80D0` VoiceTrans | no toca `blockTransfer` |
| `0x479F48` | `audioDecResume` (sin referencias, 8.1) | `0x80E0` BlockTrans, `cmdArg1=19` (no STOP + LOOP) | **activaría** `blockTransfer` (`active=true`) si se ejecutase |
| `0x47A200` | `audioDecSendToIOP` (modo 2) | `0x8100` BlockTransStatus | solo avanza `blockTransfer.offset` si ya está activo |
| `0x47A5CC` | `changeInputVolume` | `0x8010` SetParam | no toca `blockTransfer` |
| `0x47A5E0` | `changeInputVolume` | `0x8010` SetParam | no toca `blockTransfer` |

No existe ninguna otra ruta hacia `sceSdRemote` en el juego. La única
llamada capaz de activar `blockTransfer` es la de `audioDecResume`
(8.1: sin referencias).

### 8.4 CONTRADICCIÓN dinámica — la hipótesis de "audioDecReset corre cada tick" queda REFUTADA por instrumentación real

Se instrumentó ENTRY de `audioDecPause`, `audioDecResume`, y
`sceSdRemote` (comando/args/`blockTransfer.active` antes-después). Build
limpio (`ps2_runtime.vcxproj` recompilado normal —solo 44 archivos, cero
código generado tocado—, más `cl.exe` directo para los 2 `.cpp`
generados afectados, más `_BuildLinkAction`: **0 `.cpp` generados
recompilados** en ambos pasos).

Corrida real: alcanzó el mismo punto de bloqueo por *backpressure* que en
la sección 7 (8 fotogramas decodificados, `[CDMODULE/MOVIE:chunk]` hasta
`offset=0xC0000` de `0x1A5C004`). **HECHO**: en toda esa ventana, **cero**
líneas `[AUDIO:pause]`, `[AUDIO:resume]` o `[AUDIO:sceSdRemote]` —
ninguna de las tres se ejecutó ni una sola vez.

Esto **contradice** la hipótesis estática previa (sección 7, "Siguiente
paso natural"): yo había concluido que `Movie_ctrl` llama a
`audioDecReset` (y por tanto a `audioDecPause`) en cada tick, gateado por
el mismo bit `0x4000` que `audioDecIsPreset` lee — y ese bit estuvo en 0
(clear) en las 10 lecturas de `[AUDIO:preset]`, lo que debería haber
significado "se llama". La evidencia dinámica dice que NO se llama nunca.
Mi trazado estático de esa parte de `Movie_ctrl` tenía un error o una
omisión que no he verificado todavía.

**HIPÓTESIS (no instrumentada, no confirmada)**: re-revisando el flujo de
`Movie_ctrl` entre el gate de `Task_execute` (`label_1ce738`) y el punto
donde se comprueba el bit `0x4000` (`0x1CE7D8`), TODOS los caminos
estáticos pasan primero por una zona de sondeo
(`videoDecFlush`/`videoDecIsFlushed`, símbolos reales `0x47B860`/
`0x47B970`) que reintenta vía `Task_sleep(1)` en un bucle que se
interrumpe con `runtime->eeCheckpointDue()` en cada vuelta. Si
`videoDecIsFlushed()` nunca devuelve distinto de cero (y el bit `0x10`
de las mismas `flags` tampoco se pone), ese bucle podría no terminar
nunca dentro de una sola invocación lógica de `Movie_ctrl` — cediendo
control al scheduler repetidamente sin llegar jamás a `0x1CE7D8`, lo que
explicaría exactamente el cero observado. **Esto reencuadra la posible
primera divergencia real: podría estar en `videoDecFlush`/
`videoDecIsFlushed` (capa de vídeo, no de audio) en vez de en la cadena
`audioDecReset`/`audioDecResume`.** No verificado todavía — necesita su
propia instrumentación antes de tratarse como hecho.

### Reclasificación

Sigue siendo, en su forma más amplia, **CASO A** (`Task_execute(3,
MpegMovieDecode)` nunca se llama), pero el mecanismo concreto cambia:
NO es (todavía demostrado) "`audioDecReset` resetea el contador cada
tick" — es, como mínimo, "el código nunca llega a esa parte de
`Movie_ctrl` en absoluto", con `videoDecFlush`/`videoDecIsFlushed` como
candidato principal sin confirmar.

## 9. `videoDecFlush`/`videoDecIsFlushed` — V3 confirmado: nunca se alcanzan, y se identifica un ciclo cerrado real

### 9.1 Reconstrucción exacta `0x1CE738-0x1CE7D8` (HECHO, verificado línea por línea, no resumido a priori)

```
label_1ce738: v0 = *(s0+0x18)
label_1ce73c: at = (v0 < 5)
  bnez at -> [delay a0=s0] -> SI (v0<5): goto label_1ce75c
  NO (v0>=5):
    v0 = *(s0+0x2FC) & 0x10
    beqz v0 -> [delay a1=sp+0x48] -> SI (bit0x10==0):
        if (eeCheckpointDue()) return;
        goto label_1ce5b4        // reinicia TODO el sub-loop de transferencia por tick
    NO (bit0x10==1): a0=s0
label_1ce75c: jal videoDecFlush(a0=s0)              @0x47B860
label_1ce764: bnez v0 -> [delay a2=2] -> SI (v0!=0): goto label_1ce78c
  NO (v0==0):
label_1ce76c: jal Task_sleep(a0=1)                   @0x19F9D0
label_1ce774: jal videoDecFlush(a0=s0)   [SEGUNDA llamada]
label_1ce77c: beqz v0 -> SI (v0==0):
      if (eeCheckpointDue()) return;
      goto label_1ce76c           // bucle sleep(1)+flush(s0)
  NO (v0!=0): a2=2
label_1ce78c: ... jal func_15A770(...) ...
label_1ce7a8: jal Task_sleep(a0=1)
label_1ce7b0: jal videoDecIsFlushed(a0=s0)          @0x47B970
label_1ce7b8: bnez v0 -> SI (v0!=0): goto label_1ce7d8
  NO (v0==0):
    v0 = *(s0+0x2FC) & 0x10
    bnez v0 -> SI (bit0x10==1): goto label_1ce7d8
    NO: jal Task_sleep(a0=1) -> CAE sin condición a label_1ce7d8
label_1ce7d8: [chequeo de audio]
```

Confirmado: **no hay bucle sin salida** al final (1ce7b8→1ce7d8 siempre
cae, con o sin `videoDecIsFlushed()`). Los dos únicos bucles reales
(`1ce750→1ce5b4` y `1ce77c→1ce76c`) están acotados por
`eeCheckpointDue()` (ceden, no giran infinito dentro de una sola llamada
C++).

### 9.2 `videoDecFlush`/`videoDecIsFlushed` completas (HECHO)

**`videoDecIsFlushed(state)`**:
```c
result = (sceMpegIsRefBuffEmpty(state+0x278) != 0) ? 1 : 0;   // HLE real, MPEG.cpp:2551
if (!result) result = (func_47B920(state) != 0) ? 1 : 0;      // función local sin resolver
return result;
```
`sceMpegIsRefBuffEmpty` (`Stubs/MPEG.cpp:2551-2559`) devuelve
`playback.decodedFrames.empty() ? 1 : 0` — **el mismo `decodedFrames`
que ya sabíamos que crece a 8 y nunca se vacía** (nadie llama
`sceMpegGetPicture`, sección 5). Es decir: una vez decodificado el
primer fotograma, esta vía queda permanentemente en `0`.

**`videoDecFlush(state)`**: hace matemática de coma flotante y llama a
`func_47AE90(state, ...)` para obtener dos valores de salida (`a1`,
`a3`); si `a1+a3 < 4` continúa (máscara de bits, llama a
`func_3FE450`/`func_47AF80`, devuelve 1); si `a1+a3 >= 4` **devuelve 0
inmediatamente sin hacer nada más**. No depende de `decodedFrames` de
forma obvia — depende de `func_47AE90`, sin resolver todavía.

### 9.3 Instrumentación y corrida — V3 confirmado dinámicamente (HECHO)

Instrumentado: `[VIDEO:flush] ENTRY`/`result=`, `[VIDEO:isFlushed]
sceMpegIsRefBuffEmpty=`/`result=`, y en `Movie_ctrl`:
`[MOVIE:flush-loop] ENTER`/`sleep`/`EXIT`, `[MOVIE:post-flush] reached
0x1CE7D8`. Build limpio (`cl.exe` directo para los 3 `.cpp` generados
afectados + `_BuildLinkAction`: 0 código generado recompilado en ambos
pasos).

Corrida real, mismo punto de bloqueo por *backpressure* que siempre
(8 fotogramas, `[CDMODULE/MOVIE:chunk]` hasta `offset=0xC0000`).
**HECHO**: `[AUDIO:preset]` sigue apareciendo repetidamente (10 veces,
valores idénticos) — confirma que el código anterior a `label_1ce738`
se re-ejecuta muchas veces — pero **cero** líneas
`[MOVIE:flush-loop]`/`[MOVIE:post-flush]`/`[VIDEO:flush]`/
`[VIDEO:isFlushed]` en toda la corrida.

**Clasificación: V3 — `videoDecFlush` nunca se alcanza.** La única
explicación consistente con "`audioDecIsPreset` se reevalúa 10 veces con
valores idénticos, pero `videoDecFlush` jamás se llama" es que
`Movie_ctrl` está atrapado en el bucle `label_1ce750→label_1ce5b4`
(bit `0x10` de flags siempre en `0`), que **reinicia todo el sub-loop
de transferencia por tick** — incluida la llamada a
`audioDecSendToIOP`/`audioDecIsPreset` — en cada vuelta, sin llegar
jamás a `label_1ce75c`.

### 9.4 Quién debería poner el bit `0x10` — ciclo cerrado real (HECHO, búsqueda exhaustiva)

Búsqueda de todo `sw reg, 0x2FC(base)` en el juego completo: **32
sitios**, de los cuales 4 están dentro de `Movie_set`
(`0x1CE37C`, `0x1CE408`, `0x1CE448`, `0x1CE474`) — ninguno pone el bit
`0x10` (ponen `0x400`, `0x200`(condicional), `0xC00`(condicional),
`0x4000`(condicional) respectivamente; desensamblado completo de
`Movie_set` revisado línea por línea). Búsqueda del patrón
lectura-modificación-escritura específico (`lw` de `0x2FC` + `ori
reg,reg,0x10` + `sw` de vuelta a `0x2FC` del mismo puntero) en todo
`.text`: **un único sitio en todo el juego: `0x47AA7C`**, dentro de
`MpegMovieDecode` (su rama de reinicio tras `sceMpegInit()`, sección
6.5).

**Conclusión**: el bit `0x10` que `Movie_ctrl` necesita para escapar del
bucle de reintento y llegar a `Task_execute(3, MpegMovieDecode)` **solo
se pone dentro de la propia `MpegMovieDecode`** — la función que nunca
se ejecuta precisamente porque ese bucle no la deja llegar ahí. Es un
ciclo cerrado real y demostrado por código estático, no una hipótesis.
No se ha comprobado todavía si existe una inicialización masiva
(`memset`/plantilla) del estado de movie en algún punto anterior al
arranque que pudiera poner el bit `0x10` por fuera de este mecanismo
(ABIERTO) — pero no se ha encontrado ningún candidato en 32 sitios
`sw`-a-0x2FC ni en `Movie_set`/`Movie_task_init`.

## 10. `state+0x18` medido explícitamente en RECOMP — HECHO, y por qué probablemente no es la causa raíz

Instrumentado `[MOVIE:gate18]` justo después de que `0x1CE738` cargue
`v0` y `0x1CE73C` calcule `lt5`, mismo PC que la captura manual en
PCSX2 original (`s0=0x0087D680, v0=0`). Build: solo
`Movie_ctrl_0x1ce510.cpp` vía `cl.exe` directo + `_BuildLinkAction` (0
código generado recompilado).

**HECHO**: en RECOMP, `count18` (`*(state+0x18)`) es del orden de
decenas de millones (`27639812` → `27574276` → ... decreciendo en pasos
de exactamente `0x10000` por tick) durante la ventana de 8 fotogramas.
Cruzado directamente contra `[CDMODULE/MOVIE:chunk] remaining=`: los
valores **coinciden exactamente** (`count18=27639812=0x1A5C004`
coincide con el `remaining` reportado justo después). `flags=0x8000400`
(bits `0x400` y `0x08000000` puestos, bit `0x10` siempre clear) —
consistente con la sección 9: `lt5=0` en todas las observaciones, cae
al chequeo de bit `0x10` (nunca puesto) y hace loop-back a
`label_1ce5b4`, nunca alcanza `videoDecFlush`.

**Identidad de `state+0x18` (HECHO, por escritores reales)**: 3 sitios
en todo el juego escriben ese offset dentro de `Movie_set`/`Movie_ctrl`
(único rango relevante, de 439 sitios totales de `sw reg,0x18(base)` en
el juego completo — offset genérico usado por muchas structs no
relacionadas):

| Dirección | Función | Efecto |
|---|---|---|
| `0x1CE450` | `Movie_set` | `*(state+0x18) = *(state+0x0)` — inicialización al arrancar el movie |
| `0x1CE588` | `Movie_ctrl` | `*(state+0x18) = *(state+0x0)` — re-sincronización condicional (gateada por el bloque `CdReadCheck`/bit `0x400` al inicio de `Movie_ctrl`) |
| `0x1CE6C0` | `Movie_ctrl` | `*(state+0x18) -= consumido` — decremento por tick, justo después de `sceMpegDemuxPssRing` |

**HIPÓTESIS (no confirmada, pero fuertemente soportada por los tres
escritores anteriores)**: `*(state+0x18)` es un contador de **bytes
restantes del PSS actual**, inicializado a `totalSize` y decrementado
cada tick. Con una película de ~27 MB, es matemáticamente esperable que
esté en millones durante casi toda la reproducción y solo baje de 5
cerca del final. La captura manual original (`v0=0`) es compatible con
esto si se tomó cerca del final de una PSS corta, entre películas, o en
un momento distinto al que mide esta ventana de RECOMP — **no
necesariamente con una divergencia de traducción en cómo se calcula
`state+0x18`**. No confirmado todavía si el hardware real también hace
avanzar este contador de la misma forma durante reproducción media
(requiere el watchpoint WRITE propuesto).

## 11. `audioDecResume` — vía de alcance RESUELTA; corrección de orden causal

### 11.1 Cadena real (HECHO, confirmado dinámicamente por PCSX2 + decodificación de opcode)

**Retractada por completo la afirmación anterior** ("audioDecResume no
tiene vía de alcance estática conocida"). PCSX2, parado dentro de
`audioDecResume`, mostró `RA=0x0047A974`. Verificado:

```
0x47A96C (dentro de MpegMovieDecode, 0x47A760-0x47AA9C): jal audioDecStart
0x479F70 (audioDecStart):                                 J audioDecResume
  -- word=0x0811E7C0, opcode=0x02=J (NO 0x03=JAL) -- confirmado por
     decodificación de campos con script, no a ojo. Tail-call puro:
     NO toca $ra.
0x479F00 (audioDecResume) hereda ra=0x47A974 (el de MpegMovieDecode)
0x479F54: state+0x2C (mode) = 2
jr ra -> retorna DIRECTAMENTE a 0x47A974 dentro de MpegMovieDecode
```

`RA=0x0047A974` coincide EXACTO con lo predicho por el análisis
estático del delay slot de `0x47A96C`. Esto explica por qué buscar
`jal audioDecResume` o punteros crudos a `0x479F00` dio siempre cero: la
única vía real es un `J` (no `JAL`) de tail-call desde `audioDecStart`,
que a su vez SÍ se llama con `jal` normal — pero solo desde DENTRO de
`MpegMovieDecode`, no desde ninguna tabla ni callback.

### 11.2 Corrección de orden causal (HECHO, cambia la interpretación de CASO A)

El call site `0x47A96C` está en el bloque de post-procesado de
`MpegMovieDecode`, **después** de un `sceMpegGetPicture` exitoso
(0x47A958-0x47A964: relee `flags16` del mismo puntero anidado que usa
`audioDecIsPreset`; llama `audioDecStart`→`audioDecResume` SOLO si
`flags16 & 0x4000` está **CLEAR** en ese punto — condición opuesta a la
vía rápida de `audioDecIsPreset`, que necesita ese mismo bit **SET**).

Por tanto:

```
ANTES se pensaba:           mode 0->2 (vía audioDecResume)
                                  -> permite Task_execute(MpegMovieDecode)

REAL (demostrado):          Task_execute(MpegMovieDecode) ya ocurrio
                                  -> MpegMovieDecode corre, llega a
                                     sceMpegGetPicture exitoso
                                  -> audioDecStart -> audioDecResume
                                  -> mode 0->2 (CONSECUENCIA, no causa)
```

**`mode=2` es downstream de que `MpegMovieDecode` ya estuviera
ejecutándose — no la causa de que `Task_execute` lo lanzara.** CASO A
(RECOMP: `Task_execute` nunca se llama) sigue siendo el HECHO primario
confirmado dinámicamente, pero la explicación original ("`audioDecIsPreset`
devuelve 0 porque `mode` nunca llega a 2") queda **incompleta**: el
PRIMER éxito de `audioDecIsPreset` en ORIGINAL (el que dispara
`Task_execute`) no puede depender de `mode=2`, porque eso ocurre
DESPUÉS, dentro de la propia `MpegMovieDecode`.

### 11.3 Candidato principal para el primer éxito (HIPÓTESIS, no confirmada)

> **RETRACTADO (ver sección 12.2)** — dos sesiones dinámicas
> independientes de PCSX2 (Prompt P3.3), con breakpoints en `0x47A134`
> (único código de la vía rápida bit4000) y `0x1CE6EC` (retorno de
> `audioDecIsPreset` en `Movie_ctrl`) armados simultáneamente **desde
> antes** de que arrancara cada sesión, muestran que `0x1CE6EC` se
> alcanza primero con `v0=1` mientras `0x47A134` **nunca se ejecuta**.
> La hipótesis de esta subsección (vía rápida `bit4000` como mecanismo
> del primer éxito) queda descartada como mecanismo de arranque real; la
> vía B (`+0x58>=+0x4C`) es la confirmada. Contenido conservado como
> historial, no borrado.

`MoviePcmInit` inicializa `+0x58=0`, `+0x4C=0x6000` (`0 < 0x6000`, la
vía lenta de `audioDecIsPreset` daría `0`). Por tanto el candidato
principal para el PRIMER retorno `v0=1` de `audioDecIsPreset` (el que
dispara `Task_execute` en `Movie_ctrl` `0x1CE72C`) es la **vía rápida**:
`flags16 & 0x4000` **SET** en ese momento — no la comparación
`+0x58>=+0x4C`. No confirmado dinámicamente todavía.

### 11.4 Siguiente comparación propuesta (no ejecutada)

> **EJECUTADA (Prompt P3.3) — ver sección 12.1**. La secuencia completa
> se capturó dinámicamente en ORIGINAL: `+0x58` avanza `0→0x3C00→0x5C00→
> 0x6000` mediante tres escrituras reales de `audioDecSendToIOP`
> (`0x47A2E8`), inmediatamente seguidas por `0x1CE6EC` con `v0=1`. Esto
> confirma la vía B, no la vía A (ver retractación en 11.3).

Capturar en ORIGINAL, en la MISMA secuencia que llega a
`Task_execute` (`0x1CE72C`):

- `0x1CE6E4` (retorno de `audioDecIsPreset` en `Movie_ctrl`): `v0`
- `flags16` y `bit4000` exactos en ese instante (mismos que lee
  `audioDecIsPreset` internamente)
- `+0x58` y `+0x4C` en ese instante
- Confirmar después `ENTRY` de `MpegMovieDecode` (`0x47A760`)

Comparar esa secuencia exacta con RECOMP. Puede localizar la primera
divergencia real antes de `mode`, `+0x58`, `GetPicture` o *backpressure*.

### 11.5 Limitación descubierta en el proyecto Ghidra (HECHO, documentado, no resuelto)

Al intentar representar esta cadena en Ghidra, se descubrió que varias
funciones clave tienen el **cuerpo de función truncado** en la base de
datos (p. ej. `MpegMovieDecode` registrada como `0x47A760-0x47A764`,
4 bytes, en vez de los 828 reales; mismo problema en `Movie_set`,
`Movie_ctrl`, `audioDecSendToIOP`, `sendToIOP`, `sendToIOP2area`,
`StrPcmCallBack`). Los nombres/símbolos son correctos, pero
`getFunctionContaining()` no reconoce direcciones intermedias como
pertenecientes a esas funciones. Se intentó reparar con
`createFunction()` (sigue flujo de control automáticamente) — **no
tuvo efecto**, los tamaños de cuerpo no cambiaron. La causa raíz parece
estar en el motor de desensamblado/seguimiento de flujo de Ghidra para
extensiones R5900 (MMI/COP2), no en la configuración de boundaries —
fuera de alcance de esta tarea de documentación. Los comentarios EOL
añadidos en direcciones concretas (que no dependen de la agrupación en
función) siguen siendo válidos y navegables por dirección/bookmark.

## 12. Serie P3 — escalera diferencial ORIGINAL vs RECOMP hasta la primera divergencia real

Continuación directa de la sección 11. Objetivo: partiendo de la cadena
de arranque ya demostrada dinámicamente en ORIGINAL, localizar el primer
punto en el que RECOMP diverge. Resultado: divergencia encontrada y
confirmada por dos vías independientes (dinámica + estática), con causa
raíz identificada a nivel de código, no solo de síntoma.

### 12.1 Cadena de arranque ORIGINAL — DEMOSTRADA DINÁMICAMENTE de punta a
punta (Prompt P3.3)

Capturas secuenciales reales en PCSX2 (sesión única, breakpoints
sucesivos, sin fabricar el orden):

```
0x47A0D0 (audioDecEndPut, escritura de mode=1):
    a0=0x87D680  v1=1  ra=0x47A6A8   <- ra apunta dentro de StrPcmCallBack

0x47A2E8 (audioDecSendToIOP, retorno con delta):
    hit 1: delta=0x3C00   -> +0x58: 0x0000 -> 0x3C00
    hit 2: delta=0x2000   -> +0x58: 0x3C00 -> 0x5C00
    hit 3: delta=0x0400   -> +0x58: 0x5C00 -> 0x6000

0x1CE6EC (retorno de audioDecIsPreset en Movie_ctrl, justo tras el
          tercer hit anterior):
    v0=1   <- PRIMER éxito real, dispara Task_execute(3, MpegMovieDecode)

(continuación, ya con el movie corriendo)
    +0x58 sigue avanzando: 0x6000 -> 0x7800 -> 0x9800 -> 0xB400
```

**Lectura, HECHO (demostrado dinámicamente, no inferido)**: el mecanismo
real de arranque en ORIGINAL es exactamente la cadena estática ya
predicha en secciones anteriores:

```
StrPcmCallBack (ra=0x47A6A8)
  -> audioDecEndPut         escribe mode=1        @0x47A0D0
  -> audioDecSendToIOP      avanza +0x58 (modo 1)  @0x47A2E8 (x3)
  -> +0x58 alcanza 0x6000 = +0x4C (MoviePcmInit lo fija en 0x6000)
  -> audioDecIsPreset       PATH B: +0x58>=+0x4C -> v0=1   @0x1CE6EC
  -> Movie_ctrl             Task_execute(3, MpegMovieDecode)
```

Esto confirma con evidencia dinámica completa (no solo estática) la vía
B como mecanismo real, y dinamita cualquier necesidad de invocar
`audioDecResume`/`mode=2`/`bit4000` como precondición del primer éxito
— ambos son posteriores (sección 11.2).

### 12.2 RETRACTACIÓN formal — Prompt P3.1 (vía rápida `bit4000`) queda
descartada como mecanismo de arranque

**RETRACTADO.** La sección 11.3 planteaba, con una sola captura PCSX2
(`0x47A12C`/`0x47A14C`, `v1=0`), que el primer éxito de
`audioDecIsPreset` usaba la vía rápida `flags16&0x4000`. El Prompt P3.3
repitió la medición con **ambos** breakpoints (`0x47A134`, único código
de la vía rápida, y `0x1CE6EC`, el retorno) armados **desde antes** del
arranque de la sesión, en **dos** sesiones independientes: en ambas,
`0x1CE6EC` se alcanzó primero con `v0=1` y `0x47A134` **nunca llegó a
ejecutarse**. Conclusión: la vía rápida puede ser real para alguna OTRA
fase del juego (no descartada en general), pero **no** es el mecanismo
del primer éxito que dispara `Task_execute`. Contenido de 11.3 e 11.4
conservado íntegro como historial (ver anotaciones insertadas ahí).

### 12.3 Prompt P3.2 — identidad de `state+0x70`: CORRECTA pero LATERAL

Investigación disparada por una afirmación mía que el usuario rechazó
explícitamente sin evidencia extraordinaria ("el kernel PS2 devuelve un
puntero como ID de semáforo"). Reconstrucción literal del código
generado, no interpretativa:

- **Error propio identificado y corregido**: había leído
  `0x1CE3EC: jal MovieBufferInit` / `0x1CE3F0: sw v0,0x70(s0)` como
  "guarda el valor de retorno". El código generado marca explícitamente
  esa segunda instrucción como `(Delay Slot)` del `jal` — captura `v0`
  de **antes** de la llamada, no el retorno. Error real de mi parte,
  detectado solo porque el usuario exigió la reconstrucción literal.
- **Identidad real de `state+0x70`** (HECHO, por desensamblado):
  `v0 = 0x5072B0 + (halfword@state+0x6C << 2)` — una dirección calculada
  hacia una tabla estática real, respaldada por el ELF (no BSS). Todas
  las entradas de esa tabla tienen `flags16 = 0x8000` con `bit 0x4000`
  siempre clear — explica y valida (no como bug) el `flags16=0x8000`
  observado tanto en ORIGINAL como en RECOMP.
- **`CreateSema`/`MovieBufferInit`**: su valor de retorno real se
  calcula pero se **descarta por completo** — `Movie_set` lo sobrescribe
  inmediatamente después de la llamada. **Descartada en su totalidad
  como causa** de cualquier divergencia de `state+0x70` ni de ninguna
  otra parte de esta cadena.

Este hallazgo es CORRECTO y queda confirmado, pero es **lateral**: no
forma parte de la cadena causal real de arranque (sección 12.1).

### 12.4 Separación permanente `mode=1` (upstream) vs `mode=2` (downstream)

Estos dos valores de `*(state+0x2C)` **no deben volver a tratarse como
un único "problema de modo de audio"** — tienen orígenes, funciones y
posiciones causales completamente distintas:

| | `mode=1` | `mode=2` |
|---|---|---|
| Escritor único | `audioDecEndPut` (`0x47A0D0`) | `audioDecResume` (`0x479F54`, vía tail-jump desde `audioDecStart`) |
| Alcanzable solo desde | `StrPcmCallBack` (callback de demux, ver 12.5) | `MpegMovieDecode`, **después** de un `sceMpegGetPicture` exitoso |
| Posición causal | **UPSTREAM** — precondición real del primer éxito de `audioDecIsPreset` (vía B) | **DOWNSTREAM** — consecuencia de que `MpegMovieDecode` ya estuviera corriendo (sección 11.2) |
| Relevante para la divergencia actual | **SÍ** — es la cadena bloqueada (sección 12.5-12.6) | No — nunca se alcanza en RECOMP porque `MpegMovieDecode` nunca arranca, pero no es la causa |

### 12.5 Prompt P3.4 — escalera diferencial A-K, RECOMP vs ORIGINAL

Instrumentación quirúrgica añadida (temporal, sin commit) en los puntos
exactos de la cadena de 12.1, dentro de `MPEG.cpp` (`Stubs/Kernel`) y
`StrPcmCallBack_0x47a600.cpp`/`audioDecEndPut_0x47a090.cpp`
(`analysis/local/symtabfirst/generated/`, los archivos que realmente
compila el árbol de build activo — ver nota metodológica en 12.6).
Corrida real, ISO+ELF reales, hasta el mismo punto de bloqueo permanente
ya conocido (10× `[AUDIO:preset] ... result=0`).

| Fila | Qué demuestra | ORIGINAL (P3.1-P3.3) | RECOMP (P3.4) |
|---|---|---|---|
| A | demux detecta el PES de audio | HECHO (implícito en la cadena de 12.1) | **SÍ** — `[MPEG:audio-pes] streamId=0xbd`, repetido |
| B | `queueStreamCallbackEvent` crea evento `streamType=2` (ADPCM) | — (no aplica igual en ORIGINAL, es mecanismo propio de RECOMP) | **SÍ** — `[MPEG:callback-event] streamType=2 ... ` |
| C | `matchingStreamCallbacks` encuentra `StrPcmCallBack` registrado | — | **SÍ** — mismo evento, `matches=1` (streamType=1/PCM da `matches=0`, correcto: el juego solo registra ADPCM) |
| **D** | `dispatchGuestStreamCallback` llega a `queueInvocation` | (equivalente real: el callback SÍ se invoca, sección 12.1 lo prueba indirectamente vía `ra=0x47A6A8`) | **NO — PRIMERA DIVERGENCIA REAL.** Ver 12.6 |
| E | `StrPcmCallBack` ENTRY se ejecuta en guest | SÍ (implícito, `ra` capturado dentro de la función) | NO — consecuencia necesaria de D |
| F | llega a `audioDecEndPut` | SÍ | NO — consecuencia de E |
| G | escribe `mode=1` | SÍ (`0x47A0D0`, `v1=1`) | NO — consecuencia de F |
| H | `audioDecSendToIOP` alcanza `0x47A2E8` con delta≠0 | SÍ (3 hits, deltas 0x3C00/0x2000/0x400) | NO — consecuencia de G |
| I | `+0x58 > 0` | SÍ | **NO — ya confirmado por instrumentación previa** (`counter88=0` congelado, 10/10 lecturas, secciones 7-11) |
| J | `+0x58 >= 0x6000` | SÍ | NO — igual que I |
| K | `audioDecIsPreset` PATH B → `v0=1` | SÍ (`0x1CE6EC`, `v0=1`) | NO — `[AUDIO:preset] ... result=0` × 10, idéntico en todas las corridas |

**Filas I-K ya estaban confirmadas como "NO" en RECOMP desde secciones
7-11 (instrumentación `[AUDIO:preset]` preexistente); esta ronda añadió
A-D (nuevas) y confirmó E-H como consecuencia necesaria de D, no como
divergencias independientes.**

### 12.6 Causa raíz de la fila D — demostrada dinámica Y estáticamente

**Primer intento de instrumentación (nota metodológica honesta)**: la
primera versión de esta ronda usaba un contador compartido con tope 10
para el log de fila D, y por eso las primeras 10 invocaciones de
`dispatchGuestStreamCallback` observadas correspondían a **otro**
callback (`func=0x47afe0`, no identificado, casi seguro el de vídeo),
que agotó el tope antes de que se intentara despachar
`StrPcmCallBack` ni una sola vez. Corregido usando contadores separados
por función (`func==0x47a600` sin tope, el resto con tope bajo) antes de
sacar ninguna conclusión. Se verificó además, antes de reconstruir, que
el árbol de build realmente activo (`analysis/local/symtabfirst/build`,
confirmado por timestamp exacto del `.exe` generado) usa
`analysis/local/symtabfirst/generated/*.cpp` y no
`recomp/generated_active/*.cpp` (un árbol de build distinto y no usado
en este ciclo) — verificación que resultó ser una falsa alarma, pero se
documenta aquí porque fue una duda real, resuelta con evidencia, no
descartada por suposición.

**Resultado, con instrumentación corregida (HECHO, dinámico, 16/16
intentos consecutivos, cero excepciones)**:

```
[MPEG:callback-dispatch-entry] PCM#1 func=0x47a600 rdram=1 callerCtx=1 runtime=1 hasFunction=1
[MPEG:callback-dispatch-mallocfail] PCM#1 heapBase=0x0 heapEnd=0x0 heapLimit=0x0 requestedSize=32
... (idéntico en PCM#2 .. PCM#16)
```

La guarda de entrada de `dispatchGuestStreamCallback` (`MPEG.cpp:1647`)
pasa por completo: `rdram`/`callerCtx`/`runtime` no nulos,
`callback.func == 0x47a600` (no cero), `runtime->hasFunction(0x47a600)
== true` (la función SÍ está registrada en la tabla densa de RECOMP,
confirmado también estáticamente — ver más abajo). El fallo real está
en el siguiente paso: `runtime->guestMalloc(kMpegCallbackDataSize=32,
16u)` (`MPEG.cpp:1652`) **devuelve `0` siempre**, porque
`guestHeapBase()`/`guestHeapEnd()`/`guestHeapLimit()` son **los tres
`0x0`** — el guest heap sintético de RECOMP no tiene ninguna capacidad
en absoluto.

**Causa raíz, demostrada por código (`ps2_runtime.cpp`)**:

```
PS2Runtime::resetGuestHeapLocked(guestBase, guestLimit):   línea 1599
    base = align(clamp(guestBase))
    limit = clampGuestHeapLimit(guestLimit)
    if (limit <= base) { base = align(clamp(m_guestHeapSuggestedBase)); limit = clampGuestHeapLimit(0); }
    if (limit <= base) { base = 0; limit = 0; }        <- COLAPSO, línea ~1617
    m_guestHeapBlocks.clear();
    if (limit > base) m_guestHeapBlocks.push_back({base, limit-base, true});   <- nunca se ejecuta si colapsó
```

`m_guestHeapSuggestedBase` se fija una sola vez, durante la carga del
ELF (`ps2_runtime.cpp:920-932`):

```
maxLoadedRdramEnd = máximo end (vaddr+memsz) de todos los PT_LOAD no-scratchpad
paddedEnd = maxLoadedRdramEnd + kGuestHeapSafetyPad(0x1000)
suggestedHeapBase = align(paddedEnd, 16)
m_guestHeapSuggestedBase = min(suggestedHeapBase, kGuestHeapHardLimit=0x01F00000)
```

**Verificación estática independiente, directa sobre el ELF real**
(`original/SLES_503.58`, sin pasar por RECOMP en absoluto — parseo
manual de program headers):

```
16 program headers; 15 son PT_LOAD con memsz>0:
  seg0:  vaddr=0x100000  memsz=0x8f8480  end=0x9f8480   <- código/datos reales del juego
  seg1..seg14 (14 segmentos IDÉNTICOS): vaddr=0x1f00000  memsz=0x100  end=0x1f00100

maxLoadedRdramEnd = 0x1f00100   (viene de los 14 segmentos diminutos, no del código real)
paddedEnd         = 0x1f01100   (+ kGuestHeapSafetyPad)
suggestedHeapBase  = 0x1f01100   (alineado)
kGuestHeapHardLimit = 0x1f00000
0x1f01100 > 0x1f00000  =>  m_guestHeapSuggestedBase se clampa a EXACTAMENTE 0x1f00000 (línea 929)
```

Con `m_guestHeapSuggestedBase == kGuestHeapHardLimit` exactamente,
`resetGuestHeapLocked` entra en `limit<=base` dos veces seguidas (ambas
ramas producen `base==limit==0x1f00000`) y colapsa a `base=0,limit=0`
de forma determinista — **confirmado por cálculo con los bytes reales
del ELF, sin necesidad de ejecutar nada**, coincidiendo exactamente con
el `heapBase=0x0 heapEnd=0x0 heapLimit=0x0` observado en runtime.

**Naturaleza del bug (HECHO por lectura de código, no HIPÓTESIS)**: esto
es un problema de infraestructura de RECOMP (heurística de
dimensionamiento del guest heap sintético en `ps2_runtime.cpp`), **no**
una divergencia de traducción MIPS/R5900 ni un bug en ningún código del
juego. `guestMalloc` queda con capacidad cero para **cualquier**
llamador, de cualquier tamaño, durante toda la vida del proceso — el
fallo en `dispatchGuestStreamCallback`/`StrPcmCallBack` es simplemente
el primer punto donde esa falla tiene una consecuencia observable y
grave (bloquea el arranque de `mode=1`, que bloquea todo lo demás).

**ABIERTO → RESUELTO en el Prompt P3.5 (sección 13.1)**: los 14
segmentos son marcadores de un sistema de **overlays** del linker
(ee-gcc/SN Systems), confirmados por nombre (`t_sce_at`, `t_movie`,
`t_snd`, etc.) y por los símbolos `_t_XXX_segment_start/end` que el
propio linker emite — no son datos reales ni un artefacto accidental.
Ver sección 13 para el análisis completo y el diseño de fix propuesto
(no implementado).

### 12.7 Primera divergencia actual — resumen ejecutable

- **Último nodo igual entre ORIGINAL y RECOMP**: fila C — `MPEG.cpp`
  detecta el PES de audio, genera el evento `streamType=2` y encuentra
  `StrPcmCallBack` como callback registrado que coincide (`matches=1`).
  Todo esto reproduce fielmente el comportamiento real del juego.
- **Primer nodo distinto**: fila D — `dispatchGuestStreamCallback`
  (`MPEG.cpp:1641-1729`) nunca llega a `queueInvocation`.
- **Función/línea exacta responsable**: `PS2Runtime::guestMalloc`
  (`ps2_runtime.cpp:1779-1784`) → `allocateGuestBlockLocked`
  (`ps2_runtime.cpp:1658-1730`, devuelve `0` en la línea 1729 porque
  `m_guestHeapBlocks` está vacío) ← causa raíz real en
  `PS2Runtime::resetGuestHeapLocked` (`ps2_runtime.cpp:1599-1631`,
  colapso a `base=0,limit=0`) ← disparado por el cómputo de
  `m_guestHeapSuggestedBase` durante la carga del ELF
  (`ps2_runtime.cpp:786-932`).
- **Por qué las filas E-K son consecuencia y no causas independientes**:
  cada una depende estrictamente de que la anterior haya ocurrido
  (E necesita que D encole la invocación; F necesita que E se ejecute;
  ... K necesita que G haya puesto `mode=1`). Como D nunca ocurre, todas
  las siguientes están lógicamente forzadas a no ocurrir — no son
  evidencia adicional de divergencias distintas, son la propagación
  esperada de un único fallo.
- **Hipótesis abiertas**: (1) por qué el ELF tiene 14 `PT_LOAD`
  diminutos en `0x1f00000` (sección 12.6, ABIERTO); (2) si
  `kGuestHeapHardLimit`/la heurística de `maxLoadedRdramEnd` deberían
  ignorar segmentos `PT_LOAD` con `memsz` por debajo de algún umbral, o
  si el guest heap debería ubicarse en otra región de RDRAM
  independiente del "final de los segmentos cargados" — ninguna de las
  dos se ha investigado ni se propone una solución aquí (fuera de
  alcance: "localizar, no arreglar").
- **Siguiente experimento mínimo, si se retomara esta investigación**:
  no haría falta ninguno adicional para *localizar* la divergencia —
  ya está cerrada con doble evidencia (dinámica + estática determinista
  desde los bytes del ELF). El siguiente paso natural, cuando se decida
  abordar un fix, sería decidir la estrategia de dimensionamiento del
  guest heap (opciones: excluir segmentos `PT_LOAD` triviales del
  cómputo de `maxLoadedRdramEnd`; reservar el guest heap en una región
  fija independiente del layout del ELF; subir `kGuestHeapHardLimit`) —
  pero eso es una decisión de diseño de infraestructura, no fue pedida
  en esta iteración y no se ha tomado ninguna.

**Resultado Prompt P3.4 — RECOMP_CALLBACK_FIRST_DIVERGENCE**

## 13. Prompt P3.5 — diseño de fix para el layout del guest heap (sin implementar)

Continuación directa de la sección 12 (no la reescribe). Objetivo:
determinar la estrategia CORRECTA y general para reservar el guest heap
sintético sin colisionar con memoria real del juego — no un parche
específico para DMC. **No se ha implementado ni modificado ningún
código de infraestructura en esta sección.**

### 13.1 Los 14 `PT_LOAD` altos — identidad completa (HECHO, por lectura directa del ELF)

Los 16 program headers reales, con todos los campos:

| # | offset | vaddr=paddr | filesz | memsz | flags | align |
|---|---|---|---|---|---|---|
| 0 | 0x280 | 0x100000 | 0x489900 | 0x8F8480 | 7 (RWX) | 0x80 |
| 1-14 | 0x489B80 (**los 14 comparten el mismo offset de archivo**) | 0x1F00000 | **0x0** | 0x100 | 7 (RWX) | 0x80 |
| 15 | 0x489B80 | 0x1F00100 | 0x0 | **0x0** | 6 (RW) | 0x10 |

Los 14 segmentos 1-14 son **byte-por-byte idénticos** en sus 4 campos
numéricos relevantes (`vaddr`, `filesz=0`, `memsz=0x100`, `flags`) —
comparten literalmente el mismo `p_offset` de archivo (`0x489B80`, el
inicio de `.shstrtab`, es decir: **no hay ningún byte de contenido real
detrás de ninguno de ellos**, ni siquiera coincidentalmente).

Las tablas de secciones y símbolos identifican qué son **sin
ambigüedad** — no son basura ni relleno del linker:

```
sec 6-19: t_sce_at, t_rck, t_om_set, t_event, t_inf, t_scr_at,
          t_eff_db, t_mdview, t_light, t_c_main, t_cam, t_snd,
          t_flr, t_movie          (14 secciones, TODAS addr=0x1F00000)
sec 20:   heap                     addr=0x1F00100  flags=RW (sin X)
```

Y los símbolos de límite generados por el propio linker lo confirman
explícitamente, para cada una de las 14 (ejemplo con `t_movie`):

```
_t_movie_segment_start = 0x1F00000
_t_movie_text_start    = 0x1F00040   _t_movie_text_size  = 0xC0
_t_movie_text_end      = 0x1F00100
_t_movie_data_start/end = 0x1F00100  _t_movie_data_size  = 0x0
_t_movie_bss_start/end  = 0x1F00100  _t_movie_bss_size   = 0x0
_t_movie_segment_end    = 0x1F00100
```

**Conclusión, HECHO**: esto es el patrón estándar de **linker de
overlays de PS2** (ee-gcc/SN Systems): 14 módulos de overlay
mutuamente excluyentes (`t_sce_at`=tabla de actores de escena,
`t_movie`=el propio overlay de movie, `t_snd`=sonido, `t_cam`=cámara,
`t_light`=iluminación, etc. — nombres de subsistemas reales del motor
de DMC), todos mapeados a la **misma ventana de dirección**
(`0x1F00000`-`0x1F00100`, 256 bytes), cargados **uno a la vez** desde
CD en tiempo de ejecución, sustituyéndose entre sí. En este ELF base no
llevan contenido real (`filesz=0`, `data_size=0`, `bss_size=0`) — es
solo la reserva/ventana, no datos. **No se solapan entre sí** (están
literalmente en la misma dirección porque son alternativas, no datos
simultáneos) y **no representan memoria real ocupada** en el sentido
de "bytes que el juego necesita conservar ahí permanentemente" — pero
sí representan una **ventana de direcciones reservada por diseño**, que
el juego llenará bajo demanda.

Justo después (`0x1F00100`), la sección `heap` (símbolos `end`/`_end`
del runtime C estándar de ee-gcc/newlib) marca el inicio del heap C
"pequeño" del juego, y `_stack = 0x1FFE000` marca el puntero de pila
inicial — ambos, `0x1F00100` y `0x1FFE000`, están dentro del último
1MB de los 32MB de RAM.

### 13.2 Por qué existe `kGuestHeapHardLimit = 0x01F00000` (HIPÓTESIS bien fundamentada, sin rastro documental)

**HECHO**: no existe ningún comentario en el código, ni mensaje de
commit con justificación de diseño, para esta constante. `git log -S
"kGuestHeapHardLimit"` en el vendor apunta al commit que la introdujo
(`8f74733`, "refactor analyzer to not realy only on debug symbols
(#53)"), cuyo mensaje es genérico ("fix memory layout for ps2 macros")
y no explica el valor concreto. La intención debe inferirse por
comportamiento, no por documentación.

**HIPÓTESIS, fuertemente soportada por 13.1**: `0x01F00000` reserva
exactamente el **último 1MB** de los 32MB de RAM EE
(`PS2_RAM_SIZE=0x02000000`, confirmado en `ps2_memory.h:26`) — la
región donde, en este ELF concreto (y probablemente en la convención
general del toolchain ee-gcc/SN Systems para juegos con overlays),
viven la ventana de overlays, el heap C pequeño y el stack inicial
(`0x1F00000` → `0x1FFE000` → `0x2000000`). Es una constante genérica de
"no tocar el último 1MB", no un valor calculado para DMC en particular
— el hecho de que encaje case perfectamente con el layout real de este
ELF es evidencia a favor de que el diseño original era razonable, no
una coincidencia rota.

**Quién usa `guestMalloc`, HECHO (18 sitios de llamada, 7 subsistemas
distintos)** — esto NO es exclusivo de MPEG:

| Archivo | Uso |
|---|---|
| `Kernel/Stubs/MPEG.cpp` | datos de callback de stream (la ruta de esta investigación) |
| `Kernel/Syscalls/Thread.cpp:276` | **`target->stack = guestMalloc(target->stackSize, 16u)`** — el STACK de threads guest creados dinámicamente |
| `Kernel/Stubs/GS.cpp` (×3) | buffers de paquetes DMA/GIF (16 bytes álineación, visibles para DMA real) |
| `Kernel/Stubs/Font.cpp` (×4) | paquetes DMA de texto, tablas de glifos/kerning (hasta 0x40 álineación) |
| `Kernel/Stubs/LibC.cpp` (×2) | probablemente `malloc`/`calloc` guest genéricos |
| `Kernel/Stubs/Compatibility.cpp` (×2) | sin investigar en detalle |
| `ps2_iop_host.cpp` | marshalling IOP↔EE |

**Conclusión, HECHO**: el guest heap sintético es infraestructura
transversal, no algo específico de PSS/MPEG. Su colapso actual rompe
(o rompería, si esos caminos llegan a ejercitarse) creación de threads
dinámicos, subida de paquetes GS/fuente, y LibC guest — el fallo en
`StrPcmCallBack` es solo el primero en manifestarse de forma
observable en esta investigación. Las asignaciones son, por diseño,
**direcciones guest reales, visibles para DMA** (alineación de 16/64
bytes, consistente con requisitos de QWORD de PS2) — no pueden
sustituirse por memoria puramente del lado host.

### 13.3 Auditoría de la heurística actual (HECHO)

La política actual (`ps2_runtime.cpp:786-932`) es, en esencia:

```
heapBase = align(max(vaddr+memsz de TODOS los PT_LOAD no-scratchpad) + 0x1000)
heapLimit = min(0x01F00000, PS2_RAM_SIZE)
```

Esto asume implícitamente un **layout de ELF contiguo**: "todo lo que
el juego necesita está en un único bloque bajo, el heap empieza justo
después". Ese supuesto es válido para el segmento 0 (`main`,
`0x100000`-`0x9F8480`) pero **se rompe** en presencia de PT_LOAD
dispersos en direcciones altas no relacionadas con el tamaño real de
datos cargados — exactamente el caso de los 14 marcadores de overlay
en `0x1F00000`. La heurística no distingue "segmento con contenido
real" de "segmento marcador/reserva vacío"; cualquier `PT_LOAD` con
`memsz>0` cuenta igual, sin importar `filesz`.

Con este ELF: `main` termina en `0x9F8480`; los marcadores empiezan en
`0x1F00000`. Hay un hueco de **~24.4 MB** entre ambos
(`0x9F8480`-`0x1F00000`) que la heurística actual ignora por completo
— salta directamente a colocar el heap sintético justo después del
PT_LOAD más alto (los marcadores), que da en el límite duro y colapsa.
**Usar ciegamente el máximo de todos los `PT_LOAD` es conceptualmente
incorrecto cuando el ELF tiene regiones dispersas** — confirmado.

### 13.4 Uso real de memoria de DMC en `0x009F8480`..`0x01F00000` — HALLAZGO IMPORTANTE, cambia la recomendación

**No se asume que el hueco esté libre en runtime solo porque lo está
en el ELF estático** (instrucción explícita del prompt) — y en efecto
**no lo está**, con alta probabilidad, por evidencia real:

DMC tiene su **propio gestor de heap dinámico**, completamente
separado del heap C pequeño de newlib (`end`/`_stack`). Símbolos
reales encontrados: `Mem_alloc` (`0x15ADC0`), `Mem_alloc_R`
(`0x15AEF0`, cuerpo completo desensamblado), `Mem_alloc_R_check`,
`Mem_alloc_R_ptr_set/reset`, `Heap_man_head` (`0x5CDDF0`, cabecera de
una lista enlazada de bloques), `Mem_alloc_addr`/`Mem_alloc_addr_no`
(tabla de tracking). Todos estos símbolos **viven dentro del BSS de
`main`** (`<0x9F8480`) — son las estructuras de contabilidad del
allocator, no el propio heap.

Desensamblado de `Mem_alloc_R` (HECHO, completo):

```c
uint32_t top = *(uint32_t*)0x7411B4;      // 0x15af04 — puntero techo del heap, global BSS
// Heap_man_head = 0x5CDDF0 (constante materializada por lui/addiu)
// recorre la lista de bloques libres colgando de Heap_man_head buscando hueco
...
uint32_t candidateEnd = /* ... */;
uint32_t safeCeiling = top - 0xAC0000;    // 0x15af64-0x15af6c — margen fijo ~10.75 MB
if (safeCeiling < candidateEnd) { /* out-of-memory / func_15D380 */ }
```

**HECHO**: existe un margen de seguridad de `0xAC0000` (~10.75 MB)
codificado en el propio allocator del juego, restado desde `top`
(`*0x7411B4`) antes de aceptar cualquier bloque nuevo.

**HIPÓTESIS, no cerrada estáticamente todavía**: no se ha localizado
el punto exacto donde se escribe el valor inicial de `*0x7411B4`
(`Mem_alloc_R` solo lo LEE). Un escaneo de todo `.text` por
`sw reg,0x1B4(base)` da 7 candidatos, ninguno con el patrón simple
`lui $reg,0x74` inmediatamente antes — el registro base probablemente
se carga más lejos en la misma función o se deriva de otro valor ya
cacheado. **No resuelto con análisis estático simple; requeriría
seguimiento de flujo más profundo o una captura dinámica
(watchpoint en `0x7411B4`)** — se deja como próximo experimento
(13.7/PENDIENTE), no bloquea esta propuesta.

**Por qué esto importa (RIESGO, no solo curiosidad)**: si `top`
(`*0x7411B4`) apunta cerca del final real de RAM (`_stack=0x1FFE000`
o `0x02000000`), el techo efectivo del heap propio del juego sería
`top - 0xAC0000 ≈ 0x1F40000` en el caso más alto — es decir, **DMC
podría estar usando activamente, en tiempo real, gran parte del "hueco
de 24 MB"** (`0x9F8480`-`0x1F00000` y posiblemente algo más allá) como
su propio heap de contenido (texturas, modelos, audio, niveles) vía
`Mem_alloc`/`Mem_alloc_R` — **justo la región que a primera vista
parecía "vacía y seguro" por no tener ningún `PT_LOAD` con contenido
ahí**. Esto confirma exactamente la advertencia del prompt: estar
libre en el ELF NO implica estar libre en runtime.

### 13.5 Opciones de diseño (comparativa, sin implementar)

**A. Ignorar ciertos `PT_LOAD` según criterio demostrable** (p. ej.
`p_filesz==0` y `p_memsz` pequeño, o específicamente: segmentos cuyo
`p_vaddr >= kGuestHeapHardLimit`, que por definición ya caen en la
región reservada y no deberían poder "empujar" el cálculo hacia
arriba).
- Seguridad: alta **respecto al colapso actual** (arregla el bug tal
  cual está reportado); **no** alta respecto a colisión con el heap
  propio de DMC (13.4) — el resultado (`heapBase≈0x9F9480`) cae
  exactamente donde `Mem_alloc_R` probablemente aloja contenido real.
- Generalidad: **alta** — el criterio (`filesz==0`, todos idénticos,
  en la región ya reservada por `hardLimit`) es una propiedad
  estructural del ELF, no una dirección mágica de DMC; cualquier juego
  con el mismo patrón de overlays de ee-gcc se beneficiaría igual.
- Riesgo de solapar heap/stack/buffers del juego: **bajo** para el
  heap C pequeño/stack (siguen protegidos por `hardLimit`, sin tocar)
  — **potencialmente alto** para el heap dinámico propio de DMC
  (`Mem_alloc_R`), sin verificación adicional (13.4).
- Riesgo DMA: ninguno nuevo — la dirección resultante sigue siendo RAM
  guest válida.
- Compatibilidad MPEG: soluciona el bug reportado, **si** no colisiona
  con `Mem_alloc_R` en la práctica.
- Cambios necesarios: uno, localizado, en el bucle de
  `ps2_runtime.cpp:791-907` (criterio de exclusión).
- Invariantes a comprobar: que el criterio de exclusión no excluya
  accidentalmente un `PT_LOAD` con contenido real en otro juego (p.ej.
  BSS grande y legítima con `filesz` pequeño pero `memsz` grande —
  **no** debe bastar "filesz==0"; debe exigirse también similitud con
  el patrón de solapamiento de vaddr con `hardLimit`, no solo
  `filesz==0` en aislado).

**B. Elegir un hueco libre entre rangos `PT_LOAD` ocupados**
- Seguridad: **la más engañosa de las cinco** — "libre en el ELF" fue
  demostrado en 13.4 como **no equivalente** a "libre en runtime" para
  este juego en concreto. Sin verificación dinámica adicional, esta
  opción tiene el mismo riesgo que A pero con menos justificación
  estructural (no hay ninguna garantía de que el hueco identificado
  por PT_LOAD sea real memoria libre).
- Generalidad: media — depende de que otros juegos también dejen
  huecos "aparentes" que en realidad estén poblados por sus propios
  allocators, un patrón común en motores de la era PS2. El mismo
  problema se repetiría en cada juego nuevo.
- Riesgo de colisión: **alto**, exactamente por lo encontrado en 13.4.
- No recomendable sin, como mínimo, verificación dinámica del uso real
  de memoria del juego durante una sesión representativa.

**C. Reservar una región fija conocida del guest address space**
- Seguridad: depende enteramente de qué región se elija y de qué
  garantías tenga esa región de no ser tocada por NINGÚN juego. No
  existe tal región universal en un mapa de RAM de 32MB sin
  información específica por título — el propio EE no reserva nada
  "para host" por diseño (toda la RAM de 32MB es del juego).
- Generalidad: en principio alta si se pudiera encontrar una región
  así, pero en la práctica **no existe una región verdaderamente
  neutral** en un sistema con solo 32MB y sin MMU-enforced boundaries
  entre "guest" y "host" — cualquier dirección guest es, por
  definición, direccionable y potencialmente usable por el juego.
- Sin una fuente de verdad por-título (que contradice el objetivo de
  "no queremos un workaround específico para DMC"), esta opción
  requeriría heurísticas adicionales de todas formas.

**D. Cambiar/subir `kGuestHeapHardLimit`**
- Seguridad: **baja** — 13.1-13.2 demuestran que `0x01F00000` protege
  exactamente el heap C pequeño + stack real del juego
  (`end=0x1F00100`, `_stack=0x1FFE000`). Subirlo reintroduce
  directamente el riesgo que la constante fue diseñada para evitar.
- No recomendable — el límite actual está, hasta donde se ha podido
  verificar, bien elegido; el problema no está ahí.

**E. Otra estrategia respaldada por la arquitectura del runtime —
combinación de A + verificación dinámica/runtime, o reubicación
dinámica con detección de colisión**
- Corregir la heurística de exclusión (como en A, con el criterio
  estructural correcto) **y además** hacer que el runtime **verifique
  en tiempo de ejecución** que el rango elegido para el guest heap no
  colisiona con memoria que el juego está usando activamente — por
  ejemplo, tratando el guest heap como un rango candidato y
  **retrocediendo/reduciéndolo dinámicamente** si `guestMalloc` de una
  región termina devolviendo direcciones que resultan sobrescritas
  (detectable con canarios/asserts de depuración), o exponiendo el
  guest heap como una opción de configuración explícita por título
  (`configureGuestHeap`, que YA EXISTE en la API pública —
  `ps2_runtime.h:367` — pero no se usa actualmente para DMC) en vez de
  depender solo de la heurística automática.
- Seguridad: la más alta de las cinco, a costa de más superficie de
  cambio (heurística + validación) y, opcionalmente, de requerir una
  entrada de configuración por juego cuando la heurística automática
  no pueda garantizar ausencia de colisión.
- Generalidad: alta — el mecanismo de verificación es genérico; la
  configuración explícita por título es un fallback, no la regla.

### 13.6 Propuesta final (diseño, NO código)

**Política general propuesta**: combinar A (corregido) + E.

1. Corregir el cálculo de `maxLoadedRdramEnd`
   (`ps2_runtime.cpp:786-923`) para que un `PT_LOAD` **no pueda
   empujar el techo del heap más allá de `kGuestHeapHardLimit`** — es
   decir, ignorar (a efectos de este cálculo, no de la carga real,
   que debe seguir cargándolos igual) cualquier segmento cuyo
   `p_vaddr >= kGuestHeapHardLimit`. Esto es estructuralmente correcto
   (esos segmentos ya están, por definición, en la región reservada
   que `hardLimit` protege) y generaliza a cualquier juego con el
   mismo patrón de marcadores/overlays en direcciones altas — no es
   una dirección mágica de DMC, es una relación entre dos cantidades
   ya existentes en el runtime.
2. **No** confiar ciegamente en el hueco resultante (13.4 demuestra
   que puede estar ocupado por el heap propio del juego). Añadir, como
   parte del mismo fix, una **verificación mínima post-cálculo**: si
   existe una forma de saber dinámicamente cuánta RAM reclama el
   allocator propio del juego (para DMC: observar `*0x7411B4` una vez
   resuelto el ABIERTO de 13.4), usarla para recortar
   `suggestedHeapBase` por debajo de esa cota real, no solo por debajo
   de `hardLimit`.
3. **Rango que usaría en este ELF, tras el paso 1 únicamente**:
   `[0x9F9480, 0x1F00000)` (~24.3 MB) — **NO se recomienda usar este
   rango sin el paso 2**, por el riesgo de 13.4.
4. **Fallback si no existe hueco suficiente** (o si el paso 2 no puede
   resolverse dinámicamente para un juego dado): usar la API pública
   ya existente `configureGuestHeap(guestBase, guestLimit)`
   (`ps2_runtime.h:367`) para fijar explícitamente, por título, un
   rango verificado manualmente una vez — igual que ya existen otras
   piezas de configuración específicas por juego en este proyecto
   (rutas de CD, ISO, etc.). Esto no es "un workaround para DMC": es
   usar un mecanismo genérico ya presente en la API, con un valor
   verificado para DMC como primer caso de uso real.
5. **Invariantes/asserts que debería añadir el runtime**:
   - `resetGuestHeapLocked` debería hacer un `assert`/log de error
     fuerte (no solo colapsar silenciosamente a `base=0,limit=0`)
     cuando `limit<=base` tras el clamp — el colapso actual es
     **silencioso**, lo que hizo que este bug tardara toda la
     investigación P3.1-P3.4 en aislarse; un log de nivel error en
     ese punto habría acortado el diagnóstico enormemente.
   - `guestMalloc` debería loguear (al menos en builds de diagnóstico)
     cada fallo con el estado del heap — el diagnóstico añadido en
     P3.4 (`heapBase`/`heapEnd`/`heapLimit`) debería promoverse a log
     permanente de bajo volumen (p. ej. solo el primer fallo por
     proceso), no quedar como instrumentación temporal.
   - Considerar un canario simple (patrón conocido escrito al final de
     cada bloque del guest heap, verificado al liberarlo) como red de
     seguridad barata contra colisiones con el allocator propio del
     juego, detectable en builds de depuración.

### 13.7 Test plan para el futuro fix (cuando se implemente)

Exigir, en orden, deteniéndose si aparece una nueva primera
divergencia:

1. `guestHeapBase()`/`guestHeapEnd()`/`guestHeapLimit()` válidos
   (`base>0`, `limit>base`) tras la carga del ELF.
2. `guestMalloc(32,16) != 0` en el primer intento real de
   `dispatchGuestStreamCallback`.
3. `[MPEG:callback-queued]` — `queueInvocation` de `StrPcmCallBack` se
   alcanza (fila D de la sección 12.5, ahora "SÍ").
4. `[PCM:StrPcmCallBack-entry]` — ENTRY real en guest (fila E).
5. `mode=1` escrito (`0x47A0D0`, fila G).
6. `+0x58` avanza `0 → 0x3C00 → ...` (fila H, mismos deltas que
   ORIGINAL en 12.1).
7. `audioDecIsPreset` PATH B da `v0=1` (fila K, `[AUDIO:preset]
   result=1`).
8. `MpegMovieDecode` ENTRY (`Task_execute(3, MpegMovieDecode)` se
   ejecuta).
9. `sceMpegGetPicture` se llama (contador de entrada > 0).
10. Observar si desaparece el stall conocido de `decodedFrames=8`
    (sección 5) — **si el stall reaparece con un mecanismo distinto**,
    es una NUEVA primera divergencia y debe tratarse como tal, no
    como continuación de BLOCKER_004 tal como está documentado aquí.

Detenerse inmediatamente y documentar por separado si cualquiera de
los pasos 1-9 falla de una forma distinta a las ya conocidas.

**Resultado Prompt P3.5 — GUEST_HEAP_LAYOUT_FIX_DESIGN**

## 14. Prompt P3.6 — auditoría de colisión en runtime del guest heap (sin implementar)

Continuación directa de la sección 13 (no la reescribe). Análisis
realizado de forma independiente, sin asumir ninguna conclusión previa
sin volver a demostrarla desde ELF/generated/runtime. **No se ha
implementado ni modificado ningún código de infraestructura.**

### 14.1 Reconstrucción de `Mem_alloc_R` — mecanismo COMPLETO, no solo por nombres

**HECHO (desensamblado directo, no inferido por nombre de símbolo)**:

`Mem_alloc_R` es un allocator de bloques que **crece hacia abajo**
desde un techo (`top`), NO hacia arriba desde una base — corrección
respecto a la lectura provisional de la sección 13.4, que dejaba la
dirección de crecimiento sin determinar. Reconstrucción exacta:

```c
// Globals reales (todas confirmadas por desensamblado, no por nombre):
uint32_t g_top        = *(uint32_t*)0x7411B4;  // techo actual del arena
uint32_t g_topShadow   = *(uint32_t*)0x7411B0;  // mismo valor, escrito junto a g_top
uint32_t Heap_man_head[4];                       // 0x5CDDF0, cabecera de lista de bloques
uint32_t free_mem_ptr_R_head_bak = *(uint32_t*)0x5CDDD0;  // backup para "reset"
uint32_t Mem_alloc_addr_no = *(uint32_t*)0x5CDE08;         // contador de allocations (Mem_alloc, no R)
uint32_t Mem_alloc_addr[0x100] = ...0x5CDE10...;           // tabla de tracking (Mem_alloc, no R)

// Mem_alloc_R_ptr_set(a0):   0x15B0B0-0x15B0DC
void Mem_alloc_R_ptr_set(uint32_t a0) {
    Heap_man_head[3] = 0;                 // 0x5CDDFC = 0 (offset +0xC de la cabecera)
    free_mem_ptr_R_head_bak = g_top;      // guarda el valor VIEJO antes de sobrescribir
    g_top = a0;                           // *** fija el techo al argumento ***
    g_topShadow = a0;                     // (delay slot del jr $ra) mismo valor, variable hermana
}

// Mem_alloc_R_ptr_reset():   0x15B0A0-0x15B0AC
void Mem_alloc_R_ptr_reset() {
    Mem_alloc_R_ptr_set(free_mem_ptr_R_head_bak);   // restaura el checkpoint anterior
}

// Mem_alloc_R(a0=size):      0x15AEF0-0x15AFD8
void* Mem_alloc_R(uint32_t size) {
    uint32_t sizeRounded16 = align16(size);
    uint32_t candidateLow = g_top - 0x10;           // primer candidato: justo bajo el techo
    // recorre Heap_man_head buscando hueco reutilizable [a2..a2+a3) del tamaño pedido
    // (bucle en label_15af28, con soporte de eeCheckpointDue() para cesión cooperativa)
    ...
    uint32_t blockEnd = candidateLow + sizeRounded16;   // extremo alto del bloque candidato
    uint32_t safeCeiling = g_top - 0xAC0000;            // *** ver 14.2 ***
    if (safeCeiling < blockEnd) { /* falla: llama func_15D380 (probable abort/log) */ }
    // si pasa el check: registra el bloque en Heap_man_head, retorna el puntero
}
```

**Quién llama a `Mem_alloc_R_ptr_set`, HECHO parcial**: 6 sitios
(`0x134450`, `0x15A318`, `0x15B0A4`\[interno, desde `ptr_reset`\],
`0x15CF28`, `0x2C2D1C`, `0x4EE5A4`). **El primero, `0x134450`, está
demostrado por desensamblado directo del argumento**:

```
0x134438: lui  $a0, 0x1E0        ; a0 = 0x01E00000 (ningún addiu lo modifica antes del jal)
0x134450: jal  Mem_alloc_R_ptr_set
```

**HECHO: `Mem_alloc_R_ptr_set(0x01E00000)` se invoca en el binario.**
No se ha determinado con certeza en qué fase del arranque ocurre esta
llamada concreta (dirección baja `0x134450`, consistente con código
temprano, pero no confirmado dinámicamente); los otros 5 sitios no se
han desensamblado en esta iteración (**PENDIENTE**) — podrían ser
checkpoints adicionales (guardar/restaurar el techo alrededor de
operaciones puntuales, p. ej. carga de nivel), no necesariamente
cambian la conclusión de que `0x01E00000` es, como mínimo, UN techo
real y demostrado que el juego usa.

**`Mem_alloc` (sin `_R`, `0x15ADC0`) es una función DISTINTA** — un
sistema de tracking/tabla (`Mem_alloc_addr[]`, `Mem_alloc_addr_no`) que
NO reserva memoria por sí mismo en este cuerpo (solo actualiza
contadores/tabla); no se ha determinado su relación exacta con
`Mem_alloc_R` en detalle (**PENDIENTE** — ambos comparten prefijo de
nombre pero pueden ser sistemas paralelos, no el mismo allocator con
dos entradas).

### 14.2 Semántica exacta de `0x00AC0000` — DEMOSTRADA por instrucción, no inferida

**HECHO**: aparece en exactamente un lugar, dentro de `Mem_alloc_R`
(`0x15AF64-0x15AF70`):

```
0x15af64: lui  $v0, 0xFF54          ; v0 = 0xFF540000
0x15af68: addu $v0, $t0, $v0        ; v0 = g_top + 0xFF540000  (mod 2^32) == g_top - 0x00AC0000
0x15af6c: sltu $at, $v0, $v1        ; at = (v0 < blockEnd) ? 1 : 0
0x15af70: bnez $at, ...             ; si (g_top - 0xAC0000) < blockEnd -> camino de error
```

**Es, sin ambigüedad, una resta**: `g_top - 0x00AC0000`, comparada
contra el extremo alto del bloque candidato. **No es** una dirección
absoluta, ni un tamaño de bloque, ni un suelo (`floor`) independiente
— es un **margen relativo al techo actual** (`g_top`), recalculado en
cada llamada a `Mem_alloc_R` (no una constante fijada una sola vez).
Solo lo usa `Mem_alloc_R` (no se ha encontrado en `Mem_alloc`,
`Mem_alloc_R_check`, `_ptr_set` ni `_ptr_reset`).

**Consecuencia demostrada**: con `g_top = 0x01E00000` (14.1), el
margen deja `0x01E00000 - 0x00AC0000 = 0x01340000` como el punto por
debajo del cual **una asignación individual** no puede extender su
extremo alto. **Esto NO es un suelo duro del arena completo** — es un
chequeo **por-asignación** contra el techo vigente en ese momento; no
impide que asignaciones sucesivas seas acumulen bloques cada vez más
abajo, mientras cada bloque individual, medido desde el techo
**vigente en el momento de esa llamada**, no cruce el margen. **No se
ha demostrado ningún límite inferior absoluto para dónde puede terminar
el arena tras muchas asignaciones** — punto crítico para 14.9.

### 14.3 Mapa de EE RDRAM (`0x00000000`-`0x02000000`)

| Rango | Clasificación | Evidencia |
|---|---|---|
| `0x00000000`-`0x00100000` | RESERVADO | Convención estándar PS2 (vectores de excepción/kernel); no reanalizado en detalle esta iteración — **UNKNOWN en el detalle fino**, pero fuera del rango de interés |
| `0x00100000`-`0x009F8480` | **DEMOSTRADO RESIDENTE** | `PT_LOAD` seg0, código+datos+BSS reales, `filesz=0x489900`/`memsz=0x8F8480`, `__bss_end=0x9F8480` |
| `0x009F8480`-`~0x01340000` | **UNKNOWN** | Sin `PT_LOAD` ni símbolo que lo reclame; pero `Mem_alloc_R` no tiene suelo demostrado (14.2) y podría llegar hasta aquí con suficientes asignaciones acumuladas — **no puede clasificarse SAFE** |
| `~0x01340000`-`0x01E00000` | **DEMOSTRADO DINÁMICO** (parcial) | Arena de `Mem_alloc_R`: techo confirmado `0x01E00000` (14.1), margen de asignación individual demostrado hasta `0x01340000` (14.2) — el uso REAL (low/high-water mark durante una partida) no se ha medido dinámicamente (**PENDIENTE**, ver 14.8) |
| `0x01E00000`-`0x01F00000` | **UNKNOWN** | Franja de ~1MB entre el techo de `Mem_alloc_R` y `kGuestHeapHardLimit`; propósito no determinado — podría ser margen adicional intencional del propio juego o simple coincidencia de constantes redondas |
| `0x01F00000`-`0x01F00100` | **OVERLAY** | Ventana de 14 overlays mutuamente excluyentes (sección 13.1, revalidado sin cambios) |
| `0x01F00100`-`~0x01FFE000` | **DEMOSTRADO RESIDENTE (parcial) / RESERVADO** | Heap C de newlib (`end`/`_end`), alcanzado por `sbrk()` (único llamador real: `0x11727C`, no investigado en detalle — **PENDIENTE**); uso real durante el juego no medido |
| `~0x01FFE000`-`0x02000000` | **STACK** | `_stack = 0x01FFE000` (símbolo del linker, SP inicial); el uso real durante ejecución (cuán abajo crece) no medido dinámicamente |
| `0x01F00000`-`0x02000000` (el último 1MB completo) | **RESERVADO + zona de COLISIÓN DEMOSTRADA con RECOMP** | Ver 14.7 — `PS2Runtime::reserveAsyncCallbackStack` también reclama esta MISMA ventana, activamente, para stacks de invocación de callbacks guest |
| `0x70000000`-`0x70004000` (scratchpad) | fuera de alcance | Espacio de direcciones separado del RDRAM principal, no relevante para el guest heap sintético |

**Nota explícita, cumpliendo la instrucción del prompt**: los rangos
UNKNOWN de esta tabla **no se rellenan como libres**. Ninguno de los
dos huecos (`0x9F8480`-`0x1340000` y `0x1E00000`-`0x1F00000`) tiene
evidencia suficiente para clasificarse SAFE.

### 14.4 Ventana de overlay (`0x01F00000`) — revalidada, sin cambios sobre P3.5

Se revalidó explícitamente por instrucción del prompt (no se acepta
la conclusión anterior sin volver a mirarla): los 16 `PT_LOAD`, los
símbolos `_t_XXX_segment_start/end`, y las 14 secciones con nombre
siguen siendo la única lectura consistente con los datos (ver 13.1
para el detalle completo, no repetido aquí). **Confirmación
adicional esta vez**: `p_memsz=0x100` (256 bytes) es el tamaño en el
**ELF base**, no necesariamente el tamaño real que cada módulo de
overlay ocupa cuando se carga desde CD en tiempo de ejecución — el
prompt pide explícitamente no asumir que `p_memsz` es el tamaño en
runtime, y en efecto **no hay ningún dato en este ELF que acote cuánto
puede crecer un overlay cargado** (los archivos de overlay reales
están en la ISO, no en este ELF) — **UNKNOWN, marcado explícitamente,
no se ha inspeccionado la ISO en esta iteración**. La región
`0x1F00000`-`0x1F00100` debe tratarse como el **mínimo** reservado, no
como el máximo.

### 14.5 Consumidores reales de `guestMalloc`/`guestFree` — recapitulado de 13.2 más un hallazgo nuevo

Lista de consumidores ya establecida en 13.2 (MPEG, Thread stacks, GS,
Font, LibC, Compatibility, iop_host) — no repetida aquí. **Hallazgo
nuevo de esta iteración (sección 14.7)**: existe un **segundo
mecanismo, separado de `guestMalloc`**, para reservar memoria guest —
`PS2Runtime::reserveAsyncCallbackStack` — que también entrega
direcciones guest reales y también puede colisionar con memoria del
juego, por una vía completamente distinta. Cualquier estimación de
"cuánta memoria guest necesita el runtime" debe contar ambos
mecanismos, no solo `guestMalloc`.

### 14.6 `SetupHeap`/`EndOfHeap` — contrato auditado, acoplamiento confirmado

**HECHO**: `EndOfHeap` es un syscall PS2 real (0x3E), implementado en
`Syscalls/System.cpp:600-612`; su HLE simplemente devuelve
`runtime->guestHeapLimit()` (o `0x01F00000` si no hay runtime). Existe
en el ELF como función real (`0x202640`, 16 bytes, trampolín de
syscall) y tiene **exactamente un llamador real en todo el
juego**: `sbrk` (`0x202CB8-0x202D64`), la función estándar de newlib
usada por `malloc()`. `sbrk` a su vez tiene **exactamente un llamador**
(`0x11727C`, no identificado en detalle esta iteración — **PENDIENTE**).
Esta cadena (`malloc→sbrk→EndOfHeap`) es el heap C **pequeño** de
newlib, **no** `Mem_alloc_R`.

**`SetupHeap` (syscall 0x3D, `System.cpp:556-598`) — CORRECCIÓN
IMPORTANTE, RETRACTANDO la conclusión anterior de este mismo
párrafo**: la ausencia de símbolo `SetupHeap` en la tabla de símbolos
llevó a una primera conclusión ("DMC probablemente no lo llama") que
**queda RETRACTADA aquí mismo, en la misma sección, tras verificación
directa e independiente del entry point del ELF** (no se aceptó sin
comprobar): el syscall se invoca **inline**, sin pasar por ninguna
función auxiliar con nombre — por eso no deja símbolo. Desensamblado
directo, palabra a palabra, del entry point real (`e_entry=0x100008`):

```
0x100060: lui   $a0, 0x1F0        ; a0 = 0x01F00000
0x100064: lui   $a1, 0x0
0x100068: addiu $a0, $a0, 0x100   ; a0 = 0x01F00100
0x10006c: addiu $a1, $a1, 0x0     ; a1 = 0
0x100070: addiu $v1, $zero, 0x3D  ; número de syscall = 0x3D = SetupHeap
0x100074: syscall
```

**HECHO, demostrado por mí de forma independiente (no por confianza en
ninguna fuente externa): el crt0 de DMC llama `SetupHeap(0x01F00100, 0)`
incondicionalmente, como parte de la secuencia de arranque
(inmediatamente después de `InitMainThread`, syscall `0x3C`, en
`0x100054-0x100058`, con los argumentos de gp/stack/stackSize/entry ya
verificados por separado).**

**Consecuencia demostrada, trazando la HLE real paso a paso**:

```
SetupHeap(heapBaseRaw=0x01F00100, heapSize=0):
    heapBase  = align16(0x01F00100) = 0x01F00100
    heapLimit = kDefaultGuestHeapEnd = 0x01F00000     (heapSize==0)
    configureGuestHeap(0x01F00100, 0x01F00000)
      normalizedBase = clampGuestHeapBase(0x01F00100) = min(0x01F00100, hardLimit=0x01F00000) = 0x01F00000
      resetGuestHeapLocked(0x01F00000, 0x01F00000)
        base=0x01F00000  limit=clampGuestHeapLimit(0x01F00000)=0x01F00000
        limit<=base (0x1F00000<=0x1F00000) -> primera rama de colapso -> sigue limit<=base
        -> COLAPSO FINAL: base=0, limit=0
```

**Esto es una SEGUNDA causa, independiente y suficiente por sí sola,
del mismo colapso `base=0,limit=0` que P3.4 diagnosticó por la vía del
cálculo de `maxLoadedRdramEnd` durante la carga del ELF.** Son dos
mecanismos de código completamente distintos (`ensureGuestHeapInitializedLocked`
durante la carga, vs. `configureGuestHeap` al procesar el syscall del
crt0) que producen el mismo resultado por la misma razón de fondo:
`clampGuestHeapBase` no permite que ninguna base quede por encima de
`kGuestHeapHardLimit`, y `0x01F00100` (el valor real que DMC pasa)
está **256 bytes por encima** de ese límite — suficiente para que el
clamp lo deje exactamente en el borde y colapse.

**Esto invalida, de forma demostrada, la premisa de que arreglar solo
la heurística de `maxLoadedRdramEnd` (Opción A de 13.5/14.10) baste
para desbloquear `guestMalloc`**: aunque esa heurística se corrigiera
por completo, esta llamada de `SetupHeap` desde el crt0 volvería a
colapsar el heap por una ruta de código totalmente distinta,
**incondicionalmente, en cada arranque**, antes de que el juego llegue
siquiera al menú.

**Acoplamiento confirmado, HECHO**: `SetupHeap`'s HLE llama
`runtime->configureGuestHeap(...)`, que internamente ejecuta la
**misma** `resetGuestHeapLocked` que colapsa a `base=0,limit=0` cuando
`limit<=base` (idéntico mecanismo que P3.4 diagnosticó en la carga del
ELF). **Es, en efecto, una vía causal independiente** tal como pedía
el prompt: si algún OTRO juego (o una versión futura de DMC) llamara a
`SetupHeap` con argumentos que produjeran `heapBase>=kDefaultGuestHeapEnd`,
sufriría el MISMO colapso por una ruta de código completamente
distinta a la que causó el bug de P3.4. **Para DMC, con la evidencia
actual, esta vía NO está activa** (no hay llamador demostrado).

**"Heap C/libc del juego" vs "guest heap sintético interno del
runtime" — NO son el mismo concepto**: `SetupHeap`/`EndOfHeap`
gestionan (cuando se usan) el heap que el CÓDIGO DEL JUEGO percibe
como su `malloc()`. El guest heap sintético (`guestMalloc`) es
**exclusivamente interno de RECOMP**, invisible para el juego — el
juego no sabe que existe. Comparten el mismo ALMACENAMIENTO
(`m_guestHeapBase/End/Limit`, protegido por el mismo mutex) pero
representan conceptos distintos que **compiten por la misma región de
direcciones sin coordinación entre sí** — otra fuente de riesgo
general de diseño, no solo un detalle de implementación (ver 14.11).

### 14.7 Stacks del runtime/scheduler — HALLAZGO NUEVO, colisión demostrada con la misma ventana de 1MB

**HECHO, no investigado en P3.4/P3.5**: `PS2Runtime::reserveAsyncCallbackStack`
(`ps2_runtime.cpp:1938-1974`) reserva stacks de 16 KB
(`kInvocationStackSize=0x4000`, fijado en `EeScheduler.cpp:1170`) para
**cada invocación de callback guest** (una por combinación
`(thread_id, profundidad_de_anidamiento)`, cacheadas en
`m_invocationStackTops`). Mecanismo:

```
top  = m_asyncCallbackStackTop     (inicial: PS2_RAM_SIZE = 0x02000000)
floor = m_asyncCallbackStackFloor  (= max(kGuestHeapHardLimit, suggestedHeapBase) = 0x01F00000 en la práctica)
cada reserva: top -= allocSize; falla si top < floor
```

Es decir: **crece hacia abajo desde el tope absoluto de RAM
(`0x02000000`), dentro de la MISMA ventana de 1MB**
(`[0x01F00000, 0x02000000)`) que ya está ocupada por la ventana de
overlay, el heap C de newlib, y el stack nativo de DMC
(`_stack=0x01FFE000`). **Esto es un segundo mecanismo de colisión,
completamente independiente del colapso de `guestMalloc`** — y está
**activamente en uso**: `EeScheduler::invocationStackTop()`
(`EeScheduler.cpp:1154-1178`) lo llama para CUALQUIER invocación de
callback guest asíncrona, incluida la que `dispatchGuestStreamCallback`
encolaría para `StrPcmCallBack` si `guestMalloc` se arreglara.

**Consecuencia importante para cualquier fix futuro (justifica el
paso 3 del test plan del prompt)**: **arreglar solo `guestMalloc` no
basta**. Incluso si `queueInvocation` llega a encolarse, el momento en
que el scheduler ejecute `StrPcmCallBack` necesitará un stack de
`invocationStackTop()`, reservado en una ventana que, con alta
probabilidad (dado que `_stack=0x1FFE000` está a solo 8KB del techo de
esa ventana), **ya colisiona con el stack nativo real de DMC** incluso
antes de considerar el heap C o los overlays. **No se ha medido
dinámicamente si esta colisión ya está ocurriendo silenciosamente en
alguna otra ruta de callbacks que sí se ejecuta hoy** (p. ej.
callbacks GS/vsync) — **PENDIENTE**, riesgo real, no solo teórico.

### 14.8 Uso dinámico real del heap del juego — NO MEDIDO (PENDIENTE explícito)

El prompt autoriza instrumentación temporal de solo lectura/log para
medir `Mem_alloc_R` en vivo (current pointer, low/high watermark,
número de asignaciones) durante boot/título/attract/gameplay. **Esta
medición NO se ha realizado en esta iteración** — requeriría un nuevo
ciclo build-instrumento-ejecuta (o una sesión PCSX2 con watchpoints en
`0x7411B0`/`0x7411B4`/`Heap_man_head`), que no se ejecutó porque el
prompt es explícitamente un análisis de diseño ("no buscamos todavía
hacer funcionar `guestMalloc`"). **Se deja como el experimento
concreto más importante pendiente** (ver 14.12/14.15) — sin él, no
puede convertirse el rango teórico de `Mem_alloc_R`
(`~0x1340000-0x1E00000`) en una prueba de qué parte está REALMENTE
libre durante una partida real de PSS.

### 14.9 Intervalos SAFE / UNSAFE / UNKNOWN

| Intervalo | Clasificación | Justificación |
|---|---|---|
| `[0x00100000, 0x009F8480)` | **UNSAFE** | ELF residente demostrado (14.3) |
| `[0x009F8480, 0x01340000)` | **UNKNOWN** | Sin `PT_LOAD` que lo reclame, pero `Mem_alloc_R` no tiene suelo demostrado (14.2) — no puede excluirse que sus bloques lleguen aquí con uso acumulado suficiente |
| `[0x01340000, 0x01E00000)` | **UNSAFE** | Arena demostrada de `Mem_alloc_R` (techo real `0x1E00000`, margen de asignación individual hasta aquí) |
| `[0x01E00000, 0x01F00000)` | **UNKNOWN** | Sin reclamación demostrada ni descarte; franja de ~1MB sin evidencia en ninguna dirección |
| `[0x01F00000, 0x02000000)` | **UNSAFE** | Overlay + heap C + stack nativo + `reserveAsyncCallbackStack` del propio RECOMP — cuádruple ocupación demostrada/parcialmente demostrada |
| **Cualquier sub-rango candidato a SAFE CANDIDATE** | **NINGUNO DEMOSTRADO** | Ver 14.12 |

No se declara ningún intervalo SAFE CANDIDATE con la evidencia actual
— cumpliendo explícitamente la instrucción de no usar "probablemente
libre" como equivalente a comprobado.

### 14.10 Reevaluación de las estrategias de P3.5 (A-H) con la nueva evidencia

- **A (excluir PT_LOAD marcador del cálculo)**: sigue siendo
  estructuralmente correcto (14.4 no cambia esa conclusión), pero
  **ahora sabemos que el resultado que produciría**
  (`heapBase≈0x9F9480`) cae **directamente dentro del intervalo
  UNKNOWN** de 14.9, no dentro de la arena demostrada de `Mem_alloc_R`
  — el riesgo es **incierto**, no confirmado-malo ni confirmado-bueno.
  Sigue sin ser segura sin medición dinámica (14.8). **Además,
  demostrado en 14.6: A por sí sola es INSUFICIENTE** — el crt0 de DMC
  llama `SetupHeap(0x1F00100, 0)` incondicionalmente en cada arranque,
  lo que vuelve a colapsar el heap por una ruta de código
  completamente distinta (`configureGuestHeap`), sin pasar por
  `maxLoadedRdramEnd` en absoluto. Cualquier fix real necesita, como
  mínimo, corregir TAMBIÉN cómo `SetupHeap`/`configureGuestHeap`
  manejan una base que cae ligeramente por encima de
  `kGuestHeapHardLimit` (p. ej. recortarla al límite en vez de
  colapsar cuando `base>limit` por una diferencia pequeña), no solo la
  heurística de carga del ELF.
- **B (hueco automático entre PT_LOAD)**: sin cambios respecto a
  P3.5 — sigue siendo la opción más engañosa, ahora con evidencia
  todavía más concreta de por qué (el "hueco" incluye tanto la zona
  UNKNOWN como la arena UNSAFE demostrada de `Mem_alloc_R`).
- **C (región fija conocida)**: sin una región verdaderamente
  neutral demostrada (14.9 no encuentra ninguna), sigue sin ser
  viable como estrategia automática general.
- **D (subir `kGuestHeapHardLimit`)**: **reforzado en contra** — 14.7
  demuestra que el propio RECOMP (`reserveAsyncCallbackStack`) YA
  depende de que todo lo `>= kGuestHeapHardLimit` esté reservado para
  otra cosa (los stacks de invocación). Subir el límite empeoraría
  DOS colisiones a la vez, no solo una.
- **E (`configureGuestHeap` por título)**: sigue siendo válida como
  mecanismo, pero **el valor a introducir para DMC no está demostrado
  todavía** (14.8 pendiente) — usarla ahora sería sustituir una
  suposición no verificada por otra.
- **F (detección general + override por título)**: mismo estado que
  E — el "override" necesita el dato de 14.8 primero.
- **G (separar heap declarado del juego vs arena privada de
  PS2Runtime)**: **gana relevancia con la nueva evidencia** — 14.6
  demuestra que ya son conceptos distintos comparten almacenamiento
  sin coordinación; 14.7 demuestra que YA existe una SEGUNDA arena
  interna (`reserveAsyncCallbackStack`) separada de `guestMalloc`,
  pero **ambas compiten por la misma región alta sin ningún
  mecanismo de coordinación entre sí ni con `SetupHeap`**. Formalizar
  esta separación (en vez de tener dos mecanismos ad-hoc que
  casualmente comparten vecindario) parece la dirección de diseño más
  sólida.
- **H**: ver 14.11 — cuestionar si el guest heap sintético debería
  existir como "heap" en absoluto para todos sus consumidores, en vez
  de una única estrategia de reserva.

### 14.11 ¿Es correcto el diseño actual del guest heap sintético?

**No se asume que sí.** Analizando los consumidores reales (14.5,
13.2):

| Objeto | ¿Necesita dirección guest real? | ¿DMA-visible? | ¿Persistente? | ¿Podría usar arena/scratch separado? |
|---|---|---|---|---|
| Datos de callback MPEG (32 B) | Sí (se pasa al guest) | No necesariamente | No — vida de un solo callback | **Sí** — candidato ideal para una arena privada pequeña, de vida corta, gestionada por RECOMP |
| Stacks de thread guest (`Thread.cpp`) | Sí | No directamente | Sí, mientras el thread viva | Podría vivir en una región dedicada separada del heap general |
| Paquetes DMA/GIF (`GS.cpp`, `Font.cpp`) | Sí | **Sí, real** | Corta (un paquete) | Arena de staging dedicada, no heap general de propósito amplio |
| Stacks de invocación (`reserveAsyncCallbackStack`) | Sí | No directamente | Corta/cacheada por profundidad | Ya es una arena separada — pero sin coordinación con las demás |
| LibC/HLE temporal | Depende del caso | Raramente | Variable | Caso por caso |

**Conclusión, análisis, no implementación**: la mayoría de los
consumidores reales de `guestMalloc` son objetos de **vida corta y
propósito específico de RECOMP** (paquetes DMA efímeros, datos de
callback, stacks internos) — no asignaciones de propósito general
como las que haría el propio juego vía `malloc()`. Esto sugiere que
"un único heap sintético compartido, dimensionado por heurística sobre
el layout del ELF" puede ser la abstracción equivocada: **una arena
(o varias arenas por categoría: DMA/staging, stacks, callback-data)
con reglas de tamaño y ciclo de vida propias del runtime, explícitamente
NO acopladas al cálculo de `maxLoadedRdramEnd` ni a `SetupHeap`,
encajaría mejor** con el uso real observado. Ningún objeto de la tabla
anterior necesita compartir espacio con el heap `malloc()` del propio
juego — el acoplamiento actual (mismo storage, mismo colapso) parece
accidental, no una decisión de diseño deliberada (no hay comentario ni
commit que lo justifique, igual que con `kGuestHeapHardLimit`, 13.2).

### 14.12 Recomendación concreta para DMC

**NO SAFE RANGE PROVEN.**

No existe, con la evidencia reunida en esta iteración, ningún
intervalo que pueda calificarse como SAFE CANDIDATE con la
justificación exigida (14.9). La medición que falta, específicamente:

1. Instrumentación de solo lectura (log, sin cambiar comportamiento)
   en `Mem_alloc_R` (`0x15AEF0`) y `Mem_alloc_R_ptr_set/reset`
   (`0x15B0B0`/`0x15B0A0`) registrando, en cada llamada: `g_top` en
   ese momento, dirección del bloque devuelto, tamaño pedido, y el
   contador de bloques activos de `Heap_man_head`.
2. Una corrida real (PCSX2 o RECOMP con FFmpeg activo) cubriendo, como
   mínimo, arranque + título + una PSS completa (dado que este mismo
   blocker es sobre reproducción de PSS) + retorno al título, para
   capturar el low-water-mark real alcanzado por `Mem_alloc_R`.
3. Con ese low-water-mark real, y **solo entonces**, el intervalo
   `[low_water_mark_real, 0x01340000)` (si es no vacío y con margen de
   seguridad adicional, no ajustado al byte) pasaría de UNKNOWN a
   candidato justificable — pero seguiría requiriendo verificar que
   ningún OTRO subsistema (staging de `Mem_alloc` sin `_R`, buffers de
   IOP, etc.) también reclame esa franja.

Hasta que 1-3 se ejecuten, cualquier valor propuesto sería una
suposición, no una decisión respaldada por evidencia — exactamente lo
que este prompt pide evitar.

### 14.13 Recomendación general para PS2Runtime (independiente de DMC)

Separada de 14.12. Para layouts de ELF arbitrarios, con overlays y
allocators propios (el caso general, no solo DMC):

1. **No inferir automáticamente un rango "seguro" del layout estático
   del ELF por defecto.** La heurística actual (`maxLoadedRdramEnd`
   + pad) puede seguir usándose como **mejor esfuerzo cuando no hay
   nada mejor**, pero corregida para no dejar que segmentos marcador
   empujen el cálculo por encima de `kGuestHeapHardLimit` (Opción A de
   13.5/14.10) — esto es una mejora de robustez frente al colapso
   silencioso, no una prueba de ausencia de colisión.
2. **Tratar el guest heap sintético como configuración explícita por
   título por defecto para juegos con allocators propios conocidos**,
   con la heurística automática como *fallback* de último recurso, no
   al revés — esto invierte la prioridad actual, con base en que 14.6
   y 14.7 muestran que el runtime ya tiene MÁS de un mecanismo de
   reserva de memoria guest compitiendo por el mismo espacio sin
   coordinación.
3. **Separar conceptualmente** (Opción G) el heap que el propio juego
   pueda declarar vía `SetupHeap` (si lo usa) de la(s) arena(s)
   internas de RECOMP — con `reserveAsyncCallbackStack` y `guestMalloc`
   coordinados entre sí (un solo gestor de espacio para todo lo interno
   de RECOMP, no dos independientes), para que arreglar uno no
   revele silenciosamente una colisión en el otro (14.7).
4. **Fallar fuerte, nunca degradar en silencio** — ver 14.14.

### 14.14 Invariantes obligatorias para el futuro fix

Como mínimo (ampliando la lista de 13.6 con lo encontrado en esta
iteración):

- `heapBase != 0`, `heapBase < heapLimit`, alineación válida, sin
  overflow/wrap — igual que 13.6.
- Ninguna intersección con `PT_LOAD` residentes demostrados
  (`[0x100000, 0x9F8480)`).
- Ninguna intersección con la ventana de overlay
  (`[0x1F00000, 0x1F00100)` como mínimo demostrado — tratar toda
  `[0x1F00000, 0x2000000)` como reservada hasta que se demuestre lo
  contrario, dado 14.3/14.7).
- **Nueva**: ninguna intersección entre el rango de `guestMalloc` y el
  rango de `reserveAsyncCallbackStack` — hoy son dos mecanismos
  independientes sin ningún chequeo cruzado entre sí.
- **Nueva**: si se introduce cualquier verificación dinámica del heap
  propio del juego (`Mem_alloc_R` u otro), el runtime debería, como
  mínimo en builds de diagnóstico, registrar cuando el guest heap
  sintético y el low-water-mark observado del juego se acercan a
  menos de un margen configurable — detección temprana, no solo
  prevención en el momento de configurar el rango.
- `resetGuestHeapLocked`/`configureGuestHeap` **no deben** degradarse
  en silencio a `base=0,limit=0` — deben producir un error fuerte
  (log de nivel error como mínimo; se recomienda `assert`/excepción en
  builds de diagnóstico) — el colapso silencioso fue la causa de que
  P3.1-P3.4 necesitaran varias iteraciones completas para aislar este
  bug.
- `reserveAsyncCallbackStack` debería, igualmente, loguear fuerte
  (no solo devolver `0` para que el llamador lance una excepción
  genérica de "stack exhausted", que no indica POR QUÉ) cuando
  `top<=allocSize` o cuando la reserva cruza por debajo de
  `kGuestHeapHardLimit`.

### 14.15 Plan mínimo de implementación y validación POSTERIOR (no ejecutado)

El test plan detallado del prompt (pasos 0-13) se adopta tal cual como
la validación incremental obligatoria para cuando se implemente
cualquier fix — no se repite aquí íntegro por brevedad, salvo remarcar
que el **paso 0** ("arena runtime válida al boot, sin overlap
conocido") debe ejecutarse **después** de completar la medición
dinámica de 14.8/14.12, no antes, dado que "sin overlap conocido" no
puede afirmarse hoy. El **paso 3** ("cualquier stack async asociado
cae dentro de región segura") es, a la luz de 14.7, tan importante
como los pasos de `guestMalloc` propiamente dichos — no debe tratarse
como un chequeo secundario.

### 14.16 Nota metodológica — archivo externo no verificado encontrado en el árbol

Durante esta iteración apareció en `analysis/notes/` un archivo no
creado por este análisis
(`BLOCKER_004_P36_HEAP_COLLISION_AUDIT.md`, firmado como sesión
independiente con otro modelo), con afirmaciones de mediciones
dinámicas reales en PCSX2 que no se realizaron en esta sesión. **No se
ha incorporado ningún contenido de ese archivo a esta nota sin
verificación propia** — con una excepción explícita: su afirmación de
que el crt0 llama a `SetupHeap` se **verificó de forma independiente**
por desensamblado directo (14.6) y **se confirmó cierta**; se
documenta aquí como hallazgo propio, con evidencia propia, no por
haber sido leída en ese archivo. El resto de afirmaciones de ese
archivo (valores dinámicos de `Mem_alloc_R`, identidad del linker,
otros llamadores, mapa detallado de stacks de tareas, etc.) **no se ha
verificado en esta iteración y no debe asumirse correcto ni
incorrecto** sin revisión propia adicional.

**Resultado Prompt P3.6 — GUEST_HEAP_RUNTIME_COLLISION_AUDIT**

## 15. Prompt P3.6.1 — reconciliación entre las dos auditorías P3.6 independientes

Continuación directa de la sección 14. Objetivo: resolver la
discrepancia entre esta nota (secciones 12-14, "NO SAFE RANGE PROVEN")
y la auditoría externa no verificada mencionada en 14.16 (que proponía
`proposedHeapBase=0x009FA000, proposedHeapLimit=0x00ABE000`). Todo lo
que sigue es evidencia **verificada de nuevo por mí mismo**, no
aceptada de ninguna fuente externa sin comprobación directa.

### 15.1 Objetivo 1 — resuelto: identidad exacta de `0x00AC0000`

**HECHO, cadena de verificación completa, no solo un fragmento
aislado**: existe una función real, con símbolo propio,
**`Main_malloc`** (`0x15CEE0-0x15CF88`), con **un único llamador**:
`Main_init` (`0x15BA90`), que a su vez tiene **un único llamador**:
**`main`** (`0x15B3A0`, la función `main()` del propio ejecutable).
Cadena de arranque incondicional confirmada: `main → Main_init →
Main_malloc`.

Desensamblado completo y literal de `Main_malloc`:

```c
void Main_malloc(void) {
    uint32_t bssEnd = 0x009F8480;       // constante __bss_end, materializada in-line
    uint32_t base;
    if (bssEnd < 0x00AC0001) {          // sltu + bne, 0x15cee4-0x15cef8
        base = 0x00AC0000;              // rama tomada (bssEnd=0x9F8480 SÍ es menor)
    } else {
        base = bssEnd;                  // rama no tomada en este binario real
    }
    *(uint32_t*)0x7411AC = base;        // "cur" de Mem_alloc
    *(uint32_t*)0x7411A0 = base;        // "base" de Mem_alloc
    Mem_alloc_R_ptr_set(0x01E00000);    // techo de Mem_alloc_R, MISMA función
}
```

**Es, literalmente, `base = max(__bss_end, 0x00AC0000)`** — y como
`__bss_end = 0x9F8480 < 0xAC0000`, el resultado real en este binario es
**`*0x7411AC = *0x7411A0 = 0x00AC0000` exactamente**, fijado de forma
incondicional en el arranque. Esta MISMA función fija también el techo
de `Mem_alloc_R` a `0x01E00000` (coincide con el hallazgo de 14.1,
ahora con la certeza añadida de que es la ruta de arranque canónica, no
un checkpoint aislado).

**Respuesta directa a la pregunta del prompt**: *"¿Puede una arena del
runtime extenderse por encima de `0x00AC0000` sin colisionar con
`Mem_alloc`?"* — **NO, en general**: `Mem_alloc` es un bump allocator
cuyo puntero (`*0x7411AC`) solo puede **aumentar** desde `0xAC0000`
(salvo `Mem_free`, que hace `pop` LIFO hacia checkpoints **siempre
`>=0xAC0000`**, nunca por debajo) — cualquier dirección `>=0xAC0000`
es territorio potencial de `Mem_alloc` sin límite superior estático
demostrado. **Pero, a la inversa**: `Mem_alloc` **nunca puede escribir
por debajo de `0xAC0000`** — es una garantía **estática, incondicional,
verificada por cadena de llamada hasta `main()`**, no una observación
dinámica de una sesión concreta. Esto es una forma de evidencia más
fuerte que "no se vio usado durante 5 minutos de juego".

**Corrección aritmética explícita sobre la premisa del prompt**: la
afirmación *"`0x00ABE000 > 0x00AC0000`... la arena propuesta atraviesa
la frontera en ~`0x1E000` bytes"* es **matemáticamente incorrecta**:

```
0x00AC0000 = 11.272.192
0x00ABE000 = 11.264.000
0x00AC0000 - 0x00ABE000 = 0x2000 (8.192 bytes)
```

**`0x00ABE000` es MENOR que `0x00AC0000` en 8 KB — no mayor.** La
propuesta `[0x009FA000, 0x00ABE000)` **no cruza** la frontera de
`Mem_alloc`; queda enteramente por debajo, con un margen de seguridad
de 8 KB antes de tocarla. **Se corrige explícitamente esta premisa del
prompt** — no se acepta sin comprobar, tal como exige la disciplina de
esta investigación.

### 15.2 Objetivo 2 — medición dinámica de los heaps del juego

**No ejecutada en esta iteración** (instrumentación dinámica nueva,
fuera del alcance de una reconciliación de diseño). Sin embargo, para
`Mem_alloc` específicamente, **15.1 hace innecesaria la medición
dinámica del extremo bajo** (el límite inferior es una garantía
estática incondicional, no algo que deba observarse en runtime para
confiar en él). Sigue haciendo falta medición dinámica para:
- El extremo alto real de `Mem_alloc` durante una partida (cuánto
  llega a crecer `*0x7411AC` hacia `0x1E00000`) — no bloquea la
  conclusión de 15.1, pero sería necesario si se quisiera usar
  cualquier dirección `>0xAC0000`.
- El low-water-mark real de `Mem_alloc_R` — sin cambios respecto a
  14.8, sigue PENDIENTE.

### 15.3 Objetivo 3 — auditoría completa de `reserveAsyncCallbackStack`, colisión CONFIRMADA

Reconstrucción completa (código ya citado en 14.7, revalidado aquí con
un cálculo explícito de la primera reserva real):

```
tamaño por stack:        0x4000 (16 KB), constante fija (EeScheduler.cpp:1170)
dirección de crecimiento: descendente, desde PS2_RAM_SIZE (0x02000000)
floor:                    kGuestHeapHardLimit = 0x01F00000
alineación:               16 bytes
lifetime:                 cacheada por (thread_id, profundidad) en m_invocationStackTops — no se libera explícitamente mientras el thread exista
quién la reutiliza:       ninguna reutilización cross-thread/cross-depth observada; cada combinación nueva consume una franja nueva, monótonamente hacia abajo
máximo simultáneo:        no acotado por código — limitado solo por floor
```

**Cálculo de la PRIMERA reserva real (la que necesitaría
`StrPcmCallBack` si `guestMalloc` empezara a funcionar)**:

```
top inicial = 0x02000000
base = top - 0x4000 = 0x01FFC000
rango reservado: [0x01FFC000, 0x02000000)
```

**Comparación contra el stack real de DMC — HECHO, colisión
demostrada por aritmética directa, no supuesta**:

```
_stack (SP inicial de DMC, símbolo real del linker) = 0x01FFE000
0x01FFC000 <= 0x01FFE000 < 0x02000000  ->  TRUE
```

**`_stack` cae DENTRO del rango que la primera invocación de callback
reservaría.** Esto **confirma de forma independiente y con aritmética
exacta** (no solo cualitativamente, como en 14.7) que **arreglar
`guestMalloc` sin tocar `reserveAsyncCallbackStack` produciría una
colisión inmediata y directa con el puntero de pila real de DMC en la
PRIMERA invocación de `StrPcmCallBack`** — probablemente un crash o
corrupción de pila, no un fallo silencioso como el actual. Esta es,
con diferencia, la pieza más urgente de las dos causas para cualquier
fix futuro: el colapso de `guestMalloc` falla de forma segura (no
corrompe nada); esta colisión de stacks **no lo haría**.

### 15.4 Objetivo 4 — tamaño necesario para una arena interna de RECOMP (`RuntimeGuestArena`)

Estimación conservadora, separando por categoría (sin medición
dinámica exhaustiva — marcado donde aplica):

**A. Asignaciones pequeñas** (vida corta, `guestMalloc` actual):
`MPEG` callback data (32 B, `kMpegCallbackDataSize`), paquetes GS/GIF
(80-128 B + `totalQwc*16`, variable), tablas de fuente (hasta
`0x2010+0xc400≈0xE410`≈58 KB, una vez), LibC/Compatibility (tamaño
variable, no acotado — **UNKNOWN el máximo real**). Estimación
conservadora de pico simultáneo para esta categoría, dado el patrón de
uso observado (paquetes efímeros, liberados rápido):
**del orden de unos pocos cientos de KB, no varios MB** — sin
medición dinámica no puede darse un número exacto (**UNKNOWN el pico
preciso**).

**B. Asignaciones grandes** (stacks): `Thread.cpp:276`
(`target->stackSize`, tamaño pedido por el propio juego vía
`CreateThread` — **no se ha determinado el valor típico usado por
DMC**, **UNKNOWN**) + `reserveAsyncCallbackStack` (16 KB **por cada
combinación (thread, profundidad) de invocación activa** — con
`EeScheduler` sin límite superior estático, el número de combinaciones
simultáneas reales durante PSS+gameplay es **UNKNOWN**, aunque para el
caso concreto de `StrPcmCallBack` sería inicialmente 1 sola franja de
16 KB).

**Conclusión de 15.4**: no se elige un número aquí porque el propio
prompt pide explícitamente no adivinar ("no elegir 700-800 KB si solo
hacen falta 64 KB, ni 64 KB si los stacks requieren más") — la
categoría A cabe holgadamente en el espacio de 15.1
(`[0x9F8480,0xAC0000)`, ~815 KB totales disponibles antes de
`Mem_alloc`), pero la categoría B (stacks) **no debería competir por
el mismo espacio que la categoría A** dado que sus patrones de tamaño
y vida son distintos — ver 15.5.

### 15.5 Objetivo 5 — decisión arquitectónica: separar `GameHeapState` de `RuntimeGuestArena`

**Recomendación firme, reforzada por 15.1-15.3**: SÍ, separar.

- **`GameHeapState`**: lo que `SetupHeap`/`EndOfHeap` gestionan hoy —
  el contrato que el CÓDIGO DEL JUEGO percibe como "mi heap C". Con la
  evidencia de 14.6 (el crt0 SÍ llama `SetupHeap(0x1F00100, 0)`) y la
  de esta sección (DMC en realidad usa `Mem_alloc`/`Mem_alloc_R`, no el
  heap C, para su memoria real), **la forma más limpia de resolver la
  causa 2 de P3.4/P3.6 es que la HLE de `SetupHeap` dimensione un
  `GameHeapState` mínimo y coherente con lo que el juego pide, sin que
  ese cálculo pueda colapsar ni afectar a `guestMalloc`** — separar el
  almacenamiento por completo, no solo la API.
- **`RuntimeGuestArena`**: exclusivamente interna de RECOMP —
  `guestMalloc`/`guestFree` (categoría A de 15.4) **y**
  `reserveAsyncCallbackStack` (categoría B), **coordinados entre sí**
  (hoy son dos miembros de estado independientes sin chequeo cruzado —
  14.14). Ubicación propuesta: dentro de
  `[__bss_end, Mem_alloc_base)` = `[0x9F8480, 0xAC0000)` — demostrado
  libre de `Mem_alloc`/`Mem_alloc_R` por diseño estático (15.1), **no**
  en `[0x1F00000, 0x2000000)` (demostrado colisionando, 15.3).

**¿Elimina esta separación las dos causas observadas?** Sí, de forma
directa: la causa 1 (heurística `maxLoadedRdramEnd`) deja de aplicar
si `RuntimeGuestArena` no depende del layout de `PT_LOAD` en absoluto;
la causa 2 (`SetupHeap` recolapsando) deja de aplicar si
`configureGuestHeap` deja de tocar el mismo almacenamiento que
`guestMalloc` usa. **No rompe el contrato guest**: el juego sigue
recibiendo de `SetupHeap`/`EndOfHeap` lo que espera (una región
`GameHeapState` coherente, aunque el juego no la use realmente); el
guest nunca observa `RuntimeGuestArena` directamente (es puramente
interna de RECOMP, invisible al juego, igual que hoy).

### 15.6 Objetivo 6 — rango SAFE final

**SAFE, demostrado, con la evidencia reunida en 15.1**:

```
[0x009F8480, 0x00AC0000)   (~815 KB)
```

respecto a `Mem_alloc`/`Mem_alloc_R` — límite inferior de `Mem_alloc`
demostrado estáticamente e incondicional (15.1), límite superior de
este candidato estrictamente por debajo de él. Dentro de ese rango, la
propuesta externa `[0x009FA000, 0x00ABE000)` (con ~6 KB de margen
sobre `__bss_end` y 8 KB de margen bajo `0xAC0000`) es **una elección
razonable y ahora justificada** — no se re-deriva un número distinto
aquí porque el de la propuesta externa ya cae dentro del rango
demostrado con márgenes sensatos a ambos lados, y no hay evidencia que
lo contradiga.

**NO SAFE, sigue sin poder usarse para `reserveAsyncCallbackStack`**:
`[0x01F00000, 0x02000000)` — colisión demostrada con aritmética exacta
en 15.3. **Este rango SAFE de `[0x9F8480,0xAC0000)` NO es
automáticamente seguro para stacks de invocación sin verificar
además** que ninguna asignación de la categoría A (callback data)
conviva mal con las de la categoría B (stacks, mucho más grandes,
16 KB cada una) dentro del mismo espacio de 815 KB — recomendación:
particionar el rango en dos sub-arenas (p. ej. callback-data en la
parte baja, stacks de invocación en la parte alta del mismo intervalo)
en vez de un único allocator compartido, dado que sus patrones de
tamaño son muy distintos.

**Aclaración importante**: esta sección **corrige, no reescribe**, la
sección 14 — la conclusión "NO SAFE RANGE PROVEN" de 14.9/14.12 era
correcta con la evidencia disponible en ese momento (no se había
localizado todavía `Main_malloc` ni su límite inferior estático); con
la evidencia nueva de 15.1, el rango `[0x9F8480,0xAC0000)` **pasa de
UNKNOWN a SAFE demostrado** específicamente frente a
`Mem_alloc`/`Mem_alloc_R`. La región `[0x1F00000,0x2000000)` **sigue
UNSAFE**, ahora con evidencia aún más fuerte (15.3).

### 15.7 UNKNOWNs restantes tras la reconciliación

- Extremo alto real de `Mem_alloc` durante una partida (no bloquea
  15.6, ya que el candidato SAFE está por debajo de su base, no
  depende de su techo).
- Low-water-mark real de `Mem_alloc_R` (sigue sin afectar a 15.6).
- Tamaño típico de `target->stackSize` en `CreateThread` de DMC.
- Número máximo simultáneo real de combinaciones
  `(thread, profundidad)` en `reserveAsyncCallbackStack` durante
  PSS+gameplay.
- Si algún otro subsistema (no `Mem_alloc`/`Mem_alloc_R`) usa
  `[0x9F8480, 0xAC0000)` — no se ha vuelto a hacer una búsqueda
  exhaustiva de símbolos/escritores específicamente acotada a este
  rango en esta iteración (sí se hizo una búsqueda de símbolos general
  en P3.5/P3.6 sin resultados, pero no un escaneo dirigido de
  instrucciones `sw`/`lw` con direcciones literales en ese rango
  concreto).
- Contenido/tamaño real de los overlays cargados desde CD (14.4,
  sin cambios).

### 15.8 Propuesta exacta para P3.7 (diseño, no implementación)

1. Separar `GameHeapState` de `RuntimeGuestArena` (15.5) — dos bloques
   de estado independientes en `PS2Runtime`, sin almacenamiento
   compartido.
2. Ubicar `RuntimeGuestArena` en `[0x009FA000, 0x00ABE000)` (o un
   cálculo equivalente con márgenes similares, derivado
   simbólicamente de `__bss_end` y `0x00AC0000` en vez de hardcodeado
   — para mantener generalidad si otro título tiene un `__bss_end`
   distinto, siempre que se re-verifique el equivalente de `Main_malloc`
   para ese título), particionada internamente en sub-región de
   callback-data (categoría A) y sub-región de stacks de invocación
   (categoría B, sustituyendo a `reserveAsyncCallbackStack` en su
   ubicación actual).
3. Hacer que `SetupHeap`/`EndOfHeap` operen exclusivamente sobre
   `GameHeapState`, sin tocar `RuntimeGuestArena` bajo ninguna
   circunstancia.
4. Fallar fuerte (no colapsar en silencio) en ambos bloques de estado
   — invariantes de 14.14, sin cambios.
5. Ejecutar el test plan de 13.7/14.15, añadiendo explícitamente la
   verificación de 15.3 (rango de la primera invocación de
   `StrPcmCallBack` no interseca `_stack` ni ningún stack de tarea) como
   paso obligatorio antes de dar por resuelto el blocker.

**Resultado Prompt P3.6.1 — RECONCILE_RUNTIME_ARENA_AND_SAFE_RANGE**

## 16. Prompt P3.7.1 — RuntimeGuestArena, implementación

Continuación directa de la sección 15. **Esta iteración SÍ implementó
código** (autorizado explícitamente por el prompt) — a diferencia de
P3.4-P3.6.1, que eran solo investigación. Sin commit, sin push, sin
clean build, sin regeneración masiva de `generated/`.

### 16.1 Snapshot previo (HECHO)

Antes de tocar nada: repo exterior con `analysis/notes/BLOCKER_004_pss_video_output.md`
y `upstream.lock.json` modificados (este último, y
`patches/BUILD_ffmpeg_private_link_scope.patch`, **de trabajo previo
no relacionado, no tocados**); `BLOCKER_004_ASTRA_AUDIT.md` y
`BLOCKER_004_P36_HEAP_COLLISION_AUDIT.md` sin trackear (el segundo,
**de otra sesión activa en paralelo** — visto siendo reescrito de
nuevo durante esta iteración; no se tocó). En `vendor/PS2Recomp`:
`Kernel/Stubs/MPEG.cpp` y `Kernel/Stubs/Audio.cpp` con instrumentación
temporal acumulada de P3.4-P3.6 (142 y 17 líneas); `ps2_runtime.cpp`/`.h`,
`EeScheduler.cpp`, `Syscalls/System.cpp` **limpios** (sin modificar) —
exactamente los archivos que esta iteración necesitaba tocar, en
estado conocido. Build activo: `analysis/local/symtabfirst/build`,
`PS2X_ENABLE_FFMPEG=ON` confirmado en `CMakeCache.txt`, ejecutable
previo del 10/09 16:00. Ningún proceso corriendo. No se revirtió nada
de otras sesiones; no se usó `git reset`/`checkout` en ningún momento.

### 16.2 Diseño implementado

**`GuestArenaState`** (struct nuevo, `ps2_runtime.h`): `{blocks, base,
end, limit, suggestedBase, configured}` — el mismo conjunto de campos
que antes vivían sueltos como miembros de `PS2Runtime`, ahora agrupados
para poder instanciarse dos veces de forma completamente independiente:

- **`m_gameHeapState`** (+ `m_gameHeapMutex`) — lo que `SetupHeap`/
  `EndOfHeap` declaran y devuelven al juego. **Nunca** lo toca
  `guestMalloc`/`guestFree`/`reserveAsyncCallbackStack`.
- **`m_runtimeArena`** (+ `m_runtimeArenaMutex`) — exclusivamente
  interno de RECOMP: `guestMalloc`/`guestFree`/`guestCalloc`/
  `guestRealloc`, y ahora también `reserveAsyncCallbackStack`. **Nunca**
  lo toca `configureGuestHeap`/`SetupHeap`.

Los métodos privados que antes operaban directamente sobre los
miembros (`resetGuestHeapLocked`, `ensureGuestHeapInitializedLocked`,
`findGuestHeapBlockIndexLocked`, `allocateGuestBlockLocked`,
`freeGuestBlockLocked`, `coalesceGuestHeapLocked`) se generalizaron a
tomar un `GuestArenaState&` explícito (`resetArenaLocked`,
`ensureGameHeapInitializedLocked`/`ensureRuntimeArenaInitializedLocked`,
`findArenaBlockIndexLocked`, `allocateArenaBlockLocked`,
`freeArenaBlockLocked`, `coalesceArenaLocked`) — **misma lógica de
allocator de lista libre, reutilizada dos veces**, no reescrita, tal
como pedía explícitamente el prompt ("no reescribir innecesariamente
el allocator si basta con desacoplar su estado").

### 16.3 `SetupHeap`/`EndOfHeap` — semántica final

Sin cambios en `Syscalls/System.cpp` (no hizo falta tocarlo): ya
llamaba a `runtime->configureGuestHeap(...)` y `runtime->guestHeapBase/
End/Limit()`, que ahora **automáticamente** quedan ligados solo a
`m_gameHeapState` gracias al cambio en `ps2_runtime.cpp`. Cadena
`malloc/newlib -> sbrk -> EndOfHeap` **no tocada, sigue intacta**
(`EndOfHeap` sigue devolviendo `guestHeapLimit()`, ahora
correctamente scopeado a `GameHeapState`).

### 16.4 `guestMalloc`/`guestFree` — semántica final

Firmas públicas **sin cambios** (`guestMalloc(size, alignment=16u)`,
etc.) — ningún llamador externo (`Compatibility.cpp`, `Font.cpp`,
`GS.cpp`, `LibC.cpp`, `MPEG.cpp`, `Thread.cpp`, `ps2_iop_host.cpp`,
`EeScheduler.cpp`) necesitó cambios. Internamente ahora operan sobre
`m_runtimeArena` exclusivamente.

### 16.5 `reserveAsyncCallbackStack` — nueva ubicación

Ya no cuenta hacia abajo desde `PS2_RAM_SIZE` con un cursor
independiente (`m_asyncCallbackStackFloor/Top`, eliminados). Ahora
pide un bloque de `0x4000` bytes al **mismo allocator de lista libre**
de `m_runtimeArena` que usa `guestMalloc` — coordinación automática por
construcción (un único allocator no puede entregar dos bloques
solapados), sin necesidad de mantener "dos cursores con chequeo
cruzado" como sugería una de las opciones del prompt. Retorno
preservado exactamente (`blockAddr + allocSize - 0x10`, mismo
contrato que antes para los llamadores).

### 16.6 Configuración de `RuntimeGuestArena` para DMC

Mecanismo elegido: **variables de entorno explícitas**
(`PS2X_RUNTIME_ARENA_BASE`, `PS2X_RUNTIME_ARENA_LIMIT`), siguiendo la
misma convención ya usada en este proyecto para configuración por
ejecución/título (`PS2X_CD_IMAGE`). Verificadas ambas, ausentes ->
heurística automática (ahora también endurecida, ver 16.8); una sola
presente -> error fuerte + heurística; ambas presentes pero inválidas
(`base==0`, `base>=limit`, `limit>PS2_RAM_SIZE`, `base` no alineado a
16) -> error fuerte + heurística; ambas válidas -> usadas literalmente,
sin clamping adicional contra `kGuestHeapHardLimit` (el punto de un
override es precisamente poder salir de esa región cuando se ha
verificado que es seguro hacerlo).

**Limitación reconocida explícitamente**: esto NO es todavía una tabla
de configuración por-hash-de-ELF; es un override de proceso. Suficiente
para esta iteración (demuestra y valida la arquitectura), pero una
evolución futura razonable sería resolverlo automáticamente por SHA256
del ELF cargado en vez de depender de variables de entorno puestas por
quien lanza el ejecutable.

Para DMC, usado en la validación: `PS2X_RUNTIME_ARENA_BASE=0x9FA000
PS2X_RUNTIME_ARENA_LIMIT=0xABE000` — confirmado en el log real:
`[RuntimeGuestArena] using explicit override base=0x9fa000 limit=0xabe000`.

### 16.7 Auditoría RPC/TLS (Objetivo 7) — verificada, migración diferida explícitamente

**Verificado independientemente** (no aceptado del prompt sin
comprobar): `ps2xRuntime/src/lib/Kernel/Syscalls/Helpers/State.h:250-266`
define, real y activo:

```cpp
kRpcPacketPoolBase = 0x01F00000  (64 KB, 1024 paquetes de 64 B)
kRpcServerPoolBase = 0x01F10000  (64 KB, servidores de 0x80 B)
kTlsPoolBase       = 0x01F20000  (64 KB, bloques TLS de 0x100 B)
kBootModePoolBase  = 0x01F30000  (4 KB — pool adicional NO mencionado en el prompt)
```

Consumidores reales: `RPC.cpp`, `System.cpp`, `ps2_iop_host.cpp` — el
mecanismo SIF RPC (carga de módulos, pad, memory card) confirmado
activo durante el propio arranque de DMC (trazas `[IOP/RPC
trace:unhandled]` ya vistas en corridas anteriores). `kRpcPacketPoolBase`
coincide exactamente con el inicio de la ventana de overlay de DMC
(`0x1F00000`) — una tercera colisión potencial real.

**Decisión, documentada explícitamente en vez de implementada**: NO
migrados en esta iteración. Razón: no están en la ruta causal directa
hacia `StrPcmCallBack` (a diferencia de `reserveAsyncCallbackStack`,
cuya colisión con `_stack` SÍ estaba demostrada en el camino exacto de
la primera invocación); migrarlos toca 3 archivos adicionales sin
relación con el bug que motiva P3.7.1; y el prompt explícitamente pide
no arreglar nada que no sea consecuencia directa necesaria. **Riesgo
verificado, no resuelto — candidato explícito para una iteración P3.8
dedicada**, no una omisión silenciosa.

### 16.8 Heurística ELF (Objetivo 8) — endurecida, general

`ps2_runtime.cpp`, bucle de carga de `PT_LOAD`: un segmento con
`vaddr >= kGuestHeapHardLimit` ya no puede empujar `maxLoadedRdramEnd`
hacia arriba. Criterio estructural (posición relativa al límite duro
ya existente), no `p_filesz==0` en aislado, y no específico de DMC —
cualquier título con el mismo patrón de marcadores/overlays de
ee-gcc/CodeWarrior en direcciones altas se beneficia igual. Con el
override activo, esta heurística no determina el rango real de DMC en
esta corrida, pero queda corregida como higiene general para otros
títulos que dependan del cálculo automático.

### 16.9 Invariantes y fail-fast (Objetivos 9-10)

- `resetArenaLocked`: si el resultado colapsaría a `base=0,limit=0`,
  **ya no lo hace en silencio** — `std::cerr` con tag
  `[GuestArena:FATAL]`, valores de diagnóstico completos. Confirmado
  en la corrida real (ver 16.10): el colapso de `GameHeapState` por
  `SetupHeap` **sigue ocurriendo** (es el comportamiento real que DMC
  pide, no se oculta) pero ahora es ruidoso y, crucialmente, **no
  afecta a `RuntimeGuestArena`**.
- `computeRuntimeArenaOverrideLocked`: variable a medias o valores
  inválidos -> `[RuntimeGuestArena:FATAL]` explícito antes de caer a
  la heurística.
- `reserveAsyncCallbackStack`: agotamiento del arena ->
  `[RuntimeGuestArena] async callback stack exhausted` con tamaño y
  rango del arena, en vez de un `0` silencioso (el llamador,
  `EeScheduler::invocationStackTop()`, ya lanzaba una excepción en ese
  caso — se preserva, solo se añade contexto).
- Canario/colisión: no se implementó un sistema de canarios explícito
  (el prompt permitía "guard/canary... u otra solución equivalente de
  bajo impacto, no diseñar un memory sanitizer enorme") — la
  protección real conseguida es **estructural**: al unificar
  `guestMalloc` y `reserveAsyncCallbackStack` en el mismo allocator de
  lista libre, dos asignaciones activas **no pueden solaparse por
  construcción** (el allocator nunca entrega un rango ya ocupado). No
  se implementó una verificación adicional de que nada escriba en
  `RuntimeGuestArena` **desde fuera** de su gestor (p. ej. un DMA mal
  configurado) — **UNKNOWN/pendiente**, fuera del alcance mínimo de
  esta iteración.

### 16.10 Build realizado

Estrictamente incremental, árbol activo (`analysis/local/symtabfirst/build`),
**cero archivos de `generated/` recompilados**:

1. `ps2_runtime.vcxproj` normal (`/p:Configuration=Debug /p:Platform=x64`)
   — recompiló `ps2_runtime.cpp`, `MPEG.cpp`, `System.cpp`, `RPC.cpp` y
   el resto de los 44 archivos del runtime (por el cambio en el
   header); **0 advertencias, 0 errores**, ~1m49s.
2. `ps2EntryRunner.vcxproj /t:_BuildLinkAction` — solo relink, **0
   `.cpp` generados recompilados**; 1 advertencia (`LNK4075`,
   preexistente/inofensiva, ya vista en corridas anteriores), 0
   errores, ~2m47s.

### 16.11 Escalera de validación runtime — resultado

ELF real + ISO real + `PS2X_RUNTIME_ARENA_BASE=0x9FA000
PS2X_RUNTIME_ARENA_LIMIT=0xABE000` + FFmpeg ON. Instrumentación
existente de P3.4-P3.6 conservada intacta.

| # | Paso | Resultado |
|---|---|---|
| 0 | RuntimeGuestArena válida tras boot | **PASS** — `[RuntimeGuestArena] using explicit override base=0x9fa000 limit=0xabe000` |
| 1 | Sigue válida tras `SetupHeap` | **PASS** — `SetupHeap(0x1F00100,0)` colapsa `GameHeapState` (`[GuestArena:FATAL] ... requestedBase=0x1f00000 requestedLimit=0x1f00000`, ruidoso, esperado) sin ningún log ni efecto sobre `RuntimeGuestArena` |
| 2 | `guestMalloc(32,16) != 0` para `StrPcmCallBack` | **PASS** — `PCM#1..8` sin ningún `mallocfail`, por primera vez en toda la investigación |
| 3 | `dispatchGuestStreamCallback` alcanza `queueInvocation` | **PASS** (implícito en 4-5) |
| 4 | invocation stack dentro de región segura | **PASS estructural** (mismo allocator que 2; no se verificó la dirección exacta devuelta byte a byte, ver UNKNOWNs) |
| 5 | `StrPcmCallBack` ENTRY | **PASS** — `[PCM:StrPcmCallBack-entry] #1..8` |
| 6 | `audioDecEndPut` | **PASS** — `[PCM:audioDecEndPut-entry]`, `modeAtEntry` progresa 0->1 |
| 7 | `mode=1` | **PASS** — `[PCM:audioDecEndPut] mode(state+0x2C) = 1 @ 0x47A0D0` |
| 8 | `+0x58`: `0->0x3C00->0x5C00->0x6000` | **PASS** — `counter88: 0 -> 15360(0x3C00) -> 23552(0x5C00) -> 24576(0x6000)`, secuencia idéntica a ORIGINAL (sección 12.1) |
| 9 | `audioDecIsPreset` PATH B = 1 | **PASS** — `[AUDIO:preset] ... counter88=24576 counter76=24576 result=1` (primera vez, nunca visto antes en RECOMP) |
| 10 | `Task_execute(3, MpegMovieDecode)` | **PASS** — `[MOVIE:pump-gate] ... readyBit=1`, `[MOVIE:pump-start] Task_execute(3, 0x47A760)` |
| 11 | `MpegMovieDecode` ENTRY | **PASS** — `[MOVIE:decode-task] ENTRY #1` |
| 12 | `sceMpegGetPicture` ENTRY | **PASS** — `[MOVIE:decode-task] CALL sceMpegGetPicture`, `[MPEG:diag] getPicture ENTRY #1 mpeg=0x87d8f8` |
| 13 | `decodedFrames` deja de quedar clavado en 8 | **NO ALCANZADO — nueva primera divergencia, ver 16.12** |

**Pasos 0-12: PASS completo.** Es la primera vez en toda la serie
P3.1-P3.7.1 que la cadena avanza más allá de `audioDecIsPreset`.

### 16.12 Regla de primera nueva divergencia — aplicada, DETENIDO aquí

Tras `[MPEG:diag] getPicture ENTRY #1]`, el proceso **dejó de producir
cualquier línea de log nueva durante 20+ segundos** (memoria estable,
~166 MB, sin crecimiento; sin excepciones, sin `access violation`, sin
salida de error) mientras seguía vivo. Confirmado que no es una
corrida lenta: 20 s sin una sola línea nueva tras un patrón previo de
progreso continuo es un estancamiento real, no una pausa esperada.

**Último nodo correcto (HECHO, log real)**: `sceMpegGetPicture` HLE
ENTRY, `mpeg=0x87d8f8` (mismo handle que el resto de la sesión).

**Primer nodo sin explicar (lectura del código, NO investigado a
fondo — se respeta explícitamente "no arreglar automáticamente")**:
inmediatamente tras el log de ENTRY, la HLE real de `sceMpegGetPicture`
(`MPEG.cpp:2413-2448`) comprueba `playback.decodedFrames.empty()`; si
está vacío y no hay EOF/fallo, entra en una rama de espera asíncrona
real (`runtime->eeScheduler().waitExternal(EeWaitReason::Mpeg, ...)`,
con un lambda de reanudación que reintenta `sceMpegGetPicture` cuando
llega el evento externo). **No se ha determinado** si esta corrida
tomó esa rama (el log correspondiente,
`[MPEG:GetPicture] waiting for frames`, está detrás de un macro
`PS2_IF_AGRESSIVE_LOGS` que puede estar desactivado y no se confirmó
su estado) ni, si la tomó, por qué el evento de reanudación no
llegaría — el log de esta misma corrida sí mostró
`[MPEG:diag] frame decoded, total=4 (feed call #49)` **antes** de
llegar a `MpegMovieDecode`, así que el decoder FFmpeg parece seguir
produciendo fotogramas en algún momento de la ejecución; no se ha
determinado si sigue haciéndolo después de este punto ni si ese
mecanismo de reanudación llega a dispararse.

**No se investigó más profundamente, por instrucción explícita del
prompt.** Candidato natural para una iteración P3.8 dedicada
exclusivamente a `sceMpegGetPicture`/`waitExternal`/el mecanismo de
reanudación — no se tocó `MPEG.cpp` más allá de la instrumentación de
diagnóstico ya presente, no se tocó el scheduler, no se tocó
backpressure ni `kMaxDecodedPicturesAhead`.

### 16.13 Memory safety (Objetivo 15)

Parcial — cubre boot, título y el arranque de la película (hasta el
estancamiento de 16.12); **no alcanzó** attract completo, retorno al
título ni gameplay (la corrida se detuvo en el nuevo estancamiento
antes de llegar ahí). Dentro de la ventana alcanzada: sin señales de
corrupción (sin crashes, sin `access violation`, memoria de proceso
estable). No se verificaron canarios explícitos (no implementados,
16.9) ni se confirmó que `Mem_alloc`/stacks del juego permanecieran
intactos byte a byte — solo ausencia de síntomas observables.
**Validación manual pendiente** (secciones posteriores a este punto,
tal como permite explícitamente el prompt: "si requiere control del
usuario, no bloquear la tarea... dejarlo como VALIDACIÓN MANUAL
PENDIENTE").

### 16.14 UNKNOWNs tras P3.7.1

- Causa exacta del estancamiento de 16.12 (nueva primera divergencia,
  no investigada por instrucción explícita).
- Dirección exacta devuelta por la primera `reserveAsyncCallbackStack`
  real en esta corrida (no se leyó del log byte a byte; solo se
  verificó estructuralmente que no puede solapar con `guestMalloc` por
  compartir allocator).
- Migración de los pools RPC/servidor/TLS (16.7, verificados,
  diferidos).
- Resolución automática por-hash-de-ELF en vez de variables de entorno
  (16.6, limitación reconocida).
- Canario/detección de escritura externa al arena (16.9).
- Validación manual completa de memory safety más allá del punto de
  estancamiento (16.13).

## 17. Corrección de contradicciones obvias en el encabezado

El **Estado** al inicio de este documento (secciones previas a la 12)
seguía describiendo el colapso de `guestMalloc` como un hecho actual
sin resolver. Tras P3.7.1, para DMC con el override activo, **ya no lo
es** — se mantiene toda la narrativa histórica intacta (no se borra
nada) y se añade este puntero: la primera divergencia real, a fecha de
esta sección, es la de 16.12 (estancamiento tras `sceMpegGetPicture`
ENTRY), no ya el colapso de `guestMalloc` de P3.4-P3.6.1, que queda
resuelto (para DMC, vía override explícito; de forma general, parcialmente,
vía la heurística endurecida de 16.8).

**Resultado Prompt P3.7.1 — IMPLEMENT_RUNTIME_GUEST_ARENA_FIX**

## 18. Prompt P3.7.1.2 — HARDEN_RUNTIME_GUEST_ARENA

Continuación directa de la sección 16. Cierra las 4 CONDITIONS (C1, C2,
C3, C6) señaladas por la auditoría independiente P3.7.1.1 (Fable 5).
Iteración deliberadamente pequeña, sin ampliar alcance. Sin commit, sin
push, sin clean build, sin regeneración de `generated/`.

### 18.1 Snapshot previo y revalidación de premisas

Antes de tocar código: repo limpio salvo el diff propio de P3.7.1
(475/137) más el trabajo de otras sesiones ya documentado en 16.1, sin
cambios concurrentes nuevos en los archivos a tocar. **Las 4 premisas
de Fable se revalidaron contra el código real (no aceptadas de
antemano)** y las 4 coincidieron exactamente:

- C1: sin override, el fallback caía en `[~0x9F9480, 0x1F00000)` —
  confirmado trazando `ensureRuntimeArenaInitializedLocked` con el
  código de P3.7.1.
- C2: env inválida → log `[FATAL]` seguido de uso real del fallback
  automático — confirmado leyendo `computeRuntimeArenaOverrideLocked`/
  `ensureRuntimeArenaInitializedLocked` de P3.7.1.
- C3: `SetupHeap(0x1F00100,0)` → `GameHeapState` colapsaba a
  `base=0,limit=0` — confirmado retrazando `configureGuestHeap`→
  `resetArenaLocked` con los valores reales.
- C6: sin log de dirección exacta en `reserveAsyncCallbackStack` —
  confirmado, no existía.

Ninguna premisa resultó falsa; no hubo que detener ninguna corrección
por discrepancia.

### 18.2 C1 — fallback automático eliminado

**Causa confirmada**: `ensureRuntimeArenaInitializedLocked` (P3.7.1)
caía, sin override, a `resetArenaLocked` con `suggestedBase` derivado
de la heurística ELF — el mismo hueco `[~0x9F9480,0x1F00000)` que
P3.6/P3.6.1 demostraron indistinguible del territorio real de
`Mem_alloc`.

**Cambio implementado**: eliminado por completo el bloque en
`loadElf()` que alimentaba `m_runtimeArena.suggestedBase` (ya no se
calcula ni se usa). `ensureRuntimeArenaInitializedLocked` ahora tiene
exactamente dos desenlaces: override válido, o **`RuntimeGuestArena`
permanentemente `unavailable`** (nuevo flag en `GuestArenaState`) —
nunca un tercer camino heurístico silencioso.

**Comportamiento sin override (verificado, caso 11.G)**:
```
[RuntimeGuestArena:FATAL] no PS2X_RUNTIME_ARENA_BASE/PS2X_RUNTIME_ARENA_LIMIT
override was provided. Automatic ELF-derived placement is not considered
safe ... RuntimeGuestArena is PERMANENTLY UNAVAILABLE for this process.
```
seguido de fallos reales y ruidosos en el primer `guestMalloc`/
`reserveAsyncCallbackStack` reales del arranque (ver 18.6).

### 18.3 C2 — fail-fast real

**Estado confirmado antes del fix**: env inválida → FATAL log →
heurística automática usada de todas formas (no era fail-fast).

**Cambio implementado**: `computeRuntimeArenaOverrideLocked` ahora
devuelve un `enum class RuntimeArenaOverrideResult {Absent, Invalid,
Valid}` en vez de `bool`, distinguiendo "sin configurar" de
"configurado mal". `ensureRuntimeArenaInitializedLocked` trata
`Invalid` **igual que `Absent`** en cuanto al resultado práctico
(arena permanentemente `unavailable`), pero con un mensaje distinto
que identifica que hubo un intento explícito fallido. Política elegida:
**B** (marcar inválida permanentemente + fallo fuerte en cada uso
posterior), no `abort()` — coherente con que el resto del runtime ya
maneja fallos de `guestMalloc` devolviendo `0`, no terminando el
proceso.

**Casos probados (ver 18.8, todos PASS)**: solo BASE, solo LIMIT,
malformado (`banana`), `base>=limit`, `limit>PS2_RAM_SIZE` — los 5
producen el mismo desenlace: log `[FATAL]` específico del motivo +
arena permanentemente inválida + fallo ruidoso en cada
`guestMalloc`/`reserveAsyncCallbackStack` posterior, **nunca** un
fallback silencioso.

### 18.4 C3 — semántica correcta de `SetupHeap`/`EndOfHeap` para `size=0`

**Semántica PS2 revalidada** (Objetivo 4.1 del prompt): se determinó,
por el propio uso real de DMC (`SetupHeap(0x01F00100, 0)` desde crt0,
sección 17) y por ser la lectura más directa del argumento, que
`size=0` representa la **interpretación A**: un heap C legítimo con
**capacidad cero** exactamente en `base` (`heapStart=heapEnd=base`) —
no un error, no "usa algún límite por defecto no relacionado". La
interpretación de `size=0xFFFFFFFF` ("resto de la RAM", convención ya
documentada para Silent Hill) **no se tocó**, sigue exactamente igual.

**Cambios implementados**:
1. `Syscalls/System.cpp::SetupHeap`: rama nueva explícita para
   `heapSize==0` → `heapLimit=heapBase` (antes: `heapLimit =
   kDefaultGuestHeapEnd`, una región no relacionada). El salvavidas
   `if (heapLimit<=heapBase) heapLimit=kDefaultGuestHeapEnd` ahora
   respeta un flag `explicitEmptyHeap` para no pisar esta declaración
   legítima.
2. `ps2_runtime.cpp::resetArenaLocked`: nuevo parámetro `bool
   allowEmpty`. Con `allowEmpty=true` (usado solo por `GameHeapState`),
   `base==limit` es válido — se registra con un log informativo
   (`[GameHeapState] ... valid declaration, not an error.`, **sin**
   la palabra FATAL) y NO colapsa a `0,0`. Con `allowEmpty=false`
   (`RuntimeGuestArena`, sin cambios de comportamiento aquí), `base==limit`
   sigue siendo un colapso `[GuestArena:FATAL]` real — la separación
   semántica que pedía explícitamente la sección 4.3 del prompt.
3. `ps2_runtime.cpp::configureGuestHeap`: `hardLimit` cambiado de
   `kGuestHeapHardLimit` a `PS2_RAM_SIZE` — el límite duro existe para
   proteger una `RuntimeGuestArena` **automática y no verificada** de
   invadir la zona alta; nunca tuvo sentido aplicarlo a lo que el
   propio juego declara sobre su propio heap (que además ya no
   respalda ninguna asignación real desde P3.7.1). Este era el motivo
   exacto por el que `0x1F00100` se recortaba a `0x1F00000` y acababa
   colisionando consigo mismo.

**Resultado verificado en corrida real (DMC, override activo)**:
```
[GameHeapState] title declared a zero-capacity heap at 0x1f00100 -- valid declaration, not an error.
```
Sin `[GuestArena:FATAL]` para esta declaración. `GameHeapState`
final: `base=end=limit=0x01F00100` (verificado por el propio mensaje
más la traza simbólica hecha antes de tocar código). `EndOfHeap`
devuelve `guestHeapLimit()` = `0x01F00100` — exactamente el valor
pedido por el prompt, no `0`, no `0xABE000`, no `0x1F00000`.

### 18.5 Cadena `malloc/newlib -> sbrk -> EndOfHeap` — verificada intacta

No se tocó `sbrk` ni ninguna otra parte de esa cadena. `EndOfHeap`
sigue leyendo exclusivamente `m_gameHeapState` (sin cambios de esta
sección); con capacidad declarada cero, cualquier intento real de
`sbrk`/`malloc` de newlib fallaría **correctamente** (no hay a dónde
crecer) sin poder desbordarse jamás hacia `RuntimeGuestArena`,
`Mem_alloc` ni ninguna otra región — ambos arenas permanecen
completamente desacoplados en almacenamiento, exactamente como exige
la sección 5 del prompt.

### 18.6 C6 — observabilidad exacta de async callback stacks

**Cambios implementados**:
1. `ps2_runtime.cpp::reserveAsyncCallbackStack`: chequeos de invariantes
   explícitos antes de devolver nada (`blockBase>=arenaBase`,
   `blockEnd<=arenaLimit`, `blockEnd>blockBase` sin overflow,
   `allocSize>=0x10`) — si fallan, libera el bloque y devuelve `0` con
   un log `[FATAL]` en vez de un puntero potencialmente inseguro.
2. `Kernel/EeScheduler.cpp::invocationStackTop()`: una línea de log
   **solo en el cache-miss** (primera vez que una combinación
   `(threadId, depth)` necesita stack — las reutilizaciones no generan
   ruido, gratis por la propia estructura de caché existente), con
   `thread`, `depth`, `block=[base,end)`, `sp`, `size`.

**Evidencia real, corrida de regresión (18.8)** — funciona exactamente
como se pedía, sin solapes, contiguo byte a byte dentro del arena:
```
[RuntimeGuestArena:async-stack] thread=1 depth=0 block=[0x9fa000,0x9fe000) sp=0x9fdff0 size=0x4000
[RuntimeGuestArena:async-stack] thread=2 depth=0 block=[0x9fe600,0xa02600) sp=0xa025f0 size=0x4000
[RuntimeGuestArena:async-stack] thread=5 depth=0 block=[0xa02800,0xa06800) sp=0xa067f0 size=0x4000
... (thread=5, depth 1-15, cada bloque exactamente contiguo con el anterior) ...
[RuntimeGuestArena:async-stack] thread=-1 depth=0 block=[0xa42800,0xa46800) sp=0xa467f0 size=0x4000
```
**Observación no buscada, registrada honestamente**: el último bloque
(`thread=-1`) aparece justo después de `getPicture ENTRY #1` — un
contexto de invocación distinto (probablemente el callback de
reanudación de `waitExternal` mencionado en 16.12). Esto es
consistente con la hipótesis ya documentada del nuevo estancamiento,
pero **no se investigó más** — respeta la instrucción explícita de no
tocar `waitVSync`/`presentation`/el scheduler en esta iteración.

**Primera invocación real de `StrPcmCallBack`** (thread=1, depth=0):
`block=[0x9fa000,0x9fe000)`, `sp=0x9fdff0` — **dentro** de
`[0x9FA000,0xABE000)` (`0x9fa000>=0x9fa000` ✓, `0x9fe000<=0xabe000` ✓),
sin solape con ningún otro bloque (el siguiente, `thread=2`, empieza
en `0x9fe600`, después del final de este).

### 18.7 Build realizado

Estrictamente incremental, mismo árbol activo, **0 archivos
`generated/` recompilados**:
1. `ps2_runtime.vcxproj` normal — recompiló los 44 archivos del
   runtime (cambio de firma en el header); **0 advertencias, 0
   errores**, ~1m46s.
2. `ps2EntryRunner.vcxproj /t:_BuildLinkAction` — solo relink, **0
   `.cpp` generados recompilados**, 0 errores, warning `LNK4075`
   preexistente inofensivo.

### 18.8 Tests de configuración 11.A-11.G

Ejecutados como 7 procesos reales independientes (no simulados), cada
uno con ELF/ISO reales:

| Test | Config | Resultado |
|---|---|---|
| 11.A | `BASE=0x9FA000 LIMIT=0xABE000` | **PASS** — arena válida, usada correctamente toda la corrida |
| 11.B | solo `BASE` | **PASS** — fail-fast real, sin fallback |
| 11.C | solo `LIMIT` | **PASS** — fail-fast real, sin fallback |
| 11.D | `BASE=banana` | **PASS** — parseo falla, fail-fast |
| 11.E | `BASE>=LIMIT` (invertidos) | **PASS** — fail-fast |
| 11.F | `LIMIT=0x3000000` (`>PS2_RAM_SIZE`) | **PASS** — fail-fast |
| 11.G | sin override | **PASS** — `RuntimeGuestArena` permanentemente `unavailable`, **sin** arena gigante automática |

**7/7 PASS.** En B-F-G, cada corrida además demuestra que un
`guestMalloc`/`reserveAsyncCallbackStack` real posterior (durante el
arranque normal del juego) falla ruidosamente, no en silencio.

### 18.9 Escalera de regresión 0-14

Misma configuración que P3.7.1 (`PS2X_RUNTIME_ARENA_BASE=0x9FA000
PS2X_RUNTIME_ARENA_LIMIT=0xABE000`, ELF/ISO reales, FFmpeg ON).

| # | Paso | Resultado |
|---|---|---|
| 0 | `RuntimeGuestArena` válida | **PASS** |
| 1 | `SetupHeap` no la modifica | **PASS** — `GameHeapState` colapsa/declara vacío sin tocar `RuntimeGuestArena` |
| 2 | `GameHeapState` semánticamente correcto | **PASS** — `base=end=limit=0x1F00100`, sin FATAL (18.4) |
| 3 | `EndOfHeap` correcto | **PASS** (por construcción, mismo mecanismo verificado en 18.4) |
| 4 | `guestMalloc(32,16)!=0` | **PASS** |
| 5 | `queueInvocation(StrPcmCallBack)` | **PASS** |
| 6 | async stack: dirección exacta, dentro del arena | **PASS** — `block=[0x9fa000,0x9fe000)` (18.6) |
| 7 | `StrPcmCallBack` ENTRY | **PASS** |
| 8 | `audioDecEndPut` | **PASS** |
| 9 | `mode=1` | **PASS** |
| 10 | `+0x58`: `0->0x3C00->0x5C00->0x6000` | **PASS** — `counter88: 0->15360->23552->24576` |
| 11 | `audioDecIsPreset` PATH B=1 | **PASS** |
| 12 | `Task_execute(MpegMovieDecode)` | **PASS** |
| 13 | `MpegMovieDecode` ENTRY | **PASS** |
| 14 | `sceMpegGetPicture` ENTRY | **PASS** |

**15/15 PASS.** Idéntico alcance que P3.7.1.

### 18.10 Nuevo stall — regresión confirmada, NO investigado

Tras `getPicture ENTRY #1`, el mismo estancamiento de 16.12 se
reprodujo exactamente (347 líneas estables durante 15+ s, proceso
vivo, sin excepciones). **Respuesta a la pregunta del prompt: SÍ, se
sigue alcanzando el mismo punto — PASS de regresión.** No se investigó
más allá (no se tocó `MPEG.cpp` más allá de lo ya existente, no se
tocó el scheduler, no se tocó `waitVSync`/`presentationTickForFrame`/
backpressure/`kMaxDecodedPicturesAhead`).

### 18.11 Memory safety observada

Cubre boot, título, arranque de PSS, hasta el estancamiento conocido
(igual alcance que P3.7.1, no más). Sin `RuntimeGuestArena:FATAL` con
override válido (caso A), sin fallos de `guestMalloc` en ese caso, sin
agotamiento del async-stack (16 profundidades usadas de forma
contigua sin errores), sin solapes de allocator observables en el log,
dirección de stack verificada dentro del arena (18.6), sin
crashes/excepciones nuevas. Gameplay más allá de este punto: **sigue
como VALIDACIÓN MANUAL PENDIENTE** (sin cambios respecto a 16.13).

### 18.12 RPC/TLS/BootMode — confirmado DEFERRED, sin tocar

No se modificó `Helpers/State.h`, `RPC.cpp`, `System.cpp` (syscalls
RPC) ni `ps2_iop_host.cpp` en ningún punto de esta iteración — los 4
pools (`kRpcPacketPoolBase`, `kRpcServerPoolBase`, `kTlsPoolBase`,
`kBootModePoolBase`) siguen exactamente donde estaban (13.7 de la
sección 16). Confirmado explícitamente: **siguen DEFERRED**, tema
independiente para una iteración P3.8 dedicada.

### 18.13 UNKNOWNs tras P3.7.1.2

- Causa del estancamiento tras `sceMpegGetPicture` ENTRY (16.12,
  reconfirmado en 18.10, sigue sin investigar por instrucción
  explícita) — el nuevo dato del stack `thread=-1` (18.6) es una pista,
  no una explicación.
- Migración de RPC/TLS/BootMode (18.12, diferida).
- Resolución automática por-hash-de-ELF en vez de variables de entorno
  (sin cambios respecto a 16.6/16.14).
- Validación manual de memory safety más allá del estancamiento
  conocido (attract completo, retorno al título, gameplay).
- No se implementó ninguna infraestructura general de detección de
  escritura externa al arena (canario grande) — explícitamente fuera
  de alcance de esta iteración, igual que de P3.7.1.

**Resultado Prompt P3.7.1.2 — HARDEN_RUNTIME_GUEST_ARENA**

## 19. Prompt P3.8 — GetPicture presentation/wait, primera divergencia real

Continuación directa de la sección 18. **Investigación pura — sin fix
semántico, sin commit, sin push, sin clean build.** BLOCKER_004 sigue
**OPEN**.

### 19.1 Snapshot y runs realizados

Repo limpio salvo el diff acumulado de P3.7.1-P3.7.1.2 (ya documentado)
más lo añadido aquí. Un único run RECOMP real, clasificado
explícitamente per la regla de "boot barrier" del prompt:

| Run | ORIGINAL/RECOMP | BOOT_ONLY/PSS_REACHED | Alcanzó GetPicture |
|---|---|---|---|
| `p38_run1.log` | RECOMP | **PSS_REACHED** | SÍ — `getPicture ENTRY #1` en línea 550 |

RECOMP no requiere navegación manual de menús (arranca y llega a PSS
automáticamente, confirmado en todas las corridas P3.4-P3.8). **Ningún
run ORIGINAL/PCSX2 se ejecutó en esta iteración** — requiere
intervención manual del usuario (Memory Card → idioma → X → CAPCOM),
ver 19.9.

### 19.2 Congelado de P3.7 — reconfirmado, no reinvestigado

En el mismo log: `[RuntimeGuestArena] using explicit override
base=0x9fa000 limit=0xabe000`, cero líneas `FATAL`, `mode(state+0x2C)
= 1`, `[AUDIO:preset] ... result=1`, `Task_execute(3, 0x47A760)`,
`getPicture ENTRY #1` — toda la cadena P3.4-P3.7.1.2 intacta. **No se
reinvestigó** `guestMalloc`/`SetupHeap`/PATH B/`mode=1`/`state+0x58`/
`bit4000`/`state+0x70`, tal como exige la sección 2 del prompt.

### 19.3 Mapa de control real de `sceMpegGetPicture` (HECHO, leído del código actual, no de resúmenes)

```
sceMpegGetPicture(mpegAddr, imageAddr):
  LOCK
  A. decodedFrames.empty() && !eofSeen && !streamEnded && !decoderFailed?
       SÍ -> H. waitExternal(EeWaitReason::Mpeg, kMpegPictureWaitType, mpegAddr, resumeLambda)
             -- [[noreturn]]: lanza EeDispatcherTransfer, NO continúa in situ
  B. decodedFrames NOT empty?
       SÍ -> calcula presentationTargetQ32 (primer frame: == currentTick, HECHO por lectura de presentationTickForFrame)
             C. corrección si currentTick muy por delante
             currentTick < target? -> G. waitVSync(...) -- [[noreturn]], igual mecanismo
             D. si no, presentación inmediata -> pop frame, haveFrame=true
       NO -> E. usa width/height/frameCount previos, sigue sin frame
  UNLOCK
  escribe width/height/frameCount y campos internos del struct guest
  J. si haveFrame: copia frame al buffer guest; si no y frameCount==0: frame en blanco
  K. setReturnS32(ctx,0) -- SOLO alcanzable si NINGÚN wait fue tomado
```

**Corrección importante de mi propia lectura inicial**: `waitExternal`/
`waitVSync` están declaradas `[[noreturn]]` y usan `blockCurrent()` →
`throw EeDispatcherTransfer{}` — **suspenden de verdad** (no "caen a
través" del resto del código). Mi primera lectura superficial fue
incorrecta y quedó corregida antes de instrumentar nada, no después.

### 19.4 Estado exacto en el primer ENTRY real (HECHO, dinámico, no aceptado de ningún reporte previo)

```
[GP:A] #1 entry mpeg=0x87d8f8 decodedFrames=0 streamEnded=0 decoderFailed=0
        cdStreamEofSeen=0 sawInput=0 picturesServed=0
[GP:H] #1 waitExternal(kMpegPictureWaitType, mpeg=0x87d8f8) -- SUSPENDING, no frame available
```

**`decodedFrames` estaba vacío en el momento real del primer
`sceMpegGetPicture`** — la rama tomada es **A/H (`waitExternal`)**, no
la rama B/`waitVSync` que P3.7.1.1 (Fable) había propuesto como
hipótesis principal. Esto **no confirma automáticamente** la cadena
`presentationTickForFrame -> waitVSync` de la hipótesis previa — la
rechaza como explicación de ESTA suspensión concreta (ver 19.7).

### 19.5 ¿`GetPicture` retorna o suspende? (binario, demostrado)

**B — SUSPENDE**, confirmado dinámicamente. `waitExternal` registra
`EeWaitReason::Mpeg`, `type=kMpegPictureWaitType`,
`token=mpegAddr=0x87d8f8`, con una lambda de reanudación que
re-invoca `sceMpegGetPicture` desde el principio. Punto exacto:
`MPEG.cpp`, dentro de `sceMpegGetPicture`, rama `decodedFrames.empty()`.
Owner: el thread guest que ejecuta `MpegMovieDecode` (no
identificado por número exacto en esta iteración — **UNKNOWN el id**,
no crítico para la conclusión).

### 19.6 Callsite guest — revalidado desde ELF/generated, no desde Ghidra

**HECHO, verificado independientemente, byte a byte**:

```
0x47A8E4: JAL 0x109FF0        (sceMpegGetPicture, símbolo real del ELF, size=0x48)
0x47A8E8: (delay slot)
0x47A8EC: BGEZ $v0, +0x14     -> salta a 0x47A900 si v0>=0
```

Instrumentado en `MpegMovieDecode_0x47a760.cpp` (`analysis/local/symtabfirst/generated/`,
el árbol que compila el build activo): log `[GP:CONT]` justo en
`label_47a8ec`. **Resultado: `[GP:CONT]` NUNCA aparece en el log** —
consistente con 19.5 (si el HLE suspende via excepción, el guest
nunca llega a esa dirección en esta invocación) y con que la
reanudación tampoco llegó a ocurrir (19.7).

### 19.7 ¿Vuelve a llamarse/reanudarse? — primera divergencia real localizada

**HECHO, dinámico, hasta 1365 líneas de log, proceso vivo todo el
tiempo**:

- `[GP:I]` (confirma que `resumeCompletion` fue invocado): **0
  apariciones**.
- `getPicture ENTRY #2`: **0 apariciones**.
- `[MOVIE:gate18]`/`[AUDIO:preset]`/`[CDMODULE/MOVIE:chunk]`
  (instrumentación preexistente de `Movie_ctrl`, sin capar, P3.4-P3.7):
  **0 apariciones después de la línea 552** (el `[GP:H]` de la
  suspensión).
- `[GP:VSync]`: **1016 apariciones**, tick avanzando de forma
  continua y rápida (de ~5 a >15000 en la ventana observada).

**Esto refuta la lectura de P3.7.1.2 de "proceso vivo, CPU activa,
sin progreso" como un cuelgue genérico**: el proceso NO está
congelado — el bucle de eventos del scheduler (`processEvent`,
`VBlankStart`, `completeVSync`) sigue funcionando con total
normalidad miles de veces. **Lo que se detiene por completo es
`Movie_ctrl`** — el driver por-tick que alimenta el demux
(`sceMpegDemuxPssRing`) y que es, según el propio código (sección
12.5/16 de este documento), el único llamador conocido de
`completeExternalWait(kMpegPictureWaitType, ...)` capaz de despertar
esta espera concreta.

### 19.8 Tabla causal ORIGINAL vs RECOMP

| Nodo | ORIGINAL | RECOMP | Igual/Difiere |
|---|---|---|---|
| GetPicture ENTRY | **PENDIENTE** (requiere PCSX2, 19.9) | HECHO — `mpeg=0x87d8f8`, `decodedFrames=0` | — |
| frame available? | PENDIENTE | HECHO — NO (vacío) | — |
| wait required? | PENDIENTE | HECHO — SÍ | — |
| wait registered? | PENDIENTE (no aplica igual: ORIGINAL usa código de librería real, no esta HLE) | HECHO — `waitExternal`/`EeDispatcherTransfer` | — |
| VSync advances? | PENDIENTE | HECHO — SÍ, 1016 ticks observados | — |
| wake event generated? (`completeExternalWait` llamado) | PENDIENTE | **HECHO — NO, nunca observado** | — |
| wake event delivered? | PENDIENTE (n/a si no se genera) | n/a (no se generó) | — |
| continuation queued? | PENDIENTE | HECHO — NO (`resumeCompletion` nunca se movió a ready) | — |
| continuation executed? | PENDIENTE | HECHO — NO (`[GP:I]` nunca aparece) | — |
| GetPicture re-entry/return? | PENDIENTE | HECHO — NO (`getPicture ENTRY #2` nunca aparece) | — |
| guest continuation after jal? | PENDIENTE | HECHO — NO (`[GP:CONT]` nunca aparece) | — |
| **`Movie_ctrl` sigue corriendo por-tick?** | PENDIENTE | **HECHO — NO, 0 apariciones tras la suspensión** | — |

No se rellena ninguna celda ORIGINAL con inferencia — quedan
`PENDIENTE` explícitamente hasta la sesión PCSX2 de 19.9.

### 19.9 ORIGINAL — sesión PCSX2 pendiente (no ejecutada, plan preparado)

**No se dispone de control directo sobre PCSX2 en esta sesión** (ver
historial del proyecto: se evaluó un servidor GDB para PCSX2 y se
descartó por no existir en builds oficiales — el flujo establecido es
que el usuario opera PCSX2 manualmente con las direcciones exactas que
se le indican). El plan mínimo, listo para ejecutar en una sola
sesión controlada que supere Memory Card → idioma → X → CAPCOM → PSS
(por instrucción explícita del prompt, no vale una sesión que no
llegue ahí):

1. Breakpoint de ejecución en `0x00109FF0` (`sceMpegGetPicture` ENTRY,
   verificado independientemente en 19.6). Al saltar, capturar: `a0`
   (mpeg), `a1` (imageAddr), `ra`.
2. Breakpoint de ejecución en `0x0047A8EC` (continuación real tras el
   `jal`, verificado en 19.6 — NO `0x0047A900`, ese es solo el destino
   del salto condicional si `v0>=0`). Al saltar, capturar: `v0`,
   tiempo transcurrido aproximado desde el breakpoint 1.
3. Si el breakpoint 2 tarda en llegar (más de un par de segundos),
   confirma sospecha de espera real en ORIGINAL también — anotar
   cuánto.
4. Si es practicable sin instrumentación adicional: un tercer
   breakpoint de escritura/lectura sobre el bit que gatilla
   `Movie_ctrl`'s llamada a `sceMpegDemuxPssRing` (candidato:
   revisar si ORIGINAL seguiría llamando a esa función en cada tick
   independientemente del estado de `MpegMovieDecode` — comparable
   con 19.7).

Sin esta sesión, los puntos 5 ("ORIGINAL") y parte de 12
("tabla completa") del prompt **no pueden completarse con evidencia
real** — se listan como PENDIENTE, no como inferencia.

### 19.10 `thread=-1` — identidad resuelta (HECHO, estático + dinámico)

**VALID, no BUG.** `EeScheduler::acquireInvocationThread()`
(`EeScheduler.cpp:1572-1588`) reserva IDs negativos
(`m_nextInvocationThreadId--`) exclusivamente para "invocation
threads" sintéticos que ejecutan `GuestInvocation`s puntuales
encoladas por el host (callbacks MPEG, RPC, GS VSync, etc.) — nunca
threads numerados del propio juego (esos usan IDs positivos, como los
`thread=1,2,5` vistos en 18.6). Identidad dinámica confirmada: el
único bloque `thread=-1` observado aparece **inmediatamente tras el
primer `[GP:VSync]`**, no tras `getPicture`, y coincide en timing con
el disparo de `m_gsVSyncCallback` (`EeScheduler.cpp:1895-1904`,
encolado en cada `VBlankStart`) — casi con certeza **el callback de
VSync del propio GS, completamente ajeno a MPEG**, no una continuación
de `waitExternal`. Su invocation stack está correctamente cacheado
(un log por combinación thread+profundidad, sin repetirse en miles de
ticks posteriores) — comportamiento esperado, no un error.

### 19.11 VSync — clasificación final

**ADVANCES**, no FROZEN. 1016 muestras (`[GP:VSync] tick=N`),
avanzando de forma continua y sostenida durante toda la ventana
observada (varios miles de ticks), sin ninguna señal de congelación.
Fuente real: eventos `EeEventType::VBlankStart` procesados en
`EeScheduler::processEvent` (`EeScheduler.cpp:1872-1894`), que
incrementan `m_vsyncTick` y llaman `completeVSync(m_vsyncTick)`
incondicionalmente en cada evento — este mecanismo **funciona
correctamente** y no es la causa del estancamiento observado.

### 19.12 Event delivery — primer borde que NO ocurre

Cadena completa (`VSync source -> event publication -> scheduler wake
-> waiter lookup -> continuation queue -> invocation -> re-entry
GetPicture`) **no aplica aquí** — la espera registrada es
`EeWaitReason::Mpeg`/`kMpegPictureWaitType`, no `EeWaitReason::VSync`;
VSync avanzando es irrelevante para despertarla. La cadena relevante
es: `sceMpegDemuxPssRing`/`Movie_ctrl` (productor) →
`completeExternalWait(kMpegPictureWaitType, mpegAddr, KE_OK)` →
`makeReady` → `resumeCompletion` → re-entrada a `sceMpegGetPicture`.
**El primer borde que NO ocurre es el primero de todos: `Movie_ctrl`
nunca vuelve a ejecutarse**, por lo que `sceMpegDemuxPssRing` nunca se
vuelve a llamar para este `mpegAddr`, por lo que `completeExternalWait`
nunca se genera. Todos los bordes posteriores (delivery, queueing,
ejecución de la continuación) son **N/A como consecuencia**, no fallos
independientes.

| Borde | PASS/FAIL/N-A |
|---|---|
| wait registered | PASS |
| Movie_ctrl sigue corriendo (productor del evento) | **FAIL — primer borde que no ocurre** |
| external event occurs (`completeExternalWait` llamado) | N/A (consecuencia) |
| scheduler receives | N/A |
| continuation queued | N/A |
| continuation executes | N/A |

### 19.13 Retractaciones e hipótesis previas

- **RETRACTADA**: "GetPicture → `presentationTickForFrame` →
  `waitVSync` → stall" (P3.7.1.1/Fable). Refutada por evidencia
  dinámica directa: la rama realmente tomada es `waitExternal`
  (`decodedFrames.empty()` en el ENTRY real), no la rama de
  `waitVSync` — `presentationTickForFrame`/`waitVSync` **no se
  alcanzan en absoluto** en esta ejecución.
- **CONFIRMADA**: el mecanismo `waitExternal`/`blockCurrent`/
  `EeDispatcherTransfer`/`makeReady`/`resumeCompletion` está bien
  diseñado y, por lo que se pudo revisar, correctamente implementado
  — el problema no está en la maquinaria de suspensión/reanudación en
  sí, sino en que nadie vuelve a producir el evento que la
  despertaría.
- **UNKNOWN, no investigado por instrucción explícita**: por qué
  `Movie_ctrl` deja de ejecutarse por-tick tras `Task_execute(3,
  MpegMovieDecode)`. Candidatos sin explorar: el propio driver externo
  de `Movie_ctrl` podría estar co-implementado con el mismo mecanismo
  de scheduling que `MpegMovieDecode`, de forma que suspender uno
  bloquee al otro; o `Task_execute` podría alterar la prioridad/cola
  de ready de forma que el thread que invoca `Movie_ctrl` deje de
  recibir CPU. **Ninguna de las dos se investigó** — es el punto
  exacto donde se detiene P3.8, tal como exige la regla de primera
  divergencia.

### 19.14 Archivos modificados solo para diagnóstico

- `vendor/PS2Recomp/ps2xRuntime/src/lib/Kernel/Stubs/MPEG.cpp` — tags
  `[GP:A]`, `[GP:B]`, `[GP:C]`, `[GP:D]`, `[GP:E]`, `[GP:G]`, `[GP:H]`,
  `[GP:I]`, `[GP:K]` en `sceMpegGetPicture`, capados a las primeras 6
  llamadas.
- `vendor/PS2Recomp/ps2xRuntime/src/lib/Kernel/EeScheduler.cpp` — log
  `[GP:VSync]` en `processEvent`'s `VBlankStart` (primeros 5 ticks +
  cada 60º), sin cambio de lógica.
- `analysis/local/symtabfirst/generated/MpegMovieDecode_0x47a760.cpp`
  — log `[GP:CONT]` en `label_47a8ec` (continuación real tras el
  `jal`), capado a 6.

**Ningún cambio funcional** — confirmado por diseño (todo son
`std::cerr` adicionales) y por resultado (la escalera P3.4-P3.7.1.2
se reprodujo idéntica antes de llegar al punto nuevo instrumentado).

### 19.15 Build exacto realizado

Árbol activo, sin clean build, sin regeneración masiva:
1. `ps2_runtime.vcxproj` incremental ×2 (una vez por cada archivo de
   runtime tocado) — 0 advertencias, 0 errores, ~4-5s cada vez.
2. **Incidente y reparación, documentado explícitamente**: un intento
   de build normal (no `SelectedFiles`, target por defecto) de
   `ps2EntryRunner.vcxproj` **también resultó ser un mass-compile**
   (no solo `SelectedFiles` estaba roto) — abortado a los 20s por
   instrucción explícita del prompt. El `taskkill` forzoso dejó **un**
   `.obj` corrupto (`Ak_set_0x1646e0.obj`, 0 bytes; su fuente no fue
   tocada por esta iteración). Reparado recompilando ese único archivo
   con `cl.exe` directo, sin tocar su código fuente. **Lección para
   iteraciones futuras**: ni `SelectedFiles` ni el target por defecto
   son fiables en este árbol; el único método confirmado seguro sigue
   siendo `cl.exe` directo por archivo + `_BuildLinkAction`.
3. `cl.exe` directo para `MpegMovieDecode_0x47a760.cpp` (el único
   generado tocado intencionalmente) + el archivo de reparación.
4. `_BuildLinkAction` final — 0 errores, únicamente el warning
   `LNK4075` preexistente.

### 19.16 Cambios funcionales

**NINGUNO.** Todo lo añadido en P3.8 es `std::cerr` de diagnóstico.

### 19.17 Próximo punto recomendado (NO implementado)

Investigar, en una futura iteración (P3.9), **por qué `Movie_ctrl`
deja de recibir CPU tras `Task_execute(3, MpegMovieDecode)`** —
específicamente: identificar el mecanismo real que invoca `Movie_ctrl`
por-tick (nunca completamente identificado en ninguna iteración
anterior — sección 6.2 lo dejó como "llamador externo no
investigado"), y determinar si `Task_execute`/`blockCurrent` alteran
su prioridad, su cola de ready, o si comparten thread/contexto de
alguna forma que explique por qué uno bloquea al otro. Esto, junto con
la sesión PCSX2 de 19.9, cerraría la comparación ORIGINAL/RECOMP
completa.

**Resultado Prompt P3.8 — GETPICTURE_PRESENTATION_WAIT_FIRST_DIVERGENCE**

## 20. Prompt P3.8.1 — contrato real de retorno de `sceMpegGetPicture` (ORIGINAL), corrección mayor sobre P3.8

Continuación directa de la sección 19. Aporta evidencia dinámica
ORIGINAL real (sesión PCSX2 operada por el usuario) que **corrige**
—sin borrar— la conclusión de cierre de P3.8. Sigue siendo
investigación pura: no se ha implementado ningún fix.

### 20.1 Evidencia dinámica ORIGINAL (HECHO, aportada por el usuario)

Breakpoints simultáneos, sesión real PCSX2, mismo ELF/ISO:

```
sceMpegGetPicture ENTRY:  PC=0x00109FF0  a0=0x0087D8F8  a1=0x0079D680  RA=0x0047A8EC
Movie_ctrl ENTRY:         0x001CE510
caller continuation:      0x0047A8EC
```

**El primer breakpoint en dispararse fue `0x0047A8EC`** (la
continuación del propio caller), no `Movie_ctrl ENTRY`. Es decir:
**`sceMpegGetPicture` en ORIGINAL retorna antes de que `Movie_ctrl`
vuelva a ejecutarse**, con `v0=0x00000001` en el retorno. `a0` en el
ENTRY (`0x0087D8F8`) es el **mismo mpeg handle** que RECOMP usa en su
propia primera llamada (sección 19.4) — confirma que ambas
observaciones corresponden al mismo evento causal, no a corridas no
comparables.

### 20.2 Verificación independiente — desensamblado directo de la función real (HECHO, estático, no aceptado del reporte sin comprobar)

`sceMpegGetPicture` en el ELF **no es un syscall de kernel**: es
código real de biblioteca (`libmpeg`, símbolo confirmado en
`sce_symbol_database_data.h`, tamaño 72 bytes — coincide exactamente
con `size=0x48` ya verificado en secciones anteriores). Se desensambló
completa, palabra a palabra, directamente desde el ELF:

```c
// 0x109FF0 - 0x10A038 (72 bytes reales)
void sceMpegGetPicture(a0=mpeg, a1=imageAddr, a2=...) {
    sp -= 16; sq $ra, 0(sp);              // prólogo estándar
    a1 = (a1 & 0xFFFFFFFF) | 0x20000000;  // exactamente igual que la HLE de RECOMP
    a3 = *(a0 + 0x40);                    // struct interno -- igual que la HLE
    *(a3 + 0xB0) = 1;                     // igual que la HLE
    *(a3 + 0xD8) = a1;                    // igual que la HLE
    *(a3 + 0xE4) = a2;                    // igual que la HLE
    *(a3 + 0xDC) = 0;                     // igual que la HLE
    *(a3 + 0xE0) = 0;                     // igual que la HLE (delay slot del jal siguiente)
    v0 = func_10A3D0(...);                // jal -- ÚNICA llamada real, resultado no tocado después
    lq $ra, 0(sp); sp += 16;
    return v0;                            // lo que devuelva func_10A3D0, sin condición alguna
}
```

**HECHO, confirmado por lectura directa**: la parte administrativa
(escritura de los campos `+0xB0/+0xD8/+0xDC/+0xE0/+0xE4` del struct
interno) es **idéntica, campo a campo**, a lo que ya hace la HLE de
RECOMP (`MPEG.cpp:2536-2547` en la numeración de la sección 19) — esa
parte **no es la divergencia** y está correctamente traducida.

**La divergencia real**: la función real **no contiene ninguna
comprobación equivalente a `decodedFrames.empty()`, ni ninguna
llamada equivalente a `waitExternal`/bloqueo del hilo**. Llama una
única vez a una función interna (`0x10A3D0`, no desensamblada en
detalle en esta iteración — candidato a ser el disparo real hacia
hardware IPU/MPEG) y **retorna inmediatamente con lo que esa función
devuelva**, sin ninguna espera dentro de `sceMpegGetPicture` mismo.

### 20.3 Interpretación — no se toma como asumida, se deriva de lo ya demostrado

El hardware MPEG/IPU real del PS2 decodifica de forma asíncrona
(impulsado por interrupción); `sceMpegGetPicture` es, por diseño, una
llamada de **consulta puntual, no bloqueante** — el propio código del
juego (`MpegMovieDecode`, ejecutado una vez por tick vía
`Task_execute`) es responsable de volver a preguntar en un tick
posterior si la respuesta anterior indicó que la imagen no estaba
lista todavía, exactamente como demuestra 20.1 (retorna rápido, con
`v0=1`, sin que `Movie_ctrl` tenga que ejecutarse primero).

### 20.4 RETRACTACIÓN explícita de parte de la sección 19, sin borrarla

**Se conserva íntegra toda la evidencia dinámica de 19.4-19.12** (es
real, se reprodujo, y sigue siendo evidencia válida de **qué hace
RECOMP hoy**). Lo que se retracta es la interpretación de
**causalidad** de 19.7/19.12/19.13:

> ~~"La primera divergencia real es que `Movie_ctrl` deja de
> ejecutarse por-tick; la suspensión de `sceMpegGetPicture` es
> upstream mecánicamente correcta."~~

queda sustituida por:

**La primera divergencia real es que la HLE de `sceMpegGetPicture` en
RECOMP implementa un contrato equivocado: trata la llamada como
bloqueante (`decodedFrames.empty() → waitExternal → suspende el hilo
guest completo`) cuando la función real es no bloqueante (retorna de
inmediato, con un código de estado, cualquiera que sea el resultado).
`Movie_ctrl` dejando de recibir CPU (sección 19.7) es una
consecuencia downstream exacta de esa suspensión indebida — RECOMP
bloquea el propio hilo que debería, en el modelo correcto, poder
seguir corriendo `Movie_ctrl` en el siguiente tick con total
normalidad.**

Último nodo común revisado: **ENTRY de `sceMpegGetPicture` con los
mismos argumentos** (`mpeg=0x87D8F8`, offsets administrativos
idénticos). Primera divergencia revisada: **la propia decisión de
bloquear vs. retornar de inmediato**, no ningún efecto posterior.

### 20.5 UNKNOWN explícito, no investigado en esta iteración

- Semántica exacta de `v0` como código de estado (¿`1`=imagen no
  lista todavía / hay que reintentar? ¿`1`=éxito genérico? ¿el valor
  cambia según el estado real del hardware?) — no se desensambló
  `func_10A3D0` (`0x10A3D0`), que es quien realmente produce ese
  valor.
- Si `v0` varía entre llamadas sucesivas en ORIGINAL (p. ej. distinto
  una vez que sí hay imagen disponible) — no se ha capturado una
  segunda invocación real.
- Qué debe escribir RECOMP en `width`/`height`/`frameCount`/el buffer
  de imagen cuando decide "no bloquear" — no se ha diseñado ningún
  reemplazo, por instrucción implícita de no fijar todavía ningún fix.

### 20.6 No implementado

Igual que en toda la serie P3.4-P3.8: **ningún cambio de
comportamiento**. Esta sección documenta evidencia y su
interpretación causal corregida, no una propuesta de código.

**Resultado Prompt P3.8.1 — ORIGINAL_GETPICTURE_RETURN_CONTRACT**

## 21. Prompt P3.9 — contrato no bloqueante real de `sceMpegGetPicture` (ORIGINAL)

Continuación directa de la sección 20. **Investigación pura, sin fix,
sin commit.** BLOCKER_004 sigue **OPEN**. Toda la evidencia es
estática (desensamblado directo del ELF, con un desensamblador R5900
propio corregido — ver 21.1) más la evidencia dinámica ya aportada en
P3.8.1. No se ejecutó ninguna sesión PCSX2 nueva en esta iteración
(el plan para una futura queda en 21.10).

### 21.1 Corrección de herramienta (HECHO, antes de cualquier hallazgo)

El desensamblador manual usado en P3.8.1 tenía un hueco: decodificaba
`0x3F`/`0x1F` como si fueran `SQ`/`LQ` (extensión de 128 bits propia
de EE) cuando en realidad, para las palabras observadas, son `SD`/`LD`
(store/load doubleword de 64 bits, MIPS III estándar, válido en R5900).
Corregido y verificado antes de reutilizarlo — la instrucción de
prólogo de `sceMpegGetPicture` es `sd $ra,0($sp)` / `ld $ra,0($sp)`,
no `sq`/`lq`. **No cambia ninguna conclusión sustantiva de P3.8.1**
(el resto de la reconstrucción — bookkeeping, `v0=1` como constante
local, `jal 0x10A3D0`, propagación del retorno — se reverifica
idéntica con el desensamblador corregido, ver 21.2).

### 21.2 `sceMpegGetPicture` ORIGINAL — reconfirmado (HECHO)

`0x00109FF0`-`0x0010A038` (72 bytes, símbolo real `sceMpegGetPicture`,
`libmpeg`). Pseudocódigo semántico, revalidado:

```c
int32_t sceMpegGetPicture(a0=mpeg, a1=imageAddr, a2=arg2) {
    a1 = (a1 & 0xFFFFFFFF) | 0x20000000;      // máscara/flag empaquetado en la dirección
    a3 = *(a0 + 0x40);                         // struct interno ("inner")
    *(a3 + 0xB0) = 1;                          // constante local -- NUNCA es el valor de retorno
    *(a3 + 0xD8) = a1;
    *(a3 + 0xE4) = a2;
    *(a3 + 0xDC) = 0;
    *(a3 + 0xE0) = 0;
    v0 = _getpic(a3, ...);                      // ÚNICA llamada real -- ver 21.3
    return v0;                                   // sin tocar v0 después del jal
}
```

Side effects revalidados uno a uno (Objetivo 3 del prompt):

| Offset | Valor | Origen | Antes/después de `_getpic` | Clasificación |
|---|---|---|---|---|
| `+0xB0` | `1` | constante local | antes | bookkeeping administrativo (flag "función llamada", no resultado) |
| `+0xD8` | `a1 processed` | argumento del caller | antes | parámetro — dirección de imagen empaquetada |
| `+0xE4` | `a2` | argumento del caller | antes | parámetro, sin decodificar en esta iteración — UNKNOWN semántica exacta |
| `+0xDC` | `0` | constante | antes | reset de estado, propósito exacto UNKNOWN |
| `+0xE0` | `0` | constante | antes (delay slot del `jal`) | reset de estado, propósito exacto UNKNOWN |

Ninguno de estos 5 campos es el resultado de la decodificación —
todos se escriben **antes** de que `_getpic` haga ningún trabajo real.
**Coinciden exactamente, campo a campo, con lo que ya escribe la HLE
actual de RECOMP** (`MPEG.cpp`, líneas ya citadas en la sección 20.2)
— esta parte de la HLE es correcta.

### 21.3 `_getpic` (`func_0x10A3D0`) — reconstrucción completa (HECHO)

**Símbolo real confirmado**: `_getpic @ 0x0010A3D0`, tamaño `0x184`
(388 bytes), fin `0x0010A554`. Desensamblado íntegro (97
instrucciones, sin huecos, mediante un script propio de desensamblado
R5900 reutilizable, mantenido fuera del repositorio en el directorio
de trabajo temporal de la sesión).

**Hallazgo central**: `_getpic` **no consulta hardware IPU por
polling externo — es el propio bucle de parseo/decodificación MPEG en
software**, ejecutado síncronamente dentro de esta única llamada.
Confirmado por los NOMBRES REALES de todo lo que toca (no inferidos,
tomados directamente de `.symtab`):

```c
int32_t _getpic(a0 = inner_struct, ...) {
    s3 = a0; s4 = 0; s5 = 1;
    a1 = *(s3->mpeg + 0x40)->+0xD8;           // relee lo que sceMpegGetPicture acaba de escribir
    if ((a1 & 0x3F) != 0) {
        *(s0+0) = 0;
        _Error1("...");                        // 0x10AA60, mensaje de error real
        return -1;                              // ÚNICO otro valor de retorno demostrado
    }
    *(s0+0) = 0;
    _isOutputPicture = 0;                        // *0x4FC800 = 0 (reset del flag de salida)
    do {
        if (s4 != -1) {                          // s4 = resultado de la última llamada a _decodeOrSkip
            do {
                s5 = _nextHeader(...);            // 0x106FF0 -- avanza al siguiente header/start-code MPEG real
            } while (s5 != 0 &&
                     _picture_structure == *(s0+0xD4) &&   // *0x4FC12C vs campo del struct
                     _isMpeg2 != 0);                        // *0x4FC830
        }
        if (s5 < 5) {
            switch (s5) {                          // tabla de saltos real en 0x0058D810, 5 entradas
                case 1:
                    _sceMpegFlush(s3);              // 0x10A830 -- la MISMA función real detrás de sceMpegFlush
                    *(s0+0) = 1;
                    break;                           // -> fin del switch, re-evalúa el while externo
                case 2:  // (offsets +0xA0/+0x94)
                    *(s0+0xA8)=*(s0+0xA4)=*(s0+0xA0)=0;
                    s4 = _decodeOrSkip(s3, 0, *(s0+0x94));   // 0x10A680 -- decodifica/descarta UN plano
                    *(s0+0xA0) += 1;
                    break;
                case 3:  // (offsets +0xA4/+0x98) -- mismo patrón, otro plano
                    s4 = _decodeOrSkip(s3, *(s0+0xA4), *(s0+0x98));
                    *(s0+0xA4) += 1;
                    break;
                case 4:  // (offsets +0xA8/+0x9C) -- tercer plano
                    s4 = _decodeOrSkip(s3, *(s0+0xA8), *(s0+0x9C));
                    *(s0+0xA8) += 1;
                    break;
            }
        }
        // si s5>=5 -- salto directo aquí, sin ejecutar el switch
    } while (_isOutputPicture == 0);                // *0x4FC800 -- condición de salida real del bucle
    return 1;                                          // ÚNICO valor de éxito demostrado
}
```

### 21.4 Todos los return paths demostrados (Objetivo 5)

| Path | Condición | v0 | Side effects |
|---|---|---|---|
| Error de alineación | `(imageAddr & 0x3F) != 0` al entrar | **-1** | `*(inner+0)=0`; llama `_Error1` (mensaje real, no desensamblado en detalle) |
| Éxito | `_isOutputPicture != 0` (se puso a 1 en algún punto del bucle interno) | **1** | `*(inner+0)` puede quedar en `0` o `1` según qué caso del switch se ejecutó por última vez; contadores `+0xA0/0xA4/0xA8` incrementados según los planos decodificados |

**No existe ningún tercer valor de retorno demostrado** en esta
función — ni "0 = pendiente, reintenta", ni ningún otro código. Esto
se investigó explícitamente (Objetivo 5 del prompt: "no asumir cuáles
existen, demostrar desde código") y **no se encontró ninguna otra
rama de `return`** en las 97 instrucciones desensambladas — todas las
salidas convergen en `0x10A52C` (epílogo único) con `v0` fijado en
exactamente `-1` o `1` en el camino hacia ahí.

### 21.5 Significado demostrado de `v0=1` (Objetivo 6)

**Rastreado hasta su origen exacto**: `v0=1` proviene de
`0x10A528: addiu v0,zero,1`, alcanzado únicamente cuando el bucle
`do...while(_isOutputPicture==0)` termina porque **`_isOutputPicture`
(offset absoluto `0x4FC800`, símbolo real) se puso a distinto de
cero** en algún punto de una iteración anterior del bucle.
`sceMpegGetPicture` no transforma este valor — lo propaga sin tocarlo.

**Clasificación semántica** (derivada del nombre real del flag, no
inventada): **`v0=1` significa "hay una imagen de salida lista"** —
el nombre `_isOutputPicture` es exactamente eso: un flag MPEG estándar
que distingue fotogramas decodificados que deben mostrarse
(picture_coding_type relevante para salida) de los que no (p. ej.
fotogramas de referencia no destinados a presentación directa en
ciertos esquemas de codificación). Es más preciso llamarlo
**"FRAME READY"** que "SUCCESS genérico" — la función literalmente
hizo el trabajo de decodificación necesario, en bucle, hasta que una
imagen presentable quedó lista, y solo entonces retornó.

### 21.6 Comportamiento cuando el frame NO está listo todavía vs cuando SÍ lo está (Objetivos 3-4 de la salida)

**No hay un "todavía no" observable desde fuera de la función**: dado
que `_getpic` **bucla internamente** hasta que `_isOutputPicture` se
activa (o hasta agotar estados del switch sin activarlo, lo cual no
se demostró que ocurra — no se encontró ningún camino de salida del
`do...while` distinto al chequeo de `_isOutputPicture`), el "todavía
no" del diseño original **se resuelve dentro de la misma llamada**,
no se expone al caller como un estado de retorno distinto. El caller
nunca ve "está pendiente" — o ve `-1` (error de alineación, ni
siquiera llegó a intentar decodificar) o ve `1` (ya se decodificó lo
necesario).

### 21.7 `MpegMovieDecode` — uso real de `v0` (Objetivo 7, revalidado más allá del `bgez`)

**HECHO, releído completo desde el generado real**
(`analysis/local/symtabfirst/generated/MpegMovieDecode_0x47a760.cpp`):

```c
// 0x47A8EC: bgez v0, 0x47A900   (si v0>=0, salta; si v0<0, cae)
if (v0 < 0) {
    printf("...");    // 0x47A8F4-0x47A8FC, mensaje de error real
    // cae a 0x47A900 de todas formas (el printf no desvía el flujo)
}
label_0x47A900:
    v0 = *(s1 + 0x8);              // *** SOBREESCRIBE v0 inmediatamente ***
                                     // s1 = mismo puntero al struct mpeg (cacheado)
                                     // offset +0x8 = "frameCount", el MISMO campo que
                                     // sceMpegGetPicture (real Y HLE) escribe antes de retornar
    if (v0 != 0) goto 0x47AA18;     // rama alternativa, no explorada en detalle en esta iteración
    // v0 == 0 (frameCount==0): continúa en 0x47A90C, procesa el struct,
    //   pone un bit (0x742158 |= 0x80, el mismo global "Sys" de secciones
    //   anteriores) y llama a func_19F9D0
```

**Hallazgo importante, no anticipado**: **el valor exacto de `v0` de
`sceMpegGetPicture` (1 frente a cualquier otro no-negativo) es
irrelevante para el caller más allá de su signo** — la decisión real
de "¿hay algo que procesar?" la toma leyendo `*(mpegAddr+0x8)`
(`frameCount`), un campo que **la HLE de RECOMP ya escribe
correctamente hoy** (`mpegGuestWrite32(rdram, mpegAddr+0x08,
frameCount)`, ya presente en el código actual). Esto significa que
**una HLE no bloqueante correctamente diseñada no necesita inventar
ninguna semántica nueva para `v0`** — basta con:
1. Nunca devolver negativo salvo en un error genuinamente equivalente
   al de alineación (`imageAddr` inválido) — caso que RECOMP no
   necesita replicar tal cual, pero debe evitar retornar negativo por
   accidente.
2. Escribir `frameCount` correctamente según si esta llamada concreta
   produjo o no una imagen nueva — **ya lo hace**.

**No se investigó la rama `0x47AA18`** (`frameCount != 0` al entrar)
en esta iteración — UNKNOWN, candidato para una iteración futura si
resulta relevante.

### 21.8 Retry/polling del juego (Objetivo 8)

**Revisado, no reabierto el scheduler completo**: dado que `_getpic`
resuelve el "todavía no" *dentro* de sí misma (21.6), **no hay, a
nivel de este par de funciones, un mecanismo de reintento expuesto**
— `MpegMovieDecode` llama una vez por tick (vía `Task_execute`, ya
demostrado en secciones anteriores) y, en el diseño original, cada
llamada a `sceMpegGetPicture` ya viene con una imagen lista o con un
error, nunca con "vuelve a preguntar". El "polling" real del diseño
completo ocurre a un nivel superior: es el propio ciclo por-tick de
`Movie_ctrl`/`Task_execute` el que determina CUÁNDO se vuelve a
invocar `MpegMovieDecode` (y por tanto `sceMpegGetPicture`) — no algo
que `sceMpegGetPicture` gestione internamente.

### 21.9 `imageAddr` — cuándo y cómo se escribe (Objetivo 11)

**No determinado con precisión en esta iteración** (UNKNOWN
explícito): `_decodeOrSkip` (`0x10A680`, 60 bytes) es quien
presumiblemente escribe los datos de imagen decodificados, pero no se
desensambló su cuerpo. Lo que SÍ queda demostrado: la escritura (si
ocurre) sucede **dentro del bucle de `_getpic`**, síncronamente,
**antes** de que `sceMpegGetPicture` retorne — no hay ninguna
llamada/mecanismo posterior, asíncrono, que complete la transferencia
después del retorno. Esto confirma que el acoplamiento temporal que
RECOMP asume hoy (`decodedFrames.pop_front() → copy → guest buffer`,
todo dentro de la misma llamada a `sceMpegGetPicture`) **sí refleja
correctamente el diseño original** en cuanto a "todo pasa en la misma
llamada" — la diferencia no está en EL ACOPLAMIENTO TEMPORAL sino en
QUÉ HACE LA LLAMADA SI LOS DATOS AÚN NO ESTÁN DISPONIBLES (RECOMP
suspende todo el hilo guest; ORIGINAL sigue decodificando en el mismo
call hasta tenerlos).

### 21.10 Interacción con hardware IPU (Objetivo 12)

**No se encontró ninguna interacción directa con registros IPU
(`0x10002000`+) dentro de `_getpic` ni de las funciones que llama**
(`_nextHeader`, `_decodeOrSkip`, `_sceMpegFlush`, `_Error1`) — todos
los accesos vistos en el desensamblado de `_getpic` son a
direcciones/estructuras de memoria normal (`0x4FC0xx-0x4FC8xx`,
`0x58Dxxx`), no a rangos de registros de hardware conocidos de este
proyecto. **No se descarta** que `_decodeOrSkip` (no desensamblado)
acceda a IPU internamente — queda UNKNOWN, no investigado más a fondo
por estar fuera del alcance mínimo pedido ("NO hacer una investigación
general del decodificador").

### 21.11 EOF y error (Objetivo 16)

**Único error demostrado**: `imageAddr` con los 6 bits bajos
distintos de cero (`v0=-1`, con log real vía `_Error1`). **No se
alcanzó ni se pudo demostrar en esta iteración** ningún camino de EOF
explícito dentro de `_getpic` mismo — el caso `s5==1` del switch
(`_sceMpegFlush` + `*(s0+0)=1`) es el candidato más plausible para
relacionarse con EOF/flush, pero no se confirmó la condición exacta
que hace que `_nextHeader` devuelva `s5==1`. **UNKNOWN**, no inventado.

### 21.12 Tabla ORIGINAL vs RECOMP HLE (Objetivo 13)

| Concepto | ORIGINAL | RECOMP HLE actual | Match/Different/Unknown |
|---|---|---|---|
| Bloqueo | NO — bucle síncrono interno hasta completar | SÍ — `waitExternal`, suspende el hilo guest completo | **DIFFERENT — divergencia raíz** |
| Return inmediato | Solo tras completar (bucle acotado, no infinito en la práctica) | Nunca si `decodedFrames.empty()` (no retorna hasta reanudación externa) | **DIFFERENT** |
| Valor de retorno | `-1` (error alineación) o `1` (imagen lista); sin tercer valor | Siempre `0` (`setReturnS32(ctx,0)` en todos los caminos síncronos) | **DIFFERENT**, pero irrelevante para el caller (21.7: solo importa el signo) |
| Disponibilidad de frame | Se resuelve dentro de la llamada (bucle) | Se resuelve fuera, de forma asíncrona (pipeline FFmpeg independiente) | **DIFFERENT — arquitectura**, ver 21.13 |
| Bookkeeping struct (`+0xB0/+0xD8/+0xDC/+0xE0/+0xE4`) | HECHO, ver 21.2 | Ya implementado, idéntico campo a campo | **MATCH** |
| `frameCount` (`+0x8`) como señal real para el caller | HECHO, confirmado en 21.7 | Ya escrito correctamente (`playback.picturesServed`) | **MATCH** |
| EOF | No confirmado en esta iteración | Vía `streamEnded`/`decoderFailed`/`currentCdStreamEofSeen`, ya implementado | **UNKNOWN** si coincide en semántica exacta |
| Error | `v0=-1` por `imageAddr` mal alineado | HLE actual no tiene ningún camino de retorno negativo | **DIFFERENT**, bajo impacto (RECOMP nunca genera esa condición hoy) |
| Retry/polling | Implícito en el ciclo por-tick externo (`Movie_ctrl`/`Task_execute`), no dentro de la función | No aplica — RECOMP delega el "retry" al mecanismo de reanudación de `waitExternal` | **DIFFERENT — mecanismo**, ver 21.13 |
| Escritura de imagen | Síncrona, dentro de la misma llamada (21.9) | Síncrona, dentro de la misma llamada (cuando `haveFrame=true`) | **MATCH en acoplamiento temporal** |
| Presentation timing (`presentationTickForFrame`/`waitVSync`) | No alcanzado por esta rama en ningún momento observado (21.6, `_getpic` no lo referencia en absoluto) | Existe como rama posterior en la HLE, alcanzable solo si `decodedFrames` ya no está vacío al entrar | **UNKNOWN si es conceptualmente correcto** — no se encontró nada equivalente en `_getpic`, pero tampoco se ha demostrado que esa rama de la HLE sea la causa de ningún problema — ver 21.13 |

### 21.13 Aspectos de la HLE claramente incorrectos vs correctos (Objetivos 14-15)

**Incorrectos, demostrados**:
- El uso de `waitExternal` para suspender el hilo guest completo
  cuando `decodedFrames.empty()` — el original nunca suspende el
  hilo; resuelve la espera con un bucle interno acotado.
- La arquitectura de fondo (pipeline FFmpeg asíncrono, independiente
  del momento exacto de la llamada a `GetPicture`) no reflaja el
  diseño original, donde la decodificación ocurre **dentro** de la
  llamada, consumiendo bytes ya bufferizados por el demux previo.

**Correctos, demostrados, no tocar**:
- El bookkeeping administrativo del struct (`+0xB0/+0xD8/+0xDC/+0xE0/+0xE4`).
- La escritura de `frameCount`/`width`/`height` en el struct guest
  (offsets `+0x00/+0x04/+0x08`), y el hecho de que el caller decide su
  siguiente paso mirando esos campos, no el valor crudo de `v0`.
- El acoplamiento temporal "decodificar y escribir la imagen dentro de
  la misma llamada" — no hace falta introducir asincronía adicional
  ahí, solo eliminar la SUSPENSIÓN cuando no hay datos todavía.

### 21.14 Papel real de `waitExternal`/`waitVSync`/`presentationTickForFrame` (Objetivo 16)

- **`waitExternal`**: **sin contraparte en el original** para esta
  función — confirmado, no una hipótesis. Es la pieza a reemplazar.
- **`waitVSync`/`presentationTickForFrame`**: **no alcanzados por
  `_getpic` en ningún momento** — el original no gatea la
  disponibilidad de imagen contra VSync dentro de esta función. Esto
  **no prueba que esas ramas de la HLE sean conceptualmente erróneas
  en general** (podrían reflejar un mecanismo de *pacing*/framerate
  del lado RECOMP añadido deliberadamente para el pipeline asíncrono
  actual) — pero si la arquitectura pasa a ser síncrona como el
  original, dejarían de tener sentido en su forma actual. **No se
  recomienda eliminarlas todavía** — su rol correcto en un diseño no
  bloqueante queda UNKNOWN, a resolver en la fase de diseño (no en
  P3.9).

### 21.15 UNKNOWNs de esta iteración

- Cuerpo de `_decodeOrSkip` (`0x10A680`) — quién escribe realmente los
  píxeles y si toca IPU.
- Cuerpo de `_nextHeader` (`0x106FF0`) — el parser real del bitstream,
  no desensamblado en detalle (más allá de su rol como productor del
  índice de estado `s5`).
- Condición exacta bajo la cual `_nextHeader` devuelve cada uno de los
  5 estados de la tabla de saltos (`s5=0..4`).
- Semántica exacta de `+0xE4`/`+0xDC`/`+0xE0` del struct interno.
- Rama `0x47AA18` de `MpegMovieDecode` (`frameCount!=0` al entrar).
- Comportamiento EOF explícito dentro de `_getpic`.
- No se realizó ninguna sesión PCSX2 nueva — las muestras adicionales
  (segunda llamada, llamada con imagen ya disponible) siguen
  pendientes.

### 21.16 Plan de breakpoints para una futura sesión PCSX2 (preparado, no ejecutado)

Mínimo, para distinguir los 3 caminos principales sin pedir decenas de
pasos al usuario (recordatorio: la sesión debe superar Memory Card →
idioma → X → CAPCOM → PSS antes de que cuente):

1. `0x00109FF0` (`sceMpegGetPicture` ENTRY) — capturar `a0`, `a1`, `a2`.
2. `0x0010A424` y `0x0010A524`/`0x0010A528` (los dos puntos donde `v0`
   queda fijado antes del epílogo común en `0x0010A52C`) — capturar
   cuál se alcanza primero y el valor de `a1`/`a2` de esa llamada, sin
   necesidad de step-by-step manual.
3. Watchpoint de escritura sobre `0x004FC800` (`_isOutputPicture`) —
   confirma en qué instante exacto, dentro de la misma llamada, se
   activa la condición de salida del bucle.
4. Para una segunda muestra: repetir 1-3 dejando correr la película
   unos segundos más, para capturar una llamada posterior con
   `frameCount` ya distinto de 0 en el caller.

### 21.17 Build/instrumentación

**Ninguna** — P3.9 fue exclusivamente análisis estático del ELF
(`original/SLES_503.58`) con un desensamblador propio, más relectura
del código ya generado (`MpegMovieDecode_0x47a760.cpp`). No se tocó
ningún archivo de `vendor/PS2Recomp` ni `generated/` en esta
iteración. No hubo build.

**Resultado Prompt P3.9 — ORIGINAL_GETPICTURE_NONBLOCKING_CONTRACT**

## Sección 22 — Prompt P3.10: GETPICTURE_SYNC_DECODE_INPUT_GAP

### 22.1 Objetivo y método

Pregunta central: los logs históricos muestran "frame decoded, total=4"
(feed call #49) ANTES de la primera llamada real a `sceMpegGetPicture`
(`decodedFrames=0, sawInput=0`, ver sección 19). ¿Dónde está el video
input que el `_getpic` ORIGINAL (sección 21) podría consumir
síncronamente en ese mismo punto? Investigación pura, sin fix. Método:
lectura completa del ciclo de vida de `MpegPlaybackState` en el código
actual (no notas antiguas), instrumentación mínima con `mpegAddr`
añadida donde faltaba, build incremental seguro, ejecución real con
ELF+ISO real y FFmpeg activo, reconciliación de logs.

### 22.2 Candidato descartado en fase estática: `notifyMpegCdStreamStart` es código muerto

**RETRACTADO (dentro de esta misma iteración, antes de cualquier
build)**: la primera lectura de `MPEG.cpp` identificó
`notifyMpegCdStreamStart()` (1899-1923, iteraba `playbackByMpeg`
completo refrescando cada entrada) como candidato fuerte para el CASO
C. Verificación con `grep` de `notifyMpegCdStreamStart` en **todo el
repositorio** (no solo `MPEG.cpp`) devolvió **0 resultados** fuera de
su propia definición: la función está definida pero **nunca se
invoca** desde ningún punto del código actual. Descartado como
explicación — código muerto, no puede ser la causa de nada observable
en una corrida real. Lección aplicada: no aceptar un mecanismo "creíble
por lectura" sin verificar que tiene al menos un call site real.

### 22.3 Mecanismo real, activo, encontrado por lectura estática: `sceMpegCreate` sobrescribe sin condición

`sceMpegCreate` (`MPEG.cpp`, HLE) contiene, sin guardas:
```cpp
getPlaybackState(param_1) = makeFreshPlaybackState();
```
Esto **sí** es una syscall real invocada por el juego (a diferencia de
22.2). Se instrumentó (ver 22.5) para registrar, en cada llamada, si
había un estado previo con frames/`sawInput` no triviales a punto de
perderse. **Resultado dinámico (22.6): descartado como causa de esta
divergencia específica** — solo hay **una** llamada a `sceMpegCreate`
en toda la corrida (`hadExisting=0`), ocurre ANTES de cualquier feed,
así que no hay nada que perder en ese punto.

### 22.4 Mecanismo real, activo, encontrado por lectura estática: resync por `waitingForVideoSequenceHeader`

Dentro de `feedElementaryStream`, si `playback.waitingForVideoSequenceHeader`
está activo y se localiza un nuevo start code de secuencia MPEG en el
buffer de resync, el código ejecuta `playback.decoder.reset();
playback.decodedFrames.clear();` (mecanismo LOCAL, no requiere que
cambie `cdStreamGeneration`). Se instrumentó con un log
`[MPEG:resync-pending]` que se dispara ANTES de que ocurra el clear,
si `decodedFrames` no está vacío en ese instante. **Resultado dinámico
(22.6): descartado** — 0 ocurrencias de `[MPEG:resync-pending]` en la
corrida real.

### 22.5 Instrumentación añadida (mínima, aditiva, sin cambiar comportamiiento)

Archivo único tocado: `vendor/PS2Recomp/ps2xRuntime/src/lib/Kernel/Stubs/MPEG.cpp`.

1. `feedElementaryStream` gana un parámetro `uint32_t mpegAddr` (ambos
   call sites, `processPssBuffer` y `sceMpegAddBs`, ya tenían el valor
   en scope — paso puramente mecánico). Los logs `[MPEG:feedES]`,
   `[MPEG:diag] frame decoded`/`feed call #N` quedan etiquetados con
   `mpeg=0x...`.
2. Nuevo log `[MPEG:resync-pending]` (ver 22.4).
3. `sceMpegCreate` gana un log `[MPEG:create] #N mpeg=0x...
   hadExisting=B existingFrames=N existingSawInput=B` justo antes de la
   sobrescritura (ver 22.3).
4. `sceMpegDelete` gana un log `[MPEG:delete]` equivalente antes del
   `.erase()`.
5. `sceMpegInit` gana un log `[MPEG:init] #N trackedStates=N` más una
   línea por cada entrada de `playbackByMpeg` que tenga frames o
   `sawInput` no triviales, justo antes de `resetMpegStubStateUnlocked()`
   (ver 22.7 — esta fue la instrumentación decisiva).

Build: únicamente `ps2_runtime.vcxproj` (destino por defecto, MSBuild
directo — confirmado seguro repetidas veces esta sesión, distinto de
`ps2EntryRunner.vcxproj`) para compilar `MPEG.cpp`, seguido de
`ps2EntryRunner.vcxproj` con el target `_BuildLinkAction` únicamente
para relink. **0 archivos generados recompilados en ningún momento**
(verificado: 0 invocaciones de `CL.exe` en el log de link). Un primer
intento de link falló con `LNK1104` porque el `.exe` de la corrida
anterior seguía abierto (proceso no terminado) — no relacionado con el
mecanismo de mass-compile ya conocido; se resolvió matando el proceso y
reintentando el mismo target seguro.

### 22.6 Incidente lateral: reproducción accidental de BLOCKER_002 por variable de entorno faltante

La primera ejecución de esta iteración se lanzó sin `PS2X_CD_IMAGE`
(asumiendo erróneamente que las rutas de CD se resolvían solas desde
el directorio del ELF). Resultado: `[CDMODULE] no CD image configured
... cannot service real read` repetido, seguido de
`[guest-branch:missing-target] ... target=0x2000100 ...` — el mismo
patrón de crash **ya documentado y cerrado** como BLOCKER_002 (ver
`BLOCKER_002_indirect_jump_top_of_ram.md`, FASE L.7: mismo target,
mismo `v1`, mismo `s1`). **HECHO, confirmado por coincidencia exacta
con evidencia ya archivada**: la ausencia de `PS2X_CD_IMAGE` reabre
determinísticamente BLOCKER_002; no es una regresión nueva ni tiene
relación con la instrumentación de esta iteración. La ruta real de la
ISO (`<ISO_PATH>`, ver `PS2X_CD_IMAGE`) no estaba documentada en ningún
archivo del repositorio ni persistía en el entorno de la sesión — la
proporcionó el usuario directamente. Corrida inválida descartada sin
usar sus datos para ninguna conclusión.

### 22.7 Evidencia dinámica decisiva (corrida válida, ELF+ISO real, FFmpeg ON, override de arena DMC)

Secuencia completa y exacta observada (mismo proceso, mismo hilo de
interpretación, sin reordenamiento):

```
[MPEG:create] #1 mpeg=0x87d8f8 hadExisting=0 existingFrames=0 existingSawInput=0
[MPEG:diag] feed call #1 mpeg=0x87d8f8 size=4074 fedOk=1 framesSoFar=0
...
[MPEG:diag] frame decoded, total=1 mpeg=0x87d8f8 (feed call #9)
[MPEG:diag] frame decoded, total=2 mpeg=0x87d8f8 (feed call #23)
[MPEG:diag] frame decoded, total=3 mpeg=0x87d8f8 (feed call #36)
[MPEG:diag] frame decoded, total=4 mpeg=0x87d8f8 (feed call #49)
[MOVIE:decode-task] ENTRY #1
[MPEG:init] #1 trackedStates=1
[MPEG:init]   about to wipe mpeg=0x87d8f8 frames=4 sawInput=1
[MOVIE:decode-task] CALL sceMpegGetPicture
[MPEG:diag] getPicture ENTRY #1 mpeg=0x87d8f8
[GP:A] #1 entry mpeg=0x87d8f8 decodedFrames=0 streamEnded=0 decoderFailed=0 cdStreamEofSeen=0 sawInput=0 picturesServed=0
[GP:H] #1 waitExternal(kMpegPictureWaitType, mpeg=0x87d8f8) -- SUSPENDING, no frame available
```

**HECHO, DEMOSTRADO DINÁMICAMENTE, sin ambigüedad**: `sceMpegCreate` se
llama **una sola vez** en toda la corrida, para el mismo `mpegAddr`
(`0x87d8f8`) que consulta `GetPicture` — se descarta ownership cruzado
(CASO B) y recreación explícita (22.3). `[MPEG:resync-pending]` no
aparece ni una vez — se descarta el mecanismo de 22.4. El log
`[MPEG:init]`, añadido en esta iteración, muestra que **exactamente en
el primer tick de `MpegMovieDecode`** (`ENTRY #1`), **antes** de que se
registre `CALL sceMpegGetPicture`, se ejecuta `sceMpegInit()` con
`playbackByMpeg` conteniendo **exactamente la entrada de
`mpeg=0x87d8f8` con 4 frames y `sawInput=1`** — justo el estado
producido por los 49 feed calls previos. `sceMpegInit` hace
`resetMpegStubStateUnlocked()` → `g_mpeg_stub_state.playbackByMpeg.clear()`
(borrado completo del mapa, no solo de campos). La siguiente
`getPlaybackState(0x87d8f8)` (dentro de `sceMpegGetPicture`) auto-crea
una entrada nueva y vacía vía `unordered_map::operator[]` — sin pasar
por `sceMpegCreate`, por lo que `[MPEG:create]` no se repite. Esto
coincide con precisión total con `[GP:A]`: `decodedFrames=0,
sawInput=0`.

### 22.8 Mecanismo exacto confirmado estáticamente (bytes MIPS reales, ELF, no Ghidra)

`MpegMovieDecode_0x47a760.cpp` (código real recompilado desde el ELF,
`symtabfirst`), instrucción por instrucción desde la entrada de la
función:

```
0x47a760: addiu $sp,$sp,-0x30      ; prólogo
...
0x47a784: jal   func_47A6E0        ; MpegMovieDecodeInit — llamada incondicional,
                                    ; primera acción real de la función, ANTES
                                    ; de cualquier chequeo (incluido el primer
                                    ; sceMpegIsEnd en 0x47a78c)
```

`MpegMovieDecodeInit_0x47a6e0.cpp`, desde su propia entrada:

```
0x47a6e0: addiu $sp,$sp,-0x20      ; prólogo
0x47a6e4: sq    $ra,0x10($sp)
0x47a6e8: sq    $s0,0x0($sp)
0x47a6ec: jal   func_109CD0        ; sceMpegInit — incondicional, es la
                                    ; CUARTA instrucción de la función, sin
                                    ; ningún branch/test previo
```

`func_109CD0` es la dirección real de `sceMpegInit` en `libmpeg`
(confirmado por symtab — el archivo generado `sceMpegInit_0x109cd0.cpp`
reenvía a `ps2_stubs::sceMpegInit`, igual que se confirmó para
`sceMpegGetPicture` en la sección 21). **HECHO, demostrado
estáticamente byte a byte**: `MpegMovieDecode` llama SIEMPRE,
incondicionalmente, a `MpegMovieDecodeInit` como su primera acción, y
`MpegMovieDecodeInit` llama SIEMPRE, incondicionalmente, a
`sceMpegInit` como su primera acción real (tras el prólogo). Esto es
código real del juego (bytes MIPS del ELF `SLES_503.58`), no un
artefacto de RECOMP ni de la heurística de recompilación — el mismo
comportamiento ocurriría en hardware/PCSX2 real.

**Nota de precisión sobre el mapeo previo (sección 22, no retractación
de hallazgos anteriores)**: en la investigación estática inicial de
esta misma iteración se había mapeado un ÚNICO call site de
`sceMpegInit` dentro de `MpegMovieDecode_0x47a760.cpp` (label_47aa70,
alcanzable por 3 rutas: `sceMpegIsEnd!=0` temprano, `Fade_status&4`, o
caída normal tras el segundo chequeo `sceMpegIsEnd` al final del
tick). La evidencia dinámica (22.7) muestra `sceMpegInit` ejecutándose
ANTES de `CALL sceMpegGetPicture`, lo cual es topológicamente
incompatible con esas 3 rutas (todas posteriores en direcciones de
memoria a la llamada de GetPicture dentro del mismo tick) — y en
efecto, ninguna de las 2 rutas con log dedicado (`SKIP via
sceMpegIsEnd!=0`, `SKIP via Fade_status&4`) se disparó en la corrida.
La búsqueda del segundo call site (22.5 en el propio archivo) encontró
la explicación correcta: `MpegMovieDecodeInit`, llamada como
subrutina al principio de `MpegMovieDecode` (0x47a784), es una vía
completamente distinta e independiente de label_47aa70. El mapeo de
label_47aa70 sigue siendo correcto para sus propias 3 rutas (relevante
para transiciones ENTRE películas en ticks posteriores), simplemente
no es el mecanismo que actúa en el primer tick.

### 22.9 Reconciliación de los logs históricos ("frame decoded total=4" vs "decodedFrames=0")

Clasificación requerida por el prompt: **SAME STATE BUT RESET**.
Mismo `mpegAddr` (`0x87d8f8`), mismo `MpegPlaybackState` (nunca hubo
una segunda creación ni un cambio de identidad), pero reseteado por
completo (`playbackByMpeg.clear()` vía `sceMpegInit`) entre el momento
en que se decodificó el 4º frame y el momento en que `GetPicture` lo
consulta por primera vez. No es CASO B (ownership — descartado, mismo
address en todo momento) ni CASO D puro (no es que el video no haya
llegado — llegó, se decodificó, y se descartó por diseño). Es **CASO
C** tal como lo define el prompt: "el mismo mpegAddr recibió input
antes, pero su `MpegPlaybackState` fue reseteado/vaciado/recreado antes
de `GetPicture`" — con el mecanismo exacto identificado (22.8), no como
hipótesis sino como hecho demostrado en ambos planos (estático y
dinámico).

### 22.10 Timeline T0-T11 (corrida real, ELF+ISO, FFmpeg ON, override DMC)

| Paso | Evento | Estado |
|---|---|---|
| T0 | `sceMpegCreate` mpeg=0x87d8f8 | HECHO — línea única, `hadExisting=0` |
| T1 | Primer feed call (#1) | HECHO |
| T2 | Primer video PES / primer frame decodificado (feed #9) | HECHO — `total=1` |
| T3 | Frames adicionales decodificados (feed #23, #36, #49) | HECHO — `total=2,3,4` |
| T4 | `sawInput` pasa a `true` | HECHO — implícito en cada feed, confirmado `sawInput=1` en 22.7 |
| T5 | `MpegMovieDecode` ENTRY #1 (primer tick) | HECHO |
| T6 | `MpegMovieDecodeInit` invocada como subrutina | HECHO — estático (22.8) |
| T7 | `sceMpegInit` ejecuta `playbackByMpeg.clear()` (4 frames, sawInput=1 perdidos) | HECHO — dinámico (22.7) |
| T8 | `getPlaybackState(0x87d8f8)` auto-crea entrada vacía | HECHO — inferido, consistente con GP:A |
| T9 | `sceMpegIsEnd` (primer chequeo post-init) | NO INSTRUMENTADO ESTA ITERACIÓN — se asume "no terminado" dado que no se disparó `SKIP via sceMpegIsEnd!=0` |
| T10 | `MpegMovieDecode` llama `sceMpegGetPicture` | HECHO |
| T11 | `GetPicture` ve `decodedFrames=0, sawInput=0`, entra en `waitExternal` (GP:H) | HECHO |

Ningún paso quedó como NEVER en esta corrida — la cadena completa hasta
GP:H se demostró. Lo que ocurre DESPUÉS de T11 (si el demux vuelve a
alimentar la entrada fresca y si `completeExternalWait` llega a
disparar la reanudación) es exactamente el "stall" ya caracterizado en
las secciones 19-20 (P3.8/P3.8.1) — explícitamente fuera de alcance de
esta iteración (no se reabre el scheduler).

### 22.11 Clasificación final (esquema A-E del prompt)

**CASO C**, demostrado dinámica y estáticamente, sin hipótesis
pendientes. Mecanismo: `MpegMovieDecode` → (subrutina, incondicional)
`MpegMovieDecodeInit` → (incondicional) `sceMpegInit` →
`playbackByMpeg.clear()`, ejecutado en el primer tick de la tarea de
decodificación, siempre, por diseño del propio código del juego (bytes
MIPS reales del ELF). Los 4 frames "perdidos" nunca estuvieron
destinados a ser consumidos por `GetPicture` — el reset es una
inicialización legítima y esperada del subsistema MPEG antes de que
comience el bucle real de reproducción, coherente con cómo
`MpegMovieDecodeInit` (nombre real, symtab) prepara el estado antes de
que `MpegMovieDecode` empiece a tickear.

### 22.12 Respuesta a la pregunta "¿tiene RECOMP datos suficientes para un decode síncrono como el de `_getpic` ORIGINAL en este punto?"

**NO, en este instante preciso (T11)** — inmediatamente después del
reset, `decodedFrames` está vacío por diseño (ver 22.11), igual que
probablemente lo estaría el estado interno del `_getpic` ORIGINAL justo
tras su propia inicialización equivalente (no demostrado para ORIGINAL
en esta iteración — UNKNOWN, ver 22.13). La pregunta relevante para
cerrar BLOCKER_004 no es ya "por qué está vacío en T11" (respondida:
por diseño), sino **si el pipeline de demux sigue alimentando la nueva
`MpegPlaybackState` después de T11 con la velocidad/cadencia
suficiente para que un GetPicture síncrono (al estilo ORIGINAL)
encuentre datos en llamadas subsiguientes** — pregunta que conecta
directamente con el "stall" de las secciones 19-20 y queda
explícitamente fuera del alcance de P3.10.

### 22.13 UNKNOWNs de esta iteración

- No se ha demostrado si el `_getpic`/estado interno ORIGINAL sufre un
  reset equivalente en el mismo punto (no se ejecutó ninguna sesión
  PCSX2 nueva en P3.10, según lo previsto — no era estrictamente
  necesaria dado que la pregunta central quedó resuelta con evidencia
  RECOMP-side).
- No se ha instrumentado el primer `sceMpegIsEnd` posterior al reset
  (T9 en 22.10) — se infiere "no terminado" únicamente por ausencia del
  log `SKIP via sceMpegIsEnd!=0`, no por lectura directa de su valor de
  retorno.
- No se ha determinado si el demux vuelve a alimentar `mpeg=0x87d8f8`
  después de T11 en esta misma corrida (el proceso se terminó tras
  capturar la secuencia decisiva, por disciplina de alcance — ver
  "NO HACER" de P3.10). Sigue siendo la pregunta abierta relevante para
  un futuro P3.11 centrado en el "stall" post-GP:H.
- Los otros dos mecanismos de reset localizados por lectura estática
  (22.3 `sceMpegCreate`, 22.4 resync por secuencia) son reales y
  permanecen activos en el código para otros escenarios (por ejemplo,
  transición entre DOS películas ya en curso, o una recuperación de
  fallo de decodificación) — simplemente no son los que actúan en esta
  corrida concreta. No se ha buscado una corrida donde SÍ se disparen.

### 22.14 Build/instrumentación (resumen)

Un archivo tocado: `vendor/PS2Recomp/ps2xRuntime/src/lib/Kernel/Stubs/MPEG.cpp`
(instrumentación aditiva únicamente, ver 22.5 — no se cambió ningún
valor de retorno, ninguna condición de branch, ningún timing). Build
con el método confirmado seguro (MSBuild directo sobre
`ps2_runtime.vcxproj` para compilar, `ps2EntryRunner.vcxproj` con
`/t:_BuildLinkAction` para relink), verificado en cada paso que 0
archivos generados se recompilaron. Un fallo de link (`LNK1104`, archivo
`.exe` bloqueado por un proceso previo sin terminar) se resolvió
matando el proceso y reintentando el mismo target — no relacionado con
el riesgo de mass-compile ya conocido. Tres ejecuciones reales: la
primera inválida por falta de `PS2X_CD_IMAGE` (22.6, descartada sin
usar sus datos), la segunda y tercera válidas con ISO real
proporcionada por el usuario. No se hizo commit ni push. La
instrumentación queda en el árbol (marcada "temporary, not for
commit" en cada bloque, siguiendo el patrón ya establecido para las
etiquetas `GP:*`/`[MPEG:diag]` de prompts anteriores).

**Resultado Prompt P3.10 — GETPICTURE_SYNC_DECODE_INPUT_GAP**

## Qué NO se ha hecho

- (P3.13) No se implementó ningún fix de producción de `sceMpegInit` —
  los 4 modos son sondas experimentales temporales opt-in, marcadas
  explícitamente como tales en el código.
- (P3.13) No se investigó ni corrigió el agotamiento de
  `RuntimeGuestArena` observado 3/3 en modo `full` (hallazgo lateral,
  fuera de alcance de la pregunta causal de este prompt).
- (P3.13) No se determinó si `queued`/`full` producen salida visual de
  película correcta — las capturas siguen sin contenido reconocible,
  consistente con la limitación de renderizado GS ya conocida y fuera de
  alcance.
- (P3.13) No se disparó dinámicamente el riesgo de contradicción de
  generación (`cdStreamGeneration`) identificado por Fable — confirmado
  como mecanismo latente real (el único incrementador,
  `notifyMpegCdStreamStart`, sigue siendo código muerto), pero no se
  forzó un escenario donde se manifieste.
- (P3.13) No se tocó `sceMpegGetPicture`, `waitExternal`, `waitVSync`, el
  scheduler ni el orden del demux — confirmado explícitamente.
- (P3.12) No se implementó ningún fix de `sceMpegInit` — instrucción
  explícita ("aislar UNA variable"). El over-reset de
  callbacks/config/ownership sigue exactamente como lo dejó P3.11.1.
- (P3.12) No se ejecutó PCSX2 — innecesario, la hipótesis del scheduler
  era falsable íntegramente dentro de RECOMP y así se resolvió.
- (P3.12) No se investigó si demux/scheduler corregidos, combinados con
  un futuro fix de ownership, producen presentación GS visible —
  title/gameplay siguen sin evidencia de viabilidad.
- (P3.12) No se comprobó dinámicamente la inversión inferida (no
  dumpeada) del audio IOP que P3.11.1 predijo por el mismo mecanismo —
  el fix genérico la corrige por construcción, pero no se verificó con
  un dump de esa región.
- (P3.10) No se ha demostrado si el `_getpic`/estado interno ORIGINAL
  sufre un reset equivalente en el mismo punto — no se ejecutó ninguna
  sesión PCSX2 nueva en esta iteración (sección 22.13).
- (P3.10) No se ha instrumentado el primer `sceMpegIsEnd` posterior al
  reset de `sceMpegInit` — se infiere su resultado por ausencia de log,
  no por lectura directa (sección 22.13).
- (P3.10) No se ha determinado si el demux vuelve a alimentar
  `mpeg=0x87d8f8` después de `GP:H` en la misma corrida — pregunta
  abierta para un futuro P3.11 centrado en el "stall" post-reset
  (sección 22.12-22.13). No se reabrió el scheduler ni el "stall" de
  las secciones 19-20, por alcance explícito del prompt.
- (P3.10) No se ha buscado una corrida donde se disparen los otros dos
  mecanismos de reset localizados (`sceMpegCreate` sobre estado
  existente, resync por secuencia) — permanecen activos en el código
  pero no se demostró su disparo real (sección 22.13).
- (P3.10) No se implementó ningún fix, no se tocó `waitExternal`,
  `Movie_ctrl`, el scheduler, `RuntimeGuestArena`, CDMODULE,
  backpressure ni `kMaxDecodedPicturesAhead`. No hubo commit ni push.
- (P3.9) No se ha desensamblado `_decodeOrSkip` (0x10A680) ni
  `_nextHeader` (0x106FF0) en detalle — quién escribe realmente los
  píxeles y si hay interacción IPU sigue UNKNOWN (sección 21.15).
- (P3.9) No se ha ejecutado ninguna sesión PCSX2 nueva — plan de
  breakpoints preparado en 21.16, no ejecutado.
- (P3.8) No se ha ejecutado ninguna sesión PCSX2/ORIGINAL — requiere
  intervención manual del usuario superando Memory Card→idioma→X→CAPCOM;
  plan de breakpoints preparado en la sección 19.9, no ejecutado.
- (P3.8) No se ha investigado por qué `Movie_ctrl` deja de recibir CPU
  tras `Task_execute(3, MpegMovieDecode)` — es la primera divergencia
  real localizada (sección 19.7/19.12), dejada explícitamente para una
  futura iteración P3.9 por instrucción de detenerse ahí.
- No se ha tocado `kMaxDecodedPicturesAhead` ni el mecanismo de
  backpressure.
- No se ha determinado todavía quién debería llamar a
  `sceMpegGetPicture` ni por qué no ocurre (siguiente paso).
- No se ha leído `appendGuestRingBytes`/`appendGuestBytes` línea a línea
  (demux input queda en FUERTEMENTE SOPORTADO, no HECHO).
- No se ha investigado la causa del logo CAPCOM.
- No se ha abierto ninguna investigación de GS/presentación — prematuro
  mientras el pipeline se bloquee antes de llegar ahí.
- La instrumentación temporal en `MPEG.cpp` (contadores de diagnóstico)
  sigue en el árbol de trabajo, sin commit — pendiente de revertir al
  cerrar este blocker.
- No se ha determinado quién llama (o debería llamar) a
  `audioDecResume`/`audioDecPause` (sección 6.4) ni por qué no llega a
  invocarse antes/durante el arranque de una película — esto es ahora
  el único eslabón que falta para cerrar la cadena causal completa (ver
  sección 7, "Siguiente paso natural").
- No se ha instrumentado `audioDecSendToIOP` en sí (solo se leyó su
  desensamblado estático) — no hay confirmación dinámica de en qué modo
  (`*(state+44)`) se encuentra durante la corrida real.
- No se ha leído el cuerpo de `Task_execute` (`0x19F820`) ni identificado
  el llamador externo de `Movie_ctrl` (el "tick" que la invoca) — ya no
  es necesario para cerrar CASO A/B/C/D (CASO A quedó confirmado como
  causa primaria), pero seguiría siendo relevante si se quisiera
  entender el motor de tareas en general.
- No se ha resuelto el símbolo de la función llamada al ENTRY de
  `MpegMovieDecode` (`0x47A6E0`, antes de `sceMpegIsEnd()`) — irrelevante
  para BLOCKER_004 ya que esa función nunca llega a ejecutarse.
- ~~No se ha hecho ninguna sesión de PCSX2 todavía~~ — **superado**:
  la serie P3-P3.4 (sección 12) se apoya en múltiples sesiones PCSX2
  reales, operadas manualmente por el usuario con breakpoints/capturas
  precisas.
- La instrumentación temporal (`MPEG.cpp`, `Movie_ctrl_0x1ce510.cpp`,
  `MpegMovieDecode_0x47a760.cpp`, `audioDecIsPreset_0x47a120.cpp`,
  `audioDecPause_0x479e80.cpp`, `audioDecResume_0x479f00.cpp`,
  `Stubs/Audio.cpp`, y ahora además
  `StrPcmCallBack_0x47a600.cpp`/`audioDecEndPut_0x47a090.cpp` en
  `analysis/local/symtabfirst/generated/`, sección 12.5-12.6) sigue en
  el árbol de trabajo, sin commit.
- No se ha implementado ninguna estrategia de fix para el colapso del
  guest heap — sección 13 (Prompt P3.5) entrega un diseño completo y
  comparado (opciones A-E), pero explícitamente sin tocar código de
  infraestructura.
- No se ha resuelto dónde se escribe en runtime el valor de
  `*0x7411B4` (techo del heap propio de `Mem_alloc_R`, sección 13.4) —
  7 sitios candidatos de `sw ...,0x1B4(base)` encontrados, ninguno con
  el patrón simple de materialización de dirección esperado; requiere
  seguimiento de flujo más profundo o un watchpoint dinámico en PCSX2.
  Este es el ABIERTO más importante para poder confiar en cualquier
  fix futuro sin riesgo de colisión con el heap propio del juego.
- No se ha verificado dinámicamente (PCSX2 ni RECOMP instrumentado) si
  el rango `[0x9F9480, 0x1F00000)` está realmente libre durante una
  partida real, más allá de la inferencia estática de 13.4.
- (P3.6) No se ha medido dinámicamente el low/high-water mark real de
  `Mem_alloc_R` (sección 14.8) — es la medición concreta pendiente más
  importante antes de poder proponer ningún rango SAFE para el guest
  heap sintético (14.12: "NO SAFE RANGE PROVEN").
- (P3.6) No se ha desensamblado el cuerpo completo de `Mem_alloc`
  (sin `_R`) ni determinado su relación exacta con `Mem_alloc_R`
  (14.1, PENDIENTE).
- (P3.6) No se ha investigado quién llama a `sbrk` (`0x11727C`) ni
  qué código del juego usa realmente `malloc()`/el heap C de newlib
  (14.6, PENDIENTE).
- (P3.6) No se ha medido dinámicamente si `reserveAsyncCallbackStack`
  (sección 14.7, colisión demostrada por diseño con la ventana de
  overlay/heap-C/stack nativo) ya está causando corrupción silenciosa
  hoy en alguna ruta de callbacks que sí se ejecuta actualmente (p. ej.
  vsync/GS) — riesgo real, no solo teórico, sin confirmar.
- (P3.6) No se ha inspeccionado el contenido real de los overlays en
  la ISO (solo se confirmó la ventana reservada en el ELF base,
  sección 14.4) — el tamaño máximo real de un overlay cargado sigue
  UNKNOWN.
- (P3.7.1) No se ha investigado la nueva primera divergencia (sección
  16.12, estancamiento tras `sceMpegGetPicture` ENTRY) — detenido por
  instrucción explícita, candidato para P3.8.
- (P3.7.1) No se han migrado los pools RPC/servidor/TLS verificados en
  `[0x1F00000, 0x1F30000)` (sección 16.7) — verificados pero diferidos
  por no estar en la ruta causal directa del bug de esta iteración.
- (P3.7.1) No se ha completado la validación de memory safety más allá
  del punto de estancamiento (attract completo, retorno al título,
  gameplay) — sección 16.13, validación manual pendiente.
- No se ha resuelto por qué mi trazado estático del gate de
  `audioDecReset` (bit `0x4000`) no coincide con la evidencia dinámica
  (cero llamadas observadas) — sección 8.4 deja esto explícitamente
  abierto; ya no es el mecanismo principal (superado por la sección 9).
- No se ha resuelto `func_47AE90` (usada por `videoDecFlush`) ni
  `func_47B920` (segunda vía de `videoDecIsFlushed`) — irrelevantes
  mientras el bucle de la sección 9.3 ni siquiera llegue a llamarlas.
- No se ha comprobado si existe una inicialización masiva
  (`memset`/plantilla) del estado de movie que ponga el bit `0x10` por
  fuera del único sitio de lectura-modificación-escritura encontrado
  (sección 9.4, dejado explícitamente ABIERTO).
- No se ha investigado quién escribe `*(s0+0x18)` (el contador comparado
  con `<5` en `label_1ce73c`, atajo que también evita `videoDecFlush`).
- No se ha identificado el mecanismo real (hardware o de diseño) que en
  el original debería poner el bit `0x10` — la sección 9.4 demuestra el
  ciclo cerrado en el binario recompilado, pero no explica todavía cómo
  se rompe ese ciclo en PS2 real.

## P3.11 — Astra overnight investigation

Informe independiente: [BLOCKER_004_ASTRA_P311_OVERNIGHT.md](BLOCKER_004_ASTRA_P311_OVERNIGHT.md).
ELF SLES_503.58, entry 0x00100008, CRC32 77654AD2, SHA256
d0753a6b3b2f00802a50758a872d8cf051725aa31c839aa8894eee30ce58bab4.

**HECHO:** sceMpegInit ORIGINAL ocupa 0x109CD0–0x109D6C y prepara
DMA/IPU mediante sceIpuInit; no destruye objetos/callbacks software guest.
La HLE sí destruye playbackByMpeg y callbacksByMpeg. RUN_008–010 confirman
cuatro frames antes del clear, objeto nuevo después y waitExternal sin
feed posterior en 20 s. **OVER-RESET de ownership software** demostrado;
no se implementó fix. No equivale a afirmar que ORIGINAL no resetea hardware.

**Correcciones explícitas:** «mismo objeto tras clear» y «los cuatro frames
debían descartarse por diseño original» quedan retractados. La cadena
_getpic→_nextHeader→_nextBit sí accede a IPU: tampoco se sostiene describirla
como decoder enteramente software. Se conserva arriba el razonamiento histórico.

**HECHO:** ring PSS y viBuf ES guest sobreviven byte-idénticos al init;
quedan 64 KiB PSS lógicamente disponibles. El ES guest ya difiere del ES
extraído de los PES antes del init; causa y comparación PCSX2 equivalente
**UNKNOWN**. Por eso over-reset es la primera divergencia contractual del
segmento init→GetPicture auditado, no una prueba de primera divergencia
absoluta desde boot.

Loop local reproducido **3/3 PSS_REACHED**, Start→Cross mediante la API
de override de pad existente, sin drivers. Script/instrucciones en
`analysis/tools/runtime_loop/`. Intento posterior de Start no alcanzó título.
Sin gameplay, cambios semánticos MPEG, commits, push ni builds masivos.
