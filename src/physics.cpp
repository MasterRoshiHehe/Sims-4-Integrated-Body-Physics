#include "physics.h"

#include <Jolt/Jolt.h>

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Constraints/SixDOFConstraint.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>

namespace jig {
void log(const char* fmt, ...);
}

namespace jig::phys {
namespace {

using namespace JPH;

// ---------------------------------------------------------------------------
// Lock-free hand-over between the pose hook (game job threads) and the
// physics thread: neither ever waits on the other.

template <typename T>
class SeqLock {
public:
    void write(const T& value) {
        while (writing_.test_and_set(std::memory_order_acquire)) {
        }
        uint32_t s = seq_.load(std::memory_order_relaxed);
        seq_.store(s + 1, std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_release);
        std::memcpy(&data_, &value, sizeof(T));
        std::atomic_thread_fence(std::memory_order_release);
        seq_.store(s + 2, std::memory_order_release);
        writing_.clear(std::memory_order_release);
    }
    bool read(T& out) const {
        for (int tries = 0; tries < 16; ++tries) {
            uint32_t before = seq_.load(std::memory_order_acquire);
            if (before & 1) continue;
            std::memcpy(&out, &data_, sizeof(T));
            std::atomic_thread_fence(std::memory_order_acquire);
            if (seq_.load(std::memory_order_relaxed) == before) return before != 0;
        }
        return false;
    }

private:
    std::atomic<uint32_t> seq_{0};
    std::atomic_flag writing_ = ATOMIC_FLAG_INIT;
    T data_{};
};

struct Stamped {
    Input in;
    ULONGLONG tick;
};
struct Output {
    void* actor;
    float offset[kBones][3];
    float squish[kBones][3];
};
SeqLock<Stamped> g_in[kSlots];
SeqLock<Output> g_out[kSlots];
SeqLock<Params> g_params;

// ---------------------------------------------------------------------------
// Layers: arms push the jiggle balls; anchors (what the springs hold on to)
// touch nothing. STATIC is for furniture later.

namespace Layers {
constexpr ObjectLayer STATIC = 0, PARTS = 1, SOFT = 2, ANCHOR = 3, COUNT = 4;
}
namespace BPLayers {
constexpr BroadPhaseLayer STILL(0), MOVING(1);
constexpr uint32_t COUNT = 2;
}

class BPInterface final : public BroadPhaseLayerInterface {
public:
    uint32_t GetNumBroadPhaseLayers() const override { return BPLayers::COUNT; }
    BroadPhaseLayer GetBroadPhaseLayer(ObjectLayer layer) const override {
        return layer == Layers::STATIC ? BPLayers::STILL : BPLayers::MOVING;
    }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(BroadPhaseLayer) const override { return "layer"; }
#endif
};
class ObjectVsBP final : public ObjectVsBroadPhaseLayerFilter {
public:
    bool ShouldCollide(ObjectLayer layer, BroadPhaseLayer bp) const override {
        if (layer == Layers::SOFT) return true;
        if (layer == Layers::PARTS) return bp == BPLayers::MOVING;
        return false;
    }
};
class PairFilter final : public ObjectLayerPairFilter {
public:
    bool ShouldCollide(ObjectLayer a, ObjectLayer b) const override {
        if (a > b) std::swap(a, b);
        return (a == Layers::STATIC && b == Layers::SOFT) || (a == Layers::PARTS && b == Layers::SOFT);
    }
};

BPInterface g_bp;
ObjectVsBP g_objVsBp;
PairFilter g_pairs;
PhysicsSystem* g_system = nullptr;
TempAllocatorImpl* g_temp = nullptr;
JobSystemSingleThreaded* g_jobs = nullptr;

// What the world holds for one Sim (touched by the physics thread only).
struct World {
    void* actor = nullptr;
    bool made = false;
    BodyID anchor[kBones], ball[kBones], part[kParts];
    float partLength[kParts] = {};
    Ref<SixDOFConstraint> spring[kBones];
    bool use[kBones] = {};
    float size = 1;
    Vec3 last[kBones];
    float appliedHz[kBones] = {}, appliedDamping[kBones] = {};
    float appliedGravity[kBones] = {}, appliedRadius[kBones] = {}, appliedMass[kBones] = {};
    float appliedForce[kBones] = {};
    Vec3 curLo[kBones], curHi[kBones];  // the limits in use (eased back after a grab)
    float appliedArm = 0;  // the arm thickness the arm parts were made with
    float appliedYours = 0;  // the extra thickness your parts were made with
    int grabHand[kBones] = {-1, -1, -1, -1};  // your hand holding this ball (0 left, 1 right)
    Vec3 grabOffset[kBones];  // ball minus your palm when it closed
    bool wasGrip[2] = {};
    Vec3 squish[kBones];  // how much each ball is pressed (Sim frame)
};
World g_world[kSlots];

Vec3 vec(const float* f) { return Vec3(f[0], f[1], f[2]); }
Quat quat(const float* f) {
    Quat q(f[0], f[1], f[2], f[3]);
    return q.LengthSq() > 1e-6f ? q.Normalized() : Quat::sIdentity();
}

constexpr float kMinRadius = 0.01f, kMaxRadius = 0.15f;
float gravityOf(const Input& in, int b) { return std::clamp(in.gravity[b], -1.0f, 1.0f); }
float hzOf(const Input& in, int b) { return std::clamp(in.hz[b], 0.2f, 30.0f); }
float dampingOf(const Input& in, int b) { return std::clamp(in.damping[b], 0.0f, 2.0f); }
float ballRadius(const Input& in, int b) { return std::clamp(in.radius[b], kMinRadius, kMaxRadius) * in.size; }
// Your hands meet the balls at their own radius: your parts are made thicker or thinner by the difference.
float yoursExtra(const Input& in) {
    for (int b = 0; b < 2; ++b)
        if (in.use[b]) return (std::clamp(in.yourRadius[b], kMinRadius, kMaxRadius) - std::clamp(in.radius[b], kMinRadius, kMaxRadius)) * in.size;
    return 0;
}
// The limits (Sim frame, metres): never zero on a side, so the axis stays limited, not fixed.
Vec3 loOf(const Input& in, int b) {
    return Vec3(std::min(-0.0005f, -std::fabs(in.lo[b][0])), std::min(-0.0005f, -std::fabs(in.lo[b][1])),
                std::min(-0.0005f, -std::fabs(in.lo[b][2]))) * in.size;
}
Vec3 hiOf(const Input& in, int b) {
    return Vec3(std::max(0.0005f, std::fabs(in.hi[b][0])), std::max(0.0005f, std::fabs(in.hi[b][1])),
                std::max(0.0005f, std::fabs(in.hi[b][2]))) * in.size;
}
constexpr float kBallMass[2] = {0.4f, 0.6f};
// The most force a spring pulls with (N). Light balls never reach it; heavy ones do on sharp moves and
// fall behind, then swing back slower: their weight shows.
constexpr float kSpringForce = 6.0f;
float massOf(const Input& in, int b) { return in.mass[b] > 0 ? std::clamp(in.mass[b], 0.05f, 5.0f) : kBallMass[b < 2 ? 0 : 1]; }
// Always enough to hold the ball up against full gravity, plus kSpringForce for moving it; a held ball
// follows your hand with ten times that.
float springForce(const Input& in, int b, bool held) {
    if (!(in.mass[b] > 0)) return 1e6f;  // Weight off: never capped (as before 0.11.1)
    return (massOf(in, b) * 9.81f + kSpringForce * (held ? 10.0f : 1.0f)) * in.size;
}
constexpr float kPartRadius[kParts] = {0.045f, 0.045f, 0.035f, 0.035f, 0.04f, 0.04f,   // hers
                                       0.035f, 0.035f, 0.04f, 0.04f, 0.022f, 0.022f};  // yours: forearms, palms, fingers

// A capsule from a to b: centre, and the turn taking +Y along a->b.
void capsulePlace(Vec3 a, Vec3 b, RVec3& centre, Quat& rotation, float& length) {
    Vec3 d = b - a;
    length = d.Length();
    centre = RVec3(0.5f * (a + b));
    rotation = length > 1e-4f ? Quat::sFromTo(Vec3::sAxisY(), d / length) : Quat::sIdentity();
}

void destroy(World& w) {
    BodyInterface& bi = g_system->GetBodyInterfaceNoLock();
    for (int b = 0; b < kBones; ++b) {
        if (w.spring[b]) {
            g_system->RemoveConstraint(w.spring[b]);
            w.spring[b] = nullptr;
        }
        for (BodyID* id : {&w.ball[b], &w.anchor[b]})
            if (!id->IsInvalid()) {
                bi.RemoveBody(*id);
                bi.DestroyBody(*id);
                *id = BodyID();
            }
    }
    for (BodyID& id : w.part)
        if (!id.IsInvalid()) {
            bi.RemoveBody(id);
            bi.DestroyBody(id);
            id = BodyID();
        }
    w = World{};
}

void makeSpring(World& w, int b, const Input& in) {
    BodyInterface& bi = g_system->GetBodyInterfaceNoLock();
    int kind = b < 2 ? 0 : 1;
    RVec3 at(vec(in.rest[b]));
    Quat q = quat(in.frame[b]);
    BodyCreationSettings anchor(new SphereShape(0.01f), at, q, EMotionType::Kinematic, Layers::ANCHOR);
    w.anchor[b] = bi.CreateAndAddBody(anchor, EActivation::Activate);
    float radius = ballRadius(in, b);
    BodyCreationSettings ball(new SphereShape(radius), at, q, EMotionType::Dynamic, Layers::SOFT);
    ball.mGravityFactor = gravityOf(in, b);  // 0: the pose already hangs them, only movement swings them
    ball.mLinearDamping = 0;
    ball.mAngularDamping = 0.5f;
    ball.mAllowSleeping = false;
    ball.mOverrideMassProperties = EOverrideMassProperties::CalculateInertia;
    ball.mMassPropertiesOverride.mMass = massOf(in, b);
    w.ball[b] = bi.CreateAndAddBody(ball, EActivation::Activate);
    if (w.anchor[b].IsInvalid() || w.ball[b].IsInvalid()) return;

    SixDOFConstraintSettings s;
    s.mSpace = EConstraintSpace::WorldSpace;
    s.mPosition1 = s.mPosition2 = at;
    s.mAxisX1 = s.mAxisX2 = q.RotateAxisX();
    s.mAxisY1 = s.mAxisY2 = q.RotateAxisY();
    Vec3 lo = loOf(in, b), hi = hiOf(in, b);
    int i = 0;
    for (SixDOFConstraintSettings::EAxis axis :
         {SixDOFConstraintSettings::TranslationX, SixDOFConstraintSettings::TranslationY, SixDOFConstraintSettings::TranslationZ}) {
        s.SetLimitedAxis(axis, lo[i], hi[i]);
        s.mMotorSettings[axis] = MotorSettings(hzOf(in, b), dampingOf(in, b));
        s.mMotorSettings[axis].SetForceLimit(springForce(in, b, false));
        ++i;
    }
    for (SixDOFConstraintSettings::EAxis axis :
         {SixDOFConstraintSettings::RotationX, SixDOFConstraintSettings::RotationY, SixDOFConstraintSettings::RotationZ})
        s.MakeFreeAxis(axis);
    Body* b1 = g_system->GetBodyLockInterfaceNoLock().TryGetBody(w.anchor[b]);
    Body* b2 = g_system->GetBodyLockInterfaceNoLock().TryGetBody(w.ball[b]);
    if (!b1 || !b2) return;
    w.spring[b] = static_cast<SixDOFConstraint*>(s.Create(*b1, *b2));
    for (SixDOFConstraintSettings::EAxis axis :
         {SixDOFConstraintSettings::TranslationX, SixDOFConstraintSettings::TranslationY, SixDOFConstraintSettings::TranslationZ})
        w.spring[b]->SetMotorState(axis, EMotorState::Position);
    w.spring[b]->SetTargetPositionCS(Vec3::sZero());
    g_system->AddConstraint(w.spring[b]);
    w.appliedHz[b] = hzOf(in, b);
    w.appliedDamping[b] = dampingOf(in, b);
    w.curLo[b] = lo;
    w.curHi[b] = hi;
    w.grabHand[b] = -1;
    w.appliedGravity[b] = gravityOf(in, b);
    w.appliedRadius[b] = radius;
    w.appliedMass[b] = massOf(in, b);
    w.appliedForce[b] = springForce(in, b, false);
    w.last[b] = vec(in.rest[b]);
}

void makePart(World& w, int k, const Input& in, float armSize, float yours) {
    BodyInterface& bi = g_system->GetBodyInterfaceNoLock();
    RVec3 centre;
    Quat rot;
    float length;
    capsulePlace(vec(in.a[k]), vec(in.b[k]), centre, rot, length);
    float r = kPartRadius[k] * std::clamp(armSize, 0.25f, 3.0f) * in.size;
    if (k >= 6) r = std::max(0.005f, r + yours);
    float half = std::max(0.005f, 0.5f * length);
    BodyCreationSettings part(new CapsuleShape(half, r), centre, rot, EMotionType::Kinematic, Layers::PARTS);
    w.part[k] = bi.CreateAndAddBody(part, EActivation::Activate);
    w.partLength[k] = length;
}

std::atomic<int> g_logs{0};

float partRadiusOf(int k, const Params& p, const Input& in, float yours) {
    float r = kPartRadius[k] * std::clamp(p.armSize, 0.25f, 3.0f) * in.size;
    return k >= 6 ? std::max(0.005f, r + yours) : r;
}

Vec3 closestOnSegment(Vec3 a, Vec3 b, Vec3 x) {
    Vec3 d = b - a;
    float len2 = d.LengthSq();
    float t = len2 > 1e-8f ? std::clamp((x - a).Dot(d) / len2, 0.0f, 1.0f) : 0.0f;
    return a + d * t;
}

constexpr int kPalm[2] = {8, 9};  // your parts: left and right palm (hand to Mid0)

void stepSlot(int i, ULONGLONG now, const Params& p, float dt) {
    World& w = g_world[i];
    Stamped s;
    bool have = g_in[i].read(s) && s.in.actor && now - s.tick < 3000;
    if (!have || s.in.actor != w.actor) {
        if (w.actor || w.made) destroy(w);
        if (!have) return;
        w.actor = s.in.actor;
    }
    const Input& in = s.in;
    BodyInterface& bi = g_system->GetBodyInterfaceNoLock();
    w.size = in.size;
    const float yours = yoursExtra(in);
    // Your palms (for holding), when your hands are here.
    bool palmHere[2];
    Vec3 palm[2];
    for (int h = 0; h < 2; ++h) {
        palmHere[h] = p.armPush && in.part[kPalm[h]];
        palm[h] = palmHere[h] ? 0.5f * (vec(in.a[kPalm[h]]) + vec(in.b[kPalm[h]])) : Vec3::sZero();
    }
    bool closing[2];
    for (int h = 0; h < 2; ++h) {
        bool grip = palmHere[h] && in.grip[h];
        closing[h] = grip && !w.wasGrip[h];
        w.wasGrip[h] = grip;
    }
    for (int b = 0; b < kBones; ++b) {
        if (in.use[b] && w.ball[b].IsInvalid()) makeSpring(w, b, in);
        if (w.ball[b].IsInvalid() || w.anchor[b].IsInvalid()) continue;
        Vec3 rest = vec(in.rest[b]);
        Quat q = quat(in.frame[b]);
        if ((rest - w.last[b]).Length() > 0.5f) {  // the Sim jumped: start again there
            bi.SetPositionAndRotation(w.anchor[b], RVec3(rest), q, EActivation::Activate);
            bi.SetPositionAndRotation(w.ball[b], RVec3(rest), q, EActivation::Activate);
            bi.SetLinearAndAngularVelocity(w.ball[b], Vec3::sZero(), Vec3::sZero());
            w.grabHand[b] = -1;
        } else {
            bi.MoveKinematic(w.anchor[b], RVec3(rest), q, dt);
        }
        w.last[b] = rest;
        const float radius = ballRadius(in, b);

        // Holding: a hand that closes on a breast holds it until it opens (breasts only).
        if (b < 2 && w.spring[b]) {
            int& hand = w.grabHand[b];
            if (hand >= 0 && (!p.grab || !w.wasGrip[hand])) {
                hand = -1;
                w.spring[b]->SetTargetPositionCS(Vec3::sZero());
            }
            if (hand < 0 && p.grab) {
                Vec3 ball(bi.GetCenterOfMassPosition(w.ball[b]));
                for (int h = 0; h < 2; ++h) {
                    if (!w.wasGrip[h]) continue;  // both buttons held: takes it as soon as the palm reaches it
                    bool taken = false;
                    for (int o = 0; o < 2; ++o) taken |= o != b && w.grabHand[o] == h;
                    float reach = radius + partRadiusOf(kPalm[h], p, in, yours) + 0.08f * in.size;
                    float away = (ball - palm[h]).Length();
                    static std::atomic<int> tryLogs{0};
                    if (!taken && closing[h] && tryLogs.fetch_add(1) < 40)
                        jig::log("Physics: your %s hand closed %.0f mm from a breast (holds within %.0f mm)",
                                 h ? "right" : "left", away * 1000, reach * 1000);
                    if (taken || away > reach) continue;
                    hand = h;
                    w.grabOffset[b] = ball - palm[h];
                    static std::atomic<int> grabLogs{0};
                    if (grabLogs.fetch_add(1) < 40) jig::log("Physics: your %s hand holds a breast", h ? "right" : "left");
                    break;
                }
            }
            if (hand >= 0) {
                Vec3 want = palm[hand] + w.grabOffset[b] - rest;
                float reach = std::clamp(p.grabReach, 0.01f, 0.4f) * in.size;
                if (want.Length() > reach) want = want.Normalized() * reach;
                w.spring[b]->SetTargetPositionCS(q.Conjugated() * want);
            }
        }

        const float hz = hzOf(in, b) * (w.grabHand[b] >= 0 ? 3.0f : 1.0f), damping = dampingOf(in, b);
        const float force = springForce(in, b, w.grabHand[b] >= 0);
        if (w.spring[b] && (w.appliedHz[b] != hz || w.appliedDamping[b] != damping || w.appliedForce[b] != force)) {
            for (SixDOFConstraintSettings::EAxis axis : {SixDOFConstraintSettings::TranslationX,
                                                         SixDOFConstraintSettings::TranslationY,
                                                         SixDOFConstraintSettings::TranslationZ}) {
                MotorSettings& motor = w.spring[b]->GetMotorSettings(axis);
                motor.mSpringSettings.mFrequency = hz;
                motor.mSpringSettings.mDamping = damping;
                motor.SetForceLimit(force);
            }
            w.appliedForce[b] = force;
            w.appliedHz[b] = hz;
            w.appliedDamping[b] = damping;
        }
        // Limits: held, as far as the reach; let go, eased back in (no snap).
        Vec3 lo = loOf(in, b), hi = hiOf(in, b);
        if (w.grabHand[b] >= 0) {
            float reach = std::clamp(p.grabReach, 0.01f, 0.4f) * in.size;
            lo = Vec3::sMin(lo, Vec3::sReplicate(-reach));
            hi = Vec3::sMax(hi, Vec3::sReplicate(reach));
        }
        // Widening is at once; narrowing eases at 40 cm a second.
        const float ease = 0.4f * dt;
        Vec3 curLo, curHi;
        for (int x = 0; x < 3; ++x) {
            float cl = w.curLo[b][x], ch = w.curHi[b][x];
            curLo.SetComponent(x, lo[x] < cl ? lo[x] : std::min(lo[x], cl + ease));
            curHi.SetComponent(x, hi[x] > ch ? hi[x] : std::max(hi[x], ch - ease));
        }
        if (w.spring[b] && (curLo != w.curLo[b] || curHi != w.curHi[b])) {
            w.spring[b]->SetTranslationLimits(curLo, curHi);
            w.curLo[b] = curLo;
            w.curHi[b] = curHi;
        }
        const float gravity = gravityOf(in, b);
        if (w.appliedGravity[b] != gravity) {
            bi.SetGravityFactor(w.ball[b], gravity);
            w.appliedGravity[b] = gravity;
        }
        if (std::fabs(w.appliedMass[b] - massOf(in, b)) > 1e-3f) {  // new weight: same shape, new mass
            MassProperties m;
            m.ScaleToMass(1);
            m.SetMassAndInertiaOfSolidBox(Vec3::sReplicate(2 * radius), 1000);
            m.ScaleToMass(massOf(in, b));
            BodyLockWrite lock(g_system->GetBodyLockInterfaceNoLock(), w.ball[b]);
            if (lock.Succeeded()) {
                lock.GetBody().GetMotionProperties()->SetMassProperties(EAllowedDOFs::All, m);
                w.appliedMass[b] = massOf(in, b);
            }
        }
        if (std::fabs(w.appliedRadius[b] - radius) > 1e-4f) {
            bi.SetShape(w.ball[b], new SphereShape(radius), false, EActivation::Activate);
            w.appliedRadius[b] = radius;
        }
        // Squish: how deep arms and hands reach into where the breast rests (Sim frame, / its diameter).
        Vec3 press = Vec3::sZero();
        if (p.armPush)
            for (int k = 0; k < kParts; ++k) {
                if (!in.part[k]) continue;
                Vec3 c = closestOnSegment(vec(in.a[k]), vec(in.b[k]), rest);
                Vec3 d = rest - c;
                float dist = d.Length();
                float depth = radius + partRadiusOf(k, p, in, yours) - dist;
                if (depth <= 0 || dist < 1e-5f) continue;
                press += d / dist * (depth / (2 * radius));
            }
        w.squish[b] = q.Conjugated() * press;
    }
    // Arms: gone when they don't push, made again when their thickness changes.
    if (!p.armPush || w.appliedArm != p.armSize || std::fabs(w.appliedYours - yours) > 0.001f) {
        for (int k = 0; k < kParts; ++k) {
            BodyID& id = w.part[k];
            bool remake = !p.armPush || w.appliedArm != p.armSize || k >= 6;
            if (!remake || id.IsInvalid()) continue;
            bi.RemoveBody(id);
            bi.DestroyBody(id);
            id = BodyID();
        }
        w.appliedArm = p.armSize;
        w.appliedYours = yours;
    }
    for (int k = 0; k < kParts; ++k) {
        if (!p.armPush || !in.part[k]) {
            if (!w.part[k].IsInvalid()) {  // gone (your hand out of reach): no ghost left behind
                bi.RemoveBody(w.part[k]);
                bi.DestroyBody(w.part[k]);
                w.part[k] = BodyID();
            }
            continue;
        }
        RVec3 centre;
        Quat rot;
        float length;
        capsulePlace(vec(in.a[k]), vec(in.b[k]), centre, rot, length);
        if (!w.part[k].IsInvalid() && std::fabs(length - w.partLength[k]) > 0.2f * w.partLength[k] + 0.01f) {
            bi.RemoveBody(w.part[k]);
            bi.DestroyBody(w.part[k]);
            w.part[k] = BodyID();
        }
        if (w.part[k].IsInvalid()) {
            makePart(w, k, in, p.armSize, w.appliedYours);
            continue;
        }
        if ((Vec3(bi.GetCenterOfMassPosition(w.part[k])) - Vec3(centre)).Length() > 0.5f)
            bi.SetPositionAndRotation(w.part[k], centre, rot, EActivation::Activate);
        else
            bi.MoveKinematic(w.part[k], centre, rot, dt);
    }
    if (!w.made) {
        w.made = true;
        if (g_logs.fetch_add(1) < 10)
            jig::log("Physics: Sim %p in the world (balls %d%d%d%d, arm parts %d%d%d%d%d%d)", w.actor, in.use[0],
                     in.use[1], in.use[2], in.use[3], in.part[0], in.part[1], in.part[2], in.part[3], in.part[4],
                     in.part[5]);
    }
}

void readBack(int i) {
    World& w = g_world[i];
    if (!w.actor) return;
    BodyInterface& bi = g_system->GetBodyInterfaceNoLock();
    Output out{};
    out.actor = w.actor;
    for (int b = 0; b < kBones; ++b) {
        if (w.ball[b].IsInvalid() || w.anchor[b].IsInvalid()) continue;
        RVec3 ball = bi.GetCenterOfMassPosition(w.ball[b]);
        RVec3 anchor = bi.GetCenterOfMassPosition(w.anchor[b]);
        Quat q = bi.GetRotation(w.anchor[b]);
        Vec3 off = q.Conjugated() * Vec3(ball - anchor);
        if (!off.IsNaN()) {
            out.offset[b][0] = off.GetX();
            out.offset[b][1] = off.GetY();
            out.offset[b][2] = off.GetZ();
        }
        Vec3 sq = w.squish[b];
        if (!sq.IsNaN()) {
            out.squish[b][0] = sq.GetX();
            out.squish[b][1] = sq.GetY();
            out.squish[b][2] = sq.GetZ();
        }
    }
    g_out[i].write(out);
}

DWORD WINAPI run(LPVOID) {
    constexpr float kStep = 1.0f / 120.0f;
    LARGE_INTEGER freq, last, now;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&last);
    float behind = 0;
    ULONGLONG statsAt = GetTickCount64();
    long long steps = 0, ticks = 0;
    double offsetSum = 0, offsetMax = 0;
    long long offsetCount = 0;
    for (;;) {
        Sleep(2);
        QueryPerformanceCounter(&now);
        behind += float(now.QuadPart - last.QuadPart) / float(freq.QuadPart);
        last = now;
        if (behind < kStep) continue;
        if (behind > 4 * kStep) behind = 4 * kStep;  // fell far behind (a hitch): don't try to catch up
        Params p;
        if (!g_params.read(p)) continue;
        ULONGLONG tick = GetTickCount64();
        while (behind >= kStep) {
            behind -= kStep;
            LARGE_INTEGER a, b;
            QueryPerformanceCounter(&a);
            for (int i = 0; i < kSlots; ++i) stepSlot(i, tick, p, kStep);
            g_system->Update(kStep, 1, g_temp, g_jobs);
            for (int i = 0; i < kSlots; ++i) readBack(i);
            for (int i = 0; i < kSlots; ++i) {  // how far the balls sit off their rest place, for the log
                Output o;
                if (!g_world[i].actor || !g_out[i].read(o)) continue;
                for (int bone = 0; bone < kBones; ++bone) {
                    if (g_world[i].ball[bone].IsInvalid()) continue;
                    double d = std::sqrt(o.offset[bone][0] * o.offset[bone][0] + o.offset[bone][1] * o.offset[bone][1] +
                                         o.offset[bone][2] * o.offset[bone][2]);
                    offsetSum += d;
                    offsetMax = std::max(offsetMax, d);
                    ++offsetCount;
                }
            }
            QueryPerformanceCounter(&b);
            ticks += b.QuadPart - a.QuadPart;
            ++steps;
        }
        if (tick - statsAt > 60000) {
            int sims = 0;
            for (const World& w : g_world) sims += w.actor != nullptr;
            jig::log("Physics: last minute %lld steps, %.0f microseconds each on average, %d Sims in the world; balls "
                     "%.1f mm off their rest place on average, %.1f mm at most",
                     steps, steps ? ticks * 1e6 / double(freq.QuadPart) / steps : 0.0, sims,
                     offsetCount ? offsetSum * 1000 / offsetCount : 0.0, offsetMax * 1000);
            statsAt = tick;
            steps = ticks = 0;
            offsetSum = offsetMax = 0;
            offsetCount = 0;
        }
    }
}

}  // namespace

bool start(const Params& params) {
    g_params.write(params);
    RegisterDefaultAllocator();
    Factory::sInstance = new Factory();
    RegisterTypes();
    g_temp = new TempAllocatorImpl(4 * 1024 * 1024);
    g_jobs = new JobSystemSingleThreaded(cMaxPhysicsJobs);
    g_system = new PhysicsSystem();
    g_system->Init(2048, 0, 4096, 2048, g_bp, g_objVsBp, g_pairs);
    g_system->SetGravity(Vec3(0, -9.81f, 0));  // the game's Y is up; each ball's own factor (0 by default) scales it
    // An arm pressing in must win over the spring: correct overlaps faster.
    PhysicsSettings settings = g_system->GetPhysicsSettings();
    settings.mBaumgarte = 0.8f;
    settings.mNumPositionSteps = 4;
    settings.mPenetrationSlop = 0.002f;
    g_system->SetPhysicsSettings(settings);
    HANDLE thread = CreateThread(nullptr, 0, run, nullptr, 0, nullptr);
    if (!thread) return false;
    SetThreadPriority(thread, THREAD_PRIORITY_ABOVE_NORMAL);
    CloseHandle(thread);
    return true;
}

void setParams(const Params& params) { g_params.write(params); }

void publish(int slot, const Input& in) {
    if (slot < 0 || slot >= kSlots) return;
    Stamped s{in, GetTickCount64()};
    g_in[slot].write(s);
}

bool offsets(int slot, void* actor, float out[kBones][3], float squish[kBones][3]) {
    if (slot < 0 || slot >= kSlots) return false;
    Output o;
    if (!g_out[slot].read(o) || o.actor != actor) return false;
    std::memcpy(out, o.offset, sizeof(o.offset));
    std::memcpy(squish, o.squish, sizeof(o.squish));
    return true;
}

}  // namespace jig::phys
