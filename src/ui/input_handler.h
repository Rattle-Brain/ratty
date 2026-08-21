/*
 * InputHandler - Qt key events to VT input sequences
 *
 * Encodes modifiers the way xterm does (the "1;<mod>" parameter form), so
 * Shift+Arrow, Ctrl+Arrow and friends reach the shell instead of arriving
 * indistinguishable from the unmodified key.
 *
 * Two families of key need more than that form, because a terminal is not the
 * only thing between the keyboard and the shell:
 *
 *   - Word and line editing. Alt+Left, Alt+Backspace, Cmd+Backspace and the
 *     rest are what every native text field on the platform answers to, and a
 *     user expects them at a prompt too. The shell has never heard of them, so
 *     they are translated into the readline / zsh bindings that do the same
 *     job. See encodeEditingKey().
 *
 *   - Layout-composed characters. Option (macOS) and AltGr (Linux) are compose
 *     keys, not Meta: Option+ñ and AltGr+ñ are both `~`. That character has to
 *     be sent as itself, not as ESC ~. See isComposedText().
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
    /* Word- and line-wise editing keys. Null when `key` is not one of them. */
    static QByteArray encodeEditingKey(int key, Qt::KeyboardModifiers modifiers);
    /* True when the keyboard layout, not the Alt modifier, produced `text`. */
    static bool isComposedText(int key, const QString& text);

    QHash<int, KeyEncoding> specialKeys_;
};

#endif /* UI_INPUT_HANDLER_H */
