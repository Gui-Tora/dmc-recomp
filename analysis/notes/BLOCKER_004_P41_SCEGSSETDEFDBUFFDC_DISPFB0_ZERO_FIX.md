# P4.1 — SCEGSSETDEFDBUFFDC_DISPFB0_ZERO_FIX

Implementación y validación causal A/B del fix de causa raíz identificado en
[`BLOCKER_004_P402_SCEGSSETDEFDBUFFDC_ROOT_CAUSE.md`](BLOCKER_004_P402_SCEGSSETDEFDBUFFDC_ROOT_CAUSE.md).
Continúa la cadena P4.0 (Fable) → P4.0.1 → P4.0.2 → **P4.1** (este informe).

## 1. Causa raíz congelada desde P4.0.2 (HECHO, no re-derivada aquí)

`sceGsSetDefDBuffDc` (HLE, `vendor/PS2Recomp/ps2xRuntime/src/lib/Kernel/Stubs/GS.cpp`)
inicializaba el campo `FBP` de `disp[0].dispfb` con `zbufAddr` (dirección de
Z-buffer, calculada por `sceGszbufaddr` = `0xE0` para los parámetros reales
de Devil May Cry: `w=512,h=448`), en vez del literal `0` que usa
consistentemente para `disp[1].dispfb`. P4.0.2 probó, por simetría de código
guest real (nada en `Main_init`/`MainSetPalMovieEnv` escribe
`disp[0].dispfb` después del HLE, igual que para `disp[1].dispfb` antes de
que `Main_init` lo parchee explícitamente a `0xA0`), que el valor inicial
correcto de `disp[0].dispfb` según el contrato real del SDK es `FBP=0`.
`IMPLEMENTATION_READY: YES` fue la conclusión de cierre de P4.0.2.

## 2. Cambio de producción exacto (una línea)

Archivo: `vendor/PS2Recomp/ps2xRuntime/src/lib/Kernel/Stubs/GS.cpp`,
dentro de `sceGsSetDefDBuffDc` (bloque `disp[0]`/`disp[1]`, ~línea 906-912).

```diff
 const uint32_t fbp1 = zbufAddr;
-const uint64_t dispfb0 = makeDispFb(fbp1, fbw, psm, 0u, 0u);
+// P4.1 fix (BLOCKER_004_P402_SCEGSSETDEFDBUFFDC_ROOT_CAUSE.md): disp[0]'s
+// DISPFB must default to FBP 0, matching disp[1]'s own literal below --
+// not the Z-buffer address. Using zbufAddr here produced FBP 0xE0,
+// proven (by direct original PCSX2 measurement + guest-code symmetry
+// argument) to diverge from the real SDK default of 0.
+const uint64_t dispfb0 = makeDispFb(0u, fbw, psm, 0u, 0u);
 const uint64_t dispfb1 = makeDispFb(0u, fbw, psm, 0u, 0u);
```

`fbp1`/`zbufAddr` en sí **no se toca**: sigue usándose legítimamente en las
otras 4 llamadas de la misma función (Z-buffer real y punteros de frame
`draw11`/`draw12`). Verificado vía `git -C vendor/PS2Recomp diff`: exactamente
este hunk, +5/-1 líneas, ningún otro cambio.

## 3. Por qué AMOD no se tocó en este prompt (deliberado)

P4.0.2 identificó, como hallazgo secundario e independiente, que
`makePmode(1,1,0,/*amod=*/0,0,0x80)` dentro de la misma función hardcodea
`amod=0`, mientras que PCSX2 original mide `AMOD=1` (bit6, la única
diferencia entre `PMODE=0xFF26` observado en RECOMP y `PMODE=0xFF66`
observado en original). `MainSetPalMovieEnv` no toca ese bit tampoco. Este
prompt (P4.1) restringió explícitamente el alcance a un único cambio
semántico (`dispfb0`); el fix de AMOD queda diferido a un prompt posterior
(P4.1.x), para poder atribuir sin ambigüedad cualquier efecto observado en
esta validación exclusivamente al cambio de `dispfb0`.

## 4. Entorno esperado (estático, antes de correr)

| Campo | Pre-fix (medido, P4.0.1/P4.0.2) | Post-fix (predicho) |
|---|---|---|
| `disp[0].dispfb` (`0x740C40`) | `FBP=0xE0` → `0x00001c0` (word bajo `0x000001c0`... ver nota) | `FBP=0` → `0x00001000` |
| `disp[1].dispfb` (`0x740C78`) | `FBP=0xA0` → `0x000010a0` (sin cambio, parcheado por `Main_init`) | `FBP=0xA0` → `0x000010a0` (sin cambio) |
| `disp[0].pmode` / `disp[1].pmode` | `0x0000ff26` | `0x0000ff26` (sin cambio, AMOD no tocado) |

(Nota: el valor pre-fix exacto de `dispfb0` con `FBP=0xE0` no fue vuelto a
medir en este prompt — se reusa la derivación numérica ya cerrada como
HECHO en P4.0.2; no es necesario para la validación A/B, que compara
directamente contra el post-fix medido.)

## 5. Método de build

Build quirúrgico ya establecido en la sesión: MSBuild directo de
`ps2_runtime.vcxproj` en `analysis/local/symtabfirst/build/upstream/ps2xRuntime/`
(árbol activo real usado por `run.py`), verificado `grep -c "CL.exe"` = 1
(solo `GS.cpp` recompiló). Relink con
`ps2EntryRunner.vcxproj /t:_BuildLinkAction`, verificado `grep -c "CL.exe"` =
0 (cero recompilación de archivos generados). Backup del `.lib` pre-fix
conservado en `analysis/local/p41/ps2_runtime.pre_p41.lib`
(SHA256 `570bd93d279cb58e00769cca7e1a6738c7b88a435dffbf6a32e7fca8c35d5e58`).

## 6. Identidad del ejecutable

- Pre-fix (referencia, build previo de la sesión): `44c003afac3671a0cf202ae3c6ab1bf6b3bb8a5de6e32396ae82f4fa31e18e3d`
- Post-fix (usado en las 3 corridas de validación, verificado idéntico en
  `result.json` de las 3): `27faab90b0db40705b6c559c5b79c44f4b7bf2bab5bb62d338bde5b17880d13b`

## 7. Corridas de validación

`RUN_044`, `RUN_045`, `RUN_046` (`analysis/local/p311/`), mismo comando base
(`--runs 1 --keys ENTER,X --method runtime --diagnostics --post-target
MPEG_INIT --visual-trace`, `PS2X_CD_IMAGE`/`PS2X_RUNTIME_ARENA_*` estándar,
`DMC_P313_INIT_MODE=ownership`). Las 3 con `"result": "SUCCESS"` y
`exe_sha256` idéntico entre sí y a la sección 6.

## 8. Entorno guest observado (post-fix, re-verificado directamente en este turno)

Leído directamente de `getpicture.bin` (dump de RAM completo, 32 MiB, en el
primer `GetPicture` exitoso) de las 3 corridas, offset = dirección guest:

| Dirección | Campo | RUN_044 | RUN_045 | RUN_046 |
|---|---|---|---|---|
| `0x740C30` | `disp[0].pmode` | `0x0000ff26` | `0x0000ff26` | `0x0000ff26` |
| `0x740C40` | `disp[0].dispfb` | `0x00001000` | `0x00001000` | `0x00001000` |
| `0x740C68` | `disp[1].pmode` | `0x0000ff26` | `0x0000ff26` | `0x0000ff26` |
| `0x740C78` | `disp[1].dispfb` | `0x000010a0` | `0x000010a0` | `0x000010a0` |

3/3 idéntico byte a byte, y 3/3 coincide exactamente con la predicción de
la sección 4 (`FBP=0` para `disp[0]`, `FBP=0xA0` sin cambio para `disp[1]`,
`PMODE` sin cambio). `dispfb0` con `FBP=0xE0` (bug pre-fix) **ausente** en
las 3 corridas.

## 9. Estado activo de DISPFB2 (registro privilegiado GS)

No se re-instrumentó dinámicamente la escritura privilegiada en este
prompt (para no violar la restricción de "no logging GS amplio" reiterada
en prompts previos). Se infiere el valor activo a partir del contrato ya
probado byte-exacto en P4.0.1 de `MainGsSwapDBuffDc`: lee `pmode`/`dispfb2`
**literalmente** del struct guest (sin transformación) y los escribe vía
`Store64(0x12000000, pmode)` / `Store64(0x12000090, dispfb2)`. Dado que la
sección 8 confirma los valores del struct guest, y ese contrato de copia
literal ya es HECHO (disassembly directo, no repetido aquí), el valor
activo de `DISPFB2` post-fix se infiere igual a los valores de la sección
8: alterna `0x00001000` / `0x000010a0` según qué mitad del doble buffer
esté activa. **Esto es una inferencia apoyada en un contrato ya probado,
no una nueva medición directa del registro privilegiado en este turno** —
limitación reconocida explícitamente (ver §14).

## 10. Comparación original vs RECOMP post-fix

| | Original (PCSX2, medido en vivo, P4.0.1) | RECOMP post-fix (inferido, §9) |
|---|---|---|
| `DISPFB2` (activo) | alterna `0x1000` / `0x10A0` | alterna `0x1000` / `0x10A0` |
| `DISPFB1`/`DISPLAY1` | `0` (circuito 1 deshabilitado) | sin cambio en este prompt (no tocado) |
| `PMODE` | `0xFF66` (AMOD=1) | `0xFF26` (AMOD=0) — **diferencia conocida, no corregida aquí** |

`DISPFB2` coincide exactamente tras el fix. `PMODE` difiere solo en el bit
AMOD, diferencia ya identificada y deliberadamente diferida (§3).

## 11. Comparación visual A/B

Pre-fix (referencia ya establecida en P3.14.2/P3.15.x, no re-generada en
este prompt): fragmentos angostos, bandas horizontales, "ghosting" —
consistente con estar leyendo `FBP=0xE0` (un buffer de staging/Z, no el
buffer de imagen real) para uno de los dos slots de doble buffer.

Post-fix (re-inspeccionado directamente en este turno, capturas de
`--visual-trace`):
- `RUN_046/visual_082453ms.png`: texto cursivo dorado "Devil" legible
  sobre fondo humo/estrellado oscuro, coherente y sin bandas.
- `RUN_044/visual_088641ms.png`: texto cursivo "May" sobre fondo azulado
  con textura de humo/estática, igualmente coherente.
- `RUN_044/visual_082641ms.png`: textura de fuego/humo a **altura
  completa de cuadro** (448px), granular/con bloques (consistente con los
  warnings `ac-tex damaged`/`MVs not available` de ffmpeg en el log —
  artefactos de decodificación MPEG esperados, no un bug de presentación).
- `RUN_044/visual_091655ms.png`: jet de fuego/llama, coherente, pero con
  la **mitad inferior del cuadro en negro sólido** (mismo patrón que los
  frames de texto arriba).

Mejora dramática y reproducible frente al pre-fix (fragmentos irreconocibles
→ imagen/texto legible). **Caveat honesto**: en varios frames post-fix
(incluyendo ambos frames de texto) solo la mitad superior del cuadro (~260
de 448 líneas) contiene imagen; la mitad inferior es negro sólido. No se
determinó en este prompt si esto es (a) letterboxing genuino de estos
frames específicos del video PSS original, (b) un efecto de
entrelazado/campo no completamente resuelto, o (c) un síntoma residual
distinto del bug ya corregido. No bloquea la conclusión de este prompt
(el fix corrige exactamente lo que se propuso corregir, con evidencia
fuerte), pero se deja registrado como trabajo futuro, no como
"completamente resuelto".

## 12. Chequeo de regresión MPEG

Conteo de invocaciones `CALL sceMpegGetPicture` en `runtime.log`:
RUN_044=187, RUN_045=192, RUN_046=194. Ninguna corrida terminó en deadlock,
crash, ni error fatal — el log de las 3 termina en medio de una secuencia
normal de decodificación (llamada a `GetPicture` seguida de warnings
`ac-tex damaged`/`MVs not available`, que son mensajes normales y
recurrentes de ffmpeg en todo el corpus de corridas de esta sesión, no
errores). **Corrección respecto al borrador previo de este informe**: no
se confirma la cifra "150" mencionada informalmente durante la sesión como
número exacto de éxitos de esta validación — el conteo real, re-verificado
directamente en este turno vía `grep`, es 187–194 invocaciones por corrida,
igual o mayor al techo ya establecido en P3.14.2 (~250 sostenidas es el
techo roto documentado ahí; el rango aquí es consistente con corridas más
cortas por el `--post-target MPEG_INIT` usado). No hay evidencia de
regresión: progresión sostenida, sin caída súbita, sin excepción.

## 13. Reproducibilidad

3/3 corridas: mismo hash de ejecutable, mismos 4 valores de struct guest
byte-exactos (§8), mismo patrón general de progresión MPEG sana (§12),
mejora visual coherente y reproducible en múltiples timestamps de
corridas distintas (§11).

## 14. Caveats restantes (honestos, no resueltos en este prompt)

- `AMOD` (bit6 `PMODE`) sigue divergente (`0xFF26` RECOMP vs `0xFF66`
  original) — bug independiente ya identificado en P4.0.2, diferido a
  P4.1.x.
- El valor activo de `DISPFB2` (registro privilegiado) se infiere por
  contrato ya probado, no se re-midió dinámicamente en este turno (§9).
- La semántica PSM/formato de píxel del pipeline GS (establecida
  parcialmente en P3.15.2) sigue sin auditoría exhaustiva.
- Patrón de "mitad inferior del cuadro en negro" observado en varios
  frames post-fix (§11), causa no determinada en este prompt.
- No se afirma corrección general de renderizado más allá de este fix
  específico.

## 15. Estado de AMOD (fase de observación, no de fix)

Confirmado en las 3 corridas (§8): `PMODE` en `0x740C30`/`0x740C68` sigue
siendo `0x0000ff26` — AMOD permanece en el valor de RECOMP (`0`), exactamente
como se requería/esperaba para este experimento (cambio aislado a `dispfb0`
únicamente, §3). No se tocó ni se midió AMOD activo dinámicamente más allá
de esta confirmación estática.

## Ledger de correcciones (histórico, preservado)

- **P4.0 (Fable)**: baseline causal independiente; concluyó que `PMODE.EN1`
  nunca llega a 1 en RECOMP como "primer nodo divergente probado",
  infiriendo que el hardware original debía habilitar EN1/circuito-1 para
  mostrar la película.
- **P4.0.1**: **RETRACTA** la teoría EN1/circuito-1 de P4.0F en base a
  mediciones directas nuevas de PCSX2 original (`PMODE=0xFF66`, EN1=0
  también en original). Confirma que la divergencia real está en la
  construcción del entorno guest antes de la escritura GS, y ubica al
  sospechoso principal en `sceGsSetDefDBuffDc`.
- **P4.0.2**: cierra la causa raíz — `zbufAddr=0xE0` (cómputo exacto
  verificado), `disp[1].dispfb=0xA0` escrito por `Main_init` (código guest
  real, confirmado por dirección exacta), `disp[0].dispfb` permanece en el
  default HLE (bug: usa `zbufAddr` en vez de literal `0`).
  `IMPLEMENTATION_READY: YES`.
- **P4.1 (este informe)**: implementa el fix de una línea, valida 3/3 con
  coincidencia exacta de entorno guest contra la medición original en vivo,
  mejora visual dramática y reproducible, sin regresión MPEG. Clasificado
  `FIX_CHECKPOINT_WITH_KNOWN_CAVEATS` — no es un cierre global de
  BLOCKER_004 (AMOD y semántica PSM siguen pendientes).

## Clasificación

`P4_SCEGSSETDEFDBUFFDC_DISPFB0_FIX_VALIDATED` — fix de causa raíz aplicado
e independientemente re-verificado (RAM dump directo, 3/3), con mejora
visual reproducible y sin regresión. Checkpoint de tipo
`FIX_CHECKPOINT_WITH_KNOWN_CAVEATS`.
