# P4.1.8 — Astra: evolución visual y auditoría comparativa independiente

Fecha: 2026-09-13. Auditoría de evidencia existente. **NO source changes, NO build, NO run, NO commit, NO push.** No se modificó vendor, el lock ni los informes históricos. Las únicas escrituras son este informe y derivados de análisis offline bajo `analysis/local/p418/`.

## 1. Veredicto ejecutivo

**HECHO:** P4.1.3 elimina el borrado sistemático de las filas 256–447 de la película, pero su estado de ejecución tiene la UI pre-PSS casi siempre negra. RUN_072 restablece aproximadamente el comportamiento antiguo: UI intermitente y película con región inferior ausente. RUN_073 vuelve a ese tipo de compromiso, con la región **superior** ausente. No demuestra una mejora global sobre RUN_063.

**Hallazgo independiente decisivo: `P416_STATIC_ORACLE: REFUTED`.** P4.1.6 leyó correctamente los defaults de los helpers, pero omitió la cola condicional de `sceGsSetDefDBuffDc`, dentro de sus 740 bytes. Esa cola sí escribe FBP: `disp[1]` y los dos FRAME del **slot 0**, con `zbufAddr/2`. El slot 1 permanece a cero. La rama se toma con el modo configurado por `Main_init`. Por tanto, la conclusión «UI original single-buffer FBP0, host presenter necesariamente culpable» no se sostiene.

El contrato UI reconstruido es display `{0,0x38}` y draw `{0x38,0}`, no ninguno de los pares ensayados. La película conserva el beneficio local de P4.1.3 porque `Main_init` sustituye los campos del slot 0/disp1 por `0xA0`, ocultando precisamente esta omisión del SDK. Hay una divergencia guest/HLE anterior al presentador todavía sin reproducir fielmente para UI.

Clasificación primaria: **P418_GLOBAL_TIMELINE_CONFIRMS_LOCAL_FIX_WITH_UI_REGRESSION**. Hallazgo secundario mayor: oracle universal de P4.1.6 refutado; no hay prueba causal de una captura concreta entre clear y redraw. No corresponde implementar un fix del host basándose en ese oracle.

## 2. Entrada, identidad y límites del corpus

Main HEAD: `5c0881b77d3ae1f7f317d2f41b1824fc61451a19`.
Vendor HEAD: `19911ce35fb8f2029853f25612b1cc420a8c74bb`, limpio; coincide con `upstream.lock.json:patched_commit`. Main sin cambios tracked, con diez informes preexistentes sin trackear: P411, P411R, P411B, P412, P414, P415, P416, P417, P41F y P42. Se conservaron todos.

ELF real: `original/SLES_503.58`, entry `0x00100008`, CRC32 `77654AD2`, SHA-256 `d0753a6b3b2f00802a50758a872d8cf051725aa31c839aa8894eee30ce58bab4`. Estos identificadores se verificaron leyendo el ELF; todas las direcciones originales de este informe se refieren a él.

Se leyeron README, UPSTREAM, lock, informes P41/P41F/P411/P411B/P412/P413/P414/P415/P416/P417 y P42, y se contrastaron sus afirmaciones con RAM, logs, capturas y código actual. Los informes son antecedentes, no sustitutos de evidencia primaria. No se abrió investigación de gameplay, audio ni boot delay.

**941 PNG examinados**, equivalentes a 176 imágenes RGB distintas contando cada run por separado. Se decodificaron todos, se registraron sus hashes y orden, se inspeccionaron visualmente todas las clases de imágenes mediante hojas de contacto y se ampliaron los ejemplos decisivos. Un representante de cada grupo exacto cubre todos sus duplicados; no se eligieron solo capturas favorables.

Derivados locales:

- `analysis/local/p418/audit.json`: metadatos originales, inventario individual de las 941 imágenes, tiempo cuando existe, dimensiones, SHA-256 RGB, negro/magenta y grupos de duplicados.
- `RUN_*_unique_*.jpg`: diez hojas de contacto que cubren las 176 imágenes distintas. El rótulo `xN` identifica multiplicidad; el orden de estas hojas por nombre no sustituye el orden temporal `visual_*` del inventario.
- `elf_evidence.json`: bytes, palabras, `.symtab`, callers y lectura textual mínima de los diez cuerpos originales relevantes.
- `boundary_counts.json`: recuentos independientes de las trazas existentes de RUN_061/066/073.

**Limitación de procedencia:** `result.json` registra SHA del exe pero no HEADs ni todos los flags heredados. Las revisiones históricas se atribuyen combinando fechas de commits, informes y estado RAM/log; no se presentan como un manifest Git capturado en cada launch. Los ocho hashes son distintos. RUN_063 además cambia de tamaño de ventana y solo tiene capturas espaciadas, sin `visual_*`; no permite una comparación porcentual temporal con RUN_071–073.

## 3. Identidad de cada corrida

Todos los `result.json` contienen ISO real y arena `0x009FA000–0x00ABE000`; ownership y diagnósticos P311/pad/RAM. `DMC_P314_SYNC_REDECODE` no está guardado por ese manifest: se verifica por la ejecución efectiva de `[P314:GP]` y `[P3142:ring]`.

| Run | Main / vendor de base atribuidos | Exe SHA-256 | Duración / resultado del harness | P314 observado |
|---|---|---|---|---|
| 063 | c9ee65d / 16ef7ab; P41 limpio | `7a589abcf44a5730c3b3e534447160a215c30f934e15f040fefec968c2b0c7b3` | 297,438 s / BOOT_ONLY | 24 GP, 23 ring, 0 GP:H |
| 066 | c9ee65d / 16ef7ab + candidato P413 y diagnósticos | `6a8dd8855f985c54e921aef82b5c08e6b1a650c2e53e86269c71229d349e0312` | 83,782 s / SUCCESS | 24 / 23 / 0 |
| 067 | 5c0881b / 19911ce; post-P413 limpio atribuido | `7dee0dcf1174ed6814e8b7dbdb05f6d5ae5a2c1050dfaf655afc57478a2a43bf` | 71,188 s / EXIT_BEFORE_TARGET | UNKNOWN: no llegó a MPEG |
| 070 | 5c0881b / 19911ce + diagnóstico P42 | `616cef77a509c5a8af2bcff12f4fabd74ed134f92b0a9e0285b05d6564065c12` | 101,984 s / TARGET_NOT_REACHED | 24 / 23 / 0 |
| 071 | 5c0881b / 19911ce; limpio de diagnósticos GS/P42 | `25c52c78f1370be9a6b379bc36054e66aef68e939472412f027d1e543c546898` | 99,641 s / TARGET_NOT_REACHED | 24 / 23 / 0 |
| 072 | 5c0881b / 19911ce + revert experimental GS | `92dc354cc8b07396e1cc00da4eceb5a98bf36a19b81a2c72faf267ce8ab9cd9c` | 100,094 s / TARGET_NOT_REACHED | 24 / 23 / 0 |
| 073 | 5c0881b / 19911ce + frameSize y diagnóstico P415 | `5d4b649e5c60f1ce468d5d96871696b0ec5e4c7998bf669f17f4ddf1decd517b` | 77,797 s / SUCCESS | 24 / 23 / 0 |
| 074 | 5c0881b / 19911ce + latch experimental P417 | `d45ddd983ed6871a44dbc5eea101db3b996522e244a261499a56037092fc7652` | 76,875 s / SUCCESS | 24 / 23 / 0 |

**HECHO:** `BOOT_ONLY` de RUN_063 no describe su alcance real: el propio manifest registra PSS y sus capturas muestran película, título y secuencia posterior. `SUCCESS` de RUN_074 tampoco significa éxito visual: solo cumplió el marker MPEG del harness. RUN_067 no prueba nada sobre movie; su log termina en cierre de ventana, sin marker PSS ni dump GetPicture. No inferir crash MPEG ni sync desactivado de sus ceros.

El commit P413 es de las 20:27:21 del 12/09, posterior a RUN_066 (20:19:41) y anterior a RUN_067 (20:58:59). La identidad de código de RUN_066 incluye un delta aún no committeado. RUN_070 **sí** contiene instrumentación MPEG P42, aunque no diagnósticos GS: no debe llamarse exe totalmente limpio.

### Evolución de código requerida

`fbp1` en esta tabla significa el argumento HLE que siembra **draw11/draw12 (slot 1)**. No equivale a «segundo buffer original», ni al `disp[1]` escrito por la cola original.

| Run | fbp1 UI / movie | P4.1 | P4.1.3 | Temp changes | Production? |
|---|---:|---|---|---|---|
| 063 | 0x70 / 0xE0 | Sí | No | P311 comunes | Sí, baseline antiguo |
| 066 | 0 / 0 | Sí | Sí, candidato | P413 A/B, DRAW, CD/E, field | No, exe instrumentado; semántica luego promovida |
| 067 | 0 / 0 atribuido | Sí | Sí | P311 comunes; sin dump GS | Sí atribuido, alcance boot |
| 070 | 0 / 0 | Sí | Sí | P42 ES/feeding trace | No, diagnóstico |
| 071 | 0 / 0 | Sí | Sí | P311 comunes | Sí, semántica vigente |
| 072 | 0x70 / 0xE0 | Sí | Revertido temporalmente | Solo revert semántico de GS | No |
| 073 | 0x38 / 0x70 | Sí | Sustituido temporalmente | frameSize + P415 trace | No |
| 074 | 0 / 0 | Sí | Sí | Cambio de latch P417 | No |

### Estado GS reconstruido de RAM

Lecturas independientes `uint64 little-endian` de `getpicture.bin`. UI: DISPFB en `0x740780/0x7407B8`, FRAME slot0 en `0x7407F0/0x740870`, slot1 en `0x740960/0x7409E0`. Movie: DISPFB en `0x740C40/0x740C78`, FRAME slot0 en `0x740CB0/0x740D30`, slot1 en `0x740E20/0x740EA0`. Ambos contextos coinciden en cada slot.

| Run | Estado GS: UI display; draw / movie display; draw | fbp1 UI/movie | Diagnósticos distintivos | UI | Movie | Observación |
|---|---|---:|---|---|---|---|
| 063 | {0,0}; {0,70} / {0,A0}; {A0,E0} | 70/E0 | Ninguno GS | Aparece, no estable | Corte inferior + daño tardío | Referencia antigua, muestreo escaso |
| 066 | {0,0}; {0,0} / {0,A0}; {A0,0} | 0/0 | P413 | Casi negra; Language puntual | Warning completo | Diagnósticos no restauran UI sostenida |
| 067 | Sin RAM; estado atribuido a P413 | 0/0 atribuido | Ninguno GS | Negra | UNKNOWN | No llegó a PSS |
| 070 | {0,0}; {0,0} / {0,A0}; {A0,0} | 0/0 | P42 | Negra en muestras | Visible, muestreo escaso | No usar como película íntegra validada |
| 071 | {0,0}; {0,0} / {0,A0}; {A0,0} | 0/0 | Ninguno GS | Negra | Warning íntegro; daño posterior | Baseline semántico parcial actual |
| 072 | {0,0}; {0,70} / {0,A0}; {A0,E0} | 70/E0 | Revert | Intermitente | Corte inferior alternante | Reproduce el patrón antiguo |
| 073 | {0,0}; {0,38} / {0,A0}; {A0,70} | 38/70 | P415 | Intermitente | Corte superior alternante | No progreso global demostrado |
| 074 | {0,0}; {0,0} / {0,A0}; {A0,0} | 0/0 | P417 | Placeholder | Placeholder | Contrato GS igual; presentación fallida |

FBW=8 y PSM=0: FRAME raw `0x80000 | FBP`; DISPFB raw `0x1000 | FBP`. Los sets son pares de slots, no inferencias de frecuencia. Valores hexadecimales sin prefijo en las celdas para legibilidad.

## 4. Evolución visual: secuencias completas

`VISIBLE` significa contenido reconocible en la evidencia disponible, no estabilidad probada. `INTERMITTENT/FLICKER` exige muestras con contenido y muestras negras. `FULL` describe extensión del frame temprano, no una película entera libre de errores. Blanco inicial y magenta no se cuentan como UI del juego.

| Run | Memory Card | Language | Pre-PSS | Movie | Región corrupta | Estado |
|---|---|---|---|---|---|---|
| 063 | VISIBLE puntual | VISIBLE puntual | INTERMITTENT | LOWER_REGION_MISSING + OTHER tardío | Inferior; otros defectos variables posteriores | Antiguo, título también visible pero defectuoso |
| 066 | BLACK | INTERMITTENT | INTERMITTENT, casi todo negro | FULL en warning observado | Sin corte fijo observado; tardío UNKNOWN | P413 instrumentado |
| 067 | BLACK | BLACK | BLACK | UNKNOWN | UNKNOWN | Boot únicamente |
| 070 | BLACK | BLACK | BLACK en muestras | OTHER: visible, dos muestras | Sin corte fijo identificable en las dos; no evaluación exhaustiva temporal | P413 + P42 |
| 071 | BLACK | BLACK | BLACK después del blanco inicial | FULL temprano → OTHER/mezcla y estiramientos tardíos | Variable tardía, no corte inferior sistemático | P413 limpio |
| 072 | INTERMITTENT | INTERMITTENT | FLICKER y largo tramo negro | LOWER_REGION_MISSING alternante + OTHER tardío | Inferior, filas 256–447 | Revert experimental |
| 073 | INTERMITTENT | INTERMITTENT | FLICKER y largo tramo negro | UPPER_REGION_MISSING alternante | Superior, filas 0–255 | Candidato frameSize |
| 074 | UNKNOWN: magenta | UNKNOWN: magenta | UNKNOWN: magenta | OTHER: placeholder magenta | No contenido evaluable | Candidato de latch fallido |

BOOT: capturas `boot.png` negras en 063/066/067/070/071/072; 073 ya muestra Memory Card y no aísla boot; 074 es magenta. Los primeros `visual_*` blancos de 066/071/072/074 son transitorios de ventana, no prueba de un logo de boot correcto. Clasificación de boot en la evidencia: BLACK para los seis primeros estados citados; UNKNOWN para 073/074.

FIRST PSS: 063 UNKNOWN (no muestreo denso del primer warning); 066 FULL; 067 UNKNOWN; 070 MIXED (warning mezclado con fuego en `state_04`); 071 FULL; 072 MIXED FULL/PARTIAL_TOP; 073 MIXED FULL/PARTIAL_BOTTOM; 074 UNKNOWN (placeholder). Los flashes magenta de entrada se registran aparte.

### State A: RUN_063

Las **19** capturas disponibles incluyen MC (`input_01`), selector de idioma (`input_02`), negros antes/después, película completa o cortada, título (`state_06`–`state_09`) y escenas posteriores (`state_11`–`state_14`). No hay `visual_*`. `state_05` muestra la imagen de espada con un corte horizontal limpio y región inferior negra. `state_04` presenta estiramientos; el título tiene una zona rectangular de contenido negro/ruidoso: tampoco es un baseline «correcto» global.

La mezcla de generaciones stale exactas es UNKNOWN a partir de estas capturas; se observa corrupción, no identidad de generaciones. Duración, resolución y muestreo difieren mucho de RUN_073. No se inventa una tasa de flicker para RUN_063.

### State B: RUN_066/067/070/071

RUN_066: toda la secuencia periódica desde 1,422 a 76,968 s es negra (148 muestras). Las tres capturas idénticas de Language existen, pero son `after_01`, `before_CROSS`, `input_02`, no una ventana sostenida en el visual-trace. Luego se ve el warning completo. Por tanto, los diagnósticos no son evidencia de una UI arreglada.

RUN_071: 132 capturas periódicas negras consecutivas entre 1,891 y 69,405 s; antes solo dos imágenes blancas. Las fronteras de START/CROSS son negras. Tras el flash magenta de entrada aparece el warning completo sin corte recurrente. Más tarde, desde la transición de texto/fuego y los logos, hay mezcla, bloques y estiramientos evidentes (`visual_077421`, `079859`, `080952`, `086875`, `096343`, entre otros). **RETRACTADO como evaluación global:** «película perfecta»; correcto únicamente «se elimina el borrado fijo inferior temprano».

RUN_067 aporta ocho imágenes negras y ningún PSS. RUN_070 aporta ocho negras y dos de movie (`state_04/05`), insuficientes para inferir estabilidad temporal. No se suman estas corridas como si compartieran un único binario o un gate movie equivalente.

### State C: RUN_072 frente a RUN_063

**HECHO:** el par de entornos GS leído en RAM coincide en los doce campos FBP muestreados; la imagen RGB de MC es exactamente idéntica, SHA-256 `9676c26410d73d2dad909cfd7666668c9279fee3aa226864fc9b3ef05c5b2b20`. No es una comparación por tamaño de PNG.

**HECHO visual:** RUN_072 alterna warning completo y warning sin párrafos inferiores; espada con parte inferior ausente igual al patrón de `RUN_063/state_05`. La UI aparece y desaparece. **INFERENCIA respaldada:** reproduce aproximadamente el estado A. No demuestra igualdad de cadencia, ratio negro, duración ni toda la secuencia del título de RUN_063.

### State D: RUN_073 frente a RUN_063/072

Misma imagen exacta de MC que 063/072; Language completo aparece en cuatro muestras periódicas y desaparece en otras. Después alterna los cinco párrafos con solo la parte inferior: `visual_073500` y `074063` muestran ATTENZIONE/ADVERTENCIA y parte de la transición en torno a la fila 256, con el resto superior negro. `074625` vuelve al frame completo, `074907/075469` vuelven al corte.

**RUN073_NET_IMPROVEMENT: NO** = no hay mejora global demostrada. No significa que toda métrica local empeore: recupera muestras UI respecto a 071. Pero cambia qué región de la película se destruye y no restaura continuidad de la UI. El usuario describe correctamente el parecido funcional con el estado antiguo; no son imágenes ni errores geométricos idénticos.

## 5. Cuantificación independiente

Negro = todos los canales RGB de todos los píxeles exactamente cero, después de decodificar PNG. Magenta = RGB constante (255,0,255). No se usan bytes de archivo como sustituto del contenido. `non-black` incluye magenta y blanco: **no equivale a juego visible**.

Los cocientes siguientes son proporciones de **muestras**, no del tiempo real ni del total de presents. Hay gaps de captura, pausas del harness alrededor de inputs, distinta duración y capturas adicionales condicionadas a eventos.

| Run | Todos PNG | Negros | No negros | black_ratio | Magenta | RGB distintos |
|---|---:|---:|---:|---:|---:|---:|
| 063 | 19 | 7 | 12 | 36,84% | 0 | 14 |
| 066 | 175 | 157 | 18 | 89,71% | 1 | 15 |
| 067 | 8 | 8 | 0 | 100% | 0 | 1 |
| 070 | 10 | 8 | 2 | 80% | 0 | 3 |
| 071 | 207 | 144 | 63 | 69,57% | 1 | 62 |
| 072 | 200 | 125 | 75 | 62,50% | 2 | 64 |
| 073 | 162 | 134 | 28 | 82,72% | 2 | 15 |
| 074 | 160 | 0 | 160 | 0% | 159 | 2 |

No ordenar calidad global por esa tabla: RUN_074 demostraría absurdamente «el mejor» si solo se minimizara negro. RUN_063 contiene más tiempo tardío y capturas selectivas; RUN_073 termina en el warning.

### Ventanas comparables de `visual_*`

Pre-PSS: desde primer visual hasta tiempo `< PSS_REACHED` del manifest. UI 0–20 s: ventana fija, incluye transitorios iniciales. Post-PSS: resto de visuales. Cada celda contiene **negros / total (no negros; ratio)**.

| Run | Pre-PSS | UI 0–20 s | Post-PSS |
|---|---|---|---|
| 063 | UNKNOWN: sin visual-trace | UNKNOWN | UNKNOWN |
| 066 | 148/149 (1; 99,33%) | 34/35 (1; 97,14%) | 0/13 (13; 0%), 1 magenta |
| 067 | UNKNOWN: sin visual-trace | UNKNOWN | No PSS |
| 070 | UNKNOWN: sin visual-trace | UNKNOWN | UNKNOWN |
| 071 | 132/134 (2; 98,51%) | 33/35 (2; 94,29%) | 0/60 (60; 0%), 1 magenta |
| 072 | 118/126 (8; 93,65%) | 18/26 (8; 69,23%) | 0/60 (60; 0%), 1 magenta |
| 073 | 124/136 (12; 91,18%) | 23/35 (12; 65,71%) | 1/14 (13; 7,14%), 1 magenta |
| 074 | 0/136 (136; 0%), 135 magenta | 0/34 (34; 0%), 33 magenta | 0/12 (12; 0%), 12 magenta |

**Corrección de denominadores:** P414 reutilizó 133 muestras/ventanas sin delimitar consistentemente el PSS real por run. RUN_066 alcanza PSS a 76,969 s, RUN_071 a 69,406 s; con ese corte los recuentos son los anteriores. P414 acierta en «casi todo negro», no en sus cifras como ventana pre-PSS completa. Las únicas muestras periódicas no negras pre-PSS de 066/071 son blancas iniciales, no selector de idioma.

### Memory Card y Language: aproximación temporal explícita

MC_proxy = `[MC_CHECK, primer input)`; Language_proxy = `[primer input + 0,3 s, segundo input)`. Los markers son lógicos, no certifican que toda la ventana sea esa pantalla; las fronteras de teclas proporcionan comprobación visual adicional.

| Run | MC_proxy: negros/total, no negros, ratio | Language_proxy: negros/total, no negros, ratio |
|---|---|---|
| 066 | 17/17, 0, 100% | 6/6, 0, 100%; además 3 snapshots Language fuera del muestreo periódico |
| 071 | 16/16, 0, 100% | 6/6, 0, 100% |
| 072 | 5/7, 2, 71,43% | 3/6, 3, 50% |
| 073 | 10/17, 7, 58,82% | 2/6, 4, 33,33% |
| 074 | 0/17, 17, 0% (todos magenta) | 0/6, 6, 0% (todos magenta) |

063/067/070: no ratio temporal de estas fases; los snapshots input_01/input_02 son respectivamente visible/visible, negro/negro, negro/negro. El hecho de que MC y Language de RUN_073 se vean a veces no autoriza `UI_VISIBILITY_FIXED: YES` sin limitarlo a existencia puntual.

### Duplicados y continuidad

El negro RGB 640×448 tiene SHA prefijo `6909f7dfd08417d7`; aparece 157 veces en 066, 8 en 067, 8 en 070, 144 en 071, 125 en 072 y 134 en 073. El magenta `bd0acf994bc036f2` aparece 159 veces en 074. Los 147/148 visuales magenta de P417 se confirman; los otros doce snapshots también son magenta.

MC es un grupo común exacto entre 063/072/073, con multiplicidades por run 1/4/10. Language completo de 066 (3 snapshots) coincide exactamente con los 4 visuales de 073, hash prefijo `7653ea3a3ad2b4a1`. El selector de 072 muestra el mismo contenido con distinta intensidad, no hash idéntico. RUN_063 tiene resoluciones 640×448, 968×678, 1214×836 y 1276×981: no se comparan hashes entre resoluciones como si fueran frames distintos de juego.

Después del último Language, 072 presenta 111 visuales negros consecutivos de 14,062–69,984 s; 073, 113 de 13,891–71,000 s. Ese tramo por sí solo no distingue carga legítima de defecto de UI: falta referencia original temporal equivalente. No se contabiliza automáticamente como «pantalla que debería mostrar menú».

### Completitud temprana con la misma ventana

Ventana `(MPEG_INIT + 0,5 s) … (MPEG_INIT + 5,0 s)`. Se revisaron los nueve visuales de cada corrida y se comprobaron píxeles de las regiones 0–255 / 256–447. FULL_EXTENT significa contenido en ambas regiones y warning completo al inspeccionar, no fidelidad de cada píxel.

| Run | Muestras | FULL_EXTENT | Inferior negra | Superior negra | Placeholder |
|---|---:|---:|---:|---:|---:|
| 066 | 9 | 9 | 0 | 0 | 0 |
| 071 | 9 | 9 | 0 | 0 | 0 |
| 072 | 9 | 4 | 5 | 0 | 0 |
| 073 | 9 | 3 | 0 | 6 | 0 |
| 074 | 9 | 0 | 0 | 0 | 9 |

063/067/070 no ofrecen esa muestra comparable. No se declara 073 peor estadísticamente que 072 por 6/9 frente a 5/9: paridad y muestreo pueden explicar esa diferencia pequeña. Sí se demuestra que ambos siguen alternando frames completos e incompletos.

## 6. Recuento técnico de regiones: no es solo una impresión visual

Se reparsearon las líneas CD desde la primera línea A de picture=0 de cada run, sin reutilizar las cifras del informe:

| Run | FBP | Muestras CD | Región superior totalmente negra | Región inferior totalmente negra |
|---|---:|---:|---:|---:|
| 061 (pre-P413, apoyo) | A0 | 52 | 0 | 52 |
| 061 | 0 | 50 | 0 | 0 |
| 066 (P413) | A0 | 44 | 0 | 0 |
| 066 | 0 | 44 | 0 | 0 |
| 073 (frameSize) | A0 | 49 | 49 | 0 |
| 073 | 0 | 51 | 1 | 1 |

**NARROWED:** P415 decía 0/51 muestras negras de FBP0. Con el corte inclusivo desde A picture=0 hay una muestra enteramente negra; no implica un nuevo defecto regional en ese buffer, pero debe excluirse con una regla temporal justificada antes de afirmar 0/51. Las otras 50 no presentan el patrón. No cambia el hallazgo 49/49 de A0.

GS PSMCT32/FBW8: páginas de 64×32 píxeles. A0 ocupa `[0xA0,0x110)`. El FRAME E0 escribe desde la página que equivale a y=256 de A0; intersección `[0xE0,0x110)` = filas 256–447. El FRAME 70 ocupa `[0x70,0xE0)`; intersección con A0 `[0xA0,0xE0)` = filas 0–255. **HECHO aritmético:** mismo mecanismo de solapamiento físico, desplazado. No dos problemas distintos de decodificación MPEG.

RUN_066 registra 28652 draws a 0 y 25 a A0; ninguno a E0. RUN_073 registra 16275 a 0, 16474 a 38, 29 a A0, 28 a 70; ninguno a E0. El segundo candidato elimina el valor E0 pero no el aliasing dañino. «Cero draws a E0» no es un gate suficiente de integridad de película ni de UI.

## 7. Auditoría independiente del oracle P4.1.6

### Cuerpos y autoridad

| Símbolo original | Rango [inicio,fin) | Bytes |
|---|---|---:|
| sceGsSetDefDBuffDc | 0x101B48–0x101E2C | 740 |
| sceGsSetDefDrawEnv | 0x100620–0x100804 | 484 |
| sceGsSetDefDrawEnv2 | 0x1018E0–0x101AC0 | 480 |
| sceGsSetDefDispEnv | 0x1002E8–0x100558 | 624 |
| sceGszbufaddr | 0x100558–0x100620 | 200 |
| sceGsGetGParam | 0x100270–0x10027C | 12 |
| sceGsResetGraph | 0x100160–0x100270 | 272 |
| MainGsSetDefDBuffDc | 0x15C6A0–0x15C98C | 748 |

Lectura por `analysis/tools/runtime_loop/elf_contract.py`, sin ejecutar el juego. Su desensamblador mínimo deja algunas palabras sin decodificar; se decodificaron explícitamente los campos de opcode/registro/inmediato de la cola relevante, respetando delay slots. Los bytes originales, no un cuerpo de Ghidra, son autoridad. Se contrastaron también todos los comentarios de instrucciones presentes en los generated de Main_init (232/232) y MainGsSetDefDBuffDc (187/187) contra el ELF. El generated no se editó.

### Lo que P416 sí recuperó bien

Los helpers por separado siembran FBP0:

- FRAME_1 en `0x100694` (`fe020000`), FRAME_2 en `0x101950` (`fe060000`): FBW/PSM, sin FBP.
- DISPFB en `0x10038C` (`fe230010`): FBW/PSM, sin FBP; PMODE=0x66 en `0x100338/340`.
- ZBUF recibe el resultado de `sceGszbufaddr`.
- draw11/draw12 se inicializan mediante esos helpers y **conservan** FBP0 al retornar el SDK.

### Lo omitido: el propio wrapper SDK modifica esos defaults

Después de las seis llamadas de construcción y clear/GIFtags, el cuerpo continúa:

```text
PC        palabra    semántica
101D70    0c040156   jal sceGszbufaddr
101D78    8fa30020   lw v1,0x20(sp)          ; gparam guardado
101D7C    0040282d   a1 = v0                ; zbufAddr
101D80    24040001   a0 = 1
101D84    0004203c   dsll32 a0,a0,0
101D88    34840001   ori a0,a0,1            ; 0x0000000100000001
101D8C    dc620000   ld v0,0(v1)
101D90    3403ffff   v1 = 0xffff
101D94    0003183c   dsll32 v1,v1,0
101D98    3463ffff   ori v1,v1,0xffff       ; 0x0000ffff0000ffff
101D9C    00431024   and v0,v0,v1
101DA0    10440004   beq v0,a0,0x101DB4
101DA4    8fa30020   lw v1,0x20(sp)         ; delay slot
101DA8    84620000   lh v0,0(v1)
101DAC    14400014   bne v0,zero,0x101E00
101DB0    dfbf00c0   ld ra,0xc0(sp)         ; delay slot
101DB4    00052843   sra a1,a1,1            ; zbufAddr / 2
101DB8    de460038   ld a2,0x38(s2)         ; disp[1].dispfb
101DBC    de470060   ld a3,0x60(s2)         ; draw01.FRAME_1
101DC0    00051c00   sll v1,a1,16
101DC4    de4400e0   ld a0,0xe0(s2)         ; draw02.FRAME_2
101DC8    2402fe00   v0 = -512              ; borrar bits FBP
101DCC    00031c03   sra v1,v1,16
101DD0    30a501ff   andi a1,a1,0x1ff
101DD4    00822024   and a0,a0,v0
101DD8    306301ff   andi v1,v1,0x1ff
101DDC    00c23024   and a2,a2,v0
101DE0    00e23824   and a3,a3,v0
101DE4    00c33025   or a2,a2,v1
101DE8    00852025   or a0,a0,a1
101DEC    00e53825   or a3,a3,a1
101DF0    fe4400e0   sd a0,0xe0(s2)
101DF4    fe460038   sd a2,0x38(s2)
101DF8    fe470060   sd a3,0x60(s2)
```

Pseudocódigo del contrato FBP, después de los helpers:

```text
g = sceGsGetGParam()
z = sceGszbufaddr(psm,w,h)
if ((load64(g) & 0x0000ffff0000ffff) == 0x0000000100000001
    || load16_signed(g+0) == 0):
    f = (z >> 1) & 0x1ff
    disp[1].FBP = f
    draw01.FRAME.FBP = f
    draw02.FRAME.FBP = f
// disp[0], draw11, draw12 conservan FBP=0
```

**HECHO:** `zbufAddr` no se usa exclusivamente en ZBUF; su mitad se usa en tres campos FBP. La ausencia de argumentos distintos en los helpers no permite inferir ausencia de escrituras posteriores.

### La rama se aplica al arranque de DMC

`Main_init` en `0x15BAC4–0x15BAD4` llama ResetGraph con `(mode=0, interlace=1, omode=3, ffmode=1)`; a3 se copia de a1 en el delay slot. El ResetGraph original escribe interlace en g+0 (`0x1001D0`) y `ffmode != 0` en g+4 (`0x1001F0/1FC`). `sceGsGetGParam` devuelve `0x004FBC90`.

El valor enmascarado es, por construcción, `0x0000000100000001`: **BEQ tomado a 0x101DB4**. SyncV entre ResetGraph y la primera llamada no escribe esos campos. `MainGsSetDefDBuffDc` llama inmediatamente al SDK en `0x15C6C4`, antes de procesar sus flags propios.

Para PSM0, `sceGszbufaddr` original calcula `ceil(w/64)*ceil(h/32)`, doblado salvo que el mismo gparam enmascarado sea 1. Aquí está doblado: UI z=0x70, movie z=0xE0; la cola escribe respectivamente 0x38 y 0x70.

**RETRACTADO:** P416 dice que los flags del wrapper fijan gparam/SMODE2 **antes** de la llamada, y que no hay cálculo FBP condicionado por el modo. El wrapper se observa en orden contrario: llama al SDK primero; el flag stack fuerza FFMD=0 en las copias de entorno movie después (`0x15C7E0–0x15C85C`). No cambia gparam para esa llamada. Confundir el SMODE2 movie final=1 con gparam.ffmode=0 durante el SDK oculta la rama.

La lectura de `0x4FBC90` en dumps RECOMP contiene `01 00 02 00 01 00 03 00 …`; no se usa como medición viva ORIGINAL: HLE mantiene gparam host/scratch y ese RAM puede ser dato inicial del ELF. La prueba original es la cadena de instrucciones anterior.

### Valores esperados: iniciales y después del juego

| Estado | disp0 FBP | disp1 FBP | draw01/02 FBP | draw11/12 FBP |
|---|---:|---:|---:|---:|
| Helpers puros | 0 | 0 | 0 | 0 |
| SDK original UI 512×224, al retornar | 0 | 38 | 38 | 0 |
| SDK original movie 512×448, al retornar | 0 | 70 | 70 | 0 |
| Movie tras parches Main_init | 0 | A0 | A0 | 0 |
| UI HLE actual | 0 | 0 | 0 | 0 |
| Movie HLE actual tras Main_init | 0 | A0 | A0 | 0 |

El wrapper copia SDK+0x38 a UI+0x48 (`0x7407B8`), SDK+0x60 a UI+0x80 (`0x7407F0`), SDK+0xE0 a UI+0x100 (`0x740870`). Slot1 se copia desde SDK+0x1D0/+0x250 a UI+0x1F0/+0x270. El juego reemplaza esos tres campos **del movie** por A0 en Main_init; no son los campos de UI.

En los campos auditados, el retorno UI esperado contiene `0x1038` en `0x7407B8`, `0x80038` en `0x7407F0/0x740870`, y `0x80000` en `0x740960/0x7409E0`. Todos los runs ensayados dejan los tres primeros a cero en sus bits FBP. **Ninguno ensayó ese contrato completo.**

`P416_STATIC_ORACLE: REFUTED` para su afirmación central universal. Componentes válidos preservados: helpers a cero, slot1 a cero, PMODE original=0x66 y aritmética z para estos argumentos. No se afirma que todas las demás partes del SDK/HLE sean byte-fieles: no lo son por la omisión demostrada y otras diferencias ya conocidas.

## 8. UI negra frente a movie visible: diferencias probadas de pipeline

Primera diferencia guest/HLE defendible para la UI: **al retornar `sceGsSetDefDBuffDc`, falta la cola que establece disp1=38 y draw01/02=38**. Esa diferencia antecede DMA, rasterización y copia host. No prueba por sí sola cada píxel negro, pero refuta «todo coincide hasta el host».

| Capa | UI pre-PSS actual | Movie actual con P413 | Evidencia / alcance |
|---|---|---|---|
| Construcción guest env | 512×224; faltan campos FBP de cola SDK | 512×448; Main_init parchea los campos omitidos a A0 | ELF + RAM, HECHO |
| Draw productivo | Sprites texturizados para texto/UI, además de clears | Imagen completa mediante IMAGE upload; draws de fade sin textura | RUN066 pre-init: 28089 draws tme=1, 535 tme=0, todos a FBP0 |
| DMA/GIF | Paquetes de draw/env y recursos de textura | Movie_loadimage envía cadena de 32 REF del imageAddr | Rutas distintas anteriores a copiar framebuffer |
| Escritura de color | Depende de rasterizar la UI al FRAME elegido | Upload escribe directamente los píxeles al framebuffer GS | No es un fullscreen textured movie quad |
| Display/draw | UI HLE muestra 0 en ambos slots; draw 0/0 post-P413 | display 0/A0; draw A0/0 | RAM y trazas, HECHO |
| Z/test | UI ztest=2, ZMSK=0 | Movie ztest=0, ZMSK=1 | Argumentos ELF y env; no atribución causal nueva |
| SMODE2 final | 3: INT=1, FFMD=1 | 1: INT=1, FFMD=0 | RAM; distinto al gparam compartido del SDK |
| Selección host | copySource puede sustituir FBP0 totalmente negro por contextFrames | Durante warning medido, source=display sin sustitución | Código actual; RUN061 sí tiene dos sustituciones UI hacia 70 |
| Campo | No entra en `INT && !FFMD` | Aplica bob por paridad del tick | gs_cpu_backend.cpp:1778–1889 |
| Snapshot/copy/upload host | Mismo mecanismo de latch por cambio currentVSyncTick | Mismo mecanismo | ps2_runtime.cpp:399–425, gs_frontend.cpp:527–550 |

**Distinción importante:** cambiar fbp1 también cambia los candidatos del fallback host, no solo dónde cae un clear. RUN061 registra `dispFbp=0, selFbp=70` en dos muestras pre-init (líneas 1275/1388), con `usedPreferred=0`: el fallback de negro y preferredSource no son lo mismo. Con P413, los dos FRAME contextuales UI son 0; esa alternativa desaparece. Es otra razón para no reducir el A/B a prueba exclusiva de una ventana clear/redraw.

Código actual re-verificado: `GS::buildPresentationRequestUnlocked` copia registros y contextFrames; `latchHostPresentationFrame` obtiene request, luego llama backend Present; `GSCpuBackend::Present` usa snapshot de VRAM; `copySource` selecciona superficie, aplica fallback si procede; después field/alpha y upload host. No se auditó ni culpó al rasterizador OpenGL. La rama magenta está explícita cuando `copyLatchedHostPresentationFrame` falla.

## 9. ¿Está probado CLEAR → HOST COPY → REDRAW para una captura negra?

**PRESENTATION_CLEAR_WINDOW_CAUSAL: UNKNOWN.** La asincronía y ausencia de barrera de frame guest completo permiten ese interleaving. Posibilidad estática no es una secuencia dinámica observada para un screenshot determinado.

Las trazas P413 muestran muchos draws y CD/E negros, pero no identifican de forma conjunta un clear concreto, su framebuffer, el snapshot exacto asociado a la captura y el redraw posterior completo. Un `type=6,tme=0` aislado no certifica el clear de interés; faltan geometría/color y correlación de generación para ese propósito. El oracle «single-buffer original» que reforzaba la atribución queda además refutado.

RUN074 demuestra que el **candidato implementado** de latch no entregó imágenes útiles (147/148 visuales magenta), no demuestra por sí mismo cuántas veces se llamó `eeWaitVSyncTicks`. «Casi nunca se invoca» es una inferencia si no hay contador directo; también podrían intervenir latches vacíos o requests inválidos. Se refuta el éxito del candidato, no toda hipótesis de presentación ni toda posibilidad de sincronización adecuada.

## 10. Qué arregló y qué rompió cada cambio

| Cambio | Mejora concreta | Regresión / límite | Clasificación de auditoría |
|---|---|---|---|
| P4.1 DISPFB0=0 | Corrige un campo original que sí debe ser cero; movie display pasa de E0/A0 a 0/A0 | Sigue FRAME E0 destructivo; no prueba arreglo global de UI | GLOBAL_IMPROVEMENT acotado al estado conocido, sin regresión atribuida demostrada; no cierre global |
| P4.1.3 slot1 FRAME=0 | Elimina exactamente el borrado inferior por FRAME E0; slot1=0 coincide con ELF | UI observada pasa a casi todo negro; omisión de slot0/disp1 del SDK persiste | LOCAL_FIX_WITH_REGRESSION |
| P4.1.5 frameSize en slot1 | Recupera muestras de MC/Language respecto a 071 | UI aún titila; rompe región superior movie; escribe frameSize en el slot equivocado respecto al original | PURE_TRADEOFF; refutado como fix universal |
| P4.1.7 latch en eeWaitVSyncTicks | Ninguna mejora visual demostrada | Sustituye UI y movie por placeholder casi permanente | REFUTED |

Para P4.1, la recuperación visual previa descrita por P41/P41F es consistente con el cambio de dirección y la evidencia movie posterior; no se hizo aquí una comparación temporal completa de UI pre-P41. `GLOBAL_IMPROVEMENT` no significa todas las pantallas corregidas ni ausencia universal de efectos laterales. Es el único de los cuatro cambios sin regresión visual atribuida demostrada en el corpus; su efecto contractual puntual se sostiene independientemente del lenguaje de esos informes.

P413 no «rompe un contrato UI correcto que había antes»: ni el estado anterior ni el posterior reproduce la cola SDK completa. El cambio corrige un campo y empeora la manifestación de otra omisión. Se observa un trade-off visual; eso no obliga a que hardware original requiera elegir entre UI y movie.

## 11. Baselines y estado de producción

**BEST_VISUAL_BASELINE: RUN_063, provisional por cobertura de fases visibles.** Es la evidencia disponible que muestra MC, idioma, movie, título y escenas posteriores en la misma ejecución. Tiene corte inferior, título defectuoso y corrupción tardía; no es una recomendación de revertir. Su ventaja sobre runs cortos no es una prueba de mayor porcentaje de frames correctos: los tiempos observados son desiguales. Para comparación densa del estado antiguo se usa RUN_072; para movie temprano sin corte se usa RUN_071.

**BEST_SEMANTIC_BASELINE: RUN_071 / producción P4.1+P4.1.3, limitado al subconjunto FBP probado.** Es el más cercano de los estados ensayados: DISPFB0 y slot1 FRAME correctos, movie post-Main_init correcto. **Ningún run es baseline del contrato UI original completo.** Declarar P413 byte-fiel a todo el SDK sería falso.

PERMANENT PRODUCTION FIXES CURRENTLY ACTIVE:

- P41: `dispfb0=makeDispFb(0u,...)`.
- P413: `fbp1=0u` en draw11/draw12; draw01/02 aún cero; disp1 aún cero. La cola condicional original no está implementada.
- Infraestructura P3 previa en lock: CD, scheduler/arena, experimento ownership y sync/recirculación viBuf. Ownership/sync siguen siendo modos opt-in; su código presente no prueba activación sin logs/env.
- P42 backpressure no está aplicado como fix de producción en el lock inspeccionado.

TEMPORARY EXPERIMENTS REVERTED:

- RUN072: `fbp1=zbufAddr`.
- RUN073: `fbp1=zbufAddr/2`.
- RUN074: mover latch a `eeWaitVSyncTicks` y usar generation para presentar.

DIAGNOSTIC-ONLY CHANGES:

- P411/P411B/P413/P415 firmas A/B/CD/E/pre/postfield y draws; P42 capturas ES. Revertidos según informes y ausentes del vendor limpio vigente.
- P311 pad/diagnósticos RAM y logging P314 son infraestructura previa, comunes a las corridas, no candidatos gráficos acumulados.

**¿Estamos combinando soluciones experimentales? NO** para 072/073/074: el source actual conserva fbp1=0 y el latch host por tick, no los candidatos. Sí conserva los dos cambios de producción P41+P413. Vendor limpio y lock coherente prueban el source, no convierten por sí solos el exe de disco en uno de los runs.

Exe activo leído durante esta auditoría: SHA-256 `8aa853873e6fd5fa863fa9dee5307e960eb6281c0169e25e7adc8f7aa4c38986`. No coincide con ninguno de los ocho runs y **no fue ejecutado**. Su correspondencia binaria exacta con source no se certifica con un build nuevo. No se fusiona su identidad con RUN071.

## 12. Mapa de bugs actual

| Bug | Síntoma | Causa / estado de evidencia | ¿Arreglado? |
|---|---|---|---|
| 1. DISPFB0 equivocado | Fragmentos/stale por leer E0 donde debía leerse 0 | Campo HLE erróneo; P41 lo sustituye por cero, respaldado por ELF | Sí, ese campo; P41 activo |
| 2. FRAME slot1 E0 | Movie A0 pierde filas 256–447 | Alias de páginas por draw de color, no Z ni upload incompleto; confirmado por región y desaparición post-P413 | Sí en producción, reintroducido solo en 072 |
| 3. FRAME slot1 70 experimental | Movie A0 pierde filas 0–255 | Mismo alias físico, desplazado | Candidato retirado; no presente en producción |
| 4. UI pre-PSS casi negra | MC negro, Language raro/ausente tras P413 | A/B implica fbp1; nueva divergencia demostrada: falta cola SDK slot0/disp1. Causalidad pixel a pixel restante UNKNOWN | Abierto; no fix completo aplicado |
| 5. Flicker genérico / sustitución / ghosting | UI visible intermitente; contenido variable | No unificado causalmente. Puede involucrar destinos, fallback, timing o field según fase; no atribuir todo al movie E0 | Abierto |
| 6. MPEG tardío | Bloques, estiramientos, mezcla tras escena temprana | P42 captura ES con payloads omitidos por saturación/backpressure; independiente de FBP | Abierto en producción inspeccionada |
| 7. Latch P417 | Magenta casi permanente | Fallo del candidato para producir presentación utilizable; frecuencia exacta de hook no medida directamente | Experimento retirado |
| 8. Paridad SDK adicional | PMODE difiere en AMOD y otros defaults previos al wrapper | Original helper PMODE=0x66; HLE usa makePmode distinto. No prueba causa de corte regional/UI negra | Diferencia abierta, no mezclada como fix |

P42 se mantiene separado: evidencia binaria documentada en archivos existentes de `analysis/local/p42/` sitúa la primera omisión en offset ES 2.081.537, 4063 bytes, antes de que FFmpeg avise en picture 43. No se volvió a ejecutar decoder ni se reabrió ese análisis. Los logs de los runs largos contienen corrupción; asignar cada screenshot tardío a una omisión específica requeriría correlación que aquí es UNKNOWN. No confundir extensión completa de imagen con contenido correctamente decodificado.

El flicker antiguo y el negro post-P413 comparten sensibilidad al estado FBP, pero **no se ha probado que sean el mismo interleaving clear/copy/redraw**. Los cortes inferior/superior sí comparten una explicación geométrica demostrada. La pérdida ES es independiente y anterior a GS. No forzar una única raíz para todos los síntomas.

## 13. Correction ledger independiente

- P413 `UI_FLICKER_IMPROVED: YES`: **RETRACTADO** como afirmación visual. Cero draws E0 no prueba UI correcta; la UI usa otro struct y altura. No se hereda H8 de P412 como prueba causal de todos los menús.
- P414 «película perfecta»: **NARROWED** a ausencia del corte fijo temprano. RUN071 completo conserva daño tardío evidente.
- P415 `UI_VISIBILITY_FIXED: YES`: **NARROWED** a presencia de muestras MC/Language; no estabilidad. RUN073 no es mejora global demostrada.
- P416 «no existe ningún cálculo FBP», «zbufAddr exclusivamente ZBUF», «todos los FBP iniciales cero»: **RETRACTADO**, por `0x101D70–0x101DF8`.
- P416 «flags ajustan gparam antes del SDK»: **RETRACTADO**; wrapper llama al SDK antes de ajustar copias de SMODE2.
- P416 «UI original single-buffer FBP0»: **RETRACTADO** para el camino Main_init reconstruido. UI original esperada display 0/38 y draw 38/0.
- P416/P417 «presentador host causal del negro»: **NARROWED a hipótesis**, no prueba de exclusión de GS anterior. Existe un mismatch anterior verificable.
- P417 «eeWaitVSyncTicks casi nunca se invoca»: **INFERENCIA**, no conteo directo. El fracaso visual del candidato sí es HECHO.
- P411B «nunca se sube la región inferior»: corrección P412 preservada; un snapshot CD durante Present no es un snapshot inmediatamente tras el último IMAGE.
- P411 «fragilidad de timing P314» y P41 «warnings normales»: correcciones posteriores preservadas: flag faltante en el primer caso; corrupción real de feed en el segundo.
- P415 «frameSize es incorrecto» debe distinguir **valor/slot/condición**: el SDK original sí usa z/2, pero en disp1 y draw01/02 bajo una condición. El experimento lo puso en draw11/12 y dejó los otros campos sin cambiar. Su fracaso no refuta la aritmética ni la cola original.

## 14. Respuestas explícitas a las veinte preguntas

1. **¿073 mejor globalmente que 063?** No demostrado; se responde NO. UI intermitente y movie con región opuesta rota; 063 tiene mayor cobertura temporal.
2. **¿Se parece al estado antiguo?** Sí en comportamiento global; no en región concreta ni identidad del binario.
3. **¿072 reproduce aproximadamente 063?** Sí: doce campos FBP iguales, MC RGB exacta y mismo corte inferior; frecuencia global no comparable.
4. **¿P413 mejora movie a costa de UI?** Sí como resultado observable A/B. Corrige un campo original y deja otra parte del contrato incompleta.
5. **¿Trade-off o bugs independientes?** Trade-off observable entre síntomas; misma inicialización incompleta puede vincular UI/GS, pero mecanismo UI exacto aún UNKNOWN. MPEG tardío es independiente.
6. **¿Mejor visual?** RUN063 provisional por cobertura, no por ratio probado; no libre de errores.
7. **¿Mejor semántico?** RUN071/P413 en FBP movie/slot1; ninguno reproduce UI original completa.
8. **¿P416 confirmado?** No, REFUTED en la afirmación central. Se omitió la cola condicional.
9. **¿Host copy entre clear/redraw probado?** No hay prueba para una captura concreta.
10. **¿Sigue hipótesis?** Sí; PRESENTATION_CLEAR_WINDOW_CAUSAL=UNKNOWN.
11. **¿Primera diferencia UI/movie?** UI conserva la omisión de SDK en disp1 y FRAME slot0; movie recibe parches Main_init en esos campos. Además UI rasteriza sprites texturizados, movie sube IMAGE directo.
12. **¿Flicker y black mismo mecanismo?** UNKNOWN; no basta sensibilidad común a fbp1.
13. **¿Se combinan experimentos?** No los candidatos 072/073/074 en source vigente.
14. **¿Producción?** P41 DISPFB0=0 + P413 slot1 FRAME=0, infraestructura P3 previa; no nuevo fix P42.
15. **¿Revertidos?** zbufAddr, frameSize en slot1 y latch P417; diagnósticos temporales GS/MPEG.
16. **¿Bug visual arreglado realmente?** Corte inferior sistemático por FRAME E0; y selección DISPFB0 incorrecta anterior. No rendering global.
17. **¿Qué sigue abierto?** UI pre-PSS, flicker/ghosting no unificado, daño MPEG tardío; otros defectos de título no resueltos.
18. **¿Qué conclusión retractar?** Principalmente el oracle universal/single-buffer P416 y la exclusión de GS anterior al host; además los calificativos globales de mejora.
19. **¿Qué falta antes de código?** Confirmar el estado ORIGINAL UI al retornar la llamada, incluidos los tres campos que ningún candidato reproduce, y fijar el contrato completo como gate; no asumir causalidad host exclusiva.
20. **¿Siguiente único experimento/prompt?** El oracle mínimo siguiente, sin modificar presentación ni probar otro valor arbitrario.

## 15. Único siguiente prompt recomendado

**P4.1.9 — ORIGINAL_UI_DBUFF_CONDITIONAL_TAIL_ORACLE**.

Una medición PCSX2 del mismo ELF, inmediatamente en `Main_init` PC `0x0015BB08` (continuación de la primera llamada al wrapper UI). Leer `gparam` y los campos UI de ambos slots/contextos: `0x740780`, `0x7407B8`, `0x7407F0`, `0x740870`, `0x740960`, `0x7409E0`. Contrastar con la predicción estática `{DISPFB:1000,1038; FRAME:80038,80038,80000,80000}` y los dumps HLE existentes. No requiere trazar docenas de paquetes ni cambiar el runtime. No ejecutado en P418.

Objetivo: confirmar en ejecución original que la cola observada se toma y que esos campos sobreviven al wrapper. Si coincide, la investigación siguiente parte de un contrato SDK completo en lugar de inventar un latch para un supuesto single-buffer. Si no coincide, localizar el primer supuesto estático incumplido antes de tocar código. **IMPLEMENTATION_READY: NO** para otro fix de producción autorizado por esta auditoría; el diseño del arreglo y su validación global no se adelantan a ese gate.

## 16. Cierre y estado final

Main/vendor HEADs sin cambios respecto a entrada. Vendor permanece limpio. Main conserva los diez informes no trackeados anteriores y añade solo este informe; ningún archivo tracked modificado. Derivados de análisis offline bajo `analysis/local/p418/`, excluidos de Git. No se reescribió la nota canónica ni ningún reporte previo.

No se lanzaron corridas, builds ni procesos del juego; no se tocó fuente, configuración, saves, drivers, MPEG, GS ni OpenGL. No se terminó ningún proceso. NO commit, NO push, NO clean, NO regenerate. CHECKPOINT_DECISION: **NO_COMMIT**.
