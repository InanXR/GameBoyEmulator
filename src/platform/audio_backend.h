#pragma once
// SDL headers are included as <SDL.h>: the SDL2::SDL2 target supplies
// the correct include directory on every platform (the official VC
// package keeps headers flat, distro/brew/vcpkg installs use SDL2/).
#include <SDL.h>

class APU;

/**
 * SDL2 audio output.
 *
 * Owns the audio device and drains the APU's thread-safe sample ring
 * buffer from the SDL audio callback. This is the platform half of
 * the audio pipeline; synthesis and buffering live in the core
 * (src/core/audio/apu.cpp).
 */
class AudioBackend {
public:
    AudioBackend() = default;
    ~AudioBackend();

    AudioBackend(const AudioBackend&) = delete;
    AudioBackend& operator=(const AudioBackend&) = delete;

    // Opens a 44.1 kHz stereo device pulling from `apu`.
    // Returns false (with a diagnostic) if audio is unavailable; the
    // emulator keeps running silently in that case.
    bool start(APU& apu);

    void stop();

private:
    static void SDLCALL callback(void* userdata, Uint8* stream, int len);

    SDL_AudioDeviceID device = 0;
    APU* apu = nullptr;
};
