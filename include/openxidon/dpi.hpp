#pragma once

#include "openxidon/protocol.hpp"
#include "openxidon/util.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace openxidon {

inline constexpr int kMinDpi = 200;
inline constexpr int kMaxDpi = 10000;

int dpiFromCode(std::uint8_t code);
std::optional<std::uint8_t> codeFromDpi(int dpi);
std::vector<int> supportedDpiValues();

struct DpiSlot {
    int x = 800;
    int y = 800;
    bool enabled = true;
    Rgb color{};
};

struct DpiSettings {
    bool xyIndependent = false;
    std::array<DpiSlot, kDpiSlotCount> slots{};
    int currentSlot = 0;

    int enabledCount() const;
};

}
