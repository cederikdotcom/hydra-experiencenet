# Operator PIN exit gate

Provision `operator-pin.sha256` beside HydraExperienceNet.exe before starting
kiosk mode. It contains the lowercase SHA-256 hex digest of a 4–12 digit PIN
(no newline in the hashed PIN). Keep the file and application directory writable
only by SYSTEM/administrators. Never ship a shared PIN in the application ZIP.
Restart the kiosk UI after enabling or changing configuration.

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
