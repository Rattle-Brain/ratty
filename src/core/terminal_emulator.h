/*
 * TerminalEmulator - VT semantics on top of a Screen
 *
 * Layering: PTY bytes -> Utf8Decoder -> VTParser (syntax) -> TerminalEmulator
 * (semantics) -> Screen (state). This class is the only place that knows what
 * "CSI 2 J" or "SGR 38;5;208" *mean*; the parser knows only their shape and the
 * screen only how to store the result.
 *
 * It owns two screens so that the alternate buffer (DECSET 1049, used by vim,
 * less, htop...) does not destroy the shell's scrollback view, and it owns the
 * pen (current graphic rendition) that printed characters inherit.
 */

#ifndef CORE_TERMINAL_EMULATOR_H
#define CORE_TERMINAL_EMULATOR_H

#include "cell.h"
#include "cursor.h"
#include "mouse.h"
#include "palette.h"
#include "screen.h"
#include "utf8.h"
#include "vt_parser.h"
#include <functional>
#include <string>
#include <vector>

class TerminalEmulator : public VTHandler {
public:
    TerminalEmulator(int rows, int cols);
    ~TerminalEmulator() override = default;

    TerminalEmulator(const TerminalEmulator&) = delete;
    TerminalEmulator& operator=(const TerminalEmulator&) = delete;

    /* Feed raw PTY bytes. UTF-8 sequences may be split across calls. */
    void write(const char* data, size_t length);

    /* The screen currently being displayed (primary or alternate). */
    const Screen& screen() const { return *active_; }

    int rows() const { return active_->rows(); }
    int cols() const { return active_->cols(); }
    void resize(int rows, int cols);
    void reset();

    /*
     * Colours are owned per session, not globally, because OSC 4/10/11/12 let a
     * running application retheme *its own* terminal -- one pane changing its
     * background must not disturb another. `base` supplies the configured
     * defaults and is what OSC 104/110/111/112 restore to.
     */
    void setBasePalette(const Palette& base);
    const Palette& palette() const { return palette_; }

    /*
     * Cursor shape requested by the application through DECSCUSR (CSI n q).
     * Empty until an application asks, so the user's configured style wins by
     * default. Editors set this per mode -- a bar in insert mode, a block in
     * normal mode -- and ignoring it made the cursor look stuck.
     */
    bool hasRequestedCursorStyle() const { return cursorStyleRequested_; }
    CursorStyle requestedCursorStyle() const { return requestedCursorStyle_; }
    bool cursorBlinkRequested() const { return cursorBlinkRequested_; }

    /* Where the emulator wants replies sent (DSR, DA, ...). */
    using ReplySink = std::function<void(const std::string& utf8)>;
    void setReplySink(ReplySink sink) { reply_ = std::move(sink); }

    /* Notifications for the UI layer. Titles are UTF-8. */
    using TitleSink = std::function<void(const std::string& utf8)>;
    using BellSink = std::function<void()>;
    void setTitleSink(TitleSink sink) { titleSink_ = std::move(sink); }
    void setBellSink(BellSink sink) { bellSink_ = std::move(sink); }

    /* ----------------------------------------------------------- scrollback */

    /*
     * Rows of history kept for the primary screen. The alternate screen never
     * keeps any: `less` and `vim` repaint their whole window, so every scroll
     * there would deposit a screenful of redrawn text.
     */
    void setScrollbackLines(int lines);
    int scrollbackLines() const { return primary_.historyLimit(); }
    int historySize() const { return active_->historySize(); }

    /* View offset in rows; 0 is the live screen. `lines` is positive towards
     * the past. All of these return true when the view actually moved. */
    bool scrollViewBy(int lines);
    bool scrollViewToBottom();
    bool scrollViewToTop();
    int viewOffset() const { return active_->viewOffset(); }
    bool scrolledBack() const { return active_->scrolledBack(); }
    void clearScrollback();

    bool alternateScreenActive() const { return alternateActive_; }

    /* ------------------------------------------------------------- mouse */

    MouseTracking mouseTracking() const { return mouseTracking_; }
    MouseEncoding mouseEncoding() const { return mouseEncoding_; }
    /* DECSET 1004: the application wants CSI I / CSI O when the window gains
     * or loses focus. */
    bool focusEvents() const { return focusEvents_; }
    /*
     * DECSET 1007. With no mouse tracking and the alternate screen up, a wheel
     * notch is translated into cursor keys so that `less` and `man` scroll --
     * without it the wheel does nothing at all in a pager, which reads as a bug.
     * The configured value is the starting point; an application may turn it off.
     */
    bool alternateScroll() const { return alternateScroll_; }
    void setAlternateScroll(bool enable) { alternateScroll_ = enable; }

    /* True while an application has requested bracketed paste (DECSET 2004). */
    bool bracketedPaste() const { return bracketedPaste_; }
    /* True while the cursor keys should send SS3 rather than CSI (DECCKM). */
    bool applicationCursorKeys() const { return applicationCursorKeys_; }

    /* VTHandler */
    void print(char32_t ch) override;
    void control(uint8_t code) override;
    void csiDispatch(const CsiSequence& seq) override;
    void escDispatch(char intermediate, char final) override;
    void oscDispatch(int command, const std::u32string& data) override;

private:
    /*
     * Grapheme clustering for emoji sequences. Terminals receive a cluster one
     * code point at a time, and only the whole sequence says how wide the cell
     * is and whether it is a colour emoji:
     *
     *   U+26A0 U+FE0F               warning sign -> double-width colour emoji
     *   U+1F44D U+1F3FD             thumbs up + skin tone, still one cell
     *   U+1F468 U+200D U+1F4BB      man + joiner + laptop, still one cell
     *   U+1F1EA U+1F1F8             two regional indicators -> one flag
     *   U+0031 U+FE0F U+20E3        keycap 1
     *
     * Returns true when `ch` continued the previous cell instead of starting a
     * new one.
     */
    bool continueCluster(char32_t ch);
    void beginCluster(char32_t ch);

    void applySgr(const CsiSequence& seq);
    /* Parses one SGR extended-colour spec starting at `index`; returns the
     * number of parameters consumed. */
    size_t parseExtendedColor(const CsiSequence& seq, size_t index, Color& out);
    void setMode(const CsiSequence& seq, bool enable);
    void setCursorStyle(int parameter);
    /* OSC 4/5 palette control. */
    void handlePaletteOsc(const std::u32string& data, bool reset);
    /* OSC 10/11/12 default colour control; `which` is the OSC number. */
    void handleDynamicColorOsc(int which, const std::u32string& data);
    void resetDynamicColor(int which);
    /* Set or clear one tracking / encoding mode; see the implementation for why
     * clearing is conditional. */
    void setMouseTracking(MouseTracking mode, bool enable);
    void setMouseEncoding(MouseEncoding mode, bool enable);
    void useAlternateScreen(bool enable);
    void deviceStatusReport(const CsiSequence& seq);
    void sendReply(const std::string& text);

    Screen primary_;
    Screen alternate_;
    Screen* active_;

    Pen pen_;
    Pen savedPen_;

    Palette basePalette_;   // the configured colours; the reset target
    Palette palette_;       // live colours, mutated by OSC

    bool cursorStyleRequested_ = false;
    CursorStyle requestedCursorStyle_ = CursorStyle::Block;
    bool cursorBlinkRequested_ = true;

    VTParser parser_;
    Utf8Decoder decoder_;
    std::vector<char32_t> scratch_;

    /*
     * Cluster state. `awaitingJoinedBase_` is set by a zero-width joiner: the
     * next base code point belongs to the cell already on screen rather than to
     * a new one, which is what keeps a ZWJ sequence in two columns instead of
     * four.
     */
    bool awaitingJoinedBase_ = false;
    bool clusterIsEmoji_ = false;
    int regionalIndicatorCount_ = 0;

    bool alternateActive_ = false;
    bool bracketedPaste_ = false;

    MouseTracking mouseTracking_ = MouseTracking::None;
    MouseEncoding mouseEncoding_ = MouseEncoding::X10;
    bool focusEvents_ = false;
    bool alternateScroll_ = true;

    bool applicationCursorKeys_ = false;
    /* DECSET 20 / LNM: when set, LF also performs a carriage return. */
    bool newlineMode_ = false;

    ReplySink reply_;
    TitleSink titleSink_;
    BellSink bellSink_;
};

#endif /* CORE_TERMINAL_EMULATOR_H */
