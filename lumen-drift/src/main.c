/*
 * Lumen Drift — keep your light alive in a cavern full of shadows.
 * Native Linux game, C11 + SDL2 only. All graphics, lighting, font and audio are procedural.
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
#define MAX_SHADOWS 64
#define MAX_SPARKS 8
#define MAX_PARTICLES 1024
#define MAX_VOICES 16
#define RATE 44100

/* ---------------------------------------------------------------- math / rng */
typedef struct { float x, y; } V2;
static V2 v2(float x, float y) { V2 r = {x, y}; return r; }
static V2 add(V2 a, V2 b) { return v2(a.x + b.x, a.y + b.y); }
static V2 sub(V2 a, V2 b) { return v2(a.x - b.x, a.y - b.y); }
static V2 mul(V2 a, float s) { return v2(a.x * s, a.y * s); }
static float len(V2 a) { return sqrtf(a.x * a.x + a.y * a.y); }
static V2 norm(V2 a) { float l = len(a); return l > 1e-5f ? mul(a, 1.0f / l) : v2(0, 0); }
static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

static Uint32 rng = 0x9E3779B9u;
static Uint32 xr(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
static float frand(void) { return (float)(xr() >> 8) * (1.0f / 16777216.0f); }
static float frange(float a, float b) { return a + (b - a) * frand(); }

/* ---------------------------------------------------------------- audio */
typedef struct { float *d; int n; } Sound;
enum { S_PICK, S_FLARE, S_HIT, S_DASH, S_KILL, S_OVER, S_START, S_FIZZLE, S_COUNT };
static Sound snd[S_COUNT];
static struct { const float *d; int n, pos; float vol; } voices[MAX_VOICES];
static SDL_AudioDeviceID adev;

static void audio_cb(void *u, Uint8 *stream, int bytes) {
    (void)u;
    float *out = (float *)stream;
    int n = bytes / (int)sizeof(float);
    memset(out, 0, (size_t)bytes);
    for (int v = 0; v < MAX_VOICES; v++) {
        if (!voices[v].d) continue;
        for (int i = 0; i < n && voices[v].pos < voices[v].n; i++)
            out[i] += voices[v].d[voices[v].pos++] * voices[v].vol;
        if (voices[v].pos >= voices[v].n) voices[v].d = NULL;
    }
    for (int i = 0; i < n; i++) out[i] = tanhf(out[i]); /* soft clip */
}

static void play(int id, float vol) {
    if (!adev || !snd[id].d) return;
    SDL_LockAudioDevice(adev);
    int slot = 0, best = -1;
    for (int v = 0; v < MAX_VOICES; v++) {
        if (!voices[v].d) { slot = v; best = -2; break; }
        if (voices[v].pos > best) { best = voices[v].pos; slot = v; }
    }
    voices[slot].d = snd[id].d; voices[slot].n = snd[id].n; voices[slot].pos = 0; voices[slot].vol = vol;
    SDL_UnlockAudioDevice(adev);
}

static float *mk(Sound *s, float secs) {
    s->n = (int)(secs * RATE);
    s->d = calloc((size_t)s->n, sizeof(float));
    return s->d;
}

static float tri(float ph) { float x = fmodf(ph / (2 * PI), 1.0f); return 4.0f * fabsf(x - 0.5f) - 1.0f; }

static void build_sounds(void) {
    float *d, ph;
    /* pickup: rising chime */
    d = mk(&snd[S_PICK], 0.22f); ph = 0;
    for (int i = 0; i < snd[S_PICK].n; i++) {
        float t = (float)i / RATE, f = 700.0f * powf(2.0f, t / 0.11f);
        ph += 2 * PI * f / RATE;
        d[i] = (sinf(ph) * 0.7f + sinf(ph * 1.5f) * 0.3f) * expf(-t * 18) * 0.45f;
    }
    /* flare: bright whoosh sweeping down */
    d = mk(&snd[S_FLARE], 0.5f); ph = 0; float lp = 0;
    for (int i = 0; i < snd[S_FLARE].n; i++) {
        float t = (float)i / RATE, f = 1100.0f * expf(-t * 4.5f) + 120.0f;
        ph += 2 * PI * f / RATE;
        lp += (frange(-1, 1) - lp) * 0.25f;
        d[i] = (sinf(ph) * 0.6f + lp * 0.8f) * expf(-t * 6) * 0.55f;
    }
    /* hit: low growl + crunch */
    d = mk(&snd[S_HIT], 0.4f); ph = 0;
    for (int i = 0; i < snd[S_HIT].n; i++) {
        float t = (float)i / RATE;
        ph += 2 * PI * (95.0f - t * 80) / RATE;
        d[i] = ((sinf(ph) > 0 ? 0.6f : -0.6f) + frange(-0.5f, 0.5f)) * expf(-t * 9) * 0.6f;
    }
    /* dash: filtered noise swell */
    d = mk(&snd[S_DASH], 0.2f); lp = 0;
    for (int i = 0; i < snd[S_DASH].n; i++) {
        float t = (float)i / RATE;
        lp += (frange(-1, 1) - lp) * 0.12f;
        d[i] = lp * sinf(PI * t / 0.2f) * 0.9f;
    }
    /* kill: quick descending blip */
    d = mk(&snd[S_KILL], 0.14f); ph = 0;
    for (int i = 0; i < snd[S_KILL].n; i++) {
        float t = (float)i / RATE;
        ph += 2 * PI * (560.0f * expf(-t * 12)) / RATE;
        d[i] = tri(ph) * expf(-t * 22) * 0.4f;
    }
    /* game over: three falling notes */
    d = mk(&snd[S_OVER], 1.5f); ph = 0;
    const float over_notes[3] = {392.0f, 311.1f, 233.1f};
    for (int i = 0; i < snd[S_OVER].n; i++) {
        float t = (float)i / RATE; int k = (int)(t / 0.45f); if (k > 2) k = 2;
        float lt = t - k * 0.45f;
        ph += 2 * PI * over_notes[k] / RATE;
        d[i] = (tri(ph) * 0.7f + sinf(ph * 0.5f) * 0.3f) * expf(-lt * (k == 2 ? 2.2f : 5.0f)) * 0.45f;
    }
    /* start: rising arpeggio */
    d = mk(&snd[S_START], 0.6f); ph = 0;
    const float start_notes[4] = {261.6f, 329.6f, 392.0f, 523.3f};
    for (int i = 0; i < snd[S_START].n; i++) {
        float t = (float)i / RATE; int k = (int)(t / 0.11f); if (k > 3) k = 3;
        float lt = t - k * 0.11f;
        ph += 2 * PI * start_notes[k] / RATE;
        d[i] = tri(ph) * expf(-lt * (k == 3 ? 4.0f : 12.0f)) * 0.4f;
    }
    /* fizzle: not enough light to flare */
    d = mk(&snd[S_FIZZLE], 0.15f); ph = 0;
    for (int i = 0; i < snd[S_FIZZLE].n; i++) {
        float t = (float)i / RATE;
        ph += 2 * PI * 140.0f / RATE;
        d[i] = (sinf(ph) > 0 ? 0.3f : -0.3f) * expf(-t * 20);
    }
}

/* ---------------------------------------------------------------- pixel font (5x7) */
static const char FONT_CHARS[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ:.-!/?, ";
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
    {14,17,1,2,4,0,4},{0,0,0,0,12,4,8},{0,0,0,0,0,0,0},
};

static SDL_Renderer *ren;

static float text_w(const char *s, float sc) { return (float)strlen(s) * 6 * sc - sc; }

static void text(const char *s, float x, float y, float sc, Uint8 r, Uint8 g, Uint8 b, Uint8 a, bool center) {
    if (center) x -= text_w(s, sc) / 2;
    SDL_SetRenderDrawColor(ren, r, g, b, a);
    for (; *s; s++, x += 6 * sc) {
        char c = *s >= 'a' && *s <= 'z' ? (char)(*s - 32) : *s;
        const char *at = strchr(FONT_CHARS, c);
        if (!at || !c) continue;
        const unsigned char *gl = FONT[at - FONT_CHARS];
        for (int row = 0; row < 7; row++)
            for (int col = 0; col < 5; col++)
                if (gl[row] & (16 >> col)) {
                    SDL_FRect px = {x + col * sc, y + row * sc, sc, sc};
                    SDL_RenderFillRectF(ren, &px);
                }
    }
}

static void text_shadowed(const char *s, float x, float y, float sc, Uint8 r, Uint8 g, Uint8 b, Uint8 a) {
    text(s, x + sc * 0.6f, y + sc * 0.6f, sc, 0, 0, 0, (Uint8)(a * 0.7f), true);
    text(s, x, y, sc, r, g, b, a, true);
}

/* ---------------------------------------------------------------- game state */
typedef enum { ST_TITLE, ST_PLAY, ST_PAUSE, ST_DYING, ST_OVER } State;
enum { LURKER, DARTER };

typedef struct { V2 p, v, dashDir; float r, t, phase, dashT, speed; int kind; bool alive; } Shadow;
typedef struct { V2 p; float t, life; bool alive; } Spark;
typedef struct { V2 p, v; float life, max, size; Uint8 r, g, b; bool emissive, alive; } Particle;
typedef struct { V2 move; bool flare, dash; } Input;

static struct {
    State st;
    V2 p, v;
    float light, invuln, dashCd, flareCd, dashT;
    float time, score, multT, spawnT, sparkT, shake, flareT, flareMax, dyingT, hurtFlash, stateT;
    int mult, best, kills, sparksTaken;
    bool newBest;
    Shadow sh[MAX_SHADOWS];
    Spark sp[MAX_SPARKS];
    Particle pt[MAX_PARTICLES];
} G;

static char best_path[1024];
static bool no_save;

static void load_best(void) {
    char *pref = SDL_GetPrefPath(NULL, "lumen-drift");
    if (!pref) return;
    snprintf(best_path, sizeof best_path, "%sbest", pref);
    SDL_free(pref);
    FILE *f = fopen(best_path, "r");
    if (f) { if (fscanf(f, "%d", &G.best) != 1) G.best = 0; fclose(f); }
}

static void save_best(void) {
    if (!best_path[0] || no_save) return;
    FILE *f = fopen(best_path, "w");
    if (f) { fprintf(f, "%d\n", G.best); fclose(f); }
}

static void burst(V2 p, int n, float spd, float life, Uint8 r, Uint8 g, Uint8 b, float size, bool emissive) {
    for (int i = 0, k = 0; i < MAX_PARTICLES && k < n; i++) {
        if (G.pt[i].alive) continue;
        float a = frange(0, 2 * PI), s = spd * frange(0.25f, 1.0f);
        Particle *q = &G.pt[i];
        q->p = p; q->v = v2(cosf(a) * s, sinf(a) * s);
        q->life = q->max = life * frange(0.6f, 1.2f);
        q->size = size * frange(0.6f, 1.3f);
        q->r = r; q->g = g; q->b = b; q->emissive = emissive; q->alive = true;
        k++;
    }
}

static float light_radius(void) {
    float r = 46.0f + G.light * 2.1f;
    if (G.light < 25 && G.st == ST_PLAY) r *= 0.93f + 0.07f * sinf(G.time * 37.0f) * frand();
    if (G.st == ST_DYING) r = 46.0f * (G.dyingT / 1.4f) * (0.6f + 0.4f * frand());
    return r;
}

static void reset_game(void) {
    int best = G.best;
    memset(&G, 0, sizeof G);
    G.best = best;
    G.p = v2(W / 2.0f, H / 2.0f);
    G.light = 100;
    G.mult = 1;
    G.spawnT = 2.5f;
    G.sparkT = 0.3f;
}

static void spawn_spark(void) {
    for (int i = 0; i < MAX_SPARKS; i++) {
        if (G.sp[i].alive) continue;
        V2 p;
        int tries = 0;
        do { p = v2(frange(50, W - 50), frange(70, H - 50)); } while (len(sub(p, G.p)) < 140 && ++tries < 20);
        G.sp[i].p = p; G.sp[i].t = frange(0, 6); G.sp[i].life = frange(10, 15); G.sp[i].alive = true;
        return;
    }
}

static void spawn_shadow(void) {
    int alive = 0;
    for (int i = 0; i < MAX_SHADOWS; i++) alive += G.sh[i].alive;
    if (alive >= (int)fminf(MAX_SHADOWS, 4 + G.time / 7.0f)) return;
    for (int i = 0; i < MAX_SHADOWS; i++) {
        if (G.sh[i].alive) continue;
        Shadow *s = &G.sh[i];
        memset(s, 0, sizeof *s);
        V2 p;
        int tries = 0;
        do {
            switch (xr() % 4) {
            case 0: p = v2(frange(0, W), -24); break;
            case 1: p = v2(frange(0, W), H + 24); break;
            case 2: p = v2(-24, frange(0, H)); break;
            default: p = v2(W + 24, frange(0, H)); break;
            }
        } while (len(sub(p, G.p)) < 260 && ++tries < 12);
        s->p = p; s->alive = true; s->phase = frange(0, 2 * PI);
        s->kind = (G.time > 35 && frand() < 0.32f) ? DARTER : LURKER;
        if (s->kind == LURKER) {
            s->r = frange(13, 18);
            s->speed = fminf(170.0f, 62.0f + G.time * 1.1f) * frange(0.85f, 1.15f);
        } else {
            s->r = 10;
            s->speed = fminf(520.0f, 400.0f + G.time * 0.8f);
            s->t = frange(0, 0.8f);
        }
        return;
    }
}

static void kill_shadow(Shadow *s) {
    s->alive = false;
    G.kills++;
    G.score += 25.0f * G.mult;
    G.light = fminf(100, G.light + 5);
    burst(s->p, 18, 220, 0.6f, 120, 70, 170, 3, false);
    burst(s->p, 10, 160, 0.5f, 255, 240, 220, 2, true);
}

static void update_particles(float dt) {
    for (int i = 0; i < MAX_PARTICLES; i++) {
        Particle *q = &G.pt[i];
        if (!q->alive) continue;
        q->p = add(q->p, mul(q->v, dt));
        q->v = mul(q->v, expf(-3.0f * dt));
        q->life -= dt;
        if (q->life <= 0) q->alive = false;
    }
}

static void update_shadows(float dt, bool hunting) {
    float R = light_radius();
    for (int i = 0; i < MAX_SHADOWS; i++) {
        Shadow *s = &G.sh[i];
        if (!s->alive) continue;
        s->t += dt;
        V2 to = sub(G.p, s->p);
        float d = len(to);
        V2 dir = norm(to);
        float lightSlow = d < R * 0.85f ? 0.55f : 1.0f; /* light makes them sluggish */
        if (!hunting) dir = mul(dir, -0.4f);
        if (s->kind == LURKER) {
            V2 perp = v2(-dir.y, dir.x);
            V2 want = add(mul(dir, s->speed * lightSlow), mul(perp, sinf(s->t * 2.2f + s->phase) * 35.0f));
            s->v = add(s->v, mul(sub(want, s->v), fminf(1.0f, 2.6f * dt)));
        } else {
            if (s->dashT > 0) {
                s->dashT -= dt;
                s->v = mul(s->dashDir, s->speed * lightSlow);
                if (s->dashT <= 0) s->t = 0;
            } else {
                s->v = add(s->v, mul(sub(mul(dir, 34.0f), s->v), fminf(1.0f, 4.0f * dt)));
                if (s->t > 1.3f && hunting) { s->dashDir = dir; s->dashT = 0.36f; }
            }
        }
        s->p = add(s->p, mul(s->v, dt));
    }
    /* keep them from stacking */
    for (int i = 0; i < MAX_SHADOWS; i++) {
        if (!G.sh[i].alive) continue;
        for (int j = i + 1; j < MAX_SHADOWS; j++) {
            if (!G.sh[j].alive) continue;
            V2 dlt = sub(G.sh[j].p, G.sh[i].p);
            float d = len(dlt), m = G.sh[i].r + G.sh[j].r;
            if (d < m && d > 0.01f) {
                V2 push = mul(dlt, (m - d) / d * 0.5f);
                G.sh[i].p = sub(G.sh[i].p, push);
                G.sh[j].p = add(G.sh[j].p, push);
            }
        }
    }
}

static void update_play(float dt, Input in) {
    G.time += dt;
    G.score += dt;

    /* movement */
    V2 mv = in.move;
    if (len(mv) > 1) mv = norm(mv);
    if (G.dashT > 0) {
        G.dashT -= dt;
        if (frand() < 0.8f) burst(G.p, 2, 40, 0.35f, 255, 210, 140, 2.5f, true);
    } else {
        G.v = add(G.v, mul(mv, 1450.0f * dt));
        G.v = mul(G.v, expf(-4.0f * dt));
    }
    G.p = add(G.p, mul(G.v, dt));
    if (G.p.x < 14) { G.p.x = 14; G.v.x = fabsf(G.v.x) * 0.4f; }
    if (G.p.x > W - 14) { G.p.x = W - 14; G.v.x = -fabsf(G.v.x) * 0.4f; }
    if (G.p.y < 14) { G.p.y = 14; G.v.y = fabsf(G.v.y) * 0.4f; }
    if (G.p.y > H - 14) { G.p.y = H - 14; G.v.y = -fabsf(G.v.y) * 0.4f; }

    G.invuln -= dt; G.dashCd -= dt; G.flareCd -= dt; G.flareT -= dt; G.hurtFlash -= dt;
    G.multT -= dt;
    if (G.multT <= 0) G.mult = 1;

    /* the light always dims, faster as time goes on */
    G.light -= fminf(7.0f, 3.0f + G.time * 0.025f) * dt;

    /* dash */
    if (in.dash && G.dashCd <= 0) {
        V2 d = len(mv) > 0.1f ? norm(mv) : (len(G.v) > 10 ? norm(G.v) : v2(1, 0));
        G.v = mul(d, 720.0f);
        G.dashT = 0.17f; G.dashCd = 1.1f;
        play(S_DASH, 0.6f);
    }

    float R = light_radius();

    /* flare */
    if (in.flare) {
        if (G.flareCd <= 0 && G.light > 18) {
            G.light -= 14;
            G.flareCd = 0.7f; G.flareT = 0.4f; G.flareMax = R;
            G.shake = fmaxf(G.shake, 5);
            play(S_FLARE, 0.7f);
            int k = 0;
            for (int i = 0; i < MAX_SHADOWS; i++) {
                Shadow *s = &G.sh[i];
                if (s->alive && len(sub(s->p, G.p)) < R + s->r) { kill_shadow(s); k++; }
            }
            if (k) play(S_KILL, fminf(0.9f, 0.4f + 0.1f * k));
            burst(G.p, 30, 380, 0.5f, 255, 230, 180, 2.5f, true);
        } else if (G.flareCd <= 0) {
            G.flareCd = 0.3f;
            play(S_FIZZLE, 0.6f);
        }
    }

    /* sparks */
    G.sparkT -= dt;
    if (G.sparkT <= 0) { spawn_spark(); G.sparkT = frange(0.7f, 1.5f); }
    for (int i = 0; i < MAX_SPARKS; i++) {
        Spark *s = &G.sp[i];
        if (!s->alive) continue;
        s->t += dt; s->life -= dt;
        if (s->life <= 0) { s->alive = false; burst(s->p, 6, 60, 0.4f, 140, 210, 255, 2, true); continue; }
        if (len(sub(s->p, G.p)) < 24) {
            s->alive = false;
            G.mult = G.multT > 0 ? (G.mult < 5 ? G.mult + 1 : 5) : 1;
            G.multT = 2.6f;
            G.light = fminf(100, G.light + 12);
            G.score += 10.0f * G.mult;
            G.sparksTaken++;
            burst(s->p, 16, 200, 0.5f, 150, 220, 255, 2.5f, true);
            play(S_PICK, 0.55f);
        }
    }

    /* shadows */
    G.spawnT -= dt;
    if (G.spawnT <= 0) { spawn_shadow(); G.spawnT = fmaxf(0.5f, 2.5f - G.time * 0.025f) * frange(0.7f, 1.3f); }
    update_shadows(dt, true);
    for (int i = 0; i < MAX_SHADOWS; i++) {
        Shadow *s = &G.sh[i];
        if (!s->alive) continue;
        V2 d = sub(G.p, s->p);
        float dist = len(d);
        if (dist < s->r + 10) {
            V2 n = norm(d);
            if (G.invuln <= 0 && G.dashT <= 0) {
                G.light -= 22;
                G.invuln = 1.0f; G.shake = 11; G.hurtFlash = 0.25f;
                G.v = mul(n, 430);
                G.mult = 1; G.multT = 0;
                burst(G.p, 20, 260, 0.6f, 40, 20, 60, 3, false);
                play(S_HIT, 0.8f);
            }
            s->v = mul(n, -220);
            if (s->kind == DARTER) { s->dashT = 0; s->t = 0; }
        }
    }

    if (G.light <= 0) {
        G.light = 0;
        G.st = ST_DYING; G.dyingT = 1.4f;
        int sc = (int)G.score;
        if (sc > G.best) { G.best = sc; G.newBest = true; save_best(); }
        burst(G.p, 40, 300, 1.0f, 255, 220, 160, 3, true);
        play(S_OVER, 0.7f);
    }
}

/* ---------------------------------------------------------------- rendering */
static SDL_Texture *t_glow, *t_disc, *t_floor, *t_light, *t_canvas;

static SDL_Texture *make_tex(int size, bool glow) {
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, size, size, 32, SDL_PIXELFORMAT_ARGB8888);
    Uint32 *px = s->pixels;
    float c = size / 2.0f;
    for (int y = 0; y < size; y++)
        for (int x = 0; x < size; x++) {
            float d = sqrtf((x + 0.5f - c) * (x + 0.5f - c) + (y + 0.5f - c) * (y + 0.5f - c));
            float a;
            if (glow) { float q = d / c; a = q < 1 ? (1 - q * q) * (1 - q * q) : 0; }
            else a = clampf(c - 1 - d + 0.5f, 0, 1);
            px[y * (s->pitch / 4) + x] = ((Uint32)(a * 255) << 24) | 0xFFFFFF;
        }
    SDL_Texture *t = SDL_CreateTextureFromSurface(ren, s);
    SDL_FreeSurface(s);
    SDL_SetTextureBlendMode(t, glow ? SDL_BLENDMODE_ADD : SDL_BLENDMODE_BLEND);
    return t;
}

static void disc(V2 p, float r, Uint8 R, Uint8 Gc, Uint8 B, Uint8 A) {
    SDL_SetTextureColorMod(t_disc, R, Gc, B);
    SDL_SetTextureAlphaMod(t_disc, A);
    SDL_FRect d = {p.x - r, p.y - r, 2 * r, 2 * r};
    SDL_RenderCopyF(ren, t_disc, NULL, &d);
}

static void glow(V2 p, float r, Uint8 R, Uint8 Gc, Uint8 B, float k) {
    SDL_SetTextureColorMod(t_glow, R, Gc, B);
    SDL_SetTextureAlphaMod(t_glow, (Uint8)(clampf(k, 0, 1) * 255));
    SDL_FRect d = {p.x - r, p.y - r, 2 * r, 2 * r};
    SDL_RenderCopyF(ren, t_glow, NULL, &d);
}

static void ring(V2 c, float r, float thick, Uint8 R, Uint8 Gc, Uint8 B, Uint8 A) {
    SDL_FPoint pts[73];
    SDL_SetRenderDrawColor(ren, R, Gc, B, A);
    for (float k = 0; k < thick; k += 1.0f) {
        for (int i = 0; i <= 72; i++) {
            float a = i * 2 * PI / 72;
            pts[i].x = c.x + cosf(a) * (r - k); pts[i].y = c.y + sinf(a) * (r - k);
        }
        SDL_RenderDrawLinesF(ren, pts, 73);
    }
}

static void build_floor(void) {
    t_floor = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, W, H);
    SDL_SetRenderTarget(ren, t_floor);
    SDL_SetRenderDrawColor(ren, 34, 36, 50, 255);
    SDL_RenderClear(ren);
    for (int i = 0; i < 60; i++) /* broad mottling */
        disc(v2(frange(0, W), frange(0, H)), frange(40, 120), 44, 46, 62, (Uint8)frange(40, 90));
    for (int i = 0; i < 1100; i++) { /* pebbles */
        Uint8 g = (Uint8)frange(46, 82);
        disc(v2(frange(0, W), frange(0, H)), frange(1.2f, 5.5f), g, (Uint8)(g + 2), (Uint8)(g + 14), 255);
    }
    SDL_SetRenderDrawColor(ren, 20, 20, 30, 255); /* cracks */
    for (int i = 0; i < 45; i++) {
        V2 p = v2(frange(0, W), frange(0, H));
        float a = frange(0, 2 * PI);
        for (int k = 0, n = (int)frange(6, 18); k < n; k++) {
            a += frange(-0.7f, 0.7f);
            V2 q = add(p, v2(cosf(a) * 9, sinf(a) * 9));
            SDL_RenderDrawLineF(ren, p.x, p.y, q.x, q.y);
            p = q;
        }
    }
    for (int i = 0; i < 26; i++) { /* glowing moss specks */
        V2 c = v2(frange(30, W - 30), frange(30, H - 30));
        for (int k = 0; k < 9; k++) disc(add(c, v2(frange(-14, 14), frange(-14, 14))), frange(1, 2.5f), 70, 140, 110, 255);
    }
    SDL_SetRenderDrawColor(ren, 12, 12, 18, 255); /* cave wall frame */
    SDL_Rect fr[4] = {{0, 0, W, 6}, {0, H - 6, W, 6}, {0, 0, 6, H}, {W - 6, 0, 6, H}};
    SDL_RenderFillRects(ren, fr, 4);
    SDL_SetRenderTarget(ren, NULL);
}

static void draw_world(void) {
    float R = light_radius();
    bool showPlayer = G.st != ST_DYING && G.st != ST_OVER;

    SDL_RenderCopy(ren, t_floor, NULL, NULL);

    /* lit-scene layer */
    for (int i = 0; i < MAX_SPARKS; i++)
        if (G.sp[i].alive) disc(G.sp[i].p, 5, 180, 235, 255, 255);
    for (int i = 0; i < MAX_SHADOWS; i++) {
        Shadow *s = &G.sh[i];
        if (!s->alive) continue;
        for (int k = 0; k < 3; k++) {
            float a = s->t * 3.0f + s->phase + k * 2.1f;
            V2 o = v2(cosf(a) * s->r * 0.35f, sinf(a) * s->r * 0.35f);
            disc(add(s->p, o), s->r * (k ? 0.75f : 1.0f), 22, 10, 34, 255);
        }
    }
    for (int i = 0; i < MAX_PARTICLES; i++) {
        Particle *q = &G.pt[i];
        if (q->alive && !q->emissive) disc(q->p, q->size, q->r, q->g, q->b, (Uint8)(255 * q->life / q->max));
    }
    if (showPlayer) disc(G.p, 10, 255, 228, 180, 255);

    /* light map: everything multiplied by it */
    SDL_SetRenderTarget(ren, t_light);
    SDL_SetRenderDrawColor(ren, 24, 24, 40, 255);
    SDL_RenderClear(ren);
    if (G.st != ST_OVER) {
        glow(G.p, R, 255, 210, 150, 1.0f);
        glow(G.p, R * 0.45f, 255, 235, 200, 0.8f);
    }
    if (G.flareT > 0) {
        float k = G.flareT / 0.4f;
        glow(G.p, G.flareMax * (1.6f - k * 0.6f), 255, 240, 210, k);
    }
    for (int i = 0; i < MAX_SPARKS; i++) {
        Spark *s = &G.sp[i];
        if (!s->alive) continue;
        float k = 0.65f + 0.25f * sinf(s->t * 4);
        if (s->life < 2.5f) k *= 0.5f + 0.5f * sinf(s->life * 18);
        glow(s->p, 64, 120, 200, 255, k);
    }
    for (int i = 0; i < MAX_SHADOWS; i++)
        if (G.sh[i].alive) glow(G.sh[i].p, G.sh[i].r * 1.8f, 110, 20, 40, 0.5f);
    for (int i = 0; i < MAX_PARTICLES; i++) {
        Particle *q = &G.pt[i];
        if (q->alive && q->emissive) glow(q->p, q->size * 6, q->r, q->g, q->b, 0.5f * q->life / q->max);
    }
    SDL_SetRenderTarget(ren, t_canvas);
    SDL_RenderCopy(ren, t_light, NULL, NULL);

    /* emissive layer: things that glow on their own */
    for (int i = 0; i < MAX_SPARKS; i++) {
        Spark *s = &G.sp[i];
        if (!s->alive) continue;
        Uint8 a = s->life < 2.5f ? (Uint8)(150 + 100 * sinf(s->life * 18)) : 255;
        disc(s->p, 3.2f + 0.8f * sinf(s->t * 6), 225, 250, 255, a);
    }
    for (int i = 0; i < MAX_SHADOWS; i++) {
        Shadow *s = &G.sh[i];
        if (!s->alive) continue;
        V2 f = norm(sub(G.p, s->p));
        if (G.st != ST_PLAY && G.st != ST_PAUSE) f = norm(s->v);
        V2 perp = v2(-f.y, f.x);
        V2 c = add(s->p, mul(f, s->r * 0.3f));
        bool charging = s->kind == DARTER && s->dashT <= 0 && s->t > 0.85f;
        float er = s->kind == DARTER ? 2.2f : 2.6f;
        if (charging) er += 1.2f * sinf(s->t * 30) + 1.0f;
        Uint8 R = 255, Gc = s->kind == DARTER ? 190 : 70, B = s->kind == DARTER ? 60 : 90;
        disc(add(c, mul(perp, s->r * 0.35f)), er, R, Gc, B, 255);
        disc(add(c, mul(perp, -s->r * 0.35f)), er, R, Gc, B, 255);
    }
    for (int i = 0; i < MAX_PARTICLES; i++) {
        Particle *q = &G.pt[i];
        if (q->alive && q->emissive) disc(q->p, q->size, q->r, q->g, q->b, (Uint8)(255 * q->life / q->max));
    }
    if (showPlayer) {
        Uint8 a = (G.invuln > 0 && fmodf(G.invuln, 0.16f) < 0.08f) ? 90 : 255;
        disc(G.p, 6.5f, 255, 250, 235, a);
    }
    if (G.flareT > 0) {
        float k = G.flareT / 0.4f;
        ring(G.p, G.flareMax * (1.0f - k * 0.85f) + 10, 3, 255, 240, 200, (Uint8)(220 * k));
    }
    if (G.hurtFlash > 0) {
        SDL_SetRenderDrawColor(ren, 160, 20, 40, (Uint8)(G.hurtFlash / 0.25f * 90));
        SDL_RenderFillRect(ren, NULL);
    }
}

static void fmt_time(char *buf, size_t n, float t) { snprintf(buf, n, "%d:%02d", (int)t / 60, (int)t % 60); }

static void draw_hud(void) {
    char buf[64];
    /* light bar */
    float k = G.light / 100.0f;
    bool low = G.light < 25;
    SDL_SetRenderDrawColor(ren, 10, 10, 18, 200);
    SDL_FRect bg = {18, 16, 244, 16};
    SDL_RenderFillRectF(ren, &bg);
    if (low && fmodf(G.time, 0.5f) < 0.25f) SDL_SetRenderDrawColor(ren, 255, 70, 70, 255);
    else SDL_SetRenderDrawColor(ren, 255, (Uint8)(150 + 70 * k), 110, 255);
    SDL_FRect fg = {20, 18, 240 * k, 12};
    SDL_RenderFillRectF(ren, &fg);
    SDL_SetRenderDrawColor(ren, 255, 255, 255, 60); /* flare cost marker */
    SDL_FRect mark = {20 + 240 * 0.18f, 18, 1, 12};
    SDL_RenderFillRectF(ren, &mark);
    /* cooldowns */
    float dk = 1.0f - clampf(G.dashCd / 1.1f, 0, 1), fk = 1.0f - clampf(G.flareCd / 0.7f, 0, 1);
    text("DASH", 20, 38, 1.5f, 160, 160, 190, 220, false);
    SDL_SetRenderDrawColor(ren, 120, 200, 255, dk >= 1 ? 255 : 120);
    SDL_FRect dr = {60, 39, 60 * dk, 8};
    SDL_RenderFillRectF(ren, &dr);
    text("FLARE", 136, 38, 1.5f, 160, 160, 190, 220, false);
    SDL_SetRenderDrawColor(ren, 255, 220, 150, (fk >= 1 && G.light > 18) ? 255 : 110);
    SDL_FRect frr = {184, 39, 60 * fk, 8};
    SDL_RenderFillRectF(ren, &frr);

    snprintf(buf, sizeof buf, "%d", (int)G.score);
    text(buf, W / 2.0f, 16, 3, 240, 235, 220, 255, true);
    if (G.mult > 1) {
        snprintf(buf, sizeof buf, "X%d", G.mult);
        text(buf, W / 2.0f, 42, 2, 150, 220, 255, (Uint8)(120 + 135 * clampf(G.multT / 2.6f, 0, 1)), true);
    }
    fmt_time(buf, sizeof buf, G.time);
    text(buf, W - 20 - text_w(buf, 2), 16, 2, 200, 200, 220, 230, false);
    snprintf(buf, sizeof buf, "BEST %d", G.best);
    text(buf, W - 20 - text_w(buf, 1.5f), 38, 1.5f, 150, 150, 180, 200, false);
}

static void dim(Uint8 a) {
    SDL_SetRenderDrawColor(ren, 4, 4, 10, a);
    SDL_RenderFillRect(ren, NULL);
}

static void draw_overlay(void) {
    char buf[96];
    bool blink = fmodf(G.stateT, 1.0f) < 0.65f;
    if (G.st == ST_TITLE) {
        dim(110);
        text_shadowed("LUMEN DRIFT", W / 2.0f, 110, 9, 255, 225, 170, 255);
        text_shadowed("KEEP YOUR LIGHT ALIVE", W / 2.0f, 190, 3, 170, 210, 255, 255);
        const char *lines[] = {
            "MOVE     WASD / ARROWS / LEFT STICK",
            "FLARE    SPACE / A   - BURNS LIGHT, BANISHES NEARBY SHADOWS",
            "DASH     SHIFT / B   - PASS THROUGH SHADOWS UNHARMED",
            "",
            "YOUR LIGHT IS ALWAYS FADING. GATHER BLUE SPARKS TO REFUEL.",
            "CHAIN SPARKS QUICKLY FOR UP TO X5 SCORE.",
            "SHADOWS SLOW DOWN INSIDE YOUR LIGHT. WATCH FOR GOLDEN EYES.",
        };
        for (int i = 0; i < 7; i++) text(lines[i], W / 2.0f, 262 + i * 24, 2, 200, 200, 215, 235, true);
        if (blink) text_shadowed("PRESS ENTER TO BEGIN", W / 2.0f, 456, 3, 255, 240, 200, 255);
        snprintf(buf, sizeof buf, "BEST %d     F11 FULLSCREEN     ESC QUIT", G.best);
        text(buf, W / 2.0f, 560, 1.5f, 130, 130, 160, 220, true);
    } else if (G.st == ST_PAUSE) {
        dim(150);
        text_shadowed("PAUSED", W / 2.0f, 230, 7, 255, 225, 170, 255);
        text("P / ESC  RESUME        Q  QUIT TO TITLE", W / 2.0f, 320, 2, 200, 200, 215, 235, true);
    } else if (G.st == ST_OVER) {
        dim((Uint8)clampf(G.stateT * 300, 0, 170));
        text_shadowed("THE LIGHT FADES", W / 2.0f, 150, 6, 255, 190, 150, 255);
        snprintf(buf, sizeof buf, "SCORE %d", (int)G.score);
        text_shadowed(buf, W / 2.0f, 230, 4, 255, 240, 220, 255);
        if (G.newBest && blink) text_shadowed("NEW BEST!", W / 2.0f, 280, 3, 150, 220, 255, 255);
        char t[16];
        fmt_time(t, sizeof t, G.time);
        snprintf(buf, sizeof buf, "SURVIVED %s   SPARKS %d   SHADOWS BANISHED %d", t, G.sparksTaken, G.kills);
        text(buf, W / 2.0f, 330, 2, 200, 200, 215, 235, true);
        snprintf(buf, sizeof buf, "BEST %d", G.best);
        text(buf, W / 2.0f, 362, 2, 150, 150, 180, 220, true);
        if (G.stateT > 0.8f) text("ENTER  PLAY AGAIN        ESC  TITLE", W / 2.0f, 430, 2, 255, 240, 200, 255, true);
    }
}

static void render(int ww, int wh, bool hud) {
    SDL_SetRenderTarget(ren, t_canvas);
    draw_world();
    if (hud) draw_hud();
    draw_overlay();
    SDL_SetRenderTarget(ren, NULL);
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
    SDL_RenderClear(ren);
    float sc = fminf((float)ww / W, (float)wh / H);
    float sx = G.shake > 0 ? frange(-G.shake, G.shake) : 0, sy = G.shake > 0 ? frange(-G.shake, G.shake) : 0;
    SDL_FRect dst = {(ww - W * sc) / 2 + sx * sc, (wh - H * sc) / 2 + sy * sc, W * sc, H * sc};
    SDL_RenderCopyF(ren, t_canvas, NULL, &dst);
}

/* ---------------------------------------------------------------- state machine */
static void start_game(void) {
    reset_game();
    G.st = ST_PLAY;
    play(S_START, 0.6f);
}

static void step(float dt, Input in) {
    G.stateT += dt;
    G.shake = fmaxf(0, G.shake - dt * 30);
    update_particles(dt);
    switch (G.st) {
    case ST_TITLE: /* attract mode: drift lazily, keep the light full */
        G.time += dt;
        G.light = 100;
        G.p = v2(W / 2.0f + cosf(G.time * 0.4f) * 220, H / 2.0f + 40 + sinf(G.time * 0.8f) * 90);
        G.sparkT -= dt;
        if (G.sparkT <= 0) { spawn_spark(); G.sparkT = 2.0f; }
        for (int i = 0; i < MAX_SPARKS; i++) {
            Spark *s = &G.sp[i];
            if (!s->alive) continue;
            s->t += dt; s->life -= dt;
            if (s->life <= 0) s->alive = false;
        }
        break;
    case ST_PLAY: update_play(dt, in); break;
    case ST_DYING:
        G.dyingT -= dt;
        update_shadows(dt, false);
        if (G.dyingT <= 0) { G.st = ST_OVER; G.stateT = 0; }
        break;
    case ST_OVER: update_shadows(dt, false); break;
    case ST_PAUSE: break;
    }
}

/* Headless self-test: plays the game with an autopilot and writes screenshots. */
static Input autopilot(void) {
    Input in = {0};
    float bd = 1e9f;
    for (int i = 0; i < MAX_SPARKS; i++)
        if (G.sp[i].alive && len(sub(G.sp[i].p, G.p)) < bd) { bd = len(sub(G.sp[i].p, G.p)); in.move = norm(sub(G.sp[i].p, G.p)); }
    float R = light_radius();
    int near = 0;
    for (int i = 0; i < MAX_SHADOWS; i++)
        if (G.sh[i].alive && len(sub(G.sh[i].p, G.p)) < R * 0.8f) near++;
    in.flare = near >= 2 || (near && G.light > 60);
    in.dash = frand() < 0.01f;
    return in;
}

static void shot(const char *path, int ww, int wh) {
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, ww, wh, 32, SDL_PIXELFORMAT_ARGB8888);
    SDL_RenderReadPixels(ren, NULL, SDL_PIXELFORMAT_ARGB8888, s->pixels, s->pitch);
    SDL_SaveBMP(s, path);
    SDL_FreeSurface(s);
}

static int selftest(const char *dir) {
    char path[1200];
    G.st = ST_TITLE;
    for (int i = 0; i < 120; i++) step(1 / 60.0f, (Input){0});
    render(W, H, false); snprintf(path, sizeof path, "%s/title.bmp", dir); shot(path, W, H);
    start_game();
    int frames = 0, peakShadows = 0;
    float shotAt = 40.0f;
    bool took = false;
    while (G.st == ST_PLAY && frames < 60 * 300) {
        step(1 / 60.0f, autopilot());
        frames++;
        int n = 0;
        for (int i = 0; i < MAX_SHADOWS; i++) n += G.sh[i].alive;
        if (n > peakShadows) peakShadows = n;
        if (!took && G.time >= shotAt) {
            render(W, H, true); snprintf(path, sizeof path, "%s/play.bmp", dir); shot(path, W, H); took = true;
        }
    }
    if (!took) { render(W, H, true); snprintf(path, sizeof path, "%s/play.bmp", dir); shot(path, W, H); }
    for (int i = 0; i < 200 && G.st != ST_OVER; i++) step(1 / 60.0f, (Input){0});
    for (int i = 0; i < 90; i++) step(1 / 60.0f, (Input){0});
    render(W, H, true); snprintf(path, sizeof path, "%s/over.bmp", dir); shot(path, W, H);
    printf("selftest: survived %.1fs score %d sparks %d kills %d peak_shadows %d final_state %d\n",
           G.time, (int)G.score, G.sparksTaken, G.kills, peakShadows, G.st);
    return 0;
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
    SDL_Window *win = SDL_CreateWindow("Lumen Drift", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, W, H,
                                       testdir ? SDL_WINDOW_HIDDEN : (SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI));
    if (!win) { fprintf(stderr, "window: %s\n", SDL_GetError()); return 1; }
    SDL_SetWindowMinimumSize(win, W / 2, H / 2);
    ren = SDL_CreateRenderer(win, -1, testdir ? SDL_RENDERER_SOFTWARE | SDL_RENDERER_TARGETTEXTURE
                                              : SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_TARGETTEXTURE);
    if (!ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE | SDL_RENDERER_TARGETTEXTURE);
    if (!ren) { fprintf(stderr, "renderer: %s\n", SDL_GetError()); return 1; }
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);

    t_glow = make_tex(256, true);
    t_disc = make_tex(64, false);
    build_floor();
    t_light = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, W, H);
    SDL_SetTextureBlendMode(t_light, SDL_BLENDMODE_MOD);
    t_canvas = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, W, H);
    SDL_SetTextureBlendMode(t_canvas, SDL_BLENDMODE_NONE);

    if (!testdir) load_best();
    reset_game();
    G.st = ST_TITLE;

    if (testdir) { no_save = true; G.best = 0; int r = selftest(testdir); SDL_Quit(); return r; }

    SDL_GameController *pad = NULL;
    bool running = true;
    Uint64 prev = SDL_GetPerformanceCounter();
    while (running) {
        Input in = {0};
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
            case SDL_QUIT: running = false; break;
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
                if (k == SDLK_F11) {
                    bool fs = SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN_DESKTOP;
                    SDL_SetWindowFullscreen(win, fs ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
                } else if (G.st == ST_TITLE) {
                    if (k == SDLK_RETURN || k == SDLK_KP_ENTER || k == SDLK_SPACE) start_game();
                    else if (k == SDLK_ESCAPE) running = false;
                } else if (G.st == ST_PLAY) {
                    if (k == SDLK_ESCAPE || k == SDLK_p) G.st = ST_PAUSE;
                    else if (k == SDLK_SPACE || k == SDLK_j || k == SDLK_z) in.flare = true;
                    else if (k == SDLK_LSHIFT || k == SDLK_RSHIFT || k == SDLK_k || k == SDLK_x) in.dash = true;
                } else if (G.st == ST_PAUSE) {
                    if (k == SDLK_ESCAPE || k == SDLK_p) G.st = ST_PLAY;
                    else if (k == SDLK_q) { reset_game(); G.st = ST_TITLE; }
                } else if (G.st == ST_OVER && G.stateT > 0.8f) {
                    if (k == SDLK_RETURN || k == SDLK_KP_ENTER || k == SDLK_SPACE) start_game();
                    else if (k == SDLK_ESCAPE) { reset_game(); G.st = ST_TITLE; }
                }
                break;
            }
            case SDL_CONTROLLERBUTTONDOWN: {
                int b = e.cbutton.button;
                if (G.st == ST_TITLE && (b == SDL_CONTROLLER_BUTTON_START || b == SDL_CONTROLLER_BUTTON_A)) start_game();
                else if (G.st == ST_PLAY) {
                    if (b == SDL_CONTROLLER_BUTTON_START) G.st = ST_PAUSE;
                    else if (b == SDL_CONTROLLER_BUTTON_A || b == SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) in.flare = true;
                    else if (b == SDL_CONTROLLER_BUTTON_B || b == SDL_CONTROLLER_BUTTON_X || b == SDL_CONTROLLER_BUTTON_LEFTSHOULDER) in.dash = true;
                } else if (G.st == ST_PAUSE) {
                    if (b == SDL_CONTROLLER_BUTTON_START) G.st = ST_PLAY;
                    else if (b == SDL_CONTROLLER_BUTTON_BACK) { reset_game(); G.st = ST_TITLE; }
                } else if (G.st == ST_OVER && G.stateT > 0.8f) {
                    if (b == SDL_CONTROLLER_BUTTON_START || b == SDL_CONTROLLER_BUTTON_A) start_game();
                    else if (b == SDL_CONTROLLER_BUTTON_BACK) { reset_game(); G.st = ST_TITLE; }
                }
                break;
            }
            }
        }
        const Uint8 *ks = SDL_GetKeyboardState(NULL);
        in.move.x = (float)((ks[SDL_SCANCODE_D] || ks[SDL_SCANCODE_RIGHT]) - (ks[SDL_SCANCODE_A] || ks[SDL_SCANCODE_LEFT]));
        in.move.y = (float)((ks[SDL_SCANCODE_S] || ks[SDL_SCANCODE_DOWN]) - (ks[SDL_SCANCODE_W] || ks[SDL_SCANCODE_UP]));
        if (pad) {
            float ax = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTX) / 32767.0f;
            float ay = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTY) / 32767.0f;
            if (fabsf(ax) > 0.22f || fabsf(ay) > 0.22f) { in.move.x += ax; in.move.y += ay; }
            in.move.x += SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_RIGHT) - SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_LEFT);
            in.move.y += SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_DOWN) - SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_UP);
        }

        Uint64 now = SDL_GetPerformanceCounter();
        float dt = (float)(now - prev) / (float)SDL_GetPerformanceFrequency();
        prev = now;
        if (dt > 1 / 30.0f) dt = 1 / 30.0f;
        step(dt, in);

        int ww, wh;
        SDL_GetRendererOutputSize(ren, &ww, &wh);
        render(ww, wh, G.st != ST_TITLE);
        SDL_RenderPresent(ren);
    }

    if (pad) SDL_GameControllerClose(pad);
    if (adev) SDL_CloseAudioDevice(adev);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
