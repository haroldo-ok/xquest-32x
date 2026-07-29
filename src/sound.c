/* XQuest 32X - PWM audio driver (IRQ-driven, slave SH2).
 *
 * The master CPU posts sound ids in COMM4 (two 8-bit slots per write).
 * The slave SH2 configures the PWM timer to raise an interrupt every
 * TM pulses at ~11 kHz; the PWM IRQ handler mixes up to 4 voices of
 * the original 8-bit unsigned samples into the mono FIFO.
 */
#include <stdint.h>
#include "sound.h"
#include "assets.h"

#define MARS_SYS_INTMSK_B   (*(volatile uint8_t  *)0x20004001)
#define MARS_SYS_COMM4      (*(volatile uint16_t *)0x20004024)
#define MARS_SYS_COMM6      (*(volatile uint16_t *)0x20004026)
#define MARS_VDP_DISPMODE   (*(volatile uint16_t *)0x20004100)
#define MARS_PWM_CTRL       (*(volatile uint16_t *)0x20004030)
#define MARS_PWM_CYCLE      (*(volatile uint16_t *)0x20004032)
#define MARS_PWM_MONO       (*(volatile uint16_t *)0x20004038)
#define MARS_NTSC_FORMAT    0x8000

#define SAMPLE_RATE 11025
#define NUM_VOICES  4
#define PWM_TM      3           /* IRQ every 3 pulses (FIFO is 3 deep) */

/* ---------------------------------------------------------- master side */
void snd_init(void)
{
    MARS_SYS_COMM4 = 0;
}

void snd_play(int id)
{
    uint16_t cur;

    if (id <= 0 || id > SND_COUNT)
        return;
    /* two pending request slots (low/high byte) so two effects fired
     * on the same frame both get through */
    cur = MARS_SYS_COMM4;
    if ((cur & 0x00FF) == 0)
        MARS_SYS_COMM4 = (uint16_t)(cur | id);
    else if ((cur & 0xFF00) == 0)
        MARS_SYS_COMM4 = (uint16_t)(cur | (id << 8));
    else
        MARS_SYS_COMM4 = (uint16_t)id;      /* both busy: overwrite */
}

/* ---------------------------------------------------------- slave side */
typedef struct {
    const uint8_t *volatile data;   /* nonzero = voice active */
    uint32_t pos, len;
} voice_t;

static voice_t voices[NUM_VOICES];
static uint32_t pwm_cycle, pwm_center;
static uint32_t ramp;               /* anti-click power-on ramp */

static void set_int_level(uint32_t lvl)
{
    uint32_t sr;
    __asm__ volatile("stc sr,%0" : "=r"(sr));
    sr = (sr & ~0xF0u) | (lvl << 4);
    __asm__ volatile("ldc %0,sr" : : "r"(sr));
}

static void pwm_hw_init(void)
{
    if (MARS_VDP_DISPMODE & MARS_NTSC_FORMAT)
        pwm_cycle = (((23011361ul << 1) / SAMPLE_RATE + 1) >> 1) + 1;
    else
        pwm_cycle = (((22801467ul << 1) / SAMPLE_RATE + 1) >> 1) + 1;
    pwm_center = pwm_cycle >> 1;
    ramp = 1;

    MARS_PWM_CYCLE = (uint16_t)pwm_cycle;
    /* TM = PWM_TM (interrupt timer), RTP, RMD = right, LMD = left */
    MARS_PWM_CTRL = (uint16_t)((PWM_TM << 8) | 0x0085);
}

static void start_voice(int id)
{
    int i, v = 0;

    if (id <= 0 || id > SND_COUNT)
        return;
    for (i = 0; i < NUM_VOICES; i++)
        if (!voices[i].data) {
            v = i;
            break;
        }
    voices[v].data = 0;             /* deactivate while updating */
    voices[v].pos = 0;
    voices[v].len = sounds[id - 1].len;
    voices[v].data = sounds[id - 1].data;   /* activate last */
}

/* PWM timer interrupt: called from slav_pwm_irq in sh2_crt0.s.
 * Fills the mono FIFO (3 entries) with mixed samples. */
void slave_pwm(void)
{
    int i, n;

    for (n = 0; n < PWM_TM; n++) {
        int32_t acc;
        int active;

        if (MARS_PWM_MONO & 0x8000)     /* FIFO full */
            break;

        /* anti-click ramp up to center after power-on */
        if (ramp < pwm_center) {
            ramp += 4;
            if (ramp > pwm_center)
                ramp = pwm_center;
            MARS_PWM_MONO = (uint16_t)ramp;
            continue;
        }

        acc = 0;
        active = 0;
        for (i = 0; i < NUM_VOICES; i++) {
            if (voices[i].data) {
                acc += (int32_t)voices[i].data[voices[i].pos] - 128;
                active++;
                if (++voices[i].pos >= voices[i].len)
                    voices[i].data = 0;
            }
        }
        /* one full-scale 8-bit voice (+-127) spans the whole PWM range;
         * clamp when multiple voices sum */
        acc = (int32_t)pwm_center + ((acc * (int32_t)pwm_center) >> 7);
        if (acc < 1)
            acc = 1;
        if (acc > (int32_t)pwm_cycle - 1)
            acc = (int32_t)pwm_cycle - 1;
        MARS_PWM_MONO = (uint16_t)acc;
        MARS_SYS_COMM6 = (uint16_t)(active | 0x100);    /* heartbeat */
    }
}

void slave(void)
{
    int i;

    for (i = 0; i < NUM_VOICES; i++)
        voices[i].data = 0;

    pwm_hw_init();

    MARS_SYS_INTMSK_B |= 0x01;      /* enable PWM interrupt on slave */
    set_int_level(0);               /* accept all interrupt levels */

    /* main loop: hand sound requests from the master to voice slots */
    for (;;) {
        uint16_t req = MARS_SYS_COMM4;
        if (req) {
            MARS_SYS_COMM4 = 0;
            start_voice(req & 0x00FF);
            start_voice(req >> 8);
        }
    }
}
