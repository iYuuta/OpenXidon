#pragma once

#include <cstdint>
#include <istream>
#include <string>
#include <vector>

namespace openxidon {

enum class MacroEventType : std::uint8_t {
    MouseButton = 1,
    MoveX = 2,
    MoveY = 3,
    Wheel = 4,
    Key = 5,
    Modifier = 6,
};

inline constexpr int kMacroMaxDelayMs = 0x0FFF;

struct MacroEvent {
    MacroEventType type = MacroEventType::Key;
    bool press = true;
    std::uint8_t code = 0;
    int delayMs = 10;
};

struct Macro {
    std::vector<MacroEvent> events;

    std::vector<std::uint8_t> encode() const;
    std::string describe() const;

    static Macro parse(std::istream& input);
};

}
