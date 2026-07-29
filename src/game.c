/* XQuest 32X - main game logic.
 *
 * C port of the XQuest 1.3 gameplay (original DOS game by Mark Mackey,
 * 1994-1996). Fixed-point conventions follow the original: positions
 * are 10.6 fixed point ("sx/sy", pixels = sx>>6), velocities in the
 * same scale per frame.
 */
#include <stdint.h>
#include "hw32x.h"
#include "render.h"
#include "assets.h"
#include "sound.h"

/* ------------------------------------------------------------ tunables */
#define MAXMISSILES     50
#define MISSILELIFE     300
#define MAXOBJECTS      65
#define MAXEMISSILES    70
#define MAXEMINES       60
#define MAXENEMIES      40
#define FRAMERATE       60          /* 32X vsync (original was ~67) */
#define MAXSHIPPICS     24
#define MAXSHIPSPEED    640
#define BASEGAMESPEED   72          /* original 64 at 67Hz -> scale to 60Hz */
#define STARTLIVES      3
#define STARTBOMBS      3
#define START_SHIPDESTROYED 64

#define SHIPMINX        10
#define SHIPMINY        10
#define SHIPMAXX        (PAGE_W-11)
#define SHIPMAXY        (PAGE_H-11)
#define ENEMYXMIN       10
#define ENEMYXMAX       (PAGE_W-10)
#define ENEMYYMIN       10
#define ENEMYYMAX       (PAGE_H-10)
#define ENEMYSTARTY     (PAGE_H/2-5)
#define MINEXMIN        20
#define MINEXMAX        (PAGE_W-21)
#define MINEYMIN        30
#define MINEYMAX        (PAGE_H-21)
#define MINGATEX        20
#define MAXGATEX        (PAGE_W-30)
#define SHIPSTARTX      (PAGE_W/2)
#define SHIPSTARTY      (PAGE_H/2)
#define SCREENHBORDER   (VIEW_W/2-20)
#define SCREENVBORDER   (VIEW_H/2)
#define MAXVISX         (PAGE_W-VIEW_W)
#define MAXVISY         (PAGE_H-VIEW_H)
#define TIMERX          280
#define TIMERY          10

enum { OBJ_CRYS = 0, OBJ_MINE = 1, OBJ_SMART = 2 };
enum { PU_SHIELD, PU_AIMED, PU_RAPID, PU_MULTI, PU_ASS, PU_HEAVY, PU_BOUNCE,
       PU_COUNT };

/* powerup min/random duration in frames (from xqvars.pas) */
static const uint16_t pu_timemin[PU_COUNT] = {
    10 * FRAMERATE, 30 * FRAMERATE, 60 * FRAMERATE, 60 * FRAMERATE,
    60 * FRAMERATE, 60 * FRAMERATE, 30 * FRAMERATE };
static const uint16_t pu_timeran[PU_COUNT] = {
    15 * FRAMERATE, 60 * FRAMERATE, 90 * FRAMERATE, 90 * FRAMERATE,
    90 * FRAMERATE, 90 * FRAMERATE, 60 * FRAMERATE };

#define SUPERTIMEMIN    (5*FRAMERATE)
#define SUPERTIMERAN    (5*FRAMERATE)

/* difficulty table {rebound, speedfactor<<6, enemyfrequency<<6} */
static const struct { uint8_t rebound; int16_t speed64, freq64; } diffinfo[5] = {
    { 1, 45, 45 }, { 1, 64, 64 }, { 0, 64, 64 }, { 0, 96, 77 }, { 0, 128, 96 },
};
static const char *const diffname[5] =
    { "WIMP", "TIMID", "AVERAGE", "TRICKY", "INHUMAN" };

/* smartbomb screen flash palette ramp (VGA 6-bit -> CRAM done at use) */
static const uint8_t sbpal[11][3] = {
    {0,0,0},{8,0,0},{14,0,0},{20,4,0},{26,12,0},{32,20,10},
    {38,28,20},{44,36,30},{50,44,40},{56,52,50},{63,63,63} };

/* ------------------------------------------------------------- types */
typedef struct {
    int16_t x, y, xbr, ybr, oldx, oldy;
    int16_t sx, sy, delx, dely;
    int16_t time;
} missile_t;

typedef struct {
    int16_t x, y, xbr, ybr, oldx, oldy;
    int32_t sx, sy;
    int16_t delx, dely;
    int16_t kind;               /* emisskind index 1..6 */
} emissile_t;

typedef struct {
    int16_t x, y, xbr, ybr;
} eminepos_t;

typedef struct {
    int16_t x, y, xbr, ybr, oldx, oldy;
    int32_t sx, sy;
    int16_t delx, dely;
    int16_t curvecos, curvesin;
    int16_t ntyp;               /* enemykind index */
    int16_t hit, frame;
    int16_t supertime;
} enemy_t;

typedef struct {
    int16_t x, y, xbr, ybr;
    uint8_t typ;                /* OBJ_* */
    uint8_t deleted;
} object_t;

typedef struct {
    int16_t mspeed;
    uint8_t soundnum, rebound, firedirect;
} emisskind_t;

static const emisskind_t emisskind[7] = {
    { 0, 0, 0, 0 },
    { 120, SND_FIRE6, 0, 0 },
    { 150, SND_FIRE5, 0, 0 },
    { 200, SND_RETALIATE, 0, 1 },
    { 150, 0, 1, 0 },
    { 150, SND_FIRE4, 0, 1 },
    { 170, 0, 0, 0 },
};

/* ------------------------------------------------------------- state */
static struct {
    int16_t x, y, xbr, ybr, oldx, oldy;
    int32_t sx, sy;
    int16_t delx, dely;
    int16_t dir;                /* 0..23 ship picture */
    int16_t nummissiles;
} ship;

static missile_t missiles[MAXMISSILES + 1];
static emissile_t emissiles[MAXEMISSILES + 1];
static eminepos_t emines[MAXEMINES + 1];
static enemy_t enemy[MAXENEMIES + 1];
static object_t objects[MAXOBJECTS + 1];

static int16_t NumEnemies, NumEnemyMissiles, NumEnemyMines;
static uint8_t EminesAdding;

static struct {
    int16_t level, totallevel;
    int16_t numsmartbombs, lives, gameclocked;
    int32_t score, newmanscore, lastnewmanscore;
    int32_t timeonlevel;
    int16_t numobjects, numcrystals, nummines, numsmarts;
    uint8_t gatemovepos;
    int16_t attractorx, attractory;
} G;

static int16_t DiffLevel = 2;
static int16_t GameSpeed = BASEGAMESPEED;
static int16_t pu_value[PU_COUNT];
static int16_t pu_position[PU_COUNT];
static uint8_t GameOver, LevelFinished, ShipDestroyed, NoMines, GateMoving;
static int16_t ShipDestroyedCount;
static int16_t SmartBombed;
static int16_t EnemyEnteringLeft, EnemyEnteringRight;
static int16_t EnemyLeftType, EnemyRightType;
static int16_t GateMoveCount;
static int16_t GateLX, GateRX;      /* left/right gate sprite x */
static int16_t VisX, VisY;          /* visible window origin */
static uint32_t FrameCount;
static int16_t cost[16], sint[16];  /* explosion tables, <<15 */

/* saved-under buffers */
#define MAXSPR (28*28)
static uint8_t under_ship[MAXSPR];
static uint8_t under_obj[MAXOBJECTS + 1][MAXSPR];
static uint8_t under_enemy[MAXENEMIES + 1][MAXSPR];

/* input mapped from pad */
static int16_t in_dx, in_dy;
static uint8_t in_fire, in_fire_held, in_smart, in_start, in_pause;
static uint16_t prevpad;

/* =========================================================== helpers */
static uint32_t rngstate = 0x12345678;

static uint16_t rand16(void)
{
    /* xorshift32, take high 16 bits */
    rngstate ^= rngstate << 13;
    rngstate ^= rngstate >> 17;
    rngstate ^= rngstate << 5;
    return (uint16_t)(rngstate >> 16);
}

static int16_t randn(int16_t n)     /* 0..n-1 like Pascal random(n) */
{
    if (n <= 0)
        return 0;
    return (int16_t)(((uint32_t)rand16() * (uint16_t)n) >> 16);
}

static int16_t iabs16(int16_t v) { return v < 0 ? -v : v; }
static int32_t iabs32(int32_t v) { return v < 0 ? -v : v; }

static int32_t isqrt32(int32_t v)
{
    int32_t r = 0, b = 1 << 30;
    if (v < 0)
        return 0;
    while (b > v)
        b >>= 2;
    while (b) {
        if (v >= r + b) {
            v -= r + b;
            r = (r >> 1) + b;
        } else
            r >>= 1;
        b >>= 2;
    }
    return r;
}

/* sin table for ship direction: 24 entries, <<14 */
static const int16_t dir_sin24[24] = {
    0, 4240, 8192, 11585, 14189, 15826, 16384, 15826, 14189, 11585,
    8192, 4240, 0, -4240, -8192, -11585, -14189, -15826, -16384,
    -15826, -14189, -11585, -8192, -4240 };
#define dir_cos24(i) dir_sin24[((i)+6)%24]

static void numstr(int32_t v, char *buf, int width)
{
    /* simpler correct version */
    char tmp[12];
    int i = 0, n = 0, neg = 0;
    if (v < 0) { neg = 1; v = -v; }
    do { tmp[i++] = '0' + (v % 10); v /= 10; } while (v);
    if (neg) tmp[i++] = '-';
    while (width > i)
        buf[n++] = ' ', width--;
    while (i > 0)
        buf[n++] = tmp[--i];
    buf[n] = 0;
}

/* ====================================================== sprite helpers */
static int enemy_sprite(const enemy_t *e)
{
    return enemy_frame_base[e->ntyp] + (e->frame >> 8);
}

static int collide_masks(const sprite_t *a, int ax, int ay,
                         const sprite_t *b, int bx, int by)
{
    /* overlap of row masks; a assumed left of b (or equal) */
    int y0 = (ay > by) ? ay : by;
    int y1 = ((ay + a->h) < (by + b->h)) ? ay + a->h : by + b->h;
    int shift = bx - ax;
    int y;
    if (!a->mask || !b->mask)
        return 1;
    if (shift < 0 || shift > 31)
        return 0;
    for (y = y0; y < y1; y++) {
        uint32_t ma = a->mask[y - ay];
        uint32_t mb = b->mask[y - by];
        if (ma & (mb >> shift))
            return 1;
    }
    return 0;
}

static int sprites_collide(int ia, int ax, int ay, int ib, int bx, int by)
{
    const sprite_t *a = &gfx_sprites[ia];
    const sprite_t *b = &gfx_sprites[ib];
    if (ax <= bx)
        return collide_masks(a, ax, ay, b, bx, by);
    return collide_masks(b, bx, by, a, ax, ay);
}

/* ========================================================= status bar */
static void ShowScore(void);
static void ShowLives(void);
static void ShowSmartBombs(void);
static void ShowCrystals(void);
static void ShowPowerUps(void);

static void ShowScore(void)
{
    char s[16];
    numstr(G.score, s, 8);
    r_text(1, 10, 5, s);
}

static void ShowLives(void)
{
    char s[8], t[12];
    numstr(G.lives - 1, s, 2);
    t[0] = ' '; t[1] = ' ';
    t[2] = s[0]; t[3] = s[1]; t[4] = 0;
    r_text(1, 185, 5, t);
    r_status_sprite(GFX_HUDSHIP, 185, 6);
}

static void ShowSmartBombs(void)
{
    char s[8], t[12];
    numstr(G.numsmartbombs, s, 2);
    t[0] = ' '; t[1] = ' ';
    t[2] = s[0]; t[3] = s[1]; t[4] = 0;
    r_text(1, 230, 5, t);
    r_status_sprite(GFX_HUDSMART, 230, 6);
}

static void ShowCrystals(void)
{
    char s[8], t[12];
    numstr(G.numcrystals, s, 2);
    t[0] = ' '; t[1] = ' ';
    t[2] = s[0]; t[3] = s[1]; t[4] = 0;
    r_text(1, 275, 5, t);
    r_status_sprite(GFX_HUDCRYS, 275, 6);
}

static void ShowPowerUps(void)
{
    int i = 85, pu;
    for (pu = 0; pu < PU_COUNT; pu++) {
        if (pu_value[pu] > 0 && i <= 157) {
            pu_position[pu] = i;
            r_status_sprite(GFX_POWERUP0 + pu, i, 5);
            i += 18;
        } else
            pu_position[pu] = 0;
    }
    if (i <= 157)
        r_status_fill(i, 5, 157 + 17, 20, 14);
}

static void BonusShip(void)
{
    G.lastnewmanscore = G.newmanscore;
    G.newmanscore = G.lastnewmanscore +
        levels[(G.totallevel <= MAXLEVEL ? G.level : MAXLEVEL) - 1].newman;
    G.lives++;
    ShowLives();
    snd_play(SND_WOOHOO);
}

static void AddToScore(int32_t v)
{
    G.score += v;
    while (G.score >= G.newmanscore)
        BonusShip();
    ShowScore();
}

/* ======================================================== input layer */
/* D-pad tuning: thrust per frame, cruise cap (10.6 fixed: 320 = 5 px/f)
 * and coast damping applied when the stick is released. */
#define PAD_ACCEL       6
#define PAD_CRUISE      320
#define PAD_DAMP_SHIFT  3       /* delv -= delv/8 per idle frame */

/* base missile muzzle speed (10.6 fixed: 448 = 7 px/frame), added in
 * the ship's facing direction on top of the ship's own velocity */
#define MISSILE_MUZZLE  448

static void poll_input(void)
{
    uint16_t pad = hw_pad(0);
    uint16_t pressed = pad & ~prevpad;

    in_dx = 0;
    in_dy = 0;
    /* digital pad -> thrust, capped at a comfortable cruise speed so
     * the ship stays controllable (the original used mouse deltas) */
    if (pad & PAD_LEFT) {
        if (ship.delx > -PAD_CRUISE)
            in_dx = -PAD_ACCEL;
    } else if (pad & PAD_RIGHT) {
        if (ship.delx < PAD_CRUISE)
            in_dx = PAD_ACCEL;
    } else if (ship.delx != 0) {
        /* no horizontal input: coast down gently */
        int16_t d = ship.delx >> PAD_DAMP_SHIFT;
        if (d == 0)
            d = (ship.delx > 0) ? 1 : -1;
        ship.delx -= d;
    }
    if (pad & PAD_UP) {
        if (ship.dely > -PAD_CRUISE)
            in_dy = -PAD_ACCEL;
    } else if (pad & PAD_DOWN) {
        if (ship.dely < PAD_CRUISE)
            in_dy = PAD_ACCEL;
    } else if (ship.dely != 0) {
        int16_t d = ship.dely >> PAD_DAMP_SHIFT;
        if (d == 0)
            d = (ship.dely > 0) ? 1 : -1;
        ship.dely -= d;
    }
    /* A = strong brake (like keypad-5 brake in the original) */
    if (pad & PAD_A) {
        ship.delx -= ship.delx / 5;
        ship.dely -= ship.dely / 5;
    }
    in_fire = (pressed & PAD_B) != 0;
    in_fire_held = (pad & PAD_B) != 0;
    in_smart = (pressed & PAD_C) != 0;
    in_start = (pressed & PAD_START) != 0;
    in_pause = 0;
    prevpad = pad;
}

static uint16_t wait_any_button(void)
{
    uint16_t pad, pressed;
    for (;;) {
        hw_wait_vblank();
        pad = hw_pad(0);
        pressed = pad & ~prevpad;
        prevpad = pad;
        rand16();       /* keep RNG rolling like the original randomize */
        if (pressed & (PAD_A | PAD_B | PAD_C | PAD_START))
            return pressed;
    }
}

static int delay_or_event(int frames)
{
    /* returns 1 if a button was pressed during the delay */
    int i;
    uint16_t pad, pressed;
    for (i = 0; i < frames; i++) {
        hw_wait_vblank();
        pad = hw_pad(0);
        pressed = pad & ~prevpad;
        prevpad = pad;
        if (pressed & (PAD_A | PAD_B | PAD_C | PAD_START))
            return 1;
    }
    return 0;
}

/* ====================================================== level drawing */
static const uint8_t bcolor[5] = { 10, 15, 25, 15, 10 };

static int gate_width(void) { return gfx_sprites[GFX_GATEL].w; }
static int gate_height(void) { return gfx_sprites[GFX_GATEL].h; }

static void draw_playfield_statics(void)
{
    int i;

    r_clear_page(0);

    for (i = 1; i <= 5; i++) {  /* border pipes */
        r_hline(10, GateLX, 10 - i, bcolor[i - 1]);
        r_hline(GateRX + gate_width(), PAGE_W - 10, 10 - i, bcolor[i - 1]);
        r_hline(10, PAGE_W - 10, PAGE_H - 5 - i, bcolor[i - 1]);
        r_vline(4 + i, 10, PAGE_H - 9, bcolor[i - 1]);
        r_vline(PAGE_W - 11 + i, 10, PAGE_H - 9, bcolor[i - 1]);
    }

    r_put_sprite(GFX_GATEL, GateLX, 0);
    r_put_sprite(GFX_GATER, GateRX, 0);
    if (G.numcrystals > 0)     /* gate barrier */
        r_hline(GateLX + gate_width(), GateRX - 1, 7, 54);
    r_put_sprite(GFX_CTL, 0, 0);
    r_put_sprite(GFX_CTR, PAGE_W - 10, 0);
    r_put_sprite(GFX_CBR, PAGE_W - 10, PAGE_H - 10);
    r_put_sprite(GFX_CBL, 0, PAGE_H - 10);
    r_put_sprite(GFX_LGATE0, 0, PAGE_H / 2 - 10);
    r_put_sprite(GFX_RGATE0, PAGE_W - 20, PAGE_H / 2 - 10);
    if (G.gameclocked > 0)
        r_put_sprite(GFX_ATTRACTOR, G.attractorx - 6, G.attractory - 6);

    for (i = 0; i < 100; i++)
        r_putpix(randn(SHIPMAXX - SHIPMINX - 2) + SHIPMINX + 1,
                 randn(SHIPMAXY - SHIPMINY - 5) + SHIPMINY + 5,
                 randn(20) + 10);
    for (i = 0; i < 400; i++)
        r_putpix(randn(SHIPMAXX - SHIPMINX - 2) + SHIPMINX + 1,
                 randn(SHIPMAXY - SHIPMINY - 5) + SHIPMINY + 5,
                 randn(15) + 5);
}

/* ================================================== level setup logic */
static void SetupNewLevel(void)
{
    const level_t *lv = &levels[G.level - 1];
    int i, j, ok;

    G.newmanscore = G.lastnewmanscore +
        levels[(G.totallevel <= MAXLEVEL ? G.level : MAXLEVEL) - 1].newman;
    G.nummines = lv->nummine;
    G.numcrystals = lv->numcryst;
    G.numsmarts = 0;
    GateMoveCount = 0;
    G.timeonlevel = 0;
    GateLX = (PAGE_W - lv->gatewidth) / 2 - gate_width();
    GateRX = (PAGE_W + lv->gatewidth) / 2;
    while (rand16() < lv->smartprob && G.numsmarts < lv->maxsmart)
        G.numsmarts++;
    G.numobjects = G.nummines + G.numcrystals + G.numsmarts;
    NoMines = (G.nummines == 0);

    do {
        G.attractorx = randn(PAGE_W - 120) + 60;
        G.attractory = randn(PAGE_H - 120) + 60;
    } while (iabs16(G.attractorx - SHIPSTARTX) +
             iabs16(G.attractory - SHIPSTARTY) <= 60);

    {
        const sprite_t *os = &gfx_sprites[GFX_OBJ0];
        int ow = os->w, oh = os->h;
        const sprite_t *ss = &gfx_sprites[GFX_SHIP0];

        for (i = 1; i <= G.numobjects; i++) {
            do {
                object_t *o = &objects[i];
                o->x = randn(MINEXMAX - MINEXMIN) + MINEXMIN;
                o->y = randn(MINEYMAX - MINEYMIN) + MINEYMIN;
                o->xbr = o->x + ow - 1;
                o->ybr = o->y + oh - 1;
                ok = 1;
                for (j = 1; j < i; j++)
                    if (iabs16(objects[j].x - o->x) < ow + 2 &&
                        iabs16(objects[j].y - o->y) < oh + 2)
                        ok = 0;
                if (o->xbr + 8 >= SHIPSTARTX && o->ybr + 8 >= SHIPSTARTY &&
                    o->x <= SHIPSTARTX + ss->w + 8 &&
                    o->y <= SHIPSTARTY + ss->h + 8)
                    ok = 0;
                if (G.gameclocked > 0 &&
                    o->xbr + 8 >= G.attractorx && o->ybr + 8 >= G.attractory &&
                    o->x <= G.attractorx + 21 && o->y <= G.attractory + 21)
                    ok = 0;
                if (o->y < PAGE_H / 2 + 7 && o->ybr > PAGE_H / 2 - 7 &&
                    (o->x < SHIPMINX + 15 || o->xbr > SHIPMAXX - 15))
                    ok = 0;
                if (lv->gatemove == 0 && o->y < SHIPMINY + 18 &&
                    o->xbr > GateLX && o->x < GateRX)
                    ok = 0;
            } while (!ok);
            objects[i].typ = (i <= G.numcrystals) ? OBJ_CRYS :
                (i <= G.nummines + G.numcrystals) ? OBJ_MINE : OBJ_SMART;
            objects[i].deleted = 0;
        }
    }

    GameSpeed = (BASEGAMESPEED * diffinfo[DiffLevel].speed64 >> 6) +
        (BASEGAMESPEED * (int32_t)(G.gameclocked * 32) >> 6) / 2;
}

static void StartNewLevel(void)
{
    int i;

    NumEnemies = 0;
    NumEnemyMissiles = 0;
    NumEnemyMines = 0;
    EminesAdding = 0;
    LevelFinished = 0;
    ShipDestroyed = 0;
    EnemyEnteringLeft = 0;
    EnemyEnteringRight = 0;
    GateMoving = levels[G.level - 1].gatemove != 0;

    draw_playfield_statics();

    VisX = (PAGE_W - VIEW_W) / 2;
    VisY = (PAGE_H - VIEW_H) / 2;

    /* purge deleted objects */
    for (i = G.numobjects; i >= 1; i--)
        if (objects[i].deleted) {
            objects[i] = objects[G.numobjects];
            G.numobjects--;
        }

    ship.x = SHIPSTARTX;
    ship.y = SHIPSTARTY;
    ship.xbr = ship.x + gfx_sprites[GFX_SHIP0].w - 1;
    ship.ybr = ship.y + gfx_sprites[GFX_SHIP0].h - 1;
    ship.oldx = ship.x;
    ship.oldy = ship.y;
    ship.sx = ship.x << 6;
    ship.sy = ship.y << 6;
    ship.delx = 0;
    ship.dely = 0;
    ship.dir = 0;
    ship.nummissiles = 0;
    r_save_under(GFX_SHIP0, ship.x, ship.y, under_ship);

    for (i = 1; i <= G.numobjects; i++) {
        r_save_under(GFX_OBJ0, objects[i].x, objects[i].y, under_obj[i]);
        r_put_sprite(GFX_OBJ0 + objects[i].typ, objects[i].x, objects[i].y);
    }

    r_status_clear();
    AddToScore(0);
    ShowLives();
    ShowSmartBombs();
    ShowCrystals();
    ShowPowerUps();
}

/* ================================================== enemies + spawning */
static void DeleteEnemy(int i);

static void AddEnemy(int xc, int yc, int k)
{
    enemy_t *e;
    const enemykind_t *kd = &enemykind[k];

    if (NumEnemies >= MAXENEMIES)
        return;
    NumEnemies++;
    e = &enemy[NumEnemies];
    e->ntyp = k;
    if (!kd->maxspeed) {
        if (kd->speed > 0) {
            e->delx = (int16_t)((randn(kd->speed) - kd->speed2) * GameSpeed / 64);
            e->dely = (int16_t)((randn(kd->speed) - kd->speed2) * GameSpeed / 64);
        } else {
            e->delx = 0;
            e->dely = 0;
        }
        e->supertime = (k == 1) ?
            enemykind[1].numframes * (256 / enemykind[1].framespeed) : 0;
    } else {
        int idx = randn(24);
        int32_t c = dir_cos24(idx);       /* <<14 */
        e->delx = (int16_t)((kd->speed * c >> 14) * GameSpeed / 64);
        e->dely = (int16_t)((kd->speed * (16384 - (c * c >> 14)) >> 14) * GameSpeed / 64);
    }
    if (k == 0)
        e->supertime = SUPERTIMEMIN + randn(SUPERTIMERAN);
    e->curvesin = -kd->curve2 + (kd->curve ? randn(kd->curve) : 0);
    {
        int32_t s = e->curvesin;
        e->curvecos = (int16_t)isqrt32((32767L * 32767L) - s * s);
    }
    e->x = xc;
    e->y = yc;
    e->xbr = e->x + kd->width - 1;
    e->ybr = e->y + kd->height - 1;
    e->sx = (int32_t)e->x << 6;
    e->sy = (int32_t)e->y << 6;
    e->oldx = e->x;
    e->oldy = e->y;
    e->frame = 0;
    e->hit = kd->hits;
    r_save_under(enemy_frame_base[k], e->x, e->y, under_enemy[NumEnemies]);
}

static void DeleteEnemy(int i)
{
    r_restore_under(enemy_sprite(&enemy[i]), enemy[i].oldx, enemy[i].oldy,
                    under_enemy[i]);
    if (i != NumEnemies) {
        enemy[i] = enemy[NumEnemies];
        /* move the saved background too */
        {
            int n;
            for (n = 0; n < MAXSPR; n++)
                under_enemy[i][n] = under_enemy[NumEnemies][n];
        }
    }
    NumEnemies--;
}

static void AddEnemyMissile(int en)
{
    emissile_t *m;
    const enemykind_t *kd = &enemykind[enemy[en].ntyp];
    const emisskind_t *mk = &emisskind[kd->firetype];

    if (kd->firetype <= 0 || NumEnemyMissiles >= MAXEMISSILES)
        return;
    NumEnemyMissiles++;
    m = &emissiles[NumEnemyMissiles];
    m->sx = enemy[en].sx + ((int32_t)kd->width << 5);
    m->sy = enemy[en].sy + ((int32_t)kd->width << 5);
    m->x = (int16_t)(m->sx >> 6);
    m->y = (int16_t)(m->sy >> 6);
    m->oldx = m->x;
    m->oldy = m->y;
    m->kind = kd->firetype;
    m->xbr = m->x + gfx_sprites[GFX_EMISS1 + m->kind - 1].w - 1;
    m->ybr = m->y + gfx_sprites[GFX_EMISS1 + m->kind - 1].h - 1;
    if (mk->soundnum)
        snd_play(mk->soundnum);
    if (mk->firedirect) {
        int16_t dx = ship.x - m->x, dy = ship.y - m->y;
        int16_t t;
        if (iabs16(dx) > iabs16(dy))
            t = iabs16(dx) + iabs16(dy) / 2;
        else
            t = iabs16(dy) + iabs16(dx) / 2;
        if (t == 0)
            t = 1;
        m->delx = (int16_t)((int32_t)dx * mk->mspeed / t * GameSpeed / 64);
        m->dely = (int16_t)((int32_t)dy * mk->mspeed / t * GameSpeed / 64);
    } else {
        m->delx = (int16_t)((randn(mk->mspeed) - (mk->mspeed >> 1)) * GameSpeed / 64);
        m->dely = (int16_t)((randn(mk->mspeed) - (mk->mspeed >> 1)) * GameSpeed / 64);
    }
}

static void Explode(int en)
{
    int temp = MAXEMISSILES - NumEnemyMissiles;
    int i;
    const enemykind_t *kd = &enemykind[enemy[en].ntyp];
    const emisskind_t *mk = &emisskind[kd->firetype];

    if (kd->firetype <= 0)
        return;
    if (temp > 15)
        temp = 15;
    for (i = 1; i <= temp; i++) {
        emissile_t *m;
        NumEnemyMissiles++;
        m = &emissiles[NumEnemyMissiles];
        m->sx = enemy[en].sx + ((int32_t)kd->width << 5);
        m->sy = enemy[en].sy + ((int32_t)kd->width << 5);
        m->x = (int16_t)(m->sx >> 6);
        m->y = (int16_t)(m->sy >> 6);
        m->oldx = m->x;
        m->oldy = m->y;
        m->kind = kd->firetype;
        m->xbr = m->x + gfx_sprites[GFX_EMISS1 + m->kind - 1].w - 1;
        m->ybr = m->y + gfx_sprites[GFX_EMISS1 + m->kind - 1].h - 1;
        if (mk->soundnum)
            snd_play(mk->soundnum);
        m->delx = (int16_t)(((int32_t)cost[i] * mk->mspeed >> 15) * GameSpeed / 64);
        m->dely = (int16_t)(((int32_t)sint[i] * mk->mspeed >> 15) * GameSpeed / 64);
    }
}

static void EnemyDestroyed(int i, int16_t dx, int16_t dy, int explosion)
{
    const enemykind_t *kd = &enemykind[enemy[i].ntyp];

    if (pu_value[PU_HEAVY] > 0)
        enemy[i].hit = 0;
    else
        enemy[i].hit--;

    if (enemy[i].hit > 0) {
        snd_play(SND_DOH);
        return;
    }

    if (kd->shootback)
        AddEnemyMissile(i);
    if (kd->rebounds && pu_value[PU_HEAVY] <= 0) {
        snd_play(SND_OW);
        explosion = 0;
    }
    if (kd->explodes)
        Explode(i);
    AddToScore(kd->score);

    if (explosion) {
        int j, k;
        if (kd->deathsound == SND_EXPLOSN)
            snd_play(kd->deathsound + randn(3));
        else if (kd->deathsound)
            snd_play(kd->deathsound);
        if (kd->tribbles) {
            AddEnemy(enemy[i].x, enemy[i].y, enemy[i].ntyp + 1);
            AddEnemy(enemy[i].x, enemy[i].y, enemy[i].ntyp + 1);
            AddEnemy(enemy[i].x, enemy[i].y, enemy[i].ntyp + 1);
            AddEnemy(enemy[i].x, enemy[i].y, enemy[i].ntyp + 1);
            AddEnemy(enemy[i].x, enemy[i].y, enemy[i].ntyp + 1);
        }
        j = enemy[i].x;
        k = enemy[i].y;
        DeleteEnemy(i);
        AddEnemy(j, k, 1);      /* explosion animation */
    } else if (kd->rebounds && pu_value[PU_HEAVY] <= 0) {
        enemy[i].hit = kd->hits;
        enemy[i].delx = 3 * enemy[i].delx / 4 + dx / 4;
        enemy[i].dely = 3 * enemy[i].dely / 4 + dy / 4;
    } else
        DeleteEnemy(i);
}

static void FireSmartBomb(void)
{
    int i;
    if (G.numsmartbombs > 0) {
        for (i = NumEnemies; i >= 1; i--) {
            enemy[i].hit = 1;
            if (enemy[i].ntyp > 1)
                EnemyDestroyed(i, 0, 0, 1);
            else
                EnemyDestroyed(i, 0, 0, 0);
        }
        for (i = NumEnemyMissiles; i >= 1; i--) {
            /* just restoring page is handled by full redraw of removals */
        }
        NumEnemyMissiles = 0;
        G.numsmartbombs--;
        {
            const uint8_t *c = sbpal[10];
            hw_set_color(0, ((c[2] * 31 / 63) << 10) |
                            ((c[1] * 31 / 63) << 5) | (c[0] * 31 / 63));
        }
        SmartBombed = 11;
        snd_play(SND_EXPLOSN);
    }
    ShowSmartBombs();
}

static void AddEnemyMine(int en)
{
    if (!EminesAdding && NumEnemyMines < MAXEMINES) {
        const enemykind_t *kd = &enemykind[enemy[en].ntyp];
        eminepos_t *m;
        NumEnemyMines++;
        m = &emines[NumEnemyMines];
        m->x = enemy[en].x + (kd->width >> 1);
        m->y = enemy[en].y + (kd->width >> 1);
        m->xbr = m->x + gfx_sprites[GFX_EMINE].w - 1;
        m->ybr = m->y + gfx_sprites[GFX_EMINE].h - 1;
        r_put_sprite(GFX_EMINE, m->x, m->y);
        EminesAdding = 1;
        snd_play(SND_SQUELCH);
    }
}

static void MoveEnemies(void)
{
    const level_t *lv = &levels[G.level - 1];
    int i;

    for (i = 0; i <= MAXENEMYKINDS; i++) {
        if (NumEnemies < lv->maxenemies &&
            rand16() < ((uint32_t)lv->erelease * diffinfo[DiffLevel].freq64 >> 6) &&
            randn(100) < probs[G.level - 1][i]) {
            if (randn(2) == 1 && EnemyEnteringLeft <= 0) {
                EnemyEnteringLeft = 80;
                EnemyLeftType = i;
                r_put_sprite_solid(GFX_LGATE0 + 1, 0, PAGE_H / 2 - 10);
            } else if (EnemyEnteringRight <= 0) {
                EnemyEnteringRight = 80;
                EnemyRightType = i;
                r_put_sprite_solid(GFX_RGATE0 + 1, PAGE_W - 20, PAGE_H / 2 - 10);
            }
        }
    }

    if (EnemyEnteringLeft > 0) {
        EnemyEnteringLeft--;
        if ((EnemyEnteringLeft & 7) == 0)
            r_put_sprite_solid(GFX_LGATE0 + (EnemyEnteringLeft >> 3) % 5 + 1,
                               0, PAGE_H / 2 - 10);
        if (EnemyEnteringLeft == 0) {
            AddEnemy(15, ENEMYSTARTY, EnemyLeftType);
            r_put_sprite_solid(GFX_LGATE0, 0, PAGE_H / 2 - 10);
            snd_play(SND_ENEMYENT);
        }
    }
    if (EnemyEnteringRight > 0) {
        EnemyEnteringRight--;
        if ((EnemyEnteringRight & 7) == 0)
            r_put_sprite_solid(GFX_RGATE0 + (EnemyEnteringRight >> 3) % 5 + 1,
                               PAGE_W - 20, PAGE_H / 2 - 10);
        if (EnemyEnteringRight == 0) {
            AddEnemy(PAGE_W - enemykind[EnemyRightType].width - 16,
                     ENEMYSTARTY, EnemyRightType);
            r_put_sprite_solid(GFX_RGATE0, PAGE_W - 20, PAGE_H / 2 - 10);
            snd_play(SND_ENEMYENT);
        }
    }

    for (i = 1; i <= NumEnemies; i++) {
        enemy_t *e = &enemy[i];
        const enemykind_t *kd = &enemykind[e->ntyp];

        if (kd->fires && rand16() < kd->fireprob)
            AddEnemyMissile(i);
        if (kd->laysmines && rand16() < kd->fireprob)
            AddEnemyMine(i);

        e->frame += kd->framespeed;
        if (e->frame >= ((kd->numframes + 1) << 8))
            e->frame = 0;

        if (kd->follows && rand16() < kd->follow && !ShipDestroyed) {
            int16_t dx = ship.x - e->x, dy = ship.y - e->y;
            int16_t t = iabs16(dx) + iabs16(dy);
            if (t == 0)
                t = 1;
            e->delx = (int16_t)((int32_t)dx * kd->speed / t * GameSpeed / 64);
            e->dely = (int16_t)((int32_t)dy * kd->speed / t * GameSpeed / 64);
        } else if (rand16() < kd->changedir) {
            if (kd->zoom &&
                (iabs16(ship.delx) + iabs16(ship.dely)) < 60) {
                int16_t dx = ship.x - e->x, dy = ship.y - e->y;
                int16_t t = iabs16(dx) + iabs16(dy);
                if (t == 0)
                    t = 1;
                snd_play(SND_BARK);
                e->delx = (int16_t)((int32_t)dx * kd->speed / t * GameSpeed / 48);
                e->dely = (int16_t)((int32_t)dy * kd->speed / t * GameSpeed / 48);
            } else if (!kd->maxspeed) {
                e->delx = (int16_t)((randn(kd->speed) - kd->speed2) * GameSpeed / 64);
                e->dely = (int16_t)((randn(kd->speed) - kd->speed2) * GameSpeed / 64);
            } else {
                int idx = randn(24);
                int32_t c = dir_cos24(idx);
                e->delx = (int16_t)((kd->speed * c >> 14) * GameSpeed / 64);
                e->dely = (int16_t)((kd->speed * (16384 - (c * c >> 14)) >> 14) * GameSpeed / 64);
            }
        }

        if (kd->curves) {
            int32_t lt;
            int16_t tmp;
            if (rand16() < kd->changecurve) {
                e->curvesin = -kd->curve2 + (kd->curve ? randn(kd->curve) : 0);
                {
                    int32_t s = e->curvesin;
                    e->curvecos = (int16_t)isqrt32((32767L * 32767L) - s * s);
                }
            }
            tmp = e->delx;
            lt = (int32_t)e->delx * e->curvecos - (int32_t)e->dely * e->curvesin;
            e->delx = (int16_t)((lt > 0) ? (lt + 16384) / 32767
                                         : (lt - 16384) / 32767);
            lt = (int32_t)e->dely * e->curvecos + (int32_t)tmp * e->curvesin;
            e->dely = (int16_t)((lt > 0) ? (lt + 16384) / 32767
                                         : (lt - 16384) / 32767);
        }

        e->sx += e->delx;
        e->sy += e->dely;
        e->oldx = e->x;
        e->oldy = e->y;
        e->x = (int16_t)(e->sx >> 6);
        e->y = (int16_t)(e->sy >> 6);
        e->xbr = e->x + kd->width - 1;
        e->ybr = e->y + kd->height - 1;

        if (e->x < ENEMYXMIN) {
            e->x = ENEMYXMIN;
            e->delx = iabs16(e->delx);
        }
        if (e->xbr > ENEMYXMAX) {
            e->x = ENEMYXMAX - kd->width;
            e->delx = -iabs16(e->delx);
        }
        if (e->y < ENEMYYMIN) {
            e->y = ENEMYYMIN;
            e->dely = iabs16(e->dely);
        }
        if (e->ybr > ENEMYYMAX) {
            e->y = ENEMYYMAX - kd->height;
            e->dely = -iabs16(e->dely);
        }
        if (e->y < PAGE_H / 2 + 7 && e->ybr > PAGE_H / 2 - 7) {
            if (e->x < ENEMYXMIN + 7)
                e->delx = iabs16(e->delx);
            if (e->xbr > ENEMYXMAX - 7)
                e->delx = -iabs16(e->delx);
        }
        e->sx = (int32_t)e->x << 6 | (e->sx & 63);
        e->sy = (int32_t)e->y << 6 | (e->sy & 63);
        e->xbr = e->x + kd->width - 1;
        e->ybr = e->y + kd->height - 1;
    }

    for (i = NumEnemies; i >= 1; i--)
        if (enemy[i].supertime > 0) {
            enemy[i].supertime--;
            if (enemy[i].supertime <= 0)
                EnemyDestroyed(i, 0, 0, 0);
        }
}

/* ========================================================== missiles */
static void FireMissile(int16_t dx, int16_t dy)
{
    missile_t *m;
    const sprite_t *ms = &gfx_sprites[GFX_SHIPMISS];
    const sprite_t *ss = &gfx_sprites[GFX_SHIP0];

    if (ship.nummissiles >= MAXMISSILES)
        return;
    ship.nummissiles++;
    m = &missiles[ship.nummissiles];
    m->sx = (int16_t)(ship.sx + ((int32_t)ss->w << 5) - ((int32_t)ms->w << 4));
    m->sy = (int16_t)(ship.sy + ((int32_t)ss->h << 5) - ((int32_t)ms->h << 4));
    m->x = (int16_t)((uint16_t)m->sx >> 6);
    m->y = (int16_t)((uint16_t)m->sy >> 6);
    m->xbr = m->x + ms->w - 1;
    m->ybr = m->y + ms->h - 1;
    m->delx = dx;
    m->dely = dy;
    m->oldx = m->x;
    m->oldy = m->y;
    m->time = 0;
}

static void Shoot(int16_t dx, int16_t dy)
{
    if (ship.nummissiles < MAXMISSILES)
        snd_play(SND_FIRE);

    if (pu_value[PU_AIMED] > 0 && NumEnemies > 0) {
        int i, j = 1;
        int32_t mindist = 0x7FFFFFFF;
        for (i = 1; i <= NumEnemies; i++) {
            int32_t ddx = ship.x - enemy[i].x, ddy = ship.y - enemy[i].y;
            int32_t d = ddx * ddx + ddy * ddy;
            if (d < mindist) {
                mindist = d;
                j = i;
            }
        }
        mindist = isqrt32(mindist);
        if (mindist == 0)
            mindist = 1;
        {
            int32_t framedelta = mindist / (256 / 64);
            int32_t exp_ = enemy[j].x + framedelta * enemy[j].delx / 64;
            int32_t eyp = enemy[j].y + framedelta * enemy[j].dely / 64;
            dx = (int16_t)(256 * (exp_ - ship.x) / mindist);
            dy = (int16_t)(256 * (eyp - ship.y) / mindist);
        }
    }
    FireMissile(dx, dy);
    if (pu_value[PU_ASS] > 0)
        FireMissile(-dx, -dy);
    if (pu_value[PU_MULTI] > 0) {
        /* rotate (dx,dy) by +-10 degrees; cos10=16126/16384 sin10=2845/16384 */
        int32_t c = 16126, s = 2845;
        int16_t ax = (int16_t)((dx * c - dy * s) >> 14);
        int16_t ay = (int16_t)((dy * c + dx * s) >> 14);
        int16_t bx = (int16_t)((dx * c + dy * s) >> 14);
        int16_t by = (int16_t)((dy * c - dx * s) >> 14);
        FireMissile(ax, ay);
        FireMissile(bx, by);
        if (pu_value[PU_ASS] > 0) {
            FireMissile(-ax, -ay);
            FireMissile(-bx, -by);
        }
    }
}

static void UpdateMissiles(void)
{
    int i;
    const sprite_t *ms = &gfx_sprites[GFX_SHIPMISS];

    for (i = 1; i <= ship.nummissiles; i++) {
        missile_t *m = &missiles[i];
        m->oldx = m->x;
        m->oldy = m->y;
        m->sx = (int16_t)(m->sx + m->delx);
        m->x = (int16_t)((uint16_t)m->sx >> 6);
        m->xbr = m->x + ms->w;
        m->sy = (int16_t)(m->sy + m->dely);
        m->y = (int16_t)((uint16_t)m->sy >> 6);
        m->ybr = m->y + ms->h;
        m->time++;
    }

    for (i = ship.nummissiles; i >= 1; i--) {
        missile_t *m = &missiles[i];
        int del = 0;
        if (m->x < 10) {
            if (pu_value[PU_BOUNCE] > 0) {
                m->x = 11;
                m->sx = m->x << 6;
                m->delx = -m->delx;
            } else
                del = 1;
        } else if (m->xbr > PAGE_W - 10) {
            if (pu_value[PU_BOUNCE] > 0) {
                m->x = PAGE_W - 11 - ms->w;
                m->sx = m->x << 6;
                m->delx = -m->delx;
            } else
                del = 1;
        }
        if (!del) {
            if (m->y < 10) {
                if (pu_value[PU_BOUNCE] > 0) {
                    m->y = 11;
                    m->sy = m->y << 6;
                    m->dely = -m->dely;
                } else
                    del = 1;
            } else if (m->ybr > PAGE_H - 10) {
                if (pu_value[PU_BOUNCE] > 0) {
                    m->y = PAGE_H - 11 - ms->h;
                    m->sy = m->y << 6;
                    m->dely = -m->dely;
                } else
                    del = 1;
            }
        }
        if (m->time > MISSILELIFE)
            del = 1;
        if (del) {
            missiles[i] = missiles[ship.nummissiles];
            ship.nummissiles--;
        }
    }

    for (i = 1; i <= NumEnemyMissiles; i++) {
        emissile_t *m = &emissiles[i];
        const sprite_t *es = &gfx_sprites[GFX_EMISS1 + m->kind - 1];
        m->oldx = m->x;
        m->oldy = m->y;
        m->sx += m->delx;
        m->x = (int16_t)(m->sx >> 6);
        m->xbr = m->x + es->w;
        m->sy += m->dely;
        m->y = (int16_t)(m->sy >> 6);
        m->ybr = m->y + es->h;
    }

    for (i = NumEnemyMissiles; i >= 1; i--) {
        emissile_t *m = &emissiles[i];
        int obx = (m->x < 10 || m->xbr > PAGE_W - 10);
        int oby = (m->y < 10 || m->ybr > PAGE_H - 10);
        if (obx || oby) {
            if (emisskind[m->kind].rebound) {
                if (obx)
                    m->delx = -m->delx;
                else
                    m->dely = -m->dely;
                if (m->x < 10) { m->x = 10; m->sx = (int32_t)m->x << 6; }
                if (m->xbr > PAGE_W - 10) {
                    m->x = PAGE_W - 10 - gfx_sprites[GFX_EMISS1 + m->kind - 1].w;
                    m->sx = (int32_t)m->x << 6;
                }
                if (m->y < 10) { m->y = 10; m->sy = (int32_t)m->y << 6; }
                if (m->ybr > PAGE_H - 10) {
                    m->y = PAGE_H - 10 - gfx_sprites[GFX_EMISS1 + m->kind - 1].h;
                    m->sy = (int32_t)m->y << 6;
                }
                snd_play(SND_BOING);
            } else {
                emissiles[i] = emissiles[NumEnemyMissiles];
                NumEnemyMissiles--;
            }
        }
    }
}

/* ======================================================= object hits */
static void ObjectHit(int i)
{
    object_t *o = &objects[i];

    if (o->typ == OBJ_CRYS) {
        AddToScore(200);
        snd_play(SND_GETCRYSTAL);
        G.numcrystals--;
        ShowCrystals();
        if (G.numcrystals == 0) {   /* open the gate */
            r_fill(GateLX + gate_width(), 0, GateRX, 11, 0);
            snd_play(SND_GATESOUND);
        }
    } else if (o->typ == OBJ_MINE) {
        ShipDestroyed = 1;
        AddEnemy(o->x, o->y, 1);
        snd_play(SND_EXPLOSN);
    } else {
        G.numsmartbombs++;
        ShowSmartBombs();
        snd_play(SND_ALLRIGHT);
        G.numsmarts--;
    }
    r_restore_under(GFX_OBJ0, o->x, o->y, under_obj[i]);
    o->deleted = 1;
}

/* ======================================================== collisions */
static void CheckCollisions(void)
{
    int i, j;
    int shipspr = GFX_SHIP0 + ship.dir;

    if (!ShipDestroyed) {
        for (i = NumEnemies; i >= 1; i--) {
            enemy_t *e = &enemy[i];
            if (e->ntyp == 1)
                continue;       /* explosions don't collide */
            if (e->xbr >= ship.x && e->ybr >= ship.y &&
                e->x <= ship.xbr && e->y <= ship.ybr &&
                sprites_collide(shipspr, ship.x, ship.y,
                                enemy_sprite(e), e->x, e->y)) {
                if (e->ntyp > 1 && !ShipDestroyed) {
                    ShipDestroyed = 1;
                    EnemyDestroyed(i, ship.delx, ship.dely, 1);
                } else if (e->ntyp == 0) {   /* supercrystal */
                    int given = 0;
                    while (!given) {
                        switch (randn(22)) {
                        case 0: case 1: case 2:
                            if (pu_value[PU_RAPID] <= 0) {
                                pu_value[PU_RAPID] = randn(pu_timeran[PU_RAPID]) + pu_timemin[PU_RAPID];
                                ShowPowerUps();
                                given = 1;
                            }
                            break;
                        case 3: case 4: case 5:
                            if (pu_value[PU_MULTI] <= 0) {
                                pu_value[PU_MULTI] = randn(pu_timeran[PU_MULTI]) + pu_timemin[PU_MULTI];
                                ShowPowerUps();
                                given = 1;
                            }
                            break;
                        case 6: case 7: case 8:
                            if (pu_value[PU_HEAVY] <= 0) {
                                pu_value[PU_HEAVY] = randn(pu_timeran[PU_HEAVY]) + pu_timemin[PU_HEAVY];
                                ShowPowerUps();
                                given = 1;
                            }
                            break;
                        case 9: case 10: case 11:
                            if (pu_value[PU_ASS] <= 0) {
                                pu_value[PU_ASS] = randn(pu_timeran[PU_ASS]) + pu_timemin[PU_ASS];
                                ShowPowerUps();
                                given = 1;
                            }
                            break;
                        case 12: case 13:
                            if (pu_value[PU_AIMED] <= 0) {
                                pu_value[PU_AIMED] = randn(pu_timeran[PU_AIMED]) + pu_timemin[PU_AIMED];
                                ShowPowerUps();
                                given = 1;
                            }
                            break;
                        case 14: case 15:
                            if (pu_value[PU_BOUNCE] <= 0) {
                                pu_value[PU_BOUNCE] = randn(pu_timeran[PU_BOUNCE]) + pu_timemin[PU_BOUNCE];
                                ShowPowerUps();
                                given = 1;
                            }
                            break;
                        case 16: case 17:
                            if (!NoMines) {
                                if (pu_value[PU_SHIELD] == 0)
                                    pu_value[PU_SHIELD] = 1;
                                for (j = 1; j <= G.numobjects; j++)
                                    if (objects[j].typ == OBJ_MINE && !objects[j].deleted)
                                        ObjectHit(j);
                                NoMines = 1;
                                SmartBombed = 11;
                                given = 1;
                            }
                            break;
                        case 18:
                            pu_value[PU_SHIELD] += randn(pu_timeran[PU_SHIELD]) + pu_timemin[PU_SHIELD];
                            ShowPowerUps();
                            given = 1;
                            break;
                        default:
                            if (GateMoving) {
                                GateMoving = 0;
                                r_put_sprite(GFX_GATEL, GateLX, 0);
                                r_put_sprite(GFX_GATER, GateRX, 0);
                                given = 1;
                            }
                            break;
                        }
                    }
                    EnemyDestroyed(i, 0, 0, 0);
                    snd_play(SND_OHYEAH);
                }
            }
        }

        for (i = 1; i <= NumEnemyMines; i++) {
            eminepos_t *m = &emines[i];
            if (m->xbr >= ship.x && m->ybr >= ship.y &&
                m->x <= ship.xbr && m->y <= ship.ybr &&
                sprites_collide(shipspr, ship.x, ship.y, GFX_EMINE, m->x, m->y))
                ShipDestroyed = 1;
        }

        for (i = G.numobjects; i >= 1; i--) {
            object_t *o = &objects[i];
            if (!o->deleted &&
                o->xbr >= ship.x && o->ybr >= ship.y &&
                o->x <= ship.xbr && o->y <= ship.ybr &&
                sprites_collide(shipspr, ship.x, ship.y,
                                GFX_OBJ0 + o->typ, o->x, o->y))
                ObjectHit(i);
        }
    }

    for (i = 1; i <= ship.nummissiles; i++)
        for (j = NumEnemies; j >= 1; j--) {
            enemy_t *e = &enemy[j];
            if (e->ntyp == 1)
                continue;
            if (missiles[i].xbr > e->x && missiles[i].x < e->xbr &&
                missiles[i].ybr > e->y && missiles[i].y < e->ybr &&
                sprites_collide(GFX_SHIPMISS, missiles[i].x, missiles[i].y,
                                enemy_sprite(e), e->x, e->y)) {
                EnemyDestroyed(j, missiles[i].delx, missiles[i].dely, 1);
                missiles[i].time = 10000;
            }
        }

    for (i = NumEnemyMissiles; i >= 1; i--) {
        emissile_t *m = &emissiles[i];
        if (!ShipDestroyed &&
            m->xbr >= ship.x && m->ybr >= ship.y &&
            m->x <= ship.xbr && m->y <= ship.ybr &&
            sprites_collide(shipspr, ship.x, ship.y,
                            GFX_EMISS1 + m->kind - 1, m->x, m->y)) {
            ShipDestroyed = 1;
            emissiles[i] = emissiles[NumEnemyMissiles];
            NumEnemyMissiles--;
        }
    }
}

/* =========================================================== the ship */
static void MoveGate(void)
{
    int move = GateMoveCount / 64;
    GateMoveCount = GateMoveCount % 64;
    if (rand16() < levels[G.level - 1].gatechangedirprob)
        G.gatemovepos = !G.gatemovepos;
    if (move > 0) {
        /* erase old gates + barrier region */
        r_fill(MINGATEX - 10, 0, MAXGATEX + gate_width() + 10,
               gate_height() - 1, 0);
        if (G.gatemovepos) {
            GateLX += move;
            GateRX += move;
            if (GateRX > MAXGATEX)
                G.gatemovepos = 0;
        } else {
            GateLX -= move;
            GateRX -= move;
            if (GateLX < MINGATEX)
                G.gatemovepos = 1;
        }
        r_put_sprite(GFX_GATEL, GateLX, 0);
        r_put_sprite(GFX_GATER, GateRX, 0);
        if (G.numcrystals > 0)
            r_hline(GateLX + gate_width(), GateRX - 1, 7, 54);
        /* redraw top border pipes */
        {
            int k;
            for (k = 1; k <= 5; k++) {
                r_hline(10, GateLX, 10 - k, bcolor[k - 1]);
                r_hline(GateRX + gate_width(), PAGE_W - 10, 10 - k, bcolor[k - 1]);
            }
        }
    }
}

static void EnemyRepel(int i)
{
    int32_t dx = ship.x - enemy[i].x;
    int32_t dy = ship.y - enemy[i].y;
    int32_t modulus;

    if (dx == 0 && dy == 0)
        return;
    if (iabs32(dx) > iabs32(dy))
        modulus = (dx * dx + dy * dy) * (iabs32(dx) + iabs32(dy) / 2);
    else
        modulus = (dx * dx + dy * dy) * (iabs32(dy) + iabs32(dx) / 2);
    if (modulus > 0) {
        if (modulus < 500000 && (FrameCount & 31) == 0)
            snd_play(SND_REPULSE);
        ship.delx += (int16_t)(8192 * dx / modulus);
        ship.dely += (int16_t)(8192 * dy / modulus);
    }
}

static void DoAttractor(void)
{
    int32_t dx = ship.x - G.attractorx;
    int32_t dy = ship.y - G.attractory;
    int32_t modulus;

    if (dx == 0 && dy == 0)
        return;
    if (iabs32(dx) > iabs32(dy))
        modulus = (dx * dx + dy * dy) * (iabs32(dx) + iabs32(dy) / 2);
    else
        modulus = (dx * dx + dy * dy) * (iabs32(dy) + iabs32(dx) / 2);
    if (modulus > 0) {
        ship.delx -= (int16_t)(8192 * dx / modulus);
        ship.dely -= (int16_t)(8192 * dy / modulus);
    }
}

static void ship_wall_check(void)
{
    int16_t t;
    int gw = gate_width(), gh = gate_height();
    const sprite_t *ss = &gfx_sprites[GFX_SHIP0];
    int hard = (pu_value[PU_SHIELD] <= 0 && DiffLevel > 1);

    if (ship.xbr >= SHIPMAXX) {
        ship.x = SHIPMAXX - ss->w;
        if (hard)
            ShipDestroyed = 1;
        else
            ship.delx = -iabs16(ship.delx);
    }
    if (ship.x < SHIPMINX) {
        ship.x = SHIPMINX;
        if (hard)
            ShipDestroyed = 1;
        else
            ship.delx = iabs16(ship.delx);
    }
    if (ship.y < PAGE_H / 2 + 7 && ship.ybr > PAGE_H / 2 - 7) {
        if (ship.x < SHIPMINX + 6) {
            ship.x = SHIPMINX + 7;
            if (hard)
                ShipDestroyed = 1;
            else
                ship.delx = iabs16(ship.delx);
        }
        if (ship.xbr > SHIPMAXX - 6) {
            ship.x = SHIPMAXX - 7 - ss->w;
            if (hard)
                ShipDestroyed = 1;
            else
                ship.delx = -iabs16(ship.delx);
        }
    }

    /* horizontal scrolling */
    t = ship.x - (VisX + VIEW_W - SCREENHBORDER);
    if (t > 0 && VisX < MAXVISX) {
        VisX += t / 20 + 1;
        if (VisX > MAXVISX)
            VisX = MAXVISX;
    }
    t = (VisX + SCREENHBORDER) - ship.x;
    if (t > 0 && VisX > 0) {
        VisX -= t / 20 + 1;
        if (VisX < 0)
            VisX = 0;
    }

    if (ship.ybr > SHIPMAXY) {
        ship.y = SHIPMAXY - ss->h;
        if (hard)
            ShipDestroyed = 1;
        else
            ship.dely = -iabs16(ship.dely);
    }

    /* gates */
    if (ship.y < gh) {
        if ((GateLX + gw - 1 >= ship.x && GateLX <= ship.xbr &&
             sprites_collide(GFX_SHIP0 + ship.dir, ship.x, ship.y,
                             GFX_GATEL, GateLX, 0)) ||
            (GateRX + gw - 1 >= ship.x && GateRX <= ship.xbr &&
             sprites_collide(GFX_SHIP0 + ship.dir, ship.x, ship.y,
                             GFX_GATER, GateRX, 0))) {
            ship.y = gh;
            if (pu_value[PU_SHIELD] <= 0)
                ShipDestroyed = 1;
            else
                ship.dely = iabs16(ship.dely);
        }
    }

    if (ship.y < SHIPMINY) {
        if (ship.x <= GateLX || ship.xbr >= GateRX + gw) {
            ship.y = SHIPMINY;
            if (hard)
                ShipDestroyed = 1;
            else
                ship.dely = iabs16(ship.dely);
        } else {
            if (G.numcrystals > 0 && ship.y < 8) {
                ship.y = SHIPMINY;
                if (pu_value[PU_SHIELD] <= 0)
                    ShipDestroyed = 1;
                else
                    ship.dely = iabs16(ship.dely);
            }
            if (ship.y < 5 && !ShipDestroyed)
                LevelFinished = 1;
        }
    }

    /* vertical scrolling */
    t = ship.y - (VisY + VIEW_H - SCREENVBORDER);
    if (t > 0 && VisY < MAXVISY) {
        VisY += t / 20 + 1;
        if (VisY > MAXVISY)
            VisY = MAXVISY;
    }
    t = (VisY + SCREENVBORDER) - ship.y;
    if (t > 0 && VisY > 0) {
        VisY -= t / 20 + 1;
        if (VisY < 0)
            VisY = 0;
    }

    ship.sx = (int32_t)ship.x << 6 | (ship.sx & 63);
    ship.sy = (int32_t)ship.y << 6 | (ship.sy & 63);
    ship.xbr = ship.x + ss->w - 1;
    ship.ybr = ship.y + ss->h - 1;
}

static void DecrementPowerUps(void)
{
    int pu;
    for (pu = 0; pu < PU_COUNT; pu++)
        if (pu_value[pu] > 0) {
            pu_value[pu]--;
            if (pu_value[pu] < 198 && pu_position[pu] != 0) {
                if (pu_value[pu] % 22 == 0)
                    r_status_fill(pu_position[pu], 5, pu_position[pu] + 17, 20, 14);
                else if (pu_value[pu] % 22 == 11)
                    r_status_sprite(GFX_POWERUP0 + pu, pu_position[pu], 5);
            }
            if (pu_value[pu] == 0)
                ShowPowerUps();
        }
}

static void MoveShip(void)
{
    int i;
    int16_t t;
    const sprite_t *ss = &gfx_sprites[GFX_SHIP0];

    ship.delx += in_dx;
    ship.dely += in_dy;

    t = iabs16(ship.delx) + iabs16(ship.dely);
    if (t > MAXSHIPSPEED) {
        ship.delx = (int16_t)((int32_t)ship.delx * MAXSHIPSPEED / t);
        ship.dely = (int16_t)((int32_t)ship.dely * MAXSHIPSPEED / t);
    }

    for (i = 1; i <= NumEnemies; i++)
        if (enemykind[enemy[i].ntyp].repulses)
            EnemyRepel(i);
    if (G.gameclocked > 0)
        DoAttractor();

    ship.sx += ship.delx;
    ship.sy += ship.dely;
    ship.oldx = ship.x;
    ship.oldy = ship.y;
    ship.x = (int16_t)(ship.sx >> 6);
    ship.y = (int16_t)(ship.sy >> 6);
    ship.xbr = ship.x + ss->w - 1;
    ship.ybr = ship.y + ss->h - 1;

    /* direction from velocity: atan2 via 24-sector lookup.
     * Matches the original: theta = arctan(delx/dely), theta 0 = DOWN,
     * so sprite 0 faces down, 6 = right, 12 = up, 18 = left. */
    if (ship.delx != 0 || ship.dely != 0) {
        int16_t dx = ship.delx, dy = ship.dely;
        int best = 0, d;
        int32_t bestdot = -0x7FFFFFFF;
        for (d = 0; d < 24; d++) {
            /* facing vector of sprite d: (sin, +cos) in screen coords
             * (y grows downward), so d=0 -> (0,1) = down */
            int32_t vx = dir_sin24[d], vy = dir_cos24(d);
            int32_t dot = vx * dx + vy * dy;
            /* all vectors unit length; largest dot = closest direction */
            if (dot > bestdot) {
                bestdot = dot;
                best = d;
            }
        }
        ship.dir = best;
    }

    ship_wall_check();

    DecrementPowerUps();

    if (in_fire || (pu_value[PU_RAPID] > 0 && (FrameCount & 3) == 0 &&
                    in_fire_held)) {
        /* Muzzle velocity in the ship's facing direction plus the
         * ship's own momentum. The DOS original used 2x the ship
         * velocity (mouse flicks made that fast); with capped D-pad
         * speeds that left standing shots nearly static. */
        int16_t mvx = (int16_t)((MISSILE_MUZZLE * (int32_t)dir_sin24[ship.dir]) >> 14);
        int16_t mvy = (int16_t)((MISSILE_MUZZLE * (int32_t)dir_cos24(ship.dir)) >> 14);
        Shoot((int16_t)(mvx + ship.delx), (int16_t)(mvy + ship.dely));
    }
    if (in_smart)
        FireSmartBomb();
}

/* ============================================================ drawing */
static uint8_t under_miss[MAXMISSILES + 1][8 * 8];
static uint8_t under_emiss[MAXEMISSILES + 1][12 * 12];
static uint8_t under_timer[10 * 8 * 9];
static int16_t timer_x = -1, timer_y = -1, timer_w;

static void erase_timer(void)
{
    int yy, xx;
    if (timer_x < 0)
        return;
    for (yy = 0; yy < gfx_sprites[GFX_DIGIT0].h; yy++)
        for (xx = 0; xx < timer_w; xx++) {
            int px_ = timer_x + xx, py_ = timer_y + yy;
            if ((unsigned)px_ < PAGE_W && (unsigned)py_ < PAGE_H)
                page[py_][px_] = under_timer[yy * timer_w + xx];
        }
    timer_x = -1;
}

static void draw_timer(void)
{
    /* countdown timer in top-right corner of the view */
    const level_t *lv = &levels[G.level - 1];
    int32_t remain = (int32_t)lv->time - G.timeonlevel / FRAMERATE;
    char s[8];
    int n = 0, i, w;
    int x = TIMERX + VisX, y = TIMERY + VisY;
    int dh = gfx_sprites[GFX_DIGIT0].h;

    if (remain < 0)
        remain = 0;
    numstr(remain, s, 0);
    while (s[n])
        n++;
    w = 0;
    for (i = 0; i < n; i++)
        w += gfx_sprites[GFX_DIGIT0 + (s[i] - '0')].w + 1;
    if (w > timer_w)
        timer_w = w;
    if (timer_w > 10 * 8)
        timer_w = 10 * 8;

    /* save under */
    {
        int yy, xx;
        for (yy = 0; yy < dh; yy++)
            for (xx = 0; xx < timer_w; xx++) {
                int px_ = x + xx, py_ = y + yy;
                under_timer[yy * timer_w + xx] =
                    ((unsigned)px_ < PAGE_W && (unsigned)py_ < PAGE_H) ?
                    page[py_][px_] : 0;
            }
        timer_x = x;
        timer_y = y;
    }
    for (i = 0; i < n; i++) {
        int spr = GFX_DIGIT0 + (s[i] - '0');
        r_put_sprite(spr, x, y);
        x += gfx_sprites[spr].w + 1;
    }
}

static uint8_t ship_drawn;

static void EraseSprites(void)
{
    int i;

    erase_timer();
    /* erase in reverse draw order */
    for (i = NumEnemyMissiles; i >= 1; i--)
        r_restore_under(GFX_EMISS1 + emissiles[i].kind - 1,
                        emissiles[i].oldx, emissiles[i].oldy, under_emiss[i]);
    for (i = ship.nummissiles; i >= 1; i--)
        r_restore_under(GFX_SHIPMISS, missiles[i].oldx, missiles[i].oldy,
                        under_miss[i]);
    for (i = NumEnemies; i >= 1; i--)
        r_restore_under(enemy_sprite(&enemy[i]), enemy[i].oldx, enemy[i].oldy,
                        under_enemy[i]);
    if (ship_drawn) {
        r_restore_under(GFX_SHIP0, ship.oldx, ship.oldy, under_ship);
        ship_drawn = 0;
    }
    EminesAdding = 0;
}

static void DrawSprites(void)
{
    int i;

    if (!ShipDestroyed) {
        r_save_under(GFX_SHIP0, ship.x, ship.y, under_ship);
        r_put_sprite(GFX_SHIP0 + ship.dir, ship.x, ship.y);
        ship.oldx = ship.x;
        ship.oldy = ship.y;
        ship_drawn = 1;
    }

    for (i = 1; i <= NumEnemies; i++) {
        enemy_t *e = &enemy[i];
        e->oldx = e->x;
        e->oldy = e->y;
        r_save_under(enemy_sprite(e), e->x, e->y, under_enemy[i]);
        r_put_sprite(enemy_sprite(e), e->x, e->y);
    }
    for (i = 1; i <= ship.nummissiles; i++) {
        r_save_under(GFX_SHIPMISS, missiles[i].x, missiles[i].y, under_miss[i]);
        r_put_sprite(GFX_SHIPMISS, missiles[i].x, missiles[i].y);
        missiles[i].oldx = missiles[i].x;
        missiles[i].oldy = missiles[i].y;
    }
    for (i = 1; i <= NumEnemyMissiles; i++) {
        r_save_under(GFX_EMISS1 + emissiles[i].kind - 1,
                     emissiles[i].x, emissiles[i].y, under_emiss[i]);
        r_put_sprite(GFX_EMISS1 + emissiles[i].kind - 1,
                     emissiles[i].x, emissiles[i].y);
        emissiles[i].oldx = emissiles[i].x;
        emissiles[i].oldy = emissiles[i].y;
    }
    draw_timer();
}

/* =========================================================== messages */
static void TextWindowMsg(const char *msg)
{
    int cx = VisX + VIEW_W / 2, cy = VisY + VIEW_H / 2;
    int w = 0, n = 0;
    while (msg[n])
        n++;
    w = n * 8 + 40;
    r_window(cx - w / 2, cy - 20, cx + w / 2, cy + 20);
    r_text_center(0, cx, cy - 7, msg);
    r_present(VisX, VisY);
}

/* =========================================================== levels */
static void GiveBonus(void)
{
    int32_t timetaken = G.timeonlevel / FRAMERATE;
    int32_t bonus = ((int32_t)levels[G.level - 1].time - timetaken) * 500;
    char s[24], line[40];
    int wx = VisX + 50, wy = VisY + 40;
    int i;

    if (bonus < 0)
        bonus = 0;

    r_window(wx, wy, wx + 220, wy + 120);
    numstr(G.totallevel, s, 0);
    {
        /* "LEVEL n COMPLETED" */
        int p = 0;
        const char *a = "LEVEL ";
        const char *b = " COMPLETED";
        for (i = 0; a[i]; i++) line[p++] = a[i];
        for (i = 0; s[i]; i++) line[p++] = s[i];
        for (i = 0; b[i]; i++) line[p++] = b[i];
        line[p] = 0;
    }
    r_text(0, wx + 42, wy + 15, line);
    numstr(timetaken, s, 3);
    {
        int p = 0;
        const char *a = "TIME TAKEN: ";
        const char *b = " SECONDS";
        for (i = 0; a[i]; i++) line[p++] = a[i];
        for (i = 0; s[i]; i++) line[p++] = s[i];
        for (i = 0; b[i]; i++) line[p++] = b[i];
        line[p] = 0;
    }
    r_text(0, wx + 20, wy + 40, line);
    numstr(levels[G.level - 1].time, s, 3);
    {
        int p = 0;
        const char *a = "PAR       : ";
        const char *b = " SECONDS";
        for (i = 0; a[i]; i++) line[p++] = a[i];
        for (i = 0; s[i]; i++) line[p++] = s[i];
        for (i = 0; b[i]; i++) line[p++] = b[i];
        line[p] = 0;
    }
    r_text(0, wx + 20, wy + 65, line);
    numstr(bonus, s, 0);
    {
        int p = 0;
        const char *a = "BONUS     : ";
        for (i = 0; a[i]; i++) line[p++] = a[i];
        for (i = 0; s[i]; i++) line[p++] = s[i];
        line[p] = 0;
    }
    r_text(0, wx + 20, wy + 90, line);
    r_present(VisX, VisY);
    AddToScore(bonus);
    if (bonus > 0)
        snd_play(SND_COUNTDOWN);
    delay_or_event(FRAMERATE * 5 / 2);
}

static void ShowGameClockedMessage(void)
{
    static const char *const names[5] = {
        "XQUEST WARRIOR", "XQUEST WARRIOR SUPREME",
        "XQUEST COMMANDER", "XQUEST WARLORD", "XQUEST GOD" };
    int wx = VisX + 50, wy = VisY + 40;
    r_window(wx, wy, wx + 220, wy + 120);
    r_text_center(0, VisX + 160, wy + 15, "CONGRATULATIONS");
    r_text_center(0, VisX + 160, wy + 40, "YOU HAVE ATTAINED THE");
    r_text_center(0, VisX + 160, wy + 65, "RANK OF");
    r_text_center(0, VisX + 160, wy + 90, names[G.gameclocked - 1]);
    r_present(VisX, VisY);
    wait_any_button();
}

static void LevelOver(void)
{
    snd_play(SND_PHEW);
    GiveBonus();
    G.level++;
    G.totallevel++;
    if (G.level > MAXLEVEL) {
        if (G.gameclocked < 5)
            G.gameclocked++;
        ShowGameClockedMessage();
        G.level = 1;
    }
    SetupNewLevel();
    StartNewLevel();
}

static void NewLife(void)
{
    int pu;

    G.lives--;
    GameSpeed = (BASEGAMESPEED * diffinfo[DiffLevel].speed64 >> 6);
    if (G.lives == 0) {
        GameOver = 1;
        return;
    }
    for (pu = 0; pu < PU_COUNT; pu++) {
        pu_value[pu] = 0;
        pu_position[pu] = 0;
    }
    ShipDestroyedCount = START_SHIPDESTROYED;
    StartNewLevel();
    TextWindowMsg("READY");
    wait_any_button();
    StartNewLevel();    /* redraw playfield without the window */
    r_present(VisX, VisY);
}

/* ======================================================== game proper */
static void InitialiseVariables(void)
{
    int i, pu;

    G.timeonlevel = 0;
    G.level = 1;
    G.totallevel = 1;
    G.score = 0;
    G.lastnewmanscore = 0;
    G.numsmartbombs = STARTBOMBS;
    G.lives = STARTLIVES;
    G.gameclocked = 0;
    GameOver = 0;
    SmartBombed = 0;
    for (pu = 0; pu < PU_COUNT; pu++) {
        pu_value[pu] = 0;
        pu_position[pu] = 0;
    }
    ShipDestroyedCount = START_SHIPDESTROYED;
    EnemyEnteringLeft = 0;
    EnemyEnteringRight = 0;
    /* cos/sin(i*168 degrees) << 15 for cluster explosions */
    {
        static const int16_t ct[16] = {
            0, -32051, 29934, -26509, 21925, -16384, 10126, -3425, -3425,
            10126, -16383, 21925, -26509, 29934, -32051, 32767 };
        static const int16_t st[16] = {
            0, 6813, -13328, 19260, -24351, 28377, -31163, 32587, -32587,
            31163, -28377, 24351, -19260, 13328, -6813, 0 };
        for (i = 1; i <= 15; i++) {
            cost[i] = ct[i];
            sint[i] = st[i];
        }
    }
}

static void game_run(void)
{
    InitialiseVariables();
    hw_set_palette(game_palette);
    SetupNewLevel();
    StartNewLevel();
    r_present(VisX, VisY);
    TextWindowMsg("READY");
    wait_any_button();
    StartNewLevel();

    do {
        FrameCount = 0;
        do {
            poll_input();
            EraseSprites();
            MoveEnemies();
            UpdateMissiles();
            CheckCollisions();

            if (ShipDestroyed) {
                if (pu_value[PU_SHIELD] > 0)
                    ShipDestroyed = 0;
                else {
                    ship.oldx = ship.x;
                    ship.oldy = ship.y;
                    if (ShipDestroyedCount == START_SHIPDESTROYED - 1) {
                        snd_play(SND_EXPLOSN);
                        AddEnemy(ship.x, ship.y, 1);
                    }
                    ShipDestroyedCount--;
                }
            }
            if (!ShipDestroyed)
                MoveShip();

            if (SmartBombed > 1 && (FrameCount & 1) == 0) {
                SmartBombed--;
                {
                    const uint8_t *c = sbpal[SmartBombed - 1];
                    hw_set_color(0, ((c[2] * 31 / 63) << 10) |
                                    ((c[1] * 31 / 63) << 5) | (c[0] * 31 / 63));
                }
            }

            DrawSprites();

            if (GateMoving) {
                GateMoveCount += levels[G.level - 1].gatemove;
                if (GateMoveCount > 63)
                    MoveGate();
            }

            r_present(VisX, VisY);
            FrameCount++;
            G.timeonlevel++;

            if (in_start) {
                in_start = 0;
                TextWindowMsg("PAUSE");
                wait_any_button();
                /* redraw over window: restore from page is complex; simply
                 * clear the region by re-rendering the next frames */
                {
                    int cx = VisX + VIEW_W / 2, cy = VisY + VIEW_H / 2;
                    r_fill(cx - 80, cy - 20, cx + 80, cy + 20, 0);
                }
            }

            if (G.timeonlevel > (int32_t)levels[G.level - 1].time * FRAMERATE) {
                GameSpeed = (BASEGAMESPEED * diffinfo[DiffLevel].speed64 >> 6) +
                    (int16_t)((G.timeonlevel -
                               (int32_t)levels[G.level - 1].time * FRAMERATE) *
                              BASEGAMESPEED / ((int32_t)FRAMERATE * 32));
            }
        } while (!LevelFinished && ShipDestroyedCount != 0 && !GameOver);

        NumEnemies = 0;
        if (LevelFinished)
            LevelOver();
        else if (ShipDestroyed)
            NewLife();
    } while (!GameOver);

    /* game over screen */
    TextWindowMsg("GAME OVER");
    delay_or_event(FRAMERATE * 3);
}

/* ======================================================= title / menu */
static void title_screen(void)
{
    int y, x;

    /* title picture is 320x240; show the top 224 lines */
    for (y = 0; y < PAGE_H; y++)
        for (x = 0; x < PAGE_W; x++)
            page[y][x] = 0;
    for (y = 0; y < SCREEN_H; y++)
        for (x = 0; x < 320; x++)
            page[y][x] = title_px[(y + 8) * 320 + x];
    hw_set_palette(title_palette);
    r_present_full(0, 0);
    snd_play(SND_GATESOUND);
    delay_or_event(FRAMERATE * 3);
}

static int menu_screen(void)
{
    /* returns selected difficulty 0..4; START begins the game */
    int sel = DiffLevel;
    int redraw = 1;

    hw_set_palette(game_palette);
    for (;;) {
        if (redraw) {
            int i;
            r_clear_page(0);
            for (i = 0; i < 200; i++)
                r_putpix(randn(PAGE_W), randn(PAGE_H), randn(20) + 10);
            r_ctext_center(VIEW_W / 2, 30, 0, "XQUEST 32X");
            r_text_center(0, VIEW_W / 2, 70, "PORT OF THE CLASSIC PC GAME");
            r_text_center(0, VIEW_W / 2, 88, "BY MARK MACKEY");
            r_text_center(0, VIEW_W / 2, 120, "DIFFICULTY:");
            r_text_box(0, VIEW_W / 2 - 40, 140, diffname[sel]);
            r_text_center(0, VIEW_W / 2, 175, "PRESS START");
            r_text_center(0, VIEW_W / 2, 195,
                          "DPAD MOVE  B FIRE  C BOMB  A BRAKE");
            r_present_full(0, 0);
            redraw = 0;
        }
        {
            uint16_t pad = hw_pad(0);
            uint16_t pressed = pad & ~prevpad;
            prevpad = pad;
            hw_wait_vblank();
            rand16();
            if (pressed & PAD_LEFT) {
                if (sel > 0)
                    sel--;
                snd_play(SND_MENUCLICK);
                redraw = 1;
            }
            if (pressed & PAD_RIGHT) {
                if (sel < 4)
                    sel++;
                snd_play(SND_MENUCLICK);
                redraw = 1;
            }
            if (pressed & (PAD_START | PAD_B))
                return sel;
        }
    }
}

int main(void)
{
    hw_init();
    snd_init();

    title_screen();
    for (;;) {
        DiffLevel = menu_screen();
        game_run();
    }
    return 0;
}
