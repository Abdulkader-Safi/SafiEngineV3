/*
 * component_serializers.c — per-component serialize / deserialize / inspector
 *
 * Registers every stock component's callbacks with the component registry.
 * Called once from safi_register_builtin_components after all
 * ECS_COMPONENT_DEFINE calls.
 */
#include "component_serializers.h"
#include "safi/ecs/components.h"
#include "safi/ecs/component_registry.h"
#include "safi/render/assets.h"
#include "safi/physics/physics.h"
#include "safi/ui/inspector_widgets.h"
#include "safi/core/log.h"

#include <cJSON.h>
#include <microui.h>
#include <math.h>
#include <string.h>
#include <stdio.h>

/* ---- JSON helpers ------------------------------------------------------- */

static cJSON *s_json_vec3(const float v[3]) {
    cJSON *a = cJSON_CreateArray();
    cJSON_AddItemToArray(a, cJSON_CreateNumber((double)v[0]));
    cJSON_AddItemToArray(a, cJSON_CreateNumber((double)v[1]));
    cJSON_AddItemToArray(a, cJSON_CreateNumber((double)v[2]));
    return a;
}
static cJSON *s_json_vec4(const float v[4]) {
    cJSON *a = cJSON_CreateArray();
    for (int i = 0; i < 4; i++)
        cJSON_AddItemToArray(a, cJSON_CreateNumber((double)v[i]));
    return a;
}
static void s_read_vec3(const cJSON *j, float out[3]) {
    if (!j || !cJSON_IsArray(j)) return;
    for (int i = 0; i < 3; i++) {
        cJSON *el = cJSON_GetArrayItem(j, i);
        if (el) out[i] = (float)el->valuedouble;
    }
}
static void s_read_vec4(const cJSON *j, float out[4]) {
    if (!j || !cJSON_IsArray(j)) return;
    for (int i = 0; i < 4; i++) {
        cJSON *el = cJSON_GetArrayItem(j, i);
        if (el) out[i] = (float)el->valuedouble;
    }
}
static float s_read_float(const cJSON *j, float def) {
    return (j && cJSON_IsNumber(j)) ? (float)j->valuedouble : def;
}
static int s_read_int(const cJSON *j, int def) {
    return (j && cJSON_IsNumber(j)) ? j->valueint : def;
}
static bool s_read_bool(const cJSON *j, bool def) {
    if (!j) return def;
    if (cJSON_IsBool(j)) return cJSON_IsTrue(j);
    return def;
}

/* ==== SafiTransform ====================================================== */

static cJSON *ser_transform(ecs_world_t *w, ecs_entity_t e, ecs_id_t id) {
    (void)id;
    const SafiTransform *t = ecs_get(w, e, SafiTransform);
    if (!t) return NULL;
    cJSON *j = cJSON_CreateObject();
    cJSON_AddItemToObject(j, "position", s_json_vec3(t->position));
    cJSON_AddItemToObject(j, "rotation", s_json_vec4(t->rotation));
    cJSON_AddItemToObject(j, "scale",    s_json_vec3(t->scale));
    return j;
}
static void deser_transform(ecs_world_t *w, ecs_entity_t e, const cJSON *j) {
    SafiTransform t = safi_transform_identity();
    s_read_vec3(cJSON_GetObjectItem(j, "position"), t.position);
    s_read_vec4(cJSON_GetObjectItem(j, "rotation"), t.rotation);
    s_read_vec3(cJSON_GetObjectItem(j, "scale"),    t.scale);
    ecs_set_ptr(w, e, SafiTransform, &t);
}
static void insp_transform(mu_Context *ctx, ecs_world_t *w, ecs_entity_t e) {
    SafiTransform *t = ecs_get_mut(w, e, SafiTransform);
    if (mu_header_ex(ctx, "Transform", MU_OPT_EXPANDED)) {
        safi_inspector_property_vec3(ctx, "Position", t->position, 0.1f);
        safi_inspector_property_vec3(ctx, "Rotation", t->rotation, 0.1f);
        safi_inspector_property_vec3(ctx, "Scale",    t->scale, 0.1f);
    }
}

/* ==== SafiCamera ========================================================= */

static cJSON *ser_camera(ecs_world_t *w, ecs_entity_t e, ecs_id_t id) {
    (void)id;
    const SafiCamera *c = ecs_get(w, e, SafiCamera);
    if (!c) return NULL;
    cJSON *j = cJSON_CreateObject();
    cJSON_AddNumberToObject(j, "fov_y_radians", (double)c->fov_y_radians);
    cJSON_AddNumberToObject(j, "z_near",        (double)c->z_near);
    cJSON_AddNumberToObject(j, "z_far",         (double)c->z_far);
    cJSON_AddItemToObject(j, "target",  s_json_vec3(c->target));
    cJSON_AddItemToObject(j, "eye",     s_json_vec3(c->eye));
    cJSON_AddItemToObject(j, "forward", s_json_vec3(c->forward));
    cJSON_AddItemToObject(j, "up",      s_json_vec3(c->up));
    return j;
}
static void deser_camera(ecs_world_t *w, ecs_entity_t e, const cJSON *j) {
    SafiCamera c = {0};
    c.fov_y_radians = s_read_float(cJSON_GetObjectItem(j, "fov_y_radians"), 1.0472f);
    c.z_near = s_read_float(cJSON_GetObjectItem(j, "z_near"), 0.1f);
    c.z_far  = s_read_float(cJSON_GetObjectItem(j, "z_far"), 100.0f);
    s_read_vec3(cJSON_GetObjectItem(j, "target"), c.target);

    const cJSON *jeye = cJSON_GetObjectItem(j, "eye");
    if (jeye) {
        s_read_vec3(jeye, c.eye);
        s_read_vec3(cJSON_GetObjectItem(j, "forward"), c.forward);
        s_read_vec3(cJSON_GetObjectItem(j, "up"),      c.up);
    } else {
        /* Legacy scene: synthesise a pose from the render system's pre-pose
         * convention (eye sits 3 units along +Z from target, look at origin). */
        c.eye[0] = c.target[0];
        c.eye[1] = c.target[1];
        c.eye[2] = c.target[2] + 3.0f;
        float fx = -c.eye[0], fy = -c.eye[1], fz = -c.eye[2];
        float n2 = fx*fx + fy*fy + fz*fz;
        if (n2 > 1e-8f) {
            float inv = 1.0f / sqrtf(n2);
            c.forward[0] = fx * inv;
            c.forward[1] = fy * inv;
            c.forward[2] = fz * inv;
        } else {
            c.forward[0] = 0.0f; c.forward[1] = 0.0f; c.forward[2] = -1.0f;
        }
        c.up[0] = 0.0f; c.up[1] = 1.0f; c.up[2] = 0.0f;
    }
    ecs_set_ptr(w, e, SafiCamera, &c);
}
static void insp_camera(mu_Context *ctx, ecs_world_t *w, ecs_entity_t e) {
    SafiCamera *c = ecs_get_mut(w, e, SafiCamera);
    if (mu_header_ex(ctx, "Camera", MU_OPT_EXPANDED)) {
        safi_inspector_property_float(ctx, "fov (rad)", &c->fov_y_radians, 0.01f);
        safi_inspector_property_float(ctx, "z_near",    &c->z_near, 0.01f);
        safi_inspector_property_float(ctx, "z_far",     &c->z_far, 1.0f);
        safi_inspector_property_vec3(ctx, "target",     c->target, 0.1f);
    }
}

/* ==== SafiActiveCamera (tag) ============================================= */

static cJSON *ser_active_camera(ecs_world_t *w, ecs_entity_t e, ecs_id_t id) {
    (void)id;
    return ecs_has(w, e, SafiActiveCamera) ? cJSON_CreateObject() : NULL;
}
static void deser_active_camera(ecs_world_t *w, ecs_entity_t e, const cJSON *j) {
    (void)j;
    ecs_set(w, e, SafiActiveCamera, {0});
}

/* ==== SafiMeshRenderer =================================================== */

static cJSON *ser_mesh_renderer(ecs_world_t *w, ecs_entity_t e, ecs_id_t id) {
    (void)id;
    const SafiMeshRenderer *mr = ecs_get(w, e, SafiMeshRenderer);
    if (!mr) return NULL;
    cJSON *j = cJSON_CreateObject();
    const char *path = safi_assets_model_path(mr->model);
    cJSON_AddStringToObject(j, "model_path", path ? path : "");
    cJSON_AddBoolToObject(j, "visible", mr->visible);
    return j;
}
static void deser_mesh_renderer(ecs_world_t *w, ecs_entity_t e, const cJSON *j) {
    SafiMeshRenderer mr = {0};
    mr.visible = s_read_bool(cJSON_GetObjectItem(j, "visible"), true);
    const cJSON *mp = cJSON_GetObjectItem(j, "model_path");
    if (mp && cJSON_IsString(mp) && mp->valuestring[0]) {
        /* Load through the asset registry. Use lit pipeline by default.
         * SAFI_ENGINE_SHADER_DIR is defined at compile time for engine code. */
#ifdef SAFI_ENGINE_SHADER_DIR
        mr.model = safi_assets_load_model_lit(mp->valuestring,
                                               SAFI_ENGINE_SHADER_DIR);
#else
        mr.model = safi_assets_load_model_lit(mp->valuestring, "shaders");
#endif
    }
    ecs_set_ptr(w, e, SafiMeshRenderer, &mr);
}
static void insp_mesh_renderer(mu_Context *ctx, ecs_world_t *w, ecs_entity_t e) {
    SafiMeshRenderer *mr = ecs_get_mut(w, e, SafiMeshRenderer);
    if (mu_header_ex(ctx, "MeshRenderer", MU_OPT_EXPANDED)) {
        mu_layout_row(ctx, 1, (int[]){ -1 }, 0);
        const char *mpath = safi_assets_model_path(mr->model);
        char buf[256];
        snprintf(buf, sizeof(buf), "Model: %s",
                 mr->model.id ? (mpath[0] ? mpath : "<code>") : "none");
        mu_label(ctx, buf);
        safi_inspector_property_bool(ctx, "Visible", &mr->visible);
    }
}

/* ==== SafiPrimitive ====================================================== */

static cJSON *ser_primitive(ecs_world_t *w, ecs_entity_t e, ecs_id_t id) {
    (void)id;
    const SafiPrimitive *p = ecs_get(w, e, SafiPrimitive);
    if (!p) return NULL;
    cJSON *j = cJSON_CreateObject();
    cJSON_AddNumberToObject(j, "shape", (double)p->shape);
    cJSON_AddItemToObject(j, "color", s_json_vec4(p->color));
    if (p->texture_path[0])
        cJSON_AddStringToObject(j, "texture_path", p->texture_path);

    switch (p->shape) {
    case SAFI_PRIMITIVE_PLANE:
        cJSON_AddNumberToObject(j, "size", (double)p->dims.plane.size);
        break;
    case SAFI_PRIMITIVE_BOX:
        cJSON_AddItemToObject(j, "half_extents",
                              s_json_vec3(p->dims.box.half_extents));
        break;
    case SAFI_PRIMITIVE_SPHERE:
        cJSON_AddNumberToObject(j, "radius",   (double)p->dims.sphere.radius);
        cJSON_AddNumberToObject(j, "segments", (double)p->dims.sphere.segments);
        cJSON_AddNumberToObject(j, "rings",    (double)p->dims.sphere.rings);
        break;
    case SAFI_PRIMITIVE_CAPSULE:
        cJSON_AddNumberToObject(j, "radius",   (double)p->dims.capsule.radius);
        cJSON_AddNumberToObject(j, "height",   (double)p->dims.capsule.height);
        cJSON_AddNumberToObject(j, "segments", (double)p->dims.capsule.segments);
        cJSON_AddNumberToObject(j, "rings",    (double)p->dims.capsule.rings);
        break;
    }
    return j;
}
static void deser_primitive(ecs_world_t *w, ecs_entity_t e, const cJSON *j) {
    SafiPrimitive p = {0};
    p.shape = (SafiPrimitiveShape)s_read_int(cJSON_GetObjectItem(j, "shape"), 0);
    s_read_vec4(cJSON_GetObjectItem(j, "color"), p.color);
    const cJSON *tp = cJSON_GetObjectItem(j, "texture_path");
    if (tp && cJSON_IsString(tp)) {
        strncpy(p.texture_path, tp->valuestring, sizeof(p.texture_path) - 1);
    }
    switch (p.shape) {
    case SAFI_PRIMITIVE_PLANE:
        p.dims.plane.size = s_read_float(cJSON_GetObjectItem(j, "size"), 1.0f);
        break;
    case SAFI_PRIMITIVE_BOX:
        s_read_vec3(cJSON_GetObjectItem(j, "half_extents"),
                    p.dims.box.half_extents);
        break;
    case SAFI_PRIMITIVE_SPHERE:
        p.dims.sphere.radius   = s_read_float(cJSON_GetObjectItem(j, "radius"), 0.5f);
        p.dims.sphere.segments = s_read_int(cJSON_GetObjectItem(j, "segments"), 24);
        p.dims.sphere.rings    = s_read_int(cJSON_GetObjectItem(j, "rings"), 16);
        break;
    case SAFI_PRIMITIVE_CAPSULE:
        p.dims.capsule.radius   = s_read_float(cJSON_GetObjectItem(j, "radius"), 0.3f);
        p.dims.capsule.height   = s_read_float(cJSON_GetObjectItem(j, "height"), 0.8f);
        p.dims.capsule.segments = s_read_int(cJSON_GetObjectItem(j, "segments"), 16);
        p.dims.capsule.rings    = s_read_int(cJSON_GetObjectItem(j, "rings"), 8);
        break;
    }
    ecs_set_ptr(w, e, SafiPrimitive, &p);
}
static void insp_primitive(mu_Context *ctx, ecs_world_t *w, ecs_entity_t e) {
    SafiPrimitive *p = ecs_get_mut(w, e, SafiPrimitive);
    if (mu_header_ex(ctx, "Primitive", MU_OPT_EXPANDED)) {
        static const char *const shapes[] = { "Plane", "Box", "Sphere", "Capsule" };
        int shape_i = (int)p->shape;
        safi_inspector_property_enum(ctx, "Shape", &shape_i, shapes, 4);
        p->shape = (SafiPrimitiveShape)shape_i;

        switch (p->shape) {
        case SAFI_PRIMITIVE_PLANE:
            safi_inspector_property_float(ctx, "Size", &p->dims.plane.size, 0.1f);
            break;
        case SAFI_PRIMITIVE_BOX:
            safi_inspector_property_vec3(ctx, "HalfExtents",
                                         p->dims.box.half_extents, 0.1f);
            break;
        case SAFI_PRIMITIVE_SPHERE: {
            safi_inspector_property_float(ctx, "Radius",
                                          &p->dims.sphere.radius, 0.1f);
            float seg = (float)p->dims.sphere.segments;
            float rng = (float)p->dims.sphere.rings;
            safi_inspector_property_float(ctx, "Segments", &seg, 1.0f);
            safi_inspector_property_float(ctx, "Rings",    &rng, 1.0f);
            if (seg < 3) seg = 3;
            if (rng < 2) rng = 2;
            p->dims.sphere.segments = (int)seg;
            p->dims.sphere.rings    = (int)rng;
            break;
        }
        case SAFI_PRIMITIVE_CAPSULE: {
            safi_inspector_property_float(ctx, "Radius",
                                          &p->dims.capsule.radius, 0.1f);
            safi_inspector_property_float(ctx, "Height",
                                          &p->dims.capsule.height, 0.1f);
            float seg = (float)p->dims.capsule.segments;
            float rng = (float)p->dims.capsule.rings;
            safi_inspector_property_float(ctx, "Segments", &seg, 1.0f);
            safi_inspector_property_float(ctx, "Rings",    &rng, 1.0f);
            if (seg < 3) seg = 3;
            if (rng < 2) rng = 2;
            p->dims.capsule.segments = (int)seg;
            p->dims.capsule.rings    = (int)rng;
            break;
        }
        }
        safi_inspector_property_color_rgba(ctx, "Color", p->color, 0.05f);
        safi_inspector_property_string(ctx, "Texture", p->texture_path,
                                       (int)sizeof(p->texture_path));
    }
}

/* ==== SafiName =========================================================== */

static cJSON *ser_name(ecs_world_t *w, ecs_entity_t e, ecs_id_t id) {
    (void)id;
    const SafiName *n = ecs_get(w, e, SafiName);
    if (!n || !n->value) return NULL;
    cJSON *j = cJSON_CreateObject();
    cJSON_AddStringToObject(j, "value", n->value);
    return j;
}
/* Deserialize is handled specially by the scene loader (string pool). */

/* ==== SafiSpin =========================================================== */

static cJSON *ser_spin(ecs_world_t *w, ecs_entity_t e, ecs_id_t id) {
    (void)id;
    const SafiSpin *s = ecs_get(w, e, SafiSpin);
    if (!s) return NULL;
    cJSON *j = cJSON_CreateObject();
    cJSON_AddItemToObject(j, "axis", s_json_vec3(s->axis));
    cJSON_AddNumberToObject(j, "speed", (double)s->speed);
    return j;
}
static void deser_spin(ecs_world_t *w, ecs_entity_t e, const cJSON *j) {
    SafiSpin s = {0};
    s_read_vec3(cJSON_GetObjectItem(j, "axis"), s.axis);
    s.speed = s_read_float(cJSON_GetObjectItem(j, "speed"), 1.0f);
    ecs_set_ptr(w, e, SafiSpin, &s);
}
static void insp_spin(mu_Context *ctx, ecs_world_t *w, ecs_entity_t e) {
    SafiSpin *s = ecs_get_mut(w, e, SafiSpin);
    if (mu_header_ex(ctx, "Spin", MU_OPT_EXPANDED)) {
        safi_inspector_property_float(ctx, "speed", &s->speed, 0.1f);
        safi_inspector_property_vec3(ctx, "Axis", s->axis, 0.1f);
    }
}

/* ==== SafiRigidBody ====================================================== */

static cJSON *ser_rigidbody(ecs_world_t *w, ecs_entity_t e, ecs_id_t id) {
    (void)id;
    const SafiRigidBody *rb = ecs_get(w, e, SafiRigidBody);
    if (!rb) return NULL;
    cJSON *j = cJSON_CreateObject();
    cJSON_AddNumberToObject(j, "type",        (double)rb->type);
    cJSON_AddNumberToObject(j, "mass",        (double)rb->mass);
    cJSON_AddNumberToObject(j, "friction",    (double)rb->friction);
    cJSON_AddNumberToObject(j, "restitution", (double)rb->restitution);
    return j;
}
static void deser_rigidbody(ecs_world_t *w, ecs_entity_t e, const cJSON *j) {
    SafiRigidBody rb = {0};
    rb.type        = (SafiBodyType)s_read_int(cJSON_GetObjectItem(j, "type"), 0);
    rb.mass        = s_read_float(cJSON_GetObjectItem(j, "mass"), 1.0f);
    rb.friction    = s_read_float(cJSON_GetObjectItem(j, "friction"), 0.5f);
    rb.restitution = s_read_float(cJSON_GetObjectItem(j, "restitution"), 0.3f);
    ecs_set_ptr(w, e, SafiRigidBody, &rb);
}
static void insp_rigidbody(mu_Context *ctx, ecs_world_t *w, ecs_entity_t e) {
    SafiRigidBody *rb = ecs_get_mut(w, e, SafiRigidBody);
    if (mu_header_ex(ctx, "RigidBody", MU_OPT_EXPANDED)) {
        static const char *const types[] = { "Static", "Dynamic", "Kinematic" };
        int type_i = (int)rb->type;
        safi_inspector_property_enum(ctx, "Type", &type_i, types, 3);
        rb->type = (SafiBodyType)type_i;
        safi_inspector_property_float(ctx, "Mass",        &rb->mass, 0.1f);
        safi_inspector_property_float(ctx, "Friction",    &rb->friction, 0.05f);
        safi_inspector_property_float(ctx, "Restitution", &rb->restitution, 0.05f);
    }
}

/* ==== SafiCollider ======================================================= */

static cJSON *ser_collider(ecs_world_t *w, ecs_entity_t e, ecs_id_t id) {
    (void)id;
    const SafiCollider *c = ecs_get(w, e, SafiCollider);
    if (!c) return NULL;
    cJSON *j = cJSON_CreateObject();
    cJSON_AddNumberToObject(j, "shape", (double)c->shape);
    switch (c->shape) {
    case SAFI_COLLIDER_BOX:
        cJSON_AddItemToObject(j, "half_extents",
                              s_json_vec3(c->box.half_extents));
        break;
    case SAFI_COLLIDER_SPHERE:
        cJSON_AddNumberToObject(j, "radius", (double)c->sphere.radius);
        break;
    }
    return j;
}
static void deser_collider(ecs_world_t *w, ecs_entity_t e, const cJSON *j) {
    SafiCollider c = {0};
    c.shape = (SafiColliderShape)s_read_int(cJSON_GetObjectItem(j, "shape"), 0);
    switch (c.shape) {
    case SAFI_COLLIDER_BOX:
        s_read_vec3(cJSON_GetObjectItem(j, "half_extents"),
                    c.box.half_extents);
        break;
    case SAFI_COLLIDER_SPHERE:
        c.sphere.radius = s_read_float(cJSON_GetObjectItem(j, "radius"), 0.5f);
        break;
    }
    ecs_set_ptr(w, e, SafiCollider, &c);
}
static void insp_collider(mu_Context *ctx, ecs_world_t *w, ecs_entity_t e) {
    SafiCollider *c = ecs_get_mut(w, e, SafiCollider);
    if (mu_header_ex(ctx, "Collider", MU_OPT_EXPANDED)) {
        static const char *const shapes[] = { "Box", "Sphere" };
        int shape_i = (int)c->shape;
        safi_inspector_property_enum(ctx, "Shape", &shape_i, shapes, 2);
        c->shape = (SafiColliderShape)shape_i;
        switch (c->shape) {
        case SAFI_COLLIDER_BOX:
            safi_inspector_property_vec3(ctx, "HalfExtents",
                                         c->box.half_extents, 0.1f);
            break;
        case SAFI_COLLIDER_SPHERE:
            safi_inspector_property_float(ctx, "Radius",
                                          &c->sphere.radius, 0.1f);
            break;
        }
    }
}

/* ==== Lights ============================================================= */

#define LIGHT_SERIALIZER(NAME, TYPE, DIR_FIELD)                               \
static cJSON *ser_##NAME(ecs_world_t *w, ecs_entity_t e, ecs_id_t id) {       \
    (void)id;                                                                  \
    const TYPE *l = ecs_get(w, e, TYPE);                                       \
    if (!l) return NULL;                                                        \
    cJSON *j = cJSON_CreateObject();                                           \
    cJSON_AddItemToObject(j, "color", s_json_vec3(l->color));                  \
    cJSON_AddNumberToObject(j, "intensity", (double)l->intensity);

#define LIGHT_DESERIALIZER(NAME, TYPE)                                         \
static void deser_##NAME(ecs_world_t *w, ecs_entity_t e, const cJSON *j) {     \
    TYPE l = {0};                                                               \
    s_read_vec3(cJSON_GetObjectItem(j, "color"), l.color);                     \
    l.intensity = s_read_float(cJSON_GetObjectItem(j, "intensity"), 1.0f);

/* ---- DirectionalLight ---- */

static cJSON *ser_dir_light(ecs_world_t *w, ecs_entity_t e, ecs_id_t id) {
    (void)id;
    const SafiDirectionalLight *l = ecs_get(w, e, SafiDirectionalLight);
    if (!l) return NULL;
    cJSON *j = cJSON_CreateObject();
    cJSON_AddItemToObject(j, "direction", s_json_vec3(l->direction));
    cJSON_AddItemToObject(j, "color", s_json_vec3(l->color));
    cJSON_AddNumberToObject(j, "intensity", (double)l->intensity);
    return j;
}
static void deser_dir_light(ecs_world_t *w, ecs_entity_t e, const cJSON *j) {
    SafiDirectionalLight l = {0};
    s_read_vec3(cJSON_GetObjectItem(j, "direction"), l.direction);
    s_read_vec3(cJSON_GetObjectItem(j, "color"), l.color);
    l.intensity = s_read_float(cJSON_GetObjectItem(j, "intensity"), 1.0f);
    ecs_set_ptr(w, e, SafiDirectionalLight, &l);
}
static void insp_dir_light(mu_Context *ctx, ecs_world_t *w, ecs_entity_t e) {
    SafiDirectionalLight *l = ecs_get_mut(w, e, SafiDirectionalLight);
    if (mu_header_ex(ctx, "Directional Light", MU_OPT_EXPANDED)) {
        safi_inspector_property_vec3(ctx, "Direction", l->direction, 0.1f);
        safi_inspector_property_vec3(ctx, "Color",     l->color, 0.05f);
        safi_inspector_property_float(ctx, "intensity", &l->intensity, 0.1f);
    }
}

/* ---- PointLight ---- */

static cJSON *ser_point_light(ecs_world_t *w, ecs_entity_t e, ecs_id_t id) {
    (void)id;
    const SafiPointLight *l = ecs_get(w, e, SafiPointLight);
    if (!l) return NULL;
    cJSON *j = cJSON_CreateObject();
    cJSON_AddItemToObject(j, "color", s_json_vec3(l->color));
    cJSON_AddNumberToObject(j, "intensity", (double)l->intensity);
    cJSON_AddNumberToObject(j, "range",     (double)l->range);
    return j;
}
static void deser_point_light(ecs_world_t *w, ecs_entity_t e, const cJSON *j) {
    SafiPointLight l = {0};
    s_read_vec3(cJSON_GetObjectItem(j, "color"), l.color);
    l.intensity = s_read_float(cJSON_GetObjectItem(j, "intensity"), 1.0f);
    l.range     = s_read_float(cJSON_GetObjectItem(j, "range"), 10.0f);
    ecs_set_ptr(w, e, SafiPointLight, &l);
}
static void insp_point_light(mu_Context *ctx, ecs_world_t *w, ecs_entity_t e) {
    SafiPointLight *l = ecs_get_mut(w, e, SafiPointLight);
    if (mu_header_ex(ctx, "Point Light", MU_OPT_EXPANDED)) {
        safi_inspector_property_vec3(ctx, "Color", l->color, 0.05f);
        safi_inspector_property_float(ctx, "intensity", &l->intensity, 0.1f);
        safi_inspector_property_float(ctx, "range",     &l->range, 1.0f);
    }
}

/* ---- SpotLight ---- */

static cJSON *ser_spot_light(ecs_world_t *w, ecs_entity_t e, ecs_id_t id) {
    (void)id;
    const SafiSpotLight *l = ecs_get(w, e, SafiSpotLight);
    if (!l) return NULL;
    cJSON *j = cJSON_CreateObject();
    cJSON_AddItemToObject(j, "color", s_json_vec3(l->color));
    cJSON_AddNumberToObject(j, "intensity",   (double)l->intensity);
    cJSON_AddNumberToObject(j, "range",       (double)l->range);
    cJSON_AddNumberToObject(j, "inner_angle", (double)l->inner_angle);
    cJSON_AddNumberToObject(j, "outer_angle", (double)l->outer_angle);
    return j;
}
static void deser_spot_light(ecs_world_t *w, ecs_entity_t e, const cJSON *j) {
    SafiSpotLight l = {0};
    s_read_vec3(cJSON_GetObjectItem(j, "color"), l.color);
    l.intensity   = s_read_float(cJSON_GetObjectItem(j, "intensity"), 1.0f);
    l.range       = s_read_float(cJSON_GetObjectItem(j, "range"), 10.0f);
    l.inner_angle = s_read_float(cJSON_GetObjectItem(j, "inner_angle"), 0.9f);
    l.outer_angle = s_read_float(cJSON_GetObjectItem(j, "outer_angle"), 0.8f);
    ecs_set_ptr(w, e, SafiSpotLight, &l);
}
static void insp_spot_light(mu_Context *ctx, ecs_world_t *w, ecs_entity_t e) {
    SafiSpotLight *l = ecs_get_mut(w, e, SafiSpotLight);
    if (mu_header_ex(ctx, "Spot Light", MU_OPT_EXPANDED)) {
        safi_inspector_property_vec3(ctx, "Color", l->color, 0.05f);
        safi_inspector_property_float(ctx, "intensity",   &l->intensity, 0.1f);
        safi_inspector_property_float(ctx, "range",       &l->range, 1.0f);
        safi_inspector_property_float(ctx, "inner angle", &l->inner_angle, 0.01f);
        safi_inspector_property_float(ctx, "outer angle", &l->outer_angle, 0.01f);
    }
}

/* ---- RectLight ---- */

static cJSON *ser_rect_light(ecs_world_t *w, ecs_entity_t e, ecs_id_t id) {
    (void)id;
    const SafiRectLight *l = ecs_get(w, e, SafiRectLight);
    if (!l) return NULL;
    cJSON *j = cJSON_CreateObject();
    cJSON_AddItemToObject(j, "color", s_json_vec3(l->color));
    cJSON_AddNumberToObject(j, "intensity", (double)l->intensity);
    cJSON_AddNumberToObject(j, "width",     (double)l->width);
    cJSON_AddNumberToObject(j, "height",    (double)l->height);
    return j;
}
static void deser_rect_light(ecs_world_t *w, ecs_entity_t e, const cJSON *j) {
    SafiRectLight l = {0};
    s_read_vec3(cJSON_GetObjectItem(j, "color"), l.color);
    l.intensity = s_read_float(cJSON_GetObjectItem(j, "intensity"), 1.0f);
    l.width     = s_read_float(cJSON_GetObjectItem(j, "width"), 1.0f);
    l.height    = s_read_float(cJSON_GetObjectItem(j, "height"), 1.0f);
    ecs_set_ptr(w, e, SafiRectLight, &l);
}
static void insp_rect_light(mu_Context *ctx, ecs_world_t *w, ecs_entity_t e) {
    SafiRectLight *l = ecs_get_mut(w, e, SafiRectLight);
    if (mu_header_ex(ctx, "Rect Light", MU_OPT_EXPANDED)) {
        safi_inspector_property_vec3(ctx, "Color", l->color, 0.05f);
        safi_inspector_property_float(ctx, "intensity", &l->intensity, 0.1f);
        safi_inspector_property_float(ctx, "width",     &l->width, 0.1f);
        safi_inspector_property_float(ctx, "height",    &l->height, 0.1f);
    }
}

/* ---- SkyLight ---- */

static cJSON *ser_sky_light(ecs_world_t *w, ecs_entity_t e, ecs_id_t id) {
    (void)id;
    const SafiSkyLight *l = ecs_get(w, e, SafiSkyLight);
    if (!l) return NULL;
    cJSON *j = cJSON_CreateObject();
    cJSON_AddItemToObject(j, "color", s_json_vec3(l->color));
    cJSON_AddNumberToObject(j, "intensity", (double)l->intensity);
    return j;
}
static void deser_sky_light(ecs_world_t *w, ecs_entity_t e, const cJSON *j) {
    SafiSkyLight l = {0};
    s_read_vec3(cJSON_GetObjectItem(j, "color"), l.color);
    l.intensity = s_read_float(cJSON_GetObjectItem(j, "intensity"), 1.0f);
    ecs_set_ptr(w, e, SafiSkyLight, &l);
}
static void insp_sky_light(mu_Context *ctx, ecs_world_t *w, ecs_entity_t e) {
    SafiSkyLight *l = ecs_get_mut(w, e, SafiSkyLight);
    if (mu_header_ex(ctx, "Sky Light", MU_OPT_EXPANDED)) {
        safi_inspector_property_vec3(ctx, "Color", l->color, 0.05f);
        safi_inspector_property_float(ctx, "intensity", &l->intensity, 0.1f);
    }
}

/* ==== Registration ======================================================= */

#define REG(TYPE, SER, DESER, INSP, SERIAL)                                   \
    safi_component_registry_register(&(SafiComponentInfo){                     \
        .id          = ecs_id(TYPE),                                           \
        .name        = #TYPE,                                                  \
        .size        = sizeof(TYPE),                                           \
        .serialize   = SER,                                                    \
        .deserialize = DESER,                                                  \
        .draw        = INSP,                                                   \
        .serializable = SERIAL,                                                \
    })

void safi_register_builtin_component_info(ecs_world_t *world) {
    (void)world;  /* ecs_id(T) macros reference global vars, not world */

    safi_component_registry_init();

    REG(SafiTransform,        ser_transform,      deser_transform,     insp_transform,      true);
    REG(SafiGlobalTransform,  NULL,               NULL,                NULL,                 false);
    REG(SafiCamera,           ser_camera,          deser_camera,        insp_camera,          true);
    REG(SafiActiveCamera,     ser_active_camera,   deser_active_camera, NULL,                 true);
    REG(SafiMeshRenderer,     ser_mesh_renderer,   deser_mesh_renderer, insp_mesh_renderer,   true);
    REG(SafiPrimitive,        ser_primitive,        deser_primitive,     insp_primitive,       true);
    REG(SafiName,             ser_name,             NULL,                NULL,                 true);
    REG(SafiSpin,             ser_spin,             deser_spin,          insp_spin,            true);
    REG(SafiDirectionalLight, ser_dir_light,        deser_dir_light,     insp_dir_light,       true);
    REG(SafiPointLight,       ser_point_light,      deser_point_light,   insp_point_light,     true);
    REG(SafiSpotLight,        ser_spot_light,       deser_spot_light,    insp_spot_light,      true);
    REG(SafiRectLight,        ser_rect_light,       deser_rect_light,    insp_rect_light,      true);
    REG(SafiSkyLight,         ser_sky_light,        deser_sky_light,     insp_sky_light,       true);
    SAFI_LOG_INFO("component_registry: registered %d components (physics deferred)",
                  safi_component_registry_count());
}

/* Called from safi_physics_init after ECS_COMPONENT_DEFINE(SafiRigidBody/SafiCollider). */
void safi_register_physics_component_info(void) {
    REG(SafiRigidBody, ser_rigidbody, deser_rigidbody, insp_rigidbody, true);
    REG(SafiCollider,  ser_collider,  deser_collider,  insp_collider,  true);
    SAFI_LOG_INFO("component_registry: registered physics components (%d total)",
                  safi_component_registry_count());
}

#undef REG
