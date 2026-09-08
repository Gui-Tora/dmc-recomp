DMC bring-up

Objetivo activo: M0–M2 de Devil May Cry (2001), PS2, con PS2Recomp.

No afirmar viabilidad del juego completo ni hitos sin evidencia de ejecución.

No inventar direcciones, región, funciones, SIDs ni identidad del ELF.

Asociar cada binding/parche al SHA-256 del ELF y a su nombre, entry y CRC32.

Código generado en recomp/generated; ajustes DMC en runtime; cambios generales
al upstream como parches documentados en patches, sin mezclar ambas cosas.

No editar C++ generado a mano.

Mantener vendor limpio y revisar upstream.lock.json.

No añadir originales, assets ni código generado a Git.

Los retornos ficticios solo sirven para diagnóstico y requieren una nota con:

evidencia;

efecto observado;

condición para retirarlos.

Leer README.md y analysis/UPSTREAM.md antes de continuar.

Trazabilidad de investigación

Antes de comenzar una nueva fase de investigación:

Ejecutar git status.

Revisar si analysis/notes/ contiene resultados ya cerrados de la fase anterior.

Si hay resultados cerrados:

revisar git diff;

hacer commit solo de documentación y evidencia permanente relevante;

no incluir temporales, logs grandes, builds, vendor dirty ni experimentos,
salvo que se haya decidido explícitamente conservarlos.

Comenzar la nueva fase solo después de dejar un checkpoint claro del estado anterior.

Durante una fase se puede editar analysis/notes/ libremente.

Al cerrar una fase con un hallazgo significativo, crear un commit de checkpoint
antes de comenzar la siguiente.

No hacer un commit por cada pequeña edición de Markdown.

Cada commit debe representar un checkpoint investigativo reproducible.

Los mensajes de commit deben describir el hito investigativo. Ejemplos:

docs: identify OPMOJI_G.T32 as resource 0x9C

docs: locate blocker 002 divergence at unhandled IOP RPC

docs: document CD module RPC protocol

Si una hipótesis previa queda refutada, no borrar ni reescribir el razonamiento
histórico para que parezca que siempre se conocía la respuesta.

Documentar explícitamente:

cuál era la hipótesis;

qué evidencia la apoyaba;

qué nueva evidencia la refutó;

cuál es el estado actualizado.

Builds

No hacer regenerate salvo que sea necesario.

No hacer clean build ni full build salvo que sea inevitable y quede
documentado el motivo.

Preferir siempre builds incrementales mínimos.

Antes de modificar una cabecera compartida por el código generado, comprobar
si el cambio provocaría recompilar los miles de .cpp de recomp/generated.

Para instrumentación o diagnóstico, preferir cambios aislados en:

runtime;

archivos .cpp no compartidos;

hooks específicos;

mecanismos que no obliguen a recompilar todo el código generado.

No lanzar una recompilación completa únicamente para sincronizar un ejecutable
si la investigación puede continuar con análisis estático o instrumentación
incremental.

Evidencia

Separar claramente en las notas permanentes:

HECHO: observado directamente o demostrado por evidencia primaria.

INFERENCIA: conclusión que se sigue razonablemente de hechos demostrados.

HIPÓTESIS: explicación posible todavía no demostrada.

No presentar una hipótesis como hecho.

Priorizar evidencia primaria:

bytes del ELF;

.symtab;

código generado por PS2Recomp;

Rabbitizer;

observaciones runtime;

logs reproducibles;

memoria guest;

PCSX2 como ejecución de referencia;

comparaciones binarias exactas.

Usar Ghidra para reversing, navegación y xrefs, pero no tratar sus límites de
función como fuente de verdad cuando contradigan .symtab o el ELF real.

No implementar fixes antes de localizar la primera divergencia objetiva,
salvo que el usuario lo pida explícitamente.

Cuando exista una comparación entre ejecución correcta y RECOMP, buscar la
primera diferencia observable y no perseguir síntomas posteriores como causa
raíz.

La conclusión de una fase comparativa debería poder expresarse de forma
concreta, por ejemplo:

PCSX2 y RECOMP coinciden hasta X. La primera divergencia ocurre en Y:
PCSX2 hace A y RECOMP hace B.

Diagnóstico

La instrumentación temporal debe ser:

mínima;

acotada al problema actual;

fácil de retirar;

documentada.

No convertir diagnósticos temporales en fixes permanentes sin evidencia.

No parchear valores concretos, destinos de salto, punteros, buffers o retornos
solo para hacer avanzar la ejecución, salvo como experimento diagnóstico
explícito.

Si se usa un comportamiento ficticio para diagnóstico, registrar:

por qué se añadió;

qué hipótesis prueba;

qué resultado produjo;

cuándo debe retirarse.

Después de cerrar una fase importante, preservar la evidencia necesaria antes
de revertir instrumentación temporal.

PCSX2 como oráculo

PCSX2 puede utilizarse como ejecución de referencia para observar qué hace el
juego original cuando el comportamiento del RECOMP sea ambiguo.

No considerar esto un sustituto del análisis: usarlo para establecer
comportamiento observable correcto y después localizar la primera divergencia
en el RECOMP.

Cuando se compare PCSX2 contra RECOMP:

utilizar el mismo ELF y la misma revisión del juego;

fijar PCs equivalentes siempre que sea posible;

registrar argumentos, memoria y estado relevantes;

distinguir observación directa de interpretación;

preferir dumps binarios exactos frente a transcripción manual.

PINE puede utilizarse para lecturas de memoria y dumps cuando sea útil.
Breakpoints, registros y stepping deben obtenerse con las herramientas
apropiadas del debugger.

Upstream y precedentes

Antes de implementar comportamiento nuevo en PS2Recomp/runtime, comprobar si:

existe un fix upstream posterior al commit fijado;

otro proyecto basado en PS2Recomp resolvió un problema equivalente;

existe comportamiento documentado en PCSX2 o ps2sdk que permita validar la
semántica.

Los precedentes externos sirven como pistas metodológicas, no como prueba de que
DMC tenga la misma causa raíz.

No aplicar ciegamente commits upstream ni copiar soluciones de otros juegos sin
demostrar su aplicabilidad a este ELF y a esta ruta concreta.

Vendor y cambios permanentes

Mantener vendor/PS2Recomp en el commit baseline definido por el proyecto salvo
que se decida explícitamente actualizarlo.

Los cambios generales al runtime/upstream deben:

estar justificados;

estar separados de ajustes específicos de DMC;

quedar documentados;

poder compararse contra el baseline.

Los cambios específicos de Devil May Cry deben permanecer fuera de vendor
cuando sea razonablemente posible.

Antes de dejar vendor dirty al cerrar una fase:

revisar los cambios;

decidir cuáles son temporales;

preservar patches o snapshots si contienen evidencia útil;

revertir lo que no deba permanecer.

Estado histórico

No borrar errores metodológicos si fueron relevantes para llegar al resultado.

Si durante la investigación se descubre que:

una arquitectura Ghidra era incorrecta;

una semántica de opcode estaba mal interpretada;

un terminador no era realmente un terminador;

una instrumentación tenía un bug;

una hipótesis de archivo o recurso era incorrecta;

documentar la corrección y conservar el contexto suficiente para entender cómo
se llegó al nuevo resultado.

El objetivo de las notas no es aparentar una investigación lineal perfecta,
sino conservar una cadena de evidencia reproducible.