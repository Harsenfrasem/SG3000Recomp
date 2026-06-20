#include "sgrecomp/host_runtime.h"

#include <array>
#include <cstddef>

extern "C" sgrecomp::HostInstructionResult sgrecomp_run_host_instruction(sgrecomp::Z80State& cpu, sgrecomp::Bus& bus);
extern "C" void sgrecomp_load_rom(sgrecomp::Bus& bus);

int main() {
    sgrecomp::HostRuntimeConfig config;
    config.execution_mode = sgrecomp::HostExecutionMode::Hybrid;
    config.instruction_executor = sgrecomp_run_host_instruction;
    sgrecomp::HostRuntime host(sgrecomp::ConsoleModel::SMS, config);
    sgrecomp_load_rom(host.console().bus());

    for (std::size_t frame = 0; frame < 60; ++frame) {
        (void)host.run_frame();
    }

    return 0;
}
