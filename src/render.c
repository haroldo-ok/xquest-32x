/* XQuest 32X - software renderer */
#include "render.h"
#include "hw32x.h"

uint8_t page[PAGE_H][PAGE_W];
uint8_t statusbar[STATUS_H][VIEW_W];

void r_clear_page(uint8_t color)
{
    uint32_t v = color | (color << 8);
    uint32_t *p = (uint32_t *)&page[0][0];
    int n = (PAGE_W * PAGE_H) / 4;
    v |= v << 16;
    while (n--)
        *p++ = v;
}

void r_putpix(int x, int y, uint8_t c)
{
    if ((unsigned)x < PAGE_W && (unsigned)y < PAGE_H)
        page[y][x] = c;
}

void r_put_sprite(int idx, int x, int y)
{
    const sprite_t *s = &gfx_sprites[idx];
    int w = s->stride, h = s->h, sx0 = 0, sy0 = 0, yy, xx;
    const uint8_t *px = s->px;

    if (x < 0) { sx0 = -x; x = 0; }
    if (y < 0) { sy0 = -y; y = 0; }
    if (x + (w - sx0) > PAGE_W) w = PAGE_W - x + sx0;
    if (y + (h - sy0) > PAGE_H) h = PAGE_H - y + sy0;

    for (yy = sy0; yy < h; yy++) {
        const uint8_t *src = px + yy * s->stride;
        uint8_t *dst = &page[y + yy - sy0][x - sx0];
        for (xx = sx0; xx < w; xx++)
            if (src[xx])
                dst[xx] = src[xx];
    }
}

void r_put_sprite_solid(int idx, int x, int y)
{
    const sprite_t *s = &gfx_sprites[idx];
    int w = s->stride, h = s->h, yy, xx;
    const uint8_t *px = s->px;

    for (yy = 0; yy < h; yy++) {
        if ((unsigned)(y + yy) >= PAGE_H)
            continue;
        const uint8_t *src = px + yy * s->stride;
        uint8_t *dst = &page[y + yy][x];
        for (xx = 0; xx < w; xx++)
            if ((unsigned)(x + xx) < PAGE_W)
                dst[xx] = src[xx];
    }
}

void r_save_under(int idx, int x, int y, uint8_t *buf)
{
    const sprite_t *s = &gfx_sprites[idx];
    int yy, xx;
    for (yy = 0; yy < s->h; yy++)
        for (xx = 0; xx < s->stride; xx++) {
            int px_ = x + xx, py_ = y + yy;
            if ((unsigned)px_ < PAGE_W && (unsigned)py_ < PAGE_H)
                buf[yy * s->stride + xx] = page[py_][px_];
            else
                buf[yy * s->stride + xx] = 0;
        }
}

void r_restore_under(int idx, int x, int y, const uint8_t *buf)
{
    const sprite_t *s = &gfx_sprites[idx];
    int yy, xx;
    for (yy = 0; yy < s->h; yy++)
        for (xx = 0; xx < s->stride; xx++) {
            int px_ = x + xx, py_ = y + yy;
            if ((unsigned)px_ < PAGE_W && (unsigned)py_ < PAGE_H)
                page[py_][px_] = buf[yy * s->stride + xx];
        }
}

void r_hline(int x1, int x2, int y, uint8_t c)
{
    int x;
    if ((unsigned)y >= PAGE_H)
        return;
    if (x1 < 0) x1 = 0;
    if (x2 >= PAGE_W) x2 = PAGE_W - 1;
    for (x = x1; x <= x2; x++)
        page[y][x] = c;
}

void r_vline(int x, int y1, int y2, uint8_t c)
{
    int y;
    if ((unsigned)x >= PAGE_W)
        return;
    if (y1 < 0) y1 = 0;
    if (y2 >= PAGE_H) y2 = PAGE_H - 1;
    for (y = y1; y <= y2; y++)
        page[y][x] = c;
}

void r_fill(int x1, int y1, int x2, int y2, uint8_t c)
{
    int y;
    for (y = y1; y <= y2; y++)
        r_hline(x1, x2, y, c);
}

/* ------------------------------------------------------------- text */
static void draw_glyph(uint8_t *dst, int dst_stride, const uint8_t *g)
{
    /* font1 glyph: 8x14, opaque (includes background colour cells) */
    int yy, xx;
    for (yy = 0; yy < 14; yy++)
        for (xx = 0; xx < 8; xx++)
            dst[yy * dst_stride + xx] = g[yy * 8 + xx];
}

static void text_at(int target, int x, int y, const char *s)
{
    int i;
    for (i = 0; s[i]; i++) {
        unsigned char c = (unsigned char)s[i] & 0x7F;
        int fi = fontmap[c];
        int gx = x + 8 * i;
        if (fi == 0) {
            /* blank cell */
            int yy, xx;
            for (yy = 0; yy < 14; yy++)
                for (xx = 0; xx < 8; xx++) {
                    int px_ = gx + xx, py_ = y + yy;
                    if (target == 0) {
                        if ((unsigned)px_ < PAGE_W && (unsigned)py_ < PAGE_H)
                            page[py_][px_] = 0;
                    } else {
                        if ((unsigned)px_ < VIEW_W && (unsigned)py_ < STATUS_H)
                            statusbar[py_][px_] = 0;
                    }
                }
            continue;
        }
        if (target == 0) {
            if (gx >= 0 && gx + 8 <= PAGE_W && y >= 0 && y + 14 <= PAGE_H)
                draw_glyph(&page[y][gx], PAGE_W, font1_px[fi - 1]);
        } else {
            if (gx >= 0 && gx + 8 <= VIEW_W && y >= 0 && y + 14 <= STATUS_H)
                draw_glyph(&statusbar[y][gx], VIEW_W, font1_px[fi - 1]);
        }
    }
}

void r_text(int target, int x, int y, const char *s)
{
    text_at(target, x, y, s);
}

static int slen(const char *s)
{
    int n = 0;
    while (s[n]) n++;
    return n;
}

static void draw_box(int x1, int y1, int x2, int y2)
{
    r_hline(x1, x2, y1, 15);
    r_hline(x1, x2, y2, 15);
    r_vline(x1, y1, y2, 15);
    r_vline(x2, y1, y2, 15);
}

void r_text_box(int target, int x, int y, const char *s)
{
    text_at(target, x, y, s);
    if (target == 0)
        draw_box(x - 2, y - 1, x + slen(s) * 8 + 2, y + 14);
}

void r_text_center(int target, int cx, int y, const char *s)
{
    text_at(target, cx - slen(s) * 4, y, s);
}

int r_ctext_width(const char *s)
{
    int w = 0, i;
    for (i = 0; s[i]; i++) {
        unsigned char c = (unsigned char)s[i] & 0x7F;
        if (font2[c].px)
            w += font2[c].w + 1;
        else
            w += 6;
    }
    return w;
}

void r_ctext(int x, int y, uint8_t color, const char *s)
{
    int i;
    for (i = 0; s[i]; i++) {
        unsigned char c = (unsigned char)s[i] & 0x7F;
        const comixchar_t *g = &font2[c];
        if (!g->px) {
            x += 6;
            continue;
        }
        int yy, xx;
        for (yy = 0; yy < g->h; yy++)
            for (xx = 0; xx < g->w; xx++) {
                uint8_t v = g->px[yy * g->w + xx];
                if (v) {
                    int px_ = x + xx, py_ = y + yy;
                    if ((unsigned)px_ < PAGE_W && (unsigned)py_ < PAGE_H)
                        page[py_][px_] = color ? color : v;
                }
            }
        x += g->w + 1;
    }
}

void r_ctext_center(int cx, int y, uint8_t color, const char *s)
{
    r_ctext(cx - r_ctext_width(s) / 2, y, color, s);
}

void r_window(int x1, int y1, int x2, int y2)
{
    r_fill(x1, y1, x2, y2, 2);
    draw_box(x1, y1, x2, y2);
}

/* --------------------------------------------------------- status bar */
void r_status_clear(void)
{
    int y, x;
    for (y = 0; y < STATUS_H; y++)
        for (x = 0; x < VIEW_W; x++)
            statusbar[y][x] = (y == STATUS_H - 1) ? 15 : 0;
}

void r_status_sprite(int idx, int x, int y)
{
    const sprite_t *s = &gfx_sprites[idx];
    int yy, xx;
    for (yy = 0; yy < s->h; yy++)
        for (xx = 0; xx < s->stride; xx++) {
            uint8_t v = s->px[yy * s->stride + xx];
            int px_ = x + xx, py_ = y + yy;
            if (v && (unsigned)px_ < VIEW_W && (unsigned)py_ < STATUS_H)
                statusbar[py_][px_] = v;
        }
}

void r_status_fill(int x1, int y1, int x2, int y2, uint8_t c)
{
    int y, x;
    for (y = y1; y <= y2; y++)
        for (x = x1; x <= x2; x++)
            if ((unsigned)x < VIEW_W && (unsigned)y < STATUS_H)
                statusbar[y][x] = c;
}

/* ------------------------------------------------------------ present */
static void copy_row(volatile uint8_t *dst, const uint8_t *src, int n)
{
    /* framebuffer only accepts 16/32-bit writes reliably; rows are
     * 4-aligned here (320 wide, x offsets aligned) */
    volatile uint32_t *d = (volatile uint32_t *)dst;
    const uint32_t *s = (const uint32_t *)src;
    n >>= 2;
    while (n--)
        *d++ = *s++;
}

void r_present(int vx, int vy)
{
    volatile uint8_t *fb = hw_draw_fb();
    int y;

    vx &= ~3;   /* keep 32-bit alignment of source rows */
    if (vx < 0) vx = 0;
    if (vx > PAGE_W - VIEW_W) vx = PAGE_W - VIEW_W;
    if (vy < 0) vy = 0;
    if (vy > PAGE_H - VIEW_H) vy = PAGE_H - VIEW_H;

    for (y = 0; y < STATUS_H; y++)
        copy_row(fb + y * SCREEN_W, &statusbar[y][0], VIEW_W);
    for (y = 0; y < VIEW_H; y++)
        copy_row(fb + (STATUS_H + y) * SCREEN_W, &page[vy + y][vx], VIEW_W);
    hw_flip();
}

void r_present_full(int vx, int vy)
{
    volatile uint8_t *fb = hw_draw_fb();
    int y;

    vx &= ~3;
    if (vx < 0) vx = 0;
    if (vx > PAGE_W - SCREEN_W) vx = PAGE_W - SCREEN_W;
    if (vy < 0) vy = 0;
    if (vy > PAGE_H - SCREEN_H) vy = PAGE_H - SCREEN_H;

    for (y = 0; y < SCREEN_H; y++)
        copy_row(fb + y * SCREEN_W, &page[vy + y][vx], SCREEN_W);
    hw_flip();
}
