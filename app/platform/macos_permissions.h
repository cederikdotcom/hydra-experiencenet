#pragma once

#ifdef __APPLE__

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

#endif
