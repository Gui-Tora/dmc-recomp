// TEMPORARY P3.11 test infrastructure. Only included by dmc_overrides.cpp.
// ELF SLES_503.58 entry 00100008 CRC32 77654AD2;
// SHA256 d0753a6b3b2f00802a50758a872d8cf051725aa31c839aa8894eee30ce58bab4.
// Uses existing pad test override; no guest control-flow or MPEG changes.
// File format (hex): "MASK" as before (sticks centred), or "MASK LX LY" / "MASK LX LY RX RY" (GAMEPLAY_002:
// DMC walks only from the left stick, see analysis/notes/GAMEPLAY_002_FIRST_PLAYABLE_DIAGNOSIS.md). Stick bytes are
// raw pad bytes, 0x80 = centre. Unreadable values and commands older than 1 s fall back to neutral.
// While the channel is enabled the override replaces the real pad and keyboard (even when neutral); leave
// DMC_P311_PAD_FILE unset or empty to play by hand.
// Remove include + start call and recompile this ONE translation unit to retire.
#include <chrono>
#include "Stubs/Pad.h"
#include <cstdlib>
#include <cstring>
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
        unsigned previous[5] = {0x10000u, 0u, 0u, 0u, 0u};
        while (!stop.stop_requested()) {
            unsigned v[5] = {0xFFFFu, 0x80u, 0x80u, 0x80u, 0x80u};
            std::ifstream input(file);
            if (!(input >> std::hex >> v[0]) || v[0]>0xFFFFu) v[0]=0xFFFFu;
            unsigned sticks[4] = {0x80u, 0x80u, 0x80u, 0x80u};
            int read = 0;
            while (read < 4 && (input >> std::hex >> sticks[read])) ++read;
            if ((read == 2 || read == 4) && sticks[0] <= 0xFFu && sticks[1] <= 0xFFu && sticks[2] <= 0xFFu &&
                sticks[3] <= 0xFFu) {
                v[1] = sticks[0]; v[2] = sticks[1];
                if (read == 4) { v[3] = sticks[2]; v[4] = sticks[3]; }
            }
            // Fail neutral when the command is older than 1 second: a dead
            // harness must not leave a held confirmation button or a tilted stick.
            std::error_code ec;
            const auto modified=std::filesystem::last_write_time(file,ec);
            if (ec || std::filesystem::file_time_type::clock::now()-modified>std::chrono::seconds(1)) {
                v[0]=0xFFFFu; v[1]=v[2]=v[3]=v[4]=0x80u;
            }
            if (std::memcmp(v, previous, sizeof v) != 0) {
                ps2_stubs::setPadOverrideState(static_cast<uint16_t>(v[0]), static_cast<uint8_t>(v[1]),
                                               static_cast<uint8_t>(v[2]), static_cast<uint8_t>(v[3]),
                                               static_cast<uint8_t>(v[4]));
                std::fprintf(stderr,"[P311:pad] buttons=0x%04X lx=0x%02X ly=0x%02X rx=0x%02X ry=0x%02X\n",
                             v[0], v[1], v[2], v[3], v[4]);
                std::memcpy(previous, v, sizeof v);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        ps2_stubs::clearPadOverrideState();
    });
}
