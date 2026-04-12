/*
 * jolt_wrapper.cpp — the single C++ translation unit in SafiEngine.
 *
 * Wraps Jolt Physics behind a flat C API consumed by physics_system.c.
 * Single-threaded (JobSystemSingleThreaded), adequate for hundreds of bodies.
 */

/* Jolt requires these before any Jolt include. */
#include <Jolt/Jolt.h>

#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyActivationListener.h>
#include <Jolt/Physics/Body/BodyInterface.h>

#include <cstring>

#include "jolt_wrapper.h"

JPH_SUPPRESS_WARNINGS

/* ---- Object / broad-phase layers ---------------------------------------- */

namespace {

namespace Layers {
    static constexpr JPH::ObjectLayer STATIC  = 0;
    static constexpr JPH::ObjectLayer DYNAMIC = 1;
}

namespace BroadPhaseLayers {
    static constexpr JPH::BroadPhaseLayer STATIC  = JPH::BroadPhaseLayer(0);
    static constexpr JPH::BroadPhaseLayer DYNAMIC = JPH::BroadPhaseLayer(1);
    static constexpr uint32_t NUM = 2;
}

class BPLayerInterface final : public JPH::BroadPhaseLayerInterface {
public:
    uint32_t GetNumBroadPhaseLayers() const override { return BroadPhaseLayers::NUM; }
    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override {
        return layer == Layers::STATIC ? BroadPhaseLayers::STATIC : BroadPhaseLayers::DYNAMIC;
    }
    const char *GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override {
        switch ((JPH::BroadPhaseLayer::Type)layer) {
        case 0: return "STATIC";
        case 1: return "DYNAMIC";
        default: return "?";
        }
    }
};

class ObjVsBP final : public JPH::ObjectVsBroadPhaseLayerFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer obj, JPH::BroadPhaseLayer bp) const override {
        if (obj == Layers::STATIC)
            return bp == BroadPhaseLayers::DYNAMIC;
        return true;
    }
};

class ObjVsObj final : public JPH::ObjectLayerPairFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override {
        if (a == Layers::STATIC && b == Layers::STATIC)
            return false;
        return true;
    }
};

/* ---- Module state ------------------------------------------------------- */

struct JoltState {
    bool initialized = false;
    JPH::PhysicsSystem           *physics_system    = nullptr;
    JPH::TempAllocatorImpl       *temp_allocator     = nullptr;
    JPH::JobSystemSingleThreaded *job_system         = nullptr;
    BPLayerInterface              bp_layer_iface;
    ObjVsBP                       obj_vs_bp;
    ObjVsObj                      obj_vs_obj;
};

static JoltState S;

static JPH::EMotionType map_motion(SafiJoltMotionType m) {
    switch (m) {
    case SAFI_JOLT_MOTION_STATIC:    return JPH::EMotionType::Static;
    case SAFI_JOLT_MOTION_KINEMATIC: return JPH::EMotionType::Kinematic;
    case SAFI_JOLT_MOTION_DYNAMIC:   return JPH::EMotionType::Dynamic;
    }
    return JPH::EMotionType::Dynamic;
}

static JPH::ObjectLayer layer_for_motion(SafiJoltMotionType m) {
    return m == SAFI_JOLT_MOTION_STATIC ? Layers::STATIC : Layers::DYNAMIC;
}

} /* anonymous namespace */

/* ---- C API -------------------------------------------------------------- */

extern "C" {

bool safi_jolt_init(void) {
    if (S.initialized) return true;

    JPH::RegisterDefaultAllocator();

    JPH::Factory::sInstance = new JPH::Factory();
    JPH::RegisterTypes();

    S.temp_allocator = new JPH::TempAllocatorImpl(10 * 1024 * 1024); /* 10 MB */
    S.job_system     = new JPH::JobSystemSingleThreaded(JPH::cMaxPhysicsJobs);

    constexpr uint32_t max_bodies       = 1024;
    constexpr uint32_t num_body_mutexes = 0;    /* 0 = auto */
    constexpr uint32_t max_body_pairs   = 1024;
    constexpr uint32_t max_contacts     = 1024;

    S.physics_system = new JPH::PhysicsSystem();
    S.physics_system->Init(
        max_bodies, num_body_mutexes, max_body_pairs, max_contacts,
        S.bp_layer_iface, S.obj_vs_bp, S.obj_vs_obj
    );

    S.physics_system->SetGravity(JPH::Vec3(0.0f, -9.81f, 0.0f));
    S.initialized = true;
    return true;
}

void safi_jolt_shutdown(void) {
    if (!S.initialized) return;

    delete S.physics_system;  S.physics_system  = nullptr;
    delete S.job_system;      S.job_system      = nullptr;
    delete S.temp_allocator;  S.temp_allocator   = nullptr;

    JPH::UnregisterTypes();
    delete JPH::Factory::sInstance;
    JPH::Factory::sInstance = nullptr;

    S.initialized = false;
}

void safi_jolt_step(float dt) {
    if (!S.initialized) return;
    S.physics_system->Update(dt, 1, S.temp_allocator, S.job_system);
}

/* ---- Body creation ------------------------------------------------------ */

static uint32_t add_body(const JPH::ShapeRefC &shape,
                         const float pos[3], const float rot[4],
                         float mass, float friction, float restitution,
                         SafiJoltMotionType motion) {
    JPH::BodyInterface &bi = S.physics_system->GetBodyInterface();

    JPH::RVec3 p(pos[0], pos[1], pos[2]);
    JPH::Quat  q(rot[0], rot[1], rot[2], rot[3]);

    JPH::EMotionType mt = map_motion(motion);
    JPH::ObjectLayer  ol = layer_for_motion(motion);

    JPH::BodyCreationSettings settings(shape, p, q, mt, ol);
    if (mt == JPH::EMotionType::Dynamic) {
        settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
        settings.mMassPropertiesOverride.mMass = mass > 0.0f ? mass : 1.0f;
    }
    settings.mFriction    = friction;
    settings.mRestitution = restitution;

    JPH::Body *body = bi.CreateBody(settings);
    if (!body) return UINT32_MAX;

    JPH::BodyID id = body->GetID();
    bi.AddBody(id, JPH::EActivation::Activate);
    return id.GetIndexAndSequenceNumber();
}

uint32_t safi_jolt_add_box(const float half_extents[3],
                           const float position[3],
                           const float rotation[4],
                           float mass, float friction, float restitution,
                           SafiJoltMotionType motion) {
    auto shape = new JPH::BoxShape(
        JPH::Vec3(half_extents[0], half_extents[1], half_extents[2])
    );
    return add_body(shape, position, rotation, mass, friction, restitution, motion);
}

uint32_t safi_jolt_add_sphere(float radius,
                              const float position[3],
                              const float rotation[4],
                              float mass, float friction, float restitution,
                              SafiJoltMotionType motion) {
    auto shape = new JPH::SphereShape(radius);
    return add_body(shape, position, rotation, mass, friction, restitution, motion);
}

/* ---- Body operations ---------------------------------------------------- */

void safi_jolt_remove_body(uint32_t body_id) {
    if (!S.initialized) return;
    JPH::BodyInterface &bi = S.physics_system->GetBodyInterface();
    JPH::BodyID id = JPH::BodyID(body_id);
    bi.RemoveBody(id);
    bi.DestroyBody(id);
}

void safi_jolt_get_transform(uint32_t body_id, float pos[3], float rot[4]) {
    JPH::BodyInterface &bi = S.physics_system->GetBodyInterface();
    JPH::BodyID id = JPH::BodyID(body_id);
    JPH::RVec3 p = bi.GetPosition(id);
    JPH::Quat  q = bi.GetRotation(id);
    pos[0] = (float)p.GetX(); pos[1] = (float)p.GetY(); pos[2] = (float)p.GetZ();
    rot[0] = q.GetX(); rot[1] = q.GetY(); rot[2] = q.GetZ(); rot[3] = q.GetW();
}

void safi_jolt_set_transform(uint32_t body_id, const float pos[3], const float rot[4]) {
    JPH::BodyInterface &bi = S.physics_system->GetBodyInterface();
    JPH::BodyID id = JPH::BodyID(body_id);
    bi.SetPositionAndRotation(
        id,
        JPH::RVec3(pos[0], pos[1], pos[2]),
        JPH::Quat(rot[0], rot[1], rot[2], rot[3]),
        JPH::EActivation::Activate
    );
}

void safi_jolt_add_force(uint32_t body_id, const float force[3]) {
    JPH::BodyInterface &bi = S.physics_system->GetBodyInterface();
    bi.AddForce(JPH::BodyID(body_id), JPH::Vec3(force[0], force[1], force[2]));
}

void safi_jolt_add_impulse(uint32_t body_id, const float impulse[3]) {
    JPH::BodyInterface &bi = S.physics_system->GetBodyInterface();
    bi.AddImpulse(JPH::BodyID(body_id), JPH::Vec3(impulse[0], impulse[1], impulse[2]));
}

void safi_jolt_set_linear_velocity(uint32_t body_id, const float vel[3]) {
    JPH::BodyInterface &bi = S.physics_system->GetBodyInterface();
    bi.SetLinearVelocity(JPH::BodyID(body_id), JPH::Vec3(vel[0], vel[1], vel[2]));
}

void safi_jolt_set_angular_velocity(uint32_t body_id, const float vel[3]) {
    JPH::BodyInterface &bi = S.physics_system->GetBodyInterface();
    bi.SetAngularVelocity(JPH::BodyID(body_id), JPH::Vec3(vel[0], vel[1], vel[2]));
}

bool safi_jolt_is_active(uint32_t body_id) {
    JPH::BodyInterface &bi = S.physics_system->GetBodyInterface();
    return bi.IsActive(JPH::BodyID(body_id));
}

void safi_jolt_activate_body(uint32_t body_id) {
    JPH::BodyInterface &bi = S.physics_system->GetBodyInterface();
    bi.ActivateBody(JPH::BodyID(body_id));
}

} /* extern "C" */
