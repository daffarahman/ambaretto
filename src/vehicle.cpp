#include "vehicle.hpp"
#include "environment.hpp"
#include "player.hpp"
#include "plane.hpp"
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayerInterfaceTable.h>
#include <Jolt/Physics/Collision/BroadPhase/ObjectVsBroadPhaseLayerFilterTable.h>
#include <Jolt/Physics/Collision/ObjectLayerPairFilterTable.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/PlaneShape.h>
#include <Jolt/Physics/Collision/Shape/OffsetCenterOfMassShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/CylinderShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/ShapeCast.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Ragdoll/Ragdoll.h>
#include <Jolt/Physics/Constraints/SwingTwistConstraint.h>
#include <Jolt/Physics/Constraints/HingeConstraint.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace forza {
namespace {
constexpr JPH::ObjectLayer ground_layer = 0;
constexpr JPH::ObjectLayer vehicle_layer = 1;
constexpr JPH::ObjectLayer obstacle_layer = 2;
constexpr JPH::ObjectLayer character_layer = 3;
constexpr float mass = 1100;

class CameraLayers final : public JPH::ObjectLayerFilter {
    bool ShouldCollide(JPH::ObjectLayer layer) const override { return layer != character_layer; }
};

float clamp(float value, float low, float high) {
    return std::clamp(value, low, high);
}

// Registration is process-wide. Reset can create a new world while the old
// scene is still alive, so registration must outlive every PhysicsWorld.
struct JoltRuntime {
    JoltRuntime() {
        JPH::RegisterDefaultAllocator();
        JPH::Factory::sInstance = new JPH::Factory();
        JPH::RegisterTypes();
    }
    ~JoltRuntime() {
        JPH::UnregisterTypes();
        delete JPH::Factory::sInstance;
        JPH::Factory::sInstance = nullptr;
    }
};
void initialize_jolt() {
    static JoltRuntime runtime;
}
} // namespace

struct PhysicsWorld::Impl {
    JPH::BroadPhaseLayerInterfaceTable broad_phase{4, 2};
    JPH::ObjectLayerPairFilterTable pairs{4};
    std::unique_ptr<JPH::ObjectVsBroadPhaseLayerFilterTable> broad_phase_filter;
    JPH::TempAllocatorImpl allocator{64 * 1024 * 1024};
    JPH::JobSystemThreadPool jobs{JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, 2};
    JPH::PhysicsSystem system;
    std::vector<JPH::BodyID> bodies;
    JPH::CollisionGroup::GroupID next_character_group = 1;

    Impl() {
        broad_phase.MapObjectToBroadPhaseLayer(ground_layer, JPH::BroadPhaseLayer(0));
        broad_phase.MapObjectToBroadPhaseLayer(vehicle_layer, JPH::BroadPhaseLayer(1));
        broad_phase.MapObjectToBroadPhaseLayer(obstacle_layer, JPH::BroadPhaseLayer(0));
        broad_phase.MapObjectToBroadPhaseLayer(character_layer, JPH::BroadPhaseLayer(1));
        pairs.EnableCollision(ground_layer, vehicle_layer);
        pairs.EnableCollision(vehicle_layer, vehicle_layer);
        pairs.EnableCollision(vehicle_layer, obstacle_layer);
        for (JPH::ObjectLayer layer : {ground_layer, vehicle_layer, obstacle_layer, character_layer})
            pairs.EnableCollision(character_layer, layer);
        broad_phase_filter = std::make_unique<JPH::ObjectVsBroadPhaseLayerFilterTable>(broad_phase, 2, pairs, 4);
        system.Init(16384, 0, 16384, 16384, broad_phase, *broad_phase_filter, pairs);
        system.SetGravity(Vec3(0, -9.81f, 0));
        auto settings = system.GetPhysicsSettings();
        settings.mNumVelocitySteps = 12;
        settings.mNumPositionSteps = 4;
        system.SetPhysicsSettings(settings);
    }
    ~Impl() {
        auto& interface = system.GetBodyInterface();
        for (const auto& id : bodies) {
            if (interface.IsAdded(id)) interface.RemoveBody(id);
            interface.DestroyBody(id);
        }
    }
    JPH::BodyID add_body(const JPH::BodyCreationSettings& settings) {
        auto id = system.GetBodyInterface().CreateAndAddBody(settings,
            settings.mMotionType == JPH::EMotionType::Static ? JPH::EActivation::DontActivate : JPH::EActivation::Activate);
        if (id.IsInvalid()) throw std::runtime_error("Jolt could not allocate a rigid body");
        bodies.push_back(id);
        return id;
    }
};

PhysicsWorld::PhysicsWorld(bool with_ridges) {
    initialize_jolt();
    impl_ = std::make_unique<Impl>();
    // The flat test track must support sustained driving at the tunable speeds.
    JPH::RefConst<JPH::Shape> ground = new JPH::PlaneShape(JPH::Plane(Vec3(0, 1, 0), 0), nullptr, 10000);
    JPH::BodyCreationSettings floor(ground, Vec3(0, 0, 0), Quat::sIdentity(), JPH::EMotionType::Static, ground_layer);
    floor.mFriction = 0.8f;
    impl_->add_body(floor);
    if (with_ridges) {
        JPH::RefConst<JPH::Shape> ridge = new JPH::BoxShape(Vec3(4, 0.06f, 0.16f), 0.02f);
        for (float z : {-15.0f, -16.7f, -35.0f, -36.7f}) {
            JPH::BodyCreationSettings settings(ridge, Vec3(0, 0.06f, z), Quat::sIdentity(), JPH::EMotionType::Static, ground_layer);
            settings.mFriction = 0.8f;
            impl_->add_body(settings);
        }
    }
    impl_->system.OptimizeBroadPhase();
}

PhysicsWorld::~PhysicsWorld() = default;

PhysicsWorld::PhysicsWorld(const Environment& environment) {
    initialize_jolt();
    impl_ = std::make_unique<Impl>();
    JPH::VertexList vertices;
    JPH::IndexedTriangleList triangles;
    vertices.reserve(environment.vertices().size());
    triangles.reserve(environment.triangles().size());
    for (const auto& v : environment.vertices()) vertices.emplace_back(v.GetX(), v.GetY(), v.GetZ());
    for (const auto& t : environment.triangles()) triangles.emplace_back(t.a, t.b, t.c, 0);
    auto result = JPH::MeshShapeSettings(std::move(vertices), std::move(triangles)).Create();
    if (result.HasError()) throw std::runtime_error(result.GetError().c_str());
    JPH::BodyCreationSettings ground(result.Get(), Vec3::sZero(), Quat::sIdentity(), JPH::EMotionType::Static, ground_layer);
    ground.mFriction = 0.8f;
    impl_->add_body(ground);
    for (const auto& building : environment.buildings()) {
        JPH::RefConst<JPH::Shape> shape = new JPH::BoxShape(building.solid_size() / 2, 0.04f);
        JPH::BodyCreationSettings settings(shape, building.solid_center(), Quat::sIdentity(), JPH::EMotionType::Static, obstacle_layer);
        impl_->add_body(settings);
        if (building.kind == BuildingKind::GasStation) {
            const float base = building.center.GetY() - building.size.GetY() / 2;
            const float x = building.center.GetX() + building.size.GetX() * .3f;
            JPH::RefConst<JPH::Shape> canopy = new JPH::BoxShape(Vec3(building.size.GetX() * .175f, .3f, (building.size.GetZ() - 4) / 2), .02f);
            JPH::BodyCreationSettings roof(canopy, Vec3(x, base + 4.8f, building.center.GetZ()), Quat::sIdentity(), JPH::EMotionType::Static, obstacle_layer);
            impl_->add_body(roof);
            for (float z : {-building.size.GetZ() * .28f, 0.0f, building.size.GetZ() * .28f}) {
                JPH::RefConst<JPH::Shape> pump = new JPH::BoxShape(Vec3(.9f, 1, .6f), .02f);
                JPH::BodyCreationSettings settings(pump, Vec3(x, base + 1, building.center.GetZ() + z), Quat::sIdentity(), JPH::EMotionType::Static, obstacle_layer);
                impl_->add_body(settings);
            }
        }
    }
    for (const auto& barrier : environment.barriers()) {
        JPH::RefConst<JPH::Shape> shape = new JPH::BoxShape(barrier.size / 2, .02f);
        JPH::BodyCreationSettings settings(shape, barrier.center, barrier.rotation(),
            JPH::EMotionType::Static, obstacle_layer);
        impl_->add_body(settings);
    }
    for (const auto& port : environment.ports()) {
        const Vec3 direction = port.east ? Vec3::sAxisX() : Vec3::sAxisZ();
        // Overlap the access road so the deck's vertical edge is outside the junction.
        const Vec3 half = port.east ? Vec3(111, .6f, 4) : Vec3(4, .6f, 111);
        JPH::RefConst<JPH::Shape> shape = new JPH::BoxShape(half, .02f);
        JPH::BodyCreationSettings settings(shape, port.center + direction * 109 - Vec3(0, .6f, 0),
            Quat::sIdentity(), JPH::EMotionType::Static, ground_layer);
        impl_->add_body(settings);
    }
    for (const auto& tree : environment.trees()) {
        const float height = Tree::model_trunk_height * tree.scale();
        const float radius = Tree::model_trunk_radius * tree.scale();
        JPH::RefConst<JPH::Shape> trunk = new JPH::CylinderShape(height / 2, radius, .02f);
        JPH::BodyCreationSettings settings(trunk, tree.base + Vec3(0, height / 2, 0),
            Quat::sIdentity(), JPH::EMotionType::Static, obstacle_layer);
        impl_->add_body(settings);
    }
    impl_->system.OptimizeBroadPhase();
}

JPH::BodyID PhysicsWorld::create_chassis() {
    JPH::RefConst<JPH::Shape> box = new JPH::BoxShape(Vec3(0.93f, 0.22f, 1.85f));
    JPH::RefConst<JPH::Shape> chassis = new JPH::OffsetCenterOfMassShape(box, Vec3(0, -chassis_offset, 0));
    // Jolt creates bodies at the shape origin; its center of mass is 0.5 m
    // lower. All suspension mounts and drawing use the center of mass.
    JPH::BodyCreationSettings settings(chassis, Vec3(0, 0.56f + chassis_offset, 0),
        Quat::sIdentity(), JPH::EMotionType::Dynamic, vehicle_layer);
    settings.mOverrideMassProperties = JPH::EOverrideMassProperties::MassAndInertiaProvided;
    settings.mMassPropertiesOverride.mMass = mass;
    settings.mMassPropertiesOverride.mInertia = JPH::Mat44::sScale(Vec3(1700, 2200, 900));
    settings.mFriction = 0.8f;
    // Match the previous decay rate using Jolt's damping per second.
    settings.mLinearDamping = -std::log(0.975f);
    settings.mAngularDamping = -std::log(0.3f);
    settings.mAllowSleeping = false;
    settings.mMotionQuality = JPH::EMotionQuality::LinearCast;
    settings.mEnhancedInternalEdgeRemoval = true;
    return impl_->add_body(settings);
}

bool PhysicsWorld::cast_ground(const Vec3& origin, const Vec3& direction,
                               float distance, GroundHit& hit) const {
    JPH::RRayCast ray(origin, direction * distance);
    JPH::RayCastResult result;
    const auto cast = [&]() {
        return impl_->system.GetNarrowPhaseQuery().CastRay(ray, result,
            JPH::SpecifiedBroadPhaseLayerFilter(JPH::BroadPhaseLayer(0)),
            JPH::SpecifiedObjectLayerFilter(ground_layer));
    };
    if (!cast()) {
        // Compressed mesh edges can miss an exactly aligned ray; sample five millimeters beside it.
        ray = JPH::RRayCast(origin + Vec3(.005f, 0, .005f), direction * distance);
        if (!cast()) return false;
    }
    JPH::BodyLockRead lock(impl_->system.GetBodyLockInterface(), result.mBodyID);
    if (!lock.Succeeded()) return false;
    hit.point = ray.GetPointOnRay(result.mFraction);
    hit.normal = lock.GetBody().GetWorldSpaceSurfaceNormal(result.mSubShapeID2, hit.point);
    hit.distance = distance * result.mFraction;
    return true;
}

void PhysicsWorld::step(float dt) {
    if (impl_->system.Update(dt, 1, &impl_->allocator, &impl_->jobs) != JPH::EPhysicsUpdateError::None)
        throw std::runtime_error("Jolt physics capacity exceeded");
}

float PhysicsWorld::camera_fraction(const Vec3& origin, const Vec3& offset, JPH::BodyID ignore) const {
    if (offset.LengthSq() < 0.0001f) return 1;
    JPH::SphereShape sphere(0.28f);
    const JPH::RShapeCast cast(&sphere, Vec3::sOne(), JPH::RMat44::sTranslation(origin), offset);
    JPH::ShapeCastSettings settings;
    settings.mBackFaceModeTriangles = JPH::EBackFaceMode::CollideWithBackFaces;
    JPH::ClosestHitCollisionCollector<JPH::CastShapeCollector> hits;
    impl_->system.GetNarrowPhaseQuery().CastShape(cast, settings, origin, hits, {}, CameraLayers{}, JPH::IgnoreSingleBodyFilter(ignore));
    return hits.HadHit() ? std::clamp(hits.mHit.mFraction - 0.03f / offset.Length(), 0.0f, 1.0f) : 1;
}

Plane::Plane(PhysicsWorld& world) : world_(world) {
    JPH::StaticCompoundShapeSettings parts;
    const auto box = [&](Vec3 center, Vec3 half) {
        JPH::RefConst<JPH::Shape> shape = new JPH::BoxShape(half, .03f);
        parts.AddShape(center, Quat::sIdentity(), shape);
    };
    box(Vec3(0, 0, -.2f), Vec3(.48f, .48f, 2.8f));
    box(Vec3(0, .6f, -.3f), Vec3(5.5f, .09f, .75f));
    box(Vec3(0, .35f, 2.5f), Vec3(1.8f, .08f, .55f));
    box(Vec3(0, .95f, 2.5f), Vec3(.08f, .65f, .55f));
    const auto result = parts.Create();
    if (result.HasError()) throw std::runtime_error(result.GetError().c_str());
    JPH::RefConst<JPH::Shape> shape = new JPH::OffsetCenterOfMassShape(result.Get(), -result.Get()->GetCenterOfMass());
    JPH::BodyCreationSettings settings(shape, Vec3(0, parked_height, 0), Quat::sIdentity(),
        JPH::EMotionType::Dynamic, vehicle_layer);
    settings.mOverrideMassProperties = JPH::EOverrideMassProperties::MassAndInertiaProvided;
    settings.mMassPropertiesOverride.mMass = 850;
    settings.mMassPropertiesOverride.mInertia = JPH::Mat44::sScale(Vec3(2100, 5500, 3300));
    settings.mLinearDamping = .005f;
    settings.mAngularDamping = .05f;
    settings.mMaxLinearVelocity = 160;
    settings.mMaxAngularVelocity = 5;
    settings.mFriction = .6f;
    settings.mRestitution = .05f;
    settings.mAllowSleeping = false;
    settings.mMotionQuality = JPH::EMotionQuality::LinearCast;
    settings.mEnhancedInternalEdgeRemoval = true;
    body_ = world_.impl_->add_body(settings);
    wheels_[0].mount = Vec3(-1.25f, -.48f, .32f);
    wheels_[1].mount = Vec3(1.25f, -.48f, .32f);
    wheels_[2].mount = Vec3(0, -.48f, -2);
    wheels_[2].front = true;
    reset(Vec3(0, parked_height, 0), 0);
}
Vec3 Plane::position() const { return world_.impl_->system.GetBodyInterface().GetCenterOfMassPosition(body_); }
Vec3 Plane::velocity() const { return world_.impl_->system.GetBodyInterface().GetLinearVelocity(body_); }
Quat Plane::rotation() const { return world_.impl_->system.GetBodyInterface().GetRotation(body_); }
bool Plane::grounded() const {
    return std::any_of(wheels_.begin(), wheels_.end(), [](const Wheel& w) { return w.grounded; });
}
void Plane::reset(const Vec3& center, float yaw, const Vec3& speed, float throttle) {
    auto& physics = world_.impl_->system.GetBodyInterface();
    physics.SetPositionAndRotation(body_, center, Quat::sRotation(Vec3::sAxisY(), yaw), JPH::EActivation::Activate);
    physics.SetLinearAndAngularVelocity(body_, speed, Vec3::sZero());
    throttle_ = clamp(throttle, 0, 1);
    airspeed_ = speed.Length(); alpha_ = 0; propeller_angle_ = 0;
    previous_velocity_ = speed; stalled_ = damaged_ = false;
    for (auto& wheel : wheels_) wheel.spin = 0;
    refresh_gear();
}
void Plane::refresh_gear() {
    const Vec3 down = -rotate(Vec3::sAxisY());
    for (auto& wheel : wheels_) {
        const Vec3 mount = position() + rotate(wheel.mount);
        GroundHit hit;
        wheel.grounded = world_.cast_ground(mount, down, .66f + tire_radius, hit)
            && hit.normal.Dot(-down) > .45f;
        const float length = wheel.grounded ? clamp(hit.distance - tire_radius, .34f, .66f) : .66f;
        wheel.center = mount + down * length;
        wheel.compression = .5f - length;
        wheel.ground_point = wheel.grounded ? hit.point : wheel.center + down * tire_radius;
        wheel.ground_normal = wheel.grounded ? hit.normal : -down;
        wheel.normal_force = 0;
    }
}
void Plane::step(FlightInput input, float dt) {
    auto& physics = world_.impl_->system.GetBodyInterface();
    const Vec3 v = velocity(), center = position();
    // Large collision decelerations disable the engine. Ordinary gear
    // touchdowns and aerodynamic acceleration stay well below this threshold.
    if ((v - previous_velocity_).Length() > 10) damaged_ = true;
    previous_velocity_ = v;
    throttle_ = input.parking_brake || damaged_ ? 0 : clamp(throttle_ + input.throttle * .4f * dt, 0, 1);
    propeller_angle_ = std::remainder(propeller_angle_ + throttle_ * 180 * dt, 6.2831853f);
    const auto basis = rotation();
    const Vec3 right = basis * Vec3::sAxisX(), up = basis * Vec3::sAxisY(), fwd = forward();
    const Vec3 local = basis.Conjugated() * v;
    const Vec3 angular = basis.Conjugated() * physics.GetAngularVelocity(body_);
    airspeed_ = v.Length();
    const float forward_speed = -local.GetZ();
    alpha_ = std::atan2(-local.GetY(), std::max(std::abs(forward_speed), .1f));
    const float wing_alpha = alpha_ + .04f;
    stalled_ = forward_speed > 8 && std::abs(wing_alpha) > .28f;
    const float density = 1.225f * std::exp(-std::max(center.GetY(), 0.0f) / 8500);
    const float q = .5f * density * airspeed_ * airspeed_;
    const float area = 16;
    // L = q*S*Cl; Cd includes parasite and induced drag. Beyond the stall
    // angle, lift rolls off and drag rises, including backwards/inverted flight.
    float cl = .25f + 5.2f * clamp(wing_alpha, -.28f, .28f) + (input.flaps ? .35f : 0);
    if (std::abs(wing_alpha) > .28f)
        cl *= std::max(.08f, 1 - (std::abs(wing_alpha) - .28f) / .8f);
    if (forward_speed <= 0) cl = 0;
    const float cd = .035f + cl * cl / (3.14159265f * .8f * 7.56f)
        + (input.flaps ? .055f : 0) + .85f * std::sin(alpha_) * std::sin(alpha_);
    if (airspeed_ > .5f) {
        const Vec3 flow = v / airspeed_;
        const Vec3 lift_direction = right.Cross(flow).NormalizedOr(up);
        const float lift = clamp(q * area * cl, -45000, 45000);
        physics.AddForce(body_, lift_direction * lift - flow * (q * area * cd)
            - right * (local.GetX() * q * .08f));
    }
    // Fixed-pitch propeller thrust falls with forward speed.
    physics.AddForce(body_, fwd * (throttle_ * 3200 * clamp(1 - forward_speed / 115, .15f, 1)));
    const float authority = clamp(std::max(forward_speed, 0.0f) / 28, 0, 2);
    const float sideslip = std::atan2(local.GetX(), std::max(std::abs(forward_speed), 1.0f));
    const float pitch_moment = clamp(q * area * 1.45f * 1.25f * (.025f + input.pitch * .20f - alpha_), -16000, 16000);
    const Vec3 torque(pitch_moment - angular.GetX() * (800 + 2400 * authority),
        input.yaw * 2400 * authority - sideslip * q * 8 - angular.GetY() * (600 + 2000 * authority),
        input.roll * 6000 * authority - angular.GetZ() * (700 + 3500 * authority));
    physics.AddTorque(body_, basis * torque);
    refresh_gear();
    for (auto& wheel : wheels_) {
        if (!wheel.grounded) continue;
        const Vec3 point_velocity = v + (basis * angular).Cross(wheel.ground_point - center);
        wheel.normal_force = clamp(wheel.compression * 42000 - point_velocity.Dot(up) * 5000, 0, 16000);
        physics.AddImpulse(body_, up * (wheel.normal_force * dt), wheel.ground_point);
        const auto steer = Quat::sRotation(up, wheel.front ? input.yaw * .45f : 0);
        Vec3 wheel_fwd = steer * fwd;
        wheel_fwd = (wheel_fwd - wheel.ground_normal * wheel_fwd.Dot(wheel.ground_normal)).NormalizedOr(fwd);
        const Vec3 side = wheel_fwd.Cross(wheel.ground_normal);
        const float speed = point_velocity.Dot(wheel_fwd);
        const float grip = wheel.normal_force * dt;
        const float sideways = clamp(-point_velocity.Dot(side) * (850.0f / 3), -grip, grip);
        const float rolling = input.brake || input.parking_brake ? 1.0f : .015f;
        const float remaining = std::sqrt(std::max(0.0f, grip * grip - sideways * sideways));
        const float longitudinal = clamp(-speed * (850.0f / 3), -remaining * rolling, remaining * rolling);
        physics.AddImpulse(body_, side * sideways + wheel_fwd * longitudinal, wheel.ground_point);
        wheel.spin = std::remainder(wheel.spin + speed * dt / tire_radius, 6.2831853f);
    }
}

namespace {
constexpr std::size_t body_part_count = static_cast<std::size_t>(BodyPart::Count);
struct HumanPart {
    const char* name;
    int parent;
    Vec3 center, pivot, size;
    float mass;
};
const std::array<HumanPart, body_part_count>& human_parts() {
    static const std::array<HumanPart, body_part_count> parts{{
        {"pelvis", -1, {0, .97f, 0}, {0, .97f, 0}, {.36f, .24f, .24f}, 12},
        {"torso", 0, {0, 1.29f, 0}, {0, 1.08f, 0}, {.44f, .44f, .26f}, 25},
        {"head", 1, {0, 1.68f, 0}, {0, 1.51f, 0}, {.26f, .30f, .26f}, 5},
        {"left upper arm", 1, {-.30f, 1.28f, 0}, {-.28f, 1.45f, 0}, {.15f, .34f, .15f}, 3},
        {"left forearm", 3, {-.32f, .95f, 0}, {-.32f, 1.11f, 0}, {.13f, .32f, .13f}, 2},
        {"left hand", 4, {-.32f, .70f, 0}, {-.32f, .79f, 0}, {.12f, .18f, .10f}, .8f},
        {"right upper arm", 1, {.30f, 1.28f, 0}, {.28f, 1.45f, 0}, {.15f, .34f, .15f}, 3},
        {"right forearm", 6, {.32f, .95f, 0}, {.32f, 1.11f, 0}, {.13f, .32f, .13f}, 2},
        {"right hand", 7, {.32f, .70f, 0}, {.32f, .79f, 0}, {.12f, .18f, .10f}, .8f},
        {"left thigh", 0, {-.11f, .725f, 0}, {-.11f, .94f, 0}, {.18f, .43f, .18f}, 8},
        {"left shin", 9, {-.11f, .31f, 0}, {-.11f, .51f, 0}, {.14f, .40f, .14f}, 4},
        {"left foot", 10, {-.11f, .075f, -.08f}, {-.11f, .11f, 0}, {.16f, .13f, .31f}, 1.2f},
        {"right thigh", 0, {.11f, .725f, 0}, {.11f, .94f, 0}, {.18f, .43f, .18f}, 8},
        {"right shin", 12, {.11f, .31f, 0}, {.11f, .51f, 0}, {.14f, .40f, .14f}, 4},
        {"right foot", 13, {.11f, .075f, -.08f}, {.11f, .11f, 0}, {.16f, .13f, .31f}, 1.2f}
    }};
    return parts;
}
bool hinge_part(std::size_t part) { return part == 4 || part == 7 || part == 10 || part == 13; }
}

struct Character::Impl {
    JPH::Ref<JPH::CharacterVirtual> character;
    // Jolt's ragdoll owns its bodies and joints; these never enter the world's static-body list.
    JPH::Ref<JPH::Ragdoll> rig;
    Vec3 desired_velocity{0, 0, 0}, impact_direction{0, 0, -1};
    float ragdoll_time = 0, settled_time = 0, hit_cooldown = 0;
    bool enabled = true;
    ~Impl() { clear_ragdoll(); }
    void clear_ragdoll() {
        if (rig) { rig->RemoveFromPhysicsSystem(); rig = nullptr; }
        ragdoll_time = settled_time = 0;
    }
};

Character::Character(PhysicsWorld& world) : world_(world), impl_(std::make_unique<Impl>()) {
    JPH::CharacterVirtualSettings settings;
    JPH::RefConst<JPH::Shape> capsule = new JPH::CapsuleShape(0.58f, 0.32f);
    settings.mShape = new JPH::RotatedTranslatedShape(Vec3(0, 0.9f, 0), Quat::sIdentity(), capsule);
    settings.mSupportingVolume = JPH::Plane(Vec3::sAxisY(), -0.32f);
    settings.mMaxSlopeAngle = 0.8726646f;
    settings.mEnhancedInternalEdgeRemoval = true;
    settings.mMaxStrength = 100;
    impl_->character = new JPH::CharacterVirtual(&settings, Vec3(0, 2, 0), Quat::sIdentity(), &world_.impl_->system);
}
Character::~Character() = default;
bool Character::ragdolling() const { return impl_->rig != nullptr; }
Vec3 Character::position() const {
    if (!impl_->rig) return impl_->character->GetPosition();
    const Vec3 pelvis = world_.impl_->system.GetBodyInterface().GetPosition(impl_->rig->GetBodyID(0));
    return Vec3(pelvis.GetX(), impl_->rig->GetWorldSpaceBounds().mMin.GetY(), pelvis.GetZ());
}
Vec3 Character::velocity() const {
    return impl_->rig ? world_.impl_->system.GetBodyInterface().GetLinearVelocity(impl_->rig->GetBodyID(0))
                      : impl_->character->GetLinearVelocity();
}
bool Character::grounded() const {
    if (!impl_->rig) return impl_->character->GetGroundState() == JPH::CharacterBase::EGroundState::OnGround;
    GroundHit hit;
    return world_.cast_ground(position() + Vec3(0, .15f, 0), -Vec3::sAxisY(), .3f, hit)
        && hit.normal.GetY() > .55f;
}

void Character::set_enabled(bool enabled) {
    if (!enabled && impl_->rig) {
        const Vec3 feet = position();
        impl_->clear_ragdoll();
        impl_->character->SetPosition(feet);
    }
    impl_->enabled = enabled;
    if (!enabled) {
        impl_->desired_velocity = Vec3::sZero();
        impl_->character->SetLinearVelocity(Vec3::sZero());
    }
}

std::array<BodyPartPose, body_part_count> Character::body_parts() const {
    const auto& parts = human_parts();
    std::array<BodyPartPose, body_part_count> pose;
    if (impl_->rig) {
        auto& physics = world_.impl_->system.GetBodyInterface();
        for (std::size_t i = 0; i < pose.size(); ++i)
            pose[i] = {physics.GetPosition(impl_->rig->GetBodyID(static_cast<int>(i))),
                       physics.GetRotation(impl_->rig->GetBodyID(static_cast<int>(i))), parts[i].size};
        return pose;
    }
    const Vec3 speed = velocity();
    const float moving = std::clamp(std::hypot(speed.GetX(), speed.GetZ()) / 6.5f, 0.0f, 1.0f);
    const float swing = std::sin(gait_) * .65f * moving;
    std::array<float, body_part_count> pitch{};
    pitch[3] = -swing * .65f; pitch[6] = swing * .65f;
    pitch[4] = pitch[7] = .12f + .25f * moving;
    pitch[9] = swing; pitch[12] = -swing;
    pitch[10] = -.05f - std::max(0.0f, -swing) * 1.1f;
    pitch[13] = -.05f - std::max(0.0f, swing) * 1.1f;
    pitch[11] = -pitch[9] - pitch[10]; pitch[14] = -pitch[12] - pitch[13];
    const Vec3 feet = impl_->character->GetPosition();
    const Quat root = Quat::sRotation(Vec3::sAxisY(), yaw_);
    for (std::size_t i = 0; i < pose.size(); ++i) {
        const auto& part = parts[i];
        const Quat rotation = part.parent < 0 ? root
            : pose[part.parent].rotation * Quat::sRotation(Vec3::sAxisX(), pitch[i]);
        const Vec3 pivot = part.parent < 0 ? feet + root * part.pivot
            : pose[part.parent].position + pose[part.parent].rotation * (part.pivot - parts[part.parent].center);
        pose[i] = {pivot + rotation * (part.center - part.pivot), rotation, part.size};
    }
    return pose;
}

void Character::ragdoll(const Vec3& inherited_velocity, const Vec3& impulse) {
    if (!impl_->enabled) return;
    auto& physics = world_.impl_->system.GetBodyInterface();
    if (impl_->rig) {
        physics.AddImpulse(impl_->rig->GetBodyID(1), impulse);
        impl_->settled_time = 0;
        return;
    }
    const auto pose = body_parts();
    const auto& parts = human_parts();
    JPH::Ref<JPH::RagdollSettings> settings = new JPH::RagdollSettings;
    settings->mSkeleton = new JPH::Skeleton;
    settings->mParts.resize(parts.size());
    std::array<JPH::Mat44, body_part_count> matrices;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        const auto& part = parts[i];
        settings->mSkeleton->AddJoint(part.name, part.parent);
        JPH::RefConst<JPH::Shape> shape;
        if (i == 2) shape = new JPH::SphereShape(.145f);
        else if (i == 0 || i == 1 || i == 5 || i == 8 || i == 11 || i == 14)
            shape = new JPH::BoxShape(part.size / 2, .015f);
        else shape = new JPH::CapsuleShape((part.size.GetY() - part.size.GetX()) / 2, part.size.GetX() / 2);
        auto& body = settings->mParts[i];
        body.SetShape(shape);
        body.mPosition = pose[i].position;
        body.mRotation = pose[i].rotation;
        body.mMotionType = JPH::EMotionType::Dynamic;
        body.mObjectLayer = character_layer;
        body.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
        body.mMassPropertiesOverride.mMass = part.mass;
        body.mLinearDamping = .15f;
        body.mAngularDamping = .45f;
        body.mFriction = .65f;
        body.mRestitution = .02f;
        body.mMotionQuality = JPH::EMotionQuality::LinearCast;
        body.mEnhancedInternalEdgeRemoval = true;
        body.mMaxAngularVelocity = 18;
        matrices[i] = JPH::Mat44::sRotationTranslation(pose[i].rotation, pose[i].position);
        if (part.parent < 0) continue;
        if (hinge_part(i)) {
            JPH::Ref<JPH::HingeConstraintSettings> joint = new JPH::HingeConstraintSettings;
            joint->mSpace = JPH::EConstraintSpace::LocalToBodyCOM;
            joint->mPoint1 = part.pivot - parts[part.parent].center;
            joint->mPoint2 = part.pivot - part.center;
            joint->mHingeAxis1 = joint->mHingeAxis2 = Vec3::sAxisX();
            joint->mNormalAxis1 = joint->mNormalAxis2 = Vec3::sAxisY();
            const bool elbow = i == 4 || i == 7;
            joint->mLimitsMin = elbow ? -.08f : -2.4f;
            joint->mLimitsMax = elbow ? 2.4f : .08f;
            joint->mMaxFrictionTorque = .5f;
            joint->mMotorSettings = JPH::MotorSettings(3, 1, 0, elbow ? 7 : 12);
            joint->mNumVelocityStepsOverride = 16;
            joint->mNumPositionStepsOverride = 6;
            body.mToParent = joint;
        } else {
            JPH::Ref<JPH::SwingTwistConstraintSettings> joint = new JPH::SwingTwistConstraintSettings;
            joint->mSpace = JPH::EConstraintSpace::LocalToBodyCOM;
            joint->mPosition1 = part.pivot - parts[part.parent].center;
            joint->mPosition2 = part.pivot - part.center;
            joint->mTwistAxis1 = joint->mTwistAxis2 = Vec3::sAxisY();
            joint->mPlaneAxis1 = joint->mPlaneAxis2 = Vec3::sAxisX();
            const bool shoulder = i == 3 || i == 6, hip = i == 9 || i == 12;
            const float cone = shoulder ? 1.45f : hip ? 1.15f : i == 1 ? .48f : .55f;
            joint->mNormalHalfConeAngle = joint->mPlaneHalfConeAngle = cone;
            joint->mTwistMinAngle = -cone * .55f;
            joint->mTwistMaxAngle = cone * .55f;
            joint->mMaxFrictionTorque = .6f;
            joint->mSwingMotorSettings = joint->mTwistMotorSettings = JPH::MotorSettings(3, 1, 0, shoulder ? 9 : hip ? 14 : 5);
            joint->mNumVelocityStepsOverride = 16;
            joint->mNumPositionStepsOverride = 6;
            body.mToParent = joint;
        }
    }
    if (!settings->Stabilize()) throw std::runtime_error("Could not stabilize humanoid ragdoll");
    settings->DisableParentChildCollisions(matrices.data());
    settings->CalculateBodyIndexToConstraintIndex();
    settings->CalculateConstraintIndexToBodyIdxPair();
    impl_->rig = settings->CreateRagdoll(world_.impl_->next_character_group++, 0, &world_.impl_->system);
    if (!impl_->rig) throw std::runtime_error("Jolt could not allocate humanoid ragdoll");
    impl_->rig->AddToPhysicsSystem(JPH::EActivation::Activate);
    impl_->rig->SetLinearAndAngularVelocity(inherited_velocity, Vec3::sZero());
    physics.AddImpulse(impl_->rig->GetBodyID(1), impulse);
    Vec3 direction = inherited_velocity + impulse * .02f;
    direction.SetY(0);
    impl_->impact_direction = direction.NormalizedOr(forward());
    impl_->ragdoll_time = impl_->settled_time = 0;
}

void Character::hit_by(const Car& car, float dt) {
    if (!impl_->enabled || impl_->rig || impl_->hit_cooldown > 0 || !car.simulated() || car.velocity().LengthSq() < 2.25f) return;
    const Quat inverse = car.rotation().Conjugated();
    const Vec3 origin = inverse * (position() + Vec3(0, .9f, 0) - car.position()) - Vec3(0, chassis_offset, 0);
    // The virtual controller may already be pushed along by a predictive car contact.
    // Measure the impact against intended walking velocity, before that correction.
    Vec3 walking_velocity = impl_->desired_velocity;
    walking_velocity.SetY(velocity().GetY());
    const Vec3 relative = car.velocity() - walking_velocity;
    const Vec3 sweep = inverse * (-relative * dt);
    const Vec3 half(1.35f, 1.15f, 2.28f); // Includes CharacterVirtual's 0.1 m predictive contact margin.
    float first = 0, last = 1;
    for (int axis = 0; axis < 3; ++axis) {
        if (std::abs(sweep[axis]) < .00001f) {
            if (std::abs(origin[axis]) > half[axis]) return;
        } else {
            const float a = (-half[axis] - origin[axis]) / sweep[axis];
            const float b = (half[axis] - origin[axis]) / sweep[axis];
            first = std::max(first, std::min(a, b));
            last = std::min(last, std::max(a, b));
            if (first > last) return;
        }
    }
    const float speed = relative.Length();
    if (speed < 1.5f) return;
    const Vec3 direction = relative.Normalized();
    const Vec3 inherited = walking_velocity + relative * .65f + Vec3(0, std::clamp(speed * .09f, .5f, 3.5f), 0);
    ragdoll(inherited, direction * std::min(speed * 2.0f, 65.0f));
}

void Character::reset(const Vec3& feet, float yaw) {
    impl_->clear_ragdoll();
    impl_->enabled = true;
    yaw_ = yaw; gait_ = 0;
    impl_->desired_velocity = Vec3::sZero();
    impl_->character->SetPosition(feet);
    impl_->character->SetRotation(Quat::sRotation(Vec3::sAxisY(), yaw));
    impl_->character->SetLinearVelocity(Vec3::sZero());
    impl_->hit_cooldown = 0;
    impl_->character->RefreshContacts({}, {}, {}, {}, world_.impl_->allocator);
}

bool Character::can_stand_at(const Vec3& feet) const {
    const auto* shape = impl_->character->GetShape();
    JPH::AllHitCollisionCollector<JPH::CollideShapeCollector> hits;
    JPH::IgnoreMultipleBodiesFilter own_parts;
    if (impl_->rig) for (const auto& body : impl_->rig->GetBodyIDs()) own_parts.IgnoreBody(body);
    world_.impl_->system.GetNarrowPhaseQuery().CollideShape(shape, Vec3::sOne(),
        JPH::RMat44::sTranslation(feet + shape->GetCenterOfMass()), {}, feet, hits, {}, {}, own_parts);
    for (const auto& hit : hits.mHits) if (hit.mPenetrationDepth > 0.005f) return false;
    return true;
}

void Character::step(FootInput input, float dt) {
    if (!impl_->enabled) return;
    impl_->hit_cooldown = std::max(0.0f, impl_->hit_cooldown - dt);
    if (impl_->rig) {
        auto& physics = world_.impl_->system.GetBodyInterface();
        impl_->ragdoll_time += dt;
        const bool brace = impl_->ragdoll_time < .9f;
        bool settled = true;
        for (std::size_t i = 1; i < body_part_count; ++i) {
            auto* constraint = impl_->rig->GetConstraint(static_cast<int>(i - 1));
            if (hinge_part(i)) {
                auto& joint = static_cast<JPH::HingeConstraint&>(*constraint);
                joint.SetMotorState(brace ? JPH::EMotorState::Position : JPH::EMotorState::Off);
                joint.SetTargetAngle(i == 4 || i == 7 ? 1.0f : -.65f);
            } else {
                auto& joint = static_cast<JPH::SwingTwistConstraint&>(*constraint);
                joint.SetSwingMotorState(brace ? JPH::EMotorState::Position : JPH::EMotorState::Off);
                joint.SetTwistMotorState(brace ? JPH::EMotorState::Position : JPH::EMotorState::Off);
                const float pitch = i == 3 || i == 6 ? 1.1f : i == 9 || i == 12 ? .45f : i == 1 ? -.25f : 0;
                const float spread = i == 3 ? -.3f : i == 6 ? .3f : 0;
                joint.SetTargetOrientationBS(Quat::sRotation(Vec3::sAxisX(), pitch) * Quat::sRotation(Vec3::sAxisZ(), spread));
            }
        }
        if (impl_->ragdoll_time < .3f)
            physics.AddTorque(impl_->rig->GetBodyID(1), impl_->impact_direction.Cross(Vec3::sAxisY()) * 7);
        for (const auto& body : impl_->rig->GetBodyIDs())
            settled &= physics.GetLinearVelocity(body).LengthSq() < .5f && physics.GetAngularVelocity(body).LengthSq() < 1.0f;
        impl_->settled_time = settled && grounded() ? impl_->settled_time + dt : 0;
        if (impl_->ragdoll_time > 2.8f && impl_->settled_time > .7f) {
            const Vec3 pelvis = physics.GetPosition(impl_->rig->GetBodyID(0));
            const Vec3 facing = physics.GetRotation(impl_->rig->GetBodyID(1)) * Vec3(0, 0, -1);
            const float yaw = std::hypot(facing.GetX(), facing.GetZ()) > .1f ? std::atan2(-facing.GetX(), -facing.GetZ()) : yaw_;
            for (const Vec3& offset : {Vec3::sZero(), Vec3(.65f, 0, 0), Vec3(-.65f, 0, 0), Vec3(0, 0, .65f), Vec3(0, 0, -.65f)}) {
                GroundHit hit;
                if (!world_.cast_ground(pelvis + offset + Vec3(0, 1, 0), -Vec3::sAxisY(), 2.4f, hit)
                    || hit.normal.GetY() < .65f || hit.point.GetY() < -.05f) continue;
                const Vec3 feet = hit.point + Vec3(0, .06f, 0);
                if (!can_stand_at(feet)) continue;
                reset(feet, yaw);
                impl_->hit_cooldown = .75f;
                break;
            }
        }
        return;
    }
    auto& character = *impl_->character;
    input.direction.SetY(0);
    if (input.direction.LengthSq() > 1) input.direction = input.direction.Normalized();
    const Vec3 desired = input.direction * (input.sprint ? 6.5f : 3.2f);
    const float acceleration = character.IsSupported() ? 18.0f : 4.0f;
    impl_->desired_velocity += (desired - impl_->desired_velocity) * (1 - std::exp(-acceleration * dt));
    character.UpdateGroundVelocity();
    float vertical_speed = velocity().GetY();
    if (grounded() && vertical_speed - character.GetGroundVelocity().GetY() < 0.15f) {
        vertical_speed = character.GetGroundVelocity().GetY();
        if (input.jump) vertical_speed += 5.8f;
    }
    vertical_speed -= 18 * dt;
    if (vertical_speed < -10) {
        ragdoll(impl_->desired_velocity + Vec3(0, vertical_speed, 0));
        return;
    }
    character.SetLinearVelocity(impl_->desired_velocity + Vec3(0, vertical_speed, 0));
    JPH::CharacterVirtual::ExtendedUpdateSettings settings;
    settings.mWalkStairsStepUp = Vec3(0, 0.35f, 0);
    settings.mStickToFloorStepDown = vertical_speed > 0.1f ? Vec3::sZero() : Vec3(0, -0.35f, 0);
    character.ExtendedUpdate(dt, Vec3(0, -18, 0), settings, {}, {}, {}, {}, world_.impl_->allocator);
    if (vertical_speed < -8 && grounded()) {
        ragdoll(impl_->desired_velocity + Vec3(0, vertical_speed, 0));
        return;
    }
    if (input.direction.LengthSq() > 0.01f) {
        const float target = std::atan2(-input.direction.GetX(), -input.direction.GetZ());
        yaw_ += std::clamp(std::remainder(target - yaw_, 6.28318530718f), -12 * dt, 12 * dt);
        character.SetRotation(Quat::sRotation(Vec3::sAxisY(), yaw_));
    }
    const auto speed = velocity();
    if (grounded()) gait_ += std::hypot(speed.GetX(), speed.GetZ()) * dt * 2.3f;
}

Vec3 Car::position() const { return world_.impl_->system.GetBodyInterface().GetCenterOfMassPosition(body_); }
Vec3 Car::velocity() const { return world_.impl_->system.GetBodyInterface().GetLinearVelocity(body_); }
Quat Car::rotation() const { return world_.impl_->system.GetBodyInterface().GetRotation(body_); }

Car::Car(PhysicsWorld& world) : world_(world), body_(world.create_chassis()) {
    update_wheel_mounts();
    for (auto& wheel : wheels_)
        wheel.center = position() + rotate(wheel.mount - Vec3(0, tuning_.rest_length, 0));
}

void Car::update_wheel_mounts() {
    const float half_track = tuning_.track_width / 2, half_wheelbase = tuning_.wheelbase / 2;
    for (std::size_t i = 0; i < wheels_.size(); ++i) {
        auto& wheel = wheels_[i];
        wheel.front = i < 2;
        wheel.mount = Vec3(i % 2 == 0 ? -half_track : half_track, tuning_.mount_height,
                           wheel.front ? -half_wheelbase : half_wheelbase);
    }
}

void Car::reset(const Vec3& center_of_mass, float yaw) {
    auto& physics = world_.impl_->system.GetBodyInterface();
    const Quat rotation = Quat::sRotation(Vec3::sAxisY(), yaw);
    physics.SetPositionAndRotation(body_, center_of_mass + rotation * Vec3(0, chassis_offset, 0),
        rotation, simulated_ ? JPH::EActivation::Activate : JPH::EActivation::DontActivate);
    physics.SetLinearAndAngularVelocity(body_, Vec3::sZero(), Vec3::sZero());
    steer_ = 0;
    drive_direction_ = 0;
    direction_change_time_ = 0;
    for (auto& wheel : wheels_) {
        wheel.grounded = wheel.skidding = false;
        wheel.spin = wheel.compression = wheel.normal_force = 0;
        wheel.center = center_of_mass + rotation * (wheel.mount - Vec3(0, tuning_.rest_length, 0));
    }
}
void Car::set_simulated(bool simulated) {
    if (simulated == simulated_) return;
    auto& physics = world_.impl_->system.GetBodyInterface();
    if (simulated) physics.AddBody(body_, JPH::EActivation::Activate);
    else physics.RemoveBody(body_);
    simulated_ = simulated;
}

void Car::set_tuning(CarTuning tuning) {
    const CarTuning defaults;
    for (const auto& control : tuning_controls) {
        auto& value = tuning.*(control.value);
        if (!std::isfinite(value)) value = defaults.*(control.value);
        value = clamp(value, control.min, control.max);
    }
    // Compressed suspension length must stay positive, even with edits to
    // rest length and travel in either order.
    tuning.travel = std::min(tuning.travel, tuning.rest_length - .05f);
    tuning_ = tuning;
    steer_ = clamp(steer_, -tuning_.max_steer, tuning_.max_steer);
    update_wheel_mounts();
    refresh_wheel_contacts();
}

void Car::refresh_wheel_contacts() {
    const Vec3 up = rotate(Vec3::sAxisY());
    const Vec3 down = -up;
    for (auto& wheel : wheels_) {
        const Vec3 mount = position() + rotate(wheel.mount);
        GroundHit hit;
        wheel.grounded = world_.cast_ground(mount, down,
            tuning_.rest_length + tuning_.travel + tuning_.wheel_radius, hit) && hit.normal.Dot(up) > .35f;
        wheel.skidding = false;
        wheel.compression = wheel.normal_force = 0;
        wheel.center = mount + down * tuning_.rest_length;
        if (!wheel.grounded) continue;
        const float length = clamp(hit.distance - tuning_.wheel_radius,
            tuning_.rest_length - tuning_.travel, tuning_.rest_length + tuning_.travel);
        wheel.center = mount + down * length;
        wheel.ground_point = hit.point;
        wheel.ground_normal = hit.normal;
        wheel.compression = tuning_.rest_length - length;
    }
}

void Car::step(Input input, float dt) {
    auto& physics = world_.impl_->system.GetBodyInterface();
    input.throttle = clamp(input.throttle, -1, 1);
    input.steer = clamp(input.steer, -1, 1);
    const auto velocity_now = velocity();
    bool direction_braking = false;
    if (input.player_controlled) {
        const float forward_speed = velocity_now.Dot(forward());
        const int requested = input.throttle > .01f ? 1 : input.throttle < -.01f ? -1 : 0;
        // Keep the previous direction while braking; suspension rocking near
        // zero speed must not engage the opposite direction early.
        if (std::abs(forward_speed) > .1f && (!requested || !drive_direction_ || requested == drive_direction_)) {
            drive_direction_ = forward_speed > 0 ? 1 : -1;
            direction_change_time_ = 0;
        }
        if (requested && drive_direction_ && requested != drive_direction_) {
            direction_braking = true;
            if (std::abs(forward_speed) <= .1f && !input.parking_brake && !input.handbrake) {
                direction_change_time_ += dt;
                if (direction_change_time_ >= .25f) {
                    drive_direction_ = requested;
                    direction_change_time_ = 0;
                    direction_braking = false;
                }
            } else direction_change_time_ = 0;
        } else {
            direction_change_time_ = 0;
            if (requested) drive_direction_ = requested;
        }
    } else {
        drive_direction_ = 0;
        direction_change_time_ = 0;
    }
    const float speed = std::hypot(velocity_now.GetX(), velocity_now.GetZ());
    float steer_limit = std::min(tuning_.max_steer, std::atan(tuning_.wheelbase * 16 / std::max(speed * speed, 1.0f)));
    if (input.handbrake && speed > 4) steer_limit = std::min(tuning_.max_steer, steer_limit * float(1.3));
    steer_ += clamp(input.steer * steer_limit - steer_, float(-2.8) * dt, float(2.8) * dt);

    const Vec3 up = rotate(Vec3(0, 1, 0));
    const Vec3 chassis_forward = forward();
    refresh_wheel_contacts();
    for (auto& wheel : wheels_) {
        if (!wheel.grounded) {
            wheel.spin += velocity().Dot(chassis_forward) / tuning_.wheel_radius * dt;
            continue;
        }
        const Vec3 point_velocity = physics.GetPointVelocity(body_, wheel.ground_point);
        // Ray length changes with motion into/out of the road, not travel along it.
        // Ground contact already requires normal.Dot(up) > .35, so division is safe.
        const float suspension_speed = point_velocity.Dot(wheel.ground_normal) / wheel.ground_normal.Dot(up);
        const float normal_force = clamp(tuning_.spring_rate * wheel.compression - tuning_.damper_rate * suspension_speed,
            0, tuning_.max_spring_force);
        wheel.normal_force = normal_force;
        physics.AddImpulse(body_, up * (normal_force * dt), wheel.ground_point);

        Vec3 wheel_forward = chassis_forward;
        if (wheel.front) wheel_forward = Quat::sRotation(up, steer_) * wheel_forward;
        wheel_forward -= wheel.ground_normal * wheel_forward.Dot(wheel.ground_normal);
        wheel_forward = wheel_forward.Normalized();
        const Vec3 wheel_right = wheel_forward.Cross(wheel.ground_normal).Normalized();
        const float long_speed = point_velocity.Dot(wheel_forward);
        const float lateral_speed = point_velocity.Dot(wheel_right);

        // Give steering priority in the friction circle. A rear handbrake
        // lowers the grip available to those tires so the rear can slide.
        const float grip_scale = input.handbrake && !wheel.front ? tuning_.rear_drift_grip : tuning_.tire_grip;
        const float grip = normal_force * grip_scale * dt;
        const float lateral_impulse = clamp(-lateral_speed * mass * float(0.25 * 0.95), -grip, grip);
        const float remaining = std::sqrt(std::max(float(0), grip * grip - lateral_impulse * lateral_impulse));
        const float desired = input.throttle * (input.throttle < 0 ? 11 : tuning_.top_speed);
        // Apply engine force through the tires, respecting both engine output
        // and available traction. Never clamp the chassis velocity directly.
        const float engine_limit = mass * .25f * tuning_.acceleration * dt;
        const float motor_limit = std::min(normal_force * tuning_.motor_grip * dt, engine_limit);
        float long_impulse = clamp((desired - long_speed) * mass * float(0.25 * 0.34), -motor_limit, motor_limit);
        if (std::abs(input.throttle) < float(0.01))
            long_impulse = input.player_controlled ? 0 : -long_speed * mass * float(0.25 * 0.012);
        if (input.handbrake && !wheel.front) {
            const float brake_limit = normal_force * float(0.12) * dt;
            long_impulse = clamp(-long_speed * mass * float(0.25 * 0.9), -brake_limit, brake_limit);
        }
        if (input.parking_brake || direction_braking) {
            const float brake_limit = grip * (input.parking_brake ? 1 : std::abs(input.throttle));
            long_impulse = clamp(-long_speed * mass * 0.25f, -brake_limit, brake_limit);
        }
        long_impulse = clamp(long_impulse, -remaining, remaining);
        physics.AddImpulse(body_, wheel_forward * long_impulse + wheel_right * lateral_impulse, wheel.ground_point);
        wheel.spin += long_speed / tuning_.wheel_radius * dt;
        wheel.skidding = !wheel.front && speed > 4 && normal_force > 100 &&
                         (input.handbrake || std::abs(lateral_speed) > 3);
    }
}
} // namespace forza
