#pragma once

#include "openxidon/buttons.hpp"
#include "openxidon/general_block.hpp"
#include "openxidon/macro.hpp"
#include "openxidon/transport.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>

namespace openxidon {

struct NotifyEvent {
    int mode = 0;
    int dpiLevel = 0;
    int dpi = 0;
    std::array<std::uint8_t, kNotifyReportSize> raw{};
};

class Mouse {
public:
    explicit Mouse(std::unique_ptr<Transport> transport,
        std::chrono::milliseconds commandDelay = std::chrono::milliseconds(50));

    Transport& transport() { return *transport_; }

    std::array<std::uint8_t, 4> readDeviceId();
    int activeMode();
    void setActiveMode(int mode);

    GeneralBlock readGeneral(int mode);
    void writeGeneral(int mode, const GeneralBlock& block);

    void writeButtons(int mode, const ButtonMatrix& matrix);
    void writeMacro(int slot, const Macro& macro);

    std::optional<NotifyEvent> waitForEvent(int timeoutMs);

private:
    SendResult sendCommand(std::uint8_t cmd, std::uint8_t a0 = 0, std::uint8_t a1 = 0,
        std::uint8_t a2 = 0, std::uint8_t a3 = 0);
    std::vector<std::uint8_t> readCommand(std::uint8_t expectedCommand);
    void verifyGeneral(int mode, const GeneralBlock& expected);
    std::vector<std::uint8_t> newDataReport(std::uint8_t subcommand, std::uint8_t argA, std::uint8_t argB) const;
    void pause() const;
    void writePause() const;

    std::unique_ptr<Transport> transport_;
    std::chrono::milliseconds commandDelay_;
};

void requireMode(int mode);

}
