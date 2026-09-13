# P4.1.2 — Fable: causa raíz del "upload parcial" en FBP 0x0A0 (auditoría adversarial)

Auditoría 100% read-only: sin instrumentación nueva, sin build, sin corrida
nueva, sin commit, sin push. Evidencia primaria: fuente vendor actual,
dumps RAM de RUN_061 (post-fix P4.1, camino síncrono probado activo),
logs/trazas dinámicas de RUN_043 (draws/kicks/presents reales), tablas de
cadenas GIF de P4.0, y los informes P4.1.1/R/B como contexto re-verificado.

Estado de entrada: main `c9ee65d` ✓; vendor `16ef7ab8…` limpio ✓ (antes y
después — nada se tocó). RUN_060-063 presentes.

## VEREDICTO EN UNA LÍNEA

**El "upload parcial" NO existe: la subida escribe las 448 filas
completas; las filas 256-447 del buffer A0 son BORRADAS después, cada
frame en que A0 se muestra, por el sprite de fade/UI rasterizado en un
FRAME env erróneo (FBP=0xE0) que la HLE `sceGsSetDefDBuffDc` siembra con
`zbufAddr` — la SEGUNDA instancia del mismo bug que P4.1 corrigió para
`disp[0].dispfb`.**

## 1. La cadena causal, pieza a pieza

### 1.1 Fuente (HECHO — GS.cpp:906,926-930, leído en este turno)

```cpp
const uint32_t fbp1 = zbufAddr;                                  // = 0xE0 (P4.0.2, exacto)
seedGsDrawEnv1(db.draw01, ..., /*fbp=*/0u,  ..., zbufAddr, ...); // slot0: FRAME=0
seedGsDrawEnv2(db.draw02, ..., 0u,  ...);
seedGsDrawEnv1(db.draw11, ..., /*fbp=*/fbp1, ..., zbufAddr, ...); // slot1: FRAME=0xE0  ← BUG
seedGsDrawEnv2(db.draw12, ..., fbp1, ...);
```

El comentario de P4.1 («fbp1/zbufAddr sigue usándose legítimamente en…
punteros de frame draw11/draw12») queda **RETRACTADO**: ese uso es la
causa raíz de este defecto.

### 1.2 Estado guest final (HECHO — dump RUN_061, offsets verificados)

Paquete de draw-env slot0 (`0x740C30+0x70`, aplicado cuando se muestra
FBP 0): giftag A+D ×16; **FRAME_1(0x4C)=0x800A0, FRAME_2(0x4D)=0x800A0**
(parcheados por `Main_init` a 0xA0 — los dos `sh` de `0x740CB0/0x740D30`
de P4.0.2 §4 son exactamente estos campos, +0x80 y +0x100). Paquete slot1
(`+0x1E0`, aplicado cuando se muestra A0): **FRAME_1=0x800E0,
FRAME_2=0x800E0** — el default HLE con zbufAddr, que el juego NUNCA
parchea (misma asimetría de parcheo que probó P4.0.2 para dispfb).
ZBUF(0x4E)=0x10A0000E0 en ambos → ZBP=0xE0, **ZMSK=1** (Z enmascarado).

### 1.3 Aritmética de páginas GS (HECHO — direccionamiento real, no aproximación lineal)

PSMCT32: página = 64×32 px = 8192 bytes; FRAME/DISPFB FBP en unidades de
página; FBW=8 → 8 páginas por fila-de-páginas (32 líneas). Buffer A0
(FBP 0xA0): la línea y ocupa la fila de páginas `0xA0 + (y/32)·8`. Para
y=256: `0xA0 + 8·8 = 0xE0`. **Un FRAME con FBP=0xE0 escribe, desde su
línea 0, exactamente las líneas 256-447 del buffer A0** (páginas
0xE0-0x10F; el sprite de 448 líneas sigue hasta la página 0x14F, más allá
de A0). El límite en la fila 256 no es una coincidencia numérica lineal:
es la geometría real de páginas del GS, válida también en hardware.

### 1.4 El borrador (HECHO dinámico — RUN_043)

Durante la película se dibuja UN sprite opaco negro 512×448 por frame
(prim=6, tme=0, abe=0, rgba=0,0,0,0, vértices (0,0)-(512,448) tras
XYOFFSET), alternando `frame=a0` (ctxt0) y `frame=e0` (ctxt1) — 105/105
draws observados. Con los envs del dump (§1.2), «frame=e0» ES el paquete
slot1: cada frame en que el guest muestra A0, el fade se rasteriza en
FBP 0xE0 → **borra en negro las líneas 256-447 del A0 mostrado, después
de la subida y antes del present** (orden verificado en el log
interleaved: kick → DISPFB change → DRAW → PRESENT, cada ciclo).

### 1.5 La subida SÍ escribe la mitad inferior (HECHO — refuta "nunca recibe contenido")

RUN_043 [P40:GSBUF]: 384/384 strips con `mismatch=0`, `copied=7168=total`
por strip, verificación por RELECTURA de VRAM inmediatamente tras cada
strip de 16×448 — **incluidas las filas 256-447** — para ambos DBP
(0x0000 y 0x1400). La cadena GIF es estática (P4.0, tablas byte a byte:
idéntica entre slots salvo el DBP del BITBLTBUF). El transfer completa
(direction=3), sin truncado NLOOP/QWC, sin reescritura de TRXDIR.

### 1.6 Por qué FBP 0 se muestra siempre completo (0/50)

En RECOMP nadie dibuja jamás con FRAME=0 (slot0 dibuja en 0xA0
parcheado; slot1 en 0xE0 bug) → el buffer 0 solo recibe las subidas de
película → siempre íntegro. En el original, por la misma simetría de
parcheo, slot1 debería dibujar en el default SDK — que la coherencia de
ping-pong (dibujar en el buffer NO mostrado = FBP 0) y el precedente
P4.1 (defaults=0) señalan como **FBP 0** — inofensivo y correcto.
INFERENCIA fuerte; medible en PCSX2 con una lectura (`0x740E20` =
`0x740C30+0x1F0`, valor FRAME_1 del paquete slot1 original).

## 2. Respuesta a la pregunta primaria

**C** — FBP 0x0A0 recibe la región completa y correcta, y es
sobrescrita después (cada frame de display A0) por el draw con FRAME
erróneo. (A/B/D/E refutadas: §1.5, §1.3, tablas abajo, y el hallazgo de
P4.1.1B reproduce el mismo patrón en 52/52 — no fue artefacto de
muestreo; solo su interpretación «nunca recibe» se retracta.)

**FIRST CAUSAL OPERATION**: `seedGsDrawEnv1/2(db.draw11/db.draw12, …,
fbp1=zbufAddr, …)` en `sceGsSetDefDBuffDc` (GS.cpp:929-930, con
`fbp1 = zbufAddr` en GS.cpp:906) — siembra FRAME=0xE0 en el draw-env del
slot 1 en vez del segundo framebuffer (0).

## 3. Hipótesis del prompt, una a una

- **H1 (transfer distinto)**: REFUTADA — comandos byte-idénticos salvo
  DBP (tabla 1).
- **H2 (terminación temprana)**: REFUTADA — copied==total, 384/384,
  direction=3.
- **H3 (aliasing)**: el "alias" real es el solapamiento entre
  superficies por páginas (FRAME 0xE0 ⊂ A0), idéntico en hardware; el
  addressing del backend reproduce la misma geometría (la relectura
  GSBUF y el negro exactamente en 256-447 lo confirman dinámicamente).
- **H4 (ZBUF E0)**: las señas numéricas eran correctas pero el mecanismo
  no es Z: **ZMSK=1 → cero escrituras Z** (dump + draws RUN_043).
  RETRACTADA como escritura Z; la coincidencia 0x1C0000 ES causal pero
  vía el valor FRAME contaminado por `zbufAddr`, no vía Z.
- **H5 (generaciones stale)**: en la ventana auditada la región inferior
  de A0 es **NEGRO PURO** (hash constante `c6ba2821652f0383`, 52/52 —
  P4.1.1B), no imaginería de generaciones viejas; la parte superior es
  la picture actual (hash-match exacto con boundary A/B). La percepción
  humana de «mezcla temporal» se explica por la alternancia rápida
  completo(FBP0)/medio-negro(A0) más, fuera del fade fullscreen, la
  posibilidad real de que draws parciales en FRAME 0xE0 dejen contenido
  viejo mezclado (mismo mecanismo, manifestación distinta).
- **H6 (orden upload/flip)**: orden probado por el log interleaved: la
  subida completa PRECEDE al draw destructivo del mismo ciclo; no hay
  present-antes-de-upload.
- **H7 (paridad)**: correlación 1:1 con FBP=0xA0 (52/52 vs 0/50,
  P4.1.1B); FIELD sin correlación (~50/50).
- **H8 (flicker UI general)**: mismo mecanismo demostrado
  estructuralmente — los MISMOS paquetes de env sirven a la UI: en los
  frames which=1 los draws de UI van a FBP 0xE0 en vez del buffer 0 → el
  buffer 0 mostrado alterna con contenido no actualizado → flicker de
  bajo grado. Consistente con la nota lateral de P4.1.1B (fase de menú:
  sustitución a `selFbp=0x70` con «solo bloques 0-3»). Manifestación UI
  no trazada dinámicamente en detalle: se marca YES con esa reserva.
- **H9 (retardo primer frame)**: latencia FIJA de arranque (descartar
  4-5 frames pre-init + re-decode síncrono desde el inicio del ES),
  idéntica en la segunda película por diseño; sin evidencia de backlog
  creciente (pacing estable ~8.9 pics/s en ventanas de 150+ pictures).
  NO snowball (INFERENCIA).

## Tabla 1 — comparación de transferencias

| Field | FBP 0x000 | FBP 0x0A0 | Same? |
|---|---:|---:|---|
| DBP | 0x0000 | 0x1400 | **difieren (por diseño)** |
| DBW | 8 | 8 | ✓ |
| DPSM | 0 (PSMCT32) | 0 | ✓ |
| DSAX | 0,16,…,496 | 0,16,…,496 | ✓ |
| DSAY | 0 | 0 | ✓ |
| RRW | 16 | 16 | ✓ |
| RRH | 448 | 448 | ✓ |
| IMAGE bytes | 32×28672 | 32×28672 | ✓ |
| strip count | 32 | 32 | ✓ |
| source range | 0x79D680+0xE0000 | idéntico | ✓ |
| copiedPixels final | 7168/strip (=total) | 7168/strip | ✓ |
| transfer completed | sí (direction=3) | sí | ✓ |

(Cadenas estáticas byte-idénticas salvo DBP — tablas P4.0 + QW crudos;
verificación dinámica GSBUF 384/384.)

## Tabla 2 — generaciones temporales (frame A0 malo, RUN_061 t≈301899955)

| Region | Contents match |
|---|---|
| rows 0-63 | picture 15 (hash exacto boundary A/B) — ACTUAL |
| 64-127 | picture 15 — ACTUAL |
| 128-191 | picture 15 — ACTUAL |
| 192-255 | picture 15 — ACTUAL |
| 256-319 | NEGRO PURO (fade en FRAME 0xE0) |
| 320-383 | NEGRO PURO |
| 384-447 | NEGRO PURO |

Intended = picture 15; displayed = picture 15 (sup.) + negro del draw del
propio ciclo (inf.); prior displayed (FBP0) = picture 14/16 completa.

## Tabla 3 — línea temporal de eventos (patrón por ciclo, RUN_043 líneas 1365-1427; invariante del guest)

| Seq | Tick | Gen | Event | FBP | Estado relevante |
|---|---:|---:|---|---:|---|
| 1 | — | N | decode complete (GetPicture éxito) | — | boundary A/B completos |
| 2 | — | N | guest 0x79D680 completo | — | hash-idéntico a A |
| 3 | — | N | kick GIF (66 tags, 919600 B) → upload COMPLETO | slot par/impar | GSBUF mismatch=0, filas 0-447 |
| 4 | t | N | DISPFB2 change (which flip) | 0x000↔0x0A0 | swap aplica env+paquete del slot |
| 5 | t | N | DRAW fade 512×448 negro | FRAME 0xA0 (which=0) / **0xE0 (which=1)** | which=1 → borra A0 filas 256-447 |
| 6 | t | N | PRESENT | = DISPFB2 | A0 → sup. actual + inf. negro |

## Preguntas requeridas (1-30)

1. SÍ (re-verificado). 2. SÍ. 3. SÍ (0/50). 4. SÍ al presentar (52/52) —
pero recibe TODO y se borra después. 5. SÍ, 1:1. 6. SÍ (solo DBP). 7. SÍ.
8. SÍ. 9. **NO — se escriben** (GSBUF releído tras cada strip). 10. SÍ.
11. El sprite de fade/UI (prim=6, 512×448, negro opaco) rasterizado con
FRAME=0xE0 del paquete slot1. 12. NEGRO puro. 13. NO (en la ventana
auditada). 14. N/A. 15. NO en la ventana; posible fuera de ella con draws
parciales (mismo mecanismo). 16. La «mezcla» observada = alternancia
completo/medio-negro + este mecanismo; mezcla de generaciones viejas NO
probada en la ventana. 17. NO (upload precede al present del ciclo). 18.
El upload de N+1 ocurre tras el present de N — sin solape probado. 19.
SÍ — páginas 0xE0-0x10F ⊂ A0 filas 256-447 (addressing real). 20. **NO**
— ZMSK=1; el daño es la escritura de COLOR del FRAME erróneo. 21. Causal
pero vía FRAME=zbufAddr, no accidental ni vía Z. 22. NO. 23. NO. 24/25.
Mismo mecanismo estructural (mismos env packets); manifestación UI no
trazada → YES con reserva. 26. Latencia fija. 27. NO (sin crecimiento
observado). 28. NO. 29. NO (temporalidad y mecanismo independientes).
30. `seedGsDrawEnv1/2(draw11/12, fbp1=zbufAddr)` — GS.cpp:906/929-930.

## Correction ledger

- P4.1.1B «FBP 0xA0 nunca recibe las filas 256-447» → **NARROWED**: las
  recibe completas en cada subida; se borran después, en el mismo ciclo
  de display, por el draw con FRAME=0xE0. El hallazgo posicional
  (divergencia en VRAM de A0, filas 256-447, 1:1 con el buffer) queda
  íntegro y confirmado.
- P4.1.1B «asimetría de geometría de la subida GIF del slot 0xA0»
  (hipótesis de Fase 5) → RETRACTADA: la subida es simétrica y completa.
- P4.1 «zbufAddr sigue usándose legítimamente en draw11/draw12» →
  RETRACTADO: es la causa raíz del residual.
- P4.1F «candidato #1: coreografía clear/upload/flip» → NARROWED: la
  coreografía es la prevista por el guest; lo roto es el DESTINO del
  clear (FRAME env), no el orden.

## Fix recomendado (NO implementado aquí)

**P4.1.3 — SCEGSSETDEFDBUFFDC_DRAWENV_FRAME_FIX**: sembrar
`db.draw11/db.draw12` con FRAME FBP=0 (literal, como ya hace
`draw01/02`), dejando `zbufAddr` SOLO para los campos ZBUF; opcional
gate de una lectura PCSX2 (`0x740E20`, FRAME_1 del paquete slot1
original) para promover a HECHO el default 0 antes de aplicar;
validación con el mismo trace de P4.1.1B (esperado: bloques 4-6 de A0
no-negros 52/52) + verificación visual de la UI (flicker de menú
debería reducirse con el mismo fix).

Git al cierre: main `c9ee65d` + este informe sin trackear (más los 4
informes previos esperados); vendor limpio `16ef7ab8…` (sin tocar en
todo el prompt). NO build, NO corrida, NO instrumentación, NO commit,
NO push.
