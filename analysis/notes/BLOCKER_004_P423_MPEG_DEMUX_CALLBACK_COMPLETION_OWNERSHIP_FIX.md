# BLOCKER_004 P4.2.3 — MPEG demux callback completion ownership fix

Fecha: 2026-09-13  
Estado: **NO_COMMIT**  
Clasificación primaria: **P423_CANONICAL_ES_RESTORED_DECODER_RESIDUAL_REMAINS**

## Identidad y checkpoint

- **HECHO:** main de entrada y salida: `65dd4aad9bb9d79e06d9ff9e90bb32c78714ecd1`.
- **HECHO:** vendor de entrada: `0efd17c3cdb834ac13c087dcb2bd947d9fc033b3`, limpio.
- **HECHO:** ELF observado: `original/SLES_503.58`, SHA-256 `d0753a6b3b2f00802a50758a872d8cf051725aa31c839aa8894eee30ce58bab4`, entry `0x00100008`, CRC32 `77654AD2`.
- **HECHO:** sólo se modificó `vendor/PS2Recomp/ps2xRuntime/src/lib/Kernel/Stubs/MPEG.cpp`; no se tocó C++ generado, GS, framebuffer, PMODE, FIELD ni bob.
- **HECHO:** vendor queda dirty porque la puerta completa no pasó. No se creó patch, no se actualizó `upstream.lock.json`, no se hizo commit ni push.
- **HECHO:** `.claude/` y `analysis/notes/ARCH_AUDIT_002_SOL_CROSS_IMPLEMENTATION_MPEG_RENDER_ARCHITECTURE.md` ya eran untracked al entrar y no se modificaron en esta fase.

## Regla original de callback y cursor

Pseudocódigo reconstruido de `sceMpegDemuxPssRing` (`0x00109180–0x00109470`):

```text
confirmed = 0
while (parser puede producir el siguiente callback):
    v0 = callback(mpeg, callbackData, callbackUserData)
    if (v0 == 0):
        break
    confirmed = parser.bitCursor >> 3
return confirmed
```

- **HECHO:** las llamadas relevantes están en `0x001092CC` y `0x00109394`; `v0` se conserva en `s4` en `0x001092D4`/`0x0010939C`.
- **HECHO:** `0x001093A0` salta la actualización de cursor si `s4 == 0`. Si es distinto de cero, `0x001093A8–0x001093B4` convierte el cursor de bits de `[s1+0x18]` a bytes (`>> 3`) y lo guarda como último prefijo confirmado. `0x0010943C` devuelve ese prefijo.
- **HECHO:** `StrM2vCallBack@0x0047AFE0` llama `viBufBeginPut`, `Copy2area` y `viBufEndPut`; devuelve cero si la cantidad copiada es `<= 0` y uno si es `> 0`.
- **HECHO:** `v0=1` es aceptación no nula del callback, no una cantidad de bytes y no demuestra por sí solo que todo el payload cupiese.
- **HECHO:** `v0=0` detiene el wrapper y conserva como retorno el último prefijo previamente confirmado.
- **INFERENCIA:** el cursor observado por el caller sólo puede cruzar el PES actual después de que el callback guest haya retornado no cero.

## Implementación

- **HECHO:** cada `MpegPlaybackState` contiene una transacción `MpegDemuxTransaction` con identidad única, packet id, generación CD, tamaño solicitado, prefijo previo, límites de paquete/payload, offset ES, callbacks aceptados y metadatos de viBuf.
- **HECHO:** `processPssBuffer` se detiene al encontrar un PES de vídeo con callback y no borra ese paquete.
- **HECHO:** `sceMpegDemuxPss` y `sceMpegDemuxPssRing` crean la transacción y suspenden su retorno lógico.
- **HECHO:** la continuación usa `EeScheduler::invokeCurrent`. El executor no espera en mutex, condición, sleep, busy loop ni future; la transferencia al guest ocurre mediante `EeDispatcherTransfer` ya soportado por el scheduler.
- **HECHO:** `GuestInvocation::onComplete` lee el `v0` real del contexto completado y reanuda `continueP423DemuxTransaction` con el contexto padre.
- **HECHO:** el mutex MPEG se suelta antes de ejecutar guest code.
- **HECHO:** un retorno cero deja el PES en `pssBuffer`, calcula el cursor confirmado según la regla original y conserva estado de retry. Un retorno no cero permite continuar hasta confirmar el callback de vídeo.
- **HECHO:** el payload se entrega al decoder host y al oracle de ES aceptado una sola vez, en el commit aceptado; después se llama `erasePssPrefix(packetEnd)`.
- **HECHO:** los callbacks de audio ya encontrados antes del vídeo se conservan en la misma secuencia de continuación. Su `v0` no cambia la política previa del HLE para audio. No se confirman bytes posteriores al vídeo suspendido.
- **HECHO:** el retry compara dirección guest y contenido retenido antes de reconocer solapamiento. No conserva punteros a almacenamiento de `vector` a través del callback.
- **HECHO:** una completion sólo puede mutar una transacción activa con el mismo id globalmente único. Destruir/recrear estado invalida ese id; una completion tardía retorna cero al padre y no modifica el estado nuevo.
- **HECHO:** `DMC_P423_TRACE`, `DMC_P423_CAPTURE_ES` y `DMC_P423_CAPTURE_ACCEPTED_ES` son opt-in y están desactivados por defecto.

## Correcciones durante integración

- **RETRACTADO:** hacer que el `v0` del callback de audio gobernase el cursor general. RUN_087 se detuvo alrededor de 513 KiB; el ELF observado sólo justificaba la decisión requerida para el callback de vídeo de esta ruta.
- **RETRACTADO:** suspender después de cada audio y retener sus direcciones guest hasta otra llamada. RUN_088 produjo ES aceptado canónico pero el consumidor divergió en 158.845 porque el ring PSS reutilizó esas direcciones.
- **HECHO:** la implementación final conserva el orden de los audios previos dentro de la continuación que termina en el siguiente vídeo; RUN_089–091 eliminan aquella divergencia.
- **RETRACTADO:** capacidad en bytes al enqueue como causa raíz, conforme a P4.2.1.
- **HECHO:** la causa P4.2.2, accounting guest obsoleto por callback diferido, queda confirmada: al restaurar completion y ownership desaparece exactamente la omisión histórica y el ES aceptado pasa a ser canónico.
- **HECHO:** P3.14.2 no fue modificado.

## Build

- **HECHO:** el build válido se ejecutó con `build_one.ps1 -Mpeg`; compiló únicamente `MPEG.cpp` y relinkó.
- **HECHO:** no se regeneró, no hubo clean build y no se compilaron unidades de `recomp/generated`.
- **HECHO:** SHA-256 del ejecutable probado: `4e594ed45006b634a42a60d73b5fb0ff25671a11f604d3fa1d3f5551bb15e296`.
- **HECHO:** `git diff --check` no informa errores.

## Validación byte-exacta

Entorno de RUN_089–092:

```text
PS2X_CD_IMAGE=<ruta local al ISO aportado por el usuario>
PS2X_RUNTIME_ARENA_BASE=0x009FA000
PS2X_RUNTIME_ARENA_LIMIT=0x00ABE000
DMC_P313_INIT_MODE=ownership
DMC_P314_SYNC_REDECODE=1
```

| Oracle | RUN_089 | RUN_090 | RUN_091 |
|---|---:|---:|---:|
| ES aceptado bytes | 23.654.877 | 23.654.877 | 23.654.877 |
| payloads aceptados | 5.808 | 5.808 | 5.808 |
| SHA-256 | `b2e29f...cfb71` | `b2e29f...cfb71` | `b2e29f...cfb71` |
| retained/retry | 455 | 455 | 455 |
| callbacks de vídeo ejecutados | 6.263 | 6.263 | 6.263 |
| max transacción vídeo sin confirmar | 1 | 1 | 1 |
| max profundidad invocation MPEG observada | 1 | 1 | 1 |
| stale completion | 0 | 0 | 0 |
| `ac-tex damaged` | 1 | 1 | 1 |
| `Warning MVs not available` | 1 | 1 | 1 |

- **HECHO:** los tres ES aceptados tienen exactamente 23.654.877 bytes y SHA-256 completo `b2e29f64807a8336e2181788bbd2a343af4d1fbdc9cd94ee9de0dbd5a4ecfb71`.
- **HECHO:** los tres contienen exactamente 5.808 eventos `[P423:ES]`, todos `acceptedOnce=1 duplicate=0 mismatch=0`.
- **HECHO:** `FIRST_BAD_OFFSET_AFTER_FIX=NONE` en el ES aceptado completo.
- **HECHO:** el payload histórico de 4.063 bytes en `0x1FC301 / 2081537` está presente en su posición canónica. El hash completo y la igualdad de las tres ejecuciones prueban que no se omitió ni duplicó en la secuencia.
- **HECHO:** los 455 rechazos `v0=0` por ejecución dejaron delta cero en `+0x28`; los 5.808 retornos no cero incrementaron `+0x28` exactamente por el payload esperado. Los 6.263 callbacks pasaron esa comprobación; cero fallaron.
- **HECHO:** la profundidad sin confirmar por handle fue como máximo uno como consecuencia del call/return/commit, sin constante de cola.
- **HECHO:** la profundidad de invocations MPEG observada fue uno. La profundidad global de invocations no relacionadas no se instrumentó: **UNKNOWN**.
- **HECHO:** no hubo `feedFailed`, `stale data detected`, `async callback stack exhausted`, `[GP:H]` ni deadlock en las tres ejecuciones.
- **HECHO:** se observaron `FIRST_WRAP` y `SECOND_WRAP` en cada ejecución.
- **HECHO:** el consumidor P3.14.2 produjo en las tres ejecuciones un prefijo de 23.183.360 bytes idéntico al canónico, SHA-256 `721c842b343ccb033e64558bf88b117bc4d4c1528c1f88b9bf612a65875e37f0`; cruza el offset histórico y múltiples wraps sin divergencia.

## Residual localizado tras restaurar transporte

- **HECHO:** quedan 471.517 bytes canónicos ya aceptados en viBuf cuando el productor marca `streamEnded`.
- **HECHO:** la condición actual de P3.14.2 sólo alimenta FFmpeg mientras `!playback.streamEnded`; por eso no consume ese backlog y el consumidor termina en 23.183.360 bytes.
- **HECHO:** inmediatamente después del último `[P423:ES]` aparecen una vez `ac-tex damaged` y una vez `Warning MVs not available`, seguidos del flush.
- **INFERENCIA:** el único warning restante es un flush prematuro del decoder con backlog válido, separado del ownership de PES ya corregido.
- **HECHO:** P4.2.1 RUN_084 registró 213 `ac-tex damaged` y 183 `Warning MVs not available` en su ventana de 200 s; P4.2.3 registra 1 y 1 al final de la película. Las ventanas no son idénticas, por lo que esta comparación sólo indica reducción, no una tasa equivalente.
- **HECHO:** la regla de fallo del prompt exige detenerse ante este nuevo límite y no apilar un segundo fix de transporte.

## Smoke y evidencia visual

- **HECHO:** RUN_092 fue el smoke con `DMC_P423_TRACE` y ambos captures ausentes. Contiene cero líneas `[P423:*]`, alcanzó `MC_CHECK`, `MOVIE_START`, `MPEG_INIT`, cruzó dos wraps y no se bloqueó.
- **HECHO:** RUN_092 conserva el mismo residual de un `ac-tex damaged` y un `Warning MVs not available`.
- **HECHO:** todas las capturas PNG de RUN_091 tienen el mismo SHA-256, incluida `boot.png` y las capturas tardías. Ese oracle visual quedó congelado y no representa la evolución real del framebuffer.
- **UNKNOWN:** visibilidad, estabilidad y nitidez de Memory Card y Language Select en estas ejecuciones.
- **UNKNOWN:** eliminación visual completa de la corrupción MPEG tardía.
- **INFERENCIA:** no hay regresión de contrato GS introducida por el patch porque ningún código GS ni dirección de framebuffer cambió, pero la comprobación visual exigida no quedó demostrada.
- **UNKNOWN:** segunda película/fresh MPEG generation; no se alcanzó en esta fase.

## Respuestas requeridas

1. **HECHO:** regla exacta: callback no cero confirma `parser.bitCursor >> 3`; callback cero detiene y devuelve el prefijo confirmado anterior.
2. **HECHO:** en `StrM2vCallBack`, `v0=0` significa ninguna cantidad copiada; `v0=1` significa cantidad copiada positiva, no byte count ni prueba independiente de payload completo.
3. **HECHO:** el HLE suspende tras parsear el primer PES de vídeo completo y crear su transacción, antes de devolver el cursor del syscall.
4. **HECHO:** la continuación es `GuestInvocation` mediante `invokeCurrent`, con `onComplete` que pasa el `v0` guest a `continueP423DemuxTransaction`.
5. **HECHO:** el PES permanece en `MpegPlaybackState::pssBuffer`; la transacción sólo guarda offsets y valores owned.
6. **HECHO:** `erasePssPrefix(packetEnd)` ocurre tras aceptar todos los callbacks hasta el vídeo con `v0!=0`.
7. **HECHO:** no; máximo natural observado por handle: uno.
8. **HECHO:** sí; `getRegU32(completed, 2)` gobierna RETAIN o COMMIT.
9. **HECHO:** sí; el payload 4.063 está restaurado en `0x1FC301`.
10. **HECHO:** no hay duplicación en el ES aceptado completo.
11. **HECHO:** ninguno en los 23.654.877 bytes aceptados.
12. **HECHO:** 23.654.877 bytes aceptados; 23.183.360 consumidos antes del flush residual.
13. **HECHO:** 5.808 payloads aceptados.
14. **HECHO:** `b2e29f64807a8336e2181788bbd2a343af4d1fbdc9cd94ee9de0dbd5a4ecfb71`.
15. **HECHO:** 213 en RUN_084 previo; 1 por ejecución final P4.2.3.
16. **HECHO:** 183 en RUN_084 previo; 1 por ejecución final P4.2.3.
17. **HECHO:** uno.
18. **HECHO:** profundidad MPEG observada uno; total global no relacionado **UNKNOWN**.
19. **HECHO:** sí para los 6.263 callbacks auditados por ejecución: cero deltas imposibles y cero commits duplicados.
20. **HECHO:** sí hasta 23.183.360 bytes, con múltiples wraps y prefijo exacto.
21. **HECHO:** no hubo deadlock en RUN_089–092.
22. **HECHO:** no se observó stale completion; la prueba dinámica de cambio de generación durante callback es **UNKNOWN**.
23. **UNKNOWN:** el oracle visual fue inválido; permanece además el flush truncado.
24. **UNKNOWN:** no demostrado visualmente; no se tocó GS.
25. **UNKNOWN:** no demostrado visualmente; no se tocó GS.
26. **UNKNOWN:** no demostrado visualmente; no se tocó GS.
27. **UNKNOWN:** segunda película no ejecutada.
28. **HECHO:** 3/3 ES aceptados completos y canónicos; 0/3 cumplen cero warnings y consumidor completo.
29. **HECHO:** el clean smoke progresó sin P423 trace, pero conserva el warning residual; no satisface la puerta completa.
30. **HECHO:** no se justifica commit porque no pasan todos los gates obligatorios.

## Decisión

- **HECHO:** el cambio es causal para el root cause P4.2.2: restaura completion, ownership, el payload histórico y el ES aceptado canónico 3/3.
- **HECHO:** el gate integral falla por 471.517 bytes no consumidos, 1/1 warnings, falta de oracle visual válido, falta de ciclo destroy/generation dinámico y segunda película no validada.
- **HECHO:** `CHECKPOINT_DECISION=NO_COMMIT`.
- **INFERENCIA:** el siguiente trabajo debe estudiar únicamente el orden `streamEnded`/drenaje de viBuf/flush para consumir el backlog canónico antes de vaciar FFmpeg, manteniendo congelado este ownership.
