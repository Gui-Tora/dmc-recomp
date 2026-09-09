# BLOCKER_002 — Astra independent audit

## 1. Veredicto ejecutivo

**CONFIRMADO CON RESERVAS**, limitado a la causa de BLOCKER_002 y a la lectura simple `fno=2, request.mode=1` hacia RDRAM. No confirma la equivalencia general de CdModuleService con CDMODULE.IRX ni la solidez completa del pipeline.

La ausencia de un servicio para la lectura de CD explica causalmente el buffer vacío y el runaway observado. El servicio nuevo introduce precisamente la copia ausente. La identificación del recurso y del protocolo se sostiene al volver a leer ELF, IRX e ISO; no depende de aceptar las notas anteriores. Sin embargo, falta la prueba completa de RAM del caso literal `0x9C`, existen diferencias demostrables en respuesta/modos y hay dos defectos reales de integración.

Hallazgo de mayor impacto funcional potencial: **el servicio acepta cualquier mode de fno=2 como copia cruda, pero el IRX usa mode para interpretar cabeceras y elegir transferencias EE/IOP/SPU**. Hay llamadores EE reales que solicitan mode=2. No se ha observado aquí su fallo en ejecución ni se ha investigado un bloqueo posterior.

Hallazgo de infraestructura más claro: **K.7 es falso**. Un vendor B limpio no invalida por sí solo un ejecutable construido con vendor A. El contraejemplo pasa las condiciones actuales de `launch()`.

Alcance y procedencia de esta auditoría, 2026-09-09:

- Principal: `75fc0cb`; `git status --short` y `git diff` inicialmente vacíos. No se ha creado ningún commit ni cambiado commits existentes. La restricción expresa de esta auditoría prevalece sobre el checkpoint automático de AGENTS.md.
- ELF `SLES_503.58`: 6.769.316 bytes, entry `0x00100008`, CRC32 `77654AD2`, SHA256 `d0753a6b3b2f00802a50758a872d8cf051725aa31c839aa8894eee30ce58bab4`. Recalculados; comparación exacta con el ELF contenido en la ISO. SYSTEM.CNF confirma VER=1.02 y VMODE=PAL.
- IRX: 33.815 bytes, SHA256 `9d51f16d1ed861e508daa1389e6aaf4fe597687b3f0cd5feb920e62a479b34fa`; copia local idéntica a `DATA/MODULES/CDMODULE.IRX` en la ISO, LBA `0x10EB`.
- Vendor: baseline `14b1e5cb39b4af7e6fc12f9a29fdc751efde49d7`, HEAD `3b0ce90ce06314a2b6c66a6fe1f6bf2460ebd1cd`, limpio. Tree `a944d581b82e731f392b09fd60eb2fb2d57e5073`, idéntico al de `5b2044e`.
- Ejecutable conservado: SHA256 `77475d576f920ca473807a058eddaa1e0738f28315d15732e66108a71befac98`, coincide con built.json y el hash documentado en J/K. Esto no constituye una nueva ejecución.
- Leídos los documentos obligatorios, patch, pipeline, fuentes del servicio/perfil, transporte RPC, host de archivos/memoria y partes pertinentes del API de plugins. No se usaron boundaries de Ghidra.
- Cero builds, regenerate, patches aplicados, ejecuciones del juego, modificaciones funcionales o cambios de vendor. Solo esta nota y scripts de auditoría bajo `analysis/local/astra_blocker002_audit/`.

Los scripts `inspect_binary.py`, `check_evidence.py` y `check_pipeline.py` son reproducibles con `python -B`. El primero muestra palabras originales y decodifica un subconjunto MIPS; **no es un desensamblador R5900 completo** y marca instrucciones desconocidas. Para MMI/delay slots se contrastó con el C++ generado. Se comprobaron todas las palabras citadas en comentarios de seis funciones generadas contra el ELF: Print_message (467), CardMesPrint (43), GetCardInfo (130), CallCdModule (252), CdFileRead (19), CdRead00 (5), todas coinciden. Eso verifica procedencia de instrucciones, no prueba automática de toda la traducción.

## 2. Cadena causal reconstruida

DEMOSTRADO designa bytes/código directamente comprobados o una observación conservada verificable. FUERTEMENTE SOPORTADO implica una combinación causal convincente con algún tramo sin observación independiente completa. Las capturas históricas se identifican como tales; no se presentan como nuevas mediciones.

| Paso | Evidencia primaria revisada | Clasificación | Observaciones |
| --- | --- | --- | --- |
| El JR toma `0x02000100` de una tabla alterada | ELF `0x2E11A4–0x2E11C8`, tabla `0x586740`; log de crash y dumps FASE A/E | DEMOSTRADO | Índice `a0-0x72`; `a0=0x7A` da 8, dirección `0x586760`. ELF contiene `0x2E1230`; runtime contiene `0x02000100`. No es un target legítimo ni un hueco de función. |
| Print_message escribe sobre la tabla | Logs VEH conservados FASE E, stack hasta Print_message y WRITE32; C++ generado | DEMOSTRADO | Primera escritura atribuida al `sw` guest `0x2E131C`. También hay escrituras posteriores de AddPrim sobre la estructura ya invadida. Algunos registros del primer VEH son de un contexto incorrecto: no se usan como prueba de s1. |
| El cursor crece y envuelve en PS2Recomp | Log comprimido FASE D; incremento guest `0x2E1544`; implementación de acceso RAM | DEMOSTRADO | Inicio `0x00981248`, último s1Before `0x02586848`: diferencia `667776*44`. 2.003.332 llamadas registradas, cero `add-prim-return-MISMATCH`. El valor de crash siguiente es `0x02586874`. El wrap es del runtime; no se extrapola a hardware. |
| El parser consume ceros como caracteres, sin terminar | ELF/tabla de controles `0x586770`, generated Print_message; entrada FASE D con s6=`0x1E00000`, char=0 | DEMOSTRADO | `0x0E` conduce a `0x2E1040`, que limpia sp+0xD0; `0x7E` salta el byte y vuelve al bucle. “Indefinidamente” significa runaway hasta el fallo, no una prueba matemática de bucle infinito. |
| La cadena deriva del recurso vacío | CardMesPrint, GetCardInfo; logs FASE E | FUERTEMENTE SOPORTADO | `tableBase + offset(messageId) - 4`, con offsets vacíos, da `0x1E00000`. La medición histórica de ~65 KiB casi a cero no prueba por sí sola que jamás se escribiera nada; sí prueba ausencia de datos válidos al consumirlos. |
| GetCardInfo pretende cargar ese recurso | ELF `0x2191C4`, `0x2191CC–0x219228`, CdRead00/CdFileRead/CallCdModule | DEMOSTRADO | Destino literal `0x1E00000`; selección de resourceId en `0x57B7E8`; espera CdReadCheck. El selector 4 produce `0x9C`; 0/1 producen `0xB8`. |
| El recurso 0x9C procede de OPMOJI_G.T32 | Entrada ELF, directorio ISO9660, bloque PCSX2 reconstruido y búsqueda exacta en ISO completa | DEMOSTRADO | Identidad física demostrada; la captura PCSX2 conservada cubre solamente 4096 bytes. |
| EE e IOP acuerdan SID y comando | Cd_init `0x1CE970–0x1CE978`; IRX main_func `0x1950–0x1974`; cdfunc | DEMOSTRADO | SID `0x12345678` construido con LUI/ORI en ambos lados. fno=2 llega a CdReadProcess. |
| Baseline completa la RPC sin hacer la lectura | Ausencia de ruta DMC en baseline; IopSubsystem::handleRpc; RPC.cpp; trazas unhandled históricas | DEMOSTRADO | Resultado sin manejar, eco/ceros al receive de 4 bytes, busy=false. Ninguna copia del recurso al destino. No hay prueba de un paquete runtime literal 0x9C en esas trazas; no confundir el 0x5C conservado con 0x9C. |
| El nuevo servicio elimina esa omisión | cdmodule.cpp, host adapter y log K con 8 operaciones/7 IDs | FUERTEMENTE SOPORTADO | Copia real y completa según flujo de código, prefijos de RAM observados; no se conservó un dump completo post-HLE del caso original. |
| Desaparece el mecanismo de corrupción original | Ausencia del patrón de crash en ejecución conservada y llegada de datos | FUERTEMENTE SOPORTADO | No se volvió a medir la tabla post-fix. No equivale a un HECHO de integridad de todos sus bytes. |
| El problema era R6/boundaries, AddPrim clobber o terminador 0x7E | Bytes, symtab, traza D y control flow | REFUTADO | Los errores metodológicos previos se conservan en las notas históricas. No son explicaciones vigentes. |

La formulación comparativa rigurosa es: **PCSX2 demuestra contenido del recurso después de su lectura; el RECOMP anterior consume un buffer sin ese contenido. La ruta estática y las trazas sitúan la omisión en el RPC de CD sin servicio.** No está demostrado que ambos ejecutasen idénticos argumentos/estado en cada PC anterior. En particular, los selectores difieren y la tabla G.2 mezclaba ejecución observada con deducción estática. “Primera divergencia” debe entenderse como la primera omisión localizada en esta cadena, no como una comparación temporal exhaustiva de ambas máquinas.

## 3. Hallazgos que confirman el trabajo previo

### Recurso 0x9C

Recalculado desde secciones ELF y registros de directorio ISO9660, sin montar la imagen:

```
0x507C50 + 0x9C*8 = 0x508130
entrada = {0x000E6AF7, 0x00010A60}
0xE6AF7 = 944887
944887 * 2048 = 0x7357B800
0x10A60 = 68192
dest + size = 0x01E10A60 < 0x02000000
size % 16 = 0; size % 2048 = 608
```

El archivo ISO `DATA/ETC/OPMOJI_G.T32` tiene exactamente ese LBA y tamaño. SHA256 completo: `a4fd5fae540295704994b6885872865a72d4f3ae1f1bc73b0c9152e847a6c1ab`.

El `.bin` citado en las notas no está presente en el directorio inspeccionado. Se reconstruyeron los 4096 bytes del `.hex.txt` conservado: SHA256 `47e5bcd5a5e44ba182c812de7f920091e97e52fe1b6a4e12a593e5adc2c1dbd7`. Búsqueda de los **4096 bytes completos** sobre toda la ISO: un único match, `0x7357B800`. La pérdida del archivo binario no impide esta verificación porque el hex conserva todos los bytes. La procedencia temporal/PC de la captura sigue siendo la registrada por el usuario en FASE F, no reobservada aquí.

CallCdModule carga los dos words de tabla en `0x1CEBD0/0x1CEBE4` y los escribe a `request+4/+8`. `0x1CEBF4/0x1CEBFC` transportan destino desde `CdFileRead` hasta `request+0x0C`. `0x1CEC00–0x1CEC0C` transportan el byte de mode con máscara `0x7F` a `request+0x14`. El campo resourceId se escribe realmente como **u16** en `0x1CEBBC`; `+2` puede contener otro campo en comandos distintos. Para las capturas examinadas la mitad alta es cero. El HLE lo lee como u32 solo para logging, sin usarlo para decidir la copia.

### IRX: cierre independiente de los tramos antes pendientes

Direcciones siguientes relativas al IRX, según su symtab; no son direcciones cargadas inventadas del IOP:

- `main_func@0x1930` registra cdfunc=`0x1828`, buffer RpcArg=`0x64BD0`, SID `0x12345678`. Tabla `0x2798`: entrada fno=2 → `0x18B4` → `CdReadProcess@0xBE8`.
- CdReadProcess escribe `request[0x16]=0` para fno=2, llama FileReadSuspend, StartThread(Thread_cd, request), PollSema/WaitSema(Sema_main), FileReadSignal, y retorna EE_TransSize. La llamada `0x22E8` es **FileReadSignal**, no sceSifCheckStatRpc; resuelto por symtab.
- `CdReadProc@0x7E0` abre con **request LBA** (`lw a0,4(s5)` en `0x820`). CDFileOpen almacena ese LBA base y pone posición relativa a cero. CDFileSeekL cambia esa posición; CDFileRead suma base+posición antes de sceCdRead.
- Para mode=1, crea una cabecera de trabajo: tipo 0 (el delay slot de `0x878` escribe cero incluso al tomar la rama), tamaño=request[8], siguiente tipo=-1. Destino inicial EE_TransAddr[0]=request[0x0C], EE_TransSize=0.
- La lectura física normal en `0xA30–0xA3C` solicita **0x60000 bytes al buffer IOP**, aun para un recurso pequeño. CDFileRead convierte a sectores con `(n+2047)>>11`; aquí son **192 sectores**. No es correcto describir el original como una lectura CD exacta de 68192 bytes o solamente 34 sectores.
- El tamaño entregado al callback se limita a `min(restante,0x60000)`. El jalr antes no localizado está en **`0xAB4`**, a través de `trans_process_tbl[type]` en `0x3AA8`. Tabla: tipo 0 → EE (`0x690`), 1 → IOP (`0x314`), 2 → SPU (`0x4C4`), 3 → EE (`0x690`).
- `trans_mem_to_ee@0x690` arma `dma@0x3B70`: src=buffer IOP, dest=EE_TransAddr[slot], size=`(a3+15)&~15`, attr=0; sceSifSetDma@0x23FC y espera sceSifDmaStat@0x2404. La progresión del destino utiliza **size redondeado**. EE_TransSize se acumula **antes** de enviar la DMA (`0x6EC`), no después de esperar como sugería I.4; el RPC espera a la terminación del hilo.
- Al terminar, CdReadProc cierra, señala evento y Sema_main, y sale. CdReadProcess devuelve el contador; cdfunc lo almacena en **ret@0x3B68** y devuelve su dirección. Esto cierra la respuesta real de la lectura simple: contador transferido redondeado, o error en rutas de fallo; no una convención inventada de cero.

Para 0x9C, **la intención EE y la ruta IOP estática predicen 68192 bytes hacia EE**, alineados a 16. El exceso de lectura CD queda en IOP. **Solo 4096 bytes se observaron en PCSX2**. La predicción estática no convierte el resto en transferencia runtime medida.

## 4. Contradicciones / errores encontrados

1. **K.7 refutado**: la identidad del checkout no identifica el vendor usado por el exe. Véase sección 7.
2. **“0 = success” no es el protocolo original de fno=2**. El IRX devuelve EE_TransSize. El HLE escribe cero. Diferencia de protocolo demostrada, sin fallo de GetCardInfo demostrado por ella.
3. **Mode no es decorativo**: mode&2 lee una cabecera desde disco; mode&8 activa selección/progresión de bloques y llamadas de sonido. “Limitado a fno=2” no basta como delimitación de soporte correcto.
4. **Validación byte a byte sobredescrita** en el documento del patch/J.4/K.0: la comprobación runtime detallada en J.3 es un prefijo de **32 bytes**, no 68192 bytes de RAM. Los hashes completos son de archivos/ISO, no dumps completos del HLE. El log disponible confirma eso.
5. **0xB8 y 0x9C tienen los mismos primeros 32 bytes**. Sus hashes completos son distintos (`e7ca2139b6bc76fc960d2813ca47ac760b4aed282649d0d19bed0d2e9b3303d5` para B8). El prefijo compartido no distingue un recurso de otro.
6. **CdRead00 mal formulado históricamente**: a2=0 produce 1; a2 distinto de cero produce exactamente 0x81, no `0x80|(a2|1)`. El segundo camino sobrescribe a2. La ruta original tiene mode RPC NOWAIT=1 y byte request.mode=1. Son dos campos distintos.
7. **Receive no es globalmente ignorado**: CallCdModule lee `0x87DAC0` y comprueba -1 en `0x1CEE24–0x1CEE3C`; CdFileRead lo desreferencia en `0x1CF110`. Background_movie_move compara el retorno con -1 en `0x2DCE88–0x2DCE8C`. No se ha probado que algún consumidor necesite el conteo exacto, pero sí se refuta “nadie lee la respuesta”.
8. **Ausencia de crash no demuestra input normal ni tabla intacta**. K corrigió la tabla, pero persisten frases de J/K que describen “polling normal/esperando input” como observación. El log demuestra polling, no el motivo de la espera ni la pantalla renderizada.
9. **Rollback ante cualquier fallo es una promesa excesiva**: write-tree puede lanzar una excepción después de apply/add sin llamar al rollback; los resultados de reset/clean tampoco se comprueban. Reproducido mediante fallo simulado, sin ejecutar operaciones Git mutantes.

Se mantienen los antecedentes R6, terminador 0x7E, VEH compartiendo página, LBA 944840, tamaño PCSX2 exagerado y SID inicialmente no demostrado. Esta nota añade correcciones sin reescribir el relato histórico.

## 5. Riesgos del HLE actual

Las etiquetas BUG DEMOSTRADO de esta tabla pueden referirse a una **diferencia de contrato demostrada estáticamente**, no necesariamente a un crash ya observado. El impacto runtime se explicita por separado.

| Riesgo / pregunta D | Severidad | Evidencia | Impacto | Experimento mínimo futuro |
| --- | --- | --- | --- | --- |
| D1–2: leer size frente a sectores | NO PROBLEMA para 0x9C/mode=1 | IOP lee 0x60000, callback limita a restante | Exceso CD no implica exceso EE por sectores; no hace falta reproducir read-ahead interno para esta lectura | Comparar destino completo en PC de completion |
| D3–5: DMA redondea a 16 | RIESGO FUTURO; divergencia condicional demostrada | `0x6C0–0x6E0`, progresión `0x774–0x778` | Size no múltiplo de 16 puede escribir hasta 15 bytes adicionales desde IOP; HLE los omite. 0x9C, B8 y tamaños del log son múltiplos de 16 | Dump con guard de 16 bytes y caso no alineado |
| D6–7: alias y validación | POSIBLE BUG ACTUAL fuera de destinos observados | normalizeGuestAddress → ps2ResolveGuestPointer; rangos desconocidos dejan phys=0, direcciones altas pueden envolver | No es validación de mapa hardware. Ej.: 0x02000100 normaliza a 0x100; 0x40000000 a 0. Para tamaño>1, guestRange suele rechazar el segundo ejemplo; para size=1 puede escribir RAM[0]. Cached/uncached habituales sí coinciden con el mapa del runtime | Casos unitarios del adaptador sin juego; luego alias observado en DMC |
| D8–9: otros destinos/modos en fno=2 | BUG DEMOSTRADO de cobertura del protocolo; alto | IRX mode&2/&8 y tabla de callbacks; CdBindRead manda mode=2 por CdFileRead | HLE escribe archivo crudo a EE donde el original interpreta bloques y puede transferir a IOP/SPU. Scratchpad no demostrado. No hay fallo observado de esta ruta en la auditoría | Comparación de una petición mode=2 ya emitida naturalmente, sin forzar avance |
| D10: ignorar resourceId para lectura | NO PROBLEMA en ruta simple | CDFileOpen usa request[4]; resourceId se usa para debug | No necesita duplicar tabla EE. Logging u32 puede incluir mitad alta de otro campo | Ninguno para 0x9C |
| D11: confiar en LBA/size | NO PROBLEMA para paquete simple válido; RIESGO FUTURO general | EE resuelve tabla; IRX puede interpretar cabecera/seek y estados | Límites de ISO protegen acceso host, no prueban semántica de todos los paquetes. SID/fno solos no identifican el subprotocolo | Captura mode/cabecera junto con LBA/size |
| D12: estado adicional | RIESGO FUTURO | EE_TransSize, RoutineNo, header, selección mode&8, fno=8 lee contador | Omitir hilos internos es admisible; omitir resultados consultables no queda validado | Observar consultas posteriores del mismo módulo |
| D13: fno=1 sin manejar | RIESGO FUTURO | CdInit guarda SdtAddr, asigna buffers, crea semáforos/hilos; retorna 1/-1 | El HLE independiente no necesita esos objetos IOP para una copia simple, pero eso no valida inicialización observable de sonido/otros comandos | Captura init/primera lectura/response; sin emular hilos por analogía |
| D14: respuesta cero | BUG DEMOSTRADO de protocolo; impacto actual no probado | CdReadProcess→EE_TransSize→ret; HLE zeroGuest | Conteo ocultado; GetCardInfo no usa el retorno de CdRead00. Otros llamadores y wrappers sí pueden leerlo | Capturar receive y retorno wrapper para modo blocking y NOWAIT |
| Errores de archivo y copia parcial | POSIBLE BUG ACTUAL si falla IO | El HLE vuelve unhandled; framework hace eco y completa; no rollback de chunks previos | Puede quedar RAM parcial y false completion como antes del fix. La frase “no regresión” no hace correcto el manejo de errores | Host simulado con short read y error en segundo chunk |
| Identidad de ISO | RIESGO FUTURO operativo | main acepta cualquier PS2X_CD_IMAGE; servicio solo valida rango | CHD, BIN crudo de 2352 o disco equivocado pueden dar bytes erróneos con status=ok si el rango cabe | Verificación de identidad/layout de imagen antes de medir |

No hay evidencia de bytes extra de sector relevantes para 0x9C: está alineado a DMA. Tampoco hay evidencia de que el juego use aliases no canónicos en esta petición. El modelo cache/TLB de hardware no se deduce del wrapping de PS2Recomp.

### Response/completion (parte F)

RPC.cpp marca busy antes de handleRpc; CdModuleService realiza readHostFile/writeGuest, escribe receive y retorna handled; finishCall procesa receive y completeClient limpia busy. **Para este servicio sin dispatch guest efectivo ni endFunction**, no se observa completion antes de la copia: ambas rutas, blocking y NOWAIT, se completan síncronamente dentro de SifCallRpc. Este orden es sólido para GetCardInfo.

La condición no es solo “el servicio no fija guestFunction”: el framework puede tomar `server->func` si existe y no se suprime. Asimismo, con endFunction válido programa callback mediante el scheduler, y completeClient puede esperar a ese callback. No se generaliza el camino simple a todos los clientes.

CallCdModule separa el bit 0x80 del argumento EE: sin bit elige RPC mode=1 (NOWAIT), con bit RPC mode=0. endFunction se pone a cero en la rama inspeccionada. **GetCardInfo usa NOWAIT** y después hace polling. La finalización instantánea evita una espera, pero no prueba equivalencia de timing/intercalado con otros hilos o DMA por bloques. No hay evidencia de una dependencia de timing en este caso.

Además, CallCdModule en la ruta NOWAIT escribe **1** en `0x87DAC0` después de SifCallRpc (`0x1CECDC–0x1CECE0`), sobrescribiendo el cero del HLE; en blocking conserva la respuesta. GetCardInfo descarta el retorno de CdRead00 y consulta busy. Por tanto no es el “0 = success” lo que justifica su avance. No se ha realizado análisis exhaustivo de todos los lectores indirectos de receive ni de todos los llamadores del ELF.

## 6. Auditoría FASE K

La arquitectura baseline recuperable + patch versionado + commit sintético comprobado es razonable. El patch actual corresponde a cinco archivos y 293 líneas añadidas; su SHA256 recalculado es `92ed7f8dd918a979014d59071a7340bf934e68d5caec3294a36848b1ca0c4778`. Commit/tree/parent coinciden con el lock y con el árbol humano de J. Se recalculó el hash Git del objeto commit desde sus bytes: `3b0ce90c...`. **No se repitieron clones/aplicaciones** por la restricción de esta auditoría; las dos pruebas de clon nuevo de K se aceptan como evidencia histórica, no como ejecuciones propias.

La comparación textual del patch con `git diff baseline patched_commit` detectó únicamente seis líneas de contexto vacías sin el prefijo espacio; normalizando exclusivamente esa representación, los diffs coinciden. No se detectó contenido funcional materializado diferente del patch. Como comprobación no mutante del riesgo CRLF, convertir sus saltos a CRLF en memoria da SHA256 `39e47f5a45981d53afbf079ba3e44b0b1b34286993379f4f1b5eb31cc7325894`, distinto del lock.

| Aspecto G | Resultado de la inspección |
| --- | --- |
| Patch faltante/corrupto | verify_patchset_hashes comprueba todos antes de aplicar. Falla explícitamente. Si vendor ya existe, bootstrap/build/run no llaman ese verificador: un patch eliminado o alterado no invalida automáticamente un checkout ya correcto. No garantiza disponibilidad futura del material de reconstrucción. |
| Parcialmente aplicable, varios patches y orden | Cada check/apply se hace en orden sobre el resultado anterior. Error reconocido llama reset/clean. No hay éxito silencioso con un subset si el hash final se verifica. No se comprobó una aplicación real multi-patch aquí. |
| Fallos fuera del camino esperado | write-tree usa check_output fuera de un try/finally de limpieza. KeyError de patch_identity después de apply/add también deja estado intermedio. El guard posterior bloquea vendor dirty, pero el bootstrap no es transaccional ante todo fallo. |
| CRLF en vendor | clone --no-checkout y core.autocrlf=false antes del checkout resuelven el caso documentado. Vendor local confirma false. |
| CRLF del patch en repo principal | **Hueco concreto de portabilidad**: no hay .gitattributes fijando LF para el patch; git ls-files muestra i/lf, w/lf, attr vacío. El sistema tiene autocrlf=true. Un checkout nuevo del principal puede convertir el patch a CRLF y fallar su SHA256 antes de llegar al vendor. Configurar solo vendor no protege el archivo fuente del patch. Falla cerrado, no aplica bytes erróneos. |
| File modes/symlinks | Los cinco archivos cambiados son 100644; no hay entradas 120000/160000 en el árbol vendor actual. No hay problema demostrado para este patch. core.filemode/core.symlinks y filtros importan al generalizar; el hash final detecta un árbol diferente, no asegura que toda plataforma pueda producirlo. |
| Submodules | Se inicializan antes del patch. No hay gitlinks actuales. Si un patch futuro cambiase gitlinks/.gitmodules, haría falta revisar actualización posterior; no se ha validado ese soporte. |
| Windows | Rutas se pasan como argumentos y patches absolutos; host CD usa _wfopen y _fseeki64, permite offsets >2 GiB. Entorno comprobado: Git 2.50.1.windows.1. No hay matriz de versiones/plataformas. |
| commit-tree | Identidad, fechas, parent y mensaje fijados; sin git commit/hooks. El SHA esperado protege contra divergencia. Configuraciones de encoding/filtros/formato Git no están todas neutralizadas; no se demuestra determinismo universal. Para el contenido actual, objeto/tree son correctos. |
| Dirty vendor | HEAD y status --porcelain detectan drift normal. Archivos ignorados, assume-unchanged/skip-worktree o configuraciones que oculten estado no quedan garantizados; no se encontró uso de estos como causa aquí. El guard no es O(1) en sentido algorítmico: status inspecciona estado del árbol. |
| Baseline existente | No se migra; bug H.2, véase siguiente sección. |
| Clone nuevo | Flujo normal correcto para el patch actual si la fuente del patch conserva LF. Fallos intermedios dejan dest existente; siguiente bootstrap no reanuda automáticamente el proceso. |

## 7. built.json / bootstrap findings

### H.1 — BUG REAL

Contraejemplo al razonamiento K.7:

```
build A → generated=G, exe=E_A, hash=H_A
lock/vendor cambian a B limpio, sin cambiar G ni E_A
upstream(): B == effective_commit() → acepta
generated(): G todavía coincide con su recibo → acepta
launch(): G == built.generated y sha(E_A) == H_A → acepta E_A
```

`prepared.json.upstream` guarda baseline, no effective vendor; prepared() tampoco lo contrasta con el lock actual. Nada enlaza el hash del exe a B. `check_pipeline.py` ejecutó launch real con sustituciones en memoria de IO/recibos/llamada final; upstream real comprobó B limpio y la llamada simulada aceptó A. **No se lanzó ningún exe y no se cambió el lock**. Es una prueba del guard, no de un build A/B materializado.

Guardar `vendor_commit=effective_commit()` **al terminar un build exitoso** y exigir igualdad en run cierra este contraejemplo, siempre que upstream valide el HEAD real y se rechacen recibos antiguos sin ese campo. Debe detectarse también si vendor cambió durante build. El campo por sí solo, sin validarlo en run, no sirve. Tampoco cubre cambios en runtime propio, CMake, opciones, dependencias o plugins: es suficiente para este hueco de identidad vendor, no un recibo universal de todos los inputs. No se implementó.

El exe actual coincide con el hash documentado en J/K y hay trazas CDMODULE coherentes. El bug del guard **no demuestra** que el exe actual esté stale.

### H.2 — BUG REAL de migración/usabilidad, no aceptación silenciosa

apply_patchset solo se llama dentro de `if not dest.exists()`. Con baseline existente limpio y lock con patches, bootstrap omite la migración y upstream falla. Prueba del flujo real con exists=True y side effects simulados: única operación `upstream`; ninguna aplicación.

Aplicación automática sobre baseline limpio es una política defendible **si se limita a HEAD exactamente igual al baseline, sin cambios ni archivos que puedan perderse**, verifica todos los patches antes, conserva estado anterior y aplica con aislamiento/fallo recuperable. No reutilizar ciegamente el helper actual: abort_and_reset usa `clean -fd`; en un checkout preexistente la obligación de no destruir trabajo es mayor. No auto-migrar árboles dirty, commits humanos o patchsets A arbitrarios. Ser idempotente en effective_commit y dejar error explícito en otros estados. La decisión no requiere modificar el baseline remoto ni una rama del usuario. No se implementó.

## 8. Upstream y precedentes

Consulta breve de fuentes públicas el 2026-09-09: la [página de commits de main](https://github.com/ran-j/PS2Recomp/commits/main/) obtenida durante la auditoría muestra `14b1e5c` como commit superior (18 de agosto de 2026). En esa vista **no aparecen commits posteriores al baseline**; el refactor IOP #170 y el scheduler #184 son anteriores. No se encontró una corrección posterior que sustituya CDMODULE.

Limitación explícita: falló la consulta de compare/API, y la petición de red ampliada por terminal fue abortada. La evidencia disponible es la página web consultada, no un git fetch/ls-remote nuevo ni revisión de todas las ramas/PRs. No se afirma ausencia universal de trabajo no integrado.

El [README upstream de ps2xIOP](https://github.com/ran-j/PS2Recomp/blob/main/ps2xIOP/README.md) describe servicios HLE y plugins nativos: no ejecuta IRX. Esto refuerza que cargar el nombre de CDMODULE no ejecutaba mágicamente su implementación original. Los servicios locales ClFile/Sdrdrv aportan precedente para IopHost/ISO, no el contrato de DMC.

Como precedente primario de alineación, [ps2sdk loadfile.c](https://github.com/ps2dev/ps2sdk/blob/master/ee/kernel/src/loadfile.c) redondea transferencias SIF y espera DmaStat. No se usa su dirección EE→IOP para demostrar por analogía la lectura DMC: la DMA IOP→EE y el redondeo de DMC se obtuvieron de su propio IRX.

DQ8/RE Outbreak se mantienen exclusivamente como antecedentes metodológicos. No fue necesario reconstruir sus fixes ni atribuirles un protocolo equivalente para decidir esta auditoría. La captura PCSX2 del **mismo ELF/disco** sí constituye evidencia DMC; relatos de otros juegos no.

## 9. Patch vs plugin

**Recomendación: mantener el patch por ahora; migrar a plugin cuando se amplíe el protocolo o haya un coste real de conflictos upstream.** No bloquear la validación de 0x9C por esa migración.

La copia ISO y las abstracciones host son genéricas. SID, layout, semántica de mode, response y matcher de ELF son específicos del módulo de este juego; un SID compartido no demuestra que otro juego use el mismo contrato. El servicio parametrizado actual no es todavía una abstracción genérica validada.

El API C v1 ya expone read/write_guest, normalización, host paths/archivos y resultado RPC: la migración de la lógica actual es viable con un adaptador pequeño. El coste adicional real es construir/distribuir la DLL, activación y descubrimiento, matcher exclusivo para evitar competir con builtin, reset/ABI y **registrar el hash de la DLL en la procedencia del build/run**. Sin esto, un plugin puede empeorar precisamente el problema de trazabilidad encontrado.

Upstream genérico tiene sentido para conectar configuración de imagen o mejorar validaciones comunes con pruebas. No propondría subir como servicio genérico una implementación que ignora modes aceptados. El patch actual es pequeño, tiene hash/tree reproducible y permite comparar con baseline; la deuda prioritaria es el contrato/recibo, no su ubicación estética.

## 10. Qué NO está demostrado todavía

- Transferencia completa de 68192 bytes de 0x9C en PCSX2 o bajo HLE; integridad post-fix de los 64 bytes de jump table; retorno acotado de Print_message en el caso literal original.
- Igualdad PC-a-PC de estados previos entre las sesiones PCSX2 y RECOMP. En particular, la ausencia de 0x9C automática se explica plausiblemente por el **selector**, sin necesidad de suponer que solo faltaba pulsar un botón. No se capturó el selector del nuevo run.
- Equivalencia de modes distintos de 1, status/count para todos los consumidores, fallos CD, aliases arbitrarios, caché/TLB, callback/timing, fno=1 y estado compartido con otros comandos.
- Que el polling final sea ejecución correcta esperando input, ni que M2 esté completo. No se audita viabilidad del juego completo.

### Intentos de falsación (parte K)

| Alternativa al fix causal | Resultado |
| --- | --- |
| Rebuild/timing/logging | No pueden explicar por sí solos que aparezcan prefijos exactos del disco; el código lee y escribe antes de loguear. No hay A/B del mismo exe con/sin ISO para excluir diferencias de scheduling en todo el avance. |
| Fallback accidental | El log diferencia fno=1 unhandled y fno=2 servido. El fallback solo afecta receive de 4 bytes; no puede suministrar el recurso a 0x1E00000. |
| ISO equivocada | La ISO disponible contiene exactamente el ELF/IRX identificado y el recurso en el LBA esperado. El exe no verifica identidad; el log no conserva hash completo de imagen. Esa limitación operativa persiste. |
| Datos no cero pero equivocados | Coincidencia exacta de todos los first32 conservados y LBA/tamaños con tabla: fuerte contra datos arbitrarios. Insuficiente para probar el resto de RAM. B8/9C comparten ese prefijo. |
| RAM stale/alias | No hay caché de recursos en CdModuleService: cada petición abre, lee y copia. Destinos observados canónicos y dentro de límites. No se ha medido todo el rango tras copia; fallo parcial permanece posible en errores de IO. |
| Selector distinto/recurso del mismo tamaño | **Confusor real para el caso exacto**, no refutación del transporte: tabla selecciona B8 o 9C antes del servicio. Puede cambiar el texto/flujo del parser y permitir que B8 funcione aun si otra diferencia EE afectase a datos de 9C. |
| Init unhandled | No impide la copia independiente actual. Puede ocultar necesidades de otros comandos; no explica mejor que la lectura ausente el buffer vacío original. |

**¿Puede B8 funcionar y 9C fallar?** Para transportar el paquete mode=1 completo, con esta ISO estable, mismos destino/tamaño y lecturas host exitosas, no hay una bifurcación por resourceId ni por contenido que permita esa diferencia: cambia únicamente el offset, y ambos rangos están verificados. La generalización de la **copia** es fuerte. Sí pueden diferir la apertura/IO en otra sesión, el recurso seleccionado o el procesamiento EE de contenido distinto; no se ha demostrado equivalencia del **recorrido completo**. Por eso el caso literal merece una prueba corta, pero no invalida el diagnóstico causal del RPC ausente.

## 11. Experimentos mínimos recomendados antes de BLOCKER_003

1. **Caso exacto 0x9C, una comparación de completion**: mismo selector/ELF/ISO, capturar request completo y RAM `[0x1E00000,0x1E10A60)` más 16 bytes de guard en PCSX2 y RECOMP, en PC equivalente `0x219228`; hash y comparación completa. Capturar después tabla `0x586740` y retorno de Print_message. Máximo valor: cierra recurso, copia y desaparición del mecanismo sin inferirlos de un timeout.
2. **A/B del mismo ejecutable actual**, un arranque con ISO verificada y otro sin fuente CD, entradas iguales. Observar llegada de datos/completion y primer síntoma original; detener pronto. No alterar memoria para forzar avance. Aísla la copia frente a rebuild/logging. Este experimento se recomienda, no se ejecutó aquí.
3. **Contrato mínimo con host simulado**: mode=1 alineado/no alineado, respuesta blocking/NOWAIT, fallo de lectura en segundo chunk y rechazo/cobertura de mode=2. Usar scratch sin modificar el servicio principal. No necesita un build del juego ni ejecutar rutas posteriores. La prueba debe comparar con el contrato IRX, no copiar expectativas del HLE.
4. **Regresión del pipeline en scratch tras autorizar su reparación**: A→B sin rebuild debe rechazar exe A; baseline existente debe migrar de forma segura o explicar recuperación; checkout principal CRLF y fallo write-tree deben fallar limpiamente. Los contraejemplos actuales ya están demostrados; no repetir full builds para confirmarlos.

No recomiendo una campaña exhaustiva de modos/audio ni migración a plugin como prerrequisito del caso original. Para comenzar la siguiente investigación, priorizar 1 y dejar explícitas las restricciones de procedencia/cobertura; 2–4 ordenan el trabajo adicional por coste y riesgo.

## 12. Veredicto sobre BLOCKER_002

**¿Está causalmente resuelto?** Sí, fuertemente soportado para la omisión de lectura que originó el runaway. El fix contiene la operación que faltaba y hay evidencia runtime de ejecución de esa operación. No se encontró una explicación alternativa mejor de los datos ausentes.

**¿Está suficientemente validado?** Para mantener “resuelto con reservas” en la lectura simple, sí. Para “caso literal reproducido y contrato fno=2 sólido”, no. Faltan la captura completa y la comprobación de tabla/retorno; hay diferencias demostradas de mode/response y defectos de integración.

**¿Hay que reabrirlo?** No como diagnóstico causal desde cero. Sí debe quedar abierta una verificación acotada del caso 0x9C, y registrar separadamente las diferencias de protocolo y los bugs del pipeline. El título “fno=2 soportado” debería entenderse con la restricción mode=1 y sus límites de alineación/errores, no como garantía general.

**¿Podemos pasar a buscar BLOCKER_003?** Recomiendo hacerlo **después del experimento 1**, fijando hash del exe/vendor/ISO y sin confiar únicamente en built.json. No bloquear por estética del patch o por detalles internos IOP no observables. Hasta esa captura, avanzar sería aceptar conscientemente una reserva de validación, no haberla cerrado. Esta auditoría no comienza esa investigación ni implementa ninguna recomendación.
