#include "sgrecomp/ym2413.h"

#include "emu2413.h"

#include <cstring>
#include <new>

namespace sgrecomp {
namespace {

constexpr u32 kOpllClock = 3579545;
constexpr u32 kOpllInternalRate = 49716;
constexpr u64 kCpuCyclesPerOpllSample = 72;

} // namespace

Ym2413::Ym2413() : core_(OPLL_new(kOpllClock, kOpllInternalRate)) {
    if (core_ == nullptr) {
        throw std::bad_alloc();
    }
    OPLL_setChipType(core_, 0);
    OPLL_resetPatch(core_, OPLL_2413_TONE);
}

Ym2413::~Ym2413() {
    OPLL_delete(core_);
}

void Ym2413::reset() {
    selected_register_ = 0;
    audio_control_ = 0;
    registers_.fill(0);
    clock_accumulator_ = 0;
    output_ = 0;
    logged_writes_.clear();
    OPLL_reset(core_);
    OPLL_setChipType(core_, 0);
    OPLL_resetPatch(core_, OPLL_2413_TONE);
}

void Ym2413::set_present(bool present) {
    present_ = present;
    if (!present_) {
        audio_control_ = 0;
    }
}

void Ym2413::set_write_logging_enabled(bool enabled) {
    write_logging_enabled_ = enabled;
    if (!enabled) {
        logged_writes_.clear();
    }
}

void Ym2413::write_address(u8 value) {
    if (!present_) {
        return;
    }
    selected_register_ = static_cast<u8>(value & 0x3F);
    log_write(0xF0, value);
}

void Ym2413::write_data(u8 value) {
    if (!present_) {
        return;
    }
    registers_[selected_register_ & 0x3F] = value;
    OPLL_writeReg(core_, selected_register_ & 0x3F, value);
    log_write(0xF1, value);
}

void Ym2413::write_audio_control(u8 value) {
    if (!present_) {
        return;
    }
    audio_control_ = static_cast<u8>(value & 0x03);
    log_write(0xF2, audio_control_);
}

u8 Ym2413::read_audio_control() const {
    if (!present_) {
        return 0x02;
    }
    return audio_control_;
}

void Ym2413::tick(int cpu_cycles) {
    if (!present_ || cpu_cycles <= 0) {
        return;
    }

    clock_accumulator_ += static_cast<u64>(cpu_cycles);
    while (clock_accumulator_ >= kCpuCyclesPerOpllSample) {
        clock_accumulator_ -= kCpuCyclesPerOpllSample;
        output_ = OPLL_calc(core_);
    }
}

std::array<float, 2> Ym2413::sample() const {
    if (!fm_enabled()) {
        return {0.0F, 0.0F};
    }
    const float sample = static_cast<float>(output_) / 32768.0F;
    return {sample, sample};
}

void Ym2413::log_write(u8 port, u8 value) {
    if (write_logging_enabled_) {
        logged_writes_.push_back({current_cycle_, port, value});
    }
}

std::vector<u8> Ym2413::serialize_core() const {
    OPLL snapshot = *core_;
    snapshot.conv = nullptr;
    for (auto& slot : snapshot.slot) {
        slot.patch = nullptr;
        slot.wave_table = nullptr;
    }

    std::vector<u8> bytes(sizeof(snapshot));
    std::memcpy(bytes.data(), &snapshot, sizeof(snapshot));
    return bytes;
}

bool Ym2413::restore_core(std::span<const u8> bytes) {
    if (bytes.size() != sizeof(OPLL)) {
        return false;
    }

    OPLL snapshot{};
    std::memcpy(&snapshot, bytes.data(), sizeof(snapshot));
    if (snapshot.clk != kOpllClock || snapshot.rate != kOpllInternalRate || snapshot.chip_type != 0) {
        return false;
    }
    for (const int patch_number : snapshot.patch_number) {
        if (patch_number < 0 || patch_number >= 19) {
            return false;
        }
    }

    std::memcpy(core_, &snapshot, sizeof(snapshot));
    core_->conv = nullptr;
    for (auto& slot : core_->slot) {
        slot.patch = nullptr;
        slot.wave_table = nullptr;
    }
    OPLL_forceRefresh(core_);
    return true;
}

void Ym2413::replay_registers() {
    OPLL_reset(core_);
    OPLL_setChipType(core_, 0);
    OPLL_resetPatch(core_, OPLL_2413_TONE);
    for (u32 reg = 0; reg < registers_.size(); ++reg) {
        OPLL_writeReg(core_, reg, registers_[reg]);
    }
}

Ym2413State Ym2413::save_state() const {
    return {
        present_,
        selected_register_,
        audio_control_,
        registers_,
        {},
        clock_accumulator_,
        output_,
        serialize_core(),
    };
}

void Ym2413::load_state(const Ym2413State& state) {
    present_ = state.present;
    selected_register_ = static_cast<u8>(state.selected_register & 0x3F);
    audio_control_ = static_cast<u8>(state.audio_control & 0x03);
    registers_ = state.registers;
    clock_accumulator_ = state.clock_accumulator % kCpuCyclesPerOpllSample;
    output_ = state.output;
    if (!restore_core(state.core_state)) {
        replay_registers();
        clock_accumulator_ = 0;
        output_ = 0;
    }
}

} // namespace sgrecomp
