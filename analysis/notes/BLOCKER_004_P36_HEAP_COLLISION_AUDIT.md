# P3.6 — GUEST_HEAP_RUNTIME_COLLISION_AUDIT (auditoría independiente, Fable 5)

Fecha: 2026-09-10. Sin fix implementado. Sin commit. Instrumentación
exclusivamente de solo lectura (cliente PINE propio contra PCSX2, sin
breakpoints, sin escrituras, sin cambiar comportamiento del juego).

Fuentes de evidencia:
- ELF real `original/SLES_503.58` parseado con script propio (no se reutilizó
  ningún número de P3.4/P3.5 sin recalcular).
- Código generado `analysis/local/symtabfirst/generated/*.cpp` (desensamblado
  embebido, mismo árbol que compila el build activo).
- `vendor/PS2Recomp/ps2xRuntime` (runtime real).
- PCSX2 2.8.2 + SLES-50358 real, lecturas vía PINE (puerto 28011), 5 minutos
  de boot → título → 6 ciclos de attract movies (`Movie+0x18` observado con
  totalSizes reales: 0x4DA4004, 0x1A2C004, 0x3634004, 0x1A5C004, 0x5630004,
  0x3668004 — el pipeline de movie SÍ se ejercitó durante la medición).

---

## A. Premisas verificadas / refutadas

| Premisa (P3.4/P3.5) | Veredicto |
|---|---|
| 16 program headers; seg0 `0x100000..0x9F8480` (filesz 0x489900, memsz 0x8F8480, RWX); 14 PT_LOAD idénticos vaddr=0x1F00000 filesz=0 memsz=0x100; seg15 memsz=0 | **CONFIRMADO** (recalculado byte a byte; HECHO) |
| Los 14 segmentos altos son marcadores de overlays del linker | **CONFIRMADO con matices**: secciones `t_*` (sh_size=0) + símbolos `_t_*_segment_start/end`. Matiz 1: el linker es **Metrowerks CodeWarrior** (sección `.mwcats`), no ee-gcc/SN. Matiz 2: los 14 comparten `text_start=0x1F00040, text_size=0xC0` idénticos y filesz=0 — como *overlays de código* parecen vestigiales; la ventana en el juego retail se usa como **buffer de staging de ficheros** (ver E) |
| `maxLoadedRdramEnd=0x1F00100 → suggested=0x1F01100 → clamp 0x1F00000 → resetGuestHeapLocked colapsa a base=0,limit=0` | **CONFIRMADO** (recomputado independientemente desde los bytes del ELF + lectura de `ps2_runtime.cpp:786-940,1599-1642`; determinista) |
| El colapso del heap es LA causa raíz única | **INCOMPLETO — REFUTADO parcialmente**: existe una **segunda causa suficiente e independiente**: el crt0 de DMC (`sub_00100008`, 0x100060-0x100074) llama `SetupHeap(0x1F00100, 0)` (syscall 0x3D). El HLE (`System.cpp:557-598`) hace `configureGuestHeap(0x1F00100, 0x1F00000)` → `resetGuestHeapLocked` → **el mismo colapso base=0,limit=0, aunque se arregle la heurística del ELF**. Cualquier fix que solo toque `maxLoadedRdramEnd` NO desbloquea `guestMalloc` |
| `0xAC0000` es un "margen de seguridad" de `Mem_alloc_R` | **REFUTADO como interpretación**: es (1) la **capacidad** de la región R (`floor = top − 0xAC0000`) y (2) la **base mínima** del heap forward (`Main_malloc`: `base = max(__bss_end, 0xAC0000) = 0xAC0000`). Ver D |
| "DMC probablemente usa gran parte del hueco 0x9F8480..0x1F00000" | **CONFIRMADO y ahora cuantificado**: el hueco es casi todo suyo por diseño (heap forward + R-heap), PERO queda una franja `[0x9F8480, 0xAC0000)` que el juego salta deliberadamente — ver G |
| `*0x7411B4` sin escritor conocido | **RESUELTO**: único escritor `Mem_alloc_R_ptr_set` (0x15B0B0); 5 llamadores con valores 0x01E00000 (Main_malloc/Game_scene_init/Soft_reset/Puase_start_check) y 0x01FE6000 (R40a_init). Confirmado dinámicamente: `*0x7411B4 = 0x01E00000` en vivo |
| `0x7406A4` (leído por MpegMovieDecode) es "registro hardware" | **REFUTADO** (lateral): `Sys` es un global BSS en 0x7406A0 tamaño 0x2590; `0x7406A4 = Sys+4`. `0x7411B4 = Sys+0xB14` |

## B. Mapa EE RDRAM (0x00000000..0x02000000)

| Rango | Contenido | Clase | Evidencia |
|---|---|---|---|
| 0x00000000–0x00100000 | Área kernel/args PS2 | RESERVED | convención PS2 |
| 0x00100000–0x00589900 | text+data de `main` (filesz) | PROVEN RESIDENT | PT_LOAD seg0 |
| 0x00589900–0x009F8480 | BSS (`_fbss`..`__bss_end`; incluye `Movie`@0x87D680, `Sys`@0x7406A0, `Heap_man_head`@0x5CDDF0) | PROVEN RESIDENT | ELF + símbolos; crt0 la limpia |
| **0x009F8480–0x00AC0000** | franja saltada por `Main_malloc` (~798 KB) | **SAFE CANDIDATE** (con residuales, ver G) | estática: 0 referencias demostradas; dinámica: TODO-cero 5 min boot+título+6 movies |
| 0x00AC0000–≈0x01340000 | Heap forward `Mem_alloc` (bump, crece ↑, SIN chequeo de techo; puntero `*0x7411AC`; 72 escritores directos en el motor) | PROVEN DYNAMIC | código + dinámico (boot usa hasta 0xB64100; gameplay NO medido) |
| ≈0x01340000–0x01E00000 | R-heap `Mem_alloc_R` (lista, crece ↓ desde `top=*0x7411B4=0x1E00000`, floor=top−0xAC0000=0x1340000) | PROVEN DYNAMIC | código + dinámico (uso actual 0x1C25F90..0x1E00000, 6 nodos, ~1.86 MB) |
| 0x01E00000–0x01F00000 | Scratch fijo del juego (CardScreenInit, GetCardInfo, Option_load ×7, Status_init; datos vivos observados 0x1E00000..0x1E65000). En R40a: R-heap extendida hasta top=0x1FE6000 | **UNSAFE** (uso fijo por dirección) | estática + dinámica |
| 0x01F00000–0x01F00100 | Ventana marcador de overlays `t_*` (14 secciones) | OVERLAY | ELF |
| 0x01F00100 | `end`/`_end` + sección `heap` (heap C newlib, **tamaño 0**: `SetupHeap(0x1F00100,0)`, brk nunca se movió) | RESERVED | ELF + crt0 + dinámico |
| 0x01F00000–0x01FF6000 | Staging de ficheros/texturas UI: `CdFileRead(id, 0x1F00000, …)` + `LoadImageT32s` (Game_ending, Game_over, Puase_menu_init, Easy_start_main). Extensión máxima real = tamaño del mayor fichero cargado, **no acotada estáticamente** | DMA/MOVIE-STAGING / **UNSAFE** | estática (no ejercitada en la sesión: attract no abre esas pantallas) |
| 0x01FF6000–0x01FFE000 | 4 stacks de tasks del scheduler cooperativo del juego (`scheduler_init`: 4×0x2000 bajando desde 0x1FFE000; tabla en 0x5027F0) | STACK | estática + dinámica (datos vivos en 0x1FF7800.., 0x1FF9C00.., 0x1FFBC00..) |
| 0x01FFE000–0x02000000 | Stack del main thread (`SetupThread(gp, 0x1FFE000, 0x2000, …)` en crt0; `_stack=0x1FFE000`, `_stack_size=0x2000`) | STACK | estática + dinámica |

Guest heap sintético actual de RECOMP: **base=0, limit=0 (colapsado)** — no ocupa nada.
Stacks de invocación async de RECOMP: se tallan desde 0x2000000 ↓ (ver F — COLISIÓN LATENTE).

## C. Allocators de DMC

1. **`Mem_alloc` (0x15ADC0)** — bump forward. `cur=*0x7411AC` (base copy `*0x7411A0`);
   alinea (mask≤0xF por clamp de `a1`), devuelve `cur` alineado, `cur+=size`.
   **Sin chequeo de techo.** Tracking LIFO en `Mem_alloc_addr[256]` (0x5CDE10) +
   contador `Mem_alloc_addr_no` (0x5CDE08, error `Disp_err_mess` al llegar a 0x100).
   `Mem_free` (0x15AE80): pop LIFO → `*0x7411AC = Mem_alloc_addr[--n]`.
   Checkpoints: `*0x7411A4` (post-boot, 0xB64100 observado; `Soft_reset` restaura a él),
   `*0x7411A8` (restaurado por `Game_scene_init`). Init: `Main_malloc` (0x15CEE0):
   `base = (0x9F8480 < 0xAC0001) ? 0xAC0000 : 0x9F8480` → **0xAC0000** en este binario.
2. **`Mem_alloc_R` (0x15AEF0)** — lista enlazada descendente desde `top=*0x7411B4`.
   Bloques en párrafos de 16B, header 16B en la parte alta, datos debajo;
   primer nodo en `top−0x10` (confirmado en vivo: `*0x5CDDFC=0x1DFFFF0`).
   **OOM cuando el candidato baja de ≈`top−0xAC0000`** → `Disp_err_mess`, ret 0.
   `Mem_free_R` libera nodo; `Mem_alloc_R_check` (0x15B030) valida la lista y
   mantiene `*0x7411B0` (bottom actual; observado 0x1C25770).
   `Mem_alloc_R_ptr_set(a0)` (0x15B0B0): resetea la lista (`*0x5CDDFC=0`), guarda top
   anterior en 0x5CDDD0, fija `top=a0`. Tops usados: **0x01E00000** (normal) y
   **0x01FE6000** (`R40a_init` — modo especial; R-región llega entonces a
   [0x1526000, 0x1FE6000), cruzando la ventana 0x1F00000).
3. **newlib malloc/sbrk** — brk en `heap_ptr.30` (0x50A62C, init 0x1F00100), techo
   por syscall `EndOfHeap`. **Capacidad 0 por diseño** (`SetupHeap(end,0)`), brk
   jamás se movió en vivo → el juego no depende de malloc de libc en la práctica.

## D. Semántica real de `Mem_alloc_R` y 0xAC0000

`0xAC0000` (11.264 KB) aparece dos veces con papeles distintos, ambos demostrados
por instrucciones:

- `Main_malloc` 0x15CEE0-0x15CF24: base mínima del heap forward
  (`sltu`+branch contra 0xAC0001; escribe 0xAC0000 en `*0x7411AC` y `*0x7411A0`).
  Efecto: base estable entre builds mientras el BSS no supere 0xAC0000 — y deja
  `[__bss_end, 0xAC0000)` como holgura deliberada.
- `Mem_alloc_R` 0x15af64-0x15af70: `lui 0xFF54` ⇒ `top + 0xFF540000 = top − 0xAC0000`
  como **suelo** de la región descendente ⇒ 0xAC0000 = capacidad de la R-región.

NO es un "margen de seguridad" restado por prudencia: es el tamaño de región en
ambos casos. Con top=0x1E00000: R-región = [0x1340000, 0x1E00000).

## E. Overlays y rango reservado real

- Los 14 PT_LOAD altos: marcadores estructurales (filesz=0, offset compartido =
  inicio de `.shstrtab`, cero bytes de contenido). Como overlays de CÓDIGO son
  vestigiales en el retail (los 14 idénticos, `text_size=0xC0`, sin blobs en el
  ELF; no se encontró ningún loader que materialice `_t_*_text_start`).
- La ventana **SÍ se usa en runtime**, como staging de datos:
  `CdFileRead(fileId, dest=0x1F00000, …)` + `LoadImageT32s` en Game_ending,
  Game_over_main, Puase_menu_init, Easy_start_main. Tamaño real cargado = tamaño
  de fichero de CD ⇒ **no acotable por el ELF**; cota superior práctica =
  0x1FF6000 (inicio de task stacks). En modo R40a la R-heap extendida llega a
  0x1FE6000 (usos mutuamente excluyentes por diseño del juego).
- Veredicto: **todo [0x1F00000, 0x2000000) debe tratarse como reservado del
  juego** (staging + heap C marcador + task stacks + main stack). El valor de
  `kGuestHeapHardLimit=0x1F00000` es correcto como límite superior… pero NO
  protege [0x1E00000,0x1F00000) (scratch fijo del juego) ni resuelve dónde SÍ
  puede vivir el heap sintético.

## F. Consumidores / requisitos de `guestMalloc` (y estructuras hermanas)

| Caller | Tamaño/align | Lifetime | Guest-visible/DMA |
|---|---|---|---|
| `MPEG.cpp:1672` (callback data) | 32 B @16 | por evento, se libera | guest (a0 de StrPcmCallBack) |
| `MPEG.cpp:2129` (work area sceMpeg) | 0x600 @8 | sesión de movie | guest |
| `Thread.cpp:276` (stacks de threads guest con stack==0) | stackSize del juego | vida del thread | guest, SP real |
| `GS.cpp` ×3 (paquetes GIF) | qwc·16 / 80 / 128 @16 | transitorio | **DMA** |
| `Font.cpp` ×4 (texto/glifos/kerning) | ≤0xC400 @≤0x40 | mixto | **DMA** |
| `LibC.cpp`/`Compatibility.cpp` (malloc/calloc/realloc HLE — sustituyen el newlib del juego, que en HW real tiene heap 0) | arbitrario | arbitrario | guest |
| `ps2_iop_host.cpp:234` (marshalling IOP RPC/audio) | pequeño | transitorio | guest |

Pico razonable para DMC: decenas de KB; los stacks de thread dinámicos son el
único consumidor potencialmente grande (DMC crea sus tasks con stacks propios en
0x1FF6000+, así que el caso stack==0 no debería dispararse — sin confirmar).

Acoplamientos críticos descubiertos (van más allá de `guestMalloc`):

1. **`SetupHeap` HLE ⇒ `configureGuestHeap`**: el heap sintético está unificado
   con el heap de kernel que declara el juego. Para DMC (heap declarado vacío en
   0x1F00100, sobre el hard limit) esto **recolapsa el heap en cada boot**.
2. **`EndOfHeap` HLE devuelve `guestHeapLimit()`**: hoy devuelve 0 (sbrk del
   juego imposible); tras cualquier fix devolverá el límite del heap sintético,
   redefiniendo lo que el juego cree que es su techo de sbrk. Semántica
   entrelazada que un fix debe considerar explícitamente.
3. **Stacks de invocación async** (`EeScheduler.cpp:1171` +
   `reserveAsyncCallbackStack`): 0x4000 por (thread,depth), permanentes,
   tallados desde 0x2000000 ↓ con floor 0x1F01100. La primera reserva ocupa
   [0x1FFC000,0x2000000) ⇒ **pisa el stack del main thread del juego
   (0x1FFE000-0x2000000) y el task stack #2**. Colisión latente que se
   manifestará en cuanto el heap se arregle y `queueInvocation`/StrPcmCallBack
   empiece a ejecutarse. Debe corregirse junto con el heap.

## G. Intervalos clasificados

- `[0x00000000, 0x00100000)` — RESERVED (kernel).
- `[0x00100000, 0x009F8480)` — UNSAFE (imagen residente).
- **`[0x009F8480, 0x00AC0000)` — SAFE CANDIDATE** (~798 KB).
  A favor: holgura deliberada de `Main_malloc` (base fija 0xAC0000); heap forward
  crece ALEJÁNDOSE (↑ desde 0xAC0000); R-floor (0x1340000) muy por encima;
  newlib heap y stacks en el último MB; 0 referencias de código demostradas;
  TODO-cero durante 5 min de boot+título+6 attract movies (crt0 la limpia al
  arrancar, luego nadie escribe). Residual: inmediatos 0xA1xxxx/0xA3xxxx en
  código de efectos no resueltos uno a uno (probables constantes, no punteros);
  gameplay de misión no medido (sin input en la sesión).
- `[0x00AC0000, 0x01340000)` — UNSAFE (heap forward; sin techo comprobado, en
  gameplay crece scene-a-scene).
- `[0x01340000, 0x01E00000)` — UNSAFE (R-región normal).
- `[0x01E00000, 0x01F00000)` — UNSAFE (scratch fijo: card/option/status; y
  R-región extendida en R40a).
- `[0x01F00000, 0x01FF6000)` — UNSAFE (staging CdFileRead/LoadImageT32s +
  ventana overlay + heap C marcador).
- `[0x01FF6000, 0x02000000)` — UNSAFE (stacks reales del juego).
- UNKNOWN global restante: high-water del heap forward y de la R-región durante
  misiones reales (no afecta al SAFE CANDIDATE: ambos están acotados por
  diseño a [0xAC0000,→) y (→,0x1E00000] respectivamente, con la excepción R40a
  acotada a [0x1526000,0x1FE6000)).

## H. Comparación de estrategias de fix

| Estrategia | Generalidad | Seguridad DMC | Notas |
|---|---|---|---|
| A. Excluir PT_LOAD marcador del cálculo | alta | **insuficiente sola**: deja base≈0x9F9480 (ok) PERO el crt0 recolapsa vía SetupHeap; además un límite=hardLimit dejaría el heap invadiendo 0xAC0000+ (heap del juego) si crece | necesaria como higiene, no suficiente |
| B. Mayor hueco libre entre PT_LOADs | media | **falsa seguridad** (el "hueco" es el heap del juego, demostrado) | rechazada |
| C. Rango fijo universal | — | — | no existe en PS2 (32 MB todos del juego) |
| D. Subir kGuestHeapHardLimit | — | peor (el último MB está lleno de cosas reales) | rechazada |
| E. `configureGuestHeap(base,limit)` por título | n/a (por título) | alta con G | ya existe en la API; hoy solo la llama SetupHeap |
| F. Heurística + override + verificación | **la correcta** | alta | ver I/J |
| G. Alternativa arquitectónica (arena reservada para runtime, separada del kernel heap del juego) | alta | alta | ver sección 9 del prompt: es la dirección correcta a largo plazo |

Sobre la sección 9 del prompt (¿necesita el runtime RAM guest arbitraria?):
las asignaciones SÍ necesitan direcciones EE reales (DMA GIF/fonts, punteros
leídos por código guest, SP de callbacks) ⇒ no pueden vivir solo en host. Pero
NO necesitan ser "el heap del juego": una **arena pequeña, explícita y
verificada** (decenas–cientos de KB) basta para todos los consumidores
enumerados en F. El contrato correcto es: (a) el kernel-heap que declara el
juego (SetupHeap/EndOfHeap) se emula tal cual, aunque sea de tamaño 0; (b) la
arena interna del runtime es OTRA cosa, con su propio rango verificado.

## I. Recomendación específica DMC (no implementada)

    proposedHeapBase  = 0x009FA000
    proposedHeapLimit = 0x00ABE000
    capacidad         = 0x000C4000  (784 KB)

Dentro de `[0x9F8480, 0xAC0000)` con guardas de ~6.5 KB en ambos extremos.
Mecanismo: `configureGuestHeap(0x9FA000, 0xABE000)` por título (API existente),
`bloqueando` que el SetupHeap del crt0 lo sobrescriba (ver invariantes).
Etiqueta honesta: seguro bajo toda la evidencia estática y bajo 5 min de
dinámica que incluye el escenario exacto de BLOCKER_004 (attract movies);
riesgo residual = misiones reales no medidas ⇒ mantener canarios + watchpoint
de validación en la primera sesión de gameplay (test plan, L). Adicional
obligatorio: reubicar los stacks de invocación async DENTRO de esta arena
(p. ej. tallados desde 0xABE000 ↓) — dejar su floor/top actuales
([0x1F01100,0x2000000)) pisaría los stacks reales del juego.

## J. Recomendación general PS2Runtime

1. Separar conceptualmente **kernel heap del juego** (SetupHeap/EndOfHeap — emular
   fielmente lo declarado, incluso tamaño 0) de la **arena interna del runtime**
   (guestMalloc para HLE). Hoy son el mismo objeto y eso es la raíz del problema.
2. Heurística por defecto de la arena: colocar tras el mayor PT_LOAD **con
   contenido** (`filesz>0` o `memsz` grande), ignorando segmentos marcador
   (`filesz==0` y `vaddr>=hardLimit`), con tamaño moderado (256 KB–1 MB), y
   **validar**: no intersecar PT_LOADs residentes ni el rango declarado por
   SetupHeap; canarios en cada bloque; log de error y fail-fast si un canario
   muere (colisión con allocator del juego detectada).
3. `configureGuestHeap` por título como override de primera clase (config del
   port, no hack), con precedencia sobre la heurística Y sobre SetupHeap.
4. Nunca degradar en silencio a base=0/limit=0 (hoy pasa y costó P3.1–P3.6
   entero de diagnóstico).

## K. UNKNOWNs restantes

1. High-water del heap forward y R-región en misiones reales (requiere sesión
   con input o watchpoints; no bloquea el candidato, sí lo re-valida).
2. Inmediatos 0xA1xxxx/0xA3xxxx en código de efectos (probables constantes
   float/fixed; no resueltos uno a uno).
3. Extensión máxima real del staging 0x1F00000 (tamaño del mayor fichero UI).
4. Si DMC crea algún thread con stack==0 (consumidor Thread.cpp).
5. Semántica exacta que el juego espera de `EndOfHeap` tras el fix (hoy
   devuelve guestHeapLimit; con la arena separada debería devolver el techo del
   kernel heap DECLARADO, es decir 0x1F00100 para DMC).

## L. Test plan del futuro fix (idéntico en espíritu a 13.7, ampliado)

0. Invariantes en arranque: base=0x9FA000, limit=0xABE000, base<limit, align 16,
   sin intersección con PT_LOADs residentes / [0xAC0000,0x1E00000] /
   [0x1E00000,0x2000000); SetupHeap del crt0 NO altera la arena; log ERROR +
   abort si `resetGuestHeapLocked` colapsa.
1. `guestMalloc(32,16) != 0` en el primer intento.
2. `dispatchGuestStreamCallback` alcanza `queueInvocation`.
3. Stack de invocación DENTRO de la arena (no en 0x1FFxxxx).
4. `StrPcmCallBack` ENTRY; 5. `audioDecEndPut`; 6. `mode=1`.
7. `+0x58: 0 → 0x3C00 → 0x5C00 → ≥0x6000` (deltas de 12.1).
8. `audioDecIsPreset` PATH B v0=1. 9. `Task_execute(MpegMovieDecode)`.
10. `MpegMovieDecode` ENTRY. 11. `sceMpegGetPicture` ENTRY.
12. `decodedFrames` deja de clavarse en 8.
13. Canarios de arena intactos tras título+movie; después, primera sesión de
    gameplay con canarios → si alguno muere, DETENERSE: nueva divergencia.
Regla: parar en la PRIMERA divergencia nueva; no encadenar fixes.

---
Resultado independiente P3.6 — GUEST_HEAP_RUNTIME_COLLISION_AUDIT

# P3.6.1 — Reconciliación de RuntimeGuestArena y rango seguro

Fecha: 2026-09-10 (misma sesión, revisión crítica del propio P3.6).
Independencia: NO se han leído las secciones nuevas de
`BLOCKER_004_pss_video_output.md` ni `BLOCKER_004_ASTRA_AUDIT.md` producidas
por otras sesiones. `git status` al inicio de P3.6.1 coincide con el del
inicio de la sesión (mismos ficheros M/??); se reporta y no se incorpora.

## 1. La "contradicción" 0xABE000 vs 0xAC0000 — REFUTADA aritméticamente

El enunciado de la revisión afirmaba `0x00ABE000 > 0x00AC0000` y que la arena
"atraviesa 0x1E000 por encima". Es falso:

    0x00ABE000 < 0x00AC0000
    0x00AC0000 − 0x00ABE000 = 0x2000  (8 KB de guarda POR DEBAJO de la base)

La arena propuesta `[0x9FA000, 0xABE000)` está íntegramente por debajo de la
base de `Mem_alloc` y no la toca. HECHO.

## 2. Refutación intentada del rango — resultado: se REFUERZA

**2.a Inducción estructural sobre `*0x7411AC` (cur del heap forward), HECHO:**
se auditaron los 72 escritores. Clasificación exhaustiva:

- init: `Main_malloc` → `max(__bss_end=0x9F8480, 0xAC0000) = 0xAC0000`;
- 68 sitios leen-y-avanzan (`lw 0x11AC` → align-up/add → `sw`), nunca bajan;
- restores desde snapshots de un `cur` anterior:
  `Mem_free` (pop `Mem_alloc_addr[n-1]`), `Soft_reset` (←`*0x7411A4`),
  `Game_scene_init` (←`*0x7411A8`), `Status_end2` (←`pFree_bak_stat`@0x898540);
- los snapshots los escriben `Main_init` (0x15BE80: `A4=cur, B8=count` — el
  checkpoint de boot, valores vivos 0xB64100/3), `Game_init` (0x1343DC:
  `A8=cur, BC=count` — checkpoint de inicio de partida) y `Status_start`
  (0x2DE80C-0x2DE824: `pFree_bak_stat=cur`). `R203_init` escribe el centinela
  0 en `pFree_bak_stat` (solo relevante dentro del ciclo de vida del status
  screen).

Conclusión: **cur ≥ 0xAC0000 por inducción** — `Mem_alloc` no puede escribir
en `[0x9F8480, 0xAC0000)` por ningún camino. La garantía pedida ("razón
estructural") existe y es esta. Corolario: `Mem_alloc` NO tiene techo
(re-verificado: cero comparaciones de límite en `Mem_alloc`), de modo que
cualquier arena POR ENCIMA de 0xAC0000 sería UNSAFE — la propuesta está por
debajo, así que el sin-techo no la afecta.

**2.b Residual de P3.6 (inmediatos 0xA1..0xAB) — RESUELTO, no son punteros:**
patrón real `lui $v0, 0xAx; ori $a0, $v0, n; jal Esp_init(0x181100)` — son
**IDs de efecto de 32 bits** (`0x00A10002`, `0x00AA0003`, …) pasados por valor
al sistema de partículas. Los `lui 0xA0` + `lw −0x7BD8` son `uv_result_font`
@0x9F8428 (BSS con nombre, debajo de `__bss_end`); el `lui 0x9F` de
`Puase_start_check` es `Pause_stop_bak`@0x9F6560. Cero referencias de
dirección al rango candidato en todo `.text`. HECHO.

**2.c Dinámica reforzada:** escaneo fino (paso 0x80, 6400 puntos) de
`[0x9F8480, 0xAC0000)` → 0 puntos no-cero, repetido durante otra sesión de
monitorización con attract movies. `A8=0, BC=0` en vivo confirma que
`Game_init` (checkpoint de gameplay) nunca corrió — coherente con que
gameplay no se alcanzó.

**2.d Gameplay: UNKNOWN declarado.** No medible sin instalar el driver
ViGEmBus (mando virtual; no instalado en el sistema — no se instala un driver
de kernel de forma autónoma) o sin robar el foco del usuario (teclado Qt).

## 3. Auditoría completa de reserveAsyncCallbackStack — COLISIÓN DEMOSTRADA

`ps2_runtime.cpp:1938-1975` + `EeScheduler.cpp:1155-1178` (único caller):
cursor inicial `m_asyncCallbackStackTop=0x02000000`, floor para DMC
`=max(hardLimit, suggestedBase)=0x01F01100`; 0x4000 por stack, align 16,
crece hacia abajo, **nunca se libera** (`m_invocationStackTops` sin erase),
clave por (threadId, depth) ⇒ capacidad ≈63 stacks hasta el floor.

Primera reserva (primera invocación encolada, p. ej. StrPcmCallBack):

    top  = 0x2000000
    base = 0x2000000 − 0x4000 = 0x1FFC000
    SP inicial devuelto = 0x1FFFFF0

⇒ ocupa `[0x1FFC000, 0x2000000)`, que se solapa con:
- stack del main thread de DMC `[0x1FFE000, 0x2000000)` (SetupThread crt0), y
- task stack #1 del scheduler del juego `[0x1FFC000, 0x1FFE000)`.

**Clasificación: DEMOSTRADA** (aritmética determinista, sin necesidad de
ejecución). Es la primera colisión que ocurriría hoy si guestMalloc se
arreglara de forma aislada.

## 4. HALLAZGO NUEVO (corrige/completa el bloque F de P3.6): pools fijos del runtime en 0x1F00000

`Syscalls/Helpers/State.h:250-262`:

    kRpcPacketPoolBase = 0x01F00000  (0x10000 bytes)
    kRpcServerPoolBase = 0x01F10000  (0x10000 bytes)
    kTlsPoolBase       = 0x01F20000  (0x10000 bytes)

El runtime YA aparca estado RPC sceSif y TLS en `[0x1F00000, 0x1F30000)` —
exactamente la ventana que DMC usa como staging de `CdFileRead`
(Game_over/Ending/Pause/Easy). Colisión latente ADICIONAL, independiente del
heap: cuando el juego cargue un fichero a 0x1F00000, machacará los pools RPC
(CDVD/sound/pad van por RPC), y `rpcZeroRdram` puede machacar datos staged.
No observable aún porque esas pantallas no se alcanzan sin gameplay.

## 5. Semánticas definitivas (consolidadas)

- `Mem_alloc`: bump ascendente desde 0xAC0000, sin techo, snapshots/restores
  LIFO + 3 pares save/restore con nombre. cur ≥ 0xAC0000 (inducción, 2.a).
- `Mem_alloc_R`: descendente desde `top=*0x7411B4`; floor derivado
  `top−0xAC0000` (capacidad fija 0xAC0000, dinámica respecto del top):
  top=0x1E00000 → floor 0x1340000 (normal); top=0x1FE6000 → floor 0x1526000
  (R40a). El límite inferior es DERIVADO y dependiente de escena.
- `0xAC0000` = tamaño de región en ambos usos (base mínima forward /
  capacidad R). No es "margen de prudencia".

## 6. Tamaño necesario de RuntimeGuestArena (calculado antes del rango)

SMALL OBJECTS (MPEG 32B+0x600; GS ≤ pocos KB transitorios; Font ≈0x14500
persistente; IOP marshalling KBs transitorios; LibC ≈0 para DMC):
≈ 0x20000 con margen.
STACKS de invocación (0x4000 × threads×depth; DMC: main + 1 thread
Sce_scheduler, depth ≤4): ≈ 0x20000. Threads guest con stack==0: ≈0 (DMC pasa
sus propios stacks en el ThreadParam).

    minimum viable  ≈ 0x040000 (256 KB)
    reasonable      ≈ 0x080000 (512 KB)
    conservative    ≈ 0x0C0000 (768 KB)

La arena `[0x9FA000, 0xABE000)` = 0xC4000 cubre el nivel conservador.

## 7. Veredicto sobre la propuesta P3.6

**VALIDADA** (sin cambios de rango):

    base  = 0x009FA000
    limit = 0x00ABE000
    size  = 0x000C4000

Clasificación honesta: **SAFE WITH CONDITIONS** — condiciones: (1) canarios
en la arena verificados en runtime; (2) validación en la primera sesión con
gameplay real (el único UNKNOWN restante); (3) los stacks de invocación async
deben reubicarse DENTRO de la arena (dejar su floor/top actuales pisa los
stacks reales del juego, colisión demostrada en 3).

## 8. Mapa final (delta respecto a P3.6-B)

Cambios: `[0x1F00000, 0x1F30000)` pasa además a RUNTIME CURRENT (pools
RPC/TLS del runtime solapados con staging del juego — a migrar);
`[0x1FFC000, 0x2000000)` es el punto exacto de la primera colisión de stacks
async. El resto del mapa de P3.6 queda confirmado.

## 9. Diseño preliminar P3.7 (sin código)

- **GameHeapState** (nuevo): guarda base/limit declarados por el JUEGO.
  `SetupHeap` escribe SOLO aquí (para DMC: base=0x1F00100, size=0 →
  end=0x1F00100). `EndOfHeap` devuelve `GameHeapState.end` (para DMC:
  0x1F00100 — no `guestHeapLimit()`). Invariante: emular fielmente lo
  declarado, aunque sea capacidad 0.
- **RuntimeGuestArena** (nuevo nombre del guest heap sintético): configurada
  por (1) override por título si existe (DMC: [0x9FA000, 0xABE000)), si no
  (2) heurística: tras el mayor PT_LOAD con contenido real
  (filesz>0), ignorando marcadores (filesz==0 y/o vaddr≥hardLimit), tamaño
  moderado (256KB–1MB), validada contra PT_LOADs y contra GameHeapState.
  SetupHeap NO la toca. Colapso ⇒ log ERROR + fail-fast, nunca base=0
  silencioso.
- **guestMalloc/guestFree**: sin cambios de contrato; sirven desde la arena.
- **reserveAsyncCallbackStack**: floor/top DENTRO de la arena (p. ej. desde
  `arenaLimit` hacia abajo, con partición fija respecto a los bloques de
  guestMalloc), y liberar stacks al drenar invocaciones si es viable.
- **Pools RPC/TLS fijos (State.h)**: migrar a la arena o a rango por título;
  para DMC su ubicación actual colisiona con staging (hallazgo 4).
- **ELF init**: `maxLoadedRdramEnd` solo con segmentos con contenido
  (higiene, estrategia A), pero la arena ya no depende de él si hay override.
- **Invariantes**: base≠0, base<limit, align16, sin wrap, sin intersección
  con PT_LOADs residentes ni [0xAC0000,0x1E00000] ni [0x1E00000,0x2000000);
  fallo de alloc con diagnóstico; canarios.

## 10. UNKNOWNs restantes tras P3.6.1

1. Gameplay real (misiones): high-water forward/R y escrituras a la franja
   candidata — mitigado con canarios; requiere sesión con input.
2. Tamaño máximo del staging 0x1F00000 (mayor fichero UI del disco).
3. Profundidad máxima real de invocaciones anidadas (dimensiona stacks).

Resultado independiente P3.6.1 — RECONCILE_RUNTIME_ARENA_AND_SAFE_RANGE

# P3.7.1.1 — Independent RuntimeGuestArena implementation review

Fecha: 2026-09-10. Rol: reviewer adversarial del diff sin commit de P3.7.1
(implementado por la línea Sonnet). Fuente: `git diff` del submódulo
`vendor/PS2Recomp` + corrida propia del exe + fuentes primarias. La nota
canónica de Sonnet NO se usó como evidencia. Source intacto; sin commit.

## VEREDICTO GLOBAL: PASS WITH CONDITIONS

## Diff auditado

Submódulo `vendor/PS2Recomp`: `ps2_runtime.h` (+63/−…), `ps2_runtime.cpp`
(+390/−…), `MPEG.cpp` (+142, solo instrumentación/diag — sin cambios de
semántica salvo el temporal `fedOk`, equivalente), `Audio.cpp` (+17, diag
preexistente de P3.4, timestamp 09/09 — NO atribuible a P3.7.1).
Repo principal: solo notas de otras sesiones (no leídas) y
`upstream.lock.json`. `EeScheduler.cpp` NO modificado (cache de stacks por
(threadId,depth) sin liberación persiste).

## Verificaciones clave (todas contra código/corrida propia)

1. **Separación real**: `GuestArenaState m_gameHeapState` +
   `m_runtimeArena`, mutexes separados. `configureGuestHeap`/`SetupHeap`
   solo tocan game heap; `guestMalloc/Free/Calloc/Realloc` +
   `reserveAsyncCallbackStack` solo runtime arena; `loadELF` solo escribe
   `suggestedBase` de la arena si `!configured`. Accessors antiguos
   (`guestHeap*`) → game heap; usos restantes = SetupHeap/EndOfHeap ✔.
   Sin aliasing encontrado. Refactor mecánico del allocator revisado
   línea a línea: sin stale references ni cambios de lógica.
2. **Corrida propia** (exe 19:04:47 > fuentes 18:59; FFmpeg ON; env
   override): `[GuestArena:FATAL]` (colapso de GameHeapState por
   `SetupHeap(0x1F00100,0)`) seguido de `[RuntimeGuestArena] using
   explicit override base=0x9fa000 limit=0xabe000`. Escalera E–O completa:
   StrPcmCallBack ENTRY ×8 → `mode=1` → `+0x58: 0→0x3C00→0x5C00→0x6000`
   (deltas exactos del baseline P3.3) → `audioDecIsPreset result=1` →
   `Task_execute(3,0x47A760)` → `MpegMovieDecode ENTRY` →
   `getPicture ENTRY #1`. Chunks del PSS aterrizan en `0x1CB37E0+`
   (R-heap del juego, no en la arena) ✔.
3. **Stall post-getPicture**: reproducido. `getPicture ENTRY` total = 1;
   con 4 frames en cola entra en el gating de presentación
   (`presentationTickForFrame` → `waitVSync(eligibleTick−1, re-call)`);
   la continuación **nunca se ejecuta** (no hay ENTRY #2), CPU activa
   (livelock del scheduler esperando un vsync que no avanza). Ese camino
   NO está tocado por el diff (hunks de MPEG.cpp = contadores), la arena
   está sana en el momento del stall (0 mallocfail/FATAL/exhaustion).
   **Clasificación: NEW POST-P3.7.1 DIVERGENCE, no regresión** — código
   latente nunca antes alcanzado. Investigación detenida ahí.

## Hallazgos (condiciones)

C1 (**DEBE arreglarse — el más grave**): el **fallback automático** sin
env vars produce, con la nueva heurística, arena =
`[0x9F9480, 0x1F00000)` (~21 MB) — solapa `Mem_alloc` (0xAC0000+),
`Mem_alloc_R` (0x1340000–0x1E00000) y el scratch fijo del juego
(0x1E00000–0x1F00000). El first-fit desde la base disimula la colisión
mientras el uso vivo sea < ~0xC6B80. Peligro silencioso para cualquier
título sin override (incluido DMC si las vars faltan).

C2 (**DEBE**): env inválida/incompleta ⇒ log "[…FATAL]" **+ fallback a
C1** — es loud-failure, NO fail-fast. Un typo en la env degrada en
silencio a la heurística colisionante. Debe abortar (o negarse a servir
allocations).

C3 (SHOULD): semántica de SetupHeap/EndOfHeap: `SetupHeap(base,0)` es una
declaración legítima (heap C de capacidad 0; en PS2 real
`EndOfHeap→base=0x1F00100`). La implementación la representa como colapso
`base=0,limit=0` + log FATAL en cada boot de DMC, y `EndOfHeap` devuelve
0. Funcionalmente inocuo para DMC (brk nunca se mueve; malloc además está
HLE-ado), pero la representación es incorrecta en principio.
GameHeapState debería guardar fielmente base/end declarados.

C4 (posterior, no bloqueante): pools fijos del runtime
RPC `[0x1F00000,0x1F20000)`, TLS `[0x1F20000,0x1F30000)`, BootMode
`[0x1F30000,0x1F31000)` siguen dentro de la ventana de staging de DMC
(Game_over/Ending/Pause/Easy la cargan vía CdFileRead). No está en la
cadena causal actual (attract no abre esas pantallas) — problema
independiente a tracked antes de gameplay/menús profundos.

C5 (RIESGO): stacks de invocación (0x4000) jamás liberados; capacidad
≈49 en 784 KiB si la arena estuviera vacía; DMC ≈2 threads × depth baja
⇒ 64–128 KiB permanentes. Aceptable DMC; caveat general.

C6 (menores): `ensureGameHeapInitializedLocked` es código muerto (título
sin SetupHeap deja GameHeapState unconfigured y EndOfHeap devuelve
suggestedBase); límite del override sin chequeo de alineación; log de
override no imprime la dirección del primer stack (dificulta el punto F
de la escalera — recomendado 1 línea de log en reserveAsyncCallbackStack).

## Clasificaciones pedidas

- Único allocator para objetos+stacks: **CORRECTO PERO SUBÓPTIMO**
  (overlap imposible por construcción ✔; fragmentación/no-liberación como
  riesgos de largo plazo).
- Heurística ELF `vaddr < hardLimit`: **PASS para su propósito** (la
  arena/gameheap también están capadas por hardLimit; BSS grande con
  filesz=0 sigue contando; segmento que cruza el límite ⇒ colapso ruidoso,
  no colisión silenciosa). El riesgo general real no es la exclusión sino
  el TAMAÑO del fallback (C1).
- Primer async stack: no logueado; derivación por first-fit (work area
  MPEG 0x600 en 0x9FA000 + cb 32B) ⇒ bloque ≈`[0x9FA620,0x9FE620)`,
  SP≈`0x9FE610` — dentro de la arena, lejos de 0xABE000; valor EXACTO
  pendiente de 1 línea de log (C6). Invariante ∈ arena garantizado por
  construcción (mismo free-list).
- Fail-fast: **SOLO LOG** (ver C2).
- Canarios: ausentes; aceptable para esta iteración (attract/movie
  verificado); necesarios antes de declarar seguridad en gameplay
  (MANUAL VALIDATION REQUIRED — sin ViGEmBus/foco no se alcanza).

Resultado independiente P3.7.1.1 — INDEPENDENT_RUNTIME_GUEST_ARENA_IMPLEMENTATION_REVIEW
