# Índice histórico de investigación — dmc-recomp

Orden cronológico. Estado actual resumido: [GAMEPLAY_CURRENT_STATUS.md](GAMEPLAY_CURRENT_STATUS.md).
Las fechas proceden de las propias notas o, en su defecto, del historial de Git.
«Local» significa que el código o resultado existe en el árbol de trabajo del mantenedor, pero no está
versionado.

## 1. Infraestructura y límites de función (2026-09-07 – 09-09)

| ID | Problema | Descubrimiento principal | Estado | Documento |
|---|---|---|---|---|
| Scaffold | estructura, lock de upstream, pipeline | baseline `14b1e5c`; Game Overrides ligados a ELF/entry/CRC | cerrado | [../UPSTREAM.md](../UPSTREAM.md) |
| HUGE_GENERATED_FILES | archivos generados gigantes que no compilan | límites Ghidra R6 erróneos en funciones VU0 → triage con `ret0` (origen de los 20 `ret0`) | histórico; afirmación «0 llamadores JAL» **RETRACTADA** en [GAMEPLAY_001_RET0_AUDIT.md](GAMEPLAY_001_RET0_AUDIT.md) | [HUGE_GENERATED_FILES.md](HUGE_GENERATED_FILES.md) |
| BLOCKER_001 | bucle en `0x100088` | hueco `DI/EI` sin función en el CSV de Ghidra | **RESUELTO** | [BLOCKER_001_entry_di_gap.md](BLOCKER_001_entry_di_gap.md) |
| SYMTAB_BASELINE | ¿qué fuente de límites usar? | `.symtab` del ELF: 0 solapes, 0 funciones gigantes; pasa a ser el baseline | **adoptado** | [SYMTAB_BASELINE_PROMOTION.md](SYMTAB_BASELINE_PROMOTION.md) |
| OBJECT_TABLE | tablas de trabajo en `0x0110E720` | hipótesis de estructura de objetos | hipótesis no confirmada | [OBJECT_TABLE_0x0110e720.md](OBJECT_TABLE_0x0110e720.md) |

## 2. IOP / CD / película (2026-09-08 – 09-23)

| ID | Problema | Descubrimiento principal | Estado | Documento |
|---|---|---|---|---|
| BLOCKER_002 | salto indirecto a `0x2000100` en `Print_message` | servicio CD-read `sid=0x12345678` sin HLE → corrupción de memoria; `CdModuleService` mínimo (`fno=2 mode=1`) | **RESUELTO** (auditado) | [BLOCKER_002_indirect_jump_top_of_ram.md](BLOCKER_002_indirect_jump_top_of_ram.md), [BLOCKER_002_CHECKPOINT_FASE_E.md](BLOCKER_002_CHECKPOINT_FASE_E.md), [BLOCKER_002_ASTRA_AUDIT.md](BLOCKER_002_ASTRA_AUDIT.md) |
| BLOCKER_003 | película PSS sin transporte CD | RPC `fno=9/0xA/0xC/0xD` de streaming implementados | **RESUELTO** (solo transporte) | [BLOCKER_003_cdmodule_movie_streaming.md](BLOCKER_003_cdmodule_movie_streaming.md) |
| BLOCKER_004 | datos PSS llegan al EE sin vídeo visible | heap/arena, orden de lotes del scheduler, propiedad de `sceMpegInit`, recirculación de `viBuf`, `sceGsSetDefDBuffDc`, presentación FIELD | formalmente **OPEN** en su nota; película estable según el usuario (checkpoint PRE_PERF) | [BLOCKER_004_pss_video_output.md](BLOCKER_004_pss_video_output.md), [BLOCKER_004_PRE_PERF_KNOWN_GOOD_CHECKPOINT.md](BLOCKER_004_PRE_PERF_KNOWN_GOOD_CHECKPOINT.md) |
| ARCH_AUDIT_002 | arquitectura MPEG/render | auditoría cruzada de implementación | referencia | [ARCH_AUDIT_002_SOL_CROSS_IMPLEMENTATION_MPEG_RENDER_ARCHITECTURE.md](ARCH_AUDIT_002_SOL_CROSS_IMPLEMENTATION_MPEG_RENDER_ARCHITECTURE.md) |

Subfases de BLOCKER_004 (ver la nota principal para el orden): auditorías Astra/Fable; P3.6, P3.11–P3.15.2,
P4.0–P4.2.4 y Q6–Q10.1:

- [BLOCKER_004_ASTRA_AUDIT](BLOCKER_004_ASTRA_AUDIT.md)
- [P3.6](BLOCKER_004_P36_HEAP_COLLISION_AUDIT.md)
- [P3.11 Astra](BLOCKER_004_ASTRA_P311_OVERNIGHT.md)
- [P3.11.1](BLOCKER_004_FABLE_P3111_CAUSAL_AUDIT.md)
- [P3.12](BLOCKER_004_P312_SCHEDULER_BATCH_ORDER.md)
- [P3.13](BLOCKER_004_P313_SCEMPEGINIT_OWNERSHIP_MATRIX.md)
- [P3.13.1](BLOCKER_004_P3131_VISUAL_LANGUAGE_TRACE.md)
- [P3.14](BLOCKER_004_P314_SYNCHRONOUS_GUEST_ES_REDECODE.md)
- [P3.14.1](BLOCKER_004_P3141_FABLE_SYNC_MPEG_AUDIT.md)
- [P3.14.2](BLOCKER_004_P3142_VIBUF_CONSUMPTION_AND_OWNERSHIP.md)
- [P3.15](BLOCKER_004_P315_GETPICTURE_PRESENTATION_CONTRACT.md)
- [P3.15.1](BLOCKER_004_P3151_GUEST_PRESENTATION_TRACE.md)
- [P3.15.2](BLOCKER_004_P3152_MOVIE_TAG_CHAIN_IMAGEADDR_DATAFLOW.md)
- [P4.0F](BLOCKER_004_P40_FABLE_GS_PRESENTATION_CAUSAL_BASELINE.md)
- [P4.0.1](BLOCKER_004_P401_MOVIE_DISPLAY_ENV_RECONCILIATION.md)
- [P4.0.2](BLOCKER_004_P402_SCEGSSETDEFDBUFFDC_ROOT_CAUSE.md)
- [P4.1](BLOCKER_004_P41_SCEGSSETDEFDBUFFDC_DISPFB0_ZERO_FIX.md)
- [P4.1F](BLOCKER_004_P41F_DISPFB0_FIX_AND_RESIDUAL_FLICKER_AUDIT.md)
- [P4.1.1](BLOCKER_004_P411_MOVIE_FRAME_COMPLETENESS_BOUNDARY_TRACE.md)
- [P4.1.1R](BLOCKER_004_P411R_P314_BASELINE_REPRODUCIBILITY.md)
- [P4.1.1B](BLOCKER_004_P411B_MOVIE_FRAME_COMPLETENESS_BOUNDARY_TRACE_RETRY.md)
- [P4.1.2](BLOCKER_004_P412_FABLE_FBPA0_PARTIAL_UPLOAD_ROOT_CAUSE_AUDIT.md)
- [P4.1.3](BLOCKER_004_P413_SCEGSSETDEFDBUFFDC_DRAWENV_FRAME_FIX.md)
- [P4.1.4](BLOCKER_004_P414_FABLE_CLEAN_PRE_PSS_BLACK_SCREEN_ROOT_CAUSE.md)
- [P4.1.5](BLOCKER_004_P415_SCEGSSETDEFDBUFFDC_DRAWENV_SECOND_BUFFER_FIX.md)
- [P4.1.6](BLOCKER_004_P416_FABLE_ORIGINAL_SCEGSSETDEFDBUFFDC_DRAWENV_ORACLE.md)
- [P4.1.7](BLOCKER_004_P417_PRE_PSS_SINGLE_BUFFER_PRESENTATION_COHERENCE.md)
- [P4.1.8](BLOCKER_004_P418_ASTRA_VISUAL_REGRESSION_TIMELINE_COMPARATIVE_AUDIT.md)
- [P4.1.9 Astra](BLOCKER_004_P419_ASTRA_ORIGINAL_UI_DBUFF_CONDITIONAL_TAIL_ORACLE.md)
- [P4.1.9 addendum](BLOCKER_004_P419_ADDENDUM_MANUAL_ORACLE_COMPLETION.md)
- [P4.1.10](BLOCKER_004_P410_SCEGSSETDEFDBUFFDC_CONDITIONAL_TAIL_PARITY_FIX.md)
- [P4.2](BLOCKER_004_P42_FABLE_MPEG_ES_RING_RECIRCULATION_ROOT_CAUSE.md)
- [P4.2.1](BLOCKER_004_P421_MPEG_DEMUX_VIBUF_BACKPRESSURE_FIX.md)
- [P4.2.2](BLOCKER_004_P422_VIBUF_BLOCK_BYTE_ACCOUNTING_ROOT_CAUSE.md)
- [P4.2.3](BLOCKER_004_P423_MPEG_DEMUX_CALLBACK_COMPLETION_OWNERSHIP_FIX.md)
- [P4.2.4](BLOCKER_004_P424_MPEG_END_OF_STREAM_VIBUF_DRAIN_BEFORE_DECODER_FLUSH.md)
- [P4.2.4 clasificación](BLOCKER_004_P424_FIRST_DIVERGENCE_VISUAL_CLASSIFICATION.md)
- [Q6](BLOCKER_004_Q6_MPEG_HOST_RGBA_VS_GUEST_POST_GETPICTURE_ORACLE.md)
- [Q7](BLOCKER_004_Q7_GS_STAGE_FIRST_CORRUPTION_ORACLE.md)
- [Q8](BLOCKER_004_Q8_PCSX2_MOVIE_GS_DISPLAY_STATE_ORACLE.md)
- [Baseline limpio](BLOCKER_004_CLEAN_VISUAL_TIMING_BASELINE.md)
- [Q10](BLOCKER_004_Q10_FIELD_BOB_PRESENTATION_ORACLE.md)
- [Q10.1](BLOCKER_004_Q10_1_FIELD_PRESENTATION_FIX.md)

## 3. Checkpoint de desarrollo Main Menu — DEVSTATE (2026-09-24 – 10-02)

| ID | Problema | Descubrimiento principal | Estado | Documento |
|---|---|---|---|---|
| DEVSTATE_001 | ¿se puede guardar/restaurar el estado en Main Menu? | auditoría de arquitectura y gate de fase | cerrado | [DEVSTATE_001_MAIN_MENU_CHECKPOINT.md](DEVSTATE_001_MAIN_MENU_CHECKPOINT.md) |
| DEVSTATE_001A | punto seguro | factibilidad del *safe-point* | cerrado | [DEVSTATE_001A_MAIN_MENU_SAFEPOINT_FEASIBILITY.md](DEVSTATE_001A_MAIN_MENU_SAFEPOINT_FEASIBILITY.md) |
| DEVSTATE_002 | formato | diseño del serializador | normativo | [DEVSTATE_002_CHECKPOINT_SERIALIZATION_DESIGN.md](DEVSTATE_002_CHECKPOINT_SERIALIZATION_DESIGN.md) |
| DEVSTATE_003A–C | implementación | núcleo, scheduler/GS y HLE completo; cold load hasta Main Menu | cerrado | [003A](DEVSTATE_003A_CHECKPOINT_CORE_IMPLEMENTATION.md), [003B](DEVSTATE_003B_SCHED_GS_SAVE_BOUNDARY.md), [003C](DEVSTATE_003C_COMPLETE_HLE_AND_COLD_LOAD.md) |
| DEVSTATE_003 | promoción | parche `DEVSTATE_003_main_menu_checkpoint_v1` | **versionado** | [DEVSTATE_003_CHECKPOINT_PROMOTION.md](DEVSTATE_003_CHECKPOINT_PROMOTION.md) |
| DEVSTATE_003D | un relink invalidaba el checkpoint v1 | formato v2: identidad de compatibilidad de estado frente a build ID de procedencia | **versionado** | [DEVSTATE_003D_CHECKPOINT_COMPATIBILITY_ID.md](DEVSTATE_003D_CHECKPOINT_COMPATIBILITY_ID.md) |

## 4. GAMEPLAY_001 — de Main Menu al primer frame jugable (2026-10-02 – 10-14)

| ID | Problema | Descubrimiento principal | Estado | Documento |
|---|---|---|---|---|
| GAMEPLAY_001 (180 s) | pantalla negra tras NEW GAME | bucle caliente en `Eff_amm_set` (hilo 7) | clasificado | [GAMEPLAY_001_MAIN_MENU_TO_FIRST_PLAYABLE_FRAME.md](GAMEPLAY_001_MAIN_MENU_TO_FIRST_PLAYABLE_FRAME.md) («Observación de 180 s») |
| GAMEPLAY_001 s1/s3 | ¿por qué no termina el bucle? | `$s3 = 0xFFFFFFFF80000000` leído de un buffer `fno=2 mode=2` no rellenado | mecanismo demostrado | ídem («Contadores del bucle») |
| GAMEPLAY_001C | ¿qué hace ORIGINAL con `mode=2`? | oráculo PINE: copia el payload de `PL01.PLD` desde el sector 1 y `recv=0xC5F20` | **primera divergencia demostrada** | ídem («GAMEPLAY_001C») |
| GAMEPLAY_001D | implementar `mode&2` | descriptores tipo 0/3 → EE; `0xFF` byte exacto; nuevo bloqueo en `0x103` (tipo 1) | implementado (**local**) | ídem («GAMEPLAY_001D») |
| RET0_AUDIT | ¿los 20 `ret0` se alcanzan? | 1544 `JAL` reales; `capVu0MulMatrix2` es el primer `ret0` alcanzado | cerrado | [GAMEPLAY_001_RET0_AUDIT.md](GAMEPLAY_001_RET0_AUDIT.md) |
| RET0_FIRSTCALL | primera llamada a `capVu0MulMatrix2` | nativa = oráculo ORIGINAL (64 B); `RET0_COUNT = 19` | **validado (local)** | ídem («RET0_FIRSTCALL cerrado») |
| GAMEPLAY_001E | `0x103` mixto EE/IOP/SPU | semántica IRX + oráculo de 4 efectos; ABI 2 del checkpoint | implementado (**local**) | [GAMEPLAY_001E_CDMODULE_MIXED_EE_IOP_SPU.md](GAMEPLAY_001E_CDMODULE_MIXED_EE_IOP_SPU.md) |
| GAMEPLAY_001F | 19 `ret0` + `0x118`/`mode=0xA` | grupos A/B/C; 7 defectos del traductor VU0; modelo CDMODULE 14/14 | auditoría cerrada | [GAMEPLAY_001F_RET0_BATCH_AUDIT.md](GAMEPLAY_001F_RET0_BATCH_AUDIT.md) |
| GAMEPLAY_001G | CDMODULE restante + modo sombra | coincide con ORIGINAL hasta `0x32`; falta `0x232`; Add/Sub 400/400 EXACT | implementado (**local**) | [GAMEPLAY_001G_CDMODULE_AND_RET0_SHADOW.md](GAMEPLAY_001G_CDMODULE_AND_RET0_SHADOW.md) |
| GAMEPLAY_001H | ¿por qué no se pide `0x232`? | **VIF1 CHCR.TTE**: etiquetas REF pierden VIFcodes → DIRECT falso → bloqueo GS; corrección validada 4/4; `vf0.w = 0` en hilos | causa raíz demostrada; corrección **experimental local** | [GAMEPLAY_001H_VIF1_TTE_ROOT_CAUSE.md](GAMEPLAY_001H_VIF1_TTE_ROOT_CAUSE.md) |
| Seguimiento `ret0` | estado de las 20 funciones | tabla viva | vivo | [GAMEPLAY_001_RET0_RESTORATION_STATUS.md](GAMEPLAY_001_RET0_RESTORATION_STATUS.md) |
| GAMEPLAY_001I | promoción TTE, A/B Add/Sub, origen de `vf0` | parche TTE en `patches/` + lock, tests +3; Add/Sub nativas (A/B 2+2 sin regresiones; 17 `ret0`); `vf0 = 0` por creación de contexto (`StartThread`/invocaciones); ecossin 600/600 con `DMC_VF0_FIX` | cerrada; **publicada en la rama `gameplay-001i`** (parches 14 CDMODULE/ABI 2 y 15 TTE, lock `6bea2eb0`) | [GAMEPLAY_001I_TTE_PROMOTION_ADD_SUB_VF0.md](GAMEPLAY_001I_TTE_PROMOTION_ADD_SUB_VF0.md) |
| GAMEPLAY_001J | negro tras la fachada; cinemática de la oficina detenida; silencio total; rendimiento | Parche 16 `vf0` (oficina 3D visible). La cinemática esperaba a `XagChkPlay` del driver de sonido TSNDDRVM, que faltaba para DMC: servicio reconstruido del IRX (parche 17); la cinemática avanza hasta el final de su script (una corrida larga) y la voz XAG suena. Perfil: intérprete VU1 ~83 %, ~3–4 VSync/s. Parche 18: ruta F9 y disparador de checkpoint | **cerrada** (parches 16–18 preparados; nuevo bloqueo `sceDmaRecvI`/`Movie_texture`) | [GAMEPLAY_001J_VISUAL_TRANSITION_DIAGNOSIS.md](GAMEPLAY_001J_VISUAL_TRANSITION_DIAGNOSIS.md) |
| GAMEPLAY_001K | rendimiento: intérprete VU1 ~92 % del tiempo de host en la oficina | Captura de microprogramas (una imagen, `a0dbee2aeac4dafa`; 98 pares calientes) y banco `vu1_replay` (3 779/3 779 exactas). Generador propio con «scoreboard compilado»: 10,2× offline, 0 diferencias. Despachador `PS2X_VU1_NATIVE` con rollback y Verify (parche 19); código generado localmente desde el ELF (`make_vu1c.py`). Oficina 3,29 → 12,48 VSync/s; Verify en el juego: 0 diferencias | **cerrada, fusionada en `master` (PR #4)** (parche 19) | [GAMEPLAY_001K_VU1_PERFORMANCE_DESIGN.md](GAMEPLAY_001K_VU1_PERFORMANCE_DESIGN.md) |
| GAMEPLAY_001L | `sceDmaRecvI` / `Movie_texture`; después, personajes y geometría de la oficina | IPU IDEC + DMA SPR/IPU (parche 20); servidores RPC del IOP fuera de la RAM del EE (21); recorrido DMA completo sin límite de 4096 tags (22); `CLIP`/`DIV`/`FTOI` VU0 en el traductor (23); `UNPACK V4-5` de los pesos de skinning (24); `drained()` del sustituto MPEG sin FFmpeg (25); `ecossin` y `Get_light_str` nativas (15 `ret0`). La ruta pasa de `0x231` a la escena del castillo; en la oficina se ven Trish y el escenario, sin cuña negra | **cerrada, en revisión** (rama `gameplay-001l`; abiertos: franja horizontal de presentación, 6 casos `InterpNode 0/0`, 7 funciones de recorte experimentales; sin primer frame jugable) | [GAMEPLAY_001L_SCEDMARECVI_MOVIE_TEXTURE.md](GAMEPLAY_001L_SCEDMARECVI_MOVIE_TEXTURE.md) |
| siguiente | franja horizontal de presentación; después PERF_001 | — | **propuesta, no iniciada** | [GAMEPLAY_CURRENT_STATUS.md](GAMEPLAY_CURRENT_STATUS.md) §8–9 |

### Fichas de fases sin documento propio (resumen; detalle en la nota GAMEPLAY_001)

- **GAMEPLAY_001C:**
  - Evidencia: PCSX2 2.8.2 con PINE de solo lectura y la ISO.
  - HECHO:
    - mismo request en ORIGINAL y RECOMP (`0xFF`, `LBA 0xEA547`, `size 0xC6800`, `dst 0xD3CE20`, `mode 2`);
    - ORIGINAL copia `PL01.PLD[0x800:]` y responde `0xC5F20`;
    - RECOMP no maneja `mode=2`, deja el destino sin escribir y entra en un bucle caliente en `Eff_amm_set`.
  - Causa raíz: `mode=2` no implementado en el HLE.
  - Evidencia local: `analysis/local/gameplay_001c/` (`pine_oracle.py`, `pine_oracle.log`, `iso_lba.py`).
- **GAMEPLAY_001D:**
  - Implementación de `mode&2` para tipos 0/3; rechazo explícito de tipos 1/2 antes de mutar EE.
  - Validación: `0xFF` `payloadVerify=byte-exact`, `recv=0xC5F20`; `Eff_amm_set` hace 3+4+3 iteraciones,
    como ORIGINAL.
  - Siguiente bloqueo: `0x103` tipo 1.
  - Ejecutable `38e6b964…166d0a`.
  - Evidencia local: `analysis/local/gameplay_001d/`.
  - Código: `cdmodule.cpp`; desde 2026-10-14, parche 14 (`GAMEPLAY_001D_001G_cdmodule_mode2_sound_abi2.patch`).
- **RET0_FIRSTCALL:**
  - Primera llamada `Md_jnt_info_set@0x252F40 → capVu0MulMatrix2@0x1A0080`.
  - El AFTER nativo (64 B) coincide con el oráculo ORIGINAL (SHA-256 `ddbdd3bb…9a47`).
  - Se retiró solo ese `ret0`.
  - Evidencia local: `analysis/local/ret0_firstcall/`.

## 5. Correcciones y retractaciones registradas

| Corrección | Dónde se documenta |
|---|---|
| «0 llamadores JAL» de los 20 `ret0` → 1544 `JAL` reales | [GAMEPLAY_001_RET0_AUDIT.md](GAMEPLAY_001_RET0_AUDIT.md) |
| Grupos `ret0` A=10/B=7 → A=9/B=8 (`Calc_scr_light`) | [GAMEPLAY_001G_CDMODULE_AND_RET0_SHADOW.md](GAMEPLAY_001G_CDMODULE_AND_RET0_SHADOW.md) |
| «`SdrGetState`: eco del argumento» → el fallback pone `recv` a cero | [GAMEPLAY_001H_VIF1_TTE_ROOT_CAUSE.md](GAMEPLAY_001H_VIF1_TTE_ROOT_CAUSE.md) |
| «VU1 reserved `0xFFFFFFFB`» → centinela de desbordamiento XGKICK | ídem |
| Origen de los 0,56 s de `0x32 → 0x232` (PINE 001C, no 001G) | ídem |
| Límites Ghidra de `SndSceneBgmStart` (16 B) frente a `.symtab` (628 B) | ídem |
| Capturas PINE de IOP a cero en 001E → ruta de lectura PCSX2 inválida | [GAMEPLAY_001E_CDMODULE_MIXED_EE_IOP_SPU.md](GAMEPLAY_001E_CDMODULE_MIXED_EE_IOP_SPU.md) |
| Capturas pasivas de ventana en 001G → escritorio, inválidas | [GAMEPLAY_001G_CDMODULE_AND_RET0_SHADOW.md](GAMEPLAY_001G_CDMODULE_AND_RET0_SHADOW.md) |
| Parche TTE mínimo de 001H «suficiente» → requería también conservar TTE en `sceDmaSend` | [GAMEPLAY_001I_TTE_PROMOTION_ADD_SUB_VF0.md](GAMEPLAY_001I_TTE_PROMOTION_ADD_SUB_VF0.md) |
| Origen de `vf0.w = 0` «`StartThread` o checkpoint (UNKNOWN)» → creación de contexto; el checkpoint solo lo perpetúa | ídem |

## 6. Política documental

Ninguna fase se considera documentalmente cerrada si sus hallazgos solo existen en `analysis/local/`. El
procedimiento completo está en [GAMEPLAY_CURRENT_STATUS.md](GAMEPLAY_CURRENT_STATUS.md) §12. Plantilla de
bloqueo: [BLOCKER_TEMPLATE.md](BLOCKER_TEMPLATE.md).

## 7. Verificación de identidad de la ISO adjunta (2026-10-29)

| ID | Problema | Descubrimiento principal | Estado | Documento |
|---|---|---|---|---|
| ISO_IDENTITY_001 | ¿Qué versión/región representa la imagen adjunta? | `SYSTEM.CNF` declara `SLES_503.58`, v1.02, PAL; SHA-256, CRC32 y entry del ELF coinciden con la identidad canónica registrada | **verificada estáticamente**; no implica viabilidad del juego | [ISO_IDENTITY_001_ATTACHED_IMAGE.md](ISO_IDENTITY_001_ATTACHED_IMAGE.md) |
