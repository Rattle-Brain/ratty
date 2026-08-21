/*
 * Keybinding and key-encoding tests.
 *
 * Two things are easy to get silently wrong here. First, the spelling used in
 * config.json has to produce the same QKeySequence that a real key event does,
 * or a binding simply never fires. Second, shortcuts must not shadow the
 * control characters the shell needs.
 */

#include "check.h"
#include "config/config.h"
#include "ui/input_handler.h"
#include <QDir>
#include <QGuiApplication>
#include <QKeyCombination>
#include <QKeyEvent>
#include <QTemporaryDir>
#include <QTextStream>
#include <vector>

namespace {

QTemporaryDir* sandbox = nullptr;

/*
 * Force one keybinding set, whichever platform the suite happens to run on.
 * Without this the assertions would depend on the host: `mac_os_bindings`
 * defaults to the platform, so the Ctrl+Shift set is inactive on macOS.
 */
void loadBindingSet(bool macOs) {
    const QString configDir = sandbox->path() + QStringLiteral("/.config/ratty");
    QDir().mkpath(configDir);
    QFile file(configDir + QStringLiteral("/config.yaml"));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        check::that(false, "could not write the sandbox config");
        return;
    }
    QTextStream(&file) << (macOs ? QStringLiteral("mac_os_bindings: true\n")
                                 : QStringLiteral("mac_os_bindings: false\n"));
    file.close();
    Config::instance().load();
}

void expectAction(Qt::KeyboardModifiers modifiers, Qt::Key key, Action expected,
                  const char* label) {
    const QKeyEvent event(QEvent::KeyPress, key, modifiers);
    const Action actual = Config::instance().lookupAction(&event);
    check::that(actual == expected,
                QStringLiteral("%1 -> %2")
                    .arg(QString::fromLatin1(label), Config::actionToString(actual))
                    .toStdString());
}

/* `expected` is a QByteArray rather than a const char* so that sequences
 * containing a NUL (Ctrl+Space) are not silently truncated. */
void expectBytes(Qt::KeyboardModifiers modifiers, Qt::Key key, const QString& text,
                 bool applicationCursorKeys, const QByteArray& expected, const char* label) {
    const InputHandler handler;
    QKeyEvent event(QEvent::KeyPress, key, modifiers, text);
    const QByteArray actual = handler.keyEventToBytes(&event, applicationCursorKeys);
    check::that(actual == expected,
                QStringLiteral("%1 -> %2")
                    .arg(QString::fromLatin1(label),
                         QString::fromLatin1(actual.toHex(' ')))
                    .toStdString());
}

void testBindingsResolve() {
    check::section("default keybindings resolve against real key events");

    loadBindingSet(/*macOs=*/false);

    /*
     * `cmd` and `super` are both Qt::MetaModifier, so these are the bindings
     * whichever platform file is loaded.
     */
    const auto meta = Qt::MetaModifier;
    const auto ctrlShift = Qt::ControlModifier | Qt::ShiftModifier;

    /* Tabs live on the Meta key. */
    expectAction(meta, Qt::Key_T, ACTION_NEW_TAB, "cmd/super+t");
    expectAction(meta, Qt::Key_W, ACTION_CLOSE_TAB, "cmd/super+w");
    expectAction(meta, Qt::Key_H, ACTION_PREV_TAB, "cmd/super+h (tab left)");
    expectAction(meta, Qt::Key_L, ACTION_NEXT_TAB, "cmd/super+l (tab right)");
    expectAction(meta, Qt::Key_1, ACTION_GOTO_TAB_1, "cmd/super+1");
    expectAction(meta, Qt::Key_9, ACTION_GOTO_TAB_9, "cmd/super+9");

    /* Splits live on Ctrl+Shift. */
    expectAction(ctrlShift, Qt::Key_V, ACTION_SPLIT_VERTICAL, "ctrl+shift+v (split vertical)");
    expectAction(ctrlShift, Qt::Key_W, ACTION_SPLIT_HORIZONTAL, "ctrl+shift+w (split horizontal)");
    expectAction(ctrlShift, Qt::Key_C, ACTION_CLOSE_SPLIT, "ctrl+shift+c (close split)");

    /* Pane navigation is the vim direction keys. */
    expectAction(ctrlShift, Qt::Key_H, ACTION_FOCUS_LEFT, "ctrl+shift+h");
    expectAction(ctrlShift, Qt::Key_J, ACTION_FOCUS_DOWN, "ctrl+shift+j");
    expectAction(ctrlShift, Qt::Key_K, ACTION_FOCUS_UP, "ctrl+shift+k");
    expectAction(ctrlShift, Qt::Key_L, ACTION_FOCUS_RIGHT, "ctrl+shift+l");

    /* Clipboard, window and scrollback. */
    expectAction(meta, Qt::Key_C, ACTION_COPY, "cmd/super+c");
    expectAction(meta, Qt::Key_V, ACTION_PASTE, "cmd/super+v");
    expectAction(meta, Qt::Key_Q, ACTION_QUIT, "cmd/super+q");
    expectAction(meta, Qt::Key_K, ACTION_CLEAR_SCROLLBACK, "cmd/super+k");
    expectAction(Qt::NoModifier, Qt::Key_F11, ACTION_FULLSCREEN, "f11");
    expectAction(Qt::ShiftModifier, Qt::Key_PageUp, ACTION_SCROLL_UP, "shift+pageup");

    /* Font size, through every key event a user might produce. */
    expectAction(meta, Qt::Key_Equal, ACTION_INCREASE_FONT_SIZE, "cmd/super+=");
    expectAction(meta | Qt::ShiftModifier, Qt::Key_Plus, ACTION_INCREASE_FONT_SIZE,
                 "cmd/super+shift+= typing '+'");
    expectAction(meta, Qt::Key_Minus, ACTION_DECREASE_FONT_SIZE, "cmd/super+-");
    expectAction(meta, Qt::Key_0, ACTION_RESET_FONT_SIZE, "cmd/super+0");
}

/* The same binding must fire whether the layout reports the digit or the
 * shifted symbol for a physical key. */
void expectEventAction(Qt::KeyboardModifiers modifiers, Qt::Key key, Action expected,
                       const char* label) {
    QKeyEvent event(QEvent::KeyPress, key, modifiers);
    const Action actual = Config::instance().lookupAction(&event);
    check::that(actual == expected,
                QStringLiteral("%1 -> %2")
                    .arg(QString::fromLatin1(label), Config::actionToString(actual))
                    .toStdString());
}

void testLayoutTolerance() {
    check::section("keyboard-layout tolerance for shifted keys");

    loadBindingSet(/*macOs=*/false);
    const auto meta = Qt::MetaModifier;
    const auto metaShift = Qt::MetaModifier | Qt::ShiftModifier;

    /* Qt reports either the digit or the shifted symbol for the same physical
     * key, depending on platform and layout; both must reach the binding. */
    expectEventAction(meta, Qt::Key_1, ACTION_GOTO_TAB_1, "Key_1");
    expectEventAction(metaShift, Qt::Key_Exclam, ACTION_GOTO_TAB_1, "Key_Exclam (Shift+1)");
    expectEventAction(meta, Qt::Key_0, ACTION_RESET_FONT_SIZE, "Key_0");
    expectEventAction(metaShift, Qt::Key_ParenRight, ACTION_RESET_FONT_SIZE,
                      "Key_ParenRight (Shift+0)");
    expectEventAction(meta, Qt::Key_Minus, ACTION_DECREASE_FONT_SIZE, "Key_Minus");
    expectEventAction(metaShift, Qt::Key_Underscore, ACTION_DECREASE_FONT_SIZE,
                      "Key_Underscore (Shift+-)");
    expectEventAction(meta, Qt::Key_Plus, ACTION_INCREASE_FONT_SIZE, "Key_Plus");
    expectEventAction(metaShift, Qt::Key_Equal, ACTION_INCREASE_FONT_SIZE,
                      "Key_Equal with Shift");

    /*
     * On layouts where the digits are the shifted symbols, typing cmd+1 has to
     * hold Shift down, so the lookup falls back to ignoring it.
     */
    expectEventAction(metaShift, Qt::Key_1, ACTION_GOTO_TAB_1,
                      "Key_1 with Shift (AZERTY and friends)");
    expectEventAction(metaShift, Qt::Key_0, ACTION_RESET_FONT_SIZE, "Key_0 with Shift");

    /*
     * That fallback must never reach a letter, or Ctrl+Shift+C would decay into
     * Ctrl+C and steal the shell's interrupt.
     */
    expectEventAction(Qt::ControlModifier, Qt::Key_C, ACTION_NONE, "ctrl+c still unbound");
    expectEventAction(Qt::ControlModifier, Qt::Key_1, ACTION_NONE, "ctrl+1 still unbound");
    expectEventAction(Qt::ControlModifier, Qt::Key_W, ACTION_NONE, "ctrl+w still unbound");
    expectEventAction(Qt::ControlModifier, Qt::Key_V, ACTION_NONE, "ctrl+v still unbound");
    expectEventAction(Qt::MetaModifier | Qt::ShiftModifier, Qt::Key_C, ACTION_NONE,
                      "meta+shift+c does not decay to meta+c (copy)");
}

void testPlatformSetsAreEquivalent() {
    check::section("the macOS and Linux default sets are equivalent");

    /*
     * The two files differ only in whether the Meta modifier is spelled `cmd` or
     * `super`, which is what a reader of each platform expects to see. Qt maps
     * both to Qt::MetaModifier, so the resolved bindings must be identical --
     * asserting it here is what stops the two files drifting apart.
     */
    struct Probe { Qt::KeyboardModifiers modifiers; Qt::Key key; const char* label; };
    const auto meta = Qt::MetaModifier;
    const auto ctrlShift = Qt::ControlModifier | Qt::ShiftModifier;
    const Probe probes[] = {
        {meta, Qt::Key_T, "meta+t"},   {meta, Qt::Key_W, "meta+w"},
        {meta, Qt::Key_Q, "meta+q"},   {meta, Qt::Key_H, "meta+h"},
        {meta, Qt::Key_L, "meta+l"},   {meta, Qt::Key_C, "meta+c"},
        {meta, Qt::Key_V, "meta+v"},   {meta, Qt::Key_K, "meta+k"},
        {meta, Qt::Key_0, "meta+0"},   {meta, Qt::Key_5, "meta+5"},
        {ctrlShift, Qt::Key_W, "ctrl+shift+w"},
        {ctrlShift, Qt::Key_V, "ctrl+shift+v"},
        {ctrlShift, Qt::Key_C, "ctrl+shift+c"},
        {ctrlShift, Qt::Key_H, "ctrl+shift+h"},
        {ctrlShift, Qt::Key_J, "ctrl+shift+j"},
        {ctrlShift, Qt::Key_K, "ctrl+shift+k"},
        {ctrlShift, Qt::Key_L, "ctrl+shift+l"},
        {Qt::NoModifier, Qt::Key_F11, "f11"},
        {Qt::ControlModifier, Qt::Key_C, "ctrl+c"},
        {Qt::ControlModifier, Qt::Key_D, "ctrl+d"},
    };

    loadBindingSet(/*macOs=*/true);
    const int macOsCount = Config::instance().keybindingCount();
    std::vector<Action> macOsActions;
    for (const Probe& probe : probes) {
        const QKeyEvent event(QEvent::KeyPress, probe.key, probe.modifiers);
        macOsActions.push_back(Config::instance().lookupAction(&event));
    }

    loadBindingSet(/*macOs=*/false);
    check::equal(Config::instance().keybindingCount(), macOsCount,
                 "both sets bind the same number of keys");

    size_t index = 0;
    for (const Probe& probe : probes) {
        const QKeyEvent event(QEvent::KeyPress, probe.key, probe.modifiers);
        const Action linuxAction = Config::instance().lookupAction(&event);
        check::that(linuxAction == macOsActions[index],
                    std::string(probe.label) + " means the same in both sets ("
                        + Config::actionToString(linuxAction).toStdString() + ")");
        ++index;
    }
}

void testForcingTheMacOsSet() {
    check::section("the macOS set can be forced on any platform");

    /* Point 3 of the request: a Mac keyboard on a Linux machine. */
    loadBindingSet(/*macOs=*/true);
    check::that(Config::instance().macOsBindings(), "the macOS set is active when forced");
    check::that(Config::instance().keybindingCount() > 0, "and it has bindings");

    loadBindingSet(/*macOs=*/false);
    check::that(!Config::instance().macOsBindings(), "and can be forced off again");
    check::that(Config::instance().keybindingCount() > 0, "with bindings either way");
}

void testShellKeysAreNotStolen() {
    check::section("shell control keys stay unbound");

    loadBindingSet(/*macOs=*/false);

    for (const auto& [key, label] : {
             std::pair<Qt::Key, const char*>{Qt::Key_C, "ctrl+c (SIGINT)"},
             {Qt::Key_D, "ctrl+d (EOF)"},
             {Qt::Key_W, "ctrl+w (kill word)"},
             {Qt::Key_R, "ctrl+r (history search)"},
             {Qt::Key_Z, "ctrl+z (suspend)"},
             {Qt::Key_L, "ctrl+l (clear)"},
             {Qt::Key_A, "ctrl+a (line start)"},
             {Qt::Key_E, "ctrl+e (line end)"},
             {Qt::Key_U, "ctrl+u (kill line)"},
         }) {
        expectAction(Qt::ControlModifier, key, ACTION_NONE, label);
    }
    expectAction(Qt::NoModifier, Qt::Key_Tab, ACTION_NONE, "tab (completion)");
}

/*
 * The word- and line-wise editing keys. A terminal that forwards the literal
 * xterm form for these (CSI 1;3D for Alt+Left) leaves them doing nothing at a
 * prompt, because a default bash or zsh binds none of it.
 */
void testWordAndLineEditing() {
    check::section("word and line editing keys reach the shell as bindings it has");

    const auto alt = Qt::AltModifier;
    const auto meta = Qt::MetaModifier;

    /* Alt / Option: a word at a time. */
    expectBytes(alt, Qt::Key_Left, QString(), false, "\x1b" "b",
                "Alt+Left -> ESC b (backward-word)");
    expectBytes(alt, Qt::Key_Right, QString(), false, "\x1b" "f",
                "Alt+Right -> ESC f (forward-word)");
    expectBytes(alt, Qt::Key_Backspace, QString(), false, "\x1b" "\x7f",
                "Alt+Backspace -> ESC DEL (backward-kill-word)");
    expectBytes(alt, Qt::Key_Delete, QString(), false, "\x1b" "d",
                "Alt+Delete -> ESC d (kill-word)");

    /* Cmd / Super: the whole line. */
    expectBytes(meta, Qt::Key_Left, QString(), false, "\x01", "Cmd+Left -> Ctrl+A (line start)");
    expectBytes(meta, Qt::Key_Right, QString(), false, "\x05", "Cmd+Right -> Ctrl+E (line end)");
    expectBytes(meta, Qt::Key_Backspace, QString(), false, "\x15",
                "Cmd+Backspace -> Ctrl+U (kill to line start)");
    expectBytes(meta, Qt::Key_Delete, QString(), false, "\x0b",
                "Cmd+Delete -> Ctrl+K (kill to line end)");

    /* Both together is neither, and the plain keys are untouched. */
    expectBytes(alt | meta, Qt::Key_Left, QString(), false, "\x1b[1;11D",
                "Alt+Cmd+Left is not an editing key");
    expectBytes(Qt::NoModifier, Qt::Key_Left, QString(), false, "\x1b[D", "plain Left");
    expectBytes(Qt::NoModifier, Qt::Key_Backspace, QString(), false, "\x7f", "plain Backspace");
    expectBytes(Qt::ShiftModifier, Qt::Key_Left, QString(), false, "\x1b[1;2D",
                "Shift+Left still selects");
}

/*
 * Option (macOS) and AltGr (Linux) are the layout's compose key, not Meta.
 * ESC-prefixing what they produce is what made `~` impossible to type on a
 * Spanish keyboard.
 */
void testComposedCharacters() {
    check::section("layout-composed characters are sent as themselves");

    /*
     * Option+ñ on macOS: Qt reports key() as the character the key carries with
     * no modifiers -- the ñ -- and text() as what the layout composed.
     */
    expectBytes(Qt::AltModifier, Qt::Key_Ntilde, QStringLiteral("~"), false, "~",
                "Option+n-tilde -> ~ (macOS, Spanish layout)");
    /* The same key on Linux, where X11 reports AltGr as Ctrl+Alt. */
    expectBytes(Qt::ControlModifier | Qt::AltModifier, Qt::Key_Ntilde, QStringLiteral("~"), false,
                "~", "AltGr+n-tilde -> ~ (Linux, Spanish layout)");
    /* And on a layout that reports AltGr as its own modifier. */
    expectBytes(Qt::GroupSwitchModifier, Qt::Key_Ntilde, QStringLiteral("~"), false, "~",
                "AltGr+n-tilde -> ~ (GroupSwitch layouts)");

    /* The rest of the third level. */
    expectBytes(Qt::AltModifier, Qt::Key_1, QStringLiteral("|"), false, "|", "Option+1 -> |");
    expectBytes(Qt::AltModifier, Qt::Key_E, QStringLiteral("€"), false, "\xe2\x82\xac",
                "Option+e -> euro sign, as UTF-8");
    expectBytes(Qt::AltModifier, Qt::Key_G, QStringLiteral("@"), false, "@", "Option+g -> @");
    expectBytes(Qt::AltModifier, Qt::Key_Plus, QStringLiteral("]"), false, "]", "Option+plus -> ]");

    /*
     * The other side of the line: when the layout composed nothing, the
     * character agrees with its own key and Alt still means Meta, so readline's
     * word bindings keep working.
     */
    expectBytes(Qt::AltModifier, Qt::Key_F, QStringLiteral("f"), false, "\x1b" "f",
                "Alt+F -> ESC f (forward-word)");
    expectBytes(Qt::AltModifier, Qt::Key_D, QStringLiteral("d"), false, "\x1b" "d",
                "Alt+D -> ESC d (kill-word)");
    expectBytes(Qt::AltModifier, Qt::Key_Period, QStringLiteral("."), false, "\x1b" ".",
                "Alt+. -> ESC . (yank-last-arg)");
    /* Shift only changes the case, which is not a composition. */
    expectBytes(Qt::AltModifier | Qt::ShiftModifier, Qt::Key_F, QStringLiteral("F"), false,
                "\x1b" "F", "Alt+Shift+F -> ESC F");

    /* A dead-key sequence, or anything else the layout resolves to more than
     * one character, is text too. */
    expectBytes(Qt::AltModifier, Qt::Key_N, QStringLiteral("ñ"), false, "\xc3\xb1",
                "a composed accented letter goes through as UTF-8");
}

void testKeyEncoding() {
    check::section("VT input encoding");

    expectBytes(Qt::NoModifier, Qt::Key_Return, QStringLiteral("\r"), false, "\r", "Enter -> CR");
    expectBytes(Qt::NoModifier, Qt::Key_Backspace, QString(), false, "\x7f",
                "Backspace -> DEL");
    expectBytes(Qt::ControlModifier, Qt::Key_Backspace, QString(), false, "\x08",
                "Ctrl+Backspace -> BS");
    expectBytes(Qt::NoModifier, Qt::Key_Escape, QString(), false, "\x1b", "Escape");
    expectBytes(Qt::NoModifier, Qt::Key_Backtab, QString(), false, "\x1b[Z", "Shift+Tab -> CBT");

    expectBytes(Qt::ControlModifier, Qt::Key_C, QStringLiteral("\x03"), false, "\x03", "Ctrl+C");
    expectBytes(Qt::ControlModifier, Qt::Key_Space, QString(), false, QByteArray(1, '\0'),
                "Ctrl+Space -> NUL");

    expectBytes(Qt::NoModifier, Qt::Key_Up, QString(), false, "\x1b[A", "Up (normal mode)");
    expectBytes(Qt::NoModifier, Qt::Key_Up, QString(), true, "\x1bOA", "Up (DECCKM set)");
    expectBytes(Qt::ShiftModifier, Qt::Key_Up, QString(), false, "\x1b[1;2A", "Shift+Up");
    expectBytes(Qt::ControlModifier, Qt::Key_Right, QString(), false, "\x1b[1;5C", "Ctrl+Right");

    expectBytes(Qt::NoModifier, Qt::Key_Delete, QString(), false, "\x1b[3~", "Delete");
    expectBytes(Qt::ControlModifier, Qt::Key_Delete, QString(), false, "\x1b[3;5~", "Ctrl+Delete");
    expectBytes(Qt::NoModifier, Qt::Key_Home, QString(), false, "\x1b[H", "Home");
    expectBytes(Qt::NoModifier, Qt::Key_F1, QString(), false, "\x1b[P", "F1");
    expectBytes(Qt::NoModifier, Qt::Key_F5, QString(), false, "\x1b[15~", "F5");

    expectBytes(Qt::AltModifier, Qt::Key_B, QStringLiteral("b"), false, "\x1b" "b",
                "Alt+B -> ESC b (back word)");
    /* Ctrl+Arrow keeps the xterm form: readline binds it, and Ctrl+Alt is how
     * X11 spells AltGr. */
    expectBytes(Qt::ControlModifier | Qt::AltModifier, Qt::Key_Left, QString(), false,
                "\x1b[1;7D", "Ctrl+Alt+Left keeps the xterm form");
    expectBytes(Qt::NoModifier, Qt::Key_A, QStringLiteral("a"), false, "a", "plain 'a'");
    expectBytes(Qt::NoModifier, Qt::Key_unknown, QStringLiteral("ñ"), false, "\xc3\xb1",
                "non-ASCII text is sent as UTF-8");
}

} // namespace

int main(int argc, char** argv) {
    /* Match the application: Ctrl means Ctrl and Meta means Command everywhere. */
    QCoreApplication::setAttribute(Qt::AA_MacDontSwapCtrlAndMeta, true);
    QGuiApplication app(argc, argv);

    QTemporaryDir tempHome;
    if (!tempHome.isValid()) {
        std::printf("could not create a temporary HOME\n");
        return 1;
    }
    sandbox = &tempHome;
    qputenv("HOME", tempHome.path().toUtf8());

    testBindingsResolve();
    testLayoutTolerance();
    testPlatformSetsAreEquivalent();
    testForcingTheMacOsSet();
    testShellKeysAreNotStolen();
    testKeyEncoding();
    testWordAndLineEditing();
    testComposedCharacters();
    return check::report("test_input");
}
