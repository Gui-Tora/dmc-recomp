# P4.1.9 — Addendum: oracle original completado manualmente por el usuario

Este addendum NO reescribe
[BLOCKER_004_P419_ASTRA_ORIGINAL_UI_DBUFF_CONDITIONAL_TAIL_ORACLE.md](BLOCKER_004_P419_ASTRA_ORIGINAL_UI_DBUFF_CONDITIONAL_TAIL_ORACLE.md).
Ese informe es correcto tal como fue escrito: en el momento en que Astra lo
completó, el acceso automatizado al debugger de PCSX2 no estaba disponible
en esa sesión, y su conclusión (`P419_LIVE_ORACLE_BLOCKED`, los seis campos
`UNKNOWN`) refleja fielmente ese hecho. Astra no se equivocó; simplemente
el oracle en vivo llegó **después**, por una vía distinta.

## Cronología

1. P4.1.8 (Astra) reconstruye estáticamente, por desensamblado directo de
   bytes del ELF, la cola condicional de `sceGsSetDefDBuffDc`
   (`0x101D70-0x101DF8`) — HECHO por bytes, no oracle en vivo.
2. P4.1.9 (Astra) intenta obtener confirmación en vivo vía PCSX2/MCP/GDB.
   Herramientas no disponibles en esa sesión → `LIVE_ORACLE_BLOCKED`.
3. **Después de P4.1.9**, el usuario midió manualmente los mismos seis
   campos + ZBUF en PCSX2 original, usando exactamente los breakpoints y
   direcciones que P4.1.9 ya había especificado como instrucciones
   mínimas para un operador humano (sección "Instrucciones manuales
   mínimas y exactas" de ese informe).
4. Durante este mismo prompt (P4.1.10), las herramientas MCP de PCSX2
   SÍ aparecieron disponibles en la sesión. Se intentó una verificación
   independiente adicional (`configure_gdb_settings` + relanzamiento de
   PCSX2), pero el servidor MCP se desconectó al reconfigurar el GDB
   server — el mismo modo de fallo ya documentado por P4.1.4/P4.1.9. El
   usuario confirmó explícitamente, en mensaje directo, que los valores
   below fueron medidos por él/ella manualmente, con posterioridad a
   P4.1.9, y que no se debía reintentar MCP/GDB.

## Valores medidos (HECHO, oracle en vivo, procedencia: usuario, manual, post-P4.1.9)

### UI

Breakpoint EE: `PC = 0x0015BB08` (retorno de la primera llamada a
`MainGsSetDefDBuffDc`, struct UI `0x740770`, verificado `s0=0x007406A0`).

| Dirección | Raw (LE u64) | Campo | FBP (`raw & 0x1FF`) |
|---|---|---|---:|
| `0x740780` | `0x0000000000001000` | disp0.dispfb | `0x00` |
| `0x7407B8` | `0x0000000000001038` | disp1.dispfb | `0x38` |
| `0x7407F0` | `0x0000000000080038` | slot0 FRAME_1 | `0x38` |
| `0x740870` | `0x0000000000080038` | slot0 FRAME_2 | `0x38` |
| `0x740960` | `0x0000000000080000` | slot1 FRAME_1 | `0x00` |
| `0x7409E0` | `0x0000000000080000` | slot1 FRAME_2 | `0x00` |

**UI DISPLAY = {0x00, 0x38}. UI DRAW = {0x38, 0x00}.**

### Movie (retorno SDK, antes de los parches de `Main_init`)

Breakpoint EE: `PC = 0x0015BB30` (retorno de la segunda llamada, struct
película `0x740C30`, antes de que `Main_init` reescriba `disp1`/slot0).

| Dirección | Raw (LE u64) | Campo | FBP |
|---|---|---|---:|
| `0x740C40` | `0x0000000000001000` | disp0.dispfb | `0x00` |
| `0x740C78` | `0x0000000000001070` | disp1.dispfb | `0x70` |
| `0x740CB0` | `0x0000000000080070` | slot0 FRAME_1 | `0x70` |
| `0x740D30` | `0x0000000000080070` | slot0 FRAME_2 | `0x70` |
| `0x740E20` | `0x0000000000080000` | slot1 FRAME_1 | `0x00` |
| `0x740EA0` | `0x0000000000080000` | slot1 FRAME_2 | `0x00` |

ZBUF observado (idéntico en los 4 campos, ambos contextos de ambos
slots): `0x000000010A0000E0` → ZBP=`0xE0`, ZMSK=1.

**MOVIE SDK DISPLAY = {0x00, 0x70}. MOVIE SDK DRAW = {0x70, 0x00}. ZBUF = 0xE0.**

## Coincidencia con la reconstrucción estática

Los seis campos UI y los seis campos movie coinciden **exactamente**, campo
por campo, con la predicción de P4.1.8 (tabla "Valores esperados:
iniciales y después del juego", sección 7). Esto promueve la
reconstrucción estática de P4.1.8 de INFERENCIA-fuerte-por-bytes a
**HECHO confirmado en vivo** para los doce campos auditados (seis UI +
seis movie pre-parche).

## Autoridad para P4.1.10

Este addendum, junto con P4.1.8 (desensamblado) y P4.1.9 (instrucciones de
medición), constituye la base evidencial de
[BLOCKER_004_P410_SCEGSSETDEFDBUFFDC_CONDITIONAL_TAIL_PARITY_FIX.md](BLOCKER_004_P410_SCEGSSETDEFDBUFFDC_CONDITIONAL_TAIL_PARITY_FIX.md).
`ORIGINAL_UI_CONTRACT_CONFIRMED = YES` (superando el `NO` — por ausencia de
medición, no por refutación — que dejó registrado P4.1.9 en su momento).

## Cierre

Documento nuevo únicamente. No se modificó `BLOCKER_004_P419_...md`,
`BLOCKER_004_P418_...md`, ni ningún otro informe histórico. Main/vendor sin
cambios por este addendum en sí (los cambios de producción, si los hay,
quedan documentados en el informe P4.1.10 correspondiente).
