/*
 * FontManager - FreeType2 font rasterization wrapper
 *
 * Manages font faces for different styles (Regular, Bold, Italic, Bold+Italic)
 * and provides glyph rasterization for terminal rendering
 */

#ifndef RENDER_FONT_MANAGER_H
#define RENDER_FONT_MANAGER_H

#include <ft2build.h>
#include FT_FREETYPE_H
#include <cstdint>
#include <string>
#include <array>

/* Font style indices */
enum FontStyle {
    FontStyleRegular = 0,
    FontStyleBold = 1,
    FontStyleItalic = 2,
    FontStyleBoldItalic = 3,
    FontStyleCount = 4
};

/* Font metrics for terminal layout */
struct FontMetrics {
    int cellWidth;              // Advance width for monospace
    int cellHeight;             // Line height (ascender + descender + gap)
    int ascender;               // Pixels above baseline
    int descender;              // Pixels below baseline (positive value)
    int underlinePosition;      // Offset from baseline
    int underlineThickness;
    int strikethroughPosition;
};

/* Glyph bitmap returned from rasterization */
struct GlyphBitmap {
    uint8_t* bitmap;            // Grayscale 8-bit bitmap (owned by this struct)
    int width;                  // Bitmap width
    int height;                 // Bitmap height
    int bearingX;               // Horizontal offset from origin
    int bearingY;               // Vertical offset from baseline
    int advanceX;               // Horizontal advance
    uint32_t glyphIndex;        // Font glyph index

    GlyphBitmap();
    ~GlyphBitmap();

    // Delete copy, allow move
    GlyphBitmap(const GlyphBitmap&) = delete;
    GlyphBitmap& operator=(const GlyphBitmap&) = delete;
    GlyphBitmap(GlyphBitmap&& other) noexcept;
    GlyphBitmap& operator=(GlyphBitmap&& other) noexcept;
};

class FontManager {
public:
    FontManager();
    ~FontManager();

    // Delete copy, allow move
    FontManager(const FontManager&) = delete;
    FontManager& operator=(const FontManager&) = delete;
    FontManager(FontManager&& other) noexcept;
    FontManager& operator=(FontManager&& other) noexcept;

    // Font loading
    bool load(const std::string& path, FontStyle style, int sizePt, int dpi = 96);
    bool loadDefault(int sizePt, int dpi = 96);

    // Set font size (affects all loaded faces)
    bool setSize(int sizePt, int dpi = -1);

    // Get metrics (based on regular style)
    FontMetrics getMetrics() const;

    // Rasterize a single glyph by codepoint
    bool rasterizeGlyph(uint32_t codepoint, FontStyle style, GlyphBitmap& out);

    // Rasterize a single glyph by glyph index (for HarfBuzz integration)
    bool rasterizeGlyphIndex(uint32_t glyphIndex, FontStyle style, GlyphBitmap& out);

    // Get glyph index for a codepoint
    uint32_t getGlyphIndex(uint32_t codepoint, FontStyle style) const;

    // Get FreeType face (for HarfBuzz integration)
    FT_Face getFace(FontStyle style) const;

    // Check if a style is loaded
    bool hasStyle(FontStyle style) const;

    // Check if font manager is valid
    bool isValid() const { return ftLibrary_ != nullptr; }

private:
    void computeMetrics();
    FT_Face getFaceForStyle(FontStyle style) const;

    FT_Library ftLibrary_;
    std::array<FT_Face, FontStyleCount> faces_;
    std::array<bool, FontStyleCount> faceLoaded_;

    int sizePt_;
    int dpi_;
    FontMetrics metrics_;
    bool metricsValid_;
};

#endif /* RENDER_FONT_MANAGER_H */
