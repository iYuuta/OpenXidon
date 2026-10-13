#pragma once

#include <cstddef>
#include <cstdint>

namespace openxidon {

inline constexpr std::uint16_t kVendorId = 0x145F;
inline constexpr std::uint16_t kProductId = 0x02A2;
inline constexpr std::uint16_t kVendorUsagePage = 0xFF00;

inline constexpr std::uint8_t kCommandReportId = 0x05;
inline constexpr std::uint8_t kDataReportId = 0x04;
inline constexpr std::uint8_t kNotifyReportId = 0x07;

inline constexpr std::size_t kCommandReportSize = 6;
inline constexpr std::size_t kDataReportSize = 0x208;
inline constexpr std::size_t kNotifyReportSize = 8;
inline constexpr std::size_t kDataHeaderSize = 8;
inline constexpr std::size_t kDataPayloadSize = kDataReportSize - kDataHeaderSize;

inline constexpr std::size_t kGeneralWriteSize = 0x8C;
inline constexpr std::uint8_t kGeneralLengthField = 0x7B;
inline constexpr std::uint8_t kMatrixLengthField = 0x50;
inline constexpr std::uint8_t kGeneralMarker = 0x64;

inline constexpr int kModeCount = 3;
inline constexpr std::size_t kButtonSlotCount = 20;
inline constexpr std::size_t kDpiSlotCount = 8;

inline constexpr std::size_t kMacroDataSize = 0x1FE;
inline constexpr std::size_t kMacroMaxBytes = 0x1FA;
inline constexpr std::size_t kMacroMaxEvents = (kMacroMaxBytes - 2) / 3;
inline constexpr int kMacroMaxSlot = 8;

inline constexpr std::uint8_t kSensorPmw3325 = 0x0B;

namespace command {
inline constexpr std::uint8_t kReadId = 0x01;
inline constexpr std::uint8_t kMode = 0x02;
inline constexpr std::uint8_t kMacro = 0x30;
inline constexpr std::uint8_t kMacroArg = 0x02;
}

constexpr std::uint8_t generalSubcommand(int mode)
{
    return static_cast<std::uint8_t>((mode << 4) | 0x01);
}

constexpr std::uint8_t matrixSubcommand(int mode)
{
    return static_cast<std::uint8_t>((mode << 4) | 0x02);
}

}
