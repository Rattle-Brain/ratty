/*
 * Platform - the few things that cannot be asked for through Qt
 *
 * Everything here has a real implementation on exactly one platform and a no-op
 * everywhere else, so callers never need an #ifdef. Keep it small: something
 * that belongs in this file is something Qt genuinely does not expose, not
 * something that was easier to write natively.
 */

#ifndef PLATFORM_PLATFORM_H
#define PLATFORM_PLATFORM_H

namespace platform {

/*
 * Make a held key repeat, which on macOS it otherwise does not.
 *
 * macOS has two incompatible readings of "the user is holding a key down":
 * repeat the character, or offer a menu of accented variants ("press and hold").
 * The second wins for any view that takes part in the text input system, and
 * TerminalWidget has to take part -- it is the only way a dead-key `~` or an
 * accent ever arrives (see Qt::WA_InputMethodEnabled in its constructor). The
 * cost of that was key repeat: holding `j` produced one `j`.
 *
 * A terminal wants repeat, unambiguously. Nothing in a shell or a TUI is served
 * by an accent picker, and the diacritics people actually type on a Spanish or
 * French layout come from *dead keys*, which are the input method's business and
 * keep working -- the two mechanisms are independent.
 *
 * Qt exposes no way to say this, so it is said to AppKit directly. Registering
 * the preference rather than writing it keeps the change to this process: no
 * file is created, and a user who has deliberately set the system-wide value
 * still has the last word.
 *
 * Must be called before QApplication is constructed, while AppKit is still
 * reading its defaults.
 */
void enableKeyRepeat();

} // namespace platform

#endif /* PLATFORM_PLATFORM_H */
