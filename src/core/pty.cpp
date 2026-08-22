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
#include <vector>

/* environ, for building the child's environment before the fork. */
#if defined(__APPLE__)
  #include <crt_externs.h>
#else
extern char** environ;
#endif

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

char** currentEnviron() {
#if defined(__APPLE__)
    /* The linker only guarantees `environ` to an executable; this works from
     * anywhere, which includes the static library the tests link. */
    return *_NSGetEnviron();
#else
    return environ;
#endif
}

/* True when `entry` is "NAME=..." for one of the names we are replacing. */
bool namedBy(const char* entry, const char* name) {
    const size_t length = std::strlen(name);
    return std::strncmp(entry, name, length) == 0 && entry[length] == '=';
}

/*
 * The child's environment, assembled in the *parent*.
 *
 * setenv() and unsetenv() are not async-signal-safe -- they can reallocate the
 * environment block -- so the child gets a finished one through execve()
 * instead of editing its own. Four names are dropped and two added:
 *
 *   TERM, COLORTERM  set, so the shell knows what it is talking to
 *   LINES, COLUMNS   dropped: a stale value would lie about our geometry, and
 *                    the pty's winsize is authoritative
 *   PWD              dropped: some shells trust an inherited PWD over getcwd(),
 *                    and a stale one makes the prompt disagree with the
 *                    directory the shell is actually in
 */
std::vector<std::string> buildChildEnvironment() {
    static const char* const dropped[] = {"TERM", "COLORTERM", "LINES", "COLUMNS", "PWD"};

    std::vector<std::string> entries;
    for (char** entry = currentEnviron(); entry && *entry; ++entry) {
        bool skip = false;
        for (const char* name : dropped) {
            if (namedBy(*entry, name)) { skip = true; break; }
        }
        if (!skip) entries.emplace_back(*entry);
    }

    entries.emplace_back(std::string("TERM=") + kTerm);
    entries.emplace_back(std::string("COLORTERM=") + kColorTerm);
    return entries;
}

/* Report from the child without stdio, which is not async-signal-safe. */
void writeChildError(const char* message) {
    const ssize_t ignored = ::write(STDERR_FILENO, message, std::strlen(message));
    (void)ignored;
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

/*
 * The child, between fork and exec.
 *
 * Everything in here is async-signal-safe, and it has to be. RaTTY is a
 * multi-threaded process -- Qt, the Wayland client, fontconfig and the GL driver
 * all bring threads of their own -- and fork() copies only the calling thread.
 * Any lock another thread happened to be holding at that instant is copied
 * *held*, with no owner left alive to release it, so the child deadlocks the
 * first time it wants that lock.
 *
 * The malloc arena lock is the one that matters, because almost everything takes
 * it. A single std::string built on this side of the fork is enough: the child
 * hangs before reaching exec, the pty master stays open, and the parent sees a
 * perfectly valid session with a shell that never says anything. What the user
 * sees is a window with a cursor in it and no prompt -- and because it is a
 * race, it depends on optimization level, on the compositor, and on how much
 * font work happened to be in flight. That was a real bug, not a hypothetical:
 * see doc/notable-bugs.md.
 *
 * So: no allocation, no stdio, no setenv, nothing that takes a lock. Every
 * string the child needs was built by the parent and is reachable through
 * `image`; the only calls below are the ones POSIX lists as safe to make from a
 * signal handler.
 */
void PTY::execChild(const ChildImage& image) {
    /*
     * Before exec, so the shell's own startup files see it and `pwd` agrees.
     * A failure is deliberately not fatal: the directory may have been removed
     * since it was configured or since the pane we inherited it from last
     * looked, and starting somewhere is better than a pane that never opens.
     */
    if (image.workingDirectory && chdir(image.workingDirectory) != 0) {
        /* The child's stderr is the pty slave, so this lands in the user's own
         * terminal window rather than nowhere. */
        writeChildError("ratty: could not enter the configured directory\r\n");
    }

    setsid();
#ifdef TIOCSCTTY
    ioctl(STDIN_FILENO, TIOCSCTTY, 0);
#endif

    /* Qt installs handlers and may block signals; the shell must start clean. */
    sigset_t empty;
    sigemptyset(&empty);
    sigprocmask(SIG_SETMASK, &empty, nullptr);
    signal(SIGCHLD, SIG_DFL);
    signal(SIGPIPE, SIG_DFL);

    /*
     * A *login* shell (argv[0] prefixed with '-'), which is what Terminal.app
     * and kitty do: without it ~/.zprofile never runs and PATH ends up missing
     * Homebrew and friends.
     */
    execve(image.shell, const_cast<char* const*>(image.loginArgv),
           const_cast<char* const*>(image.envp));

    /* execve only returns on failure. Fall back to a plain interactive shell,
     * then to /bin/sh, before giving up. */
    execve(image.shell, const_cast<char* const*>(image.plainArgv),
           const_cast<char* const*>(image.envp));

    /* Stack storage, so this costs no allocation either. */
    char shell[] = "/bin/sh";
    char argv0[] = "-sh";
    char* argv[] = {argv0, nullptr};
    execve(shell, argv, const_cast<char* const*>(image.envp));

    writeChildError("ratty: could not start a shell\r\n");
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

    /*
     * Everything the child will need, built here -- while there is still a whole
     * process to build it in. See execChild() for what goes wrong when any of
     * this happens on the other side of the fork.
     */
    const std::string shell = getUserShell();
    const std::string loginName = "-" + baseName(shell);
    const std::string plainName = baseName(shell);

    const std::vector<std::string> environment = buildChildEnvironment();
    std::vector<char*> envp;
    envp.reserve(environment.size() + 1);
    for (const std::string& entry : environment) {
        envp.push_back(const_cast<char*>(entry.c_str()));
    }
    envp.push_back(nullptr);

    char* loginArgv[] = {const_cast<char*>(loginName.c_str()), nullptr};
    char* plainArgv[] = {const_cast<char*>(plainName.c_str()), nullptr};

    ChildImage image;
    image.shell = shell.c_str();
    image.loginArgv = loginArgv;
    image.plainArgv = plainArgv;
    image.envp = envp.data();
    image.workingDirectory = workingDirectory.empty() ? nullptr
                                                      : workingDirectory.c_str();

    child_pid_ = forkpty(&master_fd_, nullptr, nullptr, &ws);

    if (child_pid_ < 0) {
        perror("forkpty");
        master_fd_ = -1;
        child_pid_ = -1;
        return;
    }

    if (child_pid_ == 0) {
        execChild(image);
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
