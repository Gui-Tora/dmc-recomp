# P3.11.1 — Fable: auditoría causal adversarial del informe Astra P3.11

Fecha: 2026-09-11. Auditoría READ-ONLY sobre artefactos existentes.
No se ejecutó el juego, PCSX2, builds ni se modificó código semántico.
HEAD principal `888f22ef`, vendor `0f76399b`, sin cambios.

Fuente auditada: `BLOCKER_004_ASTRA_P311_OVERNIGHT.md` + sección P3.11 de la
nota canónica. Evidencia primaria re-verificada: disasm propio del ELF
(`sceMpegInit_original.txt`, `elf_contract.json`), C++ generado
(`viBufBeginPut_0x47ae90.cpp`, `viBufEndPut_0x47af80.cpp`,
`ReadDataGetAddr_0x3fe3f0.cpp`, `_getpic_0x10a3d0.cpp`,
`_nextBit_0x106db0.cpp`, `setD4_CHCR_0x10b800.cpp`), vendor
`MPEG.cpp`/`EeScheduler.cpp`/`ee_scheduler.h`, dumps y JSON de
RUN_008/009/010/011, y scripts `analyze_ram.py`/`probe_buffers.py`.

---

## 0. Veredicto global sobre P3.11

El trabajo experimental de P3.11 es sólido: fronteras ELF, fórmulas de
cursor, offsets de estructura, supervivencia de buffers y el loop 3/3
sobreviven la re-verificación instrucción a instrucción y byte a byte.
Su conclusión causal principal, sin embargo, queda **degradada**: el
over-reset de `sceMpegInit` es real, pero NO es la primera divergencia
contractual del segmento auditado. La "anomalía ES guest" que Astra dejó
como UNKNOWN queda **resuelta por esta auditoría**: es una **inversión
determinista del orden de los callbacks PES por lote de demux**, causada
por la semántica LIFO de la pila de invocaciones por thread del
EeScheduler. Esa divergencia es anterior al init y está demostrada
byte-exacta con los propios dumps de P3.11.

---

## 1. Hallazgo central nuevo de esta auditoría (HECHO)

Con `RUN_008/video_es_preinit.bin` + `demuxed_video_expected.es` +
`buffer_probe.json`:

- Agrupando los 56 PES vídeo por chunk PSS de 64 KiB
  (`pss_offset // 65536`) salen 4 lotes: paquetes 0–15, 16–27, 28–41,
  42–55 (16/12/14/14).
- Construyendo el buffer predicho como: lotes en orden, **payloads de
  cada lote en orden inverso**, el resultado coincide **byte-exacto**
  con `guest[0:228084]`, y el resto del buffer (hasta 524288) es cero.
  SHA-256 de la región reconstruida/observada:
  `9509308635cea7bd07559174891a0077c571a8dbe0aa334a175e8c920d5c5174`.
- Los "ceros iniciales" (28511 bytes) son los paquetes 9–15 del lote 1:
  **zero-stuffing MPEG legítimo del propio ES** (sus payloads son ceros
  también en el ES esperado). No son región sin escribir.
- El sequence header en offset 61084 es el payload del paquete 0 tras la
  inversión: 61084 = 65158 (bytes vídeo del lote 1) − 4074 (payload 0).
- El total 228084 no es coincidencia contable: es el mismo multiconjunto
  de bytes en otro orden por lotes.

Cadena mecánica (verificada en fuente):

1. `processPssBuffer`/`queueStreamCallbackEvent` crean eventos en orden
   de parseo (`MPEG.cpp:1247`); `dispatchStreamCallbacks` los itera en
   orden (`MPEG.cpp:1774`); cada uno hace
   `eeScheduler().queueInvocation(...)` (`MPEG.cpp:1761`).
2. `queueInvocation` hace `push_back` en `m_pendingInvocations` (FIFO,
   `EeScheduler.cpp:1099-1105`).
3. El bucle del dispatcher drena TODO el lote pendiente hacia la pila
   por thread `running->invocations` antes de ejecutar nada
   (`EeScheduler.cpp:256-266`, un push + `continue` por iteración).
4. `activeContext()` devuelve `invocations.back().context`
   (`ee_scheduler.h:126-129`) y el pop es `pop_back`
   (`EeScheduler.cpp:237-238`): **la pila ejecuta LIFO**. El último PES
   encolado corre primero.
5. `invokeCurrentSequence` (`EeScheduler.cpp:1122-1139`) inserta
   deliberadamente en orden inverso (rbegin→rend) para compensar esa
   misma semántica — prueba de que back-runs-first es el contrato real
   de la pila; `queueInvocation` por lotes no compensa.
6. Cada `StrM2vCallBack` guest escribe en el cursor creciente de viBuf:
   `viBufBeginPut` (generado, palabras originales) calcula
   `dest = base(+0x1C) + ((blockCursor(+0x2C0)+queued(+0x2C4))·2048
   + pending(+0x2C8)) % capacity(+0x20)`; con cursor/queued = 0 y
   pending monótono, **el layout físico ES el orden de ejecución**.

Por tanto el layout invertido observado es una **observación dinámica**
del orden real de ejecución de los callbacks: invertido por lote, 3/3
determinista (viBuf SHA `2a8bbb94…` idéntico en RUN_008/009/010).

Corolario (INFERENCIA, mismo mecanismo, no dumpeado): los callbacks de
audio (`StrPcmCallBack`) de cada lote sufren la misma inversión; los
bytes PCM enviados a IOP vía `sendToIOP2area` cuadran en contadores
(0x3C00→0x5C00→0x6000) pero su contenido estaría desordenado por lote.
Igualdad de contadores ≠ contenido correcto — exactamente la trampa que
este prompt pedía vigilar.

¿Es divergencia respecto de ORIGINAL? INFERENCIA fuerte sin PCSX2 nuevo:
el demux original es código guest secuencial con callbacks inline (no
puede reordenar), y un ES que no es la concatenación en orden de sus
payloads no es un stream MPEG decodificable; el original reproduce la
película, ergo su viBuf está en orden. La comparación PCSX2 pasa de
"necesaria" a "confirmatoria".

---

## 2. Tabla de auditoría de claims

| Claim | Afirmación Astra | Veredicto Fable | Evidencia más fuerte | Caveat/corrección |
|---|---|---|---|---|
| A | Contrato `sceMpegInit` [0x109CD0,0x109D6C), MMIO-only, tail-call `sceIpuInit` | SUPPORTED_WITH_CAVEAT | Disasm re-verificado, delay slots incluidos; `setD4_CHCR@0x10B800` verificado MMIO+stack; `sceIpuInit` solo LEE RAM guest (tablas 0x4FC860/0x4FC8B0) | Lo demostrado es "no escribe estructuras software guest"; "preserva la sesión software" es inferencia (correctamente etiquetada). El estado hardware IPU/DMA SÍ se destruye |
| B | `clear()` = OVER-RESET de ownership software | SUPPORTED_WITH_CAVEAT | `resetMpegStubStateUnlocked` (MPEG.cpp:1886-1903) + handler preserva 5 globales CD (2769-2780); R0/R1/R2 | Confirmado para callbacks/config/ownership; para decodedFrames/decoder la equivalencia con estado original es UNKNOWN (ver §4-§6). "Full over-reset" debe matizarse |
| C | 4 frames pre-init destruidos indebidamente | OVERSTATED (en la implicación) | R0 `frames=4`; R3-R6 muestran feed FFmpeg en orden de parseo | El original NO posee frames en ese punto (decodifica después, síncrono). El defecto real es destruir la única representación del trabajo sobre 262144 bytes consumidos SIN contrato de reconstrucción, no destruir un análogo original |
| D | clear → GetPicture vacío → waitExternal → productor parado | SUPPORTED | GP:A/GP:H + código 2513-2551; ausencia R3-R6 post-init 20 s; el waiter es el hilo productor | Necesario para ESTE stall: sí. Suficiente para explicarlo: sí. Que preservar estado restaure el comportamiento ORIGINAL: UNKNOWN, y la inversión de §1 da razones positivas de duda |
| E | Retractación "_getpic software puro"; cadena llega a IPU MMIO | SUPPORTED | `_nextBit` generado lee 0x10002010 (líneas 72/166) y escribe FDEC 0x4000xxxx a 0x10002000 (0x106E88); jal 0x10A448→0x106FF0 verificado | Precisión: `_getpic` = [0x10A3D0,0x10A554); 0x10A448 es el call-site, no la función |
| F | PSS ring, viBuf y state+0x300 byte-idénticos pre/post init | SUPPORTED (y más fuerte) | ram_analysis.json 3/3: same=True ambos buffers, 0 palabras cambiadas; PSS SHA y viBuf SHA idénticos en los TRES runs; state=0x87D680 confirmado dinámicamente por `[MOVIE:gate18]`; offsets confirmados contra generado | Snapshots no atómicos frente a hilos host, pero la igualdad 3/3 hace el riesgo irrelevante para esta conclusión |
| G | 64 KiB lógicamente disponibles desde offset 262144 | SUPPORTED | `ReadDataGetAddr` generado: `(write(+8) + cap(+0x10) − avail(+0xC)) % cap` + base(+4); (0+327680−65536)%327680=262144; sin wrap | "Disponible según el accessor" ≠ "correcto/replayable"; Astra ya lo separó |
| H | Anomalía viBuf ≠ ES extraído, "no comprendida" | RESUELTO: **CONFIRMED CORRUPTION (de ORDEN; contenido íntegro)** | §1: reconstrucción byte-exacta con inversión por lote; mecanismo localizado en scheduler | La comparación base-vs-lineal era premisa válida (cursor en 0, sin wrap) pero la lectura "empieza en ceros = anomalía" ignoraba el zero-stuffing; `probe_buffers.py` retro-validado por la reconstrucción exacta |
| I | Loop 3/3 reproducible | SUPPORTED | result.json 008/009/010: mismo exe SHA `79c7b2…`, mismo env, ENTER+X runtime, PSS_REACHED/TARGET 68.9–70.1 s, `stopped_own_pid`, `exit_before_stop=null` | Prueba reproducción hasta el marcador MPEG_INIT/GP:H; NO prueba logo/título/gameplay (capturas negras). RUN_011 verificado igual |
| J | "Primera divergencia contractual del segmento auditado: over-reset Init" | OVERSTATED → CONTRADICTED por §1 | La inversión de viBuf ocurre durante el demux, ANTES del init, dentro de la misma ventana de evidencia | Enunciado válido más fuerte: primera divergencia contractual demostrada = entrega invertida de callbacks por lote; over-reset = segunda. Divergencia absoluta desde cold boot: sigue UNKNOWN |
| K | P3.12 = comparación PCSX2 viBuf pre/post init | REPLACED | §1 responde la pregunta que P3.12 iba a hacer, sin PCSX2 | Ver §8: el siguiente experimento debe ser el fix mínimo de ORDEN de dispatch + re-verificación con las herramientas ya existentes |

---

## 3. Veredicto exacto: contrato original de `sceMpegInit`

HECHO (re-verificado): [0x109CD0,0x109D6C), 156 bytes. DIntr; lee
D_ENABLER (0x1000F520), escribe D_ENABLEW (0x1000F590) con bit 16
(suspensión DMA); limpia bit 8 (STR) de D3_CHCR (0x1000B000, fromIPU) y
D4_CHCR (0x1000B400, toIPU); re-escribe D_ENABLEW sin bit 16 en el delay
slot del jal EIntr; pone a cero D3_QWC (0x1000B020) y D4_QWC
(0x1000B420); tail-call `sceIpuInit` (retorno v0 heredado). `sceIpuInit`
[0x10B868,0x10BAA0): `setD4_CHCR(1)` (el de 0x10B800, verificado:
DIntr + D_ENABLEW + escribe a0 en D4_CHCR + EIntr, solo MMIO/stack),
reset IPU_CTRL 0x40000000 con esperas BUSY, comando 0, carga de tablas
IQ desde 0x4FC860/0x4FC8B0 al FIFO 0x10007010, comandos SETIQ/SETVQ/
SETTH (0x50/0x58/0x60/0x90 000000), reset final.

Enunciado más fuerte demostrado: **ningún store fuera de stack y MMIO en
toda la cadena**; RAM guest solo se LEE (tablas IQ). "El original
preserva la sesión software MPEG" es inferencia válida sobre RAM guest,
pero NO implica que un estado host derivado (frames FFmpeg) tenga
derecho automático a sobrevivir: el hardware de decodificación (FIFO
IPU, bit pointer, tablas) SÍ se resetea.

## 4. Veredicto exacto: `playbackByMpeg.clear()` / `callbacksByMpeg.clear()`

Over-reset **confirmado para callbacks, configuración y ownership de
entrada aceptada** (el original no toca nada de eso; el HLE lo destruye
y no puede reconstruirlo). Para `decoder`/`decodedFrames` el veredicto
correcto es más estrecho: destrucción de estado anticipado sin análogo
original directo NI contrato de reconstrucción — defecto arquitectural
del HLE, no violación demostrada de un estado que el original conserve.
Clasificación final: **"callbacks/config/input-ownership over-reset
CONFIRMED; frame/decoder preservation-equivalence UNKNOWN"** (la segunda
opción prevista por el prompt), no "full over-reset".

## 5. Tabla de ownership por campo host

| Campo host | Análogo original | Evidencia de preservación original | ¿Debe sobrevivir al Init? | Confianza |
|---|---|---|---|---|
| callbacks (`callbacksByMpeg`) | Registro de callbacks en RAM guest software | Init original no escribe RAM guest (HECHO §3) | SÍ | Alta |
| decoder (FFmpeg ctx) | IPU hardware + objeto libmpeg software | IPU se RESETEA; objeto software sobrevive | Parcial: destruirlo es defendible SOLO si la entrada que representa puede reconstruirse | Media |
| decodedFrames (4) | Ninguno en este punto (original decodifica post-Init, síncrono) | N/A | No per se; lo que debe sobrevivir es la CAPACIDAD de producir esas pictures | Media |
| sawInput | Derivado | N/A | Recreable si hay re-feed; secundario | Alta |
| pssBuffer (PES parcial) | Bytes en ring PSS guest (sobreviven) | Ring byte-idéntico (HECHO) | SÍ (aquí era 0 bytes) | Alta |
| videoSequenceSyncBuffer | Derivado del ES guest | viBuf sobrevive (HECHO) | Recreable; aquí 0 bytes | Alta |
| width/height | Config en RAM guest (sobrevive) | Init original no escribe RAM guest | SÍ | Alta |
| decodeMode | Config en RAM guest | Ídem | SÍ | Alta |
| imageBufferAddr | Config/destino en RAM guest | Ídem | SÍ | Alta |
| generation/EOF por playback | Sin análogo original (bookkeeping HLE) | N/A | SÍ dentro del propio contrato HLE: el objeto nuevo con generation=0 rompe el gate de `sceMpegIsEnd` frente a los globales preservados | Alta |
| Globales transporte CD | Estado transporte | El handler HLE ya los preserva (verificado 2769-2780) | SÍ (ya lo hace) | Alta |

## 6. Los cuatro frames pre-init

Veredicto: **caché especulativa/anticipada del HLE**, no trabajo que el
original "poseería" en el mismo punto semántico. R3-R6 demuestran que el
feed a FFmpeg ocurre en orden de parseo durante el demux (tamaños
4074/4077/4077/4048 = paquetes 0–3 en orden), es decir el host decodifica
con la entrada CORRECTA aunque el guest reciba los callbacks invertidos.
Destruirlos es dañino porque son la única representación utilizable del
trabajo sobre los 262144 bytes consumidos; "destruirlos es wrong frente
al original" NO está demostrado (el reset IPU original también destruye
pipeline de decodificación en vuelo; el original simplemente no había
decodificado nada aún).

## 7. Cadena causal clear → GetPicture vacío → waitExternal

- clear → estado nuevo vacío: HECHO (R1/R2, código).
- estado vacío + sin EOF + sin fallo → waitExternal: HECHO (GP:A/GP:H;
  condición exacta en MPEG.cpp:2513-2516).
- waitExternal → productor parado: HECHO en la ventana observada; el
  hilo que espera es el mismo que ejecuta Movie_ctrl/demux (auto-bloqueo).
- ¿Clear NECESARIO para el vacío observado? SÍ (con los 4 frames vivos
  se tomaría la rama have-frame).
- ¿SUFICIENTE para el stall? SÍ para este stall.
- ¿Preservar estado suficiente para restaurar el comportamiento
  ORIGINAL? UNKNOWN — y con §1, hay evidencia positiva de que NO basta
  (audio por lotes invertido; viBuf inutilizable para cualquier bridge).

Coincide con el veredicto esperado: contribuyente causal necesario
demostrado; suficiencia del fix UNKNOWN.

## 8. Ranking de próximos experimentos

Nuevo candidato G (surge de §1): **corregir el orden de ejecución de
lotes de invocaciones RPC** (hacer que un lote encolado con
`queueInvocation` ejecute FIFO — p.ej. volcar el lote con la misma
inversión compensatoria que ya usa `invokeCurrentSequence` — sin tocar
MPEG), y re-verificar con el loop 3/3 + `analyze_ram`/`probe_buffers`
que viBuf == ES esperado byte a byte.

| Rank | Exp. | Ganancia de información | Riesgo de enmascarar | Cambio semántico | Falsación | Coste |
|---|---|---|---|---|---|---|
| 1 | **G (nuevo)** | Máxima: convierte una divergencia demostrada en variable controlada y separa Modelos 1/3 de 2 | Nulo (corrige, no oculta: el orden correcto es el contrato) | Mínimo y acotado al dispatcher | Sí: si tras ordenar, el stall persiste igual, Model 2 pierde su rama "el desorden explica el stall actual" | Bajo |
| 2 | D (preservar solo callbacks/config en Init) | Alta para la frontera Init | Bajo | Pequeño | Sí | Bajo |
| 3 | B (PCSX2 viBuf compare) | Ahora solo confirmatoria | Nulo | Nulo | Débil | Medio |
| 4 | C (instrumentar cursores StrM2vCallBack) | Ya respondida por los dumps | Nulo | Nulo | N/A | Medio |
| 5 | E (preservar MpegPlaybackState completo) | Media | ALTO (oculta §1 y defectos de Init por separado) | Medio | Pobre | Medio |
| 6 | A (fix mínimo ownership sin diseño) | Media | Alto | Medio | Pobre | Medio |
| 7 | F (bridge síncrono GetPicture) | Prematuro | Alto | Grande | Pobre | Alto |

**Experimento único elegido: G.** Es el mínimo experimento que decide
entre modelos: deja el sistema con demux correcto y el mismo Init, de
modo que la siguiente observación de GetPicture/stall atribuye la causa
restante al Init sin confusión. B, tal como estaba, habría gastado una
sesión PCSX2 para descubrir lo que los dumps ya contenían.

**P3.12 propuesto: REPLACED.** Nombre y objetivo recomendados:

> **P3.12 — SCHEDULER_BATCH_INVOCATION_ORDER_FIX_AND_REVERIFY**
> (1) Corregir el orden FIFO de ejecución de lotes de invocaciones
> encoladas con `queueInvocation` (solo dispatcher; MPEG intacto).
> (2) Loop 3/3 + `analyze_ram`/`probe_buffers`: verificar
> `viBuf[0:228084] == demuxed_video_expected.es` byte a byte y SHA
> estable; opcionalmente dump del área IOP de audio para confirmar la
> inversión inferida y su corrección.
> (3) Re-observar R0-R8/GP con el mismo diagnóstico para re-derivar el
> stall de Init con el demux ya correcto. Sin tocar `sceMpegInit` en
> este prompt (una variable por vez); auditar de paso los otros call
> sites de `queueInvocation` (EeScheduler.cpp:1294/1916/1946) por el
> mismo patrón.

## 9. Modelos causales

- **Model 1 (Init over-reset primario):** parcialmente soportado
  (necesario y suficiente para el stall observado), pero su premisa
  "demux/input suficientemente correcto" está CONTRADICHA para la vía
  guest (viBuf invertido; audio inferido invertido). Su predicción
  ("corregir ownership restaura progreso significativo") queda en duda
  para fidelidad, aunque podría restaurar avance del hilo.
- **Model 2 (buffer pre-init ya wrong):** su premisa central está
  CONFIRMADA y explicada mecánicamente (§1). Su predicción ("preservar
  host state puede ocultar la divergencia previa") es válida para la
  vía guest/audio.
- **Model 3 (representaciones mismatched, falta contrato):** el mejor
  marco global: host path en orden (FFmpeg alimentado en parseo), guest
  path invertido, Init destruye el host path sin contrato. Ambos
  defectos requieren corrección; ninguno solo basta.
- **Model 4 (comparación malinterpretada):** CONTRADICTED en su
  conclusión ("solo queda el defecto de Init"): la diferencia es real.
  Su intuición parcial era correcta (los ceros iniciales eran contenido
  legítimo, no región vacía), pero la reconstrucción demuestra desorden
  real de entrega.
- **Model 5 (nuevo, esta auditoría):** "la pila LIFO de invocaciones
  por thread invierte TODO lote multi-evento de callbacks (vídeo y
  audio)". Demostrado para vídeo; inferido para audio; discriminable
  con el experimento G.

## 10. Correcciones que la historia canónica debe registrar

1. Sustituir "primera divergencia contractual localizada en este
   segmento: over-reset de sceMpegInit" por: primera divergencia
   contractual demostrada en la ventana observada = **inversión por
   lote del orden de callbacks del demux** (pre-init); el over-reset de
   Init es la **segunda**.
2. Cerrar la "anomalía ES guest": resuelta; inversión determinista por
   lote de 64 KiB; contenido íntegro (228084/228084 byte-exacto); los
   ceros iniciales son zero-stuffing legítimo del ES.
3. "No atribuirlo todavía a scheduler…" → ya atribuible: pila LIFO
   `GuestThread::invocations` + drenaje por lotes de
   `m_pendingInvocations` (EeScheduler.cpp:256-266, 237-238;
   ee_scheduler.h:126-129).
4. `_getpic@0x10A448` → `_getpic` = [0x10A3D0,0x10A554); 0x10A448 es el
   call-site del jal a `_nextHeader`.
5. Determinismo más fuerte de lo reportado: PSS y viBuf comparten SHA
   entre RUN_008/009/010 (no solo viBuf).
6. Los 4 frames: precisar que el original no posee frames en ese punto;
   el defecto es la ausencia de contrato de reconstrucción.
7. El resultado ffprobe "6 frames con errores" del ES guest queda
   explicado por la inversión (pictures completas locales dentro de
   cada lote, límites rotos entre lotes).
8. P3.12 (comparación PCSX2) degradado de decisivo a confirmatorio.

## 11. UNKNOWNs restantes

- Divergencia absoluta desde cold boot (anterior a la ventana movie).
- Contenido real del área de audio IOP (inversión inferida, sin dump).
- Bytes pendientes dentro de FFmpeg al destruir el decoder.
- Si demux ordenado + ownership corregido produce presentación GS
  visible (título/gameplay siguen sin evidencia de viabilidad).
- Comportamiento con múltiples `sceMpegInit` y separación entre películas.
- Si otros usuarios de `queueInvocation` (EeScheduler.cpp:1294/1916/1946)
  emiten lotes >1 y sufren la misma inversión.
- Confirmación PCSX2 del viBuf original en orden (confirmatoria).
- Causa/impacto del `[ps2xIOP:warning] [CDMODULE] unsupported fno=2
  mode=2 size=0x132000` visto en RUN_008 (fuera de alcance, anotado).

## 12. Git status e integridad

`git status --short` al cierre (idéntico al inicio, más este informe):

```text
 M analysis/notes/BLOCKER_004_pss_video_output.md
 M runtime/dmc_overrides.cpp
 M upstream.lock.json
?? analysis/notes/BLOCKER_004_ASTRA_AUDIT.md
?? analysis/notes/BLOCKER_004_ASTRA_P311_OVERNIGHT.md
?? analysis/notes/BLOCKER_004_FABLE_P3111_CAUSAL_AUDIT.md
?? analysis/notes/BLOCKER_004_P36_HEAP_COLLISION_AUDIT.md
?? analysis/tools/
?? patches/BUILD_ffmpeg_private_link_scope.patch
?? runtime/p311_pad_test.inc
```

HEAD principal `888f22ef…`, vendor `0f76399b…`, sin cambios. La nota
canónica NO fue modificada por esta auditoría. Los únicos comandos
ejecutados fueron lecturas y cálculos Python read-only sobre dumps ya
existentes (ninguno escribió en los directorios RUN_NNN ni en fuentes).

**Confirmación:** NO game run, NO PCSX2 run, NO build/compile/relink,
NO modificación semántica de fuentes, NO commit, NO push, NO clean ni
regenerate, NO git reset/checkout/restore, NO web research.
