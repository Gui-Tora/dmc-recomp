# P3.14.1 — Fable: auditoría adversarial del contrato síncrono MPEG (candidato P3.14)

Fecha: auditoría read-only sobre el árbol dirty actual. Sin commit, sin push,
sin builds, sin corridas nuevas, sin cambios semánticos de código.

## 1. Estado de entrada (HECHO)

- Main HEAD: `3483f92de5ed2e4d1875a6a962f6c0a23a421eff` (esperado ✓).
- Vendor HEAD: `ed83ab53fa4635715eb10b72bdfe9fa7469e26cd` (esperado ✓).
- Main dirty: `M analysis/notes/BLOCKER_004_pss_video_output.md` (+46, apéndice
  P3.12–P3.14), `?? BLOCKER_004_P314_SYNCHRONOUS_GUEST_ES_REDECODE.md`.
- Vendor dirty: solo `ps2xRuntime/src/lib/Kernel/Stubs/MPEG.cpp` (+125/−1),
  coincide con lo declarado por P3.14. Diff leído completo.

## 2. Archivos atribuibles a P3.14

Único cambio de código: MPEG.cpp — campo `p314ConsumedEsBytes` en
`MpegPlaybackState`, helper `isP314SyncRedecodeEnabled()`, accessor
`mpegGuestRead32`, bloque síncrono en `sceMpegGetPicture` (antes del `if`
de `waitExternal`), y forzado de modo `ownership` en `sceMpegInit` cuando
P3.14 está activo (rechazo explícito de `full`). Coincide con el informe.

## 3. Reconstrucción independiente de la arquitectura

Verificada contra el diff: en GetPicture con cola vacía, lee
`viBufBase(+0x1C)`, `viBufCapacity(+0x20)`, `videoBytesTotal(+0x28)` del
`movieState = mpegAddr − 0x278`; `available = max(0, total − cursorHost)`;
alimenta un decoder FFmpeg (creado si falta) en chunks de 4096 bytes desde
`viBufBase + cursorHost` (lineal, sin módulo), avanzando el cursor host,
hasta producir trama o agotar `available`; si `available==0` cae al
`waitExternal` existente. No escribe RAM guest (solo `getConstMemPtr`).
Los campos +0x2C0/+0x2C4/+0x2C8 no se leen ni escriben. HECHO.

## 4-7. Auditoría del ring buffer, +0x28, modelo productor/consumidor y wraparound

### El hallazgo central de esta auditoría (HECHO, demostrado dentro de la propia ventana testeada)

**P3.14 tiene un deadlock terminal de inanición del productor, y ocurrió
en las 4 corridas de validación.** La cadena, verificada estáticamente y
confirmada por los logs:

1. **Productor:** `StrM2vCallBack@0x47AFE0` → `viBufBeginPut@0x47AE90`.
   El espacio libre real es `(0xFE − queuedBlocks(+0x2C4))·2048 −
   pendingBytes(+0x2C8)` = **520192 − pending** con queued=0 (generado,
   instrucciones 0x47AED0-0x47AEFC: constante `0xFE`, `<<11`, resta de
   pending). El helper de copia copia solo lo que cabe y
   `viBufEndPut@0x47AF80` incrementa `+0x2C8` y `+0x28` con los bytes
   realmente copiados. El valor de retorno del callback (1/0) se pierde:
   el despacho HLE por `RpcCallback` no tiene canal de retorno.
2. **Consumidor original:** `viBufAddDMA@0x47B0D0` es la ÚNICA función
   que avanza `+0x2C0/+0x2C4/+0x2C8` (convierte pending en tags DMA para
   IPU). Su ÚNICO caller es `MpegNodataCallBack@0x47B300` (grep del
   generado). Ese callback se registra vía `sceMpegAddCallback`, que en
   el HLE lo guarda con `stream=false` (MPEG.cpp:2205).
3. **El HLE jamás lo invoca:** el único filtro de despacho de callbacks
   es `callback.stream && callback.type == streamType`
   (MPEG.cpp:1341, `matchingStreamCallbacks`). No existe ningún sitio
   que despache callbacks `stream=false`. **El consumo guest-visible es
   estructuralmente inalcanzable en RECOMP.**
4. **Consecuencia:** `pending(+0x2C8)` crece monótonamente; al llegar a
   520192−payload el productor no puede escribir más. RUN_029:
   `videoBytesTotal` se congela en **517313** desde GP#8 (gap
   520192−517313 = **2879 < 4063**, el payload PES mínimo). Idéntico en
   las 4 corridas (secuencia byte-a-byte igual).
5. El cursor host agota `available` (GP#11: 1217), cae a `waitExternal`,
   y **el hilo suspendido es el mismo que ejecuta el demux** → nada
   completará la espera. El log de RUN_029 tras GP#11 contiene
   exclusivamente `GP:VSync` ticks (600→5880) hasta el final. El último
   `MOVIE:chunk` es offset 0xf0000 con `remaining=0x195c004`: **el 96.5%
   de la película nunca se leyó**. `cdStreamEofSeen=0` → `sceMpegIsEnd`
   nunca reporta fin → tampoco hay salida por EOF.

**Reinterpretación obligada de la llamada #11:** el informe P3.14 la
describe como «ES agotado hasta el próximo lote de demux» (§6). Es
incorrecto: la producción llevaba congelada desde GP#8 (visible en su
propia tabla: `videoBytesTotal=517313` en #8, #9, #10, #11 con
`available` decreciente 165057→111809→54465→1217). No hay «próximo
lote»: es un **estado terminal determinista a ~520 KB de ES de vídeo
(~3.5% de la película), alcanzado en el 100% de las corridas**.

### Significado de +0x28 (HECHO)

Contador acumulado monótono de bytes ES de vídeo *aceptados* en el ring
(incrementado por `viBufEndPut` con los bytes realmente copiados; nunca
decrementado por ninguna función observada). Puede exceder `capacity`
solo si existe consumo que libere espacio — en RECOMP actual no puede
(techo efectivo ≈ 520192). Reset entre películas: UNKNOWN estático (no
localizado el memset/init del movieState en esta auditoría).

### ¿Wraparound? La pregunta se disuelve (HECHO)

El ring no puede envolver en la arquitectura actual: el productor se
detiene ANTES de la capacidad. Que el consumo máximo observado (517313)
quedara bajo 524288 no es margen de seguridad: **es el síntoma del
defecto**. Recíprocamente, añadir `% capacity` al cursor host no
arreglaría nada: el bloqueo del productor depende de `+0x2C8` guest, que
ningún bookkeeping host-only puede liberar. El modelo "producedTotal +
consumedTotal + `% capacity`" del prompt es necesario pero
**insuficiente**: sin consumo guest-visible el sistema muere primero.
Con consumo guest-visible añadido, el direccionamiento físico
`base + (K % capacity)` sí es coherente con la fórmula de escritura de
`viBufBeginPut` (posición = acumulado mod capacity), con guarda de
staleness `produced − K ≤ capacity` y lecturas partidas en el borde.

## 8. Posición inicial del decoder fresco (HECHO)

El primer byte alimentado es `viBufBase + 0` = el ÚNICO sequence header
del ES observado: escaneo del snapshot viBuf de RUN_029 (getpicture.bin,
base 0x1C337D0): **1 × `000001B3` (offset 0)**, 6 × GOP `B8`, 6 pictures
en 228084 bytes. El éxito del decode SÍ depende de arrancar en el inicio
del ES; es válido aquí porque `sceMpegInit` ocurre en el arranque de la
película y el cursor se resetea con el objeto playback. Un Init a mitad
de stream re-decodificaría desde base (contenido viejo del ring):
escenario no ejercitado por este juego en la ventana observada
(call sites de Init: entrada de `MpegMovieDecodeInit` y salida de
`MpegMovieDecode`).

## 9. Mapa de ownership de decode (HECHO estático + dinámico)

`feedElementaryStream` (async) sigue cableado INCONDICIONALMENTE a cada
PES de vídeo demuxado (MPEG.cpp:1547), también con P3.14 activo. Post-init
corre con la playback nueva (`waitingForVideoSequenceHeader=true`) sobre
bytes de mitad de stream: acumuló ~289 KB en `videoSequenceSyncBuffer`
sin encontrar header (solo hay uno, en el offset 0 que el async ya no
verá) y por eso NUNCA llegó a `decoder->feed` en la ventana. **La
no-interferencia observada es suerte del contenido del stream, no
diseño**: si existiera un sequence header posterior (otras películas, o
un cambio de secuencia en esta), el async ejecutaría su resync-clear
(MPEG.cpp:1246-1247: `decoder.reset()` + `decodedFrames.clear()`),
**destruyendo el decoder del camino síncrono en pleno uso** y arrancando
un feed competidor desde otra posición del stream. Además el async marca
`sawInput=true` y actualiza timing — estado compartido mutado por un
camino teóricamente muerto. No hay un dueño único de decode: hay dos
caminos cableados al mismo `playback.decoder`, separados solo por la
densidad de headers del stream.

## 10. Backpressure (SUPPORTED)

El bucle síncrono se detiene en la primera trama (`while
decodedFrames.empty() && ...`); un chunk podría producir >1 trama y
quedarían en cola (no observado: `frames=0` en las 40 entradas).
`kMaxDecodedPicturesAhead=8` intacto; `mpegDemuxBackpressured` nunca se
activó post-init (cola ≤1). Sin defecto aquí.

## 11. EOS vs no-data (código correcto, interpretación del informe incorrecta)

El código no fabrica EOF ni éxito: con `available==0` cae al wait
existente y `sceMpegIsEnd` sigue gobernado por EOF de productor. PERO la
distinción «temporalmente sin ES» vs «fin real» es inalcanzable en esta
arquitectura: el caso «temporal» es siempre terminal (§4-7). El deadlock
predicho por el prompt (`hostConsumed == guestProduced` con más PSS por
llegar) es exactamente lo observado, con la precisión de que el productor
también está bloqueado, y de que la llamada #11 consumió los últimos
1217 bytes antes de suspender (cursor=517313=total).

## 12. Ciclo de vida sesión/generación (SUPPORTED_WITH_CAVEAT)

`p314ConsumedEsBytes` vive en `MpegPlaybackState`; se resetea a 0 en:
`sceMpegInit` (ownership → `makeFreshPlaybackState`, no está en la lista
de preservación de `applyP313OwnershipMode`, MPEG.cpp:910-918),
`notifyMpegCdStreamStart` (2083) y `sceMpegDelete` (erase). `available`
está protegido contra underflow (clamp a 0). Riesgo restante: segunda
película con el mismo `movieState` — si el guest NO re-cera `+0x28`, tras
el Init del nuevo movie `available = total_viejo − 0` → re-lectura de
contenido viejo del ring; si SÍ lo re-cera, comportamiento correcto.
UNKNOWN estático; FOLLOWUP.

## 13. Mutación de estado guest (HECHO: ninguna; y esa es la divergencia)

P3.14 no escribe RAM guest (verificado: solo `getConstMemPtr`). Tras 10
pictures el guest muestra `pending=+0x28=517313`, `blockCursor=queued=0`
— un estado que en ORIGINAL es imposible tras 10 pictures (el consumo
IPU/DMA habría avanzado esos campos y liberado el ring). La consecuencia
guest-visible de NO mutar es precisamente el estancamiento del productor.
Medición PCSX2 mínima si se quiere cuantificar el original (no requerida
para justificar P3.14.2, la cadena estática basta): breakpoints antes del
primer `sceMpegGetPicture` original y en el retorno `0x47A8EC`; leer
`movieState+0x1C/+0x20/+0x28/+0x2C0/+0x2C4/+0x2C8/+0x300` en ambos
puntos, una sola pasada.

## 14. Contrato original en la frontera del caller

HECHO heredado (P3.8.1/P3.9): el original retorna de GetPicture de forma
síncrona (v0=1 observado en `0x47A8EC`) con el IPU trabajando por DMA.
P3.14 produce retorno síncrono con trama real: la propiedad «GetPicture
puede crear salida síncronamente desde ES ya disponible» se sostiene en
la ventana. Caveats: (a) el HLE retorna **v0=0 en todos los caminos**
(`setReturnS32(ctx,0)`), mientras la observación dinámica del original
registró **v0=1**; `frameCount` se escribe en `mpegAddr+0x08`. Qué usa el
caller para decidir presentación no está resuelto aquí — candidato serio
para explicar «decode progresa pero no hay imagen», fuera del alcance de
esta auditoría; (b) la temporización/pacing no es equivalente.
Clasificación: **PLAUSIBLE BUT INCOMPLETE**.

## 15. Tabla de veredictos A–N

| # | Claim | Veredicto | Nota |
|---|---|---|---|
| A | Descarta decoder pre-init | SUPPORTED | `decoderAfter=0`; `decoderFresh=1` solo #1, 4/4 |
| B | Descarta tramas pre-init | SUPPORTED | `framesAfter=0` en init; `frames=0` en 40/40 entradas |
| C | Primera trama post-init es nueva | SUPPORTED | decoder fresco + feed desde viBuf; framesAfter=1 tras 9 chunks |
| D | Propiedad síncrona caller-visible | SUPPORTED_WITH_CAVEAT | retorno v0=0 vs v0=1 observado en original; pacing no equivalente |
| E | Reconstrucción = ES lógicamente no leído | SUPPORTED_WITH_CAVEAT | válido solo porque el consumo guest ≡ 0 — que es el defecto; el «no leído» original lo define +0x2C0/+0x2C4/+0x2C8 |
| F | Direccionamiento lineal seguro en RUN_029–032 | SUPPORTED | máx 517313 < 524288 — pero es síntoma del deadlock, no margen |
| G | Direccionamiento lineal seguro en general | CONTRADICTED | con consumo real el ring envuelve y el cursor lineal sale del buffer; sin consumo, deadlock antes |
| H | `% capacity` simple bastaría en general | CONTRADICTED | necesario pero insuficiente: el bloqueo del productor depende de +0x2C8 guest |
| I | Cursor host-only suficiente para equivalencia guest | CONTRADICTED | inanición del productor demostrada en ventana (freeze en 517313 desde GP#8, 4/4) |
| J | No hay doble feed / doble ownership | OVERSTATED | no ocurrió doble decode en ventana, pero el async sigue cableado y su resync-clear puede destruir el decoder síncrono; separación por suerte del contenido |
| K | Distinción EOS/no-data correcta | OVERSTATED | el código no fabrica EOF, pero la lectura del #11 como transitorio es errónea: estado terminal |
| L | Semántica de reset sesión/generación correcta | SUPPORTED_WITH_CAVEAT | coherente en ventana; ciclo de +0x28 entre películas UNKNOWN |
| M | `SUSTAINED_SYNC_REDECODE` justificado en ventana | OVERSTATED | forma real: ráfaga de 10 tramas agotando el buffer pre-acumulado (~12 s) + inanición permanente el resto; techo arquitectónico ≈ 3.5% de la película |
| N | Listo para checkpoint de producción | CONTRADICTED | deadlock determinista en el camino normal + riesgo latente de ownership de decode |

## 16. Tabla de severidad

| # | Defecto | Severidad |
|---|---|---|
| 1 | Consumo guest-visible inexistente (nodata callback jamás despachado → `viBufAddDMA` inalcanzable) → productor se congela en ~520192 bytes → deadlock terminal en TODAS las corridas | **BLOCKING / MUST_FIX_BEFORE_CHECKPOINT** |
| 2 | `feedElementaryStream` async sigue cableado con P3.14 activo; resync-clear latente destruye el decoder síncrono y crea feed competidor | **MUST_FIX_BEFORE_CHECKPOINT** (gating de ownership) |
| 3 | Cursor lineal sin módulo ni guarda de staleness | FOLLOWUP_REQUIRED (irrelevante hasta corregir #1; obligatorio junto con #1) |
| 4 | Pérdida silenciosa de bytes en `StrM2vCallBack` con ring lleno (retorno ignorado por RpcCallback) | FOLLOWUP_REQUIRED (desaparece si #1 se corrige antes de llegar a ring lleno) |
| 5 | Retorno v0=0 del HLE vs v0=1 observado en original | FOLLOWUP_REQUIRED (candidato para «sin imagen visible») |
| 6 | Ciclo de vida de +0x28 entre películas vs reset del cursor | FOLLOWUP_REQUIRED |
| 7 | `videoSequenceSyncBuffer` acumula bytes muertos (cap 2 MiB) y el async muta `sawInput`/timing | NONBLOCKING_FOR_CURRENT_SCOPE (subsumido por #2) |

Nota sobre la instrucción del prompt: el out-of-bounds tras 524288 que
el prompt pedía tratar como MUST_FIX resulta **inalcanzable** en la
arquitectura actual — el defecto real (deadlock por inanición) es
estrictamente peor: no es un riesgo de sesiones largas, ocurrió en las 4
corridas de validación y limita la película al 3.5%.

## 17–25. Respuestas explícitas

17. **¿RUN_029–032 son evidencia válida?** SÍ — logs deterministas,
    autoconsistentes, mismo exe SHA por tanda, milestones correctos. Lo
    inválido es parte de su INTERPRETACIÓN en el informe (llamada #11 y
    riesgo de wrap).
18. **¿La limitación de wrap invalida esas corridas?** NO. Las corridas
    son válidas; su «seguridad» frente al wrap es un artefacto del
    defecto de inanición (la producción se congeló bajo la capacidad).
19. **¿Invalida el diseño en general?** SÍ, tal como está: la ausencia
    de consumo guest-visible (de la que el cursor lineal es un síntoma)
    hace imposible reproducir más de ~520 KB de ES de vídeo. El concepto
    (redecode síncrono desde viBuf con decoder fresco) sobrevive; la
    implementación del modelo productor/consumidor no.
20. **¿Basta `% capacity`?** NO. Es necesario para el direccionamiento
    físico una vez exista consumo, pero se requiere además: consumo
    guest-visible que libere `+0x2C8` (o el despacho del nodata callback
    para que el propio guest lo haga), y guarda de staleness
    `produced − consumed ≤ capacity` con lecturas partidas.
21. **¿`hostConsumedBytes` es un contador absoluto de consumidor?** SÍ,
    dentro de una sesión: monótono desde 0 tras cada Init (no
    ring-relativo). Su vida está atada a los resets del objeto playback
    (Init/CdStreamStart/Delete). Coherencia entre películas frente a
    +0x28: UNKNOWN.
22. **¿Puede decodificar bytes stale/releídos?** SÍ en dos escenarios no
    ejercitados: re-Init a mitad de stream (cursor→0 sobre ring
    superviviente) y, si se añadiera consumo sin guarda de staleness,
    lectura tras sobre-escritura del productor. No ocurrió en ventana.
23. **¿Puede saltarse bytes nuevos envueltos?** En la forma actual los
    bytes envueltos no pueden existir (el productor se detiene antes).
    Si se habilitara consumo manteniendo el cursor lineal: SÍ — leería
    fuera del ring en lugar de los datos envueltos.
24. **¿Async y sync pueden decodificar el mismo ES dos veces?**
    Estructuralmente SÍ (ambos caminos reciben los mismos payloads); en
    la ventana el async nunca alcanzó `decoder->feed` (único sequence
    header en offset 0). El riesgo real es peor que la duplicación: el
    resync-clear del async destruye el decoder síncrono.
25. **¿Equivalencia en la frontera del caller?** PLAUSIBLE BUT
    INCOMPLETE (sección 14: síncrono con trama real ✓; valor de retorno
    y pacing sin resolver).

## 26. Recomendación

**P3.14.2 — GUEST_VISIBLE_VIBUF_CONSUMPTION_AND_DECODE_OWNERSHIP**

(no CHECKPOINT_P314: fallan tres criterios de la puerta — defecto
determinista en el camino normal, ring incoherente, riesgo de
duplicación/destrucción de decode; no REJECT: la premisa causal de P3.14
— decoder fresco + ES guest superviviente bastan, sin estado pre-init —
quedó CONFIRMADA por sus propios datos y sobrevive a esta auditoría).

## 27. Alcance exacto de P3.14.2 (sin implementar aquí)

1. **Consumo guest-visible.** Opción A (preferida, más fiel): despachar
   el callback no-stream registrado (`sceMpegAddCallback`,
   `stream=false` — `MpegNodataCallBack@0x47B300`) cuando el camino
   síncrono agote `available`, dejando que el propio guest ejecute
   `viBufAddDMA` y avance `+0x2C0/+0x2C4/+0x2C8`. Requiere auditar
   primero qué espera exactamente ese callback (args, gate `+0x300`) y
   si el resto de su efecto (tags DMA/`SetD4chcr`) es inocuo bajo HLE.
   Opción B (menor, host-side): tras cada trama servida, reflejar el
   consumo en guest bajo la semántica del semáforo `+0x2F8`:
   decrementar `+0x2C8` y avanzar `+0x2C0/+0x2C4` en bloques de 2048
   según la aritmética exacta de `viBufAddDMA` (auditarla antes de
   copiarla). Elegir UNA tras ese mini-audit estático.
2. **Cursor con módulo + staleness.** `physical = base + (consumed %
   capacity)` con lectura partida en el borde y guarda
   `produced − consumed ≤ capacity`.
3. **Dueño único de decode.** Con P3.14 activo, cortar el
   `decoder->feed` del camino async (`feedElementaryStream`) — NUNCA la
   entrega de callbacks guest — o unificar ambos en un solo dueño.
4. **Re-validación obligatoria (criterio de éxito nuevo):** 3 corridas
   donde `videoBytesTotal` supere 524288 (ring envolviendo de verdad),
   éxitos de GetPicture ≫ 10 sostenidos más allá del antiguo techo de
   517313, y comparación byte-exacta de una ventana envuelta del ES
   reconstruido. Mantener opt-in y el fallback `waitExternal`.

Fuera de alcance de P3.14.2 (seguimientos separados): retorno v0 de
GetPicture vs original (severidad 5), ciclo +0x28 entre películas
(severidad 6).

## 28. Git status final

Idéntico al de entrada más este informe:

```text
main:   M analysis/notes/BLOCKER_004_pss_video_output.md
        ?? analysis/notes/BLOCKER_004_P314_SYNCHRONOUS_GUEST_ES_REDECODE.md
        ?? analysis/notes/BLOCKER_004_P3141_FABLE_SYNC_MPEG_AUDIT.md
vendor: M ps2xRuntime/src/lib/Kernel/Stubs/MPEG.cpp
```

HEADs sin cambios (`3483f92…` / `ed83ab53…`). Nota canónica NO tocada
por esta auditoría.

## 29. Confirmaciones

NO commit, NO push, NO clean, NO normalización del vendor, NO
experimentación runtime (cero corridas nuevas: RUN_028–032 existentes
fueron suficientes para demostrar el defecto), NO cambios semánticos de
código, NO PCSX2. Solo lecturas y cálculos Python read-only sobre dumps
y logs existentes.
