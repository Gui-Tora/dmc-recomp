# PERF_003: sondas diagnósticas del GS (fuera de la suite integrada)

`PERF_003_gs_independent_probes.diag.patch` añade a `ps2xTest/src/ps2_gs_tests.cpp` la suite
`PS2GSDepthIndependentProbes`, con dos pruebas. Sus expectativas salen de PCSX2 y no dependen de `TEST.ZTE`:

| Prueba | Incidencia | Expectativa (PCSX2) |
|---|---|---|
| `depth_is_clamped_to_the_z_format_maximum` | GS-1 | la Z se limita a 0xFFFF en Z16 y a 0xFFFFFF en Z24 |
| `date_on_a_24bit_frame_skips_the_draw` | GS-2 | con DATE=1 y FRAME CT24 no se escribe nada |

**Las dos fallan** con el patchset de `upstream.lock.json` (32 parches, `patched_commit` `b7840ea9…`). Es la
evidencia de `analysis/notes/PERF_003_GS_OPEN_ISSUES.md`. No es un parche del pipeline: no figura en el lock y no
se debe añadir a `patches/` mientras GS-1 y GS-2 sigan abiertas.

## Uso (en una copia, nunca en `vendor/PS2Recomp`)

1. Partir de un árbol en `patched_commit`: un clon de `vendor/PS2Recomp` con `core.autocrlf=false` y el patchset
   aplicado (`scripts/pipeline.py`, `apply_patchset`).
2. `git apply PERF_003_gs_independent_probes.diag.patch`. Solo añade líneas; usa el auxiliar `drawRaw` del
   parche 32. Fuera de `patches/` no hay regla `eol=lf`: en un clon de este repositorio con `core.autocrlf=true`
   el fichero sale con CRLF y `git apply` falla; en ese caso, usar `git apply --ignore-whitespace` (las líneas
   añadidas llevan CRLF, lo que no afecta a la compilación).
3. Compilar y ejecutar `ps2x_tests`. Resultado esperado mientras las incidencias sigan abiertas:
   - 483 pruebas, 26 fallos: los 24 preexistentes más estas 2.
   - Sin la sonda, la suite integrada tiene 481 pruebas y 24 fallos.

Si se corrige GS-1 o GS-2, la prueba correspondiente debe pasar a formar parte del parche de esa corrección.
