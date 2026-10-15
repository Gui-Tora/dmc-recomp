# Changelog

All notable milestones of this static-recompilation bring-up of
*Devil May Cry* (PS2, PAL `SLES_503.58`) are recorded here.

Releases are **source-only**. They never include the game, its ELF/ISO,
recompiled game code, binaries built from it, checkpoints, memory dumps or
any other material derived from the game. You need your own legally obtained
copy of the game to build and run anything.

## [v0.1.0-alpha] — 2026-10-15 — NOT PLAYABLE

First public pre-release. It packages the research and runtime work done so
far; it is a development snapshot, **not** a playable build.

Target ELF: `SLES_503.58`, SHA-256
`d0753a6b3b2f00802a50758a872d8cf051725aa31c839aa8894eee30ce58bab4`,
CRC32 `77654AD2`, entry `0x00100008`. Recompiler: `ran-j/PS2Recomp` baseline
`14b1e5cb39b4af7e6fc12f9a29fdc751efde49d7` + 18 documented patches
(`patched_commit` `585c347e4cf3e26c804b561b3f2d5aa27b0fe1ae`).

### What works (observed on the target ELF)

- Cold boot from the original ELF through the symtab-derived function
  baseline (BLOCKER_001).
- CD resource reads and movie/PSS streaming through HLE services for the
  game's `CDMODULE.IRX` RPCs (BLOCKER_002, BLOCKER_003, GAMEPLAY_001D–001G).
- PSS movie playback with legible picture: MPEG decode via FFmpeg, guest
  `viBuf` re-decode, `sceGsSetDefDBuffDc` display/draw environment fixes
  and field presentation (BLOCKER_004).
- Memory-card screen, language selection, content-warning screens, title
  and main menu.
- NEW GAME: the intro movie (`0x232`) plays to completion.
- The real-time office cutscene after the intro is rendered in 3D (VU0
  `vf0` hard-wired to `(0,0,0,1)`, VIF1 `CHCR.TTE` tag transfer), with
  visual defects. Its event script no longer stalls; in one ~46-minute
  Release run it reached its end, followed by movie `0x231` and the loading
  of the next scene, where the game stops (see Incomplete).
- First audible game audio: the cutscene's streamed XAG voice track, played
  through a high-level emulation of the game's `TSNDDRVM.IRX` sound driver
  reconstructed from the IRX itself.
- Development checkpoints (`.dmck`, format v2, runtime ABI 2) with a
  compatibility identity independent of the executable build; F9 or a
  trigger file saves them to a configurable path without ever overwriting.

### Added in this release cycle (GAMEPLAY_001J)

- `GAMEPLAY_001J_vu0_vf0_hardwired.patch`: VU0 `vf0` reads as `(0,0,0,1)` in
  every guest thread (previously zero, which blacked out the 3D scene).
- `GAMEPLAY_001J_tsnddrvm_xag_service.patch`: `TSNDDRVM` SIF RPC service
  (SID 0 packed command queue, SID 1 `XagChkStandby`/`XagChkPlay`), XAG
  channel state machine, CD streaming of the voice file, SPU-ADPCM decode
  and host audio output; enabled only by the `devil-may-cry-pal` IOP
  profile; state serialized in checkpoints; `DMC_TSND_AUDIO=0` mutes it.
- `GAMEPLAY_001J_checkpoint_save_path_and_trigger.patch`:
  `PS2X_CHECKPOINT_SAVE_PATH` and `PS2X_CHECKPOINT_TRIGGER_FILE`.
- Research note
  [`GAMEPLAY_001J_VISUAL_TRANSITION_DIAGNOSIS.md`](analysis/notes/GAMEPLAY_001J_VISUAL_TRANSITION_DIAGNOSIS.md),
  including a CPU profile and the retracted hypotheses.

### Incomplete

- **Not playable:** no gameplay frame with player control has been reached.
- After the intro the game stops at the unimplemented stub `sceDmaRecvI`
  (`Movie_texture`, IPU hardware movie textures used by the next scene).
- Performance: about 3–4 VSync/s in 3D scenes on a Release build (PAL needs
  50); ~83% of the game thread is spent in the VU1 interpreter.
- Audio: only the XAG voice/stream channels. Sound effects, background
  music and movie audio are silent. XAG audio plays at real-time speed and is
  **not synchronized** with the slower emulated picture.
- Visual defects in the office scene (darkness, green tint, holes, display
  circuit 1 position).
- 17 game functions are still replaced by documented diagnostic `ret0`
  overrides; several VU0 translator semantics (CLIP/MAC/DIV/SQRT/FTOI) are
  known to be incomplete.
- A checkpoint after the intro cannot be saved yet (`CDMODULE: movie stream
  active` safe-point condition).

### Not validated

- Only the PAL `SLES_503.58` ELF above has been tested. Other regions and
  revisions are unsupported.
- Only Windows 11 x64 with Visual Studio 2022 has been used to build.
- The TSNDDRVM HLE models only what this game's cutscene path was observed to
  use; the remaining driver functions have not been exercised.
- No comparison of audio timing against original hardware or PCSX2.

### Tests

- `ps2x_tests`: 268 passing; the remaining failures are the same
  pre-existing set as the baseline before GAMEPLAY_001J.
- Checkpoint self-tests (`DMC_CHECKPOINT_SELFTEST=1`): 97 PASS / 0 FAIL.
- The patch set replays deterministically onto the baseline and reproduces
  the recorded `patched_commit`.

[v0.1.0-alpha]: https://github.com/Gui-Tora/dmc-recomp/releases/tag/v0.1.0-alpha
