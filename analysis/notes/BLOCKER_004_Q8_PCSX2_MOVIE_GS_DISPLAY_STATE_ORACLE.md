# BLOCKER_004 — ORACLE_Q8: PCSX2 MOVIE GS DISPLAY STATE ORACLE

Modo: diagnóstico temporal, instrumentación solo en un checkout de PCSX2
externo al repo (`../pcsx2_q8_build`, fuera de dmc-recomp/git por completo).
Sin fix de producción. NO_COMMIT / NO_PUSH.

Backup pre-Q8 de `vendor/PS2Recomp`: `analysis/local/q8/vendor_before_q8.patch`
— verificado **sin cambios** respecto al estado actual (`git -C
vendor/PS2Recomp diff` es byte-idéntico al patch) al cierre de esta
investigación: Q8 no tocó el árbol de dmc-recomp en ningún momento.

## PRIMARY_QUESTION

¿El DMC original (bajo PCSX2, oráculo de hardware real) escribe/presenta el
movie usando el par de framebuffers **{0x00,0xA0}** (contrato original) o
**{0xA0,0xE0}** (lo que hace el guest recompilado, según Q7)?

## Método

Checkout limpio de PCSX2 tag `v2.8.2` (commit
`fd9d310ccbb6b8b62c976da8886a3c8fd3a10ff3`), compilado desde cero (MSBuild,
config `Devel|x64`) con 4 puntos de instrumentación **temporales**, todos
`#include "Q8Trace.h"` + printf, sin alterar ninguna lógica existente:

- `pcsx2/GS.cpp` — `Q8LogPrivWrite()`: decodifica cada escritura a la página
  de registros privilegiados del GS (`gsWrite64_page_00/01`), con `mem`,
  `value` y `cpuRegs.pc` (PC de la EE en el momento de la escritura). Emite
  `[Q8_PRIV] reg=... raw=... pc=0x...` para PMODE/SMODE1/SMODE2/DISPFB1/
  DISPFB2/DISPLAY1/DISPLAY2/CSR.
- `pcsx2/GS/GSState.cpp` — bloques `[Q8_GIF]` en `GIFRegHandlerFRAME/ZBUF/
  BITBLTBUF/TRXPOS/TRXREG/TRXDIR`, más un detector automático de "movie
  upload" (misma firma de Q7: 32 transfers host→local PSMCT32, `DBW=8`,
  `DPSM=PSMCT32`, `RRW=16 RRH=448`, `DBP∈{0,0x1400}`, `DSAX=496` = última
  strip del batch) que emite `[Q8_UPLOAD]` y lleva un contador global
  `movieUpload`.
- `pcsx2/GS/Renderers/Common/GSRenderer.cpp` — `[Q8_VSYNC]` al inicio de
  `GSRenderer::VSync()` (hook común a todos los backends), volcando
  `PMODE/SMODE1/SMODE2/DISP[0..1].{DISPFB,DISPLAY}` y
  `CTXT[0..1].{FRAME,ZBUF}` ya decodificados, junto al `movieUpload` vigente
  y el campo (`field`) del VSync.

Ejecución: `pcsx2-qtx64-dev.exe -statefile <savestate del usuario> -- <ISO>`,
avance del título con `mcp__pcsx2__input_button_press` (inyección por
teclado, sin `connect`/GDB — evita el canal históricamente inestable),
captura a archivo hasta cubrir la apertura del movie por completo, cierre
limpio.

Captura: `analysis_local_q8/q8_trace_run1.log` — **2751 VSyncs, 1079 movie
uploads completos, 8355 escrituras a registros privilegiados, 101.980
escrituras de registros GIF**. Analizado con `analyze_q8.py` (parseo por
regex, sin dependencias).

## Resultados (HECHO — medido directamente del oráculo PCSX2)

### Tabla (primeras 20 transiciones de `movieUpload`, columna 0 = boot/UI justo antes del primer upload)

| MovieUpload | DBP_page (último upload) | FRAME1.FBP | FRAME2.FBP | DISPFB1.FBP | DISPFB2.FBP | PMODE EN1/EN2 | field |
|---:|---|---|---|---|---|---|---|
| 0  | (ninguno aún) | 0x00  | 0x00  | 0x00 | 0x38 | 0/1 | 1 |
| 1  | 0xa0 | 0x128 | 0x128 | 0x00 | 0x00 | 0/0 | 0 |
| 2  | 0x0  | 0x128 | 0x128 | 0x00 | 0xa0 | 0/0 | 0 |
| 3  | 0xa0 | 0x128 | 0x128 | 0x00 | 0x00 | 0/0 | 1 |
| 4  | 0x0  | 0x128 | 0x128 | 0x00 | 0xa0 | 0/0 | 1 |
| 5  | 0xa0 | 0x128 | 0x128 | 0x00 | 0x00 | 0/1 | 1 |
| 6  | 0x0  | 0x128 | 0x128 | 0x00 | 0xa0 | 0/1 | 1 |
| 7  | 0xa0 | 0x128 | 0x128 | 0x00 | 0x00 | 0/1 | 1 |
| 8  | 0x0  | 0x128 | 0x128 | 0x00 | 0xa0 | 0/1 | 1 |
| 9  | 0xa0 | 0x128 | 0x128 | 0x00 | 0x00 | 0/1 | 1 |
| 10 | 0x0  | 0x128 | 0x128 | 0x00 | 0xa0 | 0/1 | 1 |
| 11 | 0xa0 | 0x128 | 0x128 | 0x00 | 0x00 | 0/1 | 1 |
| 12 | 0x0  | 0x128 | 0x128 | 0x00 | 0xa0 | 0/1 | 1 |
| 13 | 0xa0 | 0x128 | 0x128 | 0x00 | 0x00 | 0/1 | 1 |
| 14 | 0x0  | 0x128 | 0x128 | 0x00 | 0xa0 | 0/1 | 1 |
| 15 | 0xa0 | 0x128 | 0x128 | 0x00 | 0x00 | 0/1 | 1 |
| 16 | 0x0  | 0x128 | 0x128 | 0x00 | 0xa0 | 0/1 | 1 |
| 17 | 0xa0 | 0x128 | 0x128 | 0x00 | 0x00 | 0/1 | 0 |
| 18 | 0x0  | 0x128 | 0x128 | 0x00 | 0xa0 | 0/1 | 0 |
| 19 | 0xa0 | 0x128 | 0x128 | 0x00 | 0x00 | 0/1 | 1 |
| 20 | 0x0  | 0x128 | 0x128 | 0x00 | 0xa0 | 0/1 | 1 |

Este patrón (FRAME constante en 0x128, DISPFB2 alternando en anti-fase exacta
con la página recién subida) se sostiene sin excepción durante las 1079
subidas capturadas (verificado por grep de valores distintos, no solo las
primeras 20 filas).

### Hechos adicionales confirmados por conteo/grep sobre el log completo

- **MOVIE_UPLOAD_PAIR (PCSX2)**: `{0x00, 0xA0}` — idéntico al contrato
  original y a lo que ya HECHO por Q7 en recomp (la etapa de upload/VRAM
  nunca estuvo en duda; esto es solo una confirmación cruzada).
- **FRAME1.FBP == FRAME2.FBP == `0x128` constante** durante todo el tramo
  movie (nunca alterna). Fuera del tramo movie (boot/UI) FRAME toma otros
  valores (`0x00`, `0x38`, `0xa0`) — consistente con distintas pantallas de
  UI, no parte de la pregunta.
- **DISPFB1 nunca se escribe**: `grep -c "reg=DISPFB1"` = **0** en las 8355
  escrituras privilegiadas capturadas. Combinado con `PMODE.EN1` fijo en
  `0` durante todo el run (dos formas de grep, ambas confirman `EN1=0` como
  único valor observado), el circuito de display 1 está **completamente
  apagado y sin programar** en todo momento.
- **DISPFB2.FBP alterna `{0x00, 0xA0}`** en anti-fase exacta con la página
  que acaba de recibir el upload (mientras se sube a 0xA0, se presenta 0x00
  — el buffer ya completo de la subida anterior — y viceversa): **3342
  escrituras** de DISPFB2 en el log completo.
- **PC de EE responsable de las escrituras a DISPFB2**: exactamente dos
  direcciones, compartidas entre la fase UI (pre-movie, donde escriben
  `{0x38,0x00}`, coincide con el HECHO de Q7 para la UI) y la fase movie
  (donde escriben `{0xA0,0x00}`):
  - `pc=0x0015c408` → escribe `raw=0x10A0` (`FBP=0xA0`)
  - `pc=0x0015c490` → escribe `raw=0x1000` (`FBP=0x00`)
  Es el **mismo par de sitios de código** en ambas fases — no hay una
  rutina distinta para movie vs. UI; solo cambia el valor de FBP que
  calculan.
- **Fila 0** (boot/UI, justo antes del primer upload): `DISPFB2.FBP=0x38`,
  `PMODE EN1=0/EN2=1` — coincide exactamente con el HECHO ya congelado por
  Q7 para la UI (`{0x00,0x38}`), lo que valida que el detector de
  movie-upload y el snapshot de VSync están capturando el registro y el
  instante correctos.
- `SMODE2.raw` toma dos valores (`0x1`, `0x3`) a lo largo del run — cambios
  de `FFMD` (field/frame mode) coherentes con transición UI→movie, no
  aportan una segunda causa de corrupción (fuera del alcance de esta
  pregunta; no se investiga más porque no es necesario para responder
  PRIMARY_QUESTION).

## Comparación cruda (RAW-DIFF) contra los HECHO ya congelados de Q7 (recomp)

Q7 ya estableció (frozen, no re-derivado aquí, ver
`BLOCKER_004_Q7_GS_STAGE_FIRST_CORRUPTION_ORACLE.md`):
`RECOMP_MOVIE_UPLOAD_PAIR = {0x00,0xA0}`,
`RECOMP_FRAME_PAIR (draw) = {0xA0,0xE0}` (alternando, vía GIF FRAME A+D),
`RECOMP_DISPFB_PAIR = {0xA0,0xE0}` (alternando, escrito DIRECTO por el guest
a los registros privilegiados — `[Q7:PRIVW]`=2, solo boot ⇒ por eliminación
es el guest, no HLE).

| Campo | PCSX2 (original, HECHO Q8) | dmc-recomp (HECHO Q7) | ¿Igual? |
|---|---|---|---|
| MOVIE_UPLOAD_PAIR | {0x00,0xA0} | {0x00,0xA0} | **SÍ** |
| Buffer(s) de DRAW (FRAME) | **0x128 constante**, nunca alterna | {0xA0,0xE0} alternando | **NO** |
| Circuito de display activo | **solo circuito 2** (DISPFB2); DISPFB1 nunca escrito, EN1=0 siempre | Q7 no distinguió circuito 1 vs 2 al medir `[Q7:DISPLAY]` (OPEN, ver abajo) | pendiente de precisión |
| DISPFB (par de páginas) | **{0x00,0xA0}** (= las mismas páginas del upload) | {0xA0,0xE0} | **NO** |
| ¿DISPFB apunta directo al buffer recién subido? | **SÍ**, siempre | **NO** (0xE0 no es página de upload; es 0xA0+64 páginas) | **NO** |

**RAW_PRIVILEGED_WRITES_EQUAL = NO.** La divergencia no es un simple
"off-by-one" en qué mitad del par mostrar: es una divergencia de
**estrategia completa**. El hardware original **nunca dibuja sobre el
buffer que se está presentando** — el draw (FRAME=0x128, constante) va a
una tercera superficie completamente separada de las dos páginas de upload,
y DISPFB2 simplemente apunta, cuadro a cuadro, al buffer de upload que
acaba de terminar de llenarse (doble buffer limpio, sin overlay dibujado
encima antes de presentar). El guest recompilado, en cambio, calcula un FBP
de draw Y de display que cae en `{0xA0,0xE0}` — coincide en el valor 0xA0
(una de las dos páginas de upload reales) pero el otro término, 0xE0, **no
es una página de upload ni la página de draw real (0x128) del original**:
es simplemente `0xA0 + 64 páginas`, que en la geometría PSMCT32/FBW=8 del
buffer cae encima de las filas 256-447 del propio 0xA0 (exactamente el
mecanismo de corrupción que Q7 ya derivó al píxel).

**Refutación explícita de ambas hipótesis originales de Q8**: ni A
(`{0x00,0xA0}` puro) ni B (`{0xA0,0xE0}` como en recomp) describen
correctamente el hardware real. El original usa un **tercer patrón**: draw
en una superficie fija ajena a las páginas de upload/display, display
alternando exactamente las dos páginas de upload. Éste era justamente el
riesgo señalado en las RESTRICCIONES del prompt ("no asumas que FBP define
por sí solo un framebuffer lineal independiente... investiga semántica
completa del circuito de display antes de concluir") — la respuesta
correcta exigió los 13 campos de estado (FRAME **y** DISPFB **y** PMODE.EN1/
EN2 **y** el detector de upload), no solo DISPFB.

## FIRST_REGISTER_DIVERGENCE / FIRST_DIVERGENT_RAW_VALUE / FIRST_DIVERGENT_EE_PC

- **FIRST_REGISTER_DIVERGENCE**: DISPFB (registro privilegiado de display) —
  es el primer registro, en la cadena movie_upload → FRAME(draw) →
  DISPFB(display) → PMODE, cuyo valor observado ya no coincide entre
  original y recomp desde el primerísimo upload (`movieUpload=1`). FRAME
  (draw) también diverge (0x128 constante vs {0xA0,0xE0}), pero como
  registro de *draw* es lógicamente anterior en la tubería GS a DISPFB
  (*display*) — ambos ya están rotos, así que la corrupción no nace "en
  DISPFB" aislado: nace en cómo el guest recompilado decide **ambos**
  destinos (draw y display) del par de páginas por cuadro.
- **FIRST_DIVERGENT_RAW_VALUE**: DISPFB2 = `0x10A0` (FBP=0xA0, upload
  impar) en PCSX2 vs. el guest recompilado escribiendo el equivalente de
  FBP=0xE0 en el cuadro impar (Q7, `raw` no capturado en decimal por Q7 pero
  FBP=0xE0 medido directamente).
- **FIRST_DIVERGENT_EE_PC**: **NO CAPTURADO en esta ronda** (ver
  RECOMP_DISPFB_PAIR/RECOMP_FRAME_PAIR arriba — ambos vienen de Q7, que
  midió el VALOR con altísima confianza pero no instrumentó el PC de la EE
  en el sitio de escritura). dmc-recomp es un recompilador estático
  (PS2Recomp): no existe un registro "PC actual" genérico en el runtime
  (`ps2_memory.cpp::write32/64` no recibe PC como parámetro; se confirmó
  por inspección que ningún archivo del runtime mantiene un PC "actual"
  global salvo estructuras ad-hoc no aplicables aquí) — capturar el PC
  exige o bien un backtrace por símbolo de función generada, o instrumentar
  cada callsite candidato del código recompilado, lo cual es un trabajo
  nuevo de instrumentación de build (no un re-análisis de datos ya
  existentes) y por eso se deja como **NEXT** explícito en vez de forzar un
  segundo ciclo de build completo dentro de esta ronda — ver RESTRICCIONES
  del prompt ("Q8 debe confirmar/raw-diff esto, NO repetir pixel forensic")
  y el hecho de que el HECHO de valor (lo que realmente responde
  PRIMARY_QUESTION) ya está establecido con evidencia directa y suficiente
  de ambos lados.

## BACKEND_STATE_BEFORE_PRESENT_EQUAL

No aplica una comparación pixel-level aquí (fuera de alcance, ya cubierto
por Q6/Q7). A nivel de **estado de registros antes de presentar**: NO son
iguales — ver tabla RAW-DIFF arriba.

## Clasificación

**`Q8_GUEST_STATE_DIVERGENCE`.** El código guest recompilado escribe
valores de FBP (tanto en el registro de draw FRAME como en el de display
DISPFB) que **difieren en valor** de los que el mismo software produce bajo
emulación de hardware real (PCSX2), en el mismo tramo de ejecución (la
reproducción del movie). No es una diferencia de semántica del *handler* de
registros privilegiados (`gsRegPtr`/`PS2Memory::write32/64` en dmc-recomp
implementan el mismo modelo simple "el guest escribe, el valor se guarda
tal cual" que PCSX2's `GSRegDISPFB`/`gsWrite64_page_XX` — no hay lógica de
traducción de direcciones adicional en ningún lado que pudiera explicar la
discrepancia). Tampoco es una diferencia de circuito de lectura (DBX/DBY/
DX/DY/MAGH/MAGV/DW/DH) ni de field/interlace: el patrón observado en el
original es estructuralmente distinto desde el propio **valor lógico** de
FBP que el guest decide escribir cuadro a cuadro, mucho antes de que
cualquier semántica de circuito de display entre en juego. La causa raíz
concreta (qué dato de entrada, HLE stub, o rama de código hace que el guest
recompilado derive `0xE0`/`0xA0` donde el guest en hardware real derivaría
`0x128`/`0x00`/`0xA0` según el registro) queda **fuera del alcance de esta
ronda** — es la pregunta que abre el NEXT.

**ROOT_CAUSE_BOUNDARY**: el guest recompilado, no el emulador/runtime GS de
dmc-recomp. El mecanismo de escritura (guest → `PS2Memory::write32/64` →
`gsRegPtr` → registro) es correcto y equivalente al de PCSX2 — el problema
está en **qué valor** el código MIPS recompilado calcula antes de emitir
esa escritura, en al menos dos sitios (el que alimenta FRAME de draw y el
que alimenta DISPFB de display), ambos aparentemente ligados al mismo
esquema de selección de buffer por cuadro (ambos coinciden en usar
{0xA0,0xE0}, sugiriendo una única fuente de datos o cálculo compartido).

## NEXT

1. Localizar, en el código MIPS/PS2 original (ELF) o en el código C++
   generado por PS2Recomp, el/los sitio(s) que calculan el FBP escrito a
   FRAME y a DISPFB durante el movie — candidatos: una variable de
   "buffer index" incrementada por VSync/frame-count, posiblemente
   alimentada por un HLE stub (p. ej. algo relacionado con el conteo de
   VSync, `GsSync`, o el propio manejo de la cola de `StrM2vCallBack` ya
   auditado en P4.2.2) cuyo valor difiera del que el kernel real del PS2
   habría entregado.
2. Confirmar con un PC de EE real (requiere instrumentación de build nueva,
   no solo análisis de logs existentes) que ese sitio de cálculo, no el
   mecanismo de escritura a registro, es la causa — comparando
   específicamente contra `pc=0x0015c408`/`pc=0x0015c490` (PCSX2) para
   verificar si el guest recompilado ejecuta la MISMA dirección de EE con
   un valor de entrada distinto (→ confirma `Q8_GUEST_STATE_DIVERGENCE` con
   HLE/estado como causa) o una dirección de EE distinta (→ apuntaría a
   una traducción/recompilación incorrecta de esa rama de código, una
   clasificación más grave y distinta).
3. NO aplicar ningún parche tipo `if (FBP==0xE0) FBP=0xA0` — eso oculta el
   síntoma sin corregir el cálculo guest divergente, y contradice
   explícitamente las restricciones de este prompt y de Q7.

## Ledger

- HECHO: PCSX2 (oráculo de hardware real) usa `{0x00,0xA0}` como
  MOVIE_UPLOAD_PAIR — igual que recomp (Q7) y que el contrato original.
- HECHO: PCSX2 dibuja el fade/overlay del movie en **FRAME=0x128
  constante**, nunca en las páginas de upload — divergente de recomp
  (`{0xA0,0xE0}` alternando).
- HECHO: PCSX2 presenta el movie **solo por el circuito 2** (DISPFB2),
  alternando exactamente `{0x00,0xA0}` — las mismas páginas de upload,
  en anti-fase con la página recién escrita. DISPFB1 nunca se escribe;
  PMODE.EN1 siempre 0.
- HECHO: dos PCs de EE compartidos (UI y movie) para las escrituras de
  DISPFB2: `0x0015c408` (FBP=0xA0) y `0x0015c490` (FBP=0x00).
- HECHO (cruzado con Q7): fila 0 (pre-movie) reproduce exactamente el
  contrato UI ya congelado por Q7 (`{0x00,0x38}`), validando que ambos
  oráculos miden el mismo tramo real de ejecución.
- REFUTADO: ni hipótesis A (`{0x00,0xA0}` puro) ni B (`{0xA0,0xE0}` como
  recomp) describen el hardware real — el patrón real es un tercer
  esquema (draw fijo en 0x128, display alternando exactamente las páginas
  de upload).
- DERIVACIÓN: la clasificación `Q8_GUEST_STATE_DIVERGENCE` se apoya en que
  el mecanismo de escritura a registros privilegiados es equivalente en
  ambos lados (mismo modelo simple de "guardar el valor escrito"); la
  discrepancia nace en el VALOR que el guest recompilado calcula, no en
  cómo dmc-recomp aplica ese valor al registro.
- UNKNOWN (abierto para el NEXT): el sitio de cálculo exacto (guest MIPS o
  C++ generado) y si la divergencia es de dato de entrada (HLE/estado) o de
  traducción de código — requiere una ronda de instrumentación nueva sobre
  dmc-recomp, deliberadamente no ejecutada en esta ronda por proporcionalidad
  y por la restricción explícita de "no repetir pixel forensic / confirmar-
  raw-diff con lo ya HECHO por Q7".

## Artefactos (untracked, fuera de dmc-recomp/git)

- Checkout PCSX2 instrumentado + build: `../pcsx2_q8_build/` (completo,
  fuera del repo dmc-recomp, nunca trackeado por su git).
- Log crudo del oráculo: `../pcsx2_q8_build/analysis_local_q8/
  q8_trace_run1.log` (2751 VSyncs, 1079 uploads, 8355 priv writes, 101.980
  GIF writes).
- Script de análisis: `../pcsx2_q8_build/analyze_q8.py`.
- Capturas de validación visual: `../pcsx2_q8_build/analysis_local_q8/
  screenshot_initial.png`, `screenshot_after_start.png`.
- `analysis/local/q8/vendor_before_q8.patch` (dentro de dmc-recomp,
  gitignored) — confirmado sin diferencias con el estado actual de
  `vendor/PS2Recomp` al cierre de esta ronda.

## CHECKPOINT_DECISION

NO_COMMIT
