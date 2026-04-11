/**
 * safi/core/time.h — frame timing resource.
 *
 * Exposed as an ECS singleton so systems can read dt via
 * `ecs_singleton_get(world, SafiTime)`.
 *
 * Two clocks live here:
 *
 *   - Variable-rate (`delta`, `elapsed`) — wall-clock. Used by input-driven
 *     systems, cameras, animation, anything that should feel smooth per
 *     frame regardless of frame rate.
 *
 *   - Fixed-rate (`fixed_delta`, `fixed_elapsed`, `fixed_overshoot`) — the
 *     `SafiFixedUpdate` phase ticks at this rate. Used by physics and any
 *     deterministic simulation. `fixed_overshoot` is the unconsumed
 *     accumulator remainder in `[0, fixed_delta)`, useful as an
 *     interpolation alpha for rendering between fixed steps.
 */
#ifndef SAFI_CORE_TIME_H
#define SAFI_CORE_TIME_H

#include <stdint.h>

typedef struct SafiTime {
    float    delta;           /* seconds since last frame (wall-clock)      */
    float    elapsed;         /* seconds since app start  (wall-clock)      */
    uint64_t frame_count;     /* monotonic frame counter                    */
    float    fixed_delta;     /* seconds per fixed step   (default 1/60)    */
    float    fixed_elapsed;   /* accumulated fixed-step time                */
    float    fixed_overshoot; /* accumulator remainder in [0, fixed_delta)  */
} SafiTime;

#endif /* SAFI_CORE_TIME_H */
