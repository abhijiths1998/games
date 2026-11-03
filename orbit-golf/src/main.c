/*
 * Orbit Golf - gravity mini-golf in space.
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
#define MAX_PARTICLES 1400
#define MAX_VOICES 16
#define RATE 44100
#define STEP (1.0f / 240.0f)   /* fixed physics step */
#define BR 6.0f                /* ball radius */
#define BELT_T 8.0f            /* asteroid belt half thickness */
#define NHOLES 12
#define MAX_STROKES 10
#define NSTARS 260

/* ---------------------------------------------------------------- math / rng */
typedef struct { float x, y; } V2;
typedef struct { Uint8 r, g, b; } Col;
static V2 v2(float x, float y) { V2 r = {x, y}; return r; }
static V2 add(V2 a, V2 b) { return v2(a.x + b.x, a.y + b.y); }
static V2 sub(V2 a, V2 b) { return v2(a.x - b.x, a.y - b.y); }
static V2 mul(V2 a, float s) { return v2(a.x * s, a.y * s); }
static float dot(V2 a, V2 b) { return a.x * b.x + a.y * b.y; }
static V2 lerp_v2(V2 a, V2 b, float t) { return v2(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t); }
static float len(V2 a) { return sqrtf(a.x * a.x + a.y * a.y); }
static V2 norm(V2 a) { float l = len(a); return l > 1e-5f ? mul(a, 1.0f / l) : v2(0, 0); }
static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
static float lerpf(float a, float b, float t) { return a + (b - a) * t; }
static Col C(int r, int g, int b) { Col c = {(Uint8)r, (Uint8)g, (Uint8)b}; return c; }
static Col lerpc(Col a, Col b, float t) {
    t = clampf(t, 0, 1);
    return C((int)lerpf(a.r, b.r, t), (int)lerpf(a.g, b.g, t), (int)lerpf(a.b, b.b, t));
}

static Uint32 rng = 0x9E3779B9u;
static Uint32 xr(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
static float frand(void) { return (float)(xr() >> 8) * (1.0f / 16777216.0f); }
static float frange(float a, float b) { return a + (b - a) * frand(); }

static float hash2(int x, int y, int s) {
    Uint32 h = (Uint32)x * 374761393u + (Uint32)y * 668265263u + (Uint32)s * 2147483647u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return (float)(h & 0xFFFFFF) / 16777216.0f;
}
static float vnoise(float x, float y, int s) {
    int xi = (int)floorf(x), yi = (int)floorf(y);
    float fx = x - (float)xi, fy = y - (float)yi;
    fx = fx * fx * (3 - 2 * fx); fy = fy * fy * (3 - 2 * fy);
    float a = hash2(xi, yi, s), b = hash2(xi + 1, yi, s), c = hash2(xi, yi + 1, s), d = hash2(xi + 1, yi + 1, s);
    return lerpf(lerpf(a, b, fx), lerpf(c, d, fx), fy);
}
static float fbm(float x, float y, int s, int oct) {
    float a = 0.5f, sum = 0, norm_ = 0;
    for (int o = 0; o < oct; o++) { sum += a * vnoise(x, y, s + o * 17); norm_ += a; x *= 2.03f; y *= 2.03f; a *= 0.5f; }
    return sum / norm_;
}

/* ---------------------------------------------------------------- audio */
typedef struct { float *d; int n; } Sound;
enum { S_SHOT, S_BOUNCE, S_CLACK, S_WORM, S_SINK, S_BH, S_UI, S_FANFARE, S_COUNT };
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
    for (int i = 0; i < n; i++) out[i] = tanhf(out[i]);
}

static void play(int id, float vol) {
    if (!adev || !snd[id].d) return;
    SDL_LockAudioDevice(adev);
    int slot = 0, best = -1;
    for (int v = 0; v < MAX_VOICES; v++) {
        if (!voices[v].d) { slot = v; break; }
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
static float tri_wave(float ph) { float x = fmodf(ph / (2 * PI), 1.0f); return 4.0f * fabsf(x - 0.5f) - 1.0f; }

static void build_sounds(void) {
    float *d, ph, lp;
    /* shot: swept thump + airy noise */
    d = mk(&snd[S_SHOT], 0.32f); ph = 0; lp = 0;
    for (int i = 0; i < snd[S_SHOT].n; i++) {
        float t = (float)i / RATE;
        ph += 2 * PI * (480.0f * expf(-t * 11) + 95.0f) / RATE;
        lp += (frange(-1, 1) - lp) * 0.3f;
        d[i] = (sinf(ph) * 0.7f + lp * 0.45f * expf(-t * 12)) * expf(-t * 8) * 0.8f;
    }
    /* bounce: soft low knock */
    d = mk(&snd[S_BOUNCE], 0.16f); ph = 0;
    for (int i = 0; i < snd[S_BOUNCE].n; i++) {
        float t = (float)i / RATE;
        ph += 2 * PI * (170.0f * expf(-t * 16) + 62.0f) / RATE;
        d[i] = (sinf(ph) * 0.9f + frange(-1, 1) * 0.15f * expf(-t * 80)) * expf(-t * 26) * 0.8f;
    }
    /* clack: rocky tick */
    d = mk(&snd[S_CLACK], 0.14f); ph = 0; lp = 0;
    for (int i = 0; i < snd[S_CLACK].n; i++) {
        float t = (float)i / RATE;
        ph += 2 * PI * (1250.0f * expf(-t * 30) + 380.0f) / RATE;
        lp += (frange(-1, 1) - lp) * 0.55f;
        d[i] = (lp * 0.6f + sinf(ph) * 0.5f) * expf(-t * 34) * 0.7f;
    }
    /* wormhole: warbling sweep */
    d = mk(&snd[S_WORM], 0.55f); ph = 0;
    for (int i = 0; i < snd[S_WORM].n; i++) {
        float t = (float)i / RATE;
        float f = 260.0f + 700.0f * sinf(PI * t / 0.55f);
        ph += 2 * PI * f / RATE;
        d[i] = (sinf(ph + 3.0f * sinf(2 * PI * 17.0f * t)) * 0.55f + sinf(ph * 2.01f) * 0.2f) * sinf(PI * t / 0.55f) * 0.6f;
    }
    /* sink: bright bell arpeggio */
    d = mk(&snd[S_SINK], 1.1f);
    const float sn[4] = {523.3f, 659.3f, 784.0f, 1046.5f};
    for (int k = 0; k < 4; k++)
        for (int i = (int)(k * 0.1f * RATE); i < snd[S_SINK].n; i++) {
            float lt = (float)i / RATE - k * 0.1f;
            d[i] += (sinf(2 * PI * sn[k] * lt) + 0.35f * sinf(2 * PI * sn[k] * 2.0f * lt)) * expf(-lt * (k == 3 ? 3.2f : 6.0f)) * 0.22f;
        }
    /* black hole: descending groan */
    d = mk(&snd[S_BH], 1.0f); ph = 0; lp = 0;
    for (int i = 0; i < snd[S_BH].n; i++) {
        float t = (float)i / RATE;
        ph += 2 * PI * (420.0f * expf(-t * 3.4f) + 38.0f) / RATE;
        lp += (frange(-1, 1) - lp) * 0.08f;
        d[i] = (sinf(ph) * 0.6f + (sinf(ph * 0.5f) > 0 ? 0.25f : -0.25f) + lp * 0.9f) * expf(-t * 2.6f) * 0.7f;
    }
    /* ui blip */
    d = mk(&snd[S_UI], 0.09f); ph = 0;
    for (int i = 0; i < snd[S_UI].n; i++) {
        float t = (float)i / RATE;
        ph += 2 * PI * 920.0f / RATE;
        d[i] = sinf(ph) * expf(-t * 40) * 0.4f;
    }
    /* fanfare */
    d = mk(&snd[S_FANFARE], 1.5f); ph = 0;
    const float fn[5] = {392.0f, 523.3f, 659.3f, 784.0f, 1046.5f};
    for (int k = 0; k < 5; k++)
        for (int i = (int)(k * 0.13f * RATE); i < snd[S_FANFARE].n; i++) {
            float lt = (float)i / RATE - k * 0.13f;
            d[i] += (tri_wave(2 * PI * fn[k] * lt) * 0.7f + sinf(2 * PI * fn[k] * lt) * 0.3f) * expf(-lt * (k == 4 ? 2.4f : 7.0f)) * 0.25f;
        }
}

/* ---------------------------------------------------------------- pixel font (5x7) */
static const char FONT_CHARS[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ:.-!/?, +()=*'";
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
    {0,4,4,31,4,4,0},{2,4,8,8,8,4,2},{8,4,2,2,2,4,8},{0,31,0,31,0,0,0},{0,21,14,31,14,21,0},{4,4,8,0,0,0,0},
};

static SDL_Renderer *ren;

static float text_w(const char *s, float sc) { return (float)strlen(s) * 6 * sc - sc; }

/* Vertical gradient text: colour c1 at the top row, c2 at the bottom row. */
static void text_g(const char *s, float x, float y, float sc, Col c1, Col c2, float a, bool center) {
    if (center) x -= text_w(s, sc) / 2;
    for (; *s; s++, x += 6 * sc) {
        char c = *s >= 'a' && *s <= 'z' ? (char)(*s - 32) : *s;
        const char *at = strchr(FONT_CHARS, c);
        if (!at || !c) continue;
        const unsigned char *gl = FONT[at - FONT_CHARS];
        for (int row = 0; row < 7; row++) {
            Col cc = lerpc(c1, c2, (float)row / 6.0f);
            SDL_SetRenderDrawColor(ren, cc.r, cc.g, cc.b, (Uint8)(clampf(a, 0, 1) * 255));
            for (int col = 0; col < 5; col++)
                if (gl[row] & (16 >> col)) {
                    SDL_FRect px = {x + (float)col * sc, y + (float)row * sc, sc, sc};
                    SDL_RenderFillRectF(ren, &px);
                }
        }
    }
}
/* Text with a soft drop shadow. */
static void text(const char *s, float x, float y, float sc, Col c, float a) {
    text_g(s, x + sc * 0.7f, y + sc * 0.7f, sc, C(0, 0, 8), C(0, 0, 8), a * 0.65f, true);
    text_g(s, x, y, sc, c, c, a, true);
}
static void textg(const char *s, float x, float y, float sc, Col c1, Col c2, float a) {
    text_g(s, x + sc * 0.7f, y + sc * 0.7f, sc, C(0, 0, 8), C(0, 0, 8), a * 0.65f, true);
    text_g(s, x, y, sc, c1, c2, a, true);
}
static void text_l(const char *s, float x, float y, float sc, Col c, float a) {
    text_g(s, x + sc * 0.7f, y + sc * 0.7f, sc, C(0, 0, 8), C(0, 0, 8), a * 0.65f, false);
    text_g(s, x, y, sc, c, c, a, false);
}

/* ---------------------------------------------------------------- levels */
#define MAXP 8
#define MAXBH 4
#define MAXBT 8
#define MAXWM 2
#define MAXMO 4
typedef struct { float x, y, r, gf, ring, tilt; int style; } Planet;
typedef struct { float x, y; } BHole;
typedef struct { float x1, y1, x2, y2; } Belt;
typedef struct { float ax, ay, bx, by; } Worm;
typedef struct { int pl; float cx, cy, orbit, r, w, ph; } Moon;
typedef struct {
    const char *name;
    int par, neb;
    V2 tee, hole;
    Planet p[MAXP]; int np;
    BHole bh[MAXBH]; int nbh;
    Belt bt[MAXBT]; int nbt;
    Worm wm[MAXWM]; int nwm;
    Moon mo[MAXMO]; int nmo;
} Level;
static Level lv[NHOLES];

static Level *L_begin(int i, const char *name, int par, float tx, float ty, float hx, float hy, int neb) {
    Level *l = &lv[i];
    memset(l, 0, sizeof *l);
    l->name = name; l->par = par; l->neb = neb;
    l->tee = v2(tx, ty); l->hole = v2(hx, hy);
    return l;
}
static void L_planet(Level *l, float x, float y, float r, int style, float gf, float ring, float tilt) {
    Planet p = {x, y, r, gf, ring, tilt, style};
    l->p[l->np++] = p;
}
static void L_bh(Level *l, float x, float y) { l->bh[l->nbh].x = x; l->bh[l->nbh].y = y; l->nbh++; }
static void L_belt(Level *l, float x1, float y1, float x2, float y2) {
    Belt b = {x1, y1, x2, y2};
    l->bt[l->nbt++] = b;
}
static void L_worm(Level *l, float ax, float ay, float bx, float by) {
    Worm w = {ax, ay, bx, by};
    l->wm[l->nwm++] = w;
}
static void L_moon(Level *l, int pl, float cx, float cy, float orbit, float r, float w, float ph) {
    Moon m = {pl, cx, cy, orbit, r, w, ph};
    l->mo[l->nmo++] = m;
}

static void build_levels(void) {
    Level *l;
    l = L_begin(0, "FIRST LIGHT", 2, 110, 330, 840, 290, 0);
    L_planet(l, 470, 450, 38, 3, 1.0f, 0, 0);
    L_planet(l, 610, 160, 28, 5, 1.0f, 0, 0);

    l = L_begin(1, "THE BIG ONE", 3, 100, 510, 860, 110, 1);
    L_planet(l, 480, 300, 62, 2, 1.0f, 0, 0);
    L_planet(l, 810, 440, 26, 1, 1.0f, 0, 0);

    l = L_begin(2, "SLINGSHOT", 3, 90, 110, 860, 520, 2);
    L_planet(l, 380, 340, 48, 0, 1.0f, 0, 0);
    L_planet(l, 690, 190, 32, 4, 1.2f, 0, 0);
    L_planet(l, 640, 500, 24, 5, 1.0f, 0, 0);

    l = L_begin(3, "ASTEROID ALLEY", 3, 90, 300, 870, 300, 3);
    L_belt(l, 400, 50, 400, 380);
    L_belt(l, 610, 560, 610, 220);
    L_planet(l, 770, 140, 30, 1, 1.0f, 0, 0);
    L_planet(l, 250, 500, 30, 0, 1.0f, 0, 0);

    l = L_begin(4, "EVENT HORIZON", 3, 100, 300, 860, 300, 4);
    L_bh(l, 480, 300);
    L_planet(l, 480, 110, 36, 0, 1.0f, 0, 0);
    L_planet(l, 470, 510, 32, 4, 1.0f, 0, 0);

    l = L_begin(5, "WORMHOLE EXPRESS", 3, 90, 150, 870, 480, 5);
    L_belt(l, 500, 45, 500, 600);
    L_worm(l, 300, 400, 690, 160);
    L_planet(l, 790, 330, 30, 3, 1.0f, 0, 0);

    l = L_begin(6, "BINARY DANCE", 4, 90, 520, 860, 100, 1);
    L_planet(l, 380, 230, 38, 0, 1.2f, 0, 0);
    L_planet(l, 520, 430, 38, 1, 1.2f, 0, 0);
    L_planet(l, 810, 500, 26, 5, 1.0f, 0, 0);
    L_bh(l, 700, 290);

    l = L_begin(7, "MOON ORBIT", 4, 90, 540, 870, 70, 2);
    L_planet(l, 480, 300, 50, 2, 1.0f, 1.9f, 0.35f);
    L_moon(l, 0, 0, 0, 135, 15, 1.1f, 0);
    L_moon(l, 0, 0, 0, 200, 11, -0.8f, 3.0f);
    L_planet(l, 800, 450, 32, 3, 1.0f, 0, 0);

    l = L_begin(8, "RING ROAD", 4, 90, 100, 850, 310, 3);
    L_planet(l, 340, 340, 56, 4, 1.0f, 2.0f, -0.3f);
    L_planet(l, 610, 520, 30, 0, 1.0f, 0, 0);
    L_belt(l, 720, 170, 720, 430);
    L_belt(l, 720, 430, 935, 430);
    L_belt(l, 720, 170, 840, 170);

    l = L_begin(9, "TWIN WELLS", 4, 90, 300, 870, 500, 4);
    L_bh(l, 360, 190);
    L_bh(l, 560, 430);
    L_worm(l, 200, 520, 760, 130);
    L_planet(l, 690, 300, 34, 0, 1.0f, 0, 0);

    l = L_begin(10, "PINBALL CLUSTER", 4, 90, 540, 870, 80, 5);
    L_planet(l, 260, 420, 26, 0, 1.0f, 0, 0);
    L_planet(l, 400, 200, 30, 1, 1.0f, 0, 0);
    L_planet(l, 560, 360, 28, 3, 1.0f, 0, 0);
    L_planet(l, 720, 220, 24, 5, 1.0f, 0, 0);
    L_planet(l, 720, 470, 34, 4, 1.0f, 0, 0);
    L_belt(l, 440, 520, 580, 590);
    L_belt(l, 600, 110, 520, 50);
    L_moon(l, 2, 0, 0, 72, 10, 1.6f, 1.0f);

    l = L_begin(11, "THE GAUNTLET", 5, 80, 300, 890, 300, 0);
    L_bh(l, 300, 300);
    L_planet(l, 300, 130, 36, 3, 1.0f, 0, 0);
    L_planet(l, 300, 470, 36, 1, 1.0f, 0, 0);
    L_belt(l, 540, 45, 540, 235);
    L_belt(l, 540, 365, 540, 600);
    L_worm(l, 440, 300, 700, 110);
    L_planet(l, 740, 420, 46, 2, 1.0f, 1.8f, 0.2f);
    L_moon(l, 3, 0, 0, 95, 12, 1.3f, 2.0f);
    L_bh(l, 810, 190);
}

static V2 moon_pos(const Level *L, int i, float t) {
    const Moon *m = &L->mo[i];
    V2 c = m->pl >= 0 ? v2(L->p[m->pl].x, L->p[m->pl].y) : v2(m->cx, m->cy);
    float a = m->ph + m->w * t;
    return v2(c.x + cosf(a) * m->orbit, c.y + sinf(a) * m->orbit);
}
static V2 moon_vel(const Level *L, int i, float t) {
    const Moon *m = &L->mo[i];
    float a = m->ph + m->w * t;
    return v2(-sinf(a) * m->orbit * m->w, cosf(a) * m->orbit * m->w);
}

/* ---------------------------------------------------------------- physics */
typedef struct { V2 p, v; float wcool, impact, still; bool rest; int ev; V2 evp, evp2; } Ball;
enum { EV_BOUNCE = 1, EV_HOLE = 2, EV_BH = 4, EV_WORM = 8, EV_REST = 16, EV_CLACK = 32 };

static void collide_circle(Ball *b, V2 c, V2 cv, float R, float e, float fr, bool canrest) {
    V2 d = sub(b->p, c);
    float dist = len(d);
    if (dist >= R) return;
    V2 n = dist > 1e-4f ? mul(d, 1.0f / dist) : v2(0, -1);
    b->p = add(c, mul(n, R));
    V2 rv = sub(b->v, cv);
    float vn = dot(rv, n);
    if (vn < 0) {
        float imp = -vn;
        V2 vt = sub(rv, mul(n, vn));
        rv = add(mul(n, -vn * e), mul(vt, fr));
        b->v = add(rv, cv);
        if (imp > b->impact) b->impact = imp;
        if (imp > 55) b->ev |= EV_BOUNCE;
        if (canrest && len(rv) < 48) { b->v = v2(0, 0); b->rest = true; b->ev |= EV_REST; }
    }
}

static void phys(const Level *L, Ball *b, float t) {
    b->ev = 0; b->impact = 0;
    if (b->rest) return;
    V2 a = v2(0, 0);
    for (int i = 0; i < L->np; i++) {
        const Planet *p = &L->p[i];
        V2 d = sub(v2(p->x, p->y), b->p);
        float r2 = dot(d, d) + 120.0f;
        float k = p->gf * 900.0f * p->r * p->r / (r2 * sqrtf(r2));
        a = add(a, mul(d, k));
    }
    for (int i = 0; i < L->nbh; i++) {
        V2 d = sub(v2(L->bh[i].x, L->bh[i].y), b->p);
        float r2 = dot(d, d) + 300.0f;
        a = add(a, mul(d, 1.1e6f / (r2 * sqrtf(r2))));
    }
    for (int i = 0; i < L->nwm; i++) {
        V2 d = sub(v2(L->wm[i].ax, L->wm[i].ay), b->p);
        float r2 = dot(d, d) + 400.0f;
        a = add(a, mul(d, 70000.0f / (r2 * sqrtf(r2))));
        d = sub(v2(L->wm[i].bx, L->wm[i].by), b->p);
        r2 = dot(d, d) + 400.0f;
        a = add(a, mul(d, 70000.0f / (r2 * sqrtf(r2))));
    }
    {   /* the cup tugs the ball in gently */
        V2 d = sub(L->hole, b->p);
        float dist = len(d);
        if (dist < 55 && dist > 1e-3f) a = add(a, mul(d, 420.0f * (1 - dist / 55) / dist));
    }
    b->v = add(b->v, mul(a, STEP));
    b->v = mul(b->v, 1.0f - 0.1f * STEP);
    float sp = len(b->v);
    if (sp > 1000) b->v = mul(b->v, 1000 / sp);
    b->p = add(b->p, mul(b->v, STEP));
    if (b->wcool > 0) b->wcool -= STEP;

    for (int i = 0; i < L->np; i++)
        collide_circle(b, v2(L->p[i].x, L->p[i].y), v2(0, 0), L->p[i].r + BR, 0.5f, 0.99f, true);
    for (int i = 0; i < L->nmo; i++)
        collide_circle(b, moon_pos(L, i, t), moon_vel(L, i, t), L->mo[i].r + BR, 0.8f, 0.99f, false);
    for (int i = 0; i < L->nbt; i++) {
        const Belt *s = &L->bt[i];
        V2 ab = v2(s->x2 - s->x1, s->y2 - s->y1), ap = sub(b->p, v2(s->x1, s->y1));
        float h = clampf(dot(ap, ab) / dot(ab, ab), 0, 1);
        int before = b->ev;
        collide_circle(b, v2(s->x1 + ab.x * h, s->y1 + ab.y * h), v2(0, 0), BELT_T + BR, 0.85f, 0.995f, false);
        if (b->ev != before) b->ev |= EV_CLACK;
    }
    {   /* screen edges */
        float vn = 0;
        if (b->p.x < BR) { b->p.x = BR; vn = -b->v.x; b->v.x = -b->v.x * 0.7f; }
        if (b->p.x > W - BR) { b->p.x = W - BR; vn = b->v.x; b->v.x = -b->v.x * 0.7f; }
        if (b->p.y < BR) { b->p.y = BR; vn = -b->v.y; b->v.y = -b->v.y * 0.7f; }
        if (b->p.y > H - BR) { b->p.y = H - BR; vn = b->v.y; b->v.y = -b->v.y * 0.7f; }
        if (vn > 0) { if (vn > b->impact) b->impact = vn; if (vn > 55) b->ev |= EV_BOUNCE; }
    }
    for (int i = 0; i < L->nbh; i++)
        if (len(sub(b->p, v2(L->bh[i].x, L->bh[i].y))) < 13) { b->ev |= EV_BH; return; }
    if (len(sub(b->p, L->hole)) < 15 && len(b->v) < 340) { b->ev |= EV_HOLE; return; }
    if (b->wcool <= 0)
        for (int i = 0; i < L->nwm; i++) {
            V2 A = v2(L->wm[i].ax, L->wm[i].ay), B = v2(L->wm[i].bx, L->wm[i].by);
            V2 from, to;
            if (len(sub(b->p, A)) < 14) { from = A; to = B; }
            else if (len(sub(b->p, B)) < 14) { from = B; to = A; }
            else continue;
            b->evp = from; b->evp2 = to;
            b->p = add(to, mul(norm(b->v), 20));
            b->wcool = 0.5f;
            b->ev |= EV_WORM;
            break;
        }
    if (len(b->v) < 6) { b->still += STEP; if (b->still > 0.7f) { b->rest = true; b->v = v2(0, 0); b->ev |= EV_REST; } }
    else b->still = 0;
}

static V2 shot_vel(float ang, float pw) { float s = 90.0f + pw * 630.0f; return v2(cosf(ang) * s, sinf(ang) * s); }

static int preview(const Level *L, V2 p, V2 v, float t0, V2 *out, int max) {
    Ball b = {0};
    b.p = p; b.v = v;
    int n = 0;
    float t = t0;
    for (int s = 0; s < 400 && n < max; s++, t += STEP) {
        phys(L, &b, t);
        if (b.ev & (EV_BH | EV_HOLE)) { out[n++] = b.p; break; }
        if (b.rest) break;
        if (s % 6 == 5) out[n++] = b.p;
    }
    return n;
}

/* ---------------------------------------------------------------- procedural textures */
static SDL_Texture *t_glow, *t_disc, *t_canvas, *t_vig, *t_neb, *t_planet[6];

static SDL_Texture *make_tex(int size, bool gl) {
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, size, size, 32, SDL_PIXELFORMAT_ARGB8888);
    Uint32 *px = s->pixels;
    float c = (float)size / 2.0f;
    for (int y = 0; y < size; y++)
        for (int x = 0; x < size; x++) {
            float d = sqrtf(((float)x + 0.5f - c) * ((float)x + 0.5f - c) + ((float)y + 0.5f - c) * ((float)y + 0.5f - c));
            float a;
            if (gl) { float q = d / c; a = q < 1 ? (1 - q * q) * (1 - q * q) : 0; }
            else a = clampf(c - 1 - d + 0.5f, 0, 1);
            px[y * (s->pitch / 4) + x] = ((Uint32)(a * 255) << 24) | 0xFFFFFF;
        }
    SDL_Texture *t = SDL_CreateTextureFromSurface(ren, s);
    SDL_FreeSurface(s);
    SDL_SetTextureBlendMode(t, gl ? SDL_BLENDMODE_ADD : SDL_BLENDMODE_BLEND);
    return t;
}

static Col mix3(float r1, float g1, float b1, float r2, float g2, float b2, float t) {
    t = clampf(t, 0, 1);
    return C((int)(lerpf(r1, r2, t) * 255), (int)(lerpf(g1, g2, t) * 255), (int)(lerpf(b1, b2, t) * 255));
}

static SDL_Texture *make_planet(int style) {
    const int S = 192;
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, S, S, 32, SDL_PIXELFORMAT_ARGB8888);
    Uint32 *px = s->pixels;
    float c = S / 2.0f;
    const float Lx = -0.52f, Ly = -0.58f, Lz = 0.63f;
    for (int y = 0; y < S; y++)
        for (int x = 0; x < S; x++) {
            float nx = ((float)x + 0.5f - c) / c, ny = ((float)y + 0.5f - c) / c;
            float d2 = nx * nx + ny * ny, d = sqrtf(d2);
            float alpha = clampf((1 - d) * c * 0.8f, 0, 1);
            if (alpha <= 0) { px[y * (s->pitch / 4) + x] = 0; continue; }
            float nz = sqrtf(fmaxf(0, 1 - d2));
            float diff = fmaxf(0, nx * Lx + ny * Ly + nz * Lz);
            float u = atan2f(nx, nz + 0.001f) * 1.7f, v = asinf(clampf(ny, -1, 1)) * 1.7f;
            Col col;
            switch (style) {
            case 0: { float n = fbm(u * 2.2f + 3, v * 2.2f, 11, 5); col = mix3(0.38f, 0.16f, 0.10f, 0.88f, 0.52f, 0.30f, n * 1.5f - 0.2f); break; }
            case 1: { float n = fbm(u * 3.0f, v * 1.1f, 21, 5); col = mix3(0.50f, 0.72f, 0.90f, 0.96f, 0.99f, 1.0f, n * 1.6f - 0.3f); break; }
            case 2: { float bnd = sinf(v * 9.0f + fbm(u * 1.5f, v * 3.0f, 31, 4) * 5.0f) * 0.5f + 0.5f;
                      col = mix3(0.72f, 0.36f, 0.12f, 0.99f, 0.86f, 0.60f, bnd); break; }
            case 3: { float n = fbm(u * 1.8f + 9, v * 1.8f, 41, 5);
                      if (n < 0.5f) col = mix3(0.04f, 0.18f, 0.50f, 0.12f, 0.42f, 0.78f, (n - 0.3f) * 5);
                      else col = mix3(0.16f, 0.50f, 0.20f, 0.60f, 0.52f, 0.30f, (n - 0.5f) * 4);
                      if (fabsf(v) > 1.9f) col = lerpc(col, C(240, 248, 255), 0.9f);
                      float cl = fbm(u * 3.0f + 5, v * 3.2f, 51, 4);
                      if (cl > 0.55f) col = lerpc(col, C(255, 255, 255), clampf((cl - 0.55f) * 4, 0, 0.85f));
                      break; }
            case 4: { float bnd = sinf(v * 7.0f + fbm(u * 1.3f, v * 2.5f, 61, 4) * 5.0f) * 0.5f + 0.5f;
                      col = mix3(0.28f, 0.16f, 0.55f, 0.95f, 0.62f, 0.90f, bnd); break; }
            default: { float n = fbm(u * 4.0f, v * 4.0f, 71, 5);
                      col = mix3(0.30f, 0.30f, 0.33f, 0.72f, 0.72f, 0.75f, n * 1.4f - 0.1f);
                      for (int k = 0; k < 7; k++) {
                          float cx = hash2(k, 0, 81) * 1.6f - 0.8f, cy = hash2(k, 1, 81) * 1.6f - 0.8f, cr = 0.08f + hash2(k, 2, 81) * 0.14f;
                          float dd = sqrtf((nx - cx) * (nx - cx) + (ny - cy) * (ny - cy));
                          if (dd < cr) col = lerpc(col, C(70, 70, 78), 0.55f * (1 - dd / cr) + 0.15f);
                          else if (dd < cr * 1.18f) col = lerpc(col, C(200, 200, 205), 0.3f);
                      }
                      break; }
            }
            float shade = 0.07f + 1.0f * powf(diff, 0.85f);
            float rim = powf(1 - nz, 3.0f) * fmaxf(0, diff + 0.2f) * 0.8f;
            float r = col.r * shade + 255 * rim * 0.55f, g = col.g * shade + 255 * rim * 0.65f, b = col.b * shade + 255 * rim;
            px[y * (s->pitch / 4) + x] = ((Uint32)(alpha * 255) << 24) | ((Uint32)clampf(r, 0, 255) << 16) |
                                         ((Uint32)clampf(g, 0, 255) << 8) | (Uint32)clampf(b, 0, 255);
        }
    SDL_Texture *t = SDL_CreateTextureFromSurface(ren, s);
    SDL_FreeSurface(s);
    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    return t;
}

static const Col ATMO[6] = {{255, 120, 70}, {120, 200, 255}, {255, 170, 90}, {90, 160, 255}, {190, 120, 255}, {170, 170, 190}};

static void make_vignette(void) {
    const int w = 240, h = 150;
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
    Uint32 *px = s->pixels;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            float dx = ((float)x + 0.5f) / w * 2 - 1, dy = ((float)y + 0.5f) / h * 2 - 1;
            float d = sqrtf(dx * dx * 0.8f + dy * dy * 0.9f);
            float a = clampf((d - 0.55f) / 0.85f, 0, 1);
            a = a * a * 0.75f;
            px[y * (s->pitch / 4) + x] = ((Uint32)(a * 255) << 24);
        }
    t_vig = SDL_CreateTextureFromSurface(ren, s);
    SDL_FreeSurface(s);
    SDL_SetTextureBlendMode(t_vig, SDL_BLENDMODE_BLEND);
}

static const Col NEB[6][3] = {
    {{70, 40, 140}, {20, 90, 160}, {10, 10, 30}},
    {{150, 40, 100}, {60, 40, 150}, {16, 8, 30}},
    {{20, 120, 130}, {40, 60, 160}, {6, 14, 30}},
    {{150, 80, 40}, {110, 30, 90}, {18, 8, 24}},
    {{90, 30, 150}, {150, 50, 110}, {10, 6, 28}},
    {{30, 130, 100}, {30, 70, 150}, {6, 16, 26}},
};

static void make_nebula(int idx, int seed) {
    const int w = 240, h = 150;
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
    Uint32 *px = s->pixels;
    const Col *pal = NEB[idx % 6];
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            float fx = (float)x / 38.0f, fy = (float)y / 38.0f;
            float a = fbm(fx + 1.7f * (float)seed, fy, 100 + seed, 5);
            float b = fbm(fx * 1.3f + 40, fy * 1.3f + 7 * (float)seed, 200 + seed, 5);
            float ka = clampf((a - 0.38f) * 2.6f, 0, 1), kb = clampf((b - 0.42f) * 2.4f, 0, 1);
            float r = pal[2].r + (pal[0].r * ka + pal[1].r * kb) * 0.55f;
            float g = pal[2].g + (pal[0].g * ka + pal[1].g * kb) * 0.55f;
            float bl = pal[2].b + (pal[0].b * ka + pal[1].b * kb) * 0.55f;
            px[y * (s->pitch / 4) + x] = 0xFF000000u | ((Uint32)clampf(r, 0, 255) << 16) | ((Uint32)clampf(g, 0, 255) << 8) | (Uint32)clampf(bl, 0, 255);
        }
    if (t_neb) SDL_DestroyTexture(t_neb);
    t_neb = SDL_CreateTextureFromSurface(ren, s);
    SDL_FreeSurface(s);
    SDL_SetTextureBlendMode(t_neb, SDL_BLENDMODE_NONE);
}

/* ---------------------------------------------------------------- draw helpers */
static void disc(V2 p, float r, Col c, float a) {
    SDL_SetTextureColorMod(t_disc, c.r, c.g, c.b);
    SDL_SetTextureAlphaMod(t_disc, (Uint8)(clampf(a, 0, 1) * 255));
    SDL_FRect d = {p.x - r, p.y - r, 2 * r, 2 * r};
    SDL_RenderCopyF(ren, t_disc, NULL, &d);
}
static void glow(V2 p, float r, Col c, float k) {
    SDL_SetTextureColorMod(t_glow, c.r, c.g, c.b);
    SDL_SetTextureAlphaMod(t_glow, (Uint8)(clampf(k, 0, 1) * 255));
    SDL_FRect d = {p.x - r, p.y - r, 2 * r, 2 * r};
    SDL_RenderCopyF(ren, t_glow, NULL, &d);
}
static void ring(V2 c, float r, Col col, float a) {
    SDL_FPoint pts[65];
    SDL_SetRenderDrawColor(ren, col.r, col.g, col.b, (Uint8)(clampf(a, 0, 1) * 255));
    for (int i = 0; i <= 64; i++) {
        float an = (float)i * 2 * PI / 64;
        pts[i].x = c.x + cosf(an) * r; pts[i].y = c.y + sinf(an) * r;
    }
    SDL_RenderDrawLinesF(ren, pts, 65);
}
static void ring_arc(V2 c, float rx, float ry, float rot, bool front, Col col, float a) {
    SDL_FPoint pts[41];
    float cs = cosf(rot), sn = sinf(rot), a0 = front ? 0 : PI;
    SDL_SetRenderDrawColor(ren, col.r, col.g, col.b, (Uint8)(clampf(a, 0, 1) * 255));
    for (int i = 0; i <= 40; i++) {
        float an = a0 + PI * (float)i / 40;
        float lx = cosf(an) * rx, ly = sinf(an) * ry;
        pts[i].x = c.x + lx * cs - ly * sn; pts[i].y = c.y + lx * sn + ly * cs;
    }
    SDL_RenderDrawLinesF(ren, pts, 41);
}
static void fill_tri(V2 a, V2 b, V2 c, Col col, float al) {
    SDL_Color k = {col.r, col.g, col.b, (Uint8)(clampf(al, 0, 1) * 255)};
    SDL_Vertex v[3] = {{{a.x, a.y}, k, {0, 0}}, {{b.x, b.y}, k, {0, 0}}, {{c.x, c.y}, k, {0, 0}}};
    SDL_RenderGeometry(ren, NULL, v, 3, NULL, 0);
}
static void thick_line(V2 a, V2 b, float w, Col c1, Col c2, float al) {
    V2 d = norm(sub(b, a)), n = v2(-d.y * w / 2, d.x * w / 2);
    SDL_Color k1 = {c1.r, c1.g, c1.b, (Uint8)(clampf(al, 0, 1) * 255)}, k2 = {c2.r, c2.g, c2.b, k1.a};
    SDL_Vertex v[4] = {{{a.x + n.x, a.y + n.y}, k1, {0, 0}}, {{a.x - n.x, a.y - n.y}, k1, {0, 0}},
                       {{b.x + n.x, b.y + n.y}, k2, {0, 0}}, {{b.x - n.x, b.y - n.y}, k2, {0, 0}}};
    int idx[6] = {0, 1, 2, 1, 2, 3};
    SDL_RenderGeometry(ren, NULL, v, 4, idx, 6);
}
static void frect(float x, float y, float w, float h, Col c, float a) {
    SDL_SetRenderDrawColor(ren, c.r, c.g, c.b, (Uint8)(clampf(a, 0, 1) * 255));
    SDL_FRect r = {x, y, w, h};
    SDL_RenderFillRectF(ren, &r);
}

/* ---------------------------------------------------------------- game state */
typedef enum { ST_TITLE, ST_PLAY, ST_PAUSE, ST_CARD, ST_OVER } State;
enum { M_AIM, M_FLY, M_SINK };
enum { PA_NONE, PA_START, PA_NEXT, PA_TITLE, PA_CARD };
typedef struct { V2 p, v; float life, max, size; Col c; } Part;
typedef struct { V2 p; float life; } Tr;
typedef struct { float x, y, ph, f; int layer; Col c; } Star;
typedef struct { int aim; bool slow; } Input;

static struct {
    State st;
    float stateT, clock, t, acc, fade, shake, bannerT, msgT, flyT, sinkT, bsnd;
    int hole, strokes, mode, pend, score[NHOLES];
    bool pickedUp[NHOLES], newBest, sunkDone;
    Ball ball;
    V2 lastPos;
    char msg[48];
    /* aiming */
    bool dragging, charging, kbAim;
    V2 anchor, mouse;
    float aimAng, chargeT, chPow;
    /* misc */
    int best;
    Part parts[MAX_PARTICLES]; int np;
    Tr trail[56]; int trh;
    Star stars[NSTARS];
    float fwT;
} G;

static bool no_save = false;
static char best_path[1100];

static void load_best(void) {
    char *pref = SDL_GetPrefPath("doomedl", "orbit-golf");
    if (!pref) return;
    snprintf(best_path, sizeof best_path, "%sbest", pref);
    SDL_free(pref);
    FILE *f = fopen(best_path, "r");
    if (f) { int v = 0; if (fscanf(f, "%d", &v) == 1 && v > 0 && v < 1000) G.best = v; fclose(f); }
}
static void save_best(void) {
    if (no_save || !best_path[0]) return;
    FILE *f = fopen(best_path, "w");
    if (f) { fprintf(f, "%d\n", G.best); fclose(f); }
}

static void burst(V2 p, int n, float spd, Col c, float size, float life) {
    for (int i = 0; i < n && G.np < MAX_PARTICLES; i++) {
        float a = frange(0, 2 * PI), s = spd * frange(0.2f, 1.0f);
        Part q = {p, v2(cosf(a) * s, sinf(a) * s), 0, frange(0.5f, 1.0f) * life, size * frange(0.6f, 1.2f), c};
        q.life = q.max;
        G.parts[G.np++] = q;
    }
}
static void spray(V2 p, V2 dir, int n, float spd, Col c, float size, float life) {
    for (int i = 0; i < n && G.np < MAX_PARTICLES; i++) {
        float a = atan2f(dir.y, dir.x) + frange(-0.7f, 0.7f), s = spd * frange(0.3f, 1.0f);
        Part q = {p, v2(cosf(a) * s, sinf(a) * s), 0, frange(0.5f, 1.0f) * life, size * frange(0.6f, 1.2f), c};
        q.life = q.max;
        G.parts[G.np++] = q;
    }
}

static void init_stars(void) {
    for (int i = 0; i < NSTARS; i++) {
        Star *s = &G.stars[i];
        s->layer = i < 150 ? 0 : i < 230 ? 1 : 2;
        s->x = frange(0, W); s->y = frange(0, H); s->ph = frange(0, 6.28f); s->f = frange(1.0f, 3.5f);
        float k = frand();
        s->c = k < 0.6f ? C(255, 255, 255) : k < 0.8f ? C(170, 200, 255) : C(255, 225, 170);
    }
}

static void load_hole(int i) {
    const Level *L = &lv[i];
    G.hole = i;
    G.ball = (Ball){0};
    G.ball.p = L->tee; G.ball.rest = true;
    G.lastPos = L->tee;
    G.strokes = 0; G.mode = M_AIM;
    G.aimAng = atan2f(L->hole.y - L->tee.y, L->hole.x - L->tee.x);
    G.dragging = G.charging = G.kbAim = false;
    G.bannerT = 0; G.msgT = 0; G.t = 0; G.acc = 0; G.flyT = 0; G.sunkDone = false;
    G.np = 0;
    memset(G.trail, 0, sizeof G.trail);
    make_nebula(L->neb, i + 1);
}

static int total_strokes(void) { int s = 0; for (int i = 0; i < NHOLES; i++) s += G.score[i]; return s; }
static int total_par(void) { int s = 0; for (int i = 0; i < NHOLES; i++) s += lv[i].par; return s; }

static void apply(int act) {
    G.stateT = 0;
    switch (act) {
    case PA_START:
        memset(G.score, 0, sizeof G.score); memset(G.pickedUp, 0, sizeof G.pickedUp);
        G.newBest = false;
        load_hole(0); G.st = ST_PLAY; break;
    case PA_NEXT: load_hole(G.hole + 1); G.st = ST_PLAY; break;
    case PA_TITLE: G.st = ST_TITLE; G.np = 0; break;
    case PA_CARD:
        G.np = 0; G.fwT = 0;
        if (G.hole == NHOLES - 1) {
            G.st = ST_OVER;
            int tot = total_strokes();
            if (G.best == 0 || tot < G.best) { G.best = tot; G.newBest = true; save_best(); }
            play(S_FANFARE, 0.6f);
        } else {
            G.st = ST_CARD;
            if (G.score[G.hole] < lv[G.hole].par) play(S_FANFARE, 0.5f);
        }
        break;
    }
}
static void go(int act) { if (!G.pend) G.pend = act; }

static const char *result_label(int strokes, int par, bool picked) {
    if (picked) return "PICKED UP";
    if (strokes == 1) return "HOLE IN ONE!";
    int d = strokes - par;
    if (d <= -3) return "ALBATROSS!";
    switch (d) {
    case -2: return "EAGLE!";
    case -1: return "BIRDIE!";
    case 0: return "PAR";
    case 1: return "BOGEY";
    case 2: return "DOUBLE BOGEY";
    default: return "OVER PAR";
    }
}

static void shoot(float ang, float pw) {
    G.ball.v = shot_vel(ang, pw);
    G.ball.rest = false; G.ball.still = 0; G.ball.wcool = 0;
    G.lastPos = G.ball.p;
    G.strokes++;
    G.mode = M_FLY; G.flyT = 0;
    G.dragging = G.charging = false;
    G.aimAng = ang;
    play(S_SHOT, 0.35f + 0.5f * pw);
    spray(G.ball.p, mul(G.ball.v, -1), 14, 120 + 160 * pw, C(180, 220, 255), 3.5f, 0.5f);
    G.shake = 2 + pw * 3;
}

static void finish_picked(void) {
    G.score[G.hole] = MAX_STROKES; G.pickedUp[G.hole] = true;
    snprintf(G.msg, sizeof G.msg, "PICKED UP"); G.msgT = 1.5f;
    G.sunkDone = true;
    go(PA_CARD);
}

static void on_events(void) {
    Ball *b = &G.ball;
    if (b->ev & EV_WORM) {
        play(S_WORM, 0.55f);
        burst(b->evp, 26, 150, C(120, 220, 255), 4, 0.6f);
        burst(b->evp2, 26, 150, C(255, 160, 255), 4, 0.6f);
    }
    if ((b->ev & EV_BOUNCE) && G.bsnd <= 0) {
        float k = clampf(b->impact / 420.0f, 0.15f, 1);
        play((b->ev & EV_CLACK) ? S_CLACK : S_BOUNCE, 0.3f + 0.6f * k);
        G.bsnd = 0.06f;
        burst(b->p, 4 + (int)(b->impact / 40), 60 + b->impact * 0.35f, (b->ev & EV_CLACK) ? C(220, 190, 150) : C(200, 230, 255), 3, 0.45f);
        if (b->impact > 260) G.shake = fmaxf(G.shake, (b->impact - 200) / 55.0f);
    }
    if (b->ev & EV_BH) {
        play(S_BH, 0.75f);
        burst(b->p, 40, 220, C(190, 100, 255), 4, 0.8f);
        G.shake = 7;
        G.strokes++;
        snprintf(G.msg, sizeof G.msg, "SWALLOWED! +1"); G.msgT = 1.6f;
        b->p = G.lastPos; b->v = v2(0, 0); b->rest = true; G.mode = M_AIM;
        memset(G.trail, 0, sizeof G.trail);
        if (G.strokes >= MAX_STROKES) finish_picked();
    } else if (b->ev & EV_HOLE) {
        G.mode = M_SINK; G.sinkT = 0;
        G.score[G.hole] = G.strokes; G.pickedUp[G.hole] = false;
        play(S_SINK, 0.7f);
        burst(lv[G.hole].hole, 70, 260, C(255, 230, 140), 4, 1.1f);
        burst(lv[G.hole].hole, 40, 160, C(140, 255, 220), 3, 1.0f);
        G.shake = 4;
    } else if (b->ev & EV_REST) {
        G.mode = M_AIM;
        if (G.strokes >= MAX_STROKES) finish_picked();
    }
}

static void step(float dt, Input in) {
    G.clock += dt;
    if (G.st == ST_PAUSE) return;
    G.stateT += dt;
    if (G.pend) {
        G.fade += dt * 3.6f;
        if (G.fade >= 1) { G.fade = 1; int a = G.pend; G.pend = PA_NONE; apply(a); }
    } else if (G.fade > 0) G.fade = fmaxf(0, G.fade - dt * 2.4f);
    G.shake *= expf(-dt * 9.0f);
    if (G.shake < 0.05f) G.shake = 0;
    G.msgT -= dt; G.bsnd -= dt;
    for (int i = 0; i < G.np; i++) {
        Part *q = &G.parts[i];
        q->life -= dt;
        if (q->life <= 0) { *q = G.parts[--G.np]; i--; continue; }
        q->p = add(q->p, mul(q->v, dt));
        q->v = mul(q->v, expf(-dt * 1.6f));
    }
    for (int i = 0; i < 56; i++) if (G.trail[i].life > 0) G.trail[i].life -= dt * 1.4f;

    if (G.st == ST_TITLE) {
        float a = G.clock * 0.8f;
        V2 p = v2(760 + cosf(a) * 215, 430 + sinf(a) * 78);
        if (fmodf(G.clock, 0.03f) < dt) { G.trail[G.trh] = (Tr){p, 1}; G.trh = (G.trh + 1) % 56; }
        return;
    }
    if (G.st == ST_CARD || G.st == ST_OVER) {
        const Level *L = &lv[G.hole];
        if (G.score[G.hole] < L->par || G.st == ST_OVER) {
            G.fwT -= dt;
            if (G.fwT <= 0 && (G.st == ST_OVER ? total_strokes() < total_par() : true)) {
                G.fwT = 0.45f;
                V2 p = v2(frange(150, 810), frange(60, 260));
                Col cc = frand() < 0.5f ? C(255, 220, 120) : frand() < 0.5f ? C(130, 220, 255) : C(255, 140, 220);
                burst(p, 36, 200, cc, 3.5f, 1.2f);
            }
        }
        return;
    }
    /* ST_PLAY */
    const Level *L = &lv[G.hole];
    G.bannerT += dt;
    if (G.mode == M_AIM) {
        if (in.aim) { G.aimAng += (float)in.aim * dt * (in.slow ? 0.3f : 1.2f); G.kbAim = true; }
        if (G.charging) {
            G.chargeT += dt;
            float p = fmodf(G.chargeT * 0.9f, 2.0f);
            G.chPow = 0.08f + 0.92f * (p < 1 ? p : 2 - p);
        }
    }
    G.acc += dt;
    int n = 0;
    while (G.acc >= STEP && n < 24) {
        G.acc -= STEP; n++;
        if (G.mode == M_FLY) {
            phys(L, &G.ball, G.t);
            G.flyT += STEP;
            if ((n & 3) == 0) { G.trail[G.trh] = (Tr){G.ball.p, 1}; G.trh = (G.trh + 1) % 56; }
            if (G.ball.ev) on_events();
            if (G.mode == M_FLY && G.flyT > 25.0f) { G.ball.rest = true; G.ball.v = v2(0, 0); G.ball.ev = EV_REST; on_events(); }
        }
        G.t += STEP;
    }
    if (G.acc > STEP) G.acc = 0;
    if (G.mode == M_SINK) {
        G.sinkT += dt;
        if (G.sinkT > 1.3f && !G.sunkDone) { G.sunkDone = true; go(PA_CARD); }
    }
    /* ambient: tiny sparks drifting off the cup */
    if (G.mode != M_SINK && frand() < dt * 9) {
        Part q = {add(L->hole, v2(frange(-12, 12), frange(-4, 8))), v2(frange(-8, 8), frange(-45, -20)), 0, 0.9f, 2.5f, C(150, 255, 220)};
        q.life = q.max;
        if (G.np < MAX_PARTICLES) G.parts[G.np++] = q;
    }
}

/* ---------------------------------------------------------------- rendering */
static void draw_stars(void) {
    static const float sp[3] = {3, 8, 17}, sz[3] = {1.2f, 1.7f, 2.4f}, al[3] = {0.45f, 0.7f, 0.95f};
    for (int i = 0; i < NSTARS; i++) {
        const Star *s = &G.stars[i];
        float x = fmodf(s->x - G.clock * sp[s->layer], W);
        if (x < 0) x += W;
        float tw = 0.62f + 0.38f * sinf(G.clock * s->f + s->ph);
        frect(x, s->y, sz[s->layer], sz[s->layer], s->c, al[s->layer] * tw);
        if (s->layer == 2) glow(v2(x, s->y), 8, s->c, 0.22f * tw);
    }
}

static void draw_rings(const Planet *p, bool front) {
    float ro = p->r * p->ring, ri = p->r * 1.35f;
    int n = (int)((ro - ri) / 0.8f);
    Col base = lerpc(ATMO[p->style], C(255, 255, 255), 0.45f);
    for (int k = 0; k < n; k++) {
        float t = (float)k / (float)(n - 1), rx = lerpf(ri, ro, t);
        float a = 0.12f + 0.32f * vnoise(t * 16, 0.5f, 5);
        if (t > 0.55f && t < 0.62f) a *= 0.12f;
        a *= clampf(t * 8, 0, 1) * clampf((1 - t) * 6, 0, 1) + 0.15f;
        ring_arc(v2(p->x, p->y), rx, rx * 0.3f, p->tilt, front, lerpc(base, C(150, 120, 100), t * 0.6f), a);
    }
}

static void draw_planet(const Planet *p) {
    V2 c = v2(p->x, p->y);
    glow(c, p->r * 1.55f, ATMO[p->style], 0.5f);
    if (p->ring > 0) draw_rings(p, false);
    SDL_FRect d = {p->x - p->r, p->y - p->r, 2 * p->r, 2 * p->r};
    SDL_SetTextureColorMod(t_planet[p->style], 255, 255, 255);
    SDL_SetTextureAlphaMod(t_planet[p->style], 255);
    SDL_RenderCopyF(ren, t_planet[p->style], NULL, &d);
    glow(c, p->r * 1.12f, ATMO[p->style], 0.12f);
    if (p->ring > 0) draw_rings(p, true);
}

static void draw_blackhole(V2 c) {
    glow(c, 75, C(120, 40, 200), 0.55f);
    glow(c, 40, C(255, 120, 60), 0.35f);
    for (int arm = 0; arm < 3; arm++)
        for (int i = 0; i < 26; i++) {
            float r = 15 + (float)i * 0.95f;
            float a = (float)arm * 2 * PI / 3 + r * 0.17f - G.clock * (60.0f / (r + 8));
            V2 p = v2(c.x + cosf(a) * r, c.y + sinf(a) * r);
            float k = 1 - (float)i / 26;
            glow(p, 4 + k * 3, lerpc(C(255, 120, 60), C(255, 240, 200), k), 0.35f * k + 0.1f);
        }
    disc(c, 14.5f, C(0, 0, 0), 1);
    ring(c, 14.5f, C(255, 200, 150), 0.9f);
    ring(c, 15.5f, C(255, 130, 70), 0.5f);
}

static void draw_worm(V2 c, Col col, float dir) {
    glow(c, 46, col, 0.55f);
    for (int arm = 0; arm < 4; arm++)
        for (int i = 0; i < 12; i++) {
            float r = 18 - (float)i * 1.2f;
            float a = (float)arm * PI / 2 + (float)i * 0.28f + G.clock * 3.0f * dir;
            glow(v2(c.x + cosf(a) * r, c.y + sinf(a) * r), 5, lerpc(col, C(255, 255, 255), (float)i / 14), 0.5f);
        }
    disc(c, 7, lerpc(col, C(0, 0, 20), 0.85f), 1);
    ring(c, 19, col, 0.9f);
    ring(c, 20, C(255, 255, 255), 0.35f);
}

static void draw_belt(const Belt *b, int seed) {
    V2 a = v2(b->x1, b->y1), d = sub(v2(b->x2, b->y2), a);
    float l = len(d);
    V2 u = norm(d), nrm = v2(-u.y, u.x);
    int n = (int)(l / 10.0f);
    for (int k = 0; k <= n; k++) {
        float t = n ? (float)k / (float)n : 0;
        float h1 = hash2(k, seed, 7), h2 = hash2(k, seed, 8), h3 = hash2(k, seed, 9);
        V2 p = add(add(a, mul(d, t)), mul(nrm, (h1 - 0.5f) * 7));
        float r = 5.5f + h2 * 4.0f;
        Col base = lerpc(C(120, 100, 85), C(165, 150, 135), h3);
        disc(add(p, v2(1.5f, 2)), r, C(10, 8, 14), 0.5f);
        disc(p, r, lerpc(base, C(30, 26, 30), 0.55f), 1);
        disc(add(p, v2(-r * 0.15f, -r * 0.18f)), r * 0.82f, base, 1);
        disc(add(p, v2(-r * 0.3f, -r * 0.35f)), r * 0.4f, lerpc(base, C(255, 245, 230), 0.4f), 0.8f);
        disc(add(p, v2(r * 0.25f, r * 0.2f)), r * 0.22f, C(50, 42, 40), 0.7f);
    }
}

static void draw_hole(V2 h) {
    float pu = 0.5f + 0.5f * sinf(G.clock * 3.0f);
    glow(h, 44 + 5 * pu, C(70, 255, 200), 0.5f);
    disc(h, 14, C(3, 10, 22), 1);
    ring(h, 14, C(170, 255, 230), 0.95f);
    ring(h, 15.5f + 2 * pu, C(100, 255, 210), 0.4f + 0.3f * pu);
    ring(h, 9, C(60, 200, 170), 0.55f);
    ring(h, 4.5f, C(60, 200, 170), 0.4f);
}
static void draw_flag(V2 h) {
    float w = sinf(G.clock * 4.0f);
    thick_line(add(h, v2(0, 2)), add(h, v2(0, -38)), 2, C(230, 235, 245), C(255, 255, 255), 1);
    fill_tri(add(h, v2(1, -38)), add(h, v2(21 + 2 * w, -31 + w)), add(h, v2(1, -24)), C(255, 90, 80), 0.95f);
    fill_tri(add(h, v2(1, -38)), add(h, v2(12 + w, -34)), add(h, v2(1, -31)), C(255, 170, 150), 0.7f);
    disc(add(h, v2(0, -38)), 2.2f, C(255, 235, 150), 1);
}

static void draw_ball(V2 p, float sc) {
    if (sc <= 0) return;
    glow(p, 22 * sc, C(140, 190, 255), 0.5f);
    disc(p, (BR + 0.8f) * sc, C(110, 140, 200), 1);
    disc(p, BR * sc, C(235, 244, 255), 1);
    disc(v2(p.x - 1.6f * sc, p.y - 1.9f * sc), 2.5f * sc, C(255, 255, 255), 0.95f);
}

static void draw_world(const Level *L, bool live) {
    float t = live ? G.t : G.clock;
    /* gravity ripples */
    for (int i = 0; i < L->np; i++) {
        const Planet *p = &L->p[i];
        for (int j = 0; j < 2; j++) {
            float ph = fmodf(G.clock * 0.25f + (float)j * 0.5f + (float)i * 0.13f, 1.0f);
            ring(v2(p->x, p->y), p->r * (3.0f - 1.9f * ph), lerpc(ATMO[p->style], C(255, 255, 255), 0.4f), sinf(PI * ph) * 0.13f);
        }
    }
    for (int i = 0; i < L->nbt; i++) draw_belt(&L->bt[i], i + 3 * G.hole);
    for (int i = 0; i < L->nbh; i++) draw_blackhole(v2(L->bh[i].x, L->bh[i].y));
    for (int i = 0; i < L->nwm; i++) {
        Col c = i == 0 ? C(90, 200, 255) : C(255, 140, 230);
        draw_worm(v2(L->wm[i].ax, L->wm[i].ay), c, 1);
        draw_worm(v2(L->wm[i].bx, L->wm[i].by), c, -1);
    }
    for (int i = 0; i < L->nmo; i++) {
        const Moon *m = &L->mo[i];
        V2 c = m->pl >= 0 ? v2(L->p[m->pl].x, L->p[m->pl].y) : v2(m->cx, m->cy);
        ring(c, m->orbit, C(150, 170, 220), 0.12f);
    }
    draw_hole(L->hole);
    for (int i = 0; i < L->np; i++) draw_planet(&L->p[i]);
    for (int i = 0; i < L->nmo; i++) {
        Planet mp = {0};
        V2 p = moon_pos(L, i, t);
        mp.x = p.x; mp.y = p.y; mp.r = L->mo[i].r; mp.style = 5;
        draw_planet(&mp);
    }
    /* tee marker */
    if (live) { ring(L->tee, 9, C(255, 255, 255), 0.22f); ring(L->tee, 12, C(255, 255, 255), 0.1f); }
}

static void draw_aim(const Level *L) {
    if (G.mode != M_AIM || G.st != ST_PLAY) return;
    V2 bp = G.ball.p;
    float ang, pw;
    if (G.dragging) {
        V2 pull = sub(G.anchor, G.mouse);
        float l = len(pull);
        ring(bp, 14 + 2 * sinf(G.clock * 8), C(255, 255, 255), 0.3f);
        if (l < 14) return;
        ang = atan2f(pull.y, pull.x); pw = clampf(l / 150.0f, 0, 1);
        V2 back = add(bp, mul(norm(pull), -pw * 62));
        Col hot = lerpc(C(90, 255, 170), C(255, 80, 70), pw);
        thick_line(bp, back, 4, hot, lerpc(hot, C(255, 255, 255), 0.4f), 0.85f);
        disc(back, 5, hot, 1);
    } else if (G.kbAim) {
        ang = G.aimAng; pw = G.charging ? G.chPow : 0.55f;
    } else {
        ring(bp, 13 + 3 * sinf(G.clock * 4), C(255, 255, 255), 0.35f);
        return;
    }
    V2 pts[80];
    int n = preview(L, bp, shot_vel(ang, pw), G.t, pts, 80);
    for (int i = 0; i < n; i++) {
        float k = 1 - (float)i / 80;
        glow(pts[i], 7, C(150, 220, 255), 0.18f * k + 0.04f);
        disc(pts[i], 2.0f, C(220, 245, 255), 0.25f + 0.7f * k);
    }
    if (G.kbAim && !G.dragging) {
        V2 d = v2(cosf(ang), sinf(ang));
        V2 tip = add(bp, mul(d, 20 + pw * 26));
        thick_line(add(bp, mul(d, 12)), tip, 3, C(255, 255, 255), lerpc(C(90, 255, 170), C(255, 80, 70), pw), 0.9f);
        fill_tri(add(tip, mul(d, 7)), add(tip, v2(-d.y * 5, d.x * 5)), add(tip, v2(d.y * 5, -d.x * 5)), lerpc(C(90, 255, 170), C(255, 80, 70), pw), 0.95f);
    }
}

static void fmt_diff(char *buf, size_t n, int d) {
    if (d > 0) snprintf(buf, n, "+%d", d);
    else if (d < 0) snprintf(buf, n, "-%d", -d);
    else snprintf(buf, n, "E");
}
static Col score_col(int s, int par) { return s < par ? C(120, 255, 160) : s == par ? C(235, 240, 255) : C(255, 170, 100); }

static void draw_card(bool final) {
    frect(0, 0, W, H, C(0, 0, 14), 0.6f);
    frect(110, 40, 740, 520, C(8, 12, 36), 0.9f);
    frect(110, 40, 740, 3, C(120, 170, 255), 0.9f);
    frect(110, 557, 740, 3, C(120, 170, 255), 0.9f);
    frect(110, 40, 3, 520, C(60, 90, 180), 0.7f);
    frect(847, 40, 3, 520, C(60, 90, 180), 0.7f);
    char buf[96], d1[16], d2[16];
    int tot = total_strokes(), par = total_par();
    int played_par = 0, played = 0;
    for (int i = 0; i < NHOLES; i++) if (G.score[i]) { played += G.score[i]; played_par += lv[i].par; }
    float pop = clampf(G.stateT * 4, 0, 1);
    Col lab1, lab2;
    const char *label;
    if (!final) {
        int s = G.score[G.hole], p = lv[G.hole].par;
        snprintf(buf, sizeof buf, "HOLE %d COMPLETE", G.hole + 1);
        text(buf, W / 2, 62, 3, C(140, 200, 255), 1);
        text(lv[G.hole].name, W / 2, 96, 2, C(130, 150, 200), 1);
        label = result_label(s, p, G.pickedUp[G.hole]);
        if (s < p) { lab1 = C(255, 245, 160); lab2 = C(255, 160, 50); }
        else if (s == p) { lab1 = C(220, 240, 255); lab2 = C(110, 170, 255); }
        else { lab1 = C(255, 200, 120); lab2 = C(255, 80, 70); }
        snprintf(buf, sizeof buf, "STROKES %d    PAR %d", s, p);
    } else {
        text("ROUND COMPLETE", W / 2, 62, 3, C(140, 200, 255), 1);
        text("ORBIT GOLF - 12 HOLES", W / 2, 96, 2, C(130, 150, 200), 1);
        int d = tot - par;
        label = d <= -8 ? "COSMIC LEGEND" : d <= -3 ? "STAR PLAYER" : d <= 0 ? "ORBIT MASTER" : d <= 8 ? "SPACE CADET" : "KEEP PRACTICING";
        if (d <= 0) { lab1 = C(255, 245, 160); lab2 = C(255, 160, 50); }
        else { lab1 = C(220, 240, 255); lab2 = C(110, 170, 255); }
        fmt_diff(d1, sizeof d1, d);
        snprintf(buf, sizeof buf, "TOTAL %d    PAR %d    %s", tot, par, d1);
    }
    {
        float sc = 7 * (0.6f + 0.4f * pop);
        if (text_w(label, sc) > 700) sc = 700 / (text_w(label, 1) );
        textg(label, W / 2, 132 + (7 - sc) * 3.5f, sc, lab1, lab2, pop);
    }
    text(buf, W / 2, 208, 3, C(235, 240, 255), 1);
    /* grid */
    float gx = 480 - 3 * 104.0f;
    for (int i = 0; i < NHOLES; i++) {
        float x = gx + (float)(i % 6) * 104 + 4, y = 262 + (float)(i / 6) * 90;
        bool cur = !final && i == G.hole;
        frect(x, y, 96, 82, cur ? C(40, 70, 140) : C(18, 26, 60), cur ? 0.95f : 0.8f);
        frect(x, y, 96, 2, cur ? C(150, 200, 255) : C(60, 80, 150), 0.9f);
        snprintf(d1, sizeof d1, "HOLE %d", i + 1);
        text(d1, x + 48, y + 8, 1.7f, C(150, 170, 220), 1);
        if (G.score[i]) {
            snprintf(d1, sizeof d1, "%d", G.score[i]);
            text(d1, x + 48, y + 26, 5, score_col(G.score[i], lv[i].par), 1);
        } else text("-", x + 48, y + 30, 4, C(70, 85, 130), 1);
        snprintf(d1, sizeof d1, "PAR %d", lv[i].par);
        text(d1, x + 48, y + 68, 1.5f, C(120, 140, 190), 1);
    }
    if (!final) {
        fmt_diff(d2, sizeof d2, played - played_par);
        snprintf(buf, sizeof buf, "TOTAL %d    PAR %d    %s", played, played_par, d2);
        text(buf, W / 2, 462, 2.6f, C(235, 240, 255), 1);
    } else {
        if (G.newBest) {
            float k = 0.6f + 0.4f * sinf(G.clock * 6);
            text("NEW BEST ROUND!", W / 2, 460, 3, C(255, 235, 130), k);
        } else {
            snprintf(buf, sizeof buf, "BEST ROUND %d", G.best);
            text(buf, W / 2, 462, 3, C(235, 240, 255), 1);
        }
    }
    float bl = 0.55f + 0.45f * sinf(G.clock * 4);
    text(final ? "CLICK OR SPACE: PLAY AGAIN    ESC: TITLE" : "CLICK OR PRESS SPACE FOR THE NEXT HOLE", W / 2, 516, 2, C(160, 220, 255), bl);
}

static void render(void) {
    State st = G.st;
    const Level *L = &lv[G.hole];
    SDL_SetRenderTarget(ren, t_canvas);
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
    SDL_RenderClear(ren);
    SDL_RenderCopy(ren, t_neb, NULL, NULL);
    draw_stars();

    if (st == ST_TITLE) {
        Planet big = {760, 430, 100, 1, 1.85f, -0.28f, 2}, ice = {140, 500, 46, 1, 0, 0, 1}, red = {95, 190, 30, 1, 0, 0, 0};
        float a = G.clock * 0.8f;
        V2 bp = v2(760 + cosf(a) * 215, 430 + sinf(a) * 78);
        for (int i = 0; i < 56; i++)
            if (G.trail[i].life > 0 && G.trail[i].p.y < 430) glow(G.trail[i].p, 3 + G.trail[i].life * 6, C(120, 190, 255), G.trail[i].life * 0.55f);
        if (sinf(a) < 0) draw_ball(bp, 1);
        draw_planet(&big); draw_planet(&ice); draw_planet(&red);
        if (sinf(a) >= 0) draw_ball(bp, 1);
        for (int i = 0; i < 56; i++)
            if (G.trail[i].life > 0 && G.trail[i].p.y >= 430) glow(G.trail[i].p, 3 + G.trail[i].life * 6, C(120, 190, 255), G.trail[i].life * 0.55f);
    } else {
        draw_world(L, st == ST_PLAY || st == ST_PAUSE);
        for (int i = 0; i < 56; i++)
            if (G.trail[i].life > 0) glow(G.trail[i].p, 3 + G.trail[i].life * 6, C(120, 190, 255), G.trail[i].life * 0.55f);
        if (st == ST_PLAY || st == ST_PAUSE) {
            if (G.mode == M_SINK) {
                float e = clampf(G.sinkT * 4, 0, 1);
                draw_ball(lerp_v2(G.ball.p, L->hole, e), clampf(1 - G.sinkT * 1.6f, 0, 1));
            } else draw_ball(G.ball.p, 1);
            draw_aim(L);
        }
        draw_flag(L->hole);
    }
    for (int i = 0; i < G.np; i++) {
        const Part *q = &G.parts[i];
        float k = q->life / q->max;
        glow(q->p, q->size * (1.5f + k * 2.0f), q->c, k);
    }

    SDL_SetRenderTarget(ren, NULL);
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
    SDL_RenderClear(ren);
    SDL_FRect dst = {0, 0, W, H};
    if (G.shake > 0.1f) { dst.x = frange(-1, 1) * G.shake; dst.y = frange(-1, 1) * G.shake; }
    SDL_RenderCopyF(ren, t_canvas, NULL, &dst);
    SDL_SetTextureAlphaMod(t_vig, 255);
    SDL_RenderCopy(ren, t_vig, NULL, NULL);

    if (st == ST_TITLE) {
        float bob = sinf(G.clock * 1.5f) * 4;
        glow(v2(W / 2, 150 + bob), 330, C(60, 90, 255), 0.35f);
        textg("ORBIT GOLF", W / 2, 105 + bob, 12, C(200, 245, 255), C(70, 140, 255), 1);
        text("GRAVITY MINI-GOLF IN SPACE", W / 2, 215, 3, C(190, 200, 255), 1);
        float bl = 0.72f + 0.28f * sinf(G.clock * 4);
        text("CLICK OR PRESS SPACE TO PLAY", W / 2, 300, 2.6f, C(255, 235, 150), bl);
        text("DRAG BACK AND RELEASE TO SLING THE BALL", W / 2, 345, 1.8f, C(150, 170, 220), 1);
        text("OR AIM WITH ARROWS AND HOLD SPACE FOR POWER", W / 2, 370, 1.8f, C(150, 170, 220), 1);
        char buf[64];
        if (G.best) snprintf(buf, sizeof buf, "BEST ROUND: %d STROKES", G.best);
        else snprintf(buf, sizeof buf, "12 HOLES - BEND YOUR SHOTS AROUND PLANETS");
        text(buf, W / 2, 548, 2, C(130, 220, 190), 1);
    } else if (st == ST_PLAY || st == ST_PAUSE) {
        frect(0, 0, W, 38, C(0, 0, 14), 0.55f);
        char buf[96];
        snprintf(buf, sizeof buf, "HOLE %d/%d", G.hole + 1, NHOLES);
        text_l(buf, 16, 12, 2, C(150, 200, 255), 1);
        text(L->name, W / 2, 12, 2, C(235, 240, 255), 1);
        snprintf(buf, sizeof buf, "PAR %d   STROKES %d", L->par, G.strokes);
        text_l(buf, W - 16 - text_w(buf, 2), 12, 2, C(255, 235, 150), 1);
        if (G.mode == M_AIM && (G.charging || G.dragging)) {
            float pw = G.charging ? G.chPow : clampf(len(sub(G.anchor, G.mouse)) / 150.0f, 0, 1);
            Col hot = lerpc(C(90, 255, 170), C(255, 80, 70), pw);
            frect(W / 2 - 111, H - 33, 222, 16, C(0, 0, 14), 0.7f);
            frect(W / 2 - 108, H - 30, 216 * pw, 10, hot, 1);
            text("POWER", W / 2, H - 52, 1.6f, C(200, 220, 255), 0.9f);
        }
        if (G.msgT > 0) text(G.msg, W / 2, 96, 4, C(255, 150, 120), clampf(G.msgT * 2, 0, 1));
        if (G.hole < 2 && G.strokes == 0 && G.mode == M_AIM && !G.dragging && !G.charging && G.bannerT > 1.8f) {
            float bl = 0.55f + 0.45f * sinf(G.clock * 4);
            text("DRAG BACK AND RELEASE TO SHOOT", W / 2, H - 50, 2.4f, C(255, 255, 255), bl);
            text("OR ARROWS TO AIM + HOLD SPACE", W / 2, H - 24, 1.6f, C(160, 190, 230), bl);
        }
        if (st == ST_PLAY && G.bannerT < 2.4f) {
            float al = clampf(fminf(G.bannerT / 0.4f, (2.4f - G.bannerT) / 0.5f), 0, 1);
            float sl = (1 - clampf(G.bannerT / 0.5f, 0, 1)) * 60;
            char b2[48];
            snprintf(b2, sizeof b2, "HOLE %d", G.hole + 1);
            frect(0, 150, W, 120, C(0, 0, 18), 0.5f * al);
            textg(b2, W / 2 - sl, 160, 6, C(220, 245, 255), C(100, 160, 255), al);
            text(L->name, W / 2 + sl, 214, 3, C(255, 235, 150), al);
            snprintf(b2, sizeof b2, "PAR %d", L->par);
            text(b2, W / 2, 245, 2, C(170, 190, 230), al);
        }
        if (st == ST_PAUSE) {
            frect(0, 0, W, H, C(0, 0, 14), 0.6f);
            textg("PAUSED", W / 2, 230, 9, C(220, 245, 255), C(100, 160, 255), 1);
            text("ESC: RESUME     Q: QUIT TO TITLE", W / 2, 330, 2.2f, C(200, 215, 255), 1);
        }
    } else if (st == ST_CARD) draw_card(false);
    else if (st == ST_OVER) draw_card(true);

    if (G.fade > 0) frect(0, 0, W, H, C(0, 0, 6), G.fade);
}

/* ---------------------------------------------------------------- autopilot (used by --selftest) */
static float eval_shot(const Level *L, V2 p, V2 v, float t0) {
    Ball b = {0};
    b.p = p; b.v = v;
    float t = t0;
    for (int s = 0; s < 3600; s++, t += STEP) {
        phys(L, &b, t);
        if (b.ev & EV_HOLE) return 100000.0f - (float)s;
        if (b.ev & EV_BH) return -2000.0f;
        if (b.rest) break;
    }
    return -len(sub(b.p, L->hole));
}

static void autopilot_pick(float *ang_out, float *pw_out) {
    const Level *L = &lv[G.hole];
    typedef struct { float sc, ang, pw; } Cand;
    Cand top[8];
    int nt = 0;
    static const float pows[] = {0.12f, 0.2f, 0.3f, 0.4f, 0.5f, 0.62f, 0.75f, 0.88f, 1.0f};
    for (int ai = 0; ai < 240; ai++)
        for (size_t pi = 0; pi < sizeof pows / sizeof *pows; pi++) {
            float ang = (float)ai * (2 * PI / 240), pw = pows[pi];
            float sc = eval_shot(L, G.ball.p, shot_vel(ang, pw), G.t);
            if (nt < 8 || sc > top[nt - 1].sc) {
                int k = nt < 8 ? nt++ : 7;
                while (k > 0 && top[k - 1].sc < sc) { top[k] = top[k - 1]; k--; }
                top[k] = (Cand){sc, ang, pw};
            }
        }
    Cand best = top[0];
    for (int da = -14; da <= 14; da++)
        for (int dp = -3; dp <= 3; dp++) {
            float ang = best.ang + (float)da * 0.1f * PI / 180, pw = clampf(best.pw + (float)dp * 0.03f, 0.05f, 1);
            float sc = eval_shot(L, G.ball.p, shot_vel(ang, pw), G.t);
            if (sc > best.sc) best = (Cand){sc, ang, pw};
        }
    if (best.sc < 50000 && G.strokes >= 2 && G.strokes % 2 == 0) best = top[xr() % (Uint32)nt];
    *ang_out = best.ang; *pw_out = best.pw;
}

static void shot_bmp(const char *dir, const char *name) {
    char path[1200];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    render();
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, W, H, 32, SDL_PIXELFORMAT_ARGB8888);
    SDL_RenderReadPixels(ren, NULL, SDL_PIXELFORMAT_ARGB8888, s->pixels, s->pitch);
    SDL_SaveBMP(s, path);
    SDL_FreeSurface(s);
}

static int selftest(const char *dir) {
    for (int i = 0; i < 150; i++) step(1 / 60.0f, (Input){0});
    shot_bmp(dir, "title.bmp");
    go(PA_START);
    bool tookPlay = false, tookW = false, tookAim = false, tookCard = false, over = false;
    long frames = 0;
    while (frames < 60L * 60 * 40 && !over) {
        frames++;
        step(1 / 60.0f, (Input){0});
        if (G.st == ST_PLAY && !G.pend && G.fade < 0.3f && G.mode == M_AIM && G.bannerT > 2.5f) {
            float ang, pw;
            autopilot_pick(&ang, &pw);
            ang += frange(-0.04f, 0.04f); pw = clampf(pw * frange(0.94f, 1.06f), 0.05f, 1);   /* human-ish error */
            if (G.hole == 3 && G.strokes == 0 && !tookAim) {
                G.kbAim = true; G.aimAng = ang; G.charging = true; G.chPow = pw;
                shot_bmp(dir, "aim.bmp"); tookAim = true;
                G.charging = false;
            }
            shoot(ang, pw);
        }
        if (G.st == ST_PLAY && G.mode == M_FLY) {
            if (G.hole == 7 && !tookPlay && G.flyT > 0.9f) { shot_bmp(dir, "play.bmp"); tookPlay = true; }
            if (G.hole == 5 && !tookW && G.flyT > 0.45f) { shot_bmp(dir, "play2.bmp"); tookW = true; }
        }
        if (G.st == ST_CARD && G.stateT > 1.2f) {
            if (!tookCard) { shot_bmp(dir, "card.bmp"); tookCard = true; }
            go(PA_NEXT);
        }
        if (G.st == ST_OVER && G.stateT > 1.8f) { shot_bmp(dir, "over.bmp"); over = true; }
    }
    if (!tookPlay) shot_bmp(dir, "play.bmp");
    printf("selftest: %s after %ld frames\n  holes:", over ? "finished round" : "DID NOT FINISH", frames);
    for (int i = 0; i < NHOLES; i++) printf(" %d/%d", G.score[i], lv[i].par);
    printf("\n  total %d par %d\n", total_strokes(), total_par());
    return over ? 0 : 1;
}

/* ---------------------------------------------------------------- main */
static void begin_shot_charge(void) {
    if (G.st == ST_PLAY && G.mode == M_AIM && !G.dragging && !G.charging && G.fade < 0.5f) {
        G.charging = true; G.chargeT = 0; G.chPow = 0.08f; G.kbAim = true;
    }
}
static void release_charge(void) {
    if (G.charging && G.st == ST_PLAY && G.mode == M_AIM) shoot(G.aimAng, G.chPow);
    G.charging = false;
}
static void advance_or_start(void) {
    if (G.st == ST_TITLE) { play(S_UI, 0.6f); go(PA_START); }
    else if (G.st == ST_CARD && G.stateT > 0.5f) { play(S_UI, 0.6f); go(PA_NEXT); }
    else if (G.st == ST_OVER && G.stateT > 0.9f) { play(S_UI, 0.6f); go(PA_START); }
}

int main(int argc, char **argv) {
    const char *testdir = (argc == 3 && strcmp(argv[1], "--selftest") == 0) ? argv[2] : NULL;
    rng ^= (Uint32)SDL_GetPerformanceCounter() | 1u;
    if (testdir) rng = 0x1234567u;

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
    SDL_Window *win = SDL_CreateWindow("Orbit Golf", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, W, H,
                                       testdir ? SDL_WINDOW_HIDDEN : (SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI));
    if (!win) { fprintf(stderr, "window: %s\n", SDL_GetError()); return 1; }
    SDL_SetWindowMinimumSize(win, W / 2, H / 2);
    ren = SDL_CreateRenderer(win, -1, testdir ? SDL_RENDERER_SOFTWARE | SDL_RENDERER_TARGETTEXTURE
                                              : SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_TARGETTEXTURE);
    if (!ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE | SDL_RENDERER_TARGETTEXTURE);
    if (!ren) { fprintf(stderr, "renderer: %s\n", SDL_GetError()); return 1; }
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    SDL_RenderSetLogicalSize(ren, W, H);

    t_glow = make_tex(256, true);
    t_disc = make_tex(64, false);
    for (int i = 0; i < 6; i++) t_planet[i] = make_planet(i);
    make_vignette();
    t_canvas = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, W, H);
    SDL_SetTextureBlendMode(t_canvas, SDL_BLENDMODE_NONE);
    build_levels();
    init_stars();
    make_nebula(1, 7);

    if (!testdir) load_best();
    G.st = ST_TITLE;
    G.hole = 0;
    if (testdir) { no_save = true; G.best = 0; int r = selftest(testdir); SDL_Quit(); return r; }

    SDL_GameController *pad = NULL;
    bool running = true;
    Uint64 prev = SDL_GetPerformanceCounter();
    while (running) {
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
                }
                break;
            case SDL_WINDOWEVENT:
                if (e.window.event == SDL_WINDOWEVENT_FOCUS_LOST && G.st == ST_PLAY) { G.st = ST_PAUSE; G.dragging = G.charging = false; }
                break;
            case SDL_MOUSEBUTTONDOWN:
                G.mouse = v2((float)e.button.x, (float)e.button.y);
                if (e.button.button == SDL_BUTTON_LEFT) {
                    if (G.st == ST_PLAY && G.mode == M_AIM && !G.charging && G.fade < 0.5f) {
                        G.dragging = true; G.anchor = G.mouse; G.kbAim = false;
                    } else advance_or_start();
                } else if (e.button.button == SDL_BUTTON_RIGHT) G.dragging = false;
                break;
            case SDL_MOUSEMOTION:
                G.mouse = v2((float)e.motion.x, (float)e.motion.y);
                break;
            case SDL_MOUSEBUTTONUP:
                if (e.button.button == SDL_BUTTON_LEFT && G.dragging) {
                    V2 pull = sub(G.anchor, v2((float)e.button.x, (float)e.button.y));
                    float l = len(pull);
                    G.dragging = false;
                    if (l >= 14 && G.st == ST_PLAY && G.mode == M_AIM) shoot(atan2f(pull.y, pull.x), clampf(l / 150.0f, 0, 1));
                }
                break;
            case SDL_KEYUP:
                if (e.key.keysym.sym == SDLK_SPACE) release_charge();
                break;
            case SDL_KEYDOWN: {
                if (e.key.repeat) break;
                SDL_Keycode k = e.key.keysym.sym;
                bool enter = k == SDLK_RETURN || k == SDLK_KP_ENTER || k == SDLK_SPACE;
                if (k == SDLK_F11) {
                    bool fs = SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN_DESKTOP;
                    SDL_SetWindowFullscreen(win, fs ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
                } else if (G.st == ST_TITLE) {
                    if (enter) advance_or_start();
                    else if (k == SDLK_ESCAPE) running = false;
                } else if (G.st == ST_PLAY) {
                    if (k == SDLK_ESCAPE || k == SDLK_p) {
                        if (G.dragging || G.charging) G.dragging = G.charging = false;
                        else G.st = ST_PAUSE;
                    } else if (k == SDLK_SPACE) begin_shot_charge();
                    else if (k == SDLK_LEFT || k == SDLK_RIGHT || k == SDLK_a || k == SDLK_d) G.kbAim = true;
                } else if (G.st == ST_PAUSE) {
                    if (k == SDLK_ESCAPE || k == SDLK_p) G.st = ST_PLAY;
                    else if (k == SDLK_q) { G.st = ST_PLAY; go(PA_TITLE); }
                } else if (G.st == ST_CARD || G.st == ST_OVER) {
                    if (enter) advance_or_start();
                    else if (k == SDLK_ESCAPE) go(PA_TITLE);
                }
                break;
            }
            case SDL_CONTROLLERBUTTONDOWN: {
                int b = e.cbutton.button;
                if (G.st == ST_PLAY) {
                    if (b == SDL_CONTROLLER_BUTTON_START) G.st = ST_PAUSE;
                    else if (b == SDL_CONTROLLER_BUTTON_A) begin_shot_charge();
                    else if (b == SDL_CONTROLLER_BUTTON_B) G.charging = false;
                } else if (G.st == ST_PAUSE) {
                    if (b == SDL_CONTROLLER_BUTTON_START) G.st = ST_PLAY;
                    else if (b == SDL_CONTROLLER_BUTTON_BACK) { G.st = ST_PLAY; go(PA_TITLE); }
                } else if (b == SDL_CONTROLLER_BUTTON_START || b == SDL_CONTROLLER_BUTTON_A) advance_or_start();
                break;
            }
            case SDL_CONTROLLERBUTTONUP:
                if (e.cbutton.button == SDL_CONTROLLER_BUTTON_A) release_charge();
                break;
            }
        }
        Input in = {0};
        const Uint8 *ks = SDL_GetKeyboardState(NULL);
        in.aim = (ks[SDL_SCANCODE_RIGHT] || ks[SDL_SCANCODE_D]) - (ks[SDL_SCANCODE_LEFT] || ks[SDL_SCANCODE_A]);
        in.slow = ks[SDL_SCANCODE_LSHIFT] || ks[SDL_SCANCODE_RSHIFT];
        if (pad) {
            float ax = (float)SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTX) / 32767.0f;
            if (fabsf(ax) > 0.2f) { in.aim += ax > 0 ? 1 : -1; in.slow = in.slow || fabsf(ax) < 0.6f; }
            in.aim += SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_RIGHT) - SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_LEFT);
            if (in.aim > 1) in.aim = 1;
            if (in.aim < -1) in.aim = -1;
        }

        Uint64 now = SDL_GetPerformanceCounter();
        float dt = (float)(now - prev) / (float)SDL_GetPerformanceFrequency();
        prev = now;
        if (dt > 1 / 30.0f) dt = 1 / 30.0f;
        step(dt, in);
        render();
        SDL_RenderPresent(ren);
    }

    if (pad) SDL_GameControllerClose(pad);
    if (adev) SDL_CloseAudioDevice(adev);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
