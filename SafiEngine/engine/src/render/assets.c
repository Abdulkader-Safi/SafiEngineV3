/*
 * assets.c — central asset registry.
 *
 * Five slot tables (model, texture, mesh, material, shader) each indexed
 * by a packed 32-bit handle. Models and textures are path-cached for
 * dedup; the other three kinds are code-owned (registered from an
 * already-built struct).
 *
 * All GPU releases happen on the main thread through the captured
 * renderer pointer. The registry is single-threaded — callers must not
 * acquire/release a handle from the audio thread.
 */
#include "safi/render/assets.h"
#include "safi/render/shader.h"
#include "safi/core/log.h"

#include <stb_image.h>

#include <SDL3/SDL.h>
#include <stdlib.h>
#include <string.h>

#define MAX_MODELS     256
#define MAX_TEXTURES   512
#define MAX_MESHES     512
#define MAX_MATERIALS  256
#define MAX_SHADERS     64
#define PATH_CAP       256

typedef struct {
    bool        in_use;
    uint16_t    generation;
    int         refcount;
    char        path[PATH_CAP];
    bool        is_lit;
    char        shader_dir[PATH_CAP];
    SafiModel   model;
} ModelSlot;

typedef struct {
    bool             in_use;
    uint16_t         generation;
    int              refcount;
    char             path[PATH_CAP];
    uint32_t         width, height;
    SDL_GPUTexture  *tex;
} TextureSlot;

typedef struct {
    bool      in_use;
    uint16_t  generation;
    int       refcount;
    SafiMesh  mesh;
} MeshSlot;

typedef struct {
    bool          in_use;
    uint16_t      generation;
    int           refcount;
    SafiMaterial  material;
} MaterialSlot;

typedef struct {
    bool      in_use;
    uint16_t  generation;
    int       refcount;
    SDL_GPUShader *shader;
} ShaderSlot;

static struct {
    bool         initialized;
    SafiRenderer *r;

    ModelSlot    models  [MAX_MODELS];
    TextureSlot  textures[MAX_TEXTURES];
    MeshSlot     meshes  [MAX_MESHES];
    MaterialSlot materials[MAX_MATERIALS];
    ShaderSlot   shaders [MAX_SHADERS];

    SafiAssetReloadFn reload_cb;
    void             *reload_ctx;
} S;

/* ---- Handle packing ---------------------------------------------------- */

static inline uint32_t pack_handle(uint32_t index, uint16_t gen) {
    return ((uint32_t)(index + 1)) | ((uint32_t)gen << 16);
}
static inline bool unpack_handle(uint32_t id,
                                 uint32_t *out_index,
                                 uint16_t *out_gen) {
    if (id == 0) return false;
    uint32_t idx1 = id & 0xFFFFu;
    uint16_t gen  = (uint16_t)((id >> 16) & 0xFFFFu);
    if (idx1 == 0) return false;
    *out_index = idx1 - 1;
    *out_gen   = gen;
    return true;
}

#define RESOLVE(TABLE, CAP, h, out_slot)                                     \
    do {                                                                      \
        uint32_t _idx; uint16_t _gen;                                         \
        if (!unpack_handle((h).id, &_idx, &_gen) || _idx >= (CAP))            \
            return (out_slot);                                                \
        if (!S.TABLE[_idx].in_use || S.TABLE[_idx].generation != _gen)        \
            return (out_slot);                                                \
        return &S.TABLE[_idx];                                                \
    } while (0)

static ModelSlot *resolve_model_slot(SafiModelHandle h) {
    uint32_t idx; uint16_t gen;
    if (!unpack_handle(h.id, &idx, &gen) || idx >= MAX_MODELS) return NULL;
    ModelSlot *s = &S.models[idx];
    return (s->in_use && s->generation == gen) ? s : NULL;
}
static TextureSlot *resolve_texture_slot(SafiTextureHandle h) {
    uint32_t idx; uint16_t gen;
    if (!unpack_handle(h.id, &idx, &gen) || idx >= MAX_TEXTURES) return NULL;
    TextureSlot *s = &S.textures[idx];
    return (s->in_use && s->generation == gen) ? s : NULL;
}
static MeshSlot *resolve_mesh_slot(SafiMeshHandle h) {
    uint32_t idx; uint16_t gen;
    if (!unpack_handle(h.id, &idx, &gen) || idx >= MAX_MESHES) return NULL;
    MeshSlot *s = &S.meshes[idx];
    return (s->in_use && s->generation == gen) ? s : NULL;
}
static MaterialSlot *resolve_material_slot(SafiMaterialHandle h) {
    uint32_t idx; uint16_t gen;
    if (!unpack_handle(h.id, &idx, &gen) || idx >= MAX_MATERIALS) return NULL;
    MaterialSlot *s = &S.materials[idx];
    return (s->in_use && s->generation == gen) ? s : NULL;
}

static int alloc_model_slot   (void) { for (int i=0;i<MAX_MODELS;   i++) if (!S.models[i].in_use)    return i; return -1; }
static int alloc_texture_slot (void) { for (int i=0;i<MAX_TEXTURES; i++) if (!S.textures[i].in_use)  return i; return -1; }
static int alloc_mesh_slot    (void) { for (int i=0;i<MAX_MESHES;   i++) if (!S.meshes[i].in_use)    return i; return -1; }
static int alloc_material_slot(void) { for (int i=0;i<MAX_MATERIALS;i++) if (!S.materials[i].in_use) return i; return -1; }

/* ---- Path cache lookups ------------------------------------------------ */

static int find_model_by_path(const char *path, bool is_lit) {
    for (int i = 0; i < MAX_MODELS; i++) {
        if (S.models[i].in_use &&
            S.models[i].is_lit == is_lit &&
            strncmp(S.models[i].path, path, PATH_CAP) == 0)
            return i;
    }
    return -1;
}
static int find_texture_by_path(const char *path) {
    for (int i = 0; i < MAX_TEXTURES; i++) {
        if (S.textures[i].in_use &&
            S.textures[i].path[0] != '\0' &&
            strncmp(S.textures[i].path, path, PATH_CAP) == 0)
            return i;
    }
    return -1;
}

/* ---- Lifecycle --------------------------------------------------------- */

bool safi_assets_init(SafiRenderer *r) {
    if (S.initialized) return true;
    memset(&S, 0, sizeof(S));
    S.r = r;
    S.initialized = true;
    SAFI_LOG_INFO("assets: registry initialized "
                  "(models=%d textures=%d meshes=%d materials=%d)",
                  MAX_MODELS, MAX_TEXTURES, MAX_MESHES, MAX_MATERIALS);
    return true;
}

static void free_model_slot(int i) {
    safi_model_destroy(S.r, &S.models[i].model);
    memset(&S.models[i].model, 0, sizeof(S.models[i].model));
    S.models[i].path[0] = '\0';
    S.models[i].in_use  = false;
    S.models[i].refcount = 0;
    S.models[i].generation = (uint16_t)(S.models[i].generation + 1);
}
static void free_texture_slot(int i) {
    if (S.textures[i].tex)
        SDL_ReleaseGPUTexture(S.r->device, S.textures[i].tex);
    S.textures[i].tex = NULL;
    S.textures[i].path[0] = '\0';
    S.textures[i].in_use  = false;
    S.textures[i].refcount = 0;
    S.textures[i].generation = (uint16_t)(S.textures[i].generation + 1);
}
static void free_mesh_slot(int i) {
    safi_mesh_destroy(S.r, &S.meshes[i].mesh);
    memset(&S.meshes[i].mesh, 0, sizeof(S.meshes[i].mesh));
    S.meshes[i].in_use  = false;
    S.meshes[i].refcount = 0;
    S.meshes[i].generation = (uint16_t)(S.meshes[i].generation + 1);
}
static void free_material_slot(int i) {
    safi_material_destroy(S.r, &S.materials[i].material);
    memset(&S.materials[i].material, 0, sizeof(S.materials[i].material));
    S.materials[i].in_use  = false;
    S.materials[i].refcount = 0;
    S.materials[i].generation = (uint16_t)(S.materials[i].generation + 1);
}

void safi_assets_shutdown(void) {
    if (!S.initialized) return;

    int leaks = 0;
    for (int i = 0; i < MAX_MODELS; i++) if (S.models[i].in_use) {
        if (S.models[i].refcount > 0) {
            SAFI_LOG_WARN("assets: leaked model '%s' (refcount=%d)",
                          S.models[i].path[0] ? S.models[i].path : "<code-owned>",
                          S.models[i].refcount);
            leaks++;
        }
        free_model_slot(i);
    }
    for (int i = 0; i < MAX_TEXTURES; i++) if (S.textures[i].in_use) {
        if (S.textures[i].refcount > 0) {
            SAFI_LOG_WARN("assets: leaked texture '%s' (refcount=%d)",
                          S.textures[i].path[0] ? S.textures[i].path : "<code-owned>",
                          S.textures[i].refcount);
            leaks++;
        }
        free_texture_slot(i);
    }
    for (int i = 0; i < MAX_MESHES; i++) if (S.meshes[i].in_use) {
        if (S.meshes[i].refcount > 0) {
            SAFI_LOG_WARN("assets: leaked mesh #%d (refcount=%d)",
                          i, S.meshes[i].refcount);
            leaks++;
        }
        free_mesh_slot(i);
    }
    for (int i = 0; i < MAX_MATERIALS; i++) if (S.materials[i].in_use) {
        if (S.materials[i].refcount > 0) {
            SAFI_LOG_WARN("assets: leaked material #%d (refcount=%d)",
                          i, S.materials[i].refcount);
            leaks++;
        }
        free_material_slot(i);
    }

    SAFI_LOG_INFO("assets: shutdown (leaks=%d)", leaks);
    S.initialized = false;
    S.r = NULL;
}

/* ---- Models ------------------------------------------------------------ */

static SafiModelHandle load_model_impl(const char *path, const char *shader_dir,
                                        bool is_lit) {
    if (!path || !path[0]) return (SafiModelHandle){0};

    int idx = find_model_by_path(path, is_lit);
    if (idx >= 0) {
        S.models[idx].refcount++;
        SAFI_LOG_INFO("assets: model cache hit '%s' (refcount=%d, %s)",
                      path, S.models[idx].refcount, is_lit ? "lit" : "unlit");
        return (SafiModelHandle){ pack_handle((uint32_t)idx,
                                              S.models[idx].generation) };
    }

    idx = alloc_model_slot();
    if (idx < 0) {
        SAFI_LOG_ERROR("assets: model slot table full");
        return (SafiModelHandle){0};
    }

    ModelSlot *s = &S.models[idx];
    SafiModel m;
    bool ok = is_lit
        ? safi_model_load_lit(S.r, path, shader_dir, &m)
        : safi_model_load    (S.r, path, shader_dir, &m);
    if (!ok) return (SafiModelHandle){0};

    s->model       = m;
    s->in_use      = true;
    s->refcount    = 1;
    s->is_lit      = is_lit;
    strncpy(s->path, path, PATH_CAP - 1); s->path[PATH_CAP - 1] = '\0';
    if (shader_dir) {
        strncpy(s->shader_dir, shader_dir, PATH_CAP - 1);
        s->shader_dir[PATH_CAP - 1] = '\0';
    } else {
        s->shader_dir[0] = '\0';
    }
    SAFI_LOG_INFO("assets: loaded model '%s' (%s)", path, is_lit ? "lit" : "unlit");
    return (SafiModelHandle){ pack_handle((uint32_t)idx, s->generation) };
}

SafiModelHandle safi_assets_load_model(const char *path, const char *shader_dir) {
    return load_model_impl(path, shader_dir, /*is_lit=*/false);
}
SafiModelHandle safi_assets_load_model_lit(const char *path, const char *shader_dir) {
    return load_model_impl(path, shader_dir, /*is_lit=*/true);
}

SafiModelHandle safi_assets_register_model(SafiModel model) {
    int idx = alloc_model_slot();
    if (idx < 0) {
        SAFI_LOG_ERROR("assets: model slot table full (register)");
        return (SafiModelHandle){0};
    }
    ModelSlot *s = &S.models[idx];
    s->model       = model;
    s->in_use      = true;
    s->refcount    = 1;
    s->is_lit      = false;
    s->path[0]     = '\0';
    s->shader_dir[0] = '\0';
    return (SafiModelHandle){ pack_handle((uint32_t)idx, s->generation) };
}

SafiModel *safi_assets_resolve_model(SafiModelHandle h) {
    ModelSlot *s = resolve_model_slot(h);
    return s ? &s->model : NULL;
}

const char *safi_assets_model_path(SafiModelHandle h) {
    ModelSlot *s = resolve_model_slot(h);
    return s ? s->path : "";
}

void safi_assets_acquire_model(SafiModelHandle h) {
    ModelSlot *s = resolve_model_slot(h);
    if (s) s->refcount++;
}
void safi_assets_release_model(SafiModelHandle h) {
    ModelSlot *s = resolve_model_slot(h);
    if (!s) return;
    s->refcount--;
    if (s->refcount <= 0) {
        int idx = (int)(s - S.models);
        SAFI_LOG_INFO("assets: freeing model '%s'",
                      s->path[0] ? s->path : "<code-owned>");
        free_model_slot(idx);
    }
}

/* ---- Textures ---------------------------------------------------------- */

static SDL_GPUTexture *upload_rgba8(SafiRenderer *r, const uint8_t *pixels,
                                     uint32_t w, uint32_t h) {
    SDL_GPUTextureCreateInfo ti = {
        .type                 = SDL_GPU_TEXTURETYPE_2D,
        .format               = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
        .width                = w,
        .height               = h,
        .layer_count_or_depth = 1,
        .num_levels           = 1,
        .sample_count         = SDL_GPU_SAMPLECOUNT_1,
        .usage                = SDL_GPU_TEXTUREUSAGE_SAMPLER,
    };
    SDL_GPUTexture *tex = SDL_CreateGPUTexture(r->device, &ti);
    if (!tex) return NULL;

    uint32_t byte_size = w * h * 4u;
    SDL_GPUTransferBufferCreateInfo tbi = {
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
        .size  = byte_size,
    };
    SDL_GPUTransferBuffer *tb = SDL_CreateGPUTransferBuffer(r->device, &tbi);
    void *mapped = SDL_MapGPUTransferBuffer(r->device, tb, false);
    memcpy(mapped, pixels, byte_size);
    SDL_UnmapGPUTransferBuffer(r->device, tb);

    SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(r->device);
    SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTextureTransferInfo src = {
        .transfer_buffer = tb,
        .pixels_per_row  = w,
        .rows_per_layer  = h,
    };
    SDL_GPUTextureRegion dst = { .texture = tex, .w = w, .h = h, .d = 1 };
    SDL_UploadToGPUTexture(copy, &src, &dst, false);
    SDL_EndGPUCopyPass(copy);
    SDL_SubmitGPUCommandBuffer(cmd);
    SDL_ReleaseGPUTransferBuffer(r->device, tb);
    return tex;
}

SafiTextureHandle safi_assets_load_texture(const char *path) {
    if (!path || !path[0]) return (SafiTextureHandle){0};

    int idx = find_texture_by_path(path);
    if (idx >= 0) {
        S.textures[idx].refcount++;
        SAFI_LOG_INFO("assets: texture cache hit '%s' (refcount=%d)",
                      path, S.textures[idx].refcount);
        return (SafiTextureHandle){ pack_handle((uint32_t)idx,
                                                S.textures[idx].generation) };
    }

    int w = 0, h = 0, n = 0;
    uint8_t *pixels = stbi_load(path, &w, &h, &n, 4);
    if (!pixels) {
        SAFI_LOG_WARN("assets: failed to load texture '%s' (%s)",
                      path, stbi_failure_reason());
        return (SafiTextureHandle){0};
    }
    SDL_GPUTexture *tex = upload_rgba8(S.r, pixels, (uint32_t)w, (uint32_t)h);
    stbi_image_free(pixels);
    if (!tex) return (SafiTextureHandle){0};

    idx = alloc_texture_slot();
    if (idx < 0) {
        SDL_ReleaseGPUTexture(S.r->device, tex);
        SAFI_LOG_ERROR("assets: texture slot table full");
        return (SafiTextureHandle){0};
    }
    TextureSlot *s = &S.textures[idx];
    s->tex      = tex;
    s->width    = (uint32_t)w;
    s->height   = (uint32_t)h;
    s->in_use   = true;
    s->refcount = 1;
    strncpy(s->path, path, PATH_CAP - 1); s->path[PATH_CAP - 1] = '\0';
    SAFI_LOG_INFO("assets: loaded texture '%s' (%dx%d)", path, w, h);
    return (SafiTextureHandle){ pack_handle((uint32_t)idx, s->generation) };
}

SafiTextureHandle safi_assets_register_texture_rgba8(const uint8_t *pixels,
                                                     uint32_t width,
                                                     uint32_t height) {
    SDL_GPUTexture *tex = upload_rgba8(S.r, pixels, width, height);
    if (!tex) return (SafiTextureHandle){0};

    int idx = alloc_texture_slot();
    if (idx < 0) {
        SDL_ReleaseGPUTexture(S.r->device, tex);
        return (SafiTextureHandle){0};
    }
    TextureSlot *s = &S.textures[idx];
    s->tex      = tex;
    s->width    = width;
    s->height   = height;
    s->in_use   = true;
    s->refcount = 1;
    s->path[0]  = '\0';
    return (SafiTextureHandle){ pack_handle((uint32_t)idx, s->generation) };
}

SDL_GPUTexture *safi_assets_resolve_texture(SafiTextureHandle h) {
    TextureSlot *s = resolve_texture_slot(h);
    return s ? s->tex : NULL;
}
const char *safi_assets_texture_path(SafiTextureHandle h) {
    TextureSlot *s = resolve_texture_slot(h);
    return s ? s->path : "";
}
void safi_assets_acquire_texture(SafiTextureHandle h) {
    TextureSlot *s = resolve_texture_slot(h);
    if (s) s->refcount++;
}
void safi_assets_release_texture(SafiTextureHandle h) {
    TextureSlot *s = resolve_texture_slot(h);
    if (!s) return;
    s->refcount--;
    if (s->refcount <= 0) {
        int idx = (int)(s - S.textures);
        free_texture_slot(idx);
    }
}

/* ---- Meshes ------------------------------------------------------------ */

SafiMeshHandle safi_assets_register_mesh(SafiMesh mesh) {
    int idx = alloc_mesh_slot();
    if (idx < 0) {
        SAFI_LOG_ERROR("assets: mesh slot table full");
        return (SafiMeshHandle){0};
    }
    MeshSlot *s = &S.meshes[idx];
    s->mesh     = mesh;
    s->in_use   = true;
    s->refcount = 1;
    return (SafiMeshHandle){ pack_handle((uint32_t)idx, s->generation) };
}
SafiMesh *safi_assets_resolve_mesh(SafiMeshHandle h) {
    MeshSlot *s = resolve_mesh_slot(h);
    return s ? &s->mesh : NULL;
}
void safi_assets_acquire_mesh(SafiMeshHandle h) {
    MeshSlot *s = resolve_mesh_slot(h);
    if (s) s->refcount++;
}
void safi_assets_release_mesh(SafiMeshHandle h) {
    MeshSlot *s = resolve_mesh_slot(h);
    if (!s) return;
    s->refcount--;
    if (s->refcount <= 0) {
        int idx = (int)(s - S.meshes);
        free_mesh_slot(idx);
    }
}

/* ---- Materials --------------------------------------------------------- */

SafiMaterialHandle safi_assets_register_material(SafiMaterial mat) {
    int idx = alloc_material_slot();
    if (idx < 0) {
        SAFI_LOG_ERROR("assets: material slot table full");
        return (SafiMaterialHandle){0};
    }
    MaterialSlot *s = &S.materials[idx];
    s->material = mat;
    s->in_use   = true;
    s->refcount = 1;
    return (SafiMaterialHandle){ pack_handle((uint32_t)idx, s->generation) };
}
SafiMaterial *safi_assets_resolve_material(SafiMaterialHandle h) {
    MaterialSlot *s = resolve_material_slot(h);
    return s ? &s->material : NULL;
}
void safi_assets_acquire_material(SafiMaterialHandle h) {
    MaterialSlot *s = resolve_material_slot(h);
    if (s) s->refcount++;
}
void safi_assets_release_material(SafiMaterialHandle h) {
    MaterialSlot *s = resolve_material_slot(h);
    if (!s) return;
    s->refcount--;
    if (s->refcount <= 0) {
        int idx = (int)(s - S.materials);
        free_material_slot(idx);
    }
}

/* ---- Hot-reload stub --------------------------------------------------- */

void safi_assets_reload(SafiModelHandle h) {
    ModelSlot *s = resolve_model_slot(h);
    if (!s || s->path[0] == '\0') return;   /* can't reload code-owned models */

    SafiModel nm;
    bool ok = s->is_lit
        ? safi_model_load_lit(S.r, s->path, s->shader_dir, &nm)
        : safi_model_load    (S.r, s->path, s->shader_dir, &nm);
    if (!ok) {
        SAFI_LOG_WARN("assets: reload failed for '%s'", s->path);
        return;
    }

    safi_model_destroy(S.r, &s->model);
    s->model = nm;
    SAFI_LOG_INFO("assets: reloaded model '%s'", s->path);
    if (S.reload_cb) S.reload_cb(h.id, S.reload_ctx);
}

void safi_assets_reload_texture(SafiTextureHandle h) {
    TextureSlot *s = resolve_texture_slot(h);
    if (!s || s->path[0] == '\0') return;

    int w = 0, hp = 0, n = 0;
    uint8_t *pixels = stbi_load(s->path, &w, &hp, &n, 4);
    if (!pixels) {
        SAFI_LOG_WARN("assets: texture reload failed '%s' (%s)",
                      s->path, stbi_failure_reason());
        return;
    }
    SDL_GPUTexture *nt = upload_rgba8(S.r, pixels, (uint32_t)w, (uint32_t)hp);
    stbi_image_free(pixels);
    if (!nt) return;

    if (s->tex) SDL_ReleaseGPUTexture(S.r->device, s->tex);
    s->tex    = nt;
    s->width  = (uint32_t)w;
    s->height = (uint32_t)hp;
    SAFI_LOG_INFO("assets: reloaded texture '%s' (%dx%d)", s->path, w, hp);
    if (S.reload_cb) S.reload_cb(h.id, S.reload_ctx);
}

void safi_assets_on_reload(SafiAssetReloadFn cb, void *ctx) {
    S.reload_cb  = cb;
    S.reload_ctx = ctx;
}
