/*
 * Config - settings implementation
 */

#include "config.h"
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QKeyCombination>
#include <QKeyEvent>
#include <yaml-cpp/yaml.h>
#include <algorithm>
#include <optional>

/* ------------------------------------------------------- StartDirectory */

StartDirectory StartDirectory::fromString(const QString& text) {
    const QString trimmed = text.trimmed();

    if (trimmed.isEmpty()
        || trimmed.compare(QLatin1String("home"), Qt::CaseInsensitive) == 0) {
        return {Kind::Home, QString()};
    }
    /* "inherit" reads better in some configurations and means the same thing. */
    if (trimmed.compare(QLatin1String("cwd"), Qt::CaseInsensitive) == 0
        || trimmed.compare(QLatin1String("inherit"), Qt::CaseInsensitive) == 0) {
        return {Kind::Cwd, QString()};
    }

    /* Anything else is a path. `~` is expanded here rather than at use, so what
     * resolve() holds is always something chdir() can be given directly. */
    QString path = trimmed;
    if (path == QLatin1String("~")) {
        path = QDir::homePath();
    } else if (path.startsWith(QLatin1String("~/"))) {
        path = QDir::homePath() + path.mid(1);
    }
    return {Kind::Custom, QDir::cleanPath(path)};
}

QString StartDirectory::resolve(const QString& inherited) const {
    switch (kind) {
    case Kind::Cwd:
        if (!inherited.isEmpty() && QFileInfo(inherited).isDir()) return inherited;
        break;
    case Kind::Custom:
        if (!path.isEmpty() && QFileInfo(path).isDir()) return path;
        if (!path.isEmpty()) {
            qWarning() << "Config: start directory" << path
                       << "does not exist; using the home directory";
        }
        break;
    case Kind::Home:
        break;
    }
    return QDir::homePath();
}

namespace {

/* Bundled defaults, compiled into the binary via resources/config.qrc. */
const char kBundledDefaultsPath[] = ":/config/default_config.yaml";

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

/*
 * Scalar readers. Each returns nullopt when the key is absent or unusable, so a
 * layer only overrides what it actually specifies.
 */

std::optional<QString> readString(const YAML::Node& node, const char* key) {
    const YAML::Node value = node[key];
    if (!value || !value.IsScalar()) return std::nullopt;
    return QString::fromStdString(value.Scalar()).trimmed();
}

std::optional<int> readInt(const YAML::Node& node, const char* key) {
    const YAML::Node value = node[key];
    if (!value || !value.IsScalar()) return std::nullopt;
    try {
        return value.as<int>();
    } catch (const YAML::Exception&) {
        qWarning() << "Config:" << key << "should be a whole number, got"
                   << QString::fromStdString(value.Scalar());
        return std::nullopt;
    }
}

std::optional<double> readDouble(const YAML::Node& node, const char* key) {
    const YAML::Node value = node[key];
    if (!value || !value.IsScalar()) return std::nullopt;
    try {
        return value.as<double>();
    } catch (const YAML::Exception&) {
        qWarning() << "Config:" << key << "should be a number, got"
                   << QString::fromStdString(value.Scalar());
        return std::nullopt;
    }
}

std::optional<bool> readBool(const YAML::Node& node, const char* key) {
    const YAML::Node value = node[key];
    if (!value || !value.IsScalar()) return std::nullopt;
    try {
        return value.as<bool>();
    } catch (const YAML::Exception&) {
        qWarning() << "Config:" << key << "should be true or false, got"
                   << QString::fromStdString(value.Scalar());
        return std::nullopt;
    }
}

/* A single name or a sequence of them, which is how font families are given. */
std::optional<QStringList> readStringList(const YAML::Node& node, const char* key) {
    const YAML::Node value = node[key];
    if (!value) return std::nullopt;

    QStringList result;
    if (value.IsScalar()) {
        result << QString::fromStdString(value.Scalar()).trimmed();
        return result;
    }
    if (value.IsSequence()) {
        for (const YAML::Node& entry : value) {
            if (!entry.IsScalar()) continue;
            const QString name = QString::fromStdString(entry.Scalar()).trimmed();
            if (!name.isEmpty()) result << name;
        }
        return result;
    }
    return std::nullopt;
}

std::optional<QColor> readColor(const YAML::Node& node, const char* key) {
    const YAML::Node value = node[key];
    if (!value) return std::nullopt;

    /*
     * The commonest YAML mistake by far, and one worth naming explicitly: '#'
     * starts a comment, so `background: #1e1e1e` parses as an empty value rather
     * than a colour.
     */
    if (value.IsNull() || (value.IsScalar() && value.Scalar().empty())) {
        qWarning() << "Config: colour" << key
                   << "is empty - hex colours must be quoted in YAML, e.g. \"#1e1e1e\"";
        return std::nullopt;
    }
    if (!value.IsScalar()) return std::nullopt;

    const QString text = QString::fromStdString(value.Scalar()).trimmed();
    const QColor color(text);
    if (!color.isValid()) {
        qWarning() << "Config: invalid colour for" << key << ":" << text;
        return std::nullopt;
    }
    return color;
}

} // namespace

void Config::BindingLayer::absorb(const BindingLayer& later) {
    for (const QKeySequence& sequence : later.unbound) {
        bound.remove(sequence);
        unbound.append(sequence);
    }
    for (auto it = later.bound.constBegin(); it != later.bound.constEnd(); ++it) {
        bound.insert(it.key(), it.value());
    }
    assignedActions.unite(later.assignedActions);
}

/*
 * Parser - one YAML document applied over the current settings.
 *
 * Split out as a nested type so the YAML dependency stays inside this file, and
 * so each section reads as a flat list of "if the key is present, take it".
 */
struct Config::Parser {
    Config& config;
    BindingLayer& bindings;
    PaletteOverrides& colorOverrides;
    ChromeColors& chromeOverrides;

    void apply(const YAML::Node& root) {
        if (const auto theme = readString(root, "theme")) {
            config.themeName_ = *theme;
        }
        if (const YAML::Node node = root["colors"]; node && node.IsMap()) colors(node);
        if (const YAML::Node node = root["font"]; node && node.IsMap()) font(node);
        if (const YAML::Node node = root["cursor"]; node && node.IsMap()) cursor(node);
        if (const YAML::Node node = root["window"]; node && node.IsMap()) window(node);
        if (const YAML::Node node = root["scrollback"]; node && node.IsMap()) scrollback(node);
        if (const YAML::Node node = root["directories"]; node && node.IsMap()) {
            directories(node);
        }
        if (const YAML::Node node = root["mouse"]; node && node.IsMap()) mouse(node);
        if (const YAML::Node node = root["tab_bar"]; node && node.IsMap()) tabBar(node);
        if (const YAML::Node node = root["mac_os_bindings"]; node) {
            platformBindings(node);
        }
        if (const YAML::Node node = root["keybindings"]; node && node.IsMap()) {
            keybindings(node, bindings);
        }
    }

    /* `mac_os_bindings` accepts true, false, or "auto" to follow the platform. */
    void platformBindings(const YAML::Node& node) {
        if (!node.IsScalar()) return;

        const QString text = QString::fromStdString(node.Scalar()).trimmed().toLower();
        if (text == QLatin1String("auto") || text.isEmpty()) {
            config.macOsBindingsOverride_.reset();
            return;
        }
        try {
            config.macOsBindingsOverride_ = node.as<bool>();
        } catch (const YAML::Exception&) {
            qWarning() << "Config: mac_os_bindings should be true, false or auto, got"
                       << text;
        }
    }

    void colors(const YAML::Node& node) {
        /* Staged, not applied: the theme is not known until every layer has been
         * read. See Config::resolvePalette(). */
        if (const auto color = readColor(node, "background")) colorOverrides.background = *color;
        if (const auto color = readColor(node, "foreground")) colorOverrides.foreground = *color;
        if (const auto color = readColor(node, "cursor")) colorOverrides.cursor = *color;
        if (const auto color = readColor(node, "selection_background")) {
            colorOverrides.selectionBackground = *color;
        }

        for (int i = 0; i < PaletteOverrides::AnsiCount; ++i) {
            if (const auto color = readColor(node, kAnsiColorKeys[i])) {
                colorOverrides.ansi[static_cast<size_t>(i)] = *color;
            }
        }
    }

    void font(const YAML::Node& node) {
        /* "Monospace" is a fontconfig alias rather than a real family; treat it
         * and the empty string alike as "let the platform decide". */
        auto normalize = [](const QStringList& names) {
            QStringList result;
            for (const QString& name : names) {
                if (name.isEmpty()) continue;
                if (name.compare(QLatin1String("monospace"), Qt::CaseInsensitive) == 0) {
                    continue;
                }
                result << name;
            }
            return result;
        };

        if (const auto families = readStringList(node, "family")) {
            config.fontFamilies_ = normalize(*families);
        }
        if (const auto fallbacks = readStringList(node, "fallback")) {
            config.fontFallbacks_ = normalize(*fallbacks);
        }
        if (const auto size = readInt(node, "size")) {
            config.setFontSize(*size);
        }
    }

    void tabBar(const YAML::Node& node) {
        if (const auto style = readString(node, "style")) {
            const QString lowered = style->toLower();
            if (lowered == QLatin1String("minimal"))        config.tabBarStyle_ = TabBarStyle::Minimal;
            else if (lowered == QLatin1String("underline")) config.tabBarStyle_ = TabBarStyle::Underline;
            else if (lowered == QLatin1String("blocks"))    config.tabBarStyle_ = TabBarStyle::Blocks;
            else if (lowered == QLatin1String("pills"))     config.tabBarStyle_ = TabBarStyle::Pills;
            else if (lowered == QLatin1String("powerline")) config.tabBarStyle_ = TabBarStyle::Powerline;
            else if (!lowered.isEmpty()) {
                qWarning() << "Config: unknown tab bar style" << *style
                           << "- expected minimal, underline, blocks, pills or powerline";
            }
        }

        if (const auto position = readString(node, "position")) {
            const QString lowered = position->toLower();
            if (lowered == QLatin1String("bottom"))   config.tabBarPosition_ = TabBarPosition::Bottom;
            else if (lowered == QLatin1String("top")) config.tabBarPosition_ = TabBarPosition::Top;
            else if (!lowered.isEmpty()) {
                qWarning() << "Config: tab bar position should be top or bottom, got"
                           << *position;
            }
        }

        if (const auto show = readString(node, "show")) {
            const QString lowered = show->toLower();
            if (lowered == QLatin1String("always"))            config.tabBarVisibility_ = TabBarVisibility::Always;
            else if (lowered == QLatin1String("multiple"))     config.tabBarVisibility_ = TabBarVisibility::MultipleTabs;
            else if (lowered == QLatin1String("never"))        config.tabBarVisibility_ = TabBarVisibility::Never;
            else if (!lowered.isEmpty()) {
                qWarning() << "Config: tab bar `show` should be always, multiple or never, got"
                           << *show;
            }
        }

        /* Chrome colours are optional; anything left out is derived from the
         * terminal palette by ChromeColors::resolve(). */
        if (const YAML::Node colorsNode = node["colors"]; colorsNode && colorsNode.IsMap()) {
            ChromeColors& chrome = chromeOverrides;
            if (const auto color = readColor(colorsNode, "background")) chrome.tabBarBackground = *color;
            if (const auto color = readColor(colorsNode, "border")) chrome.tabBarBorder = *color;
            if (const auto color = readColor(colorsNode, "active_background")) chrome.activeTabBackground = *color;
            if (const auto color = readColor(colorsNode, "active_foreground")) chrome.activeTabForeground = *color;
            if (const auto color = readColor(colorsNode, "inactive_foreground")) chrome.inactiveTabForeground = *color;
            if (const auto color = readColor(colorsNode, "accent")) chrome.accent = *color;
        }
    }

    void cursor(const YAML::Node& node) {
        if (const auto style = readString(node, "style")) {
            const QString lowered = style->toLower();
            if (lowered == QLatin1String("block"))          config.cursorStyle_ = CursorStyle::Block;
            else if (lowered == QLatin1String("hollow"))    config.cursorStyle_ = CursorStyle::HollowBlock;
            else if (lowered == QLatin1String("underline")) config.cursorStyle_ = CursorStyle::Underline;
            else if (lowered == QLatin1String("bar")
                  || lowered == QLatin1String("beam"))      config.cursorStyle_ = CursorStyle::Bar;
            else if (!lowered.isEmpty()) {
                qWarning() << "Config: unknown cursor style" << *style;
            }
        }
        if (const auto blink = readBool(node, "blink")) {
            config.cursorBlink_ = *blink;
        }
    }

    void scrollback(const YAML::Node& node) {
        if (const auto lines = readInt(node, "lines")) {
            config.scrollbackLines_ = std::clamp(*lines, 0, MAX_SCROLLBACK_LINES);
        }
        if (const auto multiplier = readInt(node, "multiplier")) {
            config.scrollMultiplier_ = std::clamp(*multiplier, 1, MAX_SCROLL_MULTIPLIER);
        }
    }

    /*
     * Each value is either "home", "cwd" (equivalently "inherit") or a path.
     * One spelling covers both of the things a user might want -- a fixed
     * directory, or "wherever I already am" -- without a separate mode switch.
     */
    void directories(const YAML::Node& node) {
        if (const auto newTab = readString(node, "new_tab")) {
            config.newTabDirectory_ = StartDirectory::fromString(*newTab);
        }
        if (const auto newSplit = readString(node, "new_split")) {
            config.newSplitDirectory_ = StartDirectory::fromString(*newSplit);
        }
    }

    void mouse(const YAML::Node& node) {
        if (const auto alternateScroll = readBool(node, "alternate_scroll")) {
            config.alternateScroll_ = *alternateScroll;
        }
    }

    void window(const YAML::Node& node) {
        if (const auto width = readInt(node, "width")) {
            config.windowWidth_ = std::max(200, *width);
        }
        if (const auto height = readInt(node, "height")) {
            config.windowHeight_ = std::max(150, *height);
        }
        if (const auto opacity = readDouble(node, "opacity")) {
            config.windowOpacity_ = std::clamp(static_cast<float>(*opacity), 0.1f, 1.0f);
        }
        if (const auto fullscreen = readBool(node, "fullscreen")) {
            config.startFullscreen_ = *fullscreen;
        }
        if (const auto padding = readInt(node, "padding")) {
            config.windowPadding_ = std::clamp(*padding, 0, MAX_WINDOW_PADDING);
        }
    }

    void keybindings(const YAML::Node& node, BindingLayer& layer) {
        for (const auto& entry : node) {
            /* Read the key as a raw scalar rather than through as<std::string>:
             * a binding written as `y` or `n` would otherwise be coerced to a
             * boolean by YAML's type inference. */
            const QString keyText = QString::fromStdString(entry.first.Scalar()).trimmed();
            if (keyText.isEmpty()) continue;

            const QKeySequence sequence = parseKeySequence(keyText);
            if (sequence.isEmpty()) {
                qWarning() << "Config: unparseable key sequence" << keyText;
                continue;
            }

            if (!entry.second.IsScalar()) {
                qWarning() << "Config: keybinding" << keyText << "needs an action name";
                continue;
            }
            const QString actionName =
                QString::fromStdString(entry.second.Scalar()).trimmed();

            /* An explicit "none" unbinds a default, which is the only way for a
             * user overlay to remove a binding it did not create. */
            if (actionName.compare(QLatin1String("none"), Qt::CaseInsensitive) == 0) {
                layer.unbound.append(sequence);
                continue;
            }

            const Action action = stringToAction(actionName);
            if (action == ACTION_NONE) {
                qWarning() << "Config: unknown action" << actionName << "for" << keyText;
                continue;
            }
            layer.bound.insert(sequence, action);
            layer.assignedActions.insert(action);
        }
    }
};

void Config::mergeBindings(QHash<QKeySequence, Action>& target,
                           const BindingLayer& layer, bool ownsAssignedActions) {
    if (ownsAssignedActions && !layer.assignedActions.isEmpty()) {
        /* Release the keys inherited for any action this layer re-assigns. */
        for (auto it = target.begin(); it != target.end(); ) {
            if (layer.assignedActions.contains(it.value())) {
                it = target.erase(it);
            } else {
                ++it;
            }
        }
    }

    for (const QKeySequence& sequence : layer.unbound) {
        target.remove(sequence);
    }
    for (auto it = layer.bound.constBegin(); it != layer.bound.constEnd(); ++it) {
        target.insert(it.key(), it.value());
    }
}

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
    builtInColors_ = PaletteOverrides{};
    themeColors_ = PaletteOverrides{};
    userColors_ = PaletteOverrides{};
    themeName_.clear();
    /* Empty == "ask the platform for its monospaced default". */
    fontFamilies_.clear();
    fontFallbacks_.clear();
    fontSize_ = DEFAULT_FONT_SIZE;
    windowPadding_ = DEFAULT_WINDOW_PADDING;
    cursorStyle_ = CursorStyle::Block;
    cursorBlink_ = true;
    tabBarStyle_ = TabBarStyle::Minimal;
    tabBarPosition_ = TabBarPosition::Bottom;
    tabBarVisibility_ = TabBarVisibility::MultipleTabs;
    chromeColors_ = ChromeColors{};
    builtInChrome_ = ChromeColors{};
    themeChrome_ = ChromeColors{};
    userChrome_ = ChromeColors{};
    newTabDirectory_ = StartDirectory{StartDirectory::Kind::Home, QString()};
    newSplitDirectory_ = StartDirectory{StartDirectory::Kind::Cwd, QString()};
    windowWidth_ = DEFAULT_WINDOW_WIDTH;
    windowHeight_ = DEFAULT_WINDOW_HEIGHT;
    windowOpacity_ = 1.0f;
    startFullscreen_ = false;

    builtInBindings_ = BindingLayer{};
    userBindings_ = BindingLayer{};
    keybindings_.clear();
    macOsBindingsOverride_.reset();
    macOsBindings_ = macOsBindingsByDefault();
}

bool Config::macOsBindingsByDefault() {
#if defined(Q_OS_MACOS)
    return true;
#else
    return false;
#endif
}

void Config::resolvePlatformBindings() {
    macOsBindings_ = macOsBindingsOverride_.value_or(macOsBindingsByDefault());
}

void Config::loadKeybindings() {
    /*
     * The two files describe the same bindings; they differ only in whether the
     * Meta modifier is spelled `cmd` or `super`, which is what a reader of each
     * platform expects to see. Qt maps both to Qt::MetaModifier, so the resolved
     * key combinations are identical -- an invariant the test suite asserts, so
     * the two cannot drift apart.
     */
    const QString path = macOsBindings_ ? QStringLiteral(":/keybindings/macos.yaml")
                                        : QStringLiteral(":/keybindings/linux.yaml");
    if (!applyFile(path, Layer::Keybindings)) {
        qWarning() << "Config: default keybindings missing from resources:" << path;
    }
}

void Config::resolveKeybindings() {
    keybindings_.clear();
    /*
     * The defaults may offer several keys for one action. The user's layer is
     * merged with action ownership, so naming an action there replaces every key
     * the defaults gave it.
     */
    mergeBindings(keybindings_, builtInBindings_, /*ownsAssignedActions=*/false);
    mergeBindings(keybindings_, userBindings_, /*ownsAssignedActions=*/true);
}

QString Config::userConfigPath() {
    return QDir::homePath() + QStringLiteral("/.config/ratty/config.yaml");
}

QString Config::legacyUserConfigPath() {
    return QDir::homePath() + QStringLiteral("/.config/ratty/config.json");
}

void Config::load() {
    applyBuiltInDefaults();

    if (!applyFile(QString::fromLatin1(kBundledDefaultsPath), Layer::BuiltIn)) {
        qWarning() << "Config: bundled defaults missing from resources";
    }

    const QString userPath = userConfigPath();
    if (QFile::exists(userPath)) {
        if (applyFile(userPath, Layer::User)) {
            qInfo() << "Config: loaded user overrides from" << userPath;
        }
    } else if (QFile::exists(legacyUserConfigPath())) {
        /* Say so rather than ignoring it: a config that has quietly stopped
         * being read is worse than one that fails loudly. */
        qWarning() << "Config:" << legacyUserConfigPath()
                   << "is the old JSON format and is no longer read. Convert it to"
                   << userPath;
    }

    /*
     * The theme and the keybinding platform are themselves settings, so both are
     * only known once every layer has been read. Each is loaded now and merged
     * *underneath* what the user wrote.
     */
    resolvePlatformBindings();
    loadKeybindings();
    applyTheme();

    resolvePalette();
    resolveChrome();
    resolveKeybindings();

    qInfo() << "Config: theme" << (themeName_.isEmpty() ? QStringLiteral("<none>") : themeName_)
            << "| font preference"
            << (fontFamilies_.isEmpty() ? QStringLiteral("<system monospace>")
                                        : fontFamilies_.join(QStringLiteral(" > ")))
            << "at" << fontSize_ << "pt,"
            << "padding" << windowPadding_ << "px,"
            << keybindings_.size()
            << (macOsBindings_ ? "keybindings (macOS set)" : "keybindings (Linux set)");
}

bool Config::applyFile(const QString& path, Layer layer) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }
    const QByteArray bytes = file.readAll();
    return applyDocument(bytes.toStdString(), path, layer);
}

bool Config::applyDocument(const std::string& text, const QString& sourceLabel,
                           Layer layer) {
    if (text.empty()) return false;

    YAML::Node root;
    try {
        root = YAML::Load(text);
    } catch (const YAML::Exception& error) {
        /* Report the line and column: a YAML error without a position is close
         * to useless in a hand-edited file. */
        qWarning() << "Config:" << sourceLabel << "- YAML error at line"
                   << error.mark.line + 1 << "column" << error.mark.column + 1
                   << ":" << QString::fromStdString(error.msg);
        return false;
    }

    if (!root || !root.IsMap()) {
        qWarning() << "Config:" << sourceLabel << "- top level should be a mapping";
        return false;
    }

    /*
     * Bindings are staged per document rather than written straight through, so
     * mergeBindings() can see the whole set of actions the document assigns
     * before deciding what to displace.
     */
    /* Each kind of setting is staged into the bucket for this layer. */
    PaletteOverrides* colorTarget = &builtInColors_;
    ChromeColors* chromeTarget = &builtInChrome_;
    BindingLayer* bindingTarget = &builtInBindings_;
    switch (layer) {
    case Layer::Theme:
        colorTarget = &themeColors_; chromeTarget = &themeChrome_;
        break;
    case Layer::User:
        colorTarget = &userColors_; chromeTarget = &userChrome_;
        bindingTarget = &userBindings_;
        break;
    case Layer::BuiltIn:
    case Layer::Keybindings:
        break;
    }

    PaletteOverrides documentColors;
    ChromeColors documentChrome;
    BindingLayer documentBindings;

    Parser parser{*this, documentBindings, documentColors, documentChrome};
    try {
        parser.apply(root);
    } catch (const YAML::Exception& error) {
        qWarning() << "Config:" << sourceLabel << "- error reading settings:"
                   << QString::fromStdString(error.msg);
        return false;
    }

    colorTarget->absorb(documentColors);
    mergeChrome(*chromeTarget, documentChrome);
    bindingTarget->absorb(documentBindings);
    return true;
}

void Config::mergeChrome(ChromeColors& target, const ChromeColors& later) {
    if (later.tabBarBackground) target.tabBarBackground = later.tabBarBackground;
    if (later.tabBarBorder) target.tabBarBorder = later.tabBarBorder;
    if (later.activeTabBackground) target.activeTabBackground = later.activeTabBackground;
    if (later.activeTabForeground) target.activeTabForeground = later.activeTabForeground;
    if (later.inactiveTabForeground) target.inactiveTabForeground = later.inactiveTabForeground;
    if (later.accent) target.accent = later.accent;
}

void Config::applyTheme() {
    if (themeName_.isEmpty()) return;

    if (!themes::exists(themeName_)) {
        qWarning() << "Config: unknown theme" << themeName_ << "- available themes are"
                   << themes::available().join(QStringLiteral(", "));
        themeName_.clear();
        return;
    }

    if (!applyFile(themes::resourcePath(themeName_), Layer::Theme)) {
        qWarning() << "Config: could not read theme" << themeName_;
        themeName_.clear();
    }
}

void Config::resolvePalette() {
    /*
     * Fixed order, independent of the order the files were read: the built-in
     * palette, then whatever the bundled configuration states, then the theme,
     * then the user's own colours. So `theme: nord` plus `colors: {red: ...}`
     * gives Nord with one colour changed, whichever appears first in the file.
     */
    Palette palette;
    builtInColors_.mergeInto(palette);
    themeColors_.mergeInto(palette);
    userColors_.mergeInto(palette);
    palette_ = palette;
}

void Config::resolveChrome() {
    ChromeColors chrome = builtInChrome_;
    mergeChrome(chrome, themeChrome_);
    mergeChrome(chrome, userChrome_);
    chromeColors_ = chrome;
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
    const Qt::Key key = static_cast<Qt::Key>(event->key());

    if (const Action action = lookupAction(QKeySequence(QKeyCombination(modifiers, key)));
        action != ACTION_NONE) {
        return action;
    }

    /*
     * Only worth retrying when Shift is held: without it there is no ambiguity
     * about which of the two symbols on a key was meant.
     */
    if (!(modifiers & Qt::ShiftModifier)) return ACTION_NONE;

    const Qt::Key partner = shiftPartner(key);
    if (partner == Qt::Key_unknown) return ACTION_NONE;

    if (const Action action = lookupAction(QKeySequence(QKeyCombination(modifiers, partner)));
        action != ACTION_NONE) {
        return action;
    }

    /*
     * Finally, try without Shift.
     *
     * A binding like `cmd+1` carries no Shift, yet on layouts where the digits
     * are the *shifted* symbols -- AZERTY, among others -- typing it necessarily
     * holds Shift down. Ignoring Shift is what lets that reach the binding.
     *
     * shiftPartner() only knows digits and punctuation, never letters, and this
     * runs only for keys it recognises. So `ctrl+shift+c` can never decay into
     * `ctrl+c` and steal an interrupt from the shell.
     */
    const Qt::KeyboardModifiers withoutShift = modifiers & ~Qt::KeyboardModifiers(Qt::ShiftModifier);
    if (const Action action = lookupAction(QKeySequence(QKeyCombination(withoutShift, partner)));
        action != ACTION_NONE) {
        return action;
    }
    return lookupAction(QKeySequence(QKeyCombination(withoutShift, key)));
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
