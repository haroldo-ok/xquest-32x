/* Missile velocity regression test:
 * - shot fired from a standing ship must travel fast (muzzle velocity)
 * - shot fired while moving must be at least as fast (momentum added)
 * - shot direction must match the ship's facing
 */
#include <stdio.h>
#include <stdint.h>
#include <math.h>
#include <stdlib.h>
uint16_t stub_pad; uint32_t stub_vbl;
void hw_init(void){} void hw_set_palette(const uint16_t*p){(void)p;}
void hw_set_color(int i,uint16_t c){(void)i;(void)c;}
static uint8_t stub_fb[320*224];
volatile uint8_t*hw_draw_fb(void){return stub_fb;}
void hw_flip(void){} void hw_wait_vblank(void){stub_vbl++;}
uint16_t hw_pad(int p){return p==0?stub_pad:0;}
uint32_t hw_vblank_count(void){return stub_vbl;}
void snd_init(void){} void snd_play(int id){(void)id;}
#define main game_main
#include "../src/game.c"
#undef main

static void frame(void){
    poll_input();
    EraseSprites();
    UpdateMissiles();
    if(!ShipDestroyed) MoveShip();
    DrawSprites();
    FrameCount++;
}

static void reset_ship(void){
    ship.x=SHIPSTARTX; ship.y=SHIPSTARTY;
    ship.sx=ship.x<<6; ship.sy=ship.y<<6;
    ship.oldx=ship.x; ship.oldy=ship.y;
    ship.delx=0; ship.dely=0;
    ship.nummissiles=0;
    ShipDestroyed=0;
}

int main(void){
    int fail=0, f;
    rngstate=42; DiffLevel=2;
    InitialiseVariables();
    SetupNewLevel();
    StartNewLevel();
    NumEnemies=0; G.gameclocked=0;

    /* 1: standing shot must move at >= 6 px/frame */
    reset_ship();
    ship.dir=6;                     /* facing right */
    stub_pad=PAD_B; frame();        /* fire */
    stub_pad=0;
    if(ship.nummissiles<1){ printf("FAIL: no missile fired\n"); return 1; }
    {
        int16_t vx=missiles[1].delx, vy=missiles[1].dely;
        double speed=sqrt((double)vx*vx+(double)vy*vy)/64.0;
        printf("standing shot (facing right): v=(%d,%d) = %.2f px/frame\n",vx,vy,speed);
        if(speed < 6.0){ printf("FAIL: standing shot too slow\n"); fail=1; }
        else printf("PASS: standing shot speed\n");
        if(vx <= 0 || abs(vy) > vx/4){ printf("FAIL: standing shot direction wrong\n"); fail=1; }
        else printf("PASS: standing shot direction\n");
    }

    /* 2: shot fired while moving right must be faster than muzzle alone */
    reset_ship();
    stub_pad=PAD_RIGHT;
    for(f=0;f<60;f++){
        frame();
        /* keep ship away from the wall while it accelerates */
        ship.x=SHIPSTARTX; ship.y=SHIPSTARTY;
        ship.sx=(ship.sx&63)|(ship.x<<6); ship.sy=(ship.sy&63)|(ship.y<<6);
        ship.oldx=ship.x; ship.oldy=ship.y;
    }
    {
        int16_t shipv=ship.delx;
        stub_pad=PAD_RIGHT|PAD_B; frame();
        stub_pad=0;
        int n=ship.nummissiles;
        int16_t vx=missiles[n].delx;
        double speed=vx/64.0;
        printf("moving shot: shipv=%d, missile vx=%d = %.2f px/frame\n",shipv,vx,speed);
        if(vx < MISSILE_MUZZLE + shipv/2){ printf("FAIL: momentum not added\n"); fail=1; }
        else printf("PASS: momentum added\n");
    }

    /* 3: missile actually crosses the arena: it must cover >=140 px
     * before dying (wall) or within 30 frames */
    reset_ship();
    ship.dir=6;
    stub_pad=0; frame();            /* release button so press registers */
    stub_pad=PAD_B; frame(); stub_pad=0;
    if(ship.nummissiles<1){ printf("FAIL: test3 no missile\n"); fail=1; }
    else {
        int16_t x0=missiles[1].x;
        int alive=1, dist=0;
        for(f=0;f<30 && alive;f++){
            frame();
            if(ship.nummissiles<1) alive=0;
            else dist=missiles[1].x-x0;
        }
        printf("missile travel: %d px in %d frames (alive=%d)\n",dist,f,alive);
        if(dist<140){ printf("FAIL: missile crawls\n"); fail=1; }
        else printf("PASS: missile crosses arena\n");
    }

    /* 4: all 24 facings: standing shot speed must be consistent */
    {
        int d, bad=0;
        for(d=0;d<24;d++){
            reset_ship();
            ship.dir=d;
            stub_pad=0; frame();
            reset_ship(); ship.dir=d;
            stub_pad=PAD_B; frame(); stub_pad=0;
            if(ship.nummissiles<1){ printf("FAIL: dir %d no missile\n",d); bad=1; continue; }
            int16_t vx=missiles[1].delx, vy=missiles[1].dely;
            double speed=sqrt((double)vx*vx+(double)vy*vy)/64.0;
            if(speed<6.0 || speed>8.5){ printf("FAIL: dir %d speed %.2f\n",d,speed); bad=1; }
        }
        if(bad) fail=1; else printf("PASS: all 24 facings consistent muzzle speed\n");
    }

    printf(fail ? "SHOT TEST: FAILURES\n" : "SHOT TEST: ALL PASS\n");
    return fail;
}
