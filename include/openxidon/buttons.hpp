#pragma once

#include "openxidon/protocol.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace openxidon {

struct ButtonMatrix {
    std::array<std::uint32_t, kButtonSlotCount> slots{};

    static ButtonMatrix defaults();
    std::array<std::uint8_t, kButtonSlotCount * 4> encode() const;
};

std::uint32_t parseButtonFunction(std::string_view text);
std::string describeButtonCode(std::uint32_t code);

std::vector<std::string> simpleButtonFunctionNames();
std::vector<std::string> mediaFunctionNames();

}
