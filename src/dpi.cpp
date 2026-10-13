#include "openxidon/dpi.hpp"

namespace openxidon {

namespace {

constexpr std::uint8_t kFineCodeCount = 49;
constexpr std::uint8_t kMaxCode = 59;

}

int dpiFromCode(std::uint8_t code)
{
    if (code >= 1 && code <= kFineCodeCount) {
        return 100 * (code + 1);
    }
    if (code > kFineCodeCount && code <= kMaxCode) {
        return 5000 + 500 * (code - kFineCodeCount);
    }
    return 0;
}

std::optional<std::uint8_t> codeFromDpi(int dpi)
{
    for (std::uint8_t code = 1; code <= kMaxCode; ++code) {
        if (dpiFromCode(code) == dpi) {
            return code;
        }
    }
    return std::nullopt;
}

std::vector<int> supportedDpiValues()
{
    std::vector<int> values;
    for (std::uint8_t code = 1; code <= kMaxCode; ++code) {
        values.push_back(dpiFromCode(code));
    }
    return values;
}

int DpiSettings::enabledCount() const
{
    int count = 0;
    for (const auto& slot : slots) {
        count += slot.enabled ? 1 : 0;
    }
    return count;
}

}
