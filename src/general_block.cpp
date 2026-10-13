#include "openxidon/general_block.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>

namespace openxidon {

namespace {

constexpr std::size_t kSensorOffset = 0x01;
constexpr std::size_t kFlagsOffset = 0x02;
constexpr std::size_t kLevelOffset = 0x03;
constexpr std::size_t kDisabledMaskOffset = 0x04;
constexpr std::size_t kDpiValuesOffset = 0x05;
constexpr std::size_t kDpiValuesSize = 0x10;
constexpr std::size_t kDpiColorsOffset = 0x15;

constexpr std::uint8_t kPollingMask = 0x0F;
constexpr std::uint8_t kXyIndependentBit = 0x80;
constexpr std::uint8_t kSensorMask = 0x3F;

constexpr std::array<int, 4> kPollingRates = {125, 250, 500, 1000};

std::uint8_t requireDpiCode(int dpi)
{
    auto code = codeFromDpi(dpi);
    if (!code) {
        throw std::invalid_argument("unsupported DPI " + std::to_string(dpi)
            + " (valid: 200-5000 in steps of 100, 5500-10000 in steps of 500)");
    }
    return *code;
}

}

GeneralBlock::GeneralBlock(const Bytes& bytes)
    : bytes_(bytes)
{
}

GeneralBlock GeneralBlock::placeholder()
{
    GeneralBlock block;
    block.bytes_[0] = kGeneralMarker;
    block.bytes_[kSensorOffset] = kSensorPmw3325;
    block.setPollingRateHz(500);

    DpiSettings dpi;
    const std::array<int, kDpiSlotCount> values = {800, 1600, 2400, 3200, 4000, 5000, 5000, 5000};
    const std::array<Rgb, kDpiSlotCount> colors = {{
        {255, 0, 0}, {0, 255, 255}, {0, 0, 255}, {255, 255, 255},
        {255, 0, 255}, {0, 255, 0}, {0, 0, 0}, {0, 0, 0},
    }};
    for (std::size_t i = 0; i < kDpiSlotCount; ++i) {
        dpi.slots[i] = {values[i], values[i], i < 6, colors[i]};
    }
    dpi.currentSlot = 1;
    block.setDpi(dpi);
    return block;
}

bool GeneralBlock::isEmpty() const
{
    return std::all_of(bytes_.begin(), bytes_.begin() + kGeneralWriteSize, [](std::uint8_t b) { return b == 0; });
}

std::uint8_t GeneralBlock::sensorCode() const
{
    return bytes_[kSensorOffset] & kSensorMask;
}

int GeneralBlock::pollingRateHz() const
{
    int code = bytes_[kFlagsOffset] & kPollingMask;
    if (code < 1 || code > static_cast<int>(kPollingRates.size())) {
        return 0;
    }
    return kPollingRates[code - 1];
}

void GeneralBlock::setPollingRateHz(int hz)
{
    auto it = std::find(kPollingRates.begin(), kPollingRates.end(), hz);
    if (it == kPollingRates.end()) {
        throw std::invalid_argument("polling rate must be 125, 250, 500 or 1000 Hz");
    }
    auto code = static_cast<std::uint8_t>(std::distance(kPollingRates.begin(), it) + 1);
    bytes_[kFlagsOffset] = static_cast<std::uint8_t>((bytes_[kFlagsOffset] & ~kPollingMask) | code);
}

DpiSettings GeneralBlock::dpi() const
{
    DpiSettings settings;
    settings.xyIndependent = (bytes_[kFlagsOffset] & kXyIndependentBit) != 0;

    std::size_t offset = kDpiValuesOffset;
    for (std::size_t i = 0; i < kDpiSlotCount; ++i) {
        DpiSlot& slot = settings.slots[i];
        if (settings.xyIndependent) {
            slot.x = dpiFromCode(bytes_[offset]);
            slot.y = dpiFromCode(bytes_[offset + 1]);
            offset += 2;
        } else {
            slot.x = dpiFromCode(bytes_[offset]);
            slot.y = slot.x;
            offset += 1;
        }
        slot.enabled = ((bytes_[kDisabledMaskOffset] >> i) & 1) == 0;
        std::size_t colorOffset = kDpiColorsOffset + i * 3;
        slot.color = {bytes_[colorOffset], bytes_[colorOffset + 1], bytes_[colorOffset + 2]};
    }

    int ordinal = bytes_[kLevelOffset] >> 4;
    settings.currentSlot = -1;
    int seen = 0;
    for (std::size_t i = 0; i < kDpiSlotCount; ++i) {
        if (settings.slots[i].enabled && ++seen == ordinal) {
            settings.currentSlot = static_cast<int>(i);
            break;
        }
    }
    return settings;
}

void GeneralBlock::setDpi(const DpiSettings& settings)
{
    if (settings.enabledCount() == 0) {
        throw std::invalid_argument("at least one DPI level must be enabled");
    }
    if (settings.currentSlot < 0 || settings.currentSlot >= static_cast<int>(kDpiSlotCount)
        || !settings.slots[settings.currentSlot].enabled) {
        throw std::invalid_argument("the current DPI level must be an enabled slot");
    }

    std::fill_n(bytes_.begin() + kDpiValuesOffset, kDpiValuesSize, std::uint8_t{0});
    std::size_t offset = kDpiValuesOffset;
    std::uint8_t disabledMask = 0;
    int ordinal = 0;
    for (std::size_t i = 0; i < kDpiSlotCount; ++i) {
        const DpiSlot& slot = settings.slots[i];
        auto encode = [&slot](int dpi) -> std::uint8_t {
            if (!slot.enabled && !codeFromDpi(dpi)) {
                return 0;
            }
            return requireDpiCode(dpi);
        };
        bytes_[offset++] = encode(slot.x);
        if (settings.xyIndependent) {
            bytes_[offset++] = encode(slot.y);
        }
        if (!slot.enabled) {
            disabledMask |= static_cast<std::uint8_t>(1u << i);
        } else if (static_cast<int>(i) <= settings.currentSlot) {
            ++ordinal;
        }
        std::size_t colorOffset = kDpiColorsOffset + i * 3;
        Rgb color = slot.color == Rgb{1, 1, 1} ? Rgb{} : slot.color;
        bytes_[colorOffset] = color.r;
        bytes_[colorOffset + 1] = color.g;
        bytes_[colorOffset + 2] = color.b;
    }

    bytes_[kDisabledMaskOffset] = disabledMask;
    bytes_[kLevelOffset] = static_cast<std::uint8_t>((settings.enabledCount() & 0x0F) | (ordinal << 4));
    if (settings.xyIndependent) {
        bytes_[kFlagsOffset] |= kXyIndependentBit;
    } else {
        bytes_[kFlagsOffset] &= static_cast<std::uint8_t>(~kXyIndependentBit);
    }
}

Lighting GeneralBlock::lighting() const
{
    return decodeLighting(bytes_);
}

void GeneralBlock::setLighting(const Lighting& lighting)
{
    encodeLighting(lighting, bytes_);
}

}
