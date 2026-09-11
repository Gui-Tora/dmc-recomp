# BLOCKER_004 — Prompt P3.15: GETPICTURE_RETURN_AND_GUEST_PRESENTATION_CONTRACT

## P3.15.1 CORRECTION (posterior, ver informe dedicado)

Tras este informe, el prompt P3.15.1 aportó una supuesta evidencia
PCSX2 que afirmaba que `0x47A900` lee `*(mpegAddr+0x00)`, no
`*(mpegAddr+0x08)` como concluye la sección 9 de abajo. **Verificación
independiente por decodificación byte a byte del ELF original
(`original/SLES_503.58`) confirmó que la sección 9 de este informe es
CORRECTA**: `0x47A900` = `0x8e220008` = `lw $v0, 0x8($s1)`. La
instrucción `lw v0,0x0(s1)` que P3.15.1 recibió sí existe, pero está en
`0x47A90C`, no en `0x47A900` — un desfase de dirección en la
transcripción de la evidencia recibida, no un error de este informe.
**La interpretación de la bifurcación sobre `mpegAddr+0x08` (secciones
9, 14 de abajo) NO se retracta.**

Lo que SÍ se retracta formalmente, por evidencia real y verificada en
P3.15.1: la caracterización de `Movie_loadimage` (sección 9-10 de
abajo) como "probablemente IOP/SIF, posiblemente no relacionado con
GS" es **RETRACTADA**. Confirmado contra el código fuente del propio
runtime (`GIF_TADR`/`GIF_CHCR` en `ps2_runtime.cpp`, manejo real de
modo chain en `PS2Memory::writeIORegister`): `Movie_loadimage`
dispara una transferencia DMA real por el canal GIF (canal 2) — es
genuinamente parte de la vía de presentación GS, no un mecanismo
ajeno. Ver
[BLOCKER_004_P3151_GUEST_PRESENTATION_TRACE.md](BLOCKER_004_P3151_GUEST_PRESENTATION_TRACE.md)
para el detalle completo y la traza corregida.

## 1. Checkpoint de entrada

Main HEAD: `006b0f62585a2ff8d127bf0d2c84e6d6e4fc1f27` (verificado, limpio).
Vendor HEAD: `61a0977924148e018d66d42c30d7756db8084541` (verificado, limpio).
Ambos coinciden exactamente con lo esperado.

## 2. Todos los callers de `sceMpegGetPicture` (0x109FF0)

Búsqueda exhaustiva (agente dedicado, grep `109ff0`/`109FF0` sobre los
10.129 archivos de `analysis/local/symtabfirst/generated/`): **un único
caller estático en todo el binario recompilado**: `MpegMovieDecode`
(`0x47a760`), llamada en `0x47A8E4`, retorno `0x47A8EC`. No se encontró
ningún otro `jal` a esta dirección. `sceMpegGetPictureRAW8`/`RAW8xy`
están registradas en la tabla de despacho pero no tienen ningún caller
estático detectable — no relevantes para DMC.

## 3. Uso exacto del valor de retorno por cada caller

Único caller (`MpegMovieDecode`, `0x47A8EC`):

```
0x47a8ec: bgez $v0, -> 0x47A900     (branch si v0 >= 0)
```

**HECHO, por desensamblado directo**: la instrucción es `bgez`
(*Branch if Greater than or Equal to Zero*), NO `bnez`/`beqz`/`slti`.
Prueba `v0 >= 0`. Si es verdadero (v0 = 0, 1, o cualquier no-negativo)
→ salta a `0x47A900`. Si es falso (v0 < 0) → cae a `0x47A8F4`, que llama
`printf` (ruta de error/log) y luego confluye igualmente en `0x47A900`.

## 4. Rutas de retorno originales (reconstrucción)

No hay código MIPS real de `sceMpegGetPicture` en el árbol generado —
el archivo `sceMpegGetPicture_0x109ff0.cpp` es un trampolín que ya
redirige a la HLE (confirma lo ya documentado: funciones en
`PS2_SYSCALL_LIST` no conservan su cuerpo MIPS real). No es posible
reconstruir las rutas de retorno originales por desensamblado directo
de la función en sí — solo por el USO que el caller hace de `v0`
(sección 3), que es evidencia real de código de juego sin reemplazar.

## 5. Rutas de retorno de la HLE actual

`vendor/PS2Recomp/.../MPEG.cpp:3158`: `setReturnS32(ctx, 0)` —
**único punto de retorno de toda la función**, incondicional, tanto en
éxito (`haveFrame=true`) como en "sin trama" (`frameCount==0`, dibuja
`writeBlankMpegFrame`) como en cualquier otro camino que llegue hasta
ahí. **HECHO**: la HLE nunca retorna un valor negativo en ningún
camino observado en el código fuente actual.

## 6. ¿Importa 0 vs 1 para DMC?

**NO — HECHO, no INFERENCIA.** `bgez` trata 0 y 1 de forma idéntica
(ambos toman la misma rama). El único valor que importaría es
NEGATIVO, que la HLE nunca produce. **v0=0 (HLE) vs v0=1 (original,
observado dinámicamente en P3.8.1) es un CONTRACT MISMATCH real pero
NO CAUSAL para este caller** — respuesta definitiva a la pregunta
central del prompt, sin necesidad de instrumentación dinámica
adicional (cumpliendo la instrucción explícita de la Fase 7: "if v0 is
irrelevant... do NOT waste multiple runtime runs testing it").

## 7. Escrituras guest del original en una trama exitosa — HECHO parcial / INFERENCIA

No hay código MIPS original de `sceMpegGetPicture` disponible (sección
4) para confirmar HECHO puro. Por el USO que el caller hace de los
campos leídos (sección 9), se infiere que el original escribe, como
mínimo: `mpegAddr+0x00` (ancho), `mpegAddr+0x04` (alto),
`mpegAddr+0x08` (un campo que el caller trata como "estado/índice de
imagen", ver sección 9-11), y los píxeles decodificados en el buffer
`a1` (`imageAddr`).

## 8. Escrituras guest de la HLE actual

`MPEG.cpp:3132-3151`, HECHO por lectura directa:

```cpp
mpegGuestWrite32(rdram, mpegAddr + 0x00u, width);
mpegGuestWrite32(rdram, mpegAddr + 0x04u, height);
mpegGuestWrite32(rdram, mpegAddr + 0x08u, frameCount);  // = playback.picturesServed ANTES de incrementar
// + escrituras a *(mpegAddr+0x40) -> inner+0xb0/0xd8/0xe4/0xdc/0xe0 (offsets internos, no auditados en detalle en este prompt)
if (haveFrame) writeDecodedFrameToGuest(rdram, imageAddr, frame);       // píxeles reales
else if (frameCount == 0u) writeBlankMpegFrame(rdram, imageAddr, width, height);
setReturnS32(ctx, 0);
```

Tabla comparativa (sección requerida):

| EFFECT | ORIGINAL (inferido) | HLE ACTUAL |
|---|---|---|
| return v0 | 1 (observado dinámicamente, P3.8.1) / posiblemente distinto en otras condiciones (UNKNOWN) | 0 siempre |
| destino buffer (a1/imageAddr) | píxeles decodificados reales | píxeles decodificados reales (`writeDecodedFrameToGuest`) — estructuralmente correcto en el caso `haveFrame` |
| frameCount (`+0x08`) | UNKNOWN el valor exacto; INFERENCIA fuerte: no es un contador monótono de por-vida, ver sección 11 | `playback.picturesServed` ANTES de incrementar — monótono, 0 solo en el primer éxito de todo el proceso |
| width/height (`+0x00`/`+0x04`) | UNKNOWN valor exacto, pero el caller SÍ los consume (copia a `s0+0x7C`/`+0x80`) | escritos correctamente cada llamada |
| `inner+0xb0/0xd8/0xe4/0xdc/0xe0` (vía `*(mpegAddr+0x40)`) | UNKNOWN | escritos incondicionalmente cada llamada exitosa o no (líneas 3136-3147); no auditado en profundidad — FOLLOWUP |

## 9. Semántica de `mpegAddr+0x08`

**HECHO (por desensamblado directo del caller, código de juego real)**:
inmediatamente tras el `bgez`, el caller SIEMPRE lee
`v0 = *(mpegAddr+0x08)` (`0x47a900`) y bifurca con `bnez`:

- `== 0` → cae a `0x47a90c`: copia `width`/`height` a
  `*(s0+0x7C)`/`*(s0+0x80)` (variables locales de `MpegMovieDecode`),
  hace ajustes de flags de audio (`0x742158`), y — tras la lógica ya
  documentada de `audioDecStart`/`Resume` (sección 11.2 de la nota
  canónica, sin cambios) — llama **dos veces** a
  `Movie_set_loadimage3` (`0x47AAA0`) pasándole `width`/`height` como
  argumentos.
- `!= 0` → salta a `0x47aa18`: calcula un puntero indexado
  (`base=0x773480 + *(0x740760)*stride`) y llama **una vez** a
  `Movie_loadimage` (`0x3FE590`) con ese puntero como único argumento.

Ambos caminos confluyen después en `sceMpegIsEnd`/posible reinit —
sección 13.

**INFERENCIA fuerte** (no HECHO puro, por falta de código original):
dado que `Movie_set_loadimage3` construye por desensamblado un bucle
anidado ancho×alto con patrones de bits tipo GIFtag/registro GS
(sección 10), y `Movie_loadimage` es una función diminuta (40 bytes)
que escribe a direcciones fijas de bajo nivel `0x1000A000/A020/A030`
(patrón IOP/SIF-RPC, ver sección 10), la interpretación más plausible
es que `mpegAddr+0x08` distingue **"primera imagen de la sesión, hace
falta configurar el pipeline de subida GS completo"** (`==0`) de
**"imágenes subsiguientes, solo pedir la actualización liviana"**
(`!=0`) — probablemente NO un contador monótono de por-vida como
`picturesServed`, sino algo más cercano a "primer I-frame de un GOP" o
un flag de estado de sesión que el original resetea periódicamente. Se
declara explícitamente como **HIPÓTESIS, no HECHO** — requiere la
medición PCSX2 de la sección 17 para confirmarse.

## 10. Semántica de la imagen/`a1`

`a1` (`imageAddr`) es una dirección FIJA (`0x79D680`, idéntica en las
250+ llamadas exitosas observadas en P3.14.2, confirmado por los logs
`imageAddr=0x79d680` en cada línea `[MPEG:diag] getPicture success`) —
no varía por llamada. Es el destino directo de los píxeles decodificados
(`writeDecodedFrameToGuest`). No se encontró que este puntero se pase
explícitamente como argumento a `Movie_set_loadimage3` ni a
`Movie_loadimage` en la porción de argumentos rastreada (a0-a3,
t0-t3) — **UNKNOWN** si alguna de las dos funciones referencia
`imageAddr` internamente vía otro mecanismo (dirección fija/global no
identificada en este prompt) o si el layout de píxeles en `imageAddr`
es consumido por un camino totalmente distinto no cubierto por
`MpegMovieDecode`. No se completó el trazado interno de
`Movie_set_loadimage3` línea por línea buscando ese puntero — el cuerpo
es largo (0x2D4 bytes, bucle anidado) y esta iteración se detuvo tras
confirmar su naturaleza (construcción de paquete tipo GIF/GS) sin
verificar cada referencia de memoria.

## 11. Layout de imagen original vs HLE

No determinado en este prompt (requeriría trazar `Movie_set_loadimage3`
completo o medición PCSX2 — sección 10). **UNKNOWN**, followup
explícito.

## 12. Cadena guest de presentación tras `0x47A8EC`

`0x47A8EC` (bgez) → `0x47A900` (lee frameCount) → bifurca:
- rama `==0`: `0x47A90C`→...→ dos llamadas a `Movie_set_loadimage3`
  (`0x47A9E0` y `0x47AA0C`) — construcción de paquete GIF/GS
  width×height (INFERENCIA: subida de textura real).
- rama `!=0`: `0x47AA18`→`Movie_loadimage` (`0x47AA44`) — escritura de
  comando a direcciones fijas `0x1000A0xx` (INFERENCIA: petición
  IOP/SIF, no necesariamente relacionada con GS).

Ambas ramas confluyen en `0x47AA4C`→`0x47AA60`: llama
`sceMpegIsEnd(mpegAddr)`. Si `==0` (no terminó), **hace `goto
label_47a79c`** (vuelve al inicio de la función) — `MpegMovieDecode`
**contiene un bucle interno que llama a `sceMpegGetPicture` repetidas
veces dentro de una sola invocación**, hasta que `sceMpegIsEnd()`
devuelva verdadero, momento en el que reinicializa la sesión
(`sceMpegInit`) y retorna. **HECHO nuevo, no documentado en notas
previas** — explica por qué se observan cientos de éxitos de
`GetPicture` en ventanas de tiempo cortas (P3.14.2): no son cientos de
invocaciones separadas de `MpegMovieDecode`, sino un bucle interno
denso dentro de (probablemente) muy pocas invocaciones.

## 13. LAST COMMON NODE

Sin cambios respecto a P3.13/P3.14/P3.14.2: sigue siendo el estado
post-reset de `sceMpegInit` bajo `ownership`.

## 14. FIRST DIVERGENT NODE

Dentro del segmento ahora auditado (`0x47A8EC` en adelante), el primer
punto de divergencia CONFIRMADA no es el valor de retorno (sección 6)
sino la **semántica de `mpegAddr+0x08`** (sección 9): con el
comportamiento monótono actual de la HLE, `Movie_set_loadimage3`
(la única de las dos rutas con apariencia de subida real de textura
GS) se ejecuta **exactamente una vez en toda la sesión**, mientras que
si la hipótesis de la sección 9 es correcta, el original la ejecutaría
periódicamente (p. ej. una vez por GOP o por escena). Esto es
INFERENCIA fuerte, no HECHO confirmado — ver sección 17 para el
experimento que lo resolvería.

## 15. Clasificación de la discrepancia

**`GETPICTURE_GUEST_STATE_BUG`** (candidato principal, INFERENCIA
fuerte) — no `GETPICTURE_RETURN_BUG` (descartado con HECHO en la
sección 6).

## 16-20. Código, build, validación, visual

**No se implementó ningún cambio de código en este prompt.** Motivo
explícito: la única corrección con evidencia suficientemente sólida
(v0) resultó NO CAUSAL (sección 6), y la corrección con mayor
potencial causal (semántica de `mpegAddr+0x08`) depende de una
hipótesis (sección 9) todavía no confirmada — implementar una
"corrección" basada en una hipótesis no verificada arriesgaría
introducir una nueva desviación HLE que coincida visualmente por
casualidad, exactamente lo que el estándar de rigor de este proyecto
prohíbe. Sin build, sin corridas nuevas, sin captura visual nueva en
este prompt — se reutiliza la evidencia visual ya validada en P3.14.2
(sección 21).

## 21. Respuesta explícita: ¿es v0=0 vs v0=1 causal para los glitches observados?

**NO.** HECHO, por desensamblado directo (sección 3/6): el único
caller usa `bgez`, indistinguible entre 0 y 1.

## 22. Respuesta explícita: ¿DMC alcanza la misma ruta de presentación guest?

**PARCIALMENTE, con una discrepancia estructural identificada.** El
código llega exactamente al mismo punto de bifurcación (`0x47A900`)
que el original alcanzaría, y ambas ramas (`Movie_set_loadimage3` /
`Movie_loadimage`) SÍ se ejecutan con datos de la HLE (no se saltan
por completo). Pero, si la hipótesis de la sección 9 es correcta, la
FRECUENCIA relativa con la que se alcanza cada rama es incorrecta
(`Movie_set_loadimage3` una vez en vez de periódicamente) —
INFERENCIA, no HECHO.

## 23. Respuesta explícita: ¿se escribe la imagen decodificada en el layout guest esperado?

**PARCIALMENTE CONFIRMADO, con una laguna abierta.** El puntero
`imageAddr` es consistente sesión a sesión (HECHO) y recibe píxeles
reales vía `writeDecodedFrameToGuest` (HECHO, por lectura del código
HLE). Si ese layout coincide byte a byte con lo que `Movie_set_loadimage3`
/ el pipeline GS espera exactamente (stride, formato de píxel,
orientación) es **UNKNOWN** — no se completó el trazado de
`Movie_set_loadimage3` hasta encontrar dónde (si acaso) referencia
`imageAddr`.

## 24. Respuesta explícita: ¿hay evidencia que implique a OpenGL?

**NO EVIDENCE YET.** Ninguna de las funciones auditadas en este prompt
(`MpegMovieDecode`, `Movie_set_loadimage3`, `Movie_loadimage`) es parte
del backend de renderizado — son código de juego (construcción de
paquetes GS/IOP) que se ejecuta *antes* de que cualquier dato llegue al
backend. No se tocó ni se inspeccionó código de GS/OpenGL en este
prompt, conforme a la instrucción explícita.

## 25. Clasificación

**`GETPICTURE_GUEST_STATE_BUG`** (candidato principal — semántica de
`mpegAddr+0x08`), con `CONTRACT_MATCHES_WITH_CAVEATS` para el valor de
retorno `v0` (mismatch real pero no causal).

## 26. CHECKPOINT_DECISION

Verificación contra el gate del prompt: "COMMIT_RECOMMENDED only if
this prompt identifies AND corrects a proven guest-visible contract
mismatch and validates the change" — no se corrigió nada (sección
16-20), así que no aplica. "NO_COMMIT if v0 is proven irrelevant and no
other mismatch is found" — v0 SÍ se probó irrelevante, pero SÍ se
encontró otro mismatch candidato con buena fundamentación
(`mpegAddr+0x08`), así que tampoco aplica NO_COMMIT puro. Corresponde:

**CHECKPOINT_DECISION: EXPERIMENT_CHECKPOINT.**

Justificación: se estableció evidencia mayor y nueva (v0 descartado
con HECHO firme; bucle interno de `MpegMovieDecode` documentado por
primera vez; identificación con nombres reales de
`Movie_set_loadimage3`/`Movie_loadimage` y su naturaleza probable
GS-vs-IOP; hipótesis concreta y verificable sobre `mpegAddr+0x08`),
pero la implementación de una corrección real requiere confirmar esa
hipótesis primero — trabajo más grande, apropiado para un prompt de
seguimiento dedicado, no para forzar un cambio especulativo aquí.

## 27. Próximo prompt recomendado

No `P4.0 — GS_DISPLAY_AND_FRAMEBUFFER_CONTRACT_BASELINE` todavía — la
evidencia de este prompt apunta de vuelta al contrato
`GetPicture`↔`MpegMovieDecode` (`mpegAddr+0x08`), no río abajo en GS.
Se recomienda:

**P3.15.1 — MPEGADDR_0X08_ORIGINAL_SEMANTICS_MEASUREMENT**

con el siguiente bloque de acciones PCSX2 COMPLETO, mínimo, listo para
ejecutar por el usuario (no iniciado en este prompt, solo preparado):

1. Breakpoints de ejecución en:
   - `0x00109FF0` (entrada `sceMpegGetPicture`)
   - `0x0047A8EC` (retorno en `MpegMovieDecode`)
   - `0x0047AAA0` (entrada `Movie_set_loadimage3`)
   - `0x003FE590` (entrada `Movie_loadimage`)
2. Dejar correr hasta alcanzar **al menos 5-10 golpes** de
   `0x0047A8EC` (no solo el primero, a diferencia de la medición
   original de P3.8.1) — capturar en cada uno: `v0` (`$2`), `a0`
   (`$4`), `a1` (`$5`).
3. En cada golpe, leer memoria en `mpegAddr+0x08` (mpegAddr = valor de
   `a0` capturado en el golpe de entrada correspondiente) justo
   después del retorno.
4. Anotar, para cada uno de los golpes de `0x47A8EC`, si
   `0x0047AAA0` o `0x003FE590` se alcanzan antes del siguiente golpe
   de `0x47A8EC` (para confirmar directamente con qué frecuencia
   relativa se ejecuta cada rama).
5. Opcional si es fácil de capturar: 2-3 muestras de los primeros 64
   bytes en `imageAddr` (`a1` del primer golpe) en distintos momentos,
   para verificar si el contenido cambia call a call (confirma decode
   continuo real en el original).

## 28. Estado final de git

Main: sin cambios de código; solo este informe nuevo, sin trackear
(`?? BLOCKER_004_P315_GETPICTURE_PRESENTATION_CONTRACT.md`). Vendor:
limpio, sin tocar, HEAD `61a0977...` sin cambios.

## 29. Confirmaciones explícitas

- NO commit, NO push.
- NO clean/regenerate, NO build masivo de código generado.
- NO modificación de GS/OpenGL — ninguna de esas rutas se inspeccionó
  siquiera.
- NO trama ni éxito sintético — no se tocó ningún camino de decode.
- NO se modificó el modelo de consumo de `viBuf`, el ownership del
  decoder, el scheduler, el transporte CD, el orden de demux, ni
  `RuntimeGuestArena` — confirmado por `git status` (sin diffs en
  vendor).
