# Operator PIN exit gate

Provision `operator-pin.sha256` beside HydraExperienceNet.exe before starting
kiosk mode. It contains the lowercase SHA-256 hex digest of a 4–12 digit PIN
(no newline in the hashed PIN). Keep the file and application directory writable
only by SYSTEM/administrators. Never ship a shared PIN in the application ZIP.
Restart the kiosk UI after enabling or changing configuration.

On Windows, the same floating menu also offers **Restart…** and **Shut down…**
when a PIN is configured. Each action opens a fresh PIN prompt with an explicit
warning that all experiences close without saving. Submitting the PIN sends
the selected action to HydraHead's `POST /api/v1/operator/power`; the privileged
head independently verifies the PIN and schedules the action in ten seconds.
The head must have `operator_pin_file` configured to the same protected digest
file. An absent/old head, missing digest, bad PIN or failed Windows command
shows an error; a UI-only PIN success cannot authorize a power operation.
Cancel before submission does nothing. After submission, an HTTP timeout does
not prove the action was cancelled. Shutdown requires physical or remote power
control to turn the machine back on.

`KioskCatalogLoader.qml` retries config/catalog requests when the head is
offline, returns an error or invalid JSON, or takes more than eight seconds.
The retry interval is two seconds. Boot or head updates no longer require
restarting the UI to escape “Loading experiences”.

The floating Exit experience control and Linux in-process stream exit control
request the PIN. Incorrect attempts are throttled for two seconds. Cancel and
Escape close the prompt without ending the experience. The configured state is
cached for the UI lifetime; unreadable, deleted, or malformed configuration fails
closed after configuration has been detected. Installations without the file
retain their existing behavior.

This gates the kiosk UI, not an experience's own Quit button or the agent's
operator control API. The legacy SDL streaming client's native exit controls are
separate; this Windows deployment launches Lunar Strike locally. OS kiosk
policies are still required to prevent access to other Windows tools and files.

Tests:

- `qmake operator-pin.pro && make` (or `jom release` on Windows), then run
  `operator-pin-test`. Windows portable CI runs the native verifier tests.
- `python tests/operator-pin/test_qml.py` from a Python environment with PySide6.
  This loads the actual floating overlay, using a test Session and PIN verifier,
  and checks wrong PIN, Cancel, successful exit, and agent-driven menu return
  clearing a pending PIN prompt without authorizing an exit.
- `python tests/operator-pin/test_power_recovery.py` uses a fake head on local
  port 9740 and real QML to check boot recovery and authorized power actions.
  It never invokes a real shutdown command. Both QML tests run in CI.
