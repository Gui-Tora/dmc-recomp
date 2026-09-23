# ARCH_AUDIT_002 — SOL_CROSS_IMPLEMENTATION_MPEG_RENDER_ARCHITECTURE_AUDIT

## Alcance, identidad y checkpoint

Auditoría técnica comparativa **READ-ONLY**. No contiene un fix ni propone
modificar el C++ generado. Las direcciones DMC de este documento pertenecen
exclusivamente a:

| Campo | Valor |
|---|---|
| ELF | `original/SLES_503.58` |
| Región / revisión | PAL Europa v1.02 |
| SHA-256 | `d0753a6b3b2f00802a50758a872d8cf051725aa31c839aa8894eee30ce58bab4` |
| Entry | `0x00100008` |
| CRC32 | `77654AD2` |
| Main auditado | `65dd4aad9bb9d79e06d9ff9e90bb32c78714ecd1` |
| Vendor auditado | `0efd17c3cdb834ac13c087dcb2bd947d9fc033b3` |
| Upstream baseline de `upstream.lock.json` | `14b1e5cb39b4af7e6fc12f9a29fdc751efde49d7` |

**HECHO:** al comienzo de la fase, `master` estaba en el checkpoint esperado,
`vendor/PS2Recomp` estaba limpio y en el commit esperado. El único elemento no
seguido de main era `.claude/`, que esta auditoría no tocó.

**HECHO:** P4.2 produjo un ES canónico de 23.654.877 bytes, 5.808 payloads y
SHA-256 `b2e29f64807a8336e2181788bbd2a343af4d1fbdc9cd94ee9de0dbd5a4ecfb71`.
El ES canónico decodifica sin corrupción offline. El ES de ejecución pierde
exactamente 4.063 bytes en `0x1FC301` y vuelve a coincidir al saltar esos 4.063
bytes canónicos.

**RETRACTADO:** saturación simple por capacidad en el momento HLE de enqueue.
P4.2.1 hizo funcionar 101/101 ciclos STALL/RETRY y no movió ni eliminó la
omisión histórica.

**HECHO:** P4.2.2 observó que el callback que pierde el payload se encoló con 14
callbacks de vídeo pendientes. Antes de ejecutarse, los 13 anteriores
comprometieron 52.973 bytes. La profundidad máxima observada fue 16; se
encolaron y terminaron 5.808/5.808 callbacks.

## Resultado ejecutivo y respuestas requeridas

1. **¿Por qué se acumulan unos 14 callbacks?**  
   **HECHO:** una llamada HLE a `processPssBuffer` recorre todos los PES
   completos presentes en el bloque PSS, acumula eventos, borra cada prefijo y
   solo al salir despacha los eventos con `queueInvocation`. El transporte DMC
   entrega bloques de `0x10000` bytes. Ese bloque contiene habitualmente unas
   12–16 unidades PES multiplexadas. El scheduler no puede ejecutar un callback
   guest dentro del bucle HLE ya terminado. **INFERENCIA:** 14 procede de la
   composición concreta del bloque y del alineamiento del stream en ese punto;
   no es un quantum del SDK ni un límite de viBuf.

2. **¿Es compatible con DMC original?**  
   **HECHO:** no. El wrapper original llama al callback, conserva su retorno y
   condiciona el cursor consumido a ese retorno. El runtime conserva el orden,
   pero devuelve y avanza antes de conocer la terminación.

3. **¿Límite original más probable?**  
   **HECHO:** retorno del callback dentro de `sceMpegDemuxPssRing`, antes de
   confirmar el avance del cursor PSS. Para `StrM2vCallBack`, `viBufEndPut`
   ocurre antes de ese retorno. La frontera exacta es **D + A**: terminar el
   callback y solo entonces avanzar más allá del PES.

4. **¿Completion, reservation o demand-driven?**  
   **INFERENCIA:** completion es la abstracción mínima y fiel para P4.2.3.
   Demand-driven representa mejor el hardware nativo a largo plazo, pero exige
   modelar una interfaz IPU/DMAC que el HLE actual no expone. Reservation puede
   optimizar pipeline, pero no sustituye el contrato de completion.

5. **¿Qué enseña ps2sdk/libmpeg?**  
   **HECHO:** el consumidor pide datos. `_ipu_sync` detecta que el IPU necesita
   bits, `_req_data` llama al proveedor solo si no hay una transferencia DMA4
   activa, y la muestra `SetDMA` programa un bloque de 2.048 bytes. FIFO IPU,
   `D4_QWC` y finalización DMA forman backpressure natural. No existe una ráfaga
   de 14 escrituras futuras sin consumir.

6. **¿Qué enseña el plugin VLC?**  
   **HECHO:** confirma framing PSS/PES, IDs `0xE0–0xEF`, PTS/DTS, padding,
   system headers y audio privado Sony. La propiedad útil es que una unidad PES
   se entrega como bloque completo con ownership explícito. No modela viBuf ni
   callback guest y no debe usarse para deducir sincronización PS2.

7. **¿Qué enseña upstream #178?**  
   **HECHO:** reconoce el mismo hueco de contexto: un hilo host posee bytes PSS
   correctos pero no puede invocar callbacks guest. Sugiere una cola por
   `MpegPlaybackState` drenada en un syscall MPEG posterior. **INFERENCIA:** esa
   cola es un mecanismo útil de transporte de eventos, pero por sí sola no
   resuelve P4.2.2; debe retener ownership y hacer que el syscall espere el
   resultado antes de confirmar consumo.

8. **¿Qué enseña upstream #175?**  
   **HECHO:** documenta otro desacople temporal: decode host casi instantáneo y
   presentación guest esperada a ritmo del stream. Su pacing de
   `sceMpegGetPicture` actúa sobre entrega de frames. **INFERENCIA:** prueba que
   producción, decode, entrega y presentación deben controlarse por separado;
   no arregla el avance anticipado del demux.

9. **¿Es deficiente el parser PSS?**  
   **HECHO:** no materialmente para el PSS DMC observado. Retiene paquetes
   incompletos, produce el ES canónico exacto offline y cubre más casos de
   fragmentación que el plugin VLC. Sí tiene simplificaciones de tipado de audio
   privado y de política de final truncado que merecen endurecimiento separado.

10. **¿Es deficiente el decoder MPEG?**  
    **HECHO:** FFmpeg decodifica limpio el ES canónico. No hay evidencia de un
    defecto material del codec para este stream. Hay simplificaciones de
    read-ahead, formato de salida y espejo guest, pero la corrupción conocida
    entra al decoder ya presente en el ES.

11. **¿Capa primaria?**  
    **HECHO:** scheduling/ownership que causa un defecto de transporte. Decode
    y presentación son posteriores a la primera divergencia.

12. **¿Separar decoder host y viBuf guest?**  
    **INFERENCIA:** sí, si ambos derivan de un ledger canónico único y todos los
    efectos guest observables se mantienen. Dos cursores independientes sin un
    commit común pueden divergir y no constituyen una separación limpia.

13. **¿Deben seguir asíncronos los callbacks?**  
    **INFERENCIA:** condicional. La ejecución interna puede usar una
    continuación asíncrona del scheduler; el contrato visible de demux debe ser
    síncrono respecto a resultado y cursor.

14. **¿Ownership en `pssBuffer` hasta commit?**  
    **INFERENCIA:** sí. El runtime puede conservar un descriptor estable o una
    copia in-flight equivalente, pero no puede declarar consumido el PES antes
    del retorno guest.

15. **¿Reservar bytes pendientes?**  
    **INFERENCIA:** no es necesario con un único callback de vídeo no confirmado,
    derivado del contrato original. Si en el futuro se permite pipeline real,
    una reserva sería necesaria pero insuficiente y tendría que reproducir
    semántica de bloques, wrap y alineamiento.

16. **¿Demand-driven es factible?**  
    **INFERENCIA:** sí como rediseño de coste alto. Requiere hacer observable la
    demanda del consumidor y restaurar o sustituir con precisión los callbacks
    no-stream/IPU/DMAC. No es el cambio mínimo para P4.2.3.

17. **Mejor P4.2.3.**  
    `CALLBACK COMPLETION ACK + IN-FLIGHT PACKET OWNERSHIP`, implementado de modo
    que preserve la semántica síncrona del wrapper original.

18. **Segundo mejor.**  
    `SYNCHRONOUS VIDEO CALLBACK` inmediato/anidado, si el scheduler puede
    realizarlo sin reentrancia ni bloqueo del executor.

19. **Diseños rechazados para P4.2.3.**  
    reservation sola; cola drenada en syscall sin ACK; host feed directo;
    híbrido con dos estados independientes; límite mágico; pacing de
    `GetPicture` usado como backpressure de demux. Consumer-pull se aplaza por
    alcance, no se rechaza como arquitectura futura.

20. **Mejoras independientes.**  
    paridad FIELD/bob; semántica de callbacks MPEG no-stream; validación Sony
    private audio; auditoría de ownership/reset de `sceMpegInit`; límites de
    read-ahead; PMODE/AMOD; trazado de recursos CAPCOM.

21. **Vibración del warning.**  
    **INFERENCIA:** selección FIELD / bob del backend GS y fase de vblank. El
    warning/movie usa `SMODE2=1` mientras la UI estable usa `SMODE2=3`. No hay
    evidencia que la vincule a layout MPEG.

22. **Fase negra CAPCOM.**  
    **HECHO:** precede al primer stream de película y no hay un PSS CAPCOM en la
    tabla `/DATA/MOVIE`. **INFERENCIA:** es contenido normal dibujado por el
    juego que no se presenta, probablemente un recurso de imagen/GS. La causa
    exacta continúa `UNKNOWN`.

23. **Otros desajustes SDK/HLE.**  
    **HECHO:** callbacks MPEG no-stream no se despachan como en el programa
    original; P3.14.2 sustituye parte del retiro IPU/DMA con un espejo HLE.
    También están documentados un desacuerdo de ownership/reset en
    `sceMpegInit`, diferencias PMODE/AMOD y una semántica de FIELD aproximada.
    No se halló evidencia nueva de otro defecto activo en GIF/DMA.

24. **¿Listo para implementar?**  
    **INFERENCIA:** sí. La primera divergencia, el orden causal y el contrato del
    wrapper original están localizados. No hace falta un experimento adicional
    para decidir la frontera.

25. **Acción exacta siguiente.**  
    Implementar en una fase P4.2.3 aislada una transacción de demux reanudable
    que retenga un PES de vídeo hasta la terminación de `StrM2vCallBack`, capture
    `v0`, confirme el cursor con la regla original y después valide el ES
    completo con la matriz de este informe.

## 1. Tabla cruzada de pipelines

`SAME` y `DIFFERENT` comparan la propiedad semántica con DMC original, no una
igualdad de implementación.

| Stage | DMC original | Current dmc-recomp | ps2sdk libmpeg | VLC PSS plugin | upstream PS2Recomp |
|---|---|---|---|---|---|
| PSS source | **SAME:** CD module/ring guest | **SAME:** bytes CD/HLE a `pssBuffer` | **NOT APPLICABLE:** la API recibe ES, no demultiplexa PSS | **DIFFERENT:** stream VLC seek/read | **SAME/DIFFERENT:** guest demux; #178 añade feed CD host |
| demux owner | **SAME:** wrapper SDK en EE guest | **DIFFERENT:** parser host en `MPEG.cpp` | **NOT APPLICABLE** | **DIFFERENT:** módulo host VLC | **DIFFERENT:** HLE host; #178 también host thread |
| PES parsing | **SAME:** síncrono, cursor condicionado por callback | **DIFFERENT:** parse burst y borrado antes del callback | **NOT APPLICABLE** | **DIFFERENT:** lee una unidad completa y la entrega | **DIFFERENT:** parser HLE compartido; #178 no despacha callbacks |
| video ES ownership | **SAME:** fuente hasta aceptación del callback | **DIFFERENT:** se considera manejado al encolar | **DIFFERENT:** proveedor conserva fuente hasta solicitud y DMA | **DIFFERENT:** `block_t` transferido a `es_out_Send` | **DIFFERENT:** HLE; #178 staged/host feed sin efectos guest |
| input buffer | **SAME:** viBuf guest por bloques de 2.048 | **SAME/DIFFERENT:** viBuf guest existe, decoder host lo espeja | **DIFFERENT:** FIFO IPU + DMA4 + fuente de 2.048 | **NOT APPLICABLE:** buffers VLC | **DIFFERENT:** buffer host/guest según ruta |
| callback execution | **SAME:** llamada directa dentro del demux | **DIFFERENT:** `m_pendingInvocations` diferida | **DIFFERENT:** callback de demanda síncrono desde decoder | **NOT APPLICABLE** | **DIFFERENT:** HLE diferido; #178 omite callbacks host-feed |
| producer backpressure | **SAME:** retorno de callback antes de avanzar | **DIFFERENT:** decoded-picture cap entre llamadas; sin reserva viBuf in-flight | **DIFFERENT:** demanda IPU + DMA4 idle | **NOT APPLICABLE:** capacidad downstream VLC | **DIFFERENT:** #178 limita staging, no viBuf guest |
| consumer pacing | **SAME:** IPU/DMAC, thread y vblank PS2 | **DIFFERENT:** FFmpeg host + espejo P3.14.2 | **SAME en propiedad:** IPU solicita bits y DMA progresa | **NOT APPLICABLE:** pipeline VLC | **DIFFERENT:** host; #175 propone pacing en `GetPicture` |
| decode owner | **SAME:** sceMpeg/IPU bajo control guest | **DIFFERENT:** FFmpeg host | **SAME en hardware:** EE/IPU/DMAC | **DIFFERENT:** decoder VLC posterior | **DIFFERENT:** FFmpeg host |
| output frame format | **SAME:** buffer guest que DMC trata como 512×448×4 | **SAME observable:** RGBA guest; internamente `AV_PIX_FMT_RGBA` | **DIFFERENT:** RGBA32 en macroblocks 16×16 | **NOT APPLICABLE:** ES comprimido | **DIFFERENT/SAME:** formato HLE configurado por API |
| GS upload | **SAME:** 32 REF IMAGE bands + transferencia local | **SAME:** GIF DMA guest interpretado por GS host | **DIFFERENT:** upload como textura | **NOT APPLICABLE** | **SAME/DIFFERENT:** GS HLE común, dependiente del título |
| presentation timing | **SAME:** guest + `sceGsSyncV` + FIELD | **DIFFERENT:** ticks host/vsync y bob aproximado | **SAME en hardware:** espera dos vblank en muestra | **NOT APPLICABLE** | **DIFFERENT:** #175 añade pacing host por framerate |

**HECHO:** PCSX2 no encaja como otra columna HLE: ejecuta el SDK guest contra
IPU, DMAC y GS emulados. En `IPUdma.cpp`, DMA4 avanza `MADR/QWC` solo por los
QW aceptados por el FIFO y se detiene si el IPU no solicita datos. Su lección
es la conservación de ownership mediante progreso parcial real, no una API HLE
concreta.

## 2. Frontera natural de sincronización

| Rango | Frontera | Corrección/fidelidad | Frescura guest | Deadlock | Coste e invasividad | Compatibilidad |
|---:|---|---|---|---|---|---|
| 1 | **D + A:** después del retorno y antes de avanzar el PES | **HECHO:** coincide con wrapper original | Máxima | Bajo si se reanuda; alto si se bloquea el executor | Media | Alta, reproduce contrato SDK |
| 2 | **I:** transacción de demux suspendida, ACK `onComplete`, commit y continuación | **INFERENCIA:** equivalente observable a D+A | Máxima | Bajo con generación/handle y sin locks | Media | Alta con runtime asíncrono |
| 3 | **E:** después de `viBufEndPut` | **HECHO:** estado ya está mutado; solo es observable al volver | Máxima | Igual a D si se captura completion | Media | Alta |
| 4 | **C:** drain de `queueInvocation` | **INFERENCIA:** correcto solo si el cursor espera cada resultado | Alta | Medio por reentrancia/batching | Media | Media |
| 5 | **H:** cuando IPU/DMAC pide más | **HECHO:** es el modelo nativo de ps2sdk | Máxima | Bajo en hardware; medio en HLE incompleto | Alta | Alta conceptualmente |
| 6 | **B:** antes de aceptar en la cola | **INFERENCIA:** requiere reserva exacta; no reproduce retorno | Estado aún viejo | Bajo | Media/alta | Media |
| 7 | **F:** siguiente syscall MPEG guest | **INFERENCIA:** aporta contexto guest, pero llega tarde si ya se consumió el PES | Alta al drenar | Medio | Media | Media |
| 8 | **G:** consumo en `sceMpegGetPicture` | **INFERENCIA:** demasiado tarde y acopla demux con entrega de frame | Variable | Alto si no llega picture | Baja aparente | Baja |

**RETRACTADO:** un límite `outstandingCallbacks <= 13`. No se deriva de una
interfaz ni de capacidad. Si el contrato síncrono da una sola unidad no
confirmada, profundidad 1 es una consecuencia, no un número calibrado.

## 3. Contrato de `StrM2vCallBack`

Todas las direcciones de esta sección están ligadas al ELF identificado arriba.

**HECHO:** `StrM2vCallBack@0x0047AFE0–0x0047B0C4` llama a
`viBufBeginPut@0x0047AE90–0x0047AF80`, copia mediante `Copy2area`, llama a
`viBufEndPut@0x0047AF80–0x0047AFD4` y devuelve 1 si comprometió una cantidad
mayor que cero; devuelve 0 si no pudo comprometer nada.

**HECHO:** `viBufBeginPut` calcula las regiones escribibles usando base,
capacidad, cursor de bloques, bloques en cola y bytes pendientes. La capacidad
física es 256 × 2.048; la fórmula guest deja 254 bloques utilizables antes de
restar pendientes. `viBufEndPut` actualiza de inmediato el estado productor,
incluido el total en `+0x28`.

**HECHO:** el wrapper original de demux en `0x00109180–0x00109470` invoca los
callbacks en `0x001092CC`/`0x00109394`, conserva el retorno, y en
`0x001093A0–0x001093B4` condiciona con él el cursor confirmado; devuelve ese
cursor desde `sp+0xAC` en `0x0010943C`.

**HECHO:** `MpegNodataCallBack@0x0047B300` recibe el valor devuelto por
`sceMpegDemuxPssRing`, lo pasa a `ReadDataSetAddr` y resta exactamente esa
cantidad del contador disponible. Por tanto, el caller espera que el valor
represente ownership ya transferido, no trabajo meramente encolado.

**INFERENCIA:** el callback es lógicamente síncrono. Puede ejecutarse en un EE
que a su vez es planificado, pero no puede solaparse con otras invocaciones del
mismo demux antes de que el wrapper conozca el resultado. La serialización
original proviene del call/return normal y de la capacidad real viBuf/IPU, no
de un límite explícito de 14.

**UNKNOWN:** el significado exacto de una aceptación parcial para todos los
casos de `Copy2area`. El retorno es booleano, no el número de bytes. P4.2.3 debe
reproducir la regla de cursor del wrapper original, incluida cualquier forma en
que el wrapper conserva el resto, y no inventar que `v0=1` significa siempre
payload completo.

**HECHO:** el runtime actual preserva orden FIFO observable mediante la cola
global y las pilas de invocación por thread, pero `onComplete` solo libera los
datos del callback. No devuelve `v0` a `processPssBuffer`, que ya borró el
prefijo.

**Conclusión — INFERENCIA:** preserva **ordering**, no **ordering + completion**.

## 4. Cola y scheduler: explicación estructural de 14

Flujo actual:

```text
sceMpegDemuxPssRing
  -> processPssBuffer (recorre el bloque disponible completo)
       -> queueStreamCallbackEvent × N
       -> erasePssPrefix × N
  -> dispatchStreamCallbacksUnlocked × N
       -> dispatchGuestStreamCallback
            -> queueInvocation (push_back)
  -> retorna bytes consumidos

scheduler, después
  -> drena pending invocations
  -> crea/activa contextos guest
  -> ejecuta StrM2vCallBack en orden
  -> onComplete libera cbData
```

**HECHO:** `queueInvocation` no ejecuta ni espera. Añade una secuencia y hace
`push_back` en `m_pendingInvocations`. El loop del scheduler las mueve a pilas
de thread con la compensación LIFO documentada en P3.12, de forma que la
ejecución final respeta enqueue FIFO.

**HECHO:** DMC pide bloques CD de `0x10000` bytes para la película. En una sola
entrada HLE, el parser puede encontrar 14 PES de vídeo/audio completos. No hay
un yield guest dentro de ese parse.

**INFERENCIA:** la profundidad es el número de eventos hallados entre dos
fronteras HLE/guest, determinado por contenido PSS, tamaño del bloque y punto de
entrada. El promedio observado de ~13,8 entre caídas abruptas y el máximo 16
son coherentes con esta estructura. No se identificó un loop cap 14, un quantum
14 ni un campo de protocolo 14.

**HECHO:** cuando el callback histórico número 14 alcanzó `viBufBeginPut`, los
13 anteriores ya habían comprometido 52.973 bytes. El guestFree visto por el
HLE al enqueue era obsoleto respecto al guestFree de ejecución.

## 5. Ownership del demux y modelos posibles

### Modelo A — synchronous callback completion

**INFERENCIA:** fidelidad alta, corrección alta, complejidad media, deadlock
bajo si el callback se ejecuta como invocación guest anidada/reanudable. Es la
semántica original más directa. Bloquear el hilo C++ mientras ese mismo hilo
debe ejecutar guest sería incorrecto; “synchronous” describe el contrato, no
una espera de mutex/condvar.

### Modelo B — async packet ownership

**INFERENCIA:** mejor ajuste al scheduler existente. El PES queda `IN_FLIGHT`,
el syscall/demux queda suspendido, `onComplete` captura `v0`, confirma o retiene
el cursor y reanuda. Tiene la misma semántica visible que A y evita reentrancia
profunda. Requiere estado por handle/generación y vida útil explícita.

### Modelo C — bounded event queue con reserva

**INFERENCIA:** puede permitir paralelismo, pero tiene fidelidad menor. El
tamaño del payload se conoce al parsear, aunque la capacidad efectiva depende
de bloques de 2.048, dos regiones de wrap, cursores, pending y posible actividad
del consumidor. Duplicaría `viBufBeginPut` en host y podría desincronizarse.

### Modelo D — consumer-pull

**HECHO:** representa la propiedad de ps2sdk/libmpeg: datos suministrados cuando
el IPU los necesita y no hay DMA4 activa. **INFERENCIA:** es sólido y reusable,
pero requiere una interfaz de demanda que el runtime actual no tiene completa.

### Modelo E — host-fed decoder

**HECHO:** el ES canónico host decodifica limpio. **INFERENCIA:** elimina esta
clase de omisión del decoder host, pero elude el callback y no explica los
efectos guest de viBuf. No soluciona el fallo exacto; lo rodea.

### Modelo F — híbrido

**INFERENCIA:** puede ser válido en una static recomp si existe un solo ledger
de bytes y un commit atómico que impulsa decoder y espejo guest. Si mantiene
dos pipelines independientes, añade drift, doble buffering y dos nociones de
consumo. Es demasiado amplio para P4.2.3.

**Conclusión — INFERENCIA:** el packet/prefijo debe seguir perteneciendo al
demux hasta completion. Puede conservarse físicamente en `pssBuffer` o mediante
un objeto in-flight inmutable con rango y generación. La equivalencia de
ownership es la condición; borrar y confiar en una copia efímera no lo es.

## 6. Reserva frente a completion

| Pregunta | Resultado |
|---|---|
| ¿Se conoce el payload solicitado? | **HECHO:** sí, al terminar de parsear el PES. |
| ¿Bastan bytes libres? | **HECHO:** no describen toda la lógica guest; `viBufBeginPut` usa bloques, pending, cursores y hasta dos áreas. |
| ¿Puede fallar por algo más? | **UNKNOWN:** no se ha demostrado el conjunto completo de fallos en todas las rutas; callback/handle/registro y vida útil también importan. |
| ¿Duplicaría lógica guest? | **INFERENCIA:** sí, salvo que la reserva sea una primitiva del propio modelo viBuf compartido. |
| ¿Puede derivar? | **INFERENCIA:** sí, si el consumidor modifica viBuf entre reserva y ejecución o si el espejo P3.14.2 cambia la contabilidad. |
| ¿Es más segura? | **INFERENCIA:** no como sustituto de completion; puede ser una optimización posterior con invariantes verificables. |

**INFERENCIA:** con la frontera original solo hay una escritura de vídeo no
confirmada dentro de ese call/return. No hace falta reserva separada. Si se
decide mantener varios callbacks en vuelo, entonces debe reservarse la
capacidad futura real; esa arquitectura deja de ser la reproducción mínima del
contrato original.

## 7. Ruta ES canónica host

**HECHO:** transport correctness y decode correctness ya pueden medirse por
separado: el PSS offline produce el ES canónico y FFmpeg lo decodifica limpio;
el ES construido desde viBuf runtime pierde un payload.

**INFERENCIA:** una arquitectura futura puede tener:

```text
PSS canónico -> ledger de payloads/PTS -> decoder host
                         |
                         +-> commit de efectos guest viBuf/callback
```

El ledger debe identificar cada payload una vez, mantener offsets canónicos y
no permitir que decoder host y estado guest confirmen unidades diferentes.

**HECHO:** DMC observa viBuf directamente a través de `viBufBeginPut`,
`viBufEndPut`, `viBufAddDMA`, cursores y callback returns. P3.14.2 además refleja
consumo host en esos campos.

**INFERENCIA:** la separación es técnicamente viable, pero no se ha demostrado
que DMC u otros juegos ignoren el contenido y temporización de esos estados
fuera de la reproducción. Por ello es una opción de arquitectura general,
no el P4.2.3 mínimo.

## 8. Lecciones nativas: ps2sdk y PCSX2

Fuentes públicas inspeccionadas: [ps2sdk `libmpeg_core.c`](https://github.com/ps2dev/ps2sdk/blob/master/ee/mpeg/src/libmpeg_core.c),
[cabecera `libmpeg.h`](https://github.com/ps2dev/ps2sdk/blob/master/ee/mpeg/include/libmpeg.h)
y [muestra `mpeg.c`](https://github.com/ps2dev/ps2sdk/blob/master/ee/mpeg/samples/mpeg.c).

**HECHO:** `MPEG_Initialize` recibe un callback proveedor. `_ipu_needs_bits`
consulta el estado de bits del IPU; `_ipu_sync` llama a `_req_data` cuando el
FIFO baja del umbral. `_req_data` solo pide más si `D4_QWC == 0`. La muestra
espera el canal, inicia DMA de un bloque de 2.048 bytes y avanza la fuente solo
al programarlo; cero indica fin.

**HECHO:** el máximo trabajo de entrada simultáneo demostrable en esa interfaz
es una transferencia DMA4 activa, más los bits ya aceptados por FIFO/IPU. No es
una cola arbitraria de callbacks de aplicación.

**HECHO:** [PCSX2 `IPUdma.cpp`](https://github.com/PCSX2/pcsx2/blob/master/pcsx2/IPU/IPUdma.cpp)
reduce `QWC` y avanza `MADR` por la cantidad realmente aceptada, y para si el
IPU no solicita datos. Ejecutar el SDK original contra esta máquina emulada
conserva la relación productor/consumidor naturalmente.

**INFERENCIA:** ps2sdk es **CONSUMER-PULL**, aunque el proveedor programe un DMA
que luego progresa asíncronamente. El mínimo semántico que debe conservar el HLE
es: **la fuente no confirma ownership de datos que el consumidor/viBuf aún no
ha aceptado**. No es necesario trasplantar IPU/MMI para cumplir esa propiedad.

## 9. Calidad del demux PSS frente a VLC

Fuente inspeccionada: [`pss.c` en commit `60126df`](https://github.com/dywbe/vlc-pss-demux-plugin/blob/60126dfc2494f8703eba2f40a17af295a5317e2d/pss.c).

| Aspecto | Comparación | Clasificación |
|---|---|---|
| Pack headers | Ambos reconocen MPEG-2; el runtime también cubre variante MPEG-1 | **HECHO — OUR SIMPLIFICATION BUT SAFE FOR DMC:** ninguna carencia observada |
| System headers | Ambos saltan por longitud | **HECHO — IRRELEVANT:** equivalentes para DMC |
| PES length | Ambos usan longitud de 16 bits; runtime además busca siguiente start code si cero | **HECHO — OUR SIMPLIFICATION BUT SAFE FOR DMC:** runtime es más tolerante |
| Fragmentos | Runtime retiene un PES incompleto entre chunks; VLC hace `read_exact` y depende del stream | **HECHO — VLC-SPECIFIC:** no revela bug local |
| Video IDs | `0xE0–0xEF` | **HECHO — IRRELEVANT:** equivalentes |
| Padding | Ambos lo saltan | **HECHO — IRRELEVANT** |
| Private streams | VLC valida `0xBD`, substreams Sony `A0/A1`, `SShd/SSbd` y deinterleave PCM; runtime enruta categorías amplias | **HECHO — OUR SIMPLIFICATION BUT SAFE FOR DMC:** segura para el vídeo actual; robustez multi-juego limitada |
| PTS/DTS | Ambos extraen timestamps MPEG-2; runtime acepta más formas | **HECHO — IRRELEVANT:** no causa la omisión |
| Resync | Ambos buscan prefijo `00 00 01` | **HECHO — IRRELEVANT** |
| End of stream | Ambos reconocen program end; runtime decide además qué hacer con final truncado | **HECHO — UNKNOWN:** política de truncado no validada contra más juegos |
| Transferencia de ownership | VLC entrega un `block_t` completo downstream; no tiene callback viBuf | **HECHO — VLC-SPECIFIC:** útil como disciplina de unidad, no como semántica PS2 |

**HECHO:** el parser local puede producir el ES canónico completo y exacto. La
omisión aparece cuando el payload ya fue identificado y encolado. Por tanto,
`CURRENT_DEMUX_QUALITY` es `GOOD` para DMC y su defecto actual está en el commit
de ownership, no en framing.

## 10. Decode y cuatro dimensiones temporales

| Dimensión | Respuesta | Evidencia |
|---|---|---|
| Decoder read-ahead | **YES, acotado de forma host y no por viBuf real** | `kMaxDecodedPicturesAhead=8` se comprueba entre entradas; FFmpeg puede consumir chunks y producir más de una unidad antes del siguiente límite. |
| Frame delivery ahead of guest time | **NO material demostrado; tiene pacing propio** | `sceMpegGetPicture` usa PTS/ticks/vsync para decidir entrega en la ruta actual. El detalle aún no equivale necesariamente al hardware. |
| Demux production ahead of guest time | **YES** | Un bloque de 64 KiB se parsea en ráfaga dentro de una llamada HLE. |
| Callback accumulation ahead of guest time | **YES** | Profundidad observada 1–16 y fallo histórico a 14. |

Fuente externa: [PS2Recomp PR #175](https://github.com/ran-j/PS2Recomp/pull/175).

**HECHO:** #175 propone limitar cuándo `sceMpegGetPicture` presenta según
framerate/ticks; no hace lento el decoder ni sincroniza callback y demux.

**INFERENCIA:** usar pacing de presentación para tapar la omisión mezclaría dos
variables causales. El fix futuro debe cerrar primero el ownership. Read-ahead,
audio y presentación pueden auditarse después con métricas independientes.

## 11. Frame output y GS

Pipeline DMC reconstruido:

```text
FFmpeg/sceMpegGetPicture
  -> buffer guest RGBA de película
  -> cadena GIF con 32 REF
       cada REF: 16 × 448 × 4 = 28.672 bytes
       total: 512 × 448 × 4 = 917.504 bytes
  -> Movie_loadimage inicia GIF DMA
  -> GS IMAGE upload por bandas
  -> transferencia local MoveImage2 a superficie movie/doble buffer
  -> DISPFB
  -> presentación host
```

**HECHO:** los dumps verificaron 66 tags por kick: 33 CNT + 32 REF + terminación,
y los 32 REF suman exactamente 917.504 bytes.

**HECHO:** DMC no presenta los frames de película como un quad texturizado. La
ruta carga IMAGE directamente en una superficie GS y realiza transferencia
local a la superficie framebuffer de película. El fade posterior usa primitivas
no texturizadas.

**RETRACTADO:** descripciones históricas que llamaban “textura de película” a
este buffer como si DMC siguiera el sample ps2sdk.

**HECHO:** la muestra ps2sdk sí produce RGBA32 en disposición de macroblocks,
lo sube como textura y dibuja un sprite texturizado después de sincronizar
vblank. Es un precedente de decoder/IPU, no del upload específico de DMC.

**HECHO:** el contrato GS actual conocido-good permanece: UI DISPLAY
`{0x00,0x38}`, DRAW `{0x38,0x00}`, Z `0x70`; movie DISPLAY `{0x00,0xA0}`,
DRAW `{0xA0,0x00}`, Z `0xE0`. No hay evidencia para reabrir esas direcciones.

**INFERENCIA:** la primera corrupción actual es de decode a causa del ES
incompleto, anterior a las 32 bandas. Un hash correcto en el buffer guest y un
hash incorrecto tras IMAGE upload señalarían otra divergencia; todavía no se ha
observado ese orden para la corrupción PSS actual.

## 12. Vibración vertical del warning

**HECHO:** movie/warning usa `SMODE2=1`: interlace activo y `FFMD=0` (FIELD).
La UI estable usa `SMODE2=3`: interlace activo y `FFMD=1` (FRAME).

**HECHO:** el backend actual selecciona paridad a partir del tick de vsync y en
FIELD aplica una presentación tipo bob/duplicación de líneas. Alternar la línea
fuente par/impar puede percibirse como aproximadamente dos píxeles host.

**HECHO:** [upstream PR #182](https://github.com/ran-j/PS2Recomp/pull/182)
trata la determinación de paridad en pruebas a partir de ticks consumidos; no
aporta un fix de comportamiento DMC.

**INFERENCIA:** la capa más probable es `GS field-selection / host bob`, con
posible contribución de fase `sceGsSyncV`. MPEG output layout y pacing son menos
probables porque la vibración está ligada al modo FIELD y existe antes de usar
un frame de vídeo sano.

**UNKNOWN:** si la paridad inicial, el signo del desplazamiento de source o la
política bob exacta es la primera diferencia. Requiere comparación por campo
PCSX2/recomp, fuera de P4.2.3.

## 13. Intervalo negro CAPCOM

**HECHO:** los traces anteriores sitúan el intervalo antes del primer
`[CDMODULE/MOVIE:start]`; en ese tramo no se observa el fno de transporte PSS.
La tabla `/DATA/MOVIE` contiene 18 PSS y ninguno identifica un logo CAPCOM.

**INFERENCIA:** clasificación más probable: `GAME-DRAWN_CONTENT_MISSING`. El
logo se cargaría como recurso normal y atravesaría la ruta de draw/GIF/GS, no el
demux MPEG.

**UNKNOWN:** el asset exacto, la llamada de draw exacta y la primera divergencia
del intervalo negro no están establecidos. No puede elevarse a hecho que sea un
fallo GS concreto ni que toda la duración debiera mostrar el logo.

**RETRACTADO:** tratar el CAPCOM negro como prueba de que falta un PSS o de que
el decoder MPEG es su causa.

## 14. Auditoría dirigida de paridad SDK/HLE

| Familia | Hallazgo | Estado |
|---|---|---|
| GS init | `sceGsSetDefDBuffDc` omitía el tail condicional del wrapper; ya corregido en el baseline visual | **HECHO:** precedente cerrado |
| GS init | PMODE/AMOD difiere de algunos defaults/valores del wrapper original | **HECHO:** diferencia documentada; efecto activo `UNKNOWN` |
| GS sync/vblank | FIELD/bob y fase de `sceGsSyncV` son aproximaciones host | **HECHO:** diferencia estructural; causa exacta de vibración `UNKNOWN` |
| GIF/DMA | La cadena movie de 32 REF y contratos de framebuffer se reproducen en la base actual | **HECHO:** no se halló otro tail omitido concreto |
| MPEG demux | El retorno del callback original gobierna cursor; HLE lo ignora al confirmar | **HECHO:** blocker P4.2.3 |
| MPEG init | Existe desacuerdo documentado de ownership/reset y una compensación P3.14 | **HECHO:** paridad incompleta; alcance multi-juego `UNKNOWN` |
| viBuf/IPU | Callback no-stream/retirement original no se despacha; P3.14.2 espeja consumo host | **HECHO:** sustitución semántica activa |
| Scheduler | Cola conserva orden, no completion | **HECHO:** diferencia causal P4.2.2 |

**INFERENCIA:** el patrón relevante no es “faltan muchos SDKs”, sino tres
lugares activos con postcondición guest omitida o sustituida: retorno del
callback de demux, efectos de callback no-stream/IPU y ownership/reset MPEG.

## 15. Upstream PS2Recomp

### PR #178

Fuente: [feat(mpeg): host-fed CD-stream ES injection for the guest-driven decoder](https://github.com/ran-j/PS2Recomp/pull/178).

**HECHO:** #178 permite que un productor host alimente PSS cuando no existe
contexto de thread guest; reutiliza parser/estado MPEG y no despacha callbacks
guest en esa ruta. El texto plantea una cola por playback drenada en un syscall
MPEG guest posterior.

**INFERENCIA:** el reconocimiento del problema de contexto es directamente
aplicable. Una cola por handle también evita una cola global ambigua. No
obstante, “drain on next syscall” solo es correcto para DMC si ese syscall no
retorna consumo hasta completar cada callback y aplicar su `v0`. Si los bytes
ya se dieron por consumidos, reproduce P4.2.2 con otra cadencia.

### PR #175

Fuente: [fix(mpeg): pace sceMpegGetPicture to the stream's frame rate](https://github.com/ran-j/PS2Recomp/pull/175).

**HECHO:** #175 ataca frames entregados demasiado pronto cuando el host puede
demux/decode de inmediato. No limita toda la lectura adelantada, no sincroniza
audio y no relaciona callback completion con ownership PSS.

**INFERENCIA:** es evidencia de un problema general de reloj en HLE, pero no es
precedente causal de la omisión. Debe evaluarse después de obtener un ES exacto.

**HECHO:** al auditar, `main` upstream continúa en el baseline
`14b1e5cb39b4af7e6fc12f9a29fdc751efde49d7`; #175 y #178 no forman parte de
ese commit. No se halló un PR/commit MPEG posterior que cierre el contrato
callback/cursor de DMC.

## 16. Ranking de diseños P4.2.3

Escala: fidelidad/corrección/compatibilidad `ALTA|MEDIA|BAJA`; complejidad y
deadlock `BAJA|MEDIA|ALTA`.

| Diseño | Fidelidad | Corrección exacta P4.2.2 | Complejidad | Deadlock | Sensibilidad temporal | DMC / otros juegos | Nuevas diferencias | Decisión |
|---|---|---|---|---|---|---|---|---|
| 1. Synchronous video callback | ALTA | SÍ | MEDIA | MEDIA si reentrante; BAJA si reanudable | BAJA | ALTA / ALTA para mismo contrato | Reentrancia si se fuerza inline | **SECOND BEST** |
| 2. Completion ACK + in-flight ownership | ALTA | SÍ | MEDIA | BAJA con continuación y generation guard | BAJA | ALTA / ALTA | Estado suspendido nuevo, semántica visible igual | **BEST** |
| 3. Reserved bytes | MEDIA | Probablemente, pero no demuestra callback acceptance | MEDIA/ALTA | BAJA | MEDIA | MEDIA / BAJA | Duplica viBuf y permite pipeline inexistente | **REJECT** como solución sola |
| 4. Event queue drained at guest MPEG boundary | MEDIA/BAJA | NO sin ACK/ownership | MEDIA | MEDIA | ALTA | MEDIA / MEDIA | Resultado depende del siguiente syscall | **REJECT** sola |
| 5. Consumer-pull | ALTA | SÍ | ALTA | MEDIA | BAJA | ALTA / ALTA | Cambia ampliamente el HLE actual | **REJECT para P4.2.3**, candidato futuro |
| 6. Host-fed canonical PSS/ES | BAJA respecto al guest | Rodea, no resuelve | MEDIA | BAJA | MEDIA | MEDIA / MEDIA | Omite callbacks/viBuf observables | **REJECT** |
| 7. Hybrid host-decode + guest mirror | MEDIA | Puede rodear si hay ledger único | ALTA | MEDIA | MEDIA | MEDIA / MEDIA | Riesgo de dos verdades y drift | **REJECT** |
| 8. Simple callback limit | BAJA | Solo accidentalmente | BAJA | BAJA | ALTA | BAJA / BAJA | Magic depth y fallo dependiente del stream | **REJECT** |

**INFERENCIA:** 2 es mejor que 1 porque permite que el mecanismo del scheduler
siga siendo asíncrono sin perder la postcondición síncrona. Ambos comparten la
misma regla original; la diferencia es de integración.

## 17. Diseño exacto recomendado

### Estado y transición

```text
READY
  -> parsea hasta video PES completo
  -> crea IN_FLIGHT {handleGeneration, packetStart, packetEnd,
                     payloadStart, payloadSize, priorCommittedCursor}
  -> invoca StrM2vCallBack
  -> SUSPENDED_WAITING_CALLBACK

callback completion
  -> captura guest v0
  -> aplica exactamente la regla original de aceptación/cursor
  -> libera cbData
  -> si aceptado: COMMIT cursor/prefix; reanuda parse
  -> si no aceptado: conserva packet/cursor; retorna consumo confirmado
```

### Invariantes

1. **INFERENCIA:** como máximo un PES de vídeo no confirmado por handle mientras
   se reproduce el contrato síncrono. Este 1 está derivado de call/return.
2. **INFERENCIA:** ningún byte del prefijo hasta `packetEnd` se declara consumido
   antes del callback result correspondiente.
3. **INFERENCIA:** el valor retornado por `sceMpegDemuxPssRing` termina en el
   último cursor confirmado y nunca incluye un packet in-flight.
4. **INFERENCIA:** reintento/reanudación no duplica payload ni callback aceptado.
5. **INFERENCIA:** no se mantiene el mutex de playback durante ejecución guest.
6. **INFERENCIA:** completion valida handle y generación antes de tocar estado;
   destroy/recreate invalida la transacción de forma definida.
7. **HECHO requerido por el original:** `v0` del callback, no solo su retorno al
   scheduler, gobierna aceptación.
8. **INFERENCIA:** audio conserva orden relativo; antes de generalizar el ACK a
   audio debe verificarse su retorno original, pero no puede adelantar el cursor
   más allá de un vídeo pendiente.
9. **INFERENCIA:** decoder host recibe una unidad una sola vez, en el mismo punto
   de commit elegido para evitar decode-before-accept y duplicación.

### Funciones que tocaría una fase futura

- `processPssBuffer`: devolver estado `complete / needs-callback / blocked` y no
  borrar el PES de vídeo antes de ACK.
- `sceMpegDemuxPss` y `sceMpegDemuxPssRing`: soportar terminación/reanudación del
  syscall y calcular solo consumo confirmado.
- `dispatchGuestStreamCallback`: propagar completion y `v0`; mantener vida útil
  de los argumentos guest.
- `dispatchStreamCallbacksUnlocked`: dejar de disparar una ráfaga sin relación
  con el cursor; convertir cada evento de vídeo en transición.
- `MpegPlaybackState`: añadir transacción in-flight, generación y cursor
  confirmado.
- `EeScheduler`: solo si la API actual de continuación no puede reanudar el
  syscall; no se justifica cambiar el orden global.

**UNKNOWN:** si el API actual `invokeCurrent` basta sin extensión. Esto es un
detalle de implementación, no una duda sobre la frontera semántica.

## 18. Backlog de mejoras reales

| # | NAME | AREA | CURRENT PROBLEM | EVIDENCE / EXTERNAL REFERENCE | BENEFIT | RISK | GENERALITY | COST | CONFIDENCE | BLOCKER |
|---:|---|---|---|---|---|---|---|---|---|---|
| 1 | Completion-synchronized PES ownership | MPEG/scheduler/PSS | Cursor avanza antes del callback | ELF wrapper + P4.2.2; ps2sdk consumer-pull | Elimina omisión causal y restaura contrato | Reanudación/reentrancia | likely reusable | MEDIUM | HIGH | YES |
| 2 | Formal non-stream/IPU retirement contract | MPEG/viBuf | P3.14.2 espeja consumo; callback original no se despacha | `MpegNodataCallBack`/`viBufAddDMA`; ps2sdk/PCSX2 | Reduce drift guest/host | Alcance y hardware incompleto | runtime-wide | HIGH | HIGH sobre diferencia, MEDIUM sobre diseño | NO |
| 3 | Canonical byte ledger | architecture/MPEG | Host decode y viBuf pueden tener dos cursores | ES canónico y #178 | Trazabilidad, no duplicate/omit | Complejidad de migración | runtime-wide | HIGH | MEDIUM | NO |
| 4 | `sceMpegInit` ownership/reset parity | MPEG | Compensación P3.14 para estado que HLE altera | notas P3.14 | Menos title override | Regresión multi-juego | likely reusable | MEDIUM | MEDIUM | NO |
| 5 | Sony private-audio validation/routing | PSS | `0xBD` se clasifica de forma amplia | VLC `pss.c` | Robustez y callback correcto | DMC puede usar convención distinta | likely reusable | LOW/MEDIUM | MEDIUM | NO |
| 6 | Independent decoder read-ahead bound | MPEG/pacing | Cap de pictures no representa demanda guest | código actual; #175 | Memoria/timing predecible | Starvation si se acopla mal | runtime-wide | MEDIUM | MEDIUM | NO |
| 7 | FIELD parity and bob oracle | GS/sync/render | Warning vibra ~2 px | SMODE2 1 vs 3; PCSX2/PR #182 | Estabilidad interlaced | Puede afectar otros títulos | runtime-wide | MEDIUM | MEDIUM | NO |
| 8 | CAPCOM resource/render trace | render/GS | Intervalo pre-PSS negro | ausencia de movie start/PSS CAPCOM | Recuperar contenido de boot | Asset/call path aún unknown | DMC-only | MEDIUM | MEDIUM | NO |
| 9 | PMODE/AMOD wrapper parity | GS | Defaults HLE no coinciden plenamente | P4.1.x audit | Blend/presentation correcta | Efecto activo incierto | likely reusable | LOW/MEDIUM | LOW/MEDIUM | NO |
| 10 | Final-chunk and private-stream parser hardening | PSS | Políticas amplias/truncado | comparación VLC/spec behavior | Robustez otros PSS | Poco beneficio DMC inmediato | likely reusable | LOW | MEDIUM | NO |

## 19. Plan de validación futuro P4.2.3

| Dimensión | Criterio obligatorio | Método |
|---|---|---|
| Viejo primer bad offset | `0x1FC301` ya no diverge | Comparación binaria incremental canonical/runtime |
| Viejo payload | Los 4.063 bytes aparecen exactamente una vez | Hash/rango alrededor de offset y contador por packet |
| ES completo | 23.654.877 bytes, 5.808 payloads, SHA-256 canónico | Dump runtime y SHA-256/comparación byte a byte |
| Duplicación | Cero bytes/payloads duplicados | Alignment/diff con cursor monotónico y packet IDs |
| Omisión | Cero bytes/payloads omitidos | Primera divergencia inexistente hasta EOF |
| FFmpeg warnings | Registrar total y texto; objetivo cero para ES canónico | stderr estructurado por corrida |
| `ac-tex damaged` | 0 | Conteo exacto |
| `MVs` warnings | 0 | Conteo exacto |
| Outstanding depth | Vídeo no confirmado por handle ≤1 por contrato | Telemetría queue/start/complete/commit; sin magic 13 |
| viBuf consistency | Productor, consumidor, pending, bloques y `+0x28` consistentes en cada commit | Snapshot antes/después del callback y del mirror |
| Callback result | `v0`, bytes/cursor antes-después y decisión de commit correlacionados | Trace acotado por packet ID |
| P3.14.2 wrap | Dos regiones y cruce de ring sin duplicate/omit | Caso alrededor de wrap con dumps exactos |
| Movie visual | Sin corrupción tardía MPEG | Captura/hashes por frame y observación controlada |
| Memory Card | Estable, clara, sin flicker | Misma secuencia de boot de baseline |
| Language Select | Estable, clara, sin flicker | Misma secuencia de boot de baseline |
| GS framebuffer | UI `{00,38}/{38,00}/70`; movie `{00,A0}/{A0,00}/E0` | Trace de env/DISPFB/DRAW/ZBUF |
| Deadlock | Llega a película/EOF y puede salir; watchdog sin stalls | Run prolongado y eventos scheduler |
| Segundo movie | Si es práctico, mismo ES exacto y visual sin corrupción | Repetir matriz mínima con otra entrada PSS |
| Timing separado | Producción, decode, delivery y present tienen contadores separados | No aceptar que pacing oculte un mismatch de bytes |

**INFERENCIA:** la prueba decisiva no es “la película parece mejor”, sino ES
runtime idéntico al canónico completo con callback/viBuf consistente y sin
regresión GS. Solo después el warning FIELD y pacing merecen una fase causal.

## 20. Decisión de readiness

**INFERENCIA:** P4.2.3 está listo para implementar. Existe evidencia primaria
del ELF para el contrato, una divergencia binaria exacta, una cadena causal
runtime y precedentes externos coherentes. El diseño recomendado no depende de
adivinar profundidad ni capacidad.

**INFERENCIA:** la implementación debe comenzar por una transacción reanudable
de un solo PES de vídeo y completion ACK. No debe empezar por pacing,
reservation, host-feed, cambios GS ni una reconstrucción completa de IPU.

Resultado ARCH_AUDIT_002 —
SOL_CROSS_IMPLEMENTATION_MPEG_RENDER_ARCHITECTURE_AUDIT

PRIMARY_CLASSIFICATION:
ARCH_AUDIT_P423_COMPLETION_SYNC_RECOMMENDED

ROOT_PROBLEM:
El demux HLE confirma y borra PES antes de que el callback guest actualice viBuf y devuelva aceptación; preserva orden pero pierde completion/ownership.

ORIGINAL_SYNC_BOUNDARY:
Tras el retorno de StrM2vCallBack y antes de confirmar el avance del cursor PSS en sceMpegDemuxPssRing.

WHY_14_CALLBACKS_ACCUMULATE:
Una llamada HLE parsea un bloque CD de 0x10000 completo y encola todos sus PES antes de devolver control al scheduler; 14 es composición/alineamiento del bloque, no una constante.

PS2SDK_LIBMPEG_LESSON:
El IPU pide bits y solo se programa otro DMA4 cuando el anterior terminó; FIFO y DMA proporcionan consumer-pull y backpressure natural.

VLC_PSS_LESSON:
Confirma framing, timestamps, stream IDs y audio privado Sony, además de la entrega por unidad PES completa; no modela viBuf.

UPSTREAM_PS2RECOMP_178_LESSON:
Una cola por handle resuelve la falta de contexto guest, pero solo resuelve P4.2.2 si retiene ownership y espera el resultado antes de confirmar consumo.

UPSTREAM_PS2RECOMP_175_LESSON:
El host instantáneo crea desacoples temporales; pacing de GetPicture controla presentación, no ownership ni backpressure del demux.

CURRENT_DEMUX_QUALITY:
GOOD

CURRENT_DECODER_QUALITY:
GOOD

CURRENT_SCHEDULER_MODEL:
SEMANTICALLY_INCOMPLETE

BEST_P423_DESIGN:
CALLBACK COMPLETION ACK + IN-FLIGHT PACKET OWNERSHIP con semántica síncrona observable

SECOND_BEST_P423_DESIGN:
SYNCHRONOUS VIDEO CALLBACK inmediato/anidado

REJECTED_DESIGNS:
RESERVATION sola; GUEST-BOUNDARY DRAIN sin ACK; HOST-FED directo; HYBRID con estados independientes; SIMPLE CALLBACK LIMIT; GetPicture pacing como backpressure

VIDEO_CALLBACKS_SHOULD_REMAIN_ASYNC:
CONDITIONAL

PSS_PACKET_OWNERSHIP_UNTIL_COMPLETION:
YES

RESERVATION_REQUIRED:
NO

DEMAND_DRIVEN_FEED_FEASIBLE:
YES

HOST_GUEST_DECODE_SEPARATION_FEASIBLE:
YES

WARNING_VIBRATION_LIKELY_LAYER:
GS FIELD parity / host bob presentation

CAPCOM_BLACK_LIKELY_LAYER:
GAME-DRAWN resource/render path before PSS; exact failure UNKNOWN

OTHER_HIGH_VALUE_IMPROVEMENTS:
1) non-stream/IPU retirement parity; 2) canonical byte ledger; 3) sceMpegInit ownership; 4) Sony private-audio routing; 5) FIELD parity; 6) CAPCOM resource trace

P423_IMPLEMENTATION_READY:
YES

NEXT:
Implementar una transacción de demux reanudable que retenga un PES de vídeo hasta completion, capture v0, aplique la regla original de cursor y valide el ES completo.

CHECKPOINT_DECISION:
NO_COMMIT
