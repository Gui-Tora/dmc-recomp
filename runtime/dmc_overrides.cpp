#include "game_overrides.h"
#include "ps2_runtime.h"
#include "ps2_stubs.h"
#include "ipu/ps2_ipu.h"

#include <cstdio>
#include "p311_pad_test.inc"

// Instrumented ret0 stand-ins for the 17 R5900/VU0 functions that are still not
// restored (SLES_503.58). History: they were stubbed when Ghidra's R6 language
// produced bogus 1-3.6MB boundaries (analysis/notes/HUGE_GENERATED_FILES.md); the
// ELF .symtab now gives correct boundaries and their native bodies generate cleanly.
// The old claim of "zero real callers" was RETRACTED: the ELF has 1544 JAL to the
// original 20 entries (analysis/notes/GAMEPLAY_001_RET0_AUDIT.md).
// Restored to their generated native bodies (no longer overridden):
//   capVu0MulMatrix2   @0x001A0080 (RET0_FIRSTCALL, matches ORIGINAL oracle)
//   capVu0AddVectorXYZ @0x0019FBB0, capVu0SubVectorXYZ @0x0019FBD0 (GAMEPLAY_001I)
// Status and restoration order: analysis/notes/GAMEPLAY_001_RET0_RESTORATION_STATUS.md.
// Keep recomp/symtabfirst.toml ret0@ entries and this list in sync.

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
    DMC_INSTRUMENTED_RET0(dmcStub_Get_light_str, 0x0016D8E0u)
    DMC_INSTRUMENTED_RET0(dmcStub_capVu0Compare, 0x001A0300u)

#undef DMC_INSTRUMENTED_RET0

    void applyHugeFunctionInstrumentation(PS2Runtime &runtime)
    {
        startP311PadTest();
        runtime.registerFunction(0x0016B8E0u, dmcStub_Calc_md);
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

// GAMEPLAY_001L: guest layout of libipu for the sceIpuInit HLE (SLES_503.58, SHA-256 d0753a6b...bab4,
// entry 0x00100008, CRC32 77654AD2). From the ELF: sceIpuInit@0x0010B868 calls setD4_CHCR@0x0010B800 with
// a0=1, writes 8 qwords from 0x004FC860 to the IPU input FIFO (+0x00..+0x30, then +0x40 four times: default
// intra matrix in zigzag order and a flat non-intra matrix) and 2 qwords from 0x004FC8B0 (VQCLUT). The stub's
// built-in addresses fall inside Homing2 (code) in this ELF.
namespace
{
    void applyLibIpuInitLayout(PS2Runtime &)
    {
        ps2_ipu::LibIpuInitLayout layout;
        layout.setD4ChcrEntry = 0x0010B800u;
        layout.iqTable = 0x004FC860u;
        layout.vqTable = 0x004FC8B0u;
        ps2_ipu::setLibIpuInitLayout(layout);
    }
}

PS2_REGISTER_GAME_OVERRIDE(
    "dmc-sles-503.58-libipu-init-layout",
    "SLES_503.58",
    0x00100008u,
    0x77654AD2u,
    applyLibIpuInitLayout);
