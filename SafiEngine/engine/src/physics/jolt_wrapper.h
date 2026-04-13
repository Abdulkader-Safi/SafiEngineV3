/**
 * jolt_wrapper.h — internal C API for the Jolt Physics bridge.
 *
 * This header is consumed by physics_system.c (pure C). The implementation
 * lives in jolt_wrapper.cpp — the single C++ translation unit in the engine.
 */
#ifndef SAFI_JOLT_WRAPPER_H
#define SAFI_JOLT_WRAPPER_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Lifecycle ---------------------------------------------------------- */
bool safi_jolt_init(void);
void safi_jolt_shutdown(void);
void safi_jolt_step(float dt);

/* ---- Body motion types -------------------------------------------------- */
typedef enum SafiJoltMotionType {
    SAFI_JOLT_MOTION_STATIC,
    SAFI_JOLT_MOTION_DYNAMIC,
    SAFI_JOLT_MOTION_KINEMATIC,
} SafiJoltMotionType;

/* ---- Body creation ------------------------------------------------------ */
uint32_t safi_jolt_add_box(
    const float half_extents[3],
    const float position[3],
    const float rotation[4],   /* quaternion x,y,z,w */
    float mass,
    float friction,
    float restitution,
    SafiJoltMotionType motion,
    uint64_t user_data          /* opaque; round-trips through queries */
);

uint32_t safi_jolt_add_sphere(
    float radius,
    const float position[3],
    const float rotation[4],
    float mass,
    float friction,
    float restitution,
    SafiJoltMotionType motion,
    uint64_t user_data
);

/* ---- Collision queries -------------------------------------------------- */
typedef struct SafiJoltRayHit {
    uint32_t body_id;
    uint64_t user_data;
    float    point[3];
    float    normal[3];
    float    fraction;   /* [0, 1] along the ray */
} SafiJoltRayHit;

/* Closest-hit raycast. `ignore_body_id == UINT32_MAX` disables the filter.
 * Returns true if anything was hit and fills `*out`. */
bool safi_jolt_raycast(
    const float origin[3],
    const float direction[3],
    float max_distance,
    uint32_t ignore_body_id,
    SafiJoltRayHit *out
);

/* Overlap queries. Fill up to `cap` user_data entries into `out_user_data`
 * and return the total number of hits found (may exceed cap). */
int safi_jolt_overlap_box(
    const float center[3],
    const float half_extents[3],
    const float rotation[4],   /* nullable — NULL treated as identity */
    uint64_t *out_user_data,
    int cap
);

int safi_jolt_overlap_sphere(
    const float center[3],
    float radius,
    uint64_t *out_user_data,
    int cap
);

/* ---- Body operations ---------------------------------------------------- */
void     safi_jolt_remove_body(uint32_t body_id);
void     safi_jolt_get_transform(uint32_t body_id, float pos[3], float rot[4]);
void     safi_jolt_set_transform(uint32_t body_id, const float pos[3], const float rot[4]);
void     safi_jolt_add_force(uint32_t body_id, const float force[3]);
void     safi_jolt_add_impulse(uint32_t body_id, const float impulse[3]);
void     safi_jolt_set_linear_velocity(uint32_t body_id, const float vel[3]);
void     safi_jolt_set_angular_velocity(uint32_t body_id, const float vel[3]);
bool     safi_jolt_is_active(uint32_t body_id);
void     safi_jolt_activate_body(uint32_t body_id);

#ifdef __cplusplus
}
#endif

#endif /* SAFI_JOLT_WRAPPER_H */
