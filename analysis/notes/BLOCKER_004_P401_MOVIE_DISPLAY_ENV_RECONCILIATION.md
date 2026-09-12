# BLOCKER_004 — Prompt P4.0.1: ORIGINAL_VS_RECOMP_MOVIE_DISPLAY_ENV_RECONCILIATION

Investigación narrow, sin commit. No reescribe P4.0F — lo corrige con un
ledger explícito de retractaciones/confirmaciones.

## 1. Estado de entrada

Main HEAD `6beacfbfb1795813cb34ced0fd7e723b435016f3` ✓. Vendor HEAD
`61a0977924148e018d66d42c30d7756db8084541` ✓, limpio (`git diff` vacío,
verificado). Dirt esperado y confirmado: `?? BLOCKER_004_P40_FABLE_...md`,
`?? analysis/tools/p40/`. Sin dirt ajeno.

## 2. Evidencia PCSX2 original nueva (aceptada tal cual, medición directa)

```
Sample A: PMODE=0xFF66  DISPFB1=0 DISPLAY1=0  DISPFB2=0x10A0
Sample B: PMODE=0xFF66  DISPFB1=0 DISPLAY1=0  DISPFB2=0x1000
```

Breakpoints en `MainSetRect_movie`/`MainResetRect_movie`/
`R002_movie_sub_title_disp_01` y watch condicional `(v0&1)!=0` en los
sitios de escritura de PMODE: **no dispararon**.

## 3. Retractaciones formales (ledger)

| Claim P4.0F | Estado |
|---|---|
| "PMODE.EN1=0 es la primera divergencia" | **RETRACTADO.** `0xFF26` y `0xFF66` tienen AMBOS bit0(EN1)=0 (0x26=binario ...00110, 0x66=...01100110 — bit0 par en ambos). EN1=0 en original Y en RECOMP: no hay divergencia en EN1, nunca la hubo. |
| "El movie scanout requiere DISPFB1/circuito1" | **RETRACTADO.** Original: DISPFB1=DISPLAY1=0 durante película visible correcta. Coincide con RECOMP (circuito 1 no participa en ninguno de los dos lados). |
| "MoveImage2→0x250000 es la superficie de scanout del film" | **RE-EVALUADO, sin nueva evidencia que lo confirme o refute en este prompt** — dado que EN1=0 en ambos lados, 0x250000 (alcanzable solo vía DISPFB1) sigue sin ser escaneado en NINGUNO de los dos lados; su rol exacto (¿subtítulos únicamente, como ya apuntaba P4.0F §15.13?) permanece INFERENCIA, no re-auditado aquí. |
| "El runtime de presentación sigue fielmente el estado guest" | **Sigue siendo válido** — no se encontró evidencia en contra; de hecho esta auditoría refuerza que el runtime lee y aplica literalmente el env struct (sección 7). |
| "OpenGL no está implicado" | **Sigue siendo válido** — la divergencia encontrada en este prompt está en código HLE de la syscall `sceGsSetDefDBuffDc` (`GS.cpp`), no en el backend de renderizado. |

## 4. Diferencia de PMODE decodificada correctamente

`0xFF26` vs `0xFF66`: bit6 (AMOD) difiere — 0 en RECOMP, 1 en original.
Verificado bit a bit contra `makePmode()` (`Support.h:1599-1608`,
`amod` en bit6). **No causal para si el film se ve o no**: AMOD solo
afecta la fuente de mezcla alfa cuando hay dos circuitos activos; con
EN1=0 en ambos lados no hay circuito 1 con el que mezclar. Se documenta
como discrepancia real y sin explicar (`FIRST_UNPROVEN_NODE` menor),
no promovida a causal.

## 5. Identidad del movie (Fase 1)

RUN_043 y RUN_029 usan el mismo `pad.txt`/secuencia de teclas
(ENTER≈10s, X≈14s) y alcanzan los mismos milestones
(`MC_CHECK`→`MOVIE_START`≈69-71s→`PSS_REACHED`→`MPEG_INIT`), patrón
idéntico al usado en TODAS las corridas de esta sesión desde P3.11 —
**INFERENCIA fuerte, no HECHO puro**: es la misma primera película PSS
post-boot (pantalla de advertencia PAL) en ambas. **SAME_MOVIE_CONFIRMED
respecto a la medición manual del usuario en PCSX2: UNKNOWN** — no hay
forma de verificar desde este lado qué película exacta cargó el
usuario manualmente. **Esto NO invalida el hallazgo central** (sección
7-9): la función responsable (`MainGsSetDefDBuffDc`/
`sceGsSetDefDBuffDc`) se ejecuta UNA VEZ desde `Main_init`, no es
específica de ninguna película — su resultado es compartido por toda
la sesión, independientemente de cuál película se esté reproduciendo.

## 6. Timing de RUN_029 (Fase 2)

**Clasificación: ACTIVE_GS_STATE, no PENDING/STALE.** Verificado
directamente: los valores en `0x740C30` (PMODE=0xff26,
DISPFB2={0x10E0,0x10A0}) leídos del dump de RUN_029 (capturado en el
primer `GetPicture` exitoso, ~69s) son **idénticos, byte a byte**, a
los 180 muestreos de RUN_043 tomados a lo largo de TODA la ventana de
reproducción de la película (una corrida separada, posterior). Esto
descarta que RUN_029 capturara un estado transitorio/pendiente: el
struct es estable desde antes del primer frame hasta el final de la
ventana observada — consistente con ser escrito UNA sola vez en
`Main_init`, nunca modificado después. Resuelve el "conflicto
histórico" del prompt: no hay two snapshots contradictorios del mismo
lado — RUN_029 y RUN_043 son el MISMO RECOMP, coherentes entre sí. El
conflicto real es RECOMP-vs-original-vivo, no RECOMP-vs-RECOMP.

## 7. Rastreo de la construcción del environment (Fases 3-4, HECHO por lectura directa)

Cadena completa, verificada por desensamblado + bytes crudos del ELF +
código fuente HLE actual (no interpretación):

1. **`Main_init`** llama `MainGsSetDefDBuffDc(a0=0x740C30, ...)`
   (código de juego real, sin reemplazo HLE).
2. **`MainGsSetDefDBuffDc`** (`0x15c6a0`) llama a la syscall HLE
   `sceGsSetDefDBuffDc` (vía `jal func_101B48`) pasándole un buffer
   scratch en pila (`sp+0x60`), luego hace una serie de `memcpy`
   copiando esa estructura scratch hacia el destino final `s0`(=`a0`
   =`0x740C30`) en offsets específicos.
3. **`sceGsSetDefDBuffDc`** (HLE, `GS.cpp:869-938`) calcula:
   ```cpp
   uint32_t zbufAddr = ...;              // vía sceGszbufaddr()
   const uint32_t fbp1 = zbufAddr;
   const uint64_t dispfb0 = makeDispFb(fbp1, fbw, psm, 0u, 0u);  // usa zbufAddr
   const uint64_t dispfb1 = makeDispFb(0u,   fbw, psm, 0u, 0u);  // usa 0 literal
   db.disp[0].dispfb = dispfb0;
   db.disp[1] = db.disp[0];
   db.disp[1].dispfb = dispfb1;
   ```
   `GsDBuffDcMem` (`Support.h:1552-1563`, `static_assert` de tamaño
   confirmado): `disp[0]` en offset 0, `disp[1]` en offset 0x28
   (`GsDispEnvMem`=40 bytes exactos: pmode,smode2,dispfb,display,
   bgcolor).
4. Los `memcpy` de `MainGsSetDefDBuffDc` copian **exactamente** 0x28
   bytes desde el inicio del scratch (`disp[0]` completo) a `s0+0x0`, y
   0x28 bytes desde `scratch+0x28` (`disp[1]` completo) a `s0+0x38` —
   coincide byte a byte con los offsets que `MainGsSwapDBuffDc` lee
   (sección 8). **Confirma con certeza**: `which=0` (registro en
   `0x740C30`) = `disp[0]`; `which=1` (`0x740C68`) = `disp[1]`.
5. **`MainSetPalMovieEnv`** (`0x15cb70`, llamada después desde
   `Main_init`) opera sobre el MISMO struct base `0x740C30`, pero
   **solo toca**: bit EN1 de ambos PMODE (fuerza 0 en los dos, ya
   coincidía), bit MMOD (fuerza 1 en los dos), ALP=0xFF (ambos),
   DISPFB1=0x1118 (ambos, offsets +0x28/+0x60 — **no** +0x10/+0x48),
   un campo de 64 bits en +0x30/+0x68 (no identificado como
   DISPFB/PMODE/DISPLAY, probablemente BGCOLOR o reservado), y los
   campos de la película en +0x360/+0x3E0 (FBP=0x128, la superficie
   0x250000 — struct DIFERENTE y mucho más grande, el paquete GS de
   `Main_draw_ot_movie`, no el "which" record pequeño). **`MainSetPalMovieEnv`
   NUNCA escribe DISPFB2 (+0x10 ni +0x48).**

## 8. Confirmación de `MainGsSwapDBuffDc` (Fase 6, HECHO)

Desensamblado completo (`0x15c300-0x15c584`). Dos caminos según `a1`
("which"):

```
which==0: PMODE = *(s2+0x00) -> Store64(0x12000000, ...)   [0x15c498]
          DISPFB2 = *(s2+0x10) -> Store64(0x12000090, ...)
which!=0: PMODE = *(s2+0x38) -> Store64(0x12000000, ...)   [0x15c410]
          DISPFB2 = *(s2+0x48) -> Store64(0x12000090, ...)
```

`s2 = a0` (el puntero de env struct recibido directamente, sin
transformación) — **literal, no computado.** Confirma Fase 7
(`Store64`/escritura de registro privilegiado literal desde RAM guest)
y Fase 13 (esta función copia el struct byte a byte a los registros
privilegiados, sin ningún cálculo intermedio).

## 9. El divergente exacto (Fase 4, hallazgo central)

**HECHO (RAM guest, verificado independientemente byte a byte, RUN_029
Y RUN_043 coinciden)**: RECOMP tiene `disp[0].dispfb` (which=0,
0x740C40) = `0x10E0` (FBP=0xE0), `disp[1].dispfb` (which=1, 0x740C78)
= `0x10A0` (FBP=0xA0).

**HECHO (medición PCSX2 original, nueva)**: original tiene un slot en
FBP=0x000 y el otro en FBP=0xA0 — **el slot FBP=0xA0 coincide
exactamente entre original y RECOMP; el slot divergente es
0x000(original) vs 0xE0(RECOMP)**.

**HECHO (código fuente HLE, `GS.cpp:906-908`)**: `sceGsSetDefDBuffDc`
calcula `disp[1].dispfb` con **literal `0u`** (limpio, sin
ambigüedad), pero `disp[0].dispfb` con **`fbp1 = zbufAddr`** (el
resultado de `sceGszbufaddr()`, NO un literal limpio como su gemelo).
Esta asimetría en el propio código HLE es real y verificable —
`disp[1]` usa un patrón "0 por defecto" mientras `disp[0]` reutiliza la
dirección del Z-buffer.

**INFERENCIA fuerte, no cerrada numéricamente en este prompt**: dado
que el slot que diverge (which=0/`disp[0]`) es precisamente el que la
HLE alimenta con `zbufAddr` en vez de un literal limpio, y que
`0xE0` coincide con el FBP del Z-buffer del fade que P4.0F ya había
establecido independientemente (§6 de P4.0F: "ZBUF (fade) | 0xE0"), la
hipótesis con más evidencia a favor es: **la implementación HLE de
`sceGsSetDefDBuffDc` usa por error la dirección del Z-buffer para
`disp[0].dispfb`, cuando el SDK real casi con toda certeza inicializa
AMBOS slots a FBP=0 por defecto** (exactamente como ya hace
`disp[1]`) — el slot `0xA0` compartido por ambos lados se explicaría
entonces por una personalización POSTERIOR compartida (fuera del
alcance de `sceGsSetDefDBuffDc`, no identificada en este prompt — ver
UNKNOWN abajo) que sí coincide entre original y RECOMP, mientras que
el slot `disp[0]` — nunca personalizado después, según lo trazado en
la sección 7 — conserva el valor por defecto: 0 en el original, y
`zbufAddr` (bug) en RECOMP.

**UNKNOWN, no cerrado**: qué mecanismo produce el valor compartido
`0xA0` en el slot `disp[1]`/which=1 (ya que el código fuente de
`sceGsSetDefDBuffDc` por sí solo produciría literal 0 ahí, no 0xA0);
tampoco se re-derivó numéricamente en este prompt el valor exacto que
`sceGszbufaddr()` devuelve en esta ejecución concreta (no se hizo una
corrida nueva ni se instrumentó — ver Fase 12, no fue necesario para
la conclusión central).

## 10. Re-evaluación de `MoveImage2`→0x250000 (Fase 9)

Sin cambios respecto a P4.0F: dado que EN1=0 en ambos lados (sección
3), 0x250000 (alcanzable solo vía DISPFB1/circuito1) sigue sin
participar del scanout en NINGUNO de los dos lados según la evidencia
disponible. No se re-audita su rol exacto (¿subtítulos únicamente?) en
este prompt — la pregunta de P4.0F sigue abierta, sin nueva evidencia
en ningún sentido.

## 11. Re-evaluación del artefacto visual (Fase 10)

**Reforzada, no solo re-evaluada**: dado que el ÚNICO framebuffer que
realmente diverge entre original y RECOMP es el slot "0" (0 vs 0xE0),
y que el slot "0xA0" es COMPARTIDO/correcto en ambos lados, la
arquitectura real coincide con la sugerida por el prompt: cuando
DISPFB2 apunta al slot 0xA0 (compartido, correcto), RECOMP y original
deberían verse igual de "correctos" en ese frame; cuando DISPFB2 apunta
al slot divergente, RECOMP escanea 0x1C0000 (el Z-buffer del fade,
contenido ajeno) mientras el original escanea 0x000000 (posiblemente
un slot limpio/de subida cruda, o simplemente negro por defecto — no
determinado). Esto explica de forma más precisa el patrón de
bandas/fantasmas alternantes ya documentado por P4.0F §7, ahora
atribuido a una causa RAÍZ concreta en vez de "accidente de
solapamiento" genérico.

## 12. Tabla diferencial requerida (Fase 14)

| Node | Original | RECOMP | Status | Evidencia |
|---|---|---|---|---|
| Identidad de película | 1ª PSS post-boot (asumida) | 1ª PSS post-boot | INFERENCIA | patrón pad.txt idéntico, no probado 1:1 |
| Env struct address | `0x740C30` (asumido, no medido directamente en PCSX2) | `0x740C30` | INFERENCIA (original) / HECHO (RECOMP) | RAM dump RECOMP; original solo registros privilegiados |
| PMODE which=0 (disp[0]) | UNKNOWN exacto (no medido cuál slot=which0) | `0xFF26` | PARCIAL | — |
| PMODE which=1 (disp[1]) | UNKNOWN exacto | `0xFF26` | PARCIAL | — |
| PMODE activo medido | `0xFF66` | `0xFF26`/`0xFF24` | **MISMATCH (bit6/AMOD, no causal)** | HECHO ambos lados |
| EN1 (bit0 de PMODE) | 0 | 0 | **MATCH** | HECHO ambos lados — RETRACTA P4.0F |
| DISPFB2 which=0/disp[0] | `0x000` (FBP=0) | `0x10E0` (FBP=0xE0) | **MISMATCH — divergente primario** | HECHO ambos lados |
| DISPFB2 which=1/disp[1] | `0x10A0` (FBP=0xA0) | `0x10A0` (FBP=0xA0) | **MATCH** | HECHO ambos lados |
| `sceGsSetDefDBuffDc` fuente disp[0].dispfb | UNKNOWN (SDK real) | `zbufAddr` (código HLE) | Candidato a causa raíz | HECHO (RECOMP, código fuente) |
| `sceGsSetDefDBuffDc` fuente disp[1].dispfb | UNKNOWN | literal `0` | — | HECHO (RECOMP, código fuente) |
| `MainGsSwapDBuffDc` lectura | (no auditado en original) | literal desde struct, sin transformación | MATCH esperado | HECHO (RECOMP, desensamblado) |
| Escritura privilegiada directa | `V0=0xFF66` en `0x15C410`/`0x15C498` (medido por el usuario) | `Store64` literal del valor leído | Consistente con contrato | HECHO ambos lados |
| DISPFB1 / circuito 1 | `0` (medido) | no participa | **MATCH** | HECHO ambos lados — RETRACTA P4.0F |
| Presentación (fuente elegida) | N/A | sigue DISPFB2 fielmente (P4.0F §12) | Sin cambios | HECHO (RECOMP, P4.0F) |

## 13. Clasificación causal (Fase 15)

- **LAST_PROVEN_COMMON_NODE**: la estructura y semántica de
  `MainGsSwapDBuffDc` (lee el env struct literalmente, sin
  transformación, y lo escribe a los registros privilegiados PMODE/
  DISPFB2) — el CONTRATO de esta función coincide entre lo que el
  guest programa y lo que la medición original observa (`V0=0xFF66` en
  el sitio exacto de escritura, confirmado por el propio usuario) —
  así como el hecho de que EN1=0/circuito-1-inactivo es coherente en
  ambos lados.
- **FIRST_PROVEN_DIVERGENT_NODE**: el valor de `DISPFB2` para el slot
  `which=0`/`disp[0]` dentro del env struct — `0x000000` (FBP=0) en la
  medición original vs `0x10E0` (FBP=0xE0) en RECOMP — con la
  producción de ese valor trazada de forma concreta hasta
  `sceGsSetDefDBuffDc` (`GS.cpp:907`), donde el código HLE usa
  `zbufAddr` en vez de un literal limpio para ese campo específico,
  asimétricamente respecto a su propio tratamiento de `disp[1]`.
- **FIRST_UNPROVEN_NODE**: (a) el valor numérico exacto que
  `sceGszbufaddr()` devuelve en esta ejecución (no re-derivado
  numéricamente en este prompt); (b) qué mecanismo produce el valor
  COMPARTIDO `0xA0` en el slot `disp[1]` en ambos lados (dado que el
  código fuente de `sceGsSetDefDBuffDc` por sí solo predice literal 0
  ahí); (c) el origen del bit AMOD divergente (sección 4).

## 14. Respuestas explícitas a las 20 preguntas

1. UNKNOWN (no probado 1:1, ver §5). 2. INFERENCIA fuerte: la primera
PSS post-boot (advertencia PAL), mismo patrón de boot que toda la
sesión. 3. En el primer `GetPicture` exitoso (~69s), confirmado por el
propio mecanismo `DMC_P311_RAM_GETPICTURE`. 4. `ACTIVE_GS_STATE` —
sección 6, confirmado por coincidencia byte-exacta con RUN_043 (180
muestras, ventana completa). 5. Porque la lectura original de P4.0F
(RUN_029) SÍ es representativa del estado RECOMP real — la
"contradicción" nunca fue RECOMP-vs-RECOMP, es RECOMP-vs-original-vivo;
P4.0F no tuvo, en su momento, la medición original directa para
comparar. 6. SÍ, definitivamente — EN1=0 en ambos lados, bit por bit
(sección 3). 7. SÍ, definitivamente — DISPFB1=DISPLAY1=0 en el
original durante película visible correcta (sección 3). 8. Sin
resolver en este prompt (sección 10) — probablemente workspace de
subtítulos, sin nueva evidencia. 9. Trazado a `sceGsSetDefDBuffDc`
(`GS.cpp:907`), vía `zbufAddr` — sección 9. 10. UNKNOWN para el
original (§9); en RECOMP, literal `0` en el código HLE para
`disp[1]`, pero el valor OBSERVADO (`0xA0`) no coincide con ese
literal — mecanismo real que lo produce sin identificar (§9 UNKNOWN).
11. `zbufAddr` (=`sceGszbufaddr()`) para `disp[0]`, en vez de un
literal limpio — asimetría de código verificada, sección 9. 12. Sin
resolver (§4) — real pero no causal. 13. SÍ — literal, verificado por
desensamblado completo de `MainGsSwapDBuffDc` (§8). 14. SÍ — `Store64`
sin transformación (`ps2_runtime.cpp`, ya auditado en prompts previos
de esta sesión, patrón consistente). 15. NO — ninguna evidencia de
transformación en el runtime; el propio guest ya programa el valor
divergente ANTES de la escritura privilegiada (§9). 16. SÍ — el
divergente ya está presente en RAM guest antes de que
`MainGsSwapDBuffDc` lo lea (§9). 17. `sceGsSetDefDBuffDc` (HLE,
`GS.cpp`), específicamente su uso de `zbufAddr` para `disp[0].dispfb`
en vez de un literal limpio como su gemelo `disp[1]`. 18. SÍ,
estructuralmente coherente con los artefactos ya documentados por
P4.0F (§11) — ahora con causa raíz concreta en vez de "accidente de
solapamiento" genérico. 19. SÍ — ninguna evidencia de este prompt
implica a OpenGL; el hallazgo está enteramente en código HLE de la
syscall GS, muy anterior al backend de render. 20. **NO** — falta
cerrar el UNKNOWN de la sección 9 (por qué `disp[1]` muestra 0xA0 en
vez del literal 0 que predice el código) antes de proponer una
corrección concreta y validarla; implementar ahora arriesgaría
"arreglar" solo la mitad del mecanismo real.

## 15. Clasificación primaria

**`P4_MOVIE_ENV_GUEST_STATE_DIVERGENCE`**

Justificación: el valor guest-visible de `DISPFB2` (which=0/disp[0])
en RAM, ANTES de que cualquier escritura de registro privilegiado
ocurra, ya difiere entre original y RECOMP — confirmado con evidencia
directa de ambos lados (medición PCSX2 + RAM dump RECOMP verificado
byte a byte en dos corridas independientes). La causa productora
(`sceGsSetDefDBuffDc`, HLE) queda identificada con precisión de línea
de código, aunque el mecanismo completo (por qué el otro slot coincide
en 0xA0) no está cerrado al 100%.

## 16. UNKNOWNs restantes y próxima investigación

- Valor numérico exacto de `zbufAddr` en esta ejecución (re-derivación
  estática desde los argumentos reales de `Main_init`→
  `MainGsSetDefDBuffDc`, o instrumentación mínima de un solo punto si
  la estática no basta).
- Mecanismo que fija `disp[1].dispfb`=0xA0 en ambos lados pese a que
  `sceGsSetDefDBuffDc` por sí sola predice literal 0 — candidato:
  alguna otra llamada a `sceGsSetDefDBuffDc`/similar con argumentos
  distintos, o un tercer escritor no identificado aún.
- Origen del bit AMOD divergente (0xFF26 vs 0xFF66) — no causal, pero
  sin explicar.
- Confirmar identidad exacta de película si se requiere mayor certeza
  antes de implementar una corrección (aunque, como se señala en §5,
  la validez del hallazgo central no depende estrictamente de esto,
  dado que `Main_init` es de alcance de aplicación completa, no
  específico de película).

## 17. Estado final de git

Vendor: limpio, sin tocar, HEAD `61a0977...` (verificado antes y
después de este prompt — no se compiló, no se ejecutó nada). Main:
este informe nuevo sin trackear; el informe P4.0F permanece intacto,
sin editar (solo referenciado/corregido desde este nuevo documento,
conforme a la instrucción explícita del prompt).

## 18. Confirmaciones explícitas

NO commit, NO push, NO clean, NO regenerate, NO cambio de
comportamiento de producción, NO build, NO corrida nueva de RECOMP (se
reutilizó evidencia existente: RUN_029, RUN_043, ELF, código fuente
actual del vendor), NO PCSX2 adicional solicitado (la evidencia
existente + el rastreo de código fuente bastaron para la conclusión
central).
