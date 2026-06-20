#include "sgrecomp/ym2612.h"

#include "ym3438.h"

#include <algorithm>
#include <cstring>

namespace sgrecomp {
namespace {

constexpr u64 kCpuClock = 3579545;
constexpr u64 kYm2612Clock = 7670454;
constexpr u64 kMasterClocksPerInternalClock = 6;

std::size_t register_index(int bank, u8 address) {
    return static_cast<std::size_t>((bank & 1) * 0x100 + address);
}

int channel_from_key_code(u8 code) {
    if ((code & 0x03) == 0x03) {
        return -1;
    }
    return static_cast<int>((code & 0x03) + ((code & 0x04) != 0 ? 3 : 0));
}

} // namespace

struct Ym2612::Impl {
    ym3438_t chip{};
    bool enabled = false;
    std::array<u8, 2> selected_register{};
    std::array<u8, 0x200> registers{};
    std::array<bool, 6> key_on{};
    u64 clock_accumulator = 0;
    s16 output_left = 0;
    s16 output_right = 0;
};

Ym2612::Ym2612() : impl_(std::make_unique<Impl>()) {
    OPN2_SetChipType(ym3438_mode_ym2612);
    OPN2_Reset(&impl_->chip);
}

Ym2612::~Ym2612() = default;

void Ym2612::reset() {
    OPN2_Reset(&impl_->chip);
    impl_->selected_register.fill(0);
    impl_->registers.fill(0);
    impl_->key_on.fill(false);
    impl_->clock_accumulator = 0;
    impl_->output_left = 0;
    impl_->output_right = 0;
}

void Ym2612::set_enabled(bool enabled) {
    if (impl_->enabled == enabled) {
        return;
    }
    impl_->enabled = enabled;
    reset();
}

bool Ym2612::enabled() const {
    return impl_->enabled;
}

void Ym2612::write_address(int bank, u8 value) {
    if (!impl_->enabled) {
        return;
    }
    bank &= 1;
    impl_->selected_register[bank] = value;
    OPN2_Write(&impl_->chip, static_cast<Bit32u>(bank * 2), value);
}

void Ym2612::write_data(int bank, u8 value) {
    if (!impl_->enabled) {
        return;
    }
    bank &= 1;
    const u8 address = impl_->selected_register[bank];
    impl_->registers[register_index(bank, address)] = value;
    if (bank == 0 && address == 0x28) {
        const int channel = channel_from_key_code(static_cast<u8>(value & 0x07));
        if (channel >= 0) {
            impl_->key_on[channel] = (value & 0xF0) != 0;
        }
    }
    OPN2_Write(&impl_->chip, static_cast<Bit32u>(bank * 2 + 1), value);
}

u8 Ym2612::read_status(int bank) const {
    if (!impl_->enabled) {
        return 0xFF;
    }
    return OPN2_Read(&impl_->chip, static_cast<Bit32u>((bank & 1) * 2));
}

void Ym2612::tick(int cpu_cycles) {
    if (!impl_->enabled || cpu_cycles <= 0) {
        return;
    }
    impl_->clock_accumulator += static_cast<u64>(cpu_cycles) * kYm2612Clock;
    constexpr u64 threshold = kCpuClock * kMasterClocksPerInternalClock;
    while (impl_->clock_accumulator >= threshold) {
        impl_->clock_accumulator -= threshold;
        Bit16s output[2]{};
        OPN2_Clock(&impl_->chip, output);
        impl_->output_left = output[0];
        impl_->output_right = output[1];
    }
}

std::array<float, 2> Ym2612::sample() const {
    if (!impl_->enabled) {
        return {0.0F, 0.0F};
    }
    constexpr float scale = 1.0F / 256.0F;
    return {std::clamp(static_cast<float>(impl_->output_left) * scale, -1.0F, 1.0F),
            std::clamp(static_cast<float>(impl_->output_right) * scale, -1.0F, 1.0F)};
}

u8 Ym2612::selected_register(int bank) const {
    return impl_->selected_register[bank & 1];
}

const std::array<u8, 0x200>& Ym2612::debug_registers() const {
    return impl_->registers;
}

bool Ym2612::channel_key_on(int channel) const {
    return channel >= 0 && channel < 6 && impl_->key_on[channel];
}

Ym2612State Ym2612::save_state() const {
    Ym2612State state;
    state.enabled = impl_->enabled;
    state.selected_register = impl_->selected_register;
    state.registers = impl_->registers;
    state.key_on = impl_->key_on;
    state.clock_accumulator = impl_->clock_accumulator;
    state.output_left = impl_->output_left;
    state.output_right = impl_->output_right;
    state.core_state.resize(sizeof(impl_->chip));
    std::memcpy(state.core_state.data(), &impl_->chip, sizeof(impl_->chip));
    return state;
}

void Ym2612::load_state(const Ym2612State& state) {
    impl_->enabled = state.enabled;
    impl_->selected_register = state.selected_register;
    impl_->registers = state.registers;
    impl_->key_on = state.key_on;
    impl_->clock_accumulator = state.clock_accumulator;
    impl_->output_left = state.output_left;
    impl_->output_right = state.output_right;
    if (state.core_state.size() == sizeof(impl_->chip)) {
        std::memcpy(&impl_->chip, state.core_state.data(), sizeof(impl_->chip));
    } else {
        OPN2_Reset(&impl_->chip);
    }
}

} // namespace sgrecomp
