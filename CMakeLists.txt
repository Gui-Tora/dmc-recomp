cmake_minimum_required(VERSION 3.21)
project(DmcBringup LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(DMC_GENERATED_DIR "" CACHE PATH "Verified PS2Recomp output directory")
if(NOT EXISTS "${DMC_GENERATED_DIR}/register_functions.cpp")
    message(FATAL_ERROR "Run scripts/pipeline.py generate first and pass DMC_GENERATED_DIR")
endif()
set(PS2X_BUILD_RECOMP OFF CACHE BOOL "" FORCE)
set(PS2X_BUILD_ANALYZER OFF CACHE BOOL "" FORCE)
set(PS2X_BUILD_TEST OFF CACHE BOOL "" FORCE)
set(PS2X_BUILD_STUDIO OFF CACHE BOOL "" FORCE)
set(PS2X_BUILD_RUNTIME ON CACHE BOOL "" FORCE)
set(PS2X_ENABLE_RUNTIME_LOGS ON CACHE BOOL "" FORCE)
set(PS2X_ENABLE_IOP_RPC_TRACE ON CACHE BOOL "" FORCE)
set(PS2X_ENABLE_DEBUG_UI OFF CACHE BOOL "Initial bring-up")
set(PS2X_ENABLE_FFMPEG OFF CACHE BOOL "Initial bring-up; no video decode")
set(PS2X_ENABLE_RUNNER_UNITY_BUILD OFF CACHE BOOL "" FORCE)
set(PS2X_ENABLE_RUNNER_PCH OFF CACHE BOOL "" FORCE)
add_subdirectory(vendor/PS2Recomp upstream)
# Replace the empty upstream table in the target, without editing vendor files.
get_target_property(runner_sources ps2EntryRunner SOURCES)
list(FILTER runner_sources EXCLUDE REGEX "[/\\\\]register_functions\\.cpp$")
set_property(TARGET ps2EntryRunner PROPERTY SOURCES "${runner_sources}")
file(GLOB generated_sources CONFIGURE_DEPENDS "${DMC_GENERATED_DIR}/*.cpp")
target_sources(ps2EntryRunner PRIVATE ${generated_sources} runtime/dmc_overrides.cpp)
# GAMEPLAY_001L: the DMC overrides configure the runtime's private IPU module (ps2xRuntime/src/lib/ipu).
set_source_files_properties(runtime/dmc_overrides.cpp TARGET_DIRECTORY ps2EntryRunner PROPERTIES
    INCLUDE_DIRECTORIES "${CMAKE_SOURCE_DIR}/vendor/PS2Recomp/ps2xRuntime/src/lib")
# GAMEPLAY_001K: VU1 programs compiled ahead of time from this game's microcode. They are generated
# locally by tools/vu1_replay/make_vu1c.py from the user's own ELF into recomp/vu1c/ (ignored by Git).
# Without them the build is unchanged and every VU1 call is interpreted; with them, PS2X_VU1_NATIVE
# selects interpreter (default when unset), native or verify at run time.
file(GLOB dmc_vu1c_sources CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/recomp/vu1c/*.cpp")
if(dmc_vu1c_sources)
    target_sources(ps2EntryRunner PRIVATE ${dmc_vu1c_sources})
    # per-source include directory: the generated game sources keep their compile flags unchanged
    set_source_files_properties(${dmc_vu1c_sources} TARGET_DIRECTORY ps2EntryRunner PROPERTIES
        INCLUDE_DIRECTORIES "${CMAKE_SOURCE_DIR}/vendor/PS2Recomp/ps2xRuntime/src/lib")
    # PERF_001: one generated function per image grows past MSVC's size limit (C4883 "function size
    # suppresses optimizations"); keep optimizing it in Release. Only these generated VU1 sources.
    if(MSVC)
        set_source_files_properties(${dmc_vu1c_sources} TARGET_DIRECTORY ps2EntryRunner PROPERTIES
            COMPILE_OPTIONS "$<$<CONFIG:Release>:/d2OptimizeHugeFunctions>")
    endif()
endif()
target_include_directories(ps2EntryRunner BEFORE PRIVATE "${DMC_GENERATED_DIR}")
set_target_properties(ps2EntryRunner PROPERTIES OUTPUT_NAME dmc-recomp
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(DMC_MSVC_MP_JOBS "0" CACHE STRING "MSVC /MP job count for ps2EntryRunner (0 = let the compiler use all logical cores)")
if(MSVC)
    if(DMC_MSVC_MP_JOBS STREQUAL "0")
        target_compile_options(ps2EntryRunner PRIVATE /MP)
    else()
        target_compile_options(ps2EntryRunner PRIVATE "/MP${DMC_MSVC_MP_JOBS}")
    endif()
endif()
