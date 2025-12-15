/*
 * Pseudo Terminal (PTY) spawns a shell (bash, sh, zsh, fish, etc)
 * defaulted by the user or sh if obtaining the default shell fails
 *
 * Author: Rattle-Brain
 */

#include "pty.h"
#include <string.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <poll.h>
#include <fcntl.h>
#include <errno.h>

/* Global for raw mode restore (used by legacy functions) */
static struct termios orig_termios;
static int raw_mode_enabled = 0;

/* Obtain the user default Shell. Else return /bin/sh */
const char *get_user_shell(void) {
    const char *shell = getenv("SHELL");
    if (shell && shell[0] != '\0') {
        return shell;
    }

    struct passwd *pw = getpwuid(getuid());
    if (pw && pw->pw_shell && pw->pw_shell[0] != '\0') {
        return pw->pw_shell;
    }

    return "/bin/sh";
}

/* Legacy raw mode functions */
void disable_raw_mode(void) {
    if (raw_mode_enabled) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
        raw_mode_enabled = 0;
    }
}

void enable_raw_mode(void) {
    if (raw_mode_enabled) return;

    tcgetattr(STDIN_FILENO, &orig_termios);

    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ECHO | ICANON | ISIG);
    raw.c_iflag &= ~(IXON | ICRNL);
    raw.c_oflag &= ~(OPOST);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;

    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
    atexit(disable_raw_mode);
    raw_mode_enabled = 1;
}

PTY *pty_create(int rows, int cols) {
    PTY *pty = malloc(sizeof(PTY));
    if (!pty) return NULL;

    memset(pty, 0, sizeof(PTY));
    pty->rows = rows;
    pty->cols = cols;

    /* Set up window size */
    struct winsize ws = {
        .ws_row = rows,
        .ws_col = cols,
        .ws_xpixel = 0,
        .ws_ypixel = 0
    };

    /* Get the user's shell */
    const char *shell = get_user_shell();

    /* Fork with a new PTY */
    pty->child_pid = forkpty(&pty->master_fd, NULL, NULL, &ws);

    if (pty->child_pid < 0) {
        perror("forkpty");
        free(pty);
        return NULL;
    }

    if (pty->child_pid == 0) {
        /* Child process - exec the shell */
        setsid();
        execlp(shell, shell, NULL);
        perror("execlp");
        _exit(1);
    }

    /* Parent process */

    /* Set master fd to non-blocking for async I/O */
    int flags = fcntl(pty->master_fd, F_GETFL, 0);
    if (flags != -1) {
        fcntl(pty->master_fd, F_SETFL, flags | O_NONBLOCK);
    }

    return pty;
}

void pty_destroy(PTY *pty) {
    if (!pty) return;

    /* Close the master fd */
    if (pty->master_fd >= 0) {
        close(pty->master_fd);
    }

    /* Kill the child process if still running */
    if (pty->child_pid > 0) {
        int status;
        pid_t result = waitpid(pty->child_pid, &status, WNOHANG);

        if (result == 0) {
            /* Child still running, send SIGHUP then SIGKILL */
            kill(pty->child_pid, SIGHUP);
            usleep(50000);  /* 50ms grace period */

            result = waitpid(pty->child_pid, &status, WNOHANG);
            if (result == 0) {
                kill(pty->child_pid, SIGKILL);
                waitpid(pty->child_pid, &status, 0);
            }
        }
    }

    free(pty);
}

/* Read the input from the terminal */
ssize_t pty_read(PTY *pty, char *buf, size_t len) {
    if (!pty || pty->master_fd < 0) return -1;

    ssize_t n = read(pty->master_fd, buf, len);

    /* Handle non-blocking read */
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        return 0;  /* No data available */
    }

    return n;
}

ssize_t pty_write(PTY *pty, const char *buf, size_t len) {
    if (!pty || pty->master_fd < 0) return -1;

    return write(pty->master_fd, buf, len);
}

void pty_resize(PTY *pty, int rows, int cols) {
    if (!pty || pty->master_fd < 0) return;

    pty->rows = rows;
    pty->cols = cols;

    struct winsize ws = {
        .ws_row = rows,
        .ws_col = cols,
        .ws_xpixel = 0,
        .ws_ypixel = 0
    };

    /* Send TIOCSWINSZ to update terminal size */
    ioctl(pty->master_fd, TIOCSWINSZ, &ws);

    /* Send SIGWINCH to notify the shell */
    if (pty->child_pid > 0) {
        kill(pty->child_pid, SIGWINCH);
    }
}

/* Signal flag for interactive session */
static volatile sig_atomic_t pty_running = 1;

static void pty_signal_handler(int sig) {
    (void)sig;
    pty_running = 0;
}

int pty_spawn_interactive(int rows, int cols) {
    /* Use terminal size if 0 passed */
    if (rows <= 0 || cols <= 0) {
        struct winsize ws;
        if (ioctl(STDIN_FILENO, TIOCGWINSZ, &ws) == 0) {
            rows = ws.ws_row;
            cols = ws.ws_col;
        } else {
            rows = 24;
            cols = 80;
        }
    }

    /* Set up signal handlers */
    struct sigaction sa;
    sa.sa_handler = pty_signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    /* Enable raw mode */
    enable_raw_mode();

    /* Create the PTY */
    PTY *pty = pty_create(rows, cols);
    if (!pty) {
        disable_raw_mode();
        return -1;
    }

    pty_running = 1;
    char buf[4096];
    int exit_code = 0;

    struct pollfd pfds[2];
    pfds[0].fd = STDIN_FILENO;
    pfds[0].events = POLLIN;
    pfds[1].fd = pty->master_fd;
    pfds[1].events = POLLIN;

    while (pty_running) {
        int ret = poll(pfds, 2, 100);

        if (ret < 0) {
            if (errno == EINTR) continue;
            break;
        }

        /* Forward stdin to PTY */
        if (pfds[0].revents & POLLIN) {
            ssize_t n = read(STDIN_FILENO, buf, sizeof(buf));
            if (n <= 0) break;
            pty_write(pty, buf, n);
        }

        /* Forward PTY output to stdout */
        if (pfds[1].revents & POLLIN) {
            ssize_t n = pty_read(pty, buf, sizeof(buf));
            if (n < 0) break;
            if (n > 0) {
                write(STDOUT_FILENO, buf, n);
            }
        }

        /* Check for hangup/error on PTY */
        if (pfds[1].revents & (POLLHUP | POLLERR)) {
            break;
        }

        /* Check if child exited */
        int status;
        pid_t result = waitpid(pty->child_pid, &status, WNOHANG);
        if (result > 0) {
            if (WIFEXITED(status)) {
                exit_code = WEXITSTATUS(status);
            }
            break;
        }
    }

    pty_destroy(pty);
    disable_raw_mode();

    return exit_code;
}
