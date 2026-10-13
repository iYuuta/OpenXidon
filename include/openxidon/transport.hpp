#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <vector>

struct hid_device_;

namespace openxidon {

enum class SendResult {
    Acknowledged,
    Unacknowledged,
};

class Transport {
public:
    virtual ~Transport() = default;

    virtual SendResult sendFeature(const std::vector<std::uint8_t>& report) = 0;
    virtual std::vector<std::uint8_t> getFeature(std::uint8_t reportId, std::size_t size) = 0;
    virtual std::optional<std::vector<std::uint8_t>> readInput(std::uint8_t reportId, int timeoutMs) = 0;
};

struct DeviceInterface {
    std::string path;
    std::uint16_t usagePage = 0;
    std::uint16_t usage = 0;
    int interfaceNumber = -1;
    std::string product;
};

class HidTransport final : public Transport {
public:
    static std::vector<DeviceInterface> enumerate();
    static std::unique_ptr<HidTransport> open(const std::string& pathOverride = {});

    HidTransport(const HidTransport&) = delete;
    HidTransport& operator=(const HidTransport&) = delete;
    ~HidTransport() override;

    SendResult sendFeature(const std::vector<std::uint8_t>& report) override;
    std::vector<std::uint8_t> getFeature(std::uint8_t reportId, std::size_t size) override;
    std::optional<std::vector<std::uint8_t>> readInput(std::uint8_t reportId, int timeoutMs) override;

    std::string describeHandles() const;

private:
    struct Handle {
        hid_device_* device = nullptr;
        std::string path;
        std::vector<std::uint8_t> featureIds;
        std::vector<std::uint8_t> inputIds;
        bool descriptorKnown = false;
    };

    HidTransport() = default;

    Handle& handleFor(std::uint8_t reportId, bool feature);

    std::vector<Handle> handles_;
};

class DryRunTransport final : public Transport {
public:
    explicit DryRunTransport(std::ostream& log);

    SendResult sendFeature(const std::vector<std::uint8_t>& report) override;
    std::vector<std::uint8_t> getFeature(std::uint8_t reportId, std::size_t size) override;
    std::optional<std::vector<std::uint8_t>> readInput(std::uint8_t reportId, int timeoutMs) override;

private:
    std::ostream& log_;
    std::vector<std::uint8_t> lastCommand_;
    std::map<std::uint8_t, std::vector<std::uint8_t>> generalBlocks_;
    std::uint8_t activeMode_ = 1;
};

}
