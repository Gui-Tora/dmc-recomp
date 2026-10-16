// The bench executes no guest EE code. ps2_runtime.lib references the generated function table from
// PS2Runtime::dispatchGuestBranch; if that object file is linked, an empty table satisfies the reference,
// as ps2xTest's test_function_table.cpp does for the unit tests.
#include "ps2_runtime.h"

extern const uint32_t g_ps2RecompiledFunctionTableBase = 0u;
extern const uint32_t g_ps2RecompiledFunctionTableEnd = 0u;
extern const uint32_t g_ps2RecompiledFunctionTableSlotCount = 0u;
PS2Runtime::RecompiledFunction g_ps2RecompiledFunctionTable[1] = {};
