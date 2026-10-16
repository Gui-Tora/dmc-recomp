# vu1_replay — compiled VU1 programs and offline VU1 bench (GAMEPLAY_001K)

Nothing here contains game data. `make_vu1c.py` builds C++ from **your own** ELF into `recomp/vu1c/`
(ignored by Git); the bench reads local capture directories (also kept out of Git).

## Generate the compiled VU1 programs (optional)

```
python tools/vu1_replay/make_vu1c.py                 # default: --elf original/SLES_503.58 --out recomp/vu1c
python scripts/pipeline.py build                      # or the usual incremental build
```

`make_vu1c.py` checks the ELF SHA-256 against `dmc_vu1_programs.json`, rebuilds the 16 KiB VU1 micro
memory by applying the VIF1 `MPG` commands listed there (file offsets in the ELF), checks the image hash
the runtime keys on (`a0dbee2aeac4dafa`) and writes `recomp/vu1c/vu1c_<hash>.cpp`. Any mismatch stops
it without writing anything. The root `CMakeLists.txt` compiles whatever `.cpp` it finds in
`recomp/vu1c/`; with none, the build and the game are unchanged (interpreter only).

Run-time selection (runtime patch `GAMEPLAY_001K_vu1_native_dispatch.patch`):

| `PS2X_VU1_NATIVE` | Behaviour |
|---|---|
| unset | original code path (default) |
| `interpreter` | dispatcher bookkeeping, every call interpreted |
| `native` | compiled program when available, interpreter otherwise (with rollback) |
| `verify` | interpreter authoritative; compiled program run on copies and compared field by field |

Diagnostics: `PS2X_VU1_STATS_SEC=<s>` (statistics line), `PS2X_VU1_PATH1_HASH=1` (hash of every 1000
PATH1 packets), `PS2X_VU1_TRACE_CALLS=<n>` (input/output hashes of the first n calls).

## Files

| File | Role |
|---|---|
| `make_vu1c.py` + `dmc_vu1_programs.json` | Reproducible generation from the user's ELF (manifest = metadata only). |
| `gen_vu1c.py` | Generator: one C++ function per micro-memory image for the listed entries ("compiled scoreboard"); entries whose reachable code is outside the supported subset are rejected. |
| `vu1isa.py` | VU1 micro-instruction decoding. |
| `vu1c_support.h` | Forwards to the runtime's `vu/ps2_vu1_native.h`. |
| `main.cpp` | Offline bench: replays captured `MSCAL`/`MSCNT` calls through the unmodified interpreter and compares bit for bit; compiled A/B, differential fuzzing, dispatcher integration tests. |
| `function_table_stub.cpp` | Empty guest function table so the bench links against `ps2_runtime.lib`. |

## Bench

Build against an existing vendor-root build tree (no runtime rebuild):

```
cmake -S tools/vu1_replay -B <build> -G "Visual Studio 17 2022" -A x64 -DPS2X_PREBUILT_DIR=<vendor build> ^
      -DVU1C_GENERATED=<repo>/recomp/vu1c/vu1c_a0dbee2aeac4dafa.cpp
cmake --build <build> --config Release
```

```
vu1_replay <cap>                          # interpreter replay vs capture
vu1_replay <cap> --profile 1              # + cycle-by-cycle profile
vu1_replay <cap> --native 1 --repeat 5    # + compiled A/B and timing
vu1_replay <cap> --native 1 --fuzz 8      # + differential fuzzing with follow-up MSCNT calls
vu1_replay <cap> --dispatch-tests 1       # dispatcher integration tests T1-T5
```

The captures were produced by temporary GAMEPLAY_001K instrumentation (`DMC_VU1_CAPTURE_DIR`), retired
from the runtime after the phase; its diff is kept locally with the evidence. Exit code 0 only if every
comparison reports zero differences.

Regression procedure after any change to the generator, `vu/ps2_vu1_native.h`, the dispatcher or the
interpreter: `--dispatch-tests 1`, `--native 1 --fuzz 8` on the office capture (all zero) and
`--native 1` on the perturbed capture (exactly the three planted differences).
