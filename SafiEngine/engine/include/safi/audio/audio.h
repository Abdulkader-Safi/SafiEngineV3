/**
 * safi/audio/audio.h — cross-platform audio powered by miniaudio.
 *
 * One device, a bus (mixer-group) graph, and handle-based sounds and voices.
 * Loaded sounds are templates; each `safi_audio_play*` spawns a voice — an
 * independently-controllable instance that is reclaimed when playback ends.
 *
 * The subsystem is initialized and shut down automatically by the engine.
 * Audio runs on its own device callback thread (managed by miniaudio);
 * `safi_audio_update` is called once per frame to GC finished voices and
 * publish cached state back to the `SafiAudio` singleton.
 */
#ifndef SAFI_AUDIO_H
#define SAFI_AUDIO_H

#include <stdbool.h>
#include <stdint.h>
#include <flecs.h>

/* ---- Handles ------------------------------------------------------------ *
 * 32-bit packed (index | generation<<16). id == 0 means invalid. */
typedef struct SafiSoundHandle { uint32_t id; } SafiSoundHandle;
typedef struct SafiVoiceHandle { uint32_t id; } SafiVoiceHandle;
typedef struct SafiBusHandle   { uint32_t id; } SafiBusHandle;

#define SAFI_SOUND_INVALID ((SafiSoundHandle){0})
#define SAFI_VOICE_INVALID ((SafiVoiceHandle){0})
#define SAFI_BUS_INVALID   ((SafiBusHandle){0})

/* ---- Load mode ---------------------------------------------------------- */
typedef enum SafiAudioLoadMode {
    SAFI_AUDIO_LOAD_DECODE,   /* fully decode into memory (short sfx) */
    SAFI_AUDIO_LOAD_STREAM,   /* stream from disk (music, large files) */
} SafiAudioLoadMode;

/* ---- ECS singleton ------------------------------------------------------ */
typedef struct SafiAudio {
    bool  device_ok;
    float master_volume;
    float listener_position[3];
    float listener_forward[3];
    float listener_up[3];
    uint32_t active_voices;
    uint32_t loaded_sounds;
} SafiAudio;

/* ---- Lifecycle — called by safi_app_init/shutdown ---------------------- */
bool safi_audio_init(ecs_world_t *world);
void safi_audio_shutdown(void);
void safi_audio_update(float dt);

/* ---- Asset loading ------------------------------------------------------ */
SafiSoundHandle safi_audio_load(const char *path, SafiAudioLoadMode mode);
void            safi_audio_unload(SafiSoundHandle h);

/* ---- Playback ----------------------------------------------------------- */
SafiVoiceHandle safi_audio_play(SafiSoundHandle h, SafiBusHandle bus,
                                float volume, float pitch, bool looping);

SafiVoiceHandle safi_audio_play_3d(SafiSoundHandle h, SafiBusHandle bus,
                                   const float position[3],
                                   float volume, float pitch, bool looping);

/* ---- Voice control ------------------------------------------------------ */
void  safi_audio_stop(SafiVoiceHandle v);
void  safi_audio_set_voice_volume(SafiVoiceHandle v, float volume);
void  safi_audio_set_voice_pitch(SafiVoiceHandle v, float pitch);
void  safi_audio_set_voice_position(SafiVoiceHandle v, const float pos[3]);
bool  safi_audio_voice_is_playing(SafiVoiceHandle v);

/* ---- Buses -------------------------------------------------------------- *
 * Pass `parent = SAFI_BUS_MASTER` (or any other bus handle) to chain. */
SafiBusHandle safi_audio_bus_create(const char *name, SafiBusHandle parent);
void          safi_audio_bus_set_volume(SafiBusHandle b, float volume);
void          safi_audio_bus_set_lowpass(SafiBusHandle b, float cutoff_hz); /* 0 = off */

/* ---- 3D listener -------------------------------------------------------- */
void safi_audio_set_listener(const float position[3],
                             const float forward[3],
                             const float up[3]);

/* ---- Master ------------------------------------------------------------- */
void  safi_audio_set_master_volume(float v);
float safi_audio_get_master_volume(void);

/* ---- Pre-baked buses (valid after safi_audio_init) --------------------- */
SafiBusHandle safi_audio_bus_master(void);
SafiBusHandle safi_audio_bus_music(void);
SafiBusHandle safi_audio_bus_sfx(void);
SafiBusHandle safi_audio_bus_ui(void);

/* ---- Component declaration --------------------------------------------- */
extern ECS_COMPONENT_DECLARE(SafiAudio);

#endif /* SAFI_AUDIO_H */
