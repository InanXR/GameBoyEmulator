#pragma once
#include "../utils/types.h"
#include <SDL2/SDL.h>
#include <array>
#include <fstream>

class MMU;

/**
 * Audio Processing Unit (APU)
 * 
 * Handles GameBoy audio generation:
 * - Channel 1: Square wave with sweep and envelope
 * - Channel 2: Square wave with envelope
 * - Channel 3: Wave output
 * - Channel 4: Noise
 */
class APU {
public:
    APU();
    ~APU();

    // Initialize SDL2 Audio
    void init();
    
    // Step APU by n M-cycles
    void step(int cycles);
    
    // Register access
    u8 read(u16 addr);
    void write(u16 addr, u8 value);
    
    // SDL Audio Callback
    static void audioCallback(void* userdata, u8* stream, int len);
    
    // Save/Load state (minimal stub)
    void saveState(std::ofstream& file) const { /* TODO: implement */ }
    void loadState(std::ifstream& file) { /* TODO: implement */ }

private:
    // Audio Registers (0xFF10 - 0xFF3F)
    // Mapped internally to 0x00 - 0x2F
    std::array<u8, 0x30> registers;
    
    // SDL Audio Device
    SDL_AudioDeviceID deviceId;
    
    // Fill SDL audio buffer with samples
    void fillBuffer(u8* stream, int len);
    
    // Internal counters
    int frame_sequencer;
    int frame_sequencer_cycles;
    
    // Square Channel Structure
    struct SquareChannel {
        // Registers
        u8 nr_0; // Sweep (Ch1 only)
        u8 nr_1; // Length / Duty
        u8 nr_2; // Envelope
        u8 nr_3; // Frequency lo
        u8 nr_4; // Frequency hi / Control
        
        // Internal State
        bool enabled;
        int timer;       // Frequency timer
        int duty_pos;    // Current position in duty cycle (0-7)
        int length_counter;
        int volume;
        int envelope_timer;
        
        // Output
        u8 output;
    };
    
    SquareChannel ch1;
    SquareChannel ch2;
    
    // Sample generation and buffering
    float sample_accumulator;  // Tracks partial samples
    const float SAMPLE_RATE = 44100.0f;
    const float CPU_FREQ = 4194304.0f;
    const float CYCLES_PER_SAMPLE = CPU_FREQ / SAMPLE_RATE; // ~95 cycles per sample
    
    // Sample buffer (ring buffer)
    static const int BUFFER_SIZE = 4096;
    i16 sample_buffer[BUFFER_SIZE];
    int buffer_write_pos;
    int buffer_read_pos;
    SDL_mutex* buffer_mutex;
    
    // Helper methods
    void updateSquareChannel(SquareChannel& ch, int cycles);
    u8 getSquareDuty(int duty, int pos);
    
    // Frame Sequencer Helpers
    void clockLength();
    void clockEnvelope();
    void clockSweep();
    
    // Channel Helpers
    void triggerSquareChannel(SquareChannel& ch);
    
    // Sample generation and buffering
    i16 generateSample();
    void pushSample(i16 sample);
    i16 popSample();
    int getBufferedSampleCount();
};
