# 03 — Hardware Notes

The DMG and CGB hardware behaviors this emulator implements, subsystem
by subsystem, with the reasoning behind each. Terminology follows
[Pan Docs](https://gbdev.io/pandocs/).

**Contents**

1. [CPU (LR35902)](#1-cpu-lr35902)
2. [Interrupts](#2-interrupts)
3. [HALT and STOP](#3-halt-and-stop)
4. [Timer](#4-timer)
5. [PPU (Graphics)](#5-ppu-graphics)
6. [APU (Audio)](#6-apu-audio)
7. [Memory Map & I/O Semantics](#7-memory-map--io-semantics)
8. [Cartridges & MBCs](#8-cartridges--mbcs)
9. [Serial Port](#9-serial-port)
10. [Power-On State](#10-power-on-state)
11. [Game Boy Color (CGB)](#11-game-boy-color-cgb)

---

## 1. CPU (LR35902)

### 1.1 Instruction set
All 512 opcodes (256 base + 256 CB-prefixed) are implemented, including
the 11 illegal opcodes, which execute as (multi-cycle) NOPs like on
real hardware.

### 1.2 Cycle accounting
Cycle counts are M-cycle exact and verified by blargg's `instr_timing`:

| Instruction class | Timing |
|---|---|
| `LD r,r` / ALU `A,r` / most 1-byte ops | 1 M-cycle |
| `LD r,(HL)` / ALU `A,(HL)` | 2 M-cycles |
| `PUSH` / `POP` | 4 / 3 M-cycles |
| `JP nn` taken | 4; `JP cc` 4 taken / 3 not taken |
| `JR` | 3 taken / 2 not taken |
| `CALL nn` / `CALL cc` taken | 6 / not taken 3 |
| `RET` / `RETI` | 4; `RET cc` 5 taken / **2 not taken** |
| `RST` | 4 |
| Interrupt dispatch | 5 |

(`RET cc` not-taken costing a full 2 M-cycles matters: blargg's delay
macros rely on it and failed when it was wrong.)

### 1.3 EI delay
`EI` sets a pending flag; IME becomes set only after the *instruction
following EI* executes, so an interrupt can never preempt the
instruction directly after `EI`. This is implemented by resolving the
pending flag at the top of `CPU::step()` and checking interrupt
dispatch at the bottom.

### 1.4 DAA
The decimal adjust uses the documented two-case algorithm (addition
vs. subtraction, driven by the N flag, with H/C assists) and passes
blargg's `01-special`.

## 2. Interrupts

- Priority: VBlank > LCD STAT > Timer > Serial > Joypad, matching the
  hardware vector order (0x40, 0x48, 0x50, 0x58, 0x60).
- Dispatch takes 5 M-cycles total: two stack-write cycles are charged
  by the `push16`, three internal cycles are added explicitly.
- The dispatch clears only the serviced bit in IF; other pending bits
  survive.
- IF bits 5–7 always read as 1; writes are masked to the low 5 bits.

## 3. HALT and STOP

### 3.1 HALT wake and dispatch
The CPU sleeps while `(IF & IE) == 0` — **regardless of IME**; IME only
gates whether the interrupt is *dispatched*. The crucial correctness
detail: when HALT wakes with IME set, the interrupt dispatches
*immediately*, pushing the address of the instruction after HALT.
Dispatching one instruction late pushes the wrong resume address and
fails blargg's `02-interrupts` — this was a real bug here once.

### 3.2 The HALT bug
`HALT` with IME=0 and `(IF & IE) != 0` doesn't halt at all — the byte
after HALT is fetched with the PC failing to increment, so it executes
twice (with operand fetches overlapping the opcode byte for multi-byte
instructions). This is implemented with an explicit `halt_bug` flag and
is verified by the `halt_bug.gb` survey ROM (see
[05-Accuracy §2.5](05-ACCURACY.md)).

### 3.3 STOP
STOP parks the CPU until a joypad press sets IF bit 4; the second byte
of the instruction is skipped. While stopped, the **system counter is
frozen**: the emulator's M-cycle hook skips timer ticks, so DIV and
TIMA stop advancing on both models (Pan Docs: the system counter
increments every M-cycle "unless the CPU is in STOP mode").

On CGB, STOP after writing 1 to KEY1 ($FF4D bit 0) is the documented
speed-switch sequence: the CPU pauses ~2050 M-cycles with the clock
frozen, then the CPU/timer/serial/OAM-DMA domain doubles while the PPU
and APU keep their 4.19 MHz timing. The switch is consumed exactly
once and KEY1 bit 0 clears automatically. On DMG there is no KEY1 and
STOP never switches speed.

Hardware nuance not emulated: on DMG the LCD turns off during STOP
(and the screen state during the CGB switch pause depends on the PPU
mode the switch started in); the emulator keeps rendering.

## 4. Timer

The timer implements the real DMG circuit from Pan Docs / Gekkio's
analysis rather than the simplified "counter per rate" model:

- A 16-bit **system counter** increments every T-cycle.
- `DIV` (FF04) is the **upper byte** of that counter; writing DIV
  resets the whole counter.
- `TIMA` increments on each **falling edge** of the counter bit
  selected by TAC: bit 9 / 3 / 5 / 7 for TAC codes 0 / 1 / 2 / 3
  (4096 / 262144 / 65536 / 16384 Hz).
- Because the edge detector watches a *bit of the counter*, writing
  DIV (or changing TAC's select bits) while the watched bit is high
  produces a spurious TIMA increment — emulated, since games and test
  ROMs rely on it.
- On TIMA overflow, TIMA reads 0 for one M-cycle, reloads from TMA on
  the next, and the interrupt is requested two M-cycles after the
  overflow. A TIMA write during the delay window cancels both.

DIV reads 0xAB immediately after the boot ROM, per the DMG power-up
state.

## 5. PPU (Graphics)

### 5.1 Line timing
Mode 2 (OAM) 80 T-cycles, mode 3 (transfer) 172, mode 0 (HBlank) 204;
456 per line, 154 lines, 70224 per frame. Lines render at the end of
mode 3 using the register values at that moment — mid-frame register
changes take effect per line, which is exactly what dmg-acid2's
LYC-choreographed writes require.

### 5.2 STAT interrupt line ("blocking")
The STAT interrupt source is the OR of four conditions:

```
line = (STAT.3 & mode==0) | (STAT.4 & mode==1)
     | (STAT.5 & mode==2) | (STAT.6 & LYC==LY)
```

The IF flag is requested only on a **rising edge** of that combined
line. Two subtleties implemented:

1. **LYC writes take effect immediately**: the coincidence flag is
   re-evaluated every M-cycle, not just when LY changes. dmg-acid2
   rewrites LYC from its STAT ISR while the old coincidence is still
   true; without per-cycle re-evaluation the line never falls and the
   next interrupt never fires.
2. The PPU owns STAT bits 0–2 and writes them through an internal
   setter, since the CPU-facing write path correctly masks bits 0–2 as
   read-only.

### 5.3 Window
- Visible when LCDC bit 5 is set, `WX <= 166` (with `WX=7` at the left
  edge), and the frame has reached `WY`.
- An **internal line counter** (not LY) tracks which window tile row to
  fetch; it increments once per scanline that actually rendered window
  pixels and survives mid-frame window hiding. dmg-acid2 hides the
  window for ~50 lines between the eye and the chin; the chin renders
  from map row 2 (`$9840`) as on hardware.
- On DMG, LCDC bit 0 gates **both** background and window (bit 0 clear
  paints color 0); sprites still render.

### 5.4 Sprites
- Hardware serves at most **10 sprites per scanline** — the first 10 in
  OAM order whose Y range covers the line (X does not matter for the
  limit).
- Among those 10, the pixel winner is the **smallest X**, ties broken
  by lower OAM index (not last-drawn-wins).
- OBJ-to-BG priority (attribute bit 7): the sprite only shows where the
  BG/window pixel is color 0.
- 8×16 sprites ignore bit 0 of the tile index; Y/X flips and both OBP
  palettes are honored.
- Sprite buffers are cleared every scanline even when sprites are
  disabled — otherwise a line rendered with sprites off shows the
  previous line's stale sprite pixels (a real bug found via
  dmg-acid2's LCDC choreography).

### 5.5 LCD off
Turning LCDC bit 7 off resets LY to 0, parks the PPU in mode 0, blanks
the framebuffer, and suppresses all STAT/VBlank activity. Re-enabling
restarts the frame from line 0.

## 6. APU (Audio)

All four channels plus the global circuit, following Pan Docs
"Audio Details":

### 6.1 Frame sequencer (DIV-APU, 512 Hz)
Step counter 0–7: length clocks at steps 0/2/4/6, CH1 sweep at 2/6,
envelope at 7. Length counters: 64 (pulse/noise), 256 (wave).

### 6.2 Pulse channels (CH1, CH2)
- Timer period `(2048 − period) × 4` T-cycles per duty step; duty
  patterns 12.5/25/50/75%.
- Trigger reloads the frequency timer but **preserves its low two
  bits**, resets the duty step, and reloads the envelope (period 0
  behaves as 8).
- CH1 sweep unit: 11-bit shadow register, sweep timer clocked at
  128 Hz, enabled flag; trigger primes the shadow from the period
  register and performs an immediate overflow check when the shift is
  non-zero. Iteration: compute `shadow ± (shadow >> shift)`,
  overflow >2047 disables CH1, valid results are written back to the
  shadow *and* NR13/NR14, then a second (non-written) overflow check
  runs.

### 6.3 Wave channel (CH3)
- 32 × 4-bit samples in FF30–FF3F, fetched upper nibble first.
- The channel emits the **last fetched sample** continuously; on
  trigger the position resets but the buffered sample plays until the
  next fetch (so triggering does not click).
- Output level codes 0/1/2/3 = mute / 100% / 50% / 25% (digital shift,
  pre-DAC).
- DAC is NR30 bit 7. Wave RAM reads 0xFF / ignores writes while the
  channel is playing (DMG behavior).

### 6.4 Noise channel (CH4)
- 15-bit LFSR, new bit = `NOT(lfsr0 XOR lfsr1)`, 7-bit short mode
  copies the new bit into bit 6; LFSR resets to 0 on trigger.
- Period = divisor `{8,16,32,48,64,80,96,112}[code] << shift` T-cycles;
  shift 14/15 stops clocking (channel freezes).
- Envelope identical to the pulse channels.

### 6.5 DACs, mixer, power
- A channel's DAC is on when `NRx2 & 0xF8 != 0` (CH3: `NR30.7`);
  DAC-off forces the channel off, but a channel-off with DAC-on still
  contributes "digital 0" bias — accurately.
- Digital 0–15 maps linearly to analog −1…+1 (inverted slope);
  NR51 pans each channel L/R; NR50 scales each side `(vol+1)/8`.
- Both outputs pass through the documented **high-pass filter**
  (charge factor ≈0.996 at 44.1 kHz) to remove DC bias.
- NR52 bit 7 powers the APU down: registers clear (wave RAM and
  length counters survive, per DMG), everything reads 0xFF except
  NR52 and wave RAM, and writes are ignored.

## 7. Memory Map & I/O Semantics

- Echo RAM (E000–FDFF) mirrors WRAM; FEA0–FEFF is unusable
  (reads 0xFF, writes dropped).
- OAM DMA (FF46) performs the 160-byte copy immediately.
- Register read/write masking, per Pan Docs power-on values:
  - `IF`: writes masked to bits 0–4, reads OR 0xE0
  - `STAT`: CPU writes only bits 3–6; bit 7 reads 1; bits 0–2 PPU-owned
  - `LY`: read-only (writes ignored)
  - `TAC`: only bits 0–2 stored; upper bits read 1
  - `NR10–NR44`: per-register read masks matching documented power-on
    values (e.g. NR13 reads 0xFF, NR11 bits 0–5 read 1, ...)

## 8. Cartridges & MBCs

### 8.1 MBC1 (full banking model)
- 5-bit ROM bank register (0 translated to 1 on the low 5 bits only),
  2-bit secondary register, 1-bit mode select.
- Mode 0: 0000-3FFF locked to bank 0, RAM locked to bank 0.
- Mode 1: 0000-3FFF maps to `bank2 << 5` (banks 0/20/40/60 — the only
  way to reach banks 20/40/60 at all), and RAM banks via the 2-bit
  register.
- 4000-7FFF always maps `(bank2 << 5) | low5` in both modes.

### 8.2 MBC2
512 × 4-bit RAM is always present regardless of the header's RAM size
code; control is selected by address bit 8; RAM nibble reads are masked
to 4 bits.

### 8.3 MBC3 (with RTC)
- 7-bit ROM bank, banks 20/40/60 directly reachable.
- RAM bank register 0x08–0x0C selects the five RTC registers
  (seconds, minutes, hours, days low, days high).
- Registers hold the **latched** copies; a 0x00→0x01 pulse on
  6000-7FFF latches the live clock.
- The live clock ticks at 32768 T-cycles per second unless halted
  (days-high bit 6); day carry (bit 7) sets at the 512-day rollover.
- Latched-time semantics: games write the registers (halted), then
  latch to read back.

### 8.4 MBC5
9-bit ROM bank (low 8 bits via 2000-2FFF, bit 8 via 3000-3FFF —
no 0→1 translation), 4-bit RAM bank; rumble cartridge types (1C–1E)
mask the motor bit out of the RAM bank number.

### 8.5 Battery saves
Battery types (03, 06, 09, 0F, 10, 13, 1B, 1E) persist cartridge RAM —
plus the RTC registers and sub-second counter for MBC3 timers — to
`<rom>.sav` in a `GBSAV01`-tagged format, loaded on ROM load and
written on exit/savestate.

## 9. Serial Port

Writing SC with bit 7 set and the internal clock selected (SC bit 0)
performs an instant transfer with no peer connected: the byte in SB is
delivered to the host (stdout in headless mode; test ROMs print their
results this way), SB reads back the shifted-in 0xFF, SC's start bit
clears, and the serial interrupt is requested — matching what a
link-less DMG does with the standard `SC=$81` idiom. With the
external clock selected (SC bit 0 clear) the transfer never completes,
like a slave Game Boy waiting for a master that is not there. On CGB,
SC bit 1 (fast clock) is preserved through the transfer.

## 10. Power-On State

The emulator starts in the documented post-boot state. DMG: CPU
registers `A=01 F=B0 B=00 C=13 D=00 E=D8 H=01 L=4D SP=FFFE PC=0100`,
`DIV=0xAB`, `IF=0xE1` (VBlank pending), `LCDC=0x91`, `STAT=0x85`, and
the DMG audio register power-on values. CGB (cartridge header flag
$0143 has bit 7 set, or the `--dmg` flag is absent): CPU registers
`A=11 F=80 B=00 C=00 D=FF E=56 H=00 L=0D SP=FFFE PC=0100`, `SC=$7F`,
`DMA=$00`, `KEY1=$7E`, `VBK=$FE`, `HDMA1-5=$FF`, `RP=$3E`, `SVBK=$F8`,
and background palette RAM initialized to white ($FF bytes; bit 15 of
each RGB555 color is ignored). Object palette RAM is uninitialized on
hardware; the emulator fills it with $FF deterministically. No boot ROM
is executed.

## 11. Game Boy Color (CGB)

Cartridges flagged CGB ($0143 = $80 dual or $C0 CGB-only) boot with the
CGB register file active; everything else boots as a DMG and the CGB
registers read $FF and ignore writes. `--dmg` forces DMG mode.

**VRAM banking (VBK $FF4F)** — 16 KiB VRAM in two 8 KiB banks. CPU
accesses at 8000-9FFF go through the selected bank; rendering fetches
tile data from either bank (BG map attribute bit 3 for the background,
OAM attribute bit 3 for sprites) and BG map attributes from bank 1 at
the same addresses as the tile-number map in bank 0.

**BG map attributes (VRAM bank 1)** — per tile: palette (bits 0-2),
VRAM bank (bit 3), X flip (bit 5), Y flip (bit 6), BG-over-OBJ
priority (bit 7). Bit 4 is readable/writable but ignored by hardware.

**WRAM banking (SVBK $FF70)** — 32 KiB WRAM in eight 4 KiB banks;
bank 0 stays at C000-CFFF, banks 1-7 map at D000-DFFF (writing 0
selects bank 1), echo region included.

**Palette RAM (BGPI/BGPD $FF68-69, OBPI/OBPD $FF6A-6B)** — 64 bytes
per palette bank, two little-endian bytes per RGB555 color. Writes
auto-increment the 6-bit index (wrapping 63 to 0) when BGPI/OBPI bit 7
is set — even when the write is blocked by the mode-3 CRAM lock;
reads never auto-increment. While the PPU reads palette RAM (mode 3),
reads return $FF and writes are dropped.

**BG/OBJ priority** — three flags decide each pixel (Pan Docs):
BG color index 0 always lets the OBJ through; LCDC bit 0 clear (CGB
meaning) makes OBJs win unconditionally; otherwise the OBJ wins only
if neither its OAM attribute bit 7 nor the tile's BG attribute bit 7
requests BG priority. Sprite-vs-sprite priority uses OAM index order
in CGB mode (X order when OPRI $FF6C bit 0 selects DMG style).

**HDMA/GDMA (HDMA1-5 $FF51-55)** — source 0000-DFF0 and destination
8000-9FF0, both nibble-aligned by masking on write. HDMA5 bit 6-0
length = blocks-1. GDMA (bit 7 clear on write) copies everything at
once; HBlank DMA (bit 7 set) moves one 16-byte block per HBlank of
lines 0-143 (never during VBlank), tracks the remaining length in the
FF55 readback (bit 7 clear = active), and writing bit 7 clear to FF55
cancels it while keeping the length readable. Destination overflow
stops the transfer prematurely. The VRAM destination bank is latched
at transfer start. Caveat: transfers are instantaneous here — the CPU
is not billed the hardware's ~8 M-cycles per block (see
[05-Accuracy §3](05-ACCURACY.md)).

**Double speed (KEY1 $FF4D)** — write bit 0, then STOP (see §3.3).
After the switch the CPU consumes M-cycles twice as fast; the PPU
advances 2 dots per M-cycle instead of 4 and the APU is ticked 2 base
T-cycles per M-cycle instead of 4, so video and audio timing are
unaffected. The timer's system counter still advances 4 T-cycles per
M-cycle, so DIV and TIMA frequencies double exactly as on hardware.

**Registers** — RP $FF56 (infrared; no IR hardware, so "receiving"
reads 1), PCM12/PCM34 $FF76-77 (live APU channel digital outputs,
low nibble = odd channel), KEY0 $FF4C and the undocumented $FF72-75
are readable/writable storage.

---
*Next: [04 — Testing](04-TESTING.md)*
