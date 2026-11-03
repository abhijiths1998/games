/* world.c - the island city of Port Calder: terrain, districts, buildings, collision, road graph. */

#define NB 24
#define PITCH 64.0f
#define CITY (NB * PITCH)
#define WATER_Y (-0.8f)
#define STEP 0.6f
#define LANE_OFF 2.2f
#define WALK 7.0f
#define WMIN (-480.0f)
#define WMAX (CITY + 480.0f)
#define DOCK_X 1150.0f

enum { LOT_PLAZA, LOT_GRASS, LOT_ASPH, LOT_DIRT, LOT_PARK, LOT_SAND };
enum { D_CENTER, D_MIDTOWN, D_MAPLE, D_PARK, D_DOCKS, D_SEABRIGHT, D_EASTGATE, D_OLDTOWN, D_BEACH, D_SEA, D_N };
static const char *DNAME[D_N] = {"CALDER CENTER", "MIDTOWN", "MAPLE HEIGHTS", "CALDER PARK", "IRONSIDE DOCKS",
                                 "SEABRIGHT", "EASTGATE", "OLD QUARTER", "SUNSET STRAND", "OPEN WATER"};
static const v3 DCOL[D_N] = {{0.30f, 0.33f, 0.40f}, {0.36f, 0.34f, 0.32f}, {0.22f, 0.32f, 0.20f}, {0.16f, 0.36f, 0.14f},
                             {0.34f, 0.28f, 0.20f}, {0.40f, 0.34f, 0.30f}, {0.32f, 0.30f, 0.27f}, {0.38f, 0.26f, 0.22f},
                             {0.62f, 0.55f, 0.38f}, {0.05f, 0.12f, 0.20f}};
static unsigned char lot_type[NB][NB], dist_blk[NB][NB];
static unsigned char nflag[NB + 1][NB + 1]; /* bit0: road to +x neighbour, bit1: road to +z neighbour */

/* ---- collision boxes with a uniform grid index */
typedef struct { v3 mn, mx; int ramp; } Box; /* ramp: 0 none, 1 top rises toward -z, 2 toward +z, 3 toward -x, 4 toward +x */
static Box *boxes;
static int nbox, capbox;
#define GC 32.0f
#define GN 62
typedef struct { int *idx; int n, cap; } Cell;
static Cell grid[GN][GN];
static int gcoord(float v) { int c = (int)floorf((v - WMIN) / GC); return c < 0 ? 0 : c >= GN ? GN - 1 : c; }
static int add_box_r(v3 mn, v3 mx, int ramp) {
    if (nbox == capbox) { capbox = capbox ? capbox * 2 : 4096; boxes = realloc(boxes, sizeof(Box) * (size_t)capbox); }
    boxes[nbox] = (Box){mn, mx, ramp};
    for (int gx = gcoord(mn.x); gx <= gcoord(mx.x); gx++)
        for (int gz = gcoord(mn.z); gz <= gcoord(mx.z); gz++) {
            Cell *c = &grid[gx][gz];
            if (c->n == c->cap) { c->cap = c->cap ? c->cap * 2 : 16; c->idx = realloc(c->idx, sizeof(int) * (size_t)c->cap); }
            c->idx[c->n++] = nbox;
        }
    return nbox++;
}
static int add_box(v3 mn, v3 mx) { return add_box_r(mn, mx, 0); }

/* gather the boxes near a point (dedupe via stamp) */
static int qstamp, *qmark, qcap;
static int qlist[2048], qn;
static void query_boxes(float x0, float z0, float x1, float z1) {
    if (qcap < nbox) { qcap = nbox + 1024; qmark = realloc(qmark, sizeof(int) * (size_t)qcap); memset(qmark, 0, sizeof(int) * (size_t)qcap); qstamp = 0; }
    qstamp++;
    qn = 0;
    for (int gx = gcoord(x0); gx <= gcoord(x1); gx++)
        for (int gz = gcoord(z0); gz <= gcoord(z1); gz++) {
            Cell *c = &grid[gx][gz];
            for (int k = 0; k < c->n; k++) {
                int b = c->idx[k];
                if (qmark[b] == qstamp) continue;
                qmark[b] = qstamp;
                const Box *B = &boxes[b];
                if (B->mx.x < x0 || B->mn.x > x1 || B->mx.z < z0 || B->mn.z > z1) continue;
                if (qn < 2048) qlist[qn++] = b;
            }
        }
}

/* ---- terrain */
static float out_dist(float x, float z) {
    float dx = x < 0 ? -x : x > CITY ? x - CITY : 0, dz = z < 0 ? -z : z > CITY ? z - CITY : 0;
    return sqrtf(dx * dx + dz * dz);
}
static bool is_dock_zone(float x, float z) { return z > CITY && x > -20 && x < DOCK_X; }
static float terrain_h(float x, float z) {
    float d = out_dist(x, z);
    if (d <= 0) return 0;
    if (is_dock_zone(x, z)) return z < CITY + 52 ? 0 : -9;
    if (x > CITY && z > -40 && z < CITY + 40) { /* the long east beach */
        float s = 92 + 6 * sinf(z * 0.013f);
        if (d < s) return 0;
        float t = d - s;
        return -t * 0.022f - (t > 50 ? (t - 50) * 0.05f : 0);
    }
    float a = atan2f(z - CITY / 2, x - CITY / 2);
    float s = 58 + 18 * sinf(a * 5 + 1) + 9 * sinf(a * 11 + 2);
    if (d < s) return 0;
    float t = d - s;
    return -t * 0.05f - (t > 25 ? (t - 25) * 0.08f : 0);
}
static bool water_at(float x, float z) { return terrain_h(x, z) < WATER_Y - 0.25f; }

/* ground mesh: a height-mapped grid (flat inside the city, beaches and sea floor outside) */
static Mesh mk_ground(void) {
    FBuf b = {0};
    const int N = 300;
    float step = (WMAX - WMIN) / N;
    for (int i = 0; i < N; i++)
        for (int j = 0; j < N; j++) {
            float x0 = WMIN + i * step, z0 = WMIN + j * step, x1 = x0 + step, z1 = z0 + step;
            v3 a = V(x0, terrain_h(x0, z0), z0), c = V(x1, terrain_h(x1, z0), z0), d = V(x1, terrain_h(x1, z1), z1), e = V(x0, terrain_h(x0, z1), z1);
            fb_tri(&b, a, e, d);
            fb_tri(&b, a, d, c);
        }
    return mesh_upload(&b);
}
static Mesh mk_ocean(void) {
    FBuf b = {0};
    float r = 9000;
    fb_tri(&b, V(-r, 0, -r), V(-r, 0, r), V(r, 0, r));
    fb_tri(&b, V(-r, 0, -r), V(r, 0, r), V(r, 0, -r));
    return mesh_upload(&b);
}

/* ---- roads */
static bool edge_ok(int i, int j, int dir) { /* dir 0 +x, 1 +z, 2 -x, 3 -z */
    if (i < 0 || j < 0 || i > NB || j > NB) return false;
    switch (dir) {
    case 0: return (nflag[i][j] & 1) != 0;
    case 1: return (nflag[i][j] & 2) != 0;
    case 2: return i > 0 && (nflag[i - 1][j] & 1) != 0;
    default: return j > 0 && (nflag[i][j - 1] & 2) != 0;
    }
}
static const int DX[4] = {1, 0, -1, 0}, DZ[4] = {0, 1, 0, -1};
static v3 dir_vec(int d) { return V((float)DX[d], 0, (float)DZ[d]); }
static v3 node_pos(int i, int j) { return V(i * PITCH, 0, j * PITCH); }
static bool node_any(int i, int j) { for (int d = 0; d < 4; d++) if (edge_ok(i, j, d)) return true; return false; }
static int node_degree(int i, int j) { int n = 0; for (int d = 0; d < 4; d++) n += edge_ok(i, j, d); return n; }
static void nearest_node(v3 p, int *i, int *j) {
    *i = (int)clampf(floorf(p.x / PITCH + 0.5f), 0, NB);
    *j = (int)clampf(floorf(p.z / PITCH + 0.5f), 0, NB);
}
/* nearest node that actually has roads */
static void nearest_road_node(v3 p, int *bi, int *bj) {
    int ci, cj;
    nearest_node(p, &ci, &cj);
    float best = 1e9f;
    *bi = ci; *bj = cj;
    for (int r = 0; r <= 4; r++)
        for (int i = ci - r; i <= ci + r; i++)
            for (int j = cj - r; j <= cj + r; j++) {
                if (i < 0 || j < 0 || i > NB || j > NB || !node_any(i, j)) continue;
                float d = dist2d(node_pos(i, j), p);
                if (d < best) { best = d; *bi = i; *bj = j; }
            }
}
static bool on_road(float x, float z, float margin) {
    if (x < -5 - margin || z < -5 - margin || x > CITY + 5 + margin || z > CITY + 5 + margin) return false;
    int ni = (int)floorf(x / PITCH + 0.5f), nj = (int)floorf(z / PITCH + 0.5f);
    float ox = fabsf(x - ni * PITCH), oz = fabsf(z - nj * PITCH);
    int ci = (int)floorf(x / PITCH), cj = (int)floorf(z / PITCH);
    if (ox < 5 + margin && edge_ok(ni, cj, 1)) return true;
    if (oz < 5 + margin && edge_ok(ci, nj, 0)) return true;
    if (ox < 5 + margin && oz < 5 + margin && ni >= 0 && nj >= 0 && ni <= NB && nj <= NB && node_any(ni, nj)) return true;
    return false;
}

static int district_block(int i, int j) {
    if (i >= 3 && i <= 6 && j >= 4 && j <= 7) return D_PARK;
    if (j >= 20 && i <= 15) return D_DOCKS;
    if (i >= 20) return D_SEABRIGHT;
    if (abs(i - 12) <= 3 && abs(j - 11) <= 3) return D_CENTER;
    if (i <= 8 && j >= 12) return D_OLDTOWN;
    if (i <= 8) return D_MAPLE;
    if (i >= 16 && j >= 15) return D_EASTGATE;
    return D_MIDTOWN;
}
static int district_at(v3 p) {
    if (p.x >= 0 && p.z >= 0 && p.x < CITY && p.z < CITY) return dist_blk[(int)(p.x / PITCH)][(int)(p.z / PITCH)];
    if (water_at(p.x, p.z)) return D_SEA;
    if (is_dock_zone(p.x, p.z)) return D_DOCKS;
    return D_BEACH;
}

/* ---- static scenery helpers */
static const Mat M_TRUNK = {{0.20f, 0.13f, 0.08f}, PAT_LEAF, 0, 0.9f, {0, 0, 0}, 0};
static const Mat M_CONCRETE = {{0.42f, 0.41f, 0.39f}, PAT_PLAIN, 0, 0.85f, {0, 0, 0}, 0};
static const Mat M_DARKMETAL = {{0.10f, 0.10f, 0.11f}, PAT_PLAIN, 0.8f, 0.4f, {0, 0, 0}, 0};
static const Mat M_STEEL = {{0.55f, 0.56f, 0.58f}, PAT_PLAIN, 1.0f, 0.3f, {0, 0, 0}, 0};
static const Mat M_WHITE = {{0.80f, 0.80f, 0.78f}, PAT_PLAIN, 0, 0.6f, {0, 0, 0}, 0};
static const Mat M_WOOD = {{0.36f, 0.24f, 0.14f}, PAT_PLANK, 0, 0.8f, {0, 0, 0}, 0};
static const Mat M_POND = {{0.02f, 0.06f, 0.07f}, PAT_POND, 0, 0.05f, {0, 0, 0}, 0};
static Mat mat(v3 base, int pat, float metal, float rough) { Mat m = {base, pat, metal, rough, {0, 0, 0}, 0}; return m; }
static Mat glow(v3 base, v3 emis, int pat, float night) { Mat m = {base, pat, 0, 0.4f, emis, night}; return m; }

static void sbox(float x0, float y0, float z0, float x1, float y1, float z1, const Mat *m, float seed, bool solid) {
    inst_s(MS_CUBE, m4_box(V((x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2), V(x1 - x0, y1 - y0, z1 - z0)), m, seed);
    if (solid) add_box(V(x0, y0, z0), V(x1, y1, z1));
}
static void building(float x0, float z0, float x1, float z1, float h, const Mat *m, float seed) {
    sbox(x0, 0, z0, x1, h, z1, m, seed, true);
}
static void tree(float x, float z, float s, int kind, unsigned seed) {
    float hv = 0.85f + (seed % 100) / 300.0f;
    s *= hv;
    v3 leaf = kind == 1 ? V(0.05f, 0.14f, 0.07f) : kind == 2 ? V(0.12f, 0.24f, 0.06f) : V(0.08f + (seed % 7) * 0.012f, 0.20f + (seed % 5) * 0.015f, 0.05f);
    Mat ml = mat(leaf, PAT_LEAF, 0, 0.9f);
    if (kind == 0) {
        inst_s(MS_CYL8, m4_trs(V(x, 0, z), 0, 0, 0, V(0.45f * s, 3.2f * s, 0.45f * s)), &M_TRUNK, 0);
        inst_s(MS_BLOB, m4_trs(V(x, 4.4f * s, z), (seed % 13) * 0.4f, 0, 0, V(2.6f * s, 2.2f * s, 2.6f * s)), &ml, 0);
        inst_s(MS_BLOB, m4_trs(V(x + 0.8f * s, 5.6f * s, z - 0.4f * s), 0.7f, 0, 0, V(1.8f * s, 1.6f * s, 1.8f * s)), &ml, 0);
    } else if (kind == 1) {
        inst_s(MS_CYL8, m4_trs(V(x, 0, z), 0, 0, 0, V(0.4f * s, 2.0f * s, 0.4f * s)), &M_TRUNK, 0);
        inst_s(MS_CONE, m4_trs(V(x, 1.5f * s, z), 0, 0, 0, V(3.6f * s, 4.5f * s, 3.6f * s)), &ml, 0);
        inst_s(MS_CONE, m4_trs(V(x, 4.0f * s, z), 0.5f, 0, 0, V(2.6f * s, 3.8f * s, 2.6f * s)), &ml, 0);
    } else { /* palm */
        float lean = ((seed >> 3) % 10) * 0.025f, ly = (seed % 628) * 0.01f;
        float H = 7.5f * s;
        v3 top = V(x + sinf(ly) * lean * H, H, z - cosf(ly) * lean * H);
        inst_s(MS_CYL8, m4_trs(V(x, 0, z), ly, lean, 0, V(0.35f * s, H, 0.35f * s)), &M_TRUNK, 0);
        for (int k = 0; k < 7; k++) {
            float a = k * 0.8976f + ly;
            v3 d = fwd_of(a);
            inst_s(MS_CUBE, m4_trs(add(top, add(mul(d, 1.6f * s), V(0, -0.35f * s, 0))), a, -0.42f, 0, V(0.9f * s, 0.08f, 3.4f * s)), &ml, 0);
        }
    }
    add_box(V(x - 0.35f, 0, z - 0.35f), V(x + 0.35f, 4, z + 0.35f));
}

/* parked-car slots, street lamps, points of interest */
typedef struct { v3 pos; float yaw; } Slot;
#define MAXSLOT 1200
static Slot slots[MAXSLOT];
static int nslot;
static void add_slot(float x, float z, float yaw) { if (nslot < MAXSLOT) slots[nslot++] = (Slot){V(x, 0, z), yaw}; }

typedef struct { v3 pos, col; float radius; } SLight;
#define MAXSL 900
static SLight slights[MAXSL];
static int nslight;
static void add_slight(v3 p, v3 c, float r) { if (nslight < MAXSL) slights[nslight++] = (SLight){p, c, r}; }

enum { POI_SAFE, POI_POLICE, POI_HOSPITAL, POI_RESPRAY, POI_JOB, POI_RACE, POI_N };
typedef struct { int type; v3 pos; float yaw; int i, j; } Poi;
#define MAXPOI 40
static Poi poi[MAXPOI];
static int npoi;
static v3 block_front(int i, int j) { return V(i * PITCH + PITCH / 2, 0, j * PITCH + WALK); }
static void add_poi(int type, int i, int j) { if (npoi < MAXPOI) poi[npoi++] = (Poi){type, block_front(i, j), 0, i, j}; }
static int poi_block(int i, int j) { for (int k = 0; k < npoi; k++) if (poi[k].i == i && poi[k].j == j && poi[k].type != POI_RACE) return poi[k].type; return -1; }

/* stunt ramps */
typedef struct { v3 pos; float yaw; float len, h; } Ramp;
#define MAXRAMP 16
static Ramp ramps[MAXRAMP];
static int nramp;
/* ramp rising along +fwd(yaw) for axis-aligned yaws only (0, pi/2, pi, -pi/2) */
static void add_ramp(v3 base, float yaw, float w, float l, float h) {
    if (nramp < MAXRAMP) ramps[nramp++] = (Ramp){base, yaw, l, h};
    v3 f = fwd_of(yaw);
    v3 c = add(base, mul(f, l / 2));
    Mat m = mat(V(0.55f, 0.45f, 0.12f), PAT_RIBS, 0.5f, 0.5f);
    inst_s(MS_WEDGE, m4_trs(c, yaw, 0, 0, V(w, h, l)), &m, 0);
    int dir = fabsf(f.z) > 0.5f ? (f.z < 0 ? 1 : 2) : (f.x < 0 ? 3 : 4);
    float hw = fabsf(f.z) > 0.5f ? w / 2 : l / 2, hl = fabsf(f.z) > 0.5f ? l / 2 : w / 2;
    add_box_r(V(c.x - hw, 0, c.z - hl), V(c.x + hw, h, c.z + hl), dir);
}

static void neon(float x0, float y0, float z0, float x1, float y1, float z1, v3 col, float seed) {
    Mat m = glow(mul(col, 0.15f), mul(col, 3.0f), PAT_NEON, 0.85f);
    sbox(x0, y0, z0, x1, y1, z1, &m, seed, false);
    add_slight(V((x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2), mul(col, 1.6f), 14);
}
static const v3 NEONS[] = {{1.0f, 0.15f, 0.55f}, {0.1f, 0.85f, 1.0f}, {1.0f, 0.75f, 0.1f}, {0.4f, 1.0f, 0.3f}, {0.7f, 0.3f, 1.0f}, {1.0f, 0.35f, 0.1f}};
static const v3 TOWER_FRAME[] = {{0.55f, 0.57f, 0.60f}, {0.12f, 0.13f, 0.15f}, {0.42f, 0.32f, 0.22f}, {0.75f, 0.75f, 0.72f}, {0.20f, 0.42f, 0.48f}, {0.30f, 0.32f, 0.38f}};
static const v3 WALLS[] = {{0.42f, 0.18f, 0.12f}, {0.62f, 0.55f, 0.42f}, {0.45f, 0.45f, 0.46f}, {0.70f, 0.66f, 0.55f}, {0.30f, 0.14f, 0.10f}, {0.30f, 0.36f, 0.42f}, {0.55f, 0.42f, 0.32f}};
static const v3 PASTEL[] = {{0.85f, 0.55f, 0.55f}, {0.55f, 0.80f, 0.65f}, {0.90f, 0.80f, 0.45f}, {0.55f, 0.70f, 0.85f}, {0.85f, 0.85f, 0.82f}, {0.85f, 0.65f, 0.45f}};
static const v3 HOUSEC[] = {{0.80f, 0.78f, 0.72f}, {0.60f, 0.50f, 0.38f}, {0.55f, 0.62f, 0.66f}, {0.72f, 0.66f, 0.50f}, {0.50f, 0.30f, 0.22f}, {0.68f, 0.72f, 0.62f}};
static const v3 ROOFC[] = {{0.20f, 0.20f, 0.22f}, {0.45f, 0.16f, 0.10f}, {0.30f, 0.22f, 0.15f}, {0.18f, 0.26f, 0.22f}};
#define PICK(arr, h) arr[(int)((h) * (sizeof(arr) / sizeof(arr[0]))) % (int)(sizeof(arr) / sizeof(arr[0]))]

static void rooftop_bits(float x0, float z0, float x1, float z1, float h, float r) {
    float cx = (x0 + x1) / 2, cz = (z0 + z1) / 2, w = x1 - x0, d = z1 - z0;
    sbox(cx - w * 0.2f, h, cz - d * 0.15f, cx + w * 0.05f, h + 1.6f, cz + d * 0.1f, &M_CONCRETE, 0, false);
    if (r > 0.5f) {
        Mat t = mat(V(0.35f, 0.22f, 0.14f), PAT_PLANK, 0, 0.8f);
        inst_s(MS_CYL8, m4_trs(V(cx + w * 0.25f, h + 1.5f, cz + d * 0.2f), 0, 0, 0, V(3, 3, 3)), &t, 0);
        inst_s(MS_CONE, m4_trs(V(cx + w * 0.25f, h + 4.5f, cz + d * 0.2f), 0, 0, 0, V(3.2f, 1.2f, 3.2f)), &t, 0);
        for (int k = 0; k < 4; k++)
            inst_s(MS_CUBE, m4_box(V(cx + w * 0.25f + (k & 1 ? 1.1f : -1.1f), h + 0.75f, cz + d * 0.2f + (k & 2 ? 1.1f : -1.1f)), V(0.15f, 1.5f, 0.15f)), &M_DARKMETAL, 0);
    }
}

static void tower(float x0, float z0, float x1, float z1, float h, unsigned s) {
    float r1 = hash2f((int)x0, (int)z0, 7), r2 = hash2f((int)x0, (int)z0, 8);
    Mat podium = mat(PICK(WALLS, r1), PAT_MIDRISE, 0, 0.8f);
    Mat glass = mat(PICK(TOWER_FRAME, r2), PAT_TOWER, 0.6f, 0.3f);
    float ph = 8.5f;
    building(x0, z0, x1, z1, ph, &podium, r1);
    float in = 3 + r1 * 3;
    float tx0 = x0 + in, tz0 = z0 + in, tx1 = x1 - in, tz1 = z1 - in;
    int tiers = 1 + (int)(r2 * 3);
    float y = ph, seg = (h - ph) / tiers;
    for (int t = 0; t < tiers; t++) {
        float top = y + seg;
        sbox(tx0, y, tz0, tx1, top, tz1, &glass, r2 + t * 0.1f, true);
        y = top;
        float k = (tx1 - tx0) * 0.12f;
        if (tx1 - tx0 > 14) { tx0 += k; tx1 -= k; tz0 += k; tz1 -= k; }
    }
    float cx = (tx0 + tx1) / 2, cz = (tz0 + tz1) / 2;
    if (s % 3 == 0) {
        inst_s(MS_CYL8, m4_trs(V(cx, y, cz), 0, 0, 0, V(0.6f, 14, 0.6f)), &M_STEEL, 0);
        Mat red = glow(V(0.3f, 0, 0), V(6, 0.2f, 0.1f), PAT_NEON, 1);
        inst_s(MS_SPHERE, m4_trs(V(cx, y + 14.3f, cz), 0, 0, 0, V(0.5f, 0.5f, 0.5f)), &red, r1);
    } else rooftop_bits(tx0, tz0, tx1, tz1, y, 0.2f);
    /* storefront neon */
    v3 nc = NEONS[s % 6];
    if (s % 2 == 0) neon(x0 + 6, 5.2f, z0 - 0.35f, x1 - 6, 6.0f, z0 - 0.05f, nc, r2);
    else neon(x0 - 0.35f, 5.2f, z0 + 6, x0 - 0.05f, 6.0f, z1 - 6, nc, r2);
}

static void midrise(float x0, float z0, float x1, float z1, float h, unsigned s, bool old) {
    float r = hash2f((int)(x0 * 3), (int)(z0 * 7), 3);
    Mat m = mat(PICK(WALLS, r), PAT_MIDRISE, 0, 0.85f);
    building(x0, z0, x1, z1, h, &m, r);
    sbox(x0 - 0.2f, h, z0 - 0.2f, x1 + 0.2f, h + 0.6f, z1 + 0.2f, &M_CONCRETE, 0, false);
    rooftop_bits(x0, z0, x1, z1, h + 0.6f, old ? 0.8f : r);
    if (s % 4 == 1) neon(x0 + 3, 4.6f, z0 - 0.3f, x1 - 3, 5.1f, z0 - 0.05f, NEONS[(s / 4) % 6], r);
    if (old && s % 3 == 0) { /* awning */
        Mat aw = mat(NEONS[s % 6], PAT_PLAIN, 0, 0.7f);
        aw.base = mul(aw.base, 0.5f);
        inst_s(MS_WEDGE, m4_trs(V((x0 + x1) / 2, 3.6f, z0 - 0.8f), 3.14159f, 0, 0, V(x1 - x0 - 4, 0.6f, 1.6f)), &aw, 0);
    }
}

static void house(float cx, float cz, float w, float d, int floors, v3 wall, v3 roof, unsigned s, bool flat) {
    float h = floors * 2.9f + 0.3f;
    Mat m = mat(wall, PAT_HOUSE, 0, 0.9f);
    building(cx - w / 2, cz - d / 2, cx + w / 2, cz + d / 2, h, &m, (s % 1000) / 1000.0f);
    if (flat) {
        sbox(cx - w / 2 - 0.15f, h, cz - d / 2 - 0.15f, cx + w / 2 + 0.15f, h + 0.4f, cz + d / 2 + 0.15f, &M_WHITE, 0, false);
    } else {
        Mat rm = mat(roof, PAT_ROOF, 0, 0.8f);
        if (s % 2) inst_s(MS_GABLE, m4_trs(V(cx, h, cz), (s % 4 < 2) ? 0 : 1.5708f, 0, 0, (s % 4 < 2) ? V(w + 0.8f, 2.4f, d + 0.8f) : V(d + 0.8f, 2.4f, w + 0.8f)), &rm, 0);
        else inst_s(MS_PYR, m4_trs(V(cx, h, cz), 0, 0, 0, V(w + 0.8f, 2.6f, d + 0.8f)), &rm, 0);
    }
    /* door + porch light */
    Mat door = mat(V(0.25f, 0.14f, 0.08f), PAT_PLAIN, 0, 0.6f);
    sbox(cx - 0.6f, 0, cz - d / 2 - 0.08f, cx + 0.6f, 2.1f, cz - d / 2, &door, 0, false);
    Mat lamp = glow(V(0.4f, 0.35f, 0.2f), V(4, 2.6f, 1.2f), PAT_PLAIN, 1);
    sbox(cx + 0.9f, 2.2f, cz - d / 2 - 0.2f, cx + 1.2f, 2.5f, cz - d / 2, &lamp, 0, false);
}

static void warehouse(float x0, float z0, float x1, float z1, float h, unsigned s) {
    Mat m = mat(s % 2 ? V(0.42f, 0.44f, 0.46f) : V(0.48f, 0.30f, 0.22f), PAT_RIBS, 0.5f, 0.55f);
    building(x0, z0, x1, z1, h, &m, 0);
    Mat roof = mat(V(0.3f, 0.3f, 0.32f), PAT_RIBS, 0.6f, 0.5f);
    inst_s(MS_GABLE, m4_trs(V((x0 + x1) / 2, h, (z0 + z1) / 2), 1.5708f, 0, 0, V(z1 - z0 + 0.4f, 2.0f, x1 - x0 + 0.4f)), &roof, 0);
    Mat door = mat(V(0.15f, 0.16f, 0.18f), PAT_RIBS, 0.6f, 0.5f);
    sbox((x0 + x1) / 2 - 4, 0, z0 - 0.1f, (x0 + x1) / 2 + 4, 5.5f, z0, &door, 0, false);
    Mat lamp = glow(V(0.4f, 0.4f, 0.3f), V(3.5f, 3.0f, 2.0f), PAT_PLAIN, 1);
    sbox((x0 + x1) / 2 - 0.4f, 6.2f, z0 - 0.5f, (x0 + x1) / 2 + 0.4f, 6.5f, z0, &lamp, 0, false);
}
static const v3 CONTC[] = {{0.55f, 0.12f, 0.08f}, {0.08f, 0.25f, 0.50f}, {0.12f, 0.40f, 0.20f}, {0.75f, 0.45f, 0.08f}, {0.50f, 0.50f, 0.52f}, {0.10f, 0.45f, 0.48f}};
static void containers(float x0, float z0, float x1, float z1, unsigned s) {
    for (float x = x0; x + 2.6f <= x1; x += 3.2f)
        for (float z = z0; z + 12.2f <= z1; z += 14) {
            int stack = 1 + (int)(hash2f((int)x, (int)z, (int)s) * 3.2f);
            for (int k = 0; k < stack; k++) {
                Mat m = mat(PICK(CONTC, hash2f((int)x, (int)z, k + 40)), PAT_RIBS, 0.6f, 0.5f);
                sbox(x, k * 2.6f, z, x + 2.5f, k * 2.6f + 2.55f, z + 12.2f, &m, 0, k == 0);
            }
        }
}
static void crane(float x, float z, float yaw) {
    Mat y = mat(V(0.75f, 0.55f, 0.08f), PAT_PLAIN, 0.6f, 0.45f);
    for (int k = 0; k < 4; k++) {
        float lx = x + (k & 1 ? 5 : -5), lz = z + (k & 2 ? 5 : -5);
        sbox(lx - 0.5f, 0, lz - 0.5f, lx + 0.5f, 28, lz + 0.5f, &y, 0, true);
    }
    sbox(x - 6, 28, z - 6, x + 6, 32, z + 6, &y, 0, false);
    v3 f = fwd_of(yaw);
    inst_s(MS_CUBE, m4_trs(add(V(x, 33, z), mul(f, 22)), yaw, 0, 0, V(2.4f, 2.4f, 70)), &y, 0);
    sbox(x - 3, 32, z - 3, x + 3, 37, z + 3, &M_WHITE, 0, false);
    Mat red = glow(V(0.3f, 0, 0), V(6, 0.2f, 0.1f), PAT_NEON, 1);
    inst_s(MS_SPHERE, m4_trs(add(V(x, 34.5f, z), mul(f, 56)), 0, 0, 0, V(0.4f, 0.4f, 0.4f)), &red, 0.3f);
}

/* ---- landmarks */
static v3 WHEEL_C; /* ferris wheel hub, animated in the renderer */
static v3 LIGHTHOUSE;
static void build_landmarks(void) {
    /* Calder Needle: observation tower downtown */
    float nx = 12 * PITCH + PITCH / 2, nz = 11 * PITCH + PITCH / 2;
    Mat conc = mat(V(0.75f, 0.74f, 0.72f), PAT_PLAIN, 0, 0.6f);
    inst_s(MS_CYL, m4_trs(V(nx, 0, nz), 0, 0, 0, V(9, 190, 9)), &conc, 0);
    add_box(V(nx - 4.5f, 0, nz - 4.5f), V(nx + 4.5f, 190, nz + 4.5f));
    for (int k = 0; k < 3; k++) {
        float a = k * 2.094f;
        v3 o = V(cosf(a) * 7, 0, sinf(a) * 7);
        inst_s(MS_CUBE, m4_trs(add(V(nx, 30, nz), o), -a, 0, 0, V(1.6f, 60, 7)), &conc, 0);
    }
    Mat pod = mat(V(0.15f, 0.2f, 0.25f), PAT_TOWER, 0.7f, 0.2f);
    inst_s(MS_CYL, m4_trs(V(nx, 170, nz), 0, 0, 0, V(30, 14, 30)), &pod, 0.9f);
    inst_s(MS_CYL, m4_trs(V(nx, 184, nz), 0, 0, 0, V(24, 3, 24)), &conc, 0);
    Mat ring = glow(V(0.1f, 0.2f, 0.3f), V(0.4f, 2.5f, 4.0f), PAT_NEON, 0.9f);
    inst_s(MS_CYL, m4_trs(V(nx, 169, nz), 0, 0, 0, V(31, 0.8f, 31)), &ring, 0.4f);
    inst_s(MS_CYL8, m4_trs(V(nx, 187, nz), 0, 0, 0, V(0.8f, 40, 0.8f)), &M_STEEL, 0);
    Mat red = glow(V(0.3f, 0, 0), V(8, 0.3f, 0.1f), PAT_NEON, 1);
    inst_s(MS_SPHERE, m4_trs(V(nx, 227.5f, nz), 0, 0, 0, V(0.7f, 0.7f, 0.7f)), &red, 0.7f);
    /* plaza around it */
    for (int k = 0; k < 4; k++) {
        float a = k * 1.5708f + 0.785f;
        tree(nx + cosf(a) * 17, nz + sinf(a) * 17, 1.0f, 0, (unsigned)k * 31u);
    }
    /* Calder Bowl stadium */
    float sx = 17 * PITCH + PITCH / 2, sz = 4 * PITCH + PITCH / 2;
    Mat sm = mat(V(0.62f, 0.62f, 0.64f), PAT_RIBS, 0.3f, 0.6f);
    inst_s(MS_CYL, m4_trs(V(sx, 0, sz), 0, 0, 0, V(45, 18, 45)), &sm, 0);
    add_box(V(sx - 20, 0, sz - 20), V(sx + 20, 18, sz + 20));
    Mat lights = glow(V(0.5f, 0.5f, 0.5f), V(3.5f, 3.5f, 3.2f), PAT_PLAIN, 1);
    for (int k = 0; k < 4; k++) {
        float a = k * 1.5708f + 0.785f;
        v3 p = V(sx + cosf(a) * 21, 0, sz + sinf(a) * 21);
        inst_s(MS_CYL8, m4_trs(p, 0, 0, 0, V(0.7f, 30, 0.7f)), &M_STEEL, 0);
        inst_s(MS_CUBE, m4_trs(add(p, V(0, 30, 0)), -a + 1.5708f, 0.4f, 0, V(5, 3, 0.6f)), &lights, 0);
        add_slight(add(p, V(0, 28, 0)), V(3, 3, 2.7f), 45);
    }
    Mat ringn = glow(V(0.2f, 0.1f, 0.1f), V(4, 0.6f, 0.3f), PAT_NEON, 0.9f);
    inst_s(MS_CYL, m4_trs(V(sx, 16.5f, sz), 0, 0, 0, V(45.6f, 0.8f, 45.6f)), &ringn, 0.2f);
    /* ferris wheel pier on the east beach */
    float py = 0.6f;
    float px0 = CITY + 96, px1 = CITY + 330, pz = 700;
    Mat deck = M_WOOD;
    sbox(px0, -6, pz - 11, px1, py, pz + 11, &deck, 0, false);
    add_box(V(px0, -6, pz - 11), V(px1, py, pz + 11));
    for (float x = px0 + 10; x < px1; x += 14)
        for (int s = -1; s <= 1; s += 2) inst_s(MS_CYL8, m4_trs(V(x, -6, pz + s * 9.5f), 0, 0, 0, V(0.8f, 6.6f, 0.8f)), &M_WOOD, 0);
    Mat rail = mat(V(0.8f, 0.8f, 0.8f), PAT_PLAIN, 0.5f, 0.4f);
    sbox(px0 + 4, py, pz - 11, px1, py + 1.1f, pz - 10.8f, &rail, 0, true);
    sbox(px0 + 4, py, pz + 10.8f, px1, py + 1.1f, pz + 11, &rail, 0, true);
    sbox(px1 - 0.2f, py, pz - 11, px1, py + 1.1f, pz + 11, &rail, 0, true);
    /* ramp up from the sand */
    {
        Mat rm = M_WOOD;
        inst_s(MS_WEDGE, m4_trs(V(px0 - 6, -0.4f, pz), 1.5708f, 0, 0, V(22, 1.0f, 12)), &rm, 0);
        add_box_r(V(px0 - 12, -0.4f, pz - 11), V(px0, py, pz + 11), 4);
    }
    WHEEL_C = V(px1 - 40, py + 31, pz);
    for (int s = -1; s <= 1; s += 2)
        for (int k = -1; k <= 1; k += 2) {
            v3 foot = V(WHEEL_C.x + k * 12, py, pz + s * 6);
            v3 d = sub(V(WHEEL_C.x, WHEEL_C.y, pz + s * 3.2f), foot);
            float L = len(d);
            inst_s(MS_CUBE, m4_trs(add(foot, mul(d, 0.5f)), 0, 0, atan2f(-d.x, d.y), V(1.0f, L, 1.0f)), &M_WHITE, 0);
            add_box(V(foot.x - 0.8f, py, foot.z - 0.8f), V(foot.x + 0.8f, py + 4, foot.z + 0.8f));
        }
    /* lighthouse on the north-east point */
    LIGHTHOUSE = V(CITY + 70, 0, -70);
    Mat lw = mat(V(0.85f, 0.85f, 0.82f), PAT_PLAIN, 0, 0.5f), lr = mat(V(0.65f, 0.08f, 0.06f), PAT_PLAIN, 0, 0.5f);
    for (int k = 0; k < 6; k++) inst_s(MS_CYL, m4_trs(V(LIGHTHOUSE.x, k * 5.0f, LIGHTHOUSE.z), 0, 0, 0, V(7 - k * 0.5f, 5, 7 - k * 0.5f)), k % 2 ? &lr : &lw, 0);
    Mat lamp = glow(V(0.5f, 0.5f, 0.4f), V(8, 7, 4), PAT_PLAIN, 0.8f);
    inst_s(MS_CYL8, m4_trs(V(LIGHTHOUSE.x, 30, LIGHTHOUSE.z), 0, 0, 0, V(3.4f, 3, 3.4f)), &lamp, 0);
    inst_s(MS_CONE, m4_trs(V(LIGHTHOUSE.x, 33, LIGHTHOUSE.z), 0, 0, 0, V(4.4f, 3, 4.4f)), &lr, 0);
    add_box(V(LIGHTHOUSE.x - 3.5f, 0, LIGHTHOUSE.z - 3.5f), V(LIGHTHOUSE.x + 3.5f, 36, LIGHTHOUSE.z + 3.5f));
    /* docks: quay wall, piers, cranes and a cargo ship */
    Mat quay = mat(V(0.35f, 0.34f, 0.32f), PAT_PLAIN, 0, 0.9f);
    sbox(-20, -9, CITY + 51, DOCK_X, 0.05f, CITY + 53, &quay, 0, false);
    sbox(DOCK_X - 2, -9, CITY + 9, DOCK_X, 0.05f, CITY + 53, &quay, 0, false);
    float piers[3] = {230, 560, 900};
    for (int k = 0; k < 3; k++) {
        float x = piers[k];
        sbox(x - 16, -9, CITY + 52, x + 16, 0.0f, CITY + 190, &quay, 0, false);
        add_box(V(x - 16, -9, CITY + 52), V(x + 16, 0.0f, CITY + 190));
        containers(x - 12, CITY + 70, x - 2, CITY + 180, (unsigned)k);
        crane(x + 10, CITY + 120, 1.5708f * (k % 2 ? 1 : -1));
    }
    /* cargo ship moored at the middle pier */
    float shx = 560 + 32, shz0 = CITY + 60, shz1 = CITY + 200;
    Mat hull = mat(V(0.45f, 0.08f, 0.06f), PAT_PLAIN, 0.4f, 0.5f), hull2 = mat(V(0.06f, 0.06f, 0.07f), PAT_PLAIN, 0.4f, 0.5f);
    sbox(shx, -6, shz0, shx + 22, 2, shz1, &hull2, 0, true);
    sbox(shx, 2, shz0, shx + 22, 7, shz1, &hull, 0, true);
    inst_s(MS_WEDGE, m4_trs(V(shx + 11, -6, shz0 - 10), 0, 0, 0, V(22, 13, 20)), &hull, 0);
    Mat bridge = mat(V(0.85f, 0.85f, 0.82f), PAT_MIDRISE, 0, 0.6f);
    sbox(shx + 2, 7, shz1 - 26, shx + 20, 22, shz1 - 8, &bridge, 0.33f, false);
    containers(shx + 2, shz0 + 6, shx + 20, shz1 - 30, 9);
    for (int k = 0; k < 3; k++) crane(piers[k] - 40, CITY + 40, 3.14159f);
}

/* ---- the whole city */
static void gen_block(int i, int j) {
    float x0 = i * PITCH + 9, z0 = j * PITCH + 9, x1 = (i + 1) * PITCH - 9, z1 = (j + 1) * PITCH - 9;
    float cx = (x0 + x1) / 2, cz = (z0 + z1) / 2;
    int d = dist_blk[i][j];
    unsigned s = hashu((unsigned)(i * 131 + j * 977 + 12345));
    float r = (s % 10000) / 10000.0f;
    int ptype = poi_block(i, j);
    if (i == 12 && j == 11) { lot_type[i][j] = LOT_PLAZA; return; } /* the Needle plaza */
    if (i == 17 && j == 4) { lot_type[i][j] = LOT_ASPH; return; }    /* the stadium */
    if (ptype >= 0 && ptype != POI_JOB) {
        lot_type[i][j] = ptype == POI_RESPRAY ? LOT_ASPH : LOT_PLAZA;
        Mat m;
        float h = 14;
        if (ptype == POI_POLICE) m = mat(V(0.22f, 0.28f, 0.42f), PAT_MIDRISE, 0, 0.8f);
        else if (ptype == POI_HOSPITAL) m = mat(V(0.82f, 0.82f, 0.80f), PAT_MIDRISE, 0, 0.7f), h = 24;
        else if (ptype == POI_SAFE) m = mat(V(0.50f, 0.36f, 0.30f), PAT_MIDRISE, 0, 0.8f), h = 20;
        else m = mat(V(0.30f, 0.30f, 0.32f), PAT_RIBS, 0.5f, 0.5f), h = 7;
        if (ptype == POI_RESPRAY) {
            building(x0 + 4, z0 + 14, x1 - 4, z1 - 2, h, &m, 0);
            neon(x0 + 8, 5.5f, z0 + 13.6f, x1 - 8, 6.3f, z0 + 13.9f, V(1.0f, 0.45f, 0.05f), 0.5f);
        } else {
            building(x0 + 2, z0 + 10, x1 - 2, z1 - 2, h, &m, r);
            rooftop_bits(x0 + 2, z0 + 10, x1 - 2, z1 - 2, h, 0.1f);
        }
        if (ptype == POI_POLICE) {
            neon(x0 + 6, 9, z0 + 9.6f, x1 - 6, 10, z0 + 9.9f, V(0.15f, 0.35f, 1.0f), 0.2f);
            neon(cx - 1.5f, 3, z0 + 9.6f, cx + 1.5f, 4, z0 + 9.9f, V(1.0f, 1.0f, 1.0f), 0.6f);
        }
        if (ptype == POI_HOSPITAL) {
            neon(cx - 3, 17.2f, z0 + 9.6f, cx + 3, 18.8f, z0 + 9.9f, V(1.0f, 0.1f, 0.1f), 0.1f);
            neon(cx - 0.8f, 15, z0 + 9.6f, cx + 0.8f, 21, z0 + 9.9f, V(1.0f, 0.1f, 0.1f), 0.1f);
        }
        if (ptype == POI_SAFE) neon(cx - 5, 4.5f, z0 + 9.6f, cx + 5, 5.2f, z0 + 9.9f, V(0.3f, 1.0f, 0.5f), 0.9f);
        return;
    }
    switch (d) {
    case D_CENTER: {
        lot_type[i][j] = LOT_PLAZA;
        float dc = fmaxf(fabsf(i - 12.0f), fabsf(j - 11.0f));
        float hmax = 200 - dc * 38;
        if (r < 0.12f) { /* plaza with fountain */
            Mat f = M_CONCRETE;
            inst_s(MS_CYL, m4_trs(V(cx, 0, cz), 0, 0, 0, V(12, 0.8f, 12)), &f, 0);
            inst_s(MS_CYL, m4_trs(V(cx, 0.1f, cz), 0, 0, 0, V(11, 0.75f, 11)), &M_POND, 0);
            inst_s(MS_CYL8, m4_trs(V(cx, 0, cz), 0, 0, 0, V(1.2f, 3.2f, 1.2f)), &M_STEEL, 0);
            add_box(V(cx - 6, 0, cz - 6), V(cx + 6, 0.9f, cz + 6));
            for (int k = 0; k < 6; k++) tree(cx + cosf(k * 1.047f) * 16, cz + sinf(k * 1.047f) * 16, 0.9f, 0, s + (unsigned)k);
        } else if (r < 0.55f) tower(x0, z0, x1, z1, hmax * (0.5f + 0.5f * hash2f(i, j, 2)), s);
        else {
            float h1 = hmax * (0.35f + 0.6f * hash2f(i, j, 3)), h2 = hmax * (0.3f + 0.5f * hash2f(i, j, 4));
            if (r < 0.78f) { tower(x0, z0, cx - 1.5f, z1, h1, s); tower(cx + 1.5f, z0, x1, z1, h2, s + 1); }
            else { tower(x0, z0, x1, cz - 1.5f, h1, s); tower(x0, cz + 1.5f, x1, z1, h2, s + 1); }
        }
        break;
    }
    case D_MIDTOWN:
    case D_OLDTOWN: {
        bool old = d == D_OLDTOWN;
        lot_type[i][j] = LOT_PLAZA;
        if (!old && r < 0.14f) { /* parking lot */
            lot_type[i][j] = LOT_ASPH;
            for (int k = 0; k < 14; k++) {
                if (hash2f(i, j, k + 20) < 0.45f) continue;
                float x = x0 + 3 + (k % 7) * 6.0f + 1.5f;
                float z = k < 7 ? z0 + 6 : z1 - 6;
                add_slot(x, z, k < 7 ? 3.14159f : 0);
            }
            midrise(cx - 8, cz - 5, cx + 8, cz + 5, 4, s, false);
            break;
        }
        for (int q = 0; q < 4; q++) {
            float qx0 = q & 1 ? cx + 1 : x0, qx1 = q & 1 ? x1 : cx - 1, qz0 = q & 2 ? cz + 1 : z0, qz1 = q & 2 ? z1 : cz - 1;
            float rh = hash2f(i * 4 + q, j, 5);
            float h = old ? 9 + rh * 12 : 12 + rh * rh * 38;
            if (rh < 0.08f) { tree((qx0 + qx1) / 2, (qz0 + qz1) / 2, 1.1f, 0, s + (unsigned)q); continue; }
            midrise(qx0, qz0, qx1, qz1, h, s + (unsigned)q, old);
        }
        break;
    }
    case D_MAPLE:
    case D_SEABRIGHT: {
        bool sea = d == D_SEABRIGHT;
        lot_type[i][j] = LOT_GRASS;
        if (sea && i == NB - 1 && r < 0.6f) { /* beachfront hotel */
            Mat m = mat(PICK(PASTEL, r * 3), PAT_MIDRISE, 0, 0.8f);
            building(x0 + 4, z0 + 4, x1 - 4, z1 - 4, 18 + r * 30, &m, r);
            neon(x0 + 8, 6, z0 + 3.6f, x1 - 8, 6.8f, z0 + 3.95f, NEONS[s % 6], r);
            for (int k = 0; k < 4; k++) tree(k & 1 ? x1 - 1.5f : x0 + 1.5f, k & 2 ? z1 - 1.5f : z0 + 1.5f, 1.0f, 2, s + (unsigned)k);
            break;
        }
        for (int a = 0; a < 3; a++)
            for (int b = 0; b < 3; b++) {
                float hx = x0 + 7.7f + a * 15.3f, hz = z0 + 7.7f + b * 15.3f;
                unsigned hs = hashu(s + (unsigned)(a * 3 + b) * 7919u);
                if (a == 1 && b == 1) {
                    if (hs % 3 == 0) {
                        inst_s(MS_CUBE, m4_box(V(hx, 0.02f, hz), V(7, 0.1f, 4)), &M_POND, 0);
                        sbox(hx - 3.8f, 0, hz - 2.4f, hx + 3.8f, 0.03f, hz + 2.4f, &M_WHITE, 0, false);
                    } else tree(hx, hz, 1.2f, sea ? 2 : (hs % 2), hs);
                    continue;
                }
                float w = 8 + (hs % 30) / 10.0f, dd = 7.5f + ((hs >> 5) % 25) / 10.0f;
                v3 wall = sea ? PICK(PASTEL, (hs % 97) / 97.0f) : PICK(HOUSEC, (hs % 97) / 97.0f);
                house(hx, hz + 0.8f, w, dd, 1 + (int)((hs >> 9) % 2), wall, PICK(ROOFC, ((hs >> 12) % 89) / 89.0f), hs, sea && (hs % 3 == 0));
                if (b == 0 && hs % 3 != 0) add_slot(hx, z0 + 2.2f, (hs % 2) ? 1.5708f : -1.5708f);
                if (hs % 4 == 0) tree(hx + 5.6f, hz + 5.5f, 0.8f, sea ? 2 : 0, hs >> 3);
            }
        break;
    }
    case D_PARK: {
        lot_type[i][j] = LOT_PARK;
        if (i == 4 && j == 5) { /* the lake */
            float lx = (i + 1) * PITCH, lz = (j + 1) * PITCH;
            inst_s(MS_CYL, m4_trs(V(lx, 0.0f, lz), 0, 0, 0, V(70, 0.06f, 52)), &M_POND, 0);
            Mat bank = mat(V(0.45f, 0.40f, 0.30f), PAT_PLAIN, 0, 0.9f);
            inst_s(MS_CYL, m4_trs(V(lx, 0, lz), 0, 0, 0, V(73, 0.04f, 55)), &bank, 0);
        }
        bool lake = (i == 4 || i == 5) && (j == 5 || j == 6);
        for (int k = 0; k < 16; k++) {
            float tx = x0 - 6 + hash2f(i, j, k * 2) * 58, tz = z0 - 6 + hash2f(i, j, k * 2 + 1) * 58;
            if (lake && dist2d(V(tx, 0, tz), V(5 * PITCH, 0, 6 * PITCH)) < 40) continue;
            if (on_road(tx, tz, 4.8f)) continue;
            float bl = fabsf((tx - cx) - (tz - cz)), br = fabsf((tx - cx) + (tz - cz));
            if (bl < 4 || br < 4 || fabsf(dist2d(V(tx, 0, tz), V(cx, 0, cz)) - 14) < 3) continue;
            tree(tx, tz, 1.0f + hash2f(i, j, k + 50) * 0.5f, (k % 3 == 0) ? 1 : 0, s + (unsigned)k * 13u);
        }
        if (!lake) {
            Mat st = mat(V(0.45f, 0.45f, 0.48f), PAT_PLAIN, 0.2f, 0.6f);
            inst_s(MS_CYL, m4_trs(V(cx, 0, cz), 0, 0, 0, V(5, 1, 5)), &st, 0);
            inst_s(MS_CUBE, m4_box(V(cx, 2.3f, cz), V(1.2f, 2.6f, 1.2f)), &st, 0);
            inst_s(MS_SPHERE, m4_trs(V(cx, 4.4f, cz), 0, 0, 0, V(0.9f, 0.9f, 0.9f)), &M_STEEL, 0);
            add_box(V(cx - 2.5f, 0, cz - 2.5f), V(cx + 2.5f, 5, cz + 2.5f));
        }
        break;
    }
    case D_DOCKS:
    case D_EASTGATE: {
        lot_type[i][j] = d == D_DOCKS ? LOT_DIRT : (r < 0.5f ? LOT_ASPH : LOT_DIRT);
        if (r < 0.45f) warehouse(x0 + 2, z0 + 8, x1 - 2, z1 - 3, 9 + r * 8, s);
        else if (r < 0.8f) {
            warehouse(x0 + 2, z0 + 22, x1 - 2, z1 - 2, 8, s);
            containers(x0 + 2, z0 + 2, x1 - 2, z0 + 16, s);
        } else {
            midrise(x0 + 2, z0 + 2, cx - 2, cz - 2, 10 + r * 6, s, true);
            containers(cx + 1, z0 + 2, x1 - 1, z1 - 2, s);
            for (int k = 0; k < 4; k++) add_slot(x0 + 5 + k * 5.5f, z1 - 7, 0);
        }
        break;
    }
    }
}

static void build_city(void) {
    for (int i = 0; i < NB; i++)
        for (int j = 0; j < NB; j++) dist_blk[i][j] = (unsigned char)district_block(i, j);
    for (int i = 0; i <= NB; i++)
        for (int j = 0; j <= NB; j++) {
            unsigned char f = 0;
            if (i < NB && !(j >= 5 && j <= 7 && i >= 3 && i <= 6)) f |= 1;
            if (j < NB && !(i >= 4 && i <= 6 && j >= 4 && j <= 7)) f |= 2;
            nflag[i][j] = f;
        }
    /* points of interest: placed before the blocks so those blocks get the right buildings */
    int safe[1][2] = {{8, 9}};
    int police[3][2] = {{11, 15}, {3, 14}, {21, 9}};
    int hosp[3][2] = {{14, 6}, {6, 17}, {22, 19}};
    int resp[3][2] = {{2, 2}, {17, 17}, {21, 3}};
    int jobs[7][2] = {{1, 10}, {10, 4}, {15, 13}, {5, 21}, {18, 20}, {23, 12}, {9, 18}};
    add_poi(POI_SAFE, safe[0][0], safe[0][1]);
    for (int k = 0; k < 3; k++) add_poi(POI_POLICE, police[k][0], police[k][1]);
    for (int k = 0; k < 3; k++) add_poi(POI_HOSPITAL, hosp[k][0], hosp[k][1]);
    for (int k = 0; k < 3; k++) add_poi(POI_RESPRAY, resp[k][0], resp[k][1]);
    for (int k = 0; k < 7; k++) add_poi(POI_JOB, jobs[k][0], jobs[k][1]);
    for (int k = 0; k < npoi; k++)
        if (poi[k].type == POI_RESPRAY) poi[k].pos = V(poi[k].i * PITCH + PITCH / 2, 0, poi[k].j * PITCH + 9 + 7);
    for (int i = 0; i < NB; i++)
        for (int j = 0; j < NB; j++) gen_block(i, j);
    build_landmarks();
    /* the ground and the sea are instances too */
    Mat g = {{1, 1, 1}, PAT_GROUND, 0, 0.8f, {0, 0, 0}, 0};
    inst_s(MS_GROUND, m4_id(), &g, 0);
    Mat o = {{0.01f, 0.04f, 0.05f}, PAT_OCEAN, 0, 0.05f, {0, 0, 0}, 0};
    inst_s(MS_OCEAN, m4_trs(V(0, WATER_Y, 0), 0, 0, 0, V(1, 1, 1)), &o, 0);
    /* green belt + beach scenery */
    srand(777);
    for (int k = 0; k < 900; k++) {
        float x = WMIN + frand() * (WMAX - WMIN), z = WMIN + frand() * (WMAX - WMIN);
        float d = out_dist(x, z);
        if (d < 14 || terrain_h(x, z) < -0.35f || is_dock_zone(x, z)) continue;
        if (x > CITY + 80 && z > 640 && z < 760) continue;
        if (dist2d(V(x, 0, z), LIGHTHOUSE) < 12) continue;
        bool sand = terrain_h(x, z) < -0.02f;
        tree(x, z, 1.0f + frand() * 0.5f, sand || x > CITY ? 2 : (frand() < 0.4f ? 1 : 0), (unsigned)k * 2654435761u);
    }
    /* beach furniture: lifeguard towers and umbrellas */
    for (int k = 0; k < 9; k++) {
        float z = 80 + k * 170, x = CITY + 118;
        if (fabsf(z - 700) < 40) continue;
        Mat lg = mat(V(0.85f, 0.75f, 0.35f), PAT_PLANK, 0, 0.7f);
        for (int l = 0; l < 4; l++) inst_s(MS_CUBE, m4_box(V(x + (l & 1 ? 1.4f : -1.4f), 1.5f + terrain_h(x, z), z + (l & 2 ? 1.4f : -1.4f)), V(0.2f, 3, 0.2f)), &lg, 0);
        sbox(x - 1.8f, 3 + terrain_h(x, z), z - 1.8f, x + 1.8f, 5.4f + terrain_h(x, z), z + 1.8f, &lg, 0, false);
        add_box(V(x - 1.8f, -1, z - 1.8f), V(x + 1.8f, 5.4f, z + 1.8f));
        for (int u = 0; u < 4; u++) {
            float ux = CITY + 100 + u * 7, uz = z + 30 + u * 9;
            float th = terrain_h(ux, uz);
            Mat um = mat(NEONS[(k + u) % 6], PAT_PLAIN, 0, 0.7f);
            inst_s(MS_CYL8, m4_trs(V(ux, th, uz), 0, 0, 0, V(0.1f, 2.4f, 0.1f)), &M_WHITE, 0);
            inst_s(MS_CONE, m4_trs(V(ux, th + 2.0f, uz), 0, 0, 0, V(3.2f, 0.7f, 3.2f)), &um, 0);
        }
    }
    /* stunt ramps */
    add_ramp(V(CITY + 110, -0.1f, 300), 0, 6, 12, 3.0f);
    add_ramp(V(CITY + 110, -0.1f, 1100), 3.14159f, 6, 12, 3.2f);
    add_ramp(V(5 * PITCH + 20, 0, 4 * PITCH + 30), 1.5708f, 6, 12, 2.8f);
    add_ramp(V(4 * PITCH - 30, 0, 7 * PITCH + 30), 0, 6, 12, 2.6f);
    add_ramp(V(230, 0, CITY + 120), 3.14159f, 6, 14, 2.8f);
    add_ramp(V(900 - 8, 0, CITY + 175), 3.14159f, 6, 14, 3.0f);
    add_ramp(V(-60, 0, 700), -1.5708f, 6, 12, 3.0f);
    add_ramp(V(700, 0, -50), 0, 6, 12, 3.0f);
    add_ramp(V(1280, 0, CITY + 40), 1.5708f, 6, 12, 2.8f);
    add_ramp(V(CITY + 100, -0.1f, 600), 0, 5, 10, 1.4f);
    /* street lamp light sources for the renderer (the poles themselves are props) */
    for (int i = 0; i < npoi; i++) (void)i;
}

/* upload the per-block map that the ground shader reads */
static void upload_city_tex(void) {
    unsigned char px[(NB + 1) * (NB + 1) * 4];
    for (int j = 0; j <= NB; j++)
        for (int i = 0; i <= NB; i++) {
            unsigned char *o = px + (j * (NB + 1) + i) * 4;
            o[0] = i < NB && j < NB ? lot_type[i][j] : 0;
            o[1] = nflag[i][j];
            o[2] = i < NB && j < NB ? dist_blk[i][j] : 0;
            o[3] = 255;
        }
    glGenTextures(1, &tex_city);
    glBindTexture(GL_TEXTURE_2D, tex_city);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, NB + 1, NB + 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
}

/* ---- collision queries */
static float box_top(const Box *b, float x, float z) {
    float h = b->mx.y - b->mn.y;
    switch (b->ramp) {
    case 1: return b->mn.y + h * clampf((b->mx.z - z) / (b->mx.z - b->mn.z), 0, 1);
    case 2: return b->mn.y + h * clampf((z - b->mn.z) / (b->mx.z - b->mn.z), 0, 1);
    case 3: return b->mn.y + h * clampf((b->mx.x - x) / (b->mx.x - b->mn.x), 0, 1);
    case 4: return b->mn.y + h * clampf((x - b->mn.x) / (b->mx.x - b->mn.x), 0, 1);
    default: return b->mx.y;
    }
}
/* height of the walkable surface under (x,z) for something whose feet are at y */
static float ground_at(float x, float z, float feet, float rad) {
    float g = terrain_h(x, z);
    query_boxes(x - rad, z - rad, x + rad, z + rad);
    for (int k = 0; k < qn; k++) {
        const Box *b = &boxes[qlist[k]];
        if (x < b->mn.x - rad * 0.3f || x > b->mx.x + rad * 0.3f || z < b->mn.z - rad * 0.3f || z > b->mx.z + rad * 0.3f) continue;
        float t = box_top(b, clampf(x, b->mn.x, b->mx.x), clampf(z, b->mn.z, b->mx.z));
        if (t <= feet + STEP && t > g) g = t;
    }
    return g;
}
/* push a vertical cylinder out of solid boxes; returns true on contact, with the summed push normal */
static bool collide_circle(v3 *p, float rad, float height, v3 *nrm) {
    bool hit = false;
    v3 nsum = V(0, 0, 0);
    query_boxes(p->x - rad, p->z - rad, p->x + rad, p->z + rad);
    for (int k = 0; k < qn; k++) {
        const Box *b = &boxes[qlist[k]];
        if (b->ramp) {
            float t = box_top(b, clampf(p->x, b->mn.x, b->mx.x), clampf(p->z, b->mn.z, b->mx.z));
            if (t <= p->y + STEP) continue;
        } else if (b->mx.y <= p->y + STEP) continue;
        if (b->mn.y >= p->y + height) continue;
        float cx = clampf(p->x, b->mn.x, b->mx.x), cz = clampf(p->z, b->mn.z, b->mx.z);
        float dx = p->x - cx, dz = p->z - cz, d2 = dx * dx + dz * dz;
        if (d2 >= rad * rad) continue;
        if (d2 > 1e-8f) {
            float d = sqrtf(d2), push = rad - d;
            p->x += dx / d * push;
            p->z += dz / d * push;
            nsum = add(nsum, V(dx / d, 0, dz / d));
        } else { /* centre inside: push out along the shallowest axis */
            float l = p->x - b->mn.x, r = b->mx.x - p->x, f = p->z - b->mn.z, bk = b->mx.z - p->z;
            float m = fminf(fminf(l, r), fminf(f, bk));
            if (m == l) { p->x = b->mn.x - rad; nsum = add(nsum, V(-1, 0, 0)); }
            else if (m == r) { p->x = b->mx.x + rad; nsum = add(nsum, V(1, 0, 0)); }
            else if (m == f) { p->z = b->mn.z - rad; nsum = add(nsum, V(0, 0, -1)); }
            else { p->z = b->mx.z + rad; nsum = add(nsum, V(0, 0, 1)); }
        }
        hit = true;
    }
    if (nrm) *nrm = norm(nsum);
    return hit;
}
static bool point_blocked(v3 p, float rad, float height) {
    v3 q = p;
    return collide_circle(&q, rad, height, NULL);
}
/* ray vs solid boxes (for the camera); returns hit distance or maxd */
static float ray_world(v3 o, v3 d, float maxd) {
    v3 e = add(o, mul(d, maxd));
    query_boxes(fminf(o.x, e.x) - 1, fminf(o.z, e.z) - 1, fmaxf(o.x, e.x) + 1, fmaxf(o.z, e.z) + 1);
    float best = maxd;
    for (int k = 0; k < qn; k++) {
        const Box *b = &boxes[qlist[k]];
        if (b->mx.y < 1.2f || b->ramp) continue;
        float t0 = 0, t1 = best;
        float oo[3] = {o.x, o.y, o.z}, dd[3] = {d.x, d.y, d.z}, mn[3] = {b->mn.x, b->mn.y, b->mn.z}, mx[3] = {b->mx.x, b->mx.y, b->mx.z};
        bool ok = true;
        for (int a = 0; a < 3 && ok; a++) {
            if (fabsf(dd[a]) < 1e-6f) { if (oo[a] < mn[a] || oo[a] > mx[a]) ok = false; continue; }
            float ta = (mn[a] - oo[a]) / dd[a], tb = (mx[a] - oo[a]) / dd[a];
            if (ta > tb) { float t = ta; ta = tb; tb = t; }
            t0 = fmaxf(t0, ta);
            t1 = fminf(t1, tb);
            if (t0 > t1) ok = false;
        }
        if (ok && t0 < best) best = t0;
    }
    float g = o.y - terrain_h(o.x, o.z);
    (void)g;
    return best;
}
