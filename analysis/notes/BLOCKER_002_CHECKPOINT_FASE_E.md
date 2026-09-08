# BLOCKER_002 — Checkpoint congelado (FASE A → FASE E.2, 2026-09-08)

Resumen cerrado de todo lo demostrado antes de cambiar de metodología
(instrumentación interna → PCSX2 como oráculo, FASE F). Para el detalle
completo con código/logs, ver `BLOCKER_002_indirect_jump_top_of_ram.md` y
`analysis/local/blocker002_checkpoint_faseE/MANIFEST.md`.

## Punto de partida

```
Print_message @ 0x2e11c8: jr v0
v0 = 0x02000100  (fuera de RAM, codeRegion=no)
```

## Lo demostrado, en orden

1. **No es un problema de límites de función (Ghidra/CSV).** El baseline
   usa `.symtab` real del ELF (`Print_message: 0x2e0ed0-0x2e1638`).
   BLOCKER_002 se reproduce igual con symtab-first.
2. El `JR` pertenece a un switch real, correctamente traducido: tabla de
   saltos en `0x586740`, cálculo de índice y dirección correctos.
3. La tabla del ELF estático es correcta; ninguna entrada vale `0x02000100`.
4. En runtime la tabla está corrupta (`table[8]=0x02000100`); el `LW`/`JR`
   hacen exactamente lo que corresponde con la RAM que reciben.
5. **Watchpoint `PAGE_READONLY`+VEH** (sin tocar código generado) localizó
   al escritor: `Print_message`, PC guest `0x2e131c`, `sw v0,0(s1)`.
6. `AddPrim` (función hoja, `0x16efb0-0x16efe0`) **no toca `$s1`** —
   confirmado leyendo el código y con 0 discrepancias en 2M+ llamadas
   instrumentadas.
7. **`$s1` avanza matemáticamente**: inicial `0x00981248`, `+44
   bytes/carácter`, sin ninguna otra modificación oculta
   (`29.382.144 / 44 = 667.776` exacto, sin resto).
8. Al superar los 32MB de RDRAM, las direcciones se envuelven de vuelta a
   `[0x00000000, 0x01FFFFFF]` — `Print_message` termina pisando su propia
   jump table.
9. **Corrección FASE E.1**: `0x7E` NO es terminador (es un código
   "invisible/skip"). El terminador real es el código de control `0x0E`
   (rango de control-codes `0x05-0x13`, tabla de saltos separada en
   `0x586770`), que lleva a `label_2e1040` y pone `*(sp+0xD0)=0`.
10. La invocación problemática recibe `s6 = 0x01E00000`.
11. Dump de ~65KB alrededor de esa dirección: prácticamente todo `0x00`,
    un único byte suelto `0x80`, ningún `0x0E`. Como `0x00` no es código
    de control, `Print_message` lo trata como carácter normal
    indefinidamente.
12. `CardMesPrint` deriva `s6` correctamente:
    `tableBase(0x883238)=0x01E00004`, `messageId=0`, `entryOffset=0` →
    `s6 = tableBase + entryOffset - 4 = 0x01E00000`.
13. `GetCardInfo` prepara la estructura: `s0` = dirección global fija
    `0x8837A0`; `s1` observado = `1`; `*(s0+0x70) = *(0x883810) =
    0x01E00000`.
14. **FASE E.2 resolvió el origen de `0x01E00000`**: código original del
    juego, `0x2191c4: lui v0,0x1E0` / `0x2191c8: sw v0,0x70(s0)` —
    **constante hardcodeada por Capcom**. No es puntero corrupto, ni
    cálculo incorrecto, ni RPC, ni IOP, ni índice basura, ni error del
    loader, ni bug de codegen demostrado.
15. `0x01E00000` no pertenece a ningún `PT_LOAD` del ELF principal —
    cualquier contenido válido tiene que llegar en tiempo de ejecución.

## Cadena causal final

```
0x01E00000 = dirección correcta hardcodeada por Capcom, contenido ≈ vacío
  → Print_message no encuentra el control 0x0E
    → procesa ~667.777 "caracteres" (0x00 en su mayoría)
      → s1 += 44/carácter, sin límite
        → supera 32MB, wraparound de direccionamiento
          → pisa su propia jump table
            → table[8] = 0x02000100
              → JR 0x02000100 → BLOCKER_002
```

## Pregunta abierta para FASE F

Ya NO es "¿por qué apunta a `0x01E00000`?" (resuelto). Es:

**¿Qué ocurre en una ejecución correcta para que `0x01E00000` tenga los
datos que `GetCardInfo`/`CardMesPrint`/`Print_message` esperan?**

Ningún fix aplicado en ninguna fase — solo diagnóstico, según lo pedido en
todo momento.

## Estado del repositorio en este checkpoint

- Repo principal en `3aacb733b278c5ea6e3ee1fb36c1e956f37778fd`.
- `vendor/PS2Recomp` en `14b1e5cb39b4af7e6fc12f9a29fdc751efde49d7`,
  devuelto a este commit exacto (limpio) tras preservar toda la
  instrumentación temporal.
- Evidencia completa (logs, patches, snapshots, hashes SHA256):
  `analysis/local/blocker002_checkpoint_faseE/` (ver `MANIFEST.md`).
