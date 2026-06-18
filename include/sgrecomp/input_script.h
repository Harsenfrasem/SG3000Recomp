#pragma once

#include "sgrecomp/host_runtime.h"

#include <cstddef>
#include <string_view>
#include <vector>

namespace sgrecomp {

struct HostInputEvent {
    std::size_t frame = 0;
    HostInputState input;
};

class HostInputScript {
public:
    explicit HostInputScript(std::vector<HostInputEvent> events = {});

    const std::vector<HostInputEvent>& events() const { return events_; }
    HostInputState state_for_frame(std::size_t frame) const;

private:
    std::vector<HostInputEvent> events_;
};

HostInputScript parse_host_input_script(std::string_view text);

} // namespace sgrecomp
