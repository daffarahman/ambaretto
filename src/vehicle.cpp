#include "vehicle.hpp"
#include "environment.hpp"
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
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace forza {
namespace {
constexpr JPH::ObjectLayer ground_layer = 0;
constexpr JPH::ObjectLayer vehicle_layer = 1;
constexpr JPH::ObjectLayer obstacle_layer = 2;
constexpr float mass = 1100;
constexpr float rest_length = 0.58f;
constexpr float travel = 0.32f;
constexpr float spring_rate = 22500;
constexpr float damper_rate = 3000;
constexpr float max_steer = 0.52f;
constexpr float tire_grip = 1.6f;
constexpr float motor_grip = 0.65f;
constexpr float rear_drift_grip = 0.85f;

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
    JPH::BroadPhaseLayerInterfaceTable broad_phase{3, 2};
    JPH::ObjectLayerPairFilterTable pairs{3};
    std::unique_ptr<JPH::ObjectVsBroadPhaseLayerFilterTable> broad_phase_filter;
    JPH::TempAllocatorImpl allocator{10 * 1024 * 1024};
    JPH::JobSystemThreadPool jobs{JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, 2};
    JPH::PhysicsSystem system;
    std::vector<JPH::BodyID> bodies;

    Impl() {
        broad_phase.MapObjectToBroadPhaseLayer(ground_layer, JPH::BroadPhaseLayer(0));
        broad_phase.MapObjectToBroadPhaseLayer(vehicle_layer, JPH::BroadPhaseLayer(1));
        broad_phase.MapObjectToBroadPhaseLayer(obstacle_layer, JPH::BroadPhaseLayer(0));
        pairs.EnableCollision(ground_layer, vehicle_layer);
        pairs.EnableCollision(vehicle_layer, vehicle_layer);
        pairs.EnableCollision(vehicle_layer, obstacle_layer);
        broad_phase_filter = std::make_unique<JPH::ObjectVsBroadPhaseLayerFilterTable>(broad_phase, 2, pairs, 3);
        system.Init(1024, 0, 2048, 2048, broad_phase, *broad_phase_filter, pairs);
        system.SetGravity(Vec3(0, -9.81f, 0));
        auto settings = system.GetPhysicsSettings();
        settings.mNumVelocitySteps = 12;
        settings.mNumPositionSteps = 4;
        system.SetPhysicsSettings(settings);
    }
    ~Impl() {
        auto& interface = system.GetBodyInterface();
        for (const auto& id : bodies) {
            interface.RemoveBody(id);
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
    JPH::RefConst<JPH::Shape> ground = new JPH::PlaneShape(JPH::Plane(Vec3(0, 1, 0), 0));
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
        JPH::RefConst<JPH::Shape> shape = new JPH::BoxShape(building.size / 2, 0.04f);
        JPH::BodyCreationSettings settings(shape, building.center, Quat::sIdentity(), JPH::EMotionType::Static, obstacle_layer);
        impl_->add_body(settings);
    }
    for (const auto& tree : environment.trees()) {
        JPH::RefConst<JPH::Shape> trunk = new JPH::BoxShape(Vec3(0.3f, tree.height / 2, 0.3f));
        JPH::BodyCreationSettings settings(trunk, tree.base + Vec3(0, tree.height / 2, 0),
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
    if (!impl_->system.GetNarrowPhaseQuery().CastRay(ray, result,
            JPH::SpecifiedBroadPhaseLayerFilter(JPH::BroadPhaseLayer(0)),
            JPH::SpecifiedObjectLayerFilter(ground_layer))) return false;
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

Vec3 Car::position() const { return world_.impl_->system.GetBodyInterface().GetCenterOfMassPosition(body_); }
Vec3 Car::velocity() const { return world_.impl_->system.GetBodyInterface().GetLinearVelocity(body_); }
Quat Car::rotation() const { return world_.impl_->system.GetBodyInterface().GetRotation(body_); }

Car::Car(PhysicsWorld& world) : world_(world), body_(world.create_chassis()) {
    const std::array<Vec3, 4> mounts = {
        Vec3(float(-0.82), float(0.31), float(-1.25)),
        Vec3(float(0.82), float(0.31), float(-1.25)),
        Vec3(float(-0.82), float(0.31), float(1.25)),
        Vec3(float(0.82), float(0.31), float(1.25))};
    for (std::size_t i = 0; i < wheels_.size(); ++i) {
        wheels_[i].mount = mounts[i];
        wheels_[i].front = i < 2;
        wheels_[i].center = position() + rotate(mounts[i]) - Vec3(0, rest_length, 0);
    }
}

void Car::reset(const Vec3& center_of_mass, float yaw) {
    auto& physics = world_.impl_->system.GetBodyInterface();
    const Quat rotation = Quat::sRotation(Vec3::sAxisY(), yaw);
    physics.SetPositionAndRotation(body_, center_of_mass + rotation * Vec3(0, chassis_offset, 0),
        rotation, JPH::EActivation::Activate);
    physics.SetLinearAndAngularVelocity(body_, Vec3::sZero(), Vec3::sZero());
    steer_ = 0;
    for (auto& wheel : wheels_) {
        wheel.grounded = wheel.skidding = false;
        wheel.spin = wheel.compression = 0;
        wheel.center = center_of_mass + rotation * (wheel.mount - Vec3(0, rest_length, 0));
    }
}

void Car::step(Input input, float dt) {
    auto& physics = world_.impl_->system.GetBodyInterface();
    input.throttle = clamp(input.throttle, -1, 1);
    input.steer = clamp(input.steer, -1, 1);
    const auto velocity_now = velocity();
    const float speed = std::hypot(velocity_now.GetX(), velocity_now.GetZ());
    float steer_limit = std::min(max_steer, float(std::atan(float(2.5 * 16) / std::max(speed * speed, float(1)))));
    if (input.handbrake && speed > 4) steer_limit = std::min(max_steer, steer_limit * float(1.3));
    steer_ += clamp(input.steer * steer_limit - steer_, float(-2.8) * dt, float(2.8) * dt);

    const Vec3 up = rotate(Vec3(0, 1, 0));
    const Vec3 down = -up;
    const Vec3 chassis_forward = forward();
    for (auto& wheel : wheels_) {
        wheel.skidding = false;
        const Vec3 mount = position() + rotate(wheel.mount);
        GroundHit hit;
        wheel.grounded = world_.cast_ground(mount, down, rest_length + travel + wheel_radius, hit)
                         && hit.normal.Dot(up) > float(0.35);
        wheel.compression = 0;
        wheel.center = mount + down * rest_length;
        if (!wheel.grounded) {
            wheel.spin += velocity().Dot(chassis_forward) / wheel_radius * dt;
            continue;
        }
        const float length = clamp(hit.distance - wheel_radius, rest_length - travel, rest_length + travel);
        wheel.center = mount + down * length;
        wheel.ground_point = hit.point;
        wheel.ground_normal = hit.normal;
        wheel.compression = rest_length - length;

        const Vec3 point_velocity = physics.GetPointVelocity(body_, hit.point);
        const float normal_force = clamp(spring_rate * wheel.compression + damper_rate * point_velocity.Dot(down), 0, 14000);
        physics.AddImpulse(body_, up * (normal_force * dt), hit.point);

        Vec3 wheel_forward = chassis_forward;
        if (wheel.front) wheel_forward = Quat::sRotation(up, steer_) * wheel_forward;
        wheel_forward -= hit.normal * wheel_forward.Dot(hit.normal);
        wheel_forward = wheel_forward.Normalized();
        const Vec3 wheel_right = wheel_forward.Cross(hit.normal).Normalized();
        const float long_speed = point_velocity.Dot(wheel_forward);
        const float lateral_speed = point_velocity.Dot(wheel_right);

        // Give steering priority in the friction circle. A rear handbrake
        // lowers the grip available to those tires so the rear can slide.
        const float grip_scale = input.handbrake && !wheel.front ? rear_drift_grip : tire_grip;
        const float grip = normal_force * grip_scale * dt;
        const float lateral_impulse = clamp(-lateral_speed * mass * float(0.25 * 0.95), -grip, grip);
        const float remaining = std::sqrt(std::max(float(0), grip * grip - lateral_impulse * lateral_impulse));
        const float desired = input.throttle * (input.throttle < 0 ? 11 : 24);
        const float motor_limit = normal_force * motor_grip * dt;
        float long_impulse = clamp((desired - long_speed) * mass * float(0.25 * 0.34), -motor_limit, motor_limit);
        if (std::abs(input.throttle) < float(0.01)) long_impulse = -long_speed * mass * float(0.25 * 0.012);
        if (input.handbrake && !wheel.front) {
            const float brake_limit = normal_force * float(0.12) * dt;
            long_impulse = clamp(-long_speed * mass * float(0.25 * 0.9), -brake_limit, brake_limit);
        }
        long_impulse = clamp(long_impulse, -remaining, remaining);
        physics.AddImpulse(body_, wheel_forward * long_impulse + wheel_right * lateral_impulse, hit.point);
        wheel.spin += long_speed / wheel_radius * dt;
        wheel.skidding = !wheel.front && speed > 4 && normal_force > 100 &&
                         (input.handbrake || std::abs(lateral_speed) > 3);
    }
}
} // namespace forza
