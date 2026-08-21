/*
 * Platform - macOS implementation (Objective-C++)
 *
 * The only file in the project that is not plain C++, and only because AppKit's
 * press-and-hold behaviour has no Qt equivalent. See platform.h for why a
 * terminal has to turn it off.
 */

#include "platform.h"

#import <Foundation/Foundation.h>

namespace platform {

void enableKeyRepeat() {
    @autoreleasepool {
        /*
         * registerDefaults: writes into the *registration* domain, which lives
         * only in this process and is consulted last. That is deliberate: it
         * changes nothing on disk and nothing for any other application, and an
         * explicit `defaults write -g ApplePressAndHoldEnabled true` still
         * overrides it, because the global domain outranks this one.
         */
        [[NSUserDefaults standardUserDefaults] registerDefaults:@{
            @"ApplePressAndHoldEnabled" : @NO
        }];
    }
}

} // namespace platform
