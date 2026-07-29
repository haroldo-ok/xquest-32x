#!/usr/bin/env python3
"""
XQuest 32X asset converter.

Reads the original XQuest 1.3 data files (xquest.gfx, xquest.enm,
xquest.fnt, xquest2.fnt, xquest.snd, palette.inc, title.pbm) and the
level tables in xqvars.pas, and emits C source files with all the data
converted to formats convenient for the SH2 (big endian, 8bpp sprites,
32-bit row collision masks, 15-bit CRAM palette entries).

Load order of xquest.gfx follows InitialiseGraphics in xqinit.pas.
"""
import re
import struct
import sys
import os

SRCDIR = sys.argv[1] if len(sys.argv) > 1 else "assets"
OUTDIR = sys.argv[2] if len(sys.argv) > 2 else "gen"

os.makedirs(OUTDIR, exist_ok=True)


# ---------------------------------------------------------------- helpers
def rd(name):
    with open(os.path.join(SRCDIR, name), "rb") as f:
        return f.read()


def tp_real(b):
    """Turbo Pascal 6-byte real -> float."""
    exp = b[0]
    if exp == 0:
        return 0.0
    mant = int.from_bytes(b[1:6], "little")
    sign = -1.0 if mant & 0x8000000000 else 1.0
    mant = (mant & 0x7FFFFFFFFF) | 0x8000000000
    return sign * mant * 2.0 ** (exp - 129 - 39)


def prob16(p):
    """probability 0..1 -> 16-bit fixed (compare with rand16())"""
    v = int(round(p * 65536))
    return min(v, 65535)


class Reader:
    def __init__(self, data):
        self.d = data
        self.p = 0

    def u8(self):
        v = self.d[self.p]
        self.p += 1
        return v

    def i16(self):
        v = struct.unpack_from("<h", self.d, self.p)[0]
        self.p += 2
        return v

    def bytes(self, n):
        v = self.d[self.p:self.p + n]
        self.p += n
        return v

    def eof(self):
        return self.p >= len(self.d)


# ------------------------------------------------------------ enemy kinds
ENM_NUM = 19  # kinds 0..18
enm = rd("xquest.enm")
assert len(enm) == ENM_NUM * 109
kinds = []
for i in range(ENM_NUM):
    r = enm[i * 109:(i + 1) * 109]
    (speed, speed2, curve, curve2, hits, firetype,
     score, deathsound) = struct.unpack_from("<8h", r, 0)
    (fires, follows, curves, explodes, laysmines, shootback, zoom,
     maxspeed, rebounds, tribbles, repulses) = r[16:27]
    fireprob = tp_real(r[27:33])
    changedir = tp_real(r[33:39])
    changecurve = tp_real(r[39:45])
    follow = tp_real(r[45:51])
    width, height, bmwidth, numframes, framespeed = \
        struct.unpack_from("<5h", r, 99)
    kinds.append(dict(speed=speed, speed2=speed2, curve=curve, curve2=curve2,
                      hits=hits, firetype=firetype, score=score,
                      deathsound=deathsound, fires=fires, follows=follows,
                      curves=curves, explodes=explodes, laysmines=laysmines,
                      shootback=shootback, zoom=zoom, maxspeed=maxspeed,
                      rebounds=rebounds, tribbles=tribbles, repulses=repulses,
                      fireprob=fireprob, changedir=changedir,
                      changecurve=changecurve, follow=follow,
                      numframes=numframes, framespeed=framespeed))

# ------------------------------------------------------------------- GFX
gfx = Reader(rd("xquest.gfx"))
sprites = []          # list of (name, width, height, pixels(list rows of bmwidth*4), want_mask)


def read_bitmap(name, want_mask):
    w = gfx.i16()
    h = gfx.i16()
    bmw = ((w - 1) // 4 + 1) * 4      # row stride, multiple of 4
    px = gfx.bytes(bmw * h)
    sprites.append((name, w, h, bmw, px, want_mask))
    return len(sprites) - 1


MAXSHIPPICS = 24
ship_pics = [read_bitmap(f"ship{i}", True) for i in range(MAXSHIPPICS)]
miss_pic = read_bitmap("shipmiss", True)
obj_pics = [read_bitmap(f"obj{i}", True) for i in range(3)]  # crys, mine, smart
emine_pic = read_bitmap("emine", True)
enemy_pics = []
for k in range(ENM_NUM):
    frames = []
    for f in range(kinds[k]["numframes"] + 1):
        frames.append(read_bitmap(f"enemy{k}_{f}", True))
    enemy_pics.append(frames)
emiss_pics = [read_bitmap(f"emiss{i}", True) for i in range(1, 7)]
# plain PBMs
hud_ship = read_bitmap("hudship", False)
hud_smart = read_bitmap("hudsmart", False)
hud_crys = read_bitmap("hudcrys", False)
powerup_pics = [read_bitmap(f"powerup{i}", False) for i in range(7)]
gate_pics = [read_bitmap("gateL", True), read_bitmap("gateR", True)]
corner_pics = [read_bitmap(n, False) for n in ("cTL", "cTR", "cBR", "cBL")]
lgate_pics = [read_bitmap(f"lgate{i}", False) for i in range(6)]
rgate_pics = [read_bitmap(f"rgate{i}", False) for i in range(6)]
attractor_pic = read_bitmap("attractor", False)
digit_pics = [read_bitmap(f"digit{i}", False) for i in range(10)]
assert gfx.eof(), f"gfx not fully consumed: {gfx.p}/{len(gfx.d)}"

# ------------------------------------------------------------------ fonts
# xquest.fnt : 40 entries, 116 bytes each: w(2) h(2) data(112) 8x14
fnt = rd("xquest.fnt")
assert len(fnt) % 116 == 0
font1 = []
for i in range(len(fnt) // 116):
    w, h = struct.unpack_from("<hh", fnt, i * 116)
    data = fnt[i * 116 + 4: i * 116 + 4 + 112]
    font1.append((w, h, data))

# xquest2.fnt : comix font: byte code, w(2), h(2), h rows of w bytes
fnt2 = Reader(rd("xquest2.fnt"))
font2 = {}
while not fnt2.eof():
    code = fnt2.u8()
    w = fnt2.i16()
    h = fnt2.i16()
    data = fnt2.bytes(w * h)
    font2[code] = (w, h, data)

# --------------------------------------------------------------- palette
pal_txt = open(os.path.join(SRCDIR, "palette.inc")).read()
pal_txt = re.sub(r"\{[^}]*\}", "", pal_txt)      # strip Pascal comments
nums = [int(x) for x in re.findall(r"-?\d+", pal_txt)]
# skip array bounds (0,769) and header entries (start colour, count)
pal = nums[4:4 + 256 * 3]
assert len(pal) == 768
assert pal[0] == 0 and pal[1] == 0 and pal[2] == 0, pal[:6]


def cram(r6, g6, b6):
    # VGA 6-bit -> 32X 5-bit BGR
    r = (r6 * 31 + 31) // 63
    g = (g6 * 31 + 31) // 63
    b = (b6 * 31 + 31) // 63
    return (b << 10) | (g << 5) | r


cram_pal = [cram(pal[i * 3], pal[i * 3 + 1], pal[i * 3 + 2])
            for i in range(256)]

# ------------------------------------------------------------------ title
# title.pbm: two XLIB planar PBM halves (320x120 each): bmwidth/4(1) h(1)
# then 4 planes of (bmwidth x h) bytes, plane-major.
tdat = rd("title.pbm")
half = len(tdat) // 2


def decode_planar(dat):
    w4, h = dat[0], dat[1]
    px = dat[2:2 + w4 * 4 * h]
    img = bytearray(w4 * 4 * h)
    for p in range(4):
        for y in range(h):
            base = p * w4 * h + y * w4
            for c in range(w4):
                img[y * w4 * 4 + c * 4 + p] = px[base + c]
    return w4 * 4, h, bytes(img)


tw, th1, top = decode_planar(tdat[:half])
tw2, th2, bot = decode_planar(tdat[half:])
assert tw == 320 and tw2 == 320, (tw, tw2)
title_px = top + bot
th = th1 + th2

# title palette from titlemap.inc
tpal_txt = open(os.path.join(SRCDIR, "titlemap.inc")).read()
tpal_txt = re.sub(r"\{[^}]*\}", "", tpal_txt)
tnums = [int(x) for x in re.findall(r"-?\d+", tpal_txt)]
tpal = tnums[4:4 + 256 * 3]
title_cram = [cram(tpal[i * 3], tpal[i * 3 + 1], tpal[i * 3 + 2])
              for i in range(256)]


# ------------------------------------------------------------------ sound
snd = Reader(rd("xquest.snd"))
sounds = []
for i in range(25):
    ln = struct.unpack_from("<H", snd.d, snd.p)[0]
    snd.p += 2
    sounds.append(snd.bytes(ln))

# ---------------------------------------------------------- level tables
vars_txt = open(os.path.join(SRCDIR, "xqvars.pas")).read()

# probs table
m = re.search(r"probs:array\[1\.\.maxlevel,0\.\.maxenemykinds\] of byte=(.*?)\);\s",
              vars_txt, re.S)
probs_body = re.sub(r"\{[^}]*\}", "", m.group(1))   # strip Pascal comments
prob_nums = [int(x) for x in re.findall(r"\d+", probs_body)]
assert len(prob_nums) == 50 * 19, len(prob_nums)

# levels table
lv = re.findall(
    r"numcryst:(\d+);nummine:(\d+);maxsmart:(\d+);smartprob:([\d.]+);"
    r"newman:(\d+);\s*maxenemies:(\d+);erelease:([\d.]+);GateWidth:(\d+);"
    r"GateMove:(\d+);\s*GateChangeDirProb:([\d.]+);Time:(\d+);BonusLevel:(\w+)",
    vars_txt)
assert len(lv) == 50, len(lv)


# ------------------------------------------------------------- emit C
def rows_c(px, bmw, h):
    out = []
    for y in range(h):
        out.append(",".join(str(b) for b in px[y * bmw:(y + 1) * bmw]))
    return ",\n".join(out)


def mask_of(px, w, h, bmw):
    """32-bit row masks, bit 31 = leftmost pixel (matches original makemask)"""
    masks = []
    for y in range(h):
        m0 = 0
        for x in range(bmw):
            if px[y * bmw + x] != 0:
                m0 |= 1 << (31 - x)
        masks.append(m0)
    return masks


hdr = []
src = []
src.append('#include "assets.h"\n')

hdr.append("#ifndef ASSETS_H\n#define ASSETS_H\n")
hdr.append("#include <stdint.h>\n")
hdr.append("""
typedef struct {
    int16_t w, h, stride;
    const uint8_t *px;      /* stride*h bytes, 0 = transparent */
    const uint32_t *mask;   /* h row masks or 0 */
} sprite_t;
""")

# sprites
for idx, (name, w, h, bmw, px, want_mask) in enumerate(sprites):
    src.append(f"static const uint8_t px_{name}[] = {{\n{rows_c(px, bmw, h)} }};\n")
    if want_mask:
        masks = mask_of(px, w, h, bmw)
        src.append(f"static const uint32_t mk_{name}[] = {{ " +
                   ",".join(f"0x{v:08x}u" for v in masks) + " };\n")
src.append("const sprite_t gfx_sprites[] = {\n")
for name, w, h, bmw, px, want_mask in sprites:
    mk = f"mk_{name}" if want_mask else "0"
    src.append(f"  {{ {w}, {h}, {bmw}, px_{name}, {mk} }},\n")
src.append("};\n")

hdr.append(f"#define GFX_COUNT {len(sprites)}\n")
hdr.append("extern const sprite_t gfx_sprites[];\n")


def def_idx(n, v):
    hdr.append(f"#define {n} {v}\n")


def_idx("GFX_SHIP0", ship_pics[0])
def_idx("GFX_SHIPMISS", miss_pic)
def_idx("GFX_OBJ0", obj_pics[0])
def_idx("GFX_EMINE", emine_pic)
def_idx("GFX_EMISS1", emiss_pics[0])
def_idx("GFX_HUDSHIP", hud_ship)
def_idx("GFX_HUDSMART", hud_smart)
def_idx("GFX_HUDCRYS", hud_crys)
def_idx("GFX_POWERUP0", powerup_pics[0])
def_idx("GFX_GATEL", gate_pics[0])
def_idx("GFX_GATER", gate_pics[1])
def_idx("GFX_CTL", corner_pics[0])
def_idx("GFX_CTR", corner_pics[1])
def_idx("GFX_CBR", corner_pics[2])
def_idx("GFX_CBL", corner_pics[3])
def_idx("GFX_LGATE0", lgate_pics[0])
def_idx("GFX_RGATE0", rgate_pics[0])
def_idx("GFX_ATTRACTOR", attractor_pic)
def_idx("GFX_DIGIT0", digit_pics[0])

# enemy frame index table
src.append("const int16_t enemy_frame_base[] = { " +
           ",".join(str(enemy_pics[k][0]) for k in range(ENM_NUM)) + " };\n")
hdr.append("extern const int16_t enemy_frame_base[];\n")

# enemy kinds
hdr.append("""
typedef struct {
    int16_t speed, speed2, curve, curve2, hits, firetype, score, deathsound;
    uint8_t fires, follows, curves, explodes, laysmines, shootback, zoom,
            maxspeed, rebounds, tribbles, repulses;
    uint16_t fireprob, changedir, changecurve, follow;  /* 16-bit fixed prob */
    int16_t width, height, stride, numframes, framespeed;
} enemykind_t;
#define MAXENEMYKINDS 18
extern const enemykind_t enemykind[MAXENEMYKINDS+1];
""")
src.append("const enemykind_t enemykind[] = {\n")
for k, kd in enumerate(kinds):
    sp = sprites[enemy_pics[k][0]]
    src.append("  { %d,%d,%d,%d,%d,%d,%d,%d, "
               "%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d, "
               "%d,%d,%d,%d, %d,%d,%d,%d,%d },\n" % (
                   kd["speed"], kd["speed2"], kd["curve"], kd["curve2"],
                   kd["hits"], kd["firetype"], kd["score"], kd["deathsound"],
                   kd["fires"], kd["follows"], kd["curves"], kd["explodes"],
                   kd["laysmines"], kd["shootback"], kd["zoom"],
                   kd["maxspeed"], kd["rebounds"], kd["tribbles"],
                   kd["repulses"],
                   prob16(kd["fireprob"]), prob16(kd["changedir"]),
                   prob16(kd["changecurve"]), prob16(kd["follow"]),
                   sp[1], sp[2], sp[3], kd["numframes"], kd["framespeed"]))
src.append("};\n")

# level table
hdr.append("""
typedef struct {
    uint8_t numcryst, nummine, maxsmart;
    uint16_t smartprob;         /* 16-bit fixed */
    int32_t newman;
    uint8_t maxenemies;
    uint16_t erelease;          /* 16-bit fixed */
    int16_t gatewidth, gatemove;
    uint16_t gatechangedirprob; /* 16-bit fixed */
    uint16_t time;              /* seconds */
} level_t;
#define MAXLEVEL 50
extern const level_t levels[MAXLEVEL];
extern const uint8_t probs[MAXLEVEL][19];
""")
src.append("const level_t levels[] = {\n")
for e in lv:
    src.append("  { %s,%s,%s,%d,%s,%s,%d,%s,%s,%d,%s },\n" % (
        e[0], e[1], e[2], prob16(float(e[3])), e[4], e[5],
        prob16(float(e[6])), e[7], e[8], prob16(float(e[9])), e[10]))
src.append("};\n")
src.append("const uint8_t probs[50][19] = {\n")
for i in range(50):
    row = prob_nums[i * 19:(i + 1) * 19]
    src.append("  {" + ",".join(str(x) for x in row) + "},\n")
src.append("};\n")

# palette
src.append("const uint16_t game_palette[256] = {\n" +
           ",".join(f"0x{v:04x}" for v in cram_pal) + " };\n")
hdr.append("extern const uint16_t game_palette[256];\n")

# fonts
src.append("const uint8_t font1_px[][112] = {\n")
for w, h, data in font1:
    src.append("  {" + ",".join(str(b) for b in data) + "},\n")
src.append("};\n")
hdr.append(f"#define FONT1_COUNT {len(font1)}\n")
hdr.append("extern const uint8_t font1_px[][112];\n")  # 8x14 each

# fontmap (ASCII -> font1 index+1, 0 = blank) from xqvars.pas
fm = re.search(r"fontmap:array\[#1\.\.#128\] of byte=.*?\((.*?)\);",
               vars_txt, re.S)
fmap = [int(x) for x in re.findall(r"\d+", fm.group(1))]
assert len(fmap) == 128
src.append("const uint8_t fontmap[129] = { 0," +
           ",".join(str(x) for x in fmap) + " };\n")
hdr.append("extern const uint8_t fontmap[129];\n")

# comix font
codes = sorted(font2.keys())
src.append("static const uint8_t font2_data[] = {\n")
offs = {}
o = 0
blob = []
for c in codes:
    w, h, data = font2[c]
    offs[c] = o
    blob.extend(data)
    o += len(data)
src.append(",".join(str(b) for b in blob) + " };\n")
src.append("const comixchar_t font2[128] = {\n")
for c in range(128):
    if c in font2:
        w, h, data = font2[c]
        src.append(f"  {{ {w},{h}, font2_data+{offs[c]} }},\n")
    else:
        src.append("  { 0,0,0 },\n")
src.append("};\n")
hdr.append("""
typedef struct { int16_t w, h; const uint8_t *px; } comixchar_t;
extern const comixchar_t font2[128];
""")

# title picture
src.append(f"const int16_t title_w = {tw}, title_h = {th};\n")
src.append("const uint8_t title_px[] = {\n")
for y in range(th):
    src.append(",".join(str(b) for b in title_px[y * tw:(y + 1) * tw]) + ",\n")
src.append("};\n")
src.append("const uint16_t title_palette[256] = {\n" +
           ",".join(f"0x{v:04x}" for v in title_cram) + " };\n")
hdr.append("extern const int16_t title_w, title_h;\nextern const uint8_t title_px[];\n")
hdr.append("extern const uint16_t title_palette[256];\n")

# sounds (8-bit unsigned PCM ~11kHz)
hdr.append(f"#define SND_COUNT {len(sounds)}\n")
hdr.append("typedef struct { const uint8_t *data; uint32_t len; } sound_t;\n")
hdr.append("extern const sound_t sounds[SND_COUNT];\n")
for i, s in enumerate(sounds):
    src.append(f"static const uint8_t snd{i}[] = {{\n")
    for j in range(0, len(s), 32):
        src.append(",".join(str(b) for b in s[j:j + 32]) + ",\n")
    src.append("};\n")
src.append("const sound_t sounds[] = {\n")
for i, s in enumerate(sounds):
    src.append(f"  {{ snd{i}, {len(s)} }},\n")
src.append("};\n")

hdr.append("#endif\n")

with open(os.path.join(OUTDIR, "assets.c"), "w") as f:
    f.write("".join(src))
with open(os.path.join(OUTDIR, "assets.h"), "w") as f:
    f.write("".join(hdr))

print(f"OK: {len(sprites)} sprites, {len(sounds)} sounds, "
      f"{len(font1)} font1 glyphs, {len(font2)} comix glyphs, title {tw}x{th}")
