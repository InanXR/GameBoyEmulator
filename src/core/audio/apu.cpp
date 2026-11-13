#include "apu.h"
#include <iostream>
#include <cstring>
#include <cmath>

/**
 * APU Implementation (Pan Docs: Audio Registers / Audio Details)
 */

// Pulse duty patterns: bit i = output at duty step i
static const u8 DUTY_TABLE[4] = {
    0x01,  // 00000001 - 12.5%
    0x81,  // 10000001 - 25%
    0x87,  // 10000111 - 50%
    0x7E   // 01111110 - 75%
};

// Noise divisor table (T-cycles at shift=0); code 0 counts as 8.
static const int NOISE_DIVISOR[8] = { 8, 16, 32, 48, 64, 80, 96, 112 };

APU::APU()
    : powered(true)
    , fs_step(0)
    , fs_counter(0)
    , sample_accum(0)
    , user_volume(1.0f)
    , cap_left(0.0f)
    , cap_right(0.0f)
    , buffer_write_pos(0)
    , buffer_read_pos(0)
{
    powerOn();
}

void APU::reset() {
    // The SDL audio callback may be draining concurrently.
    std::lock_guard<std::mutex> lock(buffer_mutex);
    powerOn();
}

void APU::powerOn() {
    powered = true;
    fs_step = 0;
    fs_counter = 0;
    sample_accum = 0;
    user_volume = 1.0f;
    cap_left = 0.0f;
    cap_right = 0.0f;
    buffer_write_pos = 0;
    buffer_read_pos = 0;

    regs.fill(0);
    memset(&ch1, 0, sizeof(ch1));
    memset(&ch2, 0, sizeof(ch2));
    memset(&sweep, 0, sizeof(sweep));
    memset(&wave, 0, sizeof(wave));
    memset(&noise, 0, sizeof(noise));
    wave.sample_buffer = 0;
    memset(sample_buffer, 0, sizeof(sample_buffer));

    // Power-on register values (Pan Docs: Power-Up Sequence)
    regs[0x26 - 0x10] = 0xF1;
    regs[0x11 - 0x10] = 0xBF;
    regs[0x12 - 0x10] = 0xF3;
    regs[0x14 - 0x10] = 0xBF;
    regs[0x16 - 0x10] = 0x3F;
    regs[0x19 - 0x10] = 0xBF;
    regs[0x1A - 0x10] = 0x7F;
    regs[0x1B - 0x10] = 0xFF;
    regs[0x1C - 0x10] = 0x9F;
    regs[0x1E - 0x10] = 0xBF;
    regs[0x24 - 0x10] = 0x77;
    regs[0x25 - 0x10] = 0xF3;

    t_cycles_per_sample = 4194304.0 / SAMPLE_RATE;

    ch1.timer = 0;
    ch2.timer = 0;
    wave.timer = 0;
    noise.timer = NOISE_DIVISOR[0];
}

// ============================================
// PERIODS
// ============================================

int APU::pulsePeriod(const Pulse& ch, int base_off) const {
    (void)ch;
    u8 lo = regs[base_off - 0x10];       // NRx3
    u8 hi = regs[base_off + 4 - 0x10];  // NRx4
    int period = lo | ((hi & 0x07) << 8);
    return (2048 - period) * 4;  // T-cycles per duty step
}

int APU::wavePeriod() const {
    u8 lo = regs[0x1D - 0x10];
    u8 hi = regs[0x1E - 0x10];
    int period = lo | ((hi & 0x07) << 8);
    return (2048 - period) * 2;  // T-cycles per sample fetch
}

int APU::noisePeriod() const {
    u8 nr43 = regs[0x22 - 0x10];
    int shift = (nr43 >> 4) & 0x0F;
    int code = nr43 & 0x07;
    return NOISE_DIVISOR[code] << shift;  // 0 if shift >= 14 -> frozen
}

bool APU::pulseDAC(const Pulse& ch, int nr2_off) const {
    (void)ch;
    return (regs[nr2_off - 0x10] & 0xF8) != 0;
}

// ============================================
// MAIN TICK
// ============================================

void APU::tick(int m_cycles) {
    tickT(m_cycles * 4);
}

void APU::tickT(int t_cycles) {
    if (powered) {
        // Frame sequencer (512 Hz, 8192 T-cycles per step)
        fs_counter += t_cycles;
        while (fs_counter >= T_CYCLES_PER_FRAME_SEQ_STEP) {
            fs_counter -= T_CYCLES_PER_FRAME_SEQ_STEP;
            clockFrameSequencer();
        }

        tickPulse(ch1, 0x11, 0x14, t_cycles);
        tickPulse(ch2, 0x16, 0x19, t_cycles);
        tickWave(t_cycles);
        tickNoise(t_cycles);
    }

    // Resample to the output rate
    sample_accum += t_cycles;
    while (sample_accum >= t_cycles_per_sample) {
        sample_accum -= t_cycles_per_sample;
        generateSample();
    }
}

// Raw digital output (0-15) of one generation circuit, as tapped by
// the CGB's PCM12/PCM34 debug registers.
int APU::channelDigital(int ch) const {
    auto pulseOut = [this](const Pulse& c, int nr1_off) -> int {
        if (!c.enabled) return 0;
        u8 duty = (regs[nr1_off - 0x10] >> 6) & 0x03;
        return (DUTY_TABLE[duty] >> (7 - c.duty_pos)) & 1 ? c.volume : 0;
    };
    switch (ch) {
        case 1:
            return pulseOut(ch1, 0x11);
        case 2:
            return pulseOut(ch2, 0x16);
        case 3: {
            if (!wave.enabled) return 0;
            u8 level = (regs[0x0C] >> 5) & 0x03;
            switch (level) {
                case 0: return 0;
                case 1: return wave.sample_buffer;
                case 2: return wave.sample_buffer >> 1;
                default: return wave.sample_buffer >> 2;
            }
        }
        default:
            if (!noise.enabled) return 0;
            return (noise.lfsr & 1) ? noise.volume : 0;
    }
}

// PCM12 ($FF76): low nibble = CH1 output, high nibble = CH2 output.
u8 APU::pcm12() const {
    return static_cast<u8>((channelDigital(2) << 4) | (channelDigital(1) & 0x0F));
}

// PCM34 ($FF77): low nibble = CH3 output, high nibble = CH4 output.
u8 APU::pcm34() const {
    return static_cast<u8>((channelDigital(4) << 4) | (channelDigital(3) & 0x0F));
}

void APU::tickPulse(Pulse& ch, int nr1_off, int nr4_off, int t_cycles) {
    (void)nr1_off;
    (void)nr4_off;
    ch.timer -= t_cycles;
    while (ch.timer <= 0) {
        int base_off = (&ch == &ch1) ? 0x13 : 0x18;
        ch.timer += pulsePeriod(ch, base_off);
        ch.duty_pos = (ch.duty_pos + 1) & 7;
    }
}

void APU::tickWave(int t_cycles) {
    if (!wave.enabled) {
        // Timer still runs while disabled (period-write delay behavior)
        wave.timer -= t_cycles;
        while (wave.timer <= 0) {
            wave.timer += wavePeriod();
            // keep the position advancing so retrigger alignment holds
            wave.sample_pos = (wave.sample_pos + 1) & 31;
        }
        return;
    }

    wave.timer -= t_cycles;
    while (wave.timer <= 0) {
        wave.timer += wavePeriod();
        // Fetch the NEXT nibble: upper nibble first, left to right.
        wave.sample_pos = (wave.sample_pos + 1) & 31;
        u8 byte = regs[0x30 - 0x10 + (wave.sample_pos >> 1)];
        wave.sample_buffer =
            (wave.sample_pos & 1) ? (byte & 0x0F) : (byte >> 4);
    }
}

void APU::tickNoise(int t_cycles) {
    u8 nr43 = regs[0x22 - 0x10];
    int shift = (nr43 >> 4) & 0x0F;
    if (shift >= 14) return;  // clock shift 14/15: LFSR receives no clocks

    noise.timer -= t_cycles;
    while (noise.timer <= 0) {
        int period = noisePeriod();
        if (period <= 0) { noise.timer = NOISE_DIVISOR[0]; break; }

        noise.timer += period;

        bool short_mode = (nr43 & 0x08) != 0;

        // new bit = 1 if bit0 and bit1 are identical
        bool same = ((noise.lfsr ^ (noise.lfsr >> 1)) & 1) == 0;
        noise.lfsr >>= 1;
        if (same) noise.lfsr |= 0x4000;
        if (short_mode) {
            noise.lfsr = (noise.lfsr & ~0x0040) | (same ? 0x0040 : 0);
        }
    }
}

// ============================================
// FRAME SEQUENCER
// ============================================

void APU::clockFrameSequencer() {
    // Steps: 0 length | 2 length+sweep | 4 length | 6 length+sweep | 7 env
    if (fs_step == 0 || fs_step == 2 || fs_step == 4 || fs_step == 6) {
        clockLengths();
    }
    if (fs_step == 2 || fs_step == 6) {
        clockSweep();
    }
    if (fs_step == 7) {
        clockEnvelopes();
    }
    fs_step = (fs_step + 1) & 7;
}

void APU::clockLengths() {
    // CH1/CH2
    for (int i = 0; i < 2; i++) {
        Pulse& ch = (i == 0) ? ch1 : ch2;
        int nr4_off = (i == 0) ? 0x14 : 0x19;
        if ((regs[nr4_off - 0x10] & 0x40) && ch.length > 0) {
            if (--ch.length == 0) ch.enabled = false;
        }
    }
    // CH3
    if ((regs[0x1E - 0x10] & 0x40) && wave.length > 0) {
        if (--wave.length == 0) wave.enabled = false;
    }
    // CH4
    if ((regs[0x23 - 0x10] & 0x40) && noise.length > 0) {
        if (--noise.length == 0) noise.enabled = false;
    }
}

void APU::clockEnvelopes() {
    // CH1/CH2/CH4 (period 0 behaves as 8)
    Pulse* pulses[2] = { &ch1, &ch2 };
    for (int i = 0; i < 2; i++) {
        Pulse& ch = *pulses[i];
        int nr2_off = (i == 0) ? 0x12 : 0x17;
        u8 nr2 = regs[nr2_off - 0x10];
        int period = nr2 & 0x07;
        if (period == 0) continue;
        if (ch.env_timer > 0) {
            ch.env_timer--;
            if (ch.env_timer == 0) {
                ch.env_timer = period;
                if (nr2 & 0x08) {
                    if (ch.volume < 15) ch.volume++;
                } else {
                    if (ch.volume > 0) ch.volume--;
                }
            }
        }
    }

    // CH4
    u8 nr42 = regs[0x21 - 0x10];
    int period = nr42 & 0x07;
    if (period != 0 && noise.env_timer > 0) {
        noise.env_timer--;
        if (noise.env_timer == 0) {
            noise.env_timer = period;
            if (nr42 & 0x08) {
                if (noise.volume < 15) noise.volume++;
            } else {
                if (noise.volume > 0) noise.volume--;
            }
        }
    }
}

void APU::writeBackSweepFrequency() {
    // Write the shadow period back into NR13/NR14.
    u8 lo = static_cast<u8>(sweep.shadow & 0xFF);
    u8 hi = static_cast<u8>((sweep.shadow >> 8) & 0x07);
    regs[0x13 - 0x10] = lo;
    regs[0x14 - 0x10] = static_cast<u8>((regs[0x14 - 0x10] & 0xF8) | hi);
}

void APU::clockSweep() {
    if (!sweep.enabled) return;

    u8 nr10 = regs[0x10 - 0x10];
    int pace = (nr10 >> 4) & 0x07;
    int step = nr10 & 0x07;

    if (pace == 0) return;  // iterations disabled

    if (--sweep.timer <= 0) {
        sweep.timer = (pace == 0) ? 8 : pace;

        // Frequency calculation
        int new_period = sweep.shadow;
        if (step != 0) {
            new_period = (nr10 & 0x08)
                ? sweep.shadow - (sweep.shadow >> step)   // subtraction
                : sweep.shadow + (sweep.shadow >> step);  // addition

            if (new_period > 2047) {
                ch1.enabled = false;  // overflow check disables CH1
            } else {
                if (step != 0) {
                    sweep.shadow = new_period;
                    writeBackSweepFrequency();
                    // Second calculation + overflow check, no write back
                    int check = (nr10 & 0x08)
                        ? new_period - (new_period >> step)
                        : new_period + (new_period >> step);
                    if (check > 2047) ch1.enabled = false;
                }
            }
        }
    }
}

// ============================================
// TRIGGERS
// ============================================

void APU::triggerPulse(Pulse& ch, int nr1_off, int nr2_off, int nr3_off, int nr4_off) {
    ch.dac = pulseDAC(ch, nr2_off);
    if (!ch.dac) {
        ch.enabled = false;
        return;
    }
    ch.enabled = true;

    // Length reload (if it read as zero)
    if (ch.length == 0) ch.length = 64;

    // Frequency timer: low two bits are NOT modified on retrigger.
    int period = pulsePeriod(ch, nr3_off);
    ch.timer = (period & ~3) | (ch.timer & 3);

    // Envelope
    u8 nr2 = regs[nr2_off - 0x10];
    ch.volume = (nr2 >> 4) & 0x0F;
    int period_env = nr2 & 0x07;
    ch.env_timer = (period_env == 0) ? 8 : period_env;

    // Duty step timer reset
    ch.duty_pos = 0;
    (void)nr1_off;
    (void)nr4_off;
}

void APU::triggerWave() {
    wave.dac = (regs[0x1A - 0x10] & 0x80) != 0;
    if (!wave.dac) {
        wave.enabled = false;
        return;
    }
    wave.enabled = true;
    if (wave.length == 0) wave.length = 256;

    int period = wavePeriod();
    // Preserve the low bit like hardware does (approximation)
    wave.timer = period & ~1;

    // Sample position resets; the first fetch on the next tick reads
    // nibble 1 (the previously buffered sample plays until then).
    wave.sample_pos = 0;
}

void APU::triggerNoise() {
    noise.dac = (regs[0x21 - 0x10] & 0xF8) != 0;
    if (!noise.dac) {
        noise.enabled = false;
        return;
    }
    noise.enabled = true;
    if (noise.length == 0) noise.length = 64;

    noise.lfsr = 0x0000;  // LFSR reset to 0 on trigger (Pan Docs)
    noise.timer = noisePeriod();
    if (noise.timer <= 0) noise.timer = NOISE_DIVISOR[0];

    u8 nr42 = regs[0x21 - 0x10];
    noise.volume = (nr42 >> 4) & 0x0F;
    int period_env = nr42 & 0x07;
    noise.env_timer = (period_env == 0) ? 8 : period_env;
}

// ============================================
// REGISTER ACCESS
// ============================================

u8 APU::read(u8 off) const {
    // off = addr - 0xFF00, in [0x10, 0x3F]
    u8 idx = static_cast<u8>(off - 0x10);

    // Wave RAM is always accessible
    if (off >= 0x30) {
        if (wave.enabled) return 0xFF;   // DMG: locked while playing
        return regs[0x20 + (off - 0x30)];
    }

    if (!powered) {
        // Powered down: everything reads 0xFF except NR52
        if (off == 0x26) {
            return static_cast<u8>((regs[0x26 - 0x10] & 0x80) | 0x70);
        }
        return 0xFF;
    }

    switch (off) {
        case 0x10: return regs[0x10 - 0x10];                            // NR10
        case 0x11: return static_cast<u8>(0x3F | (regs[0x01] & 0xC0));  // NR11
        case 0x12: return regs[0x02];                                    // NR12
        case 0x13: return 0xFF;                                          // NR13 WO
        case 0x14: return static_cast<u8>(0x3F | (regs[0x04] & 0x40));   // NR14
        case 0x16: return static_cast<u8>(0x3F | (regs[0x06] & 0xC0));   // NR21
        case 0x17: return regs[0x07];                                    // NR22
        case 0x18: return 0xFF;                                          // NR23 WO
        case 0x19: return static_cast<u8>(0x3F | (regs[0x09] & 0x40));   // NR24
        case 0x1A: return static_cast<u8>(0x7F | (regs[0x0A] & 0x80));   // NR30
        case 0x1B: return 0xFF;                                          // NR31 WO
        case 0x1C: return static_cast<u8>(0x9F | (regs[0x0C] & 0x60));   // NR32
        case 0x1D: return 0xFF;                                          // NR33 WO
        case 0x1E: return static_cast<u8>(0x3F | (regs[0x0E] & 0x40));   // NR34
        case 0x20: return 0xFF;                                          // NR41 WO
        case 0x21: return regs[0x11];                                    // NR42
        case 0x22: return regs[0x12];                                     // NR43
        case 0x23: return static_cast<u8>(0x3F | (regs[0x13] & 0x40));   // NR44
        case 0x24: return regs[0x14];                                     // NR50
        case 0x25: return regs[0x15];                                     // NR51
        case 0x26: {                                                      // NR52
            u8 status = static_cast<u8>((regs[0x16] & 0x80) | 0x70);
            if (ch1.enabled) status |= 0x01;
            if (ch2.enabled) status |= 0x02;
            if (wave.enabled) status |= 0x04;
            if (noise.enabled) status |= 0x08;
            return status;
        }
        default:
            return regs[idx];
    }
}

void APU::write(u8 off, u8 value) {
    if (off >= 0x30) {
        // Wave RAM always writable (DMG ignores writes while playing)
        if (wave.enabled) return;
        regs[0x20 + (off - 0x30)] = value;
        return;
    }

    if (off == 0x26) {
        // NR52: master power
        bool enable = (value & 0x80) != 0;
        if (powered && !enable) {
            // Powering off clears all registers except wave RAM and
            // NR52; the length timers live in the channel structs (not
            // the register file) and survive on DMG.
            for (int i = 0x00; i <= 0x13; i++) regs[i] = 0;  // NR10..NR44
            regs[0x14] = 0;  // NR50
            regs[0x15] = 0;  // NR51
            ch1.enabled = ch2.enabled = wave.enabled = noise.enabled = false;
            ch1.dac = ch2.dac = wave.dac = noise.dac = false;
            sweep.enabled = false;
        } else if (!powered && enable) {
            // Powering on: registers reset to power-on defaults
            for (int i = 0; i < 0x16; i++) regs[i] = 0;
            regs[0x01] = 0xBF; regs[0x02] = 0xF3; regs[0x04] = 0xBF;
            regs[0x06] = 0x3F; regs[0x09] = 0xBF;
            regs[0x0A] = 0x7F; regs[0x0B] = 0xFF; regs[0x0C] = 0x9F; regs[0x0E] = 0xBF;
            regs[0x14] = 0x77; regs[0x15] = 0xF3;
            fs_step = 0;
            ch1.duty_pos = ch2.duty_pos = 0;
        }
        regs[0x16] = value & 0x80;
        powered = enable;
        return;
    }

    if (!powered) return;  // registers read-only while off

    u8 idx = static_cast<u8>(off - 0x10);
    regs[idx] = value;

    switch (off) {
        case 0x11: ch1.length = 64 - (value & 0x3F); break;
        case 0x16: ch2.length = 64 - (value & 0x3F); break;
        case 0x1B: wave.length = 256 - value; break;
        case 0x20: noise.length = 64 - (value & 0x3F); break;

        case 0x12:  // NR12: DAC status change can disable CH1
            if (!pulseDAC(ch1, 0x12)) ch1.enabled = false;
            break;
        case 0x17:
            if (!pulseDAC(ch2, 0x17)) ch2.enabled = false;
            break;
        case 0x21:
            if ((value & 0xF8) == 0) noise.enabled = false;
            break;
        case 0x1A:  // NR30: DAC bit gates CH3
            if (!(value & 0x80)) wave.enabled = false;
            break;

        case 0x14:  // NR14
            if (value & 0x80) triggerPulse(ch1, 0x11, 0x12, 0x13, 0x14);
            break;
        case 0x19:  // NR24
            if (value & 0x80) triggerPulse(ch2, 0x16, 0x17, 0x18, 0x19);
            break;
        case 0x1E:  // NR34
            if (value & 0x80) triggerWave();
            break;
        case 0x23:  // NR44
            if (value & 0x80) triggerNoise();
            break;

        case 0x10: {  // NR10 sweep settings
            // Clearing pace to 0 disables iterations instantly.
            u8 nr10 = regs[0x00];
            if (((nr10 >> 4) & 0x07) == 0) sweep.enabled = false;
            break;
        }
    }

    // CH1 trigger needs the sweep unit primed
    if (off == 0x14 && (value & 0x80)) {
        u8 nr10 = regs[0x00];
        int pace = (nr10 >> 4) & 0x07;
        int step = nr10 & 0x07;

        int period_reg = regs[0x03] | ((regs[0x04] & 0x07) << 8);
        sweep.shadow = period_reg;
        sweep.timer = (pace == 0) ? 8 : pace;
        sweep.enabled = (pace != 0) || (step != 0);

        // Immediate overflow check when the step is non-zero
        if (step != 0) {
            int check = (nr10 & 0x08)
                ? sweep.shadow - (sweep.shadow >> step)
                : sweep.shadow + (sweep.shadow >> step);
            if (check > 2047) ch1.enabled = false;
        }
    }
}

// ============================================
// MIXING
// ============================================

void APU::generateSample() {
    // Digital outputs (0-15)
    auto pulseDigital = [](const Pulse& ch, int nr1_off, const APU* self) -> int {
        if (!ch.enabled) return 0;
        u8 duty = (self->regs[nr1_off - 0x10] >> 6) & 0x03;
        return (DUTY_TABLE[duty] >> (7 - ch.duty_pos)) & 1 ? ch.volume : 0;
    };

    int d1 = pulseDigital(ch1, 0x11, this);
    int d2 = pulseDigital(ch2, 0x16, this);

    int d3 = 0;
    if (wave.enabled) {
        u8 level = (regs[0x0C] >> 5) & 0x03;
        switch (level) {
            case 0: d3 = 0; break;                  // mute
            case 1: d3 = wave.sample_buffer; break;      // 100%
            case 2: d3 = wave.sample_buffer >> 1; break;  // 50%
            default: d3 = wave.sample_buffer >> 2; break; // 25%
        }
    }

    int d4 = 0;
    if (noise.enabled) {
        d4 = (noise.lfsr & 1) ? noise.volume : 0;
    }

    // DACs: enabled DACs map digital 0-15 to analog [-1, +1]
    // (digital 0 -> analog +1; the slope is negative)
    auto analog = [](bool dac, int digital) -> float {
        if (!dac) return 0.0f;
        return 1.0f - (2.0f * digital) / 15.0f;
    };

    float a1 = analog(ch1.dac, d1);
    float a2 = analog(ch2.dac, d2);
    float a3 = analog(wave.dac, d3);
    float a4 = analog(noise.dac, d4);

    u8 nr51 = regs[0x15];
    u8 nr50 = regs[0x14];

    float left = 0, right = 0;
    if (nr51 & 0x10) left += a1;
    if (nr51 & 0x01) right += a1;
    if (nr51 & 0x20) left += a2;
    if (nr51 & 0x02) right += a2;
    if (nr51 & 0x40) left += a3;
    if (nr51 & 0x04) right += a3;
    if (nr51 & 0x80) left += a4;
    if (nr51 & 0x08) right += a4;

    float vol_left = static_cast<float>(((nr50 >> 4) & 0x07) + 1);
    float vol_right = static_cast<float>((nr50 & 0x07) + 1);
    left *= vol_left / 8.0f;
    right *= vol_right / 8.0f;

    // High-pass filter removes DC bias (charge factor 0.996 at 44100 Hz)
    bool dacs_on = ch1.dac || ch2.dac || wave.dac || noise.dac;
    float out_l = 0.0f, out_r = 0.0f;
    if (dacs_on) {
        out_l = left - cap_left;
        cap_left = left - out_l * 0.996f;
        out_r = right - cap_right;
        cap_right = right - out_r * 0.996f;
    } else {
        cap_left = cap_right = 0.0f;
    }

    float gain = user_volume * (32767.0f / 4.0f);
    i16 l = static_cast<i16>(std::lround(out_l * gain));
    i16 r = static_cast<i16>(std::lround(out_r * gain));

    pushSample(l, r);
}

// ============================================
// OUTPUT (called by the platform audio backend)
// ============================================

void APU::consumeSamples(i16* out, int frames) {
    std::lock_guard<std::mutex> lock(buffer_mutex);
    for (int i = 0; i < frames; i++) {
        if (buffer_read_pos != buffer_write_pos) {
            out[i * 2]      = sample_buffer[buffer_read_pos];
            out[i * 2 + 1]  = sample_buffer[buffer_read_pos + 1];
            buffer_read_pos = (buffer_read_pos + 2) % BUFFER_SIZE;
        } else {
            out[i * 2] = 0;      // underrun: silence
            out[i * 2 + 1] = 0;
        }
    }
}

void APU::pushSample(i16 left, i16 right) {
    std::lock_guard<std::mutex> lock(buffer_mutex);
    sample_buffer[buffer_write_pos] = left;
    sample_buffer[buffer_write_pos + 1] = right;
    buffer_write_pos = (buffer_write_pos + 2) % BUFFER_SIZE;
    if (buffer_write_pos == buffer_read_pos) {
        // overflow: drop oldest pair
        buffer_read_pos = (buffer_read_pos + 2) % BUFFER_SIZE;
    }
}

bool APU::popSample(i16& left, i16& right) {
    std::lock_guard<std::mutex> lock(buffer_mutex);
    if (buffer_read_pos == buffer_write_pos) {
        left = right = 0;
        return false;
    }
    left = sample_buffer[buffer_read_pos];
    right = sample_buffer[buffer_read_pos + 1];
    buffer_read_pos = (buffer_read_pos + 2) % BUFFER_SIZE;
    return true;
}

// ============================================
// SAVE STATE
// ============================================

void APU::saveState(std::ofstream& file) const {
    file.write(reinterpret_cast<const char*>(regs.data()), regs.size());
    u8 p = powered ? 1 : 0;
    file.write(reinterpret_cast<const char*>(&p), sizeof(p));
    file.write(reinterpret_cast<const char*>(&fs_step), sizeof(fs_step));
    file.write(reinterpret_cast<const char*>(&fs_counter), sizeof(fs_counter));

    auto savePulse = [&file](const Pulse& ch) {
        file.write(reinterpret_cast<const char*>(&ch.timer), sizeof(ch.timer));
        file.write(reinterpret_cast<const char*>(&ch.duty_pos), sizeof(ch.duty_pos));
        file.write(reinterpret_cast<const char*>(&ch.length), sizeof(ch.length));
        u8 f = static_cast<u8>((ch.enabled ? 1 : 0) | (ch.dac ? 2 : 0));
        file.write(reinterpret_cast<const char*>(&f), sizeof(f));
        file.write(reinterpret_cast<const char*>(&ch.volume), sizeof(ch.volume));
        file.write(reinterpret_cast<const char*>(&ch.env_timer), sizeof(ch.env_timer));
    };
    savePulse(ch1);
    savePulse(ch2);
    file.write(reinterpret_cast<const char*>(&sweep), sizeof(sweep));

    file.write(reinterpret_cast<const char*>(&wave.timer), sizeof(wave.timer));
    file.write(reinterpret_cast<const char*>(&wave.sample_pos), sizeof(wave.sample_pos));
    file.write(reinterpret_cast<const char*>(&wave.length), sizeof(wave.length));
    u8 wf = static_cast<u8>((wave.enabled ? 1 : 0) | (wave.dac ? 2 : 0));
    file.write(reinterpret_cast<const char*>(&wf), sizeof(wf));
    file.write(reinterpret_cast<const char*>(&wave.sample_buffer), sizeof(wave.sample_buffer));

    file.write(reinterpret_cast<const char*>(&noise.timer), sizeof(noise.timer));
    file.write(reinterpret_cast<const char*>(&noise.lfsr), sizeof(noise.lfsr));
    file.write(reinterpret_cast<const char*>(&noise.length), sizeof(noise.length));
    u8 nf = static_cast<u8>((noise.enabled ? 1 : 0) | (noise.dac ? 2 : 0));
    file.write(reinterpret_cast<const char*>(&nf), sizeof(nf));
    file.write(reinterpret_cast<const char*>(&noise.volume), sizeof(noise.volume));
    file.write(reinterpret_cast<const char*>(&noise.env_timer), sizeof(noise.env_timer));

    file.write(reinterpret_cast<const char*>(&sample_accum), sizeof(sample_accum));
    file.write(reinterpret_cast<const char*>(&cap_left), sizeof(cap_left));
    file.write(reinterpret_cast<const char*>(&cap_right), sizeof(cap_right));
}

void APU::loadState(std::ifstream& file) {
    file.read(reinterpret_cast<char*>(regs.data()), regs.size());
    u8 p = 0;
    file.read(reinterpret_cast<char*>(&p), sizeof(p));
    powered = (p != 0);
    file.read(reinterpret_cast<char*>(&fs_step), sizeof(fs_step));
    file.read(reinterpret_cast<char*>(&fs_counter), sizeof(fs_counter));

    auto loadPulse = [&file](Pulse& ch) {
        file.read(reinterpret_cast<char*>(&ch.timer), sizeof(ch.timer));
        file.read(reinterpret_cast<char*>(&ch.duty_pos), sizeof(ch.duty_pos));
        file.read(reinterpret_cast<char*>(&ch.length), sizeof(ch.length));
        u8 f = 0;
        file.read(reinterpret_cast<char*>(&f), sizeof(f));
        ch.enabled = (f & 1) != 0;
        ch.dac = (f & 2) != 0;
        file.read(reinterpret_cast<char*>(&ch.volume), sizeof(ch.volume));
        file.read(reinterpret_cast<char*>(&ch.env_timer), sizeof(ch.env_timer));
    };
    loadPulse(ch1);
    loadPulse(ch2);
    file.read(reinterpret_cast<char*>(&sweep), sizeof(sweep));

    file.read(reinterpret_cast<char*>(&wave.timer), sizeof(wave.timer));
    file.read(reinterpret_cast<char*>(&wave.sample_pos), sizeof(wave.sample_pos));
    file.read(reinterpret_cast<char*>(&wave.length), sizeof(wave.length));
    u8 wf = 0;
    file.read(reinterpret_cast<char*>(&wf), sizeof(wf));
    wave.enabled = (wf & 1) != 0;
    wave.dac = (wf & 2) != 0;
    file.read(reinterpret_cast<char*>(&wave.sample_buffer), sizeof(wave.sample_buffer));

    file.read(reinterpret_cast<char*>(&noise.timer), sizeof(noise.timer));
    file.read(reinterpret_cast<char*>(&noise.lfsr), sizeof(noise.lfsr));
    file.read(reinterpret_cast<char*>(&noise.length), sizeof(noise.length));
    u8 nf = 0;
    file.read(reinterpret_cast<char*>(&nf), sizeof(nf));
    noise.enabled = (nf & 1) != 0;
    noise.dac = (nf & 2) != 0;
    file.read(reinterpret_cast<char*>(&noise.volume), sizeof(noise.volume));
    file.read(reinterpret_cast<char*>(&noise.env_timer), sizeof(noise.env_timer));

    file.read(reinterpret_cast<char*>(&sample_accum), sizeof(sample_accum));
    file.read(reinterpret_cast<char*>(&cap_left), sizeof(cap_left));
    file.read(reinterpret_cast<char*>(&cap_right), sizeof(cap_right));
}
