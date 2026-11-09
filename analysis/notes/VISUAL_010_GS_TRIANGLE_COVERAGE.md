# VISUAL_010: cobertura semiabierta de triángulos en el GS por CPU (parche 35)

ELF `SLES_503.58`, SHA-256 `d0753a6b…bab4`, CRC32 `77654AD2`, entry `0x00100008`. Base: `master` `86dff6a`
(34 parches, vendor `ea54616d…`). Parche: `patches/VISUAL_010_gs_triangle_coverage.patch`.

Cadena de fases: VISUAL_007 (auditoría visual posterior al parche 34), VISUAL_008 (diagnóstico offline del moteado),
VISUAL_009 (experimento diferencial) y VISUAL_010 (este parche). Los informes detallados, las capturas y los bancos
locales están en `analysis/local/` (no versionado). El nombre evita `GS5`, que ya designa el mar corrupto (GS-5).

## 1. Defecto

- **HECHO (VISUAL_007):** la imagen del runtime muestra un moteado de puntos sueltos en la oficina y en el castillo;
  las capturas de PCSX2 de cámara equivalente (s09, trofeos de la oficina) no lo tienen. Persiste con el parche 34.
- **HECHO (VISUAL_008, GS Replay de `cap2`, VSync 27100–27101):** la fila de 11 puntos claros del cielo está formada
  por píxeles que escriben **dos** triángulos vecinos (lotes 2057/2074 y 2059/2072) con mezcla aditiva
  (`ALPHA=0x68`, `Cs·FIX/128 + Cd`, FIX=128). Los dos triángulos comparten exactamente la arista (mismos vértices en
  múltiplos de 1/16) y están a lados opuestos. Los píxeles vecinos se escriben una sola vez.
- **HECHO (código):** `GSCpuBackend::DrawTriangle` aceptaba un píxel si sus tres baricéntricas normalizadas eran
  `≥ −1e-4`. La prueba es inclusiva en todas las aristas y tiene un margen proporcional al área: 0,04 px en los
  triángulos del cielo. Por eso un centro de píxel (casi) sobre una arista compartida pertenece a los dos triángulos.
  La pendiente de la arista (0,0535) da un punto cada 18,7 px, que es lo que se observa.
- **HECHO (PCSX2, `GSRasterizer.cpp` del rasterizador SW):** recorre `[ceil(y_sup), ceil(y_inf))` y
  `[ceil(x_izq), ceil(x_der))`; un píxel de una arista compartida pertenece a un solo triángulo.

## 2. Corrección

`ps2xRuntime/src/lib/gs/gs_cpu_backend.cpp`, `DrawTriangle`: solo cambia la decisión de cobertura.

- Para cada arista se calcula la normal interior (hacia el tercer vértice, válida para cualquier orientación) y la
  función de arista `e = n·(p − ancla)` en `double`, anclada en el extremo lexicográficamente menor. Los dos
  triángulos de una arista compartida obtienen valores exactamente opuestos; además, con vértices en múltiplos de
  1/16 y centros en múltiplos de 1/2, los productos son exactos.
- El píxel se cubre si `e > 0` en las tres aristas. Con `e = 0`, solo en aristas izquierdas (normal hacia +x) o
  superiores (normal horizontal hacia +y): la convención de PCSX2 adaptada al muestreo en `+0,5` del backend.
- **No cambian:** posición de muestreo (`+0,5`), `XYOFFSET` (incluido el `>> 4`), desplazamiento entre campos,
  baricéntricas e interpolación de color, STQ, UV, Z y niebla, caja envolvente, tijera, descarte de degenerados,
  mezcla, ZTE ni precisión de otros cálculos.
- El código es el validado en VISUAL_009 (43 líneas idénticas); solo se reescribió el comentario.

## 3. Pruebas

`ps2xTest/src/ps2_gs_tests.cpp`:

- Suite nueva `PS2GSTriangleCoverage` (6 pruebas), por el camino real de registros (`PRIM`, `RGBAQ`, `XYZ2`) hasta el
  backend, con un recuento de cobertura por píxel (mezcla aditiva de R=1) y un mapa de dueño dibujado en los dos
  órdenes:
  - malla subpíxel con vértices desplazados, diagonales y orientación al azar;
  - aristas horizontales, verticales y a 45° sobre centros de píxel (empates) y un triángulo rectángulo con la regla
    arriba-izquierda explícita;
  - abanico de 48 triángulos con orientación alterna;
  - triángulos de 0,25–1,5 px y un triángulo degenerado;
  - malla recortada por la tijera;
  - par de triángulos grandes con arista compartida y mezcla aditiva (el par del cielo de `cap2`).

  En todas se comprueba que no hay dobles ni huecos, que no se dibuja fuera de la región semiabierta y que el dueño no
  depende del orden. Las coordenadas se envían con un origen de 1 024 px que retira `XYOFFSET`, porque `XYZ2` es
  12.4 sin signo.
- `GS bilinear sampling corpus matches the reference output`: se vuelven a grabar los 24 hashes de los casos de
  triángulo STQ (uno de cada 7).
  - **HECHO:** con el runtime original el corpus reproduce los 168 hashes; con el parche 35 cambian exactamente los
    24 triángulos y ningún sprite.
  - En cada triángulo cambian 3 píxeles, cuyo centro está **exactamente** sobre una arista derecha o inferior
    (distancia 0). El interior, y por tanto el muestreo bilineal, es idéntico.

Suite completa: 489 pruebas, 465 superadas, los mismos 24 fallos preexistentes (antes 483/459/24); ningún test común
cambia de estado.

## 4. Validación con GS Replay

Banco offline: el mismo reproductor enlazado con el runtime de 34 parches y con el de 35. No se ejecuta el juego.

- `cap2` (`88895af1…`):
  - el runtime de 34 parches reproduce las referencias históricas (registro `d3f437c8…`, VRAM `50d7bb62…`;
    `--force-zte` IDENTICAL, `e2f02be8…`);
  - con el parche 35, los 11 puntos del cielo desaparecen: cada píxel vale lo mismo (±2 en R+G+B) que sus vecinos;
  - cambian 433 bytes de VRAM, 48 píxeles de la imagen presentada, 94 de FBP 56, 4 de FBP 160 y 18 de Z;
  - todos se explican por la regla:
    - en FBP 0 y en Z, por cambios de cobertura del propio búfer: aristas compartidas, superposiciones y bordes
      exteriores;
    - en FBP 56, 43 píxeles llegan por el posprocesado que muestrea FBP 0 desplazado (−1,−1);
    - en FBP 160, por dos transferencias local→local que lo copian.
  - Ningún píxel gana cobertura. Cada píxel que se pierde tiene el centro fuera del triángulo (≤ 0,042 px) o
    exactamente sobre una arista derecha o inferior.
  - 0 grietas entre triángulos vecinos; 875 → 0 píxeles cubiertos por los dos triángulos de un par.
  - ZTE normal y forzado: los mismos 433 bytes; el efecto de ZTE no cambia.
  - Determinista: dos ejecuciones idénticas.
  - Nuevas referencias, que **no** sustituyen a las históricas: registro `c4a069db…`, VRAM `d96b6394…`; con
    `--force-zte`, `ab8ce6ce…` y `79787eec…`.
  - El runtime real del parche 35 da los mismos hashes que la copia experimental de VISUAL_009.
- `gs4/cap/run1`: reproduce `cap2` en modo normal. Sus lotes son idénticos a los de `cap2`, así que confirma el
  resultado pero no es una escena independiente.
- `selftest.gsrp` (escenas sintéticas grabadas con el runtime antiguo): solo cambian píxeles predichos por la regla.
  El autotest completo de GS Replay (grabar y reproducir) da 24/24 PASS con los dos runtimes.
- `cap1` (formato v1) y `val2/…_reconstructed` (integridad no válida) no se pueden usar.

## 5. Siluetas

14 de los 48 píxeles de `cap2` quedan menos parecidos a sus vecinos:

- 4 en el borde derecho del pilar (lote 1940);
- 5 en las cuñas de GS-4, que existen en `cap2` porque la captura es anterior al parche 34;
- 5 con un contraste mínimo en zonas oscuras.

En todos, el centro está fuera del triángulo que lo pierde, a 0,0001–0,028 px. Son correcciones geométricas de la
regla de cobertura.

**Sin verificar:** la correspondencia exacta de las siluetas con el hardware, que depende también de la posición de
muestreo (`+0,5` en el backend, enteros en PCSX2). No hay un fotograma de referencia equivalente.

## 6. Rendimiento

Banco GS Replay sobre `cap2`, tiempo del backend sin verificación (`--bench`), Release de MSVC 19.44 x64 con las
opciones del runtime. 15 rondas por par con el orden alternado y 4 repeticiones por proceso; la primera se descarta
como calentamiento, así que hay 45 medidas por lado. Carga de CPU mediana del 4 % (máximo 13–20 %).

| Par | 34 parches (mediana, IQR) | 35 parches (mediana, IQR) | Por ronda (mediana, IQR) | Más lento |
|---|---|---|---|---|
| Misma compilación, solo cambia la regla (copias de VISUAL_009) | 198,6 ms (196,5–203,0) | 203,4 ms (199,4–210,0) | +1,4 % (−0,2..+3,5) | 10/15 |
| Runtimes reales (`t1j` frente al árbol del parche 35) | 198,9 ms (195,8–203,6) | 203,1 ms (200,3–208,9) | +2,5 % (+0,2..+4,2) | 12/15 |

- INFERENCIA: hay un coste pequeño y reproducible de en torno al +2 % del rasterizador, por evaluar tres funciones de
  arista en `double` por píxel candidato además de las baricéntricas.
- El +5,4 % de VISUAL_009, medido con mucho más ruido, no se confirma.
- Es tiempo del banco, no del juego, y no se ha optimizado nada.

## 7. Límites y riesgos

- Solo se ha validado con capturas del castillo. El moteado oscuro y las líneas diagonales de la oficina no están
  demostrados como el mismo defecto (VISUAL_008 §5) y este parche **no** se presenta como su corrección.
- No se relaciona con GS-5 (mar corrupto).
- La posición de muestreo y el medio píxel descartado de `XYOFFSET.OFY` siguen abiertos y son un cambio aparte.
- El juego no se ha recompilado ni ejecutado con el parche. Cambia un solo `.cpp` del runtime; no afecta al código EE
  generado.
