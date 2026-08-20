/*
 * Render-layer tests that need no GL context: the grid geometry maths and font
 * resolution. Both are pure functions of their inputs, and both have failure
 * modes that are invisible until you look at a window.
 */

#include "check.h"
#include "render/box_drawing.h"
#include "render/font_manager.h"
#include "render/terminal_renderer.h"
#include <QGuiApplication>
#include <algorithm>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

FontMetrics metricsFor(int cellWidth, int cellHeight) {
    FontMetrics metrics;
    metrics.cellWidth = cellWidth;
    metrics.cellHeight = cellHeight;
    metrics.ascender = cellHeight - 4;
    metrics.descender = 4;
    return metrics;
}

void testLayoutPadding() {
    check::section("grid layout padding");

    const FontMetrics metrics = metricsFor(10, 20);

    /* With no padding the grid fills the viewport exactly. */
    const auto flush = TerminalRenderer::computeLayout(metrics, 800, 400, 0);
    check::equal(flush.cols, 80, "80 columns in 800px with no padding");
    check::equal(flush.rows, 20, "20 rows in 400px with no padding");
    check::equal(flush.originX, 0, "text starts at the left edge");
    check::equal(flush.originY, 0, "text starts at the top edge");

    /* 8px of padding costs one column and leaves the text inset. */
    const auto padded = TerminalRenderer::computeLayout(metrics, 800, 400, 8);
    check::equal(padded.cols, 78, "8px padding each side leaves 78 columns");
    check::equal(padded.rows, 19, "8px padding each side leaves 19 rows");
    check::that(padded.originX >= 8, "text is inset by at least the padding");
    check::that(padded.originY >= 8, "text is inset vertically too");

    /* Both edges must be inset, not just the near one. */
    const int rightGap = 800 - (padded.originX + padded.cols * padded.cellWidth);
    const int bottomGap = 400 - (padded.originY + padded.rows * padded.cellHeight);
    check::that(rightGap >= 8, "the right edge keeps its padding");
    check::that(bottomGap >= 8, "the bottom edge keeps its padding");
    check::that(std::abs(rightGap - padded.originX) <= 1,
                "left and right gaps are balanced");
    check::that(std::abs(bottomGap - padded.originY) <= 1,
                "top and bottom gaps are balanced");

    /* Leftover pixels are shared, so the grid stays centred at any size. */
    const auto odd = TerminalRenderer::computeLayout(metrics, 807, 411, 4);
    const int oddRight = 807 - (odd.originX + odd.cols * odd.cellWidth);
    check::that(std::abs(oddRight - odd.originX) <= 1,
                "a non-multiple width still centres the grid");

    /* Padding must never cost the last row or column. */
    const auto tiny = TerminalRenderer::computeLayout(metrics, 12, 24, 40);
    check::that(tiny.isValid(), "a tiny viewport still produces a valid layout");
    check::equal(tiny.cols, 1, "at least one column survives excessive padding");
    check::equal(tiny.rows, 1, "at least one row survives excessive padding");
    check::that(tiny.originX + tiny.cellWidth <= 12, "the single column fits");
    check::that(tiny.originY + tiny.cellHeight <= 24, "the single row fits");

    /* Degenerate inputs. */
    check::that(!TerminalRenderer::computeLayout(metrics, 0, 400, 4).isValid(),
                "zero width yields an invalid layout");
    check::that(!TerminalRenderer::computeLayout(FontMetrics{}, 800, 400, 4).isValid(),
                "invalid metrics yield an invalid layout");
}

/* A monospaced face gives 'i' and 'W' the same advance. Font metadata can lie;
 * two glyphs of different width cannot. */
bool isMonospaced(FontManager& fonts) {
    GlyphBitmap narrow;
    GlyphBitmap wide;
    if (!fonts.rasterize(U'i', FontStyleRegular, narrow)) return false;
    if (!fonts.rasterize(U'W', FontStyleRegular, wide)) return false;
    return narrow.advanceX == wide.advanceX && narrow.advanceX > 0;
}

/* Ink in a coverage mask or the alpha of a colour glyph. */
long inkPixels(const GlyphBitmap& glyph) {
    long ink = 0;
    const int stride = glyph.bytesPerPixel();
    for (size_t i = 0; i + stride <= glyph.pixels.size(); i += stride) {
        const uint8_t value = glyph.isColor ? glyph.pixels[i + 3] : glyph.pixels[i];
        if (value > 8) ++ink;
    }
    return ink;
}

void testBoxDrawingTiles() {
    check::section("box drawing tiles exactly");

    const int cellWidth = 8;
    const int cellHeight = 16;
    std::vector<uint8_t> pixels;

    auto columnHasInk = [&](int x) {
        for (int y = 0; y < cellHeight; ++y) {
            if (pixels[static_cast<size_t>(y) * cellWidth + x]) return true;
        }
        return false;
    };
    auto rowHasInk = [&](int y) {
        for (int x = 0; x < cellWidth; ++x) {
            if (pixels[static_cast<size_t>(y) * cellWidth + x]) return true;
        }
        return false;
    };

    /*
     * The property that matters: a line must reach the cell edge, or stacking
     * two cells leaves a visible seam in the border.
     */
    check::that(renderBoxDrawing(0x2502, cellWidth, cellHeight, 1, pixels),
                "U+2502 vertical line is drawn");
    check::that(rowHasInk(0) && rowHasInk(cellHeight - 1),
                "a vertical line reaches both the top and bottom edge");

    check::that(renderBoxDrawing(0x2500, cellWidth, cellHeight, 1, pixels),
                "U+2500 horizontal line is drawn");
    check::that(columnHasInk(0) && columnHasInk(cellWidth - 1),
                "a horizontal line reaches both the left and right edge");

    /* A corner must reach only its two edges. */
    check::that(renderBoxDrawing(0x250C, cellWidth, cellHeight, 1, pixels),
                "U+250C top-left corner is drawn");
    check::that(columnHasInk(cellWidth - 1) && rowHasInk(cellHeight - 1),
                "the corner reaches its right and bottom edges");
    check::that(!columnHasInk(0) && !rowHasInk(0),
                "the corner does not reach its left or top edges");

    /* A cross reaches all four. */
    check::that(renderBoxDrawing(0x253C, cellWidth, cellHeight, 1, pixels),
                "U+253C cross is drawn");
    check::that(rowHasInk(0) && rowHasInk(cellHeight - 1)
                && columnHasInk(0) && columnHasInk(cellWidth - 1),
                "the cross reaches all four edges");

    /* Blocks must be exact, not approximate. */
    check::that(renderBoxDrawing(0x2588, cellWidth, cellHeight, 1, pixels),
                "U+2588 full block is drawn");
    check::equal(std::count_if(pixels.begin(), pixels.end(), [](uint8_t v) { return v != 0; }),
                 static_cast<long>(cellWidth) * cellHeight,
                 "the full block covers every pixel of the cell");

    check::that(renderBoxDrawing(0x2580, cellWidth, cellHeight, 1, pixels),
                "U+2580 upper half block is drawn");
    check::that(rowHasInk(0) && rowHasInk(cellHeight / 2 - 1) && !rowHasInk(cellHeight / 2),
                "the upper half block fills exactly the top half");

    /* A double line lays down two separate strokes. */
    check::that(renderBoxDrawing(0x2551, cellWidth, cellHeight, 1, pixels),
                "U+2551 double vertical is drawn");
    int runs = 0;
    bool inRun = false;
    for (int x = 0; x < cellWidth; ++x) {
        const bool ink = pixels[x] != 0;
        if (ink && !inRun) ++runs;
        inRun = ink;
    }
    check::equal(runs, 2, "the double vertical is two strokes");

    /* Shades produce partial coverage, not a solid block or nothing. */
    check::that(renderBoxDrawing(0x2592, cellWidth, cellHeight, 1, pixels),
                "U+2592 medium shade is drawn");
    const long shadeInk = std::count_if(pixels.begin(), pixels.end(),
                                        [](uint8_t v) { return v != 0; });
    const long total = static_cast<long>(cellWidth) * cellHeight;
    check::that(shadeInk > total / 8 && shadeInk < total,
                "the medium shade is partial coverage");

    check::that(!isBoxDrawingCodepoint(U'A'), "'A' is not treated as box drawing");
    check::that(isBoxDrawingCodepoint(0x2500) && isBoxDrawingCodepoint(0x2588),
                "line and block characters are recognised");
    check::that(!renderBoxDrawing(U'A', cellWidth, cellHeight, 1, pixels),
                "an unsupported code point is declined so a font can serve it");
}

void testFallbackCoversTerminalCharacters() {
    check::section("fallback chain covers what a TUI draws");

    FontManager fonts;
    /* Deliberately a font that is missing a lot: patched icon fonts commonly
     * carry thousands of glyphs and no box drawing at all. */
    if (!fonts.loadFamily(std::vector<std::string>{"DroidSansMono Nerd Font"}, 26.0)) {
        /* Not installed on this machine; exercise the chain from whatever is. */
        check::that(fonts.loadFamily(std::vector<std::string>{}, 26.0),
                    "a primary font loaded");
    }

    struct Case { char32_t codepoint; const char* what; bool expectColor; };
    const Case cases[] = {
        {U'A',     "latin A",            false},
        {0x2500,   "box drawing line",   false},
        {0x2502,   "box drawing vertical", false},
        {0x2588,   "full block",         false},
        {0x2713,   "check mark",         false},
        {0x2192,   "right arrow",        false},
        {0x25CF,   "black circle",       false},
        {0x1F600,  "emoji grinning",     true},
        {0x1F4C1,  "emoji folder",       true},
        {0x2705,   "emoji white check",  true},
    };

    for (const Case& item : cases) {
        GlyphBitmap glyph;
        const bool ok = fonts.rasterize(item.codepoint, FontStyleRegular, glyph);
        const std::string label = std::string(item.what) + " (U+"
                                + std::to_string(static_cast<unsigned>(item.codepoint)) + ")";

        check::that(ok, "rasterized " + label);
        if (!ok) continue;

        /* The real symptom of a missing glyph is an empty or hollow box, so
         * require actual ink rather than merely a successful call. */
        check::that(inkPixels(glyph) > 0, "there is ink for " + label);

        if (item.expectColor) {
            check::that(glyph.isColor, label + " came back as a colour glyph");
        }
    }
}

void testFontFallback() {
    check::section("font resolution never yields a proportional font");

    /*
     * fc-match always substitutes something rather than failing, and the
     * substitute can be proportional -- asking for a font that is not installed
     * used to produce Verdana, which is unusable in a character grid.
     */
    const std::vector<std::vector<std::string>> cases = {
        {"No Such Font 12345", "Also Not Installed"},
        {},
        {"Definitely Not A Real Font"},
        {"Verdana"},                       // installed, but proportional
        {"Arial", "Comic Sans MS"},        // ditto
    };

    for (const std::vector<std::string>& preferences : cases) {
        FontManager fonts;
        const bool loaded = fonts.loadFamily(preferences, 26.0);
        const std::string label = preferences.empty()
                                      ? std::string("<empty preference list>")
                                      : preferences.front();

        check::that(loaded, "a font was loaded for " + label);
        if (!loaded) continue;
        check::that(fonts.metrics().isValid(), "metrics are valid for " + label);
        check::that(isMonospaced(fonts),
                    "the loaded face is monospaced for " + label
                        + " (got " + fonts.familyName() + ")");
    }
}

void testFontPreferenceOrder() {
    check::section("font preference order");

    /* A family that is installed must win over later entries, and a missing
     * leading entry must not prevent a later one from being chosen. */
    const std::string systemFamily = FontManager::defaultMonospaceFamily();
    check::that(!systemFamily.empty(), "the platform names a monospaced default");

    FontManager direct;
    if (direct.loadFamily(std::vector<std::string>{systemFamily}, 20.0)) {
        const std::string chosen = direct.familyName();

        FontManager afterMiss;
        const bool ok = afterMiss.loadFamily(
            {"Nonexistent Font AAA", "Nonexistent Font BBB", systemFamily}, 20.0);
        check::that(ok, "a preference list with leading misses still loads");
        check::equal(afterMiss.familyName(), chosen,
                     "the first installed preference is the one used");
    }
}

void testFontMetricsScaleWithPixelSize() {
    check::section("font metrics scale with pixel size");

    FontManager fonts;
    if (!fonts.loadFamily(std::vector<std::string>{}, 13.0)) {
        check::that(false, "could not load any font");
        return;
    }
    const int cellWidthAt13 = fonts.metrics().cellWidth;
    const int cellHeightAt13 = fonts.metrics().cellHeight;

    /*
     * Doubling the pixel size is what happens on a 2x display. The cell must
     * grow with it -- that is the whole HiDPI fix -- so a stale cell size here
     * would mean glyphs are being stretched again.
     */
    check::that(fonts.setPixelSize(26.0), "the pixel size can be changed");
    check::that(fonts.metrics().cellWidth > cellWidthAt13,
                "the cell got wider at 26px");
    check::that(fonts.metrics().cellHeight > cellHeightAt13,
                "the cell got taller at 26px");

    GlyphBitmap small;
    GlyphBitmap large;
    fonts.setPixelSize(13.0);
    fonts.rasterize(U'g', FontStyleRegular, small);
    fonts.setPixelSize(26.0);
    fonts.rasterize(U'g', FontStyleRegular, large);
    check::that(large.pixels.size() > small.pixels.size() * 2,
                "the 26px glyph carries substantially more coverage data");
}

} // namespace

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);

    testLayoutPadding();
    testBoxDrawingTiles();
    testFallbackCoversTerminalCharacters();
    testFontFallback();
    testFontPreferenceOrder();
    testFontMetricsScaleWithPixelSize();
    return check::report("test_render");
}
