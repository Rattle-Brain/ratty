/*
 * Config - application settings loaded from JSON
 *
 * Load order is: built-in defaults, then the bundled default_config.json from
 * the Qt resource system, then ~/.config/ratty/config.json as an overlay. Each
 * layer only overrides the keys it actually contains.
 *
 * That layering fixes two concrete problems with the previous version: a config
 * file without a "keybindings" section silently left the application with *no*
 * keybindings at all, and the bundled defaults were looked up through a
 * relative path, so they were only found when the binary happened to be run
 * from the project root.
 */

#ifndef CONFIG_CONFIG_H
#define CONFIG_CONFIG_H

#include "../core/cursor.h"
#include "../core/palette.h"
#include <QColor>
#include <QHash>
#include <QJsonObject>
#include <QKeyEvent>
#include <QStringList>
#include <QKeySequence>
#include <QString>

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

    /* Keybindings */
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

    void applyBuiltInDefaults();
    /* Overlay a JSON document; absent keys keep their current value. */
    bool applyJsonFile(const QString& path);
    void applyColors(const QJsonObject& colors);
    void applyFont(const QJsonObject& font);
    void applyCursor(const QJsonObject& cursor);
    void applyWindow(const QJsonObject& window);
    void applyKeybindings(const QJsonObject& keybindings);

    static QKeySequence parseKeySequence(const QString& text);
    static QString userConfigPath();
    /* The other key on the same physical key ('1' <-> '!'), or Key_unknown. */
    static Qt::Key shiftPartner(int key);

    QHash<QKeySequence, Action> keybindings_;

    Palette palette_;
    QStringList fontFamilies_;
    QStringList fontFallbacks_;
    int fontSize_ = DEFAULT_FONT_SIZE;
    int windowPadding_ = DEFAULT_WINDOW_PADDING;

    CursorStyle cursorStyle_ = CursorStyle::Block;
    bool cursorBlink_ = true;

    int windowWidth_ = DEFAULT_WINDOW_WIDTH;
    int windowHeight_ = DEFAULT_WINDOW_HEIGHT;
    float windowOpacity_ = 1.0f;
    bool startFullscreen_ = false;
};

#endif /* CONFIG_CONFIG_H */
