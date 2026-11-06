// Opt-in GS replay capture hook (introduced in PERF_003 E2). Not included by any build: to take a capture, include
// it from runtime/dmc_overrides.cpp and call startGsReplayCapture(runtime) before the checkpoint load (README.md,
// "Capture"); remove both lines afterwards and recompile that one unit.
//
// DMC_GS_CAPTURE=<file>            install the recording raster backend and write one capture to <file>;
//                                   unset: nothing is installed
// DMC_GS_CAPTURE_FROM=<tick>        first emulated VSync tick of the window
// DMC_GS_CAPTURE_VSYNCS=<n>         window length, 1 or 2 VSync ticks (default 1)
// DMC_GS_CAPTURE_MAX_MB=<n>         size limit of the capture (default 256)
//
// The recorder forwards every call to its own GSCpuBackend unchanged; see gs_replay.h.
#include "gs_replay.h"
#include "runtime/ee_scheduler.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <memory>

static void startGsReplayCapture(PS2Runtime &runtime)
{
    const char *path = std::getenv("DMC_GS_CAPTURE");
    if (!path || !*path)
        return;
    gsreplay::RecordingBackend::Options options;
    options.path = path;
    if (const char *from = std::getenv("DMC_GS_CAPTURE_FROM"))
        options.fromTick = std::strtoull(from, nullptr, 0);
    options.vsyncs = 1;
    if (const char *vsyncs = std::getenv("DMC_GS_CAPTURE_VSYNCS"))
        options.vsyncs = std::clamp<uint64_t>(std::strtoull(vsyncs, nullptr, 0), 1u, 2u);
    if (const char *maxMb = std::getenv("DMC_GS_CAPTURE_MAX_MB"))
        options.maxBytes = static_cast<size_t>(std::strtoull(maxMb, nullptr, 0)) << 20;
    options.tick = [&runtime] { return runtime.eeScheduler().currentVSyncTick(); };
    options.log = [](const char *message) { std::fprintf(stderr, "%s\n", message); };
    std::fprintf(stderr, "[GSREPLAY] recorder installed: from tick %llu, %llu VSync, limit %llu MB, file %s\n",
                 static_cast<unsigned long long>(options.fromTick), static_cast<unsigned long long>(options.vsyncs),
                 static_cast<unsigned long long>(options.maxBytes >> 20), path);
    runtime.gs().setRasterBackend(std::make_unique<gsreplay::RecordingBackend>(std::move(options)));
}
