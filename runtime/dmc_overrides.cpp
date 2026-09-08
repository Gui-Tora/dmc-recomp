#include "game_overrides.h"
#include "ps2_runtime.h"
#include "ps2_stubs.h"

#include <cstdio>

// Instrumented stand-ins for 20 addresses whose exported CSV/Ghidra boundaries were
// bogus (1-3.6MB "functions" caused by Ghidra decoding COP2/VU0/MMI R5900 opcodes as
// MIPS Release 6 instructions - language MIPS:LE:64:64-32R6addr has no R5900 support).
// See analysis/notes/HUGE_GENERATED_FILES.md for full evidence. Static analysis found
// zero real callers (no JAL/JALR, no lui+ori/addiu address construction anywhere in
// ~180k scanned instructions); the only reference to these addresses anywhere in the
// binary is inside the ".mwcats" Metrowerks CodeWarrior debug metadata section, not
// game code. This does NOT prove they are dead - VU0 macro-mode prologues (LQC2 loads
// seen in the PS2Recomp-decoded bytes) look like real matrix/vector math. This
// instrumentation counts real calls at runtime to settle it empirically.
//
// Retire this file once either: (a) a proper R5900-aware Ghidra language lets us get
// real function boundaries and recompile these normally, or (b) enough runtime
// evidence (call counts over a real playthrough) shows they are truly unreachable.

namespace
{
    struct StubCallCounter
    {
        const char *name;
        uint32_t address;
        unsigned callCount = 0;

        void hit(R5900Context *ctx)
        {
            ++callCount;
            if (callCount == 1 || callCount % 50 == 0)
            {
                std::fprintf(stderr,
                    "[dmc-stub-instrumentation] %s@0x%08X call #%u a0=0x%08X a1=0x%08X a2=0x%08X a3=0x%08X ra=0x%08X\n",
                    name, address, callCount,
                    getRegU32(ctx, 4), getRegU32(ctx, 5), getRegU32(ctx, 6), getRegU32(ctx, 7),
                    getRegU32(ctx, 31));
            }
        }
    };

#define DMC_INSTRUMENTED_RET0(FN_NAME, ADDR_U32)                                        \
    void FN_NAME(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)                \
    {                                                                                    \
        static StubCallCounter counter{#FN_NAME, ADDR_U32};                             \
        counter.hit(ctx);                                                               \
        ps2_stubs::ret0(rdram, ctx, runtime);                                           \
    }

    DMC_INSTRUMENTED_RET0(dmcStub_Calc_md, 0x0016B8E0u)
    DMC_INSTRUMENTED_RET0(dmcStub_capVu0MulMatrix2, 0x001A0080u)
    DMC_INSTRUMENTED_RET0(dmcStub_capVu0MulMatrix_r, 0x001A0180u)
    DMC_INSTRUMENTED_RET0(dmcStub_Calc_scr_light, 0x0016D920u)
    DMC_INSTRUMENTED_RET0(dmcStub_katVu0ViewVolumeClip, 0x00132D20u)
    DMC_INSTRUMENTED_RET0(dmcStub_katVu0Setmat4_shw, 0x001DA7B0u)
    DMC_INSTRUMENTED_RET0(dmcStub_InterpNode, 0x00132DD0u)
    DMC_INSTRUMENTED_RET0(dmcStub_ClipCheck2, 0x00132EC0u)
    DMC_INSTRUMENTED_RET0(dmcStub_katVu0RotTransPersQ, 0x00132EF0u)
    DMC_INSTRUMENTED_RET0(dmcStub_katVu0Clip_ld, 0x00132DA0u)
    DMC_INSTRUMENTED_RET0(dmcStub_katVu0RotTransPers_shw, 0x001DA8E0u)
    DMC_INSTRUMENTED_RET0(dmcStub_sceVu0SetRotTransPers, 0x001D5E40u)
    DMC_INSTRUMENTED_RET0(dmcStub_katVu0SetClip, 0x00132D70u)
    DMC_INSTRUMENTED_RET0(dmcStub_capCopyMatrix, 0x001A01F0u)
    DMC_INSTRUMENTED_RET0(dmcStub_capVu0ScaleVectorXYZ2, 0x0019FC40u)
    DMC_INSTRUMENTED_RET0(dmcStub_capVu0ecossin, 0x001A03C0u)
    DMC_INSTRUMENTED_RET0(dmcStub_capVu0AddVectorXYZ, 0x0019FBB0u)
    DMC_INSTRUMENTED_RET0(dmcStub_capVu0SubVectorXYZ, 0x0019FBD0u)
    DMC_INSTRUMENTED_RET0(dmcStub_Get_light_str, 0x0016D8E0u)
    DMC_INSTRUMENTED_RET0(dmcStub_capVu0Compare, 0x001A0300u)

#undef DMC_INSTRUMENTED_RET0

    void applyHugeFunctionInstrumentation(PS2Runtime &runtime)
    {
        runtime.registerFunction(0x0016B8E0u, dmcStub_Calc_md);
        runtime.registerFunction(0x001A0080u, dmcStub_capVu0MulMatrix2);
        runtime.registerFunction(0x001A0180u, dmcStub_capVu0MulMatrix_r);
        runtime.registerFunction(0x0016D920u, dmcStub_Calc_scr_light);
        runtime.registerFunction(0x00132D20u, dmcStub_katVu0ViewVolumeClip);
        runtime.registerFunction(0x001DA7B0u, dmcStub_katVu0Setmat4_shw);
        runtime.registerFunction(0x00132DD0u, dmcStub_InterpNode);
        runtime.registerFunction(0x00132EC0u, dmcStub_ClipCheck2);
        runtime.registerFunction(0x00132EF0u, dmcStub_katVu0RotTransPersQ);
        runtime.registerFunction(0x00132DA0u, dmcStub_katVu0Clip_ld);
        runtime.registerFunction(0x001DA8E0u, dmcStub_katVu0RotTransPers_shw);
        runtime.registerFunction(0x001D5E40u, dmcStub_sceVu0SetRotTransPers);
        runtime.registerFunction(0x00132D70u, dmcStub_katVu0SetClip);
        runtime.registerFunction(0x001A01F0u, dmcStub_capCopyMatrix);
        runtime.registerFunction(0x0019FC40u, dmcStub_capVu0ScaleVectorXYZ2);
        runtime.registerFunction(0x001A03C0u, dmcStub_capVu0ecossin);
        runtime.registerFunction(0x0019FBB0u, dmcStub_capVu0AddVectorXYZ);
        runtime.registerFunction(0x0019FBD0u, dmcStub_capVu0SubVectorXYZ);
        runtime.registerFunction(0x0016D8E0u, dmcStub_Get_light_str);
        runtime.registerFunction(0x001A0300u, dmcStub_capVu0Compare);
    }
}

PS2_REGISTER_GAME_OVERRIDE(
    "dmc-sles-503.58-huge-fn-instrumentation",
    "SLES_503.58",
    0x00100008u,
    0x77654AD2u,
    applyHugeFunctionInstrumentation);
