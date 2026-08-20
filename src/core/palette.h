/*
 * Palette - the single source of truth for terminal colours
 *
 * Before this existed the default foreground/background were hard-coded in
 * three separate places (VTParser, TerminalEmulator and Config) which quietly
 * disagreed with each other; a custom background in config.json would make
 * every cell paint an opaque rectangle in the *old* default colour. The grid
 * now stores symbolic colours (see Color in cell.h) and this class is the only
 * thing that turns them into pixels.
 */

#ifndef CORE_PALETTE_H
#define CORE_PALETTE_H

#include "cell.h"
#include <QColor>
#include <QString>
#include <array>

/*
 * Parse an X11/xterm colour specification, as carried by OSC 4/10/11/12:
 *
 *   #rgb  #rrggbb  #rrrgggbbb  #rrrrggggbbbb
 *   rgb:r/g/b  rgbi:...        (1-4 hex digits per component)
 *   a colour name QColor recognises ("red", "cornflowerblue")
 *
 * Returns an invalid QColor if the spec cannot be understood, so callers can
 * ignore a malformed request rather than painting something arbitrary.
 */
QColor parseColorSpec(const QString& spec);

/* Format a colour the way xterm answers an OSC colour query. */
QString formatColorSpec(const QColor& color);

class Palette {
public:
    static constexpr int PaletteSize = 256;

    Palette();

    /* Resolve a symbolic colour to a concrete one. `isForeground` decides which
     * default is used, and matters only for Color::Kind::Default. */
    QColor resolve(const Color& color, bool isForeground) const;

    /* Resolve a cell's pair of colours, applying inverse video. */
    void resolveCell(const Cell& cell, QColor& fgOut, QColor& bgOut) const;

    const QColor& defaultForeground() const { return defaultForeground_; }
    const QColor& defaultBackground() const { return defaultBackground_; }
    const QColor& cursorColor() const { return cursorColor_; }
    const QColor& selectionBackground() const { return selectionBackground_; }

    void setDefaultForeground(const QColor& c) { defaultForeground_ = c; }
    void setDefaultBackground(const QColor& c) { defaultBackground_ = c; }
    void setCursorColor(const QColor& c) { cursorColor_ = c; }
    void setSelectionBackground(const QColor& c) { selectionBackground_ = c; }

    const QColor& entry(int index) const;
    void setEntry(int index, const QColor& c);

private:
    void fillDefaultEntries();

    std::array<QColor, PaletteSize> entries_;
    QColor defaultForeground_;
    QColor defaultBackground_;
    QColor cursorColor_;
    QColor selectionBackground_;
};

#endif /* CORE_PALETTE_H */
