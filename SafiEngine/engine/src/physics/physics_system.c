#include "safi/physics/physics.h"
#include "safi/ecs/components.h"
#include "safi/ecs/phases.h"
#include "safi/core/log.h"

#include "jolt_wrapper.h"

ECS_COMPONENT_DECLARE(SafiRigidBody);
ECS_COMPONENT_DECLARE(SafiCollider);

/* Map engine body type → Jolt motion type. */
static SafiJoltMotionType map_motion(SafiBodyType t) {
    switch (t) {
    case SAFI_BODY_STATIC:    return SAFI_JOLT_MOTION_STATIC;
    case SAFI_BODY_DYNAMIC:   return SAFI_JOLT_MOTION_DYNAMIC;
    case SAFI_BODY_KINEMATIC: return SAFI_JOLT_MOTION_KINEMATIC;
    }
    return SAFI_JOLT_MOTION_DYNAMIC;
}

/* Runs on SafiFixedUpdate.
 *
 * Per tick:
 *   1. Register new bodies (SafiRigidBody._registered == false)
 *   2. Push kinematic transforms into Jolt
 *   3. Step Jolt
 *   4. Pull dynamic transforms back into SafiTransform
 */
static void physics_system(ecs_iter_t *it) {
    float dt = it->delta_time;

    ecs_query_t *q = ecs_query(it->world, {
        .terms = {
            { .id = ecs_id(SafiTransform) },
            { .id = ecs_id(SafiRigidBody) },
            { .id = ecs_id(SafiCollider) },
        },
        .cache_kind = EcsQueryCacheNone,
    });

    /* ---- Phase 1 + 2: register new bodies, detect teleports ------------- */
    bool wake_dynamic = false;
    ecs_iter_t qit = ecs_query_iter(it->world, q);
    while (ecs_query_next(&qit)) {
        SafiTransform *xf  = ecs_field(&qit, SafiTransform, 0);
        SafiRigidBody *rb  = ecs_field(&qit, SafiRigidBody, 1);
        SafiCollider  *col = ecs_field(&qit, SafiCollider, 2);

        for (int i = 0; i < qit.count; i++) {
            if (!rb[i]._registered) {
                SafiJoltMotionType motion = map_motion(rb[i].type);
                uint32_t id = UINT32_MAX;

                uint64_t udata = (uint64_t)qit.entities[i];
                switch (col[i].shape) {
                case SAFI_COLLIDER_BOX:
                    id = safi_jolt_add_box(
                        col[i].box.half_extents,
                        xf[i].position, xf[i].rotation,
                        rb[i].mass, rb[i].friction, rb[i].restitution,
                        motion, udata);
                    break;
                case SAFI_COLLIDER_SPHERE:
                    id = safi_jolt_add_sphere(
                        col[i].sphere.radius,
                        xf[i].position, xf[i].rotation,
                        rb[i].mass, rb[i].friction, rb[i].restitution,
                        motion, udata);
                    break;
                }

                if (id != UINT32_MAX) {
                    rb[i]._body_id    = id;
                    rb[i]._registered = true;
                } else {
                    SAFI_LOG_ERROR("physics: failed to create Jolt body");
                }
            } else {
                /* Detect user/inspector edits on ANY registered body:
                 * if SafiTransform disagrees with Jolt by more than an
                 * epsilon, destroy the body and let the registration
                 * path recreate it next tick. This clears cached contacts
                 * and velocity so the body restarts cleanly. */
                float jp[3], jr[4];
                safi_jolt_get_transform(rb[i]._body_id, jp, jr);
                #define POS_EPS 0.001f
                #define ROT_EPS 0.0001f
                #define FABSF(x) ((x) < 0 ? -(x) : (x))
                bool pos_changed = FABSF(jp[0] - xf[i].position[0]) > POS_EPS
                                || FABSF(jp[1] - xf[i].position[1]) > POS_EPS
                                || FABSF(jp[2] - xf[i].position[2]) > POS_EPS;
                bool rot_changed = FABSF(jr[0] - xf[i].rotation[0]) > ROT_EPS
                                || FABSF(jr[1] - xf[i].rotation[1]) > ROT_EPS
                                || FABSF(jr[2] - xf[i].rotation[2]) > ROT_EPS
                                || FABSF(jr[3] - xf[i].rotation[3]) > ROT_EPS;
                #undef FABSF
                #undef ROT_EPS
                #undef POS_EPS
                if (pos_changed || rot_changed) {
                    safi_jolt_remove_body(rb[i]._body_id);
                    rb[i]._registered = false;
                    if (rb[i].type == SAFI_BODY_STATIC)
                        wake_dynamic = true;
                } else if (rb[i].type == SAFI_BODY_KINEMATIC) {
                    /* Kinematic bodies get their transform pushed every
                     * tick (user code drives them from OnUpdate). */
                    safi_jolt_set_transform(rb[i]._body_id,
                                            xf[i].position, xf[i].rotation);
                }
            }
        }
    }

    /* If a static body was recreated (e.g. ground moved in the inspector),
     * wake all sleeping dynamic bodies so they re-evaluate contacts. */
    if (wake_dynamic) {
        qit = ecs_query_iter(it->world, q);
        while (ecs_query_next(&qit)) {
            SafiRigidBody *rb = ecs_field(&qit, SafiRigidBody, 1);
            for (int i = 0; i < qit.count; i++) {
                if (rb[i]._registered && rb[i].type == SAFI_BODY_DYNAMIC)
                    safi_jolt_activate_body(rb[i]._body_id);
            }
        }
    }

    /* ---- Phase 3: step -------------------------------------------------- */
    safi_jolt_step(dt);

    /* ---- Phase 4: pull dynamic transforms ------------------------------- */
    qit = ecs_query_iter(it->world, q);
    while (ecs_query_next(&qit)) {
        SafiTransform *xf = ecs_field(&qit, SafiTransform, 0);
        SafiRigidBody *rb = ecs_field(&qit, SafiRigidBody, 1);

        for (int i = 0; i < qit.count; i++) {
            if (rb[i].type == SAFI_BODY_DYNAMIC && rb[i]._registered) {
                safi_jolt_get_transform(rb[i]._body_id,
                                        xf[i].position, xf[i].rotation);
            }
        }
    }

    ecs_query_fini(q);
}

bool safi_physics_init(ecs_world_t *world) {
    if (!safi_jolt_init()) {
        SAFI_LOG_ERROR("physics: Jolt init failed");
        return false;
    }

    ECS_COMPONENT_DEFINE(world, SafiRigidBody);
    ECS_COMPONENT_DEFINE(world, SafiCollider);

    ecs_system(world, {
        .entity = ecs_entity(world, {
            .name = "safi_physics_system",
            .add  = ecs_ids(ecs_dependson(SafiFixedUpdate)),
        }),
        .callback = physics_system,
    });

    SAFI_LOG_INFO("physics: Jolt initialized (single-threaded, gravity -9.81)");
    return true;
}

void safi_physics_shutdown(void) {
    safi_jolt_shutdown();
}

/* ---- Collision queries -------------------------------------------------- */

bool safi_physics_raycast(ecs_world_t *world,
                          const float origin[3],
                          const float direction[3],
                          float max_distance,
                          ecs_entity_t ignore,
                          SafiRayHit *out_hit) {
    (void)world;
    if (!out_hit) return false;

    uint32_t ignore_body = UINT32_MAX;
    if (ignore && world) {
        const SafiRigidBody *rb = ecs_get(world, ignore, SafiRigidBody);
        if (rb && rb->_registered) ignore_body = rb->_body_id;
    }

    SafiJoltRayHit jhit;
    if (!safi_jolt_raycast(origin, direction, max_distance, ignore_body, &jhit))
        return false;

    out_hit->entity    = (ecs_entity_t)jhit.user_data;
    out_hit->body_id   = jhit.body_id;
    out_hit->point[0]  = jhit.point[0];
    out_hit->point[1]  = jhit.point[1];
    out_hit->point[2]  = jhit.point[2];
    out_hit->normal[0] = jhit.normal[0];
    out_hit->normal[1] = jhit.normal[1];
    out_hit->normal[2] = jhit.normal[2];
    out_hit->fraction  = jhit.fraction;
    return true;
}

int safi_physics_overlap_box(ecs_world_t *world,
                             const float center[3],
                             const float half_extents[3],
                             const float rotation[4],
                             ecs_entity_t *out_entities, int cap) {
    (void)world;
    /* ecs_entity_t is uint64_t in flecs — cast the buffer through. */
    return safi_jolt_overlap_box(center, half_extents, rotation,
                                 (uint64_t *)out_entities, cap);
}

int safi_physics_overlap_sphere(ecs_world_t *world,
                                const float center[3],
                                float radius,
                                ecs_entity_t *out_entities, int cap) {
    (void)world;
    return safi_jolt_overlap_sphere(center, radius,
                                    (uint64_t *)out_entities, cap);
}
