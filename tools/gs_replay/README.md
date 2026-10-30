# gs_replay: captura y reproducción determinista del GS

Banco offline del rasterizador CPU del GS (`GSCpuBackend`). Una captura graba, durante 1 o 2 VSync del juego, cada
llamada que el frontend del GS hace a la interfaz `GSRasterBackend`, con sus datos y salidas observables.
`gs_replay` la reproduce después contra el `GSCpuBackend` del vendor y verifica, byte a byte, que el resultado es
el mismo. Sirve para:

- validar cambios del rasterizador sin ejecutar el juego: localiza la primera llamada cuyo resultado cambia;
- medir el coste del GS aislado del EE, del VU1 y de la temporización (A/B reproducibles).

Este directorio no contiene datos del juego. **Las capturas (`*.gsrp`) contienen VRAM y datos derivados del juego
y no se versionan**; tampoco los volcados (`*.bin`, `*.ppm`) ni los builds. Se guardan en `analysis/local/`, que
está ignorado por Git.

Origen y validación: `analysis/notes/PERF_003_ARCHITECTURE_FEASIBILITY.md` (§13: equivalencia exacta; §15:
corrección de ZTE).

## Ficheros

| Fichero | Función |
|---|---|
| `gs_replay.h` | Lado de captura: `RecordingBackend` (envuelve `GSCpuBackend`; solo observa) y el formato de fichero v2 |
| `main.cpp` | Herramienta: `selftest`, `replay`, `microbench` |
| `capture_hook.inc` | Hook opcional que instala el grabador en el ejecutable del juego; no lo incluye ningún build |
| `function_table_stub.cpp` | Tabla de funciones recompiladas vacía: la herramienta no ejecuta código EE |
| `CMakeLists.txt` | Build contra un árbol ya compilado del vendor, más la prueba de CTest |

## Compilación

Requisitos: Windows x64, MSVC 2022 y CMake ≥ 3.22. Hace falta un árbol de compilación del vendor
(`vendor/PS2Recomp` en el `patched_commit` de `upstream.lock.json`) con `ps2_runtime.lib`, `ps2_iop.lib`, raylib y
el FFmpeg prebuilt. Por ejemplo, el árbol en el que se compila `ps2x_tests`. Así el `GSCpuBackend` que se
reproduce es el mismo código que usa el juego. La herramienta no recompila el runtime.

```
cmake -S tools/gs_replay -B <build> -G "Visual Studio 17 2022" -A x64 -DPS2X_PREBUILT_DIR=<árbol del vendor>
cmake --build <build> --config Release
```

En tiempo de ejecución, las DLL de FFmpeg (`<árbol del vendor>/ThirdParty/ffmpeg-prefix/src/ffmpeg_external/bin`)
tienen que estar en el `PATH`.

Comprobar antes de usarla:

- `vendor/PS2Recomp` está limpio y en el `patched_commit` (`scripts/pipeline.py` lo verifica);
- `ps2_runtime.lib` está al día con el vendor (compilación incremental del runtime).

## Prueba automática

```
ctest --test-dir <build> -C Release        # o: gs_replay selftest <directorio existente>
```

`selftest` dibuja escenas sintéticas a través del frontend real del GS con el grabador instalado y las reproduce.
Comprueba:

- igualdad exacta de la VRAM;
- que se detecta una divergencia provocada;
- la integridad del fichero;
- la reproducción del entorno de coma flotante (MXCSR) grabado;
- el estado oculto de la transferencia local→host (`L2HState`).

Resultado esperado: todas las comprobaciones dan `PASS` y el código de salida es 0.

## Captura (requiere ejecutar el juego)

1. En `runtime/dmc_overrides.cpp`, de forma temporal:
   - añadir `#include "../tools/gs_replay/capture_hook.inc"`;
   - llamar a `startGsReplayCapture(runtime);` donde se instalan los demás hooks, antes de cargar el checkpoint.
   - Recompilar solo esa unidad.
2. Ejecutar desde un checkpoint con:
   - `DMC_GS_CAPTURE=<fichero.gsrp>`;
   - `DMC_GS_CAPTURE_FROM=<tick VSync>`;
   - `DMC_GS_CAPTURE_VSYNCS=1|2`;
   - opcionalmente `DMC_GS_CAPTURE_MAX_MB`.
   
   El log muestra `[GSREPLAY] capture written` o `capture failed`.
3. Retirar el include y la llamada, y recompilar esa unidad. No se debe hacer commit de un `dmc_overrides.cpp` con el
   hook.

El grabador también registra, en cada llamada, el entorno de coma flotante del hilo que llama: los bits de control
de MXCSR, la palabra de control del CRT y `fegetround()`. El código VU1 llama al GS con `FE_TOWARDZERO`, y sin ese
dato la reproducción no es exacta (PERF_003 §13).

## Reproducción

```
gs_replay replay <captura.gsrp> [opciones]
```

| Opción | Efecto |
|---|---|
| (ninguna) | reproduce y verifica los hashes de VRAM intermedios, cada `Present` y la VRAM final |
| `--continue` | sigue tras la primera divergencia y cuenta todas |
| `--stats` | estadísticas de primitivas, píxeles y transferencias |
| `--bench N` | N tandas de tiempo: sin verificación, con verificación y todo con redondeo al más cercano (con una captura que diverge, requiere `--continue`) |
| `--by-class` | tiempo de `Submit` por clase de dibujo (no aplica `--force-zte`) |
| `--profile <fichero> N` | muestreo del hilo del backend en N reproducciones (se simboliza con el `.map`) |
| `--dump-final <fichero>` | escribe la VRAM final (4 MiB) |
| `--dump-after <registro> <fichero>` | escribe la VRAM tras ese registro |
| `--dump-present <prefijo>` | escribe cada imagen presentada (PPM; los datos son 640×512 aunque la cabecera declare menos) |
| `--dump-znever` | lista los lotes con ZTE=0 o ZTST=NEVER y su estado |
| `--force-zte` | fuerza `TEST.ZTE=1` en cada lote: reproduce la semántica anterior al parche 32 (control) |
| `--ignore-fpenv` | ignora el entorno de coma flotante grabado (solo diagnóstico) |
| `--rounding-heuristic` | heurística de redondeo anterior al formato v2 (solo diagnóstico) |
| `--allow-unobserved-stale-l2h` | permite una ventana con un búfer local→host de origen desconocido (no exacto) |

Códigos de salida:

| Código | Significado |
|---|---|
| 0 | idéntico |
| 1 | divergencia del backend (se informa de la primera) |
| 2 | error de formato o integridad, o un prólogo que no se puede reconstruir |
| 3 | error de uso o de E/S |

## Uso como validación

- **Cambio que no debe alterar el resultado:** la captura de referencia tiene que dar `IDENTICAL`.
- **Cambio de semántica:**
  - la primera divergencia tiene que caer en los lotes afectados;
  - un control que restaure la semántica anterior (como `--force-zte`) tiene que dar `IDENTICAL`;
  - dos reproducciones tienen que dar la misma VRAM final;
  - la nueva VRAM no sustituye a la referencia anterior sin pruebas semánticas independientes.
- **Rendimiento:**
  - alternar A/B varias rondas con el mismo ejecutable siempre que sea posible;
  - dar la mediana y la dispersión;
  - anotar la carga de la máquina.
