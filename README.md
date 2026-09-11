# dmc-recomp

Experimental static recompilation of *Devil May Cry* (2001) for PlayStation 2
to native PC, using [PS2Recomp](https://github.com/ran-j/PS2Recomp).

**Target**: PAL Europe, `SLES_503.58`, version 1.02.

**Status**: work in progress. This is a reverse-engineering / bring-up
project, not a playable port.

- BLOCKER_001 (entry point / function-boundary corruption): **RESOLVED**
- BLOCKER_002 (missing CD-read IOP service): **RESOLVED**
- BLOCKER_003 (movie/PSS CD streaming transport): **RESOLVED**
- BLOCKER_004 (PSS movie data reaches the EE but produces no visible
  video): **OPEN**
- Real interactive boot reached
- DualSense/gamepad input works well enough to navigate in-game menus
- Title screen reached
- Rendering is still incomplete/corrupted
- Later game states (gameplay, missions) are not yet functional
- Visible movie/PSS playback does not yet work

Do not read any of the above as "playable", "complete", "ported", or
"remastered" — none of that is true yet.

## This repository does not include game data

This repository does not include game data, the original executable, BIOS,
disc image, or other copyrighted game assets. You must provide your own
legally obtained copy of the game.

The following are never distributed here and are excluded from Git:

- `original/` — your own ELF, `SYSTEM.CNF`, memory cards
- generated recompilation output (`recomp/generated/`)
- local builds (`build/`)
- runtime logs, RAM dumps, PCSX2 captures (`logs/`, `analysis/local/`)

## Reproducibility

- The PS2Recomp upstream commit is pinned in `upstream.lock.json`.
- Project-specific changes to that upstream live as reviewable patches in
  `patches/`, not as a fork.
- `scripts/pipeline.py bootstrap` deterministically reconstructs the patched
  vendor checkout (clone the pinned commit, apply the patchset, verify the
  result) — no manual steps.
- The current baseline derives function boundaries directly from the ELF's
  own `.symtab` ("symtab-first"), not from a Ghidra CSV export. See
  `analysis/notes/SYMTAB_BASELINE_PROMOTION.md`.
- Generated C++ (`recomp/generated/`) is intentionally **not** versioned —
  it is produced locally from your own ELF and is fully reproducible from
  it.
- You provide your own ELF/disc image; `scripts/pipeline.py` drives the
  rest of the workflow (`bootstrap` → `tools` → `identify` → `prepare` →
  `generate` → `build` → `run`).

Full setup instructions: [`AGENTS.md`](AGENTS.md) and
[`analysis/UPSTREAM.md`](analysis/UPSTREAM.md).

## Development workflow

This project is developed with an AI-assisted workflow across several
different roles, kept deliberately separate so that no single actor both
implements and grades its own work.

**ChatGPT** — coordinates the investigation: helps structure
reverse-engineering strategy, connects evidence across experiments, designs
validation steps, reviews other agents' results critically, and identifies
what is still missing before a blocker can be considered closed.

**Claude (Sonnet)** — the operational agent working directly inside the
repository: reads and modifies code, implements instrumentation and fixes,
drives bootstrap/build/pipeline, runs experiments, analyzes runtime logs,
updates technical documentation, and creates local Git checkpoints.

**Astra** — an independent, high-depth investigator that runs its own
runtime experiments (autonomous boot/input loops, RAM/buffer diagnostics)
separate from the implementation session, and reconstructs causal chains
from that first-hand evidence rather than from prior write-ups.

**Fable** — an independent adversarial auditor. Deliberately separated from
both implementation and Astra's own investigation; its job is to try to
falsify prior conclusions (Claude's or Astra's), re-derive causal chains
from primary sources, and distinguish demonstrated fact from strongly
supported conclusion, from inference, from open hypothesis. Looks
specifically for overclaims, contradictions, and missing validation before a
major blocker is signed off.

**PCSX2** is used as the reference/oracle for original game behavior — its
debugger, registers, breakpoints, and RAM dumps let us compare original
execution against the recompiled build.

The investigative loop:

1. Reproduce a blocker.
2. Isolate the first meaningful divergence.
3. Define what evidence would actually settle it.
4. Claude instruments, implements, and tests.
5. Compare against PCSX2 where original behavior is needed.
6. Astra and/or Fable independently audit significant blockers or findings.
7. Incorporate audit corrections.
8. Revalidate.
9. Git checkpoint.
10. Only then move to the next blocker.

A blocker is not considered closed merely because a fix appears to work. It
is closed after causal evidence, runtime validation, and — for significant
blockers — an independent audit.

AI output is not itself evidence. Primary evidence always comes from the
original machine code / ELF, observed runtime behavior, PCSX2, RAM/register
dumps, logs, hashes, and reproducible experiments. The AI tools help
organize, implement, and audit that investigation — they don't replace it.

## Current technical status

**BLOCKER_001** — entry point / function-boundary corruption, resolved by
switching to a symtab-derived baseline instead of Ghidra's CSV export. See
[`analysis/notes/BLOCKER_001_entry_di_gap.md`](analysis/notes/BLOCKER_001_entry_di_gap.md).

**BLOCKER_002** — the game registers a real SIF RPC service for CD reads
(`sid=0x12345678`) that had no HLE implementation, so requested resources
never arrived; the read cursor (`Print_message`) then advanced without
bound and eventually corrupted a jump table. Fixed with a minimal HLE read
service (`CdModuleService`: `fno=2`, `mode=1`, 16-byte-aligned sizes). The
fix and its closing validation were independently audited (see
`analysis/notes/BLOCKER_002_ASTRA_AUDIT.md`) before being marked resolved.
Full causal chain, evidence, and hash-verified validation:
[`analysis/notes/BLOCKER_002_indirect_jump_top_of_ram.md`](analysis/notes/BLOCKER_002_indirect_jump_top_of_ram.md).

**BLOCKER_003** — movie/PSS CD streaming transport. The CDMODULE.IRX RPCs
used to start, chunk-transfer, and end movie playback (`fno=9/0xA/0xC/0xD`)
had no HLE implementation; fixed with an HLE movie/PSS streaming service
that feeds the CD-transport layer used by MPEG demuxing. Scope: CD/IOP
transport only — see
[`analysis/notes/BLOCKER_003_cdmodule_movie_streaming.md`](analysis/notes/BLOCKER_003_cdmodule_movie_streaming.md).

**BLOCKER_004** — PSS movie data reaches the EE but produces no visible
video. **Open.** Validated so far:

- FFmpeg-backed video decode is active and receiving real PSS video bytes.
- PSS transport/demux reaches the MPEG HLE pipeline.
- The runtime guest heap/arena and async callback-stack allocation
  (previously colliding with the game's own heap) were separated and
  hardened.
- The EE scheduler's batch ordering for queued guest invocations
  (`queueInvocation`) was corrected: batches were being drained in FIFO
  order into a per-thread stack that executes LIFO, silently reversing
  every batch (MPEG demux callbacks, IRQ handler groups, VBlank
  callback/IRQ ordering). After the fix, the guest video buffer (`viBuf`)
  matches the expected MPEG video elementary stream **byte-for-byte**,
  validated in 3/3 independent runs.
- The next contractual divergence currently identified within the audited
  segment is `sceMpegInit` ownership: the original resets IPU/DMA hardware
  on init but does not destroy the guest-side MPEG software session; the
  current HLE over-resets MPEG callbacks, configuration, and accepted-input
  ownership on every init.
- An opt-in, four-mode experiment (not a production fix) established which
  host state continuity the *current* HLE architecture actually needs to
  cross that boundary: ownership/config continuity alone is insufficient;
  preserving the already-decoded output queue bridges exactly the frames
  already queued and no more; sustained post-init decoding additionally
  requires the live FFmpeg decoder object to survive. None of this implies
  original hardware preserves an equivalent decoder state — it only
  characterizes what RECOMP's current architecture requires.
- With that experiment, RECOMP renders recognizable game UI for the first
  time (the multi-language content warning screen) — confirmed by
  automated, owned-window screenshots, not just a single manual capture.
  Apparent per-run language differences turned out to be the same fixed
  two-page warning sequence sampled at different moments, not
  nondeterministic language selection.
- This is the first contractual divergence demonstrated *within the
  segment audited so far*, not a claim that it is the absolute first
  divergence since cold boot.
- A follow-up design replaces the preserved-decoder experiment with a
  fresh post-init FFmpeg decoder fed synchronously from the surviving
  guest `viBuf` elementary stream. An independent adversarial audit
  found this first version deadlocked deterministically after ~520 KiB
  because no guest-visible consumption of the ring ever existed; a
  follow-up fix mirrors that consumption back into the guest's own ring
  bookkeeping and gives the synchronous path sole ownership of the host
  decoder. Validated 3/3 with the same executable: the old ~517 KB /
  10-picture ceiling is broken by roughly 25x (hundreds of sustained
  `sceMpegGetPicture` successes per run), the ring recirculates past its
  own capacity, and — for the first time in this investigation —
  recognizable decoded MPEG/PSS imagery (a fire/flame animation) renders
  on screen, reproducibly across independent runs.
- The guest-visible MPEG-to-presentation contract was then traced end to
  end. `sceMpegGetPicture`'s return value (`v0`) does differ from what has
  been observed on original hardware, but the game's only caller branches
  on "negative vs non-negative", so that mismatch is not causal for the
  missing video. Tracing further identified real game code
  (`Movie_loadimage`) that constructs and repeatedly triggers a genuine
  GIF DMA presentation chain — not, as first suspected, something
  IOP/SIF-related. Forty captured real DMA triggers all reference the
  exact same freshly-decoded image buffer `sceMpegGetPicture` writes:
  the chain's data bands cover exactly 512×448×4 bytes starting at that
  buffer's address, byte-for-byte. Within this validated scope, the MPEG
  guest contract — decode, `GetPicture`, and the guest's own GIF DMA
  submission — is consistent; any remaining visual corruption is now
  understood to sit at or after the guest→runtime GIF/GS hardware
  boundary (exact pixel-format/layout semantics not yet audited), not in
  MPEG decode or transport. Visible movie/PSS playback is still not
  correct, and the project remains **not playable**.

Full history: [`analysis/notes/BLOCKER_004_pss_video_output.md`](analysis/notes/BLOCKER_004_pss_video_output.md)
(canonical, cumulative) and the independent audits/experiments referenced
from it (`BLOCKER_004_ASTRA_P311_OVERNIGHT.md`,
`BLOCKER_004_FABLE_P3111_CAUSAL_AUDIT.md`,
`BLOCKER_004_P312_SCHEDULER_BATCH_ORDER.md`,
`BLOCKER_004_P313_SCEMPEGINIT_OWNERSHIP_MATRIX.md`,
`BLOCKER_004_P3131_VISUAL_LANGUAGE_TRACE.md`,
`BLOCKER_004_P314_SYNCHRONOUS_GUEST_ES_REDECODE.md`,
`BLOCKER_004_P3141_FABLE_SYNC_MPEG_AUDIT.md`,
`BLOCKER_004_P3142_VIBUF_CONSUMPTION_AND_OWNERSHIP.md`,
`BLOCKER_004_P315_GETPICTURE_PRESENTATION_CONTRACT.md`,
`BLOCKER_004_P3151_GUEST_PRESENTATION_TRACE.md`,
`BLOCKER_004_P3152_MOVIE_TAG_CHAIN_IMAGEADDR_DATAFLOW.md`).

Known, currently out-of-scope limitations (not yet classified as blocking
further progress):

- `CdModuleService` only handles `mode=1`; `mode=2` is left explicitly
  unsupported.
- Rendering is incomplete/corrupted past the title screen.
- The boot flow after the title screen is still under investigation.

## Build

Windows, Python 3.11+, Git, Visual Studio 2022 (C++ x64/Windows SDK), CMake
≥ 3.21. See [`AGENTS.md`](AGENTS.md) for the full workflow, coding/evidence
conventions, and `scripts/pipeline.py` usage.

```powershell
python scripts/pipeline.py bootstrap
python scripts/pipeline.py tools
python scripts/pipeline.py identify original/YOUR_ELF_NAME
```

## License

Original code in this repository is licensed under the GNU General Public
License v3.0 only (GPL-3.0-only).

PS2Recomp and other third-party components remain subject to their respective
licenses.

Devil May Cry, its executable, game data, assets, trademarks and other
copyrighted material are not distributed by this repository and remain the
property of their respective rights holders.
