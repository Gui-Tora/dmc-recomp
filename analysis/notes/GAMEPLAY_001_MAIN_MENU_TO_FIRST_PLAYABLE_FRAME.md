# GAMEPLAY_001 — Main Menu a primer frame jugable

Fecha: 2026-10-02. Fase abierta. No se ha demostrado la primera divergencia ORIGINAL/RECOMP ni se ha implementado un fix.

## Identidad y estado inicial

- **HECHO:** `git rev-parse HEAD = 702f1644d261ff8af116382b6e2ca74eb077f5b7`; `upstream.lock.json.patched_commit = ae378ab333501ec76b8db16069f8ac616514f040`.
- **HECHO:** ejecutable canónico: `analysis/local/symtabfirst/build/bin/Debug/dmc-recomp.exe`, SHA-256 `8e8cd6249d4c16170f7f43ea42f04e301d13f22628041c411afc42891630a742`. ELF `original/SLES_503.58`, SHA-256 `d0753a6b3b2f00802a50758a872d8cf051725aa31c839aa8894eee30ce58bab4` (entry/CRC32: ver `runtime/p311_pad_test.inc`, `0x00100008`/`0x77654AD2`).
- **HECHO:** antes de tocar código se guardaron `root_status.txt`, `vendor_status.txt` y `vendor_dirty_before_gameplay001.patch` en `analysis/local/gameplay_001/`; SHA-256 del patch `0a1c48381c566025de22aa80b0629101bd0e06eeaa2cb28a6b113c3fe698d872`. El vendor estaba sucio; no se revirtió ni limpió.
- **HECHO:** no hay cambios de runtime ni de código generado hechos en esta fase.

## Intento de observación con checkpoint

- **HECHO:** `analysis/local/gameplay_001/run_baseline.py` imprimió HEAD, `patched_commit`, build root y ruta absoluta antes de arrancar. Usó exclusivamente el ejecutable canónico, un proceso nuevo, la ISO local y `DMC_DEVSTATE_LOAD=dmc_checkpoint_mainmenu_v1.dmck`.
- **HECHO:** el proceso terminó limpiamente antes de que apareciera Main Menu o se enviara input. `baseline_180s.log` contiene `[CKPT:LOAD] FATAL: runtime build mismatch (v1 strict policy)`. Duración observada: 3,344 s; la prueba de 180 s **no comenzó**.
- **HECHO:** el build ID almacenado en el checkpoint es `29b02bb45d37bdf9aded78b4730228a7e4ac11dd5605cd4ade7b2d9ce4894ef2`; el ID calculado con el algoritmo de cuatro lanes FNV-1a del código sobre el ejecutable canónico actual es `94d8511801d1ead1b80af0b8a5aade654ea9a06bc5445c98d7300a71269d6444`. El checkpoint se guardó a las 18:43:58 y el ejecutable canónico tiene `LastWriteTime` 19:43:23 del mismo día.
- **INFERENCIA:** el relink posterior al guardado invalidó el checkpoint bajo la política estricta v1. Esta explicación concuerda con la comparación byte a byte del build ID; no implica corrupción de las 12 secciones ni un error en la lógica de restauración.
- **UNKNOWN:** si existe otro checkpoint completo compatible con el ejecutable actual. El archivo de evidencia `analysis/local/devstate_003c/main_menu_full.dmck` es idéntico al v1 operativo (SHA-256 `9955fb4e66aa6d6ee96c929c50fb34d4b19c3d2701adf0433eec636bb9e62c4d`).

## `fno=2 mode=2`: alcance demostrado hasta ahora

- **HECHO:** el log anterior de DEVSTATE_003C observó después de NEW GAME seis lecturas `mode=1` (`0x52/0x50/0x51/0x54/0x53/0xFE`), luego `[CDMODULE] unsupported fno=2 mode=2 size=0xc6800`, y luego `[RuntimeGuestArena:async-stack]`. Era una ejecución anterior, no la prueba nueva de 180 s.
- **HECHO:** `CdModuleService::handleRead` en `vendor/PS2Recomp/ps2xIOP/src/modules/cdmodule.cpp` lee resource ID, LBA, tamaño, destino y byte mode del buffer EE. Con mode distinto al soportado (`1`), registra el warning y devuelve `RpcResult{}` sin copiar datos ni escribir el conteo de transferencia de este servicio.
- **HECHO:** `SifCallRpc` en `vendor/PS2Recomp/ps2xRuntime/src/lib/Kernel/Syscalls/RPC.cpp` tiene una ruta genérica para `!handled`: puede copiar del send buffer al receive buffer, o poner ceros, y completar la llamada. Por tanto, el efecto guest exacto depende de los argumentos y del servidor registrado en *esta* invocación; el warning por sí solo no demuestra espera ausente ni retorno ficticio concreto.
- **HECHO:** la auditoría primaria de `CDMODULE.IRX` en `analysis/notes/BLOCKER_002_ASTRA_AUDIT.md` establece que `mode & 2` lee una cabecera del disco y puede seleccionar destinos EE/IOP/SPU; una copia lineal equivalente a `mode=1` sería semánticamente incorrecta.
- **UNKNOWN:** caller PC/thread, resource ID, LBA, destino, resultado final de RPC, rama posterior y semántica exacta de este `0xc6800`; aún falta comparar la operación correspondiente en PCSX2.

## Resultado provisional y siguiente paso

`PRIMARY_CLASSIFICATION = GAMEPLAY_001_INSUFFICIENT_EVIDENCE`. `ROOT_CAUSE_READY = NO`. `SEMANTIC_FIX_IMPLEMENTED = NO`. `FIRST_PLAYABLE_FRAME_REACHED = NO`.

**Siguiente acción única:** obtener un checkpoint Main Menu v1 creado por el ejecutable canónico actual mediante una sola navegación en frío, sin cambiar el formato ni desactivar la comprobación de build ID; repetir entonces la prueba NEW GAME de 180 s con instrumentación acotada.

**ACTUALIZACIÓN DEVSTATE_003D (2026-10-02):** la acción anterior quedó reemplazada por un cambio de infraestructura autorizado: formato v2 con identidad de compatibilidad de estado y build ID de ejecutable solo para procedencia. `dmc_checkpoint_mainmenu_v2.dmck` se guardó y cargó en procesos frescos, incluso tras un relink no semántico, con Main Menu e input confirmados por el usuario. Véase `DEVSTATE_003D_CHECKPOINT_COMPATIBILITY_ID.md`. La prueba GAMEPLAY_001 de 180 s y la primera divergencia ORIGINAL/RECOMP siguen pendientes.

`RETRACTADO`: ninguno en esta fase. La sospecha sobre `fno=2 mode=2` continúa explícitamente como hipótesis, sin promoción a causa raíz.

## Observación de 180 s tras DEVSTATE_003D (2026-10-02)

**HECHO:** Proceso fresco, `DMC_DEVSTATE_LOAD=dmc_checkpoint_mainmenu_v2.dmck`, ejecutable de `analysis/local/symtabfirst/build/bin/Debug/dmc-recomp.exe`, ELF `SLES_503.58`. La carga pasó; la captura `frame_005.png` muestra Main Menu con NEW GAME seleccionado. Se envió X a los 5,359 s del proceso. `frame_025.png` aún muestra el menú; `frame_045.png` y todas las capturas posteriores hasta `frame_208.png` son negras. El primer recurso de la transición (`0x52`) apareció a los 44,656 s. Se observaron **180,031 s** desde ese marcador, hasta 224,687 s de vida del proceso. Registro: `analysis/local/gameplay_001/profile_180s.json`, `profile_180s.log` y capturas PNG. El proceso propio se cerró después de la ventana.

**UNKNOWN:** El instante exacto de aceptación de NEW GAME está entre la captura de menú a ~25 s y la transición CD a 44,656 s; X a 5,359 s es solo el input enviado. La ventana de 180,031 s se cuenta desde el primer CD de transición, que es posterior a la aceptación. No se atribuye al input enviado una aceptación inmediata.

**HECHO:** Se añadió temporalmente un profiler alrededor del dispatch común `EeScheduler::run()`, activado por un archivo local al detectarse `resourceId=0x52`. Agrupó por PC de entrada y thread, con llamadas, suma de tiempo host y máximo por dispatch. Solo hubo cinco pares PC/thread en la última instantánea, a **166,483 s** de perfil (13,548 s antes del fin de la ventana). TOP 20 por total — se muestran los cinco existentes:

| orden | PC / función | hilo | calls | total ms | max ms |
|---:|---|---:|---:|---:|---:|
| 1 | `0x00180174` / `Eff_amm_set` | 7 | 927 | 164746,000 | 584,192 |
| 2 | `0x001801c8` / `Eff_amm_set` | 7 | 5 | 1691,457 | 345,429 |
| 3 | `0x0015ba10` / `Main_Get_Vsync` | 7 | 466 | 0,831 | 0,009 |
| 4 | `0x00180660` / ruta de `ann2d_ana` | 7 | 3 | 0,030 | 0,013 |
| 5 | `0x001805f0` / ruta de `ann2d_ana` | 7 | 2 | 0,005 | 0,003 |

**HECHO:** TOP 20 por máximo, mismos cinco pares: `0x00180174` (584,192 ms; total 164746,000), `0x001801c8` (345,429 ms; total 1691,457), `0x00180660` (0,013 ms; total 0,030), `0x0015ba10` (0,009 ms; total 0,831), `0x001805f0` (0,003 ms; total 0,005). `0x00180174` representa el **98,96 %** del tiempo transcurrido de esa instantánea. El `Eff_amm_set_0x180100.cpp` generado contiene un bucle que vuelve a `label_180174` desde la rama `0x1801e8`, con `eeCheckpointDue()`; no se instrumentaron instrucciones individuales.

**HECHO:** El checkpoint restaurado estaba en tick 658. La primera muestra del profiler fue tick 912, y a +15,049 s estaba en tick 954; a +166,483 s, tick 1377. Estimación de cadencia anterior a la ráfaga CD: `(44,656−2,312) s / (912−658) ≈ 167 ms/tick`; posterior: `166,483 s / (1377−912) ≈ 358 ms/tick`. La localización fina del primer tick lento no se midió. El límite observable es: seis CD mode=1 → `fno=2 mode=2` → stack async del hilo 7 → primer dispatch largo de `Eff_amm_set` en tick ~912. **INFERENCIA:** la caída sostenida de VSync se explica por tiempo host dentro de guest dispatch, principalmente `Eff_amm_set`; la proximidad temporal de `fno=2` no demuestra que lo cause.

**HECHO:** Antes del warning hubo seis lecturas CD completadas (`0x52`, `0x50`, `0x51`, `0x54`, `0x53`, `0xFE`). El request sospechoso fue `sid=0x12345678`, `fno=2`, caller `CallCdModule` `pc=0x001cecc0`, hilo 7, `resourceId=0xFF`, `LBA=0xEA547`, `size=0xC6800`, `dst=0xD3CE20`, `mode CD=2`, send `0x87DC80/112`, recv `0x87DAC0/4`, modo SIF `0` (síncrono). CDMODULE devolvió `handled=false`. RPC completó con `v0=0`, `resultPtr=0`, flags `0xC0` (= unhandled + fallback copy) y `recv[0..3]=FF 00 00 00`; no señaló completion semaphore. El código generado de `CallCdModule` reanuda en `0x1cecc0`, cuya siguiente instrucción es `beqz $s2`; el valor de `$s2` y la rama tomada no se capturaron. Después se observó `[RuntimeGuestArena:async-stack] thread=7 depth=0 block=[0xa0a640,0xa0e640) sp=0xa0e630`, y luego el perfil del mismo hilo. No se registraron nuevos wait/wake ni creación de thread después de activar el perfil. **UNKNOWN:** el efecto del fallback respecto al juego original, el entry inicial del hilo 7 y si la llamada no manejada influye en el bucle caliente.

**HECHO:** El log tiene 64 `[sceDmaSend]` antes del request mode=2 y **cero** después; no hubo otro CD tras él. Continuaron VSync y dispatches; `0x001801c8` pasó de una a cinco llamadas durante el perfil, lo que demuestra ejecución de continuaciones distintas dentro de `Eff_amm_set`, aunque no prueba avance de escena. La ventana y sus capturas siguieron negras. **UNKNOWN:** escrituras FRAME, contenido VRAM no negro, estado de cámara/personaje y si el bucle terminaría después de 180 s. No hay evidencia de render de gameplay después del request; no se abre una investigación de presentación host.

**INFERENCIA:** `PRIMARY_CLASSIFICATION = GAMEPLAY_001_CPU_HOT_LOOP`: el consumo host medido está concentrado en el bucle guest de `Eff_amm_set`, hilo 7. La clasificación describe el consumo de tiempo observado; **no** establece aún una divergencia ORIGINAL/RECOMP, un bucle infinito ni que `fno=2 mode=2` sea causal. `ROOT_CAUSE_READY = NO`, `SEMANTIC_FIX_IMPLEMENTED = NO`, `FIRST_PLAYABLE_FRAME_REACHED = NO`.

**RETRACTADO (instrumentación):** el primer intento de perfil no se activó porque el disparador comprobaba el archivo solo cada 4096 dispatches; se descartó como medición y se corrigió para la corrida completa. Los tres archivos runtime/IOP se restauraron desde copias byte a byte guardadas antes de instrumentar; los diffs diagnósticos quedan solo bajo `analysis/local/gameplay_001/`.

**Siguiente acción única:** observar en `Eff_amm_set` los contadores guest del bucle (`$s1`, `$s3`) con muestreo escaso y comparar su trayectoria con el request mode=2, antes de consultar PCSX2 o proponer un fix.

## Contadores del bucle `Eff_amm_set` (`$s1`/`$s3`) y vínculo con `fno=2 mode=2` (2026-10-02)

**HECHO (estructura, código generado `Eff_amm_set_0x180100.cpp`):** `$fp = lw 0($a1)` (conteo externo), `$s4 = $a1` y avanza 4 por entrada externa; por entrada: `$s5 = $a1 + lw 4($s4)`, `$s3 = lw 0($s5)` (conteo interno, `lw` sign-extiende a 64 bits), `$s1 = 0`. Bucle interno `0x180174..0x1801e8`: lee `lhu 4($a0)` (tipo de registro, 16 B por registro); tipo 2 → `jal 0x1805A0` y `$s5 = $v0`; cualquier otro tipo → `printf` (stub silencioso). `0x1801e0 addiu $s1,$s1,1`; `0x1801e4 sltu $v0,$s1,$s3` (comparación **sin signo de 64 bits**); `0x1801e8 bnez $v0 → 0x180174`. Condición exacta de salida: `$s1 >= $s3` (u64).

**HECHO (instrumentación temporal):** hook en `PS2Runtime::eeCheckpointDue` (`ps2_runtime.cpp`, sin headers) filtrando `pc=0x180174 && branch_pc=0x1801E8` (back-edge interno) y `pc=0x180150 && branch_pc=0x180568` (externo). Muestreo: primeras 20 iteraciones, cada 100 acumuladas (tope 4000 líneas), y reinicios de `$s1`. Corrida: proceso fresco, `DMC_DEVSTATE_LOAD=dmc_checkpoint_mainmenu_v2.dmck`, X a 5,344 s (el usuario dio el foco manualmente), `resourceId=0x52` y `fno=2 mode=2` a 86,360 s, ventana de 90 s. Log: `analysis/local/gameplay_001/s1s3/run_s1s3.log`; diff: `g1s1s3_instrumentation.diff`.

**HECHO (antes del request mode=2):** tick 1122, hilo 7, `Eff_amm_set` con `$a1=0xBA8260` (= `dest` de `resourceId=0x50`, leído `mode=1`), `$fp=8`, conteos internos 1..7, `$s1` progresa y sale normalmente (`$s1==$s3` en cada back-edge externo). Después, otra llamada con `$a1=0xBBCA30` (= `dest` de `0x53`), `$fp=1`, `$s3=2`, normal.

**HECHO (después del request mode=2):** primera muestra tras `unsupported fno=2 mode=2` y la traza RPC (`sendBytes` = resId `0xFF`, LBA `0xEA547`, size `0xC6800`, dst `0xD3CE20`): `$a1=$s7=$s4=0xD3CE20`, `lw 0(0xD3CE20) = 0x80000000` → `$fp=0x80000000`; `lw 4(0xD3CE20) = 0` → `$s5 = 0xD3CE20`; `$s3 = 0xFFFFFFFF80000000`. `$s1` progresa monotónicamente +1 por iteración (iter 100 → `0x4F`, … iter 398400 → `0x6142B`), sin reinicios; `$v0=1` y rama tomada en todas las muestras. `$s5`/`$a0` avanzan 0x10 por iteración (`0xD3D320` → `0x13510E0`, ya fuera del buffer de `0xC6800` bytes que termina en `0xE03620`); todos los registros leídos tienen tipo 0 → ruta `printf`. Muestras: ticks 1122–1124 (tope de líneas alcanzado a ~398k iteraciones; no es el final del bucle).

**INFERENCIA:** con `$s3 = 0xFFFFFFFF80000000`, la salida exige ~1,8·10¹⁹ iteraciones: bucle efectivamente no terminante (no oscila ni se estanca; progresa hacia un límite inalcanzable). La condición depende de memoria guest: palabra en **`0xD3CE20` = `0x80000000`** (y `0xD3CE24 = 0`). Ese buffer es exactamente el `dst` del request `fno=2 mode=2` que CDMODULE no maneja (`handled=false`, sin copia a `dst`; el fallback RPC solo toca `recv=0x87DAC0/4`). Por tanto el contenido de `0xD3CE20` no proviene de la lectura esperada; el valor `0x80000000` es contenido previo del buffer (su origen no se determinó).

**HIPÓTESIS:** en el original, `mode=2` rellena `0xD3CE20` con un blob `amm` válido (conteo externo pequeño), y `Eff_amm_set` termina. No verificado contra PCSX2 (excluido en esta fase). La semántica de `mode & 2` está descrita en `BLOCKER_002_ASTRA_AUDIT.md` (lectura de cabecera y destinos EE/IOP/SPU); una copia lineal tipo `mode=1` sería incorrecta.

Instrumentación retirada: `ps2_runtime.cpp` restaurado desde copia (`sha256 4f2783bd…b990`, idéntico al previo), build canónica reconstruida (incremental, solo `ps2_runtime.cpp` + relink; exe `1DA007A2…53C1`, el hash difiere del anterior solo por el relink).

`PRIMARY_CLASSIFICATION = GAMEPLAY_001_CPU_HOT_LOOP_ON_UNFILLED_FNO2_MODE2_BUFFER`. `ROOT_CAUSE_READY = SÍ para el mecanismo del bloqueo; NO para la semántica de reparación` (falta especificar `mode=2`). `SEMANTIC_FIX_IMPLEMENTED = NO`.

**Siguiente acción única:** especificar la semántica de `CDMODULE fno=2 mode=2` desde `CDMODULE.IRX` (auditoría BLOCKER_002) y, si hace falta, confirmar con PCSX2 el contenido de `0xD3CE20` tras ese request, antes de implementar.

## GAMEPLAY_001C — Oráculo PCSX2 para `CDMODULE fno=2 mode=2` (2026-10-02)

**Método (HECHO):** PCSX2 2.8.2 (`C:\Program Files\PCSX2\pcsx2-qt.exe`, sin GDB: la vía GDB/MCP ya había fallado en fases anteriores), misma ISO, arranque en frío, navegación manual del usuario hasta NEW GAME. Observación **solo lectura** vía PINE (`127.0.0.1:28011`, opcode Read64) con `analysis/local/gameplay_001c/pine_oracle.py`: request `0x87DC80` (0x20 B), `recv` `0x87DAC0`, destino `0xD3CE20..+0x40`, punteros `0x5C79C0..+0x20`, cada ~2 ms; log `pine_oracle.log`. Sin breakpoints ni registros (sin GDB).

**HECHO (offline, ISO):** LBA `0xEA547`, tamaño `0xC6800` = `/DATA/PLD/PL01.PLD;1` completo (SHA-256 `881bb014…7abe`). Sector 0 = cabecera de bloques: `{u32 tipo=0, u32 size=0x000C5F20, 0, 1}` y `0xFFFFFFFF` en `+0x20`; resto del sector a cero. Payload desde `+0x800`: `w0=7` y tabla de 7 offsets (`0x20, 0x27760, 0x58640, 0x59300, 0x721E0, 0xA30C0, 0xA46C0`). Los sub-bloques `+0x58640` y `+0xA30C0` tienen exactamente el formato recorrido por `Eff_amm_set`.

**HECHO (ORIGINAL, PINE):**
- Secuencia previa idéntica a RECOMP: `0x52, 0x50, 0x51, 0x54, 0x53, 0xFE` (`mode=1`, mismos LBA/tamaño/destino).
- Request: `ff000000 47a50e00 00680c00 20ced300 00000000 02…` → resId `0xFF`, LBA `0xEA547`, size `0xC6800`, dst `0xD3CE20`, mode `2` (idéntico a RECOMP).
- `0xD3CE20..0x3F` antes: todo `0`.
- ~0,1 s después: `0xD3CE20..0x3F` = **bytes de `PL01.PLD` desde el offset `0x800`** (comparación exacta de los 64 bytes: igual). La cabecera de 2048 B **no** se copia al EE.
- `recv[0]` pasa de `0xFFFFFFFF` (pendiente) a **`0x000C5F20`** = tamaño del bloque de la cabecera = bytes transferidos.
- ~0,24 s después, código EE reescribe punteros internos del blob (p. ej. `0x00027500 → 0x00D64340` = valor + `0xD3CE40`): fixups post-carga.
- Punteros del jugador (`s0=0x5C65B0` en `Set_player_devil`): `0x5C79CC = 0x00D95460` (= dst+`0x58640`), `0x5C79D4 = 0x00DDFEE0` (= dst+`0xA30C0`), y `0x5C79D0/0x5C79D8 = dst+0x59300/+0xA46C0`.
- Siguen otras peticiones `mode=2` (resId `0x103`, LBA `0xEAAA5`, size `0x55000`, dst `0xE72E20`, `recv=0x4D7D0`) y una `mode=0xA` (resId `0x1B9`); RECOMP nunca llegó a ellas.

**INFERENCIA (memoria original + código generado, sin registros observados):** `Set_player_devil` llama a `Eff_amm_set` con `a1 = 0xD95460` (`$fp=3`, `$s3 = 3, 4, 3` → 10 iteraciones internas) o `a1 = 0xDDFEE0` (`$fp=4`, `$s3 = 5, 3, 2, 3` → 13); la cabecera y los conteos leídos en PCSX2 coinciden con la ISO. Cada bucle interno sale con `$s1 == $s3` (`sltu` = 0).

**Comparación:**

| | ORIGINAL | RECOMP |
|---|---|---|
| dst antes | `0` (64 B) | no capturado |
| dst después | payload `PL01.PLD[0x800:]` (más fixups EE) | sin escritura de CDMODULE; `0x80000000` en el momento de `Eff_amm_set` |
| resultado RPC | `recv=0x000C5F20` (bytes transferidos) | `handled=false`, fallback `recv=FF 00 00 00`, `v0=0` |
| `a1` de `Eff_amm_set` | `0xD95460` / `0xDDFEE0` | `0xD3CE20` (offsets leídos = 0) |
| `$s3` | 3/4/3 y 5/3/2/3 | `0xFFFFFFFF80000000` |
| salida del bucle | `$s1==$s3` tras pocas iteraciones | no alcanzable |

**Primera divergencia (HECHO):** con request idéntico, CDMODULE original transfiere el payload de `PL01.PLD` según su cabecera (`tipo 0 → EE`, `size 0xC5F20`, omitiendo el sector de cabecera) a `0xD3CE20` y responde `0xC5F20`; el HLE de RECOMP no maneja `mode=2`, no escribe dst y responde con el fallback. Todo lo posterior (punteros a `dst+0`, `$s3` enorme, bucle caliente) es consecuencia.

**UNKNOWN:** origen del `0x80000000` en `0xD3CE20` en RECOMP (en el original el buffer era 0 antes); semántica para cabeceras con varios bloques/tipos IOP/SPU (aquí: un solo bloque tipo 0); `mode=0xA`.

`ROOT_CAUSE = CDMODULE fno=2 mode=2 no implementado en el HLE`; demostrado contra el original para este request. `SEMANTIC_FIX_IMPLEMENTED = NO`.

**Siguiente acción única:** implementar en `CdModuleService` `mode&2` según el IRX (leer el sector de cabecera, recorrer la lista `{tipo,size,…}` hasta `0xFFFFFFFF`, transferir cada bloque al destino del tipo, responder con el total transferido) y validarlo contra este oráculo (bytes de dst, `recv=0xC5F20`, punteros `0x5C79CC/D4`) y contra el siguiente request `mode=2` (`0x103`).

## GAMEPLAY_001D — Primera implementación de `CDMODULE fno=2 mode&2` (2026-10-02)

**HECHO (IRX):** `CDMODULE.IRX` SHA-256 `9d51f16d1ed861e508daa1389e6aaf4fe597687b3f0cd5feb920e62a479b34fa`. `CdReadProc@0x7E0`: con `mode&2` lee un sector al buffer de descriptores (`0x840–0x868`); descriptor de 0x20 B, terminador `type=0xFFFFFFFF` (`0x9F8–0xA04`); lee `size` en +4 y el sector relativo de datos en +0xC (`CDFileSeekL@0xB50–0xB58`). `trans_process_tbl@0x3AA8`: tipos 0/3 a `trans_mem_to_ee@0x690`, 1 a `trans_mem_to_iop@0x314`, 2 a `trans_mem_to_spu@0x4C4`. EE DMA redondea a 16 B y acumula `EE_TransSize` (`0x6C0–0x6EC`), que vuelve por el RPC. `mode&8` activa además selección/sonido (`0x91C–0x9F4`, `0xAF4–0xB58`).

**HECHO (cambio):** `CdModuleService::handleBlockRead` lee la cabecera desde el LBA del request, valida hasta el terminador, usa el sector relativo de cada descriptor, verifica los rangos de ISO/EE y copia el payload de los tipos EE 0/3 al destino con longitud DMA redondeada a 16 B. Escribe el total EE transferido en `recv` y devuelve `handled=true`. No hay constantes de recurso, escena, LBA, archivo ni destino en la implementación. `mode&8` y los tipos 1/2 se rechazan explícitamente: `IopHost` solo ofrece escritura guest EE y no representa la memoria IOP ni las operaciones `Tsnd*`/SPU que ejecuta el IRX. Las tablas mixtas se rechazan **antes de mutar EE**; no se fabrica éxito ni se omiten bloques. El código cambiado está solo en `vendor/PS2Recomp/ps2xIOP/src/modules/cdmodule.cpp`; backup local previo SHA-256 `f03e8966c8dfba779a694bffde4c1baa285fe413751050562b2bdf9c7e97aa5a`.

**HECHO (ISO, independiente del runtime):** `0xFF` tiene descriptor `type=0, size=0xC5F20, sector=1`, seguido de terminador en +0x20. `0x103` tiene `type=0, size=0x4D7D0, sector=1`; `type=1, size=0x380, sector=0x9C`; `type=2, size=0x6270, sector=0x9D`; terminador en +0x60. Este segundo caso necesita semántica IOP/SPU adicional; copiar solo el primer bloque y responder éxito sería incompleto.

**HECHO (corrida de validación `analysis/local/gameplay_001d/mode2_run.log`):** proceso fresco con checkpoint v2 y build canónica. Las seis lecturas previas coinciden con la ruta conocida. `0xFF`: `blocks=1`, `bytesRead=0xC5F20`, `status=ok`. Una comprobación temporal opcional releyó el destino EE completo y el payload de la ISO en chunks de 16 KiB y comparó byte por byte: `payloadVerify=byte-exact`. Por construcción de la tabla, el primer byte copiado procede del sector relativo 1 (`offset 0x800`), no de la cabecera. Después se observó la nueva lectura `resourceId=0x108 mode=1`, y luego `resourceId=0x103 mode=2`; no se repitió el hot loop patológico antes de este último request. El proceso se detuvo en `0x103`.

**HECHO (build final y lectura directa de respuesta):** tras retirar ambas verificaciones temporales, la build canónica incremental pasó. En `analysis/local/gameplay_001d/mode2_request103.log`, el servicio registró `0xFF ... bytesRead=0xC5F20 status=ok recv=0xC5F20` leyendo de vuelta el word de `recv`; luego `0x108 mode=1`; y para `0x103` `lba=0xEAAA5 size=0x55000 dest=0xE72E20 mode=0x2`, primer descriptor no soportado `type=1 blockSize=0x380 blockSector=0x9C`. El descriptor siguiente, comprobado en la cabecera ISO, es `type=2`. El ejecutable final tiene SHA-256 `38e6b964e84a3bdf1ef2b7094ed7742e851992437352a7cea8a1280e83166d0a`. El proceso se cerró al observar `0x103`.

**HECHO (corrida acotada de bucles `mode2_loops.log`):** un hook temporal en `eeCheckpointDue` registró una línea por bucle interno pequeño de `Eff_amm_set`, filtrando el back-edge generado `0x1801E8→0x180174`. Tras el `0xFF` correcto, la llamada con `base=0xD95460` tuvo `$fp=3` y `$s3=3,4,3` (total 10 iteraciones), exactamente la primera serie inferida del oracle. No apareció `$s3=0xFFFFFFFF80000000`; el juego llegó enseguida a `0x108` y `0x103`. La segunda serie original de 13 iteraciones **no se observó**, porque se detuvo en `0x103` como requiere esta fase. El hook se restauró byte a byte desde `analysis/local/gameplay_001d/ps2_runtime.before_loop_diag.cpp` (SHA-256 `4f2783bdaf23d45aae07ff1980e054306950c59116c97c297bd646789c62b990`); la verificación opcional de payload también se retiró del código final.

**INFERENCIA:** la divergencia causal de `0xFF` está corregida hasta alcanzar el siguiente request, con payload y conteos de `Eff_amm_set` acordes al original. El bloqueo siguiente es cobertura incompleta de descriptores tipo 1 (IOP) y 2 (SPU) en `0x103`, no evidencia de un problema de render. **UNKNOWN:** estado de render y frame jugable tras `0x103`; respuesta completa de `0x103` en RECOMP aún no puede igualar el oracle (`recv=0x4D7D0`) sin modelar los efectos IOP/SPU. No se avanzó por fallback para diagnosticar síntomas posteriores.

`ROOT_CAUSE_FIXED = SÍ para 0xFF; NO para la ruta mode&2 completa`. `FIRST_PLAYABLE_FRAME_REACHED = NO`. Sin commit ni push.

**Siguiente acción única:** especificar y exponer los destinos IOP/SPU y las operaciones de sonido de los descriptores 1/2 del IRX para completar `0x103`, con comparación acotada contra PCSX2 antes de seguir gameplay.

**ACTUALIZACIÓN RET0 (2026-10-04):** la auditoría independiente en `GAMEPLAY_001_RET0_AUDIT.md` observó la primera llamada a `capVu0MulMatrix2@0x1A0080` después de `0xFF` y antes de `0x108`; el `ret0` omite cuatro escrituras `SQC2`. La tarea IOP/SPU de `0x103` queda pospuesta hasta comparar esta primera llamada con el original. Se conserva la secuencia y la evidencia de `0x103` anteriores sin reescribirlas.

**ACTUALIZACIÓN RET0_FIRSTCALL (2026-10-06):** **HECHO:** `capVu0MulMatrix2@0x1A0080 = NATIVE_VALIDATED`. La primera llamada nativa produjo los mismos 64 bytes AFTER que el oracle ORIGINAL aportado por el usuario (`analysis/local/ret0_firstcall/oracle_compare.json`); el anterior `ret0` no modificaba el destino. Se retiró solo ese `ret0` de la configuración canónica y su override temporal; `RET0_COUNT = 19`. Build canónico PASS. La run automática por consola desde el checkpoint v2 llegó a `0x108` y al mismo descriptor tipo 1 no soportado de `0x103` (`analysis/local/ret0_firstcall/capvu0_permanent_run.log:230-232`). `FIRST_PLAYABLE_FRAME = NO`. `CURRENT_BLOCKER = 0x103 mixed mode2 descriptor semantics (IOP/SPU)`. La investigación de IOP/SPU puede retomarse en la siguiente fase; no se modificó CDMODULE aquí.

## GAMEPLAY_001E — oracle cerrado y compatibilidad del checkpoint (2026-10-11)

**HECHO:** El oracle ORIGINAL de `0x103` quedó consolidado en `analysis/local/gameplay_001e/ORACLE_103_CONSOLIDATED.md`, con dumps de los savestates `.03`–`.06`. Los cuatro efectos medidos son EE `0x4D7D0`, IOP `0x380`, copia secundaria EE `0xA0` y SPU `0x6270`; `recv=0x4D7D0`. Las direcciones IOP dinámicas no se incorporaron como constantes al HLE.

**HECHO:** El cambio local de CDMODULE captura `SdtAddr` desde `fno=1`, conserva buffers de sonido y modela la secuencia de descriptores 0/1/2; las tablas de slots y destinos SPU proceden del IRX identificado por hash. El nuevo estado persistente cambió la sección IOP a versión 2 y la ABI runtime a versión 2. El build canónico pasó y los selftests de checkpoint dieron 97 PASS / 0 FAIL (`analysis/local/gameplay_001e/checkpoint_selftest.log`).

**HECHO:** `dmc_checkpoint_mainmenu_v2.dmck`, guardado con ABI 1, fue rechazado antes de ejecución guest por `FATAL: checkpoint runtime ABI mismatch` (`analysis/local/gameplay_001e/v2_load_after_sound_abi.log`). El codec antiguo de CDMODULE guardaba solo el estado de película y no el `SdtAddr` obtenido antes del Main Menu. La ausencia de `SdtAddr` en el checkpoint v2 impide validar `0x103` desde ese archivo sin una reconstrucción no demostrada.

**UNKNOWN:** Efectos RECOMP de `0x103` (EE, IOP, SPU, copia secundaria y `recv`) y estado posterior; no se ha alcanzado `0x103` con la nueva build. La representación interna de registro VAB aún no demuestra equivalencia completa con el driver de sonido original. `FIRST_PLAYABLE_FRAME = NO`.

**Siguiente acción única:** crear desde cold boot un checkpoint nuevo de Main Menu con ABI 2 y validar desde él los cuatro efectos de `0x103` contra los dumps ORIGINAL, antes de afirmar que el blocker está resuelto.

## GAMEPLAY_001G — CDMODULE 0x118/mode 0xA/fno 8/SeReplace y avance (2026-10-12)

**HECHO:** Implementados desde el binario de CDMODULE.IRX/TSNDDRVM.IRX (detalle y direcciones en `analysis/local/gameplay_001g/REPORT_GAMEPLAY_001G.md`):

- DMA redondeado a 16 en `mode=1` (respuesta = bytes redondeados);
- `mode&8` (`TsndPortInit` por puerto + selección/salto de pares por slot);
- `fno=8` (= `EE_TransSize`);
- segundo servidor `sid=0x87654321` `fno=0x100` (`SeReplaceProcess`: tabla y datos desde EE).

Build incremental vía `cmake --build` (0 unidades generadas recompiladas) y checkpoint self-tests 97/0. `0x118` → `dmaSize=0x23FC0`, cola de 12 B igual a la imagen; `0x1B9` → `recv=0x470A00` = ORIGINAL.

**HECHO:** Con checkpoint ABI2, la secuencia RECOMP coincide con la ORIGINAL (PINE de 001C) hasta `0x32`, con 14/14 `recv` iguales donde hay captura ORIGINAL. **Primera diferencia observable:** tras `0x32` el ORIGINAL emite la película `0x232`; el RECOMP no la emite en 100–120 s y muestra:

- RPC no manejado a SDRDRV `sid=0x1 rpc=0x80000006` (`SndSceneBgmStart → SdrGetState`), que el fallback responde con eco del argumento;
- VU1 `reserved lower pc=0x2F0 0xFFFFFFFB`;
- llamadas masivas a `ret0` (`Get_light_str` ≥9900, `Add/SubVectorXYZ`, `_capVu0ecossin`, `katVu0Setmat4_shw`).

**UNKNOWN:** cuál de estos causa la ausencia de la película; estado visual (las capturas pasivas resultaron inválidas). `FIRST_PLAYABLE_FRAME = NO`.

**HECHO:** Modo sombra (temporal, `runtime/dmc_overrides.cpp` + `runtime/dmc_ret0_shadow_bodies.inc` derivado automáticamente): `capVu0AddVectorXYZ`/`SubVectorXYZ` nativas 400/400 bit-exactas frente a un modelo float32 independiente; el efecto `ret0` difiere del nativo en 307/400 llamadas. Sin equivalencia ORIGINAL demostrada. `RET0_COUNT = 19`.

**Siguiente acción única:** determinar qué espera el ORIGINAL entre `0x32` y el inicio de `0x232`. Opciones: oráculo PCSX2 del `recv` de `SdrGetState(0x80000006)` y del primer `fno=9`, o análisis estático del llamador de la película, antes de tocar SDRDRV o retirar `ret0`.

## ACTUALIZACIÓN — consolidación documental (2026-10-14)

Esta sección se añade sin modificar las anteriores. Las fases posteriores tienen ahora nota propia:

- GAMEPLAY_001E: [GAMEPLAY_001E_CDMODULE_MIXED_EE_IOP_SPU.md](GAMEPLAY_001E_CDMODULE_MIXED_EE_IOP_SPU.md).
  - Incluye la creación del checkpoint `dmc_checkpoint_mainmenu_abi2.dmck` por cold boot, que cumple la
    «Siguiente acción única» de la sección 001E.
  - Incluye la observación posterior de los cuatro efectos de `0x103` desde ese checkpoint.
- GAMEPLAY_001F: [GAMEPLAY_001F_RET0_BATCH_AUDIT.md](GAMEPLAY_001F_RET0_BATCH_AUDIT.md). Esta fase no
  figuraba en esta nota.
- GAMEPLAY_001G: [GAMEPLAY_001G_CDMODULE_AND_RET0_SHADOW.md](GAMEPLAY_001G_CDMODULE_AND_RET0_SHADOW.md).
- GAMEPLAY_001H: [GAMEPLAY_001H_VIF1_TTE_ROOT_CAUSE.md](GAMEPLAY_001H_VIF1_TTE_ROOT_CAUSE.md).
- Estado de los `ret0`: [GAMEPLAY_001_RET0_RESTORATION_STATUS.md](GAMEPLAY_001_RET0_RESTORATION_STATUS.md).
- Estado global e índice: [GAMEPLAY_CURRENT_STATUS.md](GAMEPLAY_CURRENT_STATUS.md), [RESEARCH_INDEX.md](RESEARCH_INDEX.md).

**RETRACTADO (sección GAMEPLAY_001G de esta nota):** «RPC no manejado a SDRDRV `sid=0x1 rpc=0x80000006` …
que el fallback responde con eco del argumento».

- En `SdrGetStateSend`, `send == recv` (`0x9E5500`).
- Con `send == recv`, el fallback de RPC no manejado **pone a cero** `recv`. El eco solo ocurre cuando
  `send != recv`.
- RECOMP devuelve `v0 = 0`. En la única captura ORIGINAL disponible, `v0 = 1`.

**Respuesta a la «Siguiente acción única» de 001G (HECHO, 001H):**

- La ausencia de `0x232` no la causa SDRDRV.
- La causa es la pérdida de los 64 bits altos de las etiquetas DMA REF en cadenas VIF1 con `CHCR.TTE = 1`.
  Eso desincroniza VIF1 y produce un DIRECT falso que bloquea el GS.
- Con la semántica TTE de upstream `75d729c`, aplicada como **experimento local no promovido**, RECOMP solicita
  y reproduce la película `0x232` en 4/4 ejecuciones.
- El «VU1 reserved `0xFFFFFFFB`» era el centinela de desbordamiento XGKICK, consecuencia de ese flujo corrupto.

`FIRST_PLAYABLE_FRAME = NO` (no demostrado). `RET0_COUNT = 19` (local).
