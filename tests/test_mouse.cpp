/*
 * Mouse reporting tests: which modes an application can turn on, and the exact
 * bytes each encoding puts on the wire.
 *
 * The encodings are the interesting part. There are four of them, they differ in
 * small ways that are easy to get subtly wrong (the +32 bias, which one carries
 * a release in its final byte, which one drops the modifier bits), and an
 * application that misreads a report puts the cursor in the wrong place rather
 * than failing loudly. All of it is headless.
 */

#include "check.h"
#include "core/mouse.h"
#include "core/terminal_emulator.h"
#include <string>

namespace {

void feed(TerminalEmulator& term, const std::string& bytes) {
    term.write(bytes.data(), bytes.size());
}

MouseReport press(MouseButton button, int row, int col) {
    MouseReport report;
    report.action = MouseAction::Press;
    report.button = button;
    report.row = row;
    report.col = col;
    return report;
}

/* Escapes a report so a failure message is readable. */
std::string show(const std::string& bytes) {
    std::string out;
    for (const char c : bytes) {
        const auto value = static_cast<unsigned char>(c);
        if (value == 0x1b) {
            out += "ESC";
        } else if (value >= 32 && value < 127) {
            out += c;
        } else {
            static const char* digits = "0123456789abcdef";
            out += "\\x";
            out += digits[value >> 4];
            out += digits[value & 0xf];
        }
    }
    return out;
}

void testModeSetting() {
    check::section("mouse modes are recognised");

    TerminalEmulator term(24, 80);
    check::that(term.mouseTracking() == MouseTracking::None, "reporting starts off");
    check::that(term.mouseEncoding() == MouseEncoding::X10, "and the legacy encoding is the default");

    feed(term, "\x1b[?1000h");
    check::that(term.mouseTracking() == MouseTracking::Normal, "?1000h asks for clicks");

    feed(term, "\x1b[?1002h\x1b[?1006h");
    check::that(term.mouseTracking() == MouseTracking::ButtonEvent, "?1002h adds drag");
    check::that(term.mouseEncoding() == MouseEncoding::Sgr, "?1006h switches to SGR");

    /*
     * Applications routinely enable several tracking modes and then disable them
     * one at a time on the way out. A reset that names a mode which is not the
     * active one must not turn reporting off.
     */
    feed(term, "\x1b[?1003h\x1b[?1002l");
    check::that(term.mouseTracking() == MouseTracking::AnyEvent,
                "disabling a mode that is not in force leaves reporting alone");

    feed(term, "\x1b[?1003l");
    check::that(term.mouseTracking() == MouseTracking::None, "disabling the active mode ends it");
    check::that(term.mouseEncoding() == MouseEncoding::Sgr, "the encoding is independent");

    feed(term, "\x1b[?1006l");
    check::that(term.mouseEncoding() == MouseEncoding::X10, "and reverts when disabled");

    feed(term, "\x1b[?9h");
    check::that(term.mouseTracking() == MouseTracking::X10, "?9h is the X10 compatibility mode");

    check::that(!term.focusEvents(), "focus reporting starts off");
    feed(term, "\x1b[?1004h");
    check::that(term.focusEvents(), "?1004h asks for focus events");

    check::that(term.alternateScroll(), "alternate scroll is on by default");
    feed(term, "\x1b[?1007l");
    check::that(!term.alternateScroll(), "?1007l lets an application turn it off");

    /* A full reset takes all of it away, or a crashed application would leave
     * the terminal reporting to a shell that knows nothing about it. */
    feed(term, "\x1b" "c");
    check::that(term.mouseTracking() == MouseTracking::None, "RIS stops reporting");
    check::that(!term.focusEvents(), "RIS stops focus events");
}

void testSgrEncoding() {
    check::section("SGR encoding (?1006)");

    check::equal(show(encodeMouseReport(press(MouseButton::Left, 0, 0),
                                       MouseTracking::Normal, MouseEncoding::Sgr)),
                 std::string("ESC[<0;1;1M"), "a left press at the top-left cell");

    check::equal(show(encodeMouseReport(press(MouseButton::Right, 9, 19),
                                       MouseTracking::Normal, MouseEncoding::Sgr)),
                 std::string("ESC[<2;20;10M"), "coordinates are 1-based, column first");

    MouseReport release = press(MouseButton::Left, 0, 0);
    release.action = MouseAction::Release;
    check::equal(show(encodeMouseReport(release, MouseTracking::Normal, MouseEncoding::Sgr)),
                 std::string("ESC[<0;1;1m"),
                 "a release keeps its button and is marked by a lowercase final");

    MouseReport modified = press(MouseButton::Left, 0, 0);
    modified.modifiers.shift = true;
    modified.modifiers.control = true;
    check::equal(show(encodeMouseReport(modified, MouseTracking::Normal, MouseEncoding::Sgr)),
                 std::string("ESC[<20;1;1M"), "shift adds 4 and control 16");

    check::equal(show(encodeMouseReport(press(MouseButton::WheelUp, 0, 0),
                                       MouseTracking::Normal, MouseEncoding::Sgr)),
                 std::string("ESC[<64;1;1M"), "the wheel is button 64");
    check::equal(show(encodeMouseReport(press(MouseButton::WheelDown, 0, 0),
                                       MouseTracking::Normal, MouseEncoding::Sgr)),
                 std::string("ESC[<65;1;1M"), "and 65 downwards");

    /* No coordinate ceiling, which is the whole point of this encoding. */
    check::equal(show(encodeMouseReport(press(MouseButton::Left, 299, 499),
                                       MouseTracking::Normal, MouseEncoding::Sgr)),
                 std::string("ESC[<0;500;300M"), "large coordinates are exact");
}

void testLegacyEncodings() {
    check::section("legacy encodings (?1005, ?1015, default)");

    check::equal(show(encodeMouseReport(press(MouseButton::Left, 0, 0),
                                       MouseTracking::Normal, MouseEncoding::X10)),
                 std::string("ESC[M !!"), "X10 biases every field by 32");

    MouseReport release = press(MouseButton::Left, 0, 0);
    release.action = MouseAction::Release;
    check::equal(show(encodeMouseReport(release, MouseTracking::Normal, MouseEncoding::X10)),
                 std::string("ESC[M#!!"),
                 "X10 spells a release as button 3, losing which button it was");

    /* 223 is as far as one byte reaches; clamping keeps the row usable instead
     * of dropping the report. */
    check::equal(show(encodeMouseReport(press(MouseButton::Left, 0, 299),
                                       MouseTracking::Normal, MouseEncoding::X10)),
                 std::string("ESC[M \\xff!"), "a coordinate past 223 is clamped");

    check::equal(show(encodeMouseReport(press(MouseButton::Left, 0, 299),
                                       MouseTracking::Normal, MouseEncoding::Utf8)),
                 std::string("ESC[M \\xc5\\x8c!"),
                 "the UTF-8 encoding spends two bytes instead of clamping");

    check::equal(show(encodeMouseReport(press(MouseButton::Middle, 9, 19),
                                       MouseTracking::Normal, MouseEncoding::Urxvt)),
                 std::string("ESC[33;20;10M"), "urxvt is decimal but keeps the +32 bias");

    /* Only SGR can say which button was released; the others inherit X10's
     * button-3 spelling, and getting that wrong makes a release look like a
     * left-button press. */
    check::equal(show(encodeMouseReport(release, MouseTracking::Normal, MouseEncoding::Urxvt)),
                 std::string("ESC[35;1;1M"), "an urxvt release is button 3 as well");
    check::equal(show(encodeMouseReport(release, MouseTracking::Normal, MouseEncoding::Utf8)),
                 std::string("ESC[M#!!"), "as is a UTF-8 one");
}

void testUnreportableEvents() {
    check::section("events that must not be reported");

    check::equal(encodeMouseReport(press(MouseButton::Left, 0, 0),
                                   MouseTracking::None, MouseEncoding::Sgr),
                 std::string(), "nothing at all when the application did not ask");

    MouseReport release = press(MouseButton::Left, 0, 0);
    release.action = MouseAction::Release;
    check::equal(encodeMouseReport(release, MouseTracking::X10, MouseEncoding::Sgr),
                 std::string(), "X10 tracking reports presses only");

    MouseReport wheelRelease = press(MouseButton::WheelUp, 0, 0);
    wheelRelease.action = MouseAction::Release;
    check::equal(encodeMouseReport(wheelRelease, MouseTracking::Normal, MouseEncoding::Sgr),
                 std::string(), "a wheel notch has no release to report");

    MouseReport modified = press(MouseButton::Left, 0, 0);
    modified.modifiers.control = true;
    check::equal(show(encodeMouseReport(modified, MouseTracking::X10, MouseEncoding::X10)),
                 std::string("ESC[M !!"), "X10 tracking predates modifier reporting");

    MouseReport motion = press(MouseButton::None, 4, 4);
    motion.action = MouseAction::Move;
    check::equal(encodeMouseReport(motion, MouseTracking::Normal, MouseEncoding::Sgr),
                 std::string(), "?1000 does not want motion");
    check::equal(encodeMouseReport(motion, MouseTracking::ButtonEvent, MouseEncoding::Sgr),
                 std::string(), "?1002 wants motion only while a button is held");
    check::equal(show(encodeMouseReport(motion, MouseTracking::AnyEvent, MouseEncoding::Sgr)),
                 std::string("ESC[<35;5;5M"),
                 "?1003 reports motion with no button as 3 plus the motion bit");

    MouseReport drag = press(MouseButton::Left, 4, 4);
    drag.action = MouseAction::Move;
    check::equal(show(encodeMouseReport(drag, MouseTracking::ButtonEvent, MouseEncoding::Sgr)),
                 std::string("ESC[<32;5;5M"), "a drag is the button plus the motion bit");

    MouseReport offGrid = press(MouseButton::Left, -1, 0);
    check::equal(encodeMouseReport(offGrid, MouseTracking::Normal, MouseEncoding::Sgr),
                 std::string(), "a position outside the grid is not reported");
}

} // namespace

int main() {
    testModeSetting();
    testSgrEncoding();
    testLegacyEncodings();
    testUnreportableEvents();
    return check::report("test_mouse");
}
