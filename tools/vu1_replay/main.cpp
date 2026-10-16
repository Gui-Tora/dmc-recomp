// GAMEPLAY_001K: offline VU1 replay bench.
//
// Replays the full samples written by the GAMEPLAY_001K VU1 capture instrumentation (retired from the
// runtime; preserved locally as a diff, see analysis/notes/GAMEPLAY_001K_VU1_PERFORMANCE_DESIGN.md) through the unmodified VU1Interpreter and
// compares, bit for bit, the replayed result with the result captured in the game:
//   VF/VI/ACC, Q/P/I/R, PC, MAC/status/clip, stop/halt bits, VU1 data memory, cycle delta and the
//   exact sequence of PATH1 packets (bytes and order).
//
// MSCNT (resume) samples are replayed from a reset scheduler, which is exact only when the previous
// call ended cleanly; the capture's stats.csv (budget_hits) tells whether any call was cut by budget.
//
// Usage: vu1_replay <capture_dir> [--max N] [--report out.json] [--repeat K]
// Exit code: 0 if every reproducible sample matched, 1 otherwise, 2 on usage/IO errors.
// The capture directory holds data derived from the game; it must never be committed.

#include "runtime/ps2_memory.h"
#include "runtime/ps2_vu1.h"
#include "runtime/gs/gs_frontend.h"
#include "vu1c_support.h"

#include <iostream>
#include <string>

// The runtime's dispatcher, compiled into the bench for isolated integration tests (--dispatch-tests).
#include "ps2_vu1_native_dispatch.inc"

#include <array>
#include <chrono>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

namespace
{
    // Sample record layout of the GAMEPLAY_001K capture instrumentation.
    struct SampleHeader
    {
        char magic[4];
        uint32_t version;
        uint32_t headerSize;
        uint32_t vu1StateSize;
        uint64_t callIndex;
        uint64_t vsync;
        uint64_t codeGeneration;
        uint64_t codeHash;
        uint32_t kind;
        uint32_t startPC;
        uint32_t top;
        uint32_t itop;
        uint32_t maxCycles;
        uint32_t codeHashVerified;
        uint64_t cyclesBefore;
        uint64_t cyclesAfter;
        uint64_t hostNs;
        uint32_t path1Count;
        uint32_t path1Bytes;
    };

    struct Sample
    {
        SampleHeader h{};
        VU1State before{};
        VU1State after{};
        std::vector<uint8_t> dataBefore;
        std::vector<uint8_t> dataAfter;
        std::vector<std::vector<uint8_t>> path1;
    };

    using Packets = std::vector<std::vector<uint8_t>>;

    bool readSample(FILE *f, Sample &s, std::string &error)
    {
        if (std::fread(&s.h, sizeof(s.h), 1, f) != 1)
            return false;
        if (std::memcmp(s.h.magic, "V1S1", 4) != 0 || s.h.version != 1 || s.h.headerSize != sizeof(SampleHeader))
        {
            error = "bad sample header";
            return false;
        }
        if (s.h.vu1StateSize != sizeof(VU1State))
        {
            error = "VU1State size mismatch: capture " + std::to_string(s.h.vu1StateSize) +
                    ", bench " + std::to_string(sizeof(VU1State));
            return false;
        }
        s.dataBefore.resize(PS2_VU1_DATA_SIZE);
        s.dataAfter.resize(PS2_VU1_DATA_SIZE);
        bool ok = std::fread(&s.before, sizeof(VU1State), 1, f) == 1 &&
                  std::fread(s.dataBefore.data(), 1, PS2_VU1_DATA_SIZE, f) == PS2_VU1_DATA_SIZE &&
                  std::fread(&s.after, sizeof(VU1State), 1, f) == 1 &&
                  std::fread(s.dataAfter.data(), 1, PS2_VU1_DATA_SIZE, f) == PS2_VU1_DATA_SIZE;
        s.path1.clear();
        uint32_t consumed = 0;
        for (uint32_t i = 0; ok && i < s.h.path1Count; ++i)
        {
            uint32_t size = 0;
            ok = std::fread(&size, sizeof(size), 1, f) == 1;
            if (!ok)
                break;
            std::vector<uint8_t> packet(size);
            ok = std::fread(packet.data(), 1, size, f) == size;
            consumed += sizeof(size) + size;
            s.path1.push_back(std::move(packet));
        }
        if (ok && consumed != s.h.path1Bytes)
        {
            error = "PATH1 byte count mismatch";
            return false;
        }
        if (!ok)
            error = "truncated sample";
        return ok;
    }

    bool sameBits(const void *a, const void *b, size_t n) { return std::memcmp(a, b, n) == 0; }

    // Field-by-field comparison (VU1State has padding; never memcmp the whole struct).
    std::vector<std::string> compareState(const VU1State &a, const VU1State &b, uint64_t cyclesA, uint64_t cyclesB)
    {
        std::vector<std::string> diffs;
        for (int r = 0; r < 32; ++r)
            if (!sameBits(a.vf[r], b.vf[r], sizeof(a.vf[r])))
                diffs.push_back("vf" + std::to_string(r));
        for (int r = 0; r < 16; ++r)
            if (a.vi[r] != b.vi[r])
                diffs.push_back("vi" + std::to_string(r));
        if (!sameBits(a.acc, b.acc, sizeof(a.acc)))
            diffs.push_back("acc");
        if (!sameBits(&a.q, &b.q, 4))
            diffs.push_back("q");
        if (!sameBits(&a.p, &b.p, 4))
            diffs.push_back("p");
        if (!sameBits(&a.i, &b.i, 4))
            diffs.push_back("i");
        if (a.r != b.r)
            diffs.push_back("r");
        if (a.pc != b.pc)
            diffs.push_back("pc");
        if (a.mac != b.mac)
            diffs.push_back("mac");
        if (a.status != b.status)
            diffs.push_back("status");
        if (a.clip != b.clip)
            diffs.push_back("clip");
        if (cyclesA != cyclesB)
            diffs.push_back("cycles");
        if (a.ebit != b.ebit || a.haltAfterDelaySlot != b.haltAfterDelaySlot)
            diffs.push_back("ebit/halt");
        if (a.stoppedByD != b.stoppedByD || a.stoppedByT != b.stoppedByT)
            diffs.push_back("stoppedByD/T");
        if (a.dBitEnabled != b.dBitEnabled || a.tBitEnabled != b.tBitEnabled)
            diffs.push_back("dBit/tBitEnabled");
        if (a.top != b.top || a.itop != b.itop)
            diffs.push_back("top/itop");
        if (a.branchPending != b.branchPending || a.branchTarget != b.branchTarget || a.branchDelay != b.branchDelay)
            diffs.push_back("branch");
        return diffs;
    }

    struct KeyResult
    {
        uint64_t samples = 0, exact = 0, mismatched = 0, skipped = 0, cycles = 0, hostNs = 0, captureHostNs = 0;
        std::map<std::string, uint64_t> fieldDiffs;
        std::string firstMismatch;
        // compiled-program A/B (--native 1)
        uint64_t nativeExact = 0, nativeMismatch = 0, nativeNs = 0, nativeInterpNs = 0, nativeCycles = 0;
        uint64_t nativeLookupNs = 0, nativeCopyNs = 0, nativeVerifyNs = 0, nativeFallbackInterpNs = 0;
        std::map<std::string, uint64_t> nativeFallback, nativeFieldDiffs;
        std::string nativeFirstMismatch;
    };

    struct ProfileSample
    {
        uint64_t codeHash;
        uint32_t startPC, kind;
        uint64_t callIndex, cycles;
        std::vector<uint32_t> perPc;
        std::vector<uint32_t> perPcIssued;
    };

    std::string hex64(uint64_t v)
    {
        char b[32];
        std::snprintf(b, sizeof(b), "%016" PRIx64, v);
        return b;
    }
}

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        std::fprintf(stderr, "usage: vu1_replay <capture_dir> [--max N] [--report out.json] [--repeat K]\n");
        return 2;
    }
    const std::filesystem::path dir = argv[1];
    uint64_t maxSamples = ~0ull;
    uint32_t repeat = 1;
    bool profile = false;
    bool native = false;
    bool dispatchTests = false;
    bool strictExitTest = false; // --strict-exit 1: default strict handler must terminate the process (exit 3)
    std::map<std::string, uint64_t> dtRuns, dtFail;
    std::map<std::string, std::string> dtFirstFail;
    std::filesystem::path regressionPath;
    std::filesystem::path reportPath = dir / "replay_report.json";
    for (int i = 2; i + 1 < argc; i += 2)
    {
        const std::string opt = argv[i];
        if (opt == "--max")
            maxSamples = std::strtoull(argv[i + 1], nullptr, 0);
        else if (opt == "--report")
            reportPath = argv[i + 1];
        else if (opt == "--strict-exit")
            strictExitTest = std::strtoul(argv[i + 1], nullptr, 0) != 0;
        else if (opt == "--dispatch-tests")
            dispatchTests = std::strtoul(argv[i + 1], nullptr, 0) != 0;
        else if (opt == "--native")
            native = std::strtoul(argv[i + 1], nullptr, 0) != 0;
        else if (opt == "--regressions")
            regressionPath = argv[i + 1];
        else if (opt == "--profile")
            profile = std::strtoul(argv[i + 1], nullptr, 0) != 0;
        else if (opt == "--repeat")
            repeat = static_cast<uint32_t>(std::strtoul(argv[i + 1], nullptr, 0));
    }

    FILE *f = std::fopen((dir / "samples.bin").string().c_str(), "rb");
    if (!f)
    {
        std::fprintf(stderr, "cannot open %s\n", (dir / "samples.bin").string().c_str());
        return 2;
    }

    auto mem = std::make_unique<PS2Memory>();
    if (!mem->initialize())
    {
        std::fprintf(stderr, "PS2Memory initialize failed\n");
        return 2;
    }
    Packets replayPath1;
    mem->setGifPacketCallback([&](const uint8_t *data, uint32_t sizeBytes)
                              { replayPath1.emplace_back(data, data + sizeBytes); });
    std::vector<uint8_t> vram(PS2_GS_VRAM_SIZE, 0u);
    auto gs = std::make_unique<GS>();
    gs->init(vram.data(), static_cast<uint32_t>(vram.size()), nullptr);
    auto vu1 = std::make_unique<VU1Interpreter>(VU1Interpreter::Unit::VU1);
    auto refVu1 = std::make_unique<VU1Interpreter>(VU1Interpreter::Unit::VU1);

    std::map<uint64_t, std::vector<uint8_t>> images;
    uint64_t loadedHash = 0;
    bool haveLoaded = false;
    uint64_t total = 0, exact = 0, mismatched = 0, skippedResume = 0, skippedImage = 0, unverifiedHash = 0;
    uint64_t generationBumpFailures = 0;
    std::map<std::tuple<uint64_t, uint32_t, uint32_t>, KeyResult> perKey;
    std::string error;
    uint64_t profileExact = 0, profileMismatch = 0;
    std::vector<ProfileSample> profileSamples;
    std::vector<uint8_t> nativeData(PS2_VU1_DATA_SIZE), nativePacket(vu1c::Xgkick::kBufferSize);
    const bool regressionOut = !regressionPath.empty();
    std::vector<uint64_t> regressionCalls;
    std::vector<bool> fallbackChecks;
    uint32_t fuzzPerSample = 0;
    uint64_t fuzzRuns = 0, fuzzExact = 0, fuzzMismatch = 0, fuzzRefusedButInterpClean = 0, fuzzBudgetBoundary = 0;
    std::map<int, uint64_t> fuzzRefused;
    std::map<uint32_t, uint64_t> fuzzExitPc, fuzzFollowUp;
    std::string fuzzFirstMismatch;
    for (int i = 2; i + 1 < argc; i += 2)
        if (std::string(argv[i]) == "--fuzz")
            fuzzPerSample = static_cast<uint32_t>(std::strtoul(argv[i + 1], nullptr, 0));
    if (native)
        std::printf("native: %zu compiled program entries registered\n", vu1c::registry().size());

    Sample s;
    while (total < maxSamples && readSample(f, s, error))
    {
        ++total;
        KeyResult &kr = perKey[{s.h.codeHash, s.h.startPC, s.h.kind}];
        ++kr.samples;
        kr.captureHostNs += s.h.hostNs;
        if (!s.h.codeHashVerified)
            ++unverifiedHash;
        auto img = images.find(s.h.codeHash);
        if (img == images.end())
        {
            std::ifstream in(dir / ("code_" + hex64(s.h.codeHash) + ".bin"), std::ios::binary);
            std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            if (bytes.size() != PS2_VU1_CODE_SIZE)
            {
                ++skippedImage;
                ++kr.skipped;
                continue;
            }
            img = images.emplace(s.h.codeHash, std::move(bytes)).first;
        }
        if (!haveLoaded || loadedHash != s.h.codeHash)
        {
            std::memcpy(mem->getVU1Code(), img->second.data(), PS2_VU1_CODE_SIZE);
            // Bump the code generation through the normal mapped-write path so the interpreter's decode
            // cache is invalidated exactly as in the game.
            const uint64_t before = mem->getVU1CodeGeneration();
            uint32_t word0 = 0;
            std::memcpy(&word0, img->second.data(), 4);
            mem->write32(PS2_VU1_CODE_BASE, word0);
            if (mem->getVU1CodeGeneration() == before)
                ++generationBumpFailures;
            loadedHash = s.h.codeHash;
            haveLoaded = true;
        }

        if (dispatchTests)
        {
            using namespace vu1native;
            const uint64_t capDelta = s.h.cyclesAfter - s.h.cyclesBefore;
            auto resetDispatcher = [](Mode m)
            {
                d() = Dispatcher{};
                d().initialized = true;
                d().mode = m;
            };
            auto load = [&](const VU1State &st0, const std::vector<uint8_t> &data0)
            {
                std::memcpy(mem->getVU1Data(), data0.data(), PS2_VU1_DATA_SIZE);
                vu1->state() = st0;
                replayPath1.clear();
            };
            // Tests drive the bench interpreter outside the dispatcher, so tell the dispatcher the
            // interpreter's current private counter (read with a zero-budget execute, which only resets
            // the scheduler: every test call below starts from a reset scheduler anyway).
            auto syncPrivate = [&]()
            {
                const VU1State saved = vu1->state();
                vu1->execute(mem->getVU1Code(), PS2_VU1_CODE_SIZE, mem->getVU1Data(), PS2_VU1_DATA_SIZE, *gs,
                             mem.get(), 0u, s.h.top, s.h.itop, 0u);
                d().lastPrivate = vu1->state().cycles;
                vu1->state() = saved;
            };
            auto memData = [&]()
            { return std::vector<uint8_t>(mem->getVU1Data(), mem->getVU1Data() + PS2_VU1_DATA_SIZE); };
            // Reference: a separate interpreter whose hidden state follows the natural chain.
            auto refCall = [&](uint32_t kind, VU1State &st, std::vector<uint8_t> &data, Packets &packets, bool prime)
            {
                std::vector<uint8_t> saved(mem->getVU1Data(), mem->getVU1Data() + PS2_VU1_DATA_SIZE);
                std::memcpy(mem->getVU1Data(), data.data(), PS2_VU1_DATA_SIZE);
                if (prime)
                {
                    refVu1->state() = st;
                    refVu1->execute(mem->getVU1Code(), PS2_VU1_CODE_SIZE, mem->getVU1Data(), PS2_VU1_DATA_SIZE,
                                    *gs, mem.get(), 0u, s.h.top, s.h.itop, 0u);
                }
                const uint64_t base = refVu1->state().cycles;
                refVu1->state() = st;
                refVu1->state().cycles = base;
                replayPath1.clear();
                if (kind == 0)
                    refVu1->execute(mem->getVU1Code(), PS2_VU1_CODE_SIZE, mem->getVU1Data(), PS2_VU1_DATA_SIZE,
                                    *gs, mem.get(), s.h.startPC, s.h.top, s.h.itop, 65536u);
                else
                    refVu1->resume(mem->getVU1Code(), PS2_VU1_CODE_SIZE, mem->getVU1Data(), PS2_VU1_DATA_SIZE,
                                   *gs, mem.get(), s.h.top, s.h.itop, 65536u);
                const uint64_t delta = refVu1->state().cycles - base;
                st = refVu1->state();
                st.cycles = delta; // callers compare deltas
                data = memData();
                packets = replayPath1;
                std::memcpy(mem->getVU1Data(), saved.data(), PS2_VU1_DATA_SIZE);
                return delta;
            };
            auto check = [&](const char *name, bool ok, const std::string &detail)
            {
                ++dtRuns[name];
                if (!ok)
                {
                    ++dtFail[name];
                    if (dtFirstFail[name].empty())
                        dtFirstFail[name] = "call " + std::to_string(s.h.callIndex) + ": " + detail;
                }
            };
            const vu1c::Program *prog = vu1c::lookup(s.h.codeHash, s.h.kind,
                                                     s.h.kind == 0 ? (s.h.startPC & 0x3FFFu) : s.before.pc);

            // T1: compiled call through the dispatcher (chain of compiled calls), PATH1 held and submitted.
            resetDispatcher(Mode::Native);
            d().prevCompiledOk = true;
            d().interpreterStale = true;
            d().cycleOffset = 777u;
            load(s.before, s.dataBefore);
            PS2Runtime_vu1DispatchCall(*vu1, *mem, *gs, s.h.kind, s.h.startPC, s.h.top, s.h.itop, 0u);
            {
                const VU1State a = vu1->state();
                std::string diff;
                for (const std::string &x : compareState(s.after, a, capDelta, a.cycles - s.before.cycles))
                    diff += x + " ";
                if (memData() != s.dataAfter)
                    diff += "data ";
                if (replayPath1 != s.path1)
                    diff += "path1 ";
                if (d().compiledOk != 1u)
                    diff += "not-compiled ";
                if (d().cycleOffset != 777u + capDelta)
                    diff += "offset ";
                check("T1_native_chain", prog && diff.empty(), diff);
            }

            // T2: interpreter continuity after a compiled call (MSCNT follow-up of the same program on the
            // resulting state), against an interpreter-only chain.
            if (s.h.kind == 1 || s.h.kind == 0)
            {
                const VU1State afterCompiled = vu1->state();
                const std::vector<uint8_t> afterCompiledData = memData();
                d().mode = Mode::Interpreter; // next call interpreted; interpreterStale is still set
                syncPrivate();
                replayPath1.clear();
                PS2Runtime_vu1DispatchCall(*vu1, *mem, *gs, 1u, 0u, s.h.top, s.h.itop, 0u);
                const VU1State dState = vu1->state();
                const std::vector<uint8_t> dData = memData();
                const Packets dPackets = replayPath1;
                // reference chain: call i then the follow-up, both interpreted, no reset in between
                VU1State rst = s.before;
                std::vector<uint8_t> rdata = s.dataBefore;
                Packets rpk;
                refCall(s.h.kind, rst, rdata, rpk, true);
                rst.cycles = afterCompiled.cycles; // continue from the same virtual count
                Packets rpk2;
                const uint64_t rDelta = refCall(1u, rst, rdata, rpk2, false);
                std::string diff;
                // cycles: the dispatcher's measured interpreter delta must equal the reference chain's, and
                // state.cycles must hold the virtual count (interpreter private counter + compiled offset)
                for (const std::string &x : compareState(rst, dState, rDelta, d().interpCycles))
                    diff += x + " ";
                if (dState.cycles != d().lastPrivate + d().cycleOffset)
                    diff += "virtual-cycles ";
                if (dData != rdata)
                    diff += "data ";
                if (dPackets != rpk2)
                    diff += "path1 ";
                if (d().primeCount != 1u)
                    diff += "no-prime ";
                (void)afterCompiledData;
                check("T2_interp_after_compiled", diff.empty(), diff);
            }

            // T3: rollback on Budget/Fault (mutated inputs), every 8th sample.
            if (prog && (s.h.callIndex % 8u) == 0u)
            {
                bool doneBudget = false, doneFault = false;
                for (uint32_t variant = 0; variant < 96 && !(doneBudget && doneFault); ++variant)
                {
                    uint64_t rng = (s.h.callIndex + 7u) * 0x9E3779B97F4A7C15ull + variant * 0xD1B54A32D192ED03ull;
                    auto next = [&rng]()
                    {
                        rng ^= rng << 13;
                        rng ^= rng >> 7;
                        rng ^= rng << 17;
                        return rng;
                    };
                    VU1State fin = s.before;
                    std::vector<uint8_t> fdata = s.dataBefore;
                    for (uint32_t m = 0; m < 1u + (next() % 24u); ++m)
                    {
                        const uint64_t r = next();
                        if (r & 1u)
                            fin.vi[1u + (r >> 8) % 15u] = static_cast<int16_t>(r >> 24);
                        else
                            fdata[(r >> 8) % PS2_VU1_DATA_SIZE] ^= static_cast<uint8_t>(1u << ((r >> 4) & 7u));
                    }
                    // probe the compiled program on copies
                    VU1State pst = fin;
                    std::vector<uint8_t> pdata = fdata;
                    vu1c::Ctx ctx;
                    ctx.st = &pst;
                    ctx.data = pdata.data();
                    ctx.dataSize = PS2_VU1_DATA_SIZE;
                    ctx.kind = s.h.kind;
                    ctx.startPC = s.h.startPC;
                    ctx.top = s.h.top;
                    ctx.itop = s.h.itop;
                    ctx.maxCycles = 65536u;
                    ctx.packet = nativePacket.data();
                    const int pr = prog->fn(ctx);
                    if (!((pr == vu1c::Budget && !doneBudget) || (pr == vu1c::Fault && !doneFault)))
                        continue;
                    (pr == vu1c::Budget ? doneBudget : doneFault) = true;
                    VU1State rst = fin;
                    std::vector<uint8_t> rdata = fdata;
                    Packets rpk;
                    const uint64_t rDelta = refCall(s.h.kind, rst, rdata, rpk, true);
                    resetDispatcher(Mode::Native);
                    d().prevCompiledOk = true;
                    d().interpreterStale = true;
                    load(fin, fdata);
                    syncPrivate();
                    PS2Runtime_vu1DispatchCall(*vu1, *mem, *gs, s.h.kind, s.h.startPC, s.h.top, s.h.itop, 0u);
                    const VU1State a = vu1->state();
                    std::string diff;
                    for (const std::string &x : compareState(rst, a, rDelta, d().interpCycles))
                        diff += x + " ";
                    if (a.cycles != d().lastPrivate + d().cycleOffset)
                        diff += "virtual-cycles ";
                    if (memData() != rdata)
                        diff += "data ";
                    if (replayPath1 != rpk)
                        diff += "path1 ";
                    if (d().fallback[pr] != 1u || d().interpCalls != 1u || d().compiledOk != 0u || d().cycleOffset != 0u)
                        diff += "accounting ";
                    check(pr == vu1c::Budget ? "T3_rollback_budget" : "T3_rollback_fault", diff.empty(), diff);
                }
            }

            // T4: verify mode, interpreter authoritative, compiled compared.
            resetDispatcher(Mode::Verify);
            load(s.before, s.dataBefore);
            if (s.h.kind == 1)
            {
                // the interpreter continues a clean end: reset its scheduler as the replay does
                vu1->execute(mem->getVU1Code(), PS2_VU1_CODE_SIZE, mem->getVU1Data(), PS2_VU1_DATA_SIZE, *gs,
                             mem.get(), 0u, s.h.top, s.h.itop, 0u);
                vu1->state() = s.before;
            }
            syncPrivate();
            {
                const uint64_t base = s.before.cycles;
                PS2Runtime_vu1DispatchCall(*vu1, *mem, *gs, s.h.kind, s.h.startPC, s.h.top, s.h.itop, 0u);
                (void)base;
                std::string diff;
                if (d().verifyCompared != 1u)
                    diff += "not-compared ";
                if (d().verifyMismatch != 0u)
                    diff += "verify-mismatch ";
                if (memData() != s.dataAfter)
                    diff += "data ";
                if (replayPath1 != s.path1)
                    diff += "path1 ";
                check("T4_verify", prog && diff.empty(), diff);
            }

            // T5: negative control for verify mode. The registered program is replaced by a copy that
            // flips one mantissa bit of vf5.y after an Ok run; verify must report exactly one mismatch
            // and the interpreter (authoritative) result must still equal the capture.
            for (vu1c::Program &entryProg : vu1c::registry())
            {
                if (!prog || entryProg.codeHash != prog->codeHash || entryProg.kind != prog->kind || entryProg.entryPc != prog->entryPc)
                    continue;
                static vu1c::ProgramFn realFn;
                realFn = entryProg.fn;
                entryProg.fn = [](vu1c::Ctx &c)
                {
                    const int r = realFn(c);
                    if (r == vu1c::Ok)
                    {
                        uint32_t bits;
                        std::memcpy(&bits, &c.st->vf[5][1], 4);
                        bits ^= 1u;
                        std::memcpy(&c.st->vf[5][1], &bits, 4);
                    }
                    return r;
                };
                resetDispatcher(Mode::Verify);
                load(s.before, s.dataBefore);
                if (s.h.kind == 1)
                {
                    vu1->execute(mem->getVU1Code(), PS2_VU1_CODE_SIZE, mem->getVU1Data(), PS2_VU1_DATA_SIZE, *gs,
                                 mem.get(), 0u, s.h.top, s.h.itop, 0u);
                    vu1->state() = s.before;
                }
                syncPrivate();
                PS2Runtime_vu1DispatchCall(*vu1, *mem, *gs, s.h.kind, s.h.startPC, s.h.top, s.h.itop, 0u);
                entryProg.fn = realFn;
                std::string diff;
                if (d().verifyMismatch != 1u)
                    diff += "mismatch-not-detected ";
                if (memData() != s.dataAfter || replayPath1 != s.path1)
                    diff += "authoritative-result-changed ";
                check("T5_verify_detects_divergence", diff.empty(), diff);
                break;
            }

            // T6/T7: one-field negative controls for the D/T enable bits, T8: strict mode stops at the first
            // divergence (handler called once, with the detail), T9: strict mode with no divergence.
            for (vu1c::Program &entryProg : vu1c::registry())
            {
                if (!prog || entryProg.codeHash != prog->codeHash || entryProg.kind != prog->kind || entryProg.entryPc != prog->entryPc)
                    continue;
                static vu1c::ProgramFn realFn2;
                static int corruption;
                static uint32_t strictCalls;
                static std::string strictDetail;
                realFn2 = entryProg.fn;
                auto corrupting = [](vu1c::Ctx &c)
                {
                    const int r = realFn2(c);
                    if (r == vu1c::Ok)
                    {
                        if (corruption == 1)
                            c.st->dBitEnabled = !c.st->dBitEnabled;
                        else if (corruption == 2)
                            c.st->tBitEnabled = !c.st->tBitEnabled;
                        else if (corruption == 3)
                        {
                            uint32_t bits;
                            std::memcpy(&bits, &c.st->vf[5][1], 4);
                            bits ^= 1u;
                            std::memcpy(&c.st->vf[5][1], &bits, 4);
                        }
                    }
                    return r;
                };
                auto runVerify = [&](int corrupt, bool strict)
                {
                    corruption = corrupt;
                    strictCalls = 0;
                    strictDetail.clear();
                    entryProg.fn = corrupting;
                    resetDispatcher(Mode::Verify);
                    d().verifyStrict = strict;
                    d().strictFailure = [](const std::string &detail)
                    {
                        ++strictCalls;
                        strictDetail = detail;
                    };
                    load(s.before, s.dataBefore);
                    if (s.h.kind == 1)
                    {
                        vu1->execute(mem->getVU1Code(), PS2_VU1_CODE_SIZE, mem->getVU1Data(), PS2_VU1_DATA_SIZE, *gs,
                                     mem.get(), 0u, s.h.top, s.h.itop, 0u);
                        vu1->state() = s.before;
                    }
                    syncPrivate();
                    PS2Runtime_vu1DispatchCall(*vu1, *mem, *gs, s.h.kind, s.h.startPC, s.h.top, s.h.itop, 0u);
                    entryProg.fn = realFn2;
                };
                auto onlyField = [](const std::string &detail, const char *field)
                {
                    // the reported difference list must name the field and nothing else
                    const size_t colon = detail.find(':');
                    const std::string list = colon == std::string::npos ? detail : detail.substr(colon + 1);
                    return list == std::string(" ") + field;
                };
                runVerify(1, false);
                check("T6_verify_detects_dBitEnabled", d().verifyMismatch == 1u && onlyField(d().firstMismatch, "dBitEnabled") && strictCalls == 0u,
                      d().firstMismatch);
                runVerify(2, false);
                check("T7_verify_detects_tBitEnabled", d().verifyMismatch == 1u && onlyField(d().firstMismatch, "tBitEnabled") && strictCalls == 0u,
                      d().firstMismatch);
                if (strictExitTest)
                {
                    // default handler: report, then terminate; reaching the line below is a failure
                    corruption = 3;
                    entryProg.fn = corrupting;
                    resetDispatcher(Mode::Verify);
                    d().verifyStrict = true;
                    load(s.before, s.dataBefore);
                    syncPrivate();
                    PS2Runtime_vu1DispatchCall(*vu1, *mem, *gs, s.h.kind, s.h.startPC, s.h.top, s.h.itop, 0u);
                    std::printf("strict-exit test FAILED: the process was not stopped\n");
                    return 1;
                }
                runVerify(3, true);
                check("T8_strict_stops_at_first_divergence",
                      d().verifyMismatch == 1u && strictCalls == 1u && strictDetail == d().firstMismatch && onlyField(strictDetail, "vf5") &&
                          memData() == s.dataAfter && replayPath1 == s.path1,
                      "calls=" + std::to_string(strictCalls) + " " + strictDetail);
                runVerify(0, true);
                check("T9_strict_without_divergence", d().verifyMismatch == 0u && strictCalls == 0u && d().verifyCompared == 1u,
                      "calls=" + std::to_string(strictCalls) + " " + d().firstMismatch);
                break;
            }
            continue;
        }

        uint64_t bestNs = ~0ull;
        VU1State replayAfter{};
        std::vector<uint8_t> replayData(PS2_VU1_DATA_SIZE);
        Packets replayPackets;
        uint64_t replayCycles = 0;
        for (uint32_t rep = 0; rep < repeat; ++rep)
        {
            std::memcpy(mem->getVU1Data(), s.dataBefore.data(), PS2_VU1_DATA_SIZE);
            vu1->state() = s.before;
            // Priming: execute() with a zero cycle budget resets the private scheduler (working clip :=
            // state.clip, ready stamps, branch-VI backup) without running any instruction and leaves the
            // private cycle counter in state().cycles, which is the base for the replayed cycle delta.
            // MSCNT continues with that private scheduler state; the capture can only reproduce it after a
            // clean program end (pipelines flushed, no budget cut), which is what this reset models.
            vu1->execute(mem->getVU1Code(), PS2_VU1_CODE_SIZE, mem->getVU1Data(), PS2_VU1_DATA_SIZE,
                         *gs, mem.get(), 0u, s.h.top, s.h.itop, 0u);
            const uint64_t cycles0 = vu1->state().cycles;
            vu1->state() = s.before;
            replayPath1.clear();
            const auto t0 = std::chrono::steady_clock::now();
            if (s.h.kind == 0)
                vu1->execute(mem->getVU1Code(), PS2_VU1_CODE_SIZE, mem->getVU1Data(), PS2_VU1_DATA_SIZE,
                             *gs, mem.get(), s.h.startPC, s.h.top, s.h.itop, s.h.maxCycles);
            else
                vu1->resume(mem->getVU1Code(), PS2_VU1_CODE_SIZE, mem->getVU1Data(), PS2_VU1_DATA_SIZE,
                            *gs, mem.get(), s.h.top, s.h.itop, s.h.maxCycles);
            const auto t1 = std::chrono::steady_clock::now();
            bestNs = std::min<uint64_t>(bestNs, std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
            if (rep == 0)
            {
                replayAfter = vu1->state();
                replayCycles = vu1->state().cycles - cycles0;
                std::memcpy(replayData.data(), mem->getVU1Data(), PS2_VU1_DATA_SIZE);
                replayPackets = replayPath1;
            }
        }
        kr.hostNs += bestNs;
        kr.cycles += replayCycles;

        if (native)
        {
            // A/B against the capture: the compiled program must reproduce the captured result bit for
            // bit (same comparison as the interpreter replay above). Timings (min over `repeat` runs) are
            // split into program selection, input copies, compiled execution and verification.
            using clk = std::chrono::steady_clock;
            const uint32_t entryPc = s.h.kind == 0 ? (s.h.startPC & 0x3FFFu) : s.before.pc;
            const auto tl0 = clk::now();
            const vu1c::Program *prog = vu1c::lookup(s.h.codeHash, s.h.kind, entryPc);
            const auto tl1 = clk::now();
            kr.nativeLookupNs += std::chrono::duration_cast<std::chrono::nanoseconds>(tl1 - tl0).count();
            if (!prog)
            {
                ++kr.nativeFallback["no_program"];
                kr.nativeFallbackInterpNs += bestNs;
            }
            else
            {
                VU1State st{};
                Packets nativePackets;
                auto sink = [](void *user, const uint8_t *data, uint32_t size)
                { static_cast<Packets *>(user)->emplace_back(data, data + size); };
                vu1c::Ctx ctx;
                ctx.st = &st;
                ctx.data = nativeData.data();
                ctx.dataSize = PS2_VU1_DATA_SIZE;
                ctx.kind = s.h.kind;
                ctx.startPC = s.h.startPC;
                ctx.top = s.h.top;
                ctx.itop = s.h.itop;
                ctx.maxCycles = s.h.maxCycles;
                ctx.sink = sink;
                ctx.sinkUser = &nativePackets;
                ctx.packet = nativePacket.data();
                uint64_t bestNative = ~0ull, bestCopy = ~0ull;
                int result = vu1c::Ok;
                VU1State nativeAfter{};
                std::vector<uint8_t> nativeAfterData;
                Packets nativeAfterPackets;
                for (uint32_t rep = 0; rep < repeat; ++rep)
                {
                    const auto tc0 = clk::now();
                    st = s.before;
                    std::memcpy(nativeData.data(), s.dataBefore.data(), PS2_VU1_DATA_SIZE);
                    const auto tc1 = clk::now();
                    nativePackets.clear();
                    const auto t0n = clk::now();
                    result = prog->fn(ctx);
                    const auto t1n = clk::now();
                    bestCopy = std::min<uint64_t>(bestCopy, std::chrono::duration_cast<std::chrono::nanoseconds>(tc1 - tc0).count());
                    bestNative = std::min<uint64_t>(bestNative, std::chrono::duration_cast<std::chrono::nanoseconds>(t1n - t0n).count());
                    if (rep == 0)
                    {
                        nativeAfter = st;
                        nativeAfterData = nativeData;
                        nativeAfterPackets = nativePackets;
                    }
                    if (result != vu1c::Ok)
                        break;
                }
                kr.nativeCopyNs += bestCopy;
                if (result != vu1c::Ok)
                {
                    static const char *names[] = {"ok", "unsupported_entry", "precondition", "budget", "fault"};
                    ++kr.nativeFallback[names[result & 7]];
                    kr.nativeFallbackInterpNs += bestNs;
                }
                else
                {
                    const auto tv0 = clk::now();
                    std::vector<std::string> nd = compareState(s.after, nativeAfter, s.h.cyclesAfter - s.h.cyclesBefore,
                                                               nativeAfter.cycles - s.before.cycles);
                    if (nativeAfterData != s.dataAfter)
                    {
                        uint32_t first = 0;
                        while (first < PS2_VU1_DATA_SIZE && nativeAfterData[first] == s.dataAfter[first])
                            ++first;
                        nd.push_back("data@0x" + hex64(first).substr(12));
                    }
                    if (nativeAfterPackets != s.path1)
                        nd.push_back("path1(" + std::to_string(s.path1.size()) + " vs " + std::to_string(nativeAfterPackets.size()) + ")");
                    if (nativeAfter.cycles - s.before.cycles != s.h.cyclesAfter - s.h.cyclesBefore)
                        nd.push_back("cycles(" + std::to_string(s.h.cyclesAfter - s.h.cyclesBefore) + " vs " +
                                     std::to_string(nativeAfter.cycles - s.before.cycles) + ")");
                    const auto tv1 = clk::now();
                    kr.nativeVerifyNs += std::chrono::duration_cast<std::chrono::nanoseconds>(tv1 - tv0).count();
                    kr.nativeNs += bestNative;
                    kr.nativeInterpNs += bestNs;
                    kr.nativeCycles += replayCycles;
                    if (nd.empty())
                        ++kr.nativeExact;
                    else
                    {
                        ++kr.nativeMismatch;
                        std::string joined;
                        for (const std::string &d : nd)
                        {
                            ++kr.nativeFieldDiffs[d.substr(0, d.find_first_of("@("))];
                            joined += (joined.empty() ? "" : " ") + d;
                        }
                        if (kr.nativeFirstMismatch.empty())
                            kr.nativeFirstMismatch = "call " + std::to_string(s.h.callIndex) + ": " + joined;
                        if (regressionOut)
                            regressionCalls.push_back(s.h.callIndex);
                    }
                }

                // Differential fuzzing: mutate this real sample (data bits, special float values in data
                // and VF registers, VI registers) and require the compiled program to reproduce the
                // interpreter exactly, or to refuse (Budget/Fault) exactly when the interpreter does not
                // finish cleanly within the budget. Final PCs are counted to show which exits were covered.
                for (uint32_t variant = 0; variant < fuzzPerSample && result == vu1c::Ok; ++variant)
                {
                    uint64_t rng = (s.h.callIndex + 1u) * 0x9E3779B97F4A7C15ull + variant * 0xD1B54A32D192ED03ull;
                    auto next = [&rng]()
                    {
                        rng ^= rng << 13;
                        rng ^= rng >> 7;
                        rng ^= rng << 17;
                        return rng;
                    };
                    static const uint32_t specials[] = {0x00000000u, 0x80000000u, 0x00000001u, 0x807FFFFFu, 0x7F800000u,
                                                        0xFF800000u, 0x7FC00000u, 0x7F7FFFFFu, 0xFF7FFFFFu, 0x00800000u,
                                                        0x3F800000u, 0xBF800000u, 0x5F000000u, 0x1F000000u};
                    VU1State fin = s.before;
                    std::vector<uint8_t> fdata = s.dataBefore;
                    const uint32_t mode = static_cast<uint32_t>(next() % 4u);
                    const uint32_t count = 1u + static_cast<uint32_t>(next() % 48u);
                    for (uint32_t m = 0; m < count; ++m)
                    {
                        const uint64_t r = next();
                        if (mode == 0)
                            fdata[(r >> 8) % PS2_VU1_DATA_SIZE] ^= static_cast<uint8_t>(1u << (r & 7u));
                        else if (mode == 1)
                        {
                            const uint32_t word = static_cast<uint32_t>((r >> 8) % (PS2_VU1_DATA_SIZE / 4u));
                            std::memcpy(fdata.data() + word * 4u, &specials[r % (sizeof(specials) / 4u)], 4);
                        }
                        else if (mode == 2)
                        {
                            const uint32_t reg = 1u + static_cast<uint32_t>((r >> 8) % 31u);
                            std::memcpy(&fin.vf[reg][(r >> 16) & 3u], &specials[r % (sizeof(specials) / 4u)], 4);
                        }
                        else
                        {
                            const uint32_t reg = 1u + static_cast<uint32_t>((r >> 8) % 15u);
                            fin.vi[reg] = static_cast<int16_t>(r >> 24);
                        }
                    }
                    // reference: interpreter
                    std::memcpy(mem->getVU1Data(), fdata.data(), PS2_VU1_DATA_SIZE);
                    vu1->state() = fin;
                    vu1->execute(mem->getVU1Code(), PS2_VU1_CODE_SIZE, mem->getVU1Data(), PS2_VU1_DATA_SIZE,
                                 *gs, mem.get(), 0u, s.h.top, s.h.itop, 0u);
                    const uint64_t fbase = vu1->state().cycles;
                    vu1->state() = fin;
                    replayPath1.clear();
                    if (s.h.kind == 0)
                        vu1->execute(mem->getVU1Code(), PS2_VU1_CODE_SIZE, mem->getVU1Data(), PS2_VU1_DATA_SIZE,
                                     *gs, mem.get(), s.h.startPC, s.h.top, s.h.itop, s.h.maxCycles);
                    else
                        vu1->resume(mem->getVU1Code(), PS2_VU1_CODE_SIZE, mem->getVU1Data(), PS2_VU1_DATA_SIZE,
                                    *gs, mem.get(), s.h.top, s.h.itop, s.h.maxCycles);
                    const VU1State iAfter = vu1->state();
                    const uint64_t iCycles = iAfter.cycles - fbase;
                    const std::vector<uint8_t> iData(mem->getVU1Data(), mem->getVU1Data() + PS2_VU1_DATA_SIZE);
                    const Packets iPackets = replayPath1;
                    // Below the budget the interpreter always finished; at or above it, it may have been cut
                    // (or may have finished and then flushed past the budget, which is still a clean end).
                    const bool iClean = iCycles < s.h.maxCycles;
                    // candidate: compiled program
                    VU1State nst = fin;
                    std::memcpy(nativeData.data(), fdata.data(), PS2_VU1_DATA_SIZE);
                    Packets fPackets;
                    vu1c::Ctx fctx = ctx;
                    fctx.st = &nst;
                    fctx.sinkUser = &fPackets;
                    const int fr = prog->fn(fctx);
                    ++fuzzRuns;
                    if (fr != vu1c::Ok)
                    {
                        ++fuzzRefused[fr];
                        if (iClean)
                            ++fuzzRefusedButInterpClean; // allowed (fallback), but reported
                        continue;
                    }
                    std::vector<std::string> fd = compareState(iAfter, nst, iCycles, nst.cycles - fin.cycles);
                    if (!iClean)
                        ++fuzzBudgetBoundary;
                    if (nativeData != iData)
                        fd.push_back("data");
                    if (fPackets != iPackets)
                        fd.push_back("path1");
                    ++fuzzExitPc[nst.pc];
                    // Follow-up MSCNT from the state this call ended in (covers entries the capture never
                    // started from, e.g. 0x0348 after the 0x0310 exit): interpreter continues naturally,
                    // compiled code continues from its own result.
                    if (fd.empty())
                    {
                        const vu1c::Program *next = vu1c::lookup(s.h.codeHash, 1u, nst.pc);
                        if (next)
                        {
                            const VU1State fin2 = nst;
                            const uint64_t privBefore = vu1->state().cycles;
                            replayPath1.clear();
                            vu1->resume(mem->getVU1Code(), PS2_VU1_CODE_SIZE, mem->getVU1Data(), PS2_VU1_DATA_SIZE,
                                        *gs, mem.get(), s.h.top, s.h.itop, s.h.maxCycles);
                            const uint64_t iCycles2 = vu1->state().cycles - privBefore;
                            Packets fPackets2;
                            vu1c::Ctx c2 = fctx;
                            VU1State nst2 = fin2;
                            c2.st = &nst2;
                            c2.kind = 1u;
                            c2.sinkUser = &fPackets2;
                            const int fr2 = next->fn(c2);
                            std::vector<std::string> fd2;
                            if (fr2 != vu1c::Ok)
                                fd2.push_back("refused(" + std::to_string(fr2) + ")");
                            else
                            {
                                fd2 = compareState(vu1->state(), nst2, iCycles2, nst2.cycles - fin2.cycles);
                                if (std::memcmp(nativeData.data(), mem->getVU1Data(), PS2_VU1_DATA_SIZE) != 0)
                                    fd2.push_back("data");
                                if (fPackets2 != replayPath1)
                                    fd2.push_back("path1");
                            }
                            ++fuzzFollowUp[fin2.pc];
                            if (!fd2.empty() && !(fr2 != vu1c::Ok && iCycles2 >= s.h.maxCycles))
                            {
                                fd.push_back("followup@0x" + hex64(fin2.pc).substr(12));
                                for (const std::string &x : fd2)
                                    fd.push_back(x);
                            }
                        }
                    }
                    if (fd.empty())
                        ++fuzzExact;
                    else
                    {
                        ++fuzzMismatch;
                        if (fuzzFirstMismatch.empty())
                        {
                            fuzzFirstMismatch = "call " + std::to_string(s.h.callIndex) + " variant " + std::to_string(variant) + ":";
                            for (const std::string &d : fd)
                                fuzzFirstMismatch += " " + d;
                        }
                        if (regressionOut)
                            regressionCalls.push_back((s.h.callIndex << 8) | variant);
                    }
                }

                // Fallback self-checks on the first sample of each key. An unknown entry and a pending
                // branch on resume are refused before the state is touched. A cycle budget the program
                // cannot finish within is reported as Budget after partial execution: the caller must
                // discard the partial result (state, data, PATH1) and run the interpreter from its snapshot.
                if (kr.nativeExact + kr.nativeMismatch + kr.nativeFallback.size() == 1 && result == vu1c::Ok)
                {
                    vu1c::Ctx probe = ctx;
                    VU1State pst = s.before;
                    probe.st = &pst;
                    std::memcpy(nativeData.data(), s.dataBefore.data(), PS2_VU1_DATA_SIZE);
                    probe.kind = 1;
                    pst.pc = 0x3FF8u;
                    fallbackChecks.push_back(prog->fn(probe) == vu1c::UnsupportedEntry);
                    pst = s.before;
                    pst.pc = entryPc;
                    pst.branchPending = true;
                    probe.kind = s.h.kind == 0 ? 1 : s.h.kind;
                    fallbackChecks.push_back(s.h.kind == 0 ? true : prog->fn(probe) == vu1c::Precondition);
                    pst = s.before;
                    probe.kind = s.h.kind;
                    probe.maxCycles = 8u;
                    nativePackets.clear();
                    fallbackChecks.push_back(replayCycles <= 8u || prog->fn(probe) == vu1c::Budget);
                }
            }
        }

        if (profile)
        {
            // Cycle-by-cycle re-run: resume() with a 1-cycle budget keeps the scheduler between calls,
            // so the same program advances one VU cycle per call; each cycle is charged to the pair at
            // the PC before the step (stall cycles are charged to the waiting pair). The stepped run must
            // end in exactly the captured state, otherwise the profile of that sample is discarded.
            std::memcpy(mem->getVU1Data(), s.dataBefore.data(), PS2_VU1_DATA_SIZE);
            vu1->state() = s.before;
            vu1->execute(mem->getVU1Code(), PS2_VU1_CODE_SIZE, mem->getVU1Data(), PS2_VU1_DATA_SIZE,
                         *gs, mem.get(), 0u, s.h.top, s.h.itop, 0u);
            const uint64_t base = vu1->state().cycles;
            vu1->state() = s.before;
            vu1->state().cycles = base; // run() only writes state().cycles back at the end of each call
            replayPath1.clear();
            const uint64_t target = s.h.cyclesAfter - s.h.cyclesBefore;
            std::vector<uint32_t> perPc(PS2_VU1_CODE_SIZE / 8u, 0u);
            std::vector<uint32_t> perPcIssued(PS2_VU1_CODE_SIZE / 8u, 0u);
            bool first = true;
            uint64_t steps = 0;
            while (vu1->state().cycles - base < target && steps < 4u * target + 16u)
            {
                const uint32_t pc = vu1->state().pc;
                const uint64_t c0 = vu1->state().cycles;
                if (first && s.h.kind == 0)
                    vu1->execute(mem->getVU1Code(), PS2_VU1_CODE_SIZE, mem->getVU1Data(), PS2_VU1_DATA_SIZE,
                                 *gs, mem.get(), s.h.startPC, s.h.top, s.h.itop, 1u);
                else
                    vu1->resume(mem->getVU1Code(), PS2_VU1_CODE_SIZE, mem->getVU1Data(), PS2_VU1_DATA_SIZE,
                                *gs, mem.get(), s.h.top, s.h.itop, 1u);
                first = false;
                ++steps;
                const uint32_t chargedPc = (s.h.kind == 0 && steps == 1) ? s.h.startPC : pc;
                perPc[(chargedPc & 0x3FFFu) / 8u] += static_cast<uint32_t>(vu1->state().cycles - c0);
                // A step issued the pair when the PC moved (a pair branching to itself is not counted).
                if (vu1->state().pc != pc || (s.h.kind == 0 && steps == 1))
                    ++perPcIssued[(chargedPc & 0x3FFFu) / 8u];
            }
            std::vector<uint8_t> stepData(mem->getVU1Data(), mem->getVU1Data() + PS2_VU1_DATA_SIZE);
            const bool stepExact = compareState(s.after, vu1->state(), target, vu1->state().cycles - base).empty() &&
                                   stepData == s.dataAfter && replayPath1 == s.path1;
            if (stepExact)
            {
                ++profileExact;
                profileSamples.push_back({s.h.codeHash, s.h.startPC, s.h.kind,
                                          s.h.callIndex, target, std::move(perPc), std::move(perPcIssued)});
            }
            else
                ++profileMismatch;
        }

        std::vector<std::string> diffs = compareState(s.after, replayAfter, s.h.cyclesAfter - s.h.cyclesBefore, replayCycles);
        if (replayData != s.dataAfter)
        {
            uint32_t first = 0;
            while (first < PS2_VU1_DATA_SIZE && replayData[first] == s.dataAfter[first])
                ++first;
            diffs.push_back("data@0x" + hex64(first).substr(12));
        }
        if (replayPackets != s.path1)
            diffs.push_back("path1(" + std::to_string(s.path1.size()) + " vs " + std::to_string(replayPackets.size()) + ")");

        if (diffs.empty())
        {
            ++exact;
            ++kr.exact;
        }
        else
        {
            ++mismatched;
            ++kr.mismatched;
            std::string joined;
            for (const std::string &d : diffs)
            {
                ++kr.fieldDiffs[d.substr(0, d.find('@'))];
                joined += (joined.empty() ? "" : " ") + d;
            }
            if (kr.firstMismatch.empty())
                kr.firstMismatch = "call " + std::to_string(s.h.callIndex) + ": " + joined;
        }
    }
    std::fclose(f);
    if (!error.empty())
        std::fprintf(stderr, "stopped reading samples: %s\n", error.c_str());

    const uint64_t reproducible = total - skippedResume - skippedImage;
    if (profile)
    {
        // Samples were saved with cycle-weighted (length-biased) probability, plus the first calls of each
        // key. Excluding those first calls, the mean of per-sample cycle fractions estimates the share of
        // all VU1 cycles spent at each PC.
        std::map<std::tuple<uint64_t, uint32_t, uint32_t>, uint32_t> seen;
        std::vector<double> share(PS2_VU1_CODE_SIZE / 8u, 0.0);
        std::vector<uint64_t> raw(PS2_VU1_CODE_SIZE / 8u, 0u), touched(PS2_VU1_CODE_SIZE / 8u, 0u), issued(PS2_VU1_CODE_SIZE / 8u, 0u);
        uint64_t used = 0;
        for (const ProfileSample &ps : profileSamples)
        {
            if (seen[{ps.codeHash, ps.startPC, ps.kind}]++ < 4u || ps.cycles == 0u)
                continue;
            ++used;
            for (size_t i = 0; i < ps.perPc.size(); ++i)
                if (ps.perPc[i])
                {
                    share[i] += static_cast<double>(ps.perPc[i]) / static_cast<double>(ps.cycles);
                    raw[i] += ps.perPc[i];
                    issued[i] += ps.perPcIssued[i];
                    ++touched[i];
                }
        }
        if (FILE *pf = std::fopen((dir / "profile_pc.csv").string().c_str(), "wb"))
        {
            std::fprintf(pf, "pc,est_cycle_share,raw_cycles,raw_issued,samples_touching\n");
            for (size_t i = 0; i < share.size(); ++i)
                if (raw[i])
                    std::fprintf(pf, "0x%04zx,%.8f,%" PRIu64 ",%" PRIu64 ",%" PRIu64 "\n", i * 8u,
                                 used ? share[i] / used : 0.0, raw[i], issued[i], touched[i]);
            std::fclose(pf);
        }
        if (FILE *pf = std::fopen((dir / "profile_samples.csv").string().c_str(), "wb"))
        {
            // One row per profiled sample: entry, cycles and the ordered list of distinct PCs it touched
            // (its path signature), for grouping calls by code path.
            std::fprintf(pf, "call_index,start_pc,kind,cycles,pcs\n");
            for (const ProfileSample &ps : profileSamples)
            {
                std::fprintf(pf, "%" PRIu64 ",0x%04x,%u,%" PRIu64 ",", ps.callIndex, ps.startPC, ps.kind, ps.cycles);
                for (size_t i = 0; i < ps.perPc.size(); ++i)
                    if (ps.perPc[i])
                        std::fprintf(pf, "%zx:%u;", i * 8u, ps.perPc[i]);
                std::fprintf(pf, "\n");
            }
            std::fclose(pf);
        }
        std::printf("profile: stepped_exact=%" PRIu64 " stepped_mismatch=%" PRIu64 " used_for_estimate=%" PRIu64 "\n",
                    profileExact, profileMismatch, used);
    }
    if (FILE *r = std::fopen(reportPath.string().c_str(), "wb"))
    {
        std::fprintf(r, "{\n  \"samples\": %" PRIu64 ", \"replayed\": %" PRIu64 ", \"exact\": %" PRIu64
                        ", \"mismatched\": %" PRIu64 ",\n  \"skipped_resume\": %" PRIu64 ", \"skipped_missing_image\": %" PRIu64
                        ", \"unverified_code_hash\": %" PRIu64 ", \"generation_bump_failures\": %" PRIu64
                        ", \"repeat\": %u,\n  \"read_error\": \"%s\",\n  \"keys\": [\n",
                     total, reproducible, exact, mismatched, skippedResume, skippedImage, unverifiedHash,
                     generationBumpFailures, repeat, error.c_str());
        bool firstKey = true;
        for (const auto &[key, kr] : perKey)
        {
            std::fprintf(r, "%s    {\"code_hash\": \"%s\", \"start_pc\": %u, \"kind\": %u, \"samples\": %" PRIu64
                            ", \"exact\": %" PRIu64 ", \"mismatched\": %" PRIu64 ", \"skipped\": %" PRIu64
                            ", \"replay_cycles\": %" PRIu64 ", \"replay_host_ns\": %" PRIu64 ", \"capture_host_ns\": %" PRIu64
                            ", \"field_diffs\": {",
                         firstKey ? "" : ",\n", hex64(std::get<0>(key)).c_str(), std::get<1>(key), std::get<2>(key),
                         kr.samples, kr.exact, kr.mismatched, kr.skipped, kr.cycles, kr.hostNs, kr.captureHostNs);
            bool firstField = true;
            for (const auto &[field, n] : kr.fieldDiffs)
            {
                std::fprintf(r, "%s\"%s\": %" PRIu64, firstField ? "" : ", ", field.c_str(), n);
                firstField = false;
            }
            std::fprintf(r, "}, \"first_mismatch\": \"%s\"", kr.firstMismatch.c_str());
            if (native)
            {
                std::fprintf(r, ",\n      \"native\": {\"exact\": %" PRIu64 ", \"mismatched\": %" PRIu64 ", \"cycles\": %" PRIu64
                                ", \"native_ns\": %" PRIu64 ", \"interp_ns_same_samples\": %" PRIu64 ", \"lookup_ns\": %" PRIu64
                                ", \"copy_ns\": %" PRIu64 ", \"verify_ns\": %" PRIu64 ", \"fallback_interp_ns\": %" PRIu64 ", \"fallback\": {",
                             kr.nativeExact, kr.nativeMismatch, kr.nativeCycles, kr.nativeNs, kr.nativeInterpNs,
                             kr.nativeLookupNs, kr.nativeCopyNs, kr.nativeVerifyNs, kr.nativeFallbackInterpNs);
                bool ff = true;
                for (const auto &[why, n] : kr.nativeFallback)
                {
                    std::fprintf(r, "%s\"%s\": %" PRIu64, ff ? "" : ", ", why.c_str(), n);
                    ff = false;
                }
                std::fprintf(r, "}, \"field_diffs\": {");
                ff = true;
                for (const auto &[field, n] : kr.nativeFieldDiffs)
                {
                    std::fprintf(r, "%s\"%s\": %" PRIu64, ff ? "" : ", ", field.c_str(), n);
                    ff = false;
                }
                std::fprintf(r, "}, \"first_mismatch\": \"%s\"}", kr.nativeFirstMismatch.c_str());
            }
            std::fprintf(r, "}");
            firstKey = false;
        }
        std::fprintf(r, "\n  ]\n}\n");
        std::fclose(r);
    }
    std::printf("samples=%" PRIu64 " replayed=%" PRIu64 " exact=%" PRIu64 " mismatched=%" PRIu64
                " skipped_resume=%" PRIu64 " skipped_missing_image=%" PRIu64 " unverified_code_hash=%" PRIu64
                " generation_bump_failures=%" PRIu64 "\n",
                total, reproducible, exact, mismatched, skippedResume, skippedImage, unverifiedHash, generationBumpFailures);
    uint64_t nativeMismatchTotal = 0;
    if (dispatchTests)
    {
        for (const auto &[name, runs] : dtRuns)
        {
            std::printf("dispatch %s: runs=%" PRIu64 " failed=%" PRIu64 " %s\n", name.c_str(), runs, dtFail[name],
                        dtFirstFail[name].c_str());
            nativeMismatchTotal += dtFail[name];
        }
    }
    if (native)
    {
        uint64_t ne = 0, nf = 0, nNs = 0, iNs = 0, cyc = 0;
        for (const auto &[key, kr] : perKey)
        {
            ne += kr.nativeExact;
            nativeMismatchTotal += kr.nativeMismatch;
            for (const auto &[why, n] : kr.nativeFallback)
                nf += n;
            nNs += kr.nativeNs;
            iNs += kr.nativeInterpNs;
            cyc += kr.nativeCycles;
            std::printf("  native key %s pc=0x%04x kind=%u: exact=%" PRIu64 " mismatched=%" PRIu64 " interp_ns=%" PRIu64
                        " native_ns=%" PRIu64 " speedup=%.2fx %s\n",
                        hex64(std::get<0>(key)).c_str(), std::get<1>(key), std::get<2>(key), kr.nativeExact, kr.nativeMismatch,
                        kr.nativeInterpNs, kr.nativeNs, kr.nativeNs ? double(kr.nativeInterpNs) / double(kr.nativeNs) : 0.0,
                        kr.nativeFirstMismatch.c_str());
        }
        uint32_t okChecks = 0;
        for (bool b : fallbackChecks)
            okChecks += b ? 1u : 0u;
        std::printf("native: exact=%" PRIu64 " mismatched=%" PRIu64 " fallback=%" PRIu64 " interp_ns=%" PRIu64 " native_ns=%" PRIu64
                    " weighted_speedup=%.2fx cycles=%" PRIu64 " fallback_checks=%u/%zu\n",
                    ne, nativeMismatchTotal, nf, iNs, nNs, nNs ? double(iNs) / double(nNs) : 0.0, cyc, okChecks, fallbackChecks.size());
        if (okChecks != fallbackChecks.size())
            nativeMismatchTotal += 1;
        if (fuzzPerSample)
        {
            std::printf("fuzz: runs=%" PRIu64 " exact=%" PRIu64 " mismatched=%" PRIu64 " budget_boundary_compared=%" PRIu64
                        " refused_budget=%" PRIu64 " refused_fault=%" PRIu64 " refused_other=%" PRIu64
                        " refused_while_interp_clean=%" PRIu64 " %s\n",
                        fuzzRuns, fuzzExact, fuzzMismatch, fuzzBudgetBoundary, fuzzRefused[vu1c::Budget],
                        fuzzRefused[vu1c::Fault], fuzzRefused[vu1c::UnsupportedEntry] + fuzzRefused[vu1c::Precondition],
                        fuzzRefusedButInterpClean, fuzzFirstMismatch.c_str());
            std::printf("fuzz exit pcs:");
            for (const auto &[pc, n] : fuzzExitPc)
                std::printf(" 0x%04x=%" PRIu64, pc, n);
            std::printf("\nfuzz follow-up MSCNT entries:");
            for (const auto &[pc, n] : fuzzFollowUp)
                std::printf(" 0x%04x=%" PRIu64, pc, n);
            std::printf("\n");
            nativeMismatchTotal += fuzzMismatch;
        }
        if (regressionOut)
        {
            if (FILE *rf = std::fopen(regressionPath.string().c_str(), "wb"))
            {
                for (uint64_t call : regressionCalls)
                    std::fprintf(rf, "%" PRIu64 "\n", call);
                std::fclose(rf);
            }
        }
    }
    return (mismatched == 0 && nativeMismatchTotal == 0 && error.empty()) ? 0 : 1;
}
