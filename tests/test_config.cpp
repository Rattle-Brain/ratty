/*
 * Configuration tests.
 *
 * These drive the real load path -- bundled defaults plus a user overlay -- by
 * pointing HOME at a temporary directory, rather than reaching into a private
 * parse method. That way the thing under test is what actually runs at start-up,
 * including where the file is looked for.
 *
 * The behaviour that matters is that an overlay changes *only* what it mentions,
 * and that a malformed file degrades to the defaults instead of leaving the
 * application half-configured.
 */

#include "check.h"
#include "config/config.h"
#include <QDir>
#include <QGuiApplication>
#include <QKeyCombination>
#include <QTemporaryDir>
#include <QTextStream>
#include <string>

namespace {

QTemporaryDir* sandbox = nullptr;

/* Write a user config into the sandbox and reload. Passing nullptr removes it. */
void loadWithUserConfig(const char* yaml) {
    const QString configDir = sandbox->path() + QStringLiteral("/.config/ratty");
    QDir().mkpath(configDir);
    const QString path = configDir + QStringLiteral("/config.yaml");

    QFile::remove(path);
    if (yaml) {
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            check::that(false, "could not write the sandbox config file");
            return;
        }
        QTextStream(&file) << QString::fromUtf8(yaml);
        file.close();
    }
    Config::instance().load();
}

Action actionFor(Qt::KeyboardModifiers modifiers, Qt::Key key) {
    return Config::instance().lookupAction(
        QKeySequence(QKeyCombination(modifiers, key)));
}

void testBundledDefaults() {
    check::section("bundled defaults");

    loadWithUserConfig(nullptr);
    const Config& config = Config::instance();

    check::equal(config.fontSize(), 13, "default font size");
    check::equal(config.windowPadding(), 4, "default window padding");
    check::that(config.palette().defaultBackground() == QColor(0x1e, 0x1e, 0x1e),
                "default background");
    check::that(!config.fontFamilies().isEmpty(),
                "the default font preference list is not empty");
    check::that(config.fontFamilies().first().contains(QLatin1String("Nerd")),
                "the preferred family is the Nerd Font");
    check::that(config.cursorStyle() == CursorStyle::Block, "default cursor style");
    check::that(config.cursorBlink(), "the cursor blinks by default");

    /* A config file without a keybindings section must not leave the
     * application with none at all. Which set is active follows the platform,
     * so check whichever one that is. */
    const Qt::KeyboardModifiers newTabModifiers =
        config.macOsBindings() ? Qt::KeyboardModifiers(Qt::MetaModifier)
                               : (Qt::ControlModifier | Qt::ShiftModifier);
    check::that(actionFor(newTabModifiers, Qt::Key_T) == ACTION_NEW_TAB,
                "default keybindings are present for the active set");
}

void testOverlayChangesOnlyWhatItMentions() {
    check::section("a user overlay changes only what it mentions");

    loadWithUserConfig(R"(
font:
  size: 20
window:
  padding: 11
)");
    const Config& config = Config::instance();

    check::equal(config.fontSize(), 20, "the overlaid font size took effect");
    check::equal(config.windowPadding(), 11, "the overlaid padding took effect");

    /* Everything absent from the overlay keeps its default. */
    check::that(config.palette().defaultBackground() == QColor(0x1e, 0x1e, 0x1e),
                "an unmentioned colour kept its default");
    check::equal(config.windowWidth(), Config::DEFAULT_WINDOW_WIDTH,
                 "an unmentioned window size kept its default");
    const Qt::KeyboardModifiers newTabModifiers =
        config.macOsBindings() ? Qt::KeyboardModifiers(Qt::MetaModifier)
                               : (Qt::ControlModifier | Qt::ShiftModifier);
    check::that(actionFor(newTabModifiers, Qt::Key_T) == ACTION_NEW_TAB,
                "unmentioned keybindings survived");
}

void testColorsAndPalette() {
    check::section("colours");

    loadWithUserConfig(R"(
colors:
  background: "#101418"
  foreground: "#c0c5ce"
  red: "#ff0000"
  bright_blue: "#0000ff"
)");
    const Palette& palette = Config::instance().palette();

    check::that(palette.defaultBackground() == QColor(0x10, 0x14, 0x18), "background set");
    check::that(palette.defaultForeground() == QColor(0xc0, 0xc5, 0xce), "foreground set");
    check::that(palette.entry(1) == QColor(255, 0, 0), "ANSI red set by name");
    check::that(palette.entry(12) == QColor(0, 0, 255), "bright blue set by name");
    check::that(palette.entry(2) == QColor(0x0d, 0xbc, 0x79), "unmentioned green unchanged");

    /* The cursor follows the foreground unless given explicitly. */
    check::that(palette.cursorColor() == QColor(0xc0, 0xc5, 0xce),
                "the cursor followed the foreground");

    loadWithUserConfig(R"(
colors:
  foreground: "#c0c5ce"
  cursor: "#ff00ff"
)");
    check::that(Config::instance().palette().cursorColor() == QColor(255, 0, 255),
                "an explicit cursor colour wins over the foreground");
}

void testUnquotedColourIsRejected() {
    check::section("an unquoted hex colour is rejected, not silently applied");

    /*
     * '#' starts a YAML comment, so `background: #101418` is an empty value.
     * Taking it at face value would paint the terminal black.
     */
    loadWithUserConfig(R"(
colors:
  background: #101418
font:
  size: 15
)");
    check::that(Config::instance().palette().defaultBackground()
                    == QColor(0x1e, 0x1e, 0x1e),
                "the empty colour was ignored and the default kept");
    check::equal(Config::instance().fontSize(), 15,
                 "the rest of the file was still applied");
}

void testKeybindingOverlay() {
    check::section("keybinding overlay");

    loadWithUserConfig(R"(
mac_os_bindings: false
keybindings:
  ctrl+shift+k: none
  ctrl+shift+g: new_tab
  ctrl+alt+p: paste
)");
    const auto ctrlShift = Qt::ControlModifier | Qt::ShiftModifier;

    check::that(actionFor(ctrlShift, Qt::Key_K) == ACTION_NONE,
                "`none` removed a default binding");
    check::that(actionFor(ctrlShift, Qt::Key_G) == ACTION_NEW_TAB,
                "a new binding was added");
    check::that(actionFor(Qt::ControlModifier | Qt::AltModifier, Qt::Key_P)
                    == ACTION_PASTE,
                "a binding with a different modifier set works");
    check::that(actionFor(ctrlShift, Qt::Key_T) == ACTION_NEW_TAB,
                "other defaults were untouched");
    check::that(actionFor(Qt::ControlModifier, Qt::Key_C) == ACTION_NONE,
                "plain Ctrl+C is still the shell's");
}

void testMacOsBindingsFlag() {
    check::section("mac_os_bindings selects the keybinding set");

    loadWithUserConfig("mac_os_bindings: true\n");
    check::that(Config::instance().macOsBindings(), "true forces the macOS set");
    check::that(Config::instance().lookupAction(
                    QKeySequence(QKeyCombination(Qt::MetaModifier, Qt::Key_T)))
                    == ACTION_NEW_TAB,
                "and cmd+t is bound");

    loadWithUserConfig("mac_os_bindings: false\n");
    check::that(!Config::instance().macOsBindings(), "false forces the other set");
    check::that(actionFor(Qt::ControlModifier | Qt::ShiftModifier, Qt::Key_T)
                    == ACTION_NEW_TAB,
                "and ctrl+shift+t is bound");

    /* "auto", and an absent key, both follow the platform. */
    loadWithUserConfig("mac_os_bindings: auto\n");
    check::that(Config::instance().macOsBindings() == Config::macOsBindingsByDefault(),
                "\"auto\" follows the platform");
    loadWithUserConfig(nullptr);
    check::that(Config::instance().macOsBindings() == Config::macOsBindingsByDefault(),
                "an absent flag follows the platform");

    /* A nonsense value is reported and the platform default kept. */
    loadWithUserConfig("mac_os_bindings: perhaps\n");
    check::that(Config::instance().macOsBindings() == Config::macOsBindingsByDefault(),
                "an unusable value keeps the platform default");

    /*
     * Each set is overlaid independently, so editing the active one works and
     * editing the inactive one has no effect on it.
     */
    loadWithUserConfig(R"(
mac_os_bindings: true
keybindings_macos:
  cmd+t: none
  cmd+shift+t: new_tab
keybindings:
  ctrl+shift+t: none
)");
    check::that(Config::instance().lookupAction(
                    QKeySequence(QKeyCombination(Qt::MetaModifier, Qt::Key_T)))
                    == ACTION_NONE,
                "the active macOS set honoured `none`");
    check::that(Config::instance().lookupAction(QKeySequence(QKeyCombination(
                    Qt::MetaModifier | Qt::ShiftModifier, Qt::Key_T)))
                    == ACTION_NEW_TAB,
                "and honoured the replacement binding");

    /* Switching sets brings the other set's edits into play instead. */
    loadWithUserConfig(R"(
mac_os_bindings: false
keybindings:
  ctrl+shift+t: none
)");
    check::that(actionFor(Qt::ControlModifier | Qt::ShiftModifier, Qt::Key_T)
                    == ACTION_NONE,
                "the Ctrl+Shift set honoured its own `none`");
}

void testFontFamilyForms() {
    check::section("font family accepts a scalar or a sequence");

    loadWithUserConfig(R"(
font:
  family: Menlo
)");
    check::equal(Config::instance().fontFamilies().size(), 1, "a single name reads as a list");
    check::equal(Config::instance().fontFamilies().first().toStdString(),
                 std::string("Menlo"), "and holds that name");

    loadWithUserConfig(R"(
font:
  family:
    - Iosevka
    - Menlo
  fallback:
    - Apple Color Emoji
)");
    check::equal(Config::instance().fontFamilies().size(), 2, "a sequence reads in order");
    check::equal(Config::instance().fontFamilies().first().toStdString(),
                 std::string("Iosevka"), "the first preference is first");
    check::equal(Config::instance().fontFallbacks().size(), 1, "fallbacks are read");

    /* "Monospace" is a fontconfig alias, not a family: it means "ask the
     * platform", so it must not be passed through as a name. */
    loadWithUserConfig(R"(
font:
  family: Monospace
)");
    check::that(Config::instance().fontFamilies().isEmpty(),
                "\"Monospace\" is normalised away to mean the platform default");
}

void testMalformedFileKeepsDefaults() {
    check::section("a malformed file leaves the defaults intact");

    loadWithUserConfig(R"(
font:
  size: 16
   padding: broken
)");
    check::equal(Config::instance().fontSize(), Config::DEFAULT_FONT_SIZE,
                 "a YAML syntax error discards the whole overlay");
    const Qt::KeyboardModifiers newTabModifiers =
        Config::instance().macOsBindings() ? Qt::KeyboardModifiers(Qt::MetaModifier)
                                           : (Qt::ControlModifier | Qt::ShiftModifier);
    check::that(actionFor(newTabModifiers, Qt::Key_T) == ACTION_NEW_TAB,
                "and keybindings still work");

    loadWithUserConfig("- a\n- list\n");
    check::equal(Config::instance().fontSize(), Config::DEFAULT_FONT_SIZE,
                 "a non-mapping document is rejected");

    loadWithUserConfig("");
    check::equal(Config::instance().fontSize(), Config::DEFAULT_FONT_SIZE,
                 "an empty file is harmless");

    /* Bad scalar types are reported and skipped, key by key. */
    loadWithUserConfig(R"(
font:
  size: "not a number"
window:
  padding: 9
  fullscreen: maybe
)");
    check::equal(Config::instance().fontSize(), Config::DEFAULT_FONT_SIZE,
                 "an unusable font size falls back to the default");
    check::equal(Config::instance().windowPadding(), 9,
                 "a usable key in the same section still applies");
    check::that(!Config::instance().startFullscreen(),
                "an unusable boolean keeps its default");
}

void testValueClamping() {
    check::section("out-of-range values are clamped");

    loadWithUserConfig(R"(
font:
  size: 5000
window:
  padding: 100000
  opacity: 9.0
)");
    check::equal(Config::instance().fontSize(), Config::MAX_FONT_SIZE,
                 "an absurd font size is clamped");
    check::that(Config::instance().windowPadding() <= Config::MAX_WINDOW_PADDING,
                "padding is clamped");
    check::that(Config::instance().windowOpacity() <= 1.0f, "opacity is clamped");

    loadWithUserConfig(R"(
font:
  size: -3
window:
  opacity: 0.0
)");
    check::equal(Config::instance().fontSize(), Config::MIN_FONT_SIZE,
                 "a negative font size is clamped");
    check::that(Config::instance().windowOpacity() >= 0.1f,
                "a fully transparent window is prevented");
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
    /* QDir::homePath() honours HOME, so the loader looks inside the sandbox and
     * never touches the real user's configuration. */
    qputenv("HOME", tempHome.path().toUtf8());

    testBundledDefaults();
    testOverlayChangesOnlyWhatItMentions();
    testColorsAndPalette();
    testUnquotedColourIsRejected();
    testKeybindingOverlay();
    testMacOsBindingsFlag();
    testFontFamilyForms();
    testMalformedFileKeepsDefaults();
    testValueClamping();
    return check::report("test_config");
}
