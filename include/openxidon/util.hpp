#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace openxidon {

struct Rgb {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;

    bool operator==(const Rgb&) const = default;
};

Rgb parseRgb(std::string_view text);
Rgb parseColor(std::string_view text);
std::vector<std::string> colorNames();
std::string formatRgb(const Rgb& color);

std::string toLower(std::string_view text);
std::vector<std::string> split(std::string_view text, char separator);
long parseInteger(std::string_view text, long min, long max, std::string_view what);

std::string hexBytes(std::span<const std::uint8_t> bytes);
std::string hexDump(std::span<const std::uint8_t> bytes, std::size_t baseOffset = 0);

}
