# Promoción del baseline symtab-first (2026-09-08)

## Por qué abandonamos el CSV de Ghidra (R6) como fuente por defecto

El CSV exportado con Ghidra usando el lenguaje `MIPS:LE:64:64-32R6addr`
(sin soporte real del R5900) producía límites de función incorrectos:
29 funciones >1MB (18-250MB algunas), 80 pares de funciones solapadas. La
causa raíz (documentada en `HUGE_GENERATED_FILES.md`) es que Ghidra
reinterpreta opcodes propios de COP2/VU0/MMI como instrucciones de MIPS
Release 6 que nunca existieron en el hardware real, corrompiendo el cálculo
de dónde empieza/termina cada función.

Comparación completa (`analysis/local/csv_comparison_report.txt`):

| Fuente | Funciones | >1MB | Solapes | Cobertura fusionada |
|---|---|---|---|---|
| CSV Ghidra R6 | 11012 | 29 | 80 | 4.12M bytes / 156 bloques |
| CSV Ghidra sin R6 | 11082 | 1 (`Movie_on`) | 58 | 2.41M bytes / 4721 bloques |
| `.symtab` real del ELF | 10124 | 0 | 0 | 4.09M bytes / 7366 bloques |

Este ELF no está completamente stripped: `ps2_recomp` puede extraer
directamente ~10125 funciones reales desde `.symtab`/`.strtab`, sin
necesidad de ningún CSV de Ghidra.

## Resultado del experimento A/B

- **A** = pipeline con CSV Ghidra R6 + parche manual de `BLOCKER_001`
  (extensión de `entry` a `0x1000A8`) + 20 stubs `ret0` VU0.
- **B** = mismo ELF, mismos stubs `ret0`/SDK, pero `ghidra_output = ""`
  (límites 100% desde `.symtab`, sin ningún parche de `BLOCKER_001`).

Resultado: **B pasó `BLOCKER_001` automáticamente**, sin ningún parche
manual — la `.symtab` ya tenía el límite correcto de `entry`
(`0x100008`-`0x1000A8`) desde el principio. B llegó exactamente tan lejos
como A: carga de módulos IOP, DMA, memory card, mismo bucle de
`Print_message`, y se detiene en **el mismo `BLOCKER_002`, con el mismo
registro exacto**:

```
source=0x2e11c8 target=0x2000100 ra=0x2e1540 v0=0x2000100 v1=0x586740
```

Esto **descarta** que `BLOCKER_002` sea causado por límites de función
incorrectos — es un bug real e independiente del origen de los límites
(índice o lectura de memoria corrupta, ver `BLOCKER_002_indirect_jump_top_of_ram.md`).

Según el criterio acordado ("si B supera BLOCKER_001 automáticamente y llega
igual o más lejos que A, la `.symtab` pasa a ser la fuente principal"): **la
`.symtab` pasa a ser la fuente principal de límites de función.**

## Promoción sin recompilar

El build de la variante B (`analysis/local/symtabfirst/build`) ya estaba
compilado y enlazado con éxito (ver más abajo, tardó ~2h34 por una
interrupción intermedia y corrupción del `.pdb` compartido — en condiciones
normales seria ~1-1.5h). En vez de repetir el build para convertirlo en el
baseline principal, se promovió el build **existente** sin recompilar:

- **Directorio generado usado**: `analysis/local/symtabfirst/generated`
  (10127 `.cpp`, población original vía `ps2_recomp.exe` directo con
  `ghidra_output=""`).
- **Directorio de build usado**: `analysis/local/symtabfirst/build`
  (CMake + MSBuild, target `ps2EntryRunner`).
- **Ejecutable final**: `analysis/local/symtabfirst/build/bin/Debug/dmc-recomp.exe`.
- **CMake cache**: `analysis/local/symtabfirst/build/CMakeCache.txt` — tiene
  docenas de rutas absolutas (`DmcBringup_BINARY_DIR`, `FETCHCONTENT_BASE_DIR`,
  `DMC_GENERATED_DIR`, etc.) apuntando literalmente a
  `<repo-root>/analysis/local/symtabfirst/{build,generated}` (ruta absoluta
  local, fuera de Git).
  **No se movió ni renombró ningún directorio** — mover/renombrar habría
  invalidado esas rutas y forzado una reconfiguración/recompilación completa
  (exactamente lo que se quería evitar).
- **PDBs**: `dmc-recomp.pdb` (491MB), más los de `ps2_runtime`, `ps2_iop`,
  `raylib`, `glfw` bajo `build/upstream/`, `build/_deps/`.
- **Object files**: 10229 `.obj` (10127 propios + dependencias raylib/glfw/ps2_iop/ps2_runtime).

En vez de mover archivos, se cambiaron las rutas que usa `scripts/pipeline.py`
para "build activo"/"generado activo" (funciones `sync_active()` y `build()`)
para que apunten directamente a estas rutas existentes. Documentado en el
propio código con comentarios explicando el porqué.

Como el generado activo (`analysis/local/symtabfirst/generated`) ya
contenía exactamente el código a usar, no hizo falta invocar `ps2_recomp` ni
`cmake` de nuevo: se construyeron a mano los recibos que el pipeline usa
para verificar consistencia (`analysis/local/prepared.json`,
`analysis/local/generated.json`, `analysis/local/built.json`, y el
`.manifest.json` de `sync_active()`), todos derivados de hashes reales de lo
que ya existía en disco — sin ejecutar ninguna herramienta de compilación.

## Confirmación: ni una recompilación

- **SHA256 de `dmc-recomp.exe` antes de la promoción**:
  `6416e0fa68fe11057a1f017701f8d876ff36c0eb00fc90187e52cb1a42be6e65`
- **SHA256 de `dmc-recomp.exe` después de la promoción**:
  `6416e0fa68fe11057a1f017701f8d876ff36c0eb00fc90187e52cb1a42be6e65`
- **Coinciden exactamente** — el ejecutable no se tocó en ningún momento del
  proceso de promoción.
- `python scripts/pipeline.py run --seconds 60` corrido después de la
  promoción, usando el flujo real del pipeline (no un test aislado):
  reprodujo `BLOCKER_002` con el registro idéntico
  (`source=0x2e11c8 target=0x2000100 ra=0x2e1540 v0=0x2000100 v1=0x586740`),
  confirmando que el baseline promovido funciona de extremo a extremo.

## Cambios en scripts/pipeline.py

- `sync_active()` y `build()` apuntan a `analysis/local/symtabfirst/{generated,build}`
  en vez de `recomp/generated_active`/`build/runtime/active` (ver comentarios
  en el código explicando por qué).
- `prepare`: `--csv` ahora es opcional (antes obligatorio). Sin `--csv`, no
  se sustituye `ghidra_output` en el TOML (se respeta lo que ya tenga la
  plantilla).
- `prepare`: `--toml` ahora tiene un default, `recomp/symtabfirst.toml`
  (nuevo, symtab-first, `ghidra_output = ""`, mismos stubs SDK/VU0 validados
  que `analysis/ghidra/export/dmc.toml`). El CSV de Ghidra (R6 o sin R6)
  sigue disponible pasando explícitamente
  `--toml analysis/ghidra/export/dmc.toml --csv analysis/ghidra/export/dmc_functions.csv`
  para experimentos/reversing, pero ya no es la ruta por defecto.

## Pendiente / no bloqueante

- Los directorios activos viven bajo `analysis/local/` (gitignorado, nombre
  "symtabfirst" que ya no describe bien un baseline permanente). Es
  cosmético — puede limpiarse el día que un full rebuild sea inevitable por
  otra razón; no vale la pena forzarlo solo por esto.
- `recomp/generated_active` y `build/runtime/active` (las rutas viejas, del
  baseline con CSV R6 + parche de `BLOCKER_001`) siguen en disco, sin usarse
  activamente. Se pueden borrar cuando se confirme que no van a hacer falta
  para comparaciones futuras.
