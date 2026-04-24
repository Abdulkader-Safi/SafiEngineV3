/*
 * audio_system.c — engine glue around miniaudio's ma_engine.
 *
 * Slot tables (sounds, voices) with generation counters so stale handles
 * no-op cleanly. Buses are ma_sound_groups parented to the master bus.
 * Voices are `ma_sound` instances created from a loaded source sound
 * (either a decoded buffer or a streaming data source).
 *
 * miniaudio's device runs on its own thread; ma_engine provides internal
 * locking for ma_sound / ma_sound_group ops, so the public API below is
 * safe to call from the ECS tick.
 */
#include "safi/audio/audio.h"
#include "safi/core/log.h"

#include <miniaudio.h>
#include <stdlib.h>
#include <string.h>

ECS_COMPONENT_DECLARE(SafiAudio);

/* ---- Slot tables -------------------------------------------------------- */

#define MAX_SOUNDS 256
#define MAX_VOICES 128
#define MAX_BUSES   32

typedef struct {
    bool         in_use;
    uint16_t     generation;
    SafiAudioLoadMode mode;
    /* DECODE: fully-loaded data source (ma_sound_init_from_file with no
     *         STREAM flag decodes in-place when we pass DECODE).
     * STREAM: ma_sound_init_from_file with MA_SOUND_FLAG_STREAM. */
    ma_sound    *source;       /* heap-allocated ma_sound used as template */
    char        *path;         /* duped filename (needed for per-voice clones) */
} SoundSlot;

typedef struct {
    bool      in_use;
    uint16_t  generation;
    ma_sound *sound;           /* heap-allocated ma_sound instance */
    bool      is_3d;
} VoiceSlot;

typedef struct {
    bool             in_use;
    uint16_t         generation;
    ma_sound_group  *group;    /* heap-allocated */
    /* Optional lowpass post-effect inserted between group and its parent
     * output. 0 = disabled. */
    ma_lpf_node *lowpass;
    bool         lowpass_attached;
} BusSlot;

static struct {
    bool         initialized;
    ma_engine    engine;
    SoundSlot    sounds[MAX_SOUNDS];
    VoiceSlot    voices[MAX_VOICES];
    BusSlot      buses[MAX_BUSES];
    SafiBusHandle master;
    SafiBusHandle music;
    SafiBusHandle sfx;
    SafiBusHandle ui;
    ecs_world_t *world;

    /* Cached listener pose published to the SafiAudio singleton. */
    float listener_pos[3];
    float listener_fwd[3];
    float listener_up[3];
} S;

/* ---- Handle packing ----------------------------------------------------- */

static inline uint32_t pack_handle(uint32_t index, uint16_t gen) {
    /* index is 0-based internally; encode as (index+1) so id 0 is invalid. */
    return ((uint32_t)(index + 1)) | ((uint32_t)gen << 16);
}
static inline bool unpack_handle(uint32_t id, uint32_t *out_index, uint16_t *out_gen) {
    if (id == 0) return false;
    uint32_t idx1 = id & 0xFFFFu;
    uint16_t gen  = (uint16_t)((id >> 16) & 0xFFFFu);
    if (idx1 == 0) return false;
    *out_index = idx1 - 1;
    *out_gen   = gen;
    return true;
}

static SoundSlot *resolve_sound(SafiSoundHandle h) {
    uint32_t idx; uint16_t gen;
    if (!unpack_handle(h.id, &idx, &gen)) return NULL;
    if (idx >= MAX_SOUNDS) return NULL;
    SoundSlot *s = &S.sounds[idx];
    if (!s->in_use || s->generation != gen) return NULL;
    return s;
}
static VoiceSlot *resolve_voice(SafiVoiceHandle h) {
    uint32_t idx; uint16_t gen;
    if (!unpack_handle(h.id, &idx, &gen)) return NULL;
    if (idx >= MAX_VOICES) return NULL;
    VoiceSlot *v = &S.voices[idx];
    if (!v->in_use || v->generation != gen) return NULL;
    return v;
}
static BusSlot *resolve_bus(SafiBusHandle h) {
    uint32_t idx; uint16_t gen;
    if (!unpack_handle(h.id, &idx, &gen)) return NULL;
    if (idx >= MAX_BUSES) return NULL;
    BusSlot *b = &S.buses[idx];
    if (!b->in_use || b->generation != gen) return NULL;
    return b;
}

static int alloc_sound_slot(void) {
    for (int i = 0; i < MAX_SOUNDS; i++) if (!S.sounds[i].in_use) return i;
    return -1;
}
static int alloc_voice_slot(void) {
    for (int i = 0; i < MAX_VOICES; i++) if (!S.voices[i].in_use) return i;
    return -1;
}
static int alloc_bus_slot(void) {
    for (int i = 0; i < MAX_BUSES; i++) if (!S.buses[i].in_use) return i;
    return -1;
}

/* ---- Bus helpers -------------------------------------------------------- */

static SafiBusHandle create_bus_internal(const char *name, ma_sound_group *parent) {
    int idx = alloc_bus_slot();
    if (idx < 0) { SAFI_LOG_ERROR("audio: bus table full"); return SAFI_BUS_INVALID; }
    BusSlot *b = &S.buses[idx];

    b->group = (ma_sound_group *)calloc(1, sizeof(ma_sound_group));
    ma_result r = ma_sound_group_init(&S.engine, 0, parent, b->group);
    if (r != MA_SUCCESS) {
        SAFI_LOG_ERROR("audio: ma_sound_group_init(%s) failed: %d", name ? name : "?", (int)r);
        free(b->group); b->group = NULL;
        return SAFI_BUS_INVALID;
    }
    b->in_use = true;
    b->generation++;
    b->lowpass = NULL;
    b->lowpass_attached = false;
    return (SafiBusHandle){ pack_handle((uint32_t)idx, b->generation) };
}

/* ---- Lifecycle ---------------------------------------------------------- */

bool safi_audio_init(ecs_world_t *world) {
    if (S.initialized) return true;
    memset(&S, 0, sizeof(S));
    S.world = world;

    ma_engine_config cfg = ma_engine_config_init();
    ma_result r = ma_engine_init(&cfg, &S.engine);
    if (r != MA_SUCCESS) {
        SAFI_LOG_WARN("audio: ma_engine_init failed (%d); audio disabled", (int)r);
        return false;
    }

    ECS_COMPONENT_DEFINE(world, SafiAudio);

    /* Default listener. */
    S.listener_fwd[2] = -1.0f;
    S.listener_up[1]  =  1.0f;

    /* Pre-baked buses. Master is a raw group attached to the engine's
     * endpoint; the other three parent to master. */
    S.master = create_bus_internal("master", NULL);
    if (!S.master.id) { ma_engine_uninit(&S.engine); return false; }

    ma_sound_group *master_grp = S.buses[(S.master.id & 0xFFFF) - 1].group;
    S.music = create_bus_internal("music", master_grp);
    S.sfx   = create_bus_internal("sfx",   master_grp);
    S.ui    = create_bus_internal("ui",    master_grp);

    SafiAudio init = {
        .device_ok     = true,
        .master_volume = ma_engine_get_volume(&S.engine),
    };
    init.listener_forward[2] = -1.0f;
    init.listener_up[1]      =  1.0f;
    ecs_singleton_set_ptr(world, SafiAudio, &init);

    S.initialized = true;
    SAFI_LOG_INFO("audio: miniaudio engine initialized (master/music/sfx/ui buses)");
    return true;
}

void safi_audio_shutdown(void) {
    if (!S.initialized) return;

    /* Tear down voices first, then sounds, then buses — otherwise miniaudio
     * will assert on groups with live children. */
    for (int i = 0; i < MAX_VOICES; i++) {
        if (S.voices[i].in_use && S.voices[i].sound) {
            ma_sound_uninit(S.voices[i].sound);
            free(S.voices[i].sound);
            S.voices[i].sound = NULL;
            S.voices[i].in_use = false;
        }
    }
    for (int i = 0; i < MAX_SOUNDS; i++) {
        if (S.sounds[i].in_use) {
            if (S.sounds[i].source) {
                ma_sound_uninit(S.sounds[i].source);
                free(S.sounds[i].source);
            }
            free(S.sounds[i].path);
            memset(&S.sounds[i], 0, sizeof(S.sounds[i]));
        }
    }
    for (int i = 0; i < MAX_BUSES; i++) {
        if (S.buses[i].in_use && S.buses[i].group) {
            ma_sound_group_uninit(S.buses[i].group);
            free(S.buses[i].group);
            S.buses[i].group = NULL;
            S.buses[i].in_use = false;
        }
    }

    ma_engine_uninit(&S.engine);
    S.initialized = false;
}

void safi_audio_update(float dt) {
    (void)dt;
    if (!S.initialized) return;

    /* GC finished non-looping voices. */
    uint32_t active = 0;
    for (int i = 0; i < MAX_VOICES; i++) {
        VoiceSlot *v = &S.voices[i];
        if (!v->in_use) continue;
        if (v->sound && ma_sound_at_end(v->sound)) {
            ma_sound_uninit(v->sound);
            free(v->sound);
            v->sound = NULL;
            v->in_use = false;
        } else {
            active++;
        }
    }

    uint32_t loaded = 0;
    for (int i = 0; i < MAX_SOUNDS; i++) if (S.sounds[i].in_use) loaded++;

    /* Publish snapshot back to ECS. */
    if (S.world) {
        SafiAudio snap = {
            .device_ok     = true,
            .master_volume = ma_engine_get_volume(&S.engine),
            .active_voices = active,
            .loaded_sounds = loaded,
        };
        memcpy(snap.listener_position, S.listener_pos, sizeof(snap.listener_position));
        memcpy(snap.listener_forward,  S.listener_fwd, sizeof(snap.listener_forward));
        memcpy(snap.listener_up,       S.listener_up,  sizeof(snap.listener_up));
        ecs_singleton_set_ptr(S.world, SafiAudio, &snap);
    }
}

/* ---- Asset loading ------------------------------------------------------ */

SafiSoundHandle safi_audio_load(const char *path, SafiAudioLoadMode mode) {
    if (!S.initialized || !path) return SAFI_SOUND_INVALID;
    int idx = alloc_sound_slot();
    if (idx < 0) { SAFI_LOG_ERROR("audio: sound table full"); return SAFI_SOUND_INVALID; }

    SoundSlot *s = &S.sounds[idx];
    s->source = (ma_sound *)calloc(1, sizeof(ma_sound));
    s->path   = strdup(path);
    s->mode   = mode;

    uint32_t flags = MA_SOUND_FLAG_NO_SPATIALIZATION;
    if (mode == SAFI_AUDIO_LOAD_STREAM) flags |= MA_SOUND_FLAG_STREAM;
    else                                flags |= MA_SOUND_FLAG_DECODE;

    ma_result r = ma_sound_init_from_file(&S.engine, path, flags, NULL, NULL, s->source);
    if (r != MA_SUCCESS) {
        SAFI_LOG_ERROR("audio: failed to load '%s' (%d)", path, (int)r);
        free(s->source); free(s->path);
        memset(s, 0, sizeof(*s));
        return SAFI_SOUND_INVALID;
    }

    s->in_use = true;
    s->generation++;
    return (SafiSoundHandle){ pack_handle((uint32_t)idx, s->generation) };
}

void safi_audio_unload(SafiSoundHandle h) {
    SoundSlot *s = resolve_sound(h);
    if (!s) return;
    if (s->source) { ma_sound_uninit(s->source); free(s->source); }
    free(s->path);
    memset(s, 0, sizeof(*s));
    /* keep generation across the wipe above — restore it */
    s->generation = (uint16_t)((h.id >> 16) + 1);
}

/* ---- Playback ----------------------------------------------------------- */

static SafiVoiceHandle start_voice(SoundSlot *s, SafiBusHandle bus, bool is_3d,
                                   const float pos[3], float volume, float pitch,
                                   bool looping) {
    if (!s) return SAFI_VOICE_INVALID;
    BusSlot *bs = resolve_bus(bus.id ? bus : S.sfx);
    if (!bs) return SAFI_VOICE_INVALID;

    int idx = alloc_voice_slot();
    if (idx < 0) return SAFI_VOICE_INVALID;

    VoiceSlot *v = &S.voices[idx];
    v->sound = (ma_sound *)calloc(1, sizeof(ma_sound));
    v->is_3d = is_3d;

    uint32_t flags = 0;
    if (!is_3d) flags |= MA_SOUND_FLAG_NO_SPATIALIZATION;
    if (s->mode == SAFI_AUDIO_LOAD_STREAM) flags |= MA_SOUND_FLAG_STREAM;
    else                                   flags |= MA_SOUND_FLAG_DECODE;

    /* For streams, re-open the file; for decoded sounds we could use
     * ma_sound_init_copy to share the decoded buffer, but init_from_file
     * is simpler and miniaudio caches the decoded data anyway. */
    ma_result r = ma_sound_init_from_file(&S.engine, s->path, flags, bs->group, NULL, v->sound);
    if (r != MA_SUCCESS) {
        SAFI_LOG_ERROR("audio: voice init failed for '%s' (%d)", s->path, (int)r);
        free(v->sound); v->sound = NULL;
        return SAFI_VOICE_INVALID;
    }

    ma_sound_set_volume(v->sound, volume);
    ma_sound_set_pitch(v->sound, pitch > 0.0f ? pitch : 1.0f);
    ma_sound_set_looping(v->sound, looping ? MA_TRUE : MA_FALSE);
    if (is_3d && pos) {
        ma_sound_set_spatialization_enabled(v->sound, MA_TRUE);
        ma_sound_set_position(v->sound, pos[0], pos[1], pos[2]);
    }
    ma_sound_start(v->sound);

    v->in_use = true;
    v->generation++;
    return (SafiVoiceHandle){ pack_handle((uint32_t)idx, v->generation) };
}

SafiVoiceHandle safi_audio_play(SafiSoundHandle h, SafiBusHandle bus,
                                float volume, float pitch, bool looping) {
    return start_voice(resolve_sound(h), bus, false, NULL, volume, pitch, looping);
}

SafiVoiceHandle safi_audio_play_3d(SafiSoundHandle h, SafiBusHandle bus,
                                   const float position[3],
                                   float volume, float pitch, bool looping) {
    return start_voice(resolve_sound(h), bus, true, position, volume, pitch, looping);
}

/* ---- Voice control ------------------------------------------------------ */

void safi_audio_stop(SafiVoiceHandle v) {
    VoiceSlot *slot = resolve_voice(v);
    if (!slot || !slot->sound) return;
    ma_sound_stop(slot->sound);
    ma_sound_uninit(slot->sound);
    free(slot->sound);
    slot->sound  = NULL;
    slot->in_use = false;
}
void safi_audio_pause(SafiVoiceHandle v) {
    VoiceSlot *slot = resolve_voice(v);
    if (!slot || !slot->sound) return;
    ma_sound_stop(slot->sound);      /* miniaudio's stop = pause; position preserved */
}
void safi_audio_resume(SafiVoiceHandle v) {
    VoiceSlot *slot = resolve_voice(v);
    if (!slot || !slot->sound) return;
    ma_sound_start(slot->sound);
}
void safi_audio_set_voice_volume(SafiVoiceHandle v, float volume) {
    VoiceSlot *s = resolve_voice(v);
    if (s && s->sound) ma_sound_set_volume(s->sound, volume);
}
void safi_audio_set_voice_pitch(SafiVoiceHandle v, float pitch) {
    VoiceSlot *s = resolve_voice(v);
    if (s && s->sound && pitch > 0.0f) ma_sound_set_pitch(s->sound, pitch);
}
void safi_audio_set_voice_position(SafiVoiceHandle v, const float pos[3]) {
    VoiceSlot *s = resolve_voice(v);
    if (s && s->sound && pos) ma_sound_set_position(s->sound, pos[0], pos[1], pos[2]);
}
bool safi_audio_voice_is_playing(SafiVoiceHandle v) {
    VoiceSlot *s = resolve_voice(v);
    return s && s->sound && ma_sound_is_playing(s->sound);
}

/* ---- Buses -------------------------------------------------------------- */

SafiBusHandle safi_audio_bus_create(const char *name, SafiBusHandle parent) {
    BusSlot *p = resolve_bus(parent.id ? parent : S.master);
    if (!p) return SAFI_BUS_INVALID;
    return create_bus_internal(name, p->group);
}
void safi_audio_bus_set_volume(SafiBusHandle b, float volume) {
    BusSlot *s = resolve_bus(b);
    if (s && s->group) ma_sound_group_set_volume(s->group, volume);
}
void safi_audio_bus_set_lowpass(SafiBusHandle b, float cutoff_hz) {
    (void)b; (void)cutoff_hz;
    /* Per-bus LPF routing via ma_lpf_node requires rewiring the node graph
     * (detach bus from parent output, insert LPF, reattach). Left as a
     * follow-up — master/per-voice volume covers the common case for v1. */
}

/* ---- 3D listener -------------------------------------------------------- */

void safi_audio_set_listener(const float position[3],
                             const float forward[3],
                             const float up[3]) {
    if (!S.initialized) return;
    if (position) {
        memcpy(S.listener_pos, position, sizeof(S.listener_pos));
        ma_engine_listener_set_position(&S.engine, 0, position[0], position[1], position[2]);
    }
    if (forward) {
        memcpy(S.listener_fwd, forward, sizeof(S.listener_fwd));
        ma_engine_listener_set_direction(&S.engine, 0, forward[0], forward[1], forward[2]);
    }
    if (up) {
        memcpy(S.listener_up, up, sizeof(S.listener_up));
        ma_engine_listener_set_world_up(&S.engine, 0, up[0], up[1], up[2]);
    }
}

/* ---- Master ------------------------------------------------------------- */

void  safi_audio_set_master_volume(float v) {
    if (S.initialized) ma_engine_set_volume(&S.engine, v);
}
float safi_audio_get_master_volume(void) {
    return S.initialized ? ma_engine_get_volume(&S.engine) : 0.0f;
}

/* ---- Pre-baked buses ---------------------------------------------------- */

SafiBusHandle safi_audio_bus_master(void) { return S.master; }
SafiBusHandle safi_audio_bus_music(void)  { return S.music;  }
SafiBusHandle safi_audio_bus_sfx(void)    { return S.sfx;    }
SafiBusHandle safi_audio_bus_ui(void)     { return S.ui;     }
