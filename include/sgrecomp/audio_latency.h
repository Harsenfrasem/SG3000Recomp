#pragma once

#include "sgrecomp/types.h"

#include <cstddef>

namespace sgrecomp {

class AudioLatencyController {
  public:
    void configure(u32 sample_rate, int requested_latency_ms, int maximum_latency_ms = 300);
    bool observe_submission(std::size_t queued_sample_frames, std::size_t incoming_sample_frames);
    void reset_stream();

    int requested_latency_ms() const {
        return requested_latency_ms_;
    }
    int effective_latency_ms() const {
        return effective_latency_ms_;
    }
    std::size_t target_sample_frames() const;
    bool primed() const {
        return primed_;
    }

  private:
    u32 sample_rate_ = 44100;
    int requested_latency_ms_ = 80;
    int effective_latency_ms_ = 80;
    int maximum_latency_ms_ = 180;
    std::size_t stable_submissions_ = 0;
    bool primed_ = false;
};

} // namespace sgrecomp
