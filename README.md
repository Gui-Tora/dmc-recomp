# Devil May Cry (2001) — PS2 static recompilation bring-up

Investigación para PC con PS2Recomp y Ghidra. Objetivo inmediato: M0–M2.
Todavía no se ha analizado un ELF de DMC ni demostrado que el juego arranque.

## Organización

- `original/`: ELF y datos locales, sin distribuirlos ni añadirlos a Git.
- `analysis/ghidra/`: proyecto Ghidra local; `analysis/local/`: exports y recibos.
- `analysis/notes/`: evidencia de bloqueos por SHA-256.
- `recomp/config.toml`: configuración local preparada desde el export de Ghidra.
- `recomp/generated/<sha256>/<run>/`: C++ generado, sin ediciones manuales.
- `runtime/`: overrides DMC; `patches/`: cambios generales al runtime upstream.
- `vendor/`: upstream y wiki fijados en `upstream.lock.json`, clones locales ignorados.
- `build/` y `logs/`: compilaciones y diagnósticos locales.

## Flujo en Windows

Python 3.11+, Git, Visual Studio 2022 con C++ x64/Windows SDK y CMake >=3.21.
El script encuentra también el CMake incluido en Visual Studio. La primera
configuración descarga dependencias mediante CMake FetchContent.

Desde esta carpeta:

```powershell
python scripts/pipeline.py bootstrap
python scripts/pipeline.py tools
python scripts/pipeline.py identify original/NOMBRE_REAL_DEL_ELF
```

Importar ese mismo ELF en Ghidra, completar y revisar el análisis y ejecutar
`vendor/PS2Recomp/ps2xRecomp/tools/ghidra/ExportPS2Functions.java` desde Script Manager.
Añadir su carpeta a Script Directories. Guardar TOML en `analysis/local/ghidra.toml`
y CSV en `analysis/local/functions.csv`. Crear esas carpetas al guardar si faltan.
El exportador tiene dos diálogos `askFile`; no hemos supuesto una interfaz CLI
propia del script ni automatizado el reconocimiento de funciones.

Verificar que el lenguaje/importador instalado en Ghidra comprende R5900,
incluyendo las extensiones PS2; no dar por correcto un análisis MIPS genérico.
La versión de Ghidra y su extensión PS2/JDK se fijarán al preparar ese entorno.

```powershell
python scripts/pipeline.py prepare --elf original/NOMBRE_REAL_DEL_ELF --toml analysis/local/ghidra.toml --csv analysis/local/functions.csv
python scripts/pipeline.py generate
python scripts/pipeline.py build
python scripts/pipeline.py run --seconds 60
```

`prepare` conserva las opciones y clasificaciones del export y cambia sus tres
rutas. Revisar `stubs` y `untracked_stubs` en el TOML exportado: una clasificación
automática no prueba la semántica. Para cambiarlas, editar el TOML fuente y
repetir `prepare`. Cada preparación crea una ruta nueva de generación para no
mezclar restos anteriores. Los recibos comprueban ELF, config, CSV, salida y
ejecutable; no prueban que los límites de funciones sean correctos.

El lanzador registra stdout/stderr y limita cada intento a 60 s por defecto.
Un timeout no equivale a un bloqueo confirmado: también puede ser ejecución
normal; investigar el log y el PC. Los procesos de configuración/compilación
guardan su salida completa en `logs/`. El runtime abre su ventana al ejecutar.
Ejecutar con el ELF equivocado debe fallar antes de arrancar.

El runtime inicial desactiva debug UI y FFmpeg para M0–M2; FFmpeg desactivado
impide validar vídeo real. La ruta de datos parte de la carpeta del ELF.
M3 requerirá comprobar la resolución real de archivos y el contenido del disco.

## Hitos y evidencia

| Hito | Criterio | Estado |
| --- | --- | --- |
| M0 | ELF real → C++ generado, enlazado con tabla no vacía | Pendiente ELF/export |
| M1 | Evidencia de ejecución del entry del ELF | Pendiente |
| M2 | Inicialización básica identificada y completada | Pendiente |
| M3–M4 | Lectura de datos y primera imagen | Fuera del objetivo inmediato |
| M5–M10 | Intro, menú, Mission 1, control, combate, juego completo | Sin evaluar |

Registrar el primer fallo significativo con `analysis/notes/BLOCKER_TEMPLATE.md`.
Una ventana abierta o un ejecutable compilado no demuestran M1/M2.

Ver [hallazgos y APIs](analysis/UPSTREAM.md). La viabilidad de M0–M2 merece una
prueba, pero el soporte necesario para DMC todavía no está medido.
