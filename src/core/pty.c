/*
* Pseudo Terminal (PTY) spawns a shell (bash, sh, zsh, fish, etc) 
* defaulted by the user or sh if obtaining the default shell fails
*
* Author: Rattle-Brain
*/

#include "pty.h"
#include <termios.h>
#include <unistd.h>

static struct termios orig_termios;

// Obtain the user default Shell. Else return /bin/sh (common to all linux distros)
const char* get_user_shell(void) {
  const char* shell = getenv("SHELL");
  if (shell && shell[0] != '\0'){
    return shell;
  }

  struct passwd* pw = getpwuid(getuid());

  if (pw && pw->pw_shell && pw->pw_shell[0] != '\0'){
    return pw->pw_shell;
  }

  return "/bin/sh";
}

void disable_raw_mode(void) {
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}

void enable_raw_mode(void) {
  tcgetattr(STDIN_FILENO, &orig_termios);

  struct termios raw = orig_termios;

  raw.c_lflag &= ~(ECHO | ICANON | ISIG);
  raw.c_iflag &= ~(IXON | ICRNL);
  raw.c_oflag &= ~(OPOST);
  raw.c_cc[VMIN] = 1;
  raw.c_cc[VTIME] = 0;

  tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
  atexit(disable_raw_mode);
}

void spawn_pty(void){
  char buf [4096];
  int master_fd;
  pid_t pid;

  
  // Obtain the shell
  const char* shell = get_user_shell();

  // Spawn a terminal
  pid = forkpty(&master_fd, NULL, NULL, NULL);

  if (pid < 0)
  {
    perror("Error spawning forkpty");
    exit(EXIT_FAILURE);
  }

  // Init struct after init master_fd
  struct pollfd fds[2];
  fds[0].fd = STDIN_FILENO;
  fds[0].events = POLLIN;
  fds[1].fd = master_fd;
  fds[1].events = POLLIN;


  if(pid == 0)
  {
    setsid();
    execlp(shell, shell, NULL);
    perror("execlp");
    exit(1);
  }
  else 
  {
    enable_raw_mode();
    while (1) {
      int ret = poll(fds, 2, -1);
      if (ret < 0)
        break;

      // Keyboard → PTY
      if (fds[0].revents & POLLIN) {
        ssize_t n = read(STDIN_FILENO, buf, sizeof(buf));
        if (n <= 0) break;
        write(master_fd, buf, n);
      }

      // PTY → Screen
      if (fds[1].revents & POLLIN) {
        ssize_t n = read(master_fd, buf, sizeof(buf));
        if (n <= 0) break;
        write(STDOUT_FILENO, buf, n);
      }
    }
  }
}

