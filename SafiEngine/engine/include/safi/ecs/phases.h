/**
 * safi/ecs/phases.h — engine-specific flecs pipeline phases.
 *
 * SafiEngine runs three distinct stages per frame:
 *
 *   1. Variable-rate  — EcsOnLoad .. EcsPostUpdate, driven by wall-clock dt
 *   2. Fixed-rate     — SafiFixedUpdate, driven by the accumulator
 *   3. Render         — EcsPreStore .. EcsOnStore, driven by wall-clock dt
 *
 * Each stage has its own pipeline, built in `safi_ecs_create`. Systems are
 * assigned to a stage by depending on the appropriate phase. For physics
 * and other deterministic simulation, use SafiFixedUpdate:
 *
 *     ecs_system(world, {
 *         .entity = ecs_entity(world, { .name = "physics_step",
 *                                       .add  = ecs_ids(ecs_dependson(SafiFixedUpdate)) }),
 *         .callback = physics_step,
 *     });
 *
 * The phase is deliberately NOT chained to any default flecs phase, so it
 * stays out of the default pipeline and is driven exclusively by the
 * fixed-timestep accumulator in `safi_app_tick`.
 */
#ifndef SAFI_ECS_PHASES_H
#define SAFI_ECS_PHASES_H

#include <flecs.h>

/* Custom phase for fixed-timestep systems. Set up in safi_ecs_create. */
extern ecs_entity_t SafiFixedUpdate;

/* Engine-owned pipeline handles. Used internally by safi_app_tick; user
 * code normally doesn't touch these, but they're exposed for tests and for
 * apps that want to step individual stages manually. */
ecs_entity_t safi_ecs_variable_pipeline(void);
ecs_entity_t safi_ecs_fixed_pipeline(void);
ecs_entity_t safi_ecs_render_pipeline(void);

#endif /* SAFI_ECS_PHASES_H */
