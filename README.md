[![Support me on Ko-fi](https://ko-fi.com/img/githubbutton_sm.svg)](https://ko-fi.com/masterroshihehe)

# Integrated Body Physics

> **18+ only.** This mod adds physics to adult female Sims' bodies, including nude Sims.

A standalone body physics mod for The Sims 4 (PC, DirectX 11). Breasts and butt of young adult,
adult and elder female Sims (yours and NPCs) move with real physics, simulated with
[Jolt Physics](https://github.com/jrouwe/JoltPhysics). It never applies to teens, children or men.

> **This mod is 100% vibe coded.** Every line of code was written by Claude (Anthropic's AI).
> I ([MasterRoshiHehe](https://ko-fi.com/masterroshihehe)) act as the director: I decide what
> the mod should do, test every build and report what works and what doesn't.
> I don't write the code myself. Expect bugs, and read the code before you trust it.

**Status: early development.** Things change and break often.

## Features

- Breast and butt physics with springs, damping, gravity and movement limits.
- Size presets (small, medium, bigger, huge), picked automatically from the Sim's body shape in CAS.
- Separate behaviour for clothed and nude Sims.
- Per-Sim settings from the pie menu (gravity, spacing, cup size, outfits).
- Works on its own. With [Sims4VR](https://github.com/MasterRoshiHehe/Sims4VR) installed, your VR
  hands push and can hold, and the settings also show up in the VR debug menu.

## Settings

Open the settings menu in game with the hotkey (default: `.` on the keyboard, changeable in the
menu). The first page has the basic switches and a Breasts page with its basics; everything
else is under **Advanced**. All settings are also in the mod's `.ini` file.

## Requirements

- The Sims 4 on PC, DirectX 11. Supported game version: 1.128.90.1030.
- Script mods enabled in the game (Options > Game Options > Other).

## Install

1. There is no release yet: the mod is in early development. When there is one, download the zip from the [Releases](../../releases) page. Until then, build it from source (below).
2. Unzip it into `Documents/Electronic Arts/The Sims 4/Mods/`.

## Building from source

Cross-compiled on Linux with MinGW-w64 and CMake; the script part needs Python 3.7 for its `.pyc` files.
Jolt Physics and MinHook are downloaded by CMake.

```sh
cmake -B build -DCMAKE_TOOLCHAIN_FILE=tools/mingw-toolchain.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

## License

MIT, see [LICENSE](LICENSE). Third-party libraries keep their own licenses: Jolt Physics (MIT),
MinHook (BSD 2-Clause).

This is an independent, clean-room project and contains no code from other body physics mods.

The Sims 4 is a trademark of Electronic Arts Inc. This is a fan-made mod and is not affiliated
with or endorsed by Electronic Arts.
