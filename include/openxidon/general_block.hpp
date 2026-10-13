#pragma once

#include "openxidon/dpi.hpp"
#include "openxidon/lighting.hpp"
#include "openxidon/protocol.hpp"

#include <array>
#include <cstdint>

namespace openxidon {

class GeneralBlock {
public:
    using Bytes = std::array<std::uint8_t, kDataPayloadSize>;

    GeneralBlock() = default;
    explicit GeneralBlock(const Bytes& bytes);

    static GeneralBlock placeholder();

    const Bytes& bytes() const { return bytes_; }
    Bytes& bytes() { return bytes_; }

    std::uint8_t sensorCode() const;
    bool isEmpty() const;

    int pollingRateHz() const;
    void setPollingRateHz(int hz);

    DpiSettings dpi() const;
    void setDpi(const DpiSettings& settings);

    Lighting lighting() const;
    void setLighting(const Lighting& lighting);

private:
    Bytes bytes_{};
};

}
