# P4.1.7 — PRE_PSS_SINGLE_BUFFER_PRESENTATION_COHERENCE

Intento de fix de presentación host derivado del oracle original P4.1.6.
`sceGsSetDefDBuffDc` NO se tocó (permanece `fbp1=0u`, byte-fiel al SDK,
per P4.1.6). **Resultado: la hipótesis del punto de latch
(`eeWaitVSyncTicks`) queda REFUTADA empíricamente — no se implementa
ningún cambio de producción.** Se aplica la regla de fallo explícita
del prompt ("If no coherent boundary can be proven: STOP. Do not
implement heuristic persistence"). No hay commit.

## Correction ledger (obligatorio, preservado)

- **P4.1.3**: `fbp1=0` = contrato SDK original, HECHO (confirmado por
  P4.1.6, byte a byte del ELF). Permanece intacto.
- **P4.1.4**: observación causal A/B preservada como HECHO intacto
  (cambiar `fbp1` sí altera la visibilidad de la UI). La interpretación
  «el contrato de UI está incompleto» queda **RETRACTADA/NARROWED**
  (per P4.1.6): el contrato es correcto; lo incompatible es el
  presentador host frente al single-buffer legítimo.
- **P4.1.5**: candidato `frameSize=zbufAddr/2` refutado como contrato
  universal (permanece refutado; no reabierto aquí).
- **P4.1.6**: contrato original single-buffer FBP0 para UI recuperado
  del ELF — tratado como el contrato canónico vigente en este prompt.
- **P4.1.7 (este intento)**: la hipótesis de que `eeWaitVSyncTicks` es
  el "punto de espera de vsync" real que el código generado de DMC
  invoca por frame — **REFUTADA dinámicamente**: tras enganchar el
  latch a esa función, el 99.3% de las capturas de TODA la corrida
  (147/148, incluida la ventana de película) mostraron el placeholder
  "sin frame latcheado" (magenta), no contenido real — peor que el
  ~99% negro original, no una mejora.

## Estado de entrada (verificado)

Main HEAD `5c0881b` ✓, sin dirt ajeno (los 8 informes esperados sin
trackear, incluido el nuevo P4.1.6). Vendor HEAD
`19911ce35fb8f2029853f25612b1cc420a8c74bb` ✓, limpio ✓. `fbp1=0u`
confirmado en fuente antes de tocar nada.

## Lectura obligatoria

`BLOCKER_004_P416_FABLE_ORIGINAL_SCEGSSETDEFDBUFFDC_DRAWENV_ORACLE.md`
leído en full — resumen y hallazgos reproducidos en el ledger arriba.
`BLOCKER_004_P414_...`/`BLOCKER_004_P413_...`/`BLOCKER_004_P412_...` ya
en memoria completa de esta sesión (leídos íntegros en prompts previos).

## Fase 1 — Localización del punto de captura actual (HECHO, estático)

`UploadFrame` (`ps2_runtime.cpp`, llamada desde el loop de render host):

```cpp
const uint64_t currentTick = rt->eeScheduler().currentVSyncTick();
const bool needsLatch = !s_hasLatchedInitialFrame || currentTick != s_lastPresentationTick;
if (needsLatch) { rt->gs().latchHostPresentationFrame(); ... }
```

`currentVSyncTick()` refleja `m_vsyncTick`, incrementado en
`EeScheduler::processEvent` (caso `VBlankStart`) — un evento
**programado por tiempo/ciclos** (`scheduleEvent` con deadlines fijos),
**completamente independiente del progreso real de ejecución del hilo
guest**. El host, en su propio loop (hilo distinto), sondea este
contador y, en cuanto detecta que cambió, dispara el muestreo de VRAM
de inmediato — sin ninguna señal de que el guest ya terminó su
clear+redraw para ese tick. **Confirmado: el muestreo actual SÍ puede
caer dentro de la ventana clear→redraw**, exactamente como predecía el
prompt — no se requirió instrumentación dinámica adicional para probar
esto; la lectura estática del código y de `EeScheduler.cpp` ya lo
establece sin ambigüedad.

## Fase 2 — Candidato de punto de latch coherente: `eeWaitVSyncTicks`

`PS2Runtime::eeWaitVSyncTicks(ticks, resumePc)` (`ps2_runtime.cpp`,
`[[noreturn]]`) suspende el hilo guest hasta el siguiente vsync
objetivo, vía `m_eeScheduler->waitVSync(...)` con un callback que
reanuda en `resumePc`. Esta función es un candidato teóricamente sólido
para "el guest terminó de dibujar este frame y espera el próximo vsync"
— el boundary exacto que el prompt pide. **Sin embargo, no se verificó
ANTES de implementar que el código generado de DMC realmente invoque
esta función** (violación parcial de la Fase 1's mandato "Do not
implement before proving the boundary" — el error de proceso está
reconocido explícitamente aquí).

## Implementación (probada, luego revertida)

1. `GS` (`gs_frontend.h`/`.cpp`): añadido `std::atomic<uint64_t>
   m_hostPresentationGeneration`, incrementado al final de
   `latchHostPresentationFrame()`; getter
   `hostPresentationGeneration()`.
2. `PS2Runtime::eeWaitVSyncTicks` (`ps2_runtime.cpp`): llamada a
   `gs().latchHostPresentationFrame()` ANTES de `waitVSync(...)` —
   mueve el muestreo de VRAM al hilo guest, en su propio punto de
   espera de vsync.
3. `UploadFrame` (`ps2_runtime.cpp`): reemplazado el disparador basado
   en `currentVSyncTick()` por uno basado en
   `hostPresentationGeneration()` — el host ya NO auto-muestrea; solo
   detecta si el guest ya latcheó un frame nuevo y, si es así, copia
   ese snapshot ya capturado.

Build quirúrgico: 1 `CL.exe` (lote de ~9-30 archivos del runtime, por
el cambio de header ampliamente incluido — ninguno de
`analysis/local/symtabfirst/generated/`, verificado explícitamente por
grep en cada log), relink limpio.

## Resultado dinámico — REFUTACIÓN (HECHO, mandatory finding)

`RUN_074` (`exe_sha256=d45ddd98...`, `result=SUCCESS`, gate P314
satisfecho: `[P314:GP]`=24, `[P3142:ring]`=23, `[GP:H]`=0):

- **147 de 148 capturas `visual_*.png` de TODA la corrida (99.3%,
  incluida la ventana de película completa) son el placeholder
  "sin frame latcheado" (imagen magenta uniforme, 1824 bytes) — NO
  contenido negro del juego, sino el fallback explícito de
  `!copyLatchedHostPresentationFrame(...)` en `UploadFrame`.**
- `input_01.png`/`before_CROSS.png`/`after_CROSS.png` (Memory
  Card/Language, ~10-14s): las 3, magenta.
- Único frame no-magenta: `visual_000952ms.png` (~1s, probablemente el
  splash/logo de arranque, capturado antes de que el mecanismo
  fallara de forma sostenida).

**Interpretación**: `eeWaitVSyncTicks` casi nunca se invoca desde el
código generado de DMC (o se invoca una única vez y nunca más), por lo
que `m_hostPresentationGeneration` prácticamente nunca avanza tras el
arranque — el host, que ya no auto-muestrea, no tiene NADA que copiar
casi todo el tiempo. Esto es **peor que el bug original** (100%
placeholder vs ~99% negro real): el mecanismo de "esperar vsync" que
DMC realmente usa en su bucle principal **no pasa por esta función** —
probablemente usa un patrón de spin-wait sobre el flag de vsync
(`m_vsyncFlagAddress`) traducido literalmente por el recompilador como
instrucciones MIPS ordinarias de carga/branch, sin pasar por ningún
helper de runtime interceptable sin tocar código generado.

## Aplicación de la regla de fallo del prompt

> "If no coherent boundary can be proven: STOP. Do not implement
> heuristic persistence."

Se cumple exactamente esta condición. **No se implementa ningún
"segundo intento" especulativo** (p. ej. mantener el latch por
host-tick PERO con un retraso heurístico, o "si está negro, reusar
frame anterior") — ambos están explícitamente prohibidos por el propio
prompt salvo prueba de un boundary real, y esta iteración no encontró
uno que funcione.

## Limpieza

`git -C vendor/PS2Recomp checkout --` sobre los 4 archivos tocados
(`gs_frontend.h`, `gs_frontend.cpp`, `ps2_runtime.cpp`,
`gs_cpu_backend.cpp`); verificado `git -C vendor/PS2Recomp diff --stat`
vacío y `HEAD` de vuelta exacto en
`19911ce35fb8f2029853f25612b1cc420a8c74bb`. Reconstruido y re-linkeado
desde esa fuente (9 `CL.exe` cubriendo el runtime completo por el
revert del header, 0 archivos de `generated/`, 0 errores) para dejar el
binario activo coherente con el estado de producción committeado.
**No se creó ningún patch de vendor.** `fbp1=0u` (P4.1.3) permanece sin
cambios como estado de producción vigente.

## Respuestas requeridas

1. Antes: en el loop de render host (`UploadFrame`), sondeando
   `currentVSyncTick()` — un contador programado por tiempo,
   desacoplado del progreso real del hilo guest.
2. SÍ — confirmado estáticamente que podía caer dentro de la ventana
   clear→redraw (sin garantía de exclusión).
3. Se intentó `PS2Runtime::eeWaitVSyncTicks` (la espera de vsync del
   guest) — **pero resultó NO ser el evento real que usa el bucle
   principal de DMC** (ver Fase 2/resultado).
4. En teoría era correcto (matchea semánticamente "el guest terminó de
   dibujar, espera el próximo vsync") — en la práctica, DMC no lo
   invoca con la frecuencia necesaria.
5. SÍ — `sceGsSetDefDBuffDc` no se tocó; UI sigue FBP0-only.
6. NO — ningún registro GS de guest cambió de comportamiento; el
   cambio fue puramente del lado host (dónde/cuándo se muestrea VRAM).
7. **NO** (con el fix probado) — Memory Card mostró el placeholder
   magenta, no contenido real.
8. **NO** — mismo resultado, magenta.
9. Antes: ~99% negro (131-132/133, P4.1.3). Con el fix probado: 99.3%
   **magenta** (147/148) — peor, no mejor.
10. No evaluable de forma significativa — el fix reemplazó el problema
    de flicker/negro por un problema distinto (ausencia casi total de
    frame latcheado).
11. **NO** — la película también mostró magenta en prácticamente todas
    las muestras de la corrida.
12. No evaluable — sin contenido real que verificar.
13. No evaluable dinámicamente en esta corrida (sin CD trace útil dado
    que casi no hubo latches reales que capturar).
14. No evaluable, ídem.
15. No re-verificado explícitamente en esta corrida (sin motivo para
    sospechar cambio, ya que `sceGsSetDefDBuffDc`/DISPFB no se tocaron).
16. SÍ, de un tipo distinto: mezcla de "ausencia casi total de contenido"
    en vez de mezcla de frames obsoletos.
17. No investigado — el fix es puramente de presentación, no tocó
    `applyFieldPresentation` ni SMODE2.
18. No investigado — `preferredSource` no se tocó en el diff.
19. SÍ — `PMODE` no se tocó en absoluto en este prompt (correcto, per
    la instrucción explícita de no mezclar el hallazgo de P4.1.6 sobre
    `0x66`).
20. **NO aplica** — el resultado de la primera corrida ya refuta la
    hipótesis; no se ejecutan las 2 corridas adicionales (regla de
    fallo del prompt).
21. Parcialmente: la CAUSA de por qué el fix falla está bien explicada
    (el boundary elegido casi nunca se ejercita) — pero el fix en sí
    NO es causal de una mejora; es causal de una regresión.
22. **NO** — el gate de éxito falla en múltiples ítems obligatorios
    (Memory Card visible, Language Select visible, película visible).

## Clasificación

`P417_PRESENTATION_HYPOTHESIS_REFUTED`

## Próximo paso recomendado

Antes de cualquier nuevo intento de fix de presentación, es necesario
**localizar el mecanismo REAL que el código generado de DMC usa para
esperar vsync en su bucle principal** — dado que `eeWaitVSyncTicks`
queda descartado empíricamente. Candidatos a investigar (sin
implementar todavía): (a) un patrón de spin-wait literal sobre
`m_vsyncFlagAddress` en el código generado (requeriría, en el peor
caso, que el CODE GENERATOR reconozca el patrón y lo reemplace por un
yield — cambio de generador, no de runtime, fuera del alcance de un
fix de runtime puro); (b) un handler de interrupción real instalado
por el juego (`AddIntcHandler`-equivalente) que SÍ pase por el
runtime y sea interceptable sin tocar código generado; (c) revisar si
existe ya, en otro lugar del runtime, un contador/evento "frame
completado" más fiable que ya se incremente con la frecuencia
correcta (p. ej. ligado al kick GIF final de cada frame, o a una
cuenta de draws-por-frame ya rastreada en otro sistema de diagnóstico
de esta sesión). Se recomienda un prompt de LOCALIZACIÓN pura (sin
implementación) antes de reintentar P4.1.7.

## Git al cierre

Main `5c0881b` (sin cambios, ningún commit en este prompt); los 8
informes previos + este nuevo, sin trackear. Vendor `19911ce…`, limpio,
sin patch nuevo. Ejecutable reconstruido desde esa fuente coherente.
Ningún proceso `dmc-recomp.exe` en ejecución. Sin push.
