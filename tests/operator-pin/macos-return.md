# macOS kiosk return regression

The MobileKit Mac returned head status to `idle` after the five-minute timeout,
but the native fullscreen grid remained in a detached/hidden Space. Its 220×100
exit overlay was the only visible app window; a successful `/window/show`
response was insufficient evidence of a usable menu.

Both initial kiosk presentation and `/window/show` now use a borderless normal
window sized to the display's full geometry on macOS. The window follows Spaces,
and kiosk presentation continues to hide Dock and menu. Windows/Linux retain
native fullscreen. Agent-driven returns also clear pending operator PIN UI.

On the actual Mac, launch a local game from HydraGrid, open its world, then let
the configured five-minute timeout expire. Verify normal save/exit, head `idle`,
and a fresh screenshot **through Terminal** showing the full MendelCraft-only
grid. Repeat the launch/return once and confirm the launch count rises once.
Inspect the display, not only API response codes or the floating exit handle.

## Full-display coverage and desktop clicks

On MobileKit a subsequent check found a 1900×1060 grid window on a 1920×1080
display. Clicking the exposed desktop activated macOS Show Desktop, which
pushed the grid aside even though the app remained frontmost. Borderless
presentation alone is not a fullscreen size constraint.

The Mac kiosk now fixes both minimum and maximum window size to the full
display, at startup and on agent-driven return. It also reapplies the origin
after native window creation. Other desktop windows keep their normal sizing.
Provision the venue Mac with `com.apple.WindowManager
EnableStandardClickToShowDesktop=false` as a separate OS restriction.

On the real display, compare the main window's AX position/size with NSScreen
frame (use logical points consistently). They must be exactly (0,0),1920×1080
on this Mac, not the visible work area and not 1900×1060. Attempt to shrink
the kiosk window and click its far bottom/right edges. Capture through Terminal
afterward: only the grid should remain visible. Repeat after hide/show and
after a local game returns. Do not mistake the 220×100 floating exit control
for the main window.
