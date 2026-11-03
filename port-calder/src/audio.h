/* Port Calder - audio module interface (software mixer, everything synthesized at startup). */
#pragma once
#include <stdbool.h>

enum {
    SFX_CRASH_LIGHT, SFX_CRASH_HEAVY, SFX_HORN, SFX_HORN_TRUCK, SFX_EXPLOSION, SFX_DOOR, SFX_FOOTSTEP,
    SFX_THUD,        /* pedestrian knocked over / body hit */
    SFX_GLASS, SFX_PICKUP, SFX_CASH, SFX_CHECKPOINT, SFX_MISSION_PASS, SFX_MISSION_FAIL, SFX_STAR_UP,
    SFX_BUSTED, SFX_WASTED, SFX_SPLASH, SFX_JUMP_BONUS, SFX_UI, SFX_COUNTDOWN, SFX_GO, SFX_WHISTLE,
    SFX_N
};

/* Generates every sound and radio station. Opens an SDL audio device only when open_device is true
 * (the caller has already done SDL_InitSubSystem(SDL_INIT_AUDIO) in that case). Returns false if the
 * device could not be opened; all other calls are then harmless no-ops. */
bool audio_init(bool open_device);
void audio_shutdown(void);

/* listener position and its right-hand unit vector (for panning) */
void audio_listener(float x, float y, float z, float rx, float ry, float rz);
/* one-shots. 3D: distance attenuation (full volume within ~6 m, inaudible past ~160 m) and stereo pan */
void audio_play(int sfx, float x, float y, float z, float vol, float pitch);
void audio_play2d(int sfx, float vol, float pitch);

/* continuous layers: call every frame with the current values; they glide smoothly */
void audio_engine(bool on, float rpm, float load, int kind); /* rpm,load 0..1; kind 0 car, 1 sports, 2 truck/bus */
void audio_skid(float amount);                                 /* 0..1 tyre squeal */
void audio_siren(float vol, float pan);                        /* nearest police siren, vol 0..1, pan -1..1 */
void audio_ambience(float city, float wind, float sea);        /* each 0..1 */

/* radio: station -1 = off. Switching plays a short burst of tuning static. */
void audio_radio(int station);
int audio_radio_count(void);
const char *audio_radio_name(int station); /* e.g. "KBAY 88.1 - SYNTHWAVE" (only A-Z 0-9 space . - ! ' allowed) */

void audio_volume(float master, float music);
void audio_mute(bool m);

/* headless check, no device needed: every buffer generated, finite, non-silent, peak <= 1.
 * Prints one summary line and returns the number of failures. */
int audio_selftest(void);
