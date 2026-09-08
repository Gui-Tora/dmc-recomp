# Tabla de objetos en 0x0110e720 — hipótesis (no confirmada del todo)

- ELF: `SLES_503.58`, Devil May Cry (Europa/PAL) v1.02, sha256
  `d0753a6b3b2f00802a50758a872d8cf051725aa31c839aa8894eee30ce58bab4`,
  crc32 `77654AD2`, entry `00100008`. Cualquier dirección de esta nota es
  específica de este ELF exacto; no portar a otra región/revisión sin repetir
  el análisis.
- Entorno: Ghidra 12.1.3 (JDK 21 dedicado vía `JAVA_HOME_OVERRIDE`), proyecto
  headless en `analysis/ghidra/project` (`dmc.gpr`), auto-análisis por
  defecto sin extensión de procesador adicional.

## Hallazgo

Existen dos tablas distintas relacionadas:

1. `_DAT_0110ea20/ea24/ea28` (stride 4): guarda punteros a zonas de trabajo
   separadas — `&DAT_0110e720`, `0x110e920`, `0x110e9a0` — no contiguas entre
   sí (separación de 0x200 y luego 0x80 bytes). Se llenan desde
   `Om_work_init()`. Lectura: un "directorio" de sistemas/pools distintos,
   no un único array homogéneo.

2. `0x0110e720` en adelante (stride 4, confirmado real): un array de
   punteros a objetos de la room activa. Confirmado con script headless
   propio (`DumpObjectTableXrefs.java`, no versionado, ver comando abajo)
   escaneando 24 slots (`0x0110e720`..`0x0110e77c`):

   | Slot | Dirección    | Xrefs | Funciones (muestra)                                            |
   |-----:|--------------|------:|-----------------------------------------------------------------|
   |    0 | 0x0110e720   |    65 | R121_gate_model_init, Om_work_init, R00e_evt_scene02/03/06/07_init, Om_Room_init |
   |    1 | 0x0110e724   |    49 | R112_main, R121_init, Om_work_init, R401_switch_init             |
   |    2 | 0x0110e728   |    23 | R408_Event11_03, R408_Event11_04, Bridge_up, R405_DoorCheck       |
   |    3 | 0x0110e72c   |    24 | DevilSwitchCheck, R104_stained_model_init                        |
   |    4 | 0x0110e730   |    19 | R506_sky_init, R104_stained_model_set                            |
   |    5 | 0x0110e734   |    11 | r401_zenmetu_chk, R30d_switch_init                               |
   |    6 | 0x0110e738   |    20 | R401_barrier_up, R401_barrier_init, R106_get_sword                |
   |    7 | 0x0110e73c   |     7 | R30d_main, R20b_water_move                                       |
   |    8 | 0x0110e740   |     2 | R30d_main, R401_main                                             |
   |    9 | 0x0110e744   |    13 | r21d_init, R308_init, R202_sky_move                              |
   |   10 | 0x0110e748   |     3 | R205_huuin_door_chk, R30d_return_event                           |
   |   11 | 0x0110e74c   |     7 | R401_hasira_init, katVu0SetClip                                  |
   |   12 | 0x0110e750   |     8 | R30a_return_event, R407_obj_at_ck, heart_start_skip               |
   |   13 | 0x0110e754   |     3 | R401_main, R407_obj_at_ck                                        |
   |   14 | 0x0110e758   |     5 | katVu0SetClip, R401_hasira_move2                                 |
   |   15 | 0x0110e75c   |     4 | R409_init, capVu0ScaleVectorXYZ2                                 |
   |   16 | 0x0110e760   |     2 | R409_main                                                        |
   |   17 | 0x0110e764   |     2 | R409_main                                                        |
   |   18 | 0x0110e768   |     1 | Devil_switch_check                                               |
   |   19 | 0x0110e76c   |     2 | (dos llamadas sin nombre)                                        |
   |   20 | 0x0110e770   |     0 | —                                                                 |
   |   21 | 0x0110e774   |     1 | Devil_switch_check                                               |
   |   22 | 0x0110e778   |     1 | (sin nombre)                                                     |
   |   23 | 0x0110e77c   |     4 | R405_devil_switch_check, heart_start_skip                        |

   Todos los slots devuelven `value=<unreadable>`: confirma que es RAM de
   trabajo (BSS/runtime), no dato estático del ELF; Ghidra no puede resolver
   el contenido de forma estática.

## Lectura propuesta

- Slot 0 lo tocan prácticamente todas las rooms (`R000`, `R121`, `R00e`,
  `R21b`, `R214`, `R216`...) → cada nivel reutiliza el mismo array y coloca
  ahí sus propios objetos al cargar.
- El conteo de xrefs decrece con el índice, con hueco en el slot 20 y
  repunte en 21-23 → el tamaño "típico" usado ronda 16-20 slots, con el
  resto ocupado solo en casos puntuales. Tamaño real del array aún no
  confirmado (no se escaneó más allá de `0x0110e77c`).
- Consistente con `struct Object { ...; uint32_t flags (~+0x08/+0x0c); ...; uint32_t campo (+0x14); ... }`
  y `Object *gRoomObjects[N]` empezando en `0x0110e720`, según lo visto en
  `R408_Event11_04` (`*(_DAT_0110e72x + 8) &= ~2`) y `R121_gate_model_init`
  (`*(ushort*)(_DAT_0110e720+0xc) |= 0x40; *(undefined4*)(iVar1+0x14) = 0x7fe0`).
- Validación cruzada: `R408_Event11_04` aparece exactamente en el slot 2
  (`0x0110e728`), tal como se había visto a mano en Ghidra antes de correr
  el script.

## Pendiente de confirmar

- Tamaño exacto del array (escanear más allá de `0x0110e77c` hasta que las
  xrefs se agoten de forma sostenida).
- Significado real de los offsets `+0x08`, `+0x0c`, `+0x14` dentro de cada
  objeto (¿flags, tipo, puntero a modelo?).
- Si `_DAT_0110ea20/ea24/ea28` (el "directorio" de `Om_work_init`) resuelve
  siempre a la misma zona `0x0110e720` o cambia entre rooms.

## Relevancia para el bring-up

No bloquea M0-M2 (arranque). Es relevante para M8+ (Dante controlable /
combate) si se necesitan Game Overrides que lean o manipulen objetos de
room directamente.

## Cómo reproducir

Script usado (no forma parte del repo, vivía en un scratch temporal):
`DumpObjectTableXrefs.java`, ejecutado con Ghidra headless en modo
`-process -readOnly -noanalysis` sobre el proyecto ya importado, escaneando
24 direcciones desde `0x0110e720` en pasos de 4 bytes, imprimiendo valor y
xrefs (con nombre de función contenedora) de cada una.
