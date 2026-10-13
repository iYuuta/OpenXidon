#include "openxidon/transport.hpp"

#include "openxidon/general_block.hpp"
#include "openxidon/protocol.hpp"
#include "openxidon/util.hpp"

#include <algorithm>

namespace openxidon {

namespace {

std::string trimmedHex(const std::vector<std::uint8_t>& report)
{
    std::size_t end = report.size();
    while (end > 1 && report[end - 1] == 0) {
        --end;
    }
    std::string text = hexBytes(std::span<const std::uint8_t>(report.data(), end));
    if (end < report.size()) {
        text += " ... (" + std::to_string(report.size()) + " bytes)";
    }
    return text;
}

}

DryRunTransport::DryRunTransport(std::ostream& log)
    : log_(log)
{
}

SendResult DryRunTransport::sendFeature(const std::vector<std::uint8_t>& report)
{
    log_ << "[dry-run] SET_FEATURE " << trimmedHex(report) << '\n';
    if (report.empty()) {
        return SendResult::Acknowledged;
    }
    if (report[0] == kCommandReportId) {
        lastCommand_ = report;
        if (report.size() > 2 && report[1] == command::kMode && report[2] != 0) {
            activeMode_ = report[2];
        }
        return SendResult::Acknowledged;
    }
    if (report[0] == kDataReportId && report.size() > kDataHeaderSize && (report[1] & 0x0F) == 0x01) {
        auto& block = generalBlocks_[report[1]];
        block.assign(report.begin() + kDataHeaderSize, report.end());
    }
    return SendResult::Acknowledged;
}

std::vector<std::uint8_t> DryRunTransport::getFeature(std::uint8_t reportId, std::size_t size)
{
    std::vector<std::uint8_t> response(size, 0);
    response[0] = reportId;
    std::uint8_t cmd = lastCommand_.size() > 1 ? lastCommand_[1] : 0;

    if (reportId == kCommandReportId && size >= kCommandReportSize) {
        response[1] = cmd;
        if (cmd == command::kMode) {
            response[2] = activeMode_;
        }
    } else if (reportId == kDataReportId && (cmd & 0x0F) == 0x01) {
        response[1] = cmd;
        auto it = generalBlocks_.find(cmd);
        if (it != generalBlocks_.end()) {
            std::copy_n(it->second.begin(), std::min(it->second.size(), size - kDataHeaderSize),
                response.begin() + kDataHeaderSize);
        } else {
            const GeneralBlock placeholder = GeneralBlock::placeholder();
            const auto& bytes = placeholder.bytes();
            std::copy_n(bytes.begin(), std::min(bytes.size(), size - kDataHeaderSize),
                response.begin() + kDataHeaderSize);
        }
    }
    log_ << "[dry-run] GET_FEATURE " << trimmedHex(response) << '\n';
    return response;
}

std::optional<std::vector<std::uint8_t>> DryRunTransport::readInput(std::uint8_t, int)
{
    return std::nullopt;
}

}
