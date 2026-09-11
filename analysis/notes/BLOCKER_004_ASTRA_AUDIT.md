# BLOCKER_004 — Astra independent static audit

## 1. Veredicto general

**BLOCKER_004 sigue abierto.** La explicación «hay que poner bit 0x10 para
alcanzar el arranque del consumer» está REFUTADA por el control flow del ELF.
La explicación «el demux deja de aceptar bytes porque el consumer no vacía
la cola, y por eso no se llega a flush» está demostrada como mecanismo
condicional del código actual. No identifica por sí sola la primera divergencia.

La prioridad es distinguir **consumer no programado**, **programado pero no
despachado** y **despachado pero sin alcanzar GetPicture**. El candidato mejor
soportado sigue siendo una gate de programación, concretamente
`audioDecIsPreset(state)==0`, no bit 0x10. Esto NO demuestra una avería de audio,
ni rehabilita la hipótesis refutada de audioDecReset ejecutándose cada tick.

### Alcance y procedencia

- Auditoría exclusivamente estática; ninguna ejecución del juego, build,
  regenerate, cambio funcional, commit ni push.
- ELF leído y recalculado: `original/SLES_503.58`, PAL Europe v1.02,
  entry `0x00100008`, CRC32 `77654AD2`, SHA256
  `d0753a6b3b2f00802a50758a872d8cf051725aa31c839aa8894eee30ce58bab4`.
  Todos los PCs y símbolos DMC de esta nota corresponden a ese ELF.
- Main HEAD observado: `888f22ef7a3dc685815e9995386100a91fd94c32`.
  Vendor HEAD: `0f76399b1aa35b018a89477c078924775d35b79c`, coincidente con el
  patched_commit del lock materializado. Baseline: `14b1e5cb39b4af7e6fc12f9a29fdc751efde49d7`.
- Había cambios previos en `analysis/notes/BLOCKER_004_pss_video_output.md`,
  `upstream.lock.json`, patch no trackeado `BUILD_ffmpeg_private_link_scope.patch`
  y, dentro de vendor, `Audio.cpp` y `MPEG.cpp`. Se preservaron íntegramente.
  El código generado contiene instrumentación local previa; no se modificó.
- Se leyeron AGENTS, README, UPSTREAM y las notas relevantes. README/UPSTREAM
  conservan estados históricos que no se toman como autoridad del runtime actual.
- Fuentes primarias: ELF/.symtab, generated activo en
  `analysis/local/symtabfirst/generated`, `ps2xRuntime/src/lib/Kernel/Stubs/MPEG.cpp`,
  `Kernel/EeScheduler.cpp`, `Kernel/Syscalls/Thread.cpp`, `src/lib/ps2_runtime.cpp`
  y el retorno de movie start en `ps2xIOP/src/modules/cdmodule.cpp`.
- `analysis/local/astra_blocker004_audit/check_static.py` permite repetir la
  comparación de palabras anotadas en generated con el ELF. No hubo discrepancias
  en las funciones examinadas. Esto verifica correspondencia de las anotaciones,
  NO prueba equivalencia formal del C++, ni identidad del ejecutable utilizado
  en corridas anteriores. Las ramas críticas se revisaron además manualmente.
- Las observaciones PCSX2 y las corridas anteriores citadas aquí son evidencia
  **aportada** por el usuario/notas, no nuevas mediciones de esta auditoría. No
  se convierte una transcripción histórica en una observación propia.

## 2. Hechos nuevos y correcciones históricas

| Clasificación | Hallazgo y evidencia |
| --- | --- |
| HECHO | El `jal sceMpegDemuxPssRing` es **0x1CE6A0**; **0x1CE6A8** es el retorno/copia a s1. No intercambiar ambas direcciones. |
| HECHO | `Task_execute(3, MpegMovieDecode)` está en **0x1CE72C**, ANTES del gate de remaining en 0x1CE738. El salto 0x1CE750→0x1CE5B4 vuelve a recorrer sus gates. |
| HECHO | `s2` se inicializa a cero en 0x1CE53C y se pone a uno en 0x1CE734. Es un latch de la invocación lógica de Movie_ctrl, no un local que deba reiniciarse en cada tick. |
| HECHO | `Task_suspend(0)` sólo modifica el estado del slot 0; no duerme al Movie_ctrl que lo invoca. No es una espera bloqueante entre las gates y Task_execute. |
| HECHO | En MpegMovieDecode, `lui ...,0x4000` y `lui ...,0x8000` construyen **0x40000000** y **0x80000000**. La sección 6.5 previa y el resumen de flags bajos son incorrectos. |
| HECHO | 0x007406A4 y 0x009F2890 son direcciones de RAM guest, no registros MMIO IPU. En esta ruta se usan para la decisión de input y el valor pasado a changeInputVolume, respectivamente. La interpretación histórica como registros hardware no está sustentada. |
| HECHO | videoDecFlush devuelve 0 si el espacio obtenido es **<4**, y escribe el marcador si es **>=4**. La nota previa invirtió la condición. |
| HECHO | videoDecIsFlushed llama a viBufCount cuando IsRefBuffEmpty es **distinto de cero**, no cuando es cero. La nota previa invirtió esa rama. |

La vieja hipótesis del «ciclo cerrado real» se apoyaba en bit 0x10 escrito por el
decoder y en la ausencia de flush. La nueva comprobación de 0x1CE6D4–0x1CE750
refuta la arista que situaba Task_execute después del escape del subloop.
Se conserva la nota histórica sin reescribirla; esta nota la corrige explícitamente.

### Flush, comprobación independiente

`videoDecFlush` (0x47B860–0x47B914) obtiene dos áreas mediante
`viBufBeginPut` (0x47AE90). Si sus tamaños suman menos de cuatro, retorna 0.
En caso contrario usa Copy2area y viBufEndPut y retorna 1. Los bytes originales
en 0x564EC0 son `00 00 01 B7`: el uso de lwc1/swc1 transporta esos bits;
no demuestra «matemática de coma flotante». Es un marcador de cierre del
elementary stream de vídeo, no una operación que saque imágenes de decodedFrames.

El retorno literal de `videoDecIsFlushed` (0x47B970–0x47B9B0) es:

```c
if (sceMpegIsRefBuffEmpty(state + 0x278) == 0) return 0;
return viBufCount(state) != 0;
```

El nombre no autoriza a convertir el segundo `!=0` en `==0`.
viBufCount obtiene `(state[0x2C4] << 11) + state[0x2C8]`, protegido por el
semáforo de state+0x2F8. Su adecuación a un HLE sin DMA real es otro asunto,
posterior al gate actual. Movie_ctrl repite videoDecFlush en 0x1CE76C–0x1CE780;
la comprobación de IsFlushed en 0x1CE7B0 NO tiene el bucle que algunos resúmenes
le atribuyen: tras el posible Task_sleep de 0x1CE7D0 sigue a 0x1CE7D8.

## 3. Significado de state+0x00 y state+0x18

**HECHO:** Movie_set guarda en state+0x00 el valor desreferenciado que devuelve
CdMovieRead (0x1CF050), procedente del receive buffer de fno9. En el camino
de modo 0x80 escribe también +0x14 y +0x18 en 0x1CE44C–0x1CE454 y activa
flags 0x08000400. En el camino que espera completion, Movie_ctrl llama
CdGetEETransSize/fno8 en 0x1CE578 y copia el resultado a +0x00, +0x18 y +0x14.

**HECHO:** El HLE de fno9 devuelve el tamaño total request+8. La entrada ELF
`0x507C50 + 0x22E*8` contiene LBA `0x1F62B8`, tamaño `0x1A5C004` =
**27639812 bytes**. Para esta ruta, +0x00 es el presupuesto/tamaño total del
PSS, no frames, paquetes ni bytes ya decodificados. No se vuelve a auditar aquí
todo el protocolo IRX de BLOCKER_003 ni se universaliza fno8 fuera de este contexto.

| Campo | Significado en este streaming | Actualización relevante |
| --- | --- | --- |
| +0x00 | Tamaño total/budget inicial | CdMovieRead o resultado fno8 tras espera |
| +0x04 / +0x10 | Base / capacidad del ring EE | MovieBufferInit: capacidad 0x50000 |
| +0x08 / +0x0C | Posición de escritura / bytes ocupados del ring | ReadFileSetAddr y ReadDataSetAddr |
| +0x14 | Bytes pendientes de traer al ring | Resta retorno de CdMovieTrans o CdGetEETransSize, en 0x1CE664–0x1CE66C |
| +0x18 | Bytes del presupuesto pendientes de aceptar por el demux | Resta retorno de sceMpegDemuxPssRing, en 0x1CE6B8–0x1CE6C0 |

Es correcto decir **«bytes restantes del PSS por entregar al demux»**. Decir
solamente «bytes restantes» permite confundir +0x14 con +0x18. No significa
«bytes pendientes de leer del CD» ni «vídeo pendiente de decodificar/presentar».

En una ejecución coherente, con retornos válidos y sin reset/abort, se deriva:

```text
state+0x18 = tamaño inicial - suma(retornos demux)
state+0x14 = tamaño inicial - suma(transferencias al ring)
state+0x18 = state+0x14 + ocupación del ring
```

La última igualdad presupone que los helpers no tuvieron que limitar retornos
inválidos: ReadDataSetAddr limita a la ocupación, pero Movie_ctrl resta el **s1
original**, no el retorno corregido del helper. No se ha observado aquí ese error.
La coincidencia temporal de +0x18 y `CDMODULE remaining` es compatible con vaciar
el ring a la misma velocidad que se llena; no demuestra que sean el mismo contador.

`Movie_task_init` (0x1CE4F0–0x1CE50C) limpia 0x1A0 bytes desde
0x87D720 (=state+0xA0) y un byte global en 0x741D0C. **No limpia +0x18**.

## 4. Relación exacta demux / backpressure / remaining

La secuencia ELF/generated es inequívoca:

```text
0x1CE680 ReadDataGetAddr(state, &data) -> available
0x1CE688 si available <= 0, saltar al envío auxiliar/gates
0x1CE6A0 sceMpegDemuxPssRing(state+0x278, data, available,
                            state[+4], state[+0x10])
0x1CE6A8 s1 = v0
0x1CE6B0 ReadDataSetAddr(state, s1)
0x1CE6B8 v0 = state[+0x18]
0x1CE6BC v0 -= s1
0x1CE6C0 state[+0x18] = v0
```

**Respuesta A/B/C/D: B**, bytes aceptados/consumidos de la entrada PSS por
sceMpegDemuxPssRing. No A ni C. El SDK original también separa aceptación
de paquetes y callbacks de la obtención de pictures: en 0x1092CC/0x109394
invoca callbacks, conserva su resultado en s4 y sólo avanza el cursor confirmado
si procede (0x1093A0–0x1093B4); retorna el cursor en bytes guardado en sp+0xAC,
en 0x10943C. No es un contador de salida de GetPicture.

**HECHO del runtime actual:** MPEG.cpp:1428 define backpressure como
`!currentCdStreamEofSeen && decodedFrames.size() >= 8`.
En sceMpegDemuxPssRing (2179–2271), si es true, devuelve **0**, sin append,
sin consumir el ring y sin bloquear al thread en esa llamada. Si no, el retorno
es lo copiado por appendGuestRingBytes (1539), que admite wrapping del ring y
llama a appendGuestBytes. Es aceptación al buffer/parser host, no necesariamente
un paquete completo procesado ni un frame producido. El quinto argumento viene
de t0 y la implementación readAbiArg4 lo usa en este caso; no falta ringSize.

processPssBuffer entrega payload de vídeo a feedElementaryStream y éste al
decoder FFmpeg. GetPicture retira la cabeza de decodedFrames en MPEG.cpp:2406–2407.
El límite 8 es un umbral de backpressure entre llamadas, no un tope matemático
inviolable: una sola alimentación puede producir más de ocho imágenes.

**INFERENCIA fuertemente soportada:** si la misma cola permanece llena, nadie
la consume, no hay EOF/reset y +0x18 es positivo, los siguientes retornos demux
son cero y +0x18 se congela. El CD puede todavía llenar el espacio libre del
ring; después también queda frenado. Por eso un offset CD cercano a 0xC0000 no
debe identificarse sin más con el último byte aceptado por el demux.

**No es una inevitabilidad incondicional:** EOF desactiva esta backpressure;
reset/cambio de sesión puede vaciar la cola, y un abort/bit0x10 puede salir del
subloop sin que +0x18 llegue a cero. Tampoco el test exige cero exacto:
usa comparación con signo `<5`, incluyendo 1–4 y valores negativos.

La captura PCSX2 con v0=0 en 0x1CE73C es perfectamente compatible con una
reproducción normal cuyo producer acabó entregando todo al demux. **No prueba**
que ése fuera el instante de la captura, ni que todos los frames se hubiesen
presentado. Sin alinear sesión/avance, contrastar ese cero con 27 MB no localiza
una divergencia. Observar millones en PCSX2 a mitad de reproducción tampoco
implicaría volver a la teoría de bit0x10.

## 5. Control flow hacia MpegMovieDecode

Reconstrucción de Movie_ctrl, incluidos caminos de entrada:

1. 0x1CE538: si falta flags&0x400, espera CdReadCheck mediante Task_sleep(1),
   obtiene el tamaño y prepara los contadores. s2=0 se ejecuta en el delay slot
   de ese branch tanto si se toma como si no.
2. 0x1CE590: con flags&0x200 salta al test +0x18. Sin él marca Task_suspend(0)
   y alcanza el mismo test. Si remaining>=5 y bit0x10=0, entra al subloop.
3. 0x1CE5B4: obtiene espacio libre. Si +0x14>0 y hay al menos 0x10000 bytes,
   flags&0x08000000 elige CdMovieTrans con modo 0x80; alternativamente
   flags&0x200 elige transferencia con espera y consulta de tamaño.
   Publica bytes en el ring y descuenta +0x14. No tener espacio no omite las gates.
4. 0x1CE674: Task_sleep(1); luego consulta datos y llama al demux si available>0.
   Aunque demux devuelva cero, continúa a 0x1CE6CC.
5. 0x1CE6CC: audioDecSendToIOP(state), **antes** de la programación del consumer.
6. 0x1CE6D4: si s2!=0, omite reprogramar el task.
7. 0x1CE6DC/0x1CE6E4: si audioDecIsPreset(state)==0, omite programarlo.
8. 0x1CE6F8: si falta flags&0x08000000, omite programarlo.
9. 0x1CE704: si flags&0x200, marca Task_suspend(0). Esta llamada **no bloquea**.
10. 0x1CE71C: Fade_kill(0); después materializa 0x47A760 y llama Task_execute(3)
    en 0x1CE72C. s2=1 en 0x1CE734.
11. 0x1CE738: remaining<5 conduce a flush; si no, bit0x10 decide entre salir
    a flush o repetir el subloop. **El arranque del decoder ya se evaluó antes.**

El gate de preset es exactamente:

```c
descriptor = *(uint32_t *)(state + 0x70);
preset = (*(uint16_t *)(descriptor + 2) & 0x4000) != 0
      || (int32_t)state[0x58] >= (int32_t)state[0x4C];
```

Aquí 0x58=88 decimal y 0x4C=76 decimal. No confundir esos offsets ni los flags
de 16 bits del descriptor con los flags de 32 bits de state+0x2FC.

**HECHO:** con los operandos aportados, `0x8000 & 0x4000 == 0` y
`0 < 24576`, la función retorna cero y bloquea la programación aunque FFmpeg
produzca imágenes. Ésta es una condición concreta que el ELF mantiene, no una
teoría de audio hardware. La ausencia de Pause/Resume/Remote no la refuta.

**INFERENCIA:** esos valores y la ausencia histórica de pump-start/decode-task
favorecen «no programado». No constituyen aún una comparación PCSX2/RECOMP
sincronizada de esa gate. El origen del preset falso sigue abierto.

## 6. Task runtime y por qué GetPicture puede seguir en cero

### Programación y cooperación

**HECHO:** Task_execute no invoca el function pointer ni crea un host thread.
Escribe la descripción del slot `0x7401B0 + (taskId & 0xFF)*0x48`, entry en
slot+0x1C, stack/configuración y prioridad `0x20+taskId`, y estado 2 en slot+8.
Para task3: slot **0x740288**, entry **0x7402A4**, estado **0x740290**,
prioridad 35. Para task2: prioridad 34.

`scheduler_main` (0x19F730–0x19F81C) recorre cuatro slots. Para estado 2 llama
CreateThread en 0x19F788 y StartThread en 0x19F798. Para estado 1 decrementa
el contador de sueño y llama WakeupThread cuando vence. Espera el semáforo
común, permitiendo que el task actúe antes de seguir el recorrido.

Task_sleep escribe el contador y estado del slot actual, señala el semáforo y
llama SleepThread. Task_exit señala el semáforo y usa ExitDeleteThread.
Movie_ctrl y MpegMovieDecode son, por tanto, ejecuciones guest separadas que
cooperan; no una llamada directa que deba terminar para que continúe el producer.

**HECHO del runtime:** EeScheduler::startThread inicializa PC/stack/prioridad de
la tarea; sleepCurrent/blockCurrent aparcan el thread con su contexto y transfieren
al dispatcher. wakeupThread libera la espera o acumula un wakeup anticipado.
El dispatcher ejecuta activeContext y captura EeDispatcherTransfer. El generated
de Task_sleep y Movie_ctrl tiene continuaciones para los PCs de retorno.
No hay en el camino revisado un reinicio obligatorio de s2 en cada tick.

**Veredicto scheduler:** no se ha demostrado un bug. Un task3 no despachado
tras tener su slot en estado 2, o un error de CreateThread/StartThread, sería
evidencia concreta a buscar. Sin demostrar antes la llamada Task_execute,
atribuirle el bloqueo al scheduler invierte el orden causal. Las prioridades y
las esperas son relevantes para verificarlo, no prueba de fallo por sí mismas.

### Salidas antes de GetPicture

MpegMovieDecode puede haber entrado sin llegar a GetPicture:

- **Inicialización:** primero llama MpegMovieDecodeInit (0x47A6E0). Una espera
  o fallo dentro de esta ruta impediría continuar. No hay medición que lo demuestre.
- **Fin:** IsEnd en 0x47A78C retorna no cero → branch 0x47A794 a 0x47AA70.
  Al final de cada vuelta también se consulta IsEnd (0x47AA60), pero esa vuelta
  ya pasó por GetPicture; no explica cero llamadas desde el inicio por sí sola.
- **Abort con fade completado:** flags&**0x80000000**, en 0x47A884–0x47A88C,
  y Fade_status(0)&4 en 0x47A8BC–0x47A8C0 → CdMovieExit y 0x47AA70.
  Sin ambas condiciones se alcanza GetPicture.
- flags&**0x40000000** sólo evita la detección de input para iniciar el abort.
  No salta por sí solo GetPicture. La detección usa movieId+0x6C y RAM
  0x7406A4: máscara 0x9F0 para ids 0–4, 0x100 para los demás. Puede activar
  0x80000000 y el fade; de ahí que deban medirse también input/flags antes
  de la evaluación y no sólo sus valores de entrada a la función.

Si las llamadas previas retornan y no se toma ninguna salida, el `jal` de
0x47A8E4 es obligatorio. No hay aquí una rama que duerma indefinidamente antes
de ese primer jal como comportamiento normal del cuerpo del decoder.
Las esperas internas de **GetPicture** por frames/PTS requieren haber entrado
en GetPicture; por tanto no explican un contador ENTRY=0 fiable.

### Salvedad HLE concreta, no causa actual demostrada

MpegMovieDecodeInit llama sceMpegInit en 0x47A6EC. MPEG.cpp:2490 llama
resetMpegStubStateUnlocked, que borra playbackByMpeg y callbacksByMpeg, aunque
preserva los contadores/flags globales CD. MovieBufferInit ya había creado el
objeto MPEG y MovieMpegInit registrado callbacks antes de programar al consumer.
**HECHO:** si alcanza esa inicialización, el HLE puede borrar imágenes y callbacks
precargados. **HIPÓTESIS:** posible incompatibilidad posterior del ciclo de vida;
no prueba del bloqueo actual si el task ni entra. No corregirla aquí.

En consecuencia, «IsEnd no puede devolver 1 porque hay ocho frames» necesita
comprobar que siguen en **el mismo objeto** después de la inicialización.
Con la cola efectivamente no vacía IsEnd devuelve 0 en este HLE; con EOF falso
también devuelve 0. El simple contador histórico de ocho no sustituye esos datos.

## 7. Veredicto sobre bit 0x10

**REFUTADO como requisito de arranque.** Sirve para salir del producer loop
sin exigir remaining<5. MpegMovieDecode lo escribe al terminar, tanto tras
IsEnd como tras abort/fade; no se limita estáticamente a abort.

El breakpoint PCSX2 aportado demuestra al menos su empleo en abort. Que no
saltase durante la ventana de playback normal no demuestra que nunca se escriba
al terminar naturalmente. Ninguna de las dos observaciones respalda forzarlo
para arrancar el vídeo. No se recomienda tocar ese bit.

## 8. Primera divergencia más probable y límites de las mediciones

**INFERENCIA:** el consumer no se programa porque no supera el gate de
0x1CE6E4. Es anterior al llenado de decodedFrames y puede seguir fallando
mientras el producer trabaja. La ausencia de consumo provoca después la
backpressure y el contador +0x18 congelado: éste es downstream.

No está localizada todavía la **primera divergencia comparativa**. Falta mostrar
en la misma sesión/fase que PCSX2 abre esa gate y RECOMP no, y qué dato diverge
primero. No se afirma que audio HLE, callbacks, timing o scheduler sean su causa.

Limitaciones concretas de la instrumentación existente:

- `[MOVIE:pump-gate]` está DESPUÉS de los branches s2!=0 y preset==0.
  Su ausencia no permite distinguirlos por sí sola.
- El log `[AUDIO:preset]` está en el camino de comparación; la salida temprana
  por bit0x4000 salta a label_47a14c y omite ese bloque de diagnóstico.
  No es un contador completo de todas las entradas/salidas.
- Sus diez primeras líneas y cambios observados no prueban sin más «siempre 0
  en todas las sesiones». Tampoco registra state/PC de llamador en cada línea.
- `[MOVIE:decode-task] ENTRY` está después del switch de continuaciones:
  contabiliza entradas lógicas, no cada reanudación host. Esto es apropiado para
  detectar un arranque si el log cubre la sesión desde antes de iniciarla.
- El log gate18 se emite tras ejecutar slti; la captura PCSX2 está detenida
  antes de esa instrucción. El v0 cargado es comparable; at puede no serlo todavía.
- No se verificó en esta auditoría que el exe de cada log contenga exactamente
  todos los puntos de instrumentación que hoy existen en generated.

### Diferencia adicional observable del demux HLE

El original usa el retorno de los callbacks para decidir cuánto confirma como
consumido (0x1092D4, 0x10939C, 0x1093A0). El HLE copia bytes, encola callbacks
mediante GuestInvocation y su onComplete sólo libera el buffer; **no usa el
retorno guest para limitar consumed** (MPEG.cpp:1615–1669).

**HECHO:** hay una diferencia de aceptación/ordenación respecto al SDK original.
**HIPÓTESIS:** puede afectar a la preparación de buffers que abren la gate.
No está demostrado su impacto en esta sesión de DMC; no basta para etiquetar
«scheduler bug» ni para afirmar que los payloads/callbacks sean incorrectos.
Los ocho frames producidos tampoco validan esos efectos guest auxiliares.

## 9. Hipótesis rankeadas

| Orden | Hipótesis | Soporte / condición para subir o bajar de prioridad |
| --- | --- | --- |
| 1 | No se programa el decoder por preset falso en 0x1CE6E4 | ELF + operandos históricos exactos + ausencia aportada de pump-start/decode-task. Falta comparación sincronizada; no atribuir todavía el origen a audio. |
| 2 | Preparación guest/callbacks diverge aunque FFmpeg decode | Producer y callbacks no son la misma operación. Diferencia real en retornos y ordenación HLE; impacto sobre los contadores todavía sin medir. |
| 3 | Otra gate/latch de programación falla, o los logs mezclan sesiones/builds | Medir s2 y flags en la misma iteración. 0x08000400 sí contiene readyBit y no 0x200; no apunta a esos flags en la muestra aportada. |
| 4 | Task3 se programa pero no arranca por despacho/creación | Posible, no demostrado. Exige primero Task_execute y slot3=2 observados; después resultados CreateThread/StartThread. |
| 5 | El task entra pero termina/se queda antes de GetPicture | Rutas IsEnd, init y abort identificadas. Los logs aportados de ENTRY=0 la debilitan si corresponden al exe/sesión correctos. |

La pérdida potencial de buffers por sceMpegInit es un riesgo estático concreto
para cuando arranque el task, no una explicación ya establecida de ENTRY=0.
GS/render no es una hipótesis prioritaria mientras no exista una llamada de
consumo/presentación que permita comparar esa etapa.

## 10. Siguiente experimento mínimo

**No ejecutado.** Una comparación corta de **la misma movie/id y descriptor**,
desde el comienzo y sin input de abort, en la primera apertura del consumer en
PCSX2 y en el intervalo equivalente del RECOMP. No basta otro snapshot aislado
de remaining durante una fase temporal desconocida.

1. En 0x1CE6D4 y 0x1CE6E4 registrar sesión, state, s2, v0, descriptor+2,
   state+0x58/+0x4C y flags+0x2FC. Correlacionar con el primer 0x1CE72C y
   0x47A760. **Ésta es la medición de mayor valor**, sin forzar nada.
2. En esas mismas vueltas, capturar retorno demux en 0x1CE6A8, +0x14,
   +0x18 y ocupación +0x0C; en RECOMP, cola y EOF del mismo objeto MPEG.
   Esto separa la gate inicial de su realimentación posterior por backpressure.
3. Sólo si Task_execute ocurre sin ENTRY: inspeccionar slot3 0x740288,
   CreateThread/StartThread y reanudaciones. Sólo si hay ENTRY sin GetPicture:
   comprobar retorno de init/IsEnd, flags **0x80000000**, input y Fade_status&4.

Si se confirma que PCSX2 abre preset y RECOMP no, el siguiente análisis debe
seguir los writers/callbacks que producen **ese dato divergente**, no abrir una
investigación genérica de audio ni forzar el consumer. No aumentar el límite,
suprimir backpressure, poner remaining a cero o activar bit0x10.

**Conclusión:** sí al mecanismo downstream propuesto por el usuario, con sus
precondiciones explícitas; no al retorno automático de la explicación a bit0x10;
no hay aún fundamento para un fix ni para dar por demostrado un bug de scheduler.

## 11. Actualización posterior — hallazgos que superan esta auditoría (no reescribe lo anterior)

El experimento mínimo propuesto en la sección 10 de esta auditoría se
ejecutó en la práctica (serie de prompts P3-P3.4, documentada en
`BLOCKER_004_pss_video_output.md`, sección 12), con resultado
directamente relevante para el punto 1 de esa sección: **sí se confirmó
que ORIGINAL abre el preset y RECOMP no**, con la secuencia dinámica
completa (`+0x58: 0→0x3C00→0x5C00→0x6000` seguido de `0x1CE6EC v0=1`).
Por instrucción explícita de esta misma auditoría ("si se confirma...
seguir los writers/callbacks que producen ese dato divergente"), la
investigación posterior rastreó exactamente esos writers y localizó la
primera divergencia real: en RECOMP, `MPEG.cpp::dispatchGuestStreamCallback`
nunca encola la invocación de `StrPcmCallBack` porque
`PS2Runtime::guestMalloc` devuelve `0` de forma determinista — el guest
heap sintético de RECOMP está colapsado a capacidad cero
(`base=0,limit=0`) desde el arranque, por un problema de *sizing* en
`resetGuestHeapLocked`/`ensureGuestHeapInitializedLocked`
(`ps2_runtime.cpp`), confirmado tanto dinámicamente (16/16 fallos de
`guestMalloc` instrumentados) como estáticamente (cálculo directo desde
los `PT_LOAD` reales de `original/SLES_503.58`). Es infraestructura de
host (RECOMP), no una divergencia de traducción MIPS/R5900 ni del
código del juego — coincide con el veredicto de esta auditoría de que
no había fundamento para un "bug de scheduler": el scheduler nunca llega
a intervenir, porque la invocación nunca se encola.

Ver `BLOCKER_004_pss_video_output.md` sección 12 (especialmente 12.5-12.7)
para la tabla completa de evidencia, los números exactos y las
hipótesis que quedan abiertas (origen de los 14 segmentos `PT_LOAD`
diminutos en `0x1f00000` que disparan el colapso). Esta sección no
reemplaza ni reescribe el contenido de las secciones 1-10 anteriores;
las correcciones ahí documentadas (tabla de la sección 2, la refutación
de bit0x10, etc.) siguen siendo válidas como hallazgos propios de esa
auditoría.
