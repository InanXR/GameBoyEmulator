#include "audio_backend.h"
#include "core/audio/apu.h"
#include <iostream>

AudioBackend::~AudioBackend() {
    stop();
}

bool AudioBackend::start(APU& source) {
    if (SDL_WasInit(SDL_INIT_AUDIO) == 0) {
        if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0) {
            std::cerr << "[audio] SDL audio init failed: " << SDL_GetError() << std::endl;
            return false;
        }
    }

    SDL_AudioSpec desired, obtained;
    SDL_zero(desired);
    desired.freq = APU::SAMPLE_RATE;
    desired.format = AUDIO_S16SYS;
    desired.channels = 2;       // stereo, interleaved
    desired.samples = 1024;     // lower latency buffer
    desired.callback = &AudioBackend::callback;
    desired.userdata = this;

    device = SDL_OpenAudioDevice(nullptr, 0, &desired, &obtained, 0);
    if (device == 0) {
        std::cerr << "[audio] failed to open device: " << SDL_GetError() << std::endl;
        return false;
    }

    apu = &source;
    SDL_PauseAudioDevice(device, 0);

    std::cout << "[audio] " << obtained.freq << " Hz, "
              << static_cast<int>(obtained.channels) << " ch" << std::endl;
    return true;
}

void AudioBackend::stop() {
    if (device != 0) {
        SDL_CloseAudioDevice(device);
        device = 0;
    }
    apu = nullptr;
}

void AudioBackend::callback(void* userdata, Uint8* stream, int len) {
    AudioBackend* self = static_cast<AudioBackend*>(userdata);
    // len is in bytes: 2 bytes per sample, 2 channels -> len/4 frames.
    if (self->apu != nullptr) {
        self->apu->consumeSamples(reinterpret_cast<i16*>(stream), len / 4);
    } else {
        SDL_memset(stream, 0, static_cast<size_t>(len));
    }
}
