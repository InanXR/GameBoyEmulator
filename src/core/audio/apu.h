#pragma once
#include "common/types.h"
#include <array>
#include <fstream>
#include <mutex>

/**
 * Audio Processing Unit (APU)
 *
 * Full 4-channel GameBoy audio:
 *  - CH1: pulse with frequency sweep
 *  - CH2: pulse
 *  - CH3: 32-step wave output
 *  - CH4: pseudo-random noise (15/7-bit LFSR)
 *
 * Mixing follows Pan Docs: each channel's digital output (0-15) is
 * converted by its DAC (if enabled) to an analog value, channels are
 * panned via NR51, scaled by NR50, and passed through a high-pass
 * filter to remove DC bias.
 *
 * The APU is platform-free: it synthesizes at SAMPLE_RATE and buffers
 * stereo samples in a thread-safe ring buffer. The platform layer
 * (src/platform/audio_backend.cpp) drains it from an SDL audio
 * callback.
 *
 * Frame sequencer (DIV-APU, 512 Hz):
 *   step 0: length   step 4: length
 *   step 2: length+sweep  step 6: length+sweep
 *   step 7: envelope
 */
class APU {
public:
    // Output sample rate the mixer targets (Hz).
    static constexpr int SAMPLE_RATE = 44100;

    APU();
    ~APU() = default;

    // Non-copyable (owns a mutex-protected ring buffer).
    APU(const APU&) = delete;
    APU& operator=(const APU&) = delete;

    // Power-cycle: restore the power-on register values and clear all
    // channels, the sequencer and the sample buffer.
    void reset();

    // Advance by m_cycles M-cycles (granularity of 1 M-cycle or less
    // is fine; internally processed in T-cycle units).
    void tick(int m_cycles);

    // Advance by t_cycles T-cycles directly. In CGB double-speed mode
    // a CPU M-cycle is only 2 base T-cycles (the APU stays in the
    // 4.19 MHz domain), so the emulator ticks the APU in T-cycle
    // units: 4 per normal-speed M-cycle, 2 per double-speed one.
    void tickT(int t_cycles);

    // CGB PCM12/PCM34 debug registers (FF76/FF77): the generation
    // circuits' raw digital outputs (0-15), unaffected by NR50/NR51.
    u8 pcm12() const;
    u8 pcm34() const;

    // Register access. `off` is addr - 0xFF00, in [0x10, 0x3F].
    u8 read(u8 off) const;
    void write(u8 off, u8 value);

    // Output volume (0.0 - 1.5), independent of NR50.
    void setVolume(float v) { user_volume = v; }
    float getVolume() const { return user_volume; }

    // Called by the platform audio callback: fills `frames` interleaved
    // stereo frames (L,R) into `out`. Underruns are zero-filled, so the
    // callback can always emit the requested amount.
    void consumeSamples(i16* out, int frames);

    // Save/Load state
    void saveState(std::ofstream& file) const;
    void loadState(std::ifstream& file);

private:
    // ---------- register file: regs[i] = register (0xFF10 + i) ----------
    std::array<u8, 0x30> regs;

    bool powered;          // NR52 bit 7

    // ---------- pulse channels (CH1 with sweep, CH2 plain) ----------
    struct Pulse {
        int  timer;          // T-cycles until next duty step
        int  duty_pos;       // 0-7
        int  length;         // 0-64
        bool enabled;        // channel active flag
        bool dac;            // DAC enabled
        int  volume;         // 0-15
        int  env_timer;      // envelope countdown
    };
    Pulse ch1, ch2;

    // CH1 sweep unit
    struct Sweep {
        int  shadow;         // 11-bit shadow period
        int  timer;          // countdown to next iteration (pace units)
        bool enabled;
    };
    Sweep sweep;

    // ---------- wave channel (CH3) ----------
    struct Wave {
        int  timer;          // T-cycles until next sample fetch
        int  sample_pos;     // 0-31 (index of last-fetched nibble)
        int  length;         // 0-256
        bool enabled;
        bool dac;
        u8   sample_buffer;  // last nibble fetched (output continuously)
    };
    Wave wave;

    // ---------- noise channel (CH4) ----------
    struct Noise {
        int  timer;          // T-cycles until next LFSR step
        u16  lfsr;           // 15-bit state
        int  length;         // 0-64
        bool enabled;
        bool dac;
        int  volume;
        int  env_timer;
    };
    struct Noise noise;

    // ---------- frame sequencer ----------
    int fs_step;            // 0-7
    int fs_counter;          // T-cycles; 8192 per step (512 Hz)

    static constexpr int T_CYCLES_PER_FRAME_SEQ_STEP = 8192;

    // ---------- sampling ----------
    double sample_accum;     // T-cycles accumulated toward next sample
    double t_cycles_per_sample;

    float user_volume;
    float cap_left, cap_right;  // high-pass filter capacitors

    // ---------- output ring buffer (thread-safe) ----------
    // Interleaved stereo samples; the platform audio callback drains it.
    static constexpr int BUFFER_SIZE = 8192;  // must be even
    i16 sample_buffer[BUFFER_SIZE];
    int buffer_write_pos;
    int buffer_read_pos;
    mutable std::mutex buffer_mutex;

    // ---------- helpers ----------
    int channelDigital(int ch) const;  // raw 0-15 output of CH1-4
    int pulsePeriod(const Pulse& ch, int base_off) const;
    int wavePeriod() const;
    int noisePeriod() const;
    bool pulseDAC(const Pulse& ch, int nr2_off) const;

    void tickPulse(Pulse& ch, int nr1_off, int nr4_off, int t_cycles);
    void tickWave(int t_cycles);
    void tickNoise(int t_cycles);

    void clockFrameSequencer();
    void clockLengths();
    void clockEnvelopes();
    void clockSweep();
    void writeBackSweepFrequency();

    void triggerPulse(Pulse& ch, int nr1_off, int nr2_off, int nr3_off, int nr4_off);
    void triggerWave();
    void triggerNoise();

    void generateSample();
    void pushSample(i16 left, i16 right);
    bool popSample(i16& left, i16& right);

    // Power-on state shared by the constructor and reset().
    // Callers are responsible for locking when threads may run.
    void powerOn();
};
