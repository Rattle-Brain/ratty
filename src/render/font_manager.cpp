/*
 * FontManager - FreeType glyph rasterization implementation
 */

#include "font_manager.h"
#include "box_drawing.h"
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_SYNTHESIS_H
#include <QFile>
#include <QFontDatabase>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QDebug>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace {

constexpr const char* kStyleQuery[FontStyleCount] = {
    "Regular", "Bold", "Italic", "Bold Italic"
};

/* 26.6 fixed-point helpers, so the shifts are not scattered inline. */
constexpr int fixed26_6ToPixels(long value) {
    return static_cast<int>(value >> 6);
}
constexpr int fixed26_6ToPixelsCeil(long value) {
    return static_cast<int>((value + 63) >> 6);
}

/*
 * Fallback font paths, used when fontconfig is not installed. fc-match is the
 * primary lookup (it understands aliases like "monospace" and reports the face
 * index inside .ttc collections), but relying on it exclusively made font
 * loading fail outright on a stock macOS without Homebrew.
 */
const char* const kFallbackFontPaths[] = {
#if defined(__APPLE__)
    "/System/Library/Fonts/Menlo.ttc",
    "/System/Library/Fonts/Monaco.ttf",
    "/System/Library/Fonts/SFNSMono.ttf",
    "/System/Library/Fonts/Supplemental/Andale Mono.ttf",
    "/System/Library/Fonts/Courier.ttc",
#else
    "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
    "/usr/share/fonts/dejavu/DejaVuSansMono.ttf",
    "/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf",
    "/usr/share/fonts/liberation/LiberationMono-Regular.ttf",
    "/usr/share/fonts/TTF/DejaVuSansMono.ttf",
    "/usr/share/fonts/noto/NotoSansMono-Regular.ttf",
#endif
};

/*
 * Colour emoji fonts, by family name.
 *
 * These have to be named rather than discovered, because a charset query does
 * not find them: on macOS `fc-match ":charset=1F600"` answers ".LastResort",
 * a placeholder font whose glyphs are literally empty boxes. Every candidate is
 * still verified to contain the requested code point before it is used.
 */
const char* const kEmojiFamilies[] = {
    "Apple Color Emoji",
    "Noto Color Emoji",
    "Segoe UI Emoji",
    "Twemoji Mozilla",
    "JoyPixels",
    "EmojiOne Color",
};

/* Fonts that exist only to draw "no glyph here"; never a useful fallback. */
bool isPlaceholderFamily(const std::string& family) {
    const QString name = QString::fromStdString(family);
    return name.startsWith(QLatin1String(".LastResort"), Qt::CaseInsensitive)
        || name.compare(QLatin1String("LastResort"), Qt::CaseInsensitive) == 0
        || name.contains(QLatin1String("Adobe Blank"), Qt::CaseInsensitive);
}

/*
 * Ask fontconfig for the file, face index and *resolved* family.
 *
 * `extra` appends further pattern elements, e.g. ":spacing=100" to demand a
 * monospaced answer or ":charset=2500" to demand coverage of a code point.
 */
FontFile queryFontconfig(const std::string& family, FontStyle style,
                         const QString& extra = QString()) {
    FontFile result;

    QString pattern = QStringLiteral("%1:style=%2")
                          .arg(QString::fromStdString(family),
                               QString::fromLatin1(kStyleQuery[style]));
    pattern += extra;

    QProcess process;
    process.start(QStringLiteral("fc-match"),
                  {QStringLiteral("-f"), QStringLiteral("%{file}\t%{index}\t%{family}"),
                   pattern});

    if (!process.waitForFinished(3000) || process.exitStatus() != QProcess::NormalExit) {
        return result;
    }

    const QString output = QString::fromUtf8(process.readAllStandardOutput()).trimmed();
    if (output.isEmpty()) return result;

    const QStringList parts = output.split(QLatin1Char('\t'));
    if (parts.isEmpty() || parts[0].isEmpty()) return result;

    result.path = parts[0].toStdString();
    if (parts.size() > 1) result.faceIndex = parts[1].toInt();
    if (parts.size() > 2) result.family = parts[2].toStdString();
    return result;
}

/*
 * Compare family names the way a user would: ignoring case and whitespace.
 * Nerd Font builds are inconsistent about spacing ("DroidSansMono Nerd Font"
 * vs "Droid Sans Mono Nerd Font"), and fontconfig can report a comma-separated
 * list of localised names.
 */
bool familyMatches(const std::string& requested, const std::string& resolved) {
    auto canonical = [](const QString& text) {
        return text.simplified().remove(QLatin1Char(' ')).toLower();
    };

    const QString want = canonical(QString::fromStdString(requested));
    if (want.isEmpty()) return false;

    const QString got = QString::fromStdString(resolved);
    for (const QString& alias : got.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        if (canonical(alias) == want) return true;
    }
    return false;
}

/* True for a bitmap-only font, which in practice means a colour emoji font. */
bool looksLikeColorFont(FT_Face face) {
    return face && FT_IS_SCALABLE(face) == 0 && face->num_fixed_sizes > 0
        && (FT_HAS_COLOR(face) != 0);
}

} // namespace

/* ------------------------------------------------------------- FaceSet */

FontManager::FaceSet::~FaceSet() {
    for (FT_Face face : styles) {
        if (face) FT_Done_Face(face);
    }
}

FT_Face FontManager::FaceSet::faceFor(FontStyle style) const {
    if (style >= 0 && style < FontStyleCount && styles[style]) {
        return styles[style];
    }
    /* Prefer a real face of a neighbouring style over synthesizing both axes. */
    if (style == FontStyleBoldItalic) {
        if (styles[FontStyleBold]) return styles[FontStyleBold];
        if (styles[FontStyleItalic]) return styles[FontStyleItalic];
    }
    return styles[FontStyleRegular];
}

bool FontManager::FaceSet::hasCodepoint(char32_t codepoint) const {
    FT_Face face = faceFor(FontStyleRegular);
    return face && FT_Get_Char_Index(face, codepoint) != 0;
}

/* ---------------------------------------------------------- lifecycle */

FontManager::FontManager() {
    if (FT_Init_FreeType(&ftLibrary_) != 0) {
        qCritical() << "FontManager: failed to initialize FreeType";
        ftLibrary_ = nullptr;
    }
}

FontManager::~FontManager() {
    /* FaceSet destructors release their faces; they must go before the library. */
    fallbacks_.clear();
    for (FT_Face& face : primary_.styles) {
        if (face) { FT_Done_Face(face); face = nullptr; }
    }
    if (ftLibrary_) FT_Done_FreeType(ftLibrary_);
}

bool FontManager::isValid() const {
    return ftLibrary_ != nullptr && primary_.styles[FontStyleRegular] != nullptr;
}

void FontManager::setFallbackFamilies(const std::vector<std::string>& families) {
    fallbackPreferences_ = families;
    configuredFallbacksLoaded_ = false;
}

/* ----------------------------------------------------- font discovery */

std::string FontManager::defaultMonospaceFamily() {
    /*
     * Take the family straight off the QFont. Passing it through QFontInfo
     * resolves it against the font engine, which on macOS answers
     * ".AppleSystemUIFont" -- a proportional UI font. Feeding that to fontconfig
     * then substituted Verdana, and the terminal grid was laid out with a
     * variable-width font.
     */
    const QString declared = QFontDatabase::systemFont(QFontDatabase::FixedFont).family();
    if (!declared.isEmpty() && !declared.startsWith(QLatin1Char('.'))) {
        return declared.toStdString();
    }

    for (const QString& family : QFontDatabase::families()) {
        if (family.startsWith(QLatin1Char('.'))) continue;   // private system faces
        if (QFontDatabase::isFixedPitch(family)) {
            return family.toStdString();
        }
    }

    /* fontconfig's generic alias: precisely "whatever the system has set". */
    return "monospace";
}

FontFile FontManager::resolveFontFile(const std::string& family, FontStyle style) {
    if (!family.empty()) {
        if (FontFile viaFontconfig = queryFontconfig(family, style);
            viaFontconfig.isValid()) {
            return viaFontconfig;
        }
    }

    for (const char* path : kFallbackFontPaths) {
        if (QFile::exists(QString::fromUtf8(path))) {
            FontFile fallback;
            fallback.path = path;
            return fallback;
        }
    }
    return FontFile{};
}

FontFile FontManager::resolveExactFamily(const std::string& family, FontStyle style) {
    if (family.empty()) return FontFile{};

    const FontFile candidate = queryFontconfig(family, style);
    if (!candidate.isValid()) return FontFile{};

    /* fontconfig substitutes rather than failing, so a name it does not know
     * comes back as some unrelated font. Only an exact family counts. */
    if (!familyMatches(family, candidate.family)) return FontFile{};
    return candidate;
}

std::vector<FontFile> FontManager::discoverFontsFor(char32_t codepoint) {
    std::vector<FontFile> found;

    const QString charset = QStringLiteral(":charset=%1")
                                .arg(static_cast<uint>(codepoint), 0, 16);

    /* Prefer a monospaced answer so box-drawing and similar line up with the
     * primary font, then accept any font at all. */
    for (const QString& extra : {charset + QStringLiteral(":spacing=100"), charset}) {
        FontFile file = queryFontconfig("monospace", FontStyleRegular, extra);
        if (file.isValid() && !isPlaceholderFamily(file.family)) {
            found.push_back(file);
        }
    }
    return found;
}

/* ------------------------------------------------------------- loading */

bool FontManager::loadFaceInto(FaceSet& target, const FontFile& file, FontStyle style) {
    FT_Face face = nullptr;
    if (FT_New_Face(ftLibrary_, file.path.c_str(), file.faceIndex, &face) != 0) {
        qWarning() << "FontManager: cannot open" << QString::fromStdString(file.path)
                   << "face" << file.faceIndex;
        return false;
    }

    /* Symbol-only fonts have no Unicode charmap; the default one will do. */
    FT_Select_Charmap(face, FT_ENCODING_UNICODE);

    if (target.styles[style]) FT_Done_Face(target.styles[style]);
    target.styles[style] = face;

    if (style == FontStyleRegular) {
        target.isColor = looksLikeColorFont(face);
        target.family = face->family_name ? face->family_name : file.family;
    }

    applyPixelSizeToFace(face, target.isColor, target.sizeScale);
    return true;
}

void FontManager::applyPixelSizeToFace(FT_Face face, bool isColorFont,
                                       double sizeScale) const {
    if (!face || pixelSize_ <= 0.0) return;

    if (isColorFont) {
        /*
         * A bitmap-only colour font has fixed strikes. Asking for the cell
         * height lets FreeType pick the nearest strike and scale it, which is
         * what makes an emoji fill its two cells instead of arriving at whatever
         * size the font happened to store.
         */
        /*
         * Emoji are double-width, so they span two cells: fit them to whichever
         * of the two dimensions is tighter, or they bleed into the next row.
         */
        const int target = metrics_.isValid()
                               ? std::min(metrics_.cellHeight, 2 * metrics_.cellWidth)
                               : static_cast<int>(std::lround(pixelSize_));
        if (FT_Set_Char_Size(face, 0, target * 64, 72, 72) == 0) return;

        /* Some builds refuse arbitrary sizes; fall back to the nearest strike. */
        if (face->num_fixed_sizes > 0) {
            int best = 0;
            int bestDelta = std::numeric_limits<int>::max();
            for (int i = 0; i < face->num_fixed_sizes; ++i) {
                const int delta = std::abs(
                    static_cast<int>(face->available_sizes[i].height) - target);
                if (delta < bestDelta) { bestDelta = delta; best = i; }
            }
            FT_Select_Size(face, best);
        }
        return;
    }

    /*
     * FT_Set_Char_Size with a 72 dpi resolution makes "points" and pixels
     * identical, so `pixelSize_` lands in the em box exactly. The 26.6 fixed
     * point argument keeps fractional sizes instead of truncating them.
     */
    const FT_F26Dot6 size =
        static_cast<FT_F26Dot6>(std::lround(pixelSize_ * sizeScale * 64.0));
    FT_Set_Char_Size(face, 0, size, 72, 72);
}

void FontManager::applyPixelSize(const FaceSet& faces) const {
    for (FT_Face face : faces.styles) {
        applyPixelSizeToFace(face, faces.isColor, faces.sizeScale);
    }
}

void FontManager::matchFallbackSize(FaceSet& faces) const {
    faces.sizeScale = 1.0;
    if (faces.isColor || !metrics_.isValid()) return;

    FT_Face face = faces.styles[FontStyleRegular];
    if (!face || !FT_IS_SCALABLE(face)) return;

    applyPixelSizeToFace(face, false, 1.0);
    const int lineHeight = fixed26_6ToPixelsCeil(face->size->metrics.height);
    if (lineHeight <= 0 || lineHeight == metrics_.cellHeight) return;

    /*
     * Clamped: a font whose proportions are wildly different is better left
     * alone than stretched until its stems no longer match the primary's.
     */
    const double scale = static_cast<double>(metrics_.cellHeight)
                       / static_cast<double>(lineHeight);
    faces.sizeScale = std::clamp(scale, 0.85, 1.2);
    applyPixelSize(faces);
}

bool FontManager::regularFaceIsMonospaced() const {
    FT_Face face = primary_.styles[FontStyleRegular];
    if (!face) return false;

    const FT_UInt narrow = FT_Get_Char_Index(face, U'i');
    const FT_UInt wide = FT_Get_Char_Index(face, U'W');
    if (narrow == 0 || wide == 0) {
        /* Cannot tell from these glyphs; trust the font's own claim. */
        return FT_IS_FIXED_WIDTH(face) != 0;
    }

    if (FT_Load_Glyph(face, narrow, FT_LOAD_NO_SCALE | FT_LOAD_NO_BITMAP) != 0) return false;
    const FT_Pos narrowAdvance = face->glyph->metrics.horiAdvance;
    if (FT_Load_Glyph(face, wide, FT_LOAD_NO_SCALE | FT_LOAD_NO_BITMAP) != 0) return false;
    const FT_Pos wideAdvance = face->glyph->metrics.horiAdvance;

    return narrowAdvance == wideAdvance;
}

bool FontManager::tryPrimaryRegular(const FontFile& file) {
    if (!file.isValid()) return false;
    if (!loadFaceInto(primary_, file, FontStyleRegular)) return false;

    if (!regularFaceIsMonospaced()) {
        qWarning() << "FontManager: rejecting"
                   << QString::fromStdString(file.family.empty() ? file.path : file.family)
                   << "- it is not monospaced";
        FT_Done_Face(primary_.styles[FontStyleRegular]);
        primary_.styles[FontStyleRegular] = nullptr;
        return false;
    }
    return true;
}

bool FontManager::loadFamily(const std::string& family, double pixelSize) {
    return loadFamily(std::vector<std::string>{family}, pixelSize);
}

bool FontManager::loadFamily(const std::vector<std::string>& preferences, double pixelSize) {
    if (!ftLibrary_ || pixelSize <= 0.0) return false;

    /* Start from nothing: a font change invalidates the whole chain. */
    fallbacks_.clear();
    resolution_.clear();
    configuredFallbacksLoaded_ = false;
    for (FT_Face& face : primary_.styles) {
        if (face) { FT_Done_Face(face); face = nullptr; }
    }
    primary_.isColor = false;
    pixelSize_ = pixelSize;

    std::string requested;
    FontFile regular;

    /* 1. The configured preferences, exact family matches only. */
    for (const std::string& candidate : preferences) {
        if (candidate.empty()) continue;

        const FontFile file = resolveExactFamily(candidate, FontStyleRegular);
        if (file.isValid() && tryPrimaryRegular(file)) {
            requested = candidate;
            regular = file;
            break;
        }
        qInfo() << "FontManager:" << QString::fromStdString(candidate)
                << "unavailable, trying the next preference";
    }

    /* 2. Whatever the system has configured as its monospaced font. */
    if (!regular.isValid()) {
        for (const std::string& candidate : {defaultMonospaceFamily(),
                                             std::string("monospace")}) {
            const FontFile file = queryFontconfig(candidate, FontStyleRegular,
                                                  QStringLiteral(":spacing=100"));
            if (file.isValid() && tryPrimaryRegular(file)) {
                requested = file.family.empty() ? candidate : file.family;
                regular = file;
                break;
            }
        }
    }

    /* 3. Known font paths, for a system with no fontconfig at all. */
    if (!regular.isValid()) {
        const FontFile file = resolveFontFile(std::string(), FontStyleRegular);
        if (file.isValid() && tryPrimaryRegular(file)) {
            requested = file.family;
            regular = file;
        }
    }

    if (!regular.isValid()) {
        qCritical() << "FontManager: no usable monospaced font found";
        return false;
    }

    familyName_ = primary_.family.empty() ? requested : primary_.family;
    qInfo() << "FontManager: primary" << QString::fromStdString(familyName_)
            << "from" << QString::fromStdString(regular.path);

    /* Bold/italic are optional: a missing face falls back to synthesis. */
    for (int style = FontStyleBold; style < FontStyleCount; ++style) {
        const FontFile file = resolveExactFamily(familyName_, static_cast<FontStyle>(style));
        if (!file.isValid()) continue;
        /* fontconfig hands back the regular face when a style is unavailable;
         * loading it again would defeat synthesis, so skip exact duplicates. */
        if (file.path == regular.path && file.faceIndex == regular.faceIndex) continue;
        loadFaceInto(primary_, file, static_cast<FontStyle>(style));
    }

    computeMetrics();
    return metrics_.isValid();
}

bool FontManager::setPixelSize(double pixelSize) {
    if (!ftLibrary_ || pixelSize <= 0.0) return false;
    if (std::abs(pixelSize - pixelSize_) < 0.01) return true;

    pixelSize_ = pixelSize;
    applyPixelSize(primary_);
    computeMetrics();

    /* Fallback scales are relative to the primary cell, which just changed. */
    for (const auto& fallback : fallbacks_) {
        matchFallbackSize(*fallback);
    }

    return metrics_.isValid();
}

void FontManager::computeMetrics() {
    FT_Face face = primary_.styles[FontStyleRegular];
    if (!face) {
        metrics_ = FontMetrics{};
        return;
    }

    const FT_Size_Metrics& sizeMetrics = face->size->metrics;

    metrics_.ascender = fixed26_6ToPixelsCeil(sizeMetrics.ascender);
    metrics_.descender = fixed26_6ToPixelsCeil(-sizeMetrics.descender);

    /*
     * Cell width comes from a representative glyph rather than max_advance:
     * many monospaced fonts carry oversized advances for box-drawing or
     * fullwidth glyphs, which would leave a visible gap between columns.
     */
    int advance = 0;
    for (const char32_t probe : {U'M', U'0', U'x'}) {
        const FT_UInt index = FT_Get_Char_Index(face, probe);
        if (index != 0 && FT_Load_Glyph(face, index, FT_LOAD_NO_BITMAP) == 0) {
            advance = std::max(advance, fixed26_6ToPixelsCeil(face->glyph->advance.x));
        }
    }
    if (advance <= 0) {
        advance = fixed26_6ToPixelsCeil(sizeMetrics.max_advance);
    }
    metrics_.cellWidth = std::max(1, advance);

    /* Prefer the font's own line spacing, but never let it clip the extents. */
    const int lineHeight = fixed26_6ToPixelsCeil(sizeMetrics.height);
    metrics_.cellHeight = std::max({1, lineHeight, metrics_.ascender + metrics_.descender});

    /* Distribute any leading evenly so glyphs sit optically centred. */
    const int leading = metrics_.cellHeight - (metrics_.ascender + metrics_.descender);
    if (leading > 0) {
        metrics_.ascender += leading / 2;
    }

    if (face->units_per_EM > 0 && face->underline_thickness != 0) {
        const double scale = static_cast<double>(metrics_.cellHeight)
                           / static_cast<double>(face->units_per_EM);
        metrics_.underlinePosition = std::max(1, static_cast<int>(
            std::lround(-static_cast<double>(face->underline_position) * scale)));
        metrics_.underlineThickness = std::max(1, static_cast<int>(
            std::lround(static_cast<double>(face->underline_thickness) * scale)));
    } else {
        metrics_.underlinePosition = std::max(1, metrics_.descender / 2);
        metrics_.underlineThickness = std::max(1, metrics_.cellHeight / 16);
    }
    metrics_.underlinePosition = std::min(metrics_.underlinePosition,
                                          std::max(1, metrics_.descender - 1));
    metrics_.strikethroughPosition = std::max(1, metrics_.ascender / 3);
}

/* ------------------------------------------------------ fallback chain */

void FontManager::loadConfiguredFallbacks() {
    if (configuredFallbacksLoaded_) return;
    configuredFallbacksLoaded_ = true;

    /*
     * Order matters. The platform's monospaced default comes after any
     * explicitly configured families but before emoji fonts, because it is what
     * supplies box-drawing, arrows and geometric shapes -- the characters a
     * patched icon font most often lacks.
     */
    std::vector<std::string> families = fallbackPreferences_;
    families.push_back(defaultMonospaceFamily());
    for (const char* emoji : kEmojiFamilies) {
        families.push_back(emoji);
    }

    for (const std::string& family : families) {
        if (family.empty()) continue;
        /* Already the primary font, so it would add nothing. */
        if (familyMatches(family, familyName_)) continue;

        FontFile file = resolveExactFamily(family, FontStyleRegular);
        if (!file.isValid()) {
            /* "monospace" and friends are aliases, not families, so an exact
             * match is impossible; take fontconfig's answer instead. */
            file = queryFontconfig(family, FontStyleRegular,
                                   QStringLiteral(":spacing=100"));
        }
        if (!file.isValid() || isPlaceholderFamily(file.family)) continue;

        auto faces = std::make_unique<FaceSet>();
        if (!loadFaceInto(*faces, file, FontStyleRegular)) continue;

        /* Skip a duplicate of the primary or of an existing fallback. */
        const bool duplicate =
            familyMatches(faces->family, familyName_)
            || std::any_of(fallbacks_.begin(), fallbacks_.end(),
                           [&](const std::unique_ptr<FaceSet>& existing) {
                               return existing->family == faces->family;
                           });
        if (duplicate) continue;

        matchFallbackSize(*faces);
        qInfo() << "FontManager: fallback" << QString::fromStdString(faces->family)
                << (faces->isColor ? "(colour)" : "")
                << (faces->sizeScale != 1.0
                        ? QStringLiteral("scaled x%1").arg(faces->sizeScale, 0, 'f', 3)
                        : QString());

        /* Styled faces for a fallback are a nicety, not a requirement. */
        for (int style = FontStyleBold; style < FontStyleCount; ++style) {
            const FontFile styled = resolveExactFamily(faces->family,
                                                       static_cast<FontStyle>(style));
            if (!styled.isValid()) continue;
            if (styled.path == file.path && styled.faceIndex == file.faceIndex) continue;
            loadFaceInto(*faces, styled, static_cast<FontStyle>(style));
        }

        fallbacks_.push_back(std::move(faces));
    }
}

const FontManager::FaceSet* FontManager::adoptFallback(const FontFile& file,
                                                       char32_t codepoint) {
    if (!file.isValid() || isPlaceholderFamily(file.family)) return nullptr;

    auto faces = std::make_unique<FaceSet>();
    if (!loadFaceInto(*faces, file, FontStyleRegular)) return nullptr;

    /* The whole point of discovery is coverage; verify it rather than trust it. */
    if (!faces->hasCodepoint(codepoint)) return nullptr;

    for (const auto& existing : fallbacks_) {
        if (existing->family == faces->family) return existing.get();
    }

    matchFallbackSize(*faces);
    qInfo() << "FontManager: discovered" << QString::fromStdString(faces->family)
            << "for U+" << QString::number(static_cast<uint>(codepoint), 16).toUpper();

    const FaceSet* result = faces.get();
    fallbacks_.push_back(std::move(faces));
    return result;
}

const FontManager::FaceSet* FontManager::resolveFaceSet(char32_t codepoint) {
    if (const auto it = resolution_.find(codepoint); it != resolution_.end()) {
        return it->second;
    }

    const FaceSet* chosen = nullptr;

    if (primary_.hasCodepoint(codepoint)) {
        chosen = &primary_;
    } else {
        loadConfiguredFallbacks();

        for (const auto& fallback : fallbacks_) {
            if (fallback->hasCodepoint(codepoint)) {
                chosen = fallback.get();
                break;
            }
        }

        /* Last resort: ask fontconfig which font covers this code point. */
        if (!chosen) {
            for (const FontFile& file : discoverFontsFor(codepoint)) {
                if (const FaceSet* adopted = adoptFallback(file, codepoint)) {
                    chosen = adopted;
                    break;
                }
            }
        }
    }

    /* Cache the miss too: discovery shells out, and a code point no font has
     * would otherwise pay that cost on every repaint. */
    resolution_.emplace(codepoint, chosen);
    return chosen;
}

std::string FontManager::familyForCodepoint(char32_t codepoint, FontStyle) {
    const FaceSet* faces = resolveFaceSet(codepoint);
    return faces ? faces->family : std::string();
}

/* ------------------------------------------------------- rasterization */

bool FontManager::rasterizeFrom(const FaceSet& faces, FontStyle style,
                                FT_UInt glyphIndex, GlyphBitmap& out) const {
    FT_Face face = faces.faceFor(style);
    if (!face) return false;

    /*
     * Colour fonts store bitmaps, so FT_LOAD_COLOR (and *not* FT_LOAD_NO_BITMAP)
     * is required. Outline fonts get light hinting: it snaps stems vertically
     * without touching horizontal metrics, which is what a monospaced grid
     * needs.
     */
    const FT_Int32 loadFlags = faces.isColor
                                   ? FT_LOAD_COLOR
                                   : (FT_LOAD_TARGET_LIGHT | FT_LOAD_NO_BITMAP);

    if (FT_Load_Glyph(face, glyphIndex, loadFlags) != 0) return false;

    FT_GlyphSlot slot = face->glyph;

    /* Synthesize the styles this family does not ship (outline fonts only). */
    if (!faces.isColor) {
        const bool wantBold = (style == FontStyleBold || style == FontStyleBoldItalic);
        const bool wantItalic = (style == FontStyleItalic || style == FontStyleBoldItalic);
        const bool haveRealFace = faces.styles[style] != nullptr;

        if (!haveRealFace && wantBold) FT_GlyphSlot_Embolden(slot);
        if (!haveRealFace && wantItalic) FT_GlyphSlot_Oblique(slot);
    }

    if (slot->format != FT_GLYPH_FORMAT_BITMAP) {
        const FT_Render_Mode mode = faces.isColor ? FT_RENDER_MODE_NORMAL
                                                  : FT_RENDER_MODE_LIGHT;
        if (FT_Render_Glyph(slot, mode) != 0) return false;
    }

    const FT_Bitmap& bitmap = slot->bitmap;
    out.width = static_cast<int>(bitmap.width);
    out.height = static_cast<int>(bitmap.rows);
    out.bearingX = slot->bitmap_left;
    out.bearingY = slot->bitmap_top;
    out.advanceX = fixed26_6ToPixels(slot->advance.x);
    out.isColor = (bitmap.pixel_mode == FT_PIXEL_MODE_BGRA);

    if (out.isEmpty()) {
        out.pixels.clear();
        return true;   // space and friends: valid, nothing to draw
    }

    const size_t pixelCount = static_cast<size_t>(out.width) * static_cast<size_t>(out.height);

    if (out.isColor) {
        out.pixels.resize(pixelCount * 4);
        for (int row = 0; row < out.height; ++row) {
            const unsigned char* src =
                bitmap.buffer + static_cast<ptrdiff_t>(row) * bitmap.pitch;
            uint8_t* dst = out.pixels.data() + static_cast<size_t>(row) * out.width * 4;
            for (int x = 0; x < out.width; ++x) {
                /*
                 * FreeType gives premultiplied BGRA. Undo the premultiplication
                 * and swap to RGBA so colour and coverage glyphs can share one
                 * straight-alpha blend mode.
                 */
                const uint8_t b = src[x * 4 + 0];
                const uint8_t g = src[x * 4 + 1];
                const uint8_t r = src[x * 4 + 2];
                const uint8_t a = src[x * 4 + 3];
                if (a == 0) {
                    dst[x * 4 + 0] = dst[x * 4 + 1] = dst[x * 4 + 2] = dst[x * 4 + 3] = 0;
                } else {
                    dst[x * 4 + 0] = static_cast<uint8_t>(std::min(255, r * 255 / a));
                    dst[x * 4 + 1] = static_cast<uint8_t>(std::min(255, g * 255 / a));
                    dst[x * 4 + 2] = static_cast<uint8_t>(std::min(255, b * 255 / a));
                    dst[x * 4 + 3] = a;
                }
            }
        }
        return true;
    }

    if (bitmap.pixel_mode != FT_PIXEL_MODE_GRAY) {
        /* Monochrome (1-bit) strikes are rare but must not be copied as if they
         * were 8-bit coverage. */
        out.pixels.assign(pixelCount, 0);
        for (int row = 0; row < out.height; ++row) {
            const unsigned char* src =
                bitmap.buffer + static_cast<ptrdiff_t>(row) * bitmap.pitch;
            for (int x = 0; x < out.width; ++x) {
                const bool set = (src[x >> 3] >> (7 - (x & 7))) & 1;
                out.pixels[static_cast<size_t>(row) * out.width + x] = set ? 255 : 0;
            }
        }
        return true;
    }

    out.pixels.resize(pixelCount);
    /* FT_Bitmap rows are `pitch` bytes apart, which is not always the width and
     * can be negative for bottom-up bitmaps. */
    for (int row = 0; row < out.height; ++row) {
        const unsigned char* src = bitmap.buffer + static_cast<ptrdiff_t>(row) * bitmap.pitch;
        std::memcpy(out.pixels.data() + static_cast<size_t>(row) * static_cast<size_t>(out.width),
                    src, static_cast<size_t>(out.width));
    }
    return true;
}

bool FontManager::rasterize(char32_t codepoint, FontStyle style, GlyphBitmap& out) {
    out = GlyphBitmap{};
    if (!ftLibrary_ || !primary_.styles[FontStyleRegular]) return false;

    /*
     * Line and block characters are drawn from the cell geometry rather than
     * taken from a font, so they tile exactly. See box_drawing.h for why a font
     * cannot guarantee that once a fallback is involved.
     */
    if (metrics_.isValid() && isBoxDrawingCodepoint(codepoint)) {
        if (renderBoxDrawing(codepoint, metrics_.cellWidth, metrics_.cellHeight,
                             metrics_.underlineThickness, out.pixels)) {
            out.width = metrics_.cellWidth;
            out.height = metrics_.cellHeight;
            out.bearingX = 0;
            out.bearingY = metrics_.ascender;   // top of the cell
            out.advanceX = metrics_.cellWidth;
            out.isColor = false;
            return true;
        }
    }

    const FaceSet* faces = resolveFaceSet(codepoint);

    if (faces) {
        FT_Face face = faces->faceFor(style);
        FT_UInt glyphIndex = face ? FT_Get_Char_Index(face, codepoint) : 0;

        /* A styled face of the same family may lack what the regular one has. */
        if (glyphIndex == 0 && face != faces->styles[FontStyleRegular]) {
            face = faces->styles[FontStyleRegular];
            glyphIndex = face ? FT_Get_Char_Index(face, codepoint) : 0;
            if (glyphIndex != 0) style = FontStyleRegular;
        }

        if (glyphIndex != 0 && rasterizeFrom(*faces, style, glyphIndex, out)) {
            return true;
        }
    }

    /* Nothing covers it: draw the primary font's .notdef box, which is the
     * conventional way to show "this character is missing". */
    return rasterizeFrom(primary_, style, 0, out);
}
