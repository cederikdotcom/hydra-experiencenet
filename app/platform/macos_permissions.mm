#include "macos_permissions.h"

#ifdef Q_OS_MACOS

#include <CoreGraphics/CoreGraphics.h>
#include <IOKit/pwr_mgt/IOPMLib.h>
#include <SDL_log.h>

bool checkScreenRecordingPermission()
{
    // CGDisplayCreateImage returns nil if screen recording permission
    // has not been granted. This is the standard way to check.
    CGImageRef image = CGDisplayCreateImage(CGMainDisplayID());
    if (image) {
        CGImageRelease(image);
        return true;
    }
    return false;
}

void requestScreenRecordingPermission()
{
    // Attempting CGDisplayCreateImage triggers the macOS TCC prompt
    // if the app hasn't been granted screen recording permission yet.
    // The prompt says "HydraExperienceNet would like to record this screen."
    CGImageRef image = CGDisplayCreateImage(CGMainDisplayID());
    if (image) {
        CGImageRelease(image);
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "Screen recording permission already granted");
    } else {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Screen recording permission not granted. "
                    "Please allow in System Settings > Privacy & Security > Screen Recording");
    }
}

uint32_t preventDisplaySleep()
{
    IOPMAssertionID assertionId = 0;
    IOReturn result = IOPMAssertionCreateWithName(
        kIOPMAssertionTypePreventUserIdleDisplaySleep,
        kIOPMAssertionLevelOn,
        CFSTR("HydraExperienceNet kiosk mode - display must stay on"),
        &assertionId);

    if (result == kIOReturnSuccess) {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "Display sleep prevention active (assertion %u)", assertionId);
        return (uint32_t)assertionId;
    }

    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                 "Failed to prevent display sleep: IOReturn %d", result);
    return 0;
}

void releaseDisplaySleepAssertion(uint32_t assertionId)
{
    if (assertionId != 0) {
        IOPMAssertionRelease((IOPMAssertionID)assertionId);
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "Display sleep prevention released (assertion %u)", assertionId);
    }
}

#endif
