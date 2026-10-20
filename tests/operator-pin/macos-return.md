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
