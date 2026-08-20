/*
 * CursorStyle - how the text cursor is drawn
 *
 * Lives in core rather than in the renderer so that Config can express the
 * setting without depending on the OpenGL layer.
 */

#ifndef CORE_CURSOR_H
#define CORE_CURSOR_H

enum class CursorStyle {
    Block,        // filled, translucent so the character stays readable
    HollowBlock,  // outline only (conventional for an unfocused pane)
    Underline,
    Bar,
};

#endif /* CORE_CURSOR_H */
