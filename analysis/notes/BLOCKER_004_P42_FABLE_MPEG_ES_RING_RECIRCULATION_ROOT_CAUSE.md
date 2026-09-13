# P4.2 — Fable: causa raíz de la corrupción MPEG tardía (recirculación viBuf)

Investigación de causa raíz con instrumentación temporal (revertida al
cierre). Dos corridas instrumentadas (RUN_069 + confirmación RUN_070),
gates ambientales probados en ambas ([P314:GP]=24, [P3142:ring]=23,
LAUNCH_ENV_CHECK con `DMC_P314_SYNC_REDECODE=1`,
`DMC_P313_INIT_MODE=ownership`, arena estándar, ISO exacta). Sin commit,
sin push. Vendor final: `19911ce` limpio (verificado tras revert).

## VEREDICTO EN UNA LÍNEA

**El decoder recibe bytes equivocados: el stream que RECOMP alimenta a
FFmpeg es el ES canónico con payloads PES ENTEROS OMITIDOS, y la primera
omisión (4063 bytes, offset lógico 2,081,537) la produce el PRODUCTOR
guest al descartar íntegro un payload que no cabe en el viBuf saturado —
porque el demux HLE no propaga backpressure (el retorno del callback se
ignora y el PSS avanza incondicionalmente). FFmpeg y la aritmética de
consumo/wrap de P3.14.2 quedan exonerados byte a byte.**

## 1. Referencia canónica (HECHO)

Extraída sin transcodificar del ISO exacto (`Devil May Cry 2001.iso`),
recurso PSS del opening en LBA 0x1F62B8, tamaño 0x1A5C004:

- PSS SHA256 `9855ab95b211ed0f63ed40283c67d2214c9f54e27bb7ecddf02eb076705da0f6`
- Video ES (payloads 0xE0 concatenados, demuxer propio lossless):
  **23,654,877 bytes**, 5808 paquetes, SHA256
  `b2e29f64807a8336e2181788bbd2a343af4d1fbdc9cd94ee9de0dbd5a4ecfb71`
- Decodifica limpio offline: 0 warnings en 200 frames (verificado en
  P4.1F y reconfirmado aquí).

## 2. Instrumentación temporal (revertida)

Solo `MPEG.cpp` (+49 líneas, opt-in `DMC_P42_TRACE=1`): (a) captura
append lossless del payload de vídeo en `processPssBuffer` →
`p42_demux_es.bin` (boundary B); (b) captura append de (src,chunk)
inmediatamente antes de `decoder->feed` → `p42_ffmpeg_input.bin`
(boundary E/F — los MISMOS punteros/longitudes que ve FFmpeg); (c)
`[P42:ev]` por evento M2V con `len` y lecturas guest `+0x28`/`+0x2C8` en
el despacho (contabilidad del productor por lote). Build quirúrgico:
solo MPEG.cpp compiló; relink `_BuildLinkAction` con 0 cpp. Exe
diagnóstico `616cef77a509…`.

Incidencias de harness (documentadas): un primer intento sin `--confirm`
no envió teclas (PSS_NOT_REACHED, inválido por alcance, no por gates); al
relanzar, `run.py` reutilizó el directorio `RUN_068` sobrescribiendo el
result.json anterior de esa carpeta (pérdida menor de un artefacto
histórico de P4.1.3, anotada aquí).

## 3. Resultado byte a byte (HECHO, RUN_069; confirmado RUN_070)

| Boundary | ¿Coincide con canónico? | Primera divergencia |
|---|---|---|
| ISO PSS | ✓ (referencia) | — |
| Runtime PSS input | ✓ implícito (el demux ES es byte-perfecto ⇒ el PSS recibido es correcto en toda la zona) | — |
| demux video ES (B) | **✓ BYTE-PERFECTO** — 15,429,632 bytes == prefijo canónico exacto | ninguna |
| guest viBuf tras writes (C) | **✗ PRIMER DIVERGENTE** | payload entero no escrito, offset lógico 2,081,537 |
| ES reconstruido/consumido (D/E) | hereda C; la reconstrucción es fiel a lo que hay en RAM | 2,081,537 |
| bytes a FFmpeg (F) | = E (mismo puntero) — 12,701,696 bytes capturados, **150 omisiones**, 2,121,597 bytes omitidos en total | 2,081,537 |

Diff incremental con resync: **los 150 eventos de divergencia son TODOS
OMISIONES**, con tamaños siempre iguales a sumas de payloads PES reales
(4063, 4077, 8140=4063+4077, 12217, 16294, 20357, 28511, 44805, …) — ni
una sustitución, ni duplicación, ni reorden, ni bytes stale, ni error de
concatenación de wrap.

### Tabla 3 — primera divergencia

- canonical offset: **2,081,537** (0x1FC301)
- recomp offset: 2,081,537 (mismo — omisión pura)
- previous matching bytes: 2,081,537 (100% hasta ahí, cruzando 3 wraps completos)
- divergence length: **4063** (un payload PES completo); resync exacto en canónico+4063
- divergence type: **BYTE_OMISSION (payload-aligned)**

```
canonical[-64:+64]:
e2277823daff211744ebbbc9becff614fe378d76613ee5371f6a5323670030040fb3
002f0031ebe392250061f7e3dc57345b840b17c7600f05c71bc70478fbdb
deeda1d5f9d38b854b2cac0103f4401efc8033007ff5e80170200160039007f8248d
45803b800fc019003fff422bbab8ee3e3a9f7e3db745cc7f36260c861adb
recomp[-64:+64]:
e2277823daff211744ebbbc9becff614fe378d76613ee5371f6a5323670030040fb3
002f0031ebe392250061f7e3dc57345b840b17c7600f05c71bc70478fbdb
dd030f230a4e08f9bdca6a663e4a5543dbc20200150207e9fbfd0013803c0028fefb
7cd007ef00782bdfdc5a613a72409afcd9069f37615e65378aa00b257f99
```

(recomp[+0:] == canonical[+4063:] — resync exacto.)

### El evento causal (HECHO, contabilidad [P42:ev] por lote)

Primera pérdida del productor: **lote 37, picture ~32**: el lote traía
57,036 bytes (14 payloads); `guest28` antes del lote = 2,028,564;
comprometidos = 52,973 (13 payloads enteros); **perdidos = 4063** (el
14º payload, entero — no hubo copia parcial: HECHO por el delta exacto).
Offset lógico de la pérdida = 2,028,564 + 52,973 = **2,081,537** —
idéntico al byte del diff binario. `pendingBytes` al despachar el lote =
463,892 → free inicial = 520,192 − 463,892 = 56,300; tras 13 payloads
quedaban 3,327 < 4063 → `viBufBeginPut` no ofreció espacio suficiente y
`StrM2vCallBack` no escribió nada; el HLE ignora el retorno del callback
y el PSS ya avanzó → payload perdido para siempre. Desde ahí el sistema
queda saturado (pending ≈ 470K) y pierde ~8-12KB por lote de forma
recurrente (147 lotes con pérdida en la ventana).

### Confirmación (RUN_070)

Byte-idéntico: primera divergencia 2,081,537, omisión 4063, demux
byte-perfecto (14,974,976 comparados), primer warning picture 43, gates
✓. **Determinista 2/2** (y consistente con el onset picture-43 idéntico
en las 6 corridas históricas RUN_044/045/046/061/063/069).

## 4. Wraps del ring (Tabla 2) — EXONERADOS

Con capacity=524,288 (trazas [P3142:ring] de RUN_069, byte-idénticas a
las de RUN_063 — determinismo del runtime):

| Wrap (lectura) | producedTotal≈ | consumedTotal | offset lógico | físico | byte-perfect? |
|---:|---:|---:|---:|---:|---|
| 1 | 851,333 | 524,288 (ciclo 11) | 524,288 | reset a 0 | **SÍ** |
| 2 | 1,356,433+ | 1,048,576 (ciclo ~21) | 1,048,576 | reset a 0 | **SÍ** |
| 3 | ~1,9M | 1,572,864 (ciclo ~32) | 1,572,864 | reset a 0 | **SÍ** |
| (4º ciclo) | 2,081,537 en escritura | ~1,56-1,6M | — | — | primera pérdida AQUÍ, pero por SATURACIÓN, no por el wrap |

El feed coincide con el canónico hasta 2,081,537 > 3×524,288 → los tres
primeros wraps de lectura y escritura son byte-perfectos. La correlación
histórica «~4º ciclo» era incidental: la variable causal es el backlog
(pending) alcanzando 520,192−len, no el número de wrap. La dinámica
estructural: producción media ≈59.4KB/ciclo vs consumo ≈50.4KB/ciclo
(+9KB/ciclo de backlog, serie de 20 puntos de RUN_063/069) → la
saturación es matemáticamente inevitable sin backpressure.

## 5. Mecanismo (síntesis)

1. HECHO: el demux HLE produce el ES perfecto (B ✓ 15.4MB).
2. HECHO estático (P3.11.1/P3.14.1, reconfirmado): `StrM2vCallBack`
   escribe como máximo lo que `viBufBeginPut` ofrece y `viBufEndPut`
   contabiliza solo lo copiado; su valor de retorno (0/1) se pierde en
   `dispatchGuestStreamCallback` (RpcCallback sin canal de retorno).
3. HECHO: el demux HLE consume el PSS incondicionalmente
   (`recordCdStreamBytesDemuxed` siempre) — no existe el flow-control
   del demux original (que reintentaría el PES hasta que hubiera
   espacio).
4. HECHO dinámico: pérdida payload-entera (nunca parcial) cuando
   free < len; primera en 2,081,537; recurrente después.
5. El consumidor P3.14.2 lee fielmente lo que hay → FFmpeg recibe el
   stream con huecos → `ac-tex damaged`/`MVs not available`/concealment.
6. HECHO: primer warning FFmpeg en picture 43, al CONSUMIR el hueco —
   **11 pictures (~520KB de backlog) después del evento de pérdida**
   (picture ~32, lado escritura). «Primer warning ≠ primer byte malo»
   cuantificado.

## 6. Respuestas requeridas (1-24)

1. SÍ. 2. **SÍ — byte-perfecto** (15.4MB). 3. **NO — primera
divergencia** (payloads descartados al saturarse). 4. Sí para lo
escrito (sin bytes stale/desorden); el problema es lo NO escrito.
5. `producedTotal`(+0x28) es correcto como contador de lo COMPROMETIDO
(su gap vs demux ES la pérdida). 6. SÍ. 7. SÍ (espejo ≤2047 de
redondeo, documentado). 8. SÍ (writeOffset = T%cap se preserva; probado
en §4 por byte-perfección de 3 wraps). 9. **NO** (cero duplicaciones).
10. **SÍ — por el PRODUCTOR** (150 omisiones). 11. **NO** (cero stale).
12. **SÍ** (wraps byte-perfectos). 13. El byte canónico 2,081,537 (el
primer byte del payload omitido; el decoder ve en su lugar el primer
byte del payload siguiente). 14. **Boundary C — escritura al guest
viBuf**. 15. Wrap 3 completado; ocurre durante el 4º ciclo — no causal.
16. La pérdida ocurre ~11 pictures/~520KB (de backlog) antes del primer
warning. 17. **SÍ — 2/2 byte-idéntico** (+6 corridas con onset 43).
18. SÍ (INFERENCIA fuerte: mismo código, misma dinámica estructural;
RUN_063 muestra al segundo decoder desarrollando la misma corrupción;
no byte-verificado para la sesión 2). 19. **SÍ**. 20. NO. 21. **NO** —
exonerado byte a byte. 22. La aritmética y el ownership de P3.14.2 son
CORRECTOS; lo roto es el CONTRATO DE ADMISIÓN del productor
(backpressure ausente en el demux HLE) — fuera del alcance que P3.14.2
validó (ledger abajo). 23. NO — el feed coincide con el canónico desde
el byte 0 (el arranque/predecode no desplaza el stream lógico).
24. **SÍ.**

## Correction ledger

- P3.14.2 «recirculación validada» → **NARROWED**: la validación cubría
  ownership/aritmética/wrap (correctos, reconfirmados byte a byte aquí)
  pero NO la fidelidad a largo plazo bajo desequilibrio de tasas: sin
  backpressure, la saturación del viBuf pierde payloads enteros.
- P4.1F «corrupción introducida por el camino de feed/recirculación»
  → NARROWED con precisión: la introduce el PRODUCTOR (descarte por
  saturación), no la reconstrucción/consumo.
- «~4º wrap» (históricamente sugerido) → RETRACTADO como causa: los
  wraps son byte-perfectos; la variable causal es el backlog.

## 7. Fix recomendado (NO implementado)

**P4.2.1 — MPEG_DEMUX_VIBUF_BACKPRESSURE_FIX**: en `processPssBuffer`
(rama de vídeo), antes de emitir el evento M2V, comprobar el espacio
libre real del viBuf guest (520,192 − pending(+0x2C8), con queued(+0x2C4))
y, si el siguiente payload no cabe, **detener el parseo dejando los
bytes en `pssBuffer`** (retry natural en el siguiente ciclo de demux) —
reproduciendo el flow-control del demux original; el backpressure se
propaga solo (pssBuffer → ring PSS → CD), igual que ya hace
`mpegDemuxBackpressured` para la cola de frames. Validación: repetir la
captura P4.2 y exigir `p42_ffmpeg_input.bin` == prefijo canónico
completo (0 omisiones) + 0 `ac-tex` en toda la película + segunda
película igual.

## 8. Limpieza y estado final

Instrumentación revertida (`git checkout -- MPEG.cpp`); vendor
`19911ce` **limpio** (status vacío); rebuild quirúrgico limpio (solo
MPEG.cpp) + relink (0 cpp) para dejar el binario coherente con la
fuente. Artefactos preservados en `analysis/local/p42/`
(`run069_demux_es.bin`, `run069_ffmpeg_input.bin`, y los de RUN_070 como
`p42_*.bin`) — no trackeados. Main: `5c0881b` + este informe sin
trackear. NO commit, NO push.
