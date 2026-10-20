# Local kiosk tile artwork

Place a JPEG at `artwork/<experience-name>.jpg` beside the native kiosk
executable, then restart the UI while idle. For example, the Windows local
Lunar Strike catalog uses `C:\HydraExperienceNet\artwork\lunar-strike.jpg`.
Keep artwork readable by the kiosk account and writable only by administrators.
Preserve this directory when updating the executable. No remote image service
or network access is needed, and no installation-specific image is bundled
into the application.

Experience names allow letters, digits, underscores and hyphens; missing or
unreadable images fall back to a text card. The grid adapts its column count to
the available width. Cards retain the catalog label and existing launch action.

Check the real screen after provisioning: image appears, build labels remain
readable, and selecting a card launches the same experience as before.
