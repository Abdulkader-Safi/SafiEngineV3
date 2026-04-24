#include "safi/scene/presets.h"
#include "safi/ecs/components.h"
#include "safi/ecs/component_registry.h"
#include "safi/physics/physics.h"

#include <string.h>

/* s_new_named: common prefix — create entity, attach SafiName (which
 * triggers the auto-stable-id observer) and SafiTransform/GlobalTransform
 * so the entity always has a position in the world. */
static ecs_entity_t s_new_named(ecs_world_t *world, const char *name) {
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, SafiName, { .value = name ? name : "entity" });
    safi_component_registry_construct(world, e, "SafiTransform");
    safi_component_registry_construct(world, e, "SafiGlobalTransform");
    return e;
}

ecs_entity_t safi_preset_empty(ecs_world_t *world, const char *name) {
    return s_new_named(world, name);
}

ecs_entity_t safi_preset_mesh(ecs_world_t *world, const char *name,
                              SafiModelHandle model) {
    ecs_entity_t e = s_new_named(world, name);
    safi_component_registry_construct(world, e, "SafiMeshRenderer");
    /* default_init set model={0}; now assign the requested handle via
     * ecs_set so the SafiMeshRenderer hook acquires it. */
    ecs_set(world, e, SafiMeshRenderer, { .model = model, .visible = true });
    return e;
}

ecs_entity_t safi_preset_primitive(ecs_world_t *world, const char *name,
                                   SafiPrimitiveShape shape) {
    ecs_entity_t e = s_new_named(world, name);
    safi_component_registry_construct(world, e, "SafiPrimitive");
    /* Default was box; override the shape if different. */
    SafiPrimitive *p = ecs_get_mut(world, e, SafiPrimitive);
    if (p) {
        p->shape = shape;
        ecs_modified(world, e, SafiPrimitive);
    }
    return e;
}

ecs_entity_t safi_preset_directional_light(ecs_world_t *world, const char *name) {
    ecs_entity_t e = s_new_named(world, name);
    safi_component_registry_construct(world, e, "SafiDirectionalLight");
    return e;
}
ecs_entity_t safi_preset_point_light(ecs_world_t *world, const char *name) {
    ecs_entity_t e = s_new_named(world, name);
    safi_component_registry_construct(world, e, "SafiPointLight");
    return e;
}
ecs_entity_t safi_preset_spot_light(ecs_world_t *world, const char *name) {
    ecs_entity_t e = s_new_named(world, name);
    safi_component_registry_construct(world, e, "SafiSpotLight");
    return e;
}
ecs_entity_t safi_preset_rect_light(ecs_world_t *world, const char *name) {
    ecs_entity_t e = s_new_named(world, name);
    safi_component_registry_construct(world, e, "SafiRectLight");
    return e;
}
ecs_entity_t safi_preset_sky_light(ecs_world_t *world, const char *name) {
    ecs_entity_t e = s_new_named(world, name);
    safi_component_registry_construct(world, e, "SafiSkyLight");
    return e;
}

ecs_entity_t safi_preset_camera(ecs_world_t *world, const char *name) {
    ecs_entity_t e = s_new_named(world, name);
    safi_component_registry_construct(world, e, "SafiCamera");
    return e;
}

ecs_entity_t safi_preset_static_box(ecs_world_t *world, const char *name,
                                    float half_x, float half_y, float half_z) {
    ecs_entity_t e = s_new_named(world, name);
    safi_component_registry_construct(world, e, "SafiRigidBody");
    safi_component_registry_construct(world, e, "SafiCollider");

    SafiRigidBody *rb = ecs_get_mut(world, e, SafiRigidBody);
    if (rb) {
        rb->type = SAFI_BODY_STATIC;
        ecs_modified(world, e, SafiRigidBody);
    }
    SafiCollider *c = ecs_get_mut(world, e, SafiCollider);
    if (c) {
        c->shape              = SAFI_COLLIDER_BOX;
        c->box.half_extents[0] = half_x;
        c->box.half_extents[1] = half_y;
        c->box.half_extents[2] = half_z;
        ecs_modified(world, e, SafiCollider);
    }
    return e;
}

ecs_entity_t safi_preset_dynamic_sphere(ecs_world_t *world, const char *name,
                                        float radius, float mass) {
    ecs_entity_t e = s_new_named(world, name);
    safi_component_registry_construct(world, e, "SafiRigidBody");
    safi_component_registry_construct(world, e, "SafiCollider");

    SafiRigidBody *rb = ecs_get_mut(world, e, SafiRigidBody);
    if (rb) {
        rb->type = SAFI_BODY_DYNAMIC;
        rb->mass = mass > 0.0f ? mass : 1.0f;
        ecs_modified(world, e, SafiRigidBody);
    }
    SafiCollider *c = ecs_get_mut(world, e, SafiCollider);
    if (c) {
        c->shape          = SAFI_COLLIDER_SPHERE;
        c->sphere.radius  = radius > 0.0f ? radius : 0.5f;
        ecs_modified(world, e, SafiCollider);
    }
    return e;
}
