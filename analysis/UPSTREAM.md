# Inspección de upstream

Revisión inspeccionada: `14b1e5cb39b4af7e6fc12f9a29fdc751efde49d7`.
Wiki: `19913d78ba89adec620d6571856a76c8c9a8aa14`.

## Compilar herramientas y runtime

El CMake raíz exige 3.21 (README dice 3.20). Herramientas: configurar la raíz
upstream con runtime/test/studio OFF; compilar targets `ps2_recomp` y
`ps2_analyzer`. Se usa una build separada porque ps2xRecomp incluye
`${CMAKE_SOURCE_DIR}/ps2xRuntime/cmake/ReleaseMode.cmake`.

El CMake propio incorpora upstream solo con runtime ON, reutiliza
`ps2EntryRunner`/`ps2_runtime`/`ps2_iop`, excluye la tabla vacía de ejemplo y añade
los C++ y headers de la salida externa. No copia archivos dentro de vendor.
La biblioteca IOP también tiene alias `ps2x::iop`.

Dependencias de herramientas en el código: ELFIO Release_3.12, toml11 v4.4.0,
fmt 12.1.0, libdwarf v2.2.0 y rabbitizer 1.14.3. Runtime usa raylib 5.5;
UI/FFmpeg opcionales quedan desactivados inicialmente. Las dependencias upstream
usan tags: el lock fija el proyecto principal, no garantiza builds bit a bit.

## Ghidra y configuración

README recomienda ExportPS2Functions para retail/stripped; analyzer es fallback.
El enlace Ghidra Workflow del README actualmente redirige a la portada de wiki.
Se inspeccionó el exportador real: pide TOML y CSV con dos `askFile`, recorre
funciones y etiquetas ejecutables, exporta `Name,Start,End,Size` con End exclusivo.
El TOML añade `ghidra_export` con estadísticas, `general.untracked_stubs` y
clasifica nombres contra una lista de handlers embebida. Revisar esas decisiones.

Campos relevantes: `input`, `output`, `ghidra_output`, `single_file_output`,
`low_memory_mode`, `output_worker_threads`, `patch_syscalls`, `patch_cop0`,
`patch_cache`, `stubs`, `untracked_stubs`, `skip` bajo `[general]`;
`instructions` bajo `[patches]`. La plantilla local no contiene direcciones.

La wiki aún habla de `registerAllFunctions()`, pero esta revisión genera
`g_ps2RecompiledFunctionTable` y un inicializador estático en
`register_functions.cpp`. El runtime consume esa tabla; mantener la vacía del
checkout conduciría a fallos de dispatch. El wrapper excluye expresamente esa fuente.

## Game Overrides

Header `ps2xRuntime/include/game_overrides.h`:
`PS2_REGISTER_GAME_OVERRIDE(name, elfName, entry, crc32, applyFn)`.
`applyFn` recibe `PS2Runtime&` durante loadELF. `runtime.registerFunction(address, fn)`
permite reemplazos; firma `void(uint8_t*, R5900Context*, PS2Runtime*)`.
`bindAddressHandler(runtime, address, handlerName)` enlaza un handler existente.
Un handler enlazado directamente puede dejar PC intacto: el wrapper debe resolver
el retorno a RA cuando corresponda para evitar redispatch infinito.

En esta API entry=0 o CRC=0 desactivan esos filtros: no usar ceros ficticios para DMC.
CRC es CRC-32/IEEE sobre el ELF completo. Guardar además SHA-256 en la evidencia.
No se ha registrado ningún override, dirección ni función de DMC.

## Perfiles IOP

`ps2xIOP` implementa servicios HLE RPC/DMA; no ejecuta IRX ni la CPU R3000A.
Sin perfil coincidente mantiene sus servicios core; no existe un perfil DMC en
el catálogo inspeccionado. No se puede inferir compatibilidad por ser otro juego
de Capcom. Comparar primero los protocolos reales.

Para mantener el perfil DMC fuera de vendor, la ruta prevista es plugin nativo:
habilitar `PS2X_IOP_ENABLE_PLUGINS=ON`, crear biblioteca SHARED que incluya
`ps2xIOP/include/ps2x/iop/plugin_api.h`, exportar con enlace C
`ps2x_iop_query_v1(uint32_t, ps2x_iop_plugin_api_v1*)` y colocar la DLL en
`iop_plugins/` junto al ejecutable. El descriptor v1 necesita ID, matcher, al
menos un SID y callbacks create/destroy/reset/handle_rpc; el matcher debe
restringir nombre, entry y CRC. Los SIDs deberán salir de trazas/análisis DMC.

Validar ABI/struct_size, usar callbacks del host para memoria/archivos/audio,
no cruzar STL, excepciones o punteros crudos de memoria guest por la ABI.
Registrar semántica de completado RPC, callbacks, semáforos y transferencias.
No crear un plugin de éxito ficticio para ocultar SIDs desconocidos.
El ejemplo completo está en `vendor/PS2Recomp/ps2xIOP/PluginExample.md`.

**Nota (FASE J/K, BLOCKER_002)**: el servicio HLE de lectura de CD para
DMC (`CdModuleService`) se implementó directamente dentro de vendor
(`builtin_profiles.cpp`/`module_factories.h`/`modules/cdmodule.cpp`),
no como plugin nativo — se desvía de la ruta prevista arriba. Motivo:
alcance de la tarea era cerrar el bloqueo con la modificación mínima
posible sobre la arquitectura ya existente en `ps2xIOP` (mismo patrón
que `ClFileService`/`SdrdrvService`, ya dentro de vendor), no diseñar
el plugin. Para mantener esto reproducible sin vendor dirty permanente,
el cambio vive como parche versionado (`patches/`) que
`scripts/pipeline.py bootstrap` aplica automáticamente sobre la
baseline fijada — ver `patches/README.md` y
`analysis/notes/BLOCKER_002_indirect_jump_top_of_ram.md` FASE K. Migrar
esto a un plugin nativo sigue siendo una opción futura si se prefiere
mantener vendor sin ningún diff.

## Fuentes

- https://github.com/ran-j/PS2Recomp (README y código del commit fijado).
- https://github.com/ran-j/PS2Recomp/wiki/PS2Recomp-Stripped-Game-Walkthrough-For-LLMs
- https://github.com/ran-j/PS2Recomp/wiki/Game-Override-Hooks
- https://github.com/ran-j/PS2Recomp/tree/14b1e5cb39b4af7e6fc12f9a29fdc751efde49d7/ps2xIOP
- https://nio03.github.io/unricopie/ es un catálogo de recompilaciones; sirve
  de contexto, no de documentación técnica de PS2Recomp ni de prueba de soporte DMC.
