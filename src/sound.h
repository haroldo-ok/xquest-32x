/* XQuest 32X - PWM sound, played by the slave SH2 */
#ifndef SOUND_H
#define SOUND_H

/* sound ids (1-based, matching the original xqvars.pas constants) */
#define SND_FIRE6       1
#define SND_FIRE5       2
#define SND_PHEW        3
#define SND_FIRE4       4
#define SND_FIRE        5
#define SND_BOING       6
#define SND_SQUELCH     7
#define SND_WOOHOO      8
#define SND_ALLRIGHT    9
#define SND_OHYEAH      10
#define SND_GETCRYSTAL  11
#define SND_EXPLOSN     12
#define SND_EXPLOSN2    13
#define SND_EXPLOSN3    14
#define SND_RETALIATE   15
#define SND_OW          16
#define SND_COUNTDOWN   17
#define SND_GATESOUND   18
#define SND_SXTSMASH    19
#define SND_BARK        20
#define SND_APPLAUSE    21
#define SND_ENEMYENT    22
#define SND_MENUCLICK   23
#define SND_DOH         24
#define SND_REPULSE     25

void snd_init(void);          /* master side */
void snd_play(int id);        /* master side; id = 1..25, 0 = none */
void slave(void);             /* slave SH2 entry point (called from crt0) */

#endif
