#include "openxidon/util.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <stdexcept>

namespace openxidon {

namespace {

int hexDigit(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    return -1;
}

}

Rgb parseRgb(std::string_view text)
{
    if (!text.empty() && text.front() == '#') {
        text.remove_prefix(1);
    }
    if (text.size() != 6) {
        throw std::invalid_argument("color must be RRGGBB, got '" + std::string(text) + "'");
    }
    std::uint8_t channels[3];
    for (int i = 0; i < 3; ++i) {
        int hi = hexDigit(text[i * 2]);
        int lo = hexDigit(text[i * 2 + 1]);
        if (hi < 0 || lo < 0) {
            throw std::invalid_argument("color must be RRGGBB, got '" + std::string(text) + "'");
        }
        channels[i] = static_cast<std::uint8_t>(hi * 16 + lo);
    }
    return {channels[0], channels[1], channels[2]};
}

namespace {

struct NamedColor {
    const char* name;
    Rgb color;
};

constexpr NamedColor kNamedColors[] = {
    {"red", {255, 0, 0}},
    {"green", {0, 255, 0}},
    {"blue", {0, 0, 255}},
    {"white", {255, 255, 255}},
    {"yellow", {255, 255, 0}},
    {"cyan", {0, 255, 255}},
    {"magenta", {255, 0, 255}},
    {"orange", {255, 128, 0}},
    {"purple", {128, 0, 255}},
    {"pink", {255, 64, 160}},
    {"black", {0, 0, 0}},
};

}

Rgb parseColor(std::string_view text)
{
    std::string lowered = toLower(text);
    for (const auto& named : kNamedColors) {
        if (lowered == named.name) {
            return named.color;
        }
    }
    try {
        return parseRgb(text);
    } catch (const std::invalid_argument&) {
        throw std::invalid_argument("unknown color '" + std::string(text) + "' (use RRGGBB or a name like red, blue, orange)");
    }
}

std::vector<std::string> colorNames()
{
    std::vector<std::string> names;
    for (const auto& named : kNamedColors) {
        names.emplace_back(named.name);
    }
    return names;
}

std::string formatRgb(const Rgb& color)
{
    char buffer[8];
    std::snprintf(buffer, sizeof buffer, "%02X%02X%02X", color.r, color.g, color.b);
    return buffer;
}

std::string toLower(std::string_view text)
{
    std::string result(text);
    std::transform(result.begin(), result.end(), result.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return result;
}

std::vector<std::string> split(std::string_view text, char separator)
{
    std::vector<std::string> parts;
    std::size_t start = 0;
    while (true) {
        std::size_t end = text.find(separator, start);
        parts.emplace_back(text.substr(start, end - start));
        if (end == std::string_view::npos) {
            break;
        }
        start = end + 1;
    }
    return parts;
}

long parseInteger(std::string_view text, long min, long max, std::string_view what)
{
    std::string owned(text);
    std::size_t consumed = 0;
    long value = 0;
    try {
        value = std::stol(owned, &consumed, 0);
    } catch (const std::exception&) {
        throw std::invalid_argument(std::string(what) + " must be a number, got '" + owned + "'");
    }
    if (consumed != owned.size()) {
        throw std::invalid_argument(std::string(what) + " must be a number, got '" + owned + "'");
    }
    if (value < min || value > max) {
        throw std::invalid_argument(std::string(what) + " must be between " + std::to_string(min)
            + " and " + std::to_string(max) + ", got " + owned);
    }
    return value;
}

std::string hexBytes(std::span<const std::uint8_t> bytes)
{
    std::string result;
    char buffer[4];
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        std::snprintf(buffer, sizeof buffer, i == 0 ? "%02X" : " %02X", bytes[i]);
        result += buffer;
    }
    return result;
}

std::string hexDump(std::span<const std::uint8_t> bytes, std::size_t baseOffset)
{
    std::string result;
    char buffer[16];
    for (std::size_t row = 0; row < bytes.size(); row += 16) {
        std::snprintf(buffer, sizeof buffer, "%04zX:", baseOffset + row);
        result += buffer;
        for (std::size_t i = row; i < std::min(row + 16, bytes.size()); ++i) {
            std::snprintf(buffer, sizeof buffer, " %02X", bytes[i]);
            result += buffer;
        }
        result += '\n';
    }
    return result;
}

}
