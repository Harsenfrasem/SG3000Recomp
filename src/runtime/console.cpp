#include "sgrecomp/console.h"

namespace sgrecomp {

Console::Console(ConsoleModel model) : bus_(model, vdp_, psg_, ym2413_, joypad_, &ym2612_), model_(model) {
    vdp_.set_game_gear(model_ == ConsoleModel::GameGear);
    if (model_ == ConsoleModel::SG3000) {
        vdp_.set_video_mode(VdpVideoMode::TmsGraphics1);
    }
}

Console::Console(ConsoleModel model, const EnhancementConfig& enhancements)
    : bus_(model, vdp_, psg_, ym2413_, joypad_, &ym2612_), model_(model), enhancements_(enhancements) {
    vdp_.set_game_gear(model_ == ConsoleModel::GameGear);
    if (model_ == ConsoleModel::SG3000) {
        vdp_.set_video_mode(VdpVideoMode::TmsGraphics1);
    }
    vdp_.set_enhancements(enhancements_);
    psg_.set_enhancements(enhancements_);
    bus_.set_fm_present(enhancements_.enable_fm);
    ym2612_.set_enabled(enhancements_.enable_ym2612);
}

void Console::load_rom(std::span<const u8> rom) {
    bus_.load_rom(rom);
}

void Console::load_bios(std::span<const u8> bios) {
    bus_.load_bios(bios);
}

void Console::reset() {
    cpu_ = {};
}

void Console::set_enhancements(const EnhancementConfig& enhancements) {
    enhancements_ = enhancements;
    vdp_.set_enhancements(enhancements_);
    psg_.set_enhancements(enhancements_);
    bus_.set_fm_present(enhancements_.enable_fm);
    ym2612_.set_enabled(enhancements_.enable_ym2612);
}

ConsoleState Console::save_state() const {
    return {
        cpu_,
        bus_.save_state(),
        vdp_.save_state(),
        psg_.save_state(),
        ym2413_.save_state(),
        ym2612_.save_state(),
        joypad_.player1(),
        joypad_.player2(),
    };
}

void Console::load_state(const ConsoleState& state) {
    cpu_ = state.cpu;
    bus_.load_state(state.bus);
    vdp_.load_state(state.vdp);
    vdp_.set_game_gear(model_ == ConsoleModel::GameGear);
    psg_.load_state(state.psg);
    ym2413_.load_state(state.ym2413);
    ym2612_.load_state(state.ym2612);
    enhancements_.enable_ym2612 = state.ym2612.enabled;
    joypad_.set_player1(state.joypad_player1);
    joypad_.set_player2(state.joypad_player2);
}

void Console::press_pause() {
    if (model_ == ConsoleModel::SMS) {
        service_non_maskable_interrupt(cpu_, bus_);
    }
}

void Console::run_cycles(u64 cycles) {
    const u64 target = cpu_.cycles + cycles;
    while (cpu_.cycles < target) {
        const u64 before = cpu_.cycles;
        bus_.set_cycle(before);
        vdp_.set_cycle(before);
        execute_one(cpu_, bus_);
        const int elapsed = static_cast<int>(cpu_.cycles - before);
        vdp_.tick(elapsed);
        psg_.tick(elapsed);
        ym2413_.tick(elapsed);
        ym2612_.tick(elapsed);
        if (vdp_.irq_pending()) {
            const u64 irq_before = cpu_.cycles;
            bus_.set_cycle(irq_before);
            vdp_.set_cycle(irq_before);
            if (service_maskable_interrupt(cpu_, bus_)) {
                const int irq_elapsed = static_cast<int>(cpu_.cycles - irq_before);
                vdp_.tick(irq_elapsed);
                psg_.tick(irq_elapsed);
                ym2413_.tick(irq_elapsed);
                ym2612_.tick(irq_elapsed);
            }
        }
    }
}

} // namespace sgrecomp
