#include "core/pty.h"

#if defined(__linux__)
  #include <pty.h>
#elif defined(__APPLE__) || defined(__FreeBSD__)
  #include <util.h>
#endif


int main() 
{
  spawn_pty();
  return 0;
}
