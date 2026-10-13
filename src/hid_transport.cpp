#include "openxidon/transport.hpp"

#include "openxidon/protocol.hpp"

#include <hidapi.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <set>
#include <sstream>
#include <stdexcept>
#include <thread>

namespace openxidon {

namespace {

constexpr int kRetries = 3;
constexpr auto kRetryDelay = std::chrono::milliseconds(200);
constexpr std::size_t kMaxDescriptorSize = 4096;

class HidLibrary {
public:
    HidLibrary()
    {
        if (hid_init() != 0) {
            throw std::runtime_error("hid_init failed");
        }
    }
    ~HidLibrary() { hid_exit(); }
};

void ensureHidLibrary()
{
    static HidLibrary library;
}

std::string narrow(const wchar_t* text)
{
    std::string result;
    if (!text) {
        return result;
    }
    for (; *text; ++text) {
        result += *text < 0x80 ? static_cast<char>(*text) : '?';
    }
    return result;
}

std::string hidErrorText(hid_device* device)
{
    std::string message = narrow(hid_error(device));
    return message.empty() ? "unknown error" : message;
}

struct DescriptorReports {
    std::set<std::uint8_t> feature;
    std::set<std::uint8_t> input;
};

DescriptorReports parseReportDescriptor(const std::uint8_t* data, std::size_t size)
{
    DescriptorReports reports;
    std::uint8_t reportId = 0;
    std::size_t i = 0;
    while (i < size) {
        std::uint8_t prefix = data[i];
        if (prefix == 0xFE) {
            if (i + 1 >= size) {
                break;
            }
            i += 3 + data[i + 1];
            continue;
        }
        std::size_t length = prefix & 0x03;
        if (length == 3) {
            length = 4;
        }
        std::uint32_t value = 0;
        for (std::size_t b = 0; b < length && i + 1 + b < size; ++b) {
            value |= static_cast<std::uint32_t>(data[i + 1 + b]) << (8 * b);
        }
        std::uint8_t tag = prefix & 0xFC;
        if (tag == 0x84) {
            reportId = static_cast<std::uint8_t>(value);
        } else if (tag == 0x80) {
            reports.input.insert(reportId);
        } else if (tag == 0xB0) {
            reports.feature.insert(reportId);
        }
        i += 1 + length;
    }
    return reports;
}

bool contains(const std::vector<std::uint8_t>& ids, std::uint8_t id)
{
    return std::find(ids.begin(), ids.end(), id) != ids.end();
}

std::string joinIds(const std::vector<std::uint8_t>& ids)
{
    std::string result;
    char buffer[8];
    for (auto id : ids) {
        std::snprintf(buffer, sizeof buffer, result.empty() ? "%u" : ",%u", id);
        result += buffer;
    }
    return result.empty() ? "-" : result;
}

}

std::vector<DeviceInterface> HidTransport::enumerate()
{
    ensureHidLibrary();
    std::vector<DeviceInterface> interfaces;
    hid_device_info* list = hid_enumerate(kVendorId, kProductId);
    for (hid_device_info* info = list; info; info = info->next) {
        interfaces.push_back({info->path ? info->path : "", info->usage_page, info->usage,
            info->interface_number, narrow(info->product_string)});
    }
    hid_free_enumeration(list);
    return interfaces;
}

std::unique_ptr<HidTransport> HidTransport::open(const std::string& pathOverride)
{
    ensureHidLibrary();

    std::vector<std::string> paths;
    if (!pathOverride.empty()) {
        paths.push_back(pathOverride);
    } else {
        std::vector<std::string> vendorPaths;
        std::vector<std::string> allPaths;
        for (const auto& info : enumerate()) {
            if (std::find(allPaths.begin(), allPaths.end(), info.path) == allPaths.end()) {
                allPaths.push_back(info.path);
            }
            if (info.usagePage == kVendorUsagePage
                && std::find(vendorPaths.begin(), vendorPaths.end(), info.path) == vendorPaths.end()) {
                vendorPaths.push_back(info.path);
            }
        }
        paths = vendorPaths.empty() ? allPaths : vendorPaths;
    }
    if (paths.empty()) {
        throw std::runtime_error("Trust GXT 940 (145F:02A2) not found");
    }

    std::unique_ptr<HidTransport> transport(new HidTransport());
    std::string lastError;
    for (const auto& path : paths) {
        hid_device* device = hid_open_path(path.c_str());
        if (!device) {
            lastError = "cannot open " + path + ": " + narrow(hid_error(nullptr))
                + " (install udev/70-openxidon.rules or run as root)";
            continue;
        }
        Handle handle;
        handle.device = device;
        handle.path = path;
        std::uint8_t descriptor[kMaxDescriptorSize];
        int size = hid_get_report_descriptor(device, descriptor, sizeof descriptor);
        if (size > 0) {
            auto reports = parseReportDescriptor(descriptor, static_cast<std::size_t>(size));
            handle.featureIds.assign(reports.feature.begin(), reports.feature.end());
            handle.inputIds.assign(reports.input.begin(), reports.input.end());
            handle.descriptorKnown = true;
        }
        bool relevant = !handle.descriptorKnown || contains(handle.featureIds, kCommandReportId)
            || contains(handle.featureIds, kDataReportId) || contains(handle.inputIds, kNotifyReportId);
        if (relevant) {
            transport->handles_.push_back(handle);
        } else {
            hid_close(device);
        }
    }
    if (transport->handles_.empty()) {
        throw std::runtime_error(lastError.empty() ? "no usable HID interface found" : lastError);
    }
    return transport;
}

HidTransport::~HidTransport()
{
    for (auto& handle : handles_) {
        if (handle.device) {
            hid_close(handle.device);
        }
    }
}

HidTransport::Handle& HidTransport::handleFor(std::uint8_t reportId, bool feature)
{
    for (auto& handle : handles_) {
        if (contains(feature ? handle.featureIds : handle.inputIds, reportId)) {
            return handle;
        }
    }
    for (auto& handle : handles_) {
        if (!handle.descriptorKnown) {
            return handle;
        }
    }
    return handles_.front();
}

SendResult HidTransport::sendFeature(const std::vector<std::uint8_t>& report)
{
    Handle& handle = handleFor(report.at(0), true);
    for (int attempt = 0; attempt < kRetries; ++attempt) {
        int written = hid_send_feature_report(handle.device, report.data(), report.size());
        if (written >= 0) {
            return SendResult::Acknowledged;
        }
        if (hidErrorText(handle.device).find("Protocol error") != std::string::npos) {
            return SendResult::Unacknowledged;
        }
        std::this_thread::sleep_for(kRetryDelay);
    }
    char buffer[64];
    std::snprintf(buffer, sizeof buffer, "set feature report 0x%02X failed: ", report.at(0));
    throw std::runtime_error(buffer + hidErrorText(handle.device));
}

std::vector<std::uint8_t> HidTransport::getFeature(std::uint8_t reportId, std::size_t size)
{
    Handle& handle = handleFor(reportId, true);
    std::vector<std::uint8_t> buffer(size, 0);
    for (int attempt = 0; attempt < kRetries; ++attempt) {
        std::fill(buffer.begin(), buffer.end(), std::uint8_t{0});
        buffer[0] = reportId;
        int read = hid_get_feature_report(handle.device, buffer.data(), buffer.size());
        if (read > 0) {
            buffer.resize(std::max<std::size_t>(static_cast<std::size_t>(read), size));
            return buffer;
        }
        std::this_thread::sleep_for(kRetryDelay);
    }
    char message[64];
    std::snprintf(message, sizeof message, "get feature report 0x%02X failed: ", reportId);
    throw std::runtime_error(message + hidErrorText(handle.device));
}

std::optional<std::vector<std::uint8_t>> HidTransport::readInput(std::uint8_t reportId, int timeoutMs)
{
    Handle& handle = handleFor(reportId, false);
    std::vector<std::uint8_t> buffer(64, 0);
    int read = hid_read_timeout(handle.device, buffer.data(), buffer.size(), timeoutMs);
    if (read < 0) {
        throw std::runtime_error("read failed: " + hidErrorText(handle.device));
    }
    if (read == 0) {
        return std::nullopt;
    }
    buffer.resize(static_cast<std::size_t>(read));
    return buffer;
}

std::string HidTransport::describeHandles() const
{
    std::ostringstream out;
    for (const auto& handle : handles_) {
        out << handle.path;
        if (handle.descriptorKnown) {
            out << "  feature=" << joinIds(handle.featureIds) << "  input=" << joinIds(handle.inputIds);
        } else {
            out << "  (report descriptor unavailable)";
        }
        out << '\n';
    }
    return out.str();
}

}
