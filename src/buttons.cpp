#include "openxidon/buttons.hpp"

#include "openxidon/dpi.hpp"
#include "openxidon/keys.hpp"
#include "openxidon/util.hpp"

#include <cstdio>
#include <stdexcept>

namespace openxidon {

namespace {

struct NamedCode {
    std::string_view name;
    std::uint32_t code;
};

constexpr NamedCode kSimpleFunctions[] = {
    {"left", 0x00000111},
    {"right", 0x00000211},
    {"middle", 0x00000411},
    {"back", 0x00000811},
    {"forward", 0x00001011},
    {"double-click", 0x02320131},
    {"triple-click", 0x03320131},
    {"scroll-up", 0x00000112},
    {"scroll-down", 0x0000FF12},
    {"scroll-left", 0x0000FF13},
    {"scroll-right", 0x00000113},
    {"disable", 0x00000150},
    {"light-toggle", 0x00000250},
    {"dpi-up", 0x00000141},
    {"dpi-down", 0x00000241},
    {"dpi-loop", 0x00000041},
    {"dpi-up-min", 0x00000143},
    {"dpi-down-min", 0x00000243},
    {"mode-next", 0x00000650},
    {"rate-next", 0x00000450},
    {"effect-next", 0x00000750},
    {"sniper", 0x00000950},
};

struct MediaBit {
    std::string_view name;
    int byteIndex;
    std::uint8_t bit;
};

constexpr MediaBit kMediaFunctions[] = {
    {"next", 1, 0x01},
    {"prev", 1, 0x02},
    {"stop", 1, 0x04},
    {"play-pause", 1, 0x08},
    {"mute", 1, 0x10},
    {"volume-up", 1, 0x40},
    {"volume-down", 1, 0x80},
    {"media-player", 2, 0x01},
    {"my-computer", 2, 0x02},
    {"email", 2, 0x10},
    {"calculator", 2, 0x20},
    {"search", 3, 0x01},
    {"home", 3, 0x02},
    {"browser-back", 3, 0x04},
    {"browser-forward", 3, 0x08},
    {"browser-stop", 3, 0x10},
    {"refresh", 3, 0x20},
    {"favorites", 3, 0x40},
};

constexpr std::uint8_t kTypeCombo = 0x21;
constexpr std::uint8_t kTypeMedia = 0x22;
constexpr std::uint8_t kTypeFireMouse = 0x31;
constexpr std::uint8_t kTypeFireKey = 0x32;
constexpr std::uint8_t kTypeFireModifier = 0x33;
constexpr std::uint8_t kTypeDpiLock = 0x42;
constexpr std::uint8_t kTypeMacro = 0x70;

constexpr std::uint32_t pack(std::uint8_t b0, std::uint8_t b1, std::uint8_t b2, std::uint8_t b3)
{
    return static_cast<std::uint32_t>(b0) | (static_cast<std::uint32_t>(b1) << 8)
        | (static_cast<std::uint32_t>(b2) << 16) | (static_cast<std::uint32_t>(b3) << 24);
}

std::uint8_t byteAt(std::uint32_t code, int index)
{
    return static_cast<std::uint8_t>((code >> (index * 8)) & 0xFF);
}

std::uint8_t requireKey(std::string_view name)
{
    auto usage = keyUsageFromName(name);
    if (!usage) {
        throw std::invalid_argument("unknown key '" + std::string(name) + "'");
    }
    return *usage;
}

std::uint32_t parseCombo(std::string_view argument)
{
    std::uint8_t mask = 0;
    std::vector<std::uint8_t> keys;
    for (const auto& token : split(argument, '+')) {
        if (token.empty()) {
            throw std::invalid_argument("empty token in combo '" + std::string(argument) + "'");
        }
        if (auto bit = modifierFromName(token)) {
            mask |= *bit;
        } else {
            keys.push_back(requireKey(token));
        }
    }
    if (keys.empty() || keys.size() > 2) {
        throw std::invalid_argument("combo needs one or two non-modifier keys");
    }
    return pack(kTypeCombo, mask, keys[0], keys.size() > 1 ? keys[1] : 0);
}

std::uint32_t parseMedia(std::string_view argument)
{
    std::string lowered = toLower(argument);
    for (const auto& media : kMediaFunctions) {
        if (media.name == lowered) {
            std::uint8_t bytes[4] = {kTypeMedia, 0, 0, 0};
            bytes[media.byteIndex] = media.bit;
            return pack(bytes[0], bytes[1], bytes[2], bytes[3]);
        }
    }
    throw std::invalid_argument("unknown media function '" + std::string(argument) + "'");
}

std::uint32_t parseMacro(const std::vector<std::string>& parts)
{
    if (parts.size() < 2 || parts.size() > 4) {
        throw std::invalid_argument("macro syntax is macro:<slot>[:<count>[:<mode>]]");
    }
    auto slot = static_cast<std::uint8_t>(parseInteger(parts[1], 1, kMacroMaxSlot, "macro slot"));
    auto count = static_cast<std::uint8_t>(parts.size() > 2 ? parseInteger(parts[2], 1, 255, "macro count") : 1);
    auto mode = static_cast<std::uint8_t>(parts.size() > 3 ? parseInteger(parts[3], 1, 4, "macro mode") : 1);
    if (mode == 3) {
        throw std::invalid_argument("macro mode must be 1, 2 or 4");
    }
    return pack(kTypeMacro, slot, mode, count);
}

std::uint32_t parseFire(const std::vector<std::string>& parts)
{
    if (parts.size() != 4) {
        throw std::invalid_argument("fire syntax is fire:<target>:<loop>:<interval>");
    }
    auto loop = static_cast<std::uint8_t>(parseInteger(parts[2], 1, 255, "fire loop"));
    auto interval = static_cast<std::uint8_t>(parseInteger(parts[3], 5, 255, "fire interval"));
    const std::string& target = parts[1];
    if (auto button = mouseButtonFromName(target)) {
        return pack(kTypeFireMouse, *button, loop, interval);
    }
    if (auto bit = modifierFromName(target)) {
        return pack(kTypeFireModifier, *bit, loop, interval);
    }
    return pack(kTypeFireKey, requireKey(target), loop, interval);
}

std::uint32_t parseDpiLock(const std::vector<std::string>& parts)
{
    if (parts.size() != 2) {
        throw std::invalid_argument("dpi-lock syntax is dpi-lock:<dpi>");
    }
    int dpi = static_cast<int>(parseInteger(parts[1], kMinDpi, kMaxDpi, "dpi-lock DPI"));
    auto code = codeFromDpi(dpi);
    if (!code) {
        throw std::invalid_argument("unsupported DPI " + parts[1]);
    }
    return pack(kTypeDpiLock, *code, 0, 0);
}

std::string hexCode(std::uint32_t code)
{
    char buffer[16];
    std::snprintf(buffer, sizeof buffer, "raw:0x%08X", code);
    return buffer;
}

}

ButtonMatrix ButtonMatrix::defaults()
{
    ButtonMatrix matrix;
    matrix.slots.fill(parseButtonFunction("disable"));
    matrix.slots[0] = parseButtonFunction("left");
    matrix.slots[1] = parseButtonFunction("right");
    matrix.slots[2] = parseButtonFunction("middle");
    matrix.slots[3] = parseButtonFunction("back");
    matrix.slots[4] = parseButtonFunction("forward");
    matrix.slots[5] = parseButtonFunction("dpi-up");
    matrix.slots[6] = parseButtonFunction("dpi-down");
    matrix.slots[7] = parseButtonFunction("rate-next");
    matrix.slots[8] = parseButtonFunction("rate-next");
    return matrix;
}

std::array<std::uint8_t, kButtonSlotCount * 4> ButtonMatrix::encode() const
{
    std::array<std::uint8_t, kButtonSlotCount * 4> bytes{};
    for (std::size_t i = 0; i < kButtonSlotCount; ++i) {
        for (int b = 0; b < 4; ++b) {
            bytes[i * 4 + b] = byteAt(slots[i], b);
        }
    }
    return bytes;
}

std::uint32_t parseButtonFunction(std::string_view text)
{
    std::string lowered = toLower(text);
    for (const auto& function : kSimpleFunctions) {
        if (function.name == lowered) {
            return function.code;
        }
    }

    auto parts = split(lowered, ':');
    const std::string& kind = parts[0];
    if (kind == "key") {
        if (parts.size() != 2) {
            throw std::invalid_argument("key syntax is key:<name>");
        }
        return pack(kTypeCombo, 0, requireKey(parts[1]), 0);
    }
    if (kind == "combo") {
        if (parts.size() != 2) {
            throw std::invalid_argument("combo syntax is combo:<mod>+...+<key>[+<key>]");
        }
        return parseCombo(parts[1]);
    }
    if (kind == "media") {
        if (parts.size() != 2) {
            throw std::invalid_argument("media syntax is media:<name>");
        }
        return parseMedia(parts[1]);
    }
    if (kind == "macro") {
        return parseMacro(parts);
    }
    if (kind == "fire") {
        return parseFire(parts);
    }
    if (kind == "dpi-lock") {
        return parseDpiLock(parts);
    }
    if (kind == "raw") {
        if (parts.size() != 2) {
            throw std::invalid_argument("raw syntax is raw:<hex>");
        }
        return static_cast<std::uint32_t>(parseInteger(parts[1], 0, 0xFFFFFFFFL, "raw code"));
    }
    throw std::invalid_argument("unknown button function '" + std::string(text) + "'");
}

std::string describeButtonCode(std::uint32_t code)
{
    for (const auto& function : kSimpleFunctions) {
        if (function.code == code) {
            return std::string(function.name);
        }
    }

    std::uint8_t type = byteAt(code, 0);
    std::uint8_t b1 = byteAt(code, 1);
    std::uint8_t b2 = byteAt(code, 2);
    std::uint8_t b3 = byteAt(code, 3);

    switch (type) {
    case kTypeCombo: {
        if (b1 == 0 && b3 == 0) {
            return "key:" + keyNameFromUsage(b2);
        }
        std::string result = "combo:";
        std::string mods = modifierMaskToString(b1);
        if (!mods.empty()) {
            result += mods + "+";
        }
        result += keyNameFromUsage(b2);
        if (b3 != 0) {
            result += "+" + keyNameFromUsage(b3);
        }
        return result;
    }
    case kTypeMedia:
        for (const auto& media : kMediaFunctions) {
            if (byteAt(code, media.byteIndex) == media.bit
                && (code & ~(0xFFu << (media.byteIndex * 8))) == kTypeMedia) {
                return "media:" + std::string(media.name);
            }
        }
        break;
    case kTypeMacro:
        return "macro:" + std::to_string(b1) + ":" + std::to_string(b3) + ":" + std::to_string(b2);
    case kTypeDpiLock:
        if (b2 == 0 && b3 == 0 && dpiFromCode(b1) != 0) {
            return "dpi-lock:" + std::to_string(dpiFromCode(b1));
        }
        break;
    case kTypeFireMouse:
        return "fire:" + mouseButtonToString(b1) + ":" + std::to_string(b2) + ":" + std::to_string(b3);
    case kTypeFireKey:
        return "fire:" + keyNameFromUsage(b1) + ":" + std::to_string(b2) + ":" + std::to_string(b3);
    case kTypeFireModifier:
        return "fire:" + modifierMaskToString(b1) + ":" + std::to_string(b2) + ":" + std::to_string(b3);
    default:
        break;
    }
    return hexCode(code);
}

std::vector<std::string> simpleButtonFunctionNames()
{
    std::vector<std::string> names;
    for (const auto& function : kSimpleFunctions) {
        names.emplace_back(function.name);
    }
    return names;
}

std::vector<std::string> mediaFunctionNames()
{
    std::vector<std::string> names;
    for (const auto& media : kMediaFunctions) {
        names.emplace_back(media.name);
    }
    return names;
}

}
