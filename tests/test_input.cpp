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
    check::section("Ctrl+Shift keybindings resolve against real key events");

    loadBindingSet(/*macOs=*/false);
    const auto ctrlShift = Qt::ControlModifier | Qt::ShiftModifier;
    expectAction(ctrlShift, Qt::Key_T, ACTION_NEW_TAB, "ctrl+shift+t");
    expectAction(ctrlShift, Qt::Key_W, ACTION_CLOSE_TAB, "ctrl+shift+w");
    expectAction(ctrlShift, Qt::Key_C, ACTION_COPY, "ctrl+shift+c");
    expectAction(ctrlShift, Qt::Key_V, ACTION_PASTE, "ctrl+shift+v");
    expectAction(ctrlShift, Qt::Key_Q, ACTION_QUIT, "ctrl+shift+q");
    expectAction(ctrlShift, Qt::Key_D, ACTION_CLOSE_SPLIT, "ctrl+shift+d");
    expectAction(ctrlShift, Qt::Key_Up, ACTION_FOCUS_UP, "ctrl+shift+up");
    expectAction(ctrlShift, Qt::Key_Right, ACTION_NEXT_TAB, "ctrl+shift+right");
    expectAction(ctrlShift, Qt::Key_E, ACTION_SPLIT_HORIZONTAL, "ctrl+shift+e");
    expectAction(ctrlShift, Qt::Key_Backslash, ACTION_SPLIT_HORIZONTAL, "ctrl+shift+backslash");
    expectAction(ctrlShift, Qt::Key_O, ACTION_SPLIT_VERTICAL, "ctrl+shift+o");
    expectAction(ctrlShift, Qt::Key_Plus, ACTION_INCREASE_FONT_SIZE, "ctrl+shift+plus");
    expectAction(ctrlShift, Qt::Key_Minus, ACTION_DECREASE_FONT_SIZE, "ctrl+shift+minus");
    expectAction(Qt::NoModifier, Qt::Key_F11, ACTION_FULLSCREEN, "f11");
    expectAction(Qt::ShiftModifier, Qt::Key_PageUp, ACTION_SCROLL_UP, "shift+pageup");
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
    const auto ctrlShift = Qt::ControlModifier | Qt::ShiftModifier;

    expectEventAction(ctrlShift, Qt::Key_1, ACTION_GOTO_TAB_1, "Key_1");
    expectEventAction(ctrlShift, Qt::Key_Exclam, ACTION_GOTO_TAB_1, "Key_Exclam (Shift+1)");
    expectEventAction(ctrlShift, Qt::Key_0, ACTION_RESET_FONT_SIZE, "Key_0");
    expectEventAction(ctrlShift, Qt::Key_ParenRight, ACTION_RESET_FONT_SIZE,
                      "Key_ParenRight (Shift+0)");
    expectEventAction(ctrlShift, Qt::Key_Backslash, ACTION_SPLIT_HORIZONTAL, "Key_Backslash");
    expectEventAction(ctrlShift, Qt::Key_Bar, ACTION_SPLIT_HORIZONTAL, "Key_Bar (Shift+\\)");
    expectEventAction(ctrlShift, Qt::Key_Minus, ACTION_DECREASE_FONT_SIZE, "Key_Minus");
    expectEventAction(ctrlShift, Qt::Key_Underscore, ACTION_DECREASE_FONT_SIZE,
                      "Key_Underscore (Shift+-) reaches the same action");
    expectEventAction(ctrlShift, Qt::Key_Plus, ACTION_INCREASE_FONT_SIZE, "Key_Plus");
    expectEventAction(ctrlShift, Qt::Key_Equal, ACTION_INCREASE_FONT_SIZE,
                      "Key_Equal (Shift+= on US)");

    /* The retry must not fire without Shift, or Ctrl+C would become a shortcut. */
    expectEventAction(Qt::ControlModifier, Qt::Key_C, ACTION_NONE, "ctrl+c still unbound");
    expectEventAction(Qt::ControlModifier, Qt::Key_1, ACTION_NONE, "ctrl+1 still unbound");
}

void testMacOsBindings() {
    check::section("macOS Command bindings");

    loadBindingSet(/*macOs=*/true);
    check::that(Config::instance().macOsBindings(), "the macOS set is active");

    /*
     * `cmd` is Qt::MetaModifier and `ctrl` is Qt::ControlModifier, on every
     * platform. RaTTY disables Qt's macOS habit of swapping the two, which would
     * otherwise make a cmd+ binding fire on physical Ctrl and -- far worse -- make
     * Command+C send SIGINT instead of Ctrl+C.
     */
    const auto cmd = Qt::MetaModifier;
    const auto cmdShift = Qt::MetaModifier | Qt::ShiftModifier;

    expectAction(cmd, Qt::Key_T, ACTION_NEW_TAB, "cmd+t");
    expectAction(cmd, Qt::Key_W, ACTION_CLOSE_TAB, "cmd+w");
    expectAction(cmd, Qt::Key_C, ACTION_COPY, "cmd+c");
    expectAction(cmd, Qt::Key_V, ACTION_PASTE, "cmd+v");
    expectAction(cmd, Qt::Key_Q, ACTION_QUIT, "cmd+q");
    expectAction(cmd, Qt::Key_D, ACTION_SPLIT_HORIZONTAL, "cmd+d");
    expectAction(cmdShift, Qt::Key_D, ACTION_SPLIT_VERTICAL, "cmd+shift+d");
    expectAction(cmdShift, Qt::Key_W, ACTION_CLOSE_SPLIT, "cmd+shift+w");
    expectAction(cmd, Qt::Key_1, ACTION_GOTO_TAB_1, "cmd+1");
    expectAction(cmd, Qt::Key_9, ACTION_GOTO_TAB_9, "cmd+9");
    expectAction(cmd, Qt::Key_K, ACTION_CLEAR_SCROLLBACK, "cmd+k");

    /* Font size, through every key event a user might actually produce. */
    expectAction(cmd, Qt::Key_Equal, ACTION_INCREASE_FONT_SIZE, "cmd+= (unshifted)");
    expectAction(cmdShift, Qt::Key_Plus, ACTION_INCREASE_FONT_SIZE, "cmd+shift+= typing '+'");
    expectAction(cmd, Qt::Key_Plus, ACTION_INCREASE_FONT_SIZE, "cmd++ (numpad)");
    expectAction(cmd, Qt::Key_Minus, ACTION_DECREASE_FONT_SIZE, "cmd+-");
    expectAction(cmdShift, Qt::Key_Underscore, ACTION_DECREASE_FONT_SIZE,
                 "cmd+shift+- typing '_'");
    expectAction(cmd, Qt::Key_0, ACTION_RESET_FONT_SIZE, "cmd+0");

    /* With the Command set active, Ctrl belongs entirely to the shell. */
    expectAction(Qt::ControlModifier, Qt::Key_C, ACTION_NONE, "ctrl+c reaches the shell");
    expectAction(Qt::ControlModifier, Qt::Key_D, ACTION_NONE, "ctrl+d reaches the shell");
    expectAction(Qt::ControlModifier | Qt::ShiftModifier, Qt::Key_T, ACTION_NONE,
                 "the Ctrl+Shift set is inactive");
}

void testBindingSetsAreExclusive() {
    check::section("only one binding set is active at a time");

    loadBindingSet(/*macOs=*/false);
    check::that(!Config::instance().macOsBindings(), "the Ctrl+Shift set is active");
    expectAction(Qt::MetaModifier, Qt::Key_T, ACTION_NONE,
                 "cmd+t does nothing when the macOS set is off");
    expectAction(Qt::ControlModifier | Qt::ShiftModifier, Qt::Key_T, ACTION_NEW_TAB,
                 "ctrl+shift+t works when the macOS set is off");

    loadBindingSet(/*macOs=*/true);
    expectAction(Qt::MetaModifier, Qt::Key_T, ACTION_NEW_TAB,
                 "cmd+t works when the macOS set is on");
    expectAction(Qt::ControlModifier | Qt::ShiftModifier, Qt::Key_T, ACTION_NONE,
                 "ctrl+shift+t does nothing when the macOS set is on");
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
    expectBytes(Qt::AltModifier, Qt::Key_Left, QString(), false, "\x1b[1;3D", "Alt+Left");

    expectBytes(Qt::NoModifier, Qt::Key_Delete, QString(), false, "\x1b[3~", "Delete");
    expectBytes(Qt::ControlModifier, Qt::Key_Delete, QString(), false, "\x1b[3;5~", "Ctrl+Delete");
    expectBytes(Qt::NoModifier, Qt::Key_Home, QString(), false, "\x1b[H", "Home");
    expectBytes(Qt::NoModifier, Qt::Key_F1, QString(), false, "\x1b[P", "F1");
    expectBytes(Qt::NoModifier, Qt::Key_F5, QString(), false, "\x1b[15~", "F5");

    expectBytes(Qt::AltModifier, Qt::Key_B, QStringLiteral("b"), false, "\x1b" "b",
                "Alt+B -> ESC b (back word)");
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
    testMacOsBindings();
    testBindingSetsAreExclusive();
    testShellKeysAreNotStolen();
    testKeyEncoding();
    return check::report("test_input");
}
