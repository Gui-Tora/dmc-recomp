# P4.1.9 — Astra: original UI DBuff conditional-tail oracle

Fecha: 2026-09-13. Resultado: **P419_LIVE_ORACLE_BLOCKED**.

No se obtuvo una medición PCSX2. La reconstrucción estática de P4.1.8 sigue siendo la evidencia más fuerte disponible, pero **no se promueve a confirmación en vivo**. No se autoriza todavía el cambio HLE bajo el gate de este prompt.

## Estado y alcance

- Main HEAD: `5c0881b77d3ae1f7f317d2f41b1824fc61451a19`.
- Vendor HEAD: `19911ce35fb8f2029853f25612b1cc420a8c74bb`, limpio.
- Entrada: once informes previos sin trackear, incluido P418; ningún cambio tracked. Se preservaron todos. El aviso del sandbox sobre el archivo global de ignores producía un falso `?? .claude/`; la comprobación de solo lectura fuera del sandbox no lo muestra.
- Se releyó completamente [P4.1.8](BLOCKER_004_P418_ASTRA_VISUAL_REGRESSION_TIMELINE_COMPARATIVE_AUDIT.md), incluida la cola condicional y los límites de sus conclusiones.
- No se ejecutó RECOMP ni PCSX2; no se modificó runtime, generated, vendor, configuración del emulador ni saves. No se compiló ni se terminó ningún proceso.

## Disponibilidad del oráculo

**HECHO:** la búsqueda en el catálogo de herramientas de esta sesión no devuelve herramientas PCSX2, PINE, GDB/debugger ni un buscador de herramientas que permita cargarlas. La búsqueda en `analysis/tools/` no encuentra un harness PCSX2/PINE/GDB; sus directorios son `p40` y `runtime_loop`, destinados a la investigación RECOMP. La consulta de procesos `pcsx2*`, repetida fuera del sandbox, no devuelve una instancia activa.

No existe en los mecanismos verificados una sesión de debugger original que permita colocar el breakpoint, comprobar PC y leer RAM. Esto no afirma que PCSX2 no esté instalado ni que su debugger esté averiado: **el acceso automatizado al debugger no está disponible en esta sesión**. Se aplica la salida `LIVE_ORACLE_BLOCKED` prevista explícitamente por el usuario. No se fabricó un cliente experimental, no se cambiaron opciones GDB y no se sustituyó el oráculo por RAM de RECOMP.

## Punto de medición re-verificado contra el ELF

Identidad leída nuevamente de `original/SLES_503.58`:

| Propiedad | Valor |
|---|---|
| SHA-256 | `d0753a6b3b2f00802a50758a872d8cf051725aa31c839aa8894eee30ce58bab4` |
| CRC32 | `77654AD2` |
| ELF entry | `0x00100008` |
| Main_init | `[0x0015BA90,0x0015BEA4)` |

Lectura estática mediante `analysis/tools/runtime_loop/elf_contract.py`, sin ejecutar el ELF:

```text
0015BB00  0C0571A8  jal MainGsSetDefDBuffDc (0015C6A0)
0015BB04  FFA00000  sd zero,0(sp)        ; delay slot
0015BB08  240A0001  addiu t2,zero,1      ; continuación UI

0015BB28  0C0571A8  jal MainGsSetDefDBuffDc (0015C6A0)
0015BB2C  70005E28  paddub t3,zero,zero  ; delay slot
0015BB30  960205D8  lhu v0,0x5D8(s0)    ; continuación movie, antes del parche
```

**HECHO estático:** el breakpoint UI exacto es **`0x0015BB08`**, pues la llamada MIPS retorna a PC de JAL + 8. **PC usado en vivo: ninguno.** No confundir el retorno al wrapper en `0x0015C6CC` con el retorno a Main_init solicitado.

## Predicción y observación: tablas separadas

Todos los valores de la columna «esperado» son **predicciones a comprobar**, intactas respecto al prompt. No son datos PCSX2 obtenidos aquí. FBP se decodifica como `raw & 0x1FF`.

| Campo UI | Dirección guest EE | Tamaño | Raw esperado | FBP esperado | Raw / FBP observado vivo |
|---|---:|---:|---:|---:|---|
| disp0.dispfb | `0x00740780` | 8 bytes | `0x0000000000001000` | `0x00` | UNKNOWN / UNKNOWN |
| disp1.dispfb | `0x007407B8` | 8 bytes | `0x0000000000001038` | `0x38` | UNKNOWN / UNKNOWN |
| slot0 FRAME_1 | `0x007407F0` | 8 bytes | `0x0000000000080038` | `0x38` | UNKNOWN / UNKNOWN |
| slot0 FRAME_2 | `0x00740870` | 8 bytes | `0x0000000000080038` | `0x38` | UNKNOWN / UNKNOWN |
| slot1 FRAME_1 | `0x00740960` | 8 bytes | `0x0000000000080000` | `0x00` | UNKNOWN / UNKNOWN |
| slot1 FRAME_2 | `0x007409E0` | 8 bytes | `0x0000000000080000` | `0x00` | UNKNOWN / UNKNOWN |

ZBUF opcional: leer 8 bytes en `0x00740800`, `0x00740880`, `0x00740970`, `0x007409F0` (FRAME correspondiente +0x10). Predicción `ZBP=0x70`, decodificación `raw & 0x1FF`; raw esperado para los argumentos UI `0x000000000A000070`. Valores vivos: UNKNOWN.

**ORIGINAL_UI_CONTRACT_CONFIRMED = NO** en el sentido de que no se obtuvo la confirmación requerida, no de que una medición lo haya refutado. Predicción vigente: DISPLAY `{0,38}`, DRAW `{38,0}`, ambos contextos; ZBP `70`.

## Instrucciones manuales mínimas y exactas

1. Usar PCSX2 con la misma revisión `SLES_503.58` indicada arriba. Abrir el debugger **EE** y colocar un breakpoint de ejecución en **`0x0015BB08`** antes de que el arranque original alcance Main_init. Si ya pasó ese punto, repetir el arranque original con el breakpoint preparado; no usar un savestate posterior como prueba de este retorno.
2. Al detenerse, registrar el **PC realmente mostrado** y verificar que es `0x0015BB08`. Registrar `s0`; el layout esperado usa `s0=0x007406A0`, con el struct UI en `s0+0xD0=0x00740770`. Si difiere, conservar ese dato y parar la comparación por direcciones fijas; no reajustarlas silenciosamente.
3. Sin reanudar, leer los seis campos de la tabla, **8 bytes por campo**, desde memoria guest EE. Copiar los valores crudos o conservar un dump binario. Si la vista muestra words de 32 bits: dirección A = word bajo y A+4 = word alto; combinar `(alto << 32) | bajo` por ser little-endian.
4. Si resulta más cómodo, guardar un único dump de **`0x280` bytes desde `0x00740770`**, rango `[0x00740770,0x007409F0)`. Cubre los seis campos FBP y los primeros tres ZBUF. Para incluir también el cuarto ZBUF puede ampliarse a `0x290` bytes. Conservar siempre PC, identidad del juego y direcciones junto al dump.
5. Comparar los seis raw y FBP con la tabla. Si los seis coinciden, confirmar el contrato y que las escrituras finales esperadas se realizaron. Si cualquiera difiere, registrar el valor tal cual y detenerse: el gate falla y no se implementa.

La lectura de seis valores en este punto basta para el objetivo primario; no se requiere una traza de todo el SDK ni una evaluación visual de los menús.

## Gparam: comprobación opcional

El cuerpo original de `sceGsGetGParam`, re-verificado en este turno:

```text
00100270  3C020050  lui v0,0x0050
00100274  03E00008  jr ra
00100278  2442BC90  addiu v0,v0,-0x4370   ; delay slot
```

Dirección resultante: **`0x004FBC90`**. En la misma pausa UI, leer **8 bytes** allí. También pueden leerse por separado 2 bytes en `0x004FBC90` (interlace) y 2 bytes en `0x004FBC94` (ffmode).

Condición predicha:

```text
(gparam64 & 0x0000FFFF0000FFFF) == 0x0000000100000001
```

Con ella, BEQ `0x00101DA0` conduce a `0x00101DB4`. Si no se cumple, la segunda condición comprueba el halfword interlace: cero también entra en la cola; distinto de cero toma BNE `0x00101DAC` hacia `0x00101E00`. No fijar como esperado un raw completo de gparam: contiene otros campos, incluido modo/revisión, que esta comparación enmascara.

Opcionalmente, un breakpoint en `0x00101DB4` durante la primera construcción UI confirma la entrada directamente, pero no sustituye comprobar los seis resultados finales. **Gparam vivo y ejecución de cola en este turno: UNKNOWN.**

## Movie: solo si resulta trivial tras confirmar UI

No se midió en este turno. Breakpoint opcional **`0x0015BB30`**, continuación de la segunda llamada, antes de que Main_init escriba los parches. Las predicciones siguen intactas:

| Campo movie | Dirección | Raw pre-parche esperado |
|---|---:|---:|
| disp0 | `0x00740C40` | `0x0000000000001000` |
| disp1 | `0x00740C78` | `0x0000000000001070` |
| slot0 FRAME_1 | `0x00740CB0` | `0x0000000000080070` |
| slot0 FRAME_2 | `0x00740D30` | `0x0000000000080070` |
| slot1 FRAME_1 | `0x00740E20` | `0x0000000000080000` |
| slot1 FRAME_2 | `0x00740EA0` | `0x0000000000080000` |

Leer 8 bytes en cada dirección. El cambio predicho posterior es disp1 `0x1070→0x10A0` y FRAME slot0 `0x80070→0x800A0`. Main_init escribe disp1 en `0x0015BB48`, FRAME_1 en `0x0015BB60` y FRAME_2 en el delay slot `0x0015BB74`; en `0x0015BB78` las tres escrituras ya se han ejecutado. La medición movie es secundaria y su ausencia no bloquearía una UI confirmada.

## Respuestas requeridas

1. Oráculo original obtenido: **NO**.
2. PC exacto: **0x0015BB08 verificado estáticamente; ninguno usado en vivo**.
3. UI disp0 vivo raw/FBP: **UNKNOWN / UNKNOWN**.
4. UI disp1 vivo raw/FBP: **UNKNOWN / UNKNOWN**.
5. UI slot0 FRAME_1 vivo: **UNKNOWN / UNKNOWN**.
6. UI slot0 FRAME_2 vivo: **UNKNOWN / UNKNOWN**.
7. UI slot1 FRAME_1 vivo: **UNKNOWN / UNKNOWN**.
8. UI slot1 FRAME_2 vivo: **UNKNOWN / UNKNOWN**.
9. Coincidencia viva DISPLAY `{0,38}`, DRAW `{38,0}`: **UNKNOWN**.
10. Cola definitivamente ejecutada en vivo: **UNKNOWN**.
11. Movie SDK medido: **NO**.
12. Transición movie 70→A0 medida: **UNKNOWN**; solo predicción estática.
13. Reconstrucción P418 confirmada mediante original vivo: **UNKNOWN**. No se encontró nueva refutación estática, pero repetir el análisis no satisface el gate dinámico.
14. Implementación lista según este prompt: **NO**.

## Decisión y siguiente paso

**NEXT: P4.1.9 — ORIGINAL_UI_DBUFF_LIVE_MEASUREMENT_COMPLETION.** Obtener la medición anterior mediante debugger disponible o datos manuales del usuario. No avanzar a un fix por ausencia de contradicción. Solo si el gate original confirma los seis campos quedaría habilitado el futuro P4.1.10 solicitado: modificar disp1 y draw01/draw02 según la cola original, preservando slot1=0 de P4.1.3. No se implementó ni se recomendó otro candidato.

Host latch, clear/redraw, preferredSource y FIELD no se reabrieron ni se culpabilizaron. No hay medición que justifique cambiar su comportamiento en este prompt.

## Cierre

Único archivo añadido por P419: este informe. Main/vendor HEADs conservados; vendor sin cambios, main sin diff tracked y con los once informes anteriores más este sin trackear. No se editó P418 ni la nota canónica. **NO source changes; NO vendor change; NO build; NO run de RECOMP; NO commit; NO push; NO clean; NO regenerate.** CHECKPOINT_DECISION: **NO_COMMIT**.
