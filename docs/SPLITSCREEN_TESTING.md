# Building and testing the splitscreen overhaul

The overhaul (branch `claude/splitscreen-mod-overhaul-qraosx`) was written and checked without
Windows, MCC or controllers: it has not been compiled with MSVC or run. This is the checklist for
the first build and play test.

## Build (Windows)

Needs Visual Studio 2022 (or its Build Tools) with the C++ workload, CMake 3.27+, Git and vcpkg
(for SDL2/SDL2_mixer). These are the same steps CI uses (`.github/workflows/build.yml`):

```powershell
git clone https://github.com/microsoft/vcpkg C:\vcpkg; C:\vcpkg\bootstrap-vcpkg.bat   # once, if vcpkg isn't installed
cd AlphaRing
git checkout claude/splitscreen-mod-overhaul-qraosx
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_TOOLCHAIN_FILE="C:/vcpkg/scripts/buildsystems/vcpkg.cmake" `
  -DVCPKG_TARGET_TRIPLET=x64-windows-static-md
cmake --build build --config Release --target WTSAPI32
```

Output: `build/Release/WTSAPI32.dll`. Back up the DLL you use now, then copy the new one to
`Halo The Master Chief Collection\mcc\binaries\win64\`. Your settings in `alpha_ring_menu.bin`
(same folder) are converted on first launch, so keep a copy of it too in case you go back.

Alternatively enable GitHub Actions on the fork and run the **build** workflow on this branch; the
DLL is attached to the run.

## Logs

`mcc\binaries\win64\alpha_ring_info.log`. Useful lines: `Input: hooked ...` (XInput hooks),
`PlayStation: ...` (controllers found, slots), `Toast: ...` (every on-screen notice),
`MenuSettings: player N took the MCC look ...`, `get_player_profile ...` / `retrive_gamepad_mapping ...`.

## Test checklist (campaign, 2 players)

1. **Start-up**: MCC starts with the DLL; F4 opens the menu. Quit MCC from its menu: no crash on exit.
2. **Controllers**: F4 → Splitscreen → *Assign Controllers*: each player presses a button. Start a mission; each player controls their own view.
3. **Menu from any controller**: on player 2's controller press Start + Left Stick Click: the menu opens on *Player 2*.
4. **MCC binds for player 1**: with player 1 on *MCC Settings*, change a button in MCC's own controller settings and start a mission: the change works.
5. **Own buttons**: player 2 → *Edit My Buttons* → *Jump* → press another button. In game, player 2 jumps with it and player 1's buttons are unchanged. Sprint (Halo 4) and vehicle controls still work.
6. **Sensitivity**: give the two players very different sensitivities (e.g. 2 and 10): each turns at their own speed. Same for *Look: Inverted* on one player.
7. **Pause**: player 2 presses Start: the pause menu opens. Nothing pauses while the F4 menu is open.
8. **Looks**: player 1 should now spawn in their MCC customization (not default green). For player 2: set MCC's Customize to their look, start a mission, F4 → Player 2 → *Look & Armor* → *Copy My Look From MCC Now*. If it doesn't show at once, reload the checkpoint. Then set MCC's customization back for player 1.
9. **PlayStation controllers** (DS4Windows closed; Steam Input off for MCC): turn a DualSense/DualShock 4 on over Bluetooth: a "connected as Controller N" notice; it plays; rumble works; the lightbar shows the player's color. Turn it off mid-mission: "Player N's controller disconnected".
10. **Disconnect notice** with an Xbox controller: unplug it mid-mission, plug it back in.

If something fails, note the step and attach `alpha_ring_info.log` from that session.
