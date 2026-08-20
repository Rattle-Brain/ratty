/*
 * InputHandler - Qt key events to VT input sequences
 *
 * Encodes modifiers the way xterm does (the "1;<mod>" parameter form), so
 * Shift+Arrow, Ctrl+Arrow and friends reach the shell instead of arriving
 * indistinguishable from the unmodified key.
 */

#ifndef UI_INPUT_HANDLER_H
#define UI_INPUT_HANDLER_H

#include <QByteArray>
#include <QHash>
#include <QKeyEvent>

class InputHandler {
public:
    InputHandler();

    /*
     * Translate one key press. `applicationCursorKeys` reflects DECCKM: when an
     * application has enabled it, cursor keys must be sent as SS3 (ESC O A)
     * rather than CSI (ESC [ A), which is what readline and vim key bindings
     * expect.
     *
     * Returns an empty array when the key carries no terminal input (a bare
     * modifier, for instance), so the caller can pass the event on.
     */
    QByteArray keyEventToBytes(const QKeyEvent* event, bool applicationCursorKeys) const;

private:
    /* CSI-style keys: "ESC [ <code> ~" or "ESC [ <letter>". */
    struct KeyEncoding {
        const char* csiSuffix;   // e.g. "A" for Up, "5~" for PageUp
        bool cursorKey;          // eligible for the SS3 form under DECCKM
    };

    QByteArray encodeSpecialKey(const KeyEncoding& encoding, int modifierCode,
                                bool applicationCursorKeys) const;
    static int modifierCode(Qt::KeyboardModifiers modifiers);

    QHash<int, KeyEncoding> specialKeys_;
};

#endif /* UI_INPUT_HANDLER_H */
