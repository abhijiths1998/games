/* actors.c - particles, vehicles (physics, traffic AI, police AI), pedestrians, street props, the player. */

/* ================================================================== shared game state */
enum { ST_TITLE, ST_PLAY, ST_PAUSE, ST_MAP, ST_BUSTED, ST_WASTED, ST_SHOP };
static struct {
    int state;
    float st, time;         /* time in state, total sim time */
    float tod;              /* hour of day 0..24 */
    float rain, rain_target, rain_timer;
    int money;
    float heat;             /* wanted heat: stars = floor(heat) */
    int stars;
    float evade_t, star_flash, bust_t;
    float msg_t, big_t, toast_t, radio_t, help_t;
    char msg[128], big[64], big2[96], toast[48], help[96];
    float damage_fx, shake, fade;
    int district, radio;
    float sens, fov_base;
    bool invert, autosave_ok;
    float heli_t;
    float speed_fx;
    int bot;                /* self-test autopilot */
    float stat_dist, stat_air;
    int stat_peds, stat_cars, stat_busted, stat_wasted;
} G;
static bool selftest;
static void message(const char *m, float secs) { snprintf(G.msg, sizeof(G.msg), "%s", m); G.msg_t = secs; }
static void bigtext(const char *a, const char *b, float secs) {
    snprintf(G.big, sizeof(G.big), "%s", a);
    snprintf(G.big2, sizeof(G.big2), "%s", b ? b : "");
    G.big_t = secs;
}
static void add_heat(float h);

/* ================================================================== particles + transient lights */
enum { PK_GLOW, PK_STREAK, PK_RING, PK_SMOKE };
typedef struct { v3 pos, vel, col; float life, max, size, grow, drag, grav; int kind; } Part;
#define MAXPART 4000
static Part part[MAXPART];
static int part_next;
static void spawn_part(v3 pos, v3 vel, v3 col, float life, float size, int kind, float grow, float grav) {
    Part *p = &part[part_next];
    part_next = (part_next + 1) % MAXPART;
    *p = (Part){pos, vel, col, life, life, size, grow, kind == PK_SMOKE ? 0.6f : 1.2f, grav, kind};
}
typedef struct { v3 pos, col; float radius, life, max; } DynLight;
#define MAXDL 24
static DynLight dl[MAXDL];
static void add_light(v3 pos, v3 col, float radius, float life) {
    int best = 0;
    for (int i = 0; i < MAXDL; i++) if (dl[i].life < dl[best].life) best = i;
    dl[best] = (DynLight){pos, col, radius, life, life};
}
static void sparks(v3 pos, v3 dir, int n, v3 col, float spd) {
    for (int i = 0; i < n; i++) {
        v3 v = add(mul(dir, spd * (0.3f + frand())), V(frs() * spd * 0.5f, frand() * spd * 0.6f, frs() * spd * 0.5f));
        spawn_part(pos, v, col, 0.25f + frand() * 0.4f, 0.05f + frand() * 0.05f, PK_STREAK, 0, 14);
    }
}
static void puff(v3 pos, v3 vel, float size, float dark, float life) {
    v3 c = V(dark, dark, dark * 1.05f);
    spawn_part(pos, vel, c, life, size, PK_SMOKE, size * 0.9f, -0.4f);
}
static void explosion(v3 pos, float scale) {
    add_light(add(pos, V(0, 1.5f, 0)), V(14, 7, 2.5f), 26 * scale, 0.9f);
    for (int i = 0; i < 36; i++) {
        v3 d = norm(V(frs(), frand() * 1.2f, frs()));
        spawn_part(add(pos, V(0, 1, 0)), mul(d, (4 + frand() * 9) * scale), V(5, 2.2f + frand(), 0.4f), 0.4f + frand() * 0.5f, (0.5f + frand() * 0.8f) * scale, PK_GLOW, 1.5f, -2);
    }
    for (int i = 0; i < 26; i++) {
        v3 d = norm(V(frs(), frand() * 1.5f + 0.2f, frs()));
        puff(add(pos, V(frs(), 1 + frand(), frs())), mul(d, 2 + frand() * 4 * scale), (1.0f + frand()) * scale, 0.05f + frand() * 0.08f, 2.5f + frand() * 2.5f);
    }
    spawn_part(add(pos, V(0, 0.4f, 0)), V(0, 0, 0), V(3, 1.6f, 0.5f), 0.5f, 1, PK_RING, 26 * scale, 0);
    sparks(add(pos, V(0, 1, 0)), V(0, 1, 0), 40, V(5, 3, 1), 14);
    audio_play(SFX_EXPLOSION, pos.x, pos.y, pos.z, 1.0f, 0.9f + frand() * 0.2f);
    float d = len(sub(pos, cam.pos));
    G.shake = fmaxf(G.shake, clampf(1.2f - d / 60, 0, 1));
}
static void update_particles(float dt) {
    for (int i = 0; i < MAXPART; i++) {
        Part *p = &part[i];
        if (p->life <= 0) continue;
        p->life -= dt;
        p->vel.y -= p->grav * dt;
        p->vel = mul(p->vel, 1 - fminf(p->drag * dt, 0.5f));
        p->pos = add(p->pos, mul(p->vel, dt));
        p->size += p->grow * dt;
        if (p->kind == PK_STREAK && p->pos.y < terrain_h(p->pos.x, p->pos.z)) p->life = 0;
    }
    for (int i = 0; i < MAXDL; i++) dl[i].life -= dt;
}

/* ================================================================== vehicles */
enum { VM_SEDAN, VM_TAXI, VM_SPORTS, VM_VAN, VM_PICKUP, VM_POLICE, VM_BUS, VM_MUSCLE, VM_N };
typedef struct { const char *name; float L, W, H, vmax, accel, grip, steer, mass; int engine; float hp; } CarSpec;
static const CarSpec SPEC[VM_N] = {
    {"SEDAN", 4.6f, 1.9f, 1.45f, 42, 9.0f, 9.0f, 0.55f, 1.0f, 0, 100},
    {"TAXI", 4.7f, 1.9f, 1.5f, 42, 9.0f, 9.0f, 0.55f, 1.0f, 0, 110},
    {"COUPE", 4.4f, 2.0f, 1.2f, 64, 15.0f, 11.0f, 0.50f, 0.9f, 1, 90},
    {"VAN", 5.2f, 2.1f, 2.3f, 33, 6.5f, 7.5f, 0.50f, 1.5f, 2, 140},
    {"PICKUP", 5.3f, 2.0f, 1.85f, 39, 8.0f, 8.0f, 0.52f, 1.3f, 2, 130},
    {"POLICE", 4.8f, 1.95f, 1.5f, 54, 12.5f, 10.5f, 0.55f, 1.1f, 1, 150},
    {"BUS", 11.0f, 2.6f, 3.2f, 25, 4.5f, 6.5f, 0.42f, 4.0f, 2, 300},
    {"MUSCLE", 4.9f, 2.0f, 1.3f, 56, 13.5f, 8.0f, 0.52f, 1.2f, 1, 110},
};
enum { DRV_NONE, DRV_AI, DRV_PLAYER, DRV_COP };
enum { CS_PURSUE, CS_BLOCK, CS_PARKED };
typedef struct {
    bool on;
    int model, driver;
    v3 pos, vel;
    float yaw, vy, pitch, roll, steer, thr, brk;
    bool hb;
    float hp;
    bool wrecked, sunk, airborne;
    float fire_t, wreck_t, sink_t, air_t;
    v3 col;
    float rpm, wheel;
    /* navigation */
    int tn, te, lane_i;      /* heading for node tn along edge te; lane 0 inner, 1 outer on highways */
    v3 wp[10], wdir[10];
    int wstop[10];           /* node whose light gates this waypoint, or -1 */
    float wspd[10];          /* cruise speed for the stretch ending at this waypoint */
    int nwp;
    float cruise, stuck_t, honk_t, back_t, panic_t, hit_cool, smoke_acc, lightbar;
    int cop_state, slot, mission;
    float cop_t;
    bool siren, keep;
    float last_hit_by_player, idle_t;
    float fuel;
    bool owned;
    int sig, sig_node;
} Car;
/* upgrades bought for the player's own car */
static struct { int engine; bool brakes, tires, armor; } UPG;
#define MAXCAR 90
static Car car[MAXCAR];

static void car_axes(const Car *c, v3 *f, v3 *r) { *f = fwd_of(c->yaw); *r = right_of(c->yaw); }
static float car_speed(const Car *c) { return dot(c->vel, fwd_of(c->yaw)); }

static const v3 PAINT[] = {{0.55f, 0.04f, 0.04f}, {0.04f, 0.10f, 0.35f}, {0.75f, 0.75f, 0.75f}, {0.03f, 0.03f, 0.035f}, {0.12f, 0.30f, 0.14f},
                           {0.65f, 0.40f, 0.05f}, {0.30f, 0.30f, 0.32f}, {0.85f, 0.85f, 0.82f}, {0.35f, 0.08f, 0.30f}, {0.05f, 0.35f, 0.45f},
                           {0.50f, 0.25f, 0.10f}, {0.80f, 0.55f, 0.10f}};
static v3 rand_paint(void) { return PAINT[rand() % (int)(sizeof(PAINT) / sizeof(PAINT[0]))]; }

static bool slot_used[MAXSLOT];
static v3 focus_pos(void);
static Car *alloc_car(void) {
    int pick = -1;
    for (int i = 0; i < MAXCAR; i++) if (!car[i].on) { pick = i; break; }
    if (pick < 0) { /* full: recycle the farthest ordinary car */
        float best = -1;
        v3 fp = focus_pos();
        for (int i = 0; i < MAXCAR; i++) {
            Car *c = &car[i];
            if (c->driver == DRV_PLAYER || c->owned || c->keep || c->mission >= 0) continue;
            float d = dist2d(c->pos, fp);
            if (d > best) { best = d; pick = i; }
        }
        if (pick < 0) return NULL;
        if (car[pick].slot >= 0) slot_used[car[pick].slot] = false;
    }
    memset(&car[pick], 0, sizeof(Car));
    car[pick].on = true; car[pick].slot = -1; car[pick].mission = -1;
    return &car[pick];
}
static Car *spawn_car(int model, v3 pos, float yaw, int driver) {
    Car *c = alloc_car();
    if (!c) return NULL;
    c->model = model;
    c->pos = pos;
    c->pos.y = ground_at(pos.x, pos.z, pos.y + 1, 0.5f);
    c->yaw = yaw;
    c->driver = driver;
    c->hp = SPEC[model].hp;
    c->col = model == VM_TAXI ? V(0.85f, 0.62f, 0.05f) : model == VM_POLICE ? V(0.9f, 0.9f, 0.9f) : model == VM_BUS ? V(0.15f, 0.35f, 0.55f) : rand_paint();
    c->cruise = 11 + frand() * 4;
    c->fuel = 0.3f + frand() * 0.6f;
    c->sig_node = -1;
    return c;
}

/* ---- traffic lights: 20 s cycle, north-south first */
/* ---- traffic lights: 20 s cycle, north-south roads first */
static int light_state(int n, v3 din) { /* 0 green, 1 amber, 2 red */
    if (n < 0 || !nodes[n].light) return 0;
    float ph = fmodf(G.time + hash2f(n, 7, 99) * 20, 20.0f);
    bool ns = fabsf(din.z) > fabsf(din.x);
    if (ph < 8) return ns ? 0 : 2;
    if (ph < 10) return ns ? 1 : 2;
    if (ph < 18) return ns ? 2 : 0;
    return ns ? 2 : 1;
}

/* ---- breadth-first route field over the road graph (police, GPS, autopilot) */
typedef struct { short *d; int t; } Route;
static void route_build(Route *r, int target) {
    static int q[MAXNODE];
    if (!r->d) r->d = malloc(sizeof(short) * MAXNODE);
    for (int i = 0; i < nnode; i++) r->d[i] = 30000;
    r->t = target;
    int h = 0, t = 0;
    r->d[target] = 0;
    q[t++] = target;
    while (h < t) {
        int n = q[h++];
        for (int k = 0; k < nodes[n].deg; k++) {
            int m = edge_other(nodes[n].e[k], n);
            if (r->d[m] <= r->d[n] + 1) continue;
            r->d[m] = (short)(r->d[n] + 1);
            q[t++] = m;
        }
    }
}
static int route_next_edge(const Route *r, int n, int avoid_edge) {
    if (!r->d || n < 0 || n >= nnode) return -1;
    int best = -1, bd = 1 << 30;
    for (int k = 0; k < nodes[n].deg; k++) {
        int e = nodes[n].e[k];
        int v = r->d[edge_other(e, n)] * 4 + (e == avoid_edge ? 3 : 0);
        if (v < bd) { bd = v; best = e; }
    }
    return best;
}

/* ---- waypoint queue */
static float lane_off(const Car *c, int e) { return edges[e].hwy >= 0 ? (c->lane_i ? 5.45f : 1.85f) : LANE_OFF; }
static float edge_cruise(const Car *c, int e) { return edges[e].hwy >= 0 ? c->cruise * 2.05f : c->cruise; }
static void wp_push(Car *c, v3 p, int stop, v3 dir, float spd) {
    if (c->nwp >= 10) return;
    c->wp[c->nwp] = p; c->wstop[c->nwp] = stop; c->wdir[c->nwp] = dir; c->wspd[c->nwp] = spd;
    c->nwp++;
}
static void wp_pop(Car *c) {
    for (int k = 1; k < c->nwp; k++) { c->wp[k - 1] = c->wp[k]; c->wstop[k - 1] = c->wstop[k]; c->wdir[k - 1] = c->wdir[k]; c->wspd[k - 1] = c->wspd[k]; }
    if (c->nwp > 0) c->nwp--;
}
static float cross2(v3 a, v3 b) { return a.x * b.z - a.z * b.x; }
/* the car is on edge te heading for node tn: queue the turn onto e2 and the stretch to its far end */
static void plan_turn(Car *c, int e2) {
    int B = c->tn, e = c->te;
    int A = edge_other(e, B);
    v3 D = edge_dir(e, A), R = right_vec(D), D2 = edge_dir(e2, B), R2 = right_vec(D2);
    float off1 = lane_off(c, e), off2 = lane_off(c, e2);
    float in = nodes[B].inset > 0 ? nodes[B].inset + 3.5f : 4;
    v3 Bp = nodes[B].p;
    v3 a = add(add(Bp, mul(D, -in)), mul(R, off1)), b = add(add(Bp, mul(D2, in)), mul(R2, off2));
    v3 ctl;
    float dd = dot(D, D2);
    if (dd > 0.97f) ctl = mul(add(a, b), 0.5f);
    else if (dd < -0.9f) ctl = add(Bp, add(mul(D, 4), mul(R, -off1)));
    else {
        float den = cross2(D, D2);
        float t = fabsf(den) > 1e-4f ? cross2(sub(b, a), D2) / den : in;
        ctl = add(a, mul(D, clampf(t, 0, in * 2 + 6)));
    }
    float spd = fminf(edge_cruise(c, e), edge_cruise(c, e2));
    if (dd < 0.9f) spd = fminf(spd, 8);
    for (int k = 1; k <= 3; k++) {
        float t = k / 3.0f;
        wp_push(c, add(add(mul(a, (1 - t) * (1 - t)), mul(ctl, 2 * t * (1 - t))), mul(b, t * t)), -1, D2, spd);
    }
    int C = edge_other(e2, B);
    c->te = e2; c->tn = C;
    float L2 = edges[e2].len, in3 = nodes[C].inset > 0 ? nodes[C].inset + 3.5f : 4;
    float cr = edge_cruise(c, e2);
    if (L2 > 34) wp_push(c, lane_pos(e2, B, L2 * 0.5f, off2), -1, D2, cr);
    wp_push(c, lane_pos(e2, B, fmaxf(L2 - in3, in + 1), off2), nodes[C].light ? C : -1, D2, cr);
    c->sig = dd > 0.9f || dd < -0.9f ? 0 : (cross2(D, D2) > 0 ? 1 : -1);
    c->sig_node = B;
}
static int random_turn(Car *c) {
    int B = c->tn, e = c->te;
    v3 Din = edge_dir(e, edge_other(e, B));
    int opts[5], n = 0, straight = -1;
    float bestd = -2;
    for (int k = 0; k < nodes[B].deg; k++) {
        int e2 = nodes[B].e[k];
        if (e2 == e) continue;
        opts[n++] = e2;
        float d = dot(Din, edge_dir(e2, B));
        if (d > bestd) { bestd = d; straight = e2; }
    }
    if (!n) return e;
    if (straight >= 0 && (frand() < 0.5f || n == 1)) return straight;
    return opts[rand() % n];
}
static void place_on_edge(Car *c, int e, int from, float s) {
    c->te = e; c->tn = edge_other(e, from);
    c->pos = lane_pos(e, from, s, lane_off(c, e));
    v3 D = edge_dir(e, from);
    c->yaw = yaw_of(D);
    c->nwp = 0;
    float in3 = nodes[c->tn].inset > 0 ? nodes[c->tn].inset + 3.5f : 4;
    wp_push(c, lane_pos(e, from, fmaxf(edges[e].len - in3, s + 1), lane_off(c, e)), nodes[c->tn].light ? c->tn : -1, D, edge_cruise(c, e));
}

static void car_hit_effects(Car *c, v3 at, float impact, v3 n);
static void car_physics(Car *c, float dt) {
    const CarSpec *S = &SPEC[c->model];
    v3 f, r;
    car_axes(c, &f, &r);
    float vf = dot(c->vel, f), vr = dot(c->vel, r);
    bool dead = c->wrecked || c->sunk;
    if (c->driver == DRV_PLAYER && !dead) {
        c->fuel -= dt * (0.00006f + 0.0008f * c->thr) * (S->engine == 2 ? 1.3f : S->engine == 1 ? 1.15f : 1.0f);
        if (c->fuel < 0) c->fuel = 0;
    }
    float thr = dead || (c->driver == DRV_PLAYER && c->fuel <= 0) ? 0 : c->thr, brk = dead ? 0.6f : c->brk;
    float grip = S->grip, accel = S->accel, vmax = S->vmax, bdec = 18;
    if (c->owned) {
        accel *= 1 + 0.13f * UPG.engine;
        vmax *= 1 + 0.07f * UPG.engine;
        if (UPG.tires) grip *= 1.18f;
        if (UPG.brakes) bdec *= 1.35f;
    }
    if (!c->airborne) {
        if (thr > 0) {
            if (vf < -0.5f) vf += 16 * thr * dt;
            else vf += accel * thr * (1 - clampf(vf / vmax, 0, 1) * clampf(vf / vmax, 0, 1)) * dt;
        }
        if (brk > 0) {
            if (vf > 0.5f) vf -= bdec * brk * dt;
            else vf -= accel * 0.6f * brk * (vf > -11 ? 1 : 0) * dt;
        }
        if (c->hb) { grip *= 0.22f; vf -= vf * 0.9f * dt; }
        vf -= vf * (0.05f + fabsf(vf) * 0.0025f) * dt;
        if (thr == 0 && brk == 0) vf -= (vf > 0 ? 1 : -1) * fminf(fabsf(vf), (c->driver == DRV_PLAYER ? 2.6f : 1.5f) * dt);
        vr *= expf(-grip * dt);
        float sp = fabsf(vf);
        float steer_max = S->steer / (1 + sp / 22);
        c->steer += (clampf(c->steer, -1, 1) * 0) ; /* steer already set by driver */
        float wheelbase = S->L * 0.62f;
        float yaw_rate = vf * tanf(c->steer * steer_max) / wheelbase;
        if (c->hb) yaw_rate *= 1.35f;
        c->yaw = wrap_pi(c->yaw + yaw_rate * dt);
        v3 f2 = fwd_of(c->yaw), r2 = right_of(c->yaw);
        /* a little of the lateral slide becomes drift that keeps the car's momentum */
        c->vel = add(mul(f2, vf), mul(r2, vr));
        f = f2; r = r2;
    } else {
        c->air_t += dt;
    }
    v3 old = c->pos;
    c->pos.x += c->vel.x * dt;
    c->pos.z += c->vel.z * dt;
    /* vertical: ground following, ramps launch the car */
    float g = ground_at(c->pos.x, c->pos.z, c->pos.y, 0.9f);
    if (!c->airborne) {
        if (g < c->pos.y - 0.3f) { c->airborne = true; c->air_t = 0; }
        else {
            float nvy = (g - c->pos.y) / fmaxf(dt, 1e-4f);
            c->vy = clampf(nvy, -2, 16);
            c->pos.y = g;
        }
    }
    if (c->airborne) {
        c->vy -= 21 * dt;
        c->pos.y += c->vy * dt;
        if (c->pos.y <= g) {
            if (c->vy < -9) {
                float dmg = (-c->vy - 9) * 3;
                c->hp -= dmg;
                audio_play(SFX_CRASH_HEAVY, c->pos.x, c->pos.y, c->pos.z, clampf(-c->vy / 20, 0.3f, 1), 0.8f);
                sparks(c->pos, V(0, 1, 0), 12, V(4, 2.5f, 1), 6);
                if (c->driver == DRV_PLAYER) G.shake = fmaxf(G.shake, 0.5f);
            } else if (c->vy < -4) audio_play(SFX_CRASH_LIGHT, c->pos.x, c->pos.y, c->pos.z, 0.4f, 1.3f);
            c->pos.y = g;
            c->vy = 0;
            c->airborne = false;
        }
    }
    /* walls */
    int nc = S->L > 7 ? 4 : 3;
    float rad = S->W * 0.5f;
    for (int k = 0; k < nc; k++) {
        float off = -S->L / 2 + rad + (S->L - 2 * rad) * k / (nc - 1);
        v3 p = add(c->pos, mul(f, off)), q = p, n;
        if (collide_circle(&q, rad, S->H, &n)) {
            v3 push = sub(q, p);
            c->pos.x += push.x;
            c->pos.z += push.z;
            float vn = dot(c->vel, n);
            if (vn < 0) {
                c->vel = sub(c->vel, mul(n, vn * 1.25f));
                c->vel = mul(c->vel, 0.92f);
                if (-vn > 3) car_hit_effects(c, add(q, mul(n, -rad)), -vn, n);
            }
        }
    }
    (void)old;
    /* visual pitch/roll from the ground */
    if (!c->airborne) {
        float gf = ground_at(c->pos.x + f.x * S->L * 0.4f, c->pos.z + f.z * S->L * 0.4f, c->pos.y + 0.3f, 0.3f);
        float gb = ground_at(c->pos.x - f.x * S->L * 0.4f, c->pos.z - f.z * S->L * 0.4f, c->pos.y + 0.3f, 0.3f);
        float tp = atan2f(gf - gb, S->L * 0.8f);
        c->pitch += (tp - c->pitch) * fminf(1, dt * 12);
        float lat = dot(c->vel, r);
        c->roll += (clampf(-lat * 0.012f - c->steer * fabsf(vf) * 0.004f, -0.12f, 0.12f) - c->roll) * fminf(1, dt * 6);
    } else {
        c->pitch += (clampf(c->vy * 0.02f, -0.35f, 0.25f) - c->pitch) * fminf(1, dt * 2);
    }
    /* water */
    float wl = water_level_at(c->pos.x, c->pos.z);
    if (!c->sunk && water_at(c->pos.x, c->pos.z) && c->pos.y < wl + 0.3f) {
        c->sunk = true;
        c->sink_t = 0;
        audio_play(SFX_SPLASH, c->pos.x, wl, c->pos.z, 1, 0.8f);
        for (int i = 0; i < 30; i++) spawn_part(V(c->pos.x + frs() * 2, wl, c->pos.z + frs() * 2), V(frs() * 3, 4 + frand() * 5, frs() * 3), V(0.6f, 0.7f, 0.75f), 1.2f, 0.25f, PK_SMOKE, 0.5f, 12);
    }
    if (c->sunk) {
        c->sink_t += dt;
        c->vel = mul(c->vel, 1 - fminf(dt * 1.5f, 1));
        c->pos.y = fmaxf(c->pos.y - dt * 0.7f, terrain_h(c->pos.x, c->pos.z));
        c->vy = 0;
    }
    c->wheel += vf * dt / 0.34f;
    float target_rpm = clampf(fabsf(vf) / S->vmax * 1.6f, 0, 1);
    float gearish = fmodf(target_rpm * 3.2f, 1.0f) * 0.55f + 0.25f;
    c->rpm += ((c->thr > 0 ? fmaxf(gearish, 0.3f) : gearish * 0.7f) - c->rpm) * fminf(1, dt * 6);
    if (c->hit_cool > 0) c->hit_cool -= dt;
}

/* ================================================================== pedestrians */
enum { PS_WALK, PS_CROSS, PS_FLEE, PS_DOWN, PS_RETURN, PS_HAIL, PS_COP, PS_IDLE, PS_WAITX };
typedef struct {
    bool on;
    int state;
    v3 pos, vel;
    float yaw, speed, anim, timer;
    int bi, bj, dirn;
    float s;
    v3 target;
    v3 shirt, pants, skin, hair;
    float h, spin, tumble;
    bool cop;
    int fare, xnode, xaxis, city;
    unsigned seed;
    float wait_t;
} Ped;
#define MAXPED 110
static Ped ped[MAXPED];
static const v3 SKIN[] = {{0.85f, 0.65f, 0.5f}, {0.6f, 0.42f, 0.3f}, {0.38f, 0.25f, 0.17f}, {0.92f, 0.75f, 0.62f}, {0.25f, 0.16f, 0.11f}};
static const v3 CLOTH[] = {{0.6f, 0.1f, 0.1f}, {0.1f, 0.2f, 0.5f}, {0.85f, 0.85f, 0.82f}, {0.1f, 0.1f, 0.12f}, {0.2f, 0.45f, 0.2f}, {0.75f, 0.6f, 0.2f}, {0.45f, 0.2f, 0.45f}, {0.2f, 0.5f, 0.6f}, {0.55f, 0.4f, 0.3f}};
static v3 perim_pt(int c, int bi, int bj, float s) {
    float L = PITCH - 2 * WALK;
    s = fmodf(s, 4 * L);
    if (s < 0) s += 4 * L;
    City *C = &cities[c];
    float x0 = C->x0 + bi * PITCH + WALK, z0 = C->z0 + bj * PITCH + WALK, x1 = C->x0 + (bi + 1) * PITCH - WALK, z1 = C->z0 + (bj + 1) * PITCH - WALK;
    float y = C->gy + 0.15f;
    if (s < L) return V(x0 + s, y, z0);
    if (s < 2 * L) return V(x1, y, z0 + (s - L));
    if (s < 3 * L) return V(x1 - (s - 2 * L), y, z1);
    return V(x0, y, z1 - (s - 3 * L));
}
/* which city block a point belongs to (false outside the cities) */
static bool block_of(v3 p, int *c, int *bi, int *bj) {
    *c = city_at(p.x, p.z, 0);
    if (*c < 0) return false;
    City *C = &cities[*c];
    *bi = (int)clampf(floorf((p.x - C->x0) / PITCH), 0, C->nx - 1);
    *bj = (int)clampf(floorf((p.z - C->z0) / PITCH), 0, C->nz - 1);
    return true;
}
static Ped *spawn_ped(v3 pos, bool cop) {
    for (int i = 0; i < MAXPED; i++) {
        Ped *p = &ped[i];
        if (p->on) continue;
        memset(p, 0, sizeof(*p));
        p->on = true;
        p->pos = pos;
        p->cop = cop;
        p->skin = SKIN[rand() % 5];
        p->shirt = cop ? V(0.08f, 0.12f, 0.3f) : CLOTH[rand() % 9];
        p->pants = cop ? V(0.05f, 0.06f, 0.12f) : mul(CLOTH[rand() % 9], 0.6f);
        p->hair = frand() < 0.5f ? V(0.05f, 0.04f, 0.03f) : frand() < 0.5f ? V(0.35f, 0.22f, 0.1f) : V(0.7f, 0.6f, 0.35f);
        p->h = 0.92f + frand() * 0.16f;
        p->speed = 1.1f + frand() * 0.5f;
        p->dirn = frand() < 0.5f ? 1 : -1;
        p->fare = -1;
        p->seed = hashu((unsigned)i * 2654435761u + (unsigned)(pos.x * 7 + pos.z * 13));
        block_of(pos, &p->city, &p->bi, &p->bj);
        if (p->city < 0) p->city = 0;
        return p;
    }
    return NULL;
}
static void ped_flee(Ped *p, v3 from, float secs) {
    if (p->state == PS_DOWN || p->state == PS_COP || p->state == PS_HAIL) return;
    p->state = PS_FLEE;
    v3 d = sub(p->pos, from);
    d.y = 0;
    p->target = add(p->pos, mul(norm(d), 30));
    p->timer = secs;
}
static void ped_knock(Ped *p, v3 vel) {
    p->state = PS_DOWN;
    p->vel = add(mul(vel, 0.85f), V(frs() * 1.5f, 3 + fabsf(len(vel)) * 0.15f, frs() * 1.5f));
    p->timer = 3.5f + frand() * 2;
    p->spin = frs() * 8;
    p->tumble = 0;
    p->pos.y += 0.1f;
    audio_play(SFX_THUD, p->pos.x, p->pos.y, p->pos.z, 0.9f, 0.9f + frand() * 0.3f);
}
static void scare_nearby(v3 at, float radius, float secs) {
    for (int i = 0; i < MAXPED; i++)
        if (ped[i].on && dist2d(ped[i].pos, at) < radius && !ped[i].cop) ped_flee(&ped[i], at, secs);
}

/* ================================================================== street props: lamp posts at every road corner */
typedef struct { v3 pos, vel, rot, spin; bool down; float t; } Prop;
#define NPROP (MAXNODE * 4)
static Prop prop[NPROP];
static bool prop_exists(int idx) { int n = idx / 4; return n < nnode && nodes[n].city >= 0; }
static v3 prop_home(int idx) {
    int n = idx / 4, k = idx % 4;
    return add(nodes[n].p, V(k & 1 ? 7.6f : -7.6f, 0.15f, k & 2 ? 7.6f : -7.6f));
}
static void props_init(void) {
    for (int i = 0; i < NPROP; i++) { prop[i].pos = prop_exists(i) ? prop_home(i) : V(0, -100, 0); prop[i].down = false; }
}
static int city_node_near(v3 p) {
    int c = city_at(p.x, p.z, 10);
    if (c < 0) return -1;
    City *C = &cities[c];
    int i = (int)clampf(floorf((p.x - C->x0) / PITCH + 0.5f), 0, C->nx), j = (int)clampf(floorf((p.z - C->z0) / PITCH + 0.5f), 0, C->nz);
    return city_node(c, i, j);
}
static void props_near(v3 p, int *out, int *n) {
    *n = 0;
    int nd = city_node_near(p);
    if (nd < 0) return;
    for (int k = 0; k < 4; k++) out[(*n)++] = nd * 4 + k;
}
static void knock_prop(int idx, v3 vel) {
    Prop *q = &prop[idx];
    if (q->down) return;
    q->down = true;
    q->t = 0;
    q->vel = add(mul(vel, 0.7f), V(0, 2.5f, 0));
    q->rot = V(0, 0, 0);
    q->spin = V(frs() * 2, 0, frs() * 2);
    v3 n = norm(V(vel.x, 0, vel.z));
    q->spin = V(n.z * 3, 0, -n.x * 3);
    audio_play(SFX_CRASH_LIGHT, q->pos.x, 1, q->pos.z, 0.7f, 1.4f);
    sparks(add(q->pos, V(0, 1, 0)), n, 10, V(4, 3, 1.5f), 5);
}
static void update_props(float dt) {
    for (int i = 0; i < NPROP; i++) {
        Prop *q = &prop[i];
        if (!q->down) continue;
        q->t += dt;
        if (q->t < 6) {
            q->vel.y -= 18 * dt;
            q->pos = add(q->pos, mul(q->vel, dt));
            q->rot = add(q->rot, mul(q->spin, dt));
            if (q->rot.x > 1.5f) q->rot.x = 1.5f, q->spin.x = 0;
            if (q->rot.x < -1.5f) q->rot.x = -1.5f, q->spin.x = 0;
            if (q->rot.z > 1.5f) q->rot.z = 1.5f, q->spin.z = 0;
            if (q->rot.z < -1.5f) q->rot.z = -1.5f, q->spin.z = 0;
            float g = ground_at(q->pos.x, q->pos.z, q->pos.y + 0.5f, 0.2f);
            if (q->pos.y < g) { q->pos.y = g; q->vel = mul(q->vel, 0.3f); q->vel.y = 0; }
        }
        /* respawn out of sight */
        if (q->t > 30 && dist2d(prop_home(i), cam.pos) > 150) { q->down = false; q->pos = prop_home(i); }
    }
}

/* ================================================================== the player */
static struct {
    v3 pos, vel;
    float yaw, hp, anim, down_t, regen_t, hurt_t;
    bool ground, swim, sprint;
    int car, last_car;
    float cam_yaw, cam_pitch, look_t, cam_zoom;
    v3 cam_eye;
    /* stunt tracking */
    float air_t, air_h0, air_dist;
    v3 air_from;
    bool air_ramp;
    int air_ramp_id;
    float stamina, energy_t, law_cool;
    bool tired, in_node;
} PL;

static void player_hurt(float dmg, v3 from) {
    if (G.state != ST_PLAY) return;
    PL.hp -= dmg;
    PL.regen_t = 8;
    G.damage_fx = fminf(1, G.damage_fx + dmg / 30);
    (void)from;
}

/* effects for a car hitting something solid */
static void car_hit_effects(Car *c, v3 at, float impact, v3 n) {
    if (c->hit_cool > 0) return;
    c->hit_cool = 0.25f;
    float dmg = impact > 7 ? (impact - 6) * 1.7f : 0;
    if (c->driver == DRV_PLAYER) dmg *= 0.6f;
    if (c->owned && UPG.armor) dmg *= 0.5f;
    c->hp -= dmg;
    audio_play(impact > 11 ? SFX_CRASH_HEAVY : SFX_CRASH_LIGHT, at.x, at.y + 0.5f, at.z, clampf(impact / 18, 0.25f, 1), 0.85f + frand() * 0.3f);
    if (impact > 9) audio_play(SFX_GLASS, at.x, at.y + 0.8f, at.z, 0.6f, 1);
    sparks(add(at, V(0, 0.5f, 0)), n, (int)clampf(impact * 1.5f, 4, 30), V(4, 2.6f, 1.2f), impact * 0.5f);
    if (c->driver == DRV_PLAYER) {
        G.shake = fmaxf(G.shake, clampf(impact / 25, 0, 0.8f));
        if (impact > 14) player_hurt((impact - 14) * 1.2f, at);
    }
    if (impact > 8) scare_nearby(at, 25, 4);
}

/* ================================================================== spawning around the player */
static v3 focus_pos(void) { return PL.car >= 0 ? car[PL.car].pos : PL.pos; }
static int count_cars(int driver) { int n = 0; for (int i = 0; i < MAXCAR; i++) if (car[i].on && car[i].driver == driver && !car[i].wrecked) n++; return n; }
static bool car_near(v3 p, float r) {
    for (int i = 0; i < MAXCAR; i++) if (car[i].on && dist2d(car[i].pos, p) < r) return true;
    return dist2d(PL.pos, p) < r;
}
static bool in_view(v3 p, float margin) {
    v3 d = sub(p, cam.pos);
    float l = len(d);
    if (l < 1) return true;
    return dot(mul(d, 1 / l), cam.fwd) > cosf((cam.fov * 0.5f + margin) * 3.14159f / 180 * 1.35f);
}
static int pick_model(int style) {
    float r = frand();
    if (r < 0.03f && style != ST_PARK) return VM_BUS;
    switch (style) {
    case ST_DOWNTOWN: case ST_MIDRISE: return r < 0.3f ? VM_TAXI : r < 0.45f ? VM_SPORTS : r < 0.55f ? VM_VAN : VM_SEDAN;
    case ST_INDUSTRIAL: return r < 0.35f ? VM_VAN : r < 0.65f ? VM_PICKUP : r < 0.72f ? VM_MUSCLE : VM_SEDAN;
    case ST_BEACH: case ST_MANSION: return r < 0.25f ? VM_SPORTS : r < 0.4f ? VM_MUSCLE : r < 0.5f ? VM_PICKUP : VM_SEDAN;
    case ST_DESERT: case ST_MOUNTAIN: return r < 0.4f ? VM_PICKUP : r < 0.55f ? VM_VAN : r < 0.7f ? VM_MUSCLE : VM_SEDAN;
    default: return r < 0.1f ? VM_TAXI : r < 0.22f ? VM_VAN : r < 0.3f ? VM_MUSCLE : r < 0.36f ? VM_SPORTS : r < 0.48f ? VM_PICKUP : VM_SEDAN;
    }
}
static int traffic_target = 38;
static bool spawn_traffic(float rmin, float rmax, bool allow_visible) {
    v3 fp = focus_pos();
    for (int tries = 0; tries < 14; tries++) {
        float a = frand() * 6.2831853f, d = rmin + frand() * (rmax - rmin);
        v3 p = add(fp, V(cosf(a) * d, 0, sinf(a) * d));
        float ed, s;
        int e = nearest_edge(p, 90, &ed, &s);
        if (e < 0 || edges[e].bridge && frand() < 0.5f) continue;
        int from = frand() < 0.5f ? edges[e].a : edges[e].b;
        float L = edges[e].len, i0 = nodes[from].inset + 4, i1 = nodes[edge_other(e, from)].inset + 7;
        if (L - i0 - i1 < 4) continue;
        float sp = i0 + frand() * (L - i0 - i1);
        Car tmp;
        memset(&tmp, 0, sizeof(tmp));
        tmp.lane_i = frand() < 0.5f;
        v3 at = lane_pos(e, from, sp, lane_off(&tmp, e));
        if (!allow_visible && in_view(at, 10) && dist2d(at, cam.pos) < 260) continue;
        if (car_near(at, 12)) continue;
        int style = dstyle[district_at(at)];
        Car *c = spawn_car(frand() < 0.05f ? VM_POLICE : pick_model(style), at, 0, DRV_AI);
        if (!c) return false;
        c->lane_i = tmp.lane_i;
        place_on_edge(c, e, from, sp);
        c->pos.y = ground_at(c->pos.x, c->pos.z, c->pos.y + 1, 0.5f);
        c->vel = mul(fwd_of(c->yaw), edges[e].hwy >= 0 ? 18 : 8);
        return true;
    }
    return false;
}
static bool slot_used[MAXSLOT];
static void spawn_parked(void) {
    v3 fp = focus_pos();
    if (count_cars(DRV_NONE) > 24) return;
    for (int k = 0; k < 6; k++) {
        int s = rand() % (nslot ? nslot : 1);
        if (!nslot || slot_used[s]) continue;
        float d = dist2d(slots[s].pos, fp);
        if (d < 70 || d > 200) continue;
        if (in_view(slots[s].pos, 5) && d < 140) continue;
        if (car_near(slots[s].pos, 5)) continue;
        int m = frand() < 0.15f ? VM_SPORTS : frand() < 0.2f ? VM_MUSCLE : frand() < 0.2f ? VM_PICKUP : VM_SEDAN;
        Car *c = spawn_car(m, slots[s].pos, slots[s].yaw, DRV_NONE);
        if (!c) return;
        c->slot = s;
        slot_used[s] = true;
    }
}
static int ped_target = 60;
static void spawn_peds(float rmin, float rmax, int n) {
    v3 fp = focus_pos();
    for (int k = 0; k < n; k++) {
        float a = frand() * 6.2831853f, d = rmin + frand() * (rmax - rmin);
        v3 p = add(fp, V(cosf(a) * d, 0, sinf(a) * d));
        int c, bi, bj;
        if (!block_of(p, &c, &bi, &bj)) continue;
        int st = dstyle[block_dist(c, bi, bj)];
        float dens = st == ST_DOWNTOWN || st == ST_MIDRISE || st == ST_OLD ? 1 : st == ST_INDUSTRIAL ? 0.25f : st == ST_BEACH ? 0.8f : 0.55f;
        if (frand() > dens) continue;
        float s = frand() * 4 * (PITCH - 2 * WALK);
        v3 at = perim_pt(c, bi, bj, s);
        if (dist2d(at, cam.pos) < 60 && in_view(at, 5)) continue;
        Ped *q = spawn_ped(at, false);
        if (!q) return;
        q->city = c; q->bi = bi; q->bj = bj; q->s = s;
    }
}

/* ================================================================== traffic + police driving */
static Route cop_route;
static int cop_route_frame;
static void obstacle_ahead(const Car *c, float *dist_out, bool *is_player) {
    v3 f, r;
    car_axes(c, &f, &r);
    const CarSpec *S = &SPEC[c->model];
    float best = 999;
    *is_player = false;
    float look = 7 + fabsf(car_speed(c)) * 1.4f;
    for (int i = 0; i < MAXCAR; i++) {
        const Car *o = &car[i];
        if (!o->on || o == c) continue;
        v3 d = sub(o->pos, c->pos);
        float lz = dot(d, f), lx = dot(d, r);
        if (lz < 0 || lz > look + S->L) continue;
        if (fabsf(lx) > 1.9f + SPEC[o->model].W * 0.3f) continue;
        float gap = lz - S->L / 2 - SPEC[o->model].L / 2;
        if (gap < best) { best = gap; *is_player = o->driver == DRV_PLAYER; }
    }
    for (int i = 0; i < MAXPED; i++) {
        const Ped *p = &ped[i];
        if (!p->on || p->state == PS_DOWN) continue;
        v3 d = sub(p->pos, c->pos);
        float lz = dot(d, f), lx = dot(d, r);
        if (lz < 0 || lz > look + S->L || fabsf(lx) > 1.6f) continue;
        float gap = lz - S->L / 2 - 0.5f;
        if (gap < best) best = gap;
    }
    if (PL.car < 0) {
        v3 d = sub(PL.pos, c->pos);
        float lz = dot(d, f), lx = dot(d, r);
        if (lz > 0 && lz < look + S->L && fabsf(lx) < 1.6f) { float gap = lz - S->L / 2 - 0.5f; if (gap < best) { best = gap; *is_player = true; } }
    }
    *dist_out = best;
}
static void steer_to(Car *c, v3 target, float dt) {
    float want = yaw_of(sub(target, c->pos));
    float err = wrap_pi(want - c->yaw);
    float sp = car_speed(c);
    if (sp < -0.5f) err = -err;
    float s = clampf(err * 2.4f, -1, 1);
    c->steer += (s - c->steer) * fminf(1, dt * 8);
}
static void drive_ai(Car *c, float dt) {
    float sp = car_speed(c);
    if (c->nwp < 4) plan_turn(c, random_turn(c));
    /* pop reached waypoints */
    for (int k = 0; k < 3 && c->nwp > 0; k++) {
        v3 d = sub(c->wp[0], c->pos);
        float dd = len2d(d);
        if (c->wstop[0] >= 0) {
            if (dd < 3.5f) {
                int ls = light_state(c->wstop[0], c->wdir[0]);
                if (ls == 0 || c->panic_t > 0 || dot(d, fwd_of(c->yaw)) < -1) { wp_pop(c); continue; }
            }
            break;
        }
        if (dd < 5.5f + fabsf(sp) * 0.15f || (dd < 12 && dot(d, fwd_of(c->yaw)) < 0)) wp_pop(c);
        else break;
    }
    if (c->nwp < 4) plan_turn(c, random_turn(c));
    v3 tgt = c->wp[0];
    if (c->nwp > 1 && c->wstop[0] < 0 && len2d(sub(c->wp[0], c->pos)) < 9) tgt = lerp3(c->wp[0], c->wp[1], 0.4f);
    steer_to(c, tgt, dt);
    float want = c->panic_t > 0 ? fmaxf(20, c->wspd[0]) : c->wspd[0];
    float err = fabsf(wrap_pi(yaw_of(sub(tgt, c->pos)) - c->yaw));
    if (err > 0.25f) want = fminf(want, 7.5f);
    /* red light ahead */
    for (int k = 0; k < c->nwp && k < 3; k++) {
        if (c->wstop[k] < 0) continue;
        int ls = light_state(c->wstop[k], c->wdir[k]);
        if (ls != 0 && c->panic_t <= 0) {
            float dstop = len2d(sub(c->wp[k], c->pos));
            if (ls == 2 || dstop > 10) want = fminf(want, fmaxf(0, (dstop - 2.5f) * 0.6f));
        }
        break;
    }
    float gap;
    bool pl;
    obstacle_ahead(c, &gap, &pl);
    /* gridlock breaker: after waiting long behind something that is not the player, creep through */
    if (gap < 2.5f && !pl && fabsf(sp) < 0.5f) c->cop_t += dt; else if (gap > 6) c->cop_t = 0;
    if (c->cop_t > 7) { gap = 99; if (c->cop_t > 10) c->cop_t = 0; }
    if (gap < 40) want = fminf(want, fmaxf(0, (gap - 2.5f) * 0.8f));
    if (gap < 3 && pl) {
        c->honk_t += dt;
        if (c->honk_t > 2.5f) { c->honk_t = -2 - frand() * 3; audio_play(c->model == VM_BUS || c->model == VM_VAN ? SFX_HORN_TRUCK : SFX_HORN, c->pos.x, c->pos.y + 1, c->pos.z, 0.8f, 0.95f + frand() * 0.1f); }
    } else if (c->honk_t > 0) c->honk_t = 0;
    if (c->panic_t > 0) {
        c->panic_t -= dt;
        c->honk_t += dt;
        if (c->honk_t > 1.2f) { c->honk_t = -frand() * 2; audio_play(SFX_HORN, c->pos.x, c->pos.y + 1, c->pos.z, 0.7f, 1.05f); }
    }
    c->thr = sp < want - 0.5f ? clampf((want - sp) * 0.3f, 0.15f, 1) : 0;
    c->brk = sp > want + 0.5f ? clampf((sp - want) * 0.25f, 0.2f, 1) : 0;
    c->hb = false;
    /* unstick: if blocked by geometry for long, reverse a little */
    if (fabsf(sp) < 0.4f && want > 3) c->stuck_t += dt; else c->stuck_t = fmaxf(0, c->stuck_t - dt);
    if (c->stuck_t > 4) { c->back_t = 1.5f; c->stuck_t = 0; }
    if (c->back_t > 0) { c->back_t -= dt; c->thr = 0; c->brk = 1; c->steer = -c->steer; }
}

/* police: route over the grid toward the player, then ram/box them in */
static v3 player_target_pos(void) { return PL.car >= 0 ? car[PL.car].pos : PL.pos; }
static v3 player_vel(void) { return PL.car >= 0 ? car[PL.car].vel : PL.vel; }
static void drive_cop(Car *c, float dt) {
    v3 tp = player_target_pos();
    float d = dist2d(c->pos, tp);
    float sp = car_speed(c);
    c->siren = G.stars > 0;
    if (c->cop_state == CS_BLOCK) {
        c->thr = 0; c->brk = 1; c->steer = 0;
        if (d < 35 || G.stars == 0) c->cop_state = CS_PURSUE;
        return;
    }
    if (G.stars == 0) { /* stand down: rejoin traffic */
        c->driver = DRV_AI;
        c->siren = false;
        int n = nearest_road_node(c->pos);
        c->nwp = 0;
        c->tn = n;
        c->te = nodes[n].e[0];
        wp_push(c, nodes[n].p, -1, norm(sub(nodes[n].p, c->pos)), c->cruise);
        return;
    }
    bool direct = d < 55 && ray_world(add(c->pos, V(0, 1, 0)), norm(sub(add(tp, V(0, 1, 0)), add(c->pos, V(0, 1, 0)))), d) >= d - 1;
    v3 target;
    float want = 26 + G.stars * 4;
    if (direct) {
        target = add(tp, mul(player_vel(), clampf(d / 25, 0, 1.2f)));
        c->nwp = 0;
        float pv = len2d(player_vel());
        if (pv < 15) want = fminf(want, sqrtf(fmaxf(0, 2 * 12 * (d - 5.4f))) + pv * 0.8f);
    } else {
        if (c->nwp == 0 || len2d(sub(c->wp[0], c->pos)) < 9) {
            int n = c->nwp == 0 || c->tn < 0 ? nearest_road_node(c->pos) : c->tn;
            int e = route_next_edge(&cop_route, n, c->te);
            c->nwp = 0;
            if (e < 0) { target = tp; goto steer; }
            c->te = e; c->tn = edge_other(e, n);
            wp_push(c, lane_pos(e, n, edges[e].len, 1.2f), -1, edge_dir(e, n), want);
        }
        target = c->wp[0];
        float err = fabsf(wrap_pi(yaw_of(sub(target, c->pos)) - c->yaw));
        if (err > 0.5f && len2d(sub(target, c->pos)) < 25) want = 11;
    }
steer:
    steer_to(c, target, dt);
    c->thr = sp < want ? 1 : 0;
    c->brk = sp > want + 3 ? 1 : 0;
    c->hb = false;
    if (fabsf(sp) < 1.0f && c->thr > 0) c->stuck_t += dt; else c->stuck_t = fmaxf(0, c->stuck_t - dt * 0.5f);
    if (c->stuck_t > 1.5f) { c->back_t = 1.1f; c->stuck_t = 0; }
    if (c->back_t > 0) { c->back_t -= dt; c->thr = 0; c->brk = 1; c->steer = -clampf(c->steer * 3, -1, 1); }
    /* cops leave the car to chase a player on foot */
    if (PL.car < 0 && d < 22 && fabsf(sp) < 2 && c->cop_t <= 0) {
        int n = 0;
        for (int i = 0; i < MAXPED; i++) if (ped[i].on && ped[i].cop) n++;
        if (n < 2 + G.stars) {
            v3 f, r;
            car_axes(c, &f, &r);
            Ped *p = spawn_ped(add(c->pos, mul(r, -1.6f)), true);
            if (p) { p->state = PS_COP; p->speed = 5.6f + G.stars * 0.25f; audio_play(SFX_DOOR, c->pos.x, c->pos.y, c->pos.z, 0.7f, 1); audio_play(SFX_WHISTLE, c->pos.x, c->pos.y + 1.5f, c->pos.z, 0.8f, 1); }
        }
        c->cop_t = 5;
    }
    if (c->cop_t > 0) c->cop_t -= dt;
}

static bool spawn_cop(float rmin, float rmax) {
    v3 fp = focus_pos();
    for (int t = 0; t < 16; t++) {
        float a = frand() * 6.2831853f, d = rmin + frand() * (rmax - rmin);
        v3 p = add(fp, V(cosf(a) * d, 0, sinf(a) * d));
        int n = nearest_road_node(p);
        v3 at = nodes[n].p;
        if (car_near(at, 10)) continue;
        if (in_view(at, 0) && dist2d(at, cam.pos) < 140) continue;
        Car *c = spawn_car(VM_POLICE, at, yaw_of(sub(fp, at)), DRV_COP);
        if (!c) return false;
        c->siren = true;
        c->tn = n; c->te = -1; c->nwp = 0;
        c->vel = mul(fwd_of(c->yaw), 10);
        return true;
    }
    return false;
}
static void spawn_roadblock(void) {
    if (PL.car < 0) return;
    Car *pc = &car[PL.car];
    if (car_speed(pc) < 10) return;
    v3 ahead = add(pc->pos, mul(norm(pc->vel), 150));
    v3 c0 = nodes[nearest_road_node(ahead)].p;
    if (dist2d(c0, pc->pos) < 90 || car_near(c0, 14)) return;
    for (int k = -1; k <= 1; k += 2) {
        Car *c = spawn_car(VM_POLICE, add(c0, V(k * 2.7f, 0, k * 1.0f)), 0.3f + k * 0.2f + yaw_of(pc->vel) + 1.5708f, DRV_COP);
        if (c) { c->cop_state = CS_BLOCK; c->siren = true; }
    }
    message("ROADBLOCK AHEAD!", 2.5f);
}

/* ================================================================== pedestrians update */
static void update_peds(float dt) {
    v3 pp = player_target_pos();
    for (int i = 0; i < MAXPED; i++) {
        Ped *p = &ped[i];
        if (!p->on) continue;
        float dpl = dist2d(p->pos, pp);
        if (dpl > 280 && p->fare < 0) { p->on = false; continue; }
        v3 want_v = V(0, 0, 0);
        float g = ground_at(p->pos.x, p->pos.z, p->pos.y, 0.3f);
        switch (p->state) {
        case PS_WALK: {
            p->s += p->speed * p->dirn * dt;
            v3 t = perim_pt(p->city, p->bi, p->bj, p->s + p->dirn * 1.5f);
            v3 d = sub(t, p->pos);
            d.y = 0;
            want_v = mul(norm(d), p->speed);
            if (len2d(sub(perim_pt(p->city, p->bi, p->bj, p->s), p->pos)) > 6) { p->state = PS_RETURN; p->target = perim_pt(p->city, p->bi, p->bj, p->s); }
            float L = PITCH - 2 * WALK;
            float sm = fmodf(fmodf(p->s, 4 * L) + 4 * L, 4 * L);
            float corner = fmodf(sm, L);
            if ((corner < 0.05f || corner > L - 0.05f) && p->timer <= 0) {
                p->timer = 3;
                if (frand() < 0.35f) {
                    int ci = (int)floorf(sm / L + 0.5f) % 4; /* corner index 0 NW,1 NE,2 SE,3 SW */
                    int ndx[4][2][2] = {{{-1, 0}, {0, -1}}, {{1, 0}, {0, -1}}, {{1, 0}, {0, 1}}, {{-1, 0}, {0, 1}}};
                    int pick = rand() % 2;
                    int nbi = p->bi + ndx[ci][pick][0], nbj = p->bj + ndx[ci][pick][1];
                    if (nbi >= 0 && nbj >= 0 && nbi < cities[p->city].nx && nbj < cities[p->city].nz) {
                        /* the matching corner of the neighbour block is mirrored across the road */
                        int mc = pick == 0 ? (ci == 0 ? 1 : ci == 1 ? 0 : ci == 2 ? 3 : 2) : (ci == 0 ? 3 : ci == 3 ? 0 : ci == 1 ? 2 : 1);
                        int cn[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
                        p->xnode = city_node(p->city, p->bi + cn[ci][0], p->bj + cn[ci][1]);
                        p->xaxis = pick == 0 ? 1 : 0;
                        p->wait_t = 0;
                        p->state = PS_WAITX;
                        p->bi = nbi; p->bj = nbj;
                        p->s = mc * L;
                        p->target = perim_pt(p->city, nbi, nbj, p->s);
                    }
                }
            }
            if (p->timer > 0) p->timer -= dt;
            break;
        }
        case PS_WAITX: {
            /* wait at the kerb for the traffic on the road being crossed to get a red light */
            v3 mid = mul(add(p->pos, p->target), 0.5f);
            bool ok;
            if (!on_road(mid.x, mid.z, 0)) ok = true;
            else if (nodes[p->xnode].light) ok = light_state(p->xnode, p->xaxis ? V(0, 0, 1) : V(1, 0, 0)) == 2;
            else {
                ok = true;
                for (int k = 0; k < MAXCAR && ok; k++)
                    if (car[k].on && fabsf(car_speed(&car[k])) > 2 && dist2d(car[k].pos, mid) < 22) ok = false;
            }
            p->wait_t += dt;
            p->yaw += wrap_pi(yaw_of(sub(p->target, p->pos)) - p->yaw) * fminf(1, dt * 5);
            if (ok || p->wait_t > 35) p->state = PS_CROSS;
            break;
        }
        case PS_CROSS:
        case PS_RETURN: {
            v3 d = sub(p->target, p->pos);
            d.y = 0;
            float l = len2d(d);
            float spd = p->state == PS_CROSS ? p->speed * 1.25f : p->speed;
            want_v = mul(norm(d), spd);
            if (l < 0.6f) { p->state = PS_WALK; p->timer = 3; }
            break;
        }
        case PS_FLEE: {
            v3 d = sub(p->target, p->pos);
            d.y = 0;
            want_v = mul(norm(d), 4.8f);
            p->timer -= dt;
            if (p->timer <= 0 || len2d(d) < 1) {
                int c, bi, bj;
                if (!block_of(p->pos, &c, &bi, &bj)) { p->on = false; continue; }
                p->city = c; p->bi = bi; p->bj = bj;
                /* nearest perimeter parameter */
                float best = 1e9f, bs = 0, L = PITCH - 2 * WALK;
                for (float s = 0; s < 4 * L; s += 2) { float dd = dist2d(perim_pt(c, bi, bj, s), p->pos); if (dd < best) { best = dd; bs = s; } }
                p->s = bs;
                p->target = perim_pt(c, bi, bj, bs);
                p->state = PS_RETURN;
            }
            break;
        }
        case PS_DOWN: {
            p->vel.y -= 18 * dt;
            p->pos = add(p->pos, mul(p->vel, dt));
            p->tumble += p->spin * dt;
            if (p->pos.y <= g) {
                p->pos.y = g;
                if (p->vel.y < -3) { p->vel.y *= -0.25f; p->spin *= 0.5f; } else p->vel.y = 0;
                p->vel.x *= 1 - fminf(1, dt * 4);
                p->vel.z *= 1 - fminf(1, dt * 4);
                p->spin *= 1 - fminf(1, dt * 3);
            }
            v3 n;
            if (collide_circle(&p->pos, 0.3f, 1.0f, &n)) { float vn = dot(p->vel, n); if (vn < 0) p->vel = sub(p->vel, mul(n, vn * 1.5f)); }
            p->timer -= dt;
            if (p->timer <= 0 && p->pos.y <= g + 0.05f) {
                p->tumble = 0;
                if (p->cop) p->state = PS_COP;
                else ped_flee(p, pp, 5);
            }
            continue;
        }
        case PS_HAIL: {
            p->yaw = yaw_of(sub(pp, p->pos));
            p->anim = 0;
            break;
        }
        case PS_COP: {
            if (G.stars == 0) { p->state = PS_RETURN; p->target = perim_pt(p->city, p->bi, p->bj, 0); p->cop = true; break; }
            v3 d = sub(pp, p->pos);
            d.y = 0;
            want_v = mul(norm(d), p->speed);
            if (PL.car >= 0) {
                /* player drove off: head back toward the nearest cop car, or give up */
                want_v = mul(want_v, 0.6f);
                if (dpl > 60) { p->on = false; continue; }
            }
            break;
        }
        default: break;
        }
        /* integrate walking */
        p->vel.x += (want_v.x - p->vel.x) * fminf(1, dt * 6);
        p->vel.z += (want_v.z - p->vel.z) * fminf(1, dt * 6);
        p->pos.x += p->vel.x * dt;
        p->pos.z += p->vel.z * dt;
        p->pos.y = g;
        if (p->state == PS_FLEE || p->state == PS_COP || p->state == PS_RETURN) collide_circle(&p->pos, 0.3f, 1.7f, NULL);
        float sp = len2d(p->vel);
        if (sp > 0.2f && p->state != PS_HAIL && p->state != PS_WAITX) p->yaw += wrap_pi(yaw_of(p->vel) - p->yaw) * fminf(1, dt * 8);
        p->anim += sp * dt * 2.2f;
        /* shy away from fast cars heading at them */
        if (p->state == PS_WALK || p->state == PS_CROSS || p->state == PS_WAITX) {
            for (int k = 0; k < MAXCAR; k++) {
                Car *c = &car[k];
                if (!c->on || fabsf(car_speed(c)) < 9) continue;
                v3 d = sub(p->pos, c->pos);
                float l = len2d(d);
                if (l < 14 && dot(norm(d), norm(c->vel)) > 0.8f) { ped_flee(p, c->pos, 3); break; }
            }
        }
    }
}

/* ================================================================== car interactions: car-car, car-ped, car-prop */
static void car_contacts(float dt) {
    (void)dt;
    for (int a = 0; a < MAXCAR; a++) {
        Car *A = &car[a];
        if (!A->on) continue;
        const CarSpec *SA = &SPEC[A->model];
        v3 fa, ra;
        car_axes(A, &fa, &ra);
        int nA = SA->L > 7 ? 4 : 3;
        float rA = SA->W * 0.5f;
        for (int b = a + 1; b < MAXCAR; b++) {
            Car *B = &car[b];
            if (!B->on) continue;
            if (fabsf(A->pos.x - B->pos.x) > 14 || fabsf(A->pos.z - B->pos.z) > 14 || fabsf(A->pos.y - B->pos.y) > 2.5f) continue;
            const CarSpec *SB = &SPEC[B->model];
            v3 fb, rb;
            car_axes(B, &fb, &rb);
            int nB = SB->L > 7 ? 4 : 3;
            float rB = SB->W * 0.5f;
            for (int i = 0; i < nA; i++)
                for (int j = 0; j < nB; j++) {
                    v3 pa = add(A->pos, mul(fa, -SA->L / 2 + rA + (SA->L - 2 * rA) * i / (nA - 1)));
                    v3 pb = add(B->pos, mul(fb, -SB->L / 2 + rB + (SB->L - 2 * rB) * j / (nB - 1)));
                    v3 d = sub(pa, pb);
                    d.y = 0;
                    float l = len2d(d), min = rA + rB;
                    if (l >= min || l < 1e-4f) continue;
                    v3 n = mul(d, 1 / l);
                    float ma = SA->mass * (A->wrecked ? 1.5f : 1), mb = SB->mass * (B->wrecked ? 1.5f : 1);
                    float pen = min - l;
                    A->pos = add(A->pos, mul(n, pen * mb / (ma + mb)));
                    B->pos = sub(B->pos, mul(n, pen * ma / (ma + mb)));
                    float vn = dot(sub(A->vel, B->vel), n);
                    if (vn < 0) {
                        float jimp = -(1 + 0.3f) * vn / (1 / ma + 1 / mb);
                        A->vel = add(A->vel, mul(n, jimp / ma));
                        B->vel = sub(B->vel, mul(n, jimp / mb));
                        float imp = -vn;
                        if (imp > 3) {
                            v3 at = mul(add(pa, pb), 0.5f);
                            car_hit_effects(A, at, imp * mb / (ma + mb) * 1.4f, n);
                            car_hit_effects(B, at, imp * ma / (ma + mb) * 1.4f, mul(n, -1));
                            bool pa_ = A->driver == DRV_PLAYER, pb_ = B->driver == DRV_PLAYER;
                            if (pa_ || pb_) {
                                Car *o = pa_ ? B : A;
                                if (o->driver == DRV_AI && imp > 5) o->panic_t = 8;
                                if (o->driver == DRV_COP && imp > 4 && G.stars == 0) add_heat(1.0f);
                                else if (o->driver == DRV_COP && imp > 6) add_heat(0.15f);
                                else if (o->driver == DRV_AI && imp > 10) add_heat(0.12f);
                                o->last_hit_by_player = G.time;
                            }
                        }
                    }
                }
        }
        /* peds */
        float sp = len2d(A->vel);
        for (int k = 0; k < MAXPED; k++) {
            Ped *p = &ped[k];
            if (!p->on || fabsf(p->pos.x - A->pos.x) > 7 || fabsf(p->pos.z - A->pos.z) > 7) continue;
            v3 d = sub(p->pos, A->pos);
            float lz = dot(d, fa), lx = dot(d, ra);
            if (fabsf(lz) > SA->L / 2 + 0.3f || fabsf(lx) > SA->W / 2 + 0.3f || p->pos.y > A->pos.y + SA->H + 0.5f) continue;
            if (sp > 3 && p->state != PS_DOWN) {
                ped_knock(p, A->vel);
                A->vel = mul(A->vel, 0.93f);
                if (A->driver == DRV_PLAYER) {
                    G.stat_peds++;
                    add_heat(p->cop ? 1.0f : 0.45f);
                    scare_nearby(p->pos, 30, 5);
                    G.shake = fmaxf(G.shake, 0.2f);
                }
            } else if (p->state != PS_DOWN) {
                float px = lx > 0 ? SA->W / 2 + 0.35f : -SA->W / 2 - 0.35f;
                p->pos = add(A->pos, add(mul(fa, lz), mul(ra, px)));
            }
        }
        /* the player on foot */
        if (PL.car < 0 && G.state == ST_PLAY && fabsf(PL.pos.x - A->pos.x) < 7 && fabsf(PL.pos.z - A->pos.z) < 7) {
            v3 d = sub(PL.pos, A->pos);
            float lz = dot(d, fa), lx = dot(d, ra);
            if (fabsf(lz) < SA->L / 2 + 0.35f && fabsf(lx) < SA->W / 2 + 0.35f && PL.pos.y < A->pos.y + SA->H) {
                if (sp > 4 && PL.down_t <= 0) {
                    player_hurt(sp * 1.6f, A->pos);
                    PL.vel = add(mul(A->vel, 0.8f), V(0, 4, 0));
                    PL.down_t = 1.6f;
                    PL.ground = false;
                    audio_play(SFX_THUD, PL.pos.x, PL.pos.y, PL.pos.z, 1, 0.9f);
                    if (A->driver == DRV_AI) A->panic_t = 5;
                } else {
                    /* push the player out of the side the car is closest to */
                    float ex = SA->W / 2 + 0.36f - fabsf(lx), ez = SA->L / 2 + 0.36f - fabsf(lz);
                    if (ex < ez) PL.pos = add(PL.pos, mul(ra, lx > 0 ? ex : -ex));
                    else PL.pos = add(PL.pos, mul(fa, lz > 0 ? ez : -ez));
                }
            }
        }
        /* lamp posts */
        if (sp > 1) {
            int ids[4], n;
            props_near(A->pos, ids, &n);
            for (int k = 0; k < n; k++) {
                Prop *q = &prop[ids[k]];
                if (q->down) continue;
                v3 d = sub(q->pos, A->pos);
                float lz = dot(d, fa), lx = dot(d, ra);
                if (fabsf(lz) > SA->L / 2 + 0.2f || fabsf(lx) > SA->W / 2 + 0.2f) continue;
                if (sp > 5 || A->model == VM_BUS) {
                    knock_prop(ids[k], A->vel);
                    A->vel = mul(A->vel, 0.88f);
                    A->hp -= 3;
                    if (A->driver == DRV_PLAYER) G.shake = fmaxf(G.shake, 0.25f);
                } else {
                    float ex = SA->W / 2 + 0.21f - fabsf(lx), ez = SA->L / 2 + 0.21f - fabsf(lz);
                    if (ex < ez) A->pos = sub(A->pos, mul(ra, lx > 0 ? ex : -ex));
                    else A->pos = sub(A->pos, mul(fa, lz > 0 ? ez : -ez));
                    A->vel = mul(A->vel, 0.5f);
                }
            }
        }
    }
}

/* damage, fire, explosions, sinking */
static void eject_player_from(Car *c);
static void car_damage_state(Car *c, float dt) {
    if (c->wrecked) {
        c->wreck_t += dt;
        if (c->wreck_t < 25 && frand() < dt * 8) puff(add(c->pos, V(frs() * 0.8f, 1.2f, frs() * 0.8f)), V(frs() * 0.3f, 2.2f, frs() * 0.3f), 0.9f, 0.04f, 3);
        return;
    }
    if (c->sunk) {
        if (c->driver == DRV_PLAYER && c->sink_t > 1.2f) eject_player_from(c);
        return;
    }
    const CarSpec *S = &SPEC[c->model];
    float frac = c->hp / S->hp;
    v3 f = fwd_of(c->yaw);
    v3 hood = add(c->pos, add(mul(f, S->L * 0.38f), V(0, S->H * 0.6f, 0)));
    if (frac < 0.45f) {
        c->smoke_acc += dt * (frac < 0.25f ? 14 : 6);
        while (c->smoke_acc > 1) {
            c->smoke_acc -= 1;
            float dark = frac < 0.25f ? 0.03f : 0.25f;
            puff(add(hood, V(frs() * 0.3f, 0, frs() * 0.3f)), add(mul(c->vel, 0.6f), V(frs() * 0.3f, 1.6f, frs() * 0.3f)), 0.35f, dark, 1.6f);
        }
    }
    if (frac < 0.12f) {
        c->fire_t += dt;
        if (frand() < dt * 30) spawn_part(add(hood, V(frs() * 0.4f, 0, frs() * 0.4f)), V(frs() * 0.5f, 2.5f + frand() * 2, frs() * 0.5f), V(5, 1.8f, 0.3f), 0.4f + frand() * 0.3f, 0.35f, PK_GLOW, 0.3f, -2);
        if (frand() < dt * 10) add_light(hood, V(4, 1.6f, 0.3f), 9, 0.15f);
        if (c->fire_t > 5.5f) c->hp = -1;
    }
    if (c->hp <= 0) {
        c->wrecked = true;
        c->wreck_t = 0;
        c->siren = false;
        explosion(c->pos, 1.0f);
        c->vy = 6;
        c->airborne = true;
        c->vel = add(c->vel, V(frs() * 2, 0, frs() * 2));
        scare_nearby(c->pos, 45, 6);
        if (c->driver == DRV_PLAYER) { PL.hp = -1; }
        else if (G.time - c->last_hit_by_player < 15) { add_heat(c->model == VM_POLICE ? 1.2f : 0.6f); G.stat_cars++; }
        /* blast damage */
        for (int i = 0; i < MAXCAR; i++) {
            Car *o = &car[i];
            if (!o->on || o == c || o->wrecked) continue;
            float d = dist2d(o->pos, c->pos);
            if (d < 9) { o->hp -= (9 - d) * 9; o->vel = add(o->vel, mul(norm(sub(o->pos, c->pos)), (9 - d) * 1.2f)); }
        }
        for (int i = 0; i < MAXPED; i++) {
            Ped *p = &ped[i];
            if (p->on && dist2d(p->pos, c->pos) < 8) ped_knock(p, mul(norm(sub(p->pos, c->pos)), 12));
        }
        if (PL.car < 0) {
            float d = dist2d(PL.pos, c->pos);
            if (d < 9) { player_hurt((9 - d) * 9, c->pos); PL.vel = add(mul(norm(sub(PL.pos, c->pos)), 10), V(0, 5, 0)); PL.down_t = 1.5f; PL.ground = false; }
        }
        if (c->driver == DRV_AI || c->driver == DRV_COP) {
            v3 r = right_of(c->yaw);
            Ped *p = spawn_ped(add(c->pos, mul(r, -2.4f)), c->driver == DRV_COP);
            if (p) ped_knock(p, mul(r, -6));
            c->driver = DRV_NONE;
        }
    }
}

/* update all vehicles: AI decisions, physics, despawn */
static int count_cops_near(float r) { int n = 0; v3 fp = focus_pos(); for (int i = 0; i < MAXCAR; i++) if (car[i].on && car[i].driver == DRV_COP && !car[i].wrecked && dist2d(car[i].pos, fp) < r) n++; return n; }
static void update_cars(float dt) {
    v3 fp = focus_pos();
    /* police route field toward the player, refreshed a few times per second */
    if (G.stars > 0 && ++cop_route_frame % 15 == 0) {
        route_build(&cop_route, nearest_road_node(fp));
    }
    for (int i = 0; i < MAXCAR; i++) {
        Car *c = &car[i];
        if (!c->on) continue;
        float d = dist2d(c->pos, fp);
        bool keep = c->driver == DRV_PLAYER || i == PL.last_car || c->mission >= 0 || c->keep || c->owned;
        if (c->driver == DRV_AI && fabsf(car_speed(c)) < 0.3f) c->idle_t += dt; else c->idle_t = 0;
        if (!keep && (d > 340 || (c->wrecked && c->wreck_t > 40 && d > 80) || (c->sunk && c->sink_t > 10) || (c->idle_t > 30 && d > 90 && !in_view(c->pos, 10)))) {
            if (c->slot >= 0) slot_used[c->slot] = false;
            c->on = false;
            continue;
        }
        if (c->driver == DRV_PLAYER && c->sunk && c->sink_t > 10 && PL.car != i) { c->on = false; continue; }
        if (!c->wrecked && !c->sunk) {
            if (c->driver == DRV_AI) drive_ai(c, dt);
            else if (c->driver == DRV_COP) drive_cop(c, dt);
            else if (c->driver == DRV_NONE) { c->thr = 0; c->brk = 0.4f; c->hb = true; c->steer = 0; }
        }
        /* far-away cars only need a cheap update */
        if (d > 200 && c->driver != DRV_PLAYER && !c->airborne) {
            c->pos.x += c->vel.x * dt;
            c->pos.z += c->vel.z * dt;
            if (c->driver == DRV_AI) {
                float sp = car_speed(c), want = c->cruise;
                sp += clampf(want - sp, -6 * dt, 4 * dt);
                if (c->nwp > 0) { c->yaw += clampf(wrap_pi(yaw_of(sub(c->wp[0], c->pos)) - c->yaw), -1.5f * dt, 1.5f * dt); }
                c->vel = mul(fwd_of(c->yaw), sp);
            }
            continue;
        }
        car_physics(c, dt);
        car_damage_state(c, dt);
    }
    car_contacts(dt);
}
