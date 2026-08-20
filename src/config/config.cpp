/*
 * Config - settings implementation
 */

#include "config.h"
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QKeyCombination>
#include <QKeyEvent>
#include <algorithm>

namespace {

/* Bundled defaults, compiled into the binary via resources/config.qrc. */
const char kBundledDefaultsPath[] = ":/config/default_config.json";

struct ActionName {
    Action action;
    const char* name;
};

/* One table instead of the two hand-written switch/if-chains that previously
 * had to be kept in sync by hand. */
constexpr ActionName kActionNames[] = {
    {ACTION_NEW_TAB,             "new_tab"},
    {ACTION_CLOSE_TAB,           "close_tab"},
    {ACTION_NEXT_TAB,            "next_tab"},
    {ACTION_PREV_TAB,            "prev_tab"},
    {ACTION_GOTO_TAB_1,          "goto_tab_1"},
    {ACTION_GOTO_TAB_2,          "goto_tab_2"},
    {ACTION_GOTO_TAB_3,          "goto_tab_3"},
    {ACTION_GOTO_TAB_4,          "goto_tab_4"},
    {ACTION_GOTO_TAB_5,          "goto_tab_5"},
    {ACTION_GOTO_TAB_6,          "goto_tab_6"},
    {ACTION_GOTO_TAB_7,          "goto_tab_7"},
    {ACTION_GOTO_TAB_8,          "goto_tab_8"},
    {ACTION_GOTO_TAB_9,          "goto_tab_9"},
    {ACTION_SPLIT_HORIZONTAL,    "split_horizontal"},
    {ACTION_SPLIT_VERTICAL,      "split_vertical"},
    {ACTION_CLOSE_SPLIT,         "close_split"},
    {ACTION_FOCUS_UP,            "focus_up"},
    {ACTION_FOCUS_DOWN,          "focus_down"},
    {ACTION_FOCUS_LEFT,          "focus_left"},
    {ACTION_FOCUS_RIGHT,         "focus_right"},
    {ACTION_QUIT,                "quit"},
    {ACTION_FULLSCREEN,          "fullscreen"},
    {ACTION_COPY,                "copy"},
    {ACTION_PASTE,               "paste"},
    {ACTION_INCREASE_FONT_SIZE,  "increase_font_size"},
    {ACTION_DECREASE_FONT_SIZE,  "decrease_font_size"},
    {ACTION_RESET_FONT_SIZE,     "reset_font_size"},
    {ACTION_SCROLL_UP,           "scroll_up"},
    {ACTION_SCROLL_DOWN,         "scroll_down"},
    {ACTION_CLEAR_SCROLLBACK,    "clear_scrollback"},
};

/* Named colour keys accepted under "colors", mapped to palette slots 0-15. */
constexpr const char* kAnsiColorKeys[16] = {
    "black", "red", "green", "yellow", "blue", "magenta", "cyan", "white",
    "bright_black", "bright_red", "bright_green", "bright_yellow",
    "bright_blue", "bright_magenta", "bright_cyan", "bright_white",
};

QColor readColor(const QJsonObject& object, const char* key, const QColor& fallback) {
    const QJsonValue value = object.value(QLatin1String(key));
    if (!value.isString()) return fallback;

    const QColor color(value.toString());
    if (!color.isValid()) {
        qWarning() << "Config: invalid colour for" << key << ":" << value.toString();
        return fallback;
    }
    return color;
}

} // namespace

Config::Config() {
    applyBuiltInDefaults();
}

Config& Config::instance() {
    static Config config;
    return config;
}

void Config::applyBuiltInDefaults() {
    /* Palette's own constructor already carries the standard ANSI colours and
     * a sensible dark default, so there is nothing to duplicate here. */
    palette_ = Palette();
    /* Empty == "ask the platform for its monospaced default". */
    fontFamilies_.clear();
    fontFallbacks_.clear();
    fontSize_ = DEFAULT_FONT_SIZE;
    windowPadding_ = DEFAULT_WINDOW_PADDING;
    cursorStyle_ = CursorStyle::Block;
    cursorBlink_ = true;
    windowWidth_ = DEFAULT_WINDOW_WIDTH;
    windowHeight_ = DEFAULT_WINDOW_HEIGHT;
    windowOpacity_ = 1.0f;
    startFullscreen_ = false;
    keybindings_.clear();
}

QString Config::userConfigPath() {
    return QDir::homePath() + QStringLiteral("/.config/ratty/config.json");
}

void Config::load() {
    applyBuiltInDefaults();

    if (!applyJsonFile(QString::fromLatin1(kBundledDefaultsPath))) {
        qWarning() << "Config: bundled defaults missing from resources";
    }

    const QString userPath = userConfigPath();
    if (QFile::exists(userPath)) {
        if (applyJsonFile(userPath)) {
            qInfo() << "Config: loaded user overrides from" << userPath;
        }
    }

    qInfo() << "Config: font preference"
            << (fontFamilies_.isEmpty() ? QStringLiteral("<system monospace>")
                                        : fontFamilies_.join(QStringLiteral(" > ")))
            << "at" << fontSize_ << "pt,"
            << "padding" << windowPadding_ << "px,"
            << keybindings_.size() << "keybindings";
}

bool Config::applyJsonFile(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }

    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);

    if (parseError.error != QJsonParseError::NoError) {
        qWarning() << "Config:" << path << "-" << parseError.errorString()
                   << "at offset" << parseError.offset;
        return false;
    }
    if (!document.isObject()) {
        qWarning() << "Config:" << path << "- root value is not an object";
        return false;
    }

    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("colors")).isObject()) {
        applyColors(root.value(QStringLiteral("colors")).toObject());
    }
    if (root.value(QStringLiteral("font")).isObject()) {
        applyFont(root.value(QStringLiteral("font")).toObject());
    }
    if (root.value(QStringLiteral("cursor")).isObject()) {
        applyCursor(root.value(QStringLiteral("cursor")).toObject());
    }
    if (root.value(QStringLiteral("window")).isObject()) {
        applyWindow(root.value(QStringLiteral("window")).toObject());
    }
    if (root.value(QStringLiteral("keybindings")).isObject()) {
        applyKeybindings(root.value(QStringLiteral("keybindings")).toObject());
    }
    return true;
}

void Config::applyColors(const QJsonObject& colors) {
    palette_.setDefaultBackground(readColor(colors, "background", palette_.defaultBackground()));
    palette_.setDefaultForeground(readColor(colors, "foreground", palette_.defaultForeground()));
    /* The cursor defaults to the foreground colour unless overridden, which is
     * what users expect from a "foreground: ..." only config. */
    palette_.setCursorColor(readColor(colors, "cursor", palette_.defaultForeground()));
    palette_.setSelectionBackground(
        readColor(colors, "selection_background", palette_.selectionBackground()));

    for (int i = 0; i < 16; ++i) {
        const QColor color = readColor(colors, kAnsiColorKeys[i], palette_.entry(i));
        palette_.setEntry(i, color);
    }
}

void Config::applyFont(const QJsonObject& font) {
    const QJsonValue family = font.value(QStringLiteral("family"));

    /*
     * "family" accepts a single name or an array of names to try in order. The
     * array form is how a config can ask for a preferred font and still degrade
     * gracefully on a machine where it is not installed.
     */
    auto normalize = [](const QString& name) -> QString {
        const QString trimmed = name.trimmed();
        /* "Monospace" is a fontconfig alias rather than a real family, so treat
         * it the same as an empty string: let the platform decide. */
        if (trimmed.compare(QLatin1String("monospace"), Qt::CaseInsensitive) == 0) {
            return QString();
        }
        return trimmed;
    };

    if (family.isString()) {
        fontFamilies_.clear();
        if (const QString name = normalize(family.toString()); !name.isEmpty()) {
            fontFamilies_ << name;
        }
    } else if (family.isArray()) {
        fontFamilies_.clear();
        for (const QJsonValue& entry : family.toArray()) {
            if (!entry.isString()) continue;
            if (const QString name = normalize(entry.toString()); !name.isEmpty()) {
                fontFamilies_ << name;
            }
        }
    }

    /* Fallbacks are always a list; a bare string is accepted for convenience. */
    const QJsonValue fallback = font.value(QStringLiteral("fallback"));
    if (fallback.isString()) {
        fontFallbacks_.clear();
        if (const QString name = normalize(fallback.toString()); !name.isEmpty()) {
            fontFallbacks_ << name;
        }
    } else if (fallback.isArray()) {
        fontFallbacks_.clear();
        for (const QJsonValue& entry : fallback.toArray()) {
            if (!entry.isString()) continue;
            if (const QString name = normalize(entry.toString()); !name.isEmpty()) {
                fontFallbacks_ << name;
            }
        }
    }

    if (font.value(QStringLiteral("size")).isDouble()) {
        setFontSize(font.value(QStringLiteral("size")).toInt(fontSize_));
    }
}

void Config::applyCursor(const QJsonObject& cursor) {
    const QString style = cursor.value(QStringLiteral("style")).toString().toLower();
    if (style == QLatin1String("block"))          cursorStyle_ = CursorStyle::Block;
    else if (style == QLatin1String("hollow"))    cursorStyle_ = CursorStyle::HollowBlock;
    else if (style == QLatin1String("underline")) cursorStyle_ = CursorStyle::Underline;
    else if (style == QLatin1String("bar") || style == QLatin1String("beam")) {
        cursorStyle_ = CursorStyle::Bar;
    } else if (!style.isEmpty()) {
        qWarning() << "Config: unknown cursor style" << style;
    }

    if (cursor.value(QStringLiteral("blink")).isBool()) {
        cursorBlink_ = cursor.value(QStringLiteral("blink")).toBool();
    }
}

void Config::applyWindow(const QJsonObject& window) {
    if (window.value(QStringLiteral("width")).isDouble()) {
        windowWidth_ = std::max(200, window.value(QStringLiteral("width")).toInt(windowWidth_));
    }
    if (window.value(QStringLiteral("height")).isDouble()) {
        windowHeight_ = std::max(150, window.value(QStringLiteral("height")).toInt(windowHeight_));
    }
    if (window.value(QStringLiteral("opacity")).isDouble()) {
        windowOpacity_ = std::clamp(
            static_cast<float>(window.value(QStringLiteral("opacity")).toDouble(windowOpacity_)),
            0.1f, 1.0f);
    }
    if (window.value(QStringLiteral("fullscreen")).isBool()) {
        startFullscreen_ = window.value(QStringLiteral("fullscreen")).toBool();
    }
    if (window.value(QStringLiteral("padding")).isDouble()) {
        windowPadding_ = std::clamp(
            window.value(QStringLiteral("padding")).toInt(windowPadding_),
            0, MAX_WINDOW_PADDING);
    }
}

void Config::applyKeybindings(const QJsonObject& keybindings) {
    for (auto it = keybindings.begin(); it != keybindings.end(); ++it) {
        const QKeySequence sequence = parseKeySequence(it.key());
        if (sequence.isEmpty()) {
            qWarning() << "Config: unparseable key sequence" << it.key();
            continue;
        }

        const QString actionName = it.value().toString();
        /* An explicit "none" unbinds a default, which is the only way for a
         * user overlay to remove a binding it did not create. */
        if (actionName.compare(QLatin1String("none"), Qt::CaseInsensitive) == 0) {
            keybindings_.remove(sequence);
            continue;
        }

        const Action action = stringToAction(actionName);
        if (action == ACTION_NONE) {
            qWarning() << "Config: unknown action" << actionName << "for" << it.key();
            continue;
        }
        keybindings_.insert(sequence, action);
    }
}

QKeySequence Config::parseKeySequence(const QString& text) {
    /* Normalise "ctrl+shift+t" into the spelling QKeySequence expects. */
    QStringList parts = text.split(QLatin1Char('+'), Qt::SkipEmptyParts);

    /* A trailing literal '+' ("ctrl++") splits into an empty last element. */
    if (text.endsWith(QLatin1Char('+')) && !text.endsWith(QLatin1String("++"))) {
        parts.append(QStringLiteral("+"));
    }
    if (parts.isEmpty()) return QKeySequence();

    static const QHash<QString, QString> modifiers = {
        {QStringLiteral("ctrl"), QStringLiteral("Ctrl")},
        {QStringLiteral("control"), QStringLiteral("Ctrl")},
        {QStringLiteral("shift"), QStringLiteral("Shift")},
        {QStringLiteral("alt"), QStringLiteral("Alt")},
        {QStringLiteral("option"), QStringLiteral("Alt")},
        {QStringLiteral("meta"), QStringLiteral("Meta")},
        {QStringLiteral("super"), QStringLiteral("Meta")},
        {QStringLiteral("cmd"), QStringLiteral("Meta")},
    };
    static const QHash<QString, QString> namedKeys = {
        {QStringLiteral("up"), QStringLiteral("Up")},
        {QStringLiteral("down"), QStringLiteral("Down")},
        {QStringLiteral("left"), QStringLiteral("Left")},
        {QStringLiteral("right"), QStringLiteral("Right")},
        {QStringLiteral("tab"), QStringLiteral("Tab")},
        {QStringLiteral("backtab"), QStringLiteral("Backtab")},
        {QStringLiteral("return"), QStringLiteral("Return")},
        {QStringLiteral("enter"), QStringLiteral("Enter")},
        {QStringLiteral("space"), QStringLiteral("Space")},
        {QStringLiteral("home"), QStringLiteral("Home")},
        {QStringLiteral("end"), QStringLiteral("End")},
        {QStringLiteral("pageup"), QStringLiteral("PgUp")},
        {QStringLiteral("pagedown"), QStringLiteral("PgDown")},
        {QStringLiteral("insert"), QStringLiteral("Ins")},
        {QStringLiteral("delete"), QStringLiteral("Del")},
        {QStringLiteral("backspace"), QStringLiteral("Backspace")},
        {QStringLiteral("escape"), QStringLiteral("Esc")},
        /* Punctuation spelled out, because "ctrl+shift++" cannot be split on
         * '+' unambiguously and QKeySequence does not know these names. */
        {QStringLiteral("plus"), QStringLiteral("+")},
        {QStringLiteral("minus"), QStringLiteral("-")},
        {QStringLiteral("underscore"), QStringLiteral("_")},
        {QStringLiteral("equal"), QStringLiteral("=")},
        {QStringLiteral("backslash"), QStringLiteral("\\")},
        {QStringLiteral("slash"), QStringLiteral("/")},
        {QStringLiteral("comma"), QStringLiteral(",")},
        {QStringLiteral("period"), QStringLiteral(".")},
        {QStringLiteral("semicolon"), QStringLiteral(";")},
        {QStringLiteral("grave"), QStringLiteral("`")},
        {QStringLiteral("bracketleft"), QStringLiteral("[")},
        {QStringLiteral("bracketright"), QStringLiteral("]")},
    };

    for (int i = 0; i < parts.size(); ++i) {
        const QString lowered = parts[i].toLower();
        const bool isLast = (i == parts.size() - 1);

        if (!isLast) {
            parts[i] = modifiers.value(lowered, parts[i]);
            continue;
        }

        if (const auto named = namedKeys.constFind(lowered); named != namedKeys.constEnd()) {
            parts[i] = *named;
        } else if (lowered.size() > 1 && lowered.startsWith(QLatin1Char('f'))
                   && lowered.mid(1).toInt() > 0) {
            parts[i] = QStringLiteral("F") + lowered.mid(1);
        } else if (lowered.size() == 1 && lowered[0].isLetter()) {
            parts[i] = lowered.toUpper();
        }
    }

    return QKeySequence(parts.join(QLatin1Char('+')));
}

Action Config::lookupAction(const QKeySequence& keySequence) const {
    return keybindings_.value(keySequence, ACTION_NONE);
}

Qt::Key Config::shiftPartner(int key) {
    /* US-layout shift pairs. Not exhaustive across every layout, but it covers
     * the keys shortcuts are conventionally placed on. */
    static const QHash<int, Qt::Key> partners = {
        {Qt::Key_1, Qt::Key_Exclam},        {Qt::Key_Exclam, Qt::Key_1},
        {Qt::Key_2, Qt::Key_At},            {Qt::Key_At, Qt::Key_2},
        {Qt::Key_3, Qt::Key_NumberSign},    {Qt::Key_NumberSign, Qt::Key_3},
        {Qt::Key_4, Qt::Key_Dollar},        {Qt::Key_Dollar, Qt::Key_4},
        {Qt::Key_5, Qt::Key_Percent},       {Qt::Key_Percent, Qt::Key_5},
        {Qt::Key_6, Qt::Key_AsciiCircum},   {Qt::Key_AsciiCircum, Qt::Key_6},
        {Qt::Key_7, Qt::Key_Ampersand},     {Qt::Key_Ampersand, Qt::Key_7},
        {Qt::Key_8, Qt::Key_Asterisk},      {Qt::Key_Asterisk, Qt::Key_8},
        {Qt::Key_9, Qt::Key_ParenLeft},     {Qt::Key_ParenLeft, Qt::Key_9},
        {Qt::Key_0, Qt::Key_ParenRight},    {Qt::Key_ParenRight, Qt::Key_0},
        {Qt::Key_Minus, Qt::Key_Underscore},{Qt::Key_Underscore, Qt::Key_Minus},
        {Qt::Key_Equal, Qt::Key_Plus},      {Qt::Key_Plus, Qt::Key_Equal},
        {Qt::Key_Backslash, Qt::Key_Bar},   {Qt::Key_Bar, Qt::Key_Backslash},
        {Qt::Key_BracketLeft, Qt::Key_BraceLeft},
        {Qt::Key_BraceLeft, Qt::Key_BracketLeft},
        {Qt::Key_BracketRight, Qt::Key_BraceRight},
        {Qt::Key_BraceRight, Qt::Key_BracketRight},
        {Qt::Key_Semicolon, Qt::Key_Colon}, {Qt::Key_Colon, Qt::Key_Semicolon},
        {Qt::Key_Comma, Qt::Key_Less},      {Qt::Key_Less, Qt::Key_Comma},
        {Qt::Key_Period, Qt::Key_Greater},  {Qt::Key_Greater, Qt::Key_Period},
        {Qt::Key_Slash, Qt::Key_Question},  {Qt::Key_Question, Qt::Key_Slash},
        {Qt::Key_QuoteLeft, Qt::Key_AsciiTilde},
        {Qt::Key_AsciiTilde, Qt::Key_QuoteLeft},
        {Qt::Key_Apostrophe, Qt::Key_QuoteDbl},
        {Qt::Key_QuoteDbl, Qt::Key_Apostrophe},
    };
    return partners.value(key, Qt::Key_unknown);
}

Action Config::lookupAction(const QKeyEvent* event) const {
    if (!event) return ACTION_NONE;

    const Qt::KeyboardModifiers modifiers = event->modifiers();

    if (const Action action = lookupAction(
            QKeySequence(QKeyCombination(modifiers, static_cast<Qt::Key>(event->key()))));
        action != ACTION_NONE) {
        return action;
    }

    /*
     * Only worth retrying when Shift is held: without it there is no ambiguity
     * about which of the two symbols on the key was meant, and rewriting
     * unshifted keys risks turning Ctrl+C into a shortcut.
     */
    if (!(modifiers & Qt::ShiftModifier)) return ACTION_NONE;

    const Qt::Key partner = shiftPartner(event->key());
    if (partner == Qt::Key_unknown) return ACTION_NONE;

    return lookupAction(QKeySequence(QKeyCombination(modifiers, partner)));
}

bool Config::isBound(const QKeyEvent* event) const {
    return lookupAction(event) != ACTION_NONE;
}

bool Config::isBound(const QKeySequence& keySequence) const {
    return keybindings_.contains(keySequence);
}

QKeySequence Config::keybindingFor(Action action) const {
    for (auto it = keybindings_.constBegin(); it != keybindings_.constEnd(); ++it) {
        if (it.value() == action) return it.key();
    }
    return QKeySequence();
}

void Config::setFontSize(int size) {
    fontSize_ = std::clamp(size, MIN_FONT_SIZE, MAX_FONT_SIZE);
}

QString Config::actionToString(Action action) {
    for (const ActionName& entry : kActionNames) {
        if (entry.action == action) return QString::fromLatin1(entry.name);
    }
    return QStringLiteral("none");
}

Action Config::stringToAction(const QString& text) {
    for (const ActionName& entry : kActionNames) {
        if (text == QLatin1String(entry.name)) return entry.action;
    }
    return ACTION_NONE;
}
