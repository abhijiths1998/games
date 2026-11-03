/* map.c - the state of California, scaled down: coastline, bays and lakes, mountains and deserts (a
 * height map), the cities with their street grids, and the highway network as one road graph.
 * Real place names; every business and building is invented. Horizontal scale is about 1:120. */

#define PITCH 64.0f
#define MAPX 7600.0f
#define MAPZ 9200.0f
#define WX0 (-300.0f)
#define WZ0 (-300.0f)
#define WX1 (MAPX + 300.0f)
#define WZ1 (MAPZ + 300.0f)
#define WATER_Y (-0.8f)
#define STEP 0.6f
#define LANE_OFF 2.2f
#define WALK 7.0f

/* ================================================================== noise + geometry helpers */
static float vnoise2(float x, float z, int s) {
    int ix = (int)floorf(x), iz = (int)floorf(z);
    float fx = x - ix, fz = z - iz;
    fx = fx * fx * (3 - 2 * fx);
    fz = fz * fz * (3 - 2 * fz);
    float a = hash2f(ix, iz, s), b = hash2f(ix + 1, iz, s), c = hash2f(ix, iz + 1, s), d = hash2f(ix + 1, iz + 1, s);
    return lerpf(lerpf(a, b, fx), lerpf(c, d, fx), fz);
}
static float fbm2(float x, float z, int oct, int s) {
    float a = 0.5f, t = 0, n = 0;
    for (int i = 0; i < oct; i++) { t += vnoise2(x, z, s + i * 17) * a; n += a; x *= 2.03f; z *= 2.03f; a *= 0.5f; }
    return t / n;
}
static float ridged2(float x, float z, int oct, int s) {
    float a = 0.5f, t = 0, n = 0;
    for (int i = 0; i < oct; i++) { float v = 1 - fabsf(vnoise2(x, z, s + i * 31) * 2 - 1); t += v * v * a; n += a; x *= 2.1f; z *= 2.1f; a *= 0.5f; }
    return t / n;
}
typedef struct { float x, z; } P2;
static float seg_dist(float px, float pz, P2 a, P2 b, float *t_out) {
    float dx = b.x - a.x, dz = b.z - a.z, l2 = dx * dx + dz * dz;
    float t = l2 > 0 ? clampf(((px - a.x) * dx + (pz - a.z) * dz) / l2, 0, 1) : 0;
    if (t_out) *t_out = t;
    float qx = a.x + dx * t - px, qz = a.z + dz * t - pz;
    return sqrtf(qx * qx + qz * qz);
}
static float poly_dist(float x, float z, const P2 *p, int n, bool closed) {
    float d = 1e9f;
    for (int i = 0; i < n - (closed ? 0 : 1); i++) d = fminf(d, seg_dist(x, z, p[i], p[(i + 1) % n], NULL));
    return d;
}
static bool in_poly(float x, float z, const P2 *p, int n) {
    bool in = false;
    for (int i = 0, j = n - 1; i < n; j = i++)
        if (((p[i].z > z) != (p[j].z > z)) && (x < (p[j].x - p[i].x) * (z - p[i].z) / (p[j].z - p[i].z) + p[i].x)) in = !in;
    return in;
}
#define NP(a) ((int)(sizeof(a) / sizeof(a[0])))

/* land: the Pacific coast from the Oregon line to Mexico, closed around the east */
static const P2 LAND[] = {
    {260, -400}, {260, 600}, {200, 1000}, {270, 1100}, {285, 1450}, {180, 1700}, {350, 2100}, {600, 2600}, {850, 2950},
    {1100, 3180}, {1150, 3300}, {1195, 3640}, {1195, 4300}, {1300, 4650}, {1690, 4990}, {1830, 5150}, {1840, 5420},
    {2050, 5700}, {2300, 6000}, {2600, 6100}, {2740, 6200}, {2760, 6450}, {2900, 6800}, {3250, 7120}, {3480, 7268},
    {3800, 7272}, {4100, 7200}, {4250, 7050}, {4252, 7700}, {4280, 7900}, {4500, 7995}, {5300, 7995}, {5370, 8150},
    {5370, 8420}, {5370, 8870}, {5400, 9700}, {8200, 9700}, {8200, -400}};
static const P2 STRAIT[] = {{1000, 3255}, {2000, 3255}, {2000, 3600}, {1000, 3600}};
static const P2 BAY[] = {{1995, 3255}, {2385, 3255}, {2385, 4600}, {1995, 4600}};
static const P2 MBAY[] = {{1650, 4985}, {2250, 4985}, {2250, 5112}, {1835, 5112}};
typedef struct { float x, z, rx, rz, level; const char *name; } Lake;
static const Lake LAKES[] = {{3430, 2760, 150, 210, 106, "LAKE TAHOE"}, {6450, 8150, 170, 330, -7, "SALTON SEA"}};
#define NLAKE NP(LAKES)

/* mountain ranges as ridge lines */
static const P2 R_SIERRA[] = {{2800, 1500}, {3150, 2300}, {3500, 2650}, {3750, 3600}, {3950, 4300}, {4250, 5200}, {4550, 5900}, {4700, 6300}};
static const P2 R_COASTN[] = {{520, 300}, {640, 1100}, {700, 1700}, {950, 2400}, {1250, 2850}};
static const P2 R_COASTC[] = {{1450, 4350}, {1900, 4550}, {2250, 5300}, {2500, 5800}, {2950, 6550}, {3300, 6880}};
static const P2 R_TRANS[] = {{3300, 6870}, {3900, 6880}, {4400, 6760}, {5000, 6870}, {5700, 7010}};
static const P2 R_PENIN[] = {{5750, 7820}, {5980, 8300}, {6150, 8950}};
static const P2 R_PANAMINT[] = {{5420, 4700}, {5450, 5700}};
static const P2 V_CENTRAL[] = {{2200, 1900}, {2600, 3000}, {3000, 4000}, {3500, 5200}, {4000, 6050}};
static const P2 SHASTA = {1780, 650};
static float ridge(float x, float z, const P2 *p, int n, float H, float w) {
    float d = poly_dist(x, z, p, n, false);
    return H * expf(-(d / w) * (d / w));
}

/* ================================================================== cities */
enum { ST_DOWNTOWN, ST_MIDRISE, ST_OLD, ST_SUBURB, ST_SPANISH, ST_VICTORIAN, ST_BEACH, ST_INDUSTRIAL, ST_PARK, ST_DESERT, ST_MOUNTAIN, ST_MANSION };
typedef struct { const char *name; float x0, z0; int nx, nz; float gy; int node0; unsigned char *dist; int style; } City;
enum { C_LA, C_SF, C_SD, C_OAK, C_SAC, C_FRE, C_BAK, C_SB, C_SLO, C_MON, C_SCZ, C_PS, C_RED, C_EUR, C_NAPA, C_TAHOE, C_BAR, C_FUR, C_YOS, NCITY };
static City cities[NCITY] = {
    {"LOS ANGELES", 4300, 6980, 20, 15, 6, 0, 0, ST_SUBURB},     {"SAN FRANCISCO", 1250, 3640, 11, 10, 12, 0, 0, ST_VICTORIAN},
    {"SAN DIEGO", 5420, 8420, 8, 7, 5, 0, 0, ST_SPANISH},        {"OAKLAND", 2410, 3704, 6, 6, 12, 0, 0, ST_SUBURB},
    {"SACRAMENTO", 2560, 2950, 7, 6, 8, 0, 0, ST_SUBURB},        {"FRESNO", 3500, 4900, 6, 6, 9, 0, 0, ST_SUBURB},
    {"BAKERSFIELD", 4050, 6050, 5, 5, 12, 0, 0, ST_SUBURB},      {"SANTA BARBARA", 3480, 6960, 5, 4, 5, 0, 0, ST_SPANISH},
    {"SAN LUIS OBISPO", 2800, 6150, 4, 4, 8, 0, 0, ST_SPANISH},  {"MONTEREY", 1880, 5150, 5, 4, 6, 0, 0, ST_OLD},
    {"SANTA CRUZ", 1720, 4760, 4, 3, 5, 0, 0, ST_BEACH},         {"PALM SPRINGS", 6000, 7650, 5, 5, 15, 0, 0, ST_DESERT},
    {"REDDING", 1500, 1300, 4, 4, 17, 0, 0, ST_SUBURB},          {"EUREKA", 330, 1150, 4, 4, 5, 0, 0, ST_VICTORIAN},
    {"NAPA", 2000, 2900, 3, 3, 6, 0, 0, ST_SPANISH},             {"SOUTH LAKE TAHOE", 3330, 3020, 3, 3, 110, 0, 0, ST_MOUNTAIN},
    {"BARSTOW", 5560, 6600, 3, 3, 70, 0, 0, ST_DESERT},          {"FURNACE CREEK", 5680, 5240, 2, 2, -5, 0, 0, ST_DESERT},
    {"YOSEMITE VILLAGE", 3700, 4060, 2, 2, 120, 0, 0, ST_MOUNTAIN},
};
/* district names: cities split into real neighbourhoods; outside the cities, regions */
#define MAXDN 128
static const char *dname[MAXDN];
static int dstyle[MAXDN], ndname;
static v3 dcolor[MAXDN];
static int add_dname(const char *n, int style) {
    for (int i = 0; i < ndname; i++) if (!strcmp(dname[i], n)) return i;
    if (ndname >= MAXDN) return 0;
    dname[ndname] = n;
    dstyle[ndname] = style;
    return ndname++;
}
typedef struct { int city; const char *name; int i0, j0, i1, j1, style; } DistDef;
static const DistDef DISTS[] = {
    {C_LA, "BRENTWOOD", 0, 0, 2, 2, ST_SUBURB}, {C_LA, "SANTA MONICA", 0, 3, 2, 9, ST_BEACH}, {C_LA, "VENICE", 0, 10, 2, 14, ST_BEACH},
    {C_LA, "BEVERLY HILLS", 3, 0, 6, 6, ST_MANSION}, {C_LA, "CULVER CITY", 3, 7, 6, 14, ST_SPANISH}, {C_LA, "HOLLYWOOD", 7, 0, 11, 5, ST_MIDRISE},
    {C_LA, "KOREATOWN", 7, 6, 11, 9, ST_MIDRISE}, {C_LA, "SOUTH LA", 7, 10, 11, 14, ST_SPANISH}, {C_LA, "SILVER LAKE", 12, 0, 15, 3, ST_SUBURB},
    {C_LA, "DOWNTOWN LA", 12, 4, 15, 9, ST_DOWNTOWN}, {C_LA, "PASADENA", 16, 0, 19, 2, ST_SPANISH}, {C_LA, "BOYLE HEIGHTS", 16, 3, 19, 9, ST_OLD},
    {C_LA, "LONG BEACH", 12, 10, 19, 14, ST_INDUSTRIAL},
    {C_SF, "PRESIDIO", 0, 0, 2, 2, ST_PARK}, {C_SF, "RICHMOND", 0, 3, 3, 4, ST_VICTORIAN}, {C_SF, "GOLDEN GATE PARK", 0, 5, 5, 5, ST_PARK},
    {C_SF, "SUNSET", 0, 6, 3, 9, ST_SUBURB}, {C_SF, "MARINA", 3, 0, 6, 2, ST_VICTORIAN}, {C_SF, "HAIGHT-ASHBURY", 4, 3, 5, 4, ST_VICTORIAN},
    {C_SF, "NOE VALLEY", 4, 6, 5, 9, ST_VICTORIAN}, {C_SF, "HAYES VALLEY", 6, 3, 6, 7, ST_VICTORIAN}, {C_SF, "FISHERMAN'S WHARF", 7, 0, 8, 1, ST_OLD},
    {C_SF, "NORTH BEACH", 7, 2, 8, 3, ST_OLD}, {C_SF, "CHINATOWN", 7, 4, 8, 5, ST_MIDRISE}, {C_SF, "FINANCIAL DISTRICT", 9, 0, 10, 5, ST_DOWNTOWN},
    {C_SF, "SOMA", 7, 6, 10, 7, ST_MIDRISE}, {C_SF, "MISSION", 6, 8, 10, 9, ST_SPANISH},
    {C_SD, "LITTLE ITALY", 0, 0, 2, 2, ST_OLD}, {C_SD, "OCEAN BEACH", 0, 3, 1, 6, ST_BEACH}, {C_SD, "DOWNTOWN SAN DIEGO", 3, 0, 5, 2, ST_DOWNTOWN},
    {C_SD, "GASLAMP QUARTER", 2, 3, 5, 4, ST_OLD}, {C_SD, "BALBOA PARK", 6, 0, 7, 3, ST_PARK}, {C_SD, "NORTH PARK", 6, 4, 7, 6, ST_SPANISH},
    {C_SD, "BARRIO LOGAN", 2, 5, 5, 6, ST_INDUSTRIAL},
    {C_OAK, "WEST OAKLAND", 0, 0, 1, 5, ST_INDUSTRIAL}, {C_OAK, "TEMESCAL", 2, 0, 5, 1, ST_SUBURB}, {C_OAK, "DOWNTOWN OAKLAND", 2, 2, 3, 3, ST_DOWNTOWN},
    {C_OAK, "LAKE MERRITT", 4, 2, 5, 3, ST_PARK}, {C_OAK, "JACK LONDON SQUARE", 2, 4, 5, 5, ST_OLD},
    {C_SAC, "OLD SACRAMENTO", 0, 0, 1, 3, ST_OLD}, {C_SAC, "DOWNTOWN SACRAMENTO", 2, 0, 4, 3, ST_MIDRISE}, {C_SAC, "MIDTOWN SACRAMENTO", 5, 0, 6, 3, ST_VICTORIAN},
    {C_SAC, "LAND PARK", 0, 4, 6, 5, ST_SUBURB},
    {C_FRE, "TOWER DISTRICT", 0, 0, 5, 1, ST_OLD}, {C_FRE, "DOWNTOWN FRESNO", 2, 2, 3, 3, ST_MIDRISE},
    {C_BAK, "OILDALE", 0, 0, 4, 1, ST_INDUSTRIAL}, {C_BAK, "DOWNTOWN BAKERSFIELD", 1, 2, 3, 3, ST_MIDRISE},
    {C_SB, "STATE STREET", 1, 0, 3, 2, ST_SPANISH}, {C_SB, "MONTECITO", 4, 0, 4, 3, ST_MANSION}, {C_SB, "SANTA BARBARA WATERFRONT", 0, 3, 3, 3, ST_BEACH},
    {C_MON, "CANNERY ROW", 0, 0, 4, 0, ST_INDUSTRIAL}, {C_MON, "PACIFIC GROVE", 0, 1, 1, 3, ST_VICTORIAN}, {C_MON, "OLD MONTEREY", 2, 1, 4, 3, ST_SPANISH},
    {C_SCZ, "SEABRIGHT", 0, 2, 3, 2, ST_BEACH},
    {C_PS, "PALM CANYON DRIVE", 2, 0, 2, 4, ST_SPANISH},
    {C_EUR, "OLD TOWN EUREKA", 0, 0, 3, 1, ST_VICTORIAN},
};
static int region_name(float x, float z);
static int city_at(float x, float z, float margin) {
    for (int c = 0; c < NCITY; c++) {
        City *C = &cities[c];
        if (x >= C->x0 - margin && z >= C->z0 - margin && x <= C->x0 + C->nx * PITCH + margin && z <= C->z0 + C->nz * PITCH + margin) return c;
    }
    return -1;
}
static int block_dist(int c, int i, int j) { return cities[c].dist[j * cities[c].nx + i]; }

/* ================================================================== collision boxes */
typedef struct { v3 mn, mx; int ramp; } Box; /* ramp: 0 none, 1 top rises toward -z, 2 toward +z, 3 toward -x, 4 toward +x */
static Box *boxes;
static int nbox, capbox;
#define GC 32.0f
#define GNX ((int)((WX1 - WX0) / GC) + 1)
#define GNZ ((int)((WZ1 - WZ0) / GC) + 1)
typedef struct { int *idx; int n, cap; } Cell;
static Cell *grid;
static float GY; /* base height added to everything generated while building a city */
static int gcx(float v) { int c = (int)floorf((v - WX0) / GC); return c < 0 ? 0 : c >= GNX ? GNX - 1 : c; }
static int gcz(float v) { int c = (int)floorf((v - WZ0) / GC); return c < 0 ? 0 : c >= GNZ ? GNZ - 1 : c; }
static int add_box_r(v3 mn, v3 mx, int ramp) {
    if (!grid) grid = calloc((size_t)GNX * GNZ, sizeof(Cell));
    mn.y += GY; mx.y += GY;
    if (nbox == capbox) { capbox = capbox ? capbox * 2 : 8192; boxes = realloc(boxes, sizeof(Box) * (size_t)capbox); }
    boxes[nbox] = (Box){mn, mx, ramp};
    for (int gx = gcx(mn.x); gx <= gcx(mx.x); gx++)
        for (int gz = gcz(mn.z); gz <= gcz(mx.z); gz++) {
            Cell *c = &grid[gz * GNX + gx];
            if (c->n == c->cap) { c->cap = c->cap ? c->cap * 2 : 8; c->idx = realloc(c->idx, sizeof(int) * (size_t)c->cap); }
            c->idx[c->n++] = nbox;
        }
    return nbox++;
}
static int add_box(v3 mn, v3 mx) { return add_box_r(mn, mx, 0); }
static int qstamp, *qmark, qcap;
static int qlist[2048], qn;
static void query_boxes(float x0, float z0, float x1, float z1) {
    if (!grid) grid = calloc((size_t)GNX * GNZ, sizeof(Cell));
    if (qcap < nbox) { qcap = nbox + 4096; qmark = realloc(qmark, sizeof(int) * (size_t)qcap); memset(qmark, 0, sizeof(int) * (size_t)qcap); qstamp = 0; }
    qstamp++;
    qn = 0;
    for (int gx = gcx(x0); gx <= gcx(x1); gx++)
        for (int gz = gcz(z0); gz <= gcz(z1); gz++) {
            Cell *c = &grid[gz * GNX + gx];
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

/* ================================================================== road graph */
typedef struct { v3 p; int city, ci, cj, deg, e[5]; bool light; float inset, hwdist; } Node;
typedef struct { int a, b, hwy; float len, hw, v0; v3 d; bool bridge; } Edge; /* hwy: -1 city street, else highway index */
#define MAXNODE 6000
#define MAXEDGE 8000
static Node nodes[MAXNODE];
static Edge edges[MAXEDGE];
static int nnode, nedge;
static int add_node(v3 p, int city, int ci, int cj) {
    Node *n = &nodes[nnode];
    memset(n, 0, sizeof(*n));
    n->p = p; n->city = city; n->ci = ci; n->cj = cj;
    return nnode++;
}
static int add_edge(int a, int b, int hwy, float hw) {
    if (a == b || nodes[a].deg >= 5 || nodes[b].deg >= 5) return -1;
    for (int k = 0; k < nodes[a].deg; k++) { int e = nodes[a].e[k]; if (edges[e].a == b || edges[e].b == b) return e; }
    Edge *E = &edges[nedge];
    memset(E, 0, sizeof(*E));
    E->a = a; E->b = b; E->hwy = hwy; E->hw = hw;
    v3 d = sub(nodes[b].p, nodes[a].p);
    d.y = 0;
    E->len = len2d(d);
    E->d = mul(d, 1 / fmaxf(E->len, 1e-3f));
    nodes[a].e[nodes[a].deg++] = nedge;
    nodes[b].e[nodes[b].deg++] = nedge;
    return nedge++;
}
static int edge_other(int e, int n) { return edges[e].a == n ? edges[e].b : edges[e].a; }
static v3 edge_dir(int e, int from) { return edges[e].a == from ? edges[e].d : mul(edges[e].d, -1); }
static v3 right_vec(v3 d) { return V(-d.z, 0, d.x); }
static v3 lane_pos(int e, int from, float s, float off) {
    int to = edge_other(e, from);
    v3 D = edge_dir(e, from);
    v3 p = lerp3(nodes[from].p, nodes[to].p, clampf(s / edges[e].len, 0, 1));
    return add(p, mul(right_vec(D), off));
}
static int city_node(int c, int i, int j) { return cities[c].node0 + i * (cities[c].nz + 1) + j; }
static v3 node_pos(int n) { return nodes[n].p; }

/* spatial index for edges and nodes */
#define EG 64.0f
#define EGX ((int)((WX1 - WX0) / EG) + 1)
#define EGZ ((int)((WZ1 - WZ0) / EG) + 1)
static Cell *egrid, *ngrid;
static void cell_add(Cell *g, int gx, int gz, int v) {
    if (gx < 0 || gz < 0 || gx >= EGX || gz >= EGZ) return;
    Cell *c = &g[gz * EGX + gx];
    for (int k = 0; k < c->n; k++) if (c->idx[k] == v) return;
    if (c->n == c->cap) { c->cap = c->cap ? c->cap * 2 : 4; c->idx = realloc(c->idx, sizeof(int) * (size_t)c->cap); }
    c->idx[c->n++] = v;
}
static int egx(float x) { return (int)floorf((x - WX0) / EG); }
static int egz(float z) { return (int)floorf((z - WZ0) / EG); }
static void index_roads(void) {
    egrid = calloc((size_t)EGX * EGZ, sizeof(Cell));
    ngrid = calloc((size_t)EGX * EGZ, sizeof(Cell));
    for (int n = 0; n < nnode; n++) cell_add(ngrid, egx(nodes[n].p.x), egz(nodes[n].p.z), n);
    for (int e = 0; e < nedge; e++) {
        v3 a = nodes[edges[e].a].p, b = nodes[edges[e].b].p;
        int steps = (int)(edges[e].len / (EG * 0.5f)) + 1;
        for (int k = 0; k <= steps; k++) {
            v3 p = lerp3(a, b, (float)k / steps);
            for (int dx = -1; dx <= 1; dx++)
                for (int dz = -1; dz <= 1; dz++) cell_add(egrid, egx(p.x) + dx, egz(p.z) + dz, e);
        }
    }
}
/* nearest edge to a point: returns edge id (or -1), distance, and the parameter s from edge.a */
static int nearest_edge(v3 p, float maxd, float *dist, float *s_from_a) {
    int best = -1;
    float bd = maxd;
    int cx = egx(p.x), cz = egz(p.z), r = (int)(maxd / EG) + 1;
    for (int gz = cz - r; gz <= cz + r; gz++)
        for (int gx = cx - r; gx <= cx + r; gx++) {
            if (gx < 0 || gz < 0 || gx >= EGX || gz >= EGZ) continue;
            Cell *c = &egrid[gz * EGX + gx];
            for (int k = 0; k < c->n; k++) {
                int e = c->idx[k];
                P2 a = {nodes[edges[e].a].p.x, nodes[edges[e].a].p.z}, b = {nodes[edges[e].b].p.x, nodes[edges[e].b].p.z};
                float t, d = seg_dist(p.x, p.z, a, b, &t);
                if (d < bd) { bd = d; best = e; if (s_from_a) *s_from_a = t * edges[e].len; }
            }
        }
    if (dist) *dist = bd;
    return best;
}
static int nearest_road_node(v3 p) {
    for (int r = 1; r < 40; r += 3) {
        int best = -1;
        float bd = 1e9f;
        int cx = egx(p.x), cz = egz(p.z);
        for (int gz = cz - r; gz <= cz + r; gz++)
            for (int gx = cx - r; gx <= cx + r; gx++) {
                if (gx < 0 || gz < 0 || gx >= EGX || gz >= EGZ) continue;
                Cell *c = &ngrid[gz * EGX + gx];
                for (int k = 0; k < c->n; k++) {
                    int n = c->idx[k];
                    if (!nodes[n].deg) continue;
                    float d = dist2d(nodes[n].p, p);
                    if (d < bd) { bd = d; best = n; }
                }
            }
        if (best >= 0) return best;
    }
    return 0;
}
static bool on_road(float x, float z, float margin) {
    float d;
    int e = nearest_edge(V(x, 0, z), 24, &d, NULL);
    return e >= 0 && d < edges[e].hw + margin;
}

/* ================================================================== height map */
#define HRES 20.0f
#define HNX ((int)((WX1 - WX0) / HRES) + 1)
#define HNZ ((int)((WZ1 - WZ0) / HRES) + 1)
static float *hmap;
static float coast_sd(float x, float z) { /* signed distance to the coast: positive on land */
    float d = poly_dist(x, z, LAND, NP(LAND), true);
    bool land = in_poly(x, z, LAND, NP(LAND));
    if (land) {
        float w = fminf(fminf(poly_dist(x, z, STRAIT, 4, true) * (in_poly(x, z, STRAIT, 4) ? -1 : 1), poly_dist(x, z, BAY, 4, true) * (in_poly(x, z, BAY, 4) ? -1 : 1)),
                        poly_dist(x, z, MBAY, 4, true) * (in_poly(x, z, MBAY, 4) ? -1 : 1));
        return fminf(d, w);
    }
    return -d;
}
static float raw_land(float x, float z) {
    float h = 5 + fbm2(x * 0.0021f, z * 0.0021f, 4, 3) * 26;
    float rn = ridged2(x * 0.0032f, z * 0.0032f, 4, 11);
    float m = 0;
    m += ridge(x, z, R_SIERRA, NP(R_SIERRA), 330, 400) * (0.55f + 0.7f * rn);
    m += ridge(x, z, R_COASTN, NP(R_COASTN), 140, 300) * (0.5f + 0.7f * rn);
    m += ridge(x, z, R_COASTC, NP(R_COASTC), 110, 260) * (0.5f + 0.7f * rn);
    m += ridge(x, z, R_TRANS, NP(R_TRANS), 170, 170) * (0.5f + 0.7f * rn);
    m += ridge(x, z, R_PENIN, NP(R_PENIN), 140, 220) * (0.5f + 0.7f * rn);
    m += ridge(x, z, R_PANAMINT, NP(R_PANAMINT), 200, 130) * (0.5f + 0.7f * rn);
    /* Klamath and Trinity mountains in the north-west */
    if (x < 1700 && z < 1700) m += 120 * smooth01(0, 400, fminf(1700 - x, 1700 - z)) * (0.3f + rn);
    /* Mojave: rugged desert ranges */
    if (x > 4500 && z > 5000 && z < 7600) m += (35 + 80 * rn * rn) * smooth01(4500, 5000, x) * smooth01(5000, 5400, z);
    /* Shasta */
    float ds = sqrtf((x - SHASTA.x) * (x - SHASTA.x) + (z - SHASTA.z) * (z - SHASTA.z));
    if (ds < 520) m += 470 * powf(1 - ds / 520, 1.6f);
    h += m;
    /* the Central Valley is flat farmland */
    float vd = poly_dist(x, z, V_CENTRAL, NP(V_CENTRAL), false);
    float vf = 1 - smooth01(330, 650, vd);
    h = lerpf(h, 7 + fbm2(x * 0.004f, z * 0.004f, 2, 5) * 5, vf);
    /* basins: Death Valley, the Salton trough, Lake Tahoe */
    float dv = ((x - 5720) / 230) * ((x - 5720) / 230) + ((z - 5250) / 680) * ((z - 5250) / 680);
    if (dv < 1.6f) h = lerpf(h, -5, 1 - smooth01(0.55f, 1.6f, dv));
    for (int l = 0; l < NLAKE; l++) {
        const Lake *L = &LAKES[l];
        float e = ((x - L->x) / L->rx) * ((x - L->x) / L->rx) + ((z - L->z) / L->rz) * ((z - L->z) / L->rz);
        if (e < 2.2f) h = lerpf(h, L->level - 9, 1 - smooth01(0.8f, 2.2f, e));
        if (e < 1) h = fminf(h, L->level - 2 - (1 - e) * 8);
    }
    /* Yosemite Valley: a glacial trough with granite walls, a dome at its head */
    {
        P2 a = {3500, 4110}, b = {3990, 4110};
        float t, d = seg_dist(x, z, a, b, &t);
        if (d < 300) {
            float floorh = 120;
            float wall = floorh + fmaxf(0, d - 75) * 1.6f;
            h = fminf(h, lerpf(wall, h, smooth01(200, 300, d)));
        }
        float dd = sqrtf((x - 4040) * (x - 4040) + (z - 4060) * (z - 4060));
        if (dd < 170 && x > 3990) h = fmaxf(h, 120 + sqrtf(fmaxf(0, 170 * 170 - dd * dd)) * 1.15f);
    }
    return h;
}
/* flatten jobs applied after the base terrain */
typedef struct { float x0, z0, x1, z1, h, margin; } Pad;
#define MAXPAD 64
static Pad pads[MAXPAD];
static int npad;
static void add_pad(float x0, float z0, float x1, float z1, float h, float margin) { if (npad < MAXPAD) pads[npad++] = (Pad){x0, z0, x1, z1, h, margin}; }
static float pad_weight(const Pad *p, float x, float z) {
    float dx = fmaxf(fmaxf(p->x0 - x, x - p->x1), 0), dz = fmaxf(fmaxf(p->z0 - z, z - p->z1), 0);
    return 1 - smooth01(0, p->margin, sqrtf(dx * dx + dz * dz));
}
static float base_height(float x, float z) {
    float sd = coast_sd(x, z);
    float h;
    if (sd < 0) h = fmaxf(-16, -1.6f + sd * 0.07f);
    else {
        float land = raw_land(x, z);
        float beach = -1.4f + sd * 0.035f;
        h = lerpf(beach, land, smooth01(20, 180, sd));
        if (sd < 70) h = fminf(h, beach + 2);
    }
    /* islands: Alcatraz */
    if (x > 1690 && x < 1760 && z > 3410 && z < 3470) h = 6;
    for (int c = 0; c < NCITY; c++) {
        City *C = &cities[c];
        Pad p = {C->x0 - 12, C->z0 - 12, C->x0 + C->nx * PITCH + 12, C->z0 + C->nz * PITCH + 12, C->gy, 150};
        float w = pad_weight(&p, x, z);
        if (w > 0 && (sd > 0 || w > 0.97f)) h = lerpf(h, C->gy, w);
    }
    return h;
}

/* ================================================================== highways */
typedef struct { int city, i, j; float x, z; } HP;
#define CN(c, i, j) {c, i, j, 0, 0}
#define PT(x, z) {-1, 0, 0, x, z}
#define HEND {-2, 0, 0, 0, 0}
typedef struct { const char *name; HP p[16]; } HwyDef;
static const HwyDef HWYS[] = {
    {"INTERSTATE 5", {CN(C_LA, 10, 0), PT(4650, 6800), PT(4380, 6600), PT(3950, 6000), PT(3550, 5500), PT(3250, 4900), PT(2950, 4300), PT(2750, 3700), CN(C_SAC, 2, 6), HEND}},
    {"INTERSTATE 5", {CN(C_SAC, 3, 0), PT(2300, 2500), PT(1750, 1900), CN(C_RED, 2, 4), HEND}},
    {"INTERSTATE 5", {CN(C_RED, 2, 0), PT(1520, 950), PT(1350, 500), PT(1330, 30), HEND}},
    {"INTERSTATE 5", {CN(C_LA, 14, 15), PT(5400, 8080), PT(5450, 8300), CN(C_SD, 2, 0), HEND}},
    {"HIGHWAY 99", {CN(C_BAK, 2, 0), PT(3950, 5700), CN(C_FRE, 3, 6), HEND}},
    {"HIGHWAY 99", {CN(C_FRE, 3, 0), PT(3300, 4500), PT(2950, 4300), HEND}},
    {"HIGHWAY 99", {CN(C_BAK, 2, 5), PT(4380, 6600), HEND}},
    {"US 101", {CN(C_LA, 0, 2), PT(4150, 7150), PT(3900, 7230), CN(C_SB, 5, 2), HEND}},
    {"US 101", {CN(C_SB, 0, 1), PT(3250, 7000), PT(3000, 6650), CN(C_SLO, 2, 4), HEND}},
    {"US 101", {CN(C_SLO, 2, 0), PT(2700, 5700), PT(2350, 5200), PT(2150, 4700), CN(C_SF, 6, 10), HEND}},
    {"PACIFIC COAST HIGHWAY", {CN(C_SLO, 0, 2), PT(2650, 6100), PT(2400, 5850), PT(2150, 5600), PT(1950, 5450), CN(C_MON, 2, 4), HEND}},
    {"PACIFIC COAST HIGHWAY", {CN(C_MON, 2, 0), PT(2290, 5120), PT(2280, 4900), CN(C_SCZ, 4, 1), HEND}},
    {"PACIFIC COAST HIGHWAY", {CN(C_SCZ, 2, 0), PT(1600, 4500), CN(C_SF, 1, 10), HEND}},
    {"GOLDEN GATE BRIDGE", {CN(C_SF, 2, 0), PT(1378, 3200), HEND}},
    {"US 101", {PT(1378, 3200), PT(1300, 2900), PT(950, 2300), PT(600, 1800), CN(C_EUR, 2, 4), HEND}},
    {"US 101", {CN(C_EUR, 2, 0), PT(400, 600), PT(450, 30), HEND}},
    {"HIGHWAY 299", {CN(C_EUR, 4, 2), PT(900, 1300), CN(C_RED, 0, 2), HEND}},
    {"HIGHWAY 29", {PT(1378, 3200), PT(1700, 3050), CN(C_NAPA, 0, 1), HEND}},
    {"BAY BRIDGE", {CN(C_SF, 11, 3), CN(C_OAK, 0, 2), HEND}},
    {"INTERSTATE 80", {CN(C_OAK, 3, 0), PT(2550, 3550), CN(C_SAC, 1, 6), HEND}},
    {"HIGHWAY 12", {CN(C_NAPA, 3, 1), PT(2400, 3100), CN(C_SAC, 0, 3), HEND}},
    {"INTERSTATE 80", {CN(C_SAC, 7, 4), PT(3150, 3300), PT(3280, 3480), PT(3480, 3420), CN(C_TAHOE, 2, 3), HEND}},
    {"INTERSTATE 10", {CN(C_LA, 20, 6), PT(5800, 7500), CN(C_PS, 0, 2), HEND}},
    {"INTERSTATE 10", {CN(C_PS, 5, 2), PT(6800, 7850), PT(7560, 7900), HEND}},
    {"INTERSTATE 15", {CN(C_SD, 8, 1), PT(6050, 8100), PT(5800, 7500), HEND}},
    {"INTERSTATE 15", {PT(5800, 7500), PT(5700, 7100), CN(C_BAR, 1, 3), HEND}},
    {"INTERSTATE 15", {CN(C_BAR, 2, 0), PT(6200, 6300), PT(7000, 5900), PT(7560, 5500), HEND}},
    {"HIGHWAY 58", {CN(C_BAK, 5, 2), PT(4800, 6350), CN(C_BAR, 0, 1), HEND}},
    {"HIGHWAY 190", {CN(C_BAR, 1, 0), PT(5700, 6000), CN(C_FUR, 1, 2), HEND}},
    {"HIGHWAY 41", {CN(C_FRE, 6, 1), PT(3950, 4600), PT(3850, 4300), CN(C_YOS, 1, 2), HEND}},
    {"HIGHWAY 62", {CN(C_PS, 2, 0), PT(6300, 7400), PT(6600, 7250), HEND}},
};
#define NHWY NP(HWYS)
static const char *hwy_name(int h) { return h >= 0 && h < NHWY ? HWYS[h].name : ""; }
static int find_node_near(float x, float z, float r) {
    for (int n = 0; n < nnode; n++) if (fabsf(nodes[n].p.x - x) < r && fabsf(nodes[n].p.z - z) < r) return n;
    return -1;
}
static P2 hp_pos(const HP *h) {
    if (h->city >= 0) { City *C = &cities[h->city]; return (P2){C->x0 + h->i * PITCH, C->z0 + h->j * PITCH}; }
    return (P2){h->x, h->z};
}
/* Catmull-Rom through the control points, a node every ~50 m */
static void build_highway(int hi) {
    const HwyDef *H = &HWYS[hi];
    int n = 0;
    while (H->p[n].city != -2) n++;
    P2 P[16];
    for (int k = 0; k < n; k++) P[k] = hp_pos(&H->p[k]);
    bool bridge = strstr(H->name, "BRIDGE") != NULL;
    int prev = -1;
    float dist = 0;
    for (int k = 0; k < n - 1; k++) {
        P2 p0 = P[k > 0 ? k - 1 : k], p1 = P[k], p2 = P[k + 1], p3 = P[k + 2 < n ? k + 2 : k + 1];
        float L = sqrtf((p2.x - p1.x) * (p2.x - p1.x) + (p2.z - p1.z) * (p2.z - p1.z));
        int steps = bridge ? (int)(L / 40) + 1 : (int)(L / 50) + 1;
        for (int s = (k == 0 ? 0 : 1); s <= steps; s++) {
            float t = (float)s / steps, t2 = t * t, t3 = t2 * t;
            float x = 0.5f * ((2 * p1.x) + (-p0.x + p2.x) * t + (2 * p0.x - 5 * p1.x + 4 * p2.x - p3.x) * t2 + (-p0.x + 3 * p1.x - 3 * p2.x + p3.x) * t3);
            float z = 0.5f * ((2 * p1.z) + (-p0.z + p2.z) * t + (2 * p0.z - 5 * p1.z + 4 * p2.z - p3.z) * t2 + (-p0.z + 3 * p1.z - 3 * p2.z + p3.z) * t3);
            int node;
            bool endpoint = (k == 0 && s == 0) || (k == n - 2 && s == steps);
            const HP *hp = (k == 0 && s == 0) ? &H->p[0] : (k == n - 2 && s == steps) ? &H->p[n - 1] : NULL;
            if (hp && hp->city >= 0) node = city_node(hp->city, hp->i, hp->j);
            else if (endpoint || s == 0 || s == steps) {
                node = find_node_near(x, z, 3);
                if (node < 0) node = add_node(V(x, 0, z), -1, 0, 0);
            } else node = add_node(V(x, 0, z), -1, 0, 0);
            if (prev >= 0) {
                dist += dist2d(nodes[prev].p, nodes[node].p);
                int e = add_edge(prev, node, hi, 8.0f);
                if (e >= 0) { edges[e].bridge = bridge; edges[e].v0 = dist - edges[e].len; }
            }
            prev = node;
        }
    }
}

/* road heights: follow the terrain, smoothed and grade-limited, pinned at cities */
static void road_heights(void) {
    for (int n = 0; n < nnode; n++) {
        if (nodes[n].city >= 0) nodes[n].p.y = cities[nodes[n].city].gy;
        else nodes[n].p.y = fmaxf(base_height(nodes[n].p.x, nodes[n].p.z), 1.5f);
    }
    /* bridge decks are level with the cities they join */
    for (int e = 0; e < nedge; e++)
        if (edges[e].bridge) { nodes[edges[e].a].p.y = 12; nodes[edges[e].b].p.y = 12; }
    for (int it = 0; it < 40; it++) {
        for (int n = 0; n < nnode; n++) {
            if (nodes[n].city >= 0 || nodes[n].deg == 0) continue;
            bool br = false;
            float sum = 0;
            int cnt = 0;
            for (int k = 0; k < nodes[n].deg; k++) {
                int e = nodes[n].e[k];
                if (edges[e].bridge) br = true;
                sum += nodes[edge_other(e, n)].p.y;
                cnt++;
            }
            if (br) continue;
            float avg = sum / cnt, own = fmaxf(base_height(nodes[n].p.x, nodes[n].p.z), 1.5f);
            nodes[n].p.y = lerpf(avg, own, it < 20 ? 0.3f : 0.05f);
        }
        /* grade limit 11% */
        for (int e = 0; e < nedge; e++) {
            if (edges[e].hwy < 0) continue;
            Node *a = &nodes[edges[e].a], *b = &nodes[edges[e].b];
            float g = edges[e].len * 0.11f, d = b->p.y - a->p.y;
            if (fabsf(d) > g) {
                float fix = (fabsf(d) - g) * (d > 0 ? 1 : -1);
                if (a->city < 0 && b->city < 0) { a->p.y += fix * 0.5f; b->p.y -= fix * 0.5f; }
                else if (a->city < 0) a->p.y += fix;
                else if (b->city < 0) b->p.y -= fix;
            }
        }
    }
}

static void build_heightmap(void) {
    hmap = malloc(sizeof(float) * (size_t)HNX * HNZ);
    for (int j = 0; j < HNZ; j++)
        for (int i = 0; i < HNX; i++) hmap[j * HNX + i] = base_height(WX0 + i * HRES, WZ0 + j * HRES);
    /* carve and fill along highways (not bridges): blend toward the road profile */
    for (int j = 0; j < HNZ; j++)
        for (int i = 0; i < HNX; i++) {
            float x = WX0 + i * HRES, z = WZ0 + j * HRES;
            float d, s;
            int e = nearest_edge(V(x, 0, z), 70, &d, &s);
            if (e < 0 || edges[e].hwy < 0) continue;
            float ry = lerpf(nodes[edges[e].a].p.y, nodes[edges[e].b].p.y, clampf(s / edges[e].len, 0, 1)) - 0.06f;
            float w = 1 - smooth01(14, 60, d);
            if (edges[e].bridge) { if (hmap[j * HNX + i] > ry - 4 && w > 0.9f) hmap[j * HNX + i] = fminf(hmap[j * HNX + i], ry - 4); continue; }
            hmap[j * HNX + i] = lerpf(hmap[j * HNX + i], ry, w);
        }
    for (int p = 0; p < npad; p++)
        for (int j = 0; j < HNZ; j++)
            for (int i = 0; i < HNX; i++) {
                float w = pad_weight(&pads[p], WX0 + i * HRES, WZ0 + j * HRES);
                if (w > 0) hmap[j * HNX + i] = lerpf(hmap[j * HNX + i], pads[p].h, w);
            }
}
/* exactly the same triangles as the ground mesh, so cars sit on what is drawn */
static float terrain_h(float x, float z) {
    float fx = (x - WX0) / HRES, fz = (z - WZ0) / HRES;
    if (fx < 0 || fz < 0 || fx >= HNX - 1 || fz >= HNZ - 1) return -16;
    int i = (int)fx, j = (int)fz;
    fx -= i; fz -= j;
    float ha = hmap[j * HNX + i], hc = hmap[j * HNX + i + 1], he = hmap[(j + 1) * HNX + i], hd = hmap[(j + 1) * HNX + i + 1];
    if (fz > fx) return ha + fx * (hd - he) + fz * (he - ha);
    return ha + fx * (hc - ha) + fz * (hd - hc);
}
static float water_level_at(float x, float z) {
    for (int l = 0; l < NLAKE; l++) {
        const Lake *L = &LAKES[l];
        float e = ((x - L->x) / L->rx) * ((x - L->x) / L->rx) + ((z - L->z) / L->rz) * ((z - L->z) / L->rz);
        if (e < 1.6f) return L->level;
    }
    if (x > 4500 && z > 4500 && z < 7000) return -1000; /* inland basins (Death Valley) are dry */
    return WATER_Y;
}
static bool water_at(float x, float z) { return terrain_h(x, z) < water_level_at(x, z) - 0.25f; }
static float out_dist(float x, float z) { return fmaxf(0, -coast_sd(x, z) + 60); } /* for the sea ambience */

static Mesh mk_ground(void) {
    FBuf b = {0};
    for (int j = 0; j < HNZ - 1; j++)
        for (int i = 0; i < HNX - 1; i++) {
            float x0 = WX0 + i * HRES, z0 = WZ0 + j * HRES, x1 = x0 + HRES, z1 = z0 + HRES;
            v3 a = V(x0, hmap[j * HNX + i], z0), c = V(x1, hmap[j * HNX + i + 1], z0);
            v3 d = V(x1, hmap[(j + 1) * HNX + i + 1], z1), e = V(x0, hmap[(j + 1) * HNX + i], z1);
            /* smooth-ish normals from the height field */
            v3 P[4] = {a, c, d, e};
            int I[4][2] = {{i, j}, {i + 1, j}, {i + 1, j + 1}, {i, j + 1}};
            v3 N[4];
            for (int k = 0; k < 4; k++) {
                int ii = I[k][0], jj = I[k][1];
                float hl = hmap[jj * HNX + (ii > 0 ? ii - 1 : ii)], hr = hmap[jj * HNX + (ii < HNX - 1 ? ii + 1 : ii)];
                float hu = hmap[(jj > 0 ? jj - 1 : jj) * HNX + ii], hdn = hmap[(jj < HNZ - 1 ? jj + 1 : jj) * HNX + ii];
                N[k] = norm(V(hl - hr, 2 * HRES, hu - hdn));
            }
            fb_push(&b, P[0], N[0]); fb_push(&b, P[3], N[3]); fb_push(&b, P[2], N[2]);
            fb_push(&b, P[0], N[0]); fb_push(&b, P[2], N[2]); fb_push(&b, P[1], N[1]);
        }
    return mesh_upload(&b);
}
static Mesh mk_ocean(void) {
    FBuf b = {0};
    float r = 20000;
    fb_tri(&b, V(-r, 0, -r), V(-r, 0, r), V(r, 0, r));
    fb_tri(&b, V(-r, 0, -r), V(r, 0, r), V(r, 0, -r));
    return mesh_upload(&b);
}

/* road surface: one mesh for every street, highway and intersection. The vertex "normal" carries
 * (across, along, type + length/1000) for the road shader instead of a normal. */
static void road_quad(FBuf *b, v3 p0, v3 p1, v3 p2, v3 p3, v3 a0, v3 a1, v3 a2, v3 a3) {
    fb_push(b, p0, a0); fb_push(b, p2, a2); fb_push(b, p1, a1);
    fb_push(b, p0, a0); fb_push(b, p3, a3); fb_push(b, p2, a2);
}
static float node_hw(int n) { float w = 5; for (int k = 0; k < nodes[n].deg; k++) w = fmaxf(w, edges[nodes[n].e[k]].hw); return w; }
static Mesh mk_roads(void) {
    FBuf b = {0};
    const float lift = 0.07f;
    for (int n = 0; n < nnode; n++) {
        Node *N = &nodes[n];
        N->inset = (N->city >= 0 || N->deg >= 3) ? node_hw(n) : 0;
        N->light = N->city >= 0 && N->deg >= 3;
    }
    for (int e = 0; e < nedge; e++) {
        Edge *E = &edges[e];
        Node *A = &nodes[E->a], *B = &nodes[E->b];
        v3 D = E->d, R = right_vec(D);
        float hw = E->hw;
        /* end points: inset at intersections, mitred at plain highway joints */
        v3 pa[2], pb[2];
        for (int end = 0; end < 2; end++) {
            int nn = end == 0 ? E->a : E->b;
            Node *Nn = &nodes[nn];
            v3 base = Nn->p;
            v3 *out = end == 0 ? pa : pb;
            if (Nn->inset > 0 || Nn->deg != 2) {
                v3 c = add(base, mul(D, end == 0 ? Nn->inset : -Nn->inset));
                out[0] = sub(c, mul(R, hw)); out[1] = add(c, mul(R, hw));
            } else {
                int other = Nn->e[0] == e ? Nn->e[1] : Nn->e[0];
                v3 D2 = edge_dir(other, nn); /* leaving nn along the other edge */
                v3 Din = end == 0 ? mul(D, -1) : D;  /* direction arriving at nn along this edge, oriented a->b */
                v3 dirA = end == 0 ? D : Din;
                (void)dirA;
                v3 Dthrough = end == 0 ? mul(D2, -1) : D2;
                v3 avg = norm(add(D, Dthrough));
                v3 Rm = right_vec(avg);
                float sc = 1 / fmaxf(dot(Rm, R), 0.5f);
                out[0] = sub(base, mul(Rm, hw * sc)); out[1] = add(base, mul(Rm, hw * sc));
            }
            out[0].y += lift; out[1].y += lift;
        }
        float L = E->len - (A->inset > 0 ? A->inset : 0) - (B->inset > 0 ? B->inset : 0);
        float type = (E->hwy >= 0 ? 1 : 0) + clampf(fmaxf(L, 1), 1, 998) / 1000.0f;
        float v0 = E->hwy >= 0 ? E->v0 : 0, v1 = E->hwy >= 0 ? E->v0 + E->len : L;
        road_quad(&b, pa[0], pb[0], pb[1], pa[1], V(-hw, v0, type), V(-hw, v1, type), V(hw, v1, type), V(hw, v0, type));
    }
    for (int n = 0; n < nnode; n++) {
        Node *N = &nodes[n];
        if (N->inset <= 0) continue;
        float w = N->inset;
        v3 p = add(N->p, V(0, lift + 0.005f, 0));
        float type = 2 + 0.010f;
        road_quad(&b, add(p, V(-w, 0, -w)), add(p, V(w, 0, -w)), add(p, V(w, 0, w)), add(p, V(-w, 0, w)), V(-w, -w, type), V(w, -w, type), V(w, w, type), V(-w, w, type));
    }
    return mesh_upload(&b);
}

/* ================================================================== regions + districts */
typedef struct { const char *name; float x, z, r; } Region;
static const Region REGIONS[] = {
    {"GOLDEN GATE BRIDGE", 1378, 3420, 160}, {"BAY BRIDGE", 2180, 3832, 200}, {"ALCATRAZ ISLAND", 1725, 3440, 60},
    {"SAN FRANCISCO BAY", 2190, 3900, 450}, {"MONTEREY BAY", 1950, 5050, 260}, {"LAKE TAHOE", 3430, 2760, 260}, {"SALTON SEA", 6450, 8150, 380},
    {"BIG SUR", 2150, 5650, 420}, {"MOUNT SHASTA", 1780, 650, 520}, {"YOSEMITE VALLEY", 3780, 4110, 330}, {"DEATH VALLEY", 5720, 5250, 700},
    {"JOSHUA TREE", 6550, 7300, 400}, {"NAPA VALLEY", 2050, 2850, 400}, {"MARIN HEADLANDS", 1250, 3000, 350}, {"REDWOOD COAST", 650, 1900, 700},
    {"SANTA CRUZ MOUNTAINS", 1650, 4350, 350}, {"SAN GABRIEL MOUNTAINS", 4900, 6850, 500}, {"THE GRAPEVINE", 4380, 6600, 260},
    {"TEHACHAPI PASS", 4800, 6350, 300}, {"SAN GORGONIO PASS", 5800, 7520, 300}, {"MALIBU", 4100, 7180, 260}, {"GAVIOTA COAST", 3200, 7050, 300},
    {"SIERRA NEVADA", 3700, 3700, 900}, {"SIERRA NEVADA", 4250, 5100, 700}, {"CENTRAL VALLEY", 3300, 4600, 1100}, {"CENTRAL VALLEY", 2500, 2400, 700},
    {"MOJAVE DESERT", 6200, 6400, 1100}, {"COLORADO DESERT", 7000, 8500, 900}, {"KLAMATH MOUNTAINS", 900, 900, 800}, {"SHASTA CASCADE", 2300, 900, 900},
    {"ANZA-BORREGO DESERT", 6100, 8800, 500}, {"CARRIZO PLAIN", 3500, 6400, 400}, {"SALINAS VALLEY", 2400, 5300, 400},
};
static int region_name(float x, float z) {
    if (x > 7520) return add_dname(z < 6800 ? "NEVADA STATE LINE" : "ARIZONA STATE LINE", ST_DESERT);
    if (z > 9120) return add_dname("MEXICO BORDER", ST_DESERT);
    if (z < 60) return add_dname("OREGON STATE LINE", ST_MOUNTAIN);
    if (water_at(x, z)) {
        for (int k = 0; k < NP(REGIONS); k++) if (strstr(REGIONS[k].name, "BAY") || strstr(REGIONS[k].name, "LAKE") || strstr(REGIONS[k].name, "SEA")) {
            float d = sqrtf((x - REGIONS[k].x) * (x - REGIONS[k].x) + (z - REGIONS[k].z) * (z - REGIONS[k].z));
            if (d < REGIONS[k].r) return add_dname(REGIONS[k].name, ST_PARK);
        }
        return add_dname("PACIFIC OCEAN", ST_PARK);
    }
    int best = -1;
    float bd = 1e9f;
    for (int k = 0; k < NP(REGIONS); k++) {
        float d = sqrtf((x - REGIONS[k].x) * (x - REGIONS[k].x) + (z - REGIONS[k].z) * (z - REGIONS[k].z)) / REGIONS[k].r;
        if (d < 1 && d < bd) { bd = d; best = k; }
    }
    if (best >= 0) return add_dname(REGIONS[best].name, ST_PARK);
    float sd = coast_sd(x, z);
    if (sd < 250) return add_dname("CALIFORNIA COAST", ST_BEACH);
    return add_dname("CALIFORNIA", ST_PARK);
}
static int district_at(v3 p) {
    int c = city_at(p.x, p.z, 0);
    if (c >= 0) {
        City *C = &cities[c];
        int i = (int)clampf(floorf((p.x - C->x0) / PITCH), 0, C->nx - 1), j = (int)clampf(floorf((p.z - C->z0) / PITCH), 0, C->nz - 1);
        return block_dist(c, i, j);
    }
    float d;
    int e = nearest_edge(p, 30, &d, NULL);
    if (e >= 0 && d < 14 && edges[e].hwy >= 0) return add_dname(hwy_name(edges[e].hwy), ST_PARK);
    return region_name(p.x, p.z);
}

/* ================================================================== biome map (read by the terrain shader) */
#define BRES 40.0f
#define BNX ((int)((WX1 - WX0) / BRES))
#define BNZ ((int)((WZ1 - WZ0) / BRES))
static unsigned char *biome;
static void biome_at(float x, float z, float *arid, float *farm, float *forest, float *special) {
    float h = base_height(x, z);
    *arid = 0.55f; *farm = 0; *forest = 0; *special = 0;
    if (z < 2800) *arid = 0.2f;
    else if (z < 4600) *arid = 0.5f;
    else if (z < 7000) *arid = 0.62f;
    else *arid = 0.58f;
    if (x > 4600 && z > 5000) *arid = 0.95f;
    if (x > 5600 && z > 7400) *arid = 0.97f;
    if (x < 1700 && z < 2800) *arid = 0.15f;
    float sd = poly_dist(x, z, R_SIERRA, NP(R_SIERRA), false);
    if (sd < 500 && h > 60) { *arid = fminf(*arid, 0.3f); *forest = smooth01(60, 140, h) * (1 - smooth01(280, 340, h)); }
    if (x < 1600 && z < 2700 && h > 10) *forest = fmaxf(*forest, 0.85f);
    if (sqrtf((x - SHASTA.x) * (x - SHASTA.x) + (z - SHASTA.z) * (z - SHASTA.z)) < 600) *forest = fmaxf(*forest, 0.7f * (1 - smooth01(250, 330, h)));
    float vd = poly_dist(x, z, V_CENTRAL, NP(V_CENTRAL), false);
    if (vd < 520 && city_at(x, z, 120) < 0) *farm = 1 - smooth01(360, 520, vd);
    if (x > 1850 && x < 2400 && z > 2650 && z < 3200 && city_at(x, z, 40) < 0) *special = 0.25f; /* Napa vineyards */
    float dv = ((x - 5720) / 160) * ((x - 5720) / 160) + ((z - 5300) / 450) * ((z - 5300) / 450);
    if (dv < 1) *special = 0.5f; /* salt flats */
    if (x > 6800 && z > 8200 && z < 9000) *special = 0.75f; /* dunes */
    if ((x - 5850) * (x - 5850) + (z - 5000) * (z - 5000) < 150 * 150) *special = 0.75f; /* Mesquite dunes */
}
static void build_biome(void) {
    biome = malloc((size_t)BNX * BNZ * 4);
    for (int j = 0; j < BNZ; j++)
        for (int i = 0; i < BNX; i++) {
            float a, f, fo, sp;
            biome_at(WX0 + (i + 0.5f) * BRES, WZ0 + (j + 0.5f) * BRES, &a, &f, &fo, &sp);
            unsigned char *o = biome + (j * BNX + i) * 4;
            o[0] = (unsigned char)(a * 255); o[1] = (unsigned char)(f * 255); o[2] = (unsigned char)(fo * 255); o[3] = (unsigned char)(sp * 255);
        }
}
