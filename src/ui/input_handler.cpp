/*
 * InputHandler - VT sequence generation implementation
 */

#include "input_handler.h"
#include <QDebug>

InputHandler::InputHandler() {
    initializeKeyMappings();
}

void InputHandler::initializeKeyMappings() {
    // Arrow keys
    specialKeys_.insert(Qt::Key_Up, "\x1b[A");
    specialKeys_.insert(Qt::Key_Down, "\x1b[B");
    specialKeys_.insert(Qt::Key_Right, "\x1b[C");
    specialKeys_.insert(Qt::Key_Left, "\x1b[D");

    // Home/End
    specialKeys_.insert(Qt::Key_Home, "\x1b[H");
    specialKeys_.insert(Qt::Key_End, "\x1b[F");

    // Page Up/Down
    specialKeys_.insert(Qt::Key_PageUp, "\x1b[5~");
    specialKeys_.insert(Qt::Key_PageDown, "\x1b[6~");

    // Insert/Delete
    specialKeys_.insert(Qt::Key_Insert, "\x1b[2~");
    specialKeys_.insert(Qt::Key_Delete, "\x1b[3~");

    // Function keys F1-F12
    specialKeys_.insert(Qt::Key_F1, "\x1b[11~");
    specialKeys_.insert(Qt::Key_F2, "\x1b[12~");
    specialKeys_.insert(Qt::Key_F3, "\x1b[13~");
    specialKeys_.insert(Qt::Key_F4, "\x1b[14~");
    specialKeys_.insert(Qt::Key_F5, "\x1b[15~");
    specialKeys_.insert(Qt::Key_F6, "\x1b[17~");
    specialKeys_.insert(Qt::Key_F7, "\x1b[18~");
    specialKeys_.insert(Qt::Key_F8, "\x1b[19~");
    specialKeys_.insert(Qt::Key_F9, "\x1b[20~");
    specialKeys_.insert(Qt::Key_F10, "\x1b[21~");
    specialKeys_.insert(Qt::Key_F11, "\x1b[23~");
    specialKeys_.insert(Qt::Key_F12, "\x1b[24~");
}

QByteArray InputHandler::keyEventToBytes(QKeyEvent* event) const {
    if (!event) return QByteArray();

    QByteArray data;
    int key = event->key();
    Qt::KeyboardModifiers mods = event->modifiers();

    // Handle special keys (arrows, function keys, etc.)
    if (specialKeys_.contains(key)) {
        data = specialKeys_.value(key);

        // Apply modifiers to special keys if needed
        // For now, just return the base sequence
        return data;
    }

    // Handle Enter/Return
    if (key == Qt::Key_Return || key == Qt::Key_Enter) {
        return "\r";
    }

    // Handle Backspace
    if (key == Qt::Key_Backspace) {
        return "\x7f";  // DEL character
    }

    // Handle Tab
    if (key == Qt::Key_Tab) {
        return "\t";
    }

    // Handle Escape
    if (key == Qt::Key_Escape) {
        return "\x1b";
    }

    // Handle Ctrl combinations
    if (mods & Qt::ControlModifier) {
        QString text = event->text();
        if (!text.isEmpty()) {
            QChar ch = text[0].toLower();

            // Ctrl+A through Ctrl+Z
            if (ch >= 'a' && ch <= 'z') {
                char controlChar = ch.toLatin1() - 'a' + 1;
                return QByteArray(1, controlChar);
            }

            // Ctrl+Space = NUL
            if (key == Qt::Key_Space) {
                return QByteArray(1, '\0');
            }

            // Ctrl+[ = ESC
            if (key == Qt::Key_BracketLeft) {
                return "\x1b";
            }

            // Ctrl+\ = FS
            if (key == Qt::Key_Backslash) {
                return QByteArray(1, '\x1c');
            }

            // Ctrl+] = GS
            if (key == Qt::Key_BracketRight) {
                return QByteArray(1, '\x1d');
            }

            // Ctrl+^ = RS
            if (key == Qt::Key_AsciiCircum) {
                return QByteArray(1, '\x1e');
            }

            // Ctrl+_ = US
            if (key == Qt::Key_Underscore) {
                return QByteArray(1, '\x1f');
            }
        }
    }

    // Handle Alt combinations (send ESC prefix)
    if (mods & Qt::AltModifier) {
        QString text = event->text();
        if (!text.isEmpty()) {
            QByteArray altData = "\x1b";
            altData.append(text.toUtf8());
            return altData;
        }
    }

    // Regular text input
    QString text = event->text();
    if (!text.isEmpty()) {
        return text.toUtf8();
    }

    // No mapping found
    return QByteArray();
}
