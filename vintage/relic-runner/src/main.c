/*
 * Relic Runner - an EGA-style DOS-era platformer.
 * Native Linux, C11 + SDL2. Everything (art, font, PC-speaker sound) is generated in code.
 *
 * Rook Rivet must grab the Sunstone in each of six vaults, then reach the vault door.
 * Gems for points, stomp spiders and bats, a pistol for fire totems, a jetpack for the big gaps.
 *
 *   ./relic-runner               play
 *   ./relic-runner --level N     start at level N (1-6)
 *   ./relic-runner --selftest D  headless: prove every level is completable, write screenshots to D
 */
#include "retro.h"

#define TS 16
#define ROWS 10
#define MAXW 128
#define PLAY_Y 20
#define PW 10
#define PH 14
#define JUMP_V -5.7f
#define FUEL_MAX 1500
#define NLEVELS 6
#define EXTRA_LIFE 20000

/* ================================================================== levels
 *  #  brick       R rock        = girder      - one-way ledge    ? bonus block
 *  ^  fire        ~ water       * thorns      1 2 3 4 gems       K sunstone
 *  D  vault door  J jetpack     G pistol      L extra life       P start
 *  S  spider      B bat         F fire totem  t torch (decor)    v vine (decor)
 */
typedef struct { const char *name; int theme; const char *row[ROWS]; } LevelDef;

static const LevelDef LEVELS[NLEVELS] = {
    {"ENTRANCE HALL", 0, {
        "################" "################" "################" "################",
        "#..............." "................" "................" "...............#",
        "#..............." "..........2.2.2." "................" "........K......#",
        "#..............." "..........=====." "................" "......====.....#",
        "#..t............" "................" "................" "..t............#",
        "#....111........" ".....1.1.1......" "........B......." "...............#",
        "#....???........" ".....=====......" "................" "..###..........#",
        "#..............." "................" ".....22........." "..###..........#",
        "#.P............." "....S..........." ".....RR........." "..###.....S..D.#",
        "###########^^###" "################" "##~~~~~~~~######" "################",
    }},
    {"SUNKEN CRYPT", 1, {
        "################" "################" "################" "################" "################",
        "#..............." "................" "................" "................" "...............#",
        "#..............." "................" "..........K....." "................" "...............#",
        "#..............." "................" ".........===...." "................" "...............#",
        "#..............." "................" "................" "................" "...............#",
        "#..1.1.....G...." ".....2.2.2......" "....B..........." "................" "...............#",
        "#.........----.." "...-------......" "......===......." ".......1........" "...............#",
        "#.....t........." "................" "................" "..----...----..." "...............#",
        "#.P............." "......F........." "..S............." "................" "......F......D.#",
        "################" "############~~~#" "################" "#~~~~~~~~~~~~~~#" "################",
    }},
    {"JUNGLE SHRINE", 2, {
        "################" "################" "################" "################" "################",
        "#...v.....v....." "....v......v...." "..v......v......" "....v.......v..." "...............#",
        "#..............." "................" "................" "........K......." "...............#",
        "#..............." "................" "................" ".......###......" "...............#",
        "#..............." "........B......." "................" "................" "...............#",
        "#...????........" "..1...1...1...1." "..1...1...1....." "................" "...............#",
        "#..............." "................" "......B........." "....###........." "...............#",
        "#....t.........." "................" "................" "................" "...............#",
        "#.P.........J..." "................" "................" "S..............." "...F.........D.#",
        "################" "##**************" "*************###" "################" "#######^^#######",
    }},
    {"LAVA FORGE", 3, {
        "################" "################" "################" "################" "################" "################",
        "#..............." "................" "................" "................" "................" "...............#",
        "#..............." "................" "................" "........#......." "................" "...............#",
        "#..............." "................" "................" "........#......." "................" "...............#",
        "#..........2...." "................" ".......B........" "........#......." "................" "...............#",
        "#.........===..." "...2....2....2.." "....??.?........" "........#......." "................" "...........D...#",
        "#..............." "...RR...RR...RR." "................" "........#......." "....----........" ".......#########",
        "#......##......." "...RR...RR...RR." "................" "........#......." "................" ".......#########",
        "#.P............." "...RR...RR...RR." "..........F....." "..J.....#...K..." "S.........S....." "...F...#########",
        "################" "##^RR^^^RR^^^RR^" "################" "################" "######^^^^######" "################",
    }},
    {"CRYSTAL CAVERN", 4, {
        "################" "################" "################" "################" "################",
        "#..............." "................" "....############" "####............" "...............#",
        "#..............." "................" "....############" "####............" "...............#",
        "#.......1.1....." "..........B....." "....############" "####......K....." "....B..........#",
        "#......-----...." "................" "....############" "####.....---...." "...............#",
        "#..............." "...2...2...2...." "................" "................" "...............#",
        "#..............." "...==..==..==..." "......?.?.?....." "................" ".........B.....#",
        "#..----........." "................" "................" ".....---........" "...............#",
        "#.P.......G....." "................" "...S.......S...." "................" "..F.........D..#",
        "################" "##************##" "################" "######~~~~~~####" "################",
    }},
    {"THE SUN VAULT", 5, {
        "################" "################" "################" "################" "################" "################",
        "#..............." "................" "................" "......R........." "..R............." "...............#",
        "#..............." "................" "................" "......R.....1..." "..R.....1......." ".......K.......#",
        "#..............." ".....B.........." "................" "......R........." "..R............." "......===......#",
        "#.....L........." "................" "................" "......R.....R..." "..R.....R......." "...............#",
        "#....???........" "..2....2....2..." "................" "......R.....R..." "..R.....R......." "...............#",
        "#..............." "..RR...RR...RR.." "....?..?..?....." "............R..." "........R......." "...###.........#",
        "#....t.........." "..RR...RR...RR.." "................" "......1.....R..." "..1.....R......." "...###.........#",
        "#.P........G...." "..RR...RR...RR.." "..S......S....J." "............R..." "........R......." "...###...S..D..#",
        "################" "#^RR^^^RR^^^RR^^" "################" "#^^^^^^^^^^^R^^^" "^^^^^^^^R^^^^^^#" "################",
    }},
};

typedef struct { int back, mid, hi, mortar, rhi, rlo; } Theme;
static const Theme THEMES[6] = {
    {C_DGRAY, C_BROWN, C_LRED, C_BLACK, C_LGRAY, C_DGRAY},     /* mine   */
    {C_BLUE, C_LGRAY, C_WHITE, C_DGRAY, C_LBLUE, C_BLUE},      /* crypt  */
    {C_GREEN, C_GREEN, C_LGREEN, C_BLACK, C_BROWN, C_DGRAY},   /* jungle */
    {C_RED, C_RED, C_LRED, C_BLACK, C_LGRAY, C_DGRAY},         /* forge  */
    {C_BLUE, C_CYAN, C_LCYAN, C_BLUE, C_WHITE, C_LCYAN},       /* ice    */
    {C_MAGENTA, C_BROWN, C_YELLOW, C_BLACK, C_LMAGENTA, C_MAGENTA}, /* gold */
};

typedef struct { int w, sx, sy, gems; char t[ROWS][MAXW]; } Level;

static bool ch_solid(char c) { return c == '#' || c == 'R' || c == '=' || c == '?' || c == 'u'; }
static bool ch_hazard(char c) { return c == '^' || c == '~' || c == '*' || c == '!'; }
static bool ch_gem(char c) { return c >= '1' && c <= '4'; }

static char tget(const Level *L, int tx, int ty) {
    if (tx < 0 || tx >= L->w || ty < 0) return '#';
    if (ty >= ROWS) return '.';
    return L->t[ty][tx];
}
static bool tsolid(const Level *L, int tx, int ty) { return ch_solid(tget(L, tx, ty)); }

/* ================================================================== physics (shared by game and solver) */
typedef struct { float x, y, vx, vy; bool ground, jet; int coyote, headx, heady; } Body;
typedef struct { int dx, dy; bool jump_held, jump_hit; } Ctl;

static bool box_solid(const Level *L, float x, float y, float w, float h) {
    int x0 = (int)floorf(x / TS), x1 = (int)floorf((x + w - 0.01f) / TS);
    int y0 = (int)floorf(y / TS), y1 = (int)floorf((y + h - 0.01f) / TS);
    for (int ty = y0; ty <= y1; ty++)
        for (int tx = x0; tx <= x1; tx++)
            if (tsolid(L, tx, ty)) return true;
    return false;
}
static bool box_hazard(const Level *L, float x, float y) {
    int x0 = (int)floorf((x + 2) / TS), x1 = (int)floorf((x + PW - 2.01f) / TS);
    int y0 = (int)floorf((y + 3) / TS), y1 = (int)floorf((y + PH - 2.01f) / TS);
    for (int ty = y0; ty <= y1; ty++)
        for (int tx = x0; tx <= x1; tx++)
            if (ch_hazard(tget(L, tx, ty))) return true;
    return false;
}

static void phys_step(Body *b, const Ctl *c, const Level *L) {
    b->headx = -1;
    if (b->jet) {
        b->vx += (c->dx * 1.6f - b->vx) * 0.2f;
        b->vy += (c->dy * 1.6f - b->vy) * 0.2f;
    } else {
        float acc = b->ground ? 0.25f : 0.18f, mx = 1.75f;
        if (c->dx) {
            if (b->ground && c->dx * b->vx < 0) b->vx += c->dx * 0.4f;
            else b->vx += c->dx * acc;
            if (b->vx > mx) b->vx = mx;
            if (b->vx < -mx) b->vx = -mx;
        } else if (b->ground) {
            b->vx = fabsf(b->vx) <= 0.3f ? 0 : b->vx - copysignf(0.3f, b->vx);
        }
        if (c->jump_hit && (b->ground || b->coyote > 0)) {
            b->vy = JUMP_V;
            b->ground = false;
            b->coyote = 0;
        }
        b->vy += b->vy < 0 ? (c->jump_held ? 0.28f : 0.6f) : 0.4f;
        if (b->vy > 4.5f) b->vy = 4.5f;
    }
    /* horizontal */
    b->x += b->vx;
    if (box_solid(L, b->x, b->y, PW, PH)) {
        if (b->vx > 0) b->x = floorf((b->x + PW - 0.01f) / TS) * TS - PW;
        else b->x = (floorf(b->x / TS) + 1) * TS;
        b->vx = 0;
    }
    /* vertical */
    float oldbot = b->y + PH;
    b->y += b->vy;
    b->ground = false;
    int x0 = (int)floorf(b->x / TS), x1 = (int)floorf((b->x + PW - 0.01f) / TS);
    if (b->vy > 0) {
        int row = (int)floorf((b->y + PH - 0.01f) / TS);
        bool land = false;
        for (int tx = x0; tx <= x1; tx++) {
            char ch = tget(L, tx, row);
            if (ch_solid(ch) || (ch == '-' && oldbot <= row * TS + 0.01f)) land = true;
        }
        if (land) { b->y = (float)(row * TS - PH); b->vy = 0; b->ground = true; }
    } else if (b->vy < 0) {
        int row = (int)floorf(b->y / TS);
        bool hit = false;
        for (int tx = x0; tx <= x1; tx++) if (tsolid(L, tx, row)) hit = true;
        if (hit) {
            b->y = (float)((row + 1) * TS);
            b->vy = 0;
            int cx = (int)floorf((b->x + PW / 2.0f) / TS);
            b->headx = tsolid(L, cx, row) ? cx : (tsolid(L, x0, row) ? x0 : x1);
            b->heady = row;
        }
    }
    if (b->ground) b->coyote = 6;
    else if (b->coyote > 0) b->coyote--;
}

/* ================================================================== sprites */
static const char *SPR_STAND[16] = {
    "................", ".....6666.......", "....666666......", "....444444......",
    "..6666666666....", ".....ggggg......", ".....ggg0g......", ".....ggggg......",
    "....aaaaaa......", "...aa2aa2aa.....", "...g.aaaa.g.....", ".....6666.......",
    ".....11.11......", ".....11.11......", ".....88.88......", "....888.888.....",
};
static const char *SPR_WALK1[16] = {
    "................", ".....6666.......", "....666666......", "....444444......",
    "..6666666666....", ".....ggggg......", ".....ggg0g......", ".....ggggg......",
    "....aaaaaa......", "...aa2aa2aa.....", "...g.aaaa.g.....", ".....6666.......",
    "....11..11......", "...11....11.....", "...88....88.....", "..888....888....",
};
static const char *SPR_WALK2[16] = {
    "................", ".....6666.......", "....666666......", "....444444......",
    "..6666666666....", ".....ggggg......", ".....ggg0g......", ".....ggggg......",
    "....aaaaaa......", "....a2aa2a......", "....gaaaag......", ".....6666.......",
    "......111.......", "......11........", "......88........", ".....888........",
};
static const char *SPR_JUMP[16] = {
    "................", ".....6666.......", "....666666......", "....444444......",
    "..6666666666....", ".....ggggg......", ".....ggg0g......", "..g..ggggg.g....",
    "..aaaaaaaaaa....", "....a2aa2a......", ".....aaaa.......", ".....6666.......",
    "....11...11.....", "...11.....88....", "...88.....888...", "..888...........",
};
static const char *SPR_SPIDER1[16] = {
    "................", "................", "................", "................",
    "................", "................", "......5555......", "....55dddd55....",
    "...555dddd555...", "...55c5555c55...", "..5.55555555.5..", ".5..5.5..5.5..5.",
    ".5.5..5..5..5.5.", "5..5..5..5..5..5", "5.....5..5.....5", "................",
};
static const char *SPR_SPIDER2[16] = {
    "................", "................", "................", "................",
    "................", "................", "......5555......", "....55dddd55....",
    "...555dddd555...", "...55c5555c55...", "..5.55555555.5..", "..5.5.5..5.5.5..",
    ".5..5..55..5..5.", ".5.5...55...5.5.", "5..5........5..5", "................",
};
static const char *SPR_BAT1[16] = {
    "................", "8..............8", "88............88", "888....77....888",
    "8888..7777..8888", "88888.c77c.88888", ".88888777788888.", "..888887788888..",
    "....88.77.88....", ".......77.......", "................", "................",
    "................", "................", "................", "................",
};
static const char *SPR_BAT2[16] = {
    "................", "................", "................", ".......77.......",
    "......7777......", "......c77c......", "...8888778888...", "..888887788888..",
    ".888888778888888", "888888.77.888888", "88.....77.....88", "8..............8",
    "................", "................", "................", "................",
};
static const char *SPR_TOTEM[16] = {
    "....77777777....", "...7777777777...", "..778888888877..", "..78cc7887cc87..",
    "..78ce7887ec87..", "..788888888887..", "..777788887777..", "...7700000077...",
    "...7707070777...", "....77777777....", "....78888887....", "....78777787....",
    "....78888887....", "....78777787....", "...7788888877...", "..777777777777..",
};
static const char *SPR_DIAMOND[16] = {
    "................", "................", "................", "................",
    "....99999991....", "...9ff99999911..", "..9999999999911.", "...99999999911..",
    "....999999911...", ".....9999911....", "......99911.....", ".......911......",
    "........1.......", "................", "................", "................",
};
static const char *SPR_RUBY[16] = {
    "................", "................", "................", "................",
    "......cccc......", "....ccfccccc....", "...ccfcccccc4...", "...cccccccc44...",
    "...ccccccc444...", "....cccc4444....", "......4444......", "................",
    "................", "................", "................", "................",
};
static const char *SPR_CROWN[16] = {
    "................", "................", "................", "................",
    "...e....e....e..", "...ee..eee..ee..", "...eee.eee.eee..", "...eeeeeeeeeee..",
    "...ecee9eeecee..", "...eeeeeeeeeee..", "...66666666666..", "................",
    "................", "................", "................", "................",
};
static const char *SPR_ORB[16] = {
    "................", "................", "................", "......dddd......",
    "....dddddddd....", "...ddffdddddd...", "...dfdddddddd...", "..dddddddddddd..",
    "..ddddddddddd5..", "..dddddddddd55..", "...ddddddd555...", "....dddd5555....",
    "......5555......", "................", "................", "................",
};
static const char *SPR_JETPACK[16] = {
    "................", "................", "....77....77....", "...7ff7..7ff7...",
    "...7777887777...", "...7777887777...", "...7777887777...", "...7777887777...",
    "...7777887777...", "...8888888888...", "....44....44....", "....cc....cc....",
    "................", "................", "................", "................",
};
static const char *SPR_GUN[16] = {
    "................", "................", "................", "................",
    "................", "...777777777f...", "...7888888887...", "...7777777777...",
    "...6667.........", "...666..........", "...666..........", "................",
    "................", "................", "................", "................",
};

enum { SP_STAND, SP_WALK1, SP_WALK2, SP_JUMP, SP_SPIDER1, SP_SPIDER2, SP_BAT1, SP_BAT2, SP_TOTEM,
       SP_DIAMOND, SP_RUBY, SP_CROWN, SP_ORB, SP_JETPACK, SP_GUN, SP_N };
static RtSpr SPR[SP_N];

static void load_sprites(void) {
    const char **src[SP_N] = {SPR_STAND, SPR_WALK1, SPR_WALK2, SPR_JUMP, SPR_SPIDER1, SPR_SPIDER2, SPR_BAT1,
                              SPR_BAT2, SPR_TOTEM, SPR_DIAMOND, SPR_RUBY, SPR_CROWN, SPR_ORB, SPR_JETPACK, SPR_GUN};
    for (int i = 0; i < SP_N; i++) SPR[i] = rt_sprite(src[i], 16);
}

/* ================================================================== baked tile atlas */
enum { A_BACK, A_BRICK, A_ROCK, A_GIRDER, A_LEDGE, A_BONUS1, A_BONUS2, A_USED, A_THORN, A_N };
static SDL_Texture *atlas;

static void bake_atlas(int theme) {
    const Theme *T = &THEMES[theme];
    if (!atlas) {
        atlas = SDL_CreateTexture(rt_ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, TS * A_N, TS);
        if (!atlas) return;
        SDL_SetTextureBlendMode(atlas, SDL_BLENDMODE_BLEND);
    }
    SDL_SetRenderTarget(rt_ren, atlas);
    SDL_SetRenderDrawColor(rt_ren, 0, 0, 0, 0);
    SDL_RenderClear(rt_ren);
    int o;
    /* back wall: dotted mortar lines */
    o = A_BACK * TS;
    for (int i = 0; i < 16; i += 2) {
        rt_px(o + i, 7, T->back);
        rt_px(o + i + 1, 15, T->back);
    }
    for (int i = 0; i < 7; i += 2) {
        rt_px(o + 15, i, T->back);
        rt_px(o + 7, 8 + i, T->back);
    }
    /* brick */
    o = A_BRICK * TS;
    rt_rect(o, 0, 16, 16, T->mid);
    rt_rect(o, 7, 16, 1, T->mortar);
    rt_rect(o, 15, 16, 1, T->mortar);
    rt_rect(o + 15, 0, 1, 7, T->mortar);
    rt_rect(o + 7, 8, 1, 7, T->mortar);
    rt_rect(o, 0, 15, 1, T->hi);
    rt_rect(o, 0, 1, 7, T->hi);
    rt_rect(o, 8, 7, 1, T->hi);
    rt_rect(o + 8, 8, 7, 1, T->hi);
    rt_rect(o + 8, 8, 1, 7, T->hi);
    rt_px(o + 4, 3, T->mortar);
    rt_px(o + 11, 12, T->mortar);
    rt_px(o + 3, 11, T->hi);
    /* rock */
    o = A_ROCK * TS;
    rt_rect(o, 0, 16, 16, T->rlo);
    rt_rect(o + 1, 1, 9, 6, T->rhi);
    rt_rect(o + 9, 8, 6, 6, T->rhi);
    rt_rect(o + 1, 9, 6, 5, T->rhi);
    rt_rect(o + 11, 1, 4, 5, T->rhi);
    rt_px(o + 3, 3, C_WHITE);
    rt_px(o + 11, 10, C_WHITE);
    rt_rect(o, 15, 16, 1, C_BLACK);
    rt_rect(o + 15, 0, 1, 16, C_BLACK);
    /* girder */
    o = A_GIRDER * TS;
    rt_rect(o, 0, 16, 3, C_LGRAY);
    rt_rect(o, 0, 16, 1, C_WHITE);
    rt_rect(o, 13, 16, 3, C_LGRAY);
    rt_rect(o, 15, 16, 1, C_DGRAY);
    rt_line(o, 3, o + 8, 12, C_DGRAY);
    rt_line(o + 8, 3, o + 15, 11, C_DGRAY);
    rt_line(o + 8, 3, o, 11, C_DGRAY);
    rt_line(o + 15, 4, o + 8, 12, C_DGRAY);
    rt_px(o + 2, 1, C_DGRAY);
    rt_px(o + 13, 1, C_DGRAY);
    rt_px(o + 2, 14, C_DGRAY);
    rt_px(o + 13, 14, C_DGRAY);
    /* one-way ledge */
    o = A_LEDGE * TS;
    rt_rect(o, 0, 16, 5, C_BROWN);
    rt_rect(o, 0, 16, 1, C_YELLOW);
    rt_rect(o, 4, 16, 1, C_BLACK);
    rt_px(o + 3, 2, C_BLACK);
    rt_px(o + 12, 2, C_BLACK);
    rt_line(o + 1, 5, o + 4, 8, C_DGRAY);
    rt_line(o + 14, 5, o + 11, 8, C_DGRAY);
    /* bonus blocks (two frames for palette cycling) */
    for (int k = 0; k < 2; k++) {
        o = (A_BONUS1 + k) * TS;
        int fill = k ? C_BROWN : C_YELLOW, ink = k ? C_YELLOW : C_BROWN;
        rt_rect(o, 0, 16, 16, fill);
        rt_frame(o, 0, 16, 16, C_BLACK);
        rt_rect(o + 1, 1, 14, 1, C_WHITE);
        rt_px(o + 2, 2, C_BLACK);
        rt_px(o + 13, 2, C_BLACK);
        rt_px(o + 2, 13, C_BLACK);
        rt_px(o + 13, 13, C_BLACK);
        rt_text("?", o + 3, 4, ink);
    }
    o = A_USED * TS;
    rt_rect(o, 0, 16, 16, C_BROWN);
    rt_frame(o, 0, 16, 16, C_BLACK);
    rt_rect(o + 1, 14, 14, 1, C_RED);
    rt_px(o + 2, 2, C_BLACK);
    rt_px(o + 13, 2, C_BLACK);
    rt_px(o + 2, 13, C_BLACK);
    rt_px(o + 13, 13, C_BLACK);
    /* thorns */
    o = A_THORN * TS;
    for (int s = 0; s < 4; s++) {
        int px = o + 2 + s * 4, top = 4 + (s & 1) * 3;
        for (int y = top; y < 16; y++) {
            int half = (y - top) / 4;
            rt_rect(px - half, y, 1 + half * 2, 1, y < top + 2 ? C_WHITE : C_LGRAY);
        }
    }
    rt_rect(o, 13, 16, 3, C_GREEN);
    rt_px(o + 3, 13, C_LGREEN);
    rt_px(o + 10, 14, C_LGREEN);
    SDL_SetRenderTarget(rt_ren, rt_canvas);
}

static void blit_tile(int a, int x, int y) {
    SDL_Rect s = {a * TS, 0, TS, TS}, d = {x, y, TS, TS};
    SDL_RenderCopy(rt_ren, atlas, &s, &d);
}

/* ================================================================== game state */
enum { E_SPIDER, E_BAT, E_TOTEM, E_FIREBALL };
typedef struct { int type; bool alive; float x, y, vx, x0, y0; int t, cool, dir, w, h; } Ent;
typedef struct { float x, y, vx, vy; int life, col; } Part;
typedef struct { float x, y; int life, col; char txt[12]; } Pop;

#define MAXE 96
#define MAXP 400
#define MAXPOP 16

enum { S_TITLE, S_CARD, S_PLAY, S_TALLY, S_OVER, S_ENTRY, S_WIN };

static Level L;
static Ent ents[MAXE];
static int nents;
static Part parts[MAXP];
static Pop pops[MAXPOP];

static struct {
    int state, st, tick;
    int level, score, lives, next_life;
    bool has_key, has_gun;
    int fuel;
    Body p;
    int face, dying, door_t, jump_buf;
    float anim;
    int gems_got;
    float camx, camt;
    bool paused, quitq;
    int msg_t, seal_t;
    char msg[48];
    int tally_step, tally_bonus, tally_t;
    bool perfect;
    char name[4];
    int name_len, rank, letter;
    int title_page;
    struct { bool on; float x, y, vx, dist; } bullet;
} G;

static bool selftest;

/* ================================================================== sound effects */
static float semi(int s) { return 440.0f * powf(2.0f, s / 12.0f); }
static void sfx_jump(void) { rt_tone(220, 660, 70, 2); }
static void sfx_gem(void) { RtNote n[] = {{988, 988, 35}, {1319, 1319, 55}}; rt_play(n, 2, 3); }
static void sfx_bump(void) { rt_tone(150, 90, 50, 2); }
static void sfx_block(void) { RtNote n[] = {{784, 784, 40}, {1175, 1175, 40}, {1568, 1568, 60}}; rt_play(n, 3, 3); }
static void sfx_shoot(void) { rt_tone(1400, 300, 60, 3); }
static void sfx_stomp(void) { rt_tone(400, 1000, 60, 4); }
static void sfx_kill(void) { RtNote n[] = {{800, 200, 70}, {0, 0, 15}, {300, 80, 90}}; rt_play(n, 3, 4); }
static void sfx_fireball(void) { rt_tone(260, 120, 90, 2); }
static void sfx_pickup(void) {
    RtNote n[] = {{523, 523, 45}, {659, 659, 45}, {784, 784, 45}, {1047, 1047, 90}};
    rt_play(n, 4, 5);
}
static void sfx_key(void) {
    RtNote n[] = {{523, 523, 70}, {0, 0, 20}, {659, 659, 70}, {0, 0, 20}, {784, 784, 70}, {0, 0, 20},
                  {1047, 1047, 120}, {784, 784, 70}, {1047, 1047, 200}};
    rt_play(n, 9, 6);
}
static void sfx_1up(void) {
    RtNote n[] = {{660, 660, 60}, {880, 880, 60}, {1320, 1320, 60}, {1760, 1760, 120}};
    rt_play(n, 4, 6);
}
static void sfx_die(void) {
    RtNote n[16];
    for (int i = 0; i < 16; i++) n[i] = (RtNote){semi(12 - i * 2) * (i & 1 ? 1.06f : 1), semi(10 - i * 2), 45};
    rt_play(n, 16, 9);
}
static void sfx_door(void) {
    static const int sc[] = {0, 4, 7, 12, 7, 12, 16, 19, 24};
    RtNote n[9];
    for (int i = 0; i < 9; i++) n[i] = (RtNote){semi(sc[i] - 9), semi(sc[i] - 9), i == 8 ? 260 : 70};
    rt_play(n, 9, 8);
}
static void sfx_tick(void) { rt_tone(1500, 1500, 10, 1); }
static void sfx_seal(void) { RtNote n[] = {{196, 196, 90}, {0, 0, 30}, {147, 147, 160}}; rt_play(n, 3, 3); }
static void sfx_select(void) { rt_tone(660, 990, 50, 4); }

/* an original adventure theme for the PC speaker */
static void tune_title(void) {
    static const int mel[][2] = {
        {-12, 2}, {-9, 2}, {-5, 2}, {0, 4}, {-2, 2}, {-5, 2}, {-4, 2}, {-5, 4},
        {-7, 2}, {-9, 2}, {-7, 2}, {-5, 4}, {-9, 2}, {-12, 2}, {-14, 2}, {-12, 6},
        {-12, 2}, {-9, 2}, {-5, 2}, {0, 4}, {3, 2}, {2, 2}, {0, 2}, {-2, 4},
        {-4, 2}, {-5, 2}, {-7, 2}, {-9, 4}, {-5, 2}, {-9, 2}, {-11, 2}, {-12, 8},
    };
    RtNote n[RT_QMAX];
    int k = 0;
    for (int i = 0; i < 32 && k < RT_QMAX - 1; i++) {
        float f = semi(mel[i][0]);
        n[k++] = (RtNote){f, f, mel[i][1] * 70 - 18};
        n[k++] = (RtNote){0, 0, 18};
    }
    rt_play(n, k, 2);
}
static void tune_card(void) {
    static const int mel[] = {-9, -5, 0, -5, 0, 3, 7};
    RtNote n[14];
    for (int i = 0; i < 7; i++) {
        n[i * 2] = (RtNote){semi(mel[i]), semi(mel[i]), i == 6 ? 240 : 80};
        n[i * 2 + 1] = (RtNote){0, 0, 15};
    }
    rt_play(n, 14, 7);
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

/* ================================================================== effects */
static void burst(float x, float y, int n, const int *cols, int ncol, float spd) {
    for (int i = 0; i < MAXP && n > 0; i++) {
        if (parts[i].life > 0) continue;
        float a = rt_randf() * 6.2832f, s = (0.3f + rt_randf()) * spd;
        parts[i] = (Part){x, y, cosf(a) * s, sinf(a) * s - spd * 0.5f, 25 + (int)(rt_rand() % 30),
                          cols[rt_rand() % (Uint32)ncol]};
        n--;
    }
}
static void popup(float x, float y, const char *txt, int col) {
    for (int i = 0; i < MAXPOP; i++)
        if (pops[i].life <= 0) {
            pops[i] = (Pop){x, y, 50, col, {0}};
            snprintf(pops[i].txt, sizeof(pops[i].txt), "%s", txt);
            return;
        }
}
static void add_score(int pts, float x, float y) {
    G.score += pts;
    if (x > -999) {
        char b[12];
        snprintf(b, sizeof(b), "%d", pts);
        popup(x, y, b, C_WHITE);
    }
    while (G.score >= G.next_life) {
        G.next_life += EXTRA_LIFE;
        G.lives++;
        sfx_1up();
        popup(G.p.x - 4, G.p.y - 10, "1UP!", C_LGREEN);
    }
}
static void message(const char *m) {
    snprintf(G.msg, sizeof(G.msg), "%s", m);
    G.msg_t = 240;
}

/* ================================================================== level setup */
static void ent_dims(int type, int *w, int *h) {
    switch (type) {
    case E_SPIDER: *w = 12; *h = 9; break;
    case E_BAT: *w = 12; *h = 7; break;
    case E_TOTEM: *w = 12; *h = 16; break;
    default: *w = 6; *h = 6; break;
    }
}
static Ent *spawn(int type, float x, float y) {
    for (int i = 0; i < MAXE; i++)
        if (!ents[i].alive) {
            Ent e = {0};
            e.type = type;
            e.alive = true;
            e.x = e.x0 = x;
            e.y = e.y0 = y;
            e.dir = -1;
            ent_dims(type, &e.w, &e.h);
            ents[i] = e;
            if (i >= nents) nents = i + 1;
            return &ents[i];
        }
    return NULL;
}

/* totems_as_hazard: the solver marks totem squares as hazards instead of spawning them */
static void level_build(Level *lv, int idx, bool spawn_ents, bool totems_as_hazard) {
    const LevelDef *d = &LEVELS[idx];
    memset(lv, 0, sizeof(*lv));
    for (int r = 0; r < ROWS; r++) {
        int n = (int)strlen(d->row[r]);
        if (n > lv->w) lv->w = n;
    }
    if (lv->w > MAXW) lv->w = MAXW;
    for (int r = 0; r < ROWS; r++) {
        int n = (int)strlen(d->row[r]);
        for (int x = 0; x < lv->w; x++) {
            char c = x < n ? d->row[r][x] : '.';
            float ex = (float)(x * TS + 2);
            switch (c) {
            case 'P': lv->sx = x; lv->sy = r; c = '.'; break;
            case 'S': if (spawn_ents) spawn(E_SPIDER, ex, (float)(r * TS + TS - 9)); c = '.'; break;
            case 'B': if (spawn_ents) spawn(E_BAT, ex, (float)(r * TS + 4)); c = '.'; break;
            case 'F':
                if (spawn_ents) { Ent *e = spawn(E_TOTEM, ex, (float)(r * TS)); if (e) e->cool = 60 + x * 7 % 50; }
                c = totems_as_hazard ? '!' : '.';
                break;
            }
            if (ch_gem(c)) lv->gems++;
            lv->t[r][x] = c;
        }
    }
}

static void place_player_at_start(void) {
    G.p = (Body){0};
    G.p.x = (float)(L.sx * TS + 3);
    G.p.y = (float)(L.sy * TS + TS - PH);
    G.p.ground = true;
    G.face = 1;
    float maxc = (float)(L.w * TS - RT_W);
    G.camx = G.p.x - 96;
    if (G.camx > maxc) G.camx = maxc;
    if (G.camx < 0) G.camx = 0;
    G.camt = G.camx;
}

static void start_level(int idx) {
    G.level = idx;
    memset(ents, 0, sizeof(ents));
    nents = 0;
    memset(parts, 0, sizeof(parts));
    memset(pops, 0, sizeof(pops));
    level_build(&L, idx, true, false);
    bake_atlas(LEVELS[idx].theme);
    G.has_key = G.has_gun = false;
    G.fuel = 0;
    G.gems_got = 0;
    G.dying = G.door_t = 0;
    G.bullet.on = false;
    G.msg_t = 0;
    G.jump_buf = 0;
    place_player_at_start();
    message("FIND THE SUNSTONE!");
}

static void new_game(int level) {
    G.score = 0;
    G.lives = 3;
    G.next_life = EXTRA_LIFE;
    start_level(level);
    G.state = S_CARD;
    G.st = 0;
    tune_card();
}

/* ================================================================== play update */
static bool overlap(float ax, float ay, float aw, float ah, float bx, float by, float bw, float bh) {
    return ax < bx + bw && bx < ax + aw && ay < by + bh && by < ay + ah;
}

static void kill_player(void) {
    if (G.dying || G.door_t) return;
    G.dying = 1;
    G.p.jet = false;
    static const int cols[] = {C_WHITE, C_YELLOW, C_LRED, C_LGREEN, C_SKIN};
    burst(G.p.x + PW / 2, G.p.y + PH / 2, 60, cols, 5, 2.2f);
    sfx_die();
}

static void kill_ent(Ent *e, int pts) {
    e->alive = false;
    static const int spc[] = {C_MAGENTA, C_LMAGENTA, C_WHITE};
    static const int btc[] = {C_DGRAY, C_LGRAY, C_LRED};
    static const int ttc[] = {C_LGRAY, C_YELLOW, C_LRED, C_WHITE};
    const int *c = e->type == E_SPIDER ? spc : e->type == E_BAT ? btc : ttc;
    burst(e->x + e->w / 2, e->y + e->h / 2, 24, c, 3, 1.6f);
    if (pts) add_score(pts, e->x, e->y - 6);
}

static void collect_items(void) {
    float x = G.p.x + 1, y = G.p.y + 1, w = PW - 2, h = PH - 2;
    int x0 = (int)floorf(x / TS), x1 = (int)floorf((x + w) / TS);
    int y0 = (int)floorf(y / TS), y1 = (int)floorf((y + h) / TS);
    bool on_door = false;
    for (int ty = y0; ty <= y1; ty++)
        for (int tx = x0; tx <= x1; tx++) {
            if (tx < 0 || tx >= L.w || ty < 0 || ty >= ROWS) continue;
            char *c = &L.t[ty][tx];
            float px = (float)(tx * TS), py = (float)(ty * TS);
            static const int sparkle[] = {C_WHITE, C_YELLOW, C_LCYAN};
            switch (*c) {
            case '1': case '2': case '3': case '4': {
                static const int pts[] = {100, 250, 500, 1000};
                add_score(pts[*c - '1'], px, py - 4);
                G.gems_got++;
                *c = '.';
                burst(px + 8, py + 8, 8, sparkle, 3, 1.0f);
                sfx_gem();
                break;
            }
            case 'K':
                *c = '.';
                G.has_key = true;
                add_score(2000, px - 4, py - 4);
                burst(px + 8, py + 8, 40, sparkle, 3, 2.0f);
                sfx_key();
                message("VAULT OPEN! RUN FOR THE DOOR!");
                break;
            case 'J':
                if (G.fuel < FUEL_MAX - 30) {
                    if (G.fuel == 0) message("JETPACK! PRESS C OR ALT TO FLY");
                    G.fuel = FUEL_MAX;
                    sfx_pickup();
                }
                break;
            case 'G':
                *c = '.';
                G.has_gun = true;
                sfx_pickup();
                message("PISTOL! PRESS X OR CTRL TO FIRE");
                break;
            case 'L':
                *c = '.';
                G.lives++;
                sfx_1up();
                popup(px, py - 4, "1UP!", C_LGREEN);
                break;
            case 'D': on_door = true; break;
            }
        }
    if (on_door) {
        if (G.has_key) {
            G.door_t = 1;
            G.p.jet = false;
            sfx_door();
        } else if (G.seal_t <= 0) {
            message("SEALED! GRAB THE SUNSTONE FIRST");
            sfx_seal();
            G.seal_t = 120;
        }
    }
    if (G.seal_t > 0) G.seal_t--;
}

static void update_ents(void) {
    Body *p = &G.p;
    for (int i = 0; i < nents; i++) {
        Ent *e = &ents[i];
        if (!e->alive) continue;
        e->t++;
        switch (e->type) {
        case E_SPIDER: {
            float nx = e->x + e->dir * 0.6f;
            int fx = e->dir > 0 ? (int)floorf((nx + e->w) / TS) : (int)floorf(nx / TS);
            int my = (int)floorf((e->y + e->h / 2.0f) / TS), by = (int)floorf((e->y + e->h + 1) / TS);
            char below = tget(&L, fx, by);
            if (tsolid(&L, fx, my) || !(ch_solid(below) || below == '-')) e->dir = -e->dir;
            else e->x = nx;
            break;
        }
        case E_BAT: {
            float nx = e->x + e->dir * 0.8f;
            int fx = e->dir > 0 ? (int)floorf((nx + e->w) / TS) : (int)floorf(nx / TS);
            if (tsolid(&L, fx, (int)floorf((e->y + 3) / TS)) || fabsf(nx - e->x0) > 72) e->dir = -e->dir;
            else e->x = nx;
            e->y = e->y0 + sinf(e->t * 0.05f) * 10;
            break;
        }
        case E_TOTEM: {
            float dx = p->x - e->x, dy = p->y - e->y;
            if (e->cool > 0) e->cool--;
            if (e->cool <= 0 && fabsf(dx) < 170 && fabsf(dy) < 24 && !G.dying) {
                int d = dx < 0 ? -1 : 1;
                Ent *f = spawn(E_FIREBALL, e->x + (d > 0 ? 12 : -6), e->y + 4);
                if (f) { f->vx = d * 2.2f; f->dir = d; }
                e->cool = 130;
                sfx_fireball();
            }
            break;
        }
        case E_FIREBALL:
            e->x += e->vx;
            if (tsolid(&L, (int)floorf((e->x + 3) / TS), (int)floorf((e->y + 3) / TS)) || e->t > 400) {
                static const int c[] = {C_YELLOW, C_LRED};
                burst(e->x + 3, e->y + 3, 6, c, 2, 0.8f);
                e->alive = false;
            }
            break;
        }
        if (!e->alive || G.dying || G.door_t) continue;
        /* player bullet */
        if (G.bullet.on && overlap(G.bullet.x, G.bullet.y, 6, 2, e->x, e->y, e->w, e->h)) {
            G.bullet.on = false;
            kill_ent(e, e->type == E_TOTEM ? 500 : e->type == E_FIREBALL ? 50 : 200);
            sfx_kill();
            continue;
        }
        /* touching the player */
        if (overlap(p->x + 1, p->y + 1, PW - 2, PH - 1, e->x, e->y, e->w, e->h)) {
            bool stompable = e->type == E_SPIDER || e->type == E_BAT;
            if (stompable && p->vy > 0.4f && p->y + PH - e->y < 8) {
                kill_ent(e, e->type == E_SPIDER ? 200 : 300);
                p->vy = rt_held.jump ? -6.0f : -3.8f;
                p->jet = false;
                sfx_stomp();
            } else {
                kill_player();
            }
        }
    }
}

static void update_play(void) {
    Body *p = &G.p;
    if (G.dying) {
        if (++G.dying > 110) {
            G.lives--;
            if (G.lives < 0) {
                G.state = S_OVER;
                G.st = 0;
                tune_over();
                return;
            }
            /* respawn at the vault entrance; collected items stay collected */
            G.dying = 0;
            G.bullet.on = false;
            for (int i = 0; i < nents; i++) {
                if (ents[i].type == E_FIREBALL) ents[i].alive = false;
                else if (ents[i].alive) { ents[i].x = ents[i].x0; ents[i].y = ents[i].y0; }
            }
            place_player_at_start();
        }
        return;
    }
    if (G.door_t) {
        if (++G.door_t > 80) {
            G.state = S_TALLY;
            G.st = 0;
            G.tally_step = 0;
            G.tally_t = 0;
            G.perfect = G.gems_got == L.gems;
            G.tally_bonus = 1000 * (G.level + 1) + (G.perfect ? 5000 : 0);
        }
        return;
    }

    Ctl c = {0};
    c.dx = (rt_held.r ? 1 : 0) - (rt_held.l ? 1 : 0);
    if (RT_HIT(jet) && G.fuel > 0) {
        p->jet = !p->jet;
        rt_tone(p->jet ? 200 : 400, p->jet ? 400 : 150, 60, 3);
    }
    if (p->jet) {
        c.dy = (rt_held.d ? 1 : 0) - (rt_held.u ? 1 : 0);
    } else {
        if (RT_HIT(jump)) G.jump_buf = 7;
        c.jump_held = rt_held.jump;
        c.jump_hit = G.jump_buf > 0;
    }
    bool can_jump = !p->jet && (p->ground || p->coyote > 0);
    phys_step(p, &c, &L);
    if (c.jump_hit && can_jump) { G.jump_buf = 0; sfx_jump(); }
    else if (G.jump_buf > 0) G.jump_buf--;
    if (c.dx) G.face = c.dx;
    G.anim += fabsf(p->vx);

    if (p->headx >= 0) {
        char *t = &L.t[p->heady][p->headx];
        if (p->headx < L.w && *t == '?') {
            *t = 'u';
            add_score(100, (float)(p->headx * TS), (float)(p->heady * TS - 8));
            static const int c2[] = {C_YELLOW, C_WHITE};
            burst((float)(p->headx * TS + 8), (float)(p->heady * TS), 10, c2, 2, 1.2f);
            sfx_block();
        } else {
            sfx_bump();
        }
    }
    if (p->jet) {
        if (--G.fuel <= 0) { G.fuel = 0; p->jet = false; message("OUT OF FUEL!"); }
        if (!rt_busy()) rt_tone(70 + (G.tick & 1) * 25, 80, 30, 0);
        if ((G.tick & 1) == 0) {
            static const int fc[] = {C_YELLOW, C_LRED, C_RED};
            int bx = (int)(G.face > 0 ? p->x - 1 : p->x + PW + 1);
            for (int i = 0; i < MAXP; i++)
                if (parts[i].life <= 0) {
                    parts[i] = (Part){(float)bx, p->y + 13, (rt_randf() - 0.5f) * 0.6f, 1.2f + rt_randf(), 10,
                                      fc[rt_rand() % 3]};
                    break;
                }
        }
    }
    if (RT_HIT(fire) && G.has_gun && !G.bullet.on) {
        G.bullet.on = true;
        G.bullet.x = G.face > 0 ? p->x + PW : p->x - 6;
        G.bullet.y = p->y + 6;
        G.bullet.vx = G.face * 5.0f;
        G.bullet.dist = 0;
        sfx_shoot();
    }
    if (G.bullet.on) {
        G.bullet.x += G.bullet.vx;
        G.bullet.dist += fabsf(G.bullet.vx);
        int tx = (int)floorf((G.bullet.x + 3) / TS), ty = (int)floorf((G.bullet.y + 1) / TS);
        if (tsolid(&L, tx, ty) || G.bullet.dist > 280) {
            static const int c3[] = {C_WHITE, C_YELLOW};
            burst(G.bullet.x + 3, G.bullet.y, 5, c3, 2, 0.8f);
            G.bullet.on = false;
        }
    }

    if (box_hazard(&L, p->x, p->y) || p->y > ROWS * TS + 4) { kill_player(); return; }
    collect_items();
    update_ents();

    /* half-screen "jump" scrolling, as the old engines did */
    float sx = p->x - G.camx, maxc = (float)(L.w * TS - RT_W);
    if (G.camt == G.camx) {
        if (sx > 232) G.camt = G.camx + 160;
        else if (sx < 72) G.camt = G.camx - 160;
        if (G.camt > maxc) G.camt = maxc;
        if (G.camt < 0) G.camt = 0;
    }
    if (G.camx < G.camt) G.camx = fminf(G.camt, G.camx + 8);
    if (G.camx > G.camt) G.camx = fmaxf(G.camt, G.camx - 8);
}

static void update_fx(void) {
    for (int i = 0; i < MAXP; i++) {
        Part *q = &parts[i];
        if (q->life <= 0) continue;
        q->life--;
        q->x += q->vx;
        q->y += q->vy;
        q->vy += 0.08f;
    }
    for (int i = 0; i < MAXPOP; i++)
        if (pops[i].life > 0) { pops[i].life--; pops[i].y -= 0.4f; }
    if (G.msg_t > 0) G.msg_t--;
}

/* ================================================================== drawing: world */
static void draw_fire(int x, int y, int t) {
    rt_rect(x, y + 12, 16, 4, C_RED);
    for (int c = 0; c < 16; c += 2) {
        float h = 6 + 4 * sinf(t * 0.25f + (x + c) * 0.7f) + 2 * sinf(t * 0.13f + c * 1.3f);
        int hh = (int)h;
        rt_rect(x + c, y + 16 - hh, 2, hh, C_RED);
        rt_rect(x + c, y + 16 - hh + 1, 2, hh * 2 / 3, C_LRED);
        rt_rect(x + c, y + 16 - hh + 2, 2, hh / 3, C_YELLOW);
        if (((t / 4 + c + x) % 7) == 0) rt_px(x + c, y + 16 - hh - 1, C_WHITE);
    }
}
static void draw_water(int x, int y, int t) {
    rt_rect(x, y + 3, 16, 13, C_BLUE);
    for (int c = 0; c < 16; c++) {
        int yy = y + 3 + (int)lroundf(1.4f * sinf((x + c) * 0.4f + t * 0.12f));
        rt_px(x + c, yy, C_LCYAN);
        rt_px(x + c, yy + 1, C_CYAN);
    }
    int sp = (x * 7 + t / 6) % 23;
    if (sp < 16) rt_px(x + sp, y + 9 + (sp % 5), C_LBLUE);
}
static void draw_torch(int x, int y, int t) {
    rt_rect(x + 6, y + 9, 4, 2, C_DGRAY);
    rt_rect(x + 7, y + 10, 2, 5, C_BROWN);
    int f = (t / 5 + x) % 3;
    rt_rect(x + 6, y + 4 + (f == 1), 4, 5, C_LRED);
    rt_rect(x + 7, y + 2 + f, 2, 5, C_YELLOW);
    rt_px(x + 7 + (f & 1), y + 5, C_WHITE);
    for (int i = 0; i < 6; i++) { /* dithered glow */
        int gx = x + 8 + (int)(cosf(i * 1.05f) * 9), gy = y + 6 + (int)(sinf(i * 1.05f) * 7);
        if ((i + t / 8) % 2) rt_px(gx, gy, C_BROWN);
    }
}
static void draw_vine(int x, int y) {
    for (int yy = 0; yy < 14; yy++) rt_px(x + 8 + (yy / 3) % 2, y + yy, yy % 4 == 0 ? C_LGREEN : C_GREEN);
    rt_px(x + 6, y + 4, C_LGREEN);
    rt_px(x + 11, y + 9, C_LGREEN);
    rt_px(x + 10, y + 13, C_LGREEN);
}
static void draw_sunstone(int cx, int cy, int t) {
    for (int i = 0; i < 8; i++) {
        float a = i * 0.785f + t * 0.03f;
        int len = 7 + ((i + t / 6) & 1) * 2;
        rt_line(cx, cy, cx + (int)(cosf(a) * len), cy + (int)(sinf(a) * len), (i + t / 10) & 1 ? C_YELLOW : C_BROWN);
    }
    rt_disc(cx, cy, 5, C_BROWN);
    rt_disc(cx, cy, 4, C_YELLOW);
    rt_disc(cx - 1, cy - 1, 2, C_WHITE);
}
static void draw_heart(int cx, int cy) {
    rt_disc(cx - 3, cy - 1, 3, C_LRED);
    rt_disc(cx + 3, cy - 1, 3, C_LRED);
    for (int i = 0; i < 6; i++) rt_rect(cx - 6 + i, cy + i, 13 - i * 2, 1, C_LRED);
    rt_px(cx - 4, cy - 2, C_WHITE);
    rt_rect(cx - 1, cy + 4, 3, 1, C_RED);
}
static void draw_door(int x, int y, bool open, int t) {
    rt_rect(x, y, 16, 32, C_DGRAY);
    rt_rect(x + 1, y + 1, 14, 1, C_LGRAY);
    if (!open) {
        rt_rect(x + 2, y + 3, 12, 29, C_BROWN);
        rt_rect(x + 6, y + 3, 1, 29, C_BLACK);
        rt_rect(x + 10, y + 3, 1, 29, C_BLACK);
        rt_rect(x + 2, y + 8, 12, 2, C_LGRAY);
        rt_rect(x + 2, y + 23, 12, 2, C_LGRAY);
        rt_rect(x + 11, y + 15, 2, 3, C_YELLOW);
    } else {
        rt_rect(x + 2, y + 3, 12, 29, C_BLACK);
        int pulse = (t / 6) % 4;
        for (int i = 0; i < 4; i++)
            rt_rect(x + 3 + i, y + 31 - i * 6 - pulse, 10 - i * 2, 2, i < 2 ? C_YELLOW : C_BROWN);
        rt_text("^", x + 4, y + 6, (t / 15) % 2 ? C_WHITE : C_YELLOW);
    }
}

static void draw_item(char c, int x, int y, int tx, int t) {
    int bob = (int)lroundf(sinf(t * 0.08f + tx * 0.9f) * 1.5f);
    switch (c) {
    case '1': rt_draw(SPR[SP_DIAMOND], x, y + bob, false); break;
    case '2': rt_draw(SPR[SP_RUBY], x, y + bob, false); break;
    case '3': rt_draw(SPR[SP_CROWN], x, y + bob, false); break;
    case '4': rt_draw(SPR[SP_ORB], x, y + bob, false); break;
    case 'K': draw_sunstone(x + 8, y + 8 + bob, t); break;
    case 'J':
        rt_draw(SPR[SP_JETPACK], x, y + bob, false);
        if (G.fuel < FUEL_MAX - 30 && (t / 20) % 2) rt_text("F", x + 4, y - 7, C_LCYAN);
        break;
    case 'G': rt_draw(SPR[SP_GUN], x, y + bob, false); break;
    case 'L': draw_heart(x + 8, y + 7 + bob); break;
    }
}

static void draw_world(void) {
    const int t = G.tick;
    int cam = (int)G.camx;
    SDL_Rect clip = {0, PLAY_Y, RT_W, ROWS * TS};
    SDL_RenderSetClipRect(rt_ren, &clip);
    rt_rect(0, PLAY_Y, RT_W, ROWS * TS, C_BLACK);
    int c0 = cam / TS, c1 = c0 + RT_W / TS + 1;
    /* back wall + static tiles */
    for (int ty = 0; ty < ROWS; ty++)
        for (int tx = c0; tx <= c1 && tx < L.w; tx++) {
            int x = tx * TS - cam, y = PLAY_Y + ty * TS;
            char ch = L.t[ty][tx];
            if (!ch_solid(ch)) blit_tile(A_BACK, x, y);
            switch (ch) {
            case '#': blit_tile(A_BRICK, x, y); break;
            case 'R': blit_tile(A_ROCK, x, y); break;
            case '=': blit_tile(A_GIRDER, x, y); break;
            case '-': blit_tile(A_LEDGE, x, y); break;
            case '?': blit_tile((t / 18) % 4 == 3 ? A_BONUS2 : A_BONUS1, x, y); break;
            case 'u': blit_tile(A_USED, x, y); break;
            case '*': blit_tile(A_THORN, x, y); break;
            case '^': draw_fire(x, y, t); break;
            case '~': draw_water(x, y, t); break;
            case 't': draw_torch(x, y, t); break;
            case 'v': draw_vine(x, y); break;
            case 'D': draw_door(x, y - TS, G.has_key, t); break;
            default: draw_item(ch, x, y, tx, t); break;
            }
        }
    /* entities */
    for (int i = 0; i < nents; i++) {
        Ent *e = &ents[i];
        if (!e->alive) continue;
        int x = (int)e->x - cam, y = (int)e->y + PLAY_Y;
        if (x < -32 || x > RT_W + 16) continue;
        switch (e->type) {
        case E_SPIDER: rt_draw(SPR[(e->t / 8) % 2 ? SP_SPIDER1 : SP_SPIDER2], x - 2, y + e->h - 15, e->dir > 0); break;
        case E_BAT: rt_draw(SPR[(e->t / 7) % 2 ? SP_BAT1 : SP_BAT2], x - 2, y - 4, false); break;
        case E_TOTEM:
            if (e->cool < 24 && (e->t / 3) % 2) rt_draw_tint(SPR[SP_TOTEM], x - 2, y, false, C_RED);
            else rt_draw(SPR[SP_TOTEM], x - 2, y, false);
            break;
        case E_FIREBALL:
            rt_disc(x + 3, y + 3, 3, C_LRED);
            rt_disc(x + 3, y + 3, 2, C_YELLOW);
            rt_px(x + 3 - e->dir * 4, y + 3, C_RED);
            rt_px(x + 3 - e->dir * 6, y + 2 + (e->t / 2) % 3, C_RED);
            break;
        }
    }
    /* player */
    Body *p = &G.p;
    if (!G.dying) {
        int x = (int)lroundf(p->x) - cam - 3, y = (int)lroundf(p->y) + PLAY_Y - 2;
        bool flip = G.face < 0;
        if (G.door_t) { /* walk into the door, fading to a silhouette */
            if (G.door_t > 40) y += 99; /* gone */
            else if (G.door_t > 20 && (G.door_t / 2) % 2) {
                rt_draw_tint(SPR[SP_STAND], x, y, flip, C_YELLOW);
                y += 99;
            }
        }
        int sp = SP_STAND;
        if (!p->ground && !p->jet) sp = SP_JUMP;
        else if (p->ground && fabsf(p->vx) > 0.2f) {
            int f = (int)(G.anim / 7) % 4;
            sp = f == 1 ? SP_WALK1 : f == 3 ? SP_WALK2 : SP_STAND;
        }
        if (y < RT_H) {
            if (G.fuel > 0) { /* jetpack on the back */
                int bx = flip ? x + 12 : x;
                rt_rect(bx, y + 7, 4, 7, C_LGRAY);
                rt_rect(bx, y + 7, 4, 1, C_WHITE);
                rt_rect(bx + 1, y + 14, 2, 1, C_RED);
                if (p->jet) {
                    int fl = 2 + (t / 2) % 3;
                    rt_rect(bx + 1, y + 15, 2, fl, C_YELLOW);
                    rt_px(bx + 1 + (t & 1), y + 15 + fl, C_LRED);
                }
            }
            rt_draw(SPR[sp], x, y, flip);
        }
    }
    if (G.bullet.on) {
        int bx = (int)G.bullet.x - cam, by = (int)G.bullet.y + PLAY_Y;
        rt_rect(bx, by, 6, 2, C_YELLOW);
        rt_rect(bx + (G.bullet.vx > 0 ? 4 : 0), by, 2, 2, C_WHITE);
    }
    for (int i = 0; i < MAXP; i++)
        if (parts[i].life > 0) rt_px((int)parts[i].x - cam, (int)parts[i].y + PLAY_Y, parts[i].col);
    for (int i = 0; i < MAXPOP; i++)
        if (pops[i].life > 0)
            rt_text_sh(pops[i].txt, (int)pops[i].x - cam, (int)pops[i].y + PLAY_Y, (pops[i].life / 4) % 2 ? pops[i].col : C_YELLOW);
    SDL_RenderSetClipRect(rt_ren, NULL);
}

static void draw_mini_head(int x, int y) {
    SDL_Rect s = {2, 1, 12, 7}, d = {x, y, 12, 7};
    SDL_RenderCopy(rt_ren, SPR[SP_STAND].t, &s, &d);
}

static void draw_hud(void) {
    char b[64];
    rt_rect(0, 0, RT_W, PLAY_Y, C_BLACK);
    rt_rect(0, PLAY_Y - 3, RT_W, 1, C_DGRAY);
    rt_rect(0, PLAY_Y - 2, RT_W, 1, C_LGRAY);
    rt_rect(0, PLAY_Y - 1, RT_W, 1, C_DGRAY);
    rt_text("SCORE:", 4, 5, C_LCYAN);
    snprintf(b, sizeof(b), "%07d", G.score);
    rt_text(b, 52, 5, C_WHITE);
    rt_text("LEVEL:", 124, 5, C_LCYAN);
    snprintf(b, sizeof(b), "%d", G.level + 1);
    rt_text(b, 172, 5, C_WHITE);
    rt_text("LIVES:", 196, 5, C_LCYAN);
    int lv = G.lives > 6 ? 6 : G.lives;
    for (int i = 0; i < lv; i++) draw_mini_head(244 + i * 13, 5);
    if (G.lives > 6) { snprintf(b, sizeof(b), "+%d", G.lives - 6); rt_text(b, 300, 5, C_WHITE); }

    int by = PLAY_Y + ROWS * TS;
    rt_rect(0, by, RT_W, RT_H - by, C_BLACK);
    rt_rect(0, by, RT_W, 1, C_DGRAY);
    rt_rect(0, by + 1, RT_W, 1, C_LGRAY);
    rt_rect(0, by + 2, RT_W, 1, C_DGRAY);
    int x = 4;
    if (G.has_gun) { rt_draw(SPR[SP_GUN], x - 3, by + 1, false); x += 16; }
    if (G.fuel > 0) {
        rt_text("JET", x, by + 8, G.p.jet ? C_YELLOW : C_LGRAY);
        rt_frame(x + 26, by + 8, 42, 7, C_LGRAY);
        int w = G.fuel * 40 / FUEL_MAX;
        rt_rect(x + 27, by + 9, w, 5, G.fuel < FUEL_MAX / 5 ? C_LRED : C_LGREEN);
        x += 74;
    }
    if (G.has_key) draw_sunstone(RT_W - 76, by + 11, G.tick);
    snprintf(b, sizeof(b), "%d/%d", G.gems_got, L.gems);
    rt_draw(SPR[SP_DIAMOND], RT_W - 62, by + 2, false);
    rt_text(b, RT_W - 44, by + 8, C_WHITE);
    if (G.msg_t > 0 && ((G.msg_t / 12) % 2 || G.msg_t > 200)) {
        int mw = rt_text_w(G.msg, 1);
        int mx = x + (RT_W - 80 - x - mw) / 2;
        if (mx < x) mx = x;
        rt_text(G.msg, mx, by + 8, G.has_key ? C_YELLOW : C_LCYAN);
    }
}

/* ================================================================== drawing: screens */
static const int LOGO_ROWS[7] = {C_WHITE, C_YELLOW, C_YELLOW, C_BROWN, C_LRED, C_RED, C_RED};

static void draw_logo(int y, int t) {
    int rows[7];
    for (int i = 0; i < 7; i++) rows[i] = LOGO_ROWS[i];
    int shine = (t / 4) % 40;
    if (shine < 7) rows[shine] = C_WHITE;
    const char *a = "RELIC", *b = "RUNNER";
    int wa = rt_text_w(a, 4), wb = rt_text_w(b, 4);
    rt_text_ex(a, (RT_W - wa) / 2 + 2, y + 2, 4, C_DGRAY, NULL);
    rt_text_ex(a, (RT_W - wa) / 2, y, 4, 0, rows);
    rt_text_ex(b, (RT_W - wb) / 2 + 2, y + 34, 4, C_DGRAY, NULL);
    rt_text_ex(b, (RT_W - wb) / 2, y + 32, 4, 0, rows);
}

static void draw_brick_band(int y, int theme) {
    bake_atlas(theme);
    for (int x = 0; x < RT_W; x += TS) blit_tile(A_BRICK, x, y);
}

static void draw_title(void) {
    int t = G.st;
    rt_cls(C_BLACK);
    draw_brick_band(0, 5);
    draw_brick_band(RT_H - 16, 5);
    switch (G.title_page) {
    case 0: {
        draw_logo(26, t);
        rt_textc_sh("SIX VAULTS. SIX SUNSTONES. ONE RUNNER.", 100, C_LCYAN);
        /* attract: Rook runs, a spider gives chase */
        int run = (t * 2) % (RT_W + 120) - 40;
        int f = (t / 5) % 4;
        rt_draw(SPR[f == 1 ? SP_WALK1 : f == 3 ? SP_WALK2 : SP_STAND], run, 118, false);
        rt_draw(SPR[(t / 8) % 2 ? SP_SPIDER1 : SP_SPIDER2], run - 40, 118, true);
        draw_sunstone(run + 34, 124, t);
        if ((t / 25) % 2) rt_textc("PRESS SPACE TO PLAY", 146, C_WHITE);
        rt_textc("F1 HELP  F2 CRT  M SOUND  ESC QUIT", 164, C_DGRAY);
        break;
    }
    case 1: {
        static const char *story[] = {
            "DEEP BELOW THE RED DESERT LIE THE", "SIX SUN VAULTS OF A LOST KINGDOM.", "",
            "EACH VAULT IS SEALED BY A SUNSTONE.", "", "ROOK RIVET, RELIC RUNNER, HAS COME",
            "TO CLAIM THEM ALL.", "", "GRAB THE SUNSTONE TO OPEN THE VAULT,", "THEN RUN FOR THE DOOR!",
        };
        rt_textc_sh("THE STORY", 26, C_YELLOW);
        for (int i = 0; i < 10; i++) rt_textc(story[i], 46 + i * 12, i < 3 ? C_LGRAY : C_WHITE);
        break;
    }
    case 2: {
        rt_textc_sh("HOW TO PLAY", 22, C_YELLOW);
        struct { int spr; const char *txt; } legend[] = {
            {SP_DIAMOND, "GEMS  100 / 250 / 500 / 1000"}, {-1, "SUNSTONE  OPENS THE VAULT DOOR"},
            {SP_JETPACK, "JETPACK  C / ALT  (REFILLS)"}, {SP_GUN, "PISTOL  X / CTRL"},
            {SP_SPIDER1, "SPIDERS AND BATS: STOMP THEM"}, {SP_TOTEM, "TOTEMS SPIT FIRE: SHOOT THEM"},
        };
        for (int i = 0; i < 6; i++) {
            int y = 38 + i * 19;
            if (legend[i].spr >= 0) rt_draw(SPR[legend[i].spr], 40, y - 4, false);
            else draw_sunstone(48, y + 3, t);
            rt_text(legend[i].txt, 66, y, i == 1 ? C_YELLOW : C_WHITE);
        }
        rt_textc("ARROWS MOVE   SPACE/UP JUMP   P PAUSE", 158, C_LCYAN);
        break;
    }
    case 3: {
        rt_textc_sh("HALL OF RUNNERS", 24, C_YELLOW);
        for (int i = 0; i < RT_NSCORES; i++) {
            char b[128];
            snprintf(b, sizeof(b), "%d. %-3.3s  %07d  LV %d", i + 1, rt_scores[i].name, rt_scores[i].score,
                     rt_scores[i].level);
            rt_textc(b, 46 + i * 14, i == 0 ? C_YELLOW : (i == G.rank ? C_LGREEN : C_WHITE));
        }
        break;
    }
    }
    if (G.title_page) rt_textc("< >  PAGES     SPACE  PLAY", 172, C_DGRAY);
}

static void draw_card(void) {
    char b[32];
    rt_cls(C_BLACK);
    draw_brick_band(40, LEVELS[G.level].theme);
    draw_brick_band(144, LEVELS[G.level].theme);
    snprintf(b, sizeof(b), "LEVEL %d", G.level + 1);
    int w = rt_text_w(b, 3);
    rt_text_ex(b, (RT_W - w) / 2 + 2, 70, 3, C_DGRAY, NULL);
    rt_text_ex(b, (RT_W - w) / 2, 68, 3, C_YELLOW, NULL);
    rt_textc_sh(LEVELS[G.level].name, 100, C_LCYAN);
    if ((G.st / 20) % 2) rt_textc("GET READY!", 120, C_WHITE);
}

static void draw_tally(void) {
    char b[48];
    rt_cls(C_BLACK);
    rt_box(40, 30, 240, 140, C_BLUE, C_WHITE);
    rt_textc_sh("VAULT CLEARED!", 44, C_YELLOW);
    rt_textc(LEVELS[G.level].name, 58, C_LCYAN);
    snprintf(b, sizeof(b), "GEMS FOUND   %d / %d", G.gems_got, L.gems);
    rt_textc(b, 80, C_WHITE);
    snprintf(b, sizeof(b), "VAULT BONUS  %d", 1000 * (G.level + 1));
    if (G.tally_step >= 1) rt_textc(b, 96, C_WHITE);
    if (G.tally_step >= 2) rt_textc(G.perfect ? "ALL GEMS!    5000" : "ALL GEMS     -", 110, G.perfect ? C_LGREEN : C_LGRAY);
    snprintf(b, sizeof(b), "SCORE  %07d", G.score);
    rt_textc_sh(b, 132, C_YELLOW);
    if (G.tally_step >= 3 && (G.st / 20) % 2) rt_textc("PRESS SPACE", 152, C_WHITE);
}

static void draw_over(void) {
    draw_world();
    draw_hud();
    rt_box(70, 70, 180, 60, C_RED, C_WHITE);
    rt_textc_sh("GAME OVER", 88, C_YELLOW);
    if (G.st > 90 && (G.st / 20) % 2) rt_textc("PRESS SPACE", 108, C_WHITE);
}

static void draw_entry(void) {
    char b[48];
    rt_cls(C_BLACK);
    draw_brick_band(0, 5);
    draw_brick_band(RT_H - 16, 5);
    rt_textc_sh("A NEW HIGH SCORE!", 40, C_YELLOW);
    snprintf(b, sizeof(b), "%07d", G.score);
    rt_textc(b, 58, C_WHITE);
    rt_textc("ENTER YOUR INITIALS", 84, C_LCYAN);
    for (int i = 0; i < 3; i++) {
        int x = 124 + i * 26;
        rt_rect(x, 118, 18, 2, C_LGRAY);
        char ch[2] = {0, 0};
        if (i < G.name_len) ch[0] = G.name[i];
        else if (i == G.name_len && (G.st / 15) % 2) ch[0] = (char)('A' + G.letter);
        if (ch[0]) rt_text_ex(ch, x + 1, 100, 2, i < G.name_len ? C_YELLOW : C_WHITE, NULL);
    }
    rt_textc("TYPE LETTERS, ENTER TO FINISH", 140, C_DGRAY);
    rt_textc("PAD: UP/DOWN PICK, A ADD, START OK", 152, C_DGRAY);
}

static void draw_win(void) {
    int t = G.st;
    rt_cls(C_BLACK);
    for (int i = 0; i < MAXP; i++)
        if (parts[i].life > 0) rt_px((int)parts[i].x, (int)parts[i].y, parts[i].col);
    draw_logo(16, t);
    rt_textc_sh("ALL SIX SUNSTONES ARE YOURS!", 96, C_YELLOW);
    rt_textc("THE SUN VAULTS SHINE AGAIN, AND", 116, C_WHITE);
    rt_textc("ROOK RIVET'S NAME GOES DOWN IN LEGEND.", 128, C_WHITE);
    char b[32];
    snprintf(b, sizeof(b), "FINAL SCORE %07d", G.score);
    rt_textc_sh(b, 150, C_LCYAN);
    if (t > 120 && (t / 20) % 2) rt_textc("PRESS SPACE", 172, C_WHITE);
}

static void draw_pause_overlay(void) {
    if (G.quitq) {
        rt_box(60, 76, 200, 46, C_BLUE, C_WHITE);
        rt_textc("QUIT TO TITLE?  (Y/N)", 95, C_WHITE);
    } else if (G.paused) {
        rt_box(100, 76, 120, 46, C_BLUE, C_WHITE);
        rt_textc("PAUSED", 95, (G.tick / 20) % 2 ? C_WHITE : C_YELLOW);
    }
}

static void draw_frame(void) {
    switch (G.state) {
    case S_TITLE: draw_title(); break;
    case S_CARD: draw_card(); break;
    case S_PLAY: draw_world(); draw_hud(); draw_pause_overlay(); break;
    case S_TALLY: draw_tally(); break;
    case S_OVER: draw_over(); break;
    case S_ENTRY: draw_entry(); break;
    case S_WIN: draw_win(); break;
    }
}

/* ================================================================== state machine */
static void goto_title(int page) {
    G.state = S_TITLE;
    G.st = 0;
    G.title_page = page;
    bake_atlas(5);
    if (page == 0) tune_title();
}

static void finish_game(void) {
    G.rank = rt_score_rank(G.score);
    if (G.rank >= 0) {
        G.state = S_ENTRY;
        G.st = 0;
        G.name_len = 0;
        G.letter = 0;
        memset(G.name, 0, sizeof(G.name));
    } else {
        goto_title(3);
    }
}

static void commit_entry(void) {
    if (G.name_len == 0) { strcpy(G.name, "???"); G.name_len = 3; }
    G.name[G.name_len] = 0;
    rt_score_insert(G.rank, G.name, G.score, G.level + 1);
    if (!selftest) rt_scores_save();
    sfx_select();
    goto_title(3);
}

static void update(void) {
    G.tick++;
    G.st++;
    switch (G.state) {
    case S_TITLE:
        if (G.st > 600) { G.title_page = (G.title_page + 1) % 4; G.st = 0; }
        if (RT_HIT(r)) { G.title_page = (G.title_page + 1) % 4; G.st = 0; sfx_tick(); }
        if (RT_HIT(l)) { G.title_page = (G.title_page + 3) % 4; G.st = 0; sfx_tick(); }
        if (RT_HIT(ok) || RT_HIT(jump)) { G.rank = -1; new_game(0); }
        break;
    case S_CARD:
        if (G.st > 150 || (G.st > 20 && (RT_HIT(ok) || RT_HIT(jump)))) { G.state = S_PLAY; G.st = 0; }
        break;
    case S_PLAY:
        if (G.quitq || G.paused) break;
        update_play();
        update_fx();
        break;
    case S_TALLY:
        G.tally_t++;
        if (G.tally_step == 0 && G.tally_t > 40) { G.tally_step = 1; G.tally_t = 0; }
        else if (G.tally_step == 1 && G.tally_t > 30) { G.tally_step = 2; G.tally_t = 0; }
        else if (G.tally_step == 2 && G.tally_t > 20) {
            if (G.tally_bonus > 0) {
                int s = G.tally_bonus > 100 ? 100 : G.tally_bonus;
                G.tally_bonus -= s;
                add_score(s, -1000, 0);
                if (G.tally_t % 3 == 0) sfx_tick();
            } else { G.tally_step = 3; }
        }
        if (G.tally_step == 3 && (RT_HIT(ok) || RT_HIT(jump))) {
            if (G.level + 1 >= NLEVELS) {
                G.state = S_WIN;
                G.st = 0;
                memset(parts, 0, sizeof(parts));
                tune_win();
            } else {
                start_level(G.level + 1);
                G.state = S_CARD;
                G.st = 0;
                tune_card();
            }
        }
        break;
    case S_OVER:
        update_fx();
        if (G.st > 90 && (RT_HIT(ok) || RT_HIT(jump))) finish_game();
        break;
    case S_ENTRY:
        if (RT_HIT(u)) { G.letter = (G.letter + 1) % 26; sfx_tick(); }
        if (RT_HIT(d)) { G.letter = (G.letter + 25) % 26; sfx_tick(); }
        if (rt_pad && SDL_GameControllerGetButton(rt_pad, SDL_CONTROLLER_BUTTON_A) && !rt_prev.jump &&
            G.name_len < 3) {
            G.name[G.name_len++] = (char)('A' + G.letter);
            sfx_tick();
        }
        if (rt_pad && SDL_GameControllerGetButton(rt_pad, SDL_CONTROLLER_BUTTON_START) && G.st > 10) commit_entry();
        break;
    case S_WIN:
        if (G.st % 30 == 1) { /* fireworks */
            static const int c[][3] = {{C_YELLOW, C_WHITE, C_LRED}, {C_LCYAN, C_WHITE, C_LBLUE}, {C_LGREEN, C_YELLOW, C_WHITE}, {C_LMAGENTA, C_WHITE, C_LRED}};
            burst(30 + rt_randf() * 260, 20 + rt_randf() * 70, 50, c[rt_rand() % 4], 3, 2.0f);
            rt_tone(900, 200, 120, 1);
        }
        update_fx();
        if (G.st > 120 && (RT_HIT(ok) || RT_HIT(jump))) finish_game();
        break;
    }
}

static bool handle_key(SDL_Keycode k) {
    if (k == SDLK_m) { rt_mute = !rt_mute; return true; }
    switch (G.state) {
    case S_TITLE:
        if (k == SDLK_ESCAPE) return false;
        if (k == SDLK_F1) { G.title_page = 2; G.st = 0; }
        break;
    case S_PLAY:
        if (G.quitq) {
            if (k == SDLK_y) goto_title(0);
            else if (k == SDLK_n || k == SDLK_ESCAPE) G.quitq = false;
        } else if (k == SDLK_ESCAPE) {
            G.quitq = true;
        } else if (k == SDLK_p || k == SDLK_PAUSE) {
            G.paused = !G.paused;
        }
        break;
    case S_ENTRY:
        if (k >= SDLK_a && k <= SDLK_z && G.name_len < 3) {
            G.name[G.name_len++] = (char)('A' + (k - SDLK_a));
            sfx_tick();
        } else if (k == SDLK_BACKSPACE && G.name_len > 0) {
            G.name[--G.name_len] = 0;
        } else if ((k == SDLK_RETURN || k == SDLK_KP_ENTER) && G.st > 10) {
            commit_entry();
        }
        break;
    default:
        if (k == SDLK_ESCAPE) goto_title(0);
        break;
    }
    return true;
}

/* ================================================================== self-test solver
 * Breadth-first search over standing positions, driving the real phys_step with a set of
 * macro-actions (run-ups, jumps of several heights, mid-air releases, jetpack flights).
 * Enemies are ignored; totems count as hazards. Proves the Sunstone and door are reachable. */
#define SF_KEY 1
#define SF_JET 2
typedef struct { float x, y; unsigned char fl; } SSt;
typedef struct { int dir, run, jump, release, drift_after, drift_dir; } Act;

static Level SL;
static unsigned char s_vis[4][ROWS * TS / 2 + 8][MAXW * TS / 4 + 8];
static SSt s_q[200000];
static int s_qh, s_qt;
static bool s_gem[ROWS][MAXW], s_done, s_key_seen;
static Act s_acts[160];
static int s_nacts;

static void s_touch(float x, float y, unsigned char *fl) {
    int x0 = (int)floorf((x + 1) / TS), x1 = (int)floorf((x + PW - 1) / TS);
    int y0 = (int)floorf((y + 1) / TS), y1 = (int)floorf((y + PH - 1) / TS);
    for (int ty = y0; ty <= y1; ty++)
        for (int tx = x0; tx <= x1; tx++) {
            char c = tget(&SL, tx, ty);
            if (ch_gem(c) && ty >= 0 && ty < ROWS) s_gem[ty][tx] = true;
            if (c == 'K') { *fl |= SF_KEY; s_key_seen = true; }
            if (c == 'J') *fl |= SF_JET;
            if (c == 'D' && (*fl & SF_KEY)) s_done = true;
        }
}
static void s_push(float x, float y, unsigned char fl) {
    int qx = (int)(x / 4), qy = (int)(y / 2);
    if (qx < 0 || qy < 0 || qy >= ROWS * TS / 2 + 8 || qx >= MAXW * TS / 4 + 8) return;
    if (s_vis[fl][qy][qx]) return;
    s_vis[fl][qy][qx] = 1;
    if (s_qt < (int)(sizeof(s_q) / sizeof(s_q[0]))) s_q[s_qt++] = (SSt){x, y, fl};
}
/* fall with no input until standing still; false if it dies */
static bool s_settle(Body *b, unsigned char *fl) {
    Ctl none = {0};
    for (int f = 0; f < 300; f++) {
        if (b->ground && b->vx == 0) return true;
        phys_step(b, &none, &SL);
        if (b->y > ROWS * TS + 4 || box_hazard(&SL, b->x, b->y)) return false;
        s_touch(b->x, b->y, fl);
    }
    return b->ground;
}
static void s_action(const SSt *s, const Act *a) {
    Body b = {s->x, s->y, 0, 0, true, false, 6, -1, 0};
    unsigned char fl = s->fl;
    bool airborne = false;
    for (int f = 0; f < 400; f++) {
        Ctl c = {0};
        if (f < a->run) c.dx = a->dir;
        else if (a->jump > 0) {
            int af = f - a->run;
            c.jump_hit = af == 0;
            c.jump_held = af < a->jump;
            if (airborne && b.ground) c.dx = 0;
            else if (a->drift_after >= 0) c.dx = af >= a->drift_after ? a->drift_dir : 0;
            else c.dx = (a->release < 0 || af < a->release) ? a->dir : 0;
        }
        phys_step(&b, &c, &SL);
        if (!b.ground) airborne = true;
        if (b.y > ROWS * TS + 4 || box_hazard(&SL, b.x, b.y)) return;
        s_touch(b.x, b.y, &fl);
        if (f >= a->run && (a->jump == 0 ? c.dx == 0 : (airborne && b.ground))) break;
    }
    if (!s_settle(&b, &fl)) return;
    s_push(b.x, b.y, fl);
}
static void s_fly(const SSt *s) {
    static bool seen[ROWS][MAXW];
    static int qx[ROWS * MAXW], qy[ROWS * MAXW];
    memset(seen, 0, sizeof(seen));
    int h = 0, t = 0;
    int cx = (int)floorf((s->x + PW / 2.0f) / TS), cy = (int)floorf((s->y + PH / 2.0f) / TS);
    seen[cy][cx] = true;
    qx[t] = cx; qy[t++] = cy;
    unsigned char fl = s->fl;
    while (h < t) {
        int x = qx[h], y = qy[h++];
        s_touch((float)(x * TS + 3), (float)(y * TS + 1), &fl);
        static const int d[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
        for (int k = 0; k < 4; k++) {
            int nx = x + d[k][0], ny = y + d[k][1];
            if (nx < 0 || nx >= SL.w || ny < 0 || ny >= ROWS || seen[ny][nx]) continue;
            char c = SL.t[ny][nx];
            if (ch_solid(c) || ch_hazard(c)) continue;
            seen[ny][nx] = true;
            qx[t] = nx; qy[t++] = ny;
        }
    }
    for (int i = 0; i < t; i++) { /* switch the jetpack off anywhere and drop */
        Body b = {(float)(qx[i] * TS + 3), (float)(qy[i] * TS + 1), 0, 0, false, false, 0, -1, 0};
        unsigned char f2 = fl;
        if (s_settle(&b, &f2)) s_push(b.x, b.y, f2);
    }
}
static void s_build_actions(void) {
    s_nacts = 0;
    static const int runs[] = {0, 4, 10, 20}, jumps[] = {4, 9, 30}, rels[] = {-1, 8, 16, 24};
    for (int dir = -1; dir <= 1; dir += 2)
        for (int r = 0; r < 4; r++) {
            if (runs[r]) s_acts[s_nacts++] = (Act){dir, runs[r], 0, -1, -1, 0};
            for (int j = 0; j < 3; j++)
                for (int k = 0; k < 4; k++) s_acts[s_nacts++] = (Act){dir, runs[r], jumps[j], rels[k], -1, 0};
        }
    static const int drifts[] = {6, 14, 22};
    for (int j = 1; j < 3; j++)
        for (int k = 0; k < 3; k++)
            for (int dd = -1; dd <= 1; dd += 2) s_acts[s_nacts++] = (Act){0, 0, jumps[j], -1, drifts[k], dd};
}

static bool solve_level(int idx, bool no_jet) {
    level_build(&SL, idx, false, true);
    if (no_jet)
        for (int y = 0; y < ROWS; y++)
            for (int x = 0; x < SL.w; x++)
                if (SL.t[y][x] == 'J') SL.t[y][x] = '.';
    memset(s_vis, 0, sizeof(s_vis));
    memset(s_gem, 0, sizeof(s_gem));
    s_done = s_key_seen = false;
    s_qh = s_qt = 0;
    Body b = {(float)(SL.sx * TS + 3), (float)(SL.sy * TS + TS - PH), 0, 0, false, false, 0, -1, 0};
    unsigned char fl = 0;
    if (!s_settle(&b, &fl)) { printf("  start position is not safe\n"); return false; }
    s_push(b.x, b.y, fl);
    while (s_qh < s_qt && !s_done) {
        SSt s = s_q[s_qh++];
        if (s.fl & SF_JET) s_fly(&s);
        for (int a = 0; a < s_nacts && !s_done; a++) s_action(&s, &s_acts[a]);
    }
    int got = 0;
    for (int y = 0; y < ROWS; y++)
        for (int x = 0; x < SL.w; x++) if (s_gem[y][x]) got++;
    bool rows_ok = true;
    for (int r = 0; r < ROWS; r++)
        if ((int)strlen(LEVELS[idx].row[r]) != SL.w) {
            printf("  row %d has %d columns, expected %d\n", r, (int)strlen(LEVELS[idx].row[r]), SL.w);
            rows_ok = false;
        }
    if (no_jet) { printf("   without the jetpack: door %s\n", s_done ? "reachable" : "unreachable"); return s_done; }
    printf("L%d %-15s w=%-3d sunstone %-3s door %-3s gems reached %d/%d  (%d states)\n", idx + 1,
           LEVELS[idx].name, SL.w, s_key_seen ? "YES" : "NO", s_done ? "YES" : "NO", got, SL.gems, s_qt);
    if (got < SL.gems && s_done) { /* search stopped early at the door; report the rest separately */ }
    return s_done && rows_ok;
}

static int run_selftest(const char *dir) {
    char path[1200];
    int fails = 0;
    s_build_actions();
    printf("solver: %d macro-actions per standing state\n", s_nacts);
    for (int i = 0; i < NLEVELS; i++) {
        if (!solve_level(i, false)) fails++;
        bool has_jet = false;
        for (int r = 0; r < ROWS; r++) if (strchr(LEVELS[i].row[r], 'J')) has_jet = true;
        if (has_jet && solve_level(i, true) && (i == 2 || i == 3 || i == 5)) {
            printf("  FAIL: level %d was designed to need the jetpack\n", i + 1);
            fails++;
        }
    }

    rt_scores_load("relic-runner", rt_scores); /* defaults already set by caller */
#define SHOT(name) do { draw_frame(); snprintf(path, sizeof(path), "%s/%s.bmp", dir, name); rt_shot(path); } while (0)
    for (int pg = 0; pg < 4; pg++) {
        goto_title(pg);
        G.st = 60;
        char n[16];
        snprintf(n, sizeof(n), "title%d", pg);
        SHOT(n);
    }
    new_game(0);
    G.st = 30;
    SHOT("card");
    for (int lv = 0; lv < NLEVELS; lv++) {
        start_level(lv);
        G.state = S_PLAY;
        for (int f = 0; f < 90; f++) { G.tick++; update_ents(); update_fx(); }
        char n[16];
        snprintf(n, sizeof(n), "level%d", lv + 1);
        SHOT(n);
        /* a second view further in, with the Sunstone in sight */
        for (int y = 0; y < ROWS; y++)
            for (int x = 0; x < L.w; x++)
                if (L.t[y][x] == 'K') {
                    G.camx = G.camt = fminf(fmaxf(0, x * TS - 160.0f), (float)(L.w * TS - RT_W));
                }
        if (lv == 2) { G.fuel = FUEL_MAX * 2 / 3; G.p.jet = true; G.p.x = G.camx + 120; G.p.y = 70; G.p.ground = false; }
        snprintf(n, sizeof(n), "level%d_b", lv + 1);
        SHOT(n);
    }
    start_level(0);
    G.state = S_TALLY;
    G.tally_step = 3;
    G.gems_got = 9;
    G.st = 20;
    SHOT("tally");
    G.state = S_OVER;
    G.st = 100;
    SHOT("over");
    G.state = S_ENTRY;
    G.name_len = 2;
    strcpy(G.name, "RR");
    G.st = 15;
    SHOT("entry");
    G.state = S_WIN;
    for (G.st = 0; G.st < 130; G.st++) update();
    G.state = S_WIN;
    SHOT("win");
    printf("selftest: %d of %d levels completable\n", NLEVELS - fails, NLEVELS);
    return fails ? 1 : 0;
}

/* ================================================================== main */
static const RtScore DEFAULT_SCORES[RT_NSCORES] = {
    {"ROK", 40000, 6}, {"IVY", 30000, 5}, {"DIG", 22000, 4}, {"AMY", 16000, 3},
    {"JET", 11000, 3}, {"GEM", 7000, 2},  {"BAT", 4000, 1},  {"OLD", 2000, 1},
};

int main(int argc, char **argv) {
    const char *shotdir = NULL;
    int start = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--selftest") && i + 1 < argc) shotdir = argv[++i];
        else if (!strcmp(argv[i], "--level") && i + 1 < argc) start = atoi(argv[++i]) - 1;
    }
    if (start < 0 || start >= NLEVELS) start = 0;
    selftest = shotdir != NULL;
    if (!rt_init("Relic Runner")) return 1;
    load_sprites();
    memcpy(rt_scores, DEFAULT_SCORES, sizeof(rt_scores));
    G.rank = -1;
    if (selftest) return run_selftest(shotdir);

    rt_audio_init();
    rt_scores_load("relic-runner", DEFAULT_SCORES);
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
