#include "safi/ecs/ecs.h"
#include "safi/ecs/components.h"
#include "safi/ecs/phases.h"

ecs_entity_t SafiFixedUpdate = 0;

static ecs_entity_t g_variable_pipeline = 0;
static ecs_entity_t g_fixed_pipeline    = 0;
static ecs_entity_t g_render_pipeline   = 0;

ecs_world_t *safi_ecs_create(void) {
    ecs_world_t *world = ecs_init();
    safi_register_builtin_components(world);

    /* Custom fixed-update phase. Intentionally NOT chained via EcsDependsOn
     * to any default phase — that keeps it out of the default flecs
     * pipeline so it can be driven exclusively by the fixed-timestep
     * accumulator in safi_app_tick. */
    SafiFixedUpdate = ecs_new_w_id(world, EcsPhase);

    /* --- Variable-rate pipeline: OnLoad .. PostUpdate --------------------
     * Systems that run once per frame at wall-clock dt. Input pump, camera
     * smoothing, gameplay that reads raw input, and the transform
     * propagation system (step 2) all live here. */
    g_variable_pipeline = ecs_pipeline_init(world, &(ecs_pipeline_desc_t){
        .entity = ecs_entity(world, { .name = "safi_variable_pipeline" }),
        .query.terms = {
            { .id = EcsSystem },
            { .id = ecs_dependson(EcsOnLoad),     .oper = EcsOr },
            { .id = ecs_dependson(EcsPreUpdate),  .oper = EcsOr },
            { .id = ecs_dependson(EcsOnUpdate),   .oper = EcsOr },
            { .id = ecs_dependson(EcsOnValidate), .oper = EcsOr },
            { .id = ecs_dependson(EcsPostUpdate) },
        },
    });

    /* --- Fixed-rate pipeline: SafiFixedUpdate ---------------------------
     * Ticks N times per frame at `SafiApp.fixed_dt`. Only systems on this
     * phase run here; everything else is invisible to this pipeline. */
    g_fixed_pipeline = ecs_pipeline_init(world, &(ecs_pipeline_desc_t){
        .entity = ecs_entity(world, { .name = "safi_fixed_pipeline" }),
        .query.terms = {
            { .id = EcsSystem },
            { .id = ecs_dependson(SafiFixedUpdate) },
        },
    });

    /* --- Render pipeline: PreStore .. OnStore ---------------------------
     * Runs after variable + fixed stages so render reads the final world
     * state. When step 3 lands, the engine's render_system sits here. */
    g_render_pipeline = ecs_pipeline_init(world, &(ecs_pipeline_desc_t){
        .entity = ecs_entity(world, { .name = "safi_render_pipeline" }),
        .query.terms = {
            { .id = EcsSystem },
            { .id = ecs_dependson(EcsPreStore), .oper = EcsOr },
            { .id = ecs_dependson(EcsOnStore) },
        },
    });

    return world;
}

void safi_ecs_destroy(ecs_world_t *world) {
    if (world) ecs_fini(world);
    SafiFixedUpdate     = 0;
    g_variable_pipeline = 0;
    g_fixed_pipeline    = 0;
    g_render_pipeline   = 0;
}

ecs_entity_t safi_ecs_variable_pipeline(void) { return g_variable_pipeline; }
ecs_entity_t safi_ecs_fixed_pipeline(void)    { return g_fixed_pipeline;    }
ecs_entity_t safi_ecs_render_pipeline(void)   { return g_render_pipeline;   }
