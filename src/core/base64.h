/*
 * base64 - the one encoding the terminal protocol needs
 *
 * OSC 52 carries clipboard text base64-encoded, in both directions, and that is
 * the only place RaTTY needs base64 at all. Kept here as a pair of free
 * functions rather than reached for through Qt, so that the OSC handling stays
 * in core/ with the rest of the wire formats -- the emulator is the layer that
 * knows what an escape sequence *means*, and "the payload is base64" is part of
 * that meaning.
 *
 * Decoding is deliberately strict about length and alphabet but tolerant of
 * missing padding and of embedded whitespace, which is what real senders emit:
 * tmux pads, some editors do not, and a long payload may arrive wrapped.
 */

#ifndef CORE_BASE64_H
#define CORE_BASE64_H

#include <cstdint>
#include <string>

inline std::string base64Encode(const std::string& input) {
    static const char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    std::string out;
    out.reserve(((input.size() + 2) / 3) * 4);

    size_t i = 0;
    while (i + 2 < input.size()) {
        const uint32_t triple = (static_cast<uint8_t>(input[i]) << 16)
                              | (static_cast<uint8_t>(input[i + 1]) << 8)
                              | static_cast<uint8_t>(input[i + 2]);
        out += alphabet[(triple >> 18) & 0x3F];
        out += alphabet[(triple >> 12) & 0x3F];
        out += alphabet[(triple >> 6) & 0x3F];
        out += alphabet[triple & 0x3F];
        i += 3;
    }

    if (i < input.size()) {
        const size_t remaining = input.size() - i;
        uint32_t triple = static_cast<uint32_t>(static_cast<uint8_t>(input[i])) << 16;
        if (remaining == 2) {
            triple |= static_cast<uint32_t>(static_cast<uint8_t>(input[i + 1])) << 8;
        }
        out += alphabet[(triple >> 18) & 0x3F];
        out += alphabet[(triple >> 12) & 0x3F];
        out += remaining == 2 ? alphabet[(triple >> 6) & 0x3F] : '=';
        out += '=';
    }

    return out;
}

/*
 * Decode into `out`. False -- with `out` left empty -- when the input is not
 * base64 at all, which is how a mangled OSC 52 payload is ignored rather than
 * pasted as rubbish.
 */
inline bool base64Decode(const std::string& input, std::string& out) {
    out.clear();
    out.reserve(input.size() / 4 * 3);

    uint32_t accumulator = 0;
    int bits = 0;

    for (const char c : input) {
        int value = -1;
        if (c >= 'A' && c <= 'Z')      value = c - 'A';
        else if (c >= 'a' && c <= 'z') value = c - 'a' + 26;
        else if (c >= '0' && c <= '9') value = c - '0' + 52;
        else if (c == '+')             value = 62;
        else if (c == '/')             value = 63;
        else if (c == '=')             break;                 // padding ends the data
        else if (c == '\n' || c == '\r' || c == ' ' || c == '\t') continue;
        else {
            out.clear();
            return false;
        }

        accumulator = (accumulator << 6) | static_cast<uint32_t>(value);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out += static_cast<char>((accumulator >> bits) & 0xFF);
        }
    }

    /* Leftover bits are the padding a sender omitted, and must be zero: a
     * non-zero tail means the payload was truncated mid-character. */
    if (bits > 0 && (accumulator & ((1u << bits) - 1)) != 0) {
        out.clear();
        return false;
    }
    return true;
}

#endif /* CORE_BASE64_H */
