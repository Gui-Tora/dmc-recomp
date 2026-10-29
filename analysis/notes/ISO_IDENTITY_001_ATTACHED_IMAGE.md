# ISO_IDENTITY_001 — Identidad de la imagen adjunta

**Fecha:** 2026-10-29  
**Alcance:** verificar estáticamente la identidad de la ISO adjunta y del ELF de arranque. No se ejecutó el juego ni se extrajeron assets al repositorio.

## Evidencia

**HECHO (lectura directa de la ISO):**

- La imagen es un ISO9660 de `4,698,767,360` bytes, etiqueta de volumen `DEVIL MAY CRY`.
- SHA-256 de la imagen completa: `20a1b6a2b43d5e72ed4a334740e54c48bf5a91753160334a28af2719276807c8`.
- El descriptor primario de volumen está en LBA 16 (`CD001`, versión 1). Su directorio raíz comienza en LBA `0x105`, tamaño `0x146` bytes.
- El archivo raíz `SYSTEM.CNF;1` ocupa LBA `0x106A`, tamaño `0x3A` bytes. Contenido:

  ```text
  BOOT2 = cdrom0:\SLES_503.58;1
  VER = 1.02
  VMODE = PAL
  ```

- El ELF raíz `SLES_503.58;1` ocupa LBA `0x380`, tamaño `0x674AA4` bytes. Su identidad calculada sobre el archivo completo es:
  - SHA-256: `d0753a6b3b2f00802a50758a872d8cf051725aa31c839aa8894eee30ce58bab4`
  - CRC32/IEEE: `0x77654AD2`
  - `e_entry`: `0x00100008` (ELF32, little-endian)
- La imagen y el ELF se leyeron desde la ruta de adjuntos; no se guardó una copia del ELF ni de la ISO en el árbol del repositorio.

**INFERENCIA:** la ISO adjunta corresponde a la misma identidad PAL Europa, v1.02, que el ELF canónico ya registrado en [GAMEPLAY_CURRENT_STATUS.md](GAMEPLAY_CURRENT_STATUS.md) §1: coincide el nombre de arranque y el SHA-256 completo del ELF, además de entry y CRC32. Esto verifica identidad de los bytes examinados; no demuestra autenticidad física del disco ni viabilidad del juego completo.

## Reproducción

Con sectores de 2048 bytes, leer el PVD en LBA 16, verificar tipo 1 / `CD001` / versión 1 y recorrer el directorio raíz indicado en el registro de directorio del PVD. Resolver `SYSTEM.CNF;1` y `SLES_503.58;1` por sus registros ISO9660, leer sus extensiones y longitudes exactas, y calcular SHA-256 sobre la ISO y el ELF, CRC32/IEEE sobre el ELF. El `e_entry` se lee del campo ELF32 en offset `0x18`.

El parche adjunto `GAMEPLAY_001L_sif_rpc_iop_server_handles.patch` no se inspeccionó ni aplicó en esta verificación.
