/*
 * Chrome - deriving chrome colours from the terminal palette
 */

#include "chrome.h"
#include <algorithm>

namespace {

/* Perceived brightness, for deciding which way to shift a colour. */
double luminance(const QColor& color) {
    return 0.2126 * color.redF() + 0.7152 * color.greenF() + 0.0722 * color.blueF();
}

/*
 * Nudge a colour away from itself by `amount`, lightening a dark colour and
 * darkening a light one. Using lighter()/darker() unconditionally would make a
 * light theme's tab bar vanish into its background.
 */
QColor shift(const QColor& base, int amount) {
    return luminance(base) < 0.5 ? base.lighter(100 + amount)
                                 : base.darker(100 + amount);
}

QColor blend(const QColor& from, const QColor& to, double weight) {
    const double w = std::clamp(weight, 0.0, 1.0);
    return QColor::fromRgbF(from.redF()   * (1 - w) + to.redF()   * w,
                            from.greenF() * (1 - w) + to.greenF() * w,
                            from.blueF()  * (1 - w) + to.blueF()  * w);
}

} // namespace

ChromeColors::Resolved ChromeColors::resolve(const Palette& palette) const {
    const QColor background = palette.defaultBackground();
    const QColor foreground = palette.defaultForeground();

    Resolved resolved;

    /*
     * The bar has to sit far enough off the terminal background that a filled
     * active tab reads as a distinct surface -- at a smaller offset the `blocks`
     * style was indistinguishable from `minimal`.
     */
    resolved.tabBarBackground = tabBarBackground.value_or(shift(background, 45));
    resolved.tabBarBorder = tabBarBorder.value_or(shift(background, 85));

    /* The active tab reads as a continuation of the terminal itself. */
    resolved.activeTabBackground = activeTabBackground.value_or(background);
    resolved.activeTabForeground = activeTabForeground.value_or(foreground);

    /* Inactive labels recede rather than change hue. */
    resolved.inactiveTabForeground =
        inactiveTabForeground.value_or(blend(resolved.tabBarBackground, foreground, 0.55));

    /*
     * The accent defaults to the palette's blue, which every theme defines and
     * which therefore tracks the theme without needing to be stated.
     */
    resolved.accent = accent.value_or(palette.entry(12));

    /*
     * The line between panes is the accent muted back towards the terminal
     * background, so it belongs to the theme without competing with the text:
     * a blue theme gets a blue divider, a green one a green divider. Blending
     * rather than using the accent directly matters on both extremes -- the raw
     * accent is a bright stripe on a dark theme, and on a light theme it is the
     * only saturated thing on screen.
     *
     * It replaces a hard-coded #3a3a3a, which looked deliberate on the default
     * dark theme and looked like a mistake on every other one.
     */
    resolved.splitSeparator =
        splitSeparator.value_or(blend(background, resolved.accent, 0.45));

    return resolved;
}
