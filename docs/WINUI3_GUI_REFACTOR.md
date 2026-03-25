# WinUI 3 GUI Refactor

## Goal
- Split the debug-facing GUI from the native tray backend.
- Replace the old XAML Islands flyout UI with a WinUI 3 desktop surface that is easier to inspect, iterate on, and eventually test.
- Keep the current native host intact while the new front-end matures.

## What Exists Now
- `AudioPlaybackConnector.cpp` still owns tray hosting, audio connection logic, routing logic, and the native Bluetooth/device surfaces.
- `src/AudioPlaybackConnector.WinUI3` is now the primary GUI shell rather than a read-only prototype.
- The main window layout now follows the same desktop composition direction as `Android-Cam-Bridge`: custom title bar, left navigation rail, top summary cards, and section-based content.
- The new shell focuses on:
  - Simplified Chinese as the default UI language
  - shared config editing
  - launch-at-startup control
  - Windows Bluetooth settings entry point
  - Bluetooth playback device enumeration
  - render output device enumeration
  - connect/disconnect commands for specific Bluetooth playback devices
  - native-host launch/status visibility
  - native device-picker activation and clean exit requests
  - backend runtime-state readback
  - live event-stream driven status updates
  - frontend-side activity logging

## Why This Route
- WinUI 3 does not provide a drop-in replacement for the old UWP `DevicePicker`, so a direct swap inside the current tray window would still leave UI and backend tangled together.
- A separate shell gives us a place to debug settings, device lists, and future backend IPC without destabilizing the current connection path.
- The backend can continue shipping while the new GUI grows into a full host.

## Current Integration Surface
- The native backend now accepts startup/forwarded commands for:
  - `picker`
  - `show-gui`
  - `connect <deviceId>`
  - `disconnect <deviceId>`
  - `reload-settings`
  - `exit`
- The native backend exposes:
  - a command pipe at `\\.\pipe\AudioPlaybackConnector.Command`
  - an event pipe at `\\.\pipe\AudioPlaybackConnector.Events`
- The backend still writes a runtime snapshot to `AudioPlaybackConnector.runtime.json` next to the backend executable as a cold-start fallback.
- Tray behavior has changed:
  - left click opens the WinUI 3 main window
  - right click opens a native quick-settings menu
- `scripts/build.ps1 -BuildWinUI3` bundles the native backend into the WinUI 3 output under `NativeHost`.

## Next Migration Steps
1. Move shared settings/state contracts into a backend-neutral assembly or generated schema.
2. Expand the quick-settings tray menu to cover the subset of controls that should stay available without opening the full GUI.
3. Add structured diagnostics export from the native host instead of relying on `OutputDebugString` and ad-hoc log files.
4. Package the bundled WinUI 3 + native backend payload into release-friendly artifacts and installers.
