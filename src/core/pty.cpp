/*
 * PTY - pseudo-terminal implementation
 *
 * Author: Rattle-Brain
 */

#include "pty.h"
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/wait.h>

/* workingDirectory() asks the operating system where the shell is, and the two
 * platforms answer through completely different interfaces. */
#if defined(__linux__)
  #include <climits>
#elif defined(__APPLE__)
  #include <libproc.h>
#endif

namespace {

/*
 * The child needs a TERM value or the shell falls back to "dumb" and stops
 * emitting colours and cursor movement entirely. Previously nothing was set,
 * so behaviour depended on whatever the launching environment happened to
 * export - which differs between running from a terminal and launching from
 * Finder or a .desktop file.
 */
constexpr const char* kTerm = "xterm-256color";
constexpr const char* kColorTerm = "truecolor";

/* Basename of a path, for building a login-shell argv[0] ("-zsh"). */
std::string baseName(const std::string& path) {
    const size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

} // namespace

std::string PTY::getUserShell() {
    if (const char* shell = std::getenv("SHELL"); shell && shell[0] != '\0') {
        return std::string(shell);
    }

    if (const struct passwd* pw = getpwuid(getuid()); pw && pw->pw_shell && pw->pw_shell[0] != '\0') {
        return std::string(pw->pw_shell);
    }

    return "/bin/sh";
}

void PTY::execChild(const std::string& shell, const std::string& workingDirectory) {
    /*
     * Start a *login* shell (argv[0] prefixed with '-'), which is what
     * Terminal.app and kitty do on macOS: without it ~/.zprofile never runs and
     * PATH ends up missing Homebrew and friends.
     */
    const std::string argv0 = "-" + baseName(shell);

    /*
     * Before exec, so the shell's own startup files see it and `pwd` agrees.
     * A failure is deliberately not fatal: the directory may have been removed
     * since it was configured or since the pane we inherited it from last
     * looked, and starting somewhere is better than a pane that never opens.
     */
    if (!workingDirectory.empty() && chdir(workingDirectory.c_str()) != 0) {
        /* The parent's stderr is the pty master, so this is not printed into
         * the user's shell; PWD is corrected below regardless. */
        perror("chdir");
    }
    /*
     * Some shells trust an inherited PWD over getcwd(), and a stale one makes
     * the prompt disagree with the actual directory. Unsetting it makes the
     * shell work it out for itself.
     */
    unsetenv("PWD");

    setsid();
#ifdef TIOCSCTTY
    ioctl(STDIN_FILENO, TIOCSCTTY, 0);
#endif

    setenv("TERM", kTerm, 1);
    setenv("COLORTERM", kColorTerm, 1);
    /* Stale values inherited from the parent would lie about our geometry;
     * the pty's winsize is authoritative. */
    unsetenv("LINES");
    unsetenv("COLUMNS");

    /* Qt installs handlers and may block signals; the shell must start clean. */
    sigset_t empty;
    sigemptyset(&empty);
    sigprocmask(SIG_SETMASK, &empty, nullptr);
    signal(SIGCHLD, SIG_DFL);
    signal(SIGPIPE, SIG_DFL);

    execl(shell.c_str(), argv0.c_str(), static_cast<char*>(nullptr));

    /* execl only returns on failure. Fall back to a plain interactive shell,
     * then to /bin/sh, before giving up. */
    execl(shell.c_str(), baseName(shell).c_str(), static_cast<char*>(nullptr));
    execl("/bin/sh", "-sh", static_cast<char*>(nullptr));
    _exit(127);
}

PTY::PTY(int rows, int cols, const std::string& workingDirectory)
    : rows_(rows)
    , cols_(cols)
{
    struct winsize ws = {
        .ws_row = static_cast<unsigned short>(rows),
        .ws_col = static_cast<unsigned short>(cols),
        .ws_xpixel = 0,
        .ws_ypixel = 0
    };

    const std::string shell = getUserShell();

    child_pid_ = forkpty(&master_fd_, nullptr, nullptr, &ws);

    if (child_pid_ < 0) {
        perror("forkpty");
        master_fd_ = -1;
        child_pid_ = -1;
        return;
    }

    if (child_pid_ == 0) {
        execChild(shell, workingDirectory);
    }

    /* Parent: non-blocking master for the event loop, and close-on-exec so
     * child processes we later spawn cannot inherit it. */
    const int flags = fcntl(master_fd_, F_GETFL, 0);
    if (flags != -1) {
        fcntl(master_fd_, F_SETFL, flags | O_NONBLOCK);
    }
    fcntl(master_fd_, F_SETFD, FD_CLOEXEC);
}

PTY::~PTY() {
    cleanup();
}

PTY::PTY(PTY&& other) noexcept
    : master_fd_(other.master_fd_)
    , child_pid_(other.child_pid_)
    , rows_(other.rows_)
    , cols_(other.cols_)
    , child_exited_(other.child_exited_)
{
    other.master_fd_ = -1;
    other.child_pid_ = -1;
    other.rows_ = 0;
    other.cols_ = 0;
}

PTY& PTY::operator=(PTY&& other) noexcept {
    if (this != &other) {
        cleanup();

        master_fd_ = other.master_fd_;
        child_pid_ = other.child_pid_;
        rows_ = other.rows_;
        cols_ = other.cols_;
        child_exited_ = other.child_exited_;

        other.master_fd_ = -1;
        other.child_pid_ = -1;
        other.rows_ = 0;
        other.cols_ = 0;
    }
    return *this;
}

void PTY::cleanup() {
    if (master_fd_ >= 0) {
        ::close(master_fd_);
        master_fd_ = -1;
    }

    if (child_pid_ > 0 && !child_exited_) {
        int status = 0;
        if (waitpid(child_pid_, &status, WNOHANG) == 0) {
            /* Closing the master sends SIGHUP to the session; give it a moment
             * before escalating. */
            kill(child_pid_, SIGHUP);
            usleep(50000);

            if (waitpid(child_pid_, &status, WNOHANG) == 0) {
                kill(child_pid_, SIGKILL);
                waitpid(child_pid_, &status, 0);
            }
        }
    }

    child_pid_ = -1;
    child_exited_ = true;
}

PTY::ReadResult PTY::read(char* buf, size_t len) {
    ReadResult result;

    if (master_fd_ < 0) {
        result.error = true;
        return result;
    }

    const ssize_t n = ::read(master_fd_, buf, len);

    if (n > 0) {
        result.bytes = n;
        return result;
    }

    if (n == 0) {
        result.eof = true;
        return result;
    }

    if (errno == EAGAIN || errno == EWOULDBLOCK) {
        result.wouldBlock = true;
    } else if (errno == EIO) {
        /* On both Linux and the BSDs a pty master reports EIO once the slave
         * side is gone. That is a normal session end, not a failure. */
        result.eof = true;
    } else if (errno == EINTR) {
        result.wouldBlock = true;
    } else {
        result.error = true;
    }
    return result;
}

ssize_t PTY::write(const char* buf, size_t len) {
    if (master_fd_ < 0) return -1;

    size_t written = 0;
    while (written < len) {
        const ssize_t n = ::write(master_fd_, buf + written, len - written);
        if (n > 0) {
            written += static_cast<size_t>(n);
            continue;
        }
        if (n < 0 && (errno == EINTR)) continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            /* The shell is not draining fast enough. Dropping the tail would
             * corrupt input, so report what made it through and let the caller
             * decide. In practice terminal input is tiny and this never trips. */
            break;
        }
        if (written == 0) return -1;
        break;
    }
    return static_cast<ssize_t>(written);
}

std::string PTY::workingDirectory() const {
    if (child_pid_ <= 0 || child_exited_) return std::string();

#if defined(__linux__)
    /*
     * /proc/<pid>/cwd is a symlink to the directory. readlink() on it needs no
     * privileges for a process of the same user, which our own child always is.
     */
    const std::string link = "/proc/" + std::to_string(child_pid_) + "/cwd";
    char buffer[PATH_MAX];
    const ssize_t length = ::readlink(link.c_str(), buffer, sizeof(buffer) - 1);
    if (length <= 0) return std::string();
    buffer[length] = '\0';
    return std::string(buffer);

#elif defined(__APPLE__)
    /*
     * macOS has no /proc. proc_pidinfo() answers the same question, and
     * PROC_PIDVNODEPATHINFO is the call that carries the current directory;
     * it is permitted for a process of the same uid without any entitlement.
     */
    struct proc_vnodepathinfo info{};
    const int written = proc_pidinfo(child_pid_, PROC_PIDVNODEPATHINFO, 0,
                                     &info, sizeof(info));
    if (written < static_cast<int>(sizeof(info))) return std::string();
    if (info.pvi_cdir.vip_path[0] == '\0') return std::string();
    return std::string(info.pvi_cdir.vip_path);

#else
    /* No portable way to ask. Callers fall back to the configured default. */
    return std::string();
#endif
}

void PTY::resize(int rows, int cols) {
    if (master_fd_ < 0) return;

    rows_ = rows;
    cols_ = cols;

    struct winsize ws = {
        .ws_row = static_cast<unsigned short>(rows),
        .ws_col = static_cast<unsigned short>(cols),
        .ws_xpixel = 0,
        .ws_ypixel = 0
    };

    /* TIOCSWINSZ already delivers SIGWINCH to the slave's foreground process
     * group, so the explicit kill() the old code did was both redundant and
     * wrong for job control (it targeted the shell, not the foreground job). */
    ioctl(master_fd_, TIOCSWINSZ, &ws);
}

bool PTY::hasChildExited() {
    if (child_exited_) return true;
    if (child_pid_ <= 0) return true;

    int status = 0;
    const pid_t result = waitpid(child_pid_, &status, WNOHANG);
    if (result != 0) {
        child_exited_ = true;
    }
    return child_exited_;
}
