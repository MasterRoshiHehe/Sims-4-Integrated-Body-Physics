// Jolt Physics world for Integrated Body Physics: per Sim, each jiggle bone is a small
// ball held to where the pose puts it by a spring (a six-way constraint with a
// position motor), and the Sims' arms are capsules that push the balls. The
// world steps on its own thread at 120 Hz; the pose hook hands it targets and
// reads back how far each ball sits from its rest place.
#pragma once

#include <cstdint>

namespace jig::phys {

constexpr int kSlots = 96;   // one per Sim (the pose hook's slot index)
constexpr int kBones = 4;    // left/right breast, left/right hip
constexpr int kParts = 12;   // her arms (upper arm, forearm, hand each side), then your forearms, palms, fingers

struct Input {
    void* actor = nullptr;
    float rest[kBones][3];     // where the pose puts each bone (world, metres)
    float frame[kBones][4];    // the Sim's world rotation (x y z w): x side, y up, z front
    bool use[kBones] = {};
    float a[kParts][3], b[kParts][3];  // arm capsule ends (world)
    bool part[kParts] = {};
    float size = 1;            // the Sim's scale (adults 1)
    // This Sim's own values per bone (her size preset, clothed or not), adult metres.
    float hz[kBones] = {}, damping[kBones] = {};
    float lo[kBones][3] = {}, hi[kBones][3] = {};  // how far each ball may move (Sim frame: below/above 0 per axis)
    float gravity[kBones] = {};  // gravity pull, -1..1 (below 0 pushes up)
    float radius[kBones] = {};   // ball radius against her own arms
    float yourRadius[kBones] = {};  // ball radius against your hands
    float mass[kBones] = {};   // kg: heavier lags behind sharp moves and swings back slower (the spring's force is capped)
    bool grip[2] = {};         // your left/right hand closed (fingers curled)
};

// Settings, read every step.
struct Params {
    bool armPush = true;   // arms and hands push the balls
    float armSize = 1;     // arm capsule thickness scale
    bool grab = true;      // your closed hand holds a ball it closed on
    float grabReach = 0.12f;  // metres a held ball may be pulled from its rest place
};

bool start(const Params& params);  // false: no Jolt (the built-in springs stay in use)
void setParams(const Params& params);
void publish(int slot, const Input& in);  // from the pose hook
// How far each ball sits from its rest place, in the Sim's frame (metres),
// and how much it is pressed (direction and depth / ball diameter, Sim frame).
// False until the world has this Sim.
bool offsets(int slot, void* actor, float out[kBones][3], float squish[kBones][3]);

}  // namespace jig::phys
