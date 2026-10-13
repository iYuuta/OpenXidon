#pragma once

#include "openxidon/util.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace openxidon {

enum class Effect : std::uint8_t {
    Off = 0,
    RainbowWave = 1,
    Static = 2,
    Breathing = 3,
    ColorfulTail = 4,
    Neon = 5,
    Rainbow = 6,
    Flicker = 7,
    Tail = 8,
    Wave = 9,
    Response = 10,
    Streaming = 11,
};

struct EffectInfo {
    Effect effect;
    std::string_view name;
    std::string_view displayName;
    std::uint8_t paramOffset;
    bool hasSpeed;
    bool hasBrightness;
    bool hasDirection;
    std::uint8_t colorOffset;
    std::uint8_t maxColors;
    bool hasColorCount;
};

std::span<const EffectInfo> allEffects();
std::optional<EffectInfo> effectInfo(Effect effect);
std::optional<Effect> effectFromName(std::string_view name);
std::optional<Effect> effectFromNameOrNumber(std::string_view text);
int effectMenuNumber(Effect effect);

inline constexpr int kMaxSpeed = 15;
inline constexpr int kMaxBrightness = 15;

struct Lighting {
    Effect effect = Effect::Off;
    int speed = 0;
    int brightness = 0;
    bool direction = false;
    std::vector<Rgb> colors;
};

void encodeLighting(const Lighting& lighting, std::span<std::uint8_t> block);
Lighting decodeLighting(std::span<const std::uint8_t> block);

}
