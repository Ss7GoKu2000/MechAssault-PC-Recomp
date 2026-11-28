# MechAssault PC Recomp — Rev 1

Unofficial Windows x64 build of the original Xbox MechAssault using [XboxXBERecomp](https://github.com/TGP482/XboxXBERecomp). The release includes a launcher and the tools that build the game code locally. Supply a compatible copy of the game yourself.

## Play the private test build

1. Extract the entire Windows ZIP into a writable folder, such as `Documents\MechAssault-PC`.
2. Run **MechAssault.exe** to open the launcher. Select either your **Xbox disc image** (including `.xiso.iso`) or the **extracted game folder** that directly contains `default.xbe` and the original data.
3. Click **Play**. The game checks the selected executable and builds `MechAssault.dll` on the first launch. Wait for the compiler window to finish, then click its **Start** button.
4. Use the same launcher next time. It remembers your selection locally. Disc images are read directly from the selected location. Leave that file available while playing. You can also drag an ISO or folder onto the launcher.

You need Windows 10 or 11 x64 and a Direct3D 12 GPU. Visual Studio is required only for building the project from source. Keep the package's compiler folder if you expect to update or rebuild the game later.

### Updates

Open **Check for updates** in the launcher. If a newer published release has a Windows ZIP, accept the prompt to download, verify, and install it. The launcher restarts after installation. Saves, settings, and your selected game path are kept. The updated launcher requires public release assets for automatic downloads. **GitHub releases** opens the manual download page.

### Supported disc revision

Use **MechAssault (USA) (En,Fr,De) (Rev 1)**, with a `default.xbe` SHA-1 of:

```text
372A01BBE34B0A12C209126B0978784D724D05DA
```

This is the hash of the executable **inside** the image. Renaming the ISO does not change compatibility. If you have extracted your own image, run `Get-FileHash .\default.xbe -Algorithm SHA1` from that folder to compare. A different pressing, patched executable, or region needs its own build configuration. Keep the complete extracted directory layout, including `Audio`, `media`, `Movie`, and the mission data files.

### Display and diagnostics

- The host reads `brightness_percent` from `settings.toml` beside the game executable. The range is 50–150 and the default is 100.
- Open **Graphics** from the main menu's **Options** screen or select it below **Quit Mission** in Pause. In pause layouts with the separate Graphics button, press **Right** to focus it, then **Enter** or **A**. **F8** also opens the panel. Select **Apply** to preview changes. Confirm display-mode and size changes within 15 seconds to save them, or the previous settings return. **Restore defaults** stages the defaults for review before applying them.
- On keyboard, use the arrow keys to navigate, **Enter** to confirm, and **Escape** or **Backspace** to go back. During a mission, **Escape** opens the pause menu. Movement defaults to **WASD**. Set `key_move_forward`, `key_move_backward`, `key_move_left`, and `key_move_right` in `settings.toml` to decimal Windows virtual-key codes. For letters, use the uppercase ASCII value, such as `87` for W. Restart the game after editing the file.
- Press **F7** to open the in-game **Mouse controls** panel. Use it from a menu or while paused. Adjust **Sensitivity** and **Invert Y**, then close the panel to save the settings.
- During a mission, **left-click** fires the selected weapon. **1** selects energy, **2** selects ballistic, and **3** selects missiles. Scrolling in either direction advances the game's weapon cycle.
- Hold **Space** to activate jump jets on a ’Mech equipped with them. Use `key_jump_jets` in `settings.toml` to rebind it.
- To rebind weapon controls, set `key_fire_weapon`, `key_select_energy`, `key_select_ballistic`, `key_select_missile`, or `key_cycle_weapon` to decimal Windows virtual-key codes in `settings.toml`. For example, `1` is the left mouse button, `2` is the right mouse button, and `0` unbinds an action. Set `mouse_wheel_cycle = false` to disable wheel cycling.
- Mouse movement maps to the right stick while the game window has focus. During gameplay the cursor is hidden and held at the center of the game window. In `settings.toml`, set `mouse_sensitivity` from 0 to 500 (default 100, 0 disables mouse aim and capture) and set `mouse_invert_y = true` to reverse vertical aim.

- To close the game, select **Exit** below **Options** on the main menu.

- Native-aspect gameplay is enabled initially. **F6** toggles back to the original 4:3 gameplay view. Menus and prerecorded movies retain their original proportions.
- **F1** opens the diagnostic overlay and starts the local audio trace. **F2** captures a frame while the overlay is open. Logs and captures are written beside the executable. Share them only after reviewing their contents.
- The campaign was play-tested locally, including the briefing audio and road flicker fixes. Full campaign coverage, split-screen, and other revisions are outside this test build's verification.
- For startup issues, check `game/logs/game.log` and `game/logs/compile.log`. Keep `game/hdd` when moving the installation if you want to retain local saves. From the previous flat ZIP layout, copy `hdd` and `settings.toml` into the new installation's `game` folder.

## Build from the source ZIP

1. Install Visual Studio 2022 with **Desktop development with C++**, **C++ Clang tools for Windows**, and **C++ CMake tools for Windows**. Install Git for dependency retrieval.
2. Extract your compatible disc into `assets` in the project root. Confirm `assets\default.xbe` matches the hash above.
3. Run `build.bat`. CMake fetches third-party dependencies on a clean build. The Windows executable is placed in `out\build\release`.
4. The source ZIP includes the SDK used for this build in `sdk` and the Rev 1 configuration in `config/game.toml`. Review `sdk/README.md` and start from `sdk/templates` when adapting the SDK to another Xbox title.

### Port to another MechAssault revision

Work in a separate checkout or copy. Supply that revision's extracted game files under `assets`, generate a fresh symbol config with the SDK's `xbe_symbols` tool, and recompile. Audit every address in the Rev 1 configuration and `src/game/hooks.cpp`, especially the camera projection and culling hooks. Match each hook to the target revision's instructions, calling convention, and register state before enabling it. Validate boot, saves, audio, and camera behavior on the new revision. Changing the accepted hash alone does not port a revision.

## Release contents and credits

- **Windows test ZIP** — launcher, native host, local compiler and redistribution runtimes.
- **Source ZIP** — launcher and project source, SDK, configuration, build files, and upstream license notices.

Neither archive contains a disc image, Xbox executable, game assets, saved data, or a precompiled game-code DLL. See `sdk/LICENSE` and `sdk/THIRD_PARTY.txt` in source. This project is independent of Microsoft and Day 1 Studios. Camera and frustum hook research draws on the public [CaiusCodes MechAssault PC Recomp](https://github.com/CaiusCodes/MechAssault-Xbox-PC-Recomp).
