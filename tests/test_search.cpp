/*
 * Scrollback search tests.
 *
 * Searching a terminal buffer is not searching a document: a row is a slice of
 * the window, so a command long enough to wrap is spread across two rows with no
 * separator between them, while two rows that merely follow each other are
 * separate lines a match must not run across. That distinction -- and the
 * base64 the clipboard protocol is carried in, tested here for the same reason:
 * it is a pure function with an exact answer -- is what this suite pins down.
 */

#include "check.h"
#include "core/base64.h"
#include "core/search.h"
#include "core/terminal_emulator.h"
#include <string>

namespace {

void feed(TerminalEmulator& term, const std::string& bytes) {
    term.write(bytes.data(), bytes.size());
}

std::u32string wide(const std::string& text) {
    std::u32string out;
    for (const char c : text) out += static_cast<char32_t>(c);
    return out;
}

std::string narrow(const std::u32string& text) {
    std::string out;
    for (const char32_t ch : text) {
        if (ch == U'\n') out += "\\n";
        else if (ch >= 32 && ch < 127) out += static_cast<char>(ch);
        else out += '?';
    }
    return out;
}

/* The text of a match, which is how a match is checked: the range is only
 * interesting in so far as it covers the right cells. */
std::string matchText(const Screen& screen, const SelectionRange& range) {
    return narrow(selectionText(screen, range, SelectionMode::Character));
}

void testFindsMatchesOldestFirst() {
    check::section("matches come back oldest first");

    TerminalEmulator term(3, 20);
    feed(term, "alpha needle\r\nbeta\r\nneedle gamma\r\nneedle again\r\n");

    const SearchResults results = searchScrollback(term.screen(), wide("needle"));
    check::equal(results.matches.size(), size_t{3}, "three matches");
    check::that(!results.truncated, "and nothing was dropped");
    check::that(results.matches[0].start.line < results.matches[1].start.line,
                "reported in buffer order");
    for (const SelectionRange& match : results.matches) {
        check::equal(matchText(term.screen(), match), std::string("needle"),
                     "each match covers exactly the needle");
    }
}

void testMatchesAcrossAWrapSeam() {
    check::section("a match spanning a wrap is still found");

    TerminalEmulator term(4, 8);
    feed(term, "xx needleyy");   // "needle" is split by the margin

    const SearchResults results = searchScrollback(term.screen(), wide("needle"));
    check::equal(results.matches.size(), size_t{1}, "found across the seam");
    if (results.matches.empty()) return;
    check::that(results.matches[0].start.line != results.matches[0].end.line,
                "and the match really does span two rows");
    check::equal(matchText(term.screen(), results.matches[0]), std::string("needle"),
                 "with the seam contributing no newline");
}

void testDoesNotMatchAcrossALineBreak() {
    check::section("a match does not run across a real line break");

    TerminalEmulator term(4, 10);
    feed(term, "need\r\nle\r\n");
    const SearchResults results = searchScrollback(term.screen(), wide("needle"));
    check::equal(results.matches.size(), size_t{0}, "two lines are two lines");
}

void testCaseFolding() {
    check::section("case");

    TerminalEmulator term(3, 20);
    feed(term, "Makefile\r\n");

    check::equal(searchScrollback(term.screen(), wide("makefile")).matches.size(), size_t{1},
                 "insensitive by default");
    check::equal(searchScrollback(term.screen(), wide("makefile"), true).matches.size(), size_t{0},
                 "and exact when asked");
    check::equal(searchScrollback(term.screen(), wide("Makefile"), true).matches.size(), size_t{1},
                 "which still finds the real spelling");
}

void testSearchesTheHistory() {
    check::section("the history is searched, not just the screen");

    TerminalEmulator term(2, 12);
    feed(term, "needle here\r\nfiller\r\nfiller\r\nfiller\r\n");
    check::that(term.historySize() >= 2, "the match has scrolled off the screen");

    const SearchResults results = searchScrollback(term.screen(), wide("needle"));
    check::equal(results.matches.size(), size_t{1}, "and is still found");
    if (results.matches.empty()) return;
    check::that(results.matches[0].start.line < term.screen().screenTopLine(),
                "at a line above the live screen");
}

void testEmptyAndMissing() {
    check::section("degenerate searches");

    TerminalEmulator term(3, 10);
    feed(term, "content\r\n");
    check::equal(searchScrollback(term.screen(), wide("")).matches.size(), size_t{0},
                 "an empty needle matches nothing");
    check::equal(searchScrollback(term.screen(), wide("absent")).matches.size(), size_t{0},
                 "and so does a needle that is not there");
}

void testMatchLimitIsReported() {
    check::section("the match cap is reported rather than swallowed");

    TerminalEmulator term(4, 20);
    for (int i = 0; i < 10; ++i) feed(term, "xx\r\n");

    const SearchResults capped = searchScrollback(term.screen(), wide("x"), false, 3);
    check::equal(capped.matches.size(), size_t{3}, "the cap holds");
    check::that(capped.truncated, "and says so");
}

void testWideCharacterNeedle() {
    check::section("a double-width character is matched by its own code point");

    TerminalEmulator term(3, 10);
    feed(term, "a\xe6\x97\xa5" "b\r\n");   // a, U+65E5, b

    std::u32string needle;
    needle += static_cast<char32_t>(0x65E5);
    const SearchResults results = searchScrollback(term.screen(), needle);
    check::equal(results.matches.size(), size_t{1}, "found");
    if (results.matches.empty()) return;
    /* The match covers both columns of the character, so the highlight does. */
    check::equal(results.matches[0].end.col - results.matches[0].start.col, 1,
                 "and covers both of its columns");
}

void testBase64RoundTrip() {
    check::section("base64, as OSC 52 carries it");

    const std::string cases[] = {"", "a", "ab", "abc", "abcd",
                                 "hello, world", "line\nbreak\ttab"};
    for (const std::string& input : cases) {
        std::string decoded;
        /* The label escapes the input: a control character printed raw would
         * break the report's own layout. */
        std::string label;
        for (const char c : input) {
            if (c == '\n')      label += "\\n";
            else if (c == '\t') label += "\\t";
            else                label += c;
        }
        check::that(base64Decode(base64Encode(input), decoded) && decoded == input,
                    "round trip: \"" + label + "\"");
    }

    check::equal(base64Encode("ratty"), std::string("cmF0dHk="), "a known encoding");
    std::string decoded;
    check::that(base64Decode("cmF0dHk=", decoded) && decoded == "ratty", "and its decoding");
    /* Padding is optional, whitespace is ignored, rubbish is refused. */
    check::that(base64Decode("cmF0dHk", decoded) && decoded == "ratty",
                "missing padding is tolerated");
    check::that(base64Decode("cmF0\ndHk=", decoded) && decoded == "ratty",
                "a wrapped payload is tolerated");
    check::that(!base64Decode("not base64!", decoded), "and a mangled one is refused");
}

void testOsc52SetsAndReadsTheClipboard() {
    check::section("OSC 52");

    TerminalEmulator term(4, 20);
    std::string written;
    char writtenTo = 0;
    std::string reply;

    term.setReplySink([&reply](const std::string& text) { reply += text; });
    term.setClipboardWriter([&](char which, const std::string& text) {
        writtenTo = which;
        written = text;
    });

    feed(term, "\x1b]52;c;cmF0dHk=\x1b\\");
    check::equal(written, std::string("ratty"), "a set request decodes to the text");
    check::equal(std::string(1, writtenTo), std::string("c"), "for the selection it named");

    /* An empty selection name means the clipboard. */
    feed(term, "\x1b]52;;aGk=\x1b\\");
    check::equal(written, std::string("hi"), "an unnamed selection is the clipboard");
    check::equal(std::string(1, writtenTo), std::string("c"), "and is spelled as one");

    /* A query goes unanswered while no reader is installed -- which is the
     * default, because letting the far end of a pty read the clipboard is a
     * hazard rather than a feature. */
    feed(term, "\x1b]52;c;?\x1b\\");
    check::equal(reply, std::string(""), "a query is ignored when reading is not allowed");

    term.setClipboardReader([](char, std::string& out) {
        out = "ratty";
        return true;
    });
    feed(term, "\x1b]52;c;?\x1b\\");
    check::equal(reply, std::string("\x1b]52;c;cmF0dHk=\x1b\\"),
                 "and answered in the same form once it is");

    /* A mangled payload leaves the clipboard alone. */
    written.clear();
    feed(term, "\x1b]52;c;!!!!\x1b\\");
    check::equal(written, std::string(""), "a payload that is not base64 is dropped");
}

} // namespace

int main() {
    testFindsMatchesOldestFirst();
    testMatchesAcrossAWrapSeam();
    testDoesNotMatchAcrossALineBreak();
    testCaseFolding();
    testSearchesTheHistory();
    testEmptyAndMissing();
    testMatchLimitIsReported();
    testWideCharacterNeedle();
    testBase64RoundTrip();
    testOsc52SetsAndReadsTheClipboard();
    return check::report("test_search");
}
