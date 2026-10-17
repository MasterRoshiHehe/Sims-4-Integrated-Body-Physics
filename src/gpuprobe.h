// Read-only research log of the game's skinned vertex shaders (see gpuprobe.cpp).
#pragma once

#include <string>

namespace gpuprobe {
using Log = void (*)(const char* fmt, ...);
// Starts watching (once, at game start). folder ends with a backslash. mode 1: draw statistics and shader dump;
// mode 2: bone palette capture (integratedbodyphysics_palette.bin), no draw hooks.
void start(const std::wstring& folder, Log log, int mode);
// Mode 2: true while a capture window wants this Sim's joints (one Sim per window); then call simFrame.
bool wantSim(const void* actor);
void simFrame(const void* data, unsigned size);
// Mode 6 (breast wave test): each frame, a jiggling Sim's world position and her breast bones' scale products.
bool waveOn();
void waveSim(const void* actor, const float pos[3], float detL, float detR, float toYou);  // toYou: metres from your Sim
}  // namespace gpuprobe
