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

/*
 * Word- and line-wise editing.
 *
 * These are the shortcuts every native text field on the platform has -- Alt /
 * Option for a word, Cmd / Super for the whole line -- and a shell prompt is a
 * text field as far as the user is concerned. The shell itself knows nothing
 * about them, so each one is translated into the readline binding (zsh's emacs
 * mode agrees on all of these) that does the same thing:
 *
 *   Alt+Left / Alt+Right   ESC b / ESC f   backward-word / forward-word
 *   Alt+Backspace          ESC DEL         backward-kill-word
 *   Alt+Delete             ESC d           kill-word (the one ahead)
 *   Cmd+Left / Cmd+Right   Ctrl+A / Ctrl+E line start / line end
 *   Cmd+Backspace          Ctrl+U          kill back to the start of the line
 *   Cmd+Delete             Ctrl+K          kill to the end of the line
 *
 * Sending the literal xterm form instead -- CSI 1;3D for Alt+Left, which is
 * what the table in the constructor produces -- is why these keys used to do
 * nothing: a default bash or zsh binds none of it.
 */
QByteArray InputHandler::encodeEditingKey(int key, Qt::KeyboardModifiers modifiers) {
    /*
     * Ctrl is left out deliberately. Ctrl+Alt is how X11 reports AltGr, and
     * Ctrl+Arrow already has a meaning of its own (CSI 1;5C, which readline
     * does bind).
     */
    if (modifiers & Qt::ControlModifier) return QByteArray();

    const bool word = (modifiers & Qt::AltModifier) && !(modifiers & Qt::MetaModifier);
    const bool line = (modifiers & Qt::MetaModifier) && !(modifiers & Qt::AltModifier);
    if (!word && !line) return QByteArray();

    /* The escape sequences are split across two literals on purpose: "\x1bb"
     * would be read as one hex escape, not ESC followed by 'b'. */
    switch (key) {
    case Qt::Key_Left:      return word ? QByteArray("\x1b" "b")    : QByteArray("\x01");
    case Qt::Key_Right:     return word ? QByteArray("\x1b" "f")    : QByteArray("\x05");
    case Qt::Key_Backspace: return word ? QByteArray("\x1b" "\x7f") : QByteArray("\x15");
    case Qt::Key_Delete:    return word ? QByteArray("\x1b" "d")    : QByteArray("\x0b");
    default:                return QByteArray();
    }
}

/*
 * Whether the keyboard layout composed `text`, rather than Alt merely being
 * held while a key was pressed.
 *
 * On macOS the Option key is the layout's third level and on Linux AltGr is,
 * and both arrive with Qt::AltModifier set. Option+ñ (macOS) and AltGr+ñ
 * (Linux) are both `~`; so are Option+2 for the euro sign, Option+1 for `|`,
 * and every other punctuation mark a non-US layout hides up there. All of it is
 * literal text and must be sent as itself.
 *
 * The tell is that Qt reports `key()` as the character the key carries with no
 * modifiers applied at all -- Key_Ntilde for the ñ key, whatever Option did to
 * it -- so a composed character is one that disagrees with its own key. Alt+B
 * on a US layout agrees ('B' and 'b'), and so still becomes the ESC b that
 * readline binds to backward-word.
 */
bool InputHandler::isComposedText(int key, const QString& text) {
    if (text.isEmpty()) return false;
    /* A multi-character result is a dead key or a surrogate pair; either way it
     * is text. */
    if (text.size() > 1) return true;

    const QChar character = text.at(0);
    /* Control characters come from the Ctrl paths above, never from a layout. */
    if (character.unicode() < 0x20 || character.unicode() == 0x7f) return false;
    /* Key_unknown and the keypad/media range are outside Unicode, so there is
     * no character to disagree with. */
    if (key < 0 || key > 0x10ffff) return true;

    const QChar bare(static_cast<char16_t>(key));
    return bare.toLower() != character.toLower();
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

    /* Ahead of the tables below, which would encode Alt+Left as the CSI form
     * that no shell binds. */
    if (const QByteArray editing = encodeEditingKey(key, modifiers); !editing.isEmpty()) {
        return editing;
    }

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

    if ((modifiers & Qt::AltModifier) && !text.isEmpty()) {
        /* What the layout composed is literal text -- ESC-prefixing it is what
         * made `~` impossible to type on a Spanish keyboard. */
        if (isComposedText(key, text)) return text.toUtf8();

        /* Alt-prefixed input: ESC followed by the unmodified character. */
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
