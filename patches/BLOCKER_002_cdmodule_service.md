# Parche: servicio HLE mínimo para CDMODULE.IRX (BLOCKER_002)

Archivo de patch: `BLOCKER_002_cdmodule_service.patch` (diff puro,
aplicable con `git apply`; ver `upstream.lock.json` para su SHA256 y el
commit baseline sobre el que se aplica).

## Causa

Ver `analysis/notes/BLOCKER_002_indirect_jump_top_of_ram.md` (FASE
G-J) para el desarrollo completo. Resumen: el juego registra un
servidor SIF RPC real (`sid=0x12345678`, módulo
`CDMODULE.IRX`/"CdRead_Module") para leer datos de disco (`fno=2`).
`ps2xIOP` no tenía ningún perfil para `SLES_503.58`, así que esa RPC
caía en el fallback genérico "unhandled", que limpia el flag `busy` sin
copiar bytes reales. `GetCardInfo` (EE) esperaba datos en `0x01E00000`
que nunca llegaban, y el cursor de `Print_message` avanzaba sin límite
hasta corromper la jump table en `0x586740` (causa raíz original de
BLOCKER_002, FASE C-E).

## Reproducción (antes del parche)

`sid=0x12345678`/`fno=2` aparece en `[IOP/RPC trace:unhandled]`
(`PS2X_ENABLE_IOP_RPC_TRACE`, ON por defecto); `0x01E00000` permanece
vacío; `Print_message` entra en runaway (FASE C/D).

## Fix

Nuevo `IopService` (`CdModuleService`,
`ps2xIOP/src/modules/cdmodule.cpp`) registrado solo para el perfil
`SLES_503.58` (`builtin_profiles.cpp`), que atiende únicamente `fno=2`.
Consume `resourceId`/`LBA`/`size`/`dest`/`mode` directamente del
paquete de 112 bytes que el propio juego ya resuelve vía su tabla
interna `0x507C50` (FASE H.4/H.5) — no se duplica esa tabla ni se
hardcodea ningún `resourceId`/`LBA`/tamaño. Lee los bytes reales desde
una imagen de CD host (`IopHost::hostPath(HostPathKind::CdImage)` +
`openHostFile`/`readHostFile`, mismo patrón que `sdrdrv.cpp`) y los
escribe en RAM EE vía `IopHost::writeGuest`. `fno!=2` (p. ej. `fno=1`,
init de `Cd_init`) se deja sin manejar deliberadamente, igual que antes
del parche.

Complemento genérico (no específico de DMC): `ps2xRuntime/src/main.cpp`
lee la variable de entorno opcional `PS2X_CD_IMAGE` y, si está
presente, la vuelca a `PS2Runtime::IoPaths::cdImage` — ese campo ya
existía pero no estaba conectado a ningún CLI/config.

## Validación (FASE J.3)

Build incremental (sin regenerate, sin tocar `recomp/generated`).
Ejecución real con `PS2X_CD_IMAGE` apuntando a la ISO verificada (SHA256
del ELF embebido coincide con `original/SLES_503.58`, FASE F.0):
`sid=0x12345678`/`fno=2` deja de aparecer como unhandled; 7 peticiones
reales de recursos distintos servidas correctamente (`resourceId`
`0x5c`/`0x7d`/`0x99`/`0x113`/`0x13a`/`0xb8`/`0xa4`), con LBA/tamaño
coincidentes con la tabla ELF `0x507C50` y contenido verificado byte a
byte contra una lectura independiente del ISO. Sin crash/excepción en
145 s combinados de ejecución; el proceso llega a un estado estable
(polling normal de memory card) muy por delante del punto donde ocurría
BLOCKER_002. Detalle completo, incluyendo limitaciones conocidas
(alcance limitado a `fno=2`; `resourceId=0x9C` no se reprodujo en
ejecución automática), en la nota FASE J/K.

## Integración reproducible (FASE K)

Este parche forma parte del `patchset` reconocido por
`upstream.lock.json` (`patches[]`). `scripts/pipeline.py bootstrap` lo
aplica automáticamente sobre el commit baseline fijado (`commit`) y
genera un commit local determinista (`patched_commit`, mismo árbol,
autor/fecha/mensaje fijos) — ver `upstream.lock.json` y
`analysis/notes/BLOCKER_002_indirect_jump_top_of_ram.md` FASE K para el
algoritmo exacto. No requiere ningún commit remoto ni fork: se deriva
siempre de `commit` + este archivo.
