#include "core/pty.h"

int main(void) {
    return pty_spawn_interactive(800, 100);
}
