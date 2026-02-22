/*
 * Config - Configuration management with QSettings
 *
 * Manages:
 * - Keybindings (action -> QKeySequence mapping)
 * - Appearance (colors, fonts)
 * - Window settings
 */

#ifndef CONFIG_CONFIG_H
#define CONFIG_CONFIG_H

#include <QSettings>
#include <QKeySequence>
#include <QHash>
#include <QColor>
#include <QString>
#include <functional>

/* Actions that can be bound to keys */
enum Action {
    ACTION_NONE = 0,

    // Tab management
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

    // Split management
    ACTION_SPLIT_HORIZONTAL,
    ACTION_SPLIT_VERTICAL,
    ACTION_CLOSE_SPLIT,
    ACTION_FOCUS_UP,
    ACTION_FOCUS_DOWN,
    ACTION_FOCUS_LEFT,
    ACTION_FOCUS_RIGHT,

    // Window
    ACTION_QUIT,
    ACTION_FULLSCREEN,

    // Clipboard
    ACTION_COPY,
    ACTION_PASTE,

    // Scrollback
    ACTION_SCROLL_UP,
    ACTION_SCROLL_DOWN,
    ACTION_CLEAR_SCROLLBACK,
};

class Config {
public:
    // Singleton access
    static Config& instance();

    // Load/Save
    void load();
    void save();
    void loadDefaults();

    // Keybindings
    Action lookupAction(const QKeySequence& keySequence) const;
    void bindKey(const QKeySequence& keySequence, Action action);
    QKeySequence getKeybinding(Action action) const;

    // Appearance - Colors
    QColor backgroundColor() const { return backgroundColor_; }
    QColor foregroundColor() const { return foregroundColor_; }
    QColor cursorColor() const { return cursorColor_; }
    QColor selectionBackground() const { return selectionBackground_; }

    void setBackgroundColor(const QColor& color) { backgroundColor_ = color; }
    void setForegroundColor(const QColor& color) { foregroundColor_ = color; }
    void setCursorColor(const QColor& color) { cursorColor_ = color; }
    void setSelectionBackground(const QColor& color) { selectionBackground_ = color; }

    // Appearance - Font
    QString fontFamily() const { return fontFamily_; }
    int fontSize() const { return fontSize_; }

    void setFontFamily(const QString& family) { fontFamily_ = family; }
    void setFontSize(int size) { fontSize_ = size; }

    // Window settings
    int windowWidth() const { return windowWidth_; }
    int windowHeight() const { return windowHeight_; }
    bool startFullscreen() const { return startFullscreen_; }

    void setWindowWidth(int width) { windowWidth_ = width; }
    void setWindowHeight(int height) { windowHeight_ = height; }
    void setStartFullscreen(bool fullscreen) { startFullscreen_ = fullscreen; }

    // Helpers
    static QString actionToString(Action action);
    static Action stringToAction(const QString& str);

private:
    Config();
    ~Config();

    // No copy
    Config(const Config&) = delete;
    Config& operator=(const Config&) = delete;

    void setupDefaultKeybindings();

    QSettings settings_;

    // Keybindings: keySequence -> action
    QHash<QKeySequence, Action> keybindings_;

    // Appearance
    QColor backgroundColor_;
    QColor foregroundColor_;
    QColor cursorColor_;
    QColor selectionBackground_;
    QString fontFamily_;
    int fontSize_;

    // Window
    int windowWidth_;
    int windowHeight_;
    bool startFullscreen_;
};

#endif /* CONFIG_CONFIG_H */
