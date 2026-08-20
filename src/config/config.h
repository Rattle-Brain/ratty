/*
 * Config - application settings loaded from YAML
 *
 * Load order is: built-in defaults, then the bundled default_config.yaml from
 * the Qt resource system, then ~/.config/ratty/config.yaml as an overlay. Each
 * layer only overrides the keys it actually contains.
 *
 * That layering matters: a config file without a "keybindings" section must not
 * leave the application with *no* keybindings, and the bundled defaults must be
 * found regardless of the working directory the binary was started from.
 *
 * YAML rather than JSON because a configuration file people edit by hand wants
 * comments, and needs neither quoting of every key nor a comma discipline. The
 * one YAML sharp edge worth knowing is that '#' starts a comment, so a hex
 * colour has to be quoted; the parser reports that case specifically rather than
 * silently reading an empty value.
 */

#ifndef CONFIG_CONFIG_H
#define CONFIG_CONFIG_H

#include "chrome.h"
#include "theme.h"
#include "../core/cursor.h"
#include "../core/palette.h"
#include <QColor>
#include <QHash>
#include <QKeyEvent>
#include <QSet>
#include <QStringList>
#include <QKeySequence>
#include <QString>
#include <optional>
#include <string>

/* Actions that can be bound to keys. */
enum Action {
    ACTION_NONE = 0,

    ACTION_NEW_TAB,
    ACTION_CLOSE_TAB,
    ACTION_NEXT_TAB,
    ACTION_PREV_TAB,
    ACTION_GOTO_TAB_1,
    ACTION_GOTO_TAB_2,
    ACTION_GOTO_TAB_3,
    ACTION_GOTO_TAB_4,
    ACTION_GOTO_TAB_5,
    ACTION_GOTO_TAB_6,
    ACTION_GOTO_TAB_7,
    ACTION_GOTO_TAB_8,
    ACTION_GOTO_TAB_9,

    ACTION_SPLIT_HORIZONTAL,
    ACTION_SPLIT_VERTICAL,
    ACTION_CLOSE_SPLIT,
    ACTION_FOCUS_UP,
    ACTION_FOCUS_DOWN,
    ACTION_FOCUS_LEFT,
    ACTION_FOCUS_RIGHT,

    ACTION_QUIT,
    ACTION_FULLSCREEN,

    ACTION_COPY,
    ACTION_PASTE,

    ACTION_INCREASE_FONT_SIZE,
    ACTION_DECREASE_FONT_SIZE,
    ACTION_RESET_FONT_SIZE,

    ACTION_SCROLL_UP,
    ACTION_SCROLL_DOWN,
    ACTION_CLEAR_SCROLLBACK,
};

class Config {
public:
    static constexpr int DEFAULT_FONT_SIZE = 13;
    static constexpr int MIN_FONT_SIZE = 6;
    static constexpr int MAX_FONT_SIZE = 72;
    static constexpr int DEFAULT_WINDOW_WIDTH = 1280;
    static constexpr int DEFAULT_WINDOW_HEIGHT = 720;
    static constexpr int DEFAULT_WINDOW_PADDING = 4;
    static constexpr int MAX_WINDOW_PADDING = 200;

    static Config& instance();

    void load();

    /* The active theme's identifier, and every theme that ships with RaTTY. */
    QString themeName() const { return themeName_; }
    static QStringList availableThemes() { return themes::available(); }

    /*
     * Keybindings.
     *
     * Two sets are read from the configuration -- `keybindings` for
     * Linux/Windows and `keybindings_macos` for macOS -- and exactly one is
     * active. Which one is decided after every layer has been read, because the
     * `mac_os_bindings` flag may appear in any of them and in any order.
     */
    bool macOsBindings() const { return macOsBindings_; }
    /* True on macOS unless the configuration says otherwise. */
    static bool macOsBindingsByDefault();

    /* How many keys are bound in the active set. */
    int keybindingCount() const { return static_cast<int>(keybindings_.size()); }

    Action lookupAction(const QKeySequence& keySequence) const;
    bool isBound(const QKeySequence& keySequence) const;
    QKeySequence keybindingFor(Action action) const;

    /*
     * Resolve a key event, tolerating keyboard-layout differences.
     *
     * Qt reports either the unshifted key or the shifted symbol for the same
     * physical key depending on platform and layout: Ctrl+Shift+1 arrives as
     * Key_1 on one machine and Key_Exclam on another. Matching only the literal
     * combination means such a binding fires on some keyboards and not others,
     * so this also tries the key's shift partner.
     */
    Action lookupAction(const QKeyEvent* event) const;
    bool isBound(const QKeyEvent* event) const;

    /*
     * Colours. This is the seed for each session's palette, not a live view of
     * it: an application can retheme its own terminal through OSC 4/10/11/12,
     * so read colours from TerminalSession::palette() when drawing.
     */
    const Palette& palette() const { return palette_; }

    /*
     * Font families in order of preference. The first one actually installed
     * wins; if none are, the renderer falls back to the font the system has
     * configured as its monospaced default. Nothing about that fallback is
     * hard-coded here -- it is whatever the platform reports.
     */
    QStringList fontFamilies() const { return fontFamilies_; }
    /*
     * Families consulted for code points the primary font lacks, before
     * automatic discovery. No monospaced font covers everything a terminal has
     * to draw -- a patched icon font commonly has no box-drawing characters at
     * all, and colour emoji always live in a separate font.
     */
    QStringList fontFallbacks() const { return fontFallbacks_; }
    int fontSize() const { return fontSize_; }
    void setFontSize(int size);

    /* Empty space between the text grid and the window edge, in logical
     * pixels. Scaled by the device pixel ratio at use. */
    int windowPadding() const { return windowPadding_; }

    /* Tab bar */
    TabBarStyle tabBarStyle() const { return tabBarStyle_; }
    TabBarPosition tabBarPosition() const { return tabBarPosition_; }
    TabBarVisibility tabBarVisibility() const { return tabBarVisibility_; }
    const ChromeColors& chromeColors() const { return chromeColors_; }

    /* Cursor */
    CursorStyle cursorStyle() const { return cursorStyle_; }
    bool cursorBlink() const { return cursorBlink_; }

    /* Window */
    int windowWidth() const { return windowWidth_; }
    int windowHeight() const { return windowHeight_; }
    float windowOpacity() const { return windowOpacity_; }
    bool startFullscreen() const { return startFullscreen_; }

    static QString actionToString(Action action);
    static Action stringToAction(const QString& text);

private:
    Config();
    ~Config() = default;
    Config(const Config&) = delete;
    Config& operator=(const Config&) = delete;

    /*
     * Parses one YAML document into a Config. Declared here but defined in the
     * implementation file, which keeps yaml-cpp out of every translation unit
     * that merely wants to read a setting. A nested class has access to the
     * enclosing class's private members, so no friendship is needed.
     */
    struct Parser;

    /*
     * One layer's worth of keybindings, held apart until every layer has been
     * read. Staging is necessary because the *default* bindings come from a
     * separate file whose identity depends on `mac_os_bindings`, which the user's
     * own configuration may set -- so the defaults are loaded after the user's
     * file and must still merge underneath it.
     */
    struct BindingLayer {
        QHash<QKeySequence, Action> bound;
        QList<QKeySequence> unbound;      // keys explicitly set to `none`
        QSet<Action> assignedActions;     // actions this layer gives a key to

        bool isEmpty() const { return bound.isEmpty() && unbound.isEmpty(); }
        void absorb(const BindingLayer& later);
    };

    /*
     * Which configuration layer a document belongs to. Colours are merged in
     * this order regardless of the order the files were read in, which is what
     * lets `theme:` be a setting rather than something that has to appear before
     * the colours it affects.
     */
    enum class Layer { BuiltIn, Theme, Keybindings, User };

    void applyBuiltInDefaults();
    /* Decide which platform's defaults apply; must run before loadKeybindings(). */
    void resolvePlatformBindings();
    /* Read the macOS or Linux default keybinding file. */
    void loadKeybindings();
    /* Fold the default and user layers into the active set. */
    void resolveKeybindings();
    /* Build the palette from built-in, theme and user colours, in that order. */
    void resolvePalette();
    /* Merge chrome colours the same way. */
    void resolveChrome();
    /* Load the named theme, reporting an unknown name. */
    void applyTheme();

    /*
     * Overlay a YAML file or document; absent keys keep their current value.
     *
     * `userLayer` marks the user's own configuration, which is treated as
     * authoritative for any action it mentions -- see mergeBindings().
     */
    bool applyFile(const QString& path, Layer layer);
    bool applyDocument(const std::string& text, const QString& sourceLabel,
                       Layer layer);

    /*
     * Fold one document's bindings into an accumulated set.
     *
     * When `ownsAssignedActions` is set, every action the layer assigns is
     * considered *fully* described by that layer, so the keys inherited for it
     * are dropped first. That is what makes "rebind split_vertical to
     * ctrl+shift+w" release the default key rather than leaving two keys doing
     * the same thing. The bundled defaults are merged without it, so they can
     * legitimately offer several keys for one action.
     */
    static void mergeBindings(QHash<QKeySequence, Action>& target,
                              const BindingLayer& layer, bool ownsAssignedActions);
    /* Take everything set in `later`; unset fields leave `target` alone. */
    static void mergeChrome(ChromeColors& target, const ChromeColors& later);

    static QKeySequence parseKeySequence(const QString& text);

public:
    /* Where the user's own configuration lives. */
    static QString userConfigPath();
    /* The pre-YAML location, reported if it is found so the file is not
     * silently ignored. */
    static QString legacyUserConfigPath();

private:
    /* The other key on the same physical key ('1' <-> '!'), or Key_unknown. */
    static Qt::Key shiftPartner(int key);

    /* The default layer, the user's layer, and the resolved active set. */
    BindingLayer builtInBindings_;
    BindingLayer userBindings_;
    QHash<QKeySequence, Action> keybindings_;

    /* nullopt means "decide from the platform". */
    std::optional<bool> macOsBindingsOverride_;
    bool macOsBindings_ = false;

    Palette palette_;
    /* Colours staged per layer; see Layer and resolvePalette(). */
    PaletteOverrides builtInColors_;
    PaletteOverrides themeColors_;
    PaletteOverrides userColors_;

    QString themeName_;

    QStringList fontFamilies_;
    QStringList fontFallbacks_;
    int fontSize_ = DEFAULT_FONT_SIZE;
    int windowPadding_ = DEFAULT_WINDOW_PADDING;

    CursorStyle cursorStyle_ = CursorStyle::Block;
    bool cursorBlink_ = true;

    TabBarStyle tabBarStyle_ = TabBarStyle::Minimal;
    TabBarPosition tabBarPosition_ = TabBarPosition::Bottom;
    TabBarVisibility tabBarVisibility_ = TabBarVisibility::MultipleTabs;
    ChromeColors chromeColors_;
    /* Chrome staged the same way as the palette. */
    ChromeColors builtInChrome_;
    ChromeColors themeChrome_;
    ChromeColors userChrome_;

    int windowWidth_ = DEFAULT_WINDOW_WIDTH;
    int windowHeight_ = DEFAULT_WINDOW_HEIGHT;
    float windowOpacity_ = 1.0f;
    bool startFullscreen_ = false;
};

#endif /* CONFIG_CONFIG_H */
