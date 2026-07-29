#!/bin/bash
# XQuest 32X automated test suite.
# Runs the ROM headless in PicoDrive (libretro core) and executes
# point-to-point scripts with black-screen / freeze assertions.
set -u

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(dirname "$HERE")"
CORE="${CORE:-/home/user/tools/picodrive/picodrive_libretro.so}"
ROM="${ROM:-$ROOT/build/xquest32x.32x}"
RUNNER="$HERE/runner"

mkdir -p "$HERE/artifacts"
cd "$ROOT"

fail=0

echo "=============================================="
echo " XQuest 32X test suite"
echo " core: $CORE"
echo " rom : $ROM"
echo "=============================================="

if [ ! -f "$ROM" ]; then
    echo "FAIL: ROM not found (build it with 'make')"
    exit 1
fi

# --- static ROM sanity checks -----------------------------------------
echo "--- test 0: ROM header sanity"
python3 - "$ROM" <<'EOF'
import struct, sys
data = open(sys.argv[1], 'rb').read()
ok = True
def check(cond, msg):
    global ok
    print(("PASS: " if cond else "FAIL: ") + msg)
    if not cond: ok = False
check(data[0x100:0x104] == b'SEGA', "TMSS signature 'SEGA' at 0x100")
check(data[0x3C0:0x3CA] == b'XQUEST 32X', "Mars module name at 0x3C0")
check(len(data) % 65536 == 0, "ROM padded to 64K multiple")
s = 0
for i in range(0x200, len(data)-1, 2):
    s = (s + struct.unpack_from('>H', data, i)[0]) & 0xFFFF
check(struct.unpack_from('>H', data, 0x18E)[0] == s, "header checksum matches")
sys.exit(0 if ok else 1)
EOF
[ $? -ne 0 ] && fail=1

run_script() {
    local name="$1" script="$2"
    echo "--- test: $name"
    if "$RUNNER" "$CORE" "$ROM" "$script"; then
        echo "--- $name: OK"
    else
        echo "--- $name: FAILED"
        fail=1
    fi
}

run_script "smoke (no black screen on boot)" "$HERE/smoke.script"
run_script "point-to-point gameplay"         "$HERE/p2p_gameplay.script"
run_script "soak (2 min emulated gameplay)"  "$HERE/soak.script"
run_script "PWM audio (sound effects)"       "$HERE/audio.script"

echo "--- test: ship facing direction"
if CORE="$CORE" ROM="$ROM" python3 "$HERE/check_facing.py"; then
    echo "--- ship facing direction: OK"
else
    echo "--- ship facing direction: FAILED"
    fail=1
fi

echo "--- test: pad controllability (emulator)"
if CORE="$CORE" ROM="$ROM" python3 "$HERE/check_controls.py"; then
    echo "--- pad controllability: OK"
else
    echo "--- pad controllability: FAILED"
    fail=1
fi

echo "--- test: missile velocity (emulator)"
if CORE="$CORE" ROM="$ROM" python3 "$HERE/check_shots.py"; then
    echo "--- missile velocity: OK"
else
    echo "--- missile velocity: FAILED"
    fail=1
fi

echo "--- test: shot physics (host, ASan/UBSan)"
if [ -x "$HERE/host_shot" ] && "$HERE/host_shot"; then
    echo "--- shot physics: OK"
else
    echo "--- shot physics: FAILED (or not built: gcc -fsanitize=address,undefined -o tests/host_shot tests/host_shot.c src/render.c gen/assets.c -Isrc -Igen -lm)"
    fail=1
fi

echo "--- test: control physics (host, ASan/UBSan)"
if [ -x "$HERE/host_ctrl" ] && "$HERE/host_ctrl"; then
    echo "--- control physics: OK"
else
    echo "--- control physics: FAILED (or not built: gcc -fsanitize=address,undefined -o tests/host_ctrl tests/host_ctrl.c src/render.c gen/assets.c -Isrc -Igen)"
    fail=1
fi

echo "=============================================="
if [ $fail -eq 0 ]; then
    echo " ALL TESTS PASSED"
else
    echo " SOME TESTS FAILED"
fi
echo "=============================================="
exit $fail
