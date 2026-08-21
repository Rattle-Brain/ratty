/*
 * HistoryLine - compressed scrollback row implementation
 */

#include "history.h"
#include <algorithm>

namespace {

/*
 * A cell that carries no information at all, and so does not need storing: it
 * is exactly what Screen reports for a column past a captured row's width.
 *
 * `ch == U' '` rather than Cell::isBlank(), which also accepts a zero code
 * point. A zero is rare but it is not the same character, and dropping it would
 * make the encoding lossy for no useful gain.
 */
bool isDefaultBlank(const Cell& cell) {
    return cell.ch == U' '
        && cell.flags == CellFlagNone
        && cell.fg.isDefault()
        && cell.bg.isDefault();
}

/* Runs count cells in a uint16_t, so a very wide row is split across several. */
constexpr int MaxRunLength = 0xFFFF;

} // namespace

void HistoryLine::encode(const Cell* cells, int count) {
    width_ = 0;
    runCount_ = 0;
    bytesPerChar_ = 1;

    if (!cells || count <= 0) return;

    /* Drop the uniform default-coloured tail; usually most of the row. */
    int width = count;
    while (width > 0 && isDefaultBlank(cells[width - 1])) --width;
    if (width == 0) return;

    /* Narrowest character width that covers the row. */
    char32_t widest = 0;
    for (int i = 0; i < width; ++i) widest = std::max(widest, cells[i].ch);
    const uint8_t bytesPerChar = widest > 0xFFFF ? 4 : (widest > 0xFF ? 2 : 1);

    /* Count the attribute runs before allocating, so the block is exact. */
    uint32_t runCount = 1;
    int runLength = 1;
    for (int i = 1; i < width; ++i) {
        const Cell& previous = cells[i - 1];
        const Cell& cell = cells[i];
        const bool sameAttributes = cell.flags == previous.flags
                                 && cell.fg == previous.fg
                                 && cell.bg == previous.bg;
        if (sameAttributes && runLength < MaxRunLength) {
            ++runLength;
        } else {
            ++runCount;
            runLength = 1;
        }
    }

    const size_t needed = static_cast<size_t>(runCount) * sizeof(AttrRun)
                        + static_cast<size_t>(width) * bytesPerChar;

    /*
     * Reuse the existing block when it fits and is not wastefully large. The
     * reuse is what keeps a scrolled line off the allocator in the steady
     * state; the upper bound stops one very wide line from permanently
     * inflating every buffer it is later recycled into.
     */
    if (capacity_ < needed || capacity_ > needed * 2) {
        blob_ = std::make_unique<uint8_t[]>(needed);
        capacity_ = static_cast<uint32_t>(needed);
    }

    width_ = static_cast<uint32_t>(width);
    runCount_ = runCount;
    bytesPerChar_ = bytesPerChar;

    /* Emit the runs. */
    AttrRun* run = runs();
    uint32_t emitted = 0;
    int start = 0;
    for (int i = 1; i <= width; ++i) {
        const bool end = i == width;
        bool boundary = end;
        if (!end) {
            const Cell& previous = cells[i - 1];
            const Cell& cell = cells[i];
            boundary = !(cell.flags == previous.flags
                      && cell.fg == previous.fg
                      && cell.bg == previous.bg)
                    || (i - start) >= MaxRunLength;
        }
        if (!boundary) continue;

        run[emitted].count = static_cast<uint16_t>(i - start);
        run[emitted].flags = cells[start].flags;
        run[emitted].fg = cells[start].fg;
        run[emitted].bg = cells[start].bg;
        ++emitted;
        start = i;
    }

    /* Emit the characters. */
    uint8_t* out = text();
    for (int i = 0; i < width; ++i) {
        const char32_t ch = cells[i].ch;
        switch (bytesPerChar) {
        case 1:
            out[i] = static_cast<uint8_t>(ch);
            break;
        case 2:
            out[i * 2 + 0] = static_cast<uint8_t>(ch & 0xFF);
            out[i * 2 + 1] = static_cast<uint8_t>((ch >> 8) & 0xFF);
            break;
        default:
            out[i * 4 + 0] = static_cast<uint8_t>(ch & 0xFF);
            out[i * 4 + 1] = static_cast<uint8_t>((ch >> 8) & 0xFF);
            out[i * 4 + 2] = static_cast<uint8_t>((ch >> 16) & 0xFF);
            out[i * 4 + 3] = static_cast<uint8_t>((ch >> 24) & 0xFF);
            break;
        }
    }
}

void HistoryLine::decode(Cell* out) const {
    if (!out || width_ == 0) return;

    /* Attributes, run by run. */
    const AttrRun* run = runs();
    uint32_t col = 0;
    for (uint32_t r = 0; r < runCount_; ++r) {
        const uint32_t end = std::min<uint32_t>(col + run[r].count, width_);
        for (; col < end; ++col) {
            out[col].fg = run[r].fg;
            out[col].bg = run[r].bg;
            out[col].flags = run[r].flags;
        }
    }

    /* Characters. */
    const uint8_t* in = text();
    for (uint32_t i = 0; i < width_; ++i) {
        switch (bytesPerChar_) {
        case 1:
            out[i].ch = static_cast<char32_t>(in[i]);
            break;
        case 2:
            out[i].ch = static_cast<char32_t>(in[i * 2 + 0])
                      | (static_cast<char32_t>(in[i * 2 + 1]) << 8);
            break;
        default:
            out[i].ch = static_cast<char32_t>(in[i * 4 + 0])
                      | (static_cast<char32_t>(in[i * 4 + 1]) << 8)
                      | (static_cast<char32_t>(in[i * 4 + 2]) << 16)
                      | (static_cast<char32_t>(in[i * 4 + 3]) << 24);
            break;
        }
    }
}
