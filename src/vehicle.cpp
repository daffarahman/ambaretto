#include "vehicle.hpp"
#include "environment.hpp"
#include "player.hpp"
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
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/ShapeCast.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
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

float PhysicsWorld::camera_fraction(const Vec3& origin, const Vec3& offset, JPH::BodyID ignore) const {
    if (offset.LengthSq() < 0.0001f) return 1;
    JPH::SphereShape sphere(0.28f);
    const JPH::RShapeCast cast(&sphere, Vec3::sOne(), JPH::RMat44::sTranslation(origin), offset);
    JPH::ShapeCastSettings settings;
    settings.mBackFaceModeTriangles = JPH::EBackFaceMode::CollideWithBackFaces;
    JPH::ClosestHitCollisionCollector<JPH::CastShapeCollector> hits;
    impl_->system.GetNarrowPhaseQuery().CastShape(cast, settings, origin, hits, {}, {}, JPH::IgnoreSingleBodyFilter(ignore));
    return hits.HadHit() ? std::clamp(hits.mHit.mFraction - 0.03f / offset.Length(), 0.0f, 1.0f) : 1;
}

struct Character::Impl {
    JPH::Ref<JPH::CharacterVirtual> character;
    Vec3 desired_velocity{0, 0, 0};
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
Vec3 Character::position() const { return impl_->character->GetPosition(); }
Vec3 Character::velocity() const { return impl_->character->GetLinearVelocity(); }
bool Character::grounded() const { return impl_->character->GetGroundState() == JPH::CharacterBase::EGroundState::OnGround; }

void Character::reset(const Vec3& feet, float yaw) {
    yaw_ = yaw; gait_ = 0;
    impl_->desired_velocity = Vec3::sZero();
    impl_->character->SetPosition(feet);
    impl_->character->SetRotation(Quat::sRotation(Vec3::sAxisY(), yaw));
    impl_->character->SetLinearVelocity(Vec3::sZero());
    impl_->character->RefreshContacts({}, {}, {}, {}, world_.impl_->allocator);
}

bool Character::can_stand_at(const Vec3& feet) const {
    const auto* shape = impl_->character->GetShape();
    JPH::AllHitCollisionCollector<JPH::CollideShapeCollector> hits;
    world_.impl_->system.GetNarrowPhaseQuery().CollideShape(shape, Vec3::sOne(),
        JPH::RMat44::sTranslation(feet + shape->GetCenterOfMass()), {}, feet, hits);
    for (const auto& hit : hits.mHits) if (hit.mPenetrationDepth > 0.005f) return false;
    return true;
}

void Character::step(FootInput input, float dt) {
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
    character.SetLinearVelocity(impl_->desired_velocity + Vec3(0, vertical_speed, 0));
    JPH::CharacterVirtual::ExtendedUpdateSettings settings;
    settings.mWalkStairsStepUp = Vec3(0, 0.35f, 0);
    settings.mStickToFloorStepDown = vertical_speed > 0.1f ? Vec3::sZero() : Vec3(0, -0.35f, 0);
    character.ExtendedUpdate(dt, Vec3(0, -18, 0), settings, {}, {}, {}, {}, world_.impl_->allocator);
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
        rotation, JPH::EActivation::Activate);
    physics.SetLinearAndAngularVelocity(body_, Vec3::sZero(), Vec3::sZero());
    steer_ = 0;
    for (auto& wheel : wheels_) {
        wheel.grounded = wheel.skidding = false;
        wheel.spin = wheel.compression = wheel.normal_force = 0;
        wheel.center = center_of_mass + rotation * (wheel.mount - Vec3(0, tuning_.rest_length, 0));
    }
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
        const float normal_force = clamp(tuning_.spring_rate * wheel.compression - tuning_.damper_rate * point_velocity.Dot(up),
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
        const float desired = input.throttle * (input.throttle < 0 ? 11 : 24);
        const float motor_limit = normal_force * tuning_.motor_grip * dt;
        float long_impulse = clamp((desired - long_speed) * mass * float(0.25 * 0.34), -motor_limit, motor_limit);
        if (std::abs(input.throttle) < float(0.01)) long_impulse = -long_speed * mass * float(0.25 * 0.012);
        if (input.handbrake && !wheel.front) {
            const float brake_limit = normal_force * float(0.12) * dt;
            long_impulse = clamp(-long_speed * mass * float(0.25 * 0.9), -brake_limit, brake_limit);
        }
        if (input.parking_brake)
            long_impulse = clamp(-long_speed * mass * 0.25f, -grip, grip);
        long_impulse = clamp(long_impulse, -remaining, remaining);
        physics.AddImpulse(body_, wheel_forward * long_impulse + wheel_right * lateral_impulse, wheel.ground_point);
        wheel.spin += long_speed / tuning_.wheel_radius * dt;
        wheel.skidding = !wheel.front && speed > 4 && normal_force > 100 &&
                         (input.handbrake || std::abs(lateral_speed) > 3);
    }
}
} // namespace forza
