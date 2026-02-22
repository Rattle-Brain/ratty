/*
 * Pseudo Terminal (PTY) - C++ RAII implementation
 *
 * Author: Rattle-Brain
 */

#include "pty.h"
#include <cstring>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>
#include <cstdlib>

/* Get the user's default shell */
std::string PTY::getUserShell() {
    // Try $SHELL environment variable first
    const char *shell = std::getenv("SHELL");
    if (shell && shell[0] != '\0') {
        return std::string(shell);
    }

    // Try password database
    struct passwd *pw = getpwuid(getuid());
    if (pw && pw->pw_shell && pw->pw_shell[0] != '\0') {
        return std::string(pw->pw_shell);
    }

    // Fallback
    return "/bin/sh";
}

PTY::PTY(int rows, int cols)
    : master_fd_(-1)
    , child_pid_(-1)
    , rows_(rows)
    , cols_(cols)
{
    // Set up window size
    struct winsize ws = {
        .ws_row = static_cast<unsigned short>(rows),
        .ws_col = static_cast<unsigned short>(cols),
        .ws_xpixel = 0,
        .ws_ypixel = 0
    };

    // Get the user's shell
    std::string shell = getUserShell();

    // Fork with a new PTY
    child_pid_ = forkpty(&master_fd_, nullptr, nullptr, &ws);

    if (child_pid_ < 0) {
        perror("forkpty");
        master_fd_ = -1;
        child_pid_ = -1;
        return;
    }

    if (child_pid_ == 0) {
        // Child process - exec the shell
        setsid();
        execlp(shell.c_str(), shell.c_str(), nullptr);
        perror("execlp");
        _exit(1);
    }

    // Parent process

    // Set master fd to non-blocking for async I/O
    int flags = fcntl(master_fd_, F_GETFL, 0);
    if (flags != -1) {
        fcntl(master_fd_, F_SETFL, flags | O_NONBLOCK);
    }
}

PTY::~PTY() {
    cleanup();
}

PTY::PTY(PTY&& other) noexcept
    : master_fd_(other.master_fd_)
    , child_pid_(other.child_pid_)
    , rows_(other.rows_)
    , cols_(other.cols_)
{
    // Invalidate the moved-from object
    other.master_fd_ = -1;
    other.child_pid_ = -1;
    other.rows_ = 0;
    other.cols_ = 0;
}

PTY& PTY::operator=(PTY&& other) noexcept {
    if (this != &other) {
        // Clean up our current resources
        cleanup();

        // Transfer ownership
        master_fd_ = other.master_fd_;
        child_pid_ = other.child_pid_;
        rows_ = other.rows_;
        cols_ = other.cols_;

        // Invalidate the moved-from object
        other.master_fd_ = -1;
        other.child_pid_ = -1;
        other.rows_ = 0;
        other.cols_ = 0;
    }
    return *this;
}

void PTY::cleanup() {
    // Close the master fd
    if (master_fd_ >= 0) {
        ::close(master_fd_);
        master_fd_ = -1;
    }

    // Kill the child process if still running
    if (child_pid_ > 0) {
        int status;
        pid_t result = waitpid(child_pid_, &status, WNOHANG);

        if (result == 0) {
            // Child still running, send SIGHUP then SIGKILL
            kill(child_pid_, SIGHUP);
            usleep(50000);  // 50ms grace period

            result = waitpid(child_pid_, &status, WNOHANG);
            if (result == 0) {
                kill(child_pid_, SIGKILL);
                waitpid(child_pid_, &status, 0);
            }
        }

        child_pid_ = -1;
    }
}

ssize_t PTY::read(char* buf, size_t len) {
    if (!isValid()) return -1;

    ssize_t n = ::read(master_fd_, buf, len);

    // Handle non-blocking read
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        return 0;  // No data available
    }

    return n;
}

ssize_t PTY::write(const char* buf, size_t len) {
    if (!isValid()) return -1;

    return ::write(master_fd_, buf, len);
}

void PTY::resize(int rows, int cols) {
    if (!isValid()) return;

    rows_ = rows;
    cols_ = cols;

    struct winsize ws = {
        .ws_row = static_cast<unsigned short>(rows),
        .ws_col = static_cast<unsigned short>(cols),
        .ws_xpixel = 0,
        .ws_ypixel = 0
    };

    // Send TIOCSWINSZ to update terminal size
    ioctl(master_fd_, TIOCSWINSZ, &ws);

    // Send SIGWINCH to notify the shell
    if (child_pid_ > 0) {
        kill(child_pid_, SIGWINCH);
    }
}
