/**
 * safi/render/assets.h — central asset registry and handle types.
 *
 * All render assets are addressed by a stable 32-bit handle instead of a
 * raw pointer. Handles survive reloads, are safe to serialize into scene
 * files, and let the registry deduplicate loads by path.
 *
 * Handle layout (mirrors SafiSoundHandle):
 *     id = (slot_index + 1) | (generation << 16)
 *     id == 0 is always invalid ("no handle").
 *
 * Lifetimes are refcounted:
 *   - safi_assets_load_*  / safi_assets_register_* acquire a reference.
 *   - safi_assets_acquire_* adds another reference.
 *   - safi_assets_release_* drops one. GPU resources are freed at
 *     refcount = 0 and the slot's generation is bumped so any lingering
 *     handle compares stale.
 *
 * Path-cached kinds (Model, Texture) deduplicate by absolute path: loading
 * the same file twice returns the same handle with refcount incremented.
 * Code-owned kinds (Mesh, Material, Shader) have no path cache — they are
 * `register_*`'d from a pre-built struct and identified only by handle id.
 */
#ifndef SAFI_RENDER_ASSETS_H
#define SAFI_RENDER_ASSETS_H

#include <stdbool.h>
#include <stdint.h>
#include <SDL3/SDL_gpu.h>

#include "safi/render/renderer.h"
#include "safi/render/mesh.h"
#include "safi/render/material.h"
#include "safi/render/model.h"

/* ---- Handle types ------------------------------------------------------- */

typedef struct SafiModelHandle    { uint32_t id; } SafiModelHandle;
typedef struct SafiTextureHandle  { uint32_t id; } SafiTextureHandle;
typedef struct SafiMeshHandle     { uint32_t id; } SafiMeshHandle;
typedef struct SafiMaterialHandle { uint32_t id; } SafiMaterialHandle;
typedef struct SafiShaderHandle   { uint32_t id; } SafiShaderHandle;

static inline bool safi_handle_valid(uint32_t id) { return id != 0; }

/* ---- Lifecycle ---------------------------------------------------------- */

/* Initializes the registry. Captures the renderer pointer used for all
 * GPU uploads and releases. Call once during app startup, after
 * safi_renderer_init. */
bool safi_assets_init(SafiRenderer *r);

/* Drains every slot regardless of refcount, logging a warning per leak.
 * Intended to be the last call before safi_renderer_shutdown. */
void safi_assets_shutdown(void);

/* ---- Project root + path resolution ------------------------------------ *
 *
 * Scene files store asset locations relative to the project root so the
 * same JSON works across machines and checkout paths. Call `set_root`
 * once from `safi_app_init`; absolute paths passed to the load APIs keep
 * working unchanged (they pass through the resolver). */

/* Set the project root directory. NULL clears. Does not validate
 * existence. */
void        safi_assets_set_project_root(const char *abs_root);
const char *safi_assets_project_root(void);

/* If `abs` lives underneath the project root, writes the root-relative
 * suffix (no leading slash) into `out` and returns true. Otherwise
 * copies `abs` verbatim and returns false. */
bool safi_assets_path_to_relative(const char *abs, char *out, size_t cap);

/* Absolute-passthrough: if `in` is already absolute, copy it. Otherwise
 * join it with the project root. Result is written into `out` (NUL-
 * terminated). Falls back to `in` if no root is set. */
void safi_assets_path_resolve(const char *in, char *out, size_t cap);

/* ---- Models (path-cached + refcounted) --------------------------------- */

/* Loads a glTF/GLB with the unlit pipeline. Returns a handle with refcount
 * bumped by 1. Cache hit for previously-loaded paths. shader_dir follows
 * the same convention as safi_model_load (see render/model.h). */
SafiModelHandle safi_assets_load_model(const char *path, const char *shader_dir);

/* Same as above but uses the Blinn-Phong lit pipeline. */
SafiModelHandle safi_assets_load_model_lit(const char *path, const char *shader_dir);

/* Register an already-built SafiModel with the registry. Ownership
 * transfers — the registry will call safi_model_destroy when the refcount
 * reaches zero. No path cache for this variant. */
SafiModelHandle safi_assets_register_model(SafiModel model);

/* Returns a borrowed pointer to the underlying model, or NULL if the
 * handle is stale / invalid / id == 0. */
SafiModel *safi_assets_resolve_model(SafiModelHandle h);

/* Path that was used to load the model, or "" for register_model. */
const char *safi_assets_model_path(SafiModelHandle h);

void safi_assets_acquire_model(SafiModelHandle h);
void safi_assets_release_model(SafiModelHandle h);

/* ---- Textures (path-cached + refcounted) ------------------------------- */

SafiTextureHandle safi_assets_load_texture(const char *path);

/* Upload a pre-decoded RGBA8 pixel buffer (commonly used for 1×1 solid
 * color textures). No path cache — every call creates a fresh slot. */
SafiTextureHandle safi_assets_register_texture_rgba8(const uint8_t *pixels,
                                                     uint32_t width,
                                                     uint32_t height);

SDL_GPUTexture *safi_assets_resolve_texture(SafiTextureHandle h);
const char     *safi_assets_texture_path(SafiTextureHandle h);
void            safi_assets_acquire_texture(SafiTextureHandle h);
void            safi_assets_release_texture(SafiTextureHandle h);

/* ---- Code-owned assets (no path; registered from a built struct) ------- */

SafiMeshHandle     safi_assets_register_mesh    (SafiMesh mesh);
SafiMaterialHandle safi_assets_register_material(SafiMaterial mat);

SafiMesh     *safi_assets_resolve_mesh    (SafiMeshHandle h);
SafiMaterial *safi_assets_resolve_material(SafiMaterialHandle h);

void safi_assets_acquire_mesh    (SafiMeshHandle h);
void safi_assets_release_mesh    (SafiMeshHandle h);
void safi_assets_acquire_material(SafiMaterialHandle h);
void safi_assets_release_material(SafiMaterialHandle h);

/* ---- Hot-reload (stub; file-watch wiring is a separate patch) ---------- */

/* Re-decodes the asset from its cached path and swaps the GPU resource
 * in place. The handle id remains valid. Any component that had already
 * resolved the previous pointer in this frame must re-resolve. Safe to
 * call from the main thread between frames. */
void safi_assets_reload(SafiModelHandle h);
void safi_assets_reload_texture(SafiTextureHandle h);

/* Subscribe to reload events. The callback fires after the swap; use
 * it to refresh thumbnails, reset any derived state, etc. One global
 * subscriber for now; the editor is expected to be the only consumer. */
typedef void (*SafiAssetReloadFn)(uint32_t handle_id, void *ctx);
void safi_assets_on_reload(SafiAssetReloadFn cb, void *ctx);

/* Poll-based hot-reload. Walks every in-use model/texture slot, stats
 * the underlying file, and calls `safi_assets_reload*` when mtime has
 * advanced. Cheap enough to run every frame for a handful of assets;
 * the app ticks it at ~4 Hz via an internal throttle. Safe to call from
 * the main thread only. */
void safi_assets_watch_tick(void);

#endif /* SAFI_RENDER_ASSETS_H */
