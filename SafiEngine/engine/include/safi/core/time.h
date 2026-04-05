/**
 * safi/core/time.h — frame timing resource.
 *
 * Exposed as an ECS singleton so systems can read dt via
 * `ecs_singleton_get(world, SafiTime)`.
 */
#ifndef SAFI_CORE_TIME_H
#define SAFI_CORE_TIME_H

#include <stdint.h>

typedef struct SafiTime {
    float    delta;        /* seconds since last frame */
    float    elapsed;      /* seconds since app start  */
    uint64_t frame_count;  /* monotonic frame counter  */
} SafiTime;

#endif /* SAFI_CORE_TIME_H */
