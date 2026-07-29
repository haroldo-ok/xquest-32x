/*
 * Host-side harness: compiles the actual 32X game sources for the host
 * (x86) with stubbed hardware, runs thousands of simulated frames with
 * scripted input, and relies on AddressSanitizer/UBSan to catch memory
 * bugs. Also asserts basic game-state invariants each frame.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* ---- stub hw32x ---- */
uint16_t stub_pad;
uint32_t stub_vbl;

void hw_init(void) {}
void hw_set_palette(const uint16_t *p) { (void)p; }
void hw_set_color(int i, uint16_t c) { (void)i; (void)c; }
static uint8_t stub_fb[320 * 224];
volatile uint8_t *hw_draw_fb(void) { return stub_fb; }
void hw_flip(void) {}
void hw_wait_vblank(void) { stub_vbl++; }
uint16_t hw_pad(int port) { return port == 0 ? stub_pad : 0; }
uint32_t hw_vblank_count(void) { return stub_vbl; }
void snd_init(void) {}
void snd_play(int id) { (void)id; }

/* pull in the real game with main renamed */
#define main game_main
#include "../src/game.c"
#undef main

/* ------------------------------------------------------------------ */
static int frames_run;
static int check_invariants(void)
{
    int i, bad = 0;

    if (NumEnemies < 0 || NumEnemies > MAXENEMIES) {
        printf("BAD NumEnemies=%d (frame %d)\n", NumEnemies, frames_run);
        bad = 1;
    }
    if (ship.nummissiles < 0 || ship.nummissiles > MAXMISSILES) {
        printf("BAD nummissiles=%d (frame %d)\n", ship.nummissiles, frames_run);
        bad = 1;
    }
    if (NumEnemyMissiles < 0 || NumEnemyMissiles > MAXEMISSILES) {
        printf("BAD NumEnemyMissiles=%d (frame %d)\n", NumEnemyMissiles,
               frames_run);
        bad = 1;
    }
    if (NumEnemyMines < 0 || NumEnemyMines > MAXEMINES) {
        printf("BAD NumEnemyMines=%d (frame %d)\n", NumEnemyMines, frames_run);
        bad = 1;
    }
    for (i = 1; i <= NumEnemies; i++) {
        if (enemy[i].ntyp < 0 || enemy[i].ntyp > MAXENEMYKINDS) {
            printf("BAD enemy[%d].ntyp=%d (frame %d)\n", i, enemy[i].ntyp,
                   frames_run);
            bad = 1;
        }
        if (enemy[i].frame < 0 ||
            (enemy[i].frame >> 8) > enemykind[enemy[i].ntyp].numframes) {
            printf("BAD enemy[%d].frame=%d ntyp=%d (frame %d)\n", i,
                   enemy[i].frame, enemy[i].ntyp, frames_run);
            bad = 1;
        }
    }
    if (G.level < 1 || G.level > MAXLEVEL) {
        printf("BAD level=%d (frame %d)\n", G.level, frames_run);
        bad = 1;
    }
    return bad;
}

/* one iteration of the inner game loop (mirrors game_run's inner loop) */
static void run_frame(void)
{
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
                AddEnemy(ship.x, ship.y, 1);
            }
            ShipDestroyedCount--;
        }
    }
    if (!ShipDestroyed)
        MoveShip();
    if (SmartBombed > 1 && (FrameCount & 1) == 0)
        SmartBombed--;
    DrawSprites();
    if (GateMoving) {
        GateMoveCount += levels[G.level - 1].gatemove;
        if (GateMoveCount > 63)
            MoveGate();
    }
    FrameCount++;
    G.timeonlevel++;
    frames_run++;
}

int main(int argc, char **argv)
{
    int seed = argc > 1 ? atoi(argv[1]) : 1;
    int maxframes = argc > 2 ? atoi(argv[2]) : 36000;
    int startlevel = argc > 3 ? atoi(argv[3]) : 1;
    int deaths = 0, levels_done = 0, bad = 0;

    rngstate = seed ? seed : 1;
    DiffLevel = 2;

    InitialiseVariables();
    G.level = startlevel;
    G.totallevel = startlevel;
    SetupNewLevel();
    StartNewLevel();

    while (frames_run < maxframes) {
        /* scripted "random walk" input */
        stub_pad = 0;
        switch ((frames_run / 30) % 8) {
        case 0: stub_pad = PAD_RIGHT; break;
        case 1: stub_pad = PAD_RIGHT | PAD_UP; break;
        case 2: stub_pad = PAD_UP; break;
        case 3: stub_pad = PAD_LEFT | PAD_B; break;
        case 4: stub_pad = PAD_LEFT | PAD_DOWN; break;
        case 5: stub_pad = PAD_DOWN | PAD_B; break;
        case 6: stub_pad = PAD_B; break;
        case 7: if ((frames_run % 600) == 0) stub_pad = PAD_C; break;
        }

        run_frame();
        bad |= check_invariants();

        if (LevelFinished) {
            levels_done++;
            G.level++;
            G.totallevel++;
            if (G.level > MAXLEVEL) {
                if (G.gameclocked < 5)
                    G.gameclocked++;
                G.level = 1;
            }
            NumEnemies = 0;
            SetupNewLevel();
            StartNewLevel();
        } else if (ShipDestroyedCount == 0) {
            deaths++;
            G.lives--;
            if (G.lives <= 0) {
                /* restart the game */
                InitialiseVariables();
                G.level = startlevel;
                G.totallevel = startlevel;
                SetupNewLevel();
            } else {
                int pu;
                for (pu = 0; pu < PU_COUNT; pu++)
                    pu_value[pu] = 0;
            }
            ShipDestroyedCount = START_SHIPDESTROYED;
            NumEnemies = 0;
            StartNewLevel();
        }
    }

    printf("host-sim: %d frames, %d deaths, %d levels finished, score=%d\n",
           frames_run, deaths, levels_done, (int)G.score);
    if (bad) {
        printf("host-sim: INVARIANT FAILURES\n");
        return 1;
    }
    printf("host-sim: OK\n");
    return 0;
}
