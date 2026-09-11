# Cambios generales de runtime

Guardar aquí parches revisables contra el commit de upstream.lock.json si
un bloqueo demuestra un fallo general. Cada parche debe documentar causa,
reproducción, validación y commit base.

## Convención de archivos

- `NOMBRE.patch`: diff puro, aplicable directamente con `git apply`
  (sin comentarios ni encabezados humanos — un comentario `#` antes del
  primer `diff --git` rompe `git apply`).
- Cada patch debe tener documentación asociada (causa, reproducción,
  fix, validación), en una de estas dos formas:
  - **A) companion `NOMBRE.md`** junto al patch, en este mismo
    directorio (patrón original, usado por `BLOCKER_002_cdmodule_service`); o
  - **B) una nota técnica ya existente bajo `analysis/notes/`**,
    referenciada explícitamente desde la tabla de "Patches activos" más
    abajo — usado cuando el patch agrupa trabajo ya cubierto en detalle
    por informes de investigación existentes, para no duplicar
    documentación extensa.

No crear un nuevo `.md` de patch que solo repita/resuma un informe de
`analysis/notes/` ya existente — enlazar directamente ese informe.

## Mecanismo de aplicación (`scripts/pipeline.py bootstrap`)

`upstream.lock.json` declara `patches[]` (lista ordenada de
`{path, sha256}`, rutas relativas a la raíz del proyecto),
`patch_identity` (autor/fecha/mensaje fijos) y `patched_commit` (el
commit sintético esperado). `bootstrap()`, solo cuando clona
`vendor/PS2Recomp` desde cero:

1. fija `core.autocrlf=false` en el clon antes de cualquier checkout
   (necesario para que el resultado sea idéntico bit a bit
   independientemente de la configuración global de git del host);
2. hace checkout del commit baseline real (`commit`, recuperable del
   remoto);
3. verifica SHA256 de **todos** los patches antes de tocar el árbol;
4. aplica cada patch en orden (`git apply --check` + `git apply`; ante
   cualquier fallo, revierte al baseline limpio con `git reset --hard`
   y aborta explícitamente — nunca deja baseline + un subconjunto de
   patches aplicado);
5. genera un commit determinista vía `git commit-tree` (autor,
   committer, fecha y mensaje fijos en `patch_identity`; sin rama, sin
   hooks, sin depender de `user.name`/`commit.gpgsign` del host) y
   verifica que su hash coincide exactamente con `patched_commit`;
6. deja `HEAD` detached en `patched_commit` (mismo modelo que el
   checkout de la baseline pura: sin rama local).

`upstream()` (invocado antes de `build`/`run`) verifica en O(1) que
`HEAD == patched_commit` (o `== commit` si `patches[]` está vacío) y que
el working tree está limpio — sigue detectando cualquier drift
accidental exactamente igual que antes, solo que el commit "reconocido"
ahora es baseline+patchset en vez de baseline puro. `commit` (el
baseline real) nunca se sustituye por un commit sintético: ambas
identidades quedan separadas en el lock.

Detalle completo del algoritmo, verificación de determinismo (CRLF,
reproducibilidad, dos clones independientes desde GitHub) y migración:
`analysis/notes/BLOCKER_002_indirect_jump_top_of_ram.md`, FASE K.

## Patches activos

Orden de aplicación = orden declarado en `upstream.lock.json: patches[]`
(cada uno se aplica sobre el resultado del anterior; ver mecanismo
arriba). Los cuatro patches actualmente activos:

1. `BLOCKER_002_cdmodule_service.patch` (convención A — companion
   local): servicio HLE mínimo de lectura de CD para Devil May Cry
   (`SLES_503.58`). Documentación: `BLOCKER_002_cdmodule_service.md`
   (este directorio), causa/validación completas también en
   `analysis/notes/BLOCKER_002_indirect_jump_top_of_ram.md`.

2. `BLOCKER_003_cdmodule_movie_streaming.patch` (convención B): HLE de
   las RPCs de CDMODULE.IRX para arranque/transferencia por
   bloques/cierre/estado de reproducción de películas (PSS),
   `fno=9/0xA/0xC/0xD`. Documentación:
   `analysis/notes/BLOCKER_003_cdmodule_movie_streaming.md`.

3. `BUILD_ffmpeg_private_link_scope.patch` (convención B, patch de
   build sin blocker asociado): cambia el link scope de FFmpeg en
   `ps2xRuntime` de `PUBLIC` a `PRIVATE` en `CMakeLists.txt`, requerido
   para que el build reproducible con FFmpeg activo (`PS2X_HAS_FFMPEG`)
   no propague símbolos/includes de FFmpeg a consumidores que no los
   necesitan. Diff de 13 líneas, autoexplicativo; sin nota dedicada.

4. `BLOCKER_004_runtime_p37_p312.patch` (convención B): agrupa el
   trabajo de runtime acumulado y validado de P3.7 a P3.12 para
   BLOCKER_004 — separación y endurecimiento del `RuntimeGuestArena`/
   heap guest, asignación de pila de callbacks asíncronos, soporte
   MPEG/audio ya requerido por el pipeline, y el fix de orden de lotes
   de invocaciones pendientes del scheduler (P3.12). Incluye
   instrumentación de diagnóstico marcada explícitamente como temporal
   (`temporary`, `Remove once BLOCKER_004 is closed`) — deliberadamente
   sin retirar todavía: este patch representa el runtime exacto usado
   para las validaciones P3.11/P3.12; su limpieza será una fase
   separada. Documentación (no duplicada aquí, ver cada informe):
   - `analysis/notes/BLOCKER_004_pss_video_output.md` — historia
     canónica y acumulativa de BLOCKER_004.
   - `analysis/notes/BLOCKER_004_P36_HEAP_COLLISION_AUDIT.md` — auditoría
     de colisión de heap en runtime (P3.6).
   - `analysis/notes/BLOCKER_004_ASTRA_P311_OVERNIGHT.md` — contrato de
     `sceMpegInit`, loop de reproducción autónomo (P3.11).
   - `analysis/notes/BLOCKER_004_FABLE_P3111_CAUSAL_AUDIT.md` —
     auditoría causal adversarial que localizó la inversión de orden de
     lotes del scheduler (P3.11.1).
   - `analysis/notes/BLOCKER_004_P312_SCHEDULER_BATCH_ORDER.md` — diseño,
     implementación y validación byte-exacta del fix de orden de
     invocaciones pendientes (P3.12).
