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

## Local View API (kiosk mode)

In kiosk mode, the app starts a local HTTP server on `127.0.0.1:9741` with view-layer endpoints:

| Endpoint | Description |
|----------|-------------|
| `GET /api/v1/screenshot` | Captures the screen via macOS `screencapture` command (SkyLight), returns JPEG |

The hydraheadflatscreen Go service on `:9740` proxies screenshot requests to this server. This architecture keeps permission-sensitive operations (screen recording) in the `.app` bundle where macOS TCC can properly prompt the user.

## macOS Permissions

On first kiosk launch, the app triggers the macOS Screen Recording permission prompt. The user must click **Allow** in the system dialog. This only needs to happen once.

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
