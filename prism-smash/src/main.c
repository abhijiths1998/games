/*
 * Prism Smash — a neon brick-breaker with power-ups and chain reactions.
 * Native Linux game, C11 + SDL2 only. All graphics, font and audio are procedural.
 */
#include <SDL.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W 960
#define H 600
#define PI 3.14159265358979f
#define COLS 15
#define ROWS 13
#define BW 64
#define BH 26
#define BY0 80
#define TOP 44
#define PY 552
#define PTH 14
#define BALL_R 7.0f
#define MAX_BALLS 14
#define MAX_PU 12
#define MAX_BOLTS 16
#define MAX_PARTICLES 2400
#define MAX_POPUPS 32
#define MAX_RINGS 10
#define MAX_VOICES 24
#define TRAIL 16
#define NLEVELS 12
#define RATE 44100

/* ---------------------------------------------------------------- math / rng */
typedef struct { float x, y; } V2;
static V2 v2(float x, float y) { V2 r = {x, y}; return r; }
static V2 add(V2 a, V2 b) { return v2(a.x + b.x, a.y + b.y); }
static V2 mul(V2 a, float s) { return v2(a.x * s, a.y * s); }
static float len(V2 a) { return sqrtf(a.x * a.x + a.y * a.y); }
static V2 norm(V2 a) { float l = len(a); return l > 1e-5f ? mul(a, 1.0f / l) : v2(0, -1); }
static float dot(V2 a, V2 b) { return a.x * b.x + a.y * b.y; }
static V2 rot(V2 a, float t) { float c = cosf(t), s = sinf(t); return v2(a.x * c - a.y * s, a.x * s + a.y * c); }
static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

static Uint32 rng = 0x9E3779B9u;
static Uint32 xr(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
static float frand(void) { return (float)(xr() >> 8) * (1.0f / 16777216.0f); }
static float frange(float a, float b) { return a + (b - a) * frand(); }

typedef struct { Uint8 r, g, b; } Col;
static Col scale_col(Col c, float k) {
    return (Col){(Uint8)clampf(c.r * k, 0, 255), (Uint8)clampf(c.g * k, 0, 255), (Uint8)clampf(c.b * k, 0, 255)};
}
static Col hsv(float h, float s, float v) {
    h -= floorf(h);
    float k = h * 6, f = k - floorf(k), p = v * (1 - s), q = v * (1 - s * f), t = v * (1 - s * (1 - f)), r, g, b;
    switch ((int)k % 6) {
    case 0: r = v; g = t; b = p; break;
    case 1: r = q; g = v; b = p; break;
    case 2: r = p; g = v; b = t; break;
    case 3: r = p; g = q; b = v; break;
    case 4: r = t; g = p; b = v; break;
    default: r = v; g = p; b = q; break;
    }
    return (Col){(Uint8)(r * 255), (Uint8)(g * 255), (Uint8)(b * 255)};
}
/* brick colours 0-5, 6 silver (ball / 2-hit), 7 gold (3-hit) */
static const Col PAL[8] = {{40, 220, 255}, {255, 60, 200}, {120, 255, 90}, {255, 210, 60},
                           {150, 100, 255}, {255, 100, 80}, {200, 215, 245}, {255, 195, 70}};
static const Col C_EXPL = {255, 110, 40}, C_STEEL = {140, 152, 175}, C_WHITE = {255, 255, 255};

/* ---------------------------------------------------------------- audio */
enum { S_BRICK, S_CRACK, S_STEEL, S_PADDLE, S_WALL, S_BOOM, S_POWER, S_BAD, S_LASER, S_LOSE, S_LEVEL,
       S_PRISM, S_LIFE, S_LAUNCH, S_START, S_OVER, S_COUNT };
typedef struct { float *d; int n; } Sound;
static Sound snd[S_COUNT];
static struct { const float *d; int n; float pos, rate, vol; } voices[MAX_VOICES];
static SDL_AudioDeviceID adev;
static bool quiet; /* attract-mode demo is silent */

static void audio_cb(void *u, Uint8 *stream, int bytes) {
    (void)u;
    float *out = (float *)stream;
    int n = bytes / (int)sizeof(float);
    memset(out, 0, (size_t)bytes);
    for (int v = 0; v < MAX_VOICES; v++) {
        if (!voices[v].d) continue;
        for (int i = 0; i < n; i++) {
            int ip = (int)voices[v].pos;
            if (ip + 1 >= voices[v].n) { voices[v].d = NULL; break; }
            float fr = voices[v].pos - (float)ip;
            out[i] += (voices[v].d[ip] * (1 - fr) + voices[v].d[ip + 1] * fr) * voices[v].vol;
            voices[v].pos += voices[v].rate;
        }
    }
    for (int i = 0; i < n; i++) out[i] = tanhf(out[i]);
}

static void play(int id, float vol, float rate) {
    if (!adev || !snd[id].d || quiet) return;
    SDL_LockAudioDevice(adev);
    int slot = 0;
    float best = -1;
    for (int v = 0; v < MAX_VOICES; v++) {
        if (!voices[v].d) { slot = v; break; }
        if (voices[v].pos > best) { best = voices[v].pos; slot = v; }
    }
    voices[slot].d = snd[id].d; voices[slot].n = snd[id].n; voices[slot].pos = 0;
    voices[slot].rate = rate; voices[slot].vol = vol;
    SDL_UnlockAudioDevice(adev);
}

static float *mk(int id, float secs) {
    snd[id].n = (int)(secs * RATE);
    snd[id].d = calloc((size_t)snd[id].n, sizeof(float));
    return snd[id].d;
}
static float tri(float ph) { float x = fmodf(ph / (2 * PI), 1.0f); return 4.0f * fabsf(x - 0.5f) - 1.0f; }
static float noise(void) { return frange(-1, 1); }

static void build_sounds(void) {
    float *d, ph, lp;
    int n;
    d = mk(S_BRICK, 0.2f); n = snd[S_BRICK].n; ph = 0;
    for (int i = 0; i < n; i++) {
        float t = (float)i / RATE;
        ph += 2 * PI * 660.0f / RATE;
        d[i] = (sinf(ph) + 0.5f * sinf(ph * 2.01f) + 0.25f * sinf(ph * 3.02f)) * expf(-t * 24) * 0.38f;
    }
    d = mk(S_CRACK, 0.12f); n = snd[S_CRACK].n; ph = 0; lp = 0;
    for (int i = 0; i < n; i++) {
        float t = (float)i / RATE;
        lp += (noise() - lp) * 0.5f;
        ph += 2 * PI * 300.0f / RATE;
        d[i] = (lp * 0.8f + tri(ph) * 0.4f) * expf(-t * 34) * 0.6f;
    }
    d = mk(S_STEEL, 0.3f); n = snd[S_STEEL].n;
    for (int i = 0; i < n; i++) {
        float t = (float)i / RATE;
        d[i] = (sinf(2 * PI * 1180 * t) + 0.7f * sinf(2 * PI * 1790 * t) + 0.5f * sinf(2 * PI * 2410 * t)) * expf(-t * 16) * 0.22f;
    }
    d = mk(S_PADDLE, 0.14f); n = snd[S_PADDLE].n; ph = 0;
    for (int i = 0; i < n; i++) {
        float t = (float)i / RATE;
        ph += 2 * PI * (170.0f - t * 600) / RATE;
        d[i] = sinf(ph) * expf(-t * 24) * 0.8f;
    }
    d = mk(S_WALL, 0.06f); n = snd[S_WALL].n; ph = 0;
    for (int i = 0; i < n; i++) {
        float t = (float)i / RATE;
        ph += 2 * PI * 420.0f / RATE;
        d[i] = tri(ph) * expf(-t * 55) * 0.3f;
    }
    d = mk(S_BOOM, 0.7f); n = snd[S_BOOM].n; ph = 0; lp = 0;
    for (int i = 0; i < n; i++) {
        float t = (float)i / RATE;
        lp += (noise() - lp) * (0.35f * expf(-t * 4) + 0.04f);
        ph += 2 * PI * (90.0f * expf(-t * 3) + 30) / RATE;
        d[i] = (lp * 1.4f + sinf(ph) * 0.9f) * expf(-t * 5.5f) * 0.7f;
    }
    d = mk(S_POWER, 0.36f); n = snd[S_POWER].n; ph = 0;
    { const float nt[4] = {523.3f, 659.3f, 784.0f, 1046.5f};
      for (int i = 0; i < n; i++) {
        float t = (float)i / RATE; int k = (int)(t / 0.06f); if (k > 3) k = 3;
        float lt = t - k * 0.06f;
        ph += 2 * PI * nt[k] / RATE;
        d[i] = (sinf(ph) + 0.3f * sinf(ph * 2)) * expf(-lt * (k == 3 ? 7.0f : 14.0f)) * 0.36f;
      } }
    d = mk(S_BAD, 0.4f); n = snd[S_BAD].n; ph = 0;
    for (int i = 0; i < n; i++) {
        float t = (float)i / RATE;
        ph += 2 * PI * (330.0f * expf(-t * 3.5f) + 70) / RATE;
        d[i] = (sinf(ph) > 0 ? 0.35f : -0.35f) * expf(-t * 6) ;
    }
    d = mk(S_LASER, 0.16f); n = snd[S_LASER].n; ph = 0;
    for (int i = 0; i < n; i++) {
        float t = (float)i / RATE;
        ph += 2 * PI * (2000.0f * expf(-t * 14) + 250) / RATE;
        d[i] = sinf(ph) * expf(-t * 15) * 0.4f;
    }
    d = mk(S_LOSE, 0.9f); n = snd[S_LOSE].n; ph = 0;
    for (int i = 0; i < n; i++) {
        float t = (float)i / RATE;
        ph += 2 * PI * (520.0f * expf(-t * 2.6f) + 50) / RATE;
        d[i] = (tri(ph) * 0.7f + noise() * 0.12f) * expf(-t * 3.2f) * 0.6f;
    }
    d = mk(S_LEVEL, 1.2f); n = snd[S_LEVEL].n; ph = 0;
    { const float nt[6] = {392.0f, 493.9f, 587.3f, 784.0f, 987.8f, 1174.7f};
      for (int i = 0; i < n; i++) {
        float t = (float)i / RATE; int k = (int)(t / 0.1f); if (k > 5) k = 5;
        float lt = t - k * 0.1f;
        ph += 2 * PI * nt[k] / RATE;
        d[i] = (tri(ph) * 0.6f + sinf(ph * 2) * 0.2f) * expf(-lt * (k == 5 ? 3.0f : 10.0f)) * 0.45f;
      } }
    d = mk(S_PRISM, 0.45f); n = snd[S_PRISM].n;
    for (int i = 0; i < n; i++) {
        float t = (float)i / RATE;
        float trem = 0.6f + 0.4f * sinf(2 * PI * 22 * t);
        d[i] = (sinf(2 * PI * 1320 * t) + sinf(2 * PI * 1760 * t) * 0.8f + sinf(2 * PI * 2217 * t) * 0.6f) * trem * expf(-t * 8) * 0.2f;
    }
    d = mk(S_LIFE, 0.5f); n = snd[S_LIFE].n; ph = 0;
    for (int i = 0; i < n; i++) {
        float t = (float)i / RATE;
        ph += 2 * PI * (t < 0.12f ? 784.0f : 1175.0f) / RATE;
        d[i] = (sinf(ph) + 0.4f * sinf(ph * 2)) * expf(-(t < 0.12f ? t : t - 0.12f) * 9) * 0.4f;
    }
    d = mk(S_LAUNCH, 0.14f); n = snd[S_LAUNCH].n; ph = 0;
    for (int i = 0; i < n; i++) {
        float t = (float)i / RATE;
        ph += 2 * PI * (300.0f + t * 3500) / RATE;
        d[i] = sinf(ph) * expf(-t * 18) * 0.4f;
    }
    d = mk(S_START, 0.6f); n = snd[S_START].n; ph = 0;
    { const float nt[4] = {261.6f, 329.6f, 392.0f, 523.3f};
      for (int i = 0; i < n; i++) {
        float t = (float)i / RATE; int k = (int)(t / 0.11f); if (k > 3) k = 3;
        float lt = t - k * 0.11f;
        ph += 2 * PI * nt[k] / RATE;
        d[i] = tri(ph) * expf(-lt * (k == 3 ? 4.0f : 12.0f)) * 0.4f;
      } }
    d = mk(S_OVER, 1.5f); n = snd[S_OVER].n; ph = 0;
    { const float nt[3] = {392.0f, 311.1f, 233.1f};
      for (int i = 0; i < n; i++) {
        float t = (float)i / RATE; int k = (int)(t / 0.45f); if (k > 2) k = 2;
        float lt = t - k * 0.45f;
        ph += 2 * PI * nt[k] / RATE;
        d[i] = (tri(ph) * 0.7f + sinf(ph * 0.5f) * 0.3f) * expf(-lt * (k == 2 ? 2.2f : 5.0f)) * 0.45f;
      } }
}

/* ---------------------------------------------------------------- pixel font (5x7) */
static const char FONT_CHARS[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ:.-!/?, +";
static const unsigned char FONT[][7] = {
    {14,17,19,21,25,17,14},{4,12,4,4,4,4,14},{14,17,1,2,4,8,31},{31,2,4,2,1,17,14},{2,6,10,18,31,2,2},
    {31,16,30,1,1,17,14},{6,8,16,30,17,17,14},{31,1,2,4,8,8,8},{14,17,17,14,17,17,14},{14,17,17,15,1,2,12},
    {14,17,17,31,17,17,17},{30,17,17,30,17,17,30},{14,17,16,16,16,17,14},{28,18,17,17,17,18,28},
    {31,16,16,30,16,16,31},{31,16,16,30,16,16,16},{14,17,16,23,17,17,15},{17,17,17,31,17,17,17},
    {14,4,4,4,4,4,14},{7,2,2,2,2,18,12},{17,18,20,24,20,18,17},{16,16,16,16,16,16,31},
    {17,27,21,21,17,17,17},{17,17,25,21,19,17,17},{14,17,17,17,17,17,14},{30,17,17,30,16,16,16},
    {14,17,17,17,21,18,13},{30,17,17,30,20,18,17},{15,16,16,14,1,1,30},{31,4,4,4,4,4,4},
    {17,17,17,17,17,17,14},{17,17,17,17,17,10,4},{17,17,17,21,21,21,10},{17,17,10,4,10,17,17},
    {17,17,17,10,4,4,4},{31,1,2,4,8,16,31},
    {0,12,12,0,12,12,0},{0,0,0,0,0,12,12},{0,0,0,31,0,0,0},{4,4,4,4,4,0,4},{1,1,2,4,8,16,16},
    {14,17,1,2,4,0,4},{0,0,0,0,12,4,8},{0,0,0,0,0,0,0},{0,4,4,31,4,4,0},
};

static SDL_Renderer *ren;
static float shx, shy; /* screen shake offset applied by world draw helpers */

static float text_w(const char *s, float sc) { return (float)strlen(s) * 6 * sc - sc; }
static void text(const char *s, float x, float y, float sc, Col c, float a, bool center) {
    if (center) x -= text_w(s, sc) / 2;
    SDL_SetRenderDrawColor(ren, c.r, c.g, c.b, (Uint8)(clampf(a, 0, 1) * 255));
    for (; *s; s++, x += 6 * sc) {
        char ch = *s >= 'a' && *s <= 'z' ? (char)(*s - 32) : *s;
        const char *at = strchr(FONT_CHARS, ch);
        if (!at || !ch) continue;
        const unsigned char *gl = FONT[at - FONT_CHARS];
        for (int row = 0; row < 7; row++)
            for (int col = 0; col < 5; col++)
                if (gl[row] & (16 >> col)) {
                    SDL_FRect px = {x + col * sc, y + row * sc, sc, sc};
                    SDL_RenderFillRectF(ren, &px);
                }
    }
}
static void text_sh(const char *s, float x, float y, float sc, Col c, float a) {
    text(s, x + sc * 0.7f, y + sc * 0.7f, sc, (Col){0, 0, 0}, a * 0.7f, true);
    text(s, x, y, sc, c, a, true);
}

/* ---------------------------------------------------------------- levels */
enum { BT_NORMAL, BT_MULTI, BT_EXPL, BT_STEEL, BT_PRISM };
/* 1-6 normal colours, h 2-hit, H 3-hit, X explosive, # steel, * prism, . empty */
static const char *const LV[NLEVELS][ROWS + 1] = {
    {"...............", ".1111111111111.", ".2222222222222.", ".3333*33*33333.", ".4444444444444.", ".5555555555555.", NULL},
    {"111111111111111", "222222222222222", "333333333333333", "444444444444444", "555555555555555", "666666666666666",
     "...............", "#.............#", NULL},
    {"1X1X1X1X1X1X1X1", "222222222222222", "3333X33333X3333", "444444444444444", "55555555X555555", NULL},
    {"h1h1h1h1h1h1h1h", "1h1h1h1h1h1h1h1", "h2h2h2h2h2h2h2h", "2h2h2h2h2h2h2h2", "...............", "..H.H.H.H.H.H..", NULL},
    {"#.............#", "#3*3*3*3*3*3*3#", "#.4.4.4.4.4.4.#", "#5555555555555#", "#.............#", "#6X6X6X6X6X6X6#", NULL},
    {"#.............#", ".......1.......", "......222......", ".....33333.....", "....4444444....", "...555555555...",
     "..66666666666..", "...555555555...", "....4H4*4H4....", ".....33333.....", NULL},
    {"####.#####.####", "#h1h1h1h1h1h1h#", "#1X1X1X1X1X1X1#", "#h.h.h.h.h.h.h#", "#.............#", "#4444444444444#", NULL},
    {"1111.......1111", ".2222.....2222.", "..3333...3333..", "...4444.4444...", "....5555555....", ".....66X66.....", "......H#H......", NULL},
    {"XX1XX1XX1XX1XX1", "222222222222222", "333X333X333X333", "444444444444444", "5X555X555X555X5", "hhhhhhhhhhhhhhh", NULL},
    {"1.1.1.1.1.1.1.1", "#2#2#2#2#2#2#2#", "3.3.3.3.3.3.3.3", "#4#4#4#4#4#4#4#", "5.5.5.5.5.5.5.5", "h6h6h6h6h6h6h6h", NULL},
    {"*2*2*2*2*2*2*2*", "3H3H3H3H3H3H3H3", "#.............#", ".4*4*4*4*4*4*4.", "5X5X5X5X5X5X5X5", NULL},
    {"..1111...1111..", ".222222.222222.", "3333333X3333333", "H4444444444444H", ".h55555555555h.", "..66666*66666..",
     "...H3333333H...", ".....22222.....", ".......*.......", NULL},
};
static const char *const LV_NAME[NLEVELS] = {"WARM UP", "RAINBOW", "BOOM TOWN", "CHECKERBOARD", "PRISM HALL", "DIAMOND",
                                             "FORTRESS", "V FORMATION", "CHAIN REACTION", "STEEL MAZE", "PRISM STORM", "FINALE"};

static int validate_levels(void) {
    int bad = 0;
    for (int l = 0; l < NLEVELS; l++) {
        int r = 0, breakable = 0;
        for (; LV[l][r]; r++) {
            if (strlen(LV[l][r]) != COLS) { fprintf(stderr, "level %d row %d has length %zu\n", l + 1, r, strlen(LV[l][r])); bad++; }
            for (const char *p = LV[l][r]; *p; p++) if (*p != '.' && *p != '#') breakable++;
        }
        if (r > ROWS || !breakable) { fprintf(stderr, "level %d: rows %d breakable %d\n", l + 1, r, breakable); bad++; }
    }
    return bad;
}

/* ---------------------------------------------------------------- game state */
typedef enum { ST_TITLE, ST_PLAY, ST_PAUSE, ST_OVER } State;
typedef enum { PH_INTRO, PH_RUN, PH_DYING, PH_CLEAR } Phase;
typedef struct { Uint8 type, hp, maxhp, col; bool alive; float flash, fuse; } Brick;
typedef struct { V2 p, v; bool alive, stuck; float off, stuckT, wallCd; int col; V2 trail[TRAIL]; } Ball;
typedef struct { float x, y; int type; bool alive; } Capsule;
typedef struct { float x, y; bool alive; } Bolt;
typedef struct { V2 p, v; float life, max, size; Col c; bool shard; } Particle;
typedef struct { char s[24]; float x, y, life, max, sc; Col c; } Popup;
typedef struct { V2 p; float t, max; Col c; } Ring;
typedef struct { float axis, tx; bool hasT, fire, held; } Input;
enum { PU_MULTI, PU_WIDE, PU_LASER, PU_STICKY, PU_SLOW, PU_LIFE, PU_SHRINK, PU_COUNT };
static const char PU_LETTER[PU_COUNT] = {'M', 'W', 'L', 'C', 'T', '+', '-'};
static const Col PU_COL[PU_COUNT] = {{255, 70, 210}, {60, 220, 255}, {255, 70, 70}, {110, 255, 110},
                                     {90, 140, 255}, {255, 215, 70}, {170, 120, 170}};
static const char *const PU_NAME[PU_COUNT] = {"MULTIBALL", "WIDE", "LASER", "CATCH", "SLOW-MO", "EXTRA LIFE", "SHRINK!"};

static struct {
    State st; Phase ph;
    float phT, stateT, t;
    int level, lives, combo, maxCombo, chain, remaining, score, best;
    float chainT, px, padTarget, pw, padSquash, laserCd;
    float tWide, tLaser, tSticky, tSlow, tShrink;
    float boost, shake, pulse, apOff, flashT;
    bool demo, newBest;
    Brick br[ROWS][COLS];
    Ball balls[MAX_BALLS];
    Capsule pus[MAX_PU];
    Bolt bolts[MAX_BOLTS];
    Particle parts[MAX_PARTICLES];
    int partHead;
    Popup pops[MAX_POPUPS];
    int popHead;
    Ring rings[MAX_RINGS];
    int ringHead;
} G;

static bool no_save;
static char *save_path;

static void load_best(void) {
    char *pref = SDL_GetPrefPath("doomedl", "prism-smash");
    if (!pref) return;
    size_t n = strlen(pref) + 8;
    save_path = malloc(n);
    snprintf(save_path, n, "%sbest", pref);
    SDL_free(pref);
    FILE *f = fopen(save_path, "r");
    if (f) { int v = 0; if (fscanf(f, "%d", &v) == 1 && v > 0) G.best = v; fclose(f); }
}
static void save_best(void) {
    if (no_save || !save_path) return;
    FILE *f = fopen(save_path, "w");
    if (f) { fprintf(f, "%d\n", G.best); fclose(f); }
}

/* ---------------------------------------------------------------- effects */
static void spark(V2 p, V2 v, float life, float size, Col c) {
    Particle *q = &G.parts[G.partHead++ % MAX_PARTICLES];
    q->p = p; q->v = v; q->life = q->max = life; q->size = size; q->c = c; q->shard = false;
}
static void shard(V2 p, V2 v, float life, float size, Col c) {
    spark(p, v, life, size, c);
    G.parts[(G.partHead - 1) % MAX_PARTICLES].shard = true;
}
static void burst(V2 p, Col c, int n, float spd, float life) {
    for (int i = 0; i < n; i++) {
        float a = frange(0, 2 * PI), s = frange(0.2f, 1) * spd;
        spark(p, v2(cosf(a) * s, sinf(a) * s), frange(0.4f, 1) * life, frange(6, 14), c);
    }
}
static void add_ring(V2 p, Col c, float max) {
    Ring *r = &G.rings[G.ringHead++ % MAX_RINGS];
    r->p = p; r->c = c; r->t = 0; r->max = max;
}
static void popup(const char *s, V2 p, float sc, Col c, float life) {
    Popup *q = &G.pops[G.popHead++ % MAX_POPUPS];
    snprintf(q->s, sizeof q->s, "%s", s);
    q->x = p.x; q->y = p.y; q->life = q->max = life; q->sc = sc; q->c = c;
}
static void update_fx(float dt) {
    for (int i = 0; i < MAX_PARTICLES; i++) {
        Particle *q = &G.parts[i];
        if (q->life <= 0) continue;
        q->life -= dt;
        if (q->shard) { q->v.y += 520 * dt; q->p = add(q->p, mul(q->v, dt)); }
        else { q->v = mul(q->v, expf(-dt * 2.2f)); q->p = add(q->p, mul(q->v, dt)); }
    }
    for (int i = 0; i < MAX_POPUPS; i++) if (G.pops[i].life > 0) { G.pops[i].life -= dt; G.pops[i].y -= 34 * dt; }
    for (int i = 0; i < MAX_RINGS; i++) if (G.rings[i].t < G.rings[i].max) G.rings[i].t += dt;
}

/* ---------------------------------------------------------------- game logic */
static V2 brick_center(int c, int r) { return v2(c * BW + BW / 2.0f, BY0 + r * BH + BH / 2.0f); }
static float ball_speed(void) {
    float base = 330.0f + (float)(G.level % NLEVELS) * 9 + (float)(G.level / NLEVELS) * 45;
    return fminf(base + G.boost, 640.0f);
}
static float combo_mult(void) { return 1.0f + (float)(G.combo > 28 ? 28 : G.combo) / 4.0f; }
static float combo_rate(void) { return 1.0f + 0.055f * (float)(G.combo > 18 ? 18 : G.combo); }

static void add_score(int base, V2 at, bool show) {
    int v = (int)((float)base * combo_mult());
    G.score += v;
    if (G.score > G.best) { G.best = G.score; G.newBest = true; }
    if (show) {
        char b[24]; snprintf(b, sizeof b, "+%d", v);
        float m = combo_mult();
        popup(b, at, m >= 4 ? 2.4f : 1.8f, hsv(0.5f - (m - 1) * 0.07f, 0.6f, 1), 0.8f);
    }
}

static Ball *new_ball(void) {
    for (int i = 0; i < MAX_BALLS; i++) if (!G.balls[i].alive) { memset(&G.balls[i], 0, sizeof G.balls[i]); G.balls[i].alive = true; return &G.balls[i]; }
    return NULL;
}
static int count_balls(void) { int n = 0; for (int i = 0; i < MAX_BALLS; i++) n += G.balls[i].alive; return n; }
static void init_trail(Ball *b) { for (int i = 0; i < TRAIL; i++) b->trail[i] = b->p; }

static void spawn_stuck(void) {
    for (int i = 0; i < MAX_BALLS; i++) G.balls[i].alive = false;
    Ball *b = new_ball();
    b->stuck = true; b->col = 6; b->off = 0; b->p = v2(G.px, PY - BALL_R); init_trail(b);
}

static void split_ball(const Ball *src, float ang, int col) {
    Ball *n = new_ball();
    if (!n) return;
    *n = *src;
    n->stuck = false; n->col = col;
    n->v = rot(len(src->v) > 1 ? src->v : v2(0, -ball_speed()), ang);
}

static void launch_ball(Ball *b) {
    float a = clampf(b->off / (G.pw / 2), -1, 1) * 0.9f;
    if (fabsf(a) < 0.12f) a = frange(-0.3f, 0.3f);
    b->stuck = false;
    b->v = v2(sinf(a) * ball_speed(), -cosf(a) * ball_speed());
    play(S_LAUNCH, 0.5f, 1);
}

static void drop_capsule(V2 at, float chance) {
    if (frand() > chance) return;
    int slot = -1;
    for (int i = 0; i < MAX_PU; i++) if (!G.pus[i].alive) { slot = i; break; }
    if (slot < 0) return;
    static const int wt[PU_COUNT] = {20, 18, 14, 14, 12, 5, 8};
    int tot = 0, r;
    for (int i = 0; i < PU_COUNT; i++) tot += wt[i];
    r = (int)(xr() % (Uint32)tot);
    int type = 0;
    for (int i = 0; i < PU_COUNT; i++) { if (r < wt[i]) { type = i; break; } r -= wt[i]; }
    G.pus[slot] = (Capsule){at.x, at.y, type, true};
}

static void apply_capsule(int type) {
    V2 at = v2(G.px, PY - 30);
    popup(PU_NAME[type], at, 2.2f, PU_COL[type], 1.2f);
    add_ring(v2(G.px, PY), PU_COL[type], 0.5f);
    burst(v2(G.px, PY), PU_COL[type], 20, 260, 0.6f);
    switch (type) {
    case PU_MULTI: {
        int n = MAX_BALLS;
        Ball snap[MAX_BALLS];
        memcpy(snap, G.balls, sizeof snap);
        for (int i = 0; i < n; i++) if (snap[i].alive) {
            if (snap[i].stuck) launch_ball(&G.balls[i]);
            Ball cur = G.balls[i];
            split_ball(&cur, 0.38f, (int)(xr() % 6)); split_ball(&cur, -0.38f, (int)(xr() % 6));
        }
        break; }
    case PU_WIDE: G.tWide = 15; G.tShrink = 0; break;
    case PU_LASER: G.tLaser = 14; break;
    case PU_STICKY: G.tSticky = 16; break;
    case PU_SLOW: G.tSlow = 8; break;
    case PU_LIFE: if (G.lives < 9) G.lives++; break;
    default: G.tShrink = 10; G.tWide = 0; break;
    }
    play(type == PU_SHRINK ? S_BAD : type == PU_LIFE ? S_LIFE : S_POWER, 0.8f, 1);
}

static void kill_brick(int c, int r, Ball *src, bool by_expl);

static void boom(int c, int r) {
    V2 p = brick_center(c, r);
    G.shake = fminf(G.shake + 9, 16);
    G.pulse = 1;
    add_ring(p, C_EXPL, 0.45f);
    burst(p, C_EXPL, 34, 420, 0.7f);
    burst(p, (Col){255, 230, 120}, 14, 300, 0.4f);
    play(S_BOOM, 0.85f, frange(0.9f, 1.1f));
    for (int dr = -1; dr <= 1; dr++)
        for (int dc = -1; dc <= 1; dc++) {
            int rr = r + dr, cc = c + dc;
            if (rr < 0 || rr >= ROWS || cc < 0 || cc >= COLS || (!dr && !dc)) continue;
            Brick *n = &G.br[rr][cc];
            if (n->alive && n->type != BT_STEEL && n->fuse < 0) n->fuse = 0.08f;
        }
}

static void kill_brick(int c, int r, Ball *src, bool by_expl) {
    Brick *b = &G.br[r][c];
    V2 p = brick_center(c, r);
    b->alive = false;
    G.remaining--;
    Col col = b->type == BT_EXPL ? C_EXPL : b->type == BT_PRISM ? hsv(G.t * 0.5f, 0.5f, 1) : PAL[b->col];
    for (int i = 0; i < 12; i++) {
        float a = frange(0, 2 * PI), s = frange(60, 300);
        shard(add(p, v2(frange(-24, 24), frange(-8, 8))), v2(cosf(a) * s, sinf(a) * s - 120), frange(0.5f, 1.1f), frange(2.5f, 5.5f), scale_col(col, frange(0.6f, 1.1f)));
    }
    burst(p, col, 6, 200, 0.5f);
    int base = b->type == BT_MULTI ? 30 : b->type == BT_EXPL ? 25 : b->type == BT_PRISM ? 40 : 10;
    if (by_expl) {
        G.chain++; G.chainT = 0.8f;
        base += G.chain * 6;
        if (G.chain >= 3 && (G.chain & 1)) {
            char s[24]; snprintf(s, sizeof s, "CHAIN X%d", G.chain);
            popup(s, v2(p.x, p.y - 10), 2.6f, (Col){255, 190, 70}, 1.1f);
        }
    }
    add_score(base, p, !by_expl || G.chain < 3);
    if (b->type == BT_EXPL) boom(c, r);
    else if (b->type == BT_PRISM) {
        play(S_PRISM, 0.7f, 1);
        add_ring(p, hsv(G.t, 0.5f, 1), 0.5f);
        if (src) {
            Ball cur = *src;
            split_ball(&cur, 0.45f, (int)(xr() % 6)); split_ball(&cur, -0.45f, (int)(xr() % 6));
        } else {
            for (int k = 0; k < 2; k++) {
                Ball *n = new_ball();
                if (!n) break;
                n->p = p; n->col = (int)(xr() % 6); init_trail(n);
                float a = frange(-0.9f, 0.9f) + (k ? 0.5f : -0.5f);
                n->v = v2(sinf(a) * ball_speed(), -cosf(a) * ball_speed());
            }
        }
    } else {
        play(S_BRICK, 0.55f, combo_rate());
    }
    G.pulse = fminf(G.pulse + 0.3f, 1);
    drop_capsule(p, b->type == BT_EXPL ? 0.22f : b->type == BT_PRISM ? 0.3f : 0.11f);
}

static void hit_brick(int c, int r, Ball *src) {
    Brick *b = &G.br[r][c];
    if (!b->alive) return;
    V2 p = brick_center(c, r);
    if (b->type == BT_STEEL) {
        b->flash = 1;
        play(S_STEEL, 0.5f, combo_rate());
        for (int i = 0; i < 5; i++) spark(p, v2(frange(-200, 200), frange(-200, 200)), 0.3f, 5, (Col){255, 240, 200});
        return;
    }
    if (src) { G.combo++; if (G.combo > G.maxCombo) G.maxCombo = G.combo; G.boost = fminf(G.boost + 1.4f, 110); }
    if (b->type == BT_MULTI && b->hp > 1) {
        b->hp--; b->flash = 1;
        play(S_CRACK, 0.6f, combo_rate());
        for (int i = 0; i < 6; i++) shard(p, v2(frange(-180, 180), frange(-220, 0)), 0.5f, 3, PAL[b->col]);
        add_score(5, p, false);
        return;
    }
    kill_brick(c, r, src, false);
}

static void load_level(int n) {
    memset(G.br, 0, sizeof G.br);
    G.remaining = 0;
    const char *const *rows = LV[n % NLEVELS];
    for (int r = 0; r < ROWS && rows[r]; r++)
        for (int c = 0; c < COLS && rows[r][c]; c++) {
            char ch = rows[r][c];
            Brick *b = &G.br[r][c];
            if (ch == '.') continue;
            b->alive = true; b->fuse = -1; b->hp = b->maxhp = 1; b->col = 0;
            if (ch >= '1' && ch <= '6') { b->type = BT_NORMAL; b->col = (Uint8)(ch - '1'); }
            else if (ch == 'h') { b->type = BT_MULTI; b->hp = b->maxhp = 2; b->col = 6; }
            else if (ch == 'H') { b->type = BT_MULTI; b->hp = b->maxhp = 3; b->col = 7; }
            else if (ch == 'X') b->type = BT_EXPL;
            else if (ch == '#') b->type = BT_STEEL;
            else b->type = BT_PRISM;
            if (b->type != BT_STEEL) G.remaining++;
        }
    memset(G.pus, 0, sizeof G.pus);
    memset(G.bolts, 0, sizeof G.bolts);
    G.tWide = G.tLaser = G.tSticky = G.tSlow = G.tShrink = 0;
    G.combo = 0; G.chain = 0; G.chainT = 0; G.boost = 0;
    G.ph = PH_INTRO; G.phT = 0;
    spawn_stuck();
}

static void reset_game(void) {
    G.score = 0; G.lives = 3; G.level = 0; G.maxCombo = 0; G.newBest = false;
    G.pw = 100; G.px = G.padTarget = W / 2.0f; G.shake = 0; G.pulse = 0;
    memset(G.parts, 0, sizeof G.parts);
    memset(G.pops, 0, sizeof G.pops);
    memset(G.rings, 0, sizeof G.rings);
    for (int i = 0; i < MAX_RINGS; i++) G.rings[i].t = G.rings[i].max = 1;
}

static void start_demo(void) {
    int best = G.best;
    reset_game();
    G.best = best;
    G.demo = true; quiet = true; G.level = (int)(xr() % NLEVELS);
    load_level(G.level);
    G.st = ST_TITLE;
}
static void start_game(void) {
    reset_game();
    G.demo = false; quiet = false;
    load_level(0);
    G.st = ST_PLAY; G.stateT = 0;
    play(S_START, 0.7f, 1);
}
static void end_game(void) {
    G.st = ST_OVER; G.stateT = 0; G.ph = PH_RUN;
    if (G.score > 0 && G.score >= G.best) G.best = G.score;
    save_best();
    play(S_OVER, 0.8f, 1);
}

static void paddle_bounce(Ball *b) {
    float t = clampf((b->p.x - G.px) / (G.pw / 2), -1, 1);
    float a = t * 1.05f, sp = ball_speed();
    b->v = v2(sinf(a) * sp, -cosf(a) * sp);
    b->p.y = PY - BALL_R;
    G.combo = 0; G.boost = fminf(G.boost + 0.8f, 110);
    G.padSquash = 5;
    G.apOff = frange(-0.75f, 0.75f);
    play(S_PADDLE, 0.7f, 1);
    for (int i = 0; i < 6; i++) spark(v2(b->p.x, PY), v2(frange(-160, 160), frange(-220, -40)), 0.35f, 6, PAL[0]);
    if (G.tSticky > 0) { b->stuck = true; b->off = b->p.x - G.px; b->stuckT = 0; }
}

static void update_ball(Ball *b, float dt, float ts) {
    b->stuckT += dt; b->wallCd -= dt;
    if (b->stuck) { b->off = clampf(b->off, -G.pw / 2 + 4, G.pw / 2 - 4); b->p = v2(G.px + b->off, PY - BALL_R); b->trail[0] = b->p; return; }
    float sp = ball_speed() * ts;
    b->v = mul(norm(b->v), sp);
    int n = (int)ceilf(sp * dt / 3.0f);
    if (n < 1) n = 1;
    for (int s = 0; s < n; s++) {
        b->p = add(b->p, mul(b->v, dt / (float)n));
        if (b->p.x < BALL_R) { b->p.x = BALL_R; b->v.x = fabsf(b->v.x); if (b->wallCd <= 0) { play(S_WALL, 0.5f, 1); b->wallCd = 0.06f; } }
        if (b->p.x > W - BALL_R) { b->p.x = W - BALL_R; b->v.x = -fabsf(b->v.x); if (b->wallCd <= 0) { play(S_WALL, 0.5f, 1); b->wallCd = 0.06f; } }
        if (b->p.y < TOP + BALL_R) { b->p.y = TOP + BALL_R; b->v.y = fabsf(b->v.y); if (b->wallCd <= 0) { play(S_WALL, 0.5f, 1.2f); b->wallCd = 0.06f; } }
        if (b->v.y > 0 && b->p.y + BALL_R >= PY && b->p.y < PY + PTH * 0.6f && fabsf(b->p.x - G.px) < G.pw / 2 + BALL_R * 0.6f) {
            paddle_bounce(b);
            if (b->stuck) return;
        }
        int c0 = (int)floorf((b->p.x - BALL_R) / BW), c1 = (int)floorf((b->p.x + BALL_R) / BW);
        int r0 = (int)floorf((b->p.y - BALL_R - BY0) / BH), r1 = (int)floorf((b->p.y + BALL_R - BY0) / BH);
        if (c0 < 0) c0 = 0;
        if (c1 >= COLS) c1 = COLS - 1;
        if (r0 < 0) r0 = 0;
        if (r1 >= ROWS) r1 = ROWS - 1;
        float bestd = BALL_R * BALL_R;
        int bc = -1, br = -1;
        V2 bn = v2(0, 0);
        float bdist = 0;
        for (int r = r0; r <= r1; r++)
            for (int c = c0; c <= c1; c++) {
                if (!G.br[r][c].alive) continue;
                float x = c * BW + 2, y = BY0 + r * BH + 2;
                float cx = clampf(b->p.x, x, x + BW - 4), cy = clampf(b->p.y, y, y + BH - 4);
                float dx = b->p.x - cx, dy = b->p.y - cy, d2 = dx * dx + dy * dy;
                if (d2 < bestd) {
                    bestd = d2; bc = c; br = r; bdist = sqrtf(d2);
                    bn = bdist > 1e-4f ? v2(dx / bdist, dy / bdist) : mul(norm(b->v), -1);
                }
            }
        if (bc >= 0) {
            if (dot(b->v, bn) < 0) b->v = add(b->v, mul(bn, -2 * dot(b->v, bn)));
            b->p = add(b->p, mul(bn, BALL_R - bdist));
            hit_brick(bc, br, b);
        }
    }
    if (fabsf(b->v.y) < 0.22f * sp) { b->v.y = (b->v.y < 0 ? -1 : 1) * 0.22f * sp; }
    if (b->p.y > H + 30) b->alive = false;
    for (int i = TRAIL - 1; i > 0; i--) b->trail[i] = b->trail[i - 1];
    b->trail[0] = b->p;
}

static void next_level(void) {
    G.level++;
    load_level(G.level);
    play(S_START, 0.5f, 1.3f);
}

static void step_game(float dt, Input in) {
    G.phT += dt;
    float tw = 100.0f * (G.tWide > 0 ? 1.6f : 1) * (G.tShrink > 0 ? 0.6f : 1);
    G.pw += (tw - G.pw) * fminf(1, dt * 10);
    if (in.hasT) G.padTarget = in.tx;
    G.padTarget = clampf(G.padTarget + in.axis * 780 * dt, G.pw / 2, W - G.pw / 2);
    G.px += (G.padTarget - G.px) * fminf(1, dt * 28);
    G.px = clampf(G.px, G.pw / 2, W - G.pw / 2);
    G.padSquash *= expf(-dt * 14);
    G.tWide -= dt; G.tLaser -= dt; G.tSticky -= dt; G.tSlow -= dt; G.tShrink -= dt; G.laserCd -= dt;
    G.chainT -= dt;
    if (G.chainT <= 0) G.chain = 0;
    if (G.ph == PH_INTRO && G.phT > 1.5f) { G.ph = PH_RUN; G.phT = 0; }

    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++) {
            Brick *b = &G.br[r][c];
            if (!b->alive) continue;
            b->flash = fmaxf(0, b->flash - dt * 4);
            if (b->fuse >= 0) {
                b->fuse -= dt;
                if (b->fuse <= 0) {
                    b->fuse = -1;
                    if (b->type == BT_MULTI && b->hp > 1) { b->hp--; b->flash = 1; play(S_CRACK, 0.5f, 1); }
                    else kill_brick(c, r, NULL, true);
                }
            }
        }

    float ts = G.tSlow > 0 ? 0.62f : 1.0f;
    if (G.ph == PH_INTRO || G.ph == PH_RUN) {
        for (int i = 0; i < MAX_BALLS; i++) if (G.balls[i].alive && G.balls[i].stuck) update_ball(&G.balls[i], dt, ts);
    }
    if (G.ph == PH_RUN) {
        bool launched = false;
        if (in.fire) {
            for (int i = 0; i < MAX_BALLS; i++)
                if (G.balls[i].alive && G.balls[i].stuck) { launch_ball(&G.balls[i]); launched = true; }
        }
        if (launched) G.laserCd = 0.25f;
        else if (G.tLaser > 0 && (in.held || in.fire) && G.laserCd <= 0) {
            G.laserCd = 0.26f;
            for (int k = -1; k <= 1; k += 2)
                for (int i = 0; i < MAX_BOLTS; i++)
                    if (!G.bolts[i].alive) { G.bolts[i] = (Bolt){G.px + k * (G.pw / 2 - 8), PY - 6, true}; break; }
            play(S_LASER, 0.5f, frange(0.95f, 1.1f));
        }
        for (int i = 0; i < MAX_BALLS; i++) if (G.balls[i].alive && !G.balls[i].stuck) update_ball(&G.balls[i], dt, ts);

        for (int i = 0; i < MAX_BOLTS; i++) {
            Bolt *bo = &G.bolts[i];
            if (!bo->alive) continue;
            bo->y -= 760 * dt;
            int c = (int)(bo->x / BW), r = (int)floorf((bo->y - BY0) / BH);
            if (bo->y < TOP) bo->alive = false;
            else if (c >= 0 && c < COLS && r >= 0 && r < ROWS && G.br[r][c].alive) {
                bo->alive = false;
                burst(v2(bo->x, bo->y), PU_COL[PU_LASER], 6, 200, 0.3f);
                hit_brick(c, r, NULL);
            }
        }
        for (int i = 0; i < MAX_PU; i++) {
            Capsule *p = &G.pus[i];
            if (!p->alive) continue;
            p->y += 150 * dt;
            if (p->y > PY - 8 && p->y < PY + PTH + 8 && fabsf(p->x - G.px) < G.pw / 2 + 22) { p->alive = false; apply_capsule(p->type); }
            else if (p->y > H + 20) p->alive = false;
        }

        if (count_balls() == 0) {
            G.ph = PH_DYING; G.phT = 0;
            G.shake = 10;
            if (!G.demo) G.lives--;
            play(S_LOSE, 0.8f, 1);
            for (int i = 0; i < 26; i++)
                shard(v2(G.px + frange(-G.pw / 2, G.pw / 2), PY + 6), v2(frange(-260, 260), frange(-420, -100)), frange(0.6f, 1.3f), frange(3, 6), PAL[0]);
            add_ring(v2(G.px, PY), PAL[0], 0.6f);
        } else if (G.remaining <= 0) {
            G.ph = PH_CLEAR; G.phT = 0;
            for (int i = 0; i < MAX_BALLS; i++) if (G.balls[i].alive) { burst(G.balls[i].p, PAL[G.balls[i].col], 12, 250, 0.6f); G.balls[i].alive = false; }
            int bonus = 500 + 100 * G.lives;
            if (!G.demo) { G.score += bonus; if (G.score > G.best) { G.best = G.score; G.newBest = true; } }
            char s[24]; snprintf(s, sizeof s, "BONUS +%d", bonus);
            popup(s, v2(W / 2.0f, 330), 3, (Col){255, 220, 90}, 2.0f);
            play(S_LEVEL, 0.8f, 1);
        }
    }
    if (G.ph == PH_DYING && G.phT > 1.1f) {
        if (G.lives <= 0 && !G.demo) end_game();
        else {
            G.ph = PH_RUN; G.phT = 0; G.combo = 0; G.boost = 0;
            G.tWide = G.tLaser = G.tSticky = G.tSlow = G.tShrink = 0;
            memset(G.pus, 0, sizeof G.pus); memset(G.bolts, 0, sizeof G.bolts);
            spawn_stuck();
        }
    }
    if (G.ph == PH_CLEAR) {
        if (frand() < dt * 9) {
            V2 p = v2(frange(100, W - 100), frange(120, 380));
            Col c = hsv(frand(), 0.7f, 1);
            burst(p, c, 24, 330, 0.9f); add_ring(p, c, 0.5f);
            G.shake = fmaxf(G.shake, 2);
        }
        if (G.phT > 2.6f) next_level();
    }
}

/* ---------------------------------------------------------------- autopilot (demo + selftest) */
static Input autopilot(int mode) {
    Input in = {0};
    Ball *tb = NULL, *any = NULL;
    float besty = -1e9f;
    bool stuck = false, ready = false;
    for (int i = 0; i < MAX_BALLS; i++) {
        Ball *b = &G.balls[i];
        if (!b->alive) continue;
        any = b;
        if (b->stuck) { stuck = true; if (b->stuckT > 0.35f) ready = true; }
        else if (b->v.y > 0 && b->p.y > besty) { besty = b->p.y; tb = b; }
    }
    float want = G.padTarget;
    if (mode == 1) {
        if (any) want = any->p.x < W / 2.0f ? W - 30.0f : 30.0f;
    } else {
        Capsule *cap = NULL;
        for (int i = 0; i < MAX_PU; i++) if (G.pus[i].alive && (!cap || G.pus[i].y > cap->y)) cap = &G.pus[i];
        if (tb && (tb->p.y > 330 || !cap)) {
            float t = (PY - BALL_R - tb->p.y) / tb->v.y, span = W - 2 * BALL_R;
            float x = tb->p.x + tb->v.x * t - BALL_R;
            x = fmodf(x, 2 * span);
            if (x < 0) x += 2 * span;
            if (x > span) x = 2 * span - x;
            float land = x + BALL_R, tt = G.apOff;
            int alive = 0;
            for (int r = 0; r < ROWS; r++) for (int c = 0; c < COLS; c++) alive += G.br[r][c].alive && G.br[r][c].type != BT_STEEL;
            if (alive > 0) {
                int pick = (int)((G.apOff + 1.0f) * 0.5f * (float)alive) % alive;
                V2 tgt = v2(W / 2.0f, 200);
                for (int r = 0; r < ROWS; r++) for (int c = 0; c < COLS; c++)
                    if (G.br[r][c].alive && G.br[r][c].type != BT_STEEL && pick-- == 0) tgt = brick_center(c, r);
                tt = 0;
                for (int it = 0; it < 4; it++) tt = clampf(atan2f(tgt.x - (land - tt * G.pw / 2), PY - tgt.y) / 1.05f, -0.95f, 0.95f);
            }
            want = land - tt * G.pw / 2;
        } else if (cap) want = cap->x;
        else if (stuck) want = W / 2.0f + sinf(G.t * 0.8f) * 200;
    }
    in.hasT = true; in.tx = want;
    in.fire = ready && G.ph == PH_RUN;
    in.held = G.tLaser > 0;
    return in;
}

static void step(float dt, Input in) {
    if (G.st == ST_PAUSE) return;
    G.t += dt; G.stateT += dt;
    G.pulse *= expf(-dt * 4);
    G.shake *= expf(-dt * 9);
    if (G.shake < 0.05f) G.shake = 0;
    update_fx(dt);
    if (G.st == ST_TITLE) step_game(dt, autopilot(0));
    else if (G.st == ST_PLAY) step_game(dt, in);
    else if (G.st == ST_OVER) {
        for (int i = 0; i < MAX_BALLS; i++) G.balls[i].alive = false;
        for (int r = 0; r < ROWS; r++) for (int c = 0; c < COLS; c++) G.br[r][c].flash = fmaxf(0, G.br[r][c].flash - dt * 4);
    }
}

/* ---------------------------------------------------------------- rendering */
static SDL_Texture *t_glow, *t_disc, *t_bg, *t_vig;

static SDL_Texture *make_tex(int size, bool is_glow) {
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, size, size, 32, SDL_PIXELFORMAT_ARGB8888);
    Uint32 *px = s->pixels;
    float c = size / 2.0f;
    for (int y = 0; y < size; y++)
        for (int x = 0; x < size; x++) {
            float d = sqrtf((x + 0.5f - c) * (x + 0.5f - c) + (y + 0.5f - c) * (y + 0.5f - c)), a;
            if (is_glow) { float q = d / c; a = q < 1 ? (1 - q * q) * (1 - q * q) : 0; }
            else a = clampf(c - 1 - d + 0.5f, 0, 1);
            px[y * (s->pitch / 4) + x] = ((Uint32)(a * 255) << 24) | 0xFFFFFF;
        }
    SDL_Texture *t = SDL_CreateTextureFromSurface(ren, s);
    SDL_FreeSurface(s);
    SDL_SetTextureBlendMode(t, is_glow ? SDL_BLENDMODE_ADD : SDL_BLENDMODE_BLEND);
    return t;
}
static void build_bg(void) {
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, W / 4, H / 4, 32, SDL_PIXELFORMAT_ARGB8888);
    Uint32 *px = s->pixels;
    for (int y = 0; y < H / 4; y++)
        for (int x = 0; x < W / 4; x++) {
            float fy = (float)y / (H / 4), fx = (float)x / (W / 4);
            float d = sqrtf((fx - 0.5f) * (fx - 0.5f) + (fy - 0.45f) * (fy - 0.45f));
            float k = clampf(1 - d * 1.2f, 0, 1);
            int r = (int)(6 + 14 * fy + 10 * k), g = (int)(4 + 4 * fy + 6 * k), b = (int)(20 + 24 * fy + 22 * k);
            px[y * (s->pitch / 4) + x] = 0xFF000000u | ((Uint32)r << 16) | ((Uint32)g << 8) | (Uint32)b;
        }
    t_bg = SDL_CreateTextureFromSurface(ren, s);
    SDL_FreeSurface(s);
    s = SDL_CreateRGBSurfaceWithFormat(0, W / 8, H / 8, 32, SDL_PIXELFORMAT_ARGB8888);
    px = s->pixels;
    for (int y = 0; y < H / 8; y++)
        for (int x = 0; x < W / 8; x++) {
            float fx = ((float)x / (W / 8) - 0.5f) * 2, fy = ((float)y / (H / 8) - 0.5f) * 2;
            float a = clampf((fx * fx + fy * fy - 0.55f) * 0.6f, 0, 0.7f);
            px[y * (s->pitch / 4) + x] = ((Uint32)(a * 255) << 24);
        }
    t_vig = SDL_CreateTextureFromSurface(ren, s);
    SDL_FreeSurface(s);
    SDL_SetTextureBlendMode(t_vig, SDL_BLENDMODE_BLEND);
}

static void addm(bool on) { SDL_SetRenderDrawBlendMode(ren, on ? SDL_BLENDMODE_ADD : SDL_BLENDMODE_BLEND); }
static void fillr(float x, float y, float w, float h, Col c, float a) {
    SDL_SetRenderDrawColor(ren, c.r, c.g, c.b, (Uint8)(clampf(a, 0, 1) * 255));
    SDL_FRect r = {x + shx, y + shy, w, h};
    SDL_RenderFillRectF(ren, &r);
}
static void outr(float x, float y, float w, float h, Col c, float a) {
    SDL_SetRenderDrawColor(ren, c.r, c.g, c.b, (Uint8)(clampf(a, 0, 1) * 255));
    SDL_FRect r = {x + shx, y + shy, w, h};
    SDL_RenderDrawRectF(ren, &r);
}
static void line(float x1, float y1, float x2, float y2, Col c, float a) {
    SDL_SetRenderDrawColor(ren, c.r, c.g, c.b, (Uint8)(clampf(a, 0, 1) * 255));
    SDL_RenderDrawLineF(ren, x1 + shx, y1 + shy, x2 + shx, y2 + shy);
}
static void glow(V2 p, float r, Col c, float k) {
    if (k <= 0.003f) return;
    SDL_SetTextureColorMod(t_glow, c.r, c.g, c.b);
    SDL_SetTextureAlphaMod(t_glow, (Uint8)(clampf(k, 0, 1) * 255));
    SDL_FRect d = {p.x - r + shx, p.y - r + shy, 2 * r, 2 * r};
    SDL_RenderCopyF(ren, t_glow, NULL, &d);
}
static void disc(V2 p, float r, Col c, float a) {
    SDL_SetTextureColorMod(t_disc, c.r, c.g, c.b);
    SDL_SetTextureAlphaMod(t_disc, (Uint8)(clampf(a, 0, 1) * 255));
    SDL_FRect d = {p.x - r + shx, p.y - r + shy, 2 * r, 2 * r};
    SDL_RenderCopyF(ren, t_disc, NULL, &d);
}
static void ring_draw(V2 c, float r, float thick, Col col, float a) {
    SDL_FPoint pts[49];
    SDL_SetRenderDrawColor(ren, col.r, col.g, col.b, (Uint8)(clampf(a, 0, 1) * 255));
    for (float k = 0; k < thick; k += 1.0f) {
        for (int i = 0; i <= 48; i++) {
            float an = (float)i * 2 * PI / 48;
            pts[i].x = c.x + shx + cosf(an) * (r - k); pts[i].y = c.y + shy + sinf(an) * (r - k);
        }
        SDL_RenderDrawLinesF(ren, pts, 49);
    }
}

static Col level_hue(void) { return hsv(0.55f + (float)(G.level % NLEVELS) * 0.075f, 0.8f, 1); }

static void draw_background(void) {
    SDL_RenderCopy(ren, t_bg, NULL, NULL);
    Col lc = level_hue();
    addm(true);
    float pulse = 0.5f + 0.5f * sinf(G.t * 2.1f);
    for (int i = 0; i < 4; i++) {
        V2 p = v2(W * (0.5f + 0.42f * sinf(G.t * 0.11f + (float)i * 1.9f)), H * (0.45f + 0.3f * cosf(G.t * 0.09f + (float)i * 2.7f)));
        glow(p, 260, hsv(0.55f + (float)i * 0.12f + (float)(G.level % NLEVELS) * 0.05f, 0.7f, 1), 0.06f + 0.03f * pulse + 0.05f * G.pulse);
    }
    float a = 0.07f + 0.035f * pulse + 0.12f * G.pulse;
    if (G.tSlow > 0) lc = (Col){90, 140, 255};
    float scroll = fmodf(G.t * 14, 48);
    for (float x = 0; x <= W; x += 48) line(x, TOP, x, H, lc, a);
    for (float y = TOP + scroll; y <= H; y += 48) line(0, y, W, y, lc, a * (0.5f + (y - TOP) / (H - TOP)));
    if (G.tSlow > 0) fillr(0, 0, W, H, (Col){20, 40, 110}, 0.12f);
    addm(false);
}

static void crack_lines(float x, float y, float w, float h, int c, int r, int k) {
    for (int i = 0; i < k; i++) {
        Uint32 hh = ((Uint32)(c * 7919 + r * 104729 + i * 1299709 + 17)) * 2654435761u;
        float f0 = ((hh >> 8) & 255) / 255.0f, f1 = ((hh >> 16) & 255) / 255.0f, f2 = ((hh >> 4) & 255) / 255.0f;
        float x0 = x + w * (0.15f + 0.7f * f0), x1 = x + w * (0.15f + 0.7f * f1), xm = x + w * (0.2f + 0.6f * f2);
        line(x0, y, xm, y + h * 0.5f, (Col){0, 0, 0}, 0.85f);
        line(xm, y + h * 0.5f, x1, y + h, (Col){0, 0, 0}, 0.85f);
        line(x0 + 1, y, xm + 1, y + h * 0.5f, C_WHITE, 0.35f);
        line(xm + 1, y + h * 0.5f, x1 + 1, y + h, C_WHITE, 0.35f);
    }
}

static void draw_brick(int c, int r) {
    Brick *b = &G.br[r][c];
    if (!b->alive) return;
    float x = c * BW + 2, y = BY0 + r * BH + 2, w = BW - 4, h = BH - 4, p = 1;
    if (G.ph == PH_INTRO && G.st != ST_OVER) p = clampf((G.phT - (float)c * 0.02f - (float)r * 0.05f) / 0.45f, 0, 1);
    if (p <= 0) return;
    y -= (1 - p) * (1 - p) * 260;
    Col base = b->type == BT_EXPL ? C_EXPL : b->type == BT_STEEL ? C_STEEL : PAL[b->col];
    float pul = 0.5f + 0.5f * sinf(G.t * 6 + (float)(c + r));
    V2 ctr = v2(x + w / 2, y + h / 2);
    if (b->type == BT_PRISM) base = hsv(G.t * 0.4f + (float)c * 0.06f, 0.6f, 1);
    addm(true);
    glow(ctr, 54, base, (b->type == BT_STEEL ? 0.12f : 0.26f + (b->type == BT_EXPL ? 0.15f * pul : 0)) * p + b->flash * 0.4f);
    addm(false);
    fillr(x, y, w, h, scale_col(base, 0.42f), p);
    fillr(x + 1, y + 1, w - 2, h / 2 - 1, scale_col(base, 0.70f), p);
    outr(x, y, w, h, base, p);
    fillr(x + 2, y + 1, w - 4, 1, C_WHITE, 0.4f * p);
    switch (b->type) {
    case BT_MULTI:
        outr(x + 3, y + 3, w - 6, h - 6, base, 0.55f * p);
        if (b->hp < b->maxhp) crack_lines(x, y, w, h, c, r, b->maxhp - b->hp);
        break;
    case BT_EXPL: {
        addm(true);
        glow(ctr, 17, (Col){255, 220, 120}, 0.5f + 0.5f * pul);
        addm(false);
        line(x + 18, y + 4, x + w - 18, y + h - 4, (Col){255, 240, 160}, 0.9f);
        line(x + w - 18, y + 4, x + 18, y + h - 4, (Col){255, 240, 160}, 0.9f);
        break; }
    case BT_STEEL: {
        fillr(x + 1, y + h / 2, w - 2, h / 2 - 1, scale_col(base, 0.45f), p);
        disc(v2(x + 6, y + 6), 2, (Col){60, 66, 80}, 1); disc(v2(x + w - 6, y + 6), 2, (Col){60, 66, 80}, 1);
        disc(v2(x + 6, y + h - 6), 2, (Col){60, 66, 80}, 1); disc(v2(x + w - 6, y + h - 6), 2, (Col){60, 66, 80}, 1);
        float sh = fmodf(G.t * 0.5f + (float)c * 0.13f + (float)r * 0.21f, 2.0f);
        if (sh < 1) { addm(true); line(x + sh * w, y + 1, x + sh * w - 8, y + h - 1, C_WHITE, 0.5f); addm(false); }
        break; }
    case BT_PRISM: {
        addm(true);
        for (int i = 0; i < 10; i++) {
            Col sc = hsv(G.t * 0.5f + (float)i * 0.1f + (float)c * 0.05f, 0.8f, 1);
            fillr(x + 2 + (float)i * (w - 4) / 10, y + 2, (w - 4) / 10 + 0.5f, h - 4, sc, 0.55f * p);
        }
        addm(false);
        line(x + 6, y + h - 4, x + w / 2, y + 3, C_WHITE, 0.7f);
        line(x + w / 2, y + 3, x + w - 6, y + h - 4, C_WHITE, 0.7f);
        break; }
    default: break;
    }
    if (b->flash > 0.01f) { addm(true); fillr(x, y, w, h, C_WHITE, b->flash * 0.6f); addm(false); }
}

static void draw_ball(const Ball *b) {
    Col c = PAL[b->col];
    addm(true);
    for (int i = TRAIL - 1; i > 0; i--) {
        float k = 1 - (float)i / TRAIL;
        if (len(v2(b->trail[i].x - b->trail[i - 1].x, b->trail[i].y - b->trail[i - 1].y)) < 0.01f && i > 1) continue;
        glow(b->trail[i], BALL_R * (1.2f + 1.6f * k), c, 0.5f * k * k);
    }
    glow(b->p, 30, c, 0.75f);
    addm(false);
    disc(b->p, BALL_R, C_WHITE, 1);
    disc(add(b->p, v2(-1.5f, -1.5f)), BALL_R * 0.55f, scale_col(c, 1.0f), 0.55f);
    disc(add(b->p, v2(-1.5f, -2)), BALL_R * 0.35f, C_WHITE, 1);
}

static void draw_paddle(void) {
    float w = G.pw, x = G.px - w / 2, y = PY + G.padSquash * 0.6f, h = PTH - G.padSquash * 0.5f;
    Col base = G.tLaser > 0 ? PU_COL[PU_LASER] : G.tSticky > 0 ? PU_COL[PU_STICKY] : G.tShrink > 0 ? PU_COL[PU_SHRINK] : PAL[0];
    addm(true);
    glow(v2(G.px, y + h / 2), w * 0.75f, base, 0.28f + 0.1f * sinf(G.t * 5));
    addm(false);
    fillr(x, y, w, h, scale_col(base, 0.35f), 1);
    fillr(x + 2, y + 1, w - 4, h / 2, scale_col(base, 0.75f), 1);
    fillr(x + 6, y + 2, w - 12, 2, C_WHITE, 0.5f);
    outr(x, y, w, h, base, 1);
    fillr(x - 2, y + 1, 6, h - 2, C_WHITE, 0.85f);
    fillr(x + w - 4, y + 1, 6, h - 2, C_WHITE, 0.85f);
    if (G.tLaser > 0) {
        fillr(x + 4, y - 7, 5, 8, PU_COL[PU_LASER], 1); fillr(x + w - 9, y - 7, 5, 8, PU_COL[PU_LASER], 1);
        addm(true); glow(v2(x + 6.5f, y - 7), 14, PU_COL[PU_LASER], 0.7f); glow(v2(x + w - 6.5f, y - 7), 14, PU_COL[PU_LASER], 0.7f); addm(false);
    }
    if (G.tSticky > 0) {
        for (int i = 0; i < 6; i++) fillr(x + w * (0.1f + 0.16f * (float)i), y - 3, 8, 3, PU_COL[PU_STICKY], 0.9f);
    }
}

static void draw_capsule(const Capsule *p) {
    Col c = PU_COL[p->type];
    float x = p->x - 23, y = p->y - 10 + sinf(G.t * 8 + p->x) * 1.5f;
    addm(true); glow(v2(p->x, p->y), 40, c, 0.45f + 0.2f * sinf(G.t * 9)); addm(false);
    fillr(x, y, 46, 20, scale_col(c, 0.3f), 1);
    fillr(x + 1, y + 1, 44, 9, scale_col(c, 0.55f), 1);
    outr(x, y, 46, 20, c, 1);
    fillr(x - 1, y + 4, 2, 12, c, 1); fillr(x + 45, y + 4, 2, 12, c, 1);
    char s[2] = {PU_LETTER[p->type], 0};
    text(s, p->x, y + 3, 2, C_WHITE, 1, true);
}

static void draw_world(void) {
    draw_background();
    shx = (frand() - 0.5f) * 2 * G.shake; shy = (frand() - 0.5f) * 2 * G.shake;
    for (int r = 0; r < ROWS; r++) for (int c = 0; c < COLS; c++) draw_brick(c, r);
    fillr(0, TOP, 2, H - TOP, level_hue(), 0.5f); fillr(W - 2, TOP, 2, H - TOP, level_hue(), 0.5f);
    for (int i = 0; i < MAX_PU; i++) if (G.pus[i].alive) draw_capsule(&G.pus[i]);
    addm(true);
    for (int i = 0; i < MAX_BOLTS; i++) if (G.bolts[i].alive) {
        glow(v2(G.bolts[i].x, G.bolts[i].y), 22, PU_COL[PU_LASER], 0.8f);
        fillr(G.bolts[i].x - 1.5f, G.bolts[i].y - 10, 3, 24, (Col){255, 190, 190}, 1);
    }
    for (int i = 0; i < MAX_RINGS; i++) {
        Ring *r = &G.rings[i];
        if (r->t >= r->max) continue;
        float k = r->t / r->max;
        ring_draw(r->p, 8 + k * 150, 4 * (1 - k) + 1, r->c, 1 - k);
    }
    for (int i = 0; i < MAX_PARTICLES; i++) {
        Particle *q = &G.parts[i];
        if (q->life <= 0 || q->shard) continue;
        float k = q->life / q->max;
        glow(q->p, q->size * (0.5f + k), q->c, k);
    }
    addm(false);
    for (int i = 0; i < MAX_PARTICLES; i++) {
        Particle *q = &G.parts[i];
        if (q->life <= 0 || !q->shard) continue;
        float k = clampf(q->life / q->max * 1.5f, 0, 1);
        fillr(q->p.x, q->p.y, q->size, q->size, q->c, k);
        fillr(q->p.x, q->p.y, q->size * 0.5f, q->size * 0.5f, C_WHITE, 0.5f * k);
    }
    for (int i = 0; i < MAX_BALLS; i++) if (G.balls[i].alive) draw_ball(&G.balls[i]);
    if (G.ph != PH_DYING && G.st != ST_OVER) draw_paddle();
    for (int i = 0; i < MAX_POPUPS; i++) {
        Popup *q = &G.pops[i];
        if (q->life <= 0) continue;
        float k = q->life / q->max, pop = 1 + 0.5f * clampf((k - 0.85f) * 6, 0, 1);
        text_sh(q->s, q->x + shx, q->y + shy, q->sc * pop, q->c, clampf(k * 2.5f, 0, 1));
    }
    shx = shy = 0;
}

static void draw_hud(void) {
    fillr(0, 0, W, TOP, (Col){6, 4, 18}, 0.92f);
    Col lc = level_hue();
    addm(true); fillr(0, TOP - 2, W, 2, lc, 0.7f); glow(v2(W / 2.0f, TOP), 380, lc, 0.12f); addm(false);
    char b[64];
    snprintf(b, sizeof b, "%07d", G.score);
    text("SCORE", 14, 6, 2, (Col){140, 150, 200}, 1, false);
    text(b, 14, 22, 3, C_WHITE, 1, false);
    snprintf(b, sizeof b, "LEVEL %d: %s", G.level + 1, LV_NAME[G.level % NLEVELS]);
    text(b, W / 2.0f, 6, 2, lc, 1, true);
    if (G.combo >= 3) {
        float m = combo_mult();
        snprintf(b, sizeof b, "COMBO X%.1f", m);
        text(b, W / 2.0f, 24, 2, hsv(0.5f - (m - 1) * 0.07f, 0.7f, 1), 1, true);
    }
    snprintf(b, sizeof b, "BEST %07d", G.best > G.score ? G.best : G.score);
    text(b, W - 14 - text_w(b, 2), 6, 2, (Col){255, 210, 90}, 1, false);
    for (int i = 0; i < G.lives && i < 9; i++) {
        float x = W - 24 - (float)i * 30;
        fillr(x - 22, 28, 24, 8, scale_col(PAL[0], 0.4f), 1);
        outr(x - 22, 28, 24, 8, PAL[0], 1);
        fillr(x - 21, 29, 22, 2, C_WHITE, 0.4f);
    }
    struct { float t, mx; int type; } fx[5] = {{G.tWide, 15, PU_WIDE}, {G.tLaser, 14, PU_LASER}, {G.tSticky, 16, PU_STICKY},
                                               {G.tSlow, 8, PU_SLOW}, {G.tShrink, 10, PU_SHRINK}};
    int n = 0;
    for (int i = 0; i < 5; i++) {
        if (fx[i].t <= 0) continue;
        float x = 12 + (float)n * 110;
        char s[2] = {PU_LETTER[fx[i].type], 0};
        text(s, x, 578, 2, PU_COL[fx[i].type], 1, false);
        fillr(x + 16, 583, 80, 5, (Col){30, 30, 50}, 1);
        fillr(x + 16, 583, 80 * clampf(fx[i].t / fx[i].mx, 0, 1), 5, PU_COL[fx[i].type], 1);
        n++;
    }
}

static void draw_overlay(void) {
    float blink = 0.55f + 0.45f * sinf(G.t * 5);
    if (G.st == ST_TITLE) {
        fillr(0, 0, W, H, (Col){4, 2, 14}, 0.55f);
        const char *t = "PRISM SMASH";
        float sc = 11, x0 = W / 2.0f - text_w(t, sc) / 2;
        for (int i = 0; t[i]; i++) {
            char s[2] = {t[i], 0};
            float x = x0 + (float)i * 6 * sc, y = 120 + sinf(G.t * 2.5f + (float)i * 0.5f) * 6;
            Col c = hsv(G.t * 0.15f + (float)i * 0.085f, 0.75f, 1);
            addm(true); glow(v2(x + 2.5f * sc, y + 3.5f * sc), 80, c, 0.45f); addm(false);
            text(s, x + 4, y + 5, sc, (Col){0, 0, 0}, 0.6f, false);
            text(s, x, y, sc, c, 1, false);
        }
        text_sh("NEON BRICK BREAKER", W / 2.0f, 225, 3, (Col){170, 190, 255}, 1);
        text_sh("PRESS ENTER OR CLICK TO START", W / 2.0f, 380, 3, C_WHITE, blink);
        text("MOUSE OR ARROWS: MOVE   CLICK OR SPACE: LAUNCH AND FIRE", W / 2.0f, 440, 2, (Col){150, 160, 200}, 1, true);
        text("ESC: PAUSE   F11: FULLSCREEN", W / 2.0f, 466, 2, (Col){150, 160, 200}, 1, true);
        char b[40]; snprintf(b, sizeof b, "BEST %07d", G.best);
        text(b, W / 2.0f, 520, 3, (Col){255, 210, 90}, 1, true);
        return;
    }
    if (G.st == ST_PLAY && G.ph == PH_INTRO) {
        float a = clampf(sinf(clampf(G.phT / 1.5f, 0, 1) * PI) * 2.2f, 0, 1);
        char b[40]; snprintf(b, sizeof b, "LEVEL %d", G.level + 1);
        text_sh(b, W / 2.0f, 380, 5, C_WHITE, a);
        text_sh(LV_NAME[G.level % NLEVELS], W / 2.0f, 428, 3, level_hue(), a);
    }
    if (G.st == ST_PLAY && G.ph == PH_RUN && G.phT < 30) {
        for (int i = 0; i < MAX_BALLS; i++)
            if (G.balls[i].alive && G.balls[i].stuck) {
                text_sh("CLICK OR SPACE TO LAUNCH", W / 2.0f, 470, 2, C_WHITE, blink);
                break;
            }
    }
    if (G.st == ST_PLAY && G.ph == PH_CLEAR) text_sh("LEVEL CLEARED!", W / 2.0f, 250, 6, hsv(G.t * 0.8f, 0.6f, 1), 1);
    if (G.st == ST_PAUSE) {
        fillr(0, 0, W, H, (Col){4, 2, 14}, 0.7f);
        text_sh("PAUSED", W / 2.0f, 220, 8, C_WHITE, 1);
        text_sh("ESC: RESUME     Q: QUIT TO TITLE", W / 2.0f, 320, 2, (Col){170, 190, 255}, 1);
    }
    if (G.st == ST_OVER) {
        float a = clampf(G.stateT * 2, 0, 1);
        fillr(0, 0, W, H, (Col){4, 2, 14}, 0.72f * a);
        text_sh("GAME OVER", W / 2.0f, 160, 9, (Col){255, 90, 110}, a);
        char b[48];
        snprintf(b, sizeof b, "SCORE %07d", G.score);
        text_sh(b, W / 2.0f, 260, 4, C_WHITE, a);
        snprintf(b, sizeof b, "LEVEL %d   BEST COMBO %d", G.level + 1, G.maxCombo);
        text_sh(b, W / 2.0f, 315, 2, (Col){170, 190, 255}, a);
        if (G.newBest && G.score > 0) text_sh("NEW BEST!", W / 2.0f, 355, 4, hsv(G.t, 0.6f, 1), a * blink);
        else { snprintf(b, sizeof b, "BEST %07d", G.best); text_sh(b, W / 2.0f, 355, 3, (Col){255, 210, 90}, a); }
        if (G.stateT > 0.8f) text_sh("PRESS ENTER TO PLAY AGAIN", W / 2.0f, 440, 3, C_WHITE, blink);
    }
}

static void render(void) {
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
    SDL_RenderClear(ren);
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    draw_world();
    SDL_RenderCopy(ren, t_vig, NULL, NULL);
    if (G.st != ST_TITLE) draw_hud();
    draw_overlay();
}

/* ---------------------------------------------------------------- selftest */
static void shot(const char *path) {
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, W, H, 32, SDL_PIXELFORMAT_ARGB8888);
    SDL_RenderReadPixels(ren, NULL, SDL_PIXELFORMAT_ARGB8888, s->pixels, s->pitch);
    SDL_SaveBMP(s, path);
    SDL_FreeSurface(s);
}

static int live_parts(void) { int n = 0; for (int i = 0; i < MAX_PARTICLES; i++) n += G.parts[i].life > 0; return n; }

static int selftest(const char *dir) {
    char path[1200];
    if (validate_levels()) return 1;
    /* every level: autopilot with plenty of lives, report whether it clears */
    int cleared = 0;
    for (int l = 0; l < NLEVELS; l++) {
        start_game();
        G.level = l; load_level(l); G.lives = 99; quiet = true;
        int f = 0;
        while (G.level == l && f < 60 * 150) { step(1 / 60.0f, autopilot(0)); f++; }
        bool ok = G.level != l;
        cleared += ok;
        if (!ok && getenv("PS_DEBUG")) for (int r = 0; r < ROWS; r++) for (int c = 0; c < COLS; c++) if (G.br[r][c].alive && G.br[r][c].type != BT_STEEL) printf("    left r%d c%d\n", r, c);
        printf("  level %2d %-15s %s in %5.1fs  (balls peak ok, remaining %d)\n", l + 1, LV_NAME[l], ok ? "cleared    " : "NOT cleared", f / 60.0f, ok ? 0 : G.remaining);
    }
    start_demo();
    for (int i = 0; i < 300; i++) step(1 / 60.0f, (Input){0});
    render(); snprintf(path, sizeof path, "%s/title.bmp", dir); shot(path);

    start_game();
    G.best = 0;
    int frames = 0, peak = 0;
    bool took = false;
    while (G.st == ST_PLAY && frames < 60 * 600) {
        step(1 / 60.0f, autopilot(0));
        frames++;
        int nb = count_balls();
        if (nb > peak) peak = nb;
        if (!took && G.level >= 2 && G.ph == PH_RUN && live_parts() > 350 && nb >= 2 && G.phT > 3) {
            render(); snprintf(path, sizeof path, "%s/play.bmp", dir); shot(path); took = true; break;
        }
    }
    if (!took) { render(); snprintf(path, sizeof path, "%s/play.bmp", dir); shot(path); }
    int playedScore = G.score;
    for (int i = 0; i < 60 * 180 && G.st == ST_PLAY; i++) step(1 / 60.0f, autopilot(1));
    for (int i = 0; i < 90; i++) step(1 / 60.0f, (Input){0});
    render(); snprintf(path, sizeof path, "%s/over.bmp", dir); shot(path);
    printf("selftest: levels cleared %d/%d, play shot at score %d (level %d), final state %d score %d peak balls %d\n",
           cleared, NLEVELS, playedScore, G.level + 1, G.st, G.score, peak);
    return G.st == ST_OVER ? 0 : 1;
}

int main(int argc, char **argv) {
    const char *testdir = (argc == 3 && strcmp(argv[1], "--selftest") == 0) ? argv[2] : NULL;
    rng ^= (Uint32)SDL_GetPerformanceCounter() | 1u;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    if (!testdir && SDL_InitSubSystem(SDL_INIT_AUDIO) == 0) {
        SDL_AudioSpec want = {0}, have;
        want.freq = RATE; want.format = AUDIO_F32SYS; want.channels = 1; want.samples = 512; want.callback = audio_cb;
        adev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
        if (adev) { build_sounds(); SDL_PauseAudioDevice(adev, 0); }
    }

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
    SDL_Window *win = SDL_CreateWindow("Prism Smash", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, W, H,
                                       testdir ? SDL_WINDOW_HIDDEN : (SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI));
    if (!win) { fprintf(stderr, "window: %s\n", SDL_GetError()); return 1; }
    SDL_SetWindowMinimumSize(win, W / 2, H / 2);
    ren = SDL_CreateRenderer(win, -1, testdir ? SDL_RENDERER_SOFTWARE : (SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC));
    if (!ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    if (!ren) { fprintf(stderr, "renderer: %s\n", SDL_GetError()); return 1; }
    SDL_RenderSetLogicalSize(ren, W, H);
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);

    t_glow = make_tex(128, true);
    t_disc = make_tex(64, false);
    build_bg();

    if (testdir) no_save = true; else load_best();
    start_demo();

    if (testdir) { int r = selftest(testdir); SDL_Quit(); return r; }

    SDL_GameController *pad = NULL;
    bool running = true, cursor = true;
    float mouse_x = W / 2.0f;
    Uint64 prev = SDL_GetPerformanceCounter();
    while (running) {
        Input in = {0};
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
            case SDL_QUIT: running = false; break;
            case SDL_MOUSEMOTION: mouse_x = (float)e.motion.x; in.hasT = true; in.tx = mouse_x; break;
            case SDL_MOUSEBUTTONDOWN:
                if (e.button.button != SDL_BUTTON_LEFT) break;
                if (G.st == ST_TITLE) start_game();
                else if (G.st == ST_PLAY) in.fire = true;
                else if (G.st == ST_OVER && G.stateT > 0.8f) start_game();
                else if (G.st == ST_PAUSE) G.st = ST_PLAY;
                break;
            case SDL_CONTROLLERDEVICEADDED:
                if (!pad) pad = SDL_GameControllerOpen(e.cdevice.which);
                break;
            case SDL_CONTROLLERDEVICEREMOVED:
                if (pad && e.cdevice.which == SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pad))) {
                    SDL_GameControllerClose(pad); pad = NULL;
                    if (G.st == ST_PLAY) G.st = ST_PAUSE;
                }
                break;
            case SDL_WINDOWEVENT:
                if (e.window.event == SDL_WINDOWEVENT_FOCUS_LOST && G.st == ST_PLAY) G.st = ST_PAUSE;
                break;
            case SDL_KEYDOWN: {
                if (e.key.repeat) break;
                SDL_Keycode k = e.key.keysym.sym;
                bool enter = k == SDLK_RETURN || k == SDLK_KP_ENTER;
                if (k == SDLK_F11) {
                    bool fs = SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN_DESKTOP;
                    SDL_SetWindowFullscreen(win, fs ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
                } else if (G.st == ST_TITLE) {
                    if (enter || k == SDLK_SPACE) start_game();
                    else if (k == SDLK_ESCAPE) running = false;
                } else if (G.st == ST_PLAY) {
                    if (k == SDLK_ESCAPE || k == SDLK_p) G.st = ST_PAUSE;
                    else if (k == SDLK_SPACE || enter) in.fire = true;
                } else if (G.st == ST_PAUSE) {
                    if (k == SDLK_ESCAPE || k == SDLK_p) G.st = ST_PLAY;
                    else if (k == SDLK_q) { save_best(); start_demo(); }
                } else if (G.st == ST_OVER && G.stateT > 0.8f) {
                    if (enter || k == SDLK_SPACE) start_game();
                    else if (k == SDLK_ESCAPE) start_demo();
                }
                break;
            }
            case SDL_CONTROLLERBUTTONDOWN: {
                int b = e.cbutton.button;
                if (G.st == ST_TITLE && (b == SDL_CONTROLLER_BUTTON_START || b == SDL_CONTROLLER_BUTTON_A)) start_game();
                else if (G.st == ST_PLAY) {
                    if (b == SDL_CONTROLLER_BUTTON_START) G.st = ST_PAUSE;
                    else if (b == SDL_CONTROLLER_BUTTON_A || b == SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) in.fire = true;
                } else if (G.st == ST_PAUSE) {
                    if (b == SDL_CONTROLLER_BUTTON_START) G.st = ST_PLAY;
                    else if (b == SDL_CONTROLLER_BUTTON_BACK) { save_best(); start_demo(); }
                } else if (G.st == ST_OVER && G.stateT > 0.8f) {
                    if (b == SDL_CONTROLLER_BUTTON_START || b == SDL_CONTROLLER_BUTTON_A) start_game();
                    else if (b == SDL_CONTROLLER_BUTTON_BACK) start_demo();
                }
                break;
            }
            }
        }
        const Uint8 *ks = SDL_GetKeyboardState(NULL);
        in.axis = (float)((ks[SDL_SCANCODE_D] || ks[SDL_SCANCODE_RIGHT]) - (ks[SDL_SCANCODE_A] || ks[SDL_SCANCODE_LEFT]));
        in.held = ks[SDL_SCANCODE_SPACE] || (SDL_GetMouseState(NULL, NULL) & SDL_BUTTON_LMASK);
        if (pad) {
            float ax = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTX) / 32767.0f;
            if (fabsf(ax) > 0.2f) in.axis += ax;
            in.axis += (float)(SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_RIGHT) - SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_LEFT));
            if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_A)) in.held = true;
        }
        in.axis = clampf(in.axis, -1, 1);

        bool want_cursor = G.st != ST_PLAY;
        if (want_cursor != cursor) { SDL_ShowCursor(want_cursor ? SDL_ENABLE : SDL_DISABLE); cursor = want_cursor; }

        Uint64 now = SDL_GetPerformanceCounter();
        float dt = (float)(now - prev) / (float)SDL_GetPerformanceFrequency();
        prev = now;
        if (dt > 1 / 30.0f) dt = 1 / 30.0f;
        step(dt, in);
        render();
        SDL_RenderPresent(ren);
    }

    save_best();
    if (pad) SDL_GameControllerClose(pad);
    if (adev) SDL_CloseAudioDevice(adev);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
