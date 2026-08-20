/*
 * Palette - terminal colour resolution
 */

#include "palette.h"

namespace {

/* The 16 base ANSI colours. Slots 16-231 are the 6x6x6 colour cube and
 * 232-255 the greyscale ramp; both are generated rather than tabulated. */
constexpr struct { uint8_t r, g, b; } kBaseColors[16] = {
    {  0,   0,   0},  //  0 black
    {205,  49,  49},  //  1 red
    { 13, 188, 121},  //  2 green
    {229, 229,  16},  //  3 yellow
    { 36, 114, 200},  //  4 blue
    {188,  63, 188},  //  5 magenta
    { 17, 168, 205},  //  6 cyan
    {229, 229, 229},  //  7 white
    {102, 102, 102},  //  8 bright black
    {241,  76,  76},  //  9 bright red
    { 35, 209, 139},  // 10 bright green
    {245, 245,  67},  // 11 bright yellow
    { 59, 142, 234},  // 12 bright blue
    {214, 112, 214},  // 13 bright magenta
    { 41, 184, 219},  // 14 bright cyan
    {255, 255, 255},  // 15 bright white
};

constexpr int kCubeLevels[6] = {0, 95, 135, 175, 215, 255};

/* Scale an n-hex-digit component to 8 bits. "f" -> 255, "ffff" -> 255. */
int scaleHexComponent(const QString& text) {
    bool ok = false;
    const int value = text.toInt(&ok, 16);
    if (!ok || text.isEmpty() || text.size() > 4) return -1;

    const int maximum = (1 << (4 * text.size())) - 1;
    return (value * 255 + maximum / 2) / maximum;
}

} // namespace

QColor parseColorSpec(const QString& spec) {
    const QString text = spec.trimmed();
    if (text.isEmpty()) return QColor();

    /* rgb:R/G/B and rgbi:... - the canonical X11 form, and what xterm replies. */
    const int colon = text.indexOf(QLatin1Char(':'));
    if (colon > 0) {
        const QString scheme = text.left(colon).toLower();
        if (scheme == QLatin1String("rgb") || scheme == QLatin1String("rgbi")) {
            const QStringList parts = text.mid(colon + 1).split(QLatin1Char('/'));
            if (parts.size() != 3) return QColor();

            const int r = scaleHexComponent(parts[0]);
            const int g = scaleHexComponent(parts[1]);
            const int b = scaleHexComponent(parts[2]);
            if (r < 0 || g < 0 || b < 0) return QColor();
            return QColor(r, g, b);
        }
    }

    /* #rgb / #rrggbb / #rrrgggbbb / #rrrrggggbbbb. QColor handles the 3, 6 and
     * 12 digit forms but not 9, so split by hand. */
    if (text.startsWith(QLatin1Char('#'))) {
        const QString digits = text.mid(1);
        if (digits.size() % 3 != 0 || digits.isEmpty() || digits.size() > 12) {
            return QColor();
        }
        const int width = digits.size() / 3;
        const int r = scaleHexComponent(digits.mid(0, width));
        const int g = scaleHexComponent(digits.mid(width, width));
        const int b = scaleHexComponent(digits.mid(2 * width, width));
        if (r < 0 || g < 0 || b < 0) return QColor();
        return QColor(r, g, b);
    }

    /* A colour name. QColor::isValidColorName rejects anything unknown. */
    const QColor named(text);
    return named.isValid() ? named : QColor();
}

QString formatColorSpec(const QColor& color) {
    /* xterm answers with 16 bits per component; doubling each byte is the
     * conventional widening (0xff -> 0xffff). */
    return QStringLiteral("rgb:%1/%2/%3")
        .arg(color.red() * 0x101, 4, 16, QLatin1Char('0'))
        .arg(color.green() * 0x101, 4, 16, QLatin1Char('0'))
        .arg(color.blue() * 0x101, 4, 16, QLatin1Char('0'));
}

Palette::Palette()
    : defaultForeground_(220, 220, 220)
    , defaultBackground_(30, 30, 30)
    , cursorColor_(220, 220, 220)
    , selectionBackground_(100, 149, 237, 128)
{
    fillDefaultEntries();
}

void Palette::fillDefaultEntries() {
    for (int i = 0; i < 16; ++i) {
        entries_[i] = QColor(kBaseColors[i].r, kBaseColors[i].g, kBaseColors[i].b);
    }

    // 16-231: 6x6x6 RGB cube
    for (int i = 0; i < 216; ++i) {
        const int r = kCubeLevels[(i / 36) % 6];
        const int g = kCubeLevels[(i / 6) % 6];
        const int b = kCubeLevels[i % 6];
        entries_[16 + i] = QColor(r, g, b);
    }

    // 232-255: 24-step greyscale ramp
    for (int i = 0; i < 24; ++i) {
        const int level = 8 + i * 10;
        entries_[232 + i] = QColor(level, level, level);
    }
}

const QColor& Palette::entry(int index) const {
    if (index < 0 || index >= PaletteSize) {
        return defaultForeground_;
    }
    return entries_[static_cast<size_t>(index)];
}

void Palette::setEntry(int index, const QColor& c) {
    if (index >= 0 && index < PaletteSize && c.isValid()) {
        entries_[static_cast<size_t>(index)] = c;
    }
}

QColor Palette::resolve(const Color& color, bool isForeground) const {
    switch (color.kind) {
    case Color::Kind::Indexed:
        return entry(color.index());
    case Color::Kind::Rgb:
        return QColor(color.r, color.g, color.b);
    case Color::Kind::Default:
        break;
    }
    return isForeground ? defaultForeground_ : defaultBackground_;
}

void Palette::resolveCell(const Cell& cell, QColor& fgOut, QColor& bgOut) const {
    fgOut = resolve(cell.fg, /*isForeground=*/true);
    bgOut = resolve(cell.bg, /*isForeground=*/false);

    if (cell.hasFlag(CellFlagInverse)) {
        std::swap(fgOut, bgOut);
    }

    if (cell.hasFlag(CellFlagFaint)) {
        fgOut = fgOut.darker(150);
    }
    if (cell.hasFlag(CellFlagInvisible)) {
        fgOut = bgOut;
    }
}
