/*
 * Pseudo Terminal (PTY) spawns a shell (bash, sh, zsh, fish, etc)
 * defaulted by the user or sh if obtaining the default shell fails
 *
 * Author: Rattle-Brain
 */

#ifndef PTY_H
#define PTY_H

#include <pwd.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>

#if defined(__linux__)
  #include <pty.h>
#elif defined(__APPLE__) || defined(__FreeBSD__)
  #include <util.h>
#endif

/*
 * PTY - Pseudo Terminal handle
 *
 * Encapsulates everything needed to communicate with a shell:
 * - master_fd: File descriptor for reading/writing to the shell
 * - child_pid: PID of the shell process (for signals, waiting)
 * - rows/cols: Terminal dimensions (for resize signals)
 */
typedef struct {
    int master_fd;           /* Master side file descriptor */
    pid_t child_pid;         /* Shell process PID */
    int rows;                /* Terminal rows */
    int cols;                /* Terminal columns */
    struct termios orig;     /* Original terminal settings (for restore) */
} PTY;

/* Lifecycle */
PTY *pty_create(int rows, int cols);
void pty_destroy(PTY *pty);

/* I/O */
ssize_t pty_read(PTY *pty, char *buf, size_t len);
ssize_t pty_write(PTY *pty, const char *buf, size_t len);

/* Resize */
void pty_resize(PTY *pty, int rows, int cols);

/* Utilities */
const char *get_user_shell(void);

/* Legacy raw mode functions (for standalone PTY testing) */
void enable_raw_mode(void);
void disable_raw_mode(void);

/*
 * Spawn an interactive PTY session.
 *
 * This is the main entry point for splits/tabs. It:
 * - Enables raw mode
 * - Creates a PTY with the given dimensions
 * - Runs the I/O loop (stdin <-> shell)
 * - Cleans up when the shell exits or on signal
 *
 * Returns the shell's exit code, or -1 on error.
 * When the calling process dies, the PTY dies with it.
 */
int pty_spawn_interactive(int rows, int cols);

#endif /* PTY_H */
