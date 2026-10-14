# GAMEPLAY_001H — Causa raíz VIF1 TTE, VU1/GS, Add/Sub y `_capVu0ecossin`

Fecha de la fase: 2026-10-13. Consolidación permanente: 2026-10-14.
Informe local detallado: `analysis/local/gameplay_001h/REPORT_GAMEPLAY_001H.md` (no versionado).

> **Estado de la solución:** la causa raíz está demostrada y existe una corrección validada **solo
> como experimento local** (`DMC_VIF_TTE_FIX=1`). El parche mínimo propuesto
> (`PROPOSED_VIF_TTE_ALL_TAG_IDS.patch`) **no** está en `patches/`, **no** figura en
> `upstream.lock.json` y **no** está publicado en GitHub. El primer frame jugable **no** está demostrado.

Categorías: **HECHO**, **INFERENCIA**, **HIPÓTESIS**, **UNKNOWN**, **RETRACTADO**.

## 1. Objetivo

Continuar GAMEPLAY_001G ([GAMEPLAY_001G_CDMODULE_AND_RET0_SHADOW.md](GAMEPLAY_001G_CDMODULE_AND_RET0_SHADOW.md)),
que dejó la primera diferencia observable en este punto: después del recurso `0x32`, ORIGINAL solicita la
película `0x232` y RECOMP no. Objetivos de la fase:

- restaurar con pruebas controladas los primeros `ret0` alcanzados (Add/Sub, `_capVu0ecossin`);
- localizar la primera divergencia causal que impide solicitar `0x232`;
- no mezclar en la misma prueba experimental la restauración Add/Sub con correcciones de sonido o VU1.

## 2. Estado inicial y problema

- **HECHO:** con el checkpoint ABI 2 (`dmc_checkpoint_mainmenu_abi2.dmck`, ver
  [GAMEPLAY_001E_CDMODULE_MIXED_EE_IOP_SPU.md](GAMEPLAY_001E_CDMODULE_MIXED_EE_IOP_SPU.md)) y Cross enviado por
  archivo PAD, RECOMP carga 24 recursos CDMODULE (`0x52 … 0x32`). Ninguna ejecución baseline solicita
  `0x232` (`baselineA1`, `baselineA2`, `hbA3`, `vu1A4`, `gswdA11`, `gswdA12`, `vifdiagA13`).
- **HECHO (ORIGINAL, log PINE de GAMEPLAY_001C, `analysis/local/gameplay_001c/pine_oracle.log`):** el request
  de `0x232` aparece 0,56 s después del de `0x32` (marcas `1790974635.245` → `1790974635.805`).
- **HECHO (síntomas RECOMP heredados de 001G):**
  - RPC SDRDRV no manejado `sid=0x1 rpc=0x80000006`;
  - `VU1 reserved lower pc=0x2F0 0xFFFFFFFB`;
  - miles de llamadas a `ret0`.

## 3. Evidencias utilizadas

- Bytes del ELF `original/SLES_503.58` y su `.symtab`: límites de `SndSceneBgmStart`, `_capVu0ecossin`,
  `SdrGetState*`.
- Código generado por PS2Recomp (`analysis/local/symtabfirst/generated/`) y cuerpos nativos aislados
  (`analysis/local/ret0_audit/native_generated/`).
- Instrumentación temporal del runtime: heartbeat, perfil de back-edges, estadísticas VU1, watchdog GS,
  volcado de cadenas DMA VIF1. Todo con variables de entorno; ver §8.
- Parser VIF1 independiente escrito en Python (`vif1_offline_parse.py`).
- Una captura ORIGINAL aportada por el usuario en el debugger de PCSX2: `PC=0x001A1E80`, `v0=1` y
  `EE[0x9E5500] = 01 00 00 00`.
- Precedente upstream: `ran-j/PS2Recomp` commit `75d729c` (PR #244).

## 4. Primera divergencia identificada

**HECHO.** PCSX2 y RECOMP coinciden en la secuencia de recursos hasta `0x32`. La primera divergencia
causal está en la construcción del flujo DMA de VIF1:

- El juego lanza VIF1 en modo cadena con `CHCR = 0x00000145` (STR | chain | **TTE=1**).
- Con TTE activo, el hardware transfiere al VIF los **64 bits altos de cada DMAtag** (dos VIFcodes) antes
  del payload, para **todos** los ID de etiqueta.
- El runtime local, basado en `14b1e5c` más los parches DMC, solo insertaba esos 8 bytes para los ID
  1/2/5/6/7 (cnt/next/call/ret/end), sin consultar TTE. Para REF/REFS/REFE (ID 3/4/0) los descartaba.
- En la primera cadena afectada (serial 3, primer tag `0x014EEFB0`), la segunda etiqueta (REF `qwc=2` en
  `0x014EEFC0`) lleva `hi = NOP; DIRECT 2`. RECOMP descarta esos 8 bytes, interpreta los 2 qw de datos GIF
  como VIFcodes y pierde la sincronización del flujo.

## 5. Hipótesis investigadas

| Hipótesis | Resultado |
|---|---|
| H1. El RPC `SdrGetState(0x80000006)` no manejado bloquea la película | **Descartada como causa de `0x232`** (§6) |
| H2. La instrucción reservada de VU1 `0xFFFFFFFB` es microcódigo real mal interpretado | **Descartada**: es un centinela de desbordamiento XGKICK (§7) |
| H3. `Get_light_str` (≥9900 llamadas) es un bucle patológico | **Descartada**: es la inicialización normal de la escena (§10) |
| H4. VU1 consume todo el tiempo host y por eso el EE no avanza | **Descartada como causa**: saltarse VU1 por completo (`DMC_DIAG_VU1_SKIP`) tampoco produce `0x232` |
| H5. `processVIF1Data` no conserva estado parcial entre trozos DMA | **Refutada** (§6) |
| H6. El flujo VIF1 se construye mal: se pierden datos TTE de etiquetas REF | **Demostrada** (§7) |

## 6. Hipótesis descartadas y motivo

- **H1 (sonido):**
  - **HECHO (`.symtab`):** `SndSceneBgmStart` ocupa `0x1A1DE0–0x1A2054` (628 bytes). `0x1A1E80` es el retorno
    de `jal SdrGetState` en `0x1A1E78`, con delay slot `ori a0, v0, 6`, es decir `a0 = 0x80000006`. El
    breakpoint del usuario corresponde a esa llamada.
  - **HECHO:** es un bucle de 6 iteraciones: `v0 = SdrGetState(0x80000006, s3+4)` y `sw v0, 0x742C40[s3]`.
    `SdrGetStateSend` escribe `a1` en `0x9E5500` y llama al RPC con `send = recv = 0x9E5500/16`.
    `SdrGetStateReceive` devuelve `*(u32*)0x9E5500`.
  - **HECHO (RECOMP):** el fallback de RPC no manejado con `send == recv` **pone a cero** `recv`
    (`vendor/PS2Recomp/ps2xRuntime/src/lib/Kernel/Syscalls/RPC.cpp`, rama `!handled`). RECOMP obtiene `v0 = 0`.
  - **HECHO (ORIGINAL, una sola captura):** `v0 = 1`. No se sabe a qué iteración corresponde.
  - **HECHO (experimento):** con la corrección TTE, RECOMP solicita `0x232` y reproduce la película entera
    aunque `SdrGetState` siga devolviendo 0. La diferencia de sonido **no es necesaria** para `0x232`. Queda
    abierta como divergencia de audio/BGM. No se forzó `v0 = 1`.
- **H4:** con `DMC_DIAG_VU1_SKIP` (experimento diagnóstico: omitir la ejecución VU1) siguió sin aparecer
  `0x232`. El perfil de back-edges mostró ~830 000/s antes de `0x32` y prácticamente ninguno después: el
  hilo EE estaba bloqueado en código host.
- **H5:** la cadena llega al intérprete como un único buffer (`chainData`). El estado entre trozos no
  interviene; la causa está en cómo se construye ese buffer.

## 7. Causa raíz demostrada

1. **HECHO (heartbeat):** después de `0x32` el tick VSync se congela (último `GP:VSync` ≈ 2738).
   - `Game_main` está detenido en `0x134164`, justo después de `Task_sleep(1)`.
   - El hilo 1 espera el semáforo 3 en `scheduler_main 0x19F7DC`.
   - Un hilo host consume el 100 % de una CPU.
2. **HECHO (watchdog GS):** el mismo paquete PATH2 (VIF1 DIRECT) de **55 184 bytes** se procesa durante
   más de 24 s sin terminar.
   - Las cabeceras son idénticas en dos ejecuciones (`gs_packet_1782.bin`, `vifdiag13/gs_packet_1774.bin`).
   - El contenido son floats de vértice o matriz, no un GIFtag válido.
3. **HECHO:** la «instrucción reservada» `0xFFFFFFFB` en `pc=0x2F0` es el centinela que
   `ps2_vu1_core.cpp` emite cuando desborda XGKICK
   (`where=tag-too-large pc=0x2f0 startPc=0x0 top=0x1f4 source=0x1240`). El «GIFtag» contiene datos de vértice.
4. **HECHO (volcado `vifdiag13/vif1_dump_0.{bin,txt}`, cadena serial 3, CHCR `0x145`, 4096 etiquetas):**
   - Hay 2454 etiquetas REF (id=3). **2271** llevan VIFcodes reales en la mitad alta: `DIRECT n`
     (`50000004`, `50000002`), `UNPACK` (`68448005`, `6E44C007`, `65448006`, `6C0203FA`), `STCYCL`, `FLUSH`.
   - El resto son etiquetas cnt/next (990 + 652).
5. **HECHO (parser independiente `vif1_offline_parse.py` → `vif1_offline_parse_A13.txt`):**
   - Flujo tal como lo leía el runtime: **56 838 opcodes inválidos**; el primero en `0x718`. Aparece un
     DIRECT falso `0x50436265` en `0x33430`: es el paquete GS de ~55 KiB.
   - Flujo reconstruido con semántica TTE (mitad alta de cada etiqueta antes del payload):
     - **0 anomalías**: 2683 UNPACK, 381 DIRECT y 183 MSCAL;
     - todos los DIRECT quedan alineados a qword;
     - el parseo termina exactamente al final del flujo (`0xCF250`).
6. **HECHO (precedente):** `ran-j/PS2Recomp` `75d729c` (PR #244, 2026-09-19) cambia exactamente esta
   semántica: `transferTagData = canal VIF && CHCR.TTE`, para todo ID de etiqueta. Incluye el test «VIF1 DMA
   chain transfers REF tag high bytes when TTE is enabled». La aplicabilidad a este ELF y a esta ruta se
   demuestra con los volcados anteriores, no solo con el precedente.

**Cadena causal:** etiquetas REF con TTE pierden sus VIFcodes → desincronización VIF1 → DIRECT falso de
~55 KiB → el GS procesa el paquete basura en el hilo EE → el EE no avanza y el VSync se congela →
`Game_main` no progresa → `0x232` nunca se solicita. Los desbordes XGKICK (VU1 `0xFFFFFFFB`) y los
agotamientos de presupuesto VU1 son consecuencia de datos UNPACK/MSCAL corruptos.

## 8. Modificaciones implementadas o experimentales

| Cambio | Ubicación | Estado |
|---|---|---|
| Experimento `DMC_VIF_TTE_FIX=1`: mitad alta de la etiqueta para todo ID si VIF y TTE | `vendor/.../ps2_memory.cpp` | **Experimental local**, desactivado por defecto; no versionado |
| Parche mínimo propuesto sin diagnósticos | `analysis/local/gameplay_001h/PROPOSED_VIF_TTE_ALL_TAG_IDS.patch` (SHA-256 `113bda50…010c`) | **Propuesto, no promovido** a `patches/` ni al lock |
| `DMC_VIF1_DIAG`: registro de cadena y volcado de DIRECT anómalos | `ps2_memory.cpp`, `ps2_vif1_interpreter.cpp` | Diagnóstico temporal |
| `XGKICK`/`VU1RUN`/`VU1STAT`: estadísticas VU1 (`VU1STAT` imprime siempre cada 3 s) | `vu/ps2_vu1_core.cpp` | Diagnóstico temporal |
| `DMC_HEARTBEAT`, `DMC_LOOP_PROFILE`, `DMC_GS_WATCHDOG`, `DMC_DIAG_VU1_SKIP`, contadores GS | `ps2_runtime.cpp`, `gs/ps2_gif_arbiter.cpp` | Diagnóstico temporal |
| `DMC_RET0_NATIVE`, resumen de llamadores por stub, shadow VU0 de `_capVu0ecossin` | `runtime/dmc_overrides.cpp`, `runtime/dmc_ret0_shadow_bodies.inc` | Diagnóstico temporal, sin commit |

El diff completo de la instrumentación está en `analysis/local/gameplay_001h/G001H_TEMP_INSTRUMENTATION.diff`,
con copias previas `*.orig` y `*.pre001h_*` en la misma carpeta. Por defecto el ejecutable se comporta
como el baseline (salvo la impresión periódica de `VU1STAT`).

Parche propuesto, aplicado al contrato del patchset (`ps2xRuntime/src/lib/ps2_memory.cpp`, modo cadena):

```diff
-  const bool compactVifLocalTag = (VIF0||VIF1) && (id == 1 || id == 2 || id == 5 || id == 6 || id == 7);
-  if (compactVifLocalTag) appendCompactVif1TagData(currentTagAddr, 0u);
-  if (hasPayload) { if (compactVifLocalTag) appendData(currentTagAddr + 16u, tagQwc); else appendData(dataAddr, tagQwc); }
+  const bool transferTagData = (VIF0||VIF1) && (chcr & 0x40u) != 0u;   // CHCR.TTE, todo ID
+  if (transferTagData) appendCompactVif1TagData(currentTagAddr, 0u);
+  if (hasPayload) appendData(dataAddr, tagQwc);
```

(Resumen ilustrativo; el diff exacto es el archivo `.patch`.)

## 9. Pruebas realizadas

| Etiqueta | Exe (SHA-256 abreviado) | Configuración | `0x232` |
|---|---|---|---|
| `baselineA1`, `baselineA2` | `33c0f329…` | ret0 | no |
| `nativeB1`, `nativeB2` | `33c0f329…` | `DMC_RET0_NATIVE` Add/Sub | no |
| `hbA3`, `vu1A4`, `cpuA5`, `profA6` | varios | diagnósticos | no |
| `vu1skipX1`, `profSkip7`, `loopsSkip8`, `gsSkip9`, `gswdSkip10` | varios | `DMC_DIAG_VU1_SKIP` (+ diagnósticos) | no |
| `gswdA11`, `gswdA12`, `vifdiagA13` | `ed69a233…` / previos | watchdog GS, diagnóstico VIF1 | no |
| `ttefixC14`, `ttefixC15`, `ttefixC16` | `217501e6…` | **`DMC_VIF_TTE_FIX=1`**, Add/Sub en ret0 | **sí** |
| `ecossinS17` | `860d7a05…` | TTE + shadow pasivo de ecossin | **sí** |

SHA-256 completos:

- `33c0f32907f22b4359f792dd0f1e09fc6e69eaddcb5f2698433e55c4a09ec3a4`
- `217501e67c0bc44f41ba6b00389a5e4660556c9375e0edf37a6de83bbb31ad4e`
- `860d7a055cab34a79c10c636f8ae5fc8e31cf9549481c1fc6e85f3353c0f5c78` (ejecutable final de la sesión)

Todas las ejecuciones parten de `dmc_checkpoint_mainmenu_abi2.dmck` (SHA-256 `88fe10cd…7078`), sin cold boot.

## 10. Resultados verificados

- **HECHO (TTE):** `0x232` solicitado en **4/4** ejecuciones (C14, C15, C16, S17), unos 8 s después de
  `0x32`. En el arnés Debug esa ventana incluye 5 recursos posteriores; en ORIGINAL son 0,56 s.
- **HECHO:** la película se lee completa (`MOVIE:start resourceId=0x232 lba=0x215524 totalSize=0x490C004`,
  trozos hasta `remaining=0x0`, `MOVIE:status alive=0`). El MPEG decodifica más de 1350 fotogramas.
- **HECHO:** 0 instrucciones VU1 «reservadas» y 0 desbordes XGKICK. `budgetHit` de VU1 baja de 245/485
  ejecuciones (`vu1A4`) a 0/36 753 (`ttefixC16`).
- **HECHO (regresión CDMODULE):** las 9 ejecuciones comparadas tienen idéntica secuencia de 24 recursos hasta
  `0x32`, con los mismos `mode`, `bytesRead` y `recv`, 0 avisos `ps2xIOP:warning` y los mismos tres RPC no
  manejados `(0x0,0x0)`, `(0x1,0x2)`, `(0x1,0x80000006)` (`compare_runs_001h_all.json`).
- **HECHO:** selftests del checkpoint sobre el exe `860d7a05…`: 97 PASS / 0 FAIL.
- **HECHO (después de la película, `ttefixC16`, ventana de 320 s):**
  - `MPEG:delete` y `AUDIO:pause`;
  - inicialización de escena: `Get_light_str` llega a 20 000 llamadas desde 13 llamadores;
  - bucle por frame del hilo 12: ~5500 `katVu0Clip_ld` y ~700 `katVu0SetClip` por tick; el tick avanza
    de 5937 a 5981 al final de la ventana;
  - no hay más `fno=2`.
- **HECHO (Add/Sub, A/B previo a TTE):** ret0 frente a `DMC_RET0_NATIVE` no muestra diferencias en ningún
  hito observable hasta el bloqueo de `0x32`. Shadow 001G: 400/400 EXACT.
- **HECHO (`_capVu0ecossin`, 600 casos shadow reales, hilo 12):** `vf7.w == −s²` en 600/600, cuando debería
  ser `1 − s²`. Por tanto **`vf0.w = 0` en ese contexto de hilo RECOMP**, mientras que en hardware `vf0` es
  la constante `(0,0,0,1)`. Q nativo = 0 en 600/600, así que el cuerpo nativo devolvería coseno 0. Ver
  [GAMEPLAY_001_RET0_RESTORATION_STATUS.md](GAMEPLAY_001_RET0_RESTORATION_STATUS.md).
- **HECHO (código):** `EeScheduler.cpp` (`StartThread`) inicializa el contexto con `R5900Context{}`, cuyo
  constructor hace `memset 0` y no fija `vf0`. `vf0` solo se fija en el contexto principal y en el reset
  VU0 (`ps2_runtime.cpp`). Upstream `origin/HEAD` tiene el mismo código.

## 11. Limitaciones y UNKNOWN

- **UNKNOWN:** el primer frame jugable **no está demostrado**. No hay volcado de framebuffer y las capturas
  pasivas de ventana de 001G resultaron inválidas. Tampoco se probó control de pad en la escena.
- **UNKNOWN:** impacto de `kMaxChainTags = 4096`. La cadena volcada tenía más de 4096 etiquetas
  (`hitMax=1`) y el resto se trunca; upstream conserva el mismo límite.
- **UNKNOWN:** si `vf0.w = 0` procede del contexto creado por `StartThread` o del contexto restaurado del
  checkpoint ABI 2.
- **UNKNOWN:** el lector de la tabla `0x742C40`, el efecto audible de `SdrGetState = 0` frente a 1 y la
  iteración (`a1`) de la captura ORIGINAL.
- **UNKNOWN:** redondeo y clamping del VU (hacia cero, sin Inf/NaN) para entradas extremas en Add/Sub y ecossin.
- **UNKNOWN:** observación directa en PCSX2 de TTE en estas cadenas. Lo implica el `CHCR` que escribe el
  propio juego, pero no se capturó.
- **INFERENCIA:** el ritmo posterior a la película es muy bajo (~2 ticks/s al final de la ventana) en build Debug.

## 12. Estado actual

| Elemento | Estado |
|---|---|
| Causa raíz VIF1 TTE | **Demostrada** |
| Corrección TTE | **Validada localmente como experimento** (4/4). Pendiente de promoción a `patches/` + `upstream.lock.json` |
| Add/Sub | Shadow EXACT y A/B sin diferencias antes de `0x32`; **ret0 se mantiene** (no permanente) |
| `_capVu0ecossin` | ret0 se mantiene; bloqueada por `vf0.w = 0` |
| Divergencia SdrGetState | Abierta, no causal para `0x232` |
| Primer frame jugable | **No demostrado** |

## 13. Archivos de código relacionados

- `vendor/PS2Recomp/ps2xRuntime/src/lib/ps2_memory.cpp` (construcción de cadenas DMA, `kMaxChainTags`).
- `vendor/PS2Recomp/ps2xRuntime/src/lib/ps2_vif1_interpreter.cpp` (`processVIF1Data`, DIRECT/UNPACK).
- `vendor/PS2Recomp/ps2xRuntime/src/lib/vu/ps2_vu1_core.cpp` (XGKICK, centinela `0xFFFFFFFB`).
- `vendor/PS2Recomp/ps2xRuntime/src/lib/gs/ps2_gif_arbiter.cpp` (drenaje PATH1/2/3).
- `vendor/PS2Recomp/ps2xRuntime/src/lib/Kernel/EeScheduler.cpp` (`StartThread`, contexto con `vf0` a cero).
- `vendor/PS2Recomp/ps2xRuntime/src/lib/Kernel/Syscalls/RPC.cpp` (fallback de RPC no manejado).
- `runtime/dmc_overrides.cpp`, `runtime/dmc_ret0_shadow_bodies.inc`.

## 14. Evidencia local (no versionada)

Todas las rutas están bajo `analysis/local/gameplay_001h/`:

| Evidencia | Qué demuestra | SHA-256 |
|---|---|---|
| `vifdiag13/vif1_dump_0.bin` / `.txt` | flujo VIF1 real y lista de 4096 etiquetas con mitad alta | `9d350e1f…f5d2` / `9a1dbfc4…ea5d` |
| `vif1_offline_parse.py`, `vif1_offline_parse_A13.txt` | 56 838 anomalías frente a 0 con TTE | `4bc32296…0aad3`, `9e8babc8…8e24` |
| `gs_packet_1782.bin`, `vifdiag13/gs_packet_1774.bin` | DIRECT falso de 55 184 B | `37bcc83f…7360`, `6068d653…1232` |
| `ttefixC14..16_run.{log,json}`, `ecossinS17_run.*` | `0x232` en 4/4, película completa | — |
| `baselineA1/A2`, `nativeB1/B2` `_run.*`, `compare_runs_001h_all.json` | A/B Add/Sub y regresión CDMODULE | `4fa46734…2884` (json) |
| `shadow_ecossin.jsonl`, `ecossin_ref_model.py` | `vf0.w = 0`; modelo independiente de ecossin | `55dc6670…f865`, `7d49d861…ccc1` |
| `PROPOSED_VIF_TTE_ALL_TAG_IDS.patch` | corrección mínima propuesta | `113bda508fa6d17e1686cccfaaa0743c78e88f91661620f1bced4c80901d010c` |
| `G001H_TEMP_INSTRUMENTATION.diff` | instrumentación temporal completa | — |
| `checkpoint_selftest.log` | 97/0 | — |

**Reproducción** (requiere el ELF y la ISO originales, el checkpoint ABI 2 y el build local):

1. Compilar de forma incremental con `analysis/local/gameplay_001h/build_canonical.ps1`
   (`cmake --build analysis/local/symtabfirst/build --config Debug --target ps2EntryRunner --parallel 4`).
   No usar MSBuild directo, porque provocó recompilación masiva en 001G.
2. Ejecutar `python analysis/local/gameplay_001h/run_progress.py <tag> <segundos_tras_0x1B9>` con
   `DMC_VIF_TTE_FIX=1` para el brazo corregido. Variables opcionales:
   - `DMC_VIF1_DIAG=<dir>`;
   - `DMC_GS_WATCHDOG=1` y `DMC_GS_WATCHDOG_DIR=<dir>`;
   - `DMC_RET0_SHADOW=<stub>`, `DMC_RET0_SHADOW_LOG=<jsonl>`, `DMC_RET0_SHADOW_MAX=<n>`.
3. Ejecutar `python analysis/local/gameplay_001h/vif1_offline_parse.py analysis/local/gameplay_001h/vifdiag13/vif1_dump_0`.

Sin los archivos originales, el checkpoint y el build local, los volcados no pueden regenerarse.

## 15. Relación con otras fases

- **Anterior:** GAMEPLAY_001G localizó la primera diferencia observable (`0x232` ausente) y creó el modo
  sombra Add/Sub.
- **Siguiente, GAMEPLAY_001I (propuesta):**
  1. promover el parche TTE a `patches/` + `upstream.lock.json` y retirar los diagnósticos;
  2. hacer un experimento independiente con `vf0 = (0,0,0,1)` en contextos de hilo y en la restauración del
     checkpoint;
  3. repetir el A/B Add/Sub con TTE como base común en ambos brazos.

  Ver [GAMEPLAY_CURRENT_STATUS.md](GAMEPLAY_CURRENT_STATUS.md).

## RETRACTADO / correcciones

- **RETRACTADO:** «el fallback de `SdrGetState(0x80000006)` responde con eco del argumento» (001G y la primera
  parte de 001H). Con `send == recv` el fallback pone a cero `recv`. La vista previa `sendBytes` se registra
  después del borrado y por eso salía a cero. El eco solo ocurre cuando `send != recv`.
- **Corrección:** el informe local de 001H atribuía la medición de 0,56 s al «PINE de 001G». Procede del log
  PINE de **001C**.
- **Corrección:** Ghidra acota `SndSceneBgmStart` a 16 bytes (`analysis/ghidra/export_r5900/dmc_r5900_functions.csv`).
  `.symtab` da 628 bytes. Se usa `.symtab`.
- **Corrección:** `_capVu0ecossin` tiene **9** llamadores estáticos (`capVu0*RotMatrix*`). En ejecución se
  observaron 6 return addresses. Los resúmenes previos citaban solo `UnitRotMatrixX_r`/`RotMatrixY_r`.
- **Refutada:** H5, «`processVIF1Data` no conserva estado parcial entre trozos».

## ACTUALIZACIÓN — GAMEPLAY_001I (2026-10-14)

Esta sección se añade sin modificar las anteriores.

- La corrección TTE quedó **promovida** a `patches/GAMEPLAY_001I_vif1_tte_all_tag_ids.patch`, entrada 14 de
  `upstream.lock.json` (`patched_commit` `b3cb0bd7…`), en el árbol local y **sin commit**.
- El parche incluye además la conservación de TTE en `sceDmaSend` y tests.
- Se retiraron el conmutador `DMC_VIF_TTE_FIX` y los diagnósticos de esta fase.
- El origen de `vf0.w = 0` quedó resuelto: es la creación de contexto en `StartThread` y en las invocaciones.
- Add/Sub se restauraron de forma permanente en local.
- Ver [GAMEPLAY_001I_TTE_PROMOTION_ADD_SUB_VF0.md](GAMEPLAY_001I_TTE_PROMOTION_ADD_SUB_VF0.md).

**Publicación (2026-10-14):** el parche TTE quedó como entrada **15** de `upstream.lock.json`, porque el trabajo
CDMODULE/ABI 2 entró como parche 14. `patched_commit` final: `6bea2eb0…`. Está publicado en la rama
`gameplay-001i` (commit `22088d0`).
