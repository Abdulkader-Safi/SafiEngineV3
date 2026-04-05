/**
 * safi/ecs/components.h — stock ECS components.
 *
 * Every SafiEngine app registers these automatically. They map cleanly to
 * Bevy's `Transform`, `Camera`, `Handle<Mesh>` + `Handle<Material>`, `Name`.
 *
 * The component IDs are exposed as `extern ECS_COMPONENT_DECLARE(T);` so
 * user systems can reference them across translation units.
 */
#ifndef SAFI_ECS_COMPONENTS_H
#define SAFI_ECS_COMPONENTS_H

#include <cglm/cglm.h>
#include <cglm/struct.h>
#include <flecs.h>

#include "safi/core/time.h"
#include "safi/input/input.h"

/* ---- Transform ---------------------------------------------------------- */
typedef struct SafiTransform {
    vec3   position;
    versor rotation;   /* quaternion (x, y, z, w) */
    vec3   scale;
} SafiTransform;

static inline SafiTransform safi_transform_identity(void) {
    SafiTransform t;
    glm_vec3_zero(t.position);
    glm_quat_identity(t.rotation);
    t.scale[0] = t.scale[1] = t.scale[2] = 1.0f;
    return t;
}

/* ---- Camera ------------------------------------------------------------- */
typedef struct SafiCamera {
    float fov_y_radians;
    float z_near;
    float z_far;
    vec3  target;
    mat4  view;
    mat4  proj;
} SafiCamera;

/* ---- MeshRenderer ------------------------------------------------------- */
struct SafiMesh;
struct SafiMaterial;

typedef struct SafiMeshRenderer {
    struct SafiMesh     *mesh;
    struct SafiMaterial *material;
} SafiMeshRenderer;

/* ---- Name --------------------------------------------------------------- */
typedef struct SafiName {
    const char *value;
} SafiName;

/* ---- Spin (demo component) --------------------------------------------- */
typedef struct SafiSpin {
    vec3  axis;
    float speed;   /* radians per second */
} SafiSpin;

/* ---- Component declarations (defined in engine/src/ecs/components.c) ---- */
extern ECS_COMPONENT_DECLARE(SafiTransform);
extern ECS_COMPONENT_DECLARE(SafiCamera);
extern ECS_COMPONENT_DECLARE(SafiMeshRenderer);
extern ECS_COMPONENT_DECLARE(SafiName);
extern ECS_COMPONENT_DECLARE(SafiSpin);
extern ECS_COMPONENT_DECLARE(SafiTime);
extern ECS_COMPONENT_DECLARE(SafiInput);

/* Registers every stock component with the given world. Called by
 * safi_ecs_create(); exposed here for users who bring their own world. */
void safi_register_builtin_components(ecs_world_t *world);

#endif /* SAFI_ECS_COMPONENTS_H */
