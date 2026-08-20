/*
 * Mouse reporting - encoding implementation
 */

#include "mouse.h"
#include "utf8.h"
#include <algorithm>

namespace {

/*
 * The button field of a report: a small code, plus flag bits for the modifiers,
 * the wheel and motion. This layout is inherited from X10 and every later
 * encoding keeps it -- SGR only changes how the number is written down.
 */
constexpr int kShiftBit = 4;
constexpr int kAltBit = 8;
constexpr int kControlBit = 16;
constexpr int kMotionBit = 32;
constexpr int kWheelBit = 64;

/* X10 puts every field in one byte, biased by 32; a coordinate that does not
 * fit is clamped rather than dropped, so a click near the right edge of a very
 * wide window still lands on the right row. */
constexpr int kX10Offset = 32;
constexpr int kX10MaxCoordinate = 223;

int buttonCode(MouseButton button) {
    switch (button) {
    case MouseButton::Left:       return 0;
    case MouseButton::Middle:     return 1;
    case MouseButton::Right:      return 2;
    case MouseButton::WheelUp:    return kWheelBit + 0;
    case MouseButton::WheelDown:  return kWheelBit + 1;
    case MouseButton::WheelLeft:  return kWheelBit + 2;
    case MouseButton::WheelRight: return kWheelBit + 3;
    case MouseButton::None:       break;
    }
    /* Motion with no button held: X10 spells "no button" as 3, the same code a
     * release uses. */
    return 3;
}

void appendDecimal(std::string& out, int value) {
    out += std::to_string(value);
}

} // namespace

std::string encodeMouseReport(const MouseReport& report, MouseTracking tracking,
                              MouseEncoding encoding) {
    if (tracking == MouseTracking::None) return {};
    if (report.row < 0 || report.col < 0) return {};

    const bool wheel = isWheelButton(report.button);
    const bool held = report.button != MouseButton::None && !wheel;

    switch (report.action) {
    case MouseAction::Press:
        break;
    case MouseAction::Release:
        /* X10 tracking never reports releases, and a wheel notch has none to
         * report -- it arrives as a press and nothing else. */
        if (tracking == MouseTracking::X10 || wheel) return {};
        break;
    case MouseAction::Move:
        if (tracking == MouseTracking::ButtonEvent && !held) return {};
        if (tracking != MouseTracking::ButtonEvent && tracking != MouseTracking::AnyEvent) {
            return {};
        }
        break;
    }

    int code = buttonCode(report.button);

    if (report.action == MouseAction::Move) {
        code += kMotionBit;
    }

    /* X10 tracking predates modifier reporting; sending the bits anyway would
     * make a shift-click unrecognisable to an application that asked for 9. */
    if (tracking != MouseTracking::X10) {
        if (report.modifiers.shift)   code += kShiftBit;
        if (report.modifiers.alt)     code += kAltBit;
        if (report.modifiers.control) code += kControlBit;
    }

    /*
     * Every encoding except SGR inherits X10's inability to say *which* button
     * was released: the field carries the "no button" code 3, with the modifier
     * and motion bits kept.
     */
    if (report.action == MouseAction::Release && encoding != MouseEncoding::Sgr) {
        code = 3 + (code & ~7);
    }

    /* On the wire, coordinates are 1-based. */
    const int x = report.col + 1;
    const int y = report.row + 1;

    std::string out = "\x1b[";

    switch (encoding) {
    case MouseEncoding::Sgr:
        out += '<';
        appendDecimal(out, code);
        out += ';';
        appendDecimal(out, x);
        out += ';';
        appendDecimal(out, y);
        /* The only encoding that distinguishes a release by its final byte
         * instead of by the button code, which is what lets an application tell
         * *which* button was let go. */
        out += (report.action == MouseAction::Release) ? 'm' : 'M';
        return out;

    case MouseEncoding::Urxvt:
        appendDecimal(out, code + kX10Offset);
        out += ';';
        appendDecimal(out, x);
        out += ';';
        appendDecimal(out, y);
        out += 'M';
        return out;

    case MouseEncoding::Utf8: {
        out += 'M';
        out += utf8Encode(std::u32string{static_cast<char32_t>(code + kX10Offset)});
        out += utf8Encode(std::u32string{static_cast<char32_t>(x + kX10Offset)});
        out += utf8Encode(std::u32string{static_cast<char32_t>(y + kX10Offset)});
        return out;
    }

    case MouseEncoding::X10:
        break;
    }

    /* The original form: one byte per field, and no room for a large grid. */
    out += 'M';
    out += static_cast<char>(static_cast<unsigned char>(code + kX10Offset));
    out += static_cast<char>(static_cast<unsigned char>(
        std::min(x, kX10MaxCoordinate) + kX10Offset));
    out += static_cast<char>(static_cast<unsigned char>(
        std::min(y, kX10MaxCoordinate) + kX10Offset));
    return out;
}
