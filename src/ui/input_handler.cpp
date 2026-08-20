/*
 * InputHandler - key event translation implementation
 */

#include "input_handler.h"

InputHandler::InputHandler() {
    /* Cursor keys - these switch to the SS3 form under DECCKM. */
    specialKeys_.insert(Qt::Key_Up,    {"A", true});
    specialKeys_.insert(Qt::Key_Down,  {"B", true});
    specialKeys_.insert(Qt::Key_Right, {"C", true});
    specialKeys_.insert(Qt::Key_Left,  {"D", true});
    specialKeys_.insert(Qt::Key_Home,  {"H", true});
    specialKeys_.insert(Qt::Key_End,   {"F", true});

    /* Editing and navigation keys, in the "CSI <n> ~" family. */
    specialKeys_.insert(Qt::Key_Insert,   {"2~", false});
    specialKeys_.insert(Qt::Key_Delete,   {"3~", false});
    specialKeys_.insert(Qt::Key_PageUp,   {"5~", false});
    specialKeys_.insert(Qt::Key_PageDown, {"6~", false});

    /*
     * Function keys. F1-F4 are the SS3 forms in xterm; the rest use "CSI <n> ~"
     * with the gaps at 16, 22 and 23 that the historical VT220 layout left.
     */
    specialKeys_.insert(Qt::Key_F1,  {"P", true});
    specialKeys_.insert(Qt::Key_F2,  {"Q", true});
    specialKeys_.insert(Qt::Key_F3,  {"R", true});
    specialKeys_.insert(Qt::Key_F4,  {"S", true});
    specialKeys_.insert(Qt::Key_F5,  {"15~", false});
    specialKeys_.insert(Qt::Key_F6,  {"17~", false});
    specialKeys_.insert(Qt::Key_F7,  {"18~", false});
    specialKeys_.insert(Qt::Key_F8,  {"19~", false});
    specialKeys_.insert(Qt::Key_F9,  {"20~", false});
    specialKeys_.insert(Qt::Key_F10, {"21~", false});
    specialKeys_.insert(Qt::Key_F11, {"23~", false});
    specialKeys_.insert(Qt::Key_F12, {"24~", false});
}

int InputHandler::modifierCode(Qt::KeyboardModifiers modifiers) {
    /*
     * xterm's modifier parameter is 1 + a bitmask: shift 1, alt 2, control 4,
     * meta 8. A value of 1 means "no modifiers" and is omitted from the
     * sequence entirely.
     */
    int mask = 0;
    if (modifiers & Qt::ShiftModifier)   mask |= 1;
    if (modifiers & Qt::AltModifier)     mask |= 2;
    if (modifiers & Qt::ControlModifier) mask |= 4;
    if (modifiers & Qt::MetaModifier)    mask |= 8;
    return mask + 1;
}

QByteArray InputHandler::encodeSpecialKey(const KeyEncoding& encoding, int modifierCode,
                                          bool applicationCursorKeys) const {
    const QByteArray suffix(encoding.csiSuffix);
    const bool tildeForm = suffix.endsWith('~');

    if (modifierCode > 1) {
        /* "CSI 1 ; mod <letter>" or "CSI <n> ; mod ~" */
        QByteArray result = "\x1b[";
        if (tildeForm) {
            result += suffix.left(suffix.size() - 1);
        } else {
            result += '1';
        }
        result += ';';
        result += QByteArray::number(modifierCode);
        result += tildeForm ? QByteArray("~") : suffix;
        return result;
    }

    if (!tildeForm && encoding.cursorKey && applicationCursorKeys) {
        return QByteArray("\x1bO") + suffix;
    }
    return QByteArray("\x1b[") + suffix;
}

QByteArray InputHandler::keyEventToBytes(const QKeyEvent* event,
                                         bool applicationCursorKeys) const {
    if (!event) return QByteArray();

    const int key = event->key();
    const Qt::KeyboardModifiers modifiers = event->modifiers();

    if (const auto it = specialKeys_.constFind(key); it != specialKeys_.constEnd()) {
        return encodeSpecialKey(*it, modifierCode(modifiers), applicationCursorKeys);
    }

    switch (key) {
    case Qt::Key_Return:
    case Qt::Key_Enter:
        return QByteArray("\r");
    case Qt::Key_Backspace:
        /* DEL, matching the `stty erase ^?` that every modern Unix defaults to.
         * Ctrl+Backspace conventionally sends BS so readline can bind it. */
        return (modifiers & Qt::ControlModifier) ? QByteArray("\x08") : QByteArray("\x7f");
    case Qt::Key_Tab:
        return QByteArray("\t");
    case Qt::Key_Backtab:
        return QByteArray("\x1b[Z");
    case Qt::Key_Escape:
        return QByteArray("\x1b");
    default:
        break;
    }

    /* Control combinations: map to the C0 range. */
    if ((modifiers & Qt::ControlModifier) && !(modifiers & Qt::AltModifier)) {
        switch (key) {
        case Qt::Key_Space:        return QByteArray(1, '\0');
        case Qt::Key_BracketLeft:  return QByteArray(1, '\x1b');
        case Qt::Key_Backslash:    return QByteArray(1, '\x1c');
        case Qt::Key_BracketRight: return QByteArray(1, '\x1d');
        case Qt::Key_AsciiCircum:  return QByteArray(1, '\x1e');
        case Qt::Key_Underscore:   return QByteArray(1, '\x1f');
        case Qt::Key_Question:     return QByteArray(1, '\x7f');
        default:
            if (key >= Qt::Key_A && key <= Qt::Key_Z) {
                return QByteArray(1, static_cast<char>(key - Qt::Key_A + 1));
            }
            break;
        }
    }

    const QString text = event->text();

    /* Alt-prefixed input: ESC followed by the unmodified character. */
    if ((modifiers & Qt::AltModifier) && !text.isEmpty()) {
        QByteArray result("\x1b");
        result += text.toUtf8();
        return result;
    }

    if (!text.isEmpty()) {
        /*
         * Qt already put the control character in text() for some layouts;
         * anything below 0x20 that reached this point has been handled above,
         * so what remains is real typed text.
         */
        return text.toUtf8();
    }

    return QByteArray();
}
