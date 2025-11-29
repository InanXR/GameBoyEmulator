#include "apu.h"
#include <iostream>
#include <cstring>

APU::APU() 
    : deviceId(0)
    , frame_sequencer(0)
    , frame_sequencer_cycles(0)
{
    registers.fill(0);
    
    // Initialize NR52 with audio enabled (bit 7 = 1)
    registers[0x26 - 0x10] = 0xF1; // Audio on + all channels active flags
    
    // Initialize channels
    ch1 = {0};
    ch2 = {0};
    
    // Initialize sample generation
    sample_accumulator = 0.0f;
    buffer_write_pos = 0;
    buffer_read_pos = 0;
    buffer_mutex = SDL_CreateMutex();
    
    std::memset(sample_buffer, 0, sizeof(sample_buffer));
}

APU::~APU() {
    if (buffer_mutex) {
        SDL_DestroyMutex(buffer_mutex);
    }
    if (deviceId != 0) {
        SDL_CloseAudioDevice(deviceId);
    }
}

void APU::init() {
    std::cout << "[APU] Initializing SDL Audio subsystem..." << std::endl;
    
    // Initialize SDL Audio subsystem if not already done
    if (SDL_WasInit(SDL_INIT_AUDIO) == 0) {
        std::cout << "[APU] SDL Audio not initialized, initializing now..." << std::endl;
        if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0) {
            std::cerr << "[APU] Failed to initialize SDL Audio: " << SDL_GetError() << std::endl;
            return;
        }
    }
    
    SDL_AudioSpec desired;
    SDL_AudioSpec obtained;

    SDL_zero(desired);
    desired.freq = 44100;      // CD quality
    desired.format = AUDIO_S16SYS; // 16-bit signed
    desired.channels = 2;      // Stereo
    desired.samples = 512;     // Smaller buffer for lower latency
    desired.callback = audioCallback;
    desired.userdata = this;

    std::cout << "[APU] Opening audio device..." << std::endl;
    std::cout << "[APU] Requested: " << desired.freq << "Hz, " << (int)desired.channels << " channels, buffer=" << desired.samples << std::endl;
    
    // Open audio device
    deviceId = SDL_OpenAudioDevice(NULL, 0, &desired, &obtained, 0);
    if (deviceId == 0) {
        std::cerr << "[APU] Failed to open audio device: " << SDL_GetError() << std::endl;
        return;
    }

    std::cout << "[APU] Audio device opened! ID=" << deviceId << std::endl;
    std::cout << "[APU] Obtained: " << obtained.freq << "Hz, " << (int)obtained.channels << " channels, buffer=" << obtained.samples << std::endl;

    // Unpause to start playing
    SDL_PauseAudioDevice(deviceId, 0);
    std::cout << "🎵 APU Initialized and UNPAUSED!" << std::endl;
}

void APU::step(int cycles) {
    // Frame Sequencer (512Hz)
    // CPU clock = 4194304 Hz
    // 4194304 / 512 = 8192 T-cycles = 2048 M-cycles
    
    frame_sequencer_cycles += cycles;
    if (frame_sequencer_cycles >= 2048) {
        frame_sequencer_cycles -= 2048;
        
        frame_sequencer++;
        
        // Step 0: Length
        // Step 1: -
        // Step 2: Length, Sweep
        // Step 3: -
        // Step 4: Length
        // Step 5: -
        // Step 6: Length, Sweep
        // Step 7: Volume Envelope
        
        if (frame_sequencer % 2 == 0) {
            clockLength();
        }
        
        if (frame_sequencer == 2 || frame_sequencer == 6) {
            clockSweep();
        }
        
        if (frame_sequencer == 7) {
            clockEnvelope();
        }
        
        if (frame_sequencer >= 8) {
            frame_sequencer = 0;
        }
    }
    
    // Update Channel 1
    if (ch1.enabled) updateSquareChannel(ch1, cycles);
    
    // Update Channel 2
    if (ch2.enabled) updateSquareChannel(ch2, cycles);
    
    // Generate samples based on cycles elapsed
    sample_accumulator += cycles;
    while (sample_accumulator >= CYCLES_PER_SAMPLE) {
        sample_accumulator -= CYCLES_PER_SAMPLE;
        i16 sample = generateSample();
        pushSample(sample);
    }
}

i16 APU::generateSample() {
    int sample = 0;
    
    // Each channel outputs 0-15 volume
    // When duty cycle is high, we use the volume, when low we use 0
    if (ch1.enabled && ch1.output > 0) {
        sample += (ch1.output * 2000) - 15000;  // Scale to -15000 to +15000
    }
    
    if (ch2.enabled && ch2.output > 0) {
        sample += (ch2.output * 2000) - 15000;
    }
    
    // Clamp
    if (sample > 32767) sample = 32767;
    if (sample < -32768) sample = -32768;
    
    return (i16)sample;
}

void APU::updateSquareChannel(SquareChannel& ch, int cycles) {
    ch.timer -= cycles;
    
    if (ch.timer <= 0) {
        // Reload timer
        // Frequency = 131072 / (2048 - x)
        // Timer ticks at 4 * (2048 - x) M-cycles? 
        // Actually, let's use the formula: (2048 - frequency) * 4
        
        int frequency = ch.nr_3 | ((ch.nr_4 & 0x07) << 8);
        ch.timer += (2048 - frequency) * 4;
        
        // Advance duty cycle
        ch.duty_pos = (ch.duty_pos + 1) & 7;
    }
    
    // Update output based on duty cycle
    int duty = (ch.nr_1 >> 6) & 0x03;
    ch.output = getSquareDuty(duty, ch.duty_pos) ? ch.volume : 0;
}

void APU::triggerSquareChannel(SquareChannel& ch) {
    ch.enabled = true;
    ch.volume = (ch.nr_2 >> 4);
    ch.envelope_timer = (ch.nr_2 & 0x07);
    
    // Reload frequency timer
    int freq = ch.nr_3 | ((ch.nr_4 & 0x07) << 8);
    ch.timer = (2048 - freq) * 4;
    
    // Reload length if 0
    if (ch.length_counter == 0) {
        ch.length_counter = 64;
    }
}

void APU::clockLength() {
    // Length counters clock at 256 Hz (every other frame sequencer step)
    // Only disable if length enable bit (bit 6 of NRx4) is set
    
    // Channel 1
    if ((ch1.nr_4 & 0x40) && ch1.length_counter > 0) { // Bit 6: length enable
        ch1.length_counter--;
        if (ch1.length_counter == 0) {
            ch1.enabled = false;
        }
    }
    
    // Channel 2
    if ((ch2.nr_4 & 0x40) && ch2.length_counter > 0) {
        ch2.length_counter--;
        if (ch2.length_counter == 0) {
            ch2.enabled = false;
        }
    }
}

void APU::clockEnvelope() {
    // Channel 1
    if (ch1.enabled && (ch1.nr_2 & 0x07) != 0) { // Period != 0
        if (ch1.envelope_timer > 0) {
            ch1.envelope_timer--;
            if (ch1.envelope_timer == 0) {
                // Reload period
                ch1.envelope_timer = (ch1.nr_2 & 0x07);
                
                // Update volume
                if (ch1.nr_2 & 0x08) { // Increase
                    if (ch1.volume < 15) ch1.volume++;
                } else { // Decrease
                    if (ch1.volume > 0) ch1.volume--;
                }
            }
        }
    }
    
    // Channel 2
    if (ch2.enabled && (ch2.nr_2 & 0x07) != 0) {
        if (ch2.envelope_timer > 0) {
            ch2.envelope_timer--;
            if (ch2.envelope_timer == 0) {
                ch2.envelope_timer = (ch2.nr_2 & 0x07);
                if (ch2.nr_2 & 0x08) {
                    if (ch2.volume < 15) ch2.volume++;
                } else {
                    if (ch2.volume > 0) ch2.volume--;
                }
            }
        }
    }
}

void APU::clockSweep() {
    // TODO: Implement sweep for Channel 1
}

u8 APU::getSquareDuty(int duty, int pos) {
    // GameBoy Duty Cycles (8 steps per cycle)
    // 0: 12.5% = 00000001
    // 1: 25%   = 10000001
    // 2: 50%   = 10000111  
    // 3: 75%   = 01111110
    
    const u8 duties[4] = {
        0x01, // 00000001 - 12.5%
        0x81, // 10000001 - 25%
        0x87, // 10000111 - 50%
        0x7E  // 01111110 - 75%
    };
    
    return (duties[duty] >> pos) & 1;
}

u8 APU::read(u16 addr) {
    if (addr >= 0xFF10 && addr <= 0xFF3F) {
        // Handle NR52 specially - return channel status flags
        if (addr == 0xFF26) {
            u8 nr52 = registers[0x26 - 0x10];
            // Update channel status bits (read-only)
            nr52 &= 0xF0; // Keep only bits 7-4
            if (ch1.enabled) nr52 |= 0x01;
            if (ch2.enabled) nr52 |= 0x02;
            return nr52;
        }
        return registers[addr - 0xFF10];
    }
    return 0xFF;
}

void APU::write(u16 addr, u8 value) {
    if (addr >= 0xFF10 && addr <= 0xFF3F) {
        registers[addr - 0xFF10] = value;
        
        // Handle NR52 (master audio enable)
        if (addr == 0xFF26) {
            bool audio_enabled = (value & 0x80) != 0;
            if (!audio_enabled) {
                // Disable audio - clear all registers except wave RAM and NR52
                std::cout << "[APU] Audio disabled via NR52" << std::endl;
                for (int i = 0; i < 0x20; i++) { // Clear 0xFF10-0xFF2F
                    registers[i] = 0;
                }
                registers[0x26 - 0x10] = value & 0x80; // Restore bit 7
                ch1.enabled = false;
                ch2.enabled = false;
                return;
            } else {
                std::cout << "[APU] Audio enabled via NR52" << std::endl;
            }
            return;
        }
        
        // If audio master is disabled, ignore all other writes
        if ((registers[0x26 - 0x10] & 0x80) == 0) {
            return;
        }
        
        // Channel 1
        if (addr == 0xFF10) ch1.nr_0 = value;
        if (addr == 0xFF11) { ch1.nr_1 = value; ch1.length_counter = 64 - (value & 0x3F); }
        if (addr == 0xFF12) { ch1.nr_2 = value; }
        if (addr == 0xFF13) ch1.nr_3 = value;
        if (addr == 0xFF14) {
            ch1.nr_4 = value;
            if (value & 0x80) { // Trigger
                triggerSquareChannel(ch1);
            }
        }
        
        // Channel 2
        if (addr == 0xFF16) { ch2.nr_1 = value; ch2.length_counter = 64 - (value & 0x3F); }
        if (addr == 0xFF17) { ch2.nr_2 = value; }
        if (addr == 0xFF18) ch2.nr_3 = value;
        if (addr == 0xFF19) {
            ch2.nr_4 = value;
            if (value & 0x80) { // Trigger
                triggerSquareChannel(ch2);
            }
        }
    }
}

void APU::audioCallback(void* userdata, u8* stream, int len) {
    APU* apu = static_cast<APU*>(userdata);
    apu->fillBuffer(stream, len);
}

void APU::fillBuffer(u8* stream, int len) {
    // Cast to 16-bit stereo samples
    i16* buffer = reinterpret_cast<i16*>(stream);
    int num_samples = len / 4; // 4 bytes per stereo sample
    
    // Consume samples from buffer
    for (int i = 0; i < num_samples; i++) {
        i16 sample = popSample(); // Will return 0 if buffer is empty
        
        // Write stereo (same for both channels)
        buffer[i * 2] = sample;     // Left
        buffer[i * 2 + 1] = sample; // Right
    }
}

// Buffer management methods
void APU::pushSample(i16 sample) {
    SDL_LockMutex(buffer_mutex);
    
    sample_buffer[buffer_write_pos] = sample;
    buffer_write_pos = (buffer_write_pos + 1) % BUFFER_SIZE;
    
    // Handle overflow - drop oldest sample
    if (buffer_write_pos == buffer_read_pos) {
        buffer_read_pos = (buffer_read_pos + 1) % BUFFER_SIZE;
    }
    
    SDL_UnlockMutex(buffer_mutex);
}

i16 APU::popSample() {
    SDL_LockMutex(buffer_mutex);
    
    i16 sample = 0;
    if (buffer_read_pos != buffer_write_pos) {
        sample = sample_buffer[buffer_read_pos];
        buffer_read_pos = (buffer_read_pos + 1) % BUFFER_SIZE;
    }
    
    SDL_UnlockMutex(buffer_mutex);
    return sample;
}

int APU::getBufferedSampleCount() {
    SDL_LockMutex(buffer_mutex);
    
    int count;
    if (buffer_write_pos >= buffer_read_pos) {
        count = buffer_write_pos - buffer_read_pos;
    } else {
        count = BUFFER_SIZE - buffer_read_pos + buffer_write_pos;
    }
    
    SDL_UnlockMutex(buffer_mutex);
    return count;
}
