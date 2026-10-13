#include "openxidon/macro.hpp"

#include "openxidon/keys.hpp"
#include "openxidon/protocol.hpp"
#include "openxidon/util.hpp"

#include <sstream>
#include <stdexcept>

namespace openxidon {

namespace {

constexpr int kDefaultDelayMs = 10;

bool hasPressFlag(MacroEventType type)
{
    return type == MacroEventType::MouseButton || type == MacroEventType::Key
        || type == MacroEventType::Modifier;
}

std::uint8_t toMacroModifier(std::uint8_t mask)
{
    return static_cast<std::uint8_t>((mask | (mask >> 4)) & 0x0F);
}

MacroEvent parseTarget(const std::string& target, bool press)
{
    MacroEvent event;
    event.press = press;
    auto colon = target.find(':');
    if (colon != std::string::npos && toLower(target.substr(0, colon)) == "mouse") {
        auto button = mouseButtonFromName(target.substr(colon + 1));
        if (!button) {
            throw std::invalid_argument("unknown mouse button '" + target + "'");
        }
        event.type = MacroEventType::MouseButton;
        event.code = *button;
        return event;
    }
    if (auto bit = modifierFromName(target)) {
        event.type = MacroEventType::Modifier;
        event.code = toMacroModifier(*bit);
        return event;
    }
    auto usage = keyUsageFromName(target);
    if (!usage) {
        throw std::invalid_argument("unknown key '" + target + "'");
    }
    event.type = MacroEventType::Key;
    event.code = *usage;
    return event;
}

int parseDelay(std::istringstream& stream)
{
    std::string token;
    if (!(stream >> token)) {
        return kDefaultDelayMs;
    }
    return static_cast<int>(parseInteger(token, 0, kMacroMaxDelayMs, "delay"));
}

}

std::vector<std::uint8_t> Macro::encode() const
{
    if (events.size() > kMacroMaxEvents) {
        throw std::invalid_argument("macro has " + std::to_string(events.size())
            + " events, maximum is " + std::to_string(kMacroMaxEvents));
    }
    std::vector<std::uint8_t> bytes;
    bytes.push_back(static_cast<std::uint8_t>(events.size() >> 8));
    bytes.push_back(static_cast<std::uint8_t>(events.size() & 0xFF));
    for (const auto& event : events) {
        if (event.delayMs < 0 || event.delayMs > kMacroMaxDelayMs) {
            throw std::invalid_argument("macro delay must be between 0 and " + std::to_string(kMacroMaxDelayMs));
        }
        auto head = static_cast<std::uint8_t>((static_cast<std::uint8_t>(event.type) << 4)
            | ((event.delayMs >> 8) & 0x0F));
        if (event.press && hasPressFlag(event.type)) {
            head |= 0x80;
        }
        auto low = static_cast<std::uint8_t>(event.delayMs & 0xFF);
        bytes.push_back(head);
        bytes.push_back(low == 0 ? 1 : low);
        bytes.push_back(event.code);
    }
    return bytes;
}

std::string Macro::describe() const
{
    std::ostringstream out;
    for (const auto& event : events) {
        switch (event.type) {
        case MacroEventType::MouseButton:
            out << (event.press ? "down" : "up") << " mouse:" << mouseButtonToString(event.code);
            break;
        case MacroEventType::Key:
            out << (event.press ? "down" : "up") << ' ' << keyNameFromUsage(event.code);
            break;
        case MacroEventType::Modifier:
            out << (event.press ? "down" : "up") << ' ' << modifierMaskToString(event.code);
            break;
        case MacroEventType::MoveX:
            out << "move x " << static_cast<int>(static_cast<std::int8_t>(event.code));
            break;
        case MacroEventType::MoveY:
            out << "move y " << static_cast<int>(static_cast<std::int8_t>(event.code));
            break;
        case MacroEventType::Wheel:
            out << "wheel " << static_cast<int>(static_cast<std::int8_t>(event.code));
            break;
        }
        out << ' ' << event.delayMs << '\n';
    }
    return out.str();
}

Macro Macro::parse(std::istream& input)
{
    Macro macro;
    std::string line;
    int lineNumber = 0;
    while (std::getline(input, line)) {
        ++lineNumber;
        auto hash = line.find('#');
        if (hash != std::string::npos) {
            line.erase(hash);
        }
        std::istringstream stream(line);
        std::string verb;
        if (!(stream >> verb)) {
            continue;
        }
        verb = toLower(verb);
        try {
            if (verb == "down" || verb == "up" || verb == "tap") {
                std::string target;
                if (!(stream >> target)) {
                    throw std::invalid_argument("missing key or button");
                }
                int delay = parseDelay(stream);
                if (verb == "tap") {
                    MacroEvent down = parseTarget(target, true);
                    down.delayMs = delay;
                    MacroEvent up = parseTarget(target, false);
                    up.delayMs = delay;
                    macro.events.push_back(down);
                    macro.events.push_back(up);
                } else {
                    MacroEvent event = parseTarget(target, verb == "down");
                    event.delayMs = delay;
                    macro.events.push_back(event);
                }
            } else if (verb == "move") {
                std::string axis;
                std::string amount;
                if (!(stream >> axis >> amount)) {
                    throw std::invalid_argument("move syntax is: move <x|y> <amount> [delay]");
                }
                axis = toLower(axis);
                if (axis != "x" && axis != "y") {
                    throw std::invalid_argument("move axis must be x or y");
                }
                MacroEvent event;
                event.type = axis == "x" ? MacroEventType::MoveX : MacroEventType::MoveY;
                event.press = false;
                event.code = static_cast<std::uint8_t>(static_cast<std::int8_t>(parseInteger(amount, -128, 127, "move amount")));
                event.delayMs = parseDelay(stream);
                macro.events.push_back(event);
            } else if (verb == "wheel") {
                std::string amount;
                if (!(stream >> amount)) {
                    throw std::invalid_argument("wheel syntax is: wheel <amount> [delay]");
                }
                MacroEvent event;
                event.type = MacroEventType::Wheel;
                event.press = false;
                event.code = static_cast<std::uint8_t>(static_cast<std::int8_t>(parseInteger(amount, -128, 127, "wheel amount")));
                event.delayMs = parseDelay(stream);
                macro.events.push_back(event);
            } else {
                throw std::invalid_argument("unknown action '" + verb + "'");
            }
        } catch (const std::invalid_argument& error) {
            throw std::invalid_argument("line " + std::to_string(lineNumber) + ": " + error.what());
        }
    }
    if (macro.events.empty()) {
        throw std::invalid_argument("macro is empty");
    }
    return macro;
}

}
