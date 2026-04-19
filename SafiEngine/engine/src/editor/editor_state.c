#include "safi/editor/editor_state.h"
#include "safi/ecs/components.h"

SafiEditorMode safi_editor_get_mode(const ecs_world_t *world) {
    const SafiEditorState *s = ecs_singleton_get(world, SafiEditorState);
    return s ? s->mode : SAFI_EDITOR_MODE_EDIT;
}

void safi_editor_set_mode(ecs_world_t *world, SafiEditorMode mode) {
    const SafiEditorState *s = ecs_singleton_get(world, SafiEditorState);
    if (!s) return;
    SafiEditorState next = *s;
    next.mode = mode;
    ecs_singleton_set_ptr(world, SafiEditorState, &next);
}

SafiEditorTool safi_editor_get_tool(const ecs_world_t *world) {
    const SafiEditorState *s = ecs_singleton_get(world, SafiEditorState);
    return s ? s->selected_tool : SAFI_EDITOR_TOOL_SELECT;
}

void safi_editor_set_tool(ecs_world_t *world, SafiEditorTool tool) {
    const SafiEditorState *s = ecs_singleton_get(world, SafiEditorState);
    if (!s) return;
    SafiEditorState next = *s;
    next.selected_tool = tool;
    ecs_singleton_set_ptr(world, SafiEditorState, &next);
}

ecs_entity_t safi_editor_get_selected(const ecs_world_t *world) {
    const SafiEditorState *s = ecs_singleton_get(world, SafiEditorState);
    return s ? s->selected_entity : 0;
}

void safi_editor_set_selected(ecs_world_t *world, ecs_entity_t e) {
    const SafiEditorState *s = ecs_singleton_get(world, SafiEditorState);
    if (!s) return;
    SafiEditorState next = *s;
    next.selected_entity = e;
    ecs_singleton_set_ptr(world, SafiEditorState, &next);
}
