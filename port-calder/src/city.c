/* city.c - builds California: street grids, buildings in regional styles, shops, hotels and restaurants,
 * landmarks, and the countryside (forests, farms, vineyards, wind farms, oil fields, deserts). */

static void put(int mesh, m4 m, const Mat *mt, float seed) { m.m[13] += GY; inst_s(mesh, m, mt, seed); }
static Mat mat(v3 base, int pat, float metal, float rough) { Mat m = {base, pat, metal, rough, {0, 0, 0}, 0}; return m; }
static Mat glow(v3 base, v3 emis, int pat, float night) { Mat m = {base, pat, 0, 0.4f, emis, night}; return m; }
static const Mat M_TRUNK = {{0.20f, 0.13f, 0.08f}, PAT_LEAF, 0, 0.9f, {0, 0, 0}, 0};
static const Mat M_CONCRETE = {{0.42f, 0.41f, 0.39f}, PAT_PLAIN, 0, 0.85f, {0, 0, 0}, 0};
static const Mat M_DARKMETAL = {{0.10f, 0.10f, 0.11f}, PAT_PLAIN, 0.8f, 0.4f, {0, 0, 0}, 0};
static const Mat M_STEEL = {{0.55f, 0.56f, 0.58f}, PAT_PLAIN, 1.0f, 0.3f, {0, 0, 0}, 0};
static const Mat M_WHITE = {{0.80f, 0.80f, 0.78f}, PAT_PLAIN, 0, 0.6f, {0, 0, 0}, 0};
static const Mat M_WOOD = {{0.36f, 0.24f, 0.14f}, PAT_PLANK, 0, 0.8f, {0, 0, 0}, 0};
static const Mat M_POND = {{0.02f, 0.06f, 0.07f}, PAT_POND, 0, 0.05f, {0, 0, 0}, 0};
static const Mat M_ROCK = {{0.42f, 0.38f, 0.33f}, PAT_LEAF, 0, 0.9f, {0, 0, 0}, 0};

static void sbox(float x0, float y0, float z0, float x1, float y1, float z1, const Mat *m, float seed, bool solid) {
    put(MS_CUBE, m4_box(V((x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2), V(x1 - x0, y1 - y0, z1 - z0)), m, seed);
    if (solid) add_box(V(x0, y0, z0), V(x1, y1, z1));
}
static void building(float x0, float z0, float x1, float z1, float h, const Mat *m, float seed) { sbox(x0, 0, z0, x1, h, z1, m, seed, true); }

/* trees: 0 broadleaf, 1 pine, 2 palm, 3 redwood, 4 joshua tree, 5 cactus, 6 oak (wide, dark) */
static void tree_at(float x, float y, float z, float s, int kind, unsigned seed) {
    float hv = 0.85f + (seed % 100) / 300.0f;
    s *= hv;
    v3 leaf = kind == 1 || kind == 3 ? V(0.05f, 0.13f, 0.07f) : kind == 2 ? V(0.12f, 0.24f, 0.06f) : kind == 4 ? V(0.18f, 0.24f, 0.10f)
                                                                             : kind == 6 ? V(0.10f, 0.16f, 0.05f)
                                                                                         : V(0.08f + (seed % 7) * 0.012f, 0.20f + (seed % 5) * 0.015f, 0.05f);
    Mat ml = mat(leaf, PAT_LEAF, 0, 0.9f);
    float ry = (seed % 628) * 0.01f;
    switch (kind) {
    case 0:
    case 6: {
        float w = kind == 6 ? 1.5f : 1.0f;
        inst_s(MS_CYL8, m4_trs(V(x, y, z), 0, 0, 0, V(0.45f * s, 3.0f * s, 0.45f * s)), &M_TRUNK, 0);
        inst_s(MS_BLOB, m4_trs(V(x, y + 4.0f * s, z), ry, 0, 0, V(2.6f * s * w, 2.0f * s, 2.6f * s * w)), &ml, 0);
        inst_s(MS_BLOB, m4_trs(V(x + 0.9f * s, y + 5.2f * s, z - 0.4f * s), ry + 0.7f, 0, 0, V(1.8f * s * w, 1.5f * s, 1.8f * s * w)), &ml, 0);
        break;
    }
    case 1:
    case 3: {
        float H = kind == 3 ? 3.2f : 1.0f;
        inst_s(MS_CYL8, m4_trs(V(x, y, z), 0, 0, 0, V(0.45f * s * H * 0.8f, 2.4f * s * H, 0.45f * s * H * 0.8f)), &M_TRUNK, 0);
        inst_s(MS_CONE, m4_trs(V(x, y + 1.6f * s * H, z), ry, 0, 0, V(3.4f * s * (kind == 3 ? 1.6f : 1), 5.0f * s * H, 3.4f * s * (kind == 3 ? 1.6f : 1))), &ml, 0);
        inst_s(MS_CONE, m4_trs(V(x, y + 4.4f * s * H, z), ry + 0.5f, 0, 0, V(2.4f * s * (kind == 3 ? 1.4f : 1), 4.0f * s * H, 2.4f * s * (kind == 3 ? 1.4f : 1))), &ml, 0);
        break;
    }
    case 2: {
        float lean = ((seed >> 3) % 10) * 0.025f, H = 8.0f * s;
        v3 top = V(x + sinf(ry) * lean * H, y + H, z - cosf(ry) * lean * H);
        inst_s(MS_CYL8, m4_trs(V(x, y, z), ry, lean, 0, V(0.35f * s, H, 0.35f * s)), &M_TRUNK, 0);
        for (int k = 0; k < 7; k++) {
            float a = k * 0.8976f + ry;
            v3 d = fwd_of(a);
            inst_s(MS_CUBE, m4_trs(add(top, add(mul(d, 1.6f * s), V(0, -0.35f * s, 0))), a, -0.42f, 0, V(0.9f * s, 0.08f, 3.4f * s)), &ml, 0);
        }
        break;
    }
    case 4: { /* joshua tree: crooked trunk with spiky tufts */
        Mat bark = mat(V(0.28f, 0.22f, 0.16f), PAT_LEAF, 0, 0.9f);
        inst_s(MS_CYL8, m4_trs(V(x, y, z), 0, 0, 0, V(0.4f * s, 2.6f * s, 0.4f * s)), &bark, 0);
        for (int k = 0; k < 3; k++) {
            float a = ry + k * 2.1f;
            v3 d = fwd_of(a);
            v3 b0 = V(x, y + 2.4f * s, z);
            inst_s(MS_CYL8, m4_trs(b0, a, 0.7f, 0, V(0.25f * s, 1.6f * s, 0.25f * s)), &bark, 0);
            v3 tip = add(b0, add(mul(d, 1.1f * s), V(0, 1.2f * s, 0)));
            inst_s(MS_BLOB, m4_trs(tip, a, 0, 0, V(0.55f * s, 0.6f * s, 0.55f * s)), &ml, 0);
        }
        break;
    }
    case 5: { /* saguaro-like cactus */
        Mat c = mat(V(0.16f, 0.30f, 0.12f), PAT_RIBS, 0, 0.7f);
        inst_s(MS_CYL8, m4_trs(V(x, y, z), 0, 0, 0, V(0.5f * s, 3.2f * s, 0.5f * s)), &c, 0);
        inst_s(MS_CYL8, m4_trs(V(x + 0.55f * s, y + 1.4f * s, z), 0, 0, 0, V(0.32f * s, 1.3f * s, 0.32f * s)), &c, 0);
        inst_s(MS_CYL8, m4_trs(V(x - 0.5f * s, y + 1.9f * s, z), 0, 0, 0, V(0.3f * s, 1.0f * s, 0.3f * s)), &c, 0);
        break;
    }
    }
    if (kind != 5) add_box(V(x - 0.35f - GY * 0, y - GY, z - 0.35f), V(x + 0.35f, y - GY + 4, z + 0.35f));
}
static void tree(float x, float z, float s, int kind, unsigned seed) {
    int saved = static_tier;
    if (static_tier >= 0) static_tier = 1;
    tree_at(x, GY, z, s, kind, seed);
    static_tier = saved;
}

/* parked cars, static lights, points of interest */
typedef struct { v3 pos; float yaw; } Slot;
#define MAXSLOT 3000
static Slot slots[MAXSLOT];
static int nslot;
static void add_slot(float x, float z, float yaw) { if (nslot < MAXSLOT) slots[nslot++] = (Slot){V(x, GY, z), yaw}; }
typedef struct { v3 pos, col; float radius; } SLight;
#define MAXSL 3000
static SLight slights[MAXSL];
static int nslight;
static void add_slight(v3 p, v3 c, float r) { if (nslight < MAXSL) slights[nslight++] = (SLight){add(p, V(0, GY, 0)), c, r}; }

enum { POI_SAFE, POI_POLICE, POI_HOSPITAL, POI_RESPRAY, POI_JOB, POI_RACE, POI_N };
typedef struct { int type; v3 pos; int city, i, j; } Poi;
#define MAXPOI 80
static Poi poi[MAXPOI];
static int npoi;
static v3 block_front(int c, int i, int j) { City *C = &cities[c]; return V(C->x0 + i * PITCH + PITCH / 2, C->gy, C->z0 + j * PITCH + WALK); }
static bool block_taken(int c, int i, int j);
static void add_poi(int type, int c, int i, int j) {
    if (npoi >= MAXPOI || block_taken(c, i, j)) return;
    poi[npoi++] = (Poi){type, block_front(c, i, j), c, i, j};
    if (type == POI_RESPRAY) poi[npoi - 1].pos.z += 9;
}
static int poi_block(int c, int i, int j) { for (int k = 0; k < npoi; k++) if (poi[k].city == c && poi[k].i == i && poi[k].j == j) return poi[k].type; return -1; }

/* stunt ramps */
typedef struct { v3 pos; float yaw, len, h; } Ramp;
#define MAXRAMP 20
static Ramp ramps[MAXRAMP];
static int nramp;
static void add_ramp(v3 base, float yaw, float w, float l, float h) {
    if (nramp >= MAXRAMP) return;
    ramps[nramp++] = (Ramp){base, yaw, l, h};
    v3 f = fwd_of(yaw);
    v3 c = add(base, mul(f, l / 2));
    Mat m = mat(V(0.55f, 0.45f, 0.12f), PAT_RIBS, 0.5f, 0.5f);
    inst_s(MS_WEDGE, m4_trs(c, yaw, 0, 0, V(w, h, l)), &m, 0);
    int dir = fabsf(f.z) > 0.5f ? (f.z < 0 ? 1 : 2) : (f.x < 0 ? 3 : 4);
    float hw = fabsf(f.z) > 0.5f ? w / 2 : l / 2, hl = fabsf(f.z) > 0.5f ? l / 2 : w / 2;
    add_box_r(V(c.x - hw, base.y, c.z - hl), V(c.x + hw, base.y + h, c.z + hl), dir);
}

static void neon(float x0, float y0, float z0, float x1, float y1, float z1, v3 col, float seed) {
    Mat m = glow(mul(col, 0.15f), mul(col, 3.0f), PAT_NEON, 0.85f);
    sbox(x0, y0, z0, x1, y1, z1, &m, seed, false);
    add_slight(V((x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2), mul(col, 1.6f), 14);
}
static const v3 NEONS[] = {{1.0f, 0.15f, 0.55f}, {0.1f, 0.85f, 1.0f}, {1.0f, 0.75f, 0.1f}, {0.4f, 1.0f, 0.3f}, {0.7f, 0.3f, 1.0f}, {1.0f, 0.35f, 0.1f}};
static const v3 TOWER_FRAME[] = {{0.55f, 0.57f, 0.60f}, {0.12f, 0.13f, 0.15f}, {0.42f, 0.32f, 0.22f}, {0.75f, 0.75f, 0.72f}, {0.20f, 0.42f, 0.48f}, {0.30f, 0.32f, 0.38f}};
static const v3 WALLS[] = {{0.52f, 0.22f, 0.15f}, {0.68f, 0.60f, 0.46f}, {0.50f, 0.50f, 0.52f}, {0.78f, 0.72f, 0.60f}, {0.38f, 0.17f, 0.12f}, {0.36f, 0.42f, 0.50f}, {0.62f, 0.48f, 0.36f}};
static const v3 PASTEL[] = {{0.92f, 0.62f, 0.60f}, {0.60f, 0.86f, 0.72f}, {0.95f, 0.85f, 0.50f}, {0.60f, 0.76f, 0.92f}, {0.93f, 0.92f, 0.88f}, {0.93f, 0.70f, 0.48f}};
static const v3 STUCCO[] = {{0.92f, 0.88f, 0.78f}, {0.95f, 0.82f, 0.66f}, {0.88f, 0.74f, 0.60f}, {0.96f, 0.94f, 0.90f}, {0.85f, 0.70f, 0.52f}};
static const v3 VICTORIAN[] = {{0.45f, 0.62f, 0.75f}, {0.85f, 0.55f, 0.62f}, {0.95f, 0.85f, 0.50f}, {0.55f, 0.72f, 0.55f}, {0.70f, 0.55f, 0.80f}, {0.95f, 0.70f, 0.45f}, {0.40f, 0.45f, 0.70f}};
static const v3 HOUSEC[] = {{0.85f, 0.83f, 0.77f}, {0.66f, 0.55f, 0.42f}, {0.60f, 0.68f, 0.72f}, {0.78f, 0.72f, 0.55f}, {0.56f, 0.34f, 0.25f}, {0.72f, 0.77f, 0.66f}};
static const v3 ROOFC[] = {{0.20f, 0.20f, 0.22f}, {0.55f, 0.20f, 0.12f}, {0.30f, 0.22f, 0.15f}, {0.18f, 0.26f, 0.22f}};
static const v3 ADOBE[] = {{0.72f, 0.52f, 0.38f}, {0.80f, 0.62f, 0.45f}, {0.66f, 0.46f, 0.34f}, {0.85f, 0.72f, 0.58f}};
#define PICK(arr, h) arr[(int)((h) * (sizeof(arr) / sizeof(arr[0]))) % (int)(sizeof(arr) / sizeof(arr[0]))]

/* ---- 3D signs from the pixel font */
static float sign_width(const char *t, float px) { return ((float)strlen(t) * 6 - 1) * px; }
static void sign_text(const char *t, v3 center, v3 right, float px, v3 col, float night) {
    bool dm = detail_mode;
    detail_mode = true;
    Mat m = glow(mul(col, 0.35f), mul(col, 2.4f), PAT_PLAIN, night);
    float w = sign_width(t, px);
    int n = (int)strlen(t);
    for (int ci = 0; ci < n; ci++) {
        char c = t[ci] >= 'a' && t[ci] <= 'z' ? (char)(t[ci] - 32) : t[ci];
        const char *at = strchr(FONT_CHARS, c);
        if (!at || c == ' ') continue;
        int g = (int)(at - FONT_CHARS);
        for (int r = 0; r < 7; r++)
            for (int k = 0; k < 5; k++) {
                if (!(FONT[g][r] & (16 >> k))) continue;
                float u = (ci * 6 + k) * px + px * 0.5f - w / 2;
                v3 p = add(add(center, mul(right, u)), V(0, (3 - r) * px, 0));
                float ax = fabsf(right.x) > 0.5f ? px * 0.9f : 0.08f, az = fabsf(right.x) > 0.5f ? 0.08f : px * 0.9f;
                put(MS_CUBE, m4_box(p, V(ax, px * 0.9f, az)), &m, 0);
            }
    }
    detail_mode = dm;
}
static void sign_board(v3 center, v3 right, float w, float h, v3 col) {
    float ax = fabsf(right.x) > 0.5f ? w : 0.06f, az = fabsf(right.x) > 0.5f ? 0.06f : w;
    Mat m = mat(col, PAT_PLAIN, 0.3f, 0.5f);
    put(MS_CUBE, m4_box(center, V(ax, h, az)), &m, 0);
}

/* ---- building blocks */
static void rooftop_bits(float x0, float z0, float x1, float z1, float h, float r) {
    float cx = (x0 + x1) / 2, cz = (z0 + z1) / 2, w = x1 - x0, d = z1 - z0;
    sbox(cx - w * 0.2f, h, cz - d * 0.15f, cx + w * 0.05f, h + 1.6f, cz + d * 0.1f, &M_CONCRETE, 0, false);
    if (r > 0.5f) {
        Mat t = mat(V(0.35f, 0.22f, 0.14f), PAT_PLANK, 0, 0.8f);
        put(MS_CYL8, m4_trs(V(cx + w * 0.25f, h + 1.5f, cz + d * 0.2f), 0, 0, 0, V(3, 3, 3)), &t, 0);
        put(MS_CONE, m4_trs(V(cx + w * 0.25f, h + 4.5f, cz + d * 0.2f), 0, 0, 0, V(3.2f, 1.2f, 3.2f)), &t, 0);
        for (int k = 0; k < 4; k++)
            put(MS_CUBE, m4_box(V(cx + w * 0.25f + (k & 1 ? 1.1f : -1.1f), h + 0.75f, cz + d * 0.2f + (k & 2 ? 1.1f : -1.1f)), V(0.15f, 1.5f, 0.15f)), &M_DARKMETAL, 0);
    }
}
static void business_sign(float x0, float z0, float x1, unsigned s);
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
        put(MS_CYL8, m4_trs(V(cx, y, cz), 0, 0, 0, V(0.6f, 14, 0.6f)), &M_STEEL, 0);
        Mat red = glow(V(0.3f, 0, 0), V(6, 0.2f, 0.1f), PAT_NEON, 1);
        put(MS_SPHERE, m4_trs(V(cx, y + 14.3f, cz), 0, 0, 0, V(0.5f, 0.5f, 0.5f)), &red, r1);
    } else rooftop_bits(tx0, tz0, tx1, tz1, y, 0.2f);
    if (fmodf(z0 - GY * 0, PITCH) >= 0) business_sign(x0, z0, x1, hashu(s + 17));
}
static void midrise(float x0, float z0, float x1, float z1, float h, unsigned s, bool old) {
    float r = hash2f((int)(x0 * 3), (int)(z0 * 7), 3);
    Mat m = mat(PICK(WALLS, r), PAT_MIDRISE, 0, 0.85f);
    building(x0, z0, x1, z1, h, &m, old ? 0.34f + r * 0.3f : r);
    sbox(x0 - 0.2f, h, z0 - 0.2f, x1 + 0.2f, h + 0.6f, z1 + 0.2f, &M_CONCRETE, 0, false);
    /* cornice band */
    Mat trim = mat(mul(PICK(WALLS, r), 0.7f), PAT_PLAIN, 0, 0.7f);
    sbox(x0 - 0.35f, h - 1.2f, z0 - 0.35f, x1 + 0.35f, h - 0.8f, z1 + 0.35f, &trim, 0, false);
    rooftop_bits(x0, z0, x1, z1, h + 0.6f, old ? 0.8f : r);
    if (hashu(s * 3u + 1u) % 100 < 60) business_sign(x0, z0, x1, hashu(s * 7u + 3u));
    else if (s % 4 == 1) neon(x0 + 3, 4.6f, z0 - 0.3f, x1 - 3, 5.1f, z0 - 0.05f, NEONS[(s / 4) % 6], r);
}
static void roof(float cx, float h, float cz, float w, float d, v3 col, unsigned s, bool tile) {
    Mat rm = mat(col, PAT_ROOF, 0, 0.8f);
    if (s % 2) put(MS_GABLE, m4_trs(V(cx, h, cz), (s % 4 < 2) ? 0 : 1.5708f, 0, 0, (s % 4 < 2) ? V(w + 0.8f, 2.4f, d + 0.8f) : V(d + 0.8f, 2.4f, w + 0.8f)), &rm, tile ? 0.8f : 0.2f);
    else put(MS_PYR, m4_trs(V(cx, h, cz), 0, 0, 0, V(w + 0.8f, 2.6f, d + 0.8f)), &rm, tile ? 0.8f : 0.2f);
}
static void house(float cx, float cz, float w, float d, int floors, v3 wall, v3 roofc, unsigned s, int kind) {
    /* kind 0 wood siding / painted, 1 flat roof beach, 2 spanish stucco + tile, 3 desert adobe, 4 cabin */
    float h = floors * 2.9f + 0.3f;
    Mat m = mat(wall, kind == 4 ? PAT_PLANK : PAT_HOUSE, 0, 0.9f);
    float seed = kind == 2 || kind == 3 ? 0.55f : kind == 4 ? 0.1f : (s % 1000) / 1000.0f * 0.5f;
    building(cx - w / 2, cz - d / 2, cx + w / 2, cz + d / 2, h, &m, seed);
    if (kind == 1 || kind == 3) {
        Mat cap = kind == 3 ? mat(mul(wall, 0.9f), PAT_HOUSE, 0, 0.9f) : M_WHITE;
        sbox(cx - w / 2 - 0.15f, h, cz - d / 2 - 0.15f, cx + w / 2 + 0.15f, h + 0.5f, cz + d / 2 + 0.15f, &cap, 0.6f, false);
        if (kind == 3) for (int k = 0; k < 4; k++) put(MS_CYL8, m4_trs(V(cx - w / 2 + 1 + k * (w - 2) / 3, h - 0.6f, cz - d / 2), 0, 1.5708f, 0, V(0.25f, 0.8f, 0.25f)), &M_WOOD, 0);
    } else if (kind == 4) {
        Mat rm = mat(roofc, PAT_ROOF, 0, 0.8f);
        put(MS_GABLE, m4_trs(V(cx, h, cz), 0, 0, 0, V(w + 1.2f, 4.5f, d + 1.0f)), &rm, 0.2f);
        sbox(cx + w * 0.25f, h, cz, cx + w * 0.25f + 0.8f, h + 3.5f, cz + 0.8f, &M_ROCK, 0, false);
    } else roof(cx, h, cz, w, d, kind == 2 ? V(0.62f, 0.26f, 0.14f) : roofc, s, kind == 2);
    Mat door = mat(V(0.25f, 0.14f, 0.08f), PAT_PLAIN, 0, 0.6f);
    sbox(cx - 0.6f, 0, cz - d / 2 - 0.08f, cx + 0.6f, 2.1f, cz - d / 2, &door, 0, false);
    Mat lamp = glow(V(0.4f, 0.35f, 0.2f), V(4, 2.6f, 1.2f), PAT_PLAIN, 1);
    sbox(cx + 0.9f, 2.2f, cz - d / 2 - 0.2f, cx + 1.2f, 2.5f, cz - d / 2, &lamp, 0, false);
    if (kind == 2 && s % 3 == 0) { /* arched porch */
        Mat st = mat(wall, PAT_HOUSE, 0, 0.9f);
        sbox(cx - 2.5f, 2.4f, cz - d / 2 - 1.8f, cx + 2.5f, 2.8f, cz - d / 2, &st, 0.55f, false);
        sbox(cx - 2.5f, 0, cz - d / 2 - 1.8f, cx - 2.1f, 2.4f, cz - d / 2 - 1.4f, &st, 0.55f, false);
        sbox(cx + 2.1f, 0, cz - d / 2 - 1.8f, cx + 2.5f, 2.4f, cz - d / 2 - 1.4f, &st, 0.55f, false);
    }
}
static void pool(float cx, float cz, float w, float d) {
    put(MS_CUBE, m4_box(V(cx, 0.03f, cz), V(w, 0.08f, d)), &M_POND, 0);
    Mat tile = mat(V(0.85f, 0.88f, 0.9f), PAT_PLAIN, 0, 0.4f);
    sbox(cx - w / 2 - 0.5f, 0, cz - d / 2 - 0.5f, cx + w / 2 + 0.5f, 0.02f, cz + d / 2 + 0.5f, &tile, 0, false);
}
/* a San Francisco style row of narrow painted houses along the block front */
static void victorian_row(float x0, float z0, float x1, unsigned s) {
    float x = x0;
    int k = 0;
    while (x + 6 < x1) {
        unsigned hs = hashu(s + (unsigned)k * 131u);
        float w = 6.0f + (hs % 3) * 0.6f;
        if (x + w > x1) w = x1 - x;
        float h = 8.2f + (hs % 2) * 2.9f, d = 13;
        v3 col = PICK(VICTORIAN, (hs % 97) / 97.0f);
        Mat m = mat(col, PAT_HOUSE, 0, 0.85f);
        building(x, z0, x + w, z0 + d, h, &m, 0.15f);
        Mat rm = mat(V(0.22f, 0.22f, 0.25f), PAT_ROOF, 0, 0.8f);
        put(MS_GABLE, m4_trs(V(x + w / 2, h, z0 + d / 2), 0, 0, 0, V(w + 0.2f, 2.8f, d + 0.3f)), &rm, 0.2f);
        Mat trim = mat(V(0.92f, 0.92f, 0.88f), PAT_PLAIN, 0, 0.6f);
        /* bay window and white trim */
        Mat bay = {{0.05f, 0.06f, 0.07f}, PAT_PLAIN, 0.8f, 0.08f, {0.6f, 0.45f, 0.25f}, 1};
        sbox(x + w * 0.2f, 3.0f, z0 - 1.0f, x + w * 0.8f, h - 1.0f, z0, &m, 0.15f, false);
        sbox(x + w * 0.25f, 3.6f, z0 - 1.05f, x + w * 0.75f, h - 1.6f, z0 - 0.98f, &bay, 0, false);
        sbox(x + w * 0.18f, h - 1.0f, z0 - 1.1f, x + w * 0.82f, h - 0.8f, z0, &trim, 0, false);
        sbox(x + 0.05f, 0, z0 - 0.1f, x + 0.25f, h, z0, &trim, 0, false);
        sbox(x + w * 0.3f, 0, z0 - 1.4f, x + w * 0.7f, 0.9f, z0, &M_CONCRETE, 0, false); /* stoop */
        x += w + 0.05f;
        k++;
    }
}
static void spanish_mission(float x0, float z0, float x1, float z1, unsigned s, float h) {
    v3 wall = PICK(STUCCO, (s % 89) / 89.0f);
    Mat m = mat(wall, PAT_HOUSE, 0, 0.85f);
    building(x0, z0, x1, z1, h, &m, 0.55f);
    Mat rm = mat(V(0.62f, 0.26f, 0.14f), PAT_ROOF, 0, 0.75f);
    put(MS_GABLE, m4_trs(V((x0 + x1) / 2, h, (z0 + z1) / 2), 1.5708f, 0, 0, V(z1 - z0 + 1, 2.6f, x1 - x0 + 1)), &rm, 0.8f);
    /* arcade along the front */
    for (float x = x0 + 1; x < x1 - 1; x += 3.5f) sbox(x, 0, z0 - 2.4f, x + 0.6f, 3.0f, z0 - 1.8f, &m, 0.55f, false);
    sbox(x0, 3.0f, z0 - 2.5f, x1, 3.6f, z0, &m, 0.55f, false);
    put(MS_WEDGE, m4_trs(V((x0 + x1) / 2, 3.6f, z0 - 1.25f), 3.14159f, 0, 0, V(x1 - x0, 0.8f, 2.6f)), &rm, 0.8f);
}
static void warehouse(float x0, float z0, float x1, float z1, float h, unsigned s) {
    Mat m = mat(s % 2 ? V(0.48f, 0.50f, 0.52f) : V(0.55f, 0.34f, 0.25f), PAT_RIBS, 0.5f, 0.55f);
    building(x0, z0, x1, z1, h, &m, 0);
    Mat roofm = mat(V(0.3f, 0.3f, 0.32f), PAT_RIBS, 0.6f, 0.5f);
    put(MS_GABLE, m4_trs(V((x0 + x1) / 2, h, (z0 + z1) / 2), 1.5708f, 0, 0, V(z1 - z0 + 0.4f, 2.0f, x1 - x0 + 0.4f)), &roofm, 0);
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
    Mat y = mat(V(0.78f, 0.55f, 0.08f), PAT_PLAIN, 0.6f, 0.45f);
    for (int k = 0; k < 4; k++) {
        float lx = x + (k & 1 ? 5 : -5), lz = z + (k & 2 ? 5 : -5);
        sbox(lx - 0.5f, 0, lz - 0.5f, lx + 0.5f, 28, lz + 0.5f, &y, 0, true);
    }
    sbox(x - 6, 28, z - 6, x + 6, 32, z + 6, &y, 0, false);
    v3 f = fwd_of(yaw);
    put(MS_CUBE, m4_trs(add(V(x, 33, z), mul(f, 22)), yaw, 0, 0, V(2.4f, 2.4f, 70)), &y, 0);
    sbox(x - 3, 32, z - 3, x + 3, 37, z + 3, &M_WHITE, 0, false);
}
static void ship(float x0, float z0, float x1, float z1, bool carrier) {
    Mat hull = mat(carrier ? V(0.35f, 0.37f, 0.40f) : V(0.45f, 0.08f, 0.06f), PAT_PLAIN, 0.4f, 0.5f), dark = mat(V(0.06f, 0.06f, 0.07f), PAT_PLAIN, 0.4f, 0.5f);
    float w = x1 - x0;
    sbox(x0, -7, z0, x1, 1, z1, &dark, 0, true);
    sbox(x0, 1, z0, x1, carrier ? 9 : 6, z1, &hull, 0, true);
    if (carrier) {
        Mat deck = mat(V(0.22f, 0.23f, 0.25f), PAT_PLAIN, 0.3f, 0.7f);
        sbox(x0 - 3, 9, z0 - 2, x1 + 3, 10, z1 + 2, &deck, 0, true);
        Mat isl = mat(V(0.40f, 0.42f, 0.45f), PAT_MIDRISE, 0.3f, 0.5f);
        sbox(x1 - 6, 10, (z0 + z1) / 2 - 12, x1, 24, (z0 + z1) / 2 + 6, &isl, 0.2f, true);
        put(MS_CYL8, m4_trs(V(x1 - 3, 24, (z0 + z1) / 2 - 2), 0, 0, 0, V(0.5f, 10, 0.5f)), &M_STEEL, 0);
    } else {
        Mat bridge = mat(V(0.85f, 0.85f, 0.82f), PAT_MIDRISE, 0, 0.6f);
        sbox(x0 + 2, 6, z1 - 26, x1 - 2, 20, z1 - 8, &bridge, 0.2f, false);
        containers(x0 + 2, z0 + 6, x0 + w - 2, z1 - 30, 9);
    }
}

/* ---- shops, hotels and restaurants */
enum { SHOP_GAS, SHOP_STORE, SHOP_DINER, SHOP_CLOTHES, SHOP_DEALER, SHOP_AUTO, SHOP_HOTEL, SHOP_FOOD, SHOP_N };
static const char *SHOP_KIND[SHOP_N] = {"GAS STATION", "24/7 STORE", "DINER", "CLOTHING", "CAR DEALER", "AUTO SHOP", "HOTEL", "RESTAURANT"};
static const float SHOP_OPEN[SHOP_N] = {0, 0, 6, 9, 9, 8, 0, 11}, SHOP_CLOSE[SHOP_N] = {24, 24, 23, 21, 20, 20, 24, 23};
static const v3 SHOP_COL[SHOP_N] = {{0.2f, 0.9f, 1.0f}, {0.4f, 1.0f, 0.4f}, {1.0f, 0.3f, 0.35f}, {1.0f, 0.5f, 0.9f}, {1.0f, 0.85f, 0.3f}, {1.0f, 0.55f, 0.15f}, {0.75f, 0.6f, 1.0f}, {1.0f, 0.6f, 0.3f}};
static const char SHOP_LETTER[SHOP_N] = {'G', 'M', 'D', 'C', 'V', 'A', 'H', 'R'};
typedef struct { int type, city, i, j; v3 door, spawn; char name[28]; bool drive_in; int menu; float stars; } Shop;
#define MAXSHOP 160
static Shop shops[MAXSHOP];
static int nshop;
static bool block_taken(int c, int i, int j) {
    for (int k = 0; k < nshop; k++) if (shops[k].city == c && shops[k].i == i && shops[k].j == j) return true;
    for (int k = 0; k < npoi; k++) if (poi[k].city == c && poi[k].i == i && poi[k].j == j) return true;
    return false;
}
/* restaurant menus */
enum { MENU_MEXICAN, MENU_SEAFOOD, MENU_CHINESE, MENU_ITALIAN, MENU_STEAK, MENU_BURGER, MENU_KOREAN, MENU_CAFE, MENU_BBQ, MENU_FINE };
static int menu_for(const char *name) {
    if (strstr(name, "TAQUER") || strstr(name, "TACO") || strstr(name, "CANTINA")) return MENU_MEXICAN;
    if (strstr(name, "FISH") || strstr(name, "CRAB") || strstr(name, "CLAM") || strstr(name, "OCEAN") || strstr(name, "OYSTER")) return MENU_SEAFOOD;
    if (strstr(name, "DRAGON") || strstr(name, "DUMPLING") || strstr(name, "NOODLE")) return MENU_CHINESE;
    if (strstr(name, "PIZZA") || strstr(name, "TRATTORIA")) return MENU_ITALIAN;
    if (strstr(name, "STEAK") || strstr(name, "BASQUE") || strstr(name, "TRI-TIP")) return MENU_STEAK;
    if (strstr(name, "BURGER") || strstr(name, "SURF")) return MENU_BURGER;
    if (strstr(name, "SEOUL") || strstr(name, "BBQ")) return strstr(name, "SEOUL") ? MENU_KOREAN : MENU_BBQ;
    if (strstr(name, "BISTRO") || strstr(name, "VINEYARD")) return MENU_FINE;
    return MENU_CAFE;
}
static Shop *add_shop(int type, int c, int i, int j, const char *name) {
    if (nshop >= MAXSHOP || block_taken(c, i, j)) return NULL;
    Shop *S = &shops[nshop++];
    memset(S, 0, sizeof(*S));
    S->type = type; S->city = c; S->i = i; S->j = j;
    snprintf(S->name, sizeof(S->name), "%s", name);
    City *C = &cities[c];
    float x0 = C->x0 + i * PITCH + 9, z0 = C->z0 + j * PITCH + 9, x1 = C->x0 + (i + 1) * PITCH - 9;
    float cx = (x0 + x1) / 2;
    S->drive_in = type == SHOP_GAS || type == SHOP_AUTO;
    S->door = type == SHOP_GAS ? V(cx, C->gy, z0 + 12) : type == SHOP_AUTO ? V(cx, C->gy, z0 + 8) : V(cx, C->gy, C->z0 + j * PITCH + WALK);
    S->spawn = V(cx + 8, C->gy, z0 + 8);
    S->menu = menu_for(name);
    S->stars = 3 + (hashu((unsigned)nshop * 77u) % 3);
    return S;
}
static int shop_at_block(int c, int i, int j) { for (int k = 0; k < nshop; k++) if (shops[k].city == c && shops[k].i == i && shops[k].j == j) return k; return -1; }
static bool shop_open(int type, float tod) { return tod >= SHOP_OPEN[type] && tod < SHOP_CLOSE[type]; }

static void storefront(float x0, float z0, float x1, float h, v3 wall, const char *name, v3 col) {
    float cx = (x0 + x1) / 2;
    Mat glass = {{0.04f, 0.05f, 0.06f}, PAT_PLAIN, 0.85f, 0.05f, {0.30f, 0.22f, 0.13f}, 0.75f};
    sbox(x0 + 2, 0.25f, z0 - 0.08f, x1 - 2, 3.3f, z0 + 0.02f, &glass, 0, false);
    Mat frame = mat(mul(wall, 0.5f), PAT_PLAIN, 0.4f, 0.5f);
    for (float x = x0 + 2; x <= x1 - 2 + 0.01f; x += (x1 - x0 - 4) / 4) sbox(x - 0.08f, 0.25f, z0 - 0.12f, x + 0.08f, 3.3f, z0, &frame, 0, false);
    Mat aw = mat(mul(col, 0.55f), PAT_RIBS, 0, 0.7f);
    put(MS_WEDGE, m4_trs(V(cx, 3.45f, z0 - 0.9f), 3.14159f, 0, 0, V(x1 - x0 - 2, 0.7f, 1.8f)), &aw, 0);
    float px = fminf(0.22f, (x1 - x0 - 3) / ((float)strlen(name) * 6));
    sign_board(V(cx, h - 1.3f, z0 - 0.02f), V(-1, 0, 0), sign_width(name, px) + 0.8f, 7 * px + 0.5f, V(0.06f, 0.06f, 0.07f));
    sign_text(name, V(cx, h - 1.3f, z0 - 0.09f), V(-1, 0, 0), px, col, 0.6f);
    add_slight(V(cx, 3.0f, z0 - 1.5f), mul(col, 1.2f), 10);
}
static void gas_station(float x0, float z0, float x1, float z1, const char *name, v3 col) {
    float cx = (x0 + x1) / 2;
    Mat white = mat(V(0.86f, 0.87f, 0.89f), PAT_PLAIN, 0.3f, 0.4f);
    Mat lit = glow(V(0.9f, 0.9f, 0.9f), V(3.2f, 3.2f, 3.0f), PAT_PLAIN, 0.6f);
    for (int a = -1; a <= 1; a += 2)
        for (int b = 0; b < 2; b++) sbox(cx + a * 10 - 0.3f, 0, z0 + 6 + b * 12 - 0.3f, cx + a * 10 + 0.3f, 5.4f, z0 + 6 + b * 12 + 0.3f, &white, 0, true);
    sbox(cx - 13, 5.4f, z0 + 3, cx + 13, 6.4f, z0 + 21, &white, 0, false);
    sbox(cx - 12, 5.3f, z0 + 4, cx + 12, 5.4f, z0 + 20, &lit, 0, false);
    Mat band = mat(mul(col, 0.6f), PAT_PLAIN, 0.3f, 0.5f);
    sbox(cx - 13.05f, 5.5f, z0 + 2.95f, cx + 13.05f, 6.3f, z0 + 3.0f, &band, 0, false);
    sign_text(name, V(cx, 5.9f, z0 + 2.9f), V(-1, 0, 0), 0.1f, col, 0.5f);
    Mat pump = mat(mul(col, 0.7f), PAT_PLAIN, 0.4f, 0.4f);
    for (int a = -1; a <= 1; a += 2) {
        sbox(cx + a * 4.5f - 0.6f, 0, z0 + 7, cx + a * 4.5f + 0.6f, 0.25f, z0 + 17, &M_CONCRETE, 0, false);
        for (int b = 0; b < 2; b++) sbox(cx + a * 4.5f - 0.35f, 0.25f, z0 + 9 + b * 6 - 0.25f, cx + a * 4.5f + 0.35f, 1.9f, z0 + 9 + b * 6 + 0.25f, &pump, 0, true);
    }
    for (int q = 0; q < 4; q++) add_slight(V(cx + (q & 1 ? 6 : -6), 5, z0 + (q & 2 ? 16 : 8)), V(2.6f, 2.6f, 2.4f), 14);
    Mat kiosk = mat(V(0.75f, 0.74f, 0.70f), PAT_MIDRISE, 0, 0.7f);
    building(x0 + 8, z1 - 15, x1 - 8, z1 - 2, 4.8f, &kiosk, 0.3f);
    storefront(x0 + 8, z1 - 15, x1 - 8, 4.8f, V(0.75f, 0.74f, 0.70f), "SNACKS", col);
    sbox(x0 + 1, 0, z0 + 1, x0 + 2.4f, 6.5f, z0 + 2, &white, 0, true);
    sign_text("GAS", V(x0 + 1.7f, 5.4f, z0 + 0.92f), V(-1, 0, 0), 0.15f, col, 0.5f);
    sign_text("1.75", V(x0 + 1.7f, 3.8f, z0 + 0.92f), V(-1, 0, 0), 0.08f, V(1, 0.6f, 0.1f), 0.5f);
}
/* hotels: a tower in big cities, a classic motel elsewhere */
static void hotel(float x0, float z0, float x1, float z1, const char *name, v3 col, bool tower, unsigned s) {
    float cx = (x0 + x1) / 2;
    if (tower) {
        Mat podium = mat(V(0.78f, 0.74f, 0.66f), PAT_MIDRISE, 0, 0.7f);
        building(x0 + 1, z0 + 1, x1 - 1, z1 - 1, 9, &podium, 0.2f);
        Mat t = mat(PICK(TOWER_FRAME, (s % 50) / 50.0f), PAT_TOWER, 0.6f, 0.3f);
        float h = 70 + (s % 50);
        sbox(x0 + 6, 9, z0 + 6, x1 - 6, h, z1 - 6, &t, 0.7f, true);
        float px = fminf(0.5f, (x1 - x0 - 14) / ((float)strlen(name) * 6));
        sign_text(name, V(cx, h - 3, z0 + 5.85f), V(-1, 0, 0), px, col, 0.4f);
        /* canopy at the entrance */
        sbox(cx - 7, 3.6f, z0 - 3, cx + 7, 4.0f, z0 + 1, &M_STEEL, 0, false);
        Mat lit = glow(V(0.8f, 0.7f, 0.5f), V(3, 2.4f, 1.5f), PAT_PLAIN, 0.7f);
        sbox(cx - 6.5f, 3.5f, z0 - 2.5f, cx + 6.5f, 3.6f, z0 + 0.5f, &lit, 0, false);
        storefront(x0 + 1, z0 + 1, x1 - 1, 8, V(0.78f, 0.74f, 0.66f), "LOBBY", col);
    } else {
        /* two-storey motel: rooms along the back, office at the front, the neon sign on a pole */
        Mat wall = mat(PICK(PASTEL, (s % 61) / 61.0f), PAT_HOUSE, 0, 0.85f);
        building(x0 + 2, z1 - 14, x1 - 2, z1 - 2, 6.2f, &wall, 0.55f);
        sbox(x0 + 2, 3.0f, z1 - 16.5f, x1 - 2, 3.3f, z1 - 14, &M_CONCRETE, 0, false);
        Mat rail = mat(V(0.85f, 0.85f, 0.85f), PAT_PLAIN, 0.5f, 0.4f);
        sbox(x0 + 2, 3.3f, z1 - 16.5f, x1 - 2, 4.3f, z1 - 16.4f, &rail, 0, false);
        Mat door = mat(V(0.15f, 0.2f, 0.35f), PAT_PLAIN, 0.2f, 0.5f);
        Mat win = glow(V(0.05f, 0.05f, 0.05f), V(1.0f, 0.7f, 0.4f), PAT_PLAIN, 1);
        for (float x = x0 + 4; x < x1 - 4; x += 4.2f)
            for (int fl = 0; fl < 2; fl++) {
                sbox(x, fl * 3.1f, z1 - 14.06f, x + 1.0f, fl * 3.1f + 2.1f, z1 - 14.0f, &door, 0, false);
                if (hashu((unsigned)(x * 3) + (unsigned)fl + s) % 3) sbox(x + 1.6f, fl * 3.1f + 1.0f, z1 - 14.06f, x + 3.2f, fl * 3.1f + 2.0f, z1 - 14.0f, &win, 0, false);
            }
        Mat office = mat(PICK(PASTEL, (s % 37) / 37.0f), PAT_HOUSE, 0, 0.85f);
        building(x0 + 4, z0 + 3, x0 + 16, z0 + 11, 3.6f, &office, 0.55f);
        storefront(x0 + 3, z0 + 3, x0 + 17, 5.4f, V(0.9f, 0.9f, 0.85f), "OFFICE", col);
        sbox(x1 - 6, 0, z0 + 1, x1 - 5.4f, 9, z0 + 1.6f, &M_STEEL, 0, true);
        float px = 0.16f;
        float w = sign_width(name, px) + 1.0f;
        sign_board(V(x1 - 5.7f - w / 2 + 2, 9.6f, z0 + 1.3f), V(-1, 0, 0), w, 1.9f, V(0.08f, 0.1f, 0.2f));
        sign_text(name, V(x1 - 5.7f - w / 2 + 2, 9.6f, z0 + 1.2f), V(-1, 0, 0), px, col, 0.3f);
        Mat vac = glow(V(0.3f, 0, 0), V(5, 0.4f, 0.3f), PAT_NEON, 0.6f);
        sbox(x1 - 7.5f, 7.6f, z0 + 1.2f, x1 - 3.9f, 8.3f, z0 + 1.3f, &vac, 0.4f, false);
        pool(x0 + 30, z0 + 9, 9, 5);
        tree(x0 + 22, z0 + 3, 1, 2, s);
    }
}
static void restaurant(float x0, float z0, float x1, float z1, const char *name, v3 col, int menu, unsigned s) {
    float cx = (x0 + x1) / 2;
    v3 wall = menu == MENU_MEXICAN ? V(0.92f, 0.62f, 0.38f) : menu == MENU_SEAFOOD ? V(0.55f, 0.70f, 0.82f) : menu == MENU_CHINESE ? V(0.70f, 0.15f, 0.12f)
            : menu == MENU_ITALIAN ? V(0.85f, 0.78f, 0.62f) : menu == MENU_FINE ? V(0.62f, 0.58f, 0.50f) : menu == MENU_BURGER ? V(0.95f, 0.92f, 0.85f) : V(0.70f, 0.55f, 0.40f);
    Mat m = mat(wall, menu == MENU_FINE ? PAT_MIDRISE : PAT_HOUSE, 0, 0.85f);
    float h = 5.2f;
    building(x0 + 2, z0 + 2, x1 - 2, z0 + 22, h, &m, menu == MENU_MEXICAN ? 0.55f : 0.3f);
    if (menu == MENU_CHINESE) {
        Mat rm = mat(V(0.12f, 0.35f, 0.25f), PAT_ROOF, 0, 0.7f);
        put(MS_PYR, m4_trs(V(cx, h, z0 + 12), 0, 0, 0, V(x1 - x0, 2.8f, 22)), &rm, 0.8f);
        Mat lan = glow(V(0.5f, 0.05f, 0.05f), V(5, 0.5f, 0.2f), PAT_NEON, 0.6f);
        for (int k = 0; k < 5; k++) put(MS_SPHERE, m4_trs(V(x0 + 4 + k * (x1 - x0 - 8) / 4, 3.2f, z0 + 0.6f), 0, 0, 0, V(0.35f, 0.42f, 0.35f)), &lan, k * 0.2f);
    } else if (menu == MENU_MEXICAN) {
        Mat rm = mat(V(0.62f, 0.26f, 0.14f), PAT_ROOF, 0, 0.75f);
        put(MS_WEDGE, m4_trs(V(cx, h, z0 + 2.5f), 3.14159f, 0, 0, V(x1 - x0 - 2, 1.2f, 3)), &rm, 0.8f);
    }
    storefront(x0 + 2, z0 + 2, x1 - 2, h + 1.8f, wall, name, col);
    /* patio with tables and umbrellas */
    for (int k = 0; k < 3; k++) {
        float tx = x0 + 8 + k * 9, tz = z0 + 27;
        Mat tm = mat(V(0.8f, 0.8f, 0.78f), PAT_PLAIN, 0.4f, 0.4f);
        put(MS_CYL8, m4_trs(V(tx, 0, tz), 0, 0, 0, V(1.4f, 0.8f, 1.4f)), &tm, 0);
        put(MS_CYL8, m4_trs(V(tx, 0, tz), 0, 0, 0, V(0.08f, 2.4f, 0.08f)), &M_STEEL, 0);
        Mat um = mat(mul(col, 0.7f), PAT_PLAIN, 0, 0.7f);
        put(MS_CONE, m4_trs(V(tx, 2.0f, tz), 0, 0, 0, V(3.2f, 0.7f, 3.2f)), &um, 0);
        Mat bulb = glow(V(0.5f, 0.4f, 0.2f), V(2, 1.5f, 0.6f), PAT_PLAIN, 1);
        put(MS_SPHERE, m4_trs(V(tx, 2.1f, tz), 0, 0, 0, V(0.15f, 0.15f, 0.15f)), &bulb, 0);
        add_box(V(tx - 0.7f, 0, tz - 0.7f), V(tx + 0.7f, 0.8f, tz + 0.7f));
    }
    add_slight(V(cx, 2.5f, z0 + 27), V(2.2f, 1.6f, 0.8f), 14);
    if (s % 2) tree(x1 - 4, z0 + 30, 1, menu == MENU_MEXICAN || menu == MENU_SEAFOOD ? 2 : 0, s);
}
static void build_shop(int k, float x0, float z0, float x1, float z1) {
    Shop *S = &shops[k];
    float cx = (x0 + x1) / 2, cz = (z0 + z1) / 2;
    v3 col = SHOP_COL[S->type];
    unsigned s = hashu((unsigned)k * 991u);
    City *C = &cities[S->city];
    switch (S->type) {
    case SHOP_GAS: gas_station(x0, z0, x1, z1, S->name, col); break;
    case SHOP_AUTO: {
        Mat m = mat(V(0.35f, 0.36f, 0.38f), PAT_RIBS, 0.5f, 0.5f);
        building(x0 + 3, z0 + 13, x1 - 3, z1 - 2, 8, &m, 0);
        Mat door = mat(V(0.15f, 0.15f, 0.16f), PAT_RIBS, 0.6f, 0.4f);
        for (int a = -1; a <= 1; a += 2) sbox(cx + a * 9 - 4, 0, z0 + 12.9f, cx + a * 9 + 4, 5, z0 + 13, &door, 0, false);
        sign_board(V(cx, 6.6f, z0 + 12.95f), V(-1, 0, 0), sign_width(S->name, 0.16f) + 1, 1.7f, V(0.05f, 0.05f, 0.05f));
        sign_text(S->name, V(cx, 6.6f, z0 + 12.88f), V(-1, 0, 0), 0.16f, col, 0.5f);
        add_slight(V(cx, 6, z0 + 9), V(2.6f, 1.8f, 1.0f), 16);
        break;
    }
    case SHOP_DEALER: {
        Mat glassb = mat(V(0.75f, 0.76f, 0.78f), PAT_TOWER, 0.6f, 0.2f);
        building(x0 + 3, z0 + 17, x1 - 3, z1 - 3, 8, &glassb, 0.8f);
        sign_board(V(cx, 7.0f, z0 + 16.95f), V(-1, 0, 0), sign_width(S->name, 0.18f) + 1, 1.8f, V(0.06f, 0.06f, 0.07f));
        sign_text(S->name, V(cx, 7.0f, z0 + 16.88f), V(-1, 0, 0), 0.18f, col, 0.5f);
        for (int f = 0; f < 6; f++) {
            float x = x0 + 2 + f * 8.4f;
            put(MS_CYL8, m4_trs(V(x, 0, z0 + 1), 0, 0, 0, V(0.12f, 6, 0.12f)), &M_STEEL, 0);
            Mat flag = mat(NEONS[f % 6], PAT_PLAIN, 0, 0.7f);
            put(MS_CUBE, m4_box(V(x + 0.5f, 5.3f, z0 + 1), V(1.0f, 1.2f, 0.03f)), &flag, 0);
        }
        add_slight(V(cx, 5, z0 + 10), V(2.8f, 2.6f, 2.2f), 18);
        break;
    }
    case SHOP_HOTEL: hotel(x0, z0, x1, z1, S->name, col, S->city == C_LA || S->city == C_SF || S->city == C_SD || S->city == C_SAC, s); break;
    case SHOP_FOOD: restaurant(x0, z0, x1, z1, S->name, col, S->menu, s); break;
    default: {
        v3 wall = S->type == SHOP_DINER ? V(0.80f, 0.80f, 0.82f) : S->type == SHOP_CLOTHES ? V(0.25f, 0.22f, 0.25f) : V(0.62f, 0.58f, 0.50f);
        Mat m = S->type == SHOP_DINER ? mat(wall, PAT_PLAIN, 0.9f, 0.25f) : mat(wall, PAT_MIDRISE, 0, 0.8f);
        float h = S->type == SHOP_DINER ? 5.0f : 6.5f;
        building(x0 + 2, z0 + 2, x1 - 2, cz - 1, h, &m, 0.2f);
        if (S->type == SHOP_DINER) {
            Mat stripe = mat(V(0.7f, 0.08f, 0.08f), PAT_PLAIN, 0.2f, 0.4f);
            sbox(x0 + 1.9f, 3.6f, z0 + 1.88f, x1 - 1.9f, 4.0f, cz - 0.9f, &stripe, 0, false);
        }
        storefront(x0 + 2, z0 + 2, x1 - 2, h + 1.8f, wall, S->name, col);
        midrise(x0 + 2, cz + 1, x1 - 2, z1 - 2, 9 + (s % 8), s, true);
        break;
    }
    }
    (void)C;
}

/* generic street-level business signs */
static const char *BIZ[] = {"CAFE", "BAKERY", "BOOKS", "PHARMACY", "PIZZA", "BAR", "SALON", "BANK", "FLORIST", "SUSHI", "LAUNDRY", "DELI", "RECORDS", "HARDWARE",
                            "OPTICIAN", "NOODLES", "GYM", "TAILOR", "ARCADE", "PAWN", "CINEMA", "TACOS", "BARBER", "THRIFT", "YOGA", "SURF SHOP", "BOBA", "WINE BAR"};
static void business_sign(float x0, float z0, float x1, unsigned s) {
    bool dm = detail_mode;
    detail_mode = true;
    const char *name = BIZ[s % (sizeof(BIZ) / sizeof(BIZ[0]))];
    float w = x1 - x0, px = 0.13f;
    if (sign_width(name, px) <= w - 3) {
        float cx = (x0 + x1) / 2 + ((s >> 8) % 3 - 1.0f) * (w - sign_width(name, px) - 3) * 0.3f;
        v3 col = NEONS[(s >> 4) % 6];
        sign_board(V(cx, 4.35f, z0 - 0.02f), V(-1, 0, 0), sign_width(name, px) + 0.6f, 1.3f, V(0.05f, 0.05f, 0.06f));
        sign_text(name, V(cx, 4.35f, z0 - 0.09f), V(-1, 0, 0), px, col, 0.65f);
        Mat aw = mat(mul(NEONS[(s >> 6) % 6], 0.45f), PAT_RIBS, 0, 0.7f);
        if (s % 2) put(MS_WEDGE, m4_trs(V((x0 + x1) / 2, 3.25f, z0 - 0.8f), 3.14159f, 0, 0, V(w - 3, 0.55f, 1.6f)), &aw, 0);
    }
    detail_mode = dm;
}

/* street furniture along the sidewalks */
static void furniture_item(int kind, v3 p, bool along_x, int style, int n) {
    Mat dark = mat(V(0.06f, 0.12f, 0.08f), PAT_PLAIN, 0.6f, 0.4f);
    switch (kind) {
    case 0: put(MS_CYL8, m4_trs(p, 0, 0, 0, V(0.6f, 0.95f, 0.6f)), &dark, 0); add_box(V(p.x - 0.3f, 0, p.z - 0.3f), V(p.x + 0.3f, 0.95f, p.z + 0.3f)); break;
    case 1: {
        float w = 1.9f, d = 0.55f, ax = along_x ? w : d, az = along_x ? d : w;
        put(MS_CUBE, m4_box(add(p, V(0, 0.45f, 0)), V(ax, 0.08f, az)), &M_WOOD, 0);
        put(MS_CUBE, m4_box(add(p, V(0, 0.2f, 0)), V(along_x ? 0.1f : d * 0.8f, 0.4f, along_x ? d * 0.8f : 0.1f)), &M_DARKMETAL, 0);
        add_box(V(p.x - ax / 2, 0, p.z - az / 2), V(p.x + ax / 2, 0.8f, p.z + az / 2));
        break;
    }
    case 2: {
        Mat red = mat(style == ST_SUBURB || style == ST_BEACH ? V(0.75f, 0.6f, 0.05f) : V(0.6f, 0.06f, 0.04f), PAT_PLAIN, 0.4f, 0.5f);
        put(MS_CYL8, m4_trs(p, 0, 0, 0, V(0.36f, 0.7f, 0.36f)), &red, 0);
        put(MS_SPHERE, m4_trs(add(p, V(0, 0.7f, 0)), 0, 0, 0, V(0.17f, 0.15f, 0.17f)), &red, 0);
        add_box(V(p.x - 0.2f, 0, p.z - 0.2f), V(p.x + 0.2f, 0.8f, p.z + 0.2f));
        break;
    }
    case 3: {
        float w = 3.6f, d = 1.4f, ax = along_x ? w : d, az = along_x ? d : w;
        Mat glassm = {{0.10f, 0.12f, 0.13f}, PAT_PLAIN, 0.7f, 0.05f, {0, 0, 0}, 0};
        Mat lit = glow(V(0.8f, 0.8f, 0.8f), V(2.0f, 2.0f, 1.9f), PAT_PLAIN, 1);
        put(MS_CUBE, m4_box(add(p, V(0, 2.45f, 0)), V(ax + 0.2f, 0.12f, az + 0.2f)), &M_STEEL, 0);
        put(MS_CUBE, m4_box(add(p, V(0, 2.37f, 0)), V(ax * 0.9f, 0.04f, az * 0.6f)), &lit, 0);
        put(MS_CUBE, m4_box(add(p, V(0, 1.3f, 0)), along_x ? V(w, 2.1f, 0.05f) : V(0.05f, 2.1f, w)), &glassm, 0);
        put(MS_CUBE, m4_box(add(p, V(0, 0.45f, 0)), along_x ? V(w * 0.8f, 0.08f, 0.45f) : V(0.45f, 0.08f, w * 0.8f)), &M_DARKMETAL, 0);
        Mat ad = glow(V(0.3f, 0.3f, 0.3f), mul(NEONS[n % 6], 0.9f), PAT_PLAIN, 0.8f);
        put(MS_CUBE, m4_box(add(p, along_x ? V(w / 2 + 0.05f, 1.3f, 0) : V(0, 1.3f, w / 2 + 0.05f)), along_x ? V(0.06f, 1.6f, d * 0.9f) : V(d * 0.9f, 1.6f, 0.06f)), &ad, 0);
        add_box(V(p.x - (along_x ? w / 2 : 0.1f), 0, p.z - (along_x ? 0.1f : w / 2)), V(p.x + (along_x ? w / 2 : 0.1f), 2.5f, p.z + (along_x ? 0.1f : w / 2)));
        break;
    }
    case 4: put(MS_CUBE, m4_box(add(p, V(0, 0.15f, 0)), V(1.3f, 0.3f, 1.3f)), &M_CONCRETE, 0); tree(p.x, p.z, 0.75f, style == ST_SPANISH || style == ST_BEACH || style == ST_DESERT ? 2 : 0, (unsigned)(p.x * 13 + p.z * 7)); break;
    case 5: { Mat blue = mat(V(0.08f, 0.15f, 0.45f), PAT_PLAIN, 0.4f, 0.5f); put(MS_CUBE, m4_box(add(p, V(0, 0.55f, 0)), V(0.5f, 1.1f, 0.45f)), &blue, 0); add_box(V(p.x - 0.25f, 0, p.z - 0.25f), V(p.x + 0.25f, 1.1f, p.z + 0.25f)); break; }
    default: { Mat y = mat(V(0.75f, 0.6f, 0.08f), PAT_PLAIN, 0.3f, 0.6f); put(MS_CYL8, m4_trs(p, 0, 0, 0, V(0.3f, 0.9f, 0.3f)), &y, 0); add_box(V(p.x - 0.15f, 0, p.z - 0.15f), V(p.x + 0.15f, 0.9f, p.z + 0.15f)); break; }
    }
}
static bool near_marker(v3 p, float r) {
    for (int k = 0; k < npoi; k++) if (dist2d(poi[k].pos, p) < r) return true;
    for (int k = 0; k < nshop; k++) if (dist2d(shops[k].door, p) < r) return true;
    return false;
}
static void street_furniture(int c, int i, int j, int style) {
    City *C = &cities[c];
    detail_mode = true;
    for (int sd = 0; sd < 4; sd++) {
        unsigned s = hashu((unsigned)(c * 7919 + i * 7 + j * 131 + sd * 977 + 5));
        bool along_x = sd < 2;
        float off = 8.25f;
        for (float t = 13; t < PITCH - 13; t += 7.5f + (s % 5)) {
            s = hashu(s);
            float bx = C->x0 + i * PITCH, bz = C->z0 + j * PITCH;
            v3 p = sd == 0 ? V(bx + t, 0, bz + off) : sd == 1 ? V(bx + t, 0, bz + PITCH - off) : sd == 2 ? V(bx + off, 0, bz + t) : V(bx + PITCH - off, 0, bz + t);
            if (near_marker(add(p, V(0, C->gy, 0)), 5)) continue;
            unsigned r = s % 100;
            int kind;
            if (style == ST_INDUSTRIAL) kind = r < 40 ? 6 : r < 60 ? 2 : r < 75 ? 0 : -1;
            else if (style == ST_SUBURB || style == ST_BEACH || style == ST_MANSION || style == ST_VICTORIAN || style == ST_MOUNTAIN) kind = r < 45 ? 4 : r < 60 ? 2 : r < 70 ? 5 : -1;
            else if (style == ST_PARK) kind = r < 50 ? 1 : r < 70 ? 0 : -1;
            else if (style == ST_DESERT) kind = r < 30 ? 4 : r < 45 ? 2 : -1;
            else kind = r < 22 ? 0 : r < 40 ? 1 : r < 52 ? 2 : r < 72 ? 4 : r < 80 ? 5 : r < 85 ? 3 : -1;
            if (kind >= 0) furniture_item(kind, p, along_x, style, (int)s);
        }
    }
    detail_mode = false;
}

/* ---- one city block */
static void gen_block(int c, int i, int j) {
    City *C = &cities[c];
    float x0 = C->x0 + i * PITCH + 9, z0 = C->z0 + j * PITCH + 9, x1 = C->x0 + (i + 1) * PITCH - 9, z1 = C->z0 + (j + 1) * PITCH - 9;
    float cx = (x0 + x1) / 2, cz = (z0 + z1) / 2;
    int d = block_dist(c, i, j);
    int style = dstyle[d];
    unsigned s = hashu((unsigned)(c * 100003 + i * 131 + j * 977 + 12345));
    float r = (s % 10000) / 10000.0f;
    int lot = 0; /* 0 plaza, 1 grass, 2 asphalt, 3 dirt, 4 park, 5 sand */
    switch (style) {
    case ST_SUBURB: case ST_MANSION: case ST_VICTORIAN: case ST_MOUNTAIN: lot = 1; break;
    case ST_PARK: lot = 4; break;
    case ST_INDUSTRIAL: lot = 3; break;
    case ST_DESERT: lot = 5; break;
    case ST_BEACH: lot = (i == 0 && c != C_SCZ) ? 5 : 1; break;
    default: lot = 0;
    }
    int shop = shop_at_block(c, i, j), ptype = poi_block(c, i, j);
    if (shop >= 0) lot = shops[shop].type == SHOP_GAS || shops[shop].type == SHOP_AUTO || shops[shop].type == SHOP_DEALER ? 2 : lot == 5 ? 5 : 0;
    /* the sidewalk + lot slab (raised kerb) */
    {
        Mat slab = {{0.40f, 0.39f, 0.37f}, PAT_SIDEWALK, 0, 0.8f, {0, 0, 0}, 0};
        detail_mode = false;
        put(MS_CUBE, m4_box(V((x0 + x1) / 2, 0.075f, (z0 + z1) / 2), V(PITCH - 10, 0.15f, PITCH - 10)), &slab, (float)lot + 0.5f);
        add_box(V(x0 - 4, 0, z0 - 4), V(x1 + 4, 0.15f, z1 + 4));
    }
    float B = 0.15f; /* everything in the block stands on the slab */
    GY += B;
    if (shop >= 0) { build_shop(shop, x0, z0, x1, z1); GY -= B; street_furniture(c, i, j, style); return; }
    if (ptype >= 0 && ptype != POI_JOB) {
        Mat m;
        float h = 14;
        if (ptype == POI_POLICE) m = mat(V(0.24f, 0.30f, 0.45f), PAT_MIDRISE, 0, 0.8f);
        else if (ptype == POI_HOSPITAL) m = mat(V(0.86f, 0.86f, 0.84f), PAT_MIDRISE, 0, 0.7f), h = 24;
        else if (ptype == POI_SAFE) m = mat(V(0.60f, 0.42f, 0.34f), PAT_MIDRISE, 0, 0.8f), h = 18;
        else m = mat(V(0.30f, 0.30f, 0.32f), PAT_RIBS, 0.5f, 0.5f), h = 7;
        if (ptype == POI_RESPRAY) {
            building(x0 + 4, z0 + 14, x1 - 4, z1 - 2, h, &m, 0);
            neon(x0 + 8, 5.5f, z0 + 13.6f, x1 - 8, 6.3f, z0 + 13.9f, V(1.0f, 0.45f, 0.05f), 0.5f);
            sign_text("PAINT AND BODY", V(cx, 6.0f, z0 + 13.5f), V(-1, 0, 0), 0.12f, V(1, 0.6f, 0.2f), 0.5f);
        } else {
            building(x0 + 2, z0 + 10, x1 - 2, z1 - 2, h, &m, r);
            rooftop_bits(x0 + 2, z0 + 10, x1 - 2, z1 - 2, h, 0.1f);
        }
        if (ptype == POI_POLICE) { neon(x0 + 6, 9, z0 + 9.6f, x1 - 6, 10, z0 + 9.9f, V(0.15f, 0.35f, 1.0f), 0.2f); sign_text("POLICE", V(cx, 7.3f, z0 + 9.85f), V(-1, 0, 0), 0.2f, V(0.8f, 0.9f, 1), 0.4f); }
        if (ptype == POI_HOSPITAL) {
            neon(cx - 3, 17.2f, z0 + 9.6f, cx + 3, 18.8f, z0 + 9.9f, V(1.0f, 0.1f, 0.1f), 0.1f);
            neon(cx - 0.8f, 15, z0 + 9.6f, cx + 0.8f, 21, z0 + 9.9f, V(1.0f, 0.1f, 0.1f), 0.1f);
            sign_text("HOSPITAL", V(cx, 12.3f, z0 + 9.85f), V(-1, 0, 0), 0.18f, V(1, 1, 1), 0.4f);
        }
        if (ptype == POI_SAFE) { neon(cx - 5, 4.5f, z0 + 9.6f, cx + 5, 5.2f, z0 + 9.9f, V(0.3f, 1.0f, 0.5f), 0.9f); sign_text("APARTMENTS", V(cx, 6.4f, z0 + 9.85f), V(-1, 0, 0), 0.12f, V(0.5f, 1, 0.6f), 0.5f); }
        GY -= B;
        street_furniture(c, i, j, style);
        return;
    }
    switch (style) {
    case ST_DOWNTOWN: {
        float hmax = c == C_LA ? 210 : c == C_SF ? 190 : 140;
        if (c == C_SF && i == 10 && j == 2) { /* the pyramid */
            Mat g = mat(V(0.85f, 0.85f, 0.82f), PAT_TOWER, 0.4f, 0.3f);
            put(MS_PYR, m4_trs(V(cx, 0, cz), 0, 0, 0, V(42, 230, 42)), &g, 0.3f);
            add_box(V(cx - 18, 0, cz - 18), V(cx + 18, 120, cz + 18));
            Mat red = glow(V(0.3f, 0, 0), V(6, 0.2f, 0.1f), PAT_NEON, 1);
            put(MS_SPHERE, m4_trs(V(cx, 231, cz), 0, 0, 0, V(0.7f, 0.7f, 0.7f)), &red, 0.2f);
        } else if (r < 0.1f) {
            put(MS_CYL, m4_trs(V(cx, 0, cz), 0, 0, 0, V(12, 0.8f, 12)), &M_CONCRETE, 0);
            put(MS_CYL, m4_trs(V(cx, 0.1f, cz), 0, 0, 0, V(11, 0.75f, 11)), &M_POND, 0);
            add_box(V(cx - 6, 0, cz - 6), V(cx + 6, 0.9f, cz + 6));
            for (int k = 0; k < 6; k++) tree(cx + cosf(k * 1.047f) * 16, cz + sinf(k * 1.047f) * 16, 0.9f, c == C_LA || c == C_SD ? 2 : 0, s + (unsigned)k);
        } else if (r < 0.55f) tower(x0, z0, x1, z1, hmax * (0.45f + 0.55f * hash2f(i, j, 2 + c)), s);
        else {
            float h1 = hmax * (0.35f + 0.6f * hash2f(i, j, 3 + c)), h2 = hmax * (0.3f + 0.5f * hash2f(i, j, 4 + c));
            if (r < 0.78f) { tower(x0, z0, cx - 1.5f, z1, h1, s); tower(cx + 1.5f, z0, x1, z1, h2, s + 1); }
            else { tower(x0, z0, x1, cz - 1.5f, h1, s); tower(x0, cz + 1.5f, x1, z1, h2, s + 1); }
        }
        break;
    }
    case ST_MIDRISE:
    case ST_OLD: {
        bool old = style == ST_OLD;
        if (!old && r < 0.12f) {
            for (int k = 0; k < 14; k++) {
                if (hash2f(i * 31 + c, j, k + 20) < 0.45f) continue;
                add_slot(x0 + 4.5f + (k % 7) * 6.0f, k < 7 ? z0 + 6 : z1 - 6, k < 7 ? 3.14159f : 0);
            }
            midrise(cx - 8, cz - 5, cx + 8, cz + 5, 4, s, false);
            break;
        }
        for (int q = 0; q < 4; q++) {
            float qx0 = q & 1 ? cx + 1 : x0, qx1 = q & 1 ? x1 : cx - 1, qz0 = q & 2 ? cz + 1 : z0, qz1 = q & 2 ? z1 : cz - 1;
            float rh = hash2f(i * 4 + q, j + c * 50, 5);
            float h = old ? 9 + rh * 12 : 12 + rh * rh * 38;
            if (rh < 0.08f) { tree((qx0 + qx1) / 2, (qz0 + qz1) / 2, 1.1f, 0, s + (unsigned)q); continue; }
            midrise(qx0, qz0, qx1, qz1, h, s + (unsigned)q, old);
        }
        break;
    }
    case ST_VICTORIAN: {
        victorian_row(x0, z0, x1, s);
        Mat back = mat(PICK(VICTORIAN, r), PAT_HOUSE, 0, 0.85f);
        if (r < 0.5f) victorian_row(x0, cz + 2, x1, s + 99);
        else building(x0 + 3, z1 - 14, x1 - 3, z1 - 1, 9, &back, 0.15f);
        tree(cx, cz - 2, 0.9f, 0, s);
        break;
    }
    case ST_SPANISH:
    case ST_SUBURB:
    case ST_BEACH:
    case ST_DESERT:
    case ST_MOUNTAIN: {
        if (style == ST_SPANISH && r < 0.2f) { spanish_mission(x0 + 3, z0 + 3, x1 - 3, cz + 4, s, 7 + r * 20); tree(x0 + 3, z1 - 4, 1, 2, s); tree(x1 - 3, z1 - 4, 1, 2, s + 1); break; }
        if (style == ST_BEACH && i == 0 && c != C_SCZ) { /* beachfront: a hotel or apartments with palms */
            Mat m = mat(PICK(PASTEL, r * 3), PAT_MIDRISE, 0, 0.8f);
            building(x0 + 4, z0 + 4, x1 - 4, z1 - 4, 14 + r * 26, &m, 0.7f);
            business_sign(x0 + 4, z0 + 4, x1 - 4, s);
            for (int k = 0; k < 4; k++) tree(k & 1 ? x1 - 1.5f : x0 + 1.5f, k & 2 ? z1 - 1.5f : z0 + 1.5f, 1.0f, 2, s + (unsigned)k);
            break;
        }
        for (int a = 0; a < 3; a++)
            for (int b = 0; b < 3; b++) {
                float hx = x0 + 7.7f + a * 15.3f, hz = z0 + 7.7f + b * 15.3f;
                unsigned hs = hashu(s + (unsigned)(a * 3 + b) * 7919u);
                if (a == 1 && b == 1) {
                    if (hs % 3 == 0 && style != ST_MOUNTAIN) pool(hx, hz, 7, 4);
                    else tree(hx, hz, 1.2f, style == ST_MOUNTAIN ? 1 : style == ST_DESERT ? 5 : style == ST_SUBURB ? (int)(hs % 2) : 2, hs);
                    continue;
                }
                float w = 8 + (hs % 30) / 10.0f, dd = 7.5f + ((hs >> 5) % 25) / 10.0f;
                int kind = style == ST_SPANISH ? 2 : style == ST_BEACH ? (hs % 3 == 0 ? 1 : 0) : style == ST_DESERT ? (hs % 3 ? 3 : 2) : style == ST_MOUNTAIN ? 4 : (hs % 4 == 0 ? 2 : 0);
                v3 wall = kind == 2 ? PICK(STUCCO, (hs % 97) / 97.0f) : kind == 3 ? PICK(ADOBE, (hs % 97) / 97.0f) : kind == 4 ? V(0.45f, 0.30f, 0.18f)
                        : style == ST_BEACH ? PICK(PASTEL, (hs % 97) / 97.0f) : PICK(HOUSEC, (hs % 97) / 97.0f);
                house(hx, hz + 0.8f, w, dd, 1 + (int)((hs >> 9) % 2), wall, kind == 4 ? V(0.20f, 0.28f, 0.20f) : PICK(ROOFC, ((hs >> 12) % 89) / 89.0f), hs, kind);
                if (b == 0 && hs % 3 != 0) add_slot(hx, z0 + 2.2f, (hs % 2) ? 1.5708f : -1.5708f);
                if (hs % 4 == 0) tree(hx + 5.6f, hz + 5.5f, 0.8f, style == ST_MOUNTAIN ? 1 : style == ST_DESERT ? 5 : style == ST_SUBURB ? 0 : 2, hs >> 3);
            }
        break;
    }
    case ST_MANSION: {
        unsigned hs = hashu(s + 7);
        v3 wall = PICK(STUCCO, (hs % 97) / 97.0f);
        house(cx - 6, cz - 4, 22, 14, 2, wall, V(0.6f, 0.26f, 0.14f), hs, 2);
        pool(cx + 4, cz + 10, 12, 5);
        Mat hedge = mat(V(0.08f, 0.2f, 0.06f), PAT_LEAF, 0, 0.9f);
        sbox(x0, 0, z0 - 0.5f, cx - 4, 1.8f, z0 + 0.5f, &hedge, 0, true);
        sbox(cx + 4, 0, z0 - 0.5f, x1, 1.8f, z0 + 0.5f, &hedge, 0, true);
        for (int k = 0; k < 6; k++) tree(x0 + 3 + k * 8, z1 - 2, 1.1f, 2, hs + (unsigned)k);
        add_slot(cx, z0 + 4, 1.5708f);
        break;
    }
    case ST_PARK: {
        for (int k = 0; k < 14; k++) {
            float tx = x0 + hash2f(i * 7 + c, j, k * 2) * 46, tz = z0 + hash2f(i * 7 + c, j, k * 2 + 1) * 46;
            if (fabsf(dist2d(V(tx, 0, tz), V(cx, 0, cz)) - 14) < 3) continue;
            tree(tx, tz, 1.0f + hash2f(i, j, k + 50) * 0.5f, c == C_SF && i < 3 ? 3 : (k % 3 == 0 ? 1 : 0), s + (unsigned)k * 13u);
        }
        if (r < 0.3f) { put(MS_CYL, m4_trs(V(cx, 0, cz), 0, 0, 0, V(18, 0.05f, 13)), &M_POND, 0); }
        else {
            Mat st = mat(V(0.45f, 0.45f, 0.48f), PAT_PLAIN, 0.2f, 0.6f);
            put(MS_CYL, m4_trs(V(cx, 0, cz), 0, 0, 0, V(5, 1, 5)), &st, 0);
            put(MS_CUBE, m4_box(V(cx, 2.3f, cz), V(1.2f, 2.6f, 1.2f)), &st, 0);
            put(MS_SPHERE, m4_trs(V(cx, 4.4f, cz), 0, 0, 0, V(0.9f, 0.9f, 0.9f)), &M_STEEL, 0);
            add_box(V(cx - 2.5f, 0, cz - 2.5f), V(cx + 2.5f, 5, cz + 2.5f));
        }
        break;
    }
    case ST_INDUSTRIAL: {
        if (r < 0.45f) warehouse(x0 + 2, z0 + 8, x1 - 2, z1 - 3, 9 + r * 8, s);
        else if (r < 0.8f) { warehouse(x0 + 2, z0 + 22, x1 - 2, z1 - 2, 8, s); containers(x0 + 2, z0 + 2, x1 - 2, z0 + 16, s); }
        else { midrise(x0 + 2, z0 + 2, cx - 2, cz - 2, 10 + r * 6, s, true); containers(cx + 1, z0 + 2, x1 - 1, z1 - 2, s); for (int k = 0; k < 4; k++) add_slot(x0 + 5 + k * 5.5f, z1 - 7, 0); }
        break;
    }
    }
    GY -= B;
    street_furniture(c, i, j, style);
}

/* ---- roadside stops beside the highways: a flat lot with a few businesses */
typedef struct { const char *name; float x, z; int kinds[3]; const char *names[3]; } StopDef;
static const StopDef STOPS[] = {
    {"THE GRAPEVINE", 4380, 6600, {SHOP_GAS, SHOP_DINER, -1}, {"GRAPEVINE FUEL", "SUMMIT DINER", 0}},
    {"KETTLEMAN CITY", 3250, 4900, {SHOP_GAS, SHOP_FOOD, SHOP_HOTEL}, {"VALLEY FUEL", "ROADSIDE BURGERS", "HALFWAY MOTEL"}},
    {"BAKER", 7000, 5900, {SHOP_GAS, SHOP_HOTEL, SHOP_DINER}, {"DESERT FUEL", "MIRAGE MOTEL", "ALIEN DINER"}},
    {"BIG SUR", 2150, 5600, {SHOP_HOTEL, SHOP_FOOD, -1}, {"CLIFFSIDE INN", "REDWOOD GRILL", 0}},
    {"SALINAS", 2350, 5200, {SHOP_GAS, SHOP_FOOD, -1}, {"SALINAS FUEL", "ARTICHOKE CAFE", 0}},
    {"MOUNT SHASTA", 1520, 950, {SHOP_HOTEL, SHOP_GAS, -1}, {"SHASTA VIEW MOTEL", "NORTHSTAR GAS", 0}},
    {"DESERT CENTER", 6800, 7850, {SHOP_GAS, SHOP_DINER, -1}, {"LAST CHANCE GAS", "CACTUS DINER", 0}},
    {"HOPLAND", 950, 2300, {SHOP_HOTEL, SHOP_DINER, -1}, {"REDWOOD MOTEL", "LOGGERS DINER", 0}},
    {"TEHACHAPI", 4800, 6350, {SHOP_GAS, -1, -1}, {"MOUNTAIN FUEL", 0, 0}},
    {"MALIBU", 4150, 7150, {SHOP_FOOD, -1, -1}, {"MALIBU FISH SHACK", 0, 0}},
};
static v3 baker_sign;
static void build_stop(const StopDef *S) {
    float d, s;
    int e = nearest_edge(V(S->x, 0, S->z), 200, &d, &s);
    if (e < 0) return;
    v3 a = nodes[edges[e].a].p, D = edges[e].d;
    v3 on = add(a, mul(D, clampf(s, 0, edges[e].len)));
    float y = lerpf(nodes[edges[e].a].p.y, nodes[edges[e].b].p.y, clampf(s / edges[e].len, 0, 1));
    v3 R = right_vec(D);
    /* pick the side away from the sea */
    v3 side = coast_sd(on.x + R.x * 60, on.z + R.z * 60) > coast_sd(on.x - R.x * 60, on.z - R.z * 60) ? R : mul(R, -1);
    int nk = 0;
    for (int k = 0; k < 3; k++) if (S->kinds[k] >= 0) nk++;
    /* an axis-aligned lot beside the highway; businesses in a row along x, entrances facing north */
    float hx = 50.0f * nk + 4, hz = 46;
    bool facing_z = true;
    float off = (fabsf(side.z) > fabsf(side.x) ? hz : hx) / 2 + 24;
    v3 c = add(on, mul(side, off));
    add_pad(c.x - hx / 2, c.z - hz / 2, c.x + hx / 2, c.z + hz / 2, y, 60);
    GY = y;
    Mat slab = {{0.40f, 0.39f, 0.37f}, PAT_SIDEWALK, 0, 0.8f, {0, 0, 0}, 0};
    put(MS_CUBE, m4_box(V(c.x, 0.05f, c.z), V(hx, 0.1f, hz)), &slab, 2.5f);
    add_box(V(c.x - hx / 2, 0, c.z - hz / 2), V(c.x + hx / 2, 0.1f, c.z + hz / 2));
    GY = y + 0.1f;
    /* businesses sit in 46 x 22 slots along the lot, entrances toward -z (north) or rotated by moving the lot */
    int n = 0;
    for (int k = 0; k < 3; k++) if (S->kinds[k] >= 0) n++;
    for (int k = 0, slot = 0; k < 3; k++) {
        if (S->kinds[k] < 0) continue;
        if (nshop >= MAXSHOP) break;
        Shop *sh = &shops[nshop++];
        memset(sh, 0, sizeof(*sh));
        sh->type = S->kinds[k]; sh->city = -1; sh->i = sh->j = -1;
        snprintf(sh->name, sizeof(sh->name), "%s", S->names[k]);
        sh->menu = menu_for(S->names[k]);
        sh->stars = 3 + (hashu((unsigned)nshop * 77u) % 3);
        float ox, oz;
        ox = c.x - hx / 2 + 2 + slot * 50.0f; oz = c.z - hz / 2 + 2;
        float x0 = ox, z0 = oz, x1 = ox + 46, z1 = oz + 42;
        (void)facing_z;
        sh->drive_in = sh->type == SHOP_GAS;
        float cx = (x0 + x1) / 2;
        sh->door = sh->type == SHOP_GAS ? V(cx, GY, z0 + 12) : V(cx, GY, z0 - 2);
        sh->spawn = V(cx, GY, z0 - 6);
        if (sh->type == SHOP_GAS) gas_station(x0, z0, x1, z1, sh->name, SHOP_COL[sh->type]);
        else if (sh->type == SHOP_HOTEL) hotel(x0, z0, x1, z1, sh->name, SHOP_COL[sh->type], false, hashu((unsigned)nshop));
        else if (sh->type == SHOP_FOOD) restaurant(x0, z0, x1, z1, sh->name, SHOP_COL[sh->type], sh->menu, hashu((unsigned)nshop));
        else {
            Mat m = mat(V(0.80f, 0.80f, 0.82f), PAT_PLAIN, 0.9f, 0.25f);
            building(x0 + 2, z0 + 2, x1 - 2, z0 + 20, 5, &m, 0.2f);
            storefront(x0 + 2, z0 + 2, x1 - 2, 6.8f, V(0.8f, 0.8f, 0.82f), sh->name, SHOP_COL[sh->type]);
        }
        slot++;
    }
    if (!strcmp(S->name, "BAKER")) { /* a giant roadside thermometer */
        v3 p = add(c, V(hx / 2 - 6, 0, -hz / 2 + 4));
        Mat w = mat(V(0.9f, 0.9f, 0.88f), PAT_PLAIN, 0.2f, 0.5f);
        sbox(p.x - 2, 0, p.z - 1.5f, p.x + 2, 40, p.z + 1.5f, &w, 0, true);
        baker_sign = add(p, V(0, GY, -1.6f));
    }
    GY = 0;
}

/* ---- landmarks */
static v3 WHEEL_C, LIGHTHOUSE[3];
static v3 TURBINES[48];
static int nturbine;
static v3 PUMPJACKS[32];
static int npump;
static v3 WATERFALL;
static void bridge_structure(int hwy_first_node, int hwy_last_node, bool golden) {
    v3 a = nodes[hwy_first_node].p, b = nodes[hwy_last_node].p;
    v3 D = norm(sub(b, a));
    D.y = 0;
    D = norm(D);
    float L = dist2d(a, b), y = a.y;
    v3 R = right_vec(D);
    Mat paint = golden ? mat(V(0.72f, 0.20f, 0.08f), PAT_PLAIN, 0.4f, 0.45f) : mat(V(0.55f, 0.57f, 0.60f), PAT_PLAIN, 0.6f, 0.4f);
    /* deck girder + railings */
    for (float s = 0; s < L; s += 20) {
        v3 p = add(a, mul(D, s + 10));
        put(MS_CUBE, m4_trs(V(p.x, y - 1.4f, p.z), yaw_of(D), 0, 0, V(18, 2.6f, 20.5f)), &paint, 0);
    }
    for (int sd = -1; sd <= 1; sd += 2) {
        v3 o = mul(R, sd * 8.6f);
        for (float s = 0; s < L; s += 40) {
            v3 p = add(add(a, mul(D, s + 20)), o);
            put(MS_CUBE, m4_trs(V(p.x, y + 0.6f, p.z), yaw_of(D), 0, 0, V(0.3f, 1.2f, 40.2f)), &paint, 0);
        }
    }
    /* collision: deck boxes + railings (bridges are axis aligned) */
    bool along_z = fabsf(D.z) > fabsf(D.x);
    float x0 = fminf(a.x, b.x), x1 = fmaxf(a.x, b.x), z0 = fminf(a.z, b.z), z1 = fmaxf(a.z, b.z);
    if (along_z) {
        add_box(V(a.x - 9, y - 2.6f, z0), V(a.x + 9, y, z1));
        add_box(V(a.x - 9, y, z0 + 20), V(a.x - 8.4f, y + 1.2f, z1 - 20));
        add_box(V(a.x + 8.4f, y, z0 + 20), V(a.x + 9, y + 1.2f, z1 - 20));
    } else {
        add_box(V(x0, y - 2.6f, a.z - 9), V(x1, y, a.z + 9));
        add_box(V(x0 + 20, y, a.z - 9), V(x1 - 20, y + 1.2f, a.z - 8.4f));
        add_box(V(x0 + 20, y, a.z + 8.4f), V(x1 - 20, y + 1.2f, a.z + 9));
    }
    /* towers, main cables, suspenders */
    float tower_h = golden ? 105 : 80;
    float ts[2] = {L * 0.25f, L * 0.75f};
    v3 tops[2][2];
    for (int t = 0; t < 2; t++) {
        v3 c = add(a, mul(D, ts[t]));
        for (int sd = -1; sd <= 1; sd += 2) {
            v3 leg = add(c, mul(R, sd * 10));
            put(MS_CUBE, m4_trs(V(leg.x, (y + tower_h - 12) / 2 - 6, leg.z), yaw_of(D), 0, 0, V(3.2f, tower_h + y + 12, 4.5f)), &paint, 0);
            tops[t][sd > 0] = V(leg.x, tower_h, leg.z);
            add_box(V(leg.x - 2, -12, leg.z - 2), V(leg.x + 2, tower_h, leg.z + 2));
        }
        for (int k = 0; k < (golden ? 4 : 2); k++) {
            float hy = y + 12 + k * (tower_h - y - 15) / (golden ? 3 : 1);
            put(MS_CUBE, m4_trs(V(c.x, hy, c.z), yaw_of(D), 0, 0, V(20, 2.5f, 3)), &paint, 0);
        }
        Mat red = glow(V(0.3f, 0, 0), V(6, 0.2f, 0.1f), PAT_NEON, 1);
        put(MS_SPHERE, m4_trs(V(c.x, tower_h + 1, c.z), 0, 0, 0, V(0.7f, 0.7f, 0.7f)), &red, 0.5f);
    }
    for (int sd = 0; sd < 2; sd++) {
        v3 anch0 = add(add(a, mul(R, (sd ? 1 : -1) * 10)), V(0, y + 1 - a.y, 0));
        v3 anch1 = add(add(b, mul(R, (sd ? 1 : -1) * 10)), V(0, y + 1 - b.y, 0));
        v3 pts[5] = {anch0, tops[0][sd], tops[1][sd], anch1};
        (void)pts;
        /* three spans of cable approximated by short straight pieces on a parabola */
        for (int span = 0; span < 3; span++) {
            v3 p0 = span == 0 ? anch0 : span == 1 ? tops[0][sd] : tops[1][sd];
            v3 p1 = span == 0 ? tops[0][sd] : span == 1 ? tops[1][sd] : anch1;
            float sag = span == 1 ? (tower_h - y - 3) : (tower_h - y) * 0.25f;
            int segs = span == 1 ? 24 : 10;
            for (int k = 0; k < segs; k++) {
                float t0 = (float)k / segs, t1 = (float)(k + 1) / segs;
                v3 q0 = lerp3(p0, p1, t0), q1 = lerp3(p0, p1, t1);
                q0.y -= sag * 4 * t0 * (1 - t0) * (span == 1 ? 1 : 0.4f);
                q1.y -= sag * 4 * t1 * (1 - t1) * (span == 1 ? 1 : 0.4f);
                v3 dd = sub(q1, q0);
                float l = len(dd);
                v3 m = mul(add(q0, q1), 0.5f);
                float pitch = asinf(clampf(dd.y / l, -1, 1));
                put(MS_CUBE, m4_trs(m, yaw_of(V(dd.x, 0, dd.z)), pitch, 0, V(0.7f, 0.7f, l + 0.2f)), &paint, 0);
                if (span == 1 && k % 2 == 0 && m.y > y + 2) put(MS_CUBE, m4_box(V(m.x, (m.y + y) / 2, m.z), V(0.15f, m.y - y, 0.15f)), &paint, 0);
            }
        }
    }
}
static void lighthouse(v3 at, int k) {
    float y = terrain_h(at.x, at.z);
    GY = y;
    Mat lw = mat(V(0.88f, 0.88f, 0.85f), PAT_PLAIN, 0, 0.5f), lr = mat(V(0.65f, 0.08f, 0.06f), PAT_PLAIN, 0, 0.5f);
    for (int s = 0; s < 6; s++) put(MS_CYL, m4_trs(V(at.x, s * 4.0f, at.z), 0, 0, 0, V(6 - s * 0.4f, 4, 6 - s * 0.4f)), s % 2 ? &lr : &lw, 0);
    Mat lamp = glow(V(0.5f, 0.5f, 0.4f), V(8, 7, 4), PAT_PLAIN, 0.8f);
    put(MS_CYL8, m4_trs(V(at.x, 24, at.z), 0, 0, 0, V(3, 2.6f, 3)), &lamp, 0);
    put(MS_CONE, m4_trs(V(at.x, 26.6f, at.z), 0, 0, 0, V(3.8f, 2.6f, 3.8f)), &lr, 0);
    add_box(V(at.x - 3, 0, at.z - 3), V(at.x + 3, 29, at.z + 3));
    LIGHTHOUSE[k] = V(at.x, y, at.z);
    GY = 0;
}
static void build_landmarks(void) {
    /* bridges */
    for (int e = 0; e < nedge; e++) (void)e;
    {
        int a = city_node(C_SF, 2, 0), b = find_node_near(1378, 3200, 3);
        if (b >= 0) bridge_structure(a, b, true);
        bridge_structure(city_node(C_SF, 11, 3), city_node(C_OAK, 0, 2), false);
    }
    /* Alcatraz */
    GY = 6;
    {
        Mat cell = mat(V(0.78f, 0.76f, 0.70f), PAT_MIDRISE, 0, 0.8f);
        building(1705, 3425, 1745, 3445, 9, &cell, 0.4f);
        sbox(1700, 0, 3415, 1752, 0.3f, 3462, &M_CONCRETE, 0, false);
        put(MS_CYL8, m4_trs(V(1748, 0, 3455), 0, 0, 0, V(2, 22, 2)), &M_WHITE, 0);
        Mat lamp = glow(V(0.5f, 0.5f, 0.4f), V(6, 5, 3), PAT_PLAIN, 0.9f);
        put(MS_CYL8, m4_trs(V(1748, 22, 3455), 0, 0, 0, V(1.6f, 1.6f, 1.6f)), &lamp, 0);
    }
    GY = 0;
    /* Fisherman's Wharf piers */
    GY = 12;
    for (int k = 0; k < 4; k++) {
        float x = 1700 + k * 60;
        sbox(x, -13, 3520, x + 22, 0.0f, 3640, &M_WOOD, 0, false);
        add_box(V(x, -13, 3520), V(x + 22, 0.0f, 3640));
        for (float z = 3525; z < 3640; z += 12) put(MS_CYL8, m4_trs(V(x + 1, -13, z), 0, 0, 0, V(0.7f, 13, 0.7f)), &M_WOOD, 0);
        Mat shed = mat(PICK(PASTEL, k / 4.0f), PAT_RIBS, 0.2f, 0.6f);
        sbox(x + 3, 0, 3530, x + 19, 6, 3570, &shed, 0, true);
    }
    sign_text("FISHERMAN'S WHARF", V(1790, 8.5f, 3639), V(-1, 0, 0), 0.25f, V(1, 0.3f, 0.2f), 0.4f);
    GY = 0;
    /* Santa Monica pier with the Ferris wheel */
    {
        float py = 6.6f, z = 6980 + 5 * PITCH + 32, px0 = 4080, px1 = 4282;
        Mat deck = M_WOOD;
        sbox(px0, -8, z - 10, px1, py, z + 10, &deck, 0, false);
        add_box(V(px0, -8, z - 10), V(px1, py, z + 10));
        for (float x = px0 + 6; x < px1; x += 14)
            for (int s = -1; s <= 1; s += 2) put(MS_CYL8, m4_trs(V(x, -8, z + s * 8.5f), 0, 0, 0, V(0.8f, 8.6f, 0.8f)), &deck, 0);
        Mat rail = mat(V(0.85f, 0.85f, 0.85f), PAT_PLAIN, 0.5f, 0.4f);
        sbox(px0, py, z - 10, px1 - 30, py + 1.1f, z - 9.8f, &rail, 0, true);
        sbox(px0, py, z + 9.8f, px1 - 30, py + 1.1f, z + 10, &rail, 0, true);
        sbox(px0, py, z - 10, px0 + 0.2f, py + 1.1f, z + 10, &rail, 0, true);
        WHEEL_C = V(px0 + 50, py + 26, z);
        for (int s = -1; s <= 1; s += 2)
            for (int k = -1; k <= 1; k += 2) {
                v3 foot = V(WHEEL_C.x + k * 11, py, z + s * 6);
                v3 dd = sub(V(WHEEL_C.x, WHEEL_C.y, z + s * 3.2f), foot);
                float L = len(dd);
                put(MS_CUBE, m4_trs(add(foot, mul(dd, 0.5f)), 0, 0, atan2f(-dd.x, dd.y), V(1.0f, L, 1.0f)), &M_WHITE, 0);
                add_box(V(foot.x - 0.8f, py, foot.z - 0.8f), V(foot.x + 0.8f, py + 4, foot.z + 0.8f));
            }
        /* arch sign at the pier entrance */
        Mat arch = mat(V(0.12f, 0.35f, 0.55f), PAT_PLAIN, 0.3f, 0.5f);
        sbox(px1 - 2, py, z - 9, px1 - 1, py + 7, z - 8, &arch, 0, true);
        sbox(px1 - 2, py, z + 8, px1 - 1, py + 7, z + 9, &arch, 0, true);
        sbox(px1 - 2, py + 6, z - 9, px1 - 1, py + 8.2f, z + 9, &arch, 0, false);
        GY = 0;
        static_tier = 0;
        bool dm = detail_mode;
        detail_mode = false;
        sign_text("SANTA MONICA PIER", V(px1 - 2.1f, py + 7.1f, z), V(0, 0, 1), 0.16f, V(1, 0.85f, 0.3f), 0.3f);
        detail_mode = dm;
    }
    /* Long Beach port: container piers, cranes, a cargo ship */
    {
        float piers[3] = {4700, 4950, 5200};
        for (int k = 0; k < 3; k++) {
            float x = piers[k];
            GY = 6;
            sbox(x - 16, -12, 7990, x + 16, 0, 8130, &M_CONCRETE, 0, false);
            add_box(V(x - 16, -12, 7990), V(x + 16, 0, 8130));
            containers(x - 12, 8000, x - 2, 8125, (unsigned)k);
            crane(x + 10, 8060, 1.5708f * (k % 2 ? 1 : -1));
        }
        GY = 0;
        ship(4970, 8015, 4992, 8160, false);
    }
    /* San Diego: a retired aircraft carrier at the harbor */
    ship(5310, 8470, 5345, 8620, true);
    /* Santa Barbara mission */
    {
        City *C = &cities[C_SB];
        float x0 = C->x0 + 2 * PITCH + 12, z0 = C->z0 + 9;
        GY = C->gy + 0.15f;
        Mat w = mat(V(0.95f, 0.92f, 0.85f), PAT_HOUSE, 0, 0.8f);
        (void)x0; (void)z0;
        GY = 0;
    }
    /* Sacramento capitol */
    {
        City *C = &cities[C_SAC];
        float cx = C->x0 + 3 * PITCH + 32, cz = C->z0 + 1 * PITCH + 32;
        GY = C->gy + 0.15f;
        Mat w = mat(V(0.92f, 0.90f, 0.84f), PAT_MIDRISE, 0, 0.7f);
        building(cx - 22, cz - 10, cx + 22, cz + 12, 16, &w, 0.2f);
        put(MS_CYL, m4_trs(V(cx, 16, cz), 0, 0, 0, V(16, 10, 16)), &w, 0.2f);
        Mat dome = mat(V(0.85f, 0.75f, 0.35f), PAT_PLAIN, 0.8f, 0.3f);
        put(MS_SPHERE, m4_trs(V(cx, 26, cz), 0, 0, 0, V(8, 9, 8)), &dome, 0);
        for (int k = 0; k < 6; k++) put(MS_CYL8, m4_trs(V(cx - 12 + k * 4.8f, 0, cz - 11), 0, 0, 0, V(1.2f, 13, 1.2f)), &M_WHITE, 0);
        GY = 0;
    }
    /* observatory on the hills above Hollywood */
    {
        float ox = 4850, oz = 6880, oy = terrain_h(ox, oz);
        GY = oy;
        add_pad(ox - 40, oz - 25, ox + 40, oz + 25, oy, 40);
        Mat w = mat(V(0.9f, 0.88f, 0.82f), PAT_HOUSE, 0, 0.7f);
        building(ox - 25, oz - 8, ox + 25, oz + 8, 9, &w, 0.6f);
        Mat dome = mat(V(0.30f, 0.32f, 0.30f), PAT_PLAIN, 0.9f, 0.3f);
        put(MS_SPHERE, m4_trs(V(ox, 9, oz), 0, 0, 0, V(8, 8, 8)), &dome, 0);
        put(MS_SPHERE, m4_trs(V(ox - 20, 9, oz), 0, 0, 0, V(4, 4, 4)), &dome, 0);
        put(MS_SPHERE, m4_trs(V(ox + 20, 9, oz), 0, 0, 0, V(4, 4, 4)), &dome, 0);
        GY = 0;
    }
    lighthouse(V(1860, 0, 5165), 0);  /* Point Pinos, Monterey */
    lighthouse(V(880, 0, 2930), 1);   /* Point Reyes */
    lighthouse(V(5395, 0, 8880), 2);  /* Point Loma, San Diego */
    /* Yosemite waterfall position on the north wall */
    WATERFALL = V(3700, 0, 4030);
    WATERFALL.y = terrain_h(3700, 4010);
    /* San Gorgonio wind farm and the Oildale pumpjacks */
    for (int k = 0; k < 48 && nturbine < 48; k++) {
        float x = 5650 + hash2f(k, 1, 3) * 420, z = 7380 + hash2f(k, 2, 3) * 260;
        if (on_road(x, z, 12) || water_at(x, z)) continue;
        float y = terrain_h(x, z);
        GY = y;
        Mat w = mat(V(0.9f, 0.9f, 0.9f), PAT_PLAIN, 0.3f, 0.4f);
        put(MS_CYL8, m4_trs(V(x, 0, z), 0, 0, 0, V(1.6f, 32, 1.6f)), &w, 0);
        put(MS_CUBE, m4_box(V(x, 32.5f, z), V(2, 2, 4.5f)), &w, 0);
        add_box(V(x - 0.9f, 0, z - 0.9f), V(x + 0.9f, 32, z + 0.9f));
        TURBINES[nturbine++] = V(x, y + 32.5f, z - 2.4f);
        GY = 0;
    }
    for (int k = 0; k < 32 && npump < 32; k++) {
        float x = 4000 + hash2f(k, 5, 3) * 450, z = 5820 + hash2f(k, 6, 3) * 220;
        if (on_road(x, z, 10) || city_at(x, z, 4) >= 0) continue;
        float y = terrain_h(x, z);
        GY = y;
        Mat m = mat(V(0.15f, 0.15f, 0.16f), PAT_PLAIN, 0.6f, 0.5f);
        sbox(x - 1.5f, 0, z - 5, x + 1.5f, 0.5f, z + 5, &M_CONCRETE, 0, true);
        put(MS_CUBE, m4_trs(V(x, 2.6f, z - 0.5f), 0, 0, 0.18f, V(0.4f, 5.2f, 0.4f)), &m, 0);
        put(MS_CUBE, m4_trs(V(x, 2.6f, z + 0.5f), 0, 0, -0.18f, V(0.4f, 5.2f, 0.4f)), &m, 0);
        PUMPJACKS[npump++] = V(x, y + 5.2f, z);
        GY = 0;
    }
}

/* ---- countryside: forests, oaks, palms, deserts, farms, vineyards, orchards */
static void build_country(void) {
    static_tier = 1;
    srand(777);
    int placed = 0;
    for (int k = 0; k < 90000 && placed < 15000; k++) {
        float x = frand() * MAPX, z = frand() * MAPZ;
        if (water_at(x, z) || city_at(x, z, 25) >= 0) continue;
        float h = terrain_h(x, z);
        float a, f, fo, sp;
        biome_at(x, z, &a, &f, &fo, &sp);
        if (f > 0.5f || sp > 0.1f) continue;
        float hx = terrain_h(x + 4, z) - h, hz = terrain_h(x, z + 4) - h;
        if (sqrtf(hx * hx + hz * hz) > 3.2f) continue;
        if (h > 290) continue;
        float coast = coast_sd(x, z);
        int kind = -1;
        float p = frand();
        if (fo > 0.4f) { if (p < fo * 0.9f) kind = (x < 1300 && z < 2700 && coast < 900) ? 3 : 1; }
        else if (a > 0.85f) { if (p < 0.05f) kind = (x > 6100 && z > 7000 && z < 7700) ? 4 : (p < 0.025f ? 5 : 4); }
        else if (a > 0.45f) { if (p < 0.12f) kind = (coast < 400 && z > 6800) ? 2 : 6; }
        else if (p < 0.25f) kind = 0;
        if (kind < 0) continue;
        if (on_road(x, z, 6)) continue;
        bool onpad = false;
        for (int pd = 0; pd < npad && !onpad; pd++) if (pad_weight(&pads[pd], x, z) > 0.3f) onpad = true;
        if (onpad) continue;
        GY = 0;
        tree_at(x, h, z, kind == 3 ? 1.6f : 0.9f + frand() * 0.4f, kind, (unsigned)k * 2654435761u);
        placed++;
    }
    /* palms along the southern beaches and city edges */
    for (int k = 0; k < 600; k++) {
        float z = 6900 + frand() * 2000, x = 3300 + frand() * 2300;
        float sd = coast_sd(x, z);
        if (sd < 25 || sd > 160 || city_at(x, z, 10) >= 0 || on_road(x, z, 6)) continue;
        bool onpad = false;
        for (int pd = 0; pd < npad && !onpad; pd++) if (pad_weight(&pads[pd], x, z) > 0.3f) onpad = true;
        if (onpad) continue;
        tree_at(x, terrain_h(x, z), z, 1.0f, 2, (unsigned)k * 7919u);
    }
    /* Joshua Tree boulder piles */
    static_tier = 0;
    for (int k = 0; k < 40; k++) {
        float x = 6350 + frand() * 400, z = 7150 + frand() * 300;
        if (on_road(x, z, 10)) continue;
        float y = terrain_h(x, z);
        for (int b = 0; b < 5; b++) {
            float s = 2 + frand() * 4;
            inst_s(MS_BLOB, m4_trs(V(x + frs() * 4, y + s * 0.5f, z + frs() * 4), frand() * 6, 0, 0, V(s, s * 0.8f, s * 1.1f)), &M_ROCK, 0);
        }
        add_box(V(x - 4, y, z - 4), V(x + 4, y + 5, z + 4));
    }
    /* Central Valley farms: barns, silos, farmhouses; orchards in rows */
    for (int k = 0; k < 60; k++) {
        float x = 2200 + frand() * 2000, z = 1900 + frand() * 4200;
        float a, f, fo, sp;
        biome_at(x, z, &a, &f, &fo, &sp);
        if (f < 0.8f || on_road(x, z, 30) || city_at(x, z, 60) >= 0 || water_at(x, z)) continue;
        GY = terrain_h(x, z);
        static_tier = 0;
        Mat red = mat(V(0.55f, 0.12f, 0.08f), PAT_PLANK, 0, 0.8f), white = mat(V(0.9f, 0.9f, 0.88f), PAT_HOUSE, 0, 0.8f);
        building(x - 7, z - 9, x + 7, z + 9, 7, &red, 0);
        Mat rm = mat(V(0.25f, 0.25f, 0.27f), PAT_RIBS, 0.5f, 0.5f);
        put(MS_GABLE, m4_trs(V(x, 7, z), 0, 0, 0, V(15, 5, 19)), &rm, 0);
        Mat silo = mat(V(0.7f, 0.72f, 0.74f), PAT_RIBS, 0.7f, 0.4f);
        put(MS_CYL, m4_trs(V(x + 11, 0, z - 4), 0, 0, 0, V(5, 16, 5)), &silo, 0);
        put(MS_SPHERE, m4_trs(V(x + 11, 16, z - 4), 0, 0, 0, V(2.5f, 2, 2.5f)), &silo, 0);
        add_box(V(x + 8.5f, 0, z - 6.5f), V(x + 13.5f, 16, z - 1.5f));
        house(x - 18, z + 4, 9, 8, 2, V(0.9f, 0.9f, 0.86f), V(0.3f, 0.2f, 0.15f), (unsigned)k, 0);
        (void)white;
        /* an orchard beside it */
        static_tier = 2;
        bool ok = true;
        for (int t = 0; t < 4; t++) if (on_road(x + 30 + t * 30, z, 12)) ok = false;
        if (ok && k % 2 == 0) {
            Mat leaf = mat(V(0.13f, 0.25f, 0.07f), PAT_LEAF, 0, 0.9f);
            for (int r = 0; r < 10; r++)
                for (int c = 0; c < 14; c++) {
                    float tx = x + 25 + c * 7, tz = z - 30 + r * 7;
                    float ty = terrain_h(tx, tz);
                    GY = 0;
                    inst_s(MS_CYL8, m4_trs(V(tx, ty, tz), 0, 0, 0, V(0.25f, 1.2f, 0.25f)), &M_TRUNK, 0);
                    inst_s(MS_BLOB, m4_trs(V(tx, ty + 2.0f, tz), 0, 0, 0, V(1.8f, 1.5f, 1.8f)), &leaf, 0);
                }
        }
        GY = 0;
    }
    /* Napa vineyards: long rows of vines and a winery */
    static_tier = 2;
    for (int k = 0; k < 26; k++) {
        float x = 1880 + frand() * 480, z = 2650 + frand() * 520;
        if (city_at(x, z, 40) >= 0 || on_road(x, z, 40) || water_at(x, z)) continue;
        Mat vine = mat(V(0.12f, 0.22f, 0.06f), PAT_LEAF, 0, 0.9f);
        for (int r = 0; r < 14; r++) {
            float rz = z + r * 4;
            float y0 = terrain_h(x, rz), y1 = terrain_h(x + 70, rz);
            v3 a = V(x, y0 + 0.8f, rz), b = V(x + 70, y1 + 0.8f, rz);
            v3 m = mul(add(a, b), 0.5f);
            inst_s(MS_CUBE, m4_trs(m, 1.5708f, 0, atan2f(y1 - y0, 70), V(1.0f, 1.4f, 70)), &vine, 0);
        }
    }
    static_tier = 0;
    {
        float x = 2250, z = 2800;
        GY = terrain_h(x, z);
        add_pad(x - 25, z - 15, x + 25, z + 15, GY, 40);
        Mat stone = mat(V(0.62f, 0.55f, 0.45f), PAT_MIDRISE, 0, 0.85f);
        building(x - 20, z - 8, x + 20, z + 8, 9, &stone, 0.4f);
        roof(x, 9, z, 40, 16, V(0.55f, 0.24f, 0.14f), 1, true);
        GY = 0;
    }
    static_tier = 0;
}

/* ---- street lamps + traffic signals live on the corners of city intersections (props) */

/* ---- the whole state */
static void build_world(void) {
    static_tier = 0;
    /* districts */
    for (int c = 0; c < NCITY; c++) {
        City *C = &cities[c];
        C->dist = malloc((size_t)C->nx * C->nz);
        int def = add_dname(C->name, C->style);
        memset(C->dist, def, (size_t)C->nx * C->nz);
    }
    for (int k = 0; k < NP(DISTS); k++) {
        const DistDef *D = &DISTS[k];
        int id = add_dname(D->name, D->style);
        City *C = &cities[D->city];
        for (int i = D->i0; i <= D->i1 && i < C->nx; i++)
            for (int j = D->j0; j <= D->j1 && j < C->nz; j++) C->dist[j * C->nx + i] = (unsigned char)id;
    }
    /* street grids */
    for (int c = 0; c < NCITY; c++) {
        City *C = &cities[c];
        C->node0 = nnode;
        for (int i = 0; i <= C->nx; i++)
            for (int j = 0; j <= C->nz; j++) add_node(V(C->x0 + i * PITCH, C->gy, C->z0 + j * PITCH), c, i, j);
        for (int i = 0; i <= C->nx; i++)
            for (int j = 0; j <= C->nz; j++) {
                if (i < C->nx) add_edge(city_node(c, i, j), city_node(c, i + 1, j), -1, 5);
                if (j < C->nz) add_edge(city_node(c, i, j), city_node(c, i, j + 1), -1, 5);
            }
    }
    for (int h = 0; h < NHWY; h++) build_highway(h);
    index_roads();
    road_heights();
    /* points of interest */
    struct { int t, c, i, j; } P[] = {
        {POI_SAFE, C_LA, 1, 12}, {POI_SAFE, C_SF, 9, 9}, {POI_SAFE, C_SD, 6, 5}, {POI_SAFE, C_SAC, 5, 5},
        {POI_POLICE, C_LA, 13, 5}, {POI_POLICE, C_LA, 5, 9}, {POI_POLICE, C_SF, 8, 6}, {POI_POLICE, C_SD, 5, 2}, {POI_POLICE, C_SAC, 2, 4}, {POI_POLICE, C_FRE, 2, 4},
        {POI_POLICE, C_BAK, 3, 2}, {POI_POLICE, C_RED, 1, 2}, {POI_POLICE, C_PS, 3, 3}, {POI_POLICE, C_SB, 1, 1}, {POI_POLICE, C_EUR, 2, 1},
        {POI_HOSPITAL, C_LA, 14, 2}, {POI_HOSPITAL, C_LA, 4, 12}, {POI_HOSPITAL, C_SF, 5, 8}, {POI_HOSPITAL, C_SD, 7, 4}, {POI_HOSPITAL, C_SAC, 6, 3},
        {POI_HOSPITAL, C_FRE, 4, 1}, {POI_HOSPITAL, C_BAK, 1, 3}, {POI_HOSPITAL, C_SB, 3, 0}, {POI_HOSPITAL, C_RED, 2, 1}, {POI_HOSPITAL, C_PS, 1, 3}, {POI_HOSPITAL, C_MON, 3, 3},
        {POI_RESPRAY, C_LA, 6, 12}, {POI_RESPRAY, C_LA, 18, 7}, {POI_RESPRAY, C_SF, 1, 8}, {POI_RESPRAY, C_SD, 1, 1}, {POI_RESPRAY, C_SAC, 6, 5}, {POI_RESPRAY, C_BAR, 2, 1}, {POI_RESPRAY, C_FRE, 0, 5},
        {POI_JOB, C_LA, 11, 7}, {POI_JOB, C_LA, 3, 13}, {POI_JOB, C_LA, 17, 4}, {POI_JOB, C_SF, 8, 2}, {POI_JOB, C_SF, 2, 7}, {POI_JOB, C_SD, 3, 5},
        {POI_JOB, C_OAK, 1, 1}, {POI_JOB, C_SAC, 3, 4}, {POI_JOB, C_FRE, 2, 2}, {POI_JOB, C_BAK, 3, 1}, {POI_JOB, C_SB, 1, 2}, {POI_JOB, C_MON, 2, 2},
    };
    for (int k = 0; k < NP(P); k++) add_poi(P[k].t, P[k].c, P[k].i, P[k].j);
    /* shops, hotels and restaurants */
    struct { int t, c, i, j; const char *n; } S[] = {
        {SHOP_HOTEL, C_LA, 13, 6, "PACIFIC GRAND HOTEL"}, {SHOP_HOTEL, C_LA, 8, 2, "SUNSET PALMS MOTEL"}, {SHOP_HOTEL, C_LA, 1, 6, "OCEANFRONT INN"},
        {SHOP_FOOD, C_LA, 16, 5, "TAQUERIA EL SOL"}, {SHOP_FOOD, C_LA, 1, 4, "OCEAN GRILL"}, {SHOP_FOOD, C_LA, 14, 8, "GOLDEN DRAGON"},
        {SHOP_FOOD, C_LA, 1, 11, "SURF SHACK BURGERS"}, {SHOP_FOOD, C_LA, 9, 7, "SEOUL GARDEN BBQ"}, {SHOP_DINER, C_LA, 10, 3, "STARLIGHT DINER"},
        {SHOP_FOOD, C_LA, 4, 4, "TRATTORIA ROMA"}, {SHOP_FOOD, C_LA, 17, 12, "HARBOR FISH TACOS"}, {SHOP_FOOD, C_LA, 9, 12, "BIG SMOKE BBQ"},
        {SHOP_GAS, C_LA, 6, 10, "CALDER FUEL"}, {SHOP_GAS, C_LA, 18, 1, "NORTHSTAR GAS"}, {SHOP_GAS, C_LA, 11, 14, "CALDER FUEL"},
        {SHOP_STORE, C_LA, 12, 12, "QUIK 24"}, {SHOP_STORE, C_LA, 2, 8, "CORNER MART"}, {SHOP_CLOTHES, C_LA, 5, 3, "THREADS"},
        {SHOP_DEALER, C_LA, 17, 11, "HARBOR MOTORS"}, {SHOP_AUTO, C_LA, 13, 13, "TORQUE WORKS"},
        {SHOP_HOTEL, C_SF, 9, 3, "BAYVIEW HOTEL"}, {SHOP_HOTEL, C_SF, 2, 4, "FOG CITY INN"}, {SHOP_FOOD, C_SF, 7, 4, "GOLDEN DRAGON"},
        {SHOP_FOOD, C_SF, 7, 1, "CLAM SHACK"}, {SHOP_FOOD, C_SF, 8, 8, "MISSION TAQUERIA"}, {SHOP_FOOD, C_SF, 5, 4, "SOURDOUGH CAFE"},
        {SHOP_GAS, C_SF, 3, 7, "CALDER FUEL"}, {SHOP_GAS, C_SF, 10, 8, "NORTHSTAR GAS"}, {SHOP_STORE, C_SF, 6, 5, "QUIK 24"},
        {SHOP_CLOTHES, C_SF, 4, 3, "HAIGHT THREADS"}, {SHOP_DEALER, C_SF, 8, 7, "BAY MOTORS"}, {SHOP_AUTO, C_SF, 10, 6, "TORQUE WORKS"},
        {SHOP_HOTEL, C_SD, 4, 1, "HARBOR LIGHTS HOTEL"}, {SHOP_HOTEL, C_SD, 0, 4, "SEASIDE MOTEL"}, {SHOP_FOOD, C_SD, 1, 5, "BAJA FISH TACOS"},
        {SHOP_FOOD, C_SD, 3, 3, "PIZZA NAPOLI"}, {SHOP_GAS, C_SD, 6, 6, "CALDER FUEL"}, {SHOP_STORE, C_SD, 2, 2, "CORNER MART"}, {SHOP_AUTO, C_SD, 4, 6, "TORQUE WORKS"},
        {SHOP_FOOD, C_OAK, 3, 1, "LAKESIDE BBQ"}, {SHOP_GAS, C_OAK, 1, 3, "NORTHSTAR GAS"}, {SHOP_STORE, C_OAK, 2, 4, "QUIK 24"},
        {SHOP_HOTEL, C_SAC, 3, 2, "CAPITOL INN"}, {SHOP_FOOD, C_SAC, 1, 2, "DELTA CAFE"}, {SHOP_GAS, C_SAC, 5, 4, "CALDER FUEL"},
        {SHOP_STORE, C_SAC, 6, 1, "QUIK 24"}, {SHOP_DEALER, C_SAC, 4, 4, "VALLEY MOTORS"}, {SHOP_AUTO, C_SAC, 0, 4, "TORQUE WORKS"},
        {SHOP_HOTEL, C_FRE, 1, 3, "VALLEY VIEW MOTEL"}, {SHOP_FOOD, C_FRE, 3, 2, "FARMHOUSE KITCHEN"}, {SHOP_GAS, C_FRE, 4, 5, "CALDER FUEL"},
        {SHOP_HOTEL, C_BAK, 2, 3, "OIL TOWN MOTEL"}, {SHOP_FOOD, C_BAK, 1, 2, "BASQUE HOUSE"}, {SHOP_GAS, C_BAK, 3, 4, "NORTHSTAR GAS"},
        {SHOP_HOTEL, C_SB, 2, 3, "MISSION BAY INN"}, {SHOP_FOOD, C_SB, 2, 1, "OCEAN GRILL"}, {SHOP_GAS, C_SB, 0, 2, "CALDER FUEL"},
        {SHOP_HOTEL, C_SLO, 1, 2, "MISSION MOTEL"}, {SHOP_FOOD, C_SLO, 2, 1, "TRI-TIP BBQ"}, {SHOP_GAS, C_SLO, 3, 3, "CALDER FUEL"},
        {SHOP_HOTEL, C_MON, 2, 1, "WHARF VIEW HOTEL"}, {SHOP_FOOD, C_MON, 3, 2, "CLAM CHOWDER HOUSE"}, {SHOP_GAS, C_MON, 4, 3, "NORTHSTAR GAS"},
        {SHOP_HOTEL, C_SCZ, 1, 1, "BOARDWALK MOTEL"}, {SHOP_FOOD, C_SCZ, 2, 2, "SURF SHACK BURGERS"}, {SHOP_GAS, C_SCZ, 3, 0, "CALDER FUEL"},
        {SHOP_HOTEL, C_PS, 2, 2, "DESERT PALMS RESORT"}, {SHOP_FOOD, C_PS, 1, 1, "OASIS CANTINA"}, {SHOP_GAS, C_PS, 3, 4, "LAST CHANCE GAS"},
        {SHOP_HOTEL, C_RED, 1, 1, "SHASTA VIEW MOTEL"}, {SHOP_DINER, C_RED, 2, 2, "LUMBERJACK DINER"}, {SHOP_GAS, C_RED, 3, 3, "NORTHSTAR GAS"},
        {SHOP_HOTEL, C_EUR, 1, 1, "VICTORIAN INN"}, {SHOP_FOOD, C_EUR, 2, 2, "CRAB SHACK"}, {SHOP_GAS, C_EUR, 3, 3, "CALDER FUEL"},
        {SHOP_HOTEL, C_NAPA, 1, 1, "VINEYARD INN"}, {SHOP_FOOD, C_NAPA, 0, 0, "VINEYARD BISTRO"}, {SHOP_GAS, C_NAPA, 2, 2, "CALDER FUEL"},
        {SHOP_HOTEL, C_TAHOE, 1, 1, "SIERRA LODGE"}, {SHOP_FOOD, C_TAHOE, 0, 0, "TRAIL CAFE"}, {SHOP_GAS, C_TAHOE, 2, 2, "MOUNTAIN FUEL"},
        {SHOP_HOTEL, C_BAR, 1, 1, "ROUTE 66 MOTEL"}, {SHOP_DINER, C_BAR, 0, 0, "DESERT DINER"}, {SHOP_GAS, C_BAR, 2, 2, "LAST CHANCE GAS"},
        {SHOP_HOTEL, C_FUR, 0, 0, "OASIS INN"}, {SHOP_GAS, C_FUR, 1, 1, "DESERT FUEL"},
        {SHOP_HOTEL, C_YOS, 0, 0, "VALLEY LODGE"}, {SHOP_FOOD, C_YOS, 1, 1, "TRAIL CAFE"},
    };
    for (int k = 0; k < NP(S); k++) add_shop(S[k].t, S[k].c, S[k].i, S[k].j, S[k].n);
    for (int k = 0; k < NP(STOPS); k++) build_stop(&STOPS[k]);
    /* the land, after all flatten jobs are known */
    build_heightmap();
    build_biome();
    /* city blocks */
    for (int c = 0; c < NCITY; c++) {
        GY = cities[c].gy;
        for (int i = 0; i < cities[c].nx; i++)
            for (int j = 0; j < cities[c].nz; j++) gen_block(c, i, j);
        GY = 0;
    }
    build_landmarks();
    build_country();
    /* stunt ramps beside the highways and in the open */
    struct { float x, z, yaw; } RP[] = {{5900, 5100, 0}, {5600, 6200, 1.5708f}, {6900, 8500, 3.14159f}, {7200, 8700, 0}, {3600, 5800, 1.5708f}, {2600, 4300, 0},
                                         {4260, 7400, 0}, {4258, 7600, 3.14159f}, {6400, 7350, -1.5708f}, {1400, 3150, 1.5708f}, {2900, 3900, 0}, {5700, 6900, 0}};
    for (int k = 0; k < NP(RP); k++) {
        float x = RP[k].x, z = RP[k].z;
        if (water_at(x, z) || on_road(x, z, 6)) { x += 30; }
        add_ramp(V(x, terrain_h(x, z) - 0.1f, z), RP[k].yaw, 6, 12, 2.8f);
    }
    /* ground, sea, lakes and roads are always drawn */
    static_tier = -1;
    Mat g = {{1, 1, 1}, PAT_GROUND, 0, 0.8f, {0, 0, 0}, 0};
    inst_s(MS_GROUND, m4_id(), &g, 0);
    Mat o = {{0.01f, 0.04f, 0.05f}, PAT_OCEAN, 0, 0.05f, {0, 0, 0}, 0};
    inst_s(MS_OCEAN, m4_trs(V(0, WATER_Y, 0), 0, 0, 0, V(1, 1, 1)), &o, 0);
    for (int l = 0; l < NLAKE; l++) inst_s(MS_CYL, m4_trs(V(LAKES[l].x, LAKES[l].level - 0.05f, LAKES[l].z), 0, 0, 0, V(LAKES[l].rx * 2.6f, 0.05f, LAKES[l].rz * 2.6f)), &o, 0);
    Mat road = {{0.06f, 0.06f, 0.06f}, PAT_ROADMESH, 0, 0.8f, {0, 0, 0}, 0};
    inst_s(MS_ROADS, m4_id(), &road, 0);
    static_tier = 0;
    /* district colours for the map */
    for (int d = 0; d < ndname; d++) {
        switch (dstyle[d]) {
        case ST_DOWNTOWN: dcolor[d] = V(0.55f, 0.58f, 0.66f); break;
        case ST_MIDRISE: case ST_OLD: dcolor[d] = V(0.62f, 0.56f, 0.50f); break;
        case ST_VICTORIAN: dcolor[d] = V(0.70f, 0.58f, 0.66f); break;
        case ST_SPANISH: case ST_MANSION: dcolor[d] = V(0.80f, 0.68f, 0.52f); break;
        case ST_BEACH: dcolor[d] = V(0.85f, 0.78f, 0.58f); break;
        case ST_INDUSTRIAL: dcolor[d] = V(0.52f, 0.48f, 0.42f); break;
        case ST_PARK: dcolor[d] = V(0.36f, 0.58f, 0.30f); break;
        case ST_DESERT: dcolor[d] = V(0.82f, 0.66f, 0.48f); break;
        case ST_MOUNTAIN: dcolor[d] = V(0.50f, 0.56f, 0.44f); break;
        default: dcolor[d] = V(0.66f, 0.66f, 0.60f);
        }
    }
}

/* the map picture shown on the minimap and the full-screen map */
#define MAPTW 512
#define MAPTH 620
static GLuint tex_map;
static void build_map_texture(void) {
    unsigned char *px = malloc(MAPTW * MAPTH * 4);
    for (int y = 0; y < MAPTH; y++)
        for (int x = 0; x < MAPTW; x++) {
            float wx = WX0 + (x + 0.5f) / MAPTW * (WX1 - WX0), wz = WZ0 + (y + 0.5f) / MAPTH * (WZ1 - WZ0);
            float h = terrain_h(wx, wz);
            v3 c;
            float wl = water_level_at(wx, wz);
            if (h < wl - 0.2f) { float dpt = clampf((wl - h) / 12, 0, 1); c = lerp3(V(0.16f, 0.36f, 0.48f), V(0.05f, 0.14f, 0.26f), dpt); }
            else {
                float a, f, fo, sp;
                biome_at(wx, wz, &a, &f, &fo, &sp);
                c = lerp3(V(0.30f, 0.45f, 0.22f), V(0.62f, 0.55f, 0.35f), smooth01(0.3f, 0.7f, a));
                c = lerp3(c, V(0.80f, 0.70f, 0.50f), smooth01(0.8f, 0.95f, a));
                c = lerp3(c, V(0.20f, 0.34f, 0.16f), fo * 0.8f);
                c = lerp3(c, V(0.50f, 0.55f, 0.28f), f * 0.7f);
                if (sp > 0.4f && sp < 0.6f) c = V(0.88f, 0.86f, 0.80f);
                if (h > 270) c = lerp3(c, V(0.92f, 0.94f, 0.96f), smooth01(270, 320, h));
                if (h < wl + 2) c = lerp3(c, V(0.85f, 0.78f, 0.58f), 0.6f);
                /* hill shading */
                float hx = terrain_h(wx + 20, wz) - h, hz = terrain_h(wx, wz + 20) - h;
                float shade = clampf(1 - (hx + hz) * 0.02f, 0.6f, 1.3f);
                c = mul(c, shade);
                int ci = city_at(wx, wz, -9);
                if (ci >= 0) {
                    City *C = &cities[ci];
                    int bi = (int)((wx - C->x0) / PITCH), bj = (int)((wz - C->z0) / PITCH);
                    c = mul(dcolor[block_dist(ci, bi < C->nx ? bi : C->nx - 1, bj < C->nz ? bj : C->nz - 1)], 0.9f);
                }
            }
            unsigned char *o = px + (y * MAPTW + x) * 4;
            o[0] = (unsigned char)(clampf(c.x, 0, 1) * 255); o[1] = (unsigned char)(clampf(c.y, 0, 1) * 255); o[2] = (unsigned char)(clampf(c.z, 0, 1) * 255); o[3] = 255;
        }
    /* roads drawn on top */
    for (int e = 0; e < nedge; e++) {
        v3 a = nodes[edges[e].a].p, b = nodes[edges[e].b].p;
        bool hw = edges[e].hwy >= 0;
        int steps = (int)(edges[e].len / 4) + 1;
        for (int k = 0; k <= steps; k++) {
            v3 p = lerp3(a, b, (float)k / steps);
            int x = (int)((p.x - WX0) / (WX1 - WX0) * MAPTW), y = (int)((p.z - WZ0) / (WZ1 - WZ0) * MAPTH);
            for (int dy = 0; dy <= (hw ? 1 : 0); dy++)
                for (int dx = 0; dx <= (hw ? 1 : 0); dx++) {
                    int xx = x + dx, yy = y + dy;
                    if (xx < 0 || yy < 0 || xx >= MAPTW || yy >= MAPTH) continue;
                    unsigned char *o = px + (yy * MAPTW + xx) * 4;
                    if (hw) { o[0] = 240; o[1] = 196; o[2] = 70; }
                    else { o[0] = 70; o[1] = 70; o[2] = 76; }
                }
        }
    }
    glGenTextures(1, &tex_map);
    glBindTexture(GL_TEXTURE_2D, tex_map);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, MAPTW, MAPTH, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    free(px);
}
static GLuint tex_biome;
static void upload_biome(void) {
    glGenTextures(1, &tex_biome);
    glBindTexture(GL_TEXTURE_2D, tex_biome);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, BNX, BNZ, 0, GL_RGBA, GL_UNSIGNED_BYTE, biome);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
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
        } else {
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
static bool point_blocked(v3 p, float rad, float height) { v3 q = p; return collide_circle(&q, rad, height, NULL); }
static float ray_world(v3 o, v3 d, float maxd) {
    v3 e = add(o, mul(d, maxd));
    query_boxes(fminf(o.x, e.x) - 1, fminf(o.z, e.z) - 1, fmaxf(o.x, e.x) + 1, fmaxf(o.z, e.z) + 1);
    float best = maxd;
    for (int k = 0; k < qn; k++) {
        const Box *b = &boxes[qlist[k]];
        if (b->mx.y - b->mn.y < 1.2f || b->ramp) continue;
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
    /* terrain: march */
    for (float t = 2; t < best; t += 2) {
        v3 p = add(o, mul(d, t));
        if (p.y < terrain_h(p.x, p.z) + 0.3f) return fmaxf(t - 2, 0.5f);
    }
    return best;
}
