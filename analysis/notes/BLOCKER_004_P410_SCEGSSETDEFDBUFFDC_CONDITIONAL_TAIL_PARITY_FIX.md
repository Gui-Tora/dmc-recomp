# P4.1.10 — sceGsSetDefDBuffDc: paridad con la cola condicional original

Fecha: 2026-09-13. Resultado: **FIX_CHECKPOINT_WITH_KNOWN_CAVEATS**, commit
warranted.

## Estado y alcance

- Main HEAD antes de este commit: `5c0881b77d3ae1f7f317d2f41b1824fc61451a19`.
- Vendor HEAD antes de este fix: `19911ce35fb8f2029853f25612b1cc420a8c74bb`,
  limpio. Después de este fix: `0efd17c3cdb834ac13c087dcb2bd947d9fc033b3`
  (mismo baseline pinneado `14b1e5cb39b4af7e6fc12f9a29fdc751efde49d7`, cadena
  de 9 patches, reconstruido de forma determinista — ver sección "Cadena de
  reconstrucción" más abajo).
- Único archivo de producción tocado: `vendor/PS2Recomp/ps2xRuntime/src/lib/Kernel/Stubs/GS.cpp`
  (`sceGsSetDefDBuffDc`). Instrumentación de diagnóstico temporal en
  `gs_cpu_backend.cpp` (`DMC_P410_TRACE`) usada solo para las corridas de
  validación y revertida (`git checkout --`) antes de compilar el binario
  final de producción.
- No se tocó el generador, ni `analysis/local/symtabfirst/generated/`, ni
  ningún archivo `.cpp` generado (~10.000). Build vía MSBuild directo sobre
  `ps2_runtime.vcxproj` + `ps2EntryRunner.vcxproj /t:_BuildLinkAction`,
  confirmado por `grep -c "CL.exe"`/`grep -c "symtabfirst/generated"` en 0
  para el build final.

## Causa raíz

La función SDK real `sceGsSetDefDBuffDc` (ELF `original/SLES_503.58`,
`0x101B48-0x101E2C`, 740 bytes) no retorna con `disp[1]`/`draw01`/`draw02`
dejados en el valor por defecto `FBP=0` que producen sus cuatro llamadas
internas a `seedGsDrawEnv*`/`seedGsDefDispEnv*`. Termina con una **cola
condicional** (`0x101D70-0x101DF8`, recuperada por desensamblado estático de
bytes en P4.1.8) que parchea, preservando todos los bits no-FBP vía
enmascarado `and`+`or`:

- `disp[1].dispfb.FBP`
- `draw01.FRAME.FBP`
- `draw02.FRAME.FBP`

a `frameBase = (zbufAddr>>1)&0x1FF` — la dirección real del "segundo
framebuffer" — **si y solo si** `(interlace==1 && ffmode==1) ||
interlace==0`, leído de `sceGsGetGParam` (`0x004FBC90`). `disp[0]`,
`draw11`, `draw12` **nunca** son tocados por esta cola: se quedan en el
`FBP=0` de los helpers.

Esta HLE (`GS.cpp`) inicializaba SIEMPRE los cuatro campos (`disp[1]`,
`draw01`, `draw02` incluidos) en `FBP=0` literal, sin implementar la cola.
Para DMC, `g_gparam = {interlace=1, omode=2, ffmode=1, version=3}` (fijo,
`Support.h`), de modo que la condición de la cola se cumple siempre — el
parche debía aplicarse en el 100% de las invocaciones del juego, y no lo
hacía.

## Fix

```diff
diff --git a/ps2xRuntime/src/lib/Kernel/Stubs/GS.cpp b/ps2xRuntime/src/lib/Kernel/Stubs/GS.cpp
index 44e2ea9..7dc6904 100644
--- a/ps2xRuntime/src/lib/Kernel/Stubs/GS.cpp
+++ b/ps2xRuntime/src/lib/Kernel/Stubs/GS.cpp
@@ -914,14 +914,36 @@ namespace ps2_stubs
         // lower half. ZBUF is unaffected (ZMSK=1, no Z writes occur); this was
         // purely a COLOR FRAME addressing bug, the second instance of the same
         // root-class defect P4.1 already fixed for the display env (dispfb0).
+        // draw11/draw12 (slot 1) and disp[0] are NEVER touched by the original
+        // SDK's conditional tail either (see below) -- they stay literal 0.
         const uint32_t fbp1 = 0u;
+        // P4.1.10 fix (...): the real sceGsSetDefDBuffDc (0x101B48-0x101E2C
+        // in the ELF) does not return with disp[1]/draw01/draw02 left at the
+        // helper defaults of FBP=0 -- it ends with a conditional tail
+        // (0x101D70-0x101DF8, recovered by static disassembly in P4.1.8 and
+        // confirmed by a live PCSX2 EE-debugger oracle in P4.1.9) that
+        // patches exactly those three fields' FBP to (zbufAddr>>1)&0x1FF,
+        // while leaving disp[0]/draw11/draw12 untouched. The condition,
+        // read from the same g_gparam this runtime already tracks:
+        //   (interlace == 1 && ffmode == 1) || (interlace == 0)
+        const bool gsDBuffDcTailCondition =
+            (g_gparam.interlace == 1u && g_gparam.ffmode == 1u) || (g_gparam.interlace == 0u);
+        const uint32_t gsDBuffDcTailFbp = gsDBuffDcTailCondition ? ((zbufAddr >> 1) & 0x1FFu) : 0u;
         const uint64_t dispfb0 = makeDispFb(0u, fbw, psm, 0u, 0u);
-        const uint64_t dispfb1 = makeDispFb(0u, fbw, psm, 0u, 0u);
+        const uint64_t dispfb1 = makeDispFb(gsDBuffDcTailFbp, fbw, psm, 0u, 0u);

         GsDBuffDcMem db{};
         ...
-        seedGsDrawEnv1(db.draw01, drawWidth, drawHeight, 0u, fbw, psm, zbufAddr, zpsm, ztest, false);
-        seedGsDrawEnv2(db.draw02, drawWidth, drawHeight, 0u, fbw, psm, zbufAddr, zpsm, ztest, false);
+        seedGsDrawEnv1(db.draw01, drawWidth, drawHeight, gsDBuffDcTailFbp, fbw, psm, zbufAddr, zpsm, ztest, false);
+        seedGsDrawEnv2(db.draw02, drawWidth, drawHeight, gsDBuffDcTailFbp, fbw, psm, zbufAddr, zpsm, ztest, false);
         db.giftag1 = db.giftag0;
         seedGsDrawEnv1(db.draw11, drawWidth, drawHeight, fbp1, fbw, psm, zbufAddr, zpsm, ztest, false);
         seedGsDrawEnv2(db.draw12, drawWidth, drawHeight, fbp1, fbw, psm, zbufAddr, zpsm, ztest, false);
```

Diff completo: `patches/BLOCKER_004_p4110_scegssetdefdbuffdc_conditional_tail.patch`.
La regla se expresa únicamente en términos de `zbufAddr` y `g_gparam` — es
una regla semántica del SDK, no un caso especial de DMC (no referencia
ancho/alto/dirección fijos).

## Ledger de correcciones

- **P4.1 (`BLOCKER_004_P41_...`)**: `disp[0].dispfb.FBP=0` — **sigue
  correcto**; la cola original nunca toca `disp[0]`.
- **P4.1.3 (`BLOCKER_004_P413_...`)**: `draw11`/`draw12` (slot1)
  `FRAME.FBP=0` — **sigue correcto**; la cola original nunca toca el slot1.
- **P4.1.5 (`BLOCKER_004_P415_...`, no comprometido)**: probó
  `fbp1=zbufAddr/2` para el slot1 — **refutado**: rompía la mitad superior
  del buffer de película (mismo mecanismo de aliasing de páginas, desplazado).
  Confirma por contraste que el parche pertenece al slot0 (`draw01`/`draw02`),
  no al slot1.
- **P4.1.6** (solo referenciado por P4.1.9, no un informe propio de esta
  cadena numerada): concluía que el contrato original era "FBP=0 universal"
  — **retractado por P4.1.8**: se le escapó la cola condicional al no seguir
  el flujo de control más allá de las cuatro llamadas `seedGsDrawEnv*`.
- **P4.1.8 (`BLOCKER_004_P418_...`)**: recuperó la cola condicional completa
  por desensamblado directo de bytes del ELF — **confirmado**, promovido de
  INFERENCIA-por-bytes a HECHO por P4.1.9-addendum.
- **P4.1.9 (`BLOCKER_004_P419_...` + adenda)**: el informe original
  concluyó correctamente `P419_LIVE_ORACLE_BLOCKED` en su momento (sin
  acceso a debugger). El usuario completó la medición manualmente después,
  documentada en el adenda sin reescribir el informe original — los doce
  campos (6 UI + 6 película) coinciden byte a byte con la predicción
  estática de P4.1.8.

## Cadena de reconstrucción determinista del vendor

Procedimiento estándar de esta sesión: clon local del vendor real (evita red),
`checkout --no-checkout` + `core.autocrlf=false` antes de cualquier checkout
del baseline pinneado, aplicación en orden de los 9 patches (`git apply
--check` + `git apply`), **`git add -A`** (paso antes omitido en un intento
previo fallido de esta misma sesión — `git write-tree` lee del índice, no
del árbol de trabajo; sin este paso el commit generado reproducía el
baseline sin parches) para sincronizar el índice con el árbol de trabajo
parcheado, `git write-tree` + `git commit-tree -p <baseline>` con
`patch_identity` fija, verificación `diff -rq --exclude=.git` byte-exacta
contra el árbol real del vendor (dirty, con el fix de `GS.cpp` sin comprometer),
y normalización del checkout real del vendor a ese mismo commit vía `git
fetch` (del clon local) + `git reset` (mixed) — nunca `reset --hard`, ya que
el árbol de trabajo real ya coincidía exactamente con el árbol objetivo.
`git diff 19911ce 0efd17c` confirma que el único cambio de árbol entre el
`patched_commit` anterior y el nuevo es exactamente el diff de `GS.cpp`
mostrado arriba (1 archivo, 29 inserciones, 7 eliminaciones).

## Validaciones

Entorno de cada corrida (obligatorio, verificado en el mismo lanzamiento):
`PS2X_CD_IMAGE`, `PS2X_RUNTIME_ARENA_BASE=0x009FA000`,
`PS2X_RUNTIME_ARENA_LIMIT=0x00ABE000`, `DMC_P313_INIT_MODE=ownership`,
`DMC_P314_SYNC_REDECODE=1`. Gate de validez de cada corrida: `[P314:GP]` y
`[P3142:ring]` presentes, `[GP:H]` ausente — confirmado en las 4 corridas
(RUN_077/078/079/080).

### A — Contrato de RAM guest (byte-exacto contra el oracle de P4.1.9)

UI (`s0+0xD0=0x740770`) y película (`0x740C30`, valor post-`Main_init`):

| Campo | Dirección | Esperado | RUN_077 | RUN_078 | RUN_079 | RUN_080 (binario limpio) |
|---|---:|---:|---:|---:|---:|---:|
| UI disp0.FBP | `0x740780` | `0x00` | `0x00` | `0x00` | `0x00` | `0x00` |
| UI disp1.FBP | `0x7407B8` | `0x38` | `0x38` | `0x38` | `0x38` | `0x38` |
| UI slot0 F1.FBP | `0x7407F0` | `0x38` | `0x38` | `0x38` | `0x38` | `0x38` |
| UI slot0 F2.FBP | `0x740870` | `0x38` | `0x38` | `0x38` | `0x38` | `0x38` |
| UI slot1 F1.FBP | `0x740960` | `0x00` | `0x00` | `0x00` | `0x00` | `0x00` |
| UI slot1 F2.FBP | `0x7409E0` | `0x00` | `0x00` | `0x00` | `0x00` | `0x00` |
| Movie disp0.FBP | `0x740C40` | `0x00` | `0x00` | `0x00` | `0x00` | `0x00` |
| Movie disp1.FBP (post-patch) | `0x740C78` | `0xA0` | `0xA0` | `0xA0` | `0xA0` | `0xA0` |
| Movie slot0 F1.FBP (post) | `0x740CB0` | `0xA0` | `0xA0` | `0xA0` | `0xA0` | `0xA0` |
| Movie slot0 F2.FBP (post) | `0x740D30` | `0xA0` | `0xA0` | `0xA0` | `0xA0` | `0xA0` |
| Movie slot1 F1.FBP | `0x740E20` | `0x00` | `0x00` | `0x00` | `0x00` | `0x00` |
| Movie slot1 F2.FBP | `0x740EA0` | `0x00` | `0x00` | `0x00` | `0x00` | `0x00` |

4/4 corridas byte-exactas, incluido el binario de producción sin
instrumentación (RUN_080), lo que descarta que el resultado dependa del
diagnóstico compilado.

### B — Destinos de draw (`[P410:DRAW]`, gated por `DMC_P410_TRACE`)

| Corrida | fbp=0x0 | fbp=0x38 | fbp=0xa0 | fbp=0xe0 (bug antiguo) | fbp=0x70 (valor SDK sin parchear por `Main_init`) |
|---|---:|---:|---:|---:|---:|
| RUN_077 | 17355 | 17126 | 31 | **0** | **0** |
| RUN_078 | 17084 | 16854 | 32 | **0** | **0** |
| RUN_079 | 16812 | 16582 | 32 | **0** | **0** |

Confirma: el par UI nuevo `{0x38,0}` aparece como se esperaba, el destino
de película `0xA0` aparece consistentemente, y **0 draws en las 3 corridas**
apuntan al valor roto anterior (`0xE0`) o al valor SDK pre-parche de
`Main_init` (`0x70`) — el parche guest de `Main_init` sigue superando
completamente al valor devuelto por el HLE, sin fuga operacional.

### D — Integridad de framebuffer (`[P410:CD]`, muestreo de VRAM cruda)

Por bloques de 64 filas (0-3 = mitad superior, filas 0-255; 4-6 = mitad
inferior, filas 256-447), agregados por `dispFbp`:

| Corrida | dispFbp | N muestras | mitad superior 100% negra | mitad inferior 100% negra |
|---|---|---:|---:|---:|
| RUN_077 | `0xa0` | 53 | **0** | **0** |
| RUN_078 | `0xa0` | 56 | **0** | **0** |
| RUN_079 | `0xa0` | 55 | **0** | **0** |

**0/164 muestras totales del buffer de película muestran corrupción
sistemática en ninguna mitad** — ni la corrupción de mitad superior de
P4.1.5/RUN_073 (`fbp1=zbufAddr/2` mal aplicado) ni la de mitad inferior del
bug original pre-P4.1.3. Las cifras de `dispFbp=0x0`/`0x38` (UI) muestran
negro parcial en ~55-65% de las muestras, consistente con contenido legítimo
de UI (pantallas de menú con fondo negro y texto disperso), no con un patrón
de corrupción binario/sistemático.

### E — Comparación visual

| Elemento | RUN_077 | RUN_078 | RUN_079 | Baseline previa |
|---|---|---|---|---|
| Memory Card | Completo, legible | Completo, legible | Completo, legible | RUN_063 intermitente, RUN_071 negro |
| Language Select | Completo, legible | Completo, legible | Completo, legible | RUN_073 intermitente |
| Video advertencia (5 idiomas) | Completo, sin corrupción | Completo, sin corrupción | Completo, sin corrupción | RUN_073 corrupción superior, pre-P4.1.3 corrupción inferior |

Sin flicker anómalo detectado: las ventanas negras prolongadas (20-69s, entre
Language Select y el inicio del FMV) corresponden a un estado estático de
espera "Press Start" (cursor blanco visible en la esquina en al menos un
frame muestreado), no a parpadeo intermitente entre contenido y negro.

### F — Reproducibilidad

**3/3** corridas de validación completas (RUN_077, RUN_078, RUN_079) con
instrumentación de diagnóstico, más **1/1** corrida adicional de humo
(RUN_080) con el binario de producción limpio (sin `DMC_P410_TRACE`
compilado) — 4/4 total, todas con contrato de RAM guest byte-exacto, todas
`"result": "SUCCESS"`, todos los hitos (`MC_CHECK`→`MOVIE_START`→
`PSS_REACHED`→`MPEG_INIT`→`TARGET`) en el orden correcto.

## Residuales (no cerrados por este fix)

- `PMODE`/`AMOD` (bit6): deliberadamente no tocado, deferido desde P4.1.
- P4.2.1 (backpressure del ring ES de MPEG): independiente de este fix,
  sigue abierto — ver `analysis/notes/BLOCKER_004_P42_...md`.
- BLOCKER_004 **no se declara cerrado** por este commit.

## Cierre

Único archivo de producción modificado:
`vendor/PS2Recomp/ps2xRuntime/src/lib/Kernel/Stubs/GS.cpp` (vía patch
`patches/BLOCKER_004_p4110_scegssetdefdbuffdc_conditional_tail.patch`).
Instrumentación de diagnóstico (`gs_cpu_backend.cpp`) revertida antes del
build final. `upstream.lock.json` actualizado (`patches[]` + `patched_commit`).
Vendor real normalizado y limpio en el nuevo `patched_commit`. No se tocó el
generador ni archivos generados. Build final verificado (0 `CL.exe` de
archivos generados, 0 referencias a `symtabfirst/generated` en el log).
CHECKPOINT_DECISION: **COMMIT_WARRANTED**.
