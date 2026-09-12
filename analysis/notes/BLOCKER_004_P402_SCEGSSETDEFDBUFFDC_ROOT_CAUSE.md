# BLOCKER_004 — Prompt P4.0.2: SCEGSSETDEFDBUFFDC_ZBUFADDR_AND_SHARED_SLOT_RESOLUTION

Cierre narrow de los dos UNKNOWN dejados por P4.0.1. Sin commit, sin
build, sin corrida nueva — toda la evidencia proviene de desensamblado
estático adicional (`Main_init` completo) + cómputo manual de la
fórmula real de `sceGszbufaddr` contra el código fuente actual del
vendor.

## 1. Estado de entrada

Main HEAD `6beacfbfb1795813cb34ced0fd7e723b435016f3` ✓. Vendor HEAD
`61a0977924148e018d66d42c30d7756db8084541` ✓, limpio. Dirt esperado:
los 3 informes/tools de P4.0F/P4.0.1/p40, sin cambios. Sin dirt ajeno.

## 2-3. UNKNOWN A — valor exacto de `zbufAddr`

**Corrección de mapeo previa (retracto una premisa implícita de P4.0.1,
no su conclusión)**: `Main_init` llama a `MainGsSetDefDBuffDc` **dos
veces** (`0x15BB00` y `0x15BB28`), con argumentos distintos, sobre DOS
structs distintos. Re-derivé el puntero base `s0` de `Main_init`
(confirmado independientemente: `MainGsSwapDBuffDc` usa el mismo
literal `0x7406A0` para acceder a campos no relacionados con
PMODE/DISPFB — mismo "Main global struct"). Con `s0=0x7406A0`:

- 1ª llamada (`0x15BB00`): destino `s0+0xD0=0x740770`, args
  `psm=0, w=0x200(512), h=0xE0(224)` — **struct DISTINTO**, no el
  nuestro.
- 2ª llamada (`0x15BB28`): destino `s0+0x590=0x740C30` — **coincide
  exacto con nuestro struct**, args `psm=0, w=0x200(512), h=0x1C0(448)`.

Es la **segunda** llamada la que inicializa `0x740C30`, con
`w=512, h=448` (no 224 como una lectura apresurada de la primera
llamada habría sugerido).

**Cómputo manual de `sceGszbufaddr(rdram,&temp,runtime)`** (`GS.cpp:
1319-1362`), con `temp=*ctx` heredando `a0=envAddr=0x740C30,
a2=w=512, a3=h=448` (registros no tocados entre la llamada externa y
la interna — confirmado por lectura del código fuente, `sceGsSetDefDBuffDc`
no modifica `ctx` antes de la llamada a `sceGszbufaddr`):

```
width_blocks = (512+63)>>6 = 575>>6 = 8
param_1 = a0 = 0x740C30; (0x740C30 & 2) = 0  -> rama "else"
height_blocks = (448+31)>>5 = 479>>5 = 14
product = 8*14 = 112 (0x70)

gparam_val = scratch+0x100, con g_gparam por defecto {interlace=1,omode=2,ffmode=1,version=3}
  empaquetado como uint32 LE en los 32 bits bajos: 0x03010201
  gparam_val & 0xFFFF0000FFFF = 0x0201 (513) != 1  -> rama "else": product = (product*0x20000)>>16 = product*2

zbufAddr = 112*2 = 224 = 0xE0
```

**HECHO: `zbufAddr = 0xE0`, exacto**, reproducido a mano contra la
fórmula real y los argumentos reales trazados por desensamblado
completo. Promuevo la inferencia de P4.0.1 a HECHO: la cadena
`sceGszbufaddr → zbufAddr=0xE0 → disp[0].dispfb=0x10E0` queda probada
numéricamente, no solo por coincidencia sospechosa.

## Fase 3 — auditoría de `sceGszbufaddr` en sí

`sceGszbufaddr` no tiene bytes ELF originales recuperables — es un
trampolín HLE (`sceGszbufaddr_0x100558.cpp`, confirmado: redirige
directo a `ps2_stubs::sceGszbufaddr`, mismo patrón que
`sceMpegGetPicture` en P3.15). No se puede auditar su fórmula contra
el original real por este medio. Su fórmula actual (conteo de bloques
de página GS, retorno en unidades de FBP) es plausible/coherente con
la semántica SDK conocida por el nombre de la función — **INFERENCIA,
no HECHO**, de que la FÓRMULA en sí es correcta; lo que SÍ es HECHO es
que la fórmula ACTUAL, con los argumentos reales, produce 0xE0.

## 4. UNKNOWN B — el escritor de `disp[1].dispfb=0xA0`

**Encontrado, HECHO, por desensamblado directo.** Inmediatamente
después de que retorna la 2ª llamada a `MainGsSetDefDBuffDc`
(`0x15BB30` en adelante), `Main_init` ejecuta código de juego real
(sin reemplazo HLE) que parchea explícitamente el campo FBP (bits
bajos 0-8) de TRES campos de 16 bits distintos a **`0xA0`** literal:

```
0x15bb38: a3 = 0xA0
0x15bb40-48: *(s0+0x5D8) = (*(s0+0x5D8) & ~0x1FF) | 0xA0
0x15bb4c-60: *(s0+0x610) = (*(s0+0x610) & ~0x1FF) | 0xA0
0x15bb64-78: *(s0+0x690) = (*(s0+0x690) & ~0x1FF) | 0xA0   (seguido de 3 memcpy adicionales)
```

Con `s0=0x7406A0`: `s0+0x5D8 = 0x740C78`. **`0x740C78` es exactamente
`disp[1].dispfb`** (`0x740C30+0x48`, confirmado en P4.0.1 §7-8 por
triple verificación de offsets). **Este es el escritor.** Los otros
dos campos parcheados (`0x740CB0`, `0x740D30`) no se identificaron en
detalle en este prompt (probablemente registros FRAME/otros buffers
del mismo bloque de doble-buffer, dado el mismo patrón de máscara de
FBP) — no relevantes para cerrar UNKNOWN B, que queda cerrado con el
primero.

**Es código de juego real, sin sustitución HLE** — se ejecuta
IDÉNTICO en original y RECOMP. Esto explica exactamente por qué el
slot `0xA0` coincide entre ambos lados (P4.0.1 §9, sección "shared
slot"): no es coincidencia ni un mecanismo desconocido — es una
personalización deliberada y determinista del propio juego,
posterior a `sceGsSetDefDBuffDc`, que sobrescribe el default de
`disp[1]` con el FBP real que el juego usa como framebuffer "activo".

**`disp[0].dispfb` (`0x740C40` = `s0+0x5A0`) NO aparece en esta
secuencia de parcheo** (ni en ningún otro punto localizado de
`Main_init` hasta `MainSetPalMovieEnv`, ya auditado en P4.0.1 §7) —
confirma Fase 4 del prompt: **`0x10E0` se preserva sin cambios desde
`sceGsSetDefDBuffDc` hasta la reproducción de la película.**

## 5. Fase 6 — verificación del mapeo de slots

Re-verificado, sin cambios: `which=0`(`0x740C30`)=`disp[0]`,
`which=1`(`0x740C68`)=`disp[1]`. El mapeo de P4.0.1 era correcto — lo
que estaba incompleto era identificar CUÁL llamada de `Main_init`
alimenta el struct (corregido en §2) y el escritor de `disp[1]`
(corregido en §4). No se retracta el mapeo de slots en sí.

## 6. Fase 7 — tabla de transición de estado completa

| Etapa | `disp[0].dispfb` (`0x740C40`) | `disp[1].dispfb` (`0x740C78`) |
|---|---|---|
| `sceGsSetDefDBuffDc` (scratch) | `makeDispFb(zbufAddr=0xE0,...)` = `0x10E0` | `makeDispFb(0,...)` = `0x1000` |
| Tras memcpy de `MainGsSetDefDBuffDc` | `0x10E0` (sin cambio, solo reubicado) | `0x1000` (sin cambio, solo reubicado) |
| Tras parche `Main_init` (`0x15BB38-78`) | `0x10E0` (no tocado) | **`0x10A0`** (FBP forzado a 0xA0) |
| Tras `MainSetPalMovieEnv` | `0x10E0` (no tocado — confirmado P4.0.1 §7) | `0x10A0` (no tocado) |
| En la primera película (RUN_029/RUN_043) | `0x10E0` ✓ coincide | `0x10A0` ✓ coincide |

Cadena completa, sin eslabones huérfanos.

## 7. Fase 8 — contrato SDK original (sin bytes ELF recuperables)

`sceGsSetDefDBuffDc_0x101b48.cpp` es, igual que `sceGszbufaddr`, un
trampolín HLE puro (confirmado por lectura) — **no hay bytes ELF
originales recuperables por este medio**, mismo límite estructural que
`sceMpegGetPicture` en P3.15. **No se solicita una medición PCSX2
adicional** (Fase 13): el argumento de simetría de código guest (§4)
cierra la pregunta sin necesitarla.

**Prueba lógica (no lectura directa de bytes, pero igualmente
rigurosa)**: `Main_init`/`MainGsSetDefDBuffDc` son código de juego real
sin sustitución HLE — se ejecutan IDÉNTICOS en original y RECOMP. Se
demostró (§4) que este código parchea `disp[1].dispfb` pero NUNCA
`disp[0].dispfb`, en AMBOS lados por igual. Por tanto:
`disp[0].dispfb_original_final = disp[0].dispfb_original_inicial`
(idéntica relación que en RECOMP, por la misma ausencia de parche).
Dado que el valor FINAL observado en el original es `FBP=0`
(medición PCSX2 directa, P4.0.1 §2), se sigue **necesariamente** que
`disp[0].dispfb_original_inicial` (el valor que el `sceGsSetDefDBuffDc`
REAL calcula) = `FBP 0`. **HECHO por prueba lógica de simetría de
código guest, no por lectura directa de bytes ELF de la función HLE
— pero con el mismo nivel de certeza**, dado que la premisa (código
guest idéntico en ambos lados) está en sí misma verificada por
desensamblado real.

**ORIGINAL_DISP0_INITIAL = FBP 0. ORIGINAL_DISP1_INITIAL = FBP 0**
(idéntico a disp[0] antes del parche — el propio código HLE actual ya
calcula esto correctamente para disp[1], y por la misma lógica de
simetría, el original también debe partir de 0 para AMBOS antes del
parche de `Main_init`, ya que el parche es lo que introduce la ÚNICA
asimetría intencional, `0xA0` solo en disp[1]).

## 8. Fase 9 — tabla comparativa campo por campo

| Campo | SDK original (inferido, §7) | HLE actual | Coincide |
|---|---|---|---|
| `disp[0].pmode` (antes de `MainSetPalMovieEnv`) | EN1=1,EN2=1,MMOD=0,AMOD=?,ALP=0x80 (valores por defecto SDK, INFERENCIA) | `makePmode(1,1,0,0,0,0x80)` — AMOD=0 hardcoded | Ver §10 |
| `disp[0].dispfb` | **FBP=0** (prueba lógica, §7) | `makeDispFb(zbufAddr=0xE0,...)` | **NO — bug confirmado** |
| `disp[1].dispfb` | FBP=0 (antes del parche guest) | `makeDispFb(0,...)` | **SÍ** |
| `disp[0].display`/`disp[1].display` | No auditado, sin evidencia de divergencia | `makeDisplay(636,32,0,0,w-1,h-1)` | Sin evidencia en contra |
| `disp[*].bgcolor` | No auditado | `0` | Sin evidencia en contra |

## 9. Fase 10 — gate de "definitivamente incorrecto"

| Condición requerida | Estado |
|---|---|
| HECHO: original inicializa `disp[0].dispfb` FBP=X | **SÍ, X=0** (prueba lógica de simetría, §7) |
| HECHO: HLE actual inicializa `disp[0].dispfb` FBP=Y | **SÍ, Y=zbufAddr** (código fuente, `GS.cpp:907`) |
| HECHO: Y viene de `zbufAddr` | **SÍ** |
| HECHO: esto produce el `0x10E0` observado | **SÍ, verificado numéricamente (§2-3): zbufAddr=0xE0 exacto** |
| HECHO: el comportamiento original vivo corresponde al camino que da `0x1000` | **SÍ** (medición PCSX2 directa, P4.0.1 §2) |

**Las 5 condiciones se cumplen.**

## 10. Fase 11 — diferencia secundaria AMOD (breve, no bloqueante)

`sceGsSetDefDBuffDc` llama `makePmode(1,1,0,/*amod=*/0,0,0x80)` —
`amod=0` es un **literal hardcoded en la misma función** (`GS.cpp:890`).
`MainSetPalMovieEnv` (código guest, ya auditado en P4.0.1 §7) NO toca
el bit 6 (AMOD) en ningún momento. Por la MISMA lógica de simetría de
código guest (§7): si el bit no se toca en ningún lado por código
guest, el valor final observado (`AMOD=1` en original, `AMOD=0` en
RECOMP) debe provenir directamente de sus respectivas
`sceGsSetDefDBuffDc`. **Clasificación: mismatch HLE independiente,
misma función, causa raíz distinta (un literal de argumento en vez de
una variable mal usada)** — trivialmente corregible junto con el fix
principal (cambiar `amod=0` por `amod=1` en la misma llamada a
`makePmode`), pero NO se implementa en este prompt.

## 11. Cadena causal requerida (completa)

```
ORIGINAL:
  sceGsSetDefDBuffDc (real, no recuperable en bytes, inferido HECHO por simetría)
    -> disp[0].dispfb = FBP 0   (inicial, igual que disp[1])
    -> disp[1].dispfb = FBP 0   (inicial)
    -> Main_init parchea SOLO disp[1] -> FBP 0xA0 (código guest real, idéntico a RECOMP)
    -> activo: disp[0]=0x000000, disp[1]=0x140000

RECOMP:
  sceGsSetDefDBuffDc (HLE, GS.cpp:869-938)
    -> disp[0].dispfb = makeDispFb(zbufAddr=0xE0,...) = FBP 0xE0   <- PRIMERA ASIGNACIÓN DESIGUAL
    -> disp[1].dispfb = makeDispFb(0,...) = FBP 0   (inicial, correcto)
    -> Main_init parchea SOLO disp[1] -> FBP 0xA0 (MISMO código guest, idéntico)
    -> activo: disp[0]=0x1C0000, disp[1]=0x140000
```

**Primera asignación desigual**: la línea `const uint64_t dispfb0 =
makeDispFb(fbp1, fbw, psm, 0u, 0u);` en `GS.cpp:907`, donde `fbp1 =
zbufAddr` debería ser `0u` (literal, igual que su hermana `dispfb1`).

## 12. Respuestas a las 20 preguntas

1. `0xE0` (224). 2. **Sí**, exacto. 3. UNKNOWN si la fórmula interna es
"correcta" en sentido SDK — no auditable sin bytes originales; lo que
es HECHO es que produce 0xE0 con estos argumentos. 4. **No** — el
literal correcto para `disp[0].dispfb` en esta función debería ser `0`,
no el resultado de `sceGszbufaddr()`; `zbufAddr` es para el
Z-buffer, no para un framebuffer de display. 5. FBP=0 (prueba lógica de
simetría, §7). 6. FBP=0 (antes del parche de `Main_init`). 7. `disp[0]`=
`zbufAddr`(0xE0); `disp[1]`=literal 0. 8. Layout correcto — verificado
byte a byte contra `GsDBuffDcMem`/las copias reales de
`MainGsSetDefDBuffDc` (P4.0.1 §7, re-confirmado aquí). 9. `Main_init`,
código de juego real, inmediatamente después de la 2ª llamada a
`MainGsSetDefDBuffDc`. 10. `0x15BB38-0x15BB48` (guarda el FBP en
`*(s0+0x5D8)`). 11. **Sí** — es código guest sin reemplazo HLE, se
ejecuta igual en ambos lados; por eso el slot 0xA0 coincide. 12. Porque
nada en el código guest trazado lo toca después de
`sceGsSetDefDBuffDc` — permanece en el valor que la HLE calculó
(0xE0). 13. Porque, por la misma razón (nada lo toca), permanece en el
valor que el SDK real calculó — inferido en 0 por la prueba de
simetría. 14. **Sí** — verificado numéricamente, no solo por
coincidencia de magnitud. 15. **Sí** — usar la dirección del
Z-buffer como puntero de framebuffer de DISPLAY no es una operación
SDK con sentido; el propio código ya trata `disp[1]` con un literal
limpio, mostrando que ESE es el patrón correcto. 16. Independiente —
mismo archivo, distinto campo/línea (`amod=0` hardcoded en
`makePmode`), no bloquea el fix principal. 17. No — el hallazgo
depende de `Main_init` (alcance de aplicación, no de película
específica), sección ya señalada en P4.0.1 §5. 18. **Sí** — sin
cambios, ninguna evidencia nueva lo contradice. 19. **Sí** — línea
exacta: `GS.cpp:907`, `dispfb0 = makeDispFb(fbp1=zbufAddr,...)`
debería ser `makeDispFb(0u,...)`. 20. **Sí, condicionalmente** — la
corrección es mínima y bien localizada, pero se recomienda validarla
con una corrida real antes de checkpoint (no ejecutada en este
prompt, conforme a la política).

## 13. Clasificación primaria

**`P4_SCEGSSETDEFDBUFFDC_FIELD_MAPPING_BUG`**

(no `_CALCULATION_BUG`: la fórmula de `sceGszbufaddr` en sí no se probó
incorrecta — el bug es usar SU RESULTADO en el campo equivocado, no un
error de cálculo dentro de ella).

## 14. Estado final de git

Vendor: limpio, sin tocar, HEAD `61a0977...`. Main: este informe
nuevo sin trackear; P4.0F y P4.0.1 intactos, sin editar.

## 15. Confirmaciones explícitas

NO commit, NO push, NO clean, NO regenerate, NO build, NO corrida
nueva de RECOMP, NO PCSX2 adicional solicitado, NO cambio de código de
producción.
