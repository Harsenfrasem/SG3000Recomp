#include "sgrecomp/host_runtime.h"

#include <algorithm>
#include <stdexcept>

namespace sgrecomp {

const char* host_execution_mode_name(HostExecutionMode mode) {
    switch (mode) {
    case HostExecutionMode::Interpreter:
        return "interpreter";
    case HostExecutionMode::Hybrid:
        return "hybrid";
    case HostExecutionMode::Recompiled:
        return "recompiled";
    }
    return "unknown";
}

namespace {

void validate_execution_config(const HostRuntimeConfig& config) {
    if (config.execution_mode != HostExecutionMode::Interpreter && config.instruction_executor == nullptr) {
        throw std::invalid_argument("hybrid/recompiled HostRuntime requires an instruction executor");
    }
}

} // namespace

HostRuntime::HostRuntime(ConsoleModel model, HostRuntimeConfig config) : console_(model), config_(config) {
    validate_execution_config(config_);
    console_.vdp().set_timing({config_.cpu_cycles_per_scanline, config_.scanlines_per_frame});
}

HostRuntime::HostRuntime(ConsoleModel model, const EnhancementConfig& enhancements, HostRuntimeConfig config)
    : console_(model, enhancements), config_(config) {
    validate_execution_config(config_);
    console_.vdp().set_timing({config_.cpu_cycles_per_scanline, config_.scanlines_per_frame});
}

void HostRuntime::load_rom(std::span<const u8> rom) {
    console_.load_rom(rom);
}

void HostRuntime::load_bios(std::span<const u8> bios) {
    console_.load_bios(bios);
}

void HostRuntime::reset() {
    console_.reset();
    frame_index_ = 0;
    audio_cycle_accumulator_ = 0;
    previous_pause_ = false;
    audio_.clear();
}

HostRuntimeState HostRuntime::save_state() const {
    return {console_.save_state(), frame_index_, audio_cycle_accumulator_, previous_pause_};
}

void HostRuntime::load_state(const HostRuntimeState& state) {
    console_.load_state(state.console);
    frame_index_ = state.frame_index;
    audio_cycle_accumulator_ = state.audio_cycle_accumulator;
    previous_pause_ = state.previous_pause;
    audio_.clear();
}

HostFrameResult HostRuntime::run_frame(const HostInputState& input) {
    apply_input(input);

    const u64 start_cycle = console_.cpu().cycles;
    const u64 target_cycle = start_cycle + config_.cycles_per_frame();
    std::size_t instructions = 0;
    std::size_t interpreted = 0;
    std::size_t recompiled = 0;
    std::size_t fallback = 0;
    u16 pc_min = 0xFFFF;
    u16 pc_max = 0;
    run_until_cycle(target_cycle, instructions, pc_min, pc_max, interpreted, recompiled, fallback);

    const HostFrameResult result{
        frame_index_,
        start_cycle,
        console_.cpu().cycles,
        audio_.size() / 2,
        instructions,
        instructions == 0 ? u16{0} : pc_min,
        instructions == 0 ? u16{0} : pc_max,
        console_.cpu().halted,
        interpreted,
        recompiled,
        fallback,
    };
    ++frame_index_;
    return result;
}

void HostRuntime::clear_audio() {
    audio_.clear();
}

const Vdp::Framebuffer& HostRuntime::framebuffer() const {
    return console_.vdp().display_framebuffer();
}

void HostRuntime::apply_input(const HostInputState& input) {
    const u8 player1 = console_.model() == ConsoleModel::GameGear && input.pause
                           ? static_cast<u8>(input.player1 | Joypad::Start)
                           : input.player1;
    console_.joypad().set_player1(player1);
    console_.joypad().set_player2(input.player2);
    if (input.pause && !previous_pause_) {
        console_.press_pause();
    }
    previous_pause_ = input.pause;
}

void HostRuntime::run_until_cycle(u64 target_cycle,
                                  std::size_t& instructions,
                                  u16& pc_min,
                                  u16& pc_max,
                                  std::size_t& interpreted,
                                  std::size_t& recompiled,
                                  std::size_t& fallback) {
    while (console_.cpu().cycles < target_cycle) {
        pc_min = std::min(pc_min, console_.cpu().pc);
        pc_max = std::max(pc_max, console_.cpu().pc);
        ++instructions;
        const u64 before = console_.cpu().cycles;
        console_.bus().set_cycle(before);
        console_.vdp().set_cycle(before);
        console_.psg().set_cycle(before);
        console_.ym2413().set_cycle(before);
        if (config_.execution_mode == HostExecutionMode::Interpreter) {
            execute_one(console_.cpu(), console_.bus());
            ++interpreted;
        } else {
            const HostInstructionResult result = config_.instruction_executor(console_.cpu(), console_.bus());
            if (result == HostInstructionResult::Recompiled) {
                ++recompiled;
            } else {
                ++fallback;
                if (config_.execution_mode == HostExecutionMode::Recompiled) {
                    throw std::runtime_error("strict recompiled mode encountered an interpreter fallback");
                }
            }
        }
        tick_devices(static_cast<int>(console_.cpu().cycles - before));

        if (console_.vdp().irq_pending()) {
            const u64 irq_before = console_.cpu().cycles;
            console_.bus().set_cycle(irq_before);
            console_.vdp().set_cycle(irq_before);
            console_.psg().set_cycle(irq_before);
            console_.ym2413().set_cycle(irq_before);
            if (service_maskable_interrupt(console_.cpu(), console_.bus())) {
                tick_devices(static_cast<int>(console_.cpu().cycles - irq_before));
            }
        }
    }
}

void HostRuntime::tick_devices(int elapsed_cycles) {
    if (elapsed_cycles <= 0) {
        return;
    }
    console_.vdp().tick(elapsed_cycles);
    console_.psg().tick(elapsed_cycles);
    console_.ym2413().tick(elapsed_cycles);
    console_.ym2612().tick(elapsed_cycles);
    append_audio_samples(elapsed_cycles);
}

void HostRuntime::append_audio_samples(int elapsed_cycles) {
    audio_cycle_accumulator_ += static_cast<u64>(elapsed_cycles) * config_.audio_sample_rate;
    while (audio_cycle_accumulator_ >= config_.cpu_clock_hz) {
        audio_cycle_accumulator_ -= config_.cpu_clock_hz;
        const auto psg = console_.ym2413().psg_enabled() ? console_.psg().sample() : std::array<float, 2>{0.0F, 0.0F};
        const auto fm = console_.ym2413().sample();
        const auto ym2612 = console_.ym2612().sample();
        const std::array<float, 2> mixed{psg[0] + fm[0] + ym2612[0], psg[1] + fm[1] + ym2612[1]};
        for (float channel : mixed) {
            const float clipped = std::clamp(channel, -1.0F, 1.0F);
            audio_.push_back(static_cast<s16>(clipped * 32767.0F));
        }
    }
}

} // namespace sgrecomp
