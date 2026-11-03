/*
 * PORT CALDER - an open-city driving sandbox.
 * Native Linux, C11 + SDL2 + OpenGL 3.3 core. No asset files: the island city, its traffic,
 * pedestrians, police, weather, sounds and radio stations are all generated at startup.
 *
 * Source layout (one translation unit plus the audio module):
 *   gfx.c     math, shaders, instanced renderer, post-processing, HUD primitives
 *   map.c     California: coastline, terrain height map, cities, highway graph, road mesh, regions
 *   city.c    buildings by regional style, shops/hotels/restaurants, landmarks, countryside
 *   models.c  vehicle and character models
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
#include "map.c"
#include "city.c"
#include "actors.c"
#include "game.c"
#include "models.c"

/* ================================================================== scene assembly */
static Mat paint(v3 c) { Mat m = {c, PAT_PAINT, 0.45f, 0.32f, {0, 0, 0}, 0}; return m; }
static const Mat M_GLASS = {{0.02f, 0.025f, 0.03f}, PAT_PLAIN, 0.9f, 0.06f, {0, 0, 0}, 0};
static const Mat M_TYRE = {{0.03f, 0.03f, 0.03f}, PAT_PLAIN, 0, 0.8f, {0, 0, 0}, 0};
static const Mat M_RIM = {{0.6f, 0.6f, 0.62f}, PAT_PLAIN, 1, 0.25f, {0, 0, 0}, 0};
static const Mat M_BLACK = {{0.025f, 0.025f, 0.028f}, PAT_PLAIN, 0.2f, 0.5f, {0, 0, 0}, 0};
static const Mat M_BURNT = {{0.04f, 0.035f, 0.03f}, PAT_LEAF, 0.3f, 0.9f, {0, 0, 0}, 0};

static void part_box(m4 M, v3 c, v3 s, const Mat *m) { inst(MS_CUBE, m4_mul(M, m4_box(c, s)), m); }

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
        int n = i / 4, k = i % 4;
        /* the arm reaches toward the road it lights */
        float ax = k & 1 ? -1 : 1, az = k & 2 ? -1 : 1;
        (void)home;
        m4 R = m4_trs(q->pos, 0, q->rot.x, q->rot.z, V(1, 1, 1));
        inst(MS_CYL8, m4_mul(R, m4_trs(V(0, 0, 0), 0, 0, 0, V(0.22f, 6.4f, 0.22f))), &pole);
        inst(MS_CUBE, m4_mul(R, m4_box(V(ax * 0.6f, 6.3f, az * 0.6f), V(1.4f, 0.12f, 0.12f))), &pole);
        inst(MS_CUBE, m4_mul(R, m4_box(V(ax * 1.2f, 6.2f, az * 1.2f), V(0.6f, 0.15f, 0.4f))), q->down ? &pole : &lamp_head);
        if (!q->down && nodes[n].light && fabsf(q->pos.x - cp.x) < 170 && fabsf(q->pos.z - cp.z) < 170) {
            int ew = light_state(n, V(1, 0, 0)), ns = light_state(n, V(0, 0, 1));
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
        float a = wheel_angle(t), R = 23;
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
    /* wind turbines and oil pumpjacks */
    Mat white = {{0.9f, 0.9f, 0.9f}, PAT_PLAIN, 0.3f, 0.4f, {0, 0, 0}, 0};
    for (int k = 0; k < nturbine; k++) {
        if (dist2d(TURBINES[k], cp) > 2600) continue;
        float ang = t * 1.1f + k * 0.7f;
        for (int b = 0; b < 3; b++) inst(MS_CUBE, m4_mul(m4_trs(TURBINES[k], 0, 0, ang + b * 2.0944f, V(1, 1, 1)), m4_box(V(0, 7.5f, 0), V(0.9f, 15, 0.25f))), &white);
    }
    Mat iron = {{0.12f, 0.12f, 0.13f}, PAT_PLAIN, 0.6f, 0.5f, {0, 0, 0}, 0};
    for (int k = 0; k < npump; k++) {
        if (dist2d(PUMPJACKS[k], cp) > 700) continue;
        float pa = sinf(t * 1.3f + k) * 0.32f;
        m4 B = m4_trs(PUMPJACKS[k], 0, pa, 0, V(1, 1, 1));
        inst(MS_CUBE, m4_mul(B, m4_box(V(0, 0.3f, 0), V(0.5f, 0.6f, 8))), &iron);
        inst(MS_CUBE, m4_mul(B, m4_box(V(0, -0.2f, -4.2f), V(0.6f, 1.6f, 0.8f))), &iron);
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
        for (int nd = 0; nd < nnode; nd++) {
            if (nodes[nd].city < 0 || fabsf(nodes[nd].p.x - cam.pos.x) > 140 || fabsf(nodes[nd].p.z - cam.pos.z) > 140) continue;
                for (int k = 0; k < 4; k++) {
                    int idx = nd * 4 + k;
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
    /* soft contact shadows under cars and people */
    for (int i = 0; i < MAXCAR; i++) {
        Car *c = &car[i];
        if (!c->on || c->sunk || dist2d(c->pos, cp) > 140) continue;
        const CarSpec *S = &SPEC[c->model];
        v3 f = mul(fwd_of(c->yaw), S->L / 2 + 0.5f), r = mul(right_of(c->yaw), S->W / 2 + 0.45f);
        float gy = ground_at(c->pos.x, c->pos.z, c->pos.y + 0.3f, 0.5f) + 0.04f;
        v3 o = V(c->pos.x, gy, c->pos.z);
        float a = clampf(1 - (c->pos.y - gy) * 0.3f, 0, 1) * 0.75f;
        pquad(sub(sub(o, f), r), add(sub(o, f), r), add(add(o, f), r), sub(add(o, f), r), V(0, 0, 0), a, 3);
        if (c->sig != 0 && fmodf(t * 1.6f, 1) < 0.5f && night > 0.2f && c->sig_node >= 0 && dist2d(c->pos, nodes[c->sig_node].p) < 45) {
            m4 M = m4_trs(c->pos, c->yaw, c->pitch, c->roll, V(1, 1, 1));
            billboard(m4_point(M, V(c->sig * S->W * 0.47f, 0.3f + S->H * 0.3f, -S->L / 2 - 0.1f)), 0.3f, V(3, 1.5f, 0.1f), night, 0);
            billboard(m4_point(M, V(c->sig * S->W * 0.47f, 0.3f + S->H * 0.3f, S->L / 2 + 0.1f)), 0.3f, V(3, 1.5f, 0.1f), night, 0);
        }
    }
    for (int i = 0; i < MAXPED; i++) {
        Ped *p = &ped[i];
        if (!p->on || dist2d(p->pos, cp) > 60) continue;
        v3 o = add(p->pos, V(0, 0.04f, 0));
        pquad(add(o, V(-0.5f, 0, -0.5f)), add(o, V(0.5f, 0, -0.5f)), add(o, V(0.5f, 0, 0.5f)), add(o, V(-0.5f, 0, 0.5f)), V(0, 0, 0), 0.5f, 3);
    }
    if (PL.car < 0 && G.state != ST_TITLE && !PL.swim) {
        v3 o = V(PL.pos.x, ground_at(PL.pos.x, PL.pos.z, PL.pos.y, 0.3f) + 0.04f, PL.pos.z);
        pquad(add(o, V(-0.5f, 0, -0.5f)), add(o, V(0.5f, 0, -0.5f)), add(o, V(0.5f, 0, 0.5f)), add(o, V(-0.5f, 0, 0.5f)), V(0, 0, 0), 0.55f, 3);
    }
    if (G.state == ST_PLAY || G.state == ST_PAUSE)
        for (int k = 0; k < nshop; k++) {
            if (dist2d(shops[k].door, cp) > 120) continue;
            v3 sc = SHOP_COL[shops[k].type];
            float a = shop_open(shops[k].type, G.tod) ? 0.8f : 0.2f;
            ground_ring(add(shops[k].door, V(0, 0.06f, 0)), shops[k].drive_in ? 4.0f : 1.4f, mul(sc, 1.4f), a);
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
        for (int k = 0; k < 3; k++) {
            v3 o = add(LIGHTHOUSE[k], V(0, 25.3f, 0));
            if (dist2d(o, cp) > 2500) continue;
            beam(o, add(o, V(cosf(a + k) * 260, -6, sinf(a + k) * 260)), 4, V(1.2f, 1.1f, 0.8f), 0.18f * night);
            billboard(o, 2.2f, V(4, 3.6f, 2.4f), night, 0);
        }
    }
    /* Yosemite's waterfall */
    if (dist2d(WATERFALL, cp) < 2500) {
        v3 top = WATERFALL, bot = V(WATERFALL.x, 121, WATERFALL.z + 30);
        beam(top, bot, 2.5f, V(0.7f, 0.8f, 0.9f), 0.35f);
        beam(top, bot, 1.0f, V(1.0f, 1.0f, 1.0f), 0.35f);
        for (int k = 0; k < 6; k++) billboard(add(bot, V(sinf(t + k) * 4, 2 + k * 0.6f, cosf(t * 0.7f + k) * 4)), 5, V(0.55f, 0.6f, 0.65f), 0.25f, 3);
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
        if (car[i].on && dist2d(car[i].pos, cp) < 420) draw_car_model(&car[i], t, NIGHT);
    for (int i = 0; i < MAXPED; i++) {
        Ped *p = &ped[i];
        if (!p->on || dist2d(p->pos, cp) > 200) continue;
        float lie = p->state == PS_DOWN ? (p->pos.y > ground_at(p->pos.x, p->pos.z, p->pos.y, 0.3f) + 0.05f ? 0 : -1.5f) : 0;
        draw_person(add(p->pos, V(0, lie != 0 ? 0.15f : 0, 0)), p->yaw, p->anim, len2d(p->vel), p->shirt, p->pants, p->skin, p->hair, p->h, lie, p->state == PS_DOWN ? p->tumble : 0,
                    p->state == PS_HAIL ? 1 : p->state == PS_WAITX ? 2 : 0, p->cop, p->seed);
    }
    if (PL.car < 0 && G.state != ST_TITLE) {
        float lie = PL.down_t > 0 ? -1.2f : 0;
        v3 at = PL.pos;
        if (PL.swim) at.y = water_level_at(at.x, at.z) - 1.15f;
        const Outfit *o = &OUTFIT[outfit];
        draw_person(at, PL.yaw, PL.anim, len2d(PL.vel), o->shirt, o->pants, V(0.75f, 0.55f, 0.42f), o->hat, 1.0f, lie, 0, 0, false, 7u + (unsigned)outfit * 101u);
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
        skip_always = true;
        detail_cx = cp.x; detail_cz = cp.z; detail_r = 190;
        draw_all_instances();
        detail_r = 1;
        skip_always = false;
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
    glUniform1f(U(P_LIT, "uWater"), WATER_Y);
    glUniform2f(U(P_LIT, "uMapMin"), WX0, WZ0);
    glUniform2f(U(P_LIT, "uMapSize"), BNX * BRES, BNZ * BRES);
    glUniform1f(U(P_LIT, "uSnow"), 285);
    glUniform1i(U(P_LIT, "uShadow"), 0);
    glUniform1i(U(P_LIT, "uBiome"), 1);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, tex_biome);
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
    detail_cx = cp.x; detail_cz = cp.z; detail_r = 1;
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
        if (PL.swim) target.y = water_level_at(PL.pos.x, PL.pos.z) + 0.6f;
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
    if (water_at(eye.x, eye.z) && eye.y < water_level_at(eye.x, eye.z) + 0.3f) eye.y = water_level_at(eye.x, eye.z) + 0.3f;
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
    G.msg_t -= dt; G.big_t -= dt; G.toast_t -= dt; G.radio_t -= dt; shop_msg_t -= dt;
}
static void new_game(void) {
    memset(car, 0, sizeof(car));
    memset(ped, 0, sizeof(ped));
    memset(part, 0, sizeof(part));
    memset(slot_used, 0, sizeof(slot_used));
    props_init();
    memset(&MS, 0, sizeof(MS));
    MS.fare_ped = -1;
    int k = 0;
    while (k < npoi - 1 && poi[k].type != POI_SAFE) k++;
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
    spawn_own_car(add(poi[k].pos, V(6, 0, -3.2f)), 1.5708f);
    PL.stamina = 1;
    PL.energy_t = 0;
    shop_cur = -1;
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
static int bot_tn = -1, bot_n = -1, bot_e = -1;
static float bot_stuck, bot_back;
static void bot_drive(Input *in, v3 target, float cruise, float dt) {
    Car *c = &car[PL.car];
    memset(in, 0, sizeof(*in));
    int tn = nearest_road_node(target);
    if (tn != bot_tn) { route_build(&bot_route, tn); bot_tn = tn; bot_n = -1; }
    if (bot_n < 0) { /* the node ahead on the nearest edge */
        float d, sa;
        int e = nearest_edge(c->pos, 60, &d, &sa);
        if (e < 0) { bot_n = nearest_road_node(c->pos); bot_e = -1; }
        else { bot_e = e; bot_n = dot(edges[e].d, fwd_of(c->yaw)) > 0 ? edges[e].b : edges[e].a; }
    }
    float want = cruise;
    v3 np = nodes[bot_n].p;
    bool final = bot_route.d[bot_n] == 0;
    if (!final) {
        int ne = route_next_edge(&bot_route, bot_n, bot_e);
        if (ne >= 0 && bot_e >= 0) {
            v3 din = edge_dir(bot_e, edge_other(bot_e, bot_n)), dout = edge_dir(ne, bot_n);
            float turn = dot(din, dout);
            if (turn < 0.85f && dist2d(c->pos, np) < 40) want = fminf(want, turn < 0 ? 6 : 10);
        }
        float passed = bot_e >= 0 ? dot(sub(c->pos, np), edge_dir(bot_e, edge_other(bot_e, bot_n))) : -1;
        if (dist2d(c->pos, np) < 5 + fabsf(car_speed(c)) * 0.1f || passed > 0) {
            if (ne >= 0) { bot_e = ne; bot_n = edge_other(ne, bot_n); np = nodes[bot_n].p; }
        }
    }
    v3 aim = np;
    if (bot_e >= 0) aim = add(np, mul(right_vec(edge_dir(bot_e, edge_other(bot_e, bot_n))), edges[bot_e].hwy >= 0 ? 1.85f : LANE_OFF));
    float dtg = dist2d(c->pos, target);
    if (final || dtg < 40) { aim = target; want = fminf(want, fmaxf(dtg * 0.5f, 2)); }
    float err = wrap_pi(yaw_of(sub(aim, c->pos)) - c->yaw);
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
    c->fuel = 1;
    bot_n = -1;
    bot_tn = -1;
}
/* a lane position on city street (i,j)->(i+1,j) style edges, for test setups */
static v3 city_lane(int c, int i, int j, int di, int dj, float s, float *yaw) {
    int a = city_node(c, i, j), b = city_node(c, i + di, j + dj);
    int e = -1;
    for (int k = 0; k < nodes[a].deg; k++) if (edge_other(nodes[a].e[k], a) == b) e = nodes[a].e[k];
    if (yaw) *yaw = yaw_of(edge_dir(e, a));
    return lane_pos(e, a, s, LANE_OFF);
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
    for (int i = 0; i < MS_N; i++) { ninst += stat_count[i]; for (int t = 0; t < NTIER; t++) ninst += ST[t][i].n; }
    int nhw = 0;
    float hwlen = 0;
    for (int e = 0; e < nedge; e++) if (edges[e].hwy >= 0) { nhw++; hwlen += edges[e].len; }
    printf("world: %d cities, %d road nodes, %d edges (%.1f km of highway), %d boxes, %d static instances, %d shops, %d POIs, %d ramps\n",
           NCITY, nnode, nedge, hwlen / 1000, nbox, ninst, nshop, npoi, nramp);
    /* connectivity: every city reachable from Los Angeles by road */
    {
        Route r = {0};
        route_build(&r, city_node(C_LA, 10, 7));
        int unreach = 0;
        for (int c = 0; c < NCITY; c++) if (r.d[city_node(c, 1, 1)] >= 30000) { unreach++; printf("    unreachable: %s\n", cities[c].name); }
        CHECK(unreach == 0, "every city is connected to Los Angeles by road");
        free(r.d);
    }
    /* lanes clear of buildings */
    int bad_lane = 0, lanes = 0;
    for (int e = 0; e < nedge; e++) {
        if (edges[e].bridge) continue;
        for (int side = 0; side < 2; side++) {
            int from = side ? edges[e].b : edges[e].a;
            float i0 = nodes[from].inset + 6, i1 = nodes[edge_other(e, from)].inset + 6;
            for (float sp = i0; sp < edges[e].len - i1; sp += 6) {
                v3 p = lane_pos(e, from, sp, edges[e].hwy >= 0 ? 1.85f : LANE_OFF);
                p.y = ground_at(p.x, p.z, p.y + 0.5f, 0.3f) + 0.2f;
                lanes++;
                if (point_blocked(p, 0.9f, 1.4f)) { if (bad_lane < 6) printf("    blocked lane on %s at %.0f,%.0f\n", edges[e].hwy >= 0 ? hwy_name(edges[e].hwy) : "street", p.x, p.z); bad_lane++; }
            }
        }
    }
    CHECK(bad_lane <= lanes / 400, "traffic lanes clear (%d samples, %d blocked)", lanes, bad_lane);
    int under = 0;
    for (int n = 0; n < nnode; n++) if (nodes[n].deg && water_at(nodes[n].p.x, nodes[n].p.z) && nodes[n].p.y < 5) { under++; printf("    wet node %d at %.0f,%.0f y %.1f city %d\n", n, nodes[n].p.x, nodes[n].p.z, nodes[n].p.y, nodes[n].city); }
    CHECK(under == 0, "no road node under water (%d)", under);
    int bad_poi = 0;
    for (int k = 0; k < npoi; k++) if (point_blocked(add(poi[k].pos, V(0, 0.3f, 0)), 1.2f, 1.8f)) { bad_poi++; printf("    blocked poi type %d at %.0f %.0f\n", poi[k].type, poi[k].pos.x, poi[k].pos.z); }
    CHECK(bad_poi == 0, "all %d points of interest reachable", npoi);
    int bad_door = 0;
    for (int k = 0; k < nshop; k++) if (point_blocked(add(shops[k].door, V(0, 0.3f, 0)), shops[k].drive_in ? 1.2f : 0.4f, 1.7f)) { bad_door++; printf("    blocked door %s at %.0f,%.0f\n", shops[k].name, shops[k].door.x, shops[k].door.z); }
    CHECK(bad_door == 0, "all %d shop, hotel and restaurant entrances reachable", nshop);
    for (int r = 0; r < NRACE; r++) printf("    race %-22s %2d checkpoints, par %.0f s\n", races[r].name, races[r].ncp, races[r].par);
    int short_race = 0;
    for (int r = 0; r < NRACE; r++) if (races[r].ncp < 5) short_race++;
    CHECK(short_race == 0, "every race has checkpoints");

    Input none = {0};
    /* performance in downtown LA */
    new_game();
    G.tod = 13;
    update_env();
    for (int i = 0; i < 120; i++) sim_step(&none, 1 / 60.0f);
    glFinish();
    Uint64 t0 = SDL_GetPerformanceCounter();
    for (int i = 0; i < 90; i++) { PL.cam_yaw = i * 0.07f; update_camera(&none, 1 / 60.0f, 0); render_frame(i / 60.0f); }
    glFinish();
    double ms = (double)(SDL_GetPerformanceCounter() - t0) * 1000.0 / SDL_GetPerformanceFrequency() / 90;
    printf("render: %.2f ms per frame at %dx%d\n", ms, rw, rh);
    t0 = SDL_GetPerformanceCounter();
    for (int i = 0; i < 600; i++) sim_step(&none, 1 / 60.0f);
    printf("simulation: %.3f ms per step\n", (double)(SDL_GetPerformanceCounter() - t0) * 1000.0 / SDL_GetPerformanceFrequency() / 600);

    /* title */
    G.state = ST_TITLE;
    G.tod = 19.0f;
    update_env();
    render_frame(4.0f);
    SHOT("01_title");

    /* traffic soak in LA */
    {
        srand(99);
        new_game();
        G.tod = 10;
        PL.pos = V(cities[C_LA].x0 + 13 * PITCH + 30, cities[C_LA].gy, cities[C_LA].z0 + 6 * PITCH + WALK);
        int stuck_long = 0, inside = 0, maxai = 0, ped_waits = 0;
        float stuck_t[MAXCAR] = {0};
        double speed_sum = 0;
        long speed_n = 0;
        for (int step = 0; step < 60 * 240; step++) {
            sim_step(&none, 1 / 60.0f);
            if (step % 30) continue;
            int ai = 0;
            for (int i = 0; i < MAXCAR; i++) {
                Car *c = &car[i];
                if (!c->on || c->driver != DRV_AI) { stuck_t[i] = 0; continue; }
                ai++;
                float sp = fabsf(car_speed(c));
                speed_sum += sp; speed_n++;
                if (sp < 0.3f) stuck_t[i] += 0.5f; else stuck_t[i] = 0;
                if (stuck_t[i] > 45) { stuck_long++; stuck_t[i] = -1000; }
                if (point_blocked(add(c->pos, V(0, 0.3f, 0)), 0.3f, 1.0f)) inside++;
            }
            for (int i = 0; i < MAXPED; i++) if (ped[i].on && ped[i].state == PS_WAITX) ped_waits++;
            if (ai > maxai) maxai = ai;
        }
        printf("traffic: up to %d AI cars, mean speed %.1f m/s\n", maxai, speed_n ? speed_sum / speed_n : 0);
        CHECK(maxai >= 25, "traffic population maintained (%d)", maxai);
        CHECK(speed_n && speed_sum / speed_n > 4, "traffic keeps moving");
        CHECK(stuck_long <= 3, "cars stuck for more than 45 s: %d", stuck_long);
        CHECK(inside <= 2, "cars inside buildings: %d", inside);
        CHECK(ped_waits > 0, "pedestrians wait at crossings (%d samples)", ped_waits);
        CHECK(!any_nan(), "no NaN positions");
    }
    /* highway traffic between cities */
    {
        srand(5);
        new_game();
        int n = find_node_near(3550, 5500, 30);
        if (n < 0) n = nearest_road_node(V(3550, 0, 5500));
        PL.pos = nodes[n].p;
        PL.car = -1;
        for (int s = 0; s < 60 * 60; s++) sim_step(&none, 1 / 60.0f);
        int on_hwy = 0;
        double sum = 0;
        for (int i = 0; i < MAXCAR; i++) if (car[i].on && car[i].driver == DRV_AI) { float d; int e = nearest_edge(car[i].pos, 20, &d, NULL); if (e >= 0 && edges[e].hwy >= 0) { on_hwy++; sum += fabsf(car_speed(&car[i])); } }
        printf("highway: %d cars on I-5, mean %.1f m/s\n", on_hwy, on_hwy ? sum / on_hwy : 0);
        CHECK(on_hwy >= 3 && sum / (on_hwy ? on_hwy : 1) > 12, "highway traffic flows");
    }
    /* road trip: the autopilot drives Los Angeles -> Santa Barbara on US 101 */
    {
        srand(8);
        new_game();
        G.tod = 17.8f;
        float yaw;
        v3 at = city_lane(C_LA, 1, 2, 0, -1, 20, &yaw);
        bot_put_in_car(at, yaw, VM_SPORTS);
        v3 target = nodes[city_node(C_SB, 3, 2)].p;
        float t = 0;
        bool shot = false;
        Input in;
        while (t < 240 && dist2d(car[PL.car].pos, target) > 25 && G.state == ST_PLAY) {
            bot_drive(&in, target, 30, 1 / 60.0f);
            sim_step(&in, 1 / 60.0f);
            t += 1 / 60.0f;
            if (!shot && dist2d(car[PL.car].pos, V(4100, 0, 7200)) < 120) {
                shot = true;
                update_camera(&in, 1 / 60.0f, t);
                PL.cam_yaw = car[PL.car].yaw + 2.4f; PL.look_t = 0;
                update_camera(&none, 1 / 60.0f, t);
                render_frame(t);
                SHOT("02_malibu_coast_drive");
            }
        }
        printf("    road trip end: car %.0f,%.0f speed %.1f bot_n %d bot_e %d tn %d\n", car[PL.car].pos.x, car[PL.car].pos.z, car_speed(&car[PL.car]), bot_n, bot_e, bot_tn);
        CHECK(dist2d(car[PL.car].pos, target) <= 25, "drove Los Angeles to Santa Barbara in %.0f s (%.1f km)", t, G.stat_dist / 1000);
    }
    /* courier job */
    {
        srand(7);
        new_game();
        G.tod = 16;
        int jk = -1;
        for (int k = 0; k < npoi; k++) if (poi[k].type == POI_JOB && poi[k].city == C_LA) { jk = k; break; }
        float yaw;
        v3 at = city_lane(C_LA, poi[jk].i, poi[jk].j, 1, 0, 6, &yaw);
        bot_put_in_car(at, yaw, VM_SEDAN);
        Input in;
        float t = 0;
        bool started = false, done = false;
        int deliveries0 = deliveries;
        while (t < 400 && !done) {
            bot_drive(&in, started ? MS.target : poi[jk].pos, started ? 22 : 8, 1 / 60.0f);
            sim_step(&in, 1 / 60.0f);
            t += 1 / 60.0f;
            if (MS.type == MT_DELIVERY && !started) { started = true; printf("    job started: %.0f m, %.0f s allowed\n", MS.dist, MS.limit); }
            if (started && MS.type == MT_NONE) done = true;
            if (G.state != ST_PLAY) break;
        }
        CHECK(started, "courier job starts from its marker");
        CHECK(deliveries == deliveries0 + 1, "autopilot delivered the package (%.0f s)", t);
    }
    /* street race in LA */
    {
        srand(11);
        new_game();
        G.tod = 12;
        traffic_target = 12;
        for (int i = 0; i < MAXCAR; i++) if (car[i].on && car[i].driver == DRV_AI && i % 3) car[i].on = false;
        bot_put_in_car(sub(races[0].start, mul(fwd_of(races[0].yaw), 8)), races[0].yaw, VM_SPORTS);
        Input in;
        float t = 0;
        bool started = false;
        while (t < 300) {
            if (started && MS.stage == 0) memset(&in, 0, sizeof(in));
            else bot_drive(&in, started ? races[0].cp[MS.cp] : races[0].start, started ? 26 : 5, 1 / 60.0f);
            sim_step(&in, 1 / 60.0f);
            t += 1 / 60.0f;
            if (MS.type == MT_RACE) started = true;
            if (started && MS.type == MT_NONE) break;
        }
        traffic_target = 38;
        CHECK(started && race_best[0] > 0, "finished %s in %.1f s (par %.0f)", races[0].name, race_best[0], races[0].par);
    }
    /* stunt jump */
    {
        new_game();
        G.tod = 11;
        Ramp *R = &ramps[0];
        v3 back = sub(R->pos, mul(fwd_of(R->yaw), 70));
        back.y = terrain_h(back.x, back.z);
        bot_put_in_car(back, R->yaw, VM_SPORTS);
        Input in = {0};
        float maxair = 0;
        for (int s = 0; s < 60 * 10; s++) {
            in.fwd = 1;
            in.side = clampf(wrap_pi(yaw_of(sub(add(R->pos, mul(fwd_of(R->yaw), 30)), car[PL.car].pos)) - car[PL.car].yaw) * 2, -1, 1);
            sim_step(&in, 1 / 60.0f);
            if (car[PL.car].airborne && car[PL.car].air_t > maxair) maxair = car[PL.car].air_t;
        }
        CHECK(maxair > 0.8f, "stunt ramp launches the car (%.2f s of air)", maxair);
    }
    /* pedestrian, wanted level, police bust */
    {
        srand(5);
        new_game();
        G.tod = 22.5f;
        G.rain = 0.8f; G.rain_target = 0.8f; G.rain_timer = 999;
        float yaw;
        v3 start = city_lane(C_LA, 10, 9, 1, 0, 10, &yaw);
        bot_put_in_car(start, yaw, VM_SEDAN);
        for (int i = 0; i < MAXCAR; i++) if (car[i].on && car[i].driver != DRV_PLAYER && dist2d(car[i].pos, start) < 90) car[i].on = false;
        Ped *p = spawn_ped(city_lane(C_LA, 10, 9, 1, 0, 40, NULL), false);
        p->state = PS_IDLE;
        Input in = {0};
        for (int s = 0; s < 60 * 4 && p->state != PS_DOWN; s++) { in.fwd = 1; in.side = 0; sim_step(&in, 1 / 60.0f); }
        CHECK(p->state == PS_DOWN, "car knocks the pedestrian over");
        CHECK(G.heat > 0, "wanted heat rises (%.2f)", G.heat);
        G.heat = 3.2f; G.stars = 3;
        float t = 0;
        bool shot = false;
        memset(&in, 0, sizeof(in));
        while (t < 120 && G.state == ST_PLAY) {
            sim_step(&in, 1 / 60.0f);
            t += 1 / 60.0f;
            if (!shot && count_cops_near(30) >= 2) {
                shot = true;
                PL.cam_yaw = car[PL.car].yaw + 2.6f; PL.look_t = 0;
                update_camera(&none, 1 / 60.0f, t);
                render_frame(t);
                SHOT("03_police_night_rain");
            }
        }
        CHECK(G.state == ST_BUSTED, "the police bust a stationary player (%.0f s)", t);
        respawn_at(POI_POLICE);
        CHECK(G.state == ST_PLAY && G.stars == 0 && PL.car < 0, "respawn at the police station");
        G.rain = 0; G.rain_target = 0;
    }
    /* explosion */
    {
        new_game();
        float yaw;
        bot_put_in_car(city_lane(C_LA, 14, 3, 0, 1, 20, &yaw), yaw, VM_SEDAN);
        car[PL.car].hp = 5;
        Input in = {0};
        for (int s = 0; s < 60 * 10 && G.state == ST_PLAY; s++) sim_step(&in, 1 / 60.0f);
        CHECK(G.state == ST_WASTED, "a burning car explodes and wastes the driver");
        G.state = ST_PLAY;
        respawn_at(POI_HOSPITAL);
    }
    /* shops, hotels, restaurants */
    {
        new_game();
        G.tod = 13;
        G.money = 100000;
        Input in = {0};
        int kf = -1, kh = -1, kg = -1, kv = -1;
        for (int k = 0; k < nshop; k++) {
            if (shops[k].city != C_LA) continue;
            if (shops[k].type == SHOP_FOOD && kf < 0) kf = k;
            if (shops[k].type == SHOP_HOTEL && kh < 0) kh = k;
            if (shops[k].type == SHOP_GAS && kg < 0) kg = k;
            if (shops[k].type == SHOP_DEALER && kv < 0) kv = k;
        }
        PL.pos = shops[kf].door; PL.hp = 30;
        in.enter = true; sim_step(&in, 1 / 60.0f); in.enter = false;
        CHECK(G.state == ST_SHOP && shop_cur == kf, "%s opens its menu", shops[kf].name);
        shop_sel = 1; shop_buy();
        CHECK(PL.hp > 30, "a meal heals (%s)", items[1].label);
        PL.cam_yaw = 0; PL.cam_pitch = -0.1f;
        update_camera(&none, 1 / 60.0f, 0);
        render_frame(40);
        SHOT("04_restaurant_menu");
        shop_input(0, false, true);
        PL.pos = shops[kh].door;
        G.tod = 23;
        in.enter = true; sim_step(&in, 1 / 60.0f); in.enter = false;
        CHECK(G.state == ST_SHOP && items[0].kind == IT_SLEEP, "%s offers a room", shops[kh].name);
        shop_sel = 0; shop_buy(); shop_input(0, false, true);
        CHECK(fabsf(G.tod - 8) < 0.1f, "sleeping at the hotel passes the night");
        bot_put_in_car(shops[kg].door, 1.5708f, VM_SEDAN);
        car[PL.car].fuel = 0.1f;
        for (int st = 0; st < 30; st++) sim_step(&none, 1 / 60.0f);
        in.enter = true; sim_step(&in, 1 / 60.0f); in.enter = false;
        CHECK(G.state == ST_SHOP && items[0].kind == IT_FUEL, "the pumps open the fuel menu");
        shop_sel = 0; shop_buy(); shop_input(0, false, true);
        CHECK(car[PL.car].fuel > 0.99f, "tank filled");
        car[PL.car].on = false; PL.car = -1;
        G.tod = 12;
        PL.pos = shops[kv].door;
        in.enter = true; sim_step(&in, 1 / 60.0f); in.enter = false;
        shop_sel = 5; shop_buy(); shop_input(0, false, true);
        CHECK(own_car() && own_car()->model == VM_SPORTS, "bought a coupe at %s", shops[kv].name);
    }
    /* scenery across the state */
    struct { const char *name; v3 eye, at; float tod, rain; } views[] = {
        {"05_downtown_la", {5150, 3, 7300}, {5200, 60, 7360}, 18.2f, 0},
        {"06_santa_monica_pier", {4330, 14, 7280}, {4140, 20, 7316}, 18.9f, 0},
        {"07_golden_gate", {1600, 40, 3700}, {1378, 60, 3420}, 17.8f, 0},
        {"08_sf_victorians", {1250 + 4 * PITCH + 30, 2.0f, 3640 + 3 * PITCH + 3}, {1250 + 4 * PITCH - 20, 6, 3640 + 3 * PITCH + 12}, 15.0f, 0},
        {"09_bay_bridge_night", {1960, 30, 3900}, {2300, 20, 3832}, 21.5f, 0},
        {"10_big_sur", {2250, 60, 5700}, {2100, 20, 5550}, 16.5f, 0},
        {"11_yosemite", {3560, 150, 4110}, {4040, 200, 4070}, 11.0f, 0},
        {"12_lake_tahoe", {3430, 140, 3150}, {3430, 108, 2700}, 9.0f, 0},
        {"13_death_valley", {5600, 40, 5600}, {5720, 0, 5250}, 15.5f, 0},
        {"14_palm_springs_wind", {5600, 40, 7520}, {5850, 30, 7480}, 18.5f, 0},
        {"15_central_valley", {3300, 50, 4700}, {3500, 5, 4500}, 12.0f, 0},
        {"16_napa_vineyards", {2150, 30, 3000}, {2050, 10, 2850}, 17.0f, 0},
        {"17_mount_shasta", {1300, 80, 1100}, {1780, 300, 650}, 10.0f, 0},
        {"18_redwoods", {600, 30, 1800}, {700, 40, 1700}, 13.0f, 0},
        {"19_san_diego_harbor", {5450, 20, 8700}, {5330, 10, 8540}, 17.5f, 0},
        {"20_rainy_sf_night", {1250 + 9 * PITCH + 4, 1.8f, 3640 + 4 * PITCH + 20}, {1250 + 9 * PITCH + 4, 30, 3640 + 1 * PITCH}, 22.0f, 0.9f},
    };
    for (size_t v = 0; v < sizeof(views) / sizeof(views[0]); v++) {
        new_game();
        G.state = ST_TITLE;
        PL.pos = views[v].at;
        G.tod = views[v].tod;
        for (int i = 0; i < 40; i++) spawn_traffic(10, 300, true);
        spawn_peds(5, 120, 60);
        setup_camera(views[v].eye, views[v].at, 70);
        for (int s = 0; s < 90; s++) sim_step(&none, 1 / 60.0f);
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
    float yaw;
    bot_put_in_car(city_lane(C_SF, 7, 5, 1, 0, 30, &yaw), yaw, VM_TAXI);
    car[PL.car].vel = mul(fwd_of(car[PL.car].yaw), 15);
    toggle_taxi();
    for (int s = 0; s < 40; s++) sim_step(&none, 1 / 60.0f);
    update_camera(&none, 1 / 60.0f, 0);
    render_frame(30);
    SHOT("21_hud_taxi_sf");
    G.state = ST_MAP;
    render_frame(30);
    SHOT("22_map_california");
    CHECK(!any_nan(), "no NaN after all scenarios");
    printf("selftest: %s (%d failures)\n", fails ? "FAILED" : "PASSED", fails);
    return fails ? 1 : 0;
}

/* ================================================================== frame + main loop */
static void render_frame(float t) {
    if (G.state == ST_TITLE) {
        float a = t * 0.035f;
        v3 c = V(cities[C_LA].x0 + 14 * PITCH, cities[C_LA].gy, cities[C_LA].z0 + 7 * PITCH);
        v3 eye = add(c, V(cosf(a) * 520, 150 + sinf(t * 0.1f) * 20, sinf(a) * 520));
        setup_camera(eye, add(c, V(0, 50, 0)), 60);
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
    build_world();
    finish_world_meshes();
    upload_static();
    upload_biome();
    build_map_texture();
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
    if (!selftest) printf("Port Calder: California built in %.2f s%s\n", (double)(SDL_GetPerformanceCounter() - tb) / SDL_GetPerformanceFrequency(), audio_ok ? "" : " (no audio)");
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
    PL.pos = V(cities[C_LA].x0 + 14 * PITCH, cities[C_LA].gy, cities[C_LA].z0 + 7 * PITCH);
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
                else if (G.state == ST_SHOP) {
                    if (e.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_UP) shop_input(-1, false, false);
                    else if (e.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_DOWN) shop_input(1, false, false);
                    else if (e.cbutton.button == SDL_CONTROLLER_BUTTON_A) shop_input(0, true, false);
                    else if (e.cbutton.button == SDL_CONTROLLER_BUTTON_B || e.cbutton.button == SDL_CONTROLLER_BUTTON_START) shop_input(0, false, true);
                }
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
                } else if (G.state == ST_SHOP) {
                    if (k == SDLK_UP || k == SDLK_w) shop_input(-1, false, false);
                    else if (k == SDLK_DOWN || k == SDLK_s) shop_input(1, false, false);
                    else if (k == SDLK_RETURN || k == SDLK_f || k == SDLK_SPACE || k == SDLK_KP_ENTER) shop_input(0, true, false);
                    else if (k == SDLK_ESCAPE || k == SDLK_q || k == SDLK_BACKSPACE) shop_input(0, false, true);
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
            PL.pos = V(cities[C_LA].x0 + 14 * PITCH, cities[C_LA].gy, cities[C_LA].z0 + 7 * PITCH);
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
        if (G.state == ST_SHOP) shop_msg_t -= dt;
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
