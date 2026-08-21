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
#include "config/chrome.h"
#include "config/config.h"
#include "config/theme.h"
#include <QDir>
#include <QGuiApplication>
#include <QKeyCombination>
#include <QTemporaryDir>
#include <QTextStream>
#include <cstdlib>
#include <string>

namespace {

QTemporaryDir* sandbox = nullptr;

/*
 * The palette the shipped configuration produces with no user overlay.
 *
 * Assertions about layering want to say "this did not change", and naming a hex
 * value to say it couples the test to whatever RaTTY Dark currently looks like.
 * Every one of these constants had to be edited the last two times the default
 * theme was retuned, which is a test telling you about the theme rather than
 * about the loader. Captured once, at start-up, instead.
 */
struct DefaultPalette {
    QColor background;
    QColor foreground;
    QColor green;
};
DefaultPalette shippedDefaults;

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
    /*
     * That the default *theme* was found and applied, rather than a particular
     * colour: the built-in Palette is what remains if it was not.
     */
    const Palette builtIn;
    check::that(config.palette().defaultBackground().isValid(),
                "the default background is a real colour");
    check::that(config.palette().defaultBackground() != builtIn.defaultBackground(),
                "and comes from the default theme, not the built-in fallback");
    check::that(config.palette().defaultBackground().lightness()
                    < config.palette().defaultForeground().lightness(),
                "the default theme is a dark one");
    check::that(!config.fontFamilies().isEmpty(),
                "the default font preference list is not empty");
    check::that(config.fontFamilies().first().contains(QLatin1String("Nerd")),
                "the preferred family is the Nerd Font");
    /* The shipped cursor *style* is taste; that it blinks by default is
     * documented behaviour. Assert the second, not the first. */
    check::that(config.cursorBlink(), "the cursor blinks by default");

    check::equal(config.scrollbackLines(), Config::DEFAULT_SCROLLBACK_LINES,
                 "the bundled defaults state the documented scrollback size");
    check::equal(config.scrollMultiplier(), Config::DEFAULT_SCROLL_MULTIPLIER,
                 "and the wheel step");
    check::that(config.alternateScroll(),
                "the wheel drives a pager on the alternate screen by default");

    const CursorStyle shippedCursor = config.cursorStyle();
    loadWithUserConfig("cursor:\n  style: nonsense\n");
    check::that(Config::instance().cursorStyle() == shippedCursor,
                "an unusable cursor style keeps the default");
    loadWithUserConfig(nullptr);

    /* A config file without a keybindings section must not leave the
     * application with none at all. Both platform sets put new_tab on Meta. */
    check::that(actionFor(Qt::MetaModifier, Qt::Key_T) == ACTION_NEW_TAB,
                "default keybindings are present");
    check::that(config.keybindingCount() > 20, "and the whole default set loaded");
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
    check::that(config.palette().defaultBackground() == shippedDefaults.background,
                "an unmentioned colour kept its default");
    check::equal(config.windowWidth(), Config::DEFAULT_WINDOW_WIDTH,
                 "an unmentioned window size kept its default");
    check::that(actionFor(Qt::MetaModifier, Qt::Key_T) == ACTION_NEW_TAB,
                "unmentioned keybindings survived");
}

void testScrollbackAndMouseSettings() {
    check::section("scrollback and mouse settings");

    loadWithUserConfig(R"(
scrollback:
  lines: 500
  multiplier: 7
mouse:
  alternate_scroll: false
)");
    const Config& config = Config::instance();

    check::equal(config.scrollbackLines(), 500, "the overlaid scrollback size took effect");
    check::equal(config.scrollMultiplier(), 7, "as did the wheel step");
    check::that(!config.alternateScroll(), "and alternate scroll was turned off");

    /* Zero is meaningful -- it disables the buffer -- so it must survive the
     * clamp rather than being treated as "unset". */
    loadWithUserConfig("scrollback:\n  lines: 0\n");
    check::equal(Config::instance().scrollbackLines(), 0, "zero disables the scrollback");

    /* Nonsense values fall back rather than producing a pane that keeps
     * everything or scrolls a thousand rows per notch. */
    loadWithUserConfig("scrollback:\n  lines: -5\n  multiplier: 0\n");
    check::equal(Config::instance().scrollbackLines(), 0, "a negative size clamps to none");
    check::equal(Config::instance().scrollMultiplier(), 1,
                 "a wheel step below one clamps to one row");

    loadWithUserConfig(nullptr);
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
    check::that(palette.entry(2) == shippedDefaults.green, "unmentioned green unchanged");

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
    check::that(Config::instance().palette().defaultBackground() == shippedDefaults.background,
                "the empty colour was ignored and the default kept");
    check::equal(Config::instance().fontSize(), 15,
                 "the rest of the file was still applied");
}

void testKeybindingOverlay() {
    check::section("keybinding overlay");

    loadWithUserConfig(R"(
keybindings:
  cmd+k: none
  cmd+g: new_tab
  ctrl+alt+p: paste
)");
    const auto meta = Qt::MetaModifier;

    check::that(actionFor(meta, Qt::Key_K) == ACTION_NONE,
                "`none` removed a default binding");
    check::that(actionFor(meta, Qt::Key_G) == ACTION_NEW_TAB,
                "a new binding was added");
    check::that(actionFor(Qt::ControlModifier | Qt::AltModifier, Qt::Key_P)
                    == ACTION_PASTE,
                "a binding with a different modifier set works");

    /*
     * Naming an action makes the user's config the whole story for it, so the
     * default keys for new_tab and paste are gone. Actions the config does not
     * mention keep theirs.
     */
    check::that(actionFor(meta, Qt::Key_T) == ACTION_NONE,
                "the default key for a reassigned action was released");
    check::that(actionFor(meta, Qt::Key_V) == ACTION_NONE, "likewise for paste");
    check::that(actionFor(meta, Qt::Key_W) == ACTION_CLOSE_TAB,
                "an unmentioned action kept its default key");
    check::that(actionFor(meta, Qt::Key_C) == ACTION_COPY, "and so did copy");
    check::that(actionFor(Qt::ControlModifier | Qt::ShiftModifier, Qt::Key_W)
                    == ACTION_SPLIT_HORIZONTAL,
                "and so did the split bindings");
    check::that(actionFor(Qt::ControlModifier, Qt::Key_C) == ACTION_NONE,
                "plain Ctrl+C is still the shell's");
}

void testMacOsBindingsFlag() {
    check::section("mac_os_bindings selects the default keybinding file");

    loadWithUserConfig("mac_os_bindings: true\n");
    check::that(Config::instance().macOsBindings(), "true forces the macOS file");
    const int macOsCount = Config::instance().keybindingCount();
    check::that(macOsCount > 20, "and it loaded a full set");

    loadWithUserConfig("mac_os_bindings: false\n");
    check::that(!Config::instance().macOsBindings(), "false forces the Linux file");
    check::equal(Config::instance().keybindingCount(), macOsCount,
                 "which binds the same number of keys");

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
     * The defaults are loaded *after* the user's file, because which file to
     * load depends on a setting the user may change. They must still merge
     * underneath the user's own bindings.
     */
    loadWithUserConfig(R"(
mac_os_bindings: true
keybindings:
  cmd+t: none
  cmd+shift+t: new_tab
)");
    check::that(actionFor(Qt::MetaModifier, Qt::Key_T) == ACTION_NONE,
                "the user's `none` beat the defaults loaded after it");
    check::that(actionFor(Qt::MetaModifier | Qt::ShiftModifier, Qt::Key_T) == ACTION_NEW_TAB,
                "and so did the replacement binding");
    check::that(actionFor(Qt::MetaModifier, Qt::Key_W) == ACTION_CLOSE_TAB,
                "while the rest of the defaults still applied");
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

void testRebindingAnActionReleasesItsDefaultKeys() {
    check::section("rebinding an action releases the keys it inherited");

    /*
     * The contract: the defaults apply except for what the user states. So
     * naming an action in your own config makes your config the whole story for
     * that action -- otherwise "set split_vertical to ctrl+shift+g" would leave
     * the default key working too, and two keys would do the same thing.
     */
    loadWithUserConfig(R"(
keybindings:
  ctrl+shift+g: split_vertical
)");
    const auto meta = Qt::MetaModifier;
    const auto ctrlShift = Qt::ControlModifier | Qt::ShiftModifier;

    check::that(actionFor(ctrlShift, Qt::Key_G) == ACTION_SPLIT_VERTICAL,
                "the new key performs the action");
    check::that(actionFor(ctrlShift, Qt::Key_V) == ACTION_NONE,
                "the default key for that action was released");

    /* Every other action keeps its defaults. */
    check::that(actionFor(meta, Qt::Key_T) == ACTION_NEW_TAB, "cmd+t untouched");
    check::that(actionFor(ctrlShift, Qt::Key_W) == ACTION_SPLIT_HORIZONTAL,
                "ctrl+shift+w untouched");
    check::that(actionFor(ctrlShift, Qt::Key_C) == ACTION_CLOSE_SPLIT,
                "ctrl+shift+c untouched");
    check::that(actionFor(meta, Qt::Key_C) == ACTION_COPY, "cmd+c untouched");
    check::equal(Config::instance().fontSize(), 13, "the font size is still default");
    check::equal(Config::instance().windowPadding(), 4, "padding is still default");

    /* Listing several keys for one action keeps all of them. */
    loadWithUserConfig(R"(
keybindings:
  ctrl+shift+g: split_vertical
  cmd+g: split_vertical
)");
    check::that(actionFor(ctrlShift, Qt::Key_G) == ACTION_SPLIT_VERTICAL, "first new key works");
    check::that(actionFor(meta, Qt::Key_G) == ACTION_SPLIT_VERTICAL, "second new key works");
    check::that(actionFor(ctrlShift, Qt::Key_V) == ACTION_NONE,
                "the inherited key is still released");

    /* Reassigning a key takes it from whatever it used to do. */
    loadWithUserConfig(R"(
keybindings:
  cmd+k: new_tab
)");
    check::that(actionFor(meta, Qt::Key_K) == ACTION_NEW_TAB,
                "the key now performs the new action");
    check::that(actionFor(meta, Qt::Key_T) == ACTION_NONE,
                "and new_tab's default key was released");

    /* Actions the defaults give several keys keep them all. */
    loadWithUserConfig(nullptr);
    check::that(actionFor(Qt::NoModifier, Qt::Key_F11) == ACTION_FULLSCREEN,
                "a default action with two keys keeps the first");
    check::that(actionFor(Qt::MetaModifier | Qt::ControlModifier, Qt::Key_F)
                    == ACTION_FULLSCREEN,
                "and the second");
}

void testEverythingElseIsPreserved() {
    check::section("a one-line config changes exactly one thing");

    /* Capture the full default state, then change a single setting and confirm
     * nothing else moved. */
    loadWithUserConfig(nullptr);
    const Config& config = Config::instance();

    const int defaultSize = config.fontSize();
    const int defaultPadding = config.windowPadding();
    const int defaultWidth = config.windowWidth();
    const QColor defaultBackground = config.palette().defaultBackground();
    const QColor defaultForeground = config.palette().defaultForeground();
    const QColor defaultRed = config.palette().entry(1);
    const QStringList defaultFamilies = config.fontFamilies();
    const bool defaultBlink = config.cursorBlink();
    const CursorStyle defaultCursor = config.cursorStyle();
    const int defaultBindingCount = config.keybindingCount();

    loadWithUserConfig("font:\n  size: 17\n");

    check::equal(config.fontSize(), 17, "the one stated setting changed");
    check::equal(config.windowPadding(), defaultPadding, "padding preserved");
    check::equal(config.windowWidth(), defaultWidth, "window width preserved");
    check::that(config.palette().defaultBackground() == defaultBackground,
                "background preserved");
    check::that(config.palette().defaultForeground() == defaultForeground,
                "foreground preserved");
    check::that(config.palette().entry(1) == defaultRed, "ANSI red preserved");
    check::that(config.fontFamilies() == defaultFamilies, "font families preserved");
    check::that(config.cursorBlink() == defaultBlink, "cursor blink preserved");
    check::that(config.cursorStyle() == defaultCursor, "cursor style preserved");
    check::equal(config.keybindingCount(), defaultBindingCount,
                 "every keybinding preserved");
}

void testCursorFollowsForegroundAcrossLayers() {
    check::section("the cursor colour follows the foreground across layers");

    /*
     * The bundled defaults deliberately leave `cursor` unset so that a user who
     * changes only the foreground does not end up with a cursor in the old
     * colour -- which on an inverted theme would be close to invisible.
     */
    loadWithUserConfig(R"(
colors:
  foreground: "#102030"
)");
    check::that(Config::instance().palette().cursorColor() == QColor(0x10, 0x20, 0x30),
                "a foreground-only config moves the cursor with it");

    loadWithUserConfig(R"(
colors:
  foreground: "#102030"
  cursor: "#ff8800"
)");
    check::that(Config::instance().palette().cursorColor() == QColor(0xff, 0x88, 0x00),
                "an explicit cursor colour still wins");

    loadWithUserConfig(R"(
colors:
  cursor: "#00ff00"
)");
    check::that(Config::instance().palette().cursorColor() == QColor(0, 255, 0),
                "a cursor-only config works");
    check::that(Config::instance().palette().defaultForeground() == shippedDefaults.foreground,
                "and leaves the foreground alone");
}

void testThemeCatalogue() {
    check::section("built-in themes");

    const QStringList available = Config::availableThemes();
    check::that(available.size() >= 8,
                "several themes ship with RaTTY (" + std::to_string(available.size()) + ")");
    check::that(available.contains(QStringLiteral("ratty-dark")),
                "the default theme is in the catalogue");
    check::that(themes::exists(QStringLiteral("nord")), "a known theme exists");
    check::that(!themes::exists(QStringLiteral("no-such-theme")),
                "an unknown theme does not");

    /*
     * Every theme must define the whole palette. A half-written theme would
     * inherit stray colours from whatever was loaded before it, which is the
     * kind of bug that only shows up on one shade of one character.
     */
    for (const QString& id : available) {
        loadWithUserConfig(("theme: " + id.toStdString() + "\n").c_str());
        const Config& config = Config::instance();
        const Palette& palette = config.palette();

        check::equal(config.themeName().toStdString(), id.toStdString(),
                     id.toStdString() + " loaded");
        check::that(palette.defaultBackground() != palette.defaultForeground(),
                    id.toStdString() + ": background differs from foreground");

        /*
         * Detect a forgotten or misspelled key. Each theme is applied over a
         * fresh built-in palette, so a slot the theme fails to set keeps the
         * built-in value, and every shipped theme repaints essentially all of
         * them. RaTTY Dark used to be excused from this because it restated the
         * built-in colours; it has its own palette now, so the exemption is
         * gone and the check covers every theme.
         *
         * Deliberately *not* a check that all sixteen differ from each other:
         * Nord, Catppuccin, Tokyo Night and One Dark all define their bright
         * variants as the same hue as the normal ones, which is how those
         * palettes are published.
         */
        {
            const Palette builtIn;
            int inherited = 0;
            for (int slot = 0; slot < 16; ++slot) {
                if (palette.entry(slot) == builtIn.entry(slot)) ++inherited;
            }
            check::that(inherited <= 2,
                        id.toStdString() + ": defines its own ANSI palette ("
                            + std::to_string(inherited) + " slots inherited)");
            check::that(palette.defaultBackground() != builtIn.defaultBackground()
                            || palette.defaultForeground() != builtIn.defaultForeground(),
                        id.toStdString() + ": defines its own background/foreground");
        }

        /* Text has to be readable on the background. */
        const int contrast = std::abs(palette.defaultForeground().lightness()
                                      - palette.defaultBackground().lightness());
        check::that(contrast > 60, id.toStdString() + ": foreground contrasts with background");
    }
}

void testThemeChromeStaysCoherent() {
    check::section("the tab bar follows the theme");

    /*
     * Chrome is derived, not stated, so switching theme has to move the bar with
     * it -- including for light themes, where a bar that only ever lightens
     * would vanish.
     */
    for (const QString& id : Config::availableThemes()) {
        loadWithUserConfig(("theme: " + id.toStdString() + "\n").c_str());
        const Config& config = Config::instance();
        const ChromeColors::Resolved chrome = config.chromeColors().resolve(config.palette());

        const int barLightness = chrome.tabBarBackground.lightness();
        const int terminalLightness = config.palette().defaultBackground().lightness();
        const int separation = std::abs(barLightness - terminalLightness);
        /*
         * "Distinguishable" cannot be a flat number of lightness steps. Near
         * black, a handful of steps is a large *proportional* change and plainly
         * visible -- RaTTY Dark puts its bar at lightness 12 under a terminal at
         * 19, which is obvious on screen and seven steps apart. The same seven
         * steps between 120 and 127 would not be. So: eight steps anywhere, or a
         * quarter of the lighter of the two, whichever is satisfied.
         *
         * A bar genuinely the same colour as the terminal still fails both.
         */
        const int lighter = std::max(barLightness, terminalLightness);
        check::that(separation >= 8 || separation * 4 >= lighter,
                    id.toStdString() + ": the bar is distinguishable from the terminal");

        const int labelContrast = std::abs(chrome.inactiveTabForeground.lightness()
                                           - barLightness);
        check::that(labelContrast > 20,
                    id.toStdString() + ": inactive labels are readable on the bar");

        /*
         * Either route is legitimate -- what matters is that the accent comes
         * from the theme rather than from a built-in default. A scheme whose
         * border colour is not its bright blue (RaTTY Dark, following the kitty
         * configuration it was drawn from) has to be able to say so.
         */
        if (const auto& stated = config.chromeColors().accent; stated) {
            check::that(chrome.accent == *stated,
                        id.toStdString() + ": the accent the theme stated is used");
        } else {
            check::that(chrome.accent == config.palette().entry(12),
                        id.toStdString() + ": the accent was derived from the theme");
        }
    }
}

void testThemeAndOverridePrecedence() {
    check::section("colours override the theme, whatever the file order");

    /* `colors` written *before* `theme`: the theme must still be the base. */
    loadWithUserConfig(R"(
colors:
  red: "#ff0000"
theme: nord
)");
    const Palette* palette = &Config::instance().palette();
    check::that(palette->entry(1) == QColor(255, 0, 0), "the override won");
    check::that(palette->entry(2) == QColor(0xa3, 0xbe, 0x8c), "Nord's green survived");
    check::that(palette->defaultBackground() == QColor(0x2e, 0x34, 0x40),
                "Nord's background survived");

    /* And the other way round, for the same result. */
    loadWithUserConfig(R"(
theme: nord
colors:
  red: "#ff0000"
)");
    palette = &Config::instance().palette();
    check::that(palette->entry(1) == QColor(255, 0, 0), "the override won again");
    check::that(palette->defaultBackground() == QColor(0x2e, 0x34, 0x40),
                "and the theme is still the base");

    /* Overriding the background only, keeping the theme's accents. */
    loadWithUserConfig(R"(
theme: gruvbox-dark
colors:
  background: "#000000"
)");
    palette = &Config::instance().palette();
    check::that(palette->defaultBackground() == QColor(0, 0, 0), "background overridden");
    check::that(palette->entry(3) == QColor(0xd7, 0x99, 0x21),
                "Gruvbox's yellow is untouched");
    check::that(palette->defaultForeground() == QColor(0xeb, 0xdb, 0xb2),
                "and so is its foreground");

    /* A theme sets the cursor from its foreground, so an inverted theme does not
     * leave an invisible cursor behind. */
    loadWithUserConfig("theme: solarized-light\n");
    palette = &Config::instance().palette();
    check::that(palette->cursorColor() == palette->defaultForeground(),
                "the cursor followed the theme's foreground");

    loadWithUserConfig(R"(
theme: solarized-light
colors:
  cursor: "#ff00ff"
)");
    check::that(Config::instance().palette().cursorColor() == QColor(255, 0, 255),
                "an explicit cursor still wins over the theme");
}

void testUnknownThemeIsSafe() {
    check::section("an unknown theme falls back rather than failing");

    loadWithUserConfig("theme: definitely-not-a-theme\n");
    const Config& config = Config::instance();

    check::that(config.themeName().isEmpty(), "the bad name was discarded");
    /* The built-in palette is still complete and usable. */
    check::that(config.palette().defaultBackground().isValid(), "the background is valid");
    check::that(config.palette().defaultForeground().isValid(), "the foreground is valid");
    check::that(config.palette().defaultBackground() != config.palette().defaultForeground(),
                "and they differ");

    /* Other settings in the same file still apply. */
    loadWithUserConfig("theme: nope\nfont:\n  size: 19\n");
    check::equal(Config::instance().fontSize(), 19,
                 "the rest of the file was still read");
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
    check::that(actionFor(Qt::MetaModifier, Qt::Key_T) == ACTION_NEW_TAB,
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

/*
 * Where a new pane starts.
 *
 * Tabs and splits are separate settings on purpose, and both accept the same
 * three spellings, so all six combinations are worth pinning: the two defaults,
 * a fixed path, and the fallbacks that keep resolve() from ever handing back
 * something unusable.
 */
void testStartDirectories() {
    check::section("new tab and new split start directories");

    const QString home = QDir::homePath();

    loadWithUserConfig(nullptr);
    check::that(Config::instance().newTabDirectory().kind == StartDirectory::Kind::Home,
                "by default a new tab starts at home");
    check::that(Config::instance().newSplitDirectory().kind == StartDirectory::Kind::Cwd,
                "and a new split follows the pane it came from");

    /* A split set to inherit takes the directory it is handed... */
    check::equal(Config::instance().newSplitDirectory().resolve(sandbox->path()).toStdString(),
                 sandbox->path().toStdString(),
                 "an inheriting split resolves to the directory it was given");
    /* ...and falls back to home when the pane cannot say where it is, rather
     * than to whatever directory RaTTY itself was launched from. */
    check::equal(Config::instance().newSplitDirectory().resolve(QString()).toStdString(),
                 home.toStdString(),
                 "with nothing to inherit it falls back to home");
    /* A tab ignores what it is handed: it is configured as home. */
    check::equal(Config::instance().newTabDirectory().resolve(sandbox->path()).toStdString(),
                 home.toStdString(),
                 "a home-configured tab ignores the inherited directory");

    loadWithUserConfig(R"(
directories:
  new_tab: cwd
  new_split: home
)");
    check::that(Config::instance().newTabDirectory().kind == StartDirectory::Kind::Cwd,
                "the two settings can be swapped over");
    check::that(Config::instance().newSplitDirectory().kind == StartDirectory::Kind::Home,
                "and a split can be pinned to home");

    /* Custom paths, which is the other thing a user might want. `~` expands. */
    const QString custom = sandbox->path() + QStringLiteral("/projects");
    QDir().mkpath(custom);
    loadWithUserConfig(QStringLiteral(R"(
directories:
  new_tab: "%1"
  new_split: "~"
)").arg(custom).toUtf8().constData());

    check::that(Config::instance().newTabDirectory().kind == StartDirectory::Kind::Custom,
                "a path is taken as a path");
    check::equal(Config::instance().newTabDirectory().resolve(QString()).toStdString(),
                 custom.toStdString(), "and is what the shell starts in");
    check::equal(Config::instance().newSplitDirectory().resolve(QString()).toStdString(),
                 home.toStdString(), "a bare ~ expands to home");

    /* A directory that has since been removed must not leave a pane unopenable. */
    loadWithUserConfig(R"(
directories:
  new_tab: /this/path/does/not/exist
)");
    check::equal(Config::instance().newTabDirectory().resolve(QString()).toStdString(),
                 home.toStdString(),
                 "a configured path that no longer exists falls back to home");

    loadWithUserConfig(nullptr);
}

/*
 * The split divider and the dimming of panes that are not current.
 *
 * The separator is a *derived* chrome colour, which is the interesting part: a
 * theme states an accent (or states nothing and gets the palette's blue), and
 * the divider follows it. That is what makes the line belong to the theme
 * instead of being the one grey thing in a gruvbox window.
 */
void testSplitAppearance() {
    check::section("split separator colour and unfocused dimming");

    loadWithUserConfig(nullptr);
    {
        const Config& config = Config::instance();
        const ChromeColors::Resolved chrome =
            config.chromeColors().resolve(config.palette());

        check::that(chrome.splitSeparator.isValid(), "there is always a separator colour");

        /* Muted towards the background: between the two, not equal to either. */
        const QColor background = config.palette().defaultBackground();
        check::that(chrome.splitSeparator != background,
                    "the separator is distinguishable from the background");
        check::that(chrome.splitSeparator != chrome.accent,
                    "but is not the raw accent either");

        /* Following the accent is the whole point, so it has to be measurable:
         * the divider must sit nearer the accent's hue than the background is. */
        const auto blueness = [](const QColor& c) { return c.blueF() - c.redF(); };
        check::that(blueness(chrome.splitSeparator) > blueness(background),
                    "and it leans towards the accent, which defaults to blue");

        check::that(config.dimUnfocusedSplits(), "dimming is on by default");
        check::that(config.splitDimStrength() > 0.0f
                        && config.splitDimStrength() < 1.0f,
                    "at a strength that leaves the pane readable");
    }

    /* A theme whose accent is red must give a red-leaning divider, not a blue
     * one: this is the property that would silently rot. */
    loadWithUserConfig(R"(
tab_bar:
  colors:
    accent: "#cc2222"
)");
    {
        const Config& config = Config::instance();
        const ChromeColors::Resolved chrome =
            config.chromeColors().resolve(config.palette());
        check::that(chrome.splitSeparator.redF() > chrome.splitSeparator.blueF(),
                    "a red accent yields a red-leaning separator");
    }

    /* And an explicit colour wins outright. */
    loadWithUserConfig(R"(
splits:
  separator: "#00ff00"
  dim_unfocused: false
  dim_strength: 0.8
)");
    {
        const Config& config = Config::instance();
        const ChromeColors::Resolved chrome =
            config.chromeColors().resolve(config.palette());
        check::equal(chrome.splitSeparator.name().toStdString(), std::string("#00ff00"),
                     "an explicit separator colour is used as given");
        check::that(!config.dimUnfocusedSplits(), "dimming can be switched off");
        check::that(config.splitDimStrength() > 0.79f, "and its strength set");
    }

    /* Out-of-range strengths are clamped rather than producing an invisible pane. */
    loadWithUserConfig(R"(
splits:
  dim_strength: 5.0
)");
    check::that(Config::instance().splitDimStrength() <= Config::MAX_SPLIT_DIM_STRENGTH,
                "an absurd dim strength is clamped");

    loadWithUserConfig(nullptr);
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

    /* Record what the shipped configuration produces, for the layering tests. */
    loadWithUserConfig(nullptr);
    shippedDefaults = {Config::instance().palette().defaultBackground(),
                       Config::instance().palette().defaultForeground(),
                       Config::instance().palette().entry(2)};

    testBundledDefaults();
    testOverlayChangesOnlyWhatItMentions();
    testScrollbackAndMouseSettings();
    testColorsAndPalette();
    testUnquotedColourIsRejected();
    testKeybindingOverlay();
    testMacOsBindingsFlag();
    testFontFamilyForms();
    testRebindingAnActionReleasesItsDefaultKeys();
    testEverythingElseIsPreserved();
    testCursorFollowsForegroundAcrossLayers();
    testThemeCatalogue();
    testThemeChromeStaysCoherent();
    testThemeAndOverridePrecedence();
    testUnknownThemeIsSafe();
    testMalformedFileKeepsDefaults();
    testValueClamping();
    testStartDirectories();
    testSplitAppearance();
    return check::report("test_config");
}
