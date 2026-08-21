/*
 * Platform - the implementation for everywhere that needs nothing
 *
 * X11 and Wayland repeat a held key without being asked, so there is nothing to
 * do here. The file exists so that main() can call the same function on every
 * platform instead of guarding the call.
 */

#include "platform.h"

namespace platform {

void enableKeyRepeat() {}

} // namespace platform
