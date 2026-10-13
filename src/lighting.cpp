#include "openxidon/lighting.hpp"

#include <algorithm>
#include <array>
#include <stdexcept>
#include <string>

namespace openxidon {

namespace {

constexpr std::size_t kEffectOffset = 0x2D;
constexpr std::size_t kDirectionOffset = 0x2F;
constexpr std::size_t kBreathingCountOffset = 0x35;

constexpr std::array<EffectInfo, 12> kEffects = {{
    {Effect::Off, "off", "Off", 0, false, false, false, 0, 0, false},
    {Effect::RainbowWave, "rainbow-wave", "Rainbow Wave", 0x2E, true, true, true, 0, 0, false},
    {Effect::Static, "static", "Static", 0x30, false, true, false, 0x31, 1, false},
    {Effect::Breathing, "breathing", "Breathing", 0x34, true, true, false, 0x36, 7, true},
    {Effect::ColorfulTail, "colorful-tail", "Colorful Tail", 0x4B, true, true, false, 0, 0, false},
    {Effect::Neon, "neon", "Neon", 0x4C, true, true, false, 0, 0, false},
    {Effect::Rainbow, "rainbow", "Rainbow", 0x4D, false, true, false, 0x4E, 8, false},
    {Effect::Flicker, "flicker", "Flicker", 0x66, true, true, false, 0x67, 2, false},
    {Effect::Tail, "tail", "Tail", 0x6D, true, true, false, 0, 0, false},
    {Effect::Wave, "wave", "Wave", 0x6E, true, true, false, 0, 0, false},
    {Effect::Response, "response", "Response", 0x6F, true, false, false, 0, 0, false},
    {Effect::Streaming, "streaming", "Streaming", 0x70, true, false, false, 0, 0, false},
}};

}

int effectMenuNumber(Effect effect)
{
    if (effect == Effect::Off) {
        return static_cast<int>(kEffects.size());
    }
    return static_cast<int>(effect);
}

std::optional<Effect> effectFromNameOrNumber(std::string_view text)
{
    if (auto effect = effectFromName(text)) {
        return effect;
    }
    std::string lowered = toLower(text);
    for (const auto& info : kEffects) {
        if (toLower(info.displayName) == lowered) {
            return info.effect;
        }
    }
    if (!text.empty() && text.find_first_not_of("0123456789") == std::string_view::npos && text.size() <= 2) {
        int number = std::stoi(std::string(text));
        for (const auto& info : kEffects) {
            if (effectMenuNumber(info.effect) == number) {
                return info.effect;
            }
        }
    }
    return std::nullopt;
}

std::span<const EffectInfo> allEffects()
{
    return kEffects;
}

std::optional<EffectInfo> effectInfo(Effect effect)
{
    for (const auto& info : kEffects) {
        if (info.effect == effect) {
            return info;
        }
    }
    return std::nullopt;
}

std::optional<Effect> effectFromName(std::string_view name)
{
    std::string lowered = toLower(name);
    for (const auto& info : kEffects) {
        if (info.name == lowered) {
            return info.effect;
        }
    }
    return std::nullopt;
}

void encodeLighting(const Lighting& lighting, std::span<std::uint8_t> block)
{
    auto info = effectInfo(lighting.effect);
    if (!info) {
        throw std::invalid_argument("unknown lighting effect");
    }
    if (lighting.speed < 0 || lighting.speed > kMaxSpeed) {
        throw std::invalid_argument("speed must be between 0 and " + std::to_string(kMaxSpeed));
    }
    if (lighting.brightness < 0 || lighting.brightness > kMaxBrightness) {
        throw std::invalid_argument("brightness must be between 0 and " + std::to_string(kMaxBrightness));
    }
    if (info->maxColors == 0 && !lighting.colors.empty()) {
        throw std::invalid_argument(std::string(info->displayName) + " does not use colors");
    }
    if (lighting.colors.size() > info->maxColors) {
        throw std::invalid_argument(std::string(info->displayName) + " accepts at most "
            + std::to_string(info->maxColors) + " color(s)");
    }

    block[kEffectOffset] = static_cast<std::uint8_t>(lighting.effect);
    if (lighting.effect == Effect::Off) {
        return;
    }

    auto speed = static_cast<std::uint8_t>(lighting.speed & 0x0F);
    auto brightness = static_cast<std::uint8_t>((lighting.brightness & 0x0F) << 4);
    std::uint8_t& param = block[info->paramOffset];
    if (info->hasSpeed && info->hasBrightness) {
        param = brightness | speed;
    } else if (info->hasBrightness) {
        param = static_cast<std::uint8_t>((param & 0x0F) | brightness);
    } else if (info->hasSpeed) {
        param = static_cast<std::uint8_t>((param & 0xF0) | speed);
    }

    if (info->hasDirection) {
        block[kDirectionOffset] = lighting.direction ? 1 : 0;
    }

    if (info->maxColors > 0) {
        std::fill_n(block.begin() + info->colorOffset, info->maxColors * 3, std::uint8_t{0});
        for (std::size_t i = 0; i < lighting.colors.size(); ++i) {
            block[info->colorOffset + i * 3 + 0] = lighting.colors[i].r;
            block[info->colorOffset + i * 3 + 1] = lighting.colors[i].g;
            block[info->colorOffset + i * 3 + 2] = lighting.colors[i].b;
        }
        if (info->hasColorCount) {
            block[kBreathingCountOffset] = static_cast<std::uint8_t>(lighting.colors.size());
        }
    }
}

Lighting decodeLighting(std::span<const std::uint8_t> block)
{
    Lighting lighting;
    lighting.effect = static_cast<Effect>(block[kEffectOffset]);
    auto info = effectInfo(lighting.effect);
    if (!info || lighting.effect == Effect::Off) {
        return lighting;
    }

    std::uint8_t param = block[info->paramOffset];
    if (info->hasSpeed) {
        lighting.speed = param & 0x0F;
    }
    if (info->hasBrightness) {
        lighting.brightness = param >> 4;
    }
    if (info->hasDirection) {
        lighting.direction = block[kDirectionOffset] != 0;
    }

    if (info->maxColors > 0) {
        std::size_t count = info->maxColors;
        if (info->hasColorCount) {
            count = std::min<std::size_t>(block[kBreathingCountOffset], info->maxColors);
        }
        for (std::size_t i = 0; i < count; ++i) {
            lighting.colors.push_back({block[info->colorOffset + i * 3 + 0],
                block[info->colorOffset + i * 3 + 1], block[info->colorOffset + i * 3 + 2]});
        }
        if (!info->hasColorCount) {
            while (lighting.colors.size() > 1 && lighting.colors.back() == Rgb{}) {
                lighting.colors.pop_back();
            }
        }
    }
    return lighting;
}

}
