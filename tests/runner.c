/*
 * Minimal headless libretro frontend for point-to-point testing of the
 * XQuest 32X ROM under PicoDrive.
 *
 * Usage: runner <core.so> <rom> <script>
 *
 * Script: text file, one command per line:
 *   run N            - run N frames
 *   press BTN N      - hold button for the next N frames (A,B,C,START,UP,...)
 *   shot FILE        - dump current frame as PPM
 *   assert_notblack THRESHOLD  - fail if < THRESHOLD distinct colours on screen
 *   assert_changed   - fail if the framebuffer is identical to the last
 *                      snapshot taken with 'snap'
 *   snap             - remember current framebuffer
 *   print_colors     - print number of distinct colours
 * Exit code 0 = all assertions passed.
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include "libretro.h"

static void *core;

#define SYM(name) static typeof(name) *p_##name
SYM(retro_init);
SYM(retro_deinit);
SYM(retro_api_version);
SYM(retro_set_environment);
SYM(retro_set_video_refresh);
SYM(retro_set_audio_sample);
SYM(retro_set_audio_sample_batch);
SYM(retro_set_input_poll);
SYM(retro_set_input_state);
SYM(retro_load_game);
SYM(retro_unload_game);
SYM(retro_run);
SYM(retro_get_system_av_info);

/* ------------------------------------------------------------ state */
static uint8_t fb[1024 * 1024 * 4];
static unsigned fb_w, fb_h, fb_pitch;
static enum retro_pixel_format pixfmt = RETRO_PIXEL_FORMAT_0RGB1555;
static int16_t pad_state[16];
static uint8_t snap_buf[1024 * 1024 * 4];
static unsigned snap_w, snap_h;
static int failures;

static void logcb(enum retro_log_level level, const char *fmt, ...)
{
    (void)level;
    (void)fmt;
}

static bool env_cb(unsigned cmd, void *data)
{
    switch (cmd) {
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
        pixfmt = *(enum retro_pixel_format *)data;
        return true;
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE: {
        struct retro_log_callback *cb = data;
        cb->log = logcb;
        return true;
    }
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY: {
        *(const char **)data = ".";
        return true;
    }
    case RETRO_ENVIRONMENT_GET_VARIABLE: {
        struct retro_variable *var = data;
        var->value = NULL;
        return false;
    }
    case RETRO_ENVIRONMENT_GET_CAN_DUPE:
        *(bool *)data = true;
        return true;
    default:
        return false;
    }
}

static void video_cb(const void *data, unsigned width, unsigned height,
                     size_t pitch)
{
    if (!data)
        return;
    fb_w = width;
    fb_h = height;
    fb_pitch = pitch;
    memcpy(fb, data, pitch * height);
}

/* WAV recording */
static FILE *wav_file;
static uint32_t wav_frames;

static void wav_start(const char *path)
{
    static const uint8_t hdr[44] = {0};
    wav_file = fopen(path, "wb");
    wav_frames = 0;
    if (wav_file)
        fwrite(hdr, 1, 44, wav_file);   /* placeholder header */
}

static void wav_finish(unsigned rate)
{
    if (!wav_file)
        return;
    uint32_t data_len = wav_frames * 4;
    uint32_t riff_len = 36 + data_len;
    uint8_t h[44];
    memcpy(h, "RIFF", 4);
    h[4]=riff_len; h[5]=riff_len>>8; h[6]=riff_len>>16; h[7]=riff_len>>24;
    memcpy(h+8, "WAVEfmt ", 8);
    h[16]=16; h[17]=0; h[18]=0; h[19]=0;
    h[20]=1; h[21]=0;           /* PCM */
    h[22]=2; h[23]=0;           /* stereo */
    h[24]=rate; h[25]=rate>>8; h[26]=rate>>16; h[27]=rate>>24;
    uint32_t br = rate*4;
    h[28]=br; h[29]=br>>8; h[30]=br>>16; h[31]=br>>24;
    h[32]=4; h[33]=0; h[34]=16; h[35]=0;
    memcpy(h+36, "data", 4);
    h[40]=data_len; h[41]=data_len>>8; h[42]=data_len>>16; h[43]=data_len>>24;
    fseek(wav_file, 0, SEEK_SET);
    fwrite(h, 1, 44, wav_file);
    fclose(wav_file);
    wav_file = NULL;
}

/* audio capture: track peak absolute sample since last reset */
static int32_t audio_peak;
static uint64_t audio_energy;
static uint64_t audio_samples;

static void audio_acc(int16_t l, int16_t r)
{
    if (wav_file) {
        int16_t fr[2] = { l, r };
        fwrite(fr, 1, 4, wav_file);
        wav_frames++;
    }
    int32_t al = l < 0 ? -l : l;
    int32_t ar = r < 0 ? -r : r;
    if (al > audio_peak) audio_peak = al;
    if (ar > audio_peak) audio_peak = ar;
    audio_energy += (int64_t)l * l + (int64_t)r * r;
    audio_samples += 2;
}

static void audio_cb(int16_t l, int16_t r) { audio_acc(l, r); }
static size_t audio_batch_cb(const int16_t *d, size_t frames)
{
    size_t i;
    for (i = 0; i < frames; i++)
        audio_acc(d[i * 2], d[i * 2 + 1]);
    return frames;
}
static void input_poll_cb(void) {}
static int16_t input_state_cb(unsigned port, unsigned device, unsigned index,
                              unsigned id)
{
    (void)device;
    (void)index;
    if (port == 0 && id < 16)
        return pad_state[id];
    return 0;
}

/* --------------------------------------------------------- pixel ops */
static uint32_t get_px(unsigned x, unsigned y)
{
    if (pixfmt == RETRO_PIXEL_FORMAT_XRGB8888) {
        return *(uint32_t *)(fb + y * fb_pitch + x * 4) & 0xFFFFFF;
    } else {  /* RGB565 or 0RGB1555 */
        return *(uint16_t *)(fb + y * fb_pitch + x * 2);
    }
}

static int count_colors(void)
{
    static uint8_t seen[65536];
    int n = 0;
    unsigned x, y;
    if (pixfmt == RETRO_PIXEL_FORMAT_XRGB8888) {
        /* quantize to 16 bits for counting */
        memset(seen, 0, sizeof(seen));
        for (y = 0; y < fb_h; y++)
            for (x = 0; x < fb_w; x++) {
                uint32_t p = get_px(x, y);
                uint16_t q = ((p >> 8) & 0xF800) | ((p >> 5) & 0x07E0) |
                             ((p >> 3) & 0x1F);
                if (!seen[q]) {
                    seen[q] = 1;
                    n++;
                }
            }
    } else {
        memset(seen, 0, sizeof(seen));
        for (y = 0; y < fb_h; y++)
            for (x = 0; x < fb_w; x++) {
                uint16_t q = (uint16_t)get_px(x, y);
                if (!seen[q]) {
                    seen[q] = 1;
                    n++;
                }
            }
    }
    return n;
}

static void save_ppm(const char *path)
{
    FILE *f = fopen(path, "wb");
    unsigned x, y;
    if (!f) {
        printf("FAIL: cannot write %s\n", path);
        failures++;
        return;
    }
    fprintf(f, "P6\n%u %u\n255\n", fb_w, fb_h);
    for (y = 0; y < fb_h; y++)
        for (x = 0; x < fb_w; x++) {
            uint32_t p = get_px(x, y);
            uint8_t rgb[3];
            if (pixfmt == RETRO_PIXEL_FORMAT_XRGB8888) {
                rgb[0] = p >> 16;
                rgb[1] = p >> 8;
                rgb[2] = p;
            } else if (pixfmt == RETRO_PIXEL_FORMAT_RGB565) {
                rgb[0] = (p >> 11) << 3;
                rgb[1] = ((p >> 5) & 0x3F) << 2;
                rgb[2] = (p & 0x1F) << 3;
            } else {
                rgb[0] = ((p >> 10) & 0x1F) << 3;
                rgb[1] = ((p >> 5) & 0x1F) << 3;
                rgb[2] = (p & 0x1F) << 3;
            }
            fwrite(rgb, 1, 3, f);
        }
    fclose(f);
}

/* ------------------------------------------------------------ button */
static int btn_id(const char *name)
{
    if (!strcmp(name, "UP")) return RETRO_DEVICE_ID_JOYPAD_UP;
    if (!strcmp(name, "DOWN")) return RETRO_DEVICE_ID_JOYPAD_DOWN;
    if (!strcmp(name, "LEFT")) return RETRO_DEVICE_ID_JOYPAD_LEFT;
    if (!strcmp(name, "RIGHT")) return RETRO_DEVICE_ID_JOYPAD_RIGHT;
    if (!strcmp(name, "A")) return RETRO_DEVICE_ID_JOYPAD_Y;     /* MD A */
    if (!strcmp(name, "B")) return RETRO_DEVICE_ID_JOYPAD_B;     /* MD B */
    if (!strcmp(name, "C")) return RETRO_DEVICE_ID_JOYPAD_A;     /* MD C */
    if (!strcmp(name, "START")) return RETRO_DEVICE_ID_JOYPAD_START;
    if (!strcmp(name, "X")) return RETRO_DEVICE_ID_JOYPAD_L;
    if (!strcmp(name, "Y")) return RETRO_DEVICE_ID_JOYPAD_X;
    if (!strcmp(name, "Z")) return RETRO_DEVICE_ID_JOYPAD_R;
    return -1;
}

int main(int argc, char **argv)
{
    if (argc < 4) {
        fprintf(stderr, "usage: %s core.so rom script\n", argv[0]);
        return 2;
    }

    core = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!core) {
        fprintf(stderr, "dlopen: %s\n", dlerror());
        return 2;
    }
#define LOAD(name) p_##name = dlsym(core, #name); \
    if (!p_##name) { fprintf(stderr, "missing sym " #name "\n"); return 2; }
    LOAD(retro_init);
    LOAD(retro_deinit);
    LOAD(retro_api_version);
    LOAD(retro_set_environment);
    LOAD(retro_set_video_refresh);
    LOAD(retro_set_audio_sample);
    LOAD(retro_set_audio_sample_batch);
    LOAD(retro_set_input_poll);
    LOAD(retro_set_input_state);
    LOAD(retro_load_game);
    LOAD(retro_unload_game);
    LOAD(retro_run);
    LOAD(retro_get_system_av_info);

    p_retro_set_environment(env_cb);
    p_retro_set_video_refresh(video_cb);
    p_retro_set_audio_sample(audio_cb);
    p_retro_set_audio_sample_batch(audio_batch_cb);
    p_retro_set_input_poll(input_poll_cb);
    p_retro_set_input_state(input_state_cb);
    p_retro_init();

    struct retro_game_info game = { 0 };
    game.path = argv[2];
    FILE *f = fopen(argv[2], "rb");
    if (!f) {
        fprintf(stderr, "cannot open rom %s\n", argv[2]);
        return 2;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    void *rom = malloc(sz);
    if (fread(rom, 1, sz, f) != (size_t)sz) {
        fprintf(stderr, "short read\n");
        return 2;
    }
    fclose(f);
    game.data = rom;
    game.size = sz;

    if (!p_retro_load_game(&game)) {
        fprintf(stderr, "retro_load_game failed\n");
        return 2;
    }

    FILE *script = fopen(argv[3], "r");
    if (!script) {
        fprintf(stderr, "cannot open script %s\n", argv[3]);
        return 2;
    }

    char line[512];
    int lineno = 0;
    while (fgets(line, sizeof(line), script)) {
        char cmd[64], arg1[256];
        long n;
        lineno++;
        if (line[0] == '#' || line[0] == '\n')
            continue;
        if (sscanf(line, "%63s", cmd) != 1)
            continue;

        if (!strcmp(cmd, "run")) {
            sscanf(line, "%*s %ld", &n);
            while (n-- > 0)
                p_retro_run();
        } else if (!strcmp(cmd, "press")) {
            sscanf(line, "%*s %255s %ld", arg1, &n);
            int id = btn_id(arg1);
            if (id < 0) {
                printf("FAIL(line %d): unknown button %s\n", lineno, arg1);
                failures++;
                continue;
            }
            pad_state[id] = 1;
            while (n-- > 0)
                p_retro_run();
            pad_state[id] = 0;
        } else if (!strcmp(cmd, "shot")) {
            sscanf(line, "%*s %255s", arg1);
            save_ppm(arg1);
            printf("shot %s (%ux%u)\n", arg1, fb_w, fb_h);
        } else if (!strcmp(cmd, "assert_notblack")) {
            n = 2;
            sscanf(line, "%*s %ld", &n);
            int c = count_colors();
            if (c < n) {
                printf("FAIL(line %d): screen too uniform: %d colours (< %ld)\n",
                       lineno, c, n);
                failures++;
            } else {
                printf("PASS(line %d): assert_notblack: %d colours\n",
                       lineno, c);
            }
        } else if (!strcmp(cmd, "snap")) {
            memcpy(snap_buf, fb, fb_pitch * fb_h);
            snap_w = fb_w;
            snap_h = fb_h;
        } else if (!strcmp(cmd, "assert_changed")) {
            if (snap_w == fb_w && snap_h == fb_h &&
                !memcmp(snap_buf, fb, fb_pitch * fb_h)) {
                printf("FAIL(line %d): framebuffer did not change\n", lineno);
                failures++;
            } else {
                printf("PASS(line %d): assert_changed\n", lineno);
            }
        } else if (!strcmp(cmd, "wav_start")) {
            sscanf(line, "%*s %255s", arg1);
            wav_start(arg1);
            printf("wav recording -> %s\n", arg1);
        } else if (!strcmp(cmd, "wav_stop")) {
            struct retro_system_av_info av;
            p_retro_get_system_av_info(&av);
            wav_finish((unsigned)av.timing.sample_rate);
            printf("wav stopped\n");
        } else if (!strcmp(cmd, "audio_reset")) {
            audio_peak = 0;
            audio_energy = 0;
            audio_samples = 0;
        } else if (!strcmp(cmd, "assert_audio")) {
            n = 500;
            sscanf(line, "%*s %ld", &n);
            if (audio_peak < n) {
                printf("FAIL(line %d): audio peak %d < %ld\n",
                       lineno, audio_peak, n);
                failures++;
            } else {
                printf("PASS(line %d): assert_audio: peak %d\n",
                       lineno, audio_peak);
            }
        } else if (!strcmp(cmd, "assert_silence")) {
            n = 500;
            sscanf(line, "%*s %ld", &n);
            if (audio_peak >= n) {
                printf("FAIL(line %d): audio peak %d >= %ld (expected silence)\n",
                       lineno, audio_peak, n);
                failures++;
            } else {
                printf("PASS(line %d): assert_silence: peak %d\n",
                       lineno, audio_peak);
            }
        } else if (!strcmp(cmd, "print_audio")) {
            printf("audio: peak=%d samples=%llu\n", audio_peak,
                   (unsigned long long)audio_samples);
        } else if (!strcmp(cmd, "print_colors")) {
            printf("colors: %d (%ux%u)\n", count_colors(), fb_w, fb_h);
        } else {
            printf("FAIL(line %d): unknown command %s\n", lineno, cmd);
            failures++;
        }
    }
    fclose(script);

    p_retro_unload_game();
    p_retro_deinit();
    free(rom);

    if (failures) {
        printf("== %d FAILURE(S) ==\n", failures);
        return 1;
    }
    printf("== ALL ASSERTIONS PASSED ==\n");
    return 0;
}
