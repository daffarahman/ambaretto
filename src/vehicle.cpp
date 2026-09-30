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
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>
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
