/*
 * Config - Configuration management implementation
 */

#include "config.h"
#include <QDebug>
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

Config::Config()
    : backgroundColor_(30, 30, 30)
    , foregroundColor_(220, 220, 220)
    , cursorColor_(220, 220, 220)
    , selectionBackground_(100, 149, 237, 128)
    , fontFamily_("Monospace")
    , fontSize_(DEFAULT_FONT_SIZE)
    , windowWidth_(DEFAULT_WINDOW_WIDTH)
    , windowHeight_(DEFAULT_WINDOW_HEIGHT)
    , windowOpacity_(DEFAULT_WINDOW_OPACITY)
    , startFullscreen_(DEFAULT_FULLSCREEN)
{
}

Config::~Config() {
}

Config& Config::instance() {
    static Config config;
    return config;
}

void Config::load() {
    // Try to find config file
    QString configPath = findConfigFile();

    if (configPath.isEmpty()) {
        qWarning() << "Config: No JSON config file found, using hardcoded defaults";
        loadDefaults();
        return;
    }

    qDebug() << "Config: Loading from JSON:" << configPath;

    QFile file(configPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "Config: Failed to open file:" << configPath;
        loadDefaults();
        return;
    }

    QByteArray data = file.readAll();
    file.close();

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);

    if (parseError.error != QJsonParseError::NoError) {
        qWarning() << "Config: JSON parse error:" << parseError.errorString();
        loadDefaults();
        return;
    }

    if (!doc.isObject()) {
        qWarning() << "Config: JSON root is not an object";
        loadDefaults();
        return;
    }

    QJsonObject config = doc.object();

    // Parse colors
    if (config.contains("colors") && config["colors"].isObject()) {
        QJsonObject colors = config["colors"].toObject();

        if (colors.contains("background")) {
            backgroundColor_ = QColor(colors["background"].toString());
        }

        if (colors.contains("foreground")) {
            foregroundColor_ = QColor(colors["foreground"].toString());
        }
    }

    // Parse font
    if (config.contains("font") && config["font"].isObject()) {
        QJsonObject font = config["font"].toObject();

        if (font.contains("family")) {
            fontFamily_ = font["family"].toString();
        }

        if (font.contains("size")) {
            fontSize_ = font["size"].toInt();
        }
    }

    // Parse window
    if (config.contains("window") && config["window"].isObject()) {
        QJsonObject window = config["window"].toObject();

        if (window.contains("width")) {
            windowWidth_ = window["width"].toInt();
        }

        if (window.contains("height")) {
            windowHeight_ = window["height"].toInt();
        }

        if (window.contains("opacity")) {
            windowOpacity_ = static_cast<float>(window["opacity"].toDouble());
        }

        if (window.contains("fullscreen")) {
            startFullscreen_ = window["fullscreen"].toBool();
        }
    }

    // Parse keybindings
    keybindings_.clear();
    if (config.contains("keybindings") && config["keybindings"].isObject()) {
        QJsonObject keybindings = config["keybindings"].toObject();

        for (auto it = keybindings.begin(); it != keybindings.end(); ++it) {
            QString keyStr = it.key();
            QString actionStr = it.value().toString();

            // Convert key format to Qt QKeySequence
            QKeySequence keySeq = parseKeySequence(keyStr);
            Action action = stringToAction(actionStr);

            if (keySeq.isEmpty()) {
                qWarning() << "Config: Invalid key sequence:" << keyStr;
                continue;
            }

            if (action == ACTION_NONE) {
                qWarning() << "Config: Unknown action:" << actionStr;
                continue;
            }

            keybindings_.insert(keySeq, action);
        }
    }

    // Set cursor and selection colors from foreground/background if not explicitly set
    cursorColor_ = foregroundColor_;
    selectionBackground_ = QColor(100, 149, 237, 128);  // Nice blue with transparency

    qDebug() << "Config: Successfully loaded" << keybindings_.size() << "keybindings";
}

void Config::save() {
    // For now, we only read from JSON
    // Future: Could save user overrides to ~/.config/ratty/config.json
    qDebug() << "Config: Save not yet implemented (JSON is read-only for now)";
}

void Config::loadDefaults() {
    backgroundColor_ = QColor(30, 30, 30);
    foregroundColor_ = QColor(220, 220, 220);
    cursorColor_ = QColor(220, 220, 220);
    selectionBackground_ = QColor(100, 149, 237, 128);
    fontFamily_ = "Monospace";
    fontSize_ = DEFAULT_FONT_SIZE;
    windowWidth_ = DEFAULT_WINDOW_WIDTH;
    windowHeight_ = DEFAULT_WINDOW_HEIGHT;
    windowOpacity_ = DEFAULT_WINDOW_OPACITY;
    startFullscreen_ = DEFAULT_FULLSCREEN;

    setupDefaultKeybindings();
}

QString Config::findConfigFile() {
    // 1. User config in ~/.config/ratty/config.json
    QString userConfigPath = QDir::homePath() + "/.config/ratty/config.json";
    if (QFile::exists(userConfigPath)) {
        return userConfigPath;
    }

    // 2. Default config in project directory (for development)
    if (QFile::exists("src/config/default_config.json")) {
        return "src/config/default_config.json";
    }

    // 3. Installed config
    QString installedPath = "/usr/local/share/ratty/default_config.json";
    if (QFile::exists(installedPath)) {
        return installedPath;
    }

    return QString();  // Not found
}

QKeySequence Config::parseKeySequence(const QString& keyStr) {
    // Convert format like "ctrl+shift+t" to Qt QKeySequence
    QString qtKey = keyStr;

    // Replace modifier names with Qt-style capitalization
    qtKey.replace("ctrl", "Ctrl", Qt::CaseInsensitive);
    qtKey.replace("shift", "Shift", Qt::CaseInsensitive);
    qtKey.replace("alt", "Alt", Qt::CaseInsensitive);
    qtKey.replace("super", "Meta", Qt::CaseInsensitive);
    qtKey.replace("meta", "Meta", Qt::CaseInsensitive);

    // Handle the last part (the actual key)
    QStringList parts = qtKey.split('+');
    if (!parts.isEmpty()) {
        QString& last = parts.last();

        // Special key names
        if (last.compare("up", Qt::CaseInsensitive) == 0) {
            last = "Up";
        } else if (last.compare("down", Qt::CaseInsensitive) == 0) {
            last = "Down";
        } else if (last.compare("left", Qt::CaseInsensitive) == 0) {
            last = "Left";
        } else if (last.compare("right", Qt::CaseInsensitive) == 0) {
            last = "Right";
        } else if (last.compare("tab", Qt::CaseInsensitive) == 0) {
            last = "Tab";
        } else if (last.compare("pageup", Qt::CaseInsensitive) == 0) {
            last = "PageUp";
        } else if (last.compare("pagedown", Qt::CaseInsensitive) == 0) {
            last = "PageDown";
        } else if (last.startsWith("f", Qt::CaseInsensitive) && last.length() > 1) {
            // Function keys (f1-f12)
            last = "F" + last.mid(1);
        } else if (last.length() == 1 && last[0].isLetter()) {
            // Single letter key
            last = last.toUpper();
        }
        // Special characters like \, -, etc. are kept as-is
    }

    return QKeySequence(parts.join('+'));
}

void Config::setupDefaultKeybindings() {
    keybindings_.clear();

    // Tab management
    keybindings_.insert(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_T), ACTION_NEW_TAB);
    keybindings_.insert(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_W), ACTION_CLOSE_TAB);
    keybindings_.insert(QKeySequence(Qt::CTRL | Qt::Key_Tab), ACTION_NEXT_TAB);
    keybindings_.insert(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Tab), ACTION_PREV_TAB);
    keybindings_.insert(QKeySequence(Qt::CTRL | Qt::Key_1), ACTION_GOTO_TAB_1);
    keybindings_.insert(QKeySequence(Qt::CTRL | Qt::Key_2), ACTION_GOTO_TAB_2);
    keybindings_.insert(QKeySequence(Qt::CTRL | Qt::Key_3), ACTION_GOTO_TAB_3);
    keybindings_.insert(QKeySequence(Qt::CTRL | Qt::Key_4), ACTION_GOTO_TAB_4);
    keybindings_.insert(QKeySequence(Qt::CTRL | Qt::Key_5), ACTION_GOTO_TAB_5);
    keybindings_.insert(QKeySequence(Qt::CTRL | Qt::Key_6), ACTION_GOTO_TAB_6);
    keybindings_.insert(QKeySequence(Qt::CTRL | Qt::Key_7), ACTION_GOTO_TAB_7);
    keybindings_.insert(QKeySequence(Qt::CTRL | Qt::Key_8), ACTION_GOTO_TAB_8);
    keybindings_.insert(QKeySequence(Qt::CTRL | Qt::Key_9), ACTION_GOTO_TAB_9);

    // Split management
    keybindings_.insert(QKeySequence(Qt::CTRL | Qt::Key_Backslash), ACTION_SPLIT_HORIZONTAL);
    keybindings_.insert(QKeySequence(Qt::CTRL | Qt::Key_Minus), ACTION_SPLIT_VERTICAL);
    keybindings_.insert(QKeySequence(Qt::CTRL | Qt::Key_W), ACTION_CLOSE_SPLIT);
    keybindings_.insert(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Up), ACTION_FOCUS_UP);
    keybindings_.insert(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Down), ACTION_FOCUS_DOWN);
    keybindings_.insert(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Left), ACTION_FOCUS_LEFT);
    keybindings_.insert(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Right), ACTION_FOCUS_RIGHT);

    // Window
    keybindings_.insert(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Q), ACTION_QUIT);
    keybindings_.insert(QKeySequence(Qt::Key_F11), ACTION_FULLSCREEN);

    // Clipboard
    keybindings_.insert(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_C), ACTION_COPY);
    keybindings_.insert(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_V), ACTION_PASTE);

    // Scrollback
    keybindings_.insert(QKeySequence(Qt::SHIFT | Qt::Key_PageUp), ACTION_SCROLL_UP);
    keybindings_.insert(QKeySequence(Qt::SHIFT | Qt::Key_PageDown), ACTION_SCROLL_DOWN);
    keybindings_.insert(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_K), ACTION_CLEAR_SCROLLBACK);

    qDebug() << "Config: Loaded default keybindings";
}

Action Config::lookupAction(const QKeySequence& keySequence) const {
    return keybindings_.value(keySequence, ACTION_NONE);
}

void Config::bindKey(const QKeySequence& keySequence, Action action) {
    if (action == ACTION_NONE) {
        keybindings_.remove(keySequence);
    } else {
        keybindings_.insert(keySequence, action);
    }
}

QKeySequence Config::getKeybinding(Action action) const {
    for (auto it = keybindings_.begin(); it != keybindings_.end(); ++it) {
        if (it.value() == action) {
            return it.key();
        }
    }
    return QKeySequence();
}

QString Config::actionToString(Action action) {
    switch (action) {
    case ACTION_NEW_TAB: return "new_tab";
    case ACTION_CLOSE_TAB: return "close_tab";
    case ACTION_NEXT_TAB: return "next_tab";
    case ACTION_PREV_TAB: return "prev_tab";
    case ACTION_GOTO_TAB_1: return "goto_tab_1";
    case ACTION_GOTO_TAB_2: return "goto_tab_2";
    case ACTION_GOTO_TAB_3: return "goto_tab_3";
    case ACTION_GOTO_TAB_4: return "goto_tab_4";
    case ACTION_GOTO_TAB_5: return "goto_tab_5";
    case ACTION_GOTO_TAB_6: return "goto_tab_6";
    case ACTION_GOTO_TAB_7: return "goto_tab_7";
    case ACTION_GOTO_TAB_8: return "goto_tab_8";
    case ACTION_GOTO_TAB_9: return "goto_tab_9";
    case ACTION_SPLIT_HORIZONTAL: return "split_horizontal";
    case ACTION_SPLIT_VERTICAL: return "split_vertical";
    case ACTION_CLOSE_SPLIT: return "close_split";
    case ACTION_FOCUS_UP: return "focus_up";
    case ACTION_FOCUS_DOWN: return "focus_down";
    case ACTION_FOCUS_LEFT: return "focus_left";
    case ACTION_FOCUS_RIGHT: return "focus_right";
    case ACTION_QUIT: return "quit";
    case ACTION_FULLSCREEN: return "fullscreen";
    case ACTION_COPY: return "copy";
    case ACTION_PASTE: return "paste";
    case ACTION_SCROLL_UP: return "scroll_up";
    case ACTION_SCROLL_DOWN: return "scroll_down";
    case ACTION_CLEAR_SCROLLBACK: return "clear_scrollback";
    default: return "none";
    }
}

Action Config::stringToAction(const QString& str) {
    if (str == "new_tab") return ACTION_NEW_TAB;
    if (str == "close_tab") return ACTION_CLOSE_TAB;
    if (str == "next_tab") return ACTION_NEXT_TAB;
    if (str == "prev_tab") return ACTION_PREV_TAB;
    if (str == "goto_tab_1") return ACTION_GOTO_TAB_1;
    if (str == "goto_tab_2") return ACTION_GOTO_TAB_2;
    if (str == "goto_tab_3") return ACTION_GOTO_TAB_3;
    if (str == "goto_tab_4") return ACTION_GOTO_TAB_4;
    if (str == "goto_tab_5") return ACTION_GOTO_TAB_5;
    if (str == "goto_tab_6") return ACTION_GOTO_TAB_6;
    if (str == "goto_tab_7") return ACTION_GOTO_TAB_7;
    if (str == "goto_tab_8") return ACTION_GOTO_TAB_8;
    if (str == "goto_tab_9") return ACTION_GOTO_TAB_9;
    if (str == "split_horizontal") return ACTION_SPLIT_HORIZONTAL;
    if (str == "split_vertical") return ACTION_SPLIT_VERTICAL;
    if (str == "close_split") return ACTION_CLOSE_SPLIT;
    if (str == "focus_up") return ACTION_FOCUS_UP;
    if (str == "focus_down") return ACTION_FOCUS_DOWN;
    if (str == "focus_left") return ACTION_FOCUS_LEFT;
    if (str == "focus_right") return ACTION_FOCUS_RIGHT;
    if (str == "quit") return ACTION_QUIT;
    if (str == "fullscreen") return ACTION_FULLSCREEN;
    if (str == "copy") return ACTION_COPY;
    if (str == "paste") return ACTION_PASTE;
    if (str == "scroll_up") return ACTION_SCROLL_UP;
    if (str == "scroll_down") return ACTION_SCROLL_DOWN;
    if (str == "clear_scrollback") return ACTION_CLEAR_SCROLLBACK;
    return ACTION_NONE;
}
