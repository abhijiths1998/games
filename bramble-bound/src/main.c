/*
 * Bramble Bound — an original side-scrolling platformer.
 * Guide Bramble the fox through four worlds: meadow, sunset cliffs, a glowing cave and frozen peaks.
 * Native Linux game, C11 + SDL2 only. Every sprite, tile, backdrop, light, sound and song is procedural.
 */
#include <SDL.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LW 480 /* logical canvas: pixel art, scaled up to the window */
#define LH 270
#define TS 16
#define ROWS 17
#define MAXC 512
#define PI 3.14159265358979f
#define RATE 44100
#define MAX_VOICES 20
#define MAX_ENTS 320
#define MAX_PARTS 1800
#define MAX_PLATS 16
#define MAX_POPS 24
#define MAX_AMB 90
#define SCARF_N 7
#define CAMY (ROWS * TS - LH)

/* ---------------------------------------------------------------- math / rng */
static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
static float lerpf(float a, float b, float t) { return a + (b - a) * t; }
static float approach(float v, float t, float d) { return v < t ? fminf(v + d, t) : fmaxf(v - d, t); }
static Uint32 rng = 0x9E3779B9u;
static Uint32 xr(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
static float frand(void) { return (float)(xr() >> 8) * (1.0f / 16777216.0f); }
static float frange(float a, float b) { return a + (b - a) * frand(); }
static Uint32 hash2(int x, int y) {
    Uint32 h = (Uint32)x * 374761393u + (Uint32)y * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}
static float h01(int x, int y) { return (float)(hash2(x, y) & 0xFFFF) / 65535.0f; }
static bool overlap(float ax, float ay, float aw, float ah, float bx, float by, float bw, float bh) {
    return ax < bx + bw && ax + aw > bx && ay < by + bh && ay + ah > by;
}

/* ---------------------------------------------------------------- audio */
typedef struct { float *d; int n; } Sound;
enum { S_JUMP, S_SEED, S_STOMP, S_BUMP, S_BREAK, S_POWER, S_HURT, S_DIE, S_SPRING, S_CHECK,
       S_CLEAR, S_OVER, S_START, S_LIFE, S_SPROUT, S_SPLASH, S_COUNT };
static Sound snd[S_COUNT], tunes[4];
typedef struct { const float *d; int n, pos; float vol; } Voice;
static Voice voices[MAX_VOICES], music;
static SDL_AudioDeviceID adev;
static bool music_on = true, sfx_mute;

static void audio_cb(void *u, Uint8 *stream, int bytes) {
    (void)u;
    float *out = (float *)stream;
    int n = bytes / (int)sizeof(float);
    memset(out, 0, (size_t)bytes);
    for (int v = 0; v < MAX_VOICES; v++) {
        if (!voices[v].d) continue;
        for (int i = 0; i < n && voices[v].pos < voices[v].n; i++) out[i] += voices[v].d[voices[v].pos++] * voices[v].vol;
        if (voices[v].pos >= voices[v].n) voices[v].d = NULL;
    }
    if (music.d && music_on)
        for (int i = 0; i < n; i++) {
            out[i] += music.d[music.pos] * music.vol;
            if (++music.pos >= music.n) music.pos = 0;
        }
    for (int i = 0; i < n; i++) out[i] = tanhf(out[i]);
}

static void play(int id, float vol) {
    if (!adev || !snd[id].d || sfx_mute) return;
    SDL_LockAudioDevice(adev);
    int slot = 0, best = -1;
    for (int v = 0; v < MAX_VOICES; v++) {
        if (!voices[v].d) { slot = v; break; }
        if (voices[v].pos > best) { best = voices[v].pos; slot = v; }
    }
    voices[slot] = (Voice){snd[id].d, snd[id].n, 0, vol};
    SDL_UnlockAudioDevice(adev);
}

static float *mk(Sound *s, float secs) {
    s->n = (int)(secs * RATE);
    s->d = calloc((size_t)s->n, sizeof(float));
    return s->d;
}
static float osc(int wave, float ph) {
    float x = ph / (2 * PI);
    x -= floorf(x);
    switch (wave) {
    case 0: return 4.0f * fabsf(x - 0.5f) - 1.0f;   /* triangle */
    case 1: return x < 0.5f ? 0.55f : -0.55f;       /* square */
    case 2: return x < 0.25f ? 0.55f : -0.55f;      /* 25% pulse */
    default: return sinf(ph);
    }
}
static float mtof(float m) { return 440.0f * powf(2.0f, (m - 69.0f) / 12.0f); }

/* a short melodic jingle: one note per step, last note rings out */
static void jingle(Sound *s, const float *midi, int n, float step, int wave, float decay, float vol) {
    float *d = mk(s, n * step + 0.45f), ph = 0;
    for (int i = 0; i < s->n; i++) {
        float t = (float)i / RATE;
        int k = (int)(t / step);
        if (k >= n) k = n - 1;
        float lt = t - k * step;
        ph += 2 * PI * mtof(midi[k]) / RATE;
        float env = fminf(1.0f, lt * 300) * expf(-lt * (k == n - 1 ? decay * 0.5f : decay));
        d[i] = osc(wave, ph) * env * vol;
    }
}

static void build_sounds(void) {
    float *d, ph = 0, lp = 0;
    d = mk(&snd[S_JUMP], 0.18f);
    for (int i = 0; i < snd[S_JUMP].n; i++) {
        float t = (float)i / RATE;
        ph += 2 * PI * (250 + 3200 * t) / RATE;
        d[i] = osc(2, ph) * expf(-t * 13) * 0.35f;
    }
    { const float m[] = {83, 88}; jingle(&snd[S_SEED], m, 2, 0.055f, 1, 11, 0.3f); }
    d = mk(&snd[S_STOMP], 0.2f); ph = 0;
    for (int i = 0; i < snd[S_STOMP].n; i++) {
        float t = (float)i / RATE;
        ph += 2 * PI * (60 + 260 * expf(-t * 22)) / RATE;
        d[i] = (sinf(ph) * 0.8f + frange(-1, 1) * expf(-t * 50) * 0.4f) * expf(-t * 14) * 0.6f;
    }
    d = mk(&snd[S_BUMP], 0.12f); ph = 0;
    for (int i = 0; i < snd[S_BUMP].n; i++) {
        float t = (float)i / RATE;
        ph += 2 * PI * 120 / RATE;
        d[i] = (osc(1, ph) + sinf(ph * 0.5f)) * expf(-t * 30) * 0.45f;
    }
    d = mk(&snd[S_BREAK], 0.4f); lp = 0;
    for (int i = 0; i < snd[S_BREAK].n; i++) {
        float t = (float)i / RATE;
        lp += (frange(-1, 1) - lp) * (0.5f * expf(-t * 6) + 0.05f);
        d[i] = lp * expf(-t * 9) * 1.1f;
    }
    { const float m[] = {60, 64, 67, 72, 64, 67, 72, 76, 67, 72, 76, 79}; jingle(&snd[S_POWER], m, 12, 0.045f, 1, 14, 0.24f); }
    d = mk(&snd[S_HURT], 0.38f); ph = 0;
    for (int i = 0; i < snd[S_HURT].n; i++) {
        float t = (float)i / RATE;
        ph += 2 * PI * (700 * expf(-t * 4.5f) * (1 + 0.06f * sinf(t * 90))) / RATE;
        d[i] = osc(1, ph) * expf(-t * 6) * 0.35f;
    }
    { const float m[] = {76, 72, 67, 64, 59, 55}; jingle(&snd[S_DIE], m, 6, 0.11f, 0, 5, 0.45f); }
    d = mk(&snd[S_SPRING], 0.45f); ph = 0;
    for (int i = 0; i < snd[S_SPRING].n; i++) {
        float t = (float)i / RATE;
        ph += 2 * PI * (170 + 1300 * t + 60 * sinf(t * 70)) / RATE;
        d[i] = sinf(ph) * expf(-t * 6) * 0.45f;
    }
    { const float m[] = {79, 84, 91}; jingle(&snd[S_CHECK], m, 3, 0.08f, 0, 4, 0.4f); }
    { const float m[] = {67, 72, 76, 79, 76, 79, 84, 84}; jingle(&snd[S_CLEAR], m, 8, 0.11f, 1, 3, 0.26f); }
    { const float m[] = {67, 63, 60, 55}; jingle(&snd[S_OVER], m, 4, 0.34f, 0, 3, 0.45f); }
    { const float m[] = {60, 64, 67, 72}; jingle(&snd[S_START], m, 4, 0.08f, 1, 9, 0.25f); }
    { const float m[] = {76, 79, 88, 84, 86, 91}; jingle(&snd[S_LIFE], m, 6, 0.07f, 1, 9, 0.25f); }
    d = mk(&snd[S_SPROUT], 0.35f); ph = 0;
    for (int i = 0; i < snd[S_SPROUT].n; i++) {
        float t = (float)i / RATE;
        ph += 2 * PI * (300 + 1800 * t) / RATE;
        d[i] = sinf(ph) * (0.6f + 0.4f * sinf(t * 120)) * expf(-t * 5) * 0.4f;
    }
    d = mk(&snd[S_SPLASH], 0.5f); lp = 0;
    for (int i = 0; i < snd[S_SPLASH].n; i++) {
        float t = (float)i / RATE;
        lp += (frange(-1, 1) - lp) * (0.3f * expf(-t * 4));
        d[i] = lp * expf(-t * 5) * 1.4f;
    }
}

/* ------------- procedural chiptune: one looping song per world */
typedef struct { int root, minor; float bpm; int prog[4], lead, drums, pad; float echo; Uint32 seed; } TuneDef;
static const TuneDef TUNES[4] = {
    {60, 0, 140, {0, 4, 5, 3}, 2, 2, 0, 0.12f, 11},
    {57, 0, 116, {5, 3, 0, 4}, 1, 1, 1, 0.22f, 29},
    {57, 1, 90, {0, 5, 2, 6}, 0, 0, 1, 0.45f, 41},
    {62, 1, 104, {0, 3, 5, 4}, 3, 1, 1, 0.32f, 57},
};
static int scale_note(int root, int minor, int deg) {
    static const int MAJ[7] = {0, 2, 4, 5, 7, 9, 11}, MIN[7] = {0, 2, 3, 5, 7, 8, 10};
    int o = deg >= 0 ? deg / 7 : -((-deg + 6) / 7);
    int i = deg - o * 7;
    return root + 12 * o + (minor ? MIN[i] : MAJ[i]);
}

static void build_tune(int th) {
    const TuneDef *td = &TUNES[th];
    Uint32 save = rng;
    rng = td->seed * 2654435761u | 1u;
    float eighth = 60.0f / td->bpm / 2;
    int steps = 64, ns = (int)(eighth * RATE);
    Sound *s = &tunes[th];
    s->n = ns * steps;
    s->d = calloc((size_t)s->n, sizeof(float));
    float *d = s->d;
    int mel[64], deg = 7;
    for (int st = 0; st < 32; st++) {
        int chord = td->prog[(st / 8) % 4], pos = st % 8;
        if (pos % 2 == 1 && frand() < 0.35f) { mel[st] = -99; continue; } /* hold previous */
        if (pos == 0 || pos == 4) {
            int best = deg, bd = 99;
            for (int k = -1; k <= 2; k++)
                for (int c = 0; c < 3; c++) {
                    int cand = chord + c * 2 + k * 7, dd = abs(cand - deg) + (int)(frand() * 3);
                    if (cand >= 4 && cand <= 13 && dd < bd) { bd = dd; best = cand; }
                }
            deg = best;
        } else {
            static const int stepv[4] = {-2, -1, 1, 2};
            deg += stepv[xr() % 4];
            if (deg < 4) deg = 5;
            if (deg > 13) deg = 12;
        }
        mel[st] = deg;
    }
    for (int st = 32; st < 64; st++) mel[st] = mel[st - 32];
    for (int st = 52; st < 64; st++) mel[st] = (st % 2 == 0) ? 7 + (int)(xr() % 5) - 2 : -99;
    mel[56] = 9; mel[60] = 7; mel[61] = mel[62] = mel[63] = -99;
    /* lead */
    for (int st = 0; st < steps; st++) {
        if (mel[st] == -99) continue;
        int len = 1;
        while (st + len < steps && mel[st + len] == -99) len++;
        float f = mtof((float)scale_note(td->root, td->minor, mel[st])), ph = 0;
        int a = st * ns, n = len * ns;
        for (int i = 0; i < n && a + i < s->n; i++) {
            float t = (float)i / RATE, dur = (float)n / RATE;
            float env = fminf(1, t * 200) * (0.55f + 0.45f * expf(-t * 12)) * fminf(1, (dur - t) * 40);
            float vib = t > 0.15f ? 1 + 0.006f * sinf(t * 34) : 1;
            ph += 2 * PI * f * vib / RATE;
            d[a + i] += osc(td->lead, ph) * env * (td->lead == 3 ? 0.32f : 0.2f);
        }
    }
    /* bass, pad, drums */
    for (int st = 0; st < steps; st++) {
        int chord = td->prog[(st / 8) % 4], pos = st % 8, a = st * ns;
        static const int bpat[8] = {0, -1, 7, 0, 0, -1, 7, 4};
        if (bpat[pos] >= 0) {
            float f = mtof((float)scale_note(td->root - 24, td->minor, chord + (bpat[pos] == 7 ? 7 : bpat[pos] == 4 ? 4 : 0))), ph = 0;
            for (int i = 0; i < ns * 2 && a + i < s->n; i++) {
                float t = (float)i / RATE;
                ph += 2 * PI * f / RATE;
                d[a + i] += osc(0, ph) * expf(-t * 5) * fminf(1, t * 400) * 0.32f;
            }
        }
        if (td->pad && pos == 0)
            for (int c = 0; c < 3; c++) {
                float f = mtof((float)scale_note(td->root - 12, td->minor, chord + c * 2)), ph = frand() * 6;
                for (int i = 0; i < ns * 8 && a + i < s->n; i++) {
                    float t = (float)i / RATE, T8 = ns * 8.0f / RATE;
                    ph += 2 * PI * f / RATE;
                    d[a + i] += sinf(ph) * fminf(1, t * 3) * fminf(1, (T8 - t) * 4) * 0.05f;
                }
            }
        if (td->drums >= 1) {
            if (pos == 0 || pos == 4) {
                float ph = 0;
                for (int i = 0; i < (int)(0.15f * RATE) && a + i < s->n; i++) {
                    float t = (float)i / RATE;
                    ph += 2 * PI * (50 + 120 * expf(-t * 30)) / RATE;
                    d[a + i] += sinf(ph) * expf(-t * 18) * 0.5f;
                }
            }
            float hv = (pos % 2 == 0) ? 0.07f : 0.045f;
            for (int i = 0; i < (int)(0.03f * RATE) && a + i < s->n; i++) {
                float t = (float)i / RATE;
                d[a + i] += frange(-1, 1) * expf(-t * 160) * hv;
            }
        }
        if (td->drums >= 2 && (pos == 2 || pos == 6)) {
            float lp = 0;
            for (int i = 0; i < (int)(0.14f * RATE) && a + i < s->n; i++) {
                float t = (float)i / RATE;
                lp += (frange(-1, 1) - lp) * 0.6f;
                d[a + i] += lp * expf(-t * 24) * 0.32f;
            }
        }
        if (td->drums == 0 && pos == 0) { /* cave: soft water-drop plink */
            float ph = 0;
            for (int i = 0; i < (int)(0.2f * RATE) && a + i < s->n; i++) {
                float t = (float)i / RATE;
                ph += 2 * PI * (1400 - 600 * t) / RATE;
                d[a + i] += sinf(ph) * expf(-t * 25) * 0.08f;
            }
        }
    }
    /* circular echo so the loop stays seamless */
    if (td->echo > 0) {
        int D = ns * 3;
        float *dry = malloc(sizeof(float) * (size_t)s->n);
        memcpy(dry, d, sizeof(float) * (size_t)s->n);
        for (int pass = 0; pass < 2; pass++)
            for (int i = 0; i < s->n; i++) d[i] = dry[i] + d[(i - D + s->n) % s->n] * td->echo;
        free(dry);
    }
    float pk = 0.001f;
    for (int i = 0; i < s->n; i++) pk = fmaxf(pk, fabsf(d[i]));
    for (int i = 0; i < s->n; i++) d[i] *= 0.9f / pk;
    rng = save;
}

static void set_music(int th, float vol) {
    if (!adev) return;
    if (th >= 0 && !tunes[th].d) build_tune(th);
    SDL_LockAudioDevice(adev);
    if (th < 0) music.d = NULL;
    else if (music.d != tunes[th].d) music = (Voice){tunes[th].d, tunes[th].n, 0, vol};
    music.vol = vol;
    SDL_UnlockAudioDevice(adev);
}

/* ---------------------------------------------------------------- pixel font (5x7) */
static const char FONT_CHARS[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ:.-!/?, +<>'";
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
    {14,17,1,2,4,0,4},{0,0,0,0,12,4,8},{0,0,0,0,0,0,0},{0,4,4,31,4,4,0},{2,4,8,16,8,4,2},{8,4,2,1,2,4,8},
    {4,4,8,0,0,0,0},
};

static SDL_Renderer *ren;

static float text_w(const char *s, float sc) { return (float)strlen(s) * 6 * sc - sc; }
static void glyph(char ch, float x, float y, float sc) {
    char c = ch >= 'a' && ch <= 'z' ? (char)(ch - 32) : ch;
    const char *at = c ? strchr(FONT_CHARS, c) : NULL;
    if (!at) return;
    const unsigned char *gl = FONT[at - FONT_CHARS];
    for (int row = 0; row < 7; row++)
        for (int col = 0; col < 5; col++)
            if (gl[row] & (16 >> col)) {
                SDL_FRect px = {x + col * sc, y + row * sc, sc, sc};
                SDL_RenderFillRectF(ren, &px);
            }
}
static void text(const char *s, float x, float y, float sc, Uint8 r, Uint8 g, Uint8 b, Uint8 a, bool center) {
    if (center) x -= text_w(s, sc) / 2;
    SDL_SetRenderDrawColor(ren, r, g, b, a);
    for (; *s; s++, x += 6 * sc) glyph(*s, x, y, sc);
}
/* outlined text, readable over any backdrop */
static void texto(const char *s, float x, float y, float sc, Uint8 r, Uint8 g, Uint8 b, Uint8 a, bool center) {
    float o = sc >= 2 ? sc * 0.5f : 1;
    for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++)
            if (dx || dy) text(s, x + dx * o, y + dy * o, sc, 24, 14, 32, a, center);
    text(s, x, y + (sc >= 2 ? o * 2 : 0), sc, 24, 14, 32, a, center);
    text(s, x, y, sc, r, g, b, a, center);
}

/* ---------------------------------------------------------------- render helpers */
static SDL_Texture *t_glow, *t_disc, *t_canvas, *t_light, *t_vign, *t_tiles, *t_layer[3], *t_fg;

static SDL_Texture *make_tex(int size, bool glow) {
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, size, size, 32, SDL_PIXELFORMAT_ARGB8888);
    Uint32 *px = s->pixels;
    float c = size / 2.0f;
    for (int y = 0; y < size; y++)
        for (int x = 0; x < size; x++) {
            float d = sqrtf((x + 0.5f - c) * (x + 0.5f - c) + (y + 0.5f - c) * (y + 0.5f - c)), a;
            if (glow) { float q = d / c; a = q < 1 ? (1 - q * q) * (1 - q * q) : 0; }
            else a = clampf(c - 1 - d + 0.5f, 0, 1);
            px[y * (s->pitch / 4) + x] = ((Uint32)(a * 255) << 24) | 0xFFFFFF;
        }
    SDL_Texture *t = SDL_CreateTextureFromSurface(ren, s);
    SDL_FreeSurface(s);
    SDL_SetTextureBlendMode(t, glow ? SDL_BLENDMODE_ADD : SDL_BLENDMODE_BLEND);
    return t;
}
static void glow(float x, float y, float r, Uint8 R, Uint8 Gc, Uint8 B, float k) {
    if (k <= 0) return;
    SDL_SetTextureColorMod(t_glow, R, Gc, B);
    SDL_SetTextureAlphaMod(t_glow, (Uint8)(clampf(k, 0, 1) * 255));
    SDL_FRect d = {x - r, y - r, 2 * r, 2 * r};
    SDL_RenderCopyF(ren, t_glow, NULL, &d);
}
static void disc(float x, float y, float r, Uint8 R, Uint8 Gc, Uint8 B, Uint8 A) {
    SDL_SetTextureColorMod(t_disc, R, Gc, B);
    SDL_SetTextureAlphaMod(t_disc, A);
    SDL_FRect d = {x - r, y - r, 2 * r, 2 * r};
    SDL_RenderCopyF(ren, t_disc, NULL, &d);
}
static void fill(float x, float y, float w, float h, Uint8 r, Uint8 g, Uint8 b, Uint8 a) {
    SDL_SetRenderDrawColor(ren, r, g, b, a);
    SDL_FRect q = {x, y, w, h};
    SDL_RenderFillRectF(ren, &q);
}
static SDL_Vertex vtx(float x, float y, Uint8 r, Uint8 g, Uint8 b, Uint8 a) {
    SDL_Vertex v;
    v.position.x = x; v.position.y = y;
    v.color.r = r; v.color.g = g; v.color.b = b; v.color.a = a;
    v.tex_coord.x = 0; v.tex_coord.y = 0;
    return v;
}
static void quad(SDL_Vertex a, SDL_Vertex b, SDL_Vertex c, SDL_Vertex d) {
    SDL_Vertex v[6] = {a, b, c, a, c, d};
    SDL_RenderGeometry(ren, NULL, v, 6, NULL, 0);
}

/* ---------------------------------------------------------------- sprite painter */
#define PMAX 64
static Uint32 pb[PMAX * PMAX];
static int pw, ph;
static Uint32 C(int r, int g, int b) { return 0xFF000000u | (Uint32)r << 16 | (Uint32)g << 8 | (Uint32)b; }
static Uint32 CA(int r, int g, int b, int a) { return (Uint32)a << 24 | (Uint32)r << 16 | (Uint32)g << 8 | (Uint32)b; }
static Uint32 shade(Uint32 c, float k) {
    int r = (int)clampf(((c >> 16) & 255) * k, 0, 255), g = (int)clampf(((c >> 8) & 255) * k, 0, 255), b = (int)clampf((c & 255) * k, 0, 255);
    return (c & 0xFF000000u) | (Uint32)r << 16 | (Uint32)g << 8 | (Uint32)b;
}
static Uint32 rgb3(const Uint8 *c) { return C(c[0], c[1], c[2]); }
static Uint32 mixc(Uint32 a, Uint32 b, float t) {
    int r = (int)lerpf((float)((a >> 16) & 255), (float)((b >> 16) & 255), t);
    int g = (int)lerpf((float)((a >> 8) & 255), (float)((b >> 8) & 255), t);
    int bb = (int)lerpf((float)(a & 255), (float)(b & 255), t);
    return 0xFF000000u | (Uint32)r << 16 | (Uint32)g << 8 | (Uint32)bb;
}
static void pnew(int w, int h) { pw = w; ph = h; memset(pb, 0, sizeof pb); }
static void pset(int x, int y, Uint32 c) { if (x >= 0 && y >= 0 && x < pw && y < ph) pb[y * PMAX + x] = c; }
static void prect(int x, int y, int w, int h, Uint32 c) { for (int j = y; j < y + h; j++) for (int i = x; i < x + w; i++) pset(i, j, c); }
static void pell(float cx, float cy, float rx, float ry, Uint32 c) {
    for (int y = 0; y < ph; y++)
        for (int x = 0; x < pw; x++) {
            float dx = (x + 0.5f - cx) / rx, dy = (y + 0.5f - cy) / ry;
            if (dx * dx + dy * dy <= 1) pset(x, y, c);
        }
}
static void ptri(float ax, float ay, float bx, float by, float cx, float cy, Uint32 c) {
    for (int y = 0; y < ph; y++)
        for (int x = 0; x < pw; x++) {
            float px = x + 0.5f, py = y + 0.5f;
            float d1 = (px - bx) * (ay - by) - (ax - bx) * (py - by);
            float d2 = (px - cx) * (by - cy) - (bx - cx) * (py - cy);
            float d3 = (px - ax) * (cy - ay) - (cx - ax) * (py - ay);
            bool neg = d1 < 0 || d2 < 0 || d3 < 0, pos = d1 > 0 || d2 > 0 || d3 > 0;
            if (!(neg && pos)) pset(x, y, c);
        }
}
static void poutline(Uint32 oc) {
    static Uint32 tmp[PMAX * PMAX];
    memcpy(tmp, pb, sizeof pb);
    for (int y = 0; y < ph; y++)
        for (int x = 0; x < pw; x++) {
            if (tmp[y * PMAX + x] >> 24) continue;
            static const int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};
            for (int k = 0; k < 4; k++) {
                int nx = x + dx[k], ny = y + dy[k];
                if (nx >= 0 && ny >= 0 && nx < pw && ny < ph && (tmp[ny * PMAX + nx] >> 24) >= 200) { pb[y * PMAX + x] = oc; break; }
            }
        }
}
static SDL_Texture *ptex(void) {
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, pw, ph, 32, SDL_PIXELFORMAT_ARGB8888);
    for (int y = 0; y < ph; y++) memcpy((Uint8 *)s->pixels + y * s->pitch, &pb[y * PMAX], (size_t)pw * 4);
    SDL_Texture *t = SDL_CreateTextureFromSurface(ren, s);
    SDL_FreeSurface(s);
    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    return t;
}

/* ------------- sprites */
enum { FOX_IDLE0, FOX_IDLE1, FOX_RUN0, FOX_RUN1, FOX_RUN2, FOX_RUN3, FOX_JUMP, FOX_FALL, FOX_HURT, FOX_N };
static SDL_Texture *t_fox[FOX_N], *t_beetle[3], *t_hedge[4], *t_bee[2], *t_seed, *t_leaf, *t_crate, *t_used,
    *t_brick, *t_spring[2], *t_lamp[2], *t_goal, *t_plat;

static SDL_Texture *paint_fox(int f) {
    pnew(20, 20);
    Uint32 O = C(238, 128, 48), D = C(170, 78, 36), Cr = C(255, 234, 200), E = C(30, 18, 28), Pk = C(255, 150, 150);
    float bob = (f == FOX_IDLE1 || f == FOX_RUN1 || f == FOX_RUN3) ? 1.0f : 0.0f;
    bool run = f >= FOX_RUN0 && f <= FOX_RUN3;
    float ty = run ? 9.5f : f == FOX_JUMP ? 13.0f : f == FOX_FALL ? 8.5f : 11.0f;
    pell(4.6f, ty + bob, 3.8f, 2.7f, O);
    pell(2.3f, ty - 0.6f + bob, 1.9f, 1.7f, Cr);
    /* legs */
    if (f == FOX_JUMP) { prect(7, 15, 2, 3, D); prect(12, 15, 2, 3, D); }
    else if (f == FOX_FALL || f == FOX_HURT) { prect(7, 16, 2, 4, D); prect(12, 16, 2, 4, D); }
    else {
        static const int bdx[4] = {-1, 0, 1, 0}, fdx[4] = {1, 0, -1, 0}, bl[4] = {0, 1, 0, 0}, fl[4] = {0, 0, 0, 1};
        int p = run ? f - FOX_RUN0 : 1;
        int top = 15 + (int)bob;
        prect(7 + bdx[p], top, 2, 20 - top - (run ? bl[p] : 0), D);
        prect(12 + fdx[p], top, 2, 20 - top - (run ? fl[p] : 0), D);
    }
    pell(10, 12.5f + bob, 4.7f, 4.1f, O);
    pell(11.7f, 13.7f + bob, 2.7f, 2.7f, Cr);
    pell(12.5f, 7 + bob, 4.4f, 3.9f, O);
    float ear = f == FOX_FALL ? -0.6f : 0;
    ptri(8.6f, 5 + bob, 9.8f, 0.2f + bob + ear, 12.4f, 4 + bob, O);
    ptri(12.2f, 4 + bob, 14.8f, 0.4f + bob + ear, 15.6f, 5 + bob, O);
    pset(10, 2 + (int)bob, D); pset(14, 2 + (int)bob, D);
    pell(15.7f, 8.7f + bob, 2.4f, 1.6f, Cr);
    pset(18, 8 + (int)bob, E);
    if (f == FOX_HURT) { pset(13, 5, E); pset(15, 5, E); pset(14, 6, E); pset(13, 7, E); pset(15, 7, E); }
    else { prect(14, 5 + (int)bob, 1, 2, E); pset(15, 5 + (int)bob, C(255, 255, 255)); }
    pset(13, 9 + (int)bob, Pk);
    poutline(C(46, 22, 30));
    return ptex();
}

static void paint_sprites(void) {
    for (int f = 0; f < FOX_N; f++) t_fox[f] = paint_fox(f);
    /* beetle */
    for (int f = 0; f < 3; f++) {
        pnew(16, 12);
        Uint32 sh = C(140, 82, 204), dk = C(66, 40, 92);
        if (f < 2) {
            for (int i = 0; i < 3; i++) prect(4 + i * 3 + (f ? 1 : 0), 9, 1, 3, dk);
            pell(7.5f, 7.5f, 6.5f, 6, sh);
            prect(0, 8, 16, 4, 0);
            for (int i = 0; i < 3; i++) prect(4 + i * 3 + (f ? 1 : 0), 8, 1, 3 + (i + f) % 2, dk);
            pell(5.5f, 4, 2.6f, 1.2f, C(206, 168, 246));
            prect(8, 2, 1, 6, C(100, 56, 150));
            pell(13.3f, 6.5f, 2.6f, 2.2f, dk);
            pset(14, 5, C(255, 255, 255)); pset(15, 5, C(20, 10, 20));
        } else {
            pell(8, 9.5f, 7.5f, 2.6f, sh);
            pell(6, 8.5f, 2.5f, 0.8f, C(206, 168, 246));
            pset(12, 9, C(20, 10, 20)); pset(14, 9, C(20, 10, 20));
        }
        poutline(C(36, 20, 50));
        t_beetle[f] = ptex();
    }
    /* hedgehog: 0/1 walking (spikes up), 2/3 asleep (spikes flat) */
    for (int f = 0; f < 4; f++) {
        pnew(18, 14);
        bool sleep = f >= 2;
        Uint32 body = C(150, 100, 64), sp = C(196, 196, 210), tip = C(255, 255, 255);
        float br = sleep && f == 3 ? 0.4f : 0;
        for (int i = 0; i < 6; i++) {
            float x = 3.5f + i * 2.2f, by = 9 - 4.2f * sqrtf(fmaxf(0, 1 - ((x - 8.5f) / 7.2f) * ((x - 8.5f) / 7.2f)));
            float len = sleep ? 1.6f : 5.0f;
            float ang = -PI / 2 + (x - 8.5f) * 0.12f;
            float tx = x + cosf(ang) * len, ty2 = by + sinf(ang) * len;
            ptri(x - 1.6f, by + 1, x + 1.6f, by + 1, tx, ty2, sp);
            if (!sleep) pset((int)tx, (int)ty2 + 1, tip);
        }
        pell(8.5f, 9.5f - br, 7.2f, 4.4f + br, body);
        pell(7, 8, 3, 1, shade(body, 1.25f));
        pell(14.5f, 10.2f, 2.8f, 2.4f, C(238, 210, 166));
        pset(17, 10, C(30, 20, 20));
        if (sleep) { pset(14, 9, C(40, 24, 24)); pset(15, 9, C(40, 24, 24)); }
        else { pset(15, 8, C(20, 10, 10)); pset(15, 9, C(20, 10, 10)); }
        prect(4 + (f == 1), 13, 2, 1, C(80, 50, 34)); prect(11 - (f == 1), 13, 2, 1, C(80, 50, 34));
        poutline(C(48, 28, 24));
        t_hedge[f] = ptex();
    }
    /* bee */
    for (int f = 0; f < 2; f++) {
        pnew(14, 12);
        ptri(0.5f, 7.5f, 3.5f, 6, 3.5f, 9, C(40, 30, 30));
        pell(7, 7.5f, 5, 3.6f, C(252, 212, 64));
        prect(5, 4, 1, 8, C(40, 30, 30)); prect(8, 4, 1, 8, C(40, 30, 30));
        pell(11.6f, 6.8f, 2.4f, 2.4f, C(52, 40, 40));
        pset(12, 6, C(255, 255, 255));
        poutline(C(40, 26, 26));
        if (f == 0) { pell(6, 2.6f, 2.6f, 2.2f, CA(225, 240, 255, 200)); pell(8.5f, 3, 1.8f, 1.6f, CA(225, 240, 255, 170)); }
        else { pell(6, 4.6f, 3.4f, 1.2f, CA(225, 240, 255, 200)); }
        t_bee[f] = ptex();
    }
    /* seed (golden acorn-seed) */
    pnew(10, 11);
    pell(5, 6.5f, 3.6f, 4.2f, C(255, 196, 46));
    pell(5, 3, 3.6f, 1.8f, C(196, 120, 40));
    prect(4, 0, 2, 2, C(140, 84, 30));
    pell(3.5f, 6, 1, 1.8f, C(255, 246, 190));
    poutline(C(110, 60, 20));
    t_seed = ptex();
    /* leaf */
    pnew(14, 10);
    for (int y = 0; y < 10; y++)
        for (int x = 0; x < 14; x++) {
            float u = (x + 0.5f - 7) * 0.94f + (y + 0.5f - 5) * 0.34f, v = -(x + 0.5f - 7) * 0.34f + (y + 0.5f - 5) * 0.94f;
            float wdt = 3.6f * (1 - (u / 6.4f) * (u / 6.4f));
            if (fabsf(u) < 6.4f && fabsf(v) < wdt) pset(x, y, fabsf(v) < 0.6f ? C(60, 140, 50) : v < 0 ? C(120, 214, 84) : C(86, 182, 64));
        }
    pset(0, 2, C(90, 70, 40)); pset(1, 3, C(90, 70, 40));
    poutline(C(30, 70, 30));
    t_leaf = ptex();
    /* lucky crate */
    pnew(16, 16);
    prect(0, 0, 16, 16, C(196, 136, 72));
    prect(1, 1, 14, 1, C(236, 186, 112)); prect(1, 1, 1, 14, C(226, 170, 98));
    prect(1, 5, 14, 1, C(156, 100, 52)); prect(1, 10, 14, 1, C(156, 100, 52));
    prect(14, 1, 1, 14, C(150, 96, 50)); prect(1, 14, 14, 1, C(140, 90, 46));
    {
        static const char *star[] = {"...#...", "..###..", "#######", ".#####.", ".##.##.", "#.....#"};
        for (int y = 0; y < 6; y++)
            for (int x = 0; x < 7; x++)
                if (star[y][x] == '#') pset(4 + x, 5 + y, C(255, 226, 92));
        for (int y = 0; y < 6; y++)
            for (int x = 0; x < 7; x++)
                if (star[y][x] == '#' && (y == 5 || star[y + 1][x] != '#')) pset(4 + x, 6 + y, C(150, 92, 40));
    }
    prect(0, 0, 16, 1, C(90, 52, 28)); prect(0, 15, 16, 1, C(90, 52, 28)); prect(0, 0, 1, 16, C(90, 52, 28)); prect(15, 0, 1, 16, C(90, 52, 28));
    t_crate = ptex();
    pnew(16, 16);
    prect(0, 0, 16, 16, C(118, 92, 76));
    prect(1, 1, 14, 1, C(146, 118, 98));
    prect(0, 0, 16, 1, C(66, 48, 40)); prect(0, 15, 16, 1, C(66, 48, 40)); prect(0, 0, 1, 16, C(66, 48, 40)); prect(15, 0, 1, 16, C(66, 48, 40));
    pset(3, 3, C(70, 54, 46)); pset(12, 3, C(70, 54, 46)); pset(3, 12, C(70, 54, 46)); pset(12, 12, C(70, 54, 46));
    t_used = ptex();
    /* spring mushroom */
    for (int f = 0; f < 2; f++) {
        pnew(16, 16);
        float cy = f ? 9.5f : 7;
        prect(5, (int)cy, 6, 16 - (int)cy, C(240, 226, 196));
        prect(9, (int)cy, 2, 16 - (int)cy, C(206, 190, 160));
        pell(8, cy, 7.4f, f ? 3.6f : 5, C(232, 62, 72));
        prect(0, (int)cy + 1, 16, 1, 0);
        pell(5, cy - 2, 1.4f, 1.1f, C(255, 240, 240)); pell(10, cy - 3, 1.6f, 1.2f, C(255, 240, 240)); pell(12.5f, cy - 0.5f, 1, 1, C(255, 240, 240));
        prect(1, (int)cy, 14, 1, C(170, 40, 52));
        poutline(C(70, 24, 34));
        t_spring[f] = ptex();
    }
    /* lantern checkpoint */
    for (int f = 0; f < 2; f++) {
        pnew(12, 24);
        prect(5, 9, 2, 15, C(104, 72, 52)); prect(5, 9, 1, 15, C(140, 100, 72));
        prect(2, 1, 8, 1, C(60, 44, 40)); prect(4, 0, 4, 1, C(60, 44, 40));
        prect(3, 2, 6, 6, f ? C(255, 214, 120) : C(86, 98, 120));
        if (f) { prect(5, 3, 2, 4, C(255, 250, 220)); }
        prect(3, 2, 1, 6, C(60, 44, 40)); prect(8, 2, 1, 6, C(60, 44, 40)); prect(2, 8, 8, 1, C(60, 44, 40));
        prect(3, 22, 6, 2, C(90, 90, 100));
        poutline(C(34, 24, 26));
        t_lamp[f] = ptex();
    }
    /* goal crystal obelisk */
    pnew(16, 48);
    prect(1, 40, 14, 8, C(112, 110, 132)); prect(1, 40, 14, 1, C(160, 158, 180)); prect(1, 44, 14, 1, C(90, 88, 108));
    for (int y = 0; y < 40; y++) {
        float half = y < 10 ? (float)y * 0.6f : 6.0f - (float)(y - 10) * 0.06f;
        for (int x = 0; x < 16; x++) {
            float dx = x + 0.5f - 8;
            if (fabsf(dx) > half) continue;
            Uint32 c = dx < -1.5f ? C(206, 255, 255) : dx < 1.5f ? C(126, 224, 252) : C(62, 162, 222);
            if (((y + x * 2) % 13) == 0) c = C(255, 255, 255);
            pset(x, y, c);
        }
    }
    poutline(C(24, 46, 78));
    t_goal = ptex();
}

/* sprites that depend on the world palette */
typedef struct {
    const char *name;
    Uint8 sky[4][3];
    Uint8 far[3], mid[3], nearc[3];
    Uint8 grass[3], grass2[3], dirt[3], dirt2[3], stone[3], water[3];
    Uint8 amb[3];
    bool lit;
} Theme;

static const Theme THEMES[4] = {
    {"MOSSY MEADOW", {{62, 128, 226}, {110, 176, 244}, {176, 220, 250}, {226, 242, 236}},
     {126, 156, 208}, {92, 170, 98}, {46, 112, 72},
     {118, 206, 72}, {72, 158, 52}, {152, 98, 58}, {116, 70, 42}, {150, 150, 164}, {64, 146, 226}, {255, 255, 255}, false},
    {"AMBER CLIFFS", {{56, 36, 102}, {162, 66, 118}, {246, 128, 82}, {255, 198, 118}},
     {158, 86, 120}, {122, 60, 76}, {64, 30, 50},
     {234, 170, 74}, {188, 116, 50}, {162, 94, 62}, {120, 62, 46}, {172, 124, 112}, {150, 90, 150}, {255, 255, 255}, false},
    {"GLIMMER HOLLOW", {{10, 8, 24}, {20, 16, 40}, {32, 26, 58}, {22, 18, 42}},
     {40, 34, 70}, {52, 42, 86}, {28, 22, 50},
     {84, 210, 226}, {48, 140, 170}, {78, 66, 104}, {54, 46, 78}, {100, 94, 134}, {40, 124, 156}, {56, 52, 96}, true},
    {"FROSTFALL PEAKS", {{8, 12, 40}, {22, 32, 84}, {46, 66, 130}, {88, 108, 170}},
     {74, 90, 146}, {54, 72, 126}, {28, 42, 86},
     {238, 246, 255}, {188, 208, 242}, {94, 102, 146}, {68, 74, 114}, {124, 134, 174}, {72, 124, 196}, {132, 142, 210}, true},
};

static void paint_theme_sprites(const Theme *th) {
    if (t_brick) SDL_DestroyTexture(t_brick);
    if (t_plat) SDL_DestroyTexture(t_plat);
    pnew(16, 16);
    Uint32 st = rgb3(th->stone), mortar = shade(st, 0.55f);
    prect(0, 0, 16, 16, mortar);
    for (int row = 0; row < 2; row++)
        for (int b = -1; b < 2; b++) {
            int x0 = b * 8 + (row ? 4 : 0) + 1, y0 = row * 8 + 1;
            for (int y = y0; y < y0 + 6; y++)
                for (int x = x0; x < x0 + 7; x++) {
                    float k = 0.9f + 0.2f * h01(x * 3 + row, y * 7 + b);
                    if (y == y0) k = 1.25f;
                    if (y == y0 + 5 || x == x0 + 6) k = 0.75f;
                    pset(x, y, shade(st, k));
                }
        }
    t_brick = ptex();
    pnew(48, 10);
    Uint32 g = rgb3(th->grass), s2 = shade(st, 0.85f);
    for (int y = 0; y < 10; y++)
        for (int x = 0; x < 48; x++) {
            Uint32 c = y < 3 ? (y == 0 ? shade(g, 1.1f) : g) : shade(s2, 1.0f - y * 0.04f + 0.08f * h01(x, y));
            if (y == 3 && (x * 7 % 5) < 2) c = shade(g, 0.8f);
            if (x == 0 || x == 47 || y == 9) c = shade(st, 0.45f);
            if (y > 3 && (x == 12 || x == 24 || x == 36)) c = shade(st, 0.6f);
            pset(x, y, c);
        }
    pset(0, 0, 0); pset(47, 0, 0); pset(0, 9, 0); pset(47, 9, 0);
    t_plat = ptex();
}

/* ---------------------------------------------------------------- levels */
/* Chunks are bottom-aligned; '.' empty  '#' ground  'B' brick  '?' seed crate  'L' leaf crate
   '=' one-way plank  '^' spikes  '~' water  'I' ice  'o' seed  'g' beetle  'h' hedgehog  'b' bee
   'S' spring  'C' lantern checkpoint  'G' goal  'M' sliding platform  'V' lifting platform  'P' start */
static const char *K_START_[] = {
    "......................",
    "......................",
    "........o.o.o.........",
    "......................",
    "........?..L..?.......",
    "......................",
    "......................",
    "..P...................",
    "######################",
    "######################",
    "######################", NULL};
static const char *K_MEADOW_[] = {
    "....................",
    ".............ooo....",
    "....................",
    "............#####...",
    "....................",
    "....................",
    ".....g..............",
    "####################",
    "####################",
    "####################", NULL};
static const char *K_GAP_[] = {
    "..................",
    "......o..o........",
    ".....o....o.......",
    "..................",
    "..................",
    "..................",
    "..................",
    "#######....#######",
    "#######~~~~#######",
    "#######~~~~#######", NULL};
static const char *K_STAIRS_[] = {
    "...............ooo..",
    "....................",
    "..............####..",
    "....................",
    "..........###.......",
    "....................",
    "......###...........",
    "####################",
    "####################",
    "####################", NULL};
static const char *K_BEETLES_[] = {
    "......................",
    "........o.o...........",
    "......................",
    "......B?B?B...........",
    "......................",
    "......................",
    "...........g.....g....",
    "######################",
    "######################",
    "######################", NULL};
static const char *K_PILLARS_[] = {
    "...............o......",
    "......................",
    "..........o....##.....",
    "..........##...##.....",
    "....o.....##...##.....",
    "....##....##...##.....",
    "....##..g.##.g.##.....",
    "######################",
    "######################",
    "######################", NULL};
static const char *K_SPRING_[] = {
    "....................",
    "....................",
    ".........o.o.o......",
    "........#######.....",
    "....................",
    "....................",
    "....................",
    "....................",
    "....................",
    ".....S..............",
    "####################",
    "####################",
    "####################", NULL};
static const char *K_PLATPIT_[] = {
    "........................",
    "..........o.o.o.........",
    "........................",
    "........................",
    "........................",
    "..........M.............",
    "........................",
    "#####..............#####",
    "#####~~~~~~~~~~~~~~#####",
    "#####~~~~~~~~~~~~~~#####", NULL};
static const char *K_HILL_[] = {
    "..........ooo...........",
    "........................",
    ".........#####..........",
    "........#######.........",
    ".......#########........",
    "......###########....b..",
    ".....#############......",
    "########################",
    "########################",
    "########################", NULL};
static const char *K_SPIKES_[] = {
    "......................",
    ".........ooo..........",
    "......................",
    "........=====.........",
    "......................",
    "......^^^^^^^^^.......",
    "######################",
    "######################",
    "######################", NULL};
static const char *K_HEDGE_[] = {
    "......................",
    "..........o...........",
    "......................",
    ".........?L?..........",
    "......................",
    "......................",
    ".......h.......h......",
    "######################",
    "######################",
    "######################", NULL};
static const char *K_BEES_[] = {
    ".........b......b.......",
    "........................",
    "........................",
    ".......===....===.......",
    "........................",
    "........................",
    "........................",
    "######..............####",
    "######~~~~~~~~~~~~~~####",
    "######~~~~~~~~~~~~~~####", NULL};
static const char *K_VERT_[] = {
    "....................",
    "..............ooo...",
    "....................",
    "............########",
    "............########",
    "............########",
    "............########",
    "............########",
    ".......V....########",
    "####################",
    "####################",
    "####################", NULL};
static const char *K_ICE_[] = {
    "....................",
    "...........ooo......",
    "....................",
    "....................",
    ".....g.........g....",
    "####IIIIIIIIIIII####",
    "####################",
    "####################", NULL};
static const char *K_MID_[] = {
    "..............",
    ".....o.o......",
    "..............",
    "..............",
    "....C.........",
    "##############",
    "##############",
    "##############", NULL};
static const char *K_END_[] = {
    "........................",
    "....ooo.................",
    "........................",
    "...###..................",
    "..............G.........",
    "########################",
    "########################",
    "########################", NULL};

enum { K_START, K_MEADOW, K_GAP, K_STAIRS, K_BEETLES, K_PILLARS, K_SPRING, K_PLATPIT, K_HILL, K_SPIKES,
       K_HEDGE, K_BEES, K_VERT, K_ICE, K_MID, K_END };
static const char **CHUNKS[] = {K_START_, K_MEADOW_, K_GAP_, K_STAIRS_, K_BEETLES_, K_PILLARS_, K_SPRING_,
                                K_PLATPIT_, K_HILL_, K_SPIKES_, K_HEDGE_, K_BEES_, K_VERT_, K_ICE_, K_MID_, K_END_};
static const int LV0[] = {K_START, K_MEADOW, K_GAP, K_STAIRS, K_BEETLES, K_SPRING, K_MID, K_PILLARS, K_HILL, K_SPIKES, K_END};
static const int LV1[] = {K_START, K_GAP, K_PLATPIT, K_BEETLES, K_HEDGE, K_MID, K_BEES, K_STAIRS, K_VERT, K_SPIKES, K_END};
static const int LV2[] = {K_START, K_HEDGE, K_PILLARS, K_GAP, K_MID, K_BEES, K_SPIKES, K_PLATPIT, K_BEETLES, K_HILL, K_END};
static const int LV3[] = {K_START, K_ICE, K_HEDGE, K_GAP, K_VERT, K_MID, K_BEES, K_ICE, K_PLATPIT, K_SPRING, K_SPIKES, K_END};
typedef struct { const int *ch; int n; } LevelDef;
static const LevelDef LEVELS[4] = {{LV0, 11}, {LV1, 11}, {LV2, 11}, {LV3, 12}};
#define NLEVELS 4

/* ---------------------------------------------------------------- game state */
typedef enum { ST_TITLE, ST_INTRO, ST_PLAY, ST_PAUSE, ST_DEAD, ST_CLEAR, ST_OVER, ST_WIN } State;
enum { E_SEED, E_POPSEED, E_LEAF, E_BEETLE, E_HEDGE, E_BEE, E_SPRING, E_CHECK, E_GOAL };
typedef struct { int type, dir, state; float x, y, vx, vy, w, h, t, x0, y0, aux; bool alive, active; } Ent;
typedef struct { float x, y, w, x0, x1, y0, y1, dx, dy, t, speed; bool vert; } Plat;
typedef struct { float x, y, vx, vy, life, max, size, grav; Uint8 r, g, b; bool add; } Part;
typedef struct { float x, y, t; char s[12]; } Pop;
typedef struct { float x, y, vx, vy, z, ph; } Amb;
typedef struct { float mx; bool jump, jumpHeld, run; } Input;
enum { D_NONE, D_TUFT, D_FLOWER, D_MUSH, D_CRYSTAL, D_SNOW, D_ROCK };

static char T[ROWS][MAXC];
static unsigned char deco[ROWS][MAXC];
static int cols;
static Ent ents[MAX_ENTS];
static int nents;
static Plat plats[MAX_PLATS];
static int nplats;
static Part parts[MAX_PARTS];
static Pop pops[MAX_POPS];
static Amb amb[MAX_AMB];
static struct { int r, c; float t; } bumps[8];

static struct {
    float x, y, vx, vy, w, h;
    bool ground, left, leafed, gliding, dead;
    float coyote, jbuf, inv, sx, sy, animT, runPh, noCut;
    int plat;
    float sc[SCARF_N][2];
} P;

static struct {
    State st, prevSt;
    float stateT, time, lvT, shake, camX, prevCam, iris;
    int level, lives, seeds, score, best, combo, deaths, checkCol, theme, bakedTheme, bonus;
    float spawnX, spawnY;
    bool newBest, attract;
} G;

static char best_path[1024];
static bool no_save;

static void load_best(void) {
    char *base = SDL_GetPrefPath("doomedl", "bramble-bound");
    if (!base) return;
    snprintf(best_path, sizeof best_path, "%sbest", base);
    SDL_free(base);
    FILE *f = fopen(best_path, "r");
    if (f) { if (fscanf(f, "%d", &G.best) != 1) G.best = 0; fclose(f); }
}
static void save_best(void) {
    if (no_save || !best_path[0]) return;
    FILE *f = fopen(best_path, "w");
    if (f) { fprintf(f, "%d\n", G.best); fclose(f); }
}

static bool solid(int r, int c) {
    if (c < 0 || c >= cols) return true;
    if (r < 0 || r >= ROWS) return false;
    char t = T[r][c];
    return t == '#' || t == 'B' || t == '?' || t == 'L' || t == 'U' || t == 'I' || t == 'X';
}
static bool static_solid(int r, int c) {
    if (c < 0 || c >= cols || r < 0 || r >= ROWS) return false;
    char t = T[r][c];
    return t == '#' || t == 'I' || t == 'X';
}
static char tile(int r, int c) { return (r < 0 || r >= ROWS || c < 0 || c >= cols) ? '.' : T[r][c]; }

/* ---------------------------------------------------------------- particles */
static void part(float x, float y, float vx, float vy, float life, float size, float grav, Uint8 r, Uint8 g, Uint8 b, bool add) {
    for (int i = 0; i < MAX_PARTS; i++) {
        if (parts[i].life > 0) continue;
        parts[i] = (Part){x, y, vx, vy, life, life, size, grav, r, g, b, add};
        return;
    }
}
static void burst(float x, float y, int n, float spd, float life, float size, float grav, Uint8 r, Uint8 g, Uint8 b, bool add) {
    for (int i = 0; i < n; i++) {
        float a = frand() * 2 * PI, s = spd * (0.3f + 0.7f * frand());
        part(x, y, cosf(a) * s, sinf(a) * s, life * (0.6f + 0.4f * frand()), size, grav, r, g, b, add);
    }
}
static void dust(float x, float y, int n) {
    for (int i = 0; i < n; i++)
        part(x + frange(-4, 4), y - 1, frange(-40, 40), frange(-30, -6), frange(0.25f, 0.5f), frange(1.5f, 3), -20, 220, 210, 196, false);
}
static void pop(float x, float y, const char *s) {
    for (int i = 0; i < MAX_POPS; i++)
        if (pops[i].t <= 0) {
            pops[i].x = x; pops[i].y = y; pops[i].t = 1.0f;
            snprintf(pops[i].s, sizeof pops[i].s, "%s", s);
            return;
        }
}
static void update_particles(float dt) {
    for (int i = 0; i < MAX_PARTS; i++) {
        Part *p = &parts[i];
        if (p->life <= 0) continue;
        p->life -= dt;
        p->vy += p->grav * dt;
        p->x += p->vx * dt;
        p->y += p->vy * dt;
    }
    for (int i = 0; i < MAX_POPS; i++)
        if (pops[i].t > 0) { pops[i].t -= dt; pops[i].y -= 24 * dt; }
}

/* ---------------------------------------------------------------- world baking */
static Uint32 *LP;
static int LPW, LPH, LPP;
static void lset(int x, int y, Uint32 c) { if (x >= 0 && y >= 0 && x < LPW && y < LPH) LP[y * LPP + x] = c; }
static Uint32 lget(int x, int y) { return (x >= 0 && y >= 0 && x < LPW && y < LPH) ? LP[y * LPP + x] : 0; }

/* periodic noise over the 960px parallax loop */
static float pnoise(float x, int k0, int k1, float ex, Uint32 seed) {
    float n = 0, norm = 0;
    for (int k = k0; k <= k1; k++) {
        float a = 1.0f / powf((float)k, ex);
        n += a * sinf(2 * PI * k * x / 960.0f + h01(k, (int)seed) * 6.283f);
        norm += a;
    }
    return n / norm;
}

static void tree_round(int cx, int base, int h, Uint32 col, Uint32 hi) {
    for (int y = base - h / 2; y < base; y++) lset(cx, y, shade(col, 0.6f)), lset(cx + 1, y, shade(col, 0.6f));
    int rr = h / 2;
    for (int y = -rr; y <= rr; y++)
        for (int x = -rr; x <= rr; x++) {
            float d = sqrtf((float)(x * x + y * y)) + h01(cx + x, y) * 1.5f;
            if (d > rr) continue;
            lset(cx + x, base - h / 2 - rr / 2 + y, (x + y < -rr / 2) ? hi : col);
        }
}
static void tree_pine(int cx, int base, int h, Uint32 col, Uint32 snow) {
    for (int y = 0; y < h; y++) {
        float t = (float)y / (float)h;
        float tier = fmodf(t * 4, 1.0f);
        float half = (0.15f + 0.85f * t) * h * 0.28f * (0.55f + 0.45f * tier);
        for (int x = (int)-half; x <= (int)half; x++) {
            Uint32 c = col;
            if (snow && tier < 0.25f && x < 0) c = snow;
            lset(cx + x, base - h + y, c);
        }
    }
    for (int y = 0; y < 3; y++) lset(cx, base + y - 1, shade(col, 0.7f));
}

static SDL_Texture *bake_layer(int th, int which) {
    const Theme *t = &THEMES[th];
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, 960, LH, 32, SDL_PIXELFORMAT_ARGB8888);
    memset(s->pixels, 0, (size_t)s->pitch * LH);
    LP = s->pixels; LPW = 960; LPH = LH; LPP = s->pitch / 4;
    Uint32 haze = rgb3(t->sky[3]);
    Uint32 col = rgb3(which == 0 ? t->far : which == 1 ? t->mid : t->nearc);
    Uint32 seed = (Uint32)(th * 10 + which);
    for (int x = 0; x < 960; x++) {
        float h = 0, htop = 0;
        if (which == 0) {
            float n = pnoise((float)x, 1, 16, 1.1f, seed);
            float ridge = 1 - fabsf(pnoise((float)x, 3, 12, 0.9f, seed + 7));
            h = th == 2 ? 70 + n * 30 : 108 + n * 44 + ridge * 30;
            if (th == 2) htop = 60 + pnoise((float)x, 2, 14, 0.8f, seed + 3) * 30;
        } else if (which == 1) {
            float n = pnoise((float)x, 1, 7, 1.2f, seed);
            if (th == 0) h = 66 + n * 22;
            else if (th == 1) { float raw = 84 + n * 50; h = floorf(raw / 20) * 20 + 6 + (raw - floorf(raw / 20) * 20) * 0.12f; }
            else if (th == 2) {
                h = 40 + n * 16 + 70 * powf(fabsf(sinf(2 * PI * 13 * x / 960.0f + 1.3f)), 9);
                htop = 34 + n * 10 + 60 * powf(fabsf(sinf(2 * PI * 9 * x / 960.0f + 0.4f)), 10);
            } else h = 58 + n * 16;
        } else {
            float n = pnoise((float)x, 2, 9, 1.0f, seed);
            h = th == 2 ? 26 + n * 10 + 90 * powf(fabsf(sinf(2 * PI * 7 * x / 960.0f + 2.1f)), 14) : 34 + n * 9;
            if (th == 2) htop = 24 + n * 8 + 80 * powf(fabsf(sinf(2 * PI * 5 * x / 960.0f + 0.2f)), 16);
        }
        float hprev = h;
        if (which == 0 && x > 0) {
            float n2 = pnoise((float)(x - 1), 1, 16, 1.1f, seed), r2 = 1 - fabsf(pnoise((float)(x - 1), 3, 12, 0.9f, seed + 7));
            hprev = th == 2 ? 70 + n2 * 30 : 108 + n2 * 44 + r2 * 30;
        }
        int top = LH - (int)h;
        for (int y = top; y < LH; y++) {
            float depth = (float)(y - top) / fmaxf(h, 1);
            Uint32 c = col;
            if (which == 0) {
                c = shade(col, h > hprev ? 1.12f : 0.92f);
                if ((th == 0 || th == 3) && h > 150 && y - top < 5 + (int)(h01(x, 3) * 6) + (int)((h - 150) * 0.3f)) c = mixc(C(240, 246, 255), c, 0.15f);
                c = mixc(c, haze, clampf(depth * 0.7f, 0, 0.7f));
            } else if (which == 1) {
                c = shade(col, 1.0f - depth * 0.25f);
                if (th == 1 && ((y - top) % 9 == 0)) c = shade(c, 0.85f);
                if (th == 3 && y - top < 3) c = C(214, 226, 250);
                if (y == top) c = shade(c, 1.25f);
                if (((x + y) & 1) && h01(x / 2, y / 2) < 0.18f) c = shade(c, 0.9f);
                c = mixc(c, haze, th == 2 ? 0.0f : 0.25f);
            } else {
                c = shade(col, 1.0f - depth * 0.3f);
                if (th == 2 && h > 60 && (x % 23) < 2) c = mixc(c, C(90, 220, 240), 0.35f);
                if (y == top) c = shade(c, th == 3 ? 1.6f : 1.25f);
            }
            lset(x, y, c);
        }
        for (int y = 0; y < (int)htop; y++) {
            float depth = (float)((int)htop - y) / fmaxf(htop, 1);
            lset(x, y, shade(col, 1.0f - depth * 0.3f));
        }
    }
    /* decorations on top of silhouettes */
    if (th == 0 && which >= 1) {
        for (int i = 0; i < (which == 1 ? 30 : 14); i++) {
            int cx = (int)(h01(i, which * 31) * 960);
            int y = LH - 1;
            while (y > 0 && (lget(cx, y - 1) >> 24)) y--;
            int hh = which == 1 ? 10 + (int)(h01(i, 9) * 8) : 26 + (int)(h01(i, 5) * 22);
            tree_round(cx, y + 2, hh, shade(col, 0.85f), shade(col, 1.2f));
        }
    } else if (th == 3 && which >= 1) {
        for (int i = 0; i < (which == 1 ? 46 : 18); i++) {
            int cx = (int)(h01(i, which * 17 + 3) * 960);
            int y = LH - 1;
            while (y > 0 && (lget(cx, y - 1) >> 24)) y--;
            int hh = which == 1 ? 14 + (int)(h01(i, 2) * 14) : 34 + (int)(h01(i, 8) * 30);
            tree_pine(cx, y + 2, hh, shade(col, 0.8f), which == 1 ? C(200, 214, 244) : C(150, 166, 210));
        }
    } else if (th == 1 && which == 2) {
        for (int i = 0; i < 9; i++) {
            int cx = (int)(h01(i, 77) * 960), y = LH - 1;
            while (y > 0 && (lget(cx, y - 1) >> 24)) y--;
            int hh = 30 + (int)(h01(i, 4) * 26); /* bare windswept trees */
            for (int k = 0; k < hh; k++) { int xx = cx + (int)(k * k * 0.006f); lset(xx, y - k, col); lset(xx + 1, y - k, col); }
            for (int b = 0; b < 5; b++) {
                int by = y - hh / 2 - b * hh / 10, bl = 6 + b * 2;
                for (int k = 0; k < bl; k++) lset(cx + (int)(hh * hh * 0.003f) + (b % 2 ? k : -k), by - k / 2, col);
            }
        }
    }
    SDL_Texture *tex = SDL_CreateTextureFromSurface(ren, s);
    SDL_FreeSurface(s);
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    return tex;
}

static SDL_Texture *bake_fg(int th) {
    const Theme *t = &THEMES[th];
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, 960, 40, 32, SDL_PIXELFORMAT_ARGB8888);
    memset(s->pixels, 0, (size_t)s->pitch * 40);
    LP = s->pixels; LPW = 960; LPH = 40; LPP = s->pitch / 4;
    Uint32 c = shade(rgb3(t->nearc), 0.45f);
    for (int x = 0; x < 960; x++) {
        int h = 6 + (int)(pnoise((float)x, 3, 20, 0.8f, 99 + (Uint32)th) * 6);
        for (int y = 40 - h; y < 40; y++) lset(x, y, c);
    }
    for (int i = 0; i < 160; i++) { /* blades / spikes / drifts */
        int cx = (int)(h01(i, 5) * 960), hh = 6 + (int)(h01(i, 6) * 18);
        float lean = (h01(i, 7) - 0.5f) * 0.8f;
        for (int k = 0; k < hh; k++) {
            int wdt = th == 3 ? (hh - k) / 2 : (k < hh / 2 ? 1 : 0);
            for (int w = -wdt; w <= wdt; w++) lset(cx + (int)(k * lean) + w, 39 - k, c);
        }
    }
    SDL_Texture *tex = SDL_CreateTextureFromSurface(ren, s);
    SDL_FreeSurface(s);
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    return tex;
}

static void bake_tiles(void) {
    const Theme *th = &THEMES[G.theme];
    int Wp = cols * TS, Hp = ROWS * TS;
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, Wp, Hp, 32, SDL_PIXELFORMAT_ARGB8888);
    memset(s->pixels, 0, (size_t)s->pitch * Hp);
    LP = s->pixels; LPW = Wp; LPH = Hp; LPP = s->pitch / 4;
    Uint32 grass = rgb3(th->grass), grass2 = rgb3(th->grass2), dirt = rgb3(th->dirt), dirt2 = rgb3(th->dirt2), stone = rgb3(th->stone);
    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < cols; c++) {
            char t = T[r][c];
            int X = c * TS, Y = r * TS;
            if (t == '#' || t == 'X' || t == 'I') {
                bool top = !static_solid(r - 1, c) && r > 0, left = !static_solid(r, c - 1) && c > 0,
                     right = !static_solid(r, c + 1) && c < cols - 1, bot = r < ROWS - 1 && !static_solid(r + 1, c);
                int depth = 0;
                for (int k = 1; k <= 4 && static_solid(r - k, c); k++) depth++;
                for (int py = 0; py < TS; py++)
                    for (int px = 0; px < TS; px++) {
                        int wx = X + px, wy = Y + py;
                        if (top && left && px + py < 2) continue;
                        if (top && right && (15 - px) + py < 2) continue;
                        if (bot && left && px + (15 - py) < 2) continue;
                        if (bot && right && (15 - px) + (15 - py) < 2) continue;
                        Uint32 col;
                        if (t == 'I') {
                            col = C(172, 222, 255);
                            if (py < 2 && top) col = C(236, 248, 255);
                            if (((px - py + 32) % 11) == 0 || ((px - py + 33) % 11) == 0) col = C(214, 240, 255);
                            if (h01(wx, wy) < 0.04f) col = C(255, 255, 255);
                        } else if (t == 'X') {
                            float n = h01(wx / 4, wy / 3 + 50);
                            col = shade(stone, 0.75f + 0.3f * n);
                            if (hash2(wx, wy) % 23 == 0) col = shade(stone, 0.55f);
                            if (th->lit && hash2(wx, wy * 3) % 211 == 0) col = C(120, 230, 250);
                        } else {
                            float n = h01(wx / 3, wy / 3 + 1000);
                            col = n < 0.33f ? dirt2 : dirt;
                            if (hash2(wx, wy) % 97 == 0) col = shade(stone, 1.1f);
                            if (hash2(wx + 1, wy) % 61 == 0) col = shade(col, 1.18f);
                            if (G.theme == 2 && hash2(wx, wy * 7) % 300 == 0) col = C(120, 230, 250);
                            col = shade(col, 1.0f - 0.07f * depth - py * 0.004f);
                            if (top) {
                                int gd = (G.theme == 3 ? 4 : 3) + (int)(hash2(wx, 77) % 3);
                                if (py < gd) col = py == 0 ? shade(grass, 1.12f) : ((py == gd - 1 && (hash2(wx, 5) & 1)) ? grass2 : grass);
                                else if (py == gd) col = shade(dirt2, 0.62f);
                            }
                        }
                        if ((left && px == 0) || (right && px == 15) || (bot && py == 15) || (top && py == 0 && t != '#')) col = shade(col, 0.5f);
                        else if ((left && px == 1) || (right && px == 14) || (bot && py == 14)) col = shade(col, 0.8f);
                        lset(wx, wy, col);
                    }
                /* hanging details below exposed undersides */
                if (bot && r < ROWS - 1) {
                    for (int k = 0; k < 2; k++) {
                        Uint32 hsh = hash2(c * 3 + k, r * 5);
                        if (hsh % 3) continue;
                        int hx = X + 3 + (int)(hsh % 10), L = 3 + (int)((hsh >> 8) % 8);
                        for (int j = 0; j < L; j++) {
                            float half = (float)(L - j) / (float)L * 1.6f;
                            for (int q = (int)-half; q <= (int)half; q++) {
                                Uint32 hc = t == 'I' || G.theme == 3 ? C(200, 236, 255) : t == 'X' || G.theme == 2 ? shade(stone, 0.8f) : C(90, 140, 60);
                                if (G.theme == 0 && t == '#') { if (q) continue; hc = j % 3 == 2 ? C(120, 190, 80) : C(80, 130, 56); } /* roots/vines */
                                lset(hx + q, Y + 16 + j, hc);
                            }
                        }
                    }
                }
            } else if (t == '=') {
                for (int py = 0; py < 6; py++)
                    for (int px = 0; px < TS; px++) {
                        Uint32 col = py == 0 ? C(214, 160, 96) : py == 5 ? C(92, 56, 30) : shade(C(166, 110, 58), 0.9f + 0.2f * h01((X + px) / 4, py));
                        if (py == 2 && (px == 2 || px == 13)) col = C(70, 60, 60);
                        if ((px == 0 && tile(r, c - 1) != '=') || (px == 15 && tile(r, c + 1) != '=')) col = C(92, 56, 30);
                        lset(X + px, Y + py, col);
                    }
                if (tile(r, c - 1) != '=' || tile(r, c + 1) != '=')
                    for (int k = 0; k < 5; k++) lset(X + (tile(r, c - 1) != '=' ? 2 + k / 2 : 13 - k / 2), Y + 6 + k, C(92, 56, 30));
            } else if (t == '^') {
                for (int py = 6; py < TS; py++)
                    for (int px = 0; px < TS; px++) {
                        float cx = 2.67f + 5.33f * (float)(px * 3 / TS), dx = px + 0.5f - cx;
                        float half = (float)(py - 6) / 10.0f * 2.7f;
                        if (fabsf(dx) > half) continue;
                        Uint32 col = dx < -0.5f ? C(232, 236, 246) : dx < 0.6f ? C(186, 190, 206) : C(124, 128, 150);
                        if (fabsf(dx) > half - 0.8f) col = C(60, 60, 78);
                        if (py == 15) col = C(70, 70, 90);
                        lset(X + px, Y + py, col);
                    }
            }
        }
    if (t_tiles) SDL_DestroyTexture(t_tiles);
    t_tiles = SDL_CreateTextureFromSurface(ren, s);
    SDL_SetTextureBlendMode(t_tiles, SDL_BLENDMODE_BLEND);
    SDL_FreeSurface(s);
}

/* ---------------------------------------------------------------- level loading */
static Ent *spawn(int type, float x, float y, float w, float h) {
    Ent *e = NULL;
    for (int i = 0; i < nents; i++) if (!ents[i].alive) { e = &ents[i]; break; }
    if (!e) { if (nents >= MAX_ENTS) return NULL; e = &ents[nents++]; }
    memset(e, 0, sizeof *e);
    e->type = type; e->x = e->x0 = x; e->y = e->y0 = y; e->w = w; e->h = h;
    e->alive = true; e->dir = -1; e->t = frand() * 10;
    return e;
}

static void reset_amb(void) {
    for (int i = 0; i < MAX_AMB; i++) {
        amb[i] = (Amb){frange(0, LW), frange(0, LH), 0, 0, frange(0.4f, 1.6f), frand() * 10};
    }
}

static void load_level(int idx, bool atCheck) {
    memset(T, '.', sizeof T);
    memset(deco, 0, sizeof deco);
    cols = 0; nents = 0; nplats = 0;
    memset(parts, 0, sizeof parts);
    memset(pops, 0, sizeof pops);
    memset(bumps, 0, sizeof bumps);
    G.theme = idx;
    float sx = 40, sy = 100;
    const LevelDef *L = &LEVELS[idx];
    for (int k = 0; k < L->n; k++) {
        const char **rows = CHUNKS[L->ch[k]];
        int n = 0, w = 0;
        while (rows[n]) { int l = (int)strlen(rows[n]); if (l > w) w = l; n++; }
        for (int i = 0; i < n; i++) {
            int r = ROWS - n + i, len = (int)strlen(rows[i]);
            for (int cc = 0; cc < w; cc++) {
                char ch = cc < len ? rows[i][cc] : '.';
                int c = cols + cc;
                float x = (float)(c * TS), y = (float)(r * TS);
                Ent *e;
                switch (ch) {
                case '#': case 'B': case '?': case 'L': case 'I': case '=': case '^': case '~': T[r][c] = ch; break;
                case 'o': spawn(E_SEED, x + 3, y + 3, 10, 11); break;
                case 'g': spawn(E_BEETLE, x + 1, y + 4, 14, 12); break;
                case 'h': spawn(E_HEDGE, x, y + 4, 16, 12); break;
                case 'b': if ((e = spawn(E_BEE, x + 2, y + 3, 12, 10))) e->t = (float)c; break;
                case 'S': spawn(E_SPRING, x, y, 16, 16); break;
                case 'C': spawn(E_CHECK, x + 2, y - 8, 12, 24); break;
                case 'G': spawn(E_GOAL, x, y + 16 - 48, 16, 48); break;
                case 'P': sx = x + 3; sy = y + 2; break;
                case 'M': case 'V':
                    if (nplats < MAX_PLATS) {
                        Plat *p = &plats[nplats++];
                        memset(p, 0, sizeof *p);
                        p->x = x; p->y = y; p->w = 48; p->vert = ch == 'V';
                        p->x0 = p->x1 = x; p->y0 = y; p->y1 = y - 5 * TS;
                        p->speed = p->vert ? 0.32f : 0.22f;
                    }
                    break;
                default: break;
                }
            }
        }
        cols += w;
    }
    if (idx == 2) /* cave ceiling */
        for (int c = 0; c < cols; c++) {
            for (int r = 0; r < 3; r++) T[r][c] = 'X';
            if (h01(c, 9) < 0.3f && T[4][c] == '.' && T[5][c] == '.') T[3][c] = 'X';
        }
    for (int i = 0; i < nplats; i++) {
        Plat *p = &plats[i];
        if (p->vert) continue;
        int c = (int)(p->x / TS), l = c, r = c;
        while (l > 0 && !solid(ROWS - 3, l - 1)) l--;
        while (r < cols - 1 && !solid(ROWS - 3, r + 1)) r++;
        p->x0 = (float)(l * TS + 2);
        p->x1 = (float)((r + 1) * TS) - p->w - 2;
    }
    for (int r = 1; r < ROWS; r++)
        for (int c = 0; c < cols; c++) {
            if (T[r][c] != '#' || T[r - 1][c] != '.') continue;
            float h = h01(c * 7 + r, idx * 13 + 1);
            unsigned char d = D_NONE;
            switch (idx) {
            case 0: d = h < 0.45f ? D_TUFT : h < 0.58f ? D_FLOWER : h < 0.63f ? D_ROCK : D_NONE; break;
            case 1: d = h < 0.4f ? D_TUFT : h < 0.47f ? D_FLOWER : h < 0.54f ? D_ROCK : D_NONE; break;
            case 2: d = h < 0.14f ? D_MUSH : h < 0.24f ? D_CRYSTAL : h < 0.5f ? D_TUFT : D_NONE; break;
            default: d = h < 0.12f ? D_CRYSTAL : h < 0.34f ? D_SNOW : h < 0.4f ? D_ROCK : D_NONE; break;
            }
            deco[r - 1][c] = d;
        }
    G.checkCol = atCheck ? G.checkCol : -1;
    if (atCheck && G.checkCol >= 0) {
        for (int i = 0; i < nents; i++)
            if (ents[i].type == E_CHECK && (int)(ents[i].x / TS) == G.checkCol) {
                ents[i].state = 1;
                sx = ents[i].x; sy = ents[i].y + 24 - 14;
            }
    }
    memset(&P, 0, sizeof P);
    P.x = sx; P.y = sy; P.w = 10; P.h = 14; P.sx = P.sy = 1; P.plat = -1;
    for (int i = 0; i < SCARF_N; i++) { P.sc[i][0] = P.x; P.sc[i][1] = P.y + 6; }
    G.spawnX = sx; G.spawnY = sy;
    G.camX = clampf(P.x - LW * 0.4f, 0, (float)(cols * TS - LW));
    G.prevCam = G.camX;
    G.lvT = 0; G.combo = 0;
    paint_theme_sprites(&THEMES[idx]);
    bake_tiles();
    if (G.bakedTheme != idx) {
        for (int i = 0; i < 3; i++) { if (t_layer[i]) SDL_DestroyTexture(t_layer[i]); t_layer[i] = bake_layer(idx, i); }
        if (t_fg) SDL_DestroyTexture(t_fg);
        t_fg = bake_fg(idx);
        G.bakedTheme = idx;
        reset_amb();
    }
}

/* ---------------------------------------------------------------- gameplay */
static bool move_x(float *x, float y, float w, float h, float dx) {
    if (dx == 0) return false;
    *x += dx;
    int r0 = (int)floorf(y / TS), r1 = (int)floorf((y + h - 0.01f) / TS);
    if (dx > 0) {
        int c = (int)floorf((*x + w - 0.01f) / TS);
        for (int r = r0; r <= r1; r++) if (solid(r, c)) { *x = (float)(c * TS) - w; return true; }
    } else {
        int c = (int)floorf(*x / TS);
        for (int r = r0; r <= r1; r++) if (solid(r, c)) { *x = (float)((c + 1) * TS); return true; }
    }
    return false;
}
/* 1 = landed, -1 = hit ceiling (hr/hc = tile nearest the centre) */
static int move_y(float x, float *y, float w, float h, float dy, int *hr, int *hc) {
    if (dy == 0) return 0;
    float oldB = *y + h;
    *y += dy;
    int c0 = (int)floorf(x / TS), c1 = (int)floorf((x + w - 0.01f) / TS);
    if (dy > 0) {
        int r = (int)floorf((*y + h - 0.01f) / TS);
        for (int c = c0; c <= c1; c++)
            if (solid(r, c) || (tile(r, c) == '=' && oldB <= r * TS + 0.5f)) { *y = (float)(r * TS) - h; return 1; }
    } else {
        int r = (int)floorf(*y / TS), best = -1;
        float bd = 1e9f;
        for (int c = c0; c <= c1; c++)
            if (solid(r, c)) {
                float d = fabsf((c + 0.5f) * TS - (x + w / 2));
                if (d < bd) { bd = d; best = c; }
            }
        if (best >= 0) { *y = (float)((r + 1) * TS); if (hr) *hr = r; if (hc) *hc = best; return -1; }
    }
    return 0;
}

static void add_score(int v, float x, float y) {
    G.score += v;
    if (x > -1000) { char b[12]; snprintf(b, sizeof b, "%d", v); pop(x, y, b); }
}
static void collect_seed(float x, float y) {
    G.seeds++;
    G.score += 10;
    play(S_SEED, 0.4f);
    burst(x, y, 6, 50, 0.4f, 1.5f, 0, 255, 230, 120, true);
    if (G.seeds >= 50) { G.seeds -= 50; G.lives++; play(S_LIFE, 0.5f); pop(x, y - 8, "1UP"); }
}

static const char *death_cause = "";
static void kill_player(const char *why) {
    if (!P.dead) death_cause = why;
    if (P.dead) return;
    P.dead = true; P.vy = -320; P.vx = 0; P.leafed = false; P.gliding = false;
    G.st = ST_DEAD; G.stateT = 0; G.shake = 4;
    play(S_DIE, 0.5f);
    set_music(G.theme, 0.0f);
}
static void hurt_player(const char *why) {
    if (P.inv > 0 || P.dead) return;
    if (P.leafed) {
        P.leafed = false; P.inv = 1.6f; G.shake = 3;
        play(S_HURT, 0.5f);
        for (int i = 0; i < 8; i++) part(P.x + 5, P.y, frange(-60, 60), frange(-90, -20), 0.8f, 2, 200, 110, 210, 80, false);
        return;
    }
    kill_player(why);
}
static void knock(Ent *e) {
    e->state = 2; e->vy = -230; e->vx = frange(-40, 40);
    add_score(100, e->x + e->w / 2, e->y);
    play(S_STOMP, 0.5f);
}

static void bump_tile(int r, int c) {
    char t = T[r][c];
    for (int i = 0; i < 8; i++) if (bumps[i].t <= 0) { bumps[i].r = r; bumps[i].c = c; bumps[i].t = 0.2f; break; }
    for (int i = 0; i < nents; i++) {
        Ent *e = &ents[i];
        if (!e->alive) continue;
        bool onTop = e->x + e->w > c * TS && e->x < c * TS + TS && fabsf(e->y + e->h - r * TS) < 4;
        if (!onTop) continue;
        if ((e->type == E_BEETLE || e->type == E_HEDGE) && e->state == 0) knock(e);
        if (e->type == E_SEED) { e->alive = false; collect_seed(e->x + 5, e->y + 5); }
    }
    if (t == '?') {
        T[r][c] = 'U';
        Ent *e = spawn(E_POPSEED, (float)(c * TS + 3), (float)(r * TS - 12), 10, 11);
        if (e) { e->vy = -260; e->t = 0; }
        collect_seed((float)(c * TS + 8), (float)(r * TS - 8));
    } else if (t == 'L') {
        T[r][c] = 'U';
        Ent *e = spawn(E_LEAF, (float)(c * TS + 1), (float)(r * TS + 2), 14, 10);
        if (e) { e->t = 0; e->y0 = e->y; }
        play(S_SPROUT, 0.5f);
    } else if (t == 'B') {
        if (P.leafed) {
            T[r][c] = '.';
            for (int k = 0; k < 4; k++)
                part((float)(c * TS + 4 + (k % 2) * 8), (float)(r * TS + 4 + (k / 2) * 8), (k % 2 ? 60.0f : -60.0f), -200.0f + (k / 2) * 80,
                     1.2f, 5, 900, THEMES[G.theme].stone[0], THEMES[G.theme].stone[1], THEMES[G.theme].stone[2], false);
            play(S_BREAK, 0.6f);
            G.score += 50; G.shake = 2;
        } else play(S_BUMP, 0.4f);
    } else play(S_BUMP, 0.35f);
}

static void update_plats(float dt) {
    for (int i = 0; i < nplats; i++) {
        Plat *p = &plats[i];
        p->t += dt * p->speed;
        float k = 0.5f - 0.5f * cosf(p->t * 2 * PI * 0.5f);
        float nx = p->vert ? p->x : lerpf(p->x0, p->x1, k);
        float ny = p->vert ? lerpf(p->y0, p->y1, k) : p->y;
        p->dx = nx - p->x; p->dy = ny - p->y;
        p->x = nx; p->y = ny;
    }
}

static void update_player(float dt, Input in) {
    P.animT += dt;
    if (P.inv > 0) P.inv -= dt;
    P.noCut -= dt;
    float maxv = in.run ? 168 : 108;
    int fr = (int)floorf((P.y + P.h + 1) / TS), fc = (int)floorf((P.x + P.w / 2) / TS);
    bool ice = P.ground && tile(fr, fc) == 'I';
    float acc = P.ground ? (ice ? 260.0f : 950.0f) : 650.0f, fric = P.ground ? (ice ? 70.0f : 1200.0f) : 160.0f;
    if (in.mx != 0) {
        if (P.vx * in.mx < 0) {
            acc *= ice ? 1.2f : 2.0f;
            if (P.ground && fabsf(P.vx) > 60 && frand() < 0.5f) dust(P.x + P.w / 2, P.y + P.h, 1);
        }
        if (fabsf(P.vx) <= maxv) { P.vx += in.mx * acc * dt; if (fabsf(P.vx) > maxv) P.vx = in.mx * maxv; }
        else P.vx = approach(P.vx, in.mx * maxv, fric * 0.5f * dt);
        P.left = in.mx < 0;
    } else P.vx = approach(P.vx, 0, fric * dt);

    P.coyote = P.ground ? 0.09f : P.coyote - dt;
    P.jbuf = in.jump ? 0.13f : P.jbuf - dt;
    if (P.jbuf > 0 && P.coyote > 0) {
        P.vy = -(355 + fabsf(P.vx) * 0.2f);
        P.ground = false; P.coyote = 0; P.jbuf = 0; P.plat = -1;
        P.sx = 0.75f; P.sy = 1.3f;
        play(S_JUMP, 0.32f);
        dust(P.x + P.w / 2, P.y + P.h, 4);
    }
    if (!in.jumpHeld && P.vy < -150 && P.noCut <= 0) P.vy = -150;
    P.vy += ((P.vy < 0 && in.jumpHeld) ? 860.0f : 1150.0f) * dt;
    P.gliding = P.leafed && in.jumpHeld && P.vy > 0 && !P.ground;
    if (P.gliding) {
        P.vy = approach(P.vy, fminf(P.vy, 55), 2400 * dt);
        if (frand() < dt * 8) part(P.x + 5 + frange(-6, 6), P.y - 4, frange(-10, 10), 20, 0.6f, 1, 10, 150, 230, 110, true);
    }
    if (P.vy > 440) P.vy = 440;

    if (P.plat >= 0) {
        Plat *pl = &plats[P.plat];
        move_x(&P.x, P.y, P.w, P.h, pl->dx);
        P.y = pl->y - P.h;
    }
    bool wasGround = P.ground;
    float prevB = P.y + P.h, landV = P.vy;
    if (move_x(&P.x, P.y, P.w, P.h, P.vx * dt)) P.vx = 0;
    int hr = -1, hc = -1, res = move_y(P.x, &P.y, P.w, P.h, P.vy * dt, &hr, &hc);
    P.ground = false;
    int onPlat = -1;
    if (res == 1) { P.ground = true; P.vy = 0; }
    else if (res == -1) { P.vy = 40; bump_tile(hr, hc); }
    for (int i = 0; i < nplats && !P.ground; i++) {
        Plat *pl = &plats[i];
        if (P.vy >= 0 && P.x + P.w > pl->x + 1 && P.x < pl->x + pl->w - 1 && prevB <= pl->y + 2 + fabsf(pl->dy) && P.y + P.h >= pl->y) {
            P.y = pl->y - P.h; P.vy = 0; P.ground = true; onPlat = i;
        }
    }
    P.plat = onPlat;
    if (P.ground) {
        G.combo = 0;
        if (!wasGround && landV > 220) { P.sx = 1.3f; P.sy = 0.72f; dust(P.x + P.w / 2, P.y + P.h, 5); }
    }
    P.sx = approach(P.sx, 1, dt * 3.5f);
    P.sy = approach(P.sy, 1, dt * 3.5f);
    if (P.ground && fabsf(P.vx) > 10) P.runPh += fabsf(P.vx) * dt * 0.075f;

    /* hazards */
    int c0 = (int)floorf(P.x / TS), c1 = (int)floorf((P.x + P.w - 0.01f) / TS), r0 = (int)floorf(P.y / TS), r1 = (int)floorf((P.y + P.h - 0.01f) / TS);
    for (int r = r0; r <= r1; r++)
        for (int c = c0; c <= c1; c++) {
            char t = tile(r, c);
            if (t == '^' && overlap(P.x, P.y, P.w, P.h, (float)(c * TS + 2), (float)(r * TS + 9), 12, 7)) {
                hurt_player("spikes");
                if (!P.dead) P.vy = -280;
            } else if (t == '~' && P.y + P.h > r * TS + 6 && !P.dead) {
                for (int k = 0; k < 18; k++) part(P.x + 5, (float)(r * TS + 4), frange(-70, 70), frange(-200, -60), 0.7f, 2, 600,
                                                    THEMES[G.theme].water[0], THEMES[G.theme].water[1], 255, false);
                play(S_SPLASH, 0.5f);
                kill_player("water");
            }
        }
    if (P.y > ROWS * TS + 24) kill_player("fell");

    /* scarf: chain of points trailing from the neck */
    float bob = (P.ground && fabsf(P.vx) < 10) ? ((int)(G.time * 2) % 2) : 0;
    P.sc[0][0] = P.x + P.w / 2 + (P.left ? 1.5f : -1.5f);
    P.sc[0][1] = P.y + 5 + bob;
    for (int i = 1; i < SCARF_N; i++) {
        float tx = P.sc[i - 1][0] + (P.left ? 2.0f : -2.0f) - P.vx * 0.006f;
        float ty = P.sc[i - 1][1] + 0.5f + sinf(G.time * 12 - i * 0.9f) * 0.5f * (0.4f + fabsf(P.vx) / 170) - P.vy * 0.004f;
        P.sc[i][0] = lerpf(P.sc[i][0], tx, fminf(1, dt * 28));
        P.sc[i][1] = lerpf(P.sc[i][1], ty, fminf(1, dt * 28));
        float dx = P.sc[i][0] - P.sc[i - 1][0], dy = P.sc[i][1] - P.sc[i - 1][1], l = sqrtf(dx * dx + dy * dy);
        if (l > 2.4f) { P.sc[i][0] = P.sc[i - 1][0] + dx / l * 2.4f; P.sc[i][1] = P.sc[i - 1][1] + dy / l * 2.4f; }
    }
    (void)prevB;
}

static bool hostile(const Ent *e) { return (e->type == E_BEETLE || e->type == E_HEDGE || e->type == E_BEE) && e->state == 0; }
static bool hedge_asleep(const Ent *e) { return e->type == E_HEDGE && fmodf(e->t, 5.0f) > 3.2f; }

static void update_ents(float dt) {
    for (int i = 0; i < nents; i++) {
        Ent *e = &ents[i];
        if (!e->alive) continue;
        e->t += dt;
        if (!e->active) {
            if (e->x < G.camX + LW + 48) e->active = true;
            else if (e->type == E_BEETLE || e->type == E_HEDGE || e->type == E_BEE) continue;
        }
        switch (e->type) {
        case E_BEETLE: case E_HEDGE: case E_BEE:
            if (e->state == 2) {
                e->vy += 900 * dt; e->x += e->vx * dt; e->y += e->vy * dt;
                if (e->y > ROWS * TS + 40) e->alive = false;
                break;
            }
            if (e->state == 1) {
                e->aux -= dt;
                if (e->aux <= 0) { e->alive = false; burst(e->x + e->w / 2, e->y + e->h / 2, 8, 50, 0.4f, 2, 0, 230, 220, 255, false); }
                break;
            }
            if (e->type == E_BEE) {
                float ph = e->t * 1.1f;
                e->x = e->x0 + sinf(ph) * 44;
                e->y = e->y0 + sinf(e->t * 3.3f) * 7;
                e->dir = cosf(ph) > 0 ? 1 : -1;
                break;
            }
            {
                bool sleep = hedge_asleep(e);
                e->vx = sleep ? 0 : e->dir * (e->type == E_BEETLE ? 28.0f : 20.0f);
                e->vy = fminf(e->vy + 900 * dt, 400);
                if (move_x(&e->x, e->y, e->w, e->h, e->vx * dt)) e->dir = -e->dir;
                int hr, hc;
                if (move_y(e->x, &e->y, e->w, e->h, e->vy * dt, &hr, &hc) == 1) {
                    e->vy = 0;
                    int ca = (int)floorf((e->dir > 0 ? e->x + e->w + 1 : e->x - 1) / TS), rb = (int)floorf((e->y + e->h + 1) / TS);
                    int rbody = (int)floorf((e->y + e->h - 1) / TS);
                    if ((!solid(rb, ca) && tile(rb, ca) != '=') || tile(rbody, ca) == '^') e->dir = -e->dir;
                }
                if (sleep && frand() < dt * 1.2f) pop(e->x + 12, e->y - 4, "Z");
                if (e->y > ROWS * TS) e->alive = false;
            }
            break;
        case E_POPSEED:
            e->vy += 900 * dt; e->y += e->vy * dt;
            if (e->t > 0.45f) { e->alive = false; burst(e->x + 5, e->y + 5, 6, 40, 0.35f, 1.5f, 0, 255, 230, 120, true); }
            break;
        case E_LEAF:
            if (e->state == 0) { e->y -= 34 * dt; if (e->y <= e->y0 - 16) { e->state = 1; e->x0 = e->x; } }
            else {
                e->x = e->x0 + sinf(e->t * 2.2f) * 14;
                int rb = (int)floorf((e->y + e->h + 1) / TS), cc = (int)floorf((e->x + e->w / 2) / TS);
                if (!solid(rb, cc)) e->y += 22 * dt;
                if (e->y > ROWS * TS) e->alive = false;
            }
            break;
        case E_SPRING: e->aux = fmaxf(0, e->aux - dt); break;
        default: break;
        }
    }
}

static void level_clear(void) {
    G.st = ST_CLEAR; G.stateT = 0;
    G.bonus = (int)fmaxf(0, 240 - G.lvT) * 10 + G.lives * 0;
    play(S_CLEAR, 0.55f);
    set_music(G.theme, 0.0f);
}

static void interact(float prevB, Input in) {
    for (int i = 0; i < nents; i++) {
        Ent *e = &ents[i];
        if (!e->alive || P.dead) continue;
        bool ov = overlap(P.x, P.y, P.w, P.h, e->x, e->y, e->w, e->h);
        switch (e->type) {
        case E_SEED:
            if (ov) { e->alive = false; collect_seed(e->x + 5, e->y + 5); }
            break;
        case E_LEAF:
            if (ov) {
                e->alive = false;
                P.leafed = true;
                add_score(1000, e->x + 7, e->y);
                play(S_POWER, 0.5f);
                burst(e->x + 7, e->y + 5, 16, 80, 0.6f, 2, 0, 150, 240, 110, true);
            }
            break;
        case E_BEETLE: case E_HEDGE: case E_BEE: {
            if (e->state != 0) break;
            float ex = e->x + 1, ey = e->y + 1, ew = e->w - 2, eh = e->h - 1;
            if (!overlap(P.x, P.y, P.w, P.h, ex, ey, ew, eh)) break;
            bool fromAbove = P.vy > 0 && prevB <= e->y + 7;
            bool asleep = hedge_asleep(e);
            if (fromAbove && (e->type != E_HEDGE || asleep)) {
                if (e->type == E_BEETLE) { e->state = 1; e->aux = 0.5f; }
                else knock(e);
                G.combo++;
                int v = 100 << (G.combo - 1 < 4 ? G.combo - 1 : 4);
                add_score(v, e->x + e->w / 2, e->y - 4);
                play(S_STOMP, 0.55f);
                P.vy = in.jumpHeld ? -400 : -260;
                P.sx = 1.25f; P.sy = 0.8f;
                burst(e->x + e->w / 2, e->y + 2, 8, 70, 0.4f, 2, 0, 255, 255, 200, true);
                G.shake = fmaxf(G.shake, 1.5f);
            } else if (fromAbove) { hurt_player("spiky top"); if (!P.dead) P.vy = -300; }
            else hurt_player(e->type == E_BEE ? "bee" : e->type == E_HEDGE ? "hedgehog" : "beetle");
            break;
        }
        case E_SPRING:
            if (P.vy > 0 && P.x + P.w > e->x + 2 && P.x < e->x + e->w - 2 && prevB <= e->y + 9 && P.y + P.h >= e->y + 5) {
                P.y = e->y + 5 - P.h; P.vy = -600; P.noCut = 0.5f; P.ground = false; P.plat = -1;
                e->aux = 0.25f; P.sx = 0.7f; P.sy = 1.35f;
                play(S_SPRING, 0.5f);
                burst(e->x + 8, e->y + 4, 8, 60, 0.4f, 2, 0, 255, 160, 170, true);
            }
            break;
        case E_CHECK:
            if (!e->state && P.x > e->x) {
                e->state = 1;
                G.checkCol = (int)(e->x / TS);
                play(S_CHECK, 0.5f);
                burst(e->x + 6, e->y + 5, 20, 90, 0.8f, 2, -20, 255, 210, 120, true);
                pop(e->x + 6, e->y - 6, "CHECKPOINT");
            }
            break;
        case E_GOAL:
            if (P.x + P.w > e->x + 4 && P.y + P.h > e->y) level_clear();
            break;
        default: break;
        }
    }
}

static void update_camera(float dt) {
    float look = (P.left ? -36.0f : 36.0f) * fminf(1, fabsf(P.vx) / 100.0f);
    float target = P.x + P.w / 2 - LW * 0.42f + look;
    G.prevCam = G.camX;
    G.camX = lerpf(G.camX, target, fminf(1, dt * 4.5f));
    G.camX = clampf(G.camX, 0, (float)(cols * TS - LW));
}

static void update_amb(float dt) {
    float dc = G.camX - G.prevCam;
    for (int i = 0; i < MAX_AMB; i++) {
        Amb *a = &amb[i];
        a->ph += dt;
        switch (G.theme) {
        case 0: a->x += (sinf(a->ph * 0.7f) * 8 + 4) * dt * a->z; a->y += (sinf(a->ph * 1.3f) * 6 - 3) * dt; break;
        case 1: a->x += (-18 + sinf(a->ph * 2) * 22) * dt * a->z; a->y += (20 + a->z * 14) * dt; break;
        case 2: a->x += sinf(a->ph * 0.5f) * 6 * dt; a->y -= (4 + a->z * 3) * dt; break;
        default: a->x += (sinf(a->ph * 0.9f) * 10 - 6) * dt * a->z; a->y += (14 + a->z * 22) * dt; break;
        }
        a->x -= dc * a->z;
        if (a->x < -10) a->x += LW + 20;
        if (a->x > LW + 10) a->x -= LW + 20;
        if (a->y > LH + 6) { a->y = -6; a->x = frange(0, LW); }
        if (a->y < -6) { a->y = LH + 6; a->x = frange(0, LW); }
    }
    /* drips in the cave */
    if (G.theme == 2 && frand() < dt * 3) {
        int c = (int)((G.camX + frange(0, LW)) / TS);
        if (c >= 0 && c < cols && T[3][c] == '.' && T[2][c] == 'X') part((float)(c * TS + 8), 48, 0, 0, 1.2f, 1, 500, 120, 200, 230, false);
    }
}

static void update_play(float dt, Input in) {
    G.lvT += dt;
    update_plats(dt);
    float prevB = P.y + P.h;
    update_player(dt, in);
    if (P.dead) return;
    update_ents(dt);
    interact(prevB, in);
    for (int i = 0; i < 8; i++) if (bumps[i].t > 0) bumps[i].t -= dt;
}

/* ---------------------------------------------------------------- autopilot (title demo + selftest) */
static float ap_hold, ap_stuck, ap_lastx, ap_back;
static int ap_elev = -1;
static float ap_fwd;
static Input autopilot(float dt) {
    Input in = {0};
    in.mx = 1; in.run = true;
    float feet = P.y + P.h, cx = P.x + P.w / 2;
    int rf = (int)floorf((feet - 1) / TS), rb = rf + 1;
    float look = P.x + P.w + 4 + fabsf(P.vx) * 0.1f;
    int ca = (int)floorf(look / TS);
    bool wall = solid(rf, ca) || solid(rf - 1, ca);
    bool tall = solid(rf, ca) && solid(rf - 3, ca) && solid(rf - 4, ca);
    bool hazard = tile(rf, ca) == '^' || tile(rf, ca + 1) == '^';
    bool pit = true;
    for (int r = rb; r < ROWS; r++) if (solid(r, ca) || tile(r, ca) == '=') { pit = false; break; }
    int pwid = 0;
    if (pit)
        for (int c = ca; c < cols && c < ca + 12; c++) {
            bool sup = false;
            for (int r = rf - 3; r < ROWS; r++) if (solid(r, c) || tile(r, c) == '=') { sup = true; break; }
            if (sup) break;
            pwid++;
        }
    bool enemy = false;
    for (int i = 0; i < nents; i++) {
        Ent *e = &ents[i];
        if (!e->alive || !hostile(e) || e->type == E_HEDGE) continue;
        float dx = e->x - (P.x + P.w);
        if (dx > -4 && dx < 30 + fabsf(P.vx) * 0.15f && e->y + e->h > P.y - 24 && e->y < feet + 4) enemy = true;
    }
    bool want = wall || hazard || enemy || (pit && pwid <= 7);
    float hop = 0;
    for (int i = 0; i < nents; i++) { /* wait for hedgehogs to doze off, then stomp them */
        Ent *e = &ents[i];
        if (!e->alive || e->type != E_HEDGE || e->state) continue;
        float dx = e->x - (P.x + P.w);
        if (dx < -10 || dx > 72 || fabsf(e->y + e->h - feet) > 12) continue;
        if (hedge_asleep(e) && fmodf(e->t, 5.0f) < 4.4f) { if (dx < 22 && dx > 4) { want = true; hop = 0.07f; } else if (dx <= 4) in.mx = -1; }
        else { in.mx = dx < 44 ? -1.0f : 0.0f; want = false; }
    }
    if (pit && pwid > 7 && P.plat < 0) {
        in.mx = 0;
        for (int i = 0; i < nplats; i++) {
            Plat *pl = &plats[i];
            if (!pl->vert && pl->x > cx + 2 && pl->x < cx + 50 && pl->x < pl->x0 + 4 && pl->y >= feet - 44 && pl->y <= feet + 24) { want = true; in.mx = 1; hop = 0.2f; }
        }
        if (!want) want = false;
    }
    if (tall && P.plat < 0)
        for (int i = 0; i < nplats; i++)
            if (plats[i].vert && fabsf(plats[i].x + 24 - cx) < 160) ap_elev = i;
    if (ap_elev >= nplats || (ap_elev >= 0 && (cx > plats[ap_elev].x + 120 || cx < plats[ap_elev].x - 200))) ap_elev = -1;
    if (ap_elev >= 0 && P.plat < 0) { /* walk under the lift and hop on when it is low */
        Plat *pl = &plats[ap_elev];
        float tx = pl->x + 20;
        in.mx = cx < tx - 3 ? 1.0f : cx > tx + 3 ? -1.0f : 0.0f;
        want = P.ground && in.mx == 0 && pl->y > feet - 30;
    }
    if (P.plat >= 0) {
        Plat *pl = &plats[P.plat];
        in.mx = 0;
        want = false;
        if (pl->vert) {
            if (pl->y <= pl->y1 + 3) { in.mx = 1; want = true; ap_elev = -1; ap_fwd = 0.8f; }
        } else {
            int cr = (int)floorf((P.x + P.w + 22) / TS);
            bool land = false;
            for (int r = rb - 1; r <= rb + 3; r++) if (solid(r, cr)) land = true;
            if (land) { in.mx = 1; want = true; }
            else if (P.x < pl->x + 6) in.mx = 1;
            else if (P.x + P.w > pl->x + pl->w - 6) in.mx = -1;
        }
    }
    if (P.ground && want) { in.jump = true; ap_hold = hop > 0 ? hop : pit ? 0.45f : wall ? 0.4f : 0.25f; }
    if (!P.ground && P.vy > -220) { /* steer toward something to land on */
        int cb = (int)floorf(cx / TS), fr2 = (int)floorf(feet / TS);
        bool sup = false;
        for (int r = fr2; r < ROWS; r++) if (solid(r, cb) || tile(r, cb) == '=') { sup = true; break; }
        for (int i = 0; i < nplats; i++) if (plats[i].y >= feet - 2 && cx > plats[i].x + 4 && cx < plats[i].x + plats[i].w - 4) sup = true;
        float ahead = cx + P.vx * 0.2f;
        int cah = (int)floorf(ahead / TS);
        bool supAhead = false;
        for (int r = fr2; r < ROWS; r++) if (solid(r, cah) || tile(r, cah) == '=') { supAhead = r < ROWS - 3 || solid(r, cah); break; }
        for (int i = 0; i < nplats; i++) if (plats[i].y >= feet - 2 && ahead > plats[i].x && ahead < plats[i].x + plats[i].w) supAhead = true;
        if (sup && !supAhead && ap_elev < 0 && P.vy > 0) in.mx = P.vx > 30 ? -1.0f : P.vx < -30 ? 1.0f : 0.0f;
        if (!sup) {
            float best = 1e9f, tx = cx;
            for (int dc = -5; dc <= 7; dc++) {
                int c = cb + dc;
                for (int r = fr2; r < ROWS; r++)
                    if (solid(r, c) || tile(r, c) == '=') {
                        if (r < ROWS - 3 || solid(r, c)) {
                            float d = dc < 0 ? -dc * 3.0f : dc * 0.7f;
                            if (d < best) { best = d; tx = (float)(c * TS + 8); }
                        }
                        break;
                    }
            }
            for (int i = 0; i < nplats; i++)
                if (plats[i].y >= feet - 2 && fabsf(plats[i].x + 24 - cx) < 90) { tx = plats[i].x + 24 + plats[i].dx * 20; best = -1; }
            if (best < 1e8f) in.mx = tx > cx + 3 ? 1.0f : tx < cx - 3 ? -1.0f : 0.0f;
        }
    }
    if (!P.ground && P.leafed && P.vy > 0) {
        int cbelow = (int)floorf(cx / TS);
        bool overPit = true;
        for (int r = rb; r < ROWS; r++) if (solid(r, cbelow)) { overPit = false; break; }
        if (overPit) ap_hold = 0.1f;
    }
    in.jumpHeld = ap_hold > 0;
    ap_hold -= dt;
    if (fabsf(P.x - ap_lastx) < 0.5f && in.mx != 0) ap_stuck += dt;
    else { ap_stuck = 0; ap_lastx = P.x; }
    if (ap_stuck > 1.2f) { in.jump = true; ap_hold = 0.45f; ap_stuck = 0; ap_back = frand() < 0.5f ? 0.35f : 0; }
    if (ap_back > 0) { ap_back -= dt; in.mx = -1; }
    if (ap_fwd > 0) { ap_fwd -= dt; in.mx = 1; ap_elev = -1; }
    return in;
}

/* ---------------------------------------------------------------- drawing */
static void draw_sky(float t) {
    const Theme *th = &THEMES[G.theme];
    static const float ys[4] = {0, 90, 185, LH};
    for (int i = 0; i < 3; i++) {
        const Uint8 *a = th->sky[i], *b = th->sky[i + 1];
        quad(vtx(0, ys[i], a[0], a[1], a[2], 255), vtx(LW, ys[i], a[0], a[1], a[2], 255),
             vtx(LW, ys[i + 1], b[0], b[1], b[2], 255), vtx(0, ys[i + 1], b[0], b[1], b[2], 255));
    }
    float cam = G.camX;
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_ADD);
    if (G.theme == 0) {
        float sx = 380 - cam * 0.01f, sy = 48;
        glow(sx, sy, 90, 255, 240, 200, 0.45f);
        glow(sx, sy, 30, 255, 250, 230, 0.9f);
        disc(sx, sy, 11, 255, 252, 236, 255);
    } else if (G.theme == 1) {
        float sx = 310 - cam * 0.02f, sy = 168;
        for (int i = 0; i < 14; i++) {
            float a = t * 0.04f + i * (2 * PI / 14), a2 = a + 0.07f;
            SDL_Vertex v[3] = {vtx(sx, sy, 255, 200, 140, 46), vtx(sx + cosf(a) * 420, sy + sinf(a) * 420, 255, 160, 100, 0),
                               vtx(sx + cosf(a2) * 420, sy + sinf(a2) * 420, 255, 160, 100, 0)};
            SDL_RenderGeometry(ren, NULL, v, 3, NULL, 0);
        }
        glow(sx, sy, 150, 255, 150, 80, 0.55f);
        glow(sx, sy, 50, 255, 220, 150, 0.8f);
        disc(sx, sy, 26, 255, 226, 160, 255);
    } else if (G.theme == 3) {
        for (int i = 0; i < 140; i++) {
            float x = fmodf(h01(i, 1) * 960 - cam * 0.02f + 960, 960) * 0.5f, y = h01(i, 2) * 170;
            float tw = 0.5f + 0.5f * sinf(t * (1 + h01(i, 3) * 3) + i);
            Uint8 a = (Uint8)(80 + 175 * tw * h01(i, 4));
            fill(x, y, 1, 1, 255, 255, 255, a);
            if (h01(i, 5) < 0.08f) glow(x, y, 5, 200, 220, 255, tw * 0.6f);
        }
        for (int band = 0; band < 2; band++) { /* aurora ribbons */
            SDL_Vertex v[61 * 3];
            int n = 0;
            for (int i = 0; i <= 60; i++) {
                float x = i * 8.0f, wx = x + cam * 0.04f;
                float y = 52 + band * 26 + sinf(wx * 0.013f + t * 0.35f + band) * 18 + sinf(wx * 0.031f - t * 0.6f) * 7;
                float k = 0.6f + 0.4f * sinf(wx * 0.05f + t * 1.3f + band * 2);
                Uint8 a = (Uint8)(70 * k);
                v[n++] = vtx(x, y - 46, 60, 255, 170, 0);
                v[n++] = vtx(x, y, band ? 120 : 70, 255, band ? 220 : 160, a);
                v[n++] = vtx(x, y + 12, 160, 80, 255, 0);
            }
            for (int i = 0; i < 60; i++) {
                SDL_Vertex *a = &v[i * 3], *b = &v[i * 3 + 3];
                quad(a[0], b[0], b[1], a[1]);
                quad(a[1], b[1], b[2], a[2]);
            }
        }
        float mx = 110 - cam * 0.015f, my = 46;
        glow(mx, my, 60, 160, 180, 255, 0.5f);
        SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
        disc(mx, my, 12, 236, 240, 255, 255);
        disc(mx - 3, my - 2, 3, 200, 206, 228, 255);
        disc(mx + 4, my + 3, 2, 204, 210, 230, 255);
        disc(mx + 1, my - 6, 1.5f, 206, 212, 232, 255);
    } else if (G.theme == 2) {
        for (int i = 0; i < 5; i++) { /* shafts of light from cracks */
            float x = fmodf(i * 211.0f - cam * 0.5f + 2000, 700) - 110;
            float k = 0.6f + 0.4f * sinf(t * 0.7f + i * 1.7f);
            quad(vtx(x, 0, 120, 150, 255, (Uint8)(30 * k)), vtx(x + 18, 0, 120, 150, 255, (Uint8)(30 * k)),
                 vtx(x + 70, LH, 120, 150, 255, 0), vtx(x + 30, LH, 120, 150, 255, 0));
        }
    }
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    if (G.theme <= 1) { /* drifting clouds */
        for (int i = 0; i < 9; i++) {
            float w = 720;
            float x = fmodf(h01(i, 11) * w - cam * 0.06f + t * (3 + h01(i, 12) * 4) + w * 4, w) - 120;
            float y = 20 + h01(i, 13) * (G.theme == 0 ? 80 : 60);
            Uint8 r = G.theme == 0 ? 255 : 255, g = G.theme == 0 ? 255 : 186, b = G.theme == 0 ? 255 : 170;
            for (int k = 0; k < 5; k++) {
                float rr = 8 + h01(i, 20 + k) * 9;
                disc(x + k * 11 - 22, y - h01(i, 30 + k) * 7 + (k == 0 || k == 4 ? 4 : 0), rr, r, g, b, 200);
            }
            disc(x - 8, y + 5, 7, r, g, b, 200);
            disc(x + 10, y + 5, 7, r, g, b, 200);
        }
    }
    if (G.theme == 1) { /* distant birds */
        for (int i = 0; i < 6; i++) {
            float x = fmodf(t * (14 + i * 2) + i * 130 - cam * 0.1f + 3000, 620) - 60, y = 60 + sinf(t * 0.5f + i) * 10 + i * 9;
            float f = sinf(t * 8 + i) * 2;
            SDL_SetRenderDrawColor(ren, 60, 30, 60, 220);
            SDL_RenderDrawLineF(ren, x - 3, y - f, x, y);
            SDL_RenderDrawLineF(ren, x, y, x + 3, y - f);
        }
    }
}

static void draw_layers(void) {
    static const float fac[3] = {0.12f, 0.3f, 0.55f};
    for (int i = 0; i < 3; i++) {
        if (!t_layer[i]) continue;
        float off = fmodf(G.camX * fac[i], 960);
        for (int k = 0; k < 2; k++) {
            SDL_FRect d = {-off + k * 960, (float)(i * 6), 960, LH};
            SDL_RenderCopyF(ren, t_layer[i], NULL, &d);
        }
        if (i == 1 && G.theme == 2) { /* far crystal glints */
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_ADD);
            for (int k = 0; k < 14; k++) {
                float x = fmodf(h01(k, 61) * 960 - G.camX * 0.3f + 960 * 3, 960) - 20, y = 120 + h01(k, 62) * 120;
                glow(x, y, 10, 90, 200, 255, 0.25f + 0.15f * sinf(G.time * 2 + k));
            }
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
        }
    }
}

static void draw_amb(bool front) {
    for (int i = 0; i < MAX_AMB; i++) {
        Amb *a = &amb[i];
        if ((a->z >= 1.0f) != front) continue;
        switch (G.theme) {
        case 0:
            if (i % 9 == 0) { /* butterfly */
                float f = fabsf(sinf(a->ph * 14)) * 2 + 0.5f;
                Uint8 r = i % 2 ? 255 : 250, g = i % 2 ? 200 : 140, b = i % 2 ? 80 : 220;
                fill(a->x - f, a->y + sinf(a->ph * 3) * 3, f, 2, r, g, b, 230);
                fill(a->x + 1, a->y + sinf(a->ph * 3) * 3, f, 2, r, g, b, 230);
            } else glow(a->x, a->y, 2.5f * a->z, 255, 250, 200, 0.35f);
            break;
        case 1: {
            float s = 1.5f * a->z, wob = sinf(a->ph * 5);
            Uint8 r = 220 + (i % 3) * 15, g = 90 + (i % 4) * 30;
            fill(a->x, a->y, s * (1.5f + wob), s, r, g, 40, 220);
            break;
        }
        case 2:
            glow(a->x, a->y, 3 * a->z, i % 3 ? 90 : 200, i % 3 ? 220 : 120, 255, 0.45f + 0.3f * sinf(a->ph * 2));
            break;
        default: {
            float s = a->z > 1.2f ? 2 : 1;
            fill(a->x, a->y, s, s, 240, 246, 255, (Uint8)(150 + 60 * fminf(1, a->z - 0.4f)));
            break;
        }
        }
    }
}

static void draw_water(float t) {
    const Theme *th = &THEMES[G.theme];
    int c0 = (int)(G.camX / TS), c1 = c0 + LW / TS + 1;
    for (int r = 0; r < ROWS; r++)
        for (int c = c0; c <= c1 && c < cols; c++) {
            if (T[r][c] != '~') continue;
            float X = c * TS - G.camX, Y = (float)(r * TS - CAMY);
            if (tile(r - 1, c) != '~') {
                for (int px = 0; px < TS; px++) {
                    float wx = (float)(c * TS + px);
                    float ys = Y + 4 + sinf(wx * 0.25f + t * 3) * 1.4f + sinf(wx * 0.09f - t * 2) * 1.0f;
                    SDL_SetRenderDrawColor(ren, th->water[0], th->water[1], th->water[2], 210);
                    SDL_RenderDrawLineF(ren, X + px, ys, X + px, Y + TS);
                    fill(X + px, ys, 1, 1, 230, 245, 255, 230);
                }
            } else {
                fill(X, Y, TS, TS, (Uint8)(th->water[0] * 0.75f), (Uint8)(th->water[1] * 0.75f), (Uint8)(th->water[2] * 0.85f), 225);
                for (int k = 0; k < 2; k++) {
                    float sx = X + fmodf(h01(c, r * 3 + k) * 16 + t * 6, 16), sy = Y + 3 + k * 7;
                    fill(sx, sy, 3, 1, 200, 230, 255, (Uint8)(90 + 60 * sinf(t * 3 + c + k)));
                }
            }
        }
}

static void draw_tiles_dynamic(void) {
    int c0 = (int)(G.camX / TS), c1 = c0 + LW / TS + 1;
    for (int r = 0; r < ROWS; r++)
        for (int c = c0; c <= c1 && c < cols; c++) {
            char t = T[r][c];
            if (t != 'B' && t != '?' && t != 'L' && t != 'U') continue;
            float off = 0;
            for (int i = 0; i < 8; i++)
                if (bumps[i].t > 0 && bumps[i].r == r && bumps[i].c == c) off = -sinf((0.2f - bumps[i].t) / 0.2f * PI) * 5;
            SDL_FRect d = {c * TS - G.camX, r * TS - CAMY + off, TS, TS};
            SDL_RenderCopyF(ren, t == 'B' ? t_brick : t == 'U' ? t_used : t_crate, NULL, &d);
            if (t == '?' || t == 'L') {
                float k = fmaxf(0, sinf(G.time * 3 + c * 0.7f));
                if (k > 0.9f) fill(d.x + 3 + (k - 0.9f) * 80, d.y + 2, 2, 12, 255, 255, 230, 120);
            }
        }
}

static void draw_deco(void) {
    const Theme *th = &THEMES[G.theme];
    int c0 = (int)(G.camX / TS) - 1, c1 = c0 + LW / TS + 2;
    float pcx = P.x + P.w / 2, pfeet = P.y + P.h;
    for (int r = 0; r < ROWS; r++)
        for (int c = c0 < 0 ? 0 : c0; c <= c1 && c < cols; c++) {
            unsigned char d = deco[r][c];
            if (!d) continue;
            float X = c * TS - G.camX, base = (float)((r + 1) * TS - CAMY);
            Uint32 h = hash2(c, r);
            switch (d) {
            case D_TUFT: case D_FLOWER: {
                int blades = d == D_TUFT ? 4 : 2;
                for (int i = 0; i < blades; i++) {
                    float bx = X + 2 + (float)((h >> (i * 4)) % 13), hh = 3 + (float)((h >> (i * 3 + 2)) % 4);
                    float wx = c * TS + bx - X;
                    float sway = sinf(G.time * 2.2f + wx * 0.3f) * 1.2f * hh / 6;
                    if (fabsf(pcx - wx) < 9 && fabsf(pfeet - (r + 1) * TS) < 6) sway += (wx > pcx ? 3.0f : -3.0f);
                    const Uint8 *gc = i % 2 ? th->grass2 : th->grass;
                    SDL_SetRenderDrawColor(ren, gc[0], gc[1], gc[2], 255);
                    SDL_RenderDrawLineF(ren, bx, base, bx + sway, base - hh);
                    if (d == D_FLOWER && i == 0) {
                        static const Uint8 FC[4][3] = {{255, 110, 140}, {255, 230, 90}, {170, 140, 255}, {255, 255, 255}};
                        const Uint8 *f = FC[h % 4];
                        float fx = bx + sway, fy = base - hh - 2;
                        fill(fx - 1, fy, 3, 1, f[0], f[1], f[2], 255);
                        fill(fx, fy - 1, 1, 3, f[0], f[1], f[2], 255);
                        fill(fx, fy, 1, 1, 255, 220, 80, 255);
                    }
                }
                break;
            }
            case D_ROCK:
                fill(X + 4 + (float)(h % 6), base - 3, 5, 3, (Uint8)(th->stone[0] * 0.8f), (Uint8)(th->stone[1] * 0.8f), (Uint8)(th->stone[2] * 0.8f), 255);
                fill(X + 5 + (float)(h % 6), base - 4, 3, 1, th->stone[0], th->stone[1], th->stone[2], 255);
                break;
            case D_MUSH: {
                float mx = X + 4 + (float)(h % 8), k = 0.7f + 0.3f * sinf(G.time * 2 + c);
                fill(mx + 1, base - 3, 2, 3, 220, 220, 230, 255);
                fill(mx - 1, base - 5, 6, 2, (Uint8)(120 * k), (Uint8)(240 * k), (Uint8)(255 * k), 255);
                fill(mx, base - 6, 4, 1, (Uint8)(160 * k), 255, 255, 255);
                break;
            }
            case D_CRYSTAL: {
                float mx = X + 4 + (float)(h % 8), hh = 6 + (float)(h % 5);
                Uint8 r1 = G.theme == 2 ? (h & 1 ? 120 : 200) : 180, g1 = G.theme == 2 ? (h & 1 ? 230 : 130) : 230, b1 = 255;
                SDL_Vertex v[3] = {vtx(mx, base, r1, g1, b1, 255), vtx(mx + 4, base, (Uint8)(r1 * 0.6f), (Uint8)(g1 * 0.6f), (Uint8)(b1 * 0.8f), 255),
                                   vtx(mx + 1.5f, base - hh, 255, 255, 255, 255)};
                SDL_RenderGeometry(ren, NULL, v, 3, NULL, 0);
                SDL_Vertex v2[3] = {vtx(mx + 3, base, r1, g1, b1, 255), vtx(mx + 6, base, (Uint8)(r1 * 0.6f), (Uint8)(g1 * 0.6f), b1, 255),
                                    vtx(mx + 5.5f, base - hh * 0.55f, 230, 250, 255, 255)};
                SDL_RenderGeometry(ren, NULL, v2, 3, NULL, 0);
                break;
            }
            case D_SNOW:
                fill(X + 3 + (float)(h % 7), base - 2, 6, 2, 236, 244, 255, 255);
                fill(X + 4 + (float)(h % 7), base - 3, 3, 1, 250, 252, 255, 255);
                break;
            default: break;
            }
        }
}

static void draw_sprite(SDL_Texture *t, float x, float y, float w, float h, bool flipx, bool flipy, double ang) {
    SDL_FRect d = {x - G.camX, y - CAMY, w, h};
    if (d.x > LW + 40 || d.x + w < -40) return;
    SDL_RenderCopyExF(ren, t, NULL, &d, ang, NULL, (SDL_RendererFlip)((flipx ? SDL_FLIP_HORIZONTAL : 0) | (flipy ? SDL_FLIP_VERTICAL : 0)));
}

static void draw_ents(void) {
    for (int i = 0; i < nents; i++) {
        Ent *e = &ents[i];
        if (!e->alive) continue;
        switch (e->type) {
        case E_SEED: case E_POPSEED: {
            float sp = fabsf(cosf(G.time * 3 + e->x0 * 0.05f)), w = 10 * fmaxf(0.2f, sp);
            draw_sprite(t_seed, e->x + 5 - w / 2, e->y + (e->type == E_SEED ? sinf(G.time * 2.5f + e->x0) * 1.5f : 0), w, 11, false, false, 0);
            break;
        }
        case E_LEAF: draw_sprite(t_leaf, e->x, e->y, 14, 10, false, false, sinf(e->t * 2.2f) * 20); break;
        case E_BEETLE: {
            int f = e->state == 1 ? 2 : ((int)(e->t * 6) % 2);
            draw_sprite(t_beetle[f], e->x, e->y, 16, 12, e->dir < 0, e->state == 2, 0);
            break;
        }
        case E_HEDGE: {
            bool sl = hedge_asleep(e);
            int f = sl ? 2 + ((int)(e->t * 2) % 2) : ((int)(e->t * 5) % 2);
            draw_sprite(t_hedge[f], e->x - 1, e->y - 2, 18, 14, e->dir < 0, e->state == 2, 0);
            break;
        }
        case E_BEE: draw_sprite(t_bee[(int)(e->t * 18) % 2], e->x - 1, e->y - 1, 14, 12, e->dir < 0, e->state == 2, 0); break;
        case E_SPRING: draw_sprite(t_spring[e->aux > 0 ? 1 : 0], e->x, e->y, 16, 16, false, false, 0); break;
        case E_CHECK: draw_sprite(t_lamp[e->state ? 1 : 0], e->x, e->y, 12, 24, false, false, 0); break;
        case E_GOAL: {
            float bob = sinf(G.time * 2) * 1.5f;
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_ADD);
            float bx = e->x + 8 - G.camX;
            quad(vtx(bx - 4, 0, 120, 230, 255, 0), vtx(bx + 4, 0, 120, 230, 255, 0),
                 vtx(bx + 7, e->y - CAMY + 10, 140, 240, 255, 110), vtx(bx - 7, e->y - CAMY + 10, 140, 240, 255, 110));
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
            draw_sprite(t_goal, e->x, e->y + bob * 0.3f, 16, 48, false, false, 0);
            if (frand() < 0.3f) part(e->x + frange(2, 14), e->y + 40, frange(-5, 5), frange(-50, -20), 1.2f, 1, -10, 150, 240, 255, true);
            break;
        }
        default: break;
        }
    }
    for (int i = 0; i < nplats; i++) {
        SDL_FRect d = {plats[i].x - G.camX, plats[i].y - CAMY, 48, 10};
        SDL_RenderCopyF(ren, t_plat, NULL, &d);
    }
}

static void draw_player(void) {
    if (P.inv > 0 && ((int)(P.inv * 20)) % 2 && !P.dead) return;
    int f;
    if (P.dead) f = FOX_HURT;
    else if (!P.ground) f = P.vy < 0 ? FOX_JUMP : FOX_FALL;
    else if (fabsf(P.vx) > 10) f = FOX_RUN0 + ((int)P.runPh % 4);
    else f = (int)(G.time * 2) % 2;
    /* scarf behind the body */
    for (int i = SCARF_N - 1; i >= 1; i--) {
        float s = i < 3 ? 2.0f : 1.5f;
        Uint8 r = (Uint8)(230 - i * 8), g = (Uint8)(50 + (i % 2) * 20);
        fill(P.sc[i][0] - G.camX - s / 2, P.sc[i][1] - CAMY - s / 2, s, s, r, g, 64, 255);
    }
    float w = 20 * P.sx, h = 20 * P.sy;
    SDL_FRect d = {P.x + P.w / 2 - w / 2 - G.camX, P.y + P.h - h - CAMY, w, h};
    SDL_RenderCopyExF(ren, t_fox[f], NULL, &d, P.dead ? G.stateT * 300 : 0, NULL, P.left ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);
    fill(P.sc[0][0] - G.camX - 1.5f, P.sc[0][1] - CAMY - 1, 3, 2.5f, 236, 60, 70, 255);
    if (P.leafed && !P.dead) {
        if (P.gliding) {
            float sw = sinf(G.time * 6) * 8;
            SDL_FRect L = {P.x + P.w / 2 - 13 - G.camX, P.y - 18 - CAMY, 26, 18};
            SDL_RenderCopyExF(ren, t_leaf, NULL, &L, 180 + sw, NULL, SDL_FLIP_VERTICAL);
            SDL_SetRenderDrawColor(ren, 80, 120, 50, 255);
            SDL_RenderDrawLineF(ren, P.x + P.w / 2 - G.camX, P.y - 4 - CAMY, P.x + P.w / 2 - G.camX, P.y + 4 - CAMY);
        } else {
            SDL_FRect L = {P.x + P.w / 2 + (P.left ? -6.0f : -3.0f) - G.camX, P.y - 9 * P.sy - CAMY + (f == FOX_IDLE1 ? 1 : 0), 10, 7};
            SDL_RenderCopyExF(ren, t_leaf, NULL, &L, P.left ? 20 : -20, NULL, P.left ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);
        }
    }
}

static void draw_parts(bool add) {
    for (int i = 0; i < MAX_PARTS; i++) {
        Part *p = &parts[i];
        if (p->life <= 0 || p->add != add) continue;
        float k = p->life / p->max, x = p->x - G.camX, y = p->y - CAMY;
        if (add) glow(x, y, p->size * 2.5f, p->r, p->g, p->b, k);
        else fill(x - p->size / 2, y - p->size / 2, p->size, p->size, p->r, p->g, p->b, (Uint8)(255 * fminf(1, k * 2)));
    }
}

static void lights_pass(void) {
    const Theme *th = &THEMES[G.theme];
    SDL_SetRenderTarget(ren, t_light);
    SDL_SetRenderDrawColor(ren, th->amb[0], th->amb[1], th->amb[2], 255);
    SDL_RenderClear(ren);
    float px = P.x + P.w / 2 - G.camX, py = P.y + 6 - CAMY;
    glow(px, py, 120, 255, 226, 180, 0.85f);
    glow(px, py, 50, 255, 240, 210, 0.6f);
    int c0 = (int)(G.camX / TS) - 3, c1 = c0 + LW / TS + 6;
    for (int r = 0; r < ROWS; r++)
        for (int c = c0 < 0 ? 0 : c0; c <= c1 && c < cols; c++) {
            if (deco[r][c] == D_MUSH) glow(c * TS + 8 - G.camX, (float)(r * TS + 12 - CAMY), 46, 70, 200, 230, 0.8f + 0.2f * sinf(G.time * 2 + c));
            else if (deco[r][c] == D_CRYSTAL) glow(c * TS + 8 - G.camX, (float)(r * TS + 10 - CAMY), 40, 110, 170, 255, 0.75f);
            else if (T[r][c] == '?' || T[r][c] == 'L') glow(c * TS + 8 - G.camX, (float)(r * TS + 8 - CAMY), 26, 255, 200, 120, 0.4f);
            else if (T[r][c] == '~' && tile(r - 1, c) != '~') glow(c * TS + 8 - G.camX, (float)(r * TS + 6 - CAMY), 22, 60, 140, 200, 0.4f);
        }
    for (int i = 0; i < nents; i++) {
        Ent *e = &ents[i];
        if (!e->alive) continue;
        float ex = e->x + e->w / 2 - G.camX, ey = e->y + e->h / 2 - CAMY;
        if (ex < -100 || ex > LW + 100) continue;
        if (e->type == E_SEED || e->type == E_POPSEED) glow(ex, ey, 22, 255, 210, 120, 0.6f);
        else if (e->type == E_CHECK) glow(ex, e->y + 5 - CAMY, e->state ? 80 : 30, 255, 200, 120, e->state ? 0.9f + 0.1f * sinf(G.time * 9) : 0.4f);
        else if (e->type == E_GOAL) glow(ex, ey, 130, 120, 220, 255, 0.95f);
        else if (e->type == E_LEAF) glow(ex, ey, 30, 150, 255, 120, 0.7f);
        else if (e->type == E_BEE) glow(ex, ey, 18, 255, 220, 120, 0.4f);
    }
    for (int i = 0; i < MAX_PARTS; i++)
        if (parts[i].life > 0 && parts[i].add) glow(parts[i].x - G.camX, parts[i].y - CAMY, 14, parts[i].r, parts[i].g, parts[i].b, parts[i].life / parts[i].max * 0.6f);
    if (G.theme == 2)
        for (int i = 0; i < MAX_AMB; i++) glow(amb[i].x, amb[i].y, 10, 90, 200, 255, 0.35f);
    SDL_SetRenderTarget(ren, t_canvas);
    SDL_RenderCopy(ren, t_light, NULL, NULL);
}

static void draw_emissive(void) {
    /* bloom-ish additive glows on top of the lit scene */
    int c0 = (int)(G.camX / TS) - 2, c1 = c0 + LW / TS + 4;
    for (int r = 0; r < ROWS; r++)
        for (int c = c0 < 0 ? 0 : c0; c <= c1 && c < cols; c++) {
            if (deco[r][c] == D_MUSH) glow(c * TS + 8 - G.camX, (float)(r * TS + 10 - CAMY), 10, 80, 220, 255, 0.55f);
            else if (deco[r][c] == D_CRYSTAL) glow(c * TS + 8 - G.camX, (float)(r * TS + 10 - CAMY), 10, 140, 190, 255, 0.5f);
        }
    for (int i = 0; i < nents; i++) {
        Ent *e = &ents[i];
        if (!e->alive) continue;
        float ex = e->x + e->w / 2 - G.camX, ey = e->y + e->h / 2 - CAMY;
        if (ex < -60 || ex > LW + 60) continue;
        if (e->type == E_SEED) glow(ex, ey, 9, 255, 200, 90, 0.35f + 0.15f * sinf(G.time * 4 + e->x0));
        else if (e->type == E_CHECK && e->state) glow(ex, e->y + 5 - CAMY, 12, 255, 200, 110, 0.7f + 0.2f * sinf(G.time * 11));
        else if (e->type == E_GOAL) { glow(ex, ey - 6, 40, 120, 220, 255, 0.45f); glow(ex, ey - 6, 14, 200, 250, 255, 0.5f); }
    }
    draw_parts(true);
}

static void draw_iris(float k, float cx, float cy) {
    if (k >= 1) return;
    float R = k * 330;
    SDL_SetRenderDrawColor(ren, 6, 4, 12, 255);
    SDL_FRect rs[LH * 2];
    int n = 0;
    for (int y = 0; y < LH; y++) {
        float dy = y + 0.5f - cy;
        if (fabsf(dy) >= R) { rs[n++] = (SDL_FRect){0, (float)y, LW, 1}; continue; }
        float hw = sqrtf(R * R - dy * dy);
        if (cx - hw > 0) rs[n++] = (SDL_FRect){0, (float)y, cx - hw, 1};
        if (cx + hw < LW) rs[n++] = (SDL_FRect){cx + hw, (float)y, LW - (cx + hw), 1};
    }
    SDL_RenderFillRectsF(ren, rs, n);
}

static void draw_world(void) {
    float t = G.time;
    draw_sky(t);
    draw_layers();
    draw_amb(false);
    SDL_FRect src = {G.camX, (float)CAMY, LW, LH}, dst = {0, 0, LW, LH};
    if (t_tiles) {
        SDL_Rect s = {(int)floorf(src.x), (int)src.y, LW, LH};
        SDL_FRect d = {floorf(src.x) - G.camX, 0, LW, LH};
        SDL_RenderCopyF(ren, t_tiles, &s, &d);
    }
    (void)dst;
    draw_water(t);
    draw_tiles_dynamic();
    draw_deco();
    draw_ents();
    if (G.st != ST_TITLE || G.attract) draw_player();
    draw_parts(false);
    if (THEMES[G.theme].lit) lights_pass();
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_ADD);
    draw_emissive();
    if (G.theme == 2 || G.theme == 0) draw_amb(true);
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    if (G.theme == 1 || G.theme == 3) draw_amb(true);
    if (t_fg) {
        float off = fmodf(G.camX * 1.35f, 960);
        for (int k = 0; k < 2; k++) {
            SDL_FRect d = {-off + k * 960, LH - 22, 960, 40};
            SDL_RenderCopyF(ren, t_fg, NULL, &d);
        }
    }
    SDL_RenderCopy(ren, t_vign, NULL, NULL);
    for (int i = 0; i < MAX_POPS; i++)
        if (pops[i].t > 0) texto(pops[i].s, pops[i].x - G.camX, pops[i].y - CAMY, 1, 255, 255, 210, (Uint8)(255 * fminf(1, pops[i].t * 2)), true);
}

static void fmt6(char *b, size_t n, int v) { snprintf(b, n, "%06d", v); }

static void draw_hud(void) {
    char b[48];
    SDL_FRect head = {8, 6, 13, 11};
    SDL_Rect hs = {6, 0, 14, 12};
    SDL_RenderCopyF(ren, t_fox[0], &hs, &head);
    snprintf(b, sizeof b, "X%d", G.lives < 0 ? 0 : G.lives);
    texto(b, 24, 9, 1, 255, 255, 255, 255, false);
    SDL_FRect sd = {50, 7, 9, 10};
    SDL_RenderCopyF(ren, t_seed, NULL, &sd);
    snprintf(b, sizeof b, "%02d", G.seeds);
    texto(b, 62, 9, 1, 255, 230, 140, 255, false);
    fmt6(b, sizeof b, G.score);
    texto(b, LW / 2.0f, 8, 1.5f, 255, 255, 255, 255, true);
    snprintf(b, sizeof b, "W%d %s", G.level + 1, THEMES[G.theme].name);
    texto(b, LW - 8 - text_w(b, 1), 9, 1, 230, 240, 255, 255, false);
    if (P.leafed) {
        SDL_FRect L = {88, 7, 12, 9};
        SDL_RenderCopyF(ren, t_leaf, NULL, &L);
    }
}

static void dim(Uint8 a) { fill(0, 0, LW, LH, 8, 6, 16, a); }

static void draw_title(void) {
    const char *w1 = "BRAMBLE", *w2 = "BOUND";
    float sc = 6;
    for (int line = 0; line < 2; line++) {
        const char *w = line ? w2 : w1;
        float x = LW / 2.0f - text_w(w, sc) / 2, y = 44 + line * 52;
        for (int i = 0; w[i]; i++) {
            float bob = sinf(G.time * 3 + i * 0.6f + line) * 3;
            char s[2] = {w[i], 0};
            float lx = x + i * 6 * sc;
            text(s, lx + 3, y + bob + 4, sc, 40, 20, 40, 160, false);
            for (int dy = -1; dy <= 1; dy++)
                for (int dx = -1; dx <= 1; dx++) text(s, lx + dx * 2, y + bob + dy * 2, sc, 50, 24, 30, 255, false);
            float k = (float)i / 7.0f;
            Uint8 r = line ? 120 : 255, g = (Uint8)(line ? 220 - k * 40 : 170 - k * 60), b = (Uint8)(line ? 110 : 60 + k * 30);
            text(s, lx, y + bob, sc, r, g, b, 255, false);
        }
    }
    if ((int)(G.time * 2) % 2 == 0) texto("PRESS ENTER TO START", LW / 2.0f, 172, 1.5f, 255, 255, 255, 255, true);
    char b[48];
    snprintf(b, sizeof b, "BEST %06d", G.best);
    texto(b, LW / 2.0f, 194, 1, 255, 230, 140, 255, true);
    texto("ARROWS/WASD MOVE   SPACE/Z JUMP   SHIFT/X RUN", LW / 2.0f, 238, 1, 220, 230, 255, 255, true);
    texto("HOLD JUMP WITH A LEAF TO GLIDE   M MUSIC   F11 FULLSCREEN", LW / 2.0f, 250, 1, 220, 230, 255, 255, true);
}

static void draw_overlay(void) {
    char b[64];
    switch (G.st) {
    case ST_TITLE: draw_title(); break;
    case ST_INTRO: {
        fill(0, 0, LW, LH, 6, 4, 12, 255);
        snprintf(b, sizeof b, "WORLD %d", G.level + 1);
        texto(b, LW / 2.0f, 90, 2, 255, 230, 140, 255, true);
        texto(THEMES[G.level].name, LW / 2.0f, 116, 3, 255, 255, 255, 255, true);
        SDL_FRect d = {LW / 2.0f - 30, 156, 20, 20};
        SDL_RenderCopyF(ren, t_fox[(int)(G.time * 2) % 2], NULL, &d);
        snprintf(b, sizeof b, "X %d", G.lives);
        texto(b, LW / 2.0f + 2, 164, 2, 255, 255, 255, 255, false);
        break;
    }
    case ST_PAUSE:
        dim(150);
        texto("PAUSED", LW / 2.0f, 100, 4, 255, 255, 255, 255, true);
        texto("ESC RESUME    Q QUIT TO TITLE    M MUSIC", LW / 2.0f, 150, 1, 220, 230, 255, 255, true);
        break;
    case ST_CLEAR: {
        float k = fminf(1, G.stateT * 2);
        texto("WORLD CLEAR!", LW / 2.0f, 70 - (1 - k) * 30, 3, 255, 236, 120, (Uint8)(255 * k), true);
        if (G.stateT > 1.0f) {
            snprintf(b, sizeof b, "TIME BONUS %d", G.bonus);
            texto(b, LW / 2.0f, 106, 1.5f, 255, 255, 255, 255, true);
        }
        break;
    }
    case ST_OVER: case ST_WIN: {
        dim((Uint8)fminf(170, G.stateT * 300));
        bool win = G.st == ST_WIN;
        texto(win ? "YOU DID IT!" : "GAME OVER", LW / 2.0f, 70, 4, win ? 255 : 255, win ? 230 : 120, win ? 120 : 120, 255, true);
        if (win) texto("BRAMBLE MADE IT HOME ACROSS ALL FOUR WORLDS", LW / 2.0f, 112, 1, 220, 240, 255, 255, true);
        snprintf(b, sizeof b, "SCORE %06d", G.score);
        texto(b, LW / 2.0f, 134, 2, 255, 255, 255, 255, true);
        snprintf(b, sizeof b, G.newBest ? "NEW BEST!" : "BEST %06d", G.best);
        texto(b, LW / 2.0f, 160, 1.5f, 255, 230, 140, 255, true);
        if (G.stateT > 1.0f && (int)(G.time * 2) % 2 == 0) texto("PRESS ENTER", LW / 2.0f, 200, 1.5f, 255, 255, 255, 255, true);
        break;
    }
    default: break;
    }
}

static void render(int ww, int wh) {
    SDL_SetRenderTarget(ren, t_canvas);
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
    SDL_RenderClear(ren);
    if (G.st != ST_INTRO) draw_world();
    if (G.st == ST_PLAY || G.st == ST_DEAD || G.st == ST_CLEAR || G.st == ST_PAUSE) draw_hud();
    draw_overlay();
    draw_iris(G.iris, P.x + P.w / 2 - G.camX, P.y + P.h / 2 - CAMY);
    SDL_SetRenderTarget(ren, NULL);
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
    SDL_RenderClear(ren);
    float sc = fminf((float)ww / LW, (float)wh / LH);
    if (sc >= 1) sc = floorf(sc) + ((sc - floorf(sc)) > 0.85f ? 1 : 0) > sc ? floorf(sc) : floorf(sc);
    if (sc < 1) sc = fminf((float)ww / LW, (float)wh / LH);
    float sx = G.shake > 0 ? frange(-G.shake, G.shake) : 0, sy = G.shake > 0 ? frange(-G.shake, G.shake) : 0;
    SDL_FRect dst = {floorf((ww - LW * sc) / 2 + sx * sc), floorf((wh - LH * sc) / 2 + sy * sc), LW * sc, LH * sc};
    SDL_RenderCopyF(ren, t_canvas, NULL, &dst);
}

/* ---------------------------------------------------------------- state machine */
static void new_game(void) {
    G.lives = 3; G.score = 0; G.seeds = 0; G.level = 0; G.deaths = 0; G.newBest = false; G.checkCol = -1;
    G.attract = false; sfx_mute = false;
    load_level(0, false);
    G.st = ST_INTRO; G.stateT = 0;
    play(S_START, 0.5f);
}

static void finish_run(bool win) {
    G.st = win ? ST_WIN : ST_OVER; G.stateT = 0;
    if (G.score > G.best) { G.best = G.score; G.newBest = true; save_best(); }
    play(win ? S_CLEAR : S_OVER, 0.55f);
    set_music(-1, 0);
}

static void to_title(void) {
    G.st = ST_TITLE; G.stateT = 0; G.attract = true; sfx_mute = true;
    G.level = 0;
    load_level(0, false);
    G.iris = 1;
    set_music(0, 0.18f);
}

static void step(float dt, Input in) {
    G.time += dt;
    G.stateT += dt;
    G.shake = fmaxf(0, G.shake - dt * 20);
    if (G.st != ST_PAUSE) update_particles(dt);
    switch (G.st) {
    case ST_TITLE: {
        Input ai = autopilot(dt);
        update_play(dt, ai);
        if (P.dead || G.st != ST_TITLE) { G.st = ST_TITLE; load_level(0, false); }
        update_camera(dt);
        update_amb(dt);
        break;
    }
    case ST_INTRO:
        G.iris = 0;
        if (G.stateT > 2.0f) { G.st = ST_PLAY; G.stateT = 0; set_music(G.theme, 0.2f); }
        break;
    case ST_PLAY:
        G.iris = fminf(1, G.stateT * 1.6f);
        update_play(dt, in);
        update_camera(dt);
        update_amb(dt);
        break;
    case ST_DEAD:
        if (G.stateT > 0.45f) { P.vy += 900 * dt; P.y += P.vy * dt; }
        update_amb(dt);
        if (G.stateT > 1.9f) G.iris = fmaxf(0, 1 - (G.stateT - 1.9f) * 1.8f);
        if (G.stateT > 2.6f) {
            G.deaths++;
            G.lives--;
            if (G.lives < 0) { G.iris = 1; finish_run(false); load_level(G.level, true); P.x = -1000; }
            else { load_level(G.level, true); G.st = ST_INTRO; G.stateT = 0; }
        }
        break;
    case ST_CLEAR: {
        if (G.stateT < 1.2f) { Input w = {0}; w.mx = 0.6f; update_player(dt, w); P.vx = fminf(P.vx, 60); }
        else { Input w = {0}; update_player(dt, w); }
        update_ents(dt);
        update_camera(dt);
        update_amb(dt);
        int give = (int)fminf((float)G.bonus, 1500 * dt + 1);
        if (G.stateT > 1.0f && G.bonus > 0) { G.bonus -= give; G.score += give; }
        if (frand() < dt * 4) {
            Uint8 r = (Uint8)(150 + xr() % 106), g = (Uint8)(120 + xr() % 136), b = (Uint8)(150 + xr() % 106);
            float fx = G.camX + frange(80, LW - 80), fy = frange(40, 130) + CAMY;
            burst(fx, fy, 30, 110, 1.2f, 2, 60, r, g, b, true);
        }
        if (G.stateT > 3.6f) G.iris = fmaxf(0, 1 - (G.stateT - 3.6f) * 1.8f);
        if (G.stateT > 4.3f) {
            G.score += G.bonus; G.bonus = 0;
            if (G.level + 1 >= NLEVELS) { G.iris = 1; finish_run(true); }
            else { G.level++; G.checkCol = -1; load_level(G.level, false); G.st = ST_INTRO; G.stateT = 0; }
        }
        break;
    }
    case ST_WIN:
        if (frand() < dt * 6) burst(G.camX + frange(40, LW - 40), frange(30, 150) + CAMY, 30, 120, 1.3f, 2, 60,
                                    (Uint8)(150 + xr() % 106), (Uint8)(120 + xr() % 136), (Uint8)(150 + xr() % 106), true);
        update_amb(dt);
        break;
    case ST_OVER: update_amb(dt); break;
    case ST_PAUSE: break;
    }
}

/* ---------------------------------------------------------------- selftest */
static void shot(const char *dir, const char *name) {
    char path[1200];
    render(LW * 2, LH * 2);
    snprintf(path, sizeof path, "%s/%s", dir, name);
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, LW * 2, LH * 2, 32, SDL_PIXELFORMAT_ARGB8888);
    SDL_RenderReadPixels(ren, NULL, SDL_PIXELFORMAT_ARGB8888, s->pixels, s->pitch);
    SDL_SaveBMP(s, path);
    SDL_FreeSurface(s);
}

static int selftest(const char *dir) {
    const float dt = 1 / 60.0f;
    to_title();
    for (int i = 0; i < 300; i++) step(dt, (Input){0});
    shot(dir, "title.bmp");
    new_game();
    shot(dir, "intro.bmp");
    int cleared = 0;
    for (int lv = 0; lv < NLEVELS; lv++) {
        G.level = lv; G.checkCol = -1;
        load_level(lv, false);
        G.st = ST_PLAY; G.stateT = 0;
        ap_stuck = 0; ap_back = 0;
        int frames = 0, deaths = 0;
        bool took = false, done = false;
        float maxX = 0;
        while (frames < 60 * 240) {
            frames++;
            Input in = autopilot(dt);
            step(dt, in);
            if (P.x > maxX) maxX = P.x;
            if (!took && G.lvT > 7.5f && G.st == ST_PLAY) {
                char n[32];
                snprintf(n, sizeof n, "level%d.bmp", lv + 1);
                shot(dir, n);
                took = true;
            }
            if (G.st == ST_DEAD && G.stateT > 0 && G.stateT <= dt * 1.5f && deaths < 3 && G.prevSt != ST_DEAD)
                printf("  death %d at col %d row %d: %s\n", deaths + 1, (int)(P.x / TS), (int)(P.y / TS), death_cause);
            G.prevSt = G.st;
            if (G.st == ST_DEAD && G.stateT > 2.5f) {
                deaths++;
                G.lives = 3;
                load_level(lv, true);
                G.st = ST_PLAY; G.stateT = 1;
            }
            if (G.st == ST_CLEAR) {
                if (lv == 0 && G.stateT > 1.4f && G.stateT < 1.42f) shot(dir, "clear.bmp");
                if (G.stateT > 2) { done = true; break; }
            }
        }
        if (done) cleared++;
        printf("selftest: level %d %-16s %s in %.1fs, deaths %d, furthest col %d/%d, score %d\n", lv + 1, THEMES[lv].name,
               done ? "CLEARED" : "not cleared", G.lvT, deaths, (int)(maxX / TS), cols, G.score);
    }
    G.camX = 0;
    finish_run(true);
    for (int i = 0; i < 90; i++) step(dt, (Input){0});
    shot(dir, "win.bmp");
    G.st = ST_OVER; G.stateT = 2;
    shot(dir, "over.bmp");
    printf("selftest: %d/%d levels cleared by the autopilot\n", cleared, NLEVELS);
    return 0;
}

/* ---------------------------------------------------------------- main */
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
        if (adev) { build_sounds(); build_tune(0); SDL_PauseAudioDevice(adev, 0); }
    }
    SDL_Window *win = SDL_CreateWindow("Bramble Bound", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, LW * 2, LH * 2,
                                       testdir ? SDL_WINDOW_HIDDEN : (SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI));
    if (!win) { fprintf(stderr, "window: %s\n", SDL_GetError()); return 1; }
    SDL_SetWindowMinimumSize(win, LW, LH);
    ren = SDL_CreateRenderer(win, -1, testdir ? SDL_RENDERER_SOFTWARE | SDL_RENDERER_TARGETTEXTURE
                                              : SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_TARGETTEXTURE);
    if (!ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE | SDL_RENDERER_TARGETTEXTURE);
    if (!ren) { fprintf(stderr, "renderer: %s\n", SDL_GetError()); return 1; }
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
    t_glow = make_tex(128, true);
    t_disc = make_tex(64, false);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    t_canvas = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, LW, LH);
    SDL_SetTextureBlendMode(t_canvas, SDL_BLENDMODE_NONE);
    t_light = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, LW, LH);
    SDL_SetTextureBlendMode(t_light, SDL_BLENDMODE_MOD);
    {
        SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, LW, LH, 32, SDL_PIXELFORMAT_ARGB8888);
        Uint32 *px = s->pixels;
        for (int y = 0; y < LH; y++)
            for (int x = 0; x < LW; x++) {
                float dx = (x - LW / 2.0f) / (LW / 2.0f), dy = (y - LH / 2.0f) / (LH / 2.0f);
                float d = sqrtf(dx * dx * 0.8f + dy * dy), a = clampf((d - 0.7f) / 0.7f, 0, 1);
                px[y * (s->pitch / 4) + x] = (Uint32)(a * a * 150) << 24 | 0x0A0612;
            }
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
        t_vign = SDL_CreateTextureFromSurface(ren, s);
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
        SDL_SetTextureBlendMode(t_vign, SDL_BLENDMODE_BLEND);
        SDL_FreeSurface(s);
    }
    paint_sprites();
    G.bakedTheme = -1;

    if (testdir) {
        no_save = true; sfx_mute = true;
        int r = selftest(testdir);
        SDL_Quit();
        return r;
    }
    load_best();
    to_title();

    SDL_GameController *pad = NULL;
    bool running = true;
    Uint64 prev = SDL_GetPerformanceCounter();
    while (running) {
        Input in = {0};
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
            case SDL_QUIT: running = false; break;
            case SDL_CONTROLLERDEVICEADDED: if (!pad) pad = SDL_GameControllerOpen(e.cdevice.which); break;
            case SDL_CONTROLLERDEVICEREMOVED:
                if (pad && e.cdevice.which == SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pad))) {
                    SDL_GameControllerClose(pad); pad = NULL;
                    if (G.st == ST_PLAY) { G.prevSt = G.st; G.st = ST_PAUSE; }
                }
                break;
            case SDL_WINDOWEVENT:
                if (e.window.event == SDL_WINDOWEVENT_FOCUS_LOST && G.st == ST_PLAY) { G.prevSt = G.st; G.st = ST_PAUSE; }
                break;
            case SDL_KEYDOWN: {
                if (e.key.repeat) break;
                SDL_Keycode k = e.key.keysym.sym;
                bool confirm = k == SDLK_RETURN || k == SDLK_KP_ENTER || k == SDLK_SPACE;
                if (k == SDLK_F11) {
                    bool fs = SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN_DESKTOP;
                    SDL_SetWindowFullscreen(win, fs ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
                } else if (k == SDLK_m) {
                    music_on = !music_on;
                } else if (G.st == ST_TITLE) {
                    if (confirm) new_game();
                    else if (k == SDLK_ESCAPE) running = false;
                } else if (G.st == ST_PLAY) {
                    if (k == SDLK_ESCAPE || k == SDLK_p) { G.prevSt = G.st; G.st = ST_PAUSE; }
                    else if (k == SDLK_SPACE || k == SDLK_z || k == SDLK_w || k == SDLK_UP || k == SDLK_k) in.jump = true;
                } else if (G.st == ST_PAUSE) {
                    if (k == SDLK_ESCAPE || k == SDLK_p) G.st = G.prevSt;
                    else if (k == SDLK_q) to_title();
                } else if ((G.st == ST_OVER || G.st == ST_WIN) && G.stateT > 1.0f) {
                    if (confirm || k == SDLK_ESCAPE) to_title();
                }
                break;
            }
            case SDL_CONTROLLERBUTTONDOWN: {
                int b = e.cbutton.button;
                if (G.st == ST_TITLE && (b == SDL_CONTROLLER_BUTTON_START || b == SDL_CONTROLLER_BUTTON_A)) new_game();
                else if (G.st == ST_PLAY) {
                    if (b == SDL_CONTROLLER_BUTTON_START) { G.prevSt = G.st; G.st = ST_PAUSE; }
                    else if (b == SDL_CONTROLLER_BUTTON_A || b == SDL_CONTROLLER_BUTTON_B) in.jump = true;
                } else if (G.st == ST_PAUSE) {
                    if (b == SDL_CONTROLLER_BUTTON_START) G.st = G.prevSt;
                    else if (b == SDL_CONTROLLER_BUTTON_BACK) to_title();
                } else if ((G.st == ST_OVER || G.st == ST_WIN) && G.stateT > 1.0f && (b == SDL_CONTROLLER_BUTTON_START || b == SDL_CONTROLLER_BUTTON_A)) to_title();
                break;
            }
            }
        }
        const Uint8 *ks = SDL_GetKeyboardState(NULL);
        in.mx = (float)((ks[SDL_SCANCODE_D] || ks[SDL_SCANCODE_RIGHT]) - (ks[SDL_SCANCODE_A] || ks[SDL_SCANCODE_LEFT]));
        in.jumpHeld = ks[SDL_SCANCODE_SPACE] || ks[SDL_SCANCODE_Z] || ks[SDL_SCANCODE_W] || ks[SDL_SCANCODE_UP] || ks[SDL_SCANCODE_K];
        in.run = ks[SDL_SCANCODE_LSHIFT] || ks[SDL_SCANCODE_RSHIFT] || ks[SDL_SCANCODE_X] || ks[SDL_SCANCODE_J];
        if (pad) {
            float ax = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTX) / 32767.0f;
            if (fabsf(ax) > 0.25f) in.mx = ax > 0 ? 1.0f : -1.0f;
            if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) in.mx = 1;
            if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_LEFT)) in.mx = -1;
            in.jumpHeld |= SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_A) || SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_B);
            in.run |= SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_X) || SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_Y) ||
                      SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 8000;
        }
        Uint64 now = SDL_GetPerformanceCounter();
        float dt = (float)(now - prev) / (float)SDL_GetPerformanceFrequency();
        prev = now;
        if (dt > 1 / 30.0f) dt = 1 / 30.0f;
        step(dt, in);
        int ww, wh;
        SDL_GetRendererOutputSize(ren, &ww, &wh);
        render(ww, wh);
        SDL_RenderPresent(ren);
    }
    if (pad) SDL_GameControllerClose(pad);
    if (adev) SDL_CloseAudioDevice(adev);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
