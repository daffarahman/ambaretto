#pragma once

#include <btBulletDynamicsCommon.h>
#include <array>
#include <memory>
#include <vector>

namespace forza {

inline constexpr btScalar fixed_step = btScalar(1.0 / 120.0);
inline constexpr btScalar wheel_radius = btScalar(0.34);
inline constexpr btScalar chassis_offset = btScalar(0.5);

struct Input {
    btScalar throttle = 0;
    btScalar steer = 0; // Positive turns left; the car faces local -Z.
    bool handbrake = false;
};

struct Wheel {
    btVector3 mount{0, 0, 0};
    bool front = false;
    btVector3 center{0, 0, 0};
    btVector3 ground_point{0, 0, 0};
    btVector3 ground_normal{0, 1, 0};
    bool grounded = false;
    bool skidding = false;
    btScalar compression = 0;
    btScalar spin = 0;
};

struct GroundHit {
    btVector3 point{0, 0, 0};
    btVector3 normal{0, 1, 0};
    btScalar distance = 0;
};

class PhysicsWorld {
public:
    explicit PhysicsWorld(bool with_ridges = true);
    ~PhysicsWorld();
    PhysicsWorld(const PhysicsWorld&) = delete;
    PhysicsWorld& operator=(const PhysicsWorld&) = delete;

    btRigidBody& create_chassis();
    bool cast_ground(const btVector3& origin, const btVector3& direction,
                     btScalar distance, GroundHit& hit) const;
    void step(btScalar dt = fixed_step);

private:
    btRigidBody& add_body(btCollisionShape* shape, const btVector3& position,
                         btScalar mass, const btVector3& inertia = btVector3(0, 0, 0));
    btDefaultCollisionConfiguration configuration_;
    btCollisionDispatcher dispatcher_{&configuration_};
    btDbvtBroadphase broadphase_;
    btSequentialImpulseConstraintSolver solver_;
    btDiscreteDynamicsWorld world_{&dispatcher_, &broadphase_, &solver_, &configuration_};
    std::vector<std::unique_ptr<btCollisionShape>> shapes_;
    std::vector<std::unique_ptr<btDefaultMotionState>> motion_states_;
    std::vector<std::unique_ptr<btRigidBody>> bodies_;
};

class Car {
public:
    explicit Car(PhysicsWorld& world);
    void step(Input input, btScalar dt = fixed_step);
    const btRigidBody& body() const { return body_; }
    btVector3 position() const { return body_.getCenterOfMassPosition(); }
    btVector3 velocity() const { return body_.getLinearVelocity(); }
    btVector3 rotate(const btVector3& local) const {
        return body_.getWorldTransform().getBasis() * local;
    }
    btVector3 forward() const { return rotate(btVector3(0, 0, -1)); }
    const std::array<Wheel, 4>& wheels() const { return wheels_; }
    btScalar steering() const { return steer_; }

private:
    PhysicsWorld& world_;
    btRigidBody& body_;
    std::array<Wheel, 4> wheels_{};
    btScalar steer_ = 0;
};

} // namespace forza
