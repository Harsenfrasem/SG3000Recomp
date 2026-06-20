#include "sgrecomp/audio_latency.h"

#include <algorithm>

namespace sgrecomp {

void AudioLatencyController::configure(u32 sample_rate, int requested_latency_ms, int maximum_latency_ms) {
    sample_rate_ = std::max<u32>(1, sample_rate);
    requested_latency_ms_ = std::clamp(requested_latency_ms, 10, 300);
    maximum_latency_ms_ = std::clamp(maximum_latency_ms, requested_latency_ms_, 300);
    effective_latency_ms_ = requested_latency_ms_;
    stable_submissions_ = 0;
    primed_ = false;
}

bool AudioLatencyController::observe_submission(std::size_t queued_sample_frames, std::size_t incoming_sample_frames) {
    const bool underrun = primed_ && queued_sample_frames < incoming_sample_frames;
    if (underrun) {
        effective_latency_ms_ = std::min(maximum_latency_ms_, effective_latency_ms_ + 10);
        stable_submissions_ = 0;
    } else if (primed_) {
        ++stable_submissions_;
        if (stable_submissions_ >= 300 && effective_latency_ms_ > requested_latency_ms_) {
            effective_latency_ms_ = std::max(requested_latency_ms_, effective_latency_ms_ - 5);
            stable_submissions_ = 0;
        }
    }
    primed_ = true;
    return underrun;
}

void AudioLatencyController::reset_stream() {
    effective_latency_ms_ = requested_latency_ms_;
    stable_submissions_ = 0;
    primed_ = false;
}

std::size_t AudioLatencyController::target_sample_frames() const {
    return std::max<std::size_t>(
        1, static_cast<std::size_t>((static_cast<u64>(sample_rate_) * effective_latency_ms_) / 1000));
}

} // namespace sgrecomp
