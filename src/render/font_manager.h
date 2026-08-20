/*
 * FontManager - FreeType glyph rasterization with a fallback chain
 *
 * Owns a *primary* family (one FT_Face per style) plus a lazily grown list of
 * fallback families, and rasterizes glyphs at an explicit pixel size.
 *
 * Working in pixels rather than points-plus-DPI is deliberate: the caller
 * already knows how many physical pixels a cell must be, and the old
 * point/DPI API was being fed Qt's logical DPI (72 on macOS), so a 12 pt font
 * was rasterized into a 12 px em box and stretched across 24 physical pixels.
 *
 * The fallback chain exists because no single monospaced font covers what a
 * terminal has to draw. A patched "Nerd Font" build, for instance, may carry
 * thousands of icons and still have no box-drawing characters at all -- and
 * box-drawing is what every TUI builds its borders from. Without fallback those
 * become empty .notdef boxes. Colour emoji need a separate font as well, and one
 * that stores bitmaps rather than outlines.
 */

#ifndef RENDER_FONT_MANAGER_H
#define RENDER_FONT_MANAGER_H

#include <ft2build.h>
#include FT_FREETYPE_H
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

/* Font style indices. Values double as the atlas cache key's style field. */
enum FontStyle : int {
    FontStyleRegular = 0,
    FontStyleBold = 1,
    FontStyleItalic = 2,
    FontStyleBoldItalic = 3,
    FontStyleCount = 4
};

/* Combine SGR bold/italic into a style index. */
constexpr FontStyle fontStyleFor(bool bold, bool italic) {
    return static_cast<FontStyle>((bold ? 1 : 0) | (italic ? 2 : 0));
}

/* Integer cell metrics, all in physical pixels. */
struct FontMetrics {
    int cellWidth = 0;
    int cellHeight = 0;
    int ascender = 0;               // baseline offset from the top of the cell
    int descender = 0;              // positive pixels below the baseline
    int underlinePosition = 0;      // pixels below the baseline
    int underlineThickness = 1;
    int strikethroughPosition = 0;  // pixels above the baseline

    bool isValid() const { return cellWidth > 0 && cellHeight > 0; }
};

/*
 * A rasterized glyph.
 *
 * `isColor` distinguishes the two kinds of content: an 8-bit coverage mask that
 * the renderer tints with the cell's foreground colour, or a 32-bit RGBA image
 * (a colour emoji) that must be drawn as-is. Alpha is straight, not
 * premultiplied -- FreeType hands back premultiplied BGRA and this undoes it, so
 * one blend mode serves both kinds.
 */
struct GlyphBitmap {
    std::vector<uint8_t> pixels;
    int width = 0;
    int height = 0;
    int bearingX = 0;   // left side bearing, pixels
    int bearingY = 0;   // pixels from baseline up to the top row
    int advanceX = 0;
    bool isColor = false;

    bool isEmpty() const { return width <= 0 || height <= 0; }
    /* Bytes per pixel: 4 for colour glyphs, 1 for coverage masks. */
    int bytesPerPixel() const { return isColor ? 4 : 1; }
};

/* Where a font face lives on disk. Collections (.ttc) need the face index. */
struct FontFile {
    std::string path;
    std::string family;   // the family fontconfig actually resolved to
    int faceIndex = 0;
    bool isValid() const { return !path.empty(); }
};

class FontManager {
public:
    FontManager();
    ~FontManager();

    FontManager(const FontManager&) = delete;
    FontManager& operator=(const FontManager&) = delete;

    /*
     * Load the first installed family from `preferences` at `pixelSize` physical
     * pixels per em, falling back to the platform's default monospaced font when
     * none are present. Missing bold/italic faces are synthesized.
     *
     * A preference only counts as installed if fontconfig resolves it to that
     * same family, and only if the resulting face is monospaced.
     */
    bool loadFamily(const std::vector<std::string>& preferences, double pixelSize);
    bool loadFamily(const std::string& family, double pixelSize);

    /*
     * Families to consult for code points the primary font lacks, tried in the
     * given order before automatic discovery. Call before loadFamily().
     */
    void setFallbackFamilies(const std::vector<std::string>& families);

    bool setPixelSize(double pixelSize);
    double pixelSize() const { return pixelSize_; }

    const FontMetrics& metrics() const { return metrics_; }
    bool isValid() const;
    const std::string& familyName() const { return familyName_; }

    /*
     * Rasterize one code point, searching the fallback chain when the primary
     * font does not have it. `cellWidth`/`cellHeight` size colour glyphs, which
     * come from fixed bitmap strikes rather than outlines.
     *
     * Returns false only if nothing could be rasterized at all; a code point no
     * font covers still succeeds, yielding the primary font's .notdef box.
     */
    bool rasterize(char32_t codepoint, FontStyle style, GlyphBitmap& out);

    /* Which family ended up serving `codepoint`, for diagnostics. */
    std::string familyForCodepoint(char32_t codepoint, FontStyle style);

    static FontFile resolveFontFile(const std::string& family, FontStyle style);
    static std::string defaultMonospaceFamily();

private:
    /* One family: up to four real faces, plus what kind of font it is. */
    struct FaceSet {
        std::array<FT_Face, FontStyleCount> styles{};
        std::string family;
        /* Bitmap-only, colour font (an emoji font). Rasterized with
         * FT_LOAD_COLOR and sized from the cell rather than the em. */
        bool isColor = false;
        /*
         * Multiplier on the primary pixel size, so a fallback's line height
         * matches the primary cell. Different families draw a different
         * proportion of the em: leaving them at the same em size leaves box
         * drawing a pixel or two short of the cell, and TUI borders come out
         * looking dashed.
         */
        double sizeScale = 1.0;

        ~FaceSet();
        FaceSet() = default;
        FaceSet(const FaceSet&) = delete;
        FaceSet& operator=(const FaceSet&) = delete;

        FT_Face faceFor(FontStyle style) const;
        bool hasCodepoint(char32_t codepoint) const;
    };

    static FontFile resolveExactFamily(const std::string& family, FontStyle style);
    /* Ask fontconfig for any font covering `codepoint`. */
    static std::vector<FontFile> discoverFontsFor(char32_t codepoint);

    bool loadFaceInto(FaceSet& target, const FontFile& file, FontStyle style);
    void applyPixelSize(const FaceSet& faces) const;
    void applyPixelSizeToFace(FT_Face face, bool isColorFont, double sizeScale) const;
    /* Choose `faces.sizeScale` so its line height matches the primary cell. */
    void matchFallbackSize(FaceSet& faces) const;
    void computeMetrics();

    bool regularFaceIsMonospaced() const;
    bool tryPrimaryRegular(const FontFile& file);

    /* Pick the face set that covers `codepoint`, growing the chain if needed. */
    const FaceSet* resolveFaceSet(char32_t codepoint);
    void loadConfiguredFallbacks();
    /* Append `file` as a fallback if it really covers `codepoint`. */
    const FaceSet* adoptFallback(const FontFile& file, char32_t codepoint);

    bool rasterizeFrom(const FaceSet& faces, FontStyle style, FT_UInt glyphIndex,
                       GlyphBitmap& out) const;

    FT_Library ftLibrary_ = nullptr;
    FaceSet primary_;
    std::vector<std::unique_ptr<FaceSet>> fallbacks_;

    std::vector<std::string> fallbackPreferences_;
    bool configuredFallbacksLoaded_ = false;

    /*
     * Code point to face set: nullptr means "the primary font, or nothing
     * covers it". Caching matters because discovery shells out to fc-match.
     */
    std::unordered_map<char32_t, const FaceSet*> resolution_;

    std::string familyName_;
    double pixelSize_ = 0.0;
    FontMetrics metrics_;
};

#endif /* RENDER_FONT_MANAGER_H */
