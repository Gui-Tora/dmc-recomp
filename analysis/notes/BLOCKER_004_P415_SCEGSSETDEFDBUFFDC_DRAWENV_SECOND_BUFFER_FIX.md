# P4.1.5 — SCEGSSETDEFDBUFFDC_DRAWENV_SECOND_BUFFER_FIX

Implementación y validación del candidato de P4.1.4 (`fbp1 = frameSize =
zbufAddr/2`) como corrección universal del contrato de doble buffer de
draw de `sceGsSetDefDBuffDc`. **Resultado: candidato REFUTADO
parcialmente — corrige la UI pero introduce un defecto NUEVO e inverso
en la película.** No se realiza commit. El fix P4.1.3 (`fbp1=0u`)
permanece como el estado de producción vigente, sin modificar.

## Correction ledger (obligatorio, preservado)

**P4.1.3**:
- HECHO: `FRAME=0xE0` causaba la destrucción de la región inferior de
  la película (filas 256-447 de `FBP=0xA0`).
- HECHO: sustituir `0xE0` por `0` eliminó esa corrupción.
- RETRACTADO: que `fbp1=0` sea el valor universal correcto para el
  segundo `FRAME` de `sceGsSetDefDBuffDc` — es correcto solo para el
  DBuff de película (cuyo display alterna), no para el de UI (cuyo
  display es fijo).

**P4.1.4**:
- HECHO: `fbp1=0` causa la regresión de pantalla negra pre-PSS
  (Memory Card/Language Select).
- HECHO: `fbp1=zbufAddr` restaura la UI pero restaura también el
  half-frame de película.

**P4.1.5 (este prompt)**:
- INFERENCIA FUERTE promovida a HECHO por derivación aritmética directa
  (no por oracle): `zbufAddr` es exactamente 2× el tamaño de UN
  framebuffer, verificado a mano contra la fórmula real de
  `sceGszbufaddr` (`width_blocks*height_blocks`, doblado por el branch
  de `gparam_val`) para AMBOS structs: UI 512×224 → frameSize=0x38,
  zbufAddr=0x70; película 512×448 → frameSize=0x70, zbufAddr=0xE0.
  Confirmado dinámicamente 1/1: `fbp1=zbufAddr/2` produce exactamente
  `UI slot1 FRAME=0x38` y `movie slot1 FRAME=0x70` en RAM.
- HECHO (nuevo, esta corrida): la UI SÍ se recupera — Memory Card y
  Language Select confirmados visibles (byte-count 25243 idéntico al
  baseline pre-P4.1.3 para Memory Card; captura directa confirma
  Language Select legible).
- HECHO (nuevo, esta corrida): **la película desarrolla un defecto
  NUEVO** — `FBP=0xA0` pierde ahora sus filas 0-255 (bloques 0-3) en
  negro puro, 49/49 muestras, mientras las filas 256-447 (antes rotas)
  permanecen íntegras. Confirmado visualmente: solo `ATTENZIONE`/
  `ADVERTENCIA` (párrafos inferiores) visibles, mitad superior negra —
  el defecto exactamente INVERTIDO respecto al que P4.1.3 corrigió.
- RETRACTADO: que `fbp1=frameSize` sea el contrato universal correcto
  sin reservas — es geométricamente incompatible con el mismo mecanismo
  de aliasing de páginas GS que causó el bug original.

## Estado de entrada (verificado)

Main HEAD `5c0881b` ✓, sin dirt ajeno (los 6 informes esperados sin
trackear, incluidos P4.1.4 y P4.2 nuevos). Vendor HEAD
`19911ce35fb8f2029853f25612b1cc420a8c74bb` ✓, limpio ✓. Fuente de
producción confirmada `fbp1 = 0u` antes de tocar nada.

## Lectura obligatoria

`BLOCKER_004_P414_FABLE_CLEAN_PRE_PSS_BLACK_SCREEN_ROOT_CAUSE.md` leído
en full — resumen y hallazgos reproducidos en el ledger arriba.
`BLOCKER_004_P413_SCEGSSETDEFDBUFFDC_DRAWENV_FRAME_FIX.md` y
`BLOCKER_004_P412_FABLE_FBPA0_PARTIAL_UPLOAD_ROOT_CAUSE_AUDIT.md`
re-verificados como contexto (ya en memoria completa de esta sesión).

## Oráculo PCSX2 — NO OBTENIDO (igual que P4.1.4)

`ToolSearch` no encontró ninguna herramienta PCSX2/MCP disponible en
esta sesión. Per instrucción explícita del prompt, esto NO bloquea la
implementación. `ORIGINAL_ORACLE: NOT_OBTAINED`. La fórmula
`frameSize=zbufAddr/2` se sostiene como **INFERENCIA FUERTE** respecto
a si el SDK original realmente usa este valor — pero ahora sabemos,
por evidencia dinámica propia, que aunque la fórmula sea correcta en
abstracto (coincide exactamente con la aritmética interna de
`sceGszbufaddr`), **aplicarla ciegamente a AMBOS structs (UI y
película) no es la respuesta completa**, porque el mecanismo de
aliasing de páginas GS que rompió el `FRAME=0xE0` original también
rompe a `FRAME=0x70` para el struct de película específicamente.

## Verificación aritmética previa a la implementación (HECHO, estático)

`sceGszbufaddr` (`GS.cpp`, sin cambios): `product =
width_blocks*height_blocks`; `width_blocks=(w+63)>>6`;
`height_blocks=(h+31)>>5` (rama `param_1&2==0`, aplicable aquí);
doblado ×2 si `gparam_val & 0xFFFF0000FFFF != 1` (rama activa en esta
build de DMC, ya establecido en P4.0.2).

| | UI (512×224) | Película (512×448) |
|---|---|---|
| width_blocks | 8 | 8 |
| height_blocks | 7 | 14 |
| frameSize (=product) | `0x38` | `0x70` |
| zbufAddr (=2×product) | `0x70` | `0xE0` |

Coincide exactamente con los valores ya observados dinámicamente en
P4.1.4 y re-confirmados aquí.

## Implementación

```diff
-        const uint32_t fbp1 = 0u;
+        const uint32_t fbp1 = zbufAddr / 2u;
```

(con comentario explicativo de 15 líneas). `zbufAddr` en sí, `ZBUF`, y
el fix `dispfb0`/`dispfb1` de P4.1 **no se tocaron**. Build quirúrgico
(1 `CL.exe` = `GS.cpp` solo para el fix; luego `MPEG.cpp`+
`gs_cpu_backend.cpp` para la instrumentación de validación), relink
0 `CL.exe` inesperados en cada paso.

## Entorno de validación (gate obligatorio, satisfecho)

`RUN_073` (`exe_sha256=5d4b649e5c60f1ce468d5d96871696b0ec5e4c7998bf669f17f4ddf1decd517b`,
`result=SUCCESS`): `[P314:GP]`=24, `[P3142:ring]`=23, `[GP:H]`=0 —
camino síncrono confirmado activo ANTES de interpretar cualquier
evidencia.

## Validación A — Estado guest del draw-env (HECHO, dinámico)

| Campo | Dirección | Valor observado |
|---|---|---|
| UI slot1 `FRAME_1` | `0x740960` | `0x00080038` → FBP=**0x38** ✓ |
| UI `ZBUF` | `0x740970` | `0x0a0000070` → ZBP=0x70, ZMSK=0 (sin cambio) |
| movie slot0 `FRAME_1` | `0x740CB0` | `0x000800a0` → FBP=0xA0 (sin cambio) |
| movie slot1 `FRAME_1` | `0x740E20` | `0x00080070` → FBP=**0x70** ✓ |
| movie `ZBUF` (ambos slots) | `0x740CC0`/`0x740E30` | `0x10a0000e0` → ZBP=0xE0, ZMSK=1 (sin cambio) |

Los valores predichos por la fórmula (`0x38` UI, `0x70` película) se
observan exactos en RAM. `ZBUF`/`ZMSK` de ambos structs permanecen
intactos, como exigía la restricción de "no tocar".

## Validación B — UI pre-PSS (HECHO, mandatory, con reserva de flicker)

- **Memory Card** (`input_01.png`): visible, texto completo y nítido,
  **25243 bytes — byte-count idéntico** al baseline conocido-bueno
  pre-P4.1.3 (RUN_063, ya documentado en P4.1.4).
- **Language Select** (`visual_012516ms.png`, ~12.5s): visible, texto
  completo y legible ("LANGUAGE SELECT", 5 idiomas).
- **UI_BLACK_RATIO cuantificado** (ventana 0-20s, 35 capturas
  visual-trace): **23/35 = 65.7% negro** — mejora sustancial frente al
  ~99% de P4.1.3 (131-132/133), pero **NO estabilidad completa**: hay
  flicker real y frecuente (alternancia 25243/914 bytes en la ventana
  de Memory Card). `UI_VISIBILITY_FIXED: YES` (ambas pantallas
  requeridas SÍ se ven, en algún punto de la secuencia); `UI_FLICKER_FIXED: NO`
  (el residual de flicker no es menor, es sustancial).

## Validación C — Destinos de draw dinámicos (HECHO)

| FBP | Draws observados |
|---|---|
| `0x0` | 16275 |
| `0x38` (UI, nuevo) | 16474 |
| `0xa0` (película) | 29 |
| `0x70` (película, nuevo) | 28 |
| `0xe0` | **0** |

Par UI confirmado `{0, 0x38}` (antes `{0, 0}` con P4.1.3, `{0, 0x70=zbufAddr}`
con el HLE original) — exactamente como predecía la hipótesis. Par
película `{0xa0, 0x70}` (antes `{0xa0, 0}` con P4.1.3). **0 draws a
`0xE0`** — ese bug específico de P4.1.2 sigue corregido.

## Validación D — Integridad de framebuffers de película: **FALLO** (mandatory gate)

**HECHO, hallazgo crítico**, restringido a `t≥MPEG_INIT`:

| | `FBP=0xA0` (49 muestras) | `FBP=0x000` (51 muestras) |
|---|---|---|
| Filas 0-255 (bloques 0-3) en negro puro | **49/49 (100%)** | 0/51 |
| Filas 256-447 (bloques 4-6) en negro puro | 0/49 | 0/51 |

`FBP=0x000` permanece íntegro (sin cambio). **`FBP=0xA0` ahora pierde
sistemáticamente su mitad SUPERIOR** (antes P4.1.3 lo corrigió para la
mitad inferior). Mecanismo confirmado por aritmética de páginas GS
(FBW=8): `FRAME=0x70` ocupa páginas `[0x70,0xE0)`; el buffer `FBP=0xA0`
ocupa páginas `[0xA0,0x110)`; la intersección `[0xA0,0xE0)` corresponde
exactamente a las filas 0-255 de `FBP=0xA0` — el mismo mecanismo de
aliasing físico que P4.1.2 demostró para `0xE0`, ahora manifestado con
`0x70` sobre el rango complementario.

Confirmado visualmente: `visual_073500ms.png`/`visual_074063ms.png`
(ventana previamente afectada, ~73.5-74s) muestran **solo `ATTENZIONE`/
`ADVERTENCIA` (párrafos inferiores, filas ≥256)**, con la mitad superior
en negro — el defecto exactamente invertido respecto al que P4.1.3
corrigió.

## Validación E — descartada por el fallo de D

No aplica evaluación adicional: D ya establece que `FBP=0xA0` no está
íntegro.

## Validación F — Doble buffer de display (sin regresión, preservado)

`DISPFB2` sigue alternando `0x0`/`0xa0` en los draws observados (no
re-verificado explícitamente en la corrida por haberse detenido tras el
fallo de D, pero la Validación A confirma que `dispfb0`/`dispfb1` de
P4.1 no se tocaron en el diff, y ningún mecanismo de este fix opera
sobre DISPFB).

## Aplicación de la regla de fallo del prompt

> "If fbp1=frameSize restores UI but introduces a new movie defect, DO
> NOT add another speculative branch or movie-specific hack. Instead
> locate the new destructive writer... Stop and report:
> P415_FRAME_SIZE_CONTRACT_PARTIAL. Then the original oracle becomes
> mandatory."

Exactamente esta condición se cumplió (UI restaurada + defecto nuevo de
película). **Se detiene aquí, sin implementar un segundo fix
especulativo** (p. ej. usar un valor distinto solo para el struct de
película). El "nuevo escritor destructivo" ya está identificado y
explicado con precisión (Validación D) — lo que falta no es
localización, sino el valor SDK real, que solo un oracle PCSX2
(pendiente, no obtenido en dos intentos consecutivos, P4.1.4 y P4.1.5)
puede zanjar con certeza.

## Limpieza

`git -C vendor/PS2Recomp checkout --` sobre los 3 archivos tocados
(`GS.cpp`, `MPEG.cpp`, `gs_cpu_backend.cpp`); verificado `git -C
vendor/PS2Recomp diff --stat` vacío y `HEAD` de vuelta exacto en
`19911ce35fb8f2029853f25612b1cc420a8c74bb` (el `patched_commit` vigente
de P4.1.3, sin cambios). Reconstruido y re-linkeado desde esa fuente
(1 `CL.exe` cubriendo los 3 archivos revertidos, 0 inesperados en el
relink) para dejar el binario activo coherente con el estado de
producción committeado. **No se creó ningún patch de vendor nuevo.**
`fbp1 = 0u` (P4.1.3) permanece como el estado de producción vigente.

## Respuestas requeridas

1. `fbp1 = zbufAddr / 2u` (probado, luego revertido).
2. Porque `zbufAddr` es exactamente 2× el tamaño de un framebuffer por
   la propia fórmula de `sceGszbufaddr` (verificado a mano para ambos
   structs) — pero "correcto en abstracto" no significa "libre de
   aliasing físico" para el struct de película, como demostró la
   Validación D.
3. NO — ninguna herramienta PCSX2/MCP disponible en esta sesión.
4. Ninguno — no obtenido.
5. Ninguno — no obtenido.
6. `0x38` (confirmado en RAM).
7. `0x70` (confirmado en RAM).
8. UI `ZBUF=0x70`/`ZMSK=0`; película `ZBUF=0xE0`/`ZMSK=1` — ambos sin
   cambio respecto a P4.1.3/P4.1.4.
9. SÍ — Memory Card visible, byte-count idéntico al baseline
   conocido-bueno.
10. SÍ — Language Select visible, texto completo legible.
11. Antes (P4.1.3): ~99% (131-132/133). Después (P4.1.5 candidato):
    65.7% (23/35) — mejora sustancial pero no resuelto por completo.
12. `{0, 0x38}` — el par UI predicho exactamente.
13. `{0xa0, 0x70}` — el par película con el nuevo valor.
14. SÍ — `FBP=0x000` permanece 100% íntegro (0/51 incompletas).
15. **NO** — `FBP=0xA0` pierde ahora sus filas 0-255 (49/49, 100%).
16. **NO se mantiene** — el half-frame original (filas 256-447)
    desaparece, pero es reemplazado por uno nuevo e inverso (filas
    0-255).
17. NO — 0 draws a `FRAME=0xE0` en ninguna muestra (ese bug específico
    de P4.1.2 sigue corregido).
18. Consistente con lo esperado (no re-verificado explícitamente en
    esta corrida, sin motivo para sospechar cambio — P4.1 no se tocó).
19. **SÍ** — corrupción NUEVA en `FBP=0xA0` filas 0-255, geométricamente
    explicada y confirmada visual y dinámicamente.
20. UNKNOWN — no investigado en este prompt (fuera de alcance
    explícito, corrida detenida antes de esa zona).
21. **NO** — el gate de doble validación (UI Y película sin
    half-frame, en la MISMA corrida) **falla** en su componente de
    película.
22. NO aplica — el fallo en la primera corrida detiene el conteo de
    reproducibilidad per la regla de fallo del prompt (no se ejecutan
    las 2 corridas adicionales).
23. SÍ — el mecanismo es causal y explicado con precisión aritmética
    (aliasing de páginas GS), no una correlación superficial; SÍ
    respecto a por qué falla, NO respecto a que el candidato sea la
    solución correcta.
24. **NO** — no se cumple el success gate (ítems 6/7/9 del Commit Gate
    fallan: aunque `FBP0` está completo, `FBPA0` NO lo está; el
    half-frame de película NO queda resuelto, solo desplazado).

## Clasificación

`P415_FRAME_SIZE_CONTRACT_PARTIAL`

## Próximo paso recomendado

El oráculo PCSX2 original (direcciones ya identificadas dos veces:
`0x740960` UI slot1 `FRAME_1`, `0x740E20` película slot1 `FRAME_1`) deja
de ser opcional — es la única vía restante para determinar el valor SDK
real, dado que las dos hipótesis puramente aritméticas ensayadas hasta
ahora (`0` y `frameSize=zbufAddr/2`) están ambas refutadas como
contrato universal por evidencia dinámica directa y reproducible. Una
tercera hipótesis candidata para explorar en ese prompt (no implementada
aquí, per la regla de fallo): que el segundo framebuffer de la película
NO comparta el mismo `fbp1` que el de la UI — es decir, que
`sceGsSetDefDBuffDc` no tenga un contrato universal de "segundo buffer
= frameSize" sino que dependa de un parámetro adicional no identificado
aún (p. ej. el propio `envAddr`/tamaño de display, dado que el
`Main_init` de DMC ya parchea manualmente varios campos de estos
structs — el "segundo buffer" real de la película podría, de hecho, no
necesitar coincidir numéricamente con el de la UI en absoluto).

## Git al cierre

Main `5c0881b` (sin cambios, ningún commit en este prompt); los 6
informes previos + este nuevo, sin trackear. Vendor `19911ce…`, limpio,
sin patch nuevo. Ejecutable reconstruido desde esa fuente coherente.
Ningún proceso `dmc-recomp.exe` en ejecución. Sin push.
