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

## Cambio mínimo — pendiente, no aplicado

No aplicado esta sesión — depende de la instrumentación adicional descrita
arriba para diferenciar entre "índice corrupto" y "lectura de memoria
corrupta" antes de decidir el fix.

## Resultado tras el fix de BLOCKER_001

Reproducible: con `entry` corregido, la ejecución avanza mucho más (carga de
módulos IOP, DMA, memory card, bucle de render/texto) antes de llegar a este
segundo bloqueo, en el mismo punto cada vez (`--seconds 60`, un intento).

## Siguiente bloqueo

Pendiente — depende de resolver este primero.
