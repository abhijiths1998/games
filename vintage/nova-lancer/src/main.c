/*
 * NOVA LANCER - an original CGA-style vertical shoot-'em-up (C11 + SDL2).
 * Only the four CGA palette-1 high-intensity colours are used: black, light cyan,
 * light magenta and white.  Built on the shared ../common/retro.h toolkit.
 */
#include "retro.h"

#define PW 192 /* playfield size and screen offset */
#define PH 200
#define PX 64
#define NSTAGES 4

#define BK C_BLACK
#define CY C_LCYAN
#define MG C_LMAGENTA
#define WH C_WHITE

#define PI_F 3.14159265f

/* ================================================================== types */
enum { S_TITLE, S_CARD, S_PLAY, S_CLEAR, S_OVER, S_ENTRY, S_WIN };
enum { K_SCOUT, K_WEAVER, K_DIVER, K_GUNSHIP, K_TURRET, K_HEAVY, K_ROCK, K_N };
enum { PT_DOWN, PT_SINE, PT_SWOOP, PT_DIVE, PT_HOVER, PT_LOOP, PT_TURRET, PT_ROCK };
enum { W_SPREAD, W_LANCE, W_SEEKER };
enum { D_NONE, D_POW, D_S, D_L, D_H, D_BOMB, D_1UP };
enum { F_LINE, F_V, F_SINE, F_SWL, F_SWR, F_DIVE, F_GUN2, F_HEAVY, F_LOOP, F_WALL, F_TURR3,
       F_ROCKS, F_BIGROCK, F_DIAG };

typedef struct { int dx, dy; bool fire, bomb; } Ctl;
typedef struct {
    float x, y, vx, vy, a, b, rad, avx, avy;
    int kind, hp, t, pat, delay, drop, flash, dir, w, h, fcd;
    bool alive;
} En;
typedef struct { float x, y, vx, vy; int dmg, kind, life; bool pierce, alive; Uint64 hit; } PB;
typedef struct { float x, y, vx, vy; bool alive, big; } EB;
typedef struct { float x, y, vy; int kind, t; bool alive; } Item;
typedef struct { float x, y, vx, vy; int life, col, sz; } Part;
typedef struct { float x, y, r; int t, mt; } Fire;
typedef struct { float x, y; int t; char txt[12]; } Pop;

#define MAXE 72
#define MAXPB 160
#define MAXEB 420
#define MAXIT 16
#define MAXP 520
#define MAXFB 40
#define MAXPOP 14

static En en[MAXE];
static PB pb[MAXPB];
static EB eb[MAXEB];
static Item items[MAXIT];
static Part parts[MAXP];
static Fire fires[MAXFB];
static Pop pops[MAXPOP];

static struct {
    bool active;
    int type, state, t, phase, hp, maxhp, w, h, flash, dt;
    float x, y;
} B;

static struct {
    float x, y;
    int dead, inv, fcd, shots;
    float vx;
} P;

static struct {
    int state, st, tick, title_page;
    int stage, score, hi, lives, bombs, wlevel, wtype;
    int chain, chain_t, next_life, stt, script_i, bossphase, bosst;
    int bombs_used, shake, flash, spawned, killed, deaths, rank, name_len, letter, bonus, bonus_t;
    bool paused, quitq, bomb_prev, pad_start_prev;
    char name[4];
    float scroll;
} G;

static bool selftest, bot_mode, bot_okp;
static Ctl bot_c;

/* ================================================================== tables */
static const struct { int w, h, hp, pts; } KD[K_N] = {
    {12, 10, 2, 100}, {14, 10, 3, 150}, {10, 14, 3, 200}, {18, 14, 14, 400},
    {14, 14, 10, 300}, {26, 16, 34, 1000}, {0, 0, 0, 60},
};
static const char *STAGE_NAME[NSTAGES] = {"OUTER REACH", "CINDER BELT", "VEIL WORLD", "IRON CITADEL"};
static const char *BOSS_NAME[NSTAGES] = {"WARDEN", "GRINDER", "MANTID", "OVERSEER CORE"};
static const float SCROLL_V[NSTAGES] = {0.5f, 0.6f, 0.9f, 1.0f};

typedef struct { int t, f, x, drop; } Sc;
static const Sc SCRIPT[NSTAGES][20] = {
    {{100, F_LINE, 96, 0}, {260, F_V, 96, 0}, {430, F_SWL, 0, D_POW}, {600, F_SWR, 0, 0},
     {780, F_SINE, 70, 0}, {950, F_DIAG, 0, D_POW}, {1100, F_LINE, 96, 0}, {1250, F_DIVE, 96, D_L},
     {1450, F_V, 60, 0}, {1600, F_SWL, 0, 0}, {1650, F_SWR, 0, D_POW}, {1850, F_GUN2, 96, 0},
     {2050, F_SINE, 130, 0}, {2200, F_LOOP, 96, D_BOMB}, {2400, F_V, 96, D_POW}, {2600, F_DIVE, 96, 0},
     {-1, 0, 0, 0}},
    {{100, F_ROCKS, 0, 0}, {300, F_LINE, 96, D_POW}, {450, F_ROCKS, 0, 0}, {650, F_DIVE, 96, 0},
     {800, F_BIGROCK, 60, 0}, {950, F_SWL, 0, D_H}, {1100, F_ROCKS, 0, 0}, {1300, F_SINE, 96, 0},
     {1450, F_BIGROCK, 130, D_POW}, {1600, F_V, 96, 0}, {1800, F_ROCKS, 0, 0}, {1950, F_GUN2, 96, D_BOMB},
     {2150, F_LOOP, 96, 0}, {2300, F_BIGROCK, 96, 0}, {2450, F_ROCKS, 0, D_POW}, {2650, F_DIVE, 96, 0},
     {-1, 0, 0, 0}},
    {{100, F_TURR3, 96, 0}, {250, F_SWL, 0, 0}, {420, F_WALL, 0, D_POW}, {600, F_TURR3, 60, 0},
     {760, F_LOOP, 96, 0}, {900, F_GUN2, 96, D_S}, {1100, F_V, 96, 0}, {1250, F_TURR3, 130, D_POW},
     {1400, F_DIVE, 96, 0}, {1550, F_SINE, 60, D_BOMB}, {1700, F_HEAVY, 96, 0}, {1950, F_SWR, 0, 0},
     {2100, F_WALL, 0, D_POW}, {2250, F_TURR3, 96, 0}, {2400, F_GUN2, 96, 0}, {2600, F_LOOP, 96, D_1UP},
     {-1, 0, 0, 0}},
    {{100, F_WALL, 0, 0}, {250, F_V, 96, 0}, {400, F_GUN2, 96, D_POW}, {560, F_TURR3, 96, 0},
     {700, F_DIVE, 96, 0}, {850, F_HEAVY, 96, D_POW}, {1100, F_LOOP, 96, 0}, {1250, F_SWL, 0, 0},
     {1300, F_SWR, 0, D_BOMB}, {1500, F_TURR3, 60, 0}, {1550, F_SINE, 130, 0}, {1750, F_GUN2, 96, D_POW},
     {1950, F_DIAG, 0, 0}, {2100, F_HEAVY, 96, D_S}, {2350, F_WALL, 0, 0}, {2500, F_V, 96, D_POW},
     {2700, F_LOOP, 96, 0}, {-1, 0, 0, 0}},
};

/* ================================================================== sprites (CGA: 0 black, b cyan, d magenta, f white) */
enum { SP_PLAYER, SP_SCOUT, SP_WEAVER, SP_DIVER, SP_GUNSHIP, SP_TURRET, SP_HEAVY, SP_N };
static RtSpr SPV[2][SP_N];

static const char *H_PLAYER[16] = {
    ".......f", ".......f", "......bf", "......bf", ".....bbf", ".....bdf", "..b..bdf", "..b.bbdf",
    ".bb.bbdf", ".bbbbbbf", "bbbbbfdf", "bfbbbbbf", "bf.bbbbb", "bf..bb.b", "b....b..", ".....d.."};
static const char *H_SCOUT[10] = {
    "...bbb", "..bbbb", ".bbffd", "bbbfdd", "bbbbbd", "dbb00b", "d.dbbb", "...ddb", "....bd", ".....d"};
static const char *H_WEAVER[10] = {
    "......f", "....bbf", "..bbbbd", ".bbdddd", "bbbbbd0", "b.bbbd0", "b..bbbd", "...bbbf", "....bbf", ".....bd"};
static const char *H_DIVER[14] = {
    "bbbbf", "bbbbd", "dbbbd", ".dbbf", ".dbb0", "..bbf", "..dbf", "..dbf", "...df", "...bf", "...bf",
    "....f", "....f", "....d"};
static const char *H_GUNSHIP[14] = {
    "....bbbbb", "...bbbfff", "..bbbfddd", ".bbbfdddd", "bbbbfd00d", "bdbbbdd0d", "bddbbbbdd", "bd.bbbbbb",
    "b..dbbbbf", "b..d.bbbf", "...d..bbb", "...d...db", "...f....d", "........."};
static const char *H_TURRET[14] = {
    ".....bbb", "...bbbbd", "..bbbddd", ".bbbdfff", ".bbdfffb", "bbbdffbb", "bbbbdddb", "bdbbbbbb", "bdbb0bbb",
    ".bbb0dbb", "..bb.0bb", "...b.0fb", "....0ffb", "....00bb"};
static const char *H_HEAVY[16] = {
    ".......bbbbbb", "....bbbbbfffd", "..bbbbbffdddd", ".bbbdddffd000", "bbbbdfddfd0bb",
    "bdbbdfffdd0bb", "bddbbddddbbbb", "bd.bbbbbbbbbf", "b..bdddbbbbdd", "b..bdfdbbddd0",
    "...bddbbbbd0b", "...b.dbbbbbbb", "...d..bbfffdd", "...f...bdd00d", "........dfbbb", ".........ddd0"};

static RtSpr mk_half(const char *const *half, int h, bool swap) {
    static char buf[20][48];
    const char *rows[20];
    int hw = (int)strlen(half[0]);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < hw; x++) {
            char c = half[y][x];
            if (swap) c = c == 'b' ? 'd' : c == 'd' ? 'b' : c;
            buf[y][x] = c;
            buf[y][hw * 2 - 1 - x] = c;
        }
        buf[y][hw * 2] = 0;
        rows[y] = buf[y];
    }
    return rt_sprite(rows, h);
}

static void load_sprites(void) {
    for (int v = 0; v < 2; v++) {
        SPV[v][SP_PLAYER] = mk_half(H_PLAYER, 16, v);
        SPV[v][SP_SCOUT] = mk_half(H_SCOUT, 10, v);
        SPV[v][SP_WEAVER] = mk_half(H_WEAVER, 10, v);
        SPV[v][SP_DIVER] = mk_half(H_DIVER, 14, v);
        SPV[v][SP_GUNSHIP] = mk_half(H_GUNSHIP, 14, v);
        SPV[v][SP_TURRET] = mk_half(H_TURRET, 14, v);
        SPV[v][SP_HEAVY] = mk_half(H_HEAVY, 16, v);
    }
}
static void spr(RtSpr s, float cx, float cy, bool flash) {
    int x = (int)(cx - s.w / 2.0f), y = (int)(cy - s.h / 2.0f);
    rt_draw(s, x, y, false);
    if (flash) rt_draw_tint(s, x, y, false, WH);
}

/* ================================================================== backgrounds (baked, dithered) */
static const unsigned char BAY[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
static bool dith(int x, int y, int lvl) { return BAY[y & 3][x & 3] < lvl; }
static unsigned hsh(int x, int y) {
    unsigned h = (unsigned)x * 374761393u + (unsigned)y * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}
static float vnoise(float x, float y, int gw, int gh) {
    int xi = (int)floorf(x), yi = (int)floorf(y);
    float fx = x - xi, fy = y - yi;
    fx = fx * fx * (3 - 2 * fx);
    fy = fy * fy * (3 - 2 * fy);
#define HN(a, b) ((float)(hsh((((a) % gw) + gw) % gw, (((b) % gh) + gh) % gh) & 1023) / 1023.0f)
    float a = HN(xi, yi) + (HN(xi + 1, yi) - HN(xi, yi)) * fx;
    float b = HN(xi, yi + 1) + (HN(xi + 1, yi + 1) - HN(xi, yi + 1)) * fx;
#undef HN
    return a + (b - a) * fy;
}

static SDL_Surface *bs;
static Uint32 pcol(int c) { return 0xFF000000u | (Uint32)rt_pal[c].r << 16 | (Uint32)rt_pal[c].g << 8 | rt_pal[c].b; }
static void bput(int x, int y, int c) {
    if (x < 0 || y < 0 || x >= bs->w || y >= bs->h) return;
    ((Uint32 *)((Uint8 *)bs->pixels + y * bs->pitch))[x] = pcol(c);
}
static void bblob(int cx, int cy, float r, int col, int lvl_in, int lvl_out, bool outline, int H) {
    float ph = (float)(hsh(cx, cy) & 255) / 40.0f;
    for (int wrap = -1; wrap <= 1; wrap++) {
        int oy = cy + wrap * H;
        for (int y = oy - (int)r - 6; y <= oy + (int)r + 6; y++)
            for (int x = cx - (int)r - 6; x <= cx + (int)r + 6; x++) {
                float dx = (float)(x - cx), dy = (float)(y - oy);
                float d = sqrtf(dx * dx + dy * dy), ang = atan2f(dy, dx);
                float rr = r * (1.0f + 0.18f * sinf(3 * ang + ph) + 0.08f * sinf(5 * ang + ph * 2));
                if (d > rr) continue;
                int yy = ((y % H) + H) % H;
                float lit = 1.0f - (dx * 0.6f + dy * 0.8f) / (rr * 2.0f) - 0.5f; /* light from upper left */
                int lvl = lvl_out + (int)((lvl_in - lvl_out) * (lit + 0.2f));
                if (lvl < 0) lvl = 0;
                if (outline && d > rr - 1.5f) bput(x, yy, col);
                else if (dith(x, yy, lvl)) bput(x, yy, col);
            }
    }
}

static SDL_Texture *bgtex;
static int bgH;

static void bake_bg(int stage) {
    if (bgtex) SDL_DestroyTexture(bgtex);
    bgH = stage == 3 ? 384 : stage == 2 ? 384 : 400;
    bs = SDL_CreateRGBSurfaceWithFormat(0, PW, bgH, 32, SDL_PIXELFORMAT_ARGB8888);
    SDL_FillRect(bs, NULL, pcol(BK));
    if (stage == 0) { /* deep space: faint magenta/cyan nebula dust */
        for (int i = 0; i < 7; i++)
            bblob((int)(hsh(i, 1) % PW), (int)(hsh(i, 2) % bgH), 22 + (float)(hsh(i, 3) % 18), i % 3 ? MG : CY, 3, 0, false, bgH);
    } else if (stage == 1) { /* asteroid field */
        for (int i = 0; i < 9; i++)
            bblob((int)(hsh(i, 11) % PW), (int)(hsh(i, 12) % bgH), 14 + (float)(hsh(i, 13) % 20), MG, 7, 2, false, bgH);
        for (int i = 0; i < 6; i++)
            bblob((int)(hsh(i, 21) % PW), (int)(hsh(i, 22) % bgH), 28, CY, 3, 0, false, bgH);
    } else if (stage == 2) { /* planet surface */
        for (int y = 0; y < bgH; y++)
            for (int x = 0; x < PW; x++) {
                float v = vnoise(x / 32.0f, y / 32.0f, 7, 12) * 0.7f + vnoise(x / 12.0f, y / 12.0f, 17, 32) * 0.3f;
                if (v > 0.74f) { if (dith(x, y, 4)) bput(x, y, WH); else if (dith(x, y, 8)) bput(x, y, CY); }
                else if (v > 0.36f) { if (dith(x, y, (int)((v - 0.36f) * 14))) bput(x, y, CY); }
                float rx = 96 + 52 * sinf(y * 2 * PI_F / bgH * 2) + 22 * sinf(y * 2 * PI_F / bgH * 5);
                float dd = fabsf(x - rx);
                if (dd < 4.5f) {
                    bput(x, y, BK);
                    if (dd < 3.0f && dith(x, y, 7)) bput(x, y, MG);
                } else if (dd < 5.5f && dith(x, y, 8)) bput(x, y, WH);
            }
        for (int i = 0; i < 7; i++) { /* craters */
            int cx = (int)(hsh(i, 31) % PW), cy = (int)(hsh(i, 32) % bgH), r = 5 + (int)(hsh(i, 33) % 7);
            for (int a = 0; a < 72; a++) {
                float an = a * PI_F / 36;
                bput(cx + (int)(cosf(an) * r), cy + (int)(sinf(an) * r), WH);
                if (a % 2) bput(cx + (int)(cosf(an) * (r - 1)), cy + (int)(sinf(an) * (r - 1)), BK);
            }
        }
    } else { /* station hull */
        for (int y = 0; y < bgH; y++)
            for (int x = 0; x < PW; x++) {
                int px = x / 24, py = y / 24, lx = x % 24, ly = y % 24;
                unsigned h = hsh(px, py);
                int lvl = 1 + (int)(h % 3);
                if (dith(x, y, lvl)) bput(x, y, CY);
                if ((lx == 0 || ly == 0) && (x + y) % 2 == 0) bput(x, y, CY);
                else if (lx == 1 || ly == 1) bput(x, y, BK);
                if (lx == 4 && ly == 4) bput(x, y, WH);
                if (lx == 19 && ly == 19) bput(x, y, WH);
                if (h % 5 == 0 && ly >= 9 && ly <= 14) { /* conduit */
                    bput(x, y, ly == 9 || ly == 14 ? CY : BK);
                    if (ly >= 10 && ly <= 13 && (lx / 3 + ly) % 2 == 0) bput(x, y, CY);
                }
                if (h % 7 == 3 && lx > 4 && lx < 20 && ly > 4 && ly < 20) { if (((x + y) / 4) % 2 && dith(x, y, 9)) bput(x, y, MG); }
            }
    }
    bgtex = SDL_CreateTextureFromSurface(rt_ren, bs);
    SDL_FreeSurface(bs);
    bs = NULL;
}

/* side-panel art, baked once */
static SDL_Texture *paneltex;
static void bake_panel(void) {
    bs = SDL_CreateRGBSurfaceWithFormat(0, 64, PH, 32, SDL_PIXELFORMAT_ARGB8888);
    SDL_FillRect(bs, NULL, pcol(BK));
    for (int y = 0; y < PH; y++) {
        bput(63, y, WH);
        bput(62, y, BK);
        for (int x = 59; x < 62; x++)
            if (dith(x, y, x < 60 ? 4 : 9)) bput(x, y, CY);
        if (y % 16 == 0) for (int x = 58; x < 64; x++) bput(x, y, MG);
    }
    paneltex = SDL_CreateTextureFromSurface(rt_ren, bs);
    SDL_FreeSurface(bs);
    bs = NULL;
}

/* ================================================================== sound */
static float semi(int s) { return 440.0f * powf(2.0f, s / 12.0f); }
static void sfx_shoot(void) { rt_tone(1500, 600, 22, 1); }
static void sfx_hit(void) { rt_tone(260, 200, 14, 2); }
static void sfx_boom(void) { RtNote n[] = {{700, 120, 50}, {400, 60, 70}, {160, 40, 90}}; rt_play(n, 3, 4); }
static void sfx_big(void) { RtNote n[] = {{500, 60, 100}, {0, 0, 15}, {300, 40, 140}, {120, 30, 200}}; rt_play(n, 4, 7); }
static void sfx_pick(void) { RtNote n[] = {{880, 880, 40}, {1175, 1175, 40}, {1760, 1760, 70}}; rt_play(n, 3, 6); }
static void sfx_bomb(void) { RtNote n[] = {{200, 1800, 220}, {1800, 80, 380}}; rt_play(n, 2, 8); }
static void sfx_die(void) { RtNote n[] = {{900, 200, 160}, {0, 0, 20}, {400, 50, 400}}; rt_play(n, 3, 9); }
static void sfx_tick(void) { rt_tone(1500, 1500, 10, 1); }
static void sfx_select(void) { rt_tone(660, 990, 50, 4); }
static void sfx_warn(void) {
    RtNote n[8];
    for (int i = 0; i < 8; i++) n[i] = (RtNote){i % 2 ? 440 : 660, i % 2 ? 440 : 660, 160};
    rt_play(n, 8, 6);
}
static void sfx_1up(void) {
    RtNote n[6];
    static const int m[] = {0, 4, 7, 12, 7, 16};
    for (int i = 0; i < 6; i++) n[i] = (RtNote){semi(m[i]), semi(m[i]), 70};
    rt_play(n, 6, 7);
}
/* original title theme: a brisk minor-key march */
static void tune_title(void) {
    static const int mel[][2] = {
        {-12, 2}, {-9, 2}, {-5, 2}, {0, 2}, {-2, 2}, {-5, 2}, {-9, 2}, {-5, 2},
        {-14, 2}, {-10, 2}, {-7, 2}, {-2, 2}, {-3, 2}, {-7, 2}, {-10, 2}, {-7, 2},
        {-12, 2}, {-9, 2}, {-5, 2}, {0, 2}, {3, 2}, {2, 2}, {0, 2}, {-5, 2},
        {-7, 4}, {-9, 4}, {-12, 8},
    };
    RtNote n[RT_QMAX];
    int k = 0;
    for (int i = 0; i < 27 && k < RT_QMAX - 1; i++) {
        float f = semi(mel[i][0]);
        n[k++] = (RtNote){f, f, mel[i][1] * 62 - 14};
        n[k++] = (RtNote){0, 0, 14};
    }
    rt_play(n, k, 2);
}
static void tune_card(void) {
    static const int mel[] = {-12, -5, 0, 3, 0, 7};
    RtNote n[12];
    for (int i = 0; i < 6; i++) {
        n[i * 2] = (RtNote){semi(mel[i]), semi(mel[i]), i == 5 ? 260 : 80};
        n[i * 2 + 1] = (RtNote){0, 0, 12};
    }
    rt_play(n, 12, 7);
}
static void tune_clear(void) {
    static const int mel[][2] = {{0, 1}, {4, 1}, {7, 1}, {12, 3}, {7, 1}, {12, 4}};
    RtNote n[12];
    for (int i = 0; i < 6; i++) {
        n[i * 2] = (RtNote){semi(mel[i][0] - 5), semi(mel[i][0] - 5), mel[i][1] * 90};
        n[i * 2 + 1] = (RtNote){0, 0, 15};
    }
    rt_play(n, 12, 9);
}
static void tune_over(void) {
    static const int mel[] = {-5, -6, -7, -8, -9, -12, -17};
    RtNote n[14];
    for (int i = 0; i < 7; i++) {
        n[i * 2] = (RtNote){semi(mel[i]), semi(mel[i]), i == 6 ? 500 : 180};
        n[i * 2 + 1] = (RtNote){0, 0, 30};
    }
    rt_play(n, 14, 9);
}
static void tune_win(void) {
    static const int mel[][2] = {{0, 2}, {4, 2}, {7, 2}, {12, 4}, {7, 2}, {12, 2}, {16, 6},
                                 {14, 2}, {12, 2}, {11, 2}, {12, 8}};
    RtNote n[22];
    for (int i = 0; i < 11; i++) {
        n[i * 2] = (RtNote){semi(mel[i][0] - 9), semi(mel[i][0] - 9), mel[i][1] * 80};
        n[i * 2 + 1] = (RtNote){0, 0, 20};
    }
    rt_play(n, 22, 9);
}

/* ================================================================== fx helpers */
static float frnd(float a, float b) { return a + (b - a) * rt_randf(); }

static void burst(float x, float y, int n, float spd, int life) {
    for (int i = 0; i < n; i++)
        for (int j = 0; j < MAXP; j++)
            if (parts[j].life <= 0) {
                float a = frnd(0, 2 * PI_F), s = frnd(0.2f, 1.0f) * spd;
                int r = (int)(rt_rand() % 6);
                parts[j] = (Part){x, y, cosf(a) * s, sinf(a) * s, life / 2 + (int)(rt_rand() % (unsigned)(life / 2 + 1)),
                                  r < 2 ? WH : r < 4 ? CY : MG, r == 0 ? 2 : 1};
                break;
            }
}
static void fireball(float x, float y, float r) {
    for (int i = 0; i < MAXFB; i++)
        if (fires[i].mt == 0) { fires[i] = (Fire){x, y, r, 0, 18 + (int)r}; return; }
}
static void explode(float x, float y, float r, int nparts) {
    fireball(x, y, r);
    burst(x, y, nparts, 1.0f + r * 0.12f, 34);
}
static void popup(float x, float y, const char *t) {
    for (int i = 0; i < MAXPOP; i++)
        if (pops[i].t <= 0) {
            pops[i].x = x; pops[i].y = y; pops[i].t = 40;
            snprintf(pops[i].txt, sizeof(pops[i].txt), "%s", t);
            return;
        }
}
static int mult(void) { int m = 1 + G.chain / 5; return m > 8 ? 8 : m; }
static void add_score(int pts) {
    G.score += pts;
    if (G.score > G.hi) G.hi = G.score;
    int thr = G.next_life == 0 ? 30000 : G.next_life == 1 ? 80000 : 80000 + (G.next_life - 1) * 100000;
    if (G.score >= thr) {
        G.next_life++;
        if (G.lives < 9) G.lives++;
        sfx_1up();
        popup(P.x, P.y - 14, "1UP!");
    }
}

static void ebullet(float x, float y, float ang, float spd, bool big) {
    for (int i = 0; i < MAXEB; i++)
        if (!eb[i].alive) { eb[i] = (EB){x, y, sinf(ang) * spd, cosf(ang) * spd, true, big}; return; }
}
static float aim_at(float x, float y) { return atan2f(P.x - x, P.y - y); }
static void fan(float x, float y, int n, float spread, float base, float spd) {
    for (int i = 0; i < n; i++) ebullet(x, y, base + (n == 1 ? 0 : (i - (n - 1) / 2.0f) * spread), spd, false);
}
static float bspeed(void) { return 1.5f + 0.2f * G.stage; }

/* ================================================================== enemies */
static En *add_en(int kind, float x, float y, int pat) {
    for (int i = 0; i < MAXE; i++)
        if (!en[i].alive) {
            En *e = &en[i];
            memset(e, 0, sizeof(*e));
            e->alive = true; e->kind = kind; e->x = x; e->y = y; e->pat = pat;
            e->w = KD[kind].w; e->h = KD[kind].h;
            e->hp = KD[kind].hp + (kind == K_SCOUT || kind == K_WEAVER ? 0 : G.stage / 2);
            e->a = x; e->vy = 1.1f; e->dir = 1;
            G.spawned++;
            return e;
        }
    return NULL;
}
static En *add_rock(float x, float y, float rad) {
    En *e = add_en(K_ROCK, x, y, PT_ROCK);
    if (!e) return NULL;
    e->rad = rad; e->w = e->h = (int)(rad * 2); e->hp = 2 + (int)(rad * 0.9f);
    e->vx = frnd(-0.5f, 0.5f); e->vy = frnd(0.7f, 1.5f);
    return e;
}

static void spawn_form(int f, int x, int drop) {
    En *last = NULL, *e;
    switch (f) {
    case F_LINE:
        for (int i = 0; i < 5; i++) if ((e = add_en(K_SCOUT, x + (i - 2) * 26, -12, PT_DOWN))) { e->vy = 1.2f; last = e; }
        break;
    case F_V:
        for (int i = 0; i < 5; i++) if ((e = add_en(K_SCOUT, x + (i - 2) * 22, -12 - abs(i - 2) * 14.0f, PT_DOWN))) { e->vy = 1.2f; last = e; }
        break;
    case F_SINE:
        for (int i = 0; i < 6; i++) if ((e = add_en(K_WEAVER, x, -12, PT_SINE))) { e->delay = i * 14; e->b = 42; e->vy = 1.0f; last = e; }
        break;
    case F_SWL: case F_SWR:
        for (int i = 0; i < 5; i++) {
            int d = f == F_SWL ? 1 : -1;
            if ((e = add_en(K_SCOUT, d > 0 ? -10 : PW + 10, 24, PT_SWOOP))) { e->dir = d; e->delay = i * 12; e->vx = d * 2.1f; e->vy = 0.9f; last = e; }
        }
        break;
    case F_DIVE:
        for (int i = 0; i < 3; i++) if ((e = add_en(K_DIVER, x + (i - 1) * 44, -14, PT_DIVE))) { e->delay = i * 22; e->vy = 0.8f; last = e; }
        break;
    case F_GUN2:
        for (int i = 0; i < 2; i++) if ((e = add_en(K_GUNSHIP, x + (i ? 34 : -34), -14, PT_HOVER))) { e->b = i ? 66 : 48; e->delay = i * 30; last = e; }
        break;
    case F_HEAVY:
        if ((e = add_en(K_HEAVY, x, -16, PT_HOVER))) { e->b = 50; last = e; }
        for (int i = 0; i < 2; i++) if ((e = add_en(K_WEAVER, x + (i ? 50 : -50), -12, PT_SINE))) { e->delay = 20 + i * 40; e->b = 30; }
        break;
    case F_LOOP:
        for (int i = 0; i < 4; i++) if ((e = add_en(K_SCOUT, x, -20, PT_LOOP))) { e->delay = i * 20; e->vy = 0.95f; e->b = 24; last = e; }
        break;
    case F_WALL:
        for (int i = 0; i < 6; i++) {
            int side = i % 2;
            if ((e = add_en(K_WEAVER, side ? PW - 22 : 22, -12, PT_DOWN))) { e->delay = (i / 2) * 26; e->vx = side ? -0.35f : 0.35f; e->vy = 1.25f; last = e; }
        }
        break;
    case F_TURR3:
        for (int i = 0; i < 3; i++) if ((e = add_en(K_TURRET, x + (i - 1) * 54, -14 - i * 46.0f, PT_TURRET))) last = e;
        break;
    case F_ROCKS:
        for (int i = 0; i < 6; i++) if ((e = add_rock(frnd(14, PW - 14), -20 - i * 26.0f, frnd(5, 11)))) last = e;
        break;
    case F_BIGROCK:
        if ((e = add_rock((float)x, -26, 19))) { e->vy = 0.8f; last = e; }
        break;
    case F_DIAG:
        for (int i = 0; i < 5; i++) if ((e = add_en(K_SCOUT, 10, -12, PT_DOWN))) { e->delay = i * 13; e->vx = 1.15f; e->vy = 1.25f; last = e; }
        break;
    }
    if (last) last->drop = drop;
}

static void drop_item(float x, float y, int kind) {
    for (int i = 0; i < MAXIT; i++)
        if (!items[i].alive) { items[i] = (Item){x, y, 0.8f, kind, 0, true}; return; }
}

static void kill_en(En *e) {
    e->alive = false;
    G.killed++;
    G.chain++;
    G.chain_t = 120;
    int pts = KD[e->kind].pts * mult();
    add_score(pts);
    if (e->kind == K_ROCK) {
        explode(e->x, e->y, e->rad, 10 + (int)e->rad);
        if (e->rad >= 12) for (int s = -1; s <= 1; s += 2) { En *r = add_rock(e->x + s * 6, e->y, 6); if (r) { r->vx = s * 0.8f; r->vy = e->vy; } }
        sfx_boom();
    } else {
        float r = e->kind == K_HEAVY ? 16 : e->kind == K_GUNSHIP ? 12 : 8;
        explode(e->x, e->y, r, e->kind == K_HEAVY ? 40 : 18);
        if (e->kind == K_HEAVY) { sfx_big(); G.shake = 8; } else sfx_boom();
        if (e->kind >= K_GUNSHIP && mult() > 1) {
            char t[12]; snprintf(t, sizeof(t), "%d", pts); popup(e->x, e->y, t);
        }
    }
    if (e->drop) drop_item(e->x, e->y, e->drop);
}

static void enemy_fire(En *e) {
    float bsp = bspeed();
    if (e->y < 6 || e->y > PH - 36) return;
    switch (e->kind) {
    case K_SCOUT:
        if (e->t > 20 && rt_rand() % (330 - G.stage * 50) == 0) ebullet(e->x, e->y + 4, aim_at(e->x, e->y), bsp, false);
        break;
    case K_WEAVER:
        if (G.stage >= 1 && e->t > 30 && rt_rand() % 240 == 0) ebullet(e->x, e->y + 4, aim_at(e->x, e->y), bsp, false);
        break;
    case K_GUNSHIP:
        if (e->t > 30 && --e->fcd <= 0) { e->fcd = 80 - G.stage * 8; fan(e->x, e->y + 6, 4, 0.26f, aim_at(e->x, e->y), bsp); }
        break;
    case K_TURRET:
        if (e->t > 10 && --e->fcd <= 0) { e->fcd = 70 - G.stage * 5; ebullet(e->x, e->y + 4, aim_at(e->x, e->y), bsp + 0.2f, true); }
        break;
    case K_HEAVY:
        if (e->t > 30 && --e->fcd <= 0) {
            e->fcd = 85;
            fan(e->x, e->y + 8, 5, 0.32f, aim_at(e->x, e->y), bsp);
            ebullet(e->x - 10, e->y + 8, aim_at(e->x - 10, e->y), bsp + 0.3f, true);
            ebullet(e->x + 10, e->y + 8, aim_at(e->x + 10, e->y), bsp + 0.3f, true);
        }
        break;
    default: break;
    }
}

static void enemies_update(void) {
    for (int i = 0; i < MAXE; i++) {
        En *e = &en[i];
        if (!e->alive) continue;
        if (e->delay > 0) { e->delay--; continue; }
        float ox = e->x, oy = e->y;
        e->t++;
        if (e->flash > 0) e->flash--;
        switch (e->pat) {
        case PT_DOWN: e->x += e->vx; e->y += e->vy; break;
        case PT_SINE: e->y += e->vy; e->x = e->a + e->b * sinf(e->t * 0.06f); break;
        case PT_SWOOP:
            e->vx -= e->dir * 0.036f;
            e->x += e->vx; e->y += e->vy;
            break;
        case PT_DIVE:
            if (e->t < 50) e->y += e->vy;
            else {
                if (e->t == 50) {
                    float a = aim_at(e->x, e->y);
                    e->vx = sinf(a) * 2.7f; e->vy = cosf(a) * 2.7f;
                }
                e->x += e->vx; e->y += e->vy;
            }
            break;
        case PT_HOVER:
            if (e->t < 400 && e->y < e->b) e->y += 1.2f;
            else if (e->t < 260) e->x += sinf(e->t * 0.04f) * 0.5f;
            else e->y += 1.4f;
            break;
        case PT_LOOP:
            e->x = e->a + e->b * sinf(e->t * 0.085f);
            e->y = -20 + e->t * e->vy - e->b * cosf(e->t * 0.085f) + e->b;
            break;
        case PT_TURRET: e->y += SCROLL_V[G.stage]; break;
        case PT_ROCK: e->x += e->vx; e->y += e->vy; break;
        }
        e->avx = e->x - ox;
        e->avy = e->y - oy;
        enemy_fire(e);
        if (e->y > PH + 30 || e->y < -300 || e->x < -50 || e->x > PW + 50) {
            if (e->t > 5 || e->y > PH + 30) e->alive = false;
        }
    }
}

/* ================================================================== bosses */
static void boss_spawn(int stage) {
    memset(&B, 0, sizeof(B));
    B.active = true; B.type = stage; B.state = 0;
    B.x = PW / 2.0f; B.y = -50;
    static const int W[4] = {56, 50, 62, 40}, Hh[4] = {24, 34, 26, 40};
    B.w = W[stage]; B.h = Hh[stage];
    B.maxhp = B.hp = 600 + 180 * stage;
    B.phase = 0;
}

static void boss_attack(void) {
    int t = B.t, ph = B.phase;
    float bsp = bspeed();
    switch (B.type) {
    case 0:
        B.x = 96 + 52 * sinf(t * 0.014f * (1 + ph * 0.3f));
        B.y = 36;
        if (ph == 0 && t % 55 == 0) {
            fan(B.x - 18, B.y + 10, 4, 0.24f, aim_at(B.x - 18, B.y), 1.7f);
            fan(B.x + 18, B.y + 10, 4, 0.24f, aim_at(B.x + 18, B.y), 1.7f);
        } else if (ph == 1) {
            if (t % 80 == 0) for (int i = 0; i < 12; i++) ebullet(B.x, B.y + 8, i * PI_F / 6 + t * 0.1f, 1.1f, false);
            if (t % 40 == 20) ebullet(B.x, B.y + 10, aim_at(B.x, B.y), 1.8f, true);
        } else if (ph == 2) {
            if (t % 5 == 0) { ebullet(B.x, B.y + 8, t * 0.26f, 1.5f, false); ebullet(B.x, B.y + 8, t * 0.26f + PI_F, 1.5f, false); }
            if (t % 90 == 0) fan(B.x, B.y + 10, 5, 0.3f, aim_at(B.x, B.y), 1.6f);
        }
        break;
    case 1:
        B.x = 96 + 40 * sinf(t * 0.01f * (1 + ph * 0.3f));
        B.y = 38 + 6 * sinf(t * 0.03f);
        if (ph == 0) {
            if (t % 90 == 0) fan(B.x, B.y + 14, 5, 0.35f, 0, 1.4f);
            if (t % 150 == 60) { En *r = add_rock(B.x, B.y + 16, 7); if (r) { r->vx = frnd(-0.8f, 0.8f); r->vy = 1.2f; } }
        } else if (ph == 1) {
            if (t % 70 == 0) fan(B.x, B.y + 14, 7, 0.3f, 0, 1.5f);
            if (t % 110 == 30) for (int s = -1; s <= 1; s += 2) { En *r = add_rock(B.x + s * 14, B.y + 16, 7); if (r) { r->vx = s * 0.6f; r->vy = 1.3f; } }
        } else {
            if (t % 50 == 0) fan(B.x, B.y + 14, 5, 0.3f, aim_at(B.x, B.y), 1.7f);
            if (t % 100 == 25) for (int i = 0; i < 10; i++) ebullet(B.x, B.y + 12, i * PI_F / 5 + t * 0.05f, 1.2f, false);
            if (t % 150 == 90) { En *r = add_rock(B.x, B.y + 16, 8); if (r) { r->vx = frnd(-0.8f, 0.8f); r->vy = 1.2f; } }
        }
        break;
    case 2:
        B.x = 96 + 60 * sinf(t * 0.018f * (1 + ph * 0.2f));
        B.y = 34;
        if (ph != 1 && t % 8 == 0 && (t / 40) % 2 == 0) {
            ebullet(B.x - 26, B.y + 10, 0, 1.8f, false);
            ebullet(B.x + 26, B.y + 10, 0, 1.8f, false);
        }
        if (ph == 0 && t % 70 == 35) ebullet(B.x, B.y + 12, aim_at(B.x, B.y), 1.7f, true);
        if (ph >= 1 && t % 5 == 0) ebullet(B.x, B.y + 12, 0.75f * sinf(t * 0.05f), 1.6f, false);
        if (ph == 2 && t % 75 == 0) fan(B.x, B.y + 12, 5, 0.3f, aim_at(B.x, B.y), 1.7f);
        break;
    default:
        B.x = 96 + 30 * sinf(t * 0.01f);
        B.y = 44 + 8 * sinf(t * 0.02f);
        if (ph == 0) {
            for (int k = 0; k < 4; k++) {
                float oa = t * 0.03f + k * PI_F / 2, ox = B.x + cosf(oa) * 34, oy = B.y + sinf(oa) * 24;
                if ((t + k * 22) % 90 == 0) ebullet(ox, oy, aim_at(ox, oy), bsp + 0.2f, true);
            }
        } else if (ph == 1) {
            if (t % 7 == 0) for (int k = 0; k < 4; k++) ebullet(B.x, B.y, t * 0.09f + k * PI_F / 2, 1.2f, false);
        } else {
            if (t % 100 == 0) for (int i = 0; i < 16; i++) ebullet(B.x, B.y, i * PI_F / 8 + t * 0.03f, 1.1f, false);
            if (t % 40 == 20) ebullet(B.x, B.y + 12, aim_at(B.x, B.y), 2.0f, true);
            if (t % 9 == 0) for (int k = 0; k < 2; k++) ebullet(B.x, B.y, t * 0.1f + k * PI_F, 1.15f, false);
        }
        break;
    }
}

static void boss_update(void) {
    if (!B.active) return;
    if (B.flash > 0) B.flash--;
    if (B.state == 0) {
        B.y += 0.8f;
        if (B.y >= 36) { B.state = 1; B.t = 0; }
        return;
    }
    if (B.state == 2) {
        B.dt++;
        if (B.dt % 4 == 0) explode(B.x + frnd(-B.w / 2.0f, B.w / 2.0f), B.y + frnd(-B.h / 2.0f, B.h / 2.0f), frnd(5, 12), 8);
        if (B.dt % 12 == 0) sfx_boom();
        G.shake = 4;
        if (B.dt == 100) {
            explode(B.x, B.y, 28, 80);
            sfx_big();
            G.flash = 8;
            G.shake = 14;
        }
        if (B.dt > 130) {
            B.active = false;
            G.state = S_CLEAR;
            G.st = 0;
            G.bonus = G.bombs * 1000 + G.lives * 500 + (G.spawned ? G.killed * 100 / G.spawned : 0) * 50 + 2000 * (G.stage + 1);
            G.bonus_t = 0;
            tune_clear();
        }
        return;
    }
    B.t++;
    int np = B.hp * 3 > B.maxhp * 2 ? 0 : B.hp * 3 > B.maxhp ? 1 : 2;
    if (np != B.phase) {
        B.phase = np;
        B.t = 0;
        memset(eb, 0, sizeof(eb));
        G.shake = 12;
        explode(B.x, B.y, 18, 40);
        sfx_big();
    }
    boss_attack();
}

static void boss_hit(int dmg) {
    if (!B.active || B.state != 1) return;
    B.hp -= dmg;
    B.flash = 2;
    if (B.hp <= 0) {
        B.hp = 0;
        B.state = 2;
        B.dt = 0;
        memset(eb, 0, sizeof(eb));
        add_score(5000 * (G.stage + 1));
        G.chain += 10;
        G.chain_t = 120;
    } else sfx_hit();
}

/* ================================================================== player & weapons */
static void pbullet(float x, float y, float vx, float vy, int dmg, int kind, bool pierce) {
    for (int i = 0; i < MAXPB; i++)
        if (!pb[i].alive) { pb[i] = (PB){x, y, vx, vy, dmg, kind, 0, pierce, true, 0}; return; }
}

static void fire_weapon(void) {
    int L = G.wlevel;
    float x = P.x, y = P.y - 8;
    P.shots++;
    switch (G.wtype) {
    case W_SPREAD:
        P.fcd = 6;
        if (L == 1) pbullet(x, y, 0, -8, 2, 0, false);
        else if (L == 2) { pbullet(x - 4, y, 0, -8, 2, 0, false); pbullet(x + 4, y, 0, -8, 2, 0, false); }
        else if (L == 3) { pbullet(x, y, 0, -8, 2, 0, false); pbullet(x - 4, y, -0.9f, -7.8f, 2, 0, false); pbullet(x + 4, y, 0.9f, -7.8f, 2, 0, false); }
        else {
            pbullet(x, y, 0, -8, 2, 0, false);
            pbullet(x - 3, y, -0.8f, -7.9f, 2, 0, false); pbullet(x + 3, y, 0.8f, -7.9f, 2, 0, false);
            pbullet(x - 5, y + 3, -1.9f, -7.5f, 2, 0, false); pbullet(x + 5, y + 3, 1.9f, -7.5f, 2, 0, false);
        }
        break;
    case W_LANCE: {
        P.fcd = 9;
        int d = L >= 4 ? 6 : L >= 3 ? 5 : 4;
        if (L == 1) pbullet(x, y, 0, -10, d, 1, true);
        else if (L == 2) { pbullet(x - 4, y, 0, -10, d, 1, true); pbullet(x + 4, y, 0, -10, d, 1, true); }
        else { pbullet(x, y, 0, -10, d, 1, true); pbullet(x - 6, y + 2, 0, -10, d, 1, true); pbullet(x + 6, y + 2, 0, -10, d, 1, true); }
        break;
    }
    default:
        P.fcd = 7;
        pbullet(x, y, 0, -8, 2, 0, false);
        if (L >= 3) pbullet(x - 5, y + 2, 0, -8, 2, 0, false);
        if (P.shots % 2 == 0) {
            int n = L;
            for (int i = 0; i < n; i++) pbullet(x + (i - (n - 1) / 2.0f) * 5, y + 4, (i - (n - 1) / 2.0f) * 1.4f, -3.5f, 3, 2, false);
        }
        break;
    }
    if (P.shots % 2 == 0) sfx_shoot();
}

static void do_bomb(void) {
    G.bombs--;
    G.bombs_used++;
    G.flash = 6;
    G.shake = 14;
    P.inv = P.inv > 100 ? P.inv : 100;
    memset(eb, 0, sizeof(eb));
    sfx_bomb();
    for (int i = 0; i < MAXE; i++) {
        En *e = &en[i];
        if (!e->alive || e->delay > 0 || e->y < -10) continue;
        e->hp -= 30; e->flash = 3;
        if (e->hp <= 0) kill_en(e);
    }
    if (B.active && B.state == 1) boss_hit(45);
    for (int i = 0; i < 6; i++) fireball(frnd(10, PW - 10), frnd(20, PH - 30), frnd(14, 26));
    burst(P.x, P.y, 60, 4.0f, 40);
}

static void kill_player(void) {
    if (P.dead > 0 || P.inv > 0) return;
    G.lives--;
    G.deaths++;
    G.chain = 0;
    G.chain_t = 0;
    P.dead = 80;
    explode(P.x, P.y, 14, 50);
    G.shake = 12;
    sfx_die();
    if (G.wlevel > 1) G.wlevel--;
    if (G.bombs < 2) G.bombs = 2;
}

static Ctl read_ctl(void) {
    if (bot_mode) return bot_c;
    Ctl c = {0, 0, false, false};
    const Uint8 *k = SDL_GetKeyboardState(NULL);
    c.dx = (rt_held.r ? 1 : 0) - (rt_held.l ? 1 : 0);
    c.dy = (rt_held.d ? 1 : 0) - (rt_held.u ? 1 : 0);
    c.fire = k[SDL_SCANCODE_SPACE] || k[SDL_SCANCODE_Z] || k[SDL_SCANCODE_X] || rt_held.fire ||
             (rt_pad && SDL_GameControllerGetButton(rt_pad, SDL_CONTROLLER_BUTTON_A));
    c.bomb = rt_held.jet || k[SDL_SCANCODE_B];
    return c;
}

static void player_reset_pos(void) { P.x = PW / 2.0f; P.y = PH - 28; P.vx = 0; }

static void player_update(void) {
    if (P.dead > 0) {
        if (--P.dead == 0) {
            if (G.lives <= 0) {
                G.state = S_OVER; G.st = 0;
                memset(eb, 0, sizeof(eb));
                tune_over();
                return;
            }
            player_reset_pos();
            P.inv = 160;
        }
        return;
    }
    Ctl c = read_ctl();
    float sp = 2.3f;
    if (c.dx && c.dy) sp *= 0.7071f;
    P.vx = c.dx * sp;
    P.x += c.dx * sp;
    P.y += c.dy * sp;
    if (P.x < 8) P.x = 8;
    if (P.x > PW - 8) P.x = PW - 8;
    if (P.y < 14) P.y = 14;
    if (P.y > PH - 10) P.y = PH - 10;
    if (P.inv > 0) P.inv--;
    if (P.fcd > 0) P.fcd--;
    if (c.fire && P.fcd <= 0) fire_weapon();
    if (c.bomb && !G.bomb_prev && G.bombs > 0) do_bomb();
    G.bomb_prev = c.bomb;
}

static void pick_item(Item *it) {
    it->alive = false;
    sfx_pick();
    int k = it->kind;
    if (k == D_POW) {
        if (G.wlevel < 4) G.wlevel++; else add_score(2000);
        popup(P.x, P.y - 12, G.wlevel >= 4 ? "MAX" : "POWER");
    } else if (k == D_S || k == D_L || k == D_H) {
        int w = k == D_S ? W_SPREAD : k == D_L ? W_LANCE : W_SEEKER;
        if (G.wtype == w) { if (G.wlevel < 4) G.wlevel++; else add_score(2000); }
        else G.wtype = w;
        popup(P.x, P.y - 12, w == W_SPREAD ? "SPREAD" : w == W_LANCE ? "LANCE" : "SEEKER");
    } else if (k == D_BOMB) {
        if (G.bombs < 5) G.bombs++; else add_score(1000);
        popup(P.x, P.y - 12, "BOMB");
    } else {
        if (G.lives < 9) G.lives++;
        sfx_1up();
        popup(P.x, P.y - 12, "1UP!");
    }
    add_score(200);
}

static void items_update(void) {
    for (int i = 0; i < MAXIT; i++) {
        Item *it = &items[i];
        if (!it->alive) continue;
        it->t++;
        it->y += it->vy;
        if (it->y > PH + 10) { it->alive = false; continue; }
        if (P.dead == 0 && fabsf(it->x - P.x) < 11 && fabsf(it->y - P.y) < 12) pick_item(it);
    }
}

static En *nearest_target(float x, float y, float *tx, float *ty) {
    En *best = NULL;
    float bd = 1e9f;
    for (int i = 0; i < MAXE; i++) {
        En *e = &en[i];
        if (!e->alive || e->delay > 0 || e->y < 0 || e->y > PH) continue;
        float d = (e->x - x) * (e->x - x) + (e->y - y) * (e->y - y);
        if (d < bd) { bd = d; best = e; *tx = e->x; *ty = e->y; }
    }
    if (B.active && B.state == 1) {
        float d = (B.x - x) * (B.x - x) + (B.y - y) * (B.y - y);
        if (d < bd) { best = NULL; bd = d; *tx = B.x; *ty = B.y; }
    }
    if (bd >= 1e9f) { *tx = x; *ty = -100; }
    return best;
}

static void bullets_update(void) {
    for (int i = 0; i < MAXPB; i++) {
        PB *b = &pb[i];
        if (!b->alive) continue;
        if (b->kind == 2) {
            float tx = b->x, ty = -100;
            nearest_target(b->x, b->y, &tx, &ty);
            float dx = tx - b->x, dy = ty - b->y, d = sqrtf(dx * dx + dy * dy) + 0.01f;
            b->vx += (dx / d * 5.5f - b->vx) * 0.14f;
            b->vy += (dy / d * 5.5f - b->vy) * 0.14f;
            if (b->life < 12) b->vy -= 0.3f;
        }
        b->life++;
        b->x += b->vx; b->y += b->vy;
        if (b->y < -10 || b->x < -10 || b->x > PW + 10 || b->y > PH + 10) { b->alive = false; continue; }
        for (int j = 0; j < MAXE && b->alive; j++) {
            En *e = &en[j];
            if (!e->alive || e->delay > 0 || e->y < -4) continue;
            if (b->pierce && (b->hit >> j & 1)) continue;
            if (fabsf(b->x - e->x) < e->w / 2.0f + 2 && fabsf(b->y - e->y) < e->h / 2.0f + 3) {
                e->hp -= b->dmg; e->flash = 2;
                if (b->pierce) b->hit |= 1ull << j; else b->alive = false;
                if (e->hp <= 0) kill_en(e); else sfx_hit();
            }
        }
        if (b->alive && B.active && B.state == 1 && fabsf(b->x - B.x) < B.w / 2.0f + 2 && fabsf(b->y - B.y) < B.h / 2.0f + 2) {
            if (!(b->pierce && (b->hit >> 63 & 1))) {
                boss_hit(b->dmg);
                if (b->pierce) b->hit |= 1ull << 63; else b->alive = false;
                burst(b->x, b->y, 2, 1.5f, 12);
            }
        }
    }
    bool hitp = false;
    for (int i = 0; i < MAXEB; i++) {
        EB *b = &eb[i];
        if (!b->alive) continue;
        b->x += b->vx; b->y += b->vy;
        if (b->x < -8 || b->x > PW + 8 || b->y < -8 || b->y > PH + 8) { b->alive = false; continue; }
        if (P.dead == 0 && P.inv == 0 && fabsf(b->x - P.x) < (b->big ? 4.5f : 3.5f) && fabsf(b->y - P.y) < (b->big ? 4.5f : 3.5f)) hitp = true;
    }
    if (P.dead == 0 && P.inv == 0) {
        for (int j = 0; j < MAXE; j++) {
            En *e = &en[j];
            if (!e->alive || e->delay > 0 || e->y < -4) continue;
            float hw = e->kind == K_ROCK ? e->rad - 1 : e->w / 2.0f - 2, hh = e->kind == K_ROCK ? e->rad - 1 : e->h / 2.0f - 2;
            if (fabsf(e->x - P.x) < hw + 3 && fabsf(e->y - P.y) < hh + 3) hitp = true;
        }
        if (B.active && B.state == 1 && fabsf(B.x - P.x) < B.w / 2.0f - 2 && fabsf(B.y - P.y) < B.h / 2.0f - 2) hitp = true;
    }
    if (hitp) kill_player();
}

static void update_fx(void) {
    for (int i = 0; i < MAXP; i++) {
        Part *p = &parts[i];
        if (p->life <= 0) continue;
        p->life--;
        p->x += p->vx; p->y += p->vy;
        p->vx *= 0.96f; p->vy *= 0.96f;
    }
    for (int i = 0; i < MAXFB; i++)
        if (fires[i].mt && ++fires[i].t >= fires[i].mt) fires[i].mt = 0;
    for (int i = 0; i < MAXPOP; i++)
        if (pops[i].t > 0) { pops[i].t--; pops[i].y -= 0.4f; }
}

static void script_tick(void) {
    const Sc *s = SCRIPT[G.stage];
    while (s[G.script_i].t >= 0 && G.stt >= s[G.script_i].t) {
        spawn_form(s[G.script_i].f, s[G.script_i].x, s[G.script_i].drop);
        G.script_i++;
    }
    if (s[G.script_i].t < 0 && G.bossphase == 0) {
        int alive = 0;
        for (int i = 0; i < MAXE; i++) if (en[i].alive && en[i].kind != K_ROCK) alive++;
        if (alive == 0 || G.stt > s[G.script_i - 1].t + 600) {
            G.bossphase = 1;
            G.bosst = 0;
            sfx_warn();
        }
    }
    if (G.bossphase == 1) {
        if (++G.bosst >= 130) { G.bossphase = 2; boss_spawn(G.stage); }
    }
}

static void update_play(void) {
    G.stt++;
    G.scroll += SCROLL_V[G.stage];
    if (G.chain_t > 0 && --G.chain_t == 0) G.chain = 0;
    if (G.shake > 0) G.shake--;
    if (G.flash > 0) G.flash--;
    script_tick();
    player_update();
    if (G.state != S_PLAY) return;
    enemies_update();
    boss_update();
    bullets_update();
    items_update();
    update_fx();
}

/* ================================================================== drawing */
static SDL_Texture *rocktex[24];
static SDL_Texture *rock_tex(int r) {
    if (r < 4) r = 4;
    if (r > 22) r = 22;
    if (rocktex[r]) return rocktex[r];
    int s = r * 2 + 6;
    SDL_Surface *sf = SDL_CreateRGBSurfaceWithFormat(0, s, s, 32, SDL_PIXELFORMAT_ARGB8888);
    SDL_FillRect(sf, NULL, 0);
    for (int y = 0; y < s; y++)
        for (int x = 0; x < s; x++) {
            float dx = x - s / 2.0f + 0.5f, dy = y - s / 2.0f + 0.5f, d = sqrtf(dx * dx + dy * dy), a = atan2f(dy, dx);
            float rr = r * (1.0f + 0.14f * sinf(3 * a + r) + 0.07f * sinf(5 * a + r * 2));
            if (d > rr) continue;
            float lit = 0.5f - (dx * 0.6f + dy * 0.8f) / (rr * 2.0f);
            int c = BK;
            if (dith(x, y, 4 + (int)(lit * 10))) c = CY;
            if (d > rr - 1.3f) c = lit > 0.45f ? WH : CY;
            if (((x * 7 + y * 13) % 29) == 0 && d < rr - 3) c = BK;
            ((Uint32 *)((Uint8 *)sf->pixels + y * sf->pitch))[x] = pcol(c);
        }
    rocktex[r] = SDL_CreateTextureFromSurface(rt_ren, sf);
    SDL_SetTextureBlendMode(rocktex[r], SDL_BLENDMODE_BLEND);
    SDL_FreeSurface(sf);
    return rocktex[r];
}

static void ddisc(int cx, int cy, int r, int col, int lvl) {
    rt_col(col);
    for (int dy = -r; dy <= r; dy++) {
        int dx = (int)sqrtf((float)(r * r - dy * dy));
        for (int x = cx - dx; x <= cx + dx; x++)
            if (lvl >= 16 || dith(x, cy + dy, lvl)) SDL_RenderDrawPoint(rt_ren, x, cy + dy);
    }
}
static void draw_fire(float x, float y, float r, int t, int mt) {
    float k = (float)t / (float)mt;
    int xi = (int)x, yi = (int)y;
    if (k < 0.3f) ddisc(xi, yi, (int)(r * (0.5f + k * 1.6f)), WH, 16);
    else if (k < 0.6f) { ddisc(xi, yi, (int)r, CY, 16); ddisc(xi, yi, (int)(r * 0.5f), WH, 12); }
    else if (k < 0.85f) { ddisc(xi, yi, (int)(r * 1.1f), MG, 9); ddisc(xi, yi, (int)(r * 0.6f), CY, 6); }
    else ddisc(xi, yi, (int)(r * 1.2f), MG, 3);
}

static void draw_stars(int layers_mask, float scroll, int w, int h, int ox, int oy) {
    static const float spd[3] = {0.6f, 1.4f, 2.6f};
    for (int l = 0; l < 3; l++) {
        if (!(layers_mask & (1 << l))) continue;
        for (int i = 0; i < 26; i++) {
            float y = fmodf((float)(hsh(i, l * 7 + 100) % (unsigned)h) + scroll * spd[l], (float)h);
            int x = (int)(hsh(i, l * 7 + 5) % (unsigned)w);
            rt_px(ox + x, oy + (int)y, l == 2 ? WH : CY);
            if (l == 2) rt_px(ox + x, oy + (int)y - 1, CY);
        }
    }
}

static void draw_item_icon(int cx, int cy, int kind, int t) {
    int x = cx - 6, y = cy - 6;
    const char *lt = "P";
    int fr = WH, ink = WH;
    switch (kind) {
    case D_POW: lt = "P"; fr = WH; ink = MG; break;
    case D_S: lt = "S"; fr = CY; ink = CY; break;
    case D_L: lt = "L"; fr = MG; ink = MG; break;
    case D_H: lt = "H"; fr = WH; ink = CY; break;
    case D_BOMB: lt = "B"; fr = MG; ink = WH; break;
    default: lt = "1"; fr = (t / 6) % 2 ? WH : MG; ink = WH; break;
    }
    rt_rect(x, y, 12, 12, BK);
    rt_frame(x, y, 12, 12, fr);
    if ((t / 4) % 4 == 0) rt_px(x + 1, y + 1, WH);
    rt_text(lt, x + 2, y + 2, ink);
}

static bool bflash(void) { return B.flash > 0 && G.tick % 4 == 0; }
static void bc_rect(int x, int y, int w, int h, int c) { rt_rect(x, y, w, h, bflash() ? WH : c); }
static void bc_disc(int x, int y, int r, int c) { rt_disc(x, y, r, bflash() ? WH : c); }

static void draw_boss(void) {
    int x = (int)B.x, y = (int)B.y, t = B.t;
    switch (B.type) {
    case 0:
        bc_rect(x - 28, y - 4, 56, 9, CY);
        for (int i = 0; i < 7; i++) bc_rect(x - 26 + i * 8, y - 2, 4, 5, i % 2 ? MG : BK);
        bc_rect(x - 12, y - 11, 24, 22, MG);
        bc_rect(x - 8, y - 8, 16, 16, BK);
        bc_rect(x - 3, y - 3 + (t / 20) % 2, 6, 5, WH);
        bc_rect(x - 27, y + 4, 7, 9, WH); bc_rect(x + 20, y + 4, 7, 9, WH);
        bc_rect(x - 25, y + 9, 3, 6, BK); bc_rect(x + 22, y + 9, 3, 6, BK);
        bc_rect(x - 30, y - 6, 4, 6, CY); bc_rect(x + 26, y - 6, 4, 6, CY);
        break;
    case 1:
        bc_disc(x - 23, y + 5, 10, CY); bc_disc(x + 23, y + 5, 10, CY);
        bc_disc(x - 23, y + 5, 6, BK); bc_disc(x + 23, y + 5, 6, BK);
        bc_disc(x, y, 17, CY);
        bc_disc(x, y, 13, BK);
        bc_disc(x, y, 9, MG);
        bc_disc(x, y, 4 + (t / 10) % 2, WH);
        for (int i = 0; i < 8; i++) {
            float a = i * PI_F / 4 + t * 0.03f;
            rt_line(x + (int)(cosf(a) * 13), y + (int)(sinf(a) * 13), x + (int)(cosf(a) * 20), y + (int)(sinf(a) * 20), WH);
        }
        break;
    case 2:
        for (int s = -1; s <= 1; s += 2) {
            for (int k = 0; k < 3; k++) {
                rt_line(x, y - 4 + k, x + s * 26, y - 10 + k, bflash() ? WH : CY);
                rt_line(x + s * 26, y - 10 + k, x + s * 30, y + 6 + k, bflash() ? WH : CY);
            }
            bc_rect(x + s * 28 - 2, y + 6, 4, 9, WH);
            bc_rect(x + s * 14 - 3, y - 4, 6, 4, MG);
        }
        bc_disc(x, y, 9, MG);
        bc_disc(x, y - 1, 6, BK);
        bc_rect(x - 4, y - 3, 8, 3, (t / 15) % 2 ? WH : CY);
        bc_rect(x - 2, y + 7, 4, 7, CY);
        break;
    default:
        bc_disc(x, y, 18, CY);
        bc_disc(x, y, 14, BK);
        bc_disc(x, y, 10, MG);
        bc_disc(x, y, 5 + (t / 12) % 2, WH);
        for (int i = 0; i < 4; i++) {
            float a = (B.state == 1 ? t : 0) * 0.03f + i * PI_F / 2;
            int ox = x + (int)(cosf(a) * 34), oy = y + (int)(sinf(a) * 24);
            bc_disc(ox, oy, 5, i % 2 ? CY : WH);
            bc_disc(ox, oy, 2, BK);
        }
        bc_rect(x - 20, y - 2, 6, 4, WH); bc_rect(x + 14, y - 2, 6, 4, WH);
        break;
    }
}

static void draw_player(void) {
    if (P.dead > 0) return;
    if (P.inv > 0 && (P.inv / 3) % 2) return;
    int x = (int)P.x, y = (int)P.y;
    int fl = 2 + (G.tick / 2) % 3;
    rt_rect(x - 1, y + 7, 3, fl, WH);
    rt_rect(x - 1, y + 7 + fl, 3, 2, MG);
    spr(SPV[0][SP_PLAYER], P.x, P.y, false);
    rt_rect(x - 1, y - 1, 2, 2, WH);
}

static void draw_play_world(void) {
    int stage = G.stage;
    if (bgtex) {
        int off = (int)fmodf(G.scroll, (float)bgH);
        SDL_Rect a = {0, off - bgH, PW, bgH}, b = {0, off, PW, bgH};
        SDL_RenderCopy(rt_ren, bgtex, NULL, &a);
        SDL_RenderCopy(rt_ren, bgtex, NULL, &b);
    } else rt_rect(0, 0, PW, PH, BK);
    if (stage <= 1) draw_stars(7, G.scroll, PW, PH, 0, 0);
    for (int i = 0; i < MAXIT; i++)
        if (items[i].alive) draw_item_icon((int)items[i].x, (int)items[i].y, items[i].kind, items[i].t);
    for (int i = 0; i < MAXE; i++) {
        En *e = &en[i];
        if (!e->alive || e->delay > 0) continue;
        int v = stage & 1;
        if (e->kind == K_ROCK) {
            SDL_Texture *tx = rock_tex((int)(e->rad + 0.5f));
            int s = (int)(e->rad + 0.5f) * 2 + 6;
            if (tx) { SDL_Rect d = {(int)e->x - s / 2, (int)e->y - s / 2, s, s}; SDL_RenderCopy(rt_ren, tx, NULL, &d); }
            if (e->flash > 0) rt_frame((int)e->x - (int)e->rad, (int)e->y - (int)e->rad, (int)e->rad * 2, (int)e->rad * 2, WH);
            continue;
        }
        static const int map[K_N] = {SP_SCOUT, SP_WEAVER, SP_DIVER, SP_GUNSHIP, SP_TURRET, SP_HEAVY, 0};
        spr(SPV[v][map[e->kind]], e->x, e->y, e->flash > 0);
    }
    if (B.active) draw_boss();
    draw_player();
    for (int i = 0; i < MAXPB; i++) {
        PB *b = &pb[i];
        if (!b->alive) continue;
        int x = (int)b->x, y = (int)b->y;
        if (b->kind == 1) { rt_rect(x - 1, y - 6, 3, 12, BK); rt_rect(x, y - 6, 1, 12, WH); rt_px(x - 1, y - 3, CY); rt_px(x + 1, y - 3, CY); }
        else if (b->kind == 2) { rt_rect(x - 2, y - 3, 4, 7, BK); rt_rect(x - 1, y - 2, 2, 5, MG); rt_px(x, y - 3, WH); rt_px(x, y + 3, CY); }
        else { rt_rect(x - 1, y - 4, 3, 8, BK); rt_rect(x, y - 3, 1, 6, WH); rt_px(x - 1, y - 2, CY); rt_px(x + 1, y - 2, CY); }
    }
    for (int i = 0; i < MAXEB; i++) {
        EB *b = &eb[i];
        if (!b->alive) continue;
        int x = (int)b->x, y = (int)b->y;
        if (b->big) { rt_rect(x - 3, y - 3, 7, 7, BK); rt_rect(x - 2, y - 2, 5, 5, MG); rt_rect(x - 1, y - 1, 3, 3, WH); }
        else { rt_rect(x - 2, y - 2, 5, 5, BK); rt_rect(x - 1, y - 1, 3, 3, MG); rt_px(x, y, WH); }
    }
    for (int i = 0; i < MAXFB; i++)
        if (fires[i].mt) draw_fire(fires[i].x, fires[i].y, fires[i].r, fires[i].t, fires[i].mt);
    for (int i = 0; i < MAXP; i++) {
        Part *p = &parts[i];
        if (p->life <= 0) continue;
        int c = p->life < 8 ? MG : p->col;
        if (p->sz > 1) rt_rect((int)p->x, (int)p->y, 2, 2, c); else rt_px((int)p->x, (int)p->y, c);
    }
    for (int i = 0; i < MAXPOP; i++)
        if (pops[i].t > 0) {
            int w = rt_text_w(pops[i].txt, 1), x = (int)pops[i].x - w / 2;
            if (x < 1) x = 1;
            if (x > PW - w - 1) x = PW - w - 1;
            rt_text_sh(pops[i].txt, x, (int)pops[i].y, (pops[i].t / 3) % 2 ? WH : CY);
        }
    if (G.flash > 0) {
        for (int y = 0; y < PH; y += 2) rt_rect(0, y + (G.flash & 1), PW, 1, WH);
        if (G.flash > 3) rt_rect(0, 0, PW, PH, WH);
    }
    /* boss bar */
    if (B.active && B.state >= 1) {
        rt_rect(14, 3, 164, 8, BK);
        rt_frame(14, 3, 164, 8, WH);
        int w = B.hp * 160 / B.maxhp;
        rt_rect(16, 5, w, 4, B.phase == 0 ? CY : B.phase == 1 ? MG : WH);
        rt_px(16 + 160 / 3, 4, WH); rt_px(16 + 160 * 2 / 3, 4, WH);
        rt_px(16 + 160 / 3, 8, WH); rt_px(16 + 160 * 2 / 3, 8, WH);
    }
    if (G.bossphase == 1 && G.state == S_PLAY) {
        bool on = (G.bosst / 10) % 2 == 0;
        for (int i = 0; i < 6; i++) { rt_rect(0, 62 + i * 2, PW, 1, on ? MG : BK); rt_rect(0, 118 + i * 2, PW, 1, on ? MG : BK); }
        rt_rect(0, 74, PW, 42, BK);
        rt_frame(0, 74, PW, 42, on ? WH : CY);
        if (on) {
            int w = rt_text_w("WARNING", 3);
            rt_text_ex("WARNING", (PW - w) / 2, 80, 3, WH, NULL);
        }
        int w = rt_text_w(BOSS_NAME[G.stage], 1);
        rt_text(BOSS_NAME[G.stage], (PW - w) / 2, 104, CY);
    }
}


static void draw_ship_icon(int x, int y) {
    rt_rect(x + 3, y, 2, 7, WH);
    rt_rect(x + 2, y + 3, 4, 3, CY);
    rt_rect(x, y + 5, 8, 2, CY);
    rt_px(x + 3, y + 5, MG); rt_px(x + 4, y + 5, MG);
}

static void draw_hud(void) {
    SDL_Rect l = {0, 0, 64, PH}, r = {256, 0, 64, PH};
    SDL_RenderCopy(rt_ren, paneltex, NULL, &l);
    SDL_RenderCopyEx(rt_ren, paneltex, NULL, &r, 0, NULL, SDL_FLIP_HORIZONTAL);
    char b[32];
    rt_text("SCORE", 1, 6, CY);
    snprintf(b, sizeof(b), "%07d", G.score > 9999999 ? 9999999 : G.score);
    rt_text(b, 1, 16, WH);
    rt_text("HI-SC", 1, 34, CY);
    snprintf(b, sizeof(b), "%07d", G.hi > 9999999 ? 9999999 : G.hi);
    rt_text(b, 1, 44, MG);
    rt_text("STAGE", 1, 64, CY);
    snprintf(b, sizeof(b), "%d/%d", G.stage + 1, NSTAGES);
    rt_text(b, 1, 74, WH);
    rt_text("CHAIN", 1, 96, CY);
    snprintf(b, sizeof(b), "X%d", mult());
    rt_text_ex(b, 1, 107, 2, G.chain >= 5 ? MG : WH, NULL);
    rt_frame(1, 125, 48, 6, CY);
    if (G.chain_t > 0) rt_rect(2, 126, G.chain_t * 46 / 120, 4, mult() > 1 ? MG : WH);
    snprintf(b, sizeof(b), "%d HITS", G.chain);
    rt_text(b, 1, 135, CY);
    rt_text("NOVA", 1, 170, MG);
    rt_text("LANCER", 1, 180, WH);

    rt_text("SHIPS", 268, 6, CY);
    draw_ship_icon(268, 17);
    snprintf(b, sizeof(b), "X%d", G.lives);
    rt_text(b, 282, 16, WH);
    rt_text("BOMBS", 268, 34, CY);
    for (int i = 0; i < G.bombs && i < 5; i++) { rt_disc(270 + i * 10, 52, 3, MG); rt_px(269 + i * 10, 51, WH); }
    rt_text("WEAPON", 268, 68, CY);
    rt_text(G.wtype == W_SPREAD ? "SPREAD" : G.wtype == W_LANCE ? "LANCE" : "SEEKER", 268, 78, MG);
    rt_text("LEVEL", 268, 92, CY);
    for (int i = 0; i < 4; i++) {
        rt_frame(268 + i * 13, 103, 11, 7, CY);
        if (i < G.wlevel) rt_rect(270 + i * 13, 105, 7, 3, WH);
    }
    if (G.wlevel >= 4) rt_text("MAX", 268, 113, WH);
    if (B.active && B.state >= 1) { rt_text("BOSS", 268, 140, MG); rt_text(G.stage == 3 ? "CORE" : BOSS_NAME[G.stage], 264, 150, CY); }
    if (rt_mute) rt_text("MUTE", 268, 188, MG);
}

static void draw_playfield_view(void) {
    int sx = 0, sy = 0;
    if (G.shake > 0) { sx = (int)(rt_rand() % 5) - 2; sy = (int)(rt_rand() % 5) - 2; if (G.shake < 3) sx = sy = 0; }
    SDL_Rect vp = {PX + sx, sy, PW, PH};
    SDL_RenderSetViewport(rt_ren, &vp);
    draw_play_world();
    SDL_RenderSetViewport(rt_ren, NULL);
}

#define LOGO_ROWS_N 7
static const int LOGO_ROWS[LOGO_ROWS_N] = {WH, WH, CY, CY, MG, MG, MG};

static void draw_title(void) {
    rt_cls(BK);
    float t = (float)G.st;
    draw_stars(7, (float)G.tick, 320, 200, 0, 0);
    int pg = G.title_page;
    char b[160];
    if (pg == 0) {
        const char *a = "NOVA", *c = "LANCER";
        rt_text_ex(a, 160 - rt_text_w(a, 4) / 2 + 3, 15 + 3, 4, BK, NULL);
        rt_text_ex(a, 160 - rt_text_w(a, 4) / 2, 15, 4, WH, LOGO_ROWS);
        rt_text_ex(c, 160 - rt_text_w(c, 4) / 2, 50, 4, WH, LOGO_ROWS);
        rt_rect(48, 84, 224, 1, MG);
        rt_rect(48, 87, 224, 1, CY);
        /* attract scene */
        int tt = G.st;
        float shx = 160 + 90 * sinf(tt * 0.021f);
        int p = tt % 300;
        for (int a2 = 0; a2 < 80; a2 += 10) {
            int ag = a2 + tt % 10;
            float bx = 160 + 90 * sinf((tt - ag) * 0.021f);
            rt_rect((int)bx, 152 - ag * 1, 1, 6, WH);
        }
        for (int i = 0; i < 5; i++) {
            float ex = 160 + (i - 2) * 30 + 50 * sinf(tt * 0.021f + 0.6f), ey = 118 + p * 0.1f + abs(i - 2) * -6.0f;
            int die = 190 + i * 9;
            if (p < die) spr(SPV[0][SP_SCOUT], ex, ey, false);
            else if (p < die + 20) draw_fire(ex, ey, 10, p - die, 20);
        }
        spr(SPV[0][SP_PLAYER], shx, 165, false);
        rt_rect((int)shx - 1, 173, 3, 2 + (tt / 2) % 3, WH);
        if ((tt / 30) % 2 == 0) rt_textc_sh("PRESS SPACE OR ENTER", 188, WH);
        snprintf(b, sizeof(b), "HI-SCORE %07d", rt_scores[0].score);
        rt_textc(b, 94, MG);
        rt_textc("A VINTAGE SHOOT-'EM-UP", 5, CY);
    } else if (pg == 1) {
        rt_box(16, 10, 288, 180, BK, CY);
        rt_textc_sh("HOW TO PLAY", 20, WH);
        rt_text("ARROWS/WASD", 32, 40, WH); rt_text("MOVE", 192, 40, CY);
        rt_text("SPACE/Z/X", 32, 52, WH);   rt_text("FIRE (HOLD)", 192, 52, CY);
        rt_text("B OR C", 32, 64, WH);      rt_text("SMART BOMB", 192, 64, CY);
        rt_text("P", 32, 76, WH);           rt_text("PAUSE", 192, 76, CY);
        rt_text("M", 32, 88, WH);           rt_text("MUTE", 192, 88, CY);
        rt_text("ESC", 32, 100, WH);        rt_text("QUIT TO TITLE", 192, 100, CY);
        rt_textc("QUICK KILLS BUILD A X8 CHAIN", 122, MG);
        rt_textc("GRAB CAPSULES TO POWER UP YOUR GUN", 136, WH);
        rt_textc("EXTRA SHIP AT 30000 AND 80000", 150, CY);
        rt_textc("SURVIVE 4 STAGES AND 4 BOSSES", 164, MG);
        rt_textc("GAMEPAD: STICK, A/X FIRE, Y BOMB", 176, CY);
    } else if (pg == 2) {
        rt_box(56, 10, 208, 180, BK, MG);
        rt_textc_sh("HIGH SCORES", 22, WH);
        for (int i = 0; i < RT_NSCORES; i++) {
            snprintf(b, sizeof(b), "%d. %s %07d  S%d", i + 1, rt_scores[i].name, rt_scores[i].score, rt_scores[i].level);
            rt_textc(b, 48 + i * 15, i == 0 ? WH : i % 2 ? CY : MG);
        }
        rt_textc("PRESS SPACE TO PLAY", 176, CY);
    } else {
        rt_box(16, 10, 288, 180, BK, CY);
        rt_textc_sh("POWER-UPS", 20, WH);
        static const int ks[6] = {D_POW, D_S, D_L, D_H, D_BOMB, D_1UP};
        static const char *ds[6] = {"POWER: GUN LEVEL UP", "SPREAD: WIDE FAN SHOT", "LANCE: PIERCING BOLTS",
                                    "SEEKER: HOMING MISSILES", "BOMB: CLEARS THE SCREEN", "EXTRA SHIP"};
        for (int i = 0; i < 6; i++) {
            draw_item_icon(40, 48 + i * 17, ks[i], G.tick);
            rt_text(ds[i], 58, 44 + i * 17, i % 2 ? CY : WH);
        }
        rt_textc("SAME TYPE AGAIN = LEVEL UP", 150, MG);
        rt_textc("DYING COSTS A GUN LEVEL", 164, CY);
    }
    (void)t;
    if (pg != 0) {
        for (int i = 0; i < 4; i++) rt_rect(148 + i * 8, 193, 5, 3, i == pg ? WH : CY);
    }
}

static void draw_card(void) {
    rt_cls(BK);
    draw_stars(7, (float)G.tick, 320, 200, 0, 0);
    rt_box(40, 40, 240, 110, BK, CY);
    char b[40];
    snprintf(b, sizeof(b), "STAGE %d", G.stage + 1);
    rt_text_ex(b, 160 - rt_text_w(b, 3) / 2, 56, 3, WH, LOGO_ROWS);
    int w = rt_text_w(STAGE_NAME[G.stage], 2);
    rt_text_ex(STAGE_NAME[G.stage], 160 - w / 2, 88, 2, MG, NULL);
    snprintf(b, sizeof(b), "GUARDIAN: %s", BOSS_NAME[G.stage]);
    rt_textc(b, 114, CY);
    if ((G.st / 15) % 2 == 0) rt_textc("GET READY", 130, WH);
}

static void draw_box_text(const char *a, const char *b2) {
    rt_box(72, 70, 176, 60, BK, WH);
    rt_textc_sh(a, 86, WH);
    rt_textc(b2, 106, CY);
}

static void draw_frame(void) {
    rt_cls(BK);
    switch (G.state) {
    case S_TITLE: draw_title(); return;
    case S_CARD: draw_card(); return;
    case S_ENTRY: {
        rt_cls(BK);
        draw_stars(7, (float)G.tick, 320, 200, 0, 0);
        rt_box(50, 20, 220, 160, BK, MG);
        rt_textc_sh("NEW HIGH SCORE!", 34, WH);
        char b[40];
        snprintf(b, sizeof(b), "SCORE %07d", G.score);
        rt_textc(b, 54, CY);
        snprintf(b, sizeof(b), "RANK %d OF %d", G.rank + 1, RT_NSCORES);
        rt_textc(b, 68, MG);
        rt_textc("ENTER YOUR INITIALS", 92, WH);
        for (int i = 0; i < 3; i++) {
            char ch[2] = {i < G.name_len ? G.name[i] : (i == G.name_len ? (char)('A' + G.letter) : '-'), 0};
            bool cur = i == G.name_len;
            rt_rect(112 + i * 36, 112, 28, 30, BK);
            rt_frame(112 + i * 36, 112, 28, 30, cur && (G.st / 10) % 2 ? WH : CY);
            rt_text_ex(ch, 118 + i * 36, 120, 3, cur ? MG : WH, NULL);
        }
        rt_textc("TYPE LETTERS, ENTER TO FINISH", 154, CY);
        return;
    }
    case S_WIN: {
        draw_stars(7, (float)G.tick * 2, 320, 200, 0, 0);
        for (int i = 0; i < MAXFB; i++) if (fires[i].mt) draw_fire(fires[i].x, fires[i].y, fires[i].r, fires[i].t, fires[i].mt);
        for (int i = 0; i < MAXP; i++) {
            Part *p = &parts[i];
            if (p->life > 0) rt_rect((int)p->x, (int)p->y, p->sz, p->sz, p->life < 8 ? MG : p->col);
        }
        rt_box(40, 50, 240, 100, BK, WH);
        rt_text_ex("VICTORY!", 160 - rt_text_w("VICTORY!", 3) / 2, 62, 3, WH, LOGO_ROWS);
        rt_textc("THE NOVA LANCER SAVED THE SECTOR", 98, CY);
        char b[40];
        snprintf(b, sizeof(b), "FINAL SCORE %07d", G.score);
        rt_textc(b, 114, MG);
        if ((G.st / 20) % 2 == 0 && G.st > 120) rt_textc("PRESS SPACE", 132, WH);
        spr(SPV[0][SP_PLAYER], 160 + 20 * sinf(G.tick * 0.05f), 175 - (G.st > 60 ? 0 : 0), false);
        return;
    }
    default: break;
    }
    draw_playfield_view();
    draw_hud();
    if (G.state == S_CLEAR) {
        rt_box(70, 50, 180, 100, BK, WH);
        rt_textc_sh("STAGE CLEAR!", 62, WH);
        char b[40];
        snprintf(b, sizeof(b), "%s DEFEATED", BOSS_NAME[G.stage]);
        rt_textc(b, 78, CY);
        snprintf(b, sizeof(b), "DESTROYED %d/%d", G.killed, G.spawned);
        rt_textc(b, 94, MG);
        snprintf(b, sizeof(b), "BONUS %d", G.bonus);
        rt_textc(b, 110, WH);
        if (G.bonus == 0 && (G.st / 20) % 2 == 0) rt_textc("PRESS SPACE", 128, CY);
    } else if (G.state == S_OVER) {
        rt_box(70, 70, 180, 60, BK, MG);
        rt_text_ex("GAME OVER", 160 - rt_text_w("GAME OVER", 2) / 2, 84, 2, WH, NULL);
        char b[40];
        snprintf(b, sizeof(b), "SCORE %07d", G.score);
        rt_textc(b, 108, CY);
    } else if (G.paused) {
        draw_box_text("PAUSED", "P TO RESUME");
    } else if (G.quitq) {
        draw_box_text("QUIT TO TITLE?", "Y = YES   N = NO");
    }
}

/* ================================================================== game flow */
static void start_stage(int s) {
    G.stage = s;
    memset(en, 0, sizeof(en)); memset(pb, 0, sizeof(pb)); memset(eb, 0, sizeof(eb));
    memset(items, 0, sizeof(items)); memset(parts, 0, sizeof(parts)); memset(fires, 0, sizeof(fires));
    memset(pops, 0, sizeof(pops)); memset(&B, 0, sizeof(B));
    G.stt = 0; G.script_i = 0; G.bossphase = 0; G.bosst = 0; G.scroll = 0;
    G.spawned = G.killed = 0; G.chain = G.chain_t = 0; G.shake = G.flash = 0;
    G.paused = G.quitq = false;
    bake_bg(s);
    player_reset_pos();
    P.dead = 0; P.inv = 120; P.fcd = 0;
    G.state = S_CARD; G.st = 0;
    tune_card();
}
static void new_game(int stage) {
    G.score = 0; G.lives = 3; G.bombs = 3; G.wlevel = 1; G.wtype = W_SPREAD; G.next_life = 0; G.deaths = 0; G.bombs_used = 0;
    G.hi = rt_scores[0].score;
    start_stage(stage);
}
static void goto_title(int page) {
    G.state = S_TITLE; G.st = 0; G.title_page = page; G.paused = G.quitq = false;
    if (page == 0) tune_title();
}
static void finish_game(void) {
    G.rank = rt_score_rank(G.score);
    if (G.rank >= 0) {
        G.state = S_ENTRY; G.st = 0; G.name_len = 0; G.letter = 0;
        memset(G.name, 0, sizeof(G.name));
    } else goto_title(2);
}
static void commit_entry(void) {
    if (G.name_len == 0) { strcpy(G.name, "???"); G.name_len = 3; }
    G.name[G.name_len] = 0;
    rt_score_insert(G.rank, G.name, G.score, G.stage + 1);
    if (!selftest) rt_scores_save();
    sfx_select();
    goto_title(2);
}

static bool menu_hit(void) {
    if (bot_mode) { bool r = bot_okp; bot_okp = false; return r; }
    return RT_HIT(ok) || (rt_held.fire && !rt_prev.fire);
}

static void update(void) {
    G.tick++;
    G.st++;
    if (G.state != S_PLAY) G.scroll += 0.5f;
    switch (G.state) {
    case S_TITLE:
        if (G.st > 600) { G.title_page = (G.title_page + 1) % 4; G.st = 0; }
        if (RT_HIT(r)) { G.title_page = (G.title_page + 1) % 4; G.st = 0; sfx_tick(); }
        else if (RT_HIT(l)) { G.title_page = (G.title_page + 3) % 4; G.st = 0; sfx_tick(); }
        else if (menu_hit()) { G.rank = -1; sfx_select(); new_game(0); }
        break;
    case S_CARD:
        if (G.st > 150 || (G.st > 30 && menu_hit())) { G.state = S_PLAY; G.st = 0; }
        break;
    case S_PLAY: {
        bool ps = rt_pad && SDL_GameControllerGetButton(rt_pad, SDL_CONTROLLER_BUTTON_START);
        if (ps && !G.pad_start_prev && !G.quitq) G.paused = !G.paused;
        G.pad_start_prev = ps;
        if (G.quitq || G.paused) break;
        update_play();
        break;
    }
    case S_CLEAR:
        update_fx();
        if (G.st > 40 && G.bonus > 0) {
            int s = G.bonus > 250 ? 250 : G.bonus;
            G.bonus -= s;
            add_score(s);
            if (G.st % 3 == 0) sfx_tick();
        } else if (G.bonus == 0 && G.st > 60 && menu_hit()) {
            if (G.stage + 1 >= NSTAGES) {
                G.state = S_WIN; G.st = 0;
                memset(parts, 0, sizeof(parts)); memset(fires, 0, sizeof(fires));
                tune_win();
            } else start_stage(G.stage + 1);
        }
        break;
    case S_OVER:
        update_fx();
        if (G.st > 90 && menu_hit()) finish_game();
        break;
    case S_ENTRY:
        if (RT_HIT(u)) { G.letter = (G.letter + 1) % 26; sfx_tick(); }
        if (RT_HIT(d)) { G.letter = (G.letter + 25) % 26; sfx_tick(); }
        if (rt_pad && SDL_GameControllerGetButton(rt_pad, SDL_CONTROLLER_BUTTON_A) && !rt_prev.jump && G.name_len < 3) {
            G.name[G.name_len++] = (char)('A' + G.letter);
            G.letter = 0;
            sfx_tick();
        }
        if (rt_pad && SDL_GameControllerGetButton(rt_pad, SDL_CONTROLLER_BUTTON_START) && G.st > 10) commit_entry();
        break;
    case S_WIN:
        if (G.st % 28 == 1) {
            float x = frnd(30, 290), y = frnd(15, 70);
            for (int i = 0; i < 3; i++) fireball(x + frnd(-6, 6), y + frnd(-6, 6), frnd(8, 14));
            burst(x, y, 50, 2.2f, 50);
            rt_tone(900, 200, 120, 1);
        }
        update_fx();
        if (G.st > 120 && menu_hit()) finish_game();
        break;
    }
}

static bool handle_key(SDL_Keycode k) {
    if (k == SDLK_m) { rt_mute = !rt_mute; return true; }
    switch (G.state) {
    case S_TITLE:
        if (k == SDLK_ESCAPE) return false;
        if (k == SDLK_F1) { G.title_page = 1; G.st = 0; }
        break;
    case S_PLAY:
        if (G.quitq) {
            if (k == SDLK_y) goto_title(0);
            else if (k == SDLK_n || k == SDLK_ESCAPE) G.quitq = false;
        } else if (k == SDLK_ESCAPE) G.quitq = true;
        else if (k == SDLK_p || k == SDLK_PAUSE) G.paused = !G.paused;
        break;
    case S_ENTRY:
        if (k >= SDLK_a && k <= SDLK_z && G.name_len < 3) { G.name[G.name_len++] = (char)('A' + (k - SDLK_a)); sfx_tick(); }
        else if (k == SDLK_BACKSPACE && G.name_len > 0) G.name[--G.name_len] = 0;
        else if ((k == SDLK_RETURN || k == SDLK_KP_ENTER) && G.st > 10) commit_entry();
        break;
    default:
        if (k == SDLK_ESCAPE) goto_title(0);
        break;
    }
    return true;
}

/* ================================================================== self-test bot */
static void bot_think(void) {
    Ctl c = {0, 0, true, false};
    bot_okp = (G.state != S_PLAY) && (G.tick % 15 == 0);
    if (G.state != S_PLAY || P.dead > 0) { bot_c = c; return; }
    /* where to aim */
    float tx = PW / 2.0f, best_t = 1e9f;
    for (int i = 0; i < MAXE; i++) {
        En *e = &en[i];
        if (!e->alive || e->delay > 0 || e->y < -5 || e->y > P.y - 10 || e->kind == K_ROCK) continue;
        float d = fabsf(e->x - P.x) + (P.y - e->y) * 0.25f;
        if (d < best_t) { best_t = d; tx = e->x; }
    }
    if (B.active && B.state == 1) tx = B.x;
    float ty = B.active ? PH - 50.0f : PH - 55.0f;
    /* nearest item */
    float ix = 0, iy = 0, bi = 1e9f;
    for (int i = 0; i < MAXIT; i++)
        if (items[i].alive) {
            float d = fabsf(items[i].x - P.x) + fabsf(items[i].y - P.y) * 0.5f;
            if (d < bi) { bi = d; ix = items[i].x; iy = items[i].y; }
        }
    float bestsc = -1e30f, worst_imm = 0;
    int bdx = 0, bdy = 0;
    for (int dx = -1; dx <= 1; dx++)
        for (int dy = -1; dy <= 1; dy++) {
            float sp = (dx && dy) ? 2.3f * 0.7071f : 2.3f, x = P.x, y = P.y, sc = 0;
            for (int k = 1; k <= 16; k++) {
                x += dx * sp; y += dy * sp;
                if (x < 8) x = 8;
                if (x > PW - 8) x = PW - 8;
                if (y < 14) y = 14;
                if (y > PH - 10) y = PH - 10;
                float wk = 1.0f / (1.0f + k * 0.12f);
                for (int i = 0; i < MAXEB; i++) {
                    EB *b = &eb[i];
                    if (!b->alive) continue;
                    float ddx = b->x + b->vx * k - x, ddy = b->y + b->vy * k - y, d2 = ddx * ddx + ddy * ddy;
                    if (d2 < 289) {
                        float pen = (289 - d2) / 289;
                        sc -= pen * pen * 500 * wk;
                        if (d2 < 81) sc -= 4000 * wk;
                    }
                }
                for (int i = 0; i < MAXE; i++) {
                    En *e = &en[i];
                    if (!e->alive || (e->delay > 0 && e->delay > k)) continue;
                    float ex = e->x + e->avx * k, ey = e->y + e->avy * k;
                    float hw = (e->kind == K_ROCK ? e->rad : e->w / 2.0f) + 9, hh = (e->kind == K_ROCK ? e->rad : e->h / 2.0f) + 9;
                    if (fabsf(ex - x) < hw && fabsf(ey - y) < hh) sc -= 3000 * wk;
                }
                if (B.active && B.state >= 1 && fabsf(B.x - x) < B.w / 2.0f + 9 && fabsf(B.y - y) < B.h / 2.0f + 9) sc -= 3000 * wk;
                if (k <= 6 && sc < worst_imm) worst_imm = sc;
            }
            sc -= fabsf(x - tx) * 0.5f;
            sc -= fabsf(y - ty) * 0.08f;
            if (bi < 110) sc -= (fabsf(x - ix) + fabsf(y - iy)) * 1.2f;
            if (x < 20) sc -= (20 - x) * 3;
            if (x > PW - 20) sc -= (x - (PW - 20)) * 3;
            if (!dx && !dy) sc += 8;
            if (sc > bestsc) { bestsc = sc; bdx = dx; bdy = dy; }
        }
    c.dx = bdx; c.dy = bdy;
    if (getenv("NL_IDLE")) { c.dx = c.dy = 0; bestsc = 0; }
    if (bestsc < -3500 && G.bombs > 0 && P.inv == 0) c.bomb = true;
    bot_c = c;
}

typedef struct { bool cleared; int deaths, score, frames; } StageRes;

static int run_selftest(const char *dir) {
    char path[1200];
    int fails = 0;
    StageRes res[NSTAGES];
    memset(res, 0, sizeof(res));
    bool shot_mid[NSTAGES] = {0}, shot_boss[NSTAGES] = {0}, shot_warn[NSTAGES] = {0}, shot_bomb = false, shot_clear = false;
    bot_mode = true;
    rt_seed = 4242;
#define SHOT(name) do { draw_frame(); snprintf(path, sizeof(path), "%s/%s.bmp", dir, name); rt_shot(path); } while (0)
    for (int pg = 0; pg < 4; pg++) {
        goto_title(pg);
        G.st = 90;
        G.tick = 90;
        char n[16];
        snprintf(n, sizeof(n), "title%d", pg);
        SHOT(n);
    }
    new_game(0);
    G.st = 30;
    SHOT("card");
    int stage = 0, frames = 0, total = 0;
    int deaths0 = 0;
    bool done = false;
    while (!done && total < 120000) {
        bot_think();
        update();
        frames++; total++;
        char n[40];
        if (G.state == S_PLAY) {
            if (G.stt == 700 && !shot_mid[G.stage]) {
                shot_mid[G.stage] = true;
                snprintf(n, sizeof(n), "stage%d_play", G.stage + 1);
                SHOT(n);
            }
            if (G.bossphase == 1 && G.bosst == 40 && !shot_warn[G.stage]) {
                shot_warn[G.stage] = true;
                snprintf(n, sizeof(n), "stage%d_warning", G.stage + 1);
                SHOT(n);
            }
            if (B.active && B.state == 1 && B.phase == 1 && B.t == 70 && !shot_boss[G.stage]) {
                shot_boss[G.stage] = true;
                snprintf(n, sizeof(n), "stage%d_boss", G.stage + 1);
                SHOT(n);
            }
            if (G.flash == 5 && !shot_bomb) { shot_bomb = true; SHOT("bomb"); }
            if (G.stt == 1200 && G.stage == 0) { G.paused = true; SHOT("pause"); G.paused = false; G.quitq = true; SHOT("quit"); G.quitq = false; }
        }
        if (G.state == S_CLEAR && G.st == 70 && !res[stage].cleared) {
            res[stage].cleared = true;
            res[stage].deaths = G.deaths - deaths0;
            res[stage].score = G.score;
            res[stage].frames = frames;
            if (!shot_clear) { shot_clear = true; SHOT("clear"); }
        }
        if (G.state == S_OVER && G.st == 100) { /* bot died out: report and continue with a fresh run */
            res[stage].deaths = G.deaths - deaths0;
            res[stage].score = G.score;
            res[stage].frames = frames;
            printf("  stage %d: game over\n", stage + 1);
            SHOT("over");
            if (stage + 1 >= NSTAGES) break;
            stage++; frames = 0;
            new_game(stage);
            deaths0 = 0;
        }
        if (G.state == S_CARD && G.stage != stage && res[stage].cleared) { stage = G.stage; frames = 0; deaths0 = G.deaths; }
        if (frames > 20000) {
            printf("  stage %d: timeout\n", stage + 1);
            if (stage + 1 >= NSTAGES) break;
            stage++; frames = 0;
            new_game(stage);
            deaths0 = 0;
        }
        if (G.state == S_WIN && G.st >= 130) done = true;
    }
    if (G.state == S_WIN) SHOT("win");
    for (int s = 0; s < NSTAGES; s++) {
        printf("stage %d %-12s cleared=%s deaths=%d score=%d frames=%d\n", s + 1, STAGE_NAME[s],
               res[s].cleared ? "yes" : "NO", res[s].deaths, res[s].score, res[s].frames);
        if (!res[s].cleared) fails++;
    }
    bot_mode = false;
    /* static screens */
    G.score = 123456; G.rank = 2; G.state = S_OVER; G.st = 100;
    new_game(2);
    G.state = S_OVER; G.st = 100; G.score = 123456;
    SHOT("gameover");
    G.state = S_ENTRY; G.name_len = 2; strcpy(G.name, "NOV"); G.letter = 11; G.st = 15;
    SHOT("entry");
    /* all weapons, max level, mid-fight screenshots */
    new_game(1);
    G.state = S_PLAY; G.wlevel = 4;
    for (int w = 0; w < 3; w++) {
        G.wtype = w;
        for (int f = 0; f < 200; f++) { bot_mode = true; bot_think(); bot_mode = false; bot_mode = true; update(); bot_mode = false; }
        char n[24];
        snprintf(n, sizeof(n), "weapon%d", w);
        SHOT(n);
    }
    printf("bombs used in last run: %d\n", G.bombs_used);
    printf("selftest: %d of %d stages cleared by the bot (total %d frames)\n", NSTAGES - fails, NSTAGES, total);
    return fails ? 1 : 0;
}

/* ================================================================== main */
static const RtScore DEFAULT_SCORES[RT_NSCORES] = {
    {"NOV", 120000, 4}, {"LAN", 90000, 4}, {"CER", 65000, 3}, {"ACE", 45000, 3},
    {"ZAP", 30000, 2},  {"VEX", 20000, 2}, {"ORB", 12000, 1}, {"PIP", 6000, 1},
};

int main(int argc, char **argv) {
    const char *shotdir = NULL;
    int start = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--selftest") && i + 1 < argc) shotdir = argv[++i];
        else if (!strcmp(argv[i], "--stage") && i + 1 < argc) start = atoi(argv[++i]) - 1;
    }
    if (start < 0 || start >= NSTAGES) start = 0;
    selftest = shotdir != NULL;
    if (!rt_init("Nova Lancer")) return 1;
    load_sprites();
    bake_panel();
    memcpy(rt_scores, DEFAULT_SCORES, sizeof(rt_scores));
    G.rank = -1;
    if (selftest) return run_selftest(shotdir);

    rt_audio_init();
    rt_scores_load("nova-lancer", DEFAULT_SCORES);
    for (int i = 0; i < SDL_NumJoysticks() && !rt_pad; i++)
        if (SDL_IsGameController(i)) rt_pad = SDL_GameControllerOpen(i);
    if (start > 0) new_game(start);
    else goto_title(0);

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
            } else if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_FOCUS_LOST && G.state == S_PLAY) {
                G.paused = true;
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
