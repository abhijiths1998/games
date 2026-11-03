/*
 * PORT CALDER - an open-city driving sandbox.
 * Native Linux, C11 + SDL2 + OpenGL 3.3 core. No asset files: the island city, its traffic,
 * pedestrians, police, weather, sounds and radio stations are all generated at startup.
 *
 * Source layout (one translation unit plus the audio module):
 *   gfx.c     math, shaders, instanced renderer, post-processing, HUD primitives
 *   world.c   terrain, districts, buildings, landmarks, collision, road graph
 *   actors.c  particles, vehicles + traffic/police AI, pedestrians, street props
 *   game.c    player, wanted level, missions and activities, HUD, saving
 *   main.c    scene assembly, camera, input, main loop, self-test
 *   audio.c   software mixer, synthesized effects, engine, siren, radio
 *
 *   ./port-calder                  play
 *   ./port-calder --selftest DIR   headless checks + screenshots into DIR (never writes a save)
 */
#define GL_GLEXT_PROTOTYPES 1
#include <SDL.h>
#include <GL/glcorearb.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "audio.h"

#include "gfx.c"
#include "world.c"
#include "actors.c"
#include "game.c"

/* ================================================================== scene assembly */
static Mat paint(v3 c) { Mat m = {c, PAT_PAINT, 0.45f, 0.32f, {0, 0, 0}, 0}; return m; }
static const Mat M_GLASS = {{0.02f, 0.025f, 0.03f}, PAT_PLAIN, 0.9f, 0.06f, {0, 0, 0}, 0};
static const Mat M_TYRE = {{0.03f, 0.03f, 0.03f}, PAT_PLAIN, 0, 0.8f, {0, 0, 0}, 0};
static const Mat M_RIM = {{0.6f, 0.6f, 0.62f}, PAT_PLAIN, 1, 0.25f, {0, 0, 0}, 0};
static const Mat M_BLACK = {{0.025f, 0.025f, 0.028f}, PAT_PLAIN, 0.2f, 0.5f, {0, 0, 0}, 0};
static const Mat M_BURNT = {{0.04f, 0.035f, 0.03f}, PAT_LEAF, 0.3f, 0.9f, {0, 0, 0}, 0};

static void part_box(m4 M, v3 c, v3 s, const Mat *m) { inst(MS_CUBE, m4_mul(M, m4_box(c, s)), m); }

static void push_car(const Car *c, float t) {
    const CarSpec *S = &SPEC[c->model];
    float L = S->L, W = S->W, H = S->H;
    m4 M = m4_trs(c->pos, c->yaw, c->pitch, c->roll, V(1, 1, 1));
    bool burnt = c->wrecked;
    float dmg = 1 - clampf(c->hp / S->hp, 0, 1);
    Mat body = burnt ? M_BURNT : paint(mul(c->col, 1 - dmg * 0.45f));
    Mat dark = burnt ? M_BURNT : M_BLACK;
    float night = NIGHT;
    float clear = 0.30f, bh = H * 0.42f;
    bool tall = c->model == VM_VAN || c->model == VM_BUS;
    if (c->model == VM_BUS) {
        part_box(M, V(0, clear + H * 0.45f, 0), V(W, H * 0.9f - clear * 0.5f, L), &body);
        part_box(M, V(0, clear + H * 0.62f, -0.2f), V(W + 0.02f, H * 0.28f, L - 1.4f), &M_GLASS);
        part_box(M, V(0, clear + H * 0.55f, -L / 2 + 0.05f), V(W * 0.9f, H * 0.45f, 0.12f), &M_GLASS);
        Mat sign = glow(V(0.2f, 0.2f, 0.1f), V(3, 2.2f, 0.4f), PAT_PLAIN, 0.7f);
        part_box(M, V(0, H - 0.25f, -L / 2 + 0.02f), V(W * 0.6f, 0.3f, 0.06f), &sign);
    } else if (tall) {
        part_box(M, V(0, clear + bh * 0.5f, 0), V(W, bh, L), &body);
        part_box(M, V(0, clear + bh + (H - bh - clear) * 0.5f, 0.35f), V(W * 0.98f, H - bh - clear, L * 0.78f), &body);
        part_box(M, V(0, clear + bh + 0.35f, -L * 0.36f), V(W * 0.9f, 0.6f, 0.5f), &M_GLASS);
    } else if (c->model == VM_PICKUP) {
        part_box(M, V(0, clear + bh * 0.5f, 0), V(W, bh, L), &body);
        part_box(M, V(0, clear + bh + 0.33f, -L * 0.08f), V(W * 0.9f, 0.66f, L * 0.32f), &M_GLASS);
        part_box(M, V(0, clear + bh + 0.69f, -L * 0.08f), V(W * 0.88f, 0.06f, L * 0.3f), &body);
        part_box(M, V(W / 2 - 0.06f, clear + bh + 0.22f, L * 0.26f), V(0.1f, 0.44f, L * 0.42f), &body);
        part_box(M, V(-W / 2 + 0.06f, clear + bh + 0.22f, L * 0.26f), V(0.1f, 0.44f, L * 0.42f), &body);
        part_box(M, V(0, clear + bh + 0.22f, L / 2 - 0.05f), V(W, 0.44f, 0.1f), &body);
    } else {
        bool low = c->model == VM_SPORTS;
        float cab_h = H - bh - clear;
        part_box(M, V(0, clear + bh * 0.5f, 0), V(W, bh, L), &body);
        part_box(M, V(0, clear + bh + cab_h * 0.48f, low ? 0.25f : 0.1f), V(W * 0.86f, cab_h * 0.96f, L * (low ? 0.42f : 0.5f)), &M_GLASS);
        part_box(M, V(0, clear + bh + cab_h * 0.97f, low ? 0.3f : 0.12f), V(W * 0.84f, 0.06f, L * (low ? 0.36f : 0.44f)), &body);
        if (low) part_box(M, V(0, clear + bh + 0.25f, L / 2 - 0.25f), V(W * 0.9f, 0.06f, 0.35f), &dark);
        if (c->model == VM_MUSCLE && !burnt) {
            Mat st = paint(V(0.85f, 0.85f, 0.85f));
            part_box(M, V(0.25f, clear + bh + 0.005f, -L * 0.3f), V(0.18f, 0.02f, L * 0.36f), &st);
            part_box(M, V(-0.25f, clear + bh + 0.005f, -L * 0.3f), V(0.18f, 0.02f, L * 0.36f), &st);
            part_box(M, V(0, clear + bh + 0.08f, -L * 0.28f), V(0.5f, 0.16f, 0.6f), &dark);
        }
        if (c->model == VM_POLICE && !burnt) {
            part_box(M, V(0, clear + bh * 0.3f, 0), V(W + 0.02f, bh * 0.6f, L * 0.55f), &M_BLACK);
            bool fl = fmodf(t * 3.3f, 1) < 0.5f;
            Mat r = glow(V(0.3f, 0, 0), c->siren ? (fl ? V(9, 0.3f, 0.2f) : V(0.4f, 0, 0)) : V(0.3f, 0, 0), PAT_PLAIN, 0);
            Mat b = glow(V(0, 0, 0.3f), c->siren ? (!fl ? V(0.3f, 0.8f, 9) : V(0, 0, 0.4f)) : V(0, 0, 0.3f), PAT_PLAIN, 0);
            part_box(M, V(-0.32f, H + 0.06f, 0.1f), V(0.55f, 0.13f, 0.28f), &r);
            part_box(M, V(0.32f, H + 0.06f, 0.1f), V(0.55f, 0.13f, 0.28f), &b);
        }
        if (c->model == VM_TAXI && !burnt) {
            Mat sign = glow(V(0.5f, 0.45f, 0.2f), V(3, 2.4f, 0.6f), PAT_PLAIN, 0.8f);
            part_box(M, V(0, H + 0.12f, 0.1f), V(0.7f, 0.22f, 0.3f), &sign);
        }
    }
    /* bumpers, lights, wheels */
    part_box(M, V(0, clear + 0.12f, -L / 2 - 0.04f), V(W * 0.96f, 0.2f, 0.1f), &dark);
    part_box(M, V(0, clear + 0.12f, L / 2 + 0.04f), V(W * 0.96f, 0.2f, 0.1f), &dark);
    if (!burnt) {
        float hl = 0.5f + night * 5;
        Mat head = glow(V(0.8f, 0.8f, 0.75f), V(hl, hl * 0.95f, hl * 0.8f), PAT_PLAIN, 0);
        float tl = (c->brk > 0.1f ? 5.5f : 0.4f + night * 2.5f);
        Mat tail = glow(V(0.4f, 0.02f, 0.02f), V(tl, tl * 0.06f, tl * 0.04f), PAT_PLAIN, 0);
        for (int s = -1; s <= 1; s += 2) {
            part_box(M, V(s * W * 0.34f, clear + bh * 0.7f, -L / 2 - 0.01f), V(W * 0.2f, 0.12f, 0.04f), &head);
            part_box(M, V(s * W * 0.36f, clear + bh * 0.72f, L / 2 + 0.01f), V(W * 0.18f, 0.12f, 0.04f), &tail);
        }
    }
    float wr = c->model == VM_BUS ? 0.5f : 0.36f;
    float wz = L / 2 - (c->model == VM_BUS ? 1.6f : 0.85f);
    for (int k = 0; k < 4; k++) {
        float sx = k & 1 ? 1 : -1, sz = k & 2 ? 1 : -1;
        float st = sz < 0 ? c->steer * SPEC[c->model].steer * 0.8f : 0;
        m4 wm = m4_mul(M, m4_trs(V(sx * (W / 2 - 0.12f), wr, sz * wz), st, 0, 0, V(1, 1, 1)));
        inst(MS_CYL, m4_mul(wm, m4_trs(V(-0.13f * 0 + 0.13f, 0, 0), 0, 0, 1.5708f, V(wr * 2, 0.26f, wr * 2))), &M_TYRE);
        inst(MS_CYL8, m4_mul(wm, m4_trs(V(sx * 0.005f + 0.135f, 0, 0), 0, c->wheel, 1.5708f, V(wr * 1.2f, 0.27f, wr * 1.2f))), &M_RIM);
    }
}

static void push_human(v3 pos, float yaw, float anim, float speed, v3 shirt, v3 pants, v3 skin, v3 hair, float h, float lie, float tumble, bool hail, bool cop) {
    m4 R = m4_trs(pos, yaw, lie + tumble, 0, V(h, h, h));
    float sw = sinf(anim) * clampf(speed / 3, 0, 1) * 0.7f;
    Mat ms = mat(shirt, PAT_PLAIN, 0, 0.8f), mp = mat(pants, PAT_PLAIN, 0, 0.85f), mk = mat(skin, PAT_PLAIN, 0, 0.6f), mh = mat(hair, PAT_PLAIN, 0, 0.9f);
    for (int s = -1; s <= 1; s += 2) {
        m4 leg = m4_mul(R, m4_trs(V(s * 0.11f, 0.92f, 0), 0, sw * s, 0, V(1, 1, 1)));
        inst(MS_CUBE, m4_mul(leg, m4_box(V(0, -0.45f, 0), V(0.17f, 0.9f, 0.2f))), &mp);
        inst(MS_CUBE, m4_mul(leg, m4_box(V(0, -0.88f, -0.05f), V(0.17f, 0.08f, 0.3f))), &M_BLACK);
        float arm = hail && s > 0 ? 2.8f : -sw * s * 0.9f;
        m4 am = m4_mul(R, m4_trs(V(s * 0.3f, 1.47f, 0), 0, arm, s * 0.08f, V(1, 1, 1)));
        inst(MS_CUBE, m4_mul(am, m4_box(V(0, -0.28f, 0), V(0.12f, 0.56f, 0.13f))), &ms);
        inst(MS_CUBE, m4_mul(am, m4_box(V(0, -0.6f, 0), V(0.1f, 0.1f, 0.1f))), &mk);
    }
    inst(MS_CUBE, m4_mul(R, m4_box(V(0, 1.22f, 0), V(0.46f, 0.6f, 0.26f))), &ms);
    inst(MS_SPHERE, m4_mul(R, m4_trs(V(0, 1.67f, 0), 0, 0, 0, V(0.13f, 0.15f, 0.14f))), &mk);
    if (cop) {
        part_box(R, V(0, 1.82f, 0), V(0.3f, 0.08f, 0.32f), &M_BLACK);
        part_box(R, V(0, 1.79f, -0.18f), V(0.3f, 0.03f, 0.12f), &M_BLACK);
    } else inst(MS_SPHERE, m4_mul(R, m4_trs(V(0, 1.71f, 0.02f), 0, 0, 0, V(0.14f, 0.12f, 0.145f))), &mh);
}

static float wheel_angle(float t) { return t * 0.07f; }
static void push_world_dynamic(float t) {
    v3 cp = cam.pos;
    /* lamp posts + traffic signals */
    float night = NIGHT;
    Mat lamp_head = glow(V(0.5f, 0.45f, 0.35f), V(5, 3.6f, 1.8f), PAT_PLAIN, 1);
    Mat pole = M_DARKMETAL;
    for (int i = 0; i < NPROP; i++) {
        Prop *q = &prop[i];
        if (fabsf(q->pos.x - cp.x) > 260 || fabsf(q->pos.z - cp.z) > 260) continue;
        if (!prop_exists(i)) continue;
        v3 home = prop_home(i);
        int n = i / 4, k = i % 4, ni = n / (NB + 1), nj = n % (NB + 1);
        /* the arm reaches toward the road it lights */
        float ax = k & 1 ? -1 : 1, az = k & 2 ? -1 : 1;
        (void)home;
        m4 R = m4_trs(q->pos, 0, q->rot.x, q->rot.z, V(1, 1, 1));
        inst(MS_CYL8, m4_mul(R, m4_trs(V(0, 0, 0), 0, 0, 0, V(0.22f, 6.4f, 0.22f))), &pole);
        inst(MS_CUBE, m4_mul(R, m4_box(V(ax * 0.6f, 6.3f, az * 0.6f), V(1.4f, 0.12f, 0.12f))), &pole);
        inst(MS_CUBE, m4_mul(R, m4_box(V(ax * 1.2f, 6.2f, az * 1.2f), V(0.6f, 0.15f, 0.4f))), q->down ? &pole : &lamp_head);
        if (!q->down && node_degree(ni, nj) >= 3 && fabsf(q->pos.x - cp.x) < 170 && fabsf(q->pos.z - cp.z) < 170) {
            int ew = light_state(ni, nj, 0), ns = light_state(ni, nj, 1);
            v3 cols[3] = {V(0.2f, 3.2f, 0.9f), V(3.6f, 1.8f, 0.1f), V(3.6f, 0.15f, 0.1f)};
            inst(MS_CUBE, m4_box(add(q->pos, V(0, 4.1f, 0)), V(0.42f, 0.95f, 0.42f)), &M_BLACK);
            Mat a = glow(V(0, 0, 0), cols[ew], PAT_PLAIN, 0), b = glow(V(0, 0, 0), cols[ns], PAT_PLAIN, 0);
            float yo = ew == 0 ? -0.28f : ew == 1 ? 0 : 0.28f, zo = ns == 0 ? -0.28f : ns == 1 ? 0 : 0.28f;
            inst(MS_CUBE, m4_box(add(q->pos, V(0, 4.1f - yo, 0)), V(0.46f, 0.2f, 0.2f)), &a);
            inst(MS_CUBE, m4_box(add(q->pos, V(0, 4.1f - zo, 0)), V(0.2f, 0.2f, 0.46f)), &b);
        }
    }
    (void)night;
    /* ferris wheel */
    {
        float a = wheel_angle(t), R = 28;
        m4 Mw = m4_trs(WHEEL_C, 1.5708f, 0, a, V(1, 1, 1));
        Mat rim = {{0.85f, 0.85f, 0.85f}, PAT_PLAIN, 0.6f, 0.3f, {0, 0, 0}, 0};
        Mat bulbs = glow(V(0.6f, 0.5f, 0.5f), V(2.2f, 1.0f, 2.6f), PAT_NEON, 1);
        for (int s = -1; s <= 1; s += 2) {
            inst(MS_TORUS, m4_mul(Mw, m4_trs(V(0, 0, s * 2.5f), 0, 0, 0, V(R, R, 2))), &rim);
            inst(MS_TORUS, m4_mul(Mw, m4_trs(V(0, 0, s * 2.6f), 0, 0, 0, V(R + 0.3f, R + 0.3f, 1.2f))), &bulbs);
        }
        inst(MS_CYL8, m4_mul(Mw, m4_trs(V(0, 0, -3.2f), 0, 1.5708f, 0, V(1.6f, 6.4f, 1.6f))), &M_STEEL);
        for (int k = 0; k < 16; k++) {
            float ang = k * 0.3927f;
            m4 sp = m4_mul(Mw, m4_trs(V(0, 0, 0), 0, 0, ang, V(1, 1, 1)));
            inst(MS_CUBE, m4_mul(sp, m4_box(V(0, R / 2, 0), V(0.3f, R, 0.3f))), &rim);
            v3 g = m4_point(Mw, V(cosf(ang) * R, sinf(ang) * R, 0));
            Mat gm = mat(NEONS[k % 6], PAT_PLAIN, 0.2f, 0.5f);
            gm.base = mul(gm.base, 0.6f);
            inst(MS_CUBE, m4_box(add(g, V(0, -1.6f, 0)), V(2.6f, 2.2f, 2.0f)), &gm);
            inst(MS_CUBE, m4_box(add(g, V(0, -0.3f, 0)), V(0.12f, 0.8f, 0.12f)), &rim);
        }
    }
    /* collectible tapes */
    for (int k = 0; k < NTAPE; k++) {
        if (tapes_found & (1u << k)) continue;
        if (dist2d(tapes[k], cp) > 200) continue;
        v3 p = add(tapes[k], V(0, 0.9f + sinf(t * 2 + k) * 0.12f, 0));
        Mat tm = glow(V(0.1f, 0.1f, 0.12f), V(0.2f, 0.8f, 1.6f), PAT_PLAIN, 0);
        inst(MS_CUBE, m4_trs(p, t * 1.6f + k, 0, 0, V(0.5f, 0.32f, 0.08f)), &M_BLACK);
        inst(MS_CUBE, m4_trs(p, t * 1.6f + k, 0, 0, V(0.36f, 0.14f, 0.09f)), &tm);
    }
    /* race checkpoints */
    if (MS.type == MT_RACE) {
        Race *R = &races[MS.race];
        for (int k = MS.cp; k < R->ncp && k < MS.cp + 2; k++) {
            v3 p = R->cp[k];
            v3 nx = k + 1 < R->ncp ? R->cp[k + 1] : add(p, V(1, 0, 0));
            float yaw = k > 0 ? yaw_of(sub(p, R->cp[k - 1])) : yaw_of(sub(nx, p));
            Mat rm = glow(V(0.3f, 0.2f, 0), k == MS.cp ? V(4, 2.6f, 0.3f) : V(1.2f, 0.8f, 0.1f), PAT_NEON, 0);
            inst(MS_TORUS, m4_trs(add(p, V(0, 5, 0)), yaw, 0, t, V(5.5f, 5.5f, 4)), &rm);
        }
    }
    /* police helicopter */
    if (G.heli_t > 0) {
        v3 tp = player_target_pos();
        float a = t * 0.35f;
        v3 hp = add(tp, V(cosf(a) * 30, 42 + sinf(t * 0.7f) * 2, sinf(a) * 30));
        float yaw = yaw_of(V(-sinf(a), 0, cosf(a)));
        m4 H = m4_trs(hp, yaw, 0.1f, 0.15f, V(1, 1, 1));
        Mat hb = {{0.12f, 0.14f, 0.2f}, PAT_PAINT, 0.5f, 0.3f, {0, 0, 0}, 0};
        inst(MS_SPHERE, m4_mul(H, m4_trs(V(0, 0, 0), 0, 0, 0, V(1.4f, 1.3f, 2.8f))), &hb);
        part_box(H, V(0, 0.2f, 3.6f), V(0.35f, 0.4f, 4.2f), &hb);
        part_box(H, V(0, 0.9f, 5.5f), V(0.1f, 1.4f, 0.8f), &hb);
        for (int k = 0; k < 2; k++) inst(MS_CUBE, m4_mul(H, m4_trs(V(0, 1.5f, 0), t * 30 + k * 1.5708f, 0, 0, V(0.3f, 0.05f, 10))), &M_BLACK);
        part_box(H, V(0.9f, -1.3f, 0), V(0.1f, 0.1f, 3.2f), &M_BLACK);
        part_box(H, V(-0.9f, -1.3f, 0), V(0.1f, 0.1f, 3.2f), &M_BLACK);
    }
}

/* ---- lights for this frame */
typedef struct { v3 pos, col; float radius; } FLight;
static FLight frame_lights[32];
static int nframe_lights;
static void gather_lights(float t) {
    enum { MAXC = 320 };
    static FLight cand[MAXC];
    static float score[MAXC];
    int n = 0;
#define CAND(p, c, r, sc) do { if (n < MAXC) { cand[n] = (FLight){p, c, r}; score[n++] = (sc); } } while (0)
    for (int i = 0; i < MAXDL; i++)
        if (dl[i].life > 0) CAND(dl[i].pos, mul(dl[i].col, dl[i].life / dl[i].max), dl[i].radius, -1000 + len(sub(dl[i].pos, cam.pos)));
    float night = NIGHT;
    for (int i = 0; i < MAXCAR; i++) {
        Car *c = &car[i];
        if (!c->on || c->wrecked) continue;
        float d = len(sub(c->pos, cam.pos));
        if (d > 180) continue;
        v3 f = fwd_of(c->yaw);
        if (c->siren) {
            bool fl = fmodf(t * 3.3f, 1) < 0.5f;
            CAND(add(c->pos, V(0, 2.2f, 0)), fl ? V(5, 0.2f, 0.1f) : V(0.2f, 0.6f, 6), 16, -300 + d);
        }
        if (night > 0.3f && (c->driver == DRV_PLAYER || d < 60))
            CAND(add(add(c->pos, mul(f, SPEC[c->model].L / 2 + 6)), V(0, 1.2f, 0)), mul(V(2.4f, 2.2f, 1.9f), night), 20, (c->driver == DRV_PLAYER ? -400 : 0) + d * 1.5f);
    }
    if (G.heli_t > 0) CAND(add(player_target_pos(), V(0, 5, 0)), V(5, 5, 5.5f), 18, -350);
    if (night > 0.05f) {
        int ci, cj;
        nearest_node(cam.pos, &ci, &cj);
        for (int i = ci - 2; i <= ci + 2; i++)
            for (int j = cj - 2; j <= cj + 2; j++) {
                if (i < 0 || j < 0 || i > NB || j > NB) continue;
                for (int k = 0; k < 4; k++) {
                    int idx = (i * (NB + 1) + j) * 4 + k;
                    if (!prop_exists(idx) || prop[idx].down) continue;
                    v3 h = prop_home(idx);
                    float ax = k & 1 ? -1 : 1, az = k & 2 ? -1 : 1;
                    v3 p = add(h, V(ax * 1.2f, 5.8f, az * 1.2f));
                    CAND(p, mul(V(2.6f, 1.75f, 0.85f), night), 15, len(sub(p, cam.pos)) * 1.4f);
                }
            }
        for (int i = 0; i < nslight; i++) {
            float d = len(sub(slights[i].pos, cam.pos));
            if (d < 160) CAND(slights[i].pos, mul(slights[i].col, night), slights[i].radius, d * 1.2f);
        }
    }
    nframe_lights = 0;
    for (int k = 0; k < 32 && k < n; k++) {
        int best = k;
        for (int j = k + 1; j < n; j++) if (score[j] < score[best]) best = j;
        FLight tl = cand[k]; cand[k] = cand[best]; cand[best] = tl;
        float ts = score[k]; score[k] = score[best]; score[best] = ts;
        frame_lights[nframe_lights++] = cand[k];
    }
}

/* ---- additive glows + smoke */
static void marker_beam(v3 p, v3 col, float h, float t) {
    float pulse = 0.75f + 0.25f * sinf(t * 3);
    beam(add(p, V(0, 0.1f, 0)), add(p, V(0, h, 0)), 0.9f, col, 0.35f * pulse);
    beam(add(p, V(0, 0.1f, 0)), add(p, V(0, h * 0.6f, 0)), 0.35f, mul(col, 2), 0.5f * pulse);
    ground_ring(add(p, V(0, 0.08f, 0)), 2.6f + sinf(t * 2) * 0.2f, col, 0.9f);
}
static void build_glows(float t) {
    float night = NIGHT;
    v3 cp = cam.pos;
    for (int i = 0; i < MAXPART; i++) {
        Part *p = &part[i];
        if (p->life <= 0) continue;
        float k = p->life / p->max;
        if (p->kind == PK_STREAK) beam(p->pos, sub(p->pos, mul(p->vel, 0.03f)), p->size, p->col, k);
        else if (p->kind == PK_RING) {
            float r = p->size;
            pquad(add(p->pos, V(-r, 0, -r)), add(p->pos, V(r, 0, -r)), add(p->pos, V(r, 0, r)), add(p->pos, V(-r, 0, r)), p->col, k, 2);
        } else if (p->kind == PK_SMOKE) billboard(p->pos, p->size, p->col, clampf(k * 1.6f, 0, 0.85f), 3);
        else billboard(p->pos, p->size, p->col, k, 0);
    }
    /* vehicle lights */
    for (int i = 0; i < MAXCAR; i++) {
        Car *c = &car[i];
        if (!c->on || c->wrecked || c->sunk) continue;
        float d = dist2d(c->pos, cp);
        if (d > 300) continue;
        const CarSpec *S = &SPEC[c->model];
        m4 M = m4_trs(c->pos, c->yaw, c->pitch, c->roll, V(1, 1, 1));
        float bh = S->H * 0.42f;
        if (night > 0.2f) {
            for (int s = -1; s <= 1; s += 2) {
                v3 h = m4_point(M, V(s * S->W * 0.34f, 0.3f + bh * 0.7f, -S->L / 2 - 0.1f));
                billboard(h, 0.35f, V(2.2f, 2.1f, 1.8f), night, 0);
                v3 tl = m4_point(M, V(s * S->W * 0.36f, 0.3f + bh * 0.72f, S->L / 2 + 0.1f));
                billboard(tl, c->brk > 0.1f ? 0.4f : 0.25f, V(2.4f, 0.1f, 0.05f), night * (c->brk > 0.1f ? 1.5f : 0.8f), 0);
            }
        }
        if (c->siren) {
            bool fl = fmodf(t * 3.3f, 1) < 0.5f;
            billboard(m4_point(M, V(fl ? -0.32f : 0.32f, S->H + 0.1f, 0.1f)), 0.9f, fl ? V(4, 0.2f, 0.1f) : V(0.2f, 0.6f, 5), 1, 0);
        }
    }
    /* street lamp glows */
    if (night > 0.05f)
        for (int i = 0; i < NPROP; i++) {
            Prop *q = &prop[i];
            if (q->down || fabsf(q->pos.x - cp.x) > 230 || fabsf(q->pos.z - cp.z) > 230 || !prop_exists(i)) continue;
            int k = i % 4;
            float ax = k & 1 ? -1 : 1, az = k & 2 ? -1 : 1;
            billboard(add(q->pos, V(ax * 1.2f, 6.05f, az * 1.2f)), 0.7f, V(2.2f, 1.5f, 0.7f), night * 0.8f, 0);
        }
    /* markers */
    if (G.state == ST_PLAY || G.state == ST_PAUSE) {
        bool idle = mission_marker_active();
        for (int k = 0; k < npoi; k++) {
            if (dist2d(poi[k].pos, cp) > 450) continue;
            if (poi[k].type == POI_POLICE) continue;
            if (poi[k].type == POI_JOB && !idle) continue;
            marker_beam(poi[k].pos, mul(poi_color(poi[k].type), 1.6f), poi[k].type == POI_JOB ? 30 : 10, t);
        }
        if (idle)
            for (int r = 0; r < NRACE; r++)
                if (dist2d(races[r].start, cp) < 450) marker_beam(races[r].start, V(1.8f, 1.8f, 2.0f), 30, t);
        v3 tg;
        if (mission_target(&tg) && MS.type != MT_RACE) marker_beam(tg, V(2.4f, 1.8f, 0.3f), 60, t);
        if (MS.type == MT_RACE && MS.stage == 1) billboard(add(races[MS.race].cp[MS.cp], V(0, 5, 0)), 2.5f, V(2, 1.3f, 0.2f), 0.5f, 0);
        for (int k = 0; k < NTAPE; k++) {
            if ((tapes_found & (1u << k)) || dist2d(tapes[k], cp) > 120) continue;
            billboard(add(tapes[k], V(0, 0.95f, 0)), 0.9f, V(0.3f, 1.0f, 2.0f), 0.6f + 0.3f * sinf(t * 4 + k), 0);
        }
        if (MS.type == MT_TAXI && MS.stage == 0 && MS.fare_ped >= 0 && ped[MS.fare_ped].on) marker_beam(ped[MS.fare_ped].pos, V(2.4f, 1.8f, 0.3f), 14, t);
    }
    /* helicopter searchlight */
    if (G.heli_t > 0) {
        float a = t * 0.35f;
        v3 tp = player_target_pos();
        v3 hp = add(tp, V(cosf(a) * 30, 41, sinf(a) * 30));
        beam(hp, add(tp, V(0, 0.5f, 0)), 2.2f, V(0.6f, 0.6f, 0.65f), 0.25f);
        ground_ring(add(tp, V(0, 0.1f, 0)), 7, V(1.2f, 1.2f, 1.3f), 0.5f);
        billboard(hp, 1.2f, V(4, 4, 4), 1, 0);
    }
    /* lighthouse beam at night */
    if (night > 0.2f) {
        float a = t * 0.8f;
        v3 o = add(LIGHTHOUSE, V(0, 31.5f, 0));
        beam(o, add(o, V(cosf(a) * 260, -6, sinf(a) * 260)), 4, V(1.2f, 1.1f, 0.8f), 0.18f * night);
        billboard(o, 2.2f, V(4, 3.6f, 2.4f), night, 0);
    }
    /* rain */
    if (G.rain > 0.02f) {
        int n = (int)(G.rain * 900);
        v3 fall = V(1.5f, -16, 0.8f);
        for (int k = 0; k < n; k++) {
            float hx = hash2f(k, 1, 7), hy = hash2f(k, 2, 7), hz = hash2f(k, 3, 7);
            float y = fmodf(hy * 30 - t * 16 + 1000, 30) - 8;
            v3 p = add(cp, V(hx * 50 - 25, y, hz * 50 - 25));
            beam(p, add(p, mul(fall, 0.035f)), 0.012f, V(0.5f, 0.55f, 0.65f), 0.5f * (night * 0.5f + 0.5f));
        }
    }
}

/* ---- the full frame */
static m4 light_vp;
static void setup_shadow(void) {
    v3 focus = add(cam.pos, mul(V(cam.fwd.x, 0, cam.fwd.z), 50));
    focus.y = 0;
    v3 dir = LIGHT_DIR;
    v3 right = norm(cross(dir, V(0, 1, 0))), up = cross(right, dir);
    float ext = 120, texel = 2 * ext / SHADOW_RES;
    float cx = floorf(dot(focus, right) / texel) * texel, cy = floorf(dot(focus, up) / texel) * texel, cz = dot(focus, dir);
    v3 c = add(add(mul(right, cx), mul(up, cy)), mul(dir, cz));
    light_vp = m4_mul(m4_ortho(-ext, ext, -ext, ext, 1, 1600), m4_look(add(c, mul(dir, 700)), c, V(0, 1, 0)));
}
static void render_scene(float t) {
    gather_lights(t);
    clear_dynamic();
    v3 cp = cam.pos;
    for (int i = 0; i < MAXCAR; i++)
        if (car[i].on && dist2d(car[i].pos, cp) < 420) push_car(&car[i], t);
    for (int i = 0; i < MAXPED; i++) {
        Ped *p = &ped[i];
        if (!p->on || dist2d(p->pos, cp) > 200) continue;
        float lie = p->state == PS_DOWN ? (p->pos.y > ground_at(p->pos.x, p->pos.z, p->pos.y, 0.3f) + 0.05f ? 0 : -1.5f) : 0;
        push_human(add(p->pos, V(0, lie != 0 ? 0.15f : 0, 0)), p->yaw, p->anim, len2d(p->vel), p->shirt, p->pants, p->skin, p->hair, p->h, lie, p->state == PS_DOWN ? p->tumble : 0, p->state == PS_HAIL, p->cop);
    }
    if (PL.car < 0 && G.state != ST_TITLE) {
        float lie = PL.down_t > 0 ? -1.2f : 0;
        v3 at = PL.pos;
        if (PL.swim) at.y = WATER_Y - 1.15f;
        push_human(at, PL.yaw, PL.anim, len2d(PL.vel), V(0.85f, 0.45f, 0.1f), V(0.12f, 0.14f, 0.22f), V(0.75f, 0.55f, 0.42f), V(0.08f, 0.06f, 0.05f), 1.0f, lie, 0, false, false);
    }
    push_world_dynamic(t);
    upload_dynamic();
    setup_shadow();
    /* shadow pass */
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_shadow);
    glViewport(0, 0, SHADOW_RES, SHADOW_RES);
    glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(2.5f, 4.0f);
    glUseProgram(P_DEPTH);
    glUniformMatrix4fv(U(P_DEPTH, "uLightVP"), 1, GL_FALSE, light_vp.m);
    if (LIGHT_COL.x + LIGHT_COL.y > 0.05f) {
        int sg = stat_count[MS_GROUND], so = stat_count[MS_OCEAN];
        stat_count[MS_GROUND] = stat_count[MS_OCEAN] = 0;
        int dg = DI[MS_OCEAN].n;
        DI[MS_OCEAN].n = 0;
        draw_all_instances();
        stat_count[MS_GROUND] = sg;
        stat_count[MS_OCEAN] = so;
        DI[MS_OCEAN].n = dg;
    }
    glDisable(GL_POLYGON_OFFSET_FILL);
    /* main pass */
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_msaa);
    glViewport(0, 0, rw, rh);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glUseProgram(P_SKY);
    m4 inv = m4_inv(cam.vp);
    glUniformMatrix4fv(U(P_SKY, "uInvVP"), 1, GL_FALSE, inv.m);
    glUniform3f(U(P_SKY, "uSunDir"), SUN_DIR.x, SUN_DIR.y, SUN_DIR.z);
    glUniform1f(U(P_SKY, "uNight"), NIGHT);
    glUniform1f(U(P_SKY, "uRain"), G.rain);
    glUniform1f(U(P_SKY, "uTime"), t);
    glBindVertexArray(vao_empty);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glUseProgram(P_LIT);
    glUniformMatrix4fv(U(P_LIT, "uVP"), 1, GL_FALSE, cam.vp.m);
    glUniformMatrix4fv(U(P_LIT, "uLightVP"), 1, GL_FALSE, light_vp.m);
    glUniform3f(U(P_LIT, "uCam"), cp.x, cp.y, cp.z);
    glUniform3f(U(P_LIT, "uSunDir"), SUN_DIR.x, SUN_DIR.y, SUN_DIR.z);
    glUniform3f(U(P_LIT, "uLightDir"), LIGHT_DIR.x, LIGHT_DIR.y, LIGHT_DIR.z);
    glUniform3f(U(P_LIT, "uLightCol"), LIGHT_COL.x, LIGHT_COL.y, LIGHT_COL.z);
    glUniform3f(U(P_LIT, "uAmbSky"), AMB_SKY.x, AMB_SKY.y, AMB_SKY.z);
    glUniform3f(U(P_LIT, "uAmbGnd"), AMB_GND.x, AMB_GND.y, AMB_GND.z);
    glUniform1f(U(P_LIT, "uFogDen"), FOG_DEN);
    glUniform1f(U(P_LIT, "uNight"), NIGHT);
    glUniform1f(U(P_LIT, "uRain"), G.rain);
    glUniform1f(U(P_LIT, "uTime"), t);
    glUniform1f(U(P_LIT, "uPitch"), PITCH);
    glUniform1f(U(P_LIT, "uCityN"), (float)NB);
    glUniform1f(U(P_LIT, "uWater"), WATER_Y);
    glUniform1i(U(P_LIT, "uShadow"), 0);
    glUniform1i(U(P_LIT, "uCity"), 1);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, tex_city);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex_shadow);
    float lp[32 * 4], lc[32 * 3];
    for (int i = 0; i < nframe_lights; i++) {
        lp[i * 4] = frame_lights[i].pos.x; lp[i * 4 + 1] = frame_lights[i].pos.y; lp[i * 4 + 2] = frame_lights[i].pos.z; lp[i * 4 + 3] = frame_lights[i].radius;
        lc[i * 3] = frame_lights[i].col.x; lc[i * 3 + 1] = frame_lights[i].col.y; lc[i * 3 + 2] = frame_lights[i].col.z;
    }
    glUniform1i(U(P_LIT, "uNumLights"), nframe_lights);
    if (nframe_lights) {
        glUniform4fv(U(P_LIT, "uLightPos"), nframe_lights, lp);
        glUniform3fv(U(P_LIT, "uLightCols"), nframe_lights, lc);
    }
    draw_all_instances();
    glDisable(GL_CULL_FACE);
    build_glows(t);
    flush_particles(cam.vp);
    /* resolve, bloom, composite */
    glDisable(GL_DEPTH_TEST);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo_msaa);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo_hdr);
    glBlitFramebuffer(0, 0, rw, rh, 0, 0, rw, rh, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindVertexArray(vao_empty);
    glUseProgram(P_DOWN);
    glUniform1i(U(P_DOWN, "uSrc"), 0);
    glActiveTexture(GL_TEXTURE0);
    for (int i = 0; i < NMIP; i++) {
        glBindFramebuffer(GL_FRAMEBUFFER, fbo_mip[i]);
        glViewport(0, 0, mip_w[i], mip_h[i]);
        int sw = i == 0 ? rw : mip_w[i - 1], sh = i == 0 ? rh : mip_h[i - 1];
        glBindTexture(GL_TEXTURE_2D, i == 0 ? tex_hdr : tex_mip[i - 1]);
        glUniform2f(U(P_DOWN, "uTexel"), 1.0f / sw, 1.0f / sh);
        glUniform1i(U(P_DOWN, "uFirst"), i == 0);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }
    glUseProgram(P_UP);
    glUniform1i(U(P_UP, "uSrc"), 0);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    for (int i = NMIP - 1; i > 0; i--) {
        glBindFramebuffer(GL_FRAMEBUFFER, fbo_mip[i - 1]);
        glViewport(0, 0, mip_w[i - 1], mip_h[i - 1]);
        glBindTexture(GL_TEXTURE_2D, tex_mip[i]);
        glUniform2f(U(P_UP, "uTexel"), 1.0f / mip_w[i], 1.0f / mip_h[i]);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }
    glDisable(GL_BLEND);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_ldr);
    glViewport(0, 0, rw, rh);
    glUseProgram(P_COMP);
    glUniform1i(U(P_COMP, "uHdr"), 0);
    glUniform1i(U(P_COMP, "uBloom"), 1);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, tex_mip[0]);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex_hdr);
    glUniform1f(U(P_COMP, "uExposure"), EXPOSURE);
    glUniform1f(U(P_COMP, "uBloomAmt"), 0.14f + NIGHT * 0.08f);
    glUniform1f(U(P_COMP, "uDamage"), G.damage_fx);
    glUniform1f(U(P_COMP, "uTime"), t);
    float gray = G.state == ST_WASTED ? clampf(G.st, 0, 1) : G.state == ST_BUSTED ? clampf(G.st, 0, 1) * 0.6f : clampf(1 - PL.hp / 30, 0, 1) * 0.5f;
    glUniform1f(U(P_COMP, "uGray"), gray);
    glUniform1f(U(P_COMP, "uFade"), G.fade);
    glUniform1f(U(P_COMP, "uSpeed"), G.speed_fx);
    glUniform3f(U(P_COMP, "uSunDir"), SUN_DIR.x, SUN_DIR.y, SUN_DIR.z);
    glDrawArrays(GL_TRIANGLES, 0, 3);
}

/* ================================================================== camera */
static void update_camera(const Input *in, float dt, float t) {
    float zoom = PL.cam_zoom > 0 ? PL.cam_zoom : 1;
    v3 target;
    float dist, fov = G.fov_base;
    PL.cam_pitch = clampf(PL.cam_pitch + in->look_y, -1.25f, 0.6f);
    PL.cam_yaw = wrap_pi(PL.cam_yaw + in->look_x);
    bool looked = fabsf(in->look_x) + fabsf(in->look_y) > 1e-4f;
    if (looked) PL.look_t = 0; else PL.look_t += dt;
    if (PL.car >= 0) {
        Car *c = &car[PL.car];
        const CarSpec *S = &SPEC[c->model];
        float sp = car_speed(c);
        target = add(c->pos, V(0, S->H + 0.75f, 0));
        if (PL.look_t > 1.2f || in->cam_reset) {
            float want = c->yaw;
            if (sp < -3) want = c->yaw;
            float k = fminf(1, dt * (2 + fabsf(sp) * 0.08f));
            PL.cam_yaw = wrap_pi(PL.cam_yaw + wrap_pi(want - PL.cam_yaw) * k);
            PL.cam_pitch += (-0.17f - PL.cam_pitch) * fminf(1, dt * 2);
        }
        dist = (4.0f + S->L * 0.85f) * zoom + fabsf(sp) * 0.035f;
        fov += clampf(fabsf(sp) * 0.28f, 0, 20);
        G.speed_fx = clampf((fabsf(sp) - 25) / 40, 0, 0.6f);
    } else {
        target = add(PL.pos, V(0, 1.55f, 0));
        if (PL.swim) target.y = WATER_Y + 0.6f;
        dist = 4.2f * zoom;
        G.speed_fx = 0;
    }
    float cp = cosf(PL.cam_pitch);
    v3 dir = V(sinf(PL.cam_yaw) * cp, sinf(PL.cam_pitch), -cosf(PL.cam_yaw) * cp);
    float hit = ray_world(target, mul(dir, -1), dist + 0.4f);
    float d = fminf(dist, hit - 0.4f);
    if (d < 0.8f) d = 0.8f;
    v3 eye = sub(target, mul(dir, d));
    float g = terrain_h(eye.x, eye.z);
    if (eye.y < g + 0.4f) eye.y = g + 0.4f;
    if (eye.y < WATER_Y + 0.3f && water_at(eye.x, eye.z)) eye.y = WATER_Y + 0.3f;
    float sh = G.shake * G.shake;
    eye = add(eye, V(frs() * sh * 0.3f, frs() * sh * 0.3f, frs() * sh * 0.3f));
    PL.cam_eye = eye;
    (void)t;
    setup_camera(eye, add(eye, dir), fov);
}

/* ================================================================== simulation driver */
static float spawn_tick;
static void manage_population(float dt, bool allow_visible) {
    spawn_tick -= dt;
    if (spawn_tick > 0) return;
    spawn_tick = 0.2f;
    int ai = count_cars(DRV_AI);
    for (int k = 0; k < 3 && ai < traffic_target; k++, ai++) spawn_traffic(allow_visible ? 30 : 160, 300, allow_visible);
    spawn_parked();
    int np = 0;
    for (int i = 0; i < MAXPED; i++) np += ped[i].on;
    if (np < ped_target) spawn_peds(allow_visible ? 20 : 70, 220, 4);
}
static void update_audio_layers(void) {
    audio_listener(cam.pos.x, cam.pos.y, cam.pos.z, cam.right.x, cam.right.y, cam.right.z);
    if (PL.car >= 0 && G.state == ST_PLAY) {
        Car *c = &car[PL.car];
        audio_engine(!c->wrecked && !c->sunk, c->rpm, c->thr, SPEC[c->model].engine);
        float lat = fabsf(dot(c->vel, right_of(c->yaw)));
        audio_skid(c->airborne ? 0 : clampf((lat - 2.5f) / 7, 0, 1) + (c->hb && fabsf(car_speed(c)) > 6 ? 0.4f : 0));
        audio_radio(G.radio);
    } else {
        audio_engine(false, 0, 0, 0);
        audio_skid(0);
        audio_radio(-1);
    }
    float best = 1e9f;
    v3 bp = V(0, 0, 0);
    for (int i = 0; i < MAXCAR; i++)
        if (car[i].on && car[i].siren && !car[i].wrecked) { float d = len(sub(car[i].pos, cam.pos)); if (d < best) { best = d; bp = car[i].pos; } }
    if (best < 250) audio_siren(clampf(1 - best / 250, 0, 1), clampf(dot(norm(sub(bp, cam.pos)), cam.right), -1, 1));
    else audio_siren(0, 0);
    float od = out_dist(cam.pos.x, cam.pos.z);
    audio_ambience(clampf(1 - od / 150, 0.1f, 1) * (1 - NIGHT * 0.4f), clampf(G.rain * 0.8f + fmaxf(cam.pos.y - 30, 0) / 100, 0, 1), clampf((od - 20) / 100, 0, 1));
}
static void sim_step(const Input *in, float dt) {
    G.time += dt;
    if (G.state == ST_PLAY) {
        play_time += dt;
        update_player(in, dt);
        update_wanted(dt);
        update_missions(dt);
        if (PL.hp <= 0) start_wasted();
    }
    G.tod = fmodf(G.tod + dt * (24.0f / (16 * 60)), 24);
    update_weather(dt);
    update_env();
    update_cars(dt);
    update_peds(dt);
    update_props(dt);
    update_particles(dt);
    manage_population(dt, false);
    G.damage_fx = fmaxf(0, G.damage_fx - dt * 1.2f);
    G.shake = fmaxf(0, G.shake - dt * 2.2f);
    G.msg_t -= dt; G.big_t -= dt; G.toast_t -= dt; G.radio_t -= dt;
}
static void new_game(void) {
    memset(car, 0, sizeof(car));
    memset(ped, 0, sizeof(ped));
    memset(part, 0, sizeof(part));
    memset(slot_used, 0, sizeof(slot_used));
    props_init();
    memset(&MS, 0, sizeof(MS));
    MS.fare_ped = -1;
    int k = nearest_poi(POI_SAFE, V(0, 0, 0));
    PL.pos = add(poi[k].pos, V(0, 0, -1.5f));
    PL.vel = V(0, 0, 0);
    PL.hp = 100;
    PL.car = -1;
    PL.last_car = -1;
    PL.down_t = 0;
    PL.cam_yaw = 3.14159f;
    PL.cam_pitch = -0.15f;
    PL.cam_zoom = 1;
    G.heat = 0; G.stars = 0; G.bust_t = 0;
    G.state = ST_PLAY;
    G.st = 0;
    G.fade = 0;
    /* a car waiting at the safehouse */
    Car *c = spawn_car(VM_MUSCLE, add(poi[k].pos, V(6, 0, -3.2f)), 1.5708f, DRV_NONE);
    if (c) { c->col = V(0.55f, 0.06f, 0.05f); c->keep = true; }
    for (int i = 0; i < 40; i++) spawn_traffic(40, 300, true);
    spawn_peds(15, 200, 60);
    bigtext("PORT CALDER", "TAB FOR THE MAP - YELLOW MARKERS ARE COURIER JOBS", 4);
}

/* ================================================================== self-test helpers */
static void save_bmp(const char *path) {
    unsigned char *px = malloc((size_t)rw * rh * 4);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo_ldr);
    glReadPixels(0, 0, rw, rh, GL_BGRA, GL_UNSIGNED_BYTE, px);
    SDL_Surface *f = SDL_CreateRGBSurfaceWithFormat(0, rw, rh, 32, SDL_PIXELFORMAT_ARGB8888);
    if (f) {
        for (int y = 0; y < rh; y++) memcpy((Uint8 *)f->pixels + y * f->pitch, px + (size_t)(rh - 1 - y) * rw * 4, (size_t)rw * 4);
        SDL_SaveBMP(f, path);
    }
    SDL_FreeSurface(f);
    free(px);
}
static void render_frame(float t);
/* autopilot used by the self-test: routes over the grid to a target like the police do */
static Route bot_route;
static int bot_ti = -1, bot_tj = -1, bot_i, bot_j, bot_dir = -1;
static float bot_stuck, bot_back;
static void bot_drive(Input *in, v3 target, float cruise, float dt) {
    Car *c = &car[PL.car];
    memset(in, 0, sizeof(*in));
    int ti, tj;
    nearest_road_node(target, &ti, &tj);
    if (ti != bot_ti || tj != bot_tj) { route_build(&bot_route, ti, tj); bot_ti = ti; bot_tj = tj; bot_dir = -1; }
    float dt_target = dist2d(c->pos, target);
    v3 aim;
    float want = cruise;
    if (bot_dir < 0) {
        int d = ((int)floorf(wrap_pi(c->yaw - 1.5708f) / 1.5708f + 0.5f) + 4) % 4; /* cardinal heading: 0 +x, 1 +z, 2 -x, 3 -z */
        float fx = c->pos.x / PITCH, fz = c->pos.z / PITCH;
        bot_i = d == 0 ? (int)ceilf(fx) : d == 2 ? (int)floorf(fx) : (int)floorf(fx + 0.5f);
        bot_j = d == 1 ? (int)ceilf(fz) : d == 3 ? (int)floorf(fz) : (int)floorf(fz + 0.5f);
        bot_i = (int)clampf(bot_i, 0, NB); bot_j = (int)clampf(bot_j, 0, NB);
        bot_dir = d;
    }
    v3 node = node_pos(bot_i, bot_j);
    if (bot_dir < 4 && bot_route.d[bot_i][bot_j] > 0) {
        int nd = route_next_dir(&bot_route, bot_i, bot_j, (bot_dir + 2) % 4);
        if (nd >= 0 && nd != bot_dir && dist2d(c->pos, node) < 40) want = fminf(want, 9);
    }
    if (dist2d(c->pos, node) < 4.5f) {
        if (bot_route.d[bot_i][bot_j] == 0) bot_dir = 5;
        else {
            int d = route_next_dir(&bot_route, bot_i, bot_j, bot_dir < 4 ? (bot_dir + 2) % 4 : -1);
            if (d >= 0) { bot_i += DX[d]; bot_j += DZ[d]; bot_dir = d; }
        }
        node = node_pos(bot_i, bot_j);
    }
    aim = bot_dir < 4 ? add(node, mul(V(-dir_vec(bot_dir).z, 0, dir_vec(bot_dir).x), LANE_OFF)) : node;
    if (bot_dir == 5 || dt_target < 45) { aim = target; want = fminf(want, fmaxf(dt_target * 0.5f, 2)); }
    float err = wrap_pi(yaw_of(sub(aim, c->pos)) - c->yaw);
    float dn = dist2d(c->pos, aim);
    if (fabsf(err) > 0.35f && dn < 30) want = fminf(want, 9);
    if (fabsf(err) > 0.6f) want = fminf(want, 7);
    float gap;
    bool pl;
    obstacle_ahead(c, &gap, &pl);
    if (gap < 30) want = fminf(want, fmaxf(gap - 3, 0) * 0.9f);
    float sp = car_speed(c);
    in->fwd = sp < want ? 1 : (sp > want + 2 ? -1 : 0);
    in->side = clampf(err * 2.5f, -1, 1);
    if (fabsf(sp) < 1 && want > 3) bot_stuck += dt; else bot_stuck = fmaxf(0, bot_stuck - dt);
    if (bot_stuck > 2.5f) { bot_back = 1.2f; bot_stuck = 0; }
    if (bot_back > 0) { bot_back -= dt; in->fwd = -1; in->side = -in->side; }
    if (gap < 4 && fabsf(sp) < 1) in->horn = true;
}
static void bot_put_in_car(v3 at, float yaw, int model) {
    if (PL.car >= 0) { car[PL.car].on = false; PL.car = -1; }
    Car *c = spawn_car(model, at, yaw, DRV_PLAYER);
    PL.car = (int)(c - car);
    PL.last_car = PL.car;
    c->keep = true;
    bot_dir = -1;
    bot_ti = -1;
}
static bool any_nan(void) {
    for (int i = 0; i < MAXCAR; i++) if (car[i].on && (isnan(car[i].pos.x) || isnan(car[i].pos.y) || isnan(car[i].yaw))) return true;
    for (int i = 0; i < MAXPED; i++) if (ped[i].on && (isnan(ped[i].pos.x) || isnan(ped[i].pos.y))) return true;
    return isnan(PL.pos.x) || isnan(PL.pos.y);
}

static int run_selftest(const char *dir) {
    char path[1200];
    int fails = 0;
#define SHOT(name) do { snprintf(path, sizeof(path), "%s/%s.bmp", dir, name); save_bmp(path); } while (0)
#define CHECK(cond, ...) do { bool ok_ = (cond); printf("  [%s] ", ok_ ? "ok" : "FAIL"); printf(__VA_ARGS__); printf("\n"); if (!ok_) fails++; } while (0)
    printf("GL: %s | %s\n", glGetString(GL_VERSION), glGetString(GL_RENDERER));
    CHECK(shader_errors == 0, "shaders compiled and linked");
    CHECK(audio_selftest() == 0, "audio module self-test");
    int ninst = 0;
    for (int i = 0; i < MS_N; i++) ninst += stat_count[i];
    printf("world: %d collision boxes, %d static instances, %d parking slots, %d lights, %d tapes, %d ramps, %d POIs\n", nbox, ninst, nslot, nslight, NTAPE, nramp, npoi);
    /* world sanity */
    int bad_lane = 0, lanes = 0;
    for (int i = 0; i <= NB; i++)
        for (int j = 0; j <= NB; j++)
            for (int d = 0; d < 4; d++) {
                if (!edge_ok(i, j, d)) continue;
                for (float s = 10; s < PITCH - 10; s += 4) { lanes++; if (point_blocked(lane_pt(i, j, d, s, LANE_OFF), 1.0f, 1.5f)) bad_lane++; }
            }
    for (int i = 0; i <= NB; i++) for (int j = 0; j <= NB; j++) for (int d = 0; d < 4; d++) { if (!edge_ok(i, j, d)) continue; for (float s2 = 10; s2 < PITCH - 10; s2 += 4) if (point_blocked(lane_pt(i, j, d, s2, LANE_OFF), 1.0f, 1.5f)) printf("    blocked lane %d,%d dir %d s %.0f\n", i, j, d, s2); }
    CHECK(bad_lane == 0, "traffic lanes clear of buildings (%d samples, %d blocked)", lanes, bad_lane);
    int bad_poi = 0;
    for (int k = 0; k < npoi; k++) if (point_blocked(add(poi[k].pos, V(0, 0.1f, 0)), 1.5f, 1.8f) || water_at(poi[k].pos.x, poi[k].pos.z)) { bad_poi++; printf("    blocked poi %d type %d at %.0f %.0f\n", k, poi[k].type, poi[k].pos.x, poi[k].pos.z); }
    CHECK(bad_poi == 0, "all %d points of interest reachable", npoi);
    int bad_tape = 0;
    for (int k = 0; k < NTAPE; k++) if (tapes[k].x == 0 && tapes[k].z == 0) bad_tape++;
    CHECK(bad_tape == 0, "%d lost tapes placed", NTAPE);
    int short_race = 0;
    for (int r = 0; r < NRACE; r++) { printf("    race %-15s %2d checkpoints, par %.0f s\n", races[r].name, races[r].ncp, races[r].par); if (races[r].ncp < 7) short_race++; }
    CHECK(short_race == 0, "every race has at least 7 checkpoints");
    int bad_slot = 0;
    for (int k = 0; k < nslot; k++) if (point_blocked(add(slots[k].pos, V(0, 0.2f, 0)), 0.9f, 1.4f)) bad_slot++;
    CHECK(bad_slot <= nslot / 20, "parking slots clear (%d of %d blocked)", bad_slot, nslot);

    /* title */
    G.state = ST_TITLE;
    G.tod = 19.0f;
    update_env();
    PL.pos = V(CITY / 2, 0, CITY / 2);
    for (int i = 0; i < 30; i++) spawn_traffic(20, 300, true);
    render_frame(4.0f);
    SHOT("01_title_dusk");

    /* performance */
    new_game();
    G.tod = 13;
    update_env();
    Input none = {0};
    for (int i = 0; i < 120; i++) sim_step(&none, 1 / 60.0f);
    glFinish();
    Uint64 t0 = SDL_GetPerformanceCounter();
    for (int i = 0; i < 90; i++) { PL.cam_yaw = i * 0.07f; update_camera(&none, 1 / 60.0f, 0); render_frame(i / 60.0f); }
    glFinish();
    double ms = (double)(SDL_GetPerformanceCounter() - t0) * 1000.0 / SDL_GetPerformanceFrequency() / 90;
    printf("render: %.2f ms per frame at %dx%d (4x MSAA, shadows, bloom, %d lights)\n", ms, rw, rh, nframe_lights);
    t0 = SDL_GetPerformanceCounter();
    for (int i = 0; i < 600; i++) sim_step(&none, 1 / 60.0f);
    double sms = (double)(SDL_GetPerformanceCounter() - t0) * 1000.0 / SDL_GetPerformanceFrequency() / 600;
    printf("simulation: %.3f ms per step (%d cars, peds)\n", sms, count_cars(DRV_AI));
    PL.cam_yaw = 2.4f; PL.cam_pitch = -0.12f;
    update_camera(&none, 0.016f, 0);
    render_frame(10);
    SHOT("02_street_day");

    /* traffic soak: 4 simulated minutes */
    {
        srand(99);
        new_game();
        G.tod = 10;
        PL.pos = V(12 * PITCH + 30, 0, 9 * PITCH + WALK);
        int stuck_long = 0, inside = 0, maxai = 0;
        float stuck_t[MAXCAR] = {0};
        double speed_sum = 0;
        long speed_n = 0;
        for (int step = 0; step < 60 * 240; step++) {
            sim_step(&none, 1 / 60.0f);
            if (step % 30 == 0) {
                int ai = 0;
                for (int i = 0; i < MAXCAR; i++) {
                    Car *c = &car[i];
                    if (!c->on || c->driver != DRV_AI) { stuck_t[i] = 0; continue; }
                    ai++;
                    float sp = fabsf(car_speed(c));
                    speed_sum += sp; speed_n++;
                    if (sp < 0.3f) stuck_t[i] += 0.5f; else stuck_t[i] = 0;
                    if (stuck_t[i] > 45) { stuck_long++; stuck_t[i] = -1000; float gp; bool pp; obstacle_ahead(c, &gp, &pp); printf("    stuck car at %.0f,%.0f yaw %.2f node %d,%d dir %d nwp %d gap %.1f wp0 %.0f,%.0f stop %d light %d\n", c->pos.x, c->pos.z, c->yaw, c->ni, c->nj, c->ndir, c->nwp, gp, c->wp[0].x, c->wp[0].z, c->wstop[0], light_state(c->ni, c->nj, c->ndir)); }
                    if (point_blocked(add(c->pos, V(0, 0.3f, 0)), 0.3f, 1.0f)) inside++;
                }
                if (ai > maxai) maxai = ai;
            }
        }
        printf("traffic: up to %d AI cars, mean speed %.1f m/s over 4 min\n", maxai, speed_n ? speed_sum / speed_n : 0);
        CHECK(maxai >= 25, "traffic population maintained (%d)", maxai);
        CHECK(speed_n && speed_sum / speed_n > 4, "traffic keeps moving");
        CHECK(stuck_long <= 3, "cars stuck for more than 45 s: %d", stuck_long);
        CHECK(inside == 0, "no car inside a building (%d samples)", inside);
        CHECK(!any_nan(), "no NaN positions");
    }

    /* courier job driven by the autopilot */
    {
        srand(7);
        new_game();
        G.tod = 16;
        int jk = -1;
        for (int k = 0; k < npoi; k++) if (poi[k].type == POI_JOB) { jk = k; break; }
        bot_put_in_car(add(poi[jk].pos, V(-14, 0, -WALK + LANE_OFF)), 1.5708f, VM_SEDAN);
        Input in;
        float t = 0;
        bool started = false, done = false;
        int deliveries0 = deliveries;
        int money0 = G.money;
        while (t < 240 && !done) {
            v3 tgt = started ? MS.target : poi[jk].pos;
            bot_drive(&in, tgt, started ? 19 : 8, 1 / 60.0f);
            sim_step(&in, 1 / 60.0f);
            t += 1 / 60.0f;
            if (MS.type == MT_DELIVERY && !started) { started = true; printf("    job started: %.0f m, %.0f s allowed\n", MS.dist, MS.limit); }
            if (started && MS.type == MT_NONE) done = true;
            if (G.state != ST_PLAY) break;
            if (started && fabsf(t - 30) < 0.01f) {
                G.tod = 21; update_env();
                update_camera(&in, 1 / 60.0f, t); render_frame(t);
                SHOT("03_courier_night");
                G.tod = 16;
            }
        }
        CHECK(started, "courier job starts from its marker");
        CHECK(deliveries == deliveries0 + 1, "autopilot delivered the package (%.0f s, +$%d)", t, G.money - money0);
    }

    /* street race */
    {
        srand(11);
        new_game();
        G.tod = 12;
        bot_put_in_car(lane_pt(races[1].i, races[1].j, races[1].dir, 12, LANE_OFF), races[1].yaw, VM_SPORTS);
        Input in;
        float t = 0;
        bool started = false;
        float best0 = race_best[1];
        while (t < 300) {
            v3 tgt = started ? (MS.stage == 1 ? races[1].cp[MS.cp] : car[PL.car].pos) : races[1].start;
            if (started && MS.stage == 0) memset(&in, 0, sizeof(in));
            else bot_drive(&in, tgt, started ? 26 : 5, 1 / 60.0f);
            sim_step(&in, 1 / 60.0f);
            t += 1 / 60.0f;
            if (MS.type == MT_RACE) started = true;

            if (started && MS.type == MT_NONE) break;
            if (started && MS.stage == 1 && MS.cp == 3 && fmodf(t, 1.0f) < 0.017f) {
                update_camera(&in, 1 / 60.0f, t); render_frame(t);
                SHOT("04_race");
            }
        }
        printf("    race end: %s / %s  car %d on %d hp %.0f wrecked %d sunk %d\n", G.big, G.big2, PL.car, PL.car >= 0 ? car[PL.car].on : -1, PL.car >= 0 ? car[PL.car].hp : 0, PL.car >= 0 ? car[PL.car].wrecked : 0, PL.car >= 0 ? car[PL.car].sunk : 0);
        printf("    race state: type %d stage %d cp %d/%d t %.0f car %.0f,%.0f\n", MS.type, MS.stage, MS.cp, races[1].ncp, t, car[PL.car].pos.x, car[PL.car].pos.z);
        CHECK(started, "race starts from its marker");
        CHECK(race_best[1] > 0 && race_best[1] != best0, "autopilot finished %s in %.1f s (par %.0f)", races[1].name, race_best[1], races[1].par);
    }

    /* stunt jump */
    {
        new_game();
        G.tod = 11;
        Ramp *R = &ramps[0];
        v3 back = sub(R->pos, mul(fwd_of(R->yaw), 70));
        back.y = 0;
        bot_put_in_car(back, R->yaw, VM_SPORTS);
        Input in = {0};
        unsigned j0 = jumps_done;
        float maxair = 0;
        bool shot = false;
        for (int s = 0; s < 60 * 10; s++) {
            in.fwd = 1;
            in.side = clampf(wrap_pi(yaw_of(sub(add(R->pos, mul(fwd_of(R->yaw), 30)), car[PL.car].pos)) - car[PL.car].yaw) * 2, -1, 1);
            sim_step(&in, 1 / 60.0f);
            if (car[PL.car].airborne && car[PL.car].air_t > maxair) maxair = car[PL.car].air_t;
            if (!shot && car[PL.car].airborne && car[PL.car].air_t > 0.5f) {
                shot = true;
                update_camera(&in, 1 / 60.0f, 0);
                PL.cam_yaw = R->yaw + 1.2f;
                update_camera(&none, 1 / 60.0f, 0);
                render_frame(s / 60.0f);
                SHOT("05_stunt_jump");
            }
        }
        CHECK(maxair > 1.0f && jumps_done != j0, "stunt ramp launches the car (%.2f s of air)", maxair);
    }

    /* knocking over a pedestrian raises the wanted level, then the police arrive and bust you */
    {
        srand(5);
        new_game();
        G.tod = 22.5f;
        G.rain = 0.8f; G.rain_target = 0.8f; G.rain_timer = 999;
        v3 start = lane_pt(10, 9, 0, 10, LANE_OFF);
        bot_put_in_car(start, yaw_of(dir_vec(0)), VM_SEDAN);
        Ped *p = spawn_ped(lane_pt(10, 9, 0, 40, LANE_OFF), false);
        p->state = PS_IDLE;
        Input in = {0};
        for (int s = 0; s < 60 * 4 && p->state != PS_DOWN; s++) { in.fwd = 1; in.side = 0; sim_step(&in, 1 / 60.0f); }
        CHECK(p->state == PS_DOWN, "car knocks the pedestrian over");
        CHECK(G.stars >= 0 && G.heat > 0, "wanted heat rises (%.2f)", G.heat);
        G.heat = 3.2f; G.stars = 3;
        float t = 0;
        bool shot = false;
        memset(&in, 0, sizeof(in));
                while (t < 120 && G.state == ST_PLAY) {
            sim_step(&in, 1 / 60.0f);
            t += 1 / 60.0f;
            if (!shot && count_cops_near(30) >= 2) {
                shot = true;
                PL.cam_yaw = car[PL.car].yaw + 2.6f;
                PL.look_t = 0;
                update_camera(&none, 1 / 60.0f, t);
                render_frame(t);
                SHOT("06_police_night_rain");
            }
        }
        printf("    bust test: state %d hp %.0f car hp %.0f\n", G.state, PL.hp, PL.car >= 0 ? car[PL.car].hp : -1);
        CHECK(G.state == ST_BUSTED, "the police bust a stationary player (%.0f s)", t);
        for (int s = 0; s < 60 && G.state == ST_BUSTED; s++) { G.st += 0.05f; update_camera(&none, 0.05f, t); }
        render_frame(t);
        SHOT("07_busted");
        respawn_at(POI_POLICE);
        CHECK(G.state == ST_PLAY && G.stars == 0 && PL.car < 0, "respawn at the police station with a clean record");
        G.rain = 0; G.rain_target = 0;
    }

    /* car fire and explosion with the player inside */
    {
        new_game();
        bot_put_in_car(lane_pt(14, 3, 1, 20, LANE_OFF), yaw_of(dir_vec(1)), VM_SEDAN);
        car[PL.car].hp = 5;
        Input in = {0};
        for (int s = 0; s < 60 * 10 && G.state == ST_PLAY; s++) sim_step(&in, 1 / 60.0f);
        CHECK(G.state == ST_WASTED, "a burning car explodes and wastes the driver");
        G.state = ST_PLAY;
        respawn_at(POI_HOSPITAL);
    }

    /* scenery screenshots */
    struct { const char *name; v3 eye, at; float tod, rain; } views[] = {
        {"08_downtown_sunset", {12 * PITCH + 3, 2.0f, 14 * PITCH + 30}, {12 * PITCH - 20, 50, 10 * PITCH}, 18.3f, 0},
        {"09_skyline_night", {6 * PITCH, 90, 18 * PITCH}, {12 * PITCH, 60, 10 * PITCH}, 23.0f, 0},
        {"10_beach_wheel", {CITY + 60, 4, 620}, {CITY + 290, 25, 700}, 17.6f, 0},
        {"11_docks", {420, 18, CITY + 10}, {600, 4, CITY + 150}, 9.5f, 0},
        {"12_maple_heights", {2 * PITCH + 20, 3, 9 * PITCH + 4}, {3 * PITCH, 3, 7 * PITCH}, 15.0f, 0},
        {"13_rain_midtown", {16 * PITCH + 2, 1.6f, 6 * PITCH + 20}, {16 * PITCH + 2, 6, 4 * PITCH}, 20.5f, 0.9f},
        {"14_park_aerial", {2 * PITCH, 120, 9 * PITCH}, {5 * PITCH, 0, 5.5f * PITCH}, 11.0f, 0},
    };
    for (size_t v = 0; v < sizeof(views) / sizeof(views[0]); v++) {
        new_game();
        G.state = ST_TITLE;
        G.tod = views[v].tod;
        G.rain = views[v].rain;
        PL.pos = views[v].at;
        PL.pos.y = 0;
        for (int i = 0; i < 40; i++) spawn_traffic(10, 250, true);
        spawn_peds(5, 120, 60);
        Input in = {0};
        setup_camera(views[v].eye, views[v].at, 70);
        for (int s = 0; s < 90; s++) sim_step(&in, 1 / 60.0f);
        G.tod = views[v].tod;
        G.rain = views[v].rain;
        update_env();
        setup_camera(views[v].eye, views[v].at, 70);
        render_scene(20 + v);
        hud_flush();
        SHOT(views[v].name);
    }
    G.rain = 0;
    /* HUD + map */
    new_game();
    G.tod = 19.2f;
    G.money = 48250;
    G.heat = 2.4f; G.stars = 2;
    bot_put_in_car(lane_pt(11, 8, 0, 30, LANE_OFF), yaw_of(dir_vec(0)), VM_TAXI);
    car[PL.car].vel = mul(fwd_of(car[PL.car].yaw), 20);
    toggle_taxi();
    for (int s = 0; s < 40; s++) sim_step(&none, 1 / 60.0f);
    update_camera(&none, 1 / 60.0f, 0);
    render_frame(30);
    SHOT("15_hud_taxi");
    G.state = ST_MAP;
    render_frame(30);
    SHOT("16_map");
    G.state = ST_PAUSE;
    render_frame(30);
    SHOT("17_pause");
    CHECK(!any_nan(), "no NaN after all scenarios");
    printf("selftest: %s (%d failures)\n", fails ? "FAILED" : "PASSED", fails);
    return fails ? 1 : 0;
}

/* ================================================================== frame + main loop */
static void render_frame(float t) {
    if (G.state == ST_TITLE) {
        float a = t * 0.045f;
        v3 c = V(12 * PITCH, 0, 11 * PITCH);
        v3 eye = add(c, V(cosf(a) * 420, 120 + sinf(t * 0.1f) * 20, sinf(a) * 420));
        setup_camera(eye, add(c, V(0, 40, 0)), 60);
    }
    render_scene(t);
    draw_hud(t);
}

int main(int argc, char **argv) {
    const char *shotdir = NULL;
    for (int i = 1; i < argc; i++)
        if (!strcmp(argv[i], "--selftest") && i + 1 < argc) shotdir = argv[++i];
    selftest = shotdir != NULL;
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) { fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 1; }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_Window *win = SDL_CreateWindow("Port Calder", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720,
                                       SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI | (selftest ? SDL_WINDOW_HIDDEN : 0));
    if (!win) { fprintf(stderr, "window: %s\n", SDL_GetError()); return 1; }
    SDL_GLContext ctx = SDL_GL_CreateContext(win);
    if (!ctx) { fprintf(stderr, "OpenGL 3.3 context: %s\n", SDL_GetError()); return 1; }
    SDL_GL_SetSwapInterval(selftest ? 0 : -1);
    if (!selftest && SDL_GL_GetSwapInterval() == 0) SDL_GL_SetSwapInterval(1);
    Uint64 tb = SDL_GetPerformanceCounter();
    init_gl();
    build_city();
    upload_static();
    upload_city_tex();
    props_init();
    build_races();
    build_tapes();
    int dw, dh;
    SDL_GL_GetDrawableSize(win, &dw, &dh);
    if (selftest) { dw = 1280; dh = 720; }
    make_targets(dw, dh);
    G.sens = 0.0024f;
    G.fov_base = 68;
    G.money = 500;
    G.tod = 17.2f;
    G.radio = 0;
    G.rain_timer = 200;
    char *pref = SDL_GetPrefPath("doomedl", "port-calder");
    if (pref && !selftest) {
        snprintf(save_path, sizeof(save_path), "%ssave.txt", pref);
        load_game();
    }
    if (pref) SDL_free(pref);
    bool audio_ok = false;
    if (!selftest && SDL_InitSubSystem(SDL_INIT_AUDIO) == 0) audio_ok = audio_init(true);
    if (selftest) audio_init(false);
    if (!selftest) printf("Port Calder: city built in %.2f s%s\n", (double)(SDL_GetPerformanceCounter() - tb) / SDL_GetPerformanceFrequency(), audio_ok ? "" : " (no audio)");
    update_env();
    if (selftest) {
        int rc = run_selftest(shotdir);
        audio_shutdown();
        SDL_Quit();
        return rc;
    }
    SDL_GameController *pad = NULL;
    for (int i = 0; i < SDL_NumJoysticks() && !pad; i++)
        if (SDL_IsGameController(i)) pad = SDL_GameControllerOpen(i);
    G.state = ST_TITLE;
    PL.pos = V(12 * PITCH, 0, 11 * PITCH);
    PL.car = -1;
    for (int i = 0; i < 30; i++) spawn_traffic(30, 300, true);
    Uint64 last = SDL_GetPerformanceCounter(), freq = SDL_GetPerformanceFrequency();
    float t = 0, mdx = 0, mdy = 0;
    bool running = true, muted = false;
    Input edge = {0};
    while (running) {
        SDL_Event e;
        memset(&edge, 0, sizeof(edge));
        bool start = false;
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
            case SDL_QUIT: running = false; break;
            case SDL_WINDOWEVENT:
                if (e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                    SDL_GL_GetDrawableSize(win, &dw, &dh);
                    if (dw > 0 && dh > 0) make_targets(dw, dh);
                }
                if (e.window.event == SDL_WINDOWEVENT_FOCUS_LOST && G.state == ST_PLAY) { G.state = ST_PAUSE; SDL_SetRelativeMouseMode(SDL_FALSE); }
                break;
            case SDL_MOUSEMOTION:
                if (G.state == ST_PLAY) { mdx += e.motion.xrel; mdy += e.motion.yrel; }
                break;
            case SDL_MOUSEWHEEL:
                PL.cam_zoom = clampf((PL.cam_zoom > 0 ? PL.cam_zoom : 1) - e.wheel.y * 0.1f, 0.6f, 2.2f);
                break;
            case SDL_MOUSEBUTTONDOWN:
                if (G.state == ST_TITLE) start = true;
                else if (G.state == ST_PAUSE) { G.state = ST_PLAY; SDL_SetRelativeMouseMode(SDL_TRUE); }
                break;
            case SDL_CONTROLLERDEVICEADDED: if (!pad) pad = SDL_GameControllerOpen(e.cdevice.which); break;
            case SDL_CONTROLLERBUTTONDOWN:
                if (G.state == ST_TITLE && (e.cbutton.button == SDL_CONTROLLER_BUTTON_START || e.cbutton.button == SDL_CONTROLLER_BUTTON_A)) start = true;
                else if (e.cbutton.button == SDL_CONTROLLER_BUTTON_START) {
                    if (G.state == ST_PLAY) G.state = ST_PAUSE; else if (G.state == ST_PAUSE || G.state == ST_MAP) G.state = ST_PLAY;
                } else if (e.cbutton.button == SDL_CONTROLLER_BUTTON_BACK) {
                    if (G.state == ST_PLAY) G.state = ST_MAP; else if (G.state == ST_MAP) G.state = ST_PLAY;
                } else if (G.state == ST_PLAY) {
                    if (e.cbutton.button == SDL_CONTROLLER_BUTTON_Y) edge.enter = true;
                    if (e.cbutton.button == SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) edge.radio_next = true;
                    if (e.cbutton.button == SDL_CONTROLLER_BUTTON_LEFTSHOULDER) edge.radio_prev = true;
                    if (e.cbutton.button == SDL_CONTROLLER_BUTTON_X) edge.horn = true;
                    if (e.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_UP) edge.taxi = true;
                    if (e.cbutton.button == SDL_CONTROLLER_BUTTON_RIGHTSTICK) edge.cam_reset = true;
                }
                break;
            case SDL_KEYDOWN: {
                if (e.key.repeat) break;
                SDL_Keycode k = e.key.keysym.sym;
                if (k == SDLK_F11) {
                    bool fs = SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN_DESKTOP;
                    SDL_SetWindowFullscreen(win, fs ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
                } else if (k == SDLK_m) {
                    muted = !muted;
                    audio_mute(muted);
                } else if (G.state == ST_TITLE) {
                    if (k == SDLK_ESCAPE) running = false;
                    else if (k == SDLK_RETURN || k == SDLK_SPACE) start = true;
                } else if (G.state == ST_PLAY) {
                    if (k == SDLK_ESCAPE || k == SDLK_p) { G.state = ST_PAUSE; SDL_SetRelativeMouseMode(SDL_FALSE); }
                    else if (k == SDLK_TAB) G.state = ST_MAP;
                    else if (k == SDLK_f || k == SDLK_RETURN) edge.enter = true;
                    else if (k == SDLK_e) edge.radio_next = true;
                    else if (k == SDLK_q) edge.radio_prev = true;
                    else if (k == SDLK_t) edge.taxi = true;
                    else if (k == SDLK_c) PL.cam_zoom = PL.cam_zoom > 1.5f ? 0.75f : (PL.cam_zoom > 0 ? PL.cam_zoom : 1) + 0.45f;
                } else if (G.state == ST_MAP) {
                    if (k == SDLK_TAB || k == SDLK_ESCAPE) G.state = ST_PLAY;
                } else if (G.state == ST_PAUSE) {
                    if (k == SDLK_ESCAPE || k == SDLK_p) { G.state = ST_PLAY; SDL_SetRelativeMouseMode(SDL_TRUE); }
                    else if (k == SDLK_q) { G.state = ST_TITLE; audio_engine(false, 0, 0, 0); audio_radio(-1); }
                    else if (k == SDLK_MINUS) G.sens = fmaxf(0.0004f, G.sens - 0.0002f);
                    else if (k == SDLK_EQUALS || k == SDLK_PLUS) G.sens = fminf(0.01f, G.sens + 0.0002f);
                    else if (k == SDLK_COMMA) G.fov_base = fmaxf(55, G.fov_base - 5);
                    else if (k == SDLK_PERIOD) G.fov_base = fminf(95, G.fov_base + 5);
                    else if (k == SDLK_i) G.invert = !G.invert;
                } else if ((G.state == ST_BUSTED || G.state == ST_WASTED) && G.st > 2 && (k == SDLK_RETURN || k == SDLK_SPACE)) G.st = 99;
                break;
            }
            }
        }
        Uint64 now = SDL_GetPerformanceCounter();
        float dt = (float)(now - last) / freq;
        last = now;
        if (dt > 1 / 30.0f) dt = 1 / 30.0f;
        t += dt;
        G.st += dt;
        if (start) {
            new_game();
            SDL_SetRelativeMouseMode(SDL_TRUE);
            mdx = mdy = 0;
        }
        Input in = edge;
        if (G.state == ST_PLAY) {
            const Uint8 *k = SDL_GetKeyboardState(NULL);
            in.fwd = (float)(k[SDL_SCANCODE_W] || k[SDL_SCANCODE_UP]) - (float)(k[SDL_SCANCODE_S] || k[SDL_SCANCODE_DOWN]);
            in.side = (float)(k[SDL_SCANCODE_D] || k[SDL_SCANCODE_RIGHT]) - (float)(k[SDL_SCANCODE_A] || k[SDL_SCANCODE_LEFT]);
            in.look_x = mdx * G.sens;
            in.look_y = -mdy * G.sens * (G.invert ? -1 : 1);
            in.jump = k[SDL_SCANCODE_SPACE];
            in.sprint = k[SDL_SCANCODE_LSHIFT] || k[SDL_SCANCODE_RSHIFT];
            in.horn |= k[SDL_SCANCODE_H] != 0;
            if (pad) {
                float lx = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTX) / 32767.0f;
                float ly = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTY) / 32767.0f;
                float rx = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_RIGHTX) / 32767.0f;
                float ry = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_RIGHTY) / 32767.0f;
                float rt = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) / 32767.0f;
                float lt = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_TRIGGERLEFT) / 32767.0f;
                if (fabsf(lx) > 0.15f) in.side += lx;
                if (PL.car >= 0) in.fwd += rt - lt;
                else if (fabsf(ly) > 0.15f) in.fwd -= ly;
                if (fabsf(rx) > 0.12f) in.look_x += rx * fabsf(rx) * 3.0f * dt;
                if (fabsf(ry) > 0.12f) in.look_y -= ry * fabsf(ry) * 2.2f * dt * (G.invert ? -1 : 1);
                in.jump |= SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_A) != 0;
                in.sprint |= SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_B) != 0 || SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_LEFTSTICK) != 0;
                in.fwd = clampf(in.fwd, -1, 1);
                in.side = clampf(in.side, -1, 1);
            }
            mdx = mdy = 0;
            if (in.radio_next || in.radio_prev) {
                int n = audio_radio_count();
                G.radio += in.radio_next ? 1 : -1;
                if (G.radio >= n) G.radio = -1;
                if (G.radio < -1) G.radio = n - 1;
                G.radio_t = 2.5f;
            }
            sim_step(&in, dt);
            update_camera(&in, dt, t);
        } else if (G.state == ST_TITLE) {
            Input none = {0};
            PL.pos = V(12 * PITCH, 0, 11 * PITCH);
            sim_step(&none, dt);
        } else if (G.state == ST_BUSTED || G.state == ST_WASTED) {
            Input none = {0};
            sim_step(&none, dt * 0.35f);
            update_camera(&none, dt, t);
            if (G.st > 4.5f) { G.fade = clampf((G.st - 4.5f) * 2, 0, 1); }
            if (G.st > 5.2f) { respawn_at(G.state == ST_BUSTED ? POI_POLICE : POI_HOSPITAL); G.fade = 0; }
        } else {
            update_env();
        }
        if (G.state != ST_PLAY && G.state != ST_PAUSE && G.state != ST_MAP && G.state != ST_TITLE && G.state != ST_BUSTED && G.state != ST_WASTED) G.state = ST_PLAY;
        update_audio_layers();
        render_frame(t);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo_ldr);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        glBlitFramebuffer(0, 0, rw, rh, 0, 0, rw, rh, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        SDL_GL_SwapWindow(win);
    }
    audio_shutdown();
    SDL_GL_DeleteContext(ctx);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
