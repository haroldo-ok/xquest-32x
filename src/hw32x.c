/* XQuest 32X - hardware layer implementation */
#include "hw32x.h"

static uint16_t s_curfb;   /* framebuffer currently selected for DRAWING */

#define FB_LINETABLE 0x100  /* words: line table occupies first 0x100 words */

static void init_linetable(void)
{
    volatile uint16_t *fb = MARS_FRAMEBUFFER;
    int i;
    /* 224 visible lines; every line points to its own 320-byte row.
     * word offset for line i = i*160 + 0x100 */
    for (i = 0; i < 256; i++) {
        int line = (i < SCREEN_H) ? i : SCREEN_H - 1;
        fb[i] = (uint16_t)(line * (SCREEN_W / 2) + FB_LINETABLE);
    }
}

static void clear_fb(void)
{
    volatile uint16_t *fb = MARS_FRAMEBUFFER;
    int i;
    for (i = 0; i < (SCREEN_W * SCREEN_H) / 2; i++)
        fb[FB_LINETABLE + i] = 0;
}

void hw_init(void)
{
    int i;

    /* wait for the VDP to become accessible and set 256-colour mode */
    while ((MARS_VDP_FBCTL & MARS_VDP_VBLK) == 0)
        ;
    MARS_VDP_DISPMODE = MARS_224_LINES | MARS_VDP_MODE_256;

    /* prepare both framebuffers */
    s_curfb = MARS_VDP_FBCTL & MARS_VDP_FS;
    for (i = 0; i < 2; i++) {
        init_linetable();
        clear_fb();
        MARS_VDP_FBCTL = s_curfb ^ 1;
        while ((MARS_VDP_FBCTL & MARS_VDP_FS) == s_curfb)
            ;
        s_curfb ^= 1;
    }

    /* black palette until the game loads one */
    for (i = 0; i < 256; i++)
        MARS_CRAM[i] = 0;
}

void hw_set_palette(const uint16_t *pal256)
{
    int i;
    /* CRAM should only be touched during vblank */
    while ((MARS_VDP_FBCTL & MARS_VDP_VBLK) == 0)
        ;
    for (i = 0; i < 256; i++)
        MARS_CRAM[i] = pal256[i] | 0x8000;   /* priority: 32X above MD */
}

void hw_set_color(int idx, uint16_t bgr15)
{
    while ((MARS_VDP_FBCTL & MARS_VDP_VBLK) == 0)
        ;
    MARS_CRAM[idx] = bgr15 | 0x8000;
}

volatile uint8_t *hw_draw_fb(void)
{
    return (volatile uint8_t *)MARS_FRAMEBUFFER + FB_LINETABLE * 2;
}

void hw_flip(void)
{
    MARS_VDP_FBCTL = s_curfb ^ 1;
    while ((MARS_VDP_FBCTL & MARS_VDP_FS) == s_curfb)
        ;
    s_curfb ^= 1;
}

void hw_wait_vblank(void)
{
    uint32_t t = MARS_SYS_COMM12;
    while (MARS_SYS_COMM12 == t)
        ;
}

uint16_t hw_pad(int port)
{
    uint16_t v = (port == 0) ? MARS_SYS_COMM8 : MARS_SYS_COMM10;
    if (v == 0xF001)   /* mouse plugged in - ignore */
        return 0;
    return v & 0x0FFF;
}

uint32_t hw_vblank_count(void)
{
    return MARS_SYS_COMM12;
}
