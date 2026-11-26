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
    float root[3] = {};        // the Sim's world position (her root)
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
    // Idle noise (a layer on top of the springs, breasts only): slow coherent noise on the spring's target
    // (breathing), on its stiffness, and tiny random kicks while the Sim walks.
    float edgeFirm = 0.5f;    // bounce speed rises up to this much times near the end of the range (0 = off)
    float softStart = 0.6f;   // where in the range (0..1) it starts to firm up
    float ignoreRoot = 0.5f;  // share of the Sim's own root gliding that the balls simply follow (0 = all of it jiggles them)
    bool idle = true;
    float idleAmp = 1.0f;     // breathing scale (1 = about 2 mm up and down)
    float idleSpeed = 1.0f;   // breathing speed scale (1 = about 13 breaths a minute)
    float idleStiff = 0.1f;   // stiffness varies by this fraction (0 = off)
    float idleSide = 0.05f;   // each side's stiffness differs by up to this fraction (fixed per Sim)
    float idleCircle = 1.0f;  // slow circular drift (side to side and front to back), scale
    float idleKick = 1.0f;    // walking kick scale (0 = off)
};

bool start(const Params& params);  // false: no Jolt (the built-in springs stay in use)
void setParams(const Params& params);
void publish(int slot, const Input& in);  // from the pose hook
// How far each ball sits from its rest place, in the Sim's frame (metres),
// and how much it is pressed (direction and depth / ball diameter, Sim frame).
// False until the world has this Sim.
bool offsets(int slot, void* actor, float out[kBones][3], float squish[kBones][3]);

}  // namespace jig::phys
