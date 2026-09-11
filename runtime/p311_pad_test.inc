// TEMPORARY P3.11 test infrastructure. Only included by dmc_overrides.cpp.
// ELF SLES_503.58 entry 00100008 CRC32 77654AD2;
// SHA256 d0753a6b3b2f00802a50758a872d8cf051725aa31c839aa8894eee30ce58bab4.
// Uses existing pad test override; no guest control-flow or MPEG changes.
// Remove include + start call and recompile this ONE translation unit to retire.
#include <chrono>
#include "Stubs/Pad.h"
#include <cstdlib>
#include <fstream>
#include <filesystem>
#include <string>
#include <thread>
static void startP311PadTest()
{
    const char* path = std::getenv("DMC_P311_PAD_FILE");
    if (!path || !*path) return;
    const std::string file(path);
    static std::jthread worker([file](std::stop_token stop) {
        std::fprintf(stderr, "[P311:pad] test channel enabled\n");
        unsigned previous = 0x10000u;
        while (!stop.stop_requested()) {
            unsigned mask=0xFFFFu;
            std::ifstream input(file);
            if (!(input >> std::hex >> mask) || mask>0xFFFFu) mask=0xFFFFu;
            // Fail neutral when the command is older than 1 second: a dead
            // harness must not leave a held confirmation button.
            std::error_code ec;
            const auto modified=std::filesystem::last_write_time(file,ec);
            if (ec || std::filesystem::file_time_type::clock::now()-modified>std::chrono::seconds(1)) mask=0xFFFFu;
            if (mask != previous) {
                ps2_stubs::setPadOverrideState(static_cast<uint16_t>(mask),128,128,128,128);
                std::fprintf(stderr,"[P311:pad] buttons=0x%04X\n",mask);
                previous=mask;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        ps2_stubs::clearPadOverrideState();
    });
}
