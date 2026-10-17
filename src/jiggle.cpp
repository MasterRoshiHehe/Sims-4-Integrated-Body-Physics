// Integrated Body Physics: soft-body springs on the breast and hip (skirt) bones of the
// Sims 4 human rig, for young adult and older Sims.
//
// The DLL is also a Python extension module (_integratedbodyphysics): the game's Python
// has no ctypes, so the script loads it with importlib. The script tells it
// which Sims qualify (age, frame) every couple of seconds.
//
// How the bones are moved: the game's "pose to joints" step (one call per
// Sim per frame, on the game's job threads) is reached from exactly one call
// instruction. We point that call at our function, which moves the jiggle
// bones in the pose's local joint array and then calls whatever the call went
// to before. The function itself is left untouched, so another mod that hooks
// it (the Sims 4 VR mod does, and checks its first bytes) still works, in
// either load order.
#include <windows.h>
#include <tlhelp32.h>

#include <atomic>
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>

#include "gpuprobe.h"
#include "physics.h"

namespace {

// ---------------------------------------------------------------------------
// Log: integratedbodyphysics.log next to the DLL (the package ships an empty one, so
// the mod manager keeps writes inside the mod's folder).

std::mutex g_logMutex;
FILE* g_log = nullptr;
std::wstring g_folder;

void logLine(const char* fmt, ...) {
    std::lock_guard<std::mutex> lock(g_logMutex);
    if (!g_log) {
        g_log = _wfopen((g_folder + L"integratedbodyphysics.log").c_str(), L"w");
        if (!g_log) return;
    }
    SYSTEMTIME t;
    GetLocalTime(&t);
    std::fprintf(g_log, "%02d:%02d:%02d.%03d ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    va_list args;
    va_start(args, fmt);
    std::vfprintf(g_log, fmt, args);
    va_end(args);
    std::fputc('\n', g_log);
    std::fflush(g_log);
}
#define LOG(...) logLine(__VA_ARGS__)

// ---------------------------------------------------------------------------
// Settings: integratedbodyphysics.ini (shipped), then integratedbodyphysics_user.ini (what was
// changed while playing: the console command or the Sims 4 VR debug menu,
// through ibp_set). All live.

struct Value {
    const char* key;
    float fallback;
    std::atomic<float> v;
};
Value g_values[] = {
    {"on", 1, {1}},                  // 0 = off
    {"own", 1, {1}},                 // your own (the active) Sim jiggles
    {"others", 1, {1}},              // other Sims jiggle
    {"butt", 1, {1}},                // the butt bones
    {"butt_hz", 4.0f, {4.0f}},
    {"butt_damping", 0.3f, {0.3f}},
    {"butt_max", 0.02f, {0.02f}},
    {"with_rtbp", 0, {0}},  // run even when RTBP's d3d11.dll is loaded
    {"butt_up", 1, {1}},
    {"butt_side", 1, {1}},
    {"butt_front", 1, {1}},
    {"butt_gravity", 0, {0}},
    {"butt_size", 0.05f, {0.05f}},
    {"arm_push", 1, {1}},  // arms and hands push them
    {"arm_size", 1, {1}},  // arm thickness for pushing
    {"breasts_on", 1, {1}},  // the breasts jiggle at all
    {"butt_on", 1, {1}},     // the butt jiggles at all
    {"breast_distance", 0, {0}},  // metres from your Sim beyond which they don't jiggle (0 = any distance)
    {"butt_distance", 0, {0}},
    {"butt_clothed_strength", 0.6f, {0.6f}},
    {"butt_clothed_hz", 1.3f, {1.3f}},
    {"butt_clothed_damping", 1.5f, {1.5f}},
    {"butt_clothed_gravity", 0, {0}},
    {"outfit_pie", 1, {1}},  // women's pie menus get outfit changes and Nude (testing)
    {"your_hands", 1, {1}},  // your Sim's hands and arms (the VR hands) push other Sims' breasts
    // Holding (user 2026-10-15 03:54Z): closing your VR hand on a breast holds it until you open it.
    {"grab", 1, {1}},
    {"grab_reach", 0.12f, {0.12f}},  // metres a held breast may be pulled from where it rests
    {"grab_press", 0.6f, {0.6f}},    // how far grip and trigger must both be pressed (0..1) to hold
    // Size presets picked from her CAS breast bone's scale (the measured scale is in the log).
    // (0.11.2's log: 1.38, 2.32, 2.42, 2.61 on four Sims; size_*_at 0.9/1.1/1.3 made everyone Huge.)
    {"cup_medium_at", 1.6f, {1.6f}},
    {"cup_bigger_at", 2.1f, {2.1f}},
    {"cup_huge_at", 2.5f, {2.5f}},
    // A wall down her middle (user 2026-10-15 04:12Z): pushed together, the breasts press into it, not into each other.
    {"middle_wall", 0, {0}},
    // 0.11.4 (user 06:03Z: "extremely glitchy now"): the 0.11 effects start off, to be turned on one at a time.
    {"swing_on", 0, {0}},
    {"squish_on", 0, {0}},
    {"weight_on", 0, {0}},
    // Set to 1 (the debug menu's "Scale the others from <size>", after its are-you-sure): the other three sizes
    // become that size scaled the way the factory presets differ. Never stays 1, never saved as 1.
    // (user 2026-10-16 12:04Z: the same button on every size's page, not just Medium's)
    {"scale_from_small", 0, {0}},
    {"scale_from_medium", 0, {0}},
    {"scale_from_bigger", 0, {0}},
    {"scale_from_huge", 0, {0}},
    // How far your VR hand's fingers close holding a breast / the butt (0 open .. 1 a fist); sims4vr reads
    // these, and its own grab_curl for the rest of the body (user 2026-10-17 14:21Z).
    {"breast_grab_curl", 0.45f, {0.45f}},
    {"butt_grab_curl", 0.45f, {0.45f}},
    // Windows key code that opens this mod's own menu when the Sims 4 VR mod isn't loaded (190 = '.', user
    // 2026-10-17 16:09Z).
    {"menu_key", 190, {190}},
    // 1 at game start: the read-only GPU probe logs how the game draws Sims (gpuprobe.cpp; research for the
    // bulge, wave and butt shader features, user 2026-10-16 19:14Z). Changes no drawing.
    {"shader_probe", 0, {0}},
};
enum {
    kOn, kOwn, kOthers, kButt, kButtHz, kButtDamping, kButtMax, kWithRtbp, kButtUp, kButtSide, kButtFront, kButtGravity,
    kButtSize, kArmPush, kArmSize, kBreastsOn, kButtOn, kBreastDistance, kButtDistance, kButtClothedStrength,
    kButtClothedHz, kButtClothedDamping, kButtClothedGravity, kOutfitPie, kYourHands, kGrab, kGrabReach, kGrabPress,
    kSizeMediumAt, kSizeBiggerAt, kSizeHugeAt, kBreastWall, kSwingOn, kSquishOn, kWeightOn, kScaleFromSmall, kScaleFromMedium,
    kScaleFromBigger, kScaleFromHuge, kBreastGrabCurl, kButtGrabCurl, kMenuKey, kShaderProbe, kValues
};
static_assert(sizeof(g_values) / sizeof(g_values[0]) == kValues, "g_values and its enum differ");
float val(int i) { return g_values[i].v.load(std::memory_order_relaxed); }

// Breast size presets (user 2026-10-15 03:54Z): every breast setting four times,
// as "<size>_breast_<field>" (small_breast_hz ...). Each Sim uses the one her
// CAS size picks, or the one her pie Cup size sets.
constexpr int kSizes = 4;
const char* const kSizeNames[kSizes] = {"small", "medium", "bigger", "huge"};
struct Field {
    const char* name;
    const char* legacy;  // the 0.10.0 single setting it starts from (scaled per size), if any
    float factory[kSizes];
};
const Field kFields[] = {
    {"strength", "breasts", {1.2f, 1.5f, 1.7f, 1.9f}},  // 1 = the physical spring
    {"hz", "breast_hz", {3.8f, 3.0f, 2.6f, 2.2f}},       // how fast they swing back
    {"damping", "breast_damping", {0.3f, 0.2f, 0.17f, 0.15f}},  // 0 = never settles, 1 = no swing past rest
    {"max_up", "breast_max", {0.02f, 0.03f, 0.04f, 0.05f}},     // metres they may move up (adult size)
    {"max_down", "breast_max", {0.025f, 0.04f, 0.05f, 0.065f}},  // down
    {"max_side", "breast_max", {0.02f, 0.03f, 0.04f, 0.05f}},   // to each side
    {"max_front", "breast_max", {0.015f, 0.025f, 0.03f, 0.04f}},  // forward and back
    {"up", "breast_up", {1, 1, 1, 1}},  // each direction's share of the movement, in the Sim's frame
    {"side", "breast_side", {1, 1, 1, 1}},
    {"front", "breast_front", {1, 1, 1, 1}},
    {"gravity", "breast_gravity", {0, 0, 0, 0}},  // -1..1: how much gravity pulls them (the pose already hangs them)
    {"size", "breast_size", {0.035f, 0.045f, 0.055f, 0.065f}},  // radius (metres) her own arms push against
    {"size_you", "breast_size", {0.035f, 0.045f, 0.055f, 0.065f}},  // radius your hands push against
    {"apart", "breast_apart", {0, 0, 0, 0}},  // metres the gap between them grows (below 0: pushed together)
    {"swing", nullptr, {0.3f, 0.4f, 0.5f, 0.6f}},   // how much they tilt with the movement (0 = only shift)
    {"weight", nullptr, {0.3f, 0.5f, 0.9f, 1.4f}},  // kg each: heavier lags behind sharp moves, less lively
    {"scale", nullptr, {1, 1, 1, 1}},  // how big they look, times her CAS size (their pushing sizes grow with it)
    {"squish", nullptr, {0.4f, 0.5f, 0.6f, 0.7f}},  // how much they flatten where pressed (0 = never)
    // Clothed (a top covers them): strength, bounce speed and damping times these; gravity and apart added.
    {"clothed_strength", "breast_clothed_strength", {0.6f, 0.6f, 0.6f, 0.6f}},
    {"clothed_hz", "breast_clothed_hz", {1.3f, 1.3f, 1.3f, 1.3f}},
    {"clothed_damping", "breast_clothed_damping", {1.5f, 1.5f, 1.5f, 1.5f}},
    {"clothed_gravity", "breast_clothed_gravity", {0, 0, 0, 0}},  // below 0: the top pushes them up
    {"clothed_apart", "breast_clothed_apart", {0, 0, 0, 0}},
};
enum {
    fStrength, fHz, fDamping, fMaxUp, fMaxDown, fMaxSide, fMaxFront, fUp, fSide, fFront, fGravity, fSize, fSizeYou,
    fApart, fSwing, fWeight, fScale, fSquish, fClothedStrength, fClothedHz, fClothedDamping, fClothedGravity, fClothedApart, kFields_
};
constexpr int kFieldCount = static_cast<int>(sizeof(kFields) / sizeof(kFields[0]));
static_assert(kFieldCount == kFields_, "kFields and its enum differ");
char g_presetKeys[kSizes][kFieldCount][40];
Value g_preset[kSizes][kFieldCount];
float pv(int size, int field) { return g_preset[size][field].v.load(std::memory_order_relaxed); }

void initPresets() {
    for (int z = 0; z < kSizes; ++z)
        for (int f = 0; f < kFieldCount; ++f) {
            std::snprintf(g_presetKeys[z][f], sizeof(g_presetKeys[z][f]), "%s_breast_%s", kSizeNames[z], kFields[f].name);
            g_preset[z][f].key = g_presetKeys[z][f];
            g_preset[z][f].fallback = kFields[f].factory[z];
            g_preset[z][f].v = kFields[f].factory[z];
        }
}

template <typename F>
void forEachValue(F&& fn) {
    for (Value& v : g_values) fn(v);
    for (auto& size : g_preset)
        for (Value& v : size) fn(v);
}

Value* findValue(const char* key) {
    if (!key) return nullptr;
    Value* found = nullptr;
    forEachValue([&](Value& v) {
        if (!found && !_stricmp(v.key, key)) found = &v;
    });
    return found;
}

bool readIni(const std::wstring& path, const char* key, float& out) {
    char buffer[64];
    GetPrivateProfileStringA("jiggle", key, "", buffer, sizeof(buffer), std::string(path.begin(), path.end()).c_str());
    char* end = nullptr;
    float x = std::strtof(buffer, &end);
    if (!buffer[0] || end == buffer || !std::isfinite(x)) return false;
    out = x;
    return true;
}

void readSettings() {
    std::wstring shipped = g_folder + L"integratedbodyphysics.ini", user = g_folder + L"integratedbodyphysics_user.ini";
    for (Value& v : g_values) {
        float f = v.fallback;
        for (const std::wstring* path : {&shipped, &user}) readIni(*path, v.key, f);
        v.v = f;
    }
    // Presets: their own keys; else, the first time, the 0.10.0 single setting (what you had tuned), as the
    // Medium value with the other sizes scaled from it like the factory ones.
    int migrated = 0;
    for (int f = 0; f < kFieldCount; ++f) {
        float legacy = 0;
        bool haveLegacy = false;
        if (kFields[f].legacy)
            for (const std::wstring* path : {&shipped, &user}) haveLegacy |= readIni(*path, kFields[f].legacy, legacy);
        for (int z = 0; z < kSizes; ++z) {
            Value& v = g_preset[z][f];
            float x = v.fallback;
            bool own = false;
            for (const std::wstring* path : {&shipped, &user}) own |= readIni(*path, v.key, x);
            if (!own && haveLegacy) {
                float medium = kFields[f].factory[1];
                x = std::fabs(medium) > 1e-6f ? legacy * kFields[f].factory[z] / medium
                                              : legacy + kFields[f].factory[z] - medium;
                ++migrated;
            }
            v.v = x;
        }
    }
    if (migrated) {
        LOG("Settings: %d size preset values started from your 0.10.0 breast settings", migrated);
    }
    LOG("Settings: on %.0f, medium breasts %.2f (%.1f Hz, damping %.2f, up %.3f / down %.3f m), butt %.2f",
        val(kOn), pv(1, fStrength), pv(1, fHz), pv(1, fDamping), pv(1, fMaxUp), pv(1, fMaxDown), val(kButt));
}

// Saves changed values a second after the last change, on a worker thread
// that stays for the rest of the game.
std::atomic<ULONGLONG> g_saveAt{0};
std::atomic<bool> g_saver{false};
void saveSoon() {
    g_saveAt = GetTickCount64() + 1000;
    if (g_saver.exchange(true)) return;
    CreateThread(nullptr, 0, [](LPVOID) -> DWORD {
        for (;;) {
            Sleep(250);
            ULONGLONG at = g_saveAt.load();
            if (!at || GetTickCount64() < at || !g_saveAt.compare_exchange_strong(at, 0)) continue;
            std::string path(g_folder.begin(), g_folder.end());
            path += "integratedbodyphysics_user.ini";
            forEachValue([&](Value& v) {
                char text[32];
                std::snprintf(text, sizeof(text), "%g", v.v.load());
                WritePrivateProfileStringA("jiggle", v.key, text, path.c_str());
            });
            LOG("Saved to integratedbodyphysics_user.ini");
        }
    }, nullptr, 0, nullptr);
}

jig::phys::Params physicsParams() {
    jig::phys::Params p;
    p.armPush = val(kArmPush) != 0;
    p.armSize = val(kArmSize);
    p.grab = val(kGrab) != 0;
    p.grabReach = val(kGrabReach);
    return p;
}
std::atomic<bool> g_physics{false};  // the Jolt world runs (else the built-in springs)

// Small, Bigger and Huge from Medium, each field the way the factory presets differ from Medium.
void scaleFrom(int from) {
    static const char* const kNames[kSizes] = {"Small", "Medium", "Bigger", "Huge"};
    for (int f = 0; f < kFieldCount; ++f) {
        float source = pv(from, f), factorySource = kFields[f].factory[from];
        for (int z = 0; z < kSizes; ++z) {
            if (z == from) continue;
            float x = std::fabs(factorySource) > 1e-6f ? source * kFields[f].factory[z] / factorySource
                                                       : source + kFields[f].factory[z] - factorySource;
            g_preset[z][f].v = x;
        }
    }
    LOG("The other sizes set from %s", kNames[from]);
    saveSoon();
}

bool setValue(const char* key, float f) {
    Value* v = findValue(key);
    if (!v || !std::isfinite(f)) return false;
    if (v >= &g_values[kScaleFromSmall] && v <= &g_values[kScaleFromHuge]) {
        if (f != 0) scaleFrom(int(v - &g_values[kScaleFromSmall]));
        return true;
    }
    if (v->v.exchange(f) != f) {
        LOG("Set %s = %g", v->key, f);
        saveSoon();
        if (g_physics) jig::phys::setParams(physicsParams());
    }
    return true;
}

// ---------------------------------------------------------------------------
// Maths (game: right-handed, Y up)

struct Vec3 {
    float x, y, z;
};
struct Quat {
    float x, y, z, w;
};
Vec3 add(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 sub(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 mul(Vec3 a, float k) { return {a.x * k, a.y * k, a.z * k}; }
float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec3 cross(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
float length(Vec3 a) { return std::sqrt(dot(a, a)); }
bool finite(Vec3 a) { return std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z); }
Quat conj(const Quat& q) { return {-q.x, -q.y, -q.z, q.w}; }
Vec3 rotate(const Quat& q, Vec3 v) {
    Vec3 u{q.x, q.y, q.z};
    Vec3 t = mul(cross(u, v), 2);
    return add(add(v, mul(t, q.w)), cross(u, t));
}

// ---------------------------------------------------------------------------
// Game layout (1.121.361.1020, same in the DX9 and DX11 builds)

struct alignas(16) Xf {
    float p[4];
    float q[4];
    float s[4];
};
constexpr size_t kPoseRig = 0x30, kPoseLocals = 0x38, kPoseJointCount = 0x5e;
constexpr size_t kRigSkeleton = 0x48;
constexpr size_t kActorPosition = 0x2e0, kActorRotation = 0x2f0, kActorScale = 0x300;
constexpr uint32_t kMaxJoints = 200;

using PoseToJointsFn = void (*)(void* actor, void* pose);
using HandsFn = int (*)(float* out, unsigned long long* held);  // the Sims 4 VR mod's s4vr_hands
using ModelTransformFn = Xf* (*)(void* pose, Xf* out, uint32_t joint, void* extra);
using JointIndexFn = uint32_t (*)(void* skeleton, uint32_t hash);
using JointCountFn = uint32_t (*)(void* skeleton);
using ParentFn = int (*)(void* skeleton, uint32_t joint);
using LookupFn = void* (*)(const uint64_t* id);

PoseToJointsFn g_next = nullptr;  // what the patched call went to
ModelTransformFn g_model = nullptr;
JointIndexFn g_jointIndex = nullptr;
JointCountFn g_jointCount = nullptr;
ParentFn g_parent = nullptr;
LookupFn g_lookup = nullptr;

// Byte patterns (0x100 = any byte). Each matches once in both game builds.
// The caller: tests the actor, gets its pose, then `call PoseToJoints` at +0x4e.
const uint16_t kCaller[] = {0x48, 0x85, 0xd2, 0x74, 0x100, 0x53, 0x48, 0x83, 0xec, 0x30, 0x48, 0xc7, 0x44, 0x24, 0x20,
                            0xfe, 0xff, 0xff, 0xff, 0x48, 0x8b, 0xda, 0x41, 0xf6, 0xc1, 0x04, 0x74, 0x100, 0xb2, 0x01,
                            0x48, 0x8b, 0xcb, 0xe8, 0x100, 0x100, 0x100, 0x100, 0x84, 0xc0, 0x74, 0x100, 0x41, 0xb8,
                            0x0a, 0x00, 0x00, 0x00, 0x48, 0x8b, 0xd3, 0x48, 0x8d, 0x4c, 0x24, 0x48, 0xe8, 0x100, 0x100,
                            0x100, 0x100, 0x90, 0x33, 0xd2, 0x48, 0x8b, 0xcb, 0xe8, 0x100, 0x100, 0x100, 0x100, 0x48,
                            0x8b, 0xd0, 0x48, 0x8b, 0xcb, 0xe8};
constexpr size_t kCallerCall = 0x4e;
const uint16_t kModel[] = {0x48, 0x89, 0x5c, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57, 0x48, 0x81, 0xec,
                           0x90, 0x00, 0x00, 0x00, 0x0f, 0x28, 0x05, 0x100, 0x100, 0x100, 0x100, 0x48, 0x8b, 0xfa,
                           0x44, 0x0f, 0x29, 0x54, 0x24, 0x40, 0x48, 0x8b, 0xf1};
const uint16_t kJointIndex[] = {0x48, 0x83, 0xec, 0x18, 0x44, 0x8b, 0xc2, 0x48, 0xb8, 0xc5, 0x4e, 0xec, 0xc4,
                                0x4e, 0xec, 0xc4, 0x4e, 0x44, 0x8b, 0xca, 0x4c, 0x8b, 0xd1, 0x49, 0xf7, 0xe1};
const uint16_t kJointCount[] = {0x48, 0x8b, 0xc1, 0x48, 0x8b, 0x49, 0x10, 0x48, 0x2b, 0x48, 0x08, 0x48, 0xb8, 0x25, 0x49, 0x92};
const uint16_t kParent[] = {0x8b, 0xc2, 0x48, 0x6b, 0xd0, 0x70, 0x48, 0x8b, 0x41, 0x08, 0x8b, 0x44, 0x02, 0x58, 0xc3, 0xcc};
const uint16_t kLookup[] = {0x40, 0x53, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b, 0xd9, 0xe8, 0x100, 0x100, 0x100, 0x100,
                            0x48, 0x85, 0xc0, 0x74, 0x15, 0x48, 0x8b, 0x10, 0x48, 0x8b, 0xc8, 0x4c, 0x8b, 0x42, 0x20,
                            0x48, 0x8b, 0xd3, 0x48, 0x83, 0xc4, 0x20, 0x5b, 0x49, 0xff, 0xe0};

template <size_t N>
uint8_t* find(const uint16_t (&pattern)[N]) {
    auto base = reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr));
    auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    auto nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    IMAGE_SECTION_HEADER* section = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
        if (!(section->Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
        uint8_t* begin = base + section->VirtualAddress;
        uint8_t* end = begin + section->Misc.VirtualSize - N;
        for (uint8_t* p = begin; p <= end; ++p) {
            if (p[0] != pattern[0]) continue;
            size_t k = 1;
            while (k < N && (pattern[k] > 0xff || p[k] == pattern[k])) ++k;
            if (k == N) return p;
        }
    }
    return nullptr;
}

uint32_t jointHash(const char* name) {  // FNV-1 over lower-case characters, as the game does
    uint32_t h = 0x811c9dc5;
    for (; *name; ++name) {
        char c = *name;
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        h = (h * 0x1000193) ^ static_cast<uint8_t>(c);
    }
    return h;
}

Xf modelOf(void* pose, int joint) {
    Xf t{};
    g_model(pose, &t, static_cast<uint32_t>(joint), nullptr);
    return t;
}

// ---------------------------------------------------------------------------
// Which Sims jiggle (from the script): actor pointer and strength.

constexpr int kMaxSims = 96;
std::atomic<void*> g_simActor[kMaxSims];
std::atomic<float> g_simStrength[kMaxSims], g_simButt[kMaxSims];
std::atomic<bool> g_simOwn[kMaxSims];  // the active Sim (the one you play)
std::atomic<uint32_t> g_simClothed[kMaxSims];  // 1: top covers the breasts, 2: bottom covers the butt
std::atomic<float> g_simGravity[kMaxSims];  // her own nude breast gravity (pie Gravity preset, -1..1); NaN = the setting
std::atomic<float> g_simApart[kMaxSims];    // her own nude breasts apart (pie Spacing preset, metres); NaN = the setting
std::atomic<int> g_simCup[kMaxSims];        // her pie Cup size (0 small .. 3 huge); -1 = from her CAS size
// The active Sim, and where it was the last time the game posed it (for the distance settings).
std::atomic<void*> g_ownActor{nullptr};
std::atomic<float> g_ownPos[3];
std::atomic<ULONGLONG> g_ownSeen{0};

// Too far from your Sim for this part to jiggle (its distance setting; 0 = any distance).
bool tooFar(float distanceSq, int key) {
    float limit = val(key);
    return limit > 0 && distanceSq > limit * limit;
}
std::atomic<int> g_simCount{0};

struct SimLook {
    float breasts = 0, butt = 0;
    uint32_t clothed = 0;
    float gravity = NAN, apart = NAN;
    int cup = -1;
};
bool strengthOf(void* actor, SimLook& o) {
    int n = g_simCount.load(std::memory_order_acquire);
    for (int i = 0; i < n; ++i)
        if (g_simActor[i].load(std::memory_order_relaxed) == actor) {
            if (val(g_simOwn[i].load(std::memory_order_relaxed) ? kOwn : kOthers) == 0) return false;
            o.breasts = g_simStrength[i].load(std::memory_order_relaxed);
            o.butt = g_simButt[i].load(std::memory_order_relaxed);
            o.clothed = g_simClothed[i].load(std::memory_order_relaxed);
            o.gravity = g_simGravity[i].load(std::memory_order_relaxed);
            o.apart = g_simApart[i].load(std::memory_order_relaxed);
            o.cup = g_simCup[i].load(std::memory_order_relaxed);
            return o.breasts > 0 || o.butt > 0;
        }
    return false;
}

// ---------------------------------------------------------------------------
// Springs. Each bone carries a point mass that follows where the pose puts
// the bone; the bone is drawn where the mass is. Damping works against the
// mass's speed relative to the bone's, so walking at a steady pace leaves the
// bone at rest and only speeding up, stopping, steps and turns set it moving.

enum Bone { kLeftBreast, kRightBreast, kLeftHip, kRightHip, kBones };
const char* const kBoneNames[kBones] = {"b__CAS_L_Breast__", "b__CAS_R_Breast__", "b__L_Butt__", "b__R_Butt__"};
// The game's rig has no butt bone. Some body meshes weight part of each cheek to b__L/R_Butt__
// (EA's female nude bottom: up to 27%), which only exist when a mod's rig adds them. Without
// them, the thigh twist bones carry the lower butt and upper thighs (the same mesh: half or
// more of each vertex from 0.6 to 1.1 m, all round), so those jiggle.
const char* const kFallbackNames[kBones] = {nullptr, nullptr, "b__L_ThighTwist__", "b__R_ThighTwist__"};

struct Spring {
    Vec3 mass, speed;  // world
    Vec3 rest;         // where the pose put the bone last time (world)
    bool started;
    Xf base;   // the bone's local transform from the game
    Xf wrote;  // and what we wrote over it
    bool written;
};
// Arm capsules for the physics: upper arms, forearms, hands (left, right): the Sim's own (parts 0-5).
constexpr int kOwnParts = 6;
const char* const kPartEnds[kOwnParts][2] = {
    {"b__L_UpperArm__", "b__L_Forearm__"}, {"b__R_UpperArm__", "b__R_Forearm__"}, {"b__L_Forearm__", "b__L_Hand__"},
    {"b__R_Forearm__", "b__R_Hand__"},     {"b__L_Hand__", "b__L_Mid1__"},        {"b__R_Hand__", "b__R_Mid1__"}};

struct SimState {
    void* rig;
    bool buttBones;  // the rig has b__L/R_Butt__ (else the thigh twist bones stand in)
    uint32_t count;
    int bone[kBones];
    int parent[kBones];
    int part[kOwnParts][2];
    jig::phys::Input input;
    LARGE_INTEGER last;
    Spring spring[kBones];
    float measured;  // her CAS breast bone's scale (for the size presets)
};
struct Slot {
    std::atomic<void*> actor{nullptr};
    std::atomic<ULONGLONG> seen{0};
    std::atomic<int> autoSize{1};  // the size preset her CAS size picks (0 small .. 3 huge)
    std::atomic<float> measured{0};
    std::atomic_flag busy = ATOMIC_FLAG_INIT;
    SimState st;
};
constexpr int kSlots = 96;
Slot g_slots[kSlots];
LARGE_INTEGER g_freq;

Slot* slotFor(void* actor, ULONGLONG now) {
    for (Slot& s : g_slots)
        if (s.actor.load(std::memory_order_relaxed) == actor) {
            s.seen.store(now, std::memory_order_relaxed);
            return &s;
        }
    for (Slot& s : g_slots) {
        void* old = s.actor.load(std::memory_order_relaxed);
        if (old && now - s.seen.load(std::memory_order_relaxed) < 3000) continue;
        if (s.busy.test_and_set(std::memory_order_acquire)) continue;
        if (s.actor.compare_exchange_strong(old, actor)) {
            s.st = SimState{};
            s.seen.store(now, std::memory_order_relaxed);
            s.busy.clear(std::memory_order_release);
            return &s;
        }
        s.busy.clear(std::memory_order_release);
    }
    return nullptr;
}

std::atomic<int> g_rigLogs{0}, g_checkLogs{0}, g_buttCheckLogs{0};

void resolve(SimState& st, uint8_t* rig, uint32_t count) {
    st.rig = rig;
    st.count = count;
    uint8_t* skeleton = rig + kRigSkeleton;
    bool buttBones = false;
    for (int b = 0; b < kBones; ++b) {
        uint32_t j = g_jointIndex(skeleton, jointHash(kBoneNames[b]));
        if (b == kLeftHip) buttBones = j < count;
        if (j >= count && kFallbackNames[b]) j = g_jointIndex(skeleton, jointHash(kFallbackNames[b]));
        st.bone[b] = j < count ? static_cast<int>(j) : -1;
        st.parent[b] = st.bone[b] >= 0 ? g_parent(skeleton, j) : -1;
        st.spring[b] = Spring{};
    }
    for (int k = 0; k < kOwnParts; ++k)
        for (int e = 0; e < 2; ++e) {
            uint32_t j = g_jointIndex(skeleton, jointHash(kPartEnds[k][e]));
            st.part[k][e] = j < count ? static_cast<int>(j) : -1;
        }
    st.buttBones = buttBones;
    if (g_rigLogs.fetch_add(1) < 20)
        LOG("Rig %p (%u joints): breasts %d/%d (parents %d/%d), butt %d/%d (parents %d/%d, %s)", rig, count, st.bone[0],
            st.bone[1], st.parent[0], st.parent[1], st.bone[2], st.bone[3], st.parent[2], st.parent[3],
            buttBones ? "butt bones" : "no butt bones: thigh twist bones");
}

// Stats for the log, once a minute.
std::atomic<long long> g_ticks{0}, g_calls{0};
std::atomic<int> g_simsSeen{0};
std::atomic<ULONGLONG> g_statsAt{0};

Quat qmul(const Quat& a, const Quat& b) {
    return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}

// Each direction's share of a move (world), in the Sim's own frame: x side, y up, z front.
Vec3 shaped(Vec3 move, const Quat& wq, Vec3 share) {
    Vec3 m = rotate(conj(wq), move);
    m.x *= share.x;
    m.y *= share.y;
    m.z *= share.z;
    return rotate(wq, m);
}

// The move (world), back into the parent bone's frame, onto the local position.
void writeMove(Xf* locals, int j, Spring&, Vec3 move, const Quat& wq, Vec3 ws, const Quat& pq, const Xf& parent) {
    if (!(parent.s[0] > 1e-4f && parent.s[1] > 1e-4f && parent.s[2] > 1e-4f)) return;
    float* local = locals[j].p;
    Vec3 m = rotate(conj(wq), move);
    m = {m.x / ws.x, m.y / ws.y, m.z / ws.z};
    m = rotate(conj(pq), m);
    m = {m.x / parent.s[0], m.y / parent.s[1], m.z / parent.s[2]};
    local[0] += m.x;
    local[1] += m.y;
    local[2] += m.z;
}

// The rotation whose x, y and z axes are these (orthonormal, right-handed).
Quat fromAxes(Vec3 x, Vec3 y, Vec3 z) {
    float t = x.x + y.y + z.z;
    Quat q;
    if (t > 0) {
        float s = std::sqrt(t + 1) * 2;
        q = {(y.z - z.y) / s, (z.x - x.z) / s, (x.y - y.x) / s, 0.25f * s};
    } else if (x.x > y.y && x.x > z.z) {
        float s = std::sqrt(1 + x.x - y.y - z.z) * 2;
        q = {0.25f * s, (y.x + x.y) / s, (z.x + x.z) / s, (y.z - z.y) / s};
    } else if (y.y > z.z) {
        float s = std::sqrt(1 + y.y - x.x - z.z) * 2;
        q = {(y.x + x.y) / s, 0.25f * s, (z.y + y.z) / s, (z.x - x.z) / s};
    } else {
        float s = std::sqrt(1 + z.z - x.x - y.y) * 2;
        q = {(z.x + x.z) / s, (z.y + y.z) / s, 0.25f * s, (x.y - y.x) / s};
    }
    return q;
}

Quat axisAngle(Vec3 axis, float angle) {
    float h = 0.5f * angle, sn = std::sin(h);
    return {axis.x * sn, axis.y * sn, axis.z * sn, std::cos(h)};
}

// Swing: the bone turns with its movement (a breast that drops points down),
// about the axis across its lever from the parent bone. Squish: flattened along
// the press, wider across it, volume kept (scale in the bone's own axes).
// move: the shown jiggle (world, no spacing); press: Sim frame.
void shapeBone(Xf& local, Vec3 move, Vec3 press, float swing, float squish, const Quat& wq, Vec3 ws, const Quat& pq,
               Vec3 lever) {
    Quat lq{local.q[0], local.q[1], local.q[2], local.q[3]};
    bool turned = false;
    float L = length(lever);
    if (swing > 0 && L > 1e-3f) {
        Vec3 o = rotate(conj(wq), move);
        o = {o.x / ws.x, o.y / ws.y, o.z / ws.z};
        Vec3 axis = cross(mul(lever, 1 / L), o);
        float across = length(axis);
        if (across > 1e-5f) {
            float angle = std::min(0.6f, swing * std::atan2(across, L));
            Quat r = axisAngle(mul(axis, 1 / across), angle);
            lq = qmul(conj(pq), qmul(r, qmul(pq, lq)));
            float n = std::sqrt(lq.x * lq.x + lq.y * lq.y + lq.z * lq.z + lq.w * lq.w);
            if (n > 1e-6f && std::isfinite(n)) {
                lq = {lq.x / n, lq.y / n, lq.z / n, lq.w / n};
                turned = true;
            }
        }
    }
    if (turned) {
        local.q[0] = lq.x;
        local.q[1] = lq.y;
        local.q[2] = lq.z;
        local.q[3] = lq.w;
    } else {
        lq = {local.q[0], local.q[1], local.q[2], local.q[3]};
    }
    float pressed = length(press);
    float a = std::min(0.5f, pressed * squish);
    if (a > 0.005f && std::isfinite(a)) {
        Vec3 n = mul(press, 1 / pressed);
        Vec3 nb = rotate(conj(qmul(pq, lq)), n);
        float along = 1 - a, wide = 1 / std::sqrt(along);
        local.s[0] *= wide + (along - wide) * nb.x * nb.x;
        local.s[1] *= wide + (along - wide) * nb.y * nb.y;
        local.s[2] *= wide + (along - wide) * nb.z * nb.z;
    }
}

// Your Sim's forearms, palms and fingers (user 2026-10-15 03:18Z: the VR hands
// touching other Sims' breasts, PLANCK-style): taken after the game and the
// Sims 4 VR arm IK posed your Sim, and added to every other Sim's physics
// world as parts 6-11 while they are within reach.
const char* const kYourEnds[jig::phys::kParts - kOwnParts][2] = {
    {"b__L_Forearm__", "b__L_Hand__"}, {"b__R_Forearm__", "b__R_Hand__"}, {"b__L_Hand__", "b__L_Mid0__"},
    {"b__R_Hand__", "b__R_Mid0__"},    {"b__L_Mid0__", "b__L_Mid2__"},    {"b__R_Mid0__", "b__R_Mid2__"}};
constexpr float kYourReach = 2.0f;  // metres from her middle: farther, your hands aren't added
struct YourHands {
    SRWLOCK lock = SRWLOCK_INIT;
    void* rig = nullptr;
    uint32_t count = 0;
    int ends[jig::phys::kParts - kOwnParts][2];
    float a[jig::phys::kParts - kOwnParts][3], b[jig::phys::kParts - kOwnParts][3];
    bool ok[jig::phys::kParts - kOwnParts] = {};
    bool grip[2] = {};  // left/right controller: grip and trigger both pressed
    ULONGLONG at = 0;
} g_you;

void captureYourHands(uint8_t* actor, uint8_t* pose) {
    uint8_t* rig = *reinterpret_cast<uint8_t**>(pose + kPoseRig);
    uint32_t poseCount = *reinterpret_cast<uint16_t*>(pose + kPoseJointCount);
    if (!rig) return;
    uint32_t count = g_jointCount(rig + kRigSkeleton);
    if (count > poseCount) count = poseCount;
    Xf world{};
    std::memcpy(world.p, actor + kActorPosition, sizeof(world.p));
    std::memcpy(world.q, actor + kActorRotation, sizeof(world.q));
    std::memcpy(world.s, actor + kActorScale, sizeof(world.s));
    Quat wq{world.q[0], world.q[1], world.q[2], world.q[3]};
    Vec3 wp{world.p[0], world.p[1], world.p[2]};
    AcquireSRWLockExclusive(&g_you.lock);
    if (g_you.rig != rig || g_you.count != count) {
        g_you.rig = rig;
        g_you.count = count;
        for (int k = 0; k < jig::phys::kParts - kOwnParts; ++k)
            for (int e = 0; e < 2; ++e) {
                uint32_t j = g_jointIndex(rig + kRigSkeleton, jointHash(kYourEnds[k][e]));
                g_you.ends[k][e] = j < count ? static_cast<int>(j) : -1;
            }
        static std::atomic<int> logs{0};
        if (logs.fetch_add(1) < 5)
            LOG("Your hands: rig %p, parts %d %d %d %d %d %d", rig, g_you.ends[0][1], g_you.ends[1][1], g_you.ends[2][1],
                g_you.ends[3][1], g_you.ends[4][1], g_you.ends[5][1]);
    }
    for (int k = 0; k < jig::phys::kParts - kOwnParts; ++k) {
        g_you.ok[k] = false;
        if (g_you.ends[k][0] < 0 || g_you.ends[k][1] < 0) continue;
        Xf ma = modelOf(pose, g_you.ends[k][0]), mb = modelOf(pose, g_you.ends[k][1]);
        Vec3 a = add(wp, rotate(wq, {ma.p[0] * world.s[0], ma.p[1] * world.s[1], ma.p[2] * world.s[2]}));
        Vec3 b = add(wp, rotate(wq, {mb.p[0] * world.s[0], mb.p[1] * world.s[1], mb.p[2] * world.s[2]}));
        if (!finite(a) || !finite(b)) continue;
        std::memcpy(g_you.a[k], &a, sizeof(a));
        std::memcpy(g_you.b[k], &b, sizeof(b));
        g_you.ok[k] = true;
    }
    // Holding (user 2026-10-15 05:40Z): grip and trigger pressed together on that controller, read from the
    // Sims 4 VR mod (s4vr_hands: per hand valid, palm, orientation, grip, trigger).
    static HandsFn hands = nullptr;
    static ULONGLONG lookedAt = 0, readAt = 0;
    static bool pressed[2] = {};
    ULONGLONG nowMs = GetTickCount64();
    if (!hands && nowMs - lookedAt > 5000) {
        lookedAt = nowMs;
        if (HMODULE vr = GetModuleHandleW(L"sims4vr.dll")) hands = reinterpret_cast<HandsFn>(GetProcAddress(vr, "s4vr_hands"));
    }
    if (hands && nowMs - readAt >= 15) {
        readAt = nowMs;
        float out[29];
        unsigned long long held[2];
        bool ok = hands(out, held) != 0;
        for (int h = 0; h < 2; ++h) {
            const float* v = out + 9 + h * 10;
            bool now = ok && v[0] > 0.5f && v[8] > val(kGrabPress) && v[9] > val(kGrabPress);
            static std::atomic<int> logs{0};
            if (now != pressed[h] && logs.fetch_add(1) < 40)
                LOG("Your %s hand %s (grip %.2f, trigger %.2f)", h ? "right" : "left", now ? "holds" : "lets go",
                    ok ? v[8] : 0.0f, ok ? v[9] : 0.0f);
            pressed[h] = now;
        }
    }
    g_you.grip[0] = pressed[0];
    g_you.grip[1] = pressed[1];
    g_you.at = GetTickCount64();
    ReleaseSRWLockExclusive(&g_you.lock);
}

void yourHandsInto(jig::phys::Input& in, Vec3 her) {
    AcquireSRWLockShared(&g_you.lock);
    if (GetTickCount64() - g_you.at < 300) {
        for (int k = 0; k < jig::phys::kParts - kOwnParts; ++k) {
            if (!g_you.ok[k]) continue;
            Vec3 b{g_you.b[k][0], g_you.b[k][1], g_you.b[k][2]};
            Vec3 d = sub(b, her);
            if (dot(d, d) > kYourReach * kYourReach) continue;
            std::memcpy(in.a[kOwnParts + k], g_you.a[k], sizeof(g_you.a[k]));
            std::memcpy(in.b[kOwnParts + k], g_you.b[k], sizeof(g_you.b[k]));
            in.part[kOwnParts + k] = true;
        }
        in.grip[0] = g_you.grip[0];
        in.grip[1] = g_you.grip[1];
    }
    ReleaseSRWLockShared(&g_you.lock);
}

// After the game (and any other mod, like the Sims 4 VR arm IK) has posed the
// Sim: where its arms are, for the physics.
void publishArms(uint8_t* actor, uint8_t* pose, int slotIndex) {
    Slot& slot = g_slots[slotIndex];
    if (slot.actor.load(std::memory_order_relaxed) != actor || slot.busy.test_and_set(std::memory_order_acquire)) return;
    SimState& st = slot.st;
    Xf world{};
    std::memcpy(world.p, actor + kActorPosition, sizeof(world.p));
    std::memcpy(world.q, actor + kActorRotation, sizeof(world.q));
    std::memcpy(world.s, actor + kActorScale, sizeof(world.s));
    Quat wq{world.q[0], world.q[1], world.q[2], world.q[3]};
    Vec3 wp{world.p[0], world.p[1], world.p[2]};
    auto at = [&](int j) {
        Xf m = modelOf(pose, j);
        return add(wp, rotate(wq, {m.p[0] * world.s[0], m.p[1] * world.s[1], m.p[2] * world.s[2]}));
    };
    const bool arms = val(kArmPush) != 0;
    for (int k = kOwnParts; k < jig::phys::kParts; ++k) st.input.part[k] = false;
    st.input.grip[0] = st.input.grip[1] = false;
    if (arms && val(kYourHands) != 0 && actor != g_ownActor.load(std::memory_order_relaxed)) yourHandsInto(st.input, wp);
    for (int k = 0; k < kOwnParts; ++k) {
        st.input.part[k] = false;
        if (!arms || st.part[k][0] < 0 || st.part[k][1] < 0) continue;
        Vec3 a = at(st.part[k][0]), b = at(st.part[k][1]);
        if (!finite(a) || !finite(b)) continue;
        std::memcpy(st.input.a[k], &a, sizeof(float) * 3);
        std::memcpy(st.input.b[k], &b, sizeof(float) * 3);
        st.input.part[k] = true;
    }
    st.input.actor = actor;
    st.input.size = world.s[1] > 0.1f && world.s[1] < 5 ? world.s[1] : 1;
    jig::phys::publish(slotIndex, st.input);
    slot.busy.clear(std::memory_order_release);
}

// The size preset her CAS breast bone's scale picks.
int sizeFrom(float measured) {
    if (!(measured > 0)) return 1;
    if (measured >= val(kSizeHugeAt)) return 3;
    if (measured >= val(kSizeBiggerAt)) return 2;
    if (measured >= val(kSizeMediumAt)) return 1;
    return 0;
}
std::atomic<int> g_sizeLogs{0};

// Returns the slot index used (-1: none), for publishArms.
int jiggle(uint8_t* actor, uint8_t* pose, SimLook look) {
    uint8_t* rig = *reinterpret_cast<uint8_t**>(pose + kPoseRig);
    Xf* locals = *reinterpret_cast<Xf**>(pose + kPoseLocals);
    uint32_t poseCount = *reinterpret_cast<uint16_t*>(pose + kPoseJointCount);
    if (!rig || !locals) return -1;
    uint32_t count = g_jointCount(rig + kRigSkeleton);
    if (count > poseCount) count = poseCount;
    if (!count || count > kMaxJoints) return -1;

    Xf world{};
    std::memcpy(world.p, actor + kActorPosition, sizeof(world.p));
    std::memcpy(world.q, actor + kActorRotation, sizeof(world.q));
    std::memcpy(world.s, actor + kActorScale, sizeof(world.s));
    if (!(world.s[0] > 1e-3f && world.s[1] > 1e-3f && world.s[2] > 1e-3f) || !finite({world.p[0], world.p[1], world.p[2]}))
        return -1;
    Quat wq{world.q[0], world.q[1], world.q[2], world.q[3]};
    Vec3 wp{world.p[0], world.p[1], world.p[2]};
    Vec3 ws{world.s[0], world.s[1], world.s[2]};
    float simBreasts = look.breasts, simButt = look.butt;

    ULONGLONG now = GetTickCount64();
    // Far from your Sim: that part doesn't jiggle; neither does: nothing at all (no physics either).
    float distanceSq = 0;
    if (now - g_ownSeen.load(std::memory_order_relaxed) < 3000) {
        Vec3 own{g_ownPos[0].load(std::memory_order_relaxed), g_ownPos[1].load(std::memory_order_relaxed),
                 g_ownPos[2].load(std::memory_order_relaxed)};
        Vec3 d = sub(wp, own);
        distanceSq = dot(d, d);
    }
    if (tooFar(distanceSq, kBreastDistance)) simBreasts = 0;
    if (tooFar(distanceSq, kButtDistance)) simButt = 0;
    if (!(simBreasts > 0) && !(simButt > 0)) return -1;

    Slot* slot = slotFor(actor, now);
    if (!slot || slot->busy.test_and_set(std::memory_order_acquire)) return -1;
    SimState& st = slot->st;
    if (st.rig != rig || st.count != count) resolve(st, rig, count);
    const int slotIndex = static_cast<int>(slot - g_slots);
    float offsets[jig::phys::kBones][3], press[jig::phys::kBones][3];
    const bool haveOffsets = g_physics && jig::phys::offsets(slotIndex, actor, offsets, press);
    for (bool& u : st.input.use) u = false;

    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    float dt = st.last.QuadPart ? float(t.QuadPart - st.last.QuadPart) / float(g_freq.QuadPart) : 0;
    bool step = dt > 0.002f;
    if (step || !st.last.QuadPart) st.last = t;
    bool restart = !(dt < 0.25f);
    if (dt > 0.05f) dt = 0.05f;

    // Every bone starts from the game's own transform: what we wrote last time is
    // still there when no animation posed it this frame.
    for (int b = 0; b < kBones; ++b) {
        int j = st.bone[b];
        if (j < 0) continue;
        Spring& s = st.spring[b];
        if (s.written && std::memcmp(&locals[j], &s.wrote, sizeof(Xf)) == 0)
            std::memcpy(&locals[j], &s.base, sizeof(Xf));
        else
            std::memcpy(&s.base, &locals[j], sizeof(Xf));
        s.written = false;
    }
    // Her size preset: the pie's Cup size, else from her CAS breast bone's scale.
    if (st.bone[kLeftBreast] >= 0) {
        const float* sc = st.spring[kLeftBreast].base.s;
        float m = (sc[0] + sc[1] + sc[2]) / 3;
        if (std::isfinite(m) && std::fabs(m - st.measured) > 0.01f) {
            st.measured = m;
            slot->measured.store(m, std::memory_order_relaxed);
            slot->autoSize.store(sizeFrom(m), std::memory_order_relaxed);
            if (g_sizeLogs.fetch_add(1) < 60) {
                const float* lp = st.spring[kLeftBreast].base.p;
                LOG("Size: Sim %p breast bone scale %.3f %.3f %.3f (mean %.3f), position %.3f %.3f %.3f: %s", actor,
                    sc[0], sc[1], sc[2], m, lp[0], lp[1], lp[2], kSizeNames[sizeFrom(m)]);
            }
        }
    }
    const int z = look.cup >= 0 && look.cup < kSizes ? look.cup : sizeFrom(st.measured);

    const bool topOn = (look.clothed & 1) != 0, bottomOn = (look.clothed & 2) != 0;
    // Breasts: her size preset, clothed or nude.
    float breastHz = pv(z, fHz) * (topOn ? pv(z, fClothedHz) : 1);
    float breastDamping = pv(z, fDamping) * (topOn ? pv(z, fClothedDamping) : 1);
    // Gravity: her own nude one (pie Gravity preset) or the preset's; clothed adds the clothed offset, which
    // can take it below 0: the top pushes them up (user 2026-10-15 03:10Z, Bolt on + push-up).
    float breastGravity = (std::isfinite(look.gravity) ? look.gravity : pv(z, fGravity)) + (topOn ? pv(z, fClothedGravity) : 0);
    // Each breast moves half the change in the gap, sideways in the Sim's frame (away from her middle).
    const float apart = std::clamp((std::isfinite(look.apart) ? look.apart : pv(z, fApart)) +
                                       (topOn ? pv(z, fClothedApart) : 0), -0.1f, 0.1f);
    const float breasts = val(kBreastsOn) != 0 ? pv(z, fStrength) * simBreasts * (topOn ? pv(z, fClothedStrength) : 1) : 0;
    const Vec3 breastShare{pv(z, fSide), pv(z, fUp), pv(z, fFront)};
    // Butt (parked): one value for every Sim.
    float buttMax = val(kButtMax);
    const Vec3 buttShare{val(kButtSide), val(kButtUp), val(kButtFront)};
    for (int b = 0; b < kBones; ++b) {
        bool breast = b < kLeftHip;
        st.input.hz[b] = breast ? breastHz : val(kButtHz) * (bottomOn ? val(kButtClothedHz) : 1);
        st.input.damping[b] = breast ? breastDamping : val(kButtDamping) * (bottomOn ? val(kButtClothedDamping) : 1);
        st.input.gravity[b] = breast ? breastGravity : val(kButtGravity) + (bottomOn ? val(kButtClothedGravity) : 0);
        st.input.radius[b] = breast ? pv(z, fSize) : val(kButtSize);
        st.input.yourRadius[b] = breast ? pv(z, fSizeYou) : val(kButtSize);
        st.input.mass[b] = breast && val(kWeightOn) != 0 ? pv(z, fWeight) : 0;  // 0: the 0.10 ball, no force cap
        if (breast) {  // bigger-looking breasts are bigger to push too
            st.input.radius[b] *= std::clamp(pv(z, fScale), 0.25f, 3.0f);
            st.input.yourRadius[b] *= std::clamp(pv(z, fScale), 0.25f, 3.0f);
        }
        // Limits in the Sim's frame: x side, y up (hi) / down (lo), z front and back.
        float side = breast ? pv(z, fMaxSide) : buttMax, up = breast ? pv(z, fMaxUp) : buttMax;
        float down = breast ? pv(z, fMaxDown) : buttMax, front = breast ? pv(z, fMaxFront) : buttMax;
        st.input.lo[b][0] = -side;
        st.input.hi[b][0] = side;
        st.input.lo[b][1] = -down;
        st.input.hi[b][1] = up;
        st.input.lo[b][2] = -front;
        st.input.hi[b][2] = front;
    }
    // Bodies weight only about a quarter of each cheek to butt bones: move those further so 1 shows.
    constexpr float kButtBoost = 3.5f;
    const float butt = val(kButtOn) != 0 ? val(kButt) * simButt * (bottomOn ? val(kButtClothedStrength) : 1) *
                                               (st.buttBones ? kButtBoost : 1)
                                         : 0;
    const float swing = val(kSwingOn) != 0 ? pv(z, fSwing) : 0, squish = val(kSquishOn) != 0 ? pv(z, fSquish) : 0;
    // Size: how big they look, times her own (the bone's scale), whether they jiggle this frame or not.
    const float look3 = std::clamp(pv(z, fScale), 0.25f, 3.0f);
    if (std::fabs(look3 - 1) > 1e-3f && val(kBreastsOn) != 0)
        for (int b = 0; b < kLeftHip; ++b) {
            int j = st.bone[b];
            if (j < 0) continue;
            for (int k = 0; k < 3; ++k) locals[j].s[k] *= look3;
            std::memcpy(&st.spring[b].wrote, &locals[j], sizeof(Xf));
            st.spring[b].written = true;
        }
    // Her body frame, from her own breast bones (0.11.4: 0.4.0-0.11.3 took the Sim's object axes, whose x is
    // NOT her side, so "side" moved them forward: Apart made them longer, the wall stood across her front).
    // x = her side (from her right breast to her left), y = up (world), z = her front (out of her chest).
    Quat bq = wq;
    Vec3 mid{0, 0, 0};
    bool haveMid = false, haveBody = false;
    if (st.bone[0] >= 0 && st.bone[1] >= 0 && st.parent[0] >= 0 && st.parent[1] >= 0) {
        Vec3 at[2], chest[2];
        for (int b = 0; b < 2; ++b) {
            Xf m = modelOf(pose, st.parent[b]);
            Quat q{m.q[0], m.q[1], m.q[2], m.q[3]};
            const float* l = locals[st.bone[b]].p;
            Vec3 mp = add({m.p[0], m.p[1], m.p[2]}, rotate(q, {l[0] * m.s[0], l[1] * m.s[1], l[2] * m.s[2]}));
            at[b] = add(wp, rotate(wq, {mp.x * ws.x, mp.y * ws.y, mp.z * ws.z}));
            chest[b] = add(wp, rotate(wq, {m.p[0] * ws.x, m.p[1] * ws.y, m.p[2] * ws.z}));
        }
        mid = mul(add(at[0], at[1]), 0.5f);
        Vec3 x = sub(at[kLeftBreast], at[kRightBreast]);
        x.y = 0;
        float lx = length(x);
        if (finite(mid) && lx > 0.02f) {
            x = mul(x, 1 / lx);
            Vec3 y{0, 1, 0};
            Vec3 zf = cross(x, y);  // right-handed: z = x × y
            Vec3 out = sub(mid, mul(add(chest[0], chest[1]), 0.5f));
            out.y = 0;
            if (dot(zf, out) < 0) {  // z must point out of her chest: turn the frame half round (x flips too)
                x = mul(x, -1);
                zf = mul(zf, -1);
            }
            bq = fromAxes(x, y, zf);
            haveBody = true;
            haveMid = val(kBreastWall) != 0;
        }
        static std::atomic<int> frameLogs{0};
        if (frameLogs.fetch_add(1) < 3) {
            Vec3 ox = rotate(wq, {1, 0, 0}), bx = rotate(bq, {1, 0, 0}), bz = rotate(bq, {0, 0, 1});
            LOG("Body frame: her side %.2f %.2f %.2f, front %.2f %.2f %.2f; the Sim's own x %.2f %.2f %.2f", bx.x, bx.y,
                bx.z, bz.x, bz.y, bz.z, ox.x, ox.y, ox.z);
        }
    }
    const Vec3 side = rotate(bq, {1, 0, 0});

    for (int b = 0; b < kBones; ++b) {
        int j = st.bone[b], pj = st.parent[b];
        if (j < 0 || pj < 0) continue;
        Spring& s = st.spring[b];
        float* local = locals[j].p;

        bool breast = b < kLeftHip;
        float gain = breast ? breasts : butt;
        if (!(gain > 0.001f)) {
            s.started = false;
            continue;
        }
        const Vec3 share = breast ? breastShare : buttShare;
        float hz = st.input.hz[b], zeta = st.input.damping[b];
        float maxMove = std::max(std::max(st.input.hi[b][0], st.input.hi[b][1]),
                                 std::max(-st.input.lo[b][1], st.input.hi[b][2])) * ws.y;

        // Where the pose puts the bone, in the world.
        Xf parent = modelOf(pose, pj);
        Quat pq{parent.q[0], parent.q[1], parent.q[2], parent.q[3]};
        Vec3 inParent{local[0] * parent.s[0], local[1] * parent.s[1], local[2] * parent.s[2]};
        Vec3 model = add({parent.p[0], parent.p[1], parent.p[2]}, rotate(pq, inParent));
        Vec3 lever = sub(model, {parent.p[0], parent.p[1], parent.p[2]});
        Vec3 rest = add(wp, rotate(wq, {model.x * ws.x, model.y * ws.y, model.z * ws.z}));
        if (!finite(rest)) continue;
        Vec3 spread{0, 0, 0};
        Vec3 wallPress{0, 0, 0};
        float room = 1e9f;  // how far it may move toward the wall (metres, world)
        float outward = dot(sub(rest, haveBody ? mid : wp), side) >= 0 ? 1.0f : -1.0f;
        if (breast && haveMid)  // half its way to the middle (the other breast takes the other half)
            room = std::max(0.005f * ws.x, 0.5f * std::fabs(dot(sub(rest, mid), side)));
        if (breast && apart != 0) {
            float shift = 0.5f * apart * ws.x;  // below 0: toward the middle
            if (shift < -room) {  // into the wall: stops there and squishes
                float r = st.input.radius[b] * ws.x;
                wallPress = {outward * (-shift - room) / (2 * r), 0, 0};
                shift = -room;
            }
            spread = mul(side, outward * shift);
            rest = add(rest, spread);
            room = std::max(0.0005f, room + shift);
        }
        if (breast && haveMid) {  // its side limit toward the middle ends at the wall
            float limit = room / ws.y;
            if (outward > 0)
                st.input.lo[b][0] = -std::min(-st.input.lo[b][0], limit);
            else
                st.input.hi[b][0] = std::min(st.input.hi[b][0], limit);
        }

        Vec3 jig{0, 0, 0}, pressed{0, 0, 0};
        if (g_physics) {
            st.input.use[b] = true;
            std::memcpy(st.input.rest[b], &rest, sizeof(float) * 3);
            st.input.frame[b][0] = bq.x;
            st.input.frame[b][1] = bq.y;
            st.input.frame[b][2] = bq.z;
            st.input.frame[b][3] = bq.w;
            if (!haveOffsets) continue;
            jig = shaped(mul(rotate(bq, {offsets[b][0], offsets[b][1], offsets[b][2]}), gain), bq, share);
            if (!finite(jig)) continue;
            // Never more than 30 cm shown (high Strength): held there, not dropped to the game's pose.
            if (length(jig) > 0.3f) jig = mul(jig, 0.3f / length(jig));
            // Pressed: body frame -> world -> the Sim's model space (what shapeBone works in).
            if (breast) pressed = rotate(conj(wq), rotate(bq, add({press[b][0], press[b][1], press[b][2]}, wallPress)));
            s.mass = add(rest, jig);
            s.rest = rest;
            s.started = false;  // the springs start afresh if the physics stops
        } else {
            if (!s.started || restart || length(sub(rest, s.rest)) > 0.5f * ws.y) {  // first frame, or the Sim jumped
                s.mass = rest;
                s.speed = {0, 0, 0};
                s.rest = rest;
                s.started = true;
                continue;
            }
            if (step) {
                Vec3 restSpeed = mul(sub(rest, s.rest), 1 / dt);
                float w = 6.2831853f * hz, k = w * w, c = 2 * zeta * w;
                int n = static_cast<int>(dt * 240) + 1;
                if (n > 12) n = 12;
                float h = dt / n;
                for (int i = 1; i <= n; ++i) {
                    Vec3 target = add(s.rest, mul(sub(rest, s.rest), float(i) / n));
                    Vec3 a = sub(mul(sub(target, s.mass), k), mul(sub(s.speed, restSpeed), c));
                    s.speed = add(s.speed, mul(a, h));
                    s.mass = add(s.mass, mul(s.speed, h));
                }
                s.rest = rest;
                // Held near the bone: beyond maxMove the mass is pulled back and loses its outward speed.
                Vec3 off = sub(s.mass, rest);
                float len = length(off);
                if (len > maxMove && len > 0) {
                    Vec3 dir = mul(off, 1 / len);
                    s.mass = add(rest, mul(dir, maxMove));
                    float out = dot(sub(s.speed, restSpeed), dir);
                    if (out > 0) s.speed = sub(s.speed, mul(dir, out));
                }
                if (!finite(s.mass) || !finite(s.speed)) {
                    s.started = false;
                    continue;
                }
            }
            jig = shaped(mul(sub(s.mass, s.rest), gain), bq, share);
        }
        if (breast && haveMid) {  // Strength can't carry it through the wall either
            float inward = -outward * dot(jig, side);
            if (inward > room) jig = add(jig, mul(side, outward * (inward - room)));
        }
        Vec3 move = add(jig, spread);
        writeMove(locals, j, s, move, wq, ws, pq, parent);
        if (breast) shapeBone(locals[j], jig, pressed, swing, squish, wq, ws, pq, lever);
        std::memcpy(&s.wrote, &locals[j], sizeof(Xf));
        s.written = true;

        // First few: does the game put the bone where we meant (the parent maths checks out)?
        // The butt has its own 6 (user 2026-10-17 01:55Z: butt jiggle doesn't show; prove the moves happen).
        std::atomic<int>& checks = breast ? g_checkLogs : g_buttCheckLogs;
        int checkMax = breast ? 8 : 6;
        if (length(move) > 0.003f && checks.load(std::memory_order_relaxed) < checkMax) {
            Xf got = modelOf(pose, j);
            Vec3 gw = add(wp, rotate(wq, {got.p[0] * ws.x, got.p[1] * ws.y, got.p[2] * ws.z}));
            Vec3 want = add(rest, move);
            if (checks.fetch_add(1) < checkMax)
                LOG("Check %s: moved %.1f mm, the game has it %.2f mm from where we meant", kBoneNames[b],
                    length(move) * 1000, length(sub(gw, want)) * 1000);
        }
    }
    // Shader probe mode 2: this Sim's world and all joints' model transforms, matched offline to the bone palettes.
    if (gpuprobe::wantSim(actor)) {
        static Xf frame[kMaxJoints + 2];
        frame[0] = world;
        std::memset(&frame[1], 0, sizeof(Xf));
        int32_t* info = reinterpret_cast<int32_t*>(&frame[1]);
        info[0] = static_cast<int32_t>(count);
        for (int b = 0; b < kBones && b < 8; ++b) info[4 + b] = st.bone[b];
        for (uint32_t j = 0; j < count; ++j) frame[2 + j] = modelOf(pose, static_cast<int>(j));
        gpuprobe::simFrame(frame, static_cast<unsigned>((count + 2) * sizeof(Xf)));
    }
    // Shader probe mode 6 (breast wave test): where she is and her breast bones' scale products (their skin
    // matrices' determinants), so the patched shaders' bone blocks can be matched to her.
    if (gpuprobe::waveOn() && st.bone[0] >= 0 && st.bone[1] >= 0) {
        Xf l = modelOf(pose, st.bone[0]), r = modelOf(pose, st.bone[1]);
        float dx = world.p[0] - g_ownPos[0].load(std::memory_order_relaxed),
              dz = world.p[2] - g_ownPos[2].load(std::memory_order_relaxed);
        float toYou = actor == g_ownActor.load(std::memory_order_relaxed) ? 1e9f : std::sqrt(dx * dx + dz * dz);
        gpuprobe::waveSim(actor, world.p, l.s[0] * l.s[1] * l.s[2], r.s[0] * r.s[1] * r.s[2], toYou);
    }
    slot->busy.clear(std::memory_order_release);
    return g_physics ? slotIndex : -1;
}

void hooked(void* actor, void* pose) {
    int slot = -1;
    if (actor && actor == g_ownActor.load(std::memory_order_relaxed)) {
        auto* p = reinterpret_cast<const float*>(static_cast<uint8_t*>(actor) + kActorPosition);
        if (std::isfinite(p[0]) && std::isfinite(p[1]) && std::isfinite(p[2])) {
            for (int i = 0; i < 3; ++i) g_ownPos[i].store(p[i], std::memory_order_relaxed);
            g_ownSeen.store(GetTickCount64(), std::memory_order_relaxed);
        }
    }
    if (actor && pose && val(kOn) != 0) {
        SimLook look;
        if (strengthOf(actor, look)) {
            LARGE_INTEGER a, b;
            QueryPerformanceCounter(&a);
            slot = jiggle(static_cast<uint8_t*>(actor), static_cast<uint8_t*>(pose), look);
            QueryPerformanceCounter(&b);
            g_ticks.fetch_add(b.QuadPart - a.QuadPart, std::memory_order_relaxed);
            g_calls.fetch_add(1, std::memory_order_relaxed);
            ULONGLONG now = GetTickCount64(), at = g_statsAt.load(std::memory_order_relaxed);
            if (now - at > 60000 && g_statsAt.compare_exchange_strong(at, now) && at) {
                long long calls = g_calls.exchange(0), ticks = g_ticks.exchange(0);
                LOG("Last minute: %lld Sim poses moved, %.2f microseconds each on average, %d Sims listed", calls,
                    calls ? ticks * 1e6 / double(g_freq.QuadPart) / calls : 0.0, g_simCount.load());
            }
        }
    }
    g_next(actor, pose);
    if (actor && pose && actor == g_ownActor.load(std::memory_order_relaxed) && val(kYourHands) != 0)
        captureYourHands(static_cast<uint8_t*>(actor), static_cast<uint8_t*>(pose));
    if (slot >= 0) publishArms(static_cast<uint8_t*>(actor), static_cast<uint8_t*>(pose), slot);
}

// ---------------------------------------------------------------------------
// The patch: the caller's call goes to a jump near the game (call rel32 must
// reach it) that goes on to `hooked`. All other threads are held while the
// four bytes change, and none is left inside the instruction.

uint8_t* nearAlloc(uint8_t* close_to, size_t size) {
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    uintptr_t gran = si.dwAllocationGranularity;
    uintptr_t start = reinterpret_cast<uintptr_t>(close_to) & ~(gran - 1);
    for (uintptr_t d = gran; d < 0x70000000; d += gran) {
        for (uintptr_t a : {start - d, start + d}) {
            MEMORY_BASIC_INFORMATION mbi;
            if (!VirtualQuery(reinterpret_cast<void*>(a), &mbi, sizeof(mbi)) || mbi.State != MEM_FREE) continue;
            void* p = VirtualAlloc(reinterpret_cast<void*>(a), size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
            if (p) return static_cast<uint8_t*>(p);
        }
    }
    return nullptr;
}

bool patchCall(uint8_t* call, void* to, void** previous) {
    uint8_t* stub = nearAlloc(call, 64);
    if (!stub) return false;
    stub[0] = 0xff;  // jmp [rip+0]
    stub[1] = 0x25;
    std::memset(stub + 2, 0, 4);
    std::memcpy(stub + 6, &to, 8);
    FlushInstructionCache(GetCurrentProcess(), stub, 14);

    int32_t oldRel;
    std::memcpy(&oldRel, call + 1, 4);
    *previous = call + 5 + oldRel;
    long long rel = reinterpret_cast<long long>(stub) - reinterpret_cast<long long>(call + 5);
    if (rel != static_cast<int32_t>(rel)) return false;
    int32_t newRel = static_cast<int32_t>(rel);

    // Hold every other thread outside the instruction.
    DWORD self = GetCurrentThreadId(), pid = GetCurrentProcessId();
    HANDLE threads[1024];
    int held = 0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap != INVALID_HANDLE_VALUE) {
        THREADENTRY32 te{};
        te.dwSize = sizeof(te);
        for (BOOL ok = Thread32First(snap, &te); ok && held < 1024; ok = Thread32Next(snap, &te)) {
            if (te.th32OwnerProcessID != pid || te.th32ThreadID == self) continue;
            HANDLE h = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_SET_CONTEXT, FALSE, te.th32ThreadID);
            if (!h) continue;
            if (SuspendThread(h) == static_cast<DWORD>(-1)) {
                CloseHandle(h);
                continue;
            }
            CONTEXT ctx{};
            ctx.ContextFlags = CONTEXT_CONTROL;
            if (GetThreadContext(h, &ctx) && ctx.Rip > reinterpret_cast<DWORD64>(call) &&
                ctx.Rip < reinterpret_cast<DWORD64>(call + 5)) {
                ctx.Rip = reinterpret_cast<DWORD64>(call);  // cannot happen at an instruction boundary; be safe
                SetThreadContext(h, &ctx);
            }
            threads[held++] = h;
        }
        CloseHandle(snap);
    }
    DWORD old;
    bool ok = VirtualProtect(call + 1, 4, PAGE_EXECUTE_READWRITE, &old) != 0;
    if (ok) {
        std::memcpy(call + 1, &newRel, 4);
        VirtualProtect(call + 1, 4, old, &old);
        FlushInstructionCache(GetCurrentProcess(), call, 5);
    }
    for (int i = 0; i < held; ++i) {
        ResumeThread(threads[i]);
        CloseHandle(threads[i]);
    }
    return ok;
}

// RTBP ships a d3d11.dll proxy in the game's Bin folder.
bool rtbpLoaded() {
    HMODULE d3d = GetModuleHandleW(L"d3d11.dll");
    if (!d3d) return false;
    wchar_t mine[MAX_PATH], game[MAX_PATH];
    if (!GetModuleFileNameW(d3d, mine, MAX_PATH) || !GetModuleFileNameW(nullptr, game, MAX_PATH)) return false;
    std::wstring a(mine), b(game);
    a = a.substr(0, a.find_last_of(L"\\/") + 1);
    b = b.substr(0, b.find_last_of(L"\\/") + 1);
    return _wcsicmp(a.c_str(), b.c_str()) == 0;
}

bool g_installed = false;

void install() {
    if (g_installed) return;
    g_installed = true;
    QueryPerformanceFrequency(&g_freq);
    readSettings();
    wchar_t exe[MAX_PATH] = L"";
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    LOG("Integrated Body Physics 0.12.1 in %ls", exe);
    if (rtbpLoaded()) {
        LOG("RTBP's d3d11.dll is loaded%s", val(kWithRtbp) != 0 ? " (with_rtbp = 1: running anyway)" : ": staying off (with_rtbp = 1 runs both)");
        if (val(kWithRtbp) == 0) return;
    }
    if (val(kShaderProbe) != 0) gpuprobe::start(g_folder, &logLine, static_cast<int>(val(kShaderProbe)));
    uint8_t* caller = find(kCaller);
    g_model = reinterpret_cast<ModelTransformFn>(find(kModel));
    g_jointIndex = reinterpret_cast<JointIndexFn>(find(kJointIndex));
    g_jointCount = reinterpret_cast<JointCountFn>(find(kJointCount));
    g_parent = reinterpret_cast<ParentFn>(find(kParent));
    g_lookup = reinterpret_cast<LookupFn>(find(kLookup));
    auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    auto rva = [&](auto p) { return p ? static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(p) - base) : 0ull; };
    LOG("Game functions: caller 0x%llx, model 0x%llx, joint index 0x%llx, joint count 0x%llx, parent 0x%llx, lookup 0x%llx",
        rva(caller), rva(g_model), rva(g_jointIndex), rva(g_jointCount), rva(g_parent), rva(g_lookup));
    if (!caller || !g_model || !g_jointIndex || !g_jointCount || !g_parent || !g_lookup) {
        LOG("Game build not recognised: off");
        return;
    }
    void* previous = nullptr;
    if (!patchCall(caller + kCallerCall, reinterpret_cast<void*>(&hooked), &previous)) {
        LOG("Patch failed: off");
        return;
    }
    g_physics = jig::phys::start(physicsParams());
    LOG("Physics: %s", g_physics ? "Jolt world running (120 steps a second)" : "Jolt didn't start: built-in springs");
    g_next = reinterpret_cast<PoseToJointsFn>(previous);
    LOG("On: the pose call now comes here first, then goes on to 0x%llx%s", rva(previous),
        *static_cast<uint8_t*>(previous) == 0xe9 ? " (another mod hooks that function: fine)" : "");
}

// ---------------------------------------------------------------------------
// The mod's own menu, for players without the Sims 4 VR mod (user 2026-10-17 16:09Z): the script opens it
// with game dialogs when menu_key goes down in the game's window.

const wchar_t kMenu[] =
#include "menu.inc"
    ;

bool vrLoaded() { return GetModuleHandleW(L"sims4vr.dll") != nullptr; }

std::atomic<bool> g_keyHit{false};
std::atomic<int> g_capture{-1};  // -1 = not capturing; 0 = waiting for a key; else the key pressed
std::atomic<bool> g_keyThread{false};

bool gameInFront() {
    DWORD pid = 0;
    HWND w = GetForegroundWindow();
    return w && GetWindowThreadProcessId(w, &pid) && pid == GetCurrentProcessId();
}
bool down(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }

// Watches the keyboard every 10 ms (a tap is shorter than the script's 0.1 s alarm).
void startKeyThread() {
    if (g_keyThread.exchange(true)) return;
    CreateThread(nullptr, 0, [](LPVOID) -> DWORD {
        bool was = true;  // a key held when the thread starts doesn't count
        bool held[256] = {};
        for (;;) {
            Sleep(10);
            bool front = gameInFront();
            if (g_capture.load() == 0) {
                for (int vk = 8; vk < 255; ++vk) {
                    bool d = front && down(vk);
                    bool skip = vk <= 6 || vk == VK_RETURN || vk == VK_ESCAPE || (vk >= VK_SHIFT && vk <= VK_MENU) ||
                                (vk >= VK_LSHIFT && vk <= VK_RMENU) || vk == VK_LWIN || vk == VK_RWIN;
                    if (d && !held[vk] && !skip) {
                        int none = 0;
                        g_capture.compare_exchange_strong(none, vk);
                    }
                    held[vk] = d;
                }
                was = true;
                continue;
            }
            int vk = static_cast<int>(val(kMenuKey));
            bool d = front && vk > 0 && vk < 255 && down(vk) && !down(VK_CONTROL) && !down(VK_MENU);
            if (d && !was) g_keyHit = true;
            was = d;
        }
    }, nullptr, 0, nullptr);
}

std::string utf8(const wchar_t* w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    std::string out(n > 0 ? n - 1 : 0, '\0');
    if (n > 1) WideCharToMultiByte(CP_UTF8, 0, w, -1, &out[0], n, nullptr, nullptr);
    return out;
}

// ---------------------------------------------------------------------------
// Python (3.7, the game's), minimal ABI subset.

struct PyObject {
    ptrdiff_t ob_refcnt;
    void* ob_type;
};
using PyCFunction = PyObject* (*)(PyObject*, PyObject*);
struct PyMethodDef {
    const char* ml_name;
    PyCFunction ml_meth;
    int ml_flags;
    const char* ml_doc;
};
struct PyModuleDef_Base {
    PyObject ob_base;
    PyObject* (*m_init)();
    ptrdiff_t m_index;
    PyObject* m_copy;
};
struct PyModuleDef {
    PyModuleDef_Base m_base;
    const char* m_name;
    const char* m_doc;
    ptrdiff_t m_size;
    PyMethodDef* m_methods;
    void* m_slots;
    void* m_traverse;
    void* m_clear;
    void* m_free;
};
constexpr int METH_VARARGS = 0x0001;
constexpr int METH_NOARGS = 0x0004;

struct PythonApi {
    PyObject* (*PyModule_Create2)(PyModuleDef*, int);
    int (*PyArg_ParseTuple)(PyObject*, const char*, ...);
    PyObject* (*PyLong_FromLong)(long);
    PyObject* (*Py_BuildValue)(const char*, ...);
    void (*Py_IncRef)(PyObject*);
    PyObject* None;
} py;

PyObject* none() {
    py.Py_IncRef(py.None);
    return py.None;
}

// set_sims(bytes): records of (object id u64, breasts f32, butt f32, flags u32, nude breast gravity f32
// (-1..1; NaN = the setting), nude breasts apart f32 (metres; NaN = the setting), cup size i32 (0 small ..
// 3 huge; -1 = from her CAS size)), little-endian.
// Flags: 1 = the active Sim, 2 = top clothed, 4 = bottom clothed.
PyObject* setSims(PyObject*, PyObject* args) {
    const char* data = nullptr;
    int length = 0;  // y# without PY_SSIZE_T_CLEAN: int
    if (!py.PyArg_ParseTuple(args, "y#", &data, &length)) return nullptr;
    constexpr int kRecord = 32;
    int n = length / kRecord, found = 0;
    if (n > kMaxSims) n = kMaxSims;
    void* actors[kMaxSims];
    float strength[kMaxSims], butt[kMaxSims], gravityOf[kMaxSims], apartOf[kMaxSims];
    int cupOf[kMaxSims];
    bool own[kMaxSims];
    uint32_t clothedOf[kMaxSims];
    for (int i = 0; i < n; ++i) {
        uint64_t id;
        float s, b, g;
        uint32_t flags;
        const char* r = data + i * kRecord;
        std::memcpy(&id, r, 8);
        std::memcpy(&s, r + 8, 4);
        std::memcpy(&b, r + 12, 4);
        std::memcpy(&flags, r + 16, 4);
        std::memcpy(&g, r + 20, 4);
        float ap;
        std::memcpy(&ap, r + 24, 4);
        int32_t cup;
        std::memcpy(&cup, r + 28, 4);
        void* a = g_lookup && id ? g_lookup(&id) : nullptr;
        if (!a) continue;
        actors[found] = a;
        strength[found] = s;
        own[found] = (flags & 1) != 0;
        clothedOf[found] = (flags >> 1) & 3;
        gravityOf[found] = g;  // NaN: the setting (0.11.14; was -1, and the pie used 0..1)
        apartOf[found] = ap;  // NaN: the setting
        cupOf[found] = cup >= 0 && cup < kSizes ? cup : -1;
        butt[found++] = b;
    }
    // Shrink first, then fill, then grow: a reader never sees an unset entry.
    int old = g_simCount.load();
    if (found < old) g_simCount.store(found, std::memory_order_release);
    for (int i = 0; i < found; ++i) {
        g_simStrength[i].store(strength[i], std::memory_order_relaxed);
        g_simButt[i].store(butt[i], std::memory_order_relaxed);
        g_simOwn[i].store(own[i], std::memory_order_relaxed);
        g_simClothed[i].store(clothedOf[i], std::memory_order_relaxed);
        g_simGravity[i].store(gravityOf[i], std::memory_order_relaxed);
        g_simApart[i].store(apartOf[i], std::memory_order_relaxed);
        g_simCup[i].store(cupOf[i], std::memory_order_relaxed);
        g_simActor[i].store(actors[i], std::memory_order_relaxed);
    }
    g_simCount.store(found, std::memory_order_release);
    void* ownActor = nullptr;
    for (int i = 0; i < found; ++i)
        if (own[i]) ownActor = actors[i];
    g_ownActor.store(ownActor, std::memory_order_relaxed);
    static int logs = 0;
    if ((found != old || logs < 5) && logs++ < 60) LOG("Sims listed: %d (of %d sent)", found, n);
    return py.PyLong_FromLong(found);
}

// set(name, value): on, breasts, butt (the console command).
PyObject* setValue(PyObject*, PyObject* args) {
    const char* name = nullptr;
    float v = 0;
    if (!py.PyArg_ParseTuple(args, "sf", &name, &v)) return nullptr;
    setValue(name, v);
    return none();
}

PyObject* status(PyObject*, PyObject*) {
    return py.Py_BuildValue("(iiffi)", g_next ? 1 : 0, val(kOn) != 0 ? 1 : 0, pv(1, fStrength), val(kButt), g_simCount.load());
}

// get(name) -> the setting's value, or None for an unknown key.
PyObject* getValue(PyObject*, PyObject* args) {
    const char* name = nullptr;
    if (!py.PyArg_ParseTuple(args, "s", &name)) return nullptr;
    Value* v = findValue(name);
    if (!v) return none();
    return py.Py_BuildValue("d", static_cast<double>(v->v.load()));
}

// size_of(object id) -> (size preset her CAS size picks 0..3, measured bone scale), or None when not seen.
PyObject* sizeOf(PyObject*, PyObject* args) {
    unsigned long long id = 0;
    if (!py.PyArg_ParseTuple(args, "K", &id)) return nullptr;
    uint64_t key = id;
    void* actor = g_lookup && key ? g_lookup(&key) : nullptr;
    if (!actor) return none();
    for (Slot& s : g_slots)
        if (s.actor.load(std::memory_order_relaxed) == actor && s.measured.load() > 0)
            return py.Py_BuildValue("(id)", s.autoSize.load(), static_cast<double>(s.measured.load()));
    return none();
}

PyObject* logText(PyObject*, PyObject* args) {
    const char* text = nullptr;
    if (!py.PyArg_ParseTuple(args, "s", &text)) return nullptr;
    LOG("script: %s", text);
    return none();
}

// menu() -> the menu rows (docs/MENU-EXPORT.md) as text.
PyObject* menuText(PyObject*, PyObject*) {
    static const std::string text = utf8(kMenu);
    return py.Py_BuildValue("s", text.c_str());
}

// menu_key() -> 1 once each time the menu key went down in the game's window (never with the VR mod, whose
// debug menu has these pages), else 0.
PyObject* menuKey(PyObject*, PyObject*) {
    startKeyThread();
    bool hit = g_keyHit.exchange(false);
    return py.PyLong_FromLong(hit && !vrLoaded() ? 1 : 0);
}

// vr() -> 1 when the Sims 4 VR mod is loaded.
PyObject* vr(PyObject*, PyObject*) { return py.PyLong_FromLong(vrLoaded() ? 1 : 0); }

// capture(start) -> start 1: wait for the next key; 0: the key pressed since (0 = none yet), and stop.
PyObject* capture(PyObject*, PyObject* args) {
    int start = 0;
    if (!py.PyArg_ParseTuple(args, "i", &start)) return nullptr;
    startKeyThread();
    if (start) {
        g_capture = 0;
        return py.PyLong_FromLong(0);
    }
    int vk = g_capture.exchange(-1);
    return py.PyLong_FromLong(vk > 0 ? vk : 0);
}

// key_name(code) -> the key's name on this keyboard ("." , "F5"...).
PyObject* keyName(PyObject*, PyObject* args) {
    int vk = 0;
    if (!py.PyArg_ParseTuple(args, "i", &vk)) return nullptr;
    UINT scan = MapVirtualKeyW(static_cast<UINT>(vk), 0 /* MAPVK_VK_TO_VSC */);
    LONG lparam = static_cast<LONG>(scan) << 16;
    if ((vk >= VK_PRIOR && vk <= VK_DOWN) || vk == VK_INSERT || vk == VK_DELETE || vk == VK_DIVIDE) lparam |= 1 << 24;
    wchar_t name[64] = {};
    std::string text = GetKeyNameTextW(lparam, name, 64) > 0 ? utf8(name) : "key " + std::to_string(vk);
    return py.Py_BuildValue("s", text.c_str());
}

PyMethodDef methods[] = {
    {"set_sims", setSims, METH_VARARGS, "set_sims(bytes of (id u64, breasts f32, butt f32, flags u32)) -> Sims found"},
    {"set", setValue, METH_VARARGS, "set(name, value): on, breasts, butt"},
    {"get", getValue, METH_VARARGS, "get(name) -> value of a integratedbodyphysics.ini setting"},
    {"status", status, METH_NOARGS, "(hooked, on, breasts, butt)"},
    {"size_of", sizeOf, METH_VARARGS, "size_of(object id) -> (size preset 0..3, measured scale) or None"},
    {"log", logText, METH_VARARGS, "log(text)"},
    {"menu", menuText, METH_NOARGS, "menu() -> the menu rows as text"},
    {"menu_key", menuKey, METH_NOARGS, "menu_key() -> 1 once per press of the menu key (not with the VR mod)"},
    {"vr", vr, METH_NOARGS, "vr() -> 1 when the Sims 4 VR mod is loaded"},
    {"capture", capture, METH_VARARGS, "capture(1) starts waiting for a key; capture(0) -> the key pressed (0 = none)"},
    {"key_name", keyName, METH_VARARGS, "key_name(code) -> the key's name"},
    {nullptr, nullptr, 0, nullptr},
};
PyModuleDef moduleDef = {{{1, nullptr}, nullptr, 0, nullptr}, "_integratedbodyphysics", "Integrated Body Physics", -1, methods,
                         nullptr, nullptr, nullptr, nullptr};

template <typename T>
bool resolveFn(HMODULE m, T& out, const char* name) {
    out = reinterpret_cast<T>(GetProcAddress(m, name));
    return out != nullptr;
}

bool loadApi() {
    HMODULE m = nullptr;
    for (const wchar_t* name : {L"python37_x64.dll", L"python37.dll", L"python3.dll"})
        if ((m = GetModuleHandleW(name))) break;
    if (!m) return false;
    return resolveFn(m, py.PyModule_Create2, "PyModule_Create2") && resolveFn(m, py.PyArg_ParseTuple, "PyArg_ParseTuple") &&
           resolveFn(m, py.PyLong_FromLong, "PyLong_FromLong") && resolveFn(m, py.Py_BuildValue, "Py_BuildValue") &&
           resolveFn(m, py.Py_IncRef, "Py_IncRef") && resolveFn(m, py.None, "_Py_NoneStruct");
}

}  // namespace

namespace jig {
void log(const char* fmt, ...) {
    char text[512];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(text, sizeof(text), fmt, args);
    va_end(args);
    logLine("%s", text);
}
}  // namespace jig

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        wchar_t path[MAX_PATH];
        DWORD n = GetModuleFileNameW(instance, path, MAX_PATH);
        std::wstring p(path, n);
        g_folder = p.substr(0, p.find_last_of(L"\\/") + 1);
        initPresets();
    }
    return TRUE;
}

// For other mods (the Sims 4 VR debug menu): read and change a setting by its
// integratedbodyphysics.ini key. 1 = known key.
extern "C" __declspec(dllexport) int ibp_get(const char* key, float* out) {
    Value* v = findValue(key);
    if (!v || !out) return 0;
    *out = v->v.load();
    return 1;
}
extern "C" __declspec(dllexport) int ibp_set(const char* key, float value) { return setValue(key, value) ? 1 : 0; }

// The settings menu as rows of text (src/menu.inc, docs/MENU-EXPORT.md); the values behind it go through
// ibp_get / ibp_set.
extern "C" __declspec(dllexport) const wchar_t* ibp_menu() { return kMenu; }

extern "C" __declspec(dllexport) PyObject* PyInit__integratedbodyphysics() {
    if (!loadApi()) return nullptr;
    install();
    return py.PyModule_Create2(&moduleDef, 1013);
}
