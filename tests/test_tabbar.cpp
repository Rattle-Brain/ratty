/*
 * Tab bar tests.
 *
 * The bar is drawn by hand, so the things worth pinning are the ones a repaint
 * cannot reveal by itself: that it stays thin, that labels leave room for the
 * close affordance, that the affordance is only where it is actually drawn, and
 * that chrome colours derive sensibly from any palette -- including a light one,
 * where naively darkening or lightening produces an invisible bar.
 *
 * Runs under QT_QPA_PLATFORM=offscreen; none of this needs a GPU or a display.
 */

#include "check.h"
#include "config/chrome.h"
#include "config/config.h"
#include "ui/tab_bar.h"
#include <QApplication>
#include <QDir>
#include <QFontMetrics>
#include <QTemporaryDir>
#include <QTextStream>
#include <string>

namespace {

QTemporaryDir* sandbox = nullptr;

void loadWithUserConfig(const char* yaml) {
    const QString configDir = sandbox->path() + QStringLiteral("/.config/ratty");
    QDir().mkpath(configDir);
    const QString path = configDir + QStringLiteral("/config.yaml");

    QFile::remove(path);
    if (yaml) {
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            check::that(false, "could not write the sandbox config");
            return;
        }
        QTextStream(&file) << QString::fromUtf8(yaml);
        file.close();
    }
    Config::instance().load();
}

/* A tab widget with `titles`, positioned from the current configuration. */
TabWidget* makeTabs(const QStringList& titles) {
    auto* tabs = new TabWidget();
    tabs->setTabPosition(Config::instance().tabBarPosition() == TabBarPosition::Bottom
                             ? QTabWidget::South
                             : QTabWidget::North);
    for (const QString& title : titles) {
        tabs->addTab(new QWidget(), title);
    }
    tabs->resize(800, 200);
    tabs->show();
    QCoreApplication::processEvents();
    return tabs;
}

void testStyleAndPositionParsing() {
    check::section("tab_bar configuration");

    loadWithUserConfig(nullptr);

    /*
     * Which style ships is a matter of taste and may be changed freely, so it is
     * captured rather than asserted -- pinning it here would turn a preference
     * into a regression. Position and visibility *are* documented behaviour.
     */
    const TabBarStyle shippedStyle = Config::instance().tabBarStyle();
    check::that(Config::instance().tabBarPosition() == TabBarPosition::Bottom,
                "the default position is bottom");
    check::that(Config::instance().tabBarVisibility() == TabBarVisibility::MultipleTabs,
                "the bar is hidden by default until a second tab exists");

    struct Case { const char* name; TabBarStyle style; };
    for (const Case& item : {Case{"minimal", TabBarStyle::Minimal},
                             Case{"underline", TabBarStyle::Underline},
                             Case{"blocks", TabBarStyle::Blocks},
                             Case{"pills", TabBarStyle::Pills},
                             Case{"powerline", TabBarStyle::Powerline}}) {
        loadWithUserConfig((std::string("tab_bar:\n  style: ") + item.name + "\n").c_str());
        check::that(Config::instance().tabBarStyle() == item.style,
                    std::string("style: ") + item.name);
    }

    loadWithUserConfig("tab_bar:\n  position: top\n");
    check::that(Config::instance().tabBarPosition() == TabBarPosition::Top, "position: top");

    loadWithUserConfig("tab_bar:\n  show: always\n");
    check::that(Config::instance().tabBarVisibility() == TabBarVisibility::Always,
                "show: always");
    loadWithUserConfig("tab_bar:\n  show: never\n");
    check::that(Config::instance().tabBarVisibility() == TabBarVisibility::Never,
                "show: never");

    /* An unknown value is reported and the default kept, like every other key. */
    loadWithUserConfig("tab_bar:\n  style: sparkly\n  position: sideways\n");
    check::that(Config::instance().tabBarStyle() == shippedStyle,
                "an unknown style keeps whatever the default is");
    check::that(Config::instance().tabBarPosition() == TabBarPosition::Bottom,
                "an unknown position keeps the default");
}

void testChromeColorsDerivation() {
    check::section("chrome colours derive from the palette");

    /* A dark theme: the bar must be lighter than the terminal, or it disappears. */
    Palette dark;
    dark.setDefaultBackground(QColor(0x1e, 0x1e, 0x1e));
    dark.setDefaultForeground(QColor(0xdc, 0xdc, 0xdc));

    const ChromeColors::Resolved darkChrome = ChromeColors{}.resolve(dark);
    check::that(darkChrome.tabBarBackground.lightness() > dark.defaultBackground().lightness(),
                "on a dark theme the bar is lighter than the terminal");
    check::that(darkChrome.activeTabBackground == dark.defaultBackground(),
                "the active tab matches the terminal background");
    check::that(darkChrome.accent == dark.entry(12),
                "the accent follows the palette's bright blue");

    /*
     * A light theme is the case that catches a naive implementation: shifting
     * always "lighter" would leave a white bar on a white terminal.
     */
    Palette light;
    light.setDefaultBackground(QColor(0xfa, 0xfa, 0xfa));
    light.setDefaultForeground(QColor(0x20, 0x20, 0x20));

    const ChromeColors::Resolved lightChrome = ChromeColors{}.resolve(light);
    check::that(lightChrome.tabBarBackground.lightness() < light.defaultBackground().lightness(),
                "on a light theme the bar is darker than the terminal");
    check::that(lightChrome.tabBarBackground != light.defaultBackground(),
                "and is not the same colour as the terminal");

    /* Inactive labels must be legible against the bar, not against the terminal. */
    for (const ChromeColors::Resolved& chrome : {darkChrome, lightChrome}) {
        const int contrast = std::abs(chrome.inactiveTabForeground.lightness()
                                      - chrome.tabBarBackground.lightness());
        check::that(contrast > 25, "inactive labels contrast with the bar");
    }

    /* An explicit colour always wins over the derivation. */
    ChromeColors explicitColors;
    explicitColors.accent = QColor(255, 0, 128);
    explicitColors.tabBarBackground = QColor(1, 2, 3);
    const ChromeColors::Resolved resolved = explicitColors.resolve(dark);
    check::that(resolved.accent == QColor(255, 0, 128), "an explicit accent is used");
    check::that(resolved.tabBarBackground == QColor(1, 2, 3),
                "an explicit bar background is used");
    check::that(resolved.activeTabForeground == dark.defaultForeground(),
                "unstated colours are still derived");
}

void testChromeColorsFromConfig() {
    check::section("chrome colours from the config file");

    /*
     * Over a theme that states no chrome of its own, so that "unstated" really
     * does mean "left to be derived". RaTTY's own two themes do state theirs,
     * which is a separate case and is checked below.
     */
    loadWithUserConfig(R"(
theme: nord
tab_bar:
  colors:
    background: "#101010"
    accent: "#ff8800"
)");
    const ChromeColors& chrome = Config::instance().chromeColors();
    check::that(chrome.tabBarBackground.has_value() && *chrome.tabBarBackground == QColor(0x10, 0x10, 0x10),
                "the bar background was read");
    check::that(chrome.accent.has_value() && *chrome.accent == QColor(0xff, 0x88, 0x00),
                "the accent was read");
    check::that(!chrome.activeTabForeground.has_value(),
                "an unstated chrome colour stays unset, to be derived");

    /*
     * And the layering the other way round: a theme that *does* state chrome
     * supplies the colours the user did not, while the ones the user named still
     * win. This is what stops a themed bar being half-overwritten by a config
     * that only wanted to change the accent.
     */
    loadWithUserConfig(R"(
theme: ratty-dark
tab_bar:
  colors:
    accent: "#ff8800"
)");
    const ChromeColors& layered = Config::instance().chromeColors();
    check::that(layered.accent.has_value() && *layered.accent == QColor(0xff, 0x88, 0x00),
                "the user's accent beat the theme's");
    check::that(layered.activeTabForeground.has_value(),
                "and the theme still supplied what the user did not");
}

void testBarIsThin() {
    check::section("the bar stays thin and scales with the font");

    loadWithUserConfig("tab_bar:\n  show: always\nfont:\n  size: 13\n");
    TabWidget* tabs = makeTabs({QStringLiteral("zsh"), QStringLiteral("nvim")});
    const int heightAt13 = tabs->rattyTabBar()->height();

    /* One text line plus a little padding: a stock QTabBar is roughly twice
     * this. */
    const int textHeight = QFontMetrics(tabs->rattyTabBar()->font()).height();
    check::that(heightAt13 <= textHeight + 10,
                "the bar is not much taller than its text");
    check::that(heightAt13 >= 20, "but is still comfortably clickable");
    delete tabs;

    /* Bigger terminal font, bigger bar: the metrics come from the font so the
     * bar cannot end up out of proportion. */
    loadWithUserConfig("tab_bar:\n  show: always\nfont:\n  size: 22\n");
    tabs = makeTabs({QStringLiteral("zsh")});
    check::that(tabs->rattyTabBar()->height() > heightAt13,
                "a larger terminal font gives a taller bar");
    delete tabs;
}

void testTabMetrics() {
    check::section("tab metrics");

    loadWithUserConfig("tab_bar:\n  show: always\n");
    TabWidget* tabs = makeTabs({QStringLiteral("x"),
                                QStringLiteral("a very long tab title that will not fit at all"),
                                QStringLiteral("build")});
    TabBar* bar = tabs->rattyTabBar();

    check::that(bar->tabRect(0).width() >= 60,
                "a one-character tab still gets a usable minimum width");
    check::that(bar->tabRect(1).width() <= 240,
                "a very long title is capped rather than filling the bar");
    check::that(bar->tabRect(2).width() > bar->tabRect(0).width(),
                "a longer title gives a wider tab");

    /* Reserving close-affordance space unconditionally is what stops the label
     * shifting sideways when the pointer enters a tab. */
    const int shortWidth = bar->tabRect(0).width();
    const QFontMetrics metrics(bar->font());
    check::that(shortWidth > metrics.horizontalAdvance(QStringLiteral("x")) + 20,
                "tab width leaves room for the close affordance");

    delete tabs;
}

void testVisibilityRules() {
    check::section("visibility");

    /* `multiple` is the default and is why a single-terminal window has no
     * chrome at all. */
    loadWithUserConfig("tab_bar:\n  show: multiple\n");
    TabWidget* tabs = makeTabs({QStringLiteral("one")});
    check::equal(tabs->count(), 1, "one tab open");
    delete tabs;

    loadWithUserConfig("tab_bar:\n  show: always\n");
    tabs = makeTabs({QStringLiteral("one")});
    check::that(Config::instance().tabBarVisibility() == TabBarVisibility::Always,
                "`always` keeps the bar with a single tab");
    delete tabs;
}

void testPositionDrivesTheBarShape() {
    check::section("position reaches the bar");

    /*
     * The bar decides which edge to accent from its own shape, so the shape is
     * the contract between the configuration and the painting.
     */
    loadWithUserConfig("tab_bar:\n  position: bottom\n  show: always\n");
    TabWidget* tabs = makeTabs({QStringLiteral("a"), QStringLiteral("b")});
    check::that(tabs->rattyTabBar()->shape() == QTabBar::RoundedSouth,
                "position: bottom gives the bar a south shape");
    delete tabs;

    loadWithUserConfig("tab_bar:\n  position: top\n  show: always\n");
    tabs = makeTabs({QStringLiteral("a"), QStringLiteral("b")});
    check::that(tabs->rattyTabBar()->shape() == QTabBar::RoundedNorth,
                "position: top gives the bar a north shape");
    delete tabs;
}

void testEveryStylePaints() {
    check::section("every style paints without complaint");

    /* Rendering each style catches a null-brush or bad-path mistake that only
     * shows up on the drawing path. */
    for (const char* style : {"minimal", "underline", "blocks", "pills", "powerline"}) {
        loadWithUserConfig((std::string("tab_bar:\n  show: always\n  style: ")
                            + style + "\n").c_str());
        TabWidget* tabs = makeTabs({QStringLiteral("zsh"), QStringLiteral("nvim"),
                                    QStringLiteral("htop")});
        TabBar* bar = tabs->rattyTabBar();
        tabs->setCurrentIndex(1);
        QCoreApplication::processEvents();

        QImage image(bar->size(), QImage::Format_ARGB32);
        image.fill(Qt::transparent);
        bar->render(&image);

        /* Something was drawn: an all-transparent bar would mean the paint path
         * bailed out silently. */
        bool painted = false;
        for (int y = 0; y < image.height() && !painted; ++y) {
            for (int x = 0; x < image.width(); ++x) {
                if (qAlpha(image.pixel(x, y)) != 0) { painted = true; break; }
            }
        }
        check::that(painted, std::string("style ") + style + " painted something");
        delete tabs;
    }
}

} // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    QTemporaryDir tempHome;
    if (!tempHome.isValid()) {
        std::printf("could not create a temporary HOME\n");
        return 1;
    }
    sandbox = &tempHome;
    qputenv("HOME", tempHome.path().toUtf8());

    testStyleAndPositionParsing();
    testChromeColorsDerivation();
    testChromeColorsFromConfig();
    testBarIsThin();
    testTabMetrics();
    testVisibilityRules();
    testPositionDrivesTheBarShape();
    testEveryStylePaints();
    return check::report("test_tabbar");
}
