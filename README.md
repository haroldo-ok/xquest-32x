# XQuest 32X

A Sega 32X port of **XQuest** (the classic MS-DOS arcade game by Mark
Mackey, 1994-1996 — "Get gems! Avoid mines! Blow lots of things to
pieces!").

The original game was written in Turbo Pascal + x86 assembly using
Mode X VGA (392x320 logical page, hardware scrolling, split-screen
status bar). This port re-implements the gameplay in C for the 32X's
SH2 CPUs, and converts all the original data files (sprites, fonts,
palettes, enemy stats, level tables, digitized sounds) at build time.

Original source: https://www.dosgamesarchive.com/file/xquest/xquest_13_src

## The ROM

```
xquest32x.32x          <- ready-to-run ROM (also in build/xquest32x.32x)
```

Runs in PicoDrive, Ares, BlastEm-mars, Gens/GS, Fusion, or on real
hardware via a flash cart. 384 KB, NTSC/PAL auto.

## Controls

| Control | Action |
|---------|--------|
| D-Pad   | thrust (inertial, with cruise-speed cap and gentle auto-coast when released) |
| B       | fire - shots leave at full muzzle speed in the facing direction, plus the ship's momentum (hold with Rapid Fire powerup for autofire) |
| C       | smart bomb |
| A       | brake |
| START   | start game / pause |

Gameplay: collect all the crystals, avoid the mines, then escape
through the gate at the top of the arena. Enemies pour in through the
inlets on the left/right walls. Touching a wall on AVERAGE difficulty
or above is fatal; supercrystals grant powerups (Shield, Aimed / Rapid
/ Multi / Ass / Heavy Fire, Bounce, mine-sweep, gate-freeze).

## Architecture

```
src/
  m68k_crt0.s   68000 ROM header + Mars vector table  (Chilly Willy's 32X startup code)
  m68k_crt1.s   68000-side helper loop (pad reading, vblank counter)
  sh2_crt0.s    SH2 master/slave startup + Mars module header
  mars.ld       linker script (ROM @ 0x02000000, SDRAM @ 0x06000000)
  hw32x.c/h     32X VDP (8bpp packed-pixel mode), CRAM, pad, vblank
  render.c/h    software renderer: 392x320 back buffer -> 320x224 FB,
                sprites, masks, both original fonts, status bar
  sound.c/h     IRQ-driven PWM audio @ 11 kHz on the slave SH2:
                the PWM timer interrupt mixes up to 4 voices of the
                original digitized sound effects into the mono FIFO
  game.c        the game: faithful C translation of xquest.pas logic
                (enemy AI incl. curves/zoom/repulse/tribble/cluster,
                 missiles, powerups, gate, scrolling, levels 1-50,
                 difficulty levels, game-clocked attractor mode)
tools/
  convert_assets.py  converts xquest.gfx/.enm/.fnt/.snd/palette.inc/
                     title.pbm + tables parsed from xqvars.pas into C
  fix_checksum.py    Genesis header checksum fixer
assets/         original XQuest 1.3 data files (from the released source)
tests/          automated emulator tests (see below)
```

### Notable porting decisions

- The DOS game drew into a 392x320 Mode X page with EGA-style planar
  tricks and used VGA hardware panning; here the playfield lives in a
  392x320 work-RAM buffer and the visible 320x199 window plus the
  25-line status bar are copied to the 32X framebuffer every frame
  (32-bit writes, line-table double buffering).
- All positions/velocities keep the original 10.6 fixed-point format;
  enemy stats come straight from `xquest.enm` (including Turbo Pascal
  6-byte `real` probabilities, converted to 16-bit fixed-point).
- The original ran at ~67 fps (VGA vsync); the 32X runs at 60. The
  base game speed constant is rescaled (64 -> 72) so real-time pacing
  matches, and all powerup/level timers use 60 fps.
- Collision detection uses the same 32-bit row-mask scheme as the
  original `makemask`/`CollideBitmaps` routines.
- Sound: the 25 original 8-bit ~11 kHz samples from `xquest.snd` play
  through the 32X PWM channels. The slave SH2 owns the PWM unit: it
  programs the cycle register for 11025 Hz (NTSC/PAL aware), enables
  the PWM timer interrupt (TM=3, matching the 3-deep FIFO), and the
  IRQ handler mixes up to 4 concurrent voices into the mono FIFO with
  an anti-click power-on ramp. The master posts sound ids through
  COMM4 (two 8-bit request slots per frame).

## Building

Requirements: marsdev SH-ELF + M68K-ELF toolchains, python3.

```
make MARSDEV=/path/to/mars CHILLY=/path/to/chilly/sega
```

(defaults point at /home/user/tools). Output: `xquest32x.32x`.

## Tests

Point-to-point tests run the ROM headless in **PicoDrive**
(libretro core) via a tiny C frontend (`tests/runner.c`) driven by
scripts with input injection and screen assertions:

```
tests/run_tests.sh          # runs everything
```

- **test 0** - ROM header sanity (TMSS signature, Mars module header,
  padding, checksum).
- **smoke.script** - boots the ROM and asserts the screen is never
  black (>= N distinct colours) at several checkpoints.
- **p2p_gameplay.script** - full path: title -> menu (input response
  asserted) -> READY -> level 1 -> move/shoot -> pause/resume ->
  smart bomb; every screen asserted non-black and alive.
- **soak.script** - ~2 minutes of emulated play with continuous
  input; asserts no freeze/black screen, including the death/READY
  cycle.
- **audio.script** - PWM sound test: asserts audible output (peak
  amplitude thresholds on the emulator's audio stream) for the title
  sound, menu clicks, firing and the smart bomb, asserts silence when
  nothing should play, and records `tests/artifacts/sfx_demo.wav`.
- **check_facing.py** - regression test for ship sprite orientation:
  template-matches the on-screen ship against all 24 original frames
  after accelerating in each cardinal direction.
- **check_controls.py** - pad controllability regression: taps each
  direction and waits; the ship must coast to rest away from the
  walls and remain alive (template-matched intact on screen).
- **host_ctrl.c** - host-side control-physics test (ASan/UBSan):
  asserts the cruise-speed cap, coast-down time (<2.5 s), brake
  strength (halves speed in <0.25 s) and the coast stopping distance
  from cruise (<60 px).
- **check_shots.py** - missile-velocity regression (emulator): fires
  from a standing ship and tracks the shot by frame differencing; it
  must advance at >= 5 px/frame.
- **host_shot.c** - host-side shot-physics test (ASan/UBSan): standing
  shots have full muzzle velocity in the facing direction, moving
  shots gain the ship's momentum, shots cross the arena, and all 24
  facings give a consistent muzzle speed.
- **host_sim.c** - compiles `game.c` for the host with stubbed
  hardware and runs 36000+ frames under AddressSanitizer/UBSan while
  checking game-state invariants every frame:
  `gcc -fsanitize=address,undefined -o tests/host_sim tests/host_sim.c
   src/render.c gen/assets.c -Isrc -Igen && ./tests/host_sim 1 36000 1`

Screenshots of every checkpoint are saved to `tests/artifacts/`.

## Credits / license

- Original game, graphics, sounds and design (C) 1994-1996 Mark
  Mackey. The XQuest 1.3 sources and data were released by the author;
  the shareware distribution terms in `orig/f1177/LICENSE.DOC` apply
  to the original assets.
- 32X startup code (m68k_crt0/crt1, sh2_crt0) by Chilly Willy
  (public 32X homebrew devkit code).
- Port code in `src/*.c`, tools and tests: new work for this port.
- Not affiliated with or endorsed by SEGA.
