# GAMEPLAY_003 — Dante atraviesa el suelo al saltar: diagnóstico inicial

ELF `SLES_503.58`, SHA-256 `d0753a6b3b2f00802a50758a872d8cf051725aa31c839aa8894eee30ce58bab4`, CRC32 `77654AD2`,
entry `0x00100008`. Base: rama `gameplay-002-first-playable` con el parche 29 local, exe `67fdaa7a…`.

**Alcance:** análisis estático del ELF y del código generado, más lectura de la evidencia existente. No hay
corridas nuevas ni cambios de código.

Marcas: **HECHO**, **INFERENCIA**, **HIPÓTESIS**, **UNKNOWN**.

Herramientas locales en `analysis/local/gameplay_003/`:

- `callees.py`: llamadas directas (JAL) de una función, con nombres de la `.symtab`.
- `closure.py`: cierre transitivo de llamadas e intersección con los 15 `ret0`.
- `atari_check_4.asm`: desensamblado del test de colisión.
- `castle_entry_rdram.bin`: RDRAM extraída de `g002_castle_entry.dmck`, MEM + 0x10.

## 1. Síntoma observado

**HECHO** (`analysis/local/gameplay_002/e_view/`: probe y `g002_samples.bin`, bloque del jugador `0x5C65B0` cada 30
ticks):

| Tick | Rutina (`+0..+3`) | y (`+0x124`) | vy (`+0x12FC`) | g (`+0x12F8`) |
|---|---|---|---|---|
| 28380 | `01000100` (wait) | −3527,0 | 0 | 0 |
| 28410 | `012B0001` (`Pl_r1_escape_V`) | −3527,0 | 0 | 0 |
| 28440 | `012B0005` | −2366,6 | −15,30 | 5,10 |
| 28470 | `012B0007` | −5044,1 | −168,30 | 5,10 |
| 28500 | `012B0007` | −12311,6 | −321,30 | 5,10 |
| 28530 | `012B0007` | −24169,1 | −474,30 | 5,10 |
| 28560 | `01450300` | −39332,4 | — | — |
| 28590 | `011E0301` | −5048,0 | −179,4 | 6,90 |

Interpretación de la tabla:

- Caída libre con gravedad constante (vy −= g en cada actualización), sin aterrizaje.
- A partir de ~28560 el juego recoloca a Dante y vuelve a caer: síntoma posterior, no causa.
- La rutina de despacho del jugador está en la tabla `0x4FF500`, indexada por `pl+1`: `0x2B` =
  `Pl_r1_escape_V`, `0x1E` = `Pl_r1_jump_sword`, `0x45` = `Pl_r1_jump_handgun` (botones pulsados en el aire).

## 2. Ruta de salto y aterrizaje (ELF)

**HECHO (desensamblado y JAL directos):**

- `Pl_r0_move` (0x139700) → `jalr` a `[0x4FF500 + 4·pl[1]]`.
- **Rutinas aéreas:** `Pl_r1_escape_V` (0x1C69D0), `Pl_r1_jump` (0x13A7C0), `Pl_r1_fall` (0x13BBA0),
  `Pl_r1_jump_sword`, `Pl_r1_jump_handgun`. Todas llaman a `Em_pos_fall`, `Pl_pos_hosei_jump`, `Get_height` y
  `Motion_move_pl`.
- **Física vertical: `Em_pos_fall` (0x16A460), `a0` = trabajo del jugador.**
  - Posición en `+0x120` (y en `+0x124`), velocidad vertical en `+0x12FC`, gravedad en `+0x12F8`, altura del
    personaje en `+0x11F4` (925,0).
  - Subiendo (vy > 0): `Get_ceiling` y `Get_height_nolnk`; integra `y += vy`, `vy −= g`.
  - **Cayendo (vy ≤ 0), el aterrizaje:**
    - `h = max(Get_height(pos), Get_height(pos + (10, 0, 10)))`;
    - `y += vy; vy −= g`;
    - si `h > y`, aterriza: `y = h` y devuelve 1;
    - si no, sigue en el aire y devuelve 0.
- **Consulta de suelo: `Get_height` (0x16A710) y `Get_height_pl` (0x16AAE0, cada fotograma desde `Pl_pos_hosei`).**
  - Rayo vertical con `Scr_Atari_4(out, start, end, mask = 0x1C0)` de y+500 a y−2500; de reserva, de y+100 a
    y−20000.
  - Sin impacto devuelve −1 000 000 (`0xC9742400`).
  - El resultado se escribe en `a1`, que para el jugador es `+0x1340` (`0x5C78F0`): altura, nodo, normal y 1,0.
    `Get_height_pl` además guarda el polígono en `+0x1658`.
- **Colisión con el escenario: `Scr_Atari_4` (0x15DA40).**
  - Estado en las globales `0x5DA5D8` (máscara), `0x5DA5E0` (resultado), `0x5DA600` (t), `0x5DA610`/`20`/`30`
    (dirección, fin, inicio).
  - Raíz del árbol en `[0x7403F8] + 0x10`.
  - Cuantiza el rayo con `sceVu0FTOI0Vector`, recorre el árbol con `get_next_4` (0x15DBE0) y prueba los
    triángulos con **`atari_check_4` (0x15DCA0)**.

## 3. Funciones que podrían explicar la ausencia de colisión

**HECHO, los `ret0`:** el cierre de llamadas directas desde `Em_pos_fall`, `Get_height`, `Get_height_pl` y
`Pl_pos_hosei_jump` (64 funciones) no contiene ninguno de los 15 `ret0`.

- Desde las rutinas de salto, caminar y correr (204 funciones) solo aparece `capCopyMatrix`, a través de
  `Motion_set2`, que es animación.
- Caminar y correr comparten esa ruta y funcionan.
- **No hay evidencia** de que un `ret0` participe en la colisión.
- **Limitación:** las llamadas indirectas (`jalr`) no están incluidas en el cierre.

**HECHO, los datos de colisión están presentes y coinciden con PCSX2:**

- `[0x7403F8] = 0x0109D710` en el checkpoint de RECOMP y en PCSX2 s09.
- La RDRAM `0x1000000–0x1400000`, que contiene la raíz del árbol, es idéntica byte a byte entre
  `g002_castle_entry.dmck` y s09.

**HECHO, macros VU0 llamadas como funciones:**

- `sceVu0SubVector`, `AddVector`, `ScaleVector` y `FTOI0Vector` se ejecutan como HLE del runtime
  (`Stubs/VU.cpp`), no como código traducido.
- Son aritméticamente correctas para este uso, pero difieren del original en dos puntos:
  - **no escriben `vf4`–`vf6`;**
  - **`sceVu0ScaleVector`, si `f12 == 0`, reinterpreta `a2` como escala.**
- Ninguna de las funciones de la ruta (`Scr_Atari_4`, `get_next_4`, `Atari_lnk_4`) lee `vf` tras esas llamadas
  (0 instrucciones COP2).
- **INFERENCIA:** no explican el fallo. El caso `f12 == 0` (t = 0) es un defecto potencial aparte.

**HECHO, VU0 en línea dentro de `atari_check_4`** (`analysis/local/gameplay_003/atari_check_4.asm`):

- Es el test rayo-triángulo con COP2 en modo macro.
- `vmul.x vf8, vf17, vf18` (0x15DDB4) multiplica las distancias firmadas de los dos extremos del rayo al plano.
- Tras 4 `vnop`:
  - `cfc2 t0, $vi17` (0x15DDCC) lee los **flags MAC**;
  - `andi t0, 0x88` comprueba Sx|Zx (signo o cero del componente x);
  - `beqz → 0x15DED8` sale sin impacto si el rayo no cruza el plano.
- Hay dos lecturas más de `$vi17`, en 0x15DE24 (tras `vsubq.x`) y 0x15DE40 (tras `vsubx.w`), que acotan t a
  [0, 1].
- `atari_check_4R` (0x15E140) tiene la misma estructura (3 lecturas).

**HECHO, código generado** (`recomp/generated_active/atari_check_4_0x15dca0.cpp`):

- `vmul.x` se traduce a `PS2_VMUL` + `PS2_VBLEND` **sin actualizar `ctx->vu0_mac_flags`**.
- `cfc2 $vi17` se traduce a `SET_GPR_U32(ctx, 8, ctx->vu0_mac_flags)`.
- En **todo** `recomp/generated_active` no hay ninguna escritura de `vu0_mac_flags`. Solo hay 6 lecturas, las de
  `atari_check_4` y `atari_check_4R`.
- El traductor (`ps2xRecomp/src/lib/vu_translator.cpp`, `COP2_CFC2`, `VU0_CR_MAC`) solo emite la lectura.
- El runtime pone `vu0_mac_flags = 0` al iniciar y lo sincroniza con el estado de los microprogramas VU0
  (`ps2_runtime.cpp:248/277/307`).
- **Upstream** `main` (último cambio de `vu_translator.cpp`: `f49ca4ed`, 2026-08-05) tampoco lo actualiza.
- **Referencia de semántica:** en el VU, cada instrucción FMAC actualiza el registro de flags MAC.
  - PCSX2 `VUflags.cpp` (copia local en `analysis/local/gameplay_001l/clip_debug/ref/`): cero = `0x0001<<shift`,
    signo = `0x0010<<shift`, con shift 3 para x. Por tanto Zx = `0x08` y Sx = `0x80`, y `andi 0x88` es
    exactamente «x ≤ 0».

**INFERENCIA (fuerte):** en RECOMP el primer filtro de `atari_check_4` lee unos flags MAC que ninguna instrucción
FMAC traducida actualiza.

- Si `vu0_mac_flags & 0x88 == 0`, cada triángulo se descarta y `Scr_Atari_4` nunca encuentra impacto.
- `Get_height` y `Get_height_pl` devuelven entonces −1e6, y `Em_pos_fall` nunca aterriza.

**HIPÓTESIS H1:** el valor efectivo de `vu0_mac_flags` en el castillo hace que el filtro descarte todos los
triángulos. Encaja con la evidencia de la sección 4, pero no se ha medido el valor en tiempo de ejecución.

## 4. Primera divergencia objetiva conocida

**HECHO:** el bloque de resultado de la consulta de suelo del jugador (`+0x1340`, `0x5C78F0`) diverge antes del
salto.

| | Rutina | y | `+0x1340` altura | nodo | normal | `+0x1658` |
|---|---|---|---|---|---|---|
| PCSX2 s09 (de pie, con control) | `01430300` | −3527,0 | **−3527,0** | **0x1001** | **(0, 1, 0)** | **0x010A17E0** |
| RECOMP, ticks 27990–28410 (de pie, con control) | `0100…`/`0102…` | −3527,0 | 0,0 | 0 | (0, 0, 0) | — |
| RECOMP, ticks 28440–28620 (en el aire) | `012B…` | cayendo | 0,0 | 0 | (0, 0, 0) | — |

- En la oficina, durante el evento, el bloque está a cero tanto en PCSX2 (s07, s08) como en RECOMP: allí no se
  consulta el suelo.
- **Primera divergencia observable:** la primera consulta de suelo con control en el castillo (tick ≤ 27990 en la
  ejecución restaurada). PCSX2 encuentra el suelo a −3527 y RECOMP no encuentra nada.
- El salto (tick 28410) y la recolocación (~28560) son síntomas posteriores.
- **Por qué caminar funciona:** a ras de suelo la y no depende de la consulta. Pasa a depender de ella en
  `Em_pos_fall`.

**Lo que falta para cerrarlo con un dato de ejecución:** el valor de `vu0_mac_flags` y el resultado del filtro
en 0x15DDD4 durante una consulta de suelo, en RECOMP y en PCSX2 (sección 5).

## 5. Prueba mínima y reproducible

### RECOMP

Desde `g002_castle_entry.dmck` (tick 26640, control en el ~28020):

1. **Instrumentación temporal**, una TU (`runtime/`), solo lectura. Envolver:
   - `atari_check_4` (0x15DCA0) y `Scr_Atari_4` (0x15DA40) cuando el llamador sea `Get_height_pl` o
     `Get_height` del jugador.
   - Registrar, en las 200 primeras llamadas tras el desbloqueo: `ctx->vu0_mac_flags` a la entrada, el retorno de
     `Scr_Atari_4` (v0, puntero al polígono o 0), `0x5DA600` (t) y `start/end`.
   - Registrar en `Em_pos_fall` (0x16A460, a0 = 0x5C65B0): y, vy, g, la altura devuelta y v0 por llamada.
2. **Secuencia:** esperar a `0x742168 == 0`, 3 s quieto, X (saltar), 5 s. Usar P311 con el formato ampliado si
   se quiere reproducible.
3. **Coste:** compilación incremental de 1 TU más enlace (~5–10 min) y una ejecución de ~6 min.
4. **Resultado esperado si H1 es cierta:** `vu0_mac_flags & 0x88 == 0` en cada llamada, `Scr_Atari_4` devuelve 0
   y `Get_height` −1e6.

### PCSX2

Savestate 9, el mismo punto (Dante quieto ante la puerta, rutina `01430300`):

| Breakpoint EE | Qué leer |
|---|---|
| 0x15DDD0 (tras `cfc2 t0,$vi17` en `atari_check_4`) | `t0` (flags MAC), `vf8.x`, `a0` (nodo/triángulo) |
| 0x16AB68 (tras el primer `Scr_Atari_4` de `Get_height_pl`) | `v0` (≠ 0 si hay impacto), `[sp+0x84]` (altura), `[0x5DA600]` (t) |
| 0x16A5FC (salida de `Em_pos_fall`) | `v0` (1 = aterriza), `f20` (altura), `[0x5C66D4]` (y), `[0x5C78AC]` (vy), `[0x5C78A8]` (g) |

**Memoria por fotograma durante un salto** (X pulsado desde s09):

- `0x5C65B0..0x5C65B3` (rutina);
- `0x5C66D4` (y);
- `0x5C78AC` (vy);
- `0x5C78A8` (g);
- `0x5C78F0..0x5C790F` (bloque de suelo);
- `0x5C7C08` (polígono).

Con eso se compara, salto contra salto: el estado del jugador, la velocidad vertical, el contacto (`v0` de
`Em_pos_fall`), la altura calculada y la transición `0x2B` → aterrizaje.

**Necesidad de corrida:** no hace falta una corrida larga; todo parte del checkpoint del castillo.

### Corrección, si H1 se confirma (no implementada)

- **Dónde:** un cambio general del traductor (patch nuevo sobre `vu_translator.cpp`) para que las instrucciones
  FMAC de COP2 en modo macro actualicen `vu0_mac_flags`, al menos en las funciones que leen `$vi17`.
- **Coste:**
  - hay que regenerar;
  - solo cambiarían `atari_check_4` y `atari_check_4R` si la emisión se limita a funciones con `cfc2 $vi17`
    (38 ficheros contienen operaciones FMAC);
  - para no recompilar los ~10 000 `.cpp`, regenerar en una carpeta aparte y copiar solo los ficheros que cambian;
  - la validación tiene que demostrarlo antes de recompilar nada.

## 6. Mantenimiento: log de `[CKPT:DEFER]` (parche 29)

> **Actualización (sección 9.3):** la atribución del log de 149 MB a `[CKPT:DEFER]` quedó refutada. Se conserva
> el razonamiento original a continuación.

**HECHO:** en la corrida E, la espera del segundo guardado alternó tres motivos y registró ~9400 líneas
(log de 149 MB). El código imprime una línea cada vez que cambia el motivo.

**Corrección mínima propuesta (no aplicada):** imprimir solo la primera espera y después como máximo una línea
cada 60 VSync, con el número de intentos y el último motivo. No cambia ni la validación ni el momento del guardado.

```cpp
// CheckpointDeferState: añadir  uint64_t lastLogTick = 0;
if (!safety.ok && m_vsyncTick - d.firstTick < deferVsyncs)
{
    if (d.attempts == 1u || m_vsyncTick - d.lastLogTick >= 60u)
    {
        std::fprintf(stderr, "[CKPT:DEFER] waiting tick=%llu attempts=%llu reason=%s\n", ...);
        d.lastLogTick = m_vsyncTick;
    }
    return;
}
```

**Integración:** solo afecta a `EeScheduler.cpp`.

- Exige regenerar el `.patch` del parche 29 y su hash en `upstream.lock.json`, y volver a verificar el replay.
- La TU es `EeScheduler.cpp`, sin cabeceras, así que la compilación es incremental.
- El parche 29 sigue siendo local y sin integrar.

## 7. Verificación de la hipótesis de los flags MAC (preparación)

### Instrucciones que deben producir los flags que lee `atari_check_4`

| Lectura | Instrucción productora | Bits que usa | Semántica (PCSX2 `applyBinaryMACOp`, `VU_MAC_UPDATE`) |
|---|---|---|---|
| `cfc2 t0,$vi17` en 0x15DDCC → `andi 0x88` → `beqz` 0x15DDD4 (filtro 1) | `vmul.x vf8, vf17, vf18` en 0x15DDB4: x = d_start·d_end | Sx (`0x80`), Zx (`0x08`) | x actualizado; y, z y w borrados. Producto < 0 → `0x0080`; = 0 → `0x0008`; > 0 → `0x0000` |
| `cfc2 t0,$vi17` en 0x15DE24 | `vsubq.x vf9, vf0, Q` en 0x15DE0C: x = −Q = t | Sx | ídem |
| `cfc2 t1,$vi17` en 0x15DE40, luego `(t0\|t1) & 0x80` → `bnez` 0x15DEB4 | `vsubx.w vf0, vf0, vf9x` en 0x15DE28: w = 1 − t (destino vf0, el valor se descarta, los flags no) | solo w; x queda borrado | `t1 & 0x80 = 0` en PCSX2 |

Notas:

- `vdiv` (0x15DDB8) y `vnop` no modifican los flags MAC.
- Los flags CLIP (`vclipw` en 0x15DE7C/80 → `cfc2 $vi18` en 0x15DE98, `& 0xA82`) sí se actualizan en el código
  generado.

### Qué genera PS2Recomp

**HECHO** (`recomp/generated_active/atari_check_4_0x15dca0.cpp`, `vu_translator.cpp`):

- FMAC → `PS2_VMUL` / `PS2_VSUB` + `blend` sobre `vu0_vf`, sin ninguna escritura de `vu0_mac_flags`.
- `cfc2 $vi17` → `SET_GPR_U32(ctx, rt, ctx->vu0_mac_flags)`.

**HECHO, vías indirectas revisadas:**

- `seedVu0IdleSuccess` pone el valor a 0.
- `copyVu0StateToContext` lo copia de un microprograma VU0, pero ningún fichero generado llama a
  `executeVU0Microprogram` / `VCALLMS`.
- La restauración del checkpoint lo deja como estaba: `vu0_mac_flags = 0x00000000` en `g002_castle_entry`,
  `g002_O_office` y el checkpoint del menú de VISUAL_001 (CORE, offset 1150).
- Ningún stub HLE lo escribe.
- **Conclusión:** en DMC vale siempre 0.

### Clave de equivalencia PCSX2 ↔ RECOMP

- **Consulta:** el primer rayo de `Get_height_pl` (`jal` en 0x16AB60, ra = 0x16AB68). Va de (x, y+500, z) a
  (x, y−500, z), con máscara `0x1C0`.
- **Triángulo:**
  - Registro `0x010A17E0`: el que PCSX2 s09 guardó en `+0x1658` y en `0x5DA5E0`; `[+0x1C] = 0x1001` coincide con
    el nodo de `+0x1344`.
  - Cuádruple plano en y = −3527, normal (0, 1, 0), vértices x 8247–9738 y z 1720–3397.
  - Idéntico byte a byte en la RAM de RECOMP.
- **Modelo sombra** (`analysis/local/gameplay_003/shadow_gate.py`) con la posición de s09:
  - reproduce la altura de impacto −3527 que guardó PCSX2;
  - para el rayo de `Get_height_pl` (±500): d_start = 500, d_end = −500, producto = −250 000 → **MAC esperado
    `0x0080`**, filtro 1 PASS, t = 0,5.
- **Valores esperados en PCSX2** para `a0 = 0x010A17E0` en esa consulta:
  - `t0` en 0x15DDD0 = `0x0080`;
  - `t0` en 0x15DE28 = `0x0000`;
  - `t1` en 0x15DE44 = `0x0000`;
  - `t3` en 0x15DE9C sin bits `0xA82`;
  - después, `[0x5DA5E0] = 0x010A17E0` y `[0x5DA600] = 0.5`;
  - en el retorno 0x16AB68, `v0 = 0x010A17E0`.
- **En RECOMP:** las tres lecturas valen `0x00000000` y la bifurcación de 0x15DDD4 se toma («sin impacto»).

### Instrumentación (`runtime/g003_collision_probe.inc`, `DMC_G003_PROBE=1`)

- Una TU de solo lectura que envuelve `Scr_Atari_4`, `atari_check_4` y `Em_pos_fall`. Los cuerpos generados se
  ejecutan sin cambios.
- **Registra solo en la entrada:** las funciones generadas pueden volver al planificador a mitad de una llamada
  (`dispatchGuestBranch` → `return`) y reanudarse fuera del envoltorio.
- **Corrección metodológica:** la primera versión envolvía `Get_height_pl` y leía su retorno, y devolvía −4030.
  Era el estado tras el primer rayo, porque la función había cedido el control. Se retiraron esos envoltorios.
- **Rayos del jugador:** por `ra` de `Get_height_pl`, o por `ra` de `Get_height` con start.xz igual a la x/z del
  jugador.
- **Resultado final del rayo:** de las globales `0x5DA5E0` / `0x5DA600` en la siguiente entrada a `Scr_Atari_4`.
- **Por triángulo:** `ctx->vu0_mac_flags` antes de la llamada, el modelo sombra del filtro 1 (`expected_mac`),
  aceptado o no (cambio de `0x5DA5E0` dentro de la llamada) y `complete` (`ctx->pc == ra`). El triángulo
  `0x010A17E0` se marca `FLOOR_TRI`.
- **Por `Em_pos_fall` del jugador:** rutina, y, vy, g y el bloque `+0x1340`.
- **Límites:**
  - triángulos: 120 + uno de cada 500, más hasta 300 de `FLOOR_TRI`;
  - rayos: 60 + uno de cada 300;
  - `Em_pos_fall`: 600;
  - un resumen cada 60 ticks.
- **Compilación:** incremental, solo `dmc_overrides.cpp` más el enlace. Exe `192ef5d7…`.

**HECHO, pruebas de humo de 45 s** (`analysis/local/gameplay_003/smoke*`, cinemática del castillo, ticks
26751–26940; sin control y sin salto):

- La sonda se arma, no hay errores y el VSync avanza.
- **30 casos con el filtro 1 esperado PASS y el de RECOMP REJECT.**
  - Ejemplo: `Get_height_pl` de Dante en (8950, −3530, 2610), triángulo `0x0109D980` (flags `0x1001`),
    d_start = 41,295, d_end = −958,429, `expected_mac=0x80`, `mac_read=0x00000000`, `accepted=0`, `complete=1`.
- 0 triángulos aceptados.
- **INFERENCIA:** primera observación dinámica de la divergencia en el filtro 1. Falta la referencia de PCSX2 de
  la misma consulta para confirmar el valor real (`0x0080`).

### Ejecución corta preparada (no lanzada)

- **Guion:** `analysis/local/gameplay_003/g003_jump_test.sh`.
- **Secuencia:**
  1. Restaurar `g002_castle_entry.dmck`.
  2. Sondas G003 (desde el tick 27900) y G002 (estado cada 10 ticks).
  3. Esperar `0x742168 = 0`, unos 4,5 min.
  4. 3 s quieto, CROSS 0,3 s, 8 s más.
  5. Parar.
- **Condiciones:** sin peticiones de guardado y sin sobrescribir la salida.
- **Duración estimada:** ~5 min, con un límite duro de 6,5 min.

## 8. Corrección de los flags MAC de VU0 (parche 30) y validación

### 8.1 Causa raíz: confirmada

**HECHO, código:**

- Ni el traductor ni el código generado escriben `vu0_mac_flags` tras las FMAC de COP2 en modo macro.
- El valor efectivo en DMC es siempre 0: no hay microprogramas VU0 y los checkpoints guardan 0.
- **Corrección metodológica:** las secciones 3 y 7 citaban `recomp/generated_active/`, una copia antigua. El exe se
  compila desde `analysis/local/symtabfirst/generated` (`DMC_GENERATED_DIR`). Ese árbol sí tiene el descarte de
  `vf0` y el `vdiv` del parche 23, pero **tampoco** escribe `vu0_mac_flags`. La conclusión no cambia.

**HECHO, ejecución, antes del cambio:**

- Rayo de suelo de Dante: 0 triángulos aceptados (sección 7).
- Bloque `+0x1340` siempre a cero.

**HECHO, ejecución, después del cambio:**

- Mismo checkpoint, mismo rayo (`Get_height_pl`, ra = 0x16AB68, de (8950, −3027, 2610) a (8950, −4027, 2610)).
- Resultado: `result_hit = 0x010A17E0`, t = 0,500000; bloque `+0x1340` = h −3527, nodo `0x1001`, normal
  (0, 1, 0). Son exactamente los valores de PCSX2 s09 (sección 4) y del modelo sombra (sección 7).
- En la ventana con control, el 100 % de los rayos de `Get_height_pl` impactan (180 de 180 en `jump_mac2`).
- **INFERENCIA:** aceptar un triángulo exige pasar el filtro 1 (`andi 0x88`, 0x15DDD0) y el final
  (`(t0|t1) & 0x80`), así que en esas consultas `cfc2 $vi17` leyó `0x80` en 0x15DDCC y `0x00` en 0x15DE24/40.
  **No es una medición directa del registro.**

### 8.2 Implementación y archivos modificados

Parche `patches/GAMEPLAY_003_vu0_macro_mac_flags.patch`, número 30 en `upstream.lock.json`:

- `patched_commit 239ce23a92458d1bb573e9edc354f320601b5add`.
- El replay de baseline más 30 parches da el árbol `3edc3418…`, idéntico al vendor.
- *(Valores de la versión auditada. Tras regenerar el parche 29 en la sección 9: `ce2d053d…`, árbol `fa1b4d7a…`.)*
- Los parches 1–29 no cambian.

**Archivos:**

- `ps2xRuntime/include/ps2_vu0_mac.h` (nuevo):
  - `ps2Vu0MacFieldBits` y `ps2Vu0MacUpdate(old, result, lanes, keep)`.
  - Semántica de `VU_MAC_UPDATE`: S = signo; ±0 → Z; subnormal → Z|U; exponente 255 → O; si no, solo S.
  - Los componentes no escritos se borran; OPMULA/OPMSUB conservan w (`keep = 0x1111`).
  - Solo flags: los valores no se tocan. El *clamping* de PCSX2 en desbordamiento queda fuera.
- `ps2xRecomp/src/lib/code_generator.cpp`, `translateVUInstruction`:
  - Clasifica por código de tabla. Special1 → `vf[fd]`; Special2 → `ACC`; mismos códigos en ambas: `0x00`–`0x0F`,
    `0x18`–`0x1C`, `0x1E`, `0x20`–`0x2A`, `0x2C`, `0x2D` y `0x2E` (OPM).
  - Añade la actualización tras la escritura del destino.
  - No la añade si el código emitido no escribe ese destino.
  - Va antes del descarte de `vf0`, así que `vsubx.w $vf0` deja los flags de su resultado, como hace `RDzero` en
    PCSX2.
- `function_emitter.cpp` y `ps2_recompiler.cpp`: el `#include "ps2_vu0_mac.h"` solo en los ficheros que lo usan,
  o en la cabecera común del modo de un solo fichero.
- Tests: `ps2xTest/src/ps2_vu_tests.cpp` y `code_generator_tests.cpp`.

**Fuera del alcance:**

- El flag STATUS (`vu0_status`), que en PCSX2 se deriva de MAC. Nada en DMC lo lee (0 lecturas en el árbol del
  exe).
- MAX/MINI no tocan MAC (PCSX2 `applyMinMax`).

### 8.3 Pruebas unitarias

`ps2x_tests`: 467 tests (462 + 5 nuevos); los mismos 24 fallos preexistentes que en GAMEPLAY_002.

Los 5 tests nuevos:

1. **Oráculo** (port literal de `VU_MAC_UPDATE`, `VU_MACx_CLEAR`, `applyBinaryMACOp` y `_vuOPMULA`): 12 288
   casos. Combinan 18 valores (±0, ±1, ±máximo, normales mínimos, subnormales, ±Inf, NaN), 16 máscaras y 6
   valores MAC previos. Resultado: 0 diferencias, también con `keep` de OPM.
2. **Bits por componente:** S, Z, −0 (S|Z), subnormal (Z|U), Inf/NaN (O), componentes no escritos borrados y w
   conservado en OPM.
3. **Escenario `atari_check_4`:**
   - `vmul.x` → `0x0080`;
   - `vsubq.x` (t = 0,5) → `0x0000`;
   - `vsubx.w $vf0` (w = 0,5) → `0x0000`;
   - rechazos: mismo lado → `0x0000`; extremo en el plano → `0x0008`; t < 0 → `0x80`.
4. **Emisión con palabras reales:**
   - `vmul.x` 0x4B128A2A, `vsubq.x` 0x4B000264, `vaddz.x` 0x4B118C42, `vopmula` 0x4BD0A2FE y `vopmsub`
     0x4BD4862E emiten la actualización con el destino, la máscara y el `keep` correctos;
   - `vsubx.w $vf0` 0x4A290004 actualiza antes de restaurar `vf0`;
   - `vdiv`, `vnop`, `vclipw`, `vitof0`, `vftoi4` y `cfc2` no escriben MAC.
5. **Cobertura exacta** de los 48 primeros códigos de Special1 y Special2.

**Mutación:** cambiar `<< (3 - i)` por `<< i` hace fallar los tests; revertido.

### 8.4 Regeneración controlada

- `ps2_recomp` compilado sin y con el cambio (`analysis/local/gameplay_003/recomp_bin/`).
- Ambos generaron con `recomp/symtabfirst.toml` (`gen_base/`, `gen_mac/`) 10 129 ficheros.
- **Base frente a corregido:** exactamente 43 ficheros distintos (`compare_gen.json`).
- **Solo aditivo** (`verify_mac_only.py`): quitando las 395 sentencias MAC y el `#include` se obtiene byte a byte
  el fichero activo.
- **Árbol activo frente a base:** 20 ficheros difieren por motivos ajenos y anteriores (tabla de registro,
  `ret0`/`Clip`, MPEG/audio); ninguno coincide con los 43. No se tocaron.
- **Integración:** copiados solo los 43 (originales en `active_before_mac/`). El `.manifest.json` ya tenía 33
  entradas desfasadas por integraciones manuales anteriores y no se modificó.
- **Compilación incremental:** 43 TU más el enlace. Exe `9e96a869…`.

### 8.5 PCSX2 frente a RECOMP

**Nivel de instrucción, no medido:**

- Las dos compilaciones de PCSX2 disponibles (la versión instalada y una compilación de desarrollo local) no
  incluyen el servidor GDB/`qPcsx2`. Solo abrió PINE (puerto 28011, sin breakpoints).
- `t0`/`t1` en 0x15DDD0/0x15DE28/0x15DE44 siguen siendo **predicción** (sección 7). El PCSX2 lanzado se cerró; los
  savestates no se tocaron.

**Nivel de resultado, comparado:**

| | PCSX2 s09 | RECOMP antes | RECOMP después |
|---|---|---|---|
| Triángulo de la consulta de suelo | `0x010A17E0` | 0 | `0x010A17E0` |
| `+0x1340` altura | −3527 | 0 | −3527 |
| nodo `+0x1344` | `0x1001` | 0 | `0x1001` |
| normal | (0, 1, 0) | (0, 0, 0) | (0, 1, 0) |
| y de Dante de pie | −3527 | −3527 / −3530 | −3527 |

**Tabla de botones:**

- **HECHO:** la configuración activa (`0x503C30`, cfg 0) es idéntica en RECOMP y PCSX2. El salto es el bit lógico
  `0x4` (`Check_jump` 0x153830 lee `key & 0x4`) y lo produce **TRIÁNGULO**.
- X produce `0x1000840A` y no salta.
- **Corrección de método:** las dos primeras pruebas automáticas pulsaron X. No fue un defecto del runtime.

### 8.6 Prueba de salto

`analysis/local/gameplay_003/jump_mac4/`: exe `9e96a869…`, `g002_castle_entry.dmck`, sondas G002 y G003, sin
guardados. Control en el tick ~28010; TRIÁNGULO 1,2 s; 5 min 24 s en total.

**HECHO, trayectoria** (entrada de `Em_pos_fall`):

| Tick | Subfase | y | vy | g |
|---|---|---|---|---|
| 28062 | `03002B01`, impulso | −3527,0 | 162,0 | 8,1 |
| 28082 | punto más alto | −2351,3 | 0 | 5,1 |
| 28085 | caída | −2366,6 | −15,3 | 5,10 |
| 28103 | `07` | −3422,3 | −107,1 | — |
| 28110 (G002) | `012B0009` | **−3527,0** | — | — |
| 28130 | `01000100` (espera) | −3527,0 | — | — |

- El bloque de suelo es −3527 / `0x1001` / (0, 1, 0) durante todo el salto.
- **Primera divergencia, cerrada:** hasta el tick 28085 (−2366,6; −15,30; 5,10) la trayectoria es idéntica a la
  sesión sin corrección (`gameplay_002/e_view`, tick 28440). En la subfase `07`, sin la corrección pasaba a −5044 y
  seguía cayendo; con ella aterriza en −3527.
- **Sin errores:** 0 FATAL o access violation; matrices finitas; Dante dibujado en todos los fotogramas; los mismos
  `ret0` que antes.

**Lectura de la sonda:** en `[G003:tri]`, `mac_read`/`gate1_recomp` son el MAC a la **entrada** de
`atari_check_4`. Con la corrección, el valor que lee `cfc2` se produce dentro de la función, así que esas columnas
ya no representan el filtro. La columna válida es `accepted`.

### 8.7 Posibles regresiones y pendientes

- **Comportamiento:** solo cambia donde se leen los flags MAC, es decir, en `atari_check_4`/`atari_check_4R` (6
  lecturas). Las otras 41 funciones cambiadas solo escriben MAC.
  - **INFERENCIA:** sus resultados no cambian.
  - Sí cambia todo lo que depende de `Scr_Atari_4`/`4R` (jugador, enemigos, objetos, cámara/paredes), que antes
    nunca impactaba. Hay que vigilar comportamientos nuevos en colisiones.
- **Rendimiento:** 395 actualizaciones más y más trabajo de colisión. El control llegó a los 277–291 s, frente a
  271 s antes; es una sola medición por configuración y no se puede concluir nada.
- **No es equivalencia completa:**
  - no se midieron los MAC de PCSX2 por instrucción;
  - no se comparó un salto completo contra PCSX2 fotograma a fotograma;
  - no se probaron bordes, pendientes ni paredes (`atari_check_4R`).
- **STATUS (`vu0_status`):** no se deriva de MAC. Ningún lector en DMC.
- **`sceVu0ScaleVector` (HLE):** con `f12 == 0` reinterpreta `a2`. Defecto potencial aparte.
- **Observación, sin efecto conocido:** el bucle de `atari_check_4` no avanza `a0` entre iteraciones
  (`addiu a0, 0x30` está en el *delay slot* del `jr`). En las consultas registradas, `count` siempre vale 1.
- **Parche 29:** el log de `[CKPT:DEFER]` sigue pendiente (sección 6).

### 8.8 Siguientes pasos recomendados

1. Con un PCSX2 con servidor GDB/`qPcsx2`, medir `t0`/`t1` en 0x15DDD0/0x15DE28/0x15DE44 para
   `a0 = 0x010A17E0`.
2. Comparar un salto completo contra PCSX2 desde s09 (y, vy, rutina por fotograma).
3. Saltar contra pared, desde un borde y en pendiente (`atari_check_4R`).
4. Medir el rendimiento antes y después con el benchmark de PERF_001, varias repeticiones.
5. Mantenimiento del parche 29 (log diferido).
6. Decidir cuándo integrar los parches 29 y 30 (locales, sin commit).

## 9. Consolidación de los parches 29 y 30

### 9.1 Confirmación manual

**HECHO (prueba manual del usuario, exe `9e96a869…`, `g002_castle_entry.dmck`):** Dante aparece, responde al
mando, camina, corre, salta y aterriza sobre el suelo. El fallo de atravesar el suelo no se reproduce.

- Alcance: corrige el defecto concreto de salto y aterrizaje. **No** valida todo el sistema de colisiones
  (paredes, bordes, pendientes, enemigos y cámara siguen sin probar; sección 8.7).
- **Corrección de método:** en el primer intento el mando no respondía. La causa fue el lanzador de pruebas,
  no el parche 30: `run_g002.py` define siempre `DMC_P311_PAD_FILE`, y mientras el canal P311 está activo
  `setPadOverrideState` sustituye la entrada real (`Pad.cpp`, `g_padOverrideEnabled`) aunque el estado inyectado
  sea neutro. Con `DMC_P311_PAD_FILE` vacía no se arranca el canal y el mando y el teclado funcionan.
  `runtime/p311_pad_test.inc` lo documenta ahora en su cabecera.

### 9.2 Auditoría previa a la integración (resumen)

- 395 de 395 FMAC del árbol regenerado con destino (`vf[fd]`/`ACC`), máscara, `keep` de OPM y orden respecto a la
  restauración de `vf0` correctos; ninguna instrucción no FMAC escribe MAC.
- Los 43 ficheros integrados coinciden con `gen_mac` y, sin las sentencias MAC, con los anteriores; no se mezclan
  con las 20 diferencias históricas del árbol activo.
- 5 cuerpos `ret0` antiguos (`Calc_scr_light`, `InterpNode`, `katVu0Clip_ld`, `katVu0RotTransPersQ`,
  `katVu0ViewVolumeClip`) conservan 55 FMAC sin MAC; no se ejecutan salvo con `DMC_RET0_NATIVE`.

### 9.3 Log de 149 MB: hipótesis refutada

- **Hipótesis anterior (secciones 6 y GAMEPLAY_002 §13):** el tamaño del log de la corrida E se debía a
  `[CKPT:DEFER]` (parche 29), que alternaba tres motivos.
- **Evidencia que la apoyaba:** ~9400 líneas `[CKPT:DEFER]` en ese log.
- **Nueva evidencia:** `e_castle/log.txt` tiene 155 989 787 bytes y 1 085 216 líneas. `[CKPT:DEFER]` suma 9447
  líneas (788 352 bytes). 1 048 318 líneas son `[dmc-stub-instrumentation]` de `StubCallCounter`
  (`runtime/dmc_overrides.cpp`, una línea cada 50 llamadas), sobre todo `katVu0Clip_ld`.
- **Estado:** se corrigieron los dos registros, ambos solo de log:
  - parche 29: `[CKPT:DEFER]` imprime el primer rechazo y después como máximo una línea cada 60 VSync, con
    `skipped=` (rechazos no impresos). Validación y tiempos de guardado sin cambios;
  - `StubCallCounter`: registra las llamadas 1, 2, 4, 8…

### 9.4 Cambios

- Parche 29 regenerado (`d5f4eaeb…`; antes `a112e6b3…`): solo cambia el registro de `[CKPT:DEFER]` en
  `EeScheduler.cpp`.
- Parche 30 sin cambios (`6d079a51…`).
- `upstream.lock.json`: `patched_commit` `ce2d053db5772642e756eacce4205ac38594894d`.
- Sondas G002 y G003 retiradas del runtime (`.inc`, `#include` y llamadas); copias en
  `analysis/local/gameplay_003/consolidation/probes_retired/`. Las secciones 7 y 8 describen la versión usada.
- P311 se conserva (formato `MASK`, `MASK LX LY`, `MASK LX LY RX RY`).

### 9.5 Validación

- **Replay** (`apply_patchset` reproducido en un clon limpio del baseline `14b1e5cb`):
  - 28 parches → `e3d4250d…`, el `patched_commit` de master (parches 1–28 intactos);
  - 29 parches → `6be089cd…`;
  - 30 parches → árbol `fa1b4d7a…`, commit `ce2d053d…`; el árbol del vendor es idéntico.
- **Tests** (`ps2x_tests`, compilación incremental: solo `EeScheduler.cpp`): 467 tests, 443 pasan, los mismos 24
  fallos preexistentes.
- **Compilación del exe:** incremental, `EeScheduler.cpp` y `dmc_overrides.cpp` más el enlace (3 min 12 s).
- **Prueba corta** (`consolidation/smoke1`, 6,5 min, `g002_castle_entry.dmck`, P311 con el formato antiguo
  `FFFF`, una petición de guardado diferido en el tick 28100 hacia un fichero nuevo):
  - carga correcta; VSync hasta 28380; 0 FATAL o access violation;
  - `[CKPT:DEFER]`: 6 líneas para 252 070 intentos (ticks 28100, 28160, …, 28400, `skipped≈50 000`); la
    validación rechazó siempre (`GS local->host buffer not fully consumed`) y no se escribió ningún checkpoint;
  - log completo de 55 384 bytes; `[dmc-stub-instrumentation]` en potencias de 2.
- Sin sondas no se midió de nuevo la trayectoria del salto en esta prueba; la retirada solo quita código de
  observación (las funciones generadas no cambian).

### 9.6 Riesgos pendientes

Los de la sección 8.7, más:

- flags calculados sobre valores del host (sin *clamp* ni redondeo de la VU), sin latencia de pipeline y sin
  STATUS derivado;
- el MAC distinto de cero se guarda en los checkpoints y pasa al estado de microprogramas (DMC no usa
  microprogramas VU0);
- coste de rendimiento sin medir;
- la espera de guardado en el castillo no encuentra punto seguro (`localToHostPendingBytes ≠ 0`); causa UNKNOWN.
