/* Control-feel regression test: verifies cruise cap, coast damping,
 * brake strength and time/distance to stop from cruise speed. */
#include <stdio.h>
#include <stdint.h>
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

int main(void){
    int f, fail=0;
    rngstate=42; DiffLevel=2;
    InitialiseVariables();
    SetupNewLevel();
    StartNewLevel();
    NumEnemies=0;               /* isolate ship physics */
    G.gameclocked=0;

    /* 1: hold RIGHT for 3 seconds: speed must cap at cruise, not runaway */
    stub_pad=PAD_RIGHT;
    { int peak=0;
      for(f=0;f<180 && !ShipDestroyed;f++){ frame(); if(ship.delx>peak) peak=ship.delx; }
      printf("hold RIGHT 180f: delx peak=%d (cap %d, hard max %d)\n",
             peak, PAD_CRUISE+PAD_ACCEL, MAXSHIPSPEED);
      if(peak > PAD_CRUISE+PAD_ACCEL){ printf("FAIL: cruise cap exceeded\n"); fail=1; }
      else printf("PASS: cruise cap respected\n");
    }

    /* re-center to keep away from walls */
    ship.x=SHIPSTARTX; ship.y=SHIPSTARTY; ship.sx=ship.x<<6; ship.sy=ship.y<<6;
    ShipDestroyed=0;

    /* 2: release pad: ship must coast to (near) rest quickly */
    stub_pad=0;
    { int stopf=-1;
      for(f=0;f<300;f++){ frame();
        if(stopf<0 && ship.delx==0 && ship.dely==0) stopf=f;
        /* keep it away from walls during the test */
        ship.x=SHIPSTARTX; ship.y=SHIPSTARTY; ship.sx=(ship.sx&63)|(ship.x<<6); ship.sy=(ship.sy&63)|(ship.y<<6);
        ship.oldx=ship.x; ship.oldy=ship.y;
      }
      printf("release: stopped after %d frames (%.1fs)\n", stopf, stopf/60.0);
      if(stopf<0 || stopf>150){ printf("FAIL: coast-down too slow\n"); fail=1; }
      else printf("PASS: coast-down\n");
    }

    /* 3: brake from cruise: must halve speed in under 0.25 s */
    stub_pad=PAD_RIGHT;
    for(f=0;f<120;f++) frame();
    { int v0=ship.delx;
      stub_pad=PAD_A;
      for(f=0;f<15;f++) frame();
      printf("brake 15f: %d -> %d\n", v0, ship.delx);
      if(ship.delx > v0/2){ printf("FAIL: brake too weak\n"); fail=1; }
      else printf("PASS: brake\n");
    }

    /* 4: stopping distance from cruise (coast only) must be short
     * enough to react before a wall: < 60 px */
    ship.x=SHIPSTARTX; ship.y=SHIPSTARTY; ship.sx=ship.x<<6; ship.sy=ship.y<<6;
    ship.delx=PAD_CRUISE; ship.dely=0; ShipDestroyed=0;
    stub_pad=0;
    { int32_t startx=ship.sx;
      for(f=0;f<300 && ship.delx!=0;f++) frame();
      int dist=(ship.sx-startx)>>6;
      printf("coast stopping distance from cruise: %d px\n", dist);
      if(dist>60){ printf("FAIL: stopping distance too long\n"); fail=1; }
      else printf("PASS: stopping distance\n");
    }

    printf(fail ? "CONTROL TEST: FAILURES\n" : "CONTROL TEST: ALL PASS\n");
    return fail;
}
