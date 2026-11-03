/* Port Calder - audio module.
 * Everything (sound effects, engine/ambience layers, five radio stations) is synthesized in code at
 * startup from a deterministic RNG; no asset files. Mixer runs in the SDL audio callback at 48 kHz.
 * Radio stations are stored as 32 kHz 16-bit stereo loops (memory) and resampled on playback. */
#include "audio.h"
#include <SDL.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define SR 48000          /* device / sfx rate */
#define SSR 32000         /* station storage rate */
#define NVOICE 32
#define NST 5
#define PI_F 3.14159265358979f
#define TAU_F 6.28318530717958f

/* ------------------------------------------------------------------------------------------ */
/* helpers                                                                                     */
/* ------------------------------------------------------------------------------------------ */
typedef struct { uint32_t s; } Rng;
static inline uint32_t rng_u(Rng *r) { uint32_t x = r->s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return r->s = x; }
static inline float rng_f(Rng *r) { return (float)(int32_t)rng_u(r) * (1.0f / 2147483648.0f); }   /* -1..1 */
static inline float rng_01(Rng *r) { return (float)(rng_u(r) >> 8) * (1.0f / 16777216.0f); }       /* 0..1 */

static inline float clampf(float x, float a, float b) { return x < a ? a : (x > b ? b : x); }
/* soft saturator (rational tanh approximation), exactly +-1 beyond |x|=3 */
static inline float sat(float x) {
    if (x > 3.f) x = 3.f; else if (x < -3.f) x = -3.f;
    return x * (27.f + x * x) / (27.f + 9.f * x * x);
}
static inline float mtof(float m) { return 440.f * exp2f((m - 69.f) / 12.f); }

#define TBL 4096
static float g_sin[TBL + 2];
static inline float tsin(float ph) { /* ph in cycles, any value (wrapped) */
    float x = ph - (float)(int)ph; if (x < 0.f) x += 1.f;
    x *= TBL; int i = (int)x; float f = x - (float)i;
    return g_sin[i] + (g_sin[i + 1] - g_sin[i]) * f;
}
#define tsinw tsin

static inline float blep(float t, float dt) {
    if (t < dt) { t /= dt; return t + t - t * t - 1.f; }
    if (t > 1.f - dt) { t = (t - 1.f) / dt; return t * t + t + t + 1.f; }
    return 0.f;
}
static inline float osc_saw(float ph, float dt) { return 2.f * ph - 1.f - blep(ph, dt); }
static inline float osc_sq(float ph, float dt, float pw) {
    float v = ph < pw ? 1.f : -1.f; v += blep(ph, dt);
    float p2 = ph - pw; if (p2 < 0.f) p2 += 1.f; v -= blep(p2, dt); return v;
}

typedef struct { float lo, bd; } Svf;

static double now_s(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (double)t.tv_sec + 1e-9 * (double)t.tv_nsec; }

/* ------------------------------------------------------------------------------------------ */
/* sound effects                                                                               */
/* ------------------------------------------------------------------------------------------ */
typedef struct { float *d; int n; } Sfx;
static Sfx g_sfx[SFX_N];
typedef struct { float *b; int n; Rng r; } Sb;

static Sb sb_new(int id, float dur, uint32_t seed) {
    Sb s; s.n = (int)(dur * SR); s.b = (float *)calloc((size_t)s.n, sizeof(float));
    if (!s.b) s.n = 0;
    s.r.s = seed ? seed : 1u; g_sfx[id].d = s.b; g_sfx[id].n = s.n; return s;
}
static void sb_norm(Sb *s, float peak) {
    float mx = 0.f;
    for (int i = 0; i < s->n; i++) { float a = fabsf(s->b[i]); if (a > mx) mx = a; }
    float g = mx > 1e-9f ? peak / mx : 0.f;
    for (int i = 0; i < s->n; i++) s->b[i] *= g;
    int f = SR / 300; if (f > s->n) f = s->n;               /* 3.3 ms end fade, no click at the tail */
    for (int i = 0; i < f; i++) s->b[s->n - 1 - i] *= (float)i / (float)f;
}

/* pitched voice: freq glides f0 -> f1 (time constant pt), exp decay tau, wave 0 sine 1 saw 2 square 3 tri */
static void s_tone(Sb *s, double t0, double dur, float f0, float f1, float pt, float amp, float atk, float tau,
                   int wave, float lpf) {
    int st = (int)(t0 * SR), len = (int)(dur * SR);
    double ph = 0; float pe = pt > 0 ? 1.f : 0.f, kp = pt > 0 ? expf(-1.f / (pt * SR)) : 0.f;
    float ke = expf(-1.f / (tau * SR)), e = 1.f, lz = 0.f, la = lpf > 0 ? 1.f - expf(-TAU_F * lpf / SR) : 1.f;
    int na = (int)(atk * SR);
    for (int k = 0; k < len; k++) {
        int i = st + k; if (i >= s->n) break; if (i < 0) continue;
        float f = f1 + (f0 - f1) * pe; pe *= kp;
        float dt = f / SR; ph += dt; ph -= (double)(int)ph; float p = (float)ph, v;
        switch (wave) {
        case 0: v = tsin(p); break;
        case 1: v = osc_saw(p, dt); break;
        case 2: v = osc_sq(p, dt, 0.5f); break;
        default: v = 4.f * fabsf(p - 0.5f) - 1.f; break;
        }
        lz += la * (v - lz);
        float a = (k < na) ? (float)k / (float)na : 1.f;
        float rel = (k > len - 240) ? (float)(len - k) / 240.f : 1.f;
        s->b[i] += amp * a * e * rel * lz; e *= ke;
    }
}
/* filtered noise: mode 0 lp, 1 bp, 2 hp, 3 raw. cutoff glides c0 -> c1 (time const ct) */
static void s_noise(Sb *s, double t0, double dur, int mode, float c0, float c1, float ct, float q, float amp,
                    float atk, float tau) {
    int st = (int)(t0 * SR), len = (int)(dur * SR);
    float lo = 0, bd = 0, f = 0.1f, ce = ct > 0 ? 1.f : 0.f, kc = ct > 0 ? expf(-1.f / (ct * SR)) : 0.f;
    float ke = expf(-1.f / (tau * SR)), e = 1.f; int na = (int)(atk * SR);
    for (int k = 0; k < len; k++) {
        int i = st + k; if (i >= s->n) break; if (i < 0) continue;
        if ((k & 7) == 0) { float fc = clampf(c1 + (c0 - c1) * ce, 20.f, 7000.f); f = 2.f * sinf(PI_F * fc / SR); }
        ce *= kc;
        float x = rng_f(&s->r), y;
        if (mode == 3) y = x;
        else {
            lo += f * bd; float hp = x - lo - q * bd; bd += f * hp;
            y = mode == 0 ? lo : (mode == 1 ? bd * sqrtf(q) : hp);
        }
        float a = (k < na) ? (float)k / (float)na : 1.f;
        float rel = (k > len - 240) ? (float)(len - k) / 240.f : 1.f;
        s->b[i] += amp * a * e * rel * y; e *= ke;
    }
}
/* sparse random pops with decaying density */
static void s_crackle(Sb *s, double t0, double dur, float rate, float amp, float tau) {
    int st = (int)(t0 * SR), len = (int)(dur * SR); float pe = 0.f, e = 1.f, ke = expf(-1.f / (tau * SR));
    for (int k = 0; k < len; k++) {
        int i = st + k; if (i >= s->n) break; if (i < 0) continue;
        if (rng_01(&s->r) < rate * e / SR) pe = amp * (0.3f + 0.7f * rng_01(&s->r));
        s->b[i] += pe * rng_f(&s->r); pe *= 0.86f; e *= ke;
    }
}
/* bell with inharmonic partials */
static void s_bell(Sb *s, double t0, float f, float amp, float tau, int inharm) {
    static const float ri[4] = {1.f, 2.756f, 5.404f, 8.93f}, rh[4] = {1.f, 2.f, 3.f, 4.f};
    static const float am[4] = {1.f, 0.38f, 0.14f, 0.06f}, dc[4] = {1.f, 0.55f, 0.3f, 0.15f};
    int st = (int)(t0 * SR);
    for (int p = 0; p < 4; p++) {
        float fp = f * (inharm ? ri[p] : rh[p]); if (fp > 11000.f) break;
        float w = TAU_F * fp / SR, ke = expf(-1.f / (tau * dc[p] * SR)), e = 1.f; int len = (int)(tau * dc[p] * 7 * SR);
        for (int k = 0; k < len; k++) {
            int i = st + k; if (i >= s->n) break; if (i < 0) continue;
            float a = k < 72 ? (float)k / 72.f : 1.f;
            s->b[i] += amp * am[p] * a * e * sinf(w * (float)k); e *= ke;
        }
    }
}

static void sfx_crash_light(void) {
    Sb s = sb_new(SFX_CRASH_LIGHT, 0.55f, 0x1001u);
    s_noise(&s, 0, 0.45, 1, 3000, 900, 0.05f, 0.6f, 1.1f, 0, 0.06f);
    s_noise(&s, 0, 0.1, 3, 0, 0, 0, 1, 0.5f, 0, 0.012f);
    static const float mf[5] = {410, 687, 1130, 1710, 2330};
    for (int i = 0; i < 5; i++) s_tone(&s, 0.004 * i, 0.3, mf[i], mf[i] * 0.98f, 0.1f, 0.10f, 0, 0.09f, 0, 0);
    s_tone(&s, 0, 0.2, 120, 60, 0.03f, 0.6f, 0, 0.06f, 0, 0);
    for (int i = 0; i < 3; i++) s_noise(&s, 0.045 + 0.05 * i, 0.08, 1, 2500, 2500, 0, 0.8f, 0.4f, 0, 0.02f);
    sb_norm(&s, 0.8f);
}
static void sfx_crash_heavy(void) {
    Sb s = sb_new(SFX_CRASH_HEAVY, 1.1f, 0x1002u);
    s_noise(&s, 0, 1.0, 0, 3500, 400, 0.25f, 0.7f, 1.0f, 0, 0.3f);
    s_tone(&s, 0, 0.6, 90, 35, 0.08f, 1.0f, 0.003f, 0.25f, 0, 0);
    static const float mf[5] = {180, 311, 523, 860, 1290};
    for (int i = 0; i < 5; i++) s_tone(&s, 0.02 * i, 0.8, mf[i], mf[i] * 0.97f, 0.2f, 0.18f, 0, 0.35f, 0, 0);
    static const float ht[6] = {0, 0.06f, 0.13f, 0.2f, 0.31f, 0.45f};
    for (int i = 0; i < 6; i++) s_noise(&s, ht[i], 0.15, 1, 1600, 700, 0.04f, 0.7f, 0.7f, 0, 0.04f);
    s_crackle(&s, 0.2, 0.7, 400, 0.5f, 0.3f);
    sb_norm(&s, 0.95f);
}
static void sfx_horn(void) {
    Sb s = sb_new(SFX_HORN, 0.55f, 0x1003u);
    s_tone(&s, 0, 0.55, 428, 415.3f, 0.03f, 0.5f, 0.012f, 1000, 1, 2400);
    s_tone(&s, 0, 0.55, 540, 523.3f, 0.03f, 0.5f, 0.012f, 1000, 1, 2400);
    s_tone(&s, 0, 0.55, 415.3f * 1.004f, 415.3f * 1.004f, 0, 0.2f, 0.012f, 1000, 2, 1800);
    sb_norm(&s, 0.6f);
}
static void sfx_horn_truck(void) {
    Sb s = sb_new(SFX_HORN_TRUCK, 0.95f, 0x1004u);
    s_tone(&s, 0, 0.95, 101, 98, 0.05f, 0.55f, 0.03f, 1000, 1, 650);
    s_tone(&s, 0, 0.95, 126, 123, 0.05f, 0.55f, 0.03f, 1000, 1, 650);
    s_tone(&s, 0, 0.95, 49, 49, 0, 0.35f, 0.03f, 1000, 2, 300);
    s_noise(&s, 0, 0.95, 0, 500, 500, 0, 0.8f, 0.06f, 0.05f, 1000);
    sb_norm(&s, 0.65f);
}
static void sfx_explosion(void) {
    Sb s = sb_new(SFX_EXPLOSION, 2.2f, 0x1005u);
    s_tone(&s, 0, 1.7, 70, 28, 0.35f, 1.0f, 0.004f, 0.9f, 0, 0);
    s_tone(&s, 0, 1.0, 140, 45, 0.12f, 0.6f, 0.002f, 0.35f, 0, 0);
    s_noise(&s, 0, 2.0, 0, 1800, 120, 0.5f, 0.7f, 1.2f, 0, 0.7f);
    s_noise(&s, 0, 0.2, 3, 0, 0, 0, 1, 0.9f, 0, 0.04f);
    s_noise(&s, 0, 1.2, 1, 400, 250, 0.3f, 0.8f, 0.7f, 0.003f, 0.35f);
    s_crackle(&s, 0.12, 2.0, 160, 0.5f, 0.9f);
    s_crackle(&s, 0.05, 0.6, 500, 0.4f, 0.25f);
    sb_norm(&s, 0.97f);
}
static void sfx_door(void) {
    Sb s = sb_new(SFX_DOOR, 0.28f, 0x1006u);
    s_tone(&s, 0, 0.2, 150, 70, 0.02f, 0.8f, 0, 0.05f, 0, 0);
    s_noise(&s, 0, 0.1, 1, 900, 900, 0, 0.8f, 1.0f, 0, 0.02f);
    s_tone(&s, 0.035, 0.05, 2400, 2400, 0, 0.3f, 0, 0.01f, 0, 0);
    s_tone(&s, 0.035, 0.05, 3300, 3300, 0, 0.2f, 0, 0.01f, 0, 0);
    s_tone(&s, 0.062, 0.05, 2900, 2900, 0, 0.06f, 0, 0.008f, 0, 0);
    sb_norm(&s, 0.6f);
}
static void sfx_footstep(void) {
    Sb s = sb_new(SFX_FOOTSTEP, 0.13f, 0x1007u);
    s_noise(&s, 0, 0.12, 0, 2800, 900, 0.03f, 0.9f, 1.3f, 0, 0.02f);
    s_tone(&s, 0, 0.1, 110, 70, 0.02f, 0.3f, 0, 0.03f, 0, 0);
    sb_norm(&s, 0.35f);
}
static void sfx_thud(void) {
    Sb s = sb_new(SFX_THUD, 0.3f, 0x1008u);
    s_tone(&s, 0, 0.28, 95, 45, 0.05f, 1.0f, 0, 0.07f, 0, 0);
    s_noise(&s, 0, 0.15, 0, 500, 500, 0, 0.9f, 0.6f, 0, 0.04f);
    sb_norm(&s, 0.75f);
}
static void sfx_glass(void) {
    Sb s = sb_new(SFX_GLASS, 1.1f, 0x1009u);
    s_noise(&s, 0, 0.3, 2, 3500, 3500, 0, 0.8f, 1.0f, 0, 0.03f);
    static const float nt[3] = {0.05f, 0.12f, 0.2f};
    for (int i = 0; i < 3; i++) s_noise(&s, nt[i], 0.1, 2, 4500, 4500, 0, 0.8f, 0.4f, 0, 0.02f);
    for (int i = 0; i < 36; i++) {
        float f = 2200.f + rng_01(&s.r) * 6000.f, t = rng_01(&s.r) * 0.5f, a = 0.05f + 0.13f * rng_01(&s.r);
        float tau = 0.03f + 0.2f * rng_01(&s.r);
        s_tone(&s, t, tau * 6, f, f, 0, a, 0.001f, tau, 0, 0);
    }
    sb_norm(&s, 0.8f);
}
static void sfx_pickup(void) {
    Sb s = sb_new(SFX_PICKUP, 0.55f, 0x100Au);
    s_bell(&s, 0, 1318.5f, 0.8f, 0.15f, 0); s_bell(&s, 0.09, 1975.5f, 0.9f, 0.22f, 0);
    sb_norm(&s, 0.6f);
}
static void sfx_cash(void) {
    Sb s = sb_new(SFX_CASH, 0.95f, 0x100Bu);
    s_noise(&s, 0, 0.05, 1, 4500, 4500, 0, 1.2f, 0.8f, 0, 0.008f);
    s_noise(&s, 0.055, 0.05, 1, 5200, 5200, 0, 1.2f, 0.6f, 0, 0.008f);
    s_tone(&s, 0, 0.1, 200, 100, 0.02f, 0.4f, 0, 0.03f, 0, 0);
    s_bell(&s, 0.1, 2093.f, 0.7f, 0.35f, 1); s_bell(&s, 0.12, 3136.f, 0.5f, 0.3f, 0);
    sb_norm(&s, 0.6f);
}
static void sfx_checkpoint(void) {
    Sb s = sb_new(SFX_CHECKPOINT, 0.3f, 0x100Cu);
    s_tone(&s, 0, 0.09, 880, 880, 0, 0.6f, 0.003f, 1000, 2, 3000);
    s_tone(&s, 0.075, 0.12, 1174.7f, 1174.7f, 0, 0.6f, 0.003f, 0.08f, 2, 3000);
    sb_norm(&s, 0.5f);
}
static void sfx_mission_pass(void) {
    Sb s = sb_new(SFX_MISSION_PASS, 1.6f, 0x100Du);
    static const float n[4] = {523.25f, 659.25f, 783.99f, 1046.5f};
    for (int i = 0; i < 4; i++) { s_tone(&s, 0.11 * i, 0.4, n[i], n[i], 0, 0.5f, 0.004f, 0.25f, 3, 6000); s_bell(&s, 0.11 * i, n[i] * 2, 0.15f, 0.2f, 0); }
    for (int i = 0; i < 4; i++) s_tone(&s, 0.46, 1.1, n[i], n[i], 0, 0.3f, 0.006f, 0.5f, 3, 6000);
    s_bell(&s, 0.46, 1046.5f, 0.3f, 0.6f, 0); s_bell(&s, 0.46, 1568.f, 0.2f, 0.7f, 0);
    sb_norm(&s, 0.7f);
}
static void sfx_mission_fail(void) {
    Sb s = sb_new(SFX_MISSION_FAIL, 1.5f, 0x100Eu);
    static const float n[4] = {392.f, 311.1f, 261.6f, 196.f};
    static const float t[4] = {0, 0.3f, 0.6f, 0.9f}, d[4] = {0.28f, 0.28f, 0.28f, 0.6f};
    for (int i = 0; i < 4; i++) {
        float f1 = i == 3 ? n[i] * 0.94f : n[i];
        s_tone(&s, t[i], d[i], n[i], f1, 0.5f, 0.45f, 0.01f, 0.5f, 1, 900);
        s_tone(&s, t[i], d[i], n[i] * 1.006f, f1 * 1.006f, 0.5f, 0.35f, 0.01f, 0.5f, 1, 900);
    }
    sb_norm(&s, 0.65f);
}
static void sfx_star_up(void) {
    Sb s = sb_new(SFX_STAR_UP, 0.8f, 0x100Fu);
    static const float f[3] = {987.8f, 1318.5f, 1760.f};
    for (int i = 0; i < 3; i++) { s_tone(&s, 0.09 * i, 0.2, f[i], f[i], 0, 0.5f, 0.003f, 0.09f, 3, 0); s_tone(&s, 0.09 * i + 0.24, 0.2, f[i], f[i], 0, 0.2f, 0.003f, 0.09f, 3, 0); }
    s_bell(&s, 0.18, 1760.f, 0.4f, 0.3f, 0);
    sb_norm(&s, 0.6f);
}
static void sfx_busted(void) {
    Sb s = sb_new(SFX_BUSTED, 1.8f, 0x1010u);
    for (int i = 0; i < 4; i++) { float f = (i & 1) ? 698.5f : 880.f; s_tone(&s, 0.18 * i, 0.17, f, f, 0, 0.35f, 0.004f, 1000, 2, 3000); }
    s_tone(&s, 0.78, 0.9, 80, 40, 0.1f, 1.0f, 0.002f, 0.3f, 0, 0);
    s_noise(&s, 0.78, 0.4, 0, 2000, 300, 0.1f, 0.8f, 0.8f, 0, 0.08f);
    static const float ch[4] = {110.f, 220.f, 261.6f, 329.6f};
    for (int i = 0; i < 4; i++) s_tone(&s, 0.78, 1.0, ch[i], ch[i], 0, 0.3f, 0.01f, 0.5f, 1, 1200);
    sb_norm(&s, 0.85f);
}
static void sfx_wasted(void) {
    Sb s = sb_new(SFX_WASTED, 2.2f, 0x1011u);
    s_tone(&s, 0, 2.0, 55, 35, 0.5f, 1.0f, 0.005f, 1.0f, 0, 0);
    s_tone(&s, 0, 0.5, 100, 40, 0.06f, 1.0f, 0.002f, 0.18f, 0, 0);
    static const float f[3] = {55.f, 58.3f, 82.4f};
    for (int i = 0; i < 3; i++) s_tone(&s, 0, 2.1, f[i], f[i] * 0.9f, 1.0f, 0.3f, 0.05f, 1.0f, 1, 250);
    s_tone(&s, 0.05, 1.6, 220, 110, 0.8f, 0.22f, 0.05f, 0.7f, 1, 500);
    s_noise(&s, 0, 2.0, 0, 600, 80, 0.5f, 0.8f, 0.4f, 0.01f, 0.8f);
    sb_norm(&s, 0.9f);
}
static void sfx_splash(void) {
    Sb s = sb_new(SFX_SPLASH, 0.9f, 0x1012u);
    s_noise(&s, 0, 0.8, 1, 2500, 800, 0.15f, 0.7f, 1.0f, 0.01f, 0.22f);
    s_noise(&s, 0, 0.5, 2, 3000, 3000, 0, 0.8f, 0.3f, 0, 0.1f);
    s_tone(&s, 0, 0.3, 100, 60, 0.05f, 0.5f, 0, 0.1f, 0, 0);
    for (int i = 0; i < 14; i++) {
        float f = 300.f + rng_01(&s.r) * 500.f;
        s_tone(&s, 0.05 + rng_01(&s.r) * 0.5, 0.08, f, f * 2.2f, 0.015f, 0.2f * rng_01(&s.r), 0.002f, 0.03f, 0, 0);
    }
    sb_norm(&s, 0.75f);
}
static void sfx_jump_bonus(void) {
    Sb s = sb_new(SFX_JUMP_BONUS, 1.3f, 0x1013u);
    static const float n[5] = {523.25f, 659.25f, 783.99f, 1046.5f, 1318.5f};
    for (int i = 0; i < 5; i++) s_tone(&s, 0.09 * i, 0.12, n[i], n[i], 0, 0.35f, 0.003f, 0.12f, 2, 4500);
    static const float c[3] = {783.99f, 1046.5f, 1318.5f};
    for (int i = 0; i < 3; i++) { s_tone(&s, 0.45, 0.8, c[i], c[i], 0, 0.3f, 0.005f, 0.6f, 2, 4500); s_tone(&s, 0.45, 0.8, c[i], c[i], 0, 0.15f, 0.005f, 0.6f, 3, 0); }
    s_bell(&s, 0.45, 2093.f, 0.25f, 0.5f, 0); s_bell(&s, 0.5, 2637.f, 0.2f, 0.5f, 0);
    sb_norm(&s, 0.75f);
}
static void sfx_ui(void) {
    Sb s = sb_new(SFX_UI, 0.07f, 0x1014u);
    s_tone(&s, 0, 0.06, 1800, 1200, 0.01f, 0.7f, 0, 0.012f, 0, 0);
    s_noise(&s, 0, 0.02, 2, 4000, 4000, 0, 0.8f, 0.3f, 0, 0.004f);
    sb_norm(&s, 0.45f);
}
static void sfx_countdown(void) {
    Sb s = sb_new(SFX_COUNTDOWN, 0.28f, 0x1015u);
    s_tone(&s, 0, 0.25, 660, 660, 0, 0.8f, 0.004f, 1000, 0, 0);
    s_tone(&s, 0, 0.25, 1320, 1320, 0, 0.2f, 0.004f, 1000, 0, 0);
    s_tone(&s, 0, 0.25, 660, 660, 0, 0.2f, 0.004f, 1000, 3, 0);
    sb_norm(&s, 0.55f);
}
static void sfx_go(void) {
    Sb s = sb_new(SFX_GO, 0.55f, 0x1016u);
    s_tone(&s, 0, 0.5, 1320, 1320, 0, 0.8f, 0.004f, 0.5f, 0, 0);
    s_tone(&s, 0, 0.5, 1980, 1980, 0, 0.25f, 0.004f, 0.4f, 0, 0);
    s_tone(&s, 0, 0.5, 1320, 1320, 0, 0.2f, 0.004f, 0.5f, 2, 3500);
    sb_norm(&s, 0.6f);
}
static void sfx_whistle(void) {
    Sb s = sb_new(SFX_WHISTLE, 0.75f, 0x1017u);
    double ph = 0; Svf sv = {0, 0};
    for (int k = 0; k < s.n; k++) {
        float t = (float)k / SR;
        float env = clampf(t / 0.02f, 0, 1) * (t < 0.55f ? 1.f : expf(-(t - 0.55f) / 0.05f));
        float trill = (t < 0.4f) ? 1.f : 0.f;
        float f = 2850.f + trill * 110.f * tsinw(24.f * t) + 12.f * tsinw(6.f * t);
        ph += f / SR; ph -= (double)(int)ph;
        float v = tsin((float)ph) + 0.25f * tsin((float)fmod(ph * 2, 1.0));
        float fq = 2.f * sinf(PI_F * 3000.f / SR);
        sv.lo += fq * sv.bd; float hp = rng_f(&s.r) - sv.lo - 0.4f * sv.bd; sv.bd += fq * hp;
        s.b[k] = env * (0.7f * v + 0.25f * sv.bd * 0.6f) * (trill > 0 ? (0.8f + 0.2f * tsinw(24.f * t)) : 1.f);
    }
    sb_norm(&s, 0.55f);
}

static void gen_sfx(void) {
    sfx_crash_light(); sfx_crash_heavy(); sfx_horn(); sfx_horn_truck(); sfx_explosion(); sfx_door(); sfx_footstep();
    sfx_thud(); sfx_glass(); sfx_pickup(); sfx_cash(); sfx_checkpoint(); sfx_mission_pass(); sfx_mission_fail();
    sfx_star_up(); sfx_busted(); sfx_wasted(); sfx_splash(); sfx_jump_bonus(); sfx_ui(); sfx_countdown(); sfx_go();
    sfx_whistle();
}

/* ------------------------------------------------------------------------------------------ */
/* radio stations: procedural composition + synthesis                                          */
/* ------------------------------------------------------------------------------------------ */
typedef struct { int16_t *pcm; int n; const char *name; } Station;
static Station g_st[NST] = {
    {0, 0, "KBAY 88.1 - SYNTHWAVE"}, {0, 0, "KMUD 94.7 - LOFI HIP HOP"}, {0, 0, "KRAW 99.3 - PUNK ROCK"},
    {0, 0, "KSUN 104.5 - REGGAE AND DUB"}, {0, 0, "KRSH 107.7 - DRUM AND BASS"}};

typedef struct { double st; double swing; int bars; } Clk;       /* st = 16th note seconds */
typedef struct { float *l, *r, *s; int n; Rng rng; } Mx;         /* stereo bus + effect send */

static double tt(const Clk *c, int bar, int step) {
    double t = (double)(bar * 16 + step) * c->st; if (step & 1) t += c->swing * c->st; return t;
}
static bool mx_init(Mx *m, const Clk *c, uint32_t seed) {
    m->n = (int)((double)c->bars * 16.0 * c->st * SSR + 0.5);
    m->l = (float *)calloc(3 * (size_t)m->n, sizeof(float));
    if (!m->l) return false;
    m->r = m->l + m->n; m->s = m->r + m->n; m->rng.s = seed; return true;
}
static inline void pan2(float p, float *gl, float *gr) { float a = (p + 1.f) * 0.25f * PI_F; *gl = cosf(a); *gr = sinf(a); }
static inline int stidx(int n, double t0) { int s = (int)floor(t0 * SSR + 0.5) % n; return s < 0 ? s + n : s; }
static inline int wrapi(const Mx *m, int i) { while (i >= m->n) i -= m->n; return i; }

typedef struct {
    int wave, nosc; float det, pw;            /* 0 saw 1 pulse 2 tri 3 sine */
    float a, d, s, r;
    float c0, c1, ctau;                       /* lowpass cutoff glide (c0<=0: none) */
    float drive, vol, pan, send, vib, vibd, sub;
} Inst;

static inline __attribute__((always_inline)) void note_impl(Mx *m, const Inst *in, double t0, double dur, float freq, const int wave) {
    int n = m->n, st = stidx(n, t0), nd = (int)(dur * SSR); if (nd < 1) nd = 1;
    int no = in->nosc < 1 ? 1 : (in->nosc > 3 ? 3 : in->nosc);
    float dm = exp2f(in->det / 1200.f), mult[3] = {1.f, 1.f, 1.f};
    if (no == 2) { mult[0] = 1.f / dm; mult[1] = dm; } else if (no == 3) { mult[1] = 1.f / dm; mult[2] = dm; }
    float ph[3], dt[3], base[3];
    for (int j = 0; j < no; j++) { ph[j] = rng_01(&m->rng); base[j] = freq * mult[j] / SSR; dt[j] = base[j]; }
    float nrm = no == 1 ? 1.f : (no == 2 ? 0.7f : 0.55f);
    float gl, gr; pan2(in->pan, &gl, &gr); gl *= in->vol; gr *= in->vol;
    int na = (int)(in->a * SSR); float ka = na > 0 ? 1.f / (float)na : 1.f;
    float kd = in->d > 0 ? expf(-1.f / (in->d * SSR)) : 0.f, kr = expf(-1.f / (fmaxf(in->r, 0.002f) * SSR));
    float e = na > 0 ? 0.f : 1.f, sus = in->s;
    int filt = in->c0 > 0 || in->c1 > 0;
    float ce = in->ctau > 0 ? 1.f : 0.f, ctk = in->ctau > 0 ? expf(-32.f / (in->ctau * SSR)) : 0.f;
    float g = 1.f, z1 = 0, z2 = 0, subph = 0, vph = rng_01(&m->rng), pw = in->pw > 0 ? in->pw : 0.5f;
    int maxk = nd + (int)(in->r * 6.f * SSR) + 8; if (maxk > n - 1) maxk = n - 1;
    for (int k = 0; k < maxk; k++) {
        if ((k & 31) == 0) {
            if (filt) { float fc = clampf(in->c1 + (in->c0 - in->c1) * ce, 30.f, 0.45f * SSR); ce *= ctk; g = 1.f - expf(-TAU_F * fc / SSR); }
            if (in->vibd > 0) {
                float vs = tsinw(vph) * in->vibd * 0.0005776f * clampf((float)k / (0.25f * SSR), 0.f, 1.f);
                vph += 32.f * in->vib / SSR;
                for (int j = 0; j < no; j++) dt[j] = base[j] * (1.f + vs);
            }
        }
        if (k < nd) { if (k < na) e = (float)(k + 1) * ka; else e = sus + (e - sus) * kd; }
        else { e *= kr; if (e < 0.008f) break; }
        float v = 0.f;
        for (int j = 0; j < no; j++) {
            float p = ph[j];
            if (wave == 0) v += osc_saw(p, dt[j]);
            else if (wave == 1) v += osc_sq(p, dt[j], pw);
            else if (wave == 2) v += 4.f * fabsf(p - 0.5f) - 1.f;
            else v += tsin(p);
            p += dt[j]; if (p >= 1.f) p -= 1.f; ph[j] = p;
        }
        v *= nrm;
        if (in->sub > 0) { v += in->sub * tsin(subph); subph += 0.5f * dt[0]; if (subph >= 1.f) subph -= 1.f; }
        if (in->drive > 0) v = sat(v * in->drive);
        if (filt) { z1 += g * (v - z1); z2 += g * (z1 - z2); v = z2; }
        v *= e;
        int i = wrapi(m, st + k);
        m->l[i] += v * gl; m->r[i] += v * gr; m->s[i] += v * in->send;
    }
}

static void note(Mx *m, const Inst *in, double t0, double dur, float freq) {
    switch (in->wave) {
    case 0: note_impl(m, in, t0, dur, freq, 0); break;
    case 1: note_impl(m, in, t0, dur, freq, 1); break;
    case 2: note_impl(m, in, t0, dur, freq, 2); break;
    default: note_impl(m, in, t0, dur, freq, 3); break;
    }
}

static void kick(Mx *m, double t0, float vol, float f0, float f1, float ptau, float tau, float click) {
    int n = m->n, st = stidx(n, t0), len = (int)(tau * 6.f * SSR); if (len > n - 1) len = n - 1;
    float pe = 1.f, kp = expf(-1.f / (ptau * SSR)), ke = expf(-1.f / (tau * SSR)), e = 1.f, ph = 0.f;
    int nc = (int)(0.004f * SSR);
    for (int k = 0; k < len; k++) {
        float f = f1 + (f0 - f1) * pe; pe *= kp; ph += f / SSR; ph -= (float)(int)ph;
        if (e < 0.015f) break;
        float v = tsin(ph) * e; e *= ke;
        if (k < nc) v += click * rng_f(&m->rng) * (1.f - (float)k / (float)nc);
        v = sat(v * 1.4f) * vol; int i = wrapi(m, st + k);
        m->l[i] += v * 0.7071f; m->r[i] += v * 0.7071f; m->s[i] += v * 0.03f;
    }
}
static void snare(Mx *m, double t0, float vol, float tone, float ttau, float nb, float ntau, float send, float pan) {
    int n = m->n, st = stidx(n, t0); float mt = ttau > ntau ? ttau : ntau;
    int len = (int)(mt * 6.f * SSR); if (len > n - 1) len = n - 1;
    float ke_t = expf(-1.f / (ttau * SSR)), ke_n = expf(-1.f / (ntau * SSR)), et = 1.f, en = 1.f, ph = 0.f, lz = 0.f, pe = 1.f;
    float kp = expf(-1.f / (0.015f * SSR)), gl, gr; pan2(pan, &gl, &gr);
    for (int k = 0; k < len; k++) {
        float f = tone * (1.f + 0.35f * pe); pe *= kp; ph += f / SSR; ph -= (float)(int)ph;
        if (et < 0.004f && en < 0.004f) break;
        lz += nb * (rng_f(&m->rng) - lz);
        float v = (0.7f * tsin(ph) * et + lz * en) * vol; et *= ke_t; en *= ke_n;
        int i = wrapi(m, st + k);
        m->l[i] += v * gl; m->r[i] += v * gr; m->s[i] += v * send;
    }
}
static void hat(Mx *m, double t0, float vol, float tau, float pan, float bright, float send) {
    int n = m->n, st = stidx(n, t0), len = (int)(tau * 5.f * SSR); if (len > n - 1) len = n - 1;
    float ke = expf(-1.f / (tau * SSR)), e = 1.f, lz = 0.f, l2 = 0.f, gl, gr; pan2(pan, &gl, &gr);
    for (int k = 0; k < len; k++) {
        if (e < 0.004f) break;
        float x = rng_f(&m->rng); lz += 0.3f * (x - lz); float y = x - lz;
        l2 += bright * (y - l2); float v = l2 * e * vol; e *= ke;
        int i = wrapi(m, st + k);
        m->l[i] += v * gl; m->r[i] += v * gr; m->s[i] += v * send;
    }
}
static void riser(Mx *m, double t0, double dur, float vol) {
    int n = m->n, st = stidx(n, t0), len = (int)(dur * SSR); if (len > n - 1) len = n - 1;
    float lz = 0;
    for (int k = 0; k < len; k++) {
        float u = (float)k / (float)len, a = 0.03f + 0.75f * u * u;
        lz += a * (rng_f(&m->rng) - lz);
        float v = lz * u * u * vol; int i = wrapi(m, st + k);
        m->l[i] += v; m->r[i] += v * 0.95f; m->s[i] += v * 0.3f;
    }
}
/* FM electric piano (tine + tremolo/autopan) */
static void rhodes(Mx *m, double t0, double dur, float freq, float vol, float pan, float send, float bright) {
    int n = m->n, st = stidx(n, t0);
    int maxk = (int)((dur + 0.6) * SSR); if (maxk > n - 1) maxk = n - 1;
    int nd = (int)(dur * SSR);
    float pc = rng_01(&m->rng), pm = 0.f, dt = freq / SSR, e = 1.f, ke = expf(-1.f / (1.1f * SSR)), kr = expf(-1.f / (0.12f * SSR));
    float ie = 1.f, ki = expf(-1.f / (0.22f * SSR)), te = 1.f, kt = expf(-1.f / (0.02f * SSR)), lph = rng_01(&m->rng);
    float tp = 0.f, lf = 0.f;
    for (int k = 0; k < maxk; k++) {
        float idx = (0.25f + bright * ie) * 0.32f;                       /* cycles of phase modulation */
        float mod = tsin(pm); pm += dt; if (pm >= 1.f) pm -= 1.f;
        float v = tsinw(pc + idx * mod); pc += dt; if (pc >= 1.f) pc -= 1.f;
        if (te > 0.01f) { tp += 14.f * dt; tp -= (float)(int)tp; v += 0.05f * tsin(tp) * te; }
        float env = e; if (k < 100) env *= (float)k / 100.f;
        if (k >= nd) { e *= kr; if (e < 0.008f) break; } else e *= ke;
        ie *= ki; te *= kt;
        if ((k & 7) == 0) { lph += 8.f * 4.8f / SSR; lf = tsin(lph); }
        v *= env * (1.f + 0.16f * lf);
        float p = pan + 0.35f * lf, gl = 0.7071f * (1.f - 0.7f * p), gr = 0.7071f * (1.f + 0.7f * p);
        int i = wrapi(m, st + k);
        m->l[i] += v * vol * gl; m->r[i] += v * vol * gr; m->s[i] += v * vol * send;
    }
}

typedef struct {
    float kf0, kf1, kpt, ktau, kclick, kvol;
    float stone, sttau, sbright, sntau, svol, ssend;
    float hvol, htau, hbright, hpan, hsend, ovol, otau;
} Kit;
static inline float lvl(char c) { return c == 'X' ? 1.2f : c == 'x' ? 1.f : c == 'o' ? 0.45f : 0.f; }
static void drum_bar(Mx *m, const Clk *c, int bar, const Kit *k, const char *K, const char *S, const char *H,
                     const char *O, float hum) {
    for (int s = 0; s < 16; s++) {
        double t = tt(c, bar, s);
        float v;
        if ((v = lvl(K[s])) > 0) kick(m, t + hum * rng_f(&m->rng), k->kvol * v, k->kf0, k->kf1, k->kpt, k->ktau, k->kclick);
        if ((v = lvl(S[s])) > 0) snare(m, t + hum * rng_f(&m->rng), k->svol * v, k->stone, k->sttau, k->sbright, k->sntau, k->ssend, 0.f);
        if ((v = lvl(H[s])) > 0) hat(m, t + hum * rng_f(&m->rng), k->hvol * v * (0.8f + 0.4f * rng_01(&m->rng)), k->htau, ((s >> 1) & 1) ? k->hpan : -k->hpan, k->hbright, k->hsend);
        if ((v = lvl(O[s])) > 0) hat(m, t, k->ovol * v, k->otau, 0.2f, k->hbright, k->hsend);
    }
}

typedef struct { int root; int n[4]; } Ch;
typedef short Ph[3];   /* step, midi, length in steps; length 0 ends the list */
static void phrase(Mx *m, const Inst *in, const Clk *c, int bar0, const Ph *p, int tr) {
    for (; (*p)[2] > 0; p++) {
        int step = (*p)[0];
        note(m, in, tt(c, bar0 + step / 16, step % 16), (*p)[2] * c->st * 0.95, mtof((float)((*p)[1] + tr)));
    }
}

/* ----- post effects (circular: primed on the end of the loop so the seam is seamless) ----- */
static void fx_delay(Mx *m, int dl, int dr, float fb, float wet, float lpc) {
    float *bl = (float *)calloc((size_t)dl, 4), *br = (float *)calloc((size_t)dr, 4);
    if (!bl || !br) { free(bl); free(br); return; }
    int il = 0, ir = 0, n = m->n, prime = 10 * SSR; if (prime > n) prime = n;
    float zl = 0, zr = 0;
    for (int pass = 0; pass < 2; pass++) {
        int i0 = pass == 0 ? n - prime : 0, i1 = n;
        for (int i = i0; i < i1; i++) {
            float in = m->s[i], yl = bl[il], yr = br[ir];
            zl += lpc * (yl - zl); zr += lpc * (yr - zr);
            bl[il] = in + fb * zr; br[ir] = fb * zl;
            if (++il >= dl) il = 0;
            if (++ir >= dr) ir = 0;
            if (pass) { m->l[i] += yl * wet; m->r[i] += yr * wet; }
        }
    }
    free(bl); free(br);
}
static void fx_reverb(Mx *m, float wet, float fbk, float damp) {   /* Schroeder reverb running at half rate */
    static const int cl[2][4] = {{405, 431, 463, 492}, {416, 445, 476, 505}}, al[2][2] = {{82, 202}, {90, 210}};
    float cb[2][4][512], ab[2][2][256], cs[2][4]; int ci[2][4] = {{0}}, ai[2][2] = {{0}};
    memset(cb, 0, sizeof cb); memset(ab, 0, sizeof ab); memset(cs, 0, sizeof cs);
    int n = m->n, prime = 6 * SSR; if (prime > n) prime = n;
    float pl = 0.f, pr = 0.f;
    for (int pass = 0; pass < 2; pass++) {
        int i0 = pass == 0 ? ((n - prime) & ~1) : 0;
        for (int i = i0; i < n; i += 2) {
            float x = (m->s[i] + (i + 1 < n ? m->s[i + 1] : 0.f)) * 0.075f, o[2];
            for (int ch = 0; ch < 2; ch++) {
                float sum = 0;
                for (int j = 0; j < 4; j++) {
                    float y = cb[ch][j][ci[ch][j]];
                    cs[ch][j] = y * (1.f - damp) + cs[ch][j] * damp;
                    cb[ch][j][ci[ch][j]] = x + cs[ch][j] * fbk; sum += y;
                    if (++ci[ch][j] >= cl[ch][j]) ci[ch][j] = 0;
                }
                for (int j = 0; j < 2; j++) {
                    float bo = ab[ch][j][ai[ch][j]], out = -sum + bo;
                    ab[ch][j][ai[ch][j]] = sum + bo * 0.5f; sum = out;
                    if (++ai[ch][j] >= al[ch][j]) ai[ch][j] = 0;
                }
                o[ch] = sum;
            }
            if (pass) {
                m->l[i] += 0.5f * (pl + o[0]) * wet; m->r[i] += 0.5f * (pr + o[1]) * wet;
                if (i + 1 < n) { m->l[i + 1] += o[0] * wet; m->r[i + 1] += o[1] * wet; }
            }
            pl = o[0]; pr = o[1];
        }
    }
}
static void fx_lowpass(Mx *m, float hz) {      /* two cascaded poles, circular */
    float a = 1.f - expf(-TAU_F * hz / SSR), z[2][2] = {{0}}; int n = m->n, prime = 2000 < n ? 2000 : n;
    for (int pass = 0; pass < 2; pass++)
        for (int i = pass ? 0 : n - prime; i < n; i++) {
            z[0][0] += a * (m->l[i] - z[0][0]); z[0][1] += a * (z[0][0] - z[0][1]);
            z[1][0] += a * (m->r[i] - z[1][0]); z[1][1] += a * (z[1][0] - z[1][1]);
            if (pass) { m->l[i] = z[0][1]; m->r[i] = z[1][1]; }
        }
}
static void st_finish(Station *S, Mx *m, float rms_target) {
    int n = m->n; double ml = 0, mr = 0;
    for (int i = 0; i < n; i++) { ml += m->l[i]; mr += m->r[i]; }
    float dl = (float)(ml / n), dr = (float)(mr / n); double e = 0;
    for (int i = 0; i < n; i++) { m->l[i] -= dl; m->r[i] -= dr; e += (double)m->l[i] * m->l[i] + (double)m->r[i] * m->r[i]; }
    float rms = sqrtf((float)(e / (2.0 * n))), g = rms > 1e-9f ? rms_target / rms : 0.f;
    /* convert in place (the int16 pair for frame i overwrites the float slot l[i], already consumed), then shrink */
    int16_t *pcm = (int16_t *)m->l;
    for (int i = 0; i < n; i++) {
        float l = m->l[i], r = m->r[i];
        pcm[2 * i] = (int16_t)lrintf(sat(l * g) * 0.95f * 32767.f);
        pcm[2 * i + 1] = (int16_t)lrintf(sat(r * g) * 0.95f * 32767.f);
    }
    void *sm = realloc(m->l, (size_t)n * 2 * sizeof(int16_t));
    S->pcm = sm ? (int16_t *)sm : pcm; S->n = n;
}

/* ---------------- 0: synthwave, A minor, 112 bpm, 24 bars (A / B / A') ---------------- */
static void gen_synth(Station *S) {
    Clk c = {60.0 / 112.0 / 4.0, 0.0, 24}; Mx m; if (!mx_init(&m, &c, 0xA11CE5u)) return;
    static const Ch ch[7] = {{33, {57, 60, 64, 67}}, {29, {53, 57, 60, 64}}, {38, {50, 53, 57, 62}}, {40, {52, 56, 59, 64}},
                             {36, {55, 60, 64, 67}}, {31, {55, 59, 62, 67}}, {40, {52, 55, 59, 64}}};
    static const int prog[24] = {0, 1, 2, 3, 0, 1, 2, 3, 4, 5, 1, 6, 4, 5, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3};
    static const Ph leadA[] = {{0, 76, 6}, {6, 72, 2}, {8, 74, 4}, {12, 69, 4}, {16, 72, 6}, {22, 69, 2}, {24, 72, 4}, {28, 76, 4},
                               {32, 74, 6}, {38, 77, 2}, {40, 74, 4}, {44, 69, 4}, {48, 76, 8}, {56, 80, 4}, {60, 76, 2}, {62, 74, 2}, {0, 0, 0}};
    static const Ph leadB[] = {{0, 79, 4}, {4, 76, 4}, {8, 79, 4}, {12, 84, 4}, {16, 83, 6}, {22, 79, 2}, {24, 74, 4}, {28, 79, 4},
                               {32, 81, 6}, {38, 77, 2}, {40, 72, 4}, {44, 77, 4}, {48, 76, 6}, {54, 79, 2}, {56, 83, 6}, {62, 79, 2},
                               {64, 84, 6}, {70, 79, 2}, {72, 76, 4}, {76, 79, 4}, {80, 79, 6}, {86, 74, 2}, {88, 71, 4}, {92, 74, 4},
                               {96, 77, 6}, {102, 74, 2}, {104, 81, 4}, {108, 77, 4}, {112, 80, 8}, {120, 83, 4}, {124, 80, 2}, {126, 76, 2}, {0, 0, 0}};
    const Inst pad = {.wave = 0, .nosc = 2, .det = 13, .a = .5f, .d = 0, .s = 1, .r = .2f, .c0 = 1500, .c1 = 1500, .vol = .07f, .send = .45f};
    const Inst arp = {.wave = 1, .pw = .4f, .nosc = 2, .det = 7, .a = .002f, .d = .11f, .s = .12f, .r = .06f, .c0 = 4200, .c1 = 1000, .ctau = .1f, .vol = .1f, .send = .4f};
    const Inst bas = {.wave = 0, .nosc = 1, .a = .004f, .d = .12f, .s = .55f, .r = .05f, .c0 = 1100, .c1 = 380, .ctau = .1f, .drive = 1.6f, .vol = .16f, .sub = .25f};
    const Inst led = {.wave = 0, .nosc = 2, .det = 9, .a = .015f, .d = .12f, .s = .75f, .r = .15f, .c0 = 4500, .c1 = 2600, .ctau = .3f, .vol = .1f, .pan = .1f, .send = .4f, .vib = 5.5f, .vibd = 9};
    const Kit kit = {135, 46, .03f, .17f, .3f, .9f, 190, .08f, .6f, .13f, .5f, .55f, .12f, .028f, .6f, .3f, .1f, .1f, .13f};
    static const int seq[16] = {0, 1, 2, 3, 2, 1, 2, 3, 0, 1, 2, 3, 2, 1, 3, 2};
    const double bar = 16 * c.st;
    for (int b = 0; b < 24; b++) {
        const Ch *h = &ch[prog[b]]; int sec = b / 8; double t0 = tt(&c, b, 0);
        for (int j = 0; j < 3; j++) { Inst p = pad; p.pan = (j - 1) * 0.5f; note(&m, &p, t0, bar, mtof((float)h->n[j])); }
        static const int bo[2][4] = {{0, 0, 12, 0}, {0, 12, 0, 7}};
        for (int s = 0; s < 16; s += 2) note(&m, &bas, tt(&c, b, s), c.st * 1.7, mtof((float)(h->root + bo[sec == 1][(s / 2) & 3])));
        for (int s = 0; s < 16; s++) {
            Inst a = arp; a.pan = (s & 1) ? 0.5f : -0.5f; if (sec == 1) a.vol *= 1.1f;
            note(&m, &a, tt(&c, b, s), c.st * 1.5, mtof((float)(h->n[seq[s]] + 12 + ((sec == 1 && (s & 4)) ? 12 : 0))));
        }
        const char *K = sec == 1 ? "x...x...x...x..." : "x.....x.x.....x.";
        if (b < 2) K = "................";
        const char *Hh = sec == 1 ? "xoxoxoxoxoxoxoxo" : "x.x.x.x.x.x.x.x.";
        char Sn[17]; memcpy(Sn, "....x.......x...", 17);
        if (b % 8 == 7) { memcpy(Sn, "........xxxxxxxx", 17); }
        if (b < 2) Hh = "....x.......x...";
        drum_bar(&m, &c, b, &kit, K, Sn, b < 1 ? "................" : Hh, (b % 2) ? ".............x.." : "..............x.", 0.0f);
        if (b == 8 || b == 16 || b == 0) hat(&m, t0, 0.2f, 1.0f, 0.f, 0.9f, 0.3f);
    }
    phrase(&m, &led, &c, 8, leadB, 0);
    phrase(&m, &led, &c, 16, leadA, 0); phrase(&m, &led, &c, 20, leadA, 0);
    fx_delay(&m, (int)(3 * c.st * SSR), (int)(2 * c.st * SSR), 0.4f, 0.5f, 0.45f);
    fx_reverb(&m, 0.5f, 0.84f, 0.3f);
    st_finish(S, &m, 0.15f);
}

/* ---------------- 1: lo-fi hip hop, D minor-ish, 78 bpm swung, 16 bars (A / B) ---------------- */
static void gen_lofi(Station *S) {
    Clk c = {60.0 / 78.0 / 4.0, 0.3, 16}; Mx m; if (!mx_init(&m, &c, 0x10F1u)) return;
    static const Ch ch[9] = {{38, {53, 57, 60, 64}}, {43, {55, 59, 62, 65}}, {36, {52, 55, 59, 62}}, {45, {55, 61, 64, 67}},
                             {41, {57, 60, 64, 65}}, {40, {55, 59, 62, 64}}, {46, {58, 62, 65, 69}}, {45, {57, 60, 64, 67}},
                             {41, {53, 57, 60, 64}}};
    /* Dm9 G7 Cmaj7 A7 | Dm9 G7 Cmaj7 Fmaj7 || Fmaj7 Em7 Dm9 Cmaj7 | Bbmaj7 A7 Dm9 A7 */
    static const int prog[16] = {0, 1, 2, 3, 0, 1, 2, 8, 8, 5, 0, 2, 6, 3, 0, 3};
    static const Ph p1[] = {{2, 74, 3}, {6, 76, 2}, {8, 77, 6}, {16, 76, 3}, {20, 72, 2}, {24, 74, 8}, {0, 0, 0}};
    static const Ph p2[] = {{0, 69, 4}, {4, 72, 3}, {8, 74, 4}, {12, 72, 2}, {16, 76, 6}, {24, 74, 3}, {28, 72, 3}, {0, 0, 0}};
    static const Ph p3[] = {{0, 74, 6}, {8, 70, 3}, {12, 72, 4}, {16, 73, 4}, {20, 76, 4}, {24, 81, 8}, {0, 0, 0}};
    static const Ph p4[] = {{0, 77, 4}, {4, 74, 3}, {8, 72, 4}, {12, 69, 4}, {16, 73, 6}, {24, 76, 4}, {28, 73, 2}, {0, 0, 0}};
    const Inst bas = {.wave = 3, .nosc = 1, .a = .01f, .d = .25f, .s = .6f, .r = .1f, .c0 = 500, .c1 = 400, .drive = 1.8f, .vol = .17f, .sub = .2f};
    const Inst led = {.wave = 2, .nosc = 2, .det = 6, .a = .01f, .d = .2f, .s = .5f, .r = .25f, .c0 = 2400, .c1 = 1500, .ctau = .3f, .vol = .12f, .pan = -.15f, .send = .45f, .vib = 4.5f, .vibd = 10};
    const Kit kit = {115, 58, .04f, .13f, .12f, .7f, 180, .16f, .4f, .17f, .6f, .3f, .3f, .03f, .35f, .25f, .2f, .15f, .12f};
    for (int b = 0; b < 16; b++) {
        const Ch *h = &ch[prog[b]];
        float vv = 0.17f + 0.03f * rng_f(&m.rng);
        /* strummed chords */
        static const int hits[2][3][2] = {{{0, 6}, {6, 3}, {10, 6}}, {{0, 10}, {12, 4}, {-1, 0}}};
        for (int q = 0; q < 3; q++) {
            int st = hits[b & 1][q][0]; if (st < 0) continue;
            for (int j = 0; j < 4; j++)
                rhodes(&m, tt(&c, b, st) + 0.012 * j + 0.004 * rng_f(&m.rng), hits[b & 1][q][1] * c.st, mtof((float)h->n[j]),
                       vv * (q == 0 ? 1.f : 0.75f), (j - 1.5f) * 0.25f, 0.35f, 1.f);
        }
        static const int bp[2][4][3] = {{{0, 0, 5}, {6, 7, 2}, {10, 0, 3}, {14, 5, 2}}, {{0, 0, 6}, {8, 0, 3}, {12, -5, 3}, {-1, 0, 0}}};
        for (int q = 0; q < 4; q++) {
            if (bp[b & 1][q][0] < 0) continue;
            note(&m, &bas, tt(&c, b, bp[b & 1][q][0]), bp[b & 1][q][2] * c.st * 0.95, mtof((float)(h->root + bp[b & 1][q][1])));
        }
        const char *K = (b & 1) ? "x.....x..x.x...." : "x.....x...x.....";
        drum_bar(&m, &c, b, &kit, K, "....x......ox...", "x.o.x.o.x.o.x.xo", "................", 0.006f);
    }
    phrase(&m, &led, &c, 8, p1, 0); phrase(&m, &led, &c, 10, p2, 0);
    phrase(&m, &led, &c, 12, p3, 0); phrase(&m, &led, &c, 14, p4, 0);
    phrase(&m, &led, &c, 6, p2, -12);
    fx_reverb(&m, 0.35f, 0.83f, 0.45f);
    fx_lowpass(&m, 5200.f);
    /* vinyl: hiss + crackle (stationary noise, so the loop seam is inaudible) */
    float lz = 0;
    for (int i = 0; i < m.n; i++) {
        float h = rng_f(&m.rng); lz += 0.5f * (h - lz); float hs = (h - lz) * 0.006f;
        m.l[i] += hs; m.r[i] += hs * 0.8f + rng_f(&m.rng) * 0.001f;
        if (rng_01(&m.rng) < 16.f / SSR) { float a = 0.01f + 0.05f * rng_01(&m.rng); a *= a > 0.04f ? 1.5f : 1.f; for (int k = 0; k < 18; k++) { int j = wrapi(&m, i + k); float v = a * rng_f(&m.rng) * expf(-0.35f * k); m.l[j] += v; m.r[j] += v * 0.7f; } }
    }
    st_finish(S, &m, 0.13f);
}

/* ---------------- 2: punk rock, E, 168 bpm, 32 bars (verse / chorus / verse+riff / chorus) ---------------- */
static void gen_punk(Station *S) {
    Clk c = {60.0 / 168.0 / 4.0, 0.0, 32}; Mx m; if (!mx_init(&m, &c, 0xBADC0DEu)) return;
    static const int roots[32] = {40, 40, 45, 45, 40, 40, 47, 47, 49, 45, 40, 47, 49, 45, 40, 47,
                                  40, 40, 45, 45, 40, 40, 47, 47, 49, 45, 40, 47, 49, 45, 47, 47};
    static const Ph riff[] = {{0, 64, 2}, {2, 67, 2}, {4, 69, 3}, {8, 71, 2}, {10, 69, 2}, {12, 67, 4}, {0, 0, 0}};
    Inst gtr = {.wave = 0, .nosc = 2, .det = 6, .a = .002f, .d = .08f, .s = .35f, .r = .03f, .c0 = 2800, .c1 = 2800, .drive = 9, .vol = .085f};
    const Inst bas = {.wave = 0, .nosc = 1, .a = .003f, .d = .1f, .s = .6f, .r = .04f, .c0 = 1200, .c1 = 700, .ctau = .05f, .drive = 2.f, .vol = .14f};
    const Inst led = {.wave = 0, .nosc = 2, .det = 8, .a = .004f, .d = .1f, .s = .7f, .r = .05f, .c0 = 3500, .c1 = 3500, .drive = 6, .vol = .07f, .pan = .3f, .send = .15f, .vib = 5.5f, .vibd = 10};
    const Kit kit = {115, 62, .025f, .1f, .4f, .75f, 200, .09f, .8f, .15f, .85f, .1f, .17f, .03f, .7f, .3f, .05f, .2f, .11f};
    for (int b = 0; b < 32; b++) {
        int sec = b / 8, chor = (sec & 1); float r = (float)roots[b];
        for (int s = 0; s < 16; s += 2) {
            bool pm = !chor; double t = tt(&c, b, s);
            float dur = (float)(c.st * (pm ? 1.4 : (s == 0 ? 3.0 : 1.9)));
            if (chor && (s == 6 || s == 14) && b % 4 == 3) dur = (float)(c.st * 1.2);
            for (int side = 0; side < 2; side++) {
                Inst g = gtr; g.pan = side ? 0.85f : -0.85f; g.det = side ? 8.f : 5.f; g.vol = gtr.vol * (s == 0 ? 1.2f : 1.f);
                if (chor) { g.d = .5f; g.s = .6f; g.c0 = g.c1 = 3200; }
                double tj = t + 0.004 * rng_f(&m.rng);
                static const int ivl[3] = {0, 7, 12};
                for (int q = 0; q < 3; q++) note(&m, &g, tj, dur, mtof(r + (float)ivl[q] + 12.f));
            }
            static const int bo[8] = {0, 0, 0, 12, 0, 0, 7, 0};
            note(&m, &bas, t, c.st * 1.7, mtof(r + (chor ? (float)bo[(s / 2) & 7] : 0.f)));
        }
        const char *K = chor ? "x.x.x.x.x.x.x.x." : "x.x.....x...x...";
        char Sn[17]; memcpy(Sn, "....X.......X...", 17);
        char Kc[17]; memcpy(Kc, K, 17);
        if (b % 8 == 7) { memcpy(Sn, "........xxxxXXXX", 17); memcpy(Kc, "x.x.....x.x.....", 17); }
        drum_bar(&m, &c, b, &kit, Kc, Sn, chor ? "x.x.x.x.x.x.x.x." : "x.x.x.x.x.x.x.x.", chor ? ".o.o.o.o.o.o.o.o" : "................", 0.002f);
        if (b % 8 == 0 || (b % 8 == 4 && chor)) hat(&m, tt(&c, b, 0), 0.45f, 1.1f, 0.2f, 0.9f, 0.1f);
        if (sec == 2) phrase(&m, &led, &c, b, riff, roots[b] - 40);
    }
    fx_reverb(&m, 0.14f, 0.78f, 0.4f);
    st_finish(S, &m, 0.19f);
}

/* ---------------- 3: reggae / dub, A minor, 76 bpm, 16 bars ---------------- */
static void gen_dub(Station *S) {
    Clk c = {60.0 / 76.0 / 4.0, 0.0, 16}; Mx m; if (!mx_init(&m, &c, 0xD0B5u)) return;
    static const Ch ch[4] = {{33, {57, 60, 64, 64}}, {38, {57, 62, 65, 65}}, {40, {56, 59, 62, 64}}, {41, {57, 60, 65, 65}}};
    static const int prog[16] = {0, 0, 1, 1, 0, 0, 1, 2, 0, 0, 1, 1, 0, 3, 2, 2};
    static const Ph m1[] = {{4, 76, 6}, {12, 74, 2}, {14, 72, 4}, {20, 69, 8}, {28, 72, 4}, {0, 0, 0}};
    static const Ph m2[] = {{2, 77, 5}, {8, 76, 4}, {12, 74, 4}, {20, 72, 6}, {28, 69, 4}, {0, 0, 0}};
    static const Ph m3[] = {{0, 76, 8}, {10, 72, 4}, {16, 77, 6}, {24, 76, 4}, {28, 72, 3}, {0, 0, 0}};
    static const Ph m4[] = {{4, 80, 4}, {8, 76, 4}, {12, 71, 4}, {20, 76, 10}, {0, 0, 0}};
    const Inst sk = {.wave = 1, .pw = .45f, .nosc = 2, .det = 5, .a = .002f, .d = .06f, .s = .12f, .r = .05f, .c0 = 2400, .c1 = 1500, .ctau = .1f, .vol = .26f, .pan = .25f, .send = .6f};
    const Inst org = {.wave = 3, .nosc = 1, .a = .3f, .d = 0, .s = 1, .r = .5f, .vol = .04f, .pan = -.3f, .send = .08f, .sub = 0};
    const Inst bas = {.wave = 3, .nosc = 1, .a = .008f, .d = .3f, .s = .75f, .r = .08f, .c0 = 600, .c1 = 400, .drive = 1.5f, .vol = .15f, .sub = .1f};
    const Inst mel = {.wave = 0, .nosc = 2, .det = 5, .a = .03f, .d = .2f, .s = .7f, .r = .2f, .c0 = 2500, .c1 = 1800, .ctau = .4f, .vol = .06f, .pan = .2f, .send = .3f, .vib = 5, .vibd = 12};
    const Kit kit = {105, 58, .03f, .13f, .3f, .75f, 420, .03f, .5f, .02f, .7f, .55f, .15f, .035f, .6f, .3f, .35f, .0f, .1f};
    static const int bp[2][4][3] = {{{0, 0, 5}, {6, 3, 2}, {8, 0, 4}, {12, 7, 3}}, {{0, 0, 4}, {4, 0, 2}, {8, 10, 3}, {12, 7, 3}}};
    for (int b = 0; b < 16; b++) {
        const Ch *h = &ch[prog[b]]; int dub = b >= 8;
        for (int q = 0; q < 4; q++) note(&m, &bas, tt(&c, b, bp[b & 1][q][0]), bp[b & 1][q][2] * c.st * 0.95, mtof((float)(h->root + bp[b & 1][q][1])));
        for (int s = 2; s < 16; s += 4) for (int j = 0; j < 3; j++) { Inst k = sk; k.pan = 0.15f + 0.1f * j; if (b < 2) k.vol *= 0.6f; note(&m, &k, tt(&c, b, s), c.st * 0.8, mtof((float)h->n[j] + 12.f)); }
        if (dub) for (int j = 0; j < 3; j++) note(&m, &org, tt(&c, b, 0), 16 * c.st, mtof((float)h->n[j] - 12.f));
        bool brk = (b == 11 || b == 15);
        const char *K = brk ? "................" : "........x.......";
        const char *S = brk ? "........x......." : "........x.......";
        if (b == 7 || b == 15) { K = "........x...x..."; }
        char Hh[17]; memcpy(Hh, dub ? "o.x.o.x.o.x.o.x." : "x.o.x.o.x.o.x.o.", 17);
        Kit kk = kit; if (brk) { kk.kvol = 0; kk.svol = 0.7f; }
        drum_bar(&m, &c, b, &kk, K, S, Hh, "................", 0.0f);
        if (b == 7 || b == 15) { kick(&m, tt(&c, b, 12), .5f, 190, 130, .05f, .18f, 0); kick(&m, tt(&c, b, 14), .5f, 160, 105, .05f, .18f, 0); }
    }
    phrase(&m, &mel, &c, 8, m1, 0); phrase(&m, &mel, &c, 10, m2, 0); phrase(&m, &mel, &c, 12, m3, 0); phrase(&m, &mel, &c, 14, m4, 0);
    fx_delay(&m, (int)(3 * c.st * SSR), (int)(4 * c.st * SSR), 0.56f, 0.55f, 0.3f);
    fx_reverb(&m, 0.3f, 0.84f, 0.4f);
    st_finish(S, &m, 0.16f);
}

/* ---------------- 4: drum and bass, F minor, 172 bpm, 40 bars (intro / drop / break / drop) ---------------- */
static void gen_dnb(Station *S) {
    Clk c = {60.0 / 172.0 / 4.0, 0.0, 40}; Mx m; if (!mx_init(&m, &c, 0xD2B2u)) return;
    static const Ch ch[4] = {{29, {56, 60, 63, 67}}, {25, {53, 56, 61, 65}}, {34, {53, 58, 61, 65}}, {36, {55, 60, 63, 67}}};
    const Inst pad = {.wave = 0, .nosc = 2, .det = 12, .a = .35f, .d = 0, .s = 1, .r = .3f, .c0 = 1800, .c1 = 1800, .vol = .06f, .send = .5f};
    const Inst reese = {.wave = 0, .nosc = 3, .det = 18, .a = .005f, .d = 0, .s = 1, .r = .05f, .c0 = 900, .c1 = 420, .ctau = .35f, .drive = 2.2f, .vol = .13f};
    const Inst sub = {.wave = 3, .nosc = 1, .a = .005f, .d = 0, .s = 1, .r = .04f, .vol = .19f};
    const Inst plk = {.wave = 1, .pw = .35f, .nosc = 2, .det = 8, .a = .002f, .d = .09f, .s = .05f, .r = .05f, .c0 = 4500, .c1 = 1200, .ctau = .08f, .vol = .075f, .send = .3f};
    const Kit kit = {150, 52, .02f, .1f, .35f, .8f, 220, .08f, .7f, .11f, .8f, .2f, .15f, .02f, .8f, .3f, .1f, .1f, .09f};
    static const int bp[2][3][3] = {{{0, 0, 10}, {10, 0, 3}, {13, 12, 3}}, {{0, 0, 6}, {6, 7, 3}, {10, 0, 6}}};
    for (int b = 0; b < 40; b++) {
        int sec = b < 8 ? 0 : (b < 24 ? 1 : (b < 32 ? 2 : 3)); const Ch *h = &ch[(b / 2) & 3 ? (b / 2) & 3 : 0];
        h = &ch[(b / 2) % 4];
        for (int j = 0; j < 3; j++) { Inst p = pad; p.pan = (j - 1) * 0.6f; if (sec == 2) p.vol *= 1.5f; else if (sec == 1 || sec == 3) p.vol *= 0.8f; note(&m, &p, tt(&c, b, 0), 16 * c.st, mtof((float)h->n[j])); }
        if (sec == 1 || sec == 3) {
            for (int q = 0; q < 3; q++) {
                double t = tt(&c, b, bp[b & 1][q][0]), d = bp[b & 1][q][2] * c.st * 0.97; float f = mtof((float)(h->root + bp[b & 1][q][1]));
                note(&m, &reese, t, d, f); note(&m, &sub, t, d, mtof((float)(h->root + (bp[b & 1][q][1] > 11 ? 0 : bp[b & 1][q][1]))));
            }
        } else if (sec == 2 && (b & 1) == 0) note(&m, &sub, tt(&c, b, 0), 15 * c.st, mtof((float)h->root));
        if ((sec == 1 && b >= 16) || sec == 3) {
            static const int ps[16] = {1, 0, 1, 1, 0, 1, 1, 0, 1, 0, 1, 1, 0, 1, 1, 0}, ix[8] = {0, 2, 1, 3, 2, 1, 3, 2};
            for (int s = 0; s < 16; s++) if (ps[s]) { Inst a = plk; a.pan = (s & 1) ? .45f : -.45f; note(&m, &a, tt(&c, b, s), c.st * 1.2, mtof((float)(h->n[ix[s & 7]] + 12 + ((s & 8) ? 12 : 0)))); }
        }
        const char *K, *Sn, *Hh = "x.xxx.xxx.xxx.xx";
        static const char *Ke = "x.........x.....", *Ko = "x.........x.x...";
        K = (b & 1) ? Ko : Ke; Sn = "....x.......x...";
        char Gh[17]; memcpy(Gh, (b & 1) ? "..o...o.o..o.o.." : "......o....o..o.", 17);
        char Sc[17]; memcpy(Sc, Sn, 17); for (int i = 0; i < 16; i++) if (Gh[i] == 'o' && Sc[i] == '.') Sc[i] = 'o';
        if (sec == 0) { K = b >= 4 ? K : "................"; if (b < 4) { Hh = "x...x...x...x..."; memcpy(Sc, "................", 17); } }
        if (sec == 2) { K = "x..............."; memcpy(Sc, "........x.......", 17); Hh = "x.x.x.x.x.x.x.x."; }
        if (b % 8 == 7 && sec != 2) memcpy(Sc, "....x...x.x.xxxX", 17);
        if (sec == 2 && b == 31) { K = "x...x...x...x..."; memcpy(Sc, "........x.x.xxxx", 17); }
        drum_bar(&m, &c, b, &kit, K, Sc, Hh, (b & 1) ? "..............x." : "......x.........", 0.0f);
        if (b == 8 || b == 24 || b == 32) hat(&m, tt(&c, b, 0), 0.4f, 1.2f, 0.2f, 0.9f, 0.3f);
        if (b == 6 || b == 30) riser(&m, tt(&c, b, 0), 32 * c.st, 0.35f);
    }
    fx_delay(&m, (int)(3 * c.st * SSR), (int)(6 * c.st * SSR), 0.35f, 0.3f, 0.4f);
    fx_reverb(&m, 0.3f, 0.82f, 0.4f);
    st_finish(S, &m, 0.17f);
}

/* ------------------------------------------------------------------------------------------ */
/* generation driver                                                                           */
/* ------------------------------------------------------------------------------------------ */
static bool g_gen = false;
static double g_gen_time = 0.0;
static void (*const g_gen_st[NST])(Station *) = {gen_synth, gen_lofi, gen_punk, gen_dub, gen_dnb};

static void free_all(void) {
    for (int i = 0; i < SFX_N; i++) { free(g_sfx[i].d); g_sfx[i].d = NULL; g_sfx[i].n = 0; }
    for (int i = 0; i < NST; i++) { free(g_st[i].pcm); g_st[i].pcm = NULL; g_st[i].n = 0; }
    g_gen = false;
}
static void build_tables(void);
static bool gen_all(void) {
    if (g_gen) return true;
    double t0 = now_s();
    for (int i = 0; i <= TBL + 1; i++) g_sin[i] = sinf(TAU_F * (float)i / (float)TBL);
    build_tables();
    gen_sfx();
    for (int i = 0; i < NST; i++) g_gen_st[i](&g_st[i]);
    g_gen_time = now_s() - t0;
    for (int i = 0; i < SFX_N; i++) if (!g_sfx[i].d) { free_all(); return false; }
    for (int i = 0; i < NST; i++) if (!g_st[i].pcm) { free_all(); return false; }
    g_gen = true; return true;
}

/* ------------------------------------------------------------------------------------------ */
/* engine wavetables                                                                           */
/* ------------------------------------------------------------------------------------------ */
#define ETAB 2048
typedef struct { float tab[ETAB + 2]; float fmin, fmax, gain, cmin, cmax, rasp, lump; } EngTab;
static EngTab g_eng[3];

static void build_tables(void) {
    static const float fmin[3] = {30.f, 42.f, 20.f}, fmax[3] = {180.f, 260.f, 90.f};
    for (int kd = 0; kd < 3; kd++) {
        EngTab *T = &g_eng[kd]; int H = (int)(19000.f / fmax[kd]); if (H > 220) H = 220;
        float pk = 0.f;
        for (int j = 0; j < ETAB; j++) T->tab[j] = 0.f;
        for (int k = 1; k <= H; k++) {
            float kf = (float)k, a;
            if (kd == 0) a = powf(kf, -1.15f) * (1.f + 1.2f * expf(-0.25f * (kf - 3.f) * (kf - 3.f)));
            else if (kd == 1) a = powf(kf, -0.65f) * ((k & 1) ? 1.f : 0.6f);
            else a = powf(kf, -1.5f) * (1.f + 1.f * expf(-0.45f * (kf - 2.f) * (kf - 2.f)));
            float phs = kd == 2 ? 0.08f * kf * kf / (float)H : 0.f;
            for (int j = 0; j < ETAB; j++) T->tab[j] += a * tsinw((float)j * kf / ETAB + 0.25f + phs);
        }
        for (int j = 0; j < ETAB; j++) if (fabsf(T->tab[j]) > pk) pk = fabsf(T->tab[j]);
        for (int j = 0; j < ETAB; j++) T->tab[j] /= pk;
        T->tab[ETAB] = T->tab[0]; T->tab[ETAB + 1] = T->tab[1];
        T->fmin = fmin[kd]; T->fmax = fmax[kd];
        T->gain = kd == 0 ? 0.62f : (kd == 1 ? 0.66f : 0.40f);
        T->cmin = kd == 0 ? 450.f : (kd == 1 ? 650.f : 200.f);
        T->cmax = kd == 0 ? 3800.f : (kd == 1 ? 5200.f : 1500.f);
        T->rasp = kd == 1 ? 1.7f : (kd == 0 ? 1.15f : 1.f);
        T->lump = kd == 2 ? 0.35f : 0.22f;
    }
}

/* ------------------------------------------------------------------------------------------ */
/* runtime mixer                                                                               */
/* ------------------------------------------------------------------------------------------ */
typedef struct { const float *buf; int len; double pos; float rate, gl, gr; int age; int on; } Voice;

static struct Rt {
    Voice v[NVOICE];
    float lx, ly, lz, rx, ry, rz;
    /* targets written by the game thread */
    float t_master, t_music; bool muted;
    bool eng_on; float eng_rpm, eng_load; int eng_kind;
    float t_skid, t_sirvol, t_sirpan, t_city, t_wind, t_sea;
    int radio_req;
    /* smoothed state owned by the audio callback */
    float master, music;
    float e_env, e_rpm, e_load, e_ph, e_ph2, e_jn, e_l1, e_l2, e_ln, e_g; int e_kind, e_cnt; float e_cut;
    float k_amt, k_wph, k_ln; Svf k_s1, k_s2; float k_hz, k_f1, k_f2; int k_cnt;
    float s_vol, s_pan, s_ph, s_lph, s_t, s_lp;
    float a_city, a_wind, a_sea, a_c1, a_c2, a_c3, a_lfo1, a_lfo2, a_gust, a_gl, a_lfo3; Svf a_ws, a_ss; float a_wf, a_sf; int a_cnt;
    int rad_last, rad_cur, rad_tn; float rad_env; double rad_pos; unsigned long long bcast;
    float st_lo, st_bd, st_ph, st_pop; Svf st_s;
    Rng rng;
} rt;

static SDL_AudioDeviceID g_dev = 0;
#define LK() do { if (g_dev) SDL_LockAudioDevice(g_dev); } while (0)
#define UL() do { if (g_dev) SDL_UnlockAudioDevice(g_dev); } while (0)
static const float k_slow = 0.00083f;   /* ~25 ms smoothing at 48 kHz */

static void rt_reset(void) {
    memset(&rt, 0, sizeof rt);
    rt.rx = 1.f; rt.t_master = 1.f; rt.t_music = 1.f; rt.master = 0.f; rt.music = 1.f;
    rt.radio_req = -1; rt.rad_last = -1; rt.rad_cur = -1; rt.rng.s = 0x5EED1234u;
    rt.eng_kind = 0; rt.e_kind = 0;
}

/* ----- one-shots ----- */
static void start_voice(int sfx, float gl, float gr, float pitch) {
    int slot = -1; float best = -1.f;
    for (int i = 0; i < NVOICE; i++) if (!rt.v[i].on) { slot = i; break; }
    if (slot < 0) for (int i = 0; i < NVOICE; i++) { float p = (float)(rt.v[i].pos / rt.v[i].len); if (p > best) { best = p; slot = i; } }
    Voice *v = &rt.v[slot];
    v->buf = g_sfx[sfx].d; v->len = g_sfx[sfx].n; v->pos = 0; v->rate = clampf(pitch, 0.25f, 4.f);
    v->gl = gl; v->gr = gr; v->age = 0; v->on = v->len > 2;
}
static void mix_voices(float *out, int frames) {
    for (int vi = 0; vi < NVOICE; vi++) {
        Voice *v = &rt.v[vi]; if (!v->on) continue;
        const float *b = v->buf; double pos = v->pos; int age = v->age;
        for (int i = 0; i < frames; i++) {
            int p = (int)pos; if (p >= v->len - 1) { v->on = 0; break; }
            float fr = (float)(pos - p), s = b[p] + (b[p + 1] - b[p]) * fr;
            if (age < 24) s *= (float)age / 24.f;
            age++; out[2 * i] += s * v->gl; out[2 * i + 1] += s * v->gr; pos += v->rate;
        }
        v->pos = pos; v->age = age;
    }
}

/* ----- engine ----- */
static void layer_engine(float *out, int frames) {
    struct Rt *r = &rt; bool want = r->eng_on;
    if (r->e_env < 1e-4f && !want) { r->e_env = 0.f; return; }
    if (r->e_env < 1e-4f) { r->e_kind = r->eng_kind < 0 ? 0 : (r->eng_kind > 2 ? 2 : r->eng_kind); r->e_rpm = clampf(r->eng_rpm, 0.f, 1.f); r->e_load = clampf(r->eng_load, 0.f, 1.f); }
    const EngTab *T = &g_eng[r->e_kind];
    for (int i = 0; i < frames; i++) {
        int rk = r->eng_kind < 0 ? 0 : (r->eng_kind > 2 ? 2 : r->eng_kind);
        bool same = rk == r->e_kind, tgt_on = want && same;
        float k = tgt_on ? 0.0026f : (want ? 0.004f : 0.0030f);       /* up ~0.1 s, off ~0.3 s */
        r->e_env += ((tgt_on ? 1.f : 0.f) - r->e_env) * k;
        if (r->e_env < 1e-4f && !tgt_on) { if (!want) { r->e_env = 0.f; return; } r->e_kind = rk; T = &g_eng[rk]; r->e_rpm = clampf(r->eng_rpm, 0.f, 1.f); }
        r->e_rpm += (clampf(r->eng_rpm, 0.f, 1.f) - r->e_rpm) * 0.0010f;
        r->e_load += (clampf(r->eng_load, 0.f, 1.f) - r->e_load) * 0.0015f;
        float rpm = r->e_rpm, load = r->e_load;
        r->e_jn += (rng_f(&r->rng) - r->e_jn) * 0.0007f;
        float f = (T->fmin + (T->fmax - T->fmin) * rpm) * (1.f + 0.05f * r->e_jn * (1.2f - rpm));
        r->e_ph += f / SR; if (r->e_ph >= 1.f) r->e_ph -= 1.f;
        r->e_ph2 += 0.5f * f / SR; if (r->e_ph2 >= 1.f) r->e_ph2 -= 1.f;
        float x = r->e_ph * ETAB; int ix = (int)x; float fr = x - (float)ix;
        float w = T->tab[ix] + (T->tab[ix + 1] - T->tab[ix]) * fr;
        w *= 1.f + T->lump * (1.f - rpm) * tsin(r->e_ph2);
        r->e_ln += 0.12f * (rng_f(&r->rng) - r->e_ln);
        w += r->e_ln * (0.18f + 0.35f * load * (0.4f + rpm));
        if (T->rasp > 1.1f) w = sat(w * T->rasp * 1.4f) * 0.8f;
        if ((r->e_cnt++ & 15) == 0) {
            float cut = T->cmin + (T->cmax - T->cmin) * load * (0.35f + 0.65f * rpm) + 0.15f * (T->cmax - T->cmin) * rpm;
            r->e_g = 1.f - expf(-TAU_F * cut / SR);
        }
        r->e_l1 += r->e_g * (w - r->e_l1); r->e_l2 += r->e_g * (r->e_l1 - r->e_l2);
        float amp = r->e_env * T->gain * (0.55f + 0.45f * load) * (0.8f + 0.4f * rpm);
        float o = r->e_l2 * amp; out[2 * i] += o; out[2 * i + 1] += o;
    }
}
/* ----- tyre squeal ----- */
static void layer_skid(float *out, int frames) {
    struct Rt *r = &rt; float tg = clampf(r->t_skid, 0.f, 1.f);
    if (r->k_amt < 1e-4f && tg < 1e-4f) { r->k_amt = 0.f; return; }
    for (int i = 0; i < frames; i++) {
        r->k_amt += (tg - r->k_amt) * 0.0025f;
        float amt = r->k_amt;
        r->k_wph += 7.f / SR; if (r->k_wph >= 1.f) r->k_wph -= 1.f;
        r->k_ln += 0.002f * (rng_f(&r->rng) - r->k_ln);
        if ((r->k_cnt++ & 31) == 0) {
            float fc = 1750.f + 750.f * amt + 60.f * tsin(r->k_wph) + 400.f * r->k_ln * 8.f;
            r->k_f1 = 2.f * sinf(PI_F * fc / SR); r->k_f2 = 2.f * sinf(PI_F * fc * 1.52f / SR);
        }
        float x = rng_f(&r->rng), h1, h2;
        r->k_s1.lo += r->k_f1 * r->k_s1.bd; h1 = x - r->k_s1.lo - 0.05f * r->k_s1.bd; r->k_s1.bd += r->k_f1 * h1;
        r->k_s2.lo += r->k_f2 * r->k_s2.bd; h2 = x - r->k_s2.lo - 0.07f * r->k_s2.bd; r->k_s2.bd += r->k_f2 * h2;
        float v = (r->k_s1.bd * 0.9f + r->k_s2.bd * 0.5f) * (1.f + 3.f * r->k_ln) * 0.045f + x * 0.02f * amt;
        v *= amt * sqrtf(amt) * 2.2f;
        out[2 * i] += v; out[2 * i + 1] += v;
    }
}
/* ----- police siren ----- */
static void layer_siren(float *out, int frames) {
    struct Rt *r = &rt; float tv = clampf(r->t_sirvol, 0.f, 1.f), tp = clampf(r->t_sirpan, -1.f, 1.f);
    if (r->s_vol < 1e-4f && tv < 1e-4f) { r->s_vol = 0.f; return; }
    for (int i = 0; i < frames; i++) {
        r->s_vol += (tv - r->s_vol) * k_slow; r->s_pan += (tp - r->s_pan) * k_slow;
        r->s_t += 1.f / SR; if (r->s_t >= 8.f) r->s_t -= 8.f;
        r->s_lph += (r->s_t < 5.2f ? 0.38f : 2.8f) / SR; if (r->s_lph >= 1.f) r->s_lph -= 1.f;
        float sw = 0.5f - 0.5f * tsin(r->s_lph + 0.25f);
        float f = 640.f + 600.f * sw;
        r->s_ph += f / SR; if (r->s_ph >= 1.f) r->s_ph -= 1.f;
        float p = r->s_ph;
        float v = tsin(p) + 0.45f * tsinw(2.f * p) + 0.25f * tsinw(3.f * p) + 0.12f * tsinw(4.f * p);
        r->s_lp += 0.35f * (v - r->s_lp);
        v = r->s_lp * 0.22f * r->s_vol;
        float a = (r->s_pan + 1.f) * 0.25f * PI_F;
        out[2 * i] += v * cosf(a); out[2 * i + 1] += v * sinf(a);
    }
}
/* ----- ambience ----- */
static void layer_ambience(float *out, int frames) {
    struct Rt *r = &rt; float tc = clampf(r->t_city, 0.f, 1.f), tw = clampf(r->t_wind, 0.f, 1.f), ts = clampf(r->t_sea, 0.f, 1.f);
    if (r->a_city < 1e-4f && r->a_wind < 1e-4f && r->a_sea < 1e-4f && tc < 1e-4f && tw < 1e-4f && ts < 1e-4f) { r->a_city = r->a_wind = r->a_sea = 0.f; return; }
    for (int i = 0; i < frames; i++) {
        r->a_city += (tc - r->a_city) * 0.0005f; r->a_wind += (tw - r->a_wind) * 0.0008f; r->a_sea += (ts - r->a_sea) * 0.0005f;
        float x = rng_f(&r->rng), o = 0.f;
        if (r->a_city > 1e-4f) {
            r->a_lfo1 += 0.07f / SR; if (r->a_lfo1 >= 1.f) r->a_lfo1 -= 1.f;
            r->a_c1 += 0.012f * (x - r->a_c1); r->a_c2 += 0.07f * (x - r->a_c2);
            float rum = rng_f(&r->rng); r->a_c3 += 0.004f * (rum - r->a_c3);
            float mod = 0.75f + 0.25f * tsin(r->a_lfo1);
            o += (r->a_c1 * 1.3f + r->a_c2 * 0.35f * mod + r->a_c3 * 4.f) * r->a_city * 0.7f;
        }
        if (r->a_wind > 1e-4f) {
            r->a_gust += 0.00015f * (rng_f(&r->rng) - r->a_gust);
            if ((r->a_cnt & 31) == 0) { float fc = clampf(300.f + 1800.f * r->a_wind + 600.f * r->a_gust * 20.f, 200.f, 5500.f); r->a_wf = 2.f * sinf(PI_F * fc / SR); }
            r->a_ws.lo += r->a_wf * r->a_ws.bd; float h = x - r->a_ws.lo - 0.6f * r->a_ws.bd; r->a_ws.bd += r->a_wf * h;
            float g = 0.8f + 0.5f * r->a_gust * 20.f; if (g < 0.2f) g = 0.2f;
            o += r->a_ws.bd * 0.65f * r->a_wind * sqrtf(r->a_wind) * g;
        }
        if (r->a_sea > 1e-4f) {
            r->a_lfo2 += 0.09f / SR; if (r->a_lfo2 >= 1.f) r->a_lfo2 -= 1.f;
            r->a_lfo3 += 0.057f / SR; if (r->a_lfo3 >= 1.f) r->a_lfo3 -= 1.f;
            float sw = 0.55f * (0.5f + 0.5f * tsin(r->a_lfo2)) + 0.45f * (0.5f + 0.5f * tsin(r->a_lfo3 + 0.3f)); sw *= sw;
            if ((r->a_cnt & 31) == 0) r->a_sf = 2.f * sinf(PI_F * (200.f + 1500.f * sw) / SR);
            r->a_ss.lo += r->a_sf * r->a_ss.bd; float h = x - r->a_ss.lo - 0.9f * r->a_ss.bd; r->a_ss.bd += r->a_sf * h;
            r->a_gl += 0.3f * (x - r->a_gl);
            o += (r->a_ss.lo * (0.12f + 0.88f * sw) * 0.9f + (x - r->a_gl) * sw * sw * sw * 0.04f) * r->a_sea * 0.8f;
        }
        r->a_cnt++;
        out[2 * i] += o; out[2 * i + 1] += o * 0.97f;
    }
}
/* ----- radio ----- */
static void layer_radio(float *out, int frames) {
    struct Rt *r = &rt; int req = r->radio_req;
    if (req != r->rad_last) { r->rad_last = req; r->rad_tn = req >= 0 ? (int)(0.3f * SR) : 0; }
    float tmus = clampf(r->t_music, 0.f, 2.f);
    if (req < 0 && r->rad_env < 1e-4f && r->rad_tn == 0) { r->rad_env = 0.f; r->music = tmus; r->rad_cur = -1; return; }
    const int total = (int)(0.3f * SR);
    for (int i = 0; i < frames; i++) {
        float tgt;
        if (req < 0) tgt = 0.f;
        else if (r->rad_tn > 0) {
            if (r->rad_tn <= (int)(0.2f * SR) && r->rad_cur != req) {
                r->rad_cur = req; const Station *S = &g_st[req];
                double off = (double)S->n * (0.13 + 0.21 * req);
                r->rad_pos = fmod((double)(r->bcast + (unsigned long long)i) * SSR / SR + off, (double)S->n);
            }
            tgt = (r->rad_tn <= (int)(0.12f * SR) && r->rad_cur == req) ? 1.f : 0.f;
        } else tgt = r->rad_cur == req ? 1.f : 0.f;
        r->rad_env += (tgt - r->rad_env) * 0.0015f; r->music += (tmus - r->music) * k_slow;
        float l = 0.f, rr = 0.f;
        if (r->rad_cur >= 0) {
            const Station *S = &g_st[r->rad_cur];
            if (r->rad_env > 1e-4f) {
                int p = (int)r->rad_pos, q = p + 1 >= S->n ? 0 : p + 1; float fr = (float)(r->rad_pos - p);
                l = ((float)S->pcm[2 * p] + (float)(S->pcm[2 * q] - S->pcm[2 * p]) * fr) * (1.f / 32768.f) * r->rad_env;
                rr = ((float)S->pcm[2 * p + 1] + (float)(S->pcm[2 * q + 1] - S->pcm[2 * p + 1]) * fr) * (1.f / 32768.f) * r->rad_env;
            }
            r->rad_pos += (double)SSR / SR; if (r->rad_pos >= S->n) r->rad_pos -= S->n;   /* the station keeps broadcasting */
        }
        if (r->rad_tn > 0) {
            float u = (float)r->rad_tn / (float)total;
            float a = clampf((float)(total - r->rad_tn) / (0.02f * SR), 0.f, 1.f) * clampf((float)r->rad_tn / (0.1f * SR), 0.f, 1.f);
            float x = rng_f(&r->rng);
            r->st_lo += 0.3f * (x - r->st_lo); float hp = x - r->st_lo;
            if ((i & 7) == 0 && rng_01(&r->rng) < 0.02f * 8) r->st_pop = 0.8f * rng_f(&r->rng);
            r->st_pop *= 0.97f;
            r->st_ph += (500.f + 2600.f * u * u) / SR; if (r->st_ph >= 1.f) r->st_ph -= 1.f;
            float s = (0.55f * (r->st_lo * 0.6f + hp * 0.7f) + r->st_pop + 0.12f * tsin(r->st_ph) * (0.4f + 0.6f * rng_01(&r->rng))) * a * 0.35f;
            l += s; rr += s * 0.9f;
            r->rad_tn--;
        }
        out[2 * i] += l * r->music; out[2 * i + 1] += rr * r->music;
    }
}

static void mix_block(float *out, int frames) {
    struct Rt *r = &rt;
    memset(out, 0, (size_t)frames * 2 * sizeof(float));
    if (!g_gen) return;
    mix_voices(out, frames);
    layer_engine(out, frames); layer_skid(out, frames); layer_siren(out, frames); layer_ambience(out, frames);
    layer_radio(out, frames);
    float tmst = r->muted ? 0.f : clampf(r->t_master, 0.f, 2.f);
    for (int i = 0; i < frames; i++) {
        r->master += (tmst - r->master) * 0.0015f;
        out[2 * i] = sat(out[2 * i] * r->master); out[2 * i + 1] = sat(out[2 * i + 1] * r->master);
    }
    r->bcast += (unsigned long long)frames;
}
static void SDLCALL audio_cb(void *ud, Uint8 *stream, int len) { (void)ud; mix_block((float *)stream, len / 8); }

/* ------------------------------------------------------------------------------------------ */
/* public API                                                                                  */
/* ------------------------------------------------------------------------------------------ */
bool audio_init(bool open_device) {
    if (!gen_all()) return false;
    rt_reset();
    if (!open_device) return true;
    if (g_dev) return true;
    SDL_AudioSpec w, h; SDL_zero(w); SDL_zero(h);
    w.freq = SR; w.format = AUDIO_F32SYS; w.channels = 2; w.samples = 512; w.callback = audio_cb;
    g_dev = SDL_OpenAudioDevice(NULL, 0, &w, &h, 0);
    if (!g_dev) return false;
    SDL_PauseAudioDevice(g_dev, 0);
    return true;
}
void audio_shutdown(void) {
    if (g_dev) { SDL_CloseAudioDevice(g_dev); g_dev = 0; }
    free_all(); memset(&rt, 0, sizeof rt);
}
void audio_listener(float x, float y, float z, float rx, float ry, float rz) {
    LK(); rt.lx = x; rt.ly = y; rt.lz = z; rt.rx = rx; rt.ry = ry; rt.rz = rz; UL();
}
void audio_play(int sfx, float x, float y, float z, float vol, float pitch) {
    if (!g_gen || sfx < 0 || sfx >= SFX_N) return;
    LK();
    float dx = x - rt.lx, dy = y - rt.ly, dz = z - rt.lz, d = sqrtf(dx * dx + dy * dy + dz * dz), g;
    if (d >= 160.f) { UL(); return; }
    g = d <= 6.f ? 1.f : (6.f / d) * (1.f - (d - 6.f) / 154.f);
    float p = d > 1e-3f ? (dx * rt.rx + dy * rt.ry + dz * rt.rz) / d : 0.f;
    p = clampf(p, -1.f, 1.f) * clampf(d / 2.f, 0.f, 1.f);
    float a = (p + 1.f) * 0.25f * PI_F; g *= vol * 1.25f;
    if (g > 0.002f) start_voice(sfx, g * cosf(a), g * sinf(a), pitch);
    UL();
}
void audio_play2d(int sfx, float vol, float pitch) {
    if (!g_gen || sfx < 0 || sfx >= SFX_N) return;
    LK(); float g = vol * 1.25f * 0.7071f; if (g > 0.002f) start_voice(sfx, g, g, pitch); UL();
}
void audio_engine(bool on, float rpm, float load, int kind) {
    LK(); rt.eng_on = on; rt.eng_rpm = rpm; rt.eng_load = load; rt.eng_kind = kind; UL();
}
void audio_skid(float amount) { LK(); rt.t_skid = amount; UL(); }
void audio_siren(float vol, float pan) { LK(); rt.t_sirvol = vol; rt.t_sirpan = pan; UL(); }
void audio_ambience(float city, float wind, float sea) { LK(); rt.t_city = city; rt.t_wind = wind; rt.t_sea = sea; UL(); }
void audio_radio(int station) { if (station < 0 || station >= NST) station = -1; LK(); rt.radio_req = station; UL(); }
int audio_radio_count(void) { return NST; }
const char *audio_radio_name(int station) { return (station >= 0 && station < NST) ? g_st[station].name : "RADIO OFF"; }
void audio_volume(float master, float music) { LK(); rt.t_master = master; rt.t_music = music; UL(); }
void audio_mute(bool m) { LK(); rt.muted = m; UL(); }

/* ------------------------------------------------------------------------------------------ */
/* self test (no device, no files)                                                             */
/* ------------------------------------------------------------------------------------------ */
static int check_f(const char *what, int idx, const float *b, int n, float rms_min) {
    double e = 0; float pk = 0; int bad = 0;
    for (int i = 0; i < n; i++) { float a = b[i]; if (!isfinite(a)) { bad = 1; break; } if (fabsf(a) > pk) pk = fabsf(a); e += (double)a * a; }
    float rms = n > 0 ? sqrtf((float)(e / n)) : 0.f;
    if (bad || n < 16 || pk > 1.0f || rms < rms_min) { fprintf(stderr, "audio: %s %d FAILED (n=%d peak=%.3f rms=%.5f%s)\n", what, idx, n, pk, rms, bad ? " non-finite" : ""); return 1; }
    return 0;
}

int audio_selftest(void) {
    int fails = 0;
    if (!gen_all()) { fprintf(stderr, "audio: generation failed\n"); printf("audio: generation FAILED\n"); return 1; }
    double bytes = 0;
    for (int i = 0; i < SFX_N; i++) { fails += check_f("sfx", i, g_sfx[i].d, g_sfx[i].n, 1e-3f); bytes += (double)g_sfx[i].n * 4.0; }
    for (int s = 0; s < NST; s++) {
        const Station *S = &g_st[s]; bytes += (double)S->n * 4.0;
        int bad = 0; int pk = 0; double e = 0, dcl = 0, dcr = 0;
        if (S->n < 45 * SSR) bad = 1;
        for (int i = 0; i < S->n * 2; i++) { int a = abs((int)S->pcm[i]); if (a > pk) pk = a; e += (double)S->pcm[i] * S->pcm[i]; if (i & 1) dcr += S->pcm[i]; else dcl += S->pcm[i]; }
        double rms = sqrt(e / (2.0 * S->n)) / 32768.0;
        /* loop seam: the jump across the wrap must be no bigger than normal sample-to-sample motion */
        double mx = 1, sd = 0;
        for (int i = 1; i < S->n; i++) { double d = fabs((double)S->pcm[2 * i] - S->pcm[2 * i - 2]); sd += d; }
        double seam = fabs((double)S->pcm[0] - S->pcm[2 * (S->n - 1)]); mx = sd / S->n;
        if (seam > 12.0 * mx + 600.0) bad = 1;
        if (fabs(dcl / S->n) > 600 || fabs(dcr / S->n) > 600) bad = 1;
        if (pk > 32767 || rms < 0.02 || !S->name) bad = 1;
        if (bad) { fprintf(stderr, "audio: station %d FAILED (n=%d peak=%d rms=%.4f seam=%.0f avgstep=%.0f)\n", s, S->n, pk, rms, seam, mx); fails++; }
    }
    /* render ~2 s through the real mixer with every layer active */
    struct Rt saved; LK(); memcpy(&saved, &rt, sizeof rt);
    rt_reset(); audio_volume(1.f, 1.f);
    rt.lx = rt.ly = rt.lz = 0.f; rt.rx = 1.f;
    rt.eng_on = true; rt.eng_rpm = 0.5f; rt.eng_load = 0.7f; rt.eng_kind = 0; rt.t_skid = 0.6f; rt.t_sirvol = 0.7f; rt.t_sirpan = 0.3f;
    rt.t_city = 0.5f; rt.t_wind = 0.4f; rt.t_sea = 0.3f; rt.radio_req = 0;
    static float buf[512 * 2]; static float all[96000 * 2]; int total = 0;
    for (int blk = 0; blk < 188; blk++) {
        if (blk == 5) start_voice(SFX_EXPLOSION, 0.7f, 0.7f, 1.f);
        if (blk == 20) start_voice(SFX_HORN, 0.9f, 0.3f, 1.f);
        if (blk == 40) start_voice(SFX_CRASH_HEAVY, 0.5f, 0.9f, 0.9f);
        if (blk == 60) { rt.eng_rpm = 0.9f; rt.radio_req = 3; }
        if (blk == 90) start_voice(SFX_MISSION_PASS, 0.8f, 0.8f, 1.f);
        mix_block(buf, 512);
        if (total + 512 <= 96000) { memcpy(all + 2 * total, buf, sizeof buf); total += 512; }
    }
    double e = 0; float pk = 0; int bad = 0;
    for (int i = 0; i < total * 2; i++) { if (!isfinite(all[i])) bad = 1; if (fabsf(all[i]) > pk) pk = fabsf(all[i]); e += (double)all[i] * all[i]; }
    float rms = sqrtf((float)(e / (total * 2.0)));
    if (bad || pk > 1.0f || rms < 0.01f || total < 90000) { fprintf(stderr, "audio: mixer test FAILED (peak=%.3f rms=%.4f)\n", pk, rms); fails++; }
    memcpy(&rt, &saved, sizeof rt); UL();
    printf("audio: %d sfx, %d stations, %.1f MB, generated in %.2f s - %s\n", SFX_N, NST, bytes / 1048576.0, g_gen_time,
           fails ? "FAIL" : "OK");
    return fails;
}

/* ------------------------------------------------------------------------------------------ */
/* listening/analysis helper: AUDIO_TEST_MAIN builds an executable                             */
/* ------------------------------------------------------------------------------------------ */
#ifdef AUDIO_TEST_MAIN
static void wr32(FILE *f, uint32_t v) { fputc(v & 255, f); fputc((v >> 8) & 255, f); fputc((v >> 16) & 255, f); fputc((v >> 24) & 255, f); }
static void wr16(FILE *f, uint16_t v) { fputc(v & 255, f); fputc((v >> 8) & 255, f); }
static int wav_write(const char *path, const int16_t *pcm, int frames, int ch, int rate) {
    FILE *f = fopen(path, "wb"); if (!f) { perror(path); return 1; }
    uint32_t bytes = (uint32_t)frames * (uint32_t)ch * 2;
    fwrite("RIFF", 1, 4, f); wr32(f, 36 + bytes); fwrite("WAVEfmt ", 1, 8, f); wr32(f, 16); wr16(f, 1); wr16(f, (uint16_t)ch);
    wr32(f, (uint32_t)rate); wr32(f, (uint32_t)rate * (uint32_t)ch * 2); wr16(f, (uint16_t)(ch * 2)); wr16(f, 16);
    fwrite("data", 1, 4, f); wr32(f, bytes); fwrite(pcm, 2, (size_t)frames * ch, f); fclose(f); return 0;
}
static int16_t f2i(float x) { return (int16_t)lrintf(clampf(x, -1.f, 1.f) * 32767.f); }
static void render_layer(const char *dir, const char *name, int secs, void (*setup)(float t)) {
    rt_reset(); audio_volume(1.f, 1.f);
    int frames = (secs * SR + 511) / 512 * 512; int16_t *o = (int16_t *)malloc((size_t)frames * 4); float buf[1024];
    for (int done = 0; done < frames; done += 512) { setup((float)done / SR); mix_block(buf, 512); for (int i = 0; i < 1024; i++) o[2 * done + i] = f2i(buf[i]); }
    char p[1024]; snprintf(p, sizeof p, "%s/layer_%s.wav", dir, name); wav_write(p, o, frames, 2, SR); free(o);
}
static void su_eng0(float t) { audio_engine(true, t / 6.f > 1 ? 1 : t / 6.f, 0.8f, 0); }
static void su_eng1(float t) { audio_engine(true, t / 6.f > 1 ? 1 : t / 6.f, 0.8f, 1); }
static void su_eng2(float t) { audio_engine(true, t / 6.f > 1 ? 1 : t / 6.f, 0.8f, 2); }
static void su_engoff(float t) { audio_engine(t < 1.5f, 0.5f, 0.5f, 0); }
static void su_skid(float t) { audio_skid(t < 1 ? 0 : (t < 3 ? 0.4f : 1.f)); }
static void su_siren(float t) { audio_siren(0.8f, t / 10.f * 2 - 1); }
static void su_city(float t) { (void)t; audio_ambience(1, 0, 0); }
static void su_wind(float t) { audio_ambience(0, t / 8.f > 1 ? 1 : t / 8.f, 0); }
static void su_sea(float t) { (void)t; audio_ambience(0, 0, 1); }
static void su_radio(float t) { audio_radio(t < 1 ? 0 : (t < 2 ? 1 : (t < 3 ? 4 : -1))); }

int main(int argc, char **argv) {
    int fails = audio_selftest();
    if (argc > 1) {
        const char *dir = argv[1]; char p[1024];
        for (int i = 0; i < SFX_N; i++) {
            int16_t *o = (int16_t *)malloc((size_t)g_sfx[i].n * 2);
            for (int k = 0; k < g_sfx[i].n; k++) o[k] = f2i(g_sfx[i].d[k]);
            snprintf(p, sizeof p, "%s/sfx_%02d.wav", dir, i); fails += wav_write(p, o, g_sfx[i].n, 1, SR); free(o);
        }
        for (int s = 0; s < NST; s++) {   /* 20 s that straddle the loop point (10 s before the wrap, 10 s after) */
            int frames = 20 * SSR, start = g_st[s].n - 10 * SSR; int16_t *o = (int16_t *)malloc((size_t)frames * 4);
            for (int k = 0; k < frames; k++) { int j = (start + k) % g_st[s].n; o[2 * k] = g_st[s].pcm[2 * j]; o[2 * k + 1] = g_st[s].pcm[2 * j + 1]; }
            snprintf(p, sizeof p, "%s/station_%d.wav", dir, s); fails += wav_write(p, o, frames, 2, SSR); free(o);
        }
        render_layer(dir, "engine0", 8, su_eng0); render_layer(dir, "engine1", 8, su_eng1); render_layer(dir, "engine2", 8, su_eng2);
        render_layer(dir, "engoff", 3, su_engoff); render_layer(dir, "skid", 5, su_skid); render_layer(dir, "siren", 10, su_siren);
        render_layer(dir, "city", 4, su_city); render_layer(dir, "wind", 8, su_wind); render_layer(dir, "sea", 12, su_sea);
        render_layer(dir, "radio", 5, su_radio);
        printf("audio: wrote WAV files to %s\n", dir);
    }
    audio_shutdown();
    return fails ? 1 : 0;
}
#endif
