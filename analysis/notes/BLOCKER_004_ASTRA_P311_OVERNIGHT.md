# P3.11 — Astra: sceMpegInit contract and autonomous runtime loop

Investigación iniciada 2026-09-11. Informe independiente; no sustituye la
historia P3.8–P3.10. No se autorizaron commits ni fixes de MPEG/scheduler.

## Checkpoint de entrada (sin commit por instrucción expresa)

- HEAD principal: `888f22ef7a3dc685815e9995386100a91fd94c32`.
- Vendor HEAD: `0f76399b1aa35b018a89477c078924775d35b79c`, coincide con
  `patched_commit` del lock dirty; baseline upstream `14b1e5cb39b4af7e6fc12f9a29fdc751efde49d7`.
- Dirty previo principal: nota canónica (4054 líneas añadidas/modificadas),
  `upstream.lock.json`; untracked `BLOCKER_004_ASTRA_AUDIT.md`,
  `BLOCKER_004_P36_HEAP_COLLISION_AUDIT.md`, `BUILD_ffmpeg_private_link_scope.patch`.
- Dirty previo vendor: `ps2_runtime.h`, `EeScheduler.cpp`, `Audio.cpp`,
  `MPEG.cpp`, `System.cpp`, `ps2_runtime.cpp` (788 inserciones, 143 eliminaciones).
  No se limpia ni revierte. Snapshot local: `analysis/local/p311/vendor-before-p311.patch`.
- Leídos README, UPSTREAM, AGENTS, lock, secciones históricas pertinentes.
  El diff inicial completo excedió la salida de herramienta; se revisaron
  después los fragmentos pertinentes. No afirmar revisión íntegra de cada línea.
- Ejecutable inicial: `analysis/local/symtabfirst/build/bin/Debug/dmc-recomp.exe`,
  347622400 bytes, LastWriteTime local 2026-09-10 23:39:32,
  SHA256 `962050d8d6497b766374c17c31faf650ab88ac4755ad1670bc27afcf6c1b2b9b`.
- `CMakeCache.txt`: `PS2X_ENABLE_FFMPEG:BOOL=ON`.
- ELF verificado por lectura binaria propia, no por nombre solamente:
  `original/SLES_503.58`, entry `0x00100008`, CRC32 `77654AD2`, SHA256
  `d0753a6b3b2f00802a50758a872d8cf051725aa31c839aa8894eee30ce58bab4`.

## Hallazgo estático: contrato ORIGINAL

**HECHO:** `.symtab` delimita `sceMpegInit` en `[0x109CD0,0x109D6C)`,
156 bytes. El generado es solo un wrapper HLE y NO conserva ese cuerpo.
`analysis/tools/runtime_loop/elf_contract.py` extrae los bytes, palabras,
símbolos y llamadas directamente del ELF, sin paquetes instalados.
Salida completa local: `analysis/local/p311/elf_contract.json`.

Pseudocódigo semántico del cuerpo completo, incluyendo delay slots:

```c
sceMpegInit() {
    save_ra_on_stack();
    DIntr();
    MMIO32(0x1000F590) = MMIO32(0x1000F520) | 0x00010000;
    MMIO32(0x1000B000) &= 0xFFFFFEFF;
    MMIO32(0x1000B400) &= 0xFFFFFEFF;
    MMIO32(0x1000F590) = MMIO32(0x1000F520) & 0xFFFEFFFF;
    EIntr();
    MMIO32(0x1000B020) = 0;
    MMIO32(0x1000B420) = 0;
    restore_ra_and_stack();
    tail_call sceIpuInit();
}
```

**HECHO:** no argumento MPEG se usa en este cuerpo, no itera sobre objetos,
no escribe callbacks ni estructuras de reproducción en RAM guest. Sus
stores son stack y MMIO. No hay retorno explícito `v0=0`: hereda el valor
dejado por `sceIpuInit`, cuyo epílogo no fija una constante de éxito.

**HECHO:** `sceIpuInit` es `[0x10B868,0x10BAA0)`, 568 bytes, y llama a
`setD4_CHCR` **en 0x10B800** (existe otro símbolo homónimo en 0x10B4F8;
no intercambiarlos). Este callee solo manipula interrupt enable/DMA y stack.
`DIntr`/`EIntr` manipulan COP0; no destruyen objetos MPEG en RAM.

`sceIpuInit`: `setD4_CHCR(1)`; escribe `0x40000000` en `0x10002010` y
espera que desaparezca BUSY; escribe comando 0 en `0x10002000`; alimenta
FIFO `0x10007010` con tablas desde `0x004FC860` y `0x004FC8B0`; emite
comandos `0x50000000`, `0x58000000`, `0x60000000`, `0x90000000`, con
esperas BUSY; vuelve a escribir reset/control `0x40000000` y comando 0.
**HECHO:** contiene inicialización/reset real del hardware IPU, no es un no-op.
**INFERENCIA:** contrato de preparación de hardware compartido y sus tablas,
conservando objetos/callbacks/buffers software guest. No equivale a destruir
toda la sesión lógica MPEG.

## Call sites y frecuencia

Escaneo de instrucciones `j`/`jal` en secciones ejecutables del ELF:

| PC | Caller | Uso |
|---|---|---|
| `0x47A6EC` | `MpegMovieDecodeInit` | Incondicional al inicio |
| `0x47AA70` | `MpegMovieDecode` | Ruta de cierre/salida |

Estos son todos los callers **directos** hallados. Una enumeración de j/jal
no demuestra por sí sola inexistencia de referencias indirectas.
`MpegMovieDecodeInit` además llama `sceIpuInit` en `0x47A6F4`, borra
`state+0x74/+0x9C`, construye tags mediante `SettoIpuTag`, configura
`D4_MADR/TADR`, y llama `SetD4chcr(5)` en `0x47A744`.
No confundir estas escrituras del caller con efectos de `sceMpegInit`.
Tampoco inferir frecuencia por frame de una función que contiene un bucle:
la llamada incondicional es por entrada a `MpegMovieDecode`.

## Correcciones explícitas a P3.9/P3.10

**RETRACTADO:** «mismo MpegPlaybackState después del clear». La evidencia
anterior apoyaba identidad de la **clave** `0x87D8F8`; no identidad del
objeto. `clear()` destruye el viejo objeto y `operator[]` crea otro.

**RETRACTADO:** «el juego dispone por diseño descartar esos cuatro frames».
La evidencia previa era la llamada guest a `sceMpegInit`, confundida con
la semántica del handler HLE. El cuerpo original leído ahora conserva RAM
software y resetea hardware. No valida el borrado de la cola host que
representa trabajo realizado anticipadamente sobre bytes ya consumidos.

**RETRACTADO:** «decoder enteramente software / sin interacción IPU» como
descripción de la cadena completa. Aunque `_getpic` no tenga MMIO directo,
`_getpic@0x10A448 -> _nextHeader@0x106FF0 -> _nextBit@0x106DB0`
alcanza MMIO IPU: `_nextBit` lee `0x10002010` en `0x106DE4` y emite
comandos FDEC `0x40000000`/`0x40000000|nbits` a `0x10002000`
en `0x106E88/0x106EC4`. Código generado contiene las palabras originales.
El comportamiento es síncrono desde la perspectiva del caller; eso NO
significa que no espere hardware ni que toda la decodificación sea CPU.

**HECHO nuevo de precisión:** la primera recreación post-init puede ocurrir
ya en `sceMpegIsEnd`, antes de GetPicture: ese handler también llama
`getPlaybackState`. La afirmación de que necesariamente la crea GetPicture
es incorrecta para la ruta observada, que primero consulta IsEnd.

## Contraste de estados

| Estado | ORIGINAL sceMpegInit y callees | HLE actual | Clasificación |
|---|---|---|---|
| Objeto MPEG software | No destrucción/escritura | Borra mapa completo | DIFFERENT |
| Decoder hardware | Reset DMA/IPU + tablas | No ejecución del original; destruye decoder FFmpeg | PARTIAL MATCH conceptual; equivalencia no probada |
| Compressed bytes software | Sin writes fuera de stack/MMIO | Destruye pssBuffer y buffers auxiliares | DIFFERENT |
| FIFO/bit pointer IPU | Reset hardware | Sin correspondencia individual demostrada | UNKNOWN |
| Frame queue host | No estructura equivalente directa | Destruye decodedFrames | DIFFERENT como ownership; equivalencia temporal UNKNOWN |
| Callbacks | No writes/destrucción | callbacksByMpeg.clear | DIFFERENT |
| Globales libmpeg RAM | No writes | initialized=false→true; resetea trace counters/handles | DIFFERENT |
| Stream generation global | No transporte CD tocado | Guarda/restaura | Conservación MATCH conceptual |
| EOF global CD | No transporte CD tocado | Guarda/restaura ambos flags | Conservación MATCH conceptual |
| Contadores transporte CD | No transporte CD tocado | Guarda/restaura produced/demuxed | Conservación MATCH conceptual |
| EOF/generation por playback | Software intacto | Default objeto nuevo (generation=0 en operator[]) | DIFFERENT |
| width/height/decodeMode/imageBuffer | Software intacto | Se pierden; defaults 320/240/0/0 | DIFFERENT |

**Clasificación:** OVER-RESET de ownership software, con reset hardware
original real. No convertirlo en «no reset original» ni «basta quitar clear».

## Buffers y feed: lo demostrado estáticamente

`StrM2vCallBack@0x47AFE0` obtiene el PES, llama `viBufBeginPut`, copia
mediante helper `0x3FE450` y llama `viBufEndPut@0x47AF80`; este incrementa
`state+0x2C8` y `state+0x28` bajo semáforo. Es una vía de almacenamiento
comprimido guest distinta de `decodedFrames`.

El HLE demux recibe `mpeg=state+0x278` (`Movie_ctrl@0x1CE69C`), bytes
disponibles y base/tamaño de ring. El JAL real es **0x1CE6A0**;
`0x1CE6A8` es continuación, corrección de algunas notas históricas.
Después contabiliza bytes consumidos y alcanza el gate de `Task_execute`.
Si la tarea de decode retornase/cediese según el flujo guest, existe una
ruta estructural de continuación del productor; no es una prueba de que un
retorno ficticio de GetPicture sea correcto ni produzca un frame válido.

Separar siempre: cola decodedFrames; parser/decoder FFmpeg; pssBuffer host;
ring PSS guest; viBuf ES guest; transporte CD pendiente. El clear no borra
RAM guest ni rebobina los cursores del productor. Bytes residuales en RAM
no equivalen a bytes lógicamente disponibles/reproducibles.

## Input: descubrimiento inicial

Dos mappings locales: `PSPadBackend` usa gamepad 0 si está disponible y
solo en el else teclado; fallback de `Pad.cpp` tiene otro mapping.
Ambos incluyen Cross=X y Start=Enter. En backend: D-pad flechas/WASD,
Cross X/Space, Circle C/Escape, Square Z/keypad0, Triangle V/keypad1.
El test usa la API existente `setPadOverrideState` (más prioritaria que
backend), no requiere mando virtual ni driver.

RUN_001 (45 s) y RUN_002 (25 s): sin input, BOOT_ONLY. RUN_002/boot.png
muestra aviso de memory card y pide START para comenzar con defaults;
no se considera «menú de idioma» ni «PSS reached».
RUN_003: Enter 250 ms por PostMessage, INPUT_FAILED; captura posterior
conserva aviso. Que PostMessage devuelva true NO demuestra input guest.
RUN_004: SendInput preparado, abortado ANTES de enviar por no poder
verificar foreground; HARNESS_ERROR, BOOT_ONLY de alcance.

## Incidentes y límites registrados

- PowerShell inició en C:\\ pese a workdir; Set-Location dentro de sandbox
  dio AccessDenied. Lecturas absolutas y ejecuciones autorizadas fuera del
  sandbox permitieron continuar. No se alteró la configuración del sistema.
- `elftools` y `rabbitizer` no están instalados en Python disponible. No
  se instalaron: extractor ELF32 con struct/hashlib/zlib estándar.
- Captura manual de RUN_001 llegó después del timeout y falló por HWND
  inexistente. El harness captura ahora dentro de la vida del proceso.
- Primera compilación de dmc_overrides.cpp falló por declaración ausente
  del override de pad; se añadió Stubs/Pad.h solo a esa unidad.
- Relink informa LNK4075: /INCREMENTAL ignorado por /FORCE del proyecto.
  Es relink del target, NO clean/full build ni recompilación generated.

## Verificación del build corregido

`build_mpeg_corrected.log`: CL compila **solo MPEG.cpp** y MSBuild ejecuta
`_BuildLinkAction`; exit code 0. La biblioteca activa contiene exactamente
un miembro MPEG.obj (ruta absoluta del objeto nuevo). No mass compile de
generated; no clean, regenerate ni full build. Exe resultante: 347604480
bytes, LastWriteTime 2026-09-11 00:43:49, SHA256
`79c7b25e0392945bcdb1270b997cff83931b193ed38d9a74d234c877aa335d03`.
Su validez operativa se confirmó con RUN_008–010, no solo por existir.

Incidente de archive: el primer intento de insertar el objeto por ruta
absoluta añadió un segundo miembro frente al antiguo
`ps2_runtime.dir\Debug\MPEG.obj`; LIB avisó LNK4006. No se usó ese binario
para evidencia instrumentada. Se corrigió retirando el miembro exacto del
backup, añadiendo el objeto nuevo y verificando cardinalidad=1 antes del
relink. Backup local conservado; no se borraron fuentes ni otros objetos.

Builds realizados: intento CL dmc_overrides fallido por include; CL de esa
misma unidad + relink correcto; CL MPEG + archive ambiguo y relink (no usado
para runs); CL MPEG + archive corregido + relink correcto. Ninguno compiló
C++ generado. No se editó ninguna cabecera compartida con generated.

## Corridas y repetibilidad

Logs, metadatos y capturas: `analysis/local/p311/RUN_NNN/`.
`result.json` conserva PID, HWND, título, variables, hash, inputs, hitos,
resultado, tiempo y salida; stderr/stdout se capturan en el mismo archivo.

| Run | PID propio | Input | Alcance | Resultado |
|---|---:|---|---|---|
| 001 | 58264 | ninguno | BOOT_ONLY | timeout 45 s |
| 002 | 64144 | ninguno | BOOT_ONLY | timeout 25 s; captura aviso MC |
| 003 | 52684 | Enter/PostMessage | BOOT_ONLY | INPUT_FAILED; 25 s |
| 004 | 60896 | ninguno enviado | BOOT_ONLY | HARNESS_ERROR: foreground no verificado |
| 005 | 63244 | Start/runtime | BOOT_ONLY, idioma visible | llegó a idiomas; timeout 25 s |
| 006 | 56580 | Start→Cross/runtime | transición tras idioma | timeout 60 s insuficiente |
| 007 | 39460 | Start→Cross/runtime | PSS_REACHED | SUCCESS, target a 69.875 s |
| 008 | 60448 | Start→Cross/runtime | PSS_REACHED | SUCCESS, target 68.891 s + 20 s observación |
| 009 | 35532 | Start→Cross/runtime | PSS_REACHED | SUCCESS, target 68.875 s + 20 s observación |
| 010 | 65272 | Start→Cross/runtime | PSS_REACHED | SUCCESS, target 70.125 s + 20 s observación |
| 011 | 11688 | Start→Cross, luego Start en stall | PSS_REACHED; TITLE no observado | target 70.188 s + 60 s; skip sin avance |

**HECHO:** 008–010 son **3/3 consecutivas** con el mismo exe instrumentado.
El algoritmo temprano rotuló 005/006 INPUT_FAILED al no llegar a PSS; las
capturas/logs demuestran que sí hubo avance. No reescribir esos JSON:
interpretación corregida aquí. El harness actual distingue PSS_NOT_REACHED
y ofrece límites BOOT/MENU/PSS/TARGET además del límite total.

Ninguna tuvo salida espontánea antes de stop. `exit_before_stop=null` y
`stopped_own_pid` identifica el proceso cerrado. El exit code 1 de la
terminación deliberada no significa CRASH.

## R0–R8: evidencia dinámica nueva

Se añadieron trazas acotadas P311 a MPEG.cpp y dos dumps RAM por corrida,
sin mutar guest ni alterar ninguna condición/retorno/suspensión MPEG.
Variables opt-in `DMC_P311_MPEG_DIAG`, `DMC_P311_RAM_PREINIT`,
`DMC_P311_RAM_GETPICTURE`. Retirar las adiciones P311 después de preservar
los resultados. Las trazas anteriores P3.x se mantienen intactas.

RUN_008, repetido en 009 y 010:

```text
R0 init ra=0x0047A6F4 callbackKeys=1 generation=0
   produced=0 demuxed=262144 eofPending=0 eofSeen=0
   key=0x0087D8F8 decoder=1 frames=4 sawInput=1
   pssBytes=0 syncBytes=0 width=320 height=240 generation=0
R1 init complete playbackKeys=0 callbackKeys=0
R2 new playback key=0x0087D8F8
R7 first GetPicture key=0x0087D8F8
GP:A decodedFrames=0 streamEnded=0 decoderFailed=0
     cdStreamEofSeen=0 sawInput=0 picturesServed=0
GP:H waitExternal ... SUSPENDING
```

R2 aparece antes de R7: concuerda con la recreación en IsEnd.
Es **objeto anterior destruido / objeto nuevo / misma clave**.

| Evento después del reset | Resultado |
|---|---|
| R3 siguiente VIDEO PES/ES | No observado en la ventana post-target de 20 s |
| R4 nuevo sawInput=1 | No observado |
| R5 primer input FFmpeg nuevo | No observado |
| R6 nuevo decoded frame | No observado |
| R7 primer GetPicture | Observado; luego no segunda entrada |
| R8 frame consumido | No observado |

Las etiquetas combinadas R3-R4/R5-R6 registran entrada ES y llamada a
`decoder->feed`, no cada `avcodec_send_packet`. No afirmar un packet
FFmpeg individual a partir de ellas. El contador feedES se resetea en init,
por lo que un primer feed posterior habría quedado dentro del límite de
cuatro trazas; la ausencia no se debe a haber agotado ese límite antes.
Los ticks VSync siguen apareciendo después de GP:H; eso no demuestra
ejecución de Movie_ctrl. No se reabrió el scheduler como causa abstracta.

**Conclusión:** el nuevo state NO produce frame en la ejecución sin fixes.
Si produciría uno al forzar una continuación es **UNKNOWN**: liberar la
espera o fingir un retorno alteraría semántica y no estaba autorizado.
El feed futuro está **UNOBSERVABLE BECAUSE CURRENT HLE BLOCKS EXECUTION**
en la ruta guest relevante; no equivale a que no haya bytes disponibles.

## Qué sobrevive realmente: comparación binaria

`analyze_ram.py` compara los dumps de 32 MiB pre-init y primer GetPicture.
Estos son snapshots de lectura en puntos EE concretos, no una promesa de
atomicidad global frente a todos los hilos host.

Movie state `0x0087D680`, MPEG `state+0x278=0x0087D8F8`:

| Almacenamiento/campo | Pre-init | Primer GetPicture |
|---|---|---|
| PSS ring base/capacidad | `0x01CB37E0` / 327680 | idénticos |
| PSS write cursor `+8` | 0 | 0 |
| PSS available `+0xC` | 65536 | 65536 |
| viBuf base/capacidad | `0x01C337D0` / 524288 | idénticos |
| viBuf pending `+0x2C8` | 228084 | 228084 |
| viBuf block cursor/queued `+0x2C0/+0x2C4` | 0 / 0 | 0 / 0 |
| Video byte counter `+0x28` | 228084 | 228084 |
| Movie state primeras 0x300 bytes | snapshot | cero palabras distintas |
| Host pssBuffer/sync buffer | 0 / 0 bytes | objeto destruido; defaults vacíos |
| Host FFmpeg decoder | presente, produjo 4 frames | destruido; objeto nuevo sin decoder |
| Host frame queue | 4 | 0 |
| Registro de callbacks host | 1 clave MPEG | 0 claves |

El ring PSS completo y viBuf completo son byte-idénticos pre/post en las
tres corridas. viBuf además tiene el mismo SHA entre 008,009,010:
`2a8bbb94e792a946a1229d10b6b9c99af29104decec8dae8ad77972e4667acc5`.
PSS SHA RUN_008:
`de59ded2274119fcd74385aeb0283740a089d70f020e59fa8e210b31b0486ec4`.

`ReadDataGetAddr@0x3FE3F0` demuestra que el cursor de lectura es
`(writeCursor + capacity - available) % capacity`; aquí quedan 64 KiB
lógicamente disponibles desde offset 262144. Los primeros 262144 bytes
siguen físicamente presentes pero ya fueron contabilizados como demuxed.
No se pueden reinyectar ciegamente sin definir ownership/cursor.
`produced=0` es el contador host observado, NO ausencia de transporte:
la lectura real del CD y el ring lleno lo contradicen como interpretación.
Cantidad exacta pendiente dentro de FFmpeg antes de destruirlo: **UNKNOWN**.

### Anomalía adicional anterior al init: ES guest no coincide

**HECHO:** extracción independiente de los 56 PES vídeo completos de los
262144 bytes PSS consumidos produce exactamente **228084 bytes**,
SHA `127e3b621ba1d33b1ee9584681fb78431befaf13cbd4972fc0b2de1cb132b82c`.
El ES extraído comienza `00 00 01 B3 ...`; el buffer guest comienza ceros.
Primera diferencia binaria en offset 2. El primer sequence header del
buffer guest está en offset 61084, no en 0. Esa disposición se repite en
los tres dumps pre-init. Por tanto no nace de sceMpegInit.

**INFERENCIA:** hay un problema o una representación no comprendida en la
vía PES→callback→viBuf. **UNKNOWN:** primera instrucción causal y estado
equivalente PCSX2. No atribuirlo todavía a scheduler, copy helper, lifetime,
LQ/SQ, orden de callbacks ni colisión de memoria.

`probe_buffers.py` preserva extracción, comparación, comandos y salida de
ffprobe (ya instalado). Offline devuelve MPEG2 512x448 y 6 frames para el
ES guest, pero con errores de slices/textura y concealment; el PSS completo
devuelve 7 y el ES extraído 6 con errores al final del prefijo truncado.
**No es prueba de seis pictures correctas disponibles para el juego.**
Sí refuta deducir «no existen datos comprimidos» solo de decodedFrames=0.
No se alimentó el runtime con estos archivos ni se aplicó reparación.

## Último nodo común y primera divergencia: alcance exacto

Último nodo común demostrado en el segmento auditado:
`MpegMovieDecode ENTRY → MpegMovieDecodeInit → sceMpegInit@0x109CD0`,
call site `0x47A6EC`. ORIGINAL resetea DMA/IPU y conserva RAM/objetos/
callbacks software. RECOMP destruye sus objetos de playback/callbacks y
recrea un state vacío. **Primera divergencia contractual localizada en
este segmento: over-reset de sceMpegInit.**

Después, ORIGINAL GetPicture ejecuta su cadena síncrona con IPU y, en la
observación PCSX2 histórica P3.8.1, retorna 1 a `0x47A8EC` antes de volver
a Movie_ctrl. RECOMP nuevo vacía la cola y suspende. La evidencia nueva
RECOMP y la estática del ELF explican el mecanismo anterior al wait.

**No afirmar que sea la primera divergencia absoluta desde cold boot**:
la anomalía ES guest anterior a init no tiene comparación PCSX2 equivalente.
La confirmación PCSX2 citada es histórica; no se ejecutó una sesión nueva.
El proceso PCSX2 PID 54580 ya existía desde antes de esta investigación y
no se tocó. Se inspeccionó README del PCSX2_MCP local; exige GDB/qPcsx2
según capacidades, y no había tool PCSX2 conectado en esta sesión.

Modelo sustentado: demux/callback almacena ES guest; init prepara hardware
sin borrar ese almacenamiento; caller configura DMA/IPU; GetPicture
procesa síncronamente hasta output/error. El HLE adelanta decode al demux,
guarda output en host y luego destruye ese trabajo antes de pedirlo. Falta
un contrato coherente de conservación de entrada/output en esa frontera.
Over-reset está demostrado; necesidad de un bridge síncrono adicional
después de corregir ownership sigue por validar, no es fix ya decidido.

## Upstream, precedentes y tests

Historia local relevante: `9f99d615c5d7022eab9b16cc79c89a95ab0e1556`
("bad wip mpeg fix for code veronica") toca MPEG, CD, scheduler y tests;
`f4309cd` integra scheduler (#184). Es contexto del diseño, no prueba
causal de DMC. No se copiaron commits ni se cambió vendor HEAD.

El [MPEG.cpp remoto consultado](https://github.com/ran-j/PS2Recomp/blob/main/ps2xRuntime/src/lib/Kernel/Stubs/MPEG.cpp)
todavía incluye resetMpegStubStateUnlocked con clear de callbacks/playback
en sceMpegInit. No se encontró allí un fix aplicable para este contrato.
No presentar la consulta a main sin pin como auditoría de todos los PRs.

[PCSX2 IPU.cpp](https://github.com/PCSX2/pcsx2/blob/master/pcsx2/IPU/IPU.cpp)
confirma que reset IPU limpia FIFO/bit state y que BCLR reinicia entrada;
[IPU.h](https://github.com/PCSX2/pcsx2/blob/master/pcsx2/IPU/IPU.h) identifica
FDEC/SETIQ/SETVQ/SETTH. Contexto primario de implementación del hardware,
no sustituto de una observación dinámica de DMC. El ELF establece cuáles
de esos accesos realiza este juego.

Tests encontrados en `ps2xTest/src/ps2_runtime_expansion_tests.cpp`:
"sceMpegGetPicture blocks as a typed scheduler wait and resumes on EOF"
y "waits for new decoder output instead of duplicating the last frame",
además de callback/demux/EOF/reset. Esos tests validan el diseño HLE actual,
no el contrato original DMC; pasar uno no resolvería esta contradicción.
No había ejecutable de tests en el build activo; no se construyó una suite
grande. Validación pertinente: corridas controladas y comparación binaria.

## Diseño recomendado del futuro fix — NO implementado

Separar inicialización de hardware/libmpeg de destrucción de una sesión.
`sceMpegInit` no debe perder callbacks, configuración ni ownership de bytes
ya aceptados por el demux. Definir cómo los frames decodificados por
anticipado representan esos bytes al atravesar la inicialización IPU;
preservarlos o reconstruir desde entrada retenida requiere contrato claro,
no un caso especial para cuatro frames o esta dirección MPEG.

Conservar los eventos explícitos de fin/cambio de stream y sus generaciones;
validar por separado Create/Reset/Delete contra ORIGINAL antes de reutilizar
sus resets. Si sigue faltando output en GetPicture, diseñar una vía que
consuma entrada lógicamente disponible y entregue una picture válida con
frameCount/layout correctos, sin inventar pending=0, duplicar la anterior
ni desbloquear el scheduler con éxito ficticio. No asumir que la mera
conservación de la cola garantiza presentación GS visible.

Tests propuestos: Create/feed/Init/GetPicture conservando callback y trabajo
aceptado; múltiples Init sin pérdida indebida; separación entre películas;
EOF con datos pendientes; exactamente una actualización de frameCount y
buffer por picture; casos sin entrada y error; comparación de ES callback
byte a byte antes de usar viBuf como fuente de un bridge; 3/3 cold boots
con ISO/arena y validación visual del output. Los tests genéricos del
scheduler siguen separados de la adaptación del contrato de libmpeg.

## Próximo prompt recomendado

**P3.12 — ORIGINAL/RECOMP VIDEO_ES_OWNERSHIP_AND_INIT_BOUNDARY**:
conservar este checkpoint sin reabrir heap; capturar en PCSX2 el mismo
state/viBuf antes y después de sceMpegInit; localizar el primer byte que
diverge en StrM2vCallBack/viBuf respecto a los PES fuente; después decidir
el contrato de preservación del HLE y autorizar un fix concreto separado.
No convertir la anomalía ES nueva en causa demostrada sin esa comparación.

## Unknowns que permanecen

Primera divergencia absoluta anterior a init; causa de ES guest distinto;
equivalencia temporal entre reset IPU y contexto FFmpeg anticipado; bytes
exactos pendientes dentro de FFmpeg; disponibilidad de una picture correcta
desde viBuf actual; frames posteriores con ownership corregido; presentación
GS visible; title/gameplay. No hay evidencia de viabilidad del juego completo.

## Intento de título y cierre de ramas

Después del 3/3, RUN_011 envió un solo Start de 250 ms a los 75.297 s,
cinco segundos después de GP:H. Log confirma FFF7→FFFF. Se observó hasta
130.266 s: solo GetPicture ENTRY #1, ningún frame nuevo después del reset,
ningún avance a título. `state_06.png` a 120 s sigue negro/corrupto, sin
pantalla de título identificable. SUCCESS en JSON significa alcanzar el
marker diagnóstico, **NO** TITLE_REACHED. No se intentó gameplay ni se
confirmó ninguna opción de guardado. CAPCOM/PSS como fase queda sustentado
por resourceId=0x22E y feed; el logo CAPCOM visible no se certifica por las
capturas negras/corruptas disponibles.

El límite de la rama no es falta de input: canal probado 3/3 y máscara de
skip observada. Modificar la suspensión o preservar artificialmente frames
sería un fix/experimento semántico no autorizado. Se cierra esta sesión de
diagnóstico sin ello, con buffers/logs y diseño pendientes preservados.

## Artefactos, cambios propios y seguridad

- `analysis/notes/BLOCKER_004_ASTRA_P311_OVERNIGHT.md`: informe completo.
- Nota canónica: solo sección breve P3.11 añadida; historia conservada.
- `analysis/tools/runtime_loop/`: run.py, build_one.ps1, elf_contract.py,
  analyze_ram.py, probe_buffers.py, README.md, example.log.txt.
- `runtime/dmc_overrides.cpp`: dos líneas (include y arranque opt-in).
- `runtime/p311_pad_test.inc`: adaptador temporal usando API de test existente.
- Vendor MPEG.cpp: solo trazas P311 y dos dumps opt-in; comportamiento previo
  conservado. Otros archivos dirty de vendor pertenecían al checkpoint previo.
- `analysis/local/p311/`: snapshots de diffs, logs, resultados, capturas,
  RAM/buffers y backups de biblioteca. No añadirlos a Git.

`git diff --check` principal y vendor pasó al cierre de pruebas. No se
encontraron ejecutables de tests en el build activo; syntax check Python
pasó. La verificación funcional son las corridas y dumps descritos.
Se conservaron los experimentos fallidos y sus resultados; no se transformó
timeout en crash ni captura fallida en evidencia visual.

**Confirmación:** NO commit, NO push, NO clean/full build, NO regenerate,
NO C++ generado editado, NO driver instalado, NO configuración del sistema
modificada, NO proceso ajeno terminado. PCSX2 PID 54580 permaneció abierto.

## Git status final y observaciones de integridad

HEADs sin cambios. Principal:

```text
 M analysis/notes/BLOCKER_004_pss_video_output.md
 M runtime/dmc_overrides.cpp
 M upstream.lock.json
?? analysis/notes/BLOCKER_004_ASTRA_AUDIT.md
?? analysis/notes/BLOCKER_004_ASTRA_P311_OVERNIGHT.md
?? analysis/notes/BLOCKER_004_P36_HEAP_COLLISION_AUDIT.md
?? analysis/tools/
?? patches/BUILD_ffmpeg_private_link_scope.patch
?? runtime/p311_pad_test.inc
```

Vendor sigue dirty en los seis archivos del snapshot de entrada; solo
MPEG.cpp recibió adiciones propias P311. `vendor-final.patch` preserva
el diff final. No se cambió el lock ni se limpiaron modificaciones ajenas.
La consulta sandbox mostró transitoriamente `.claude/` como untracked al
no poder leer ignores globales; la consulta final fuera del sandbox no lo
muestra. No se editó `.claude/`.

La nota canónica registra variación del diff fuera de la sección P3.11
respecto a los conteos iniciales (4053/1 frente a 4096/14 inserciones/
eliminaciones). La única edición propia allí fue el apéndice de 30 líneas;
no se atribuyen a esta investigación las otras diferencias observadas.
Se preservaron y se dejó de editar esa nota al advertirlo, sin revertirlas.

Validación final adicional: biblioteca con un MPEG.obj; objetos MPEG.obj
3320093 bytes y dmc_overrides.obj 718036 bytes (ninguno vacío); scripts
Python parsean; sin procesos RECOMP propios restantes. RUN_011 también
usó el SHA final. `runs_summary.json` actualizado incluye las 11 corridas.
