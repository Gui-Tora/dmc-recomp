# Primer bloqueo real — M1, hueco de función en 0x100088

- ELF: `SLES_503.58`, Devil May Cry (Europa/PAL) v1.02. sha256
  `d0753a6b3b2f00802a50758a872d8cf051725aa31c839aa8894eee30ce58bab4`,
  crc32 `77654AD2`, entry `00100008`. Región/revisión confirmada (SYSTEM.CNF).
- Entorno: upstream PS2Recomp `14b1e5cb39b4af7e6fc12f9a29fdc751efde49d7`, Ghidra
  12.1.3 (lenguaje `MIPS:LE:64:64-32R6addr`, sin soporte R5900 real — ver
  `analysis/notes/HUGE_GENERATED_FILES.md`), MSVC 14.44, build en
  `build/runtime/active` (commit de esta build: primer `dmc-recomp.exe`
  compilado con éxito, 2026-09-07/08).
- Reproducción: `python scripts/pipeline.py run --seconds 60` tras el primer
  `build` exitoso. Log completo: `logs/1788818234409255300-run.log`.

## Último hito demostrado y evidencia

M0 (ELF → C++ → `dmc-recomp.exe` compilado) y M1 parcial (el entry point real
del juego arranca a ejecutarse) confirmados. El log muestra:
`ELF file loaded successfully. Entry point: 0x100008` /
`Starting execution at address 0x100008`, y el runtime PC (raylib, ventana,
GPU NVIDIA RTX 4060, audio WASAPI, carga de segmentos) inicializa sin
problemas. El Game Override de instrumentación (`runtime/dmc_overrides.cpp`)
también se aplicó correctamente: `[game_overrides] applying
'dmc-sles-503.58-huge-fn-instrumentation'` / `applied 1 matching override(s)`
— confirma que el mecanismo de overrides atado a este ELF exacto funciona.
Ninguno de los 20 stubs instrumentados se disparó en estos 60s (sin logs
`[dmc-stub-instrumentation]`) — consistente con, pero no prueba definitiva de,
que no se llaman durante el arranque.

## Primer error significativo; PC/RA/argumentos observados

```
[guest-branch:missing-target] kind=DirectJump op=EE scheduler
  source=0x100088 target=0x100088 pc=0x100088 ra=0x100088
  sp=0x2000000 gp=0x591870 a0=0x0 a1=0x0 a2=0x2000 a3=0x589a00
  v0=0x0 v1=0x74
```
La ejecución llega hasta `0x100088` y se queda ahí (source=target=pc), sin
avanzar, durante el resto de los 60s (traza repetida `0x20a2e0 -> 0x20a2e0
-> ...`, un valor de traza interna, no la propia dirección PC).

## Función/rango en Ghidra y callers

`entry` (única función definida en esa zona): `Start=0x00100008,
End=0x00100088, Size=128` (End exclusivo, según CSV de
`analysis/ghidra/export/dmc_functions.csv`). Siguiente función conocida:
`_exit,0x001000A8,...`. Hueco sin asignar: `0x100088`-`0x1000A7` (32 bytes).

## Hipótesis y evidencia que la distingue de otras causas

Parseado el ELF directamente (sin Ghidra, para no depender de su análisis ya
sabido incompleto para R5900): los program headers dan el segmento principal
`PT_LOAD offset=0x280 vaddr=0x100000 filesz=0x489900`. El word crudo LE en
`0x100088` es `0x42000038` — opcode COP0 (bits 31:26 = 0x10) con función
`0x38`, que en el R5900 corresponde a una instrucción propia de interrupciones
(`DI`/`EI`, no estándar en MIPS genérico). Hipótesis: Ghidra no reconoció esta
instrucción R5900-específica como código válido al analizar, dejando ese
rango sin asignar a ninguna función — el mismo síntoma raíz que las 20
funciones gigantes (falta de soporte R5900 en el lenguaje de Ghidra usado),
pero aquí produciendo un **hueco** (instrucción excluida) en vez de un
**exceso** (límites disparados). Distinto de "función no encontrada" por
salto indirecto sin resolver: aquí es una instrucción real, contigua, dentro
del rango de código, simplemente nunca exportada como parte de ninguna
función.

## Cambio aplicado (2026-09-08) — RESUELTO

Se aisló el hueco corriendo `ps2_recomp.exe` con un CSV mínimo de una sola
fila (`gap_100088,0x100088,0x1000A8,32`) en `analysis/local/blocker001/`,
fuera del pipeline principal. rabbitizer/PS2Recomp decodificó las 8
instrucciones reales sin ambigüedad:

```
0x100088: ei                          ; Enable Interrupts (COP0, R5900)
0x10008c: lui   $v0, 0x59
0x100090: addiu $v0, $v0, -0x6600     ; v0 = 0x589A00
0x100094: lw    $a0, 0x0($v0)         ; a0 = *(0x589A00)
0x100098: jal   func_15B3A0           ; llamada real
0x10009c: addiu $a1, $v0, 0x4         ; (delay slot) a1 = 0x589A04
0x1000a0: j     func_202280           ; salto final (sin retorno)
0x1000a4: move  $a0, $v0              ; (delay slot)
```

Es la secuencia clásica de arranque CRT de PS2SDK: habilita interrupciones,
monta `argc`/`argv` desde `0x589A00` (justo tras el segmento cargado del ELF,
`0x100000+0x489900=0x589900`), llama a la función principal (`func_15B3A0`,
sin nombre todavío) y al volver llama a `Exit` (`func_202280` — PS2Recomp lo
reconoce por nombre real desde el `.symtab` del ELF) con el valor de retorno.

**Hallazgo mayor en el camino**: este ELF no está completamente stripped —
`ps2_recomp` extrae 18025 símbolos / 10125 funciones directamente del
`.symtab`/`.strtab` reales del ELF, independientemente de Ghidra. El CSV de
Ghidra tiene prioridad donde lo cubre, pero PS2Recomp rellena huecos no
cubiertos con sus propios límites de símbolo — así fue como tradujo
correctamente `func_202280` como `Exit` sin que nosotros lo pidiéramos. Esto
sugiere que la tabla de símbolos real podría ser una fuente de límites de
función más fiable que el CSV de Ghidra para varios de los casos problemáticos
(incluidas quizá las 20 funciones VU0 gigantes) — pendiente de investigar,
no se toca todavía por instrucción explícita (prioridad: superar bloqueos de
ejecución primero).

**Fix aplicado**: editado directamente `analysis/ghidra/export/dmc_functions.csv`,
extendiendo el límite de `entry` de `0x00100088` a `0x001000A8` (de 128 a 160
bytes), absorbiendo el hueco en la misma función en vez de dejarlo sin cubrir.
Contiguo exacto con `_exit` (que empieza en `0x001000A8`), sin solape. No se
usó `ret0` ni ningún stub — son las instrucciones reales.

## Resultado tras el fix

Repetido `prepare` → `generate` → `build` → `run` con el CSV corregido. Ver
`BLOCKER_002_*.md` para el siguiente bloqueo encontrado (o confirmación de que
no apareció ninguno en la ventana probada).
