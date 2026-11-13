/**
 * SDL2 frontend — entry point.
 *
 * Owns everything platform-specific: argument parsing, the windowed
 * main loop (display, input, audio device, hotkeys, frame pacing,
 * screenshots), and headless dispatch for the automated test suite.
 * Emulation itself lives in the platform-free core (src/core/).
 */
#include "core/emulator.h"
#include "display.h"
#include "audio_backend.h"
#include <iostream>
#include <fstream>
#include <string>
#include <cmath>

/**
 * Usage:
 *   gameboy <rom.gb> [options]
 *
 * Options:
 *   --headless N     Run N frames without a window/audio (for testing)
 *   --screenshot F   In headless mode: write the final framebuffer to F (PPM)
 *   --scale N        Window scale factor (1-8, default 4)
 */

static void printHeader() {
    std::cout << "\n"
              << "  ============================================\n"
              << "     GameBoy Emulator - C++17 + SDL2\n"
              << "     Sharp LR35902 CPU Emulation\n"
              << "     Built by Inan - 2026\n"
              << "  ============================================\n\n";
}

static void printUsage(const char* prog) {
    std::cerr << "Usage: " << prog << " <rom.gb> [--headless N] [--screenshot file.ppm] "
              << "[--scale N]\n";
    std::cerr << "  --headless N    run N frames without a window (testing)\n";
    std::cerr << "  --screenshot F  headless: dump final framebuffer as PPM\n";
    std::cerr << "  --scale N       window scale 1-8 (default 4)\n";
}

static void printControls() {
    std::cout << "\nStarting emulation..." << std::endl;
    std::cout << "  Arrow keys   D-Pad" << std::endl;
    std::cout << "  Z / X        A / B" << std::endl;
    std::cout << "  Enter/Shift  Start / Select" << std::endl;
    std::cout << "  F1 Reset   F2 Screenshot   F5 Save   F8 Load" << std::endl;
    std::cout << "  P Pause   Tab Turbo   F11 Fullscreen" << std::endl;
    std::cout << "  M Mute   +/- Volume   ESC Quit" << std::endl;
}

// Writes a 160x144 PPM (P6) using the DMG green palette.
static bool writeScreenshot(const std::string& path,
                            const std::array<u8, 160 * 144>& fb) {
    static const u8 PALETTE[4][3] = {
        {224, 248, 208}, {136, 192, 112}, {52, 104, 86}, {8, 24, 32}
    };
    std::ofstream out(path, std::ios::binary);
    if (!out) return false;
    out << "P6\n160 144\n255\n";
    for (int y = 0; y < 144; y++) {
        for (int x = 0; x < 160; x++) {
            u8 shade = fb[static_cast<size_t>(y) * 160 + x] & 3;
            out.write(reinterpret_cast<const char*>(PALETTE[shade]), 3);
        }
    }
    return static_cast<bool>(out);
}

static void handleHotkeys(Emulator& emu, Display& display, bool& paused) {
    Display::Hotkey hk;
    while ((hk = display.nextHotkey()) != Display::Hotkey::None) {
        switch (hk) {
            case Display::Hotkey::SaveState:
                emu.saveState();
                break;
            case Display::Hotkey::LoadState:
                emu.loadState();
                break;
            case Display::Hotkey::Reset:
                emu.reset();
                break;
            case Display::Hotkey::Pause:
                paused = !paused;
                std::cout << (paused ? "Paused" : "Resumed") << std::endl;
                break;
            case Display::Hotkey::ToggleFullscreen:
                display.toggleFullscreen();
                break;
            case Display::Hotkey::VolumeUp: {
                float v = std::fmin(1.5f, emu.audio().getVolume() + 0.1f);
                emu.audio().setVolume(v);
                std::cout << "Volume: " << (v * 100.0f) << "%" << std::endl;
                break;
            }
            case Display::Hotkey::VolumeDown: {
                float v = std::fmax(0.0f, emu.audio().getVolume() - 0.1f);
                emu.audio().setVolume(v);
                std::cout << "Volume: " << (v * 100.0f) << "%" << std::endl;
                break;
            }
            case Display::Hotkey::Mute: {
                float v = emu.audio().getVolume();
                emu.audio().setVolume(v > 0.0f ? 0.0f : 1.0f);
                std::cout << (v > 0.0f ? "Muted" : "Unmuted") << std::endl;
                break;
            }
            case Display::Hotkey::Screenshot: {
                std::string path = emu.siblingPath(".ppm");
                if (writeScreenshot(path, emu.framebuffer())) {
                    std::cout << "Screenshot saved: " << path << std::endl;
                }
                break;
            }
            default:
                break;
        }
    }
}

// The windowed main loop: input, hotkeys, emulation, presentation and
// frame pacing all belong to the frontend.
static int runWindowed(Emulator& emu, int scale) {
    Display display(scale);
    if (!display.init()) {
        std::cerr << "Failed to initialize display!" << std::endl;
        return 1;
    }

    // Audio is best-effort: the emulator keeps running silently if no
    // device can be opened.
    AudioBackend audio;
    audio.start(emu.audio());

    printControls();

    bool paused = false;

    // Frame pacing: vsync when available, timer fallback otherwise.
    const double target_frame_ms = 1000.0 / 59.7275;
    Uint64 perf_freq = SDL_GetPerformanceFrequency();
    Uint64 frame_start = SDL_GetPerformanceCounter();
    Uint64 fps_accum_start = frame_start;
    int fps_frames = 0;

    while (emu.isRunning() && !display.shouldClose()) {
        display.poll();
        handleHotkeys(emu, display, paused);

        // Apply merged keyboard+gamepad input to the joypad
        u8 mask = display.getJoypadMask();
        Joypad& jp = emu.joypad();
        jp.setButton(Joypad::BTN_UP,     (mask & Joypad::BTN_UP) != 0);
        jp.setButton(Joypad::BTN_DOWN,   (mask & Joypad::BTN_DOWN) != 0);
        jp.setButton(Joypad::BTN_LEFT,   (mask & Joypad::BTN_LEFT) != 0);
        jp.setButton(Joypad::BTN_RIGHT,  (mask & Joypad::BTN_RIGHT) != 0);
        jp.setButton(Joypad::BTN_A,      (mask & Joypad::BTN_A) != 0);
        jp.setButton(Joypad::BTN_B,      (mask & Joypad::BTN_B) != 0);
        jp.setButton(Joypad::BTN_START,  (mask & Joypad::BTN_START) != 0);
        jp.setButton(Joypad::BTN_SELECT, (mask & Joypad::BTN_SELECT) != 0);

        if (paused) {
            display.render(emu.framebuffer());
            SDL_Delay(16);
            continue;
        }

        if (display.isTurboHeld()) {
            // Fast forward: run several frames without pacing
            for (int i = 0; i < 5; i++) emu.runFrame();
        } else {
            emu.runFrame();
        }

        display.render(emu.framebuffer());

        // Fallback pacing when vsync is unavailable/disabled
        Uint64 now = SDL_GetPerformanceCounter();
        double elapsed_ms = (now - frame_start) * 1000.0 / perf_freq;
        if (elapsed_ms < target_frame_ms) {
            SDL_Delay(static_cast<Uint32>(target_frame_ms - elapsed_ms));
        }
        frame_start = SDL_GetPerformanceCounter();

        // FPS in window title, once per second
        fps_frames++;
        double fps_elapsed = (frame_start - fps_accum_start) * 1000.0 / perf_freq;
        if (fps_elapsed >= 1000.0) {
            display.setTitleFPS(fps_frames * 1000.0 / fps_elapsed);
            fps_frames = 0;
            fps_accum_start = frame_start;
        }
    }

    audio.stop();
    std::cout << "\nEmulation stopped" << std::endl;
    return 0;
}

int main(int argc, char* argv[]) {
    printHeader();

    std::string romPath;
    int headless_frames = -1;
    std::string screenshotPath;
    int scale = 4;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--headless" && i + 1 < argc) {
            headless_frames = std::atoi(argv[++i]);
            if (headless_frames < 0) headless_frames = 0;
        } else if (arg == "--screenshot" && i + 1 < argc) {
            screenshotPath = argv[++i];
        } else if (arg == "--scale" && i + 1 < argc) {
            scale = std::atoi(argv[++i]);
            if (scale < 1) scale = 1;
            if (scale > 8) scale = 8;
        } else if (arg == "--help" || arg == "-h") {
            printUsage(argv[0]);
            return 0;
        } else if (!arg.empty() && arg[0] == '-') {
            std::cerr << "Unknown option: " << arg << std::endl;
            printUsage(argv[0]);
            return 1;
        } else if (romPath.empty()) {
            romPath = arg;
        } else {
            std::cerr << "Unexpected extra argument: " << arg << std::endl;
            printUsage(argv[0]);
            return 1;
        }
    }

    if (romPath.empty()) {
        printUsage(argv[0]);
        std::cerr << "\nExample: " << argv[0] << " roms/tetris.gb\n";
        return 1;
    }

    Emulator emulator;
    if (!emulator.loadROM(romPath)) {
        return 1;
    }

    if (headless_frames >= 0) {
        // Headless testing mode: no window, no audio. Serial output
        // from the ROM (e.g. blargg test results) is echoed to stdout.
        emulator.runHeadless(headless_frames);
        if (!screenshotPath.empty()) {
            if (!writeScreenshot(screenshotPath, emulator.framebuffer())) {
                std::cerr << "Failed to write screenshot: " << screenshotPath << std::endl;
            }
        }
        std::cout << emulator.getSerialOutput() << std::flush;
        return 0;
    }

    int rc = runWindowed(emulator, scale);

    // The AudioBackend held the audio subsystem; display.init()
    // initialized video. Tear SDL down after both are gone.
    SDL_Quit();
    return rc;
}
