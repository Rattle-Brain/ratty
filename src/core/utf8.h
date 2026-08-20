/*
 * UTF-8 helpers
 *
 * Utf8Decoder exists because PTY reads land on arbitrary byte boundaries: a
 * 4 KiB read can end halfway through a multi-byte character. The previous code
 * called QString::fromUtf8() on each chunk, which turned every split sequence
 * into replacement characters. The decoder keeps the partial sequence and
 * resumes with the next chunk.
 */

#ifndef CORE_UTF8_H
#define CORE_UTF8_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

class Utf8Decoder {
public:
    /* Decode `length` bytes, appending complete code points to `out`. Any
     * trailing incomplete sequence is retained for the next call. */
    void decode(const char* data, size_t length, std::vector<char32_t>& out) {
        size_t i = 0;
        while (i < length) {
            const uint8_t byte = static_cast<uint8_t>(data[i]);
            ++i;

            if (remaining_ == 0) {
                if (byte < 0x80) {
                    out.push_back(byte);
                } else if ((byte & 0xE0) == 0xC0) {
                    codepoint_ = byte & 0x1Fu; remaining_ = 1; minimum_ = 0x80;
                } else if ((byte & 0xF0) == 0xE0) {
                    codepoint_ = byte & 0x0Fu; remaining_ = 2; minimum_ = 0x800;
                } else if ((byte & 0xF8) == 0xF0) {
                    codepoint_ = byte & 0x07u; remaining_ = 3; minimum_ = 0x10000;
                } else {
                    /* Stray continuation byte or invalid lead byte. */
                    out.push_back(kReplacement);
                }
                continue;
            }

            if ((byte & 0xC0) != 0x80) {
                /* Truncated sequence: emit a replacement for the abandoned
                 * sequence and retry this byte as a fresh lead byte. */
                out.push_back(kReplacement);
                remaining_ = 0;
                --i;
                continue;
            }

            codepoint_ = (codepoint_ << 6) | (byte & 0x3Fu);
            if (--remaining_ == 0) {
                const bool overlong = codepoint_ < minimum_;
                const bool surrogate = codepoint_ >= 0xD800 && codepoint_ <= 0xDFFF;
                const bool tooLarge = codepoint_ > 0x10FFFF;
                out.push_back((overlong || surrogate || tooLarge) ? kReplacement
                                                                 : static_cast<char32_t>(codepoint_));
            }
        }
    }

    void reset() { remaining_ = 0; codepoint_ = 0; minimum_ = 0; }

private:
    static constexpr char32_t kReplacement = 0xFFFD;

    uint32_t codepoint_ = 0;
    uint32_t minimum_ = 0;
    int remaining_ = 0;
};

/* Encode a code point sequence as UTF-8 (used for window titles). */
inline std::string utf8Encode(const std::u32string& text) {
    std::string out;
    out.reserve(text.size());
    for (char32_t ch : text) {
        const uint32_t cp = static_cast<uint32_t>(ch);
        if (cp < 0x80) {
            out.push_back(static_cast<char>(cp));
        } else if (cp < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }
    return out;
}

#endif /* CORE_UTF8_H */
