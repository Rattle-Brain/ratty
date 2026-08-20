/*
 * Mouse reporting - the modes an application can ask for, and the wire format
 *
 * Kept in core/, away from Qt, for the same reason the rest of the terminal
 * model is: the encoding is fiddly, has four spellings, and is far easier to
 * pin down with a headless test than by clicking around in a window.
 *
 * An application enables reporting with DECSET 9/1000/1002/1003 (how much to
 * report) and DECSET 1005/1006/1015 (how to spell it). The two are independent:
 * every tracking mode can be reported in every encoding, which is why they are
 * separate enums rather than one combined mode.
 */

#ifndef CORE_MOUSE_H
#define CORE_MOUSE_H

#include <cstdint>
#include <string>

/*
 * How much the application asked to hear about.
 *
 *   None        nothing; the terminal handles the mouse itself
 *   X10         DECSET 9    presses only, no modifiers, no releases
 *   Normal      DECSET 1000 presses and releases
 *   ButtonEvent DECSET 1002 ... plus motion while a button is held (drag)
 *   AnyEvent    DECSET 1003 ... plus motion with no button held
 */
enum class MouseTracking : uint8_t { None, X10, Normal, ButtonEvent, AnyEvent };

/*
 * How a report is spelled.
 *
 *   X10   the original "CSI M Cb Cx Cy", one byte per field, offset by 32.
 *         Coordinates above 223 do not fit, which is why the others exist.
 *   Utf8  DECSET 1005: the same, with the coordinate bytes as UTF-8.
 *   Sgr   DECSET 1006: "CSI < b ; x ; y M|m", decimal and unbounded. What every
 *         modern application asks for.
 *   Urxvt DECSET 1015: "CSI b ; x ; y M", decimal but with the +32 bias kept.
 */
enum class MouseEncoding : uint8_t { X10, Utf8, Sgr, Urxvt };

enum class MouseButton : uint8_t {
    None,
    Left,
    Middle,
    Right,
    WheelUp,
    WheelDown,
    WheelLeft,
    WheelRight,
};

enum class MouseAction : uint8_t { Press, Release, Move };

struct MouseModifiers {
    bool shift = false;
    bool alt = false;
    bool control = false;
};

/* One event, in 0-based grid coordinates. */
struct MouseReport {
    MouseAction action = MouseAction::Press;
    MouseButton button = MouseButton::None;
    int row = 0;
    int col = 0;
    MouseModifiers modifiers;
};

inline bool isWheelButton(MouseButton button) {
    return button == MouseButton::WheelUp || button == MouseButton::WheelDown
        || button == MouseButton::WheelLeft || button == MouseButton::WheelRight;
}

/*
 * Encode one event, or return an empty string when this event is not reportable
 * in this mode -- a release under X10 tracking, motion nobody asked for, a
 * negative coordinate. Callers can therefore send the result unconditionally.
 */
std::string encodeMouseReport(const MouseReport& report, MouseTracking tracking,
                              MouseEncoding encoding);

#endif /* CORE_MOUSE_H */
