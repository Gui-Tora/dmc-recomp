# GAMEPLAY_001 — auditoría de las 20 funciones `ret0` (2026-10-04)

## Estado y fuentes

**HECHO:** HEAD raíz `6cc8ce8af99cfb123fa4f1d49d5d8880489e0507`; vendor HEAD `0efd17c3cdb834ac13c087dcb2bd947d9fc033b3`. `upstream.lock.json` fija `14b1e5cb39b4af7e6fc12f9a29fdc751efde49d7`, 13 parches y árbol sintético `dcd03052f7e8b43bac6dd9e598ea7237893f78c3`. Se preservaron sin tocar el cambio local de `GAMEPLAY_001_MAIN_MENU_TO_FIRST_PLAYABLE_FRAME.md`, `.claude/` y 27 archivos vendor modificados, incluido `cdmodule.cpp`. No se hizo commit/push ni se cambió la semántica IOP/SPU.

**HECHO:** ELF `original/SLES_503.58`, SHA-256 `d0753a6b3b2f00802a50758a872d8cf051725aa31c839aa8894eee30ce58bab4`, entry `0x00100008`, CRC32 `0x77654AD2`. La fuente de límites es su `.symtab`, analizada con `analysis/local/ret0_audit/audit_elf.py`; salida completa con los primeros/últimos cuatro words, referencias y opcodes en `analysis/local/ret0_audit/elf_audit.json`. `recomp/symtabfirst.toml:416-435` contiene exactamente 20 entradas `ret0@`; `runtime/dmc_overrides.cpp:53-98` registra los mismos 20 contadores y reemplazos para este ELF.

**HECHO:** El estado local de `cdmodule.cpp` sigue cubriendo descriptores EE 0/3 y rechaza tipos 1/2 antes de copiar cualquier bloque del request mixto. `mode2_run.log` registra `0xFF` con `payloadVerify=byte-exact`; `mode2_request103.log` registra `recv=0xC5F20`, luego `0x108` y el rechazo de `0x103`; `mode2_loops.log` registra 3+4+3 después de `0xFF`. **UNKNOWN:** la serie original de 13, el primer frame y el estado GIF completo tras `0x103`; esos logs no demuestran por sí solos ausencia de todo draw work antes de `0x103`.

## Las 20 funciones reales

Tamaño y fin proceden de `.symtab`; primera y última instrucción del codegen aislado, cuya salida coincide en bytes/direcciones con el ELF. `L/S/C/M` = conteos de LQC2/SQC2/COP2/MMI; `JAL/J` = transferencias estáticas desde secciones ejecutables del ELF. La última instrucción puede ser delay slot de `JR $ra`.

| Nombre | Inicio | Bytes / fin | Primera | Última | L/S/C/M | JAL/J |
|---|---|---|---|---|---|---|
| Calc_md | `0x16B8E0` | 324 / `0x16BA24` | `paddub` | `sqc2 vf23` | 24/2/40/7 | 7/0 |
| capVu0MulMatrix2 | `0x1A0080` | 116 / `0x1A00F4` | `lqc2 vf8` | `sqc2 vf15` | 8/4/16/0 | 249/0 |
| capVu0MulMatrix_r | `0x1A0180` | 100 / `0x1A01E4` | `lqc2 vf4` | `sqc2 vf11` | 8/4/12/0 | 12/0 |
| Calc_scr_light | `0x16D920` | 196 / `0x16D9E4` | `lui v1` | `sqc2 vf22` | 13/1/28/0 | 3/0 |
| katVu0ViewVolumeClip | `0x132D20` | 72 / `0x132D68` | `lqc2 vf4` | `jr ra` | 5/1/10/0 | 21/0 |
| katVu0Setmat4_shw | `0x1DA7B0` | 68 / `0x1DA7F4` | `lqc2 vf9` | `lqc2 vf24` | 16/0/0/0 | 1/0 |
| InterpNode | `0x132DD0` | 236 / `0x132EBC` | `lqc2 vf10` | `sqc2 vf19` | 10/5/34/2 | 20/0 |
| ClipCheck2 | `0x132EC0` | 48 / `0x132EF0` | `lqc2 vf10` | `jr ra` | 2/0/8/0 | 20/0 |
| katVu0RotTransPersQ | `0x132EF0` | 76 / `0x132F3C` | `lqc2 vf4` | `sw a0` | 5/1/10/0 | 7/0 |
| katVu0Clip_ld | `0x132DA0` | 28 / `0x132DBC` | `lqc2 vf9` | `vclipw`* | 1/0/5/0 | 2/0 |
| katVu0RotTransPers_shw | `0x1DA8E0` | 244 / `0x1DA9D4` | `lqc2 vf4` | `sqc2 vf8` | 3/6/51/0 | 1/0 |
| sceVu0SetRotTransPers | `0x1D5E40` | 52 / `0x1D5E74` | `lqc2 vf8` | `sqc2 vf10` | 1/1/9/0 | 24/0 |
| katVu0SetClip | `0x132D70` | 36 / `0x132D94` | `lqc2 vf1` | `lqc2 vf8` | 8/0/0/0 | 1/0 |
| capCopyMatrix | `0x1A01F0` | 36 / `0x1A0214` | `lqc2 vf4` | `sqc2 vf7` | 4/4/0/0 | 11/0 |
| capVu0ScaleVectorXYZ2 | `0x19FC40` | 32 / `0x19FC60` | `lqc2 vf4` | `sqc2 vf6` | 2/1/2/0 | 13/0 |
| _capVu0ecossin | `0x1A03C0` | 116 / `0x1A0434` | `lui v1` | `jr ra` | 1/0/22/0 | 9/0 |
| capVu0AddVectorXYZ | `0x19FBB0` | 20 / `0x19FBC4` | `lqc2 vf4` | `sqc2 vf4` | 2/1/1/0 | 523/0 |
| capVu0SubVectorXYZ | `0x19FBD0` | 20 / `0x19FBE4` | `lqc2 vf4` | `sqc2 vf4` | 2/1/1/0 | 592/0 |
| Get_light_str | `0x16D8E0` | 60 / `0x16D91C` | `lqc2 vf1` | `jr ra` | 2/0/10/0 | 21/0 |
| capVu0Compare | `0x1A0300` | 52 / `0x1A0334` | `lqc2 vf1` | `xori v0,1` | 2/0/7/0 | 7/0 |

**HECHO:** Ninguno de estos 20 rangos se solapa. La búsqueda sobre secciones ejecutables encontró 1544 `JAL` directos a sus entradas y cero `J` directos. No halló construcciones adyacentes `LUI`+`ORI/ADDIU` hacia estas entradas; esto no excluye construcciones separadas o callbacks dinámicos. Las apariciones de words con las direcciones en secciones no ejecutables están en `.symtab`/`.mwcats`, y dos además en `.relmain`, que es metadata de relocalización. Las listas de PCs por función están en `elf_audit.json`.

**UNKNOWN:** los posibles destinos de cada `JALR` no se pueden enumerar solo mediante el escaneo de targets inmediatos. El codegen los envía a `dispatchGuestBranch`, por lo que un `JALR` cuyo target sea una de las 20 entradas pasaría por la tabla reemplazada; la búsqueda de constantes adyacentes y datos no agota el análisis de flujo de valores.

**RETRACTADO:** `HUGE_GENERATED_FILES.md` y el comentario de `runtime/dmc_overrides.cpp` afirmaban cero caller `JAL` y solo referencias `.mwcats`. Era una limitación/error del escaneo histórico: el ELF real contiene 1544 words `JAL` a estas entradas; `Md_jnt_info_set@0x252F40` aparece también como `JAL 0x1A0080` en el C++ generado. Los tamaños gigantes por decodificación Ghidra R6 sí eran un problema histórico real, pero la `.symtab` resuelve los 20 límites.

## Reachability dinámica antes de `0x103`

**HECHO:** `analysis/local/gameplay_001d/mode2_request103.log:78` confirma que el ejecutable cargó el override; el SHA-256 de ese ejecutable y del canónico actual es `38e6b964e84a3bdf1ef2b7094ed7742e851992437352a7cea8a1280e83166d0a`. El literal de instrumentación está presente en el ejecutable. Se reutilizó el log completo, sin repetir la ejecución.

**HECHO:** secuencia `0xFF` transferido (`0xC5F20`) en línea 226 → primer `dmcStub_capVu0MulMatrix2@0x1A0080` en línea 227 → llamada #50 en línea 228 → `0x108` en línea 229 → rechazo del primer descriptor tipo 1 de `0x103` en línea 230. El primer hit: `a0=0xE2D7F0`, `a1=0xE2D660`, `a2=0xE2D7F0`, `a3=0x80`, `ra=0x252F48`. `ra` identifica `Md_jnt_info_set@0x252E40`, `JAL` en `0x252F40` (`analysis/local/symtabfirst/generated/Md_jnt_info_set_0x252e40.cpp:268-278`). El contador imprime la primera llamada y cada 50; al menos 50 llamadas ocurrieron antes de `0x108`; el total exacto está entre 50 y 99 si no hubo pérdida de log. No hubo hits registrados de los otros 19 antes de `0x103` en ese log. `mode2_loops.log` registra `Eff_amm_set` con `thread=7`, serie 3+4+3, inmediatamente antes de los hits, pero el logger del `ret0` no captura el hilo: **INFERENCIA:** el primer hit probablemente pertenece al hilo 7; **UNKNOWN:** ID probado en el instante exacto.

**HECHO:** `capVu0MulMatrix2` del ELF carga ocho vectores, ejecuta 16 opcodes COP2 de cálculo matricial y termina con cuatro `SQC2` a `a0+0,+0x10,+0x20,+0x30`; el cuarto es delay slot de `JR $ra`. El override actual llama `ps2_stubs::ret0`, que solo escribe `v0=0` y vuelve a `ra`: omite esas escrituras de 64 bytes y cambios de estado VU0. En el primer hit `a0==a2`, así que la salida se superpone a un operando de entrada. **INFERENCIA:** existe una divergencia de implementación anterior a `0x103`. **UNKNOWN:** valores originales concretos de la primera matriz, su estado previo, efecto posterior, si la traducción generada reproduce PCSX2 y si esta es la primera divergencia de ejecución ORIGINAL/RECOMP.

## Despacho, soporte y límites

**HECHO:** `runtime.registerFunction` reemplaza la entrada en la tabla densa; `dispatchGuestBranch` usa esa tabla para `JAL/JALR` y el scheduler para entradas por PC. `control_flow_emitter.cpp` puede emitir una llamada C++ directa para un `J` interfunción, evitando `registerFunction`; aquí el escaneo del ELF halló cero `J` a estas 20 entradas. `JR $ra` devuelve PC y el scheduler vuelve a consultar la tabla. `Md_jnt_info_set` usa la ruta instrumentada `dispatchGuestBranch`. No hay llamadas C++ directas al wrapper generado de `capVu0MulMatrix2` salvo su definición y registro; para las demás la evidencia principal es el escaneo de `J=0` y el emisor común.

**HECHO:** el commit fijado ya contiene traducción de LQC2/SQC2, COP2 macro y MMI; las rutas `r5900_decoder.cpp`, `vu_translator.cpp`, `mmi_translator.cpp`, `control_flow_emitter.cpp` no difieren entre el commit upstream fijado y el vendor actual. En el historial anterior al commit fijado están el soporte macro VU0 `4961710`, implementación de biblioteca VU0 `6970f3a` y correcciones MMI `d87bff4`/`4fff583`. Esto demuestra disponibilidad de codegen, no fidelidad numérica de las operaciones.

**HECHO:** variante aislada `analysis/local/ret0_audit/native_variant.toml` quitó solo las 20 líneas `ret0@` y escribió a `analysis/local/ret0_audit/native_generated`; no tocó `recomp/symtabfirst.toml`, código canónico ni overrides. `native_codegen.log` informa 10126 funciones, 20/20 cuerpos emitidos con finales exactos de `.symtab`, cero fallos de decode, cero instrucciones unhandled y cero fallbacks críticos. `check_variant.py` validó los 20 archivos, sus límites y ausencia de `ret0`/marcadores de unhandled. Los 20 archivos son pequeños (aprox. 1.9–21 KB). *`katVu0Clip_ld` muestra `.word` solo en el comentario Rabbitizer de `vclipw`; el C++ siguiente sí emite la actualización de `vu0_clip_flags`. No se compiló ni ejecutó esta variante; semántica nativa aún no validada.*

**INFERENCIA:** La justificación de mantener `ret0` para evitar funciones gigantes ha caducado; se preservaron como control experimental durante la migración symtab-first. Ninguno requiere `ret0` por un fallo de decode/codegen demostrado. **UNKNOWN:** fidelidad de la traducción para cada función y cualquier construcción indirecta de dirección no detectada por el escaneo adyacente. `NATIVE_CODEGEN_AUDIT = PASS` para generación, no para ejecución.

## Decisión

`RET0_PRE_103 = INVOLVED`. `CURRENT_FIRST_DIVERGENCE = UNKNOWN` en comparación ORIGINAL/RECOMP; el primer `ret0` alcanzado y candidato prioritario es `capVu0MulMatrix2@0x1A0080`, después de `0xFF` y antes de `0x108`, con cuatro escrituras `SQC2` omitidas. `0x103_STILL_NEXT_BLOCKER = NO` como prioridad investigativa; sigue siendo un blocker IOP/SPU observado, pero es posterior a esta llamada.

**Siguiente acción única:** comparar en PCSX2 y RECOMP la primera llamada `Md_jnt_info_set@0x252F40 → capVu0MulMatrix2@0x1A0080` con los mismos argumentos y los 64 bytes de `a0` antes/después, y validar el cuerpo generado aislado antes de sustituir ese único `ret0`. No implementar aún los descriptores IOP/SPU de `0x103`.

## RET0_FIRSTCALL cerrado — capVu0MulMatrix2 nativa (2026-10-06)

**HECHO:** La primera llamada ORIGINAL/RECOMP coincide en `PC=0x001A0080`, `a0=0x00E2D7F0`, `a1=0x00E2D660`, `a2=0x00E2D7F0`, `a3=0x80`, `ra=0x00252F48` y los 64 bytes BEFORE. El `ret0` dejaba AFTER igual a BEFORE, mientras ORIGINAL modifica la matriz. El usuario aportó los 64 bytes AFTER del oracle; `analysis/local/ret0_firstcall/oracle_compare.json` compara esa transcripción binaria con `capvu0_first_native.log`: `native_before_equals_original_before=true`, `native_after_equals_oracle=true`, SHA-256 común de AFTER `ddbdd3bba9487f3c0337ee43ac352ac1b0cb206430e0a9d8ef8e75c49db49a47`. La cuarta fila AFTER es `390B3836008009C4000040400000803F`. No se volvió a ejecutar PCSX2 en esta promoción.

**HECHO:** `recomp/symtabfirst.toml` y la configuración local de generación conservan exactamente 19 `ret0@`; se quitó solo `ret0@0x001A0080`. PS2Recomp regeneró 10 129 archivos en `analysis/local/ret0_firstcall/generated_19` (`regenerate_19.log`). La comparación `regen_diff.json` encontró 13 diferencias contra el árbol activo: el cuerpo objetivo, una declaración de stub redundante en `ps2_recompiled_stubs.h` y once C++ con diagnósticos locales anteriores. Se sincronizó solo `capVu0MulMatrix2_0x1a0080.cpp` al árbol generado canónico; coincide byte a byte con el archivo recién generado, contiene `LQC2`/VU0/`SQC2` y no llama a `ret0`. No se editó C++ generado a mano ni se sobrescribieron los otros diagnósticos locales. Se dejó la declaración redundante del header compartido para evitar recompilar miles de unidades; no registra ni ejecuta un stub.

**HECHO:** Se retiraron el wrapper y los marcadores `CAPVU0_FIRST_CALL`/`CAPVU0_NATIVE_FIRST_CALL` de `runtime/dmc_overrides.cpp`, incluida la sustitución con `runtime.registerFunction(0x001A0080u, ...)`. Quedan 19 overrides instrumentados. `register_functions.cpp` sigue asociando la entrada `0x001A0080` al cuerpo recompilado. Build incremental canónico PASS (solo `capVu0MulMatrix2_0x1a0080.cpp` y `dmc_overrides.cpp` recompilados), EXE SHA-256 `edbba5d343094a5797f8ae89b79f9382d5edac38bd282f799fc4b95a55121cb6`.

**HECHO:** Run fresca por consola con checkpoint v2 y canal PAD por archivo existente, sin GUI automation: `analysis/local/ret0_firstcall/run_permanent_console.py`, `capvu0_permanent_run.log`, `capvu0_permanent_run.json`. Checkpoint LOAD success (línea 83), Cross por `DMC_P311_PAD_FILE` (líneas 214/220), `0xFF recv=0xC5F20` (línea 230), `0x108` (231), descriptor tipo 1 no soportado de `0x103 mode=2` (232). El harness cerró solo su proceso al observar el blocker; no hubo regresión anterior registrada. No se observó primer frame jugable.

`capVu0MulMatrix2 = NATIVE_VALIDATED`; `RET0_DEFERRED_DEBT_STATUS = ACTIVE`; `RET0_COUNT = 19`; `FIRST_PLAYABLE_FRAME = NO`; `CURRENT_BLOCKER = 0x103 mixed mode2 descriptor semantics (IOP/SPU)`. Los otros 19 cuerpos se investigarán solo cuando se demuestre que son alcanzados y relevantes. Sin commit ni push.

## ACTUALIZACIÓN — seguimiento posterior (2026-10-14)

Esta sección se añade sin modificar las anteriores.

- La clasificación de los 19 restantes (Grupo A=9, B=8, C=2, tras la corrección del inventario de opcodes en
  001G) está en [GAMEPLAY_001_RET0_RESTORATION_STATUS.md](GAMEPLAY_001_RET0_RESTORATION_STATUS.md).
- Ese documento también recoge:
  - los defectos del traductor VU0 (CLIP, MAC, escritura a `vf0`, DIV, SQRT, FTOI);
  - el defecto de runtime `vf0.w = 0` en contextos de hilo hallado en 001H.
- **HECHO (2026-10-14):** la restauración de `capVu0MulMatrix2` descrita arriba existe solo en el árbol de
  trabajo local (TOML con 19 `ret0@` y overrides con 19 registros, sin commit). En `origin/master` siguen
  20 `ret0`.
