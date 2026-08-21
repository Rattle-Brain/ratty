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
    /*
     * Height of a capital letter. Not used for layout -- it is what colour
     * emoji are sized against, so that an icon sits among the uppercase letters
     * rather than filling the whole line box.
     */
    int capHeight = 0;

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

/*
 * Which form of a dual-form code point is wanted.
 *
 * Many code points have both a monochrome text glyph and a colour emoji glyph,
 * in different fonts. U+26A0 is a narrow warning sign until a U+FE0F selector
 * asks for the emoji; U+FE0E asks for the text form. Without this the font
 * chain simply returns whichever font it reaches first, so the selector has no
 * effect at all.
 */
enum class GlyphPresentation : uint8_t {
    Auto = 0,    // first font in the chain that has the glyph
    Text = 1,    // prefer a monochrome font
    Emoji = 2,   // prefer a colour font
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
    /*
     * A quarter again the height of a capital.
     *
     * Above parity with an `M` on purpose. An emoji is a round, busy shape and a
     * capital is a flat one, so matching their heights makes the emoji look
     * *smaller* than the text rather than equal to it -- the same optical
     * correction a typeface makes when it draws `O` slightly taller than `H`.
     * Sized at 0.9 the emoji were legibly too small, and worst at small font
     * sizes where the cell leaves least room to begin with.
     *
     * The value is bounded at use by the cell the glyph occupies, so raising it
     * cannot reintroduce the overlap this replaced -- see colorGlyphTarget().
     */
    static constexpr double DefaultEmojiScale = 1.25;

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

    /*
     * How tall a colour emoji is drawn, as a multiple of the primary font's
     * capital height. 1.0 makes an emoji exactly as tall as an `M`.
     *
     * This exists as a dial because it is purely a matter of taste, and because
     * the alternative -- letting the emoji font decide -- does not work: a colour
     * font ships a handful of fixed bitmap strikes (Apple Color Emoji has 20, 26
     * and 40 pixels among others) and FreeType answers a size request with the
     * nearest one. That is how a 13 px cell ended up with a 20 px emoji in it,
     * and why the size used to hop about as the font size changed instead of
     * following it. Whatever strike comes back is now resampled to the size
     * asked for here.
     */
    void setEmojiScale(double scale);
    double emojiScale() const { return emojiScale_; }

    /*
     * A ready-made FontManager for this exact font request, shared with every
     * other caller that asks for the same thing.
     *
     * Faces, metrics and the resolved fallback chain depend on nothing but
     * (families, fallbacks, pixelSize), so two panes showing the same font at
     * the same size have no reason to own separate copies -- and building a copy
     * is the expensive part: opening four to eight FT faces and discovering the
     * fallback chain. Panes come and go constantly (every split reparents them),
     * so this is what makes a new pane cheap.
     *
     * Returns nullptr when no font could be loaded at all. The result is
     * intended to be held for as long as it is used; a chain nobody holds any
     * more is dropped when the next distinct request arrives.
     */
    static std::shared_ptr<FontManager> shared(const std::vector<std::string>& families,
                                               const std::vector<std::string>& fallbacks,
                                               double pixelSize,
                                               double emojiScale);

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
    bool rasterize(char32_t codepoint, FontStyle style, GlyphBitmap& out,
                   GlyphPresentation presentation = GlyphPresentation::Auto);

    /* Which family ended up serving `codepoint`, for diagnostics. */
    std::string familyForCodepoint(char32_t codepoint, FontStyle style,
                                   GlyphPresentation presentation = GlyphPresentation::Auto);

    static FontFile resolveFontFile(const std::string& family, FontStyle style);
    static std::string defaultMonospaceFamily();

private:
    /* One family: up to four real faces, plus what kind of font it is. */
    struct FaceSet {
        std::array<FT_Face, FontStyleCount> styles{};
        /*
         * Backing bytes for a face loaded from memory rather than from a path,
         * which is how the bundled symbols font is used. FreeType does not copy
         * the buffer, so it has to outlive every face made from it.
         */
        std::vector<unsigned char> embedded;
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
        /*
         * True only if the face draws something for `codepoint`. A cmap entry is
         * not enough: colour emoji fonts map regional indicators and keycap
         * digits to *empty* glyphs, because the real flag or keycap is only
         * reachable by shaping the whole sequence. Selecting such a face would
         * render nothing at all.
         */
        bool hasRenderableGlyph(char32_t codepoint) const;
    };

    /* The uncached body of defaultMonospaceFamily(), which memoizes it. */
    static std::string computeDefaultMonospaceFamily();
    static FontFile resolveExactFamily(const std::string& family, FontStyle style);
    /* Ask fontconfig for any font covering `codepoint`. */
    static std::vector<FontFile> discoverFontsFor(char32_t codepoint);

    bool loadFaceInto(FaceSet& target, const FontFile& file, FontStyle style);
    /*
     * The symbols font compiled into the binary, adopted as a fallback for the
     * private-use icon code points that no stock font carries. Terminals that
     * show icons on a bare machine (kitty, Ghostty) all ship this font; relying
     * on the system's is what leaves a TUI full of empty boxes.
     */
    void loadBundledSymbolsFallback();
    void applyPixelSize(const FaceSet& faces) const;
    void applyPixelSizeToFace(FT_Face face, bool isColorFont, double sizeScale) const;
    /* Choose `faces.sizeScale` so its line height matches the primary cell. */
    void matchFallbackSize(FaceSet& faces) const;
    void computeMetrics();

    bool regularFaceIsMonospaced() const;
    bool tryPrimaryRegular(const FontFile& file);

    /* Pick the face set that covers `codepoint`, growing the chain if needed. */
    const FaceSet* resolveFaceSet(char32_t codepoint, GlyphPresentation presentation);
    void loadConfiguredFallbacks();
    /* Append `file` as a fallback if it really covers `codepoint`. */
    const FaceSet* adoptFallback(const FontFile& file, char32_t codepoint);

    bool rasterizeFrom(const FaceSet& faces, FontStyle style, FT_UInt glyphIndex,
                       GlyphBitmap& out) const;
    /* The box a colour glyph has to fit: as tall as emojiScale() capitals, and
     * never wider than the two cells emoji presentation occupies. */
    int colorGlyphTarget() const;
    /*
     * Resample a colour bitmap down to `targetWidth` x `targetHeight`, in
     * premultiplied space, and leave straight RGBA in `out`. Done here rather
     * than by drawing a smaller quad because the atlas is sampled 1:1 with
     * GL_NEAREST -- scaling at draw time would alias the bitmap instead of
     * resizing it.
     */
    static void storeColorBitmap(const FT_Bitmap& bitmap, int targetWidth, int targetHeight,
                                 GlyphBitmap& out);
    /* Cache key combining a code point with the presentation asked for. */
    static uint64_t resolutionKey(char32_t codepoint, GlyphPresentation presentation) {
        return (static_cast<uint64_t>(codepoint) << 2)
             | static_cast<uint64_t>(presentation);
    }

    FT_Library ftLibrary_ = nullptr;
    FaceSet primary_;
    std::vector<std::unique_ptr<FaceSet>> fallbacks_;

    std::vector<std::string> fallbackPreferences_;
    bool configuredFallbacksLoaded_ = false;

    /*
     * (code point, presentation) to face set; nullptr means nothing covers it.
     * Caching matters because discovery shells out to fc-match.
     */
    std::unordered_map<uint64_t, const FaceSet*> resolution_;

    std::string familyName_;
    double pixelSize_ = 0.0;
    double emojiScale_ = DefaultEmojiScale;
    FontMetrics metrics_;
};

#endif /* RENDER_FONT_MANAGER_H */
