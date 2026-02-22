/*
 * InputHandler - Converts Qt key events to VT sequences
 *
 * Maps keyboard input to terminal escape sequences according to VT standards
 */

#ifndef UI_INPUT_HANDLER_H
#define UI_INPUT_HANDLER_H

#include <QKeyEvent>
#include <QByteArray>
#include <QHash>

class InputHandler {
public:
    InputHandler();

    // Convert a Qt key event to VT sequence bytes
    QByteArray keyEventToBytes(QKeyEvent* event) const;

private:
    void initializeKeyMappings();

    // Special key mappings (function keys, arrows, etc.)
    QHash<int, QByteArray> specialKeys_;
};

#endif /* UI_INPUT_HANDLER_H */
