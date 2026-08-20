/*
 * Chrome - colours and style for the parts of the window that are not terminal
 *
 * Kept apart from Palette, which is strictly the terminal's own 256 colours plus
 * its default foreground and background. The tab bar is application chrome, and
 * conflating the two would mean an application's OSC colour request could
 * repaint the tab bar.
 *
 * Every chrome colour is optional. Left unset it is *derived* from the terminal
 * palette, so a theme that only states a background and foreground still gets a
 * tab bar that belongs to it. That is the property that lets colour themes be
 * defined without enumerating chrome.
 */

#ifndef CONFIG_CHROME_H
#define CONFIG_CHROME_H

#include "../core/palette.h"
#include <QColor>
#include <optional>

/* How the tab bar draws itself. */
enum class TabBarStyle {
    Minimal,    // text only, with a thin accent along the active tab's inner edge
    Underline,  // text with a full-width accent rule under the active tab
    Blocks,     // the active tab is a filled rectangle, tabs divided by hairlines
    Pills,      // the active tab is a filled rounded capsule
    Powerline,  // angled chevrons, echoing a Powerline prompt
};

enum class TabBarPosition { Top, Bottom };

/* When the bar is shown at all. */
enum class TabBarVisibility {
    Always,
    MultipleTabs,   // hidden while a single tab is open
    Never,
};

/*
 * Chrome colours. Each is optional; `resolve()` fills the gaps from the terminal
 * palette so a partially specified theme is still coherent.
 */
struct ChromeColors {
    std::optional<QColor> tabBarBackground;
    std::optional<QColor> tabBarBorder;
    std::optional<QColor> activeTabBackground;
    std::optional<QColor> activeTabForeground;
    std::optional<QColor> inactiveTabForeground;
    std::optional<QColor> accent;

    /* The fully determined set, ready to paint with. */
    struct Resolved {
        QColor tabBarBackground;
        QColor tabBarBorder;
        QColor activeTabBackground;
        QColor activeTabForeground;
        QColor inactiveTabForeground;
        QColor accent;
    };

    Resolved resolve(const Palette& palette) const;
};

#endif /* CONFIG_CHROME_H */
