#include "sgrecomp/console.h"

namespace sgrecomp {

Console::Console(ConsoleModel model)
    : bus_(model, vdp_, psg_, joypad_) {}

void Console::load_rom(std::span<const u8> rom) {
    bus_.load_rom(rom);
}

void Console::reset() {
    cpu_ = {};
}

void Console::run_cycles(u64 cycles) {
    const u64 target = cpu_.cycles + cycles;
    while (cpu_.cycles < target) {
        const u64 before = cpu_.cycles;
        execute_one(cpu_, bus_);
        const int elapsed = static_cast<int>(cpu_.cycles - before);
        vdp_.tick(elapsed);
        psg_.tick(elapsed);
        if (vdp_.irq_pending()) {
            const u64 irq_before = cpu_.cycles;
            if (service_maskable_interrupt(cpu_, bus_)) {
                const int irq_elapsed = static_cast<int>(cpu_.cycles - irq_before);
                vdp_.tick(irq_elapsed);
                psg_.tick(irq_elapsed);
            }
        }
    }
}

} // namespace sgrecomp
