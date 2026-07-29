/*
 * XQuest 32X - minimal hardware layer for the Sega 32X (Mars).
 * Original game (C) 1994-1996 Mark Mackey. 32X port implementation.
 */
#ifndef HW32X_H
#define HW32X_H

#include <stdint.h>

#define MARS_CRAM           ((volatile uint16_t *)0x20004200)
#define MARS_FRAMEBUFFER    ((volatile uint16_t *)0x24000000)
#define MARS_FB8            ((volatile uint8_t  *)0x24000000)

#define MARS_SYS_INTMSK     (*(volatile uint16_t *)0x20004000)
#define MARS_SYS_COMM0      (*(volatile uint16_t *)0x20004020)
#define MARS_SYS_COMM2      (*(volatile uint16_t *)0x20004022)
#define MARS_SYS_COMM4      (*(volatile uint16_t *)0x20004024)
#define MARS_SYS_COMM6      (*(volatile uint16_t *)0x20004026)
#define MARS_SYS_COMM8      (*(volatile uint16_t *)0x20004028) /* pad 1 */
#define MARS_SYS_COMM10     (*(volatile uint16_t *)0x2000402A) /* pad 2 */
#define MARS_SYS_COMM12     (*(volatile uint32_t *)0x2000402C) /* vblank count */

#define MARS_VDP_DISPMODE   (*(volatile uint16_t *)0x20004100)
#define MARS_VDP_FILLEN     (*(volatile uint16_t *)0x20004104)
#define MARS_VDP_FILADR     (*(volatile uint16_t *)0x20004106)
#define MARS_VDP_FILDAT     (*(volatile uint16_t *)0x20004108)
#define MARS_VDP_FBCTL      (*(volatile uint16_t *)0x2000410A)

#define MARS_NTSC_FORMAT    0x8000
#define MARS_224_LINES      0x0000
#define MARS_VDP_MODE_256   0x0001
#define MARS_VDP_VBLK       0x8000
#define MARS_VDP_FS         0x0001

/* pad bits (SEGA layout from the 68000 helper loop) */
#define PAD_UP      0x0001
#define PAD_DOWN    0x0002
#define PAD_LEFT    0x0004
#define PAD_RIGHT   0x0008
#define PAD_B       0x0010
#define PAD_C       0x0020
#define PAD_A       0x0040
#define PAD_START   0x0080
#define PAD_Z       0x0100
#define PAD_Y       0x0200
#define PAD_X       0x0400
#define PAD_MODE    0x0800

#define SCREEN_W    320
#define SCREEN_H    224

void hw_init(void);
void hw_set_palette(const uint16_t *pal256);
void hw_set_color(int idx, uint16_t bgr15);
/* returns pointer to the 8bpp pixel area of the current DRAW framebuffer */
volatile uint8_t *hw_draw_fb(void);
void hw_flip(void);           /* queue flip and wait for it to take effect */
void hw_wait_vblank(void);
uint16_t hw_pad(int port);    /* current pressed buttons */
uint32_t hw_vblank_count(void);

#endif
