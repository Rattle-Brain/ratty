/*
 * VTParser - ANSI/VT escape sequence parser
 *
 * A purely *syntactic* state machine modelled on Paul Williams' DEC parser. It
 * knows how escape sequences are shaped and nothing at all about grids,
 * cursors, colours or attributes: it hands whole, well-formed sequences to a
 * VTHandler which supplies the semantics. Previously this class also
 * interpreted SGR and owned the current text attributes, which meant terminal
 * state lived in two places at once.
 *
 * The parser consumes Unicode code points, not bytes, so UTF-8 decoding
 * happens upstream (see Utf8Decoder). All control and escape syntax lives in
 * the ASCII range, so this costs nothing and makes multi-byte text fall out
 * naturally.
 */

#ifndef CORE_VT_PARSER_H
#define CORE_VT_PARSER_H

#include <cstdint>
#include <string>
#include <vector>

/* C0 control characters the parser forwards verbatim to the handler. */
enum : uint8_t {
    C0_BEL = 0x07,
    C0_BS  = 0x08,
    C0_HT  = 0x09,
    C0_LF  = 0x0A,
    C0_VT  = 0x0B,
    C0_FF  = 0x0C,
    C0_CR  = 0x0D,
    C0_SO  = 0x0E,
    C0_SI  = 0x0F,
};

/*
 * A fully parsed CSI sequence: ESC [ <private> <params> <intermediate> <final>
 *
 * Parameters are stored flat. An omitted parameter is kept as `Omitted` so a
 * handler can tell "CSI m" (reset) from "CSI 0 m" where it matters, and apply
 * the correct per-command default otherwise. Sub-parameters (the colon form
 * used by "SGR 38:2:r:g:b") are flagged rather than flattened, so both the
 * colon and semicolon spellings can be honoured.
 */
struct CsiSequence {
    static constexpr int Omitted = -1;
    static constexpr size_t MaxParams = 32;

    std::vector<int> params;
    std::vector<uint8_t> isSubParam;   // parallel to params; 1 == joined with ':'
    char privateMarker = 0;            // '?', '<', '=', '>' or 0
    char intermediate = 0;             // ' ' .. '/' or 0
    char final = 0;

    /* Parameter `index`, or `fallback` when absent/omitted. */
    int param(size_t index, int fallback) const {
        if (index >= params.size() || params[index] == Omitted) return fallback;
        return params[index];
    }
    bool subParam(size_t index) const {
        return index < isSubParam.size() && isSubParam[index] != 0;
    }
    size_t count() const { return params.size(); }
};

/*
 * VTHandler - semantics for parsed sequences.
 *
 * One interface instead of the previous dozen std::function slots: the wiring
 * is a single `setHandler` call and adding a sequence type no longer means
 * touching a setter, a member and a constructor lambda.
 */
class VTHandler {
public:
    virtual ~VTHandler() = default;

    /* A printable code point. */
    virtual void print(char32_t ch) = 0;

    /* A C0 control character (see the enum above). */
    virtual void control(uint8_t code) = 0;

    /* CSI ... final */
    virtual void csiDispatch(const CsiSequence& seq) = 0;

    /* ESC <intermediate> <final>, e.g. ESC ( B  or  ESC 7 */
    virtual void escDispatch(char intermediate, char final) = 0;

    /* OSC <command> ; <data> ST — `command` is -1 when unparseable. */
    virtual void oscDispatch(int command, const std::u32string& data) = 0;
};

class VTParser {
public:
    VTParser() = default;

    void setHandler(VTHandler* handler) { handler_ = handler; }

    /* Feed decoded code points. Safe to call with arbitrary chunk boundaries:
     * parser state persists across calls. */
    void advance(const char32_t* data, size_t length);
    void advance(char32_t ch);

    void reset();

private:
    enum class State {
        Ground,
        Escape,
        EscapeIntermediate,
        CsiEntry,
        CsiParam,
        CsiIntermediate,
        CsiIgnore,
        OscString,
        /* SOS/PM/APC and DCS bodies are consumed and discarded. */
        StringIgnore,
    };

    void clearSequence();
    void pushParamDigit(char32_t ch);
    void pushParamSeparator(bool isColon);
    void dispatchCsi();
    void dispatchOsc();
    bool handleC0(char32_t ch);

    VTHandler* handler_ = nullptr;
    State state_ = State::Ground;

    CsiSequence seq_;
    bool paramStarted_ = false;
    std::u32string oscBuffer_;
    /* Set when an OSC/string body saw ESC and is waiting for the '\' of ST. */
    bool stringEscPending_ = false;
};

#endif /* CORE_VT_PARSER_H */
