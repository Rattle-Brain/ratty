/*
 * FontManager - FreeType2 font rasterization implementation
 */

#include "font_manager.h"
#include <cstring>
#include <cstdio>
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_GLYPH_H
#include FT_SYNTHESIS_H

// GlyphBitmap implementation
GlyphBitmap::GlyphBitmap()
    : bitmap(nullptr)
    , width(0)
    , height(0)
    , bearingX(0)
    , bearingY(0)
    , advanceX(0)
    , glyphIndex(0)
{
}

GlyphBitmap::~GlyphBitmap() {
    if (bitmap) {
        free(bitmap);
        bitmap = nullptr;
    }
}

GlyphBitmap::GlyphBitmap(GlyphBitmap&& other) noexcept
    : bitmap(other.bitmap)
    , width(other.width)
    , height(other.height)
    , bearingX(other.bearingX)
    , bearingY(other.bearingY)
    , advanceX(other.advanceX)
    , glyphIndex(other.glyphIndex)
{
    other.bitmap = nullptr;
    other.width = 0;
    other.height = 0;
}

GlyphBitmap& GlyphBitmap::operator=(GlyphBitmap&& other) noexcept {
    if (this != &other) {
        if (bitmap) {
            free(bitmap);
        }

        bitmap = other.bitmap;
        width = other.width;
        height = other.height;
        bearingX = other.bearingX;
        bearingY = other.bearingY;
        advanceX = other.advanceX;
        glyphIndex = other.glyphIndex;

        other.bitmap = nullptr;
        other.width = 0;
        other.height = 0;
    }
    return *this;
}

// FontManager implementation
FontManager::FontManager()
    : ftLibrary_(nullptr)
    , faces_()
    , faceLoaded_()
    , sizePt_(12)
    , dpi_(96)
    , metrics_()
    , metricsValid_(false)
{
    faces_.fill(nullptr);
    faceLoaded_.fill(false);

    if (FT_Init_FreeType(&ftLibrary_) != 0) {
        fprintf(stderr, "FontManager: Failed to initialize FreeType\n");
        ftLibrary_ = nullptr;
    }
}

FontManager::~FontManager() {
    for (int i = 0; i < FontStyleCount; i++) {
        if (faces_[i]) {
            FT_Done_Face(faces_[i]);
        }
    }

    if (ftLibrary_) {
        FT_Done_FreeType(ftLibrary_);
    }
}

FontManager::FontManager(FontManager&& other) noexcept
    : ftLibrary_(other.ftLibrary_)
    , faces_(std::move(other.faces_))
    , faceLoaded_(std::move(other.faceLoaded_))
    , sizePt_(other.sizePt_)
    , dpi_(other.dpi_)
    , metrics_(other.metrics_)
    , metricsValid_(other.metricsValid_)
{
    other.ftLibrary_ = nullptr;
    other.faces_.fill(nullptr);
    other.faceLoaded_.fill(false);
}

FontManager& FontManager::operator=(FontManager&& other) noexcept {
    if (this != &other) {
        // Clean up current resources
        for (int i = 0; i < FontStyleCount; i++) {
            if (faces_[i]) {
                FT_Done_Face(faces_[i]);
            }
        }
        if (ftLibrary_) {
            FT_Done_FreeType(ftLibrary_);
        }

        // Transfer ownership
        ftLibrary_ = other.ftLibrary_;
        faces_ = std::move(other.faces_);
        faceLoaded_ = std::move(other.faceLoaded_);
        sizePt_ = other.sizePt_;
        dpi_ = other.dpi_;
        metrics_ = other.metrics_;
        metricsValid_ = other.metricsValid_;

        other.ftLibrary_ = nullptr;
        other.faces_.fill(nullptr);
        other.faceLoaded_.fill(false);
    }
    return *this;
}

void FontManager::computeMetrics() {
    FT_Face face = faces_[FontStyleRegular];
    if (!face) {
        // Try to find any loaded face
        for (int i = 0; i < FontStyleCount; i++) {
            if (faces_[i]) {
                face = faces_[i];
                break;
            }
        }
    }

    if (!face) {
        metricsValid_ = false;
        return;
    }

    // FreeType metrics are in 26.6 fixed point (divide by 64)
    metrics_.ascender = face->size->metrics.ascender >> 6;
    metrics_.descender = -(face->size->metrics.descender >> 6);
    metrics_.cellHeight = face->size->metrics.height >> 6;

    // For monospace fonts, use the advance of 'M' or 'W'
    FT_UInt glyphIndex = FT_Get_Char_Index(face, 'M');
    if (glyphIndex && FT_Load_Glyph(face, glyphIndex, FT_LOAD_DEFAULT) == 0) {
        metrics_.cellWidth = face->glyph->advance.x >> 6;
    } else {
        // Fallback: estimate from max_advance
        metrics_.cellWidth = face->size->metrics.max_advance >> 6;
    }

    // Underline/strikethrough positions
    if (face->underline_position != 0) {
        metrics_.underlinePosition = metrics_.ascender - (face->underline_position >> 6);
        metrics_.underlineThickness = face->underline_thickness >> 6;
        if (metrics_.underlineThickness < 1) {
            metrics_.underlineThickness = 1;
        }
    } else {
        // Defaults if not specified
        metrics_.underlinePosition = metrics_.ascender + 2;
        metrics_.underlineThickness = 1;
    }

    metrics_.strikethroughPosition = metrics_.ascender / 2;
    metricsValid_ = true;
}

bool FontManager::load(const std::string& path, FontStyle style, int sizePt, int dpi) {
    if (!ftLibrary_ || style < 0 || style >= FontStyleCount) {
        return false;
    }

    // Free existing face for this style
    if (faces_[style]) {
        FT_Done_Face(faces_[style]);
        faces_[style] = nullptr;
        faceLoaded_[style] = false;
    }

    // Load the font face
    FT_Face face;
    if (FT_New_Face(ftLibrary_, path.c_str(), 0, &face) != 0) {
        fprintf(stderr, "FontManager: Failed to load font: %s\n", path.c_str());
        return false;
    }

    // Set character size
    if (dpi <= 0) dpi = 96;
    if (sizePt <= 0) sizePt = 12;

    if (FT_Set_Char_Size(face, 0, sizePt * 64, dpi, dpi) != 0) {
        FT_Done_Face(face);
        fprintf(stderr, "FontManager: Failed to set char size\n");
        return false;
    }

    faces_[style] = face;
    faceLoaded_[style] = true;
    sizePt_ = sizePt;
    dpi_ = dpi;

    // Recompute metrics
    computeMetrics();

    return true;
}

bool FontManager::loadDefault(int sizePt, int dpi) {
    if (!ftLibrary_) return false;

    // Try common monospace fonts in order of preference
    const char* fontPaths[] = {
        // macOS
        "/System/Library/Fonts/Monaco.ttf",
        "/System/Library/Fonts/Menlo.ttc",
        "/Library/Fonts/SF-Mono-Regular.otf",
        "/System/Library/Fonts/SFNSMono.ttf",
        // Linux
        "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
        "/usr/share/fonts/TTF/DejaVuSansMono.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf",
        "/usr/share/fonts/truetype/freefont/FreeMono.ttf",
        "/usr/share/fonts/truetype/ubuntu/UbuntuMono-R.ttf",
        // Fallback
        "/usr/share/fonts/truetype/noto/NotoMono-Regular.ttf",
        nullptr
    };

    for (int i = 0; fontPaths[i] != nullptr; i++) {
        if (load(fontPaths[i], FontStyleRegular, sizePt, dpi)) {
            return true;
        }
    }

    fprintf(stderr, "FontManager: No default monospace font found\n");
    return false;
}

bool FontManager::setSize(int sizePt, int dpi) {
    if (!ftLibrary_ || sizePt <= 0) return false;
    if (dpi <= 0) dpi = dpi_;

    bool success = true;

    for (int i = 0; i < FontStyleCount; i++) {
        if (faces_[i]) {
            if (FT_Set_Char_Size(faces_[i], 0, sizePt * 64, dpi, dpi) != 0) {
                success = false;
            }
        }
    }

    if (success) {
        sizePt_ = sizePt;
        dpi_ = dpi;
        computeMetrics();
    }

    return success;
}

FontMetrics FontManager::getMetrics() const {
    if (!metricsValid_) {
        return FontMetrics{};
    }
    return metrics_;
}

FT_Face FontManager::getFaceForStyle(FontStyle style) const {
    if (style >= 0 && style < FontStyleCount && faces_[style]) {
        return faces_[style];
    }

    // Fallback chain
    if (style == FontStyleBoldItalic) {
        if (faces_[FontStyleBold]) return faces_[FontStyleBold];
        if (faces_[FontStyleItalic]) return faces_[FontStyleItalic];
    }

    return faces_[FontStyleRegular];
}

bool FontManager::rasterizeGlyph(uint32_t codepoint, FontStyle style, GlyphBitmap& out) {
    if (!ftLibrary_) return false;

    FT_Face face = getFaceForStyle(style);
    if (!face) return false;

    uint32_t glyphIndex = FT_Get_Char_Index(face, codepoint);
    if (glyphIndex == 0 && codepoint != 0) {
        // Glyph not found - try regular face
        face = faces_[FontStyleRegular];
        if (face) {
            glyphIndex = FT_Get_Char_Index(face, codepoint);
        }
    }

    return rasterizeGlyphIndex(glyphIndex, style, out);
}

bool FontManager::rasterizeGlyphIndex(uint32_t glyphIndex, FontStyle style, GlyphBitmap& out) {
    if (!ftLibrary_) return false;

    FT_Face face = getFaceForStyle(style);
    if (!face) return false;

    // Free existing bitmap if present
    if (out.bitmap) {
        free(out.bitmap);
        out.bitmap = nullptr;
    }

    // Load the glyph
    FT_Int32 loadFlags = FT_LOAD_RENDER;

    // Apply synthetic bold/italic if needed
    bool needSyntheticBold = (style == FontStyleBold || style == FontStyleBoldItalic) &&
                             !faces_[style];
    bool needSyntheticItalic = (style == FontStyleItalic || style == FontStyleBoldItalic) &&
                               !faces_[style];

    if (FT_Load_Glyph(face, glyphIndex, loadFlags) != 0) {
        return false;
    }

    FT_GlyphSlot slot = face->glyph;

    // Apply synthetic transformations
    if (needSyntheticBold) {
        FT_GlyphSlot_Embolden(slot);
    }
    if (needSyntheticItalic) {
        FT_GlyphSlot_Oblique(slot);
    }

    // Render if not already rendered
    if (slot->format != FT_GLYPH_FORMAT_BITMAP) {
        if (FT_Render_Glyph(slot, FT_RENDER_MODE_NORMAL) != 0) {
            return false;
        }
    }

    FT_Bitmap* bitmap = &slot->bitmap;

    out.width = bitmap->width;
    out.height = bitmap->rows;
    out.bearingX = slot->bitmap_left;
    out.bearingY = slot->bitmap_top;
    out.advanceX = slot->advance.x >> 6;
    out.glyphIndex = glyphIndex;

    // Copy bitmap data
    if (bitmap->width > 0 && bitmap->rows > 0) {
        size_t size = bitmap->width * bitmap->rows;
        out.bitmap = static_cast<uint8_t*>(malloc(size));
        if (!out.bitmap) return false;

        // Handle different pitch values
        if (bitmap->pitch == static_cast<int>(bitmap->width)) {
            std::memcpy(out.bitmap, bitmap->buffer, size);
        } else {
            for (unsigned int row = 0; row < bitmap->rows; row++) {
                std::memcpy(out.bitmap + row * bitmap->width,
                           bitmap->buffer + row * bitmap->pitch,
                           bitmap->width);
            }
        }
    } else {
        out.bitmap = nullptr;
    }

    return true;
}

uint32_t FontManager::getGlyphIndex(uint32_t codepoint, FontStyle style) const {
    FT_Face face = getFaceForStyle(style);
    if (!face) return 0;
    return FT_Get_Char_Index(face, codepoint);
}

FT_Face FontManager::getFace(FontStyle style) const {
    return getFaceForStyle(style);
}

bool FontManager::hasStyle(FontStyle style) const {
    return style >= 0 && style < FontStyleCount && faceLoaded_[style];
}
