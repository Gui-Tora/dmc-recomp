# VISUAL_007–009: auditoría visual posterior al parche 34 y diagnóstico de la cobertura de triángulos

**ELF:** `SLES_503.58` (PAL), SHA-256 `d0753a6b…bab4`, CRC32 `77654AD2`, entry `0x00100008`.

**Base:**

- `master` `86dff6a` (PR #12);
- 34 parches, vendor `ea54616d…`;
- exe integrado `a1e08309…`.

**Rama local:** `visual-007-post-gs4-audit`, sin commits.

**Fechas:** 2026-11-08.

**Procedencia:** informes locales `analysis/local/VISUAL_007_AUDIT.md`, `VISUAL_008_RESULTS.md` y
`VISUAL_009_RESULTS.md`, con sus directorios `visual008/` y `visual009/`. **No están versionados** ni se incluyen en el
repositorio. Esta nota conserva sus conclusiones.

**Continuación:** la corrección resultante es el parche 35, documentado en
[VISUAL_010_GS_TRIANGLE_COVERAGE.md](VISUAL_010_GS_TRIANGLE_COVERAGE.md). Aquí no se repite su detalle.

**Etiquetas:**

- **HECHO:** observado o demostrado con evidencia primaria (capturas, GS Replay, código).
- **INFERENCIA:** se sigue de los hechos.
- **HIPÓTESIS:** no demostrada.
- **SOSPECHA:** término de la auditoría para defectos vistos sin comparación válida.

## 1. Estado visual después del parche 34

**Qué hizo el parche 34** (GS-4): corrigió en el traductor EE la semántica de `PROT3W` y `LWU`. Ver
[PERF_003_GS_OPEN_ISSUES.md](PERF_003_GS_OPEN_ISSUES.md) §GS-4 y [PERF_003_ARCHITECTURE_FEASIBILITY.md](PERF_003_ARCHITECTURE_FEASIBILITY.md)
§24–27.

**VISUAL_007** es una auditoría de solo lectura sobre evidencias ya existentes. No se ejecutó el juego ni se compiló
nada. Se usaron:

- capturas del exe integrado:
  - menú y película: 46 capturas;
  - oficina, ticks 16389–16927: 118;
  - cinemática del castillo, 27060–27540: 714;
- capturas anteriores al parche 34;
- referencias de PCSX2:
  - capturas del usuario en la oficina, sin tick anotado;
  - el estado s09, el castillo ante la puerta con la misma cámara que el runtime;
- la captura de GS Replay `cap2` del castillo (VSync ~27101).

**Límite de la comparación:** salvo s09, las comparaciones con PCSX2 son de cámara equivalente, no de fotograma
idéntico.

## 2. Defectos corregidos y defectos que permanecen

| ID | Defecto | Estado tras el parche 34 | Evidencia |
|---|---|---|---|
| — | Cuñas naranjas delante de Dante (GS-4) | **Corregido** en las escenas observadas del castillo (27060–27540) | HECHO: 210 de 433 capturas con el exe estable; 0 de 714 con el integrado |
| V7-02 | Moteado de puntos sueltos sobre superficies texturizadas | Permanece | HECHO: oficina (trofeos), castillo en juego (roca) y cinemática (suelo, muro, fila de puntos del cielo). Ausente en PCSX2 con cámara equivalente. Se reproduce offline en `cap2` |
| V7-03 | Líneas finas diagonales en la pared de la oficina | Permanece | HECHO: en el runtime; PCSX2 no las muestra en ese plano |
| V7-04 | Mar con bloques de colores (GS-5) | Sin comparación válida tras el parche 34 | HECHO antes del parche 34 (t28555 frente a s09) |
| V7-05 | Imagen 512×448 a escala 1:1 con bandas de 64 px | Permanece | HECHO: es un problema de la presentación en el host, no de la imagen del guest |
| V7-06 a V7-11 | Puertas saturadas; figura oscura en primer plano; oscuridad y tinte de la oficina; forma en abanico junto a Trish; trazo junto al cuello; ghosting de la película | SOSPECHA o sin referencia | Detalle en el informe local |

Otras observaciones:

- **HECHO.** La captura 0083 de la oficina es idéntica píxel a píxel antes y después del parche 34. El parche no
  cambia el moteado ni las líneas de ese plano.
- **INFERENCIA.** Que GS-4 desaparezca no demuestra que no queden otros errores de transformación. Solo demuestra que
  el búfer facial de `Morph_sub_h` ya no produce cuñas en esas escenas.

**Recomendación de VISUAL_007:** investigar primero V7-02, el único defecto pendiente que se reproduce en una captura
offline ya existente.

## 3. Cuñas naranjas de GS-4: resumen y relación con estas fases

La investigación completa está en las notas de PERF_003 enlazadas en la sección 1. En resumen:

- **HECHO.** El rasterizador dibuja exactamente los triángulos recibidos. Las cuñas son triángulos de tira de la
  malla de la cabeza que unen vértices bien colocados con un racimo de vértices mal colocados.
- **HECHO.** El VIF, la DMA y el VU1 transportan y reproducen sus entradas sin error. Las posiciones erróneas salen
  del búfer facial que escribe el EE.
- **HECHO.** La primera divergencia es la traducción de `PROT3W` y `LWU` en `Morph_sub_h`.
- **Corrección:** parche 34.

**Relación con VISUAL_008/009:**

- `cap2` se capturó con el exe estable, anterior al parche 34, así que contiene las cuñas.
- Eso no afecta al diagnóstico del cielo, cuyos lotes no pertenecen a la malla de la cabeza.
- En VISUAL_010, 5 de los 14 píxeles de silueta que quedan menos parecidos a sus vecinos caen en esas cuñas.

## 4. Diagnóstico de los 11 puntos del cielo con GS Replay (VISUAL_008)

**Banco:** el binario de GS Replay de PERF_003. Su backend GS equivale al de 34 parches, porque el parche 34 no toca
el GS.

**Reproducción de `cap2` (SHA-256 `88895af1…`), HECHO:**

- `--force-zte`: IDENTICAL, VRAM `e2f02be8…`;
- modo normal: registro de hashes `d3f437c8…`, VRAM `50d7bb62…`.

Ambos coinciden con las referencias de PERF_003.

**Coordenadas:** la imagen presentada es FBP 0 (512×224) con cada línea duplicada, y el análisis trabaja en
coordenadas de FBP 0.

**Rastreo de escrituras, HECHO:**

- Los 11 puntos claros de la fila del cielo, como (232,63), (251,64) y (363,70), reciben **dos** escrituras de
  geometría. Proceden de dos triángulos vecinos: los lotes 2057/2074 y 2059/2072.
- Los píxeles de control vecinos reciben una sola escritura.
- Ningún punto procede de la VRAM inicial ni de una transferencia, y no hay píxeles sin escribir entre las muestras.

**Clasificación:** clase nueva, «doble escritura en arista compartida». No es un hueco, ni un texel erróneo, ni una
transferencia.

## 5. Doble cobertura con mezcla aditiva

**HECHO.** Estado de los lotes del cielo:

- triángulos sueltos con textura T8;
- `ALPHA=0x8000000068`, es decir, `Cs·FIX/128 + Cd` con FIX=128: **suma aditiva**;
- prueba de alfa GREATER 16;
- ZTE con GEQUAL.

**HECHO.** La arista compartida:

- Los lotes 2057 y 2074 comparten exactamente la arista (−65,0; 47,5625)–(293,4375; 66,75), en múltiplos de 1/16.
- Sus terceros vértices están a lados opuestos: son triángulos vecinos, no una segunda pasada.
- En la arista, Z es igual en los dos triángulos, así que GEQUAL deja pasar la segunda escritura.
- Con la suma aditiva, el píxel recibe `Cs` dos veces y sale ≈1,4 veces más claro.

**INFERENCIA.** En lotes opacos, como el modo `0x2a` sin mezcla, una doble cobertura sustituye un color por otro casi
igual y no se ve. La réplica encontró 929 pares vecinos con doble cobertura predicha en `cap2`, la mayoría opacos.

## 6. Defecto de la tolerancia de cobertura inclusiva

**HECHO (código del vendor de 34 parches).** `GSCpuBackend::DrawTriangle`:

- muestrea en `(x+0,5, y+0,5)`;
- acepta el píxel si las tres baricéntricas normalizadas son `≥ −1e-4`.

La prueba es inclusiva en todas las aristas y no tiene regla arriba-izquierda. Su margen es proporcional al área:
`1e-4·|2·área|/|arista|`, que da 0,041–0,042 px en los triángulos del cielo.

**HECHO (geometría):**

- La arista tiene pendiente 0,0535.
- Los centros de píxel caen en la franja de ±0,04 px una vez cada 1/0,0535 = 18,68 px.
- Eso coincide con el patrón observado: puntos cada ~18–19 px que bajan una línea por ciclo.

**HECHO (réplica float32 de la regla, `visual008/cover.py`):**

- Los 7 pares aditivos del cielo predicen 69 píxeles con doble cobertura. Explican **11 de 11** puntos observados.
- Los predichos que no se ven quedan detrás del pilar (por la prueba de Z) o tapados después por el pelo.

**HECHO (código de PCSX2, rasterizador SW, `GSRasterizer.cpp`):** recorre intervalos semiabiertos
`[ceil(y_sup), ceil(y_inf))` × `[ceil(x_izq), ceil(x_der))`.

**INFERENCIA:** PCSX2 asigna cada píxel de una arista compartida a un solo triángulo.

**Conclusión de VISUAL_008:**

- **Puntos del cielo de `cap2`:** causa identificada, con confianza alta. Las entradas del backend (vértices, Z,
  color, STQ, textura, CLUT) son coherentes; el error está en la regla de cobertura, no en EE, VU1 ni texturas.

## 7. Experimento de cobertura semiabierta (VISUAL_009)

**Banco aislado:** `visual009/`, fuera del vendor. Contiene tres variantes del reproductor, y cada una enlaza
exactamente una implementación de `GSCpuBackend`:

| Variante | Backend |
|---|---|
| `lib` | el de la biblioteca |
| `copy` | copia sin cambios |
| `exp` | copia con la regla experimental |

**Control (HECHO):** `copy` es bit a bit igual a `lib`, con el registro `d3f437c8…` y la VRAM `50d7bb62…`, y
`--force-zte` IDENTICAL. Por tanto, cualquier diferencia de `exp` se debe a la regla.

**Regla experimental** (solo la decisión de cobertura):

- Función de arista en `double`, con la normal interior hacia el tercer vértice, anclada en el extremo
  lexicográficamente menor.
- Dos triángulos que comparten una arista obtienen valores exactamente opuestos.
- Un píxel se cubre con `e > 0` en las tres aristas. Con `e = 0`, solo en aristas izquierdas o superiores.
- Es la convención de PCSX2 adaptada al muestreo en +0,5.

No cambian:

- el muestreo;
- `XYOFFSET`;
- la interpolación;
- la tijera;
- la mezcla;
- ZTE.

**Pruebas sintéticas (HECHO, `cov_test.cpp`):** mallas subpíxel, empates sobre centros de píxel, abanicos,
triángulos de 0,25–1,5 px, tijera y el par real del cielo.

| | Regla original | Regla experimental |
|---|---|---|
| Resultado | Falla en 7 casos (desde 2 hasta 3023 dobles por caso) | Pasa todos: 0 dobles, 0 huecos, 0 píxeles de más y 0 cambios al invertir el orden de dibujo |

## 8. Validaciones diferenciales (VISUAL_009, `cap2`)

**HECHO:**

- **Puntos del cielo:** los 11 desaparecen; cada píxel vale lo mismo (±1) que sus vecinos.
- **Cambios en total:** 433 bytes de VRAM de 4 MiB:

  | Superficie | Píxeles distintos |
  |---|---|
  | FBP 0 | 48 |
  | FBP 56 | 94 |
  | FBP 160 | 4 |
  | Z (al final) | 18 |

- **Todos los cambios se explican:**
  - en FBP 0 y Z, por cobertura en el propio búfer;
  - 43 de los 94 de FBP 56, por un posprocesado que muestrea FBP 0 desplazado (−1,−1);
  - FBP 160, por dos transferencias local→local que copian el framebuffer.

  Ninguno queda sin explicar.
- **Cobertura:**
  - 0 píxeles ganados;
  - cada píxel perdido tiene el centro fuera del triángulo (≤ 0,042 px) o exactamente sobre una arista derecha o
    inferior;
  - 0 grietas entre vecinos;
  - píxeles cubiertos por los dos triángulos de un par: de 875 a 0.
- **ZTE:** con `--force-zte` cambian los mismos 433 bytes. El efecto de ZTE es independiente de la regla.
- **Determinismo:** dos ejecuciones idénticas. Registro `c4a069db…` y VRAM `d96b6394…` en normal; `ab8ce6ce…` y
  `79787eec…` con `--force-zte`.
- **Rendimiento:** en VISUAL_009 se midió +5,4 % con mucho ruido, sin conclusión. VISUAL_010 lo midió de nuevo:
  ≈ +2 % del rasterizador en el banco.

## 9. Límites de fidelidad frente al hardware PS2

- **Siluetas.** 14 de los 48 píxeles cambiados de FBP 0 quedan menos parecidos a sus vecinos. Son píxeles de silueta
  con el centro fuera del objeto por 0,004–0,028 px.
  - Su correspondencia exacta con el hardware depende también de la posición de muestreo: +0,5 en el backend,
    coordenadas enteras en PCSX2.
  - **UNKNOWN**: no hay un fotograma de referencia equivalente.
- **Medio píxel de `XYOFFSET.OFY`.** El backend usa `ofy = XYOFFSET.OFY >> 4` y descarta el medio píxel de campo
  (30984/16 = 1936,5). Es una pregunta abierta, aparte de la cobertura.
- **Fuente de comparación.** PCSX2 se usó por su código (rasterizador SW), no por una imagen de la misma cinemática.
- **Alcance de la evidencia.** Una sola captura de 2 VSync de la cinemática del castillo. La réplica float32 coincide
  con el rastreo en todas las muestras comprobadas, pero no se verificó píxel a píxel en toda la imagen.

## 10. Lo que estas fases no diagnosticaron

| Defecto | Estado | Por qué está separado de los 11 puntos |
|---|---|---|
| Moteado oscuro (trofeos de la oficina, roca y suelo del castillo) | **No diagnosticado.** Confianza media-baja en que sea el mismo mecanismo | La doble escritura aditiva aclara; los puntos oscuros requerirían una mezcla que oscurezca, que no se ha observado. No hay captura GS de esas escenas |
| Líneas diagonales de la oficina (V7-03) | **Sin clasificar** (clase E de VISUAL_008) | No están en `cap2`. HIPÓTESIS sin demostrar: el mismo mecanismo con aristas de pendiente cercana a 1 |
| GS-5, mar corrupto | **Independiente, sin diagnosticar** | Son bloques rectangulares de colores ajenos. La doble cobertura solo intensifica píxeles sueltos con el color del objeto. Hace falta una captura del plano de s09 |
| `XYOFFSET.OFY` y posición de muestreo | **Abierto**, cambio aparte | Mueve siluetas; no se mezcla con la regla de cobertura |
| Aspecto 1:1 (V7-05) | Abierto | Presentación del host |

## 11. Parche 35 y VISUAL_010

**HECHO.** La regla de VISUAL_009 se integró como parche 35, `patches/VISUAL_010_gs_triangle_coverage.patch`, con el
mismo código validado. La integración en `master` fue por la PR #13 (`529e9ed`).

Lo que añadió VISUAL_010:

- la suite `PS2GSTriangleCoverage`;
- la regrabación de 24 hashes del corpus bilineal;
- las nuevas referencias de `cap2`, que **no** sustituyen a las históricas;
- la medida de rendimiento.

Todo está en [VISUAL_010_GS_TRIANGLE_COVERAGE.md](VISUAL_010_GS_TRIANGLE_COVERAGE.md).

**Validación posterior (HECHO, PERF_004D):** en las imágenes presentadas del juego con el exe de 35 parches, la métrica
de puntos claros aislados y la de cuñas son iguales en los modos síncrono y asíncrono. Ver
[PERF_004D_ASYNC_GS_INTEGRATION.md](PERF_004D_ASYNC_GS_INTEGRATION.md). Esto no es una nueva validación frente a PCSX2.

El parche 35 corrige los 11 puntos del cielo de `cap2`. **No** se presenta como corrección del moteado oscuro, de las
líneas de la oficina ni de GS-5.
