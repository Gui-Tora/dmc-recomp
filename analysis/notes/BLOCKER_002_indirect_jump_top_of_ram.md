# Segundo bloqueo real — salto indirecto a dirección fuera de RAM

- ELF: `SLES_503.58`, Devil May Cry (Europa/PAL) v1.02. sha256
  `d0753a6b3b2f00802a50758a872d8cf051725aa31c839aa8894eee30ce58bab4`,
  crc32 `77654AD2`, entry `00100008`.
- Entorno: mismo build que resolvió `BLOCKER_001` (CSV de `entry` extendido a
  `0x1000A8`). Reproducción: `python scripts/pipeline.py run --seconds 60`.
  Log completo: `logs/1788867429655652600-run.log`.

## Último hito demostrado y evidencia

Avance grande respecto a `BLOCKER_001`: la ejecución ya no se queda en el
arranque. Llega a cargar módulos IOP reales vía SIF (`SIO2MAN.IRX`,
`PADMAN.IRX`, `MCMAN.IRX`, `MCSERV.IRX`, `LIBSD.IRX`, `SDRDRV.IRX`,
`MODHSYN.IRX`, `MODMIDI.IRX`, `MODMSIN.IRX`, `TSNDDRVM.IRX`, `CDMODULE.IRX`),
hace llamadas RPC reales (algunas `[IOP/RPC trace:unhandled]`, sin handler
pero sin crashear), inicia una transferencia DMA real (`sceDmaSend`), y
completa dos operaciones de memory card (`[MC] GetInfo ... result=0`,
`[MC] Sync cmd=1 result=0`). Entra después en un bucle real de
render/texto (`Print_region_ck` → `fptoui`/`__unpack_f` → `AddPrim`, repetido
varias veces) antes de bloquearse.

## Primer error significativo; PC/RA/argumentos observados

```
[guest-branch:missing-target] kind=IndirectJump op=JR
  source=0x2e11c8 target=0x2000100 pc=0x2000100 ra=0x2e1540
  sp=0x1ffbe70 gp=0x591870
  a0=0x7a a1=0x13b8858 a2=0x13b8858 a3=0x13bb048
  s0=0x981240 s1=0x2586874 v0=0x2000100 v1=0x586740
  codeRegion=no
```
A diferencia de `BLOCKER_001` (salto directo a dirección fija), este es un
`JR` (salto indirecto vía registro) — el destino `0x2000100` es un valor
calculado en tiempo de ejecución, no una constante del binario.
`0x2000100 = 0x2000000 + 0x100`, y `0x2000000` son exactamente los 32MB de
RAM principal de la PS2 — es decir, el salto apunta justo 256 bytes más allá
del final físico de la memoria. `codeRegion=no` confirma que esa dirección no
está mapeada como código en absoluto.

## Función/rango en Ghidra y callers

La propia instrucción origen (`0x2e11c8`) **tampoco está cubierta por
ninguna función en el CSV** — hay un hueco entre `FUN_002e1520`
(`0x2E1520`-`0x2E1528`) y `Print_message_end` (`0x2E1640`), y `0x2e11c8` cae
en un hueco anterior a `FUN_002e1480` (sin identificar todavía su inicio
exacto). Mismo síntoma raíz que `BLOCKER_001` (función/rango no exportado por
Ghidra), pero aquí el propio salto que falla vive en zona no traducida, así
que ni siquiera hay código recompilado ejecutándose ahí — algo más arriba en
la cadena de llamada saltó a esa dirección y de ahí sale el `JR` roto.

Traza de llamada previa (valores repetidos antes del fallo, de más antiguo a
más reciente):
```
0x2e1740 (Print_region_ck) -> 0x11d9a8 (fptoui) -> 0x11d018 (__unpack_f)
  -> 0x11d9a8 -> 0x11d018 -> 0x16efb0 (AddPrim) -> 0x16efb0 -> 0x16efb0
  -> [se repite el ciclo completo varias veces]
```
`ra=0x2e1540` (dirección de retorno) tampoco está cubierta por ninguna
función del CSV (mismo hueco entre `FUN_002e1520` y `Print_message_end`).

## Hipótesis y evidencia que la distingue de `BLOCKER_001`

Dos causas posibles, no excluyentes:

1. **Hueco de cobertura (como BLOCKER_001)**: si `0x2e11c8` y `0x2e1540`
   estuvieran correctamente traducidos (extendiendo los límites de función
   correspondientes, como se hizo con `entry`), el `JR` podría resolver a un
   destino válido real en vez de ejecutar bytes no traducidos/mal
   interpretados como código.
2. **Valor de registro genuinamente corrupto**: `v0=0x2000100` (el valor
   saltado) podría venir de un puntero/función callback nunca inicializado
   correctamente — plausible dado que varias llamadas RPC del IOP en esta
   misma ejecución quedaron `unhandled` (sin implementación real), y alguna
   de ellas podría ser responsable de escribir la dirección de retorno o
   callback que este código está leyendo.

No se puede distinguir entre las dos sin corregir primero el hueco de
cobertura en `0x2e11c8`/`0x2e1540` y volver a intentar — igual que con
`BLOCKER_001`, el primer paso siempre es tener la traducción real antes de
sospechar de un bug de emulación.

## Investigación adicional (2026-09-08) — causa raíz precisada

La función real que contiene `0x2e11c8` es **`Print_message`**
(`0x2e0ed0`-`0x2e1638`, 1896 bytes) según la `.symtab` real del ELF — ni el
CSV de Ghidra actual ni el nuevo (sin R6) la delimitan correctamente; ambos
la tienen tapada por una función gigante y erronea distinta (`sceVu0MulMatrix`
en el CSV actual, `Movie_on` en el nuevo — ver comparación completa en
`analysis/local/csv_comparison_report.txt`).

Aislado `0x2e0ed0`-`0x2e1638` con `ps2_recomp` (rabbitizer). El `JR` es el
final de una **tabla de saltos real** (switch compilado):
```
0x2e11b4: lui   $v1, 0x58          ; v1 = 0x580000
0x2e11b8: addiu $v1, $v1, 0x6740   ; v1 = 0x586740 (base de tabla)
0x2e11bc: sll   $v0, $v0, 2        ; v0 = indice*4
0x2e11c0: addu  $v0, $v0, $v1      ; v0 = direccion de la entrada
0x2e11c4: lw    $v0, 0x0($v0)      ; v0 = *(tabla[indice])
0x2e11c8: jr    $v0
```
El indice esta acotado a `<0xC` (12) por un `sltiu` justo antes (si no,
salta fuera de la tabla). Leidas las 16 primeras entradas directamente del
ELF estatico (offset de archivo `p_offset=0x280` para `p_vaddr=0x100000`):
todas son direcciones sanas dentro de la propia `Print_message`
(`0x2e11d0`...`0x2e1254`, `0x2e1054`...`0x2e1170`) — **ninguna vale
`0x2000100`**. Esto descarta que sea un dato legitimo del juego: la
corrupcion es real, ocurre en tiempo de ejecucion.

El log del crash mostraba `v1=0x586740` — coincide exacto con la base de
tabla calculada aqui, asi que el registro base estaba bien. La corrupcion
esta en el **indice** usado para el calculo de la entrada, o en la
**lectura de memoria** de esa direccion — no se ha aislado cual de las dos
todavia.

**Sobre la coincidencia `0x1F00100 + 0x100000 = 0x2000100`** (direccion del
marcador `heap` del linker + direccion de carga del ELF): numericamente
exacta, pero sin mecanismo causal probado con esta evidencia. Ni confirmada
ni descartada — pendiente de instrumentar el punto exacto (capturar indice
real y direccion efectiva de lectura en el momento del fallo, mismo patron
que la instrumentacion de los 20 stubs VU0) para saber si el indice se
desborda hacia una zona relacionada con el heap, o si es la lectura de
memoria la que devuelve contenido de otra region.

## FASE A — instrumentación del crash-handler (2026-09-08) — RESUELTO: memoria corrupta

Parche temporal en `vendor/PS2Recomp/ps2xRuntime/src/lib/ps2_runtime.cpp`
(handler de `[guest-branch:missing-target]`, ver comentario "TEMPORARY
diagnostic instrumentation for BLOCKER_002" en el código): cuando
`sourcePc == 0x2e11c8`, vuelca 16 uint32 LE desde `0x586740` y los compara
contra los valores conocidos del ELF estático. Build incremental (solo
recompiló `ps2_runtime.cpp` + relink, ~2 min) sobre el baseline symtab-first
promovido, sin tocar `recomp/generated`.

Resultado — **B: la tabla en RAM NO coincide con el ELF, en ninguna de las
16 entradas**:

```
t[0]=0x813b87e8/esperado=0x2e11d0   t[8]=0x02000100/esperado=0x2e1230  <-- == v0 del crash
t[1]=0x64002b16/esperado=0x2e11dc   t[9]=0x7af00d00/esperado=0x2e123c
t[2]=0x0098d0f0/esperado=0x2e11e8   t[a]=0x0000ffff/esperado=0x2e1248
t[3]=0x00000000/esperado=0x2e11f4   t[b]=0x813b87f8/esperado=0x2e1254
t[4]=0x80606060/esperado=0x2e1200   t[c]=0x64002b16/esperado=0x2e1054
t[5]=0x00000000/esperado=0x2e120c   t[d]=0x0098d0f0/esperado=0x2e107c
t[6]=0x7ef00d00/esperado=0x2e1218   t[e]=0x00000000/esperado=0x2e10e8
t[7]=0x0000ffff/esperado=0x2e1224   t[f]=0x80606060/esperado=0x2e1170
```

Dos hallazgos clave:

1. **`t[8] = 0x02000100` coincide exacto con `v0` del crash original.**
   Confirma que el índice usado fue **8** y que `0x586740 + 8*4 = 0x586760`
   es la dirección efectiva correcta — el cálculo de índice/dirección NO es
   el problema (descarta A: índice corrupto, B: dirección efectiva
   incorrecta de la lista de hipótesis original). El problema es que la
   memoria en esa dirección ya no contiene la tabla del ELF.
2. **Patrón de repetición con periodo 11 words (44 bytes)**:
   `t[1]==t[c]=0x64002b16`, `t[2]≈t[d]≈0x0098d0f0`, `t[7]==t[a]=0x0000ffff`,
   `t[4]==t[f]=0x80606060`. No es basura aleatoria — es contenido real de
   otra estructura que se solapó ahí. `t[1]/t[c]=0x64002b16` coincide EXACTO
   con `s0[c]` del log del crash original (`s0=0x981240`, la dirección que
   lee el prólogo de `Print_message` — ver `OBJECT_TABLE_0x0110e720.md` para
   el contexto de esa zona de trabajo/tablas `t_*`).

**Veredicto: C — memoria de la tabla sobrescrita/corrupta.** No hace falta
ya la Fase B (instrumentar el índice antes del `sll`) — el índice ya se
conoce (8) y el cálculo de dirección es correcto.

## FASE C — watchpoint acotado a 0x586740-0x58677f (2026-09-08)

Objetivo: encontrar la primera escritura que transforma la tabla correcta
en los datos corruptos, sin füll rebuild (cero regenerate, cero build de
los 10127 `.cpp` generados — solo runtime afectado + relink).

Restricción dura: el camino de escritura por defecto de las instrucciones
`sw`/`sq`/etc. del código recompilado normal pasa por
`WRITE8/16/32/64/128` → `FAST_WRITE*` → `Ps2FastWrite8/16/32/64/128`
(`static inline` en `ps2_runtime_macros.h`), cabecera incluida por los
10127 `.cpp` generados. Tocarla forzaría una recompilación completa —
explícitamente prohibido esta fase. Por eso se instrumentaron primero los
mecanismos de "escritura en bloque" (memcpy/DMA/SIF/RPC/segment-init) que
viven en archivos `.cpp`/cabeceras NO compartidas con el código generado.

Se añadió una función de diagnóstico (`diagBlocker002TableCheck` /
`diagBlocker002LoaderCheck`, ver comentarios "TEMPORARY diagnostic
instrumentation for BLOCKER_002" en el código) que vuelca las 16 entradas
de la tabla y compara contra el valor esperado del ELF, antes y después de
cada candidato, solo cuando el rango destino intersecta
`[0x586740, 0x586780)`. Tres candidatos probados, los tres build
incrementales (1 a 9 archivos + relink, sin tocar `recomp/generated`):

1. **`Copy` (syscall, `System.cpp`)** — memcpy genérico guest→guest
   (`Copy@0x002032B8`, ya en la lista de `stubs`). **Negativo**: cero
   invocaciones con destino en el rango en toda la ejecución hasta el
   crash.
2. **`copyGuestByteRange` (`Kernel/Stubs/SIF.cpp`)** — usado por
   `sceSifGetOtherData` (entrega de payload RPC) y por el camino tipo
   `sceSifSetDma` (transferencias DMA vía SIF). **Negativo**: cero
   invocaciones con destino en el rango, en ninguno de los dos call sites,
   en toda la ejecución hasta el crash.
3. **`loadElfIntoGuestMemory` / `SifLoadElfPart` (`Kernel/Syscalls/Helpers/Loader.h`)**
   — carga segmentos `PT_LOAD` de un ELF arbitrario (auxiliar/overlay)
   directamente en RAM invitada en tiempo de ejecución, sin comprobar
   solapamiento con memoria ya en uso; candidato fuerte por el patrón de
   "contenido de otra estructura" visto en FASE A. **Negativo**: la
   función no se invocó ni una vez con rango tocando la tabla (posible que
   ni siquiera se invoque en esta ejecución antes del crash).

También se revisó (sin necesidad de build, por rango de direcciones)
`writeGuestKernelWord`/`initializeEeKernelState` (`System.cpp`,
`ps2_runtime.cpp`): ambos limitados a direcciones `< 0x80000`
(`kGuestSyscallMirrorLimit`), muy por debajo de `0x586740` — descartados
por inspección, no hizo falta instrumentar.

Las tres ejecuciones reprodujeron el crash original de forma idéntica
(`source=0x2e11c8 target=0x2000100 ra=0x2e1540 v0=0x2000100 v1=0x586740`),
confirmando que la instrumentación no altera el comportamiento observado.

**Conclusión de FASE C**: descartados todos los mecanismos de "escritura en
bloque" (memcpy/DMA/SIF/RPC/segment-init) accesibles sin tocar la cabecera
compartida. La escritura culpable, con alta probabilidad, viene de una
instrucción `sw`/`sh`/`sb`/`sq` **ordinaria** del código recompilado del
propio juego, pasando por `FAST_WRITE*` — el único camino que queda sin
poder instrumentarse de forma barata.

## FASE C.1 — estado justo tras cargar el ELF principal (2026-09-08)

Instrumentado `PS2Runtime::loadELF` (`ps2_runtime.cpp`), justo después de
que terminan de cargarse todos los segmentos `PT_LOAD` y antes de arrancar
el hilo del juego: vuelca y compara las 16 entradas de `0x586740` contra el
valor esperado del ELF. Build incremental (solo `ps2_runtime.cpp` + relink).

Resultado:
```
[BLOCKER_002:initial-table] INTACT
```

**Confirma el caso B**: la tabla es correcta inmediatamente después de la
carga del ELF. La corrupción ocurre después, durante la ejecución —
descartado definitivamente cualquier bug de carga/mapeo del ELF principal.
Pasamos a FASE C.2 (watchpoint a nivel de Windows) según lo decidido.

## FASE C.2 — watchpoint a nivel de página (PAGE_READONLY + VEH) — CAUSA RAÍZ LOCALIZADA

Implementado en `vendor/PS2Recomp/ps2xRuntime/src/lib/Kernel/Diagnostics/Blocker002Watchpoint.cpp`
(archivo nuevo, aislado, recogido automáticamente por el glob
`CONFIGURE_DEPENDS` de `Kernel/*.cpp` en `CMakeLists.txt` — no hizo falta
tocarlo a mano). Mecanismo: `VirtualProtect(PAGE_READONLY)` sobre la(s)
página(s) host que contienen las direcciones vigiladas +
`AddVectoredExceptionHandler`; en cada violación de escritura que
intersecta el rango vigilado, se resuelve el símbolo/línea del RIP
culpable vía DbgHelp y se hace un stack-unwind manual (`RtlLookupFunctionEntry`
+ `RtlVirtualUnwind`, el mismo mecanismo que usa el SEH de Windows) para
saltar por encima de rutinas opacas de la CRT (`memcpy`/`_NLG_Return2` en
`VCRUNTIME140D.dll`, sin símbolos útiles) y encontrar la función real
llamante. Tras registrar el evento, se quita la protección, se activa el
flag TF (single-step) para dejar pasar exactamente esa instrucción, y se
vuelve a proteger la página — así no se altera el comportamiento del resto
de escrituras legítimas en la misma página de 4KB. Solo build incremental
(1 `.cpp` nuevo + relink); cero recompilación de código generado.

**Resultado — culpable identificado con precisión de instrucción**:

```
[BLOCKER_002:VEH-write:table@0x586740] guestAddr=0x586740 ...
stack=[..._NLG_Return2+0x4fc[VCRUNTIME140D.dll]
  <- Ps2FastWrite32+0xca(ps2_runtime_macros.h:279)[dmc-recomp.exe]
  <- Print_message_0x2e0ed0+0x105bc(Print_message_0x2e0ed0.cpp:1308)[dmc-recomp.exe]
  <- EeScheduler::run+0x96e(EeScheduler.cpp:297)[dmc-recomp.exe] <- ...]
```

La propia `Print_message` (la MISMA función cuyo `JR` final falla) escribe
sobre su propia tabla de saltos. Instrucción exacta en el `.cpp` generado
(`Print_message_0x2e0ed0.cpp:1306-1308`, PC guest `0x2e131c`):

```cpp
// 0x2e1318: lui $v0, 0x8000       -> v0 = 0x80000000
// 0x2e131c: sw  $v0, 0x0($s1)     -> WRITE32(s1 + 0, v0)
WRITE32(ADD32(GPR_U32(ctx, 17), 0), GPR_U32(ctx, 2));
```

`$s1` (registro 17) vale `0x586740` en ese punto — la misma base de la
tabla. Rastreado hacia atrás en el mismo archivo: `$s1` se carga UNA vez al
entrar en la función (`Print_message_0x2e0ed0.cpp:105`):

```cpp
SET_GPR_S32(ctx, 17, (int32_t)FAST_READ32(0x98D098u));
```

`0x98D098` es una variable **BSS** (confirmado leyendo los program headers
del ELF: cae en la región `memsz > filesz` del segmento 0, zero-init por
el loader — no viene con valor de fichero). Ningún otro `SET_GPR..17` toca
el registro entre la línea 105 y la 1308 en el propio archivo generado, así
que el valor de `$s1` usado en la escritura viene directamente de esa
lectura, salvo que una llamada anidada lo reescriba sin restaurarlo.

**Segundo watchpoint sobre `0x98D098`**: se añadió una segunda región
vigilada (mismo archivo, mismo mecanismo) para ver quién escribe ese
global. Resultado: el global se escribe repetidamente durante una sola
invocación de `Print_message` — no es una variable de un solo uso, sino un
**cursor compartido** (probablemente "posición actual de escritura en la
lista de primitivas GS") que se lee/actualiza también desde `AddPrim`
(`AddPrim_0x16efb0.cpp:61`, llamada anidada vía `dispatchGuestBranch` desde
dentro de la propia `Print_message`) y desde otras invocaciones anidadas de
`Print_message` (cadena real observada:
`ps2_main → Main_draw_clear → Cockpit_sys_init → Print_message_init` para
la inicialización, y `ps2_main → ... → GetCardInfo → get_info00 →
CardMesPrint → Print_message → AddPrim` para las actualizaciones
posteriores). En dos ejecuciones distintas, los valores observados en el
global oscilan entre punteros de aspecto sano (`0x981248`, `0x80988124`,
`0x8098f888`, todos en/cerca de la región `0x98xxxx`/`0x80988xxx`ya
señalada en `OBJECT_TABLE_0x0110e720.md`) y `0x0`/`0x80000000` — nunca se
capturó el instante exacto en que vale `0x586740` en ninguna de las dos
ejecuciones (la ejecución es multihilo y no determinista en el timing; el
registro `$s1` usado en la escritura culpable es una **copia local** tomada
al entrar en la función, así que puede quedar desactualizado/corrupto
respecto al valor "actual" del global en el momento del crash sin que eso
se refleje directamente en el propio global).

**Hipótesis más probable**: `Print_message` es reentrante (se llama a sí
misma anidada, y llama a `AddPrim` que también toca el mismo cursor
`0x98D098`), y `$s1` es un registro *callee-saved* en la convención MIPS —
si alguna llamada anidada (`AddPrim` u otra) no preserva `$s1` correctamente
alrededor de su propio uso del registro 17 (ya sea un bug real del juego
original, poco probable dado que en hardware real nunca coincidiría con la
dirección de su propia tabla, o un bug de traducción de PS2Recomp que
pierde/desordena una instrucción `sw $s1,offset($sp)` / `lw $s1,offset($sp)`
de guardado/restauración alrededor de la llamada anidada), la instancia
externa de `Print_message` retoma la ejecución con un `$s1` obsoleto o
corrupto tras la llamada a `AddPrim`, que en la ejecución observada
coincide numéricamente con `0x586740`.

## FASE D.1 — confirmación matemática del mecanismo (2026-09-08)

Corrección del usuario a la hipótesis previa: `$s1` es *callee-saved* en la
ABI MIPS — si `AddPrim` lo tocara, sería responsabilidad de `AddPrim`
preservarlo, no de `Print_message`. Verificado leyendo `AddPrim_0x16efb0.cpp`
completo (función hoja de 12 instrucciones, 0x16efb0-0x16efe0): **no toca
el registro 17 en absoluto** — solo usa `$a0-$a3,$t0,$v1,$ra`. La escritura
a `0x98D098` vista antes en el watchpoint venía de `$a1` (parámetro,
dirección del cursor), no del registro `$s1`.

Instrumentados los límites de llamada reales (`Print_message`→`AddPrim`)
vía `runtime.registerFunction()` + llamada-a-través (mismo mecanismo que
`dmc_overrides.cpp` usa para los 20 stubs VU0), en
`runtime/dmc_overrides.cpp` (archivo del proyecto, no vendor). Resultado de
una ejecución completa hasta el crash:

- `initialS1` (al entrar en la única invocación de `Print_message`) =
  `0x00981248`.
- Última llamada a `AddPrim` antes del crash: iteración **2.003.332**,
  `s1Before=0x02586848`.
- **`(0x02586848 - 0x00981248) mod 2^32 = 29.382.144 = 667.776 × 44`
  EXACTO (resto 0)** — patrón de 3 llamadas a `AddPrim` por carácter
  confirmado (`2003332/3 ≈ 667.777`).
- 0 casos de `add-prim-return-MISMATCH` (ver código): `$s1` nunca cambia
  entre el `s1Before` logueado y el valor tras `AddPrim` — confirma
  matemáticamente que `AddPrim` nunca modifica el registro.
- `s1_inicial + 667.777×44 = 0x02586874`; enmascarado a 32MB de RAM
  (`& 0x1FFFFFF`) = `0x586874` — coincide (a 0x134 bytes) con el punto de
  la escritura culpable, y coincide *exactamente* con `s1=0x2586874` visto
  en el primerísimo volcado de crash de toda la investigación (antes de
  cualquier instrumentación).

**Conclusión: `$s1` avanza puramente por el incremento local de +44/carácter
dentro de `Print_message`; no hay ninguna otra modificación oculta.** El
mecanismo de corrupción no es un desbordamiento de un buffer pequeño en
línea recta (matemáticamente inviable, como señaló el usuario — haría
falta un valor inicial mucho más alto para llegar por simple suma) sino
**wraparound de direccionamiento**: `$s1` crece sin ningún límite mucho más
allá del final físico de los 32MB de RAM (`0x2000000`), y al usarse como
dirección de escritura se enmascara (`& 0x1FFFFFF`), envolviendo de vuelta
al principio del espacio de 32MB y aterrizando por aritmética modular sobre
la propia tabla de saltos de `Print_message`.

El bucle de `Print_message` (`label_2e1170`, línea 746) solo termina al leer
el carácter `0x7E` (`beql $a0,$v1` con `$v1=0x7E`) — no en un terminador
nulo. Con ~667.777 "caracteres" procesados sin encontrar `0x7E`, la causa
raíz real deja de ser la tabla de saltos y pasa a ser: **¿por qué la cadena
apuntada por `$s6` no contiene un `0x7E` durante cientos de miles de
bytes?**

## FASE E — origen del puntero de cadena en `$s6` (2026-09-08)

`$s6` se carga en `Print_message_0x2e0ed0.cpp:75` desde otro global BSS,
`0x981240` (la misma dirección vista como `s0=0x981240` en el primer
volcado de crash de la sesión). Búsqueda de escritores: solo
`CardMesPrint_0x217a80.cpp` lo escribe (línea 115), con esta aritmética
completa (leída directamente del código generado, instrucción por
instrucción):

```
tableBase   = *(uint32_t*)0x883238           // otro puntero global
messageId   = a2 & 0xFFFF                    // parámetro de CardMesPrint
entryOffset = *(uint32_t*)(tableBase + messageId*8)
s6Value     = tableBase + entryOffset - 4
*(uint32_t*)0x981240 = s6Value
```

Instrumentada la entrada de `CardMesPrint` (mismo mecanismo
registerFunction + call-through) para capturar en vivo:

```
a0=0x28 a1=0xBE a2=0x0 a3=0x8837B8 messageId=0x0
tableBase(0x883238)=0x01E00004 entryOffset=0x00000000
predicted_s6=0x01E00000
```

Coincide exacto con lo visto en `Print_message` (`initialS6=0x01E00000`).
**`messageId=0`** (no un ID inválido/fuera de rango evidente) y
**`tableBase=0x01E00004` NO es cero ni basura obvia** — es un puntero con
pinta legítima, ya inicializado. El problema no está en la inicialización
del puntero de tabla en sí.

**Volcado de memoria alrededor de `$s6`** (`getMemPtr` byte a byte,
0x1DFFF00-0x1E10000, ~65KB): **completamente cero salvo un único byte no
cero en todo el rango** (`0x1E00003 = 0x80`). Ningún `0x7E` en 65KB. Esto
confirma sin ambigüedad que **el buffer de texto en `0x1E00000` nunca se
rellenó con datos reales** — no es un problema de longitud de cadena ni de
terminador incorrecto, es un buffer vacío.

**Búsqueda del escritor de `0x883238`**: `grep` literal no encontró ningún
escritor en los 10127 `.cpp` generados (solo el `READ32` de `CardMesPrint`)
— indicio de direccionamiento `$at`-relativo (`lui`+`ori`/`addiu`), no
detectable por búsqueda de texto. Se añadió una tercera región al
watchpoint VEH existente (`Blocker002Watchpoint.cpp`, sin tocar ninguna
cabecera compartida ni código generado) vigilando `0x883238`. Resultado:
**una única escritura en toda la ejecución**, desde
`GetCardInfo_0x219160.cpp:818` — código de juego real y genuino (no un
stub ni una ruta IOP/RPC/SIF/DMA). Aritmética completa reconstruida del
código generado (`0x2192b8`-`0x2192ec`):

```
v1 = 0x1FA0000                      // lui
a0 = v1 | 0x1000                    // (campo no relacionado en s0+0x6C)
v1 = v1 | 0x2000                    // (campo no relacionado en s0+0x68)
v1 = READ32(s0 + 0x70)              // *** base real de la tabla de recursos ***
v0 = s1 << 2                        // s1 = índice (idioma/estado?)
v0 = v0 + v1                        // índice*4 + base
v0 = READ32(v0 + 16)                // offset relativo, tabla anidada
v0 = v1 + v0                        // base + offset
v0 = v0 + 4
*0x883238 = v0                      // == 0x01E00004 en esta ejecución
```

Es decir: `tableBase_final = *(s0+0x70) + *(*(s0+0x70) + s1*4 + 16) + 4`.
Patrón estándar de tabla de recursos con offsets relativos (típico de
tablas de texto/fuente localizadas). **No hay indicio de bug de
recompilación aquí** — es una traducción directa y coherente del MIPS
original.

## FASE E.1 — semántica real de 0x7E (2026-09-08) — CORRECCIÓN IMPORTANTE

**`0x7E` NO es el terminador del mensaje.** Verificado leyendo el propio
código generado (derivado de rabbitizer/PS2Recomp, no de Ghidra) al
completo, ambos lados del branch de `label_2e1170`:

- `label_2e0fd0` (cabeza real del bucle, línea 280) lee el byte crudo en
  `$s6`, calcula `byte-5`, y si `(byte-5) < 15` (sin signo) — es decir,
  **`byte` en `[0x05, 0x13]`** — despacha a una **segunda tabla de saltos**,
  distinta de la de `0x586740`, ubicada en **`0x586770`** (11 destinos
  distintos para 15 códigos de control). Si el byte NO está en ese rango,
  cae a `label_2e1170` (carácter normal).
- En `label_2e1170`, el check contra `0x7E` (`beql $a0,0x7E`) solo hace
  **saltar ese byte sin imprimirlo** (incrementa `$s6` en 1, vuelve a
  `label_2e0fd0`) — es un código "invisible/skip", no un terminador.
- La condición de salida REAL está unas líneas más abajo en
  `label_2e1170` (línea 772-787): lee un contador/flag en `sp+0xD0`, y si
  es `0`, salta a `label_2e15e4` → epílogo real (`label_2e1608`,
  restauración de `$s0-$s7/$fp/$ra` + `jr $ra`, línea 2129).
- Ese flag `sp+0xD0` se pone a `0` únicamente en **`label_2e1040`**
  (línea 417-418: `WRITE32(sp+0xD0, 0)` seguido de `goto label_2e1170`).

**Leída la segunda tabla de saltos (`0x586770`) directamente de bytes ELF
estáticos** (no runtime, no corrupta — tabla distinta de la de
`0x586740`), los 15 códigos de control resueltos:

| código | destino | rol |
|---|---|---|
| 0x05 | `label_2e1054` | control (color/posición, vuelve a 2e1170) |
| 0x06 | `label_2e107c` | control |
| 0x07 | `label_2e10e8` | control |
| 0x08 | `label_2e1170` | no-op (carácter normal) |
| 0x09 | `label_2e1048` | control (consume 2 bytes) |
| 0x0a,0x0b | `label_2e1170` | no-op |
| 0x0c | `label_2e1000` | control |
| 0x0d | `label_2e1024` | control |
| **0x0e** | **`label_2e1040`** | **TERMINADOR REAL — pone sp+0xD0=0** |
| 0x0f,0x10 | `label_2e1170` | no-op |
| 0x11 | `label_2e10a4` | control |
| 0x12 | `label_2e10f0` | control |
| 0x13 | `label_2e1130` | inyecta secuencia sintética `0x0C,0x7E,0x07` |

**El terminador real del formato de `Print_message` es el byte de control
`0x0E`, no `0x7E`.** Esto no cambia la conclusión de FASE E (el buffer en
`0x1E00000` sigue estando vacío — 65KB de ceros con un único byte suelto)
pero corrige el mecanismo exacto: como `0x00` no es un código de control
(está fuera de `[0x05,0x13]`), cae siempre por la rama de "carácter
normal", nunca dispara la búsqueda de `0x0E`, y el bucle nunca encuentra
la condición de salida real — consistente con, y reforzando, el hallazgo
de que el buffer nunca se rellenó con contenido real (que sí debería
contener un byte `0x0E` en algún punto si fuera un mensaje válido).

No se ha verificado todavía (puntos 5-6 de FASE E.1, no bloqueantes): otras
funciones de impresión que compartan este mismo formato de control-codes, o
ejemplos de texto estático real en el ELF terminando en `0x0E`. Se pospone
por prioridad — la corrección del mecanismo ya está confirmada con fuente
primaria (bytes ELF), y no cambia el diagnóstico de fondo.

## Estado y próximo paso pendiente de decidir

Cadena de causalidad completa hasta ahora:

```
0x586740 (tabla de saltos) corrupta
  <- $s1 hace wraparound de 32MB tras ~667.777 incrementos de +44
    <- Print_message nunca encuentra 0x7E en la "cadena" de $s6
      <- $s6 = 0x1E00000, buffer vacío (65KB de ceros, un byte suelto)
        <- $s6 viene de CardMesPrint: tableBase(0x883238) + offset(messageId=0) - 4
          <- 0x883238 lo escribe GetCardInfo (código real, no stub):
             *(s0+0x70) + *(*(s0+0x70)+s1*4+16) + 4
            <- PENDIENTE: origen de *(s0+0x70) (la base real de la tabla de
               recursos) y de s1 (el índice) — no investigado todavía.
```

Preguntas abiertas para continuar: (1) ¿qué es `s0` en `GetCardInfo` (qué
struct, de dónde sale, ¿parámetro o global?) y qué contiene realmente
`*(s0+0x70)` — ¿un puntero a un recurso que debería haberse cargado desde
el CD/IOP y nunca se cargó, o un puntero ya corrupto/cero-derivado?; (2)
qué es el índice `s1` en ese punto (¿id de idioma, código de estado de
memory card?) y si su valor es el esperado; (3) si el recurso de texto en
sí (whatever `*(s0+0x70)` apunta a, en última instancia el mismo buffer de
0x1E00000) se carga vía un mecanismo de archivo/CD/IOP que podamos
cross-referenciar con los `[SIF module]`/`[IOP/RPC trace:unhandled]` ya
vistos — sin asumir la relación todavía (las direcciones RPC vistas en el
log, `0x87dac0/0x9e5500/0x9e5540/0x7407e0`, no coinciden con
`0x883238`/`0x1E00000`, así que de momento NO hay conexión de datos
demostrada con los RPC unhandled).

## FASE E.2 — rastreo de la tabla de recursos (2026-09-08) — RESUELTO: constante hardcodeada

`$s0` en `GetCardInfo` (verificado en el prólogo, `0x219170-0x219174`):
**dirección global fija `0x8837A0` (`lui 0x88; addiu +0x37A0`), constante en
tiempo de compilación** — no argumento, no struct pasada, no resultado de
otra función.

Capturados en vivo (vía el mecanismo VEH ya existente, con acceso directo
al `R5900Context` real — ver corrección de bug más abajo) los registros
exactos en el momento de la escritura a `0x883238`:

```
guestPC=0x2192ec s0=0x8837a0 s1=0x1 v0=0x1e00004 v1=0x1e00000 ra=0x2192f0
```

**`s1=1`** (índice pequeño y sano, no basura). **`v1=0x1e00000` es
`*(s0+0x70)` en el momento de leerse** — confirma que el campo YA vale
`0x1E00000` antes de que `GetCardInfo` lo use.

**Bug encontrado y corregido en la propia instrumentación**: un primer
intento de vigilar `0x883810` (`s0+0x70`) con una región estrecha de 4
bytes no capturó ninguna escritura, pese a que el valor cambia de `0`
(confirmado justo tras cargar el ELF y de nuevo justo antes de armar el
watchpoint) a `0x1E00000`. Causa: `0x883238` y `0x883810` están a solo
~1.4KB de distancia y caen en la **misma página host de 4KB**; el bucle
del VEH se quedaba con la primera región cuya página coincidía
(`tableBasePtr@0x883238`), comprobaba solo su rango estrecho, y — al no
coincidir — dejaba pasar la escritura sin comprobar la región siguiente
(`s0plus0x70`) que sí la habría reconocido. Corregido en
`Blocker002VectoredHandler` para comprobar TODAS las regiones que
comparten página antes de decidir culpable/no-culpable y hacer el
passthrough. Documentado en el propio código como lección para futuras
fases.

Con el fix, capturadas las 11 escrituras reales a esa página durante la
ejecución: limpieza de BSS en `entry` (PC `0x100018`, `WRITE128`), dos
llamadas a una función `memset`-like (`func_212C50`, invocada desde dentro
de `GetCardInfo`), dos campos vecinos no relacionados (`s0+0x68`,
`s0+0x6C`, ya identificados antes como aritmética de otro propósito), y
**la escritura decisiva**:

```cpp
// GetCardInfo_0x219160.cpp:326-332
// 0x2191c4: lui $v0, 0x1E0
SET_GPR_S32(ctx, 2, (int32_t)((uint32_t)480 << 16));   // v0 = 0x01E00000
// 0x2191c8: sw $v0, 0x70($s0)
WRITE32(ADD32(GPR_U32(ctx, 16), 112), GPR_U32(ctx, 2)); // *(s0+0x70) = v0
```

**`0x1E00000` es una constante inmediata (`lui $v0, 0x1E0`) hardcodeada
directamente en el código de `GetCardInfo`** — no depende de ningún
cálculo, tabla, lectura de archivo, RPC ni IOP. Es un valor literal del
binario original.

**Respuesta al punto 9 (A vs B): opción A confirmada.** `0x1E00000` es el
destino CORRECTO por diseño — una dirección fija reservada del juego
(buffer de mensajes/texto de memory card), no un puntero incorrecto
derivado de una base/offset/índice equivocados. El problema sigue siendo
exclusivamente que sus datos nunca se cargaron.

## Estado tras FASE E.2 y siguiente pregunta abierta

Cadena de causalidad completa y cerrada hasta el origen del puntero:

```
0x586740 (tabla de saltos) corrupta
  <- wraparound de $s1 tras ~667.777 incrementos de +44 (demostrado, FASE D.1)
    <- Print_message nunca encuentra el terminador real 0x0E (FASE E.1)
      <- $s6 = 0x1E00000, buffer vacío: 65KB de ceros, un byte suelto (FASE E)
        <- CardMesPrint deriva esto de tableBase(0x883238) + offset(messageId=0) - 4
          <- GetCardInfo escribe 0x883238 = *(s0+0x70) + *(*(s0+0x70)+s1*4+16) + 4
            <- *(s0+0x70) = 0x1E00000, CONSTANTE HARDCODEADA en GetCardInfo
               (0x2191c4-0x2191c8, "lui $v0,0x1E0; sw $v0,0x70($s0)")
               — destino correcto por diseño, no un bug de recompilación.
```

**La cadena de "quién calcula el puntero" está agotada — termina en una
constante de diseño del juego.** La pregunta que queda, y que es la
verdaderamente relevante para desbloquear M2, es la que el usuario ya
anticipó en los puntos 10-11 de FASE E.2: **¿quién debería rellenar el
buffer de 0x1E00000 con datos reales (texto de memory card), se ejecuta esa
función en nuestro runtime, y si no, por qué?** — pendiente de investigar.
Dado que `0x1E00000` está fuera de cualquier segmento `PT_LOAD` del ELF,
cualquier contenido legítimo ahí solo puede llegar por una escritura en
tiempo de ejecución (código de juego cargando/descomprimiendo un recurso,
o una vía IOP/SIF/RPC/CD) — sin asumir todavía cuál, según lo pedido.

Ninguna corrección aplicada — solo diagnóstico, según lo pedido en todas
las fases. Instrumentación temporal presente y no revertida en:
`vendor/PS2Recomp/ps2xRuntime/src/lib/ps2_runtime.cpp`,
`.../Kernel/Diagnostics/Blocker002Watchpoint.cpp` (nuevo),
`.../Kernel/Syscalls/System.cpp`, `.../Kernel/Stubs/SIF.cpp`,
`.../Kernel/Syscalls/Helpers/Loader.h`, y `runtime/dmc_overrides.cpp`
(este último SÍ es código del proyecto, no vendor).

## Cambio mínimo — pendiente, no aplicado

Todavía no aplicado ningún fix — solo diagnóstico. `sourcePc == 0x2e11c8`,
`v0`/`v1` correctos leídos de un registro sano; el bug está en el contenido
de memoria, no en el JR en sí. No se ha parcheado el valor, ni el índice, ni
forzado un destino — instrucción explícita del usuario, respetada.

## Resultado tras el fix de BLOCKER_001

Reproducible: con `entry` corregido, la ejecución avanza mucho más (carga de
módulos IOP, DMA, memory card, bucle de render/texto) antes de llegar a este
segundo bloqueo, en el mismo punto cada vez (`--seconds 60`, un intento).

## Siguiente bloqueo

Pendiente — depende de resolver este primero.

## FASE F — cambio de enfoque: PCSX2 como oráculo (declaración, 2026-09-08)

**Solo declaración del cambio de metodología. FASE F NO ha comenzado
todavía.**

La cadena causal de BLOCKER_002 quedó completamente cerrada hasta el
origen del puntero (FASE E.2): `0x1E00000` es una dirección hardcodeada
por Capcom en `GetCardInfo` (`lui $v0,0x1E0`), correcta por diseño, no un
bug de cálculo/recompilación. El problema real es que su contenido está
vacío en nuestro runtime. Cadena completa:

```
0x1E00000 (dirección correcta hardcodeada) con contenido ≈ vacío
  → Print_message no encuentra el control 0x0E
    → procesa ~667.777 "caracteres" (en su mayoría 0x00)
      → $s1 avanza +44/carácter sin límite
        → supera los 32MB de RDRAM, wraparound de direccionamiento
          → pisa su propia jump table (0x586740)
            → table[8] = 0x02000100
              → JR 0x02000100 → BLOCKER_002
```

La pregunta para la siguiente fase deja de ser "¿por qué apunta a
`0x1E00000`?" (resuelto) y pasa a ser: **¿qué ocurre en una ejecución
correcta (PS2 real / PCSX2) para que `0x1E00000` tenga el contenido que
`GetCardInfo`/`CardMesPrint`/`Print_message` esperan encontrar ahí?**

Precedentes registrados (patrón, no diagnóstico — no asumir que nuestro
caso sea igual a ninguno de los dos):

- **Dragon Quest VIII / PS2Recomp**: un estado inicial de COP0/kernel
  incorrecto (no reproducía lo que dejaría la Boot ROM real) hizo fallar
  `StartThread`, lo que impedía arrancar un thread de streaming, cuyos
  datos esperados nunca llegaban — el síntoma aparecía mucho más tarde y
  en un sitio aparentemente no relacionado. Ese caso concreto de COP0 ya
  está corregido en nuestro baseline; se registra solo como patrón: un
  fallo pequeño de plataforma puede manifestarse mucho después como un
  buffer/estado que nunca se preparó.
- **Resident Evil Outbreak (recompilación PS2)**: para problemas
  relacionados con SIF RPC, SIF DMA, espera de tareas IOP,
  `SifLoadModule` HLE, filesystem CD/DVD, carga de recursos, buffers DMA y
  sincronización EE/IOP, otros proyectos han usado PCSX2/Pine para
  observar el comportamiento correcto y comparar esa MISMA ruta contra el
  recomp, en vez de implementar servicios al azar.

No se asume que BLOCKER_002 sea un problema de SIF/CD/IOP — está por
determinar observando la ejecución correcta.

Checkpoint completo de evidencia (logs, patches, snapshots, hashes) antes
de este cambio de enfoque: `analysis/local/blocker002_checkpoint_faseE/`
(ver `MANIFEST.md` ahí dentro). `vendor/PS2Recomp` se devolvió a su commit
baseline (`14b1e5cb39b4af7e6fc12f9a29fdc751efde49d7`) tras preservar toda
la instrumentación temporal como patch/snapshot — ningún hallazgo de FASE
A-E.2 depende de código sin commitear; todo lo relevante está documentado
en este archivo y en el MANIFEST del checkpoint.

## FASE F — comparación PCSX2

### FASE F.0 — localizar el oracle (2026-09-08) — BLOQUEADO, esperando al usuario

**HECHO** — comprobado directamente en esta máquina/sesión:

- No hay ejecutable `pcsx2*.exe` en `Program Files`, `Program Files (x86)`,
  registro de Windows (`Uninstall`), accesos directos del Menú Inicio ni
  del Escritorio.
- Búsqueda recursiva completa de `C:\` por `*pcsx2*` sin resultados (además
  de las comprobaciones anteriores).
- **Sí existe** una carpeta de datos de usuario de PCSX2 real y usada
  anteriormente: `C:\Users\chris\OneDrive\Documentos\PCSX2\` (sincronizada
  vía OneDrive), con `memcards/` (`card 1.ps2` de 34MB, `Mcd002.ps2` de
  8.6MB, ambas de 2024-09-25), `inis/PCSX2.ini`, `logs/`, `sstates/`, y un
  volcado de crash (`crash-2024-08-08-01-40-58-665.dmp`) — confirma uso
  real de PCSX2 en el pasado, no una instalación nunca usada.
- `inis/PCSX2.ini` contiene explícitamente: `Bios = E:\PS2Emulator\bios\ps2_bios`
  — el BIOS (y muy probablemente la instalación de PCSX2 misma) vive en una
  unidad `E:\`.
- La carpeta local `bios/` dentro de la carpeta de datos de PCSX2 está
  **vacía**.
- `Get-Volume`/`Get-CimInstance Win32_LogicalDisk`/`Get-Disk` confirman que
  esta sesión solo tiene **un disco físico montado (1TB, letra `C:`)**. No
  existe `D:` ni `E:` accesible ahora mismo, pese a que el prompt de
  sistema de esta sesión lista `D:\` como directorio de trabajo adicional.
- No se encontró ninguna ISO del juego en ningún sitio buscado —
  `original/` solo contiene el ELF ya extraído (`SLES_503.58`), 
  `SYSTEM.CNF`, y dos carpetas `mc0/`/`mc1/` vacías (del propio recomp, no
  de PCSX2).

**INFERENCIA** (no confirmado, pero se sigue directamente de lo anterior):

- La instalación de PCSX2 + BIOS + posiblemente la ISO del juego residen en
  una unidad externa/secundaria (`E:\PS2Emulator\...`) que no está conectada
  o montada en esta sesión/máquina ahora mismo.
- La carpeta `Documentos\PCSX2` bajo OneDrive sincroniza configuración pero
  NO el propio ejecutable ni el BIOS (archivos grandes/binarios,
  probablemente excluidos de la sincronización o simplemente nunca
  copiados ahí).

**HIPÓTESIS**: ninguna todavía — no hay suficiente información para
especular sobre por qué `E:` no está disponible (¿unidad externa
desconectada? ¿esta sesión corre en una máquina/VM distinta a la que
normalmente tiene esa unidad?).

**Bloqueado**: no se puede continuar FASE F.1 sin (a) acceso a un PCSX2
ejecutable, (b) un BIOS PS2 legítimo, y (c) la ISO completa del juego (o
confirmación de que arrancar el ELF suelto es aceptable para esta
comparación, con el riesgo de que eso invalide justo la ruta de
CD/filesystem que podría ser relevante). Pendiente de instrucción del
usuario antes de proceder — no se ha tocado nada más de FASE F.

**Actualización — (a) resuelto.** Usuario eligió instalar PCSX2 de nuevo en
`C:`. Instalado desde el repo oficial (`github.com/PCSX2/pcsx2`, release
`v2.8.2`, publicado 2026-09-04):

```
C:\Program Files\PCSX2\pcsx2-qt.exe
sha256: 982c7c62600a999cf15a25c18349426166c785e7867b2fcc5018d733245b71a3
```

Instalador oficial descargado vía `curl` desde la URL de release de GitHub
(`pcsx2-v2.8.2-windows-x64-installer.exe`, 46.7MB), instalado en modo
silencioso (Inno Setup, `/VERYSILENT /SUPPRESSMSGBOXES /NORESTART`; el
flag `/DIR` fue ignorado por el instalador, quedó en la ruta por defecto
`C:\Program Files\PCSX2\` en vez de `C:\PCSX2\`). El instalador lanzó
automáticamente `pcsx2-qt.exe` al terminar (comportamiento por defecto del
propio instalador, no una acción explícita nuestra) — probablemente
mostrando ahora mismo el asistente de primer arranque en la sesión gráfica
del usuario.

**(b) y (c) siguen bloqueados**: sin BIOS PS2 ni ISO del juego en ningún
sitio de esta máquina. Un BIOS de PS2 es firmware con copyright que debe
volcarse desde la consola física del usuario — no se puede descargar ni
generar. Pendiente de que el usuario lo aporte (o indique que el `E:\`
original vuelve a estar disponible).

**Actualización — (b) y (c) resueltos.** Usuario aportó varios BIOS en
`C:\Users\chris\Documents\PCSX2\bios\ps2-bios-all-bios\` (incluye variantes
PAL: `PS2 Bios 30004R V6 Pal.bin`, `SCPH-70004_BIOS_V12_PAL_200.BIN`) y la
ISO completa en
`C:\Users\chris\Documents\PCSX2\roms\Devil may Cry 2001\Devil May Cry 2001.iso`
(región Europa, `EnFrDeEsIt`, 4.698.767.360 bytes), más un `.chd` y `.cue`
del mismo disco.

**HECHO** — verificado montando la ISO (`Mount-DiskImage`) e inspeccionando
directamente:

- `D:\SLES_503.58` (dentro de la ISO) tiene **SHA256 idéntico**
  (`d0753a6b3b2f00802a50758a872d8cf051725aa31c839aa8894eee30ce58bab4`) al
  ELF que usamos en `original/`. Confirma que es el disco exacto, no una
  revisión distinta.
- `D:\SYSTEM.CNF` idéntico: `BOOT2=cdrom0:\SLES_503.58;1`, `VER=1.02`,
  `VMODE=PAL`.
- La ISO contiene `D:\DATA\ETC\CARDT_U.DAT` (1.696.304 bytes) y variantes
  por idioma `CARDT_G/F/S/I.DAT` (mismo tamaño), además de
  `CARDL_*.DAT`/`CARDS_*.DAT`/`CARDI_*.DAT` (más pequeños) y
  `MODULES\MCMAN.IRX`/`MCSERV.IRX` (drivers IOP de memory card, ya vistos
  cargar durante el boot en el recomp).
- `CARDT_U.DAT` contiene **7901 ocurrencias del byte `0x0E`** en todo el
  archivo (0 en los primeros 4096 bytes — el principio parece ser una
  cabecera/tabla de offsets, con una firma `TIM2` en el offset `0x30`,
  sugiriendo un recurso compuesto texto+textura, no texto puro desde el
  byte 0).

**INFERENCIA** (no confirmado todavía, pendiente de observación en vivo):

- El nombre (`CARDT` = "Card Text"/"Card Table"), la organización
  por-idioma, y la alta densidad de bytes `0x0E` (el terminador real
  confirmado en FASE E.1) hacen de `CARDT_U.DAT` un candidato muy fuerte
  para ser (o contener) el recurso que debería terminar cargado en
  `0x1E00000`.
- No se ha confirmado todavía que este archivo específico sea el que
  `GetCardInfo`/`CardMesPrint` cargan, ni el mecanismo (¿vía IOP/`MCMAN.IRX`
  al detectar la tarjeta? ¿carga directa EE vía `fioRead`/`sceCdRead`? ¿en
  qué momento del boot?). Queda para observación directa en FASE F.1/F.3.

ISO desmontada limpiamente tras la inspección (`Dismount-DiskImage`), sin
dejar unidad montada.

## FASE F — PCSX2 como oráculo

### Por qué llegamos a PCSX2

Hasta FASE E.2 quedó demostrado (todo esto es recapitulación, no nuevo en
esta sección — ver secciones anteriores para el detalle completo):

- BLOCKER_002 termina siendo un `JR` a `0x02000100`.
- El switch/`JR`/`LW` originales son correctos.
- La jump table del ELF es correcta.
- La tabla se corrompe durante runtime.
- `Print_message` es quien la termina pisando.
- `$s1` no está clobberado por `AddPrim` ni por ninguna otra llamada.
- `$s1` avanza exactamente `+44` bytes por carácter.
- Tras ~667.777 caracteres supera los 32MB de RDRAM y las escrituras
  terminan envolviendo dentro de RAM.
- `0x7E` NO es terminador.
- El código de control real que provoca la salida es `0x0E`.
- `Print_message` recibe `s6=0x01E00000`.
- En RECOMP esa región estaba prácticamente toda a cero.
- Por eso nunca aparece `0x0E` y `Print_message` entra en runaway.
- `GetCardInfo` contiene literalmente:

  ```
  0x2191c4: lui v0,0x1E0
  0x2191c8: sw  v0,0x70(s0)
  ```

  por lo que `0x01E00000` es una dirección correcta elegida por Capcom.

- `0x01E00000` no pertenece a ningún `PT_LOAD` del ELF principal.

La incógnita pasó a ser: **¿qué ocurre en una PS2 correcta para que esa
región reciba sus datos?**

### Influencia del trabajo previo de otros proyectos

Registro de la pista metodológica que motivó el cambio de enfoque — **no
se afirma que BLOCKER_002 comparta causa con ninguno de estos casos**,
solo se registra la influencia en la metodología.

**PS2Recomp / Dragon Quest VIII**: un problema previo donde un estado
inicial incorrecto de kernel/COP0 provocaba aproximadamente
`StartThread` falla → el thread de streaming no arranca → los datos
esperados nunca llegan → el fallo aparece mucho después. Ese bug concreto
ya está corregido en nuestro baseline. La pista que aporta: cuando el
código EE parece correcto pero falta un dato esperado, la causa puede
estar en un comportamiento anterior de plataforma/runtime que nunca
ocurrió.

**Resident Evil Outbreak / REO (recompilación PS2)**: ese trabajo ha
necesitado tratar explícitamente SIF RPC, SIF DMA, IOP HLE, CDVD,
filesystem, file loading, buffers DMA, callbacks y sincronización, y
utiliza PCSX2/Pine como herramienta para comparar el comportamiento
original contra el recomp.

Esto llevó a cambiar la metodología de *probar loaders/RPC/DMA/CD/etc. uno
por uno* a *observar primero una ejecución correcta, encontrar la primera
divergencia, y solo después volver al recomp*.

**Declaración explícita**: NO llegamos a `CdRead0` probando APIs de CD al
azar. Llegamos combinando (1) trazabilidad interna completa de
BLOCKER_002 hasta FASE E.2, (2) revisión de problemas encontrados por
otros proyectos de recompilación PS2, (3) uso de PCSX2 como oráculo
inspirado por esas metodologías, y (4) comparación directa de
`GetCardInfo` instrucción por instrucción contra el recomp.

### FASE F.0 — preparación (resumen; detalle completo en la sección de
arriba)

- PCSX2 v2.8.2 funcional, localizado/instalado/configurado en esta
  máquina.
- BIOS PAL (`SCPH-70004_BIOS_V12_PAL_200`) detectado correctamente por
  PCSX2.
- ISO completa del juego localizada, verificada contra nuestro
  `SLES_503.58` (SHA256 idéntico) y `SYSTEM.CNF` idéntico.
- Candidato encontrado dentro de la ISO: `DATA\ETC\CARDT_U.DAT` (~1.6MB),
  con variantes por idioma `CARDT_G.DAT`, `CARDT_F.DAT`, `CARDT_S.DAT`,
  `CARDT_I.DAT`. `CARDT_U.DAT` contiene ~7901 bytes `0x0E`. **Esto es
  solamente una pista — NO se afirma todavía que `resourceId 0x9C` sea
  `CARDT_U.DAT`.**
- PINE está disponible en esta build pero verificado (vía documentación
  oficial) que sirve principalmente para leer/escribir memoria
  (`MsgRead8/16/32/64`, `MsgWrite...`), no expone breakpoints ni registros
  de CPU directamente.
- La comparación de FASE F.1 se realizó con el depurador gráfico R5900
  integrado de PCSX2 (`pcsx2-qt.exe -debugger`), operado manualmente por
  el usuario siguiendo instrucciones dadas paso a paso.

### FASE F.1 — primera divergencia objetiva

**HECHO PCSX2** — observado directamente en el depurador R5900 de PCSX2,
ejecutando la ISO verificada:

Breakpoint puesto en `GetCardInfo @ 0x00219160`. La función se alcanzó
correctamente durante el arranque normal del juego.

Detenido justo antes de `0x002191C8: sw v0,0x70(s0)`, con estos valores
observados:

```
PC = 0x002191C8
v0 = 0x01E00000
s0 = 0x008837A0
s1 = 0x00000004
RA = 0x0021919C
SP = 0x01FFBF80
```

Esto confirma en PCSX2 que `*(0x883810) = 0x01E00000` — exactamente como
predice el código original y como se observó en el recomp. (Nota: el
`s1=4` observado aquí corresponde a un PC distinto dentro de `GetCardInfo`
que el `s1=1` visto anteriormente en el recomp en otro punto de la misma
función — **no son directamente comparables sin fijar el mismo PC exacto
en ambos lados**; pendiente de una comparación PC-a-PC más rigurosa si
hace falta más adelante.)

Tras ejecutar el `sw`, se inspeccionó `0x01E00000` en PCSX2: **también
estaba vacío en ese momento.** Esto refina una idea anterior — que el
buffer esté vacío al entrar en `GetCardInfo` es NORMAL, la PS2 correcta
también parte de un buffer vacío ahí. El contenido real aparece DESPUÉS,
mediante una operación explícita.

**Descubrimiento clave — `CdRead0`.** Inmediatamente después el código
ejecuta:

```
0x2191DC: lw  a1,0x70(s0)      ; a1 = 0x01E00000
0x2191E8: lhu a0,0(v0)
0x2191EC: jal CdRead0
0x2191F0: paddub a2,zero,zero   ; delay slot: a2 = 0
```

Breakpoint puesto en `0x002191EC`. Valores observados en PCSX2:

```
a0 = 0x0000009C
a1 = 0x01E00000
a2 = 0   (delay slot)
```

Por tanto queda demostrado (HECHO PCSX2) que el juego original realiza
conceptualmente `CdRead0(resourceId=0x9C, destination=0x01E00000, arg2=0)`.

**Espera de completion**: después de `CdRead0`, `GetCardInfo` ejecuta
`CdReadCheck`, y mientras no termina: `Task_sleep(1)` → `CdReadCheck` →
repetir. Existe una operación de lectura que el juego espera de forma
explícita antes de continuar.

**Buffer después de la lectura**: breakpoint puesto en `0x00219228`
(inmediatamente después del bucle de `CdReadCheck`). En ese punto PCSX2
muestra que `0x01E00000` YA contiene datos reales. Primeros bytes
observados:

```
02 00 00 00 40 00 00 01 50 00 00 00 01 01 00 00
01 00 02 00 00 00 00 00 01 00 00 00 00 00 00 00
00 00 41 21 02 00 00 20 60 02 00 00 00 00 00 00
01 00 00 00 01 00 00 00 80 00 00 41 21 22 00 00
...
```

**HECHO RECOMP** (ya demostrado en fases anteriores, recapitulado aquí
para la comparación directa): `GetCardInfo` llega al mismo punto, con la
misma dirección `0x01E00000` calculada de la misma forma; en el momento en
que `Print_message` la consume, la región sigue prácticamente vacía (65KB
de ceros, un único byte suelto `0x80`, ningún `0x0E`).

**Tabla comparativa**:

| | PCSX2 | RECOMP |
|---|---|---|
| `GetCardInfo` alcanzado | sí | sí |
| `*(0x883810)` tras el `sw` | `0x01E00000` | `0x01E00000` |
| `0x01E00000` justo tras el `sw` | vacío | vacío |
| Operación posterior | `CdRead0(0x9C, 0x01E00000, 0)` + `CdReadCheck` loop | (ninguna observada) |
| `0x01E00000` en el momento en que `Print_message` lo consume | contiene datos reales | prácticamente todo `0x00` |

**INFERENCIA** (se sigue directamente de lo observado, no requiere
suposición adicional): la ruta `CdRead0`/`CdReadCheck` iniciada por
`GetCardInfo` es la que introduce el contenido real de `0x01E00000` en una
ejecución correcta. Esta es la **primera divergencia objetiva demostrada**
entre una ejecución correcta y el recomp — no una hipótesis, es la
comparación directa PC-a-PC de ambos lados en el mismo punto del código.

**HIPÓTESIS / pendiente — NO afirmar todavía**:

- que `resourceId 0x9C` corresponda a `CARDT_U.DAT`;
- que el problema sea CDVD;
- que sea IOP;
- que sea SIF;
- que `CdRead0` esté mal implementado/emulado en el recomp;
- que `CdReadCheck` esté mal;
- que el runtime del recomp devuelva éxito incorrectamente en alguna de
  estas rutas.

Todo eso sigue sin demostrar. La pregunta ya no es "¿quién debería llenar
`0x01E00000`?" (respondida a nivel de ruta: `CdRead0`/`CdReadCheck` desde
`GetCardInfo`) — la nueva pregunta es **¿por qué PS2Recomp no termina
dejando esos datos en `0x01E00000`?**, y sigue completamente abierta.

Ninguna corrección aplicada, ningún build realizado, ningún cambio al
runtime — solo observación y documentación, según lo pedido.

### FASE F.2 — resolución de `resourceId 0x9C` (análisis estático, en progreso)

**HECHO** — leído directamente del código generado (`.symtab`, no Ghidra) y
de bytes ELF estáticos:

`CdRead0` (el nombre usado informalmente en FASE F.1) es en realidad
**`CdRead00` (`0x1cf220-0x1cf238`)**, confirmado por el `jal func_1CF220`
en `GetCardInfo_0x219160.cpp:366` que coincide exactamente con el
breakpoint `0x2191EC` usado en PCSX2.

Reconstrucción completa de cómo se calcula `a0` (`resourceId=0x9C`),
instrucción por instrucción (`GetCardInfo_0x219160.cpp:334-364`):

```
0x2191cc: at = 0x740000
0x2191d0: v1 = *(uint8_t*)(at+0x2209) = *(0x742209)      // "índice" (byte, BSS)
0x2191d4: v0 = 0x580000
0x2191d8: v0 = v0 + (-0x4818) = 0x57B7E8                  // base de tabla (ELF estático)
0x2191dc: a1 = *(s0+0x70) = 0x01E00000                    // destino (ya resuelto en FASE F.1)
0x2191e0: v1 = v1 << 1                                     // índice * 2 (tabla de uint16)
0x2191e4: v0 = v0 + v1
0x2191e8: a0 = *(uint16_t*)v0 = *(0x57B7E8 + índice*2)     // *** resourceId ***
0x2191ec: jal CdRead00(a0=resourceId, a1=dest, a2=0)
```

**`0x742209` es BSS** (confirmado contra los program headers del ELF,
igual que otras variables de este tipo vistas en fases anteriores) — un
byte de estado en tiempo de ejecución, casi con toda seguridad un
selector de idioma/variante (rango de valores pequeño, patrón de tabla
con huecos cada 8 entradas — ver abajo).

**Tabla en `0x57B7E8` leída directamente de bytes ELF** (dirección
file-backed, estática, 100% fiable — no depende de ejecución):

```
índice  valor    índice  valor    índice  valor    índice  valor    índice  valor
0x00    0x00b8   0x08    0x0085   0x10    0x008d   0x18    0x008f   0x20    0x008a
0x01    0x00b8   0x09    0x0085   0x11    0x00fa   0x19    0x00f8   0x21    0x00fc
0x02    0x00b9   0x0a    0x00ba   0x12    0x00bb   0x1a    0x00bc   0x22    0x00bd
0x03    0x00ee   0x0b    0x00e4   0x13    0x00e5   0x1b    0x00e6   0x23    0x00e7
0x04    0x009c   0x0c    0x009d   0x14    0x00a0   0x1c    0x00a2   0x24    0x009e
0x05    0x00e3   0x0d    0x00d9   0x15    0x00da   0x1d    0x00db   0x25    0x00dc
0x06    0x0000   0x0e    0x0000   0x16    0x0000   0x1e    0x0000   0x26    0x0000
0x07    0x0000   0x0f    0x0000   0x17    0x0000   0x1f    0x0000   0x27    0x0000
```

**Índice `0x04` → valor `0x9C`, coincidencia exacta** con el `a0=0x9C`
observado en vivo en PCSX2 (FASE F.1). Confirma que el byte selector en
`0x742209` valía **`4`** en esa ejecución concreta de PCSX2.

El patrón — 6 valores reales seguidos de 2 ceros de relleno, repetido cada
8 entradas — es consistente con **6 variantes (idioma u otra dimensión) ×
N recursos**, con 2 slots de relleno/reservados por grupo. `resourceId`
NO es un índice ISO/LBA directo ni un puntero — es un **entero pequeño
(caben en 8-9 bits) que identifica un recurso lógico dentro de una tabla
interna del juego**, seleccionado por idioma/variante.

`CdRead00` es un wrapper trivial (`0x1cf220-0x1cf238`): si `a2==0` (nuestro
caso), fuerza `a2 = 0x80|1 = 0x81` (flags/modo) y hace tail-call a
**`CdFileRead` (`0x1cf0d0-0x1cf11c`, nombre real del `.symtab`)** — el
nombre de la función confirma independientemente que `a0` es un
identificador de ARCHIVO, no un sector crudo.

`CdFileRead` rellena una estructura de petición de 4 words en
`0x87DC60-0x87DC6C` (`resourceId`, `dest=0x01E00000`, `mode=0x81`,
`reservado=0`) y llama a `func_1CEAA0(a0=2, a1=0x87DC20)` — un puntero de
"resultado"/"cola" distinto (offset 0x40 antes de la estructura de
petición), cuyo valor de retorno se desreferencia una vez más
(`v0=READ32(v0+0)`). Esto tiene la forma de un mecanismo clásico de
"encolar petición de I/O" — no se ha investigado más adentro de
`func_1CEAA0` todavía (fuera del alcance pedido: resolver qué es
`resourceId`, no diagnosticar el subsistema de I/O).

**INFERENCIA**: `resourceId=0x9C` (156) es casi con toda seguridad un
índice hacia una tabla de archivos embebida en el propio ejecutable (un
"file table" con, probablemente, `{sector/LBA, tamaño}` por entrada,
patrón muy común en juegos PS2 para evitar buscar por nombre en el
filesystem CD en caliente). Esa tabla NO se ha localizado todavía — sería
el siguiente paso de rastreo estático si hiciera falta, pero antes se
prioriza la comparación binaria directa (pasos 3-4) que puede resolver la
pregunta de forma mucho más directa y concluyente.

**HIPÓTESIS — NO confirmado todavía**: que `CARDT_U.DAT` sea el archivo
resuelto por `resourceId=0x9C`. Sigue siendo solo una pista por nombre y
contenido (FASE F.0). Pendiente de la comparación binaria (pasos 3-4).

**Pendiente de datos del usuario**: para completar los pasos 3-4-5 hace
falta un volcado real de `0x01E00000` desde PCSX2 en el punto ya
localizado (`0x00219228`, justo después del bucle `CdReadCheck`, donde ya
confirmamos que el buffer contiene datos reales). Pedido al usuario:
volcar 4KB (`0x01E00000-0x01E01000`) desde la vista de memoria del
depurador de PCSX2 — vía exportar/guardar región a archivo si esa función
existe en la UI, o alternativamente pegar los primeros 512-1024 bytes en
hexadecimal directamente. Sin esto no se puede completar la comparación
binaria contra `CARDT_U.DAT` ni determinar el tamaño real de la
operación.

### FASE F.3 — resolución binaria de `resourceId 0x9C` — RESUELTO, refuta la hipótesis `CARDT_U.DAT`

**HECHO PCSX2/PINE** — con el usuario deteniendo la ejecución manualmente
en `0x00219228` (mismo punto que FASE F.1, confirmado visualmente por el
usuario que `0x01E00000` ya estaba poblado), se activó PINE
(`EnablePINE=true` en el `.ini`, efectivo solo tras reinicio completo de
PCSX2 — confirmado empíricamente que editar el `.ini` con la app ya
abierta NO se aplica en caliente, no existe pipe/socket hasta reiniciar).
PINE confirmado escuchando en **TCP `127.0.0.1:28011`** (no named pipe en
Windows, a diferencia de lo asumido inicialmente) — verificado con
`MsgVersion` → `"PCSX2 v2.8.2"`.

Cliente PINE mínimo implementado en Python (protocolo exacto extraído de
`PINE.cpp` del propio repo oficial: paquete `[u32 tamaño LE][opcode][addr
u32 LE]` por mensaje, respuesta `[u32 tamaño][u8 status][datos]`,
`MsgRead64=3`) para leer, en modo **solo lectura**, `0x01E00000` a
`0x01E00FFF` (4096 bytes exactos) mientras PCSX2 seguía pausado en el
breakpoint.

Guardado en:
```
analysis/local/blocker002_pcsx2/pcsx2_1e00000_0x1000.bin   (4096 bytes)
analysis/local/blocker002_pcsx2/pcsx2_1e00000_0x1000.hex.txt
```

Verificación:
- Tamaño exacto: **4096 bytes**, confirmado.
- SHA256: `47e5bcd5a5e44ba182c812de7f920091e97e52fe1b6a4e12a593e5adc2c1dbd7`
- 2359/4096 bytes no-cero (57%), último byte no-cero en el offset `0xFFF`
  (el bloque tiene contenido estructurado hasta el final, no es un
  volcado casi vacío).
- Comparación contra los primeros 64 bytes observados manualmente por el
  usuario en la GUI: **discrepancias menores en 2 posiciones**
  (intercambio local de un par de bytes `00`/`01` alrededor del offset
  12-14, y un desplazamiento de un byte en el offset 24-28), con el resto
  — incluyendo los puntos de referencia `00 00 41 21`, `60 02 00 00`,
  `80 00` — coincidiendo exactamente. Patrón consistente con error de
  transcripción manual (confusión de bytes `00`/`01` adyacentes, muy común
  al copiar hex a mano), no con un problema del volcado por PINE. No se
  bloqueó en esto — se priorizó la comparación binaria objetiva (ver
  abajo), que es indiferente a errores de transcripción humana.

**Comparación binaria contra la ISO — HECHO, verificado por dos métodos
independientes**:

1. Búsqueda del bloque completo de 4096 bytes como substring exacto en la
   ISO cruda completa (4.698.767.360 bytes, leída en chunks de 64MB con
   solape): **una única coincidencia**, en el offset absoluto
   `0x7357b800` (944.840 × 2048 — exactamente alineado a sector CD-ROM de
   2048 bytes, confirmado `LogicalSectorSize=2048` al montar la ISO).
2. Búsqueda independiente archivo por archivo (545 archivos no-`MOVIE`,
   1.346.403.265 bytes en total, escaneados en ~4.5s) montando la ISO y
   leyendo cada archivo: **coincidencia exacta en un único archivo**,
   `D:\DATA\ETC\OPMOJI_G.T32`, **offset 0** (el match empieza justo al
   principio del archivo), tamaño total del archivo **68.192 bytes**.
   SHA256 de los primeros 4096 bytes del archivo == SHA256 del volcado de
   PCSX2, **idéntico**.

Los candidatos originales (`CARDT_U.DAT`, `CARDT_G.DAT`, `CARDT_F.DAT`,
`CARDT_S.DAT`, `CARDT_I.DAT`) fueron comprobados explícitamente primero y
**ninguno contiene el bloque** — descartados con evidencia directa, no por
omisión.

**Resultado**:

```
resourceId  = 0x9C (156)
selector    = 4          (byte en 0x742209, ver FASE F.2)
archivo     = DATA\ETC\OPMOJI_G.T32
offset      = 0x0 (inicio del archivo)
tamaño match = 4096 bytes (0x1000) exactos, SHA256 idéntico
tamaño total del archivo en ISO = 68192 bytes
```

**INFERENCIA**: `resourceId 0x9C` con selector `4` resuelve a
`OPMOJI_G.T32`, no a `CARDT_U.DAT` como se había hipotetizado en FASE F.0
por coincidencia de nombre. La hipótesis original queda **refutada con
evidencia directa**, no simplemente sin confirmar. El sufijo `_G` en el
nombre podría seguir el mismo patrón de variante-por-selector visto en
`CARDT_*` (`U/G/F/S/I`), pero **no se ha verificado** qué selector
produce qué sufijo de archivo en este caso concreto (`OPMOJI_G` con
selector `4` no encaja obviamente con el orden `U,G,F,S,I` visto en
`CARDT_*`; no se investiga más por ahora, fuera del alcance pedido).

**HIPÓTESIS — NO confirmado todavía**:

- Qué representa exactamente "OPMOJI" o el contenido real dentro del
  archivo (texto de mensajes, datos de otro tipo, o ambos combinados —
  recuérdese que `CARDT_U.DAT` también mostró una firma `TIM2` cerca del
  inicio en FASE F.0, sugiriendo que este formato de recurso del juego
  mezcla texturas y datos con frecuencia).
- Si el tamaño real de la operación `CdRead0(0x9C, 0x01E00000, 0)` es el
  archivo completo (68192 bytes) o solo una parte — **no determinado
  todavía**, solo se verificaron los primeros 4096 bytes solicitados por
  el usuario. Sería necesario un volcado más largo (o comparar contra el
  resto del archivo) para confirmarlo.
- Que `resourceId 0x9C` provenga de una tabla de recursos "genérica" del
  motor del juego (aplicable más allá de memory card) en vez de ser
  específico de `GetCardInfo`/`CardMesPrint` — no investigado.

No se ha vuelto al recomp. No se ha implementado ningún fix. Solo
observación, lectura de memoria en modo solo-lectura vía PINE, y
comparación binaria — según lo pedido.

## FASE G — comparación CdRead PCSX2 vs RECOMP (2026-09-08)

### FASE G.1 — trazado estático de la ruta completa

**HECHO** — leído directamente del `.symtab` y bytes ELF, sin ejecutar nada:

Nombres reales confirmados vía `.symtab` (más informativos que los alias
usados en FASE F): `func_1CEAA0` = **`CallCdModule`**
(`0x1ceaa0-0x1cee68`, función enorme, dispatcher de comandos). Además:
`CdReadCheck_0x1cea50`, `CdReadCheck2_0x1cea80`,
`CdReadManager_0x1cea90` (nombres reales, no investigados todos a fondo
por estar fuera del camino crítico).

`CallCdModule` recibe `a0=2` (mismo valor que `CdFileRead` pasa como
primer argumento a `func_1CEAA0`) y lo usa como índice en una tabla de
saltos interna estática en `0x584D10` (14 entradas, `sltiu $at,$a1,0xE`).
Tabla leída directamente de bytes ELF: `cmd=2` y `cmd=3` apuntan ambos a
`0x1ceb9c`. Esa rama:

1. Comprueba `sceSifCheckStatRpc` de un cliente previo, con fallback a
   `printf`/`FlushCache` si no está listo (código de robustez, no relevante
   para el bug).
2. Construye un paquete de 112 bytes (`0x70`) en la dirección global fija
   **`0x87DC80`**, con campos copiados desde la estructura que `CdFileRead`
   había preparado antes en `0x87DC60` (`resourceId`, `dest=0x01E00000`,
   `mode=0x81`) más metadatos adicionales (tamaño calculado a partir de
   `0x4C($s3)`/`0x44($s3)` — campos de otra estructura de fecha/tamaño de
   archivo, offset por sector `0x7FF>>11`, etc. — no completamente
   descompuestos, fuera del alcance de esta fase).
3. Llama a `sceSifCallRpc` (`0x1ceb8c: jal func_205748` = `sceSifCallRpc`
   real, confirmado por symtab) con:
   - `clientPtr = $a0 = 0x87DBF0`
   - `sendBuf = $a3 = 0x87DC80`, `sendSize` (calculado, ver arriba)
   - `receiveBuffer`/`receiveSize` en el stack (no localizados con
     precisión — el propio `SifCallRpc` del runtime intenta tanto
     convención de registros como de stack, ver FASE G.1 más abajo).

`SifCallRpc` (runtime, `RPC.cpp:334`) — **implementación real, no stub**:
resuelve `sid` desde `g_rpc_clients[clientPtr]`, busca un `server`
registrado para ese `sid`, y si existe llama a
`PS2IopTransport::handleRpc()` → `PS2Runtime::handleIopRpc()` →
`IopSubsystem::handleRpc()` (`ps2xIOP/src/iop_subsystem.cpp:280-288`):

```cpp
RpcResult IopSubsystem::handleRpc(const RpcRequest &request)
{
    const auto it = m_impl->routes.find(request.sid);
    if (it == m_impl->routes.end() || !it->second)
    {
        return {};   // RpcResult{} por defecto: handled=false
    }
    return it->second->handleRpc(request);
}
```

`m_impl->routes` se rellena en `rebuildRoutes()` a partir de
`service->sids()` de cada servicio "core" (`createMcservService`,
`createDbcmanService`, `createLibSdService` — memory card, gestor de
config, sonido) y de perfiles built-in específicos de otros juegos
(`ps2xIOP/src/builtin_profiles.cpp` — perfiles vistos para LOTR, Fatal
Frame, etc., ninguno para `SLES_503.58`/Devil May Cry). **Ningún servicio
registra el `sid` usado por esta llamada** (confirmado por
`grep` — cero ocurrencias de un sid coincidente en todo `ps2xIOP/`).

Vuelta a `SifCallRpc::completeClient` (`RPC.cpp:568-639`), con
`iopResult.handled == false`:

```cpp
if (receiveBuffer != 0 && receiveSize != 0) {
    if (handled && resultPointer != 0 && resultPointer != receiveBuffer)
        rpcCopyToRdram(...);                    // no aplica, handled=false
    else if (!handled && sendBuf != 0 && sendSize != 0 && sendBuf != receiveBuffer)
        rpcCopyToRdram(rdram, receiveBuffer, sendBuf, min(sendSize, receiveSize));  // fallback: eco
    else if (!handled)
        rpcZeroRdram(rdram, receiveBuffer, receiveSize);  // fallback: ceros
}
g_rpc_clients[clientPtr].busy = false;   // SIEMPRE se limpia, sin importar handled
```

**`CdReadCheck` (`0x1cea50-0x1cea80`) es trivial** — su ÚNICA lógica es
llamar a `sceSifCheckStatRpc(0x87DBF0)` y devolver directamente su
resultado (`0`=terminado, `≠0`=pendiente). `SifCheckStatRpc`
(`RPC.cpp:809`) devuelve `g_rpc_clients[clientPtr].busy ? 1 : 0` — y como
`busy` se limpia incondicionalmente al final de `completeClient`
(línea de arriba), **`CdReadCheck` informa "terminado" inmediatamente,
sea cual sea `handled`.**

### FASE G.1 — confirmación empírica (log YA existente, sin build ni instrumentación nueva)

`PS2X_ENABLE_IOP_RPC_TRACE` está **activado por defecto**
(`option(... ON)` en `ps2xRuntime/CMakeLists.txt:15`) — el logging de RPCs
sin manejar ya estaba compilado en el ejecutable existente (SHA256
`9ff9fa76...`, el mismo de antes del checkpoint — NO se recompiló nada).
Bastó con ejecutar el binario ya existente (`python`/invocación directa,
60s) y grep sobre su log:

```
[IOP/RPC trace:unhandled] sid=0x12345678 rpc=0x1 pc=0x1ceb94 ra=0x1ceb94 send=0x87dc80/112 recv=0x87dac0/4 sendBytes=[00 00 00 00 00 00 00 00 00 00 00 00 40 36 74 00] loadedModules=[...]
[IOP/RPC trace:unhandled] sid=0x12345678 rpc=0x2 pc=0x1cecc0 ra=0x1cecc0 send=0x87dc80/112 recv=0x87dac0/4 sendBytes=[5C 00 00 00 97 3C 0E 00 C0 0E 00 00 00 00 AC 00] loadedModules=[...]
```

**`pc=0x1ceb94` coincide EXACTO** con la dirección inmediatamente posterior
al `jal sceSifCallRpc` dentro de `CallCdModule` (`0x1ceb8c` + delay slot).
**`send=0x87dc80/112`** coincide EXACTO con el paquete de 112 bytes en
`0x87DC80` reconstruido estáticamente arriba. `recv=0x87dac0/4` — un
buffer de resultado de solo 4 bytes (no `0x01E00000` directamente; la
transferencia real de los ~68KB del archivo sería responsabilidad del
módulo IOP real, vía una DMA/SIF posterior que nunca llega a
dispararse porque el propio RPC nunca se maneja).

`sid=0x12345678` es un valor demasiado específico para ser un
"no vinculado" genérico (que normalmente sería `0`) — es casi con toda
seguridad el ID de servicio SIF real y fijo que el juego original usa
para su módulo de lectura de CD, tal cual está hardcodeado en el binario.

### FASE G.2 — tabla comparativa

| Etapa | PCSX2 | RECOMP |
|---|---|---|
| `GetCardInfo` alcanza `CdRead00` | sí (FASE F.1) | sí (código idéntico, confirmado estáticamente) |
| `resourceId` | `0x9C` | `0x9C` (misma aritmética, FASE F.2) |
| `dest` | `0x01E00000` | `0x01E00000` |
| `mode` | `0x81` | `0x81` (misma constante hardcodeada `0x80\|1`) |
| `CdFileRead` ejecuta | sí | sí |
| `CallCdModule` ejecuta (cmd=2) | sí | sí |
| `sceSifCallRpc` se invoca | sí | sí (`RPC.cpp:334`, real, no stub) |
| **`sid=0x12345678` tiene handler real** | **sí (IOP real de PCSX2)** | **NO — `routes.find()` vacío, `IopSubsystem::handleRpc` devuelve `RpcResult{}`** |
| Petición marcada `handled` | (n/a, real) | `false` |
| Buffer de recepción (4 bytes en `0x87DAC0`) | poblado por el módulo IOP real | eco de `sendBuf` o ceros (fallback genérico) |
| `busy` del cliente se limpia | sí (tras trabajo real) | sí (**incondicional**, sin trabajo real) |
| `CdReadCheck` ve completion | sí (legítimo) | sí (**falso positivo**) |
| `0x01E00000` recibe los 68KB del recurso | sí (confirmado FASE F.3) | **no — nunca se dispara ninguna transferencia real** |

**Primera divergencia**: ocurre exactamente en
`IopSubsystem::handleRpc` (`ps2xIOP/src/iop_subsystem.cpp:280-288`) — el
`sid=0x12345678` que el juego usa para su módulo de lectura de CD/archivo
no tiene ningún servicio IOP registrado en nuestro runtime. Todo lo
anterior (GetCardInfo, CdRead00, CdFileRead, CallCdModule, construcción
del paquete, la propia llamada a `sceSifCallRpc`) coincide exactamente
entre PCSX2 y el recomp.

### Respuesta a los 9 casos planteados

Confirmado **CASO 3 combinado con CASO 4**: la petición SÍ llega al
runtime/HLE (`SifCallRpc` se ejecuta con los parámetros correctos), pero
**no hay ningún servicio IOP que resuelva `sid=0x12345678`** (CASO 3), y
el runtime **marca la operación como completada (`busy=false`)
incondicionalmente**, sin haber copiado datos reales (CASO 4). Descartados
con evidencia directa: CASO 1 (sí llega al runtime), CASO 2 (`resourceId`
se resuelve correctamente, ver FASE F.2/F.3), CASO 5/6 (no hay copia en
absoluto, ni de origen ni destino incorrectos — simplemente no hay copia
real), CASO 7 (parcialmente cierto pero la causa raíz es CASO 3, no un
bug de `CdReadCheck` en sí — `CdReadCheck` refleja fielmente lo que
`SifCheckStatRpc` le dice), CASO 8 (no aplica, no hay callback/thread
pendiente — el fallo es la ausencia total de handler), CASO 9 (correcto a
nivel de síntoma: SÍ depende de un servicio IOP/SIF/CDVD no implementado,
pero ahora identificado con precisión: falta específicamente el servicio
para `sid=0x12345678`, no una vaguedad genérica "CD/IOP").

**Entorno del runtime — comprobado**: el runtime actual NO monta ni tiene
acceso a la ISO ni a archivos extraídos del disco en ningún punto de esta
ruta (`GetCardInfo`→...→`IopSubsystem::handleRpc`) — no hay ningún
`ifstream`/apertura de archivo en toda la cadena estática trazada, todo
opera sobre buffers en RAM guest y el registro `routes` en memoria. La
ausencia de `DATA/ETC/OPMOJI_G.T32` en el árbol del proyecto es
irrelevante en este punto exacto: la petición ni siquiera llega a
intentar leer ningún archivo, porque el servicio que debería iniciar esa
lectura no existe. **No se ha copiado `OPMOJI_G.T32` al proyecto — no
hacía falta para esta comprobación, y no se ha demostrado todavía que
copiar el archivo (sin implementar el servicio) resolvería nada.**

**HIPÓTESIS — NO investigado todavía**:

- Qué servicio IOP real debería registrar `sid=0x12345678` (¿un módulo de
  CD genérico específico de Capcom, distinto del `MCMAN`/`MCSERV`
  estándar de Sony ya soportados?).
- Cómo obtiene el módulo IOP real la ubicación física (LBA/offset) de
  `OPMOJI_G.T32` en el disco a partir de `resourceId=0x9C` — no
  localizada la tabla que lo resuelve (pendiente del punto 4 de FASE G.1,
  no completado por priorizar la comparación PCSX2 vs recomp).
- Si el servicio, una vez implementado, necesitaría acceso real a la ISO
  (montada) o si el juego espera los datos por otra vía.

No se ha implementado ningún fix. No se ha copiado ningún archivo. No se
ha modificado `CdRead00`/`CallCdModule`/`SifCallRpc`. No se ha falsificado
ninguna respuesta. No se ha hecho ningún build (se usó el ejecutable ya
existente, sin recompilar, tal como se indicó explícitamente).
