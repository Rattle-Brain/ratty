/*
 * Terminal core tests: screen geometry, VT parsing and SGR interpretation.
 *
 * These need no GL context and no widgets, which is the point of keeping
 * core/ free of both.
 */

#include "check.h"
#include "core/cursor.h"
#include "core/palette.h"
#include "core/terminal_emulator.h"
#include "core/unicode.h"
#include "core/utf8.h"
#include <string>

namespace {

Palette palette;

void feed(TerminalEmulator& term, const std::string& bytes) {
    term.write(bytes.data(), bytes.size());
}

/* Text of one row, trailing blanks stripped. */
std::string rowText(const Screen& screen, int row) {
    std::string out;
    for (int col = 0; col < screen.cols(); ++col) {
        const char32_t ch = screen.at(row, col).ch;
        out += (ch >= 32 && ch < 127) ? static_cast<char>(ch) : '?';
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

/* Text of one row *as displayed*, which is a history row when the view is
 * scrolled back. */
std::string viewRowText(const Screen& screen, int row) {
    std::string out;
    for (int col = 0; col < screen.cols(); ++col) {
        const char32_t ch = screen.viewAt(row, col).ch;
        out += (ch >= 32 && ch < 127) ? static_cast<char>(ch) : '?';
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

/* Cells whose resolved background differs from the palette default, i.e. cells
 * that paint an opaque rectangle. */
int paintedCells(const Screen& screen) {
    int count = 0;
    for (int row = 0; row < screen.rows(); ++row) {
        for (int col = 0; col < screen.cols(); ++col) {
            QColor fg;
            QColor bg;
            palette.resolveCell(screen.at(row, col), fg, bg);
            if (bg != palette.defaultBackground()) ++count;
        }
    }
    return count;
}

/*
 * What zsh actually sends after a command, captured from a pty. The inverse '%'
 * is the PROMPT_SP end-of-line marker: zsh prints it, pads to the right margin,
 * then does "\r \r" to erase it -- which only works if the terminal defers the
 * line wrap. Getting this wrong left a white block above every prompt.
 */
const char* kZshPromptCycle =
    "\x1b[1m\x1b[7m%\x1b[27m\x1b[1m\x1b[0m"
    "                                                                               "
    "\r \r"
    "\x1b]2;title\x07\x1b]1;icon\x07\x1b]7;file://host/tmp\x1b\\"
    "\r\x1b[0m\x1b[27m\x1b[24m\x1b[J"
    "\x1b[01;32muser@host\x1b[00m \x1b[01;34mrepo\x1b[00m \x1b[33m(main) \x1b[00m> "
    "\x1b[K\x1b[?1h\x1b=\x1b[?2004h";

void testDeferredWrap() {
    check::section("deferred wrap (the pending-wrap flag)");

    TerminalEmulator term(4, 5);
    feed(term, "abcde");
    check::that(term.screen().cursorRow() == 0 && term.screen().cursorCol() == 4,
                "filling the last column keeps the cursor on the same row");

    feed(term, "f");
    check::that(term.screen().cursorRow() == 1 && term.screen().cursorCol() == 1,
                "the next printable character performs the wrap");
    check::equal(rowText(term.screen(), 0), std::string("abcde"), "row 0 holds abcde");
    check::equal(rowText(term.screen(), 1), std::string("f"), "row 1 holds f");

    /* CR must cancel a pending wrap rather than trigger it. */
    TerminalEmulator cr(4, 5);
    feed(cr, "abcde\rX");
    check::equal(rowText(cr.screen(), 0), std::string("Xbcde"),
                 "CR after the last column returns to column 0 of the same row");
    check::equal(cr.screen().cursorRow(), 0, "CR did not advance a row");

    /* With autowrap off, characters pile up in the last column. */
    TerminalEmulator nowrap(4, 5);
    feed(nowrap, "\x1b[?7l" "abcdefg");
    check::equal(nowrap.screen().cursorRow(), 0, "DECAWM off never wraps");
}

void testZshPromptArtifact() {
    check::section("zsh prompt cycle leaves no artifact");

    TerminalEmulator term(24, 80);
    feed(term, "echo hola\r\n");
    feed(term, kZshPromptCycle);

    check::equal(paintedCells(term.screen()), 0,
                 "no cell paints an opaque background after one prompt");
    check::equal(rowText(term.screen(), 0), std::string("echo hola"), "output row intact");
    check::that(rowText(term.screen(), 1).find("user@host") == 0,
                "the prompt sits on the very next row, with no blank row between");

    for (int i = 0; i < 5; ++i) {
        feed(term, "\x1b[?1l\x1b>\x1b[?2004l\r\r\n");
        feed(term, kZshPromptCycle);
    }
    check::equal(paintedCells(term.screen()), 0,
                 "still no opaque cells after five more prompts");
    check::that(rowText(term.screen(), 6).find("user@host") == 0,
                "prompts occupy consecutive rows (one row per Enter)");
}

void testOscTermination() {
    check::section("OSC string termination");

    /* ESC \ (ST) must be consumed as a unit. Treating the ESC as the end of the
     * OSC left the '\' to be printed into the grid. */
    TerminalEmulator st(2, 20);
    feed(st, "\x1b]7;file://host/tmp\x1b\\" "A");
    check::equal(rowText(st.screen(), 0), std::string("A"),
                 "ST-terminated OSC leaves no stray backslash");

    TerminalEmulator bel(2, 20);
    feed(bel, "\x1b]2;title\x07" "B");
    check::equal(rowText(bel.screen(), 0), std::string("B"), "BEL-terminated OSC consumed");

    std::string title;
    TerminalEmulator titled(2, 20);
    titled.setTitleSink([&title](const std::string& value) { title = value; });
    feed(titled, "\x1b]0;my title\x07");
    check::equal(title, std::string("my title"), "OSC 0 reports the window title");
}

void testCsiParsing() {
    check::section("CSI parsing");

    /* Private and intermediate bytes must be recognised, not printed. The old
     * parser treated '>' as a final byte and dumped the rest as text. */
    TerminalEmulator priv(2, 20);
    feed(priv, "\x1b[>4;2m" "A");
    check::equal(rowText(priv.screen(), 0), std::string("A"),
                 "CSI > 4;2 m is swallowed, not printed");

    TerminalEmulator interm(2, 20);
    feed(interm, "\x1b[!p" "B");
    check::equal(rowText(interm.screen(), 0), std::string("B"),
                 "CSI ! p (soft reset) is swallowed");

    TerminalEmulator space(2, 20);
    feed(space, "\x1b[2 q" "C");
    check::equal(rowText(space.screen(), 0), std::string("C"),
                 "CSI 2 SP q (cursor style) is swallowed");

    /* Editing sequences. */
    TerminalEmulator edit(2, 10);
    feed(edit, "abcdef\x1b[1;3H\x1b[2P");
    check::equal(rowText(edit.screen(), 0), std::string("abef"), "DCH deletes 2 characters");

    TerminalEmulator ins(2, 10);
    feed(ins, "abcdef\x1b[1;3H\x1b[2@");
    check::equal(rowText(ins.screen(), 0), std::string("ab  cdef"), "ICH inserts 2 blanks");

    TerminalEmulator ech(2, 10);
    feed(ech, "abcdef\x1b[1;3H\x1b[2X");
    check::equal(rowText(ech.screen(), 0), std::string("ab  ef"), "ECH erases 2 in place");
}

void testScrollRegion() {
    check::section("scrolling region (DECSTBM)");

    TerminalEmulator term(5, 10);
    feed(term, "one\r\ntwo\r\nthree\r\nfour\r\nfive");
    /* Confine scrolling to rows 2-4 (1-based), then scroll it. */
    feed(term, "\x1b[2;4r\x1b[4;1H\r\n" "six");

    check::equal(rowText(term.screen(), 0), std::string("one"),
                 "row outside the region is untouched");
    check::equal(rowText(term.screen(), 1), std::string("three"), "region scrolled up");
    check::equal(rowText(term.screen(), 2), std::string("four"), "region scrolled up");
    check::equal(rowText(term.screen(), 3), std::string("six"), "new text at the region bottom");
    check::equal(rowText(term.screen(), 4), std::string("five"),
                 "row below the region is untouched");
}

void testAlternateScreen() {
    check::section("alternate screen buffer");

    TerminalEmulator term(3, 10);
    feed(term, "shell");
    feed(term, "\x1b[?1049h");
    check::equal(rowText(term.screen(), 0), std::string(""), "alternate buffer starts empty");
    feed(term, "editor");
    check::equal(rowText(term.screen(), 0), std::string("editor"), "alternate buffer holds text");
    feed(term, "\x1b[?1049l");
    check::equal(rowText(term.screen(), 0), std::string("shell"),
                 "leaving the alternate buffer restores the shell's screen");
}

void testSgrColors() {
    check::section("SGR colours");

    TerminalEmulator term(2, 20);
    feed(term, "\x1b[38;5;208mA"
               "\x1b[38;2;10;20;30mB"
               "\x1b[48;5;27mC"
               "\x1b[0mD"
               "\x1b[31mE"
               "\x1b[91mF");

    auto fgOf = [&](int col) {
        QColor fg;
        QColor bg;
        palette.resolveCell(term.screen().at(0, col), fg, bg);
        return fg;
    };
    auto bgOf = [&](int col) {
        QColor fg;
        QColor bg;
        palette.resolveCell(term.screen().at(0, col), fg, bg);
        return bg;
    };

    check::that(fgOf(0) == palette.entry(208), "SGR 38;5;208 selects palette slot 208");
    check::that(fgOf(1) == QColor(10, 20, 30), "SGR 38;2;R;G;B selects a direct colour");
    check::that(bgOf(2) == palette.entry(27), "SGR 48;5;27 selects a background slot");
    check::that(fgOf(3) == palette.defaultForeground(), "SGR 0 restores the default");
    check::that(fgOf(4) == palette.entry(1), "SGR 31 selects red");
    check::that(fgOf(5) == palette.entry(9), "SGR 91 selects bright red");

    /* The colon form, which some emitters use. */
    TerminalEmulator colon(2, 10);
    feed(colon, "\x1b[38:2:1:2:3mX");
    QColor fg;
    QColor bg;
    palette.resolveCell(colon.screen().at(0, 0), fg, bg);
    check::that(fg == QColor(1, 2, 3), "SGR 38:2:R:G:B (colon form) is understood");

    /* Inverse swaps, and an erase keeps the pen's background. */
    TerminalEmulator inv(2, 10);
    feed(inv, "\x1b[7mZ");
    palette.resolveCell(inv.screen().at(0, 0), fg, bg);
    check::that(bg == palette.defaultForeground(), "inverse video swaps fg and bg");
}

void testEraseKeepsBackground() {
    check::section("erase semantics");

    /* Erasing must retain the current background so full-width coloured bars
     * work, but must not retain, say, underline. */
    TerminalEmulator term(2, 10);
    feed(term, "\x1b[44m\x1b[4m\x1b[2K");
    const Cell& cell = term.screen().at(0, 3);
    check::that(cell.bg == Color::indexed(4), "erased cells keep the pen's background");
    check::that(!cell.hasFlag(CellFlagUnderline), "erased cells drop the underline");
}

void testOscColors() {
    check::section("OSC colour control (OSC 4 / 10 / 11 / 12)");

    std::string reply;
    TerminalEmulator term(4, 20);
    term.setReplySink([&reply](const std::string& value) { reply += value; });

    Palette base;
    base.setDefaultBackground(QColor(0x1e, 0x1e, 0x1e));
    base.setDefaultForeground(QColor(0xdc, 0xdc, 0xdc));
    term.setBasePalette(base);

    /* Setting a palette entry, in the spellings applications actually use. */
    feed(term, "\x1b]4;1;#ff0000\x1b\\");
    check::that(term.palette().entry(1) == QColor(255, 0, 0), "OSC 4 with #rrggbb");

    feed(term, "\x1b]4;2;rgb:00/80/00\x07");
    check::that(term.palette().entry(2) == QColor(0, 128, 0), "OSC 4 with rgb:r/g/b");

    feed(term, "\x1b]4;3;#abc\x1b\\");
    check::that(term.palette().entry(3) == QColor(0xaa, 0xbb, 0xcc), "OSC 4 with #rgb");

    feed(term, "\x1b]4;5;#111111;6;#222222\x1b\\");
    check::that(term.palette().entry(5) == QColor(0x11, 0x11, 0x11)
                && term.palette().entry(6) == QColor(0x22, 0x22, 0x22),
                "OSC 4 sets several entries in one sequence");

    reply.clear();
    feed(term, "\x1b]4;1;?\x1b\\");
    check::equal(reply, std::string("\x1b]4;1;rgb:ffff/0000/0000\x1b\\"),
                 "OSC 4 query answers in the rgb: form");

    /* Neovim asks for the background at start-up to choose a light or dark
     * colour scheme; with no answer it has to guess. */
    reply.clear();
    feed(term, "\x1b]11;?\x1b\\");
    check::equal(reply, std::string("\x1b]11;rgb:1e1e/1e1e/1e1e\x1b\\"),
                 "OSC 11 query reports the background");

    reply.clear();
    feed(term, "\x1b]10;?\x1b\\");
    check::equal(reply, std::string("\x1b]10;rgb:dcdc/dcdc/dcdc\x1b\\"),
                 "OSC 10 query reports the foreground");

    feed(term, "\x1b]11;#1f1f26\x1b\\");
    check::that(term.palette().defaultBackground() == QColor(0x1f, 0x1f, 0x26),
                "OSC 11 sets the background");
    feed(term, "\x1b]12;#00ff00\x1b\\");
    check::that(term.palette().cursorColor() == QColor(0, 255, 0),
                "OSC 12 sets the cursor colour");

    /* Resets restore the *configured* values, not the built-in ones. */
    feed(term, "\x1b]111\x1b\\");
    check::that(term.palette().defaultBackground() == QColor(0x1e, 0x1e, 0x1e),
                "OSC 111 restores the configured background");
    feed(term, "\x1b]104\x1b\\");
    check::that(term.palette().entry(1) == base.entry(1),
                "OSC 104 restores the whole palette");

    const QColor before = term.palette().entry(1);
    feed(term, "\x1b]4;1;notacolour\x1b\\");
    check::that(term.palette().entry(1) == before, "a malformed colour spec is ignored");

    /* Cells store a palette index, not a resolved colour, so retheming applies
     * to text that is already on screen. */
    TerminalEmulator live(2, 10);
    live.setBasePalette(base);
    feed(live, "\x1b[31mX");
    feed(live, "\x1b]4;1;#123456\x1b\\");
    QColor fg;
    QColor bg;
    live.palette().resolveCell(live.screen().at(0, 0), fg, bg);
    check::that(fg == QColor(0x12, 0x34, 0x56),
                "already-drawn cells pick up a retheming OSC 4");
}

void testCursorStyle() {
    check::section("DECSCUSR cursor shape");

    TerminalEmulator term(4, 20);
    check::that(!term.hasRequestedCursorStyle(),
                "no request initially, so the configured style wins");

    feed(term, "\x1b[5 q");
    check::that(term.hasRequestedCursorStyle()
                && term.requestedCursorStyle() == CursorStyle::Bar
                && term.cursorBlinkRequested(),
                "CSI 5 SP q selects a blinking bar");

    feed(term, "\x1b[2 q");
    check::that(term.requestedCursorStyle() == CursorStyle::Block
                && !term.cursorBlinkRequested(),
                "CSI 2 SP q selects a steady block");

    feed(term, "\x1b[4 q");
    check::that(term.requestedCursorStyle() == CursorStyle::Underline,
                "CSI 4 SP q selects an underline");

    feed(term, "\x1b[0 q");
    check::that(!term.hasRequestedCursorStyle(),
                "CSI 0 SP q hands control back to the configuration");

    /* The space intermediate is what makes it DECSCUSR. */
    feed(term, "\x1b[5q");
    check::that(!term.hasRequestedCursorStyle(), "CSI 5 q (no intermediate) is not DECSCUSR");
}

/* UTF-8 encode a code point sequence, as it would arrive from a pty. */
std::string encode(std::initializer_list<char32_t> codepoints) {
    return utf8Encode(std::u32string(codepoints.begin(), codepoints.end()));
}

/* Columns the first cluster on row 0 occupies. */
int clusterWidth(const Screen& screen) {
    if (screen.at(0, 0).isBlank() && !screen.at(0, 0).hasFlag(CellFlagWideTrailer)) return 0;
    return screen.at(0, 1).hasFlag(CellFlagWideTrailer) ? 2 : 1;
}

void testEmojiPresentationSelectors() {
    check::section("emoji presentation selectors (U+FE0E / U+FE0F)");

    /*
     * U+26A0 is dual-form: a narrow monochrome warning sign by default, a
     * double-width colour emoji once U+FE0F asks for it. Dropping the selector
     * -- as the parser used to -- makes the two indistinguishable.
     */
    {
        TerminalEmulator term(2, 20);
        feed(term, encode({0x26A0}));
        check::that(!term.screen().at(0, 0).isEmojiPresentation(),
                    "U+26A0 alone is text presentation");
        check::equal(clusterWidth(term.screen()), 1, "and occupies one column");
        check::equal(term.screen().cursorCol(), 1, "cursor advanced one column");
    }
    {
        TerminalEmulator term(2, 20);
        feed(term, encode({0x26A0, 0xFE0F}));
        check::that(term.screen().at(0, 0).isEmojiPresentation(),
                    "U+26A0 U+FE0F is emoji presentation");
        check::equal(clusterWidth(term.screen()), 2, "and widens to two columns");
        check::equal(term.screen().cursorCol(), 2, "cursor advanced two columns");
        check::equal(static_cast<unsigned>(term.screen().at(0, 0).ch), 0x26A0u,
                     "the base code point is unchanged");
    }
    {
        TerminalEmulator term(2, 20);
        feed(term, encode({0x26A0, 0xFE0E}));
        check::that(!term.screen().at(0, 0).isEmojiPresentation(),
                    "U+26A0 U+FE0E forces text presentation");
        check::equal(clusterWidth(term.screen()), 1, "and stays one column");
    }

    /* A default-emoji code point can be forced back to text and narrowed. */
    {
        TerminalEmulator term(2, 20);
        feed(term, encode({0x1F600}));
        check::that(term.screen().at(0, 0).isEmojiPresentation(),
                    "U+1F600 is emoji by default");
        check::equal(clusterWidth(term.screen()), 2, "and is double-width");
    }
    {
        TerminalEmulator term(2, 20);
        feed(term, encode({0x1F600, 0xFE0E}));
        check::that(!term.screen().at(0, 0).isEmojiPresentation(),
                    "U+1F600 U+FE0E asks for the text form");
        check::equal(clusterWidth(term.screen()), 1, "and narrows to one column");
        check::equal(term.screen().cursorCol(), 1, "the cursor came back with it");
    }

    /* A selector must not widen ordinary text. */
    {
        TerminalEmulator term(2, 20);
        feed(term, encode({U'A', 0xFE0F, U'B'}));
        check::that(!term.screen().at(0, 0).isEmojiPresentation(),
                    "a selector after a letter is ignored");
        check::equal(clusterWidth(term.screen()), 1, "the letter stays one column");
        check::equal(static_cast<unsigned>(term.screen().at(0, 1).ch), 0x42u,
                     "the next character follows immediately");
    }

    /* A selector with nothing before it must not crash or leak. */
    {
        TerminalEmulator term(2, 20);
        feed(term, encode({0xFE0F, U'X'}));
        check::equal(static_cast<unsigned>(term.screen().at(0, 0).ch), 0x58u,
                     "a leading selector is dropped");
    }

    /* Widening at the right margin must not corrupt the line. */
    {
        TerminalEmulator term(2, 4);
        feed(term, encode({U'a', U'b', U'c', 0x26A0, 0xFE0F}));
        check::equal(static_cast<unsigned>(term.screen().at(0, 3).ch), 0x26A0u,
                     "the emoji stays in the last column when it cannot widen");
        check::that(!term.screen().at(0, 0).hasFlag(CellFlagWideTrailer),
                    "no stray trailer was written");
    }
}

void testEmojiClusters() {
    check::section("emoji sequences occupy one cell");

    struct Case {
        std::u32string sequence;
        const char* what;
    };
    const Case cases[] = {
        {{0x1F468, 0x200D, 0x1F4BB},  "zero-width joiner sequence"},
        {{0x1F44D, 0x1F3FD},          "skin tone modifier"},
        {{0x1F1EA, 0x1F1F8},          "regional indicator pair (flag)"},
        {{0x0031, 0xFE0F, 0x20E3},    "keycap sequence"},
        {{0x1F3F4, 0xE0067, 0xE0062, 0xE0073, 0xE0063, 0xE0074, 0xE007F},
                                      "tag sequence (subdivision flag)"},
        {{0x1F469, 0x200D, 0x1F469, 0x200D, 0x1F467}, "multi-joiner family"},
    };

    for (const Case& item : cases) {
        TerminalEmulator term(2, 20);
        term.write(utf8Encode(item.sequence).data(), utf8Encode(item.sequence).size());

        /*
         * The whole sequence is one grapheme cluster, so it must occupy exactly
         * two columns. Printing one cell per code point is what made a joined
         * emoji sprawl across four or eight columns.
         */
        check::equal(clusterWidth(term.screen()), 2,
                     std::string(item.what) + " occupies two columns");
        check::equal(term.screen().cursorCol(), 2,
                     std::string(item.what) + " advanced the cursor by two");
        check::that(term.screen().at(0, 0).isEmojiPresentation(),
                    std::string(item.what) + " is emoji presentation");
        check::that(term.screen().at(0, 2).isBlank()
                        && !term.screen().at(0, 2).hasFlag(CellFlagWideTrailer),
                    std::string(item.what) + " left nothing beyond its two columns");
    }

    /* Three regional indicators are one flag followed by a lone indicator. */
    {
        TerminalEmulator term(2, 20);
        feed(term, encode({0x1F1EA, 0x1F1F8, 0x1F1EA}));
        check::equal(term.screen().cursorCol(), 3,
                     "a third regional indicator starts a new cluster");
    }

    /*
     * A cluster cannot span a control character. Line feed is used rather than
     * carriage return so the check is unambiguous: after a CR the modifier would
     * legitimately overwrite column 0, which says nothing about clustering.
     */
    {
        TerminalEmulator term(3, 20);
        feed(term, encode({0x1F44D}));
        feed(term, "\n");
        feed(term, encode({0x1F3FD}));
        check::equal(static_cast<unsigned>(term.screen().at(0, 0).ch), 0x1F44Du,
                     "a line feed ended the cluster, leaving the emoji untouched");
        check::that(!term.screen().at(0, 2).hasFlag(CellFlagWideTrailer),
                    "the emoji was not widened by the orphaned modifier");
        check::equal(static_cast<unsigned>(term.screen().at(1, 2).ch), 0x1F3FDu,
                     "the orphaned modifier printed on its own as a swatch");
    }
    {
        TerminalEmulator term(3, 20);
        feed(term, encode({0x26A0}));
        feed(term, "\x1b[1;10H");
        feed(term, encode({0xFE0F}));
        check::that(!term.screen().at(0, 0).isEmojiPresentation(),
                    "a cursor movement ended the cluster, so the selector was dropped");
    }

    /* Combining marks attach rather than taking a column of their own. */
    {
        TerminalEmulator term(2, 20);
        feed(term, encode({U'e', 0x0301, U'x'}));
        check::equal(static_cast<unsigned>(term.screen().at(0, 0).ch), 0x65u, "'e' printed");
        check::equal(static_cast<unsigned>(term.screen().at(0, 1).ch), 0x78u,
                     "the combining acute took no column");
    }
}

void testUtf8() {
    check::section("UTF-8 decoding across chunk boundaries");

    const std::string text = "a\xe2\x9c\x93z";   // 'a', U+2713, 'z'
    TerminalEmulator term(2, 10);
    /* Split mid-sequence, as a 4 KiB pty read can. */
    term.write(text.data(), 2);
    term.write(text.data() + 2, text.size() - 2);

    check::equal(static_cast<unsigned>(term.screen().at(0, 0).ch), 0x61u, "'a' decoded");
    check::equal(static_cast<unsigned>(term.screen().at(0, 1).ch), 0x2713u,
                 "the split U+2713 decoded correctly");
    check::equal(static_cast<unsigned>(term.screen().at(0, 2).ch), 0x7Au, "'z' decoded");

    /* Invalid input must not desynchronise the parser. */
    TerminalEmulator bad(2, 10);
    const std::string invalid = "\xff" "A";
    bad.write(invalid.data(), invalid.size());
    check::equal(static_cast<unsigned>(bad.screen().at(0, 1).ch), 0x41u,
                 "an invalid byte yields U+FFFD and parsing continues");
}

void testWideCharacters() {
    check::section("double-width characters");

    TerminalEmulator term(2, 10);
    /* U+4E2D is East Asian Wide and must occupy two columns. */
    const std::string cjk = "\xe4\xb8\xad" "x";
    term.write(cjk.data(), cjk.size());

    check::equal(static_cast<unsigned>(term.screen().at(0, 0).ch), 0x4E2Du, "wide glyph at col 0");
    check::that(term.screen().at(0, 1).hasFlag(CellFlagWideTrailer),
                "col 1 is marked as the trailing half");
    check::equal(static_cast<unsigned>(term.screen().at(0, 2).ch), 0x78u,
                 "the following character lands at col 2");
}

void testSpaceSeparators() {
    check::section("spaces that are not U+0020");

    /*
     * `tree` indents with "|" plus two NO-BREAK SPACEs. They must occupy their
     * columns and be stored as themselves -- the decision to paint nothing for
     * them belongs to the renderer, not the model, or a later copy-to-clipboard
     * would lose the character.
     */
    TerminalEmulator term(2, 10);
    const std::string nbsp = "a\xc2\xa0\xc2\xa0" "b";
    term.write(nbsp.data(), nbsp.size());

    check::equal(static_cast<unsigned>(term.screen().at(0, 1).ch), 0xA0u,
                 "a NO-BREAK SPACE is stored as itself");
    check::equal(static_cast<unsigned>(term.screen().at(0, 3).ch), 0x62u,
                 "and each one takes exactly one column");
    check::that(isSpaceSeparator(0x00A0) && isSpaceSeparator(0x2007)
                    && isSpaceSeparator(0x3000),
                "the space separators are recognised as blank");
    check::that(!isSpaceSeparator(0x2502) && !isSpaceSeparator(U'x'),
                "and ordinary glyphs are not");

    /* U+3000 is East Asian Wide: blank, but two columns of it. */
    TerminalEmulator wide(2, 10);
    const std::string ideographic = "\xe3\x80\x80" "z";
    wide.write(ideographic.data(), ideographic.size());
    check::that(wide.screen().at(0, 1).hasFlag(CellFlagWideTrailer),
                "an IDEOGRAPHIC SPACE keeps its trailer cell");
    check::equal(static_cast<unsigned>(wide.screen().at(0, 2).ch), 0x7Au,
                 "so the next character lands two columns along");
}

void testResize() {
    check::section("resize");

    TerminalEmulator term(4, 10);
    feed(term, "one\r\ntwo\r\nthree");
    term.resize(4, 5);
    check::equal(rowText(term.screen(), 0), std::string("one"),
                 "narrowing truncates rather than losing rows");
    check::equal(term.cols(), 5, "columns updated");

    term.resize(10, 20);
    check::equal(term.rows(), 10, "rows updated");
    check::that(term.screen().cursorRow() < term.rows(), "cursor stayed in range");
}

void testScrollbackCapture() {
    check::section("scrollback: rows leaving the top are kept");

    TerminalEmulator term(3, 10);
    feed(term, "one\r\ntwo\r\nthree\r\nfour\r\nfive");

    check::equal(term.historySize(), 2, "two rows scrolled off and were kept");
    check::equal(rowText(term.screen(), 0), std::string("three"),
                 "the live screen holds the last three rows");
    check::equal(rowText(term.screen(), 2), std::string("five"), "and the newest at the bottom");

    /* Nothing is displayed differently until the view is moved. */
    check::equal(viewRowText(term.screen(), 0), std::string("three"),
                 "with no offset the view is the live screen");

    check::that(term.scrollViewBy(1), "scrolling back one row moves the view");
    check::equal(viewRowText(term.screen(), 0), std::string("two"),
                 "the row above the screen comes from the history");
    check::equal(viewRowText(term.screen(), 1), std::string("three"),
                 "the live rows follow it");
    check::equal(term.screen().cursorRow(), 2,
                 "the cursor stays where the application put it");

    check::that(term.scrollViewBy(50), "scrolling past the oldest line still moves");
    check::equal(term.viewOffset(), 2, "and clamps at the oldest line kept");
    check::equal(viewRowText(term.screen(), 0), std::string("one"), "which is the first row printed");

    check::that(!term.scrollViewBy(1), "already at the top, nothing moves");

    /* New output snaps back, or text would arrive out of sight. */
    feed(term, "!");
    check::equal(term.viewOffset(), 0, "output returns the view to the live screen");
    check::equal(rowText(term.screen(), 2), std::string("five!"), "and the text landed there");
}

void testScrollbackLimits() {
    check::section("scrollback: limit, regions and the alternate screen");

    TerminalEmulator term(2, 10);
    term.setScrollbackLines(3);
    for (int i = 0; i < 20; ++i) feed(term, "line\r\n");
    check::equal(term.historySize(), 3, "the limit bounds what is kept");

    /* A DECSTBM region is a subwindow the application scrolls itself; those
     * rows are not history. */
    TerminalEmulator region(4, 10);
    feed(region, "\x1b[2;3r");           // scroll region rows 2-3
    feed(region, "\x1b[3;1Ha\r\nb\r\nc\r\n");
    check::equal(region.historySize(), 0, "scrolling inside a region keeps no history");

    /* The alternate screen redraws rather than scrolls. */
    TerminalEmulator alt(3, 10);
    feed(alt, "\x1b[?1049h");
    for (int i = 0; i < 10; ++i) feed(alt, "x\r\n");
    check::equal(alt.historySize(), 0, "the alternate screen keeps no history");
    check::that(alt.alternateScreenActive(), "and reports that it is active");
    feed(alt, "\x1b[?1049l");
    check::equal(alt.viewOffset(), 0, "leaving it returns to the live view");

    /* ED 3 is "erase saved lines". */
    TerminalEmulator erase(2, 10);
    feed(erase, "a\r\nb\r\nc\r\nd\r\n");
    check::that(erase.historySize() > 0, "history accumulated");
    feed(erase, "aaa");
    feed(erase, "\x1b[3J");
    check::equal(erase.historySize(), 0, "ED 3 erases the saved lines");
    check::equal(rowText(erase.screen(), 1), std::string("aaa"),
                 "and leaves the display alone, as xterm does");

    /* Zero disables it outright. */
    TerminalEmulator off(2, 10);
    off.setScrollbackLines(0);
    for (int i = 0; i < 5; ++i) feed(off, "y\r\n");
    check::equal(off.historySize(), 0, "a limit of zero keeps nothing");
    check::that(!off.scrollViewBy(1), "and there is nowhere to scroll");
}

void testScrollbackResize() {
    check::section("scrollback: shrinking keeps the rows it drops");

    TerminalEmulator term(4, 10);
    feed(term, "aa\r\nbb\r\ncc\r\ndd");
    check::equal(term.historySize(), 0, "nothing has scrolled yet");

    term.resize(2, 10);
    check::equal(term.historySize(), 2, "the rows dropped from the top became history");
    check::equal(rowText(term.screen(), 0), std::string("cc"), "the bottom rows stayed");

    term.scrollViewBy(2);
    check::equal(viewRowText(term.screen(), 0), std::string("aa"),
                 "and the dropped rows are still readable");

    /* History is not reflowed: a widened window leaves old rows as they were. */
    term.scrollViewToBottom();
    term.resize(2, 20);
    term.scrollViewBy(2);
    check::equal(viewRowText(term.screen(), 0), std::string("aa"),
                 "a history row survives a width change unreflowed");
}

void testDeviceReports() {
    check::section("device status reports");

    std::string reply;
    TerminalEmulator term(10, 20);
    term.setReplySink([&reply](const std::string& value) { reply += value; });

    feed(term, "\x1b[3;7H\x1b[6n");
    check::equal(reply, std::string("\x1b[3;7R"), "DSR 6 reports the 1-based cursor position");

    reply.clear();
    feed(term, "\x1b[5n");
    check::equal(reply, std::string("\x1b[0n"), "DSR 5 reports terminal ok");
}

} // namespace

int main() {
    testDeferredWrap();
    testZshPromptArtifact();
    testOscTermination();
    testCsiParsing();
    testScrollRegion();
    testAlternateScreen();
    testSgrColors();
    testEraseKeepsBackground();
    testOscColors();
    testCursorStyle();
    testEmojiPresentationSelectors();
    testEmojiClusters();
    testUtf8();
    testWideCharacters();
    testResize();
    testSpaceSeparators();
    testScrollbackCapture();
    testScrollbackLimits();
    testScrollbackResize();
    testDeviceReports();
    return check::report("test_terminal");
}
