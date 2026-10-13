#include "openxidon/mouse.hpp"

#include "openxidon/protocol.hpp"

#include <algorithm>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <thread>

namespace openxidon {

namespace {

constexpr auto kWritePause = std::chrono::milliseconds(22);

}

void requireMode(int mode)
{
    if (mode < 1 || mode > kModeCount) {
        throw std::invalid_argument("mode must be between 1 and " + std::to_string(kModeCount));
    }
}

Mouse::Mouse(std::unique_ptr<Transport> transport, std::chrono::milliseconds commandDelay)
    : transport_(std::move(transport))
    , commandDelay_(commandDelay)
{
}

void Mouse::pause() const
{
    std::this_thread::sleep_for(commandDelay_);
}

void Mouse::writePause() const
{
    std::this_thread::sleep_for(kWritePause);
}

SendResult Mouse::sendCommand(std::uint8_t cmd, std::uint8_t a0, std::uint8_t a1, std::uint8_t a2, std::uint8_t a3)
{
    return transport_->sendFeature({kCommandReportId, cmd, a0, a1, a2, a3});
}

std::vector<std::uint8_t> Mouse::readCommand(std::uint8_t expectedCommand)
{
    auto response = transport_->getFeature(kCommandReportId, kCommandReportSize);
    if (response.size() < kCommandReportSize) {
        throw std::runtime_error("short command response");
    }
    if (response[1] != expectedCommand) {
        char message[96];
        std::snprintf(message, sizeof message, "mouse did not take command 0x%02X (it answered for 0x%02X)",
            expectedCommand, response[1]);
        throw std::runtime_error(message);
    }
    return response;
}

void Mouse::verifyGeneral(int mode, const GeneralBlock& expected)
{
    pause();
    GeneralBlock actual = readGeneral(mode);
    for (std::size_t i = 1; i < kGeneralWriteSize; ++i) {
        if (actual.bytes()[i] != expected.bytes()[i]) {
            char message[128];
            std::snprintf(message, sizeof message,
                "mode %d settings were not saved (byte 0x%02zX is 0x%02X, expected 0x%02X)", mode, i,
                actual.bytes()[i], expected.bytes()[i]);
            throw std::runtime_error(message);
        }
    }
}

std::vector<std::uint8_t> Mouse::newDataReport(std::uint8_t subcommand, std::uint8_t argA, std::uint8_t argB) const
{
    std::vector<std::uint8_t> report(kDataReportSize, 0);
    report[0] = kDataReportId;
    report[1] = subcommand;
    report[2] = argA;
    report[3] = argB;
    return report;
}

std::array<std::uint8_t, 4> Mouse::readDeviceId()
{
    sendCommand(command::kReadId);
    pause();
    auto response = readCommand(command::kReadId);
    return {response[2], response[3], response[4], response[5]};
}

int Mouse::activeMode()
{
    sendCommand(command::kMode);
    pause();
    auto response = readCommand(command::kMode);
    int mode = response[2];
    if (mode < 1 || mode > kModeCount) {
        throw std::runtime_error("device reported invalid active mode " + std::to_string(mode));
    }
    return mode;
}

void Mouse::setActiveMode(int mode)
{
    requireMode(mode);
    SendResult result = sendCommand(command::kMode, static_cast<std::uint8_t>(mode));
    writePause();
    if (result == SendResult::Unacknowledged && activeMode() != mode) {
        throw std::runtime_error("mouse did not switch to mode " + std::to_string(mode));
    }
}

GeneralBlock Mouse::readGeneral(int mode)
{
    requireMode(mode);
    const std::chrono::milliseconds waits[] = {commandDelay_, std::chrono::milliseconds(200),
        std::chrono::milliseconds(500), std::chrono::milliseconds(1000)};
    std::string lastProblem;
    GeneralBlock lastBlock;
    bool haveBlock = false;
    for (auto wait : waits) {
        sendCommand(generalSubcommand(mode));
        std::this_thread::sleep_for(wait);
        std::vector<std::uint8_t> response;
        try {
            response = transport_->getFeature(kDataReportId, kDataReportSize);
        } catch (const std::exception& error) {
            lastProblem = error.what();
            continue;
        }
        if (response.size() < kDataReportSize) {
            lastProblem = "short data response";
            continue;
        }
        if (response[1] != generalSubcommand(mode)) {
            lastProblem = "mouse returned the wrong settings block for mode " + std::to_string(mode);
            continue;
        }
        GeneralBlock::Bytes bytes{};
        std::copy_n(response.begin() + kDataHeaderSize, bytes.size(), bytes.begin());
        lastBlock = GeneralBlock(bytes);
        haveBlock = true;
        if (!lastBlock.isEmpty()) {
            return lastBlock;
        }
        lastProblem = "mouse returned an empty settings block for mode " + std::to_string(mode);
    }
    if (haveBlock) {
        return lastBlock;
    }
    throw std::runtime_error(lastProblem);
}

void Mouse::writeGeneral(int mode, const GeneralBlock& block)
{
    requireMode(mode);
    auto report = newDataReport(generalSubcommand(mode), 0, kGeneralLengthField);
    std::copy_n(block.bytes().begin(), kGeneralWriteSize, report.begin() + kDataHeaderSize);
    report[kDataHeaderSize] = kGeneralMarker;
    SendResult result = transport_->sendFeature(report);
    writePause();
    if (result == SendResult::Unacknowledged) {
        verifyGeneral(mode, block);
    }
}

void Mouse::writeButtons(int mode, const ButtonMatrix& matrix)
{
    requireMode(mode);
    auto report = newDataReport(matrixSubcommand(mode), 0, kMatrixLengthField);
    auto bytes = matrix.encode();
    std::copy(bytes.begin(), bytes.end(), report.begin() + kDataHeaderSize);
    transport_->sendFeature(report);
    writePause();
}

void Mouse::writeMacro(int slot, const Macro& macro)
{
    if (slot < 1 || slot > kMacroMaxSlot) {
        throw std::invalid_argument("macro slot must be between 1 and " + std::to_string(kMacroMaxSlot));
    }
    auto data = macro.encode();
    if (data.size() > kMacroDataSize) {
        throw std::invalid_argument("macro is too large");
    }
    auto report = newDataReport(command::kMacro, command::kMacroArg, 0);
    report[kDataHeaderSize] = static_cast<std::uint8_t>(slot);
    std::copy(data.begin(), data.end(), report.begin() + kDataHeaderSize + 1);
    transport_->sendFeature(report);
    writePause();
}

std::optional<NotifyEvent> Mouse::waitForEvent(int timeoutMs)
{
    auto data = transport_->readInput(kNotifyReportId, timeoutMs);
    if (!data || data->empty() || (*data)[0] != kNotifyReportId) {
        return std::nullopt;
    }
    NotifyEvent event;
    std::copy_n(data->begin(), std::min(data->size(), event.raw.size()), event.raw.begin());
    event.mode = event.raw[1];
    event.dpiLevel = event.raw[2] & 0x0F;
    event.dpi = dpiFromCode(event.raw[3]);
    return event;
}

}
