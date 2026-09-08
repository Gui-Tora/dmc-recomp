# Cambios generales de runtime

Guardar aquí parches revisables contra el commit de upstream.lock.json si
un bloqueo demuestra un fallo general. Cada parche debe documentar causa,
reproducción, validación y commit base.

## Convención de archivos

Cada patch son DOS archivos:

- `NOMBRE.patch`: diff puro, aplicable directamente con `git apply`
  (sin comentarios ni encabezados humanos — un comentario `#` antes del
  primer `diff --git` rompe `git apply`).
- `NOMBRE.md`: documentación (causa, reproducción, fix, validación),
  enlazando a la nota de investigación completa si existe una.

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

- `BLOCKER_002_cdmodule_service.patch` / `.md`: servicio HLE mínimo de
  lectura de CD para Devil May Cry (`SLES_503.58`). Ver
  `BLOCKER_002_cdmodule_service.md` para causa/validación completas.
