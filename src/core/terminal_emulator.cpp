/*
 * TerminalEmulator - VT semantics implementation
 */

#include "terminal_emulator.h"
#include "base64.h"
#include "unicode.h"
#include <QString>
#include <QStringList>
#include <algorithm>

namespace {
constexpr int kMaxRows = 4096;
constexpr int kMaxCols = 4096;

/* Enough that "scroll up until you find it" works for a build log, small
 * enough that a pane costs a few megabytes rather than tens. Config overrides
 * it; the constant is what a bare emulator (and every test) gets. */
constexpr int kDefaultScrollbackLines = 10000;
constexpr int kMaxScrollbackLines = 1000000;
} // namespace

TerminalEmulator::TerminalEmulator(int rows, int cols)
    : primary_(rows, cols)
    , alternate_(rows, cols)
    , active_(&primary_)
{
    parser_.setHandler(this);
    primary_.setHistoryLimit(kDefaultScrollbackLines);
    /* The alternate screen deliberately keeps none; see the header. */
    alternate_.setHistoryLimit(0);
    /*
     * And it is not rewrapped on resize either. What is on it is a full-screen
     * application's own layout -- htop's table, vim's windows -- laid out for
     * the size the application was told about; joining its rows into logical
     * lines would be nonsense. The application redraws instead.
     */
    alternate_.setReflowEnabled(false);
}

void TerminalEmulator::write(const char* data, size_t length) {
    /*
     * New output snaps the view back to the bottom. Anything else means text
     * arrives somewhere the user cannot see, and the cursor -- which is drawn at
     * its position within the *view* -- would appear to have wandered off.
     */
    active_->scrollViewToBottom();

    scratch_.clear();
    decoder_.decode(data, length, scratch_);
    parser_.advance(scratch_.data(), scratch_.size());
}

void TerminalEmulator::resize(int rows, int cols) {
    rows = std::clamp(rows, 1, kMaxRows);
    cols = std::clamp(cols, 1, kMaxCols);
    primary_.resize(rows, cols, pen_);
    alternate_.resize(rows, cols, pen_);
}

void TerminalEmulator::setBasePalette(const Palette& base) {
    basePalette_ = base;
    palette_ = base;
}

void TerminalEmulator::reset() {
    pen_.reset();
    palette_ = basePalette_;
    cursorStyleRequested_ = false;
    savedPen_ = pen_;
    primary_.reset(pen_);
    alternate_.reset(pen_);
    active_ = &primary_;
    alternateActive_ = false;
    awaitingJoinedBase_ = false;
    clusterIsEmoji_ = false;
    regionalIndicatorCount_ = 0;
    bracketedPaste_ = false;
    applicationCursorKeys_ = false;
    newlineMode_ = false;
    mouseTracking_ = MouseTracking::None;
    mouseEncoding_ = MouseEncoding::X10;
    focusEvents_ = false;
    parser_.reset();
    decoder_.reset();
}

void TerminalEmulator::setScrollbackLines(int lines) {
    primary_.setHistoryLimit(std::clamp(lines, 0, kMaxScrollbackLines));
}

bool TerminalEmulator::scrollViewBy(int lines) {
    return active_->scrollViewBy(lines);
}

bool TerminalEmulator::scrollViewToBottom() {
    return active_->scrollViewToBottom();
}

bool TerminalEmulator::scrollViewToTop() {
    return active_->scrollViewToTop();
}

bool TerminalEmulator::scrollViewToLine(int64_t line, int preferredRow) {
    return active_->scrollViewToLine(line, preferredRow);
}

void TerminalEmulator::clearScrollback() {
    /* Only the primary screen has any, but clearing the active one keeps the
     * action honest while an alternate-screen application is up. */
    active_->clearHistory();
}

void TerminalEmulator::sendReply(const std::string& text) {
    if (reply_) reply_(text);
}

/* ------------------------------------------------------------ VTHandler */

void TerminalEmulator::print(char32_t ch) {
    /*
     * Printable ASCII takes the short way round, and it is worth a special case
     * because it is very nearly everything a terminal is ever asked to draw.
     *
     * Such a code point cannot take part in a grapheme cluster: it is not a
     * joiner, a variation selector, an emoji modifier, a regional indicator, a
     * tag, an enclosing keycap or a combining mark, it has no emoji
     * presentation, and it is one column wide. The general path establishes all
     * of that by walking a dozen sorted range tables per character, and those
     * tables alone were measurably several percent of the cost of receiving
     * output.
     *
     * The two cluster flags are still cleared, exactly as beginCluster() would:
     * an ordinary character ends whatever cluster was in progress.
     * `awaitingJoinedBase_` is the one case where an ASCII code point does carry
     * cluster meaning -- a ZWJ arrived immediately before it -- so the fast path
     * stands aside and lets continueCluster() have it.
     */
    if (ch >= U' ' && ch < 0x7F && !awaitingJoinedBase_) {
        clusterIsEmoji_ = false;
        regionalIndicatorCount_ = 0;
        active_->print(ch, pen_, 1, 0);
        return;
    }

    if (continueCluster(ch)) return;
    beginCluster(ch);
}

void TerminalEmulator::beginCluster(char32_t ch) {
    const int width = charWidth(ch);
    if (width <= 0) {
        /* A lone combining mark with nothing to attach to. Dropping it is
         * preferable to letting it consume a column. */
        return;
    }

    clusterIsEmoji_ = hasEmojiPresentationByDefault(ch);
    regionalIndicatorCount_ = isRegionalIndicator(ch) ? 1 : 0;
    awaitingJoinedBase_ = false;

    const uint16_t flags = clusterIsEmoji_
                               ? static_cast<uint16_t>(CellFlagEmojiPresentation)
                               : uint16_t{0};

    active_->print(ch, pen_, presentationWidth(clusterIsEmoji_, width), flags);
}

bool TerminalEmulator::continueCluster(char32_t ch) {
    /* A joiner arrived last time, so this base code point joins that cell. */
    if (awaitingJoinedBase_) {
        awaitingJoinedBase_ = false;
        if (active_->hasAdjustableCell()) {
            /* A joined sequence is always an emoji, and always double-width. */
            clusterIsEmoji_ = true;
            active_->adjustLastCell(2, CellFlagEmojiPresentation, 0, pen_);
            return true;
        }
        return false;   // nothing to join; treat it as a fresh cluster
    }

    if (!active_->hasAdjustableCell()) {
        /* Nothing to extend. A stray continuation code point is dropped. */
        return isZeroWidthJoiner(ch) || isVariationSelector(ch)
            || isTagCharacter(ch) || isEnclosingKeycap(ch)
            || (isZeroWidth(ch) && !isRegionalIndicator(ch));
    }

    if (isEmojiPresentationSelector(ch)) {
        /*
         * U+FE0F. Only meaningful on a pictograph: after ordinary text it must
         * not widen a letter into two columns.
         */
        if (!isExtendedPictographic(active_->lastPrintedChar())) return true;
        clusterIsEmoji_ = true;
        active_->adjustLastCell(2, CellFlagEmojiPresentation, 0, pen_);
        return true;
    }

    if (isTextPresentationSelector(ch)) {
        /* U+FE0E: force the monochrome, single-column form. */
        clusterIsEmoji_ = false;
        active_->adjustLastCell(charWidth(active_->lastPrintedChar()) == 2
                                    && !hasEmojiPresentationByDefault(active_->lastPrintedChar())
                                        ? 2 : 1,
                                0, CellFlagEmojiPresentation, pen_);
        return true;
    }

    if (isZeroWidthJoiner(ch)) {
        awaitingJoinedBase_ = true;
        return true;
    }

    if (isEmojiModifier(ch)) {
        /* A skin-tone modifier belongs to the emoji before it. */
        clusterIsEmoji_ = true;
        active_->adjustLastCell(2, CellFlagEmojiPresentation, 0, pen_);
        return true;
    }

    if (isTagCharacter(ch)) {
        /* Tag sequences spell out a subdivision after a base flag; the tags
         * themselves are never rendered. */
        return true;
    }

    if (isEnclosingKeycap(ch)) {
        /* Completes a keycap sequence, which is presented as an emoji. */
        clusterIsEmoji_ = true;
        active_->adjustLastCell(2, CellFlagEmojiPresentation, 0, pen_);
        return true;
    }

    if (isRegionalIndicator(ch)) {
        /* A second indicator completes a flag; a third starts a new pair. */
        if (regionalIndicatorCount_ == 1) {
            regionalIndicatorCount_ = 2;
            clusterIsEmoji_ = true;
            active_->adjustLastCell(2, CellFlagEmojiPresentation, 0, pen_);
            return true;
        }
        return false;
    }

    if (isZeroWidth(ch)) {
        /*
         * A combining mark. It changes nothing about the cell's geometry, and
         * composing it into the base glyph would need text shaping, so it is
         * absorbed rather than given a column of its own.
         */
        return true;
    }

    return false;   // an ordinary character: it starts a new cluster
}

void TerminalEmulator::control(uint8_t code) {
    /* A control character always ends the current grapheme cluster. */
    awaitingJoinedBase_ = false;
    regionalIndicatorCount_ = 0;

    switch (code) {
    case C0_BEL:
        if (bellSink_) bellSink_();
        break;
    case C0_BS:
        active_->backspace();
        break;
    case C0_HT:
        active_->tab();
        break;
    case C0_LF:
    case C0_VT:
    case C0_FF:
        /*
         * A bare LF only moves down a row; the carriage return comes from the
         * shell's own CR (or from LNM). The previous implementation folded CR
         * into LF unconditionally, which hid missing-CR bugs and broke any
         * application that relies on plain index movement.
         */
        if (newlineMode_) active_->carriageReturn();
        active_->lineFeed(pen_);
        break;
    case C0_CR:
        active_->carriageReturn();
        break;
    case C0_SO:
    case C0_SI:
        /* Character-set shifts: only the US-ASCII/DEC-graphics pair matters and
         * it is not implemented yet, so stay in the current set. */
        break;
    default:
        break;
    }
}

void TerminalEmulator::escDispatch(char intermediate, char final) {
    if (intermediate != 0) {
        /* ESC ( B / ESC ) 0 etc. select character sets - accepted and ignored. */
        return;
    }

    switch (final) {
    case 'D':  // IND - index
        active_->lineFeed(pen_);
        break;
    case 'E':  // NEL - next line
        active_->carriageReturn();
        active_->lineFeed(pen_);
        break;
    case 'M':  // RI - reverse index
        active_->reverseIndex(pen_);
        break;
    case '7':  // DECSC - save cursor and rendition
        active_->saveCursor();
        savedPen_ = pen_;
        break;
    case '8':  // DECRC - restore cursor and rendition
        active_->restoreCursor();
        pen_ = savedPen_;
        break;
    case 'c':  // RIS - full reset
        reset();
        break;
    case '=':  // DECKPAM - application keypad
    case '>':  // DECKPNM - numeric keypad
        break;
    default:
        break;
    }
}

void TerminalEmulator::csiDispatch(const CsiSequence& seq) {
    /* DEC private sequences: CSI ? ... h/l and friends. */
    if (seq.privateMarker == '?') {
        if (seq.final == 'h') { setMode(seq, true); return; }
        if (seq.final == 'l') { setMode(seq, false); return; }
        return;  // DECRQM and other private queries are not answered yet
    }
    if (seq.privateMarker != 0) {
        /* CSI > / CSI < / CSI = : terminal identification and XTerm extensions.
         * They are recognised (so their parameters are never printed as text)
         * and otherwise ignored. */
        return;
    }

    switch (seq.final) {
    case '@':  // ICH - insert blank characters
        active_->insertChars(seq.param(0, 1), pen_);
        break;
    case 'A':  // CUU
        active_->moveBy(-std::max(1, seq.param(0, 1)), 0);
        break;
    case 'B':  // CUD
    case 'e':  // VPR
        active_->moveBy(std::max(1, seq.param(0, 1)), 0);
        break;
    case 'C':  // CUF
    case 'a':  // HPR
        active_->moveBy(0, std::max(1, seq.param(0, 1)));
        break;
    case 'D':  // CUB
        active_->moveBy(0, -std::max(1, seq.param(0, 1)));
        break;
    case 'E':  // CNL - cursor next line
        active_->moveBy(std::max(1, seq.param(0, 1)), 0);
        active_->moveToColumn(0);
        break;
    case 'F':  // CPL - cursor previous line
        active_->moveBy(-std::max(1, seq.param(0, 1)), 0);
        active_->moveToColumn(0);
        break;
    case 'G':  // CHA - cursor horizontal absolute
    case '`':  // HPA
        active_->moveToColumn(std::max(1, seq.param(0, 1)) - 1);
        break;
    case 'd':  // VPA - vertical position absolute
        active_->moveToRow(std::max(1, seq.param(0, 1)) - 1);
        break;
    case 'H':  // CUP
    case 'f':  // HVP
        active_->moveTo(std::max(1, seq.param(0, 1)) - 1,
                        std::max(1, seq.param(1, 1)) - 1);
        break;
    case 'I':  // CHT - forward tab stops
        active_->tab(std::max(1, seq.param(0, 1)));
        break;
    case 'Z':  // CBT - backward tab stops
        active_->backTab(std::max(1, seq.param(0, 1)));
        break;
    case 'J':  // ED
        active_->eraseInDisplay(seq.param(0, 0), pen_);
        break;
    case 'K':  // EL
        active_->eraseInLine(seq.param(0, 0), pen_);
        break;
    case 'L':  // IL - insert lines
        active_->insertLines(seq.param(0, 1), pen_);
        break;
    case 'M':  // DL - delete lines
        active_->deleteLines(seq.param(0, 1), pen_);
        break;
    case 'P':  // DCH - delete characters
        active_->deleteChars(seq.param(0, 1), pen_);
        break;
    case 'S':  // SU - scroll up
        active_->scrollUp(seq.param(0, 1), pen_);
        break;
    case 'T':  // SD - scroll down
        active_->scrollDown(seq.param(0, 1), pen_);
        break;
    case 'X':  // ECH - erase characters
        active_->eraseChars(seq.param(0, 1), pen_);
        break;
    case 'm':  // SGR
        applySgr(seq);
        break;
    case 'n':  // DSR
        deviceStatusReport(seq);
        break;
    case 'c':  // DA1 - primary device attributes: "VT220 with colour"
        sendReply("\x1b[?62;1;6;22c");
        break;
    case 'r':  // DECSTBM - set scrolling region
        if (seq.count() == 0) {
            active_->resetScrollRegion();
            active_->moveTo(0, 0);
        } else {
            active_->setScrollRegion(std::max(1, seq.param(0, 1)) - 1,
                                     seq.param(1, active_->rows()) - 1);
        }
        break;
    case 's':  // SCOSC - save cursor
        active_->saveCursor();
        savedPen_ = pen_;
        break;
    case 'u':  // SCORC - restore cursor
        active_->restoreCursor();
        pen_ = savedPen_;
        break;
    case 'h':  // SM - ANSI modes
    case 'l':  // RM
        setMode(seq, seq.final == 'h');
        break;
    case 't':  // XTWINOPS - window manipulation, deliberately not implemented
        break;
    case 'q':
        /* DECSCUSR is "CSI n SP q"; the space intermediate distinguishes it
         * from other 'q' finals. */
        if (seq.intermediate == ' ') {
            setCursorStyle(seq.param(0, 0));
        }
        break;
    default:
        break;
    }
}

void TerminalEmulator::oscDispatch(int command, const std::u32string& data) {
    switch (command) {
    case 0:  // set icon name and window title
    case 2:  // set window title
        if (titleSink_) titleSink_(utf8Encode(data));
        break;

    case 4:    // set/query one or more palette entries
        handlePaletteOsc(data, /*reset=*/false);
        break;
    case 104:  // reset palette entries (all of them when no parameter)
        handlePaletteOsc(data, /*reset=*/true);
        break;

    case 10:   // default foreground
    case 11:   // default background
    case 12:   // cursor colour
        handleDynamicColorOsc(command, data);
        break;
    case 110:  // reset default foreground
    case 111:  // reset default background
    case 112:  // reset cursor colour
        resetDynamicColor(command - 100);
        break;

    case 52:   // clipboard set/query
        handleClipboardOsc(data);
        break;

    default:
        /* OSC 1 (icon name), 7 (working directory), 8 (hyperlinks) and 133
         * (prompt marks) are recognised by the parser and dropped here. */
        break;
    }
}

/*
 * OSC 4 ; index ; spec [ ; index ; spec ]...   set palette entries
 * OSC 4 ; index ; ?                            query a palette entry
 * OSC 104                                      reset the whole palette
 * OSC 104 ; index [ ; index ]...               reset specific entries
 */
void TerminalEmulator::handlePaletteOsc(const std::u32string& data, bool reset) {
    const QString text = QString::fromStdString(utf8Encode(data));

    if (reset && text.isEmpty()) {
        palette_ = basePalette_;
        return;
    }

    const QStringList fields = text.split(QLatin1Char(';'));

    if (reset) {
        for (const QString& field : fields) {
            bool ok = false;
            const int index = field.trimmed().toInt(&ok);
            if (!ok) continue;
            /* Restore from the configured palette, not the built-in one, so a
             * user's themed slot survives an application's reset. */
            palette_.setEntry(index, basePalette_.entry(index));
        }
        return;
    }

    /* Set/query pairs. */
    for (int i = 0; i + 1 < fields.size(); i += 2) {
        bool ok = false;
        const int index = fields[i].trimmed().toInt(&ok);
        if (!ok || index < 0 || index >= Palette::PaletteSize) continue;

        const QString spec = fields[i + 1].trimmed();

        if (spec == QLatin1String("?")) {
            const QString reply = QStringLiteral("\x1b]4;%1;%2\x1b\\")
                                      .arg(index)
                                      .arg(formatColorSpec(palette_.entry(index)));
            sendReply(reply.toStdString());
            continue;
        }

        if (const QColor color = parseColorSpec(spec); color.isValid()) {
            palette_.setEntry(index, color);
        }
    }
}

/*
 * OSC 52 - clipboard.
 *
 *   OSC 52 ; Pc ; <base64>  put text on the selection Pc names
 *   OSC 52 ; Pc ; ?         ask what is on it
 *
 * Pc is a list of selection names ('c' clipboard, 'p'/'s' primary); an empty
 * one means the clipboard. Only the first name is acted on, which is what every
 * terminal that implements this does -- the alternative is one keystroke
 * scattering text across several selections.
 *
 * This is how a program on the far side of an ssh connection copies to the
 * local clipboard, and it is the reason `tmux save-buffer` and an editor's yank
 * can reach it at all. The reverse -- letting that program *read* the clipboard
 * -- is a genuine hazard, which is why both directions are sinks the UI layer
 * installs deliberately rather than something answered from here.
 */
void TerminalEmulator::handleClipboardOsc(const std::u32string& data) {
    const size_t separator = data.find(U';');
    const std::u32string selection = data.substr(0, separator == std::u32string::npos
                                                        ? 0
                                                        : separator);
    const std::u32string payload = separator == std::u32string::npos
                                       ? std::u32string()
                                       : data.substr(separator + 1);

    /* Selection names are ASCII letters; anything else is malformed. */
    char which = 'c';
    if (!selection.empty()) {
        const char32_t first = selection[0];
        if (first > 0x7F) return;
        which = static_cast<char>(first);
    }

    if (payload == U"?") {
        if (!clipboardReader_) return;
        std::string text;
        if (!clipboardReader_(which, text)) return;

        std::string reply = "\x1b]52;";
        reply += which;
        reply += ';';
        reply += base64Encode(text);
        reply += "\x1b\\";   // ST
        sendReply(reply);
        return;
    }

    if (!clipboardWriter_) return;

    /* The payload is base64, so every byte of it is ASCII. */
    std::string encoded;
    encoded.reserve(payload.size());
    for (const char32_t ch : payload) {
        if (ch > 0x7F) return;
        encoded += static_cast<char>(ch);
    }

    std::string text;
    if (!base64Decode(encoded, text)) return;
    clipboardWriter_(which, text);
}

/*
 * OSC 10 / 11 / 12 - default foreground, default background, cursor colour.
 * A spec of "?" is a query, which is how applications discover whether the
 * terminal is light or dark. Neovim asks "OSC 11 ; ?" at start-up and, with no
 * answer, has to guess -- which gets a light colour scheme wrong.
 */
void TerminalEmulator::handleDynamicColorOsc(int which, const std::u32string& data) {
    const QString spec = QString::fromStdString(utf8Encode(data)).trimmed();
    if (spec.isEmpty()) return;

    auto current = [&]() {
        switch (which) {
        case 10: return palette_.defaultForeground();
        case 11: return palette_.defaultBackground();
        default: return palette_.cursorColor();
        }
    };

    if (spec == QLatin1String("?")) {
        const QString reply = QStringLiteral("\x1b]%1;%2\x1b\\")
                                  .arg(which)
                                  .arg(formatColorSpec(current()));
        sendReply(reply.toStdString());
        return;
    }

    const QColor color = parseColorSpec(spec);
    if (!color.isValid()) return;

    switch (which) {
    case 10: palette_.setDefaultForeground(color); break;
    case 11: palette_.setDefaultBackground(color); break;
    case 12: palette_.setCursorColor(color); break;
    default: break;
    }
}

void TerminalEmulator::resetDynamicColor(int which) {
    switch (which) {
    case 10: palette_.setDefaultForeground(basePalette_.defaultForeground()); break;
    case 11: palette_.setDefaultBackground(basePalette_.defaultBackground()); break;
    case 12: palette_.setCursorColor(basePalette_.cursorColor()); break;
    default: break;
    }
}

/*
 * DECSCUSR - CSI n SP q. Editors use this to show the mode: a bar while
 * inserting, a block otherwise.
 */
void TerminalEmulator::setCursorStyle(int parameter) {
    switch (parameter) {
    case 0:  // default
        cursorStyleRequested_ = false;
        cursorBlinkRequested_ = true;
        return;
    case 1: requestedCursorStyle_ = CursorStyle::Block;     cursorBlinkRequested_ = true;  break;
    case 2: requestedCursorStyle_ = CursorStyle::Block;     cursorBlinkRequested_ = false; break;
    case 3: requestedCursorStyle_ = CursorStyle::Underline; cursorBlinkRequested_ = true;  break;
    case 4: requestedCursorStyle_ = CursorStyle::Underline; cursorBlinkRequested_ = false; break;
    case 5: requestedCursorStyle_ = CursorStyle::Bar;       cursorBlinkRequested_ = true;  break;
    case 6: requestedCursorStyle_ = CursorStyle::Bar;       cursorBlinkRequested_ = false; break;
    default:
        return;
    }
    cursorStyleRequested_ = true;
}

/* ----------------------------------------------------------------- modes */

void TerminalEmulator::setMode(const CsiSequence& seq, bool enable) {
    const bool isPrivate = seq.privateMarker == '?';

    for (size_t i = 0; i < seq.count(); ++i) {
        const int mode = seq.param(i, 0);

        if (!isPrivate) {
            switch (mode) {
            case 20:  // LNM - line feed / new line mode
                newlineMode_ = enable;
                break;
            default:
                break;
            }
            continue;
        }

        switch (mode) {
        case 1:     // DECCKM - application cursor keys
            applicationCursorKeys_ = enable;
            break;
        case 7:     // DECAWM - autowrap
            primary_.setAutoWrap(enable);
            alternate_.setAutoWrap(enable);
            break;
        case 25:    // DECTCEM - cursor visibility
            active_->setCursorVisible(enable);
            break;
        case 1047:  // alternate screen buffer
        case 1049:  // alternate buffer + save/restore cursor
            useAlternateScreen(enable);
            break;
        case 1048:  // save/restore cursor only
            if (enable) { active_->saveCursor(); savedPen_ = pen_; }
            else        { active_->restoreCursor(); pen_ = savedPen_; }
            break;
        case 2004:  // bracketed paste
            bracketedPaste_ = enable;
            break;

        /*
         * Mouse reporting. Tracking and encoding are set independently, and an
         * application commonly enables several: `?1002h ?1006h` is the usual
         * pair. Disabling one tracking mode while another is still on must not
         * turn reporting off wholesale, hence the "only if it is mine" tests.
         */
        case 9:     // X10 compatibility: presses only
            setMouseTracking(MouseTracking::X10, enable);
            break;
        case 1000:  // presses and releases
            setMouseTracking(MouseTracking::Normal, enable);
            break;
        case 1002:  // ... and drag
            setMouseTracking(MouseTracking::ButtonEvent, enable);
            break;
        case 1003:  // ... and all motion
            setMouseTracking(MouseTracking::AnyEvent, enable);
            break;
        case 1004:  // focus in / focus out reporting
            focusEvents_ = enable;
            break;
        case 1005:  // UTF-8 coordinates
            setMouseEncoding(MouseEncoding::Utf8, enable);
            break;
        case 1006:  // SGR coordinates
            setMouseEncoding(MouseEncoding::Sgr, enable);
            break;
        case 1015:  // urxvt coordinates
            setMouseEncoding(MouseEncoding::Urxvt, enable);
            break;
        case 1007:  // alternate scroll: wheel to cursor keys on the alt screen
            alternateScroll_ = enable;
            break;

        default:
            /* Sixel scrolling and the rest are not implemented; ignoring them
             * is the correct behaviour, since applications probe by feature. */
            break;
        }
    }
}

/*
 * A mode reset only counts when it names the mode that is actually in force.
 * Applications routinely enable 1002 and 1003 together and then disable them one
 * at a time on the way out; treating any `l` as "off" would drop reporting while
 * the application still expects it.
 */
void TerminalEmulator::setMouseTracking(MouseTracking mode, bool enable) {
    if (enable) {
        mouseTracking_ = mode;
    } else if (mouseTracking_ == mode) {
        mouseTracking_ = MouseTracking::None;
    }
}

void TerminalEmulator::setMouseEncoding(MouseEncoding mode, bool enable) {
    if (enable) {
        mouseEncoding_ = mode;
    } else if (mouseEncoding_ == mode) {
        mouseEncoding_ = MouseEncoding::X10;
    }
}

void TerminalEmulator::useAlternateScreen(bool enable) {
    if (enable == alternateActive_) return;

    /* Whichever screen we are leaving, leave it looking at its live rows: the
     * offset would otherwise still be there on the way back. */
    active_->scrollViewToBottom();

    if (enable) {
        active_->saveCursor();
        savedPen_ = pen_;
        alternate_.reset(pen_);
        active_ = &alternate_;
        alternateActive_ = true;
    } else {
        active_ = &primary_;
        alternateActive_ = false;
        pen_ = savedPen_;
        active_->restoreCursor();
    }
}

void TerminalEmulator::deviceStatusReport(const CsiSequence& seq) {
    switch (seq.param(0, 0)) {
    case 5:  // "terminal ok"
        sendReply("\x1b[0n");
        break;
    case 6: {  // CPR - report cursor position, 1-based
        std::string reply = "\x1b[";
        reply += std::to_string(active_->cursorRow() + 1);
        reply += ';';
        reply += std::to_string(active_->cursorCol() + 1);
        reply += 'R';
        sendReply(reply);
        break;
    }
    default:
        break;
    }
}

/* ------------------------------------------------------------------- SGR */

size_t TerminalEmulator::parseExtendedColor(const CsiSequence& seq, size_t index, Color& out) {
    /*
     * Handles both spellings of the extended-colour forms:
     *
     *   38;5;N        38:5:N        indexed
     *   38;2;R;G;B    38:2:R:G:B    direct RGB
     *
     * The old code fed every parameter through a flat switch, so "38;5;208"
     * matched nothing for 38, then hit `5` (blink) and `208` (unknown) - which
     * is why 256-colour output came out uncoloured.
     *
     * Returns the number of parameters consumed, including the 38/48 itself.
     */
    const int selector = seq.param(index + 1, -1);

    if (selector == 5) {
        const int value = seq.param(index + 2, -1);
        if (value >= 0 && value <= 255) {
            out = Color::indexed(static_cast<uint8_t>(value));
        }
        return 3;
    }

    if (selector == 2) {
        /* Some emitters use the colon form with a colour-space id:
         * 38:2::R:G:B. Skip an empty slot if that is what we are looking at. */
        size_t base = index + 2;
        if (seq.param(base, -1) < 0 && seq.count() > base + 3) {
            ++base;
        }
        const int r = seq.param(base + 0, -1);
        const int g = seq.param(base + 1, -1);
        const int b = seq.param(base + 2, -1);
        if (r >= 0 && g >= 0 && b >= 0) {
            out = Color::rgb(static_cast<uint8_t>(std::min(r, 255)),
                             static_cast<uint8_t>(std::min(g, 255)),
                             static_cast<uint8_t>(std::min(b, 255)));
        }
        return (base - index) + 3;
    }

    /* Unknown selector: consume just the 38/48 so parsing resynchronises. */
    return 1;
}

void TerminalEmulator::applySgr(const CsiSequence& seq) {
    if (seq.count() == 0) {
        pen_.reset();
        return;
    }

    for (size_t i = 0; i < seq.count(); ++i) {
        const int code = seq.param(i, 0);

        switch (code) {
        case 0:  pen_.reset(); break;
        case 1:  pen_.setFlag(CellFlagBold, true); break;
        case 2:  pen_.setFlag(CellFlagFaint, true); break;
        case 3:  pen_.setFlag(CellFlagItalic, true); break;
        case 4:  pen_.setFlag(CellFlagUnderline, true); break;
        case 5:
        case 6:  pen_.setFlag(CellFlagBlink, true); break;
        case 7:  pen_.setFlag(CellFlagInverse, true); break;
        case 8:  pen_.setFlag(CellFlagInvisible, true); break;
        case 9:  pen_.setFlag(CellFlagStrike, true); break;
        case 21:
        case 22: pen_.setFlag(CellFlagBold | CellFlagFaint, false); break;
        case 23: pen_.setFlag(CellFlagItalic, false); break;
        case 24: pen_.setFlag(CellFlagUnderline, false); break;
        case 25: pen_.setFlag(CellFlagBlink, false); break;
        case 27: pen_.setFlag(CellFlagInverse, false); break;
        case 28: pen_.setFlag(CellFlagInvisible, false); break;
        case 29: pen_.setFlag(CellFlagStrike, false); break;

        case 38: i += parseExtendedColor(seq, i, pen_.fg) - 1; break;
        case 48: i += parseExtendedColor(seq, i, pen_.bg) - 1; break;
        case 39: pen_.fg = Color::defaultColor(); break;
        case 49: pen_.bg = Color::defaultColor(); break;

        default:
            if (code >= 30 && code <= 37) {
                pen_.fg = Color::indexed(static_cast<uint8_t>(code - 30));
            } else if (code >= 40 && code <= 47) {
                pen_.bg = Color::indexed(static_cast<uint8_t>(code - 40));
            } else if (code >= 90 && code <= 97) {
                pen_.fg = Color::indexed(static_cast<uint8_t>(code - 90 + 8));
            } else if (code >= 100 && code <= 107) {
                pen_.bg = Color::indexed(static_cast<uint8_t>(code - 100 + 8));
            }
            break;
        }
    }
}
