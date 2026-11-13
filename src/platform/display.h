#pragma once
#include <SDL2/SDL.h>
#include "core/common/types.h"
#include "core/input/joypad.h"
#include <array>
#include <queue>

/**
 * SDL2 Display & Input
 *
 * Owns the window, renderer, texture, and all input handling:
 *  - Keyboard + game controller merged into one Joypad button mask
 *  - Edge-detected hotkeys (save/load state, reset, pause, fullscreen,
 *    turbo, volume, mute, screenshot, quit)
 *  - Optional integer scaling and fullscreen toggle
 */
class Display {
public:
    static constexpr int GB_WIDTH = 160;
    static constexpr int GB_HEIGHT = 144;

    enum class Hotkey {
        None = 0,
        Quit,
        SaveState,
        LoadState,
        Reset,
        Pause,
        ToggleFullscreen,
        VolumeUp,
        VolumeDown,
        Mute,
        Screenshot
    };

    explicit Display(int scale = 4);
    ~Display();

    bool init();
    void render(const std::array<u8, 160 * 144>& framebuffer);

    // Pumps the SDL event queue; updates button state and hotkeys.
    void poll();

    // Merged Joypad button mask (Joypad::Button bits, 1 = pressed).
    u8 getJoypadMask() const { return joypad_mask; }

    // Hold-to-fast-forward.
    bool isTurboHeld() const { return turbo_held; }

    // Pop the next hotkey event (or Hotkey::None).
    Hotkey nextHotkey();

    // Toggle between windowed and borderless fullscreen (integer scaled).
    void toggleFullscreen();

    void setTitleFPS(double fps);
    bool shouldClose() const { return quit; }

private:
    int scale;
    SDL_Window* window;
    SDL_Renderer* renderer;
    SDL_Texture* texture;
    SDL_GameController* controller;
    bool quit;

    u8 keyboard_mask;   // buttons held on keyboard
    u8 gamepad_mask;   // buttons held on gamepad
    u8 joypad_mask;    // merged
    bool turbo_held;

    std::queue<Hotkey> hotkeys;

    void onKey(SDL_Keycode key, bool down);
    void onControllerButton(Uint8 button, bool down);
};
