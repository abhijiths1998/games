/*
 * retro.h - shared helpers for the vintage game collection (C11 + SDL2, header-only).
 *
 *   - 320x200 canvas with the 16-colour EGA palette (plus one EGA-64 skin tone)
 *   - CRT presentation: 4:3 aspect, scanlines, vignette (F2 toggles)
 *   - DOS-style bold 8x8 text, 40 columns across
 *   - one-voice "PC speaker" square-wave sound with pitch slides
 *   - keyboard + gamepad buttons with edge detection
 *   - pixel-art sprites from strings, BMP screenshots, high-score table
 *
 * Include it from exactly one .c file per game.
 */
#ifndef RETRO_H
#define RETRO_H

#include <SDL.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RT_W 320
#define RT_H 200
#define RT_UP 4 /* the CRT pass draws at 4x before scaling to the window */

enum { C_BLACK, C_BLUE, C_GREEN, C_CYAN, C_RED, C_MAGENTA, C_BROWN, C_LGRAY,
       C_DGRAY, C_LBLUE, C_LGREEN, C_LCYAN, C_LRED, C_LMAGENTA, C_YELLOW, C_WHITE,
       C_SKIN, C_NCOL };

static const SDL_Color rt_pal[C_NCOL] = {
    {0, 0, 0, 255},     {0, 0, 170, 255},    {0, 170, 0, 255},    {0, 170, 170, 255},
    {170, 0, 0, 255},   {170, 0, 170, 255},  {170, 85, 0, 255},   {170, 170, 170, 255},
    {85, 85, 85, 255},  {85, 85, 255, 255},  {85, 255, 85, 255},  {85, 255, 255, 255},
    {255, 85, 85, 255}, {255, 85, 255, 255}, {255, 255, 85, 255}, {255, 255, 255, 255},
    {255, 170, 85, 255},
};

static SDL_Window *rt_win;
static SDL_Renderer *rt_ren;
static SDL_Texture *rt_canvas, *rt_big, *rt_mask;
static bool rt_crt = true;

/* ------------------------------------------------------------------ video */
static SDL_Texture *rt_make_mask(void) {
    int w = RT_W * RT_UP, h = RT_H * RT_UP;
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!s) return NULL;
    for (int y = 0; y < h; y++) {
        Uint32 *row = (Uint32 *)((Uint8 *)s->pixels + y * s->pitch);
        int line = y % RT_UP;
        float scan = line == RT_UP - 1 ? 0.42f : line == 0 ? 0.10f : 0.0f;
        float ny = (y + 0.5f) / h * 2 - 1;
        for (int x = 0; x < w; x++) {
            float nx = (x + 0.5f) / w * 2 - 1;
            float v = (nx * nx * 0.5f + ny * ny * 0.6f);
            float vig = v > 0.25f ? (v - 0.25f) * 0.55f : 0;
            float a = 1 - (1 - scan) * (1 - (vig > 0.6f ? 0.6f : vig));
            row[x] = (Uint32)(a * 255) << 24;
        }
    }
    SDL_Texture *t = SDL_CreateTextureFromSurface(rt_ren, s);
    SDL_FreeSurface(s);
    if (t) {
        SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(t, SDL_ScaleModeLinear);
    }
    return t;
}

static bool rt_init(const char *title) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return false;
    }
    rt_win = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 960, 720,
                              SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!rt_win) { fprintf(stderr, "window: %s\n", SDL_GetError()); return false; }
    rt_ren = SDL_CreateRenderer(rt_win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC |
                                                SDL_RENDERER_TARGETTEXTURE);
    if (!rt_ren) rt_ren = SDL_CreateRenderer(rt_win, -1, SDL_RENDERER_SOFTWARE | SDL_RENDERER_TARGETTEXTURE);
    if (!rt_ren) { fprintf(stderr, "renderer: %s\n", SDL_GetError()); return false; }
    rt_canvas = SDL_CreateTexture(rt_ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, RT_W, RT_H);
    rt_big = SDL_CreateTexture(rt_ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, RT_W * RT_UP, RT_H * RT_UP);
    if (!rt_canvas || !rt_big) { fprintf(stderr, "texture: %s\n", SDL_GetError()); return false; }
    SDL_SetTextureScaleMode(rt_canvas, SDL_ScaleModeNearest);
    SDL_SetTextureScaleMode(rt_big, SDL_ScaleModeLinear);
    rt_mask = rt_make_mask();
    SDL_SetRenderDrawBlendMode(rt_ren, SDL_BLENDMODE_BLEND);
    SDL_SetRenderTarget(rt_ren, rt_canvas);
    return true;
}

static void rt_present(void) {
    int ww, wh;
    SDL_SetRenderTarget(rt_ren, NULL);
    SDL_GetRendererOutputSize(rt_ren, &ww, &wh);
    SDL_SetRenderDrawColor(rt_ren, 0, 0, 0, 255);
    SDL_RenderClear(rt_ren);
    float s = fminf(ww / 4.0f, wh / 3.0f);
    SDL_Rect dst = {(int)((ww - 4 * s) / 2), (int)((wh - 3 * s) / 2), (int)(4 * s), (int)(3 * s)};
    if (rt_crt && rt_mask) {
        SDL_SetRenderTarget(rt_ren, rt_big);
        SDL_RenderCopy(rt_ren, rt_canvas, NULL, NULL);
        SDL_RenderCopy(rt_ren, rt_mask, NULL, NULL);
        SDL_SetRenderTarget(rt_ren, NULL);
        SDL_RenderCopy(rt_ren, rt_big, NULL, &dst);
    } else {
        SDL_RenderCopy(rt_ren, rt_canvas, NULL, &dst);
    }
    SDL_RenderPresent(rt_ren);
    SDL_SetRenderTarget(rt_ren, rt_canvas);
}

static void rt_shot(const char *path) {
    SDL_SetRenderTarget(rt_ren, rt_canvas);
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, RT_W, RT_H, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!s) return;
    SDL_RenderReadPixels(rt_ren, NULL, SDL_PIXELFORMAT_ARGB8888, s->pixels, s->pitch);
    SDL_SaveBMP(s, path);
    SDL_FreeSurface(s);
}

/* ------------------------------------------------------------------ drawing */
static void rt_col(int c) { SDL_SetRenderDrawColor(rt_ren, rt_pal[c].r, rt_pal[c].g, rt_pal[c].b, 255); }
static void rt_cls(int c) { rt_col(c); SDL_RenderClear(rt_ren); }
static void rt_rect(int x, int y, int w, int h, int c) {
    SDL_Rect r = {x, y, w, h};
    rt_col(c);
    SDL_RenderFillRect(rt_ren, &r);
}
static void rt_frame(int x, int y, int w, int h, int c) {
    SDL_Rect r = {x, y, w, h};
    rt_col(c);
    SDL_RenderDrawRect(rt_ren, &r);
}
static void rt_px(int x, int y, int c) { rt_col(c); SDL_RenderDrawPoint(rt_ren, x, y); }
static void rt_line(int x0, int y0, int x1, int y1, int c) { rt_col(c); SDL_RenderDrawLine(rt_ren, x0, y0, x1, y1); }
static void rt_disc(int cx, int cy, int r, int c) {
    rt_col(c);
    for (int dy = -r; dy <= r; dy++) {
        int dx = (int)sqrtf((float)(r * r - dy * dy));
        SDL_RenderDrawLine(rt_ren, cx - dx, cy + dy, cx + dx, cy + dy);
    }
}
/* DOS dialog box: filled, white double border */
static void rt_box(int x, int y, int w, int h, int fill, int border) {
    rt_rect(x, y, w, h, fill);
    rt_frame(x + 2, y + 2, w - 4, h - 4, border);
    rt_frame(x + 4, y + 4, w - 8, h - 8, border);
}

/* ------------------------------------------------------------------ font (5x7, drawn bold in an 8x8 cell) */
static const char RT_FONT_CHARS[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ:.-!/?, +<>'()=*";
static const unsigned char RT_FONT[][7] = {
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
    {4,4,8,0,0,0,0},{2,4,8,8,8,4,2},{8,4,2,2,2,4,8},{0,0,31,0,31,0,0},{0,21,14,31,14,21,0},
};

static const unsigned char *rt_glyph(char ch) {
    char c = (ch >= 'a' && ch <= 'z') ? (char)(ch - 32) : ch;
    const char *at = c ? strchr(RT_FONT_CHARS, c) : NULL;
    return at ? RT_FONT[at - RT_FONT_CHARS] : NULL;
}
static int rt_text_w(const char *s, int sc) { return (int)strlen(s) * 8 * sc; }

/* rowcol: optional 7 colours, one per glyph row (for gradient logos); else uses c */
static void rt_text_ex(const char *s, int x, int y, int sc, int c, const int *rowcol) {
    for (; *s; s++, x += 8 * sc) {
        const unsigned char *g = rt_glyph(*s);
        if (!g) continue;
        for (int row = 0; row < 7; row++) {
            rt_col(rowcol ? rowcol[row] : c);
            for (int col = 0; col < 5; col++)
                if (g[row] & (16 >> col)) {
                    SDL_Rect px = {x + 1 * sc + col * sc, y + row * sc, 2 * sc, sc};
                    SDL_RenderFillRect(rt_ren, &px);
                }
        }
    }
}
static void rt_text(const char *s, int x, int y, int c) { rt_text_ex(s, x, y, 1, c, NULL); }
static void rt_textc(const char *s, int y, int c) { rt_text(s, (RT_W - rt_text_w(s, 1)) / 2, y, c); }
static void rt_text_sh(const char *s, int x, int y, int c) {
    rt_text(s, x + 1, y + 1, C_BLACK);
    rt_text(s, x, y, c);
}
static void rt_textc_sh(const char *s, int y, int c) { rt_text_sh(s, (RT_W - rt_text_w(s, 1)) / 2, y, c); }

/* ------------------------------------------------------------------ sprites from strings
 * '.' transparent, '0'-'9' and 'a'-'f' EGA colours, 'g' skin tone. */
typedef struct { SDL_Texture *t; int w, h; } RtSpr;

static RtSpr rt_sprite(const char *const *rows, int h) {
    RtSpr sp = {NULL, (int)strlen(rows[0]), h};
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, sp.w, h, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!s) return sp;
    for (int y = 0; y < h; y++) {
        Uint32 *px = (Uint32 *)((Uint8 *)s->pixels + y * s->pitch);
        for (int x = 0; x < sp.w; x++) {
            char ch = rows[y][x];
            int ci = ch >= '0' && ch <= '9' ? ch - '0' : ch >= 'a' && ch <= 'f' ? ch - 'a' + 10 : ch == 'g' ? C_SKIN : -1;
            px[x] = ci < 0 ? 0 : (0xFFu << 24) | (Uint32)rt_pal[ci].r << 16 | (Uint32)rt_pal[ci].g << 8 | rt_pal[ci].b;
        }
    }
    sp.t = SDL_CreateTextureFromSurface(rt_ren, s);
    SDL_FreeSurface(s);
    if (sp.t) SDL_SetTextureBlendMode(sp.t, SDL_BLENDMODE_BLEND);
    return sp;
}
static void rt_draw(RtSpr sp, int x, int y, bool flip) {
    if (!sp.t) return;
    SDL_Rect d = {x, y, sp.w, sp.h};
    SDL_RenderCopyEx(rt_ren, sp.t, NULL, &d, 0, NULL, flip ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);
}
/* draw with every opaque pixel replaced by one colour (hit flashes, silhouettes) */
static void rt_draw_tint(RtSpr sp, int x, int y, bool flip, int c) {
    if (!sp.t) return;
    SDL_SetTextureColorMod(sp.t, rt_pal[c].r, rt_pal[c].g, rt_pal[c].b);
    SDL_SetTextureBlendMode(sp.t, SDL_BLENDMODE_ADD);
    rt_draw(sp, x, y, flip);
    SDL_SetTextureColorMod(sp.t, 255, 255, 255);
    SDL_SetTextureBlendMode(sp.t, SDL_BLENDMODE_BLEND);
}

/* ------------------------------------------------------------------ PC speaker sound
 * One voice, like the real thing: a new sound only interrupts one of equal or lower priority. */
typedef struct { float f0, f1; int ms; } RtNote; /* slide f0 -> f1 Hz; f0 = 0 is a rest */
#define RT_QMAX 96
static SDL_AudioDeviceID rt_adev;
static RtNote rt_q[RT_QMAX];
static int rt_qn, rt_qi, rt_qprio, rt_left, rt_total;
static double rt_phase;
static float rt_lp;
static bool rt_mute;
static int rt_rate = 44100;

static void rt_audio_cb(void *u, Uint8 *stream, int len) {
    (void)u;
    float *out = (float *)stream;
    int n = len / (int)sizeof(float);
    for (int i = 0; i < n; i++) {
        if (rt_left <= 0 && rt_qn > 0) {
            rt_qi++;
            if (rt_qi < rt_qn) {
                rt_total = rt_left = rt_q[rt_qi].ms * rt_rate / 1000;
                if (rt_total < 1) rt_total = rt_left = 1;
            } else {
                rt_qn = 0;
                rt_qprio = 0;
            }
        }
        float target = 0;
        if (rt_qn > 0 && rt_qi >= 0 && rt_qi < rt_qn) {
            RtNote *nt = &rt_q[rt_qi];
            float k = 1.0f - (float)rt_left / (float)rt_total;
            float f = nt->f0 + (nt->f1 - nt->f0) * k;
            if (nt->f0 > 0 && f > 0) {
                rt_phase += f / rt_rate;
                if (rt_phase >= 1) rt_phase -= floor(rt_phase);
                target = rt_mute ? 0 : (rt_phase < 0.5 ? 0.16f : -0.16f);
            }
            rt_left--;
        }
        rt_lp += (target - rt_lp) * 0.30f; /* the little speaker's cone */
        out[i] = rt_lp;
    }
}
static void rt_audio_init(void) {
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) return;
    SDL_AudioSpec want = {0}, have;
    want.freq = 44100;
    want.format = AUDIO_F32SYS;
    want.channels = 1;
    want.samples = 1024;
    want.callback = rt_audio_cb;
    rt_adev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (rt_adev) { rt_rate = have.freq; SDL_PauseAudioDevice(rt_adev, 0); }
}
static void rt_play(const RtNote *notes, int count, int prio) {
    if (!rt_adev || count <= 0) return;
    if (count > RT_QMAX) count = RT_QMAX;
    SDL_LockAudioDevice(rt_adev);
    if (rt_qn == 0 || prio >= rt_qprio) {
        memcpy(rt_q, notes, sizeof(RtNote) * (size_t)count);
        rt_qn = count;
        rt_qi = -1;
        rt_left = 0;
        rt_qprio = prio;
    }
    SDL_UnlockAudioDevice(rt_adev);
}
static void rt_tone(float f0, float f1, int ms, int prio) {
    RtNote n = {f0, f1, ms};
    rt_play(&n, 1, prio);
}
static bool rt_busy(void) { return rt_qn > 0; }

/* ------------------------------------------------------------------ input */
typedef struct { bool l, r, u, d, jump, fire, jet, ok, back; } RtBtn;
static RtBtn rt_held, rt_prev;
static SDL_GameController *rt_pad;
#define RT_HIT(f) (rt_held.f && !rt_prev.f)

static void rt_poll(void) {
    rt_prev = rt_held;
    const Uint8 *k = SDL_GetKeyboardState(NULL);
    RtBtn b = {0};
    b.l = k[SDL_SCANCODE_LEFT] || k[SDL_SCANCODE_A] || k[SDL_SCANCODE_KP_4];
    b.r = k[SDL_SCANCODE_RIGHT] || k[SDL_SCANCODE_D] || k[SDL_SCANCODE_KP_6];
    b.u = k[SDL_SCANCODE_UP] || k[SDL_SCANCODE_W] || k[SDL_SCANCODE_KP_8];
    b.d = k[SDL_SCANCODE_DOWN] || k[SDL_SCANCODE_S] || k[SDL_SCANCODE_KP_2];
    b.jump = k[SDL_SCANCODE_SPACE] || k[SDL_SCANCODE_Z] || k[SDL_SCANCODE_UP] || k[SDL_SCANCODE_W];
    b.fire = k[SDL_SCANCODE_X] || k[SDL_SCANCODE_LCTRL] || k[SDL_SCANCODE_RCTRL] || k[SDL_SCANCODE_J];
    b.jet = k[SDL_SCANCODE_C] || k[SDL_SCANCODE_LALT] || k[SDL_SCANCODE_RALT] || k[SDL_SCANCODE_K];
    b.ok = k[SDL_SCANCODE_RETURN] || k[SDL_SCANCODE_SPACE] || k[SDL_SCANCODE_KP_ENTER];
    b.back = k[SDL_SCANCODE_ESCAPE];
    if (rt_pad) {
        SDL_GameController *p = rt_pad;
        int ax = SDL_GameControllerGetAxis(p, SDL_CONTROLLER_AXIS_LEFTX);
        int ay = SDL_GameControllerGetAxis(p, SDL_CONTROLLER_AXIS_LEFTY);
        b.l |= ax < -12000 || SDL_GameControllerGetButton(p, SDL_CONTROLLER_BUTTON_DPAD_LEFT);
        b.r |= ax > 12000 || SDL_GameControllerGetButton(p, SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
        b.u |= ay < -12000 || SDL_GameControllerGetButton(p, SDL_CONTROLLER_BUTTON_DPAD_UP);
        b.d |= ay > 12000 || SDL_GameControllerGetButton(p, SDL_CONTROLLER_BUTTON_DPAD_DOWN);
        b.jump |= SDL_GameControllerGetButton(p, SDL_CONTROLLER_BUTTON_A);
        b.fire |= SDL_GameControllerGetButton(p, SDL_CONTROLLER_BUTTON_X) ||
                  SDL_GameControllerGetButton(p, SDL_CONTROLLER_BUTTON_B) ||
                  SDL_GameControllerGetButton(p, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
        b.jet |= SDL_GameControllerGetButton(p, SDL_CONTROLLER_BUTTON_Y) ||
                 SDL_GameControllerGetButton(p, SDL_CONTROLLER_BUTTON_LEFTSHOULDER);
        b.ok |= SDL_GameControllerGetButton(p, SDL_CONTROLLER_BUTTON_A) ||
                SDL_GameControllerGetButton(p, SDL_CONTROLLER_BUTTON_START);
        b.back |= SDL_GameControllerGetButton(p, SDL_CONTROLLER_BUTTON_BACK);
    }
    rt_held = b;
}

/* handles pad hot-plug, F2 (CRT) and F11 (fullscreen); returns true if it used the event */
static bool rt_event(const SDL_Event *e) {
    switch (e->type) {
    case SDL_CONTROLLERDEVICEADDED:
        if (!rt_pad) rt_pad = SDL_GameControllerOpen(e->cdevice.which);
        return true;
    case SDL_CONTROLLERDEVICEREMOVED:
        if (rt_pad && e->cdevice.which == SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(rt_pad))) {
            SDL_GameControllerClose(rt_pad);
            rt_pad = NULL;
        }
        return true;
    case SDL_KEYDOWN:
        if (e->key.repeat) return false;
        if (e->key.keysym.sym == SDLK_F2) { rt_crt = !rt_crt; return true; }
        if (e->key.keysym.sym == SDLK_F11) {
            bool fs = SDL_GetWindowFlags(rt_win) & SDL_WINDOW_FULLSCREEN_DESKTOP;
            SDL_SetWindowFullscreen(rt_win, fs ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
            return true;
        }
        return false;
    }
    return false;
}

/* ------------------------------------------------------------------ high scores */
#define RT_NSCORES 8
typedef struct { char name[4]; int score, level; } RtScore;
static RtScore rt_scores[RT_NSCORES];
static char rt_score_path[1024];

static void rt_scores_load(const char *app, const RtScore *defaults) {
    memcpy(rt_scores, defaults, sizeof(rt_scores));
    char *dir = SDL_GetPrefPath("doomedl", app);
    if (!dir) return;
    snprintf(rt_score_path, sizeof(rt_score_path), "%sscores.txt", dir);
    SDL_free(dir);
    FILE *f = fopen(rt_score_path, "r");
    if (!f) return;
    for (int i = 0; i < RT_NSCORES; i++) {
        RtScore s = {{0}, 0, 0};
        if (fscanf(f, "%3s %d %d", s.name, &s.score, &s.level) != 3) break;
        rt_scores[i] = s;
    }
    fclose(f);
}
static void rt_scores_save(void) {
    if (!rt_score_path[0]) return;
    FILE *f = fopen(rt_score_path, "w");
    if (!f) return;
    for (int i = 0; i < RT_NSCORES; i++)
        fprintf(f, "%s %d %d\n", rt_scores[i].name, rt_scores[i].score, rt_scores[i].level);
    fclose(f);
}
static int rt_score_rank(int score) {
    for (int i = 0; i < RT_NSCORES; i++)
        if (score > rt_scores[i].score) return i;
    return -1;
}
static void rt_score_insert(int rank, const char *name, int score, int level) {
    if (rank < 0 || rank >= RT_NSCORES) return;
    memmove(&rt_scores[rank + 1], &rt_scores[rank], sizeof(RtScore) * (size_t)(RT_NSCORES - 1 - rank));
    RtScore s = {{0}, score, level};
    snprintf(s.name, sizeof(s.name), "%s", name);
    rt_scores[rank] = s;
}

/* tiny deterministic RNG */
static Uint32 rt_seed = 12345;
static Uint32 rt_rand(void) { rt_seed = rt_seed * 1664525u + 1013904223u; return rt_seed >> 8; }
static float rt_randf(void) { return (float)(rt_rand() & 0xFFFF) / 65535.0f; }

#endif
