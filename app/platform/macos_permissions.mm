#include "macos_permissions.h"

#ifdef __APPLE__

#include <CoreGraphics/CoreGraphics.h>
#include <IOKit/pwr_mgt/IOPMLib.h>
#include <SDL_log.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#import <AppKit/AppKit.h>
#import <ScreenCaptureKit/ScreenCaptureKit.h>
#include <dispatch/dispatch.h>

void makeWindowFollowAllSpaces(uint64_t windowId)
{
    // winId() on macOS returns a pointer to the native NSView. The
    // enclosing NSWindow is reached via `-window`. Setting
    // CanJoinAllSpaces + FullScreenAuxiliary makes the window appear
    // on every Space (including a fullscreen Space that belongs to
    // another window of the same app) while still respecting the
    // floating level from Qt.WindowStaysOnTopHint.
    NSView* view = (__bridge NSView*)(void*)(uintptr_t)windowId;
    if (view == nil) {
        return;
    }
    NSWindow* win = [view window];
    if (win == nil) {
        return;
    }
    // Only CanJoinAllSpaces. The previous attempt also ORed in
    // FullScreenAuxiliary and Stationary; that combination failed
    // -[NSWindow _validateCollectionBehavior:] on macOS 26 and caused
    // HydraExperienceNet to crash with SIGABRT on stream launch.
    // CanJoinAllSpaces on its own is enough to make the overlay
    // appear on every Space, including another window's fullscreen
    // Space.
    @try {
        NSWindowCollectionBehavior behavior =
            [win collectionBehavior] |
            NSWindowCollectionBehaviorCanJoinAllSpaces;
        [win setCollectionBehavior:behavior];
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "kiosk overlay: set window collectionBehavior to follow all Spaces");
    }
    @catch (NSException *e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "kiosk overlay: setCollectionBehavior failed: %s",
                     [[e reason] UTF8String]);
    }
}

void enableKioskPresentation()
{
    // Fully hide the menu bar and dock, not auto-hide. We stay off of
    // NSApplicationPresentationFullScreen on purpose — the floating Qt
    // exit overlay cannot follow into a macOS fullscreen Space.
    //
    // Re-apply whenever the app becomes active, because macOS can reset
    // presentation options when the frontmost app changes (e.g. a
    // screencapture run briefly activates Terminal).
    dispatch_async(dispatch_get_main_queue(), ^{
        NSApplicationPresentationOptions options =
            NSApplicationPresentationHideMenuBar |
            NSApplicationPresentationHideDock;
        [NSApp setPresentationOptions:options];
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "kiosk presentation options set: hide menu bar + dock");

        [[NSNotificationCenter defaultCenter]
            addObserverForName:NSApplicationDidBecomeActiveNotification
                        object:nil
                         queue:[NSOperationQueue mainQueue]
                    usingBlock:^(NSNotification *note) {
            NSApplicationPresentationOptions opts =
                NSApplicationPresentationHideMenuBar |
                NSApplicationPresentationHideDock;
            [NSApp setPresentationOptions:opts];
        }];
    });
}

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

CGImageRef captureDisplay()
{
    __block CGImageRef result = nil;
    dispatch_semaphore_t semaphore = dispatch_semaphore_create(0);

    // Get shareable content (displays and windows)
    [SCShareableContent getShareableContentExcludingDesktopWindows:NO
                                             onScreenWindowsOnly:YES
                                               completionHandler:^(SCShareableContent * _Nullable content, NSError * _Nullable error) {
        if (error || !content || content.displays.count == 0) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "ScreenCaptureKit: failed to get shareable content: %s",
                         error ? error.localizedDescription.UTF8String : "no displays");
            dispatch_semaphore_signal(semaphore);
            return;
        }

        SCDisplay *mainDisplay = content.displays.firstObject;

        SCContentFilter *filter = [[SCContentFilter alloc] initWithDisplay:mainDisplay
                                                         excludingWindows:@[]];

        SCStreamConfiguration *config = [[SCStreamConfiguration alloc] init];
        config.width = mainDisplay.width;
        config.height = mainDisplay.height;
        config.pixelFormat = kCVPixelFormatType_32BGRA;
        config.captureResolution = SCCaptureResolutionNominal;
        config.showsCursor = YES;

        [SCScreenshotManager captureImageWithFilter:filter
                                      configuration:config
                                  completionHandler:^(CGImageRef _Nullable image, NSError * _Nullable captureError) {
            if (captureError || !image) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                             "ScreenCaptureKit: capture failed: %s",
                             captureError ? captureError.localizedDescription.UTF8String : "nil image");
            } else {
                result = CGImageRetain(image);
            }
            dispatch_semaphore_signal(semaphore);
        }];
    }];

    // Wait up to 5 seconds for the async capture
    dispatch_semaphore_wait(semaphore, dispatch_time(DISPATCH_TIME_NOW, 5 * NSEC_PER_SEC));

    if (!result) {
        // Fallback to CGDisplayCreateImage
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "ScreenCaptureKit failed, falling back to CGDisplayCreateImage");
        result = CGDisplayCreateImage(CGMainDisplayID());
    }

    return result;
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
