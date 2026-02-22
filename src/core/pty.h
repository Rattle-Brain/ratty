/*
 * Pseudo Terminal (PTY) - C++ RAII wrapper for shell spawning
 *
 * Spawns a shell (bash, sh, zsh, fish, etc) defaulted by the user
 * or sh if obtaining the default shell fails
 *
 * Author: Rattle-Brain
 */

#ifndef PTY_H
#define PTY_H

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

/*
 * PTY - Pseudo Terminal handle
 *
 * RAII wrapper for a pseudo-terminal that manages the lifecycle of a shell process.
 * Encapsulates everything needed to communicate with a shell:
 * - master_fd: File descriptor for reading/writing to the shell
 * - child_pid: PID of the shell process (for signals, waiting)
 * - rows/cols: Terminal dimensions (for resize signals)
 */
class PTY {
public:
    /*
     * Create a PTY with specified dimensions
     * Forks a new shell process and sets up the pseudo-terminal
     */
    PTY(int rows, int cols);

    /*
     * Destructor - cleans up file descriptors and terminates child process
     */
    ~PTY();

    // Delete copy constructor and copy assignment (not copyable)
    PTY(const PTY&) = delete;
    PTY& operator=(const PTY&) = delete;

    // Allow move semantics
    PTY(PTY&& other) noexcept;
    PTY& operator=(PTY&& other) noexcept;

    /*
     * Read from PTY (non-blocking)
     * Returns number of bytes read, 0 if no data, -1 on error
     */
    ssize_t read(char* buf, size_t len);

    /*
     * Write to PTY
     * Returns number of bytes written, -1 on error
     */
    ssize_t write(const char* buf, size_t len);

    /*
     * Resize the terminal
     * Sends TIOCSWINSZ ioctl and SIGWINCH signal to shell
     */
    void resize(int rows, int cols);

    /*
     * Get the master file descriptor
     * Use this for integrating with event loops (select, poll, epoll, Qt notifiers)
     */
    int masterFd() const { return master_fd_; }

    /*
     * Get the child process PID
     */
    pid_t childPid() const { return child_pid_; }

    /*
     * Get current dimensions
     */
    int rows() const { return rows_; }
    int cols() const { return cols_; }

    /*
     * Check if PTY is valid
     */
    bool isValid() const { return master_fd_ >= 0 && child_pid_ > 0; }

    /*
     * Get the user's default shell
     * Checks $SHELL env var, then password database, fallback to /bin/sh
     */
    static std::string getUserShell();

private:
    int master_fd_;       // Master side file descriptor
    pid_t child_pid_;     // Shell process PID
    int rows_;            // Terminal rows
    int cols_;            // Terminal columns

    // Helper for cleanup
    void cleanup();
};

#endif /* PTY_H */
