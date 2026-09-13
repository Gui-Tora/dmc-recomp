# P4.1.6 — Fable: oracle del contrato ORIGINAL de sceGsSetDefDBuffDc

Recuperación del contrato original SIN especulación: desensamblado
directo de los cuerpos MIPS reales del SDK linkado dentro de
`original/SLES_503.58` (los trampolines HLE existen solo en el árbol
generado; el ELF conserva las implementaciones), verificado
cruzadamente contra los argumentos reales de `Main_init` y contra todos
los dumps RAM y mediciones vivas históricas. Sin builds, sin corridas,
sin cambios de fuente. Vendor `19911ce` intacto; main `5c0881b`
intacto; producción sigue en P4.1.3 (`fbp1=0u`).

## 1. Fase 1 — Argumentos exactos de las dos llamadas (HECHO, ELF crudo, Main_init 0x15BAE0-0x15BB2C)

| Argumento | UI call (0x15BB00) | Movie call (0x15BB28) |
|---|---:|---:|
| db address | 0x740770 (s0+0xD0) | 0x740C30 (s0+0x590) |
| psm (a1) | 0 (PSMCT32) | 0 |
| width (a2) | 0x200 (512) | 0x200 (512) |
| height (a3) | **0xE0 (224)** | **0x1C0 (448)** |
| ztest (t0) | **2** | **0** |
| zpsm (t1) | 0x3A (PSMZ16S) | 0x3A |
| flags de modo del wrapper (t2/t3/stack0) | 1 / 1 / 0 | 1 / 0 / 1 |

Difieren: height, **ztest**, y los flags de modo (que gobiernan el
gparam→SMODE2: UI=3, movie=1). Validación cruzada perfecta con los
dumps: UI ZBUF raw `0x0A000070` (ZBP=0x70, **ZMSK=0** ⇐ ztest=2) y
movie ZBUF `0x10A0000E0` (ZBP=0xE0, **ZMSK=1** ⇐ ztest=0).

## 2. Fases 2/7 — Implementación original recuperada (HECHO, bytes del ELF)

Símbolos del `.symtab` real: `sceGsSetDefDBuffDc@0x101B48` (740 B),
`sceGsSetDefDispEnv@0x1002E8`, `sceGsSetDefDrawEnv@0x100620`,
`sceGsSetDefDrawEnv2@0x1018E0`, `sceGszbufaddr@0x100558`,
`sceGsSetDefClear@0x100808`. Cuerpo de `sceGsSetDefDBuffDc`:

```
disp[0]  = SetDefDispEnv(db+0x00, psm, w, h, dx=0, dy=0)
disp[1]  = SetDefDispEnv(db+0x28, psm, w, h, 0, 0)      ; IDÉNTICA
draw01   = SetDefDrawEnv (db+0x60,  psm, w, h, ztest, zpsm)
draw02   = SetDefDrawEnv2(db+0xE0,  psm, w, h, ztest, zpsm)
draw11   = SetDefDrawEnv (db+0x1D0, psm, w, h, ztest, zpsm)   ; MISMOS ARGS
draw12   = SetDefDrawEnv2(db+0x250, psm, w, h, ztest, zpsm)   ; MISMOS ARGS
clear0/1 = SetDefClear(...) si clear≠0 (offsets centrados 0x800−w/2, 0x800−h/2; sin FBP)
```

**No existe NINGÚN argumento ni cálculo de segundo framebuffer: los
cuatro draw-envs y los dos disp-envs se siembran con llamadas
idénticas.** Dentro de los helpers:

- `SetDefDrawEnv` construye FRAME = `(fbw&0x3F)<<16 | (psm&0xF)<<24` —
  **los bits 0-8 (FBP) jamás se escriben → FRAME.FBP = 0 SIEMPRE**
  (0x100638-0x100694); ZBUF = `zbufaddr(psm,w,h)<<32… | zpsm<<24 |
  (ztest==0 ? ZMSK : 0)` — **zbufAddr va EXCLUSIVAMENTE al ZBUF**.
- `SetDefDispEnv` escribe **PMODE = 0x66** (EN1=0, EN2=1, CRT=1,
  MMOD=1, **AMOD=1**, ALP=0), SMODE2 = f(gparam) ∈ {1,2,3}, y
  DISPFB = `(psm&0xF)<<15 | fbw<<9` — **FBP = 0 SIEMPRE**.
- `sceGszbufaddr` original == fórmula del HLE (bloques ×2 según
  gparam) — la aritmética de P4.0.2/P4.1.5 era correcta.

## 3. Fase 4 — Parches del juego (HECHO, por desensamblado + eliminación)

- **Movie (0x740C30)**: inmediatamente tras la 2ª llamada
  (0x15BB30-…), `Main_init` parchea `disp1.dispfb→0xA0` y los FRAME de
  slot0 → 0xA0 (ya probado en P4.0.2/P4.1.2). `MainSetPalMovieEnv`
  después fuerza EN1=0/MMOD=1/ALP=0xFF (→ PMODE 0x?66 final = 0xFF66
  con AMOD=1 preservado del default).
- **UI (0x740770)**: **NINGÚN parche.** El código entre las dos
  llamadas solo prepara argumentos; y por eliminación dinámica: los
  dumps RECOMP (que ejecutan el MISMO guest) muestran el struct UI
  exactamente con los defaults puros del HLE — si el juego lo
  parcheara, se vería.

Clasificación por campo: movie disp1/slot0-FRAMEs = GAME_PATCHED; todo
lo demás (incluido TODO el struct UI) = SDK_INITIAL/UNCHANGED.

## 4. Tabla comparativa (Fase 8)

| Contract | UI slot1 | Movie slot1 | UI result | Movie result |
|---|---:|---:|---|---|
| old HLE zbufAddr | 0x70 | 0xE0 | visible/flicker | corrupción inferior |
| P4.1.3 zero | 0x00 | 0x00 | ~99% negro | correcta |
| P4.1.5 frameSize | 0x38 | 0x70 | mejorada | corrupción superior |
| **ORIGINAL (ELF)** | **0x00** | **0x00** | correcta (hardware) | correcta (con parches del juego) |

**El contrato original ES `fbp1 = 0` — P4.1.3 es byte-fiel al SDK.**
Los tres experimentos quedan explicados: `zbufAddr` y `frameSize`
"funcionaban" a medias porque fabricaban un doble buffer ESPURIO que
casualmente convenía al PRESENTADOR HOST; sus efectos dañinos eran el
aliasing de páginas (0xE0 sobre A0 filas 256+; 0x70 sobre A0 filas
0-255) del mismo mecanismo geométrico probado en P4.1.2.

## 5. La consecuencia decisiva (re-lectura de P4.1.4)

Con el contrato original = todo FBP0 y el struct UI sin parches:
**la UI original de DMC dibuja Y muestra el MISMO buffer FBP0
(single-buffer real, con clear por frame)**. En hardware eso es normal
(scanout continuo; clear+redraw dentro del ciclo). El negro pre-PSS del
RECOMP NO es, por tanto, una divergencia de contrato guest: es la
**incompatibilidad del modelo de presentación host** (snapshot
instantáneo asíncrono de VRAM que cae en la ventana clear→pre-redraw,
~99% del muestreo) **con el single-buffer legítimo del juego**. El
FIRST_PROVEN_DIVERGENT de P4.1.4 se re-etiqueta: la paridad
draw/display post-P4.1.3 ES la original; el divergente es el
presentador (NARROWED, no retractado: la causalidad A/B de P4.1.4 sigue
siendo HECHO — el cambio de fbp1 alteraba la visibilidad — pero el
término correcto no era «contrato de UI incompleto» sino «presentador
incompatible con el contrato verdadero»).

Hallazgos de paridad SDK adicionales (no causales del negro, para el
ledger): el default PMODE del HLE (`makePmode(1,1,0,0,0,0x80)` →
EN1=1, AMOD=0, ALP=0x80) difiere del original **0x66** (EN1=0,
**AMOD=1**, ALP=0) — esto resuelve de raíz el mismatch AMOD arrastrado
desde P4.0.1 (era el default del propio `sceGsSetDefDispEnv`) y añade
EN1/ALP como deltas menores hoy enmascarados por `MainSetPalMovieEnv`.

## 6. Fases 5/6 y oracle vivo

- Vivo histórico (P4.0.1, película): PMODE=0xFF66/DISPFB {0x1000,
  0x10A0} — coincide exactamente con contrato+parches (0x66 base + ALP
  0xFF del juego; FBP 0/A0). El AMOD=1 vivo queda explicado.
- Vivo de Memory Card/Language: NO medido en esta sesión (el MCP de
  PCSX2 quedó caído tras el intento de P4.1.4 y no se recuperó) —
  `ORIGINAL_VALUES_OBSERVED_LIVE: NO`. Innecesario para el contrato
  (bytes del ELF + validación cruzada total con dumps), útil solo como
  confirmación futura del comportamiento single-buffer en pantalla.
- SMODE2: UI=3 (INT=1, FFMD=1) vs movie=1 (INT=1, FFMD=0) — decodificado;
  proviene del gparam fijado por los flags del wrapper ANTES de cada
  llamada; **no participa en el cálculo de FBP** (no hay tal cálculo).

## 7. Respuestas (1-28, condensadas)

1-3. Tabla §1; difieren height/ztest/flags-de-modo. 4-7. UI slot1
FRAME original = `0x80000` (FBP **0**); movie slot1 = `0x80000` (FBP
**0**) — inmediatamente tras la llamada SDK. 8. slot0 = también FBP 0
(los cuatro idénticos). 9. ZBUF: UI `0x0A000070` (ZBP 0x70, ZMSK=0);
movie `0x10A0000E0` (ZBP 0xE0, ZMSK=1). 10. Sí (FRAME_1==FRAME_2 por
construcción). 11. **NO** (UI sin parches). 12-13. Sí: disp1→0xA0 y
slot0-FRAMEs→0xA0 (Main_init, 0x15BB30+). 14. Ninguno. 15-16. FBP 0
(single-buffer; INFERENCIA por contrato+ausencia de parches; vivo no
medido). 17. Draws al buffer no mostrado del par parcheado {0,A0}.
18. {0x1000,0x10A0} (medido vivo, P4.0.1). 19-20. UI SMODE2=3 =
INT+FFMD(frame-read); movie=1 = INT+FIELD. 21. NO — no hay cálculo de
FBP que modular. 22. **SÍ — recuperada completa** (§2). 23. Porque 0 ES
el valor original y el movie tiene su doble buffer por parches del
juego. 24. frameSize=0x70 aliasa las páginas 0xA0-0xE0 = filas 0-255
de A0 (mismo mecanismo P4.1.2, invertido). 25. zbufAddr(224)=0x70
fabricaba un pseudo-doble-buffer que dejaba FBP0 estable la mitad de
los frames — amable con el presentador host. 26. SÍ: la fórmula
universal es la constante 0 (ya implementada). 27. No: la variación
operativa la ponen los PARCHES del juego, no la función. 28. El
contrato ya está implementado (P4.1.3); lo implementable ahora es
(a) el fix del PRESENTADOR para single-buffer (causal del negro) y
(b) paridad opcional del default PMODE (0x66: AMOD=1/EN1=0/ALP=0).

## Correction ledger

- P4.1.3: mecanismo E0→movie = HECHO (intacto); «0 como reemplazo
  universal» — **REHABILITADO: 0 ES el contrato SDK original** (la
  retractación de P4.1.5 sobre la universalidad del *valor* se
  sustituye por: el valor es correcto; lo incompatible era el
  presentador host).
- P4.1.5: aritmética frameSize=zbufAddr/2 = correcta (confirmada contra
  el cuerpo original de `sceGszbufaddr`); como contrato = REFUTADA
  (confirmado: el SDK no escribe ningún FBP).
- P4.1.4: causalidad A/B = HECHO intacto; interpretación «UI contract
  incomplete» → **NARROWED**: el contrato de P4.1.3 es el original; el
  divergente real es la presentación host frente al single-buffer
  legítimo de la UI.
- P4.0.1 «origen del AMOD divergente: UNKNOWN» → **RESUELTO**: default
  0x66 del `sceGsSetDefDispEnv` original vs `makePmode(...amod=0...)`
  del HLE.

## Estado final

Sin cambios de fuente ni vendor (`19911ce` limpio, verificado); main
`5c0881b`; este informe sin trackear. El PCSX2.ini quedó con los GDB
servers habilitados desde P4.1.4 (documentado; no es cambio de repo).
NO commit, NO push.
