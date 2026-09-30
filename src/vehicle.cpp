#include "vehicle.hpp"
#include <algorithm>
#include <cmath>

namespace forza {
namespace {
constexpr short ground_group = 1;
constexpr short vehicle_group = 2;
constexpr btScalar mass = 1100;
constexpr btScalar rest_length = btScalar(0.58);
constexpr btScalar travel = btScalar(0.32);
constexpr btScalar spring_rate = 22500;
constexpr btScalar damper_rate = 3000;
constexpr btScalar max_steer = btScalar(0.52);
constexpr btScalar tire_grip = btScalar(1.6);
constexpr btScalar motor_grip = btScalar(0.65);
constexpr btScalar rear_drift_grip = btScalar(0.85);

btScalar clamp(btScalar value, btScalar low, btScalar high) {
    return std::clamp(value, low, high);
}
} // namespace

PhysicsWorld::PhysicsWorld(bool with_ridges) {
    world_.setGravity(btVector3(0, btScalar(-9.81), 0));
    world_.getSolverInfo().m_numIterations = 12;
    shapes_.push_back(std::make_unique<btStaticPlaneShape>(btVector3(0, 1, 0), 0));
    add_body(shapes_.back().get(), btVector3(0, 0, 0), 0);
    if (with_ridges) {
        shapes_.push_back(std::make_unique<btBoxShape>(btVector3(4, btScalar(0.06), btScalar(0.16))));
        for (btScalar z : {btScalar(-15), btScalar(-16.7), btScalar(-35), btScalar(-36.7)}) {
            add_body(shapes_.back().get(), btVector3(0, btScalar(0.06), z), 0);
        }
    }
}

PhysicsWorld::~PhysicsWorld() {
    for (const auto& body : bodies_) {
        world_.removeRigidBody(body.get());
    }
    bodies_.clear();
    motion_states_.clear();
    // Compound shapes reference child shapes; destroy the compound first.
    while (!shapes_.empty()) shapes_.pop_back();
}

btRigidBody& PhysicsWorld::add_body(btCollisionShape* shape, const btVector3& position,
                                   btScalar body_mass, const btVector3& inertia) {
    btTransform transform;
    transform.setIdentity();
    transform.setOrigin(position);
    motion_states_.push_back(std::make_unique<btDefaultMotionState>(transform));
    btRigidBody::btRigidBodyConstructionInfo info(body_mass, motion_states_.back().get(), shape, inertia);
    info.m_friction = btScalar(0.8);
    bodies_.push_back(std::make_unique<btRigidBody>(info));
    auto& body = *bodies_.back();
    world_.addRigidBody(&body, body_mass > 0 ? vehicle_group : ground_group,
                       ground_group | vehicle_group);
    return body;
}

btRigidBody& PhysicsWorld::create_chassis() {
    shapes_.push_back(std::make_unique<btBoxShape>(btVector3(btScalar(0.93), btScalar(0.22), btScalar(1.85))));
    auto compound = std::make_unique<btCompoundShape>();
    btTransform child;
    child.setIdentity();
    child.setOrigin(btVector3(0, chassis_offset, 0));
    compound->addChildShape(child, shapes_.back().get());
    shapes_.push_back(std::move(compound));
    // The rigid body's origin is the lowered center of mass. The collider and
    // visible chassis are offset upward, preserving the suspension geometry.
    auto& body = add_body(shapes_.back().get(), btVector3(0, btScalar(0.56), 0), mass,
                          btVector3(1700, 2200, 900));
    body.setDamping(btScalar(0.025), btScalar(0.7));
    body.setActivationState(DISABLE_DEACTIVATION);
    body.setCcdMotionThreshold(btScalar(0.5));
    body.setCcdSweptSphereRadius(btScalar(0.2));
    return body;
}

bool PhysicsWorld::cast_ground(const btVector3& origin, const btVector3& direction,
                               btScalar distance, GroundHit& hit) const {
    const btVector3 end = origin + direction * distance;
    btCollisionWorld::ClosestRayResultCallback callback(origin, end);
    callback.m_collisionFilterGroup = vehicle_group;
    callback.m_collisionFilterMask = ground_group; // Excludes the chassis.
    world_.rayTest(origin, end, callback);
    if (!callback.hasHit()) return false;
    hit.point = callback.m_hitPointWorld;
    hit.normal = callback.m_hitNormalWorld.normalized();
    hit.distance = distance * callback.m_closestHitFraction;
    return true;
}

void PhysicsWorld::step(btScalar dt) {
    world_.stepSimulation(dt, 0, dt);
}

Car::Car(PhysicsWorld& world) : world_(world), body_(world.create_chassis()) {
    const std::array<btVector3, 4> mounts = {
        btVector3(btScalar(-0.82), btScalar(0.31), btScalar(-1.25)),
        btVector3(btScalar(0.82), btScalar(0.31), btScalar(-1.25)),
        btVector3(btScalar(-0.82), btScalar(0.31), btScalar(1.25)),
        btVector3(btScalar(0.82), btScalar(0.31), btScalar(1.25))};
    for (std::size_t i = 0; i < wheels_.size(); ++i) {
        wheels_[i].mount = mounts[i];
        wheels_[i].front = i < 2;
        wheels_[i].center = position() + rotate(mounts[i]) - btVector3(0, rest_length, 0);
    }
}

void Car::step(Input input, btScalar dt) {
    input.throttle = clamp(input.throttle, -1, 1);
    input.steer = clamp(input.steer, -1, 1);
    const auto velocity_now = velocity();
    const btScalar speed = std::hypot(velocity_now.x(), velocity_now.z());
    btScalar steer_limit = std::min(max_steer, btScalar(std::atan(btScalar(2.5 * 16) / std::max(speed * speed, btScalar(1)))));
    if (input.handbrake && speed > 4) steer_limit = std::min(max_steer, steer_limit * btScalar(1.3));
    steer_ += clamp(input.steer * steer_limit - steer_, btScalar(-2.8) * dt, btScalar(2.8) * dt);

    const btVector3 up = rotate(btVector3(0, 1, 0));
    const btVector3 down = -up;
    const btVector3 chassis_forward = forward();
    for (auto& wheel : wheels_) {
        wheel.skidding = false;
        const btVector3 mount = position() + rotate(wheel.mount);
        GroundHit hit;
        wheel.grounded = world_.cast_ground(mount, down, rest_length + travel + wheel_radius, hit)
                         && hit.normal.dot(up) > btScalar(0.35);
        wheel.compression = 0;
        wheel.center = mount + down * rest_length;
        if (!wheel.grounded) {
            wheel.spin += velocity().dot(chassis_forward) / wheel_radius * dt;
            continue;
        }
        const btScalar length = clamp(hit.distance - wheel_radius, rest_length - travel, rest_length + travel);
        wheel.center = mount + down * length;
        wheel.ground_point = hit.point;
        wheel.ground_normal = hit.normal;
        wheel.compression = rest_length - length;
        const btVector3 relative_point = hit.point - position();
        const btVector3 point_velocity = body_.getVelocityInLocalPoint(relative_point);
        const btScalar normal_force = clamp(spring_rate * wheel.compression + damper_rate * point_velocity.dot(down), 0, 14000);
        body_.applyImpulse(up * (normal_force * dt), relative_point);

        btVector3 wheel_forward = chassis_forward;
        if (wheel.front) wheel_forward = quatRotate(btQuaternion(up, steer_), wheel_forward);
        wheel_forward -= hit.normal * wheel_forward.dot(hit.normal);
        wheel_forward.normalize();
        const btVector3 wheel_right = wheel_forward.cross(hit.normal).normalized();
        const btScalar long_speed = point_velocity.dot(wheel_forward);
        const btScalar lateral_speed = point_velocity.dot(wheel_right);

        // Give steering priority in the friction circle. A rear handbrake
        // lowers the grip available to those tires so the rear can slide.
        const btScalar grip_scale = input.handbrake && !wheel.front ? rear_drift_grip : tire_grip;
        const btScalar grip = normal_force * grip_scale * dt;
        const btScalar lateral_impulse = clamp(-lateral_speed * mass * btScalar(0.25 * 0.95), -grip, grip);
        const btScalar remaining = std::sqrt(std::max(btScalar(0), grip * grip - lateral_impulse * lateral_impulse));
        const btScalar desired = input.throttle * (input.throttle < 0 ? 11 : 24);
        const btScalar motor_limit = normal_force * motor_grip * dt;
        btScalar long_impulse = clamp((desired - long_speed) * mass * btScalar(0.25 * 0.34), -motor_limit, motor_limit);
        if (std::abs(input.throttle) < btScalar(0.01)) long_impulse = -long_speed * mass * btScalar(0.25 * 0.012);
        if (input.handbrake && !wheel.front) {
            const btScalar brake_limit = normal_force * btScalar(0.12) * dt;
            long_impulse = clamp(-long_speed * mass * btScalar(0.25 * 0.9), -brake_limit, brake_limit);
        }
        long_impulse = clamp(long_impulse, -remaining, remaining);
        body_.applyImpulse(wheel_forward * long_impulse + wheel_right * lateral_impulse, relative_point);
        wheel.spin += long_speed / wheel_radius * dt;
        wheel.skidding = !wheel.front && speed > 4 && normal_force > 100 &&
                         (input.handbrake || std::abs(lateral_speed) > 3);
    }
}
} // namespace forza
