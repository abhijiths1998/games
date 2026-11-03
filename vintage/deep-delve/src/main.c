/*
 * DEEP DELVE - an original EGA text-mode dungeon crawl for the vintage collection.
 * 40x25 colour text screen, ten procedurally generated floors, one guardian, one artefact.
 */
#include "retro.h"
#include <ctype.h>
#include <stdarg.h>

#define COLS 40
#define SROWS 25
#define MW 78
#define MH 40
#define VW 40
#define VH 20
#define VY 2
#define MAXMON 64
#define MAXITEM 120
#define NDEPTH 10
#define INVMAX 20
#define LOGN 64
#define HMAX 2400

/* ================================================================== glyphs */
enum { G_WALL = 1, G_FLOOR, G_DOOR, G_ODOOR, G_STAIRS, G_PLAYER, G_GOLD, G_POTION, G_SCROLL, G_WAND,
       G_WEAPON, G_ARMOR, G_FOOD, G_KNIFE, G_ARTEFACT, G_RETICLE, G_PEDESTAL, G_FLAME1, G_FLAME2,
       G_BOLT, G_HEART, G_CORPSE };
enum { BX_H = 128, BX_V, BX_TL, BX_TR, BX_BL, BX_BR, SH_FULL, SH_DARK, SH_MID, SH_LIGHT, HALF_UP, HALF_DN };

#define A8(a, b, c, d, e, f, g, h) a b c d e f g h
typedef struct { int code; const char *art; } SymDef;
static const SymDef SYMS[] = {
    {G_WALL, A8("###.####", "###.####", "###.####", "........", "####.###", "####.###", "####.###", "........")},
    {G_FLOOR, A8("........", "........", "........", "...##...", "...##...", "........", "........", "........")},
    {G_DOOR, A8(".######.", ".#....#.", ".#....#.", ".#....#.", ".#..#.#.", ".#....#.", ".#....#.", ".######.")},
    {G_ODOOR, A8(".######.", "##....##", "#......#", "#......#", "#......#", "#......#", "#......#", "#......#")},
    {G_STAIRS, A8("##......", "##......", "####....", "####....", "######..", "######..", "########", "########")},
    {G_PLAYER, A8("..####..", ".##..##.", ".##.###.", ".##.###.", ".##.....", ".##...#.", "..####..", "........")},
    {G_GOLD, A8("........", "..####..", ".######.", ".##..##.", ".##..##.", ".######.", "..####..", "........")},
    {G_POTION, A8("..####..", "...##...", "...##...", "..####..", ".######.", ".######.", ".######.", "..####..")},
    {G_SCROLL, A8("........", ".######.", "#.####.#", "#......#", "#.####.#", ".######.", "........", "........")},
    {G_WAND, A8(".....###", "....###.", "...###..", "..###...", ".###....", "###.....", "##......", "#.......")},
    {G_WEAPON, A8("......##", ".....###", "....###.", "#..###..", ".####...", "..##....", ".#.##...", "#...#...")},
    {G_ARMOR, A8(".##..##.", "########", "########", ".######.", ".######.", ".######.", "..####..", "..#..#..")},
    {G_FOOD, A8("........", "..####..", ".######.", "########", "########", ".######.", "..####..", "........")},
    {G_KNIFE, A8("...##...", "...##...", "...##...", "...##...", "...##...", ".######.", "...##...", "...##...")},
    {G_ARTEFACT, A8("#..##..#", "#..##..#", "##.##.##", "########", "########", ".######.", "..####..", "........")},
    {G_RETICLE, A8("##....##", "#......#", "........", "........", "........", "........", "#......#", "##....##")},
    {G_PEDESTAL, A8("........", "........", "........", ".######.", "########", "########", "########", "........")},
    {G_FLAME1, A8("...#....", "..##....", "..###...", ".####...", ".#####..", ".#####..", "..###...", "...#....")},
    {G_FLAME2, A8("....#...", "....##..", "...###..", "...####.", "..#####.", "..#####.", "...###..", "....#...")},
    {G_BOLT, A8("........", "........", "..####..", ".######.", ".######.", "..####..", "........", "........")},
    {G_HEART, A8("........", ".##..##.", "########", "########", "########", ".######.", "..####..", "...##...")},
    {G_CORPSE, A8("........", "........", "........", "........", ".#.##.#.", "..####..", ".######.", "........")},
};
static const unsigned char LOWER[26][7] = {
    {0, 0, 14, 1, 15, 17, 15},   {16, 16, 30, 17, 17, 17, 30}, {0, 0, 14, 16, 16, 17, 14},  {1, 1, 15, 17, 17, 17, 15},
    {0, 0, 14, 17, 31, 16, 14},  {6, 9, 8, 28, 8, 8, 8},       {0, 15, 17, 17, 15, 1, 14},  {16, 16, 22, 25, 17, 17, 17},
    {4, 0, 12, 4, 4, 4, 14},     {2, 0, 6, 2, 2, 18, 12},      {16, 16, 18, 20, 24, 20, 18}, {12, 4, 4, 4, 4, 4, 14},
    {0, 0, 26, 21, 21, 17, 17},  {0, 0, 22, 25, 17, 17, 17},   {0, 0, 14, 17, 17, 17, 14},  {0, 30, 17, 17, 30, 16, 16},
    {0, 15, 17, 17, 15, 1, 1},   {0, 0, 22, 25, 16, 16, 16},   {0, 0, 15, 16, 14, 1, 30},   {8, 8, 28, 8, 8, 9, 6},
    {0, 0, 17, 17, 17, 19, 13},  {0, 0, 17, 17, 17, 10, 4},    {0, 0, 17, 17, 21, 21, 10},  {0, 0, 17, 10, 4, 10, 17},
    {0, 17, 17, 17, 15, 1, 14},  {0, 0, 31, 2, 4, 8, 31},
};

static SDL_Texture *atlas;

static void box_bits(int ch, unsigned char *r) {
#define HL(row, c0, c1) for (int c_ = (c0); c_ <= (c1); c_++) r[row] |= (unsigned char)(0x80 >> c_)
#define VL(col, r0, r1) for (int q_ = (r0); q_ <= (r1); q_++) r[q_] |= (unsigned char)(0x80 >> (col))
    switch (ch) {
    case BX_H: HL(2, 0, 7); HL(5, 0, 7); break;
    case BX_V: VL(2, 0, 7); VL(5, 0, 7); break;
    case BX_TL: HL(2, 2, 7); HL(5, 5, 7); VL(2, 2, 7); VL(5, 5, 7); break;
    case BX_TR: HL(2, 0, 5); HL(5, 0, 2); VL(5, 2, 7); VL(2, 5, 7); break;
    case BX_BL: HL(5, 2, 7); HL(2, 5, 7); VL(2, 0, 5); VL(5, 0, 2); break;
    case BX_BR: HL(5, 0, 5); HL(2, 0, 2); VL(5, 0, 5); VL(2, 0, 2); break;
    case SH_FULL: for (int i = 0; i < 8; i++) r[i] = 0xFF; break;
    case SH_DARK: for (int i = 0; i < 8; i++) r[i] = i & 1 ? 0xBB : 0xEE; break;
    case SH_MID: for (int i = 0; i < 8; i++) r[i] = i & 1 ? 0x55 : 0xAA; break;
    case SH_LIGHT: for (int i = 0; i < 8; i++) r[i] = i & 1 ? 0x22 : 0x88; break;
    case HALF_UP: for (int i = 0; i < 4; i++) r[i] = 0xFF; break;
    case HALF_DN: for (int i = 4; i < 8; i++) r[i] = 0xFF; break;
    }
#undef HL
#undef VL
}

static void get_glyph(int ch, unsigned char *r) {
    memset(r, 0, 8);
    for (size_t i = 0; i < sizeof(SYMS) / sizeof(SYMS[0]); i++)
        if (SYMS[i].code == ch)
            for (int y = 0; y < 8; y++)
                for (int x = 0; x < 8; x++)
                    if (SYMS[i].art[y * 8 + x] == '#') r[y] |= (unsigned char)(0x80 >> x);
    if (ch >= 128) { box_bits(ch, r); return; }
    const unsigned char *g = NULL;
    if (ch >= 'a' && ch <= 'z') g = LOWER[ch - 'a'];
    else if (ch >= 32 && ch < 128) g = rt_glyph((char)ch);
    if (!g) return;
    for (int row = 0; row < 7; row++)
        for (int col = 0; col < 5; col++)
            if (g[row] & (16 >> col)) r[row] |= (unsigned char)((0x80 >> (1 + col)) | (0x80 >> (2 + col)));
}

static void build_atlas(void) {
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, 128, 128, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!s) return;
    for (int y = 0; y < 128; y++) memset((Uint8 *)s->pixels + y * s->pitch, 0, 128 * 4);
    for (int ch = 0; ch < 256; ch++) {
        unsigned char r[8];
        get_glyph(ch, r);
        for (int y = 0; y < 8; y++)
            for (int x = 0; x < 8; x++)
                if (r[y] & (0x80 >> x)) {
                    Uint32 *px = (Uint32 *)((Uint8 *)s->pixels + ((ch / 16) * 8 + y) * s->pitch);
                    px[(ch % 16) * 8 + x] = 0xFFFFFFFFu;
                }
    }
    atlas = SDL_CreateTextureFromSurface(rt_ren, s);
    SDL_FreeSurface(s);
    if (atlas) SDL_SetTextureBlendMode(atlas, SDL_BLENDMODE_BLEND);
}

/* ================================================================== text-mode screen */
static unsigned char sc[SROWS][COLS], sf[SROWS][COLS], sb[SROWS][COLS];

static void scr_clear(void) {
    memset(sc, ' ', sizeof(sc));
    memset(sf, C_LGRAY, sizeof(sf));
    memset(sb, C_BLACK, sizeof(sb));
}
static void put(int x, int y, int ch, int fg, int bg) {
    if (x < 0 || y < 0 || x >= COLS || y >= SROWS) return;
    sc[y][x] = (unsigned char)ch;
    sf[y][x] = (unsigned char)fg;
    if (bg >= 0) sb[y][x] = (unsigned char)bg;
}
static void puts_(int x, int y, const char *s, int fg, int bg) {
    for (; *s; s++, x++) put(x, y, toupper((unsigned char)*s), fg, bg);
}
static void putsc(int y, const char *s, int fg, int bg) { puts_((COLS - (int)strlen(s)) / 2, y, s, fg, bg); }
static void putf(int x, int y, int fg, int bg, const char *fmt, ...) {
    char b[96];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof(b), fmt, ap);
    va_end(ap);
    puts_(x, y, b, fg, bg);
}
static void fillr(int x, int y, int w, int h, int ch, int fg, int bg) {
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++) put(x + i, y + j, ch, fg, bg);
}
static void panel(int x, int y, int w, int h, int fg, int bg, const char *title) {
    fillr(x, y, w, h, ' ', fg, bg);
    for (int i = 1; i < w - 1; i++) { put(x + i, y, BX_H, fg, bg); put(x + i, y + h - 1, BX_H, fg, bg); }
    for (int j = 1; j < h - 1; j++) { put(x, y + j, BX_V, fg, bg); put(x + w - 1, y + j, BX_V, fg, bg); }
    put(x, y, BX_TL, fg, bg); put(x + w - 1, y, BX_TR, fg, bg);
    put(x, y + h - 1, BX_BL, fg, bg); put(x + w - 1, y + h - 1, BX_BR, fg, bg);
    if (title) {
        int tl = (int)strlen(title);
        int tx = x + (w - tl) / 2;
        put(tx - 1, y, ' ', fg, bg);
        puts_(tx, y, title, C_YELLOW, bg);
        put(tx + tl, y, ' ', fg, bg);
    }
}
static void scr_flush(void) {
    for (int y = 0; y < SROWS; y++)
        for (int x = 0; x < COLS; x++) {
            if (sb[y][x] != C_BLACK) rt_rect(x * 8, y * 8, 8, 8, sb[y][x]);
            int ch = sc[y][x];
            if (ch == ' ' || ch == 0 || !atlas) continue;
            SDL_Color c = rt_pal[sf[y][x]];
            SDL_SetTextureColorMod(atlas, c.r, c.g, c.b);
            SDL_Rect src = {(ch % 16) * 8, (ch / 16) * 8, 8, 8}, dst = {x * 8, y * 8, 8, 8};
            SDL_RenderCopy(rt_ren, atlas, &src, &dst);
        }
}

/* ================================================================== data tables */
enum { T_WALL, T_FLOOR, T_DOOR, T_ODOOR, T_STAIRS };
enum { IK_GOLD, IK_POTION, IK_SCROLL, IK_WAND, IK_WEAPON, IK_ARMOR, IK_FOOD, IK_KNIFE, IK_ARTEFACT };
enum { P_HEAL, P_VIGOR, P_VENOM, P_HASTE, P_MIGHT, P_INSIGHT, P_TORPOR, P_BLAZE, NPOT };
enum { S_TELE, S_MAP, S_ENCW, S_ENCA, S_SMITE, S_DREAD, S_SUMMON, S_LORE, NSCR };
enum { W_SPARK, W_FROST, W_TERROR, NWAND };

static const char *POT_NAME[NPOT] = {"HEALING", "VIGOR", "VENOM", "HASTE", "MIGHT", "INSIGHT", "TORPOR", "BLAZE"};
static const int POT_W[NPOT] = {30, 6, 10, 10, 6, 8, 8, 8};
static const char *POT_LOOK[NPOT] = {"RUBY", "AMBER", "MURKY", "MILKY", "VIOLET", "SMOKY", "TEAL", "SILVER"};
static const int POT_COL[NPOT] = {C_LRED, C_YELLOW, C_DGRAY, C_WHITE, C_LMAGENTA, C_LGRAY, C_CYAN, C_LCYAN};
static const char *SCR_NAME[NSCR] = {"TELEPORT", "MAPPING", "SHARPENING", "WARDING", "SMITING", "DREAD", "SUMMONING", "LORE"};
static const int SCR_W[NSCR] = {10, 14, 12, 12, 10, 8, 8, 10};
static const char *WAND_NAME[NWAND] = {"SPARKS", "FROST", "TERROR"};
static const char *WAND_LOOK[5] = {"YEW", "BONE", "IRON", "GLASS", "COPPER"};
static const int WAND_COL[5] = {C_BROWN, C_WHITE, C_LGRAY, C_LCYAN, C_LRED};
static const char *SYL[] = {"ZU", "KAR", "MEL", "OR", "VEX", "DAN", "IL", "POR", "THU", "NEB", "RAX", "SOL", "KI", "YEN", "BRO", "QUA"};

typedef struct { const char *name; int mn, mx; } WeapDef;
static const WeapDef WEAP[] = {{"CUDGEL", 2, 4}, {"DIRK", 2, 5}, {"HATCHET", 3, 6}, {"LONGBLADE", 4, 8}, {"WARPICK", 5, 10}, {"GREATAXE", 7, 12}};
#define NWEAP 6
typedef struct { const char *name; int ac; } ArmDef;
static const ArmDef ARM[] = {{"RAGS", 0}, {"LEATHER VEST", 1}, {"STUDDED COAT", 2}, {"RING MAIL", 3}, {"SCALE HAUBERK", 4}, {"PLATE HARNESS", 6}};
#define NARM 6
typedef struct { const char *name; int nut; } FoodDef;
static const FoodDef FOOD[] = {{"TRAIL BREAD", 900}, {"DRIED FRUIT", 500}, {"SMOKED RATION", 1500}};

enum { F_PACK = 1, F_ERRATIC = 2, F_RANGED = 4, F_KEEP = 8, F_SLEEPY = 16, F_REGEN = 32, F_STILL = 64,
       F_POISON = 128, F_DRAIN = 256, F_BOSS = 512 };
typedef struct {
    const char *name, *verb, *rverb;
    char ch;
    unsigned char col;
    short hp, dmin, dmax, def, speed, xp, d0, d1, range;
    unsigned flags;
} MonDef;
static const MonDef MD[] = {
    {"mire rat", "bites", "", 'r', C_BROWN, 4, 1, 2, 0, 100, 4, 1, 4, 0, F_PACK},
    {"flit moth", "flutters at", "", 'm', C_LMAGENTA, 4, 1, 2, 0, 150, 5, 1, 4, 0, F_ERRATIC},
    {"pale grub", "gnaws", "", 'g', C_LGRAY, 9, 1, 3, 0, 60, 6, 1, 3, 0, 0},
    {"spore puff", "spatters", "", 'p', C_LGREEN, 10, 1, 2, 0, 100, 8, 2, 6, 0, F_STILL | F_POISON},
    {"rust beetle", "gnashes at", "", 'B', C_RED, 16, 2, 4, 2, 70, 14, 2, 6, 0, 0},
    {"hollow acolyte", "slaps", "hurls a dark bolt at", 'c', C_MAGENTA, 12, 2, 5, 0, 100, 18, 3, 7, 6, F_RANGED | F_KEEP},
    {"lantern imp", "scratches", "spits an ember at", 'i', C_YELLOW, 9, 2, 5, 0, 110, 16, 3, 7, 6, F_RANGED | F_KEEP},
    {"gloom hound", "bites", "", 'd', C_CYAN, 14, 3, 6, 1, 130, 20, 4, 8, 0, F_PACK},
    {"bone gleaner", "rakes", "", 's', C_WHITE, 20, 3, 7, 2, 100, 24, 4, 8, 0, 0},
    {"hex weaver", "bites", "spits venom at", 'x', C_LRED, 24, 3, 6, 1, 100, 35, 5, 9, 5, F_RANGED | F_POISON},
    {"pit stalker", "mauls", "", 'S', C_LBLUE, 22, 4, 9, 1, 120, 30, 5, 9, 0, F_SLEEPY},
    {"moss hulk", "pounds", "", 'H', C_GREEN, 45, 4, 9, 2, 60, 40, 5, 10, 0, F_REGEN},
    {"veil wraith", "chills", "", 'W', C_LCYAN, 26, 4, 8, 3, 100, 45, 6, 10, 0, F_DRAIN},
    {"stone warden", "crushes", "", 'G', C_LGRAY, 60, 6, 12, 5, 50, 60, 6, 10, 0, 0},
    {"ember drake", "claws", "breathes fire at", 'D', C_LRED, 55, 5, 10, 3, 100, 80, 7, 10, 7, F_RANGED},
    {"hollow king", "strikes", "hurls ghostfire at", 'K', C_YELLOW, 150, 7, 13, 5, 100, 500, 11, 11, 6, F_RANGED | F_BOSS},
};
#define NMD ((int)(sizeof(MD) / sizeof(MD[0])))
#define MD_KING (NMD - 1)

/* ================================================================== state */
typedef struct { unsigned char kind, sub; signed char ench; short qty, chg; } Item;
typedef struct { Item it; int x, y; bool on; } FItem;
typedef struct { int type, x, y, hp, energy, asleep, stun, slow, fear, aware, cd; bool alive; } Mon;
typedef struct { int x, y, w, h; } Room;
typedef struct { int x, y, ch, col, ttl, delay; } Fx;

enum { S_TITLE, S_PLAY, S_INV, S_PICK, S_TARGET, S_LOG, S_OVER, S_ENTRY, S_WIN };

static unsigned char tile[MH][MW], seen[MH][MW], vis[MH][MW];
static short sd[MH][MW];      /* distance from the level start, -1 = unreachable */
static unsigned char dmap[MH][MW]; /* distance to the player for monster pathing */
static Mon mons[MAXMON];
static FItem fit[MAXITEM];
static Room rooms[20];
static int nrooms;
static bool cave_level;
static Fx fxs[48];

static struct {
    int state, st, tick, title_page;
    int px, py, hp, maxhp, lvl, xp, gold, kills, depth, hunger, poison, haste, sleep, sense, might, turn;
    Item inv[INVMAX];
    int eqw, eqa;
    bool pot_known[NPOT], scr_known[NSCR], wand_known[NWAND];
    int pot_look[NPOT], wand_look[NWAND];
    char scr_title[NSCR][16];
    bool dead, won, quitq, god;
    char killer[40];
    int rank, name_len, letter;
    char name[4];
    int cur;          /* menu cursor */
    int pick_mode;    /* 0 throw/zap */
    int tgt_slot, tx, ty;
    int nextspawn, rest_stop, sx, sy;
    int pad_rep;
    int last_dmg_flash;
} G;
static char logt[LOGN][41];
static unsigned char logc[LOGN];
static int logturn[LOGN], logn;
static bool selftest;

static int rnd(int n) { return n > 0 ? (int)(rt_rand() % (Uint32)n) : 0; }
static int rr(int a, int b) { return a + rnd(b - a + 1); }
static int imin(int a, int b) { return a < b ? a : b; }
static int imax(int a, int b) { return a > b ? a : b; }
static int iabs(int a) { return a < 0 ? -a : a; }
static int cheb(int x0, int y0, int x1, int y1) { return imax(iabs(x0 - x1), iabs(y0 - y1)); }

/* ================================================================== sound */
static float semi(int s) { return 440.0f * powf(2.0f, s / 12.0f); }
static void sfx_hit(void) { rt_tone(320, 110, 55, 2); }
static void sfx_miss(void) { rt_tone(180, 130, 35, 1); }
static void sfx_hurt(void) { RtNote n[] = {{200, 90, 70}, {0, 0, 10}, {150, 70, 60}}; rt_play(n, 3, 4); }
static void sfx_kill(void) { RtNote n[] = {{700, 200, 60}, {0, 0, 10}, {330, 90, 80}}; rt_play(n, 3, 4); }
static void sfx_pickup(void) { RtNote n[] = {{660, 660, 35}, {880, 880, 50}}; rt_play(n, 2, 3); }
static void sfx_gold(void) { RtNote n[] = {{988, 988, 30}, {1319, 1319, 50}}; rt_play(n, 2, 3); }
static void sfx_door(void) { rt_tone(110, 150, 40, 1); }
static void sfx_quaff(void) { RtNote n[] = {{300, 500, 40}, {500, 300, 40}, {300, 600, 50}}; rt_play(n, 3, 3); }
static void sfx_read(void) { rt_tone(500, 1100, 120, 3); }
static void sfx_eat(void) { RtNote n[] = {{240, 240, 30}, {0, 0, 20}, {240, 240, 30}}; rt_play(n, 3, 2); }
static void sfx_zap(void) { rt_tone(1500, 250, 110, 4); }
static void sfx_throw(void) { rt_tone(900, 500, 50, 2); }
static void sfx_select(void) { rt_tone(660, 990, 50, 4); }
static void sfx_tick(void) { rt_tone(1500, 1500, 10, 1); }
static void sfx_stairs(void) {
    RtNote n[6];
    for (int i = 0; i < 6; i++) { n[i] = (RtNote){semi(7 - i * 2), semi(7 - i * 2), 55}; }
    rt_play(n, 6, 6);
}
static void sfx_levelup(void) {
    static const int m[] = {-5, 0, 4, 7, 12};
    RtNote n[10];
    for (int i = 0; i < 5; i++) { n[i * 2] = (RtNote){semi(m[i]), semi(m[i]), i == 4 ? 200 : 65}; n[i * 2 + 1] = (RtNote){0, 0, 10}; }
    rt_play(n, 10, 7);
}
static void tune_title(void) {
    static const int mel[][2] = {
        {-12, 2}, {-9, 2}, {-5, 2}, {-9, 2}, {-12, 2}, {-13, 2}, {-12, 4}, {-7, 2}, {-4, 2}, {0, 2}, {-4, 2}, {-7, 2}, {-8, 2}, {-7, 4},
        {-5, 2}, {-2, 2}, {2, 2}, {-2, 2}, {-5, 2}, {-9, 2}, {-12, 4}, {-17, 2}, {-14, 2}, {-12, 2}, {-9, 2}, {-12, 6}, {-24, 8},
    };
    RtNote n[RT_QMAX];
    int k = 0;
    for (int i = 0; i < (int)(sizeof(mel) / sizeof(mel[0])) && k < RT_QMAX - 1; i++) {
        float f = semi(mel[i][0]);
        n[k++] = (RtNote){f, f, mel[i][1] * 75 - 20};
        n[k++] = (RtNote){0, 0, 20};
    }
    rt_play(n, k, 2);
}
static void tune_over(void) {
    static const int mel[] = {-5, -6, -8, -9, -12, -15, -24};
    RtNote n[14];
    for (int i = 0; i < 7; i++) {
        n[i * 2] = (RtNote){semi(mel[i]), semi(mel[i]), i == 6 ? 600 : 200};
        n[i * 2 + 1] = (RtNote){0, 0, 30};
    }
    rt_play(n, 14, 9);
}
static void tune_win(void) {
    static const int mel[][2] = {{0, 2}, {4, 2}, {7, 2}, {12, 4}, {7, 2}, {12, 2}, {16, 6}, {14, 2}, {12, 2}, {11, 2}, {12, 8}};
    RtNote n[22];
    for (int i = 0; i < 11; i++) {
        n[i * 2] = (RtNote){semi(mel[i][0] - 5), semi(mel[i][0] - 5), mel[i][1] * 80};
        n[i * 2 + 1] = (RtNote){0, 0, 20};
    }
    rt_play(n, 22, 9);
}

/* ================================================================== messages */
static void msg_push(const char *s, int col) {
    int i = logn % LOGN;
    snprintf(logt[i], sizeof(logt[i]), "%.40s", s);
    logc[i] = (unsigned char)col;
    logturn[i] = G.turn;
    logn++;
}
static void msgf(int col, const char *fmt, ...) {
    char buf[200];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    for (char *p = buf; *p; p++) *p = (char)toupper((unsigned char)*p);
    char *s = buf;
    bool first = true;
    while (*s) {
        char line[96];
        int len = (int)strlen(s), lim = first ? 39 : 38;
        if (len <= lim) { snprintf(line, sizeof(line), "%s%s", first ? "" : " ", s); msg_push(line, col); break; }
        int cut = lim;
        while (cut > 0 && s[cut] != ' ') cut--;
        if (cut == 0) cut = lim;
        char tmp[48];
        memcpy(tmp, s, (size_t)cut);
        tmp[cut] = 0;
        snprintf(line, sizeof(line), "%s%s", first ? "" : " ", tmp);
        msg_push(line, col);
        s += cut;
        while (*s == ' ') s++;
        first = false;
    }
}
#define MSG(...) msgf(C_WHITE, __VA_ARGS__)
#define MSGC(c, ...) msgf((c), __VA_ARGS__)

/* ================================================================== map helpers */
static bool inmap(int x, int y) { return x >= 0 && y >= 0 && x < MW && y < MH; }
static bool passable(int x, int y) { return inmap(x, y) && tile[y][x] != T_WALL; }
static bool opaque(int x, int y) { return !inmap(x, y) || tile[y][x] == T_WALL || tile[y][x] == T_DOOR; }
static bool is_door(int x, int y) { return tile[y][x] == T_DOOR || tile[y][x] == T_ODOOR; }
static bool can_step(int x, int y, int dx, int dy) {
    int nx = x + dx, ny = y + dy;
    if (!passable(nx, ny)) return false;
    if (dx && dy && (is_door(x, y) || is_door(nx, ny))) return false;
    return true;
}
static Mon *mon_at(int x, int y) {
    for (int i = 0; i < MAXMON; i++)
        if (mons[i].alive && mons[i].x == x && mons[i].y == y) return &mons[i];
    return NULL;
}
static int fitem_at(int x, int y) {
    for (int i = 0; i < MAXITEM; i++)
        if (fit[i].on && fit[i].x == x && fit[i].y == y) return i;
    return -1;
}

/* Bresenham path from (x0,y0) toward (x1,y1); writes up to max cells (excluding the start) */
static int line_path(int x0, int y0, int x1, int y1, int *xs, int *ys, int max) {
    int dx = iabs(x1 - x0), dy = -iabs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    int err = dx + dy, n = 0;
    while (n < max && !(x0 == x1 && y0 == y1)) {
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
        xs[n] = x0;
        ys[n] = y0;
        n++;
    }
    return n;
}
static bool los(int x0, int y0, int x1, int y1) {
    int xs[64], ys[64];
    int n = line_path(x0, y0, x1, y1, xs, ys, 64);
    for (int i = 0; i < n - 1; i++)
        if (opaque(xs[i], ys[i])) return false;
    return true;
}

static void update_fov(void) {
    memset(vis, 0, sizeof(vis));
    const int R = 9;
    for (int y = G.py - R; y <= G.py + R; y++)
        for (int x = G.px - R; x <= G.px + R; x++) {
            if (!inmap(x, y)) continue;
            int dx = x - G.px, dy = y - G.py;
            if (dx * dx + dy * dy > R * R + R) continue;
            if (los(G.px, G.py, x, y)) { vis[y][x] = 1; seen[y][x] = 1; }
        }
}

static void compute_dmap(void) {
    static short q[MW * MH];
    memset(dmap, 255, sizeof(dmap));
    int qh = 0, qt = 0;
    q[qt++] = (short)(G.py * MW + G.px);
    dmap[G.py][G.px] = 0;
    while (qh < qt) {
        int c = q[qh++], x = c % MW, y = c / MW, d = dmap[y][x];
        if (d >= 40) continue;
        for (int k = 0; k < 8; k++) {
            static const int DX[8] = {1, -1, 0, 0, 1, 1, -1, -1}, DY[8] = {0, 0, 1, -1, 1, -1, 1, -1};
            int nx = x + DX[k], ny = y + DY[k];
            if (!inmap(nx, ny) || dmap[ny][nx] != 255 || !can_step(x, y, DX[k], DY[k])) continue;
            dmap[ny][nx] = (unsigned char)(d + 1);
            q[qt++] = (short)(ny * MW + nx);
        }
    }
}

/* BFS distance from (sx,sy) over passable tiles into sd[][] */
static int bfs_from(int sx, int sy) {
    static short q[MW * MH];
    static const int DX[8] = {1, -1, 0, 0, 1, 1, -1, -1}, DY[8] = {0, 0, 1, -1, 1, -1, 1, -1};
    for (int y = 0; y < MH; y++)
        for (int x = 0; x < MW; x++) sd[y][x] = -1;
    int qh = 0, qt = 0, cnt = 0;
    q[qt++] = (short)(sy * MW + sx);
    sd[sy][sx] = 0;
    while (qh < qt) {
        int c = q[qh++], x = c % MW, y = c / MW;
        cnt++;
        for (int k = 0; k < 8; k++) {
            int nx = x + DX[k], ny = y + DY[k];
            if (!inmap(nx, ny) || sd[ny][nx] != -1 || !can_step(x, y, DX[k], DY[k])) continue;
            sd[ny][nx] = (short)(sd[y][x] + 1);
            q[qt++] = (short)(ny * MW + nx);
        }
    }
    return cnt;
}

/* ================================================================== level generation */
static bool rooms_overlap(const Room *a, int x, int y, int w, int h) {
    return !(x + w + 2 <= a->x || a->x + a->w + 2 <= x || y + h + 2 <= a->y || a->y + a->h + 2 <= y);
}
static void carve_corridor(int x0, int y0, int x1, int y1) {
    int x = x0, y = y0;
    bool hfirst = rnd(2);
    for (int pass = 0; pass < 2; pass++) {
        bool horiz = (pass == 0) == hfirst;
        if (horiz) { while (x != x1) { x += x < x1 ? 1 : -1; if (tile[y][x] == T_WALL) tile[y][x] = T_FLOOR; } }
        else { while (y != y1) { y += y < y1 ? 1 : -1; if (tile[y][x] == T_WALL) tile[y][x] = T_FLOOR; } }
    }
}
static void gen_rooms(int depth) {
    memset(tile, T_WALL, sizeof(tile));
    nrooms = 0;
    int want = 8 + rnd(5);
    for (int tries = 0; tries < 200 && nrooms < want; tries++) {
        int w = rr(4, 11), h = rr(3, 6);
        if (depth == NDEPTH && nrooms == 0) { w = 9; h = 5; }
        int x = rr(2, MW - w - 3), y = rr(2, MH - h - 3);
        bool ok = true;
        for (int i = 0; i < nrooms; i++)
            if (!rooms_overlap(&rooms[i], x, y, w, h)) continue; else { ok = false; break; }
        if (!ok) continue;
        rooms[nrooms++] = (Room){x, y, w, h};
        for (int j = y; j < y + h; j++)
            for (int i = x; i < x + w; i++) tile[j][i] = T_FLOOR;
    }
    for (int i = 1; i < nrooms; i++)
        carve_corridor(rooms[i - 1].x + rooms[i - 1].w / 2, rooms[i - 1].y + rooms[i - 1].h / 2,
                       rooms[i].x + rooms[i].w / 2, rooms[i].y + rooms[i].h / 2);
    for (int k = 0; k < nrooms / 3; k++) {
        int a = rnd(nrooms), b = rnd(nrooms);
        if (a != b)
            carve_corridor(rooms[a].x + rooms[a].w / 2, rooms[a].y + rooms[a].h / 2,
                           rooms[b].x + rooms[b].w / 2, rooms[b].y + rooms[b].h / 2);
    }
    /* doors where corridors meet room walls */
    for (int i = 0; i < nrooms; i++) {
        Room *r = &rooms[i];
        for (int x = r->x; x < r->x + r->w; x++) {
            int ys[2] = {r->y - 1, r->y + r->h};
            for (int k = 0; k < 2; k++) {
                int y = ys[k];
                if (tile[y][x] == T_FLOOR && tile[y][x - 1] == T_WALL && tile[y][x + 1] == T_WALL && rnd(100) < 70) tile[y][x] = T_DOOR;
            }
        }
        for (int y = r->y; y < r->y + r->h; y++) {
            int xs[2] = {r->x - 1, r->x + r->w};
            for (int k = 0; k < 2; k++) {
                int x = xs[k];
                if (tile[y][x] == T_FLOOR && tile[y - 1][x] == T_WALL && tile[y + 1][x] == T_WALL && rnd(100) < 70) tile[y][x] = T_DOOR;
            }
        }
    }
    cave_level = false;
}
static bool gen_cave(void) {
    static unsigned char tmp[MH][MW];
    memset(tile, T_WALL, sizeof(tile));
    for (int y = 1; y < MH - 1; y++)
        for (int x = 1; x < MW - 1; x++) tile[y][x] = rnd(100) < 44 ? T_WALL : T_FLOOR;
    for (int it = 0; it < 5; it++) {
        memcpy(tmp, tile, sizeof(tmp));
        for (int y = 1; y < MH - 1; y++)
            for (int x = 1; x < MW - 1; x++) {
                int n = 0;
                for (int j = -1; j <= 1; j++)
                    for (int i = -1; i <= 1; i++) n += tmp[y + j][x + i] == T_WALL;
                tile[y][x] = n >= 5 ? T_WALL : T_FLOOR;
            }
    }
    /* keep the largest connected region */
    int best = 0, bx = -1, by = -1;
    static unsigned char done[MH][MW];
    memset(done, 0, sizeof(done));
    for (int y = 1; y < MH - 1; y++)
        for (int x = 1; x < MW - 1; x++)
            if (tile[y][x] == T_FLOOR && !done[y][x]) {
                int c = bfs_from(x, y);
                for (int j = 0; j < MH; j++)
                    for (int i = 0; i < MW; i++) if (sd[j][i] >= 0) done[j][i] = 1;
                if (c > best) { best = c; bx = x; by = y; }
            }
    if (best < 380) return false;
    bfs_from(bx, by);
    for (int y = 0; y < MH; y++)
        for (int x = 0; x < MW; x++)
            if (sd[y][x] < 0) tile[y][x] = T_WALL;
    nrooms = 0;
    cave_level = true;
    return true;
}

static int potion_pick(void) {
    int tot = 0;
    for (int i = 0; i < NPOT; i++) tot += POT_W[i];
    int r = rnd(tot);
    for (int i = 0; i < NPOT; i++) { if (r < POT_W[i]) return i; r -= POT_W[i]; }
    return 0;
}
static int scroll_pick(void) {
    int tot = 0;
    for (int i = 0; i < NSCR; i++) tot += SCR_W[i];
    int r = rnd(tot);
    for (int i = 0; i < NSCR; i++) { if (r < SCR_W[i]) return i; r -= SCR_W[i]; }
    return 0;
}
static Item make_item(int depth) {
    Item it = {0, 0, 0, 1, 0};
    int r = rnd(100);
    if (r < 26) { it.kind = IK_POTION; it.sub = (unsigned char)potion_pick(); }
    else if (r < 46) { it.kind = IK_SCROLL; it.sub = (unsigned char)scroll_pick(); }
    else if (r < 58) { it.kind = IK_FOOD; it.sub = (unsigned char)(rnd(100) < 30 ? 2 : rnd(2)); }
    else if (r < 68) {
        it.kind = IK_WEAPON;
        int lim = imin(NWEAP, 2 + depth / 2 + (rnd(4) == 0));
        it.sub = (unsigned char)rnd(lim);
        int e = rnd(100);
        it.ench = (signed char)(e < 60 ? 0 : e < 78 ? 1 : e < 88 ? 2 : -1);
    } else if (r < 78) {
        it.kind = IK_ARMOR;
        int lim = imin(NARM, 2 + depth / 2 + (rnd(4) == 0));
        it.sub = (unsigned char)rnd(lim);
        int e = rnd(100);
        it.ench = (signed char)(e < 60 ? 0 : e < 78 ? 1 : e < 88 ? 2 : -1);
        if (it.sub == 0) it.sub = 1;
    } else if (r < 85) { it.kind = IK_WAND; it.sub = (unsigned char)rnd(NWAND); it.chg = (short)rr(3, 6); }
    else { it.kind = IK_KNIFE; it.qty = (short)rr(3, 6); }
    return it;
}
static void add_floor(Item it, int x, int y) {
    for (int i = 0; i < MAXITEM; i++)
        if (!fit[i].on) { fit[i] = (FItem){it, x, y, true}; return; }
}
static bool random_spot(int mind, int *ox, int *oy, bool avoid_items) {
    for (int t = 0; t < 400; t++) {
        int x = rr(1, MW - 2), y = rr(1, MH - 2);
        if (tile[y][x] != T_FLOOR || sd[y][x] < mind) continue;
        if (mon_at(x, y) || (x == G.px && y == G.py)) continue;
        if (avoid_items && fitem_at(x, y) >= 0) continue;
        *ox = x; *oy = y;
        return true;
    }
    return false;
}
static Mon *spawn_mon(int type, int x, int y, bool awake) {
    for (int i = 0; i < MAXMON; i++)
        if (!mons[i].alive) {
            Mon *m = &mons[i];
            memset(m, 0, sizeof(*m));
            m->alive = true; m->type = type; m->x = x; m->y = y; m->hp = MD[type].hp;
            m->asleep = !awake && ((MD[type].flags & F_SLEEPY) || rnd(100) < 55);
            m->aware = awake;
            return m;
        }
    return NULL;
}
static int pick_monster(int depth) {
    int cand[NMD], n = 0;
    for (int i = 0; i < NMD - 1; i++)
        if (depth >= MD[i].d0 && depth <= MD[i].d1) {
            cand[n++] = i;
            if (depth >= MD[i].d0 + 1 && depth <= MD[i].d1 - 1) cand[n++] = i; /* mid-range favoured */
        }
    return n ? cand[rnd(n)] : 0;
}
static void spawn_group(int type, int x, int y, bool awake) {
    spawn_mon(type, x, y, awake);
    if (MD[type].flags & F_PACK) {
        int extra = rr(1, 3);
        for (int k = 0; k < 12 && extra > 0; k++) {
            int nx = x + rr(-2, 2), ny = y + rr(-2, 2);
            if (inmap(nx, ny) && tile[ny][nx] == T_FLOOR && !mon_at(nx, ny) && !(nx == G.px && ny == G.py)) {
                spawn_mon(type, nx, ny, awake);
                extra--;
            }
        }
    }
}

static void gen_level(int depth) {
    memset(mons, 0, sizeof(mons));
    memset(fit, 0, sizeof(fit));
    memset(seen, 0, sizeof(seen));
    memset(vis, 0, sizeof(vis));
    memset(fxs, 0, sizeof(fxs));
    bool want_cave = depth != NDEPTH && (depth == 3 || depth == 6 || depth == 8 || (depth > 1 && rnd(100) < 15));
    for (int attempt = 0;; attempt++) {
        if (want_cave && attempt < 20 && gen_cave()) break;
        if (!want_cave || attempt >= 20) {
            gen_rooms(depth);
            if (nrooms >= 5) break;
        }
    }
    int sx, sy;
    if (cave_level) {
        do { sx = rr(1, MW - 2); sy = rr(1, MH - 2); } while (tile[sy][sx] != T_FLOOR);
    } else { sx = rooms[0].x + rooms[0].w / 2; sy = rooms[0].y + rooms[0].h / 2; }
    G.px = G.sx = sx; G.py = G.sy = sy;
    bfs_from(sx, sy);
    /* stairs: far from the start (not on the last floor, where the sanctum is) */
    int bx = sx, by = sy, bd = 0;
    int bossroom = -1;
    if (depth == NDEPTH) {
        for (int i = 1; i < nrooms; i++) {
            int d = sd[rooms[i].y + rooms[i].h / 2][rooms[i].x + rooms[i].w / 2];
            if (d > bd) { bd = d; bossroom = i; }
        }
        Room *r = &rooms[bossroom];
        bx = r->x + r->w / 2; by = r->y + r->h / 2;
    } else {
        int md = 0;
        for (int y = 0; y < MH; y++)
            for (int x = 0; x < MW; x++) if (tile[y][x] == T_FLOOR && sd[y][x] > md) md = sd[y][x];
        for (int t = 0; t < 2000; t++) {
            int x = rr(1, MW - 2), y = rr(1, MH - 2);
            if (tile[y][x] == T_FLOOR && sd[y][x] >= md * 8 / 10) { bx = x; by = y; break; }
        }
        if (bx == sx && by == sy) {
            for (int y = 0; y < MH; y++)
                for (int x = 0; x < MW; x++) if (tile[y][x] == T_FLOOR && sd[y][x] == md) { bx = x; by = y; }
        }
        tile[by][bx] = T_STAIRS;
    }
    if (depth == NDEPTH) {
        Item art = {IK_ARTEFACT, 0, 0, 1, 0};
        add_floor(art, bx, by);
        Mon *k = spawn_mon(MD_KING, bx + 1, by, false);
        if (k) { k->asleep = 1; }
        for (int i = 0; i < 4; i++) {
            int x = rooms[bossroom].x + rnd(rooms[bossroom].w), y = rooms[bossroom].y + rnd(rooms[bossroom].h);
            if (!mon_at(x, y) && (x != bx || y != by)) spawn_mon(pick_monster(depth), x, y, false);
        }
    }
    /* monsters */
    int nm = 8 + depth * 2 / 3 + rnd(4);
    for (int i = 0; i < nm; i++) {
        int x, y;
        if (!random_spot(9, &x, &y, false)) continue;
        spawn_group(pick_monster(depth), x, y, false);
    }
    /* items */
    int ni = 5 + rnd(4);
    for (int i = 0; i < ni; i++) {
        int x, y;
        if (random_spot(3, &x, &y, true)) add_floor(make_item(depth), x, y);
    }
    int ng = 4 + rnd(3);
    for (int i = 0; i < ng; i++) {
        int x, y;
        if (random_spot(1, &x, &y, true)) add_floor((Item){IK_GOLD, 0, 0, (short)(rr(6, 16) + depth * rr(5, 9)), 0}, x, y);
    }
    {   /* a guaranteed ration every other floor */
        int x, y;
        if (random_spot(3, &x, &y, true)) add_floor((Item){IK_FOOD, (unsigned char)(rnd(2) ? 0 : 2), 0, 1, 0}, x, y);
        if (rnd(100) < 35 && random_spot(3, &x, &y, true)) add_floor((Item){IK_FOOD, (unsigned char)rnd(3), 0, 1, 0}, x, y);
        if (depth <= 4 && random_spot(3, &x, &y, true)) add_floor((Item){IK_POTION, P_HEAL, 0, 1, 0}, x, y);
    }
    G.nextspawn = 220 + rnd(200);
}

/* ================================================================== items & inventory */
static void item_name(const Item *it, char *b, size_t n) {
    char q[12] = "";
    if (it->qty > 1) snprintf(q, sizeof(q), "%d ", it->qty);
    bool pl = it->qty > 1;
    switch (it->kind) {
    case IK_GOLD: snprintf(b, n, "%d GOLD", it->qty); break;
    case IK_POTION:
        if (G.pot_known[it->sub]) snprintf(b, n, "%s%s OF %s", q, pl ? "POTIONS" : "POTION", POT_NAME[it->sub]);
        else snprintf(b, n, "%s%s %s", q, POT_LOOK[G.pot_look[it->sub]], pl ? "POTIONS" : "POTION");
        break;
    case IK_SCROLL:
        if (G.scr_known[it->sub]) snprintf(b, n, "%s%s OF %s", q, pl ? "SCROLLS" : "SCROLL", SCR_NAME[it->sub]);
        else snprintf(b, n, "%s%s '%s'", q, pl ? "SCROLLS" : "SCROLL", G.scr_title[it->sub]);
        break;
    case IK_WAND:
        if (G.wand_known[it->sub]) snprintf(b, n, "WAND OF %s (%d)", WAND_NAME[it->sub], it->chg);
        else snprintf(b, n, "%s WAND", WAND_LOOK[G.wand_look[it->sub]]);
        break;
    case IK_WEAPON: snprintf(b, n, "%+d %s", it->ench, WEAP[it->sub].name); break;
    case IK_ARMOR: snprintf(b, n, "%+d %s", it->ench, ARM[it->sub].name); break;
    case IK_FOOD: snprintf(b, n, "%s%s", q, FOOD[it->sub].name); break;
    case IK_KNIFE: snprintf(b, n, "%s%s", q, pl ? "THROWING KNIVES" : "THROWING KNIFE"); break;
    default: snprintf(b, n, "THE HOLLOW STAR"); break;
    }
}
static int item_glyph(const Item *it, int *col) {
    switch (it->kind) {
    case IK_GOLD: *col = C_YELLOW; return G_GOLD;
    case IK_POTION: *col = POT_COL[G.pot_look[it->sub]]; return G_POTION;
    case IK_SCROLL: *col = C_WHITE; return G_SCROLL;
    case IK_WAND: *col = WAND_COL[G.wand_look[it->sub]]; return G_WAND;
    case IK_WEAPON: *col = C_LCYAN; return G_WEAPON;
    case IK_ARMOR: *col = C_LBLUE; return G_ARMOR;
    case IK_FOOD: *col = C_BROWN; return G_FOOD;
    case IK_KNIFE: *col = C_LGRAY; return G_KNIFE;
    default: *col = C_LMAGENTA; return G_ARTEFACT;
    }
}
static bool stackable(const Item *it) { return it->kind == IK_POTION || it->kind == IK_SCROLL || it->kind == IK_FOOD || it->kind == IK_KNIFE; }
static int inv_add(Item it) {
    if (stackable(&it))
        for (int i = 0; i < INVMAX; i++)
            if (G.inv[i].qty > 0 && G.inv[i].kind == it.kind && G.inv[i].sub == it.sub) { G.inv[i].qty = (short)(G.inv[i].qty + it.qty); return i; }
    for (int i = 0; i < INVMAX; i++)
        if (G.inv[i].qty <= 0) { G.inv[i] = it; return i; }
    return -1;
}
static void inv_take(int slot) {
    if (--G.inv[slot].qty <= 0) {
        G.inv[slot].qty = 0;
        if (G.eqw == slot) G.eqw = -1;
        if (G.eqa == slot) G.eqa = -1;
    }
}
static int player_ac(void) { return G.eqa >= 0 ? ARM[G.inv[G.eqa].sub].ac + G.inv[G.eqa].ench : 0; }
static int xp_need(int lvl) { return 14 * lvl * (lvl + 1); }
static int calc_score(void) { return G.gold + G.depth * 100 + G.kills * 10 + (G.won ? 2000 : 0); }

static void shuffle_ints(int *a, int n) {
    for (int i = n - 1; i > 0; i--) { int j = rnd(i + 1), t = a[i]; a[i] = a[j]; a[j] = t; }
}
static void new_game(Uint32 seed) {
    rt_seed = seed;
    int st = G.state, tp = G.title_page;
    memset(&G, 0, sizeof(G));
    G.state = st; G.title_page = tp;
    memset(logt, 0, sizeof(logt));
    logn = 0;
    int p[NPOT];
    for (int i = 0; i < NPOT; i++) p[i] = i;
    shuffle_ints(p, NPOT);
    for (int i = 0; i < NPOT; i++) G.pot_look[i] = p[i];
    int w[5] = {0, 1, 2, 3, 4};
    shuffle_ints(w, 5);
    for (int i = 0; i < NWAND; i++) G.wand_look[i] = w[i];
    for (int i = 0; i < NSCR; i++) {
        for (;;) {
            char t[16] = "";
            for (int wd = 0; wd < 2; wd++) {
                strcat(t, SYL[rnd(16)]);
                if (rnd(2)) strcat(t, SYL[rnd(16)]);
                if (wd == 0) strcat(t, " ");
            }
            bool dup = false;
            for (int j = 0; j < i; j++) if (!strcmp(G.scr_title[j], t)) dup = true;
            if (!dup && strlen(t) < 15) { strcpy(G.scr_title[i], t); break; }
        }
    }
    G.maxhp = G.hp = 32; G.lvl = 1; G.depth = 1; G.hunger = 1800;
    G.eqw = G.eqa = -1;
    G.inv[0] = (Item){IK_WEAPON, 1, 0, 1, 0};
    G.inv[1] = (Item){IK_ARMOR, 1, 0, 1, 0};
    G.inv[2] = (Item){IK_KNIFE, 0, 0, 4, 0};
    G.inv[3] = (Item){IK_FOOD, 0, 0, 1, 0};
    G.eqw = 0; G.eqa = 1;
    G.rank = -1;
    gen_level(1);
    update_fov();
    compute_dmap();
    MSG("You descend into the Deep. Find the Hollow Star.");
}

/* ================================================================== effects */
static void fx_add(int x, int y, int ch, int col, int ttl, int delay) {
    for (int i = 0; i < 48; i++)
        if (fxs[i].ttl <= 0 && fxs[i].delay <= 0) { fxs[i] = (Fx){x, y, ch, col, ttl, delay}; return; }
}

static void die(const char *by) {
    if (G.dead) return;
    G.dead = true;
    snprintf(G.killer, sizeof(G.killer), "%s", by);
}
static void hurt_player(int dmg, const char *by) {
    if (dmg < 1) dmg = 1;
    G.hp -= dmg;
    G.last_dmg_flash = 6;
    sfx_hurt();
    if (G.hp <= 0) {
        if (G.god) { G.hp = G.maxhp; return; }
        G.hp = 0;
        die(by);
    }
}
static void gain_xp(int n) {
    G.xp += n;
    while (G.xp >= xp_need(G.lvl)) {
        G.lvl++;
        int up = 4 + rnd(3);
        G.maxhp += up;
        G.hp = imin(G.maxhp, G.hp + up + 3);
        MSGC(C_YELLOW, "Welcome to level %d!", G.lvl);
        sfx_levelup();
    }
}
static void kill_mon(Mon *m) {
    const MonDef *d = &MD[m->type];
    m->alive = false;
    G.kills++;
    gain_xp(d->xp);
    if (d->flags & F_BOSS) { MSGC(C_YELLOW, "The %s crumbles to dust!", d->name); }
    else if (rnd(100) < 14) add_floor(make_item(G.depth), m->x, m->y);
    sfx_kill();
}
static void hit_mon(Mon *m, int dmg) {
    m->hp -= dmg;
    m->asleep = 0;
    m->aware = 1;
    fx_add(m->x, m->y, MD[m->type].ch, C_WHITE, 3, 0);
}

static void player_attack(Mon *m) {
    const MonDef *d = &MD[m->type];
    int mn = 1, mx = 3, ench = 0;
    if (G.eqw >= 0) { mn = WEAP[G.inv[G.eqw].sub].mn; mx = WEAP[G.inv[G.eqw].sub].mx; ench = G.inv[G.eqw].ench; }
    int chance = 88 - d->def * 4 + ench * 2 + G.lvl;
    if (chance < 40) chance = 40;
    if (chance > 97) chance = 97;
    bool sneak = m->asleep || m->stun > 0;
    if (sneak) chance = 100;
    if (rnd(100) < chance) {
        int dmg = rr(mn, mx) + ench + G.might + (G.lvl - 1) / 3;
        bool crit = rnd(100) < 8;
        if (crit || sneak) dmg *= 2;
        if (dmg < 1) dmg = 1;
        hit_mon(m, dmg);
        if (m->hp <= 0) { MSGC(C_LGREEN, "You slay the %s!", d->name); kill_mon(m); }
        else MSG("You %s the %s.", crit ? "smite" : sneak ? "ambush" : "hit", d->name);
        sfx_hit();
    } else {
        m->asleep = 0; m->aware = 1;
        MSGC(C_LGRAY, "You miss the %s.", d->name);
        sfx_miss();
    }
}
static void mon_attack(Mon *m) {
    const MonDef *d = &MD[m->type];
    int chance = 88 - player_ac() * 4 - G.lvl / 3;
    if (chance < 25) chance = 25;
    if (rnd(100) < chance) {
        int dmg = rr(d->dmin, d->dmax) + G.depth / 3;
        MSGC(C_LRED, "The %s %s you.", d->name, d->verb);
        if (d->flags & F_POISON && !(d->flags & F_RANGED) && rnd(100) < 50) { G.poison += 4; MSGC(C_LGREEN, "You are poisoned!"); }
        if (d->flags & F_DRAIN && rnd(100) < 35 && G.maxhp > 12) { G.maxhp--; G.hp = imin(G.hp, G.maxhp); MSGC(C_LCYAN, "Your life force wanes."); }
        hurt_player(dmg, d->name);
    } else {
        MSGC(C_LGRAY, "The %s misses you.", d->name);
        sfx_miss();
    }
}
static void mon_ranged(Mon *m) {
    const MonDef *d = &MD[m->type];
    int chance = 80 - player_ac() * 3;
    if (chance < 30) chance = 30;
    int xs[16], ys[16];
    int n = line_path(m->x, m->y, G.px, G.py, xs, ys, 16);
    for (int i = 0; i < n; i++) fx_add(xs[i], ys[i], G_BOLT, d->col, 2, i);
    if (rnd(100) < chance) {
        int dmg = (d->flags & F_BOSS) ? rr(5, 9) : rr(d->dmin, d->dmax) + G.depth / 4;
        MSGC(C_LRED, "The %s %s you!", d->name, d->rverb);
        if (d->flags & F_POISON && rnd(100) < 60) { G.poison += 4; MSGC(C_LGREEN, "You are poisoned!"); }
        hurt_player(dmg, d->name);
    } else MSGC(C_LGRAY, "The %s's shot misses.", d->name);
}

/* ================================================================== monster AI */
static const int DXS[8] = {1, -1, 0, 0, 1, 1, -1, -1}, DYS[8] = {0, 0, 1, -1, 1, -1, 1, -1};
static bool mon_try(Mon *m, int dx, int dy) {
    if (!can_step(m->x, m->y, dx, dy)) return false;
    int nx = m->x + dx, ny = m->y + dy;
    if (mon_at(nx, ny) || (nx == G.px && ny == G.py)) return false;
    if (tile[ny][nx] == T_DOOR) tile[ny][nx] = T_ODOOR;
    m->x = nx; m->y = ny;
    return true;
}
static bool mon_toward(Mon *m) {
    int cur = dmap[m->y][m->x];
    if (cur == 255) return false;
    int bestd = cur, cand[8], nc = 0;
    for (int k = 0; k < 8; k++) {
        int nx = m->x + DXS[k], ny = m->y + DYS[k];
        if (!inmap(nx, ny) || !can_step(m->x, m->y, DXS[k], DYS[k]) || mon_at(nx, ny) || (nx == G.px && ny == G.py)) continue;
        int d = dmap[ny][nx];
        if (d < bestd) { bestd = d; nc = 0; }
        if (d == bestd && d < cur) cand[nc++] = k;
    }
    if (!nc) return false;
    int k = cand[rnd(nc)];
    return mon_try(m, DXS[k], DYS[k]);
}
static bool mon_away(Mon *m) {
    int cur = cheb(m->x, m->y, G.px, G.py), cand[8], nc = 0, bestd = cur;
    for (int k = 0; k < 8; k++) {
        int nx = m->x + DXS[k], ny = m->y + DYS[k];
        if (!inmap(nx, ny) || !can_step(m->x, m->y, DXS[k], DYS[k]) || mon_at(nx, ny) || (nx == G.px && ny == G.py)) continue;
        int d = cheb(nx, ny, G.px, G.py);
        if (d > bestd) { bestd = d; nc = 0; }
        if (d == bestd && d > cur) cand[nc++] = k;
    }
    if (!nc) return false;
    int k = cand[rnd(nc)];
    return mon_try(m, DXS[k], DYS[k]);
}
static void mon_wander(Mon *m) {
    int k = rnd(8);
    mon_try(m, DXS[k], DYS[k]);
}
static int alive_count(void) {
    int n = 0;
    for (int i = 0; i < MAXMON; i++) n += mons[i].alive;
    return n;
}
static void mon_act(Mon *m) {
    const MonDef *d = &MD[m->type];
    int dist = cheb(m->x, m->y, G.px, G.py);
    bool sees = vis[m->y][m->x];
    if (m->asleep) {
        int chance = sees ? 14 : (dmap[m->y][m->x] <= 7 ? 5 : 0);
        if (d->flags & F_BOSS) chance = sees ? 60 : 0;
        if (rnd(100) < chance) { m->asleep = 0; m->aware = 1; }
        return;
    }
    if (m->stun > 0) { m->stun--; return; }
    if ((d->flags & F_REGEN) && m->hp < d->hp && G.turn % 5 == 0) m->hp++;
    if (sees) m->aware = 1;
    if (m->fear > 0) { m->fear--; if (!mon_away(m) && dist == 1) mon_attack(m); return; }
    if (dist == 1 && can_step(m->x, m->y, G.px - m->x, G.py - m->y)) { mon_attack(m); return; }
    if (d->flags & F_STILL) return;
    if (d->flags & F_BOSS) {
        if (m->cd > 0) m->cd--;
        else if (m->aware && sees && alive_count() < 12) {
            m->cd = 9;
            int made = 0;
            for (int k = 0; k < 8 && made < 2; k++) {
                int nx = m->x + DXS[k], ny = m->y + DYS[k];
                if (passable(nx, ny) && !mon_at(nx, ny) && !(nx == G.px && ny == G.py) && !is_door(nx, ny)) {
                    Mon *s = spawn_mon(pick_monster(8), nx, ny, true);
                    if (s) made++;
                }
            }
            if (made) { MSGC(C_LMAGENTA, "The Hollow King calls his guard!"); return; }
        }
    }
    if ((d->flags & F_RANGED) && sees && dist <= d->range && dist > 1 && rnd(100) < ((d->flags & F_BOSS) ? 50 : 40)) {
        mon_ranged(m);
        return;
    }
    if ((d->flags & F_KEEP) && sees && dist <= 2 && mon_away(m)) return;
    if ((d->flags & F_ERRATIC) && rnd(100) < 45) { mon_wander(m); return; }
    if (m->aware && dmap[m->y][m->x] != 255) { if (!mon_toward(m)) mon_wander(m); }
    else if (rnd(100) < 35) mon_wander(m);
}

/* ================================================================== turn processing */
static bool monster_in_view(void) {
    for (int i = 0; i < MAXMON; i++)
        if (mons[i].alive && vis[mons[i].y][mons[i].x]) return true;
    return false;
}
static void end_turn(int ticks) {
    update_fov();
    compute_dmap();
    for (int i = 0; i < MAXMON && !G.dead; i++) {
        Mon *m = &mons[i];
        if (!m->alive) continue;
        int sp = MD[m->type].speed;
        if (m->slow > 0) { sp /= 2; m->slow--; }
        m->energy += sp * ticks / 100;
        int guard = 0;
        while (m->energy >= 100 && m->alive && !G.dead && guard++ < 4) {
            m->energy -= 100;
            mon_act(m);
        }
    }
    G.turn++;
    if (G.dead) return;
    if (G.poison > 0) {
        G.poison--;
        hurt_player(1, "poison");
        if (G.poison == 0) MSG("The poison wears off.");
    }
    if (G.haste > 0 && --G.haste == 0) MSG("You slow down.");
    if (G.sense > 0) G.sense--;
    if (!(G.haste > 0 && (G.turn & 1))) {
        int old = G.hunger;
        if (G.hunger > 0) G.hunger--;
        if (old > 300 && G.hunger <= 300) MSGC(C_YELLOW, "You are hungry.");
        if (old > 100 && G.hunger <= 100) MSGC(C_LRED, "You are weak with hunger!");
        if (G.hunger == 0 && G.turn % 3 == 0) { MSGC(C_LRED, "You are starving!"); hurt_player(1, "starvation"); }
    }
    int interval = imax(4, 12 - G.lvl / 2);
    if (G.hp < G.maxhp && G.poison == 0 && G.hunger > 0 && G.turn % interval == 0) G.hp = imin(G.maxhp, G.hp + 1 + G.lvl / 8);
    if (--G.nextspawn <= 0) {
        G.nextspawn = 250 + rnd(200);
        int x, y;
        if (alive_count() < 20 && random_spot(10, &x, &y, false) && !vis[y][x]) spawn_group(pick_monster(G.depth), x, y, true);
    }
}

/* ================================================================== player actions */
static void check_here(void) {
    int i = fitem_at(G.px, G.py);
    if (i < 0) return;
    char nm[48];
    item_name(&fit[i].it, nm, sizeof(nm));
    int extra = 0;
    for (int j = 0; j < MAXITEM; j++) if (fit[j].on && j != i && fit[j].x == G.px && fit[j].y == G.py) extra++;
    MSG("Here: %s%s. (G)", nm, extra ? " and more" : "");
}
static void win_game(void) {
    G.won = true;
    G.state = S_WIN;
    G.st = 0;
    tune_win();
}
static bool act_wait(void) {
    if (G.dead || G.won) return false;
    end_turn(G.haste ? 50 : 100);
    return true;
}
static bool act_move(int dx, int dy) {
    if (G.dead || G.won) return false;
    int nx = G.px + dx, ny = G.py + dy;
    if (!inmap(nx, ny) || !can_step(G.px, G.py, dx, dy)) return false;
    Mon *m = mon_at(nx, ny);
    if (m) { player_attack(m); end_turn(G.haste ? 50 : 100); return true; }
    if (tile[ny][nx] == T_DOOR) { tile[ny][nx] = T_ODOOR; sfx_door(); }
    G.px = nx; G.py = ny;
    bool any = false;
    for (int i = 0; i < MAXITEM; i++) {
        if (!fit[i].on || fit[i].x != nx || fit[i].y != ny) continue;
        if (fit[i].it.kind == IK_GOLD) {
            G.gold += fit[i].it.qty;
            MSGC(C_YELLOW, "You pick up %d gold.", fit[i].it.qty);
            fit[i].on = false;
            sfx_gold();
        } else if (fit[i].it.kind == IK_ARTEFACT) {
            fit[i].on = false;
            MSGC(C_LMAGENTA, "You take the Hollow Star!");
            win_game();
            return true;
        } else any = true;
    }
    if (any) check_here();
    end_turn(G.haste ? 50 : 100);
    return true;
}
static bool act_pickup(void) {
    if (G.dead || G.won) return false;
    bool got = false, full = false;
    for (int i = 0; i < MAXITEM; i++) {
        if (!fit[i].on || fit[i].x != G.px || fit[i].y != G.py) continue;
        Item it = fit[i].it;
        if (it.kind == IK_ARTEFACT) { fit[i].on = false; win_game(); return true; }
        int slot = inv_add(it);
        if (slot < 0) { full = true; continue; }
        char nm[48];
        item_name(&G.inv[slot], nm, sizeof(nm));
        MSG("%c) %s.", 'a' + slot, nm);
        fit[i].on = false;
        got = true;
    }
    if (full) MSG("Your pack is full.");
    if (!got) { if (!full) MSG("Nothing here to pick up."); return false; }
    sfx_pickup();
    end_turn(G.haste ? 50 : 100);
    return true;
}
static bool act_descend(void) {
    if (G.dead || G.won) return false;
    if (tile[G.py][G.px] != T_STAIRS) { MSG("There are no stairs here."); return false; }
    G.depth++;
    gen_level(G.depth);
    update_fov();
    compute_dmap();
    sfx_stairs();
    if (G.depth == NDEPTH) MSGC(C_LMAGENTA, "Depth %d. A cold light burns ahead...", G.depth);
    else MSG("You descend to depth %d.", G.depth);
    return true;
}
static void reveal_map(void) {
    for (int y = 0; y < MH; y++)
        for (int x = 0; x < MW; x++) {
            if (tile[y][x] == T_WALL) continue;
            for (int j = -1; j <= 1; j++)
                for (int i = -1; i <= 1; i++) if (inmap(x + i, y + j)) seen[y + j][x + i] = 1;
        }
}
static void learn_pot(int s) { G.pot_known[s] = true; }
static void quaff(int sub) {
    sfx_quaff();
    bool was = G.pot_known[sub];
    learn_pot(sub);
    switch (sub) {
    case P_HEAL: G.hp = imin(G.maxhp, G.hp + 18 + G.lvl * 2); G.poison = 0; MSGC(C_LGREEN, "Warmth floods your wounds."); break;
    case P_VIGOR: G.maxhp += 6; G.hp = G.maxhp; G.poison = 0; MSGC(C_LGREEN, "You feel vigorous! Max HP up."); break;
    case P_VENOM: G.poison += 10; MSGC(C_LGREEN, "Bitter venom burns in your veins!"); break;
    case P_HASTE: G.haste += 25; MSGC(C_LCYAN, "The world slows around you."); break;
    case P_MIGHT: G.might++; MSGC(C_LCYAN, "Your arm surges with might."); break;
    case P_INSIGHT: G.sense = 60; MSGC(C_LCYAN, "Your mind's eye opens."); break;
    case P_TORPOR: G.sleep = 6; MSGC(C_LRED, "A heavy torpor takes you..."); break;
    case P_BLAZE:
        MSGC(C_LRED, "Flames burst around you!");
        hurt_player(rr(3, 6), "a flask of fire");
        for (int i = 0; i < MAXMON; i++) {
            Mon *m = &mons[i];
            if (m->alive && cheb(m->x, m->y, G.px, G.py) <= 2) {
                hit_mon(m, rr(6, 12));
                if (m->hp <= 0) { MSG("The %s burns to ash!", MD[m->type].name); kill_mon(m); }
            }
        }
        break;
    }
    (void)was;
}
static void read_scroll(int sub) {
    sfx_read();
    G.scr_known[sub] = true;
    switch (sub) {
    case S_TELE: {
        int x, y;
        if (random_spot(0, &x, &y, false)) { G.px = x; G.py = y; }
        MSGC(C_LCYAN, "The world lurches. You are elsewhere.");
        break;
    }
    case S_MAP: reveal_map(); MSGC(C_LCYAN, "The dungeon's bones are laid bare."); break;
    case S_ENCW:
        if (G.eqw >= 0) { G.inv[G.eqw].ench++; MSGC(C_LGREEN, "Your weapon gleams keener."); } else MSG("Your hands tingle.");
        break;
    case S_ENCA:
        if (G.eqa >= 0) { G.inv[G.eqa].ench++; MSGC(C_LGREEN, "Your armour hardens."); } else MSG("Your skin prickles.");
        break;
    case S_SMITE:
        MSGC(C_YELLOW, "Lightning lashes your foes!");
        for (int i = 0; i < MAXMON; i++) {
            Mon *m = &mons[i];
            if (m->alive && vis[m->y][m->x]) {
                hit_mon(m, rr(8, 14) + G.depth);
                if (m->hp <= 0) { MSG("The %s is blasted apart!", MD[m->type].name); kill_mon(m); }
            }
        }
        break;
    case S_DREAD:
        MSGC(C_LCYAN, "A wave of dread rolls outward.");
        for (int i = 0; i < MAXMON; i++) if (mons[i].alive && vis[mons[i].y][mons[i].x] && !(MD[mons[i].type].flags & F_BOSS)) mons[i].fear = 14;
        break;
    case S_SUMMON: {
        MSGC(C_LRED, "Shapes crawl out of the dark!");
        int made = 0;
        for (int k = 0; k < 8 && made < 3; k++) {
            int nx = G.px + DXS[k], ny = G.py + DYS[k];
            if (passable(nx, ny) && !mon_at(nx, ny) && !is_door(nx, ny)) { if (spawn_mon(pick_monster(G.depth), nx, ny, true)) made++; }
        }
        break;
    }
    case S_LORE: {
        int c[NPOT + NSCR + NWAND], n = 0;
        for (int i = 0; i < INVMAX; i++) {
            if (G.inv[i].qty <= 0) continue;
            if (G.inv[i].kind == IK_POTION && !G.pot_known[G.inv[i].sub]) c[n++] = G.inv[i].sub;
            if (G.inv[i].kind == IK_SCROLL && !G.scr_known[G.inv[i].sub]) c[n++] = 100 + G.inv[i].sub;
            if (G.inv[i].kind == IK_WAND && !G.wand_known[G.inv[i].sub]) c[n++] = 200 + G.inv[i].sub;
        }
        if (!n) { MSG("You learn nothing new."); break; }
        int v = c[rnd(n)];
        if (v < 100) G.pot_known[v] = true; else if (v < 200) G.scr_known[v - 100] = true; else G.wand_known[v - 200] = true;
        MSGC(C_LCYAN, "Knowledge floods in.");
        break;
    }
    }
}
static bool act_use(int slot) {
    if (G.dead || G.won || slot < 0 || slot >= INVMAX || G.inv[slot].qty <= 0) return false;
    Item *it = &G.inv[slot];
    char nm[48];
    switch (it->kind) {
    case IK_POTION: { int s = it->sub; inv_take(slot); quaff(s); break; }
    case IK_SCROLL: { int s = it->sub; inv_take(slot); read_scroll(s); break; }
    case IK_FOOD:
        if (G.hunger > HMAX - 400) { MSG("You are too full to eat."); return false; }
        G.hunger = imin(HMAX, G.hunger + FOOD[it->sub].nut);
        MSG("You eat the %s.", FOOD[it->sub].name);
        sfx_eat();
        inv_take(slot);
        break;
    case IK_WEAPON:
        if (G.eqw == slot) { MSG("You already wield that."); return false; }
        G.eqw = slot;
        item_name(it, nm, sizeof(nm));
        MSG("You wield the %s.", nm);
        sfx_select();
        break;
    case IK_ARMOR:
        if (G.eqa == slot) { MSG("You already wear that."); return false; }
        G.eqa = slot;
        item_name(it, nm, sizeof(nm));
        MSG("You don the %s.", nm);
        sfx_select();
        break;
    default: return false;
    }
    if (!G.dead) end_turn(G.haste ? 50 : 100);
    while (G.sleep > 0 && !G.dead) { G.sleep--; end_turn(100); }
    return true;
}

/* throw a knife or zap a wand along the line to (tx,ty) */
static bool act_throw(int slot, int tx, int ty) {
    if (G.dead || G.won || slot < 0 || slot >= INVMAX || G.inv[slot].qty <= 0) return false;
    Item *it = &G.inv[slot];
    if (it->kind != IK_KNIFE && it->kind != IK_WAND) return false;
    if (tx == G.px && ty == G.py) return false;
    int dx = tx - G.px, dy = ty - G.py, mx = imax(iabs(dx), iabs(dy));
    int range = it->kind == IK_WAND ? 10 : 8;
    int fx = G.px + dx * range / mx, fy = G.py + dy * range / mx;
    int xs[16], ys[16];
    int n = line_path(G.px, G.py, fx, fy, xs, ys, range);
    int stopx = G.px, stopy = G.py;
    Mon *hit = NULL;
    for (int i = 0; i < n; i++) {
        if (!passable(xs[i], ys[i]) || tile[ys[i]][xs[i]] == T_DOOR) break;
        stopx = xs[i]; stopy = ys[i];
        Mon *m = mon_at(xs[i], ys[i]);
        if (m) { hit = m; break; }
    }
    int sub = it->sub;
    bool wand = it->kind == IK_WAND;
    for (int i = 0; i < n; i++) {
        if (xs[i] == stopx && ys[i] == stopy) { fx_add(xs[i], ys[i], wand ? G_BOLT : G_KNIFE, wand ? C_LCYAN : C_LGRAY, 3, i); break; }
        fx_add(xs[i], ys[i], wand ? G_BOLT : G_KNIFE, wand ? C_YELLOW : C_LGRAY, 2, i);
    }
    if (wand) {
        sfx_zap();
        it->chg--;
        if (hit) {
            const MonDef *d = &MD[hit->type];
            G.wand_known[sub] = true;
            switch (sub) {
            case W_SPARK:
                hit_mon(hit, rr(7, 14) + G.depth / 2);
                MSGC(C_YELLOW, "Sparks lash the %s!", d->name);
                break;
            case W_FROST:
                hit_mon(hit, rr(2, 5));
                hit->stun = 6; hit->slow = 12;
                MSGC(C_LCYAN, "Frost seizes the %s!", d->name);
                break;
            default:
                if (!(d->flags & F_BOSS)) hit->fear = 16;
                hit_mon(hit, 0);
                MSGC(C_LCYAN, "The %s recoils in terror!", d->name);
                break;
            }
            if (hit->hp <= 0) { MSGC(C_LGREEN, "The %s is slain!", d->name); kill_mon(hit); }
        } else MSG("The bolt fizzles into the dark.");
        if (it->chg <= 0) { MSG("The wand crumbles to dust."); it->qty = 0; if (G.eqw == slot) G.eqw = -1; }
    } else {
        sfx_throw();
        bool drop = true;
        if (hit) {
            const MonDef *d = &MD[hit->type];
            int chance = 82 - d->def * 4 + G.lvl;
            if (hit->asleep) chance = 100;
            if (rnd(100) < chance) {
                hit_mon(hit, rr(2, 5) + G.might / 2);
                if (hit->hp <= 0) { MSGC(C_LGREEN, "Your knife slays the %s!", d->name); kill_mon(hit); }
                else MSG("Your knife strikes the %s.", d->name);
                if (rnd(100) < 40) drop = false;
            } else { MSGC(C_LGRAY, "Your knife misses the %s.", d->name); hit->aware = 1; hit->asleep = 0; }
        }
        if (drop) add_floor((Item){IK_KNIFE, 0, 0, 1, 0}, stopx, stopy);
        inv_take(slot);
    }
    if (!G.dead) end_turn(G.haste ? 50 : 100);
    return true;
}
static bool act_rest(void) {
    if (G.dead || G.won) return false;
    if (monster_in_view()) { MSG("Not with enemies near."); return false; }
    if (G.hp >= G.maxhp) { return act_wait(); }
    for (int i = 0; i < 40; i++) {
        int hp0 = G.hp;
        act_wait();
        if (G.dead || monster_in_view() || G.hp < hp0 || G.hp >= G.maxhp || G.hunger < 120) break;
    }
    return true;
}

/* ================================================================== drawing */
static const int WALLC[5] = {C_BROWN, C_LGRAY, C_GREEN, C_CYAN, C_LRED};
static const int FLOORC[5] = {C_BROWN, C_LGRAY, C_GREEN, C_CYAN, C_RED};

static void draw_world(void) {
    int camx = G.px - VW / 2, camy = G.py - VH / 2;
    camx = imax(0, imin(MW - VW, camx));
    camy = imax(0, imin(MH - VH, camy));
    int th = (G.depth - 1) / 2;
    if (th > 4) th = 4;
    for (int sy = 0; sy < VH; sy++)
        for (int sx = 0; sx < VW; sx++) {
            int x = camx + sx, y = camy + sy;
            if (!inmap(x, y) || !seen[y][x]) continue;
            bool v = vis[y][x];
            int ch = ' ', fg = C_BLACK;
            switch (tile[y][x]) {
            case T_WALL: ch = G_WALL; fg = v ? WALLC[th] : C_DGRAY; break;
            case T_FLOOR: ch = v ? G_FLOOR : ' '; fg = FLOORC[th]; break;
            case T_DOOR: ch = G_DOOR; fg = v ? C_BROWN : C_DGRAY; break;
            case T_ODOOR: ch = G_ODOOR; fg = v ? C_BROWN : C_DGRAY; break;
            case T_STAIRS: ch = G_STAIRS; fg = v ? C_WHITE : C_LGRAY; break;
            }
            int fi = fitem_at(x, y);
            if (fi >= 0 && tile[y][x] != T_WALL) {
                int c;
                ch = item_glyph(&fit[fi].it, &c);
                fg = v ? c : C_DGRAY;
            }
            put(sx, VY + sy, ch, fg, C_BLACK);
        }
    for (int i = 0; i < MAXMON; i++) {
        Mon *m = &mons[i];
        if (!m->alive) continue;
        int sx = m->x - camx, sy = m->y - camy;
        if (sx < 0 || sy < 0 || sx >= VW || sy >= VH) continue;
        bool v = vis[m->y][m->x];
        if (!v && G.sense <= 0) continue;
        int col = v ? MD[m->type].col : C_MAGENTA;
        if (v && m->asleep) col = C_DGRAY;
        if (v && m->stun > 0) col = C_LCYAN;
        put(sx, VY + sy, MD[m->type].ch, col, C_BLACK);
    }
    int psx = G.px - camx, psy = G.py - camy;
    put(psx, VY + psy, G_PLAYER, G.last_dmg_flash > 0 ? C_LRED : C_WHITE, C_BLACK);
    for (int i = 0; i < 48; i++) {
        Fx *f = &fxs[i];
        if (f->ttl <= 0 || f->delay > 0) continue;
        int sx = f->x - camx, sy = f->y - camy;
        if (sx >= 0 && sy >= 0 && sx < VW && sy < VH) put(sx, VY + sy, f->ch, f->col, C_BLACK);
    }
    if (G.state == S_TARGET) {
        int xs[16], ys[16];
        int n = line_path(G.px, G.py, G.tx, G.ty, xs, ys, 16);
        for (int i = 0; i < n - 1; i++) {
            int sx = xs[i] - camx, sy = ys[i] - camy;
            if (sx >= 0 && sy >= 0 && sx < VW && sy < VH) put(sx, VY + sy, G_BOLT, C_YELLOW, C_BLACK);
        }
        int sx = G.tx - camx, sy = G.ty - camy;
        if (sx >= 0 && sy >= 0 && sx < VW && sy < VH) {
            int ch = sc[VY + sy][sx];
            if (ch == G_BOLT || ch == ' ') ch = G_RETICLE;
            put(sx, VY + sy, ch, sf[VY + sy][sx] == C_BLUE ? C_WHITE : sf[VY + sy][sx], (G.tick / 8) & 1 ? C_BLUE : C_DGRAY);
        }
    }
}

static void draw_status(void) {
    fillr(0, 0, COLS, 2, ' ', C_WHITE, C_BLUE);
    int hpc = G.hp * 100 / G.maxhp > 50 ? C_LGREEN : G.hp * 100 / G.maxhp > 25 ? C_YELLOW : C_LRED;
    puts_(0, 0, "HP", C_LCYAN, C_BLUE);
    putf(3, 0, hpc, C_BLUE, "%d/%d", G.hp, G.maxhp);
    int fill = (G.hp * 10 + G.maxhp - 1) / G.maxhp;
    if (G.hp <= 0) fill = 0;
    for (int i = 0; i < 10; i++) put(12 + i, 0, i < fill ? SH_FULL : SH_LIGHT, i < fill ? hpc : C_DGRAY, C_BLUE);
    puts_(23, 0, "DEPTH", C_LCYAN, C_BLUE);
    putf(29, 0, C_WHITE, C_BLUE, "%d", G.depth);
    put(32, 0, G_GOLD, C_YELLOW, C_BLUE);
    putf(33, 0, C_YELLOW, C_BLUE, "%d", G.gold);
    putf(0, 1, C_WHITE, C_BLUE, "LV%d", G.lvl);
    putf(5, 1, C_LGRAY, C_BLUE, "XP%d/%d", G.xp, xp_need(G.lvl));
    const char *hs = G.hunger > 300 ? "FED" : G.hunger > 100 ? "HUNGRY" : G.hunger > 0 ? "WEAK" : "FAINT";
    putf(17, 1, G.hunger > 300 ? C_LGREEN : G.hunger > 100 ? C_YELLOW : C_LRED, C_BLUE, "%s", hs);
    int x = 24;
    if (G.poison > 0) { puts_(x, 1, "PSN", C_LGREEN, C_BLUE); x += 4; }
    if (G.haste > 0) { puts_(x, 1, "HST", C_LCYAN, C_BLUE); x += 4; }
    if (G.sense > 0) { puts_(x, 1, "SEE", C_LMAGENTA, C_BLUE); x += 4; }
    putf(35, 1, C_WHITE, C_BLUE, "AC%d", player_ac());
}
static void draw_msgs(void) {
    for (int i = 0; i < 3; i++) {
        int idx = logn - 1 - i;
        if (idx < 0) continue;
        int k = idx % LOGN;
        int age = G.turn - logturn[k];
        int col = age <= 0 ? logc[k] : age <= 2 ? C_LGRAY : C_DGRAY;
        puts_(0, 24 - i, logt[k], col, C_BLACK);
    }
}
static void draw_play(void) {
    scr_clear();
    draw_world();
    draw_status();
    draw_msgs();
    if (G.state == S_TARGET) {
        fillr(0, 22, COLS, 1, ' ', C_BLACK, C_BLACK);
        Mon *m = mon_at(G.tx, G.ty);
        if (m && vis[m->y][m->x]) putf(0, 22, C_YELLOW, C_BLACK, "TARGET: %s (%d HP)", MD[m->type].name, m->hp);
        else puts_(0, 22, "TARGET: MOVE CURSOR, ENTER FIRES", C_YELLOW, C_BLACK);
    }
    if (G.quitq) {
        panel(6, 9, 28, 5, C_WHITE, C_BLUE, NULL);
        putsc(10, "ABANDON THIS DELVE?", C_YELLOW, C_BLUE);
        putsc(12, "Y = QUIT TO TITLE   N = STAY", C_WHITE, C_BLUE);
    }
}

static void list_items(const char *title, const char *hint, bool throw_only) {
    panel(0, 0, COLS, SROWS, C_LCYAN, C_BLACK, title);
    int shown = 0;
    for (int i = 0; i < INVMAX; i++) {
        int y = 2 + i;
        Item *it = &G.inv[i];
        if (it->qty <= 0) continue;
        bool ok = !throw_only || it->kind == IK_KNIFE || it->kind == IK_WAND;
        int bg = (G.cur == i) ? C_BLUE : C_BLACK;
        fillr(2, y, 36, 1, ' ', C_WHITE, bg);
        putf(3, y, ok ? C_YELLOW : C_DGRAY, bg, "%c", 'a' + i);
        put(4, y, ')', ok ? C_LGRAY : C_DGRAY, bg);
        int c;
        int g = item_glyph(it, &c);
        put(6, y, g, ok ? c : C_DGRAY, bg);
        char nm[48];
        item_name(it, nm, sizeof(nm));
        puts_(8, y, nm, ok ? C_WHITE : C_DGRAY, bg);
        if (G.eqw == i) puts_(30, y, "IN HAND", C_LGREEN, bg);
        if (G.eqa == i) puts_(33, y, "WORN", C_LGREEN, bg);
        shown++;
    }
    if (!shown) putsc(10, "YOUR PACK IS EMPTY", C_DGRAY, C_BLACK);
    int mn = 1, mx = 3;
    if (G.eqw >= 0) { mn = WEAP[G.inv[G.eqw].sub].mn + G.inv[G.eqw].ench; mx = WEAP[G.inv[G.eqw].sub].mx + G.inv[G.eqw].ench; }
    putf(2, 22, C_LGRAY, C_BLACK, "DMG %d-%d  ARMOUR %d  MIGHT +%d", mn + G.might, mx + G.might, player_ac(), G.might);
    putsc(23, hint, C_YELLOW, C_BLACK);
}

static void draw_title(void) {
    int t = G.tick;
    scr_clear();
    int pg = G.title_page;
    if (pg == 0) {
        for (int y = 0; y < SROWS; y++)
            for (int x = 0; x < COLS; x++) {
                if (y > 2 && y < 23) continue;
                Uint32 h = (Uint32)x * 374761393u + (Uint32)y * 668265263u;
                h = (h ^ (h >> 13)) * 1274126177u;
                int pct = y < 3 ? 100 - y * 28 : 100 - (24 - y) * 40;
                if (y == 23) pct = 55;
                if ((int)((h >> 8) % 100) < pct) put(x, y, G_WALL, (h >> 4) & 1 ? C_BROWN : C_DGRAY, C_BLACK);
            }
        panel(2, 3, 36, 8, C_BROWN, C_BLACK, NULL);
        putsc(9, "A DUNGEON CRAWL IN TEN DEPTHS", C_LCYAN, C_BLACK);
        /* torches */
        int fl = (t / 7) & 1;
        put(3, 15, fl ? G_FLAME1 : G_FLAME2, (t / 5) & 1 ? C_YELLOW : C_LRED, C_BLACK);
        put(36, 15, fl ? G_FLAME2 : G_FLAME1, (t / 6) & 1 ? C_YELLOW : C_LRED, C_BLACK);
        put(3, 16, BX_V, C_BROWN, C_BLACK);
        put(36, 16, BX_V, C_BROWN, C_BLACK);
        for (int x = 0; x < COLS; x++) put(x, 17, SH_MID, C_BROWN, C_BLACK);
        put(37, 16, G_STAIRS, (t / 20) & 1 ? C_WHITE : C_YELLOW, C_BLACK);
        int pos = (t / 5) % 70 - 12;
        put(pos, 16, G_PLAYER, C_WHITE, C_BLACK);
        put(pos - 3, 16, 'r', C_BROWN, C_BLACK);
        put(pos - 4, 16, 'r', C_BROWN, C_BLACK);
        put(pos - 7, 16, 'd', C_CYAN, C_BLACK);
        put(pos - 11, 16, 'D', C_LRED, C_BLACK);
        put(pos + 3, 16, G_GOLD, C_YELLOW, C_BLACK);
        for (int i = 0; i < 4; i++) { /* dripping water */
            int dx = 8 + i * 8 + (i * 5) % 3, ph = (t / 3 + i * 17) % 40;
            if (ph < 14) put(dx, 12 + ph / 3 + (ph > 11), G_BOLT, C_LBLUE, -1);
        }
        if ((t / 30) % 2 == 0) putsc(19, "PRESS ENTER TO DESCEND", C_WHITE, C_BLACK);
        putsc(21, "F1 HELP  H SCORES  M MUTE  ESC QUIT", C_LGRAY, C_BLACK);
    } else if (pg == 1) {
        panel(0, 0, COLS, SROWS, C_LCYAN, C_BLACK, "HOW TO PLAY");
        static const char *L[] = {
            "DESCEND TEN FLOORS OF THE DEEP.",
            "ON THE LAST, THE HOLLOW KING GUARDS",
            "THE HOLLOW STAR. TAKE IT TO WIN.",
            "",
            "BUMP A FOE TO STRIKE IT. SLEEPING",
            "FOES TAKE DOUBLE DAMAGE.",
            "THROW KNIVES AND ZAP WANDS WITH T,",
            "AIM WITH THE ARROWS, ENTER FIRES.",
            "",
            "POTIONS, SCROLLS AND WANDS ARE",
            "UNKNOWN UNTIL USED. NAMES CHANGE",
            "EVERY GAME. SOME ARE POISON!",
            "",
            "EAT BEFORE YOU STARVE. REST WITH R.",
            "GOLD COUNTS. DEATH IS FINAL.",
            "",
            "SCORE = GOLD + 100 PER DEPTH",
            "        + 10 PER KILL + 2000 FOR",
            "        THE HOLLOW STAR",
        };
        for (int i = 0; i < (int)(sizeof(L) / sizeof(L[0])); i++) puts_(3, 2 + i, L[i], i < 3 ? C_YELLOW : C_WHITE, C_BLACK);
        putsc(23, "< >  PAGES    ENTER  START", C_LGRAY, C_BLACK);
    } else if (pg == 2) {
        panel(0, 0, COLS, SROWS, C_LCYAN, C_BLACK, "KEYS");
        static const char *K[][2] = {
            {"ARROWS WASD", "MOVE / ATTACK"}, {"NUMPAD  Y U B N", "DIAGONALS"}, {". OR 5", "WAIT A TURN"},
            {"R", "REST UNTIL HEALED"}, {"G OR ,", "PICK UP"}, {"I", "INVENTORY"},
            {"A-T IN PACK", "USE / EAT / EQUIP"}, {"T", "THROW OR ZAP"}, {"> OR ENTER", "DESCEND STAIRS"},
            {"L", "MESSAGE LOG"}, {"ESC", "QUIT TO TITLE"}, {"M", "MUTE"}, {"F2  F11", "CRT  FULLSCREEN"},
        };
        for (int i = 0; i < 13; i++) { puts_(2, 2 + i, K[i][0], C_YELLOW, C_BLACK); puts_(19, 2 + i, K[i][1], C_WHITE, C_BLACK); }
        puts_(2, 16, "GAMEPAD", C_LCYAN, C_BLACK);
        puts_(2, 17, "STICK/DPAD MOVE   A TAKE/STAIRS/WAIT", C_WHITE, C_BLACK);
        puts_(2, 18, "X OR B THROW   Y PACK   BACK QUIT", C_WHITE, C_BLACK);
        puts_(2, 19, "IN PACK: A USE, B/Y CLOSE", C_WHITE, C_BLACK);
        putsc(23, "< >  PAGES    ENTER  START", C_LGRAY, C_BLACK);
    } else {
        panel(0, 0, COLS, SROWS, C_LCYAN, C_BLACK, "HALL OF DELVERS");
        puts_(4, 3, "RANK NAME   SCORE   DEPTH", C_LCYAN, C_BLACK);
        for (int i = 0; i < RT_NSCORES; i++) {
            int c = i == 0 ? C_YELLOW : i < 3 ? C_WHITE : C_LGRAY;
            putf(5, 5 + i * 2, c, C_BLACK, "%d.", i + 1);
            putf(9, 5 + i * 2, c, C_BLACK, "%s", rt_scores[i].name);
            putf(16, 5 + i * 2, c, C_BLACK, "%6d", rt_scores[i].score);
            putf(27, 5 + i * 2, c, C_BLACK, "%d", rt_scores[i].level);
        }
        putsc(23, "< >  PAGES    ENTER  START", C_LGRAY, C_BLACK);
    }
    scr_flush();
    if (pg == 0) {
        static const int GRAD[7] = {C_WHITE, C_YELLOW, C_YELLOW, C_BROWN, C_LRED, C_RED, C_RED};
        const char *s = "DEEP DELVE";
        int x = (RT_W - rt_text_w(s, 3)) / 2;
        rt_text_ex(s, x + 2, 40, 3, C_BLACK, NULL);
        rt_text_ex(s, x, 38, 3, 0, GRAD);
    }
}

static void draw_over(void) {
    scr_clear();
    for (int x = 0; x < COLS; x++) { put(x, 0, SH_MID, C_RED, C_BLACK); put(x, 24, SH_MID, C_RED, C_BLACK); }
    putsc(1, "YOU HAVE FALLEN", C_LRED, C_BLACK);
    for (int x = 16; x < 24; x++) put(x, 3, HALF_DN, C_LGRAY, C_BLACK);
    fillr(14, 4, 12, 7, ' ', C_BLACK, C_LGRAY);
    fillr(15, 4, 10, 1, ' ', C_BLACK, C_LGRAY);
    put(14, 3, ' ', C_BLACK, C_BLACK);
    putsc(5, "R.I.P.", C_BLACK, C_LGRAY);
    putf(15, 7, C_BLACK, C_LGRAY, "DEPTH %d", G.depth);
    putf(15, 9, C_BLACK, C_LGRAY, "LEVEL %d", G.lvl);
    for (int x = 12; x < 28; x++) put(x, 11, SH_DARK, C_GREEN, C_BLACK);
    char kb[64];
    snprintf(kb, sizeof(kb), "KILLED BY %s ON DEPTH %d", G.killer, G.depth);
    for (char *p = kb; *p; p++) *p = (char)toupper((unsigned char)*p);
    putsc(13, kb, C_WHITE, C_BLACK);
    putf(10, 15, C_YELLOW, C_BLACK, "GOLD     %6d", G.gold);
    putf(10, 16, C_LGRAY, C_BLACK, "KILLS    %6d", G.kills);
    putf(10, 17, C_LGRAY, C_BLACK, "TURNS    %6d", G.turn);
    putf(10, 19, C_LCYAN, C_BLACK, "SCORE    %6d", calc_score());
    if (G.st > 40 && (G.tick / 25) % 2 == 0) putsc(23, "PRESS ENTER", C_WHITE, C_BLACK);
    scr_flush();
}
static void draw_win(void) {
    scr_clear();
    for (int i = 0; i < 40; i++) {
        Uint32 h = (Uint32)(i * 2654435761u) ^ (Uint32)((G.st / 14 + i) * 40503u);
        h = (h ^ (h >> 15)) * 2246822519u;
        int x = (int)((h >> 3) % COLS), y = (int)((h >> 11) % SROWS);
        put(x, y, (h >> 20) & 1 ? '*' : '+', (int[]){C_YELLOW, C_WHITE, C_LRED, C_LCYAN, C_LGREEN, C_LMAGENTA}[(h >> 24) % 6], C_BLACK);
    }
    panel(3, 2, 34, 20, C_YELLOW, C_BLACK, "VICTORY");
    put(19, 4, G_ARTEFACT, (G.tick / 8) & 1 ? C_LMAGENTA : C_WHITE, C_BLACK);
    putsc(6, "THE HOLLOW STAR IS YOURS", C_YELLOW, C_BLACK);
    putsc(8, "THE DEEP GOES QUIET AT LAST.", C_WHITE, C_BLACK);
    putsc(10, "LIGHT RETURNS TO THE STAIRWELL.", C_WHITE, C_BLACK);
    putf(9, 12, C_YELLOW, C_BLACK, "GOLD     %6d", G.gold);
    putf(9, 14, C_LGRAY, C_BLACK, "KILLS    %6d", G.kills);
    putf(9, 15, C_LGRAY, C_BLACK, "LEVEL    %6d", G.lvl);
    putf(9, 16, C_LGRAY, C_BLACK, "TURNS    %6d", G.turn);
    putf(9, 18, C_LCYAN, C_BLACK, "SCORE    %6d", calc_score());
    if (G.st > 60 && (G.tick / 25) % 2 == 0) putsc(20, "PRESS ENTER", C_WHITE, C_BLACK);
    scr_flush();
}
static void draw_entry(void) {
    scr_clear();
    panel(4, 4, 32, 15, C_YELLOW, C_BLACK, "NEW HIGH SCORE");
    putsc(7, "YOUR NAME IS CARVED IN STONE", C_WHITE, C_BLACK);
    putf(13, 9, C_LCYAN, C_BLACK, "SCORE %d", calc_score());
    putf(13, 11, C_LGRAY, C_BLACK, "RANK %d", G.rank + 1);
    for (int i = 0; i < 3; i++) {
        int x = 15 + i * 4;
        bool cur = i == G.name_len;
        char c = i < G.name_len ? G.name[i] : (cur ? (char)('A' + G.letter) : '-');
        put(x, 13, c, cur ? C_YELLOW : C_WHITE, cur ? C_BLUE : C_BLACK);
        if (cur && (G.tick / 15) & 1) put(x, 14, BX_H, C_YELLOW, C_BLACK);
    }
    putsc(16, "TYPE OR USE UP/DOWN AND ENTER", C_LGRAY, C_BLACK);
    scr_flush();
}

static void draw_log(void) {
    panel(0, 0, COLS, SROWS, C_LCYAN, C_BLACK, "MESSAGES");
    int n = imin(logn, 22);
    for (int i = 0; i < n; i++) {
        int k = (logn - n + i) % LOGN;
        puts_(1, 2 + i, logt[k], i == n - 1 ? C_WHITE : C_LGRAY, C_BLACK);
    }
    putsc(24, " ESC CLOSES ", C_YELLOW, C_BLACK);
}

static void draw_frame(void) {
    rt_cls(C_BLACK);
    switch (G.state) {
    case S_TITLE: draw_title(); return;
    case S_OVER: draw_over(); return;
    case S_WIN: draw_win(); return;
    case S_ENTRY: draw_entry(); return;
    default: break;
    }
    draw_play();
    if (G.state == S_INV) list_items("PACK", "LETTER OR ENTER USES   ESC CLOSES", false);
    else if (G.state == S_PICK) list_items("THROW OR ZAP WHAT?", "KNIVES AND WANDS   ESC CANCELS", true);
    else if (G.state == S_LOG) draw_log();
    scr_flush();
}

/* ================================================================== game flow */
typedef struct { bool l, r, u, d, a, b, x, y, start, back; } Pad;
static Pad pad, pad_prev;
#define PHIT(f) (pad.f && !pad_prev.f)
static void pad_read(void) {
    pad_prev = pad;
    memset(&pad, 0, sizeof(pad));
    if (!rt_pad) return;
    SDL_GameController *p = rt_pad;
    int ax = SDL_GameControllerGetAxis(p, SDL_CONTROLLER_AXIS_LEFTX), ay = SDL_GameControllerGetAxis(p, SDL_CONTROLLER_AXIS_LEFTY);
    pad.l = ax < -14000 || SDL_GameControllerGetButton(p, SDL_CONTROLLER_BUTTON_DPAD_LEFT);
    pad.r = ax > 14000 || SDL_GameControllerGetButton(p, SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
    pad.u = ay < -14000 || SDL_GameControllerGetButton(p, SDL_CONTROLLER_BUTTON_DPAD_UP);
    pad.d = ay > 14000 || SDL_GameControllerGetButton(p, SDL_CONTROLLER_BUTTON_DPAD_DOWN);
    pad.a = SDL_GameControllerGetButton(p, SDL_CONTROLLER_BUTTON_A);
    pad.b = SDL_GameControllerGetButton(p, SDL_CONTROLLER_BUTTON_B) || SDL_GameControllerGetButton(p, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
    pad.x = SDL_GameControllerGetButton(p, SDL_CONTROLLER_BUTTON_X);
    pad.y = SDL_GameControllerGetButton(p, SDL_CONTROLLER_BUTTON_Y) || SDL_GameControllerGetButton(p, SDL_CONTROLLER_BUTTON_LEFTSHOULDER);
    pad.start = SDL_GameControllerGetButton(p, SDL_CONTROLLER_BUTTON_START);
    pad.back = SDL_GameControllerGetButton(p, SDL_CONTROLLER_BUTTON_BACK);
}

static void goto_title(int page) {
    G.state = S_TITLE;
    G.st = 0;
    G.title_page = page;
    G.quitq = false;
    if (page == 0) tune_title();
}
static void start_game(void) {
    new_game((Uint32)(SDL_GetPerformanceCounter() ^ (SDL_GetPerformanceCounter() >> 17)) | 1u);
    G.state = S_PLAY;
    G.st = 0;
    sfx_select();
}
static void finish_game(void) {
    G.rank = rt_score_rank(calc_score());
    if (G.rank >= 0) {
        G.state = S_ENTRY;
        G.st = 0;
        G.name_len = 0;
        G.letter = 0;
        memset(G.name, 0, sizeof(G.name));
    } else goto_title(3);
}
static void commit_entry(void) {
    if (G.name_len == 0) { strcpy(G.name, "???"); G.name_len = 3; }
    G.name[G.name_len] = 0;
    rt_score_insert(G.rank, G.name, calc_score(), G.depth);
    if (!selftest) rt_scores_save();
    sfx_select();
    goto_title(3);
}

static int nearest_visible(int *ox, int *oy, int skip) {
    int best = 99, bi = -1, seen_n = 0;
    for (int i = 0; i < MAXMON; i++) {
        Mon *m = &mons[i];
        if (!m->alive || !vis[m->y][m->x]) continue;
        int d = cheb(m->x, m->y, G.px, G.py);
        if (skip >= 0) { /* cycling: pick the next one by index after skip */
            seen_n++;
        }
        if (d < best) { best = d; bi = i; }
    }
    (void)seen_n;
    if (bi >= 0) { *ox = mons[bi].x; *oy = mons[bi].y; }
    return bi;
}
static void begin_target(int slot) {
    G.tgt_slot = slot;
    int x, y;
    if (nearest_visible(&x, &y, -1) >= 0) { G.tx = x; G.ty = y; }
    else { G.tx = G.px + 1; G.ty = G.py; }
    G.state = S_TARGET;
}
static void cycle_target(void) {
    int start = -1;
    for (int i = 0; i < MAXMON; i++) if (mons[i].alive && mons[i].x == G.tx && mons[i].y == G.ty) start = i;
    for (int k = 1; k <= MAXMON; k++) {
        int i = (start + k + MAXMON) % MAXMON;
        if (mons[i].alive && vis[mons[i].y][mons[i].x]) { G.tx = mons[i].x; G.ty = mons[i].y; return; }
    }
}
static bool has_throwable(void) {
    for (int i = 0; i < INVMAX; i++) if (G.inv[i].qty > 0 && (G.inv[i].kind == IK_KNIFE || G.inv[i].kind == IK_WAND)) return true;
    return false;
}
static void open_pick(void) {
    if (!has_throwable()) { MSG("You have nothing to throw or zap."); return; }
    G.state = S_PICK;
    G.cur = 0;
    for (int i = 0; i < INVMAX; i++) if (G.inv[i].qty > 0 && (G.inv[i].kind == IK_KNIFE || G.inv[i].kind == IK_WAND)) { G.cur = i; break; }
}
static void open_inv(void) {
    G.state = S_INV;
    G.cur = 0;
    for (int i = 0; i < INVMAX; i++) if (G.inv[i].qty > 0) { G.cur = i; break; }
}
static void move_cursor(int d) {
    for (int k = 1; k <= INVMAX; k++) {
        int i = (G.cur + d * k + INVMAX * 4) % INVMAX;
        if (G.inv[i].qty > 0) { G.cur = i; return; }
    }
}
static void use_slot(int slot) {
    if (slot < 0 || slot >= INVMAX || G.inv[slot].qty <= 0) return;
    int k = G.inv[slot].kind;
    if (k == IK_KNIFE || k == IK_WAND) { begin_target(slot); return; }
    act_use(slot);
    G.state = S_PLAY;
}
static void interact(void) {
    if (tile[G.py][G.px] == T_STAIRS) act_descend();
    else if (fitem_at(G.px, G.py) >= 0) act_pickup();
    else act_wait();
}

static bool key_dir(const SDL_Keysym *k, int *dx, int *dy) {
    *dx = *dy = 0;
    switch (k->scancode) {
    case SDL_SCANCODE_LEFT: case SDL_SCANCODE_KP_4: *dx = -1; return true;
    case SDL_SCANCODE_RIGHT: case SDL_SCANCODE_KP_6: *dx = 1; return true;
    case SDL_SCANCODE_UP: case SDL_SCANCODE_KP_8: *dy = -1; return true;
    case SDL_SCANCODE_DOWN: case SDL_SCANCODE_KP_2: *dy = 1; return true;
    case SDL_SCANCODE_KP_7: *dx = -1; *dy = -1; return true;
    case SDL_SCANCODE_KP_9: *dx = 1; *dy = -1; return true;
    case SDL_SCANCODE_KP_1: *dx = -1; *dy = 1; return true;
    case SDL_SCANCODE_KP_3: *dx = 1; *dy = 1; return true;
    default: break;
    }
    switch (k->sym) {
    case SDLK_a: *dx = -1; return true;
    case SDLK_d: *dx = 1; return true;
    case SDLK_w: *dy = -1; return true;
    case SDLK_s: *dy = 1; return true;
    case SDLK_y: *dx = -1; *dy = -1; return true;
    case SDLK_u: *dx = 1; *dy = -1; return true;
    case SDLK_b: *dx = -1; *dy = 1; return true;
    case SDLK_n: *dx = 1; *dy = 1; return true;
    default: return false;
    }
}

/* returns false to quit the program */
static bool handle_key(const SDL_Keysym *ks, bool repeat) {
    SDL_Keycode k = ks->sym;
    bool shift = (ks->mod & KMOD_SHIFT) != 0;
    int dx, dy;
    if (k == SDLK_m && !repeat) { rt_mute = !rt_mute; return true; }
    bool enter = k == SDLK_RETURN || k == SDLK_KP_ENTER;
    switch (G.state) {
    case S_TITLE:
        if (repeat) break;
        if (k == SDLK_ESCAPE) return false;
        if (enter || k == SDLK_SPACE) start_game();
        else if (k == SDLK_RIGHT || k == SDLK_KP_6) { G.title_page = (G.title_page + 1) % 4; G.st = 0; sfx_tick(); }
        else if (k == SDLK_LEFT || k == SDLK_KP_4) { G.title_page = (G.title_page + 3) % 4; G.st = 0; sfx_tick(); }
        else if (k == SDLK_F1) { G.title_page = 1; G.st = 0; }
        else if (k == SDLK_h) { G.title_page = 3; G.st = 0; }
        break;
    case S_PLAY:
        if (G.quitq) {
            if (repeat) break;
            if (k == SDLK_y) goto_title(0);
            else if (k == SDLK_n || k == SDLK_ESCAPE) G.quitq = false;
            break;
        }
        if (key_dir(ks, &dx, &dy)) { act_move(dx, dy); break; }
        if (repeat) { if (k == SDLK_r) act_rest(); break; }
        if (k == SDLK_ESCAPE) G.quitq = true;
        else if (k == SDLK_PERIOD && shift) act_descend();
        else if (k == SDLK_GREATER) act_descend();
        else if (k == SDLK_PERIOD || k == SDLK_KP_5 || k == SDLK_KP_PERIOD || k == SDLK_5 || k == SDLK_SPACE) act_wait();
        else if (k == SDLK_r) act_rest();
        else if (k == SDLK_g || k == SDLK_COMMA) act_pickup();
        else if (enter) interact();
        else if (k == SDLK_i) open_inv();
        else if (k == SDLK_t) open_pick();
        else if (k == SDLK_l) G.state = S_LOG;
        break;
    case S_INV:
    case S_PICK:
        if (repeat) break;
        if (k == SDLK_ESCAPE || k == SDLK_SPACE || k == SDLK_BACKSPACE) { G.state = S_PLAY; break; }
        if (k == SDLK_UP || k == SDLK_KP_8) { move_cursor(-1); break; }
        if (k == SDLK_DOWN || k == SDLK_KP_2) { move_cursor(1); break; }
        if (enter) { if (G.state == S_PICK) { int c = G.inv[G.cur].kind; if (c == IK_KNIFE || c == IK_WAND) begin_target(G.cur); } else use_slot(G.cur); break; }
        if (k >= SDLK_a && k < SDLK_a + INVMAX) {
            int slot = (int)(k - SDLK_a);
            if (G.state == S_PICK) {
                if (G.inv[slot].qty > 0 && (G.inv[slot].kind == IK_KNIFE || G.inv[slot].kind == IK_WAND)) begin_target(slot);
            } else use_slot(slot);
        }
        break;
    case S_TARGET:
        if (key_dir(ks, &dx, &dy)) {
            int nx = G.tx + dx, ny = G.ty + dy;
            if (inmap(nx, ny) && cheb(nx, ny, G.px, G.py) <= 12) { G.tx = nx; G.ty = ny; }
            break;
        }
        if (repeat) break;
        if (k == SDLK_ESCAPE) G.state = S_PLAY;
        else if (k == SDLK_TAB || k == SDLK_t) cycle_target();
        else if (enter || k == SDLK_f) {
            int slot = G.tgt_slot;
            G.state = S_PLAY;
            act_throw(slot, G.tx, G.ty);
        }
        break;
    case S_LOG:
        if (!repeat && (k == SDLK_ESCAPE || k == SDLK_l || k == SDLK_SPACE || enter)) G.state = S_PLAY;
        break;
    case S_OVER:
    case S_WIN:
        if (!repeat && enter && G.st > 30) finish_game();
        break;
    case S_ENTRY:
        if (repeat) break;
        if (k >= SDLK_a && k <= SDLK_z && G.name_len < 3) { G.name[G.name_len++] = (char)('A' + (k - SDLK_a)); sfx_tick(); }
        else if (k == SDLK_BACKSPACE && G.name_len > 0) G.name[--G.name_len] = 0;
        else if (k == SDLK_UP) { G.letter = (G.letter + 1) % 26; sfx_tick(); }
        else if (k == SDLK_DOWN) { G.letter = (G.letter + 25) % 26; sfx_tick(); }
        else if (enter && G.st > 10) commit_entry();
        break;
    }
    return true;
}

static void update(void) {
    G.tick++;
    G.st++;
    if (G.last_dmg_flash > 0) G.last_dmg_flash--;
    for (int i = 0; i < 48; i++) {
        if (fxs[i].delay > 0) fxs[i].delay--;
        else if (fxs[i].ttl > 0) fxs[i].ttl--;
    }
    pad_read();
    int pdx = (pad.r ? 1 : 0) - (pad.l ? 1 : 0), pdy = (pad.d ? 1 : 0) - (pad.u ? 1 : 0);
    switch (G.state) {
    case S_TITLE:
        if (G.st > 700) { G.title_page = (G.title_page + 1) % 4; G.st = 0; }
        if (PHIT(r)) { G.title_page = (G.title_page + 1) % 4; G.st = 0; }
        if (PHIT(l)) { G.title_page = (G.title_page + 3) % 4; G.st = 0; }
        if (PHIT(a) || PHIT(start)) start_game();
        break;
    case S_PLAY:
        if (G.quitq) {
            if (PHIT(a)) goto_title(0);
            if (PHIT(b) || PHIT(y)) G.quitq = false;
            break;
        }
        if (pdx || pdy) {
            if (G.pad_rep == 0 || (G.pad_rep > 14 && (G.pad_rep - 14) % 5 == 0)) act_move(pdx, pdy);
            G.pad_rep++;
        } else G.pad_rep = 0;
        if (PHIT(a)) interact();
        if (PHIT(b) || PHIT(x)) open_pick();
        if (PHIT(y)) open_inv();
        if (PHIT(back) || PHIT(start)) G.quitq = true;
        break;
    case S_INV:
    case S_PICK:
        if (PHIT(u)) move_cursor(-1);
        if (PHIT(d)) move_cursor(1);
        if (PHIT(a)) {
            if (G.state == S_PICK) { int c = G.inv[G.cur].kind; if (c == IK_KNIFE || c == IK_WAND) begin_target(G.cur); }
            else use_slot(G.cur);
        }
        if (PHIT(b) || PHIT(y) || PHIT(back)) G.state = S_PLAY;
        break;
    case S_TARGET:
        if (pdx || pdy) {
            if (G.pad_rep == 0 || (G.pad_rep > 14 && (G.pad_rep - 14) % 4 == 0)) {
                int nx = G.tx + pdx, ny = G.ty + pdy;
                if (inmap(nx, ny) && cheb(nx, ny, G.px, G.py) <= 12) { G.tx = nx; G.ty = ny; }
            }
            G.pad_rep++;
        } else G.pad_rep = 0;
        if (PHIT(x)) cycle_target();
        if (PHIT(a)) { int slot = G.tgt_slot; G.state = S_PLAY; act_throw(slot, G.tx, G.ty); }
        if (PHIT(b) || PHIT(y) || PHIT(back)) G.state = S_PLAY;
        break;
    case S_LOG:
        if (PHIT(a) || PHIT(b) || PHIT(y)) G.state = S_PLAY;
        break;
    case S_OVER:
    case S_WIN:
        if (G.st > 30 && PHIT(a)) finish_game();
        break;
    case S_ENTRY:
        if (PHIT(u)) { G.letter = (G.letter + 1) % 26; sfx_tick(); }
        if (PHIT(d)) { G.letter = (G.letter + 25) % 26; sfx_tick(); }
        if (PHIT(a) && G.name_len < 3) { G.name[G.name_len++] = (char)('A' + G.letter); sfx_tick(); }
        if (PHIT(x) && G.name_len > 0) G.name[--G.name_len] = 0;
        if (PHIT(start) && G.st > 10) commit_entry();
        break;
    }
    if (G.dead && (G.state == S_PLAY || G.state == S_TARGET || G.state == S_INV || G.state == S_PICK)) {
        G.state = S_OVER;
        G.st = 0;
        tune_over();
    }
}

/* ================================================================== self-test bot */
static short bpar[MH * MW], bq[MH * MW];
static unsigned char bot_ignore[MH][MW];
static int bot_goal_ok(int mode, int x, int y, int tx, int ty) {
    switch (mode) {
    case 0:
        for (int j = -1; j <= 1; j++)
            for (int i = -1; i <= 1; i++)
                if (inmap(x + i, y + j) && !seen[y + j][x + i]) return 1;
        return 0;
    case 1: {
        int f = fitem_at(x, y);
        if (f < 0) return 0;
        if (bot_ignore[y][x]) return 0;
        if (fit[f].it.kind == IK_GOLD || fit[f].it.kind == IK_ARTEFACT) return 1;
        for (int i = 0; i < INVMAX; i++) if (G.inv[i].qty <= 0) return 1;
        if (stackable(&fit[f].it))
            for (int i = 0; i < INVMAX; i++) if (G.inv[i].kind == fit[f].it.kind && G.inv[i].sub == fit[f].it.sub) return 1;
        return 0;
    }
    case 2: return tile[y][x] == T_STAIRS;
    default: return x == tx && y == ty;
    }
}
static bool bot_bfs(int mode, int tx, int ty, int *odx, int *ody) {
    int qh = 0, qt = 0;
    for (int i = 0; i < MH * MW; i++) bpar[i] = -2;
    int start = G.py * MW + G.px;
    bpar[start] = -1;
    bq[qt++] = (short)start;
    while (qh < qt) {
        int c = bq[qh++], x = c % MW, y = c / MW;
        if (c != start && bot_goal_ok(mode, x, y, tx, ty)) {
            int cur = c;
            while (bpar[cur] != start && bpar[cur] >= 0) cur = bpar[cur];
            *odx = cur % MW - G.px;
            *ody = cur / MW - G.py;
            return true;
        }
        for (int k = 0; k < 8; k++) {
            int nx = x + DXS[k], ny = y + DYS[k];
            if (!inmap(nx, ny) || !seen[ny][nx] || bpar[ny * MW + nx] != -2 || !can_step(x, y, DXS[k], DYS[k])) continue;
            if (mon_at(nx, ny) && !(mode == 3 && nx == tx && ny == ty)) continue;
            bpar[ny * MW + nx] = (short)c;
            bq[qt++] = (short)(ny * MW + nx);
        }
    }
    return false;
}
static int bot_find(int kind, int sub) {
    for (int i = 0; i < INVMAX; i++)
        if (G.inv[i].qty > 0 && G.inv[i].kind == kind && (sub < 0 || G.inv[i].sub == sub)) return i;
    return -1;
}
static int bot_unknown(int kind) {
    for (int i = 0; i < INVMAX; i++) {
        if (G.inv[i].qty <= 0 || G.inv[i].kind != kind) continue;
        if (kind == IK_POTION && !G.pot_known[G.inv[i].sub]) return i;
        if (kind == IK_SCROLL && !G.scr_known[G.inv[i].sub]) return i;
    }
    return -1;
}
static bool bot_upgrade(void) {
    int bw = -1, ba = -1;
    float ws = G.eqw >= 0 ? (WEAP[G.inv[G.eqw].sub].mn + WEAP[G.inv[G.eqw].sub].mx) / 2.0f + G.inv[G.eqw].ench : 1;
    int as = G.eqa >= 0 ? ARM[G.inv[G.eqa].sub].ac + G.inv[G.eqa].ench : -1;
    for (int i = 0; i < INVMAX; i++) {
        if (G.inv[i].qty <= 0) continue;
        if (G.inv[i].kind == IK_WEAPON && i != G.eqw) {
            float s = (WEAP[G.inv[i].sub].mn + WEAP[G.inv[i].sub].mx) / 2.0f + G.inv[i].ench;
            if (s > ws + 0.4f) { ws = s; bw = i; }
        }
        if (G.inv[i].kind == IK_ARMOR && i != G.eqa) {
            int s = ARM[G.inv[i].sub].ac + G.inv[i].ench;
            if (s > as) { as = s; ba = i; }
        }
    }
    if (bw >= 0) return act_use(bw);
    if (ba >= 0) return act_use(ba);
    return false;
}
static int bot_wait_streak;
static int bot_level_turns;
static void bot_turn(void) {
    if (bot_level_turns == 0) memset(bot_ignore, 0, sizeof(bot_ignore));
    bot_level_turns++;
    int nearest = -1, nd = 99, adj = 0;
    for (int i = 0; i < MAXMON; i++) {
        Mon *m = &mons[i];
        if (!m->alive || !vis[m->y][m->x]) continue;
        int d = cheb(m->x, m->y, G.px, G.py);
        if (d == 1 && can_step(G.px, G.py, m->x - G.px, m->y - G.py)) adj++;
        if (d < nd) { nd = d; nearest = i; }
    }
    int hpp = G.hp * 100 / G.maxhp;
    if (G.hunger < 400) {
        int f = bot_find(IK_FOOD, -1);
        if (f >= 0 && act_use(f)) return;
    }
    if (nearest >= 0 && !(MD[mons[nearest].type].flags & F_STILL && nd > 1) ) {
        Mon *m = &mons[nearest];
        const MonDef *d = &MD[m->type];
        if (hpp < 45) {
            int h = -1;
            for (int i = 0; i < INVMAX; i++) if (G.inv[i].qty > 0 && G.inv[i].kind == IK_POTION && G.inv[i].sub == P_HEAL && G.pot_known[P_HEAL]) h = i;
            if (h < 0) h = bot_unknown(IK_POTION);
            if (h >= 0 && act_use(h)) return;
            int s = bot_find(IK_SCROLL, S_TELE);
            if (s >= 0 && G.scr_known[S_TELE] && hpp < 30 && act_use(s)) return;
            s = bot_unknown(IK_SCROLL);
            if (s >= 0 && hpp < 30 && act_use(s)) return;
        }
        /* ranged options */
        bool dangerous = d->hp >= 14 || hpp < 60;
        if (nd >= 2 && nd <= 8 && !m->asleep) {
            for (int i = 0; i < INVMAX; i++) {
                Item *it = &G.inv[i];
                if (it->qty <= 0 || it->kind != IK_WAND) continue;
                bool known = G.wand_known[it->sub];
                if ((!known || (it->sub != W_TERROR && dangerous) || (it->sub == W_TERROR && hpp < 40)) && act_throw(i, m->x, m->y)) return;
            }
        }
        if (nd >= 2 && nd <= 7) {
            int k = bot_find(IK_KNIFE, -1);
            if (k >= 0 && act_throw(k, m->x, m->y)) return;
        }
        if (adj > 0) {
            Mon *t = NULL;
            for (int i = 0; i < MAXMON; i++) {
                Mon *o = &mons[i];
                if (!o->alive || cheb(o->x, o->y, G.px, G.py) != 1 || !can_step(G.px, G.py, o->x - G.px, o->y - G.py)) continue;
                if (!t || o->hp < t->hp) t = o;
            }
            if (t && act_move(t->x - G.px, t->y - G.py)) return;
        }
        if (!m->asleep && m->aware && nd <= 6 && bot_wait_streak < 5) { bot_wait_streak++; act_wait(); return; }
        bot_wait_streak = 0;
        int dx, dy;
        if (hpp >= 60 && bot_bfs(3, m->x, m->y, &dx, &dy) && act_move(dx, dy)) return;
    } else bot_wait_streak = 0;
    if (!monster_in_view()) {
        if (bot_upgrade()) return;
        int s = bot_find(IK_SCROLL, S_ENCW);
        if (s >= 0 && G.scr_known[S_ENCW] && act_use(s)) return;
        s = bot_find(IK_SCROLL, S_ENCA);
        if (s >= 0 && G.scr_known[S_ENCA] && act_use(s)) return;
        if (hpp < 70 && G.hunger > 150) { if (act_rest()) return; }
        if (hpp >= 70) {
            int u = bot_unknown(IK_SCROLL);
            if (u >= 0 && act_use(u)) return;
            u = bot_unknown(IK_POTION);
            if (u >= 0 && act_use(u)) return;
        }
        if (G.hp >= G.maxhp - 2) {
            int p = bot_find(IK_POTION, P_VIGOR);
            if (p >= 0 && G.pot_known[P_VIGOR] && act_use(p)) return;
            p = bot_find(IK_POTION, P_MIGHT);
            if (p >= 0 && G.pot_known[P_MIGHT] && act_use(p)) return;
        }
    }
    if (fitem_at(G.px, G.py) >= 0 && !bot_ignore[G.py][G.px]) {
        if (act_pickup()) return;
        bot_ignore[G.py][G.px] = 1;
    }
    int dx, dy;
    bool descend_now = bot_level_turns > 2500;
    if (!descend_now) {
        if (bot_bfs(1, 0, 0, &dx, &dy) && act_move(dx, dy)) return;
        if (bot_bfs(0, 0, 0, &dx, &dy) && act_move(dx, dy)) return;
    }
    if (tile[G.py][G.px] == T_STAIRS) {
        if (hpp < 80 && G.hunger > 150 && !monster_in_view() && act_rest()) return;
        int dep = G.depth;
        bot_level_turns = 0;
        if (act_descend() && G.depth != dep) return;
    }
    if (bot_bfs(2, 0, 0, &dx, &dy) && act_move(dx, dy)) return;
    if (bot_bfs(1, 0, 0, &dx, &dy) && act_move(dx, dy)) return;
    int k = rnd(8);
    if (!act_move(DXS[k], DYS[k])) act_wait();
}

/* ================================================================== self-test */
static int check_level(int seed, int depth) {
    int fails = 0;
#define CHK(cond, ...) do { if (!(cond)) { printf("  FAIL seed %d depth %d: ", seed, depth); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)
    CHK(passable(G.px, G.py), "start in wall");
    CHK(sd[G.py][G.px] == 0, "start not reachable");
    int stairs = 0;
    for (int y = 0; y < MH; y++)
        for (int x = 0; x < MW; x++) {
            if (tile[y][x] == T_STAIRS) { stairs++; CHK(sd[y][x] > 0, "stairs unreachable at %d,%d", x, y); }
            if (x == 0 || y == 0 || x == MW - 1 || y == MH - 1) CHK(tile[y][x] == T_WALL, "border breached");
        }
    CHK(depth == NDEPTH ? stairs == 0 : stairs == 1, "stairs count %d", stairs);
    int kings = 0, art = 0;
    for (int i = 0; i < MAXMON; i++) {
        Mon *m = &mons[i];
        if (!m->alive) continue;
        CHK(passable(m->x, m->y), "monster %d in wall", i);
        CHK(passable(m->x, m->y) && sd[m->y][m->x] >= 0, "monster %d unreachable", i);
        CHK(!(m->x == G.px && m->y == G.py), "monster on player");
        for (int j = i + 1; j < MAXMON; j++) CHK(!(mons[j].alive && mons[j].x == m->x && mons[j].y == m->y), "monsters stacked");
        if (m->type == MD_KING) kings++;
    }
    for (int i = 0; i < MAXITEM; i++) {
        if (!fit[i].on) continue;
        CHK(passable(fit[i].x, fit[i].y), "item %d in wall", i);
        CHK(passable(fit[i].x, fit[i].y) && sd[fit[i].y][fit[i].x] >= 0, "item %d unreachable", i);
        if (fit[i].it.kind == IK_ARTEFACT) art++;
    }
    CHK(depth == NDEPTH ? (kings == 1 && art == 1) : (kings == 0 && art == 0), "boss/artefact kings=%d art=%d", kings, art);
    return fails;
#undef CHK
}

static int run_selftest(const char *dir) {
    char path[1200];
    int fails = 0;
    selftest = true;
    /* 1: level generation */
    int nseeds = 520, caves = 0, levels = 0;
    for (int s = 1; s <= nseeds; s++) {
        new_game((Uint32)s * 7919u + 13u);
        G.state = S_TITLE;
        fails += check_level(s, 1);
        levels++;
        for (int d = 2; d <= NDEPTH; d++) {
            G.depth = d;
            gen_level(d);
            if (cave_level) caves++;
            fails += check_level(s, d);
            levels++;
        }
    }
    printf("levelgen: %d seeds x %d floors = %d levels (%d caves), %d failures\n", nseeds, NDEPTH, levels, caves, fails);

    /* 2: autopilot bot using the real turn code */
    int nbots = 8;
    const char *e = getenv("DD_BOTS");
    if (e) nbots = atoi(e);
    int past3 = 0, deepest = 0, wins = 0;
    for (int b = 0; b < nbots; b++) {
        new_game((Uint32)(b + 1) * 104729u);
        G.state = S_PLAY;
        bot_level_turns = 0;
        bot_wait_streak = 0;
        int steps = 0;
        while (!G.dead && !G.won && steps < 60000) { bot_turn(); steps++; }
        printf("bot %d: depth %d, level %d, turns %d, gold %d, kills %d, %s\n", b + 1, G.depth, G.lvl, G.turn, G.gold, G.kills,
               G.won ? "WON" : G.dead ? G.killer : "alive (step cap)");
        if (G.depth > 3) past3++;
        if (G.depth > deepest) deepest = G.depth;
        if (G.won) wins++;
        if (steps >= 60000 && !G.dead && !G.won) { printf("  note: bot hit the step cap\n"); }
    }
    printf("bots: %d of %d got past depth 3, deepest %d, wins %d\n", past3, nbots, deepest, wins);

    /* 3: screenshots */
    rt_score_path[0] = 0; /* nothing can ever be written from the self-test */
#define SHOT(name) do { draw_frame(); snprintf(path, sizeof(path), "%s/%s.bmp", dir, name); rt_shot(path); } while (0)
    static const RtScore DEF[RT_NSCORES] = {
        {"ROK", 4400, 7}, {"IVY", 3300, 6}, {"DIG", 2500, 5}, {"AMY", 1800, 4}, {"JET", 1200, 3}, {"GEM", 800, 3}, {"BAT", 400, 2}, {"OLD", 200, 1}};
    memcpy(rt_scores, DEF, sizeof(rt_scores));
    for (int pg = 0; pg < 4; pg++) {
        goto_title(pg);
        G.tick = pg == 0 ? 230 : 40 + pg * 17;
        char n[16];
        snprintf(n, sizeof(n), "title%d", pg);
        SHOT(n);
    }
    static const int shot_depths[] = {1, 2, 4, 7, 10};
    for (int si = 0; si < 5; si++) {
        int d = shot_depths[si];
        new_game(300 + (Uint32)d * 31u);
        G.state = S_PLAY;
        G.god = true;
        G.depth = d;
        if (d > 1) {
            gen_level(d);
            G.lvl = 4 + d; G.maxhp = 30 + d * 6; G.hp = G.maxhp; G.gold = 40 * d;
        }
        update_fov();
        compute_dmap();
        bot_level_turns = 0;
        for (int k = 0; k < 90 + d * 20 && !G.won; k++) bot_turn();
        G.god = false;
        G.hp = imax(1, G.hp);
        G.won = false;
        G.state = S_PLAY;
        /* put a few monsters in sight for the picture */
        int placed = 0;
        for (int r = 2; r <= 5 && placed < 3; r++)
            for (int k = 0; k < 8 && placed < 3; k++) {
                int nx = G.px + DXS[k] * r, ny = G.py + DYS[k] * r;
                if (inmap(nx, ny) && tile[ny][nx] == T_FLOOR && !mon_at(nx, ny) && vis[ny][nx] && los(G.px, G.py, nx, ny)) {
                    Mon *m = spawn_mon(pick_monster(d), nx, ny, true);
                    if (m) placed++;
                }
            }
        for (int i = 0; i < 48; i++) fxs[i].ttl = 0;
        G.turn += 10;
        char n[24];
        snprintf(n, sizeof(n), "play_depth%d", d);
        SHOT(n);
        if (d == 4) {
            /* inventory with a bit of everything */
            G.god = false;
            memset(G.inv, 0, sizeof(G.inv));
            G.inv[0] = (Item){IK_WEAPON, 3, 1, 1, 0};
            G.inv[1] = (Item){IK_ARMOR, 3, 0, 1, 0};
            G.inv[2] = (Item){IK_KNIFE, 0, 0, 5, 0};
            G.inv[3] = (Item){IK_POTION, P_HEAL, 0, 2, 0};
            G.inv[4] = (Item){IK_POTION, P_VENOM, 0, 1, 0};
            G.inv[5] = (Item){IK_SCROLL, S_MAP, 0, 1, 0};
            G.inv[6] = (Item){IK_SCROLL, S_SMITE, 0, 3, 0};
            G.inv[7] = (Item){IK_WAND, W_SPARK, 0, 1, 4};
            G.inv[8] = (Item){IK_WAND, W_FROST, 0, 1, 3};
            G.inv[9] = (Item){IK_FOOD, 2, 0, 2, 0};
            G.inv[10] = (Item){IK_FOOD, 1, 0, 1, 0};
            G.inv[11] = (Item){IK_POTION, P_HASTE, 0, 1, 0};
            G.eqw = 0; G.eqa = 1;
            G.pot_known[P_HEAL] = G.scr_known[S_MAP] = G.wand_known[W_SPARK] = true;
            G.state = S_INV;
            G.cur = 3;
            SHOT("inventory");
            G.state = S_PICK;
            G.cur = 2;
            SHOT("pick");
            G.state = S_PLAY;
            begin_target(2);
            for (int k = 0; k < 40; k++) G.tick++;
            SHOT("target");
            G.state = S_PLAY;
            G.quitq = true;
            SHOT("quit");
            G.quitq = false;
            G.state = S_LOG;
            SHOT("log");
            G.state = S_PLAY;
        }
    }
    new_game(77);
    G.depth = 5; G.gold = 312; G.kills = 41; G.lvl = 6; G.turn = 2210;
    snprintf(G.killer, sizeof(G.killer), "%s", "hollow acolyte");
    G.dead = true;
    G.state = S_OVER;
    G.st = 100;
    SHOT("death");
    G.rank = 2; G.state = S_ENTRY; G.name_len = 2; strcpy(G.name, "ZED"); G.letter = 3; G.st = 20;
    SHOT("entry");
    G.dead = false; G.won = true; G.depth = 10; G.state = S_WIN; G.st = 100; G.gold = 1432; G.kills = 128;
    SHOT("victory");
    if (fails) printf("selftest: FAILED (%d)\n", fails);
    else printf("selftest: OK\n");
    return fails ? 1 : 0;
}

/* ================================================================== main */
static const RtScore DEFAULT_SCORES[RT_NSCORES] = {
    {"ROK", 4400, 7}, {"IVY", 3300, 6}, {"DIG", 2500, 5}, {"AMY", 1800, 4},
    {"JET", 1200, 3}, {"GEM", 800, 3},  {"BAT", 400, 2},  {"OLD", 200, 1},
};

int main(int argc, char **argv) {
    const char *shotdir = NULL;
    for (int i = 1; i < argc; i++)
        if (!strcmp(argv[i], "--selftest") && i + 1 < argc) shotdir = argv[++i];
    selftest = shotdir != NULL;
    if (!rt_init("Deep Delve")) return 1;
    build_atlas();
    memcpy(rt_scores, DEFAULT_SCORES, sizeof(rt_scores));
    G.rank = -1;
    if (selftest) return run_selftest(shotdir);

    rt_audio_init();
    rt_scores_load("deep-delve", DEFAULT_SCORES);
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
            else if (e.type == SDL_KEYDOWN) {
                if (!handle_key(&e.key.keysym, e.key.repeat != 0)) running = false;
            }
        }
        Uint64 now = SDL_GetPerformanceCounter();
        acc += (double)(now - last) / (double)freq;
        last = now;
        if (acc > 0.25) acc = 0.25;
        while (acc >= dt) {
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
