# Integrated Body Physics (was Claude Jiggle)

Standalone Sims 4 mod: spring physics on the breast bones (`b__CAS_L/R_Breast__`)
and hip bones (`b__L/R_Skirt__`) of young adult, adult and elder human Sims, NPCs
included. Separate from the Sims 4 VR mod; works with or without it.

- `src/jiggle.cpp`: the DLL (also the Python extension module `_claudejiggle`).
  It redirects the one call to the game's pose-to-joints function (found by byte
  pattern in DX9 and DX11 builds), moves the jiggle bones in the pose's local
  joint array, and calls on. The function itself is not patched, so the VR mod's
  hook on it keeps working.
- `script/claudejiggle/__init__.py`: loads the DLL, sends the qualifying Sims every
  2 s, adds the `jiggle` console command.
- `config/claudejiggle.ini`: settings.

Build (Linux): `cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=tools/mingw-toolchain.cmake -DCMAKE_BUILD_TYPE=Release && cmake --build build`;
script: `python3.7 tools/build_script.py <out>`.
Package: `claudejiggle/` with claudejiggle.dll, .ts4script, .ini and an empty .log
(so writes stay in the MO2 mod folder) -> `D:\Modding\Sims 4\mods\Claude Jiggle\claudejiggle\`.
Log: `claudejiggle.log` in that folder.
Clean-room: never read or copy RTBP.
