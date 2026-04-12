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

/* Compose a TRS model matrix from a local transform. The result equals
 * T * R * S, matching the convention used throughout the engine (column
 * vectors on the right). */
static inline void safi_transform_to_mat4(const SafiTransform *xf, mat4 out) {
    glm_mat4_identity(out);
    glm_translate(out, (float *)xf->position);
    mat4 rot;
    glm_quat_mat4((float *)xf->rotation, rot);
    glm_mat4_mul(out, rot, out);
    glm_scale(out, (float *)xf->scale);
}

/* ---- GlobalTransform ---------------------------------------------------- *
 * World-space model matrix. Written every frame by the transform
 * propagation system (see engine/src/ecs/transform.c) on EcsPostUpdate.
 * Renderers and physics read this instead of rebuilding a matrix from
 * SafiTransform.
 *
 * Opt-in: entities only get a world transform if they explicitly carry
 * SafiGlobalTransform. Entities with only SafiTransform are ignored by
 * the propagation pass. */
typedef struct SafiGlobalTransform {
    mat4 matrix;
} SafiGlobalTransform;

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

/* ---- Lights ------------------------------------------------------------- */

/* Infinite-distance parallel light (sunlight). */
typedef struct SafiDirectionalLight {
    float direction[3];   /* world-space, normalized */
    float intensity;
    float color[3];
    float _pad0;
} SafiDirectionalLight;

/* Omni-directional point light (bulb). Position from SafiTransform. */
typedef struct SafiPointLight {
    float color[3];
    float intensity;
    float range;          /* attenuation radius */
    float _pad0[3];
} SafiPointLight;

/* Cone-shaped spotlight. Position/direction from SafiTransform. */
typedef struct SafiSpotLight {
    float color[3];
    float intensity;
    float range;
    float inner_angle;    /* cosine of half-angle */
    float outer_angle;    /* cosine of half-angle */
    float _pad0;
} SafiSpotLight;

/* Rectangular area light (panel). Position/orientation from SafiTransform. */
typedef struct SafiRectLight {
    float color[3];
    float intensity;
    float width;
    float height;
    float _pad0[2];
} SafiRectLight;

/* Uniform ambient environment light (sky). */
typedef struct SafiSkyLight {
    float color[3];
    float intensity;
} SafiSkyLight;

/* ---- Component declarations (defined in engine/src/ecs/components.c) ---- */
extern ECS_COMPONENT_DECLARE(SafiTransform);
extern ECS_COMPONENT_DECLARE(SafiGlobalTransform);
extern ECS_COMPONENT_DECLARE(SafiCamera);
extern ECS_COMPONENT_DECLARE(SafiMeshRenderer);
extern ECS_COMPONENT_DECLARE(SafiName);
extern ECS_COMPONENT_DECLARE(SafiSpin);
extern ECS_COMPONENT_DECLARE(SafiTime);
extern ECS_COMPONENT_DECLARE(SafiInput);
extern ECS_COMPONENT_DECLARE(SafiDirectionalLight);
extern ECS_COMPONENT_DECLARE(SafiPointLight);
extern ECS_COMPONENT_DECLARE(SafiSpotLight);
extern ECS_COMPONENT_DECLARE(SafiRectLight);
extern ECS_COMPONENT_DECLARE(SafiSkyLight);

/* Registers every stock component with the given world. Called by
 * safi_ecs_create(); exposed here for users who bring their own world. */
void safi_register_builtin_components(ecs_world_t *world);

#endif /* SAFI_ECS_COMPONENTS_H */
