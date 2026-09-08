# Archivos generados anormalmente grandes — bloqueo real de compilación

- ELF: `SLES_503.58`, Devil May Cry (Europa/PAL) v1.02, sha256
  `d0753a6b3b2f00802a50758a872d8cf051725aa31c839aa8894eee30ce58bab4`,
  crc32 `77654AD2`, entry `00100008`.
- Entorno: upstream PS2Recomp commit `14b1e5cb39b4af7e6fc12f9a29fdc751efde49d7`,
  export Ghidra en `analysis/ghidra/export/dmc_functions.csv` (11012 funciones),
  primera `generate` en `recomp/generated/<sha256>/1788792954874396800/`.

## Reproducción

`python scripts/pipeline.py generate` terminó "Recompilation completed successfully"
sin error fatal, pero `python scripts/pipeline.py build` reventó al validar hashes:

```
File "scripts/pipeline.py", line 164, in <dictcomp>
    actual = {p.name: digest(p) for p in folder.iterdir() if p.suffix in ('.cpp', '.h')}
File "scripts/pipeline.py", line 30, in digest
    return hashlib.sha256(path.read_bytes()).hexdigest()
MemoryError
```

## Último hito demostrado y evidencia

`generate` completo (10171 archivos), sin llegar a `build`. `digest()` cargaba el
archivo entero en memoria antes de hashear — corregido para leer en bloques de 1MB
(no es la causa raíz, solo hacía más frágil el síntoma).

## Causa raíz

21 de los 10171 archivos generados concentran el 94% del peso total:

```
total generado:      3327550344 bytes (~3.17 GB) en 10171 archivos
21 archivos >5MB:     2981.28 MB  (94%)
resto (10150 arch.):   ~192 MB   (mediana ~8.8 KB, tamaño normal)
```

Los 20 más grandes (excluyendo `register_functions.cpp`, que es la tabla de
despacho y pesa 87MB por separado — cubre 1176823 slots, uno por cada dirección
de instrucción de todo el rango de texto del ELF, 0x100008-0x57d3e4; su tamaño es
inherente al tamaño del ELF, no a estas 20 funciones):

```
251 MB  Calc_md_0x16b8e0.cpp
235 MB  capVu0MulMatrix2_0x1a0080.cpp
217 MB  capVu0MulMatrix_r_0x1a0180.cpp
215 MB  Calc_scr_light_0x16d920.cpp
215 MB  katVu0ViewVolumeClip_0x132d20.cpp
200 MB  katVu0Setmat4_shw_0x1da7b0.cpp
196 MB  InterpNode_0x132dd0.cpp
196 MB  ClipCheck2_0x132ec0.cpp
178 MB  katVu0RotTransPersQ_0x132ef0.cpp
160 MB  katVu0Clip_ld_0x132da0.cpp
144 MB  katVu0RotTransPers_shw_0x1da8e0.cpp
144 MB  sceVu0SetRotTransPers_0x1d5e40.cpp
127 MB  katVu0SetClip_0x132d70.cpp
125 MB  capCopyMatrix_0x1a01f0.cpp
107 MB  capVu0ScaleVectorXYZ2_0x19fc40.cpp
 91 MB  _capVu0ecossin_0x1a03c0.cpp
 91 MB  capVu0AddVectorXYZ_0x19fbb0.cpp
 91 MB  capVu0SubVectorXYZ_0x19fbd0.cpp
 37 MB  Get_light_str_0x16d8e0.cpp
 19 MB  capVu0Compare_0x1a0300.cpp
```

## Hipótesis y evidencia que la distingue de otras causas

Todas corresponden a funciones marcadas con avisos `[warning] control-flow ... -
unresolved JR/JALR ... promoted N fallback entries` en el log de `generate`
(hasta 124 fallback entries en algunas). La mayoría son rutinas VU0 en modo
macro (`capVu0*`, `katVu0*`, `sceVu0SetRotTransPers`) usadas para matrices/
vectores de cámara y transformación 3D, más un par de funciones de cálculo de
luces/interpolación (`Calc_md`, `Calc_scr_light`, `InterpNode`, `ClipCheck2`,
`Get_light_str`). Hipótesis: cuando el recompilador no puede resolver un salto
indirecto con certeza, genera un fallback que cubre un rango de direcciones
mucho mayor de lo necesario, inflando el cuerpo de la función. Confirmado
distinguiéndolo de un simple archivo grande "normal": una función ya marcada
como stub (`sceGsResetGraph`) genera un wrapper de 376 bytes, así que el
problema es específico de la traducción completa de estas 20, no de cómo se
generan los stubs en general.

## Cambio mínimo aplicado

Añadidas las 20 direcciones a `[general].stubs` en
`analysis/ghidra/export/dmc.toml` (sección "Triage manual", ver ese archivo),
todas con el handler genérico `ret0` (no hay contrato conocido todavía).
`sceVu0SetRotTransPers` ya estaba en `untracked_stubs` (ignorado por el
recompilador de todas formas); se deja ahí y se añade también a `stubs`, sin
conflicto.

## Contrato supuesto y criterio de retirada

`ret0`: no se sabe si el llamador espera un valor de retorno específico o solo
efectos secundarios (escritura de matrices/vectores vía punteros). Retirar
cuando: (a) se implemente VU0 macro mode en el runtime, o (b) se observe un
efecto visual/lógico concreto atribuible a que estas 20 funciones no hacen
nada (previsible en el rango de M4 en adelante, "primera imagen visible" y
después, no en M0-M2).

## Resultado tras el cambio (2026-09-07, confirmado)

`generate` repetido con el TOML curado: `recomp/generated_active` bajó a 230MB
(de 3.17GB). `build` en curso, avanzando con normalidad.

## Investigación adicional (2026-09-07, tras petición explícita de no asumir que "sin
caller" = "función muerta")

**1. Procesador/lenguaje de Ghidra confirmado inadecuado**: el programa está
analizado con `LANGUAGE_ID=MIPS:LE:64:64-32R6addr` (compiler spec `default`).
Ese `R6addr` es **MIPS Release 6** (~2014). El R5900 de la PS2 (1999, MIPS III +
extensiones EE propias: MMI, COP2/VU0) no tiene ninguna instrucción de MIPS R6.
Ghidra no tiene un módulo de lenguaje específico para R5900 instalado; usa el
MIPS genérico más cercano disponible, que reinterpreta opcodes exclusivos de
EE (MMI vía SPECIAL2, COP2 LQC2/SQC2) como instrucciones de compact branch de
R6 (`beqzc`, etc.) que nunca existieron en el hardware real.

**2. Bytes crudos vs. decodificación, confirmado con evidencia directa**: en
`0x00132d20` (`katVu0ViewVolumeClip`) el word `0xd8a40000` lo decodifica
Ghidra como `beqzc a1,0x00232d24` (salto absurdo, +0x100004 bytes), pero
PS2Recomp/rabbitizer (consciente de R5900) lo decodifica correctamente como
`LQC2 $vf4, 0x0($a1)` — carga un registro vectorial VU0 desde un puntero de
argumento. Mismo patrón confirmado en `capVu0MulMatrix2@0x1a0080`
(`LQC2 $vf8, 0x0($a2)`). En `Calc_md@0x16b8e0`, el word `0x71006628` lo
decodifica Ghidra como `SPECIAL2` sin resolver, PS2Recomp lo decodifica como
`paddub $t4,$t0,$zero` (MMI real, suma paralela de bytes). De las 20, 18
tienen como primera instrucción un `LQC2` disfrazado de `beqzc`; 2
(`Calc_scr_light`, `_capVu0ecossin`) tienen como primera instrucción un
`LUI`/`AUI` normal (correctamente decodificado, coincide en ambos backends) —
para esas dos el cuerpo gigante debe originarse en una instrucción posterior,
no en la primera.

**3. Búsqueda de referencias indirectas (no solo xrefs directas de Ghidra)**:
escaneadas ~180.881 instrucciones de todo el programa buscando el patrón
`lui`+`ori`/`addiu` que construye una constante de 32 bits (forma habitual de
cargar una dirección de función para llamarla vía registro/tabla): **0
coincidencias para las 20 direcciones**. Escaneada también toda la memoria
inicializada buscando las 20 direcciones como valor crudo de 4 bytes (tabla de
punteros/callbacks en datos): aparecen 2-3 veces cada una, pero **todas dentro
del bloque `.mwcats`** (metadata de depuración del compilador Metrowerks
CodeWarrior), no en ninguna sección de datos del propio juego.

**Conclusión de esta fase**: la causa del tamaño (Ghidra sin soporte R5900)
está confirmada al 100% con evidencia de bytes. Que estas 20 funciones no se
usen **no está confirmado**, solo no hay evidencia de invocación por ningún
mecanismo estático rastreado (ni JAL/JALR directo, ni construcción de
dirección en código, ni tabla de punteros en datos de juego). Los cuerpos
decodificados por PS2Recomp sí parecen prólogos reales de funciones
matriciales/vectoriales VU0 (cargan varios `vfN` justo al entrar), consistente
con sus nombres. Sigue pendiente la confirmación empírica en runtime.

**Instrumentación añadida** en `runtime/dmc_overrides.cpp`: las 20 direcciones
se registran como Game Override específico de este ELF (nombre+entry+CRC32),
con un stub que sigue comportándose como `ret0` pero cuenta llamadas y
registra en stderr la primera llamada (y cada 50 siguientes) con nombre,
dirección, a0-a3 y dirección de retorno. Pendiente: correr
`python scripts/pipeline.py run` y ver si aparece algún log
`[dmc-stub-instrumentation]`.

**Clasificación provisional** (categorías F=Ghidra decodifica mal MMI/COP2/VU0
confirmado para las 20; B=función real, uso sin confirmar todavía):

| Dirección | Nombre | Tamaño Ghidra (bytes) | Primera instrucción real (PS2Recomp) | Categoría |
|---|---|---:|---|---|
| 0x0016B8E0 | Calc_md | 3,670,240 | `paddub $t4,$t0,$zero` (MMI) | F + B(sin confirmar) |
| 0x001A0080 | capVu0MulMatrix2 | 3,408,052 | `LQC2 $vf8,0x0($a2)` | F + B(sin confirmar) |
| 0x001A0180 | capVu0MulMatrix_r | 3,146,604 | `LQC2 $vf4,0x0($a1)` | F + B(sin confirmar) |
| 0x0016D920 | Calc_scr_light | 3,408,208 | `AUI/LUI v1,0xbb00` (normal) | F + B(sin confirmar) |
| 0x00132D20 | katVu0ViewVolumeClip | 3,146,108 | `LQC2 $vf4,0x0($a1)` | F + B(sin confirmar) |
| 0x001DA7B0 | katVu0Setmat4_shw | 2,883,964 | `LQC2 $vf9,0x0($a0)` | F + B(sin confirmar) |
| 0x00132DD0 | InterpNode | 2,883,900 | `LQC2` (a1-based) | F + B(sin confirmar) |
| 0x00132EC0 | ClipCheck2 | 2,883,872 | `LQC2` (a0-based) | F + B(sin confirmar) |
| 0x00132EF0 | katVu0RotTransPersQ | 2,621,572 | `LQC2 $vf4,0x0($a1)` | F + B(sin confirmar) |
| 0x00132DA0 | katVu0Clip_ld | 2,360,400 | `LQC2` (a0-based) | F + B(sin confirmar) |
| 0x001DA8E0 | katVu0RotTransPers_shw | 2,097,892 | `LQC2 $vf4,...($a1)` | F + B(sin confirmar) |
| 0x001D5E40 | sceVu0SetRotTransPers | 2,097,172 | `LQC2 $vf8,...($a1)` | F + B(sin confirmar) |
| 0x00132D70 | katVu0SetClip | 1,835,236 | `LQC2` (a0-based) | F + B(sin confirmar) |
| 0x001A01F0 | capCopyMatrix | 1,835,224 | `LQC2 $vf4,...($a1)` | F + B(sin confirmar) |
| 0x0019FC40 | capVu0ScaleVectorXYZ2 | 1,572,988 | `LQC2 $vf4,...($a1)` | F + B(sin confirmar) |
| 0x001A03C0 | _capVu0ecossin | 1,311,168 | `AUI/LUI v1,0xa000` (normal) | F + B(sin confirmar) |
| 0x0019FBB0 | capVu0AddVectorXYZ | 1,311,776 | `LQC2 $vf4,...($a1)` | F + B(sin confirmar) |
| 0x0019FBD0 | capVu0SubVectorXYZ | 1,310,736 | `LQC2 $vf4,...($a1)` | F + B(sin confirmar) |
| 0x0016D8E0 | Get_light_str | 524,304 | `LQC2` (a0-based) | F + B(sin confirmar) |
| 0x001A0300 | capVu0Compare | 262,152 | `LQC2` (a0-based) | F + B(sin confirmar) |

Todas: 0 xrefs directas reales (el único "xref" que Ghidra reportaba era
`from=0x00000000 type=EXTERNAL`, un artefacto sintético, no una llamada real);
0 coincidencias de construcción `lui+ori/addiu`; 2-3 apariciones como dato
crudo, todas en `.mwcats` (metadata del compilador, no del juego).

## Próximos pasos (no cerrados todavía)

1. Correr `pipeline.py run` y revisar si hay logs `[dmc-stub-instrumentation]`
   (confirmaría uso real) o silencio total (reforzaría, sin todavía probar del
   todo, que no se alcanzan en el camino de boot/menú).
2. Si aparecen llamadas: son prioritarias, implementarlas de verdad (no basta
   con contarlas).
3. Considerar instalar un módulo de lenguaje R5900 dedicado para Ghidra (existe
   trabajo de la comunidad de reversing de PS2 al respecto) para obtener
   límites de función reales en vez de depender de heurísticas — inversión
   mayor, no bloquea M0-M2, revisar en M4+ (primera imagen visible) si estas
   funciones resultan relevantes para render/cámara.
