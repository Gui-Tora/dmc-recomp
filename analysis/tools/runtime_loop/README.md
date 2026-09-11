# P3.11 experimental runtime loop

Test infrastructure for the local SLES_503.58 build. No MPEG/scheduler fix.
Requires the existing Windows Python + Pillow, ELF, ISO, and built executable.
Nothing is installed. Artifacts, logs and guest bytes stay in ignored
`analysis/local/p311/RUN_NNN/`; do not add them to Git.

From the repository root, with `PS2X_CD_IMAGE` set to your own disc image
(the script asserts this is set; it is never hard-coded):

```powershell
$env:PS2X_CD_IMAGE = 'C:\path\to\your\Devil May Cry 2001.iso'
python analysis/tools/runtime_loop/run.py --runs 3 --seconds 180 --confirm --keys ENTER,X --method runtime --diagnostics --post-target 20
```

The tested sequence is START (250 ms), release, CROSS (250 ms), release.
It advances the observed missing-game-data memory-card notice, then the
language menu. It does not select save operations. Current PSS arrival is
about 69 seconds; a 60-second timeout was too short.

The harness waits for its own visible window and the MC GetDir log marker
before starting the sequence (minimum boot settle 10 seconds). The second
input has a bounded 4-second interval after START; this transition is based
on observed captures, **not** a semantic language-menu-ready detector.
The runtime channel supersedes physical input while enabled, so don't use
the same run for manual controller testing.

`runtime/p311_pad_test.inc`, included only by `runtime/dmc_overrides.cpp`,
polls the per-run `DMC_P311_PAD_FILE` and calls the pre-existing pad test
override API. Invalid/missing/stale (>1 s) commands release all buttons.
Normal launches without this variable are unaffected by this adapter.
There is no driver, global keyboard input, guest memory patch or fake return.
The DMC override registration is gated by name, entry and CRC; the harness
also verifies SHA256. ELF identity is documented in the adapter and report.

The loop sets ISO and arena variables, checks for pre-existing RECOMP
processes, owns an exclusive lock, launches one process, records PID/HWND/title,
captures only that HWND, and merges stdout/stderr into one per-run log.
`PSS_REACHED` requires the MPEG feed marker; `TARGET` defaults to `[GP:H]`.
`--target` changes the literal marker; `--post-target` keeps observing after it.
Timeout and crash are separate. Process termination uses the exact `Popen`
handle created by the harness, never a process name. Forced-stop exit code 1
is recorded separately from `exit_before_stop`; it is not classified as crash.

Results: `result.json` contains environment, executable hash, inputs,
milestones, timings, result, exit code and owned PID termination. Each run has
`runtime.log`, boot/input/periodic PNGs and, with diagnostics, two 32 MiB RAM
snapshots. A stale `loop.lock` is not removed automatically; inspect the PID
and ongoing runs before manually removing that one file.

Offline analysis:

```powershell
python analysis/tools/runtime_loop/analyze_ram.py analysis/local/p311/RUN_008
python analysis/tools/runtime_loop/probe_buffers.py analysis/local/p311/RUN_008
python analysis/tools/runtime_loop/elf_contract.py --text sceMpegInit sceIpuInit
```

`probe_buffers.py` is deliberately specific to the recorded first-init
prefix of 262144 PSS bytes; do not generalize it to another phase/ELF. It uses
the already installed ffprobe. Its decoded frame counts include possible
error concealment; check the preserved stderr before interpreting them.

Build scripts (only if the diagnostic source changed):

```powershell
powershell -NoProfile -File analysis/tools/runtime_loop/build_one.ps1
powershell -NoProfile -File analysis/tools/runtime_loop/build_one.ps1 -Mpeg
```

First command compiles only dmc_overrides.cpp. Second compiles only MPEG.cpp,
removes the precise old archive member from a saved baseline library,
inserts the new object and verifies exactly one MPEG.obj. Both use only
`/t:_BuildLinkAction` afterward. No generated source is compiled. The MPEG
library backup belongs to this session: do not reuse it after unrelated
runtime changes, because those would be lost from the library.
The project ignores incremental linking due to its pre-existing /FORCE flag;
the link may take minutes. Never run a relink while a loop owns the executable.

To retire: remove only the P3.11 include/start call in dmc_overrides.cpp and
the marked P3.11 observational additions in MPEG.cpp; keep the pre-existing
P3.x work. Recompile the affected individual units and relink. Do not reset
vendor wholesale. `vendor-before-p311.patch` preserves the pre-existing diff.

The optional `--try-movie-skip` is a separate, single START experiment after
PSS + GP:H. It requires runtime input, default GP:H target and post-target
>=15 seconds. It does not automate title-menu or gameplay navigation.

Full interpretation and limitations:
`analysis/notes/BLOCKER_004_ASTRA_P311_OVERNIGHT.md`.
