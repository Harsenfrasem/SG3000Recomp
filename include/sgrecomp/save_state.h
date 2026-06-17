#pragma once

#include "sgrecomp/console.h"
#include "sgrecomp/types.h"

#include <span>
#include <string>
#include <vector>

namespace sgrecomp {

struct SaveStateMetadata {
    bool present = false;
    ConsoleModel model = ConsoleModel::SMS;
    std::string rom_hash;
};

struct SaveStateImage {
    SaveStateMetadata metadata;
    ConsoleState state;
};

std::vector<u8> serialize_console_state(const ConsoleState& state, const SaveStateMetadata& metadata = {});
ConsoleState deserialize_console_state(std::span<const u8> bytes);
SaveStateImage deserialize_console_state_image(std::span<const u8> bytes);

std::vector<u8> save_console_state(const Console& console, const SaveStateMetadata& metadata = {});
void load_console_state(Console& console, std::span<const u8> bytes);
SaveStateMetadata read_save_state_metadata(std::span<const u8> bytes);
void validate_save_state_metadata(const SaveStateMetadata& actual, const SaveStateMetadata& expected);

} // namespace sgrecomp
