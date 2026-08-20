/*
 * Box drawing - geometric line and block glyphs
 */

#include "box_drawing.h"
#include <algorithm>
#include <cstring>

namespace {

/* Stroke weight of one arm of a line character. */
enum Weight : uint8_t {
    None = 0,
    Light = 1,
    Heavy = 2,
    Double = 3,
};

/*
 * A line character is fully described by the weight of its four arms. Encoding
 * it that way turns ~150 code points into one table and one draw routine,
 * instead of ~150 special cases.
 */
struct LineGlyph {
    char32_t codepoint;
    uint8_t left, right, up, down;
    /* Dash count for the dashed variants; 0 for a solid line. */
    uint8_t dashes;
};

constexpr LineGlyph kLineGlyphs[] = {
    /* Plain lines */
    {0x2500, Light, Light, None,  None,  0},
    {0x2501, Heavy, Heavy, None,  None,  0},
    {0x2502, None,  None,  Light, Light, 0},
    {0x2503, None,  None,  Heavy, Heavy, 0},

    /* Dashed lines: same arms, broken into n segments */
    {0x2504, Light, Light, None,  None,  3},
    {0x2505, Heavy, Heavy, None,  None,  3},
    {0x2506, None,  None,  Light, Light, 3},
    {0x2507, None,  None,  Heavy, Heavy, 3},
    {0x2508, Light, Light, None,  None,  4},
    {0x2509, Heavy, Heavy, None,  None,  4},
    {0x250A, None,  None,  Light, Light, 4},
    {0x250B, None,  None,  Heavy, Heavy, 4},

    /* Corners */
    {0x250C, None,  Light, None,  Light, 0},
    {0x250D, None,  Heavy, None,  Light, 0},
    {0x250E, None,  Light, None,  Heavy, 0},
    {0x250F, None,  Heavy, None,  Heavy, 0},
    {0x2510, Light, None,  None,  Light, 0},
    {0x2511, Heavy, None,  None,  Light, 0},
    {0x2512, Light, None,  None,  Heavy, 0},
    {0x2513, Heavy, None,  None,  Heavy, 0},
    {0x2514, None,  Light, Light, None,  0},
    {0x2515, None,  Heavy, Light, None,  0},
    {0x2516, None,  Light, Heavy, None,  0},
    {0x2517, None,  Heavy, Heavy, None,  0},
    {0x2518, Light, None,  Light, None,  0},
    {0x2519, Heavy, None,  Light, None,  0},
    {0x251A, Light, None,  Heavy, None,  0},
    {0x251B, Heavy, None,  Heavy, None,  0},

    /* Tees, left */
    {0x251C, None,  Light, Light, Light, 0},
    {0x251D, None,  Heavy, Light, Light, 0},
    {0x251E, None,  Light, Heavy, Light, 0},
    {0x251F, None,  Light, Light, Heavy, 0},
    {0x2520, None,  Light, Heavy, Heavy, 0},
    {0x2521, None,  Heavy, Heavy, Light, 0},
    {0x2522, None,  Heavy, Light, Heavy, 0},
    {0x2523, None,  Heavy, Heavy, Heavy, 0},

    /* Tees, right */
    {0x2524, Light, None,  Light, Light, 0},
    {0x2525, Heavy, None,  Light, Light, 0},
    {0x2526, Light, None,  Heavy, Light, 0},
    {0x2527, Light, None,  Light, Heavy, 0},
    {0x2528, Light, None,  Heavy, Heavy, 0},
    {0x2529, Heavy, None,  Heavy, Light, 0},
    {0x252A, Heavy, None,  Light, Heavy, 0},
    {0x252B, Heavy, None,  Heavy, Heavy, 0},

    /* Tees, down */
    {0x252C, Light, Light, None,  Light, 0},
    {0x252D, Heavy, Light, None,  Light, 0},
    {0x252E, Light, Heavy, None,  Light, 0},
    {0x252F, Heavy, Heavy, None,  Light, 0},
    {0x2530, Light, Light, None,  Heavy, 0},
    {0x2531, Heavy, Light, None,  Heavy, 0},
    {0x2532, Light, Heavy, None,  Heavy, 0},
    {0x2533, Heavy, Heavy, None,  Heavy, 0},

    /* Tees, up */
    {0x2534, Light, Light, Light, None,  0},
    {0x2535, Heavy, Light, Light, None,  0},
    {0x2536, Light, Heavy, Light, None,  0},
    {0x2537, Heavy, Heavy, Light, None,  0},
    {0x2538, Light, Light, Heavy, None,  0},
    {0x2539, Heavy, Light, Heavy, None,  0},
    {0x253A, Light, Heavy, Heavy, None,  0},
    {0x253B, Heavy, Heavy, Heavy, None,  0},

    /* Crosses */
    {0x253C, Light, Light, Light, Light, 0},
    {0x253D, Heavy, Light, Light, Light, 0},
    {0x253E, Light, Heavy, Light, Light, 0},
    {0x253F, Heavy, Heavy, Light, Light, 0},
    {0x2540, Light, Light, Heavy, Light, 0},
    {0x2541, Light, Light, Light, Heavy, 0},
    {0x2542, Light, Light, Heavy, Heavy, 0},
    {0x2543, Heavy, Light, Heavy, Light, 0},
    {0x2544, Light, Heavy, Heavy, Light, 0},
    {0x2545, Heavy, Light, Light, Heavy, 0},
    {0x2546, Light, Heavy, Light, Heavy, 0},
    {0x2547, Heavy, Heavy, Heavy, Light, 0},
    {0x2548, Heavy, Heavy, Light, Heavy, 0},
    {0x2549, Heavy, Light, Heavy, Heavy, 0},
    {0x254A, Light, Heavy, Heavy, Heavy, 0},
    {0x254B, Heavy, Heavy, Heavy, Heavy, 0},

    /* Light/heavy half lines */
    {0x2574, Light, None,  None,  None,  0},
    {0x2575, None,  None,  Light, None,  0},
    {0x2576, None,  Light, None,  None,  0},
    {0x2577, None,  None,  None,  Light, 0},
    {0x2578, Heavy, None,  None,  None,  0},
    {0x2579, None,  None,  Heavy, None,  0},
    {0x257A, None,  Heavy, None,  None,  0},
    {0x257B, None,  None,  None,  Heavy, 0},

    /* Doubles */
    {0x2550, Double, Double, None,   None,   0},
    {0x2551, None,   None,   Double, Double, 0},
    {0x2552, None,   Double, None,   Light,  0},
    {0x2553, None,   Light,  None,   Double, 0},
    {0x2554, None,   Double, None,   Double, 0},
    {0x2555, Double, None,   None,   Light,  0},
    {0x2556, Light,  None,   None,   Double, 0},
    {0x2557, Double, None,   None,   Double, 0},
    {0x2558, None,   Double, Light,  None,   0},
    {0x2559, None,   Light,  Double, None,   0},
    {0x255A, None,   Double, Double, None,   0},
    {0x255B, Double, None,   Light,  None,   0},
    {0x255C, Light,  None,   Double, None,   0},
    {0x255D, Double, None,   Double, None,   0},
    {0x255E, None,   Double, Light,  Light,  0},
    {0x255F, None,   Light,  Double, Double, 0},
    {0x2560, None,   Double, Double, Double, 0},
    {0x2561, Double, None,   Light,  Light,  0},
    {0x2562, Light,  None,   Double, Double, 0},
    {0x2563, Double, None,   Double, Double, 0},
    {0x2564, Double, Double, None,   Light,  0},
    {0x2565, Light,  Light,  None,   Double, 0},
    {0x2566, Double, Double, None,   Double, 0},
    {0x2567, Double, Double, Light,  None,   0},
    {0x2568, Light,  Light,  Double, None,   0},
    {0x2569, Double, Double, Double, None,   0},
    {0x256A, Double, Double, Light,  Light,  0},
    {0x256B, Light,  Light,  Double, Double, 0},
    {0x256C, Double, Double, Double, Double, 0},

    /*
     * Rounded corners drawn square. An arc would need per-pixel coverage and at
     * terminal cell sizes the difference is a pixel or two; tiling matters more.
     */
    {0x256D, None,  Light, None,  Light, 0},
    {0x256E, Light, None,  None,  Light, 0},
    {0x256F, Light, None,  Light, None,  0},
    {0x2570, None,  Light, Light, None,  0},
};

const LineGlyph* findLineGlyph(char32_t codepoint) {
    for (const LineGlyph& glyph : kLineGlyphs) {
        if (glyph.codepoint == codepoint) return &glyph;
    }
    return nullptr;
}

class Canvas {
public:
    Canvas(std::vector<uint8_t>& pixels, int width, int height)
        : pixels_(pixels), width_(width), height_(height) {
        pixels_.assign(static_cast<size_t>(width) * static_cast<size_t>(height), 0);
    }

    /* Fill a half-open rectangle, clipped to the cell. */
    void fill(int x0, int y0, int x1, int y1, uint8_t value = 255) {
        x0 = std::max(0, x0);
        y0 = std::max(0, y0);
        x1 = std::min(width_, x1);
        y1 = std::min(height_, y1);
        for (int y = y0; y < y1; ++y) {
            std::memset(pixels_.data() + static_cast<size_t>(y) * width_ + x0,
                        value, static_cast<size_t>(x1 - x0));
        }
    }

    /* Fill every `period`-th pixel, for the shade characters. */
    void dither(int numerator, int denominator) {
        for (int y = 0; y < height_; ++y) {
            for (int x = 0; x < width_; ++x) {
                /* An ordered 2x2 pattern reads as an even tone at cell sizes. */
                const int cell = ((y & 1) << 1) | (x & 1);
                const bool on = (cell * denominator) / 4 < numerator;
                pixels_[static_cast<size_t>(y) * width_ + x] = on ? 255 : 0;
            }
        }
    }

private:
    std::vector<uint8_t>& pixels_;
    int width_;
    int height_;
};

/* Pixel thickness for a weight, given the base line thickness. */
int weightThickness(uint8_t weight, int base) {
    switch (weight) {
    case Light:  return base;
    case Heavy:  return std::max(base + 1, (base * 2 + 1) / 2 + base / 2);
    case Double: return base;   // drawn as two strokes of `base`
    default:     return 0;
    }
}

void drawLineGlyph(const LineGlyph& glyph, Canvas& canvas, int width, int height,
                   int base) {
    const int midX = width / 2;
    const int midY = height / 2;

    /* Distance from the centre line to each of a double line's two strokes. */
    const int doubleGap = std::max(base + 1, height / 12);

    /* Thickness of the horizontal and vertical strokes through the centre. */
    const int hWeight = std::max(glyph.left, glyph.right);
    const int vWeight = std::max(glyph.up, glyph.down);
    const int hThickness = hWeight ? weightThickness(hWeight, base) : base;
    const int vThickness = vWeight ? weightThickness(vWeight, base) : base;

    /*
     * Each arm runs from its cell edge *past* the centre by half the
     * perpendicular stroke, so a corner or tee has no notch where the two
     * strokes meet.
     */
    const int hSpan = (vWeight == Double) ? doubleGap + vThickness
                                          : (vThickness + 1) / 2;
    const int vSpan = (hWeight == Double) ? doubleGap + hThickness
                                          : (hThickness + 1) / 2;

    auto strokeBandsH = [&](uint8_t weight, int& y0, int& t, int& y0b) {
        /* Returns the one or two horizontal bands for `weight`. y0b < 0 == one. */
        if (weight == Double) {
            t = base;
            y0 = midY - doubleGap - t / 2;
            y0b = midY + doubleGap - t / 2;
        } else {
            t = weightThickness(weight, base);
            y0 = midY - t / 2;
            y0b = -1;
        }
    };

    auto drawHorizontal = [&](uint8_t weight, bool towardsLeft) {
        if (weight == None) return;
        const int x0 = towardsLeft ? 0 : midX - hSpan;
        const int x1 = towardsLeft ? midX + hSpan : width;

        int y0 = 0;
        int t = 0;
        int y0b = -1;
        strokeBandsH(weight, y0, t, y0b);
        canvas.fill(x0, y0, x1, y0 + t);
        if (y0b >= 0) canvas.fill(x0, y0b, x1, y0b + t);
    };

    auto drawVertical = [&](uint8_t weight, bool towardsUp) {
        if (weight == None) return;
        const int y0 = towardsUp ? 0 : midY - vSpan;
        const int y1 = towardsUp ? midY + vSpan : height;

        if (weight == Double) {
            const int t = base;
            canvas.fill(midX - doubleGap - t / 2, y0, midX - doubleGap - t / 2 + t, y1);
            canvas.fill(midX + doubleGap - t / 2, y0, midX + doubleGap - t / 2 + t, y1);
            return;
        }
        const int t = weightThickness(weight, base);
        canvas.fill(midX - t / 2, y0, midX - t / 2 + t, y1);
    };

    drawHorizontal(glyph.left, true);
    drawHorizontal(glyph.right, false);
    drawVertical(glyph.up, true);
    drawVertical(glyph.down, false);

    if (glyph.dashes >= 2) {
        /* Punch gaps out of the solid line just drawn. Gaps are placed between
         * segments, never at the cell edge, so consecutive cells still join. */
        const bool horizontal = (glyph.left != None || glyph.right != None);
        const int span = horizontal ? width : height;
        const int gap = std::max(1, span / (glyph.dashes * 3));

        for (int i = 1; i < glyph.dashes; ++i) {
            const int centre = (span * i) / glyph.dashes;
            const int start = centre - gap / 2;
            if (horizontal) {
                canvas.fill(start, 0, start + gap, height, 0);
            } else {
                canvas.fill(0, start, width, start + gap, 0);
            }
        }
    }
}

bool drawBlockGlyph(char32_t codepoint, Canvas& canvas, int width, int height) {
    /* Eighths measured from the relevant edge. */
    auto eighthY = [&](int n) { return height - (height * n) / 8; };
    auto eighthX = [&](int n) { return (width * n) / 8; };

    switch (codepoint) {
    case 0x2580: canvas.fill(0, 0, width, height / 2); return true;              // upper half
    case 0x2581: canvas.fill(0, eighthY(1), width, height); return true;         // lower 1/8
    case 0x2582: canvas.fill(0, eighthY(2), width, height); return true;
    case 0x2583: canvas.fill(0, eighthY(3), width, height); return true;
    case 0x2584: canvas.fill(0, eighthY(4), width, height); return true;         // lower half
    case 0x2585: canvas.fill(0, eighthY(5), width, height); return true;
    case 0x2586: canvas.fill(0, eighthY(6), width, height); return true;
    case 0x2587: canvas.fill(0, eighthY(7), width, height); return true;
    case 0x2588: canvas.fill(0, 0, width, height); return true;                  // full block
    case 0x2589: canvas.fill(0, 0, eighthX(7), height); return true;             // left 7/8
    case 0x258A: canvas.fill(0, 0, eighthX(6), height); return true;
    case 0x258B: canvas.fill(0, 0, eighthX(5), height); return true;
    case 0x258C: canvas.fill(0, 0, eighthX(4), height); return true;             // left half
    case 0x258D: canvas.fill(0, 0, eighthX(3), height); return true;
    case 0x258E: canvas.fill(0, 0, eighthX(2), height); return true;
    case 0x258F: canvas.fill(0, 0, eighthX(1), height); return true;             // left 1/8
    case 0x2590: canvas.fill(eighthX(4), 0, width, height); return true;         // right half
    case 0x2594: canvas.fill(0, 0, width, height / 8 > 0 ? height / 8 : 1); return true;
    case 0x2595: canvas.fill(eighthX(7), 0, width, height); return true;         // right 1/8

    /* Shades */
    case 0x2591: canvas.dither(1, 4); return true;   // 25%
    case 0x2592: canvas.dither(2, 4); return true;   // 50%
    case 0x2593: canvas.dither(3, 4); return true;   // 75%

    /* Quadrants */
    case 0x2596: canvas.fill(0, height / 2, width / 2, height); return true;
    case 0x2597: canvas.fill(width / 2, height / 2, width, height); return true;
    case 0x2598: canvas.fill(0, 0, width / 2, height / 2); return true;
    case 0x2599:
        canvas.fill(0, 0, width / 2, height);
        canvas.fill(0, height / 2, width, height);
        return true;
    case 0x259A:
        canvas.fill(0, 0, width / 2, height / 2);
        canvas.fill(width / 2, height / 2, width, height);
        return true;
    case 0x259B:
        canvas.fill(0, 0, width, height / 2);
        canvas.fill(0, 0, width / 2, height);
        return true;
    case 0x259C:
        canvas.fill(0, 0, width, height / 2);
        canvas.fill(width / 2, 0, width, height);
        return true;
    case 0x259D: canvas.fill(width / 2, 0, width, height / 2); return true;
    case 0x259E:
        canvas.fill(width / 2, 0, width, height / 2);
        canvas.fill(0, height / 2, width / 2, height);
        return true;
    case 0x259F:
        canvas.fill(width / 2, 0, width, height);
        canvas.fill(0, height / 2, width, height);
        return true;
    default:
        return false;
    }
}

} // namespace

bool isBoxDrawingCodepoint(char32_t codepoint) {
    if (codepoint >= 0x2580 && codepoint <= 0x259F) return true;
    return findLineGlyph(codepoint) != nullptr;
}

bool renderBoxDrawing(char32_t codepoint, int cellWidth, int cellHeight,
                      int lineThickness, std::vector<uint8_t>& pixels) {
    if (cellWidth <= 0 || cellHeight <= 0) return false;

    const int base = std::clamp(lineThickness, 1, std::max(1, cellWidth / 2));

    if (codepoint >= 0x2580 && codepoint <= 0x259F) {
        Canvas canvas(pixels, cellWidth, cellHeight);
        if (drawBlockGlyph(codepoint, canvas, cellWidth, cellHeight)) return true;
        pixels.clear();
        return false;
    }

    if (const LineGlyph* glyph = findLineGlyph(codepoint)) {
        Canvas canvas(pixels, cellWidth, cellHeight);
        drawLineGlyph(*glyph, canvas, cellWidth, cellHeight, base);
        return true;
    }

    return false;
}
