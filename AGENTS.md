# DMC bring-up

Objetivo activo: M0–M2 de Devil May Cry (2001), PS2, con PS2Recomp.
No afirmar viabilidad del juego completo ni hitos sin evidencia de ejecución.
No inventar direcciones, región, funciones, SIDs ni identidad del ELF.
Asociar cada binding/parche al SHA-256 del ELF y a su nombre, entry y CRC32.
Código generado en recomp/generated; ajustes DMC en runtime; cambios generales
al upstream como parches documentados en patches, sin mezclar ambas cosas.
No editar C++ generado a mano. Mantener vendor limpio y revisar upstream.lock.json.
No añadir originales, assets ni código generado a Git.
Los retornos ficticios solo sirven para diagnóstico y requieren una nota con
evidencia, efecto observado y condición para retirarlos.
Leer README.md y analysis/UPSTREAM.md antes de continuar.
