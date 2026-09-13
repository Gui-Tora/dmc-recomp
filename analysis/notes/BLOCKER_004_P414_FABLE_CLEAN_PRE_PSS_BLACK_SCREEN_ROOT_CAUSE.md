# P4.1.4 — Fable: causa raíz de la pantalla negra pre-PSS con el binario limpio

Auditoría de regresión con A/B semántico controlado. Corridas nuevas:
RUN_071 (estado A limpio, reproducción) y RUN_072 (estado B limpio,
revert semántico temporal de P4.1.3 — restaurado al cierre). Evidencia
adicional primaria: dumps RAM de RUN_063/070/071/072 (los env structs se
escriben una vez en Main_init y sobreviven a los dumps del movie),
capturas de frontera de tecla y visual traces, informe P4.1.3 releído.
P4.2 permanece congelado e intacto (no se tocó nada MPEG).

## VEREDICTO EN UNA LÍNEA

**P4.1.3 es correcto para la película pero INCOMPLETO como contrato
universal: `fbp1=0` aplicado también al SEGUNDO DBuff del juego (el de
UI, 512×224 en `0x740770`) convirtió su doble buffer de dibujo
{0, 0x70} en {0, 0} sobre la ÚNICA superficie que la UI muestra (display
fijo FBP0), dejando el buffer visible en un ciclo permanente
clear→redraw cuya ventana negra domina el muestreo de presentación
(~99% de capturas negras). Ni 0 ni zbufAddr satisfacen ambos contratos:
el A/B demuestra que el valor SDK real es un tercero (INFERENCIA fuerte:
`frameSize = zbufAddr/2`).**

## 1. Fase 1 — Reproducción limpia (HECHO)

RUN_071, exe limpio `25c52c78…` (fuente = P4.1.3 exacto, reconstruido en
el cierre de P4.2): lógica normal (MC_CHECK 2.2s, inputs aceptados,
MOVIE_START 69.4s), pero TODAS las capturas de frontera de tecla
(input_01/02, before/after START/CROSS, ~10-14.5s = Memory Card y
Language) son **negro puro** (914 bytes), y el visual-trace pre-PSS es
131/133 negro. A 76.4s la película se ve **perfecta y completa** (los
fixes P4.1/P4.1.3 del movie intactos). `CLEAN_BLACK_REPRODUCED: YES`.

## 2. Fase 2 — Coherencia del build (HECHO)

Fuente vendor contiene `fbp1 = 0u` (GS.cpp:917); el dump de RUN_071
muestra en RAM guest los envs post-fix en AMBOS structs
(slot1 FRAME = `0x80000` en `0x740960` (UI) y `0x740E20` (movie)) y la
película sin half-frame — el exe limpio ES P4.1.3.
`CLEAN_BUILD_COHERENT: YES`.

## 3. El descubrimiento estructural: DOS DBuff structs, dos contratos

`Main_init` llama `MainGsSetDefDBuffDc` DOS veces (P4.0.2): struct
**UI/boot `0x740770` (512×224, SMODE2=3)** y struct **película
`0x740C30` (512×448, SMODE2=1)**. Estado real leído de dumps:

| Campo | RUN_063 (pre-P4.1.3) | RUN_070/071 (post) |
|---|---|---|
| UI disp0/disp1 dispfb | `0x1000` / `0x1000` (display SIEMPRE FBP0; ya post-P4.1) | igual |
| UI slot0 FRAME_1/2 | `0x80000` (0) | `0x80000` |
| **UI slot1 FRAME_1/2** | **`0x80070` (0x70)** | **`0x80000` (0)** ← único cambio |
| UI ZBUF ambos | `0x0A000070` (ZBP 0x70, ZMSK=0) | igual |
| movie disp | {0x1000, 0x10A0} | igual |
| movie slot0 FRAME | 0x800A0 | igual |
| movie slot1 FRAME | 0x800E0 | 0x80000 (el fix) |

Con la Validación B de P4.1.3 (26,950 de 26,976 draws de boot/menú a
`fbp=0x0` post-fix): la UI dibuja con el par del struct 0x740770.
Pre-P4.1.3 alternaba {0, 0x70}; post-fix TODO va a 0 — el buffer que se
muestra — junto con el clear de cada ciclo. El present del host
(asíncrono) muestrea el estado clear→pre-redraw casi siempre → negro.
Pre-P4.1.3, en los frames slot1 el buffer 0 quedaba ESTABLE con el
último frame completo → visible (con el flicker de bajo grado ya
conocido). Todo el fenómeno queda explicado con un solo mecanismo.

## 4. Fase 3 — A/B semántico controlado (HECHO, doble predicción cumplida)

Estados LIMPIOS, mismo método quirúrgico (solo GS.cpp compiló, relink
sin cpp), mismo harness/inputs/entorno (gates LAUNCH_ENV_CHECK):

| | A: `fbp1=0` (RUN_071, exe 25c52c78) | B: `fbp1=zbufAddr` (RUN_072, exe 92dc354c) |
|---|---|---|
| envs en RAM (dump) | UI slot1=0x80000; movie slot1=0x80000 | UI slot1=**0x80070**; movie slot1=**0x800E0** |
| Memory Card (input_01, ~10s) | **negro puro (914 B)** | **VISIBLE, nítida (25,243 B — byte-count idéntico a RUN_063)** |
| Language (before_CROSS, ~14s) | negro puro | visible (4.6 KB) |
| pre-PSS visual-trace | 131/133 negro | 117/124 negro (UI intermitente — el flicker conocido) |
| película | **perfecta** (5 párrafos, sin corte) | **half-frame DE VUELTA** (74.5s: 3.5 párrafos, corte ~fila 256) |

⇒ P4.1.3 es **causalmente responsable** de la regresión pre-PSS (A/B
limpio, una línea) Y su reverso restaura el bug del movie:
**ningún valor único ∈ {0, zbufAddr} cumple ambos contratos.**

## 5. Fase 7 — ¿La instrumentación GS enmascaraba? → NO (RETRACTADA la premisa)

Cuantificación por tamaños de captura: RUN_066 (P4.1.3 + diagnósticos
GS) tenía el pre-PSS **132/133 NEGRO** — su «Language Select visible»
era UNA captura de 133 (13 KB), estadísticamente indistinguible de las
2/133 semi-capturas de RUN_071 sin diagnósticos. La matriz real es
negro≈99% en ambos estados post-P4.1.3, con o sin instrumentación.
`GS_DIAGNOSTICS_MASK_BUG: NO` — la aparente diferencia RUN_066 vs
RUN_067 era azar de muestreo sobre una ventana visible de ~1%.

## 6. Fase 4 — Oracle PCSX2: NO OBTENIDO

Se intentó: PCSX2 lanzado vía MCP, pero el GDB server no estaba
habilitado; al configurarlo (`configure_gdb_settings` ✓ escribió
PCSX2.ini) el servidor MCP se desconectó y las herramientas dejaron de
estar disponibles en esta sesión. `ORIGINAL_DRAWENV_UI_CONTRACT =
UNKNOWN` / `ORIGINAL_UI_FRAME_CONTRACT_PROVEN: NO`. Direcciones exactas
para el oracle futuro (derivadas del layout ya triple-verificado):
UI slot1 FRAME_1 = `0x740960`; movie slot1 FRAME_1 = `0x740E20`; UI
disp0/disp1 dispfb = `0x740780`/`0x7407B8`, durante Memory Card.

## 7. Contrato real más probable (INFERENCIA fuerte, pendiente de oracle)

La fórmula de `sceGszbufaddr` = 2×(w/64)×ceil(h/32) páginas = **2× el
tamaño de UN framebuffer** — es la dirección del Z asumiendo el layout
clásico {buffer0=0, buffer1=frameSize, Z=2×frameSize}. El SDK real casi
con certeza usa **fbp1 = frameSize (= zbufAddr/2)** para el segundo
draw-env: 0x38 para 512×224 (par UI {0,0x38}, Z=0x70 ✓) y 0x70 para
512×448 (par {0,0x70}, Z=0xE0 ✓ — y 0x70-0xE0 NO toca A0 ⇒ el fix del
movie SE CONSERVA). Es el único valor único compatible con TODA la
evidencia (UI visible + movie sin half-frame + Z observado). El HLE
conflató fbp1:=zbufAddr (=Z), P4.1.3 lo conflató con 0.
Nota: zbufAddr(224)=0x70 coincidía «de casualidad» con un segundo buffer
utilizable para la UI — por eso la UI funcionó siempre hasta P4.1.3.

## 8. Fases 8-9 — Exoneraciones y control positivo

- `[CDMODULE] unsupported fno=2 mode=2`: presente exactamente 1× en
  TODAS las corridas (visibles y negras: RUN_063/066/067/070/071) —
  **EXONERADO** (`FNO2_MODE2_CAUSAL: NO`).
- ¿Por qué la película SÍ se ve?: usa el struct 448 con display
  ALTERNANDO {0, A0} (disp1 parcheado por Main_init) y los píxeles
  llegan por SUBIDA GIF directa al buffer mostrado; sus draws (fade) van
  al buffer NO mostrado ({A0, 0} cruzado — paridad correcta post-fix).
  La UI, en cambio, muestra FIJO FBP0 (ambos disp=0x1000, sin parche) y
  post-P4.1.3 dibuja+clearea esa misma superficie.
- preferredSource/FIELD: sin indicios de implicación (el mecanismo no
  los necesita; el fallback fbp==0 no rescata porque el instante
  muestreado ES negro).

## 9. Split obligatorio del veredicto P4.1.3

- **MOVIE_DRAWENV_FIX_VALID: YES** — reconfirmado por partida doble en
  este prompt (A: película perfecta; B: half-frame reaparece).
- **GLOBAL_DRAWENV_INITIALIZATION_VALID: NO** — `fbp1=0` universal
  rompe el contrato del DBuff de UI (regresión A/B probada).

## Correction ledger (P4.1.3)

- «`UI_FLICKER_IMPROVED: YES` (estructural)» → **RETRACTADO**: la
  eliminación de FRAME=0xE0 era correcta, pero el valor sustituto 0
  degradó la UI de «flicker» a «negro ~99%»; la validación estructural
  (0 draws a 0xE0) no cubría la paridad draw/display del struct de UI.
- La validación de P4.1.3 con corridas cortadas en `getPicture #10` y
  foco en película no detectó la regresión pre-PSS → gap de validación
  documentado (las capturas pre-PSS de sus propias corridas ya estaban
  ~99% negras).
- «RUN_066 muestra Language Select» (premisa de este prompt) →
  NARROWED: una única captura de 133; no evidencia de enmascaramiento.

## 10. Fix recomendado (NO implementado)

**P4.1.5 — SCEGSSETDEFDBUFFDC_DRAWENV_SECOND_BUFFER_FIX**: sustituir
`fbp1 = 0u` por el segundo framebuffer real
(`fbp1 = zbufAddr / 2` — o equivalente explícito
`(w/64)*ceil(h/32)` páginas), con: (a) gate opcional de oracle PCSX2
(leer `0x740960`/`0x740E20` en original durante Memory Card — decide
entre frameSize y cualquier otra constante); (b) validación doble
obligatoria: UI pre-PSS visible (input_01 con contenido) Y película sin
half-frame (secuencia visual del warning completa), en la MISMA corrida;
(c) sin tocar dispfb0/dispfb1 (P4.1 intacto — el A/B no lo implica).

## 11. Limpieza y estado final

Estado B revertido (checkout GS.cpp → `fbp1 = 0u` de producción =
P4.1.3 exacto); vendor `19911ce` limpio; rebuild+relink quirúrgico final
(solo GS.cpp, 0 generados). Sin commit, sin push. Main `5c0881b` + este
informe (y los previos esperados) sin trackear. RUN_071/072 preservadas.
Incidencia de sesión: el servidor MCP de PCSX2 quedó caído tras el
intento de oracle; el proceso PCSX2 lanzado ya no existía al cierre.
