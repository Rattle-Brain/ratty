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

    PTY(int rows, int cols);
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

    /* Reaps the child if it has exited. Once true, stays true: the previous
     * implementation called waitpid() from a const method on every poll, so the
     * first call consumed the exit status and later cleanup could not reap. */
    bool hasChildExited();

    static std::string getUserShell();

private:
    void cleanup();
    /* Runs in the forked child; never returns. */
    [[noreturn]] void execChild(const std::string& shell);

    int master_fd_ = -1;
    pid_t child_pid_ = -1;
    int rows_ = 0;
    int cols_ = 0;
    bool child_exited_ = false;
};

#endif /* CORE_PTY_H */
