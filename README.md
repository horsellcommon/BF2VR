# BF2VR Improved 0.3.0

An unofficial VR mod for Star Wars Battlefront II (2017), for **offline** Arcade and Instant Action. _If you use this mod online then you risk a ban._

## Requirements and setup

- Battlefront II with these settings: DirectX 12 off, TAA High, and Borderless window mode.
- Microsoft Visual C++ 2015–2022 Redistributable for x64.

Keep the extracted mod files together in their own folder. Start your OpenXR runtime and enter an offline match. Face forward, run `Loader.exe`, and press Enter at its prompt. Press End to disable the mod. Restart Battlefront before loading the DLL again.

## Quest controller mappings

| Quest control | Action |
| --- | --- |
| Left stick | Move |
| Right stick | Look |
| Left trigger | Aim / zoom |
| Right trigger | Fire in gameplay / click in Battlefront menus |
| Right A | Jump |
| Right B | Crouch |
| Left X | Left ability |
| Left Y | Right ability |
| Left X + Y | Middle ability |
| Left stick click | Toggle sprint |
| Right stick click | Tap to switch shoulders / hold to switch first or third person |
| Left grip | Open or close the pause menu |

Turn Speed defaults to 1500 and can be adjusted in F7.

## F7 options overlay

With Battlefront focused, press F7 to open or close the overlay. Use Up/Down to select, Left/Right to change a value, and Enter to recenter headset.

The F7 panel adjusts world units per metre, menu size, gameplay HUD size, projection render scale, sharpening, and crosshair and marker anchoring. The starting HUD and menu scale is 25%. Projection scale starts at 1.15× and can request up to 2.0× per dimension. Actual dimensions are shown in the panel and depend on the runtime's limits. If a live resize fails, the mod retains the current images. Higher settings can increase GPU and memory use. This setting scales the OpenXR projection images while Battlefront's own rendering resolution is separate.

## Troubleshooting and removal

The local log is in `Documents/BF2VR/BF2VR.txt`. The loader injects a DLL into the running game and security software may flag that behavior. Source is included for review. Treat logs and crash dumps as private before sharing them. To remove the mod, close the game and delete its extracted folder.

## Source and validation

The corresponding source and pinned dependencies are in `BF2VR-0.3.0-source.zip`. All four offline regression suites passed, including the OpenXR allocation cap, live fallback, and overlay layer tests.

## Credits

Original BF2VR by Ethan Porcaro (IfanSnek). Maintenance and packaging used OpenAI Codex. BF2VR is licensed under GPL-3.0-or-later. See `LICENSE` and the dependency notices for third-party attribution.

[Original BF2VR project](https://github.com/IfanSnek/BF2VR)
