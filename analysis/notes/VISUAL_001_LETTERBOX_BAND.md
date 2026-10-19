# VISUAL_001 — franja horizontal y banda verdosa en el letterbox de la oficina

Rama `visual-001-letterbox` (desde `master` `4ffafb0`). ELF `SLES_503.58`, SHA-256 `d0753a6b…bab4`, CRC32
`77654AD2`, entry `0x00100008`.

Evidencia local en `analysis/local/gameplay_001l/visual001/`:

- `run1/` (corrida corta; sonda `DMC_V001_PROBE` en los ticks 23740–23760);
- `composition_simulation.png`, `fbp504_pcsx2_vs_recomp.png`;
- desensamblado de `sceGsSetDefDispEnv` y `sceGsSetDefDBuffDc`.

Categorías: HECHO, INFERENCIA, HIPÓTESIS.

## 1. Recorrido de la presentación

1. **Registros de display.**
   - El código nativo de DMC `MainGsSwapDBuffDc@0x15C300` escribe en cada intercambio de buffer
     `PMODE`, `SMODE2`, `DISPFB2`, `DISPLAY2` y `BGCOLOR` (`0x12000000…`).
   - Los toma de la estructura de display que rellenó `sceGsSetDefDBuffDc`. Esa función del SDK es un **HLE**
     del runtime (`Kernel/Stubs/GS.cpp`; en `recomp/symtabfirst.toml` es `stub`).
   - Si `PMODE.EN1` está activo, escribe además `DISPFB1`/`DISPLAY1` desde dos campos propios de DMC que siguen
     a la estructura (`+0x28/+0x30`, `+0x60/+0x68`).
   - Las estructuras están en `0x740770` (escena) y `0x740C30` (película).
2. **Frontend** (`gs_frontend.cpp`):
   - guarda las escrituras privilegiadas en `m_privRegs`;
   - en cada tick, `latchHostPresentationFrame()` construye un `GSPresentationRequest` (PMODE, SMODE2,
     DISPFB/DISPLAY 1-2, BGCOLOR, tick) y llama a `backend->Present()`.
3. **Backend** (`gs_cpu_backend.cpp`):
   - `Present()` toma una copia de la VRAM y llama a `PresentFromLocalMemory()`;
   - por circuito: `decodeDisplaySize` (ancho `(DW+1)/(MAGH+1)`, alto `DH+1`) y `CopyFrameToHostRgba`, que lee
     PSMCT32 por páginas y bloques;
   - con dos circuitos mezcla CRT1 sobre CRT2 con `ALP`/`MMOD`, **ambos desde la fila 0 y la columna 0**;
   - **no se usa `SMODE2`**: el parche q101 retiró el tratamiento de campos.
4. **Host** (`ps2_runtime.cpp`):
   - copia el frame fijado (`copyLatchedHostPresentationFrame`) fila a fila a la textura con `UpdateTexture`;
   - `DrawTexturePro` lo escala a la ventana conservando la proporción.
   - No hay más composición.

## 2. Registros: RECOMP frente a PCSX2 (misma escena)

PCSX2: estado 07 del usuario, registros privilegiados localizados en «PCSX2 Internal Structures.dat» por `CSR`
(`0x551B400C`). RECOMP: sonda en los ticks 23740–23760, valores estables.

| | RECOMP | PCSX2 |
|---|---|---|
| `DISPLAY2` | DX636 **DY64 MAGH0 DW511 DH223** | DX656 **DY104 MAGH4 DW2559 DH447** |
| `DISPLAY1` | DX656 **DY104** MAGH4 DW2559 DH63 | DX656 **DY456** MAGH4 DW2559 DH63 |
| `PMODE` | `0xFF27` (AMOD 0) | `0xFF67` (AMOD 1) |
| `SMODE2`, `DISPFB1/2` | 3, FBP 504 / FBP 0-56 | iguales |

- **HECHO:** la estructura de display del juego (`0x740770`) contiene esos mismos valores en cada lado. Los
  registros reflejan fielmente lo que hay en la estructura.
- **HECHO, SDK real** (desensamblado del ELF, `sceGsSetDefDispEnv@0x1002E8`; `sceGsSetDefDBuffDc@0x101B48` lo
  llama para `disp[0]` y `disp[1]`):
  - `PMODE = 0x66`; `SMODE2` según interlace/ffmode;
  - `mag = (w+2559)/w`, `MAGH = mag−1`, `DW = mag·w−1`;
  - PAL entrelazado: `DX = dx·mag+656`, `DY = dy+72`, `DH = 2h−1` en modo campo o `h−1` en modo frame;
  - NTSC entrelazado: `DX = dx·mag+636`, `DY = dy+50`;
  - sin entrelazado: `DY` +36 (PAL) o +25 (NTSC), `DH = h−1`.
- **HECHO, HLE** (`sceGsSetDefDBuffDc`, también `sceGsSetDefDBuff`): `DISPLAY = (636, 32, MAGH 0, MAGV 0,
  w−1, h−1)` y `PMODE = makePmode(1,1,0,0,0,0x80)`. No tiene en cuenta el modo de vídeo, el entrelazado, el modo
  campo ni la ampliación. `sceGsSetDefDispEnv` (HLE) tampoco suma las bases.
- Para DMC (PAL, campo, 512×224) el SDK da `656/72/MAGH4/2559/447`. El juego suma 32 a `DY`, lo que da
  exactamente los valores de PCSX2.
- **INFERENCIA:** el juego calcula `DISPLAY1.DY` a partir de la altura de `DISPLAY2`. En la estructura de
  película (h=448) el `DH` del HLE coincide con el real y `DISPLAY1.DY` sale 456, igual que en PCSX2. En la de
  escena (`DH` 223 en vez de 447) sale 104.
  - **REFUTADA (implementación, §9.1).** `DISPLAY1.DY` no depende de `DISPLAY2`: `MainGsSetDefDBuffDc` lo
    inicializa con una constante (DY 104) y el juego lo cambia más tarde a 456. Ocurre con el HLE antiguo en el
    tick 23756, y el `DH` de la película lo fuerza el propio juego.

## 3. Primera divergencia de cada síntoma

Correspondencia exacta: el frame presentado en el tick T es, con diferencia **0,00**, la composición de la VRAM
del tick T−1 (cuando se fijó): filas 0–63 = circuito 1 y filas 64–223 = buffer mostrado.

**A. Banda verdosa** (filas 32–63 del frame de 224, filas 112–147 en la ventana de 512×358 a 125 % de DPI):

- **HECHO:** son 32 filas de `(23,36,29)`, el color de borrado del juego. El presentador lee el circuito 1 con
  `DH+1 = 64` filas desde FBP 504. A partir de la fila 32 se sale del final de la VRAM (páginas 504–511) y
  continúa en la página 0, es decir, las filas 0–31 de FBP 0 (diferencia 0,00).
- **HECHO:** aparece **un VSync de cada dos**, cuando se muestra FBP 56 y FBP 0 es el buffer trasero recién
  borrado. Cuando se muestra FBP 0 esas filas son su propio letterbox negro.
- **HECHO, semántica del hardware:** en modo campo (`SMODE2 INT=1, FFMD=1`), `DH` cuenta líneas de ráster y cada
  campo lee `(DH+1)/2` filas. Para el circuito 1 son 32 filas, justo hasta el final de la VRAM y sin vuelta.
- **Primera etapa: presentación** (`decodeDisplaySize` / `PresentFromLocalMemory` no tienen en cuenta
  `SMODE2`). La VRAM es correcta.

**B. Franja desplazada** (filas 0–27):

- **HECHO:** FBP 504, filas 0–31, es una **copia exacta** (0,00) de las filas **176–207** del buffer mostrado.
  También está en FBP 160.
- 176 = `(DY1 − DY2)/2 = (456 − 104)/2` en el hardware: el juego copia esa banda para que el circuito 1 la
  muestre **en su mismo sitio**. En PCSX2, por tanto, el circuito 1 queda encima de su propio contenido.
- En el RECOMP la banda se pinta en la fila 0 por dos motivos:
  - el presentador no aplica `DX/DY` (HECHO, código; ya documentado en 001J);
  - `DISPLAY1.DY` además es incorrecto (104) por el `DH` del HLE.
- **Primera etapa: configuración.** El HLE `sceGsSetDefDBuffDc` produce un `DISPLAY` que no es el del SDK. Hay
  un segundo defecto independiente en la presentación (colocación).

**Simulación** (`composition_simulation.png`, VRAM real del RECOMP):

- Con los registros del SDK y una composición PCRTC correcta (posición `DX/DY`, `(DH+1)/2` filas por campo), la
  salida es **idéntica al buffer mostrado**: sin franja y con el letterbox negro arriba y abajo, como en PCSX2.
- Arreglar solo el presentador con los registros del HLE recortaría la imagen a 112 filas.

**Limitación:**

- Las capturas de ventana (`PrintWindow`) salen remuestreadas por el DPI al 125 % (512×358).
- El mejor alineamiento con un frame interno deja un error medio de ≈10 (escala vertical 1,22, desplazamiento
  85). No se pueden asociar con certeza a un fotograma exacto.
- Muestran la misma estructura (franja arriba, banda negra o verde, escena). La referencia autoritativa es el
  frame interno, que son los píxeles exactos que se suben a la textura.

## 4. Propuesta (texto del diagnóstico; implementada con cambios en §7)

1. **HLE de libgraph** (`Kernel/Stubs/GS.cpp`, parche general):
   - `sceGsSetDefDispEnv`, `sceGsSetDefDBuffDc` y `sceGsSetDefDBuff` calculan `PMODE` y `DISPLAY` con la fórmula
     del SDK, usando `g_gparam` (omode, interlace, ffmode), que el HLE de `sceGsResetGraph` ya rellena;
   - `PMODE` = `0x66`;
   - sin constantes de DMC.
2. **Presentador** (`gs_cpu_backend.cpp`, `PresentFromLocalMemory`/`decodeDisplaySize`, parche general):
   - por circuito, ancho `(DW+1)/(MAGH+1)` y alto de ráster `(DH+1)/(MAGV+1)`;
   - con `INT=1, FFMD=1` se leen `alto/2` filas;
   - cada circuito se coloca en `((DX−minDX)/(MAGH+1), (DY−minDY)/(MAGV+1)/campo)` dentro de la unión, mezclando
     como ahora (`ALP`/`MMOD`, fondo `BGCOLOR`);
   - salida a altura de ráster (cada fila de campo duplicada) para que la ventana no aplaste la imagen. El test
     preexistente que falla, «latched host presentation line-doubles interlaced field output», cubre ese punto
     y debería pasar.
3. **Fuera del alcance:** la heurística de host «FBP 0 negro → FRAME de contexto» se deja como está.

## 5. Validación propuesta

- **Tests unitarios** (`ps2x_tests`):
  - HLE: valores de `DISPLAY` y `PMODE` para PAL/NTSC × interlace/progresivo × campo/frame × anchos
    640/512/320. Caso de oro DMC: `656/72/4/2559/447`.
  - Presentador: dos circuitos en modo campo (el circuito 1 en su fila, `(DH+1)/2` filas, sin vuelta de VRAM),
    modo frame, un circuito, y una VRAM con FBP 504 al final sin desbordamiento.
- **Juego, corridas cortas** con la sonda `DMC_V001_PROBE`:
  - oficina: filas sin franja y sin banda en los dos buffers; el frame presentado coincide con la simulación;
    los registros coinciden con PCSX2;
  - película `0x232`: la banda de subtítulos (FBP 280) en su sitio, abajo;
  - Main Menu: un solo circuito, sin cambios;
  - geometría 3D: sin cambios, porque no se toca VU, VIF ni el rasterizador.
- **Referencia:** PCSX2 en la película y en el menú (registros y captura), igual que en la oficina.

## 6. Coste estimado

- Dos ficheros del runtime (`GS.cpp`, `gs_cpu_backend.cpp`) y sus tests: un parche nuevo, o dos.
- Builds incrementales de unos 2–3 min (runtime) y 5–10 min (tests). No toca el código generado ni requiere la
  compilación completa.
- Validación en juego: 3 corridas cortas de unos 5 min.

## 7. Implementación (parches 26 y 27)

Lock con 27 parches, `patched_commit` `e58f58b93897d28a8fa5f47f9f8c2e73601f5b23`. Los 25 parches anteriores no
cambian. Detalle en `patches/README.md` (entradas 26 y 27).

1. **Parche 26, `VISUAL_001_hle_libgraph_display_env.patch` (`GS.cpp`).**
   - Un único cálculo, `computeGsDefDispEnv`, reproduce el `sceGsSetDefDispEnv` del ELF.
   - Lo usan `sceGsSetDefDispEnv`, `sceGsSetDefDBuffDc` (con dx = dy = 0, como el original) y
     `sceGsSetDefDBuff`.
   - Todo sale de `g_gparam`: `PMODE`, `SMODE2`, `DISPLAY`, `BGCOLOR`. No hay constantes de DMC ni de PAL fijas.
   - Los `DISPFB` de P4.1/P4.1.10 se conservan.
2. **Parche 27, `VISUAL_001_pcrtc_circuit_presentation.patch` (`gs_cpu_backend.cpp`).**
   - Geometría por circuito.
   - En modo campo se leen `(DH+1)/2` filas y cada una cubre dos líneas.
   - Los circuitos se colocan en `DX/DY` y se mezclan con `PMODE` como antes.
   - Sin weave ni desplazamiento por paridad (§9.2).
   - `decodeDisplaySize` se sustituye por `decodeCircuitLayout`.
   - No hay cambios en cabeceras: se recompilan solo `GS.cpp`, `gs_cpu_backend.cpp` y `gs_frontend.cpp`.

No se ha tocado VU0, VU1, VIF, DMA, skinning ni el rasterizador.

## 8. Validación

### 8.1 Tests (`ps2x_tests`)

| Estado | Tests | Fallos |
|---|---|---|
| Base (25 parches, tests de 001L) | 450 | 25 |
| Runtime de 25 parches + tests nuevos | 458 | 32: los 25 + 7 tests nuevos |
| Parche 26 solo (con sus tests) | 453 | 25 |
| Parches 26 + 27 | 458 | **24**: ninguno nuevo; pasa el preexistente «line-doubles…», cuya configuración también se corrigió (SMODE2 1 → 3), así que el cambio no se debe solo al runtime |

- HECHO: los 7 tests nuevos fallan con el runtime anterior y pasan con los parches:
  - HLE: seis modos de vídeo, `omode` desconocido y el caso DMC;
  - presentador: filas de campo sin vuelta de VRAM, dos circuitos en `DX/DY` con `PMODE`/`SLBG`, 448 líneas,
    y sin mezcla con el campo anterior.
- El test de modo FRAME (no regresión de q101) pasa en ambos estados.
- Cambios en tests existentes:
  - el test de `sceGsSetDefDBuffDc` comprueba `DW = ancho·(MAGH+1)` (antes `DW = 639`);
  - el test «line-doubles» pasa de `SMODE2` 1 a 3, que es lo que dice su comentario.

### 8.2 En el juego: limitación de los checkpoints

- HECHO: el juego construye las estructuras de display una sola vez.
  - `Main_init@0x15BA90` llama a `sceGsResetGraph(0, 1, 3, 1)`.
  - Después llama a `MainGsSetDefDBuffDc@0x15C6A0` para la escena (0x740770, 512×224, modo campo) y para la
    película (0x740C30, 512×448; el juego fuerza FRAME y DH 447).
  - `MainGsSetDefDBuffDc` copia los dos `disp` y les pone `EN1`, `MMOD` y `ALP` 0xFF, suma 32 a `DY` y escribe un
    `DISPLAY1` constante (DY 104).
- HECHO: el checkpoint del menú (tick 20153) contiene esas estructuras ya creadas con el HLE antiguo.
  `g_gparam` no forma parte del checkpoint.
  - Con el ejecutable corregido y el checkpoint tal cual (run2), los registros siguen siendo los antiguos y el
    presentador correcto da 640×224 con escalas mezcladas: es peor que antes.
  - **El parche 26 solo actúa tras un arranque en frío.**
- No se lanzó un arranque en frío: es una corrida larga y no estaba autorizada.

### 8.3 Experimento diagnóstico (`DMC_V001_REINIT_DISPLAY`, solo en la sonda local)

- **Por qué:** validar todo el camino real (swap del juego → registros GS → presentador → ventana) sin arranque
  en frío.
- **Qué hace:** en el primer `MainGsSwapDBuffDc` tras el tick 20154 ejecuta el código del propio juego con los
  argumentos de `Main_init`:
  - `sceGsResetGraph(0, 1, 3, 1)`;
  - `MainGsSetDefDBuffDc` para las dos estructuras.

  De ese resultado conserva solo `SMODE2`, `DISPLAY` y el bit `AMOD` de `PMODE`. Todo lo demás se restaura.
- **Cuándo retirarlo:** con la sonda, antes de cerrar VISUAL_001. No es un arreglo.
- HECHO, resultado del experimento:

| | Antes (HLE antiguo) | Después (experimento) | PCSX2 s07 |
|---|---|---|---|
| Oficina `PMODE` | FF27 | FF67 | FF67 |
| Oficina `DISPLAY2` | DX636 DY64 MAGH0 DW511 DH223 | DX656 DY104 MAGH4 DW2559 DH447 | igual que después |
| Oficina `DISPLAY1` (tick ≥ 23756) | DX656 DY456 MAGH4 DW2559 DH63 | igual | igual |
| Película `DISPLAY2` | DX636 DY64 MAGH0 DW511 DH447, `SMODE2` 1 | DX656 DY104 MAGH4 DW2559 DH447, `SMODE2` 1 | — |
| Menú | `PMODE` FF26 (circuito 1 apagado) | FF66 | — |

### 8.4 Corridas cortas antes/después

Mismo checkpoint, mismas entradas y la misma sonda (`run3_before`, `run3_after`). Capturas en los ticks
20156–20162 (menú), 21501–21509 (película `0x232`) y 23740–23760 (oficina).

- **Comparación con el modelo:**
  - HECHO: 32 de 33 frames presentados «después» coinciden **al 100 % de los píxeles** con un modelo PCRTC
    independiente (Python, semántica del manual GS) aplicado a la VRAM y a los registros capturados.
  - La excepción es el primer frame del rango, cuyo origen es anterior a la captura.
  - El frame fijado corresponde a la VRAM de la captura anterior o de la misma.
- **Oficina:**
  - HECHO: 512×448; 0 filas del color de borrado (23,36,29) en 33 frames.
  - HECHO: 0 filas difieren de la vista con el circuito 2 solo, tanto con `DY1` 104 como con 456. La banda del
    circuito 1 cae exactamente sobre las filas que copia, así que no hay franja desplazada.
  - HECHO, antes:
    - 512×224, con la banda en la fila 0;
    - en run1, 10 de 21 frames con filas (23,36,29);
    - en run3_before, 0 de 33: el síntoma depende del momento del latch, pero la lectura de 64 filas está en
      el código antiguo en ambos casos.
- **Película `0x232`:**
  - HECHO, antes: la banda de subtítulos (FBP 280) aparecía arriba y duplicaba el texto.
  - HECHO, después: aparece en su sitio (DY 456 → línea 352) y el fotograma no tiene duplicados.
- **Menú:** HECHO: un solo circuito; pasa de 512×224 aplanado a 512×448.
- **Proporción vertical frente a PCSX2:**
  - HECHO: en una captura de PCSX2 de la misma escena (4:3), el contenido ocupa las líneas 39–408 de 448
    (escalado desde 21–218 de 240). En RECOMP «después», 40–407.
  - La VRAM del savestate s07 no sirve para comparar píxeles: sus dos buffers de display tienen el color de
    borrado. INFERENCIA: con el renderer por hardware, la memoria local no contiene los render targets.
- **Imágenes (locales):** `cmp_office.png`, `cmp_menu_movie.png`, `pcsx2_before_after_office.png`.
- **Contenido renderizado:**
  - HECHO: las dos corridas son deterministas con un desfase de 14 ticks. Los 7 buffers de la oficina
    «después» que se solapan con la ventana «antes» son **idénticos byte a byte** a los «antes»; el buffer
    del menú en 20158 también.
  - La geometría y el render no cambian. La película llega a su fin (`movie_end`) en ambas corridas.
  - No verificado en esta fase: Trish y la moto no aparecen en la ventana de ticks capturada.

## 9. Correcciones de método

1. **`DISPLAY1.DY`:** ver la refutación en §2.
   - Evidencia (run2, HLE antiguo): en los ticks 23740–23755, `DY1` 104 y FBP 504 copia las filas 0–31 del
     buffer mostrado.
   - En los ticks 23756–23760, `DY1` 456 y FBP 504 copia las filas 176–207.
   - En ambos casos la fila copiada es `(DY1 − 104)/2`: el juego asume `DY2` = 104 (SDK 72 + 32).
2. **Weave y paridad del VSync (hipótesis de diseño, refutada).**
   - **Hipótesis:** la primera versión del parche 27 entrelazaba el campo anterior y desplazaba por la paridad
     del tick (`CSR.FIELD`). Se basaba en que FBP 0 y FBP 56 se dibujan con OFY 1936,5 y 1936,0.
   - **Evidencia en contra (HECHO):**
     - el frame que se fija procede de la VRAM de 1–2 capturas antes (latch asíncrono en el hilo de render);
     - la asociación buffer↔paridad del tick se invierte entre corridas: en run1, FBP 56 en ticks pares; en
       run3_after_weave, FBP 0; en run3_after, FBP 56;
     - el medio píxel lo fija el juego en cada frame con su propia variable (lh 0xC4 sobre 0x7406A0, en
       `MainGsSwapDBuffDc`);
     - entrelazar los buffers de capturas consecutivas da una rugosidad vertical de 4,4 en ambos órdenes,
       frente a 2,5 de un solo campo (peinado visible).
   - **Estado:** cada fila de campo cubre sus dos líneas y no se mezclan campos. Los tests de paridad y weave se
     sustituyeron por «448 líneas en cualquier campo» y «sin mezcla con el campo anterior».
   - Un weave correcto necesitaría saber el campo del swap del juego en el frontend. Eso implica cambiar una
     cabecera compartida y una recompilación completa, así que queda fuera.
3. **Experimento, primera versión (descartada).**
   - Copiaba el `PMODE` entero de la reinicialización y pisaba bits que el juego cambia después del arranque:
     en el menú `EN1`; en la película `ALP` y `MMOD`. El menú mostraba el circuito 1 y la película perdía la
     banda.
   - Esa corrida (`run3_after_DISCARDED_pmode_artifact`) no se usa. La versión válida solo toma `AMOD`, que es el
     único bit de `PMODE` en que difieren el HLE antiguo (0x8007) y el de libgraph (0x66) fuera de los bits del
     juego.

## 10. Pendiente y diferencias

- **Validación definitiva:** arranque en frío hasta la oficina con el ejecutable sin sonda. Es una corrida
  larga y requiere autorización.
- **Checkpoints:** los actuales llevan el `DISPLAY` antiguo. Con los parches 26 y 27, cargar uno da una imagen
  peor que antes (§8.2) hasta que se regenere desde un arranque en frío. Regenerarlo está fuera del alcance.
- **`g_gparam` no está en el checkpoint:** tras cargar uno queda en NTSC, campo. Solo afecta a nuevas llamadas a
  `sceGsSetDefDispEnv`/`DBuffDc`; DMC no las hace después del arranque.
- **Proporción horizontal:** el host muestra 512×448 con proporción uniforme (barras laterales), mientras que
  PCSX2 estira a 4:3. Corresponde al dibujado del host (`ps2_runtime.cpp`), no al presentador.
- **Movimiento de medio línea:** al no entrelazar, el contenido de campos alternos difiere en medio línea de
  campo, porque el juego dibuja cada buffer con su propio medio píxel.
- **Sin medir:** los demás modos (NTSC, progresivo) solo están cubiertos por tests; no se ha probado otro juego.

## 11. Instrumentación temporal

La sonda local `v001_present_probe.inc` (retirada; nunca formó parte del repositorio) se incluía desde
`dmc_overrides.cpp` y estaba desactivada por defecto.

- Variables: `DMC_V001_DIR`, `DMC_V001_PROBE`, `DMC_V001_PROBE_MOVIE`, `DMC_V001_PROBE_SWAP` y el experimento
  `DMC_V001_REINIT_DISPLAY`.
- No tiene commit ni forma parte de los parches.
- Para la corrida en frío se añadió un modo de observación: registro de cambios de registros y capturas
  bajo petición.
- **Retirada al cerrar VISUAL_001 (2026-10-19):**
  - se eliminaron el fichero, el include y la llamada; `dmc_overrides.cpp` vuelve a ser idéntico a `master`;
  - se recompiló solo esa unidad (ejecutable `f62d670f…`, sin cadenas de la sonda);
  - la copia y el diff del gancho quedan como evidencia local en `visual001/probe_retired/`.

## 12. Arranque en frío (2026-10-19, corrida autorizada `cold1`)

Ejecutable con los parches 26 y 27 más la sonda solo de observación: sin checkpoint, sin el experimento
`DMC_V001_REINIT_DISPLAY` y sin escribir registros. FFmpeg ON.

**Recorrido:**

1. Pantalla de memory card (START manual por el canal de pad).
2. Selección de idioma (English, CROSS).
3. Logo `0x22E` y menú principal.
4. Por inactividad, intro `0x233` y demo `0x22E`; el script pulsó START.
5. Menú: guardado del checkpoint nuevo.
6. CROSS (NEW GAME) y película `0x232`.

- HECHO: la corrida terminó en el tick 15240, **durante la película y antes de la oficina**. Fue un fallo del
  script, no del juego: tomó el final de `0x22E` como final de `0x232`, hizo las capturas «office_*» durante la
  película y cerró el proceso.
- Cero errores fatales. El script quedó corregido y no se relanzó.

**Registros (solo inicialización normal del juego):**

- HECHO: desde el tick 13, `DISPLAY2` = DX656 DY104 MAGH4 DW2559 DH447 (`0x1BF9FF02068290`). Es el valor de
  PCSX2 s07 y no cambia en toda la corrida.
- Menú: `PMODE` FF66, `SMODE2` 3 o 1, circuito 1 apagado.
- Película: `PMODE` FF67, `SMODE2` 1, `DISPFB1` FBP 280, `DISPLAY1` DX656 DY456 MAGH4 DW2559 DH63.
- Antes de la película (tick 13185), estructura de escena: `PMODE` FF67, `SMODE2` 3, `DISPLAY1` DY104.
- INFERENCIA: son los mismos valores que produjo el experimento de §8.3, que queda confirmado por la ruta
  real.

**Frames presentados:**

- HECHO: 42 de 42 frames (12 del menú, 30 de la película) coinciden al 100 % de los píxeles con el modelo PCRTC
  independiente sobre la VRAM capturada.
- Todos son de 512×448 y ninguno tiene filas del color de borrado.
- En la película, la banda de subtítulos está en su sitio, sin duplicado arriba.
- Imagen local: `cold1_sheet.png`.

**Sin verificar en esta corrida:** la oficina (franja, banda verde, Trish, moto, iluminación).

**Checkpoint nuevo:** `dmc_checkpoint_mainmenu_abi2_tsnd_visual001_v1.dmck`.

- Menú principal, tick 12904, 48 899 218 bytes, SHA-256 `94b229290f33a80c…3eeb`.
- Formato 2, ABI 2, `compat` `c3db86c0…4fdc`.
- Ejecutable `8dc36a68…` (identidad `e411abc1…`), lock de 27 parches; con sonda, que no altera el estado.
- Contiene las estructuras de display del arranque con el HLE corregido.
- El primer intento de guardado (tick 11041) se rechazó con `MPEG not idle`, durante la intro, y no creó
  ningún fichero.
- No se ha validado cargándolo en un proceso nuevo. Los checkpoints anteriores no se tocaron.

**Compatibilidad:**

- HECHO: la identidad `compat` es la misma que la del checkpoint antiguo (`c3db86c0…`). El gate de
  compatibilidad **acepta** checkpoints creados con el HLE antiguo, aunque su estado de display sea
  incompatible con el presentador nuevo (§8.2).

**Propuesta (no implementada):**

1. Incluir en la identidad de compatibilidad una versión semántica de los HLE cuyo resultado queda en memoria
   del juego (por ejemplo `gs.libgraph_display=2` desde el parche 26), para rechazar con un mensaje claro los
   checkpoints anteriores.
2. Guardar `g_gparam` en el checkpoint, en una sección nueva con versión, y rechazar o avisar cuando falte.
3. Como comprobación específica de DMC en `runtime/`, fuera del vendor: al cargar, avisar si el `DISPLAY` de
   las estructuras 0x740770 y 0x740C30 tiene MAGH 0 con DW 511 (valor del HLE antiguo).

## 13. Cierre de VISUAL_001 (2026-10-19)

**Resultado:** queda resuelta la causa de la banda verdosa y de la franja desplazada.

- **Configuración:** el HLE de libgraph programaba un `DISPLAY` que no es el del SDK (parche 26).
- **Presentación:** el presentador ignoraba el modo campo y `DX/DY` (parche 27).

**Validación: qué se comprobó y cómo.**

| Comprobación | Corrida real sin intervención | Con el experimento diagnóstico | Solo tests o modelo |
|---|---|---|---|
| Registros correctos de la inicialización natural (`DISPLAY2` = PCSX2 s07) | **sí** (`cold1`, §12) | también | — |
| Menú 512×448, composición correcta | **sí** (`cold1`) | sí (`run3_after`) | modelo 100 % |
| Película `0x232`: banda de subtítulos en su sitio, sin duplicado | **sí** (`cold1`) | sí | modelo 100 % |
| Oficina sin franja desplazada ni banda verdosa, 512×448 | no | **sí** (`run3_after`, §8.3–8.4) | modelo 100 % |
| Oficina: registros iguales a PCSX2 s07 (`PMODE`, `SMODE2`, `DISPFB`, `DISPLAY1/2`) | no | **sí** | — |
| Proporción vertical de la oficina frente a PCSX2 (líneas 40–407 frente a 39–408) | no | **sí** | — |
| Trish, moto e iluminación de la oficina | no | no (fuera de la ventana de ticks) | — |
| Modos NTSC, progresivo y FRAME con otras dimensiones | no | no | tests |

**No hubo una corrida en frío completa hasta la oficina.** `cold1` terminó durante `0x232` por un fallo del
script (§12).

**Por qué se pueden combinar las dos corridas:**

- HECHO: los ejecutables de `run3_after` (`ea66a3b9…`) y `cold1` (`8dc36a68…`) se compilaron de las mismas
  fuentes del runtime. `GS.cpp` y `gs_cpu_backend.cpp` son idénticos byte a byte al replay de 27 parches.
- Solo cambió la sonda local de `dmc_overrides.cpp`: en `cold1`, de observación; en `run3_after`, con el
  experimento.
- HECHO: el experimento produjo para la estructura de escena el mismo `DISPLAY2` (`0x1BF9FF02068290`),
  `SMODE2` 3 y `PMODE` 0x…67 con circuito 1 que la inicialización natural de `cold1` (tick 13185; `DISPLAY2`
  desde el tick 13).
- INFERENCIA: la oficina de `run3_after` usa los mismos registros que tendría tras un arranque real. El resto
  del estado venía del checkpoint antiguo, que no contiene nada de display aparte de esas estructuras.

**Verificación de integración:**

- Lock con 27 parches, `patched_commit` `e58f58b9…`. Los parches 1–25 no cambian.
- Replay desde el árbol de trabajo y desde los bytes que Git almacenaría (índice temporal, sin tocar el
  índice real): ambos coinciden con el lock, y el vendor es idéntico al replay.
- Los parches y el lock se almacenan con LF, igual que en el árbol de trabajo.
- `ps2x_tests`: 458 tests, 24 fallos, todos preexistentes. Sin regresiones nuevas.

**Pendiente para fases posteriores (no implementado):**

1. **Compatibilidad de checkpoints antiguos con la nueva semántica de display.** La identidad `compat` no
   cambió, así que los acepta y la imagen empeora (§8.2). Propuesta en §12.
2. **Persistencia de `g_gparam` en el checkpoint.**
3. **Carga del checkpoint nuevo en otro proceso:** `dmc_checkpoint_mainmenu_abi2_tsnd_visual001_v1.dmck`
   (menú, tick 12904, SHA-256 `94b22929…3eeb`) todavía no está validado. Hasta entonces, el antiguo sigue
   siendo el operativo para el audio y para fases que no dependen de la presentación.
4. **Escalado horizontal 4:3** en el dibujado del host.
5. **Campos entrelazados:** sin weave; los campos alternos difieren en medio línea (§9.2).
6. **Modos de vídeo no comprobados en el juego:** NTSC, progresivo y FRAME con otras dimensiones.
7. **Oficina de extremo a extremo** desde un arranque en frío, si se quiere confirmación directa: Trish, moto
   e iluminación.
