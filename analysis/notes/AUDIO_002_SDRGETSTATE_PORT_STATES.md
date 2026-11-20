# AUDIO_002 — `SdrGetState(0x80000006)` y los estados de puerto TQ/SQ (pasos de Dante)

- **Fecha:** 2026-11-18 – 2026-11-19 (AUDIO_001, AUDIO_002-0 y fase E de AUDIO_002-1/2).
- **ELF:** `SLES_503.58` PAL, SHA-256 `d0753a6b…bab4`, CRC32 `77654AD2`, entry `0x00100008`.
- **Base:** `master` `3f212b11`, vendor `247af590` (36 parches), exe `f9b98c83…`.
- **Estado:** causa localizada; **corrección no implementada** (decisión explícita: no responder todavía a la RPC
  ni forzar valores en memoria).
- Nota hermana: [AUDIO_002_1_2_MIXER_CLOCK.md](AUDIO_002_1_2_MIXER_CLOCK.md) (parche 37, solo salida de audio).

Clasificación: **HECHO**, **INFERENCIA**, **HIPÓTESIS**, **UNKNOWN**.

## 1. Contexto: estado del audio (AUDIO_001)

**HECHO** (corridas desde checkpoints de menú, oficina y castillo): el único audio que sale hoy por el dispositivo
es la voz XAG del servicio TSNDDRVM (parche 17).

| Tipo | ¿Lo pide el juego? | ¿Llegan los datos? | ¿Sale audio? |
|---|---|---|---|
| Voz XAG (`0x1B7`/`0x1B8`) | sí | sí (lectura de la ISO) | **sí** |
| Efectos SE (scripts TQ sobre bancos HD/BD) | sí | sí (SPU RAM modelada de CDMODULE) | no |
| Música BGM (secuencias SQ) | sí (`20 00 80` al entrar al castillo) | sí | no |
| Audio de películas PSS (PCM 48 kHz) | sí | sí (heap IOP simulado) | no |

## 2. Contabilidad de comandos (AUDIO_002-0, HECHO)

Instrumentación temporal (ya retirada) de las funciones `Sdr*` del EE y del servicio TSNDDRVM:

- **Sin pérdidas EE → IOP:** en todos los opcodes SID 0 observados, las llamadas EE coinciden con los comandos que
  recibe TSNDDRVM. En el castillo, `SdrSeReq` 82 = 82 comandos `07` y `SdrSeChg` 984 = 984 `0B`.
- **Castillo:** 1 066 comandos SE en 2 068 ticks: 6 canales (`09`–`0E`) reciben `0B` cada 12 ticks y `07`
  periódicos, también corriendo, atacando y en pausa. Ninguno está modelado.
- **Menú y Opciones:** 3 SE de cursor y 8 `SetRev` (2 por cambio de película, uno por core). No se pide BGM.
- **Ritmo:** 1 consulta SID 1 por tick en menús y gameplay; 1 cada 2 ticks en películas.

## 3. Los pasos se descartan en el EE (HECHO + INFERENCIA)

1. **HECHO** (código generado): `SndSeCall@0x1A18D4–0x1A18EC` calcula `s0 = ((a0 & 0x3FFF) >> 8) + 4` y sale si
   `0x742C30[s0] != 1`. Las llamadas a `SdrGetState`, `SdrSeChg` y `SdrSeReq` quedan detrás de esa comprobación.
2. **HECHO** (instrumentación, castillo): `SndSeCall(0x401/0x8401)` se llama con `s0 = 8` y `0x742C30[8] = 0`, así
   que sale sin enviar nada al IOP.
3. **HECHO** (código generado): `SndSceneBgmStart@0x1A1E64–0x1A1E88` rellena `tabla[4..9]` con
   `SdrGetState(0x80000006, n)` para `n = 4..9`.
4. **HECHO:** el HLE de TSNDDRVM no atiende `SID 1` `0x80000006`. El fallback de RPC pone a cero el búfer de
   recepción, así que el juego recibe un 0 ficticio.
5. **INFERENCIA:** las llamadas `0x401`/`0x8401` son pasos: alternan izquierda y derecha cada ~25–35 ticks solo
   mientras Dante corre, y la ruta pasa por `foot_se_chk@0x1A3360`.

**Primera divergencia:** la respuesta de `SdrGetState(0x80000006)`, no la falta de motor SE. Con un motor SE
completo, los pasos seguirían sin pedirse. Cualquier efecto que pase por `SndSeCall` con `s0` entre 4 y 9 está
bloqueado igual; los ambientales (`SndEff*`) no pasan por la tabla y sí llegan al IOP.

## 4. Significado de la consulta en el IRX (HECHO, lectura de `TSNDDRVM.IRX`)

- `TsndPutFunc` despacha por la tabla de `.rodata 0xA7E0`; la entrada 6 va a `0x5074`.
- El manejador llama a `get_port(n)` (`0x31AC`, índice directo si `n` es menor que el número de puertos) y responde
  `PortInfo[n].state` (`0xE330 + 8n`):
  - −1 si el puerto no es válido;
  - `0x10` mientras se carga;
  - si no, el estado.
- Quién fija el estado:
  - `TsndPortLoadInit` lo pasa a «cargando» (`+0x10`);
  - `snd_load_tqinit` → **1** (TQ: scripts de efectos);
  - `snd_load_sqinit` → **2** (SQ: secuencias de música).
- **`CDMODULE.IRX` importa** de `tsnddrv` las exportaciones #4–#14, entre ellas #5 `TsndVabRegist`, #6
  `snd_load_sqinit`, #7 `snd_load_tqinit`, #8 `TsndPortLoadInit` y #13 `TsndPortInit`. Es CDMODULE quien registra
  los bancos en TSNDDRVM al cargarlos.

## 5. Estados reales en los savestates del usuario (PCSX2, HECHO)

Lectura estática, en solo lectura, de los `.p2s` del usuario (`SLES-50358 (7D8F539A).01–09`) con el lector del
proyecto. TSNDDRVM está cargado en IOP `0x98630` con 10 puertos.

| Savestate | Escena | EE `0x742C30[4..9]` | `PortInfo[0..9].state` |
|---|---|---|---|
| 02 | oficina (XAG `[40,+272)` preparado) | `1,1,0,1,0,1` | `0,0,0,0,1,1,0,1,0,1` |
| 03–05 | carga en curso | `0,0,0,0,0,0` | puerto 5 = `0x10` (cargando) |
| 07, 08 | oficina, XAG sonando (estado 3) | `1,1,0,1,0,1` | `0,0,0,0,1,1,0,1,0,1` |
| 09 | castillo con control | **`1,1,0,1,1,1`** | `2,0,0,0,1,1,0,1,1,1` |
| RECOMP (checkpoint del castillo) | castillo | `0,0,0,0,0,0` | — |

- **HECHO:** en todos los savestates donde la escena está cargada, la tabla del EE es **idéntica** a
  `PortInfo[4..9]` de TSNDDRVM.
- **HECHO:** en el castillo original `tabla[8] = 1` (banco TQ registrado en el puerto 8), y el puerto 0 vale 2
  (secuencia SQ).
- **INFERENCIA:** en el original, `SndSeCall(0x401)` supera la comprobación y emite el SE de los pasos.

## 6. Qué haría falta para corregirlo (no implementado)

1. Que TSNDDRVM atienda `SID 1` `fno 6` devolviendo `PortInfo[n].state` según la semántica del IRX (§4).
2. Que el HLE de CDMODULE registre los bancos en el HLE de TSNDDRVM, como hacen las importaciones #5–#8 y #13 del
   original. Sin esto, `fno 6` devolvería «sin cargar» aunque se implemente.
3. Validación: el checkpoint del castillo ya contiene la tabla a cero (`SndSceneBgmStart` se ejecutó antes del
   tick 26 640). Hará falta un checkpoint anterior a la carga de la escena, o volver a entrar en ella, y comparar la
   tabla con el savestate 09.

Aunque se haga, los pasos solo **llegarían** al IOP. Para que suenen falta además un motor SE (scripts TQ sobre
bancos HD/BD), que no existe todavía.

## 7. Correcciones registradas

| Se decía | Evidencia que lo apoyaba | Evidencia nueva | Estado |
|---|---|---|---|
| AUDIO_001: la traza `[IOP/RPC trace:unhandled]` solo existe con `PS2X_ENABLE_IOP_RPC_TRACE`, así que estas consultas no dejan rastro | no había líneas de esa traza en las corridas | `CMakeLists.txt` la fuerza a `ON`; aparece en los arranques en frío de `gameplay_001j` (`sid=0x1 rpc=0x80000006`) | **refutado:** la traza está activa; desde los checkpoints no se llega a consultas sin atender |
| AUDIO_001: el agravante de los SE es que `SdrGetState(4)` recibe 0 | lectura estática de `SndSeCall` | `SdrGetState(4)` no llega a llamarse: la puerta anterior es `tabla[s0]` | **corregido:** el bloqueo decisivo es `0x80000006` |
| AUDIO_002-0: el significado de `fno 6` es UNKNOWN; el original «devolvería 1 para los bancos cargados» (INFERENCIA) | valor exigido por `SndSeCall` | lectura del IRX (§4) y savestates (§5) | **confirmado:** devuelve el estado del puerto (1 = TQ, 2 = SQ) |

## 8. Limitaciones

- PCSX2 no se ejecutó en vivo: los estados son lecturas estáticas de savestates. La relación causal entre
  `tabla[8] = 1` y el sonido de los pasos en el original es INFERENCIA (encaja con el código, pero no se observó
  la emisión del SE).
- Los contadores del EE se limitaron a funciones que no se bloquean.
