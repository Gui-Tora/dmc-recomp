#include "game_overrides.h"

// No registrations until a concrete ELF and a measured blocker are available.
// Register with PS2_REGISTER_GAME_OVERRIDE(name, basename, entry, crc32, apply).
// Keep the corresponding SHA-256 and Ghidra evidence in analysis/notes.
// A custom raw handler must return to RA if it leaves PC unchanged.
