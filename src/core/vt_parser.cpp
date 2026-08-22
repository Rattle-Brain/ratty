/*
 * VTParser - ANSI/VT escape sequence parser implementation
 */

#include "vt_parser.h"

namespace {

/*
 * How much of an OSC string body is kept. A title needs a few dozen characters;
 * the bound exists for OSC 52, whose payload is a base64 clipboard and can be
 * an entire selection. Past this the tail is dropped -- a truncated base64
 * payload fails to decode and is ignored, which is the right outcome for a
 * clipboard that did not arrive whole.
 */
constexpr size_t kMaxOscLength = 64 * 1024;

constexpr char32_t kEsc = 0x1B;
constexpr char32_t kCan = 0x18;   // CAN - abort the current sequence
constexpr char32_t kSub = 0x1A;   // SUB - as CAN, but should display an error glyph
constexpr char32_t kDel = 0x7F;

bool isCsiPrivateMarker(char32_t ch) {
    return ch == U'<' || ch == U'=' || ch == U'>' || ch == U'?';
}

/* ECMA-48 intermediate bytes: 0x20-0x2F */
bool isIntermediate(char32_t ch) {
    return ch >= 0x20 && ch <= 0x2F;
}

/* ECMA-48 final bytes: 0x40-0x7E */
bool isFinal(char32_t ch) {
    return ch >= 0x40 && ch <= 0x7E;
}

bool isParamByte(char32_t ch) {
    return (ch >= U'0' && ch <= U'9') || ch == U';' || ch == U':';
}

} // namespace

void VTParser::reset() {
    state_ = State::Ground;
    clearSequence();
    oscBuffer_.clear();
    stringEscPending_ = false;
}

void VTParser::clearSequence() {
    seq_.params.clear();
    seq_.isSubParam.clear();
    seq_.privateMarker = 0;
    seq_.intermediate = 0;
    seq_.final = 0;
    paramStarted_ = false;
}

void VTParser::advance(const char32_t* data, size_t length) {
    for (size_t i = 0; i < length; ++i) {
        advance(data[i]);
    }
}

void VTParser::pushParamDigit(char32_t ch) {
    const int digit = static_cast<int>(ch - U'0');

    if (!paramStarted_) {
        if (seq_.params.size() >= CsiSequence::MaxParams) return;
        seq_.params.push_back(0);
        seq_.isSubParam.push_back(0);
        paramStarted_ = true;
    }

    int& value = seq_.params.back();
    if (value == CsiSequence::Omitted) value = 0;
    /* Clamp instead of overflowing: a malformed 20-digit parameter must not
     * produce undefined behaviour. */
    if (value < 100000) {
        value = value * 10 + digit;
    }
}

void VTParser::pushParamSeparator(bool isColon) {
    if (!paramStarted_) {
        /* Two separators in a row, or a leading one: record an omitted param. */
        if (seq_.params.size() < CsiSequence::MaxParams) {
            seq_.params.push_back(CsiSequence::Omitted);
            seq_.isSubParam.push_back(0);
        }
    }
    paramStarted_ = false;

    /* The *next* parameter inherits the separator kind. Encoded by pre-seeding
     * the sub-param flag when the following digit arrives. */
    if (isColon && seq_.params.size() < CsiSequence::MaxParams) {
        seq_.params.push_back(CsiSequence::Omitted);
        seq_.isSubParam.push_back(1);
        paramStarted_ = true;
    }
}

bool VTParser::handleC0(char32_t ch) {
    if (ch >= 0x20) return false;

    switch (ch) {
    case kEsc:
        clearSequence();
        state_ = State::Escape;
        return true;
    case kCan:
    case kSub:
        state_ = State::Ground;
        clearSequence();
        return true;
    default:
        break;
    }

    /* Control characters are executed immediately even in the middle of an
     * escape sequence — that is what real terminals do, and it keeps a stray
     * ESC from swallowing a newline. */
    if (handler_) {
        handler_->control(static_cast<uint8_t>(ch));
    }
    return true;
}

void VTParser::advance(char32_t ch) {
    if (!handler_) return;

    switch (state_) {
    case State::Ground:
        if (handleC0(ch)) return;
        if (ch == kDel) return;
        handler_->print(ch);
        return;

    case State::Escape:
        if (ch < 0x20) {
            /* CAN/SUB/ESC handled here; other controls execute and we stay. */
            if (ch == kEsc || ch == kCan || ch == kSub) { handleC0(ch); return; }
            handler_->control(static_cast<uint8_t>(ch));
            return;
        }
        if (ch == U'[') { clearSequence(); state_ = State::CsiEntry; return; }
        if (ch == U']') { oscBuffer_.clear(); stringEscPending_ = false; state_ = State::OscString; return; }
        if (ch == U'P' || ch == U'X' || ch == U'^' || ch == U'_') {
            /* DCS / SOS / PM / APC: consume the body, act on none of it. */
            stringEscPending_ = false;
            state_ = State::StringIgnore;
            return;
        }
        if (isIntermediate(ch)) {
            seq_.intermediate = static_cast<char>(ch);
            state_ = State::EscapeIntermediate;
            return;
        }
        state_ = State::Ground;
        handler_->escDispatch(0, static_cast<char>(ch));
        return;

    case State::EscapeIntermediate:
        if (ch < 0x20) { handleC0(ch); return; }
        if (isIntermediate(ch)) {
            /* Keep only the last intermediate; no sequence we act on uses two. */
            seq_.intermediate = static_cast<char>(ch);
            return;
        }
        state_ = State::Ground;
        handler_->escDispatch(seq_.intermediate, static_cast<char>(ch));
        return;

    case State::CsiEntry:
        if (ch < 0x20) { handleC0(ch); return; }
        if (isCsiPrivateMarker(ch)) {
            seq_.privateMarker = static_cast<char>(ch);
            state_ = State::CsiParam;
            return;
        }
        if (isParamByte(ch)) {
            state_ = State::CsiParam;
            advance(ch);
            return;
        }
        if (isIntermediate(ch)) {
            seq_.intermediate = static_cast<char>(ch);
            state_ = State::CsiIntermediate;
            return;
        }
        if (isFinal(ch)) {
            seq_.final = static_cast<char>(ch);
            dispatchCsi();
            return;
        }
        state_ = State::CsiIgnore;
        return;

    case State::CsiParam:
        if (ch < 0x20) { handleC0(ch); return; }
        if (ch >= U'0' && ch <= U'9') { pushParamDigit(ch); return; }
        if (ch == U';') { pushParamSeparator(false); return; }
        if (ch == U':') { pushParamSeparator(true); return; }
        if (isCsiPrivateMarker(ch)) {
            /* A private marker after parameters is malformed. */
            state_ = State::CsiIgnore;
            return;
        }
        if (isIntermediate(ch)) {
            seq_.intermediate = static_cast<char>(ch);
            state_ = State::CsiIntermediate;
            return;
        }
        if (isFinal(ch)) {
            seq_.final = static_cast<char>(ch);
            dispatchCsi();
            return;
        }
        state_ = State::CsiIgnore;
        return;

    case State::CsiIntermediate:
        if (ch < 0x20) { handleC0(ch); return; }
        if (isIntermediate(ch)) { seq_.intermediate = static_cast<char>(ch); return; }
        if (isFinal(ch)) {
            seq_.final = static_cast<char>(ch);
            dispatchCsi();
            return;
        }
        state_ = State::CsiIgnore;
        return;

    case State::CsiIgnore:
        if (ch < 0x20) { handleC0(ch); return; }
        if (isFinal(ch)) {
            state_ = State::Ground;
            clearSequence();
        }
        return;

    case State::OscString:
    case State::StringIgnore: {
        /*
         * String bodies end at BEL or at ST (ESC '\'). The old parser dropped
         * out of the OSC state on the ESC and then *printed* the following
         * backslash: zsh emits "ESC ] 7 ; file://... ESC \" before every
         * prompt, so a stray '\' was being written into the grid.
         */
        if (stringEscPending_) {
            stringEscPending_ = false;
            if (ch == U'\\') {
                if (state_ == State::OscString) dispatchOsc();
                state_ = State::Ground;
                oscBuffer_.clear();
                return;
            }
            /* ESC followed by anything else: abandon the string and re-handle
             * the byte as the start of a fresh escape sequence. */
            state_ = State::Escape;
            oscBuffer_.clear();
            clearSequence();
            advance(ch);
            return;
        }
        if (ch == kEsc) { stringEscPending_ = true; return; }
        if (ch == C0_BEL) {
            if (state_ == State::OscString) dispatchOsc();
            state_ = State::Ground;
            oscBuffer_.clear();
            return;
        }
        if (ch == kCan || ch == kSub) {
            state_ = State::Ground;
            oscBuffer_.clear();
            return;
        }
        if (state_ == State::OscString && oscBuffer_.size() < kMaxOscLength) {
            oscBuffer_.push_back(ch);
        }
        return;
    }
    }
}

void VTParser::dispatchCsi() {
    handler_->csiDispatch(seq_);
    clearSequence();
    state_ = State::Ground;
}

void VTParser::dispatchOsc() {
    /* Split "<number>;<payload>". */
    size_t sep = oscBuffer_.find(U';');
    int command = -1;
    std::u32string data;

    if (sep == std::u32string::npos) {
        sep = oscBuffer_.size();
    }

    bool numeric = sep > 0;
    long value = 0;
    for (size_t i = 0; i < sep; ++i) {
        const char32_t c = oscBuffer_[i];
        if (c < U'0' || c > U'9') { numeric = false; break; }
        value = value * 10 + static_cast<long>(c - U'0');
        if (value > 100000) { numeric = false; break; }
    }
    if (numeric) {
        command = static_cast<int>(value);
        if (sep < oscBuffer_.size()) data = oscBuffer_.substr(sep + 1);
    } else {
        data = oscBuffer_;
    }

    handler_->oscDispatch(command, data);
}
