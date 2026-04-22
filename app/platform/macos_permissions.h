#pragma once

#include <stdint.h>

#ifdef __APPLE__
#include <CoreGraphics/CGImage.h>
#include <CoreGraphics/CGDirectDisplay.h>

// Checks if screen recording permission has been granted.
// Returns true if the app can capture the screen.
bool checkScreenRecordingPermission();

// Triggers the macOS TCC prompt for screen recording permission
// by attempting a test capture. The user will see a system dialog
// asking to allow HydraExperienceNet to record the screen.
void requestScreenRecordingPermission();

// Prevents the display from sleeping using IOPMAssertion.
// Returns an assertion ID that can be passed to releaseDisplaySleepAssertion().
// Returns 0 on failure.
uint32_t preventDisplaySleep();

// Releases a display sleep prevention assertion.
void releaseDisplaySleepAssertion(uint32_t assertionId);

// Triggers the macOS TCC prompt for local network access by sending
// a UDP broadcast. The user will see "HydraExperienceNet would like
// to find and connect to devices on your local network."
void requestLocalNetworkPermission();

// Engages "kiosk" presentation options so the macOS menu bar and dock
// auto-hide while the app is frontmost. Does NOT switch into macOS
// fullscreen Space, so the floating Qt overlay window can still sit on
// top of the kiosk content.
void enableKioskPresentation();

// Configures an NSWindow so it appears on every macOS Space (including
// any fullscreen Space another window is in), and floats above normal
// windows. Used by the floating Qt exit overlay so it continues to sit
// above the stream/kiosk even when the underlying window enters a
// macOS fullscreen Space. Pass the QWindow's winId() as windowId.
void makeWindowFollowAllSpaces(uint64_t windowId);

// Captures the main display using ScreenCaptureKit (macOS 14+).
// Captures everything including SDL Metal fullscreen content and across Spaces.
// Returns a CGImageRef that the caller must release. Returns NULL on failure.
// Falls back to CGDisplayCreateImage if ScreenCaptureKit is unavailable.
CGImageRef captureDisplay();

#endif
