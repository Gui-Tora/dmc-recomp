# P4.0F — Fable: baseline causal completo de presentación GS (MPEG → renderer)

Investigación primaria + auditoría adversarial. Continúa y cierra el P4.0
iniciado por Astra. Sin corridas nuevas: toda la evidencia dinámica proviene
de RUN_043 (y dumps RAM de RUN_029 para estado guest en el arranque de la
película). Sin commit, sin push, sin cambios de producción.

## 1. Estado de entrada

- Main HEAD `6beacfbfb1795813cb34ced0fd7e723b435016f3` ✓ (esperado).
- Vendor HEAD `61a0977924148e018d66d42c30d7756db8084541` ✓.
- Dirty al entrar: vendor `gs_frontend.cpp` (+41) y `ps2_memory.cpp` (+17)
  — exactamente los diagnósticos declarados por Astra, leídos completos;
  main: `?? analysis/tools/p40/` únicamente. **Sin dirt ajeno.**
- Al cierre: vendor revertido a `61a0977` (diff --stat vacío verificado);
  main: este informe + tools p40 sin trackear.

## 2. Evidencia de Astra heredada y su clasificación

- `analysis/tools/p40/instrument.py` + `build.ps1`: generan/parchean los
  diagnósticos y el build quirúrgico → **TEMPORARY_DIAGNOSTIC_TOOL** (su
  producto ya fue revertido; conservarlos untracked es inocuo, pueden
  regenerar el patch si P4.0.1 lo necesita).
- `analysis/tools/p40/reconstruct.py` + `analysis/local/p40/tables.md` +
  `reconstruction.json`: reconstrucción estática de las cadenas de tags →
  **REUSABLE_TOOL** (recomendado para un checkpoint posterior).
- RUN_043: 256 [P40:DMA], 132 [P40:TAG], 262 [P40:QW], 256 [P40:DMAEND],
  12 [P40:GIF], 792 [P40:REG], 384 [P40:GSBUF], 105 [P40:DRAW],
  210 [P40:VTX], 180 [P40:DISPLAY], 180 [P40:PRESENT]. Parser propio en
  scratchpad (`run043.json`).

## 3. Verificación independiente de lo más fuerte de Astra

- **Cadena de tags** (tables.md) verificada contra [P40:TAG]/[P40:QW] y
  contra los QW crudos de RAM guest: 66 tags por kick (33 CNT + 32 REF +
  END), 919600 bytes, REF paso 0x7000, fuentes `0x79D680+N·28672` ✓.
- **BITBLTBUF real** (QW crudo `0x0008140000000000` reg 0x50): DBP=0x1400
  (slot 1) / DBP=0 (slot 0, GSBUF movie 2), DBW=8, **DPSM=0=PSMCT32** —
  HECHO desde memoria guest, no interpretación. TRXREG=16×448
  (`0x000001C000000010`), TRXPOS X=16·k (verificado k=0,1,2,3…),
  TRXDIR=0. REG counts: BITBLTBUF/TRXREG 1× por película, TRXPOS/TRXDIR
  32× ✓.
- **GSBUF**: 384/384 muestras con mismatch=0 e input==gs hash; 12
  películas muestreadas × 32 strips; X cubre {0,16,…,496} sin huecos ni
  duplicados; 28672 bytes = 7168 px × 4 ✓ por strip.
- **Corrección de unidades hecha por esta auditoría** (Fase 5): las
  superficies de subida son BYTES 0x000000 y 0x140000 (DBP×256); los
  framebuffers FBP {0xA0,0xE0} son BYTES {0x140000,0x1C0000} (FBP×8192).
  El "0/0x140000 vs 0x140000/0x1C0000" de Astra era correcto tras
  normalizar — y el solapamiento upload-slot1 ≡ framebuffer A0 resulta
  central para el diagnóstico (ver §7).

## 4. Auditoría adversarial de mismatch=0 (Fases 2-3)

El diagnóstico escribe con `UploadImage` (WriteVram) y relee con
`ReadVram` del MISMO backend: **mismatch=0 = INTERNAL_RUNTIME_SELF_
CONSISTENCY, no equivalencia hardware**. Un swizzle PSMCT32 erróneo pero
simétrico daría también 0. Sin embargo: (a) el consumidor interno
(MoveImage local→local y CopyFrameToHostRgba) lee por el mismo mapper →
la autoconsistencia basta para la cadena interna del RECOMP; (b) la
evidencia visual (película reconocible en pantalla, §7) demuestra que los
píxeles sobreviven extremo a extremo. La equivalencia bit-exacta con el
swizzle hardware queda UNKNOWN y es NO-CAUSAL para el síntoma.

**Orden de bytes/alfa (Fase 3)**: `writeDecodedFrameToGuest` escribe
byte0=R,1=G,2=B,3=A=0x80 — coincide con PSMCT32 (R en bits 0-7) y con la
semántica de alfa PS2 (0x80=1.0). COMPATIBLE. Además escribe el buffer
guest en **strips columna de 16×448 contiguos** — exactamente el layout
que consumen los 32 REF (por eso cada REF es una banda vertical). La
geometría de los 32 strips (Fase 4) queda completa: sin huecos, sin
solapes, sin cambio de destino a mitad de frame (BITBLTBUF 1× por kick).

## 5. Continuación: reconstrucción de la arquitectura REAL de display de película (nueva, esta auditoría)

Todo verificado por desensamblado generado + words crudos del ELF + RAM
guest de RUN_029 (`getpicture.bin`):

1. **`MpegMovieDecode` rama `+0x08==0`** (primera picture): fija bit
   0x1000 en `0x742158` (modo película), escribe `0x5CE3FF` =
   `((flags&0x8001)!=0x8000)` con flags = halfword alto del descriptor
   `0x5072B0 + idx·4` (idx=4, id=0x22E; tabla estática, TODAS las
   entradas 0x8000 en ELF y en RAM; sin ningún escritor en todo el
   binario) → **FF=0 SIEMPRE, también en hardware** → la pareja
   `MainGsSwapDBuffDc_movie`/flip de campo de `Main_Get_Vsync` (envs
   0x7410F0, SMODE2=3) es **código muerto para esta tabla de películas**
   — RETRACTADO como candidato. Pasa `imageAddr=0x79D680` como a1 a
   `Movie_set_loadimage3` (cierra el UNKNOWN de P3.15 §10) con
   a3=0/0x1400 constantes (los dos DBP).
2. **Swap real durante película**: `ps2_main` (rama 0x1000, FF==0) llama
   cada frame a `MainGsSwapDBuffDc(0x740C30+…, which)` que escribe PMODE/
   SMODE2/DISPFB2/DISPLAY2/BGCOLOR privilegiados desde el env struct
   `0x740C30` — contenidos en RUN_029: PMODE=0xff26, SMODE2=1, DISPFB2
   {0x10E0,0x10A0} — **idéntico a lo observado en los 180 [P40:DISPLAY]
   de RUN_043**: el lado display del RECOMP reproduce fielmente lo que el
   guest programa. `MainSetPalMovieEnv` (desde `Main_init`) es quien
   produce PMODE=0xff26/ALP=0xFF/EN1=0 y DISPFB1=0x1118.
3. **El display del film es el CIRCUITO 1**: `Main_draw_ot_movie`
   (modo `0x742209=1`, verificado en RAM; jumptable 0x57F950: modos 1-5 →
   misma ruta) envía cada frame un paquete (0x740F80+0x350) que configura
   el CONTEXTO 2 con **FRAME_2=0x80128 (FBP 0x128 → 0x250000)**,
   XYOFFSET_2=(28672,29184), SCISSOR_2 511×447, y luego llama
   **`MoveImage2` = copia GS local→local (TRXDIR=2) de 512×448 desde el
   slot de subida {SBP 0x0000|0x1400, structs 0x5DE998/0x5DE9C0} hacia
   DBP=0x2500 → byte 0x250000** (superficie de película). El fade
   (`Fade_trans`) dibuja el sprite negro 512×448 en a0/e0 — ese es el
   sprite tme=0 observado (no es el draw de la película; no existe draw
   texturizado del film: el prompt asumía TEX0 y DMC usa MoveImage).
4. **Selección de scanout del film**: DISPFB1=0x1118 = FBP 0x118 →
   0x230000 (la ventana que contiene 0x250000 con offset de 64 líneas),
   DISPLAY1 PAL preparado por `MainSetPalMovieEnv` — pero con **EN1=0**.
   `MainSetRect_movie@0x15CAF0` pone EN1=1 en ambos envs (y
   `MainResetRect_movie` lo quita). Sus únicos callers estáticos son las
   funciones de subtítulos de los scripts de sala
   (`R002_movie_sub_title_disp_01/02`, `R00e_*`, `R40a_*`);
   `r002_opening_event_2` llama `Movie_set`/`Movie_on`/`MovieEndCheck` y
   `MainResetRect_movie` 2×.

## 6. Normalización de unidades (tabla, Fase 5)

| Campo | Raw | Unidad | Bytes |
|---|---|---|---|
| BITBLTBUF.DBP (slot0/1) | 0x0 / 0x1400 | ×256 B | 0x000000 / 0x140000 |
| MoveImage2 DBP | 0x2500 | ×256 B | 0x250000 |
| MoveImage2 SBP | 0x0 / 0x1400 | ×256 B | 0x000000 / 0x140000 |
| FRAME.FBP (fade) | 0xA0 / 0xE0 | ×8192 B | 0x140000 / 0x1C0000 |
| FRAME_2 (paquete movie) | 0x128 | ×8192 B | 0x250000 |
| DISPFB2.FBP | 0xA0 / 0xE0 | ×8192 B | 0x140000 / 0x1C0000 |
| DISPFB1.FBP (movie env) | 0x118 | ×8192 B | 0x230000 (= film−64 líneas) |
| DISPFB1.FBP (boot stale) | 0x000 (raw 0x1400, FBW=10) | ×8192 B | 0x000000 |
| ZBUF (fade) | 0xE0 | ×8192 B | 0x1C0000 (zmask=1) |

PS2Recomp usa las mismas conversiones (decodeDisplayFrame FBP×pages;
CopyFrameToHostRgba `fbp*8192`); verificado en fuente.

## 7. Divergencia observada y explicación exacta de lo visible

**HECHO (180/180 muestras RUN_043): PMODE ∈ {0xff24, 0xff26} — EN1
nunca se enciende durante toda la ventana de película.** El env guest
aún tenía 0xff26 al primer GetPicture (dump RUN_029) y nunca se observó
0xff27. Por tanto la superficie de película 0x250000 — que SÍ recibe la
copia por frame (fragmentos visibles, abajo) — **no es seleccionada por
ningún circuito en todo RUN_043**.

Lo que se ve en pantalla es un accidente de solapamiento:

- Frames con DISPFB2=0xA0: se muestra el **upload crudo del slot 1**
  (0x140000 ≡ framebuffer A0) — película completa top-aligned, peleada
  con el sprite de fade negro que la borra cada frame alterno.
- Frames con DISPFB2=0xE0: 0x250000 cae DENTRO del rango de E0
  (0x1C0000–0x2A0000, fila 288) → se ven **bandas de la película** en la
  parte baja + negro — exactamente las bandas/duplicados/fantasmas de
  las capturas (visual_098905) y el histórico «warning duplicado» de
  P3.13.1 (upload-en-A0 + fragmento-en-E0 = dos copias).
- El corte vertical y el ghosting de visual_099453/074062 corresponden a
  esta mezcla alternante, no a un defecto de transferencia: el frame de
  referencia decodificado del ES (ffmpeg sobre
  `demuxed_video_expected.es`) confirma que el contenido en sí es
  correcto (los 5 párrafos del warning existen en la fuente; la captura
  muestra la mezcla parcial).

La copia MoveImage se ejecuta (evidencia: fragmentos en el rango E0), es
decir **DMA desde scratchpad + TRXDIR=2 local→local funcionan** en el
runtime (PerformLocalToLocalTransfer, completo, sin truncado).

## 8-12. Draw de película / doble buffer / display / presentación / renderer

- **Draw texturizado de película: NO EXISTE en DMC** — mecanismo =
  MoveImage2. Los 105 draws de la ventana son 1 sprite de fade
  (512×448, tme=0, rgba=0, prim=6, 2 vértices) por frame, alternando
  FRAME a0/e0 con el swap normal — coherente con el diseño.
- **Doble buffer**: envs 0x740C30 (which=0→{PMODE ff26, DISPFB2 0x10E0},
  which=1→{…,0x10A0}) construidos en `Main_init`
  (`MainGsSetDefDBuffDc`, único caller real) y ajustados por
  `MainSetPalMovieEnv`. El RECOMP los aplica fielmente.
- **Display**: DISPFB2 decodificado {FBP A0/E0, FBW 8, PSM 0, DBX/DBY 0};
  DISPLAY2=0x1bf1ff0004027c → DX=636, DY=64, MAGH=0, MAGV=0, DW=511,
  DH=447 → 512×448 1:1 ✓ PAL. DISPFB1 privilegiado quedó en el valor de
  boot (raw 0x1400: FBP=0, FBW=10) porque el swap solo escribe DISPFB1 si
  env-PMODE bit0 (EN1) — coherente.
- **PMODE 0xff26** = EN1=0, EN2=1, CRTMD=1, MMOD=1, AMOD=0, SLBG=0,
  ALP=0xFF; 0xff24 (7 muestras iniciales) = ambos circuitos apagados →
  los 7 PRESENT vacíos ✓.
- **SMODE2=1** = INT=1, FFMD=0 (FIELD): la presentación host hace bob
  (duplica líneas del campo por vsyncTick, `applyFieldPresentation`) —
  aproximación razonable; reduce detalle vertical y añade shimmer; NO es
  la divergencia primaria.
- **Presentación (Fase 15)**: `preferred=0` significa
  `m_hasPreferredDisplaySource=false` (ninguna heurística activa). Con
  pmode ff26 → solo circuito 2 válido → `copySource(displayFrame2)` lee
  EXACTAMENTE el framebuffer que DISPFB2 designa (base FBP×8192, origen
  DBX/DBY, fbw). El fallback `countNonBlackPixels` solo aplica si
  fbp==0 — no se activó. **La selección de fuente NO es la divergencia:
  sigue fielmente al guest.**
- **Fase 16**: PRESENT bytes=1310720 = canvas fijo 640×512×4
  (kHostFrameWidth/Height); wh=512,448 es el rect de contenido;
  `copyLatchedHostPresentationFrame` extrae con stride 640. Explicado,
  sin bug.
- **Renderer handoff**: coherente; nada implica a OpenGL.

## 13. Tabla de capas

| Layer | Evidence | Status | Confidence | Main caveat |
|---|---|---|---|---|
| HLE guest pixel buffer | R,G,B,A=0x80; strips 16×448; 0x79D680 | MATCH | Alta | layout strip = contrato de la cadena ✓ |
| GIF DMA tags | 256 kicks × 66 tags, QW crudos | MATCH | Alta | — |
| BITBLTBUF | DBP 0/0x1400, DBW 8, DPSM 0 | MATCH | Alta | — |
| TRXPOS/TRXREG/TRXDIR | 16×448, X=16k, dir 0 | MATCH | Alta | — |
| GS local-memory transfer | GSBUF 384/384 mismatch=0 | MATCH | Alta | autoconsistencia, no equivalencia hardware |
| PSMCT32 byte contract | R bits0-7, A=0x80 | MATCH | Alta | swizzle hardware-exacto UNKNOWN (no causal) |
| movie TEX0 source | N/A — DMC no usa textura para el film | PARTIAL | Alta | mecanismo real = MoveImage2 local→local |
| movie primitive/geometry | sprite = fade, no film | MATCH | Alta | — |
| FRAME destination | fade→a0/e0; FRAME_2=0x250000 p/ subtítulos | MATCH | Media | orden intra-frame no instrumentado |
| original double-buffer state | envs 0x740C30 = observado | MATCH | Alta | — |
| guest DISPFB state | DISPFB2 {A0,E0} fiel al env | MATCH | Alta | — |
| GS HLE display state | sceGs* HLE NO usados por DMC | MATCH | Alta | pseudo-regs 0x59-0x5C no intervienen |
| PMODE circuit selection | EN1 nunca 1 (180/180); film surface jamás escaneada | **MISMATCH** | Alta (RECOMP) / Media (contrato original) | temporización EN1 original sin medir (PCSX2) |
| FIELD/interlace | SMODE2=1, bob host | PARTIAL | Media | aproximación; secundario |
| runtime presentation source selection | sigue DISPFB2; preferred=0 | MATCH | Alta | — |
| CopyFrameToHostRgba | canvas 640×512, fuente correcta | MATCH | Alta | — |
| renderer handoff | frame coherente entregado | MATCH | Media | fuera de alcance |

## 14. Conclusión causal (Fase 18)

A. píxeles correctos → SÍ (HECHO). B. GIF DMA los consume → SÍ (HECHO).
C. llegan a la superficie GS programada → SÍ (HECHO, con caveat de
autoconsistencia). D. muestreo TEX0 → N/A (MoveImage; se ejecuta). E.
draw al FRAME previsto → SÍ (fade; film via copia). F. **selección
DISPFB/PMODE del framebuffer del film → NO: EN1=0 permanente; 0x250000
nunca escaneado. PRIMER NO de la cadena.** G. presentación elige el
mismo framebuffer que DISPFB → SÍ. H. renderer recibe petición coherente
→ SÍ.

- **LAST_PROVEN_COMMON_NODE**: la copia por frame de la película
  completa a VRAM (subida 32×16×448 byte-exacta + MoveImage2 hacia
  0x250000 ejecutándose), con el estado de display reproduciendo
  fielmente los envs del guest.
- **FIRST_PROVEN_DIVERGENT_NODE**: selección de circuito de display
  durante la película — `PMODE.EN1` jamás se activa (180/180), de modo
  que la superficie de película (0x250000 vía DISPFB1=0x1118) nunca
  participa del scanout; lo visible es el solapamiento accidental
  slot1≡A0 + fragmento 0x250000∈E0. Nota de rigor: es HECHO que el
  RECOMP nunca la selecciona y que ese estado no puede mostrar el film
  correctamente; que el ORIGINAL activa EN1 en esta misma ventana es
  INFERENCIA forzada por la arquitectura (sin EN1 el hardware tampoco
  podría mostrarlo), pendiente de una medición PCSX2.
- **FIRST_UNPROVEN_NODE**: el mecanismo/temporización que enciende EN1 en
  el original (¿`MainSetRect_movie` desde el script de sala al arrancar
  la película? ¿otra vía?) y por qué esa vía no corre (o no corre aún)
  en RECOMP.

## 15. Respuestas a las 29 preguntas

1. Solo autoconsistencia interna (más supervivencia de bytes extremo a
   extremo). 2. Sí, PSMCT32 en fuente y destino (QW crudo). 3. Sí
   (R,G,B,A orden GS). 4. Sí (A=0x80=1.0). 5. Sí (12 películas × 32
   strips, X completo). 6. Sí (layout strip del HLE = contrato de la
   cadena). 7. Sí, tabla §6. 8. 0x000000 y 0x140000. 9. Ninguna de las
   dos cosas: es una superficie de STAGING consumida por MoveImage
   (slot1 solapa físicamente el framebuffer A0). 10. N/A — no hay TEX0
   de película; la copia sí referencia los píxeles frescos. 11. El draw
   identificado es el FADE, no la película (corrección al modelo del
   prompt). 12. Sí para el fade (512×448). 13. El fade → a0/e0; los
   subtítulos → FRAME_2=0x250000; el film → 0x250000 vía MoveImage.
   14. a0/e0 sí son el doble buffer; 0x250000 es la superficie dedicada
   de película. 15. 0x140000 y 0x1C0000. 16. `MainGsSwapDBuffDc_movie`
   NO se usa (gate FF=0 por diseño de la tabla de descriptores; código
   muerto aquí) — la alternancia real la hace el swap normal. 17. N/A:
   DMC no llama a los sceGs de display; el HLE GS no interviene (los
   pseudo-registros 0x59-0x5C existen pero no se usan en este camino).
   18. Circuito 2 (EN2) únicamente; EN1 nunca. 19. DISPFB2: A0/E0
   alternando. 20. NO — apunta al buffer del fade, no al del film: esa
   es la divergencia. 21. Aproximada (bob por campo, doc §8-12);
   secundaria. 22. `preferred=0` = ninguna fuente preferida registrada;
   sin heurística. 23. Usa el DISPFB activo decodificado (fiel). 24. En
   teoría el fallback existe solo para fbp==0; aquí no actuó; no eligió
   VRAM stale. 25. Canvas host fijo 640×512×4; wh es el rect de
   contenido. 26. Sí — lee exactamente el framebuffer que DMC seleccionó.
   27. Resuelta: no era discrepancia de unidades sino dos vías distintas
   (staging vs framebuffers) + una superficie tercera (0x250000); el
   solapamiento slot1≡A0 es real y explica lo visible. 28. Sí: la
   no-activación de EN1/circuito-1 (estado de display guest) antes del
   handoff. 29. No — ninguna evidencia implica a OpenGL.

## 16. UNKNOWNs restantes y próxima investigación

- Temporización EN1 original (medición PCSX2 mínima, UNA sola: breakpoints
  en `0x15CAF0` (MainSetRect_movie) y `0x15CB30` (Reset), y lectura
  periódica de PMODE (0x12000000)/DISPFB1 (0x12000070) durante la ventana
  de película del original; opcionalmente breakpoint en `0x4B0740`
  (R002_movie_sub_title_disp_01) para identificar el caller real).
- Por qué la vía que activa EN1 no corre en RECOMP (¿script de sala 002 no
  alcanzado en el boot?, ¿cue de subtítulos posterior a la ventana?).
- Equivalencia hardware del swizzle PSMCT32 (no causal aquí).
- Detalle fino del corte/bandas por captura (mezcla de fases A0/E0; no
  requiere más evidencia para el veredicto).
- El descarte del par `MainGsSwapDBuffDc_movie`/`Main_Get_Vsync` field
  flip vale para ESTA tabla de películas (todas flags 0x8000); si algún
  build/región usa otra tabla, revisar.

Herramientas: `reconstruct.py` REUSABLE; `instrument.py`/`build.ps1`
TEMPORARY (conservadas untracked; el patch que generaban está revertido).

Checkpoint: la evidencia nueva (arquitectura MoveImage/EN1, retractación
del camino swap_movie, explicación exacta de los artefactos visibles) es
sustancial y debería quedar en historia → DOCS_ONLY.
