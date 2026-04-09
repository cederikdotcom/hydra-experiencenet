#include "macos_permissions.h"

#ifdef __APPLE__

#include <CoreGraphics/CoreGraphics.h>
#include <IOKit/pwr_mgt/IOPMLib.h>
#include <SDL_log.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

bool checkScreenRecordingPermission()
{
    // CGDisplayCreateImage returns nil if screen recording permission
    // has not been granted. This is the standard way to check.
    CGImageRef image = CGWindowListCreateImage(
        CGRectInfinite,
        kCGWindowListOptionOnScreenOnly,
        kCGNullWindowID,
        kCGWindowImageDefault);
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
    CGImageRef image = CGWindowListCreateImage(
        CGRectInfinite,
        kCGWindowListOptionOnScreenOnly,
        kCGNullWindowID,
        kCGWindowImageDefault);
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

void requestLocalNetworkPermission()
{
    // Sending a UDP broadcast triggers the macOS Local Network TCC prompt.
    // The prompt shows "HydraExperienceNet would like to find and connect
    // to devices on your local network" with an Allow button.
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Failed to create socket for local network permission trigger");
        return;
    }

    int broadcastEnable = 1;
    setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &broadcastEnable, sizeof(broadcastEnable));

    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(9);  // discard protocol port
    addr.sin_addr.s_addr = htonl(INADDR_BROADCAST);

    const char msg[] = "hydra-local-network-probe";
    sendto(sock, msg, sizeof(msg), 0, (struct sockaddr*)&addr, sizeof(addr));
    close(sock);

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "Local network permission probe sent (TCC dialog should appear if not yet granted)");
}

#endif
