/* game.c - player control, wanted level, missions and side activities, collectibles, HUD, saving. */

typedef struct {
    float fwd, side, look_x, look_y;
    bool jump, sprint, enter, horn, radio_next, radio_prev, taxi, zoom, cam_reset;
} Input;

/* ================================================================== environment (time of day, weather) */
static v3 SUN_DIR, LIGHT_DIR, LIGHT_COL, AMB_SKY, AMB_GND;
static float NIGHT, FOG_DEN, EXPOSURE;
static void update_env(void) {
    float a = (G.tod - 6) / 12 * 3.14159265f;
    SUN_DIR = norm(V(cosf(a) * 0.85f, sinf(a), 0.42f));
    float day = smooth01(-0.12f, 0.16f, SUN_DIR.y);
    NIGHT = 1 - smooth01(-0.14f, 0.06f, SUN_DIR.y);
    float dusk = expf(-powf(SUN_DIR.y * 4.5f, 2));
    float rain = G.rain;
    if (SUN_DIR.y > 0) {
        LIGHT_DIR = norm(V(SUN_DIR.x, fmaxf(SUN_DIR.y, 0.06f), SUN_DIR.z));
        v3 c = lerp3(V(3.4f, 3.15f, 2.9f), V(3.2f, 1.7f, 0.8f), dusk);
        LIGHT_COL = mul(c, smooth01(0.0f, 0.12f, SUN_DIR.y) * (1 - rain * 0.75f));
    } else {
        v3 md = V(-SUN_DIR.x, -SUN_DIR.y, SUN_DIR.z * 0.6f + 0.25f);
        LIGHT_DIR = norm(V(md.x, fmaxf(md.y, 0.08f), md.z));
        LIGHT_COL = mul(V(0.16f, 0.20f, 0.32f), smooth01(0.0f, 0.12f, -SUN_DIR.y) * (1 - rain * 0.6f));
    }
    AMB_SKY = lerp3(V(0.025f, 0.03f, 0.055f), V(0.42f, 0.52f, 0.68f), day);
    AMB_SKY = lerp3(AMB_SKY, V(0.5f, 0.35f, 0.3f), dusk * 0.3f * day);
    AMB_GND = lerp3(V(0.02f, 0.016f, 0.014f), V(0.20f, 0.18f, 0.15f), day);
    if (rain > 0) { AMB_SKY = lerp3(AMB_SKY, mul(V(0.36f, 0.4f, 0.46f), day + 0.08f), rain * 0.6f); }
    FOG_DEN = lerpf(0.00045f, 0.0006f, NIGHT) + rain * 0.0022f;
    EXPOSURE = lerpf(0.82f, 1.55f, NIGHT) * (1 + rain * 0.15f);
}
static void update_weather(float dt) {
    G.rain_timer -= dt;
    if (G.rain_timer <= 0) {
        G.rain_target = frand() < 0.28f ? 0.6f + frand() * 0.4f : 0;
        G.rain_timer = 120 + frand() * 240;
    }
    G.rain += clampf(G.rain_target - G.rain, -dt * 0.02f, dt * 0.02f);
}

/* ================================================================== wanted level */
static void add_heat(float h) {
    if (G.state != ST_PLAY || G.bot == 2) return;
    int before = G.stars;
    G.heat = clampf(G.heat + h, 0, 5.99f);
    G.stars = (int)G.heat;
    G.evade_t = 0;
    if (G.stars > before) { audio_play2d(SFX_STAR_UP, 0.7f, 1); G.star_flash = 1.5f; }
}
static float cop_spawn_t, roadblock_t;
static bool cop_sees_player(void) {
    v3 tp = player_target_pos();
    for (int i = 0; i < MAXCAR; i++) {
        Car *c = &car[i];
        if (!c->on || c->driver != DRV_COP || c->wrecked) continue;
        float d = dist2d(c->pos, tp);
        if (d < 40) return true;
        if (d < 110) {
            v3 o = add(c->pos, V(0, 1.5f, 0)), t = add(tp, V(0, 1.2f, 0));
            if (ray_world(o, norm(sub(t, o)), d) >= d - 1) return true;
        }
    }
    for (int i = 0; i < MAXPED; i++) if (ped[i].on && ped[i].cop && ped[i].state == PS_COP && dist2d(ped[i].pos, tp) < 35) return true;
    if (G.stars >= 4 && G.heli_t > 3) return true;
    return false;
}
static void start_busted(void);
static void update_wanted(float dt) {
    G.star_flash -= dt;
    if (G.stars == 0) { G.bust_t = 0; G.heli_t = 0; return; }
    int want = G.stars * 2 + (G.stars >= 4 ? 1 : 0);
    if (want > 9) want = 9;
    cop_spawn_t -= dt;
    if (count_cops_near(300) < want && cop_spawn_t <= 0) { spawn_cop(130, 230); cop_spawn_t = 3.5f - G.stars * 0.4f; }
    if (G.stars >= 3) {
        roadblock_t -= dt;
        if (roadblock_t <= 0) { spawn_roadblock(); roadblock_t = 28 - G.stars * 3; }
    }
    if (G.stars >= 4) G.heli_t += dt; else G.heli_t = 0;
    if (cop_sees_player()) G.evade_t = 0;
    else {
        G.evade_t += dt;
        if (G.evade_t > 9 + G.stars * 3.5f) {
            G.heat = 0; G.stars = 0; G.evade_t = 0;
            message("YOU LOST THEM", 3);
            audio_play2d(SFX_CHECKPOINT, 0.6f, 0.8f);
            return;
        }
    }
    /* busted: stopped with police right next to you */
    v3 tp = player_target_pos();
    bool close = false;
    float pspeed = PL.car >= 0 ? fabsf(car_speed(&car[PL.car])) : len2d(PL.vel);
    for (int i = 0; i < MAXCAR; i++) {
        Car *c = &car[i];
        if (c->on && c->driver == DRV_COP && !c->wrecked && dist2d(c->pos, tp) < (PL.car >= 0 ? 6.5f : 4.0f) && fabsf(car_speed(c)) < 6) close = true;
    }
    for (int i = 0; i < MAXPED; i++)
        if (ped[i].on && ped[i].cop && ped[i].state == PS_COP && dist2d(ped[i].pos, tp) < 1.5f) close = true;
    if (close && pspeed < (PL.car >= 0 ? 2.5f : 2.2f)) G.bust_t += dt;
    else G.bust_t = fmaxf(0, G.bust_t - dt * 1.5f);
    if (G.bust_t > (PL.car >= 0 ? 2.2f : 1.3f)) start_busted();
}

/* ================================================================== missions and activities */
enum { MT_NONE, MT_DELIVERY, MT_TAXI, MT_RACE };
#define NRACE 4
#define MAXCP 14
typedef struct { int i, j, dir; v3 start; float yaw; v3 cp[MAXCP]; int ncp; float par; const char *name; } Race;
static Race races[NRACE];
static const char *RACE_NAMES[NRACE] = {"HARBOR SPRINT", "DOWNTOWN DASH", "SEABRIGHT LOOP", "IRONSIDE RUN"};
static struct {
    int type, stage, race, cp, streak, fare_ped, poi;
    float timer, limit, t0, dist;
    v3 target;
    bool taxi_duty;
    float cooldown;
} MS;
/* persistent progress */
#define NTAPE 30
static v3 tapes[NTAPE];
static unsigned tapes_found, jumps_done;
static float race_best[NRACE];
static int deliveries, fares;
static float play_time;

static void build_races(void) {
    int starts[NRACE][3] = {{2, 24, 0}, {9, 8, 0}, {20, 2, 1}, {3, 20, 0}};
    for (int r = 0; r < NRACE; r++) {
        Race *R = &races[r];
        R->name = RACE_NAMES[r];
        R->i = starts[r][0]; R->j = starts[r][1]; R->dir = starts[r][2];
        for (unsigned attempt = 0; attempt < 50; attempt++) {
        unsigned seed = 1000u + (unsigned)r * 77u + attempt * 7919u;
        static unsigned char vis[NB + 1][NB + 1];
        memset(vis, 0, sizeof(vis));
        int i = R->i, j = R->j, d = R->dir;
        vis[i][j] = 1;
        R->ncp = 0;
        float length = 0;
        int edges = 0;
        for (int step = 0; step < 60 && R->ncp < 10; step++) {
            int opts[3], n = 0;
            int cand[3] = {d, (d + 1) % 4, (d + 3) % 4};
            for (int k = 0; k < 3; k++) {
                int a = i + DX[cand[k]], b = j + DZ[cand[k]];
                if (edge_ok(i, j, cand[k]) && !vis[a][b]) opts[n++] = cand[k];
            }
            if (!n) break;
            seed = hashu(seed);
            int pick = step == 0 || ((seed % 100) < 55 && edge_ok(i, j, d) && !vis[i + DX[d]][j + DZ[d]]) ? d : opts[seed % (unsigned)n];
            if (!edge_ok(i, j, pick) || vis[i + DX[pick]][j + DZ[pick]]) pick = opts[seed % (unsigned)n];
            d = pick;
            i += DX[d]; j += DZ[d];
            vis[i][j] = 1;
            length += PITCH;
            edges++;
            if (edges >= 2 + (int)(hashu(seed + 5) % 2)) { R->cp[R->ncp++] = node_pos(i, j); edges = 0; }
        }
        R->par = length;
        if (R->ncp >= 9) break;
        }
        float length = R->par;
        R->start = lane_pt(R->i, R->j, R->dir, 22, LANE_OFF);
        R->yaw = yaw_of(dir_vec(R->dir));
        R->par = length / 24.0f + 8;
    }
}
static void build_tapes(void) {
    srand(4242);
    int n = 0, tries = 0;
    while (n < NTAPE && tries < 100000) {
        tries++;
        v3 p;
        float r = frand();
        if (r < 0.62f) { /* inside city blocks: alleys, yards, plazas */
            int bi = rand() % NB, bj = rand() % NB;
            p = V(bi * PITCH + 9 + frand() * 46, 0, bj * PITCH + 9 + frand() * 46);
        } else if (r < 0.8f) { p = V(CITY + 20 + frand() * 140, 0, frand() * CITY); }
        else if (r < 0.9f) { p = V(frand() * DOCK_X, 0, CITY + 20 + frand() * 170); }
        else { p = V(-90 + frand() * 80, 0, frand() * CITY); }
        p.y = ground_at(p.x, p.z, 50, 0.3f);
        if (p.y > 0.7f || water_at(p.x, p.z) || p.y < WATER_Y + 0.3f) continue;
        if (point_blocked(add(p, V(0, 0.1f, 0)), 1.0f, 1.8f)) continue;
        if (on_road(p.x, p.z, 1)) continue;
        bool close = false;
        for (int k = 0; k < n; k++) if (dist2d(tapes[k], p) < 150) close = true;
        if (close) continue;
        tapes[n++] = p;
    }
}

static int nearest_poi(int type, v3 p) {
    int best = -1;
    float bd = 1e9f;
    for (int k = 0; k < npoi; k++)
        if (poi[k].type == type) { float d = dist2d(poi[k].pos, p); if (d < bd) { bd = d; best = k; } }
    return best;
}
static v3 random_destination(v3 from, float dmin, float dmax) {
    for (int t = 0; t < 400; t++) {
        int bi = rand() % NB, bj = rand() % NB;
        v3 p = perim_pt(bi, bj, frand() * 4 * (PITCH - 2 * WALK));
        float d = dist2d(p, from);
        if (d < dmin || d > dmax) continue;
        int i, j;
        nearest_road_node(p, &i, &j);
        if (dist2d(node_pos(i, j), p) > 40) continue;
        return p;
    }
    return V(CITY / 2, 0, CITY / 2 + WALK);
}
static void pay(int amount) { G.money += amount; audio_play2d(SFX_CASH, 0.8f, 1); }
static void mission_end(bool ok, const char *why) {
    if (!ok) { audio_play2d(SFX_MISSION_FAIL, 0.8f, 1); bigtext("FAILED", why, 3); }
    if (MS.type == MT_TAXI && MS.fare_ped >= 0 && ped[MS.fare_ped].on) { ped[MS.fare_ped].state = PS_RETURN; ped[MS.fare_ped].fare = -1; }
    MS.type = MT_NONE;
    MS.taxi_duty = false;
    MS.cooldown = 4;
}
static void start_delivery(int poi_idx) {
    MS.type = MT_DELIVERY;
    MS.stage = 0;
    MS.poi = poi_idx;
    MS.target = random_destination(player_target_pos(), 450, 1200);
    MS.dist = dist2d(MS.target, player_target_pos());
    MS.limit = MS.dist / 12.5f + 25;
    MS.timer = MS.limit;
    bigtext("COURIER JOB", "DELIVER THE PACKAGE BEFORE TIME RUNS OUT", 3);
    audio_play2d(SFX_PICKUP, 0.8f, 1);
}
static void next_fare(void) {
    v3 fp = player_target_pos();
    for (int t = 0; t < 60; t++) {
        float a = frand() * 6.283f, d = 140 + frand() * 160;
        v3 p = add(fp, V(cosf(a) * d, 0, sinf(a) * d));
        int bi = (int)floorf(p.x / PITCH), bj = (int)floorf(p.z / PITCH);
        if (bi < 0 || bj < 0 || bi >= NB || bj >= NB) continue;
        float s = frand() * 4 * (PITCH - 2 * WALK);
        Ped *q = spawn_ped(perim_pt(bi, bj, s), false);
        if (!q) return;
        q->bi = bi; q->bj = bj; q->s = s;
        q->state = PS_HAIL;
        q->fare = 1;
        MS.fare_ped = (int)(q - ped);
        MS.stage = 0;
        MS.target = q->pos;
        MS.timer = 0;
        return;
    }
}
static void start_race(int r) {
    MS.type = MT_RACE;
    MS.race = r;
    MS.stage = 0;
    MS.cp = 0;
    MS.timer = 3.5f;
    Car *c = &car[PL.car];
    c->pos = races[r].start;
    c->yaw = races[r].yaw;
    c->vel = V(0, 0, 0);
    bigtext(races[r].name, "", 2.5f);
}
static bool mission_marker_active(void) { return MS.type == MT_NONE && MS.cooldown <= 0 && G.stars == 0; }

static void update_missions(float dt) {
    MS.cooldown -= dt;
    v3 pp = player_target_pos();
    bool in_car = PL.car >= 0;
    Car *c = in_car ? &car[PL.car] : NULL;
    float sp = in_car ? fabsf(car_speed(c)) : len2d(PL.vel);
    /* markers that start things */
    G.help_t -= dt;
    if (mission_marker_active()) {
        for (int k = 0; k < npoi; k++) {
            float d = dist2d(poi[k].pos, pp);
            if (d > 5) continue;
            if (poi[k].type == POI_JOB) {
                if (in_car && !c->wrecked) start_delivery(k);
                else { snprintf(G.help, sizeof(G.help), "COURIER JOBS NEED A VEHICLE"); G.help_t = 0.5f; }
            }
        }
        for (int r = 0; r < NRACE; r++) {
            if (dist2d(races[r].start, pp) > 6) continue;
            if (in_car && !c->wrecked) start_race(r);
            else { snprintf(G.help, sizeof(G.help), "STREET RACE: BRING A CAR"); G.help_t = 0.5f; }
        }
    }
    /* respray, safehouse, hospital */
    for (int k = 0; k < npoi; k++) {
        float d = dist2d(poi[k].pos, pp);
        if (poi[k].type == POI_RESPRAY && in_car && d < 6 && sp < 6 && MS.cooldown <= 0) {
            MS.cooldown = 6;
            if (G.money < 100) { message("RESPRAY COSTS $100", 3); continue; }
            G.money -= 100;
            c->hp = SPEC[c->model].hp;
            c->fire_t = 0;
            if (c->model != VM_POLICE && c->model != VM_TAXI) c->col = rand_paint();
            G.heat = 0; G.stars = 0;
            audio_play2d(SFX_CASH, 0.8f, 0.8f);
            bigtext("RESPRAYED", "NEW PAINT, CLEAN RECORD  -$100", 3);
        }
        if (poi[k].type == POI_SAFE && !in_car && d < 3 && MS.cooldown <= 0) {
            MS.cooldown = 8;
            if (G.stars > 0) { message("LOSE THE COPS BEFORE SAVING", 3); continue; }
            extern void save_game(void);
            save_game();
            PL.hp = 100;
        }
        if (poi[k].type == POI_HOSPITAL && !in_car && d < 3 && PL.hp < 99 && MS.cooldown <= 0) {
            MS.cooldown = 5;
            PL.hp = 100;
            message("PATCHED UP", 2);
            audio_play2d(SFX_PICKUP, 0.7f, 0.8f);
        }
    }
    /* taxi duty */
    if (in_car && c->model == VM_TAXI && MS.type == MT_NONE && G.help_t <= 0) { snprintf(G.help, sizeof(G.help), "PRESS T TO START TAXI FARES"); G.help_t = 0.2f; }
    switch (MS.type) {
    case MT_DELIVERY:
        MS.timer -= dt;
        if (!in_car) { snprintf(G.help, sizeof(G.help), "GET BACK IN A VEHICLE!"); G.help_t = 0.2f; }
        if (in_car && dist2d(pp, MS.target) < 7 && sp < 12) {
            int p = 150 + (int)(MS.dist * 0.55f) + (int)(MS.timer * 6);
            pay(p);
            deliveries++;
            char b[64];
            snprintf(b, sizeof(b), "+$%d", p);
            bigtext("DELIVERED!", b, 3);
            audio_play2d(SFX_MISSION_PASS, 0.9f, 1);
            MS.type = MT_NONE;
            MS.cooldown = 3;
        } else if (MS.timer <= 0) mission_end(false, "THE PACKAGE WAS LATE");
        break;
    case MT_TAXI:
        if (!in_car || c->model != VM_TAXI) { mission_end(false, "YOU LEFT YOUR TAXI"); break; }
        if (MS.stage == 0) {
            Ped *q = MS.fare_ped >= 0 ? &ped[MS.fare_ped] : NULL;
            if (!q || !q->on || q->state != PS_HAIL) { next_fare(); break; }
            MS.target = q->pos;
            if (dist2d(pp, q->pos) < 9 && sp < 3) {
                q->on = false;
                MS.fare_ped = -1;
                audio_play(SFX_DOOR, pp.x, pp.y, pp.z, 0.8f, 1.1f);
                MS.stage = 1;
                MS.target = random_destination(pp, 350, 1000);
                MS.dist = dist2d(MS.target, pp);
                MS.limit = MS.dist / 12 + 15;
                MS.timer = MS.limit;
                message("FARE ON BOARD - DRIVE TO THE MARKER", 3);
            }
        } else {
            MS.timer -= dt;
            if (dist2d(pp, MS.target) < 9 && sp < 3) {
                MS.streak++;
                int p = 60 + (int)(MS.dist * 0.4f) + (int)(MS.timer * 4) + MS.streak * 25;
                pay(p);
                fares++;
                char b[64];
                snprintf(b, sizeof(b), "+$%d  STREAK %d", p, MS.streak);
                bigtext("FARE COMPLETE", b, 2.5f);
                audio_play2d(SFX_CHECKPOINT, 0.9f, 1.2f);
                Ped *q = spawn_ped(MS.target, false);
                if (q) { q->bi = (int)clampf(floorf(q->pos.x / PITCH), 0, NB - 1); q->bj = (int)clampf(floorf(q->pos.z / PITCH), 0, NB - 1); q->state = PS_RETURN; q->target = q->pos; }
                MS.stage = 0;
                MS.fare_ped = -1;
                next_fare();
            } else if (MS.timer <= 0) { MS.streak = 0; mission_end(false, "THE FARE GOT OUT"); }
        }
        break;
    case MT_RACE: {
        Race *R = &races[MS.race];
        if (!in_car || c->wrecked || c->sunk) { mission_end(false, "YOU LOST YOUR CAR"); break; }
        if (MS.stage == 0) {
            int before = (int)ceilf(MS.timer);
            MS.timer -= dt;
            c->thr = 0; c->brk = 1;
            c->vel = V(0, 0, 0);
            int after = (int)ceilf(MS.timer);
            if (after != before && after > 0) { audio_play2d(SFX_COUNTDOWN, 0.9f, 1); char b[16]; snprintf(b, sizeof(b), "%d", after); bigtext(b, "", 0.8f); }
            if (MS.timer <= 0) { MS.stage = 1; MS.t0 = G.time; audio_play2d(SFX_GO, 1, 1); bigtext("GO!", "", 1); }
        } else {
            MS.target = R->cp[MS.cp];
            if (dist2d(pp, R->cp[MS.cp]) < 11) {
                MS.cp++;
                audio_play2d(SFX_CHECKPOINT, 0.8f, 1 + MS.cp * 0.03f);
                if (MS.cp >= R->ncp) {
                    float t = G.time - MS.t0;
                    int medal = t < R->par ? 0 : t < R->par * 1.15f ? 1 : t < R->par * 1.3f ? 2 : 3;
                    const char *mn[4] = {"GOLD", "SILVER", "BRONZE", "NO MEDAL"};
                    int prize[4] = {2000, 1000, 500, 100};
                    char b[96];
                    bool rec = race_best[MS.race] <= 0 || t < race_best[MS.race];
                    if (rec) race_best[MS.race] = t;
                    snprintf(b, sizeof(b), "%s  %d:%05.2f  +$%d%s", mn[medal], (int)t / 60, fmodf(t, 60), prize[medal], rec ? "  NEW RECORD" : "");
                    pay(prize[medal]);
                    bigtext("FINISH", b, 4);
                    audio_play2d(medal < 3 ? SFX_MISSION_PASS : SFX_MISSION_FAIL, 0.9f, 1);
                    MS.type = MT_NONE;
                    MS.cooldown = 6;
                }
            }
            if (G.time - MS.t0 > R->par * 2.2f + 30) mission_end(false, "OUT OF TIME");
        }
        break;
    }
    default: break;
    }
    /* collectibles */
    for (int k = 0; k < NTAPE; k++) {
        if (tapes_found & (1u << k)) continue;
        if (len(sub(add(tapes[k], V(0, 0.9f, 0)), add(pp, V(0, 0.9f, 0)))) < (in_car ? 3.2f : 1.8f)) {
            tapes_found |= 1u << k;
            int n = __builtin_popcount(tapes_found);
            char b[64];
            snprintf(b, sizeof(b), "LOST TAPE %d OF %d  +$250", n, NTAPE);
            pay(250);
            bigtext("FOUND A LOST TAPE", b, 3);
            audio_play2d(SFX_PICKUP, 1, 1.1f);
            if (n == NTAPE) { pay(10000); bigtext("ALL TAPES FOUND", "+$10000 - THE CITY'S LOST SOUNDS ARE YOURS", 5); }
        }
    }
}
static void toggle_taxi(void) {
    if (PL.car < 0 || car[PL.car].model != VM_TAXI) return;
    if (MS.type == MT_TAXI) { mission_end(true, ""); message("OFF DUTY", 2); return; }
    if (MS.type != MT_NONE) return;
    MS.type = MT_TAXI;
    MS.streak = 0;
    MS.fare_ped = -1;
    next_fare();
    message("ON DUTY - PICK UP THE FARE", 3);
}

/* ================================================================== player */
static void exit_car(void) {
    Car *c = &car[PL.car];
    const CarSpec *S = &SPEC[c->model];
    v3 f, r;
    car_axes(c, &f, &r);
    v3 cands[3] = {add(c->pos, mul(r, -(S->W / 2 + 0.7f))), add(c->pos, mul(r, S->W / 2 + 0.7f)), add(c->pos, V(0, S->H + 0.2f, 0))};
    v3 out = cands[2];
    for (int k = 0; k < 2; k++)
        if (!point_blocked(add(cands[k], V(0, 0.2f, 0)), 0.35f, 1.7f)) { out = cands[k]; break; }
    PL.pos = out;
    PL.pos.y = fmaxf(out.y, ground_at(out.x, out.z, out.y + 0.5f, 0.3f));
    PL.vel = mul(c->vel, 0.5f);
    PL.yaw = c->yaw;
    c->driver = DRV_NONE;
    c->thr = 0;
    PL.last_car = PL.car;
    PL.car = -1;
    audio_play(SFX_DOOR, c->pos.x, c->pos.y, c->pos.z, 0.8f, 1);
    audio_engine(false, 0, 0, 0);
    if (MS.type == MT_TAXI) mission_end(true, "");
}
static void eject_player_from(Car *c) {
    if (PL.car < 0 || &car[PL.car] != c) return;
    PL.pos = add(c->pos, V(0, 0.5f, 0));
    PL.vel = V(0, 0, 0);
    c->driver = DRV_NONE;
    PL.last_car = PL.car;
    PL.car = -1;
    PL.swim = true;
}
static void enter_nearest_car(void) {
    int best = -1;
    float bd = 4.2f;
    for (int i = 0; i < MAXCAR; i++) {
        Car *c = &car[i];
        if (!c->on || c->wrecked || c->sunk) continue;
        v3 f, r;
        car_axes(c, &f, &r);
        v3 d = sub(PL.pos, c->pos);
        float lz = fabsf(dot(d, f)) - SPEC[c->model].L / 2, lx = fabsf(dot(d, r)) - SPEC[c->model].W / 2;
        float dd = sqrtf(fmaxf(lz, 0) * fmaxf(lz, 0) + fmaxf(lx, 0) * fmaxf(lx, 0)) + fabsf(PL.pos.y - c->pos.y) * 0.5f;
        if (dd < bd) { bd = dd; best = i; }
    }
    if (best < 0) return;
    Car *c = &car[best];
    v3 r = right_of(c->yaw);
    if (c->driver == DRV_AI || c->driver == DRV_COP) {
        Ped *p = spawn_ped(add(c->pos, mul(r, -(SPEC[c->model].W / 2 + 0.8f))), c->driver == DRV_COP);
        if (p) {
            p->bi = (int)clampf(floorf(p->pos.x / PITCH), 0, NB - 1);
            p->bj = (int)clampf(floorf(p->pos.z / PITCH), 0, NB - 1);
            if (c->driver == DRV_COP) { p->state = PS_COP; p->speed = 5.6f; }
            else { ped_knock(p, mul(r, -3)); p->timer = 1.0f; }
        }
        add_heat(c->driver == DRV_COP ? 2.0f : 0.55f);
        scare_nearby(c->pos, 20, 4);
        c->nwp = 0;
    }
    c->driver = DRV_PLAYER;
    c->siren = false;
    c->cop_state = CS_PURSUE;
    if (c->slot >= 0) { slot_used[c->slot] = false; c->slot = -1; }
    PL.car = best;
    PL.last_car = best;
    PL.cam_yaw = c->yaw;
    audio_play(SFX_DOOR, c->pos.x, c->pos.y, c->pos.z, 0.8f, 0.95f);
    snprintf(G.toast, sizeof(G.toast), "%s", SPEC[c->model].name);
    G.toast_t = 2.5f;
    G.radio_t = 3;
}

static float horn_cool;
static void update_player(const Input *in, float dt) {
    horn_cool -= dt;
    if (PL.car >= 0) {
        Car *c = &car[PL.car];
        c->thr = clampf(in->fwd, 0, 1);
        c->brk = clampf(-in->fwd, 0, 1);
        float target = clampf(in->side, -1, 1);
        float rate = fabsf(target) > fabsf(c->steer) ? 4.5f : 7.0f;
        c->steer += clampf(target - c->steer, -rate * dt, rate * dt);
        c->hb = in->jump;
        PL.pos = add(c->pos, V(0, 0.5f, 0));
        PL.vel = c->vel;
        if (in->horn && horn_cool <= 0) {
            horn_cool = 0.45f;
            audio_play(c->model == VM_BUS || c->model == VM_VAN || c->model == VM_PICKUP ? SFX_HORN_TRUCK : SFX_HORN, c->pos.x, c->pos.y + 1, c->pos.z, 1, 1);
            v3 f = fwd_of(c->yaw);
            scare_nearby(add(c->pos, mul(f, 8)), 10, 2.5f);
        }
        if (c->model == VM_POLICE && in->horn) c->siren = !c->siren && false;
        if (in->taxi) toggle_taxi();
        /* stunts */
        if (c->airborne) {
            if (PL.air_t == 0) {
                PL.air_from = c->pos;
                PL.air_ramp = false;
                for (int k = 0; k < nramp; k++)
                    if (dist2d(c->pos, add(ramps[k].pos, mul(fwd_of(ramps[k].yaw), ramps[k].len))) < 9) { PL.air_ramp = true; PL.air_ramp_id = k; }
            }
            PL.air_t += dt;
        } else if (PL.air_t > 0) {
            float t = PL.air_t, dist = dist2d(c->pos, PL.air_from);
            G.stat_air = fmaxf(G.stat_air, t);
            if (t > 0.8f && !c->wrecked && !c->sunk) {
                int bonus = (int)(t * 40 + dist * 6);
                char b[96];
                if (PL.air_ramp && !(jumps_done & (1u << PL.air_ramp_id))) {
                    jumps_done |= 1u << PL.air_ramp_id;
                    bonus += 500;
                    snprintf(b, sizeof(b), "%.1fS  %.0fM  +$%d   STUNT %d OF %d", t, dist, bonus, __builtin_popcount(jumps_done), nramp);
                    bigtext("STUNT JUMP!", b, 3.5f);
                } else {
                    snprintf(b, sizeof(b), "%.1fS  %.0fM  +$%d", t, dist, bonus);
                    bigtext("AIR TIME", b, 2.5f);
                }
                pay(bonus);
                audio_play2d(SFX_JUMP_BONUS, 0.9f, 1);
            }
            PL.air_t = 0;
        }
        G.stat_dist += len2d(c->vel) * dt;
        if (in->enter && fabsf(car_speed(c)) < 7) exit_car();
        return;
    }
    /* on foot */
    PL.air_t = 0;
    bool water = water_at(PL.pos.x, PL.pos.z) && PL.pos.y < WATER_Y + 0.4f;
    PL.swim = water;
    float g = ground_at(PL.pos.x, PL.pos.z, PL.pos.y, 0.3f);
    if (PL.down_t > 0) {
        PL.down_t -= dt;
        PL.vel.y -= 20 * dt;
        PL.pos = add(PL.pos, mul(PL.vel, dt));
        if (PL.pos.y <= g) { PL.pos.y = g; PL.vel = mul(PL.vel, 1 - fminf(1, dt * 5)); PL.vel.y = 0; }
        collide_circle(&PL.pos, 0.35f, 1.7f, NULL);
        return;
    }
    v3 cf = fwd_of(PL.cam_yaw), cr = right_of(PL.cam_yaw);
    v3 wish = add(mul(cf, in->fwd), mul(cr, in->side));
    float wl = len2d(wish);
    if (wl > 1) wish = mul(wish, 1 / wl);
    PL.sprint = in->sprint;
    float spd = water ? 2.6f : in->sprint ? 7.2f : 4.4f;
    v3 target = mul(wish, spd);
    float acc = PL.ground || water ? 14 : 3;
    PL.vel.x += (target.x - PL.vel.x) * fminf(1, acc * dt);
    PL.vel.z += (target.z - PL.vel.z) * fminf(1, acc * dt);
    if (wl > 0.1f) PL.yaw += wrap_pi(yaw_of(wish) - PL.yaw) * fminf(1, dt * 10);
    if (water) {
        PL.vel.y = 0;
        PL.pos.x += PL.vel.x * dt;
        PL.pos.z += PL.vel.z * dt;
        PL.pos.y += (WATER_Y - 1.15f - PL.pos.y) * fminf(1, dt * 4);
        float g2 = ground_at(PL.pos.x, PL.pos.z, PL.pos.y + 1.2f, 0.3f);
        if (g2 > WATER_Y - 0.6f) PL.pos.y = fmaxf(PL.pos.y, g2);
        PL.ground = false;
    } else {
        if (in->jump && PL.ground) { PL.vel.y = 5.6f; PL.ground = false; }
        PL.vel.y -= 20 * dt;
        PL.pos = add(PL.pos, mul(PL.vel, dt));
        g = ground_at(PL.pos.x, PL.pos.z, PL.pos.y, 0.3f);
        if (PL.pos.y <= g + 0.02f && PL.vel.y <= 0) {
            if (PL.vel.y < -14) player_hurt((-PL.vel.y - 14) * 6, PL.pos);
            PL.pos.y = g;
            PL.vel.y = 0;
            PL.ground = true;
        } else if (PL.pos.y > g + 0.3f) PL.ground = false;
    }
    collide_circle(&PL.pos, 0.35f, 1.7f, NULL);
    /* lamp posts block the player */
    int ids[4], n;
    props_near(PL.pos, ids, &n);
    for (int k = 0; k < n; k++) {
        Prop *q = &prop[ids[k]];
        if (q->down) continue;
        v3 d = sub(PL.pos, q->pos);
        float l = len2d(d);
        if (l < 0.55f && l > 1e-4f) { PL.pos.x = q->pos.x + d.x / l * 0.55f; PL.pos.z = q->pos.z + d.z / l * 0.55f; }
    }
    PL.anim += len2d(PL.vel) * dt * 2.2f;
    if (in->enter) enter_nearest_car();
    /* health regenerates slowly */
    PL.regen_t -= dt;
    if (PL.regen_t <= 0 && PL.hp < 100) PL.hp = fminf(100, PL.hp + dt * 1.5f);
}

/* ================================================================== busted / wasted / respawn */
static void clear_police(void) {
    for (int i = 0; i < MAXCAR; i++)
        if (car[i].on && car[i].driver == DRV_COP) car[i].on = false;
    for (int i = 0; i < MAXPED; i++)
        if (ped[i].on && ped[i].cop) ped[i].on = false;
    G.heat = 0; G.stars = 0; G.bust_t = 0; G.heli_t = 0;
}
static void start_busted(void) {
    if (G.state != ST_PLAY) return;
    G.state = ST_BUSTED;
    G.st = 0;
    G.stat_busted++;
    audio_play2d(SFX_BUSTED, 1, 1);
    if (MS.type != MT_NONE) mission_end(false, "");
    G.big_t = 0;
}
static void start_wasted(void) {
    if (G.state != ST_PLAY) return;
    G.state = ST_WASTED;
    G.st = 0;
    G.stat_wasted++;
    audio_play2d(SFX_WASTED, 1, 1);
    if (MS.type != MT_NONE) mission_end(false, "");
    G.big_t = 0;
}
static void respawn_at(int type) {
    int k = nearest_poi(type, player_target_pos());
    v3 at = k >= 0 ? poi[k].pos : V(CITY / 2, 0, CITY / 2);
    if (PL.car >= 0) { car[PL.car].driver = DRV_NONE; PL.car = -1; }
    clear_police();
    PL.pos = add(at, V(0, 0, -1.5f));
    PL.pos.y = 0;
    PL.vel = V(0, 0, 0);
    PL.hp = 100;
    PL.down_t = 0;
    PL.cam_yaw = 3.14159f;
    int fee = G.money / 10;
    if (fee > 2500) fee = 2500;
    G.money -= fee;
    char b[64];
    snprintf(b, sizeof(b), type == POI_HOSPITAL ? "HOSPITAL BILL  -$%d" : "BAIL AND FINES  -$%d", fee);
    message(b, 4);
    G.state = ST_PLAY;
    G.st = 0;
    G.tod = fmodf(G.tod + 2, 24);
}

/* ================================================================== save / load */
static char save_path[1024];
void save_game(void) {
    if (selftest || !save_path[0]) { message("GAME SAVED", 3); return; }
    FILE *f = fopen(save_path, "w");
    if (!f) { message("COULD NOT SAVE", 3); return; }
    fprintf(f, "portcalder 1\nmoney %d\ntapes %u\njumps %u\ndeliveries %d\nfares %d\ntod %.3f\ntime %.1f\n", G.money, tapes_found, jumps_done, deliveries, fares, G.tod, play_time);
    for (int r = 0; r < NRACE; r++) fprintf(f, "race %d %.3f\n", r, race_best[r]);
    fprintf(f, "sens %.5f\nfov %.1f\ninvert %d\nradio %d\npeds %d\ncars %d\nair %.2f\ndist %.0f\n", G.sens, G.fov_base, G.invert, G.radio, G.stat_peds, G.stat_cars, G.stat_air, G.stat_dist);
    fclose(f);
    bigtext("GAME SAVED", "HEALTH RESTORED", 3);
    audio_play2d(SFX_CHECKPOINT, 0.8f, 0.9f);
}
static void load_game(void) {
    if (!save_path[0]) return;
    FILE *f = fopen(save_path, "r");
    if (!f) return;
    char key[32];
    while (fscanf(f, "%31s", key) == 1) {
        int iv; unsigned uv; float fv;
        if (!strcmp(key, "money") && fscanf(f, "%d", &iv) == 1) G.money = iv;
        else if (!strcmp(key, "tapes") && fscanf(f, "%u", &uv) == 1) tapes_found = uv;
        else if (!strcmp(key, "jumps") && fscanf(f, "%u", &uv) == 1) jumps_done = uv;
        else if (!strcmp(key, "deliveries") && fscanf(f, "%d", &iv) == 1) deliveries = iv;
        else if (!strcmp(key, "fares") && fscanf(f, "%d", &iv) == 1) fares = iv;
        else if (!strcmp(key, "tod") && fscanf(f, "%f", &fv) == 1) G.tod = fmodf(fabsf(fv), 24);
        else if (!strcmp(key, "time") && fscanf(f, "%f", &fv) == 1) play_time = fv;
        else if (!strcmp(key, "race")) { int r; if (fscanf(f, "%d %f", &r, &fv) == 2 && r >= 0 && r < NRACE) race_best[r] = fv; }
        else if (!strcmp(key, "sens") && fscanf(f, "%f", &fv) == 1) G.sens = clampf(fv, 0.0004f, 0.01f);
        else if (!strcmp(key, "fov") && fscanf(f, "%f", &fv) == 1) G.fov_base = clampf(fv, 55, 95);
        else if (!strcmp(key, "invert") && fscanf(f, "%d", &iv) == 1) G.invert = iv != 0;
        else if (!strcmp(key, "radio") && fscanf(f, "%d", &iv) == 1) G.radio = iv;
        else if (!strcmp(key, "peds") && fscanf(f, "%d", &iv) == 1) G.stat_peds = iv;
        else if (!strcmp(key, "cars") && fscanf(f, "%d", &iv) == 1) G.stat_cars = iv;
        else if (!strcmp(key, "air") && fscanf(f, "%f", &fv) == 1) G.stat_air = fv;
        else if (!strcmp(key, "dist") && fscanf(f, "%f", &fv) == 1) G.stat_dist = fv;
    }
    fclose(f);
}
static int completion_pct(void) {
    int got = __builtin_popcount(tapes_found) * 2 + __builtin_popcount(jumps_done) * 3 + (deliveries > 10 ? 10 : deliveries) * 2 + (fares > 10 ? 10 : fares) * 2;
    int max = NTAPE * 2 + nramp * 3 + 20 + 20;
    for (int r = 0; r < NRACE; r++) { max += 5; if (race_best[r] > 0 && race_best[r] < races[r].par) got += 5; }
    return got * 100 / max;
}

/* ================================================================== HUD */
static float UI; /* scale */
static struct { v3 c; float rot, scale, ox, oy, size; } MAPV;
static void map_xf(v3 p, float *sx, float *sy) {
    v3 d = sub(p, MAPV.c);
    float cs = cosf(MAPV.rot), sn = sinf(MAPV.rot);
    float lx = d.x * cs + d.z * sn, lz = -d.x * sn + d.z * cs;
    *sx = MAPV.ox + lx * MAPV.scale;
    *sy = MAPV.oy + lz * MAPV.scale;
}
static void map_rect(float x0, float z0, float x1, float z1, v3 col, float a) {
    float ax, ay, bx, by;
    map_xf(V((x0 + x1) / 2, 0, z0), &ax, &ay);
    map_xf(V((x0 + x1) / 2, 0, z1), &bx, &by);
    hline(ax, ay, bx, by, (x1 - x0) * MAPV.scale, col.x, col.y, col.z, a);
}
static void map_blip(v3 p, float r, v3 col, bool clamp_edge, char letter) {
    float x, y;
    map_xf(p, &x, &y);
    if (clamp_edge) {
        float h = MAPV.size / 2 - r - 2;
        float dx = x - MAPV.ox, dy = y - MAPV.oy;
        float m = fmaxf(fabsf(dx), fabsf(dy));
        if (m > h) { dx *= h / m; dy *= h / m; x = MAPV.ox + dx; y = MAPV.oy + dy; }
    }
    hrect(x - r - 1.5f, y - r - 1.5f, 2 * r + 3, 2 * r + 3, 0, 0, 0, 0.8f);
    hrect(x - r, y - r, 2 * r, 2 * r, col.x, col.y, col.z, 1);
    if (letter) { char s[2] = {letter, 0}; htext(s, x - 2.5f * UI * 0.55f + 0.5f, y - 3.5f * UI * 0.55f + 0.5f, UI * 0.55f, 0, 0, 0, 1, 0); }
}
static v3 poi_color(int t) {
    switch (t) {
    case POI_SAFE: return V(0.3f, 1, 0.45f);
    case POI_POLICE: return V(0.3f, 0.5f, 1);
    case POI_HOSPITAL: return V(1, 0.3f, 0.3f);
    case POI_RESPRAY: return V(1, 0.55f, 0.1f);
    case POI_JOB: return V(1, 0.85f, 0.2f);
    default: return V(1, 1, 1);
    }
}
static const char POI_LETTER[POI_N] = {'S', 'P', 'H', 'R', 'J', 'X'};
static Route gps_route;
static int gps_ti = -1, gps_tj = -1;
static bool mission_target(v3 *t) {
    if (MS.type == MT_NONE) return false;
    if (MS.type == MT_RACE && MS.stage == 0) return false;
    *t = MS.target;
    return true;
}
static void draw_map_contents(bool full, float t) {
    v3 water = V(0.05f, 0.13f, 0.2f);
    hrect(MAPV.ox - MAPV.size / 2, MAPV.oy - MAPV.size / 2, MAPV.size, MAPV.size, water.x, water.y, water.z, 0.92f);
    /* land masses */
    map_rect(-60, -60, CITY + 60, CITY + 60, V(0.16f, 0.24f, 0.13f), 1);
    map_rect(CITY, -40, CITY + 110, CITY + 40, V(0.55f, 0.50f, 0.36f), 1);
    map_rect(-20, CITY, DOCK_X, CITY + 52, V(0.30f, 0.29f, 0.27f), 1);
    for (int k = 0; k < 3; k++) { float x = k == 0 ? 230 : k == 1 ? 560 : 900; map_rect(x - 16, CITY + 52, x + 16, CITY + 190, V(0.30f, 0.29f, 0.27f), 1); }
    map_rect(CITY + 96, 689, CITY + 330, 711, V(0.45f, 0.32f, 0.2f), 1);
    map_rect(-9, -9, CITY + 9, CITY + 9, V(0.12f, 0.12f, 0.13f), 1);
    float view = MAPV.size / MAPV.scale * 0.75f;
    for (int i = 0; i < NB; i++)
        for (int j = 0; j < NB; j++) {
            v3 c = V(i * PITCH + PITCH / 2, 0, j * PITCH + PITCH / 2);
            if (!full && dist2d(c, MAPV.c) > view + 50) continue;
            v3 col = DCOL[dist_blk[i][j]];
            float e = 9;
            /* merge park blocks across the removed roads */
            float x0 = i * PITCH + (edge_ok(i, j, 1) || i == 0 ? e : -e), x1 = (i + 1) * PITCH - (edge_ok(i + 1, j, 1) ? e : -e);
            float z0 = j * PITCH + (edge_ok(i, j, 0) || j == 0 ? e : -e), z1 = (j + 1) * PITCH - (edge_ok(i, j + 1, 0) ? e : -e);
            map_rect(x0, z0, x1, z1, col, 1);
        }
    /* gps route */
    v3 tgt;
    if (mission_target(&tgt)) {
        int ti, tj;
        nearest_road_node(tgt, &ti, &tj);
        if (ti != gps_ti || tj != gps_tj) { route_build(&gps_route, ti, tj); gps_ti = ti; gps_tj = tj; }
        int i, j;
        nearest_road_node(player_target_pos(), &i, &j);
        float px, py, qx, qy;
        map_xf(player_target_pos(), &px, &py);
        map_xf(node_pos(i, j), &qx, &qy);
        hline(px, py, qx, qy, 3.5f * UI * 0.5f, 1, 0.8f, 0.1f, 0.9f);
        int prev = -1;
        for (int s = 0; s < 80 && gps_route.d[i][j] > 0 && gps_route.d[i][j] < 9999; s++) {
            int d = route_next_dir(&gps_route, i, j, prev >= 0 ? (prev + 2) % 4 : -1);
            if (d < 0) break;
            map_xf(node_pos(i, j), &px, &py);
            i += DX[d]; j += DZ[d];
            map_xf(node_pos(i, j), &qx, &qy);
            hline(px, py, qx, qy, 3.5f * UI * 0.5f, 1, 0.8f, 0.1f, 0.9f);
            prev = d;
        }
        map_xf(node_pos(i, j), &px, &py);
        map_xf(tgt, &qx, &qy);
        hline(px, py, qx, qy, 3.5f * UI * 0.5f, 1, 0.8f, 0.1f, 0.9f);
    }
    float br = 4.2f * UI * 0.55f;
    for (int k = 0; k < npoi; k++) {
        if (poi[k].type == POI_JOB && !mission_marker_active()) continue;
        if (!full && dist2d(poi[k].pos, MAPV.c) > view) continue;
        map_blip(poi[k].pos, br, poi_color(poi[k].type), false, POI_LETTER[poi[k].type]);
    }
    if (mission_marker_active())
        for (int r = 0; r < NRACE; r++)
            if (full || dist2d(races[r].start, MAPV.c) < view) map_blip(races[r].start, br, V(0.9f, 0.9f, 0.95f), false, 'X');
    if (full)
        for (int k = 0; k < nramp; k++) if (!(jumps_done & (1u << k))) map_blip(ramps[k].pos, br * 0.6f, V(0.8f, 0.4f, 1), false, 0);
    if (MS.type == MT_RACE && MS.stage == 1) {
        Race *R = &races[MS.race];
        if (MS.cp + 1 < R->ncp) map_blip(R->cp[MS.cp + 1], br * 0.7f, V(1, 0.85f, 0.2f), !full, 0);
    }
    for (int i = 0; i < MAXCAR; i++)
        if (car[i].on && car[i].driver == DRV_COP && !car[i].wrecked) {
            bool fl = fmodf(t * 4, 1) < 0.5f;
            map_blip(car[i].pos, br * 0.6f, fl ? V(1, 0.2f, 0.2f) : V(0.2f, 0.4f, 1), false, 0);
        }
    if (mission_target(&tgt)) map_blip(tgt, br * 1.1f, V(1, 0.85f, 0.15f), !full, 0);
    /* player arrow */
    float px, py;
    map_xf(player_target_pos(), &px, &py);
    float yaw = PL.car >= 0 ? car[PL.car].yaw : PL.yaw;
    float a = yaw - MAPV.rot;
    float s = 7 * UI * 0.6f;
    float fx = sinf(a), fy = -cosf(a);
    htri(px + fx * s * 1.4f, py + fy * s * 1.4f, px - fy * s * 0.8f - fx * s, py + fx * s * 0.8f - fy * s, px + fy * s * 0.8f - fx * s, py - fx * s * 0.8f - fy * s, 0, 0, 0, 0.9f);
    s *= 0.75f;
    htri(px + fx * s * 1.4f, py + fy * s * 1.4f, px - fy * s * 0.8f - fx * s, py + fx * s * 0.8f - fy * s, px + fy * s * 0.8f - fx * s, py - fx * s * 0.8f - fy * s, 1, 1, 1, 1);
}

static void money_str(char *b, size_t n, int m) {
    char t[32];
    snprintf(t, sizeof(t), "%d", m < 0 ? -m : m);
    int l = (int)strlen(t), o = 0;
    char out[48];
    out[o++] = '$';
    for (int i = 0; i < l; i++) { out[o++] = t[i]; if ((l - i - 1) % 3 == 0 && i < l - 1) out[o++] = ','; }
    out[o] = 0;
    snprintf(b, n, "%s", out);
}
static void draw_bar(float x, float y, float w, float h, float v, v3 c) {
    hrect(x - 2, y - 2, w + 4, h + 4, 0, 0, 0, 0.55f);
    hrect(x, y, w, h, c.x * 0.25f, c.y * 0.25f, c.z * 0.25f, 0.7f);
    hrect(x, y, w * clampf(v, 0, 1), h, c.x, c.y, c.z, 0.95f);
}
static void draw_minimap(float t) {
    float size = 200 * UI * 0.55f;
    float m = 18 * UI * 0.55f;
    MAPV.size = size;
    MAPV.ox = m + size / 2;
    MAPV.oy = rh - m - size / 2;
    MAPV.c = player_target_pos();
    float sp = PL.car >= 0 ? fabsf(car_speed(&car[PL.car])) : 0;
    MAPV.scale = size / (300 + sp * 6);
    MAPV.rot = cam.fwd.x == 0 && cam.fwd.z == 0 ? 0 : -yaw_of(cam.fwd) + 0;
    /* rotation so that camera forward points up: rotate world by -yaw */
    MAPV.rot = yaw_of(cam.fwd);
    hud_flush();
    glEnable(GL_SCISSOR_TEST);
    glScissor((int)(MAPV.ox - size / 2), (int)(rh - (MAPV.oy + size / 2)), (int)size, (int)size);
    draw_map_contents(false, t);
    hud_flush();
    glDisable(GL_SCISSOR_TEST);
    float x0 = MAPV.ox - size / 2, y0 = MAPV.oy - size / 2;
    hrect(x0 - 3, y0 - 3, size + 6, 3, 0, 0, 0, 0.8f);
    hrect(x0 - 3, y0 + size, size + 6, 3, 0, 0, 0, 0.8f);
    hrect(x0 - 3, y0, 3, size, 0, 0, 0, 0.8f);
    hrect(x0 + size, y0, 3, size, 0, 0, 0, 0.8f);
    /* north marker */
    float nx, ny;
    map_xf(add(MAPV.c, V(0, 0, -1000)), &nx, &ny);
    float dx = nx - MAPV.ox, dy = ny - MAPV.oy, mm = fmaxf(fabsf(dx), fabsf(dy));
    nx = MAPV.ox + dx / mm * (size / 2 - 8);
    ny = MAPV.oy + dy / mm * (size / 2 - 8);
    htext("N", nx - 3 * UI * 0.5f, ny - 4 * UI * 0.5f, UI * 0.6f, 1, 1, 1, 0.9f, 0);
}
static void draw_full_map(float t) {
    hrect(0, 0, (float)rw, (float)rh, 0, 0, 0, 0.75f);
    float size = fminf(rw, rh) * 0.86f;
    MAPV.size = size;
    MAPV.ox = rw / 2.0f;
    MAPV.oy = rh / 2.0f + 10 * UI * 0.5f;
    MAPV.c = V(CITY / 2 + 60, 0, CITY / 2 + 40);
    MAPV.scale = size / (CITY + 480);
    MAPV.rot = 0;
    draw_map_contents(true, t);
    htext("PORT CALDER", rw / 2.0f, MAPV.oy - size / 2 - 22 * UI * 0.5f, UI * 0.9f, 1, 1, 1, 1, 1);
    /* district labels */
    struct { int d; float x, z; } lab[] = {{D_CENTER, 12.5f, 11.5f}, {D_MIDTOWN, 15.5f, 3}, {D_MAPLE, 3.5f, 10}, {D_PARK, 5, 6}, {D_DOCKS, 7.5f, 21.8f}, {D_SEABRIGHT, 22, 7}, {D_EASTGATE, 18, 18}, {D_OLDTOWN, 4.5f, 16}};
    for (size_t k = 0; k < sizeof(lab) / sizeof(lab[0]); k++) {
        float x, y;
        map_xf(V(lab[k].x * PITCH, 0, lab[k].z * PITCH), &x, &y);
        htext(DNAME[lab[k].d], x, y, UI * 0.45f, 1, 1, 1, 0.85f, 1);
    }
    float lx = MAPV.ox + size / 2 + 12, ly = MAPV.oy - size / 2;
    if (lx + 150 > rw) lx = 10;
    const char *names[] = {"SAFEHOUSE (SAVE)", "POLICE", "HOSPITAL", "RESPRAY $100", "COURIER JOB", "STREET RACE"};
    for (int k = 0; k < POI_N; k++) {
        float yy = ly + k * 16 * UI * 0.55f;
        hrect(lx, yy, 9 * UI * 0.55f, 9 * UI * 0.55f, poi_color(k).x, poi_color(k).y, poi_color(k).z, 1);
        htext(names[k], lx + 14 * UI * 0.55f, yy + 1, UI * 0.45f, 1, 1, 1, 0.9f, 0);
    }
    htext("TAB - CLOSE", rw / 2.0f, rh - 18 * UI * 0.5f, UI * 0.5f, 0.8f, 0.8f, 0.8f, 0.9f, 1);
}

static void draw_hud(float t) {
    UI = rh / 720.0f * 2.0f;
    char b[128];
    if (G.state == ST_TITLE) {
        float a = clampf(t * 0.5f, 0, 1);
        hrect(0, rh * 0.30f, (float)rw, 150 * UI * 0.5f, 0, 0, 0, 0.35f * a);
        htext("PORT CALDER", rw / 2.0f + 3, rh * 0.33f + 3, UI * 4.2f, 0.9f, 0.2f, 0.5f, a, 1);
        htext("PORT CALDER", rw / 2.0f, rh * 0.33f, UI * 4.2f, 1, 0.85f, 0.5f, a, 1);
        htext("AN OPEN-CITY DRIVING SANDBOX", rw / 2.0f, rh * 0.33f + 40 * UI, UI * 0.8f, 0.85f, 0.9f, 1, a, 1);
        if (fmodf(t, 1.2f) < 0.85f) htext("PRESS ENTER", rw / 2.0f, rh * 0.62f, UI * 1.1f, 1, 1, 1, a, 1);
        htext("WASD DRIVE/WALK  MOUSE CAMERA  F ENTER/EXIT  SPACE HANDBRAKE/JUMP  SHIFT SPRINT", rw / 2.0f, rh - 46 * UI * 0.5f, UI * 0.5f, 0.8f, 0.8f, 0.8f, a, 1);
        htext("H HORN  Q/E RADIO  T TAXI DUTY  TAB MAP  C CAMERA  ESC PAUSE  F11 FULLSCREEN  M MUTE", rw / 2.0f, rh - 30 * UI * 0.5f, UI * 0.5f, 0.8f, 0.8f, 0.8f, a, 1);
        hud_flush();
        return;
    }
    if (G.state == ST_MAP) { draw_full_map(t); hud_flush(); return; }
    float s = UI * 0.55f;
    /* top right: clock, money, stars, health */
    float rx = rw - 18 * s;
    int hh = (int)G.tod, mm = (int)((G.tod - hh) * 60);
    snprintf(b, sizeof(b), "%02d:%02d", hh, mm);
    htext(b, rx, 16 * s, s * 2.0f, 1, 1, 1, 0.95f, 2);
    money_str(b, sizeof(b), G.money);
    htext(b, rx, 40 * s, s * 2.6f, 0.45f, 1, 0.45f, 1, 2);
    for (int k = 0; k < 5; k++) {
        float cx = rx - 10 * s - k * 24 * s, cy = 92 * s;
        bool on = 4 - k < G.stars;
        bool flash = G.stars > 0 && G.evade_t > 1 && fmodf(t * 3, 1) < 0.5f;
        hstar(cx, cy, 11 * s, 0, 0, 0, 0.6f);
        if (on) hstar(cx, cy, 9 * s, flash ? 0.6f : 1, flash ? 0.6f : 0.85f, flash ? 0.6f : 0.2f, 1);
        else hstar(cx, cy, 8 * s, 0.25f, 0.25f, 0.25f, 0.6f);
    }
    draw_bar(rx - 130 * s, 110 * s, 130 * s, 7 * s, PL.hp / 100, V(0.35f, 0.9f, 0.4f));
    if (PL.car >= 0) {
        Car *c = &car[PL.car];
        draw_bar(rx - 130 * s, 124 * s, 130 * s, 5 * s, c->hp / SPEC[c->model].hp, V(1, 0.6f, 0.15f));
    }
    /* bottom right: speed and vehicle */
    if (PL.car >= 0) {
        Car *c = &car[PL.car];
        int kmh = (int)(fabsf(car_speed(c)) * 3.6f);
        snprintf(b, sizeof(b), "%d", kmh);
        htext(b, rx - 46 * s, rh - 52 * s, s * 4, 1, 1, 1, 0.95f, 2);
        htext("KM/H", rx, rh - 34 * s, s * 1.2f, 0.8f, 0.8f, 0.8f, 0.9f, 2);
        if (c->siren) htext("SIREN", rx, rh - 76 * s, s, 0.4f, 0.6f, 1, 0.9f, 2);
    }
    if (G.radio_t > 0 && PL.car >= 0) {
        const char *rn = G.radio >= 0 ? audio_radio_name(G.radio) : "RADIO OFF";
        htext(rn, rw / 2.0f, 18 * s, s * 1.4f, 1, 0.7f, 0.35f, clampf(G.radio_t, 0, 1), 1);
    }
    if (G.toast_t > 0) htext(G.toast, rx, rh - 100 * s, s * 1.6f, 1, 1, 0.8f, clampf(G.toast_t, 0, 1), 2);
    /* help box */
    if (G.help_t > 0) {
        float w = text_w(G.help, s * 1.1f) + 20 * s;
        hrect(16 * s, 16 * s, w, 26 * s, 0, 0, 0, 0.6f);
        htext(G.help, 26 * s, 25 * s, s * 1.1f, 1, 1, 1, 0.95f, 0);
    } else if (PL.car < 0 && G.state == ST_PLAY) {
        for (int i = 0; i < MAXCAR; i++) {
            Car *c = &car[i];
            if (!c->on || c->wrecked || c->sunk || dist2d(c->pos, PL.pos) > SPEC[c->model].L * 0.5f + 3) continue;
            snprintf(b, sizeof(b), c->driver == DRV_AI ? "F - CARJACK %s" : c->driver == DRV_COP ? "F - STEAL POLICE CAR" : "F - ENTER %s", SPEC[c->model].name);
            float w = text_w(b, s * 1.1f) + 20 * s;
            hrect(16 * s, 16 * s, w, 26 * s, 0, 0, 0, 0.6f);
            htext(b, 26 * s, 25 * s, s * 1.1f, 1, 1, 1, 0.95f, 0);
            break;
        }
    }
    /* mission status */
    if (MS.type != MT_NONE) {
        const char *obj = "";
        if (MS.type == MT_DELIVERY) obj = "DELIVER THE PACKAGE";
        else if (MS.type == MT_TAXI) obj = MS.stage == 0 ? "PICK UP THE FARE" : "TAKE THE FARE TO THE MARKER";
        else if (MS.type == MT_RACE) obj = races[MS.race].name;
        htext(obj, rw / 2.0f, 50 * s, s * 1.5f, 1, 0.85f, 0.3f, 1, 1);
        if (MS.type == MT_RACE && MS.stage == 1) {
            float et = G.time - MS.t0;
            snprintf(b, sizeof(b), "CHECKPOINT %d/%d   %d:%05.2f   PAR %d:%02d", MS.cp + 1, races[MS.race].ncp, (int)et / 60, fmodf(et, 60), (int)races[MS.race].par / 60, (int)races[MS.race].par % 60);
            htext(b, rw / 2.0f, 70 * s, s * 1.2f, 1, 1, 1, 0.95f, 1);
        } else if ((MS.type == MT_DELIVERY) || (MS.type == MT_TAXI && MS.stage == 1)) {
            snprintf(b, sizeof(b), "%d:%02d   %.0fM", (int)fmaxf(MS.timer, 0) / 60, (int)fmaxf(MS.timer, 0) % 60, dist2d(MS.target, player_target_pos()));
            float warn = MS.timer < 10 && fmodf(t * 2, 1) < 0.5f ? 0.3f : 1;
            htext(b, rw / 2.0f, 70 * s, s * 1.4f, 1, warn, warn, 1, 1);
        }
        if (MS.type == MT_TAXI) { snprintf(b, sizeof(b), "STREAK %d", MS.streak); htext(b, rw / 2.0f, 88 * s, s, 0.8f, 0.9f, 1, 0.9f, 1); }
    }
    /* messages */
    if (G.msg_t > 0) htext(G.msg, rw / 2.0f, rh * 0.74f, s * 1.5f, 1, 1, 1, clampf(G.msg_t, 0, 1), 1);
    if (G.big_t > 0) {
        float a = clampf(G.big_t * 2, 0, 1);
        htext(G.big, rw / 2.0f, rh * 0.25f, s * 4.2f, 1, 0.82f, 0.3f, a, 1);
        if (G.big2[0]) htext(G.big2, rw / 2.0f, rh * 0.25f + 40 * s, s * 1.5f, 1, 1, 1, a, 1);
    }
    /* district name */
    static int last_d = -1;
    static float dt_t;
    int d = district_at(player_target_pos());
    if (d != last_d) { last_d = d; dt_t = 3; }
    dt_t -= 1 / 60.0f;
    if (dt_t > 0 && G.state == ST_PLAY) htext(DNAME[d], rx, rh - 130 * s, s * 2.0f, 1, 1, 1, clampf(dt_t, 0, 1), 2);
    if (G.bust_t > 0.2f) htext("STOP RESISTING!", rw / 2.0f, rh * 0.6f, s * 1.6f, 0.5f, 0.7f, 1, 0.9f, 1);
    draw_minimap(t);
    if (G.state == ST_BUSTED || G.state == ST_WASTED) {
        float a = clampf(G.st - 0.5f, 0, 1);
        const char *w = G.state == ST_BUSTED ? "BUSTED" : "WASTED";
        htext(w, rw / 2.0f, rh * 0.42f, s * 6, G.state == ST_BUSTED ? 0.4f : 0.85f, G.state == ST_BUSTED ? 0.6f : 0.15f, G.state == ST_BUSTED ? 1 : 0.1f, a, 1);
    }
    if (G.state == ST_PAUSE) {
        hrect(0, 0, (float)rw, (float)rh, 0, 0, 0, 0.6f);
        float y = rh * 0.16f;
        htext("PAUSED", rw / 2.0f, y, s * 4, 1, 0.85f, 0.4f, 1, 1);
        y += 70 * s;
        int pt = (int)play_time;
        const char *lines[16];
        char L[16][96];
        int n = 0;
        snprintf(L[n], 96, "CITY COMPLETION  %d%%", completion_pct()); lines[n] = L[n]; n++;
        snprintf(L[n], 96, "LOST TAPES  %d/%d     STUNT JUMPS  %d/%d", __builtin_popcount(tapes_found), NTAPE, __builtin_popcount(jumps_done), nramp); lines[n] = L[n]; n++;
        snprintf(L[n], 96, "DELIVERIES  %d     TAXI FARES  %d", deliveries, fares); lines[n] = L[n]; n++;
        for (int r = 0; r < NRACE; r++) {
            if (race_best[r] > 0) snprintf(L[n], 96, "%s  BEST %d:%05.2f  PAR %d:%02d", races[r].name, (int)race_best[r] / 60, fmodf(race_best[r], 60), (int)races[r].par / 60, (int)races[r].par % 60);
            else snprintf(L[n], 96, "%s  NOT RACED", races[r].name);
            lines[n] = L[n]; n++;
        }
        snprintf(L[n], 96, "PLAY TIME %d:%02d:%02d   DRIVEN %.1f KM   LONGEST AIR %.1fS", pt / 3600, pt / 60 % 60, pt % 60, G.stat_dist / 1000, G.stat_air); lines[n] = L[n]; n++;
        snprintf(L[n], 96, "BUSTED %d   WASTED %d", G.stat_busted, G.stat_wasted); lines[n] = L[n]; n++;
        for (int k = 0; k < n; k++) { htext(lines[k], rw / 2.0f, y, s * 1.2f, 1, 1, 1, 0.92f, 1); y += 20 * s; }
        y += 18 * s;
        snprintf(b, sizeof(b), "MOUSE SENSITIVITY  %.1f   (- / +)", G.sens * 1000);
        htext(b, rw / 2.0f, y, s * 1.2f, 0.75f, 0.9f, 1, 1, 1); y += 20 * s;
        snprintf(b, sizeof(b), "FIELD OF VIEW  %.0f   (, / .)", G.fov_base);
        htext(b, rw / 2.0f, y, s * 1.2f, 0.75f, 0.9f, 1, 1, 1); y += 20 * s;
        snprintf(b, sizeof(b), "INVERT Y  %s   (I)", G.invert ? "ON" : "OFF");
        htext(b, rw / 2.0f, y, s * 1.2f, 0.75f, 0.9f, 1, 1, 1); y += 34 * s;
        htext("ESC RESUME     Q QUIT TO TITLE", rw / 2.0f, y, s * 1.3f, 1, 1, 1, 1, 1);
    }
    hud_flush();
}
