/*
 * HistoryLine - one row of scrollback, stored compressed
 *
 * Scrollback dominates a long-lived pane's memory. Held as plain cells a row
 * costs `cols * sizeof(Cell)` bytes -- 3200 for a 200-column row -- and the
 * default 10 000-line history therefore costs about 32 MiB per pane, plus one
 * heap block per row. Eight panes of a working day is a quarter of a gigabyte
 * of mostly blanks.
 *
 * Two things about terminal output make that enormously wasteful, and this
 * class exploits both:
 *
 *   - Most of a row is trailing blank. A shell line is rarely as wide as the
 *     window, and the tail is uniform default-coloured space. Those cells are
 *     dropped entirely: Screen already reports columns past a stored row's width
 *     as blank, and a *default* blank is indistinguishable from "not stored".
 *     Trailing cells carrying a non-default background -- the coloured bars a
 *     TUI draws -- are not default blanks and so survive. Nor is the seam of a
 *     wrapped row (CellFlagWrapped), which is what carries a soft line break
 *     through the scrollback intact.
 *
 *   - Colours and rendition change far more slowly than characters do. Almost
 *     every row is one single run of attributes, so they are run-length encoded
 *     while the characters are kept per-cell.
 *
 * Characters are then stored in the narrowest fixed width that covers the row:
 * one byte for the Latin-1 range, two for the BMP, four otherwise. An ASCII
 * line costs one byte per column rather than sixteen.
 *
 * Both halves live in a single heap block, so a row still costs exactly one
 * allocation -- the same as the vector it replaces, and the reason pushHistory()
 * can keep recycling buffers instead of calling the allocator per scrolled line.
 *
 * The encoding is lossless: decode(encode(row)) reproduces every cell.
 */

#ifndef CORE_HISTORY_H
#define CORE_HISTORY_H

#include "cell.h"
#include <cstddef>
#include <cstdint>
#include <memory>

/*
 * `count` consecutive cells sharing colours and rendition. Laid out so the
 * members pack without padding; asserted below.
 */
struct AttrRun {
    uint16_t count;
    uint16_t flags;
    Color fg;
    Color bg;
};
static_assert(sizeof(AttrRun) == 12, "AttrRun must not gain padding");

class HistoryLine {
public:
    HistoryLine() = default;

    /* Movable, not copyable: a row is owned by exactly one history deque. */
    HistoryLine(HistoryLine&&) noexcept = default;
    HistoryLine& operator=(HistoryLine&&) noexcept = default;
    HistoryLine(const HistoryLine&) = delete;
    HistoryLine& operator=(const HistoryLine&) = delete;

    /*
     * Compress `count` cells into this row, reusing the existing buffer when it
     * is a reasonable fit. Trailing default blanks are dropped, so width() may
     * be less than `count`.
     */
    void encode(const Cell* cells, int count);

    /*
     * Expand back into `out`, which must have room for width() cells. Writes
     * nothing when the row is empty.
     */
    void decode(Cell* out) const;

    /* Columns actually stored. Everything past this is a default blank. */
    int width() const { return static_cast<int>(width_); }

    /* Heap bytes this row holds, for diagnostics and tests. */
    size_t byteSize() const { return capacity_; }

private:
    /*
     * Runs first, then the characters. Runs are 2-byte aligned and the block
     * comes from new[], so the run array is always suitably aligned; the
     * character bytes that follow are read and written a byte at a time.
     */
    const AttrRun* runs() const { return reinterpret_cast<const AttrRun*>(blob_.get()); }
    AttrRun* runs() { return reinterpret_cast<AttrRun*>(blob_.get()); }
    const uint8_t* text() const { return blob_.get() + runBytes(); }
    uint8_t* text() { return blob_.get() + runBytes(); }
    size_t runBytes() const { return static_cast<size_t>(runCount_) * sizeof(AttrRun); }

    std::unique_ptr<uint8_t[]> blob_;
    uint32_t capacity_ = 0;      // bytes allocated in blob_
    uint32_t width_ = 0;         // columns stored
    uint32_t runCount_ = 0;      // attribute runs in blob_
    uint8_t bytesPerChar_ = 1;   // 1, 2 or 4
};

#endif /* CORE_HISTORY_H */
