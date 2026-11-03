/*
 * Ember Isle - a small open-world 3D exploration game in the style of early-90s
 * software-rendered polygon games. Native Linux, C11 + SDL2, no GPU 3D, no assets.
 *
 * The island's seven beacons have gone dark. Find and light them all, gather the
 * floating embers, then return to the lighthouse and rekindle its lamp, before or
 * after the sun goes down.
 *
 * Renderer: flat-shaded triangles, z-buffer, near-plane clipping, per-face fog, and
 * ordered dithering into a 6x7x6 "VGA" colour cube, all on the CPU at 320x200.
 *
 *   ./ember-isle                 play
 *   ./ember-isle --selftest DIR  headless: verify the world, let a bot light every beacon,
 *                                time the renderer, write screenshots to DIR
 */
#include "retro.h"

#define CELLS 96
#define MAPN (CELLS + 1)
#define CS 4.0f
#define WORLD (CELLS * CS)
#define SEA -6.0f
#define FXP 200.0f
#define FYP (200.0f / 1.2f)
#define NEARZ 0.15f
#define NBEACON 7
#define NEMBER 40
#define NSHEEP 14
#define NGULL 6
#define NCLOUD 14
#define DAY_LEN 600.0f
#define EYE 1.6f

typedef struct { float x, y, z; } V3;
static V3 v3(float x, float y, float z) { return (V3){x, y, z}; }
static V3 vsub(V3 a, V3 b) { return v3(a.x - b.x, a.y - b.y, a.z - b.z); }
static V3 vcross(V3 a, V3 b) { return v3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }
static float vdot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static V3 vnorm(V3 a) {
    float l = sqrtf(vdot(a, a));
    return l > 1e-9f ? v3(a.x / l, a.y / l, a.z / l) : v3(0, 1, 0);
}
static float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }
static float lerpf(float a, float b, float t) { return a + (b - a) * t; }
static float smooth01(float e0, float e1, float x) {
    float t = clampf((x - e0) / (e1 - e0), 0, 1);
    return t * t * (3 - 2 * t);
}

/* ================================================================== terrain */
static float H[MAPN][MAPN];
static V3 cell_n[CELLS][CELLS][2];
static float cell_rgb[CELLS][CELLS][2][3];

static float hash2(int x, int y, int s) {
    Uint32 h = (Uint32)x * 374761393u + (Uint32)y * 668265263u + (Uint32)s * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return (float)(h & 0xFFFFFF) / 16777215.0f;
}
static float vnoise(float x, float y, int s) {
    int xi = (int)floorf(x), yi = (int)floorf(y);
    float fx = x - xi, fy = y - yi;
    fx = fx * fx * (3 - 2 * fx);
    fy = fy * fy * (3 - 2 * fy);
    float a = hash2(xi, yi, s), b = hash2(xi + 1, yi, s), c = hash2(xi, yi + 1, s), d = hash2(xi + 1, yi + 1, s);
    return lerpf(lerpf(a, b, fx), lerpf(c, d, fx), fy);
}
static float fbm(float x, float y, int s) {
    float sum = 0, amp = 0.5f, tot = 0;
    for (int o = 0; o < 5; o++) {
        sum += vnoise(x, y, s + o * 17) * amp;
        tot += amp;
        x *= 2.03f;
        y *= 2.03f;
        amp *= 0.5f;
    }
    return sum / tot;
}

static float ground(float x, float z) {
    float gx = x / CS, gz = z / CS;
    if (gx < 0 || gz < 0 || gx >= CELLS || gz >= CELLS) return SEA;
    int i = (int)gx, j = (int)gz;
    float fx = gx - i, fz = gz - j;
    float v00 = H[j][i], v10 = H[j][i + 1], v01 = H[j + 1][i], v11 = H[j + 1][i + 1];
    if (fx > fz) return v00 + (v10 - v00) * fx + (v11 - v10) * fz;
    return v00 + (v01 - v00) * fz + (v11 - v01) * fx;
}
static V3 ground_normal(float x, float z) {
    float e = 0.5f;
    return vnorm(v3(ground(x - e, z) - ground(x + e, z), 2 * e, ground(x, z - e) - ground(x, z + e)));
}

static void tri_color(float h, float ny, float jit, float out[3]) {
    float r, g, b;
    if (h < 0.7f) { r = 222; g = 202; b = 140; }
    else if (ny < 0.72f) { r = 128; g = 118; b = 108; }
    else if (h > 15.0f) { r = 236; g = 240; b = 246; }
    else if (h > 12.5f) { r = 120; g = 124; b = 104; }
    else {
        float f = fbm(jit * 0.0f + h * 0.05f, jit, 9);
        r = 80 + 20 * f; g = 150 - 34 * (h / 12.5f) + 10 * f; b = 52;
    }
    float j = (jit - 0.5f) * 14;
    out[0] = r + j; out[1] = g + j; out[2] = b + j * 0.6f;
}

static void gen_terrain(void) {
    for (int j = 0; j < MAPN; j++)
        for (int i = 0; i < MAPN; i++) {
            float x = i / (float)CELLS, z = j / (float)CELLS;
            float dx = x - 0.5f, dz = z - 0.5f;
            float d = sqrtf(dx * dx + dz * dz) * 2;
            d += (fbm(x * 3 + 7, z * 3 + 3, 1) - 0.5f) * 0.5f;
            float m = 1 - smooth01(0.42f, 0.9f, d);
            float n = fbm(x * 3.2f, z * 3.2f, 2);
            float r = 1 - fabsf(fbm(x * 2.5f + 11, z * 2.5f + 5, 3) * 2 - 1);
            H[j][i] = m * (1.6f + 11 * n * n + 8 * r * r * m * m) + (1 - m) * SEA - 0.9f;
        }
    for (int j = 0; j < CELLS; j++)
        for (int i = 0; i < CELLS; i++) {
            V3 a = v3(i * CS, H[j][i], j * CS), b = v3((i + 1) * CS, H[j][i + 1], j * CS);
            V3 c = v3(i * CS, H[j + 1][i], (j + 1) * CS), d = v3((i + 1) * CS, H[j + 1][i + 1], (j + 1) * CS);
            V3 n1 = vnorm(vcross(vsub(d, a), vsub(b, a)));
            V3 n2 = vnorm(vcross(vsub(c, a), vsub(d, a)));
            if (n1.y < 0) n1 = v3(-n1.x, -n1.y, -n1.z);
            if (n2.y < 0) n2 = v3(-n2.x, -n2.y, -n2.z);
            cell_n[j][i][0] = n1;
            cell_n[j][i][1] = n2;
            tri_color((a.y + b.y + d.y) / 3, n1.y, hash2(i, j, 5), cell_rgb[j][i][0]);
            tri_color((a.y + d.y + c.y) / 3, n2.y, hash2(i, j, 6), cell_rgb[j][i][1]);
        }
}

/* ================================================================== meshes */
#define MAXMV 200
#define MAXMT 260
enum { F_EMIS = 1, F_DBL = 2 };
typedef struct { unsigned short a, b, c; unsigned char r, g, bl, fl; } MTri;
typedef struct { int nv, nt; V3 v[MAXMV]; MTri t[MAXMT]; } Mesh;

static void mesh_tri(Mesh *m, int a, int b, int c, V3 ref, int r, int g, int bl, int fl) {
    if (m->nt >= MAXMT) return;
    V3 n = vcross(vsub(m->v[b], m->v[a]), vsub(m->v[c], m->v[a]));
    V3 cen = v3((m->v[a].x + m->v[b].x + m->v[c].x) / 3, (m->v[a].y + m->v[b].y + m->v[c].y) / 3,
                (m->v[a].z + m->v[b].z + m->v[c].z) / 3);
    if (vdot(n, vsub(cen, ref)) < 0) { int t = b; b = c; c = t; }
    m->t[m->nt++] = (MTri){(unsigned short)a, (unsigned short)b, (unsigned short)c, (unsigned char)r,
                           (unsigned char)g, (unsigned char)bl, (unsigned char)fl};
}
static int mesh_v(Mesh *m, V3 p) {
    if (m->nv >= MAXMV) return m->nv - 1;
    m->v[m->nv] = p;
    return m->nv++;
}
/* a frustum: n sides, radius r0 at y0 to r1 at y1 (0 = apex), centred on (ox, oz) */
static void frustum(Mesh *m, int n, float r0, float r1, float y0, float y1, float ox, float oz, float ang,
                    int r, int g, int b, int fl) {
    V3 ref = v3(ox, (y0 + y1) / 2, oz);
    int base0 = m->nv, top0;
    if (r0 > 0) for (int i = 0; i < n; i++) {
        float a = ang + i * 6.2832f / n;
        mesh_v(m, v3(ox + cosf(a) * r0, y0, oz + sinf(a) * r0));
    } else mesh_v(m, v3(ox, y0, oz));
    top0 = m->nv;
    if (r1 > 0) for (int i = 0; i < n; i++) {
        float a = ang + i * 6.2832f / n;
        mesh_v(m, v3(ox + cosf(a) * r1, y1, oz + sinf(a) * r1));
    } else mesh_v(m, v3(ox, y1, oz));
    for (int i = 0; i < n; i++) {
        int i2 = (i + 1) % n;
        if (r0 > 0 && r1 > 0) {
            mesh_tri(m, base0 + i, base0 + i2, top0 + i2, ref, r, g, b, fl);
            mesh_tri(m, base0 + i, top0 + i2, top0 + i, ref, r, g, b, fl);
        } else if (r0 > 0) {
            mesh_tri(m, base0 + i, base0 + i2, top0, ref, r, g, b, fl);
        } else {
            mesh_tri(m, base0, top0 + i2, top0 + i, ref, r, g, b, fl);
        }
    }
    if (r1 > 0) { /* top cap */
        int c = mesh_v(m, v3(ox, y1, oz));
        for (int i = 0; i < n; i++) mesh_tri(m, c, top0 + i, top0 + (i + 1) % n, v3(ox, y1 - 1, oz), r, g, b, fl);
    }
}

enum { M_PINE, M_OAK, M_ROCK, M_HOUSE, M_BEACON, M_FIRE, M_LIGHTHOUSE, M_LAMP, M_STONE, M_EMBER, M_SHEEP,
       M_CLOUD, M_PILLAR, M_HUT, M_N };
static Mesh MESH[M_N];

static void build_meshes(void) {
    Mesh *m;
    m = &MESH[M_PINE];
    frustum(m, 5, 0.25f, 0.2f, 0, 1.3f, 0, 0, 0, 110, 70, 40, 0);
    frustum(m, 6, 1.7f, 0, 1.0f, 3.8f, 0, 0, 0, 40, 100, 50, 0);
    frustum(m, 6, 1.25f, 0, 2.8f, 5.4f, 0, 0, 0.5f, 52, 122, 60, 0);
    m = &MESH[M_OAK];
    frustum(m, 5, 0.3f, 0.25f, 0, 1.9f, 0, 0, 0, 120, 80, 45, 0);
    frustum(m, 6, 0.9f, 2.0f, 1.7f, 3.0f, 0, 0, 0, 70, 140, 50, 0);
    frustum(m, 6, 2.0f, 0.5f, 3.0f, 4.6f, 0, 0, 0, 92, 160, 60, 0);
    m = &MESH[M_ROCK];
    frustum(m, 5, 1.3f, 0.7f, -0.4f, 0.9f, 0, 0, 0.3f, 128, 124, 118, 0);
    frustum(m, 5, 0.7f, 0.2f, 0.9f, 1.4f, 0.1f, 0.05f, 0.9f, 140, 136, 128, 0);
    m = &MESH[M_HOUSE];
    frustum(m, 4, 2.6f, 2.6f, -0.5f, 2.4f, 0, 0, 0.7854f, 226, 214, 184, 0);
    frustum(m, 4, 3.1f, 0, 2.4f, 4.3f, 0, 0, 0.7854f, 176, 62, 44, 0);
    frustum(m, 4, 0.55f, 0.55f, -0.5f, 1.4f, 0, 1.75f, 0.7854f, 90, 56, 34, 0);
    frustum(m, 4, 0.35f, 0.35f, 3.0f, 4.6f, 0.9f, -0.6f, 0.7854f, 120, 110, 100, 0);
    m = &MESH[M_HUT];
    frustum(m, 6, 1.8f, 1.8f, -0.5f, 1.6f, 0, 0, 0, 150, 110, 70, 0);
    frustum(m, 6, 2.3f, 0, 1.6f, 3.4f, 0, 0, 0, 200, 170, 90, 0);
    m = &MESH[M_BEACON];
    frustum(m, 6, 1.5f, 1.15f, -0.5f, 1.0f, 0, 0, 0, 130, 126, 120, 0);
    frustum(m, 4, 0.22f, 0.2f, 1.0f, 3.0f, 0, 0, 0, 110, 72, 40, 0);
    frustum(m, 6, 0.5f, 0.95f, 3.0f, 3.6f, 0, 0, 0, 72, 62, 58, 0);
    m = &MESH[M_FIRE];
    frustum(m, 5, 0.75f, 0, 3.5f, 5.2f, 0, 0, 0, 255, 120, 30, F_EMIS);
    frustum(m, 4, 0.45f, 0, 3.5f, 5.9f, 0.1f, 0.05f, 0.6f, 255, 220, 90, F_EMIS);
    m = &MESH[M_LIGHTHOUSE];
    frustum(m, 8, 3.2f, 3.0f, -1.0f, 1.0f, 0, 0, 0, 140, 136, 128, 0);
    for (int s = 0; s < 4; s++) {
        float r0 = 2.3f - s * 0.17f, r1 = 2.3f - (s + 1) * 0.17f;
        frustum(m, 8, r0, r1, 1.0f + s * 3, 4.0f + s * 3, 0, 0, 0.2f, s & 1 ? 196 : 238, s & 1 ? 46 : 236, s & 1 ? 40 : 230, 0);
    }
    frustum(m, 8, 2.1f, 2.1f, 13.0f, 13.4f, 0, 0, 0.2f, 60, 60, 66, 0);
    frustum(m, 8, 1.4f, 0, 15.0f, 16.6f, 0, 0, 0.2f, 196, 46, 40, 0);
    m = &MESH[M_LAMP];
    frustum(m, 8, 1.2f, 1.2f, 13.4f, 15.0f, 0, 0, 0.2f, 255, 255, 255, 0);
    m = &MESH[M_STONE];
    frustum(m, 4, 0.65f, 0.4f, -0.5f, 3.4f, 0, 0, 0.3f, 112, 118, 132, 0);
    m = &MESH[M_EMBER];
    frustum(m, 4, 0, 0.45f, 0.0f, 0.6f, 0, 0, 0, 255, 140, 40, F_EMIS);
    frustum(m, 4, 0.45f, 0, 0.6f, 1.3f, 0, 0, 0, 255, 200, 70, F_EMIS);
    m = &MESH[M_SHEEP];
    frustum(m, 4, 0.75f, 0.75f, 0.45f, 1.15f, 0, 0, 0.7854f, 236, 232, 222, 0);
    frustum(m, 4, 0.3f, 0.26f, 0.7f, 1.3f, 0, 0.8f, 0.7854f, 50, 46, 44, 0);
    frustum(m, 4, 0.12f, 0.12f, 0, 0.5f, 0.3f, 0.3f, 0, 50, 46, 44, 0);
    frustum(m, 4, 0.12f, 0.12f, 0, 0.5f, -0.3f, -0.3f, 0, 50, 46, 44, 0);
    m = &MESH[M_CLOUD];
    frustum(m, 6, 6.0f, 4.0f, 0, 1.6f, 0, 0, 0, 250, 250, 252, 0);
    frustum(m, 6, 3.6f, 2.0f, 1.6f, 3.0f, 1.5f, 0.5f, 0.4f, 250, 250, 252, 0);
    m = &MESH[M_PILLAR];
    frustum(m, 6, 0.75f, 0.75f, -0.5f, 0.3f, 0, 0, 0, 190, 180, 160, 0);
    frustum(m, 6, 0.55f, 0.5f, 0.3f, 4.2f, 0, 0, 0, 214, 204, 182, 0);
}

/* ================================================================== the world's things */
typedef struct { int mesh; float x, y, z, yaw, s, rad; } Inst;
#define MAXI 1600
static Inst inst[MAXI];
static int ninst;

typedef struct { float x, y, z; bool lit; float lit_t; } Beacon;
typedef struct { float x, y, z; bool got; } Ember;
typedef struct { float x, z, yaw, tx, tz, speed; int t; } Sheep;
typedef struct { float cx, cz, r, h, ang, spd; } Gull;
typedef struct { float x, y, z, s, yaw; } Cloud;

static Beacon beacons[NBEACON];
static Ember embers[NEMBER];
static Sheep sheep[NSHEEP];
static Gull gulls[NGULL];
static Cloud clouds[NCLOUD];
static V3 lighthouse;
static V3 village;

static void add_inst(int mesh, float x, float z, float yaw, float s, float rad) {
    if (ninst >= MAXI) return;
    inst[ninst++] = (Inst){mesh, x, ground(x, z), z, yaw, s, rad};
}
static bool near_landmark(float x, float z, float d) {
    float d2 = d * d;
    for (int i = 0; i < NBEACON; i++) {
        float dx = x - beacons[i].x, dz = z - beacons[i].z;
        if (dx * dx + dz * dz < d2) return true;
    }
    float dx = x - lighthouse.x, dz = z - lighthouse.z;
    if (dx * dx + dz * dz < d2 * 1.6f) return true;
    for (int i = 0; i < ninst; i++) {
        if (inst[i].rad < 1.2f) continue;
        dx = x - inst[i].x; dz = z - inst[i].z;
        float rr = d + inst[i].rad;
        if (dx * dx + dz * dz < rr * rr) return true;
    }
    return false;
}
static bool standable(float x, float z) { return ground(x, z) > 0.8f && ground_normal(x, z).y > 0.82f; }

static void populate(void) {
    rt_seed = 777;
    ninst = 0;
    const float C = WORLD / 2;
    /* lighthouse: the northernmost good coastal spot (north = +z) */
    float best = -1;
    for (int j = 0; j < CELLS; j++)
        for (int i = 0; i < CELLS; i++) {
            float x = i * CS + 2, z = j * CS + 2, h = ground(x, z);
            if (h < 1.2f || h > 5 || !standable(x, z)) continue;
            float score = z - fabsf(x - C) * 0.5f;
            if (score > best) { best = score; lighthouse = v3(x, h, z); }
        }
    /* beacons: one per sector, on the best hill at mid-range */
    for (int b = 0; b < NBEACON; b++) {
        float a0 = b * 6.2832f / NBEACON + 1.1f;
        const float R = WORLD / 2;
        float bs = -1e9f;
        V3 pos = v3(C, 0, C);
        for (int k = 0; k < 400; k++) {
            float a = a0 + (rt_randf() - 0.5f) * 0.75f, r = R * (0.22f + rt_randf() * 0.55f);
            float x = C + sinf(a) * r, z = C + cosf(a) * r;
            if (!standable(x, z)) continue;
            float dlh = hypotf(x - lighthouse.x, z - lighthouse.z);
            if (dlh < 45) continue;
            bool crowded = false;
            for (int o = 0; o < b; o++) if (hypotf(x - beacons[o].x, z - beacons[o].z) < 50) crowded = true;
            if (crowded) continue;
            float s = ground(x, z) - fabsf(r - R * 0.5f) * 0.06f;
            if (s > bs) { bs = s; pos = v3(x, ground(x, z), z); }
        }
        beacons[b] = (Beacon){pos.x, pos.y, pos.z, false, 0};
    }
    add_inst(M_LIGHTHOUSE, lighthouse.x, lighthouse.z, 0, 1, 3.0f);
    for (int b = 0; b < NBEACON; b++) add_inst(M_BEACON, beacons[b].x, beacons[b].z, 0, 1, 1.4f);
    /* village: the flattest low ground, away from the lighthouse */
    best = 1e9f;
    for (int k = 0; k < 3000; k++) {
        float x = 30 + rt_randf() * (WORLD - 60), z = 30 + rt_randf() * (WORLD - 60), h = ground(x, z);
        if (h < 1.5f || h > 5 || hypotf(x - lighthouse.x, z - lighthouse.z) < 50) continue;
        float rough = 0;
        for (int s = 0; s < 8; s++) rough += fabsf(ground(x + cosf(s * 0.785f) * 9, z + sinf(s * 0.785f) * 9) - h);
        if (rough < best) { best = rough; village = v3(x, h, z); }
    }
    for (int k = 0, placed = 0; k < 200 && placed < 6; k++) {
        float a = rt_randf() * 6.2832f, r = 7 + rt_randf() * 9;
        float x = village.x + cosf(a) * r, z = village.z + sinf(a) * r;
        if (!standable(x, z) || near_landmark(x, z, 3.5f)) continue;
        float yaw = atan2f(village.x - x, village.z - z);
        add_inst(placed % 3 == 2 ? M_HUT : M_HOUSE, x, z, yaw, 1, 2.6f);
        placed++;
    }
    /* standing stones on the highest broad hill */
    best = -1;
    V3 ring = village;
    for (int k = 0; k < 3000; k++) {
        float x = 20 + rt_randf() * (WORLD - 40), z = 20 + rt_randf() * (WORLD - 40);
        if (!standable(x, z) || near_landmark(x, z, 12)) continue;
        float h = ground(x, z);
        if (h > best && h < 12) { best = h; ring = v3(x, h, z); }
    }
    for (int s = 0; s < 7; s++) {
        float a = s * 6.2832f / 7;
        add_inst(M_STONE, ring.x + cosf(a) * 5.5f, ring.z + sinf(a) * 5.5f, a, 0.9f + rt_randf() * 0.3f, 0.6f);
    }
    /* ruined colonnade somewhere else */
    for (int k = 0; k < 500; k++) {
        float x = 25 + rt_randf() * (WORLD - 50), z = 25 + rt_randf() * (WORLD - 50);
        if (!standable(x, z) || near_landmark(x, z, 14) || hypotf(x - ring.x, z - ring.z) < 40) continue;
        for (int p = 0; p < 6; p++)
            add_inst(M_PILLAR, x + (p % 3) * 4.0f, z + (p / 3) * 6.0f, 0, p == 4 ? 0.55f : 1.0f, 0.7f);
        break;
    }
    /* forest and rocks */
    for (int j = 0; j < CELLS; j++)
        for (int i = 0; i < CELLS; i++) {
            float x = i * CS + rt_randf() * CS, z = j * CS + rt_randf() * CS, h = ground(x, z);
            float dens = fbm(x * 0.02f, z * 0.02f, 21);
            if (h > 1.0f && h < 12.5f && ground_normal(x, z).y > 0.8f && rt_randf() < (dens - 0.35f) * 1.6f &&
                !near_landmark(x, z, 4)) {
                bool pine = h > 6 || rt_randf() < 0.3f;
                add_inst(pine ? M_PINE : M_OAK, x, z, rt_randf() * 6.28f, 0.8f + rt_randf() * 0.5f, 0.45f);
            } else if (h > 0.8f && rt_randf() < (h > 9 ? 0.12f : 0.025f) && !near_landmark(x, z, 4)) {
                float s = 0.6f + rt_randf() * 1.1f;
                add_inst(M_ROCK, x, z, rt_randf() * 6.28f, s, s * 1.0f);
            }
        }
    /* embers */
    for (int e = 0, tries = 0; e < NEMBER && tries < 20000; tries++) {
        float x = 12 + rt_randf() * (WORLD - 24), z = 12 + rt_randf() * (WORLD - 24);
        if (ground(x, z) < 0.6f || near_landmark(x, z, 2.5f)) continue;
        bool close = false;
        for (int k = 0; k < e; k++) if (hypotf(embers[k].x - x, embers[k].z - z) < 14) close = true;
        for (int k = 0; k < ninst && !close; k++)
            if (hypotf(inst[k].x - x, inst[k].z - z) < inst[k].rad + 1.2f) close = true;
        if (close) continue;
        embers[e++] = (Ember){x, ground(x, z) + 0.9f, z, false};
    }
    for (int s = 0; s < NSHEEP; s++) {
        float x, z;
        int k = 0;
        do {
            float a = rt_randf() * 6.2832f, r = 8 + rt_randf() * 30;
            x = village.x + cosf(a) * r;
            z = village.z + sinf(a) * r;
        } while ((!standable(x, z) || ground(x, z) > 7) && ++k < 200);
        sheep[s] = (Sheep){x, z, rt_randf() * 6.28f, x, z, 0, (int)(rt_rand() % 200)};
    }
    for (int g = 0; g < NGULL; g++)
        gulls[g] = (Gull){C + (rt_randf() - 0.5f) * 180, C + (rt_randf() - 0.5f) * 180, 10 + rt_randf() * 14,
                          18 + rt_randf() * 10, rt_randf() * 6.28f, (rt_randf() < 0.5f ? -1 : 1) * (0.3f + rt_randf() * 0.3f)};
    for (int c = 0; c < NCLOUD; c++)
        clouds[c] = (Cloud){rt_randf() * 400 - 70, 46 + rt_randf() * 12, rt_randf() * 400 - 70, 1 + rt_randf() * 1.2f,
                            rt_randf() * 6.28f};
}

/* ================================================================== sky, light and time of day */
typedef struct { float t, top[3], hor[3], light[3], amb, elev; } SkyKey;
static const SkyKey SKY[] = {
    {0.00f, {58, 108, 200}, {160, 196, 232}, {1.00f, 0.97f, 0.90f}, 0.45f, 0.75f},
    {0.30f, {66, 96, 176}, {236, 192, 140}, {1.00f, 0.84f, 0.64f}, 0.40f, 0.25f},
    {0.40f, {48, 46, 116}, {242, 122, 78}, {0.95f, 0.58f, 0.44f}, 0.34f, 0.02f},
    {0.50f, {6, 8, 26}, {24, 30, 62}, {0.36f, 0.44f, 0.70f}, 0.30f, -0.40f},
    {0.80f, {6, 8, 26}, {24, 30, 62}, {0.36f, 0.44f, 0.70f}, 0.30f, -0.40f},
    {0.90f, {60, 70, 140}, {232, 160, 150}, {0.90f, 0.70f, 0.66f}, 0.34f, 0.10f},
    {1.00f, {58, 108, 200}, {160, 196, 232}, {1.00f, 0.97f, 0.90f}, 0.45f, 0.75f},
};
static struct { float top[3], hor[3], light[3], amb, elev, night; V3 sun, lightdir; } sky;

static void set_time_of_day(float tod) {
    tod -= floorf(tod);
    int k = 0;
    while (k < 5 && SKY[k + 1].t < tod) k++;
    const SkyKey *a = &SKY[k], *b = &SKY[k + 1];
    float f = (tod - a->t) / (b->t - a->t);
    for (int c = 0; c < 3; c++) {
        sky.top[c] = lerpf(a->top[c], b->top[c], f);
        sky.hor[c] = lerpf(a->hor[c], b->hor[c], f);
        sky.light[c] = lerpf(a->light[c], b->light[c], f);
    }
    sky.amb = lerpf(a->amb, b->amb, f);
    sky.elev = lerpf(a->elev, b->elev, f);
    sky.night = clampf(1 - (sky.top[0] + sky.top[1] + sky.top[2]) / 120.0f, 0, 1);
    float az = 2.4f; /* the sun sets in the south-west */
    sky.sun = vnorm(v3(sinf(az) * cosf(sky.elev), sinf(sky.elev), cosf(az) * cosf(sky.elev)));
    float le = sky.elev > 0.35f ? sky.elev : 0.35f; /* the moon lights the night from the same side */
    sky.lightdir = vnorm(v3(sinf(az + 0.4f) * cosf(le), sinf(le), cosf(az + 0.4f) * cosf(le)));
}

/* ================================================================== rasterizer */
static Uint32 fb[RT_W * RT_H];
static float zb[RT_W * RT_H];
static SDL_Texture *fbtex;
static const unsigned char BAYER[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};

static struct { V3 pos; float yaw, pitch, cy, sy, cp, sp, fog0, fog1; V3 fwd, right; } cam;

static void cam_setup(V3 pos, float yaw, float pitch, float fog0, float fog1) {
    cam.pos = pos;
    cam.yaw = yaw;
    cam.pitch = pitch;
    cam.cy = cosf(yaw);
    cam.sy = sinf(yaw);
    cam.cp = cosf(pitch);
    cam.sp = sinf(pitch);
    cam.fwd = v3(cam.sy, 0, cam.cy);
    cam.right = v3(cam.cy, 0, -cam.sy);
    cam.fog0 = fog0;
    cam.fog1 = fog1;
}
static V3 to_cam(V3 p) {
    V3 d = vsub(p, cam.pos);
    float xr = d.x * cam.cy - d.z * cam.sy, zf = d.x * cam.sy + d.z * cam.cy;
    return v3(xr, d.y * cam.cp - zf * cam.sp, d.y * cam.sp + zf * cam.cp);
}
static V3 dir_to_cam(V3 d) {
    float xr = d.x * cam.cy - d.z * cam.sy, zf = d.x * cam.sy + d.z * cam.cy;
    return v3(xr, d.y * cam.cp - zf * cam.sp, d.y * cam.sp + zf * cam.cp);
}

static int qlevel(float c, int levels, float th) {
    float v = clampf(c, 0, 255) / 255.0f * (levels - 1);
    int lo = (int)v;
    if (v - lo > th && lo < levels - 1) lo++;
    return lo * 255 / (levels - 1);
}
static void quant16(const float rgb[3], Uint32 q[16]) {
    for (int i = 0; i < 16; i++) {
        float th = (i + 0.5f) / 16.0f;
        q[i] = 0xFF000000u | (Uint32)qlevel(rgb[0], 6, th) << 16 | (Uint32)qlevel(rgb[1], 7, th) << 8 |
               (Uint32)qlevel(rgb[2], 6, th);
    }
}

typedef struct { float x, y, iz; } SV;
static long tri_count;

static void raster_tri(SV a, SV b, SV c, const Uint32 *q) {
    SV t;
    if (a.y > b.y) { t = a; a = b; b = t; }
    if (b.y > c.y) { t = b; b = c; c = t; }
    if (a.y > b.y) { t = a; a = b; b = t; }
    if (c.y - a.y < 1e-6f) return;
    int y0 = (int)ceilf(a.y - 0.5f), y1 = (int)ceilf(c.y - 0.5f) - 1;
    if (y0 < 0) y0 = 0;
    if (y1 > RT_H - 1) y1 = RT_H - 1;
    for (int y = y0; y <= y1; y++) {
        float py = y + 0.5f;
        float tl = (py - a.y) / (c.y - a.y);
        float xl = a.x + (c.x - a.x) * tl, zl = a.iz + (c.iz - a.iz) * tl;
        float xr, zr;
        if (py < b.y) {
            float ts = (b.y - a.y) > 1e-6f ? (py - a.y) / (b.y - a.y) : 0;
            xr = a.x + (b.x - a.x) * ts; zr = a.iz + (b.iz - a.iz) * ts;
        } else {
            float ts = (c.y - b.y) > 1e-6f ? (py - b.y) / (c.y - b.y) : 0;
            xr = b.x + (c.x - b.x) * ts; zr = b.iz + (c.iz - b.iz) * ts;
        }
        if (xl > xr) { float s = xl; xl = xr; xr = s; s = zl; zl = zr; zr = s; }
        int x0 = (int)ceilf(xl - 0.5f), x1 = (int)ceilf(xr - 0.5f) - 1;
        if (x0 < 0) x0 = 0;
        if (x1 > RT_W - 1) x1 = RT_W - 1;
        if (x0 > x1) continue;
        float dz = xr - xl > 1e-6f ? (zr - zl) / (xr - xl) : 0;
        float iz = zl + (x0 + 0.5f - xl) * dz;
        const unsigned char *br = BAYER[y & 3];
        int row = y * RT_W;
        for (int x = x0; x <= x1; x++, iz += dz) {
            if (iz > zb[row + x]) {
                zb[row + x] = iz;
                fb[row + x] = q[br[x & 3]];
            }
        }
    }
}

/* camera-space triangle -> clip against the near plane -> project -> fill */
static void draw_cam_tri(V3 a, V3 b, V3 c, const Uint32 *q) {
    V3 in[3] = {a, b, c}, out[4];
    int n = 0;
    for (int i = 0; i < 3; i++) {
        V3 p = in[i], s = in[(i + 1) % 3];
        bool pin = p.z > NEARZ, sin_ = s.z > NEARZ;
        if (pin) out[n++] = p;
        if (pin != sin_) {
            float t = (NEARZ - p.z) / (s.z - p.z);
            out[n++] = v3(p.x + (s.x - p.x) * t, p.y + (s.y - p.y) * t, NEARZ);
        }
    }
    if (n < 3) return;
    SV sv[4];
    bool any_left = false, any_right = false, any_top = false, any_bot = false;
    for (int i = 0; i < n; i++) {
        float iz = 1.0f / out[i].z;
        sv[i] = (SV){RT_W / 2 + out[i].x * iz * FXP, RT_H / 2 - out[i].y * iz * FYP, iz};
        if (sv[i].x < RT_W) any_left = true;
        if (sv[i].x >= 0) any_right = true;
        if (sv[i].y < RT_H) any_top = true;
        if (sv[i].y >= 0) any_bot = true;
    }
    if (!any_left || !any_right || !any_top || !any_bot) return;
    tri_count++;
    for (int i = 1; i + 1 < n; i++) raster_tri(sv[0], sv[i], sv[i + 1], q);
}

static float fog_at(float d) { return clampf((d - cam.fog0) / (cam.fog1 - cam.fog0), 0, 1); }

/* shade, fog and draw one world-space triangle; returns false if culled */
static void world_tri(V3 a, V3 b, V3 c, V3 n, const float base[3], int fl) {
    V3 cen = v3((a.x + b.x + c.x) / 3, (a.y + b.y + c.y) / 3, (a.z + b.z + c.z) / 3);
    V3 toc = vsub(cam.pos, cen);
    float facing = vdot(n, toc);
    if (facing <= 0) {
        if (!(fl & F_DBL)) return;
        n = v3(-n.x, -n.y, -n.z);
    }
    float dist = sqrtf(vdot(toc, toc));
    if (dist > cam.fog1 + 8) return;
    float rgb[3];
    if (fl & F_EMIS) {
        for (int k = 0; k < 3; k++) rgb[k] = base[k];
    } else {
        float d = vdot(n, sky.lightdir);
        float k = sky.amb + (d > 0 ? d : 0) * (1 - sky.amb) * 0.95f;
        for (int i = 0; i < 3; i++) rgb[i] = base[i] * k * sky.light[i];
    }
    float f = fog_at(dist);
    if (fl & F_EMIS) f *= 0.4f; /* glowing things punch through the haze */
    for (int i = 0; i < 3; i++) rgb[i] = lerpf(rgb[i], sky.hor[i], f);
    Uint32 q[16];
    quant16(rgb, q);
    draw_cam_tri(to_cam(a), to_cam(b), to_cam(c), q);
}

static void draw_mesh(const Mesh *m, float x, float y, float z, float yaw, float s, const float *tint, int extra_fl) {
    static V3 w[MAXMV];
    float cy = cosf(yaw), sy = sinf(yaw);
    for (int i = 0; i < m->nv; i++) {
        V3 p = m->v[i];
        w[i] = v3(x + (p.x * cy + p.z * sy) * s, y + p.y * s, z + (-p.x * sy + p.z * cy) * s);
    }
    for (int i = 0; i < m->nt; i++) {
        const MTri *t = &m->t[i];
        V3 a = w[t->a], b = w[t->b], c = w[t->c];
        V3 n = vnorm(vcross(vsub(b, a), vsub(c, a)));
        float base[3] = {t->r, t->g, t->bl};
        if (tint) for (int k = 0; k < 3; k++) base[k] = tint[k];
        world_tri(a, b, c, n, base, t->fl | extra_fl);
    }
}

static bool in_view(float x, float z, float rad, float maxd) {
    float dx = x - cam.pos.x, dz = z - cam.pos.z;
    if (dx * dx + dz * dz > (maxd + rad) * (maxd + rad)) return false;
    float zf = dx * cam.sy + dz * cam.cy, xr = dx * cam.cy - dz * cam.sy;
    if (zf < -rad - 6) return false;
    float spread = fabsf(cam.pitch) > 0.5f ? 1.6f : 0.95f;
    return fabsf(xr) < (zf > 0 ? zf : 0) * spread + rad + 8;
}

/* depth-tested 3D point (particles) */
static void draw_point(V3 p, Uint32 col, int size) {
    V3 c = to_cam(p);
    if (c.z < NEARZ) return;
    float iz = 1 / c.z;
    int sx = (int)(RT_W / 2 + c.x * iz * FXP), sy = (int)(RT_H / 2 - c.y * iz * FYP);
    for (int dy = 0; dy < size; dy++)
        for (int dx = 0; dx < size; dx++) {
            int x = sx + dx, y = sy + dy;
            if (x < 0 || y < 0 || x >= RT_W || y >= RT_H) continue;
            if (iz > zb[y * RT_W + x]) fb[y * RT_W + x] = col;
        }
}

/* a column of light, drawn screen-door transparent as the old games did */
static void draw_beam(V3 base, float height, Uint32 col, float width) {
    V3 a = to_cam(base), b = to_cam(v3(base.x, base.y + height, base.z));
    if (a.z < NEARZ || b.z < NEARZ) return;
    float ia = 1 / a.z, ib = 1 / b.z;
    float ax = RT_W / 2 + a.x * ia * FXP, ay = RT_H / 2 - a.y * ia * FYP;
    float bx = RT_W / 2 + b.x * ib * FXP, by = RT_H / 2 - b.y * ib * FYP;
    if (ay < by) return;
    int y0 = (int)by, y1 = (int)ay;
    if (y0 < 0) y0 = 0;
    if (y1 > RT_H - 1) y1 = RT_H - 1;
    for (int y = y0; y <= y1; y++) {
        float t = (ay - by) > 0.5f ? (y - by) / (ay - by) : 0;
        float x = bx + (ax - bx) * t, iz = ib + (ia - ib) * t;
        int hw = (int)(width * iz * FXP * 0.5f);
        if (hw < 0) hw = 0;
        if (hw > 30) hw = 30;
        for (int xx = (int)x - hw; xx <= (int)x + hw; xx++) {
            if (xx < 0 || xx >= RT_W || ((xx + y) & 1)) continue;
            int i = y * RT_W + xx;
            if (iz > zb[i]) fb[i] = col;
        }
    }
}

/* ================================================================== game state */
enum { S_TITLE, S_INTRO, S_PLAY, S_WIN, S_ENTRY };
typedef struct { float fwd, strafe, turn, look; bool jump, sprint; } In;
typedef struct { V3 p, v; int life; Uint32 col; } Spark;
#define MAXS 300

static struct {
    int state, st, title_page;
    float tod, time;
    V3 pos;
    float vy, yaw, pitch, step_acc;
    bool ground, swim, was_swim;
    int beacons_lit, embers_got, carry, score, need_t;
    bool all_lit, won;
    float win_t;
    char msg[64];
    int msg_t;
    bool map, paused, quitq, mouse;
    float mdx, mdy;
    char name[4];
    int name_len, rank, letter;
    long frames;
} G;
static Spark sparks[MAXS];
static bool selftest;

static void message(const char *m) {
    snprintf(G.msg, sizeof(G.msg), "%s", m);
    G.msg_t = 240;
}
static void spark_burst(V3 p, int n, Uint32 c1, Uint32 c2, float spd) {
    for (int i = 0; i < MAXS && n > 0; i++) {
        if (sparks[i].life > 0) continue;
        float a = rt_randf() * 6.28f, e = rt_randf() * 1.4f;
        sparks[i] = (Spark){p, v3(cosf(a) * cosf(e) * spd, sinf(e) * spd + spd * 0.5f, sinf(a) * cosf(e) * spd),
                            30 + (int)(rt_rand() % 40), rt_rand() & 1 ? c1 : c2};
        n--;
    }
}

/* ================================================================== sound */
static float semi(int s) { return 440.0f * powf(2.0f, s / 12.0f); }
static void sfx_ember(void) { RtNote n[] = {{1568, 1568, 40}, {2093, 2093, 40}, {2637, 2637, 70}}; rt_play(n, 3, 4); }
static void sfx_beacon(void) {
    static const int sc[] = {-9, -5, 0, 3, 7, 12};
    RtNote n[12];
    for (int i = 0; i < 6; i++) {
        n[i * 2] = (RtNote){semi(sc[i]), semi(sc[i]), i == 5 ? 260 : 90};
        n[i * 2 + 1] = (RtNote){0, 0, 15};
    }
    rt_play(n, 12, 7);
}
static void sfx_splash(void) { RtNote n[] = {{900, 200, 90}, {500, 120, 120}}; rt_play(n, 2, 3); }
static void sfx_step(bool sand) { rt_tone(sand ? 110 : 75, 55, 14, 0); }
static void sfx_jump(void) { rt_tone(180, 360, 60, 2); }
static void sfx_bleat(void) {
    RtNote n[8];
    for (int i = 0; i < 8; i++) n[i] = (RtNote){i & 1 ? 330.0f : 300.0f, i & 1 ? 320.0f : 290.0f, 28};
    rt_play(n, 8, 1);
}
static void sfx_gull(void) { RtNote n[] = {{1900, 1300, 110}, {0, 0, 40}, {1800, 1200, 90}}; rt_play(n, 3, 1); }
static void tune_title(void) {
    static const int mel[][2] = {{-9, 3}, {-5, 1}, {-2, 2}, {0, 2}, {-2, 2}, {-5, 2}, {-7, 4}, {-9, 2}, {-12, 2},
                                 {-9, 3}, {-5, 1}, {0, 2}, {3, 2}, {2, 2}, {-2, 2}, {0, 8}};
    RtNote n[32];
    for (int i = 0; i < 16; i++) {
        float f = semi(mel[i][0]);
        n[i * 2] = (RtNote){f, f, mel[i][1] * 110 - 20};
        n[i * 2 + 1] = (RtNote){0, 0, 20};
    }
    rt_play(n, 32, 2);
}
static void tune_win(void) {
    static const int mel[][2] = {{0, 2}, {4, 2}, {7, 2}, {12, 4}, {11, 2}, {7, 2}, {9, 2}, {12, 6}, {16, 8}};
    RtNote n[18];
    for (int i = 0; i < 9; i++) {
        float f = semi(mel[i][0] - 9);
        n[i * 2] = (RtNote){f, f, mel[i][1] * 90};
        n[i * 2 + 1] = (RtNote){0, 0, 20};
    }
    rt_play(n, 18, 9);
}

/* ================================================================== simulation */
static void collide_objects(V3 *p) {
    for (int i = 0; i < ninst; i++) {
        Inst *o = &inst[i];
        if (o->rad <= 0) continue;
        float dx = p->x - o->x, dz = p->z - o->z, r = o->rad + 0.35f;
        float d2 = dx * dx + dz * dz;
        if (d2 >= r * r || p->y > o->y + 6 * o->s) continue;
        float d = sqrtf(d2);
        if (d < 1e-4f) { dx = 1; d = 1; dz = 0; }
        p->x = o->x + dx / d * r;
        p->z = o->z + dz / d * r;
    }
}

static void win_game(void) {
    G.won = true;
    G.win_t = 0;
    int bonus = (int)fmaxf(0, 900 - G.time) * 10;
    G.score += 5000 + bonus;
    tune_win();
    message("THE LIGHTHOUSE BURNS AGAIN!");
}

static void step_player(const In *in, float dt) {
    G.yaw += in->turn * 2.3f * dt + G.mdx * 0.0035f;
    G.pitch = clampf(G.pitch + in->look * 1.4f * dt - G.mdy * 0.0035f, -1.1f, 0.9f);
    G.mdx = G.mdy = 0;
    float spd = G.swim ? 3.2f : in->sprint ? 10.0f : 6.0f;
    float fx = sinf(G.yaw), fz = cosf(G.yaw);
    float mx = (fx * in->fwd + fz * in->strafe), mz = (fz * in->fwd - fx * in->strafe);
    float ml = sqrtf(mx * mx + mz * mz);
    if (ml > 1) { mx /= ml; mz /= ml; }
    V3 np = v3(G.pos.x + mx * spd * dt, G.pos.y, G.pos.z + mz * spd * dt);
    float lim = WORLD + 40;
    np.x = clampf(np.x, -40, lim);
    np.z = clampf(np.z, -40, lim);
    collide_objects(&np);
    G.pos.x = np.x;
    G.pos.z = np.z;
    float gh = ground(G.pos.x, G.pos.z);
    G.swim = gh < -1.1f;
    if (G.swim) {
        G.pos.y = lerpf(G.pos.y, -1.1f, 0.2f);
        G.vy = 0;
        G.ground = false;
    } else {
        if (in->jump && G.ground) { G.vy = 7.5f; G.ground = false; sfx_jump(); }
        G.vy -= 22 * dt;
        G.pos.y += G.vy * dt;
        if (G.pos.y <= gh) { G.pos.y = gh; G.vy = 0; G.ground = true; }
        else if (G.pos.y > gh + 0.05f) G.ground = false;
    }
    if (G.swim && !G.was_swim) sfx_splash();
    G.was_swim = G.swim;
    if (G.ground && ml > 0.1f) {
        G.step_acc += spd * dt;
        if (G.step_acc > 2.4f) { G.step_acc = 0; sfx_step(gh < 0.7f); }
    }
}

static void step_world(float dt) {
    G.time += dt;
    G.tod += dt / DAY_LEN;
    /* beacons */
    for (int b = 0; b < NBEACON; b++) {
        Beacon *k = &beacons[b];
        if (k->lit) { k->lit_t += dt; continue; }
        if (hypotf(G.pos.x - k->x, G.pos.z - k->z) < 2.6f && fabsf(G.pos.y - k->y) < 3) {
            if (G.carry < 3) {
                if (G.need_t <= 0) {
                    char m[64];
                    snprintf(m, sizeof(m), "NEEDS 3 EMBERS - YOU CARRY %d", G.carry);
                    message(m);
                    rt_tone(220, 150, 120, 2);
                }
                G.need_t = 180;
                continue;
            }
            G.carry -= 3;
            k->lit = true;
            G.beacons_lit++;
            G.score += 1000;
            spark_burst(v3(k->x, k->y + 4, k->z), 60, 0xFFFFD050, 0xFFFF8020, 6);
            sfx_beacon();
            char m[64];
            if (G.beacons_lit < NBEACON) snprintf(m, sizeof(m), "BEACON %d OF %d LIT!", G.beacons_lit, NBEACON);
            else snprintf(m, sizeof(m), "ALL BEACONS LIT! TO THE LIGHTHOUSE!");
            message(m);
            if (G.beacons_lit == NBEACON) G.all_lit = true;
        }
    }
    if (G.all_lit && !G.won && hypotf(G.pos.x - lighthouse.x, G.pos.z - lighthouse.z) < 4.5f) win_game();
    if (G.won) G.win_t += dt;
    /* embers */
    for (int e = 0; e < NEMBER; e++) {
        Ember *m = &embers[e];
        if (m->got) continue;
        float dx = G.pos.x - m->x, dz = G.pos.z - m->z, dy = (G.pos.y + 1) - m->y;
        if (dx * dx + dz * dz < 2.6f && fabsf(dy) < 2.2f) {
            m->got = true;
            G.embers_got++;
            G.carry++;
            G.score += 100;
            spark_burst(v3(m->x, m->y, m->z), 18, 0xFFFFC040, 0xFFFFFFFF, 3);
            sfx_ember();
        }
    }
    /* sheep wander, and scatter from the player */
    for (int s = 0; s < NSHEEP; s++) {
        Sheep *q = &sheep[s];
        q->t++;
        float dx = q->x - G.pos.x, dz = q->z - G.pos.z, d = sqrtf(dx * dx + dz * dz);
        if (d < 7) {
            q->tx = q->x + dx / (d + 0.01f) * 8;
            q->tz = q->z + dz / (d + 0.01f) * 8;
            q->speed = 4.5f;
            if (q->t % 90 == 0 && d < 12) sfx_bleat();
        } else if (q->t % 240 == 0) {
            q->tx = q->x + (rt_randf() - 0.5f) * 10;
            q->tz = q->z + (rt_randf() - 0.5f) * 10;
            q->speed = 1.0f;
        }
        float ex = q->tx - q->x, ez = q->tz - q->z, el = sqrtf(ex * ex + ez * ez);
        if (el > 0.3f) {
            float nx = q->x + ex / el * q->speed * dt, nz = q->z + ez / el * q->speed * dt;
            if (standable(nx, nz) && ground(nx, nz) < 8) {
                q->x = nx;
                q->z = nz;
                float ty = atan2f(ex, ez), dyaw = remainderf(ty - q->yaw, 6.2832f);
                q->yaw += dyaw * 0.15f;
            } else {
                q->tx = q->x; q->tz = q->z;
            }
        }
    }
    for (int g = 0; g < NGULL; g++) {
        gulls[g].ang += gulls[g].spd * dt;
        if (!selftest && rt_rand() % 2400 == 0) {
            float dx = gulls[g].cx - G.pos.x, dz = gulls[g].cz - G.pos.z;
            if (dx * dx + dz * dz < 60 * 60) sfx_gull();
        }
    }
    for (int c = 0; c < NCLOUD; c++) {
        clouds[c].x += 1.2f * dt;
        if (clouds[c].x > 330) clouds[c].x -= 400;
    }
    for (int i = 0; i < MAXS; i++) {
        Spark *s = &sparks[i];
        if (s->life <= 0) continue;
        s->life--;
        s->p = v3(s->p.x + s->v.x * dt, s->p.y + s->v.y * dt, s->p.z + s->v.z * dt);
        s->v.y -= 6 * dt;
    }
    /* fire embers drift up from lit beacons */
    for (int b = 0; b < NBEACON; b++)
        if (beacons[b].lit && rt_rand() % 6 == 0)
            for (int i = 0; i < MAXS; i++)
                if (sparks[i].life <= 0) {
                    sparks[i] = (Spark){v3(beacons[b].x + (rt_randf() - 0.5f), beacons[b].y + 4.2f, beacons[b].z + (rt_randf() - 0.5f)),
                                        v3((rt_randf() - 0.5f) * 0.8f, 2.5f + rt_randf() * 2, (rt_randf() - 0.5f) * 0.8f), 50,
                                        rt_rand() & 1 ? 0xFFFFE070 : 0xFFFF7020};
                    break;
                }
    if (G.msg_t > 0) G.msg_t--;
    if (G.need_t > 0) G.need_t--;
}

/* ================================================================== world rendering */
static Uint32 rgb32(float r, float g, float b) {
    return 0xFF000000u | (Uint32)clampf(r, 0, 255) << 16 | (Uint32)clampf(g, 0, 255) << 8 | (Uint32)clampf(b, 0, 255);
}

static void draw_sky(void) {
    for (int y = 0; y < RT_H; y++) {
        float ang = cam.pitch + atanf((RT_H / 2 - y - 0.5f) / FYP);
        float t = clampf(ang / 0.7f, 0, 1);
        t = sqrtf(t);
        float rgb[3];
        for (int c = 0; c < 3; c++) rgb[c] = lerpf(sky.hor[c], sky.top[c], t);
        Uint32 q[16];
        quant16(rgb, q);
        const unsigned char *br = BAYER[y & 3];
        for (int x = 0; x < RT_W; x++) {
            fb[y * RT_W + x] = q[br[x & 3]];
            zb[y * RT_W + x] = 0;
        }
    }
    /* stars */
    if (sky.night > 0.05f) {
        Uint32 st = rgb32(255 * sky.night, 255 * sky.night, 240 * sky.night);
        for (int i = 0; i < 220; i++) {
            float a = hash2(i, 1, 40) * 6.2832f, e = 0.08f + hash2(i, 2, 40) * 1.4f;
            V3 c = dir_to_cam(v3(sinf(a) * cosf(e), sinf(e), cosf(a) * cosf(e)));
            if (c.z <= 0.01f) continue;
            int sx = (int)(RT_W / 2 + c.x / c.z * FXP), sy = (int)(RT_H / 2 - c.y / c.z * FYP);
            if (sx >= 0 && sy >= 0 && sx < RT_W && sy < RT_H && (i % 7 || ((G.frames / 20 + i) % 5))) fb[sy * RT_W + sx] = st;
        }
    }
    /* sun or moon */
    V3 dir = sky.elev > -0.05f ? sky.sun : vnorm(v3(-sky.sun.x, 0.6f, -sky.sun.z));
    V3 c = dir_to_cam(dir);
    if (c.z > 0.05f) {
        int sx = (int)(RT_W / 2 + c.x / c.z * FXP), sy = (int)(RT_H / 2 - c.y / c.z * FYP);
        bool moon = sky.elev <= -0.05f;
        Uint32 core = moon ? 0xFFE8ECF4 : (sky.elev < 0.3f ? 0xFFFFC870 : 0xFFFFFFD0);
        Uint32 halo = moon ? 0xFF8890B0 : (sky.elev < 0.3f ? 0xFFFF9050 : 0xFFFFF0A0);
        for (int dy = -12; dy <= 12; dy++)
            for (int dx = -12; dx <= 12; dx++) {
                int x = sx + dx, y = sy + dy;
                if (x < 0 || y < 0 || x >= RT_W || y >= RT_H) continue;
                float r = sqrtf((float)(dx * dx) + dy * dy * 1.44f);
                if (r < 6) fb[y * RT_W + x] = core;
                else if (r < 11 && ((x + y) & 1) == 0 && BAYER[y & 3][x & 3] > (r - 6) * 3) fb[y * RT_W + x] = halo;
            }
        if (moon) for (int dy = -3; dy <= 2; dy++)
            for (int dx = 1; dx <= 4; dx++) {
                int x = sx + dx, y = sy + dy - 1;
                if (x >= 0 && y >= 0 && x < RT_W && y < RT_H && dx * dx + dy * dy < 12) fb[y * RT_W + x] = 0xFFB8BED0;
            }
    }
}

static void draw_terrain(int radius) {
    int ci = (int)floorf(cam.pos.x / CS), cj = (int)floorf(cam.pos.z / CS);
    float wt = G.frames * 0.03f;
    for (int dj = -radius; dj <= radius; dj++)
        for (int di = -radius; di <= radius; di++) {
            if (di * di + dj * dj > radius * radius) continue;
            int i = ci + di, j = cj + dj;
            float cx = i * CS + CS / 2, cz = j * CS + CS / 2;
            if (!in_view(cx, cz, CS, radius * CS)) continue;
            bool inside = i >= 0 && j >= 0 && i < CELLS && j < CELLS;
            float h00 = inside ? H[j][i] : SEA, h10 = inside ? H[j][i + 1] : SEA;
            float h01 = inside ? H[j + 1][i] : SEA, h11 = inside ? H[j + 1][i + 1] : SEA;
            V3 a = v3(i * CS, h00, j * CS), b = v3((i + 1) * CS, h10, j * CS);
            V3 c = v3(i * CS, h01, (j + 1) * CS), d = v3((i + 1) * CS, h11, (j + 1) * CS);
            if (inside) {
                if (h00 > -0.3f || h10 > -0.3f || h11 > -0.3f) world_tri(a, b, d, cell_n[j][i][0], cell_rgb[j][i][0], 0);
                if (h00 > -0.3f || h11 > -0.3f || h01 > -0.3f) world_tri(a, d, c, cell_n[j][i][1], cell_rgb[j][i][1], 0);
            }
            float mn = fminf(fminf(h00, h10), fminf(h01, h11));
            if (mn < 0.05f) { /* sea: flat, faceted, with sun glints */
                V3 wa = v3(a.x, 0, a.z), wb = v3(b.x, 0, b.z), wc = v3(c.x, 0, c.z), wd = v3(d.x, 0, d.z);
                for (int k = 0; k < 2; k++) {
                    float shim = sinf(wt * 1.7f + i * 0.9f + j * 1.3f + k * 2.1f) * 10;
                    float shore = mn > -1.0f ? 18 : 0;
                    float col[3] = {38 + shim + shore, 92 + shim + shore, 158 + shim * 0.5f + shore * 0.5f};
                    V3 cen = v3(cx, 0, cz);
                    V3 vd = vnorm(vsub(cen, cam.pos));
                    V3 rf = v3(vd.x, -vd.y, vd.z);
                    float glint = vdot(rf, sky.elev > -0.05f ? sky.sun : vnorm(v3(-sky.sun.x, 0.6f, -sky.sun.z)));
                    if (glint > 0.985f - 0.01f * sinf(wt + i * 3.1f + j)) {
                        float g = sky.elev > -0.05f ? 140 : 70;
                        col[0] += g; col[1] += g; col[2] += g * 0.7f;
                    }
                    if (k == 0) world_tri(wa, wb, wd, v3(0, 1, 0), col, F_EMIS * 0);
                    else world_tri(wa, wd, wc, v3(0, 1, 0), col, 0);
                }
            }
        }
}

static void draw_objects(float maxd) {
    for (int i = 0; i < ninst; i++) {
        Inst *o = &inst[i];
        if (!in_view(o->x, o->z, 6 * o->s, maxd)) continue;
        draw_mesh(&MESH[o->mesh], o->x, o->y, o->z, o->yaw, o->s, NULL, 0);
    }
    float flick = 0.85f + 0.15f * sinf(G.frames * 0.4f);
    for (int b = 0; b < NBEACON; b++) {
        Beacon *k = &beacons[b];
        if (!k->lit || !in_view(k->x, k->z, 6, maxd)) continue;
        draw_mesh(&MESH[M_FIRE], k->x, k->y + (1 - flick) * 1.5f, k->z, G.frames * 0.05f, flick, NULL, 0);
    }
    float lamp[3] = {80, 90, 110};
    if (G.won) { lamp[0] = 255; lamp[1] = 236; lamp[2] = 140; }
    draw_mesh(&MESH[M_LAMP], lighthouse.x, lighthouse.y, lighthouse.z, 0, 1, lamp, G.won ? F_EMIS : 0);
    for (int e = 0; e < NEMBER; e++) {
        Ember *m = &embers[e];
        if (m->got || !in_view(m->x, m->z, 2, maxd)) continue;
        draw_mesh(&MESH[M_EMBER], m->x, m->y + sinf(G.frames * 0.05f + e) * 0.25f - 0.6f, m->z, G.frames * 0.04f + e, 1, NULL, 0);
    }
    for (int s = 0; s < NSHEEP; s++) {
        Sheep *q = &sheep[s];
        if (!in_view(q->x, q->z, 2, maxd)) continue;
        float hop = q->speed > 2 ? fabsf(sinf(q->t * 0.4f)) * 0.3f : 0;
        draw_mesh(&MESH[M_SHEEP], q->x, ground(q->x, q->z) + hop, q->z, q->yaw, 1, NULL, 0);
    }
    for (int c = 0; c < NCLOUD; c++) {
        Cloud *k = &clouds[c];
        if (!in_view(k->x, k->z, 12 * k->s, cam.fog1 + 40)) continue;
        draw_mesh(&MESH[M_CLOUD], k->x, k->y, k->z, k->yaw, k->s, NULL, 0);
    }
    for (int g = 0; g < NGULL; g++) {
        Gull *u = &gulls[g];
        float x = u->cx + cosf(u->ang) * u->r, z = u->cz + sinf(u->ang) * u->r;
        if (!in_view(x, z, 2, maxd)) continue;
        float yaw = u->ang + (u->spd > 0 ? 1.5708f : -1.5708f), flap = sinf(G.frames * 0.25f + g) * 0.5f;
        float fx = sinf(yaw), fz = cosf(yaw), rx = cosf(yaw), rz = -sinf(yaw);
        V3 head = v3(x + fx * 0.5f, u->h, z + fz * 0.5f), tail = v3(x - fx * 0.5f, u->h, z - fz * 0.5f);
        V3 lw = v3(x - rx * 1.4f, u->h + flap, z - rz * 1.4f), rw = v3(x + rx * 1.4f, u->h + flap, z + rz * 1.4f);
        float white[3] = {235, 235, 240};
        world_tri(head, tail, lw, v3(0, 1, 0), white, F_DBL);
        world_tri(head, rw, tail, v3(0, 1, 0), white, F_DBL);
    }
}

static void draw_effects(void) {
    for (int b = 0; b < NBEACON; b++)
        if (beacons[b].lit) {
            Uint32 col = sky.night > 0.4f ? 0xFFFFE890 : 0xFFFFF4C0;
            draw_beam(v3(beacons[b].x, beacons[b].y + 4.5f, beacons[b].z), 70, col, 1.0f);
        }
    if (G.won) { /* the lighthouse lamp sweeps */
        float a = G.win_t * 1.2f;
        for (int k = 0; k < 2; k++) {
            float aa = a + k * 3.1416f;
            V3 o = v3(lighthouse.x, lighthouse.y + 14.2f, lighthouse.z);
            for (int s = 1; s < 40; s++) {
                float d = s * 2.0f;
                draw_point(v3(o.x + sinf(aa) * d, o.y + 0.2f, o.z + cosf(aa) * d), 0xFFFFF0A0, 1 + (s < 10));
            }
        }
    }
    for (int i = 0; i < MAXS; i++)
        if (sparks[i].life > 0) draw_point(sparks[i].p, sparks[i].col, 1);
    for (int e = 0; e < NEMBER; e++) { /* embers twinkle through the haze */
        Ember *m = &embers[e];
        if (m->got || (G.frames / 6 + e) % 4) continue;
        float dx = m->x - cam.pos.x, dz = m->z - cam.pos.z;
        if (dx * dx + dz * dz < 220.0f * 220.0f) draw_point(v3(m->x, m->y + 0.4f, m->z), 0xFFFFD060, 1);
    }
}

static void render_world(V3 eye, float yaw, float pitch, float fog0, float fog1, int radius) {
    set_time_of_day(G.tod);
    cam_setup(eye, yaw, pitch, fog0, fog1);
    draw_sky();
    draw_terrain(radius);
    draw_objects(fog1);
    draw_effects();
    SDL_UpdateTexture(fbtex, NULL, fb, RT_W * 4);
    SDL_RenderCopy(rt_ren, fbtex, NULL, NULL);
}

static V3 player_eye(void) {
    float bob = G.ground ? sinf(G.step_acc * 2.6f) * 0.06f : 0;
    if (G.swim) return v3(G.pos.x, 0.45f + sinf(G.frames * 0.05f) * 0.08f, G.pos.z);
    return v3(G.pos.x, G.pos.y + EYE + bob, G.pos.z);
}

/* ================================================================== HUD and screens */
static SDL_Texture *maptex;
#define MAPPX 128

static void build_map(void) {
    static Uint32 px[MAPPX * MAPPX];
    for (int r = 0; r < MAPPX; r++)
        for (int c = 0; c < MAPPX; c++) {
            float x = c * WORLD / MAPPX, z = (MAPPX - 1 - r) * WORLD / MAPPX;
            float h = ground(x, z), rgb[3];
            if (h < 0) {
                float d = clampf(-h / 6, 0, 1);
                rgb[0] = 40 - 20 * d; rgb[1] = 100 - 40 * d; rgb[2] = 170 - 40 * d;
            } else {
                V3 n = ground_normal(x, z);
                tri_color(h, n.y, 0.5f, rgb);
                float sh = 0.7f + 0.5f * (n.x * -0.6f + n.z * 0.6f + n.y * 0.5f);
                for (int k = 0; k < 3; k++) rgb[k] *= sh;
            }
            Uint32 q[16];
            quant16(rgb, q);
            px[r * MAPPX + c] = q[BAYER[r & 3][c & 3]];
        }
    maptex = SDL_CreateTexture(rt_ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, MAPPX, MAPPX);
    if (maptex) SDL_UpdateTexture(maptex, NULL, px, MAPPX * 4);
}

static void map_xy(float x, float z, int ox, int oy, int *mx, int *my) {
    *mx = ox + (int)(x / WORLD * MAPPX);
    *my = oy + MAPPX - 1 - (int)(z / WORLD * MAPPX);
}

static void draw_map(void) {
    int ox = (RT_W - MAPPX) / 2, oy = 42;
    rt_box(ox - 10, oy - 22, MAPPX + 20, MAPPX + 32, C_BLUE, C_WHITE);
    rt_textc("MAP OF EMBER ISLE", oy - 13, C_YELLOW);
    SDL_Rect d = {ox, oy, MAPPX, MAPPX};
    SDL_RenderCopy(rt_ren, maptex, NULL, &d);
    int mx, my;
    map_xy(lighthouse.x, lighthouse.z, ox, oy, &mx, &my);
    rt_rect(mx - 1, my - 3, 3, 6, C_WHITE);
    rt_px(mx, my - 4, G.won ? C_YELLOW : C_RED);
    for (int b = 0; b < NBEACON; b++) {
        map_xy(beacons[b].x, beacons[b].z, ox, oy, &mx, &my);
        rt_rect(mx - 2, my - 2, 5, 5, C_BLACK);
        rt_rect(mx - 1, my - 1, 3, 3, beacons[b].lit ? C_YELLOW : C_LGRAY);
    }
    map_xy(G.pos.x, G.pos.z, ox, oy, &mx, &my);
    if ((G.frames / 10) % 2) {
        rt_disc(mx, my, 2, C_LRED);
        rt_line(mx, my, mx + (int)(sinf(G.yaw) * 6), my - (int)(cosf(G.yaw) * 6), C_WHITE);
    }
}

static void draw_compass(void) {
    int cx = RT_W / 2, w = 150, y = 4;
    rt_rect(cx - w / 2 - 2, y - 2, w + 4, 15, C_BLACK);
    rt_frame(cx - w / 2 - 2, y - 2, w + 4, 15, C_DGRAY);
    float span = 1.6f; /* radians shown across the strip */
    for (int k = 0; k < 24; k++) {
        float a = k * 6.2832f / 24;
        float d = remainderf(a - G.yaw, 6.2832f);
        if (fabsf(d) > span / 2) continue;
        int x = cx + (int)(d / span * w);
        if (k % 6 == 0) {
            const char *L[] = {"N", "E", "S", "W"};
            rt_text(L[k / 6], x - 3, y + 2, k == 0 ? C_LRED : C_WHITE);
        } else rt_rect(x, y + (k % 2 ? 7 : 5), 1, k % 2 ? 3 : 5, C_LGRAY);
    }
    for (int b = 0; b <= NBEACON; b++) {
        float tx, tz;
        int col;
        if (b == NBEACON) {
            if (!G.all_lit || G.won) continue;
            tx = lighthouse.x; tz = lighthouse.z;
            col = (G.frames / 8) % 2 ? C_WHITE : C_LRED;
        } else {
            tx = beacons[b].x; tz = beacons[b].z;
            col = beacons[b].lit ? C_YELLOW : C_LCYAN;
        }
        float d = remainderf(atan2f(tx - G.pos.x, tz - G.pos.z) - G.yaw, 6.2832f);
        if (fabsf(d) > span / 2) {
            if (b == NBEACON || !beacons[b].lit) { /* arrows at the edges for targets out of view */
                int x = d < 0 ? cx - w / 2 - 10 : cx + w / 2 + 4;
                rt_text(d < 0 ? "<" : ">", x, y + 2, col);
            }
            continue;
        }
        int x = cx + (int)(d / span * w);
        rt_rect(x - 1, y + 10, 3, 3, col);
    }
}

static void draw_hud(void) {
    char b[64];
    draw_compass();
    rt_rect(0, RT_H - 13, RT_W, 13, C_BLACK);
    rt_rect(0, RT_H - 14, RT_W, 1, C_DGRAY);
    snprintf(b, sizeof(b), "BEACONS %d/%d", G.beacons_lit, NBEACON);
    rt_text(b, 4, RT_H - 10, G.all_lit ? C_YELLOW : C_WHITE);
    snprintf(b, sizeof(b), "EMBERS %d", G.carry);
    rt_text(b, 112, RT_H - 10, G.carry >= 3 ? C_YELLOW : C_LRED);
    snprintf(b, sizeof(b), "%d/%d", G.embers_got, NEMBER);
    rt_text(b, 196, RT_H - 10, C_DGRAY);
    int t = (int)G.time;
    snprintf(b, sizeof(b), "%02d:%02d", t / 60, t % 60);
    rt_text(b, RT_W - 44, RT_H - 10, C_LCYAN);
    if (G.msg_t > 0 && ((G.msg_t / 12) % 2 || G.msg_t > 180)) rt_textc_sh(G.msg, 26, C_YELLOW);
    if (G.swim) rt_text_sh("SWIMMING", 4, 22, C_LCYAN);
    if (G.map) draw_map();
    if (G.quitq) {
        rt_box(60, 76, 200, 46, C_BLUE, C_WHITE);
        rt_textc("QUIT TO TITLE?  (Y/N)", 95, C_WHITE);
    } else if (G.paused) {
        rt_box(100, 76, 120, 46, C_BLUE, C_WHITE);
        rt_textc("PAUSED", 95, (G.frames / 20) % 2 ? C_WHITE : C_YELLOW);
    }
}

static void draw_logo(int y) {
    static const int rows[7] = {C_WHITE, C_YELLOW, C_YELLOW, C_LRED, C_LRED, C_RED, C_RED};
    const char *a = "EMBER ISLE";
    int w = rt_text_w(a, 3);
    rt_text_ex(a, (RT_W - w) / 2 + 2, y + 2, 3, C_BLACK, NULL);
    rt_text_ex(a, (RT_W - w) / 2, y, 3, 0, rows);
}

static void orbit_view(float t, float dist, float h) {
    const float C = WORLD / 2;
    float a = t * 0.06f;
    V3 eye = v3(C + sinf(a) * dist, h, C + cosf(a) * dist);
    float yaw = atan2f(C - eye.x, C - eye.z);
    float pitch = -atan2f(h - 4, dist);
    render_world(eye, yaw, pitch, 160, 420, 105);
}

static void draw_title(void) {
    orbit_view(G.frames / 60.0f, 250, 70);
    switch (G.title_page) {
    case 0:
        draw_logo(22);
        rt_textc_sh("RELIGHT THE SEVEN BEACONS", 50, C_WHITE);
        if ((G.st / 25) % 2) rt_textc_sh("PRESS SPACE TO BEGIN", 160, C_YELLOW);
        rt_textc_sh("F2 CRT   M SOUND   ESC QUIT", 182, C_LGRAY);
        break;
    case 1: {
        rt_box(24, 20, 272, 162, C_BLUE, C_WHITE);
        rt_textc("HOW TO PLAY", 30, C_YELLOW);
        static const char *L[] = {
            "W S / UP DOWN    WALK", "A D / LEFT RIGHT TURN", "Q E              STEP SIDEWAYS",
            "SHIFT            RUN", "SPACE            JUMP", "MOUSE            LOOK (CLICK)",
            "PGUP PGDN        LOOK UP / DOWN", "TAB              MAP", "", "GATHER EMBERS. A BEACON TAKES 3.",
            "WALK INTO A BEACON TO LIGHT IT,", "THEN RETURN TO THE LIGHTHOUSE!",
        };
        for (int i = 0; i < 12; i++) rt_text(L[i], 36, 46 + i * 10, i >= 9 ? C_LCYAN : C_WHITE);
        break;
    }
    case 2:
        rt_box(56, 20, 208, 150, C_BLUE, C_WHITE);
        rt_textc("KEEPERS OF THE FLAME", 32, C_YELLOW);
        for (int i = 0; i < RT_NSCORES; i++) {
            char b[64];
            snprintf(b, sizeof(b), "%d. %-3.3s  %07d", i + 1, rt_scores[i].name, rt_scores[i].score);
            rt_textc(b, 52 + i * 13, i == G.rank ? C_LGREEN : C_WHITE);
        }
        break;
    }
}

static void draw_intro(void) {
    orbit_view(G.frames / 60.0f, 220, 60);
    rt_box(20, 40, 280, 116, C_BLUE, C_WHITE);
    static const char *L[] = {"THE KEEPER OF EMBER ISLE IS GONE,", "AND ITS SEVEN BEACONS ARE DARK.", "",
                              "EACH BEACON NEEDS 3 EMBERS TO BURN.", "LIGHT THEM ALL, THEN RETURN TO", "THE LIGHTHOUSE AND REKINDLE IT.", "",
                              "THE SUN IS ALREADY SINKING..."};
    for (int i = 0; i < 8; i++) rt_textc(L[i], 54 + i * 11, i == 7 ? C_YELLOW : C_WHITE);
    if ((G.st / 25) % 2 && G.st > 30) rt_textc("PRESS SPACE", 142, C_LCYAN);
}

static void draw_win(void) {
    orbit_view(G.frames / 60.0f, 190, 52);
    rt_box(40, 52, 240, 100, C_BLUE, C_WHITE);
    rt_textc_sh("THE ISLAND IS LIT!", 64, C_YELLOW);
    char b[64];
    int t = (int)G.time;
    snprintf(b, sizeof(b), "TIME        %02d:%02d", t / 60, t % 60);
    rt_textc(b, 84, C_WHITE);
    snprintf(b, sizeof(b), "EMBERS      %d / %d", G.embers_got, NEMBER);
    rt_textc(b, 96, C_WHITE);
    snprintf(b, sizeof(b), "SCORE       %07d", G.score);
    rt_textc(b, 112, C_LCYAN);
    if (G.st > 120 && (G.st / 25) % 2) rt_textc("PRESS SPACE", 134, C_WHITE);
}

static void draw_entry(void) {
    orbit_view(G.frames / 60.0f, 250, 70);
    rt_box(50, 40, 220, 120, C_BLUE, C_WHITE);
    rt_textc_sh("A NEW BEST!", 52, C_YELLOW);
    rt_textc("ENTER YOUR INITIALS", 74, C_LCYAN);
    for (int i = 0; i < 3; i++) {
        int x = 124 + i * 26;
        rt_rect(x, 116, 18, 2, C_LGRAY);
        char ch[2] = {0, 0};
        if (i < G.name_len) ch[0] = G.name[i];
        else if (i == G.name_len && (G.st / 15) % 2) ch[0] = (char)('A' + G.letter);
        if (ch[0]) rt_text_ex(ch, x + 1, 98, 2, i < G.name_len ? C_YELLOW : C_WHITE, NULL);
    }
    rt_textc("TYPE, THEN ENTER", 132, C_LGRAY);
}

static void draw_frame(void) {
    switch (G.state) {
    case S_TITLE: draw_title(); break;
    case S_INTRO: draw_intro(); break;
    case S_PLAY: {
        V3 eye = player_eye();
        render_world(eye, G.yaw, G.pitch, 60, 150, 40);
        draw_hud();
        break;
    }
    case S_WIN: draw_win(); break;
    case S_ENTRY: draw_entry(); break;
    }
}

/* ================================================================== flow */
static void new_game(void) {
    for (int b = 0; b < NBEACON; b++) { beacons[b].lit = false; beacons[b].lit_t = 0; }
    for (int e = 0; e < NEMBER; e++) embers[e].got = false;
    memset(sparks, 0, sizeof(sparks));
    G.beacons_lit = G.embers_got = G.carry = G.score = G.need_t = 0;
    G.all_lit = G.won = false;
    G.time = 0;
    G.tod = 0.12f;
    /* start on the beach below the village, facing inland */
    G.pos = village;
    float best = -1;
    for (int k = 0; k < 64; k++) {
        float a = k * 6.2832f / 64;
        for (float r = 10; r < 60; r += 2) {
            float x = village.x + sinf(a) * r, z = village.z + cosf(a) * r;
            float h = ground(x, z);
            if (h < 0.15f) {
                float px = village.x + sinf(a) * (r - 4), pz = village.z + cosf(a) * (r - 4);
                if (ground(px, pz) > 0.2f && (best < 0 || r < best)) { best = r; G.pos = v3(px, ground(px, pz), pz); }
                break;
            }
        }
    }
    G.yaw = atan2f(village.x - G.pos.x, village.z - G.pos.z);
    G.pitch = -0.05f;
    G.vy = 0;
    G.ground = true;
    G.swim = G.was_swim = false;
    G.map = G.paused = G.quitq = false;
    message("FIND EMBERS TO KINDLE THE BEACONS");
}

static void goto_title(int page) {
    G.state = S_TITLE;
    G.st = 0;
    G.title_page = page;
    G.tod = 0.36f;
    if (page == 0) tune_title();
    if (G.mouse) { SDL_SetRelativeMouseMode(SDL_FALSE); G.mouse = false; }
}

static void finish_game(void) {
    G.rank = rt_score_rank(G.score);
    if (G.rank >= 0) {
        G.state = S_ENTRY;
        G.st = 0;
        G.name_len = 0;
        G.letter = 0;
        memset(G.name, 0, sizeof(G.name));
    } else goto_title(2);
}

static void commit_entry(void) {
    if (G.name_len == 0) { strcpy(G.name, "???"); G.name_len = 3; }
    G.name[G.name_len] = 0;
    rt_score_insert(G.rank, G.name, G.score, G.beacons_lit);
    if (!selftest) rt_scores_save();
    goto_title(2);
}

static In read_input(void) {
    In in = {0};
    const Uint8 *k = SDL_GetKeyboardState(NULL);
    in.fwd = (k[SDL_SCANCODE_W] || k[SDL_SCANCODE_UP]) - (float)(k[SDL_SCANCODE_S] || k[SDL_SCANCODE_DOWN]);
    in.turn = (k[SDL_SCANCODE_D] || k[SDL_SCANCODE_RIGHT]) - (float)(k[SDL_SCANCODE_A] || k[SDL_SCANCODE_LEFT]);
    in.strafe = k[SDL_SCANCODE_E] - (float)k[SDL_SCANCODE_Q];
    in.look = k[SDL_SCANCODE_PAGEUP] - (float)k[SDL_SCANCODE_PAGEDOWN];
    in.jump = k[SDL_SCANCODE_SPACE];
    in.sprint = k[SDL_SCANCODE_LSHIFT] || k[SDL_SCANCODE_RSHIFT];
    if (rt_pad) {
        float lx = SDL_GameControllerGetAxis(rt_pad, SDL_CONTROLLER_AXIS_LEFTX) / 32767.0f;
        float ly = SDL_GameControllerGetAxis(rt_pad, SDL_CONTROLLER_AXIS_LEFTY) / 32767.0f;
        float rx = SDL_GameControllerGetAxis(rt_pad, SDL_CONTROLLER_AXIS_RIGHTX) / 32767.0f;
        float ry = SDL_GameControllerGetAxis(rt_pad, SDL_CONTROLLER_AXIS_RIGHTY) / 32767.0f;
        if (fabsf(ly) > 0.2f) in.fwd -= ly;
        if (fabsf(lx) > 0.2f) in.strafe += lx;
        if (fabsf(rx) > 0.2f) in.turn += rx;
        if (fabsf(ry) > 0.2f) in.look -= ry;
        in.jump |= SDL_GameControllerGetButton(rt_pad, SDL_CONTROLLER_BUTTON_A);
        in.sprint |= SDL_GameControllerGetButton(rt_pad, SDL_CONTROLLER_BUTTON_LEFTSTICK) ||
                     SDL_GameControllerGetButton(rt_pad, SDL_CONTROLLER_BUTTON_B) ||
                     SDL_GameControllerGetAxis(rt_pad, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 12000;
    }
    in.turn = clampf(in.turn, -1, 1);
    in.fwd = clampf(in.fwd, -1, 1);
    return in;
}

static void update(void) {
    const float dt = 1.0f / 60.0f;
    G.frames++;
    G.st++;
    switch (G.state) {
    case S_TITLE:
        G.tod = 0.36f + sinf(G.frames * 0.002f) * 0.03f;
        if (G.st > 540) { G.title_page = (G.title_page + 1) % 3; G.st = 0; }
        if (RT_HIT(r)) { G.title_page = (G.title_page + 1) % 3; G.st = 0; }
        if (RT_HIT(l)) { G.title_page = (G.title_page + 2) % 3; G.st = 0; }
        if (RT_HIT(ok)) { G.rank = -1; new_game(); G.state = S_INTRO; G.st = 0; G.tod = 0.12f; }
        break;
    case S_INTRO:
        if (G.st > 30 && RT_HIT(ok)) { G.state = S_PLAY; G.st = 0; G.frames = 0; }
        break;
    case S_PLAY: {
        if (RT_HIT(jet)) G.map = !G.map;
        if (G.paused || G.quitq) break;
        In in = read_input();
        step_player(&in, dt);
        step_world(dt);
        if (G.won && G.win_t > 6) { G.state = S_WIN; G.st = 0; if (G.mouse) { SDL_SetRelativeMouseMode(SDL_FALSE); G.mouse = false; } }
        break;
    }
    case S_WIN:
        step_world(dt);
        if (G.st > 120 && RT_HIT(ok)) finish_game();
        break;
    case S_ENTRY:
        if (RT_HIT(u)) G.letter = (G.letter + 1) % 26;
        if (RT_HIT(d)) G.letter = (G.letter + 25) % 26;
        if (rt_pad && SDL_GameControllerGetButton(rt_pad, SDL_CONTROLLER_BUTTON_A) && !rt_prev.jump && G.name_len < 3)
            G.name[G.name_len++] = (char)('A' + G.letter);
        if (rt_pad && SDL_GameControllerGetButton(rt_pad, SDL_CONTROLLER_BUTTON_START) && G.st > 10) commit_entry();
        break;
    }
}

static bool handle_key(SDL_Keycode k) {
    if (k == SDLK_m) { rt_mute = !rt_mute; return true; }
    switch (G.state) {
    case S_TITLE:
        if (k == SDLK_ESCAPE) return false;
        break;
    case S_PLAY:
        if (G.quitq) {
            if (k == SDLK_y) goto_title(0);
            else if (k == SDLK_n || k == SDLK_ESCAPE) G.quitq = false;
        } else if (k == SDLK_ESCAPE) {
            if (G.mouse) { SDL_SetRelativeMouseMode(SDL_FALSE); G.mouse = false; }
            else G.quitq = true;
        } else if (k == SDLK_p || k == SDLK_PAUSE) G.paused = !G.paused;
        else if (k == SDLK_TAB) G.map = !G.map;
        break;
    case S_ENTRY:
        if (k >= SDLK_a && k <= SDLK_z && G.name_len < 3) G.name[G.name_len++] = (char)('A' + (k - SDLK_a));
        else if (k == SDLK_BACKSPACE && G.name_len > 0) G.name[--G.name_len] = 0;
        else if ((k == SDLK_RETURN || k == SDLK_KP_ENTER) && G.st > 10) commit_entry();
        break;
    default:
        if (k == SDLK_ESCAPE) goto_title(0);
        break;
    }
    return true;
}

/* ================================================================== self-test */
static int run_selftest(const char *dir) {
    char path[1200];
    int fails = 0;
#define SHOT(name) do { draw_frame(); snprintf(path, sizeof(path), "%s/%s.bmp", dir, name); rt_shot(path); } while (0)
#define CHECK(cond, ...) do { if (!(cond)) { printf("FAIL: " __VA_ARGS__); printf("\n"); fails++; } } while (0)

    /* 1. the world */
    int land = 0;
    float hmax = -99;
    for (int j = 0; j < MAPN; j++)
        for (int i = 0; i < MAPN; i++) {
            if (H[j][i] > 0) land++;
            if (H[j][i] > hmax) hmax = H[j][i];
        }
    printf("terrain: %.0f%% land, highest point %.1f, %d objects, %d mesh triangles in a pine\n",
           100.0f * land / (MAPN * MAPN), hmax, ninst, MESH[M_PINE].nt);
    CHECK(land > MAPN * MAPN / 5 && land < MAPN * MAPN * 3 / 4, "land fraction out of range");
    for (int b = 0; b < NBEACON; b++) {
        CHECK(ground(beacons[b].x, beacons[b].z) > 0.8f, "beacon %d is not on land", b);
        for (int k = 0; k < b; k++)
            CHECK(hypotf(beacons[b].x - beacons[k].x, beacons[b].z - beacons[k].z) > 40, "beacons %d and %d too close", k, b);
    }
    CHECK(ground(lighthouse.x, lighthouse.z) > 1, "lighthouse is not on land");
    int bad_embers = 0;
    for (int e = 0; e < NEMBER; e++)
        if (ground(embers[e].x, embers[e].z) < 0.5f || embers[e].y == 0) bad_embers++;
    CHECK(bad_embers == 0, "%d embers misplaced", bad_embers);

    /* 2. a bot walks the island with the real movement code and lights everything */
    new_game();
    G.state = S_PLAY;
    int stuck = 0, unstick = 0;
    V3 last = G.pos;
    long f;
    for (f = 0; f < 60L * 60 * 15 && !G.won; f++) {
        float tx, tz, bd = 1e9f;
        if (G.all_lit) { tx = lighthouse.x; tz = lighthouse.z; }
        else if (G.carry < 3) {
            tx = tz = 0;
            for (int e = 0; e < NEMBER; e++)
                if (!embers[e].got) {
                    float d = hypotf(embers[e].x - G.pos.x, embers[e].z - G.pos.z);
                    if (d < bd) { bd = d; tx = embers[e].x; tz = embers[e].z; }
                }
        } else {
            tx = tz = 0;
            for (int b = 0; b < NBEACON; b++)
                if (!beacons[b].lit) {
                    float d = hypotf(beacons[b].x - G.pos.x, beacons[b].z - G.pos.z);
                    if (d < bd) { bd = d; tx = beacons[b].x; tz = beacons[b].z; }
                }
        }
        In in = {0};
        float want = atan2f(tx - G.pos.x, tz - G.pos.z), d = remainderf(want - G.yaw, 6.2832f);
        in.turn = clampf(d * 3, -1, 1);
        in.fwd = fabsf(d) < 1.2f ? 1 : 0.2f;
        in.sprint = true;
        if (unstick > 0) { unstick--; in.strafe = (f / 300) % 2 ? 1 : -1; in.jump = true; in.fwd = 0.6f; }
        G.frames++;
        step_player(&in, 1 / 60.0f);
        step_world(1 / 60.0f);
        if (f % 90 == 0) {
            if (hypotf(G.pos.x - last.x, G.pos.z - last.z) < 1.5f) { stuck++; unstick = 50; }
            last = G.pos;
        }
    }
    printf("bot: %d/%d beacons lit, %d embers on the way, lighthouse %s after %.1f min of game time (%d stuck moments)\n",
           G.beacons_lit, NBEACON, G.embers_got, G.won ? "REACHED" : "NOT reached", f / 3600.0, stuck);
    CHECK(G.won, "the bot could not finish the island");

    /* 3. renderer speed */
    Uint64 t0 = SDL_GetPerformanceCounter();
    tri_count = 0;
    int nfr = 0;
    for (int k = 0; k < 8; k++) {
        G.pos = v3(beacons[k % NBEACON].x - 10, ground(beacons[k % NBEACON].x - 10, beacons[k % NBEACON].z), beacons[k % NBEACON].z);
        for (int r = 0; r < 12; r++, nfr++) render_world(player_eye(), r * 0.52f, -0.05f, 60, 150, 40);
    }
    double ms = (double)(SDL_GetPerformanceCounter() - t0) * 1000.0 / SDL_GetPerformanceFrequency() / nfr;
    printf("renderer: %.2f ms per frame, %ld triangles per frame on average\n", ms, tri_count / nfr);
    CHECK(ms < 16.7, "renderer too slow for 60 fps");

    /* 4. screenshots */
    goto_title(0);
    for (int p = 0; p < 3; p++) {
        char n[16];
        G.title_page = p;
        G.st = 30;
        G.frames = 600 + p * 400;
        snprintf(n, sizeof(n), "title%d", p);
        SHOT(n);
    }
    new_game();
    G.state = S_INTRO;
    G.st = 50;
    SHOT("intro");
    G.state = S_PLAY;
    G.frames = 100;
    SHOT("play_start");
    struct { float tod; int beacon; float back, yaw_off, pitch; const char *name; } views[] = {
        {0.15f, 0, 14, 0, -0.08f, "play_day"}, {0.33f, 3, 18, 0.3f, 0.0f, "play_golden"},
        {0.40f, 5, 16, -0.4f, 0.05f, "play_sunset"}, {0.60f, 2, 20, 0.2f, 0.08f, "play_night"},
    };
    for (int v = 0; v < 4; v++) {
        for (int b = 0; b < NBEACON; b++) beacons[b].lit = v >= 2 || b < 2;
        G.all_lit = v >= 2;
        Beacon *k = &beacons[views[v].beacon];
        float a = views[v].yaw_off;
        G.pos = v3(k->x - sinf(a) * views[v].back, 0, k->z - cosf(a) * views[v].back);
        G.pos.y = fmaxf(ground(G.pos.x, G.pos.z), -1.1f);
        G.swim = ground(G.pos.x, G.pos.z) < -1.1f;
        G.yaw = a;
        G.pitch = views[v].pitch;
        G.tod = views[v].tod;
        for (int i = 0; i < 120; i++) { G.frames++; step_world(0); }
        G.msg_t = 0;
        SHOT(views[v].name);
    }
    G.map = true;
    SHOT("map");
    G.map = false;
    G.pos = v3(WORLD / 2, -1.1f, -10);
    G.swim = true;
    G.yaw = 0;
    G.pitch = 0.05f;
    G.tod = 0.2f;
    SHOT("swimming");
    G.won = true;
    G.win_t = 3;
    G.tod = 0.6f;
    G.state = S_WIN;
    G.st = 130;
    SHOT("win");
    G.state = S_ENTRY;
    G.name_len = 2;
    strcpy(G.name, "EI");
    G.st = 15;
    SHOT("entry");
    printf("selftest: %s\n", fails ? "FAILED" : "all checks passed");
    return fails ? 1 : 0;
}

/* ================================================================== main */
static const RtScore DEFAULT_SCORES[RT_NSCORES] = {
    {"KPR", 16000, 7}, {"GUL", 13000, 7}, {"EWE", 10000, 7}, {"SUN", 8000, 6},
    {"OAK", 6000, 5},  {"FOG", 4000, 4},  {"SEA", 2500, 3},  {"ASH", 1000, 1},
};

int main(int argc, char **argv) {
    const char *shotdir = NULL;
    for (int i = 1; i < argc; i++)
        if (!strcmp(argv[i], "--selftest") && i + 1 < argc) shotdir = argv[++i];
    selftest = shotdir != NULL;
    if (!rt_init("Ember Isle")) return 1;
    fbtex = SDL_CreateTexture(rt_ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, RT_W, RT_H);
    if (!fbtex) { fprintf(stderr, "texture: %s\n", SDL_GetError()); return 1; }
    SDL_SetTextureBlendMode(fbtex, SDL_BLENDMODE_NONE);
    gen_terrain();
    build_meshes();
    populate();
    build_map();
    memcpy(rt_scores, DEFAULT_SCORES, sizeof(rt_scores));
    G.rank = -1;
    if (selftest) return run_selftest(shotdir);

    rt_audio_init();
    rt_scores_load("ember-isle", DEFAULT_SCORES);
    for (int i = 0; i < SDL_NumJoysticks() && !rt_pad; i++)
        if (SDL_IsGameController(i)) rt_pad = SDL_GameControllerOpen(i);
    goto_title(0);

    Uint64 freq = SDL_GetPerformanceFrequency(), last = SDL_GetPerformanceCounter();
    double acc = 0, dt = 1.0 / 60.0;
    bool running = true;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = false;
            else if (rt_event(&e)) {}
            else if (e.type == SDL_KEYDOWN && !e.key.repeat) {
                if (!handle_key(e.key.keysym.sym)) running = false;
            } else if (e.type == SDL_MOUSEBUTTONDOWN && G.state == S_PLAY && !G.mouse && !G.quitq) {
                G.mouse = SDL_SetRelativeMouseMode(SDL_TRUE) == 0;
            } else if (e.type == SDL_MOUSEMOTION && G.mouse) {
                G.mdx += e.motion.xrel;
                G.mdy += e.motion.yrel;
            } else if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_FOCUS_LOST && G.state == S_PLAY) {
                G.paused = true;
                if (G.mouse) { SDL_SetRelativeMouseMode(SDL_FALSE); G.mouse = false; }
            }
        }
        Uint64 now = SDL_GetPerformanceCounter();
        acc += (double)(now - last) / (double)freq;
        last = now;
        if (acc > 0.25) acc = 0.25;
        while (acc >= dt) {
            rt_poll();
            update();
            acc -= dt;
        }
        draw_frame();
        rt_present();
    }
    if (rt_adev) SDL_CloseAudioDevice(rt_adev);
    SDL_Quit();
    return 0;
}
