/*
 * Themes - named colour schemes, and the staging that makes them layer
 *
 * A theme is just a configuration fragment holding a `colors:` section, shipped
 * inside the binary under :/themes. That means themes need no parser of their
 * own, and a user can read one to learn the format.
 *
 * The reason colours are *staged* rather than written straight into the palette
 * is ordering. The theme's name is itself a setting, so it is not known until
 * every configuration layer has been read -- and by then the user's own
 * `colors:` entries have already been seen. Collecting each layer's colours
 * separately and merging them in a fixed order (built-in, then theme, then user)
 * makes the result independent of which file mentioned what, and of where in a
 * file it was mentioned.
 */

#ifndef CONFIG_THEME_H
#define CONFIG_THEME_H

#include "../core/palette.h"
#include <QColor>
#include <QString>
#include <QStringList>
#include <array>
#include <optional>

/*
 * A partial palette. Every field is optional, so a theme that states only a
 * background and foreground leaves the rest of the palette alone.
 */
struct PaletteOverrides {
    static constexpr int AnsiCount = 16;

    std::optional<QColor> background;
    std::optional<QColor> foreground;
    std::optional<QColor> cursor;
    std::optional<QColor> selectionBackground;
    std::array<std::optional<QColor>, AnsiCount> ansi;

    bool isEmpty() const;
    /* Write whatever is set into `palette`, leaving the rest untouched. */
    void mergeInto(Palette& palette) const;
    /* Take everything set in `later`, which wins over what is already here. */
    void absorb(const PaletteOverrides& later);
};

namespace themes {

/* Theme identifiers, as used in the `theme:` setting. Sorted. */
QStringList available();

/* True if `id` names a built-in theme. */
bool exists(const QString& id);

/* Resource path for a theme's YAML fragment. */
QString resourcePath(const QString& id);

} // namespace themes

#endif /* CONFIG_THEME_H */
