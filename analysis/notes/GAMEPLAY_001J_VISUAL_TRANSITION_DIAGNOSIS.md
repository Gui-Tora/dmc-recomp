# GAMEPLAY_001J — Diagnóstico visual de la transición posterior a la fachada

Fecha: 2026-10-14. Evidencia local: `analysis/local/gameplay_001j/` (no versionada).
Informe local: `analysis/local/gameplay_001j/REPORT_GAMEPLAY_001J.md`.
Categorías: **HECHO**, **INFERENCIA**, **HIPÓTESIS**, **UNKNOWN**, **RETRACTADO**.

> **Estado:** fase de diagnóstico. **Ningún cambio permanente.** La instrumentación es temporal y está
> activada solo por variable de entorno. Sin commit ni push. **Primer frame jugable: NO demostrado.**

## 1. Objetivo

Explicar los tres problemas visuales que el usuario observó en la grabación
`Grabación 2026-10-14 122319.mp4` (346 s, 642×480, 30 fps):

- **VISUAL_001:** imagen duplicada o desplazada.
- **VISUAL_002:** ghosting del personaje durante la introducción.
- **VISUAL_003 (prioritario):** pérdida de imagen después de la fachada de Devil May Cry.

Para cada uno, determinar si la escena se genera mal o si se presenta un buffer incorrecto.

## 2. Estado inicial

Rama `gameplay-001i` publicada (parches 14 CDMODULE/ABI 2 y 15 TTE; 17 `ret0`). Ejecutable reproducible
`7110206b…`. Desde el checkpoint ABI 2 la película `0x232` se reproduce completa.

## 3. Evidencias utilizadas

- **Grabación del usuario:** fotogramas extraídos con ffmpeg a 1 fps entre 3:00 y 3:50, más muestras.
- **Volcado GS temporal (`DMC_GS_DUMP_DIR`, cada 2 s)** en `GSCpuBackend::Present`. Registra:
  - PMODE, SMODE2, DISPFB1/2, DISPLAY1/2 y BGCOLOR;
  - FRAME de los contextos de dibujo;
  - la imagen presentada;
  - cada FRAME de contexto pasado por la misma ruta de presentación;
  - cada 4 volcados, la memoria local GS completa (4 MiB);
  - contadores de paquetes GIF por PATH.
- **Decodificador PSMCT32 independiente** (`vram_decode.py`, tablas de bloque y columna del GS). Validación:
  la textura del logo «Devil May Cry» se decodifica correctamente.
- **Corridas:** `gsG1` (estado publicado más volcado) y `gsV2` (lo mismo más `DMC_VF0_FIX=1`; única
  variable cambiada).

## 4. Línea temporal y primera divergencia

| Grabación | Runtime (tick) | Observación |
|---|---|---|
| hasta 3:24 | ~2856 → ~5926 | película `0x232` (FMV); `DISPFB2` alterna FBP 160/0 (512×448); circuito 1 = franja de 64 líneas, FBP 280, `DY=456` |
| 3:10–3:24 | final de la película | fachada estática (último tramo de la FMV) |
| 3:25 | ~5926 | `MOVIE:status alive=0`, `MPEG:delete`; `PMODE=0xFF25` durante un instante (solo circuito 1) → pantalla negra |
| después | 5927 → 6025 | escena en tiempo real: `SMODE2=0x3`, `DISPFB2` alterna FBP 0/56 (512×224), circuito 1 = FBP 504 con `DY=104`; PATH1/PATH2 suben de ~1,7 k a ~46 k/35 k paquetes |

**HECHO (primera divergencia de VISUAL_003, A/B de una variable):**

- **Sin corrección `vf0`** (`gsG1`): el buffer de dibujo contiene **solo un plano de suelo** texturizado. Lo
  demás es color de borrado.
- **Con `DMC_VF0_FIX`** (`gsV2`): el mismo buffer contiene **la escena 3D**, el interior de la oficina
  (pared, ventana con persianas, diana, plantas), y se presenta.

Por tanto, la «pérdida de imagen» es sobre todo **geometría que no se genera** porque `vf0 = (0,0,0,0)` en
los hilos del juego (causa demostrada en
[GAMEPLAY_001I_TTE_PROMOTION_ADD_SUB_VF0.md](GAMEPLAY_001I_TTE_PROMOTION_ADD_SUB_VF0.md)). No es un bloqueo
de CPU ni un buffer correcto sin presentar.

## 5. Progreso de EE, scheduler y canalización gráfica (HECHO)

- El EE sigue ejecutándose. Los ticks VSync avanzan de 5927 a 6025 en la ventana posterior a la película
  (~2 ticks/s de reloj en Debug).
- Sin nuevas peticiones CDMODULE `fno=2` (24 en total, las mismas que antes).
- VIF1/VU1/GIF siguen enviando datos: PATH1 (XGKICK de VU1) y PATH2 (VIF1 DIRECT) crecen sin parar. PATH3
  crece despacio.
- El GS procesa: el FRAME de los contextos alterna entre FBP 0, 56 y 160, y los buffers cambian de contenido.

## 6. Buffers de dibujo y presentación (HECHO)

| Registro | Película | Escena posterior |
|---|---|---|
| PMODE | `0xFF27` (EN1, EN2, MMOD=1, ALP=0xFF) | `0xFF27` (tras un `0xFF25` transitorio) |
| SMODE2 | `0x1` | `0x3` |
| DISPFB2 / DISPLAY2 | FBP 160/0, FBW 8; DX=636 DY=64, 512×448 | FBP 0/56, FBW 8; DX=636 DY=64, 512×224 |
| DISPFB1 / DISPLAY1 | FBP 280; DX=656 **DY=456**, MAGH=4, DW=2560 (512 px), **DH=64** | FBP 504; DX=656 **DY=104**, mismo tamaño |
| FRAME (ctx 0/1) | 296 / 0 / 160 | 0 / 56 / 160 |

- El circuito 2 es la imagen principal. El circuito 1 es una **franja de 64 líneas** superpuesta, con `ALP=0xFF`.
- **HECHO (código, `GSCpuBackend::PresentFromLocalMemory`):** la composición de dos circuitos copia el
  circuito 1 desde `y=0`, `x=0` del resultado. **No aplica la posición DX/DY** de cada circuito respecto al
  otro (+392 líneas y +4 px en la película; +40 líneas en la escena).
- **HECHO:** la misma función tiene una heurística de host. Si `DISPFB.FBP == 0` y la imagen sale negra,
  presenta en su lugar un FRAME de contexto no negro. No es comportamiento hardware. En estas corridas no se
  activó (`usedPreferred = 0` y FBP de display ≠ 0 en el circuito 1), pero puede enmascarar estados.

**Respuesta a la pregunta de la fase:** en VISUAL_003 **la escena se generaba mal** (geometría ausente por
`vf0`). Sobre eso hay además un **error de presentación** independiente: la franja del circuito 1 se
coloca en una posición incorrecta.

## 7. VISUAL_001 — duplicación o desplazamiento

- **HECHO:** en las imágenes presentadas durante la película, las 64 líneas superiores muestran otra parte de
  la imagen o el texto de los subtítulos, como «HIS NAME WAS SPARDA» arriba mientras abajo se ve otro texto.
  Coinciden con el circuito 1 (FBP 280, DH=64), que el presentador pega en `y=0` cuando su `DY` lo sitúa
  392 líneas más abajo.
- **INFERENCIA:** VISUAL_001 es el error de colocación de circuitos del presentador de host, no un error de
  datos MPEG ni de geometría.
- **UNKNOWN:** el aspecto exacto en hardware de la composición (alfa `ALP=0xFF`, orden de fondo). Para la
  corrección conviene una captura PCSX2 de PMODE/DISPLAY1/2 en la misma escena.

## 8. VISUAL_002 — ghosting

- **HECHO:** los volcados de la película muestran estelas o dobles contornos del personaje en la imagen del
  circuito 2.
- **HIPÓTESIS (sin demostrar):**
  - decodificación o recirculación MPEG (cuadros de referencia; historia de BLOCKER_004 P4.2.x);
  - mezcla con un buffer anterior durante el doble buffer 160/0;
  - efecto original de la FMV.

  No se ha comparado con PCSX2. No se elimina nada.

## 9. VISUAL_003 — pantalla oscura tras la fachada

- **Causa principal demostrada:** `vf0 = 0` en los hilos → geometría ausente (§4).
- Con `vf0` corregido, la escena sigue siendo **oscura y verdosa**, con un **moiré de huecos diagonales**.
  - **HECHO:** el buffer de dibujo alterno (FBP 0 en `gsV2`, FBP 56 en `gsG1`) contiene solo el color de
    borrado verde.
  - **INFERENCIA:** el tinte verde procede de ese color de borrado, que asoma por los huecos.
- **HIPÓTESIS:**
  - los huecos son un defecto de rasterización o de prueba de alfa/Z del GS por software;
  - la oscuridad viene de `Get_light_str` y `Calc_scr_light` en `ret0`.

  Ninguna está demostrada.
- La imagen es idéntica entre volcados de unos 46 ticks.
  - **UNKNOWN:** si es cámara fija o espera; a ~2 ticks/s solo se observaron ~2 s de juego tras la película.

## 10. Relación con los `ret0`

- **HECHO:** durante la transición se llaman `Get_light_str`, `katVu0Clip_ld`, `katVu0SetClip` y
  `_capVu0ecossin`. No se llaman `ClipCheck2`, `katVu0ViewVolumeClip`, `Calc_md` ni `InterpNode`.
- **HECHO (código):** `Scr_parts_near_trans` acumula `v0 & 0x3F` de `katVu0Clip_ld` en una máscara de clip.
  Con `ret0` la máscara es 0, que equivale a «totalmente dentro».
  - **INFERENCIA:** el `ret0` de `Clip_ld` **no** oculta geometría. No es la causa de los objetos ausentes.
- La geometría ausente se explica por `vf0` (§4), no por la frecuencia de llamadas a ningún `ret0`.

## 11. Experimentos y modificaciones (temporales)

| Experimento | Archivo | Estado |
|---|---|---|
| `DMC_GS_DUMP_DIR` / `DMC_GS_DUMP_PERIOD`: volcado de presentación | `vendor/.../gs/gs_cpu_backend.cpp` (+ `friend` en `gs_cpu_backend.h`, solo lo incluyen 2 `.cpp` del runtime) | temporal, local |
| Contadores GIF por PATH | `vendor/.../gs/ps2_gif_arbiter.cpp` | temporal, local |
| `DMC_VF0_PROBE` / `DMC_VF0_FIX` (de 001I) | `vendor/.../Kernel/EeScheduler.cpp` | temporal, local |

Diff completo: `analysis/local/gameplay_001j/G001J_TEMP_INSTRUMENTATION.diff`. Nada se ha promovido.

## 12. Estado y siguiente bloqueador

- **FIRST_PLAYABLE_FRAME:** NO. Hay imagen 3D de la oficina solo con el experimento `vf0`; no se probaron
  controles ni movimiento de Dante.
- **Siguiente bloqueador (orden):**
  1. Corrección permanente de `vf0` como cambio general. La escena depende de ella.
  2. Colocación DX/DY de los circuitos en el presentador (VISUAL_001), con una captura PCSX2 de referencia.
  3. Huecos en la rasterización (moiré) y tinte. Antes de corregir, oráculo de un fotograma de la oficina en
     PCSX2.
  4. A/B de iluminación (`Get_light_str` / `Calc_scr_light`), una función cada vez.
  5. Rendimiento (~2 ticks/s) para poder observar más tiempo de juego y probar controles.

## 13. Archivos relacionados

`vendor/PS2Recomp/ps2xRuntime/src/lib/gs/gs_cpu_backend.cpp` (`PresentFromLocalMemory`),
`.../gs/gs_frontend.cpp` (`buildPresentationRequestUnlocked`), `.../Kernel/EeScheduler.cpp`.

## 14. Evidencia local (`analysis/local/gameplay_001j/`)

- `video/`: fotogramas y hojas de la grabación (contenido del juego, no publicar).
- `gsdumpG1/`, `gsdumpV2/`:
  - JSON de registros por volcado;
  - PNG/RGBA de lo presentado y de cada contexto;
  - VRAM `.bin` (contenido del juego, no publicar);
  - `summary.json`, `summary.txt`.
- `vram_decode.py`, `gsdump_convert.py`.
- `post_movie_present_vs_ctx.png`, `fb_0_56_x3.png`, `fbV2_0_56_x3.png`, `presentV2.png`,
  `movie_present_92_102.png`.
- `gsG1_run.*`, `gsV2_run.*`, `pubR1_run.*`.
- `G001J_TEMP_INSTRUMENTATION.diff`.
- Copias de los diagnósticos excluidos de la publicación: `dmc_overrides.cpp.diag_001g_001i`,
  `EeScheduler.cpp.vf0_experiment`.

## 15. Relación con otras fases

- Anterior: [GAMEPLAY_001I_TTE_PROMOTION_ADD_SUB_VF0.md](GAMEPLAY_001I_TTE_PROMOTION_ADD_SUB_VF0.md).
  Esta fase convierte la causa de `vf0` en la explicación de VISUAL_003.
- Presentación GS histórica: [BLOCKER_004_pss_video_output.md](BLOCKER_004_pss_video_output.md),
  [BLOCKER_004_Q10_1_FIELD_PRESENTATION_FIX.md](BLOCKER_004_Q10_1_FIELD_PRESENTATION_FIX.md).
- Siguiente: ver [GAMEPLAY_CURRENT_STATUS.md](GAMEPLAY_CURRENT_STATUS.md) §9.

---

## Continuación (2026-10-14): corrección general de `vf0`, composición GS, `ecossin` y control

Esta parte se añade sin modificar las secciones anteriores. Estado de Git:

- PR #2 (rama `gameplay-001i`) fusionado en `master` (`f3c4e33`).
- El trabajo siguiente está en la rama local `gameplay-001j`, **sin commit**.

### A. Parche general `vf0` (`patches/GAMEPLAY_001J_vu0_vf0_hardwired.patch`, parche 16)

**HECHO (diseño, sin tocar `ps2_runtime.h`):**

- `EeScheduler.cpp` fija `vf0 = (0,0,0,1)` y `vi0 = 0` en:
  - `StartThread`;
  - `queueInvocation`, `invokeCurrent` e `invokeCurrentSequence` (todas las invocaciones pasan por ahí).
- `checkpoint_sections.cpp` normaliza `vf0`/`vi0` en `readR5900Context` **y** en `writeR5900Context`. El
  formato no cambia.
  - **Corrección durante la fase:** la primera versión solo normalizaba al leer y rompía dos selftests de ida y
    vuelta byte a byte (`SCHED-1/2/5/6`, `SCHED-6`), con resultado 95/2. Normalizar también al escribir los
    devolvió a 97/0.
- `code_generator.cpp` (recompilador): tras cualquier instrucción traducida que asigne `ctx->vu0_vf[0]`, se
  restaura la constante. Equivale a descartar la escritura.
  - Regenerar con la herramienta corregida cambia **solo** `atari_check_4` y `atari_check_4R`; comparado el
    árbol completo de 10 129 archivos frente a la herramienta previa.
  - Se sincronizaron solo esos 2 archivos.
- `ee_scheduler.h` añade un gancho de test, `debugThreadContextForTest`. Solo lo incluyen unidades del
  runtime (~19 recompiladas), no el código generado.

**HECHO (tests):**

- `ps2x_tests`: 267 PASS, frente a 264 sin el parche. El conjunto de 4 tests fallidos preexistentes es idéntico
  y la suite termina en la misma aserción preexistente de `PS2SifDma`.
- Tests nuevos: escrituras a `vf0` descartadas (VSUB/LQC2/QMTC2), contexto de `StartThread` y restauración de
  checkpoint.
- **Corrección:** una primera versión del test de `StartThread` ejecutaba el scheduler y dejaba colgado el
  proceso de la suite tras la aserción preexistente (timeout en vez de abortar). Lo demostró un control sin
  los tests nuevos (exit 3). Se sustituyó por una inspección sin ejecutar el scheduler.

**HECHO (equivalencia con el experimento `DMC_VF0_FIX`):**

- Con el parche y sin forzado, la sonda ve `vf0 = (0,0,0,1)` en todos los hilos e invocaciones desde su primer
  despacho (18/18 combinaciones; corrida `gsE1`).
- Los buffers de dibujo (páginas 0–111) son idénticos a la corrida experimental `gsV2` en los mismos ticks:
  1,0000 desde el tick 5987 y 0,998–0,9998 en los primeros volcados.
- Tras demostrarlo, el experimento se retiró del vendor.

**Lock local** (sin commit): 16 parches, `patched_commit = ac9928ad3ce1cbf04d6d6b5ac89224c4e80aa30e`. El
replay limpio coincide con el vendor, salvo el diagnóstico GS temporal. SHA-256 del parche:
`b4b0934cc9c972874a71e02c6f3f55ecd80f3e9032f442ccda9fc969b89b2e9b`.

### B. Composición GS (VISUAL_001) — experimento `DMC_GS_DISPLAY_POS`

- **HECHO:** en `PresentFromLocalMemory`, ruta de dos circuitos, cada circuito se coloca en
  `((DX−minDX)/(MAGH+1), (DY−minDY)/(MAGV+1))` y se recorta a 640×512. Es una regla general, sin constantes de
  DMC.
- **Prueba causal aislada** (`gsP1`, sin `vf0`):
  - Main Menu: idéntico (un solo circuito).
  - Película: la franja del circuito 1 deja de duplicarse arriba y pasa a `y=392–455`, `x+4`.
  - Escena: la franja pasa de `y=0` a `y=40`.
- **UNKNOWN:** la altura efectiva y el contenido del circuito 1 en modo entrelazado FRAME (`SMODE2=0x3`).
  - `DISPLAY1.DH=64` frente a las 8 páginas (32 líneas) que quedan desde FBP 504 hasta el final de la VRAM: las
    otras 32 líneas dan la vuelta a la página 0 y muestran escena.
  - **HIPÓTESIS:** en ese modo la franja es una barra de letterbox de 32 líneas.
  - **Se mantiene experimental:** hace falta un oráculo PCSX2 (PMODE, SMODE2, DISPFB/DISPLAY 1-2 y una captura
    de la oficina).

### C. Oficina 3D (VISUAL_003)

- **HECHO:** con el parche `vf0` y **sin variables experimentales** (exe `0054a52a…`, corrida `finalF2`), la
  oficina 3D aparece en la imagen presentada: pared, ventana con persiana, diana y plantas
  (`analysis/local/gameplay_001j/VISUAL_BEFORE_AFTER_office.png`).
- **Defectos que quedan**, independientes y sin causa demostrada:
  - **VISUAL_004:** oscuridad general (HIPÓTESIS: `Get_light_str`/`Calc_scr_light` en `ret0`).
  - **VISUAL_005:** tinte verde y huecos diagonales (moiré). INFERENCIA: el color de borrado del buffer asoma
    por los huecos de rasterización.
  - **VISUAL_006:** la franja del circuito 1 tapa una banda (ver B).

### D. A/B de `_capVu0ecossin` (con el parche `vf0` en ambos brazos)

- Brazos: A1, A2 en `ret0` con sombra pasiva; B1, B2 nativa (`DMC_RET0_NATIVE`). Mismo ejecutable y checkpoint.
- **HECHO:**
  - 4/4 corridas con hitos idénticos (24 recursos, `0x232`, película completa, 0 errores);
  - sombra en el brazo A: seno y coseno 600/600 bit a bit iguales al modelo independiente, ya **sin**
    forzado;
  - framebuffers A1 frente a B1 idénticos en 8/10 ticks comparables. Los 2 restantes coinciden con el momento
    del intercambio del doble buffer.
- **INFERENCIA:** en la ventana observada, la versión nativa no cambia nada visible.
- **Estado:** `ret0` se mantiene (no se restaura sin cobertura de una escena donde influya). Siguen 17 `ret0`.

### E. Control (FASE E)

- **HECHO:** tras la película, el canal de pad por archivo entregó START, Cross, START, Cross, ARRIBA,
  IZQUIERDA y DERECHA (`[P311:pad] buttons=…`; corrida `gsC1`).
- **HECHO:** entre los ticks 5976 y 6091 la imagen presentada es idéntica. El ritmo es ~1 tick/s de reloj en
  Debug, así que toda la secuencia cubre ~2 s de juego.
- **UNKNOWN:** si el juego atiende el pad en esa parte de la cinemática.
- **Dante controlable: NO demostrado.**
- No se creó `dmc_checkpoint_postintro_v1.dmck`.
  - **HECHO:** F9 guarda siempre en la ruta fija `dmc_checkpoint_mainmenu_v2.dmck` (`ps2_runtime.cpp`), así
    que **sobrescribiría el checkpoint v2**.
  - Antes de un checkpoint postintro hay que hacer configurable esa ruta.

### F. VISUAL_002 (ghosting)

Pendiente. No se ha tocado. Requiere distinguir entre vídeo original, entrelazado o posprocesado (oráculo
PCSX2).

### Siguiente

1. Oráculo PCSX2 de la composición (registros de display y captura de la oficina) para cerrar la altura del
   circuito 1 y promover la composición como parche.
2. Rendimiento en Debug (~1 tick/s), o un build Release justificado y documentado, para poder observar control
   real.
3. VISUAL_004/005 con un oráculo de fotograma.
4. Ruta de guardado configurable para el checkpoint postintro.

## Build Release y comparación Debug/Release (2026-10-15)

Informe local: `analysis/local/gameplay_001j/REPORT_GAMEPLAY_001J_RELEASE.md`.

- **HECHO:** el build Release de 16 parches compila (exit 0, 0 errores, 122 min) con MSBuild MultiToolTask
  (`CL_MPCount=4`). Exe Release sha256 `8ccd45557c81a95daa1c36d91663b9b965728f6aff5f2328320c240e9b6b1ffe`.
  - `/MP8` y `/MP4`, con workers persistentes de 3,4–4 GB cada uno, agotaron la RAM.
  - Sin tlogs CL, cada reinicio recompila desde cero.
- **HECHO, mismo checkpoint ABI2, 15 min por corrida:**

  | Fase | Debug | Release |
  |---|---|---|
  | Película `0x232` | 179 s, ~15–18 VSync/s | 51 s, ~60 VSync/s |
  | Oficina 3D | 0,48 VSync/s | 3,30 VSync/s (FPS presentados iguales) |

  - En Debug, la oficina está limitada por CPU monohilo (un hilo al 100 %).
- **RETRACTADO:** la cifra de «~5,6 VSync/s en Debug durante la película» procedía del JSON del volcado GS, que
  deja de actualizarse cuando la presentación se congela. Esa congelación es intermitente (2 de 4 corridas Debug
  previas) y su causa es UNKNOWN. Ahora se mide con `[GP:VSync]`.
- **HECHO:** en Release, el Cross enviado a t≥5 s llega después del timeout del menú y arranca la demo `0x234`. El
  driver envía ahora el Cross 0,5 s después de cargar el checkpoint.
- **HECHO (bloqueo nuevo):**
  - Tras `0x232`, el reloj VSync y el render avanzan, pero la imagen presentada es idéntica byte a byte desde el
    tick ~6157 hasta el 8618 (~49 s de juego).
  - Solo alternan los dos buffers.
  - No hay cargas CD ni RPC nuevas.
- **INFERENCIA:** la lógica de la cinemática no progresa; no es un problema de rendimiento.
- **HIPÓTESIS:** espera de audio o voz (nunca ha habido audio), o efecto de un stub `ret0`. Está pendiente
  localizar la espera.

### Evidencia del juego original aportada por el usuario (2026-10-15)

HECHO (observación del usuario sobre el juego ORIGINAL en PS2, no sobre RECOMP):

1. Cuando la cámara se acerca a la fachada de Devil May Cry, suena un teléfono.
2. Al aparecer la oficina 3D, Dante responde diciendo «Devil May Cry».
3. La cinemática continúa después de esa frase; la oficina todavía no es jugable.

En RECOMP, la oficina se muestra pero la imagen queda idéntica unos 49 s de tiempo emulado, y nunca se ha oído
audio. HIPÓTESIS a comprobar: la progresión del script depende del audio o voz (teléfono / frase de Dante).

### Dependencia de voz en la cinemática posterior a `0x232` (2026-10-15)

Evidencia:

- Traza temporal `runtime/dmc_snd_trace.inc` (`DMC_SND_TRACE=1`), con wrappers sobre los cuerpos generados.
  Corridas `analysis/local/gameplay_001j/snd_release_6m/` y `snd2_release_3m/`.
- Código generado de `Event_scheduler`, `Event_exec_voice`, `Event_voice_call` y `SndAdsPlayCk`.
- `TSNDDRVM.IRX` (sha256 `c00b17f0…31fd`) y `SDRDRV.IRX` (sha256 `91bdee01…638e`), extraídos de la ISO y
  desensamblados con Rabbitizer (`analysis/local/gameplay_001j/irx/`).

**HECHO (código):** `Event_scheduler@0x44C350` es un bucle de tarea. En cada frame:

1. Si el flag de voz `0x9F085D == 1` y `0x9F085B == 0`, llama a `Event_voice_call(0x9F085E, 0x9F085F)` y
   después borra el flag.
2. Ejecuta `Event_exec_cam/pos/mot/face/voice/prog/s_flg/c_flg/light/quake/blur/fade/vib/msg` y
   `Event_skip_possible_check`.
3. Incrementa el contador de frame del evento `0x9F0854`.
4. Llama a `Task_sleep(1)`.

`Event_exec_voice` pone el flag cuando `[cursor+2]` coincide con ese contador.

**HECHO (código):** `Event_voice_call@0x44E320` hace, en este orden:

1. Si `voiceTable[id]` existe: `SndAdsStop(0)` y `while (SndAdsPlayCk(0) >= 0) Task_sleep(1)`.
2. `SndAdsBindPlaySet(1, tabla[id][sub])` y `while (SndAdsStandbyCk(0) <= 0) Task_sleep(1)`.
3. `SndAdsStart(0)` y `Fade_kill(0)`.

`SndAdsPlayCk(ch)` devuelve −1 si `[0x742160] & 8` o `[0x742164] & 0x8000`. Si no, devuelve
`SdrGetState(2, ch)`, que es el RPC `sid=0x1 fno=2` con `send = recv = 0x9E5500/16`.

**HECHO (traza RECOMP, Release):**

- Tras `MPEG:delete`, el planificador hace su primera vuelta completa. `Event_exec_voice` se ejecuta una vez y
  pone el flag.
- En la segunda vuelta, `Event_voice_call(1, 1)` entra: `SndAdsStop(0)` → `SdrXagStop`, y luego
  `SndAdsPlayCk(0)` se llama una y otra vez desde el bucle (`ra=0x44E3F4`).
- `Event_voice_call` no vuelve, no hay más vueltas del planificador y el contador de evento no avanza.
- El resto de tareas sigue dibujando la misma escena: imagen idéntica.

**HECHO (IRX ORIGINAL):**

- `SDRDRV.IRX` es el `sdr_driver` de Sony y registra el SID `0x80000701` (sceSdRemote), no el `0x1`.
- Los SID `0` y `1` los registra `TSNDDRVM.IRX`: `TsndFunc@0x4D14` para el SID 0 y `TsndPutFunc@0x4D88` para el
  SID 1.
- `TsndPutFunc` despacha por `fno & 0xFF` con una tabla de salto (`.rodata` `0xA7E0`):
  - `fno=1` → `XagChkStandby`;
  - `fno=2` → `XagChkPlay`.
- `XagChkPlay(ch)` lee el byte de estado del canal XAG y devuelve: 0 → **−1**; 3 → 1; 4 → 2; 5 → 3; otro → 0.

**HECHO (runtime):**

- Para DMC no hay servicio para los SID 0 ni 1 (RPC «unhandled»).
- El perfil TSNDDRV integrado (`builtin_profiles.cpp`) pertenece a `recvx`, no a DMC.
- El fallback de RPC no manejado con `send == recv` pone `recv` a cero (documentado en 001H).
- Por tanto, `SdrGetState(2, 0)` devuelve 0 en RECOMP. Con la semántica de `XagChkPlay`, 0 significa «canal ocupado
  o en preparación».

**INFERENCIA (causa del bloqueo):**

- La cinemática queda bloqueada en el primer bucle de espera de `Event_voice_call`. El canal XAG 0 nunca
  informa «inactivo» (−1), porque el driver de sonido TSNDDRVM no está implementado para DMC.
- Esa misma ausencia explica que nunca haya habido audio: SE, BGM y voz se envían a los SID 0 y 1 y caen en el
  fallback.

**Expectativa en el ORIGINAL:**

- `XagChkPlay(0)` devolvería −1 con el canal parado.
- Tras `SndAdsBindPlaySet`, la voz se lee del CD por stream (`XagCdRead`, `XagStandby2`). Los datos de voz **no**
  están precargados antes de terminar `0x232`; esto es INFERENCIA por la ruta XAG.
- `XagChkStandby` > 0 cuando el stream está listo; después, `SndAdsStart` lanza «Devil May Cry».
- Luego, `Fade_kill`, se borra el flag y el planificador continúa con cámara, posición y movimientos.

**Pendiente:**

- Validar con PCSX2 el valor real de `0x9E5500` tras `SdrGetState(2,0)` en ese punto.
- Decidir la implementación: un servicio TSNDDRV para DMC, al menos XAG `fno=1/2` y la cola del SID 0.
- No se ha forzado ningún valor.

### TSNDDRVM fase 1: estado de canales XAG (2026-10-15)

**Implementación** (vendor, local, sin commit; se convertirá en un parche documentado):

- `ps2xIOP/src/modules/tsnddrvm_xag.cpp`: servicio `TSNDDRVM`, SID 0 y 1, registrado **solo** en el perfil
  `devil-may-cry-pal` (`builtin_profiles.cpp`). El perfil TSNDDRV de RECVX no se toca.
- Además: `module_factories.h` y `ps2xIOP/CMakeLists.txt` (+1 fuente), y un test en `ps2xTest/src/ps2_iop_tests.cpp`.

**Semántica reconstruida (HECHO, IRX y ELF):**

- **SID 0, `fno` 0:** secuencia de comandos `opcode + argumentos big-endian`, terminada en `0xFF`.
  - Longitudes por opcode según los empaquetadores EE (`sending_req@0x3CD870`, `makebuff*`).
  - `tsndReqFunc` despacha por el nibble alto; `0x30` y `0x70` van a `treq_XAG` (tabla `.rodata 0xA678`):
    `0x31` Standby, `0x32` Start, `0x33` Stop, `0x3D` Standby2(entry, vol), `0x73` SetCore, `0x79` SetSkip3, …
- **SID 1:** `fno = 0x80000000` (solo sube la prioridad del hilo) `| n`; la tabla de 17 entradas está en
  `.rodata 0xA7E0`. Para un `fno` fuera de la tabla devuelve −10.
- **Estado del canal XAG** (byte 0 del registro `0x1E3B0 + ch*0xA8`):
  - 0: inactivo (`XagControlExitFin`);
  - 1: preparando (primer `sb` de `XagControl`, el hilo que lanza `XagStandby2`);
  - 2: arranque pedido (`XagStart` desde 1 o 4);
  - 3, 4 y 5: los fija el stream en curso.
- **Comandos:**
  - `XagStop` → `clear_sema` + `ReleaseWaitThread` → `ExitFin` → estado 0.
  - `XagStandby2` solo actúa con el estado en 0.
- **Consultas:**
  - `XagChkPlay`: 0 → −1; 3/4/5 → 1/2/3; resto → 0.
  - `XagChkStandby`: −1 si no hay semáforo de standby; 2 si `byte 0x96`; si no, contador del semáforo. El
    semáforo lo crea `XagControl` con contador 0 y máximo 1.
  - Un canal ≥ 2 en el argumento significa «core actual»; `SetCore` vale 1 al inicio, según `.sdata`.
- **Retorno:** la respuesta va en la palabra 0 de un búfer estático de 16 bytes. La finalización NOWAIT la hace el
  callback EE `cb_sifRpc → iSignalSema`; el servicio no señaliza.

**Límite de la fase 1:**

- No se modela el streaming de CD (`XagCdRead`/`XagTrans`) ni el semáforo de standby. Un canal en preparación
  responde `XagChkStandby = −1` y nunca afirma estar listo.
- Los `fno` del SID 1 que no son 1 ni 2 siguen en el fallback.
- Los comandos no XAG solo se registran (fase 3).

**Validación:**

- `ps2x_tests`: 268 PASS, frente a 267 de la base; el conjunto de fallos y la aserción `PS2SifDma` son idénticos
  a los preexistentes.
- **Builds incrementales:**
  - Release: 10 unidades de `ps2_iop` + enlace, exe `e3e3a798…`.
  - Debug: lo mismo + `dmc_overrides.cpp`, exe `c5873fba…`.
  - 0 unidades generadas.
- **Corrida Debug `analysis/local/gameplay_001j/tsnd1_debug_7m/`** (`DMC_SND_TRACE=1`):
  - `0x232` completa.
  - `Event_voice_call(1,1)` entra (`ra=0x44C758`); `XagChkPlay(0) = −1` porque el canal 0 está en estado 0.
  - **La primera espera termina.**
  - Después, `SndAdsBindPlaySet(1,1)` → `SdrXagStandby2(1,0x7F)`. Comandos recibidos: `SetCore 0`, `Stop`,
    `SetCore 0`, `SetSkip3` (`0x79`) y `Standby2 entry=1 vol=0x4000`. El canal 0 pasa de 0 a 1.
- **Siguiente espera (HECHO):** `while (SndAdsStandbyCk(0) <= 0) Task_sleep(1)` (`ra=0x44E438`), con
  `XagChkStandby(0) = −1` en cada consulta. El planificador sigue detenido dentro de `Event_voice_call`.
- Además, `SndControl` consulta cada frame `XagChkPlay(1)`, que devuelve −1.

**Incidente de build:**

- El árbol `symtabfirst/build` lo generó **CMake 3.31.6-msvc6** (VS 2022, generador «Visual Studio 17 2022»).
- Los scripts invocaban el `cmake` del PATH (4.4.3). Al cambiar un `CMakeLists.txt`, ese `cmake --build`
  reconfiguró por su cuenta, falló y reescribió `CMakeCache.txt`.
- **Corrección:**
  - copia de la caché y de los 27 `.vcxproj` en `analysis/local/gameplay_001j/cmake_regen_backup/`;
  - una reconfiguración con el 3.31.6 original;
  - comparación de los `.vcxproj`: solo cambió el `ClCompile` del archivo nuevo;
  - los scripts usan ahora el `CMAKE_COMMAND` de la caché.

### TSNDDRVM fase 2a: stream XAG con estado real y desbloqueo de la cinemática (2026-10-15)

**Semántica añadida (HECHO, IRX):**

- `TsndInitXag@0x6D0C` (`0x77`): reserva el búfer, fija `maxCh[core]`, pone el core actual a 1 y resetea ambos
  canales.
- `XagSetBuff` (`0x74`): reparte el búfer entre cores; se rechaza si hay semáforos vivos.
- `XagSetSkip3@0x7C08` (`0x79`): `rec+0xA4` = bloque inicial y `rec+0xA6` = número de bloques.
- `XagControl@0x8D64` valida:
  - canales = `min(entry, maxCh)`, que si es 0 lleva a `ExitFin`;
  - bloque múltiplo de 16 y ≤ `0x20000`;
  - fichero ≥ 2 bloques;
  - ring ≥ 2 slots;
  - rango SPU ≤ `0x1E7800`.
- Después, `XagControl` lanza `XagCdRead@0x821C`, duerme hasta `XagStart`, hace `WaitSema(standby)` y pone el
  estado a 3.
- `XagCdRead` señaliza el semáforo de standby, creado con contador 0 y máximo 1, con 2 bloques en el ring
  (`0x853C`) o al terminar un fragmento corto (`0x877C`).
- **Tablas del IRX** (`.data`):
  - `XagTable@0xA930`: 4 entradas. La entrada 1 es el fichero `0x1B8`, 2 canales, `0x4000` bytes/canal y bloque,
    pitch `0x0EB3` (≈ 44,1 kHz).
  - Tabla de ficheros `@0xA9F0`, `{lba, size}`. Coincide con la numeración de recursos de CDMODULE
    (`0x1B9` → `0x14D801`, `0x232` → `0x215524`). El fichero `0x1B8` está en LBA `0x133411` y ocupa `0xD1F8000`.
  - Slots SPU `@0xA870`.
- **Arranque (HECHO, ELF):** `SndInit@0x1A0B90` envía sin condiciones `SdrXagInit(0x70000, 2, 2)` y
  `SdrXagSetBuff(0, 0x30000)`.

**Implementación** (vendor, local):

- El servicio modela la inicialización, la validación, el standby, la reproducción con duración en tiempo
  emulado y el fin.
  - Duración: muestras = bloques × `0x4000` / 16 × 28; frecuencia = 48 000 × pitch / 4096; ticks PAL a 50/s.
- Las tablas del IRX van como bindings del perfil DMC.
- `IopHost::vsyncTick()` es virtual, con implementación por defecto; el adaptador del runtime devuelve el tick
  VSync del `EeScheduler`.
- **No modelado todavía:** la salida SPU (no hay audio), los bucles, la pausa, los fades y las fases 4/5.
- Tests: 268 PASS; mismos fallos preexistentes.
- **Builds incrementales:**
  - Release: 18 unidades (13 de `ps2_iop` y 5 del runtime).
  - Debug: equivalente.
  - 0 unidades generadas.

**Checkpoint nueva:**

- `dmc_checkpoint_mainmenu_abi2_tsnd_v1.dmck`: 48 900 884 bytes, SHA-256
  `5fc03336934d6fb70b855694da5a0ca68f6a55fe5368ddccf32d256c045fc09e`, tick 20153.
- **Motivo:** la checkpoint ABI 2 anterior se guardó sin servicio TSNDDRVM, así que los comandos de `SndInit` se
  perdieron en el fallback y el driver quedaría sin inicializar. Con el IRX real, eso aborta el stream.
- **Cómo se obtuvo:** arranque en frío en Release (`analysis/local/gameplay_001j/coldboot_tsnd1/`):
  - el servicio recibe `TsndInitXag buffer=0x70000 maxCh=2/2` y `XagSetBuff core=0 size=0x30000`;
  - recorrido: pantalla START → idioma → logo `0x22E` → intro `0x233` → demo `0x22E` cortada con START → menú
    principal;
  - el guardado se pide con el disparador por archivo `PS2X_CHECKPOINT_TRIGGER_FILE`, equivalente a F9;
  - un primer intento falló con `MPEG not idle` mientras sonaba `0x233`.
- Es la misma secuencia de vídeos que el arranque ABI 2 del 2026-10-11.
- No se sobrescribió ninguna checkpoint.

**Validación en Debug** (`analysis/local/gameplay_001j/tsnd2_debug_10m/`, carga en un proceso nuevo, un solo
Cross):

- Ruta NEW GAME correcta y `0x232` completa.
- `Event_voice_call(1,1)`:
  - `XagChkPlay(0) = −1`;
  - comandos `SetCore 0`, `Stop`, `SetSkip3 start=40 count=272`, `Standby2 entry=1`;
  - stream: fichero `0x1B8`, bloques `[40, +272)` = `0x880000` bytes, 7 798 784 muestras/canal → standby;
  - `XagChkStandby(0) = 1`;
  - `SndAdsStart` → `XagStart`: estado 3, ticks 23522 → 32365 (≈ 177 s);
  - **`Event_voice_call` retorna.**
- **HECHO:** el planificador de eventos vuelve a iterar: `Event_exec_voice` se ejecuta en cada vuelta, unas 40
  veces en la corrida.
- **HECHO:** las imágenes presentadas cambian en cada volcado tras el tick 23520 (cinco hashes distintos). Antes
  eran idénticas durante unos 49 s.
- **INFERENCIA:** el fragmento de 177 s del fichero de voces `0x1B8` cubre el diálogo de la cinemática completa.
  La duración real habría que contrastarla con el original.
- FIRST_PLAYABLE_FRAME: no. Control: no evaluado. Audio: no hay salida (fase 2b).

### Resultados: progresión de la cinemática y reproducción de audio (2026-10-15)

#### PROGRESIÓN DE LA CINEMÁTICA (Release, `analysis/local/gameplay_001j/tsnd_release_60m/`)

Configuración: exe `9eea591f…`, checkpoint `dmc_checkpoint_mainmenu_abi2_tsnd_v1.dmck`, un solo Cross.

**HECHO:**

- `0x232` termina a t=57 s.
- `Event_voice_call(1,1)` → stream XAG en estado 3 desde el tick 23548.
- El planificador itera durante toda la cinemática: `Event_exec_voice`, ~1 llamada por VSync.
- Las imágenes cambian en cada volcado: 9 de 9 distintas en los primeros 10 min, con la cámara recorriendo la
  oficina.
- Velocidad: ~3,0–3,8 VSync/s.
- No hay cargas de CD, RPC no manejados nuevos ni bloqueos del render.

**HECHO (fin de la intro, tick 32366, t=2783 s, unos 46 min reales):**

1. `Event_skip(3)` → `SndAdsStop(0)` → `XagStop` con el canal todavía en estado 3. El juego corta la pista 25 ticks
   antes de su final teórico (32391), así que **la duración efectiva viene del script, no de los 177 s del
   fragmento**.
2. `Fade_kill` → vídeo **`0x231`** completo.
3. Carga del recurso **`0x1D4`** (modo 10, `SndBgmLoadCk`, bancos SPU slots 0 y 8) y de `0x2E`/`0x31`/`0x33`/`0x32`.
4. `SndSceneBgmStart` → `SdrBgmReq`.

**BLOQUEO NUEVO (HECHO):**

- `Unimplemented PS2 stub called: sceDmaRecvI` (PC `0x3FEE34` en `Movie_texture@0x3FE9F0`, `a0=0x1000D000`
  *fromSPR*, `a2=0x40`). El runtime aborta el hilo del juego y el proceso sale con código 0.
- `Movie_texture` lo llama `Movie_task_scheduler` y lo usan `Mist_movie_*`, `Background_movie_*` y las pantallas de
  Status, Save, Mission_clear, Try_again y Result.
- Controla el IPU por registros: `Movie_set_toIPU_tag`, espera a `fromIPU` `CHCR@0x1000B000`, y `sceDmaRecvI`
  desde el scratchpad.
- `sceDmaRecvI` real (`0x102618`): espera `CHCR.STR=0`, escribe `MADR`/`QWC` y `CHCR` con `MOD=interleave`,
  `DIR=0` y `STR=1`.
- El runtime y upstream (`origin/main eb8688b`) tienen `sceDmaRecv*` como `TODO`, y no emulan el IPU por hardware
  ni la DMA del scratchpad.
- **INFERENCIA:** la escena posterior a la intro usa vídeo IPU como textura (niebla o fondo). Requiere un
  subsistema nuevo; no basta con implementar un stub.
- FIRST_PLAYABLE_FRAME: no alcanzado; el proceso termina antes.

#### REPRODUCCIÓN REAL DE AUDIO (fase 2b)

- **Implementación** (vendor, local):
  - `IopHost::streamPcm`/`stopPcmStream` (virtuales con implementación por defecto; `iop_host.h` no lo incluye el
    código generado);
  - el adaptador del runtime usa 2 `AudioStream` de raylib con callback;
  - el servicio decodifica SPU-ADPCM desde la ISO en un hilo por core tras `XagStart`, y cancela con `XagStop`.
  - `ps2_audio.h` no se tocó: lo incluye todo el código generado.
- **Decodificación offline previa (HECHO):** `irx/xag_decode.py` sobre la ISO da 176,85 s a 44 098 Hz, estéreo,
  flags ADPCM {2, 3}, señal continua (RMS > 787 todos los segundos). El WAV es local.
- **HECHO (corrida `audio1_release_5m`):** `XAG audio core=0: queued 7798784 frames at 44098 Hz (complete)`.
  **El usuario confirma que se oye el audio**: es el primer audio audible de todo el proyecto.
- **Limitación:** el audio suena a velocidad real y la escena avanza a ~1/15 de la velocidad real, así que van
  desincronizados.
- Siguen mudos los efectos (comandos TQ `0x0X`), la BGM (`0x2X`) y el audio de las películas PSS.
- Builds: Release incremental de 18 unidades, exe `1e6ac562…`; 0 unidades generadas.

### Rendimiento: perfil de Release en la oficina (2026-10-15)

**Herramienta:** perfilador de muestreo propio, dentro del proceso (`Kernel/G001JProfiler.cpp`, temporal,
`DMC_PROFILE`).

- Cada 1 ms suspende los hilos, lee `RIP` y lo simboliza con DbgHelp contra un PDB obtenido solo al enlazar
  (`_LINK_=/DEBUG /OPT:REF /OPT:ICF`), sin recompilar.
- xperf no era utilizable porque la sesión no es administrador.

**HECHO** (`analysis/local/gameplay_001j/profile_A_office.txt`, 60 s en la oficina):

- El hilo del juego está al 98 % de un núcleo: ~83 % en el **intérprete VU1** y ~8 % en el GS por software.
  - VU1: `commitReadyPipelines` 30 %, `normalizeOperand` 7,8 %, `run` 6,7 %, `calculatePairReadyCycle` 6,2 %,
    `updateFmacFlags` 5,9 %, `calculateFmacExactResult` 5,8 %, `execUpper` 4,4 %…
  - GS: `DrawTriangle`, `ReadCT32`, `SampleTexture`, `WritePixel`.
- El código EE recompilado no aparece (`ps2_main` 0,12 %).
- El hilo de render está al 8 %, casi todo en `ZwDelayExecution`; el resto de hilos, ≤ 3 %.
- La instrumentación pesa poco: `g001jMaybeDump` ~1 % y E/S de logs < 0,2 %.

**HECHO** (contadores `[G001J:vu1]` en los callbacks `MSCAL`/`MSCNT`):

- Oficina: ~1,27 M ciclos VU1 por VSync, ~630 microprogramas por VSync, intérprete a ~4,6 M ciclos/s de host y 92 %
  del tiempo de host en VU1.
- Película: ~1 500 ciclos por VSync.

**INFERENCIA:**

- Llegar a 25 VSync/s exige un intérprete VU1 ~6,8× más rápido (31,7 M ciclos/s); 50 VSync/s, ~13,6×.
- Hoy cuesta ~215 ns por ciclo VU1.
- La carga es legítima: ~21 % de la capacidad de un VU1 real a 294,912 MHz por campo PAL.

**Experimento O1/O2, RETRACTADO como optimización:**

- **Qué era:** un límite «próximo commit» por instancia, guardado fuera de `ps2_vu1.h`, para salir pronto de
  `commitReadyPipelines` y saltar ciclos ociosos en `advanceTo`.
- **Resultado:** no mejoró el rendimiento (4,54–4,56 M frente a 4,57–4,60 M ciclos/s).
- **Además, alteró el resultado.** Lo verificó un hash FNV de todos los paquetes PATH1 cada 1 000 paquetes:
  - dos corridas base, A y A2, coinciden en 191 de 191 bloques: el hash es determinista;
  - la versión optimizada difiere desde el primer bloque.
- Se revirtió al código base. Causa de la divergencia: UNKNOWN; no se investigó al no aportar velocidad.
- **Lección:** casi siempre hay entradas pendientes en el pipeline, así que el coste es el propio recorrido.
- **Método útil para otras optimizaciones:** el hash PATH1 permite comprobar exactitud entre A y B.

**Decisión pendiente (importante):** el objetivo de 25 VSync/s requiere cambiar la arquitectura de ejecución del
VU1: recompilación o JIT de microprogramas, o un modelo de pipeline mucho más barato. Las optimizaciones locales no
alcanzan ese factor. Además, cambiar `ps2_vu1.h` recompila las ~10 000 unidades generadas, porque lo incluye
`ps2_runtime.h`.

### Final de intro: salto original y checkpoint (2026-10-15)

- **HECHO (código):**
  - El salto de cinemáticas lo hace `Event_scheduler` cuando `0x9F085A == 1` (lo fija `Sys_flg_set` desde el
    script) y el bit de juego 21 está en `0x74073C`.
  - `0x74073C` es «recién pulsado» del struct de teclas `0x740730`, que rellena `Key_set@0x1A4020`.
  - El bit 21 se traduce del botón real `0x0100` (L2) mediante las tablas `0x503C30`/`0x503CB0`. START es el
    bit 0.
- **HECHO (traza `[SND:skipgate]`):**
  - El script permite saltar (`allowed=1`).
  - Con START o L2 mantenidos 1,5 s, `0x74073C` sigue en 0.
  - `Key_set` no actualiza las teclas mientras `[0x742168] & 0x8000` (`Key_stop_mask`), y `Event_voice_call`
    activa ese bit.
- **INFERENCIA:** la intro no se puede saltar con este mecanismo durante la reproducción de la voz.
- **Consecuencia:** se repite una vez la intro (~46 min) con captura automática (`introskip_ckpt.py`). Tras el vídeo
  `0x231`, con el MPEG libre y el XAG parado, guarda `dmc_checkpoint_introend_v1.dmck` y reintenta si falla.

### HLE de `Movie_texture` (opción 2, en curso)

- **Contrato reconstruido:** detallado en la cabecera de `runtime/dmc_movie_texture.inc`.
  - Slot `0x87D720 + slot*0x34` y avance de frame transcrito.
  - Frame IPU = byte de flags + bitstream `IDEC`.
  - `IDEC` con `OFM=0` (RGB32), sin dither; `SETTH` 0/0 hace que el alfa sea 0x80.
  - Salida a RGBA32 lineal macrobloque a macrobloque (`sceDmaRecvI` fromSPR en interleave, 0x40 qwords).
  - Al final, `Movie_loadimage`.
- **Implementación:**
  - **General (vendor):** `ps2xDecodeIpuFrameRgba` en `Kernel/Stubs/IpuFrameDecode.cpp`, con FFmpeg
    `AV_CODEC_ID_IPU` y conversión BT.601 de rango limitado.
  - **DMC (`runtime/`):** override de `0x3FE9F0`, desactivable con `DMC_MOVIE_TEXTURE_HLE=0`.
  - No se reproducen los efectos laterales en registros IPU/DMAC/scratchpad.
- Debug compila (5 unidades, ninguna generada). Queda la validación con la checkpoint de final de intro.

### Checkpoint de final de intro: resultado (2026-10-15) — corrección

- **HECHO:** la captura automática (`analysis/local/gameplay_001j/introend_capture/`) alcanzó el disparador:
  `0x231` terminó (`MPEG:delete`). Todos los intentos de guardado fallaron, del tick 33862 al 33867, con
  `safe-point validation failed: CDMODULE: movie stream active`. A los ~0,3 s el proceso terminó en
  `sceDmaRecvI`. No se creó ningún archivo.
- **CORRECCIÓN:** antes de la corrida se afirmó que el disparador era alcanzable antes del fallo, y lo fue. Esa
  verificación solo cubrió las condiciones generales del punto seguro, no la condición propia de CDMODULE (stream
  de película activo).
- **INFERENCIA:** tras el final natural de `0x231`, CDMODULE sigue considerando activo el stream hasta la carga
  siguiente. En el arranque en frío, el guardado tuvo éxito después de cortar la demo con START (`CdMovieExit`).
- **Causa exacta:** UNKNOWN. No se investigó; por decisión del usuario, la fase se cierra sin esta checkpoint.

## Cierre de GAMEPLAY_001J (2026-10-15)

**Promovido a parches** (base + 18 parches, `patched_commit 585c347e…`; vendor idéntico byte a byte al replay
limpio, 263 archivos):

| Parche | Contenido |
|---|---|
| 16 | `GAMEPLAY_001J_vu0_vf0_hardwired.patch`: `vf0`/`vi0` cableados |
| 17 | `GAMEPLAY_001J_tsnddrvm_xag_service.patch`: servicio TSNDDRVM (estado XAG, stream, salida audible del XAG) y extensiones de `IopHost` |
| 18 | `GAMEPLAY_001J_checkpoint_save_path_and_trigger.patch`: ruta F9 configurable sin sobrescritura y disparador por archivo |

**Validación del estado publicado:**

- Builds incrementales; ninguna unidad generada recompilada:
  - Release: 8 unidades, exe `a1934eb3…`;
  - Debug: 8 unidades, exe `b35b2f55…`.
- Selftests de checkpoint: 97 PASS, 0 FAIL.
- `ps2x_tests`: 268 PASS; conjunto de fallos idéntico a la base `vf0`, más la aserción preexistente de `PS2SifDma`.
- Prueba de humo Release desde `dmc_checkpoint_mainmenu_abi2_tsnd_v1.dmck`:
  - NEW GAME, `0x232` completa;
  - stream XAG en standby y reproducción;
  - audio encolado completo.

**Retirado del árbol publicado** (conservado en `analysis/local/gameplay_001j/preserved_session/` y
`provisional_ipu/`):

| Retirado | Estado |
|---|---|
| Volcado GS y experimento `DMC_GS_DISPLAY_POS` | instrumentación |
| Contadores GIF | instrumentación |
| `[G001J:latch]`/`[G001J:render]` | instrumentación |
| Perfilador `G001JProfiler.cpp` | instrumentación |
| Contadores `[G001J:vu1]` | instrumentación |
| Hash PATH1 | instrumentación |
| `dmc_snd_trace.inc` | instrumentación |
| Ritmo de audio por tiempo emulado | **no verificado**; diff `UNVERIFIED_tsnddrvm_audio_emulated_time_pacing.diff` |
| HLE de `Movie_texture` y `ps2xDecodeIpuFrameRgba` | **no validados**, no integrados |
| Optimización VU1 O1/O2 | descartada (sin ganancia y no exacta) |

**Estado al cierre:**

- La intro avanza: película `0x232` completa y cinemática en tiempo real de la oficina con voz XAG audible
  (desincronizada; con defectos visuales).
- En una corrida Release de ~46 min, la cinemática termina por su script en el tick 32366, sigue el vídeo `0x231`, carga el escenario siguiente y se
  detiene en `sceDmaRecvI` (IPU/scratchpad, `Movie_texture`).
- Rendimiento Release en escenas 3D: ~3–4 VSync/s, limitado por el intérprete VU1.
- FIRST_PLAYABLE_FRAME: no.
