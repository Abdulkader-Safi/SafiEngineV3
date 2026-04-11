/*
 * scene.h — Scene construction for the gltf_viewer demo.
 *
 * Owns the "what's in the world" step: loads the glTF model from disk and
 * spawns the model, camera, sun, and sky entities. All handles land in the
 * g_demo module state so systems can reach them without ECS queries.
 */
#ifndef GLTF_VIEWER_SCENE_H
#define GLTF_VIEWER_SCENE_H

#include <safi/safi.h>
#include <stdbool.h>

/* Load the demo model and spawn the scene entities. Returns false if the
 * glTF asset fails to load — the caller should shut the app down and exit. */
bool scene_setup(SafiApp *app);

#endif /* GLTF_VIEWER_SCENE_H */
