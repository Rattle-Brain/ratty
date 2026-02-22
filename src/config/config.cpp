/*
 * Config - Configuration management implementation
 */

#include "config.h"
#include <QDebug>

Config::Config()
    : settings_("Ratty", "RattyTerminal")
    , backgroundColor_(30, 30, 30)
    , foregroundColor_(220, 220, 220)
    , cursorColor_(220, 220, 220)
    , selectionBackground_(100, 149, 237, 128)
    , fontFamily_("Monospace")
    , fontSize_(14)
    , windowWidth_(1024)
    , windowHeight_(768)
    , startFullscreen_(false)
{
}

Config::~Config() {
}

Config& Config::instance() {
    static Config config;
    return config;
}

void Config::load() {
    settings_.beginGroup("Appearance");
    backgroundColor_ = settings_.value("backgroundColor", backgroundColor_).value<QColor>();
    foregroundColor_ = settings_.value("foregroundColor", foregroundColor_).value<QColor>();
    cursorColor_ = settings_.value("cursorColor", cursorColor_).value<QColor>();
    selectionBackground_ = settings_.value("selectionBackground", selectionBackground_).value<QColor>();
    fontFamily_ = settings_.value("fontFamily", fontFamily_).toString();
    fontSize_ = settings_.value("fontSize", fontSize_).toInt();
    settings_.endGroup();

    settings_.beginGroup("Window");
    windowWidth_ = settings_.value("width", windowWidth_).toInt();
    windowHeight_ = settings_.value("height", windowHeight_).toInt();
    startFullscreen_ = settings_.value("fullscreen", startFullscreen_).toBool();
    settings_.endGroup();

    // Load keybindings
    settings_.beginGroup("Keybindings");
    keybindings_.clear();
    QStringList keys = settings_.childKeys();
    for (const QString& key : keys) {
        QString actionStr = settings_.value(key).toString();
        Action action = stringToAction(actionStr);
        if (action != ACTION_NONE) {
            QKeySequence keySeq(key);
            keybindings_.insert(keySeq, action);
        }
    }
    settings_.endGroup();

    // If no keybindings loaded, use defaults
    if (keybindings_.isEmpty()) {
        setupDefaultKeybindings();
    }

    qDebug() << "Config: Loaded" << keybindings_.size() << "keybindings";
}

void Config::save() {
    settings_.beginGroup("Appearance");
    settings_.setValue("backgroundColor", backgroundColor_);
    settings_.setValue("foregroundColor", foregroundColor_);
    settings_.setValue("cursorColor", cursorColor_);
    settings_.setValue("selectionBackground", selectionBackground_);
    settings_.setValue("fontFamily", fontFamily_);
    settings_.setValue("fontSize", fontSize_);
    settings_.endGroup();

    settings_.beginGroup("Window");
    settings_.setValue("width", windowWidth_);
    settings_.setValue("height", windowHeight_);
    settings_.setValue("fullscreen", startFullscreen_);
    settings_.endGroup();

    settings_.beginGroup("Keybindings");
    settings_.remove("");  // Clear existing
    for (auto it = keybindings_.begin(); it != keybindings_.end(); ++it) {
        settings_.setValue(it.key().toString(), actionToString(it.value()));
    }
    settings_.endGroup();

    settings_.sync();
    qDebug() << "Config: Saved";
}

void Config::loadDefaults() {
    backgroundColor_ = QColor(30, 30, 30);
    foregroundColor_ = QColor(220, 220, 220);
    cursorColor_ = QColor(220, 220, 220);
    selectionBackground_ = QColor(100, 149, 237, 128);
    fontFamily_ = "Monospace";
    fontSize_ = 14;
    windowWidth_ = 1024;
    windowHeight_ = 768;
    startFullscreen_ = false;

    setupDefaultKeybindings();
}

void Config::setupDefaultKeybindings() {
    keybindings_.clear();

    // Tab management
    keybindings_.insert(QKeySequence(Qt::CTRL | Qt::Key_T), ACTION_NEW_TAB);
    keybindings_.insert(QKeySequence(Qt::CTRL | Qt::Key_W), ACTION_CLOSE_TAB);
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
    keybindings_.insert(QKeySequence(Qt::CTRL | Qt::Key_D), ACTION_SPLIT_HORIZONTAL);
    keybindings_.insert(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_D), ACTION_SPLIT_VERTICAL);
    keybindings_.insert(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_W), ACTION_CLOSE_SPLIT);
    keybindings_.insert(QKeySequence(Qt::ALT | Qt::Key_Up), ACTION_FOCUS_UP);
    keybindings_.insert(QKeySequence(Qt::ALT | Qt::Key_Down), ACTION_FOCUS_DOWN);
    keybindings_.insert(QKeySequence(Qt::ALT | Qt::Key_Left), ACTION_FOCUS_LEFT);
    keybindings_.insert(QKeySequence(Qt::ALT | Qt::Key_Right), ACTION_FOCUS_RIGHT);

    // Window
    keybindings_.insert(QKeySequence(Qt::CTRL | Qt::Key_Q), ACTION_QUIT);
    keybindings_.insert(QKeySequence(Qt::Key_F11), ACTION_FULLSCREEN);

    // Clipboard
    keybindings_.insert(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_C), ACTION_COPY);
    keybindings_.insert(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_V), ACTION_PASTE);

    // Scrollback
    keybindings_.insert(QKeySequence(Qt::SHIFT | Qt::Key_PageUp), ACTION_SCROLL_UP);
    keybindings_.insert(QKeySequence(Qt::SHIFT | Qt::Key_PageDown), ACTION_SCROLL_DOWN);
    keybindings_.insert(QKeySequence(Qt::CTRL | Qt::Key_L), ACTION_CLEAR_SCROLLBACK);

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
