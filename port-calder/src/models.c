/* models.c - procedural vehicle and character models for Port Calder (no assets: everything is built at start-up).
 * Included by main.c after game.c. Everything here is prefixed mdl_ / MDL_ / MX_.
 * Meshes live in MESH[MS_X0 + MX_*] (24 slots). Cars are lofted bodies (side profile + tumblehome cross sections) with a
 * glass/trim mesh made from decals on the same loft; characters are articulated from smooth limb/torso/head meshes. */
#define MDL_PI 3.14159265f
#define MDL_MS 140 /* max stations in a body loft */
#define MDL_NP 15  /* points in a half cross-section */
#define MDL_NK 28  /* points around a full cross-section */
#define MDL_ND 800

/* mesh slot layout: 6 body families x (paint, dark), then shared parts */
enum { MDL_SEDAN, MDL_COUPE, MDL_MUSCLE, MDL_VAN, MDL_PICKUP, MDL_BUS, MDL_NFAM };
enum { MX_TYRE = 12, MX_RIM, MX_LAMP, MX_LIMB, MX_TORSO, MX_HEAD, MX_HAIRS, MX_HAIRL, MX_CAP, MX_SHOE, MX_SKIRT, MX_FACE, MX_COUNT };
_Static_assert(MX_COUNT <= 24, "models.c uses more than the 24 reserved mesh slots");

/* ------------------------------------------------------------------ small helpers */
static float mdl_sp(float x, float k) { return copysignf(powf(fabsf(x), k), x); }
static m4 mdl_T(v3 t) { return m4_trs(t, 0, 0, 0, V(1, 1, 1)); }
static m4 mdl_S(v3 s) { return m4_trs(V(0, 0, 0), 0, 0, 0, s); }
static m4 mdl_TS(v3 t, v3 s) { return m4_trs(t, 0, 0, 0, s); }
static m4 mdl_R(float yaw, float pitch, float roll) { return m4_trs(V(0, 0, 0), yaw, pitch, roll, V(1, 1, 1)); }
static float mdl_u8(unsigned h, int sh) { return (float)((h >> sh) & 255) / 255.0f; }

/* orthonormal frame: local +z = nz (outward), +y roughly up; scale applied per axis */
static m4 mdl_frame(v3 p, v3 nz, v3 sc) {
    nz = norm(nz);
    v3 x = cross(V(0, 1, 0), nz);
    if (len(x) < 1e-4f) x = V(1, 0, 0);
    x = norm(x);
    v3 y = cross(nz, x);
    m4 m = m4_id();
    m.m[0] = x.x * sc.x; m.m[1] = x.y * sc.x; m.m[2] = x.z * sc.x;
    m.m[4] = y.x * sc.y; m.m[5] = y.y * sc.y; m.m[6] = y.z * sc.y;
    m.m[8] = nz.x * sc.z; m.m[9] = nz.y * sc.z; m.m[10] = nz.z * sc.z;
    m.m[12] = p.x; m.m[13] = p.y; m.m[14] = p.z;
    return m;
}

/* emit a triangle whose winding is fixed to agree with its vertex normals */
static void mdl_tri(FBuf *b, v3 a, v3 na, v3 c, v3 nc, v3 d, v3 nd) {
    v3 g = cross(sub(c, a), sub(d, a));
    if (dot(g, g) < 1e-14f) return;
    if (dot(g, add(add(na, nc), nd)) < 0) { v3 t = c; c = d; d = t; t = nc; nc = nd; nd = t; }
    fb_push(b, a, na); fb_push(b, c, nc); fb_push(b, d, nd);
}

/* ------------------------------------------------------------------ vertex grids with smoothed normals */
typedef struct { int ns, nk; bool wrap; v3 *p, *n; } MdlGrid;
#define MDL_GP(g, i, j) ((g)->p[(i) * (g)->nk + (j)])
static MdlGrid mdl_grid(int ns, int nk, bool wrap) {
    MdlGrid g = {ns, nk, wrap, calloc((size_t)(ns * nk), sizeof(v3)), calloc((size_t)(ns * nk), sizeof(v3))};
    return g;
}
static void mdl_grid_free(MdlGrid *g) { free(g->p); free(g->n); g->p = g->n = NULL; }
static void mdl_grid_normals(MdlGrid *g) {
    int nj = g->wrap ? g->nk : g->nk - 1;
    for (int i = 0; i < g->ns * g->nk; i++) g->n[i] = V(0, 0, 0);
    for (int i = 0; i < g->ns - 1; i++)
        for (int j = 0; j < nj; j++) {
            int j1 = (j + 1) % g->nk;
            v3 a = MDL_GP(g, i, j), b = MDL_GP(g, i + 1, j), c = MDL_GP(g, i + 1, j1), d = MDL_GP(g, i, j1);
            v3 n1 = cross(sub(b, a), sub(d, a)), n2 = cross(sub(c, b), sub(d, b));
            g->n[i * g->nk + j] = add(g->n[i * g->nk + j], n1);
            g->n[(i + 1) * g->nk + j] = add(g->n[(i + 1) * g->nk + j], add(n1, n2));
            g->n[i * g->nk + j1] = add(g->n[i * g->nk + j1], add(n1, n2));
            g->n[(i + 1) * g->nk + j1] = add(g->n[(i + 1) * g->nk + j1], n2);
        }
    v3 c0 = V(0, 0, 0);
    for (int i = 0; i < g->ns * g->nk; i++) c0 = add(c0, g->p[i]);
    c0 = mul(c0, 1.0f / (float)(g->ns * g->nk));
    float sgn = 0;
    for (int i = 0; i < g->ns * g->nk; i++) sgn += dot(g->n[i], sub(g->p[i], c0));
    for (int i = 0; i < g->ns * g->nk; i++) {
        v3 n = sgn < 0 ? mul(g->n[i], -1) : g->n[i];
        g->n[i] = len(n) > 1e-12f ? norm(n) : V(0, 1, 0);
    }
}
static void mdl_emit_cells(FBuf *b, const MdlGrid *g, int i0, int i1, int j0, int j1, float off) {
    for (int i = i0; i < i1; i++)
        for (int j = j0; j < j1; j++) {
            int jj[2] = {j % g->nk, (j + 1) % g->nk};
            v3 P[4], N[4];
            int ci[4][2] = {{i, 0}, {i + 1, 0}, {i + 1, 1}, {i, 1}};
            for (int k = 0; k < 4; k++) {
                int idx = ci[k][0] * g->nk + jj[ci[k][1]];
                N[k] = g->n[idx];
                P[k] = add(g->p[idx], mul(N[k], off));
            }
            mdl_tri(b, P[0], N[0], P[1], N[1], P[2], N[2]);
            mdl_tri(b, P[0], N[0], P[2], N[2], P[3], N[3]);
        }
}
static void mdl_emit_grid(FBuf *b, const MdlGrid *g) { mdl_emit_cells(b, g, 0, g->ns - 1, 0, g->wrap ? g->nk : g->nk - 1, 0); }
/* bilinear point + normal on a grid at fractional (station t, ring s) */
static void mdl_surf(const MdlGrid *g, float t, float s, v3 *p, v3 *n) {
    t = clampf(t, 0, (float)g->ns - 1.0001f);
    int i = (int)t, j = (int)floorf(s);
    float ft = t - (float)i, fs = s - (float)j;
    j = ((j % g->nk) + g->nk) % g->nk;
    int j1 = (j + 1) % g->nk;
    int ix[4] = {i * g->nk + j, i * g->nk + j1, (i + 1) * g->nk + j1, (i + 1) * g->nk + j};
    float w[4] = {(1 - ft) * (1 - fs), (1 - ft) * fs, ft * fs, ft * (1 - fs)};
    v3 pp = V(0, 0, 0), nn = V(0, 0, 0);
    for (int k = 0; k < 4; k++) { pp = add(pp, mul(g->p[ix[k]], w[k])); nn = add(nn, mul(g->n[ix[k]], w[k])); }
    *p = pp;
    *n = norm(nn);
}
/* patch of the surface offset outward: glass, trim, stripes */
static void mdl_decal(FBuf *b, const MdlGrid *g, float t0, float t1, float s0, float s1, float off) {
    float ts[MDL_MS + 2], ss[40];
    int nt = 0, ns = 0;
    ts[nt++] = t0;
    for (float x = floorf(t0) + 1; x < t1 - 1e-4f && nt < MDL_MS; x += 1) ts[nt++] = x;
    ts[nt++] = t1;
    ss[ns++] = s0;
    for (float x = floorf(s0) + 1; x < s1 - 1e-4f && ns < 38; x += 1) ss[ns++] = x;
    ss[ns++] = s1;
    for (int a = 0; a < nt - 1; a++)
        for (int c = 0; c < ns - 1; c++) {
            v3 P[4], N[4];
            float tt[4] = {ts[a], ts[a + 1], ts[a + 1], ts[a]}, sv[4] = {ss[c], ss[c], ss[c + 1], ss[c + 1]};
            for (int k = 0; k < 4; k++) { mdl_surf(g, tt[k], sv[k], &P[k], &N[k]); P[k] = add(P[k], mul(N[k], off)); }
            mdl_tri(b, P[0], N[0], P[1], N[1], P[2], N[2]);
            mdl_tri(b, P[0], N[0], P[2], N[2], P[3], N[3]);
        }
}
/* flat cap closing the ring of station i */
static void mdl_cap(FBuf *b, const MdlGrid *g, int i, v3 n) {
    v3 c = V(0, 0, 0);
    for (int j = 0; j < g->nk; j++) c = add(c, MDL_GP(g, i, j));
    c = mul(c, 1.0f / (float)g->nk);
    for (int j = 0; j < g->nk; j++) mdl_tri(b, c, n, MDL_GP(g, i, j), n, MDL_GP(g, i, (j + 1) % g->nk), n);
}

/* ------------------------------------------------------------------ superellipsoid (a rounded box when e is small), unit size 1 */
static void mdl_se(FBuf *b, m4 xf, float e, int nv, int nu) {
    m4 inv = m4_inv(xf);
    for (int i = 0; i < nv; i++)
        for (int j = 0; j < nu; j++) {
            v3 P[4], N[4];
            int ci[4][2] = {{i, j}, {i + 1, j}, {i + 1, j + 1}, {i, j + 1}};
            for (int k = 0; k < 4; k++) {
                float v = -MDL_PI / 2 + MDL_PI * (float)ci[k][0] / (float)nv, th = 2 * MDL_PI * (float)ci[k][1] / (float)nu;
                float cv = cosf(v), sv = sinf(v), ct = cosf(th), st = sinf(th);
                v3 p = V(0.5f * mdl_sp(cv, e) * mdl_sp(ct, e), 0.5f * mdl_sp(sv, e), 0.5f * mdl_sp(cv, e) * mdl_sp(st, e));
                v3 n = V(mdl_sp(cv, 2 - e) * mdl_sp(ct, 2 - e), mdl_sp(sv, 2 - e), mdl_sp(cv, 2 - e) * mdl_sp(st, 2 - e));
                P[k] = m4_point(xf, p);
                v3 nn = V(inv.m[0] * n.x + inv.m[1] * n.y + inv.m[2] * n.z, inv.m[4] * n.x + inv.m[5] * n.y + inv.m[6] * n.z,
                          inv.m[8] * n.x + inv.m[9] * n.y + inv.m[10] * n.z);
                N[k] = norm(nn);
            }
            mdl_tri(b, P[0], N[0], P[1], N[1], P[2], N[2]);
            mdl_tri(b, P[0], N[0], P[2], N[2], P[3], N[3]);
        }
}
/* surface of revolution about y from (radius, height) pairs; axis 1 turns it to lie along x. Duplicate points give hard edges. */
static void mdl_revolve(FBuf *b, const float (*pr)[2], int np, int nseg, int axis, m4 xf) {
    MdlGrid g = mdl_grid(np, nseg, true);
    for (int i = 0; i < np; i++)
        for (int j = 0; j < nseg; j++) {
            float a = 2 * MDL_PI * (float)j / (float)nseg;
            v3 p = V(pr[i][0] * cosf(a), pr[i][1], pr[i][0] * sinf(a));
            if (axis == 1) p = V(p.y, p.x, p.z);
            MDL_GP(&g, i, j) = m4_point(xf, p);
        }
    mdl_grid_normals(&g);
    mdl_emit_grid(b, &g);
    mdl_grid_free(&g);
}

/* ------------------------------------------------------------------ car bodies */
typedef struct { const float (*k)[2]; int n; float blur; } MdlCv;
#define MDL_CV(a, bl) {a, (int)(sizeof(a) / sizeof((a)[0])), bl}
typedef struct { float u0, u1, s0, s1, off; int sym; } MdlDec; /* decal over stations u0..u1 (fraction of length), ring s0..s1; sym mirrors left */
typedef struct {
    float L, W, H, rnf, rnr, pf, pr, step, crown, flare, wrad, wz, wx;
    MdlCv top, belt, bot, wr, wl;
    const float *ff, *gg;
    const MdlDec *dec;
    int ndec;
    float hl[5], tl[5], iF[5], iR[5]; /* lamp: u, s, w, h, d */
    float py[2];                      /* front / rear plate height */
    float mir[4];                     /* mirror: u, size x, y, z */
    int kind;                         /* extras: 0 sedan 1 coupe 2 muscle 3 van 4 pickup 5 bus */
} MdlDef;
typedef struct {
    int ns;
    float u[MDL_MS], z[MDL_MS], w[MDL_MS], ysh[MDL_MS];
    MdlGrid g;
} MdlBody;
typedef struct {
    int paint, dark;
    float L, W, H, wx, wz, wrad, roofz, roofy;
    m4 head[2], tail[2], indf[2], indr[2], plate[2];
} MdlFam;
static MdlFam mdl_fam[MDL_NFAM];
static const int mdl_famof[VM_N] = {[VM_SEDAN] = MDL_SEDAN, [VM_TAXI] = MDL_SEDAN, [VM_SPORTS] = MDL_COUPE, [VM_VAN] = MDL_VAN,
                                    [VM_PICKUP] = MDL_PICKUP, [VM_POLICE] = MDL_SEDAN, [VM_BUS] = MDL_BUS, [VM_MUSCLE] = MDL_MUSCLE};

static float mdl_lin(const MdlCv *c, float u) {
    if (u <= c->k[0][0]) return c->k[0][1];
    for (int i = 1; i < c->n; i++)
        if (u <= c->k[i][0]) return lerpf(c->k[i - 1][1], c->k[i][1], (u - c->k[i - 1][0]) / (c->k[i][0] - c->k[i - 1][0]));
    return c->k[c->n - 1][1];
}
static void mdl_dense(const MdlCv *c, float *o, int N, float L) {
    static float tmp[MDL_ND + 1];
    for (int i = 0; i <= N; i++) o[i] = mdl_lin(c, (float)i / (float)N);
    int r = (int)(c->blur / (L / (float)N) * 0.5f + 0.5f);
    if (r < 1) return;
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i <= N; i++) {
            float s = 0;
            for (int k = -r; k <= r; k++) s += o[i + k < 0 ? 0 : i + k > N ? N : i + k];
            tmp[i] = s / (float)(2 * r + 1);
        }
        memcpy(o, tmp, sizeof(float) * (size_t)(N + 1));
    }
}
static float mdl_samp(const float *d, int N, float u) {
    float x = clampf(u, 0, 1) * (float)N;
    int i = (int)x;
    if (i >= N) return d[N];
    return lerpf(d[i], d[i + 1], x - (float)i);
}
static const float mdl_ff0[6] = {0, 0.25f, 0.55f, 0.80f, 0.95f, 1.0f}, mdl_gg0[6] = {0, 0.22f, 0.52f, 0.80f, 0.93f, 0.995f};

static void mdl_section(v3 *r, float z, float w, float yb, float ysh, float yt, float wr, float crown, float bulge, const float *ff, const float *gg) {
    float rs = fmaxf(0, fminf(0.10f, fminf(0.3f * (ysh - yb), 0.25f * w)));
    float cx = w - rs, cy = yb + rs;
    r[0] = V(0, yb, z);
    r[1] = V(0.5f * cx, yb, z);
    r[2] = V(cx + rs * cosf(-0.96f), cy + rs * sinf(-0.96f), z);
    r[3] = V(cx + rs * cosf(-0.35f), cy + rs * sinf(-0.35f), z);
    r[4] = V(w, cy, z);
    float y6 = fmaxf(cy + 0.02f, ysh - 0.08f * fminf(1, (ysh - cy) / 0.2f));
    r[5] = V(w + bulge, 0.5f * (cy + y6), z);
    r[6] = V(w, y6, z);
    float xs = w * 0.992f, hc = yt - ysh;
    r[7] = V(xs, ysh, z);
    for (int m = 1; m <= 5; m++) r[7 + m] = V(lerpf(xs, wr, ff[m]), ysh + hc * gg[m], z);
    float cr = fminf(crown, 0.5f * hc);
    r[12].y = yt - cr * 0.7f;
    r[13] = V(wr * 0.55f, yt - cr * 0.3f, z);
    r[14] = V(0, yt, z);
}

static void mdl_body(const MdlDef *d, MdlBody *B) {
    float L = d->L, W = d->W;
    int N = (int)(L / 0.02f) + 1;
    if (N > MDL_ND - 1) N = MDL_ND - 1;
    static float top[MDL_ND + 1], belt[MDL_ND + 1], bot[MDL_ND + 1], wrf[MDL_ND + 1], wlf[MDL_ND + 1];
    mdl_dense(&d->top, top, N, L); mdl_dense(&d->belt, belt, N, L); mdl_dense(&d->bot, bot, N, L);
    mdl_dense(&d->wr, wrf, N, L); mdl_dense(&d->wl, wlf, N, L);
    float us[MDL_MS], pfs[MDL_MS];
    int n = 0;
    const int NN = 8;
    bool fr = d->rnf > 0.01f, rr = d->rnr > 0.01f;
    float u0 = fr ? d->rnf / L : 0, u1 = 1 - (rr ? d->rnr / L : 0);
    if (fr) for (int k = 0; k <= NN; k++) {
        float th = (MDL_PI / 2) * powf((float)k / NN, 1.6f), a = cosf(th);
        us[n] = d->rnf * (1 - a) / L;
        pfs[n++] = powf(fmaxf(0, 1 - powf(a, d->pf)), 1 / d->pf);
    } else { us[n] = 0; pfs[n++] = 1; }
    float mid[MDL_MS];
    int nm = 0;
    for (int i = 0; i < d->ndec; i++) {
        float c2[2] = {d->dec[i].u0, d->dec[i].u1};
        for (int k = 0; k < 2; k++) if (c2[k] > u0 + 0.004f && c2[k] < u1 - 0.004f && nm < 48) mid[nm++] = c2[k];
    }
    for (float x = u0 + d->step / L; x < u1 - 0.5f * d->step / L && nm < MDL_MS - 40; x += d->step / L) mid[nm++] = x;
    for (int i = 1; i < nm; i++) { float v = mid[i]; int j = i - 1; while (j >= 0 && mid[j] > v) { mid[j + 1] = mid[j]; j--; } mid[j + 1] = v; }
    float last = u0;
    for (int i = 0; i < nm; i++) if (mid[i] - last > 0.0045f && u1 - mid[i] > 0.0045f) { us[n] = mid[i]; pfs[n++] = 1; last = mid[i]; }
    if (rr) for (int k = NN; k >= 0; k--) {
        float th = (MDL_PI / 2) * powf((float)k / NN, 1.6f), a = cosf(th);
        us[n] = 1 - d->rnr * (1 - a) / L;
        pfs[n++] = powf(fmaxf(0, 1 - powf(a, d->pr)), 1 / d->pr);
    } else { us[n] = 1; pfs[n++] = 1; }
    B->ns = n;
    B->g = mdl_grid(n, MDL_NK, true);
    const float *ff = d->ff ? d->ff : mdl_ff0, *gg = d->gg ? d->gg : mdl_gg0;
    for (int i = 0; i < n; i++) {
        float u = us[i], z = -L / 2 + u * L;
        float yt = mdl_samp(top, N, u), bl = mdl_samp(belt, N, u), yb = mdl_samp(bot, N, u);
        float wl = (W / 2 - 0.035f) * pfs[i] * mdl_samp(wlf, N, u);
        float bump = d->flare * expf(-powf((fabsf(z) - d->wz) / 0.5f, 4));
        float w = wl + bump * pfs[i];
        float wr = fminf(w, wl * mdl_samp(wrf, N, u));
        float ysh = fminf(bl, yt - 0.03f);
        if (ysh < yb + 0.12f) ysh = yb + 0.12f;
        if (yt < ysh + 0.01f) yt = ysh + 0.01f;
        v3 r[MDL_NP];
        mdl_section(r, z, w, yb, ysh, yt, wr, d->crown, 0.012f * pfs[i], ff, gg);
        for (int j = 0; j < MDL_NP; j++) MDL_GP(&B->g, i, j) = r[j];
        for (int j = 15; j < MDL_NK; j++) { v3 q = r[MDL_NK - j]; MDL_GP(&B->g, i, j) = V(-q.x, q.y, q.z); }
        B->u[i] = u; B->z[i] = z; B->w[i] = w + 0.0f; B->ysh[i] = ysh;
    }
    mdl_grid_normals(&B->g);
}
static float mdl_tu(const MdlBody *B, float u) {
    for (int i = 0; i < B->ns - 1; i++) if (u <= B->u[i + 1]) return (float)i + (u - B->u[i]) / fmaxf(1e-6f, B->u[i + 1] - B->u[i]);
    return (float)B->ns - 1;
}
static float mdl_wat(const MdlBody *B, float z) { /* body half width near z */
    for (int i = 0; i < B->ns - 1; i++) if (z <= B->z[i + 1]) return lerpf(B->w[i], B->w[i + 1], (z - B->z[i]) / fmaxf(1e-6f, B->z[i + 1] - B->z[i]));
    return B->w[B->ns - 1];
}
static void mdl_flatq(FBuf *b, v3 a, v3 c, v3 d, v3 e, v3 n) { mdl_tri(b, a, n, c, n, d, n); mdl_tri(b, a, n, d, n, e, n); }
/* black wheel-arch disc on the body side (the tyre sits in front of it) */
static void mdl_arch(FBuf *b, float x, float cy, float cz, float R, float yc, float dir) {
    const int NA = 28, NR = 3;
    v3 n = V(dir, 0, 0);
    for (int i = 0; i < NR; i++)
        for (int j = 0; j < NA; j++) {
            v3 P[4];
            float rr[2] = {R * (float)i / NR, R * (float)(i + 1) / NR};
            float aa[2] = {2 * MDL_PI * (float)j / NA, 2 * MDL_PI * (float)(j + 1) / NA};
            int ci[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
            for (int k = 0; k < 4; k++) {
                float r = rr[ci[k][0]], a = aa[ci[k][1]];
                P[k] = V(x, fmaxf(yc, cy + r * cosf(a)), cz + r * sinf(a));
            }
            mdl_flatq(b, P[0], P[1], P[2], P[3], n);
        }
}

/* ---- per-model definitions (reference dimensions are the SPEC values; the game scales slightly per spec) */
static const float mdl_sed_top[][2] = {{0, 0.58f}, {0.02f, 0.72f}, {0.06f, 0.80f}, {0.12f, 0.855f}, {0.30f, 0.925f}, {0.335f, 0.94f}, {0.47f, 1.445f}, {0.50f, 1.452f}, {0.62f, 1.445f}, {0.66f, 1.40f}, {0.805f, 1.0f}, {0.83f, 0.995f}, {0.95f, 0.985f}, {0.985f, 0.93f}, {1, 0.74f}};
static const float mdl_sed_belt[][2] = {{0, 0.74f}, {0.15f, 0.85f}, {0.33f, 0.92f}, {0.6f, 0.96f}, {0.82f, 0.99f}, {1, 0.96f}};
static const float mdl_sed_bot[][2] = {{0, 0.34f}, {0.05f, 0.25f}, {0.95f, 0.25f}, {1, 0.37f}};
static const float mdl_sed_wr[][2] = {{0, 0.88f}, {0.30f, 0.90f}, {0.34f, 0.86f}, {0.47f, 0.72f}, {0.65f, 0.72f}, {0.81f, 0.86f}, {1, 0.86f}};
static const float mdl_sed_wl[][2] = {{0, 1}, {0.7f, 1}, {1, 0.95f}};
static const MdlDec mdl_sed_dec[] = {
    {0.335f, 0.47f, 12.15f, 15.85f, 0.012f, 0}, {0.65f, 0.80f, 12.15f, 15.85f, 0.012f, 0},
    {0.395f, 0.52f, 7.3f, 11.0f, 0.012f, 1}, {0.56f, 0.70f, 7.3f, 11.0f, 0.012f, 1}, {0.70f, 0.74f, 7.6f, 10.2f, 0.012f, 1},
    {0.0f, 0.045f, 3.0f, 5.2f, 0.007f, 1},   /* lower grille / intake */
    {0.0f, 0.03f, 5.5f, 7.0f, 0.006f, 1},    /* upper grille */
    {0.0f, 1.0f, 2.2f, 3.7f, 0.005f, 1},     /* rocker sill */
    {0.52f, 0.5275f, 4.2f, 7.4f, 0.004f, 1}, {0.35f, 0.3575f, 4.2f, 7.4f, 0.004f, 1}, {0.71f, 0.7175f, 4.2f, 7.4f, 0.004f, 1}, /* door lines */
    {0.40f, 0.44f, 6.9f, 7.25f, 0.007f, 1}, {0.605f, 0.645f, 6.9f, 7.25f, 0.007f, 1}, /* handles */
    {0.955f, 1.0f, 2.2f, 4.2f, 0.007f, 1}};  /* rear valance */
static const float mdl_cou_top[][2] = {{0, 0.45f}, {0.02f, 0.56f}, {0.07f, 0.64f}, {0.20f, 0.72f}, {0.37f, 0.80f}, {0.405f, 0.82f}, {0.52f, 1.19f}, {0.56f, 1.20f}, {0.64f, 1.19f}, {0.76f, 0.95f}, {0.82f, 0.92f}, {0.95f, 0.92f}, {0.985f, 0.88f}, {1, 0.70f}};
static const float mdl_cou_belt[][2] = {{0, 0.55f}, {0.25f, 0.70f}, {0.4f, 0.76f}, {0.8f, 0.84f}, {1, 0.80f}};
static const float mdl_cou_bot[][2] = {{0, 0.27f}, {0.05f, 0.19f}, {0.95f, 0.19f}, {1, 0.28f}};
static const float mdl_cou_wr[][2] = {{0, 0.9f}, {0.38f, 0.88f}, {0.52f, 0.68f}, {0.64f, 0.68f}, {0.78f, 0.88f}, {1, 0.9f}};
static const float mdl_cou_wl[][2] = {{0, 1}, {0.8f, 1.02f}, {1, 1}};
static const MdlDec mdl_cou_dec[] = {
    {0.405f, 0.52f, 12.15f, 15.85f, 0.012f, 0}, {0.64f, 0.77f, 12.15f, 15.85f, 0.012f, 0},
    {0.45f, 0.64f, 7.3f, 11.0f, 0.012f, 1},
    {0.0f, 0.05f, 2.8f, 5.0f, 0.007f, 1}, {0.0f, 0.03f, 5.3f, 6.6f, 0.006f, 1},
    {0.0f, 1.0f, 2.0f, 3.4f, 0.005f, 1},
    {0.43f, 0.4375f, 3.8f, 7.2f, 0.004f, 1}, {0.665f, 0.6725f, 3.8f, 7.2f, 0.004f, 1},
    {0.49f, 0.52f, 6.7f, 7.0f, 0.007f, 1},
    {0.94f, 1.0f, 2.0f, 4.5f, 0.007f, 1},
    {0.24f, 0.30f, 5.4f, 6.6f, 0.007f, 1}}; /* side vent */
static const float mdl_mus_top[][2] = {{0, 0.62f}, {0.02f, 0.78f}, {0.07f, 0.9f}, {0.40f, 0.98f}, {0.43f, 1.0f}, {0.52f, 1.27f}, {0.56f, 1.30f}, {0.66f, 1.29f}, {0.82f, 1.05f}, {0.90f, 1.03f}, {0.97f, 1.02f}, {0.99f, 0.95f}, {1, 0.85f}};
static const float mdl_mus_belt[][2] = {{0, 0.78f}, {0.4f, 0.97f}, {0.7f, 1.02f}, {1, 0.98f}};
static const float mdl_mus_bot[][2] = {{0, 0.33f}, {0.05f, 0.24f}, {0.95f, 0.24f}, {1, 0.36f}};
static const float mdl_mus_wr[][2] = {{0, 0.9f}, {0.41f, 0.9f}, {0.52f, 0.72f}, {0.66f, 0.72f}, {0.84f, 0.88f}, {1, 0.9f}};
static const float mdl_mus_wl[][2] = {{0, 1}, {0.75f, 1.01f}, {1, 0.97f}};
static const MdlDec mdl_mus_dec[] = {
    {0.425f, 0.54f, 12.15f, 15.85f, 0.012f, 0}, {0.68f, 0.83f, 12.15f, 15.85f, 0.012f, 0},
    {0.475f, 0.68f, 7.3f, 11.0f, 0.012f, 1},
    {0.0f, 0.04f, 3.0f, 5.2f, 0.007f, 1}, {0.0f, 0.025f, 5.5f, 7.0f, 0.006f, 1},
    {0.0f, 1.0f, 2.2f, 3.6f, 0.005f, 1},
    {0.46f, 0.4675f, 4.0f, 7.4f, 0.004f, 1}, {0.70f, 0.7075f, 4.0f, 7.4f, 0.004f, 1},
    {0.50f, 0.53f, 6.9f, 7.2f, 0.007f, 1},
    {0.955f, 1.0f, 2.2f, 4.2f, 0.007f, 1},
    {0.0f, 0.43f, 13.3f, 13.75f, 0.006f, 0}, {0.0f, 0.43f, 14.25f, 14.7f, 0.006f, 0}, {0.55f, 0.66f, 13.3f, 13.75f, 0.006f, 0}, {0.55f, 0.66f, 14.25f, 14.7f, 0.006f, 0},
    {0.80f, 1.0f, 13.3f, 13.75f, 0.006f, 0}, {0.80f, 1.0f, 14.25f, 14.7f, 0.006f, 0}}; /* racing stripes */
static const float mdl_van_top[][2] = {{0, 0.80f}, {0.02f, 0.98f}, {0.06f, 1.07f}, {0.17f, 1.15f}, {0.20f, 1.17f}, {0.29f, 2.12f}, {0.33f, 2.27f}, {0.50f, 2.30f}, {0.97f, 2.30f}, {0.99f, 2.2f}, {1, 2.0f}};
static const float mdl_van_belt[][2] = {{0, 0.85f}, {0.2f, 1.18f}, {1, 1.18f}};
static const float mdl_van_bot[][2] = {{0, 0.38f}, {0.05f, 0.30f}, {0.95f, 0.30f}, {1, 0.40f}};
static const float mdl_van_wr[][2] = {{0, 0.88f}, {0.2f, 0.90f}, {0.31f, 0.94f}, {1, 0.95f}};
static const float mdl_van_wl[][2] = {{0, 1}, {1, 1}};
static const MdlDec mdl_van_dec[] = {
    {0.20f, 0.30f, 12.15f, 15.85f, 0.012f, 0},
    {0.225f, 0.335f, 7.4f, 11.0f, 0.012f, 1},
    {0.0f, 0.04f, 3.2f, 5.5f, 0.007f, 1}, {0.0f, 0.03f, 5.6f, 7.2f, 0.006f, 1},
    {0.0f, 1.0f, 2.2f, 3.5f, 0.005f, 1},
    {0.20f, 0.2075f, 4.0f, 7.2f, 0.004f, 1}, {0.34f, 0.3475f, 4.0f, 10.8f, 0.004f, 1},
    {0.54f, 0.5475f, 4.0f, 10.8f, 0.004f, 0}, {0.82f, 0.8275f, 4.0f, 10.8f, 0.004f, 0}, /* sliding door (right side) */
    {0.28f, 0.31f, 6.5f, 6.9f, 0.007f, 1},
    {0.985f, 1.0f, 8.0f, 11.0f, 0.012f, 1},
    {0.975f, 1.0f, 2.2f, 4.5f, 0.007f, 1}};
static const float mdl_pik_top[][2] = {{0, 0.75f}, {0.02f, 0.92f}, {0.06f, 1.02f}, {0.25f, 1.10f}, {0.295f, 1.12f}, {0.405f, 1.82f}, {0.43f, 1.85f}, {0.575f, 1.84f}, {0.60f, 1.75f}, {0.612f, 1.05f}, {0.64f, 0.96f}, {0.97f, 0.95f}, {0.99f, 0.90f}, {1, 0.80f}};
static const float mdl_pik_belt[][2] = {{0, 0.80f}, {0.3f, 1.1f}, {0.6f, 1.1f}, {0.62f, 0.93f}, {1, 0.93f}};
static const float mdl_pik_bot[][2] = {{0, 0.40f}, {0.05f, 0.30f}, {0.95f, 0.30f}, {1, 0.40f}};
static const float mdl_pik_wr[][2] = {{0, 0.9f}, {0.3f, 0.92f}, {0.41f, 0.82f}, {0.58f, 0.82f}, {0.62f, 0.96f}, {1, 0.96f}};
static const float mdl_pik_wl[][2] = {{0, 1}, {1, 1}};
static const MdlDec mdl_pik_dec[] = {
    {0.295f, 0.41f, 12.15f, 15.85f, 0.012f, 0}, {0.578f, 0.60f, 12.3f, 15.7f, 0.012f, 0},
    {0.335f, 0.415f, 7.3f, 11.0f, 0.012f, 1}, {0.44f, 0.57f, 7.3f, 11.0f, 0.012f, 1},
    {0.0f, 0.04f, 3.2f, 5.4f, 0.007f, 1}, {0.0f, 0.03f, 5.6f, 7.1f, 0.006f, 1},
    {0.0f, 1.0f, 2.2f, 3.5f, 0.005f, 1},
    {0.30f, 0.3075f, 4.0f, 7.2f, 0.004f, 1}, {0.425f, 0.4325f, 4.0f, 7.2f, 0.004f, 1}, {0.575f, 0.5825f, 4.0f, 7.2f, 0.004f, 1},
    {0.64f, 0.975f, 12.9f, 15.1f, 0.006f, 0},
    {0.975f, 1.0f, 2.2f, 4.5f, 0.007f, 1}};
static const float mdl_bus_top[][2] = {{0, 2.95f}, {0.01f, 3.08f}, {0.03f, 3.2f}, {0.5f, 3.22f}, {0.98f, 3.2f}, {1, 3.05f}};
static const float mdl_bus_belt[][2] = {{0, 1.0f}, {1, 1.0f}};
static const float mdl_bus_bot[][2] = {{0, 0.5f}, {0.02f, 0.42f}, {0.98f, 0.42f}, {1, 0.5f}};
static const float mdl_bus_wr[][2] = {{0, 0.96f}, {1, 0.96f}};
static const float mdl_bus_wl[][2] = {{0, 1}, {1, 1}};
static const float mdl_bus_ff[6] = {0, 0.10f, 0.25f, 0.45f, 0.75f, 1.0f}, mdl_bus_gg[6] = {0, 0.12f, 0.33f, 0.62f, 0.93f, 0.995f};
static const MdlDec mdl_bus_dec[] = {
    {0.0f, 0.04f, 7.5f, 10.4f, 0.012f, 1},
    {0.11f, 0.255f, 7.6f, 10.25f, 0.012f, 1}, {0.27f, 0.405f, 7.6f, 10.25f, 0.012f, 1}, {0.42f, 0.5f, 7.6f, 10.25f, 0.012f, 1},
    {0.62f, 0.76f, 7.6f, 10.25f, 0.012f, 1}, {0.775f, 0.91f, 7.6f, 10.25f, 0.012f, 1},
    {0.05f, 0.105f, 3.8f, 10.3f, 0.010f, 0}, {0.51f, 0.61f, 3.8f, 10.3f, 0.010f, 0}, /* doors on the right */
    {0.0f, 0.04f, 3.4f, 7.0f, 0.007f, 1},                                              /* lower front panel */
    {0.0f, 1.0f, 2.4f, 3.4f, 0.005f, 1}, {0.97f, 1.0f, 8.0f, 10.0f, 0.012f, 1}};
static const MdlDef mdl_defs[MDL_NFAM] = {
    [MDL_SEDAN] = {4.6f, 1.9f, 1.45f, 0.7f, 0.6f, 2.6f, 2.4f, 0.10f, 0.03f, 0.03f, 0.34f, 1.4f, 0.855f,
                   MDL_CV(mdl_sed_top, 0.2f), MDL_CV(mdl_sed_belt, 0.1f), MDL_CV(mdl_sed_bot, 0.05f), MDL_CV(mdl_sed_wr, 0.2f), MDL_CV(mdl_sed_wl, 0.1f),
                   NULL, NULL, mdl_sed_dec, (int)(sizeof(mdl_sed_dec) / sizeof(mdl_sed_dec[0])),
                   {0.040f, 6.6f, 0.34f, 0.12f, 0.05f}, {0.975f, 6.6f, 0.34f, 0.12f, 0.05f}, {0.02f, 4.6f, 0.2f, 0.06f, 0.04f}, {0.985f, 4.6f, 0.2f, 0.06f, 0.04f},
                   {0.45f, 0.30f}, {0.35f, 0.08f, 0.09f, 0.16f}, 0},
    [MDL_COUPE] = {4.4f, 2.0f, 1.2f, 0.8f, 0.6f, 2.3f, 2.2f, 0.10f, 0.025f, 0.035f, 0.35f, 1.35f, 0.905f,
                   MDL_CV(mdl_cou_top, 0.2f), MDL_CV(mdl_cou_belt, 0.1f), MDL_CV(mdl_cou_bot, 0.05f), MDL_CV(mdl_cou_wr, 0.2f), MDL_CV(mdl_cou_wl, 0.1f),
                   NULL, NULL, mdl_cou_dec, (int)(sizeof(mdl_cou_dec) / sizeof(mdl_cou_dec[0])),
                   {0.040f, 6.4f, 0.36f, 0.09f, 0.05f}, {0.97f, 6.2f, 0.34f, 0.09f, 0.05f}, {0.02f, 4.4f, 0.2f, 0.05f, 0.04f}, {0.985f, 4.4f, 0.2f, 0.05f, 0.04f},
                   {0.36f, 0.26f}, {0.40f, 0.075f, 0.08f, 0.15f}, 1},
    [MDL_MUSCLE] = {4.9f, 2.0f, 1.3f, 0.7f, 0.55f, 2.6f, 2.4f, 0.10f, 0.03f, 0.035f, 0.36f, 1.5f, 0.905f,
                    MDL_CV(mdl_mus_top, 0.2f), MDL_CV(mdl_mus_belt, 0.1f), MDL_CV(mdl_mus_bot, 0.05f), MDL_CV(mdl_mus_wr, 0.2f), MDL_CV(mdl_mus_wl, 0.1f),
                    NULL, NULL, mdl_mus_dec, (int)(sizeof(mdl_mus_dec) / sizeof(mdl_mus_dec[0])),
                    {0.036f, 6.5f, 0.34f, 0.12f, 0.05f}, {0.975f, 6.6f, 0.36f, 0.11f, 0.05f}, {0.02f, 4.6f, 0.2f, 0.06f, 0.04f}, {0.985f, 4.6f, 0.2f, 0.06f, 0.04f},
                    {0.45f, 0.30f}, {0.43f, 0.08f, 0.09f, 0.15f}, 2},
    [MDL_VAN] = {5.2f, 2.1f, 2.3f, 0.5f, 0.25f, 3.0f, 4.0f, 0.12f, 0.03f, 0.03f, 0.38f, 1.6f, 0.955f,
                 MDL_CV(mdl_van_top, 0.15f), MDL_CV(mdl_van_belt, 0.1f), MDL_CV(mdl_van_bot, 0.05f), MDL_CV(mdl_van_wr, 0.15f), MDL_CV(mdl_van_wl, 0.1f),
                 NULL, NULL, mdl_van_dec, (int)(sizeof(mdl_van_dec) / sizeof(mdl_van_dec[0])),
                 {0.045f, 6.4f, 0.34f, 0.14f, 0.05f}, {0.985f, 6.5f, 0.12f, 0.5f, 0.05f}, {0.02f, 4.8f, 0.2f, 0.07f, 0.04f}, {0.99f, 4.8f, 0.2f, 0.07f, 0.04f},
                 {0.55f, 0.5f}, {0.22f, 0.10f, 0.14f, 0.20f}, 3},
    [MDL_PICKUP] = {5.3f, 2.0f, 1.85f, 0.5f, 0.3f, 3.0f, 3.5f, 0.12f, 0.03f, 0.03f, 0.40f, 1.7f, 0.905f,
                    MDL_CV(mdl_pik_top, 0.08f), MDL_CV(mdl_pik_belt, 0.08f), MDL_CV(mdl_pik_bot, 0.05f), MDL_CV(mdl_pik_wr, 0.1f), MDL_CV(mdl_pik_wl, 0.1f),
                    NULL, NULL, mdl_pik_dec, (int)(sizeof(mdl_pik_dec) / sizeof(mdl_pik_dec[0])),
                    {0.040f, 6.5f, 0.34f, 0.14f, 0.05f}, {0.985f, 6.2f, 0.40f, 0.14f, 0.05f}, {0.02f, 4.8f, 0.2f, 0.07f, 0.04f}, {0.99f, 4.8f, 0.2f, 0.07f, 0.04f},
                    {0.5f, 0.45f}, {0.285f, 0.10f, 0.14f, 0.20f}, 4},
    [MDL_BUS] = {11.0f, 2.6f, 3.2f, 0.5f, 0.45f, 3.2f, 3.2f, 0.25f, 0.06f, 0.0f, 0.50f, 3.9f, 1.175f,
                 MDL_CV(mdl_bus_top, 0.05f), MDL_CV(mdl_bus_belt, 0.1f), MDL_CV(mdl_bus_bot, 0.05f), MDL_CV(mdl_bus_wr, 0.15f), MDL_CV(mdl_bus_wl, 0.1f),
                 mdl_bus_ff, mdl_bus_gg, mdl_bus_dec, (int)(sizeof(mdl_bus_dec) / sizeof(mdl_bus_dec[0])),
                 {0.012f, 4.8f, 0.34f, 0.18f, 0.05f}, {0.99f, 4.8f, 0.30f, 0.22f, 0.05f}, {0.01f, 4.2f, 0.2f, 0.10f, 0.04f}, {0.99f, 4.2f, 0.2f, 0.10f, 0.04f},
                 {0.55f, 0.6f}, {0.07f, 0.12f, 0.16f, 0.30f}, 5},
};

/* paint-coloured extras per model kind (mirrors, spoiler, hood scoop, pickup bed), dark extras (exhaust, bed liner) */
static void mdl_extras(const MdlDef *d, const MdlBody *B, FBuf *paint, FBuf *dark) {
    float L = d->L;
    /* door mirrors */
    float mz = -L / 2 + d->mir[0] * L, mw = mdl_wat(B, mz);
    int mi = (int)mdl_tu(B, d->mir[0]);
    float my = B->ysh[mi < B->ns ? mi : B->ns - 1] + d->mir[3] * 0.5f;
    if (d->kind == 5) my = 1.75f;
    for (int s = -1; s <= 1; s += 2) {
        mdl_se(paint, mdl_TS(V(s * (mw + d->mir[1] * 0.5f + (d->kind == 5 ? 0.12f : 0.0f)), my, mz), V(d->mir[1], d->mir[2], d->mir[3])), 0.55f, 8, 10);
        mdl_se(dark, mdl_TS(V(s * (mw + 0.03f), my - 0.02f, mz + 0.03f), V(0.12f, 0.025f, 0.05f)), 0.5f, 6, 8);
    }
    float tz = L / 2;
    switch (d->kind) {
    case 1: /* spoiler */
        mdl_se(paint, mdl_TS(V(0, 1.06f, tz - 0.32f), V(1.5f, 0.035f, 0.28f)), 0.35f, 8, 14);
        for (int s = -1; s <= 1; s += 2) {
            mdl_se(paint, mdl_TS(V(s * 0.62f, 0.99f, tz - 0.32f), V(0.05f, 0.14f, 0.12f)), 0.4f, 6, 8);
            mdl_se(paint, mdl_TS(V(s * 0.75f, 1.07f, tz - 0.32f), V(0.025f, 0.09f, 0.3f)), 0.4f, 6, 8);
        }
        for (int s = -1; s <= 1; s += 2) mdl_se(dark, mdl_TS(V(s * 0.55f, 0.28f, tz + 0.005f), V(0.14f, 0.07f, 0.1f)), 0.8f, 8, 12);
        break;
    case 2: /* hood scoop + exhausts */
        mdl_se(paint, mdl_TS(V(0, 0.99f, -L / 2 + 0.26f * L), V(0.55f, 0.15f, 0.8f)), 0.5f, 8, 14);
        mdl_se(dark, mdl_TS(V(0, 1.02f, -L / 2 + 0.26f * L - 0.32f), V(0.4f, 0.05f, 0.1f)), 0.5f, 6, 10);
        for (int s = -1; s <= 1; s += 2) mdl_se(dark, mdl_TS(V(s * 0.6f, 0.26f, tz + 0.01f), V(0.15f, 0.09f, 0.1f)), 0.8f, 8, 12);
        break;
    case 4: { /* open bed: side walls, tailgate, wheel wells, front bulkhead cap */
        float z0 = -L / 2 + 0.625f * L, z1 = L / 2 - 0.035f, bw = mdl_wat(B, 0.5f * (z0 + z1)) - 0.06f;
        for (int s = -1; s <= 1; s += 2) {
            mdl_se(paint, mdl_TS(V(s * bw, 1.03f, 0.5f * (z0 + z1)), V(0.12f, 0.22f, z1 - z0)), 0.3f, 6, 10);
            mdl_se(paint, mdl_TS(V(s * (bw - 0.3f), 1.0f, d->wz), V(0.22f, 0.2f, 0.9f)), 0.5f, 8, 10);
        }
        mdl_se(paint, mdl_TS(V(0, 1.03f, z1), V(2 * bw + 0.12f, 0.22f, 0.09f)), 0.3f, 6, 10);
        mdl_se(paint, mdl_TS(V(0, 1.0f, z0 + 0.04f), V(2 * bw, 0.18f, 0.08f)), 0.3f, 6, 10);
        break;
    }
    case 3: /* roof vent */
        mdl_se(paint, mdl_TS(V(0, 2.31f, 0.3f), V(0.6f, 0.07f, 0.9f)), 0.4f, 6, 10);
        break;
    default: break;
    }
}

static void mdl_build_car(int fam) {
    const MdlDef *d = &mdl_defs[fam];
    MdlBody B;
    mdl_body(d, &B);
    FBuf paint = {0}, dark = {0};
    mdl_emit_grid(&paint, &B.g);
    if (d->rnf <= 0.01f) mdl_cap(&paint, &B.g, 0, V(0, 0, -1));
    if (d->rnr <= 0.01f) mdl_cap(&paint, &B.g, B.ns - 1, V(0, 0, 1));
    for (int i = 0; i < d->ndec; i++) {
        const MdlDec *c = &d->dec[i];
        float t0 = mdl_tu(&B, c->u0), t1 = mdl_tu(&B, c->u1);
        if (t1 - t0 < 1e-3f) continue;
        mdl_decal(&dark, &B.g, t0, t1, c->s0, c->s1, c->off);
        if (c->sym) mdl_decal(&dark, &B.g, t0, t1, MDL_NK - c->s1, MDL_NK - c->s0, c->off);
    }
    float cy = d->wrad, yc = 0.36f;
    for (int k = 0; k < 4; k++) {
        float sx = k & 1 ? 1 : -1, sz = k & 2 ? 1 : -1, cz = sz * d->wz;
        mdl_arch(&dark, sx * (mdl_wat(&B, cz) + 0.004f), cy, cz, d->wrad + 0.075f, d->kind == 3 || d->kind == 4 ? 0.44f : d->kind == 5 ? 0.9f : yc, sx);
    }
    mdl_extras(d, &B, &paint, &dark);
    MdlFam *F = &mdl_fam[fam];
    F->paint = MS_X0 + fam * 2; F->dark = MS_X0 + fam * 2 + 1;
    F->L = d->L; F->W = d->W; F->H = d->H; F->wx = d->wx; F->wz = d->wz; F->wrad = d->wrad;
    F->roofz = 0; F->roofy = d->H;
    const float *lamps[4] = {d->hl, d->tl, d->iF, d->iR};
    m4 *dst[4] = {F->head, F->tail, F->indf, F->indr};
    for (int k = 0; k < 4; k++)
        for (int s = 0; s < 2; s++) {
            const float *l = lamps[k];
            v3 p, n;
            mdl_surf(&B.g, mdl_tu(&B, l[0]), s == 1 ? l[1] : MDL_NK - l[1], &p, &n);
            dst[k][s] = mdl_frame(p, n, V(l[2] * (k == 0 ? 0.8f : 1), l[3] * (k == 0 ? 0.75f : 1), l[4]));
        }
    for (int s = 0; s < 2; s++) {
        float z = s == 0 ? -d->L / 2 - 0.006f : d->L / 2 + 0.006f;
        F->plate[s] = mdl_frame(V(0, d->py[s], z), V(0, 0, s == 0 ? -1 : 1), V(0.52f, 0.12f, 0.02f));
    }
    MESH[F->paint] = mesh_upload(&paint);
    MESH[F->dark] = mesh_upload(&dark);
    mdl_grid_free(&B.g);
}

/* ------------------------------------------------------------------ wheels, lamp */
static void mdl_build_wheels(void) {
    FBuf t = {0}, r = {0};
    static const float tyre[][2] = {{0.60f, -0.27f}, {0.66f, -0.335f}, {0.80f, -0.355f}, {0.90f, -0.34f}, {0.965f, -0.29f}, {0.995f, -0.2f}, {1.0f, -0.1f},
                                    {1.0f, 0.1f}, {0.995f, 0.2f}, {0.965f, 0.29f}, {0.90f, 0.34f}, {0.80f, 0.355f}, {0.66f, 0.335f}, {0.60f, 0.27f}};
    mdl_revolve(&t, tyre, (int)(sizeof(tyre) / sizeof(tyre[0])), 28, 1, m4_id());
    MESH[MS_X0 + MX_TYRE] = mesh_upload(&t);
    static const float barrel[][2] = {{0.64f, 0.30f}, {0.62f, 0.27f}, {0.60f, 0.2f}, {0.60f, -0.2f}, {0.62f, -0.27f}, {0.64f, -0.30f}};
    mdl_revolve(&r, barrel, 6, 24, 1, m4_id());
    static const float hub[][2] = {{0.0f, 0.25f}, {0.14f, 0.25f}, {0.16f, 0.22f}, {0.16f, 0.15f}, {0.0f, 0.15f}};
    mdl_revolve(&r, hub, 5, 12, 1, m4_id());
    for (int k = 0; k < 5; k++) {
        float a = 2 * MDL_PI * (float)k / 5;
        m4 xf = m4_mul(mdl_R(0, a, 0), m4_mul(mdl_T(V(0.22f, 0.36f, 0)), mdl_S(V(0.09f, 0.52f, 0.11f))));
        mdl_se(&r, xf, 0.5f, 6, 8);
    }
    MESH[MS_X0 + MX_RIM] = mesh_upload(&r);
    FBuf l = {0};
    mdl_se(&l, mdl_S(V(1, 1, 1)), 0.3f, 10, 14);
    MESH[MS_X0 + MX_LAMP] = mesh_upload(&l);
}

/* ------------------------------------------------------------------ people meshes */
static float mdl_jaw(float y) { return 1 - 0.30f * powf(clampf(-y / 0.125f, 0, 1), 1.6f); }
static float mdl_head_z(float x, float y) { /* front (-z) surface of the head */
    float q = 1 - (x / 0.096f) * (x / 0.096f) - (y / 0.122f) * (y / 0.122f);
    return -0.108f * 0.95f * sqrtf(fmaxf(q, 0));
}
static void mdl_mk_head(void) {
    FBuf b = {0};
    const int NS = 18, NK = 28;
    MdlGrid g = mdl_grid(NS, NK, true);
    for (int i = 0; i < NS; i++)
        for (int j = 0; j < NK; j++) {
            float th = MDL_PI * (float)i / (NS - 1), ph = 2 * MDL_PI * (float)j / NK;
            float yy = cosf(th), rr = sinf(th);
            float y = yy * 0.122f, f = mdl_jaw(y);
            float x = rr * cosf(ph) * 0.096f * f, z = rr * sinf(ph) * 0.108f * f;
            if (z < 0) z *= 0.95f;
            if (z < -0.03f) z -= 0.026f * expf(-x * x / (2 * 0.013f * 0.013f) - (y + 0.012f) * (y + 0.012f) / (2 * 0.026f * 0.026f));
            MDL_GP(&g, i, j) = V(x, y, z);
        }
    mdl_grid_normals(&g);
    mdl_emit_grid(&b, &g);
    mdl_grid_free(&g);
    static const float neck[][2] = {{0.0f, -0.08f}, {0.046f, -0.08f}, {0.05f, -0.15f}, {0.056f, -0.23f}, {0.0f, -0.23f}};
    mdl_revolve(&b, neck, 5, 14, 0, m4_id());
    for (int s = -1; s <= 1; s += 2) mdl_se(&b, mdl_TS(V(s * 0.097f, -0.005f, 0.005f), V(0.02f, 0.05f, 0.032f)), 1.0f, 6, 8);
    MESH[MS_X0 + MX_HEAD] = mesh_upload(&b);
    FBuf f = {0};
    for (int s = -1; s <= 1; s += 2) {
        float ex = s * 0.037f, ey = 0.02f;
        mdl_se(&f, mdl_TS(V(ex, ey, mdl_head_z(ex, ey) + 0.001f), V(0.024f, 0.013f, 0.014f)), 1.0f, 6, 8);
        float bx = s * 0.038f, by = 0.048f;
        mdl_se(&f, m4_mul(mdl_TS(V(bx, by, mdl_head_z(bx, by) + 0.002f), V(1, 1, 1)), m4_mul(mdl_R(0, 0, s * 0.18f), mdl_S(V(0.05f, 0.009f, 0.014f)))), 0.6f, 6, 8);
    }
    mdl_se(&f, mdl_TS(V(0, -0.064f, mdl_head_z(0, -0.064f) + 0.001f), V(0.038f, 0.007f, 0.012f)), 1.0f, 6, 8);
    MESH[MS_X0 + MX_FACE] = mesh_upload(&f);
}
/* hair shell: open at the bottom, hairline depends on direction; long = curtain down the back and sides */
static void mdl_mk_hair(int slot, bool lng) {
    FBuf b = {0};
    const int NS = 14, NK = 28;
    MdlGrid g = mdl_grid(NS, NK, true);
    for (int j = 0; j < NK; j++) {
        float ph = 2 * MDL_PI * (float)j / NK, sph = sinf(ph);
        float yh;
        if (!lng) yh = -0.005f + (sph < 0 ? 0.062f * -sph : -0.075f * sph);
        else {
            float q = clampf(-sph * 1.25f, 0, 1);
            yh = lerpf(-0.17f - 0.14f * fmaxf(sph, 0), 0.055f, q * q);
        }
        for (int i = 0; i < NS; i++) {
            float v = powf((float)i / (NS - 1), 0.85f);
            float y = lerpf(0.130f, yh, v);
            float yc = fmaxf(y, -0.1f);
            float q = fmaxf(0, 1 - (yc / 0.122f) * (yc / 0.122f));
            float rx = 0.096f * 1.09f * sqrtf(q) * mdl_jaw(yc), rz = 0.108f * 1.09f * sqrtf(q) * mdl_jaw(yc);
            if (y < -0.1f && lng) { rx += 0.012f * (-0.1f - y) * 3; rz += 0.02f * (-0.1f - y) * 3; }
            if (sph > 0) rz *= 1.08f;
            MDL_GP(&g, i, j) = V(rx * cosf(ph), y, rz * sph);
        }
    }
    mdl_grid_normals(&g);
    mdl_emit_grid(&b, &g);
    mdl_grid_free(&g);
    MESH[MS_X0 + slot] = mesh_upload(&b);
}
static void mdl_mk_cap(void) {
    FBuf b = {0};
    const int NS = 8, NK = 24;
    MdlGrid g = mdl_grid(NS, NK, true);
    for (int j = 0; j < NK; j++) {
        float ph = 2 * MDL_PI * (float)j / NK;
        for (int i = 0; i < NS; i++) {
            float th = (float)i / (NS - 1) * 1.42f;
            MDL_GP(&g, i, j) = V(0.108f * sinf(th) * cosf(ph), 0.04f + 0.115f * cosf(th), 0.12f * sinf(th) * sinf(ph));
        }
    }
    mdl_grid_normals(&g);
    mdl_emit_grid(&b, &g);
    mdl_grid_free(&g);
    mdl_se(&b, m4_mul(mdl_TS(V(0, 0.045f, -0.125f), V(1, 1, 1)), m4_mul(mdl_R(0, 0.1f, 0), mdl_S(V(0.20f, 0.014f, 0.16f)))), 0.5f, 6, 14);
    MESH[MS_X0 + MX_CAP] = mesh_upload(&b);
}
static void mdl_mk_body_parts(void) {
    /* limb: hangs down from y=0 to y=-1, radius 1 a quarter of the way down (scaled per use) */
    FBuf b = {0};
    static const float limb[][2] = {{0.0f, 0.0f}, {0.5f, -0.012f}, {0.82f, -0.05f}, {0.98f, -0.14f}, {1.0f, -0.28f}, {0.93f, -0.55f}, {0.80f, -0.82f}, {0.68f, -0.95f}, {0.5f, -0.99f}, {0.0f, -1.0f}};
    mdl_revolve(&b, limb, (int)(sizeof(limb) / sizeof(limb[0])), 12, 0, m4_id());
    MESH[MS_X0 + MX_LIMB] = mesh_upload(&b);
    /* torso: superelliptic sections from the hip (y=0) to the neck base (y=0.625) */
    static const float ts[][3] = {{-0.02f, 0.158f, 0.098f}, {0.08f, 0.150f, 0.092f}, {0.22f, 0.165f, 0.100f}, {0.36f, 0.185f, 0.112f},
                                  {0.48f, 0.205f, 0.108f}, {0.55f, 0.185f, 0.095f}, {0.60f, 0.10f, 0.07f}, {0.625f, 0.045f, 0.045f}};
    const int NS = 8, NK = 18;
    MdlGrid g = mdl_grid(NS, NK, true);
    for (int i = 0; i < NS; i++)
        for (int j = 0; j < NK; j++) {
            float a = 2 * MDL_PI * (float)j / NK;
            MDL_GP(&g, i, j) = V(ts[i][1] * mdl_sp(cosf(a), 0.8f), ts[i][0], ts[i][2] * mdl_sp(sinf(a), 0.8f) - 0.006f * ts[i][0]);
        }
    mdl_grid_normals(&g);
    FBuf t = {0};
    mdl_emit_grid(&t, &g);
    mdl_grid_free(&g);
    MESH[MS_X0 + MX_TORSO] = mesh_upload(&t);
    /* shoe: ankle at the origin, toe toward -z, sole at y=-0.08 */
    FBuf s = {0};
    mdl_se(&s, mdl_TS(V(0, -0.045f, -0.06f), V(0.105f, 0.09f, 0.27f)), 0.55f, 8, 12);
    mdl_se(&s, mdl_TS(V(0, -0.072f, -0.065f), V(0.108f, 0.03f, 0.285f)), 0.4f, 6, 12);
    mdl_se(&s, mdl_TS(V(0, 0.0f, 0.02f), V(0.092f, 0.085f, 0.11f)), 0.8f, 6, 10);
    MESH[MS_X0 + MX_SHOE] = mesh_upload(&s);
    FBuf k = {0};
    static const float skirt[][2] = {{0.16f, 0.0f}, {0.19f, -0.12f}, {0.225f, -0.26f}, {0.25f, -0.38f}, {0.255f, -0.40f}};
    mdl_revolve(&k, skirt, 5, 20, 0, m4_id());
    MESH[MS_X0 + MX_SKIRT] = mesh_upload(&k);
}

static void models_init(void) {
    for (int f = 0; f < MDL_NFAM; f++) mdl_build_car(f);
    mdl_build_wheels();
    mdl_mk_head();
    mdl_mk_hair(MX_HAIRS, false);
    mdl_mk_hair(MX_HAIRL, true);
    mdl_mk_cap();
    mdl_mk_body_parts();
}

/* ------------------------------------------------------------------ car draw */
static void draw_car_model(const Car *c, float t, float night) {
    const CarSpec *S = &SPEC[c->model];
    const MdlFam *F = &mdl_fam[mdl_famof[c->model]];
    v3 sc = V(S->W / F->W, S->H / F->H, S->L / F->L);
    m4 M = m4_trs(c->pos, c->yaw, c->pitch, c->roll, V(1, 1, 1));
    m4 MS = m4_mul(M, mdl_S(sc));
    bool burnt = c->wrecked;
    float dmg = 1 - clampf(c->hp / S->hp, 0, 1);
    Mat body = {mul(c->col, 1 - dmg * 0.45f), PAT_PAINT, 0.45f, 0.30f, {0, 0, 0}, 0};
    Mat dark = {{0.012f, 0.014f, 0.018f}, PAT_PLAIN, 0.7f, 0.12f, {0, 0, 0}, 0};
    Mat tyre = {{0.03f, 0.03f, 0.03f}, PAT_PLAIN, 0, 0.8f, {0, 0, 0}, 0};
    Mat rim = {{0.62f, 0.62f, 0.65f}, PAT_PLAIN, 1, 0.22f, {0, 0, 0}, 0};
    if (burnt) {
        body = (Mat){{0.04f, 0.035f, 0.03f}, PAT_LEAF, 0.3f, 0.9f, {0, 0, 0}, 0};
        dark.base = V(0.01f, 0.01f, 0.01f);
        rim.base = V(0.1f, 0.09f, 0.08f);
        rim.metal = 0.4f;
    }
    inst(F->paint, MS, &body);
    inst(F->dark, MS, &dark);
    /* wheels: front pair steers, rims spin */
    float wr = F->wrad;
    for (int k = 0; k < 4; k++) {
        float sx = k & 1 ? 1 : -1, sz = k & 2 ? 1 : -1;
        float st = sz < 0 ? c->steer * S->steer * 0.8f : 0;
        m4 wm = m4_mul(M, m4_trs(V(sx * F->wx * sc.x, wr, sz * F->wz * sc.z), st + (sx < 0 ? MDL_PI : 0), 0, 0, V(1, 1, 1)));
        inst(MS_X0 + MX_TYRE, m4_mul(wm, mdl_S(V(wr, wr, wr))), &tyre);
        inst(MS_X0 + MX_RIM, m4_mul(wm, m4_trs(V(0, 0, 0), 0, sx < 0 ? c->wheel : -c->wheel, 0, V(wr, wr, wr))), &rim);
    }
    if (burnt) return;
    static const Mat PLATE = {{0.85f, 0.85f, 0.8f}, PAT_PLAIN, 0.2f, 0.4f, {0, 0, 0}, 0};
    int lamp = MS_X0 + MX_LAMP;
    float hl = 0.5f + night * 5;
    Mat head = glow(V(0.8f, 0.8f, 0.75f), V(hl, hl * 0.95f, hl * 0.8f), PAT_PLAIN, 0);
    float tl = c->brk > 0.1f ? 5.5f : 0.4f + night * 2.5f;
    Mat tail = glow(V(0.4f, 0.02f, 0.02f), V(tl, tl * 0.06f, tl * 0.04f), PAT_PLAIN, 0);
    for (int s = 0; s < 2; s++) {
        inst(lamp, m4_mul(MS, F->head[s]), &head);
        inst(lamp, m4_mul(MS, F->tail[s]), &tail);
    }
    inst(lamp, m4_mul(MS, F->plate[0]), &PLATE);
    inst(lamp, m4_mul(MS, F->plate[1]), &PLATE);
    /* indicators: amber, blinking toward the coming turn */
    bool blink = c->sig != 0 && fmodf(t * 1.6f, 1) < 0.5f && c->sig_node >= 0 && dist2d(c->pos, nodes[c->sig_node].p) < 45;
    if (c->sig_node >= 0 && blink) {
        v3 np = nodes[c->sig_node].p;
        if (dot(sub(np, c->pos), fwd_of(c->yaw)) < -6) blink = false;
    }
    for (int s = 0; s < 2; s++) {
        bool on = blink && (s == 1 ? 1 : -1) == c->sig;
        Mat am = glow(V(0.5f, 0.3f, 0.05f), on ? V(6, 3, 0.3f) : V(0.15f, 0.08f, 0.0f), PAT_PLAIN, 0);
        inst(lamp, m4_mul(MS, F->indf[s]), &am);
        inst(lamp, m4_mul(MS, F->indr[s]), &am);
    }
    /* model specials */
    float H = S->H, L = S->L;
    if (c->model == VM_POLICE) {
        static const Mat BLK = {{0.02f, 0.02f, 0.025f}, PAT_PLAIN, 0.2f, 0.5f, {0, 0, 0}, 0};
        bool fl = fmodf(t * 3.3f, 1) < 0.5f;
        Mat r = glow(V(0.3f, 0, 0), c->siren ? (fl ? V(9, 0.3f, 0.2f) : V(0.4f, 0, 0)) : V(0.3f, 0, 0), PAT_PLAIN, 0);
        Mat b = glow(V(0, 0, 0.3f), c->siren ? (!fl ? V(0.3f, 0.8f, 9) : V(0, 0, 0.4f)) : V(0, 0, 0.3f), PAT_PLAIN, 0);
        float bz = -L * 0.5f + L * 0.55f;
        inst(lamp, m4_mul(M, mdl_TS(V(0, H + 0.03f, bz), V(1.0f, 0.06f, 0.30f))), &BLK);
        inst(lamp, m4_mul(M, mdl_TS(V(-0.27f, H + 0.09f, bz), V(0.46f, 0.12f, 0.27f))), &r);
        inst(lamp, m4_mul(M, mdl_TS(V(0.27f, H + 0.09f, bz), V(0.46f, 0.12f, 0.27f))), &b);
        for (int s = -1; s <= 1; s += 2)
            inst(MS_CUBE, m4_mul(M, mdl_TS(V(s * (S->W * 0.5f - 0.025f), 0.62f, -L * 0.5f + L * 0.52f), V(0.02f, 0.36f, L * 0.30f))), &BLK);
    }
    if (c->model == VM_TAXI) {
        Mat sign = glow(V(0.5f, 0.45f, 0.2f), V(3, 2.4f, 0.6f), PAT_PLAIN, 0.8f);
        inst(lamp, m4_mul(M, mdl_TS(V(0, H + 0.1f, -L * 0.5f + L * 0.55f), V(0.7f, 0.18f, 0.3f))), &sign);
    }
    if (c->model == VM_BUS) {
        Mat sign = glow(V(0.2f, 0.2f, 0.1f), V(3, 2.2f, 0.4f), PAT_PLAIN, 0.7f);
        inst(lamp, m4_mul(M, mdl_TS(V(0, H - 0.3f, -L / 2 + 0.01f), V(1.7f, 0.26f, 0.05f))), &sign);
    }
}

/* ------------------------------------------------------------------ people */
static void draw_person(v3 pos, float yaw, float anim, float speed, v3 shirt, v3 pants, v3 skin, v3 hair,
                        float height, float lie, float tumble, int pose, bool cop, unsigned seed) {
    unsigned h = hashu(seed * 2654435761u + 0x9e3779b9u);
    float T = (float)G.time, hf = mdl_u8(h, 0) * 6.2831f;
    static const float BX[3] = {0.90f, 1.0f, 1.12f}, BZ[3] = {0.92f, 1.0f, 1.10f};
    int build = (int)(h >> 8) % 3;
    float bx = BX[build], bz = BZ[build];
    int hs = cop ? 5 : (int)(h >> 10) % 7; /* 0 short 1 long 2 ponytail 3 bald 4 bun 5 cap 6 short */
    int outfit = cop ? 1 : (int)(h >> 14) % 7; /* 0,1 t-shirt 2 long sleeve 3,4 open jacket 5,6 dress/skirt */
    bool tee = outfit <= 1, jacket = outfit == 3 || outfit == 4, dress = outfit >= 5;
    int acc = cop ? 9 : (int)(h >> 18) % 8; /* 0 backpack 1 shoulder bag, others none */
    bool shades = ((h >> 22) & 3) == 0 && pose != 2;
    bool shorts = !cop && !dress && ((h >> 24) & 7) == 0;
    bool lying = lie != 0;
    float amp = clampf(speed / 3, 0, 1), run = clampf((speed - 3) / 3.5f, 0, 1);
    if (lying) { amp = 0; run = 0; }
    float ph = anim;
    Mat ms = mat(shirt, PAT_PLAIN, 0, 0.8f), mp = mat(pants, PAT_PLAIN, 0, 0.85f), mk = mat(skin, PAT_PLAIN, 0, 0.55f), mh = mat(hair, PAT_PLAIN, 0, 0.85f);
    static const v3 SHOE[4] = {{0.04f, 0.04f, 0.045f}, {0.75f, 0.75f, 0.72f}, {0.22f, 0.13f, 0.08f}, {0.5f, 0.08f, 0.06f}};
    Mat msh = mat(SHOE[(h >> 26) & 3], PAT_PLAIN, 0, 0.6f);
    Mat mdk = mat(V(0.03f, 0.025f, 0.025f), PAT_PLAIN, 0, 0.5f);
    Mat mblk = mat(V(0.02f, 0.02f, 0.025f), PAT_PLAIN, 0.2f, 0.4f);
    float lr = (float)((h >> 5) & 31) / 31.0f;
    Mat macc = mat(V(0.1f + 0.5f * lr, 0.12f + 0.3f * mdl_u8(h, 3), 0.15f + 0.4f * mdl_u8(h, 11)), PAT_PLAIN, 0, 0.7f);
    Mat mdress = dress ? (((h >> 11) & 1) ? mp : ms) : ms;
    Mat mlegs = (dress || shorts) ? mk : mp;
    Mat marm = (jacket || outfit == 2 || cop) ? ms : mk;

    /* gait */
    float swing = 0.50f * amp + 0.42f * run, kmax = 0.04f + amp * 0.16f + run * 0.35f;
    float armamp = 0.55f * amp + 0.5f * run;
    float dy = -0.88f * (1 - cosf(swing * fabsf(sinf(ph)))) + run * 0.05f * cosf(2 * ph) - (pose == 2 ? 0.01f : 0);
    float breath = sinf(T * 2.1f + hf);
    float lean = 0.04f + 0.22f * run, twist = -0.14f * amp * sinf(ph) * (1 + run);
    float sway = pose == 2 ? 0.035f * sinf(T * 0.7f + hf) : 0;
    m4 R = m4_trs(pos, yaw, lie + tumble, 0, V(height, height, height));
    m4 P = m4_mul(R, m4_mul(mdl_T(V(0, 0.93f + dy, 0)), mdl_R(0, 0, sway)));
    m4 Tt = m4_mul(P, m4_mul(mdl_R(twist, -lean, 0), mdl_S(V(1, 1, 1))));
    /* limp when knocked down */
    float r1 = mdl_u8(h, 2), r2 = mdl_u8(h, 9), r3 = mdl_u8(h, 15), r4 = mdl_u8(h, 21), r5 = mdl_u8(h, 27);

    /* legs */
    float hx = 0.092f * bx;
    for (int s = -1; s <= 1; s += 2) {
        float phs = ph + (s < 0 ? MDL_PI : 0);
        float tt = swing * sinf(phs), kb = kmax * 0.2f + (0.1f + 0.55f * amp + 0.8f * run) * fmaxf(0, cosf(phs)) * (amp > 0 ? 1.0f : 0.0f);
        float splay = s * (0.03f + 0.06f * amp);
        if (pose == 2) { tt += s > 0 ? 0.08f : -0.04f; kb += s > 0 ? 0.22f : 0.04f; }
        if (lying) { tt = (r4 - 0.5f) * 0.4f * s; kb = 0.15f + 0.45f * (s > 0 ? r5 : r3); splay = s * (0.16f + 0.18f * r1); }
        m4 TH = m4_mul(P, m4_trs(V(s * hx, 0, 0), 0, tt, splay, V(1, 1, 1)));
        inst(MS_X0 + MX_LIMB, m4_mul(TH, mdl_S(V(0.086f * bx, 0.44f, 0.09f * bz))), &mlegs);
        m4 K = m4_mul(TH, m4_trs(V(0, -0.44f, 0), 0, -kb, 0, V(1, 1, 1)));
        inst(MS_X0 + MX_LIMB, m4_mul(K, mdl_S(V(0.064f * bx, 0.43f, 0.066f * bz))), shorts || dress ? &mk : &mp);
        float fp = clampf(-(tt - kb) * 0.85f - 0.2f * run, -0.7f, 0.7f);
        m4 A = m4_mul(K, m4_trs(V(0, -0.43f, 0), 0, fp, 0, V(1, 1, 1)));
        inst(MS_X0 + MX_SHOE, A, &msh);
    }
    /* pelvis */
    inst(MS_SPHERE, m4_mul(P, mdl_TS(V(0, 0.015f, 0.003f), V(0.172f * bx, 0.112f, 0.118f * bz))), dress ? &mdress : &mp);
    if (dress) inst(MS_X0 + MX_SKIRT, m4_mul(P, mdl_TS(V(0, 0.07f, 0), V(bx * (((h >> 11) & 1) ? 1.0f : 0.85f), 1.0f, bz * 0.85f))), &mdress);
    /* torso */
    float cb = 1 + 0.014f * breath;
    inst(MS_X0 + MX_TORSO, m4_mul(Tt, mdl_S(V(bx * cb, 1 + 0.01f * breath, bz * cb))), &ms);
    if (jacket) {
        Mat inner = mat(lerp3(shirt, V(0.9f, 0.9f, 0.88f), 0.7f), PAT_PLAIN, 0, 0.8f);
        inst(MS_X0 + MX_LAMP, m4_mul(Tt, mdl_TS(V(0, 0.33f, -0.1f * bz), V(0.10f * bx, 0.44f, 0.05f))), &inner);
    }
    if (cop) {
        inst(MS_CYL, m4_mul(P, mdl_TS(V(0, 0.03f, 0), V(0.34f * bx, 0.07f, 0.23f * bz))), &mblk);
        Mat gold = {{0.9f, 0.7f, 0.15f}, PAT_PLAIN, 1.0f, 0.3f, {0, 0, 0}, 0};
        inst(MS_X0 + MX_LAMP, m4_mul(Tt, mdl_TS(V(0.075f * bx, 0.40f, -0.113f * bz), V(0.04f, 0.05f, 0.02f))), &gold);
    }
    /* arms */
    float sx = 0.185f * bx + 0.036f;
    for (int s = -1; s <= 1; s += 2) {
        float phs = ph + (s < 0 ? MDL_PI : 0);
        float ua = -armamp * sinf(phs), roll = s * (0.07f + 0.03f * run), fb = 0.14f + 0.25f * amp + 0.95f * run + 0.25f * armamp * fmaxf(0, -sinf(phs)), wave = 0;
        if (pose == 2) {
            if (((h >> 25) & 1) && s > 0) { ua = 0.55f; fb = 1.9f; roll = 0.1f; }
            else { ua = 0.05f; fb = 0.35f; }
        }
        if (pose == 1 && s > 0) { ua = 2.72f; roll = 0.28f; fb = 0.15f; wave = 0.38f * sinf(T * 9); }
        if (lying) { ua = (r2 - 0.5f) * 0.9f; roll = s * (0.75f + 0.5f * r3); fb = 0.2f + 0.5f * r1 * (s > 0 ? 1 : 0.3f); }
        m4 J = m4_mul(Tt, mdl_T(V(s * sx, 0.515f, 0)));
        m4 U = m4_mul(J, m4_trs(V(0, 0, 0), 0, ua, roll, V(1, 1, 1)));
        inst(MS_X0 + MX_LIMB, m4_mul(U, mdl_S(V(0.052f * bx, 0.30f, 0.054f * bz))), jacket || outfit == 2 || cop || tee ? &ms : &mk);
        m4 E = m4_mul(U, m4_trs(V(0, -0.30f, 0), 0, fb, wave, V(1, 1, 1)));
        inst(MS_X0 + MX_LIMB, m4_mul(E, mdl_S(V(0.042f * bx, 0.27f, 0.044f * bz))), &marm);
        inst(MS_SPHERE, m4_mul(E, mdl_TS(V(0, -0.30f, 0), V(0.038f, 0.052f, 0.032f))), &mk);
    }
    /* head */
    float hy = 0, hp = 0, hr = 0;
    if (pose == 2) { hy = 0.7f * sinf(T * 0.45f + hf); hp = ((h >> 25) & 1) ? 0.35f : 0.0f; }
    if (pose == 1) hy = 0.25f * sinf(T * 1.3f);
    if (lying) { hr = (r1 - 0.5f) * 0.8f; hp = -0.2f; }
    float bob = 0.004f * breath;
    m4 Hd = m4_mul(Tt, m4_mul(mdl_T(V(0, 0.79f + bob, -0.005f)), m4_mul(mdl_R(hy, hp, hr), mdl_S(V(1.15f, 1.15f, 1.15f)))));
    inst(MS_X0 + MX_HEAD, Hd, &mk);
    inst(MS_X0 + MX_FACE, Hd, &mdk);
    if (shades) inst(MS_X0 + MX_LAMP, m4_mul(Hd, mdl_TS(V(0, 0.022f, mdl_head_z(0, 0.022f) - 0.006f), V(0.19f, 0.042f, 0.03f))), &mblk);
    if (hs == 1 || hs == 2 || hs == 4 || hs == 0 || hs == 6 || hs == 5) {
        if (hs == 1) inst(MS_X0 + MX_HAIRL, Hd, &mh);
        else inst(MS_X0 + MX_HAIRS, Hd, &mh);
    }
    if (hs == 2) inst(MS_X0 + MX_LIMB, m4_mul(Hd, m4_mul(mdl_T(V(0, 0.045f, 0.105f)), m4_mul(mdl_R(0, 0.55f + 0.2f * sinf(ph) * amp, 0), mdl_S(V(0.036f, 0.24f, 0.036f))))), &mh);
    if (hs == 4) inst(MS_SPHERE, m4_mul(Hd, mdl_TS(V(0, 0.125f, 0.045f), V(0.058f, 0.052f, 0.058f))), &mh);
    if (hs == 5) {
        Mat mc = cop ? mat(V(0.04f, 0.06f, 0.14f), PAT_PLAIN, 0, 0.6f) : mat(mul(pants, 1.3f), PAT_PLAIN, 0, 0.7f);
        inst(MS_X0 + MX_CAP, cop ? m4_mul(Hd, mdl_TS(V(0, 0.015f, 0), V(1.0f, 1.12f, 1.0f))) : Hd, &mc);
        if (cop) {
            Mat gold = {{0.9f, 0.7f, 0.15f}, PAT_PLAIN, 1.0f, 0.3f, {0, 0, 0}, 0};
            inst(MS_X0 + MX_LAMP, m4_mul(Hd, mdl_TS(V(0, 0.115f, -0.112f), V(0.04f, 0.04f, 0.02f))), &gold);
        }
    }
    /* accessories */
    if (acc == 0) inst(MS_X0 + MX_LAMP, m4_mul(Tt, mdl_TS(V(0, 0.30f, (0.105f + 0.06f) * bz), V(0.27f * bx, 0.38f, 0.14f))), &macc);
    if (acc == 1) {
        inst(MS_X0 + MX_LAMP, m4_mul(P, mdl_TS(V(-0.21f * bx, -0.06f, 0.02f), V(0.07f, 0.2f, 0.26f))), &macc);
        inst(MS_CUBE, m4_mul(Tt, m4_trs(V(0, 0.33f, 0), 0, 0, -0.95f, V(0.02f, 0.66f, 0.2f * bz))), &macc);
    }
}
