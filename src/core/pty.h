/*
* Pseudo Terminal (PTY) spawns a shell (bash, sh, zsh, fish, etc) 
* defaulted by the user or sh if obtaining the default shell fails
*
* Author: Rattle-Brain
*/

#include <pwd.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/poll.h>
#include <unistd.h>
#include <sys/types.h>
#include <termios.h>
#include <poll.h>
#include <pty.h>

#if defined(__linux__)
  #include <pty.h>
#elif defined(__APPLE__) || defined(__FreeBSD__)
  #include <util.h>
#endif

static struct termios orig_termios;

const char* get_user_shell(void);
void disable_raw_mode(void);
void enable_raw_mode(void);
void spawn_pty(void);
