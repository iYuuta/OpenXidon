#include "openxidon/buttons.hpp"
#include "openxidon/dpi.hpp"
#include "openxidon/general_block.hpp"
#include "openxidon/keys.hpp"
#include "openxidon/lighting.hpp"
#include "openxidon/macro.hpp"
#include "openxidon/mouse.hpp"
#include "openxidon/protocol.hpp"
#include "openxidon/transport.hpp"
#include "openxidon/util.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <functional>
#include <iostream>
#include <sstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace openxidon;

namespace {

struct Options {
    bool dryRun = false;
    bool force = false;
    std::string devicePath;
    int delayMs = 50;
    std::vector<std::string> args;
};

class UsageError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

constexpr const char* kUsage = R"(Usage: openxidon [options] <command> [arguments]

Options:
  --dry-run          print the HID reports instead of sending them
  --device <path>    use a specific hidraw path
  --delay <ms>       wait between a command and its response (default 50)
  --force            write even if the sensor code does not match PMW3325
  -h, --help         show this help

Commands:
  list                                   list matching HID interfaces
  info [mode|all]                        show device and mode settings
  mode [1-3]                             get or set the active onboard mode
  dump <mode>                            hex dump of a mode's settings block (save it as a backup)
  restore <mode> <file>                  write a saved 'dump' file back to a mode
  polling <mode|all> <125|250|500|1000>  set the USB polling rate
  dpi <mode|all> set <slot> <dpi>[x<dpi>]
  dpi <mode|all> enable|disable <slot>
  dpi <mode|all> current <slot>
  dpi <mode|all> color <slot> <RRGGBB>
  dpi <mode|all> xy on|off
  effects                                list lighting effects with their numbers
  light                                  pick a lighting effect interactively
  light [mode|all] <# | name | next | prev> [colors...] [speed=N] [brightness=N] [direction=0|1]
                                         change lighting (mode defaults to the active one)
  buttons <mode|all> [<slot>=<function> ...]
                                         write the button map (defaults + overrides)
  macro <slot 1-8> <file|->              upload a macro from a text file
  listen                                 print mode/DPI events from the mouse
  functions                              list effects, button functions and key names
  probe                                  read-only connection test (for troubleshooting)

DPI slots are 1-8 (the official software uses 1-6), button slots are 1-20.
)";

std::vector<int> parseModes(const std::string& text)
{
    if (toLower(text) == "all") {
        std::vector<int> modes;
        for (int mode = 1; mode <= kModeCount; ++mode) {
            modes.push_back(mode);
        }
        return modes;
    }
    return {static_cast<int>(parseInteger(text, 1, kModeCount, "mode"))};
}

int parseDpiSlot(const std::string& text)
{
    return static_cast<int>(parseInteger(text, 1, kDpiSlotCount, "DPI slot")) - 1;
}

void requireArgs(const std::vector<std::string>& args, std::size_t count)
{
    if (args.size() < count) {
        throw UsageError("missing arguments");
    }
}

std::string sensorName(std::uint8_t code)
{
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, "0x%02X%s", code, code == kSensorPmw3325 ? " (PMW3325)" : "");
    return buffer;
}

std::string effectOptions(const EffectInfo& info)
{
    std::vector<std::string> options;
    if (info.hasSpeed) {
        options.emplace_back("speed");
    }
    if (info.hasBrightness) {
        options.emplace_back("brightness");
    }
    if (info.hasDirection) {
        options.emplace_back("direction");
    }
    if (info.maxColors == 1) {
        options.emplace_back("1 color");
    } else if (info.maxColors > 1) {
        options.emplace_back((info.hasColorCount ? "up to " : "") + std::to_string(info.maxColors) + " colors");
    }
    std::string text;
    for (std::size_t i = 0; i < options.size(); ++i) {
        text += (i ? ", " : "") + options[i];
    }
    return text.empty() ? "-" : text;
}

void printEffectTable()
{
    std::printf("  %2s  %-15s %-16s %s\n", "#", "Effect", "Name", "Options");
    std::vector<EffectInfo> ordered(allEffects().begin(), allEffects().end());
    std::sort(ordered.begin(), ordered.end(), [](const EffectInfo& a, const EffectInfo& b) {
        return effectMenuNumber(a.effect) < effectMenuNumber(b.effect);
    });
    for (const auto& info : ordered) {
        std::printf("  %2d  %-15s %-16s %s\n", effectMenuNumber(info.effect), std::string(info.displayName).c_str(),
            std::string(info.name).c_str(), effectOptions(info).c_str());
    }
}

std::string prompt(const std::string& question, const std::string& fallback)
{
    std::cout << question;
    if (!fallback.empty()) {
        std::cout << " [" << fallback << "]";
    }
    std::cout << ": " << std::flush;
    std::string line;
    if (!std::getline(std::cin, line)) {
        throw std::runtime_error("input closed");
    }
    auto start = line.find_first_not_of(" \t");
    if (start == std::string::npos) {
        return fallback;
    }
    auto end = line.find_last_not_of(" \t");
    return line.substr(start, end - start + 1);
}

template <typename Parse>
auto promptUntilValid(const std::string& question, const std::string& fallback, Parse parse)
{
    while (true) {
        std::string answer = prompt(question, fallback);
        try {
            return parse(answer);
        } catch (const std::exception& error) {
            std::cout << "  " << error.what() << '\n';
        }
    }
}

std::string describeLighting(const Lighting& lighting)
{
    auto info = effectInfo(lighting.effect);
    if (!info) {
        return "unknown effect " + std::to_string(static_cast<int>(lighting.effect));
    }
    std::string text(info->displayName);
    if (info->hasSpeed) {
        text += " speed=" + std::to_string(lighting.speed);
    }
    if (info->hasBrightness) {
        text += " brightness=" + std::to_string(lighting.brightness);
    }
    if (info->hasDirection) {
        text += std::string(" direction=") + (lighting.direction ? "1" : "0");
    }
    if (!lighting.colors.empty()) {
        text += " colors=";
        for (std::size_t i = 0; i < lighting.colors.size(); ++i) {
            text += (i ? "," : "") + formatRgb(lighting.colors[i]);
        }
    }
    return text;
}

void printMode(int mode, const GeneralBlock& block)
{
    std::cout << "Mode " << mode << ":\n";
    std::cout << "  sensor:   " << sensorName(block.sensorCode()) << '\n';
    int rate = block.pollingRateHz();
    std::cout << "  polling:  " << (rate ? std::to_string(rate) + " Hz" : "unknown") << '\n';

    DpiSettings dpi = block.dpi();
    std::cout << "  dpi:      " << (dpi.xyIndependent ? "x/y independent" : "linked")
              << ", current slot " << (dpi.currentSlot >= 0 ? std::to_string(dpi.currentSlot + 1) : "?") << '\n';
    for (std::size_t i = 0; i < kDpiSlotCount; ++i) {
        const DpiSlot& slot = dpi.slots[i];
        std::string value = std::to_string(slot.x);
        if (dpi.xyIndependent) {
            value += "x" + std::to_string(slot.y);
        }
        std::printf("    %zu  %-11s %-4s %s%s\n", i + 1, value.c_str(), slot.enabled ? "on" : "off",
            formatRgb(slot.color).c_str(), static_cast<int>(i) == dpi.currentSlot ? "  <" : "");
    }
    std::cout << "  lighting: " << describeLighting(block.lighting()) << '\n';
}

class App {
public:
    explicit App(Options options)
        : options_(std::move(options))
    {
    }

    int run()
    {
        if (options_.args.empty()) {
            throw UsageError("no command given");
        }
        const std::string command = toLower(options_.args[0]);
        std::vector<std::string> args(options_.args.begin() + 1, options_.args.end());

        if (command == "list") {
            return list();
        }
        if (command == "functions") {
            return functions();
        }
        if (command == "effects") {
            return effects();
        }
        if (command == "probe") {
            return probe();
        }
        if (command == "restore") {
            return restore(args);
        }
        if (command == "info") {
            return info(args);
        }
        if (command == "mode") {
            return mode(args);
        }
        if (command == "dump") {
            return dump(args);
        }
        if (command == "polling") {
            return polling(args);
        }
        if (command == "dpi") {
            return dpi(args);
        }
        if (command == "light") {
            return light(args);
        }
        if (command == "buttons") {
            return buttons(args);
        }
        if (command == "macro") {
            return macro(args);
        }
        if (command == "listen") {
            return listen();
        }
        throw UsageError("unknown command '" + command + "'");
    }

private:
    Mouse& mouse()
    {
        if (!mouse_) {
            std::unique_ptr<Transport> transport;
            if (options_.dryRun) {
                transport = std::make_unique<DryRunTransport>(std::cout);
            } else {
                transport = HidTransport::open(options_.devicePath);
            }
            mouse_ = std::make_unique<Mouse>(std::move(transport), std::chrono::milliseconds(options_.delayMs));
        }
        return *mouse_;
    }

    void modifyGeneral(const std::vector<int>& modes, const std::function<void(GeneralBlock&)>& change)
    {
        for (int mode : modes) {
            GeneralBlock block = mouse().readGeneral(mode);
            if (block.sensorCode() != kSensorPmw3325 && !options_.force) {
                throw std::runtime_error("mode " + std::to_string(mode) + " reports sensor "
                    + sensorName(block.sensorCode()) + ", refusing to write (use --force)");
            }
            change(block);
            mouse().writeGeneral(mode, block);
            std::cout << "mode " << mode << " updated\n";
        }
    }

    int list()
    {
        auto interfaces = HidTransport::enumerate();
        if (interfaces.empty()) {
            std::cout << "no Trust GXT 940 found\n";
            return 1;
        }
        for (const auto& item : interfaces) {
            std::printf("%s  usage_page=0x%04X usage=0x%04X interface=%d  %s\n", item.path.c_str(),
                item.usagePage, item.usage, item.interfaceNumber, item.product.c_str());
        }
        if (!options_.dryRun) {
            try {
                auto transport = HidTransport::open(options_.devicePath);
                std::cout << "\nOpened:\n" << transport->describeHandles();
            } catch (const std::exception& error) {
                std::cout << "\nOpen failed: " << error.what() << '\n';
            }
        }
        return 0;
    }

    int probe()
    {
        Transport& transport = mouse().transport();
        if (auto* hid = dynamic_cast<HidTransport*>(&transport)) {
            std::cout << "Handles:\n" << hid->describeHandles() << '\n';
        }
        auto show = [](const std::vector<std::uint8_t>& data, std::size_t limit) {
            std::size_t count = std::min(limit, data.size());
            return hexBytes(std::span<const std::uint8_t>(data.data(), count))
                + (count < data.size() ? " ... (" + std::to_string(data.size()) + " bytes)" : "");
        };
        auto tryGet = [&](std::uint8_t id, std::size_t size, std::size_t limit) {
            try {
                auto data = transport.getFeature(id, size);
                std::cout << "    get " << static_cast<int>(id) << " ok:   " << show(data, limit) << '\n';
            } catch (const std::exception& error) {
                std::cout << "    get " << static_cast<int>(id) << " FAIL: " << error.what() << '\n';
            }
        };
        auto trySet = [&](std::vector<std::uint8_t> report) {
            try {
                bool acknowledged = transport.sendFeature(report) == SendResult::Acknowledged;
                std::cout << (acknowledged ? "    set ok:   " : "    set sent (no ack): ") << hexBytes(report) << '\n';
            } catch (const std::exception& error) {
                std::cout << "    set FAIL: " << hexBytes(report) << "  -> " << error.what() << '\n';
            }
        };
        auto wait = [this] { std::this_thread::sleep_for(std::chrono::milliseconds(options_.delayMs)); };

        std::cout << "1. read report 5 without a command\n";
        tryGet(kCommandReportId, kCommandReportSize, 6);
        std::cout << "2. read report 4 without a command\n";
        tryGet(kDataReportId, kDataReportSize, 40);
        std::cout << "3. command 01 (read ID)\n";
        trySet({kCommandReportId, command::kReadId, 0, 0, 0, 0});
        wait();
        tryGet(kCommandReportId, kCommandReportSize, 6);
        std::cout << "4. command 02 (read active mode)\n";
        trySet({kCommandReportId, command::kMode, 0, 0, 0, 0});
        wait();
        tryGet(kCommandReportId, kCommandReportSize, 6);
        std::cout << "5. command 11 (read mode 1 settings)\n";
        trySet({kCommandReportId, generalSubcommand(1), 0, 0, 0, 0});
        wait();
        tryGet(kDataReportId, kDataReportSize, 40);
        std::cout << "\nNothing was written to the mouse's settings.\n";
        return 0;
    }

    int functions()
    {
        std::cout << "Lighting effects:\n ";
        for (const auto& effect : allEffects()) {
            std::cout << ' ' << effect.name;
        }
        std::cout << "\n\nButton functions:\n ";
        for (const auto& name : simpleButtonFunctionNames()) {
            std::cout << ' ' << name;
        }
        std::cout << "\n  key:<key>  combo:<mod>+...+<key>[+<key>]  media:<name>  dpi-lock:<dpi>\n"
                     "  macro:<slot>[:<count>[:<mode 1|2|4>]]  fire:<target>:<loop 1-255>:<interval 5-255>\n"
                     "  raw:<hex>\n\nMedia:\n ";
        for (const auto& name : mediaFunctionNames()) {
            std::cout << ' ' << name;
        }
        std::cout << "\n\nModifiers:\n  ctrl shift alt win lctrl lshift lalt lwin rctrl rshift ralt rwin\n"
                     "\nMouse buttons (fire targets, macro mouse:<name>):\n  left right middle back forward\n"
                     "\nKeys:\n ";
        int column = 0;
        for (const auto& name : keyNames()) {
            std::cout << ' ' << name;
            if (++column % 16 == 0) {
                std::cout << "\n ";
            }
        }
        std::cout << "\n\nMacro file lines:\n"
                     "  down|up|tap <key|modifier|mouse:<button>> [delay_ms]\n"
                     "  move x|y <-128..127> [delay_ms]\n"
                     "  wheel <-128..127> [delay_ms]\n";
        return 0;
    }

    int info(const std::vector<std::string>& args)
    {
        auto modes = parseModes(args.empty() ? "all" : args[0]);
        auto deviceId = mouse().readDeviceId();
        int active = mouse().activeMode();
        std::cout << "Device ID:   " << hexBytes(deviceId) << '\n';
        std::cout << "Active mode: " << active << "\n\n";
        int failures = 0;
        for (int mode : modes) {
            try {
                GeneralBlock block = mouse().readGeneral(mode);
                if (block.isEmpty()) {
                    std::cout << "Mode " << mode << ":\n  (the mouse returned an empty settings block)\n";
                    ++failures;
                } else {
                    printMode(mode, block);
                }
            } catch (const std::exception& error) {
                std::cout << "Mode " << mode << ":\n  could not read: " << error.what() << '\n';
                ++failures;
            }
        }
        return failures == 0 ? 0 : 1;
    }

    int restore(const std::vector<std::string>& args)
    {
        requireArgs(args, 2);
        int target = static_cast<int>(parseInteger(args[0], 1, kModeCount, "mode"));
        std::ifstream file(args[1]);
        if (!file) {
            throw std::runtime_error("cannot open " + args[1]);
        }
        GeneralBlock::Bytes bytes{};
        std::size_t count = 0;
        std::string line;
        while (std::getline(file, line)) {
            auto colon = line.find(':');
            if (colon == std::string::npos) {
                continue;
            }
            std::size_t offset = static_cast<std::size_t>(
                parseInteger("0x" + line.substr(0, colon), 0, static_cast<long>(bytes.size()) - 1, "dump offset"));
            std::istringstream values(line.substr(colon + 1));
            std::string token;
            while (values >> token) {
                if (offset >= bytes.size()) {
                    break;
                }
                bytes[offset++] = static_cast<std::uint8_t>(parseInteger("0x" + token, 0, 255, "dump byte"));
                count = std::max(count, offset);
            }
        }
        if (count < kGeneralWriteSize) {
            throw std::runtime_error(args[1] + " is not a complete 'openxidon dump' file");
        }
        GeneralBlock block(bytes);
        if (block.sensorCode() != kSensorPmw3325 && !options_.force) {
            throw std::runtime_error(args[1] + " does not contain a PMW3325 settings block (use --force to write anyway)");
        }
        std::cout << "Restoring mode " << target << " from " << args[1] << ":\n";
        printMode(target, block);
        mouse().writeGeneral(target, block);
        std::cout << "mode " << target << " restored\n";
        return 0;
    }

    int mode(const std::vector<std::string>& args)
    {
        if (args.empty()) {
            std::cout << mouse().activeMode() << '\n';
            return 0;
        }
        int target = static_cast<int>(parseInteger(args[0], 1, kModeCount, "mode"));
        mouse().setActiveMode(target);
        std::cout << "active mode set to " << target << '\n';
        return 0;
    }

    int dump(const std::vector<std::string>& args)
    {
        requireArgs(args, 1);
        int target = static_cast<int>(parseInteger(args[0], 1, kModeCount, "mode"));
        const GeneralBlock block = mouse().readGeneral(target);
        std::cout << hexDump(block.bytes());
        return 0;
    }

    int polling(const std::vector<std::string>& args)
    {
        requireArgs(args, 2);
        int hz = static_cast<int>(parseInteger(args[1], 1, 1000, "polling rate"));
        modifyGeneral(parseModes(args[0]), [hz](GeneralBlock& block) { block.setPollingRateHz(hz); });
        return 0;
    }

    int dpi(const std::vector<std::string>& args)
    {
        requireArgs(args, 3);
        auto modes = parseModes(args[0]);
        const std::string action = toLower(args[1]);
        std::function<void(DpiSettings&)> change;

        if (action == "set") {
            requireArgs(args, 4);
            int slot = parseDpiSlot(args[2]);
            auto parts = split(toLower(args[3]), 'x');
            if (parts.size() > 2) {
                throw UsageError("DPI must be <dpi> or <x>x<y>");
            }
            int x = static_cast<int>(parseInteger(parts[0], kMinDpi, kMaxDpi, "DPI"));
            int y = parts.size() == 2 ? static_cast<int>(parseInteger(parts[1], kMinDpi, kMaxDpi, "DPI")) : x;
            if (!codeFromDpi(x) || !codeFromDpi(y)) {
                throw std::invalid_argument("DPI must be 200-5000 in steps of 100 or 5500-10000 in steps of 500");
            }
            change = [slot, x, y, separate = parts.size() == 2](DpiSettings& settings) {
                if (separate) {
                    settings.xyIndependent = true;
                }
                settings.slots[slot].x = x;
                settings.slots[slot].y = y;
            };
        } else if (action == "enable" || action == "disable") {
            int slot = parseDpiSlot(args[2]);
            bool enabled = action == "enable";
            change = [slot, enabled](DpiSettings& settings) { settings.slots[slot].enabled = enabled; };
        } else if (action == "current") {
            int slot = parseDpiSlot(args[2]);
            change = [slot](DpiSettings& settings) { settings.currentSlot = slot; };
        } else if (action == "color") {
            requireArgs(args, 4);
            int slot = parseDpiSlot(args[2]);
            Rgb color = parseRgb(args[3]);
            change = [slot, color](DpiSettings& settings) { settings.slots[slot].color = color; };
        } else if (action == "xy") {
            const std::string value = toLower(args[2]);
            if (value != "on" && value != "off") {
                throw UsageError("dpi xy takes on or off");
            }
            bool on = value == "on";
            change = [on](DpiSettings& settings) {
                settings.xyIndependent = on;
                if (!on) {
                    for (auto& slot : settings.slots) {
                        slot.y = slot.x;
                    }
                }
            };
        } else {
            throw UsageError("unknown dpi action '" + action + "'");
        }

        modifyGeneral(modes, [&change](GeneralBlock& block) {
            DpiSettings settings = block.dpi();
            if (settings.currentSlot < 0) {
                for (std::size_t i = 0; i < kDpiSlotCount; ++i) {
                    if (settings.slots[i].enabled) {
                        settings.currentSlot = static_cast<int>(i);
                        break;
                    }
                }
            }
            for (auto& slot : settings.slots) {
                if (!slot.enabled) {
                    continue;
                }
                if (!codeFromDpi(slot.x)) {
                    slot.x = kMinDpi;
                }
                if (!codeFromDpi(slot.y)) {
                    slot.y = slot.x;
                }
            }
            change(settings);
            if (settings.currentSlot >= 0 && !settings.slots[settings.currentSlot].enabled) {
                for (std::size_t i = 0; i < kDpiSlotCount; ++i) {
                    if (settings.slots[i].enabled) {
                        settings.currentSlot = static_cast<int>(i);
                        break;
                    }
                }
            }
            block.setDpi(settings);
        });
        return 0;
    }

    int effects()
    {
        printEffectTable();
        std::cout << "\nExamples:\n"
                     "  openxidon light                      interactive picker\n"
                     "  openxidon light breathing            switch the active mode to Breathing\n"
                     "  openxidon light 3 red blue           effect #3 with red and blue\n"
                     "  openxidon light static orange        solid orange\n"
                     "  openxidon light next / prev          cycle through the effects\n"
                     "  openxidon light all neon speed=5     apply to all three modes\n"
                     "  openxidon light off\n\nColor names:";
        for (const auto& name : colorNames()) {
            std::cout << ' ' << name;
        }
        std::cout << " (or any RRGGBB hex)\n";
        return 0;
    }

    static bool isModeToken(const std::string& text)
    {
        std::string lowered = toLower(text);
        return lowered == "all" || lowered == "1" || lowered == "2" || lowered == "3";
    }

    static bool isEffectToken(const std::string& text)
    {
        std::string lowered = toLower(text);
        return lowered == "next" || lowered == "prev" || effectFromNameOrNumber(text).has_value();
    }

    static Effect cycleEffect(Effect current, int step)
    {
        constexpr int first = static_cast<int>(Effect::RainbowWave);
        constexpr int last = static_cast<int>(Effect::Streaming);
        int value = static_cast<int>(current);
        if (value < first || value > last) {
            return step > 0 ? Effect::RainbowWave : Effect::Streaming;
        }
        value += step;
        if (value > last) {
            value = first;
        } else if (value < first) {
            value = last;
        }
        return static_cast<Effect>(value);
    }

    static Lighting startFrom(Effect effect, const Lighting& current)
    {
        if (effect == current.effect) {
            return current;
        }
        auto currentInfo = effectInfo(current.effect);
        Lighting lighting;
        lighting.effect = effect;
        lighting.speed = currentInfo && currentInfo->hasSpeed ? current.speed : 2;
        lighting.brightness = currentInfo && currentInfo->hasBrightness ? current.brightness : 2;
        auto info = effectInfo(effect);
        if (info && info->maxColors > 0) {
            for (const auto& color : current.colors) {
                if (lighting.colors.size() < info->maxColors) {
                    lighting.colors.push_back(color);
                }
            }
            if (lighting.colors.empty()) {
                lighting.colors.push_back({255, 0, 0});
            }
        }
        return lighting;
    }

    static std::vector<Rgb> parseColorList(const std::string& text)
    {
        std::vector<Rgb> colors;
        for (const auto& part : split(text, ',')) {
            colors.push_back(parseColor(part));
        }
        return colors;
    }

    static std::string formatColorList(const std::vector<Rgb>& colors)
    {
        std::string text;
        for (std::size_t i = 0; i < colors.size(); ++i) {
            text += (i ? "," : "") + formatRgb(colors[i]);
        }
        return text;
    }

    static void applyLightOptions(Lighting& lighting, const std::vector<std::string>& options)
    {
        bool colorsReplaced = false;
        for (const auto& option : options) {
            auto eq = option.find('=');
            if (eq == std::string::npos) {
                Rgb color = parseColor(option);
                if (!colorsReplaced) {
                    lighting.colors.clear();
                    colorsReplaced = true;
                }
                lighting.colors.push_back(color);
                continue;
            }
            const std::string key = toLower(option.substr(0, eq));
            const std::string value = option.substr(eq + 1);
            if (key == "speed") {
                lighting.speed = static_cast<int>(parseInteger(value, 0, kMaxSpeed, "speed"));
            } else if (key == "brightness") {
                lighting.brightness = static_cast<int>(parseInteger(value, 0, kMaxBrightness, "brightness"));
            } else if (key == "direction") {
                lighting.direction = parseInteger(value, 0, 1, "direction") == 1;
            } else if (key == "colors" || key == "color") {
                lighting.colors = parseColorList(value);
                colorsReplaced = true;
            } else {
                throw UsageError("unknown light option '" + key + "'");
            }
        }
    }

    int lightInteractive()
    {
        int active = mouse().activeMode();
        Lighting current = mouse().readGeneral(active).lighting();
        std::cout << "Active mode " << active << " is showing: " << describeLighting(current) << "\n\n";
        printEffectTable();
        std::cout << '\n';
        Effect effect = promptUntilValid("Effect (number or name)", "", [](const std::string& answer) {
            auto parsed = effectFromNameOrNumber(answer);
            if (!parsed) {
                throw std::invalid_argument("pick a number from the list");
            }
            return *parsed;
        });
        auto info = *effectInfo(effect);
        std::cout << "Selected: " << info.displayName << '\n';

        Lighting lighting = startFrom(effect, current);
        if (info.hasSpeed) {
            lighting.speed = promptUntilValid("Speed 0-" + std::to_string(kMaxSpeed), std::to_string(lighting.speed),
                [](const std::string& answer) { return static_cast<int>(parseInteger(answer, 0, kMaxSpeed, "speed")); });
        }
        if (info.hasBrightness) {
            lighting.brightness = promptUntilValid("Brightness 0-" + std::to_string(kMaxBrightness),
                std::to_string(lighting.brightness), [](const std::string& answer) {
                    return static_cast<int>(parseInteger(answer, 0, kMaxBrightness, "brightness"));
                });
        }
        if (info.hasDirection) {
            lighting.direction = promptUntilValid("Direction 0 or 1", lighting.direction ? "1" : "0",
                [](const std::string& answer) { return parseInteger(answer, 0, 1, "direction") == 1; });
        }
        if (info.maxColors > 0) {
            std::string question = "Colors (RRGGBB or names like red, blue, orange; comma separated; up to "
                + std::to_string(info.maxColors) + ")";
            lighting.colors = promptUntilValid(question, formatColorList(lighting.colors),
                [&info](const std::string& answer) {
                    auto colors = parseColorList(answer);
                    if (colors.size() > info.maxColors) {
                        throw std::invalid_argument("at most " + std::to_string(info.maxColors) + " colors");
                    }
                    return colors;
                });
        }
        auto modes = promptUntilValid("Apply to mode 1, 2, 3 or all", std::to_string(active),
            [](const std::string& answer) { return parseModes(answer); });

        std::cout << "Applying: " << describeLighting(lighting) << '\n';
        modifyGeneral(modes, [&lighting](GeneralBlock& block) { block.setLighting(lighting); });
        return 0;
    }

    int light(const std::vector<std::string>& args)
    {
        if (args.empty()) {
            return lightInteractive();
        }
        std::vector<int> modes;
        std::size_t index = 0;
        if (args.size() >= 2 && isModeToken(args[0]) && isEffectToken(args[1])) {
            modes = parseModes(args[0]);
            index = 1;
        }
        const std::string effectText = args[index];
        if (!isEffectToken(effectText)) {
            throw std::invalid_argument("unknown effect '" + effectText + "' (run 'openxidon effects' to see the list)");
        }
        std::vector<std::string> options(args.begin() + static_cast<long>(index) + 1, args.end());
        if (modes.empty()) {
            int active = mouse().activeMode();
            modes = {active};
            std::cout << "Using active mode " << active << '\n';
        }

        modifyGeneral(modes, [&effectText, &options](GeneralBlock& block) {
            Lighting current = block.lighting();
            std::string lowered = toLower(effectText);
            Effect effect = lowered == "next" ? cycleEffect(current.effect, 1)
                : lowered == "prev"           ? cycleEffect(current.effect, -1)
                                              : *effectFromNameOrNumber(effectText);
            Lighting lighting = startFrom(effect, current);
            applyLightOptions(lighting, options);
            block.setLighting(lighting);
            std::cout << "  " << describeLighting(lighting) << '\n';
        });
        return 0;
    }

    int buttons(const std::vector<std::string>& args)
    {
        requireArgs(args, 1);
        auto modes = parseModes(args[0]);
        ButtonMatrix matrix = ButtonMatrix::defaults();
        for (std::size_t i = 1; i < args.size(); ++i) {
            auto eq = args[i].find('=');
            if (eq == std::string::npos) {
                throw UsageError("expected <slot>=<function>, got '" + args[i] + "'");
            }
            int slot = static_cast<int>(parseInteger(args[i].substr(0, eq), 1, kButtonSlotCount, "button slot"));
            matrix.slots[slot - 1] = parseButtonFunction(args[i].substr(eq + 1));
        }
        std::cout << "Button map (the mouse cannot report its current map, unspecified slots use defaults):\n";
        for (std::size_t i = 0; i < kButtonSlotCount; ++i) {
            std::printf("  %2zu  0x%08X  %s\n", i + 1, matrix.slots[i], describeButtonCode(matrix.slots[i]).c_str());
        }
        for (int mode : modes) {
            mouse().writeButtons(mode, matrix);
            std::cout << "mode " << mode << " buttons written\n";
        }
        return 0;
    }

    int macro(const std::vector<std::string>& args)
    {
        requireArgs(args, 2);
        int slot = static_cast<int>(parseInteger(args[0], 1, kMacroMaxSlot, "macro slot"));
        Macro parsed;
        if (args[1] == "-") {
            parsed = Macro::parse(std::cin);
        } else {
            std::ifstream file(args[1]);
            if (!file) {
                throw std::runtime_error("cannot open " + args[1]);
            }
            parsed = Macro::parse(file);
        }
        std::cout << "Macro slot " << slot << " (" << parsed.events.size() << " events):\n" << parsed.describe();
        mouse().writeMacro(slot, parsed);
        std::cout << "macro written; bind it with: openxidon buttons <mode> <slot>=macro:" << slot << '\n';
        return 0;
    }

    int listen()
    {
        if (options_.dryRun) {
            std::cout << "listen is not available in dry-run mode\n";
            return 1;
        }
        std::cout << "Listening for mouse events, press Ctrl+C to stop\n";
        while (true) {
            auto event = mouse().waitForEvent(1000);
            if (event) {
                std::cout << "mode=" << event->mode << " dpi_level=" << event->dpiLevel << " dpi=" << event->dpi
                          << "  raw=" << hexBytes(event->raw) << std::endl;
            }
        }
    }

    Options options_;
    std::unique_ptr<Mouse> mouse_;
};

Options parseOptions(int argc, char** argv)
{
    Options options;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (!options.args.empty()) {
            options.args.push_back(arg);
        } else if (arg == "--dry-run") {
            options.dryRun = true;
        } else if (arg == "--force") {
            options.force = true;
        } else if (arg == "--device" || arg == "--delay") {
            if (i + 1 >= argc) {
                throw UsageError(arg + " needs a value");
            }
            std::string value = argv[++i];
            if (arg == "--device") {
                options.devicePath = value;
            } else {
                options.delayMs = static_cast<int>(parseInteger(value, 0, 5000, "delay"));
            }
        } else if (arg == "-h" || arg == "--help") {
            std::cout << kUsage;
            std::exit(0);
        } else if (!arg.empty() && arg[0] == '-' && arg != "-") {
            throw UsageError("unknown option '" + arg + "'");
        } else {
            options.args.push_back(arg);
        }
    }
    return options;
}

}

int main(int argc, char** argv)
{
    try {
        App app(parseOptions(argc, argv));
        return app.run();
    } catch (const UsageError& error) {
        std::cerr << "openxidon: " << error.what() << "\n\n" << kUsage;
        return 2;
    } catch (const std::exception& error) {
        std::cerr << "openxidon: " << error.what() << '\n';
        return 1;
    }
}
