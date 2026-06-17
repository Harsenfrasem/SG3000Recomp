#pragma once

#include "sgrecomp/console.h"
#include "sgrecomp/types.h"

#include <span>
#include <vector>

namespace sgrecomp {

std::vector<u8> serialize_console_state(const ConsoleState& state);
ConsoleState deserialize_console_state(std::span<const u8> bytes);

std::vector<u8> save_console_state(const Console& console);
void load_console_state(Console& console, std::span<const u8> bytes);

} // namespace sgrecomp
