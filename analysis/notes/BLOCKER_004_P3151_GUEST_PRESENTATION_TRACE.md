# BLOCKER_004 — Prompt P3.15.1: CORRECT_GETPICTURE_GUEST_STATE_AND_TRACE_MOVIE_PRESENTATION_PATH

## P3.15.2 CORRECTION (posterior, ver informe dedicado)

La sección 9 de abajo dejaba como pregunta abierta si la cadena de
tags que `Movie_loadimage` dispara referencia realmente los píxeles
frescos de `imageAddr`. P3.15.2 la respondió con instrumentación
dinámica real (no especulación): **sí, de forma exacta** — el primer
tag REF de la cadena apunta literalmente a `0x79D680` (byte exacto) y
32 bandas cubren el buffer completo (917504 bytes = 512×448×4 exacto),
en 40/40 disparos observados. Esto **retracta parcialmente** la
preocupación implícita de esta sección de que la arquitectura
"configurar una vez (`Movie_set_loadimage3`) + disparar muchas veces
(`Movie_loadimage`)" pudiera ser un defecto — es, según la evidencia
dinámica, un diseño coherente y funcionando como cabría esperar. Ver
[BLOCKER_004_P3152_MOVIE_TAG_CHAIN_IMAGEADDR_DATAFLOW.md](BLOCKER_004_P3152_MOVIE_TAG_CHAIN_IMAGEADDR_DATAFLOW.md).

## 1. Estado de entrada

Main HEAD: `006b0f62585a2ff8d127bf0d2c84e6d6e4fc1f27` (verificado). Vendor
HEAD: `61a0977924148e018d66d42c30d7756db8084541` (verificado, limpio).
Main contenía, sin commitear, el informe P3.15 y el apéndice canónico
correspondiente — ninguno se descartó; se corrige preservando la
retractación histórica según exige el prompt.

## 2. Evidencia PCSX2 nueva, tal como la recibí

```
PC = 0x0047A8EC: v0=0x00000000, s1=0x0087D8F8, ra=0x0047A8EC
0x0047A900  lw  v0,0x0(s1)
0x0047A904  bne v0,zero,0x0047AA18
Memoria en s1: +0x00=0x00000200(512) +0x04=0x000001C0(448) +0x08=0x00000000
```

## 3. Retractaciones formales — IMPORTANTE: retracto la afirmación del prompt, no mi hallazgo de P3.15

**Verificación independiente realizada ANTES de aceptar cualquier
retractación** (exigencia de rigor de esta sesión: nunca aceptar
"nueva evidencia" sin verificarla, incluida la del propio prompt).
Decodifiqué manualmente, byte a byte, los words MIPS reales del ELF
`original/SLES_503.58` (offset de archivo calculado desde los program
headers: segmento 0, `vaddr=0x100000`, `offset=0x280`), sin depender
del código generado ni de la transcripción del prompt:

```
0x0047a8ec  0x04410004  bgez  v0  (rs=v0, offset=+4 palabras)
0x0047a900  0x8e220008  lw    v0, 0x8(s1)     <- opcode 0x23(lw), rs=s1(17), rt=v0(2), imm=0x0008
0x0047a904  0x14400044  bne   v0, zero, ...
0x0047a90c  0x8e220000  lw    v0, 0x0(s1)     <- ESTA es la instrucción "lw v0,0x0(s1)"
```

**HECHO, verificado por triple fuente independiente** (bytes crudos del
ELF decodificados a mano + código generado `MpegMovieDecode_0x47a760.cpp`
línea 550-552, ya leído en P3.15 + re-lectura en este prompt): la
instrucción en `0x47A900` es `lw $v0, 0x8($s1)` — lee
`*(mpegAddr+0x08)`, exactamente como afirmó P3.15. La instrucción
`lw v0,0x0(s1)` que el prompt atribuye a `0x47A900` **existe
realmente, pero está en `0x47A90C`**, seis instrucciones más adelante
(la copia de `width` a la variable local `*(s0+0x7C)`, ya documentada
en P3.15 sección 9 como parte del camino `==0`).

**RETRACTADO — pero el retractado es el enunciado del prompt, no
P3.15**: la premisa "`0x47A900` lee `mpegAddr+0x00`, no `mpegAddr+0x08`"
es incorrecta. La interpretación original de P3.15 (bifurcación real
sobre `*(mpegAddr+0x08)`) **se mantiene, con evidencia aún más fuerte**
(verificación de bytes crudos del ELF, no solo del código generado).
Causa más probable de la discrepancia: un desfase de dirección al
transcribir la sesión de PCSX2 (posible scroll/copy-paste de la lista
de instrucciones que capturó la línea correcta con la etiqueta de
dirección incorrecta) — se lo señalo al usuario para que revise su
propia sesión, sin acusación, con la evidencia byte-exacta como
desempate.

**Lo que SÍ se conserva de la evidencia nueva** (no depende de la
dirección exacta del mnemónico, y es información nueva y válida):
- Los valores de memoria reportados (`+0x00=512`, `+0x04=448`,
  `+0x08=0`) son consistentes con el `movieState`/`mpegAddr` real y con
  la interpretación ya establecida (`+0x00`=ancho, `+0x04`=alto). Si
  `+0x08=0` corresponde genuinamente a la primera imagen exitosa de la
  sesión, es exactamente el valor que la hipótesis de P3.15 (contador
  monótono vs. algo distinto) predeciría en ese punto — **no distingue
  entre hipótesis, ni las refuta**.
- El retorno `v0=0` capturado en `0x47A8EC` es un dato potencialmente
  útil y **no contradice** la conclusión de P3.15 sobre `v0` — al
  contrario, si es genuino, refuerza que el original también puede
  devolver `0` en al menos algunos casos reales, consistente con
  "`bgez` trata 0 y 1 igual". No se puede corroborar de forma
  independiente en este prompt (no hay traza PCSX2 propia); se reporta
  como HECHO-reportado-por-el-usuario, no HECHO verificado por mí.

**NO se retracta**: "v0 es causal" nunca se afirmó — la sección 2
confirma la conclusión ya alcanzada, no la contradice.

## 4. Desensamblado correcto de la bifurcación

```
0x47A8EC  bgez  v0, 0x47A900      (rama si v0>=0; solo negativo diverge, ruta printf)
0x47A900  lw    v0, 0x8(s1)        v0 = *(mpegAddr+0x08)
0x47A904  bne   v0, zero, 0x47AA18  (si *(mpegAddr+0x08) != 0 -> Movie_loadimage; si ==0 -> cae a 0x47A90C -> Movie_set_loadimage3)
```

## 5. Significado exacto del operando de la bifurcación

`s1` = `mpegAddr` (confirmado en P3.15 y re-confirmado aquí: `s1` se
carga en `MpegMovieDecode` ENTRY como `0x880000-0x2708 = 0x87D8F8`,
coincide exactamente con el `mpegAddr` observado dinámicamente en
P3.14.2 y con el valor `s1=0x0087D8F8` que el propio prompt reporta).
El operando de `bne` es `*(mpegAddr+0x08)` — sin cambios respecto a
P3.15.

## 6. Valores observados en el original: +0x00=512, +0x04=448, +0x08=0

Aceptados como reportados por el usuario (no verificados
independientemente por mí en este prompt — no tengo sesión PCSX2
propia). Coherentes con lo esperado.

## 7. Comparación con las escrituras de la HLE

| Campo | Original (PCSX2, reportado) | HLE actual |
|---|---|---|
| `+0x00` (ancho) | 512 | `playback.width` — coincide si el PSS decodificado es 512×448 |
| `+0x04` (alto) | 448 | `playback.height` — ídem |
| `+0x08` | 0 (en el punto capturado) | `picturesServed` antes de incrementar — 0 en la primera imagen exitosa, coincide en ESE punto |

**512×448 coincide con las dimensiones reales reportadas por la propia
HLE en todas las corridas P3.14.2** (`[MPEG:diag] getPicture success
#N size=512x448`, confirmado en los logs ya existentes de P3.14.2) —
**HECHO: concordancia estructural de ancho/alto entre original y
HLE**, clasificado `SUPPORTED`.

## 8. Conclusión actualizada sobre `v0`

Sin cambios respecto a P3.15: **no causal** para el único caller
(`bgez`). El nuevo dato `v0=0` reportado en el original refuerza en
vez de debilitar esa conclusión (ver sección 3).

## 9. Desensamblado completo de `Movie_loadimage` (0x3FE590-0x3FE5B8, 40 bytes)

Ya completo en P3.15, re-verificado aquí instrucción por instrucción:

```
0x3fe590  dsll32 v1,a0,4   ; v1 = a0 << 36 (efectivamente v1=a0, máscara de 32 bits)
0x3fe594  dsrl32 v1,v1,4
0x3fe598  lui    at,0x1001
0x3fe59c  sw     v1,-0x5FD0(at)   -> Store32(0x1000A030, v1)   [GIF_TADR]
0x3fe5a0  lui    at,0x1001
0x3fe5a4  sw     zero,-0x5FE0(at) -> Store32(0x1000A020, 0)     [GIF_QWC]
0x3fe5a8  addiu  v1,zero,0x105
0x3fe5ac  lui    at,0x1001
0x3fe5b0  jr     ra
0x3fe5b4  sw     v1,-0x6000(at)   -> Store32(0x1000A000, 0x105) [GIF_CHCR]  (delay slot)
```

## 10. Identidad de cada registro `0x1000A0xx` tocado

**HECHO, confirmado directamente contra el código fuente del propio
runtime** (`vendor/PS2Recomp/ps2xRuntime/src/lib/ps2_runtime.cpp:2368-2371`
y `ps2_memory.cpp`), NO por apariencia/nomenclatura como advertía el
prompt:

| Dirección | Registro real PS2 (constante nombrada en el runtime) |
|---|---|
| `0x1000A000` | `GIF_CHCR` — control/kick del canal DMA 2 (GIF) |
| `0x1000A020` | QWC del canal DMA 2 (quadword count) |
| `0x1000A030` | `GIF_TADR` — tag address del canal DMA 2 (modo chain) |

Son **registros reales de DMA channel 2 (GIF)**, no IOP/SIF como
especuló P3.15 (sección 9 de P3.15, explícitamente marcada allí como
INFERENCIA, no HECHO). **RETRACTADO explícitamente**: la caracterización
de P3.15 de `Movie_loadimage` como "probable IOP/SIF, posiblemente no
relacionado con GS" — es errónea. Se corrige en la sección 11.

## 11. Rol exacto de `Movie_loadimage`

**HECHO**: `Movie_loadimage` escribe TADR, luego QWC=0, luego
CHCR=0x105 — la secuencia exacta para iniciar una transferencia DMA en
**modo chain** (bits 2-3 de CHCR = `(0x105>>2)&3 = 1` = chain mode) por
el canal GIF. Verificado contra `PS2Memory::writeIORegister`
(`ps2_memory.cpp:1268-1318`): al detectar una escritura a
`channelBase+0x00` con el bit STR puesto (`value & 0x100`... nota:
0x105 tiene el bit 0x100 puesto) sobre `channelBase==0x1000A000`, el
runtime **SÍ tiene una ruta dedicada, real e implementada** que lee
`tagAddr = m_ioRegisters[channelBase+0x30]` (=TADR, lo que
`Movie_loadimage` acaba de fijar) y procesa la cadena de tags,
incluyendo lectura en vivo de `m_rdram` (`appendData`, línea
1321-1354, lee memoria real en el momento de la transferencia, no en
el momento de construir el paquete). **`Movie_loadimage` SÍ es
genuinamente la vía real de presentación GS por-frame — GS-relacionada,
no IOP/SIF.**

**Nota separada**: existe además una función `kickGifDmaChainFromMMIO`
(`ps2_runtime.cpp:2361`) con constantes `GIF_TADR`/`GIF_CHCR` nombradas
explícitamente y lógica de `tryProcessNativeGifImageUploadChain`/
`tryProcessNativeGifPackedChain` — **verificado que no tiene ningún
caller en todo el runtime** (grep exhaustivo, 0 resultados fuera de su
propia definición y declaración). Es código muerto/no cableado; NO es
la ruta que realmente procesa la escritura de `Movie_loadimage` (esa
va por `writeIORegister`, sección anterior). Se documenta por
completitud — no bloquea la conclusión de la sección 11, que se basa
en `writeIORegister`, la ruta real y confirmada.

## 12. Dataflow de `Movie_set_loadimage3`

Confirmado (P3.15 + re-lectura): bucle anidado ancho×alto (`t7=width>>4`
como límite externo, `s2=a2*t2` como producto ancho-relacionado
interno) que escribe entradas de 16 bytes alineadas con patrones de
bits tipo GIFtag (`0x2000_0000|...`, `0x1000_0000...`) en un cursor que
avanza `sp+0x8C`, hacia un buffer indexado por `*(0x740760)*stride`
desde base `0x773480` (el mismo rango que indexa `Movie_loadimage`).
**INFERENCIA, no verificada exhaustivamente**: construye la(s)
cadena(s) de tags GIF que `Movie_loadimage` disparará después,
posiblemente una tabla de N slots pre-formateados (round-robin vía
`0x740760`).

## 13. ¿Referencia `Movie_set_loadimage3` a `imageAddr` (0x79D680)?

**UNKNOWN, no resuelto en este prompt.** No se completó el rastreo de
cada referencia de memoria dentro de sus ~0x2D4 bytes buscando ese
valor exacto o un puntero derivado de él. Los argumentos que recibe
(`a2=0x400`, `a3=0/0x1400`, `t2`=ancho, `t3`=alto) no incluyen
`imageAddr` como literal — si lo referencia, sería vía otra dirección
fija no identificada aún, o el propio `writeIORegister`/`appendData`
podría leerlo indirectamente en tiempo de transferencia si el tag
apunta allí. **Esta es la pregunta de mayor valor pendiente** —
followup explícito.

## 14. Consumidores probados de `imageAddr`

Único consumidor confirmado: `writeDecodedFrameToGuest` (HLE, escribe
los píxeles). Ningún consumidor guest confirmado en este prompt —
limitación de la sección 13.

## 15-17. Layout de imagen HLE vs esperado, tabla de coincidencia

No auditado en profundidad en este prompt (Fase 6 del prompt, fuera del
foco principal dado el hallazgo de mayor prioridad en la sección 11).
**UNKNOWN** — followup.

## 18-19. Primera operación guest de cara a hardware / frontera guest→runtime

**HECHO, la respuesta central de este prompt**: la primera operación
guest que efectivamente escribe un registro de hardware real
(DMA/GIF) tras una imagen decodificada exitosa es la secuencia de
`Movie_loadimage` (`0x3FE590`, sección 9) — TADR→QWC→CHCR sobre el
canal DMA 2 — **para la segunda imagen en adelante**; para la primera
imagen, es el conjunto de escrituras que construye
`Movie_set_loadimage3` (no confirmado que ella misma dispare el DMA
directamente en la porción ya leída — ver limitación sección 13).

## 20. ¿Ejecutan original y RECOMP el mismo camino guest?

**SÍ, estructuralmente, hasta donde se verificó.** El código de
`MpegMovieDecode`/`Movie_loadimage`/`Movie_set_loadimage3` es código de
juego real, sin reemplazo HLE — RECOMP lo ejecuta tal cual (no hay
sustitución de `PS2_SYSCALL_LIST` para estas tres funciones). La
bifurcación sobre `*(mpegAddr+0x08)` se alcanza igual en ambos.

## 21. ¿Recibe el runtime datos equivalentes en esa frontera?

**HECHO, confirmado**: la escritura de `Movie_loadimage` a
`0x1000A000` con `chcr=0x105` (modo chain) SÍ es interceptada por una
ruta real e implementada (`writeIORegister`, sección 11) que procesa la
cadena de tags leyendo memoria guest en vivo — no es un no-op ni un
stub. Lo que NO se confirmó es si el CONTENIDO de esa cadena de tags
(construida por `Movie_set_loadimage3`) referencia correctamente los
píxeles frescos de `imageAddr` (sección 13, UNKNOWN).

## 22. LAST COMMON NODE

Sin cambios: post-reset de `sceMpegInit` bajo `ownership`.

## 23. FIRST DIVERGENT NODE

**No establecido con certeza en este prompt.** El candidato de P3.15
(`mpegAddr+0x08` causando que `Movie_set_loadimage3` se ejecute una
sola vez) sigue siendo estructuralmente válido tal como se describió
—no fue refutado, la "refutación" del prompt resultó ser un error de
transcripción de dirección (sección 3)— pero ya no es necesariamente
el nodo de mayor prioridad: el hallazgo de que `Movie_loadimage`
también dispara DMA GIF real (sección 11) significa que **incluso las
imágenes 2ª en adelante SÍ llegan a un camino de presentación GS real**,
no a una vía muerta como P3.15 temía. El nodo divergente más probable
ahora es más profundo: dentro de `tryProcessNativeGifImageUploadChain`/
`tryProcessNativeGifPackedChain` (interpretación del contenido real de
la cadena de tags) — territorio explícitamente fuera de alcance de
este prompt ("no inspeccionar OpenGL/GS todavía").

## 24. ¿Fue `+0x08` causal?

**NO REFUTADO — la afirmación del prompt de que lee `+0x00` es
incorrecta (sección 3).** La bifurcación sobre `+0x08` es real. Sigue
sin probarse que sea la causa raíz de los glitches visuales (eso
requeriría la medición de la sección 17 de P3.15, aún no ejecutada) —
pero tampoco se retracta como candidato.

## 25. ¿Es `v0` causal?

**NO.** Sin cambios (sección 8).

## 26. ¿Provee la HLE ancho/alto correctamente?

**SÍ — HECHO**, 512×448 coincide con lo reportado del original y con
lo que la propia HLE ya produce en cada corrida P3.14.2.

## 27. ¿Es `imageAddr` estructuralmente compatible con lo que consume DMC?

**UNKNOWN.** No resuelto — ver sección 13/14.

## 28. ¿Cuál es el problema más probable ahora?

**Entre las opciones dadas: `GUEST_SUBMISSION_DIVERGENCE` es la mejor
etiqueta disponible, con la reserva explícita de que la submission en
sí (TADR/CHCR) se confirmó correcta — la incertidumbre real está un
nivel más abajo, en si el CONTENIDO de la cadena de tags que se
somete referencia los píxeles correctos.** No es `MPEG_CONTRACT_MATCHES`
puro (queda la duda de la sección 13/23), pero tampoco hay evidencia
de `RUNTIME_HARDWARE_CONTRACT_DIVERGENCE` ni de problema en OpenGL.

## 29. ¿Hay evidencia contra OpenGL?

**NO EVIDENCE YET.** Ni en este prompt ni en P3.15 se inspeccionó
código de GS/OpenGL. La frontera identificada (`writeIORegister`,
`tryProcessNativeGif*Chain`) está claramente ANTES del backend de
renderizado.

## 30. Clasificación

**`GUEST_SUBMISSION_DIVERGENCE`** (candidato principal, con la
salvedad de la sección 28) — reemplaza la clasificación
`GETPICTURE_GUEST_STATE_BUG` de P3.15, que se mantiene como hipótesis
viva pero ya no como el hallazgo más significativo de la investigación.

## 31. CHECKPOINT DECISION

**NO_COMMIT.** No se implementó ningún cambio de código en este
prompt (correctamente, según lo exigido) — solo corrección de
documentación y una traza causal más profunda.

## 32. Próximo prompt recomendado

**P3.15.2 — MOVIE_TAG_CHAIN_IMAGEADDR_DATAFLOW**: cerrar la sección
13/14 (la pregunta de mayor valor pendiente) — determinar de forma
concluyente si la cadena de tags que `Movie_set_loadimage3` construye
(y que `Movie_loadimage` dispara por DMA) referencia `imageAddr`
(`0x79D680`) directa o indirectamente, completando el rastreo de
`Movie_set_loadimage3` y, si hace falta, un diagnóstico acotado
(opt-in) en `tryProcessNativeGifImageUploadChain`/`PackedChain` para
observar en runtime qué direcciones fuente reales llegan ahí durante
una corrida P3.14.2 real. Solo después de esto tiene sentido decidir
si `P4.0 — GS_GUEST_TO_RUNTIME_PRESENTATION_CONTRACT` es el siguiente
paso natural.

## 33. Estado final de git

Main: sin commits — este informe nuevo sin trackear, más la corrección
pendiente de aplicar a `BLOCKER_004_P315_GETPICTURE_PRESENTATION_CONTRACT.md`
y al apéndice canónico (secciones siguientes de este mismo turno).
Vendor: limpio, intacto, HEAD sin cambios.

## 34. Confirmaciones explícitas

NO commit, NO push, NO clean/regenerate, NO instrumentación GS amplia,
NO modificación de OpenGL, NO corrección especulativa de `+0x08`, NO
corrección especulativa de `v0`.
