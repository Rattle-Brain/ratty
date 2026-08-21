/*
 * FontManager - FreeType glyph rasterization implementation
 */

#include "font_manager.h"
#include "../core/unicode.h"
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

/*
 * The bundled symbols font, compiled in from resources/fonts.qrc. Symbols only:
 * it carries no Latin, no box drawing and no spaces, so it can never take over a
 * character the primary or platform font should be serving.
 */
const char kBundledSymbolsFont[] = ":/fonts/fonts/SymbolsNerdFontMono-Regular.ttf";

/* fc-match / fc-list are subprocesses; never block the UI on a broken install. */
constexpr int kFontconfigTimeoutMs = 3000;

/*
 * How many fonts a charset query may offer before we stop reading. Each
 * candidate that is tried opens a face, and a common code point can be claimed
 * by two hundred fonts; the first one that verifiably has the glyph wins, so a
 * short list costs nothing and bounds the worst case.
 */
constexpr size_t kMaxDiscoveryCandidates = 12;

/* Fonts that exist only to draw "no glyph here"; never a useful fallback. */
bool isPlaceholderFamily(const std::string& family) {
    const QString name = QString::fromStdString(family);
    return name.startsWith(QLatin1String(".LastResort"), Qt::CaseInsensitive)
        || name.compare(QLatin1String("LastResort"), Qt::CaseInsensitive) == 0
        || name.contains(QLatin1String("Adobe Blank"), Qt::CaseInsensitive);
}

/*
 * Answers already had from fontconfig, for the lifetime of the process.
 *
 * Every lookup below is a `fc-match` *subprocess*, at roughly 30 ms a time, and
 * the same handful of questions get asked over and over: each pane builds its
 * own font chain, and each chain asks for the primary family in four styles,
 * the platform monospace, and six candidate emoji families. A four-way split
 * came to 157 spawns and several hundred milliseconds of pure process
 * overhead. The installed font set does not change while RaTTY is running, so
 * the answer to a repeated question is the answer already given.
 *
 * Guarded by nothing: font loading happens on the GUI thread only, which is
 * also the only thread allowed to touch the FT faces these results produce.
 */
std::unordered_map<std::string, FontFile>& fontconfigMemo() {
    static std::unordered_map<std::string, FontFile> memo;
    return memo;
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

    const std::string memoKey = pattern.toStdString();
    auto& memo = fontconfigMemo();
    if (const auto it = memo.find(memoKey); it != memo.end()) {
        return it->second;
    }

    /* A failed lookup is cached too: a family that is not installed stays not
     * installed, and re-asking is the expensive case (fc-match still has to
     * search before it can substitute). */
    const auto remember = [&memo, &memoKey](const FontFile& file) -> const FontFile& {
        return memo.emplace(memoKey, file).first->second;
    };

    QProcess process;
    process.start(QStringLiteral("fc-match"),
                  {QStringLiteral("-f"), QStringLiteral("%{file}\t%{index}\t%{family}"),
                   pattern});

    if (!process.waitForFinished(kFontconfigTimeoutMs)
        || process.exitStatus() != QProcess::NormalExit) {
        return remember(result);
    }

    const QString output = QString::fromUtf8(process.readAllStandardOutput()).trimmed();
    if (output.isEmpty()) return remember(result);

    const QStringList parts = output.split(QLatin1Char('\t'));
    if (parts.isEmpty() || parts[0].isEmpty()) return remember(result);

    result.path = parts[0].toStdString();
    if (parts.size() > 1) result.faceIndex = parts[1].toInt();
    if (parts.size() > 2) result.family = parts[2].toStdString();
    return remember(result);
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

bool FontManager::FaceSet::hasRenderableGlyph(char32_t codepoint) const {
    FT_Face face = faceFor(FontStyleRegular);
    if (!face) return false;

    const FT_UInt glyphIndex = FT_Get_Char_Index(face, codepoint);
    if (glyphIndex == 0) return false;

    if (isColor) {
        if (FT_Load_Glyph(face, glyphIndex, FT_LOAD_COLOR) != 0) return false;
        if (face->glyph->format != FT_GLYPH_FORMAT_BITMAP) return true;
        return face->glyph->bitmap.width > 0 && face->glyph->bitmap.rows > 0;
    }

    if (FT_Load_Glyph(face, glyphIndex, FT_LOAD_NO_SCALE | FT_LOAD_NO_BITMAP) != 0) {
        return false;
    }
    return face->glyph->outline.n_points > 0 || face->glyph->metrics.width > 0;
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
     * Cached: the fallback path below walks every installed family and asks the
     * font engine whether each is fixed-pitch, which is far too expensive to
     * repeat once per pane -- and the system's monospaced font does not change
     * under us.
     */
    static const std::string cached = [] {
        return computeDefaultMonospaceFamily();
    }();
    return cached;
}

std::string FontManager::computeDefaultMonospaceFamily() {
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
    /*
     * Cached per code point, and shared by every pane. `fc-list` scans the whole
     * font set, so this is the most expensive question asked here; without the
     * cache a second pane rendering the same unusual character pays for it all
     * over again.
     */
    static std::unordered_map<char32_t, std::vector<FontFile>> memo;
    if (const auto it = memo.find(codepoint); it != memo.end()) {
        return it->second;
    }

    std::vector<FontFile> found;

    /*
     * `fc-list` rather than `fc-match`, because only the former *filters*.
     * fc-match always answers something: given a charset nothing good covers it
     * returns its best guess, which on macOS is the placeholder .LastResort --
     * and rejecting that answer used to end the search, leaving a .notdef box
     * even when another installed font did have the code point. fc-list returns
     * every font whose charset actually contains it, so the placeholder is one
     * candidate among several instead of the last word.
     */
    QProcess process;
    process.start(QStringLiteral("fc-list"),
                  {QStringLiteral("-f"),
                   QStringLiteral("%{file}\t%{index}\t%{family[0]}\t%{spacing}\n"),
                   QStringLiteral(":charset=%1").arg(static_cast<uint>(codepoint), 0, 16)});
    if (!process.waitForFinished(kFontconfigTimeoutMs)) {
        process.kill();
        return memo.emplace(codepoint, std::move(found)).first->second;
    }

    /* Monospaced candidates first, so a fallback's advance matches the grid. */
    std::vector<FontFile> proportional;

    const QString output = QString::fromLocal8Bit(process.readAllStandardOutput());
    for (const QString& line : output.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        const QStringList fields = line.split(QLatin1Char('\t'));
        if (fields.size() < 3) continue;

        FontFile file;
        file.path = fields[0].trimmed().toStdString();
        file.faceIndex = fields[1].trimmed().toInt();
        file.family = fields[2].trimmed().toStdString();
        if (!file.isValid() || isPlaceholderFamily(file.family)) continue;

        /* An unset spacing means proportional; 100 is fontconfig's FC_MONO. */
        const bool monospaced = fields.size() > 3 && fields[3].trimmed().toInt() == 100;
        (monospaced ? found : proportional).push_back(std::move(file));

        if (found.size() + proportional.size() >= kMaxDiscoveryCandidates) break;
    }

    found.insert(found.end(), std::make_move_iterator(proportional.begin()),
                 std::make_move_iterator(proportional.end()));
    return memo.emplace(codepoint, std::move(found)).first->second;
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

/* ------------------------------------------------------- shared instances */

namespace {

struct SharedFontChain {
    std::vector<std::string> families;
    std::vector<std::string> fallbacks;
    std::shared_ptr<FontManager> manager;
};

std::vector<SharedFontChain>& sharedFontChains() {
    static std::vector<SharedFontChain> chains;
    return chains;
}

} // namespace

std::shared_ptr<FontManager> FontManager::shared(const std::vector<std::string>& families,
                                                 const std::vector<std::string>& fallbacks,
                                                 double pixelSize) {
    if (pixelSize <= 0.0) return nullptr;

    auto& chains = sharedFontChains();

    const auto sameRequest = [&](const SharedFontChain& chain) {
        return chain.families == families && chain.fallbacks == fallbacks
            && chain.manager && chain.manager->isValid();
    };

    /* The common case: another pane already wants exactly this. */
    for (const SharedFontChain& chain : chains) {
        if (sameRequest(chain)
            && std::abs(chain.manager->pixelSize() - pixelSize) < 0.01) {
            return chain.manager;
        }
    }

    /*
     * The same chain at a different size, held by nobody: re-scale it in place
     * rather than reloading the faces. This is the font-zoom path -- resizing
     * an open face is a metrics recomputation, where a reload is eight
     * FT_New_Face calls.
     */
    for (SharedFontChain& chain : chains) {
        if (sameRequest(chain) && chain.manager.use_count() == 1
            && chain.manager->setPixelSize(pixelSize)) {
            return chain.manager;
        }
    }

    auto manager = std::make_shared<FontManager>();
    manager->setFallbackFamilies(fallbacks);
    if (!manager->loadFamily(families, pixelSize)) return nullptr;

    /* Anything left unheld is a chain no pane came back for -- a size stepped
     * past on the way to this one, most likely. Dropping it here bounds the
     * cache without needing a policy. */
    std::erase_if(chains, [](const SharedFontChain& chain) {
        return !chain.manager || chain.manager.use_count() == 1;
    });
    chains.push_back({families, fallbacks, manager});
    return manager;
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

void FontManager::loadBundledSymbolsFallback() {
    QFile file(QString::fromLatin1(kBundledSymbolsFont));
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "FontManager: bundled symbols font missing from resources";
        return;
    }

    const QByteArray data = file.readAll();
    if (data.isEmpty()) return;

    auto faces = std::make_unique<FaceSet>();
    /* FreeType does not copy the buffer, so the FaceSet owns it. */
    faces->embedded.assign(data.constBegin(), data.constEnd());

    FT_Face face = nullptr;
    if (FT_New_Memory_Face(ftLibrary_, faces->embedded.data(),
                           static_cast<FT_Long>(faces->embedded.size()), 0, &face) != 0) {
        qWarning() << "FontManager: bundled symbols font could not be read";
        return;
    }

    FT_Select_Charmap(face, FT_ENCODING_UNICODE);
    faces->styles[FontStyleRegular] = face;
    faces->isColor = looksLikeColorFont(face);
    faces->family = face->family_name ? face->family_name : "Symbols Nerd Font Mono";

    /* Already the primary font -- someone has it installed and configured -- so
     * a second copy would add nothing. */
    if (familyMatches(faces->family, familyName_)) return;

    applyPixelSizeToFace(face, faces->isColor, faces->sizeScale);
    matchFallbackSize(*faces);

    qInfo() << "FontManager: fallback" << QString::fromStdString(faces->family)
            << "(bundled)";
    fallbacks_.push_back(std::move(faces));
}

void FontManager::loadConfiguredFallbacks() {
    if (configuredFallbacksLoaded_) return;
    configuredFallbacksLoaded_ = true;

    /*
     * Order matters. The platform's monospaced default comes after any
     * explicitly configured families but before emoji fonts, because it is what
     * supplies box-drawing, arrows and geometric shapes -- the characters a
     * patched icon font most often lacks.
     */
    /*
     * `allowSubstitution` is only for generic aliases like "monospace", which
     * have no exact family to match. It must stay off for real family names:
     * fontconfig answers *something* for a name it does not know, so allowing
     * substitution meant every emoji font that was not installed dragged in an
     * arbitrary unrelated font as a fallback.
     */
    struct Candidate {
        std::string family;
        bool allowSubstitution;
    };

    std::vector<Candidate> candidates;
    for (const std::string& family : fallbackPreferences_) {
        candidates.push_back({family, false});
    }
    candidates.push_back({defaultMonospaceFamily(), true});
    for (const char* emoji : kEmojiFamilies) {
        candidates.push_back({emoji, false});
    }


    for (const Candidate& candidate : candidates) {
        const std::string& family = candidate.family;
        if (family.empty()) continue;
        /* Already the primary font, so it would add nothing. */
        if (familyMatches(family, familyName_)) continue;

        FontFile file = resolveExactFamily(family, FontStyleRegular);
        if (!file.isValid() && candidate.allowSubstitution) {
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

    /*
     * Last of the loaded families, and still ahead of charset discovery. The
     * configured families and the platform monospace come first because they are
     * what supply arrows, geometric shapes and check marks; the bundled font
     * exists for the private-use icon code points that no stock font has at all,
     * and that a charset query can only answer with a placeholder or an
     * unrelated CJK face.
     */
    loadBundledSymbolsFallback();
}

const FontManager::FaceSet* FontManager::adoptFallback(const FontFile& file,
                                                       char32_t codepoint) {
    if (!file.isValid() || isPlaceholderFamily(file.family)) return nullptr;

    auto faces = std::make_unique<FaceSet>();
    if (!loadFaceInto(*faces, file, FontStyleRegular)) return nullptr;

    /* The whole point of discovery is coverage; verify it rather than trust it. */
    if (!faces->hasRenderableGlyph(codepoint)) return nullptr;

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

const FontManager::FaceSet* FontManager::resolveFaceSet(char32_t codepoint,
                                                        GlyphPresentation presentation) {
    const uint64_t key = resolutionKey(codepoint, presentation);
    if (const auto it = resolution_.find(key); it != resolution_.end()) {
        return it->second;
    }

    loadConfiguredFallbacks();

    /*
     * Presentation decides the search *order*, not a hard filter: if no font of
     * the preferred kind has the glyph, one of the other kind still beats a
     * .notdef box.
     */
    const bool wantColor = (presentation == GlyphPresentation::Emoji);
    const bool wantMono = (presentation == GlyphPresentation::Text);
    const bool presentationMatters = wantColor || wantMono;

    auto kindMatches = [&](const FaceSet& faces) {
        if (wantColor) return faces.isColor;
        if (wantMono) return !faces.isColor;
        return true;
    };

    auto search = [&](bool strict) -> const FaceSet* {
        /*
         * A colour request skips the primary font: the primary is the
         * monospaced text font, and its flat glyph is exactly what the selector
         * asked us not to use.
         */
        if (!(strict && wantColor)
            && (!strict || kindMatches(primary_))
            && primary_.hasRenderableGlyph(codepoint)) {
            return &primary_;
        }
        for (const auto& fallback : fallbacks_) {
            if (strict && !kindMatches(*fallback)) continue;
            if (fallback->hasRenderableGlyph(codepoint)) return fallback.get();
        }

        /*
         * Nothing loaded fits, so ask fontconfig. Doing this *inside* the strict
         * pass matters: no monospaced font on a stock macOS carries U+26A0, so a
         * text-presentation request would otherwise settle for the colour emoji
         * -- precisely what U+FE0E asks us not to do.
         */
        for (const FontFile& file : discoverFontsFor(codepoint)) {
            const FaceSet* adopted = adoptFallback(file, codepoint);
            if (!adopted) continue;
            if (strict && !kindMatches(*adopted)) continue;
            return adopted;
        }
        return nullptr;
    };

    const FaceSet* chosen = nullptr;
    if (presentationMatters) chosen = search(/*strict=*/true);
    if (!chosen) chosen = search(/*strict=*/false);

    /* Cache the miss too: discovery shells out, and a code point no font has
     * would otherwise pay that cost on every repaint. */
    resolution_.emplace(key, chosen);
    return chosen;
}

std::string FontManager::familyForCodepoint(char32_t codepoint, FontStyle,
                                            GlyphPresentation presentation) {
    const FaceSet* faces = resolveFaceSet(codepoint, presentation);
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

bool FontManager::rasterize(char32_t codepoint, FontStyle style, GlyphBitmap& out,
                            GlyphPresentation presentation) {
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

    const FaceSet* faces = resolveFaceSet(codepoint, presentation);

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

    /*
     * A space paints nothing, and a blank glyph is indistinguishable from a
     * missing one to any coverage test -- both have an empty outline -- so the
     * chain above reports that no font has U+00A0 and we would draw .notdef for
     * it. `tree` indents with NO-BREAK SPACEs, and every one of them came out as
     * an empty box.
     */
    if (isSpaceSeparator(codepoint)) {
        out = GlyphBitmap{};
        out.advanceX = metrics_.isValid() ? metrics_.cellWidth : 0;
        return true;
    }

    /* Nothing covers it: draw the primary font's .notdef box, which is the
     * conventional way to show "this character is missing". */
    return rasterizeFrom(primary_, style, 0, out);
}
