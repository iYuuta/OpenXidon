#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace openxidon {

namespace modifier {
inline constexpr std::uint8_t kLeftCtrl = 0x01;
inline constexpr std::uint8_t kLeftShift = 0x02;
inline constexpr std::uint8_t kLeftAlt = 0x04;
inline constexpr std::uint8_t kLeftWin = 0x08;
inline constexpr std::uint8_t kRightCtrl = 0x10;
inline constexpr std::uint8_t kRightShift = 0x20;
inline constexpr std::uint8_t kRightAlt = 0x40;
inline constexpr std::uint8_t kRightWin = 0x80;
}

namespace mouse_button {
inline constexpr std::uint8_t kLeft = 0x01;
inline constexpr std::uint8_t kRight = 0x02;
inline constexpr std::uint8_t kMiddle = 0x04;
inline constexpr std::uint8_t kBack = 0x08;
inline constexpr std::uint8_t kForward = 0x10;
}

std::optional<std::uint8_t> keyUsageFromName(std::string_view name);
std::string keyNameFromUsage(std::uint8_t usage);
std::vector<std::string> keyNames();

std::optional<std::uint8_t> modifierFromName(std::string_view name);
std::string modifierMaskToString(std::uint8_t mask);

std::optional<std::uint8_t> mouseButtonFromName(std::string_view name);
std::string mouseButtonToString(std::uint8_t bit);

}
