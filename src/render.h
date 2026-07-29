/* XQuest 32X - software rendering onto the 32X 8bpp framebuffer.
 *
 * The DOS original used Mode X with a 392x320 logical page and a
 * hardware-scrolled 320x(200-split) window plus a split screen status
 * bar. On the 32X we keep the same 392x320 logical playfield in a
 * work RAM back buffer and blit the visible window + a 25-line status
 * bar into the framebuffer each frame.
 */
#ifndef RENDER_H
#define RENDER_H

#include <stdint.h>
#include "assets.h"

#define PAGE_W      392
#define PAGE_H      320
#define STATUS_H    25
#define VIEW_W      320
#define VIEW_H      (224 - STATUS_H)   /* 199 visible playfield lines */

extern uint8_t page[PAGE_H][PAGE_W];       /* playfield back buffer */
extern uint8_t statusbar[STATUS_H][VIEW_W];

/* playfield drawing */
void r_clear_page(uint8_t color);
void r_put_sprite(int idx, int x, int y);                 /* transparent */
void r_put_sprite_solid(int idx, int x, int y);           /* incl. colour 0 */
void r_save_under(int idx, int x, int y, uint8_t *buf);   /* copy page rect */
void r_restore_under(int idx, int x, int y, const uint8_t *buf);
void r_hline(int x1, int x2, int y, uint8_t c);
void r_vline(int x, int y1, int y2, uint8_t c);
void r_fill(int x1, int y1, int x2, int y2, uint8_t c);
void r_putpix(int x, int y, uint8_t c);

/* text (8x14 game font). target: 0 = playfield page, 1 = status bar */
void r_text(int target, int x, int y, const char *s);
void r_text_box(int target, int x, int y, const char *s);
void r_text_center(int target, int cx, int y, const char *s);
/* big "comix" font used on menus; returns text width in pixels */
int  r_ctext_width(const char *s);
void r_ctext(int x, int y, uint8_t color, const char *s);
void r_ctext_center(int cx, int y, uint8_t color, const char *s);

/* window with border (playfield coords) */
void r_window(int x1, int y1, int x2, int y2);

/* status bar */
void r_status_clear(void);
void r_status_sprite(int idx, int x, int y);
void r_status_fill(int x1, int y1, int x2, int y2, uint8_t c);

/* present: copy visible window at (vx,vy) + status bar to framebuffer */
void r_present(int vx, int vy);
/* present full 320x224 window of the page (for title/menu screens, vy=0) */
void r_present_full(int vx, int vy);

#endif
