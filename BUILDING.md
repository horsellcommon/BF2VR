# Building on Windows

Install Visual Studio 2022 C++ Build Tools with CMake 3.26 or later and a Windows SDK. Run `Build.ps1 -RunTests` in PowerShell to compile x64 Release binaries and run all four offline suites. The pinned dependency trees in `third_party` allow building without fetching sources.

Copy `out/build/Release/BF2VR.dll` and `out/build/Release/Loader.exe` into a mod folder with `EnableHudAnchors.flag`, `HudTransform.hlsl`, `HudScale.txt`, `MarkerProjectionMode.txt`, `MenuScale.txt`, `WorldUnitsPerMeter.txt`, and `XRProjectionScale.txt`. See the README for use. The build script and offline tests do not launch the game.
