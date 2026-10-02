# DEVSTATE_003D — Identidad de compatibilidad del checkpoint

Fecha: 2026-10-02. Estado: validado de extremo a extremo; promoción documentada al final.

## Problema observado

**HECHO:** El checkpoint Main Menu v1 (`dmc_checkpoint_mainmenu_v1.dmck`, SHA-256 `9955fb4e66aa6d6ee96c929c50fb34d4b19c3d2701adf0433eec636bb9e62c4d`) guardó el build ID `29b02bb45d37bdf9aded78b4730228a7e4ac11dd5605cd4ade7b2d9ce4894ef2`. Un relink posterior produjo el ID `94d8511801d1ead1b80af0b8a5aade654ea9a06bc5445c98d7300a71269d6444`; DEVSTATE v1 rechazó la carga con `runtime build mismatch (v1 strict policy)`, antes de ejecutar guest. Véase `analysis/local/gameplay_001/baseline_180s.log`.

**INFERENCIA:** El hash de ejecutable completo es demasiado sensible para servir de identidad de compatibilidad de estado serializado. El rechazo v1 fue correcto según su política; no demuestra corrupción del checkpoint.

## Cambio de contrato

**HECHO:** El formato de archivo pasa de versión 1 a 2. La cabecera v2 contiene `checkpointCompatId[32]` (gate estricto) y `executableBuildId[32]` (procedencia). Un archivo v1 se rechaza explícitamente como `unsupported format version` antes de leer una cabecera v2 completa o restaurar estado. Se conserva v1 sin modificar en la raíz y en `analysis/local/devstate_003d/old_mainmenu_v1.dmck`.

**HECHO:** `checkpointCompatId` se calcula de forma determinista a partir de `kCheckpointRuntimeAbiVersion=1`, `kFormatVersion=2`, cantidad de secciones requeridas y, por cada una de las doce, ID, versión y bit `required`. Algoritmo: cuatro lanes independientes FNV-1a-64 sobre enteros de 32 bits en orden little-endian. ID actual: `95a9a51600f57a4f3bf496298dfb9193904e0b7c9bc832a5bd830613e75d4fdc`.

**INFERENCIA:** Logging, contadores y relinks que dejan intacto ese contrato mantienen el mismo `checkpointCompatId`. Una modificación de semántica de estado que no cambie versiones de sección **requiere** incrementar manualmente `kCheckpointRuntimeAbiVersion`; ejemplos: orden de restauración, interpretación del scheduler, deadlines, estado persistente de RPC/CD/GS o nuevos campos runtime. El cálculo no puede detectar automáticamente una omisión humana de ese incremento: es una responsabilidad explícita de revisión.

**HECHO:** La comprobación de game ID, ELF, arquitectura, formato, sección requerida, versión requerida y CRC sigue siendo fatal. Una diferencia solo en `executableBuildId` imprime `[CKPT:LOAD] executable build differs, checkpoint ABI compatible` y no bloquea el parseo. La política de medio/ISO no se modificó.

## Validación hecha

**HECHO:** Build incremental exclusivo de `analysis/local/symtabfirst/build`. Se recompilaron unidades de runtime/checkpoint y `main.cpp`; no se regeneró ni editó C++ generado. El primer comando `cmake --build` falló por colisión `Path`/`PATH` del entorno de MSBuild; se utilizó MSBuild directamente sobre los proyectos ya generados en ese mismo árbol. No se hizo clean/full build.

**HECHO:** Ejecutable v2 final previo al arranque en frío: SHA-256 `1610d6af518a15855cd02d4a12aa15cc3834e1a8ed84598169fc6d56e44ee4cb`. Su `executableBuildId` registrado por runtime es `87bdcd97d993f36e265729f0c549c43718ef6d4efd00c1c2efe9c54c175d9fb3`.

**HECHO:** `DMC_CHECKPOINT_SELFTEST=1` terminó con **97 PASS / 0 FAIL** (`analysis/local/devstate_003d/selftest.log`). Los nuevos tests cubren: mismo ABI con ejecutable distinto, rechazo de compat ID distinto antes de restauración, ELF erróneo, CRC dañado, sección requerida ausente, influencia de cada versión de sección y de la versión ABI en el ID, y preservación legible del ID exacto de ejecutable.

**HECHO:** La carga real del antiguo v1 con el ejecutable v2 registró `[CKPT:LOAD] FATAL: unsupported format version` (`analysis/local/devstate_003d/old_checkpoint_rejection.log`); no hubo ejecución guest.

## Validación interactiva

**HECHO:** El primer intento de iniciar el arranque en frío quedó en la sesión invisible de herramientas. El usuario señaló que no veía la ventana; ese proceso se cerró sin haber guardado checkpoint. Se relanzó en el escritorio interactivo, confirmado visible por el usuario. No se atribuye ninguna observación visual al proceso invisible.

**HECHO:** En el proceso frío visible, el usuario llegó manualmente a Main Menu, confirmó que Arriba/Abajo funcionaban y pulsó F9 una vez. Log: `[CKPT:REQUEST] SUCCESS tick=658 path=dmc_checkpoint_mainmenu_v2.dmck size=46350585 ms=355.33`. El proceso se cerró por completo. El verificador independiente `analysis/local/devstate_003d/verify_v2.py` confirmó cabecera/CRC y 12/12 secciones; SHA-256 del archivo `64cb9683311dc948378ea0f249f12130033c8e21f207e2d49f45d28b94aa3d72`. ID compat `95a9a51600f57a4f3bf496298dfb9193904e0b7c9bc832a5bd830613e75d4fdc`; ID ejecutable guardado `87bdcd97d993f36e265729f0c549c43718ef6d4efd00c1c2efe9c54c175d9fb3`.

**HECHO:** Proceso fresco con el mismo ejecutable: `[CKPT:LOAD] SUCCESS stage=Complete pc=0x19f7dc resuming scheduler`; usuario confirmó Main Menu visible e input funcional (`restore_before_rebuild.log`). El proceso se cerró por completo.

**HECHO:** Se añadió un marcador de diagnóstico opt-in a `main.cpp` para forzar un relink no semántico (solo lee una variable de entorno e imprime un renglón). El ejecutable pasó a SHA-256 `e10f90caa9898c0198e853ea47c08205d9f108112fcce289f2f27a67d76329f7`, ID exacto `cb9d9f83d546d0588636814d77da498fe732b053cb665e56cdec8762d1002e62`. El compat ID siguió idéntico. El mismo checkpoint, sin modificación, imprimió `[CKPT:LOAD] executable build differs, checkpoint ABI compatible` y luego `SUCCESS stage=Complete`; usuario confirmó de nuevo Main Menu visible e input funcional (`restore_after_rebuild.log`). El proceso se cerró por completo.

**HECHO:** El marcador temporal se retiró del código y se hizo un relink incremental final (solo `main.cpp` + link). Ejecutable final: SHA-256 `2b8da0b529bb7164410d446a6ccc0a3a09128113b62d6e3777f0277cbd9f0e50`; ID exacto `7dc4c2cd275222c36e623f1b4a5911b918ef6d4efd00c1c2efe9c54c175d9fb3`. ID compat sin cambios. Selftests finales: **97 PASS / 0 FAIL** (`selftest_final.log`). SHA-256 del checkpoint sin cambios.

**HECHO:** El archivo v2 cargó también en la build final con aviso de ejecutable distinto y `SUCCESS stage=Complete`. En el smoke, el usuario seleccionó NEW GAME y confirmó la misma pantalla negra. El log `restore_final_smoke.log` reprodujo las lecturas `resourceId=0x52…0xFE`, el warning `unsupported fno=2 mode=2 size=0xc6800` y la asignación de stack async (`thread=7`), como en la evidencia anterior de GAMEPLAY_001/DEVSTATE_003C. El proceso se cerró. **INFERENCIA:** No hay regresión de restauración visible hasta ese bloqueo conocido; no se atribuye causalidad al warning ni se investiga gameplay en esta fase.

## Patch reproducible y estado de Git

**HECHO:** La implementación se aisló en `patches/DEVSTATE_003D_checkpoint_compatibility_id.patch`, SHA-256 `865ca98a81d6b203b841be593cc11fe979aabcc95f11c21614ba23b8556340b7`, aplicado después de DEVSTATE_003 v1. El patch contiene solo seis archivos de checkpoint/main; no incluye `Pad.cpp`, input temporal, builds, ISO, checkpoint, logs ni código generado. `upstream.lock.json` tiene trece patches y `patched_commit=dcd03052f7e8b43bac6dd9e598ea7237893f78c3`.

**HECHO:** En `analysis/local/devstate_003d/full_replay` se aplicaron los trece patches, en orden, sobre el commit upstream real `14b1e5cb39b4af7e6fc12f9a29fdc751efde49d7`, verificando SHA-256 de cada patch y `git apply --check`. El árbol resultante `2c57ce677a0785792aab0018bfaf254b9c3df8c4` produjo el commit sintético determinista `dcd03052f7e8b43bac6dd9e598ea7237893f78c3`, idéntico al lock. Un replay separado del patch 13 sobre el `patched_commit` anterior `ae378ab...` produjo contenido igual (normalizando CRLF) a los seis archivos validados en la build local.

**HECHO:** Durante la implementación no se modificó el HEAD ni se limpió el working tree preexistente de `vendor/PS2Recomp`; su estado anterior se preservó. En esa fase no se hizo commit ni push. `analysis/local/devstate_003d/` es evidencia local excluida de Git.

## Promoción reproducible

**HECHO:** Antes del commit se repitió el replay de los 13 patches desde `14b1e5cb39b4af7e6fc12f9a29fdc751efde49d7` en `analysis/local/devstate_003d/full_replay_promotion`. Cada SHA-256 y `git apply --check` pasó; el árbol resultante fue `2c57ce677a0785792aab0018bfaf254b9c3df8c4` y el commit sintético `dcd03052f7e8b43bac6dd9e598ea7237893f78c3` coincide con `upstream.lock.json`.

**HECHO:** La compilación incremental de `ps2EntryRunner.vcxproj` en `analysis/local/symtabfirst/build` terminó correctamente. Los selftests repetidos registraron **97 PASS / 0 FAIL** en `analysis/local/devstate_003d/selftest_promotion.log`. El commit de producción `a64e246` (`feat: decouple checkpoint compatibility from executable build`) contiene únicamente el patch, `upstream.lock.json` y `patches/README.md`. La documentación se promueve en un commit separado; los checkpoints locales, builds y logs no forman parte del commit de producción.

**UNKNOWN:** La causa de la pantalla negra de NEW GAME; corresponde a GAMEPLAY_001.

`RETRACTADO`: ninguno. El checkpoint v1 no se reinterpreta ni se fuerza.
