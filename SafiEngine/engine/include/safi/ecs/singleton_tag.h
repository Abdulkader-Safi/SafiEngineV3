/**
 * safi/ecs/singleton_tag.h — enforce "at most one" on marker components.
 *
 * Some tags are conceptually exclusive — `SafiActiveCamera` is the obvious
 * example. When a new holder is set (scene load, snapshot restore, editor
 * camera arbitration, future multi-viewport work), the previous holder must
 * lose the tag. This helper centralises that policy so every code path
 * agrees instead of relying on each deserializer to remember.
 */
#ifndef SAFI_ECS_SINGLETON_TAG_H
#define SAFI_ECS_SINGLETON_TAG_H

#include <flecs.h>

/* Remove `tag_id` from every entity except `holder`. Safe to call on
 * components that currently have zero or one holder; it just becomes a
 * no-op. Fine to call from inside a deserialize callback — the internal
 * query doesn't overlap the caller's iteration. */
void safi_ecs_make_tag_unique(ecs_world_t *world,
                              ecs_id_t     tag_id,
                              ecs_entity_t holder);

#endif /* SAFI_ECS_SINGLETON_TAG_H */
