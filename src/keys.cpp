#include "openxidon/keys.hpp"

#include "openxidon/util.hpp"

#include <array>
#include <cstdio>
#include <utility>

namespace openxidon {

namespace {

struct NamedValue {
    std::string name;
    std::uint8_t value;
};

const std::vector<NamedValue>& keyTable()
{
    static const std::vector<NamedValue> table = [] {
        std::vector<NamedValue> keys;
        for (int i = 0; i < 26; ++i) {
            keys.push_back({std::string(1, static_cast<char>('a' + i)), static_cast<std::uint8_t>(0x04 + i)});
        }
        for (int i = 1; i <= 9; ++i) {
            keys.push_back({std::to_string(i), static_cast<std::uint8_t>(0x1E + i - 1)});
        }
        keys.push_back({"0", 0x27});
        for (int i = 1; i <= 12; ++i) {
            keys.push_back({"f" + std::to_string(i), static_cast<std::uint8_t>(0x3A + i - 1)});
        }
        for (int i = 1; i <= 9; ++i) {
            keys.push_back({"kp" + std::to_string(i), static_cast<std::uint8_t>(0x59 + i - 1)});
        }
        keys.push_back({"kp0", 0x62});
        const std::array<NamedValue, 39> named = {{
            {"enter", 0x28}, {"esc", 0x29}, {"backspace", 0x2A}, {"tab", 0x2B}, {"space", 0x2C},
            {"minus", 0x2D}, {"equal", 0x2E}, {"lbracket", 0x2F}, {"rbracket", 0x30},
            {"backslash", 0x31}, {"semicolon", 0x33}, {"apostrophe", 0x34}, {"grave", 0x35},
            {"comma", 0x36}, {"period", 0x37}, {"slash", 0x38}, {"capslock", 0x39},
            {"printscreen", 0x46}, {"scrolllock", 0x47}, {"pause", 0x48}, {"insert", 0x49},
            {"home", 0x4A}, {"pageup", 0x4B}, {"delete", 0x4C}, {"end", 0x4D},
            {"pagedown", 0x4E}, {"right", 0x4F}, {"left", 0x50}, {"down", 0x51}, {"up", 0x52},
            {"numlock", 0x53}, {"kpdivide", 0x54}, {"kpmultiply", 0x55}, {"kpminus", 0x56},
            {"kpplus", 0x57}, {"kpenter", 0x58}, {"kpperiod", 0x63}, {"menu", 0x65},
            {"return", 0x28},
        }};
        keys.insert(keys.end(), named.begin(), named.end());
        return keys;
    }();
    return table;
}

const std::vector<NamedValue>& modifierTable()
{
    static const std::vector<NamedValue> table = {
        {"lctrl", modifier::kLeftCtrl}, {"ctrl", modifier::kLeftCtrl},
        {"lshift", modifier::kLeftShift}, {"shift", modifier::kLeftShift},
        {"lalt", modifier::kLeftAlt}, {"alt", modifier::kLeftAlt},
        {"lwin", modifier::kLeftWin}, {"win", modifier::kLeftWin}, {"super", modifier::kLeftWin},
        {"meta", modifier::kLeftWin}, {"gui", modifier::kLeftWin},
        {"rctrl", modifier::kRightCtrl}, {"rshift", modifier::kRightShift},
        {"ralt", modifier::kRightAlt}, {"altgr", modifier::kRightAlt}, {"rwin", modifier::kRightWin},
    };
    return table;
}

const std::vector<NamedValue>& mouseButtonTable()
{
    static const std::vector<NamedValue> table = {
        {"left", mouse_button::kLeft}, {"right", mouse_button::kRight},
        {"middle", mouse_button::kMiddle}, {"back", mouse_button::kBack},
        {"forward", mouse_button::kForward},
    };
    return table;
}

std::optional<std::uint8_t> lookup(const std::vector<NamedValue>& table, std::string_view name)
{
    std::string lowered = toLower(name);
    for (const auto& entry : table) {
        if (entry.name == lowered) {
            return entry.value;
        }
    }
    return std::nullopt;
}

}

std::optional<std::uint8_t> keyUsageFromName(std::string_view name)
{
    return lookup(keyTable(), name);
}

std::string keyNameFromUsage(std::uint8_t usage)
{
    for (const auto& entry : keyTable()) {
        if (entry.value == usage) {
            return entry.name;
        }
    }
    char buffer[8];
    std::snprintf(buffer, sizeof buffer, "0x%02X", usage);
    return buffer;
}

std::vector<std::string> keyNames()
{
    std::vector<std::string> names;
    for (const auto& entry : keyTable()) {
        names.push_back(entry.name);
    }
    return names;
}

std::optional<std::uint8_t> modifierFromName(std::string_view name)
{
    return lookup(modifierTable(), name);
}

std::string modifierMaskToString(std::uint8_t mask)
{
    static const std::array<std::pair<std::uint8_t, const char*>, 8> names = {{
        {modifier::kLeftCtrl, "lctrl"}, {modifier::kLeftShift, "lshift"},
        {modifier::kLeftAlt, "lalt"}, {modifier::kLeftWin, "lwin"},
        {modifier::kRightCtrl, "rctrl"}, {modifier::kRightShift, "rshift"},
        {modifier::kRightAlt, "ralt"}, {modifier::kRightWin, "rwin"},
    }};
    std::string result;
    for (const auto& [bit, name] : names) {
        if (mask & bit) {
            if (!result.empty()) {
                result += '+';
            }
            result += name;
        }
    }
    return result;
}

std::optional<std::uint8_t> mouseButtonFromName(std::string_view name)
{
    return lookup(mouseButtonTable(), name);
}

std::string mouseButtonToString(std::uint8_t bit)
{
    for (const auto& entry : mouseButtonTable()) {
        if (entry.value == bit) {
            return entry.name;
        }
    }
    char buffer[8];
    std::snprintf(buffer, sizeof buffer, "0x%02X", bit);
    return buffer;
}

}
