# Hydra ExperienceNet — Runbook

Branded fork of [moonlight-qt](https://github.com/moonlight-stream/moonlight-qt) for kiosk displays.

## Kiosk Mode

Skips host selection and shows the app grid directly for a pre-paired host.

```bash
HydraExperienceNet kiosk <HOST> --district <DISTRICT> --venue <VENUE>
```

- `HOST`: IP address or name of the paired Sunshine host
- `--district`: Read-only district label displayed in the header
- `--venue`: Read-only venue label displayed in the header

The host must already be paired (hydraheadflatscreen handles pairing automatically).

## Headless pair subcommand (Phase 1 of window consolidation)

As of v6.1.22 the `pair` subcommand accepts `--headless`. In headless
mode the QML engine is never loaded: the Launcher runs on the core
event loop, exits 0 on success or 1 on failure, and writes any
detail to stderr. hydraheadflatscreen v2.0.29+ passes `--headless`
before every stream launch so the pair step no longer flashes a Qt
window on the kiosk display.

Older HydraExperienceNet binaries ignore unknown flags, so the agent
can pass `--headless` unconditionally without a version-gated path.

Phase 2 — hosting the stream inside the kiosk process and removing
the stream subprocess entirely — is tracked on
issues.experiencenet.com **#111**. Until then, each tile-tap still
produces exactly one stream-subprocess window transition (the kiosk
hide + stream show); the pair-subprocess window transition is gone.

## Kiosk loading screen (tile tap to first frame)

The stream subprocess reuses the kiosk's fullscreen chrome during the
connection phase so the transition from the tile grid to first video
frame stays edge-to-edge and branded:

- `main.cpp` sets `kioskMode=true` for the `stream` subcommand as
  well, so the frameless window flag applies from declaration time.
- `CliStartStreamSegue.qml` calls `window.showFullScreen()` on
  `StackView.onActivated` and paints a dark backdrop with a centered
  `BusyIndicator` + "Loading experience" label.
- `StreamSegue.qml` shows the same backdrop and friendly label
  instead of the per-stage Moonlight text ("Starting control
  connection", "RTSP handshake", etc) that is engineer-speak.
- `app/cli/pair.cpp` treats an already-paired body as success
  instead of the original Moonlight-Qt "X is already paired" error
  dialog. The kiosk agent re-runs the pair subcommand before every
  stream to refresh credentials, so the dialog fired on every
  launch.

## Kiosk view goes fullscreen on launch

As of v6.1.18 the kiosk view uses a **real macOS fullscreen Space**
again — which is the only reliable way to truly cover every pixel of
the display without getting tangled in work-area math — and the
floating Qt exit overlay window is marked to **follow all Spaces**
so it keeps sitting above the kiosk even once the kiosk enters its
fullscreen Space.

Pieces involved:

1. `kioskMode` context property set by `main.cpp` on the kiosk launch
   path, read by `gui/main.qml` to declare `Qt.Window |
   Qt.FramelessWindowHint` on the main `ApplicationWindow`. Must be
   set at declaration time: changing flags on an already-visible
   window does not recreate the underlying NSWindow styleMask.
2. `KioskView.qml`'s `StackView.onActivated` calls
   `window.showFullScreen()`.
3. `StreamOverlay.qml`'s `Component.onCompleted` calls
   `KioskBridge.makeFollowAllSpaces(streamOverlayWindow)`. The bridge
   is a tiny QML-registered singleton (`platform/kioskbridge.h`) that
   invokes `makeWindowFollowAllSpaces()` in
   `platform/macos_permissions.mm`, which ORs
   `NSWindowCollectionBehaviorCanJoinAllSpaces` into the overlay
   NSWindow's collection behaviour so it appears on every Space
   including the kiosk's fullscreen Space. v6.1.18 also tried
   `FullScreenAuxiliary | Stationary`; that combination failed
   `-[NSWindow _validateCollectionBehavior:]` on macOS 26 and
   aborted the stream subprocess. v6.1.19 dropped those extras and
   wrapped the call in `@try/@catch` so any future validation
   regression becomes a log line instead of a crash.
4. `enableKioskPresentation()` (still called from `main.cpp` on
   kiosk launch) sets `NSApplicationPresentationHideMenuBar |
   HideDock` and re-applies them on
   `NSApplicationDidBecomeActiveNotification`. Kept for parity even
   though showFullScreen already hides the menu bar and dock — the
   presentation options keep things hidden if the app ever leaves
   fullscreen without our intent.
5. **Re-entering fullscreen on window show.** When a stream starts,
   hydraheadflatscreen calls `/api/v1/window/hide` and the Qt app
   hides every top-level window. On a macOS window that is already
   in a fullscreen Space, `hide()` can drop it out of the Space, so
   a later `show()` brings it back as a plain windowed app centred
   on the desktop with wallpaper visible around it. `handleWindowShow`
   in `app/api/localserver.cpp` therefore calls `showFullScreen()`
   (not plain `show()`) on non-`Qt.Tool` top-level windows so the
   kiosk returns to fullscreen each time. v6.1.20.

History of the approach: v6.1.11 `showFullScreen` (overlay stranded);
v6.1.12 `showMaximized` (overlay stays, but chrome visible); v6.1.15
added `FramelessWindowHint` from runtime and `AutoHide` options
(neither took effect reliably); v6.1.16 moved flags to declaration
time and used `HideMenuBar | HideDock`; v6.1.17 sized to `Screen`
geometry (left wallpaper strips because `Screen.height` reported
pre-hide work area); v6.1.18 reverted to real fullscreen Space and
taught the overlay to follow it — the arrangement that finally
produces an edge-to-edge kiosk with the overlay always on top. The stream
subcommand is launched by hydraheadflatscreen v2.0.28+ with
`--display-mode borderless`, which is Moonlight-Qt's
WM_FULLSCREEN_DESKTOP — a frameless fullscreen window that does NOT
create a separate macOS Space, so the floating Qt exit overlay can
stay on top of it.

Do not switch the stream to `--display-mode fullscreen` (true macOS
fullscreen): that creates a new Space and the exit overlay window does
not follow into it.

## Help button (kiosk header)

A "?" button sits in the top-right of the kiosk header (after the venue badge). Tapping it opens a modal dialog with two QR codes side by side that visitors scan with their smartphone:

- **Left QR code** — encodes `https://issues.experiencenet.com`. Scanning opens the issue tracker in the visitor's browser.
- **Right QR code** — encodes `tel:+3249927842020`. Scanning prompts the visitor's phone to call the urgent support number. The number `+32 499 27 84 20` is also printed below the QR code as a fallback.

Clicking the backdrop or the "Close" button dismisses the dialog. The dialog does not block the stream-start path — `helpVisible` is a local property of `KioskView.qml` and is unrelated to stream state.

The QR code images are pre-generated SVG files embedded as Qt resources (`res/qr_issue.svg` and `res/qr_phone.svg`). To change the URL or phone number, regenerate the SVGs:

```bash
npx qrcode -t svg -o app/res/qr_issue.svg "https://issues.experiencenet.com/report"
npx qrcode -t svg -o app/res/qr_phone.svg "tel:+3249927842020"
```

Also update the fallback phone number text in `KioskView.qml` (`"Scan to call for urgent help\n+32 499 27 84 20"`) and tag a new release.

## Exit-to-menu overlay (stream view and kiosk view)

A subtle handle is visible at the top centre of the screen both during
an active stream and on the experience library (kiosk grid). Tapping
it opens a small dropdown menu with a single "Exit experience" item
that slides out directly beneath the handle.

**Implementation:** as of v6.1.10 the handle + dropdown are implemented
as a frameless, always-on-top Qt Quick window (`app/gui/StreamOverlay.qml`)
loaded by `StreamSegue.qml` when the stream connection starts. v6.1.11
extends this so `KioskView.qml` loads the same overlay as soon as the
experience library activates, which means the handle is also visible on
the grid. When no Session is bound (kiosk view), the overlay's tap handler
no-ops — the handle is purely for visual consistency there. Qt Quick
handles hit testing, hover, and click routing natively — no SDL drawable
coordinate math, no interaction with the Moonlight mouse capture mode.
The window is destroyed on `sessionReadyForDeletion` so it does not hold
a Session reference beyond its lifetime. The older SDL/Metal overlay
(`OverlayExitButton` / `OverlayExitMenu` in `overlaymanager.h`) is still
present during a transition period and will be removed in a later
cleanup once the QML overlay is proven out. Behaviour:

- **Handle (always visible):** a circle glyph at the top centre of
  the screen. Present throughout the stream and on the kiosk grid so
  the exit gesture is discoverable.
- **Tap / click the handle:** toggles the dropdown. No disconnect yet.
- **Tap / click "Exit experience":** triggers a clean disconnect. As of
  v6.1.25 the overlay also briefly expands to fullscreen with a black
  "Quitting experience" veil during the disconnect to cover the macOS
  desktop flash that was visible between the stream subprocess's
  fullscreen Space collapsing and the kiosk's Space coming forward.
  A one-shot 1.5-second `Timer` in `StreamOverlay.qml` collapses the
  veil back regardless of what the streaming pipeline does, so a
  hang on the stream side cannot leave the visitor stuck behind an
  opaque black screen. The existing `quitStarting` →
  `sessionFinished` path continues to return the visitor to the kiosk
  grid (same flow as the Ctrl+Alt+Shift+Q keyboard shortcut).
- **Tap anywhere else while the menu is open:** collapses the dropdown
  back to just the circle handle.
- **Reveal strip** (legacy, kept harmless): a tap in the top 60 px
  re-asserts the handle's visibility.

The hit regions are tuned to match where the overlays actually land on
screen rather than where drawable-pixel offsets naively suggest: on a
retina mac mini the Metal drawable is 2x the SDL window points, so a
80 drawable-pixel offset appears at roughly window y=40. Current layout
(all in SDL window coords):

- Handle: top-right **220 x 40 px** (y=0-40).
- Menu: top-right **360 x 70 px** directly below the handle (y=40-110).

The menu surface is rendered as text on a translucent dark pill (24 x 12
px padding) so it reads as a clickable element against any stream
content; the handle is plain white text on purpose to keep its subtle
indicator feel. A future iteration will replace the circle glyph with a
Hydra SVG logo; the hit-region sizing stays the same.

**Required agent flag:** kiosks must run hydraheadflatscreen v2.0.26+
which passes `--absolute-mouse` to the Moonlight stream subcommand.
Without it, clicks are reported near the window centre regardless of
where the visitor physically moves the mouse, and no top-right hit
region is reachable. See the hydraheadflatscreen runbook section
"Mouse cursor is captured / clicks all land near centre of window"
for the full picture.

Implementation lives in `app/streaming/video/overlaymanager.{h,cpp}`
(new `OverlayExitButton` type), `app/streaming/session.{h,cpp}`
(`showExitOverlay`, `isPointInExitOverlay`, `triggerExitFromOverlay`),
`app/streaming/input/mouse.cpp` and `app/streaming/input/abstouch.cpp`
(hit testing and reveal gesture), and `app/streaming/video/ffmpeg-renderers/vt_metal.mm`
plus `.../sdlvid.cpp` (top-center positioning).

Other renderers (plvk, egl, d3d11va, dxva2, vaapi, vdpau, drm) do not yet
position the overlay — the pill falls back to their default render position
there. Kiosk deployments currently run on macOS (vt_metal) or SDL.

## Standard Moonlight Modes

All upstream moonlight-qt commands still work:

```bash
HydraExperienceNet                          # Normal GUI (host picker)
HydraExperienceNet stream <HOST> <APP>      # Direct stream
HydraExperienceNet pair <HOST>              # Pair with host
HydraExperienceNet list <HOST>              # List apps on host
HydraExperienceNet quit <HOST>              # Quit running app
```

## Build (macOS arm64)

Requires Qt 6.7+ and Xcode.

```bash
git clone --recursive https://github.com/cederikdotcom/hydra-experiencenet.git
cd hydra-experiencenet
qmake moonlight-qt.pro
make release
```

For DMG distribution:
```bash
./scripts/generate-dmg.sh Release
```

## Downloading a released DMG

The release server's URL scheme for this project is **version-prefixed** on
both the path segment and the filename. The raw version (e.g. the one in
`latest.json`) does NOT work directly — you must wrap it with `v`.

```bash
# From latest.json, pick up the version:
curl -s https://releases.experiencenet.com/hydraexperiencenet/production/latest.json
# {"version":"6.1.5"}

# Correct download URL (note the leading v AND the version suffix in the filename):
curl -L -fsS \
  https://releases.experiencenet.com/hydraexperiencenet/production/v6.1.5/HydraExperienceNet-v6.1.5.dmg \
  -o HydraExperienceNet.dmg
```

Wrong URLs that will 404 via the mirror:
- `...production/6.1.5/HydraExperienceNet.dmg` (no v, no version suffix)
- `...production/v6.1.5/HydraExperienceNet.dmg` (no version suffix)
- `...production/6.1.5/HydraExperienceNet-v6.1.5.dmg` (no v)

If the release server's mirror path ever 404s, the GitHub release asset URL is
a reliable fallback — `https://github.com/cederikdotcom/hydra-experiencenet/releases/download/v<X.Y.Z>/HydraExperienceNet-v<X.Y.Z>.dmg`.

## Local View API (kiosk mode)

In kiosk mode, the app starts a local HTTP server on `127.0.0.1:9741` with view-layer endpoints:

| Endpoint | Description |
|----------|-------------|
| `GET /api/v1/probe?host=IP&port=PORT` | TCP reachability probe routed through the app's Local Network TCC |
| `POST /api/v1/window/hide` | Hide the kiosk window (stream takes over display) |
| `POST /api/v1/window/show` | Show the kiosk window (stream ended) — enters fullscreen for non-Tool windows |

**Screenshots are not part of this API.** v6.1.24 removed `/api/v1/screenshot`
and the `Screen Recording` TCC request from the kiosk startup path — the
endpoint was being hit by hydracluster dashboard polling and re-triggered a
macOS permission prompt on every kiosk launch. Ops take kiosk screenshots
externally through Terminal's own Screen Recording TCC grant
(`osascript → tell Terminal to do script → /usr/sbin/screencapture`).

## macOS Permissions

The kiosk app requests **Local Network** (for body probes) on first launch.
Screen Recording is no longer requested (see Local View API section above).

The app also prevents display sleep via `IOPMAssertion` while in kiosk mode, replacing the `caffeinate` approach used by the Go service.

To check or reset permissions:
```bash
# Check if screen recording is granted
tccutil reset ScreenCapture com.experiencenet.hydraexperiencenet

# View current TCC state (requires Full Disk Access)
sqlite3 ~/Library/Application\ Support/com.apple.TCC/TCC.db "SELECT * FROM access WHERE service='kTCCServiceScreenCapture'"
```

## Integration with hydraheadflatscreen

hydraheadflatscreen manages the lifecycle:
1. Pre-pairs with Sunshine on the assigned body
2. Launches `HydraExperienceNet kiosk HOST --district D --venue V`
3. User selects an experience from the grid, streams fullscreen
4. On stream end, returns to the grid
5. If the app exits unexpectedly, hydraheadflatscreen relaunches it
6. Screenshot requests on `:9740` are proxied to the Qt app on `:9741`

## Upstream Sync

This fork only adds kiosk mode and branding. All streaming, backend, and existing CLI code is untouched. To sync with upstream:

```bash
git remote add upstream https://github.com/moonlight-stream/moonlight-qt.git
git fetch upstream
git merge upstream/master
```

Conflicts should be minimal — limited to `main.cpp` (new case in switch), `commandlineparser.h/.cpp` (new enum + class), and `app.pro` (new source files).

## Troubleshooting

| Issue | Fix |
|-------|-----|
| "Computer has not been paired" | hydraheadflatscreen must pair first, or run `HydraExperienceNet pair HOST` manually |
| "Failed to connect to HOST" | Check network connectivity and WireGuard tunnel |
| Empty app grid | Sunshine may not have any apps registered, or host is still loading |
| Stream doesn't start | Check Sunshine logs on the body machine |
| Screenshot returns 403 | Screen recording permission not granted. Reset with `tccutil reset ScreenCapture` and relaunch kiosk |
| Screenshot returns 502 | Qt app local server on :9741 not running. Check if kiosk app is running |
