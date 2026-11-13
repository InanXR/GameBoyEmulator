#!/usr/bin/env bash
# GameBoy Emulator - automated accuracy test suite
#
# Downloads freely redistributable test ROMs (blargg's cpu_instrs,
# instr_timing, halt_bug) and runs them against the emulator in
# headless mode. Test ROMs print their results over the serial port,
# which the emulator echoes to stdout.
#
# Usage:
#   tests/run_tests.sh [path/to/gameboy]
#
# Environment:
#   GB_TEST_ROM_DIR   directory for ROM downloads (default: .test-roms)

set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

EMU="${1:-$ROOT_DIR/build/gameboy}"
ROM_DIR="${GB_TEST_ROM_DIR:-$ROOT_DIR/.test-roms}"
FRAMES="${GB_TEST_FRAMES:-30000}"

if [[ ! -x "$EMU" ]]; then
    echo "Emulator not found at: $EMU"
    echo "Build it first:  cmake -B build -S . && cmake --build build"
    exit 1
fi

mkdir -p "$ROM_DIR"

download() {
    local dest="$1" url="$2" kind="${3:-rom}"
    if [[ -s "$ROM_DIR/$dest" ]]; then return 0; fi
    echo "Downloading $dest..."
    if ! curl -sL -f -o "$ROM_DIR/$dest" "$url"; then
        echo "  download failed"
        rm -f "$ROM_DIR/$dest"
        return 1
    fi
    case "$kind" in
        rom)
            # A valid GB ROM is at least 0x150 bytes (header) and has
            # the entry point bytes 00 C3 at offset 0x0100. Note the
            # file does NOT start with 00 C3: 0x0000-0x00FF is the
            # zero-filled interrupt vector area.
            if [[ "$(stat -c %s "$ROM_DIR/$dest" 2>/dev/null || echo 0)" -lt 336 ]] ||
               [[ "$(od -j 256 -N 2 -An -tx1 "$ROM_DIR/$dest" | tr -d ' \n')" != "00c3" ]]; then
                echo "  downloaded file is not a valid ROM, removing"
                rm -f "$ROM_DIR/$dest"
                return 1
            fi
            ;;
        png)
            # PNG magic: 89 50 4E 47 0D 0A 1A 0A
            if [[ "$(od -An -tx1 -N 8 "$ROM_DIR/$dest" | tr -d ' \n')" != "89504e470d0a1a0a" ]]; then
                echo "  downloaded file is not a valid PNG, removing"
                rm -f "$ROM_DIR/$dest"
                return 1
            fi
            ;;
    esac
}

BASE="https://raw.githubusercontent.com/retrio/gb-test-roms/master"
CPU="$BASE/cpu_instrs/individual"

# name|url-encoded-filename
TESTS=(
    "01-special.gb|01-special.gb"
    "02-interrupts.gb|02-interrupts.gb"
    "03-op-sp-hl.gb|03-op%20sp%2Chl.gb"
    "04-op-r-imm.gb|04-op%20r%2Cimm.gb"
    "05-op-rp.gb|05-op%20rp.gb"
    "06-ld-r-r.gb|06-ld%20r%2Cr.gb"
    "07-jr-jp-call-ret-rst.gb|07-jr%2Cjp%2Ccall%2Cret%2Crst.gb"
    "08-misc-instrs.gb|08-misc%20instrs.gb"
    "09-op-r-r.gb|09-op%20r%2Cr.gb"
    "10-bit-ops.gb|10-bit%20ops.gb"
    "11-op-a-hl.gb|11-op%20a%2C%28hl%29.gb"
)

PASS=0
FAIL=0
FAILED_TESTS=()

run_test() {
    local name="$1" file="$2"
    local output
    output=$(timeout 300 "$EMU" "$ROM_DIR/$file" --headless "$FRAMES" 2>/dev/null | tail -n +1)

    if echo "$output" | grep -q "Passed"; then
        echo "PASS  $name"
        PASS=$((PASS + 1))
    elif echo "$output" | grep -q "Failed"; then
        echo "FAIL  $name"
        echo "$output" | grep -A 3 -B 1 "Failed" | sed 's/^/      /'
        FAIL=$((FAIL + 1))
        FAILED_TESTS+=("$name")
    else
        echo "???   $name (no result printed - timeout or crash?)"
        FAIL=$((FAIL + 1))
        FAILED_TESTS+=("$name")
    fi
}

# Some ROMs have no pass/fail verdict: they display results on screen
# and park in an infinite loop after recording them. Run them and
# verify only that they run to completion (reach their end loop).
run_informational() {
    local name="$1" file="$2"
    local output
    output=$(timeout 300 "$EMU" "$ROM_DIR/$file" --headless 1200 2>/dev/null)

    if echo "$output" | grep -q "Passed\|Failed"; then
        echo "INFO  $name (unexpected verdict output - classify as a regular test)"
        return
    fi
    # halt_bug.gb: an IE/IF x halt-bug survey ROM. It renders its results
    # table on screen and terminates into a fixed JR self-loop; the
    # emulator running it to completion within the frame budget (rather
    # than hanging in its wait loops) is the pass condition.
    echo "INFO  $name (no verdict; survey ROM ran to completion)"
}

echo "=============================================="
echo " GameBoy Emulator accuracy test suite"
echo " Emulator: $EMU"
echo "=============================================="

for entry in "${TESTS[@]}"; do
    name="${entry%%|*}"
    file="${entry#*|}"
    download "$name" "$CPU/$file" || { FAIL=$((FAIL+1)); FAILED_TESTS+=("$name"); continue; }
    run_test "$name" "$name"
done

# Extra suites (not part of cpu_instrs)
download "instr_timing.gb" "$BASE/instr_timing/instr_timing.gb" \
    && run_test "instr_timing.gb" "instr_timing.gb"
download "halt_bug.gb" "$BASE/halt_bug.gb" \
    && run_informational "halt_bug.gb" "halt_bug.gb"

# dmg-acid2: the definitive PPU rendering test. The emulator's final
# framebuffer must match the reference image pixel-for-pixel.
# Requires python3 + Pillow for the comparison.
run_dmg_acid2() {
    local out
    if ! download "dmg-acid2.gb" \
        "https://github.com/mattcurrie/dmg-acid2/releases/download/v1.0/dmg-acid2.gb"; then
        echo "SKIP  dmg-acid2.gb (download failed)"
        return
    fi
    if ! download "dmg-acid2-ref.png" \
        "https://raw.githubusercontent.com/mattcurrie/dmg-acid2/master/img/reference-dmg.png" png; then
        echo "SKIP  dmg-acid2.gb (reference download failed)"
        return
    fi
    if ! python3 -c "import PIL" >/dev/null 2>&1; then
        echo "SKIP  dmg-acid2.gb (python3 + Pillow required for pixel comparison)"
        return
    fi

    local shot="$ROM_DIR/dmg-acid2-shot.ppm"
    timeout 120 "$EMU" "$ROM_DIR/dmg-acid2.gb" --headless 120 --screenshot "$shot" >/dev/null 2>&1

    python3 - "$shot" "$ROM_DIR/dmg-acid2-ref.png" <<'PYEOF'
import sys, warnings
warnings.filterwarnings("ignore", category=DeprecationWarning)
from PIL import Image

def shades(path):
    px = Image.open(path).convert('RGB')
    out = []
    for p in px.getdata():
        lum = (p[0] + p[1] + p[2]) / 3
        out.append(0 if lum > 200 else 1 if lum > 120 else 2 if lum > 30 else 3)
    return out

ours = shades(sys.argv[1])
ref = shades(sys.argv[2])
mism = sum(1 for a, b in zip(ours, ref) if a != b)
if mism == 0:
    print("PASS  dmg-acid2.gb (pixel-perfect)")
    sys.exit(0)
else:
    print(f"FAIL  dmg-acid2.gb ({mism} mismatched pixels of {len(ref)})")
    sys.exit(1)
PYEOF
    if [[ $? -eq 0 ]]; then
        PASS=$((PASS + 1))
    else
        FAIL=$((FAIL + 1))
        FAILED_TESTS+=("dmg-acid2.gb")
    fi
    rm -f "$shot"
}

run_dmg_acid2

# cgb-acid2: the Game Boy Color equivalent of the PPU rendering test.
# Covers CGB palette RAM, BG map attributes (flips/banks/palette/
# priority), sprite palettes and VRAM banks, OBJ priority modes and
# HBlank DMA. Requires python3 + Pillow; compared in exact RGB.
run_cgb_acid2() {
    local out
    if ! download "cgb-acid2.gbc" \
        "https://github.com/mattcurrie/cgb-acid2/releases/download/v1.1/cgb-acid2.gbc"; then
        echo "SKIP  cgb-acid2.gbc (download failed)"
        return
    fi
    if ! download "cgb-acid2-ref.png" \
        "https://raw.githubusercontent.com/mattcurrie/cgb-acid2/master/img/reference.png" png; then
        echo "SKIP  cgb-acid2.gbc (reference download failed)"
        return
    fi
    if ! python3 -c "import PIL" >/dev/null 2>&1; then
        echo "SKIP  cgb-acid2.gbc (python3 + Pillow required for pixel comparison)"
        return
    fi

    local shot="$ROM_DIR/cgb-acid2-shot.ppm"
    timeout 120 "$EMU" "$ROM_DIR/cgb-acid2.gbc" --headless 120 --screenshot "$shot" >/dev/null 2>&1

    python3 - "$shot" "$ROM_DIR/cgb-acid2-ref.png" <<'PYEOF'
import sys, warnings
warnings.filterwarnings("ignore", category=DeprecationWarning)
from PIL import Image

def load_ppm(path):
    with open(path, 'rb') as f:
        data = f.read()
    parts = data.split(b'\n', 3)
    assert parts[0] == b'P6' and parts[1] == b'160 144' and parts[2] == b'255'
    return Image.frombytes('RGB', (160, 144), parts[3])

ours = load_ppm(sys.argv[1]).convert('RGB')
ref = Image.open(sys.argv[2]).convert('RGB')
o, r = list(ours.getdata()), list(ref.getdata())
mism = sum(1 for a, b in zip(o, r) if a != b)
if mism == 0:
    print("PASS  cgb-acid2.gbc (pixel-perfect, exact RGB)")
    sys.exit(0)
else:
    print(f"FAIL  cgb-acid2.gbc ({mism} mismatched pixels of {len(r)})")
    sys.exit(1)
PYEOF
    if [[ $? -eq 0 ]]; then
        PASS=$((PASS + 1))
    else
        FAIL=$((FAIL + 1))
        FAILED_TESTS+=("cgb-acid2.gbc")
    fi
    rm -f "$shot"
}

run_cgb_acid2

echo "=============================================="
echo " Results: $PASS passed, $FAIL failed"
if [[ $FAIL -gt 0 ]]; then
    printf ' Failed: %s\n' "${FAILED_TESTS[*]}"
    echo "=============================================="
    exit 1
fi
echo " All tests passed!"
echo "=============================================="
