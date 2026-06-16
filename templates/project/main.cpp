#include "sgrecomp/console.h"

#include <array>
#include <cstddef>

extern "C" void sgrecomp_run_instruction(sgrecomp::Z80State& cpu, sgrecomp::Bus& bus);
extern "C" void sgrecomp_load_rom(sgrecomp::Bus& bus);

int main() {
    sgrecomp::Console console(sgrecomp::ConsoleModel::MasterSystem);
    sgrecomp_load_rom(console.bus());

    for (std::size_t i = 0; i < 1000000; ++i) {
        sgrecomp_run_instruction(console.cpu(), console.bus());
    }

    return 0;
}
