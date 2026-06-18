#include "sgrecomp/input_script.h"

#include "sgrecomp/joypad.h"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <iterator>
#include <stdexcept>
#include <string>

namespace sgrecomp {
namespace {

std::string trim(std::string_view text) {
    const auto first = std::find_if_not(text.begin(), text.end(), [](unsigned char c) {
        return std::isspace(c) != 0;
    });
    const auto last = std::find_if_not(text.rbegin(), text.rend(), [](unsigned char c) {
        return std::isspace(c) != 0;
    }).base();
    return first < last ? std::string(first, last) : std::string{};
}

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::vector<std::string> split(std::string_view text, char delimiter) {
    std::vector<std::string> fields;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t end = text.find(delimiter, start);
        fields.push_back(trim(text.substr(start, end == std::string_view::npos ? text.size() - start : end - start)));
        if (end == std::string_view::npos) {
            break;
        }
        start = end + 1;
    }
    return fields;
}

std::size_t parse_frame(const std::string& text) {
    std::size_t value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size()) {
        throw std::runtime_error("invalid frame number: " + text);
    }
    return value;
}

u8 parse_buttons(std::string text) {
    text = lower(trim(text));
    if (text.empty() || text == "none" || text == "-") {
        return 0;
    }

    u8 mask = 0;
    for (std::string button : split(text, '+')) {
        button = lower(std::move(button));
        if (button == "up") mask |= Joypad::Up;
        else if (button == "down") mask |= Joypad::Down;
        else if (button == "left") mask |= Joypad::Left;
        else if (button == "right") mask |= Joypad::Right;
        else if (button == "button1" || button == "b1") mask |= Joypad::Button1;
        else if (button == "button2" || button == "b2") mask |= Joypad::Button2;
        else throw std::runtime_error("unknown input button: " + button);
    }
    return mask;
}

bool parse_pause(std::string text) {
    text = lower(trim(text));
    if (text == "1" || text == "true" || text == "on" || text == "pressed") return true;
    if (text == "0" || text == "false" || text == "off" || text == "released") return false;
    throw std::runtime_error("invalid pause state: " + text);
}

} // namespace

HostInputScript::HostInputScript(std::vector<HostInputEvent> events)
    : events_(std::move(events)) {
    if (!std::is_sorted(events_.begin(), events_.end(), [](const HostInputEvent& left, const HostInputEvent& right) {
            return left.frame < right.frame;
        })) {
        throw std::invalid_argument("input script events must be ordered by frame");
    }
}

HostInputState HostInputScript::state_for_frame(std::size_t frame) const {
    const auto it = std::upper_bound(events_.begin(), events_.end(), frame,
        [](std::size_t target, const HostInputEvent& event) { return target < event.frame; });
    return it == events_.begin() ? HostInputState{} : std::prev(it)->input;
}

HostInputScript parse_host_input_script(std::string_view text) {
    std::vector<HostInputEvent> events;
    std::size_t line_number = 0;
    std::size_t start = 0;
    while (start <= text.size()) {
        ++line_number;
        const std::size_t end = text.find('\n', start);
        std::string line = trim(text.substr(start, end == std::string_view::npos ? text.size() - start : end - start));
        if (!line.empty() && line.front() != '#') {
            const auto fields = split(line, ',');
            if (lower(fields.front()) != "frame") {
                if (fields.size() != 4) {
                    throw std::runtime_error("input script line " + std::to_string(line_number) + " must have 4 CSV fields");
                }
                try {
                    HostInputEvent event;
                    event.frame = parse_frame(fields[0]);
                    event.input.player1 = parse_buttons(fields[1]);
                    event.input.player2 = parse_buttons(fields[2]);
                    event.input.pause = parse_pause(fields[3]);
                    if (!events.empty() && event.frame <= events.back().frame) {
                        throw std::runtime_error("frame numbers must be strictly increasing");
                    }
                    events.push_back(event);
                } catch (const std::exception& error) {
                    throw std::runtime_error("input script line " + std::to_string(line_number) + ": " + error.what());
                }
            } else if (fields.size() != 4 || lower(fields[1]) != "player1"
                || lower(fields[2]) != "player2" || lower(fields[3]) != "pause") {
                throw std::runtime_error("input script line " + std::to_string(line_number)
                    + ": expected header frame,player1,player2,pause");
            }
        }
        if (end == std::string_view::npos) {
            break;
        }
        start = end + 1;
    }
    return HostInputScript(std::move(events));
}

} // namespace sgrecomp
