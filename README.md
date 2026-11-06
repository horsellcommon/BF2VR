# BF2VR Improved 0.3.0

An unofficial Windows x64 VR mod for Star Wars Battlefront II (2017), for offline Arcade and Instant Action. It uses the included native loader and OpenXR.

## Requirements and setup

- Star Wars Battlefront II (2017), Windows x64, and the Microsoft Visual C++ 2015–2022 x64 runtime. Previous builds were tested with the Steam executable version 1.0.117.50617 and SteamVR.
- A connected headset and a configured OpenXR runtime.
- In Battlefront, set DirectX 12 to Off, TAA to High, and window mode to Borderless.

Keep the extracted mod files together in their own folder. Start your OpenXR runtime and enter an offline match. Face forward, run `Loader.exe`, and press Enter at its prompt. Press End to disable the mod. Restart Battlefront before loading the DLL again.

## F7 options overlay

With Battlefront focused, press F7 to open or close the overlay. Use Up/Down to select, Left/Right to change a value, and Enter on Recenter headset. Gameplay continues while the overlay is open.

The panel adjusts world units per metre, menu size, gameplay HUD size, projection render scale, sharpening, and crosshair and marker anchoring. Changes save alongside the DLL. The starting HUD and menu scale is 25%. Projection scale starts at 1.15× and can request up to 2.0× per dimension. Actual dimensions are shown in the panel and depend on the runtime's limits. If a live resize fails, the mod retains the current images. Higher settings can increase GPU and memory use. This setting scales the OpenXR projection images. Battlefront's own rendering resolution is separate.

## Troubleshooting and removal

The local log is in `Documents/BF2VR/BF2VR.txt`. The loader injects a DLL into the running game and security software may flag that behavior. Source is included for review. Treat logs and crash dumps as private before sharing them. To remove the mod, close the game and delete its extracted folder.

## Source and validation

The corresponding source and pinned dependencies are in `BF2VR-0.3.0-source.zip`. All four offline regression suites passed, including the OpenXR allocation cap, live fallback, and overlay layer tests. This release's F7 overlay and 2.0× setting still need a live headset check.

## Credits

Original BF2VR by Ethan Porcaro (IfanSnek). Maintenance and packaging used OpenAI Codex. BF2VR is licensed under GPL-3.0-or-later. See `LICENSE` and the dependency notices for third-party attribution.

[Original BF2VR project](https://github.com/IfanSnek/BF2VR)
