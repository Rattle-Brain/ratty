/*
 * PTY - RAII wrapper around a pseudo-terminal and its shell process
 *
 * Spawns the user's shell (from $SHELL, else the password database, else
 * /bin/sh) on the slave side of a new pty and hands back a non-blocking master
 * descriptor suitable for a Qt socket notifier.
 *
 * Author: Rattle-Brain
 */

#ifndef CORE_PTY_H
#define CORE_PTY_H

#include <pwd.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>
#include <string>

#if defined(__linux__)
  #include <pty.h>
#elif defined(__APPLE__) || defined(__FreeBSD__)
  #include <util.h>
#endif

class PTY {
public:
    /* Outcome of a read. The three non-data cases used to be conflated into a
     * single 0/-1 return, which made "no data right now" indistinguishable
     * from "the shell exited". */
    struct ReadResult {
        ssize_t bytes = 0;
        bool wouldBlock = false;
        bool eof = false;
        bool error = false;
    };

    /*
     * `workingDirectory` is where the shell starts. Empty inherits RaTTY's own
     * directory, which is what happened before this existed and is almost never
     * what anyone wants: for a terminal launched from a desktop entry it is `/`.
     * A directory that cannot be entered is not fatal -- the shell starts in the
     * inherited one rather than not at all.
     */
    PTY(int rows, int cols, const std::string& workingDirectory = std::string());
    ~PTY();

    PTY(const PTY&) = delete;
    PTY& operator=(const PTY&) = delete;
    PTY(PTY&& other) noexcept;
    PTY& operator=(PTY&& other) noexcept;

    ReadResult read(char* buf, size_t len);
    ssize_t write(const char* buf, size_t len);

    /* Push the new window size to the slave and signal the foreground group. */
    void resize(int rows, int cols);

    int masterFd() const { return master_fd_; }
    pid_t childPid() const { return child_pid_; }
    int rows() const { return rows_; }
    int cols() const { return cols_; }

    bool isValid() const { return master_fd_ >= 0 && child_pid_ > 0; }

    /*
     * The directory the shell is in *now*, or empty if it cannot be determined.
     *
     * Read from the operating system rather than tracked from the byte stream:
     * OSC 7 would require the shell to be configured to report itself, which
     * bash is not by default, whereas /proc (Linux) and proc_pidinfo (macOS)
     * answer for any shell. This is what lets a new split open where the pane it
     * came from is.
     */
    std::string workingDirectory() const;

    /* Reaps the child if it has exited. Once true, stays true: the previous
     * implementation called waitpid() from a const method on every poll, so the
     * first call consumed the exit status and later cleanup could not reap. */
    bool hasChildExited();

    static std::string getUserShell();

private:
    void cleanup();

    /*
     * Everything the child execs, built *entirely* by the parent before the
     * fork. Every member points into storage the parent owns and the child only
     * reads; see the note in execChild() for why not one byte of this may be
     * assembled on the other side of the fork.
     */
    struct ChildImage {
        const char* shell = nullptr;
        /* argv for a login shell ("-zsh") and for a plain one ("zsh"). */
        char* const* loginArgv = nullptr;
        char* const* plainArgv = nullptr;
        /* The complete environment, TERM and friends already in it. */
        char* const* envp = nullptr;
        /* Where to chdir first, or null to stay where we are. */
        const char* workingDirectory = nullptr;
    };

    /*
     * Runs in the forked child and never returns. Calls nothing that is not
     * async-signal-safe -- which is the whole reason ChildImage exists.
     */
    [[noreturn]] static void execChild(const ChildImage& image);

    int master_fd_ = -1;
    pid_t child_pid_ = -1;
    int rows_ = 0;
    int cols_ = 0;
    bool child_exited_ = false;
};

#endif /* CORE_PTY_H */
