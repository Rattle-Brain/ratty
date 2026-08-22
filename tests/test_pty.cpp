/*
 * PTY tests: that a shell actually starts, and that it starts *cleanly*.
 *
 * This suite exists because of a bug it would have caught. Everything the child
 * needs is now built by the parent before the fork, because fork() in a
 * multi-threaded process copies only the calling thread: a lock another thread
 * held at that instant stays locked forever in the child, and the malloc arena
 * lock is enough to hang it before it reaches exec. The parent sees a valid
 * session with a shell that never says anything — a window with a cursor and no
 * prompt. See doc/notable-bugs.md.
 *
 * Two things are therefore pinned down here: that the environment the child gets
 * through execve() is the right one (it is assembled by hand now, so a mistake
 * would silently cost the shell its PATH), and that a fork racing against
 * threads that are hammering the allocator still reaches exec.
 *
 * Every wait is bounded, so a regression reports a failure rather than hanging
 * the suite.
 */

#include "check.h"
#include "core/pty.h"
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

/* Read until `marker` shows up, or the deadline passes. The master is
 * non-blocking, so this polls rather than blocking on read(). */
std::string readUntil(PTY& pty, const std::string& marker, int timeoutMs = 5000) {
    std::string output;
    const auto deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);

    char buffer[4096];
    while (Clock::now() < deadline) {
        const PTY::ReadResult result = pty.read(buffer, sizeof(buffer));
        if (result.bytes > 0) {
            output.append(buffer, static_cast<size_t>(result.bytes));
            if (output.find(marker) != std::string::npos) return output;
            continue;
        }
        if (result.eof || result.error) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return output;
}

void send(PTY& pty, const std::string& line) {
    const std::string bytes = line + "\n";
    pty.write(bytes.data(), bytes.size());
}

/*
 * A pty echoes what is typed at it, so the reply to a command arrives *after* a
 * copy of the command itself. Delimiting the answer with two control characters
 * is what tells them apart: the shell writes them as single bytes, while the
 * echoed command line contains the four characters `\001` that asked for one.
 * Nothing here has to turn the echo off, or guess how long that would take.
 */
constexpr char kOpen = '\001';
constexpr char kClose = '\002';

/* Ask the shell to print one expansion, and return what came back. */
std::string ask(PTY& pty, const std::string& expression, int timeoutMs = 5000) {
    send(pty, "printf '\\001%s\\002\\n' " + expression);

    const std::string output = readUntil(pty, std::string(1, kClose), timeoutMs);
    const size_t end = output.rfind(kClose);
    if (end == std::string::npos) return std::string();
    const size_t start = output.rfind(kOpen, end);
    if (start == std::string::npos) return std::string();
    return output.substr(start + 1, end - start - 1);
}

void testShellStartsAndAnswers() {
    check::section("a shell starts and answers");

    PTY pty(24, 80);
    check::that(pty.isValid(), "the pty was created");
    check::that(pty.childPid() > 0, "and a child was forked");
    if (!pty.isValid()) return;

    /* `uname` is not a builtin, so finding it proves PATH survived into the
     * hand-built environment. */
    check::equal(ask(pty, "\"$(uname >/dev/null 2>&1 && echo ok)\""), std::string("ok"),
                 "the shell ran a command from PATH");
}

void testChildEnvironment() {
    check::section("the environment the child is given");

    PTY pty(24, 80);
    if (!pty.isValid()) {
        check::that(false, "the pty was created");
        return;
    }

    /* Without TERM the shell falls back to "dumb" and stops emitting colour and
     * cursor movement entirely, which is the whole reason it is set. */
    check::equal(ask(pty, "\"$TERM\""), std::string("xterm-256color"), "TERM is set");
    check::equal(ask(pty, "\"$COLORTERM\""), std::string("truecolor"), "COLORTERM is set");

    /* HOME and PATH are not things RaTTY sets, so seeing them proves the
     * inherited environment was copied rather than replaced. */
    check::equal(ask(pty, "\"${HOME:+present}\""), std::string("present"),
                 "and the rest of the environment came with it");
}

void testWorkingDirectory() {
    check::section("the shell starts where it was told to");

    /* Somewhere that exists on both platforms and is not $HOME. */
    PTY pty(24, 80, "/usr");
    if (!pty.isValid()) {
        check::that(false, "the pty was created");
        return;
    }

    check::equal(ask(pty, "\"$(pwd)\""), std::string("/usr"),
                 "the shell is in the directory it was started in");

    /* And the OS agrees, which is what a new split inherits. */
    check::equal(pty.workingDirectory(), std::string("/usr"),
                 "and the operating system says the same");
}

void testDirectoryThatIsGone() {
    check::section("a directory that no longer exists is not fatal");

    PTY pty(24, 80, "/nonexistent-ratty-test-directory");
    check::that(pty.isValid(), "the pane still opens");
    if (!pty.isValid()) return;

    check::equal(ask(pty, "alive"), std::string("alive"), "with a working shell in it");
}

void testForkRacingAgainstThreads() {
    check::section("forking while other threads are allocating");

    /*
     * The regression test for the reason this file exists. The threads below do
     * what Qt, fontconfig and the GL driver are doing during start-up -- holding
     * the allocator lock a good fraction of the time -- and every fork here has
     * to reach exec anyway. Against a child that allocates before exec, this
     * hangs; the timeout turns that into a failure.
     */
    std::atomic<bool> stop{false};
    std::vector<std::thread> threads;
    for (int i = 0; i < 4; ++i) {
        threads.emplace_back([&stop]() {
            while (!stop.load(std::memory_order_relaxed)) {
                std::string churn(512, 'x');
                std::vector<std::string> held;
                for (int n = 0; n < 32; ++n) held.push_back(churn + std::to_string(n));
            }
        });
    }

    int answered = 0;
    constexpr int attempts = 8;
    for (int i = 0; i < attempts; ++i) {
        PTY pty(24, 80);
        if (!pty.isValid()) continue;
        if (ask(pty, "up", 4000) == "up") ++answered;
    }

    stop.store(true, std::memory_order_relaxed);
    for (std::thread& thread : threads) thread.join();

    check::equal(answered, attempts,
                 "every shell reached exec and answered");
}

} // namespace

int main() {
    /*
     * /bin/sh rather than the developer's own login shell: the assertions are
     * about what the *child* was handed, and a personal zsh configuration can
     * print banners, take a second to start, or rewrite PWD in its prompt hook.
     * getUserShell() reads SHELL, so this is the supported way to pin it.
     */
    setenv("SHELL", "/bin/sh", 1);

    testShellStartsAndAnswers();
    testChildEnvironment();
    testWorkingDirectory();
    testDirectoryThatIsGone();
    testForkRacingAgainstThreads();
    return check::report("test_pty");
}
