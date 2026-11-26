# AUDIO_002-5 — Audio PCM de las películas PSS por ADMA (parche 40, experimental)

- **Fecha:** 2026-11-26 (diagnóstico 2026-11-24; puerta final y revisión r2 2026-11-26).
- **ELF:** `SLES_503.58` PAL, SHA-256 `d0753a6b…bab4`, CRC32 `77654AD2`, entry `0x00100008`.
- **Base:** `master` `b4a628a3` (PR #18), vendor `a46af4f5` (39 parches), exe de referencia `609f79f8`.
- **Resultado:** `patches/AUDIO_002_5_pss_adma.patch` (revisión r2), SHA-256
  `a98ac5189ad45052bcece39033a1a3c3d21c33273dea90ab888b1a698a5a50a4`. `patched_commit` con 40 parches:
  `d21ad5075aea44d1a15704a88388ab691993c32f`. Exe validado `d497a9a0…`.
- **Alcance:** experimental y opt-in (`PS2X_IOP_HEAP_ALIAS` + `PS2X_SD_ADMA`). No toca GS, planificador, SE, BGM,
  SPU2 ni TSNDDRVM. No regenera el EE.
- **Evidencia local** (no versionada): `analysis/local/AUDIO_002_5_PSS_DIAGNOSIS.md`, `AUDIO_002_5_PSS_RESULTS.md`,
  `AUDIO_002_5_FINAL_GATE.md` y `analysis/local/audio002_5/` (desensamblado, referencia extraída de la ISO, corridas,
  WAV, salidas de la suite y candidatos c1–c3 y r2).

Clasificación: **HECHO**, **INFERENCIA**, **HIPÓTESIS**, **UNKNOWN**.

## 1. Problema

Las películas PSS (demo `0x234`, intro `0x22e`) se reproducían en silencio. XAG ya sonaba (parches 37–39).

**Cadena** (HECHO, desensamblado por `.symtab` y corrida instrumentada): es la muestra `ezmpeg/audiodec` de Sony.
Demux (`sceMpegDemuxPssRing`) → `StrPcmCallBack@0x47A600` → anillo EE de 48 KiB → `audioDecSendToIOP@0x47A160` →
`sceSifSetDma` hacia `iopBuff = sceSifAllocIopHeap(0x6000)` → libsd `BlockTrans` en bucle (`audioDecResume@0x479F00`).
Formato: bloques de 1024 bytes (512 L + 512 R, s16 LE), 48 kHz, estéreo.

**Primera divergencia (HECHO, corrida `diag_menu2` y emulación literal de `iopGetArea@0x47A320`):**
- el juego calcula el área libre como `(BlockTransStatus & 0xFFFFFF) − iopBuff`;
- en RECOMP, `iopBuff = 0x04000000` no cabe en 24 bits: el área sale siempre negativa y, tras la precarga de 24 KiB,
  no se envía nada más al IOP (547 consultas de estado sin cambios en `sent`/`last`).

**Divergencias posteriores e independientes (HECHO):**
1. el stub de `BlockTransStatus` avanzaba 1024 bytes por consulta, no según el tiempo (128 frente a 960 frames por
   field);
2. el demux del runtime contaba como aceptados los callbacks de audio que devolvían 0; libmpeg detiene el demux;
3. ninguna fuente del mezclador leía el búfer IOP.

**Upstream:** `ran-j/PS2Recomp` `origin/main` sustituyó el heap simulado por un espacio IOP propio (PR #244, 139
archivos). Confirma la dirección del diagnóstico; portarlo queda fuera de esta fase.

## 2. Corrección (resumen; detalle en `patches/AUDIO_002_5_pss_adma.md`)

1. **Alias de 24 bits del heap SIF** (`PS2X_IOP_HEAP_ALIAS`): los bloques que caben en la ventana se entregan como
   `base + desplazamiento`; almacén, claves y checkpoint siguen canónicos. Para DMC: `0x00020000:0x00080000`.
2. **Reloj ADMA por tiempo del guest** (`PS2X_SD_ADMA=1`, efectivo solo con el alias): 960 frames por field, un
   bloque cada 256 frames, bucle y bit de banco.
3. **Fuente `Pss` del mezclador:** lee el búfer IOP al renderizar, desintercala y aplica BVOL con signo.
4. **Retención del PES de audio:** un callback que devuelve 0 frena el demux y se reintenta.

## 3. Evolución del candidato

| Revisión | Contenido | Resultado |
|---|---|---|
| c1 | alias, reloj, fuente | 12 callbacks de audio devuelven 0 y se pierden; tras el primero, el flujo salta 16 338 bytes y **L/R quedan desalineados**: ruido desde el bloque 182 (HECHO) |
| c2 | + retención del PES | 0 paquetes perdidos; las dos películas idénticas a la referencia |
| c3 | c2 opcional con dos variables | sin variables, idéntico a 39 parches. **Defectos encontrados en la puerta final (§6)** |
| **r2** | c3 + ADMA solo con alias, parser estricto, aviso de 24 bits, instantánea de diagnóstico, 20 tests de SIF/libsd | publicado como parche 40 |

**Hipótesis refutada (c1):** «el retorno ignorado de los callbacks de audio es secundario; basta medir los
descartes». Un solo paquete perdido corrompe todo el audio posterior.

## 4. Fidelidad (HECHO salvo indicación)

**Referencia independiente:** script que extrae de la ISO, solo leyendo, el private stream 1 de cada PSS, quita la
subcabecera, parsea `SShd`/`SSbd` y desintercala. Comparación por bloques de 256 frames con BVOL aplicado.

| Película / corrida | Bloques | Resultado |
|---|---|---|
| `0x234` interrumpida (fundido incluido) | 1 676 | idénticos, en orden |
| `0x22e` intro | 2 445 | idénticos |
| `0x234` completa, sin entrada | 12 873 | 12 772 primeros idénticos (68,12 s); los ~0,25 s finales no suenan por `audioDecReset` del juego (INFERENCIA) |
| `0x22e` completa | 3 730 | todos idénticos |

- **Determinismo:** dos corridas completas de `0x234` dan el mismo WAV byte a byte; GS síncrono frente a asíncrono,
  idéntico.
- **PCM ≠ ganancia.** El PCM de la ISO llega intacto. La ganancia (BVOL negativo por el desbordamiento del fundido
  del juego, MVOL, cadena SPU2) solo sigue el modelo implementado: **UNKNOWN** frente a hardware.
- **Cadencia del dispositivo:** con el planificador a 60 Hz y contenido PAL, el juego avanza 59,7 ticks/s; el anillo
  del dispositivo recorta el 16,6 % de los frames. **La salida audible tiene saltos.** El WAV (reloj del guest) no.
- **Ritmo del vídeo:** película 1, todas las imágenes en el mismo tick que con 39 parches; película 2, arranca 2
  ticks más tarde.

## 5. Validación de r2

### 5.1 Suite `ps2x_tests` (HECHO)

**Recuento.** MiniTest cuenta un test por cada `tc.Run`; no hay secciones ni subcasos. Todos los registrados se
ejecutan (pasados + fallados = total).

| Construcción | Tests | Composición |
|---|---|---|
| 39 parches | 520 | baseline |
| parche c3 solo | 531 (calculado; no se ejecutó la suite completa con c3 solo) | 520 + 11 (`AudioPssAdma`) |
| c3 en la puerta final | 549 | 531 + 18 tests de puerta: la versión del archivo de puerta compilable contra c3, sin los 2 que usan `sdAdmaDebugState` (ausente en c3) |
| **r2 (parche 40)** | **551** | 520 + 11 + 20 (`PssGateSif` 9, `PssGateLibSd` 11) |

«20 tests nuevos» del informe de la puerta se refiere a r2 frente a c3; frente a 39 parches, el parche añade 31.

| Configuración | Pasan / 551 | Fallos nuevos frente a 39 parches |
|---|---|---|
| ninguna | 543 | 0 |
| solo ADMA (ignorado) | 543 | 0 |
| solo alias | 522 | 21 de SIF de upstream |
| las dos | 520 | 21 de SIF + 2 de `sceSdRemote` de upstream |
| alias inválido (base 0, sufijo basura, desalineado) + ADMA | 543 | 0 |

- Los 8 fallos comunes son los mismos tests que fallan con 39 parches (GS, codegen, kernel, MPEG).
- **SIF:** 20 tests de upstream usan `0x20000–0x3D100` como memoria temporal del EE; con la ventana desplazada a
  `0x00E00000:0x00E60000` pasan. El restante exige direcciones del heap ≥ 32 MiB: invariante que el alias rompe.
- **`sceSdRemote`:** llaman sin runtime en el tick 0 y esperan +1024 por consulta.
- **Suites de audio** (`Audio*`, `Xag*`, `Pss*`): 62/62 en las cuatro configuraciones.

### 5.2 Alias y libsd (HECHO)

- `PssGateSif` cubre asignación, alineación, solapamiento, liberación y reutilización, resolución alias/canónica,
  DMA en ambos sentidos, lectura desde el host IOP, rechazo fuera de límites sin escrituras parciales, agotamiento de
  los 384 KiB (caída a direcciones canónicas que funcionan), alias desactivado, checkpoint SIF con claves canónicas y
  **ninguna escritura en la RDRAM del EE** (centinela intacto en la ventana).
- `PssGateLibSd` usa un `PS2Runtime` real cuyo tick se fija con el codec SCHED del scheduler y un modelo independiente:
  inicio, posición en 400 fields, bucle y banco, fin sin bucle, pausa y reanudación, segundo `BlockTrans`, núcleos
  independientes, `sceSdRemoteInit`, búfer inválido, BVOL positivo, cero y negativo, fuente `Pss` y carga de
  checkpoint AUDIO.

### 5.3 Ventana de DMC

- **HECHO:** el ELF solo carga en `0x00100000–0x009F8480` y `0x01F00000`. En los 14 checkpoints inspeccionados,
  `0x20000–0x100000` está entera a cero; por debajo solo hay datos en `0x2F0–0x2F9`.
- **INFERENCIA:** el juego no usa la ventana para datos visibles por SIF o el host IOP en las rutas ejecutadas.
- **Límite:** no es una prueba sobre todo el juego.

### 5.4 En el juego (exe `d497a9a0`, checkpoint del menú, intro hasta el final)

| Configuración | WAV | Película | Fuentes al final |
|---|---|---|---|
| ninguna / solo alias / solo ADMA | idéntico al exe de 39 parches (`458e4229…`) | termina | 0 |
| las dos | idéntico al de c3 (`f5c493a7…`), que coincide con la ISO | termina | 0 (2 añadidas, 2 retiradas) |
| las dos, oficina (XAG) | idéntico a c3 (`cf8ff366…`) | — | 1 (solo XAG) |

### 5.5 Rendimiento (c3; r2 solo añade comprobaciones al inicio y avisos únicos)

- Castillo completo con el sistema tranquilo: 283,8 s frente a 285,6 s de la referencia (−0,6 %). Una corrida con
  +8,9 % coincidió con carga externa (+9 puntos de CPU del sistema).
- Oficina: −3,4 %. Menú con películas: 26,2–26,5 s frente a 27,6 s.
- Memoria: pico de 169–242 MB por proceso (antes 173–238).

## 6. Puerta final: defectos de c3 (HECHO) y corrección

1. **Solo `PS2X_SD_ADMA=1` activaba ADMA.** El manifiesto de c3 decía que «hacen falta las dos», pero el código no lo
   impedía. El guest recibía `0x04000000`, no rellenaba nunca su búfer y la retención del PES detenía la intro **en
   silencio**: 10 actualizaciones de estado de la película frente a 722, sin STOP final. El WAV no lo delataba (esa
   intro es silencio también en la referencia). **r2:** ADMA se ignora con aviso y la configuración es idéntica a la
   predeterminada.
2. **El parser aceptaba base 0.** La primera asignación devolvía 0, que el guest lee como fallo. **r2:** base 0 y
   caracteres sobrantes se rechazan con aviso.

**Corrección de una afirmación previa:** el informe de c3 decía que los dos tests de `sceSdRemote` «ya fallaban antes
de este parche» y que no había build de `ps2xTest`. **Refutado:** la suite se construyó sin impedimento técnico y los
dos tests pasan con 39 parches; la lista anterior venía del vendor `ea54616d` y mezclaba líneas de log. Fallan solo con
ADMA efectivo, por el cambio de contrato.

## 7. Checkpoints

- Formato sin cambios: el alias no altera el almacén ni las claves; el codec AUDIO es el mismo y el reloj se rearranca
  en la primera llamada tras la carga. El guardado con una transferencia activa se sigue rechazando.
- Cargas verificadas con las dos variables (exe c3; r2 no cambia esta ruta): menú, oficina v2 (XAG relanzado, también
  repetida con r2) y castillo v4 (hash verificado).
- Un checkpoint guardado con el alias puede contener punteros a la ventana: guardar y cargar con la misma
  configuración.

## 8. Riesgos y trabajo pendiente

1. Salida audible con saltos hasta corregir la cadencia del planificador para PAL (fuera de esta fase).
2. Alias como solución local, solapado numéricamente con la RDRAM del EE. Alternativa: espacio IOP propio.
3. Ganancia SPU2 sin contraste con hardware (BVOL negativo, MVOL, nivel PSS/XAG).
4. Cinemáticas PSS dentro del juego sin probar.
5. Cola de ~0,25 s perdida al terminar; segunda película 2 ticks más tarde (sin PCSX2).
6. Puente del mezclador ligado al último `PS2IopHostAdapter` construido.
