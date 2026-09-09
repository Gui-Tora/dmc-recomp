# dmc-recomp

Experimental static recompilation of *Devil May Cry* (2001) for PlayStation 2
to native PC, using [PS2Recomp](https://github.com/ran-j/PS2Recomp).

**Target**: PAL Europe, `SLES_503.58`, version 1.02.

**Status**: work in progress. This is a reverse-engineering / bring-up
project, not a playable port.

- BLOCKER_001 (entry point / function-boundary corruption): **RESOLVED**
- BLOCKER_002 (missing CD-read IOP service): **RESOLVED**
- Real interactive boot reached
- DualSense/gamepad input works well enough to navigate in-game menus
- Title screen reached
- Rendering is still incomplete/corrupted
- Later game states (gameplay, missions) are not yet functional
- BLOCKER_003 has not yet been formally identified

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

This project is developed with an AI-assisted workflow across three
different roles, kept deliberately separate so that no single actor both
implements and grades its own work.

**ChatGPT** — coordinates the investigation: helps structure
reverse-engineering strategy, connects evidence across experiments, designs
validation steps, reviews Claude's and Astra's results critically, and
identifies what is still missing before a blocker can be considered closed.

**Claude** — the operational agent working directly inside the repository:
reads and modifies code, implements instrumentation and fixes, drives
bootstrap/build/pipeline, runs experiments, analyzes runtime logs, updates
technical documentation, and creates local Git checkpoints.

**Astra** — an independent technical auditor, deliberately separated from
the implementation role. Reconstructs causal chains independently, attempts
to falsify prior conclusions, and distinguishes demonstrated fact from
strongly supported conclusion, from inference, from open hypothesis. Looks
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
6. Astra independently audits significant blockers.
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

Known, currently out-of-scope limitations (not yet classified as a new
blocker — none of them has been shown to actually block further progress):

- `CdModuleService` only handles `mode=1`; `mode=2` is left explicitly
  unsupported.
- CDMODULE RPCs `0x9`, `0xA`, `0xD` are unhandled.
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

No `LICENSE` file has been added to this repository yet — that decision is
pending. The PS2Recomp upstream this project builds on and links against is
licensed under the **GNU General Public License, version 3** (plain GPLv3
text, no per-file SPDX headers or "or later" qualifier in the upstream
source). Any compiled `dmc-recomp` executable is therefore a combined work
bound by GPLv3, regardless of what license this repository's own original
code (scripts, patches, documentation) ends up under.
