# DEVSTATE_003 — Promoción del checkpoint de Main Menu al pipeline de patches

Fecha: 2026-10-02. Cierra la fase DEVSTATE_001/002/003A/003B/003C,
promoviendo la implementación ya validada a `patches/` +
`upstream.lock.json`, sin modificar semántica de runtime.

## 1. Qué se promovió

`patches/DEVSTATE_003_main_menu_checkpoint_v1.patch` (patch 12, último en
orden de aplicación), `sha256=261d78196be982c37822a65011f2d8998baa094a4e7f144310cfbd1a3f121243`.
Contiene exactamente el sistema de checkpoint "Main Menu v1" descrito en
`DEVSTATE_001_MAIN_MENU_CHECKPOINT.md` a `DEVSTATE_003C_COMPLETE_HLE_AND_COLD_LOAD.md`:
formato de archivo de 12 secciones, validador de punto seguro, arranque en
frío vs. restauración (`DMC_DEVSTATE_LOAD`), solicitud de guardado real
(F9), identidad de proceso, y los self-tests asociados.

`upstream.lock.json`: `patched_commit` pasó de `fd7f2b4358689cbbc46e98ce1c78e3b8b2478963`
(11 patches) a `ae378ab333501ec76b8db16069f8ac616514f040` (12 patches, el
nuevo al final de `patches[]`).

## 2. Estado de partida (vendor dirty) y por qué no se podía commitear directo

El checkout local de `vendor/PS2Recomp` estaba (y permanece, fuera de este
patch) intencionalmente sucio: su `HEAD` (`0efd17c3...`) correspondía a un
`patched_commit` generado con un subconjunto más antiguo de `patches[]`
(anterior a los patches 10/11, `p423_p424`/`q101`), y el working tree
contenía además, sobre eso:

- el contenido de los patches 10 (`p423_p424_mpeg_movie_completion`) y 11
  (`q101_field_presentation`), ya escritos como archivos `.patch` y ya en
  `upstream.lock.json` desde commits de root anteriores, pero nunca
  vueltos a materializar en el checkout vía `bootstrap()`;
- diagnóstico temporal `INPUT_001` en `Pad.cpp` (gateado por
  `DMC_INPUT001_TRACE`, marcado en su propio comentario como "Remove after
  input arbitration fix is validated"), sin relación con el checkpoint;
- la implementación completa de DEVSTATE_001-003C.

Commitear ese árbol tal cual habría versionado trabajo ya cubierto por
patches existentes y un diagnóstico explícitamente excluido de producción
mezclado con el trabajo real de esta fase. Había que aislar.

## 3. Método de aislamiento (verificado por hash/diff, no por inspección)

1. Se preservó el estado completo (status root+vendor, diff dirty
   completo, SHA256) en `analysis/local/devstate_checkpoint/` antes de
   tocar nada — nunca se destruyó el working tree real de vendor.
2. Se clonó `vendor/PS2Recomp` localmente a un directorio scratch aislado,
   se hizo checkout del commit baseline real (`14b1e5cb39b4af7e6fc12f9a29fdc751efde49d7`,
   el mismo `commit` de `upstream.lock.json`), y se aplicaron los 11
   patches ya declarados, en orden, vía `git apply --check`+`git apply`.
3. Se generó el commit determinista (`git commit-tree`, misma
   `patch_identity` fija del pipeline) y se verificó que reproduce
   exactamente `fd7f2b4358689cbbc46e98ce1c78e3b8b2478963` — confirma que
   el estado de 11 patches declarado en el lock es íntegro y reproducible
   byte a byte desde el commit real de upstream (sin red: el clon local
   reutiliza los mismos objetos git que originalmente vinieron de GitHub,
   ya presentes en el repositorio).
4. Se trajo ese commit limpio al object store del vendor real (`git
   fetch` desde el clon scratch — solo agrega objetos, nunca toca
   `HEAD`/working tree) y se comparó (`git diff fd7f2b4... -- .`) contra
   el árbol de trabajo real: **`GS.cpp` y `gs_cpu_backend.cpp` dejaron de
   aparecer como modificados** (es decir, su "dirty" previo era 100 % el
   contenido ya versionado de los patches 8/9/11, nada de DEVSTATE) — la
   diferencia restante quedó acotada a exactamente 26 archivos
   modificados + 3 rutas nuevas (`checkpoint_bytes.h`,
   `runtime/checkpoint/`, `src/lib/checkpoint/`).
5. De esos 26, se inspeccionaron a nivel de hunk los dos casos dudosos:
   - `MPEG.cpp`: el diff restante resultó ser 100 % DEVSTATE
     (`checkpointProbeMpegMask`/`checkpointSerializeMpeg`/
     `checkpointDeserializeMpeg`) — el contenido de `p423_p424` ya estaba
     absorbido por el baseline limpio, no quedaba residual.
   - `Pad.cpp`: el diff restante contenía 2 hunks de diagnóstico
     `INPUT_001` (función `input001TraceEnabled()` + bloque de traza en
     `getPadData`) entremezclados con 1 hunk de
     `checkpointSerializePad`/`checkpointDeserializePad`. Se excluyeron
     los 2 hunks de `INPUT_001` a mano y se verificó por `grep` que
     `input001TraceEnabled`/`DMC_INPUT001_TRACE` no se usan en ningún otro
     archivo (diagnóstico autocontenido, seguro de remover sin tocar
     semántica de runtime).
6. Se reconstruyó el patch final copiando los 25 archivos 100 % DEVSTATE +
   las 7 rutas nuevas completas, y fusionando a mano el único hunk de
   `Pad.cpp` sobre el baseline limpio. Verificación final: `diff -rq`
   recursivo (contenido de archivo, no basado en índice git) entre ese
   árbol reconstruido y el árbol real de vendor — **cero diferencias en
   todo el árbol**, salvo exactamente `Pad.cpp` (la exclusión
   `INPUT_001` esperada) y un directorio `analysis/local/pre_perf_checkpoint`
   ajeno (no versionado, irrelevante).
7. Se generó `patches/DEVSTATE_003_main_menu_checkpoint_v1.patch` desde
   ese árbol reconstruido (`git diff --cached`, formato `git apply`-puro,
   sin cabecera de comentario).

## 4. Replay completo desde upstream (determinismo)

Clon fresco adicional, checkout del commit real de upstream, aplicación
de los 12 patches en el orden declarado (11 existentes + el nuevo,
último) — las 12 aplicaciones son limpias (`git apply --check` sin
fallos). Commit determinista resultante:
`ae378ab333501ec76b8db16069f8ac616514f040`. Comparación final
(`diff -rq` recursivo de contenido) entre ese árbol de 12 patches y el
árbol real de vendor (ya con la exclusión `INPUT_001` aplicada también
ahí para la build de verificación, ver §5): **cero diferencias en todo el
árbol**.

**FULL_REPLAY_FROM_UPSTREAM: PASS.**
**VALIDATED_IMPLEMENTATION_MATCH: YES** (verificado por diff de contenido
archivo por archivo, no por inspección visual).

## 5. Build y self-tests contra el estado promovido

Para validar la build exactamente contra el estado que el patch
reproduce (y no solo contra el árbol dirty previo), se aplicó la misma
exclusión `INPUT_001` al `Pad.cpp` real de vendor (cambio mínimo,
autocontenido, sin headers compartidos) y se hizo un build incremental
(recompilar `Pad.cpp` + relink de `ps2_runtime.lib`/`ps2EntryRunner`,
sin rebuild masivo). Self-tests de checkpoint
(`DMC_CHECKPOINT_SELFTEST=1`):

```
[CKPT:SELFTEST] done, failures=0
```

**89 PASS / 0 FAIL** (conteo idéntico a la corrida de validación de
DEVSTATE_003C, ya que el código `INPUT_001` excluido nunca se ejercita en
el camino de self-test — no se altera ningún resultado de test al
quitarlo).

## 6. Resultados validados que se promueven (resumen; detalle completo en
DEVSTATE_003B/003C)

- SAVE real: `tick=11612`, `size=46350553` bytes, `344.36 ms`, 12/12
  secciones con CRC válido.
- LOAD en proceso completamente nuevo: `stage=Complete`,
  `pc=0x19f7dc`, scheduler reanuda en `tick=11613`. Main Menu visible e
  interactivo confirmado por el usuario (no inferido de logs); input
  (Abajo/Arriba) correcto; continuidad de SCHED/GS/CD/MPEG/SIF-RPC
  confirmada.
- Equivalencia COLD vs. RESTORED en NEW GAME: ambas rutas llegan al mismo
  bloqueador preexistente de pantalla negra, con lecturas de CD
  idénticas byte a byte (`lba`/tamaños/`first32`); sin divergencia de
  restauración antes de ese bloqueador conocido. Clasificación:
  `DEVSTATE_COLD_RESTORE_EQUIVALENT`.
- Audio: el silencio de audio es una condición **preexistente del
  proyecto** (confirmado por el usuario: nunca se ha oído audio en
  ninguna sesión, ni en frío, en todo el desarrollo), no una regresión de
  DEVSTATE — mismo patrón de decodificación PCM en ambas rutas. Ver
  memoria de proyecto `project_dmc_audio_silent_baseline.md` (fuera de
  este repositorio, en el sistema de memoria del asistente) para evitar
  que una futura fase lo reinvestigue como si fuera nuevo.

## 7. Qué queda explícitamente fuera (no se tocó, no se resolvió)

- El bloqueador de pantalla negra post-NEW GAME (preexistente, boundary
  real de GAMEPLAY_001).
- El diagnóstico `INPUT_001` en `Pad.cpp` — sigue como cambio local no
  versionado en el checkout de vendor, pendiente de su propia decisión
  de promoción o retiro (no se discutió ni se decidió en esta fase).
- El silencio de audio real (causa raíz no investigada aquí).

## 8. Commits de esta fase

- Commit de producción (`patches/DEVSTATE_003_main_menu_checkpoint_v1.patch`,
  `upstream.lock.json`, `patches/README.md`, `.gitignore`): ver
  `git log` — mensaje `feat: add main menu development checkpoint`.
- Commit de documentación (notas `DEVSTATE_00*` + esta nota de
  promoción): ver `git log` — mensaje
  `docs: checkpoint validated devstate cold load`.

No se commiteó ningún artefacto local: `*.dmck` (añadido a `.gitignore`),
`analysis/local/**` (ya ignorado), logs, builds, ISO, memory cards.
