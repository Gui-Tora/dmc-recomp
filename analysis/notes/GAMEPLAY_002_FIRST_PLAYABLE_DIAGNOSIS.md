# GAMEPLAY_002 — Diagnóstico de FIRST_PLAYABLE_FRAME

ELF `SLES_503.58`, SHA-256 `d0753a6b3b2f00802a50758a872d8cf051725aa31c839aa8894eee30ce58bab4`, CRC32 `77654AD2`,
entry `0x00100008`. Base: `master` `eccfa607` (28 parches, `patched_commit e3d4250d`).

**Alcance:** diagnóstico estático y lectura de evidencia existente. No hay corridas nuevas, fixes, cambios en los
parches ni código generado.

Marcas: **HECHO**, **INFERENCIA**, **HIPÓTESIS**, **UNKNOWN**.

## 1. Estado reproducible

- **HECHO:** la única ejecución que pasó de 0x231 al castillo es la corrida de GAMEPLAY_001L, sección 12
  (`analysis/local/gameplay_001l/run_post_office/`).
  - Exe de los parches 1–21: no tenía los parches 22 (cadena DMA), 23 (clip de VU0) ni 24 (skinning `V4-5`).
  - `capVu0ecossin` y `Get_light_str` eran *stand-ins* `ret0` en ese exe; hoy están restauradas en `master`.
  - Entrada solo por P311 (`DMC_P311_PAD_FILE`).
- **HECHO:** con `master` (parches 1–28) nunca se ha ejecutado más allá de la oficina. Por tanto, no hay ninguna
  observación de Dante en el castillo con el código actual.
- **HECHO:** el único checkpoint compatible es el del menú principal de VISUAL_001. Guardar más adelante falló
  dos veces:
  - durante una película: «CDMODULE: movie stream active»;
  - en la oficina: «GS vertex queue holds a half-submitted primitive».

## 2. Qué funciona (hasta donde hay evidencia)

**HECHO (corrida de la sección 12 y notas 001L):**

- NEW GAME → 0x232 → oficina → 0x231 → carga de `0x1D4` → escena 3D del castillo con un segundo clip XAG.
- El VSync avanza hasta el final.
- START abre y cierra el menú de estado (vida, Vital Star, orbes ×0), que solo existe en gameplay.

**HECHO (`master`):** en la oficina, la cadena DMA (parche 22), el skinning (parche 24) y la trigonometría
restaurada dibujan personajes. Ese es el estado de VISUAL_001 y PERF_001.

## 3. Ruta NEW GAME → castillo

| Paso | Debería ejecutarse (estático) | Se ha ejecutado (evidencia) |
|---|---|---|
| Bucle | `ps2_main` 0x15B3A0 → `Pad_read(0x7406A0)` 0x1A3B50 cada frame; `Game_loop` 0x134A40 | sí (todo el log) |
| Película 0x232 | — | ticks ~20340 en adelante |
| Oficina (evento) | `Event_scheduler` 0x44C350, `Pl_r0_scenario` 0x13F1A0 | ticks hasta ~32393 |
| 0x231 | — | ticks 32393–33893 |
| Castillo | `Mission_start` 0x4A1F60, `Pl_r0_init` 0x138960 → `Pl_r1_wait` 0x139750 | carga en el tick ~33893; cámara fija desde ~35700 |
| Control | `Game_loop` → `Get_joystick_theta(0x5C65B0)` 0x152B70 → `Pl_r1_wait` → walk/run | **UNKNOWN** (sin sonda) |
| Dibujado | `Scr_draw` 0x130DE0, `Md_draw_entry` 0x251180 / `_db` 0x250F70 | **UNKNOWN** en el castillo con `master` |

**HECHO (log del exe antiguo):**

- `capCopyMatrix` (`ret0`) se llama desde `Motion_set2` (ra 0x253218) entre 50 y 99 veces, en los ticks
  35573–35813, solo en el castillo.
- `katVu0Setmat4_shw` se llama desde `Shw_trans_tag` (sombras), y su recuento pasa de 12000 a 18550 en el castillo.

**INFERENCIA:** en el castillo se asignan animaciones a algún modelo y se generan sombras. No está demostrado que
ese modelo sea Dante: `Motion_set2` también la llaman las rutinas `Em*`, `Om*` y `Load_weapon`.

## 4. Mando: análisis estático completo

**HECHO (desensamblado del ELF):**

1. **`Pad_read`**
   - Recibe `a0 = 0x7406A0` (`ps2_main` en 0x15B8EC; `s0 = 0x7406A0` desde 0x15B3B0).
   - Invierte botones y sticks: `xori 0xFFFF` y `not`.
   - Si `0x75385C & 4 == 0`, que en la INFERENCIA es «pad no en modo analógico», fuerza los sticks a neutro
     (`0x80808080` antes de invertirlos).
   - Después llama a `Pad_set(0x7406A0, 1)`, a `Key_set(0x740730, 0x7406A0)` y a `Pad_actuater`.
2. **`Pad_set` (0x1A4230)**
   - Escribe los botones en `+0x00` y copia los sticks a `+0x36..+0x45`; el stick izquierdo X/Y queda en
     `+0x38/+0x39`.
   - Si `+0xC != 0`, convierte el stick en bits de dirección digitales (umbrales `>0xB0` y `<0x50`).
   - `+0x12` y `+0x14` son bits de autorrepetición, con contadores 10/2 y 20/4.
   - **La conversión va solo de stick a digital. No existe la inversa.**
3. **`Key_set` (0x1A4020)**
   - Aplica la tabla de configuración y copia los sticks a `key+0x1C..` (stick izquierdo en `0x74074E/0x74074F`).
   - Si `0x742168 & 0x8000` está activo, salta a `Key_stop_mask` y no copia los sticks. El contador `0x753854`
     limpia ese bit.
4. **`Game_loop` 0x134BAC–0x134BC8**
   - Si `0x742168 & 0x80000000 == 0`, llama a `Get_joystick_theta(0x5C65B0)`.
5. **`Get_joystick_theta` (0x152B70)**
   - Calcula el ángulo (`func_19FE90`, atan2) y la magnitud (`func_19FB00`, sqrt) **solo a partir de `0x74074E`
     y `0x74074F` (stick izquierdo)**.
   - Escribe el ángulo y la magnitud en `0x5C65B0 + 0x1434` y `+0x1438`.
6. **`Pl_r1_wait` (0x1399D4) y `Check_walk_run` (0x152FE0)**
   - Leen la magnitud en `+0x1438`: ≤ 48,0 → no se mueve; ≤ 104,0 → camina; mayor → corre.
7. **Escritores de `+0x1438`:** en todo el texto, la única escritura sobre el jugador es la de
   `Get_joystick_theta`. Las otras dos (`Em0b_R1_mv11`, `Em05_move`) operan sobre estructuras de enemigos.
8. **`Get_joystick_dir` (0x152C40)** usa los bits digitales cuando la magnitud es ≤ 48. Sin embargo, solo lo
   llaman `Pl_swim_control_e` y `Pl_ds_move` (natación y otro modo), no la caminata normal.

**HECHO (runtime):**

- P311 llama a `setPadOverrideState(mask, 128, 128, 128, 128)`: sticks siempre centrados.
- En la corrida de la sección 12 se enviaron `0xFF7F` (LEFT) y `0xFFBF` (DOWN), es decir, solo la cruceta.
- `Pad.cpp` solo genera stick desde el teclado (WASD) si `analogMode` está activo, y no lo hace si hay override.

**INFERENCIA (fuerte, se sigue de los hechos 1–8):** con la entrada de P311, Dante no puede pasar de
`Pl_r1_wait` a caminar ni a correr, **tampoco en una PS2 real**: la magnitud vale ~1,4 (stick neutro invertido a
0x7F) y el umbral es 48. Por tanto, «LEFT y DOWN no producen cambios» (sección 12) **no es evidencia de que el
control esté bloqueado**. La prueba no podía mover a Dante.

**Corrección del razonamiento previo:**

- **Hipótesis de la sección 12:** la falta de respuesta a las direcciones se relacionaba con los `ret0` activos.
- **Qué la apoyaba:** las direcciones no tenían efecto visible y `ecossin` devolvía 0.
- **Qué la refuta:** el análisis estático anterior. Con la cruceta, la caminata no se activa por diseño del juego,
  así que la observación no discrimina.
- **Estado actualizado:** la capacidad de control con `master` es UNKNOWN y requiere entrada en el stick
  analógico.

**UNKNOWN:**

- si en el castillo `0x75385C & 4` está activo (pad en modo analógico);
- el valor de `0x742168` (bits 0x8000 y 0x80000000);
- el valor de `0x753854`.

## 5. Dante: ¿existe, se dibuja?

- **HECHO (exe antiguo, oficina, sección 13):**
  - con `ecossin` en stub, la matriz de Dante tenía 16 NaN y una profundidad de 0x80000000, y
    `Md_draw_entry_db` la descartaba;
  - en PCSX2 la traslación vale (9243, −3098, 2634);
  - antes del parche 22, la cadena DMA truncada impedía dibujar personajes.
- **INFERENCIA:** la ausencia de Dante en la corrida del castillo se explica por defectos ya corregidos en
  `master`. Esto no demuestra que con `master` sí aparezca.
- **UNKNOWN:** si con `master` el objeto del jugador en `0x5C65B0` se inicializa en el castillo (`Pl_r0_init`) y
  si su matriz es finita y pasa el recorte.

## 6. Los 15 `ret0` (sin asumir causalidad)

Llamadas observadas en el castillo en la corrida antigua; el recuento de la oficina está en
`GAMEPLAY_001_RET0_RESTORATION_STATUS.md`.

| `ret0` | Castillo | Llamador | Relevancia para Dante |
|---|---|---|---|
| `capCopyMatrix` | sí, ~50–99 llamadas, ticks 35573–35813 | `Motion_set2` | **candidato**: copia la matriz de cada parte a +0x80 (INFERENCIA: origen de la interpolación entre animaciones); con `ret0` no copia nada |
| `katVu0Setmat4_shw` | sí, 12000 → 18550 | `Shw_trans_tag` | sombras; no afecta al modelo |
| `katVu0Clip_ld` / `SetClip` | no en el castillo | `Scr_parts_near_trans` | escenario |
| `capVu0Compare` | solo en la oficina | `srp_05_init` | efecto |
| `capVu0ScaleVectorXYZ2` | solo en la oficina | `Lightning_*` | relámpagos |
| `capVu0MulMatrix_r` | no observada | `Calc_make_matrix`, `Sub_calc_matrix` | potencial; sin llamadas registradas |
| resto (`Calc_md`, `InterpNode`, `ClipCheck2`, …) | ver nota de estado | `Scr_parts_near_trans`, `Mul_index_data_obj*` | escenario |

- **HIPÓTESIS:** si `capCopyMatrix` es necesaria para la mezcla de animaciones, su `ret0` deja la matriz de origen
  sin inicializar o con el valor anterior durante la transición. Podría deformar a Dante, pero no hacer que
  desaparezca. No está demostrado.

## 7. PCSX2

- **HECHO:** los savestates 07 y 08 están en la oficina. No hay ningún estado de PCSX2 después de 0x231.
- **Instrucciones de captura (direcciones demostradas en las secciones 3–4)**, en el primer frame controlable del
  castillo (tras 0x231 y el clip XAG):
  1. Guardar el estado en una ranura nueva.
  2. Leer:
     - `0x742168` (4 B);
     - `0x753854` (2 B);
     - `0x75385C` (4 B);
     - `0x7406A0..0x7406E6` (trabajo del pad);
     - `0x740730..0x74075C` (teclas);
     - `0x5C65B0` (jugador; volcar 0x1600 B).
  3. Repetir el paso 2 con el stick izquierdo inclinado a fondo y comparar `+0x1434`, `+0x1438` y los bytes
     iniciales `+0..+7`, que son el estado de la rutina.
  4. Repetir el paso 2 con la cruceta pulsada, para confirmar que no mueve a Dante.

## 8. Primera divergencia

**No localizada.** No existe ninguna observación de `master` en el castillo ni de PCSX2 en el castillo. La única
«divergencia» registrada (Dante invisible e inmóvil):

- se observó con un exe que tenía defectos de dibujado ya corregidos;
- se basó en una entrada digital que, según el código del juego, no puede mover a Dante.

## 9. Hipótesis (ordenadas)

1. **H1 — Ya funciona.**
   - Con `master`, Dante existe y se dibuja en el castillo, y se mueve con el stick analógico.
   - Lo que falló hasta ahora fue el exe antiguo (sin los parches 22 y 24, y con `ecossin` en stub) y el método de
     entrada (solo cruceta).
   - **Evidencia a favor:** las secciones 4–5.
2. **H2 — Modo analógico.**
   - El pad del runtime no llega a modo DualShock en el castillo (`0x75385C & 4 == 0`), así que incluso el stick
     queda neutro.
   - **Evidencia a favor:** `Pad.cpp` arranca en modo digital; `analogMode` lo fija `scePadSetMainMode`.
   - No hay traza.
3. **H3 — Animación o matriz.**
   - Dante existe pero se dibuja deformado o queda descartado por la animación (`capCopyMatrix` en `ret0`), o por
     otro cálculo de matriz del castillo no observado en la oficina.

## 10. Prueba mínima propuesta (requiere autorización)

**Una sola corrida desde el checkpoint de VISUAL_001 con `master` hasta el castillo.**

- **Instrumentación (solo `runtime/`, una unidad de compilación):**
  - Ampliar P311 para enviar también el stick izquierdo. Se modifica una herramienta de diagnóstico ya existente,
    no se añade ningún stub ni retorno ficticio.
  - Añadir una sonda que, cada 60 ticks a partir de 0x231, vuelque en binario: `0x742168`, `0x753854`,
    `0x75385C`, `0x7406A0..0x7406E6`, `0x740730..0x74075C` y `0x5C65B0` (0x1600 B).
  - Hacer capturas.
- **Secuencia:** NEW GAME, sin pulsar nada durante 0x232, la oficina y 0x231. En el castillo:
  1. START (menú de estado) y START para cerrarlo;
  2. stick izquierdo a fondo durante 3 s;
  3. cruceta durante 3 s, como control negativo.
- **Lectura del resultado:**
  - Dante visible y `+0x1438 > 48` → H1 confirmada, y se puede comparar el desplazamiento.
  - `0x75385C & 4 == 0` → H2.
  - Dante ausente o deformado con la matriz no finita → H3, con el volcado como evidencia primaria.

## 11. Coste

- **Compilación:** incremental, solo `runtime/dmc_overrides.cpp`.
- **Duración estimada de la corrida: 40–45 min.**
  - La oficina de `master` (~12 000 ticks) a 6,76 VSync/s supone ~30 min.
  - Las películas suman ~1 min.
  - El castillo hasta el control (~2 000 ticks) suma ~5 min.
  - Hay que añadir margen.
- Supera el umbral de 15–25 min fijado para esta fase, así que **no se ha ejecutado**.
- **Alternativa barata:** la captura en PCSX2 de la sección 7, que es manual pero establece el valor de
  referencia de las mismas direcciones antes de gastar la corrida.

## 12. Checkpoint durante el gameplay (fases A–D)

### Por qué no se podía guardar después de NEW GAME

**HECHO:** los dos rechazos conocidos venían de condiciones del punto seguro más estrictas que lo que el formato
ya serializa.

- **`CDMODULE: movie stream active`:**
  - El gate exigía `m_movie.active == 0`; el diseño (DEVSTATE_002 §3) pedía `alive = 0`, es decir, stream agotado.
  - DMC nunca envía `fno=0xC` al terminar una película. En la corrida post-oficina hubo 2 `MOVIE:start`
    (0x232 y 0x231), 0 `MOVIE:close`, último `MOVIE:chunk ... remaining=0x0` y `MOVIE:status alive=0`.
  - La sesión agotada queda `active = 1` hasta la siguiente `fno=9`: es una condición **persistente** durante
    todo el gameplay, no transitoria. Una petición pendiente nunca habría pasado.
  - Sus cinco campos (`active`, `resourceId`, `lba`, `totalSize`, `bytesConsumed`) se serializan y se restauran.
    El host abre y cierra el ISO dentro de cada RPC.
- **`GS vertex queue holds a half-submitted primitive`:**
  - El gate exigía `m_vtxCount == 0` (DEVSTATE_003B), pero la cola se serializa entera: `m_vtxCount`,
    `m_vtxIndex` y los 6 vértices, campo a campo.
  - Tras un kick de TRISTRIP, TRIFAN o LINESTRIP el GS retiene 1 o 2 vértices hasta el siguiente `PRIM`. También
    es una condición **persistente** mientras el último primitivo del fotograma sea una tira.
  - El backend CPU dibuja en `Submit`, de forma inmediata y sin lotes pendientes.

### Qué se ha cambiado

Parche 29, `patches/GAMEPLAY_002_checkpoint_gameplay_safe_point.patch`, documentado en `patches/README.md`:

- **CDMODULE:** el gate pasa a `active && bytesConsumed < totalSize`. Un stream con bytes pendientes se sigue
  rechazando.
- **GS:** se rechaza solo un recuento fuera de `[0, kMaxVerts]`, lo mismo que el lector ya rechaza.
- **`EeScheduler`**, mecanismos opt-in que, sin las variables de entorno, dejan el comportamiento como antes:
  - `PS2X_CHECKPOINT_DEFER_VSYNCS=N`: la petición sigue pendiente mientras la validación, que solo lee, la
    rechace, y se reintenta en cada frontera durante N VSync;
  - `PS2X_CHECKPOINT_AT_VSYNC=T` y `..._PATH`: guardado al llegar al tick T, sin sobrescribir.
- **Sin cambios de formato ni de ABI:** el `checkpointCompatId` sigue siendo `c3db86c0…`, y el checkpoint de
  VISUAL_001 carga.
- **Sin cabeceras tocadas:** el build incremental compiló 4 TU y enlazó; no se recompiló código generado.
- **Replay:** baseline más los 29 parches da el árbol `86a7089f…`, idéntico al vendor, con
  `patched_commit 8ee3ee0f5996e3659e375abf46e62c8f08178ef7`. Los parches 1–28 no cambian.

**Alternativa B, SAVE/LOAD nativo, descartada:**

- **INFERENCIA:** el guardado del juego está en el flujo de fin de misión (`Mission_clear_01_save`,
  `Save_main`). Exige terminar una misión jugando y no puede devolver al primer control de la misión 1.
- **HIPÓTESIS no comprobada:** cargar lleva al inicio de la misión siguiente.
- Con el checkpoint ya reparado no se ha desarrollado.

### Pruebas

**`ps2x_tests`:** 462 tests, los mismos 24 fallos preexistentes que en PERF_001 P3 (mismo conjunto). Dos tests
nuevos:

- **GS:** con 1 o 2 vértices en cola (tira, abanico, triángulo a medias y line strip), el GS restaurado dibuja,
  byte a byte, lo mismo que el original, y distinto de un GS con los mismos registros pero sin la cola.
- **CDMODULE:**
  - un stream con bytes pendientes se rechaza;
  - el agotado se acepta;
  - tras restaurar sigue `alive = 0` y no copia nada;
  - guardar → restaurar → guardar da un resultado idéntico.

**DMC, ejecutable Release con los 29 parches (`analysis/local/gameplay_002/`):**

- **D1, guardado en la oficina:**
  - Desde el checkpoint de VISUAL_001: CROSS en el tick 12909, fin de 0x232 en el tick 16264.
  - Petición en el tick 16384, guardado en el tick 16385, al primer intento.
  - El archivo contiene `vtxCount = 2` y la película 0x232 agotada, así que con los gates anteriores se habría
    rechazado.
- **D2, restauración en un proceso nuevo:**
  - Carga correcta y nuevo guardado diferido.
  - Esperó dos condiciones **transitorias** (`GS transfer partially complete`, `activeInv != 0`) y guardó en
    el primer punto seguro, en el tick 16386, tras 121 intentos.
- **D3–D5, comparación exacta:**
  - La ejecución original (D3) guardó en el tick 16387 y después en el tick 16700.
  - Ese checkpoint se restauró dos veces en procesos nuevos (D4, D5), cada una guardando en el tick 16700.
  - D4 y D5 son idénticos en las 12 secciones.
  - Frente a D3 son idénticos en 11 de 12 secciones: MEM (39,9 MB), MMIO, GS, SCHED, IOP, SIFRPC, CD, MC,
    AUDIO, PAD y MPEG.
  - En CORE difieren 4 bytes: el contador `VU1State.cycles`, que solo sirve de contabilidad. Su semántica tras
    la carga ya está documentada en `ps2_vu1_native_dispatch.inc`, y no afectó a ningún estado del juego en
    313 ticks. No se toca.
- **Corrección metodológica:**
  - Las capturas de `shot.py` del tick «16685» en D1 y D2 mostraban la cámara en posiciones distintas.
  - La comparación exacta demuestra el mismo estado. La captura no está ligada al tick: depende del log con
    búfer y de la presentación asíncrona, así que no sirve como prueba de equivalencia.

### Instrumentación preparada (fase C)

- **P311 (`runtime/p311_pad_test.inc`):**
  - Acepta `MASK`, como antes, o `MASK LX LY [RX RY]` en hexadecimal.
  - Si el archivo no se puede leer o tiene más de 1 s, vuelve a neutro: botones sueltos y sticks en 0x80.
  - **HECHO (D6):** el stick llega al trabajo del pad invertido (`FF→0`, `00→255`), como predice `Pad_read`. La
    cruceta RIGHT llega a las teclas (`0x04000000`).
- **Sonda `runtime/g002_gameplay_probe.inc` (`DMC_G002_PROBE`):**
  - *(Retirada del runtime en la consolidación, GAMEPLAY_003 §9.4; copia local en `analysis/local`.)*
  - Escribe una línea en cada múltiplo de N ticks y volcados binarios acotados (5,8 KB por muestra).
  - Registra `0x742168`, `0x753854`, `0x75385C`, el modo del pad, los sticks del pad y de las teclas, y el
    jugador en `0x5C65B0`: estado, magnitud, ángulo, posición, número de flotantes finitos en la matriz,
    profundidad y flags de dibujado.
  - Cuenta las entradas de `0x5C65B0` en `Md_draw_entry_db` y los tags OT que inserta.
  - Solo lee.
- **HECHO, oficina con evento, RECOMP frente a los savestates s07 y s08 de PCSX2 (existentes):**

  | Campo | PCSX2 | RECOMP |
  |---|---|---|
  | `0x742168` | `0x80808080` | `0x80808080` |
  | Estado del pad (`0x75385C`) | `0x77` | `0x77` |
  | Estado del jugador (`+0..+3`) | `01000100` | `01000100` |
  | Posición | (9286, −3528, 2757) | (9286, −3528, 2757) |
  | Modo del pad (`0x753871`) | `0x79` (DualShock 2 con presión) | `0x73` |

  - Con `0x742168 & 0x8000`, `Key_set` no copia el stick a las teclas: `key_lx` sigue en 128 aunque
    `pad_lx` cambie. Es el comportamiento del juego durante el evento, igual que en PCSX2.
  - **Divergencia menor anotada:** el modo del pad. Su efecto es UNKNOWN; `0x75385C & 4` coincide.
- **H2 (sección 9):** el pad sí está en modo analógico (`0x75385C & 4`) en la oficina. Queda debilitada, pero no
  refutada para el castillo.

## 13. Fase E: corrida hasta el castillo y restauración (2026-10-29)

**Referencia de PCSX2:**

- Savestate 9 (`SLES-50358 (7D8F539A).09.p2s`, SHA-256 `cbb1d016…2752`), copiado y extraído en
  `analysis/local/gameplay_002/pcsx2_s09/`. El original no se ha tocado.
- **HECHO:** con Dante quieto ante la puerta del castillo:

  | Campo | Valor |
  |---|---|
  | `0x742168` | 0 |
  | `0x753854` | 0 |
  | `0x75385C` | `0x77` |
  | Modo del pad | `0x79` |
  | Stick (pad y teclas) | `7E/7B` |
  | Ángulo | −2,7611 |
  | Magnitud | 5,385 |
  | Rutina | `01 43 03 00` |
  | Posición | (8897,76, −3527, 2675,5) |
  | Flotantes finitos en la matriz | 16 |
  | Profundidad | 9005 |
  | Flags de dibujado | `0x0B` |

  Coincide con lo que anotó el usuario.

**Corrida única E (`analysis/local/gameplay_002/e_castle/`):**

- Exe `67fdaa7a…`, 29 parches, desde el checkpoint de VISUAL_001. Duró 48,7 min; se paró por límite de tiempo.
- **HECHO, ruta completa sin saltos:**
  - CROSS en el tick 12909;
  - 0x232;
  - oficina a ~4 VSync/s;
  - 0x231 entre los ticks 25204 y 26584;
  - castillo.
  - Ningún FATAL ni access violation.
- **HECHO, checkpoint de entrada al castillo:** `ckpt/g002_castle_entry.dmck`, tick 26640, al primer intento.
- **HECHO, sonda:**
  - Desde el tick ~28010, `0x742168 = 0`: entrada desbloqueada.
  - `Get_joystick_theta` funciona: con el stick neutro (0x80 crudo → 127), la magnitud es 1,414.
  - El jugador entra en `Md_draw_entry_db` en cada fotograma (10 de cada 10 ticks) e inserta tags OT.
  - Matriz con 16 flotantes finitos y profundidad ~9006.
  - Rutina `01 43 03 00` y posición (8897,89, −3527, 2675,52), igual que PCSX2 s09.
- **Fallos de método:**
  - El orquestador se colgó tras el primer guardado: `grep -c … || echo 0` devuelve «0\n0» y la comparación
    nunca se cumple.
  - La secuencia de entradas se lanzó a mano.
  - El segundo guardado, el de «primer control» (tick 30064), no encontró ningún punto seguro hasta el final de
    la corrida. Alterna `GS local->host buffer not fully consumed`, `activeInv != 0` y
    `completedDmacCauses not empty`.
    - **INFERENCIA:** en el castillo existe una lectura GS→host que mantiene `localToHostPendingBytes ≠ 0` en
      casi todas las fronteras. Causa UNKNOWN; no se ha investigado.
  - El registro de `[CKPT:DEFER]` imprime una línea cada vez que cambia el motivo. Con motivos alternos produjo
    unas 9400 líneas y un log de 149 MB. **Defecto del parche 29**, pendiente de corregir: registrar solo la
    primera vez y después de forma periódica.
    - **Corrección posterior (GAMEPLAY_003, sección 9):** la atribución del tamaño era incorrecta. Las ~9400
      líneas de `[CKPT:DEFER]` ocupan ~0,8 MB; el 96 % del log (1 048 318 líneas) es `[dmc-stub-instrumentation]`
      de los `ret0` (sobre todo `katVu0Clip_ld`). Ambos registros se limitaron.

**Restauración en un proceso nuevo (`e_restore/`, 5 min, sin peticiones de guardado):**

- **HECHO:** `g002_castle_entry.dmck` carga y continúa. Entre los ticks 26650 y 28100, las 145 muestras de la
  sonda son idénticas a las de la corrida original.
- **HECHO, entradas en el castillo:**

  | Entrada | Teclas | Jugador |
  |---|---|---|
  | START | — | `0x742168` cambia (`0xFFF88080`, `0xEFF00080`) y se deja de dibujar al jugador (menú); al cerrar vuelve a 0 |
  | Stick a la derecha (lx = 0xFF) | `key_lx = 0` | magnitud 128,004 (PCSX2 en movimiento: 128,13), ángulo −1,5786, rutina `01 02 00 01` (correr); posición de 8883 a 8443 en 20 ticks |
  | Stick arriba (ly = 0x00) | `key_ly = 255` | magnitud 127,004; posición de (8374, 2706) a (8949, 3214) |
  | Cruceta izquierda | `key = 0x08000000` | no se mueve, como predice la sección 4 |

- **Limitación:** todas las capturas de esta restauración son blancas (255 en todos los píxeles); la captura de
  ventana falló. No hay prueba visual de Dante en pantalla. La evidencia de que se dibuja es la de la sonda:
  entra en el OT en cada fotograma con matriz finita.

## 14. Cierre: FIRST_PLAYABLE_FRAME (definición limitada) conseguido

**Definición usada:** primer personaje visible y controlable. No implica gameplay estable ni juego jugable.

**HECHO, confirmación visual directa del usuario** (sesión manual, `analysis/local/gameplay_002/e_view/`, restaurada
desde `g002_castle_entry.dmck`, teclado sin P311):

- Dante aparece en pantalla en el castillo.
- Camina y corre con el stick izquierdo (WASD).
- START y el mando responden.
- Las escenas y la geometría del castillo se renderizan, con errores visuales.

**HECHO, sonda** (secciones 13 y 14):

- `0x742168 = 0` desde el tick ~28020.
- El stick analógico llega a las teclas, con la inversión que predice `Pad_read`.
- `Get_joystick_theta` da magnitudes de 118 a 139 con el stick a fondo; PCSX2 da 128,13.
- Rutinas observadas: `Pl_r1_wait` (0x00), `Pl_r1_walk` (0x01) y `Pl_r1_run` (0x02).
- Desplazamientos de ~1100 unidades sobre el plano Y = −3527.
- El jugador se dibuja en cada fotograma.

**Capturas:** las capturas automáticas del castillo salieron en blanco (fallo de captura de ventana). La evidencia
visual es la observación del usuario. Las capturas locales que existen (`e_castle/shots/m231_*.png`, `d*/shots/`)
están en `analysis/local/` y no van a Git.

**Checkpoint funcional:** `analysis/local/gameplay_002/ckpt/g002_castle_entry.dmck` (tick 26640, local).

- Carga en un proceso nuevo y reproduce la ejecución original (145 de 145 muestras idénticas).
- Llega al control en ~4,5 min.
- Es el punto de partida reproducible de GAMEPLAY_003.

**Limitaciones:**

- **Rendimiento:** ~4–5 VSync/s en la oficina y el castillo.
- **Gráficos:** errores visuales en el castillo, sin diagnosticar.
- **Modo del pad:** `0x73` frente a `0x79` en PCSX2.
- **Sin checkpoint con control:** las condiciones de punto seguro casi nunca se cumplen en el castillo
  (`GS local->host buffer not fully consumed`, sección 13).
- **Defecto nuevo, crítico para jugar:** al saltar, Dante atraviesa el suelo y cae sin fin. PCSX2 no lo hace
  (lo confirma el usuario). Diagnóstico: `GAMEPLAY_003_JUMP_COLLISION_DIAGNOSIS.md`.

**FIRST_PLAYABLE_FRAME frente a gameplay estable:** el primero está conseguido. El gameplay estable no:

- no hay colisión con el escenario por rayo (aterrizaje);
- hay errores gráficos;
- el rendimiento está unas diez veces por debajo del tiempo real.
