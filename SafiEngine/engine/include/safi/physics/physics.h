/**
 * safi/physics/physics.h — rigid-body physics powered by Jolt.
 *
 * The physics system runs on SafiFixedUpdate. Entities that carry
 * (SafiTransform, SafiRigidBody, SafiCollider) participate in simulation:
 *
 *   - Static bodies never move but other bodies collide with them.
 *   - Dynamic bodies are driven by forces and gravity.
 *   - Kinematic bodies are moved by user code (OnUpdate); physics pushes
 *     their transform into Jolt before each step.
 *
 * After each step the system pulls Jolt's positions/rotations back into
 * SafiTransform for dynamic bodies. Transform propagation (PreStore)
 * then writes SafiGlobalTransform before the render stage.
 */
#ifndef SAFI_PHYSICS_H
#define SAFI_PHYSICS_H

#include <stdbool.h>
#include <stdint.h>
#include <flecs.h>

/* ---- Body type ---------------------------------------------------------- */
typedef enum SafiBodyType {
    SAFI_BODY_STATIC,
    SAFI_BODY_DYNAMIC,
    SAFI_BODY_KINEMATIC,
} SafiBodyType;

/* ---- Collider shape ----------------------------------------------------- */
typedef enum SafiColliderShape {
    SAFI_COLLIDER_BOX,
    SAFI_COLLIDER_SPHERE,
} SafiColliderShape;

/* ---- Components --------------------------------------------------------- */

typedef struct SafiRigidBody {
    SafiBodyType type;
    float mass;            /* kg; 0 for static                           */
    float friction;        /* [0, 1]; default 0.5                         */
    float restitution;     /* [0, 1]; default 0.3                         */
    uint32_t _body_id;     /* Jolt body ID — set by the physics system    */
    bool _registered;      /* internal — true once added to Jolt          */
} SafiRigidBody;

typedef struct SafiCollider {
    SafiColliderShape shape;
    union {
        struct { float half_extents[3]; } box;
        struct { float radius; }          sphere;
    };
} SafiCollider;

/* ---- Lifecycle ---------------------------------------------------------- */

/* Initialize Jolt, register physics components and the physics system on
 * SafiFixedUpdate. Called automatically by safi_app_init. */
bool safi_physics_init(ecs_world_t *world);

/* Shut down Jolt and release all resources. Called by safi_app_shutdown. */
void safi_physics_shutdown(void);

/* ---- Component declarations --------------------------------------------- */
extern ECS_COMPONENT_DECLARE(SafiRigidBody);
extern ECS_COMPONENT_DECLARE(SafiCollider);

#endif /* SAFI_PHYSICS_H */
